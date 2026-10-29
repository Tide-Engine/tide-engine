from pathlib import Path
import hashlib,json,shutil,subprocess
p=Path(__file__).resolve().parent
root=p.parents[1]
dest=root/'bench/results/packed-integration-20261029'
dest.mkdir(exist_ok=True)
d=dest/'diagnostics';d.mkdir(exist_ok=True)
files=['prepare.py','cache.py','build.py','extra.py','run.ps1','analyze.py','export.py','table.c','check.c','threaded.c','wide.c','wide.h','wide_pack.c','commands.json','extra-commands.json','native-validation.txt','web-validation.txt','threaded-validation.txt','table-validation.txt','web-table-validation.txt','audit.txt']
for name in files: shutil.copyfile(p/name,d/name)
(d/'tide').mkdir(exist_ok=True)
shutil.copyfile(p/'tide/table.h',d/'tide/table.h')
shutil.copytree(p/'measurements',dest/'measurements',dirs_exist_ok=True)
for name in ['summary.json','comparison.html']: shutil.copyfile(p/name,dest/name)
inputs=['engine/src/table.c','engine/include/tide/table.h','engine/src/entity.c','engine/src/page.c','engine/src/jobs.c','engine/src/net.c','engine/src/delta.c','tests/test_main.c','tests/test_table.c','cmake/TideFlags.cmake']
manifest={'head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),'native_compiler':subprocess.check_output([r'C:\Program Files\LLVM\bin\clang.exe','--version'],text=True).splitlines()[0],'node':subprocess.check_output(['node','--version'],text=True).strip(),'inputs':{n:hashlib.sha256((root/n).read_bytes()).hexdigest() for n in inputs},'libraries':{n:hashlib.sha256((root/n).read_bytes()).hexdigest() for n in ['build/release/engine/tide.lib','build/web-release/engine/libtide.a']},'binaries':{n:hashlib.sha256((p/n).read_bytes()).hexdigest() for n in ['check.exe','check.wasm','threaded.exe','table-tests.exe','table-tests.wasm']},'files':{str(f.relative_to(dest)).replace('\\','/'):hashlib.sha256(f.read_bytes()).hexdigest() for f in dest.rglob('*') if f.is_file() and f.name!='manifest.json'}}
(dest/'manifest.json').write_text(json.dumps(manifest,indent=2))
print(dest)
