// SPDX-License-Identifier: BSD-3-Clause
// TGLDisplayProbe: standalone Tiger Lake display underrun probe.
//
// Coexists with any WhateverXe build: no hooks, no patching, no provider
// open(), own IOMatchCategory. Maps IGPU BAR0 and samples a fixed register
// allow list on its own work loop (see TGLDisplayProbeLogic.hpp contract).
//
// Boot arguments:
//   -tgldp-off         do not start
//   -tgldp-count       opt-in: W1C PIPESTATUS bit31 after recording it, so
//                      every new underrun is counted (default: no writes)
//   -tgldp-readonly    force no writes at all (overrides -tgldp-count, tgldp-wa)
//   tgldp-ms=<n>       sampling period in ms, clamped to [5, 1000], default 20
//   tgldp-wa=<mask>    opt-in workarounds (1 buddy timer, 2 buddy mask,
//                      4 DCPR_2, 8 DBUF tracker); default 0
//   tgldp-pagemask=<v> BW_BUDDY page mask, required for wa bit 2
//
// Results: IORegistry property "TGLDisplayProbe" on this service, generated
// on read by an OSSerializer (no periodic registry writes).
#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOMessage.h>
// From IOKit/pwr_mgt/RootDomain.h (kpi.iokit); that header needs private
// assertion types, so declare only the one function used here.
extern "C" IONotifier *registerPrioritySleepWakeInterest(IOServiceInterestHandler handler, void *self, void *ref);
#include <libkern/c++/OSSerialize.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSArray.h>
#include <pexpert/pexpert.h>
#include <kern/clock.h>
#include "TGLDisplayProbeLogic.hpp"

class TGLDisplayProbe : public IOService {
	OSDeclareDefaultStructors(TGLDisplayProbe)
	IOPCIDevice *pci {};
	IOMemoryMap *map {};
	volatile uint8_t *mmio {};
	uint64_t mmioLength {};
	IOWorkLoop *loop {};
	IOTimerEventSource *timer {};
	IOSimpleLock *lock {};
	uint32_t periodMs {20};
	TGLDP::Config cfg {};
	TGLDP::State st {};
	uint16_t device {};
	IONotifier *sleepNotifier {};
	volatile bool paused {};
	// Native framebuffer coverage counters (see TGLDP::Donor).
	IOService *donor {};            // retained while counters are read
	uint64_t donorAddr[5] {};
	uint64_t donorValue[5] {}, donorAtQualify[5] {};
	uint32_t donorState {};         // 0 searching, 1 qualified, 2 rejected, 3 not found (gave up)
	uint32_t donorAttempts {};
	uint64_t pciUnavailable {};

	struct IO {
		volatile uint8_t *base; uint64_t length;
		uint32_t read(uint32_t r) {
			if (!TGLDP::allowed(r) || r + 4 > length) return 0;
			return *reinterpret_cast<volatile uint32_t *>(base + r);
		}
		void write(uint32_t r, uint32_t v) {
			if (!TGLDP::allowed(r) || r + 4 > length) return;
			*reinterpret_cast<volatile uint32_t *>(base + r) = v;
			(void)*reinterpret_cast<volatile uint32_t *>(base + r); // post the write
		}
	};

	static void timerFired(OSObject *owner, IOTimerEventSource *sender);
	static IOReturn sleepWake(void *target, void *ref, UInt32 type, IOService *provider, void *arg, vm_size_t size);
	bool pciAccessible();
	void findDonor();
	static bool serialize(void *target, void *ref, OSSerialize *s);
	static bool supportedDevice(uint16_t id);
	void releaseResources();

public:
	bool start(IOService *provider) override;
	void stop(IOService *provider) override;
	void free() override;
};

OSDefineMetaClassAndStructors(TGLDisplayProbe, IOService)

bool TGLDisplayProbe::supportedDevice(uint16_t id) {
	// Tiger Lake UP3/UP4 GT2/GT1 (Xe-LP, display version 12).
	static const uint16_t ids[] {0x9A40, 0x9A49, 0x9A78, 0x9AC0, 0x9AC9, 0x9AD9, 0x9AF8};
	for (auto v : ids) if (v == id) return true;
	return false;
}

static uint64_t nowNs() {
	uint64_t t, ns;
	clock_get_uptime(&t);
	absolutetime_to_nanoseconds(t, &ns);
	return ns;
}

bool TGLDisplayProbe::start(IOService *provider) {
	char flag[8];
	if (PE_parse_boot_argn("-tgldp-off", flag, sizeof(flag))) return false;
	if (!IOService::start(provider)) return false;
	pci = OSDynamicCast(IOPCIDevice, provider);
	if (!pci) return false;
	const uint16_t vendor = pci->configRead16(kIOPCIConfigVendorID);
	device = pci->configRead16(kIOPCIConfigDeviceID);
	if (vendor != 0x8086 || !supportedDevice(device)) {
		IOLog("TGLDisplayProbe: unsupported %04x:%04x\n", vendor, device);
		return false;
	}
	cfg.count = PE_parse_boot_argn("-tgldp-count", flag, sizeof(flag));
	uint32_t v = 0;
	if (PE_parse_boot_argn("tgldp-ms", &v, sizeof(v))) periodMs = v < 5 ? 5 : v > 1000 ? 1000 : v;
	if (PE_parse_boot_argn("tgldp-wa", &v, sizeof(v))) cfg.wa = v & TGLDP::WAAll;
	if (PE_parse_boot_argn("tgldp-pagemask", &v, sizeof(v))) cfg.pageMask = v;
	if (PE_parse_boot_argn("-tgldp-readonly", flag, sizeof(flag))) { cfg.count = false; cfg.wa = 0; }

	// Second, uncached mapping of BAR0. The owning driver keeps its own map.
	map = pci->mapDeviceMemoryWithIndex(0, kIOMapInhibitCache);
	if (!map) { IOLog("TGLDisplayProbe: BAR0 map failed\n"); releaseResources(); return false; }
	mmio = reinterpret_cast<volatile uint8_t *>(map->getVirtualAddress());
	mmioLength = map->getLength();
	if (!mmio || mmioLength < 0x100000) { IOLog("TGLDisplayProbe: BAR0 too small\n"); releaseResources(); return false; }

	lock = IOSimpleLockAlloc();
	loop = IOWorkLoop::workLoop();
	timer = IOTimerEventSource::timerEventSource(this, timerFired);
	if (!lock || !loop || !timer || loop->addEventSource(timer) != kIOReturnSuccess) {
		releaseResources(); return false;
	}
	if (auto ser = OSSerializer::forTarget(this, serialize)) {
		setProperty("TGLDisplayProbe", ser);
		ser->release();
	}
	setProperty("TGLDisplayProbeVersion", 2ULL, 32);
	sleepNotifier = registerPrioritySleepWakeInterest(sleepWake, this, nullptr);
	registerService();
	timer->setTimeoutMS(periodMs);
	IOLog("TGLDisplayProbe: started %04x period=%ums count=%d wa=0x%x\n", device, periodMs, cfg.count, cfg.wa);
	return true;
}

// System sleep: stop sampling before the GPU powers down; resume a few
// seconds after full wake so the display has been re-initialised natively.
IOReturn TGLDisplayProbe::sleepWake(void *target, void *, UInt32 type, IOService *, void *, vm_size_t) {
	auto self = static_cast<TGLDisplayProbe *>(target);
	if (!self || !self->timer) return kIOReturnSuccess;
	switch (type) {
	case kIOMessageSystemWillSleep:
	case kIOMessageSystemWillPowerOff:
	case kIOMessageSystemWillRestart:
		__atomic_store_n(&self->paused, true, __ATOMIC_RELEASE);
		self->timer->cancelTimeout();
		break;
	case kIOMessageSystemHasPoweredOn:
		__atomic_store_n(&self->paused, false, __ATOMIC_RELEASE);
		self->timer->setTimeoutMS(3000);
		break;
	default:
		break;
	}
	return kIOReturnSuccess;
}

// The device must still decode memory. Config space stays readable in D3,
// so check the vendor ID and Memory Space Enable before touching BAR0.
bool TGLDisplayProbe::pciAccessible() {
	return pci->configRead16(kIOPCIConfigVendorID) == 0x8086 &&
		(pci->configRead16(kIOPCIConfigCommand) & kIOPCICommandMemorySpace);
}

void TGLDisplayProbe::findDonor() {
	if (donorState != 0) return;
	if (++donorAttempts > 600) { donorState = 3; return; }   // ~10 min at 1 s cadence
	auto matching = IOService::serviceMatching("AppleIntelFramebufferController");
	if (!matching) return;
	IOService *svc = IOService::copyMatchingService(matching);
	matching->release();
	if (!svc) return;
	const OSMetaClass *meta = svc->getMetaClass();
	const char *name = meta ? meta->getClassName() : nullptr;
	const uint64_t vptr = *reinterpret_cast<const uint64_t *>(svc);
	auto slot = [vptr](uint32_t i) { return reinterpret_cast<const uint64_t *>(vptr)[i]; };
	uint64_t addr[5] {};
	if (name && !strcmp(name, "AppleIntelFramebufferController") &&
	    TGLDP::Donor::qualify(vptr, reinterpret_cast<uint64_t>(meta), slot, addr)) {
		donor = svc;   // keep the reference: the donor kext cannot unload under us
		for (uint32_t i = 0; i < 5; ++i) {
			donorAddr[i] = addr[i];
			donorAtQualify[i] = donorValue[i] = *reinterpret_cast<volatile uint64_t *>(addr[i]);
		}
		donorState = 1;
		IOLog("TGLDisplayProbe: native framebuffer counters qualified\n");
	} else {
		svc->release();
		donorState = 2;
		IOLog("TGLDisplayProbe: framebuffer layout not qualified; counters disabled\n");
	}
}

void TGLDisplayProbe::timerFired(OSObject *owner, IOTimerEventSource *sender) {
	auto self = OSDynamicCast(TGLDisplayProbe, owner);
	if (!self || !self->mmio || __atomic_load_n(&self->paused, __ATOMIC_ACQUIRE)) return;
	if (self->donorState == 0 && self->st.ticks % (1000 / self->periodMs + 1) == 0) self->findDonor();
	if (self->donorState == 1) {
		uint64_t v[5];
		for (uint32_t i = 0; i < 5; ++i) v[i] = *reinterpret_cast<volatile uint64_t *>(self->donorAddr[i]);
		IOSimpleLockLock(self->lock); memcpy(self->donorValue, v, sizeof(v)); IOSimpleLockUnlock(self->lock);
	}
	if (!self->pciAccessible()) {
		IOSimpleLockLock(self->lock); ++self->pciUnavailable; ++self->st.ticks; IOSimpleLockUnlock(self->lock);
		sender->setTimeoutMS(self->periodMs);
		return;
	}
	IO io {self->mmio, self->mmioLength};
	// This work loop is the only writer. The lock only orders it against the
	// serializer's copy; a tick is a few dozen uncached reads.
	const uint64_t t = nowNs();
	IOSimpleLockLock(self->lock);
	TGLDP::tick(io, self->st, self->cfg, t);
	IOSimpleLockUnlock(self->lock);
	sender->setTimeoutMS(self->periodMs);
}

static void put(OSDictionary *d, const char *k, uint64_t v) {
	if (auto n = OSNumber::withNumber(v, 64)) { d->setObject(k, n); n->release(); }
}

bool TGLDisplayProbe::serialize(void *target, void *, OSSerialize *s) {
	auto self = static_cast<TGLDisplayProbe *>(target);
	if (!self->lock || !self->mmio) return false;
	auto copy = static_cast<TGLDP::State *>(IOMalloc(sizeof(TGLDP::State)));
	if (!copy) return false;
	uint64_t dv[5], dq[5], pciUnavail;
	IOSimpleLockLock(self->lock);
	*copy = self->st; memcpy(dv, self->donorValue, sizeof(dv)); memcpy(dq, self->donorAtQualify, sizeof(dq));
	pciUnavail = self->pciUnavailable;
	IOSimpleLockUnlock(self->lock);
	const TGLDP::State &st = *copy;
	auto d = OSDictionary::withCapacity(96);
	if (!d) { IOFree(copy, sizeof(*copy)); return false; }
	put(d, "Device", self->device); put(d, "PeriodMs", self->periodMs);
	put(d, "CountMode", self->cfg.count); put(d, "WAMask", self->cfg.wa); put(d, "PageMask", self->cfg.pageMask);
	put(d, "NowNs", nowNs()); put(d, "Ticks", st.ticks); put(d, "Sampled", st.sampled);
	put(d, "SkippedDC", st.skippedDC); put(d, "SkippedPW1Off", st.skippedPW1);
	put(d, "UnavailableAllOnes", st.unavailable); put(d, "UnavailablePCI", pciUnavail);
	put(d, "Paused", self->paused); put(d, "LastDCStateEn", st.dcLast); put(d, "Writes", st.writes);
	// Native counters: 0 searching, 1 qualified, 2 layout rejected, 3 not found.
	put(d, "DonorCounterState", self->donorState);
	static const char *const dn[] {"Entry", "PipeA", "PipeB", "PipeC", "Dispatch"};
	for (uint32_t i = 0; i < 5; ++i) {
		char k[48];
		snprintf(k, sizeof(k), "DonorUnderrun%s", dn[i]); put(d, k, self->donorState == 1 ? dv[i] : 0);
		snprintf(k, sizeof(k), "DonorUnderrun%sAtQualify", dn[i]); put(d, k, self->donorState == 1 ? dq[i] : 0);
	}
	put(d, "WASnapshotChanges", st.snapshotChanges); put(d, "WAReapplied", st.waReapplied);
	static const char *const waNames[] {"WAWriteBuddyCtl1", "WAWriteBuddyCtl2", "WAWriteBuddyMask1",
		"WAWriteBuddyMask2", "WAWriteDCPR2", "WAWriteDBufS1", "WAWriteDBufS2"};
	for (uint32_t i = 0; i < 7; ++i) put(d, waNames[i], st.waWrites[i]);
	static const char *const pipeNames[] {"A", "B", "C"};
	for (uint32_t p = 0; p < TGLDP::kPipes; ++p) {
		char k[40];
		snprintf(k, sizeof(k), "Pipe%sUnderruns", pipeNames[p]); put(d, k, st.pipe[p].underruns);
		snprintf(k, sizeof(k), "Pipe%sFirstNs", pipeNames[p]); put(d, k, st.pipe[p].firstNs);
		snprintf(k, sizeof(k), "Pipe%sLastNs", pipeNames[p]); put(d, k, st.pipe[p].lastNs);
		snprintf(k, sizeof(k), "Pipe%sLastFrame", pipeNames[p]); put(d, k, st.pipe[p].lastFrame);
		snprintf(k, sizeof(k), "Pipe%sPowered", pipeNames[p]); put(d, k, st.pipe[p].powered);
		snprintf(k, sizeof(k), "Pipe%sStickyAtFirstSample", pipeNames[p]); put(d, k, st.pipe[p].stickyAtFirstSample);
		snprintf(k, sizeof(k), "Pipe%sFirstSampled", pipeNames[p]); put(d, k, st.pipe[p].firstSampled);
	}
	auto snap = [&](const char *prefix, const TGLDP::Snapshot &x) {
		char k[48];
		auto p = [&](const char *name, uint32_t v) { snprintf(k, sizeof(k), "%s%s", prefix, name); put(d, k, v); };
		p("Valid", x.valid); p("DCStateEn", x.dcState); p("PwrWellCtl2", x.pwrWell);
		p("BuddyCtl1", x.buddyCtl[0]); p("BuddyCtl2", x.buddyCtl[1]);
		p("BuddyMask1", x.buddyMask[0]); p("BuddyMask2", x.buddyMask[1]);
		p("DCPR2", x.dcpr2); p("DBufS1", x.dbuf[0]); p("DBufS2", x.dbuf[1]);
		p("SouthDspClkGateD", x.southGate); p("PSRCtl", x.psr); p("PSR2Ctl", x.psr2);
		p("IMRA", x.imr[0]); p("IMRB", x.imr[1]); p("IMRC", x.imr[2]);
	};
	snap("First", st.first);
	snap("Last", st.last);
	put(d, "EventCount", st.ringNext);
	if (auto ring = OSArray::withCapacity(TGLDP::kRing)) {
		const uint32_t n = st.ringNext < TGLDP::kRing ? st.ringNext : TGLDP::kRing;
		for (uint32_t i = 0; i < n; ++i) {
			const auto &e = st.ring[(st.ringNext - n + i) % TGLDP::kRing];
			if (auto ed = OSDictionary::withCapacity(5)) {
				put(ed, "Ns", e.ns); put(ed, "Pipe", e.pipe); put(ed, "Frame", e.frame);
				put(ed, "Flips", e.flips); put(ed, "Status", e.status);
				ring->setObject(ed); ed->release();
			}
		}
		d->setObject("Events", ring); ring->release();
	}
	const bool ok = d->serialize(s);
	d->release();
	IOFree(copy, sizeof(*copy));
	return ok;
}

void TGLDisplayProbe::releaseResources() {
	if (sleepNotifier) { sleepNotifier->remove(); sleepNotifier = nullptr; }
	if (timer) {
		timer->cancelTimeout();
		if (loop) loop->removeEventSource(timer);
		timer->release(); timer = nullptr;
	}
	if (loop) { loop->release(); loop = nullptr; }
	mmio = nullptr;
	if (map) { map->release(); map = nullptr; }
	// The lock is freed in free(): an in-flight serializer may still use it.
	donorState = 0;
	if (donor) { donor->release(); donor = nullptr; }
}

void TGLDisplayProbe::free() {
	releaseResources();
	if (lock) { IOSimpleLockFree(lock); lock = nullptr; }
	IOService::free();
}

void TGLDisplayProbe::stop(IOService *provider) {
	removeProperty("TGLDisplayProbe");
	releaseResources();
	IOService::stop(provider);
}
