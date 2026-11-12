#include "kern_tgl_hang_bcs_wait.hpp"
#include <cassert>
#include <map>
#include <vector>
#include <cstdio>
using namespace TGLHangBCSWait;
namespace {
constexpr uint64_t K=0xffff800000000000ULL,Ring=K+0x200000,Ctx=Ring+0x1000,Task=Ring+0x2000,
 LRCBuf=Ring+0x3000,RingBuf=Ring+0x4000,LRC=Ring+0x5000,Storage=Ring+0x8000,
 TableObject=K+0x20000,Class=K+0xabcdef,Target=0x40000040,Value=K+0x30040;
struct Fixture {
 std::map<uint64_t,std::vector<uint8_t>> blocks;
 uint8_t regs[0xb0]{};
 bool mutateOwner{},mutateWords{},mutatePTE{},changeValue{},partial{};
 unsigned valueReads{};
 void alloc(uint64_t p,unsigned n){blocks[p].resize(n);}
 template<class T>void put(uint64_t p,T v) {
  auto it=blocks.upper_bound(p);assert(it!=blocks.begin());--it;
  assert(p-it->first+sizeof(v)<=it->second.size());memcpy(it->second.data()+p-it->first,&v,sizeof(v));
 }
 void reg(unsigned n,uint32_t v){memcpy(regs+n,&v,4);}
 void packet(unsigned offset=16,uint32_t header=0x0e009002) {
  const uint32_t words[]={header,11,uint32_t(Target),0};
  for(unsigned i=0;i<4;++i)put(Storage+((offset+4*i)&4095),words[i]);
 }
 Fixture() {
  for(auto p:{Ring,Ctx,Task,LRCBuf,RingBuf,Storage,TableObject,K+0x1000,K+0x2000,
              K+0x7000,K+0xc000,K+0x11000,K+0x12000,K+0x30000})alloc(p,4096);
  alloc(LRC,8192);for(auto p:{K+0x3000,K+0x8000,K+0xd000})alloc(p,16384);
  put(TableObject,Class);put(TableObject+0x40,K+0x1000);put(TableObject+0x48,K+0x3000);
  put(K+0x1000+0x18,uint64_t(0x2000));put(K+0x3000,K+0x7000);put(K+0x3008,K+0x8000);
  put(K+0x8000+32,K+0xc000);put(K+0x8000+40,K+0xd000);put(K+0xd000,K+0x11000);
  put(K+0x11000+0x20,K+0x12000);put(K+0x12000,uint64_t(0x30003));
  put(Ring+0x40,uint8_t(2));put(Ctx+0x6c,uint8_t(2));put(Ring+0x20,Ctx);
  put(Ring+0x10,K+0x99000);put(Ctx+0x50,K+0x99000);put(Ctx+0xb0,Ring);
  put(Ctx+0x58,Task);put(Task+0x260,TableObject);put(Ctx+0x98,LRCBuf);
  put(LRCBuf+0x38,LRC);put(LRCBuf+0x20,uint64_t(8192));put(Ring+0x18,LRC);
  put(Ring+0x80,RingBuf);put(RingBuf+0x38,Storage);put(RingBuf+0x20,uint64_t(4096));
  put(Ring+0x8c,uint32_t(4096));put(Ctx+0x7c,uint32_t(8));put(LRC+0x10cc,uint32_t(0x2000));
  regs[0x94]=2;reg(0,20);reg(0xac,8<<5);packet();put(Value,uint32_t(10));
 }
 uint64_t read(uint64_t p,void *out,unsigned n) {
  if(p==Value && n==4) {
   ++valueReads;
   if(mutateOwner)put(Ctx+0x50,K+0x99008);
   if(mutateWords)put(Storage+16,uint32_t(0));
   if(mutatePTE)put(K+0x12000,uint64_t(0x31003));
   if(changeValue && valueReads==2)put(Value,uint32_t(11));
  }
  auto it=blocks.upper_bound(p);if(it==blocks.begin())return 0;--it;
  if(p-it->first+n>it->second.size())return 0;
  if(partial && p==Value)return 0;
  memcpy(out,it->second.data()+p-it->first,n);return n;
 }
 Capture run(uint64_t directEnd=K+0x1000000) {
  Capture c{};collect(c,Ring,regs,Class,K,directEnd,
   [&](uint64_t p,void *out,unsigned n){return read(p,out,n);});return c;
 }
};
}
int main() {
 {Fixture f;auto c=f.run();assert(!c.q[1] && c.q[9]==1 && !c.candidates[0].q[4]);
  assert(c.candidates[0].q[3]==Target && c.candidates[0].q[5]==10 && !c.candidates[0].q[7]);}
 {Fixture f;f.changeValue=true;auto c=f.run();assert(!c.q[1] && !c.candidates[0].q[4]);
  assert(c.candidates[0].q[5]==10 && c.candidates[0].q[6]==11 && c.candidates[0].q[8]);}
 {Fixture f;f.put(Storage+16,uint32_t(0));f.packet(4092);f.reg(0,0);auto c=f.run();
  assert(!c.q[1] && c.q[9]==1 && c.candidates[0].q[0]==4092 && !c.candidates[0].q[4]);}
 for(unsigned mode=0;mode<7;++mode) {
  Fixture f;
  if(mode==0)f.regs[0x94]=0;
  if(mode==1)f.put(Ring+0x40,uint8_t(0));
  if(mode==2)f.put(Ctx+0xb0,Ring+8);
  if(mode==3)f.put(TableObject,Class+8);
  if(mode==4)f.put(LRCBuf+0x38,LRC+4);
  if(mode==5)f.put(Ring+0x8c,uint32_t(32769));
  if(mode==6)f.reg(0xac,9<<5);
  assert(f.run().q[1] && !f.valueReads);
 }
 for(uint32_t header:{0x0e409002U,0x0e00c002U,0x0e009003U}) {
  Fixture f;f.packet(16,header);auto c=f.run();assert(!c.q[1] && !c.q[9] && !f.valueReads);
 }
 {Fixture f;f.put(Storage+24,uint32_t(Target|1));auto c=f.run();assert(c.candidates[0].q[4]==1 && !f.valueReads);}
 {Fixture f;f.put(LRC+0x10cc,uint32_t(0x4000));auto c=f.run();assert(c.candidates[0].q[4]==2 && !f.valueReads);}
 {Fixture f;f.put(K+0x12000,uint64_t(0));auto c=f.run();assert(c.candidates[0].q[4]==2 && !f.valueReads);}
 {Fixture f;auto c=f.run(Value);assert(c.candidates[0].q[4]==3 && !f.valueReads);}
 {Fixture f;f.partial=true;assert(f.run().candidates[0].q[4]==3);}
 {Fixture f;f.mutatePTE=true;assert(f.run().candidates[0].q[4]==4);}
 {Fixture f;f.mutateOwner=true;assert(f.run().q[1]==4);}
 {Fixture f;f.mutateWords=true;assert(f.run().q[1]==4);}
 printf("PASS: BCS wait CPU witnesses, wrapping, changing values and rejected identities/mappings/races; no GPU work\n");
}
