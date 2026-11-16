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

// PLANE_CTL of the primary plane for pipes A/B/C: 0x70180, 0x71180, 0x72180.
// Only offset 0x180 within each 4 KiB pipe bank is accepted. This excludes the
// cursor control at 0x70080, the sprite planes at 0x70280/0x70380, the LPVP
// shadow register at 0x70680, and pipe D at 0x73180.
inline bool isPrimaryPlaneControl(unsigned long reg) {
    return reg >= FirstPlaneCtl && reg < EndPlaneCtl && (reg & 0xFFFU) == 0x180;
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
