#include <cassert>
#include <cstdio>
#include <map>
#include <vector>
#include "kern_xelp_sst_program.hpp"
struct IO {
 std::map<uint32_t,uint32_t> regs;
 std::vector<uint32_t> reads,writes;
 uint32_t drop {};
 uint32_t read(uint32_t reg){reads.push_back(reg);return regs.at(reg);}
 void write(uint32_t reg,uint32_t value){writes.push_back(reg);if(reg!=drop)regs.at(reg)=value;}
};
int main(){
 unsigned count=0;
 for(unsigned ddi=0;ddi<9;++ddi)for(unsigned pipe=0;pipe<3;++pipe)for(unsigned lanes:{1U,2U,4U}) {
  const auto clock=0x46140+4*pipe,func=0x60400+0x1000*pipe,buffer=0x64000+0x100*ddi;
  for(unsigned fault=0;fault<9;++fault){
   IO io;io.regs={{clock,0x12345},{func,0xA05670},{buffer,0x40}};
   const auto read=[&](uint32_t r){return io.read(r);};const auto write=[&](uint32_t r,uint32_t v){io.write(r,v);};
   if(fault==1)io.regs[func]|=0x80000000;
   if(fault==2)io.regs[buffer]|=0x80000000;
   if(fault==3)io.regs[clock]|=((ddi+1)%9+1)<<28;
   if(fault==4)io.regs[func]|=((ddi+1)%9+1)<<27;
   if(fault==5)io.drop=clock;
   if(fault==6)io.drop=func;
   const auto r=XELP::SSTProgram::prepare(read,write,ddi,pipe,fault==7?3:lanes,fault!=8);
   const unsigned reason[]={0,2,2,3,4,6,7,1,5};assert(r.reason==reason[fault]);
   if(!fault){assert(io.regs[clock]==(((ddi+1)<<28)|0x12345));
    assert(io.regs[func]==(((ddi+1)<<27)|0x02000000|0xA05670|((lanes-1)<<1)));
    assert(io.writes.size()==2&&io.writes[0]==clock&&io.writes[1]==func);}
   else if(fault==5)assert(io.writes.size()==1&&io.writes[0]==clock);
   else if(fault==6)assert(io.writes.size()==2);
   else assert(io.writes.empty());
   if(fault==8)assert(io.reads.empty());
   assert(io.regs[buffer]==(fault==2?0x80000040:0x40));++count;
  }
 }
 for(unsigned pipe=0;pipe<3;++pipe)for(unsigned mode:{0U,0x80000000U,0x88000000U})
 for(unsigned enhanced=0;enhanced<2;++enhanced)for(unsigned panel=0;panel<2;++panel)
 for(unsigned sink=0;sink<2;++sink)for(unsigned alt=0;alt<3;++alt)for(unsigned powered=0;powered<2;++powered){
  const auto reg=0x60540+0x1000*pipe;IO io;io.regs[reg]=mode|0x40040;
  const auto r=XELP::SSTProgram::framing([&](uint32_t x){return io.read(x);},[&](uint32_t x,uint32_t v){io.write(x,v);},pipe,enhanced,panel,sink,alt,powered);
  assert(r.ready==bool(powered));
  if(!powered)assert(io.reads.empty()&&io.writes.empty());
  else if(mode!=0x80000000U)assert(io.writes.empty()&&io.regs[reg]==(mode|0x40040));
  else {assert((io.regs[reg]&0x40000)==(enhanced<<18));
   assert((io.regs[reg]&0x40)==((panel&&sink&&alt<=1)?alt<<6:0x40));}
  ++count;
 }
 IO io;const auto read=[&](uint32_t r){return io.read(r);};const auto write=[&](uint32_t r,uint32_t v){io.write(r,v);};
 assert(XELP::SSTProgram::prepare(read,write,9,0,4,true).reason==8);
 assert(XELP::SSTProgram::prepare(read,write,0,3,4,true).reason==8);
 assert(!XELP::SSTProgram::framing(read,write,3,1,false,false,0,true).ready);
 assert(!XELP::SSTProgram::framing(read,write,0,2,false,false,0,true).ready);
 assert(io.reads.empty()&&io.writes.empty());
 printf("PASS common SST programming: %u route/framing/fault cases; no unpowered or foreign-route writes\n",count);
}
