// SPDX-License-Identifier: MIT
#include "../src/BoundedCount.hpp"
#include <cassert>
#include <climits>
int main() {
    int count=17;
    for (const char *bad: {"", "0", "-1", "+1", "1x", " 1", "1 ", "10001", "9999999999999999999999"}) {
        assert(!xe::boundedCount(bad,10000,count));
        assert(count==17);
    }
    assert(!xe::boundedCount(nullptr,10000,count));
    assert(xe::boundedCount("1",10000,count) && count==1);
    assert(xe::boundedCount("10000",10000,count) && count==10000);
    assert(xe::boundedCount("2147483647",INT_MAX,count) && count==INT_MAX);
}
