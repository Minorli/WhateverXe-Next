#!/usr/bin/env python3
"""Verify legacy preservation and unmodified DP-alt clock-gating calls."""
from pathlib import Path
import re,subprocess,tempfile
r=Path(__file__).resolve().parents[1];s=(r/'WhateverXe/kern_xe.cpp').read_text()
fn=re.search(r'void XE::wrapTGLPhyClockGating\(.*?\n\}',s,re.S).group()
f=r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#define FunctionCast(f,p) reinterpret_cast<decltype(&f)>(p)
bool tglQuietTraining(){return true;}
struct IORegistryEntry{static IORegistryEntry*getRegistryRoot(){static IORegistryEntry r;return &r;}template<class...T>void setProperty(T...){}};
struct XE {
 bool tglPhyClockPreserveReady=true,tglDekelReady=true;uintptr_t orgTGLPhyClockGating;
 bool tglLegacyTCEligible(unsigned ddi){return ddi>=3&&ddi<=6;}
 static bool tglDekelAccess(void*,unsigned,bool,uint32_t&);
 static void wrapTGLPhyClockGating(void*,bool);
} instance,*callbackXE=&instance;
unsigned calls=0,reads=0,writes=0;uint32_t regs[2];bool seenGate;
bool XE::tglDekelAccess(void*,unsigned i,bool w,uint32_t&v){assert(i==0x48||i==0x49);if(w){++writes;regs[i-0x48]=v;}else{++reads;v=regs[i-0x48];}return true;}
void native(void*,bool gate){++calls;seenGate=gate;for(auto &v:regs)v=gate?v|0x3e:v&~0x3e;}
// PRODUCTION
int main(){
 alignas(8)uint8_t port[0x600]{};uint32_t cfg[]={3,0,1,1};*reinterpret_cast<uint32_t**>(port+0x548)=cfg;
 auto &mode=*reinterpret_cast<uint32_t*>(port+0x550),&lanes=*reinterpret_cast<uint32_t*>(port+0x124);
 instance.orgTGLPhyClockGating=reinterpret_cast<uintptr_t>(native);
 for(unsigned d=3;d<=6;++d)for(unsigned m:{3u,4u})for(unsigned n:{1u,2u,4u})for(bool gate:{false,true}){
  cfg[0]=d;mode=m;lanes=n;calls=reads=writes=0;regs[0]=regs[1]=0x123456be;
  XE::wrapTGLPhyClockGating(port,gate);
  if(m==3){assert(calls==1&&seenGate==gate&&!reads&&!writes);}
  else if(!gate){assert(!calls&&!writes&&regs[0]==0x123456be&&regs[1]==0x123456be);}
  else assert(calls==1&&seenGate&&!writes);
 }
 puts("PASS production PHY clock gating: native DP-alt calls unchanged, legacy preservation retained across TC1..4 and 1/2/4 lanes");
}
'''.replace('// PRODUCTION',fn)
with tempfile.TemporaryDirectory(prefix='tgl-phy-gate-') as d:
 d=Path(d);(d/'test.cpp').write_text(f)
 subprocess.run(['clang++','-std=c++14','-fsanitize=address,undefined',str(d/'test.cpp'),'-o',str(d/'test')],check=True)
 subprocess.run([str(d/'test')],check=True)
