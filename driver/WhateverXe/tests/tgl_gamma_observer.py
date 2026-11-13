#!/usr/bin/env python3
"""Default 10-bit gamma uploads claim no readback slot and publish nothing; opt-in keeps the observer."""
from pathlib import Path
import argparse,re,subprocess,tempfile
r=Path(__file__).resolve().parents[1];p=argparse.ArgumentParser();p.add_argument('--source',type=Path);a=p.parse_args()
s=(a.source or r/'WhateverXe/kern_xe.cpp').read_text()
body=re.search(r'void XE::wrapTGLGamma10\(.*?\n\}',s,re.S).group()
f=r'''#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cassert>
#define arrsize(a) (sizeof(a)/sizeof((a)[0]))
void clock_get_uptime(uint64_t*p){*p=42;}
void absolutetime_to_nanoseconds(uint64_t a,uint64_t*p){*p=a;}
void *current_thread(){return reinterpret_cast<void*>(0x1234);}
static bool tglDisplayTraceEnabled;
unsigned publications=0,nativeCalls=0,claimedDuringNative=0;
struct TGLGammaObservation {
 uintptr_t thread; uint32_t count, dropped; uint64_t writes[1100];
 uint32_t indexReg, readCount, savedIndex, restoredIndex; uint32_t readback[1024];
};
TGLGammaObservation tglGammaObservations[4] {};
void nativeGamma(void*,void*,uint32_t){
 ++nativeCalls;
 // wrapTGLRegisterWrite arms the LUT readback only for a thread owning a slot.
 for(auto &e:tglGammaObservations) if(e.thread==reinterpret_cast<uintptr_t>(current_thread())) ++claimedDuringNative;
}
#define FunctionCast(wrapper,original) (original)
struct IORegistryEntry {
 static IORegistryEntry *getRegistryRoot(){static IORegistryEntry r;return &r;}
 void setProperty(const char*,void*,size_t){++publications;}
};
struct XE {
 void (*orgTGLGamma10)(void*,void*,uint32_t)=nativeGamma;
 static void wrapTGLGamma10(void*,void*,uint32_t);
};
XE instance,*callbackXE=&instance;
// BODY
int main(){
 alignas(8) static uint8_t path[0x3700]{};
 for(unsigned i=0;i<50;++i)XE::wrapTGLGamma10(path,nullptr,i);
 assert(nativeCalls==50&&publications==0&&claimedDuringNative==0);
 for(auto &e:tglGammaObservations)assert(e.thread==0);
 tglDisplayTraceEnabled=true;
 XE::wrapTGLGamma10(path,nullptr,7);
 assert(nativeCalls==51&&claimedDuringNative==1&&publications>=2);
 for(auto &e:tglGammaObservations)assert(e.thread==0);   // slot released after the call
}
'''.replace('// BODY',body)
with tempfile.TemporaryDirectory(prefix='tgl-gamma-') as d:
 d=Path(d);(d/'check.cpp').write_text(f)
 subprocess.run(['clang++','-std=c++14','-O1','-g','-fsanitize=address,undefined',str(d/'check.cpp'),'-o',str(d/'check')],check=True)
 subprocess.run([str(d/'check')],check=True)
print('PASS: 50 default gamma uploads reached native with no readback slot and zero publications; opt-in observer retained')
