#include "kern_tgl_combo_dp.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
int main() {
 for(unsigned mask=0;mask<4;++mask)for(unsigned hdmi=0;hdmi<4;++hdmi) {
  XELP::VBT::Ports v;v.status=XELP::VBT::Status::Valid;v.present=mask;
  v.port[0]={0x60d6,10,0,0x40,0xc0,0};v.port[1]={0x60d6,7,0,0x10,0xc0,0};
  const auto selected=TGLComboDP::fromFirmware(v,hdmi);assert(selected==(mask&~hdmi));
  alignas(8) uint8_t p[0x1c0]{};p[0x12]=9;
  const uint32_t panel[]={0,0,0,0,2,0x18},dp[]={1,2,1,0,0x400,0x201};
  memcpy(p+0x20,panel,24);memcpy(p+0x38,dp,24);
  uint8_t before[sizeof(p)];memcpy(before,p,sizeof(p));
  assert(TGLComboDP::apply(selected,p,sizeof(p)));
  for(unsigned d=0;d<9;++d) {
   auto config=reinterpret_cast<uint32_t *>(p+0x20+24*d);
   if(selected&(1U<<d)) {
    assert(TGLComboDP::bound(selected,p,config));
    uint32_t copy[6];memcpy(copy,config,24);assert(!TGLComboDP::bound(selected,p,copy));
   } else assert(!memcmp(p+0x20+24*d,before+0x20+24*d,24));
  }
  // Every selected predecessor must qualify before any mutation.
  memcpy(p,before,sizeof(p));p[0x38+16]^=1;memcpy(before,p,sizeof(p));
  assert(!TGLComboDP::apply(3,p,sizeof(p)) && !memcmp(before,p,sizeof(p)));
 }
 for(unsigned d=0;d<2;++d)for(unsigned fault=0;fault<12;++fault) {
  XELP::VBT::Ports v;v.status=XELP::VBT::Status::Valid;v.present=1U<<d;
  auto &p=v.port[d];p={0x60d6,d?7u:10u,0,d?0x10u:0x40u,0xc0,0};
  if(fault==1)v.status=XELP::VBT::Status::Conflict;
  if(fault==2)p.type|=0x1000;
  if(fault==3)p.flags|=1;
  if(fault==4)p.capabilities|=1;
  if(fault==5)p.capabilities|=2;
  if(fault==6)p.aux=0x50;
  if(fault==7)p.dvo=8;
  if(fault==8){v.present|=1U<<4;v.port[4]=p;}
  if(fault==9)p.type=0x60d2;
  if(fault==10)v.present=0;
  if(fault==11)v.unmapped=1;
  assert(TGLComboDP::fromFirmware(v,0)==(fault?0u:1U<<d));
 }
 puts("PASS fixed Combo DP: firmware routing, protocol/transport exclusions, exact binding and atomic native records");
}
