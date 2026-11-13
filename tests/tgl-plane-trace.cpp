// CPU tests for src/TGLPlaneTrace.hpp. No IOKit, no hardware.
#include "../src/TGLPlaneTrace.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace TGLPlane;

static constexpr uint64_t kThread = 0xFFFFFF8012345678ULL, kAccess = 0xFFFFFF80ABCDEF00ULL;
static uint64_t seq = 0;
static Record W(uint64_t ns, uint64_t reg, uint32_t value, uint32_t caller = 0x1234, uint32_t group = 1) {
	Record r; r.ns = ns; r.thread = kThread; r.seq = ++seq; r.reg = reg; r.value = value; r.caller = caller; r.access = kAccess; r.group = group;
	return r;
}
static Sample S(uint64_t ns, uint64_t c0, uint64_t c1) { Sample s; s.ns = ns; s.count[0] = c0; s.count[1] = c1; s.counted[0] = s.counted[1] = true; return s; }
static uint64_t P(int plane, uint32_t off) { return kPlaneBase + uint64_t(plane) * kPlaneStride + off; }

static void decoding() {
	assert(planeOf(0x7019C) == 0 && offOf(0x7019C) == SURF && !strcmp(planeRegName(offOf(0x7019C)), "SURF"));
	assert(planeOf(0x7029C) == 1 && planeOf(0x70400) == 2 && planeOf(0x70480) == -1 && planeOf(0x70008) == -1);
	assert(!strcmp(planeRegName(offOf(0x7027C)), "BUF_CFG") && !strcmp(planeRegName(offOf(0x70240)), "WM"));
	assert(!strcmp(planeRegName(offOf(0x701C0)), "AUX_DIST") && !strcmp(planeRegName(offOf(0x701CC)), "COLOR_CTL"));
	const auto c = decodeCtl(0x80000000U | (0x8U << 23) | (4U << 10) | (1U << 15) | (1U << 9) | (1U << 4) | 2U);
	assert(c.enable && c.async && c.rcDecomp && c.mcDecomp && c.format == 0x8 && c.tiling == 4 && c.rotation == 2);
	assert(!strcmp(formatName(c.format), "XRGB8888") && !strcmp(tilingName(c.tiling), "Y"));
	const auto off = decodeCtl(0x02000000U);
	assert(!off.enable && !off.async && !strcmp(formatName(off.format), "XRGB2101010"));
}

static void dedupeGapsAndWindow() {
	seq = 0; Trace t;
	std::vector<Record> first;
	for (int i = 0; i < 100; ++i) first.push_back(W(1000 + i, P(0, CTL), 0x80000000U));
	assert(t.ingest(S(5000, 0, 100), first) == 100);
	assert(t.ingest(S(6000, 0, 100), first) == 0 && t.gaps().empty());      // duplicate publication
	std::vector<Record> last;
	for (int i = 0; i < 600; ++i) { auto r = W(7000 + i, P(0, STRIDE), 4); if (i >= 600 - 512) last.push_back(r); }
	assert(t.ingest(S(9000, 0, 700), last) == 512);
	assert(t.gaps().size() == 1 && t.gaps()[0].lost == 88 && t.gaps()[0].group == 1);
	assert(t.gaps()[0].fromNs == 6000 && t.gaps()[0].toNs == 7000 + 88);
	const auto rep = analyze(t);
	assert(rep.records == 612 && rep.recordsInWindow == 512);           // boot/pre-window history excluded
	assert(rep.lost[1] == 88 && rep.delta[1] == 600 && rep.plane[0].writes == 512 && rep.plane[0].ctl.empty());
	// A counter that goes backwards is a reload, recorded as a zero-loss break.
	t.ingest(S(9500, 0, 3), {});
	assert(t.gaps().size() == 2 && t.gaps()[1].lost == 0);
}

static void postArmAndReuse() {
	seq = 0; Trace t; std::vector<Record> v;
	const uint32_t surfaces[3] = {0x100000, 0x200000, 0x300000};
	uint64_t ns = 1000000;
	for (int f = 0; f < 6; ++f, ns += 16666666) {
		v.push_back(W(ns, P(0, CTL), 0x80000000U | (0x8U << 23) | (4U << 10)));
		v.push_back(W(ns + 1000, P(0, POS), 0));
		v.push_back(W(ns + 2000, P(0, SIZE), (1079U << 16) | 1919U));
		v.push_back(W(ns + 3000, P(0, SURF), surfaces[f % 3]));
		if (f == 2) v.push_back(W(ns + 103000, P(0, STRIDE), 30, 0x4242));   // late write after arm
	}
	t.ingest(S(1, 0, v.size()), {}); t.ingest(S(ns, 0, v.size() * 2), v);
	const auto rep = analyze(t);
	const auto &p = rep.plane[0];
	assert(p.surf == 6 && p.surfaces.size() == 3 && p.surfReuse == 3 && p.reuseDepthHist[2] == 3);
	assert(p.minReuseNs == 3 * 16666666ULL && p.surfSameAsPrev == 0);
	assert(p.postArm == 1 && p.postArmByName.at("STRIDE") == 1 && p.postArmCallers.at(0x4242) == 1);
	assert(p.postArmHist[1] == 1);                                       // 100 us bucket
	assert(p.rects.size() == 1 && p.posSize.at(0) == 6 && p.enableTransitions == 0);
	auto j = toJSON(rep);
	assert(j.find("\"w\":1920") != std::string::npos && j.find("\"h\":1080") != std::string::npos);
}

static void overlayAndBufCfg() {
	seq = 0; Trace t; std::vector<Record> v;
	v.push_back(W(100, P(1, CTL), 0));                                        // plane 2 off
	v.push_back(W(200, P(1, POS), (600U << 16) | 40U));
	v.push_back(W(300, P(1, SIZE), (199U << 16) | 299U));
	v.push_back(W(400, P(1, CTL), 0x80000000U | (0x2U << 23) | (1U << 4)));  // NV12 + media decomp
	v.push_back(W(500, P(1, SURF), 0x900000));
	v.push_back(W(600, P(0, BUF_CFG), (100U << 16) | 0));
	v.push_back(W(700, P(0, BUF_CFG), (100U << 16) | 0));
	v.push_back(W(800, P(0, BUF_CFG), (80U << 16) | 0));
	v.push_back(W(900, P(1, CTL), 0));
	t.ingest(S(50, 0, 0), {}); t.ingest(S(1000, 0, v.size()), v);
	const auto rep = analyze(t);
	const auto &o = rep.plane[1];
	assert(o.enableTransitions == 1 && o.disableTransitions == 1 && o.mcCtl == 1 && o.ctlEnableWrites == 1);
	assert(o.rects.size() == 1 && (o.rects[0].first & 0x1FFF) == 40 && (o.rects[0].first >> 16) == 600);
	assert(rep.plane[0].bufCfg == 3 && rep.plane[0].bufCfgChanges == 1 && rep.plane[0].bufCfgValues.size() == 2);
	const auto j = toJSON(rep);
	assert(j.find("\"format\":\"NV12\"") != std::string::npos && j.find("\"mc_decomp\":true") != std::string::npos);
	assert(j.find("\"x\":40,\"y\":600,\"w\":300,\"h\":200") != std::string::npos);
}

static void marksAndGroup0() {
	seq = 0; Trace t; std::vector<Record> v;
	const uint64_t s = 1000000000ULL;
	v.push_back(W(s + 10, 0x45404, 0x5, 0x77, 0));
	v.push_back(W(s + 20, 0x45404, 0x5, 0x77, 0));
	v.push_back(W(s + 30, 0x45404, 0x15, 0x78, 0));
	v.push_back(W(s + 3000000000ULL, P(0, POS), 1));
	v.push_back(W(s + 3000000100ULL, P(0, POS), 2));
	t.ingest(S(s, 0, 0), {}); t.ingest(S(s + 6000000000ULL, 3, 2), v);
	t.mark(s + 3200000000ULL);      // 200 ms after the POS change
	t.mark(s + 100);                // window starts before capture -> uncovered
	const auto rep = analyze(t);
	assert(rep.group0Counts.at(0x45404) == 3 && rep.group0Changes.size() == 2 && rep.group0Changes[1].to == 0x15);
	assert(rep.marks.size() == 2 && rep.marks[0].covered && rep.marks[0].posSizeChanges == 1 && rep.marks[0].planeWrites == 2);
	assert(!rep.marks[1].covered && rep.marks[1].group0 == 3);
	const auto j = toJSON(rep);
	assert(j.find("PWR_WELL_CTL2") != std::string::npos);
	// Thread and access pointers are not emitted in any form.
	char a[32], b[32], c[32], d[32];
	snprintf(a, sizeof(a), "%llx", (unsigned long long)kThread); snprintf(b, sizeof(b), "%llx", (unsigned long long)kAccess);
	snprintf(c, sizeof(c), "%llu", (unsigned long long)kThread); snprintf(d, sizeof(d), "%llu", (unsigned long long)kAccess);
	for (const char *x : {a, b, c, d}) assert(j.find(x) == std::string::npos);
}

static void lostRecordsUncoverMark() {
	seq = 0; Trace t;
	t.ingest(S(1000, 0, 10), {});
	std::vector<Record> v; for (int i = 0; i < 512; ++i) v.push_back(W(5000000000ULL + i, P(0, SURF), 0x1000));
	t.ingest(S(6000000000ULL, 0, 2000), v);
	t.mark(4000000000ULL);
	const auto rep = analyze(t);
	assert(rep.lost[1] == 2000 - 10 - 512 && !rep.marks[0].covered);
	assert(rep.plane[0].surfSameAsPrev == 511);
}

static void absentRecorderNeverCovers() {
	Trace t; Sample s; s.ns = 1000;                       // counted[] false: property missing
	t.ingest(s, {}); s.ns = 5000000000ULL; t.ingest(s, {});
	t.mark(3000000000ULL);
	const auto rep = analyze(t);
	assert(!rep.counted[1] && rep.marks.size() == 1 && !rep.marks[0].covered);
}

int main() {
	decoding();
	dedupeGapsAndWindow();
	postArmAndReuse();
	overlayAndBufCfg();
	marksAndGroup0();
	lostRecordsUncoverMark();
	absentRecorderNeverCovers();
	puts("tgl-plane-trace logic: 7 cases passed (CPU only).");
	return 0;
}
