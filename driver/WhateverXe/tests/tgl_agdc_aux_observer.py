#!/usr/bin/env python3
"""The production AUX observer must never inject a port-specific transaction."""
from pathlib import Path
import re, subprocess, tempfile, sys
r=Path(__file__).resolve().parents[1]
s=(Path(sys.argv[1]) if len(sys.argv)>1 else r/'WhateverXe/kern_xe.cpp').read_text()
body=re.search(r'uint32_t XE::wrapTGLAGDCAUX\(.*?\n\}',s,re.S).group()
code=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
struct XE {uintptr_t orgTGLAGDCAUX;static uint32_t wrapTGLAGDCAUX(void*,void*,void*);};
XE state,*callbackXE=&state;
#define FunctionCast(f,p) reinterpret_cast<decltype(&f)>(p)
struct IORegistryEntry {
 static IORegistryEntry *getRegistryRoot(){static IORegistryEntry e;return &e;}
 template<class... A>void setProperty(A...){}
};
template<class... A>void tglTrace(A...){}
void clock_get_uptime(uint64_t *v){*v=60000000000ULL;}
void absolutetime_to_nanoseconds(uint64_t v,uint64_t *n){*n=v;}
unsigned calls,result;void *expectedRequest,*expectedOutput;
uint32_t native(void *c,void *request,void *output){
 assert(c==&state && request==expectedRequest && output==expectedOutput);
 assert(++calls==1);return result;
}
BODY
int main(){
 state.orgTGLAGDCAUX=reinterpret_cast<uintptr_t>(native);
 for(unsigned ddi=0;ddi<10;++ddi)for(unsigned write=0;write<2;++write)
 for(unsigned error=0;error<2;++error)for(unsigned length=0;length<32;++length){
  alignas(8) uint8_t request[64]{},output[64]{},before[64]{};
  request[0]=ddi;request[9]=2;request[12]=length;request[13]=write;
  memcpy(before,request,sizeof(request));expectedRequest=request;expectedOutput=output;
  calls=0;result=error?0xe00002d6:0;
  assert(XE::wrapTGLAGDCAUX(&state,request,output)==result);
  assert(calls==1&&!memcmp(before,request,sizeof(request)));
 }
 calls=0;expectedRequest=expectedOutput=nullptr;
 assert(XE::wrapTGLAGDCAUX(&state,nullptr,nullptr)==result&&calls==1);
 puts("PASS 1280 AUX observer cases: one native call, original arguments/result, no port-specific probe");
}
'''.replace('BODY',body)
with tempfile.TemporaryDirectory(prefix='tgl-aux-observer-') as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
