// SPDX-License-Identifier: MIT
// Restore the donor's queue-owned cache path instead of altering CB disposal.
// Diagnostic-process candidate only; not a globally deployable component.
// Concurrent native constructors have reproduced shared-storage aliasing.
// This experiment is retained for diagnosis, not promoted on serial A/B results.
#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#import <IOKit/IOKitLib.h>
#include <CommonCrypto/CommonDigest.h>
#include <dlfcn.h>
#include <atomic>
#include <cstring>
#include <new>
#include <cstdlib>
#include "POSHQueueCache.h"

static std::atomic<uint64_t> allocated{0}, destroyed{0}, refused{0};
@interface XEPOSHQueueMarker : NSObject
@end
@implementation XEPOSHQueueMarker
- (void)dealloc { ++destroyed; [super dealloc]; }
@end

namespace {
constexpr char path[]="/Library/GPUBundles/AppleIntelTGLGraphicsMTLDriver.bundle/Contents/MacOS/AppleIntelTGLGraphicsMTLDriver";
constexpr char expected[]="1e5e4f5bd76ff567a102125dfa3c74a255934d45c753a88aedcbfac7dab53c6c";
constexpr size_t storageBytes=0x12010;
IMP originalInit;
ptrdiff_t sharedOffset, activeOffset;
char markerKey;
bool installed;

bool diagnosticProcess() {
    const char *name=getprogname();
    return name && (!strcmp(name,"metal-memory-check") || !strcmp(name,"metal-async-check") ||
        !strcmp(name,"posh-queue-check") || !strcmp(name,"metal-surface-check"));
}
bool physicalDevice(uint64_t registryID) {
    io_registry_entry_t entry=IOServiceGetMatchingService(kIOMainPortDefault,IORegistryEntryIDMatching(registryID));
    if (!entry) return false;
    CFTypeRef value=IORegistryEntrySearchCFProperty(entry,kIOServicePlane,CFSTR("WhateverXeDeviceSelection"),
        kCFAllocatorDefault,kIORegistryIterateParents|kIORegistryIterateRecursively);
    IOObjectRelease(entry);
    uint32_t selection[8]{};
    const bool valid=value && CFGetTypeID(value)==CFDataGetTypeID() &&
        CFDataGetLength(static_cast<CFDataRef>(value))==sizeof(selection);
    if (valid) memcpy(selection,CFDataGetBytePtr(static_cast<CFDataRef>(value)),sizeof(selection));
    if (value) CFRelease(value);
    // Published from physical PCI config by XeHost before identity selection.
    return valid && selection[0]==1 && selection[1]==0x9a498086 &&
        selection[2]==1 && selection[3]==selection[1] && selection[7]==1;
}

bool field(Class cls,const char *name,const char *type,size_t width,ptrdiff_t &offset) {
    Ivar ivar=class_getInstanceVariable(cls,name);
    if (!ivar || strcmp(ivar_getTypeEncoding(ivar),type)) return false;
    offset=ivar_getOffset(ivar);
    return offset>=0 && static_cast<size_t>(offset)+width<=class_getInstanceSize(cls);
}
bool pointerField(Class cls,const char *name,ptrdiff_t &offset) {
    Ivar ivar=class_getInstanceVariable(cls,name);
    if (!ivar || ivar_getTypeEncoding(ivar)[0]!='^') return false;
    offset=ivar_getOffset(ivar);
    return offset>=0 && static_cast<size_t>(offset)+sizeof(void *)<=class_getInstanceSize(cls);
}
bool methodBytes(Class cls,SEL selector,NSData *file,size_t start,size_t end,unsigned args) {
    Method method=class_getInstanceMethod(cls,selector);
    if (!method || method_getNumberOfArguments(method)!=args || end>file.length) return false;
    Dl_info info{}; IMP imp=method_getImplementation(method);
    if (!dladdr((void *)imp,&info) || !info.dli_fname || strcmp(info.dli_fname,path) ||
        (const char *)(void *)imp!=(const char *)info.dli_fbase+start) return false;
    return memcmp((void *)imp,(const char *)file.bytes+start,end-start)==0;
}
id init(id object,SEL selector,id device,NSUInteger maximum) {
    id result=((id (*)(id,SEL,id,NSUInteger))originalInit)(object,selector,device,maximum);
    if (!result) return result;
    auto *bytes=static_cast<char *>((void *)result);
    void *existing=nullptr; uint32_t active=0;
    memcpy(&existing,bytes+sharedOffset,sizeof(existing));
    memcpy(&active,bytes+activeOffset,sizeof(active));
    // The queue has not been returned to its caller yet. Never replace a cache
    // installed by another owner or publish into a queue already creating CBs.
    if (existing || active) { ++refused; return result; }
    void *storage=::operator new(storageBytes,std::nothrow);
    if (!storage) { ++refused; return result; }
    // Match the native allocation's initialized fields: two 1024-entry count
    // arrays and their overflow flags. The remaining entries are read only
    // after a nonzero count and are written by the unchanged native encoder.
    memset(storage,0,0x1000);
    memset(static_cast<char *>(storage)+0x9008,0,0x1000);
    static_cast<char *>(storage)[0x9000]=0;
    static_cast<char *>(storage)[0x12008]=0;
    auto marker=[XEPOSHQueueMarker new];
    if (!marker) { ::operator delete(storage); ++refused; return result; }
    objc_setAssociatedObject(result,&markerKey,marker,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    [marker release];
    // Native CB init now borrows this allocation only for its only-active
    // path. Overlapping CBs still allocate/delete their own native storage.
    // Native queue dealloc remains the sole cache disposer. No completion is
    // invented, no CB flag is changed, and no early free is added.
    memcpy(bytes+sharedOffset,&storage,sizeof(storage));
    ++allocated;
    return result;
}
}

bool installPOSHQueueCache(uint64_t metalRegistryID) {
    @autoreleasepool {
    if (installed) return true;
    if (!diagnosticProcess() || !physicalDevice(metalRegistryID)) return false;
    Class queue=objc_getClass("MTLIGAccelCommandQueue"), cb=objc_getClass("MTLIGAccelCommandBuffer");
    NSData *file=[NSData dataWithContentsOfFile:@(path)];
    if (!queue || !cb || !file || file.length>UINT32_MAX) return false;
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(file.bytes,static_cast<CC_LONG>(file.length),digest);
    char hex[65]; for (unsigned i=0;i<sizeof(digest);++i) snprintf(hex+2*i,3,"%02x",digest[i]);
    SEL selector=sel_registerName("initWithDevice:maxCommandBufferCount:");
    Method ctor=class_getInstanceMethod(queue,selector);
    char countType[16]{},returnType[16]{},deviceType[16]{};
    if (!ctor) return false;
    method_getArgumentType(ctor,3,countType,sizeof(countType));
    method_getArgumentType(ctor,2,deviceType,sizeof(deviceType));
    method_getReturnType(ctor,returnType,sizeof(returnType));
    if (strcmp(hex,expected) || strcmp(countType,"Q") || strcmp(deviceType,"@") || strcmp(returnType,"@") ||
        !pointerField(queue,"m_pPoshUsedAsTarget",sharedOffset) ||
        !field(queue,"m_numActiveCommandBuffers","I",4,activeOffset) ||
        !methodBytes(queue,selector,file,0x3ca30,0x3cb6c,4) ||
        !methodBytes(queue,sel_registerName("dealloc"),file,0x3b7f6,0x3b906,2) ||
        !methodBytes(cb,sel_registerName("initWithQueue:retainedReferences:synchronousDebugMode:"),file,0x7e862,0x7eb66,5) ||
        !methodBytes(cb,sel_registerName("dealloc"),file,0x7ecba,0x7ee0a,2)) return false;
    // Single-threaded diagnostic setup; do not compose with POSHDeferred.
    originalInit=method_setImplementation(ctor,(IMP)init);
    installed=true;
    return true;
    }
}
POSHQueueCacheStats poshQueueCacheStats() {
    return {allocated.load(),destroyed.load(),refused.load()};
}
