#!/usr/bin/env python3
"""Execute production no-POSH restore wrapper; optionally qualify the exact local donor.

Native copying, caller identity and existing CPS/ADL helpers are explicit mocks.
The optional exact-donor mode also executes context free and Scheduler5 cleanup;
OSObject release and the GC base tail remain mocks. No GPU/kernel scheduling.
"""
from pathlib import Path
import hashlib
import platform
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
    assert platform.machine().lower() in ('x86_64', 'amd64'), 'native lifecycle replay requires x86_64'

code = r'''
#include "kern_tgl_noposh.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <vector>
#include <sys/mman.h>
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
// Exact donor's complete IGHardwareContext::free, with real Scheduler5 no-op
// cleanup. OSObject releases and the IGGCObject base tail are boundary mocks.
// This is an observation of a native ownership omission, NOT a leak-free oracle.
static std::vector<void *> released;
static unsigned baseFrees;
static void releaseObject(void *object){released.push_back(object);}
static void freeBase(void *){++baseFrees;}
static void replayFree(const std::vector<uint8_t> &bytes){
 auto native=static_cast<uint8_t *>(mmap(nullptr,bytes.size(),PROT_READ|PROT_WRITE,
                                       MAP_PRIVATE|MAP_ANON,-1,0));
 assert(native!=MAP_FAILED);memcpy(native,bytes.data(),bytes.size());
 uintptr_t cleanup=0;memcpy(&cleanup,native+0xD3940+16+0x138,8);assert(cleanup==0x39608);
 // The only redirected native boundary is the base-class free tail. All
 // context member accesses, branches and releases execute original bytes.
 put(native,0xC8660+0xA0,reinterpret_cast<void *>(freeBase));
 assert(mprotect(native,bytes.size(),PROT_READ|PROT_EXEC)==0);
 for(uint8_t flags:{uint8_t(0x10),uint8_t(0xB3)})for(bool clear:{false,true}){
  reset();context[0x6E]=flags;void *pocs=context+0x80;put(context,0xA0,pocs);
  if(clear){assert(tglCPSRestore(context));assert(!(context[0x6E]&0x10));}
  // Allocation has already happened before restore; the production wrapper
  // must retain the pointer until native teardown, including the clear case.
  void *retained=nullptr;memcpy(&retained,context+0xA0,8);assert(retained==pocs);
  alignas(16) uint8_t c[0xD8]{},owner[0x1260]{},scheduler[8]{},objects[6][8]{};
  uintptr_t releaseTable[6]{},schedulerTable[0x140/8]{};
  releaseTable[0x28/8]=reinterpret_cast<uintptr_t>(releaseObject);
  schedulerTable[0x138/8]=reinterpret_cast<uintptr_t>(native+cleanup);
  put(scheduler,0,schedulerTable);put(owner,0x1250,scheduler);
  void *head=c;put(c,0x48,&head);put(c,0x50,owner);c[0x6E]=context[0x6E];
  const size_t offsets[]{0xB0,0xB8,0xA8,0x98,0x58,0xA0};
  for(unsigned i=0;i<6;++i){put(objects[i],0,releaseTable);put(c,offsets[i],objects[i]);}
  released.clear();baseFrees=0;
  reinterpret_cast<void (*)(void *)>(native+0x7C5D8)(c);
  assert(head==nullptr && baseFrees==1 && released.size()==5);
  for(unsigned i=0;i<5;++i){assert(released[i]==objects[i]);memcpy(&retained,c+offsets[i],8);assert(!retained);}
  memcpy(&retained,c+0xA0,8);assert(retained==objects[5]);
 }
 assert(munmap(native,bytes.size())==0);
 puts("PASS: native context free omits +A0 release with POSH set or cleared; existing native omission, NOT leak-free");
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
  replayFree(bytes);
 }
}
'''.replace('WRAPPER', wrapper).replace('QUALIFIER', qualifier)
with tempfile.TemporaryDirectory(prefix='tgl-noposh-adapter-') as directory:
    p = Path(directory)
    (p / 'test.cpp').write_text(code)
    subprocess.run(['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined',
                    '-I'+str(root/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test'), *sys.argv[1:]],check=True)
