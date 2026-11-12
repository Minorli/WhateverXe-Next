#!/usr/bin/env python3
"""Exercise production combo entry qualification and exact native ABI bytes."""
from pathlib import Path
import re,sys,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'WhateverXe/kern_xe.cpp').read_text()
start=s.index('bool XE::tglProgramComboSignal(')
fn=re.search(r'bool XE::tglProgramComboSignal\(.*?\n\}',s,re.S).group().replace('XE::tglProgramComboSignal','run')
if len(sys.argv)>1:
 b=Path(sys.argv[1]).read_bytes()
 for name,at in [('pcsABI',0x60eb9),('lanesABI',0x61468),('successABI',0x61478)]:
  expected=bytes(int(x.strip(),0) for x in re.search(name+r'\[\] \{([^}]+)',s).group(1).split(','))
  assert b[at:at+len(expected)]==expected
code=r'''
#include "kern_alderlake_s.hpp"
#include "kern_tgl_combo_dp.hpp"
#include "kern_xelp_sst_route.hpp"
#include "kern_tgl_combo_signal.hpp"
#include <cassert>
#include <map>
#include <cstdio>
struct State {
 uint32_t tglComboDPMask=0;bool adlsDisplaySelected=false;AlderLakeS::Wiring adlsWiring;alignas(8) uint8_t tglPlatformCopy[0x1c0]{};
 void *tglObservationController=nullptr;bool tglActive=true,tglComboSignalReady=true;
 struct {uint32_t identity=0x9A498086;XELP::Family family=XELP::Family::TigerLake;} xeProfile;
 XELP::VBT::Ports firmwarePorts;
} state,*callbackXE=&state;
constexpr uint32_t kIOReturnNotReady=0xe00002d8;
std::map<unsigned,unsigned> registers;unsigned reads=0,writes=0,records=0;
uint32_t wrapTGLRegisterRead(void *,unsigned long reg){++reads;return registers[reg];}
void wrapTGLRegisterWrite(void *,unsigned long reg,uint32_t value){++writes;
 if(reg==0x6C694)for(unsigned n=0;n<4;++n)registers[0x6C894+0x100*n]=value;
 if(reg==0x160694)for(unsigned n=0;n<4;++n)registers[0x160894+0x100*n]=value;
 if(reg==0x162694)for(unsigned n=0;n<4;++n)registers[0x162894+0x100*n]=value;
 registers[reg]=value;
}
void tglTransitionRecord(unsigned stage,bool,void *,unsigned,unsigned,unsigned,const void *,unsigned){assert(stage==73);++records;}
FUNCTION
int main(){
 for(unsigned bad=0;bad<23;++bad){
  alignas(8) uint8_t p[0x700]{},ctl[0xD00]{},access[0x100]{};uint32_t config[8]{};
  auto store=[](void *b,unsigned o,auto v){memcpy(static_cast<uint8_t *>(b)+o,&v,sizeof(v));};
  state={};state.tglObservationController=ctl;state.firmwarePorts.status=XELP::VBT::Status::Valid;
  state.firmwarePorts.panel.valid=true;state.firmwarePorts.panel.ddi=0;
  store(p,0x548,config+0);store(p,0x550,1U);store(p,0x578,access+0);store(ctl,0xC40,access+0);
  store(p,0x124,1U);store(p,0x128,10U);store(p,0x620,0x162804U);store(p,0x5C8,0x162028U);
  store(access,0x60,0x200000U);store(access,0x50,ctl+0);
  registers.clear();registers[0x45404]=3;registers[0x45454]=3;reads=writes=records=0;
  if(bad==1)state.tglActive=false;
  if(bad==2)state.tglComboSignalReady=false;
  if(bad==3)state.xeProfile.family=XELP::Family::AlderLakeN;
  if(bad==4)state.firmwarePorts.panel.valid=false;
  if(bad==5)config[0]=3;
  if(bad==6)config[2]=2;
  if(bad==7)config[3]=1;
  if(bad==8)p[0x117]=1;
  if(bad==9)state.firmwarePorts.panel.ddi=1;
  if(bad==10)state.firmwarePorts.port[0].flags=2;
  if(bad==11)store(p,0x578,ctl+0);
  if(bad==12)store(p,0x124,3U);
  if(bad==13)store(p,0x128,12U);
  if(bad==14)state.xeProfile.identity=0x9A491234;
  if(bad==15)store(p,0x620,0x6C804U);
  if(bad==16)store(access,0x60,0x100000U);
  if(bad==17)registers[0x45404]=0;
  if(bad==18)registers[0x45454]=~0U;
  if(bad==19)registers[0x162A9C]=~0U;
  if(bad==22)state.xeProfile.identity=0xFFFF8086;
  uint32_t result=99;
  bool applied=run(bad==20?nullptr:p,0,0,bad==21?p:nullptr,result);
  assert(applied==(bad==0||bad==19));assert((writes!=0)==(bad==0));
  if(bad==0)assert(result==0&&records==1);
  if(bad==19)assert(result==kIOReturnNotReady);
  if(bad!=0&&bad!=17&&bad!=18&&bad!=19)assert(reads==0);
 }

 // Known physical subplatforms must select electrical policy independently
 // of a render backend. HBR2 U/Y differs at voltage0/emphasis1; HBR3 eDP
 // uses the ICL table regardless of VBT low-swing/HOBL flags.
 for(unsigned id:{0x9A498086u,0x9A788086u,0x9A608086u})for(unsigned rate:{20u,30u})
 for(bool low:{false,true})for(bool hobl:{false,true}) {
  alignas(8) uint8_t p[0x700]{},ctl[0xD00]{},access[0x100]{};uint32_t config[8]{};
  auto store=[](void *b,unsigned o,auto v){memcpy(static_cast<uint8_t *>(b)+o,&v,sizeof(v));};
  state={};state.xeProfile.identity=id;state.tglObservationController=ctl;
  state.firmwarePorts.status=XELP::VBT::Status::Valid;state.firmwarePorts.panel={true,low,hobl,0,0};
  store(p,0x548,config+0);store(p,0x550,1U);store(p,0x578,access+0);store(ctl,0xC40,access+0);
  store(p,0x124,4U);store(p,0x128,rate);store(p,0x620,0x162804U);store(p,0x5C8,0x162028U);
  store(access,0x60,0x200000U);store(access,0x50,ctl+0);
  registers.clear();registers[0x45404]=3;registers[0x45454]=3;reads=writes=records=0;
  uint32_t result=99;assert(run(p,0,1,nullptr,result));
  assert(result==0);
  const unsigned post=rate==30?8:low?7:id==0x9A608086?8:9;
  assert(((registers[0x162890]>>12)&63)==post);
 }

 // External TGL Combo uses DP electrical values even if a different panel
 // advertises low-swing/HOBL. It requires an exact immutable port record.
 for(unsigned ddi:{0u,1u})for(unsigned rate:{6u,10u,20u,30u})for(unsigned bad=0;bad<5;++bad) {
  alignas(8) uint8_t p[0x700]{},ctl[0xD00]{},access[0x100]{};
  auto store=[](void *b,unsigned o,auto v){memcpy(static_cast<uint8_t *>(b)+o,&v,sizeof(v));};
  state={};state.tglComboDPMask=1U<<ddi;state.tglObservationController=ctl;
  state.firmwarePorts.status=XELP::VBT::Status::Valid;
  state.firmwarePorts.panel={true,true,true,0,ddi^1};
  const uint32_t c[]={ddi,ddi+1,1,0,0x400,0x201};
  auto config=reinterpret_cast<uint32_t *>(state.tglPlatformCopy+0x20+24*ddi);memcpy(config,c,sizeof(c));
  const auto base=ddi?0x6c000U:0x162000U;
  store(p,0x548,config);store(p,0x550,1U);store(p,0x578,access+0);store(ctl,0xC40,access+0);
  store(p,0x124,4U);store(p,0x128,rate);store(p,0x620,base+0x804);store(p,0x5C8,base+0x28);
  store(access,0x60,0x200000U);store(access,0x50,ctl+0);
  registers.clear();registers[0x45404]=3;registers[0x45454]=3U<<(2*ddi);reads=writes=records=0;
  if(bad==1)state.tglComboDPMask=0;
  if(bad==2)store(p,0x548,c+0);
  if(bad==3)config[1]^=1;
  if(bad==4)store(p,0x550,3U);
  uint32_t result=99;assert(run(p,0,1,nullptr,result)==(bad==0));
  assert(bool(writes)==(bad==0));
  if(!bad){assert(result==0);assert(((registers[base+0x890]>>12)&63)==(rate>10?9u:8u));}
 }
 for(unsigned ddi:{0u,3u,4u})for(unsigned bad=0;bad<12;++bad) {
  alignas(8) uint8_t p[0x700]{},ctl[0xD00]{},access[0x100]{};
  auto store=[](void *b,unsigned o,auto v){memcpy(static_cast<uint8_t *>(b)+o,&v,sizeof(v));};
  state={};state.adlsDisplaySelected=true;state.adlsWiring.valid=true;state.adlsWiring.dp=1U<<ddi;state.adlsWiring.pin[ddi]=ddi?ddi+6:1;
  AlderLakeS::Port route;assert(AlderLakeS::Port::resolve(ddi,route));auto base=route.phyBase();state.xeProfile.family=XELP::Family::AlderLakeS;
  state.tglObservationController=ctl;state.firmwarePorts.status=XELP::VBT::Status::Valid;
  const uint32_t c[]={ddi,ddi?ddi+6:1,1,0,0x400,0x201};auto config=reinterpret_cast<uint32_t *>(state.tglPlatformCopy+0x20+24*ddi);memcpy(config,c,sizeof(c));
  store(p,0x548,config);store(p,0x550,1U);store(p,0x578,access+0);store(ctl,0xC40,access+0);
  store(p,0x124,4U);store(p,0x128,20U);store(p,0x620,base+0x804);store(p,0x5C8,base+0x28);
  store(access,0x60,0x200000U);store(access,0x50,ctl+0);
  registers.clear();registers[0x45404]=0x3f;registers[0x45454]=3U<<(2*ddi);reads=writes=records=0;
  if(bad==1)state.adlsWiring.dp=0;
  if(bad==2)store(p,0x548,c+0);
  if(bad==3)store(p,0x550,4U);
  if(bad==4)store(p,0x128,12U);
  if(bad==5)registers[0x45404]=0;
  if(bad==6)registers[0x45454]=0;
  if(bad==7)store(p,0x620,base+0x904);
  if(bad>=8)config[bad-8]^=1;
  uint32_t result=99;assert(run(p,0,0,nullptr,result)==(bad==0));
  assert((writes!=0)==(bad==0));if(!bad)assert(!result && records==1 && registers[base+0x89c]==0x35000000);
 }
 // ADL-S panel uses the same programming sequence with panel electrical
 // policy, never the TGL table or the external-DP policy by accident.
 for(unsigned rate:{6U,10U,20U,30U})for(unsigned low=0;low<2;++low)for(unsigned hobl=0;hobl<2;++hobl) {
  alignas(8) uint8_t p[0x700]{},ctl[0xD00]{},access[0x100]{};
  auto store=[](void *b,unsigned o,auto v){memcpy(static_cast<uint8_t *>(b)+o,&v,sizeof(v));};
  state={};state.adlsDisplaySelected=true;state.xeProfile.family=XELP::Family::AlderLakeS;
  state.tglObservationController=ctl;auto &v=state.firmwarePorts;v.status=XELP::VBT::Status::Valid;v.present=1;v.port[0]={0x78C6,10,0,0x40,0,0};
  v.panel.valid=true;v.panel.ddi=0;v.panel.backlightValid=true;v.panel.pwmPresent=true;v.panel.backlightType=2;v.panel.lowSwing=low;v.panel.hobl=hobl;
  state.adlsWiring=AlderLakeS::Wiring::fromFirmware(v);assert(state.adlsWiring.panel==1);
  auto config=reinterpret_cast<uint32_t*>(state.tglPlatformCopy+0x20);const uint32_t record[]={0,0,0,0,2,0x18};memcpy(config,record,sizeof(record));
  store(p,0x548,config);store(p,0x550,1U);store(p,0x578,access+0);store(ctl,0xC40,access+0);
  store(p,0x124,2U);store(p,0x128,rate);store(p,0x620,0x162804U);store(p,0x5C8,0x162028U);
  store(access,0x60,0x200000U);store(access,0x50,ctl+0);
  registers.clear();registers[0x45404]=3;registers[0x45454]=3;reads=writes=records=0;
  uint32_t result=99;assert(run(p,0,0,nullptr,result)&&!result&&records==1);
  XELP::ComboPolicy expected;assert(expected.select(XELP::ComboPolicy::Platform::ADLS,rate,true,low,false,0,0));
  assert((registers[0x16289C]>>24&127)==expected.coefficients.scalar);
  assert((registers[0x162890]&0x3ffff)==(expected.coefficients.post1<<12|expected.coefficients.cursor));
  assert(!(registers[0x162028]&12));
  state.adlsWiring.panel=0;writes=0;assert(!run(p,0,0,nullptr,result)&&!writes);
 }
 puts("PASS production combo guard: family, port, firmware, ownership, power and missing bank");
}
'''.replace('FUNCTION',fn)
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined','-I'+str(root/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
