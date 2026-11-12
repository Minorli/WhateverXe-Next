#include "kern_xelp_panel_pwm.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>
using P=XELP::PanelPWM;
int main(){
 for(unsigned strap:{0U,256U})for(unsigned hz:{1U,200U,1000U,65535U})for(bool low:{false,true}) {
  const auto p=P::select(0,0,strap,hz,low);
  assert(p.valid() && p.period==((strap?24000000U:19200000U)+hz/2)/hz && p.polarity==(low?1U<<29:0));
 }
 for(auto period:{1U,65535U,65537U,96000U,120000U,24000000U,0xfffffffeU})for(bool low:{false,true}) {
  const auto p=P::select(period,low?1U<<29:0,~0U,0,!low);assert(p.period==period&&p.polarity==(low?1U<<29:0));
  uint32_t last=0;
  for(uint32_t brightness=0;brightness<=65535;++brightness){uint32_t duty=~0U;assert(p.duty(brightness,duty));
   assert(duty==uint64_t(period)*brightness/65535 && duty>=last && duty<=period);last=duty;}
  assert(last==period);assert(!p.duty(65536,last));
 }
 assert(!P::select(0,0,~0U,200,false).valid());assert(!P::select(0,0,0,0,false).valid());
 assert(!P::select(~0U,0,0,200,false).valid());assert(!P::select(1,~0U,0,200,false).valid());
 puts("PASS 917504 PWM brightness cases; BIOS/VBT period and polarity, rawclk, monotonic wide arithmetic and invalid inputs");
}
