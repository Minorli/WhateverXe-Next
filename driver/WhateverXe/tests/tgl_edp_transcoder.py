#!/usr/bin/env python3
"""Execute the guarded production eDP transcoder and zero-N patches on x86."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "WhateverXe/kern_xe.cpp").read_text()
start = source.index("\t\t\t\t// TGL/Xe-LP has no dedicated eDP")
end = source.index("\n\t\t\t}\n", start)
install = source[start:end]
rows = re.findall(r'\{"([^"]+)",(0x\w+),(0x\w+),(\d+),\{([^}]+)\}\}', install)
assert len(rows) == 21

def array(name):
    value = re.search(rf'{name}\[\] \{{([^}}]+)\}}', install).group(1)
    return bytes(int(x, 0) for x in value.split(","))

if len(sys.argv) > 1:
    donor = Path(sys.argv[1]).read_bytes()
    for _, _, offset, length, values in rows:
        expected = bytes(int(x, 0) for x in values.split(","))
        assert donor[int(offset, 0):int(offset, 0) + int(length)] == expected
    for offset, name in [(0x49ee9, "timingExpected"), (0x49dc4, "zeroExit")]:
        expected = array(name)
        assert donor[offset:offset + len(expected)] == expected

declarations = install[install.index("struct EDPTranscoderSite"):install.index("bool ready")]
code = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
#include <sys/mman.h>
DECLARATIONS
struct IORegistryEntry {
 static IORegistryEntry *getRegistryRoot(){static IORegistryEntry r; return &r;}
 void setProperty(const char *,bool){}
};
struct KernelPatcher {
 enum class Error {NoError,Failed};
 unsigned writes=0, failAt=~0U; bool badSymbol=false, error=false;
 uintptr_t solveSymbol(unsigned,const char *name,uintptr_t base,size_t) {
  for(const auto &s:sites)if(!strcmp(name,s.symbol))return base+s.function+badSymbol;
  return 0;
 }
 void routeBlock(uintptr_t a,const uint8_t *b,size_t n) {
  if(writes==failAt){error=true;return;} ++writes;memcpy((void*)a,b,n);
 }
 Error getError(){return error?Error::Failed:Error::NoError;}
 void clearError(){error=false;}
};
bool run(KernelPatcher &patcher,uintptr_t address,size_t size) {
 unsigned index=0;bool tglEDPTranscoderReady=false;
 INSTALL
 return tglEDPTranscoderReady;
}
int main(){
 const size_t size=0xe0000;
 std::vector<uint8_t> original(size,0xcc);
 for(const auto &s:sites)memcpy(original.data()+s.offset,s.expected,s.length);
 memcpy(original.data()+0x49ee9,timingExpected,sizeof(timingExpected));
 memcpy(original.data()+0x49dc4,zeroExit,sizeof(zeroExit));
 for(unsigned i=0;i<25;++i){
  auto b=original;KernelPatcher p;size_t n=size;
  if(i<21)b[sites[i].offset]^=1;
  if(i==21)b[0x49ee9]^=1;
  if(i==22)b[0x49dc4]^=1;
  if(i==23)n=0x57cde;
  if(i==24)p.badSymbol=true;
  assert(!run(p,(uintptr_t)b.data(),n)&&p.writes==0);
 }
 for(unsigned i=0;i<=21;++i){
  auto b=original;KernelPatcher p;p.failAt=i;
  assert(!run(p,(uintptr_t)b.data(),size)&&p.writes==i);
 }
 auto b=original;KernelPatcher p;
 assert(run(p,(uintptr_t)b.data(),size)&&p.writes==22);
 // Verify the complete diff is restricted to these instructions.
 auto expected=original;
 for(const auto &s:sites)memset(expected.data()+s.offset,0x90,s.length);
 memcpy(expected.data()+0x49ee9,timingReplacement,sizeof(timingReplacement));
 assert(b==expected);
 auto exec=(uint8_t*)mmap(nullptr,size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
 assert(exec!=MAP_FAILED);
 // Call each patched instruction with arbitrary pipe A/B/C/D. Preserve all
 // callee-saved registers used by the donor's inlined helpers.
 for(const auto &s:sites){
  if(s.length==2)continue; // branch tested separately below
  unsigned reg=(s.expected[0]==0x41?8:0)+(s.expected[s.length==6?1:0]-0xb8);
  for(unsigned mode=0;mode<2;++mode){
   std::vector<uint8_t> fn={0x53,0x41,0x54,0x41,0x55,0x41,0x57};
   fn.insert(fn.end(),{0x85,0xf6}); // test port type before overwriting esi
   fn.insert(fn.end(),{(uint8_t)(reg>=8?0x41:0x40),0x89,(uint8_t)(0xf8+(reg&7))}); // edi -> reg
   fn.insert(fn.end(),{0x75,s.length}); // external port skips override
   const uint8_t *site=(mode?b:original).data()+s.offset;
   fn.insert(fn.end(),site,site+s.length);
   fn.insert(fn.end(),{(uint8_t)(reg>=8?0x44:0x40),0x89,(uint8_t)(0xc0+((reg&7)<<3))});
   fn.insert(fn.end(),{0x41,0x5f,0x41,0x5d,0x41,0x5c,0x5b,0xc3});
   assert(!mprotect(exec,size,PROT_READ|PROT_WRITE));memcpy(exec,fn.data(),fn.size());
   assert(!mprotect(exec,size,PROT_READ|PROT_EXEC));
   auto call=(unsigned(*)(unsigned,unsigned))exec;
   for(unsigned pipe=0;pipe<4;++pipe){
    assert(call(pipe,0)==(mode?pipe:15));
    assert(call(pipe,1)==pipe);
   }
  }
 }
 // Execute SetupParams' eDP branch: valid ports must select DDI+1,
 // including eDP. The branch's native zero-clock target is kept as a stub.
 for(unsigned mode=0;mode<2;++mode){
  assert(!mprotect(exec,size,PROT_READ|PROT_WRITE));memset(exec,0x90,0x80);
  exec[0]=0x85;exec[1]=0xff;
  memcpy(exec+2,(mode?b:original).data()+0x4e7a3,2);
  const uint8_t select[]={0x89,0xf0,0xff,0xc0,0xc3};memcpy(exec+4,select,5);
  exec[0x43]=0x31;exec[0x44]=0xc0;exec[0x45]=0xc3;
  assert(!mprotect(exec,size,PROT_READ|PROT_EXEC));
  auto clock=(unsigned(*)(unsigned,unsigned))exec;
  for(unsigned ddi=0;ddi<9;++ddi)for(unsigned kind=0;kind<3;++kind)
   assert(clock(kind,ddi)==((mode||kind)?ddi+1:0));
 }
 // Execute the exact RIP-relative N guard. The native zero-output/unlock
 // block is represented by a distinguishable return; its entry bytes and
 // destination were checked above. Nonzero values must fall through.
 assert(!mprotect(exec,size,PROT_READ|PROT_WRITE));
 memcpy(exec+0x49ee9,timingReplacement,sizeof(timingReplacement));
 const uint8_t zeroReturn[]={0x31,0xc0,0xc3};
 const uint8_t goodReturn[]={0xb8,0x01,0x00,0x00,0x00,0xc3};
 memcpy(exec+0x49dc4,zeroReturn,sizeof(zeroReturn));
 memcpy(exec+0x49efb,goodReturn,sizeof(goodReturn));
 exec[0]=0x89;exec[1]=0xf8;exec[2]=0xe9;
 int32_t delta=0x49ee9-7;memcpy(exec+3,&delta,4);
 assert(!mprotect(exec,size,PROT_READ|PROT_EXEC));
 auto call=(unsigned(*)(unsigned))exec;
 assert(call(0)==0&&call(1)==1&&call(0xffffff)==1&&call(0xffffffff)==1);
 munmap(exec,size);
}
'''.replace("DECLARATIONS", declarations).replace("INSTALL", install)
with tempfile.TemporaryDirectory(prefix="tgl-edp-transcoder-") as directory:
    directory = Path(directory)
    (directory / "test.cpp").write_text(code)
    subprocess.run(["clang++", "-arch", "x86_64", "-std=c++17", "-O1",
                    str(directory / "test.cpp"), "-o", str(directory / "test")], check=True)
    subprocess.run([str(directory / "test")], check=True)
print("PASS: 20 transcoder sites and eDP clock selector; allocated pipes A-D; external ports unchanged; zero-N exit; preflight and write failures")
