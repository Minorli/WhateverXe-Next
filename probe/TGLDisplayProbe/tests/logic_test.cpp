// Host CPU tests for TGLDisplayProbeLogic.hpp. No hardware access.
#include "../TGLDisplayProbeLogic.hpp"
#include <cassert>
#include <cstdio>
#include <map>
#include <set>
#include <vector>

using namespace TGLDP;

struct FakeIO {
	std::map<uint32_t, uint32_t> reg;
	std::vector<std::pair<uint32_t, uint32_t>> writes;
	std::set<uint32_t> reads;
	uint32_t read(uint32_t r) {
		assert(allowed(r) && "read outside allow list");
		reads.insert(r);
		auto it = reg.find(r);
		return it == reg.end() ? 0 : it->second;
	}
	void write(uint32_t r, uint32_t v) {
		assert(allowed(r) && "write outside allow list");
		writes.push_back({r, v});
		if (r == PIPESTATUS(0) || r == PIPESTATUS(1) || r == PIPESTATUS(2)) reg[r] &= ~v; // W1C
		else reg[r] = v;
	}
};

static FakeIO poweredA() {
	FakeIO io;
	io.reg[DC_STATE_EN] = 0;
	io.reg[PWR_WELL_CTL2] = PW1_STATE;          // only pipe A powered
	io.reg[FRMCOUNT(0)] = 1000; io.reg[FLIPCOUNT(0)] = 77;
	io.reg[BUDDY_CTL[0]] = 0x80000000U | (3U << 16) | 0x5; io.reg[BUDDY_CTL[1]] = 0x00010000;
	io.reg[DCPR_2] = 0x00000011; io.reg[DBUF_CTL[0]] = DBUF_POWERED | 0x80000000U;
	io.reg[DBUF_CTL[1]] = 0;                     // slice 2 unpowered
	return io;
}
static Config countCfg() { Config c; c.count = true; return c; }

static void dcBlocksEverything() {
	for (uint32_t dc : {1U, 2U, 8U}) {
		FakeIO io = poweredA(); io.reg[DC_STATE_EN] = dc; io.reg[PIPESTATUS(0)] = UNDERRUN;
		State st {}; Config cfg = countCfg(); cfg.wa = WAAll; cfg.pageMask = 0x1F;
		assert(!tick(io, st, cfg, 1));
		assert(io.reads == std::set<uint32_t>{DC_STATE_EN});
		assert(io.writes.empty() && st.skippedDC == 1 && st.pipe[0].underruns == 0);
	}
}

static void allOnesIsUnavailable() {
	FakeIO io = poweredA(); io.reg[DC_STATE_EN] = 0xFFFFFFFFU;
	State st {}; Config cfg = countCfg(); cfg.wa = WAAll; cfg.pageMask = 0x1F;
	assert(!tick(io, st, cfg, 1) && st.unavailable == 1 && st.skippedDC == 0 && io.writes.empty());
	io.reg[DC_STATE_EN] = 0; io.reg[PWR_WELL_CTL2] = 0xFFFFFFFFU;
	assert(!tick(io, st, cfg, 2) && st.unavailable == 2 && io.writes.empty());
	io.reg[PWR_WELL_CTL2] = PW1_STATE; io.reg[PIPESTATUS(0)] = 0xFFFFFFFFU;
	tick(io, st, cfg, 3);
	assert(st.unavailable == 3 && st.pipe[0].underruns == 0 && !st.pipe[0].firstSampled);
	for (auto &w : io.writes) assert(w.first != PIPESTATUS(0));
}

static void pw1OffBlocks() {
	FakeIO io = poweredA(); io.reg[PWR_WELL_CTL2] = 0;
	State st {}; Config cfg = countCfg();
	assert(!tick(io, st, cfg, 1) && st.skippedPW1 == 1);
	assert((io.reads == std::set<uint32_t>{DC_STATE_EN, PWR_WELL_CTL2}) && io.writes.empty());
}

static void defaultConfigNeverWrites() {
	FakeIO io = poweredA();
	State st {}; Config cfg;                     // defaults
	assert(!cfg.count && cfg.wa == 0);
	for (int i = 0; i < 4; ++i) { io.reg[PIPESTATUS(0)] = i % 2 ? UNDERRUN : 0; tick(io, st, cfg, i + 1); }
	assert(io.writes.empty() && st.writes == 0 && st.pipe[0].underruns == 2);
}

static void stickyAtFirstSampleIsNotAnEvent() {
	for (bool count : {false, true}) {
		FakeIO io = poweredA(); io.reg[PIPESTATUS(0)] = UNDERRUN;
		State st {}; Config cfg; cfg.count = count;
		for (int i = 0; i < 3; ++i) tick(io, st, cfg, i + 1);
		assert(st.pipe[0].stickyAtFirstSample == 1 && st.pipe[0].underruns == 0 && st.ringNext == 0);
		assert(io.writes.size() == (count ? 1u : 0u));
		// A later fresh assertion is an event in both modes.
		io.reg[PIPESTATUS(0)] = 0; tick(io, st, cfg, 10);
		io.reg[PIPESTATUS(0)] = UNDERRUN; tick(io, st, cfg, 11);
		assert(st.pipe[0].underruns == 1 && st.pipe[0].firstNs == 11);
	}
}

static void countModeW1C() {
	FakeIO io = poweredA();
	State st {}; Config cfg = countCfg();
	tick(io, st, cfg, 50);                       // first sample clean
	io.reg[PIPESTATUS(0)] = UNDERRUN | 0x5;
	assert(tick(io, st, cfg, 100));
	assert(st.pipe[0].underruns == 1 && st.pipe[0].firstNs == 100 && st.pipe[0].lastFrame == 1000 && st.pipe[0].lastFlips == 77);
	assert(io.writes.size() == 1 && io.writes[0].first == PIPESTATUS(0) && io.writes[0].second == UNDERRUN);
	assert(io.reg[PIPESTATUS(0)] == 0x5);        // other bits untouched
	assert(st.ringNext == 1 && st.ring[0].status == (UNDERRUN | 0x5));
	assert(tick(io, st, cfg, 200) && st.pipe[0].underruns == 1 && io.writes.size() == 1);
	io.reg[PIPESTATUS(0)] |= UNDERRUN;
	assert(tick(io, st, cfg, 300) && st.pipe[0].underruns == 2 && st.pipe[0].lastNs == 300 && st.pipe[0].firstNs == 100);
}

static void readOnlyCountsAssertionsOnce() {
	FakeIO io = poweredA();
	State st {}; Config cfg;
	tick(io, st, cfg, 1);
	io.reg[PIPESTATUS(0)] = UNDERRUN;
	for (int i = 0; i < 5; ++i) tick(io, st, cfg, 10 + i);
	assert(io.writes.empty() && st.pipe[0].underruns == 1);
	io.reg[PIPESTATUS(0)] = 0; tick(io, st, cfg, 20);
	io.reg[PIPESTATUS(0)] = UNDERRUN; tick(io, st, cfg, 21);
	assert(st.pipe[0].underruns == 2);
}

static void unpoweredPipesNeverRead() {
	FakeIO io = poweredA(); io.reg[PIPESTATUS(1)] = UNDERRUN; io.reg[PIPESTATUS(2)] = UNDERRUN;
	State st {}; Config cfg = countCfg();
	tick(io, st, cfg, 1);
	for (uint32_t p = 1; p < kPipes; ++p) {
		assert(!io.reads.count(PIPESTATUS(p)) && !io.reads.count(FRMCOUNT(p)) && !io.reads.count(DE_PIPE_IMR(p)));
		assert(st.pipe[p].underruns == 0 && !st.pipe[p].powered);
	}
	io.reg[PWR_WELL_CTL2] = PW1_STATE | pipePowerBit(1);
	tick(io, st, cfg, 2);
	assert(st.pipe[1].stickyAtFirstSample == 1 && st.pipe[1].underruns == 0 && !io.reads.count(PIPESTATUS(2)));
	io.reg[PIPESTATUS(1)] = UNDERRUN; tick(io, st, cfg, 3);
	assert(st.pipe[1].underruns == 1 && st.pipe[2].underruns == 0);
}

static void workaroundsFieldOnlyAndIdempotent() {
	FakeIO io = poweredA();
	State st {}; Config cfg; cfg.wa = WABuddyTimer | WADCPR2 | WADBufTrack;
	tick(io, st, cfg, 1);
	assert(io.reg[BUDDY_CTL[0]] == (0x80000000U | BUDDY_TIMER | 0x5));
	assert(io.reg[BUDDY_CTL[1]] == BUDDY_TIMER);
	assert(io.reg[DCPR_2] == (0x11U | DCPR2_BITS));
	assert(io.reg[DBUF_CTL[0]] == (DBUF_POWERED | 0x80000000U | DBUF_TRACK));
	assert(io.reg[DBUF_CTL[1]] == 0);
	assert(io.reg[BUDDY_MASK[0]] == 0 && io.reg[BUDDY_MASK[1]] == 0);
	const auto n = io.writes.size();
	assert(n == 4 && st.waReapplied == 0);
	tick(io, st, cfg, 2);
	assert(io.writes.size() == n);
	io.reg[DCPR_2] = 0x11;
	tick(io, st, cfg, 3);
	assert(io.reg[DCPR_2] == (0x11U | DCPR2_BITS) && st.waReapplied == 1 && st.waWrites[4] == 2);
	assert(st.snapshotChanges >= 1);
}

static void pageMaskNeverGuessed() {
	FakeIO io = poweredA(); io.reg[BUDDY_MASK[0]] = 0x3; io.reg[BUDDY_MASK[1]] = 0x3;
	State st {}; Config cfg; cfg.wa = WABuddyMask;
	tick(io, st, cfg, 1);
	assert(io.writes.empty());
	cfg.pageMask = 0x1F;
	tick(io, st, cfg, 2);
	assert(io.reg[BUDDY_MASK[0]] == 0x1F && io.reg[BUDDY_MASK[1]] == 0x1F && io.writes.size() == 2);
}

static void ringWraps() {
	FakeIO io = poweredA();
	State st {}; Config cfg = countCfg();
	tick(io, st, cfg, 1);
	for (uint32_t i = 0; i < kRing + 5; ++i) { io.reg[PIPESTATUS(0)] = UNDERRUN; io.reg[FRMCOUNT(0)] = i; tick(io, st, cfg, i + 2); }
	assert(st.ringNext == kRing + 5 && st.pipe[0].underruns == kRing + 5);
	assert(st.ring[(kRing + 4) % kRing].frame == kRing + 4 && st.ring[0].frame == kRing);
}

static void donorQualification() {
	using namespace Donor;
	const uint64_t dataSlide = 0xFFFFFF8012340000ULL, textSlide = 0xFFFFFF7F80000000ULL; // separate segments
	std::map<uint32_t, uint64_t> vt;
	for (auto &s : kSlots) vt[s.index] = textSlide + s.fileTarget;
	auto reader = [&](uint32_t i) { return vt.count(i) ? vt[i] : 0; };
	const uint64_t vptr = dataSlide + kVPtr, meta = dataSlide + kMetaClass;
	uint64_t out[5] {};
	assert(qualify(vptr, meta, reader, out));
	for (int i = 0; i < 5; ++i) assert(out[i] == dataSlide + kCounters[i]);
	assert(out[1] == vptr + (0xE6CE0 - 0xCBA80));
	uint64_t none[5] {};
	assert(!qualify(vptr, meta + 8, reader, none) && none[0] == 0);          // data layout differs
	assert(!qualify(vptr + 4, meta + 4, reader, none));                       // misaligned vptr
	assert(!qualify(0, kMetaClass - kVPtr, reader, none));
	vt[184] += 0x10;                                                          // different binary
	assert(!qualify(vptr, meta, reader, none) && none[0] == 0);
}

int main() {
	dcBlocksEverything();
	allOnesIsUnavailable();
	pw1OffBlocks();
	defaultConfigNeverWrites();
	stickyAtFirstSampleIsNotAnEvent();
	countModeW1C();
	readOnlyCountsAssertionsOnce();
	unpoweredPipesNeverRead();
	workaroundsFieldOnlyAndIdempotent();
	pageMaskNeverGuessed();
	ringWraps();
	donorQualification();
	puts("TGLDisplayProbe logic: 12 cases passed (CPU only, no hardware access).");
	return 0;
}
