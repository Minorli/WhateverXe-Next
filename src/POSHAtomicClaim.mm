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
#include "POSHAtomicClaim.h"
#include "POSHAtomicCode.hpp"

namespace {
constexpr char path[]="/Library/GPUBundles/AppleIntelTGLGraphicsMTLDriver.bundle/Contents/MacOS/AppleIntelTGLGraphicsMTLDriver";
constexpr char expected[]="1e5e4f5bd76ff567a102125dfa3c74a255934d45c753a88aedcbfac7dab53c6c";
bool installed;
bool diagnosticProcess() {
    const char *p=getprogname();
    return p && (!strcmp(p,"posh-queue-check") || !strcmp(p,"metal-surface-check") ||
        !strcmp(p,"metal-async-check") || !strcmp(p,"metal-memory-check") || !strcmp(p,"posh-concurrent-gpu"));
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

bool installPOSHAtomicClaim(uint64_t metalRegistryID) {
    @autoreleasepool {
        if (installed) return true;
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
        installed=true; return true;
    }
}
