// SPDX-License-Identifier: BSD-3-Clause
// Pure helpers for the opt-in synchronous-flip candidate. The donor's
// configurePlane defaults PLANE_CTL to the async-address-update value
// (0x80000600) unless the flip transaction requests synchronous presentation.
// Async address updates are allowed to take effect mid-frame, which can produce
// a partial-frame tear. Clearing PLANE_CTL bit9 makes the same write a normal
// double-buffered sync flip; the donor's idle path already uses that form.
#ifndef kern_tgl_plane_ctl_hpp
#define kern_tgl_plane_ctl_hpp
#include <stdint.h>

namespace TGLPlaneCtl {
constexpr uint32_t AsyncFlipBit = 1U << 9;
constexpr uint32_t FirstPlaneCtl = 0x70180;
constexpr uint32_t EndPlaneCtl = 0x73000; // pipe D starts here and is excluded

// PLANE_CTL for the primary planes of pipes A/B/C: 0x70180/0x70280/0x70380,
// 0x71180/..., 0x72180/... The cursor planar control at 0x70080 is a
// different register and is never matched. Only the 0x100 plane stride is
// accepted, so unrelated registers in the same 0x7xx00 bank are excluded.
inline bool isPrimaryPlaneControl(unsigned long reg) {
    return reg >= FirstPlaneCtl && reg < EndPlaneCtl &&
        ((reg - FirstPlaneCtl) & 0xFFU) == 0;
}

// Returns true only when the requested write was an async flip and has been
// converted. Other bits, including the donor's bit3 arm marker, are untouched.
inline bool forceSynchronous(uint32_t &value) {
    if (!(value & AsyncFlipBit)) return false;
    value &= ~AsyncFlipBit;
    return true;
}
}
#endif
