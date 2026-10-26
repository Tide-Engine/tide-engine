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
    if (process.env.TIDE_HOST_TIDE) {
        const prefab=path.join(folder,'Level.tprefab');fs.writeFileSync(prefab,'prefab Level { Transform; }\n');
        fs.writeFileSync(path.join(folder,'tide.packages'),'');
        await vscode.workspace.getConfiguration('tide').update('path',process.env.TIDE_HOST_TIDE,vscode.ConfigurationTarget.Workspace);
        const logPid=() => {
            const root=process.env.TIDE_HOST_LOGS;
            if (!fs.existsSync(root)) return null;
            for (const date of fs.readdirSync(root)) {
                const file=path.join(root,date,'window1/exthost/tide-engine.tide/Tide.log');
                if(fs.existsSync(file)){const match=/Prefab editor displayed:.*\(PID (\d+)\)/.exec(fs.readFileSync(file,'utf8'));if(match)return +match[1];}
            }
        };
        await vscode.commands.executeCommand('tide.editPrefab',vscode.Uri.file(prefab));
        const pid=await until('real native prefab frame displayed in webview',logPid);
        assert.ok(alive(pid));fs.writeFileSync(path.join(folder,'prefab-pids.json'),JSON.stringify([pid]));
        await vscode.commands.executeCommand('tide.prefabSave');
        await vscode.commands.executeCommand('tide.editPrefab',vscode.Uri.file(prefab));
        log('HOST: real native prefab backend, PNG webview display, command bridge and panel reuse passed');
    }
    // Returning ends this extension-host test. VS Code invokes deactivation;
    // the outer runner checks all PIDs after the real host has shut down.
    log('HOST: embedded page and developer tools passed; returning for real deactivation');
    fs.writeFileSync(path.join(folder, 'passed'), 'passed');
};
