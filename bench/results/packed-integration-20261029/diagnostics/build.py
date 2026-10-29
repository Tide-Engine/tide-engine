from pathlib import Path
import subprocess, os, shutil, json
p=Path(__file__).resolve().parent
root=p.parents[1]
os.environ['TIDE_HIDDEN']='1'
for name in ['wide.c','wide.h','wide_pack.c']:
    shutil.copyfile(root/'bench/results/bevy-layout-20261029/diagnostics/common'/name,p/name)
clang=r'C:\Program Files\LLVM\bin\clang.exe'
flags=['-O2','-DNDEBUG','-std=c17','-D_DLL','-D_MT','-Xclang','--dependent-lib=msvcrt','-Wall','-Wextra','-Wpedantic','-Werror','-fno-fast-math','-ffp-contract=off','-ffunction-sections','-fdata-sections','-I'+str(p),'-I'+str(root/'engine/include')]
commands=[]
def run(args):
    commands.append(args)
    subprocess.run(args,cwd=root,check=True)
sources=[p/n for n in ['table.c','wide.c','wide_pack.c','check.c']]
for src in sources: run([clang,*flags,'-c',str(src),'-o',str(src.with_suffix('.obj'))])
run([clang,'-nostartfiles','-nostdlib','-fuse-ld=lld-link','-Xlinker','/subsystem:console',*[str(s.with_suffix('.obj')) for s in sources],str(root/'build/release/engine/tide.lib'),'-lkernel32','-luser32','-lshell32','-ladvapi32','-loldnames','-o',str(p/'check.exe')])
(p/'commands.json').write_text(json.dumps(commands,indent=2))
wflags=['--target=wasm32-wasip1-threads','--sysroot='+str(root/'build/wasi-sdk-24/wasi-sysroot-24.0'),'-O2','-DNDEBUG','-std=c17','-pthread','-msimd128','-Wall','-Wextra','-Wpedantic','-Werror','-fno-fast-math','-ffp-contract=off','-ffunction-sections','-fdata-sections','-I'+str(p),'-I'+str(root/'engine/include')]
for src in sources: run([clang,*wflags,'-c',str(src),'-o',str(src.with_suffix('.wasm.obj'))])
run([clang,*wflags,'-nodefaultlibs','-Wl,--gc-sections','-Wl,--allow-undefined','-Wl,-z,stack-size=1048576','-Wl,--import-memory','-Wl,--export-memory','-Wl,--shared-memory','-Wl,--max-memory=4294967296',*[str(s.with_suffix('.wasm.obj')) for s in sources],str(root/'build/web-release/engine/libtide.a'),'-lc',str(root/'build/wasi-sdk-24/libclang_rt.builtins-wasm32-wasi-24.0/libclang_rt.builtins-wasm32.a'),'-o',str(p/'check.wasm')])
(p/'commands.json').write_text(json.dumps(commands,indent=2))
