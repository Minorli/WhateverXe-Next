// SPDX-License-Identifier: MIT
// Read-only plane-programming trace for the UU flicker. It asks the installed
// WhateverXe (0.8.0-based downstream or main) to publish its existing
// tglProgramRecord rings by opening and immediately closing one standard shared
// (type 1) framebuffer client, exactly like tgl-capture's display mode. No
// external method, no MMIO, no type-0 (WindowServer) client, no boot-arg.
//
// Press Enter whenever the flicker is seen; the mark is stored in the same
// mach-absolute nanosecond base as the kernel records.
//
// Output (O_EXCL, 0600) in an existing directory, intended for ignored local/:
//   summary.json  decoded statistics, no kernel pointers, no SURF addresses
//   records.csv   window records; thread replaced by a small index, access dropped
#import <Foundation/Foundation.h>
#import <IOKit/IOKitLib.h>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <mach/mach_time.h>
#include <poll.h>
#include <unistd.h>
#include "../src/TGLPlaneTrace.hpp"

static volatile sig_atomic_t stopped;
static void stop(int) { stopped = 1; }

static uint64_t nowNs() {
	static mach_timebase_info_data_t tb {};
	if (!tb.denom) mach_timebase_info(&tb);
	return mach_absolute_time() * tb.numer / tb.denom;
}
static id prop(io_registry_entry_t e, NSString *key) {
	return CFBridgingRelease(IORegistryEntryCreateCFProperty(e, (__bridge CFStringRef)key, kCFAllocatorDefault, 0));
}
static bool number(id v, uint64_t &out) {
	if (![v isKindOfClass:[NSNumber class]]) return false;
	out = [(NSNumber *)v unsignedLongLongValue]; return true;
}
static void records(id v, uint32_t group, std::vector<TGLPlane::Record> &out) {
	if (![v isKindOfClass:[NSData class]]) return;
	NSData *d = v; const size_t n = d.length / (TGLPlane::kRecordWords * 8);
	const auto *q = static_cast<const uint64_t *>(d.bytes);
	for (size_t i = 0; i < n; ++i) out.push_back(TGLPlane::fromWords(q + i * TGLPlane::kRecordWords, group));
}
static kern_return_t publish() {
	io_service_t fb = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("AppleIntelFramebuffer"));
	if (!fb) return kIOReturnNotFound;
	io_connect_t c = IO_OBJECT_NULL;
	const kern_return_t kr = IOServiceOpen(fb, mach_task_self(), 1, &c);
	IOObjectRelease(fb);
	if (kr == KERN_SUCCESS) IOServiceClose(c);
	return kr;
}
struct Extras {
	bool ackOK = false; uint64_t ack[8] {}; uint64_t errCount = 0; bool errOK = false;
	bool probeOK = false; uint64_t donorEntry = 0, donorPipeA = 0;
};
static Extras extras(io_registry_entry_t root) {
	Extras x;
	id ack = prop(root, @"TGLStreamAckCounts");
	if ([ack isKindOfClass:[NSData class]] && [(NSData *)ack length] >= 32) {
		const auto *w = static_cast<const uint32_t *>([(NSData *)ack bytes]);
		for (int i = 0; i < 8; ++i) x.ack[i] = w[i];
		x.ackOK = true;
	}
	x.errOK = number(prop(root, @"TGLStreamErrorCount"), x.errCount);
	io_service_t p = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("TGLDisplayProbe"));
	if (p) {
		id d = prop(p, @"TGLDisplayProbe");
		if ([d isKindOfClass:[NSDictionary class]])
			x.probeOK = number(d[@"DonorUnderrunEntry"], x.donorEntry) && number(d[@"DonorUnderrunPipeA"], x.donorPipeA);
		IOObjectRelease(p);
	}
	return x;
}
static void gammaEvents(io_registry_entry_t root, std::map<uint64_t, uint64_t> &seen) {
	for (int i = 0; i < 16; ++i) {
		id v = prop(root, [NSString stringWithFormat:@"TGLGamma10Last%d", i]);
		if (![v isKindOfClass:[NSData class]] || [(NSData *)v length] < 24) continue;
		const auto *q = static_cast<const uint64_t *>([(NSData *)v bytes]);
		if (q[2]) seen.emplace(q[2], q[0]);   // seq -> start ns
	}
}
static int openOut(NSString *dir, const char *name) {
	NSString *path = [dir stringByAppendingPathComponent:@(name)];
	return open(path.fileSystemRepresentation, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
}
static void writeAll(int fd, const std::string &s) {
	size_t off = 0; while (off < s.size()) { ssize_t n = write(fd, s.data() + off, s.size() - off); if (n <= 0) break; off += size_t(n); }
}

int main(int argc, char **argv) {
	@autoreleasepool {
		int seconds = 120, intervalMs = 250; NSString *dir = nil; bool marks = true; TGLPlane::Params params;
		for (int i = 1; i < argc; ++i) {
			if (!strcmp(argv[i], "-d") && i + 1 < argc) seconds = atoi(argv[++i]);
			else if (!strcmp(argv[i], "-i") && i + 1 < argc) intervalMs = atoi(argv[++i]);
			else if (!strcmp(argv[i], "-o") && i + 1 < argc) dir = @(argv[++i]);
			else if (!strcmp(argv[i], "--no-marks")) marks = false;
			else if (!strcmp(argv[i], "--before-ms") && i + 1 < argc) params.markBeforeNs = uint64_t(strtoull(argv[++i], nullptr, 10)) * 1000000ULL;
			else if (!strcmp(argv[i], "--after-ms") && i + 1 < argc) params.markAfterNs = uint64_t(strtoull(argv[++i], nullptr, 10)) * 1000000ULL;
			else if (!strcmp(argv[i], "--arm-us") && i + 1 < argc) params.armWindowNs = uint64_t(strtoull(argv[++i], nullptr, 10)) * 1000ULL;
			else { fprintf(stderr, "usage: %s -o existing-local-dir [-d seconds<=1800] [-i ms 100..2000] [--no-marks] [--before-ms 2000] [--after-ms 500] [--arm-us 1000]\n", argv[0]); return 64; }
		}
		BOOL isDir = NO;
		if (!dir || ![[NSFileManager defaultManager] fileExistsAtPath:dir isDirectory:&isDir] || !isDir) { fprintf(stderr, "-o must name an existing directory\n"); return 64; }
		if (params.markBeforeNs > 60000000000ULL || params.markAfterNs > 10000000000ULL || !params.armWindowNs || params.armWindowNs > 16000000ULL) {
			fprintf(stderr, "mark/arm window out of range\n"); return 64;
		}
		if (seconds < 1 || seconds > 1800 || intervalMs < 100 || intervalMs > 2000) { fprintf(stderr, "duration/interval out of range\n"); return 64; }
		const int sfd = openOut(dir, "summary.json"), cfd = openOut(dir, "records.csv");
		if (sfd < 0 || cfd < 0) { fprintf(stderr, "refusing to overwrite summary.json/records.csv\n"); return 73; }
		signal(SIGINT, stop); signal(SIGTERM, stop);

		io_registry_entry_t root = IORegistryGetRootEntry(kIOMainPortDefault);
		id enabled = prop(root, @"TGLProgramEnabled");
		TGLPlane::Trace trace; std::map<uint64_t, uint64_t> gamma;
		uint64_t publishFail = 0, samples = 0, lastKR = 0;
		const Extras x0 = extras(root);
		const uint64_t start = nowNs(), end = start + uint64_t(seconds) * 1000000000ULL;
		if (marks) fprintf(stderr, "tgl-plane-trace: %d s, every %d ms. Press Enter at each flicker; Ctrl-C ends early.\n", seconds, intervalMs);
		std::string pending;
		while (!stopped && nowNs() < end) {
			@autoreleasepool {
				const kern_return_t kr = publish();
				if (kr != KERN_SUCCESS) { ++publishFail; lastKR = uint64_t(uint32_t(kr)); }
				TGLPlane::Sample s; s.ns = nowNs();
				std::vector<TGLPlane::Record> recs;
				for (uint32_t g = 0; g < 2; ++g) {
					s.counted[g] = number(prop(root, [NSString stringWithFormat:@"TGLProgram%uCount", g]), s.count[g]);
					records(prop(root, [NSString stringWithFormat:@"TGLProgram%uFirst", g]), g, recs);
					records(prop(root, [NSString stringWithFormat:@"TGLProgram%uLast", g]), g, recs);
				}
				trace.ingest(s, recs); ++samples;
				gammaEvents(root, gamma);
			}
			// Wait for the next tick while collecting Enter marks.
			const uint64_t until = nowNs() + uint64_t(intervalMs) * 1000000ULL;
			while (!stopped) {
				const uint64_t t = nowNs(); if (t >= until) break;
				struct pollfd pfd { STDIN_FILENO, POLLIN, 0 };
				const int ms = int((until - t) / 1000000ULL) + 1;
				if (marks && poll(&pfd, 1, ms) > 0 && (pfd.revents & POLLIN)) {
					char buf[256]; const ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
					if (n <= 0) { marks = false; continue; }
					for (ssize_t k = 0; k < n; ++k) if (buf[k] == '\n') { trace.mark(nowNs()); fprintf(stderr, "mark %zu\n", trace.marks().size()); }
				} else if (!marks) usleep(useconds_t(ms) * 1000);
			}
		}
		const Extras x1 = extras(root);
		IOObjectRelease(root);

		const auto rep = TGLPlane::analyze(trace, params);
		std::string j = TGLPlane::toJSON(rep, params);
		j.pop_back(); // reopen the object for collector fields
		char buf[256];
		j += ",\"collector\":{\"samples\":" + std::to_string(samples) + ",\"interval_ms\":" + std::to_string(intervalMs) +
			",\"publish_failures\":" + std::to_string(publishFail) + ",\"last_publish_error\":" + std::to_string(lastKR) +
			",\"program_enabled\":" + ([enabled isKindOfClass:[NSNumber class]] ? ([(NSNumber *)enabled boolValue] ? "true" : "false") : "null") +
			",\"start_ns\":" + std::to_string(start) + ",\"mark_before_ns\":" + std::to_string(params.markBeforeNs) +
			",\"mark_after_ns\":" + std::to_string(params.markAfterNs) + "}";
		uint64_t gammaInWindow = 0; std::string gammaNs;
		for (auto &kv : gamma) if (kv.second >= trace.windowStartNs() && kv.second <= trace.windowEndNs()) {
			if (gammaInWindow++ < 64) { if (!gammaNs.empty()) gammaNs += ","; gammaNs += std::to_string(kv.second); }
		}
		j += ",\"gamma10\":{\"uploads_seen_in_window\":" + std::to_string(gammaInWindow) +
			",\"max_seq\":" + (gamma.empty() ? std::string("null") : std::to_string(gamma.rbegin()->first)) + ",\"ns\":[" + gammaNs + "]}";
		if (x0.ackOK && x1.ackOK) {
			j += ",\"iir_ack_delta\":[";
			for (int i = 0; i < 8; ++i) { snprintf(buf, sizeof(buf), "%s%llu", i ? "," : "", (unsigned long long)(x1.ack[i] - x0.ack[i])); j += buf; }
			j += "]";
		} else j += ",\"iir_ack_delta\":null";
		j += ",\"bit31_ack_delta\":" + (x0.errOK && x1.errOK ? std::to_string(x1.errCount - x0.errCount) : std::string("null"));
		j += ",\"donor_underrun\":" + (x0.probeOK && x1.probeOK ?
			"{\"entry_start\":" + std::to_string(x0.donorEntry) + ",\"entry_end\":" + std::to_string(x1.donorEntry) +
			",\"pipe_a_start\":" + std::to_string(x0.donorPipeA) + ",\"pipe_a_end\":" + std::to_string(x1.donorPipeA) + "}" : std::string("null"));
		j += "}\n";
		writeAll(sfd, j); close(sfd);

		std::map<uint64_t, unsigned> threads; std::string csv = "ns,seq,group,reg,value,fast,caller,thread\n";
		for (const auto &r : trace.ordered()) {
			if (r.ns < trace.windowStartNs() || r.ns > trace.windowEndNs()) continue;
			const auto t = threads.emplace(r.thread, unsigned(threads.size())).first->second;
			snprintf(buf, sizeof(buf), "%llu,%llu,%u,0x%llx,0x%08llx,%llu,0x%llx,%u\n", (unsigned long long)r.ns, (unsigned long long)r.seq,
				r.group, (unsigned long long)r.reg, (unsigned long long)r.value, (unsigned long long)r.fast, (unsigned long long)r.caller, t);
			csv += buf;
		}
		writeAll(cfd, csv); close(cfd);
		fprintf(stderr, "samples=%llu records_in_window=%llu lost_plane=%llu marks=%zu publish_failures=%llu\n",
			(unsigned long long)samples, (unsigned long long)rep.recordsInWindow, (unsigned long long)rep.lost[1], trace.marks().size(),
			(unsigned long long)publishFail);
		if (!samples) return 2;
		return trace.counted(1) ? 0 : 3; // 3: recorder not published (absent, disabled or -wex-tgl-prog-off)
	}
}
