#include "kern_tgl_hang_cpu.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <set>
#include <utility>
using namespace TGLHangCPU;
int main() {
 constexpr uint64_t base=0xffff800000000000ULL,owner=base+0x1000,
  scheduler=base+0x4000,streamer=base+0x8000,table=base+0xc000,
  item=base+0xe000,context=base+0x10000,ring=base+0x13000,stamps=base+0x18000;
 std::array<uint8_t,0x20000> memory{};
 auto put=[&](uint64_t p,uint64_t v,unsigned n=8) {memcpy(memory.data()+p-base,&v,n);};
 auto setup=[&](unsigned engine) {
  memory={};put(scheduler+0x10,owner);put(owner+0x1250,scheduler);
  put(owner+0x1190,0x1234000000018880ULL);
  put(scheduler+0xca0+engine*8,streamer);put(streamer+0x10,owner);
  put(streamer+0x20,scheduler);put(streamer+0x28,table+engine*0x79);
  put(table+engine*0x79,engine,1);put(streamer+0x1c04,11,4);
  put(streamer+0x30,item);put(item,context);put(context+0x50,owner);
  put(context+0x6c,engine,1);put(context+0x7c,1019,4);
  put(context+0x89,0x123456789abcULL);put(context+0xb0,ring);
  put(ring+0x10,owner);put(ring+0x20,context);put(ring+0x40,engine,1);
  put(ring+0x30,stamps);put(ring+0x38,25,4);put(stamps+25*64,817,4);
  put(ring+0x8a0,31,4);
 };
 unsigned reads=0;std::set<std::pair<uint64_t,unsigned>> addresses;
 auto read=[&](uint64_t p,void *out,unsigned n)->uint64_t {
  ++reads;assert(n<=8 && TGLHangPending::kernelRange(p,n));
  addresses.insert({p,n});
  if(p<base || p-base>memory.size() || n>memory.size()-(p-base))return 0;
  memcpy(out,memory.data()+p-base,n);return n;
 };
 for(unsigned engine=0;engine<Engines;++engine) {
  setup(engine);Capture c{};reads=0;addresses.clear();
  c.q[2]=10;c.q[8]=7;c.q[9]=engine+1;
  collect(c,owner,scheduler,table+engine*0x79,engine,read);
  assert(c.q[0]==1 && c.q[1]==0 && c.q[2]==10 && c.q[7]==engine && c.q[9]==engine+1);
  assert(c.q[10]==2 && !c.q[11] && reads<=130);
  for(auto &p:c.pass) {
   assert(p.streamer.value==streamer && p.ownerFlags.value==0x1234000000018880ULL);
   assert(p.slots[0].id.value==1019 && p.slots[0].descriptor.bytes==8);
   assert(p.slots[0].retired.value==817 && p.slots[0].retired.bytes==4);
   assert(p.slots[0].ringEngine.value==engine && !p.slots[1].context.bytes);
  }
  // Fault every distinct read, including short reads of otherwise valid data.
  const auto valid=addresses;
  for(auto failing:valid) {
   c={};
   auto shortRead=[&](uint64_t p,void *out,unsigned n)->uint64_t {
    if(p==failing.first)return n>1?read(p,out,n-1):0;
    return read(p,out,n);
   };
   collect(c,owner,scheduler,table+engine*0x79,engine,shortRead);
   assert(c.q[1]!=0 && c.q[10]==0);
  }
 }
 for(unsigned engine:{1U,2U}) {
  // CCS/BCS must use their own identity, never a plausible RCS slot.
  setup(engine);put(context+0x6c,0,1);Capture c{};
  collect(c,owner,scheduler,table+engine*0x79,engine,read);
  assert(c.q[1]==3 && !c.pass[0].slots[0].ring.bytes);
 }
 for(auto mismatch:std::array<uint64_t,4>{owner+0x1250,streamer+0x10,streamer+0x20,streamer+0x28}) {
  setup(1);put(mismatch,0);Capture c{};
  collect(c,owner,scheduler,table+0x79,1,read);assert(c.q[1]==2);
 }
 for(unsigned index:{64U,UINT32_MAX}) {
  setup(1);put(ring+0x38,index,4);Capture c{};
  collect(c,owner,scheduler,table+0x79,1,read);
  assert(c.q[1]==3 && !c.pass[0].slots[0].retired.bytes);
 }
 setup(1);Capture c{};unsigned copies=0;
 auto changes=[&](uint64_t p,void *out,unsigned n)->uint64_t {
  if(p==stamps+25*64 && ++copies==2)put(p,818,4);
  return read(p,out,n);
 };
 collect(c,owner,scheduler,table+0x79,1,changes);
 assert(c.q[1]==4 && c.q[10]==2 && c.q[11]==1);
 setup(1);c={};copies=0;
 auto identityChanges=[&](uint64_t p,void *out,unsigned n)->uint64_t {
  if(p==streamer+0x20 && ++copies==2)put(p,0);
  return read(p,out,n);
 };
 collect(c,owner,scheduler,table+0x79,1,identityChanges);
 assert(c.q[1]==2 && c.q[10]==1);
 for(uint64_t invalid:{0ULL,0x1000ULL,UINT64_MAX-4}) {
  c={};reads=0;collect(c,invalid,scheduler,table,0,read);assert(c.q[1]==1 && !reads);
  c={};reads=0;collect(c,owner,invalid,table,0,read);assert(c.q[1]==1 && !reads);
  c={};reads=0;collect(c,owner,scheduler,invalid,0,read);assert(c.q[1]==1 && !reads);
  setup(0);put(streamer+0x30,invalid);c={};
  collect(c,owner,scheduler,table,0,read);
  assert(invalid?c.q[1]==3:c.q[1]==0); // Empty slots are valid witnesses.
 }
 c={};reads=0;collect(c,owner,scheduler,table,6,read);assert(c.q[1]==1 && !reads);
 puts("PASS: six engines, nofault/short reads, ownership, bounds and changing witnesses; CPU only");
}
