// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
// Process-local candidate. Install before creating any command queue.
// No driver file edits, destructor replacement, or WindowServer injection.
// Unmodified native CB constructors can borrow this cache simultaneously.
// AtomicClaim has passed bounded process-local comparisons, but cache lifetime
// across commit/cleanup/error still prevents deployment to general clients.
bool installPOSHQueueCache(uint64_t metalRegistryID);
struct POSHQueueCacheStats { uint64_t allocated, destroyed, refused; };
POSHQueueCacheStats poshQueueCacheStats();
