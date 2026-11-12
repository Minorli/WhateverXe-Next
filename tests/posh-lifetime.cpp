// SPDX-License-Identifier: MIT
#include "../src/POSHLifetime.hpp"
#include <atomic>
#include <cassert>
#include <thread>
#include <cstdio>
static std::atomic<unsigned> frees {0};
static void count(void *p) { assert(p == &frees); ++frees; }
int main() {
    { xe::POSHLifetime t(&frees, count); t.cpuFinished(); assert(frees == 0);
      t.gpuFinished(); assert(frees == 1); t.cpuFinished(); t.gpuFinished(); assert(frees == 1); }
    { xe::POSHLifetime t(&frees, count); t.gpuFinished(); assert(frees == 1);
      t.cpuFinished(); assert(frees == 2); }
    { xe::POSHLifetime t(&frees, count); t.cpuFinished(); }
    assert(frees == 2); // No fabricated GPU completion during destruction.
    for (unsigned n = 0; n < 1000; ++n) {
        xe::POSHLifetime t(&frees, count);
        std::thread a([&] { t.cpuFinished(); });
        std::thread b([&] { t.gpuFinished(); });
        a.join(); b.join(); assert(frees == 3+n);
    }
    puts("POSH lifetime: both orderings, duplicate events, missing completion, 1000 races passed");
}
