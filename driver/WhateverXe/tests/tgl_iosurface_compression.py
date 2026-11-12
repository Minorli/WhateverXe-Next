#!/usr/bin/env python3
"""Execute both guarded donor flag writers and their replacements on x86_64."""
from pathlib import Path
import re, subprocess, sys, tempfile
root = Path(__file__).resolve().parents[1]
s = (root / 'WhateverXe/kern_xe.cpp').read_text()
def array(name):
    raw = re.search(r'const uint8_t ' + name + r'\[\] \{([^}]+)\}', s).group(1)
    return bytes(int(v.strip(), 0) for v in raw.split(','))
initial, setter = array('iosCCSInitial'), array('iosCCSSet')
native_initial, native_setter = array('iosCCSNative')[-8:], array('iosCCSSetNative')
if len(sys.argv) > 1:
    b = Path(sys.argv[1]).read_bytes()
    assert b[0x246C3:0x246E1] == array('iosCCSNative')
    assert b[0x2B872:0x2B87C] == native_setter
    assert b[0x959D1:0x959E3] == b'renderCompression\0'
# The replaced store is the only initialized byte; setter must ignore both
# true and false requests without changing the caller's request or other state.
code = r'''
#include <sys/mman.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
void run(const uint8_t *bytes,unsigned length,bool init) {
 uint8_t *m=(uint8_t *)mmap(nullptr,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);assert(m!=MAP_FAILED);
 const uint8_t entry[]={0x41,0x55,0x41,0x56,0x41,0x57,0x49,0x89,0xfd,0x49,0x89,0xfe,0x49,0x89,0xf7};
 const uint8_t exit[]={0x41,0x5f,0x41,0x5e,0x41,0x5d,0xc3};
 memcpy(m,entry,sizeof(entry));memcpy(m+sizeof(entry),bytes,length);memcpy(m+sizeof(entry)+length,exit,sizeof(exit));
 assert(mprotect(m,4096,PROT_READ|PROT_EXEC)==0);
 for(unsigned v=0;v<256;++v){uint8_t owner[0x1500],wanted[0x1500],request=v;memset(owner,0xa5,sizeof(owner));memcpy(wanted,owner,sizeof(owner));wanted[0x1474]=init?0:v;
  ((void(*)(void*,void*))m)(owner,&request);assert(!memcmp(owner,wanted,sizeof(owner)) && request==v);}
 munmap(m,4096);
}
int main(){
 ARRAYS
 run(setNative,sizeof(setNative),false);
 run(setPatched,sizeof(setPatched),true);
 run(initialPatched,sizeof(initialPatched),true);
 puts("PASS native setter and uncompressed initial/dynamic policy: 256 requests, adjacent bytes and input preserved");
}
'''
arrays = '\n'.join('const uint8_t ' + n + '[]={' + ','.join(map(str, a)) + '};' for n, a in [('setNative', native_setter), ('setPatched', setter), ('initialPatched', initial)])
with tempfile.TemporaryDirectory() as d:
    p = Path(d); (p/'test.cpp').write_text(code.replace('ARRAYS', arrays))
    subprocess.run(['clang++', '-arch', 'x86_64', str(p/'test.cpp'), '-o', str(p/'test')], check=True)
    subprocess.run([str(p/'test')], check=True)
