"""Compare ADL-S adapters with the exact donor's port and Combo PHY tables."""
from pathlib import Path
import hashlib,struct,subprocess,sys,tempfile

repo=Path(__file__).resolve().parents[1]
donor=Path(sys.argv[1]).read_bytes()
assert hashlib.sha256(donor).hexdigest()=='c004fc1ac9ee4157e2e12736f40fa6639bde025a279326beeb9dd48c9b445731'
# PortHAL::init's Combo B branch consists of 25 immediate/table-store pairs.
# Decode only this bounded, qualified shape; no whole-driver execution.
fields={}
for at in range(0x5f2b3,0x5f45b,17):
    assert donor[at:at+2]==b'\x48\xb8' and donor[at+10:at+13]==b'\x49\x89\x86'
    lo,hi=struct.unpack_from('<II',donor,at+2)
    offset=struct.unpack_from('<I',donor,at+13)[0]
    assert offset not in fields and offset+4 not in fields
    fields[offset]=lo;fields[offset+4]=hi
phy=[fields[off] for off in range(0x5ac,0x64c,4)]
cfields={}
for at in range(0x5f468,0x5f610,17):
    assert donor[at:at+2]==b'\x48\xb8' and donor[at+10:at+13]==b'\x49\x89\x86'
    lo,hi=struct.unpack_from('<II',donor,at+2);offset=struct.unpack_from('<I',donor,at+13)[0]
    cfields[offset]=lo;cfields[offset+4]=hi
assert donor[0x5f611:0x5f618]==bytes.fromhex('41c78648060000')
cfields[0x648]=struct.unpack_from('<I',donor,0x5f618)[0]
cphy=[cfields[off] for off in range(0x5ac,0x64c,4)]
afields={}
for at in range(0x5f0c0,0x5f269,17):
    assert donor[at:at+2]==b'\x48\xb8' and donor[at+10:at+13]==b'\x49\x89\x86'
    lo,hi=struct.unpack_from('<II',donor,at+2);offset=struct.unpack_from('<I',donor,at+13)[0]
    afields[offset]=lo;afields[offset+4]=hi
afields[0x648]=struct.unpack_from('<I',donor,0x5f270)[0]
aphy=[afields.get(off,0) for off in range(0x580,0x6a8,4)]
ports=list(struct.unpack_from('<54I',donor,0x9a928))
assert ports[:6]==[0,0,0,0,2,0x18]
assert ports[18:24]==[3,9,1,1,0x400,0x201]
code='''#include "kern_alderlake_s.hpp"
#include "kern_display13_fb_topology.hpp"
#include <cassert>
#include <cstdio>
int main() {
 const uint32_t aphy[]={APHY};const uint32_t cphy[]={CPHY};const uint32_t phy[]={PHY};const uint32_t ports[]={PORTS};
 uint32_t registers[0x128/4] {};registers[4]=0x64300;registers[(0x64c-0x580)/4]=0x4610c;
 assert(AlderLakeS::comboRegisters(3,registers,sizeof(registers)/4));
 assert(!memcmp(registers+(0x5ac-0x580)/4,phy,sizeof(phy)));
 memset(registers,0,sizeof(registers));registers[4]=0x64400;registers[(0x64c-0x580)/4]=0x46110;
 assert(AlderLakeS::comboRegisters(4,registers,sizeof(registers)/4));
 assert(!memcmp(registers+(0x5ac-0x580)/4,cphy,sizeof(cphy)));
 memcpy(registers,aphy,sizeof(aphy));
 assert(AlderLakeS::comboRegisters(0,registers,sizeof(registers)/4));
 assert(registers[(0x640-0x580)/4]==0x162A24); // donor duplicated lane3; restore lane2
 alignas(8) uint8_t platform[0x1c0] {};platform[0x12]=9;
 memcpy(platform+0x20,ports,sizeof(ports));
 XELP::VBT::Ports firmware;firmware.status=XELP::VBT::Status::Valid;
 firmware.present=8;firmware.port[3]={0x60d2,1,2};
 const auto wiring=AlderLakeS::Wiring::fromFirmware(firmware);
 assert(wiring.apply(platform,sizeof(platform)));
 assert(wiring.matches(reinterpret_cast<uint32_t *>(platform+0x68)));
 memcpy(platform+0x20,ports,sizeof(ports));
 firmware.present|=1;firmware.port[0]={0x78c6,10,0,0x40,0,0};
 firmware.panel.valid=true;firmware.panel.ddi=0;firmware.panel.backlightValid=true;
 firmware.panel.pwmPresent=true;firmware.panel.backlightType=2;
 const auto panel=AlderLakeS::Wiring::fromFirmware(firmware);
 assert(panel.panel==1 && panel.apply(platform,sizeof(platform)));
 assert(!memcmp(platform+0x20,ports,24) && panel.boundPanel(platform,reinterpret_cast<uint32_t *>(platform+0x20)));
 // The same captured predecessor records feed Display13 CPU topology.
 for(unsigned assignment=1;assignment<9;++assignment) {
  Display13OutputWiring w;w.valid=true;unsigned code=assignment;
  for(unsigned d=0;d<2;++d){unsigned kind=code%3;code/=3;
   if(kind==1){w.hdmi|=1U<<d;w.ddc[d]=d+1;}if(kind==2)w.dp|=1U<<d;}
  memcpy(platform+0x20,ports,sizeof(ports));alignas(8) uint8_t output[sizeof(platform)]{};
  assert(Display13FBTopology::apply(output,sizeof(output),platform,w));
  assert(Display13FBTopology::boundPorts(output,w).selected==w.selected());
  for(unsigned d=0;d<2;++d)if(w.dp&(1U<<d)) {
   const uint32_t native[]={d,d+1,1,0,0x400,0x201};assert(!memcmp(output+0x20+24*d,native,sizeof(native)));
  }
 }
 puts("PASS exact donor Combo A/B/C register tables and firmware connector adaptation");
}'''.replace('APHY',','.join(map(str,aphy))).replace('CPHY',','.join(map(str,cphy))).replace('PHY',','.join(map(str,phy))).replace('PORTS',','.join(map(str,ports)))
with tempfile.TemporaryDirectory(prefix='adls-donor-') as tmp:
    tmp=Path(tmp);(tmp/'test.cpp').write_text(code)
    subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined','-I'+str(repo/'WhateverXe'),
                    str(tmp/'test.cpp'),'-o',str(tmp/'test')],check=True)
    subprocess.run([str(tmp/'test')],check=True)
