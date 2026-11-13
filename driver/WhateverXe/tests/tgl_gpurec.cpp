// CPU contract for -wex-tgl-gpurec (kern_tgl_gpurec.hpp).
// Register values below are from i7-1165G7 (9A49) gpuRestart reports:
// a healthy RCS vs. an RCS left at reset defaults after a sibling reset.
#include "kern_tgl_gpurec.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>
using namespace TGLGpuRec;
struct Entry {uint32_t reg,value;uint8_t masked,pad[0x24-9];};
static_assert(sizeof(Entry)==EntrySize,"native resetRegisterInfo stride");
// TGLGpuRecLive revision 2 appends after WhitelistFixes; revision-1 positions stay.
static_assert(Resets==0 && SiblingReplays==2 && LastRCU==6 && Last==7 && ReservedStampDeclined==12 && WhitelistFixes==13,
 "revision 1 positions");
static_assert(LiveRevision==2 && PreFixRCU==14 && PreFixReads==15 && PocsBefore==16 && PocsAfter==17 &&
 Pocs88Before==18 && Pocs88After==19 && SiblingResubmits==20 && SiblingHalts==21 && HaltNotReady==22 &&
 SiblingResumes==23 && Slots==24,"TGLGpuRecLive revision 2 layout");
static_assert(RegistersFlag==1 && ReservedStampsFlag==2 && HaltFlag==4 && WithPOSHFlag==8,"TGLGpuRecLive flag word");
static_assert(Pocs[0]==0x18080 && Pocs[1]==0x18088 && ResetControl[0]==0x20D0 && ResetControl[1]==0x1A0D0,"registers");
// Register file standing in for the MMIO BAR; unwritten registers read 0.
struct MMIO {
 std::map<uint32_t,uint32_t> r;std::vector<uint32_t> order;
 uint32_t read(uint32_t a) const {auto i=r.find(a);return i==r.end()?0:i->second;}
 void write(uint32_t a,uint32_t v) {r[a]=v;order.push_back(a);}
};
int main() {
 // Native IGVector<resetRegisterInfo>.
 std::vector<Entry> rcs(3);rcs[0]={0x2080,0,0,{}};rcs[1]={0x229C,0,1,{}};rcs[2]={0x24D0,0,0,{}};
 struct {uint64_t count,capacity;const void *data;} vector {rcs.size(),rcs.size(),rcs.data()};
 List l {};assert(list(&vector,0,l) && l.count==3 && reg(l,1)==0x229C && masked(l,1) && !masked(l,2));
 assert(!list(&vector,1,l));                         // CCS list must start with 0x1A080
 vector.count=0;assert(!list(&vector,0,l));vector.count=65;assert(!list(&vector,0,l));
 vector.count=3;vector.data=nullptr;assert(!list(&vector,0,l));assert(!list(nullptr,0,l));
 vector.data=rcs.data();assert(list(&vector,0,l));
 // Native restore rule: default wins, masked gets all enables.
 assert(restoreValue(0,0x8,true)==0xFFFF0008 && restoreValue(0x80,0x8,false)==0x80 && restoreValue(0,0xE100,false)==0xE100);
 // RCU_MODE: CCS enabled at start (accel+0x1190 bit 33) but cleared by GDRST.
 const uint64_t ccsOn=1ULL<<33;assert(ccsEnabled(ccsOn)==1 && ccsEnabled(0)==0);
 const uint32_t good[5] {0x1,0x1,0xB134,0xFFFFFFFE,0xFFFFFFFE};
 uint32_t live[5] {0x0,0x0,0x0,0xFFFFFFFF,0xFFFFFFFF},value[5] {};
 assert(healthy(good,1) && !healthy(live,1));
 const uint32_t m=plan(live,good,1,value);
 assert(m==0x1F && value[0]==0x10001 && value[1]==0xFFFF0001 && value[2]==0xB134 && value[3]==0xFFFFFFFE);
 assert(plan(good,good,1,value)==0);                 // idempotent
 uint32_t off[5] {0,0,0,0xFFFFFFFF,0xFFFFFFFF};
 assert(plan(off,good,0,value)==0);                  // CCS off: native start writes none of these
 off[0]=1;assert(plan(off,good,0,value)==1 && value[0]==0x10000);
 assert(plan(live,nullptr,1,value)==1);              // no trusted snapshot: only the native RCU write
 // RCS whitelist: an own-RCS restore after a sibling reset left
 // FORCE_TO_NONPRIV0 at 0xE000 (healthy: 0xE100).
 uint32_t wl[9];for(unsigned i=0;i<9;++i)wl[i]=Whitelist[i].value;
 assert(whitelistPlan(wl,true,true)==0);
 wl[0]=0xE000;assert(whitelistPlan(wl,true,true)==1 && whitelistPlan(wl,false,true)==0);
 wl[0]=0xE100;wl[8]=0;assert(whitelistPlan(wl,true,false)==0 && whitelistPlan(wl,true,true)==0x100);

 // Ordering fix. A CCS-own reset: GDRST leaves the RCS at hardware defaults
 // (FORCE_TO_NONPRIV0 0xE000), the extras pass rewrites the whitelist, then
 // the sibling RCS list is restored from a snapshot taken after an earlier
 // wipe (0xE000 too). getDefaultResetValue knows no NONPRIV register.
 {
  // Native order inside rcsRegisters: HWS_PGA first, NONPRIV0 before GFX_MODE here.
  std::vector<Entry> sibling(3);sibling[0]={0x2080,0,0,{}};sibling[1]={0x24D0,0,0,{}};sibling[2]={0x229C,0,1,{}};
  struct {uint64_t count,capacity;const void *data;} siblingVector {sibling.size(),sibling.size(),sibling.data()};
  List sl {};assert(list(&siblingVector,0,sl) && sl.count==3);
  MMIO mmio;
  for(const auto &p:Whitelist)mmio.r[p.reg]=p.value;
  mmio.r[0x24D0]=0xE000;mmio.r[0x2080]=0;mmio.r[0x229C]=0;
  auto read=[&mmio](uint32_t r) {return mmio.read(r);};
  auto write=[&mmio](uint32_t r,uint32_t v) {mmio.write(r,v);};
  auto none=[](uint32_t) {return 0U;};
  assert(fixWhitelist(read,write,true,true)==1 && mmio.read(0x24D0)==0xE100);   // extras pass
  const uint32_t snapshot[3] {0x7F000,0xE000,0x8};
  MMIO before=mmio;
  // Revision 1 order: the list write is the last NONPRIV writer and undoes the fix.
  writeList(sl,snapshot,none,[&before](uint32_t r,uint32_t v) {before.write(r,v);});
  assert(before.read(0x24D0)==0xE000 && before.order.back()==0x229C);
  // Revision 2: the whitelist is re-applied after every list restore.
  assert(restoreList(sl,snapshot,none,read,write,true,true)==1);
  assert(mmio.read(0x24D0)==0xE100 && mmio.read(0x2080)==0x7F000 && mmio.read(0x229C)==0xFFFF0008);
  assert(mmio.order.back()==0x24D0);                 // the whitelist is the last writer
  // A healthy snapshot needs no rewrite; a native default still wins.
  const uint32_t healthySnapshot[3] {0x7F000,0xE100,0x8};
  assert(restoreList(sl,healthySnapshot,[](uint32_t r) {return r==0x229C?0x80U:0U;},read,write,true,true)==0);
  assert(mmio.read(0x24D0)==0xE100 && mmio.read(0x229C)==0xFFFF0080);
  // initModeRegisters did not program the whitelist (accel+0x1191 bit 0 clear):
  // leave the restored value alone, as native start would.
  assert(restoreList(sl,snapshot,none,read,write,false,true)==0 && mmio.read(0x24D0)==0xE000);
  // Optional entries 7/8 only with accel+0xFE6 bit 25.
  mmio.r[0x24FC]=0;
  assert(fixWhitelist(read,write,true,false)==1 && mmio.read(0x24FC)==0);
  assert(fixWhitelist(read,write,true,true)==0x100 && mmio.read(0x24FC)==0x10002341 && mmio.read(0x24D0)==0xE100);
 }

 // Sibling replay: processGPUReset only resubmits with an active execlist element.
 {
  alignas(8) uint8_t streamer[0x60] {};
  uint32_t port=0;void *element=&port;
  assert(!activeElement(nullptr) && !activeElement(streamer));
  memcpy(streamer+StreamerElements+ElementStride,&element,sizeof(element));
  assert(!activeElement(streamer));                  // port 0 is empty
  port=1;memcpy(streamer+StreamerPort,&port,sizeof(port));assert(activeElement(streamer));
  port=2;memcpy(streamer+StreamerPort,&port,sizeof(port));assert(!activeElement(streamer)); // never a native port
  port=0;memcpy(streamer+StreamerPort,&port,sizeof(port));
  memcpy(streamer+StreamerElements,&element,sizeof(element));assert(activeElement(streamer));
 }

 // -wex-tgl-gpurec-halt: halt only before a plain GDRST of an enabled engine;
 // resume only a request that survived the reset.
 assert(haltWanted(true,false,true) && !haltWanted(false,false,true) && !haltWanted(true,true,true) && !haltWanted(true,false,false));
 assert(haltReady(RequestReset|ReadyToReset) && haltReady(ReadyToReset) && !haltReady(RequestReset) && !haltReady(0) && !haltReady(0xFFFFFFFF));
 assert(resumeWanted(RequestReset) && resumeWanted(RequestReset|ReadyToReset) && !resumeWanted(0) && !resumeWanted(ReadyToReset));
 assert(!resumeWanted(0xFFFFFFFF));                  // dead BAR: nothing to resume

 puts("tgl_gpurec: reset lists, restore order, RCU/extras, live layout and replay/halt contracts ok; no stamp completion");
 return 0;
}
