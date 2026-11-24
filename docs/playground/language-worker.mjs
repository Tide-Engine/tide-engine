// The editor's language server, off the page's thread. The page sends
// {id, method, params} requests and {method, params} notifications; replies
// come back as {id, result}, published diagnostics as {diagnostics, version}.
// `open` takes the text and, for the graph playground, its document's uri
// (graph-host.mjs's GRAPH_URI); the text playground's is DOCUMENT.
import { LanguageServer, DOCUMENT } from './language.mjs';
import { loadAssets, unpackFiles } from './assets.mjs';

let server;
const waiting = [];
let starting;

async function start(text, uri = DOCUMENT) {
    const [tidels, headers] = await loadAssets(['tidels.bin', 'headers.bin']);
    server = new LanguageServer(await WebAssembly.compile(tidels), unpackFiles(headers), uri);
    forward(await server.start(text));
}

function forward(replies) {
    for (const reply of replies) {
        if (reply.method === 'textDocument/publishDiagnostics' && reply.params.uri === server.uri) {
            postMessage({ diagnostics: reply.params.diagnostics, version: server.version });
        }
    }
}

function handle(data) {
    if (data.method === 'textDocument/didChange') {
        forward(server.notify('textDocument/didChange', {
            textDocument: { uri: server.uri, version: ++server.version }, contentChanges: [{ text: data.params.text }],
        }));
        return;
    }
    const replies = [];
    const result = server.request(data.method, { textDocument: { uri: server.uri }, ...data.params }, replies);
    forward(replies);
    postMessage({ id: data.id, result });
}

// A crash in the server (a trap in its wasm) takes it down; the editor keeps
// working without it.
function safely(data) {
    if (!server) {
        if (data.id !== undefined) postMessage({ id: data.id, result: null });
        return;
    }
    try {
        handle(data);
    } catch (error) {
        server = undefined;
        postMessage({ failed: error.message ?? String(error) });
        if (data.id !== undefined) postMessage({ id: data.id, result: null });
    }
}

onmessage = async ({ data }) => {
    if (data.method === 'open') {
        starting = start(data.params.text, data.params.uri).catch(error => postMessage({ failed: error.message ?? String(error) }));
        await starting;
        starting = undefined;
        for (const queued of waiting.splice(0)) safely(queued);
    } else if (starting) waiting.push(data);
    else safely(data);
};
