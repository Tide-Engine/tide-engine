from pathlib import Path
import json, subprocess, hashlib, os, shutil

p=Path(__file__).resolve().parent
root=p.parents[1]
old=root/'build/bevy-locality-20261029'
os.environ['TIDE_HIDDEN']='1'
common=p/'common'; common.mkdir(exist_ok=True)
for name in ['wide.c','wide.h','wide_pack.c','model.c']:
    shutil.copyfile(old/'common'/name,common/name)
main=(root/'bench/versus/wide/main.c').read_text()
main=main.replace('extern const versus_way wide_c;', 'extern const versus_way wide_c;\nextern const versus_way layout_way;\nint layout_validate(void);\nint layout_maintenance(void);\n#include <string.h>')
main=main.replace('static versus_flag flags[] = {', 'static versus_flag flags[] = {')
main=main.replace('    {"frozen", "of a hundred crates, how many are frozen", 0, 0, 0, 0},', '    {"frozen", "of a hundred crates, how many are frozen", 0, 0, 0, 0},\n    {"layout", "0 records, 1 blocks4, 2 packed, 3 writeback, 4 rebuild", 0, 0, 0, 0},')
main=main.replace('    const versus_way ways[] = {{"Tide", make, tick, digest, destroy}, wide_c};', '''    if (argc == 2 && strcmp(argv[1], "--validate-layout") == 0) return layout_validate();
    if (argc == 2 && strcmp(argv[1], "--maintenance") == 0) return layout_maintenance();
    versus_way ways[] = {{"Tide", make, tick, digest, destroy}, layout_way, wide_c};
    if (getenv("TIDE_LAYOUT_FIRST")) { const versus_way swap=ways[0]; ways[0]=ways[1]; ways[1]=swap; }''')
(p/'main.c').write_text(main)
m=json.loads((old/'manifest.json').read_text())
clang=m['clang']; commands=[]
def run(args):
    commands.append(args)
    result=subprocess.run(args,cwd=root,text=True,capture_output=True)
    if result.returncode:
        print(result.stdout,result.stderr)
        raise SystemExit(result.returncode)
    return result.stdout

base=m['commands'][0]['arguments']; flags=base[:base.index('-o')]
flags=[x.replace(str(old).replace('\\','/')+'/common',str(common).replace('\\','/')) for x in flags]
sources=[p/'main.c',p/'prototype.c',common/'wide.c',common/'wide_pack.c']
for src in sources:
    run([clang,*flags,'-c',str(src),'-o',str(p/(src.stem+'.obj'))])
link=next(x['arguments'] for x in m['commands'] if '-nostartfiles' in x['arguments'])
link=[str(p/(Path(x).stem+'.obj')) if x.endswith('.obj') else x for x in link]
link=[x.replace(str(p/'model.obj'),str(p/'prototype.obj')) for x in link]
for i,x in enumerate(link):
    if x.endswith('/control/versus_wide.exe'): link[i]=str(p/'versus_layout.exe')
    elif x.startswith('/implib:'): link[i]='/implib:'+str(p/'versus_layout.lib')
    elif x.startswith('/pdb:'): link[i]='/pdb:'+str(p/'versus_layout.pdb')
run([clang,*link])

sysroot=root/'build/wasi-sdk-24/wasi-sysroot-24.0'
wflags=['--target=wasm32-wasip1-threads','--sysroot='+str(sysroot),'-I'+str(common),'-I'+str(root/'engine/include'),'-I'+str(root/'platform/include'),'-I'+str(root/'bench/versus'),'-O2','-DNDEBUG','-std=c17','-pthread','-msimd128','-Wall','-Wextra','-Wpedantic','-Werror','-fno-fast-math','-ffp-contract=off','-ffunction-sections','-fdata-sections']
for src in sources:
    run([clang,*wflags,'-c',str(src),'-o',str(p/(src.stem+'.wasm.obj'))])
builtins=root/'build/wasi-sdk-24/libclang_rt.builtins-wasm32-wasi-24.0/libclang_rt.builtins-wasm32.a'
libs=[root/('build/web-release/'+s) for s in ['bench/versus/libtide_versus.a','platform/libtide_platform.a','engine/libtide.a']]
run([clang,*wflags,'-nodefaultlibs','-Wl,--gc-sections','-Wl,--allow-undefined','-Wl,-z,stack-size=1048576','-Wl,--import-memory','-Wl,--export-memory','-Wl,--shared-memory','-Wl,--max-memory=4294967296',*[str(p/(s.stem+'.wasm.obj')) for s in sources],*[str(s) for s in libs],'-lc',str(builtins),'-o',str(p/'versus_layout.wasm')])
inputs={}
for args in commands:
    for a in args:
        q=Path(a)
        if q.is_file() and q.suffix in ['.c','.lib','.a']: inputs[str(q)]=hashlib.sha256(q.read_bytes()).hexdigest()
for f in [common/'wide.h',common/'model.c']:
    inputs[str(f)]=hashlib.sha256(f.read_bytes()).hexdigest()
report={'commands':commands,'inputs':inputs,'head':run(['git','rev-parse','HEAD']).strip(),'binaries':{str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in [p/'versus_layout.exe',p/'versus_layout.wasm']},'scope':'Experimental layouts with Tide scheduler, not generated production ECS storage. Manual build, O2 strict floats.'}
(p/'build.json').write_text(json.dumps(report,indent=2))
print('Native and WASI layouts compiled')
