// SPDX-License-Identifier: MIT
#include "../src/RecoveryState.hpp"
#include <array>
#include <cassert>
#include <cstdio>
int main() {
    using xe::RecoveryState;
    std::array<uint8_t,xe::RecoveryWireBytes+8> bytes{};
    const auto size=xe::RecoveryWireBytes;
    assert(xe::recoverySnapshot(bytes.data(),size,true).state==RecoveryState::Clean);
    for(size_t n=0;n<bytes.size();++n)if(n!=size)
        assert(xe::recoverySnapshot(bytes.data(),n,true).state==RecoveryState::Unknown);
    assert(xe::recoverySnapshot(nullptr,size,true).state==RecoveryState::Unknown);
    assert(xe::recoverySnapshot(bytes.data(),size,false).state==RecoveryState::Unknown);
    // Counter equality after reset used to be confused with recovered progress.
    for(uint64_t calls:{0ULL,1ULL,16ULL,100000ULL})for(uint64_t returns:{0ULL,1ULL,16ULL,100000ULL}) {
        memcpy(bytes.data(),&calls,8);memcpy(bytes.data()+8,&returns,8);
        const auto result=xe::recoverySnapshot(bytes.data(),size,true);
        assert(result.calls==calls && result.returns==returns);
        assert(result.state==((calls || returns)?RecoveryState::Recovered:RecoveryState::Clean));
    }
    bytes.fill(0);
    for(size_t n=16;n<size;++n) {
        bytes[n]=1;
        assert(xe::recoverySnapshot(bytes.data(),size,true).state==RecoveryState::Inconsistent);
        bytes[n]=0;
    }
    // CFData byte storage need not have uint64_t alignment.
    assert(xe::recoverySnapshot(bytes.data()+1,size,true).state==RecoveryState::Clean);
    for(auto state:{RecoveryState::Unknown,RecoveryState::Clean,RecoveryState::Recovered,RecoveryState::Inconsistent})
        for(bool visible:{false,true})for(bool failure:{false,true}) {
            const xe::RecoverySnapshot snapshot{state,0,0};
            assert(xe::comparisonBaselineClean(snapshot,visible,failure)==
                   (state==RecoveryState::Clean && visible && !failure));
        }
    puts("PASS: missing/truncated/extended/unready/recovered/inconsistent/unaligned recovery data; CPU only");
}
