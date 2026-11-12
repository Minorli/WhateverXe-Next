// SPDX-License-Identifier: MIT
#pragma once
#include <charconv>
#include <cstring>

namespace xe {
inline bool boundedCount(const char *text, int maximum, int &result) noexcept {
    if (!text || !*text) return false;
    int value=0;
    const char *end=text+std::strlen(text);
    const auto parsed=std::from_chars(text,end,value);
    if (parsed.ec!=std::errc{} || parsed.ptr!=end || value<1 || value>maximum) return false;
    result=value;
    return true;
}
}
