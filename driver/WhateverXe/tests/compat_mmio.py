#!/usr/bin/env python3
"""Exercise actual Compat accessors at the last valid MMIO element and beyond."""
from pathlib import Path
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[1]
source = (repo / 'AppleIntelTGLCompat/AppleIntelTGLCompat.cpp').read_text()

def function(signature):
    start = source.index(signature)
    return source[start:source.index('\n}', start) + 2]

fixture = r'''
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cassert>
#include <cstring>
#define IOLog(...)
template <typename T> T &field(void *object, size_t offset) {
    return *reinterpret_cast<T *>(static_cast<uint8_t *>(object) + offset);
}
bool admitted=true;
bool hardwareAllowed(void *) { return admitted; }
void *manager(void *controller) { return controller; }
unsigned dispatches=0;
void nativeWrite64(void *access,unsigned long offset,uint64_t value) {
    assert(offset <= field<uint32_t>(access,0x60)-8);
    ++dispatches;
    *reinterpret_cast<volatile uint64_t *>(field<volatile uint8_t *>(access,0x50)+offset)=value;
}
template <typename F> F method(void *,size_t slot) {
    assert(slot==0x138); return reinterpret_cast<F>(nativeWrite64);
}
'''
for signature in (
    'static uint32_t read32(void *access, unsigned long offset) {',
    'extern "C" uint64_t tglRead64(void *controller, volatile void *base, unsigned long offset) {',
    'extern "C" void tglWrite64(void *controller, volatile void *base, unsigned long offset,',
):
    fixture += function(signature) + '\n'
fixture += r'''
int main() {
    alignas(8) uint8_t access[0x68]{};
    alignas(8) uint64_t mapping[2]{0x1122334455667788ULL,0x99AABBCCDDEEFF00ULL};
    alignas(8) uint64_t alternate[2]{};
    field<volatile uint8_t *>(access,0x50)=reinterpret_cast<volatile uint8_t *>(mapping);
    auto &size=field<uint32_t>(access,0x60);
    size=sizeof(mapping);
    // Last four/eight bytes belong to the mapping; an end-equal span is valid.
    if(read32(access,12)!=0x99AABBCCU) {
        fputs("FAIL: read32 rejects last valid element\n",stderr);return 1;
    }
    assert(read32(access,0)==0x55667788U);
    assert(read32(access,13)==0 && read32(access,16)==0 && read32(access,~0UL)==0);
    assert(tglRead64(access,mapping,8)==mapping[1]);
    assert(tglRead64(access,mapping,9)==0 && tglRead64(access,mapping,~0UL)==0);
    tglWrite64(access,alternate,8,0x123456789ABCDEF0ULL);
    assert(alternate[1]==0x123456789ABCDEF0ULL);
    tglWrite64(access,alternate,9,1);tglWrite64(access,alternate,~0UL,1);
    assert(alternate[0]==0 && alternate[1]==0x123456789ABCDEF0ULL);
    // Keep the native DSB-capable dispatch for the manager's own base.
    tglWrite64(access,mapping,8,0xFFEEDDCCBBAA9988ULL);
    assert(dispatches==1 && mapping[1]==0xFFEEDDCCBBAA9988ULL);
    size=4;assert(read32(access,0)==0x55667788U && read32(access,1)==0);
    size=8;assert(tglRead64(access,mapping,0)==mapping[0]);
    tglWrite64(access,alternate,0,7);assert(alternate[0]==7);
    for(size=0;size<4;++size)assert(read32(access,0)==0);
    for(size=0;size<8;++size) {
        assert(tglRead64(access,mapping,0)==0);
        tglWrite64(access,alternate,0,9);assert(alternate[0]==7);
    }
    admitted=false;size=16;
    assert(tglRead64(access,mapping,0)==0);
    tglWrite64(access,alternate,0,9);assert(alternate[0]==7);
    tglWrite64(access,mapping,0,9);assert(dispatches==1);
    puts("PASS Compat MMIO: last valid elements, undersized/out-of-range spans, native dispatch and hardware rejection");
}
'''
with tempfile.TemporaryDirectory(prefix='whxe-compat-mmio-') as directory:
    root = Path(directory)
    (root / 'test.cpp').write_text(fixture)
    subprocess.run(['clang++', '-std=c++17', '-fsanitize=address,undefined',
                    str(root / 'test.cpp'), '-o', str(root / 'test')], check=True)
    subprocess.run([str(root / 'test')], check=True)
