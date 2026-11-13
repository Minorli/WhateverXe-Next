// SPDX-License-Identifier: BSD-3-Clause
// Derived from SaqibAlMalik/WhateverXe e0aff600 (issue #109); NootedGreen references below.
// -wex-tgl-gpurec: render reset-domain recovery for the
// exact AppleIntelTGLGraphics 16.0.32 donor. Port of NootedGreen XE-GPUREC
// v1+v2 and XE-STAMPFIX (JulioNectar XPS13Plus kern_gen11.cpp 12109-12354;
// NP550XDA kern_gen11.cpp 13298-13345, 14521-14606).
//  - IGHardwareRingBuffer::resetGraphicsEngine (0x43576) saves ring+0x78
//    (IGVector<resetRegisterInfo>, 0x24-byte entries), writes GDRST 0x941C =
//    ring+0x70 (0x43769), restores that list with getDefaultResetValue
//    (0x2E60A) or the saved value (masked | 0xFFFF0000), then replays only its
//    own engine: scheduler vtable+0x190 (0x4385B) -> IGScheduler5::
//    processGPUReset (0x380D8) -> IGHardwareCommandStreamer5::processGPUReset
//    (0x3A396). RCS (_rcsResetRegisters 0x14B2B0) and CCS (_ccsResetRegisters
//    0x14AEB8) share GDRST bit 1, so a reset of either leaves the other at
//    hardware defaults (RCS GFX_MODE 0, FORCE_TO_NONPRIV0 0xE000).
//  - IntelAccelerator::initModeRegisters (0x28E7E) programs RCU_MODE 0x14800
//    bit 0 = (accel+0x1190 >> 33) & 1 (CCS enable) and CCS FORCE_TO_NONPRIV0
//    0x1A4D0 (0x28F27) only at engine start; no reset list restores them.
//  - IGAccelEventMachine::eventTimeout (0x15F22) maps a stamp index to an
//    engine through IG_STAMP_INDEX_TO_HWCS_LUT (0xA0290; _SCI 0xA0280). It
//    returns NULL (no channel, nothing restarted) for reason 1 (display stamp
//    range), 2 (accel+0x1190 & 0x20, 0x15F55), 3 (checkForProgress), 4 (no
//    active context: halt/resume, 0x16076..0x160A1) and 5 (another context
//    active, under the time threshold: 0x160A3..0x16101). Reason 6 (the
//    active context owns the stamp, or the threshold passed) calls
//    prepareGPUReset and returns the channel for a real restart (0x16120).
//  - Upstream stampfix (NOT included here) acts ONLY on a NULL return. It never touches reason 6,
//    so it does not stop a loop of real restarts: repeated restarts of one
//    context (a reset that does not clear the hang) are outside its reach.
#ifndef kern_tgl_gpurec_hpp
#define kern_tgl_gpurec_hpp
#include <stdint.h>
#include <string.h>

namespace TGLGpuRec {
constexpr unsigned EntrySize=0x24, MaxEntries=64;
constexpr uint32_t FirstRegister[2] {0x2080,0x1A080};   // HWS_PGA heads both native lists
constexpr uint32_t Extras[5] {0x14800,0xCE90,0x1A4D0,0x1A0A8,0x1A098};
constexpr bool ExtrasMasked[5] {true,true,false,false,false};
// TGLGpuRecLive = {LiveRevision, Flag bits, counts[Slots]}. Revision 1 ended
// at WhitelistFixes (14 slots); revision 2 appends after it. Decoders index by
// position: never reorder, only insert before Slots and bump the revision.
constexpr uint32_t LiveRevision=2;
enum Flag : uint32_t {RegistersFlag=1,ReservedStampsFlag=2,HaltFlag=4,WithPOSHFlag=8};
enum Count : unsigned {
 Resets,          // qualified render-domain resets seen by the route
 SiblingRestores, // sibling list rewritten after GDRST wiped its HWS_PGA
 SiblingReplays,  // IGScheduler5::processGPUReset calls for the sibling: CALLS,
                  // not resubmits (see SiblingResubmits)
 OwnFixes,KickSkips,
 ExtrasFixes,     // fix-up passes that wrote RCU_MODE or a CCS extra
 LastRCU,         // RCU_MODE after the last fix-up pass (post-fix value)
 Last,            // own | ok<<4 | kick<<5 | did<<8 (1 sibling restore, 2 own
                  // fix, 4 sibling replay, 8 extras/whitelist rewritten) |
                  // sibling HWS_PGA snapshot & 0xFFFFF000
 SiblingNotReady,ListInvalid,ReservedStampFixes,ReservedStampLast,ReservedStampDeclined,
 WhitelistFixes,  // passes that rewrote an RCS FORCE_TO_NONPRIV constant
 // Revision 2.
 PreFixRCU,       // RCU_MODE between GDRST and the own replay, before any fix:
                  // with the CCS enabled at start, bit 0 clear here means the
                  // GDRST cleared the CCS enable
 PreFixReads,     // number of PreFixRCU samples (0: PreFixRCU never written)
 PocsBefore,PocsAfter,       // 0x18080 around the native reset (read-only)
 Pocs88Before,Pocs88After,   // 0x18088 around the native reset (read-only)
 SiblingResubmits, // sibling replays whose streamer had an active execlist
                   // element, so the native beginReset/resubmit actually ran
 SiblingHalts,    // -wex-tgl-gpurec-halt: native haltCommandStreamer(sibling)
 HaltNotReady,    // halt returned without READY_TO_RESET (its poll timed out)
 SiblingResumes,  // REQUEST_RESET survived the native reset: native resume
 Slots
};
// Read-only POCS observation; never written (its full register set is
// unknown). 0x18080 is the POCS HWS_PGA (initHardwareStatusPageRegisters
// 0x28FB8); startGraphicsEngine writes 0x18088=0x80008 (0x252E9). Neither is
// in a native reset list.
constexpr uint32_t Pocs[2] {0x18080,0x18088};

struct List {uint64_t count; const uint8_t *data;};
// IGVector layout: {u64 count; u64 capacity; resetRegisterInfo *data}.
inline bool list(const void *vector,unsigned engine,List &out) {
 if(!vector || engine>1)return false;
 auto v=static_cast<const uint8_t *>(vector);
 memcpy(&out.count,v,8);memcpy(&out.data,v+0x10,8);
 uint32_t first=0;
 if(!out.data || !out.count || out.count>MaxEntries)return false;
 memcpy(&first,out.data,4);
 return first==FirstRegister[engine];
}
inline uint32_t reg(const List &l,uint64_t i) {uint32_t r;memcpy(&r,l.data+i*EntrySize,4);return r;}
inline bool masked(const List &l,uint64_t i) {return l.data[i*EntrySize+8]!=0;}
// Same value rule as the native restore loop after GDRST.
inline uint32_t restoreValue(uint32_t nativeDefault,uint32_t saved,bool isMasked) {
 uint32_t v=nativeDefault?nativeDefault:saved;
 return isMasked?(v|0xFFFF0000U):v;
}
// The native restore loop (0x437FA..0x43840) over one list. nativeDefault(r)
// is getDefaultResetValue, write(r,v) the MMIO store.
template<class Default,class Write>
inline void writeList(const List &l,const uint32_t *saved,Default nativeDefault,Write write) {
 for(uint64_t i=0;i<l.count;++i) {
  const uint32_t r=reg(l,i);
  write(r,restoreValue(nativeDefault(r),saved[i],masked(l,i)));
 }
}
inline uint32_t ccsEnabled(uint64_t features) {return static_cast<uint32_t>((features>>33)&1);}
// Writes needed after a render-domain reset. Bit n selects Extras[n].
inline uint32_t plan(const uint32_t live[5],const uint32_t *good,uint32_t ccsOn,uint32_t value[5]) {
 uint32_t mask=0;
 if((live[0]&1)!=ccsOn) {value[0]=0x10000U|ccsOn;mask|=1;}   // initModeRegisters' masked bit 0
 // initModeRegisters writes 0xCE90 and the CCS registers only with CCS on
 // (je 0x28EBA after the RCU store); leave them alone otherwise.
 if(good && ccsOn)for(unsigned i=1;i<5;++i) {
  const bool same=ExtrasMasked[i]?((live[i]&0xFFFF)==(good[i]&0xFFFF)):(live[i]==good[i]);
  if(same)continue;
  value[i]=ExtrasMasked[i]?(0xFFFF0000U|(good[i]&0xFFFF)):good[i];mask|=1U<<i;
 }
 return mask;
}
// A snapshot is trusted only while RCU_MODE still matches the start-up value.
inline bool healthy(const uint32_t live[5],uint32_t ccsOn) {return (live[0]&1)==ccsOn;}

// RCS FORCE_TO_NONPRIV 0x24D0..0x24FC are in _rcsResetRegisters (rcsRegisters
// 0xC2200) but not in populateResetRegisterList's default list (acc+0x12C0),
// so the native restore writes back the pre-reset value: once a sibling reset
// has wiped them (0xE000 seen in a 9A49 gpuRestart report after an own RCS
// reset), they stay wiped. initModeRegisters programs these constants
// (0x28EC3..0x28F1D) when accel+0x1191 bit 0 is set; the last two also need
// bit 25 of accel+0xFE6.
struct Pair {uint32_t reg,value;};
constexpr Pair Whitelist[9] {{0x24D0,0xE100},{0x24D4,0x7010},{0x24D8,0xB134},{0x24DC,0x7008},{0x24E0,0x2580},
 {0x24F0,0x731C},{0x24F4,0x2244},{0x24F8,0x10002351},{0x24FC,0x10002341}};
// Bit n selects Whitelist[n] for a rewrite.
inline uint32_t whitelistPlan(const uint32_t live[9],bool programmed,bool optional) {
 uint32_t mask=0;
 if(!programmed)return 0;
 for(unsigned i=0;i<9;++i) {
  if(i>=7 && !optional)break;
  if(live[i]!=Whitelist[i].value)mask|=1U<<i;
 }
 return mask;
}
// Read the whitelist, rewrite what differs; returns the whitelistPlan mask.
template<class Read,class Write>
inline uint32_t fixWhitelist(Read read,Write write,bool programmed,bool optional) {
 uint32_t live[9];
 for(unsigned i=0;i<9;++i)live[i]=read(Whitelist[i].reg);
 const uint32_t rewrite=whitelistPlan(live,programmed,optional);
 for(unsigned i=0;i<9;++i)if(rewrite&(1U<<i))write(Whitelist[i].reg,Whitelist[i].value);
 return rewrite;
}
// List restore after the native reset. FORCE_TO_NONPRIV has no native
// default, so a list write stores whatever the snapshot held (0xE000 once an
// earlier sibling reset wiped it) and would undo an earlier whitelist fix-up.
// The whitelist is therefore re-applied after every list write: it is always
// the last writer. Returns the whitelist rewrite mask.
template<class Default,class Read,class Write>
inline uint32_t restoreList(const List &l,const uint32_t *saved,Default nativeDefault,Read read,Write write,
                            bool programmed,bool optional) {
 writeList(l,saved,nativeDefault,write);
 (void)read(reg(l,0)); // posting read
 return fixWhitelist(read,write,programmed,optional);
}

// IGHardwareCommandStreamer5::processGPUReset (0x3A396) loads the active
// execlist element [this+0x30+16*port], port = u32 this+0x50 (0 or 1); when it
// is NULL the je at 0x3A3C8 goes to the epilogue (0x3A52B) with no
// beginReset/resubmission (the CSB head was already rewritten before that).
constexpr unsigned StreamerPort=0x50, StreamerElements=0x30, ElementStride=16;
inline bool activeElement(const uint8_t *streamer) {
 if(!streamer)return false;
 uint32_t port;memcpy(&port,streamer+StreamerPort,sizeof(port));
 if(port>1)return false;
 const void *element;memcpy(&element,streamer+StreamerElements+port*ElementStride,sizeof(element));
 return element!=nullptr;
}

// -wex-tgl-gpurec-halt. IGScheduler::haltCommandStreamer(IGHwCsType) (0x56978)
// stores RING_RESET_CTL (kIGHwCsDesc+0x3D: RCS 0x20D0, CCS 0x1A0D0) = masked
// REQUEST_RESET and polls READY_TO_RESET (bit 1), at most 10000 times, with
// assert_wait_timeout/thread_block like the GDRST poll in resetGraphicsEngine.
// resumeCommandStreamer (0x56A2E) stores the masked clear and polls the same
// way. The only native halt call (eventTimeout 0x1603C) stops the engine
// whose stamp timed out; the other engine of the render domain keeps running
// into the GDRST.
constexpr uint32_t ResetControl[2] {0x20D0,0x1A0D0};
constexpr uint32_t RequestReset=1, ReadyToReset=2;
// Halt only ahead of the plain GDRST path (no ring-kick engine flags) and only
// an engine that start-up enabled.
inline bool haltWanted(bool enabled,bool kick,bool siblingInUse) {return enabled && !kick && siblingInUse;}
inline bool haltReady(uint32_t control) {return control!=0xFFFFFFFFU && (control&ReadyToReset);}
// GDRST resets the sibling's RESET_CTL with the rest of the render domain; a
// request that is still set afterwards needs the native resume.
inline bool resumeWanted(uint32_t control) {return control!=0xFFFFFFFFU && (control&RequestReset);}

// No orphan-stamp completion: native returned/submitted and signal ownership remain unchanged.
}
#endif
