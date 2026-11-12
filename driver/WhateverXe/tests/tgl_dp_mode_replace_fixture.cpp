#include "kern_tgl_port_map.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#define FunctionCast(f,p) reinterpret_cast<decltype(&f)>(p)
struct IORegistryEntry{static IORegistryEntry*getRegistryRoot(){static IORegistryEntry r;return &r;}template<class...T>void setProperty(T...){}};
template<class...T>void tglTransitionRecord(T...){}
bool tglQuietTraining(){return true;}
struct XE {
 bool tglDPModeReplaceReady=true;
 uintptr_t orgTGLSetDklDPMode;
 bool tglDPSSTMode(unsigned d,unsigned m){return d>=3&&d<=6&&(m==3||m==4);}
 static void wrapTGLSetDklDPMode(void*);
 static bool tglDPAltFia(void*,TGLFiaPort&);
 static uint32_t wrapTGLPortReadReg(void*,unsigned);
 static void wrapTGLFIALaneCount(void*,unsigned);
 static bool tglDekelAccess(void*,unsigned,bool,uint32_t&);
} instance,*callbackXE=&instance;
std::map<unsigned,unsigned> mmio;unsigned nativeCalls=0,phyWrites=0,lanesWrites=0,pin=4;bool ready=true;
uint32_t phy[2];
bool XE::tglDPAltFia(void*p,TGLFiaPort&f){auto cfg=*reinterpret_cast<uint32_t**>((uint8_t*)p+0x548);TGLTypeCPort tc;return ready&&TGLTypeCPort::resolve(cfg[0],tc)&&tc.resolveFia(16,f);}
uint32_t XE::wrapTGLPortReadReg(void*,unsigned r){assert(mmio.count(r));return mmio[r];}
void XE::wrapTGLFIALaneCount(void*p,unsigned lanes){TGLFiaPort f;assert(tglDPAltFia(p,f));++lanesWrites;mmio[f.laneSelect]=(mmio[f.laneSelect]&~f.laneSelectMask)|(((1u<<lanes)-1)<<f.laneSelectShift);}
bool XE::tglDekelAccess(void*,unsigned i,bool write,uint32_t&v){assert(i==0x48||i==0x49);if(write){++phyWrites;phy[i-0x48]=v;}else v=phy[i-0x48];return true;}
void native(void*){++nativeCalls;phy[0]|=0xc0;phy[1]|=0xc0;phyWrites+=2;}
// PRODUCTION
int main(){
 alignas(8)uint8_t port[0x600]{};uint32_t cfg[8]{};*reinterpret_cast<uint32_t**>(port+0x548)=cfg;
 auto &mode=*reinterpret_cast<uint32_t*>(port+0x550),&lanes=*reinterpret_cast<uint32_t*>(port+0x124);
 cfg[3]=1;instance.orgTGLSetDklDPMode=reinterpret_cast<uintptr_t>(native);
 unsigned cases=0;
 for(unsigned d=3;d<=6;++d)for(unsigned width:{1u,2u,4u})for(unsigned assignment=1;assignment<=6;++assignment){
  cfg[0]=d;mode=3;lanes=width;TGLFiaPort f;assert(XE::tglDPAltFia(port,f));
  mmio[f.owned]=f.readyMask;mmio[f.live-0x20]=assignment<<(4*f.index);mmio[f.laneSelect]=0xabcddcba;
  phy[0]=0x12345abc;phy[1]=0x87654321;nativeCalls=phyWrites=lanesWrites=0;
  uint32_t expected[2];assert(TGLDklDPMode::select(assignment,width,expected));
  XE::wrapTGLSetDklDPMode(port);
  assert(!nativeCalls&&phyWrites==2&&lanesWrites==1);
  assert(phy[0]==((0x12345abcu&~0xc0u)|expected[0]));assert(phy[1]==((0x87654321u&~0xc0u)|expected[1]));
  ++cases;
 }
 // Missing qualification preserves the predecessor's native+correction path.
 instance.tglDPModeReplaceReady=false;nativeCalls=0;XE::wrapTGLSetDklDPMode(port);assert(nativeCalls==1);
 instance.tglDPModeReplaceReady=true;mode=4;nativeCalls=0;XE::wrapTGLSetDklDPMode(port);assert(nativeCalls==1);
 mode=2;nativeCalls=0;XE::wrapTGLSetDklDPMode(port);assert(nativeCalls==1);
 mode=3;ready=false;nativeCalls=phyWrites=0;XE::wrapTGLSetDklDPMode(port);assert(!nativeCalls&&!phyWrites);
 printf("PASS %u production DP-alt DP_MODE cases: one write per bank, preserved non-mode bits, qualified fallback, unavailable FIA blocks writes\n",cases);
}
