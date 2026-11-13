// SPDX-License-Identifier: BSD-3-Clause
// Open and immediately close a type-1 (shared) user client on each
// AppleIntelFramebuffer. WhateverXe publishes its on-demand display records
// (TGLStreamErrorCount, TGLStreamAckCounts, TGLStreamErrorRing, ...) at that
// boundary. No external method is called. Then print those root properties.
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>

static void printKey(io_registry_entry_t root, const char *name) {
	CFStringRef key = CFStringCreateWithCString(NULL, name, kCFStringEncodingUTF8);
	CFTypeRef v = IORegistryEntryCreateCFProperty(root, key, NULL, 0);
	CFRelease(key);
	if (!v) { printf("%s = unavailable\n", name); return; }
	if (CFGetTypeID(v) == CFNumberGetTypeID()) {
		long long n = 0; CFNumberGetValue(v, kCFNumberLongLongType, &n);
		printf("%s = %lld\n", name, n);
	} else if (CFGetTypeID(v) == CFDataGetTypeID()) {
		const UInt8 *p = CFDataGetBytePtr(v); CFIndex len = CFDataGetLength(v);
		printf("%s = <%ld bytes>", name, (long)len);
		for (CFIndex i = 0; i + 4 <= len && i < 64; i += 4)
			printf(" %08x", (unsigned)(p[i] | p[i+1] << 8 | p[i+2] << 16 | (unsigned)p[i+3] << 24));
		printf("%s\n", len > 64 ? " ..." : "");
	} else {
		printf("%s = <type %lu>\n", name, (unsigned long)CFGetTypeID(v));
	}
	CFRelease(v);
}

int main(void) {
	io_iterator_t it = IO_OBJECT_NULL;
	if (IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching("AppleIntelFramebuffer"), &it) != KERN_SUCCESS) {
		fprintf(stderr, "no AppleIntelFramebuffer\n");
		return 1;
	}
	int opened = 0, seen = 0;
	for (io_service_t fb; (fb = IOIteratorNext(it)); IOObjectRelease(fb)) {
		++seen;
		io_connect_t c = IO_OBJECT_NULL;
		kern_return_t kr = IOServiceOpen(fb, mach_task_self(), 1, &c);
		printf("framebuffer %d: IOServiceOpen(type 1) = 0x%x\n", seen, kr);
		if (kr == KERN_SUCCESS) { ++opened; IOServiceClose(c); }
	}
	IOObjectRelease(it);
	io_registry_entry_t root = IORegistryGetRootEntry(kIOMainPortDefault);
	const char *keys[] = {"TGLStreamErrorCount", "TGLStreamAckCounts", "TGLStreamErrorRing",
		"TGLProgramEnabled", "TGLWEGRouteScanoutPower", "TGLCCSAlignmentReady", "TGLIOSurfaceCCSPolicyReady",
		"TGLNoPOSHReady", "TGLGpuRecReady"};
	for (unsigned i = 0; i < sizeof(keys) / sizeof(*keys); ++i) printKey(root, keys[i]);
	IOObjectRelease(root);
	printf("framebuffers=%d opened=%d\n", seen, opened);
	return opened ? 0 : 2;
}
