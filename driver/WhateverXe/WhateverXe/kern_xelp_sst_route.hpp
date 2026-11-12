// Display12 SST routing after firmware, physical platform and donor admission.
// A connector label (notably USB-C) is not a PHY or a power-domain selector.
// Domain relationships follow Linux v6.12 intel_display_power_map.c.
#ifndef KERN_XELP_SST_ROUTE_HPP
#define KERN_XELP_SST_ROUTE_HPP
#include <stdint.h>

namespace XELP {
struct Display12SSTRoute {
    enum class Transport : uint8_t { None, Combo, DekelLegacy, DekelAlt };
    uint32_t ddi {}, ddiBuffer {}, mode {}, lanePG {}, auxParentPG {};
    Transport transport {Transport::None};
    bool panel {};
    bool valid() const {return transport!=Transport::None;}
    bool needsLease() const {return valid() && !panel;}
    bool retainComboAUX() const {return transport==Transport::Combo;}
    uint32_t lanePowerMask() const {return valid()?(1U<<(2*lanePG))-1:0;}
    uint32_t powerGroup(uint32_t pipe) const {
        // Pipe A/B/C require PW1/PW3/PW4, independently of lane consumers.
        if(!valid() || pipe>=3)return 0;
        const uint32_t pipePG[]={1,3,4};
        return lanePG>pipePG[pipe]?lanePG:pipePG[pipe];
    }
    uint32_t mainPowerMask(uint32_t pipe) const {
        const auto pg=powerGroup(pipe);
        return pg?(1U<<(2*pg))-1:0;
    }
};

// These are qualified backend selections, not raw VBT masks. The platform
// adapter retains responsibility for identity, physical mapping and native ABI.
// Keeping the three transports separate prevents TC-numbered Combo ports from
// accidentally acquiring Dekel/FIA semantics.
struct Display12SSTRoutes {
    uint32_t combo {}, legacy {}, alt {}, panel {};
    Display12SSTRoute external(uint32_t ddi,uint32_t mode) const {
        Display12SSTRoute r;
        if(ddi>=9)return r;
        const auto bit=1U<<ddi;
        // A TC-capable receptacle may offer both legacy DP and DP-alt. Only
        // the acquired native mode selects the live transport; candidates do
        // not imply ownership. Combo and Dekel on the same DDI are conflicting.
        if((combo&bit) && ((legacy|alt)&bit))return r;
        const bool c=mode==1 && (combo&bit),l=mode==4 && (legacy&bit),a=mode==3 && (alt&bit);
        if((!c && !l && !a) || (!c && ddi<3))return r;
        r.ddi=ddi;r.ddiBuffer=0x64000+0x100*ddi;
        r.transport=c?Display12SSTRoute::Transport::Combo:
            l?Display12SSTRoute::Transport::DekelLegacy:Display12SSTRoute::Transport::DekelAlt;
        r.mode=c?1:l?4:3;
        r.lanePG=ddi<3?1:3;
        // DP-alt uses its TC ownership/power lifecycle. Fixed DP needs an
        // explicit parent ref even with no active pipe, including Combo A.
        r.auxParentPG=a?0:r.lanePG;
        return r;
    }
    Display12SSTRoute fixed(uint32_t ddi) const {
        if(ddi<2 && (panel&(1U<<ddi))) {
            if((combo|legacy|alt)&(1U<<ddi))return {};
            Display12SSTRoute r;
            if(resolve(ddi,0,1,r)){r.auxParentPG=1;return r;}
        }
        const auto r=external(ddi,1);
        return r.valid()?r:external(ddi,4);
    }
    bool eligible(uint32_t ddi) const {return fixed(ddi).needsLease() || external(ddi,3).needsLease();}
    bool resolve(uint32_t ddi,uint32_t kind,uint32_t mode,Display12SSTRoute &out) const {
        out={};
        // Preserve the qualified donor's native Combo panel path. Panel
        // sequence/backlight admission is upstream, not inferred from DP.
        if(ddi<2 && kind==0 && mode==1) {
            out.ddi=ddi;out.ddiBuffer=0x64000+0x100*ddi;out.mode=1;out.lanePG=1;
            out.transport=Display12SSTRoute::Transport::Combo;out.panel=true;
            return true;
        }
        const auto r=external(ddi,mode);
        if(kind!=1 || !r.valid() || mode!=r.mode)return false;
        out=r;return true;
    }
};
}
#endif
