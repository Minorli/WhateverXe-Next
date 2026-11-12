// SPDX-License-Identifier: MIT
// Two independent owners: the native destructor and GPU completion.
#pragma once
#include <atomic>
#include <cstdint>

namespace xe {
class POSHLifetime {
    enum : uint32_t { CpuDone = 1, GpuDone = 2, Freed = 4 };
    std::atomic<uint32_t> state {0};
    void *pointer;
    void (*deleter)(void *);
    void finish(uint32_t flag) noexcept {
        auto next = state.fetch_or(flag, std::memory_order_acq_rel) | flag;
        if ((next & (CpuDone | GpuDone)) != (CpuDone | GpuDone)) return;
        // Only one racing caller wins disposal. Neither caller reads the pointer
        // again, and the control object's lifetime is retained by each caller.
        next = state.fetch_or(Freed, std::memory_order_acq_rel);
        if (!(next & Freed)) deleter(pointer);
    }
public:
    POSHLifetime(void *p, void (*d)(void *)) : pointer(p), deleter(d) {}
    POSHLifetime(const POSHLifetime &) = delete;
    POSHLifetime &operator=(const POSHLifetime &) = delete;
    void cpuFinished() noexcept { finish(CpuDone); }
    void gpuFinished() noexcept { finish(GpuDone); }
    // Destruction deliberately does not dispose unresolved GPU-owned storage.
    // A missed callback must leak conservatively instead of freeing live data.
};
}
