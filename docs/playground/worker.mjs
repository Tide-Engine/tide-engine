import { BrowserCompiler } from './compiler.mjs';

let compiler;
async function initialize() {
    const base = '/playground/generated/';
    const manifestResponse = await fetch(base + 'manifest.json');
    if (!manifestResponse.ok) throw new Error('Live editing is unavailable in this docs build.');
    const manifest = await manifestResponse.json();
    let received = 0;
    const total = Object.values(manifest.assets).reduce((sum, asset) => sum + asset.size, 0);
    async function asset(name) {
        const response = await fetch(base + name);
        if (!response.ok) throw new Error(`Could not load ${name}: HTTP ${response.status}`);
        const chunks = [];
        const reader = response.body.getReader();
        for (;;) {
            const { value, done } = await reader.read();
            if (done) break;
            chunks.push(value);
            received += value.length;
            postMessage({ type: 'progress', text: `Loading compiler · ${Math.min(100, Math.round(received / total * 100))}%` });
        }
        const compressed = new Uint8Array(chunks.reduce((sum, chunk) => sum + chunk.length, 0));
        let at = 0;
        for (const chunk of chunks) { compressed.set(chunk, at); at += chunk.length; }
        const digest = new Uint8Array(await crypto.subtle.digest('SHA-256', compressed));
        const hex = [...digest].map(byte => byte.toString(16).padStart(2, '0')).join('');
        if (hex !== manifest.assets[name].sha256) throw new Error('The compiler files changed while loading. Reload this page and try again.');
        return new Uint8Array(await new Response(new Blob([compressed]).stream().pipeThrough(new DecompressionStream('gzip'))).arrayBuffer());
    }
    const [tidec, clang, lld, packed] = await Promise.all(['tidec.bin', 'clang.bin', 'lld.bin', 'files.bin'].map(asset));
    const [tidecModule, clangModule, lldModule] = await Promise.all([tidec, clang, lld].map(bytes => WebAssembly.compile(bytes)));
    const files = Object.fromEntries(Object.entries(JSON.parse(new TextDecoder().decode(packed))).map(([name, encoded]) => {
        const text = atob(encoded);
        return [name, Uint8Array.from(text, char => char.charCodeAt(0))];
    }));
    compiler = new BrowserCompiler({ tidec: tidecModule, clang: clangModule, lld: lldModule }, files);
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
