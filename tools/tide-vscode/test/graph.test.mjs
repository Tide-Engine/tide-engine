// The graph editor's host side in VS Code (src/graph.js) with VS Code, the webview and the language client
// replaced: what the page says becomes edits of the document and requests to tidels, and what changes comes back.
import assert from 'node:assert/strict';
import { test } from 'node:test';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const { register } = require('../src/graph.js');

function fakes({ client = true } = {}) {
    class Emitter {
        listeners = new Set();
        event = l => { this.listeners.add(l); return { dispose: () => this.listeners.delete(l) }; };
        fire(v) { for (const l of this.listeners) l(v); }
        dispose() {}
    }
    const changed = new Emitter(), diagnosticsChanged = new Emitter();
    let text = '{"tidegraph": 1, "nextId": 1, "decls": []}\n';
    const uri = { fsPath: 'C:\\game\\a.tidegraph', scheme: 'file', toString: () => 'file:///c%3A/game/a.tidegraph' };
    const document = {
        uri,
        get lineCount() { return text.split('\n').length; },
        getText: () => text,
        saved: 0,
        async save() { this.saved++; return true; },
    };
    const requests = [];
    const clipboard = { text: '' };
    const registered = {};
    const vscode = {
        EventEmitter: Emitter,
        Range: class { constructor(...a) { this.a = a; } },
        Uri: { file: p => ({ fsPath: p, toString: () => 'file:///' + p }), from: o => ({ ...o, toString: () => `${o.scheme}:${o.path}?${o.query}` }) },
        ViewColumn: { Beside: -2 },
        DiagnosticSeverity: { Error: 0, Warning: 1, Information: 2, Hint: 3 },
        WorkspaceEdit: class { replace(u, r, t) { this.text = t; } },
        workspace: {
            async applyEdit(edit) {
                text = edit.text;
                changed.fire({ document, contentChanges: [{}] });
                return true;
            },
            onDidChangeTextDocument: changed.event,
            textDocuments: [],
            registerTextDocumentContentProvider: () => ({ dispose() {} }),
            openTextDocument: async u => ({ uri: u }),
        },
        languages: { getDiagnostics: () => [], onDidChangeDiagnostics: diagnosticsChanged.event },
        env: { clipboard: { readText: async () => clipboard.text, writeText: async t => { clipboard.text = t; } } },
        window: {
            registerCustomEditorProvider: (type, provider) => { registered.provider = provider; return { dispose() {} }; },
            showErrorMessage: () => {},
            showTextDocument: async () => {},
            activeTextEditor: undefined,
        },
        commands: { registerCommand: (name, f) => { registered[name] = f; return { dispose() {} }; } },
    };
    const lsp = {
        async sendRequest(method, params) {
            requests.push({ method, params, text });
            if (method === 'tide/graphText') return { text: 'system A() { }', spans: [] };
            return null;
        },
    };
    const graphs = register(vscode, {
        output: null,
        extensionPath: new URL('..', import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1'),
        clientFor: () => (client ? lsp : null),
        findTide: async () => ({ error: 'no tide here' }),
    });
    const sent = [];
    let receive;
    const panel = {
        webview: {
            options: null,
            html: '',
            cspSource: 'vscode-resource:',
            asWebviewUri: u => ({ toString: () => 'https://webview/' + u.fsPath }),
            postMessage: async m => { sent.push(m); return true; },
            onDidReceiveMessage: l => { receive = l; return { dispose() {} }; },
        },
        onDidChangeViewState: () => ({ dispose() {} }),
        onDidDispose: () => ({ dispose() {} }),
    };
    return { vscode, graphs, registered, document, panel, sent, requests, clipboard, diagnosticsChanged, setText: t => { text = t; changed.fire({ document, contentChanges: [{}] }); },
        say: async m => { receive(m); await new Promise(r => setTimeout(r, 0)); await new Promise(r => setTimeout(r, 0)); } };
}

test('the page loads the document, and its edits and requests reach VS Code and tidels in order', async () => {
    const f = fakes();
    await f.registered.provider.resolveCustomTextEditor(f.document, f.panel);
    assert.match(f.panel.webview.html, /Content-Security-Policy/);
    assert.match(f.panel.webview.html, /src="https:\/\/webview\/.*app\.js"/, 'the page has the editor\'s scripts');
    assert.doesNotMatch(f.panel.webview.html, /script-src[^;]*'unsafe-inline'/);
    await f.say({ type: 'ready' });
    const load = f.sent.find(m => m.type === 'load');
    assert.equal(load.text, f.document.getText());
    assert.equal(load.uri, 'file:///c%3A/game/a.tidegraph');
    assert.ok(load.schema?.nodes?.length, 'the schema the page starts with');

    // An edit, then a request sent after it: tidels is asked about the edited text
    const edited = '{"tidegraph": 1, "nextId": 2, "decls": []}\n';
    f.say({ type: 'edit', text: edited });
    await f.say({ type: 'request', id: 7, method: 'tide/graphText', params: { textDocument: { uri: load.uri } } });
    assert.equal(f.document.getText(), edited);
    assert.equal(f.requests[0].text, edited);
    assert.deepEqual(f.sent.find(m => m.type === 'response' && m.id === 7).result, { text: 'system A() { }', spans: [] });
    assert.ok(!f.sent.some(m => m.type === 'text'), "the page's own edit isn't sent back to it");

    // A change made elsewhere is
    f.setText('{"tidegraph": 1, "nextId": 3, "decls": []}\n');
    assert.equal(f.sent.at(-1).type, 'text');

    // The clipboard and saving
    await f.say({ type: 'clipboardWrite', text: '{"tidegraphClip": 1, "nodes": []}' });
    assert.equal(f.clipboard.text, '{"tidegraphClip": 1, "nodes": []}');
    await f.say({ type: 'request', id: 8, method: 'host/clipboardRead' });
    assert.equal(f.sent.find(m => m.id === 8).result, '{"tidegraphClip": 1, "nodes": []}');
    await f.say({ type: 'save' });
    assert.equal(f.document.saved, 1);

    // Published diagnostics go to the page with their node
    f.graphs.handleDiagnostics(f.document.uri, [{ severity: 0, message: 'no member', data: { node: 4 }, range: { start: { line: 15, character: 10 }, end: { line: 15, character: 20 } } }]);
    const d = f.sent.filter(m => m.type === 'diagnostics').at(-1).diagnostics;
    assert.deepEqual(d[0], { severity: 1, message: 'no member', data: { node: 4 }, range: { start: { line: 15, character: 10 }, end: { line: 15, character: 20 } } });

    // The page's commands, for keybindings
    f.registered['tide.graph.undo']();
    assert.deepEqual(f.sent.at(-1), { type: 'command', name: 'undo' });
});

test('without a language server the page is told, and its requests fail', async () => {
    const f = fakes({ client: false });
    await f.registered.provider.resolveCustomTextEditor(f.document, f.panel);
    await f.say({ type: 'ready' });
    assert.ok(f.sent.some(m => m.type === 'server' && m.available === false));
    await f.say({ type: 'request', id: 1, method: 'tide/graphTypes', params: {} });
    assert.match(f.sent.find(m => m.id === 1).error, /language server/);
    await f.say({ type: 'request', id: 2, method: 'workspace/executeCommand', params: {} });
    assert.match(f.sent.find(m => m.id === 2).error, /no request/, 'only the graph requests go to tidels');
});
