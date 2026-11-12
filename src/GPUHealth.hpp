// SPDX-License-Identifier: MIT
// Diagnostic preflight only. Never opens an accelerator user client or asks
// the GPU to do work. A recovered boot is not a clean comparison baseline.
#pragma once
#include <IOKit/IOKitLib.h>
#include <cstdio>
#include <cstring>

namespace xe {
inline bool noGPURecovery() {
    io_registry_entry_t root=IORegistryGetRootEntry(kIOMainPortDefault);
    CFTypeRef value=root?IORegistryEntryCreateCFProperty(root,CFSTR("TGLRecoveryLive"),kCFAllocatorDefault,0):nullptr;
    if (root) IOObjectRelease(root);
    uint64_t counters[2]{};
    const bool valid=value && CFGetTypeID(value)==CFDataGetTypeID() &&
        CFDataGetLength(static_cast<CFDataRef>(value))>=static_cast<CFIndex>(sizeof(counters));
    if (valid) memcpy(counters,CFDataGetBytePtr(static_cast<CFDataRef>(value)),sizeof(counters));
    if (value) CFRelease(value);
    if (!valid || counters[0] || counters[1]) {
        fprintf(stderr,"GPU probe refused before new work: recovery monitor=%s calls=%llu returns=%llu; clean boot required\n",
            valid?"available":"unavailable",counters[0],counters[1]);
        return false;
    }
    return true;
}
}
