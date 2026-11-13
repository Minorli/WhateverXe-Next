"""Check the exact donor's CPU halt-witness admission. Never execute the donor."""
import json
from pathlib import Path
import re
import sys
from accelerator_fingerprint import require_accelerator

if len(sys.argv) != 2:
    raise SystemExit('usage: tgl_hang_cpu_native.py EXACT_ACCELERATOR')
data = Path(sys.argv[1]).read_bytes()
require_accelerator(data)
root = Path(__file__).resolve().parents[1]
header = (root / 'WhateverXe/kern_tgl_hang_cpu.hpp').read_text()
for name, offset in [('SchedulerLookup', 0x380ec), ('StreamerOwners', 0x39aaa),
                     ('StreamerDescriptor', 0x39aee)]:
    body = re.search(r'constexpr uint8_t ' + name + r'\[\]\s*\{([^}]+)\}', header)[1]
    expected = bytes(int(x.strip(), 0) for x in body.split(',') if x.strip())
    assert data[offset:offset+len(expected)] == expected, name
# The source hook stays between native wake acquisition and the native STOP.
# It passes r14d (engine) and r15 (scheduler), set before that original call.
assert data[0x56983:0x56997] == bytes.fromhex('4189f64989ff488b7f10be01000000ba07000000')
assert data[0x56997] == 0xe8
target = 0x5699c + int.from_bytes(data[0x56998:0x5699c], 'little', signed=True)
assert target == 0x28c2a
assert data[0x569be:0x569c6] == bytes.fromhex('42c7042001000100')
assert [data[0xd4200+i*0x79] for i in range(6)] == list(range(6))
print(json.dumps(dict(result='PASS', engines=6,
    scope='exact donor bytes, descriptor identities and halt-call placement; no execution, MMIO or GPU')))
