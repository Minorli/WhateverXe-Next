#!/usr/bin/env python3
"""Exercise the extracted DVMT admission/calculation against BIOS GMS fixtures."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
s=(r/'WhateverXe/kern_dvmt.cpp').read_text();body=s[s.index('void DVMTCalcFix::processKernel'):s.index('void DVMTCalcFix::processFramebufferKext')]
code=r'''
#include <cstdint>
#include <cassert>
#include <cstdio>
#define SYSLOG(...)
#define DBGLOG(...)
bool optin=true,published=true;
bool checkKernelArgument(const char *){return optin;}
struct GPU {void *getProperty(const char *){return nullptr;}};
struct KernelPatcher {};struct DeviceInfo {GPU *videoBuiltin;};
namespace WIOKit {uint32_t ggc;constexpr int kIOPCIConfigGraphicsControl=0x50;bool awaitPublishing(GPU *){return published;}uint32_t readPCIConfigValue(GPU *,int,int,int){return ggc;}}
class DVMTCalcFix {public:bool applied=false,enabled=false,available=true;uint32_t dvmt=0;void processKernel(KernelPatcher &,DeviceInfo *);};
'''+body+r'''
int main(){GPU gpu;DeviceInfo info{&gpu};KernelPatcher p;
struct Case{unsigned gms,mb;bool patch;};
for(auto c:{Case{1,32,false},Case{2,64,false},Case{3,96,false},Case{15,480,false},Case{0x20,1024,true},Case{0x30,1536,true},Case{0x40,2048,true},Case{0xf0,4,true},Case{0xfe,60,true}}){
 DVMTCalcFix d;WIOKit::ggc=c.gms<<8;d.processKernel(p,&info);assert(d.dvmt==c.mb*1024*1024);assert(d.enabled==c.patch);assert(!d.applied);
}
// Encodings unsupported by this decoder must not authorize a zero-byte patch.
for(unsigned gms=0x10;gms<=0xff;++gms) {
 if(gms==0x20 || gms==0x30 || gms==0x40 || (gms>=0xf0 && gms<=0xfe))continue;
 DVMTCalcFix d;WIOKit::ggc=gms<<8;d.processKernel(p,&info);
 if(d.enabled || d.applied || d.dvmt) {
  fprintf(stderr,"FAIL unsupported GMS=%02x leaves patch enabled=%u bytes=%u\n",gms,d.enabled,d.dvmt);return 1;
 }
}
DVMTCalcFix d;WIOKit::ggc=0xfe00;d.processKernel(p,&info);assert(d.dvmt==60*1024*1024);
optin=false;d.processKernel(p,&info);assert(!d.enabled&&!d.applied&&d.dvmt==0);
optin=true;d.available=false;d.dvmt=123;d.processKernel(p,&info);assert(!d.enabled&&!d.applied&&d.dvmt==0);
d.available=true;published=false;d.dvmt=123;d.processKernel(p,&info);assert(!d.enabled&&!d.applied&&d.dvmt==0);
puts("PASS DVMT: supported sizes, unsupported encodings, opt-out, unavailable and failed publishing; no stale size or zero-size patch");}
'''
code='#include <initializer_list>\n'+code
with tempfile.TemporaryDirectory(prefix='whxe-dvmt-') as d:
 p=Path(d);(p/'check.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined',str(p/'check.cpp'),'-o',str(p/'check')],check=True)
 subprocess.run([str(p/'check')],check=True)
