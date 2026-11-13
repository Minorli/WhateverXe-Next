// CPU contract for -wex-tgl-no-posh (kern_tgl_noposh.hpp). GPU behaviour is
// not established here; see the TGLNoPOSHLive counters on hardware.
#include "kern_tgl_noposh.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace TGLNoPOSH;
static uint32_t &dw(std::vector<uint8_t> &v,uint32_t o) {return *reinterpret_cast<uint32_t *>(v.data()+o);}
static std::vector<uint8_t> image(uint32_t control,uint32_t lrca) {
 std::vector<uint8_t> v(0x14000,0x5A);
 dw(v,ControlRegisterOffset)=ControlRegister;dw(v,ControlOffset)=control;
 dw(v,LRCARegisterOffset)=LRCARegister;dw(v,LRCAOffset)=lrca;
 return v;
}
int main() {
 // Flag: RCS only, POSH bit only, idempotent.
 uint8_t ctx[0x100] {};ctx[FlagsOffset]=0x1C;
 assert(clearFlag(ctx) && ctx[FlagsOffset]==0x0C);
 assert(!clearFlag(ctx) && ctx[FlagsOffset]==0x0C);
 ctx[FlagsOffset]=0x1C;ctx[EngineOffset]=1;assert(!clearFlag(ctx) && ctx[FlagsOffset]==0x1C);
 assert(!clearFlag(nullptr));
 // Safe image cloned from a POSH startup context (native fresh branch: 0xFFFF0001 | 0x100010).
 auto v=image(0xFFFF0011,0x4123A000);auto before=v;
 auto r=sanitize(v.data(),v.size());
 assert(r==(ZeroedLRCA|ClearedControl|ChangedControl));
 assert(dw(v,LRCAOffset)==0 && dw(v,ControlOffset)==0xFFFF0001);
 for(size_t i=0;i<v.size();++i) if((i<ControlOffset||i>=ControlOffset+4)&&(i<LRCAOffset||i>=LRCAOffset+4)) assert(v[i]==before[i]);
 // Identical to the native non-POSH clone fix-up at 0x7C169..0x7C17A for every control value.
 auto w=image(0,0);
 for(uint64_t c=0;c<=0xFFFFFFFFULL;c+=0x10001ULL) {
  dw(w,ControlOffset)=static_cast<uint32_t>(c);sanitize(w.data(),w.size());
  assert(dw(w,ControlOffset)==((static_cast<uint32_t>(c)&0xFFEFFFEFU)|0x100000U));
 }
 // Idempotent; a clean image is untouched except the control mask bit.
 r=sanitize(v.data(),v.size());assert(r==0);
 // Layout mismatch writes nothing.
 auto m=image(0xFFFF0011,0x1000);dw(m,LRCARegisterOffset)=0x21C0;auto mb=m;
 assert(sanitize(m.data(),m.size())==Mismatch && m==mb);
 assert(sanitize(v.data(),LRCAOffset+3)==Mismatch && sanitize(nullptr,0x14000)==Mismatch);
 puts("tgl_noposh: flag, LRC fix-up, idempotence and mismatch paths ok");
 return 0;
}
