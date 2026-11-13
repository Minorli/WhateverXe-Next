// First claimed native halt per engine. CPU copies only: no MMIO, allocation,
// polling, completion changes or claim that this is the first hardware fault.
#ifndef kern_tgl_hang_cpu_hpp
#define kern_tgl_hang_cpu_hpp
#include "kern_tgl_hang_pending.hpp"
namespace TGLHangCPU {
constexpr unsigned Engines=6,Slots=4;
constexpr uint64_t Schema=1;
// Exact admitted donor: IGScheduler5::getActiveContexts and streamer init.
constexpr uint8_t SchedulerLookup[] {
 0x55,0x48,0x89,0xe5,0x89,0xf0,0x48,0x8b,0xbc,0xc7,0xa0,0x0c,0,0,
 0x48,0x89,0xd6,0x48,0x89,0xca,0x4c,0x89,0xc1,0x5d,0xe9,0x0d,0x22,0,0};
constexpr uint8_t StreamerOwners[] {0x4c,0x89,0x73,0x10,0x4c,0x89,0x6b,0x18,0x4c,0x89,0x63,0x20};
constexpr uint8_t StreamerDescriptor[] {
 0x41,0x0f,0xb6,0xf7,0x48,0x6b,0xc6,0x79,0x48,0x8d,0x0d,0x03,0xa7,0x09,0,
 0x48,0x01,0xc1,0x48,0x89,0x4b,0x28};
struct Field {uint64_t value,bytes;};
struct Slot {
 Field item,context,contextOwner,contextEngine,id,descriptor,ring,ringOwner,
  ringContext,ringEngine,stamps,stampIndex,retired,writeIndex;
};
struct Witness {
 Field schedulerOwner,ownerScheduler,streamer,streamerOwner,streamerScheduler,
  descriptor,descriptorEngine,ownerFlags,nativeHead;
 Slot slots[Slots];
};
struct Capture {
 // schema, guard, begin/end ns, owner, scheduler, exact descriptor, engine,
 // native domains, claim ordinal, number of complete passes, changed.
 // guard: 1 invalid input; 2 identity; 3 incomplete/invalid row; 4 changed.
 uint64_t q[12];
 Witness pass[2];
};
static_assert(sizeof(Capture)==2176,"bounded CPU pre-STOP wire layout");
// Caller's preallocated destination is fresh; begin/end/ordinal stay external.
template<class Read>
void collect(Capture &c,uint64_t owner,uint64_t scheduler,uint64_t descriptor,
             unsigned engine,Read read) {
 c.q[0]=Schema;c.q[1]=1;c.q[4]=owner;c.q[5]=scheduler;c.q[6]=descriptor;c.q[7]=engine;
 if(engine>=Engines || !TGLHangPending::kernelRange(owner,0x1258) ||
    !TGLHangPending::kernelRange(scheduler,0xca0+Engines*8) ||
    !TGLHangPending::kernelRange(descriptor,0x79))return;
 auto get=[&](uint64_t p,unsigned offset,Field &f,unsigned n=8) {
  f={};
  if(!TGLHangPending::kernelRange(p,uint64_t(offset)+n))return false;
  f.bytes=read(p+offset,&f.value,n);
  return f.bytes==n;
 };
 for(unsigned pass=0;pass<2;++pass) {
  auto &w=c.pass[pass];c.q[1]=2;
  if(!get(scheduler,0x10,w.schedulerOwner) || w.schedulerOwner.value!=owner ||
     !get(owner,0x1250,w.ownerScheduler) || w.ownerScheduler.value!=scheduler ||
     !get(scheduler,0xca0+engine*8,w.streamer) ||
     !get(w.streamer.value,0x10,w.streamerOwner) || w.streamerOwner.value!=owner ||
     !get(w.streamer.value,0x20,w.streamerScheduler) || w.streamerScheduler.value!=scheduler ||
     !get(w.streamer.value,0x28,w.descriptor) || w.descriptor.value!=descriptor ||
     !get(descriptor,0,w.descriptorEngine,1) || w.descriptorEngine.value!=engine)return;
  c.q[1]=3;
  bool complete=get(owner,0x1190,w.ownerFlags) && get(w.streamer.value,0x1c04,w.nativeHead,4);
  for(unsigned i=0;i<Slots;++i) {
   auto &s=w.slots[i];
   if(!get(w.streamer.value,0x30+i*8,s.item)) {complete=false;continue;}
   if(!s.item.value)continue;
   // Stop each dependent chain on a failed read or mismatched identity. A
   // successful nofault copy does not pin storage or exclude address reuse.
   if(!get(s.item.value,0,s.context) ||
      !get(s.context.value,0x50,s.contextOwner) || s.contextOwner.value!=owner ||
      !get(s.context.value,0x6c,s.contextEngine,1) || s.contextEngine.value!=engine ||
      !get(s.context.value,0x7c,s.id,4) || !get(s.context.value,0x89,s.descriptor) ||
      !get(s.context.value,0xb0,s.ring) ||
      !get(s.ring.value,0x10,s.ringOwner) || s.ringOwner.value!=owner ||
      !get(s.ring.value,0x20,s.ringContext) || s.ringContext.value!=s.context.value ||
      !get(s.ring.value,0x40,s.ringEngine,1) || s.ringEngine.value!=engine ||
      !get(s.ring.value,0x30,s.stamps) || !get(s.ring.value,0x38,s.stampIndex,4) ||
      s.stampIndex.value>=64 ||
      !get(s.stamps.value,unsigned(s.stampIndex.value*64),s.retired,4) ||
      !get(s.ring.value,0x8a0,s.writeIndex,4) || s.writeIndex.value>=64)complete=false;
  }
  if(!complete)return;
  ++c.q[10];
 }
 c.q[11]=memcmp(&c.pass[0],&c.pass[1],sizeof(Witness))!=0;
 c.q[1]=c.q[11]?4:0;
 // Equality of two sequential copies does NOT establish an atomic GPU view,
 // exclude ABA, or prove that these slots were the executing GPU context.
}
}
#endif
