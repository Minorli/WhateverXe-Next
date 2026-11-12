// SPDX-License-Identifier: MIT
// Execute the actual emitted x86 instructions, not a C++ counter substitute.
// Synthetic objects only; no Apple binary, Metal device or GPU submission.
#include "../src/POSHAtomicCode.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <thread>
#include <sys/mman.h>

struct Queue { uint64_t guard[2];uint32_t active; };
struct Buffer { uint64_t guard;uint8_t only; };
static_assert(offsetof(Queue,active)==16 && offsetof(Buffer,only)==8);
int main() {
    std::array<uint8_t,4096> original,code;
    original.fill(0xcc);code=original;
    assert(!xe::makePOSHAtomicPage(nullptr,4096,16,8));
    assert(!xe::makePOSHAtomicPage(code.data(),4095,16,8));
    assert(!xe::makePOSHAtomicPage(code.data(),4096,-4,8));
    assert(!xe::makePOSHAtomicPage(code.data(),4096,17,8));
    assert(!xe::makePOSHAtomicPage(code.data(),4096,16,-1));
    assert(code==original);
    assert(xe::makePOSHAtomicPage(code.data(),code.size(),16,8));
    for (size_t i=0;i<code.size();++i) {
        const bool replaced=(i>=0x9b0 && i<0x9c9) || (i>=0xa3d && i<0xa47) || (i>=0xb7e && i<0xb8b);
        if (!replaced) assert(code[i]==original[i]);
    }
    // Preserve RBX, translate the C ABI to the native live registers, and
    // return the ZF value that the unchanged donor JE will consume.
    const uint8_t enter[]={0x53,0x48,0x89,0xf8,0x48,0x89,0xf3,0xe9,0,0,0,0};
    memcpy(code.data()+0x20,enter,sizeof(enter));int32_t jump=0x9b0-(0x20+sizeof(enter));memcpy(code.data()+0x28,&jump,4);
    const uint8_t exit[]={0x0f,0x94,0xc0,0x0f,0xb6,0xc0,0x5b,0xc3};memcpy(code.data()+0x9c9,exit,sizeof(exit));
    const uint8_t releaseEntry[]={0x48,0x89,0xf9,0xe9,0,0,0,0};
    memcpy(code.data()+0x100,releaseEntry,sizeof(releaseEntry));jump=0xb7e - (0x100+sizeof(releaseEntry));
    memcpy(code.data()+0x104,&jump,4);code[0xb8b]=0xc3;
    auto *memory=static_cast<uint8_t *>(mmap(nullptr,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0));
    assert(memory!=MAP_FAILED);memcpy(memory,code.data(),code.size());assert(!mprotect(memory,4096,PROT_READ|PROT_EXEC));
    auto claim=reinterpret_cast<unsigned (*)(Queue *,Buffer *)>(memory+0x20);
    auto release=reinterpret_cast<void (*)(Queue *)>(memory+0x100);
    constexpr unsigned rounds=300,workers=4;
    for (unsigned n=0;n<rounds;++n) {
        Queue queue{{0x12345678,0xabcdef},0};Buffer buffers[workers]{};unsigned predicates[workers]{};
        std::array<std::thread,workers> threads;
        for (unsigned i=0;i<workers;++i) threads[i]=std::thread([&,i]{buffers[i].guard=0x987654;predicates[i]=claim(&queue,&buffers[i]);});
        for (auto &t:threads) t.join();
        unsigned only=0;
        for (unsigned i=0;i<workers;++i) {assert(predicates[i]==buffers[i].only);only+=predicates[i];assert(buffers[i].guard==0x987654);}
        assert(only==1 && queue.active==workers);
        for (auto &t:threads) t=std::thread([&]{release(&queue);});
        for (auto &t:threads) t.join();
        assert(queue.active==0 && queue.guard[0]==0x12345678 && queue.guard[1]==0xabcdef);
    }
    assert(!munmap(memory,4096));
    printf("PASS: emitted claim/ZF/flag and concurrent release, %u x %u; guards/bounds/unchanged bytes; no GPU\n",rounds,workers);
}
