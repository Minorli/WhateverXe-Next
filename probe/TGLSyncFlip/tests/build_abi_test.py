#!/usr/bin/env python3
"""Compile the real SDK IOService vtable at old/selected ABI floors (CPU only).

The old target must reproduce imports absent from Darwin 25; the production
floor must consume those slots as IOReport methods without importing DriverKit.
This tests the SDK/compiler output, not a hand-written vtable model.
"""
import argparse
import importlib.util
import subprocess
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("tglsync_build", HERE / "build.py")
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--sdk", type=Path, required=True)
a = p.parse_args()
source = """#include <IOKit/IOService.h>
class ABIProbe : public IOService { OSDeclareDefaultStructors(ABIProbe) };
OSDefineMetaClassAndStructors(ABIProbe, IOService)
"""
with tempfile.TemporaryDirectory(prefix="tglsync-abi-") as temp:
    symbols = {}
    for target in ("10.6", builder.MIN_TARGET):
        obj = Path(temp) / (target + ".o")
        subprocess.run([
            "xcrun", "clang++", "-arch", "x86_64", "-x", "c++", "-std=c++17", "-c", "-",
            "-o", str(obj), "-fapple-kext", "-fno-rtti", "-fno-exceptions", "-nostdinc",
            "-DKERNEL", "-DKERNEL_PRIVATE", "-DDRIVER_PRIVATE", "-D__APPLE__", "-D__MACH__",
            "-I" + str(a.sdk.resolve() / "Headers"), "-mmacosx-version-min=" + target,
            "-Wno-deprecated-declarations", "-Wno-ossharedptr-misuse",
        ], input=source, text=True, check=True)
        symbols[target] = subprocess.check_output(["xcrun", "nm", "-u", str(obj)], text=True)
    old, selected = symbols["10.6"], symbols[builder.MIN_TARGET]
    for slot in ("_RESERVEDIOService0Ev", "_RESERVEDIOService1Ev"):
        assert slot in old, "old ABI no longer reproduces the regression: " + slot
        assert slot not in selected, "production ABI still imports removed slot: " + slot
    assert "configureReport" in selected and "updateReport" in selected
    assert "DispatchE5IORPC" not in selected, "DriverKit ABI entered the kext"
print("TGLSyncFlip ABI: old-target regression reproduced; selected IOService vtable passed (CPU only).")
