#!/usr/bin/env python3
"""Exercise production DP_MODE replacement, preserved fields and fallback."""
from pathlib import Path
import re, subprocess, tempfile, sys, hashlib
r=Path(__file__).resolve().parents[1];s=(r/'WhateverXe/kern_xe.cpp').read_text()
if len(sys.argv)>1:
 b=Path(sys.argv[1]).read_bytes()
 assert hashlib.sha256(b).hexdigest()=='c004fc1ac9ee4157e2e12736f40fa6639bde025a279326beeb9dd48c9b445731'
 expected=bytes(int(x,0) for x in re.findall(r'0x[0-9A-Fa-f]+',re.search(r'dpModeBody\[\] \{(.*?)\};',s,re.S).group(1)))
 assert b[0x60930:0x60ae0]==expected
fn=re.search(r'void XE::wrapTGLSetDklDPMode\(.*?\n\}',s,re.S).group()
f=(r/'tests/tgl_dp_mode_replace_fixture.cpp').read_text().replace('// PRODUCTION',fn)
with tempfile.TemporaryDirectory(prefix='tgl-dp-mode-') as d:
 d=Path(d);(d/'check.cpp').write_text(f)
 subprocess.run(['clang++','-std=c++14','-fsanitize=address,undefined','-I',str(r/'WhateverXe'),str(d/'check.cpp'),'-o',str(d/'check')],check=True)
 subprocess.run([str(d/'check')],check=True)
