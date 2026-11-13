#include "kern_tgl_csb_failure.hpp"
#include <array>
#include <cstdio>
#include <cstring>
using namespace TGLCSBFailure;
int main() {
 constexpr uint64_t base=0xffff800000000000ULL;
 constexpr uint64_t streamer=base+0x1000,owner=base+0x4000,hwsp=base+0x6000;
 std::array<uint8_t,0x8000> memory{};
 auto store=[&](uint64_t p,uint64_t v) {memcpy(memory.data()+p-base,&v,8);};
 store(streamer+0x10,owner);store(streamer+0x68,hwsp);
 const uint64_t flags=0x1234000000018880ULL;
 store(owner+0x1190,flags);store(hwsp+0xBC,11);
 unsigned reads=0;bool truncated=false;
 auto read=[&](uint64_t p,void *out,unsigned n)->uint64_t {
  ++reads;
  if(p<base || p-base>=memory.size() || n>memory.size()-(p-base))return 0;
  if(truncated && p==owner+0x1190)n=1;
  memcpy(out,memory.data()+p-base,n);return n;
 };
 Engine e{};e.q[0]=streamer;collect(e,read);
 if(e.q[25]!=8 || e.q[24]!=flags || (e.q[24]&0x18000)!=0x18000) {
  fprintf(stderr,"FAIL: owner flags truncated: bytes=%llu flags=%llx\n",
   (unsigned long long)e.q[25],(unsigned long long)e.q[24]);return 1;
 }
 if(e.q[10]!=0x80 || e.q[13]!=0xC0 || e.q[27]!=4 || e.q[26]!=11)return 2;
 e={};e.q[0]=streamer;truncated=true;collect(e,read);
 if(e.q[25]!=1)return 3; // A partial read stays explicitly incomplete.
 for(uint64_t invalid:{0ULL,0x1000ULL,UINT64_MAX-4}) {
  e={};e.q[0]=invalid;reads=0;collect(e,read);
  if(reads || e.q[10] || e.q[25])return 4;
 }
 e={};e.q[0]=streamer;store(streamer+0x10,UINT64_MAX-4);truncated=false;
 collect(e,read);if(e.q[25])return 5;
 static_assert(sizeof(Engine)==1456,"existing failure wire layout");
 static_assert(sizeof(Capture)==8752,"six bounded engines");
 puts("PASS: full owner flags, partial reads and invalid ranges; CPU only");
}
