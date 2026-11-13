// CPU contract for -wex-tgl-nomc (kern_tgl_nomc.hpp): the two exact donor
// sites, what each replacement changes, and the all-or-nothing state check.
#include "kern_tgl_nomc.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace TGLNoMC;
static_assert(Sites[0].offset==0x27499 && Sites[0].length==16 && Sites[1].offset==0x4BF0A && Sites[1].length==14,"donor sites");
static_assert(Sites[0].offset+Sites[0].length<=Sites[1].offset && End==Sites[1].offset+Sites[1].length,"ordered, End covers both");
int main() {
 // populateAccelConfig: `and eax,ebx` (21 D8) -> `xor eax,eax` (31 C0) ahead of
 // `shl rax,0x21`, so the EnableMultiContext bit (33) is always stored as 0.
 assert(Sites[0].find[0]==0x21 && Sites[0].find[1]==0xD8 && Sites[0].replace[0]==0x31 && Sites[0].replace[1]==0xC0);
 assert(Sites[0].find[2]==0x48 && Sites[0].find[3]==0xC1 && Sites[0].find[4]==0xE0 && Sites[0].find[5]==0x21);
 assert(!memcmp(Sites[0].find+2,Sites[0].replace+2,Sites[0].length-2));
 // newUnifiedAddressRangeAllocator: `test byte [rax+0x1194],2` is bit 33 of the
 // accel+0x1190 word; only its `je +0x26` (74 26) becomes two NOPs.
 uint32_t displacement=0;memcpy(&displacement,Sites[1].find+2,sizeof(displacement));
 assert(Sites[1].find[0]==0xF6 && Sites[1].find[1]==0x80 && displacement==0x1194 && Sites[1].find[6]==2);
 static_assert((0x1194-0x1190)*8+1==33,"byte 0x1194 bit 1 is accel+0x1190 bit 33");
 assert(Sites[1].find[7]==0x74 && Sites[1].find[8]==0x26 && Sites[1].replace[7]==0x90 && Sites[1].replace[8]==0x90);
 assert(!memcmp(Sites[1].find,Sites[1].replace,7) && !memcmp(Sites[1].find+9,Sites[1].replace+9,Sites[1].length-9));
 // State on a synthetic image: only all-native or all-patched is recognised.
 std::vector<uint8_t> image(End+16,0xCC);
 assert(state(nullptr,image.size())==Unknown && state(image.data(),End-1)==Unknown);
 assert(state(image.data(),image.size())==Unknown);
 for(const auto &s:Sites)memcpy(image.data()+s.offset,s.find,s.length);
 assert(state(image.data(),image.size())==Original && state(image.data(),End)==Original);
 memcpy(image.data()+Sites[0].offset,Sites[0].replace,Sites[0].length);
 assert(state(image.data(),image.size())==Unknown);  // one site written: neither state
 memcpy(image.data()+Sites[1].offset,Sites[1].replace,Sites[1].length);
 assert(state(image.data(),image.size())==Patched);
 image[Sites[1].offset+12]^=1;
 assert(state(image.data(),image.size())==Unknown);  // a foreign edit inside a patched site
 // Exercise the production transaction, including a write that reports failure.
 for(unsigned fail=0;fail<4;++fail) {
  for(const auto &site:Sites)memcpy(image.data()+site.offset,site.find,site.length);
  unsigned writes=0;
  auto result=install(image.data(),image.size(),[&](const Site &site,bool undo) {
   if(undo) {
    if(fail==3)return false; // rollback failure must remain visible
    memcpy(image.data()+site.offset,site.find,site.length);return true;
   }
   ++writes;
   if(fail==1 && writes==2)return false; // second site never written
   memcpy(image.data()+site.offset,site.replace,site.length);
   return !(writes==2 && fail>=2); // second site written, reported failure
  });
  if(fail==0)assert(result==InstallResult::Installed && state(image.data(),image.size())==Patched);
  else if(fail==3)assert(result==InstallResult::Unsafe);
  else assert(result==InstallResult::Unchanged && state(image.data(),image.size())==Original);
 }
 // A foreign initial image is never touched by the transaction.
 image[Sites[0].offset+3]^=1;
 unsigned writes=0;
 assert(install(image.data(),image.size(),[&](const Site &,bool){++writes;return true;})==InstallResult::Unchanged);
 assert(writes==0);
 puts("tgl_nomc: donor sites, replacement semantics and all-or-nothing state ok");
 return 0;
}
