// The editor in a headless, muted Chrome or Edge: a page of its files (files.json) given a graph tidec made, whose
// nodes, ports and wires have to be in the DOM, and an edit made through it that comes back to the host as text.
//
//   node --test tools/tide-graph/test/browser.test.mjs
//
// The browser is TIDE_BROWSER, else Chrome or Edge where they install; with none, the test is skipped.
import assert from 'node:assert/strict';
import { execFileSync, spawn } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { test } from 'node:test';
import { pathToFileURL } from 'node:url';
import { findProgram, repo, root } from './load.mjs';

function findBrowser() {
    if (process.env.TIDE_BROWSER) return process.env.TIDE_BROWSER;
    const places = process.platform === 'win32' ? [
        path.join(process.env.ProgramFiles || 'C:\\Program Files', 'Google/Chrome/Application/chrome.exe'),
        path.join(process.env['ProgramFiles(x86)'] || 'C:\\Program Files (x86)', 'Google/Chrome/Application/chrome.exe'),
        path.join(process.env.LOCALAPPDATA || '', 'Google/Chrome/Application/chrome.exe'),
        path.join(process.env['ProgramFiles(x86)'] || 'C:\\Program Files (x86)', 'Microsoft/Edge/Application/msedge.exe'),
        path.join(process.env.ProgramFiles || 'C:\\Program Files', 'Microsoft/Edge/Application/msedge.exe'),
    ] : process.platform === 'darwin' ? [
        '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
        '/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge',
    ] : ['/usr/bin/google-chrome', '/usr/bin/chromium', '/usr/bin/chromium-browser', '/usr/bin/microsoft-edge'];
    return places.find(p => fs.existsSync(p)) || null;
}

const browser = findBrowser();

test('a graph renders with its nodes, ports and wires, and an edit comes back as text', { skip: !browser && 'no Chrome or Edge (set TIDE_BROWSER)' }, async () => {
    const files = JSON.parse(fs.readFileSync(path.join(root, 'files.json'), 'utf8'));
    // index.html, the page to work on the editor with, loads the same files
    const index = fs.readFileSync(path.join(root, 'index.html'), 'utf8');
    assert.deepEqual([...index.matchAll(/<script src="([^"]+)"/g)].map(m => m[1]), files.scripts, 'index.html loads files.json\'s scripts');

    const scratch = fs.mkdtempSync(path.join(os.tmpdir(), 'tide-graph-browser-'));
    try {
        const tidec = findProgram('tidec', 'TIDE_TIDEC');
        const graphFile = path.join(scratch, 'play.tidegraph');
        execFileSync(tidec, ['--to-graph', path.join(repo, 'examples/sumo/play.tide'), '-o', graphFile], { stdio: 'pipe' });
        const text = fs.readFileSync(graphFile, 'utf8');
        const schema = JSON.parse(fs.readFileSync(path.join(repo, 'compiler/src/graph_schema.json'), 'utf8'));
        // The page's checks write what they found into #result, which --dump-dom prints
        const check = `
            const posted = [];
            const host = { post: m => {
                posted.push(m);
                if (m.type === 'request') setTimeout(() => TideGraph.receive({ type: 'response', id: m.id, error: 'no tidels in this test' }));
            } };
            const app = TideGraph.start(host);
            const result = { errors: [] };
            addEventListener('error', e => result.errors.push(String(e.message)));
            try {
                TideGraph.receive({ type: 'load', text: ${JSON.stringify(text)}, schema: ${JSON.stringify(schema)}, uri: 'file:///play.tidegraph', name: 'play' });
                const ed = app.editor;
                // The graph with the most nodes
                const best = ed.doc.graphs().map(g => g.slot).sort((a, b) => ed.doc.graph(b).nodes.length - ed.doc.graph(a).nodes.length)[0];
                ed.show(best);
                const g = ed.graph;
                const s = ed.schema;
                result.nodes = g.nodes.length;
                result.nodeViews = document.querySelectorAll('.tg-node:not(.tg-pseudo)').length;
                result.pseudo = document.querySelectorAll('.tg-node.tg-pseudo').length;
                result.ports = g.nodes.reduce((c, n) => c + s.ports(n).length, 0);
                result.portViews = document.querySelectorAll('.tg-node:not(.tg-pseudo) .tg-port').length;
                result.wires = g.nodes.reduce((c, n) => c + s.dataWires(n).length + s.execWires(n).length, 0) + (g.entry || g.root ? 1 : 0);
                result.wireViews = document.querySelectorAll('.tg-wires path').length;
                result.execWires = document.querySelectorAll('.tg-wires path.tg-wire-exec').length;
                result.goodPaths = [...document.querySelectorAll('.tg-wires path')].filter(p => /^M -?[0-9.]+ -?[0-9.]+ C /.test(p.getAttribute('d')) && !p.getAttribute('d').includes('NaN')).length;
                result.outline = document.querySelectorAll('.tg-tree-row').length;
                result.decls = ed.doc.decls.length;
                // An edit: a node added from the search's catalog, then undone
                const before = posted.filter(m => m.type === 'edit').length;
                app.addFromSearch({ node: { k: 'int', fields: { text: '7' } } }, { x: 0, y: 0 }, null);
                const edits = posted.filter(m => m.type === 'edit');
                result.edited = edits.length - before;
                result.editHasNode = edits.length ? edits.at(-1).text.includes('"k": "int", "x": 0, "y": 0, "text": "7"') : false;
                result.nodeViewsAfter = document.querySelectorAll('.tg-node:not(.tg-pseudo)').length;
                result.inspector = document.querySelector('.tg-inspector-title')?.textContent || '';
                app.command('undo');
                result.undone = posted.filter(m => m.type === 'edit').at(-1).text === ${JSON.stringify(text)};
                result.nodeViewsUndone = document.querySelectorAll('.tg-node:not(.tg-pseudo)').length;
                // Text from the host (its own undo, a checkout) is read again
                TideGraph.receive({ type: 'text', text: ${JSON.stringify(text)}.replace('"BACK"', '"FRONT"') });
                result.external = ed.doc.decls[0].name;
                // Diagnostics on a node mark it
                const marked = g.nodes[0].id;
                ed.show(best);
                TideGraph.receive({ type: 'diagnostics', diagnostics: [{ severity: 1, message: 'wrong', data: { node: marked }, range: { start: { line: 0, character: 0 }, end: { line: 0, character: 1 } } }] });
                result.marked = !!document.querySelector('.tg-node.tg-has-error[data-id="' + marked + '"]');
                result.problems = document.querySelectorAll('.tg-problem').length;
                result.light = getComputedStyle(document.body).backgroundColor;
            } catch (e) {
                result.errors.push(String(e && e.stack || e));
            }
            document.getElementById('result').textContent = JSON.stringify(result);
        `;
        const page = `<!doctype html><html><head><meta charset="utf-8"><title>test</title>
            ${files.styles.map(f => `<link rel="stylesheet" href="${pathToFileURL(path.join(root, f)).href}">`).join('\n')}
            </head><body style="width: 1400px; height: 900px">
            ${files.scripts.map(f => `<script src="${pathToFileURL(path.join(root, f)).href}"></script>`).join('\n')}
            <pre id="result" style="display:none">none</pre>
            <script>${check}</script></body></html>`;
        const pageFile = path.join(scratch, 'page.html');
        fs.writeFileSync(pageFile, page);
        const profile = path.join(scratch, 'profile');
        const out = await new Promise((resolve, reject) => {
            const child = spawn(browser, ['--headless=new', '--mute-audio', '--disable-gpu', '--no-first-run', '--no-default-browser-check',
                '--disable-extensions', `--user-data-dir=${profile}`, '--window-size=1400,900', '--allow-file-access-from-files',
                '--virtual-time-budget=5000', '--dump-dom', pathToFileURL(pageFile).href], { stdio: ['ignore', 'pipe', 'pipe'], windowsHide: true });
            let stdout = '', stderr = '';
            child.stdout.on('data', d => { stdout += d; });
            child.stderr.on('data', d => { stderr += d; });
            const timer = setTimeout(() => { child.kill(); reject(new Error('the browser took over 60 s: ' + stderr.slice(-2000))); }, 60000);
            child.on('error', reject);
            child.on('close', () => { clearTimeout(timer); resolve({ stdout, stderr }); });
        });
        const found = /<pre id="result"[^>]*>([^<]*)<\/pre>/.exec(out.stdout);
        assert.ok(found, 'the page has a result: ' + out.stderr.slice(-2000));
        const r = JSON.parse(found[1].replace(/&quot;/g, '"').replace(/&amp;/g, '&').replace(/&lt;/g, '<').replace(/&gt;/g, '>'));
        assert.deepEqual(r.errors, []);
        assert.ok(r.nodes > 50, `${r.nodes} nodes in the biggest graph`);
        assert.equal(r.nodeViews, r.nodes, 'a node in the DOM for each');
        assert.equal(r.pseudo, 1, 'Start or Result');
        assert.equal(r.portViews, r.ports, 'a port in the DOM for each');
        assert.equal(r.wireViews, r.wires, 'a wire in the DOM for each');
        assert.equal(r.goodPaths, r.wires, 'every wire is a bezier between two places');
        assert.ok(r.execWires > 0);
        assert.ok(r.outline >= r.decls, 'the outline has the declarations');
        assert.equal(r.edited, 1, 'one edit, one text');
        assert.ok(r.editHasNode, 'the text has the new node, as tidec writes nodes');
        assert.equal(r.nodeViewsAfter, r.nodes + 1);
        assert.match(r.inspector, /Int/);
        assert.ok(r.undone, 'undo writes the text it was');
        assert.equal(r.nodeViewsUndone, r.nodes);
        assert.equal(r.external, 'FRONT');
        assert.ok(r.marked, 'a diagnostic marks its node');
        assert.equal(r.problems, 1);
    } finally {
        fs.rmSync(scratch, { recursive: true, force: true });
    }
});
