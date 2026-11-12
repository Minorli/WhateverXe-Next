// ARCHIVED / DO NOT INSTALL: unconditional-free experiment, not a production fix.
// Lifetime and texture-reset risks are unresolved. See STATUS.md and docs/POSH.md.
// Local experimental mitigation for WhateverXe issue 47.
// Do not distribute the generated driver page; it is local verification data.
#include <Headers/kern_api.hpp>
#include <Headers/kern_devinfo.hpp>
#include <Headers/plugin_start.hpp>
#include <IOKit/pci/IOPCIDevice.h>
#include <libkern/OSAtomic.h>
#include <sys/vnode.h>
#include "ExpectedPage.hpp"

// Match the kernel variable linkage used by Lilu's kern_util.hpp.
extern const int version_revision;

namespace {
constexpr memory_object_offset_t TargetPage = 0x7e000;
constexpr size_t BranchOffset = 0xd87;
constexpr size_t PageBytes = 4096;
constexpr const char TargetPath[] = "/Library/GPUBundles/AppleIntelTGLGraphicsMTLDriver.bundle/Contents/MacOS/AppleIntelTGLGraphicsMTLDriver";
mach_vm_address_t originalValidatePage = 0;
volatile SInt32 patchedPages = 0;
volatile SInt32 rejectedPages = 0;

void validatePage(vnode_t vnode, memory_object_t pager, memory_object_offset_t offset,
                  const void *data, int *validated, int *tainted, int *nx) {
    // Always preserve the original validation result and existing hook chain.
    FunctionCast(validatePage, originalValidatePage)(vnode, pager, offset, data, validated, tainted, nx);
    if (offset != TargetPage || !data || !validated || !tainted || !nx ||
        !*validated || *tainted || *nx || PAGE_SIZE != PageBytes) return;

    char path[sizeof(TargetPath)];
    int length = sizeof(path);
    if (vn_getpath(vnode, path, &length) != 0 || strcmp(path, TargetPath) != 0) return;

    // Full-page equality excludes different driver builds and other code changes.
    if (memcmp(data, ExpectedPage, PageBytes) != 0) {
        if (OSIncrementAtomic(&rejectedPages) == 0)
            SYSLOG("posh", "target page mismatch; skipped without changing validation");
        return;
    }
    if (MachInfo::setKernelWriting(true, KernelPatcher::kernelWriteLock) != KERN_SUCCESS) {
        SYSLOG("posh", "could not acquire write access; skipped");
        return;
    }
    auto *bytes = const_cast<uint8_t *>(static_cast<const uint8_t *>(data));
    // Do not alter the validation flags or the original on-disk signature.
    // Remove only the conditional branch that leaks the privately owned POSH allocation.
    bytes[BranchOffset] = 0x90;
    bytes[BranchOffset + 1] = 0x90;
    const auto restore = MachInfo::setKernelWriting(false, KernelPatcher::kernelWriteLock);
    if (OSIncrementAtomic(&patchedPages) == 0)
        SYSLOG("posh", "applied exact-page POSH branch fix; restore=%d", restore);
}

void onPatcher(void *, KernelPatcher &patcher) {
    if (!checkKernelArgument("-xeposhfix") || getKernelVersion() != KernelVersion::Tahoe || getKernelMinorVersion() != 6 || version_revision != 0) {
        SYSLOG("posh", "inactive: requires explicit -xeposhfix and Darwin 25.6.0");
        return;
    }
    auto *info = DeviceInfo::create();
    auto *gpu = info ? OSDynamicCast(IOPCIDevice, info->videoBuiltin) : nullptr;
    const bool supported = gpu && gpu->configRead16(kIOPCIConfigVendorID) == 0x8086 &&
        gpu->configRead16(kIOPCIConfigDeviceID) == 0x9a49 &&
        gpu->configRead8(kIOPCIConfigRevisionID) == 0x01;
    if (info) DeviceInfo::deleter(info);
    if (!supported) {
        SYSLOG("posh", "inactive: requires physical Intel 9A49 revision 01");
        return;
    }
    KernelPatcher::RouteRequest request {"_cs_validate_page", validatePage, originalValidatePage};
    if (!patcher.routeMultipleLong(KernelPatcher::KernelID, &request, 1)) {
        SYSLOG("posh", "could not route page validation; no mitigation installed");
        patcher.clearError();
        return;
    }
    SYSLOG("posh", "armed for original 16.0.32 page only; disk bundle/signature unchanged");
}

const char *disableArguments[] {"-xeposhfixoff"};
void startPlugin() {
    const auto error = lilu.onPatcherLoad(onPatcher);
    if (error != LiluAPI::Error::NoError)
        SYSLOG("posh", "could not register patcher callback: %d", error);
}
}

PluginConfiguration ADDPR(config) {
    xStringify(PRODUCT_NAME), parseModuleVersion(xStringify(MODULE_VERSION)), LiluAPI::AllowNormal,
    disableArguments, arrsize(disableArguments), nullptr, 0, nullptr, 0,
    KernelVersion::Tahoe, KernelVersion::Tahoe, startPlugin
};

// Standard libkmod lifecycle; the kernel invokes C++ initializers separately.
extern "C" {
extern kern_return_t _start(kmod_info_t *, void *);
extern kern_return_t _stop(kmod_info_t *, void *);
extern kern_return_t XePOSHFix_kern_start(kmod_info_t *, void *);
extern kern_return_t XePOSHFix_kern_stop(kmod_info_t *, void *);
__attribute__((visibility("default"))) KMOD_EXPLICIT_DECL(local.Minorli.XePOSHFix, "0.1.0", _start, _stop)
__attribute__((visibility("hidden"))) kmod_start_func_t *_realmain = XePOSHFix_kern_start;
__attribute__((visibility("hidden"))) kmod_stop_func_t *_antimain = XePOSHFix_kern_stop;
__attribute__((visibility("hidden"))) int _kext_apple_cc = __APPLE_CC__;
}
