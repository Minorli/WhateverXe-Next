#include "kern_tgl_combo_signal.hpp"
#include <cassert>
#include <map>
#include <vector>
#include <cstdio>
int main() {
 for(unsigned ddi=0;ddi<3;++ddi)for(unsigned lanes:{0,1,2,3,4})
 for(unsigned rate:{6,10,20,30})for(bool low:{false,true})for(bool hobl:{false,true})
 for(unsigned vs=0;vs<5;++vs)for(unsigned pre=0;pre<5;++pre) {
  TGLComboSignal s;
  bool valid=ddi<2&&(lanes==1||lanes==2||lanes==4)&&
      (rate==6||rate==10||rate==20||rate==30)&&vs<4&&pre<4&&vs+pre<=3;
  assert(s.select(ddi,lanes,rate,low,hobl,vs,pre)==valid);
  if(!valid)continue;
  std::map<unsigned,unsigned> regs;
  std::vector<std::pair<unsigned,unsigned>> writes;
  auto read=[&](unsigned reg){return regs[reg];};
  auto write=[&](unsigned reg,unsigned value){
   assert(reg>=s.base&&reg<s.base+0xBA0);
   // Group DW5/PCS writes affect main lanes, never the AUX lane.
   if(reg==s.base+0x694)for(unsigned n=0;n<4;++n)regs[s.base+0x894+0x100*n]=value;
   regs[reg]=value;writes.emplace_back(reg,value);
  };
  // Unrelated lane bits and loadgen survive coefficient programming.
  for(unsigned n=0;n<4;++n)regs[s.base+0x890+0x100*n]=0x12340000;
  assert(s.program(read,write));
  assert(writes.front().first==s.base+0x604);
  assert(writes[6].first==s.base+0x694 && !(writes[6].second&0x80000000));
  assert(writes[writes.size()-2].first==s.base+0x694 && (writes[writes.size()-2].second&0x80000000));
  for(unsigned n=0;n<4;++n) {
   auto value=regs[s.base+0x890+0x100*n];
   assert((value&0x7FFC0000)==0x12340000);
   assert(bool(value&0x80000000)==(rate!=30&&(n==1||n==2||(lanes==4&&n==3))));
  }
  // Missing bank declines before the first write.
  writes.clear();regs[s.base+0xA9C]=~0U;assert(!s.program(read,write)&&writes.empty());
 }
 for(unsigned ddi:{0u,3u,4u})for(unsigned lanes:{0u,1u,2u,3u,4u})for(unsigned rate:{0u,6u,10u,12u,20u,30u})
 for(unsigned v=0;v<5;++v)for(unsigned e=0;e<5;++e) {
  TGLComboSignal s;bool ok=(lanes==1||lanes==2||lanes==4)&&(rate==6||rate==10||rate==20||rate==30)&&v<4&&e<4&&v+e<=3;
  assert(s.selectADLS(ddi,lanes,rate,v,e)==ok);if(!ok)continue;
  AlderLakeS::Port route;assert(AlderLakeS::Port::resolve(ddi,route));
  assert(s.base==route.phyBase()&&!s.low);
  std::map<unsigned,unsigned> regs;
  // HBR3 must clear even inherited loadgen on every lane (Linux >600 MHz).
  for(unsigned n=0;n<4;++n)regs[s.base+0x890+0x100*n]=0x80000000U;
  assert(s.program([&](unsigned r){return regs[r];},[&](unsigned r,unsigned value){
   assert(r>=s.base&&r<s.base+0xBA0);
   if(r==s.base+0x694)for(unsigned n=0;n<4;++n)regs[s.base+0x894+0x100*n]=value;
   regs[r]=value;
  }));
  for(unsigned n=0;n<4;++n)assert(bool(regs[s.base+0x890+0x100*n]&0x80000000U)==
    (rate!=30 && (n==1 || n==2 || (lanes==4 && n==3))));
 }
 // A PHY that drops only the loadgen clear must not report success.
 {
  TGLComboSignal hbr3;assert(hbr3.selectADLS(3,4,30,0,0));
  std::map<unsigned,unsigned> regs;
  assert(!hbr3.program([&](unsigned r){return regs[r];},[&](unsigned r,unsigned value){
   if(r==hbr3.base+0x694)for(unsigned n=0;n<4;++n)regs[hbr3.base+0x894+0x100*n]=value;
   for(unsigned n=0;n<4;++n)if(r==hbr3.base+0x890+0x100*n)value|=0x80000000U;
   regs[r]=value;
  }));
 }
 TGLComboSignal s;assert(s.select(0,1,10,false,false,0,0));
 assert(s.swing==0xA&&s.scalar==0x32&&s.cursor==0x3F&&!s.post1);
 // Silent dropped coefficient write must not report successful programming.
 std::map<unsigned,unsigned> regs;
 assert(!s.program([&](unsigned r){return regs[r];},[&](unsigned r,unsigned v){regs[r]=v;}));
 puts("PASS combo PHY tables, rate guards, ordering, lane masks, bank bounds and readback failure");
}
