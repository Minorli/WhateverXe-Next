// Failure-only CPU witnesses near a saved BCS head. Candidate instructions and
// software PTEs do not establish the GPU's active packet or cached translation.
#ifndef kern_tgl_hang_bcs_wait_hpp
#define kern_tgl_hang_bcs_wait_hpp
#include "kern_tgl_hang_pending.hpp"
#include "kern_tgl_ppgtt.hpp"
namespace TGLHangBCSWait {
using TGLHangPending::kernelRange;
using TGLHangPending::u32;
enum Field {Context,Owner,Task,Table,TableClass,LRCBuffer,LRCCPU,LRCSize,
 RingBuffer,RingCPU,RingSize,RingLength,ContextID,Root,Fields};
struct Candidate {
 // ring offset, header, comparison data, target GPU address, guard,
 // CPU value before/after, unsigned GTE satisfied before/after.
 uint64_t q[9];
 TGLPPGTT::Mapping before,after;
};
struct Capture {
 // schema, guard, begin/end ns, ring, saved head/tail/start, window offset,
 // number of candidate headers, reserved. Guard 0 means CPU witnesses stable.
 uint64_t q[16],before[Fields],after[Fields];
 uint32_t words[16],recheck[16];
 Candidate candidates[4];
};
static_assert(sizeof(Capture)<4096,"bounded BCS wait evidence");
template<class Read>
bool ownership(uint64_t *q,uint64_t ring,uint64_t expectedClass,Read &read) {
 auto get=[&](uint64_t p,unsigned offset,uint64_t &v,unsigned n=8) {
  v=0;return kernelRange(p,offset+n) && read(p+offset,&v,n)==n;
 };
 uint64_t engine=0,contextEngine=0,owner=0,back=0,lrc=0,hi=0,lo=0;
 if(!get(ring,0x40,engine,1) || engine!=2 || !get(ring,0x20,q[Context]) ||
    !get(q[Context],0x6c,contextEngine,1) || contextEngine!=2 ||
    !get(ring,0x10,q[Owner]) || !q[Owner] || !get(q[Context],0x50,owner) || owner!=q[Owner] ||
    !get(q[Context],0xb0,back) || back!=ring ||
    !get(q[Context],0x58,q[Task]) || !get(q[Task],0x260,q[Table]) ||
    !get(q[Table],0,q[TableClass]) || !expectedClass || q[TableClass]!=expectedClass ||
    !get(q[Context],0x98,q[LRCBuffer]) || !get(q[LRCBuffer],0x38,q[LRCCPU]) ||
    !get(q[LRCBuffer],0x20,q[LRCSize]) || !get(ring,0x18,lrc) || lrc!=q[LRCCPU] ||
    !get(ring,0x80,q[RingBuffer]) || !get(q[RingBuffer],0x38,q[RingCPU]) ||
    !get(q[RingBuffer],0x20,q[RingSize]) || !get(ring,0x8c,q[RingLength],4) ||
    !get(q[Context],0x7c,q[ContextID],4))return false;
 if(q[LRCSize]<8192 || q[LRCSize]>81920 || (q[LRCSize]&4095) ||
    !kernelRange(q[LRCCPU],q[LRCSize]) || (q[LRCCPU]&4095) ||
    q[RingLength]<4096 || q[RingLength]>32768 || (q[RingLength]&(q[RingLength]-1)) ||
    q[RingSize]<q[RingLength] || !kernelRange(q[RingCPU],q[RingLength]) || (q[RingCPU]&4095))return false;
 if(!get(q[LRCCPU],0x10c4,hi,4) || !get(q[LRCCPU],0x10cc,lo,4))return false;
 q[Root]=(hi<<32)|lo;return q[Root] && !(q[Root]&4095);
}
template<class Read>
void collect(Capture &c,uint64_t ring,const uint8_t *regs,uint64_t expectedClass,
 uint64_t directBase,uint64_t directEnd,Read read) {
 c.q[0]=1;c.q[1]=1;c.q[4]=ring;
 c.q[5]=u32(regs)&0x1ffffc;c.q[6]=u32(regs+4);c.q[7]=u32(regs+12);
 if(regs[0x94]!=2 || !ownership(c.before,ring,expectedClass,read))return;
 const auto b=c.before;
 c.q[1]=2;
 if(b[ContextID]!=((u32(regs+0xac)>>5)&2047) || c.q[5]>=b[RingLength])return;
 c.q[8]=(c.q[5]+b[RingLength]-16)&(b[RingLength]-1);
 auto window=[&](uint32_t *out) {
  for(unsigned i=0;i<16;++i)
   if(read(b[RingCPU]+((c.q[8]+i*4)&(b[RingLength]-1)),out+i,4)!=4)return false;
  return true;
 };
 c.q[1]=3;if(!window(c.words))return;
 auto read64=[&](uint64_t p,uint64_t &v) {
  return kernelRange(p,8) && read(p,&v,8)==8;
 };
 // Exact observed Gen8+ PPGTT polling/GTE packet. Do not reinterpret GGTT,
 // token waits, unknown compare modes, or partial headers as this packet.
 // Bit definitions: Linux i915 gt/intel_gpu_commands.h, MI_SEMAPHORE_WAIT.
 for(unsigned i=0;i+4<=16 && c.q[9]<4;++i)if(c.words[i]==0x0e009002) {
  auto &d=c.candidates[c.q[9]++];auto q=d.q;
  q[0]=(c.q[8]+i*4)&(b[RingLength]-1);q[1]=c.words[i];q[2]=c.words[i+1];
  q[3]=uint64_t(c.words[i+2])|(uint64_t(c.words[i+3])<<32);q[4]=1;
  if(!q[3] || q[3]>>47 || (q[3]&3))continue;
  d.before=TGLPPGTT::inspect(b[Table],q[3],read64);const auto &m=d.before;
  q[4]=2;
  if(m.status || m.rootPhysical!=b[Root] || m.physical>UINT64_MAX-directBase)continue;
  const uint64_t cpu=directBase+m.physical;
  q[4]=3;
  if(!kernelRange(cpu,4) || cpu<directBase || cpu>=directEnd || directEnd-cpu<4)continue;
  uint32_t first=0,last=0;
  if(read(cpu,&first,4)!=4 || read(cpu,&last,4)!=4)continue;
  q[5]=first;q[6]=last;q[7]=first>=q[2];q[8]=last>=q[2];
  d.after=TGLPPGTT::inspect(b[Table],q[3],read64);q[4]=4;
  if(!memcmp(&d.before,&d.after,sizeof(d.before)))q[4]=0;
 }
 c.q[1]=4;
 if(!window(c.recheck) || memcmp(c.words,c.recheck,sizeof(c.words)) ||
    !ownership(c.after,ring,expectedClass,read) || memcmp(c.before,c.after,sizeof(c.before)))return;
 c.q[1]=0;
}
}
#endif
