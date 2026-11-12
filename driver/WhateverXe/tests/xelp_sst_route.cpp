#include "kern_xelp_sst_route.hpp"
#include "kern_tgl_tc_boot.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>
using namespace XELP;

int main() {
    // Exercise composition, not machine connector lists: all 4^9 assignments
    // of absent / Combo / Dekel legacy / DP-alt, then each pipe independently.
    unsigned checked=0;
    for(unsigned assignment=0;assignment<(1U<<18);++assignment) {
        Display12SSTRoutes routes;
        for(unsigned d=0;d<9;++d) {
            const auto type=(assignment>>(2*d))&3;
            if(type==1)routes.combo|=1U<<d;
            if(type==2)routes.legacy|=1U<<d;
            if(type==3)routes.alt|=1U<<d;
        }
        for(unsigned d=0;d<9;++d) {
            const auto type=(assignment>>(2*d))&3;
            const unsigned expectedMode[]={0,1,4,3};
            const auto r=routes.external(d,expectedMode[type]);
            const bool valid=type && (type==1 || d>=3);
            assert(r.valid()==valid);
            if(!valid)continue;
            assert(r.needsLease() && !r.panel);
            assert(r.ddi==d && r.ddiBuffer==0x64000+0x100*d);
            assert(r.retainComboAUX()==(type==1));
            // A fixed DP AUX transaction must work with all pipes stopped.
            // DP-alt deliberately delegates that ownership to the TC backend.
            assert((r.auxParentPG!=0)==(type!=3));
            assert(r.auxParentPG==(type==3?0U:d<3?1U:3U));
            for(unsigned mode=0;mode<6;++mode) {
                Display12SSTRoute resolved;
                assert(routes.resolve(d,1,mode,resolved)==(mode==expectedMode[type]));
                assert(resolved.valid()==(mode==expectedMode[type]));
                assert(!routes.resolve(d,2,mode,resolved)); // HDMI is not SST.
            }
            // Reference domain unions: PW1 / PW1..3 / PW1..4.
            const unsigned masks[2][3]={{3,0x3f,0xff},{0x3f,0x3f,0xff}};
            for(unsigned pipe=0;pipe<3;++pipe)assert(r.mainPowerMask(pipe)==masks[d>=3][pipe]);
            assert(!r.mainPowerMask(3) && !r.mainPowerMask(~0U));
            ++checked;
        }
    }
    for(unsigned d=0;d<9;++d) {
        const auto bit=1U<<d;
        for(auto r:{Display12SSTRoutes{bit,bit,0},Display12SSTRoutes{bit,0,bit}})
            assert(!r.eligible(d)); // Ambiguous adapter selection cannot authorize IO.
        // Legacy and DP-alt are alternative live modes of the same TC port.
        const Display12SSTRoutes tc{0,bit,bit};
        assert(tc.external(d,3).valid()==(d>=3));
        assert(tc.external(d,4).valid()==(d>=3));
        assert(!tc.external(d,1).valid());
        Display12SSTRoute panel;
        assert(Display12SSTRoutes{}.resolve(d,0,1,panel)==(d<2));
        if(d<2)assert(panel.panel && !panel.needsLease() && !panel.auxParentPG && panel.retainComboAUX());
    }
    assert((!Display12SSTRoutes{~0U,0,0}.eligible(9)));
    assert((!Display12SSTRoutes{~0U,0,0}.eligible(~0U)));
    // Firmware intent and qualified backend are an intersection. Automatic
    // legacy registration supplies a route only after the native HPD decision.
    for(unsigned wiring=0;wiring<64;++wiring)for(unsigned backend=0;backend<64;++backend) {
        auto w=TGLTCWiring::fromMasks(wiring<<3,0,0);
        for(unsigned d=3;d<9;++d) {
            assert(w.sstRoutes(backend<<3,0,0).external(d,4).valid()==bool((wiring&backend)&(1U<<(d-3))));
            assert(w.sstRoutes(0,0,0).external(d,4).valid()==false);
        }
    }
    // Panel AUX owns core power, but the panel path never becomes an
    // external training-lease candidate. Mixed external ports stay separate.
    for(unsigned d=0;d<2;++d) {
        Display12SSTRoutes routes{0x18,0,0,1U<<d};
        auto p=routes.fixed(d);assert(p.panel && p.auxParentPG==1 && !routes.eligible(d));
        assert(routes.fixed(3).auxParentPG==3 && routes.eligible(3));
        routes.combo|=1U<<d;assert(!routes.fixed(d).valid());
    }
    std::printf("PASS %u SST routes across 4^9 transport assignments, all pipes/modes; ambiguous selection and backend intersection\n",checked);
}
