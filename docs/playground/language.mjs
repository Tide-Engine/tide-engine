// The real tidels, compiled to a WASI reactor (tidels.c), behind a small
// JSON-RPC client. Shared by the editor's worker and the integration test.
import { WASI, File, Directory, OpenFile, PreopenDirectory, ConsoleStdout } from '@bjorn3/browser_wasi_shim';

const encoder = new TextEncoder();
const decoder = new TextDecoder();
export const DOCUMENT = 'file:///game.tide';

export class LanguageServer {
    // `files` are the compiler bundle's: the engine headers hovers and
    // completions describe built-ins from.
    constructor(module, files) {
        const root = new Directory(new Map());
        for (const [name, bytes] of Object.entries(files)) {
            if (!name.startsWith('include/tide/')) continue;
            const parts = name.split('/');
            let dir = root;
            for (const part of parts.slice(0, -1)) {
                if (!dir.contents.has(part)) dir.contents.set(part, new Directory(new Map()));
                dir = dir.contents.get(part);
            }
            dir.contents.set(parts.at(-1), new File(bytes, { readonly: true }));
        }
        this.log = '';
        const print = new ConsoleStdout(bytes => {
            if (this.log.length < 65536) this.log += decoder.decode(bytes);
        });
        this.wasi = new WASI(['tidels'], ['PWD=/', 'TIDE_HIDDEN=1'], [
            new OpenFile(new File([])), print, print, new PreopenDirectory('/', root.contents),
        ], { debug: false });
        this.memory = new WebAssembly.Memory({ initial: 256, maximum: 65536, shared: true });
        this.module = module;
        this.next = 1;
        this.version = 0;
    }

    async start(text) {
        this.instance = await WebAssembly.instantiate(this.module, {
            wasi_snapshot_preview1: this.wasi.wasiImport, env: { memory: this.memory },
        });
        this.wasi.initialize(this.instance);
        this.request('initialize', { processId: null, rootUri: 'file:///', capabilities: {} });
        this.notify('initialized', {});
        return this.notify('textDocument/didOpen', {
            textDocument: { uri: DOCUMENT, languageId: 'tide', version: ++this.version, text },
        });
    }

    // Sends one message; returns every message tidels sent back.
    send(message) {
        const exports = this.instance.exports;
        const bytes = encoder.encode(JSON.stringify(message));
        const at = exports.tidels_input(bytes.length);
        new Uint8Array(this.memory.buffer, at, bytes.length).set(bytes);
        exports.tidels_handle(bytes.length);
        const length = exports.tidels_output_len();
        if (!length) return [];
        // A copy: TextDecoder refuses views of shared memory.
        const out = new Uint8Array(length);
        out.set(new Uint8Array(this.memory.buffer, exports.tidels_output(), length));
        return decoder.decode(out).split('\0').filter(Boolean).map(text => JSON.parse(text));
    }

    notify(method, params) {
        return this.send({ jsonrpc: '2.0', method, params });
    }

    // The request's result, or null when it failed; `replies` gets the rest.
    request(method, params, replies = []) {
        const id = this.next++;
        let result = null;
        for (const reply of this.send({ jsonrpc: '2.0', id, method, params })) {
            if (reply.id === id && !reply.method) result = reply.error ? null : reply.result;
            else replies.push(reply);
        }
        return result;
    }

    // The whole new text; returns its diagnostics (LSP Diagnostic[]).
    change(text) {
        const replies = this.notify('textDocument/didChange', {
            textDocument: { uri: DOCUMENT, version: ++this.version }, contentChanges: [{ text }],
        });
        return diagnosticsOf(replies);
    }

    at(method, line, character, extra = {}) {
        return this.request(method, { textDocument: { uri: DOCUMENT }, position: { line, character }, ...extra });
    }
}

// The diagnostics for the playground's document among `replies`, or
// undefined when none were published.
export function diagnosticsOf(replies) {
    let found;
    for (const reply of replies) {
        if (reply.method === 'textDocument/publishDiagnostics' && reply.params.uri === DOCUMENT) found = reply.params.diagnostics;
    }
    return found;
}
