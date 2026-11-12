#include "kern_xelp_panel_power.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>
using P=XELP::PanelPower;
int main() {
 XELP::VBT::Ports v;v.status=XELP::VBT::Status::Valid;v.present=1;
 v.port[0]={0x78C6,10,0,0x40,0,0};
 auto &panel=v.panel;panel.valid=true;panel.ddi=0;
 panel.backlightValid=true;panel.pwmPresent=true;panel.backlightType=2;
 assert(P::nativePPS0(v,XELP::Family::TigerLake));
 for(unsigned controller=0;controller<16;++controller)for(unsigned type=0;type<16;++type) {
  panel.controller=controller;panel.backlightType=type;
  assert(P::nativePPS0(v,XELP::Family::TigerLake)==(!controller && type==2));
 }
 panel.controller=0;panel.backlightType=2;
 assert(P::nativePPS0(v,XELP::Family::AlderLakeS));
 assert(P::nativePPS0(v,XELP::Family::RaptorLakeS));
 v.present|=2;v.port[1]=v.port[0];assert(!P::nativePPS0(v,XELP::Family::TigerLake));v.present=1;
 const uint32_t zero[5]={};
 auto plan=P::select(panel,zero,0,0,0);
 const uint32_t defaults[]={500,500,2100,5000,7};
 assert(plan.valid && !memcmp(plan.fields,defaults,sizeof(defaults)));
 // Each source wins independently; rounding never shortens T12. BIOS cycle
 // readback is already encoded, so repeated power calls cannot grow it.
 unsigned cases=0;
 for(unsigned i=0;i<5;++i)for(unsigned n: {0U,1U,999U,1000U,1001U,6000U,8191U,8192U,30000U,30001U,65535U}) {
  auto p=panel;p.delays[i]=n;
  const auto q=P::select(p,zero,0,0,0);
  const unsigned index[]={2,0,1,3,4};
  const auto expected=i==4 ? (n+1000+999)/1000 : n;
  assert(q.valid==(i==4 ? expected<=31 : n<=8191));
  if(q.valid) {
   if(n)assert(q.fields[index[i]]==expected);
   uint32_t on=(q.fields[2]<<16)|q.fields[0],off=(q.fields[3]<<16)|q.fields[1];
   auto again=P::select(p,zero,on,off,q.fields[4]<<4);
   assert(again.valid && !memcmp(q.fields,again.fields,sizeof(q.fields)));
  }
  ++cases;
 }
 uint32_t native[]={3000,2000,7000,8000,20};
 panel.delays[0]=7100;panel.delays[1]=4000;panel.delays[2]=1;panel.delays[3]=2;panel.delays[4]=5000;
 plan=P::select(panel,native,(6000U<<16)|100,100U<<16|2500,12<<4);
 const uint32_t mixed[]={4000,2500,7100,8000,20};
 assert(plan.valid && !memcmp(plan.fields,mixed,sizeof(mixed)));
 for(unsigned i=0;i<5;++i) {auto saved=native[i];native[i]=i==4?32:8192;assert(!P::select(panel,native,0,0,0).valid);native[i]=saved;}
 assert(!P::select(panel,native,~0U,0,0).valid);
 assert(!P::select(panel,native,0,~0U,0).valid);
 assert(!P::select(panel,native,0,0,~0U).valid);
 // An explicit property remains effective even if the donor's T8 is absent.
 native[0]=0;plan=P::select(panel,native,0,0,0);assert(plan.valid && plan.fields[3]==8000);
 printf("PASS panel controller/method qualification; %u delay boundaries, source merging, overflow and idempotence\n",cases);
}
