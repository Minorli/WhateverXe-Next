#!/usr/bin/env python3
"""Replay the production PG2/3/4 poll with delayed, absent and failed MMIO."""
from pathlib import Path
import hashlib, subprocess, sys, tempfile

root = Path(__file__).resolve().parents[1]
source = (root/'WhateverXe/kern_xe.cpp').read_text()
begin = source.index('\t// Only the qualified donor post-ACK fuse reads')
body = source[begin:source.index('\tif (reg == 0x42000 && s->tglPG1FuseReadCaller', begin)]
sites = [0x39D, 0x53F, 0x6D3]
expected = [bytes.fromhex(x) for x in [
    'be00200400e85d2900000fbae019', 'be00200400e8bb2700000fbae018',
    'be00200400e8272600000fbae017']]
function = bytearray(0x700)
for off, seq in zip(sites, expected):
    function[off:off+len(seq)] = seq
if len(sys.argv) > 1:
    donor = Path(sys.argv[1]).read_bytes()
    assert hashlib.sha256(donor).hexdigest() == 'c004fc1ac9ee4157e2e12736f40fa6639bde025a279326beeb9dd48c9b445731'
    function = donor[0x5B640:0x5BD40]
    for off, seq in zip(sites, expected):
        assert function[off:off+14] == seq
code = r'''
#include "kern_xelp_donor.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
static unsigned elapsed,reads,readyAt,failAt;
static uint32_t statusMask;
static void *expectedAccess;
static void IODelay(unsigned us){assert(us==10);elapsed+=us;}
static uint32_t read(void *access,unsigned long reg){
 assert(access==expectedAccess && reg==0x42000);++reads;
 if(elapsed>=failAt)return UINT32_MAX;
 return 0x80000000U | (elapsed>=readyAt ? statusMask : 0);
}
struct State {uintptr_t tglPGFuseWaitFunction=0x100000;uintptr_t orgTGLRegisterRead=reinterpret_cast<uintptr_t>(read);} state;
uint32_t run(void *access,unsigned long reg,uintptr_t caller,uint32_t value){
 auto s=&state;
 BODY
 return value;
}
int main(){
 uint8_t function[]={BYTES};
 assert(XELPDonor::powerWellFuseSites(function,sizeof(function)));
 assert(!XELPDonor::powerWellFuseSites(nullptr,sizeof(function)));
 for(unsigned n=0;n<0x6E1;++n)assert(!XELPDonor::powerWellFuseSites(function,n));
 // Every instruction byte, including the callee displacement, is guarded.
 for(auto off:XELPDonor::fuseReadSites)for(unsigned j=0;j<14;++j){
  function[off+j]^=1;assert(!XELPDonor::powerWellFuseSites(function,sizeof(function)));function[off+j]^=1;
 }
 expectedAccess=function;unsigned cases=0;
 for(unsigned pg=0;pg<3;++pg)for(unsigned delay=0;delay<=1010;delay+=10)
 for(unsigned failure:{UINT32_MAX,20U}) {
  elapsed=reads=0;readyAt=delay;failAt=failure;statusMask=1U<<(25-pg);
  uint32_t initial=0x80000000U|(delay==0?statusMask:0);
  auto result=run(expectedAccess,0x42000,0x100000+XELPDonor::fuseReadSites[pg]+10,initial);
  unsigned end=delay<1000?delay:1000;if(failure<end)end=failure;
  assert(elapsed==end && reads==end/10);
  assert(result==(end>=failure?UINT32_MAX:0x80000000U|(end>=delay?statusMask:0)));
  ++cases;
 }
 for(uintptr_t caller=0x100000;caller<0x100700;++caller){
  auto mask=XELPDonor::powerWellFuseMask(0x100000,caller);
  for(auto reg:{0x42000UL,0x45404UL})for(auto initial:{0U,UINT32_MAX,0xFFFFFFFFU ^ mask}){
   elapsed=reads=0;readyAt=failAt=UINT32_MAX;
   auto result=run(expectedAccess,reg,caller,initial);
   const bool wait=reg==0x42000 && mask && initial!=UINT32_MAX;
   assert(elapsed==(wait?1000U:0U) && reads==elapsed/10);
   assert(result==(wait?0x80000000U:initial));++cases;
  }
 }
 state.tglPGFuseWaitFunction=0;elapsed=reads=0;
 assert(run(expectedAccess,0x42000,0x1003A7,0)==0 && !reads && !elapsed);
 assert(!XELPDonor::powerWellFuseMask(0x100000,0xFFFFF));
 printf("PASS: %u production fuse poll cases; exact sites, timeout, read failure, unrelated callers\n",cases);
}
'''.replace('BODY', body).replace('BYTES', ','.join(map(str, function)))
with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp); (p/'test.cpp').write_text(code)
    subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined',
                    '-I'+str(root/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
