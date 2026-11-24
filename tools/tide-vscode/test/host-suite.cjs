const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vscode = require('vscode');

const wait = ms => new Promise(resolve => setTimeout(resolve, ms));
async function until(label, check, timeout = 30_000) {
    const end = Date.now() + timeout;
    do {
        const value = await check();
        if (value) return value;
        await wait(50);
    } while (Date.now() < end);
    assert.fail(`Timed out: ${label}`);
}
const alive = pid => { try { process.kill(pid, 0); return true; } catch { return false; } };

// A .tidegraph in the real custom editor: the page loads it, edits made through the page (commands the page runs, as
// its keys do) reach VS Code's document, tidels answers the page and publishes the graph's diagnostics on its node,
// and the document saves as the page wrote it.
async function graphEditor(folder, extension, log) {
    const file = path.join(folder, 'grow.tidegraph');
    const text = [
        '{',
        '  "tidegraph": 1,',
        '  "nextId": 7,',
        '  "decls": [',
        '    {',
        '      "kind": "system",',
        '      "id": 1,',
        '      "name": "Grow",',
        '      "params": [',
        '        {"id": 2, "mode": "mut", "type": "Body", "name": "body"}',
        '      ],',
        '      "body": {',
        '        "entry": 6,',
        '        "nodes": [',
        '          {"id": 3, "k": "name", "x": 0, "y": 0, "name": "body"},',
        '          {"id": 4, "k": "member", "x": 200, "y": 0, "object": 3, "member": "missing"},',
        '          {"id": 5, "k": "float", "x": 200, "y": 100, "text": "1.0"},',
        '          {"id": 6, "k": "assign", "x": 400, "y": 0, "op": "+=", "target": 4, "value": 5}',
        '        ]',
        '      }',
        '    }',
        '  ]',
        '}',
        '',
    ].join('\n');
    fs.writeFileSync(file, text);
    const uri = vscode.Uri.file(file);
    await vscode.commands.executeCommand('vscode.openWith', uri, 'tide.graph');
    const editors = extension.exports.graphEditors();
    const same = (a, b) => path.relative(a, b) === ''; // VS Code writes the drive's letter small
    const editor = await until('the graph editor opens', () => [...editors].find(e => same(e.document.uri.fsPath, file)));
    const document = editor.document;
    const sent = (match) => editor.log.some(match);
    await until('the page loads the document', () => sent(m => m.from === 'page' && m.type === 'ready') && sent(m => m.to === 'page' && m.type === 'load'));
    // The page asks tidels for the graph's Tide and types, and gets answers
    await until('tidels answers the page', () => {
        const asked = editor.log.filter(m => m.from === 'page' && m.method === 'tide/graphText').map(m => m.id);
        return editor.log.some(m => m.to === 'page' && m.type === 'response' && m.ok && asked.includes(m.id));
    });
    // The unknown member's error is on its node, in VS Code and sent to the page
    const onNode = () => vscode.languages.getDiagnostics(uri).find(d => d.severity === vscode.DiagnosticSeverity.Error && d.data?.node === 4);
    await until('an error on the member node', onNode);
    assert.equal(onNode().range.start.line, 15, "the error is on the node's JSON line");
    assert.ok(sent(m => m.to === 'page' && m.type === 'diagnostics'));
    // Edits through the page: everything selected and deleted is one edit of VS Code's document
    editor.post({ type: 'command', name: 'selectAll' });
    editor.post({ type: 'command', name: 'delete' });
    await until('the page deletes the nodes in the document', () => document.getText().includes('"nodes": []'));
    assert.ok(document.isDirty);
    assert.ok(sent(m => m.from === 'page' && m.type === 'edit'));
    // The page's undo writes the text it was, byte for byte
    editor.post({ type: 'command', name: 'undo' });
    await until('undo through the page', () => document.getText() === text);
    // Copy and paste go through VS Code's clipboard: pasted nodes have new ids, and the wires between them follow
    await vscode.env.clipboard.writeText('');
    editor.post({ type: 'command', name: 'selectAll' });
    editor.post({ type: 'command', name: 'copy' });
    await until('copied nodes on the clipboard', async () => (await vscode.env.clipboard.readText()).includes('tidegraphClip'));
    editor.post({ type: 'command', name: 'paste' });
    await until('pasted nodes in the document', () => (document.getText().match(/"k": "assign"/g) || []).length === 2);
    const graph = JSON.parse(document.getText());
    const nodes = graph.decls[0].body.nodes;
    assert.equal(nodes.length, 8);
    assert.equal(new Set(nodes.map(n => n.id)).size, 8, 'pasted ids are new');
    assert.ok(graph.nextId > Math.max(...nodes.map(n => n.id)));
    const pasted = nodes.find(n => n.k === 'assign' && n.id !== 6);
    assert.ok(nodes.some(n => n.id === pasted.target && n.k === 'member'), "the pasted assignment's target is the pasted member");
    // VS Code saves the document as the page wrote it: LF, tidec's layout
    assert.ok(await document.save());
    assert.equal(fs.readFileSync(file, 'utf8'), document.getText());
    assert.doesNotMatch(document.getText(), /\r/);
    // The member made right, the error goes
    const fixed = new vscode.WorkspaceEdit();
    fixed.replace(uri, new vscode.Range(0, 0, document.lineCount, 0), document.getText().replace(/"member": "missing"/g, '"member": "size"'));
    assert.ok(await vscode.workspace.applyEdit(fixed));
    await until('the page reads the text VS Code changed', () => sent(m => m.to === 'page' && m.type === 'text'));
    await until('no error once the member is there', () => !vscode.languages.getDiagnostics(uri).some(d => d.severity === vscode.DiagnosticSeverity.Error));
    await document.save();
    // Show as Tide: the graph's Tide, from tidels
    await vscode.commands.executeCommand('tide.showAsTide', uri);
    const tide = await until('the Tide of the graph', () => vscode.workspace.textDocuments.find(d => d.uri.scheme === 'tide-graph-text'));
    assert.match(tide.getText(), /system Grow\(mut Body body\)/);
    assert.match(tide.getText(), /body\.size \+= 1\.0;/);
    // Convert to Tide Graph: `tide graph` beside a .tide file, opened in the graph editor
    if (fs.existsSync(process.env.TIDE_HOST_TIDE)) {
        await vscode.workspace.getConfiguration('tide').update('path', process.env.TIDE_HOST_TIDE, vscode.ConfigurationTarget.Workspace);
        fs.writeFileSync(path.join(folder, 'shrink.tide'), 'system Shrink(mut Body body)\n{\n    body.size -= 1;\n}\n');
        const made = await vscode.commands.executeCommand('tide.convertToGraph', vscode.Uri.file(path.join(folder, 'shrink.tide')), { replace: true });
        assert.ok(made && fs.existsSync(made.fsPath), 'the graph is made');
        assert.ok(!fs.existsSync(path.join(folder, 'shrink.tide')), '--replace takes the .tide file away');
        await until('the made graph opens in the graph editor', () => [...editors].some(e => same(e.document.uri.fsPath, made.fsPath)));
        await vscode.workspace.getConfiguration('tide').update('path', process.env.TIDE_HOST_NODE, vscode.ConfigurationTarget.Workspace);
    } else {
        log('HOST: no tide beside tidels: Convert to Tide Graph not tried');
    }
    // Closed, as an open custom editor keeps VS Code from restarting the extension host (the folders' test below)
    const tabs = vscode.window.tabGroups.all.flatMap(g => g.tabs).filter(t => t.input instanceof vscode.TabInputCustom && t.input.viewType === 'tide.graph');
    assert.ok(await vscode.window.tabGroups.close(tabs));
    await until('the graph editors close', () => editors.size === 0);
}

exports.run = async function () {
    const folder = process.env.TIDE_HOST_WORKSPACE;
    const log = text => fs.appendFileSync(path.join(folder, 'host.log'), text + '\n');
    log('Real VS Code ' + vscode.version);
    const document = await vscode.workspace.openTextDocument(path.join(folder, 'main.tide'));
    await vscode.window.showTextDocument(document);
    const extension = vscode.extensions.getExtension('tide-engine.tide');
    assert.ok(extension);
    await extension.activate();
    const hover = () => vscode.commands.executeCommand('vscode.executeHoverProvider', document.uri, new vscode.Position(4, 10));
    await until('real language client hover', async () => (await hover())?.length);
    assert.equal(document.languageId, 'tide');
    const references = await vscode.commands.executeCommand('vscode.executeReferenceProvider', document.uri, new vscode.Position(0, 24));
    assert.ok(references.some(r => r.range.start.line === 4));
    const lenses = await vscode.commands.executeCommand('vscode.executeCodeLensProvider', document.uri);
    const lens = lenses.find(l => l.command?.command === 'editor.action.showReferences');
    assert.ok(lens, 'The language client must return a clickable reference lens');
    assert.ok(lens.command.arguments[0] instanceof vscode.Uri);
    assert.ok(lens.command.arguments[1] instanceof vscode.Position);
    assert.ok(lens.command.arguments[2][0] instanceof vscode.Location);
    await vscode.commands.executeCommand(lens.command.command, ...lens.command.arguments);
    await vscode.commands.executeCommand('closeReferenceSearch');
    const rename = await vscode.commands.executeCommand('vscode.executeDocumentRenameProvider', document.uri, new vscode.Position(0, 24), 'size');
    assert.ok(await vscode.workspace.applyEdit(rename));
    assert.match(document.getText(), /body\.size/);
    await until('renamed hover', async () => (await hover())?.length);
    const edit = new vscode.WorkspaceEdit();
    edit.replace(document.uri, new vscode.Range(4, 9, 4, 13), 'missing');
    assert.ok(await vscode.workspace.applyEdit(edit));
    await until('live diagnostics', () => vscode.languages.getDiagnostics(document.uri).some(d => d.severity === vscode.DiagnosticSeverity.Error));
    const fix = new vscode.WorkspaceEdit();
    fix.replace(document.uri, new vscode.Range(4, 9, 4, 16), 'size');
    assert.ok(await vscode.workspace.applyEdit(fix));
    await until('diagnostics clear after edit', () => !vscode.languages.getDiagnostics(document.uri).some(d => d.severity === vscode.DiagnosticSeverity.Error));
    await Promise.all([vscode.commands.executeCommand('tide.restartServer'), vscode.commands.executeCommand('tide.restartServer')]);
    await until('hover after rapid real server restarts', async () => (await hover())?.length);
    log('HOST: diagnostics, edits, rename, navigation, clickable lens, restart passed');

    await graphEditor(folder, extension, log);
    log('HOST: graph editor, its edits through the page, tidels, saving, Show as Tide and Convert to Tide Graph passed');

    // Two real language clients, each using its project's installed version, and an actual pin-file change.
    const other = path.join(path.dirname(folder), 'other-game');
    fs.mkdirSync(other);
    fs.writeFileSync(path.join(other, 'other.tide'), 'component OtherBody { int value; }\nscene Main { }\n');
    fs.writeFileSync(path.join(other, 'tide.packages'), 'tide = 0.6.0\n');
    for (const version of ['0.5.0', '0.6.0']) {
        const root = path.join(process.env.TIDE_VERSIONS, version);
        fs.mkdirSync(path.join(root, 'bin'), { recursive: true });
        fs.writeFileSync(path.join(root, 'VERSION'), version);
        fs.copyFileSync(process.env.TIDE_HOST_SERVER, path.join(root, 'bin', path.basename(process.env.TIDE_HOST_SERVER)));
    }
    fs.writeFileSync(path.join(folder, 'tide.packages'), 'tide = 0.5.0\n');
    await vscode.workspace.getConfiguration('tide').update('server.path', undefined, vscode.ConfigurationTarget.Workspace);
    await vscode.workspace.getConfiguration('tide').update('path', undefined, vscode.ConfigurationTarget.Workspace);
    assert.ok(vscode.workspace.updateWorkspaceFolders(1, 0, { uri: vscode.Uri.file(other) }));
    const otherDocument = await vscode.workspace.openTextDocument(path.join(other, 'other.tide'));
    await until('second project uses its own real server', async () =>
        (await vscode.commands.executeCommand('vscode.executeHoverProvider', otherDocument.uri, new vscode.Position(0, 13)))?.length);
    await until('first project retains its real server', async () => (await hover())?.length);
    fs.writeFileSync(path.join(folder, 'tide.packages'), 'tide = 0.6.0\n');
    // Observe the actual selected command in the language-client output, rather than accepting an old hover.
    const switched = () => {
        const root = process.env.TIDE_HOST_LOGS;
        for (const date of fs.readdirSync(root)) {
            const file = path.join(root, date, 'window1/exthost/tide-engine.tide/Tide.log');
            if (fs.existsSync(file) && fs.readFileSync(file, 'utf8').toLowerCase().includes(`0.6.0${path.sep}bin${path.sep}tidels${process.platform === 'win32' ? '.exe' : ''} (${folder})`.toLowerCase())) return true;
        }
        return false;
    };
    await until('pin-file watcher switches the first project to 0.6.0', switched);
    await until('hover after pin-file restart', async () => (await hover())?.length);
    log('HOST: two pinned project servers and automatic pin switch passed');
    // Simulate a development editor crashing without removing its selection file. The IDE must notice the
    // process exit itself and select the normal pin, rather than wait for another Unity launch to repair it.
    const { spawn, execFile } = require('node:child_process');
    const session = spawn(process.env.TIDE_HOST_NODE, ['-e',
        `setInterval(()=>{try{process.kill(${process.env.TIDE_HOST_RUNNER_PID},0)}catch{process.exit(0)}},100)`],
        { windowsHide:true, stdio:'ignore' });
    await new Promise((resolve,reject)=>{session.once('spawn',resolve);session.once('error',reject);});
    const lockFile = path.join(folder,'Packages','packages-lock.json'), cacheFile = path.join(folder,'Library','Tide','toolchain.json');
    const readLogs = () => fs.readdirSync(process.env.TIDE_HOST_LOGS).map(date=>{
        const file=path.join(process.env.TIDE_HOST_LOGS,date,'window1/exthost/tide-engine.tide/Tide.log');
        return fs.existsSync(file)?fs.readFileSync(file,'utf8').toLowerCase():'';
    }).join('\n');
    try {
        const windows=process.platform==='win32';
        const args=windows?['-NoProfile','-NonInteractive','-Command',`([DateTimeOffset][Diagnostics.Process]::GetProcessById(${session.pid}).StartTime).ToUnixTimeSeconds()`]
            :['-p',String(session.pid),'-o','lstart='];
        const text=await new Promise((resolve,reject)=>execFile(windows?'powershell.exe':'ps',args,
            {windowsHide:true,env:{...process.env,LC_ALL:'C',TZ:'UTC'}},(error,stdout)=>error?reject(error):resolve(stdout.trim())));
        const started=windows?Number(text):Math.floor(Date.parse(text+' UTC')/1000);
        fs.mkdirSync(path.dirname(lockFile),{recursive:true});fs.mkdirSync(path.dirname(cacheFile),{recursive:true});
        fs.writeFileSync(lockFile,JSON.stringify({dependencies:{'com.tide.unity':{source:'git',hash:'session-test',version:'url#v0.6.0'}}}));
        const command=path.join(process.env.TIDE_VERSIONS,'0.5.0','bin','tide'+(windows?'.exe':''));
        fs.writeFileSync(cacheFile,JSON.stringify({format:2,commit:'session-test',tide:command,sessionPid:session.pid,sessionStarted:started}));
        let after=readLogs().length;
        await vscode.commands.executeCommand('tide.restartServer');
        await until('development session selects its own real language server',()=>readLogs().slice(after).includes(`0.5.0${path.sep}bin${path.sep}tidels`));
        after=readLogs().length;
        session.kill();
        await until('session exit automatically restores the pin with an unchanged cache file',()=>
            readLogs().slice(after).includes(`0.6.0${path.sep}bin${path.sep}tidels`));
        await until('hover after development session exit',async()=>(await hover())?.length);
        assert.ok(fs.existsSync(cacheFile),'the test must exercise a stale cache, not file deletion');
        log('HOST: development session exit restores the project pin without restarting the IDE');
    } finally {
        if(session.exitCode===null)session.kill();
        if(fs.existsSync(lockFile))fs.unlinkSync(lockFile);
        if(fs.existsSync(cacheFile))fs.unlinkSync(cacheFile);
    }
    // Restore the CLI fixture used by the process-lifetime checks below.
    fs.writeFileSync(path.join(folder, 'tide.packages'), '');
    await vscode.workspace.getConfiguration('tide').update('server.path', process.env.TIDE_HOST_SERVER, vscode.ConfigurationTarget.Workspace);
    await vscode.workspace.getConfiguration('tide').update('path', process.env.TIDE_HOST_NODE, vscode.ConfigurationTarget.Workspace);

    const recorded = () => fs.readdirSync(folder).filter(f => /^pids-\d+\.json$/.test(f));
    await vscode.commands.executeCommand('tide.run');
    const firstFile = await until('first real terminal process tree', () => recorded()[0]);
    const first = JSON.parse(fs.readFileSync(path.join(folder, firstFile)));
    assert.ok(first.every(alive));
    await vscode.commands.executeCommand('tide.run');
    const secondFile = await until('replacement real process tree', () => recorded().find(f => f !== firstFile));
    const second = JSON.parse(fs.readFileSync(path.join(folder, secondFile)));
    assert.ok(first.every(pid => !alive(pid)), 'Replacement must wait for the preceding tree to exit');
    assert.ok(second.every(alive));
    vscode.window.terminals.find(t => /tide run\)/.test(t.name)).dispose();
    await until('terminal close kills all descendants', () => second.every(pid => !alive(pid)));
    log('HOST: real terminal replacement and descendant cleanup passed');

    await vscode.commands.executeCommand('tide.runWeb');
    const webFile = await until('web process tree', () => recorded().find(f => f !== firstFile && f !== secondFile));
    const web = JSON.parse(fs.readFileSync(path.join(folder, webFile)));
    await until('embedded browser loads the complete split URL and executes JS', () => fs.existsSync(path.join(folder, `page-${web[0]}`)), 60_000);
    await vscode.commands.executeCommand('tide.openDevTools');
    // Returning ends this extension-host test. VS Code invokes deactivation;
    // the outer runner checks all PIDs after the real host has shut down.
    log('HOST: embedded page and developer tools passed; returning for real deactivation');
    fs.writeFileSync(path.join(folder, 'passed'), 'passed');
};
