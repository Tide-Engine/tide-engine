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
