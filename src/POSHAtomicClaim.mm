// SPDX-License-Identifier: MIT
// Narrow, process-private native counter repair. No lock spans superclass init,
// allocator calls, queue capacity waits, callbacks, or native commit.
#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#import <IOKit/IOKitLib.h>
#include <CommonCrypto/CommonDigest.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <dlfcn.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <climits>
#include <atomic>
#include <malloc/malloc.h>
#include <new>
#include "POSHAtomicClaim.h"
#include "POSHAtomicCode.hpp"

static std::atomic<uint64_t> tracked{0}, committedCount{0}, abandoned{0}, destroyed{0};
static std::atomic<uint64_t> privateTracked{0}, privateAbandonedFreed{0};
@interface XEPOSHActiveToken : NSObject {
@public
    id queue;
    std::atomic<bool> commitEntered;
    void *privatePosh;
}
@end
@implementation XEPOSHActiveToken
- (void)dealloc { [queue release]; ++destroyed; [super dealloc]; }
@end

namespace {
constexpr char path[]="/Library/GPUBundles/AppleIntelTGLGraphicsMTLDriver.bundle/Contents/MacOS/AppleIntelTGLGraphicsMTLDriver";
constexpr char expected[]="1e5e4f5bd76ff567a102125dfa3c74a255934d45c753a88aedcbfac7dab53c6c";
bool installed, reclaimInstalled;
IMP originalInit, originalCommit, originalDealloc;
ptrdiff_t queueOffset;
int32_t poshOffset,onlyOffset,sharedOffset;
int32_t activeOffset;
char tokenKey;
void *pointer(id object,ptrdiff_t offset) {
    void *p=nullptr;memcpy(&p,(char *)(void *)object+offset,sizeof(p));return p;
}
[[noreturn]] void unsupported(const char *reason) {
    fprintf(stderr,"POSH lifecycle diagnostic stopped: %s\n",reason);
    std::abort();
}
id init(id object,SEL selector,id queue,bool retained,bool debug) {
    // The exact native init can throw before OR after its claim. A wrapper
    // cannot infer the point reached from the queue's concurrent count. Until
    // instruction-level unwind ownership is proven, never resume this process
    // with an untracked claim or pretend an allocation exception was repaired.
    try {
        @try {
            id result=((id (*)(id,SEL,id,bool,bool))originalInit)(object,selector,queue,retained,debug);
            // The native nil return is before the claim, in superclass init.
            if (!result) return nil;
            id nativeQueue=nil;
            memcpy(&nativeQueue,(char *)(void *)result+queueOffset,sizeof(nativeQueue));
            if (nativeQueue!=queue || objc_getAssociatedObject(result,&tokenKey))
                unsupported("unexpected successful init ownership");
            XEPOSHActiveToken *token=[XEPOSHActiveToken new];
            if (!token) unsupported("tracking allocation failed");
            token->queue=[queue retain]; token->commitEntered.store(false,std::memory_order_relaxed);
            const auto bytes=(const char *)(void *)result;
            void *posh=pointer(result,poshOffset);
            if (bytes[onlyOffset] && posh && pointer(queue,sharedOffset)!=posh) {
                // This exact init used operator new(0x12010); native dealloc
                // deliberately skips delete for only-active, even unsubmitted.
                if (malloc_size(posh)!=77824) unsupported("unexpected private POSH allocation");
                token->privatePosh=posh; ++privateTracked;
            }
            objc_setAssociatedObject(result,&tokenKey,token,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
            if (objc_getAssociatedObject(result,&tokenKey)!=token) unsupported("tracking association failed");
            ++tracked; [token release];
            return result;
        } @catch (id exception) {
            (void)exception; unsupported("native/tracking init Objective-C exception");
        }
    } catch (...) { unsupported("native init C++ exception; rollback not proven"); }
}
void commit(id object,SEL selector) {
    XEPOSHActiveToken *token=(XEPOSHActiveToken *)[objc_getAssociatedObject(object,&tokenKey) retain];
    if (!token || token->commitEntered.exchange(true,std::memory_order_acq_rel))
        unsupported("missing claim or duplicate commit");
    // Native commit's atomic decrement remains in place and occurs BEFORE
    // any callback/superclass commit. Mark entry first, not MTL status later.
    // A retained token survives a synchronous completion / native disposal.
    ++committedCount;
    @try { ((void (*)(id,SEL))originalCommit)(object,selector); }
    @finally { [token release]; }
}
void dealloc(id object,SEL selector) {
    XEPOSHActiveToken *token=(XEPOSHActiveToken *)[objc_getAssociatedObject(object,&tokenKey) retain];
    if (token && token->privatePosh &&
        (!*((const char *)(void *)object+onlyOffset) || pointer(object,poshOffset)!=token->privatePosh ||
         pointer(token->queue,sharedOffset)==token->privatePosh))
        unsupported("private POSH ownership changed before cleanup");
    try {
        @try { ((void (*)(id,SEL))originalDealloc)(object,selector); }
        @catch (id exception) { (void)exception; unsupported("native cleanup Objective-C exception"); }
    } catch (...) { unsupported("native cleanup C++ exception"); }
    // object no longer exists. The retained token independently owns queue.
    // Keep the claim through state heaps, all descriptor/superclass cleanup
    // AND associated-object destructors, which may reenter queue construction.
    if (token && !token->commitEntered.load(std::memory_order_acquire)) {
        // No commit entered in this supported client. Native dealloc has now
        // finished every CPU descriptor consumer; no GPU owns this private
        // allocation. Shared cache and submitted buffers never enter this free.
        if (token->privatePosh) {
            ::operator delete(token->privatePosh);token->privatePosh=nullptr;
            ++privateAbandonedFreed;
        }
        auto *count=(uint32_t *)((char *)(void *)token->queue+activeOffset);
        uint32_t prior=__atomic_load_n(count,__ATOMIC_ACQUIRE);
        do {
            if (!prior) unsupported("abandoned claim would underflow");
        } while (!__atomic_compare_exchange_n(count,&prior,prior-1,true,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE));
        ++abandoned;
    }
    [token release];
}
bool diagnosticProcess() {
    const char *p=getprogname();
    return p && (!strcmp(p,"posh-queue-check") || !strcmp(p,"metal-surface-check") ||
        !strcmp(p,"metal-async-check") || !strcmp(p,"metal-memory-check") || !strcmp(p,"posh-concurrent-gpu") ||
        !strcmp(p,"posh-abandon-check"));
}
bool physicalDevice(uint64_t registryID) {
    io_registry_entry_t entry=IOServiceGetMatchingService(kIOMainPortDefault,IORegistryEntryIDMatching(registryID));
    if (!entry) return false;
    CFTypeRef value=IORegistryEntrySearchCFProperty(entry,kIOServicePlane,CFSTR("WhateverXeDeviceSelection"),
        kCFAllocatorDefault,kIORegistryIterateParents|kIORegistryIterateRecursively);
    IOObjectRelease(entry);
    uint32_t s[8]{};
    const bool valid=value && CFGetTypeID(value)==CFDataGetTypeID() &&
        CFDataGetLength(static_cast<CFDataRef>(value))==sizeof(s);
    if (valid) memcpy(s,CFDataGetBytePtr(static_cast<CFDataRef>(value)),sizeof(s));
    if (value) CFRelease(value);
    return valid && s[0]==1 && s[1]==0x9a498086 && s[2]==1 && s[3]==s[1] && s[7]==1;
}
bool field(Class cls,const char *name,const char *type,size_t width,int32_t &offset) {
    Ivar ivar=class_getInstanceVariable(cls,name);
    if (!ivar || strcmp(ivar_getTypeEncoding(ivar),type)) return false;
    const ptrdiff_t o=ivar_getOffset(ivar);
    if (o<0 || o>INT32_MAX || static_cast<size_t>(o)+width>class_getInstanceSize(cls)) return false;
    offset=static_cast<int32_t>(o); return true;
}
bool methodBytes(Class cls,const char *selector,NSData *file,size_t start,size_t end,unsigned args,void *&base) {
    Method method=class_getInstanceMethod(cls,sel_registerName(selector));
    if (!method || method_getNumberOfArguments(method)!=args || end>file.length) return false;
    IMP imp=method_getImplementation(method); Dl_info info{};
    if (!dladdr((void *)imp,&info) || !info.dli_fname || strcmp(info.dli_fname,path) ||
        (const char *)(void *)imp!=(const char *)info.dli_fbase+start ||
        (base && base!=info.dli_fbase) ||
        memcmp((void *)imp,(const char *)file.bytes+start,end-start)) return false;
    base=info.dli_fbase; return true;
}
}

bool installPOSHAtomicClaim(uint64_t metalRegistryID,bool reclaimAbandoned) {
    @autoreleasepool {
        if (installed) return diagnosticProcess() && physicalDevice(metalRegistryID) &&
            (!reclaimAbandoned || reclaimInstalled);
        if (!diagnosticProcess() || !physicalDevice(metalRegistryID) || vm_page_size!=4096) return false;
        Class cb=objc_getClass("MTLIGAccelCommandBuffer"),queue=objc_getClass("MTLIGAccelCommandQueue");
        NSData *file=[NSData dataWithContentsOfFile:@(path)];
        if (!cb || !queue || !file || file.length>UINT32_MAX || file.length<0x7f000) return false;
        unsigned char digest[CC_SHA256_DIGEST_LENGTH];
        CC_SHA256(file.bytes,static_cast<CC_LONG>(file.length),digest);
        char hex[65]; for (unsigned i=0;i<sizeof(digest);++i) snprintf(hex+2*i,3,"%02x",digest[i]);
        void *base=nullptr; int32_t active=0,only=0;
        if (strcmp(hex,expected) || !field(queue,"m_numActiveCommandBuffers","I",4,active) ||
            (active&3) || !field(cb,"m_isOnlyActiveCommandBuffer","B",1,only) ||
            !methodBytes(cb,"initWithQueue:retainedReferences:synchronousDebugMode:",file,0x7e862,0x7eb66,5,base) ||
            !methodBytes(cb,"commit",file,0x7eb67,0x7ec50,2,base) ||
            !methodBytes(cb,"dealloc",file,0x7ecba,0x7ee0a,2,base)) return false;
        Method ctor=class_getInstanceMethod(cb,sel_registerName("initWithQueue:retainedReferences:synchronousDebugMode:"));
        Method submit=class_getInstanceMethod(cb,sel_registerName("commit"));
        Method dtor=class_getInstanceMethod(cb,sel_registerName("dealloc"));
        if (reclaimAbandoned) {
            Ivar q=class_getInstanceVariable(cb,"_pIGCommandQueue");
            if (!q || ivar_getTypeEncoding(q)[0]!='@') {
                fprintf(stderr,"Lifecycle guard refused queue ivar type: %s\n",q?ivar_getTypeEncoding(q):"missing");
                return false;
            }
            queueOffset=ivar_getOffset(q);
            if (queueOffset<0 || (queueOffset&7) ||
                static_cast<size_t>(queueOffset)+sizeof(id)>class_getInstanceSize(cb)) return false;
            activeOffset=active;
            Ivar posh=class_getInstanceVariable(cb,"_pPoshUsedAsTarget");
            Ivar shared=class_getInstanceVariable(queue,"m_pPoshUsedAsTarget");
            if (!posh || !shared || ivar_getTypeEncoding(posh)[0]!='^' || ivar_getTypeEncoding(shared)[0]!='^' ||
                !field(cb,"_pPoshUsedAsTarget",ivar_getTypeEncoding(posh),8,poshOffset) || (poshOffset&7) ||
                !field(queue,"m_pPoshUsedAsTarget",ivar_getTypeEncoding(shared),8,sharedOffset) || (sharedOffset&7)) return false;
            onlyOffset=only;
            char retained[16]{},debug[16]{};
            method_getArgumentType(ctor,3,retained,sizeof(retained));
            method_getArgumentType(ctor,4,debug,sizeof(debug));
            if ((strcmp(retained,"B") && strcmp(retained,"c")) ||
                (strcmp(debug,"B") && strcmp(debug,"c"))) {
                fprintf(stderr,"Lifecycle guard refused init scalar types: %s / %s\n",retained,debug);
                return false;
            }
        }
        auto *page=static_cast<uint8_t *>(base)+0x7e000;
        if (memcmp(page,static_cast<const uint8_t *>(file.bytes)+0x7e000,4096)) return false;
        std::array<uint8_t,4096> updated;
        memcpy(updated.data(),page,updated.size());
        if (!xe::makePOSHAtomicPage(updated.data(),updated.size(),active,only)) return false;
        // COPY obtains a private COW code page. No disk/bundle/signature edits.
        // All validation precedes mutation; setup must be single-threaded.
        const auto task=mach_task_self();
        if (mach_vm_protect(task,(mach_vm_address_t)page,4096,false,
                VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY)!=KERN_SUCCESS) return false;
        memcpy(page,updated.data(),updated.size());
        if (mach_vm_protect(task,(mach_vm_address_t)page,4096,false,
                VM_PROT_READ|VM_PROT_EXECUTE)!=KERN_SUCCESS) {
            // Never return to a caller with a partial executable-code update.
            fprintf(stderr,"Atomic-claim page could not restore execute protection\n");
            std::abort();
        }
        if (reclaimAbandoned) {
            // Setup is single-threaded and no queues exist yet. The exact
            // native IMPs were validated before the COW code-page update.
            originalInit=method_setImplementation(ctor,(IMP)init);
            originalCommit=method_setImplementation(submit,(IMP)commit);
            originalDealloc=method_setImplementation(dtor,(IMP)dealloc);
            reclaimInstalled=true;
        }
        installed=true; return true;
    }
}
POSHActiveStats poshActiveStats() {
    return {tracked.load(),committedCount.load(),abandoned.load(),destroyed.load(),
        privateTracked.load(),privateAbandonedFreed.load()};
}
