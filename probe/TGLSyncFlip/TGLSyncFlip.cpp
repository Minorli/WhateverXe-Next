// SPDX-License-Identifier: BSD-3-Clause
// TGLSyncFlip: standalone Lilu plugin for a same-boot A/B of async primary-plane
// flips on the Tiger Lake framebuffer. It leaves the running WhateverXe binary
// untouched.
//
//  * Without -tglsync nothing is routed (control boot).
//  * With -tglsync, PerformFlipTransaction and syncPlaneUpdates are routed after a
//    symbol + offset + byte-anchor check against framebuffer 115a4616...888e.
//    The mode starts at tglsyncmode=N (default 0 = pass-through).
//  * An administrator can change the mode at runtime through setProperties
//    (tools/tglsync-ctl). Counters are published only when asked, never per flip.
// See TGLSyncFlipLogic.hpp for the static evidence behind each offset.
#include <Headers/plugin_start.hpp>
#include <Headers/kern_api.hpp>
#include <Headers/kern_util.hpp>
#include <Headers/kern_version.hpp>
#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IOLib.h>
#include <libkern/c++/OSBoolean.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSString.h>
#include <kern/clock.h>
#include "TGLSyncFlipLogic.hpp"

using namespace TGLSync;

static const char *pathFb[] {
	"/System/Library/Extensions/AppleIntelTGLGraphicsFramebuffer.kext/Contents/MacOS/AppleIntelTGLGraphicsFramebuffer"};
static KernelPatcher::KextInfo kextFb {"com.apple.driver.AppleIntelTGLGraphicsFramebuffer", pathFb, arrsize(pathFb), {}, {},
	KernelPatcher::KextInfo::Unloaded};

namespace {
Stats gStats {};
ModeGate gMode;
uint32_t gRoutesReady {};
mach_vm_address_t orgPerformFlip {}, orgSyncPlaneUpdates {};

uint64_t nowNs() {
	uint64_t t, ns;
	clock_get_uptime(&t);
	absolutetime_to_nanoseconds(t, &ns);
	return ns;
}
void add(uint64_t &c, uint64_t v = 1) { __atomic_fetch_add(&c, v, __ATOMIC_RELAXED); }
void raiseMax(uint64_t &m, uint64_t v) {
	uint64_t cur = __atomic_load_n(&m, __ATOMIC_RELAXED);
	while (v > cur && !__atomic_compare_exchange_n(&m, &cur, v, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}
}
bool setMode(uint32_t mode) {
	if (!gMode.set(mode)) return false;
	add(gStats.modeChanges);
	__atomic_store_n(&gStats.lastModeChangeNs, nowNs(), __ATOMIC_RELAXED);
	return true;
}

using PerformFlip = IOReturn (*)(void *, void *, unsigned long long, uint8_t *);
using SyncPlaneUpdates = uint64_t (*)(uint8_t *, bool);

IOReturn wrapPerformFlip(void *fb, void *tx, unsigned long long arg, uint8_t *params) {
	const uint32_t mode = activeMode(gMode.enter(), __atomic_load_n(&gRoutesReady, __ATOMIC_ACQUIRE));
	bool forced = false;
	if (mode == Transaction) {
		if (!params) add(gStats.nullParams);
		else if ((forced = requestSync(params))) add(gStats.forced);
		else add(gStats.alreadySync);
	}
	const uint64_t start = nowNs();
	const IOReturn result = reinterpret_cast<PerformFlip>(orgPerformFlip)(fb, tx, arg, params);
	const uint64_t elapsed = nowNs() - start;
	// Give the caller back exactly its own request, so switching to Off really
	// returns to native behaviour on the next transaction.
	if (forced) releaseSync(params);
	add(gStats.flips[mode]);
	add(gStats.hist[mode][bucketOf(elapsed)]);
	raiseMax(gStats.maxNs[mode], elapsed);
	gMode.leave();
	return result;
}

// Native queueing copies CTL, then SURF, into trigger packets. Restore the
// cache before the native caller resumes, even on its early-return branch.
// No updatePlane route: the current downstream WEX already owns that route.
uint64_t wrapSyncPlaneUpdates(uint8_t *plane, bool enable) {
	const uint32_t mode = activeMode(gMode.enter(), __atomic_load_n(&gRoutesReady, __ATOMIC_ACQUIRE));
	uint64_t result;
	{
		PlaneCommitScope scope(plane, !enable && mode == PlaneCtl);
		if (!enable && mode == PlaneCtl) add(gStats.planeCalls);
		if (scope.changed()) add(gStats.planeCleared);
		result = reinterpret_cast<SyncPlaneUpdates>(orgSyncPlaneUpdates)(plane, enable);
	}
	gMode.leave();
	return result;
}

void processFramebuffer(void *, KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
	if (index != kextFb.loadIndex || gStats.state != NotSeen) return;
	const auto pf = patcher.solveSymbol<mach_vm_address_t>(index, PerformFlipSymbol, address, size);
	patcher.clearError();
	const auto cp = patcher.solveSymbol<mach_vm_address_t>(index, ConfigurePlaneSymbol, address, size);
	patcher.clearError();
	const auto sp = patcher.solveSymbol<mach_vm_address_t>(index, SyncPlaneUpdatesSymbol, address, size);
	patcher.clearError();
	if (!pf || !cp || !sp) { gStats.state = SymbolMissing; return; }
	if (pf != address + PerformFlipOffset || cp != address + ConfigurePlaneOffset || sp != address + SyncPlaneUpdatesOffset ||
		!matches(reinterpret_cast<const uint8_t *>(address), size)) {
		gStats.state = ShapeMismatch;
		SYSLOG("tglsync", "framebuffer shape mismatch; not routing");
		return;
	}
	KernelPatcher::RouteRequest requests[] {
		{PerformFlipSymbol, wrapPerformFlip, orgPerformFlip},
		{SyncPlaneUpdatesSymbol, wrapSyncPlaneUpdates, orgSyncPlaneUpdates},
	};
	const bool ok = patcher.routeMultiple(index, requests, arrsize(requests), address, size);
	patcher.clearError();
	gStats.state = ok && orgPerformFlip && orgSyncPlaneUpdates ? Routed : RouteFailed;
	__atomic_store_n(&gRoutesReady, gStats.state == Routed, __ATOMIC_RELEASE);
	SYSLOG("tglsync", "routes %s, mode %u", gStats.state == Routed ? "ready" : "FAILED", gMode.mode());
}

void pluginStart() {
	gStats.version = Version;
	gStats.armed = checkKernelArgument("-tglsync");
	if (!gStats.armed) { gStats.state = Disarmed; return; }
	uint32_t mode = Off;
	if (lilu_get_boot_args("tglsyncmode", &mode, sizeof(mode)) && validMode(mode)) gMode.set(mode);
	lilu.onKextLoadForce(&kextFb, 1, processFramebuffer, nullptr);
}
}

static const char *bootargOff[] {"-tglsyncoff"};
static const char *bootargDebug[] {"-tglsyncdbg"};
static const char *bootargBeta[] {"-tglsyncbeta"};

PluginConfiguration ADDPR(config) {
	xStringify(PRODUCT_NAME), parseModuleVersion(xStringify(MODULE_VERSION)), LiluAPI::AllowNormal,
	bootargOff, arrsize(bootargOff), bootargDebug, arrsize(bootargDebug), bootargBeta, arrsize(bootargBeta),
	KernelVersion::Tahoe, KernelVersion::Tahoe, pluginStart};

// The IOService doubles as the control endpoint (LILU_CUSTOM_IOKIT_INIT).
class EXPORT PRODUCT_NAME : public IOService {
	OSDeclareDefaultStructors(PRODUCT_NAME)
	void publish();
public:
	IOService *probe(IOService *provider, SInt32 *score) override;
	bool start(IOService *provider) override;
	IOReturn setProperties(OSObject *properties) override;
};
OSDefineMetaClassAndStructors(PRODUCT_NAME, IOService)

void PRODUCT_NAME::publish() {
	Stats snapshot;
	for (size_t i = 0; i < sizeof(Stats) / sizeof(uint64_t); ++i)
		reinterpret_cast<uint64_t *>(&snapshot)[i] = __atomic_load_n(reinterpret_cast<uint64_t *>(&gStats) + i, __ATOMIC_RELAXED);
	snapshot.mode = gMode.mode();
	setProperty("TGLSyncFlipStats", &snapshot, sizeof(snapshot));
	setProperty("TGLSyncFlipMode", snapshot.mode, 32);
	setProperty("TGLSyncFlipRouted", snapshot.state == Routed);
}

IOService *PRODUCT_NAME::probe(IOService *provider, SInt32 *score) {
	setProperty("VersionInfo", kextVersion);
	auto service = IOService::probe(provider, score);
	return ADDPR(startSuccess) ? service : nullptr;
}

bool PRODUCT_NAME::start(IOService *provider) {
	if (!IOService::start(provider)) return false;
	publish();
	registerService();
	return ADDPR(startSuccess);
}

// Accepts {"Mode": n} (0..2) and {"Refresh": true}. Administrator only.
IOReturn PRODUCT_NAME::setProperties(OSObject *properties) {
	if (IOUserClient::clientHasPrivilege(current_task(), kIOClientPrivilegeAdministrator) != kIOReturnSuccess)
		return kIOReturnNotPrivileged;
	auto dict = OSDynamicCast(OSDictionary, properties);
	if (!dict) return kIOReturnBadArgument;
	if (auto mode = OSDynamicCast(OSNumber, dict->getObject("Mode"))) {
		if (gStats.state != Routed) return kIOReturnNotReady;
		if (!validMode(mode->unsigned32BitValue())) return kIOReturnBadArgument;
		if (!setMode(mode->unsigned32BitValue())) return kIOReturnBusy;
	} else if (!OSDynamicCast(OSBoolean, dict->getObject("Refresh"))) {
		return kIOReturnUnsupported;
	}
	publish();
	return kIOReturnSuccess;
}

extern "C" {
extern kern_return_t _start(kmod_info_t *, void *);
extern kern_return_t _stop(kmod_info_t *, void *);
extern kern_return_t TGLSyncFlip_kern_start(kmod_info_t *, void *);
extern kern_return_t TGLSyncFlip_kern_stop(kmod_info_t *, void *);
__attribute__((visibility("default"))) KMOD_EXPLICIT_DECL(org.whateverxe-next.TGLSyncFlip, "1.0.2", _start, _stop)
__attribute__((visibility("hidden"))) kmod_start_func_t *_realmain = TGLSyncFlip_kern_start;
__attribute__((visibility("hidden"))) kmod_stop_func_t *_antimain = TGLSyncFlip_kern_stop;
__attribute__((visibility("hidden"))) int _kext_apple_cc = __APPLE_CC__;
}
