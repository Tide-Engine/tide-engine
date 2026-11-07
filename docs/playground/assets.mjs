// The staged playground payloads (stage.mjs): fetched, checked against the
// manifest and decompressed, for the compiler's and the editor's workers.
const base = '/playground/generated/';

export async function loadAssets(names, progress = () => {}) {
    const manifestResponse = await fetch(base + 'manifest.json');
    if (!manifestResponse.ok) throw new Error('Live editing is unavailable in this docs build.');
    const manifest = await manifestResponse.json();
    for (const name of names) {
        if (!manifest.assets[name]) throw new Error(`This docs build has no ${name}. Rebuild docs_playground.`);
    }
    let received = 0;
    const total = names.reduce((sum, name) => sum + manifest.assets[name].size, 0);
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
            progress(Math.min(100, Math.round(received / total * 100)));
        }
        const compressed = new Uint8Array(chunks.reduce((sum, chunk) => sum + chunk.length, 0));
        let at = 0;
        for (const chunk of chunks) { compressed.set(chunk, at); at += chunk.length; }
        const digest = new Uint8Array(await crypto.subtle.digest('SHA-256', compressed));
        const hex = [...digest].map(byte => byte.toString(16).padStart(2, '0')).join('');
        if (hex !== manifest.assets[name].sha256) throw new Error('The compiler files changed while loading. Reload this page and try again.');
        return new Uint8Array(await new Response(new Blob([compressed]).stream().pipeThrough(new DecompressionStream('gzip'))).arrayBuffer());
    }
    return Promise.all(names.map(asset));
}

// A bundle of files (files.bin, headers.bin): JSON of names to base64.
export function unpackFiles(bytes) {
    return Object.fromEntries(Object.entries(JSON.parse(new TextDecoder().decode(bytes))).map(([name, encoded]) => {
        const text = atob(encoded);
        return [name, Uint8Array.from(text, char => char.charCodeAt(0))];
    }));
}
