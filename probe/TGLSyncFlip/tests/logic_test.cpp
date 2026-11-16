// CPU-only tests for TGLSyncFlipLogic.hpp. No kernel, no IOKit.
// With --framebuffer PATH it also checks every anchor against a real
// AppleIntelTGLGraphicsFramebuffer binary (file offsets == vm offsets there).
#include "../TGLSyncFlipLogic.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <thread>
#include <vector>

using namespace TGLSync;

static std::vector<uint8_t> syntheticImage() {
	std::vector<uint8_t> img(0x92600, 0xCC);
	for (const auto &a : Anchors) memcpy(&img[a.offset], a.bytes, a.length);
	return img;
}

static void anchors() {
	auto img = syntheticImage();
	assert(matches(img.data(), img.size()));
	assert(!matches(nullptr, img.size()));
	assert(!matches(img.data(), 0x2BE52));                        // last anchor would overrun
	for (const auto &a : Anchors) {                               // every single-byte change is caught
		for (uint8_t i = 0; i < a.length; ++i) {
			auto bad = img; bad[a.offset + i] ^= 0x01;
			assert(!matches(bad.data(), bad.size()));
		}
	}
	// A Lilu/WEX route on either entry point replaces the prologue with a jump.
	auto routed = img; routed[PerformFlipOffset] = 0xE9;
	assert(!matches(routed.data(), routed.size()));
	routed = img; routed[ConfigurePlaneOffset] = 0xE9;
	assert(!matches(routed.data(), routed.size()));
	routed = img; routed[SyncPlaneUpdatesOffset] = 0xE9;
	assert(!matches(routed.data(), routed.size()));
	// Every assumption the wrappers rely on is anchored: both entry points, the
	// args sync test, both CTL immediates, the CTL cache store, args = params+8,
	// both reads of params+0x34, the single-plane check and the call edge.
	const uint32_t required[] {0x2198, 0x2211, 0x2229, 0x2252, 0x239E, 0x2B602, 0x2B620, 0x2B62F, 0x2B79B, 0x2B7F4, 0x2BE4E,
		0x33AE, 0x3406, 0x3513, 0x3525, 0x3564, 0x3576, 0x2C0C1, 0x2C1C5, 0x92549, 0x92555};
	assert(sizeof(Anchors) / sizeof(Anchors[0]) == sizeof(required) / sizeof(required[0]));
	for (uint32_t off : required) {
		bool found = false;
		for (const auto &a : Anchors) found |= a.offset == off;
		assert(found);
	}
}

static void transaction() {
	alignas(8) uint8_t params[0x60] {};
	uint32_t f = 0x40000018; memcpy(params + ParamsFlags, &f, 4);      // async, other bits set
	assert(requestSync(params));
	memcpy(&f, params + ParamsFlags, 4); assert(f == 0x40000019);
	// args[0] = params + 8, and configurePlane tests args+0x2C: the same word.
	uint32_t viaArgs; memcpy(&viaArgs, params + 8 + 0x2C, 4); assert(viaArgs & 1);
	// Native code may change other bits during the call; only bit0 is undone.
	f |= 0x00000200; memcpy(params + ParamsFlags, &f, 4);
	releaseSync(params);
	memcpy(&f, params + ParamsFlags, 4); assert(f == 0x40000218);
	// A caller that already asked for sync is not touched and not restored.
	f = 0x1; memcpy(params + ParamsFlags, &f, 4);
	assert(!requestSync(params));
	memcpy(&f, params + ParamsFlags, 4); assert(f == 0x1);
	assert(!requestSync(nullptr));
	releaseSync(nullptr);
}

static void planeCtl() {
	alignas(8) uint8_t plane[0x200] {};
	const uint32_t observed[] {0x82000600, 0x82000608};                 // UU capture values
	for (uint32_t v : observed) {
		memcpy(plane + PlaneCtlCache, &v, 4);
		assert(clearPlaneAsync(plane));
		uint32_t out; memcpy(&out, plane + PlaneCtlCache, 4);
		assert(out == (v & ~AsyncFlip) && (out & 0x8) == (v & 0x8));    // bit3 (DB-update-disable allow) kept
	}
	uint32_t sync = 0x82000408; memcpy(plane + PlaneCtlCache, &sync, 4);
	assert(!clearPlaneAsync(plane));
	uint32_t out; memcpy(&out, plane + PlaneCtlCache, 4); assert(out == sync);
	assert(!clearPlaneAsync(nullptr));
}

// Model the donor's final commit: clear its bit3, copy CTL by value, then
// commit SURF. Deliberately do not rerun configurePlane between equal args.
// The old persistent bit9 patch fails the 2->0 assertion below.
static void commitTransitions() {
	alignas(8) uint8_t plane[0x200] {};
	auto &ctl = *reinterpret_cast<uint32_t *>(plane + PlaneCtlCache);
	auto &reg = *reinterpret_cast<uint32_t *>(plane + PlaneCtlRegister);
	reg = 0x70180;
	ctl = 0x82000608;
	const auto commit = [&](uint32_t mode, bool begin = false) {
		uint32_t packet;
		{
			PlaneCommitScope scope(plane, mode == PlaneCtl && !begin);
			ctl &= ~8U; // the native method's bit3 edit must survive restoration
			packet = ctl;
		}
		return packet;
	};
	assert(!(commit(PlaneCtl) & AsyncFlip));
	assert(ctl == 0x82000600); // cache restored; native bit3 edit preserved
	assert(commit(Off) & AsyncFlip); // 2->0, with unchanged configurePlane args
	assert(!(commit(PlaneCtl) & AsyncFlip));
	// 2->1: native transaction sync recalculates CTL once; scope must not
	// resurrect an async bit that the native sync path deliberately removed.
	ctl = 0x82000408;
	assert(!(commit(Transaction) & AsyncFlip));
	assert(ctl == 0x82000400);
	ctl = 0x82000608;
	assert(commit(PlaneCtl, true) & AsyncFlip); // initial DB-disable call untouched
	// Cursor/sprite/pipe-D controls are outside this candidate's scope.
	for (uint32_t other : {0x70080U, 0x70280U, 0x73180U}) {
		reg = other;
		assert(commit(PlaneCtl) & AsyncFlip);
	}
	for (uint32_t primary : {0x70180U, 0x71180U, 0x72180U}) {
		reg = primary;
		assert(!(commit(PlaneCtl) & AsyncFlip));
		assert(ctl & AsyncFlip);
	}
	PlaneCommitScope nullScope(nullptr, true);
	assert(!nullScope.changed());
}

static void modeBoundary() {
	ModeGate gate;
	assert(gate.mode() == Off && gate.set(PlaneCtl));
	assert(gate.enter() == PlaneCtl); // outer PerformFlipTransaction
	assert(!gate.set(Off));
	assert(gate.enter() == PlaneCtl); // nested syncPlaneUpdates
	assert(!gate.set(Transaction));
	gate.leave();
	assert(!gate.set(Off));
	gate.leave();
	assert(gate.set(Off) && gate.mode() == Off);
	assert(!gate.set(ModeCount));
}

static void concurrentModeBoundary() {
	ModeGate gate;
	std::atomic<bool> start {false};
	auto reader = [&] {
		while (!start.load()) std::this_thread::yield();
		for (unsigned i = 0; i < 10000; ++i) {
			const uint32_t mode = gate.enter();
			assert(gate.mode() == mode);
			assert(gate.enter() == mode);
			assert(!gate.set((mode + 1) % ModeCount));
			gate.leave();
			assert(gate.mode() == mode);
			gate.leave();
		}
	};
	std::thread a(reader), b(reader);
	start.store(true);
	for (unsigned i = 0; i < 10000; ++i) gate.set(i % ModeCount);
	a.join(); b.join();
	assert(gate.set(Off) && gate.mode() == Off);
}

static void routeAdmission() {
	alignas(8) uint8_t params[0x60] {};
	alignas(8) uint8_t plane[0x160] {};
	*reinterpret_cast<uint32_t *>(plane + PlaneCtlRegister) = 0x70180;
	auto &ctl = *reinterpret_cast<uint32_t *>(plane + PlaneCtlCache);
	ctl = 0x82000600;
	for (uint32_t requested = 0; requested < ModeCount; ++requested) {
		// The first route can succeed and the second fail. A boot request for
		// either experimental mode must not mutate the surviving wrapper's data.
		const uint32_t mode = activeMode(requested, false);
		assert(mode == Off);
		if (mode == Transaction) requestSync(params);
		{ PlaneCommitScope scope(plane, mode == PlaneCtl); assert(!scope.changed()); }
		assert(*reinterpret_cast<uint32_t *>(params + ParamsFlags) == 0);
		assert(ctl == 0x82000600);
		assert(activeMode(requested, true) == requested);
	}
	assert(activeMode(ModeCount, true) == Off);
}

static void buckets() {
	assert(bucketOf(0) == 0 && bucketOf(999999) == 0 && bucketOf(1000000) == 1);
	assert(bucketOf(7999999) == 2 && bucketOf(8000000) == 3 && bucketOf(15999999) == 4);
	assert(bucketOf(16000000) == 5 && bucketOf(UINT64_MAX) == 5);
	assert(validMode(0) && validMode(2) && !validMode(3));
	static_assert(sizeof(Stats) % 8 == 0, "kext snapshots Stats as 64-bit words");
	static_assert(offsetof(Stats, flips) == 16, "32-bit header must stay 16 bytes");
}

static int framebuffer(const char *path) {
	FILE *f = fopen(path, "rb");
	if (!f) { perror(path); return 1; }
	std::vector<uint8_t> img;
	uint8_t buf[65536]; size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0) img.insert(img.end(), buf, buf + n);
	fclose(f);
	if (!matches(img.data(), img.size())) { fprintf(stderr, "anchors do NOT match %s\n", path); return 1; }
	printf("anchors match %s (%zu bytes)\n", path, img.size());
	return 0;
}

int main(int argc, char **argv) {
	anchors(); transaction(); planeCtl(); buckets();
	commitTransitions(); modeBoundary(); concurrentModeBoundary(); routeAdmission();
	puts("TGLSyncFlip logic: 8 cases passed (CPU only, including cached-args transitions, concurrent switching and partial-route admission).");
	if (argc == 3 && !strcmp(argv[1], "--framebuffer")) return framebuffer(argv[2]);
	return 0;
}
