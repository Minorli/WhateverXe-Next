// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
// Diagnostic process only; call during single-threaded setup, before queues.
// Changes native CPU active-count accounting, never GPU wait/completion code.
// Not a system installer or a complete proof of shared-cache lifetime.
// reclaimAbandoned adds a separate, opt-in native-lifecycle experiment. It
// returns uncommitted claims only AFTER the complete native dealloc returns.
// Also deletes their native only-active, non-shared allocation after cleanup;
// that final disposal path is not yet native-validated. Submitted POSH stays native.
// Native init exceptions terminate the diagnostic: rollback is not proven.
bool installPOSHAtomicClaim(uint64_t metalRegistryID, bool reclaimAbandoned=false);
struct POSHActiveStats {
    uint64_t tracked, committed, abandoned, destroyed, privateTracked, privateAbandonedFreed;
};
POSHActiveStats poshActiveStats();
