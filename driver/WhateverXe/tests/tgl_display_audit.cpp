// CPU-only contract for the opt-in TGL display audit.
// Uses a synthetic register map; never touches MMIO or a GPU.
#include <cassert>
#include <cstdio>
#include <map>
#include "kern_tgl_display_audit.hpp"

namespace {
struct FakeIO {
    std::map<uint32_t, uint32_t> regs;
    bool readable(uint32_t reg) const { return regs.find(reg) != regs.end(); }
    uint32_t read(uint32_t reg) const {
        auto it = regs.find(reg);
        return it == regs.end() ? UINT32_MAX : it->second;
    }
};
void fill(FakeIO &io) {
    using namespace TGLDisplayAudit;
    for (auto r : Registers::PipeStatus) io.regs[r] = 0;
    for (auto r : Registers::PipeISR) io.regs[r] = 0;
    for (auto r : Registers::PipeIMR) io.regs[r] = 0xFFFFFFFF;
    io.regs[Registers::BuddyCtl[0]] = 0x00080000;   // timer 8
    io.regs[Registers::BuddyCtl[1]] = 0x00080000;
    io.regs[Registers::BuddyMask[0]] = 0x1F;        // DDR4, two channels
    io.regs[Registers::BuddyMask[1]] = 0x1F;
    io.regs[Registers::Dcpr2] = 0x0F000000;
    io.regs[Registers::Dbuf[0]] = 0x80400000;       // request + tracker 8
    io.regs[Registers::Dbuf[1]] = 0x80400000;
}
}

int main() {
    using namespace TGLDisplayAudit;
    FakeIO io;
    fill(io);
    State s;

    // First sample reads every ABox register plus the requested pipe.
    assert(capture(io, s, 1000, SampleIntervalNs, 1U));
    assert(s.samples == 1 && s.invalid == 0 && s.pipeMask == 1);
    assert(s.buddyMask[0] == 0x1F && s.buddyMask[1] == 0x1F);
    assert(s.buddyCtl[0] == 0x00080000 && s.dcpr2 == 0x0F000000);
    assert(s.dbuf[0] == 0x80400000 && s.dbuf[1] == 0x80400000);
    assert(s.status[0] == 0 && s.imr[0] == 0xFFFFFFFF);
    assert(s.underrunEver == 0 && s.underrunArms[0] == 0);

    // A sample inside the interval is refused without changing counters.
    const State before = s;
    assert(!capture(io, s, 2000, SampleIntervalNs, 1U));
    assert(s.samples == before.samples && s.invalid == before.invalid);
    assert(s.lastSampleNs == before.lastSampleNs);

    // Sticky bit31: first observation records the time and one arm.
    io.regs[Registers::PipeStatus[0]] = UnderrunBit | 0x3;
    assert(capture(io, s, SampleIntervalNs + 1000, SampleIntervalNs, 1U));
    assert(s.samples == 2);
    assert(s.underrunEver == 1U && s.underrunArms[0] == 1);
    assert(s.underrunSamples[0] == 1);
    assert(s.underrunFirstNs[0] == SampleIntervalNs + 1000);

    // Still set: counted per sample, but not as another arm.
    assert(capture(io, s, 2 * SampleIntervalNs + 2000, SampleIntervalNs, 1U));
    assert(s.underrunArms[0] == 1 && s.underrunSamples[0] == 2);
    assert(s.underrunFirstNs[0] == SampleIntervalNs + 1000);

    // If the bit is observed cleared and later set, that is a new arm.
    io.regs[Registers::PipeStatus[0]] = 0;
    assert(capture(io, s, 3 * SampleIntervalNs + 3000, SampleIntervalNs, 1U));
    assert(s.underrunArms[0] == 1 && s.previouslySet == 0);
    io.regs[Registers::PipeStatus[0]] = UnderrunBit;
    assert(capture(io, s, 4 * SampleIntervalNs + 4000, SampleIntervalNs, 1U));
    assert(s.underrunArms[0] == 2 && s.underrunSamples[0] == 3);

    // Sampling only pipe B must not clear pipe A's remembered sticky bit.
    io.regs[Registers::PipeStatus[1]] = UnderrunBit;
    assert(capture(io, s, 5 * SampleIntervalNs + 5000, SampleIntervalNs, 2U));
    assert(s.pipeMask == 2 && s.underrunArms[1] == 1);
    assert(s.previouslySet == 3U); // A preserved (not read), B added
    assert(s.underrunArms[0] == 2);

    // Missing register: refused, no partial commit, invalid counter grows.
    const State partial = s;
    io.regs.erase(Registers::Dcpr2);
    assert(!capture(io, s, 6 * SampleIntervalNs + 6000, SampleIntervalNs, 3U));
    assert(s.samples == partial.samples && s.lastSampleNs == partial.lastSampleNs);
    assert(s.invalid == partial.invalid + 1);
    assert(s.status[2] == partial.status[2]);
    fill(io);

    // All-ones is not a value; reject it and keep prior state.
    io.regs[Registers::PipeStatus[2]] = UINT32_MAX;
    const State allOnes = s;
    assert(!capture(io, s, 7 * SampleIntervalNs + 7000, SampleIntervalNs, 4U));
    assert(s.samples == allOnes.samples && s.status[2] == allOnes.status[2]);
    assert(s.invalid == allOnes.invalid + 1);
    fill(io);

    // A write with no specific pipe still samples ABox registers.
    assert(capture(io, s, 8 * SampleIntervalNs + 8000, SampleIntervalNs, 0U));
    assert(s.pipeMask == 0 && s.samples == allOnes.samples + 1);
    assert(s.buddyMask[0] == 0x1F);

    // Pipe derivation accepts the native plane and IRQ-ack registers only.
    uint32_t mask = 0;
    pipeOf(0x70180, mask); assert(mask == 1U);
    pipeOf(0x71058, mask); assert(mask == 2U);
    pipeOf(0x72058, mask); assert(mask == 4U);
    pipeOf(0x44428, mask); assert(mask == 4U);
    pipeOf(0x44420, mask); assert(mask == 0U); // ISR, not an ack write
    pipeOf(0x45140, mask); assert(mask == 0U);
    pipeOf(0x163890, mask); assert(mask == 0U);

    puts("PASS: TGL display audit captures a complete read-only sample, throttles, "
         "tracks sticky underrun first/arm per pipe, preserves unsampled pipes, "
         "and refuses missing/all-ones reads without partial publish");
    return 0;
}
