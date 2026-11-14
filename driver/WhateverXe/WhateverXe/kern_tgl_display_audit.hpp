// SPDX-License-Identifier: BSD-3-Clause
// Opt-in, read-only display-engine audit for Tiger Lake.
//
// The donor does not program the i915 TGL underrun workarounds (BW_BUDDY,
// DCPR_2, DBUF tracker). Before deciding whether to program them we must read
// the live values and the sticky PIPESTATUS underrun bits. A standalone kext
// has no display power-domain lease; this sampler instead runs only from a
// native display-register write, where the caller already holds that MMIO.
// It never writes hardware and refuses to publish a partial sample.
#ifndef kern_tgl_display_audit_hpp
#define kern_tgl_display_audit_hpp
#include <stdint.h>

namespace TGLDisplayAudit {
constexpr uint32_t Version = 1;
constexpr uint32_t UnderrunBit = 1U << 31;

// Linux v6.12 i915: ICL_PIPESTATUS, GEN8_DE_PIPE_ISR/IMR at 0x44400/0x44404
// plus 0x10 per pipe; TGL abox_mask GENMASK(2,1) selects BW_BUDDY index 1/2
// (_PICK_EVEN is linear in the index). DBUF_CTL_S1/S2 are 0x45008/0x44FE8.
struct Registers {
    static constexpr uint32_t PipeStatus[3] {0x70058, 0x71058, 0x72058};
    static constexpr uint32_t PipeISR[3] {0x44400, 0x44410, 0x44420};
    static constexpr uint32_t PipeIMR[3] {0x44404, 0x44414, 0x44424};
    static constexpr uint32_t BuddyCtl[2] {0x45140, 0x45150};
    static constexpr uint32_t BuddyMask[2] {0x45144, 0x45154};
    static constexpr uint32_t Dcpr2 = 0x46434;
    static constexpr uint32_t Dbuf[2] {0x45008, 0x44FE8};
};

// `status`, `isr` and `imr` are written only for pipes present in the sample's
// pipeMask; the other entries retain their last observed value. `invalid`
// counts refused samples and never authorizes interpreting missing data as 0.
// Bit31 in PIPESTATUS is sticky until W1C; without writing we can only report
// the first observation, how many samples still saw it, and observed 0->1 arms.
struct State {
    uint64_t lastSampleNs {};
    uint64_t firstSampleNs {};
    uint64_t underrunFirstNs[3] {};
    uint32_t samples {};
    uint32_t invalid {};
    uint32_t pipeMask {};
    uint32_t status[3] {};
    uint32_t isr[3] {};
    uint32_t imr[3] {};
    uint32_t buddyCtl[2] {};
    uint32_t buddyMask[2] {};
    uint32_t dcpr2 {};
    uint32_t dbuf[2] {};
    uint32_t underrunSamples[3] {};
    uint32_t underrunArms[3] {};
    uint32_t underrunEver {};   // bit mask of pipes with a first observation
    uint32_t previouslySet {};  // last observed bit31 mask, sampled pipes only
    uint32_t schema {Version};
};

// Derive the pipe whose registers are safe to read for this native write.
// PIPESTATUS/ISR/IMR live in the per-pipe display bank (0x7xxxx per pipe);
// IRQ acknowledgement is GEN8_DE_PIPE_IIR at 0x44408 + 0x10 per pipe. Other
// display registers imply ABox power but not a specific pipe, so mask is 0.
inline uint32_t pipeOf(unsigned long reg, uint32_t &mask) {
    mask = 0;
    if (reg >= 0x70000 && reg < 0x73000) {
        mask = 1U << ((reg - 0x70000) >> 12);
        return mask;
    }
    if (reg >= 0x44400 && reg < 0x44440 && (reg & 0xF) == 8) {
        mask = 1U << ((reg - 0x44400) >> 4);
        return mask;
    }
    return 0;
}

// IO contract: bool readable(uint32_t) const; uint32_t read(uint32_t) const.
// Returns true only when the whole requested sample was read and committed.
template<class IO>
bool capture(IO &io, State &s, uint64_t nowNs, uint64_t minIntervalNs, uint32_t pipeMask) {
    if (pipeMask & ~7U) return false;
    if (minIntervalNs && s.lastSampleNs && nowNs - s.lastSampleNs < minIntervalNs) return false;

    // Validate every requested register before mutating any state.
    const uint32_t fixed[] {
        Registers::BuddyCtl[0], Registers::BuddyCtl[1],
        Registers::BuddyMask[0], Registers::BuddyMask[1],
        Registers::Dcpr2, Registers::Dbuf[0], Registers::Dbuf[1]};
    for (auto reg : fixed) if (!io.readable(reg)) { ++s.invalid; return false; }
    for (unsigned pipe=0; pipe<3; ++pipe) {
        if (!(pipeMask & (1U << pipe))) continue;
        if (!io.readable(Registers::PipeStatus[pipe]) ||
            !io.readable(Registers::PipeISR[pipe]) ||
            !io.readable(Registers::PipeIMR[pipe])) { ++s.invalid; return false; }
    }

    uint32_t buddyCtl[2] {}, buddyMask[2] {}, dcpr2 = UINT32_MAX, dbuf[2] {};
    for (unsigned i=0; i<2; ++i) {
        buddyCtl[i] = io.read(Registers::BuddyCtl[i]);
        buddyMask[i] = io.read(Registers::BuddyMask[i]);
    }
    dcpr2 = io.read(Registers::Dcpr2);
    for (unsigned i=0; i<2; ++i) dbuf[i] = io.read(Registers::Dbuf[i]);
    if (buddyCtl[0]==UINT32_MAX || buddyCtl[1]==UINT32_MAX ||
        buddyMask[0]==UINT32_MAX || buddyMask[1]==UINT32_MAX ||
        dcpr2==UINT32_MAX || dbuf[0]==UINT32_MAX || dbuf[1]==UINT32_MAX) {
        ++s.invalid; return false;
    }

    uint32_t status[3] {}, isr[3] {}, imr[3] {};
    for (unsigned pipe=0; pipe<3; ++pipe) {
        if (!(pipeMask & (1U << pipe))) continue;
        status[pipe] = io.read(Registers::PipeStatus[pipe]);
        isr[pipe] = io.read(Registers::PipeISR[pipe]);
        imr[pipe] = io.read(Registers::PipeIMR[pipe]);
        // All-ones IMR is a legitimate "every pipe interrupt masked" value and
        // is the state this audit needs to expose, so it is not a read-failure
        // sentinel here. A whole-sample failed read still fails on status/ISR.
        if (status[pipe]==UINT32_MAX || isr[pipe]==UINT32_MAX) {
            ++s.invalid; return false;
        }
    }

    // Commit the complete sample.
    if (!s.samples) s.firstSampleNs = nowNs;
    s.lastSampleNs = nowNs;
    ++s.samples;
    s.pipeMask = pipeMask;
    for (unsigned i=0; i<2; ++i) { s.buddyCtl[i] = buddyCtl[i]; s.buddyMask[i] = buddyMask[i]; }
    s.dcpr2 = dcpr2;
    for (unsigned i=0; i<2; ++i) s.dbuf[i] = dbuf[i];

    uint32_t set = 0;
    for (unsigned pipe=0; pipe<3; ++pipe) {
        if (!(pipeMask & (1U << pipe))) continue;
        s.status[pipe] = status[pipe];
        s.isr[pipe] = isr[pipe];
        s.imr[pipe] = imr[pipe];
        if (status[pipe] & UnderrunBit) set |= 1U << pipe;
    }
    for (unsigned pipe=0; pipe<3; ++pipe) {
        if (!(set & (1U << pipe))) continue;
        ++s.underrunSamples[pipe];
        if (!(s.underrunEver & (1U << pipe))) {
            s.underrunEver |= 1U << pipe;
            s.underrunFirstNs[pipe] = nowNs;
        }
        if (!(s.previouslySet & (1U << pipe))) ++s.underrunArms[pipe];
    }
    // Only sampled pipes can change the sticky observation. An unsampled pipe
    // keeps its bit: absence of a read is not evidence that it cleared.
    s.previouslySet = (s.previouslySet & ~pipeMask) | set;
    return true;
}

// Suggested sampling period at the native write boundary. This is a bound on
// observation cost, not a claim that every underrun is caught between samples.
constexpr uint64_t SampleIntervalNs = 100000000ULL; // 100 ms
}
#endif
