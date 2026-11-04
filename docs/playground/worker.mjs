import { BrowserCompiler } from './compiler.mjs';
import { loadAssets, unpackFiles } from './assets.mjs';

let compiler;
async function initialize() {
    const [tidec, clang, lld, packed] = await loadAssets(['tidec.bin', 'clang.bin', 'lld.bin', 'files.bin'],
        percent => postMessage({ type: 'progress', text: `Loading compiler · ${percent}%` }));
    const [tidecModule, clangModule, lldModule] = await Promise.all([tidec, clang, lld].map(bytes => WebAssembly.compile(bytes)));
    compiler = new BrowserCompiler({ tidec: tidecModule, clang: clangModule, lld: lldModule }, unpackFiles(packed));
}

// The page submits one compile at a time; the worker stays responsive to the
// page because compilation never runs on the page's thread.
onmessage = async ({ data }) => {
    try {
        if (!compiler) await initialize();
        const started = performance.now();
        const result = await compiler.compile(data.source, text => postMessage({ type: 'progress', id: data.id, text }));
        postMessage({ type: 'built', id: data.id, ...result, seconds: (performance.now() - started) / 1000 }, [result.wasm.buffer, result.pack.buffer]);
    } catch (error) {
        postMessage({ type: 'error', id: data.id, text: error.message ?? String(error) });
    }
};
