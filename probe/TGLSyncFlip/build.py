#!/usr/bin/env python3
"""Build TGLSyncFlip.kext (x86_64 Lilu plugin) with the same kernel C++ ABI flags as WhateverXe."""
import argparse, hashlib, json, plistlib, subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
NAME, VERSION, IDENT = 'TGLSyncFlip', '1.0.2', 'org.whateverxe-next.TGLSyncFlip'
MIN_TARGET = '10.9'


def run(cmd):
    subprocess.run([str(x) for x in cmd], check=True, cwd=HERE)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sdk', type=Path, default=REPO / 'driver/WhateverXe/MacKernelSDK')
    p.add_argument('--lilu', type=Path, default=REPO / 'driver/WhateverXe/Lilu.kext')
    p.add_argument('--output', type=Path, default=HERE / 'build')
    a = p.parse_args()
    sdk, lilu, out = a.sdk.resolve(), a.lilu.resolve() / 'Contents/Resources', a.output.resolve()
    for need in (sdk / 'Headers', lilu / 'Library/plugin_start.cpp', sdk / 'Library/x86_64/libkmod.a'):
        if not need.exists():
            raise SystemExit(f'missing dependency: {need} (pass --sdk / --lilu)')
    objdir, bundle = out / 'obj', out / (NAME + '.kext') / 'Contents'
    objdir.mkdir(parents=True, exist_ok=True)
    (bundle / 'MacOS').mkdir(parents=True, exist_ok=True)
    # Same ABI choices as driver/WhateverXe/tools/build.py for the WhateverXe plugin:
    # 10.9 consumes IOService reserved slots 0/1 as configureReport/updateReport.
    # 10.6 imports removed _RESERVEDIOService0/1; 10.15 adds DriverKit ABI.
    base = ['xcrun', 'clang++', '-arch', 'x86_64', '-std=c++17', '-O2', '-g', '-fapple-kext', '-fno-exceptions', '-fno-rtti',
            '-fno-stack-protector', '-fno-builtin', '-fno-asynchronous-unwind-tables', '-mno-red-zone', '-nostdinc',
            '-DKERNEL', '-DKERNEL_PRIVATE', '-D__APPLE__', '-D__MACH__', '-DAPPLE_KEXT_ASSERTIONS=1', '-DDRIVER_PRIVATE',
            '-DAPPLE', '-DNeXT', '-DLILU_CUSTOM_IOKIT_INIT=1', '-DPRODUCT_NAME=' + NAME, '-DMODULE_VERSION=' + VERSION,
            '-I' + str(sdk / 'Headers'), '-I' + str(lilu), '-I' + str(HERE), '-mmacosx-version-min=' + MIN_TARGET, '-fno-common',
            '-mmmx', '-msse', '-msse2', '-msse3', '-mssse3', '-mfpmath=sse', '-Wall', '-Wextra', '-Werror',
            '-Wno-unused-parameter', '-Wno-deprecated-declarations', '-Wno-ossharedptr-misuse', '-Wno-inconsistent-missing-override',
            '-fdebug-compilation-dir=.', '-ffile-prefix-map=' + str(HERE) + '=/TGLSyncFlip',
            '-ffile-prefix-map=' + str(sdk) + '=/MacKernelSDK', '-ffile-prefix-map=' + str(lilu) + '=/Lilu']
    sources = [HERE / 'TGLSyncFlip.cpp', lilu / 'Library/plugin_start.cpp']
    objects = []
    for s in sources:
        o = objdir / (s.stem + '.o')
        run(base + ['-c', s, '-o', o])
        objects.append(o)
    exe = bundle / 'MacOS' / NAME
    run(['xcrun', 'ld', '-arch', 'x86_64', '-kext', '-undefined', 'dynamic_lookup', '-no_fixup_chains',
         '-macos_version_min', MIN_TARGET, '-o', exe, *objects, sdk / 'Library/x86_64/libkmod.a'])
    run(['xcrun', 'strip', '-S', exe])
    symbols = subprocess.check_output(['xcrun', 'nm', str(exe)], text=True)
    for s in (' _kmod_info', ' __realmain', ' __start'):
        assert s in symbols, 'missing kext lifecycle symbol' + s
    undefined = subprocess.check_output(['xcrun', 'nm', '-u', str(exe)], text=True)
    assert not any(x in undefined for x in ['chkstk_darwin', '___memcpy_chk', '___memmove_chk', 'DispatchE5IORPC', '_RESERVEDIOService0Ev', '_RESERVEDIOService1Ev']), \
        'incompatible userspace/IOKit ABI imported into kernel plugin'
    libs = {x: '10.0.0' for x in ['com.apple.kpi.bsd', 'com.apple.kpi.dsep', 'com.apple.kpi.iokit', 'com.apple.kpi.libkern', 'com.apple.kpi.mach',
                                  'com.apple.kpi.unsupported']}
    libs['as.vit9696.Lilu'] = '1.2.0'
    info = dict(CFBundleIdentifier=IDENT, CFBundleExecutable=NAME, CFBundleName=NAME, CFBundlePackageType='KEXT',
                CFBundleVersion=VERSION, CFBundleShortVersionString=VERSION, CFBundleInfoDictionaryVersion='6.0',
                OSBundleCompatibleVersion=VERSION, OSBundleRequired='Root', OSBundleLibraries=libs,
                IOKitPersonalities={NAME: dict(CFBundleIdentifier=IDENT, IOClass=NAME, IOMatchCategory=NAME,
                                               IOProviderClass='IOResources', IOResourceMatch='IOKit')})
    (bundle / 'Info.plist').write_bytes(plistlib.dumps(info))
    digest = hashlib.sha256(exe.read_bytes()).hexdigest()
    manifest = {'name': NAME, 'version': VERSION, 'sha256': digest, 'deployment_target': MIN_TARGET,
                'source_sha256': {s.name: hashlib.sha256(s.read_bytes()).hexdigest() for s in [HERE / 'TGLSyncFlip.cpp', HERE / 'TGLSyncFlipLogic.hpp']}}
    (out / 'build-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    main()
