exec((__import__('pathlib').Path(__file__).parent/'build.py').read_text().split('sources=')[0])
run([clang,*flags,'-c',str(p/'threaded.c'),'-o',str(p/'threaded.obj')])
run([clang,'-nostartfiles','-nostdlib','-fuse-ld=lld-link','-Xlinker','/subsystem:console',*[str(p/(s+'.obj')) for s in ['table','wide','wide_pack','threaded']],str(root/'build/release/engine/tide.lib'),'-lkernel32','-luser32','-lshell32','-ladvapi32','-loldnames','-o',str(p/'threaded.exe')])
for name in ['test_main','test_table']:
    run([clang,*flags,'-I'+str(root/'tests'),'-c',str(root/('tests/'+name+'.c')),'-o',str(p/(name+'.obj'))])
run([clang,'-nostartfiles','-nostdlib','-fuse-ld=lld-link','-Xlinker','/subsystem:console',*[str(p/(s+'.obj')) for s in ['table','test_main','test_table']],str(root/'build/release/engine/tide.lib'),'-lkernel32','-luser32','-lshell32','-ladvapi32','-loldnames','-o',str(p/'table-tests.exe')])
(p/'extra-commands.json').write_text(json.dumps(commands,indent=2))
base=json.loads((p/'commands.json').read_text())
compile_web=next(a for a in base if '-c' in a and any(x.startswith('--target=wasm') for x in a))
wflags=compile_web[1:compile_web.index('-c')]
for name in ['test_main','test_table']:
    run([clang,*wflags,'-I'+str(root/'tests'),'-c',str(root/('tests/'+name+'.c')),'-o',str(p/(name+'.wasm.obj'))])
link=base[-1]
link=[x for x in link if not x.endswith(('wide.wasm.obj','wide_pack.wasm.obj','check.wasm.obj'))]
link[link.index('-o')+1]=str(p/'table-tests.wasm')
link[1:1]=[str(p/(s+'.wasm.obj')) for s in ['test_main','test_table']]
run(link)
(p/'extra-commands.json').write_text(json.dumps(commands,indent=2))
