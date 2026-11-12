// SPDX-License-Identifier: MIT
// Build with -fno-objc-arc: hooked init/dealloc must preserve native ownership ABI.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <objc/runtime.h>
#include <CommonCrypto/CommonDigest.h>
#include <dlfcn.h>
#include <malloc/malloc.h>
#include <atomic>
#include <cstring>
#include <new>
#include "POSHLifetime.hpp"
#include "POSHDeferred.h"

static std::atomic<uint64_t> tracked{0}, detached{0}, disposed{0}, rejected{0}, completions{0};
static std::atomic<uint64_t> deallocInFlight{0}, uncommitted{0};
static void dispose(void *p) { ::operator delete(p); ++disposed; }
@interface XEPOSHToken : NSObject {
@public
    xe::POSHLifetime *lifetime;
    void *captured;
}
@end
@implementation XEPOSHToken
- (void)dealloc { delete lifetime; [super dealloc]; }
@end

namespace {
char associationKey;
IMP originalInit, originalDealloc;
ptrdiff_t poshOffset, onlyOffset, queueOffset, sharedOffset;
bool installed;
constexpr char path[] = "/Library/GPUBundles/AppleIntelTGLGraphicsMTLDriver.bundle/Contents/MacOS/AppleIntelTGLGraphicsMTLDriver";
constexpr char expected[] = "1e5e4f5bd76ff567a102125dfa3c74a255934d45c753a88aedcbfac7dab53c6c";
void *readPointer(id object, ptrdiff_t offset) {
    void *value; std::memcpy(&value, static_cast<char *>((void *)object)+offset, sizeof(value)); return value;
}
bool originalFile() {
    NSData *data = [NSData dataWithContentsOfFile:@(path)];
    if (!data || data.length > UINT32_MAX) return false;
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(data.bytes, static_cast<CC_LONG>(data.length), digest);
    char hex[65]; for (unsigned i=0;i<sizeof(digest);++i) snprintf(hex+2*i,3,"%02x",digest[i]);
    return strcmp(hex,expected)==0;
}
bool ivar(Class cls, const char *name, size_t bytes, ptrdiff_t &offset, const char *type=nullptr) {
    Ivar field = class_getInstanceVariable(cls,name); if (!field) return false;
    offset = ivar_getOffset(field);
    return offset>=0 && static_cast<size_t>(offset)+bytes<=class_getInstanceSize(cls) &&
        (!type || strcmp(ivar_getTypeEncoding(field),type)==0);
}
bool methodInDriver(Method method) {
    Dl_info info{};
    return method && dladdr((void *)method_getImplementation(method), &info) &&
        info.dli_fname && strcmp(info.dli_fname,path)==0;
}
bool scalarArgument(Method method, unsigned index) {
    char type[16]{}; method_getArgumentType(method,index,type,sizeof(type));
    return strcmp(type,"B")==0 || strcmp(type,"c")==0;
}
id init(id object, SEL selector, id queue, bool retained, bool debug) {
    id result = ((id (*)(id,SEL,id,bool,bool))originalInit)(object,selector,queue,retained,debug);
    if (!result) return result;
    auto *bytes=static_cast<char *>((void *)result);
    void *pointer=readPointer(result,poshOffset);
    // Only intercept the confirmed leak case; overlapping native CBs keep their
    // original deallocation path. Never adopt queue-owned/shared storage.
    if (!pointer || !bytes[onlyOffset]) return result;
    if (readPointer(result,queueOffset)!=(void *)queue || readPointer(queue,sharedOffset)==pointer ||
        malloc_size(pointer)!=77824) { ++rejected; return result; }
    auto *token=[XEPOSHToken new];
    token->captured=pointer;
    token->lifetime=new (std::nothrow) xe::POSHLifetime(pointer,dispose);
    if (!token->lifetime) { [token release]; ++rejected; return result; }
    objc_setAssociatedObject(result,&associationKey,token,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    [(id<MTLCommandBuffer>)result addCompletedHandler:^(id<MTLCommandBuffer> completed) {
        // Capture only token; no retain cycle through the command buffer.
        if (completed.status==MTLCommandBufferStatusCompleted || completed.status==MTLCommandBufferStatusError) {
            ++completions; token->lifetime->gpuFinished();
        }
    }];
    ++tracked; [token release];
    return result;
}
void dealloc(id object, SEL selector) {
    auto *token=(XEPOSHToken *)[objc_getAssociatedObject(object,&associationKey) retain];
    bool adopted=false;
    if (token) {
        auto *bytes=static_cast<char *>((void *)object);
        void *queue=readPointer(object,queueOffset);
        if (bytes[onlyOffset] && readPointer(object,poshOffset)==token->captured && queue &&
            readPointer((id)queue,sharedOffset)!=token->captured) {
            const auto status=[(id<MTLCommandBuffer>)object status];
            // NotEnqueued/Enqueued has never been committed to the GPU.
            if (status<MTLCommandBufferStatusCommitted) { ++uncommitted; token->lifetime->gpuFinished(); }
            else if (status<MTLCommandBufferStatusCompleted) ++deallocInFlight;
            void *empty=nullptr; std::memcpy(bytes+poshOffset,&empty,sizeof(empty));
            adopted=true; ++detached;
        } else ++rejected;
    }
    ((void (*)(id,SEL))originalDealloc)(object,selector);
    // Descriptor/native superclass cleanup has now completed. Do not access
    // object again. This and the completion block independently retain token.
    if (adopted) token->lifetime->cpuFinished();
    [token release];
}
}
bool installPOSHDeferredFix() {
    if (installed) return true;
    Class cb=objc_getClass("MTLIGAccelCommandBuffer"), q=objc_getClass("MTLIGAccelCommandQueue");
    Method ctor=class_getInstanceMethod(cb,sel_registerName("initWithQueue:retainedReferences:synchronousDebugMode:"));
    Method dtor=class_getInstanceMethod(cb,sel_registerName("dealloc"));
    if (!cb || !q || !originalFile() || !methodInDriver(ctor) || !methodInDriver(dtor) ||
        method_getNumberOfArguments(ctor)!=5 || !scalarArgument(ctor,3) || !scalarArgument(ctor,4) ||
        !ivar(cb,"_pPoshUsedAsTarget",8,poshOffset) || !ivar(cb,"m_isOnlyActiveCommandBuffer",1,onlyOffset,"B") ||
        !ivar(cb,"_pIGCommandQueue",8,queueOffset) || !ivar(q,"m_pPoshUsedAsTarget",8,sharedOffset)) return false;
    // Single-threaded setup before this diagnostic creates any command buffer.
    originalDealloc=method_setImplementation(dtor,(IMP)dealloc);
    originalInit=method_setImplementation(ctor,(IMP)init);
    installed=true; return true;
}
POSHStats poshDeferredStats() {
    return {tracked.load(),detached.load(),disposed.load(),rejected.load(),completions.load(),deallocInFlight.load(),uncommitted.load()};
}
