#include "kern_alderlake_s.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>
using namespace XELP;
static_assert(ADLSComboResources::physicalMask()==0x79,"physical ADL-S ports");
static_assert(ADLSComboBackend::mask(Donor::C004FC1A)==0x19,"native Combo capacity");
int main() {
 const uint32_t physical[]={0,3,4,5,6};
 for(unsigned n=0;n<5;++n) {
  assert(ADLSComboResources::ddi(n)==physical[n]);assert(ADLSComboResources::phy(physical[n])==n);
  AlderLakeS::Port p;assert(AlderLakeS::Port::resolve(physical[n],p) && p.phy==n);
 }
 assert(ADLSComboResources::ddi(5)==9 && ADLSComboResources::ddi(~0U)==9);
 for(unsigned d=0;d<32;++d)for(auto donor:{Donor::Unknown,Donor::C004FC1A,Donor(2)}) {
  uint32_t slot=0;const bool known=donor==Donor::C004FC1A&&(d==0||d==3||d==4);
  assert(ADLSComboBackend::clockSlot(donor,d,slot)==known);
  // The native index replacement is d>>1, only within the selected capacity.
  assert(slot==(known?d>>1:~0U));
 }
 // All masks over the nine donor port records, independent of VBT protocol.
 // Physical D/E remain recognizable but never become usable native slots.
 for(unsigned mask=0;mask<512;++mask) {
  ProbeConfig c;c.profile=identify(0x46908086,12);c.combo=mask;
  c.comboBackend=ADLSComboBackend::mask(c.profile.donor);
  assert(c.valid()==(mask && !(mask&~0x19U)));
  c.comboBackend=ADLSComboResources::physicalMask();assert(!c.valid());
 }
 assert(!ADLSComboBackend::mask(Donor::Unknown));
 puts("PASS physical PHY mapping separate from donor slots: 96 slot cases and 512 provider masks, no D/E resource inference");
}
