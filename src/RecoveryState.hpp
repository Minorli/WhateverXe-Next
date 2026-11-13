// SPDX-License-Identifier: MIT
// Decode only the known r123 live wire layout. No GPU or IOKit dependency.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace xe {
enum class RecoveryState { Unknown, Clean, Recovered, Inconsistent };
struct RecoverySnapshot {
    RecoveryState state=RecoveryState::Unknown;
    uint64_t calls=0,returns=0;
};
// A clean counter record is only one witness. An independent failure or a
// gap in report visibility must not be labelled a clean comparison baseline.
inline bool comparisonBaselineClean(const RecoverySnapshot &snapshot,
                                    bool reportsContinuous,bool failureData) {
    return snapshot.state==RecoveryState::Clean && reportsContinuous && !failureData;
}
constexpr size_t RecoveryWireWords=2+16*16;
constexpr size_t RecoveryWireBytes=RecoveryWireWords*sizeof(uint64_t);
inline RecoverySnapshot recoverySnapshot(const void *bytes,size_t size,bool ready) {
    RecoverySnapshot result;
    if(!ready || !bytes || size!=RecoveryWireBytes)return result;
    const auto *p=static_cast<const uint8_t *>(bytes);
    memcpy(&result.calls,p,8);memcpy(&result.returns,p+8,8);
    if(result.calls || result.returns) {
        // Even matching calls/returns say nothing about hardware progress.
        result.state=RecoveryState::Recovered;return result;
    }
    // A clean boot has neither attempts nor published recovery records.
    // Do not accept a zero prefix from an unknown or inconsistent layout.
    for(size_t n=16;n<size;++n)if(p[n]) {
        result.state=RecoveryState::Inconsistent;return result;
    }
    result.state=RecoveryState::Clean;return result;
}
inline const char *recoveryStateName(RecoveryState state) {
    switch(state) {
        case RecoveryState::Unknown:return "unknown";
        case RecoveryState::Clean:return "clean";
        case RecoveryState::Recovered:return "recovered";
        case RecoveryState::Inconsistent:return "inconsistent";
    }
    return "unknown";
}
}
