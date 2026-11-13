// SPDX-License-Identifier: MIT
// Real GPU work, bounded depth, immediate client CB release; process-local hooks.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <malloc/malloc.h>
#include <mach/mach.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include "../src/POSHDeferred.h"
#include "../src/POSHQueueCache.h"
#include "../src/BoundedCount.hpp"
#include "../src/GPUHealth.hpp"

struct Results { std::atomic<unsigned> done{0}, bad{0}, errors{0}; };
static void memory(const char *phase, unsigned n) {
    malloc_statistics_t m{}; malloc_zone_statistics(nullptr,&m);
    task_vm_info_data_t v{}; mach_msg_type_number_t size=TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(),TASK_VM_INFO,(task_info_t)&v,&size)!=KERN_SUCCESS) std::exit(14);
    printf("{\"phase\":\"%s\",\"submitted\":%u,\"malloc_bytes\":%zu,\"footprint_bytes\":%llu}\n",phase,n,m.size_in_use,v.phys_footprint);
    fflush(stdout);
}
int main(int argc,char **argv) {
    @autoreleasepool {
        const char *mode=argc>1?argv[1]:"texture";
        int count=200;
        if (argc>4 || (argc>2 && !xe::boundedCount(argv[2],10000,count))) return 2;
        bool fix=argc>3 && strcmp(argv[3],"deferred")==0;
        bool queueCache=argc>3 && strcmp(argv[3],"queue-cache")==0;
        bool texture=strcmp(mode,"texture")==0, mixed=strcmp(mode,"mixed")==0;
        bool abandoned=strcmp(mode,"abandoned")==0;
        if ((!texture && !mixed && !abandoned) || count<1 || count>10000) return 2;
        if (argc>3 && !fix && !queueCache && strcmp(argv[3],"stock")!=0) return 2;
        if (!xe::noGPURecovery()) return 14;
        id<MTLDevice> device=MTLCreateSystemDefaultDevice(); if (!device) return 3;
        if (queueCache && !installPOSHQueueCache(device.registryID)) { fprintf(stderr,"Queue-cache guard refused\n"); return 4; }
        id<MTLCommandQueue> queues[2]={[device newCommandQueue],[device newCommandQueue]};
        if (!queues[0] || !queues[1]) return 3;
        if (fix && !installPOSHDeferredFix()) { fprintf(stderr,"Deferred guard refused\n"); return 4; }
        auto results=std::make_shared<Results>();
        dispatch_group_t group=dispatch_group_create();
        dispatch_semaphore_t depth=dispatch_semaphore_create(4);
        memory("start",0);
        auto start=std::chrono::steady_clock::now();
        for (int n=0;n<count;++n) {
            if (n%16==0 && !xe::noGPURecovery()) return 14;
            if (results->bad || results->errors) return 7;
            @autoreleasepool {
                if (abandoned) { id<MTLCommandBuffer> cb=[queues[n&1] commandBuffer]; if (!cb) return 5; continue; }
                if (dispatch_semaphore_wait(depth,dispatch_time(DISPATCH_TIME_NOW,10*NSEC_PER_SEC))) {
                    fprintf(stderr,"In-flight depth timed out; stop GPU tests\n"); return 12;
                }
                const NSUInteger width=256+64*(n%8),height=256+64*(n%4);
                const NSUInteger length=width*height*4;
                id<MTLBuffer> source=[device newBufferWithLength:length options:MTLResourceStorageModeShared];
                id<MTLBuffer> target=[device newBufferWithLength:length options:MTLResourceStorageModeShared];
                id<MTLBuffer> staging=mixed?[device newBufferWithLength:length options:MTLResourceStorageModePrivate]:nil;
                if (!source || !target || (mixed && !staging)) return 5;
                auto *input=(unsigned char *)source.contents;
                for (NSUInteger i=0;i<length;++i) input[i]=static_cast<unsigned char>((i*17+n*23)^(i>>8));
                memset(target.contents,0xcc,length);
                id<MTLCommandBuffer> cb=[queues[n&1] commandBuffer]; if (!cb) return 5;
                id<MTLBlitCommandEncoder> encoder=[cb blitCommandEncoder]; if (!encoder) return 5;
                if (texture) {
                    MTLTextureDescriptor *desc=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:(n&1?MTLPixelFormatBGRA8Unorm:MTLPixelFormatRGBA8Unorm) width:width height:height mipmapped:NO];
                    desc.storageMode=MTLStorageModePrivate; desc.usage=MTLTextureUsageShaderRead|MTLTextureUsageRenderTarget;
                    id<MTLTexture> image=[device newTextureWithDescriptor:desc]; if (!image) return 5;
                    [encoder copyFromBuffer:source sourceOffset:0 sourceBytesPerRow:width*4 sourceBytesPerImage:length sourceSize:MTLSizeMake(width,height,1) toTexture:image destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0,0,0)];
                    [encoder copyFromTexture:image sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(width,height,1) toBuffer:target destinationOffset:0 destinationBytesPerRow:width*4 destinationBytesPerImage:length];
                } else {
                    // Ordered fill + aligned 2D copy + misaligned 3D copy in one CB.
                    [encoder fillBuffer:staging range:NSMakeRange(0,length) value:0];
                    [encoder copyFromBuffer:source sourceOffset:0 toBuffer:staging destinationOffset:0 size:length];
                    [encoder copyFromBuffer:staging sourceOffset:4 toBuffer:target destinationOffset:0 size:length-4];
                }
                [encoder endEncoding];
                dispatch_group_enter(group);
                [cb addCompletedHandler:^(id<MTLCommandBuffer> completed) {
                    if (completed.status!=MTLCommandBufferStatusCompleted) {
                        if (results->errors.fetch_add(1)==0) fprintf(stderr,"GPU CB failed: %s\n",completed.error.description.UTF8String);
                    } else {
                        const auto *expected=(const unsigned char *)source.contents+(mixed?4:0);
                        if (memcmp(expected,target.contents,length-(mixed?4:0))!=0) ++results->bad;
                        if (mixed) {
                            const auto *last=(const unsigned char *)target.contents+length-4;
                            if (last[0]!=0xcc || last[1]!=0xcc || last[2]!=0xcc || last[3]!=0xcc) ++results->bad;
                        }
                    }
                    ++results->done; dispatch_semaphore_signal(depth); dispatch_group_leave(group);
                }];
                [cb commit];
                // cb and encoder leave the autoreleasepool immediately. No
                // waitUntilCompleted and no capture of cb in the handler.
            }
            if ((n+1)%500==0) memory("progress",n+1);
        }
        if (dispatch_group_wait(group,dispatch_time(DISPATCH_TIME_NOW,10*NSEC_PER_SEC))) return 12;
        if (!xe::noGPURecovery()) return 14;
        if (results->bad || results->errors) return 7;
        // Drain each queue: user completion blocks may precede native destruction
        // of the final CB. Sentinels don't count toward workload verification.
        for (auto queue:queues) { @autoreleasepool { auto cb=[queue commandBuffer]; [cb commit]; [cb waitUntilCompleted]; if (cb.status!=MTLCommandBufferStatusCompleted) return 6; } }
        memory("end",count);
        const auto s=poshDeferredStats();
        const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        printf("{\"mode\":\"%s\",\"candidate\":\"%s\",\"submitted\":%d,\"completed\":%u,\"mismatches\":%u,\"gpu_errors\":%u,\"seconds\":%.3f,\"tracked\":%llu,\"detached\":%llu,\"disposed\":%llu,\"rejected\":%llu,\"dealloc_inflight\":%llu,\"uncommitted\":%llu}\n",mode,fix?"deferred":queueCache?"queue-cache":"stock",count,results->done.load(),results->bad.load(),results->errors.load(),seconds,s.tracked,s.detached,s.disposed,s.rejected,s.deallocInFlight,s.uncommitted);
        if (queueCache) {
            const auto q=poshQueueCacheStats();
            printf("{\"queue_cache\":true,\"allocated\":%llu,\"destroyed\":%llu,\"refused\":%llu}\n",q.allocated,q.destroyed,q.refused);
            if (q.allocated!=2 || q.refused) return 10;
        }
        return results->bad || results->errors || (!abandoned && results->done!=static_cast<unsigned>(count)) ||
            (fix && (s.rejected || !s.tracked || s.detached!=s.disposed)) ? 7:0;
    }
}
