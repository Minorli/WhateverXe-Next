// Bounded failure-only copies. Sequential CPU evidence, not an atomic GPU view.
#ifndef kern_tgl_csb_failure_hpp
#define kern_tgl_csb_failure_hpp
#include "kern_tgl_hang_pending.hpp"
namespace TGLCSBFailure {
constexpr uint64_t Schema=308;
struct Engine {
 // live[0..9], lengths[10..21], times[22..23], full owner flags/length,
 // HWSP tail/length. Consumers must check lengths before interpreting bytes.
 uint64_t q[28];
 uint8_t streamer[0x80],nativeHead[16],descriptor[0x80],hwsp[0xC0];
 uint8_t items[4][0x20],contexts[4][0xA0];
};
struct Capture {uint64_t schema,count;Engine engine[6];};
template<class Read>
void collect(Engine &d,Read read) {
 auto q=d.q;
 auto copy=[&](uint64_t p,void *out,unsigned bytes)->uint64_t {
  return TGLHangPending::kernelRange(p,bytes) ? read(p,out,bytes) : 0;
 };
 auto pointer=[](const uint8_t *p) {return TGLHangPending::u64(p);};
 q[10]=copy(q[0],d.streamer,sizeof(d.streamer));
 if(q[10]!=sizeof(d.streamer))return;
 q[11]=copy(q[0]+0x1C00,d.nativeHead,sizeof(d.nativeHead));
 q[12]=copy(pointer(d.streamer+0x28),d.descriptor,sizeof(d.descriptor));
 q[13]=copy(pointer(d.streamer+0x68),d.hwsp,sizeof(d.hwsp));
 const auto owner=pointer(d.streamer+0x10),hwsp=pointer(d.streamer+0x68);
 // The exact owner field is 64-bit. A byte drops both forcewake mode 0x1800
 // and dependency handling mode 0x18000, creating a misleading failure record.
 if(TGLHangPending::kernelRange(owner,0x1198))
  q[25]=copy(owner+0x1190,q+24,sizeof(q[24]));
 for(unsigned n=0;n<4;++n) {
  q[14+n]=copy(pointer(d.streamer+0x30+n*8),d.items[n],sizeof(d.items[n]));
  if(q[14+n]==sizeof(d.items[n]))
   q[18+n]=copy(pointer(d.items[n]),d.contexts[n],sizeof(d.contexts[n]));
 }
 if(TGLHangPending::kernelRange(hwsp,0xC0))q[27]=copy(hwsp+0xBC,q+26,4);
}
}
#endif
