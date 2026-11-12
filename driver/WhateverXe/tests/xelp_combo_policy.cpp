#include "kern_xelp_combo_policy.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>
using Policy=XELP::ComboPolicy;using Platform=Policy::Platform;using Table=Policy::Table;
int main() {
 for(auto id:{0x9A40,0x9A49,0x9A59,0x9A78,0x9AC0,0x9AC9,0x9AD9,0x9AF8})
  assert(XELP::ComboPolicy::tigerLake((uint32_t(id)<<16)|0x8086)==XELP::ComboPolicy::Platform::TGLUY);
 for(auto id:{0x9A60,0x9A68,0x9A70})
  assert(XELP::ComboPolicy::tigerLake((uint32_t(id)<<16)|0x8086)==XELP::ComboPolicy::Platform::TGL);
 assert(XELP::ComboPolicy::tigerLake(0x46908086)==XELP::ComboPolicy::Platform::Unknown);
 assert(XELP::ComboPolicy::tigerLake(0x9A491234)==XELP::ComboPolicy::Platform::Unknown);

 // FNV-1a of all five-byte rows in Linux v6.12 intel_ddi_buf_trans.c,
 // in Table enum order. This also checks the post_cursor_2 field.
 const uint32_t expected[]={0x9C7E3F56U,0x004ED0CBU,0x49762BB3U,0x16ACF609U,0xEB4FB3B0U,0xBE6E1ADFU,0x231944E5U,0x67165B6FU,0x52DE18D6U,0x2DFE14A9U,0x4A81F931U,0xE7F29A4EU,0x9C126509U};
 for(unsigned table=1;table<=13;++table) {
  uint32_t hash=2166136261U;const unsigned count=table==6?9:10;
  for(unsigned i=0;i<count;++i) {
   Policy::Coefficients c;assert(Policy::row(Table(table),i,c));
   for(auto v:{c.swing,c.scalar,c.cursor,c.post2,c.post1})hash=(hash^v)*16777619U;
  }
  assert(hash==expected[table-1]);Policy::Coefficients c;
  assert(!Policy::row(Table(table),count,c));
 }
 unsigned cases=0;
 for(auto platform:{Platform::TGL,Platform::TGLUY,Platform::RKL,Platform::ADLS,Platform::ADLP})
 for(auto rate:{6U,10U,20U,30U})for(bool panel:{false,true})
 for(bool low:{false,true})for(bool hobl:{false,true})for(unsigned vs=0;vs<4;++vs)for(unsigned pre=0;pre<4;++pre){
  Policy p;const bool valid=vs+pre<=3;
  assert(p.select(platform,rate,panel,low,hobl,vs,pre)==valid);
  if(!valid){assert(p.table==Table::Invalid);continue;}
  // External DP must ignore panel flags; eDP HBR3 precedes HOBL/low swing.
  const Table normal[][2]={{Table::TGLHBR,Table::TGLHigh},{Table::TGLHBR,Table::TGLUYHigh},
   {Table::RKLHBR,Table::RKLHigh},{Table::TGLHBR,Table::ADLSHigh},{Table::ADLPHBR,Table::ADLPHigh}};
  Table want=normal[unsigned(platform)-1][rate>10];
  if(panel) {
   if(rate==30)want=platform==Platform::ADLS?Table::ADLSHigh:platform==Platform::ADLP?Table::ADLPHigh:Table::ICLHigh;
   else if(hobl)want=Table::HOBL;
   else if(low)want=platform==Platform::ADLS?Table::ADLSLow:platform==Platform::ADLP?Table::ADLPLow:Table::LowSwing;
  }
  assert(p.table==want&&p.hobl==(want==Table::HOBL));
  const unsigned levels[][4]={{0,1,2,3},{4,5,6,0},{7,8,0,0},{9,0,0,0}};
  assert(p.level==(want==Table::HOBL&&vs==3?8:levels[vs][pre]));++cases;
 }
 for(auto rate:{0U,12U,19U,31U,~0U}){Policy p;assert(!p.select(Platform::TGL,rate,true,false,false,0,0));}
 for(auto platform:{Platform::Unknown,Platform(255)}){Policy p;assert(!p.select(platform,10,true,false,false,0,0));}
 Policy p;assert(p.select(Platform::ADLS,20,false,false,false,0,0));
 assert(!p.select(Platform::ADLS,20,false,false,false,4,0)&&p.table==Table::Invalid);
 std::printf("PASS 129 Linux coefficient rows and %u platform/protocol/rate/panel/training selections\n",cases);
}
