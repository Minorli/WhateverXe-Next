#!/usr/bin/env python3
"""Execute production ICL-only PHY write suppression; keep AUX power requests."""
from pathlib import Path
import subprocess,sys,tempfile
root=Path(__file__).resolve().parents[1];s=(root/'WhateverXe/kern_xe.cpp').read_text()
start=s.index('\t// Linux icl_aux_power_well_enable/disable')
guard=s[start:s.index('\ttglProgramRecord(access, reg, value, 0',start)].replace('tglConnectionCaller(__builtin_return_address(0))','caller')
if len(sys.argv)>1:
 b=Path(sys.argv[1]).read_bytes()
 for i,end in enumerate([0x5cab6,0x5caf5,0x5d0c9,0x5d108]):
  reg=0x6c030 if i&1 else 0x162030
  expected=b'\xbe'+reg.to_bytes(4,'little')+bytes.fromhex('89c2ff9130010000')
  assert b[end-len(expected):end]==expected
code=r'''
#include "kern_tgl_port_map.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <initializer_list>
struct State {bool tglActive=true,tglComboAUXGateReady=true;void *tglObservationController=nullptr;} state,*callbackXE=&state;
unsigned skipped=0;bool forwarded=false;
void tglTransitionRecord(unsigned stage,bool done,void *,unsigned long reg,uintptr_t caller,uint32_t value){assert(stage==72 && done && TGLComboAUXGate::legacyWrite(reg,caller) && value==0xd1);++skipped;}
void run(void *access,unsigned long reg,uint32_t value,uintptr_t caller){
GUARD
 forwarded=true;
}
int main(){
 alignas(8) uint8_t ctl[0xD00]{};void *access=ctl+8;memcpy(ctl+0xC40,&access,sizeof(access));
 for(auto caller:{0ul,0x5CAB5ul,0x5CAB6ul,0x5CAF5ul,0x5D0C9ul,0x5D108ul,0x5D109ul})
 for(auto reg:{0x162030ul,0x6C030ul,0x160030ul,0x45440ul,0x45444ul,0x45454ul})
 for(unsigned bad=0;bad<5;++bad){
  state={};state.tglObservationController=bad==1?nullptr:ctl;
  state.tglActive=bad!=2;state.tglComboAUXGateReady=bad!=3;
  forwarded=false;skipped=0;
  const bool match=(reg==0x162030 && (caller==0x5CAB6||caller==0x5D0C9)) || (reg==0x6C030 && (caller==0x5CAF5||caller==0x5D108));
  const bool suppress=bad==0 && match;
  run(bad==4?nullptr:access,reg,0xd1,caller);
  assert(forwarded!=suppress && skipped==suppress);
 }
 puts("PASS: production PHY gate guard; unrelated callers, ports, power request/ACK registers preserved");
}
'''.replace('GUARD',guard)
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined','-I'+str(root/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
