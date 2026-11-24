// .tidegraph files in VS Code: Tide's graph editor (tools/tide-graph, a web page) as the file's custom text editor.
// The document stays VS Code's: undo, save, dirty state, hot exit and git's diffs are of its JSON text. The page
// sends each edit as the whole text and asks tidels through the extension's language client, which has the
// document open (its selector takes .tidegraph files), so diagnostics, types and completion are of the unsaved
// graph.
//
// Also: Tide: Convert to Tide Graph (`tide graph` on a .tide file) and Tide: Show as Tide (the graph's Tide).

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { execFile } = require('child_process');

const VIEW_TYPE = 'tide.graph';
const TEXT_SCHEME = 'tide-graph-text';

// Where the editor's files are: packaged beside the extension (graph/), or in the repository (tools/tide-graph)
function graphRoot(extensionPath) {
    for (const dir of [path.join(extensionPath, 'graph'), path.join(extensionPath, '..', 'tide-graph')]) {
        if (fs.existsSync(path.join(dir, 'files.json'))) return dir;
    }
    return null;
}

// The schema the page starts with, until tidels gives its own: packaged as graph/graph_schema.json, or the
// compiler's in the repository
function schemaCopy(root, extensionPath) {
    for (const file of [path.join(root, 'graph_schema.json'), path.join(extensionPath, '..', '..', 'compiler', 'src', 'graph_schema.json')]) {
        try {
            return JSON.parse(fs.readFileSync(file, 'utf8'));
        } catch {}
    }
    return null;
}

// A diagnostic as the page takes it: LSP's severity, and the node it's on
function pageDiagnostic(vscode, d) {
    const severity = { [vscode.DiagnosticSeverity.Error]: 1, [vscode.DiagnosticSeverity.Warning]: 2, [vscode.DiagnosticSeverity.Information]: 3, [vscode.DiagnosticSeverity.Hint]: 4 }[d.severity] ?? 1;
    return {
        severity,
        message: d.message,
        data: d.data && typeof d.data === 'object' ? d.data : undefined,
        range: { start: { line: d.range.start.line, character: d.range.start.character }, end: { line: d.range.end.line, character: d.range.end.character } },
    };
}

function register(vscode, api) {
    const { output } = api;
    const editors = new Set(); // { document, panel, post }
    const diagnostics = new Map(); // uri -> the last published, with data
    let active = null;
    const texts = new Map(); // tide-graph-text documents' contents
    const changedText = new vscode.EventEmitter();

    // Each message after the one before it: an edit is in the document before the request after it is asked
    function serial() {
        let queue = Promise.resolve();
        return step => {
            queue = queue.then(step).catch(error => output?.error?.(error));
            return queue;
        };
    }

    const provider = {
        async resolveCustomTextEditor(document, panel) {
            const root = graphRoot(api.extensionPath);
            const webview = panel.webview;
            if (!root) {
                webview.html = '<!doctype html><body>Tide: the graph editor\'s files are missing from this extension.</body>';
                return;
            }
            webview.options = { enableScripts: true, localResourceRoots: [vscode.Uri.file(root)] };
            webview.html = page(vscode, webview, root);
            const queue = serial();
            // What went between the page and the extension, the last of it, for the host test
            const log = [];
            const note = entry => { log.push(entry); if (log.length > 100) log.shift(); };
            const post = message => {
                if (message.type === 'response') note({ to: 'page', type: 'response', id: message.id, ok: message.error === undefined });
                else note({ to: 'page', type: message.type, name: message.name });
                return webview.postMessage(message);
            };
            const editor = { document, panel, post, log };
            editors.add(editor);
            active = editor;
            let fromPage = null; // The text the page last sent, which needn't go back to it
            const uri = document.uri.toString();
            const sendDiagnostics = () => post({ type: 'diagnostics', diagnostics: (diagnostics.get(uri) ?? vscode.languages.getDiagnostics(document.uri)).map(d => pageDiagnostic(vscode, d)) });

            async function handle(m) {
                switch (m?.type) {
                    case 'ready':
                        post({ type: 'load', text: document.getText(), uri, name: path.basename(document.uri.fsPath), schema: schemaCopy(root, api.extensionPath) });
                        sendDiagnostics();
                        if (!api.clientFor(document.uri)) post({ type: 'server', available: false, message: 'the Tide language server isn\'t running' });
                        return;
                    case 'edit': {
                        if (typeof m.text !== 'string' || m.text === document.getText()) return;
                        fromPage = m.text;
                        const edit = new vscode.WorkspaceEdit();
                        edit.replace(document.uri, new vscode.Range(0, 0, document.lineCount, 0), m.text);
                        if (!await vscode.workspace.applyEdit(edit)) post({ type: 'text', text: document.getText() });
                        return;
                    }
                    case 'request':
                        return answer(m);
                    case 'clipboardWrite':
                        if (typeof m.text === 'string') await vscode.env.clipboard.writeText(m.text);
                        return;
                    case 'save':
                        await document.save();
                        return;
                    case 'showTide':
                        await showAsTide(document.uri, m.text);
                        return;
                    case 'error':
                        vscode.window.showErrorMessage(`Tide: ${m.message}`);
                        return;
                    default:
                        return;
                }
            }

            async function answer(m) {
                try {
                    let result;
                    if (m.method === 'host/clipboardRead') {
                        result = await vscode.env.clipboard.readText();
                    } else if (typeof m.method === 'string' && m.method.startsWith('tide/graph')) {
                        const client = api.clientFor(document.uri);
                        if (!client) throw new Error('the Tide language server isn\'t running');
                        result = await client.sendRequest(m.method, m.params ?? null);
                    } else {
                        throw new Error(`no request ${m.method}`);
                    }
                    post({ type: 'response', id: m.id, result: result ?? null });
                } catch (error) {
                    post({ type: 'response', id: m.id, error: error?.message ?? String(error) });
                }
            }

            const subscriptions = [
                webview.onDidReceiveMessage(m => {
                    note({ from: 'page', type: m?.type, method: m?.method, id: m?.id });
                    queue(() => handle(m));
                }),
                vscode.workspace.onDidChangeTextDocument(e => {
                    if (e.document !== document || !e.contentChanges.length) return;
                    const text = document.getText();
                    if (text === fromPage) return;
                    fromPage = null;
                    post({ type: 'text', text });
                }),
                vscode.languages.onDidChangeDiagnostics(e => {
                    if (e.uris.some(u => u.toString() === uri)) sendDiagnostics();
                }),
                panel.onDidChangeViewState(e => {
                    if (e.webviewPanel.active) active = editor;
                }),
            ];
            editor.diagnostics = sendDiagnostics;
            panel.onDidDispose(() => {
                editors.delete(editor);
                if (active === editor) active = null;
                subscriptions.forEach(s => s.dispose());
            });
        },
    };

    // The graph's Tide, as tidels makes it of the document as it is; `tide graph --text` of the saved file
    // without one
    async function showAsTide(uri, given) {
        let text = typeof given === 'string' ? given : null;
        if (text === null) {
            const client = api.clientFor(uri);
            if (client) {
                try {
                    text = (await client.sendRequest('tide/graphText', { textDocument: { uri: uri.toString() } }))?.text ?? null;
                } catch {}
            }
        }
        if (text === null) {
            const tide = await api.findTide(path.dirname(uri.fsPath));
            if (!tide.file) {
                vscode.window.showErrorMessage(`Tide: ${tide.error}`);
                return;
            }
            try {
                text = await run(tide.file, ['graph', '--text', uri.fsPath], path.dirname(uri.fsPath));
            } catch (error) {
                vscode.window.showErrorMessage(`Tide: ${error.message}`);
                return;
            }
        }
        const name = path.basename(uri.fsPath).replace(/\.tidegraph$/, '') + '.tide';
        const shown = vscode.Uri.from({ scheme: TEXT_SCHEME, path: '/' + name, query: uri.toString() });
        texts.set(shown.toString(), text);
        changedText.fire(shown);
        const doc = await vscode.workspace.openTextDocument(shown);
        await vscode.window.showTextDocument(doc, { viewColumn: vscode.ViewColumn.Beside, preview: true });
    }

    function run(file, args, cwd) {
        return new Promise((resolve, reject) => execFile(file, args, { cwd, windowsHide: true, maxBuffer: 64 << 20 }, (error, stdout, stderr) => {
            if (error) reject(new Error((stderr || error.message).trim()));
            else resolve(stdout.replace(/\r\n/g, '\n'));
        }));
    }

    // Tide: Convert to Tide Graph: `tide graph` makes a .tidegraph of a .tide file; then it's opened. With both files
    // the game declares everything twice, so it asks whether the .tide file goes (--replace).
    async function convert(uri, options) {
        const file = uri?.fsPath ?? vscode.window.activeTextEditor?.document.uri.fsPath;
        if (!file || !file.endsWith('.tide')) {
            vscode.window.showErrorMessage('Tide: Convert to Tide Graph takes a .tide file.');
            return null;
        }
        let replace = options?.replace;
        if (replace === undefined) {
            const choice = await vscode.window.showInformationMessage(
                `Convert ${path.basename(file)} to a graph, ${path.basename(file)}graph?`,
                { modal: true, detail: 'Replace removes the .tide file. Keep Both leaves it, and the game then declares everything twice until one goes.' },
                'Replace', 'Keep Both');
            if (!choice) return null;
            replace = choice === 'Replace';
        }
        const document = vscode.workspace.textDocuments.find(d => d.uri.scheme === 'file' && path.relative(d.uri.fsPath, file) === '');
        if (document?.isDirty) await document.save();
        const tide = await api.findTide(path.dirname(file));
        if (!tide.file) {
            vscode.window.showErrorMessage(`Tide: ${tide.error}`);
            return null;
        }
        try {
            await run(tide.file, ['graph', file, ...(replace ? ['--replace'] : [])], path.dirname(file));
        } catch (error) {
            vscode.window.showErrorMessage(`Tide: couldn't convert ${path.basename(file)}: ${error.message}`);
            return null;
        }
        const graph = vscode.Uri.file(file + 'graph');
        await vscode.commands.executeCommand('vscode.openWith', graph, VIEW_TYPE);
        return graph;
    }

    function graphUri(uri) {
        if (uri?.fsPath?.endsWith('.tidegraph')) return uri;
        if (active) return active.document.uri;
        const doc = vscode.window.activeTextEditor?.document;
        return doc?.uri.fsPath.endsWith('.tidegraph') ? doc.uri : null;
    }

    return {
        // The language client's published diagnostics, with their data (the node), for the open graphs
        handleDiagnostics(uri, list) {
            diagnostics.set(uri.toString(), list);
            for (const e of editors) if (e.document.uri.toString() === uri.toString()) e.diagnostics();
        },
        // For tests: the open graph editors, each with a way to send its page a message
        editors,
        disposables: [
            vscode.window.registerCustomEditorProvider(VIEW_TYPE, provider, {
                webviewOptions: { retainContextWhenHidden: true },
                supportsMultipleEditorsPerDocument: false,
            }),
            vscode.workspace.registerTextDocumentContentProvider(TEXT_SCHEME, {
                onDidChange: changedText.event,
                provideTextDocumentContent: u => texts.get(u.toString()) ?? '',
            }),
            changedText,
            vscode.commands.registerCommand('tide.convertToGraph', (uri, options) => convert(uri, options)),
            vscode.commands.registerCommand('tide.showAsTide', async uri => {
                const graph = graphUri(uri);
                if (!graph) {
                    vscode.window.showErrorMessage('Tide: Show as Tide takes a .tidegraph file.');
                    return;
                }
                await showAsTide(graph);
            }),
            // Commands the page runs, for keybindings and tests: tide.graph.<name> (undo, paste, layout...)
            ...['undo', 'redo', 'copy', 'cut', 'paste', 'delete', 'duplicate', 'selectAll', 'frame', 'layout', 'search'].map(name =>
                vscode.commands.registerCommand(`tide.graph.${name}`, () => active?.post({ type: 'command', name }))),
        ],
    };
}

// The page: the editor's files from files.json, the host's side of its messages, and a policy that runs only them
function page(vscode, webview, root) {
    const files = JSON.parse(fs.readFileSync(path.join(root, 'files.json'), 'utf8'));
    const nonce = crypto.randomBytes(16).toString('base64');
    const uri = f => webview.asWebviewUri(vscode.Uri.file(path.join(root, f))).toString();
    const csp = `default-src 'none'; style-src ${webview.cspSource} 'unsafe-inline'; script-src 'nonce-${nonce}'; img-src ${webview.cspSource} data:; font-src ${webview.cspSource};`;
    return `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta http-equiv="Content-Security-Policy" content="${csp}">
<meta name="viewport" content="width=device-width, initial-scale=1">
${files.styles.map(f => `<link rel="stylesheet" href="${uri(f)}">`).join('\n')}
</head>
<body>
${files.scripts.map(f => `<script nonce="${nonce}" src="${uri(f)}"></script>`).join('\n')}
<script nonce="${nonce}">
const vscode = acquireVsCodeApi();
addEventListener('message', e => TideGraph.receive(e.data));
TideGraph.start({ post: m => vscode.postMessage(m) });
</script>
</body>
</html>`;
}

module.exports = { register, VIEW_TYPE, graphRoot };
