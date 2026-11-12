// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
// Diagnostic process only; never inject into WindowServer or change a bundle.
bool installPOSHDeferredFix();
struct POSHStats { uint64_t tracked, detached, disposed, rejected, completions, deallocInFlight, uncommitted; };
POSHStats poshDeferredStats();
