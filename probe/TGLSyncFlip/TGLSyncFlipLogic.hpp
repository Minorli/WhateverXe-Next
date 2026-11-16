// SPDX-License-Identifier: BSD-3-Clause
// Pure logic for the TGLSyncFlip A/B plugin. No kernel or IOKit dependency, so
// tests/logic_test.cpp exercises exactly the code the kext runs.
//
// Static basis: AppleIntelTGLGraphicsFramebuffer 115a4616...888e (Tahoe 26.7.3), file offsets.
//   PerformFlipTransaction 0x2B602
//     0x2B620  leaq 0x8(%rcx),%rdx     FlipTransactionArgs[0] = params + 8
//     0x2B62F  movl 0x34(%rcx),%ebx    request flags = params + 0x34 (== args[0] + 0x2C)
//     0x2B79B  movl 0x34(%rcx),%r15d   the same flags, kept as the "synchronous" local
//     0x2B7F4  cmpb $2,(%rcx)          more than one plane is rejected, so only args[0] exists
//     0x2BE4E  call configurePlane
//     bit0 clear (async request): no pre-vblank wait, then after armUpdate a
//       1e6/(refresh*50) x IODelay(50) poll for the pipe flip-done flag that
//       processDEInterrupt (0x73542) sets from pipe IIR bit3; then completes.
//   configurePlane 0x2198
//     0x2211  testb %al,0x2C(%r15)     args sync bit
//     0x2229  movl $0x80000600,%eax    default CTL: enable + bit9 async address update
//     0x2252  movl $0x80000400,%eax    synchronous CTL (bit10 here is X tiling, rewritten at 0x2336)
//     0x239E  movl %r13d,0x100(%rbx)   plane CTL cache; updatePlane/syncPlaneUpdates write it
//     0x21CF  memcmp(args+8, plane+0x18, 0x50) -> a changed +0x2C forces a full plane setup
#ifndef TGLSyncFlipLogic_hpp
#define TGLSyncFlipLogic_hpp
#include <stddef.h>
#include <stdint.h>

namespace TGLSync {
constexpr uint32_t Version = 1;
constexpr const char *PerformFlipSymbol =
	"__ZN21AppleIntelFramebuffer22PerformFlipTransactionEP30IOAccelDisplayPipeTransaction2yP21FlipTransactionParams";
constexpr const char *ConfigurePlaneSymbol = "__ZN15AppleIntelPlane14configurePlaneEP19FlipTransactionArgs";
constexpr const char *SyncPlaneUpdatesSymbol = "__ZN15AppleIntelPlane16syncPlaneUpdatesEb";
constexpr uint32_t PerformFlipOffset = 0x2B602;
constexpr uint32_t ConfigurePlaneOffset = 0x2198;
constexpr uint32_t SyncPlaneUpdatesOffset = 0x33AE;

constexpr size_t ParamsFlags = 0x34;
constexpr uint32_t SyncRequest = 1U << 0;
constexpr size_t PlaneCtlCache = 0x100;
constexpr size_t PlaneCtlRegister = 0x98;
constexpr uint32_t AsyncFlip = 1U << 9;

// Off: pure pass-through (only timing is recorded).
// Transaction: the flip is presented to the native code as a synchronous request,
//   so CTL, the pre-vblank wait, completion and the args cache all take the
//   native sync path that idle and Google Remote Desktop already use.
// PlaneCtl: the native request stays async (including its bounded flip-done
//   poll). Only the CTL value consumed by syncPlaneUpdates(false) loses bit9;
//   the cache is restored before returning. Poll timeout is NOT proof of latch.
enum Mode : uint32_t { Off = 0, Transaction = 1, PlaneCtl = 2, ModeCount = 3 };
inline bool validMode(uint32_t m) { return m < ModeCount; }
// A partial route installation must remain pass-through even if a nonzero
// boot mode was requested. Only the complete two-route set activates changes.
inline uint32_t activeMode(uint32_t m, bool routesReady) {
	return routesReady && validMode(m) ? m : Off;
}

// Pin the selected mode across a native flip and its nested plane callback.
// Control requests may switch only at a quiescent boundary; they never wait
// inside the kernel or change an in-flight transaction's mode.
class ModeGate {

	uint32_t state {Off}; // low two bits: mode; remaining bits: active calls
public:
	uint32_t enter() { return __atomic_fetch_add(&state, 4U, __ATOMIC_ACQUIRE) & 3U; }
	void leave() { __atomic_fetch_sub(&state, 4U, __ATOMIC_RELEASE); }
	uint32_t mode() const { return __atomic_load_n(&state, __ATOMIC_ACQUIRE) & 3U; }
	bool set(uint32_t next) {
		if (!validMode(next)) return false;
		uint32_t expected = __atomic_load_n(&state, __ATOMIC_ACQUIRE);
		if (expected & ~3U) return false;
		return __atomic_compare_exchange_n(&state, &expected, next, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
	}
};

struct Anchor { uint32_t offset; uint8_t length; uint8_t bytes[17]; };
constexpr Anchor Anchors[] {
	{0x2198, 17, {0x55,0x48,0x89,0xE5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xEC,0x28}},
	{0x2211, 4, {0x41,0x84,0x47,0x2C}},
	{0x2229, 5, {0xB8,0x00,0x06,0x00,0x80}},
	{0x2252, 5, {0xB8,0x00,0x04,0x00,0x80}},
	{0x239E, 7, {0x44,0x89,0xAB,0x00,0x01,0x00,0x00}},
	{0x2B602, 17, {0x55,0x48,0x89,0xE5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xEC,0x48}},
	{0x2B620, 4, {0x48,0x8D,0x51,0x08}},
	{0x2B62F, 3, {0x8B,0x59,0x34}},
	{0x2B79B, 4, {0x44,0x8B,0x79,0x34}},
	{0x2B7F4, 3, {0x80,0x39,0x02}},
	{0x2BE4E, 5, {0xE8,0x45,0x63,0xFD,0xFF}},
	// Additional guards for the temporary commit scope. Keep all old anchors.
	{0x33AE, 17, {0x55,0x48,0x89,0xE5,0x41,0x57,0x41,0x56,0x53,0x50,0x41,0x89,0xF7,0x48,0x89,0xFB,0x48}},
	{0x3406, 6, {0x8B,0x83,0x00,0x01,0x00,0x00}},
	{0x3513, 16, {0x48,0x8B,0x7B,0x70,0x8B,0xB3,0x98,0x00,0x00,0x00,0x8B,0x93,0x00,0x01,0x00,0x00}},
	{0x3525, 5, {0xE8,0xD2,0xEF,0x08,0x00}},
	{0x3564, 16, {0x48,0x8B,0x7B,0x70,0x8B,0xB3,0xB8,0x00,0x00,0x00,0x8B,0x93,0x20,0x01,0x00,0x00}},
	{0x3576, 5, {0xE8,0x81,0xEF,0x08,0x00}},
	{0x2C0C1, 5, {0xE8,0xE8,0x72,0xFD,0xFF}},
	{0x2C1C5, 5, {0xE8,0xA6,0xCA,0x00,0x00}},
	{0x92549, 16, {0x4C,0x8D,0x04,0x40,0x42,0x89,0xB4,0x87,0xF0,0x32,0x00,0x00,0x42,0x89,0x94,0x87}},
	{0x92555, 16, {0x42,0x89,0x94,0x87,0xF4,0x32,0x00,0x00,0x42,0x89,0x8C,0x87,0xF8,0x32,0x00,0x00}},
};

// Every anchor must lie inside the image and match byte for byte.
inline bool matches(const uint8_t *image, size_t size) {
	if (!image) return false;
	for (const auto &a : Anchors) {
		if (a.offset > size || a.length > size - a.offset) return false;
		for (uint8_t i = 0; i < a.length; ++i)
			if (image[a.offset + i] != a.bytes[i]) return false;
	}
	return true;
}

// Sets the sync request bit. Returns true only when this call changed it, so the
// caller restores exactly what it changed and no other flag bit.
inline bool requestSync(uint8_t *params) {
	if (!params) return false;
	auto flags = reinterpret_cast<uint32_t *>(params + ParamsFlags);
	if (*flags & SyncRequest) return false;
	*flags |= SyncRequest;
	return true;
}
inline void releaseSync(uint8_t *params) {
	if (params) *reinterpret_cast<uint32_t *>(params + ParamsFlags) &= ~SyncRequest;
}

// Clears bit9 in the plane's cached CTL. Returns true when it was set.
inline bool clearPlaneAsync(uint8_t *plane) {
	if (!plane) return false;
	auto ctl = reinterpret_cast<uint32_t *>(plane + PlaneCtlCache);
	if (!(*ctl & AsyncFlip)) return false;
	*ctl &= ~AsyncFlip;
	return true;
}

// syncPlaneUpdates(false) consumes the CTL by value at 0x351D/0x3525,
// before the SURF packet at 0x3576. Its native bit3 edits must survive.
// Restoring only bit9 leaves configurePlane's unchanged-args fast path valid.
class PlaneCommitScope {
	uint8_t *plane {nullptr};
public:
	PlaneCommitScope(uint8_t *p, bool enabled) {
		if (!p || !enabled) return;
		const uint32_t reg = *reinterpret_cast<const uint32_t *>(p + PlaneCtlRegister);
		if (reg != 0x70180 && reg != 0x71180 && reg != 0x72180) return;
		if (clearPlaneAsync(p)) plane = p;
	}
	~PlaneCommitScope() {
		if (plane) *reinterpret_cast<uint32_t *>(plane + PlaneCtlCache) |= AsyncFlip;
	}
	bool changed() const { return plane != nullptr; }
	PlaneCommitScope(const PlaneCommitScope &) = delete;
	PlaneCommitScope &operator=(const PlaneCommitScope &) = delete;
};

// PerformFlipTransaction duration buckets: <1, <4, <8, <12, <16, >=16 ms.
constexpr unsigned Buckets = 6;
inline unsigned bucketOf(uint64_t ns) {
	constexpr uint64_t edges[Buckets - 1] {1000000, 4000000, 8000000, 12000000, 16000000};
	unsigned i = 0;
	while (i < Buckets - 1 && ns >= edges[i]) ++i;
	return i;
}

// Plain 64-bit counters. The kext updates them with atomics; layout is the
// published binary property, so the field order is part of schema Version.
struct Stats {
	uint32_t version, mode, armed, state;
	uint64_t flips[ModeCount];
	uint64_t maxNs[ModeCount];
	uint64_t hist[ModeCount][Buckets];
	uint64_t forced, alreadySync, nullParams;
	uint64_t planeCalls, planeCleared;
	uint64_t modeChanges, lastModeChangeNs;
};

enum State : uint32_t { NotSeen = 0, Routed = 1, SymbolMissing = 2, ShapeMismatch = 3, RouteFailed = 4, Disarmed = 5 };
}
#endif
