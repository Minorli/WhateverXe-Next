// CPU-only contract for the opt-in synchronous-flip candidate.
// No MMIO, no GPU, no installed driver.
#include <cassert>
#include <cstdio>
#include "kern_tgl_plane_ctl.hpp"

int main() {
    using namespace TGLPlaneCtl;

    // Primary-plane CTL registers for pipes A/B/C only.
    const unsigned long primary[] {0x70180, 0x71180, 0x72180};
    for (auto reg : primary) assert(isPrimaryPlaneControl(reg));

    // Cursor control, sprite planes, the LPVP shadow at +0x500, plane data
    // registers, pipe D, and unrelated banks are not the primary plane control.
    const unsigned long other[] {
        0x70080, 0x70084, 0x70280, 0x70380, 0x70680, 0x7019c, 0x7027c, 0x70340,
        0x73080, 0x73180, 0x44408, 0x45140, 0xFFFF80};
    for (auto reg : other) assert(!isPrimaryPlaneControl(reg));

    // The donor's actual values observed during the UU capture.
    uint32_t asyncArm = 0x82000608;
    uint32_t asyncFlip = 0x82000600;
    uint32_t syncArm = 0x82000408;
    uint32_t syncFlip = 0x82000400;

    assert(forceSynchronous(asyncArm));
    assert(asyncArm == 0x82000408); // bit3 arm marker preserved
    assert(forceSynchronous(asyncFlip));
    assert(asyncFlip == 0x82000400);

    // Already-synchronous writes are not reported as conversions.
    assert(!forceSynchronous(syncArm));
    assert(!forceSynchronous(syncFlip));
    assert(syncArm == 0x82000408 && syncFlip == 0x82000400);

    // Non-flip control values are left untouched.
    uint32_t disabled = 0x00000400;
    assert(!forceSynchronous(disabled) && disabled == 0x00000400);
    uint32_t decomp = 0x80008600; // async + render decompression bit15
    assert(forceSynchronous(decomp));
    assert(decomp == 0x80008400);

    puts("PASS: primary-plane CTL identified, async bit9 cleared on the donor's "
         "observed values, arm marker and unrelated registers preserved");
    return 0;
}
