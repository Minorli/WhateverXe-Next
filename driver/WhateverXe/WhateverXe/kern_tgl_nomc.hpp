// SPDX-License-Identifier: BSD-3-Clause
// Derived from SaqibAlMalik/WhateverXe e0aff600 (issue #109); NootedGreen references below.
// -wex-tgl-nomc: run the exact AppleIntelTGLGraphics 16.0.32 donor without
// multi-context, i.e. without the CCS (compute work then shares the RCS).
// Fallback for render-domain recovery: a CCS hang can no longer reset the RCS
// and RCU_MODE can never be left half-enabled. Port of NootedGreen NP550XDA
// -ngreennomc (kern_gen11.cpp 2486-2497). Two exact-offset sites; every site
// is byte-checked before any is patched and re-checked afterwards:
//  - 0x27499, IntelAccelerator::populateAccelConfig (0x270E8)+0x3B1, right
//    after utilGetProperty("EnableMultiContext") (0x27494): `and eax,ebx`
//    becomes `xor eax,eax`, so accel+0x1190 bit 33 (CCS enable, also read by
//    initModeRegisters for RCU_MODE bit 0) is stored as 0.
//  - 0x4BF0A, IntelTGLMemoryManager::newUnifiedAddressRangeAllocator
//    (0x4BED8)+0x32: the `je` after `test byte [accel+0x1194],2` (the same
//    bit 33) becomes two NOPs, so the high-range reservation (reserveRange
//    0xFFFF800000000000, size 0x800000000000 at 0x4BF34) stays as with the
//    CCS on. Native EnableMultiContext=0 skips it; keeping it follows NP550XDA
//    and is not proven necessary.
// Both patterns occur exactly once in the donor binary.
#ifndef kern_tgl_nomc_hpp
#define kern_tgl_nomc_hpp
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace TGLNoMC {
constexpr size_t MaxLength=16;
struct Site {uint32_t offset; uint8_t length; uint8_t find[MaxLength]; uint8_t replace[MaxLength];};
constexpr Site Sites[2] {
 {0x27499,16,{0x21,0xD8,0x48,0xC1,0xE0,0x21,0x48,0xB9,0xFF,0xFF,0xFF,0xFF,0xFD,0xFF,0xFF,0xFF},
             {0x31,0xC0,0x48,0xC1,0xE0,0x21,0x48,0xB9,0xFF,0xFF,0xFF,0xFF,0xFD,0xFF,0xFF,0xFF}},
 {0x4BF0A,14,{0xF6,0x80,0x94,0x11,0x00,0x00,0x02,0x74,0x26,0x41,0xF6,0x46,0x24,0x02},
             {0xF6,0x80,0x94,0x11,0x00,0x00,0x02,0x90,0x90,0x41,0xF6,0x46,0x24,0x02}},
};
constexpr size_t End=0x4BF0A+14; // one past the last checked byte
enum State : unsigned {Unknown,Original,Patched};
// Original: every site holds its native bytes; Patched: every site holds its
// replacement. Anything else (a partial or foreign edit) is Unknown.
inline State state(const uint8_t *base,size_t size) {
 if(!base || size<End)return Unknown;
 bool original=true,patched=true;
 for(const auto &s:Sites) {
  original=original && !memcmp(base+s.offset,s.find,s.length);
  patched=patched && !memcmp(base+s.offset,s.replace,s.length);
 }
 return original?Original:patched?Patched:Unknown;
}
enum class InstallResult {Unchanged,Installed,Unsafe};
// A patcher can write bytes and still report failure. Roll back by inspecting
// both sites, not by counting successful calls. Never overwrite foreign bytes.
// Unsafe means our attempted transaction did not restore the original image;
// the caller must prevent accelerator startup in that case.
template<class Patch>
inline InstallResult install(uint8_t *base,size_t size,Patch patch) {
 if(state(base,size)!=Original)return InstallResult::Unchanged;
 bool ok=true;
 for(const auto &s:Sites) {
  const bool applied=patch(s,false);
  if(!applied || memcmp(base+s.offset,s.replace,s.length)) {ok=false;break;}
 }
 if(ok && state(base,size)==Patched)return InstallResult::Installed;
 for(const auto &s:Sites)
  if(!memcmp(base+s.offset,s.replace,s.length))patch(s,true);
 return state(base,size)==Original?InstallResult::Unchanged:InstallResult::Unsafe;
}
}
#endif
