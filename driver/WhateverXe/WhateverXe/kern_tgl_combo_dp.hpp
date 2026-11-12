// Fixed DP on TGL Combo A/B. Wiring comes from VBT; this adapter only
// connects existing Combo PHY/AUX/PLL and shared SST operations.
#ifndef KERN_TGL_COMBO_DP_HPP
#define KERN_TGL_COMBO_DP_HPP
#include "kern_vbt.hpp"
struct TGLComboDP {
 static constexpr uint32_t Mask=3;
 static uint32_t fromFirmware(const XELP::VBT::Ports &ports,uint32_t hdmi) {
  if(!ports.valid() || ports.unmapped)return 0;
  uint32_t selected=0;
  for(unsigned d=0;d<2;++d) {
   const auto bit=1U<<d;const auto &p=ports.port[d];
   // Linux VBT DP_AUX_A/B; native AUX is tied to the physical DDI.
   const auto aux=d?0x10U:0x40U;
   if(!(ports.present&bit) || (hdmi&bit) || XELP::VBT::ddi(XELP::Family::TigerLake,p.dvo)!=d ||
      (p.type&0x14)!=0x14 || (p.type&(0x1000|0x400|0x20|1)) ||
      (p.flags&0x17) || (p.capabilities&3) || p.aux!=aux)continue;
   bool shared=false;
   for(unsigned other=0;other<XELP::PortCount;++other)if(other!=d && (ports.present&(1U<<other)))
    shared|=ports.port[other].dp() && ports.port[other].aux==aux;
   if(!shared)selected|=bit;
  }
  return selected;
 }
 static bool bound(uint32_t selected,const void *platform,const uint32_t *config) {
  if((selected&~Mask) || !platform || !config || config[0]>=2 || !(selected&(1U<<config[0])))return false;
  const auto d=config[0];const uint32_t expected[]={d,d+1,1,0,0x400,0x201};
  return config==reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(platform)+0x20+24*d) &&
   !memcmp(config,expected,sizeof(expected));
 }
 static bool accepts(uint32_t selected,const void *platform,size_t length) {
  if((selected&~Mask) || !platform || length<0x1C0)return false;
  auto p=static_cast<const uint8_t *>(platform);if(p[0x12]!=9)return false;
  for(unsigned d=0;d<2;++d)if(selected&(1U<<d)) {
   const uint32_t native[]={d,d+1,1,0,0x400,0x201},panel[]={0,0,0,0,2,0x18};
   if(memcmp(p+0x20+24*d,native,sizeof(native)) && (d || memcmp(p+0x20,panel,sizeof(panel))))return false;
  }
  return true;
 }
 static bool apply(uint32_t selected,void *platform,size_t length) {
  if(!accepts(selected,platform,length))return false;
  auto p=static_cast<uint8_t *>(platform);
  for(unsigned d=0;d<2;++d)if(selected&(1U<<d)) {
   const uint32_t record[]={d,d+1,1,0,0x400,0x201};memcpy(p+0x20+24*d,record,sizeof(record));
  }
  return true;
 }
};
#endif
