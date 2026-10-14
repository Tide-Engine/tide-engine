// Real Tide -> C -> wasm compilation. Shared by the browser worker and the
// integration test; no server compilation or alternate language interpreter.
import { WASI, File, Directory, OpenFile, PreopenDirectory, ConsoleStdout } from '@bjorn3/browser_wasi_shim';

const encoder = new TextEncoder();
const decoder = new TextDecoder();
export class BrowserCompiler {
    constructor(modules, files) {
        this.modules = modules;
        this.files = files;
    }

    async compile(source, progress = () => {}) {
        const root = new Directory(new Map());
        const put = (name, bytes, readonly = false) => {
            const parts = name.replace(/^\//, '').split('/');
            let dir = root;
            for (const part of parts.slice(0, -1)) {
                if (!dir.contents.has(part)) dir.contents.set(part, new Directory(new Map()));
                dir = dir.contents.get(part);
            }
            const file = new File(bytes, { readonly });
            dir.contents.set(parts.at(-1), file);
            return file;
        };
        const get = name => {
            let node = root;
            for (const part of name.split('/')) node = node.contents.get(part);
            if (!node) throw new Error(`The compiler did not produce ${name}`);
            return node.data;
        };
        for (const [name, bytes] of Object.entries(this.files)) put(name, bytes, true);
        put('game.tide', encoder.encode(source));
        root.contents.set('out', new Directory(new Map()));
        let diagnostics = '';
        const run = async (name, args) => {
            const print = new ConsoleStdout(bytes => {
                if (diagnostics.length < 65536) diagnostics += decoder.decode(bytes);
            });
            const wasi = new WASI([name === 'lld' ? 'wasm-ld' : name, ...args], ['PWD=/', 'TIDE_HIDDEN=1'], [
                new OpenFile(new File([])), print, print, new PreopenDirectory('/', root.contents),
            ], { debug: false });
            const imports = { wasi_snapshot_preview1: wasi.wasiImport };
            if (name === 'tidec') imports.env = {
                memory: new WebAssembly.Memory({ initial: 512, maximum: 65536, shared: true }),
            };
            const instance = await WebAssembly.instantiate(this.modules[name], imports);
            const status = wasi.start(instance);
            if (status !== 0) throw new Error(diagnostics.trim() || `${name} exited with code ${status}`);
        };
        progress('Checking Tide');
        await run('tidec', ['game.tide', '-o', 'out', '--name', 'game', '--layout']);
        progress('Compiling');
        const common = ['-cc1', '-triple', 'wasm32-unknown-wasi', '-emit-obj', '-x', 'c', '-std=c17', '-O1',
            '-ffp-contract=off', '-fvisibility=hidden', '-fno-common',
            '-target-feature', '+atomics', '-target-feature', '+bulk-memory', '-target-feature', '+mutable-globals',
            '-target-feature', '+simd128', '-D_REENTRANT', '-D__wasm_threads__=1',
            '-internal-isystem', '/lib/clang/22/include', '-internal-isystem', '/include', '-I/out'];
        await run('clang', [...common, '/out/game.c', '-o', '/out/game.o']);
        await run('clang', [...common, '/main.c', '-o', '/out/main.o']);
        progress('Linking');
        await run('lld', ['--allow-undefined', '--import-memory', '--export-memory', '--shared-memory',
            '--max-memory=4294967296', '-z', 'stack-size=1048576', '--strip-all', '-L/lib',
            '/lib/crt1-command.o', '/out/game.o', '/out/main.o', '-ltide_platform', '-ltide',
            '-lc', '-lm', '-lclang_rt.builtins', '-o', '/out/game.wasm']);
        const wasm = get('out/game.wasm').slice();
        const pack = root.contents.get('out').contents.get('game.pack')?.data.slice() ?? new Uint8Array();
        return { wasm, pack, diagnostics: diagnostics.trim() };
    }
}
