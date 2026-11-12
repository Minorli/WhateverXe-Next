#!/usr/bin/env python3
"""Exercise production AUX parent wrappers with mixed ADL-S physical domains."""
from pathlib import Path
import re,subprocess,tempfile
r=Path(__file__).resolve().parents[1]
s=(r/'WhateverXe/kern_xe.cpp').read_text();h=(r/'WhateverXe/kern_xe.hpp').read_text()
fields=h[h.index('\tbool tglAUXParentReady'):h.index('\tstatic void wrapTGLEnableAUXPower')]
body='\n'.join(re.search(p,s,re.S).group() for p in [
    r'void XE::wrapTGLEnableAUXPower\(.*?\n\}',r'void XE::wrapTGLDisableAUXPower\(.*?\n\}'])
code=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <vector>
#include "kern_alderlake_s.hpp"
#include "kern_tgl_tc_boot.hpp"
struct IOService;
struct XE {
 bool adlsDisplaySelected=true;AlderLakeS::Wiring adlsWiring;
 FIELDS
 uintptr_t tglPortByDDI,tglEnablePG,tglDisablePG,orgTGLEnableAUXPower,orgTGLDisableAUXPower;
 static void wrapTGLEnableAUXPower(void*,uint32_t);
 static void wrapTGLDisableAUXPower(void*,uint32_t);
};
XE state,*callbackXE=&state;
#define FunctionCast(f,p) reinterpret_cast<decltype(&f)>(p)
template<class... T>void tglAUXRecord(T...){}
BODY
alignas(8) uint8_t power[0x90],other[0x90];
uint32_t &ref(void *p,unsigned off){return *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(p)+off);}
std::vector<unsigned> calls;
void *port(unsigned d){assert(d<9);return reinterpret_cast<void*>(uintptr_t(d+1));}
void getPG(void *p,unsigned level){assert(level==1||level==3);calls.push_back(10+level);for(unsigned i=1;i<=level;++i)++ref(p,0x1c+4*i);}
void putPG(void *p,unsigned level){assert(level==1||level==3);calls.push_back(20+level);for(unsigned i=1;i<=level;++i){assert(ref(p,0x1c+4*i));--ref(p,0x1c+4*i);}}
void getAUX(void *p,unsigned d){calls.push_back(1);assert(ref(p,0x20));if(d>=3)assert(ref(p,0x28));++ref(p,0x54+4*d);}
void putAUX(void *p,unsigned d){calls.push_back(2);if(ref(p,0x54+4*d))--ref(p,0x54+4*d);}
void reset(unsigned mask){
 state={};state.adlsWiring.valid=true;state.adlsWiring.dp=mask;state.tglAUXParentReady=true;
 state.tglPortByDDI=(uintptr_t)port;state.tglEnablePG=(uintptr_t)getPG;state.tglDisablePG=(uintptr_t)putPG;
 state.orgTGLEnableAUXPower=(uintptr_t)getAUX;state.orgTGLDisableAUXPower=(uintptr_t)putAUX;
 memset(power,0,sizeof(power));memset(other,0,sizeof(other));calls.clear();
}
void check(){
 const auto tc=state.tglAUXParentRefs[3]+state.tglAUXParentRefs[4];
 assert(ref(power,0x20)==tc+state.tglAUXParentRefs[0]);
 assert(ref(power,0x24)==tc&&ref(power,0x28)==tc);
}
int main(){
 unsigned cases=0;
 for(unsigned panel=0;panel<2;++panel)for(unsigned mask:{1U,8U,16U,9U,17U,24U,25U}) {
  std::vector<unsigned> order;for(unsigned d=0;d<5;++d)if(mask&(1U<<d))order.push_back(d);
  do {auto release=order;std::sort(release.begin(),release.end());do {
   reset(mask);
   if(panel && (mask&1)){state.adlsWiring.panel=1;state.adlsWiring.dp&=~1U;assert(!state.tglDPSSTEligible(0));}
   for(auto d:order){
    ref(power,0x54+4*d)=1; // inherited firmware ref
    for(unsigned n=0;n<2;++n){calls.clear();XE::wrapTGLEnableAUXPower(power,d);
     assert(calls==(std::vector<unsigned>{d?13U:11U,1}));check();}
   }
   for(auto d:release){
    // Cross-object release cannot spend the real owner's PG reference.
    ref(other,0x54+4*d)=1;calls.clear();XE::wrapTGLDisableAUXPower(other,d);
    assert(calls==std::vector<unsigned>{2});check();
    for(unsigned n=0;n<3;++n){calls.clear();XE::wrapTGLDisableAUXPower(power,d);
     assert(calls==(n?std::vector<unsigned>{2,d?23U:21U}:std::vector<unsigned>{2}));check();}
   }
   assert(!ref(power,0x20));++cases;
  }while(std::next_permutation(release.begin(),release.end()));}while(std::next_permutation(order.begin(),order.end()));
 }
 reset(0);assert(!state.tglAUXParentEligible(0));
 reset(1);state.adlsWiring.valid=false;assert(!state.tglAUXParentEligible(0));
 reset(1);state.adlsDisplaySelected=false;assert(!state.tglAUXParentEligible(0));
 reset(1);state.tglAUXParentReady=false;assert(!state.tglAUXParentEligible(0));
 reset(1);assert(!state.tglAUXParentEligible(9)&&!state.tglAUXParentEligible(~0U));
 printf("PASS %u mixed AUX A/TC parent orderings; nested/inherited refs; foreign owner; admission guards\n",cases);
}
'''.replace('FIELDS',fields).replace('BODY',body)
with tempfile.TemporaryDirectory(prefix='adls-aux-parent-') as d:
    d=Path(d);(d/'test.cpp').write_text(code)
    subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined','-I'+str(r/'WhateverXe'),str(d/'test.cpp'),'-o',str(d/'test')],check=True)
    subprocess.run([str(d/'test')],check=True)
