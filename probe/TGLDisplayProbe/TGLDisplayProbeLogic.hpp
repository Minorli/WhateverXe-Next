// SPDX-License-Identifier: BSD-3-Clause
// TGLDisplayProbe: hardware-independent sampling logic.
//
// Purpose: measure Tiger Lake (display version 12) pipe FIFO underruns and
// snapshot the display-init workaround registers that the Apple TGL donor
// framebuffer never programs (see docs/ClaudeReview-20261113-Display.md).
//
// Register references: Linux v6.12 i915
//   ICL_PIPESTATUS        0x70058 (+0x1000/pipe), bit31 sticky, write-1-clear
//                         (intel_fifo_underrun.c writes back the read mask)
//   PIPE_FRMCOUNT_G4X     0x70040, PIPE_FLIPCOUNT_G4X 0x70044 (+0x1000/pipe)
//   DC_STATE_EN           0x45504, bits1:0 DC5/DC6 enable, bit3 DC9
//   HSW_PWR_WELL_CTL2     0x45404, state bit (idx*2): PW_1 idx0 (pipe A),
//                         PW_3 idx2 (pipe B), PW_4 idx3 (pipe C)  (TGL map)
//   GEN8_DE_PIPE_IMR      0x44404 + 0x10*pipe
//   BW_BUDDY_CTL(1/2)     0x45140/0x45150, TLB_REQ_TIMER bits21:16 (Wa_22010178259 = 8)
//   BW_BUDDY_PAGE_MASK    0x45144/0x45154 (value from DRAM type/channels table)
//   GEN11_CHICKEN_DCPR_2  0x46434, bits27:24 (Wa_14011508470)
//   DBUF_CTL_S1/S2        0x45008/0x44FE8, bit30 power state, bits23:19 tracker (=8)
//   SOUTH_DSPCLK_GATE_D   0xC2020, bit15 (Wa_14011294188) -- snapshot only
//   EDP_PSR_CTL(A)        0x60800 bit31, EDP_PSR2_CTL(A) 0x60900 bit31 -- snapshot only
//
// Deployment boundary: this sampler does NOT acquire a display power-domain
// lease. The kernel adapter must not call it until that boundary is qualified.
// Version 1.0.1 ships native RAM counters only; no BAR mapping or MMIO adapter.
// Safety contract for the CPU-tested, currently undeployed MMIO sampler:
//  * Every access is to an offset in kAllowed. No other register is touched.
//  * While DC_STATE_EN allows DC5/DC6/DC9 nothing beyond DC_STATE_EN is read.
//  * A pipe's registers are read only while its power well reports on.
//  * Default: no writes at all. Count mode (opt-in) only writes PIPESTATUS
//    bit31 (W1C, as i915 does) of a pipe that showed it.
//  * An all-ones DC_STATE_EN read (device not responding) is reported as
//    unavailable, never as data.
//  * Workaround writes are opt-in per bit, read-modify-write of their own
//    field only, idempotent, and the page mask is never guessed.
#ifndef TGLDisplayProbeLogic_hpp
#define TGLDisplayProbeLogic_hpp
#include <stdint.h>
#include <stddef.h>

namespace TGLDP {

// Used by the kernel under its work-loop gate. cancelTimeout alone does not
// drain an executing callback: transitions must take that SAME gate first.
struct Lifecycle {
	bool sleeping = false, stopping = false;
	uint64_t resumeAfter = 0;
	void pause() { sleeping = true; resumeAfter = 0; }
	void wake(uint64_t now) { if (!stopping) { sleeping = false; resumeAfter = now + 3000000000ULL; } }
	void stop() { stopping = true; }
	bool canSample(uint64_t now) const { return !sleeping && !stopping && now >= resumeAfter; }
};

// Deliberately no boot-arg bypass. A PCI/power-well snapshot is not a lease.
constexpr bool kMMIOQualified = false;
inline bool acceptsWriteMode(bool count, uint32_t wa) { return !count && !wa; }

constexpr uint32_t kPipes = 3;
constexpr uint32_t PIPESTATUS(uint32_t p) { return 0x70058 + 0x1000 * p; }
constexpr uint32_t FRMCOUNT(uint32_t p) { return 0x70040 + 0x1000 * p; }
constexpr uint32_t FLIPCOUNT(uint32_t p) { return 0x70044 + 0x1000 * p; }
constexpr uint32_t DE_PIPE_IMR(uint32_t p) { return 0x44404 + 0x10 * p; }
constexpr uint32_t DC_STATE_EN = 0x45504, PWR_WELL_CTL2 = 0x45404;
constexpr uint32_t BUDDY_CTL[2] {0x45140, 0x45150}, BUDDY_MASK[2] {0x45144, 0x45154};
constexpr uint32_t DCPR_2 = 0x46434, DBUF_CTL[2] {0x45008, 0x44FE8};
constexpr uint32_t SOUTH_DSPCLK_GATE_D = 0xC2020, PSR_CTL = 0x60800, PSR2_CTL = 0x60900;

constexpr uint32_t kAllowed[] {
	0x70058, 0x71058, 0x72058, 0x70040, 0x71040, 0x72040, 0x70044, 0x71044, 0x72044,
	0x44404, 0x44414, 0x44424, DC_STATE_EN, PWR_WELL_CTL2,
	0x45140, 0x45150, 0x45144, 0x45154, DCPR_2, 0x45008, 0x44FE8,
	SOUTH_DSPCLK_GATE_D, PSR_CTL, PSR2_CTL,
};
inline bool allowed(uint32_t reg) {
	for (auto r : kAllowed) if (r == reg) return true;
	return false;
}

constexpr uint32_t UNDERRUN = 1U << 31;
constexpr uint32_t DC_MASK = 0x3U | 0x8U;               // DC5, DC6, DC9 enables
constexpr uint32_t pipePowerBit(uint32_t p) {            // PW_1, PW_3, PW_4 state
	return p == 0 ? (1U << 0) : p == 1 ? (1U << 4) : (1U << 6);
}
constexpr uint32_t PW1_STATE = 1U << 0;

// Opt-in workaround selectors (boot-arg tgldp-wa=<mask>).
enum WA : uint32_t {
	WABuddyTimer = 1,   // BW_BUDDY_CTL TLB_REQ_TIMER = 8
	WABuddyMask  = 2,   // BW_BUDDY_PAGE_MASK = Config::pageMask (must be non-zero)
	WADCPR2      = 4,   // CHICKEN_DCPR_2 |= bits27:24
	WADBufTrack  = 8,   // DBUF_CTL tracker state service = 8, powered slices only
	WAAll        = 15,
};
constexpr uint32_t BUDDY_TIMER_MASK = 0x3FU << 16, BUDDY_TIMER = 8U << 16;
constexpr uint32_t DCPR2_BITS = 0xFU << 24;
constexpr uint32_t DBUF_TRACK_MASK = 0x1FU << 19, DBUF_TRACK = 8U << 19, DBUF_POWERED = 1U << 30;

inline uint32_t wantBuddyCtl(uint32_t v) { return (v & ~BUDDY_TIMER_MASK) | BUDDY_TIMER; }
inline uint32_t wantDCPR2(uint32_t v) { return v | DCPR2_BITS; }
inline uint32_t wantDBuf(uint32_t v) { return (v & ~DBUF_TRACK_MASK) | DBUF_TRACK; }

struct Config {
	bool count = false;      // opt-in: W1C PIPESTATUS bit31 after recording it
	uint32_t wa = 0;         // WA mask, default none
	uint32_t pageMask = 0;   // required for WABuddyMask; 0 = never written
};

struct Snapshot {
	uint32_t valid, dcState, pwrWell;
	uint32_t imrValid;
	uint32_t buddyCtl[2], buddyMask[2], dcpr2, dbuf[2], southGate, psr, psr2;
	uint32_t imr[kPipes];
	// Workaround-relevant fields only; DC/power/IMR state changes constantly.
	bool sameWorkaroundState(const Snapshot &o) const {
		for (uint32_t i = 0; i < 2; ++i)
			if (buddyCtl[i] != o.buddyCtl[i] || buddyMask[i] != o.buddyMask[i] || dbuf[i] != o.dbuf[i]) return false;
		return dcpr2 == o.dcpr2 && southGate == o.southGate && psr == o.psr && psr2 == o.psr2;
	}
};

struct Event { uint64_t ns; uint32_t pipe, frame, flips, status; };

struct PipeStats {
	uint64_t underruns, firstNs, lastNs;
	uint32_t lastFrame, lastFlips, powered, stickyHeld;
	uint32_t firstSampled, stickyAtFirstSample;  // bit31 already set when first observed
	uint32_t baselineValid, stickyAfterGap;
};

constexpr uint32_t kRing = 64;
struct State {
	uint64_t ticks, sampled, skippedDC, skippedPW1, unavailable, writes;
	// waWrites index: 0,1 BUDDY_CTL; 2,3 BUDDY_MASK; 4 DCPR_2; 5,6 DBUF_CTL S1/S2.
	uint64_t snapshotChanges, waWrites[7], waReapplied;
	uint32_t ringNext, dcLast;
	PipeStats pipe[kPipes];
	Snapshot first, last;
	Event ring[kRing];
};

inline void samplingGap(State &st) {
	for (auto &p : st.pipe) { p.baselineValid = 0; p.powered = 0; }
}

// IO must provide: uint32_t read(uint32_t), void write(uint32_t, uint32_t).
template <class IO>
inline void snapshot(IO &io, Snapshot &s, uint32_t dc, uint32_t pw) {
	s = Snapshot {};
	s.valid = 1; s.dcState = dc; s.pwrWell = pw;
	for (uint32_t i = 0; i < 2; ++i) {
		s.buddyCtl[i] = io.read(BUDDY_CTL[i]);
		s.buddyMask[i] = io.read(BUDDY_MASK[i]);
		s.dbuf[i] = io.read(DBUF_CTL[i]);
	}
	s.dcpr2 = io.read(DCPR_2);
	s.southGate = io.read(SOUTH_DSPCLK_GATE_D);
	s.psr = io.read(PSR_CTL);
	s.psr2 = io.read(PSR2_CTL);
	for (uint32_t p = 0; p < kPipes; ++p)
		if (pw & pipePowerBit(p)) {
			s.imr[p] = io.read(DE_PIPE_IMR(p));
			// An IMR of all ones is a legitimate all-masked value. Availability
			// comes from the caller's lease, never this field's value alone.
			s.imrValid |= 1U << p;
		}
	for (uint32_t i = 0; i < 2; ++i)
		if (s.buddyCtl[i] == UINT32_MAX || s.buddyMask[i] == UINT32_MAX || s.dbuf[i] == UINT32_MAX) s.valid = 0;
	if (s.dcpr2 == UINT32_MAX || s.southGate == UINT32_MAX || s.psr == UINT32_MAX || s.psr2 == UINT32_MAX) s.valid = 0;
}

template <class IO>
inline void writeTracked(IO &io, State &st, uint32_t reg, uint32_t value) {
	io.write(reg, value);
	++st.writes;
}

template <class IO>
inline bool applyOne(IO &io, State &st, uint32_t reg, uint32_t bits, uint32_t mask, uint32_t slot, bool &reapply) {
	const uint32_t now = io.read(reg);
	if (now == UINT32_MAX) { ++st.unavailable; return false; }
	const uint32_t want = (now & ~mask) | (bits & mask);
	if (now == want) return false;
	writeTracked(io, st, reg, want);
	if (st.waWrites[slot]) reapply = true;   // value reverted after our write
	++st.waWrites[slot];
	return true;
}

template <class IO>
inline void applyWorkarounds(IO &io, State &st, const Config &cfg) {
	bool reapply = false;
	if (cfg.wa & WABuddyTimer)
		for (uint32_t i = 0; i < 2; ++i) applyOne(io, st, BUDDY_CTL[i], BUDDY_TIMER, BUDDY_TIMER_MASK, i, reapply);
	if ((cfg.wa & WABuddyMask) && cfg.pageMask)
		for (uint32_t i = 0; i < 2; ++i) applyOne(io, st, BUDDY_MASK[i], cfg.pageMask, UINT32_MAX, 2 + i, reapply);
	if (cfg.wa & WADCPR2)
		applyOne(io, st, DCPR_2, DCPR2_BITS, DCPR2_BITS, 4, reapply);
	if (cfg.wa & WADBufTrack)
		for (uint32_t i = 0; i < 2; ++i) {
			const uint32_t v = io.read(DBUF_CTL[i]);
			if (v == UINT32_MAX) { ++st.unavailable; continue; }
			if (v & DBUF_POWERED) applyOne(io, st, DBUF_CTL[i], DBUF_TRACK, DBUF_TRACK_MASK, 5 + i, reapply);
		}
	if (reapply) ++st.waReapplied;
}

// One sampling step. Returns false when the tick was skipped for DC states.
template <class IO>
inline bool tick(IO &io, State &st, const Config &cfg, uint64_t nowNs) {
	++st.ticks;
	const uint32_t dc = io.read(DC_STATE_EN);
	st.dcLast = dc;
	if (dc == 0xFFFFFFFFU) { ++st.unavailable; samplingGap(st); return false; }
	if (dc & DC_MASK) { ++st.skippedDC; samplingGap(st); return false; }
	const uint32_t pw = io.read(PWR_WELL_CTL2);
	if (pw == 0xFFFFFFFFU) { ++st.unavailable; samplingGap(st); return false; }
	if (!(pw & PW1_STATE)) { ++st.skippedPW1; samplingGap(st); return false; }
	++st.sampled;
	for (uint32_t p = 0; p < kPipes; ++p) {
		auto &ps = st.pipe[p];
		ps.powered = (pw & pipePowerBit(p)) != 0;
		if (!ps.powered) { ps.baselineValid = 0; continue; }
		const uint32_t status = io.read(PIPESTATUS(p));
		if (status == 0xFFFFFFFFU) { ++st.unavailable; ps.baselineValid = 0; continue; }
		if (!ps.baselineValid) {
			const bool first = !ps.firstSampled;
			ps.firstSampled = 1;
			ps.baselineValid = 1;
			if (status & UNDERRUN) {
				// Set before we ever looked: an old/unknown-time underrun, not an event.
				if (first) ps.stickyAtFirstSample = 1;
				else ++ps.stickyAfterGap;
				if (cfg.count) writeTracked(io, st, PIPESTATUS(p), UNDERRUN);
				else ps.stickyHeld = 1;
				continue;
			}
		}
		if (!(status & UNDERRUN)) { ps.stickyHeld = 0; continue; }
		if (!cfg.count && ps.stickyHeld) continue;   // ReadOnly: one event per assertion
		const uint32_t frame = io.read(FRMCOUNT(p)), flips = io.read(FLIPCOUNT(p));
		++ps.underruns;
		if (!ps.firstNs) ps.firstNs = nowNs;
		ps.lastNs = nowNs; ps.lastFrame = frame; ps.lastFlips = flips;
		st.ring[st.ringNext % kRing] = Event {nowNs, p, frame, flips, status};
		++st.ringNext;
		if (cfg.count) writeTracked(io, st, PIPESTATUS(p), UNDERRUN);
		else ps.stickyHeld = 1;
	}
	Snapshot s;
	snapshot(io, s, dc, pw);
	if (!s.valid) ++st.unavailable;
	else if (!st.first.valid) st.first = s;
	else if (st.last.valid && !s.sameWorkaroundState(st.last)) ++st.snapshotChanges;
	st.last = s;
	if (cfg.wa) applyWorkarounds(io, st, cfg);
	return true;
}

// ---------------------------------------------------------------------------
// Native coverage counters of AppleIntelTGLGraphicsFramebuffer
// 115a4616e862228a8c82df6e52ba41ac02b9750f116352ff3575aadcc950888e
// (identical in WhateverXe-Graphics 0.4.1 and 0.4.3, Tahoe payload).
//
// invokeDEErrorIntrHandler(0x744BC) increments, in __DATA,__llvm_prf_cnts:
//   0xE6CD8 entry, 0xE6CE0/0xE6CE8/0xE6CF0 pipe A/B/C underrun bit sums;
//   SWInterruptHandler's dispatch branch at 0xE6BC8.
// The runtime address is derived from a live AppleIntelFramebufferController:
// its vptr (file 0xCBA70+0x10, __DATA,__const) and its gMetaClass (file
// 0x138B80, __DATA,__common) bracket the counters inside one __DATA segment.
// Qualification requires the data-segment delta between them to match the
// file exactly, and four vtable text-pointer deltas to match the file, before
// any counter is read.
namespace Donor {
constexpr uint64_t kVPtr = 0xCBA80, kMetaClass = 0x138B80;
constexpr uint64_t kCounters[5] {0xE6CD8, 0xE6CE0, 0xE6CE8, 0xE6CF0, 0xE6BC8};
struct Slot { uint32_t index; uint64_t fileTarget; };
constexpr Slot kSlots[] {
	{0, 0x3E7A2},    // ~AppleIntelFramebufferController
	{7, 0x3E852},    // getMetaClass
	{183, 0x446BC},  // probe
	{184, 0x3F1FC},  // start
	{185, 0x44982},  // stop
};
// vptr: the object's first word; slots: reader for vptr[i]; metaClass:
// address returned by obj->getMetaClass(). On success, out[i] = runtime
// address of kCounters[i].
template <class SlotReader>
inline bool qualify(uint64_t vptr, uint64_t metaClass, SlotReader slot, uint64_t out[5]) {
	if (!vptr || (vptr & 7) || metaClass <= vptr) return false;
	if (metaClass - vptr != kMetaClass - kVPtr) return false;
	const uint64_t t0 = slot(kSlots[0].index);
	if (!t0) return false;
	for (const auto &s : kSlots) {
		const uint64_t t = slot(s.index);
		if (t - t0 != s.fileTarget - kSlots[0].fileTarget) return false;
	}
	for (uint32_t i = 0; i < 5; ++i) out[i] = vptr - kVPtr + kCounters[i];
	return true;
}

// Verify the live increment instructions refer to the qualified DATA counters.
// Text and DATA can have different slides. Decode the relocated RIP displacement
// rather than assuming file offsets are runtime pointers. Reader must fail safely.
template <class Reader>
inline bool qualifyInstructions(uint64_t firstTextSlot, const uint64_t counters[5], Reader read) {
	constexpr uint64_t insns[5] {0x744CA, 0x744EC, 0x74514, 0x74539, 0x74084};
	for (uint32_t i = 0; i < 5; ++i) {
		uint8_t b[7] {};
		const uint64_t address = firstTextSlot + (insns[i] - kSlots[0].fileTarget);
		if (!read(address, b, sizeof(b)) || b[0] != 0x48 || b[1] != ((i == 0 || i == 4) ? 0xFF : 0x01) || b[2] != 0x05) return false;
		const uint32_t disp = uint32_t(b[3]) | uint32_t(b[4]) << 8 | uint32_t(b[5]) << 16 | uint32_t(b[6]) << 24;
		if (address + 7 + static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(disp))) != counters[i]) return false;
	}
	return true;
}

template <class Reader>
inline bool sample(const uint64_t addresses[5], uint64_t values[5], Reader read) {
	uint64_t temporary[5] {};
	for (uint32_t i = 0; i < 5; ++i)
		if (!read(addresses[i], &temporary[i], sizeof(temporary[i]))) return false;
	for (uint32_t i = 0; i < 5; ++i) values[i] = temporary[i];
	return true;
}
} // namespace Donor

} // namespace TGLDP
#endif
