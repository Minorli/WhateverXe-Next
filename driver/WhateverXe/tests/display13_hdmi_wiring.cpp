#include <cassert>
#include <cstdio>
#include <type_traits>
#include "kern_display13_fb_topology.hpp"
using namespace Display13FBTopology;
static_assert(!std::is_convertible<Display13OutputWiring,TGLHDMIWiring>::value,"Do not silently drop DP policy through a legacy HDMI API");
int main() {
    for(unsigned mask=1;mask<=7;++mask) {
        const uint32_t legacy[] {1,mask,(mask&1)?1U:0U,(mask&2)?2U:0U,(mask&4)?3U:0U};
        const auto w=Display13HDMIWiring::fromProperty(legacy,sizeof(legacy));
        assert(w.valid && selectedWiring(w));
        assert(supportedWiring(w)==!(mask&4));
    }
    // Previously proposed version2 TC HDMI data is deliberately unsupported,
    // including a version2 A/B-only record. Use the existing version1 ABI.
    for(unsigned mask=1;mask<128;++mask) {
        uint32_t retired[11] {2,mask};
        for(unsigned d=0;d<7;++d)if(mask&(1U<<d))retired[d+2]=d<3?d+1:d+6;
        assert(!Display13HDMIWiring::fromProperty(retired,sizeof(retired)).valid);
    }
    const uint32_t t8[] {1,3,1,2,0};
    const auto w=Display13HDMIWiring::fromProperty(t8,sizeof(t8));
    assert(supportedWiring(w) && bootDDI(w,0x88030000)==0 && bootDDI(w,0x90030000)==1);
    assert(bootDDI(w,0xA0030000)==UINT32_MAX); // T8 rear TC1 GOP must not be adopted.
    puts("PASS Combo-only HDMI: version1 A/B retained; Combo C not physically admitted; retired TC HDMI/version2 rejected.");
}
