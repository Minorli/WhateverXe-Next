// SPDX-License-Identifier: MIT
// Native ownership/lifetime check: creates queues and abandoned CBs, no commits.
// --concurrent reproduces a native active-count race; queue-cache can also
// alias live POSH storage. Keep these failures visible as deployment blockers.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <objc/runtime.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <dispatch/dispatch.h>
#include "../src/POSHQueueCache.h"
#include "../src/POSHAtomicClaim.h"

static void *pointer(id object,const char *name) {
    Ivar ivar=class_getInstanceVariable(object_getClass(object),name); assert(ivar);
    void *result=nullptr;
    memcpy(&result,(const char *)(void *)object+ivar_getOffset(ivar),sizeof(result));
    return result;
}
static bool only(id object) {
    Ivar ivar=class_getInstanceVariable(object_getClass(object),"m_isOnlyActiveCommandBuffer"); assert(ivar);
    return *((const char *)(void *)object+ivar_getOffset(ivar));
}
int main(int argc,char **argv) {
    const bool concurrent=argc>=2 && !strcmp(argv[1],"--concurrent");
    const bool stock=argc==3 && !strcmp(argv[2],"stock");
    const bool atomic=argc==3 && (!strcmp(argv[2],"atomic") || !strcmp(argv[2],"queue-cache-atomic"));
    const bool cache=!stock && !(argc==3 && !strcmp(argv[2],"atomic"));
    if (argc>3 || (argc>=2 && !concurrent) ||
        (argc==3 && !stock && !atomic && strcmp(argv[2],"queue-cache"))) return 2;
    @autoreleasepool {
        id<MTLDevice> device=MTLCreateSystemDefaultDevice(); if (!device) return 3;
        assert(!installPOSHQueueCache(0));
        assert(!installPOSHAtomicClaim(0));
        if (cache && !installPOSHQueueCache(device.registryID)) { fprintf(stderr,"Queue-cache guard refused\n"); return 4; }
        if (atomic && !installPOSHAtomicClaim(device.registryID)) { fprintf(stderr,"Atomic-claim guard refused\n"); return 4; }
        for (unsigned n=0;n<100;++n) {
            @autoreleasepool {
                id<MTLCommandQueue> queue=[device newCommandQueue]; assert(queue);
                void *shared=pointer(queue,"m_pPoshUsedAsTarget"); assert(cache?!!shared:!shared);
                // First borrows, overlapping second stays native-private.
                id<MTLCommandBuffer> first=[[queue commandBuffer] retain];
                id<MTLCommandBuffer> second=[[queue commandBufferWithUnretainedReferences] retain];
                assert(first && second && only(first) && !only(second));
                assert(cache?pointer(first,"_pPoshUsedAsTarget")==shared:pointer(first,"_pPoshUsedAsTarget")!=nullptr);
                assert(pointer(second,"_pPoshUsedAsTarget")!=shared);
                // Release the client's queue reference before its CBs. Native
                // lifetime must keep the cache alive until both are disposed.
                const auto before=poshQueueCacheStats();
                [queue release];
                assert(poshQueueCacheStats().destroyed==before.destroyed);
                [first release]; [second release];
            }
        }
        if (concurrent) {
            unsigned aliases=0,wrongCount=0,zeroBorrowers=0,maxBorrowers=0,maxOnly=0,privateAliases=0;
            constexpr unsigned rounds=300,workers=4;
            dispatch_queue_t pool=dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0);
            for (unsigned n=0;n<rounds;++n) {
                @autoreleasepool {
                    id<MTLCommandQueue> queue=[device newCommandQueue]; assert(queue);
                    void *shared=pointer(queue,"m_pPoshUsedAsTarget"); assert(cache?!!shared:!shared);
                    // Start native CB constructors together. Keep all four CBs
                    // alive and uncommitted: two borrowers of the same cache
                    // here would share mutable CPU bookkeeping during encoding.
                    id<MTLCommandBuffer> buffers[workers]{};
                    id<MTLCommandBuffer> *output=buffers;
                    dispatch_group_t group=dispatch_group_create();
                    dispatch_semaphore_t ready=dispatch_semaphore_create(0);
                    dispatch_semaphore_t start=dispatch_semaphore_create(0);
                    for (unsigned i=0;i<workers;++i) dispatch_group_async(group,pool,^{
                        @autoreleasepool {
                            dispatch_semaphore_signal(ready);
                            dispatch_semaphore_wait(start,DISPATCH_TIME_FOREVER);
                            output[i]=[[queue commandBuffer] retain];
                        }
                    });
                    for (unsigned i=0;i<workers;++i) {
                        const auto readyResult=dispatch_semaphore_wait(ready,dispatch_time(DISPATCH_TIME_NOW,3*NSEC_PER_SEC));
                        assert(!readyResult);
                    }
                    for (unsigned i=0;i<workers;++i) dispatch_semaphore_signal(start);
                    const auto groupResult=dispatch_group_wait(group,dispatch_time(DISPATCH_TIME_NOW,3*NSEC_PER_SEC));
                    assert(!groupResult);
                    unsigned borrowers=0,onlyCount=0;
                    for (auto cb:buffers) { assert(cb); borrowers+=shared && pointer(cb,"_pPoshUsedAsTarget")==shared; onlyCount+=only(cb); }
                    if (borrowers>1) ++aliases;
                    if (cache && borrowers==0) ++zeroBorrowers;
                    if (borrowers>maxBorrowers) maxBorrowers=borrowers;
                    if (onlyCount>maxOnly) maxOnly=onlyCount;
                    for (unsigned a=0;a<workers;++a) for (unsigned b=a+1;b<workers;++b)
                        if (pointer(buffers[a],"_pPoshUsedAsTarget")==pointer(buffers[b],"_pPoshUsedAsTarget") &&
                            pointer(buffers[a],"_pPoshUsedAsTarget")!=shared) ++privateAliases;
                    Ivar active=class_getInstanceVariable(object_getClass(queue),"m_numActiveCommandBuffers"); assert(active);
                    uint32_t count=0;memcpy(&count,(const char *)(void *)queue+ivar_getOffset(active),4);
                    if (count!=workers) ++wrongCount;
                    [queue release];
                    for (auto cb:buffers) [cb release];
                    dispatch_release(group);dispatch_release(ready);dispatch_release(start);
                }
            }
            const auto s=poshQueueCacheStats();
            printf("{\"mode\":\"%s\",\"concurrent_rounds\":%u,\"workers\":%u,\"cache_alias_rounds\":%u,\"zero_cache_borrower_rounds\":%u,\"max_cache_borrowers\":%u,\"max_only_active_buffers\":%u,\"private_aliases\":%u,\"active_count_failures\":%u,\"allocated\":%llu,\"destroyed\":%llu,\"refused\":%llu,\"gpu_commits\":0}\n",
                atomic?(cache?"queue-cache-atomic":"atomic"):stock?"stock":"queue-cache",rounds,workers,aliases,zeroBorrowers,maxBorrowers,maxOnly,privateAliases,wrongCount,s.allocated,s.destroyed,s.refused);
            const uint64_t expectedQueues=cache?100+rounds:0;
            return aliases || zeroBorrowers || maxOnly!=1 || privateAliases || wrongCount || s.allocated!=expectedQueues || s.destroyed!=expectedQueues || s.refused ? 8:0;
        }
        const auto s=poshQueueCacheStats();
        printf("{\"queues\":100,\"allocated\":%llu,\"destroyed\":%llu,\"refused\":%llu,\"gpu_commits\":0}\n",s.allocated,s.destroyed,s.refused);
        return s.allocated==100 && s.destroyed==100 && !s.refused ? 0:7;
    }
}
