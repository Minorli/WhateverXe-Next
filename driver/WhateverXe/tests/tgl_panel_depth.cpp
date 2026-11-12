#include "kern_tgl_panel_depth.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <initializer_list>
static void checksum(uint8_t *e) {e[127]=0;unsigned s=0;for(unsigned i=0;i<127;++i)s+=e[i];e[127]=-s;}
int main() {
 uint8_t e[128]={0,255,255,255,255,255,255,0};e[18]=1;e[19]=4;
 for(unsigned code=0;code<8;++code) {
  e[20]=0x85|(code<<4);checksum(e);
  const auto cap=TGLPanelDepth::maximum(e,128);
  assert(cap==(code && code<7 ? 4+2*code : 0));
  const unsigned depths[]={6,8,10,12,16};
  for(unsigned i=0;i<5;++i) {
   auto out=TGLPanelDepth::clamp(1,1U<<i,cap);
   unsigned expected=i;
   if(cap)while(expected && depths[expected]>cap)--expected;
   assert(out==(1U<<expected));
   assert(TGLPanelDepth::clamp(2,1U<<i,cap)==(1U<<i));
  }
 }
 e[20]=0x95;checksum(e);assert(TGLPanelDepth::maximum(e,128)==6);
 for(unsigned i=0;i<128;++i) {e[i]^=1;assert(!TGLPanelDepth::maximum(e,128));e[i]^=1;}
 assert(!TGLPanelDepth::maximum(e,127));assert(!TGLPanelDepth::maximum(nullptr,128));
 e[19]=3;checksum(e);assert(!TGLPanelDepth::maximum(e,128));
 e[19]=4;e[20]=0x15;checksum(e);assert(!TGLPanelDepth::maximum(e,128));
 e[20]=0x91;checksum(e);assert(!TGLPanelDepth::maximum(e,128));
 for(unsigned bits: {0U,3U,32U,65535U})assert(TGLPanelDepth::clamp(1,bits,6)==bits);
 puts("PASS: EDID checksum/version/interface/depth and RGB-only single-selection clamp");
}
