"""Build standalone probes against completely rebuilt, gated Tide archives.

The build directories must have been configured with TIDE_PACKED_TABLES=ON.
No overlay, copied generated C, or changes to the docs/game source are used.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--native-build', default='build/packed-release')
parser.add_argument('--web-build', default='build/packed-web-release')
parser.add_argument('--out', default='build/packed-production')
parser.add_argument('--clang', default=r'C:\Program Files\LLVM\bin\clang.exe')
args = parser.parse_args()
os.environ['TIDE_HIDDEN'] = '1'
os.environ.pop('TIDE_PACKED_MODE', None)
os.environ.pop('TIDE_PACKED_REPORT', None)
out = ROOT / args.out
out.mkdir(parents=True, exist_ok=True)
native, web = ROOT / args.native_build, ROOT / args.web_build
for build in [native, web]:
    assert 'TIDE_PACKED_TABLES:BOOL=ON' in (build / 'CMakeCache.txt').read_text(), str(build)
commands = []

def run(command):
    command = [str(x) for x in command]
    commands.append(command)
    subprocess.run(command, cwd=ROOT, check=True)

common = ['-O2', '-DNDEBUG', '-DTIDE_PACKED_TABLES=1', '-std=c17',
          '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-fno-fast-math',
          '-ffp-contract=off', '-ffunction-sections', '-fdata-sections',
          '-I' + str(ROOT / 'engine/include')]
native_flags = ['-D_CRT_SECURE_NO_WARNINGS', '-D_DLL', '-D_MT', '-Xclang', '--dependent-lib=msvcrt']
sdk = ROOT / 'build/wasi-sdk-24'
web_flags = ['--target=wasm32-wasip1-threads',
             '--sysroot=' + str(sdk / 'wasi-sysroot-24.0'), '-pthread', '-msimd128']
artifacts = {}
for name, source, driver in [('entities', 'docs/examples/entities.tide', 'entities_replay.c'),
                             ('wide', 'bench/versus/wide/wide.tide', 'wide_driver.c')]:
    generated = out / name
    generated.mkdir(exist_ok=True)
    run([native / 'bin/tidec.exe', ROOT / source, '-o', generated, '--name', name, '--pack-c'])
    sources = [ROOT / 'bench/packed' / driver, generated / (name + '_pack.c')]
    for target, build, flags, suffix in [('native', native, native_flags, '.exe'),
                                         ('web', web, web_flags, '.wasm')]:
        objects = []
        for index, src in enumerate(sources):
            obj = generated / f'{target}-{index}.obj'
            run([args.clang, *common, *flags, '-I' + str(generated), '-c', src, '-o', obj])
            objects.append(obj)
        binary = out / (name + suffix)
        if target == 'native':
            run([args.clang, '-nostartfiles', '-nostdlib', '-fuse-ld=lld-link',
                 '-Xlinker', '/subsystem:console', '-Xlinker', '/opt:ref', *objects,
                 build / 'engine/tide.lib', '-lkernel32', '-luser32', '-lshell32',
                 '-ladvapi32', '-loldnames', '-o', binary])
        else:
            run([args.clang, *web_flags, '-nodefaultlibs', '-Wl,--gc-sections',
                 '-Wl,-z,stack-size=1048576', '-Wl,--import-memory', '-Wl,--export-memory',
                 '-Wl,--shared-memory', '-Wl,--max-memory=4294967296', *objects,
                 build / 'engine/libtide.a', '-lc',
                 sdk / 'libclang_rt.builtins-wasm32-wasi-24.0/libclang_rt.builtins-wasm32.a',
                 '-o', binary])
        artifacts[str(binary.relative_to(ROOT))] = hashlib.sha256(binary.read_bytes()).hexdigest()
inputs = [Path(__file__).resolve(), ROOT / 'bench/packed/entities_replay.c', ROOT / 'bench/packed/wide_driver.c',
          ROOT / 'engine/include/tide/table.h', ROOT / 'engine/src/table.c',
          ROOT / 'compiler/src/codegen.c', ROOT / 'compiler/src/packed_rows_codegen.inc',
          native / 'bin/tidec.exe', native / 'engine/tide.lib', web / 'engine/libtide.a']
inputs += list(out.glob('*/*.c')) + list(out.glob('*/*.h'))
(out / 'build.json').write_text(json.dumps({'commands': commands, 'artifacts': artifacts,
    'compiler': subprocess.check_output([args.clang, '--version'], text=True).splitlines()[0],
    'inputs': {str(p.relative_to(ROOT)).replace('\\', '/'): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}}, indent=2))
