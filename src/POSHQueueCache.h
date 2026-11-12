// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
// Process-local candidate. Install before creating any command queue.
// No driver file edits, destructor replacement, or WindowServer injection.
// Known concurrency failure: native CB constructors can borrow this cache
// simultaneously. Diagnostic experiment only; never deploy to general clients.
bool installPOSHQueueCache(uint64_t metalRegistryID);
struct POSHQueueCacheStats { uint64_t allocated, destroyed, refused; };
POSHQueueCacheStats poshQueueCacheStats();
