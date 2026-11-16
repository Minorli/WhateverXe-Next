// SPDX-License-Identifier: BSD-3-Clause
// tglsync-ctl: read or change the TGLSyncFlip mode.
//   tglsync-ctl status           print counters (no change)
//   sudo tglsync-ctl mode N      N = 0 off, 1 transaction sync, 2 plane CTL sync
// Each line starts with the uptime in ns (same clock as the kernel's
// lastModeChangeNs and tgl-plane-trace marks) so blocks can be aligned later.
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../TGLSyncFlipLogic.hpp"

static const char *modeName(uint32_t m) {
	static const char *names[] = {"off", "transaction", "plane-ctl"};
	return m < 3 ? names[m] : "?";
}
static const char *stateName(uint32_t s) {
	static const char *names[] = {"not-seen", "routed", "symbol-missing", "shape-mismatch", "route-failed", "disarmed"};
	return s < 6 ? names[s] : "?";
}

static int printStatus(io_service_t svc) {
	CFTypeRef v = IORegistryEntryCreateCFProperty(svc, CFSTR("TGLSyncFlipStats"), kCFAllocatorDefault, 0);
	if (!v || CFGetTypeID(v) != CFDataGetTypeID() || CFDataGetLength((CFDataRef)v) != (CFIndex)sizeof(struct TGLSync::Stats)) {
		fprintf(stderr, "TGLSyncFlipStats missing or wrong size\n");
		if (v) CFRelease(v);
		return 3;
	}
	struct TGLSync::Stats s;
	memcpy(&s, CFDataGetBytePtr((CFDataRef)v), sizeof(s));
	CFRelease(v);
	if (s.version != TGLSync::Version) { fprintf(stderr, "schema %u, expected %u\n", s.version, TGLSync::Version); return 3; }
	printf("%" PRIu64 " state=%s armed=%u mode=%u(%s) mode_changes=%" PRIu64 " last_change_ns=%" PRIu64 "\n",
	       clock_gettime_nsec_np(CLOCK_UPTIME_RAW), stateName(s.state), s.armed, s.mode, modeName(s.mode),
	       s.modeChanges, s.lastModeChangeNs);
	printf("forced=%" PRIu64 " already_sync=%" PRIu64 " null_params=%" PRIu64 " plane_calls=%" PRIu64 " plane_cleared=%" PRIu64 "\n",
	       s.forced, s.alreadySync, s.nullParams, s.planeCalls, s.planeCleared);
	for (unsigned m = 0; m < TGLSync::ModeCount; ++m) {
		printf("mode%u flips=%" PRIu64 " max_ms=%.3f hist_ms[<1,<4,<8,<12,<16,>=16]=", m, s.flips[m], s.maxNs[m] / 1e6);
		for (unsigned b = 0; b < TGLSync::Buckets; ++b) printf("%s%" PRIu64, b ? "," : "", s.hist[m][b]);
		printf("\n");
	}
	return s.state == TGLSync::Routed ? 0 : 4;
}

static kern_return_t send(io_service_t svc, CFStringRef key, CFTypeRef value) {
	const void *k[] = {key}, *vals[] = {value};
	CFDictionaryRef d = CFDictionaryCreate(kCFAllocatorDefault, k, vals, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	kern_return_t kr = IORegistryEntrySetCFProperties(svc, d);
	CFRelease(d);
	return kr;
}

int main(int argc, char **argv) {
	io_service_t svc = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("TGLSyncFlip"));
	if (!svc) { fprintf(stderr, "TGLSyncFlip service not found (kext not loaded?)\n"); return 2; }
	int rc;
	if (argc == 2 && !strcmp(argv[1], "status")) {
		// Refresh needs root; without it the last published snapshot is shown.
		if (send(svc, CFSTR("Refresh"), kCFBooleanTrue) != KERN_SUCCESS) fprintf(stderr, "(not root: showing last snapshot)\n");
		rc = printStatus(svc);
	} else if (argc == 3 && !strcmp(argv[1], "mode")) {
		char *end = NULL;
		long m = strtol(argv[2], &end, 10);
		if (!end || *end || m < 0 || m >= TGLSync::ModeCount) { fprintf(stderr, "mode must be 0, 1 or 2\n"); IOObjectRelease(svc); return 64; }
		int32_t mm = (int32_t)m;
		CFNumberRef n = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &mm);
		kern_return_t kr = send(svc, CFSTR("Mode"), n);
		CFRelease(n);
		if (kr != KERN_SUCCESS) { fprintf(stderr, "set mode failed: 0x%x (need sudo; routes must be ready)\n", kr); IOObjectRelease(svc); return 5; }
		rc = printStatus(svc);
	} else {
		fprintf(stderr, "usage: %s status | mode 0|1|2\n", argv[0]);
		rc = 64;
	}
	IOObjectRelease(svc);
	return rc;
}
