// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
namespace xe {
// Caller must first verify the complete file, mapped page, methods and ivar ABI.
// This emits only original replacement instructions, no donor code or payload.
inline bool makePOSHAtomicPage(uint8_t *page,size_t length,int32_t active,int32_t only) {
    if (!page || length!=4096 || active<0 || (active&3) || only<0) return false;
    // RAX=queue, RBX=CB; EDX is dead. ZF remains the original zero predicate.
    const uint8_t claim[]={0xba,1,0,0,0,0xf0,0x0f,0xc1,0x90,0,0,0,0,
        0x85,0xd2,0x0f,0x94,0x83,0,0,0,0};
    memset(page+0x9b0,0x90,0x19);
    memcpy(page+0x9b0,claim,sizeof(claim));
    memcpy(page+0x9b0+9,&active,4);
    memcpy(page+0x9b0+18,&only,4);
    // Remove the original later queue reload / non-atomic increment.
    memset(page+0xa3d,0x90,10);
    // RCX=queue; RDX is dead. lock decl [rcx+active].
    const uint8_t release[]={0xf0,0xff,0x89,0,0,0,0};
    memset(page+0xb7e,0x90,13);
    memcpy(page+0xb7e,release,sizeof(release));
    memcpy(page+0xb7e + 3,&active,4);
    return true;
}
}
