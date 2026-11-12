// SPDX-License-Identifier: MIT
// Real native CB construction, independent encoding and concurrent commits.
// Not a hang reproducer: bounded work, no injected GPU waits, 10 s deadline.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <objc/runtime.h>
#include <dispatch/dispatch.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include "../src/POSHQueueCache.h"
#include "../src/POSHAtomicClaim.h"
#include "../src/BoundedCount.hpp"

constexpr unsigned workers=4,width=128,height=128,length=width*height*4;
static bool encode(id<MTLDevice> device,id<MTLCommandQueue> queue,id<MTLRenderPipelineState> state,
    unsigned seed,id<MTLCommandBuffer> &cb,id<MTLBuffer> &source,id<MTLBuffer> &target) {
    source=[device newBufferWithLength:length options:MTLResourceStorageModeShared];
    target=[device newBufferWithLength:length options:MTLResourceStorageModeShared];
    if (!source || !target) return false;
    auto *bytes=(uint8_t *)source.contents;
    for (unsigned j=0;j<length;++j) bytes[j]=(j*17+seed)^(j>>7);
    memset(target.contents,0xcc,length);
    cb=[[queue commandBuffer] retain]; if (!cb) return false;
    MTLTextureDescriptor *desc=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:width height:height mipmapped:NO];
    desc.storageMode=MTLStorageModePrivate;desc.usage=MTLTextureUsageShaderRead;
    id<MTLTexture> texture=[[device newTextureWithDescriptor:desc] autorelease];
    desc.usage=MTLTextureUsageRenderTarget;
    id<MTLTexture> rendered=[[device newTextureWithDescriptor:desc] autorelease];
    id<MTLBlitCommandEncoder> blit=[cb blitCommandEncoder];
    if (!texture || !rendered || !blit) return false;
    [blit copyFromBuffer:source sourceOffset:0 sourceBytesPerRow:width*4 sourceBytesPerImage:length sourceSize:MTLSizeMake(width,height,1) toTexture:texture destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0,0,0)];
    [blit endEncoding];
    MTLRenderPassDescriptor *pass=[MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture=rendered;
    pass.colorAttachments[0].loadAction=MTLLoadActionDontCare;
    pass.colorAttachments[0].storeAction=MTLStoreActionStore;
    id<MTLRenderCommandEncoder> render=[cb renderCommandEncoderWithDescriptor:pass];
    if (!render) return false;
    [render setRenderPipelineState:state];[render setFragmentTexture:texture atIndex:0];
    [render drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];[render endEncoding];
    blit=[cb blitCommandEncoder];if (!blit) return false;
    [blit copyFromTexture:rendered sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(width,height,1) toBuffer:target destinationOffset:0 destinationBytesPerRow:width*4 destinationBytesPerImage:length];
    [blit endEncoding];return true;
}

static uint32_t active(id queue) {
    Ivar ivar=class_getInstanceVariable(object_getClass(queue),"m_numActiveCommandBuffers");
    if (!ivar) abort();
    uint32_t n=0; memcpy(&n,(const char *)(void *)queue+ivar_getOffset(ivar),4); return n;
}
static bool only(id cb) {
    Ivar ivar=class_getInstanceVariable(object_getClass(cb),"m_isOnlyActiveCommandBuffer");
    if (!ivar) abort();
    return *((const char *)(void *)cb+ivar_getOffset(ivar));
}
static bool wait(dispatch_group_t group) {
    return !dispatch_group_wait(group,dispatch_time(DISPATCH_TIME_NOW,10*NSEC_PER_SEC));
}
static NSSet *reports() {
    NSMutableSet *all=[NSMutableSet set];
    bool readable=false;
    for (NSString *dir in @[@"/Library/Logs/DiagnosticReports",@"/Library/Logs/DiagnosticReports/Retired"]) {
        NSArray *names=[[NSFileManager defaultManager] contentsOfDirectoryAtPath:dir error:nil];
        if (!names) continue;
        readable=true;
        for (NSString *name in names) if ([name.pathExtension isEqualToString:@"gpuRestart"]) [all addObject:name];
    }
    return readable?all:nil;
}
static bool temperatureOK(NSString *path) {
    NSDictionary *a=[[NSFileManager defaultManager] attributesOfItemAtPath:path error:nil];
    NSDate *modified=a[NSFileModificationDate];
    NSString *s=[NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:nil];
    NSRange range=[s rangeOfString:@"CPU die temperature: " options:NSBackwardsSearch];
    double degrees=0;
    return modified && -modified.timeIntervalSinceNow<6 && s && range.location!=NSNotFound &&
        sscanf([s substringFromIndex:range.location].UTF8String,"CPU die temperature: %lf",&degrees)==1 &&
        degrees>0 && degrees<75;
}
// Exercise superclass queue-capacity blocking without holding a repair lock.
static int capacity(id<MTLDevice> device,dispatch_queue_t pool,bool &blocked) {
    id<MTLCommandQueue> queue=[device newCommandQueueWithMaxCommandBufferCount:1];
    if (!queue) return 3;
    __block id<MTLCommandBuffer> second=nil;
    dispatch_semaphore_t entered=dispatch_semaphore_create(0),returned=dispatch_semaphore_create(0);
    @autoreleasepool {
        id<MTLCommandBuffer> first=[queue commandBuffer];
        if (!first) return 3;
        dispatch_async(pool,^{ @autoreleasepool {
            dispatch_semaphore_signal(entered);
            second=[[queue commandBuffer] retain];
            dispatch_semaphore_signal(returned);
        } });
        if (dispatch_semaphore_wait(entered,dispatch_time(DISPATCH_TIME_NOW,3*NSEC_PER_SEC))) return 12;
        blocked=dispatch_semaphore_wait(returned,dispatch_time(DISPATCH_TIME_NOW,50*NSEC_PER_MSEC))!=0;
        [first commit];
    }
    // This donor may not enforce a requested limit of one at construction.
    // Preserve the observation; never claim that an unobserved wait was tested.
    if ((blocked && dispatch_semaphore_wait(returned,dispatch_time(DISPATCH_TIME_NOW,10*NSEC_PER_SEC))) ||
        !second || active(queue)!=1 || only(second)!=blocked) return 12;
    @autoreleasepool {
        [second commit]; [second release]; second=nil;
    }
    [queue release]; dispatch_release(entered);dispatch_release(returned);
    return 0;
}
struct StreamResults { std::atomic<unsigned> completed{0},bad{0},errors{0},stopped{0}; };
// No cohort barrier: constructors/encoders overlap other threads' commits and
// real GPU completion. Each thread immediately drops its own CB reference.
static int stream(id<MTLDevice> device,id<MTLRenderPipelineState> state,dispatch_queue_t pool,
    int rounds,NSString *powerLog) {
    id<MTLCommandQueue> queue=[device newCommandQueue];if (!queue) return 3;
    auto results=std::make_shared<StreamResults>();
    dispatch_group_t submitters=dispatch_group_create(),completed=dispatch_group_create();
    dispatch_semaphore_t depth=dispatch_semaphore_create(workers);
    for (unsigned i=0;i<workers;++i) dispatch_group_async(submitters,pool,^{
        for (int n=0;n<rounds && !results->stopped;++n) @autoreleasepool {
            if ((n%8==0 && !temperatureOK(powerLog)) ||
                dispatch_semaphore_wait(depth,dispatch_time(DISPATCH_TIME_NOW,10*NSEC_PER_SEC))) {
                ++results->stopped;break;
            }
            id<MTLCommandBuffer> cb=nil;id<MTLBuffer> source=nil,target=nil;
            if (!encode(device,queue,state,n*31+i*43,cb,source,target)) {
                ++results->stopped;[cb release];[source release];[target release];
                dispatch_semaphore_signal(depth);break;
            }
            dispatch_group_enter(completed);
            [cb addCompletedHandler:^(id<MTLCommandBuffer> done) {
                if (done.status!=MTLCommandBufferStatusCompleted) ++results->errors;
                else if (memcmp(source.contents,target.contents,length)) ++results->bad;
                ++results->completed;dispatch_semaphore_signal(depth);dispatch_group_leave(completed);
            }];
            [cb commit];[cb release];[source release];[target release];
        }
    });
    if (!wait(submitters) || !wait(completed)) std::exit(12);
    const auto count=active(queue);[queue release];
    printf("{\"phase\":\"stream\",\"workers\":%u,\"requested_per_worker\":%d,\"completed\":%u,\"data_mismatches\":%u,\"gpu_errors\":%u,\"stopped\":%u,\"final_active_count\":%u}\n",
        workers,rounds,results->completed.load(),results->bad.load(),results->errors.load(),results->stopped.load(),count);
    dispatch_release(submitters);dispatch_release(completed);dispatch_release(depth);
    return results->completed!=static_cast<unsigned>(rounds)*workers || results->bad || results->errors || results->stopped || count ? 8:0;
}
int main(int argc,char **argv) {
    @autoreleasepool {
        int rounds=20;
        if (argc!=4 || !xe::boundedCount(argv[2],100,rounds)) return 2;
        const bool atomic=!strcmp(argv[1],"atomic") || !strcmp(argv[1],"queue-cache-atomic");
        const bool cache=!strcmp(argv[1],"queue-cache") || !strcmp(argv[1],"queue-cache-atomic");
        if (!atomic && !cache && strcmp(argv[1],"stock")) return 2;
        NSString *powerLog=@(argv[3]);
        if (!temperatureOK(powerLog)) { fprintf(stderr,"Stop: missing/stale/hot temperature sample\n");return 14; }
        NSSet *baseline=[reports() retain]; if (!baseline) return 4;
        id<MTLDevice> device=MTLCreateSystemDefaultDevice(); if (!device) return 3;
        if (cache && !installPOSHQueueCache(device.registryID)) return 4;
        if (atomic && !installPOSHAtomicClaim(device.registryID)) return 4;
        dispatch_queue_t pool=dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0);
        bool capacityBlocked=false;
        if (int c=capacity(device,pool,capacityBlocked)) { fprintf(stderr,"Queue capacity test failed: %d\n",c);return c; }
        NSError *error=nil;
        NSString *shader=@"#include <metal_stdlib>\nusing namespace metal;\n"
          "struct V { float4 p [[position]]; float2 uv; };\n"
          "vertex V triangle(uint i [[vertex_id]]) { const float2 p[3]={float2(-1,-1),float2(3,-1),float2(-1,3)}; V v; v.p=float4(p[i],0,1); v.uv=p[i]*float2(.5,-.5)+.5; return v; }\n"
          "fragment float4 samplePixel(V v [[stage_in]],texture2d<float> input [[texture(0)]]) { constexpr sampler s(coord::normalized,address::clamp_to_edge,filter::nearest); return input.sample(s,v.uv); }\n";
        id<MTLLibrary> library=[device newLibraryWithSource:shader options:nil error:&error];
        if (!library) return 6;
        MTLRenderPipelineDescriptor *pipeline=[MTLRenderPipelineDescriptor new];
        pipeline.vertexFunction=[[library newFunctionWithName:@"triangle"] autorelease];
        pipeline.fragmentFunction=[[library newFunctionWithName:@"samplePixel"] autorelease];
        pipeline.colorAttachments[0].pixelFormat=MTLPixelFormatBGRA8Unorm;
        id<MTLRenderPipelineState> state=[device newRenderPipelineStateWithDescriptor:pipeline error:&error];
        [pipeline release];[library release];
        if (!state) return 6;
        id<MTLCommandQueue> queue=[device newCommandQueue]; if (!queue) return 3;
        unsigned wrongCount=0,wrongOnly=0,mismatch=0,errors=0,commits=0;
        int failedBeforeSubmit=-1;
        uint32_t observedCount=0;unsigned observedOnly=0;
        for (int round=0;round<rounds;++round) {
            if (!temperatureOK(powerLog) || ![reports() isSubsetOfSet:baseline]) return 14;
            @autoreleasepool {
                // Four independently owned resources/encoders, one shared queue.
                id<MTLCommandBuffer> buffers[workers]{};
                id<MTLBuffer> source[workers]{},target[workers]{};
                auto *out=buffers;auto *src=source;auto *dst=target;
                std::atomic<unsigned> encoded{0};auto *encodedCount=&encoded;
                dispatch_group_t group=dispatch_group_create();
                dispatch_semaphore_t start=dispatch_semaphore_create(0);
                for (unsigned i=0;i<workers;++i) dispatch_group_async(group,pool,^{ @autoreleasepool {
                    dispatch_semaphore_wait(start,DISPATCH_TIME_FOREVER);
                    if (encode(device,queue,state,round*31+i*43,out[i],src[i],dst[i])) ++*encodedCount;
                } });
                for (unsigned i=0;i<workers;++i) dispatch_semaphore_signal(start);
                if (!wait(group)) return 12;
                if (encoded!=workers) return 5;
                unsigned onlyCount=0;
                for (auto cb:buffers) { if (!cb) return 5;onlyCount+=only(cb); }
                observedOnly=onlyCount;observedCount=active(queue);
                wrongOnly+=onlyCount!=1;wrongCount+=observedCount!=workers;
                // Stop before submitting a cohort with broken CPU ownership.
                if (wrongCount || wrongOnly) {
                    failedBeforeSubmit=round;
                    for (unsigned i=0;i<workers;++i) { [buffers[i] release];[source[i] release];[target[i] release]; }
                    dispatch_release(group);dispatch_release(start);
                    break;
                }
                dispatch_group_t completed=dispatch_group_create();
                for (auto cb:buffers) {
                    dispatch_group_enter(completed);
                    [cb addCompletedHandler:^(id<MTLCommandBuffer>) { dispatch_group_leave(completed); }];
                    dispatch_group_async(group,pool,^{ @autoreleasepool { [cb commit]; } });
                }
                if (!wait(group) || !wait(completed)) return 12;
                wrongCount+=active(queue)!=0;
                for (unsigned i=0;i<workers;++i) {
                    ++commits;
                    errors+=buffers[i].status!=MTLCommandBufferStatusCompleted;
                    mismatch+=memcmp(source[i].contents,target[i].contents,length)!=0;
                    [buffers[i] release];[source[i] release];[target[i] release];
                }
                dispatch_release(group);dispatch_release(completed);dispatch_release(start);
                if (errors || mismatch || wrongCount) break;
            }
        }
        [queue release];
        const bool cohortPassed=commits==static_cast<unsigned>(rounds)*workers && !wrongCount && !wrongOnly && !mismatch && !errors;
        const int streaming=cohortPassed?stream(device,state,pool,rounds,powerLog):8;
        [state release];
        NSSet *after=reports();
        const bool noReport=after && [after isSubsetOfSet:baseline];
        [baseline release];[device release];
        printf("{\"mode\":\"%s\",\"rounds\":%d,\"workers\":%u,\"gpu_commits\":%u,\"capacity_probe_commits\":2,\"capacity_probe_blocked\":%s,\"active_count_failures\":%u,\"only_active_failures\":%u,\"pre_submit_failure_round\":%d,\"last_cohort_active_count\":%u,\"last_cohort_only_active\":%u,\"data_mismatches\":%u,\"gpu_errors\":%u,\"new_gpu_report\":%s}\n",argv[1],rounds,workers,commits,capacityBlocked?"true":"false",wrongCount,wrongOnly,failedBeforeSubmit,observedCount,observedOnly,mismatch,errors,noReport?"false":"true");
        return !cohortPassed || streaming || !noReport ? 8:0;
    }
}
