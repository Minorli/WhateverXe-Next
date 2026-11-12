// SPDX-License-Identifier: MIT
// Same native queue reused after abandoned command buffers. No GPU commits.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <objc/runtime.h>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <dispatch/dispatch.h>
#include <malloc/malloc.h>
#include "../src/POSHQueueCache.h"
#include "../src/POSHAtomicClaim.h"
#include "../src/GPUHealth.hpp"

static uint32_t active(id queue) {
    Ivar field=class_getInstanceVariable(object_getClass(queue),"m_numActiveCommandBuffers");
    if (!field) abort();
    uint32_t value=0;
    memcpy(&value,(const char *)(void *)queue+ivar_getOffset(field),sizeof(value));
    return value;
}
static bool only(id cb) {
    Ivar field=class_getInstanceVariable(object_getClass(cb),"m_isOnlyActiveCommandBuffer");
    if (!field) abort();
    return *((const char *)(void *)cb+ivar_getOffset(field));
}
static std::atomic<unsigned> destroyed{0},reentries{0},reentryFailures{0};
@interface XEAbandonWitness : NSObject {
@public
    id<MTLCommandQueue> queue;
}
@end
@implementation XEAbandonWitness
- (void)dealloc {
    if (queue) {
        ++reentries;
        const auto before=active(queue);
        // Associated-object destructors run inside native superclass cleanup.
        // The outer abandoned claim must still be held while this reenters.
        if (!before) ++reentryFailures;
        @autoreleasepool {
            id<MTLCommandBuffer> nested=[queue commandBuffer];
            if (!nested || only(nested) || active(queue)!=before+1) ++reentryFailures;
            id<MTLBlitCommandEncoder> encoder=[nested blitCommandEncoder];
            if (!encoder) ++reentryFailures;
            [encoder endEncoding];
        }
        if (active(queue)!=before) ++reentryFailures;
        [queue release];
    }
    ++destroyed; [super dealloc];
}
@end
static char witnessKey;
static uint64_t mallocBytes() {
    malloc_statistics_t stats{};malloc_zone_statistics(nullptr,&stats);return stats.size_in_use;
}

int main(int argc,char **argv) {
    if (argc!=2) return 2;
    const bool reclaim=!strcmp(argv[1],"lifecycle") || !strcmp(argv[1],"queue-cache-lifecycle");
    const bool atomic=reclaim || !strcmp(argv[1],"atomic") || !strcmp(argv[1],"queue-cache-atomic");
    const bool cache=!strcmp(argv[1],"queue-cache") || !strcmp(argv[1],"queue-cache-atomic") || !strcmp(argv[1],"queue-cache-lifecycle");
    if (!atomic && !cache && strcmp(argv[1],"stock")) return 2;
    if (!xe::noGPURecovery()) return 14;
    @autoreleasepool {
        id<MTLDevice> device=MTLCreateSystemDefaultDevice(); if (!device) return 3;
        if (cache && !installPOSHQueueCache(device.registryID)) return 4;
        if (atomic && !installPOSHAtomicClaim(device.registryID,reclaim)) return 4;
        if (atomic && (installPOSHAtomicClaim(0,reclaim) ||
            !installPOSHAtomicClaim(device.registryID,reclaim))) return 4;
        id<MTLCommandQueue> queue=[device newCommandQueue]; if (!queue) return 3;
        unsigned stale=0,wrongOnly=0;
        constexpr unsigned rounds=32;
        for (unsigned n=0;n<rounds;++n) {
            @autoreleasepool {
                id<MTLCommandBuffer> cb=n%2?[queue commandBufferWithUnretainedReferences]:[queue commandBuffer];
                if (!cb) return 5;
                // No other buffers are alive on this queue, even after the
                // previous round. A retained teardown witness proves disposal.
                wrongOnly+=!only(cb);
                XEAbandonWitness *witness=[XEAbandonWitness new];
                objc_setAssociatedObject(cb,&witnessKey,witness,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
                [witness release];
                if (n%3==1) {
                    id<MTLBlitCommandEncoder> encoder=[cb blitCommandEncoder];
                    if (!encoder) return 5;
                    [encoder endEncoding];
                }
            }
            stale+=active(queue)!=0;
            if (destroyed!=n+1) return 6;
        }
        uint32_t remaining=active(queue);
        unsigned overlapFailures=0,concurrentFailures=0,memoryRounds=0;
        int64_t mallocGrowth=0;
        if (!stale && !wrongOnly && !remaining) {
            // One cache borrower and one native-private CB. Abandon the first
            // while the other remains alive, then construct another overlap.
            id<MTLCommandBuffer> first=nil,second=nil;
            @autoreleasepool {
                first=[[queue commandBuffer] retain];
                second=[[queue commandBufferWithUnretainedReferences] retain];
                overlapFailures+=!first || !second || !only(first) || only(second) || active(queue)!=2;
            }
            [first release];
            overlapFailures+=active(queue)!=1;
            @autoreleasepool {
                id<MTLCommandBuffer> third=[queue commandBuffer];
                overlapFailures+=!third || only(third) || active(queue)!=2;
                [second release];
                overlapFailures+=active(queue)!=1;
            }
            overlapFailures+=active(queue)!=0;
            for (unsigned n=0;n<8;++n) {
                @autoreleasepool {
                    id<MTLCommandBuffer> cb=[queue commandBuffer];
                    if (!cb || !only(cb)) return 5;
                    XEAbandonWitness *witness=[XEAbandonWitness new];
                    witness->queue=[queue retain];
                    objc_setAssociatedObject(cb,&witnessKey,witness,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
                    [witness release];
                }
                overlapFailures+=active(queue)!=0;
            }
            // Concurrent constructors AND final release on one reused queue.
            dispatch_group_t group=dispatch_group_create();
            for (unsigned i=0;i<4;++i) dispatch_group_async(group,
                dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{
                    for (unsigned n=0;n<64;++n) @autoreleasepool {
                        id<MTLCommandBuffer> cb=[queue commandBuffer];
                        if (!cb) abort();
                        id<MTLBlitCommandEncoder> encoder=[cb blitCommandEncoder];
                        if (!encoder) abort();
                        [encoder endEncoding];
                    }
                });
            if (dispatch_group_wait(group,dispatch_time(DISPATCH_TIME_NOW,10*NSEC_PER_SEC))) return 12;
            concurrentFailures+=active(queue)!=0;
            dispatch_release(group);
            const auto before=mallocBytes();
            for (unsigned n=0;n<512;++n) {
                @autoreleasepool {
                    id<MTLCommandBuffer> cb=[queue commandBuffer];
                    if (!cb || !only(cb)) return 5;
                    id<MTLBlitCommandEncoder> encoder=[cb blitCommandEncoder];
                    if (!encoder) return 5;
                    [encoder endEncoding];
                }
                ++memoryRounds;
                concurrentFailures+=active(queue)!=0;
            }
            mallocGrowth=static_cast<int64_t>(mallocBytes())-static_cast<int64_t>(before);
            remaining=active(queue);
        }
        [queue release]; [device release];
        const auto stats=poshQueueCacheStats();
        const auto claims=poshActiveStats();
        printf("{\"mode\":\"%s\",\"rounds\":%u,\"destroyed_witnesses\":%u,\"stale_active_rounds\":%u,\"wrong_only_active\":%u,\"final_active\":%u,\"cache_allocated\":%llu,\"cache_destroyed\":%llu,\"gpu_commits\":0,\"reentries\":%u,\"reentry_failures\":%u,\"overlap_failures\":%u,\"concurrent_failures\":%u,\"tracked\":%llu,\"committed\":%llu,\"abandoned\":%llu,\"destroyed_tokens\":%llu,\"memory_rounds\":%u,\"malloc_growth_bytes\":%lld,\"private_tracked\":%llu,\"private_abandoned_freed\":%llu}\n",
            argv[1],rounds,destroyed.load(),stale,wrongOnly,remaining,stats.allocated,stats.destroyed,
            reentries.load(),reentryFailures.load(),overlapFailures,concurrentFailures,
            claims.tracked,claims.committed,claims.abandoned,claims.destroyed,memoryRounds,mallocGrowth,
            claims.privateTracked,claims.privateAbandonedFreed);
        return stale || wrongOnly || remaining || reentryFailures || overlapFailures || concurrentFailures ||
            (reclaim && (claims.tracked!=819 || claims.committed || claims.abandoned!=819 || claims.destroyed!=819 ||
                claims.privateTracked!=claims.privateAbandonedFreed || (!cache && claims.privateTracked<553) ||
                (cache && claims.privateTracked) || memoryRounds!=512 || mallocGrowth>2*1024*1024)) ||
            (cache && (stats.allocated!=1 || stats.destroyed!=1 || stats.refused)) ? 8:0;
    }
}
