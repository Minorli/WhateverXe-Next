#include "kern_tgl_port_map.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>

int main() {
 for (uint32_t ref=0; ref<8; ++ref)
  for (auto reg : {0x164000UL,0x164284UL,0x164288UL,0x16428CUL,0x164290UL,0x164200UL})
   for (uint32_t fraction=0; fraction<32768; ++fraction) {
    const uint32_t original=0xA00001A5U | (fraction << 10);
    uint32_t value=original;
    const bool qualified=ref==2 && (reg==0x164284 || reg==0x16428C);
    assert(TGLComboPLLFraction::adjust(ref,reg,value)==qualified);
    if (!qualified) assert(value==original);
    else {
     assert((value & ~(0x7FFFU<<10))==(original & ~(0x7FFFU<<10)));
     assert(((value>>10)&0x7FFF)==(fraction+1)/2);
    }
   }
 uint32_t value=0x01C001A5;
 assert(TGLComboPLLFraction::adjust(2,0x164284,value) && value==0x00E001A5);
 puts("PASS: combo PLL reference/target guards, every fraction, rounding, unrelated-bit preservation");
}
