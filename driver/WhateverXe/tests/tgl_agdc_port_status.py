#!/usr/bin/env python3
"""Production AGDC stale status policy across physical ports and wiring sources."""
from pathlib import Path
import re,subprocess,tempfile,sys,hashlib
r=Path(__file__).resolve().parents[1];s=(r/'WhateverXe/kern_xe.cpp').read_text()
if len(sys.argv)>1:
 b=Path(sys.argv[1]).read_bytes()
 assert hashlib.sha256(b).hexdigest()=='c004fc1ac9ee4157e2e12736f40fa6639bde025a279326beeb9dd48c9b445731'
 # Exact native GetPortStatus: lookup(id-1), state>1, write output+8.
 for offset,expected in [(0x6a9fa,'8b3effcfe89907fbff'),(0x6aa11,'83b854050000010f9fc1'),(0x6aa22,'894b08')]:
  assert b[offset:offset+len(bytes.fromhex(expected))]==bytes.fromhex(expected)
body=re.search(r'uint32_t XE::wrapTGLAGDCPortStatus\(.*?\n\}',s,re.S).group()
code=r'''
#include "kern_vbt.hpp"
#include "kern_tgl_tc_boot.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
struct XE {
 uintptr_t orgTGLAGDCPortStatus{},tglPortByIndex{};bool tglInactivePorts=true,adlsDisplaySelected=false;
 bool tglTCWiringLoaded=true,tglTCWiringExplicit=false;
 TGLTCWiring tglTCWiring=TGLTCWiring::fromMasks(0,0,0);XELP::VBT::Ports firmwarePorts;
 struct {XELP::Family family=XELP::Family::TigerLake;}xeProfile;
 void *tglObservationController{};alignas(8) uint8_t tglPlatformCopy[0x200]{};
 static uint32_t wrapTGLAGDCPortStatus(void*,void*,void*);
} state,*callbackXE=&state;
#define FunctionCast(f,p) reinterpret_cast<decltype(&f)>(p)
struct IORegistryEntry {static IORegistryEntry *getRegistryRoot(){static IORegistryEntry e;return &e;}
 template<class... A>void setProperty(A...){} };
template<class... A>void tglTrace(A...){}
void clock_get_uptime(uint64_t *v){*v=1;}void absolutetime_to_nanoseconds(uint64_t v,uint64_t *n){*n=v;}
alignas(8) uint8_t ports[9][0x600]{},ctl[0xD50]{};
unsigned calls=0,lookups=0,nativeStatus=1,nativeResult=0;
void *get(unsigned index){assert(index<9);++lookups;return ports[index];}
uint32_t native(void*,void*,void *out){++calls;if(out)memcpy(static_cast<uint8_t*>(out)+8,&nativeStatus,4);return nativeResult;}
template<class T>void put(void *base,unsigned at,T value){memcpy(static_cast<uint8_t*>(base)+at,&value,sizeof(value));}
BODY
int main() {
 unsigned cases=0;
 for(unsigned ddi=0;ddi<9;++ddi)for(unsigned mode=0;mode<5;++mode)for(unsigned status=0;status<4;++status)
 for(unsigned sinks=0;sinks<2;++sinks)for(unsigned wiring=0;wiring<8;++wiring){
  state={};memset(ports,0,sizeof(ports));state.orgTGLAGDCPortStatus=reinterpret_cast<uintptr_t>(native);
  state.tglPortByIndex=reinterpret_cast<uintptr_t>(get);state.tglObservationController=ctl;
  state.firmwarePorts.status=XELP::VBT::Status::Valid;
  state.firmwarePorts.present=1; // declared panel; no other child.
  if(wiring==1)state.firmwarePorts.present|=1U<<ddi;
  if(wiring==2)state.firmwarePorts.unmapped=1;
  if(wiring==3)state.firmwarePorts.status=XELP::VBT::Status::Missing;
  if(wiring==4)state.tglTCWiringExplicit=true; // unspecified != excluded
  if(wiring==5){state.tglTCWiringExplicit=true;state.tglTCWiring=TGLTCWiring::fromMasks(0,0,TGLTCWiring::Mask);}
  if(wiring==6)state.tglTCWiringLoaded=false;
  if(wiring==7)state.adlsDisplaySelected=true;
  auto c=reinterpret_cast<uint32_t*>(state.tglPlatformCopy+0x20+24*ddi);
  c[0]=ddi;c[1]=ddi+6;c[2]=1;put(ports[ddi],0x548,c);put(ports[ddi],0x550,mode);put(ports[ddi],0x554,status);
  ports[ddi][0xF8]=sinks;put(ctl,0xD40,state.tglPlatformCopy+0);
  alignas(8) uint32_t req[4]={ddi+1},out[4]={};
  uint8_t before[sizeof(ports)];memcpy(before,ports,sizeof(ports));calls=lookups=0;
  assert(!XE::wrapTGLAGDCPortStatus(ctl,req,out)&&calls==1);
  const bool absent=ddi>=3&&(wiring==0||wiring==5);
  assert(out[2]==(absent&&mode==0&&status==2&&!sinks?0u:1u));
  assert(!memcmp(before,ports,sizeof(ports))); // no mode/state/power mutations.
  assert(lookups==unsigned(absent));++cases;
 }
 // Exact controller/config association is mandatory even for excluded wiring.
 for(unsigned bad=0;bad<7;++bad) {
  state={};state.orgTGLAGDCPortStatus=reinterpret_cast<uintptr_t>(native);state.tglPortByIndex=reinterpret_cast<uintptr_t>(get);
  state.tglObservationController=ctl;state.tglTCWiringExplicit=true;state.tglTCWiring=TGLTCWiring::fromMasks(0,0,0x1f8);
  auto c=reinterpret_cast<uint32_t*>(state.tglPlatformCopy+0x20+24*5);c[0]=5;c[1]=11;c[2]=1;
  memset(ports[5],0,sizeof(ports[5]));put(ports[5],0x548,c);put(ports[5],0x554,2u);put(ctl,0xD40,state.tglPlatformCopy+0);
  uint32_t copy[6];memcpy(copy,c,sizeof(copy));
  if(bad==0)put(ports[5],0x548,copy+0);
  if(bad==1)state.tglObservationController=nullptr;
  if(bad==2)state.xeProfile.family=XELP::Family::AlderLakeN;
  if(bad==3)c[0]=4;
  if(bad==4)c[1]=10;
  if(bad==5)c[2]=0;
  if(bad==6)state.tglInactivePorts=false;
  uint32_t req[4]={6},out[4]={};calls=0;
  assert(!XE::wrapTGLAGDCPortStatus(ctl,req,out)&&out[2]==1&&calls==1);
 }
 printf("PASS %u port/mode/state/sink/wiring combinations plus binding guards; native state untouched\n",cases);
}
'''.replace('BODY',body)
with tempfile.TemporaryDirectory(prefix='agdc-status-') as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined','-I'+str(r/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
