// SPDX-License-Identifier: MIT
// Diagnostic preflight only. Never opens an accelerator user client or asks
// the GPU to do work. A recovered boot is not a clean comparison baseline.
#pragma once
#include <IOKit/IOKitLib.h>
#include <cstdio>
#include <cstring>
#include "RecoveryState.hpp"

namespace xe {
inline bool noGPURecovery() {
    io_registry_entry_t root=IORegistryGetRootEntry(kIOMainPortDefault);
    CFTypeRef value=root?IORegistryEntryCreateCFProperty(root,CFSTR("TGLRecoveryLive"),kCFAllocatorDefault,0):nullptr;
    CFTypeRef readiness=root?IORegistryEntryCreateCFProperty(root,CFSTR("TGLRecoverySinglePassReady"),kCFAllocatorDefault,0):nullptr;
    if (root) IOObjectRelease(root);
    RecoverySnapshot snapshot;
    if(value && CFGetTypeID(value)==CFDataGetTypeID())
        snapshot=recoverySnapshot(CFDataGetBytePtr(static_cast<CFDataRef>(value)),
            CFDataGetLength(static_cast<CFDataRef>(value)),readiness==kCFBooleanTrue);
    if (readiness) CFRelease(readiness);
    if (value) CFRelease(value);
    if (snapshot.state!=RecoveryState::Clean) {
        fprintf(stderr,"GPU probe refused before new work: recovery monitor=%s calls=%llu returns=%llu; verified clean boot required\n",
            recoveryStateName(snapshot.state),snapshot.calls,snapshot.returns);
        return false;
    }
    return true;
}
}
