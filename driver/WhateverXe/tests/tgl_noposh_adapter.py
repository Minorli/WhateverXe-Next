#!/usr/bin/env python3
"""Execute production no-POSH restore wrapper; optionally qualify the exact local donor.

Native copying, caller identity and existing CPS/ADL helpers are explicit mocks.
Checks ordering and propagation, not GPU completion or real kernel scheduling.
"""
from pathlib import Path
import hashlib
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'WhateverXe/kern_xe.cpp').read_text()
wrapper = source[source.index('bool tglNoPOSHEnabled'):source.index('bool tglCPSSerialize')]
qualifier = source[source.index('static __attribute__((noinline)) bool tglQualifyNoPOSH'):source.index('// Constructor return is before scheduler submission.')]
if len(sys.argv) > 1:
    donor = Path(sys.argv[1])
    assert hashlib.sha256(donor.read_bytes()).hexdigest() == '567511bed855a5165cbc3615f61be7b4fcf607fde4fc5c48567fb127726d496e'

code = r'''
#include "kern_tgl_noposh.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <vector>
using mach_vm_address_t=uintptr_t;
uintptr_t tglCPSRestoreOriginal=0,tglCPSRestoreCaller=0x7C131,tglCPSCPUAddress=0,fixtureCaller=0;
unsigned nativeCalls=0,pointerCalls=0,goldenCalls=0,captureCalls=0;
bool nativeOK=true,expectClear=false,pointerOK=true,goldenOK=true;
alignas(64) uint8_t image[0x1200],saved[0x1200],buffer[0x40],context[0xB0];
static void put(void *base,size_t off,void *p){memcpy(static_cast<uint8_t *>(base)+off,&p,sizeof(p));}
static uint32_t &word(uint8_t *p,size_t off){return *reinterpret_cast<uint32_t *>(p+off);}
static uint8_t *cpu(void *p){assert(p==buffer);return image;}
static bool original(void *c){
 ++nativeCalls;assert(c==context);
 if(expectClear)assert(!(context[0x6E]&0x10));
 if(nativeOK)memcpy(image,saved,sizeof(image));
 return nativeOK;
}
static bool tglCPSRestorePointer(void *,uintptr_t){++pointerCalls;return pointerOK;}
static void adlnCaptureOwnedGolden(void *){++captureCalls;}
static bool tglAlderLakeNGoldenRestore(void *){++goldenCalls;return goldenOK;}
#define FunctionCast(fn,addr) reinterpret_cast<decltype(&fn)>(addr)
// Boundary mock: use each native caller return PC without executing Apple code.
#define __builtin_return_address(n) fixtureCaller
WRAPPER
#undef __builtin_return_address
QUALIFIER
static void reset(){
 memset(context,0,sizeof(context));memset(image,0xA5,sizeof(image));memset(saved,0x5A,sizeof(saved));
 memset(buffer,0,sizeof(buffer));memset(tglNoPOSHCounts,0,sizeof(tglNoPOSHCounts));
 word(saved,0x1008)=0x2244;word(saved,0x100C)=0x12345678;
 word(saved,0x10D4)=0x21B0;word(saved,0x10D8)=0x12345000;
 put(context,0x98,buffer);uint64_t length=sizeof(image);memcpy(buffer+0x20,&length,8);
 context[0x6E]=0xB3;fixtureCaller=tglCPSRestoreCaller;
 tglNoPOSHEnabled=true;nativeOK=pointerOK=goldenOK=true;expectClear=true;
 nativeCalls=pointerCalls=goldenCalls=captureCalls=0;
 tglCPSRestoreOriginal=reinterpret_cast<uintptr_t>(original);tglCPSCPUAddress=reinterpret_cast<uintptr_t>(cpu);
}
int main(int argc,char **argv){
 // Constructor and reset caller: native copy must see the cleared flag;
 // only the CTX_CTRL masked bit and POSH_LRCA may change after that copy.
 for(uintptr_t caller:{uintptr_t(0x7C131),uintptr_t(0x7C91E)}){
  reset();fixtureCaller=caller;assert(tglCPSRestore(context));assert(nativeCalls==1);
  assert(context[0x6E]==0xA3);assert(word(image,0x100C)==0x12345668);assert(word(image,0x10D8)==0);
  for(size_t i=0;i<sizeof(image);++i)if((i<0x100C||i>=0x1010)&&(i<0x10D8||i>=0x10DC))assert(image[i]==saved[i]);
  assert(pointerCalls==1 && goldenCalls==(caller==0x7C131) && captureCalls==goldenCalls);
  assert(tglNoPOSHCounts[caller==0x7C131?TGLNoPOSH::InitSanitized:TGLNoPOSH::ResetSanitized]==1);
 }
 reset();nativeOK=false;assert(!tglCPSRestore(context));assert(nativeCalls==1 && pointerCalls==0 && goldenCalls==0);
 assert(tglNoPOSHCounts[TGLNoPOSH::RestoreFalse]==1 && image[0x100C]==0xA5);
 for(bool disabled:{false,true}){
  reset();expectClear=false;if(disabled)tglNoPOSHEnabled=false;else context[0x6C]=1;
  assert(tglCPSRestore(context));assert(nativeCalls==1 && !memcmp(image,saved,sizeof(image)));
  assert(context[0x6E]==0xB3 && tglNoPOSHCounts[TGLNoPOSH::Calls]==0);
 }
 reset();pointerOK=false;assert(!tglCPSRestore(context));assert(nativeCalls==1 && goldenCalls==0);
 reset();goldenOK=false;assert(!tglCPSRestore(context));assert(nativeCalls==1 && goldenCalls==1);
 // A copied image with an unknown layout must not be reported restored.
 // Native init takes its fresh-image path; beginReset propagates false.
 for(uintptr_t caller:{uintptr_t(0x7C131),uintptr_t(0x7C91E)}){
  reset();fixtureCaller=caller;word(saved,0x10D4)=0xDEAD;assert(!tglCPSRestore(context));
  assert(tglNoPOSHCounts[TGLNoPOSH::LayoutMismatch]==1 && !memcmp(image,saved,sizeof(image)));
  assert(nativeCalls==1 && pointerCalls==0 && captureCalls==0 && goldenCalls==0);
 }
 reset();put(context,0x98,nullptr);assert(!tglCPSRestore(context));
 assert(tglNoPOSHCounts[TGLNoPOSH::LayoutMismatch]==1 && pointerCalls==0);
 puts("PASS: production wrapper order/delegation and sanitization failure propagation");
 if(argc==2){
  std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),{});
  assert(tglQualifyNoPOSH(reinterpret_cast<uintptr_t>(bytes.data()),bytes.size()));
  assert(!tglQualifyNoPOSH(reinterpret_cast<uintptr_t>(bytes.data()),0xC0577));
  // Independent donor offsets, retained from the immutable static audit.
  for(size_t off:{0x7BB13U,0x7C15EU,0x7C421U,0x7C919U,0x4301FU,0xB09A4U,0xB0BC0U,0xC04A8U,0xC0574U}){
   bytes[off]^=1;assert(!tglQualifyNoPOSH(reinterpret_cast<uintptr_t>(bytes.data()),bytes.size()));bytes[off]^=1;
  }
  puts("PASS: exact SHA donor qualification; each existing anchor independently rejects corruption (CPU only)");
 }
}
'''.replace('WRAPPER', wrapper).replace('QUALIFIER', qualifier)
with tempfile.TemporaryDirectory(prefix='tgl-noposh-adapter-') as directory:
    p = Path(directory)
    (p / 'test.cpp').write_text(code)
    subprocess.run(['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined',
                    '-I'+str(root/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test'), *sys.argv[1:]],check=True)
