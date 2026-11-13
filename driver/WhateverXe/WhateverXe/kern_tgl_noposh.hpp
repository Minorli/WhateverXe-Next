// SPDX-License-Identifier: BSD-3-Clause
// Derived from SaqibAlMalik/WhateverXe e0aff600 (issue #109); NootedGreen references below.
// -wex-tgl-no-posh: Tiger Lake RCS contexts without POSH (position-only
// shading on the POCS engine). Exact AppleIntelTGLGraphics 16.0.32 donor:
//  - IGHardwareContext flags byte +0x6E bit 0x10 marks a POSH context.
//    initWithOptions tests it at 0x7BB13 (allocates the 16 KiB POCS image at
//    +0xA0), restoreFromSafeImage at 0x7C421 (copies the POCS safe image) and
//    submitBatchBuffer at 0x43021 (MI_BATCH_BUFFER_START bit 20).
//  - g_cInitGfxHwCtxParamsForGraphics (0xB09A4 startup, 0xB0BC0 command
//    queues) is {0,0,0x1C,...}: every graphics context and both safe-image
//    source contexts made by startGraphicsEngine are POSH contexts.
//  - LRC page 1 (g_cInitGfxRingContextRCS, 0xC04A0): CTX_CTRL 0x2244 at
//    +0x1008/+0x100C and POSH_LRCA 0x21B0 at +0x10D4/+0x10D8.
//  - The clone branch of initWithOptions (0x7C169..0x7C17A) gives a non-POSH
//    context CTX_CTRL=(v&0xFFEFFFEF)|0x00100000. beginReset (0x7C919) calls
//    restoreFromSafeImage without that fix-up, so a GPU reset restores the
//    safe image's POSH control bit and POSH_LRCA (the released startup
//    context's POCS image) into every RCS context it replays.
// Clearing the flag before the native copy, then applying the same fix-up
// plus POSH_LRCA=0 after every successful copy, gives each RCS context the
// non-POSH image that the native clone branch builds. Port of NootedGreen
// xe/noposh (NP550XDA kern_gen11.cpp 14927-14946, default since 2026-11-03).
#ifndef kern_tgl_noposh_hpp
#define kern_tgl_noposh_hpp
#include <stdint.h>

namespace TGLNoPOSH {
constexpr unsigned EngineOffset=0x6C, FlagsOffset=0x6E;
constexpr uint8_t POSHFlag=0x10;
constexpr uint32_t ControlRegister=0x2244, ControlRegisterOffset=0x1008, ControlOffset=0x100C;
constexpr uint32_t LRCARegister=0x21B0, LRCARegisterOffset=0x10D4, LRCAOffset=0x10D8;
// Masked register: bit 20 enables the write of bit 4 (POSH enable).
constexpr uint32_t ControlKeep=0xFFEFFFEFU, ControlMaskBit4=0x00100000U;
enum Count : unsigned {
 Calls,          // RCS restoreFromSafeImage calls seen while enabled
 FlagCleared,    // POSH flag found set and cleared before the native copy
 RestoreFalse,   // native copy declined (no safe image yet: startup contexts)
 InitSanitized,  // successful copies from initWithOptions (return 0x7C131)
 ResetSanitized, // successful copies from beginReset (return 0x7C91E) or others
 LRCAZeroed,     // POSH_LRCA was non-zero and was cleared
 ControlCleared, // CTX_CTRL bit 4 was set and was cleared
 LayoutMismatch, // register identities differ: nothing written
 Slots
};
enum Result : uint32_t {ZeroedLRCA=1,ClearedControl=2,ChangedControl=4,Mismatch=0x80000000U};

// RCS only. True when the POSH flag was set and is now clear.
inline bool clearFlag(uint8_t *context) {
 if(!context || context[EngineOffset]!=0 || !(context[FlagsOffset]&POSHFlag))return false;
 context[FlagsOffset]=static_cast<uint8_t>(context[FlagsOffset]&~POSHFlag);
 return true;
}

// Rewrite an LRC image whose page 1 holds the native RCS register layout.
// No write at all unless both register identities match.
inline uint32_t sanitize(uint8_t *image,uint64_t length) {
 if(!image || length<LRCAOffset+4)return Mismatch;
 auto word=[image](uint32_t offset)->uint32_t & {return *reinterpret_cast<uint32_t *>(image+offset);};
 if(word(ControlRegisterOffset)!=ControlRegister || word(LRCARegisterOffset)!=LRCARegister)return Mismatch;
 uint32_t result=0;
 if(word(LRCAOffset)) {word(LRCAOffset)=0;result|=ZeroedLRCA;}
 const uint32_t control=word(ControlOffset),fixed=(control&ControlKeep)|ControlMaskBit4;
 if(control&0x10U)result|=ClearedControl;
 if(fixed!=control) {word(ControlOffset)=fixed;result|=ChangedControl;}
 return result;
}
}
#endif
