// SPDX-License-Identifier: MIT
// Pure decoder for WhateverXe's existing display-programming recorder
// (tglProgramRecord in 0.8.0 / downstream 0.8.1 / main). The kernel copies
// 8 x uint64 per requested register write: ns, thread, global seq, reg, value,
// fast, caller offset (image-relative or 0xFFFFFFFF) and access pointer.
// Group 1 = pipe A plane registers 0x70180..0x70400 (planes 1..3).
// Group 0 = pipe/transcoder/power-well registers (modeset and power events).
//
// These are writes requested at the wrapper boundary, NOT proof of what the
// hardware latched. No MMIO here. Thread/access pointers never leave this
// module except as small opaque indexes.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace TGLPlane {

constexpr uint32_t kRecordWords = 8, kRing = 512, kPlaneBase = 0x70180, kPlaneStride = 0x100, kPlanes = 3;
constexpr uint32_t kNoCaller = 0xFFFFFFFFU;

struct Record { uint64_t ns = 0, thread = 0, seq = 0, reg = 0, value = 0, fast = 0, caller = kNoCaller, access = 0; uint32_t group = 0; };

inline Record fromWords(const uint64_t *q, uint32_t group) {
	Record r; r.ns = q[0]; r.thread = q[1]; r.seq = q[2]; r.reg = q[3]; r.value = q[4] & 0xFFFFFFFFULL;
	r.fast = q[5]; r.caller = q[6]; r.access = q[7]; r.group = group; return r;
}

// ---- register decode -------------------------------------------------------
enum Off : uint32_t {
	CTL = 0x00, STRIDE = 0x08, POS = 0x0C, SIZE = 0x10, KEYVAL = 0x14, KEYMSK = 0x18, SURF = 0x1C,
	KEYMAX = 0x20, OFFSET = 0x24, SURFLIVE = 0x2C, AUX_DIST = 0x40, AUX_OFFSET = 0x44, CUS_CTL = 0x48,
	COLOR_CTL = 0x4C, WM0 = 0xC0, WM7 = 0xDC, WM_TRANS = 0xE8, NV12_BUF_CFG = 0xF8, BUF_CFG = 0xFC,
};
inline bool isPlaneReg(uint64_t reg) { return reg >= kPlaneBase && reg < kPlaneBase + kPlanes * kPlaneStride; }
inline int planeOf(uint64_t reg) { return isPlaneReg(reg) ? int((reg - kPlaneBase) / kPlaneStride) : -1; }
inline uint32_t offOf(uint64_t reg) { return uint32_t((reg - kPlaneBase) % kPlaneStride); }
inline bool isWM(uint32_t o) { return (o >= WM0 && o <= WM7 && !(o & 3)) || o == WM_TRANS; }
inline const char *planeRegName(uint32_t o) {
	switch (o) {
	case CTL: return "CTL"; case STRIDE: return "STRIDE"; case POS: return "POS"; case SIZE: return "SIZE";
	case KEYVAL: return "KEYVAL"; case KEYMSK: return "KEYMSK"; case SURF: return "SURF"; case KEYMAX: return "KEYMAX";
	case OFFSET: return "OFFSET"; case SURFLIVE: return "SURFLIVE"; case AUX_DIST: return "AUX_DIST";
	case AUX_OFFSET: return "AUX_OFFSET"; case CUS_CTL: return "CUS_CTL"; case COLOR_CTL: return "COLOR_CTL";
	case WM_TRANS: return "WM_TRANS"; case NV12_BUF_CFG: return "NV12_BUF_CFG"; case BUF_CFG: return "BUF_CFG";
	default: break;
	}
	if (isWM(o)) return "WM";
	if (o >= 0x30 && o < 0x40) return "CC_VAL";
	if (o >= 0x60 && o < 0x90) return "INPUT_CSC";
	return "OTHER";
}
inline const char *group0Name(uint64_t reg) {
	switch (reg) {
	case 0x60000: return "HTOTAL_A"; case 0x60004: return "HBLANK_A"; case 0x60008: return "HSYNC_A";
	case 0x6000C: return "VTOTAL_A"; case 0x60010: return "VBLANK_A"; case 0x60014: return "VSYNC_A";
	case 0x6001C: return "PIPESRC_A"; case 0x60028: return "VSYNCSHIFT_A";
	case 0x60400: return "TRANS_DDI_FUNC_CTL_A"; case 0x60404: return "TRANS_DDI_FUNC_CTL2_A";
	case 0x70008: return "TRANSCONF_A"; case 0x7002C: return "PIPE_MISC2_A"; case 0x70030: return "PIPE_MISC_A";
	case 0x45400: return "PWR_WELL_CTL1"; case 0x45404: return "PWR_WELL_CTL2";
	case 0x45450: return "PWR_WELL_CTL_AUX1"; case 0x45454: return "PWR_WELL_CTL_AUX2";
	case 0x46000: return "CDCLK_CTL";
	default: return "OTHER";
	}
}

// ICL/TGL PLANE_CTL (i915 v6.12 i915_reg.h / intel_universal_plane.c).
struct Ctl { bool enable, async, rcDecomp, mcDecomp, flipH; uint32_t format, tiling, rotation; };
inline Ctl decodeCtl(uint32_t v) {
	return Ctl{((v >> 31) & 1U) != 0, ((v >> 9) & 1U) != 0, ((v >> 15) & 1U) != 0, ((v >> 4) & 1U) != 0,
		((v >> 8) & 1U) != 0, (v >> 23) & 0x1FU, (v >> 10) & 7U, v & 3U};
}
inline const char *formatName(uint32_t f) {
	switch (f) {
	case 0x00: return "YUV422"; case 0x02: return "NV12"; case 0x04: return "XRGB2101010"; case 0x06: return "P010";
	case 0x08: return "XRGB8888"; case 0x0A: return "P012"; case 0x0C: return "XRGB16161616F"; case 0x0E: return "P016";
	case 0x10: return "XYUV"; case 0x01: return "Y210"; case 0x03: return "Y212"; case 0x05: return "Y216";
	case 0x07: return "Y410"; case 0x09: return "Y412"; case 0x0B: return "Y416"; case 0x18: return "INDEXED";
	case 0x1C: return "RGB565"; default: return "UNKNOWN";
	}
}
inline const char *tilingName(uint32_t t) {
	switch (t) { case 0: return "linear"; case 1: return "X"; case 4: return "Y"; case 5: return "Yf"; default: return "reserved"; }
}

// ---- collection ------------------------------------------------------------
struct Sample { uint64_t ns = 0; uint64_t count[2] {}; bool counted[2] {}; };
struct Gap { uint32_t group; uint64_t fromNs, toNs, lost; };

class Trace {
public:
	// Offer every record found in one publication (First + Last arrays).
	// Returns the number of new unique records. seq==0 slots are unused.
	size_t ingest(const Sample &s, const std::vector<Record> &records) {
		size_t added = 0; uint64_t oldestNew[2] {UINT64_MAX, UINT64_MAX}; size_t addedPer[2] {};
		for (const auto &r : records) {
			if (!r.seq || r.group > 1) continue;
			if (!bySeq_.emplace(r.seq, r).second) continue;
			++added; ++addedPer[r.group];
			oldestNew[r.group] = std::min(oldestNew[r.group], r.ns);
		}
		for (uint32_t g = 0; g < 2; ++g) {
			if (!s.counted[g]) continue;
			if (havePrev_[g] && s.count[g] >= prevCount_[g]) {
				const uint64_t delta = s.count[g] - prevCount_[g];
				if (delta > addedPer[g]) gaps_.push_back(Gap{g, prevNs_[g], oldestNew[g] == UINT64_MAX ? s.ns : oldestNew[g], delta - addedPer[g]});
			} else if (havePrev_[g]) {
				gaps_.push_back(Gap{g, prevNs_[g], s.ns, 0}); // counter went backwards: reload/reboot
			}
			if (!havePrev_[g]) firstCount_[g] = s.count[g];
			havePrev_[g] = true; prevCount_[g] = s.count[g]; prevNs_[g] = s.ns; lastCount_[g] = s.count[g];
		}
		samples_.push_back(s);
		return added;
	}
	void mark(uint64_t ns) { marks_.push_back(ns); }
	std::vector<Record> ordered() const { std::vector<Record> v; v.reserve(bySeq_.size()); for (auto &kv : bySeq_) v.push_back(kv.second); return v; }
	const std::vector<Gap> &gaps() const { return gaps_; }
	const std::vector<uint64_t> &marks() const { return marks_; }
	const std::vector<Sample> &samples() const { return samples_; }
	// Records that existed before the first sample are history, not window data.
	uint64_t windowStartNs() const { return samples_.empty() ? 0 : samples_.front().ns; }
	uint64_t windowEndNs() const { return samples_.empty() ? 0 : samples_.back().ns; }
	uint64_t totalDelta(uint32_t g) const { return havePrev_[g] ? lastCount_[g] - firstCount_[g] : 0; }
	bool counted(uint32_t g) const { return havePrev_[g]; }
private:
	std::map<uint64_t, Record> bySeq_;
	std::vector<Gap> gaps_; std::vector<uint64_t> marks_; std::vector<Sample> samples_;
	bool havePrev_[2] {}; uint64_t prevCount_[2] {}, prevNs_[2] {}, firstCount_[2] {}, lastCount_[2] {};
};

// ---- analysis --------------------------------------------------------------
struct Params { uint64_t armWindowNs = 1000000, markBeforeNs = 2000000000ULL, markAfterNs = 500000000ULL; };

struct PlaneStats {
	uint64_t writes = 0, surf = 0, ctlEnableWrites = 0, ctlDisableWrites = 0, enableTransitions = 0, disableTransitions = 0;
	uint64_t postArm = 0, wm = 0, bufCfg = 0, bufCfgChanges = 0, auxWrites = 0, asyncCtl = 0, rcCtl = 0, mcCtl = 0;
	uint64_t surfSameAsPrev = 0, surfReuse = 0, minReuseNs = UINT64_MAX, firstSurfNs = 0, lastSurfNs = 0;
	std::map<std::string, uint64_t> byName, postArmByName;
	std::map<uint32_t, uint64_t> ctl, posSize, bufCfgValues;   // posSize key: index into rects
	std::vector<std::pair<uint32_t, uint32_t>> rects;          // (pos,size)
	std::set<uint64_t> surfaces;
	std::map<uint32_t, uint64_t> postArmCallers;
	uint64_t postArmHist[6] {};                                // <50us,<200us,<1ms,<4ms,<16.7ms,>=
	uint64_t reuseDepthHist[5] {};                             // distinct buffers between reuse: 0,1,2,3,>=4
};
struct Change { uint64_t ns, reg, from, to, caller; bool first; };
struct MarkWindow { uint64_t markNs = 0; bool covered = true; uint64_t records = 0, planeWrites = 0, surf = 0, group0 = 0, postArm = 0;
	uint64_t enableToggles = 0, posSizeChanges = 0, bufCfgChanges = 0, ctlChanges = 0; };
struct Report {
	uint64_t windowNs = 0, records = 0, recordsInWindow = 0, unknownCaller = 0;
	PlaneStats plane[kPlanes];
	std::map<uint64_t, uint64_t> group0Counts;
	std::vector<Change> group0Changes;   // bounded
	std::vector<MarkWindow> marks;
	uint64_t lost[2] {}, delta[2] {}; bool counted[2] {};
	std::vector<Gap> gaps;
};

inline unsigned histBucket(uint64_t d) {
	return d < 50000 ? 0 : d < 200000 ? 1 : d < 1000000 ? 2 : d < 4000000 ? 3 : d < 16700000 ? 4 : 5;
}

inline Report analyze(const Trace &t, const Params &p = {}) {
	Report rep;
	const auto recs = t.ordered();
	const uint64_t w0 = t.windowStartNs(), w1 = t.windowEndNs();
	rep.windowNs = w1 > w0 ? w1 - w0 : 0; rep.records = recs.size(); rep.gaps = t.gaps();
	for (uint32_t g = 0; g < 2; ++g) { rep.counted[g] = t.counted(g); rep.delta[g] = t.totalDelta(g); }
	for (auto &g : rep.gaps) rep.lost[g.group] += g.lost;

	struct PState { bool haveCtl = false, enabled = false; uint32_t lastCtl = 0, lastPos = 0, lastSize = 0, lastBuf = 0;
		bool havePos = false, haveSize = false, haveBuf = false; bool haveSurf = false; uint64_t surfNs = 0, lastSurf = 0;
		std::map<uint64_t, uint64_t> lastUse; std::vector<uint64_t> order; };
	PState st[kPlanes];
	std::map<uint64_t, uint64_t> g0Last;
	struct Ev { uint64_t ns; unsigned kind; }; // 0 plane write, 1 surf, 2 group0, 3 postArm, 4 enable toggle, 5 pos/size change, 6 bufcfg change, 7 ctl change
	std::vector<Ev> evs;

	for (const auto &r : recs) {
		// First[] holds boot-time history and Last[] the ring tail before the
		// first sample; only the sampled interval is analysed.
		if (r.ns < w0 || r.ns > w1) continue;
		++rep.recordsInWindow;
		if (r.caller == kNoCaller) ++rep.unknownCaller;
		const uint32_t v = uint32_t(r.value);
		if (r.group == 0 || !isPlaneReg(r.reg)) {
			++rep.group0Counts[r.reg];
			auto it = g0Last.find(r.reg);
			const bool first = it == g0Last.end();
			if ((first || it->second != v) && rep.group0Changes.size() < 256)
				rep.group0Changes.push_back(Change{r.ns, r.reg, first ? 0 : it->second, v, r.caller, first});
			g0Last[r.reg] = v;
			evs.push_back({r.ns, 2});
			continue;
		}
		const int pi = planeOf(r.reg); const uint32_t o = offOf(r.reg);
		auto &ps = rep.plane[pi]; auto &s = st[pi];
		++ps.writes; ++ps.byName[planeRegName(o)];
		evs.push_back({r.ns, 0});
		if (o != SURF && o != SURFLIVE && s.haveSurf && r.ns >= s.surfNs) {
			const uint64_t d = r.ns - s.surfNs;
			if (d <= p.armWindowNs) {
				++ps.postArm; ++ps.postArmByName[planeRegName(o)]; ++ps.postArmCallers[uint32_t(r.caller)];
				evs.push_back({r.ns, 3});
			}
			++ps.postArmHist[histBucket(d)];
		}
		switch (o) {
		case CTL: {
			const auto c = decodeCtl(v);
			++ps.ctl[v]; c.enable ? ++ps.ctlEnableWrites : ++ps.ctlDisableWrites;
			if (c.async) ++ps.asyncCtl; if (c.rcDecomp) ++ps.rcCtl; if (c.mcDecomp) ++ps.mcCtl;
			if (s.haveCtl && s.enabled != c.enable) { c.enable ? ++ps.enableTransitions : ++ps.disableTransitions; evs.push_back({r.ns, 4}); }
			if (s.haveCtl && s.lastCtl != v) evs.push_back({r.ns, 7});
			s.haveCtl = true; s.enabled = c.enable; s.lastCtl = v; break;
		}
		case POS: if (s.havePos && s.lastPos != v) evs.push_back({r.ns, 5}); s.havePos = true; s.lastPos = v; break;
		case SIZE: if (s.haveSize && s.lastSize != v) evs.push_back({r.ns, 5}); s.haveSize = true; s.lastSize = v; break;
		case BUF_CFG:
			++ps.bufCfg; ++ps.bufCfgValues[v];
			if (s.haveBuf && s.lastBuf != v) { ++ps.bufCfgChanges; evs.push_back({r.ns, 6}); }
			s.haveBuf = true; s.lastBuf = v; break;
		case AUX_DIST: case AUX_OFFSET: ++ps.auxWrites; break;
		case SURF: {
			++ps.surf; if (!ps.firstSurfNs) ps.firstSurfNs = r.ns; ps.lastSurfNs = r.ns;
			const uint64_t addr = v & ~0xFFFULL;
			if (s.haveSurf && s.lastSurf == addr) ++ps.surfSameAsPrev;
			auto it = s.lastUse.find(addr);
			if (it != s.lastUse.end()) {
				++ps.surfReuse; ps.minReuseNs = std::min(ps.minReuseNs, r.ns - it->second);
				std::set<uint64_t> between;
				for (auto j = s.order.rbegin(); j != s.order.rend() && *j != addr; ++j) between.insert(*j);
				++ps.reuseDepthHist[std::min<size_t>(between.size(), 4)];
			}
			s.lastUse[addr] = r.ns; s.order.push_back(addr); if (s.order.size() > 64) s.order.erase(s.order.begin());
			ps.surfaces.insert(addr);
			if (s.havePos && s.haveSize) {
				const auto key = std::make_pair(s.lastPos, s.lastSize);
				auto f = std::find(ps.rects.begin(), ps.rects.end(), key);
				uint32_t idx = uint32_t(f - ps.rects.begin());
				if (f == ps.rects.end()) { if (ps.rects.size() < 32) ps.rects.push_back(key); else idx = UINT32_MAX; }
				if (idx != UINT32_MAX) ++ps.posSize[idx];
			}
			s.haveSurf = true; s.surfNs = r.ns; s.lastSurf = addr;
			evs.push_back({r.ns, 1});
			break;
		}
		default: if (isWM(o)) ++ps.wm; break;
		}
	}
	for (uint64_t m : t.marks()) {
		MarkWindow mw; mw.markNs = m;
		const uint64_t a = m > p.markBeforeNs ? m - p.markBeforeNs : 0, b = m + p.markAfterNs;
		for (auto &g : rep.gaps) if (g.group == 1 && g.lost && g.fromNs < b && g.toNs > a) mw.covered = false;
		if (a < w0 || b > w1) mw.covered = false;
		if (!t.counted(1)) mw.covered = false;   // recorder absent: nothing was observed
		for (auto &e : evs) if (e.ns >= a && e.ns <= b) {
			++mw.records;
			switch (e.kind) { case 0: ++mw.planeWrites; break; case 1: ++mw.surf; break; case 2: ++mw.group0; break;
			case 3: ++mw.postArm; --mw.records; break; case 4: ++mw.enableToggles; --mw.records; break;
			case 5: ++mw.posSizeChanges; --mw.records; break; case 6: ++mw.bufCfgChanges; --mw.records; break;
			case 7: ++mw.ctlChanges; --mw.records; break; }
		}
		rep.marks.push_back(mw);
	}
	return rep;
}

// ---- JSON (no kernel pointers; SURF addresses are counted, not printed) -----
inline std::string hex(uint64_t v) { char b[24]; snprintf(b, sizeof(b), "\"0x%llx\"", static_cast<unsigned long long>(v)); return b; }
inline std::string num(uint64_t v) { return std::to_string(v); }

inline std::string toJSON(const Report &r, const Params &p = {}) {
	std::string j = "{";
	auto kv = [&](const char *k, const std::string &v, bool comma = true) { j += "\""; j += k; j += "\":"; j += v; if (comma) j += ","; };
	kv("schema", "\"tgl-plane-trace/1\"");
	kv("scope", "\"pipe A planes 1-3 requested writes at WEX wrapper; not hardware latch proof\"");
	kv("window_ns", num(r.windowNs)); kv("records_unique", num(r.records)); kv("records_in_window", num(r.recordsInWindow));
	kv("unknown_caller", num(r.unknownCaller)); kv("arm_window_ns", num(p.armWindowNs));
	for (uint32_t g = 0; g < 2; ++g) {
		std::string k = "ring" + std::to_string(g);
		j += "\"" + k + "\":{\"counted\":" + (r.counted[g] ? "true" : "false") + ",\"delta\":" + num(r.delta[g]) +
			",\"lost\":" + num(r.lost[g]) + "},";
	}
	j += "\"planes\":[";
	for (uint32_t i = 0; i < kPlanes; ++i) {
		const auto &ps = r.plane[i];
		if (i) j += ",";
		j += "{\"plane\":" + num(i + 1) + ",\"writes\":" + num(ps.writes) + ",\"surf\":" + num(ps.surf);
		const uint64_t span = ps.lastSurfNs > ps.firstSurfNs ? ps.lastSurfNs - ps.firstSurfNs : 0;
		char rate[32]; snprintf(rate, sizeof(rate), "%.2f", span && ps.surf > 1 ? double(ps.surf - 1) * 1e9 / double(span) : 0.0);
		j += ",\"surf_per_s\":" + std::string(rate) + ",\"distinct_surfaces\":" + num(ps.surfaces.size());
		j += ",\"surf_same_as_prev\":" + num(ps.surfSameAsPrev) + ",\"surf_reuse\":" + num(ps.surfReuse);
		j += ",\"min_reuse_ns\":" + (ps.minReuseNs == UINT64_MAX ? std::string("null") : num(ps.minReuseNs));
		j += ",\"reuse_depth_hist\":[" ; for (int b = 0; b < 5; ++b) { if (b) j += ","; j += num(ps.reuseDepthHist[b]); } j += "]";
		j += ",\"ctl_enable_writes\":" + num(ps.ctlEnableWrites) + ",\"ctl_disable_writes\":" + num(ps.ctlDisableWrites);
		j += ",\"enable_transitions\":" + num(ps.enableTransitions) + ",\"disable_transitions\":" + num(ps.disableTransitions);
		j += ",\"async_ctl\":" + num(ps.asyncCtl) + ",\"rc_decomp_ctl\":" + num(ps.rcCtl) + ",\"mc_decomp_ctl\":" + num(ps.mcCtl);
		j += ",\"aux_writes\":" + num(ps.auxWrites) + ",\"wm_writes\":" + num(ps.wm) + ",\"buf_cfg_writes\":" + num(ps.bufCfg);
		j += ",\"buf_cfg_changes\":" + num(ps.bufCfgChanges) + ",\"post_arm_writes\":" + num(ps.postArm);
		j += ",\"after_surf_delta_hist\":["; for (int b = 0; b < 6; ++b) { if (b) j += ","; j += num(ps.postArmHist[b]); } j += "]";
		j += ",\"post_arm_by_reg\":{"; { bool c = false; for (auto &kv2 : ps.postArmByName) { if (c) j += ","; c = true; j += "\"" + kv2.first + "\":" + num(kv2.second); } } j += "}";
		j += ",\"post_arm_callers\":{"; { bool c = false; for (auto &kv2 : ps.postArmCallers) { if (c) j += ","; c = true; j += hex(kv2.first) + ":" + num(kv2.second); } } j += "}";
		j += ",\"writes_by_reg\":{"; { bool c = false; for (auto &kv2 : ps.byName) { if (c) j += ","; c = true; j += "\"" + kv2.first + "\":" + num(kv2.second); } } j += "}";
		j += ",\"ctl_values\":[";
		{ bool c = false; for (auto &kv2 : ps.ctl) { const auto d = decodeCtl(kv2.first); if (c) j += ","; c = true;
			j += "{\"value\":" + hex(kv2.first) + ",\"count\":" + num(kv2.second) + ",\"enable\":" + (d.enable ? "true" : "false") +
				",\"format\":\"" + formatName(d.format) + "\",\"tiling\":\"" + tilingName(d.tiling) + "\",\"async\":" + (d.async ? "true" : "false") +
				",\"rc_decomp\":" + (d.rcDecomp ? "true" : "false") + ",\"mc_decomp\":" + (d.mcDecomp ? "true" : "false") +
				",\"rotation\":" + num(d.rotation) + "}"; } }
		j += "],\"rects_at_surf\":[";
		for (size_t k = 0; k < ps.rects.size(); ++k) {
			const auto pos = ps.rects[k].first, size = ps.rects[k].second;
			if (k) j += ",";
			auto it = ps.posSize.find(uint32_t(k));
			j += "{\"x\":" + num(pos & 0x1FFF) + ",\"y\":" + num((pos >> 16) & 0x1FFF) + ",\"w\":" + num((size & 0x1FFF) + 1) +
				",\"h\":" + num(((size >> 16) & 0x1FFF) + 1) + ",\"surf\":" + num(it == ps.posSize.end() ? 0 : it->second) + "}";
		}
		j += "],\"buf_cfg_values\":[";
		{ bool c = false; for (auto &kv2 : ps.bufCfgValues) { if (c) j += ","; c = true;
			j += "{\"start\":" + num(kv2.first & 0x1FFF) + ",\"end\":" + num((kv2.first >> 16) & 0x1FFF) + ",\"count\":" + num(kv2.second) + "}"; } }
		j += "]}";
	}
	j += "],\"group0_writes\":{";
	{ bool c = false; for (auto &kv2 : r.group0Counts) { if (c) j += ","; c = true; j += hex(kv2.first) + ":{\"name\":\"" + group0Name(kv2.first) + "\",\"writes\":" + num(kv2.second) + "}"; } }
	j += "},\"group0_changes\":[";
	for (size_t k = 0; k < r.group0Changes.size(); ++k) { const auto &c = r.group0Changes[k]; if (k) j += ",";
		j += "{\"ns\":" + num(c.ns) + ",\"reg\":" + hex(c.reg) + ",\"name\":\"" + group0Name(c.reg) + "\",\"from\":" + (c.first ? std::string("null") : hex(c.from)) +
			",\"to\":" + hex(c.to) + ",\"caller\":" + hex(c.caller) + "}"; }
	j += "],\"gaps\":[";
	for (size_t k = 0; k < r.gaps.size(); ++k) { const auto &g = r.gaps[k]; if (k) j += ",";
		j += "{\"group\":" + num(g.group) + ",\"from_ns\":" + num(g.fromNs) + ",\"to_ns\":" + num(g.toNs) + ",\"lost\":" + num(g.lost) + "}"; }
	j += "],\"marks\":[";
	for (size_t k = 0; k < r.marks.size(); ++k) { const auto &m = r.marks[k]; if (k) j += ",";
		j += "{\"ns\":" + num(m.markNs) + ",\"covered\":" + (m.covered ? "true" : "false") + ",\"plane_writes\":" + num(m.planeWrites) +
			",\"surf\":" + num(m.surf) + ",\"group0\":" + num(m.group0) + ",\"post_arm\":" + num(m.postArm) +
			",\"enable_toggles\":" + num(m.enableToggles) + ",\"pos_size_changes\":" + num(m.posSizeChanges) +
			",\"buf_cfg_changes\":" + num(m.bufCfgChanges) + ",\"ctl_changes\":" + num(m.ctlChanges) + "}"; }
	j += "]}";
	return j;
}

} // namespace TGLPlane
