// SPDX-License-Identifier: BSD-3-Clause
// Standalone, fail-closed native RAM counter observer. Version 1.0.1 does not
// map BAR0: an independent timer and a power-well snapshot are not a PM lease.
// The MMIO algorithm remains CPU-tested in Logic.hpp, not deployed to hardware.
#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOMessage.h>
#include <libkern/c++/OSSerialize.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSString.h>
#include <pexpert/pexpert.h>
#include <kern/clock.h>
#include "TGLDisplayProbeLogic.hpp"
extern "C" IONotifier *registerPrioritySleepWakeInterest(IOServiceInterestHandler, void *, void *);

class TGLDisplayProbe : public IOService {
    OSDeclareDefaultStructors(TGLDisplayProbe)
    IOPCIDevice *pci {};
    IOWorkLoop *loop {};
    IOTimerEventSource *timer {};
    IOLock *lock {};
    IONotifier *sleepNotifier {};
    IOService *donor {};
    uint64_t donorAddr[5] {};
    bool readCounter(uint64_t, void *, size_t) const;
    uint16_t device {};
    uint32_t periodMs {100};
    TGLDP::Lifecycle life {}; // only under the work-loop gate
    struct Status {
        uint64_t now {}, ticks {}, sampled {}, readFailures {};
        uint64_t values[5] {}, baseline[5] {};
        uint32_t donorState {}, attempts {};
        bool paused {}, valid {}, stopped {};
    } status;
    static void timerFired(OSObject *, IOTimerEventSource *);
    static IOReturn sleepWake(void *, void *, UInt32, IOService *, void *, vm_size_t);
    static IOReturn transition(OSObject *, void *, void *, void *, void *);
    static bool serialize(void *, void *, OSSerialize *);
    void findDonor(); // work-loop gate, never under serialization lock
    void releaseResources();
public:
    bool start(IOService *) override;
    void stop(IOService *) override;
    void free() override;
};
OSDefineMetaClassAndStructors(TGLDisplayProbe, IOService)

static uint64_t nowNs() {
    uint64_t t, ns; clock_get_uptime(&t); absolutetime_to_nanoseconds(t, &ns); return ns;
}
bool TGLDisplayProbe::readCounter(uint64_t address, void *out, size_t size) const {
    // Only the five admitted, aligned globals in pinned donor DATA are readable.
    // This is not a general fault-recovery primitive or arbitrary kernel reader.
    if (!donor || donor->isInactive() || size != sizeof(uint64_t)) return false;
    for (auto allowed : donorAddr) if (address == allowed) {
        const uint64_t value = *reinterpret_cast<volatile const uint64_t *>(address);
        memcpy(out, &value, sizeof(value)); return true;
    }
    return false;
}
static void put(OSDictionary *d, const char *k, uint64_t value) {
    if (auto n = OSNumber::withNumber(value, 64)) { d->setObject(k, n); n->release(); }
}

bool TGLDisplayProbe::start(IOService *provider) {
    char flag[8];
    if (PE_parse_boot_argn("-tgldp-off", flag, sizeof(flag))) return false;
    bool count = PE_parse_boot_argn("-tgldp-count", flag, sizeof(flag));
    uint32_t wa = 0; PE_parse_boot_argn("tgldp-wa", &wa, sizeof(wa));
    // Refuse unsupported requests, even in combination with -tgldp-readonly.
    // Never silently claim that an opt-in write experiment actually ran.
    if (!TGLDP::acceptsWriteMode(count, wa)) {
        IOLog("TGLDisplayProbe: MMIO/write modes not qualified in 1.0.1; refusing start\n"); return false;
    }
    if (!IOService::start(provider)) return false;
    pci = OSDynamicCast(IOPCIDevice, provider);
    if (!pci || pci->getBusNumber() || pci->getDeviceNumber() != 2 || pci->getFunctionNumber()) return false;
    const uint16_t vendor = pci->configRead16(kIOPCIConfigVendorID);
    device = pci->configRead16(kIOPCIConfigDeviceID);
    const uint16_t ids[] {0x9A40, 0x9A49, 0x9A78, 0x9AC0, 0x9AC9, 0x9AD9, 0x9AF8};
    bool supported = false; for (auto id : ids) if (device == id) supported = true;
    if (vendor != 0x8086 || !supported) return false;
    uint32_t ms = 0;
    if (PE_parse_boot_argn("tgldp-ms", &ms, sizeof(ms))) periodMs = ms < 100 ? 100 : ms > 1000 ? 1000 : ms;
    lock = IOLockAlloc();
    loop = IOWorkLoop::workLoop();
    timer = IOTimerEventSource::timerEventSource(this, timerFired);
    if (!lock || !loop || !timer || loop->addEventSource(timer) != kIOReturnSuccess) { releaseResources(); return false; }
    sleepNotifier = registerPrioritySleepWakeInterest(sleepWake, this, nullptr);
    if (!sleepNotifier) { releaseResources(); return false; }
    auto ser = OSSerializer::forTarget(this, serialize);
    if (!ser) { releaseResources(); return false; }
    const bool published = setProperty("TGLDisplayProbe", ser);
    ser->release();
    if (!published) { releaseResources(); return false; }
    setProperty("TGLDisplayProbeVersion", 3ULL, 32);
    registerService();
    timer->setTimeoutMS(periodMs);
    IOLog("TGLDisplayProbe: started %04x native-RAM-only period=%ums; MMIO unavailable\n", device, periodMs);
    return true;
}

IOReturn TGLDisplayProbe::sleepWake(void *target, void *, UInt32 type, IOService *, void *, vm_size_t) {
    auto self = static_cast<TGLDisplayProbe *>(target);
    if (!self || !self->loop) return kIOReturnSuccess;
    return self->loop->runAction(transition, self, reinterpret_cast<void *>(uintptr_t(type)));
}
IOReturn TGLDisplayProbe::transition(OSObject *owner, void *arg, void *, void *, void *) {
    auto self = static_cast<TGLDisplayProbe *>(owner);
    // IOTimerEventSource runs under this same gate. Taking it drains a running
    // sample before acknowledging sleep; cancellation alone is insufficient.
    const uintptr_t type = reinterpret_cast<uintptr_t>(arg);
    if (type == UINTPTR_MAX) self->life.stop();
    switch (type) {
    case kIOMessageSystemWillSleep:
    case kIOMessageSystemWillPowerOff:
    case kIOMessageSystemWillRestart:
        self->life.pause(); if (self->timer) self->timer->cancelTimeout(); break;
    case kIOMessageSystemHasPoweredOn:
        self->life.wake(nowNs());
        if (!self->life.stopping && self->timer) self->timer->setTimeoutMS(3000);
        break;
    default: break;
    }
    if (self->life.stopping && self->timer) self->timer->cancelTimeout();
    if (self->lock) {
        IOLockLock(self->lock);
        self->status.paused = !self->life.canSample(nowNs());
        self->status.stopped = self->life.stopping;
        IOLockUnlock(self->lock);
    }
    return kIOReturnSuccess;
}

void TGLDisplayProbe::findDonor() {
    IOLockLock(lock);
    if (status.donorState || ++status.attempts > 600) {
        if (!status.donorState) status.donorState = 3;
        IOLockUnlock(lock); return;
    }
    IOLockUnlock(lock);
    auto matching = IOService::serviceMatching("AppleIntelFramebufferController");
    if (!matching) return;
    IOService *svc = IOService::copyMatchingService(matching);
    matching->release();
    if (!svc) return;
    const OSMetaClass *meta = svc->getMetaClass();
    const char *name = meta ? meta->getClassName() : nullptr;
    const uint64_t vptr = *reinterpret_cast<const uint64_t *>(svc);
    uint64_t addr[5] {}, values[5] {};
    auto slot = [&](uint32_t index) {
        return reinterpret_cast<const uint64_t *>(vptr)[index];
    };
    bool qualified = name && !strcmp(name, "AppleIntelFramebufferController") && svc->getProvider() == pci &&
        TGLDP::Donor::qualify(vptr, reinterpret_cast<uint64_t>(meta), slot, addr);
    if (qualified) {
        // These checks follow the original five vtable/text and DATA guards.
        // The matching live object pins the donor mapping, including __text.
        const uint64_t base = slot(0) - TGLDP::Donor::kSlots[0].fileTarget;
        auto readText = [base](uint64_t at, void *out, size_t size) {
            constexpr uint64_t allowed[] {0x744CA, 0x744EC, 0x74514, 0x74539, 0x74084};
            if (size != 7) return false;
            for (auto offset : allowed) if (at == base + offset) { memcpy(out, reinterpret_cast<const void *>(at), size); return true; }
            return false;
        };
        qualified = TGLDP::Donor::qualifyInstructions(slot(0), addr, readText);
    }
    if (qualified) {
        donor = svc; // live object pins donor code/data; only release after draining timer
        memcpy(donorAddr, addr, sizeof(addr));
        qualified = TGLDP::Donor::sample(addr, values, [this](uint64_t at, void *out, size_t size) { return readCounter(at, out, size); });
    }
    if (qualified) {
        IOLockLock(lock);
        memcpy(status.values, values, sizeof(values)); memcpy(status.baseline, values, sizeof(values));
        status.valid = true; status.donorState = 1;
        IOLockUnlock(lock);
    } else {
        donor = nullptr;
        svc->release();
        IOLockLock(lock); status.donorState = 2; IOLockUnlock(lock);
    }
    IOLog("TGLDisplayProbe: native counter qualification %s\n", qualified ? "passed" : "rejected");
}

void TGLDisplayProbe::timerFired(OSObject *owner, IOTimerEventSource *sender) {
    auto self = OSDynamicCast(TGLDisplayProbe, owner);
    if (!self || self->life.stopping) return;
    const uint64_t now = nowNs();
    if (!self->life.canSample(now)) {
        if (!self->life.sleeping) sender->setTimeoutMS(self->periodMs);
        return;
    }
    // Search at approximately one-second cadence. No registry calls while
    // holding lock: the OSSerializer can enter with a registry lock held.
    if (self->status.ticks % (1000 / self->periodMs + 1) == 0) self->findDonor();
    uint64_t values[5] {};
    const bool ok = self->donor && !self->donor->isInactive() &&
        TGLDP::Donor::sample(self->donorAddr, values, [self](uint64_t at, void *out, size_t size) { return self->readCounter(at, out, size); });
    IOLockLock(self->lock);
    self->status.now = now; ++self->status.ticks; self->status.paused = false;
    self->status.valid = ok;
    if (ok) { memcpy(self->status.values, values, sizeof(values)); ++self->status.sampled; }
    else if (self->donor) ++self->status.readFailures;
    IOLockUnlock(self->lock);
    sender->setTimeoutMS(self->periodMs);
}

bool TGLDisplayProbe::serialize(void *target, void *, OSSerialize *s) {
    auto self = static_cast<TGLDisplayProbe *>(target);
    if (!self->lock) return false;
    IOLockLock(self->lock); const Status x = self->status; IOLockUnlock(self->lock);
    auto d = OSDictionary::withCapacity(32); if (!d) return false;
    put(d, "Device", self->device); put(d, "PeriodMs", self->periodMs);
    put(d, "NowNs", x.now); put(d, "ReadNs", nowNs()); put(d, "Ticks", x.ticks);
    put(d, "DonorSampled", x.sampled); put(d, "DonorReadFailures", x.readFailures);
    put(d, "Paused", x.paused); put(d, "Stopped", x.stopped);
    put(d, "DonorCounterState", x.donorState); put(d, "DonorCountersValid", x.valid && !x.paused && !x.stopped);
    put(d, "MMIOAccessState", TGLDP::kMMIOQualified); put(d, "SkippedNoPowerLease", x.ticks);
    if (auto reason = OSString::withCString("No qualified display power-domain lease; RAM counters only")) {
        d->setObject("MMIOUnavailableReason", reason); reason->release();
    }
    put(d, "Writes", 0); put(d, "CountMode", 0); put(d, "WAMask", 0);
    // Unsupported measurements are omitted, never emitted as measured zeros.
    if (x.donorState == 1 && x.valid && !x.paused && !x.stopped) {
        const char *names[] {"Entry", "PipeA", "PipeB", "PipeC", "Dispatch"};
        for (uint32_t i = 0; i < 5; ++i) {
            char key[48];
            snprintf(key, sizeof(key), "DonorUnderrun%s", names[i]); put(d, key, x.values[i]);
            snprintf(key, sizeof(key), "DonorUnderrun%sAtQualify", names[i]); put(d, key, x.baseline[i]);
        }
    }
    const bool ok = d->serialize(s); d->release(); return ok;
}

void TGLDisplayProbe::releaseResources() {
    if (loop) {
        loop->runAction(transition, this, reinterpret_cast<void *>(UINTPTR_MAX));
    }
    // Never wait for an interest callback while holding the gate it needs.
    if (sleepNotifier) { sleepNotifier->remove(); sleepNotifier = nullptr; }
    if (timer) {
        if (loop) loop->removeEventSource(timer);
        timer->release(); timer = nullptr;
    }
    if (loop) { loop->release(); loop = nullptr; }
    if (donor) { donor->release(); donor = nullptr; }
}
void TGLDisplayProbe::stop(IOService *provider) {
    removeProperty("TGLDisplayProbe"); releaseResources(); IOService::stop(provider);
}
void TGLDisplayProbe::free() {
    releaseResources();
    if (lock) { IOLockFree(lock); lock = nullptr; }
    IOService::free();
}
