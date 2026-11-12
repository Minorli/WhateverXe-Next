// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
// Diagnostic process only; call during single-threaded setup, before queues.
// Changes native CPU active-count accounting, never GPU wait/completion code.
// Not a system installer or a complete proof of shared-cache lifetime.
bool installPOSHAtomicClaim(uint64_t metalRegistryID);
