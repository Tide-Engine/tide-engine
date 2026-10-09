import { spawnSync } from 'node:child_process';
import { readdir } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { prepareStarter, resolveTideBinary, starterDirectory } from './prepare-starter.mjs';

const directory = path.dirname(fileURLToPath(import.meta.url));
const environment = { ...process.env, TIDE_HIDDEN: '1', TIDE_NO_UPDATE_CHECK: '1' };
function run(executable, args) {
  const result = spawnSync(executable, args, { env: environment, windowsHide: true, encoding: 'utf8' });
  if (result.error) throw result.error;
  if (result.stdout) process.stdout.write(result.stdout);
  if (result.stderr) process.stderr.write(result.stderr);
  if (result.status !== 0) throw new Error(`${executable} failed (${result.signal || result.status})`);
  return result.stdout;
}

const tide = await resolveTideBinary();
const version = run(tide, ['--version']);
const root = version.match(/^installed in (.+)$/m)?.[1].trim();
if (!root) throw new Error('Cannot locate the installed Tide headers from tide --version');
const suffix = process.platform === 'win32' ? '.exe' : '';
const output = path.join(starterDirectory, 'build');
run(tide, ['build', starterDirectory, '--release', '-o', path.join(output, `moonwake${suffix}`)]);
const cache = path.join(starterDirectory, '.tide', 'native-release');
const engine = path.join(cache, 'engine');
const objects = (await readdir(engine)).filter(file => file.endsWith('.o')).sort().map(file => path.join(engine, file));
const test = path.join(output, `starter-test${suffix}`);
const target = process.platform === 'win32'
  ? ['--target=x86_64-w64-windows-gnu', `--sysroot=${path.join(root, 'mingw', 'sysroot')}`] : [];
const testObject = path.join(output, 'starter-test.o');
run(tide, ['cc', ...target, '-Qunused-arguments', '-std=c17', '-O1', '-Wall', '-Wextra', '-Werror',
  '-fno-fast-math', '-ffp-contract=off',
  `-I${path.join(cache, 'gen')}`, `-I${path.join(root, 'include')}`,
  '-c', path.join(directory, 'starter-test.c'), '-o', testObject]);
const libraries = process.platform === 'win32'
  ? ['-fuse-ld=lld', '-nodefaultlibs', '-lmingw32', '-lmingwex', '-lmoldname', '-lmsvcrt', '-lkernel32',
    '-luser32', '-lgdi32', '-lshell32', '-ladvapi32', '-lws2_32', '-lsecur32', '-lole32',
    path.join(root, 'mingw', 'libclang_rt.builtins-x86_64.a')]
  : ['-lm'];
run(tide, ['cc', ...target, testObject, path.join(cache, 'game.o'), ...objects, ...libraries, '-o', test]);
const nativeResult = run(test, []);

// Run the same behavioral and exact-bit replay checks against the shipped
// WebAssembly target, through the repository's WASI runner.
await prepareStarter({ tideBinary: tide, log: value => process.stdout.write(value) });
const webCache = path.join(starterDirectory, '.tide', 'web-release');
const webEngine = path.join(webCache, 'engine');
const webObjects = (await readdir(webEngine)).filter(file => file.endsWith('.o')).sort().map(file => path.join(webEngine, file));
const webTarget = ['--target=wasm32-wasip1-threads', `--sysroot=${path.join(root, 'wasi', 'sysroot')}`, '-pthread', '-msimd128'];
const webObject = path.join(output, 'starter-test-web.o');
run(tide, ['cc', ...webTarget, '-Qunused-arguments', '-std=c17', '-O1', '-Wall', '-Wextra', '-Werror',
  '-fno-fast-math', '-ffp-contract=off',
  `-I${path.join(webCache, 'gen')}`, `-I${path.join(root, 'include')}`,
  '-c', path.join(directory, 'starter-test.c'), '-o', webObject]);
const webTest = path.join(output, 'starter-test.wasm');
run(tide, ['cc', ...webTarget, webObject, path.join(webCache, 'game.o'), ...webObjects,
  '-Wl,-z,stack-size=1048576', '-Wl,--import-memory', '-Wl,--export-memory',
  '-Wl,--shared-memory', '-Wl,--max-memory=4294967296', '-nodefaultlibs', '-lc',
  path.join(root, 'wasi', 'libclang_rt.builtins.a'), '-o', webTest]);
const webResult = run(process.execPath, [path.resolve(directory, '..', '..', 'cmake', 'run_wasi.mjs'), webTest]);
const nativeHash = nativeResult.match(/Moonwake replay hash: (\d+)/)?.[1];
const webHash = webResult.match(/Moonwake replay hash: (\d+)/)?.[1];
if (!nativeHash || nativeHash !== webHash) throw new Error('Moonwake native and web simulation hashes differ');
process.stdout.write(`Moonwake native/web hash agrees: ${nativeHash}\n`);
