// Exercise the actual adapter with editor/process boundaries replaced. No IDE,
// language server or game is launched by these tests.
import assert from 'node:assert/strict';
import { EventEmitter } from 'node:events';
import fs from 'node:fs';
import path from 'node:path';
import { test } from 'node:test';
import vm from 'node:vm';

const source = fs.readFileSync(new URL('../src/extension.js', import.meta.url), 'utf8');

function editor({ platform = 'win32', trusted = true, folders = ['C:\\game'], env = {}, files = [], realPaths = {}, settings = {}, failStarts = 0, killGroupFails = false } = {}) {
    const paths = platform === 'win32' ? path.win32 : path.posix;
    const contents = new Map(files.map(f => Array.isArray(f) ? f : [f, '']));
    const children = [], terminals = [], notifications = [], picks = [], clients = [], lifecycle = [], signals = [];
    const key = f => platform === 'win32' ? paths.normalize(f).toLowerCase() : paths.normalize(f);
    const lookup = f => [...contents].find(([name]) => key(name) === key(f));
    class Emitter {
        listeners = [];
        values = [];
        event = listener => { this.listeners.push(listener); return { dispose() {} }; };
        fire(value) { this.values.push(value); this.listeners.forEach(l => l(value)); }
        dispose() { this.listeners = []; }
    }
    const uri = f => ({ fsPath: f, scheme: 'file', toString: () => f });
    const vscode = {
        EventEmitter: Emitter,
        ThemeIcon: class { constructor(id) { this.id = id; } },
        Position: class { constructor(line, character) { this.line = line; this.character = character; } },
        Range: class { constructor(startLine, startCharacter, endLine, endCharacter) { this.start = { line: startLine, character: startCharacter }; this.end = { line: endLine, character: endCharacter }; } },
        Location: class { constructor(uri, range) { this.uri = uri; this.range = range; } },
        Uri: { file: uri, parse: uri },
        env: { asExternalUri: async value => value },
        commands: {
            getCommands: async () => ['workbench.action.browser.open'],
            executed: [],
            async executeCommand(...args) { this.executed.push(args); },
        },
        workspace: {
            isTrusted: trusted,
            workspaceFolders: folders.map(f => ({ name: paths.basename(f), uri: uri(f) })),
            getWorkspaceFolder: u => vscode.workspace.workspaceFolders.find(f => u.fsPath.startsWith(f.uri.fsPath)),
            getConfiguration: () => ({ inspect: k => settings[k] }),
        },
        window: {
            createTerminal(options) { const terminal = { options, show() {}, dispose() { options.pty.close(); } }; terminals.push(terminal); return terminal; },
            showErrorMessage(message) { notifications.push(message); },
            async showQuickPick(items) { picks.push(items); return items[0]; },
        },
    };
    const spawn = (file, args, options) => {
        const child = new EventEmitter();
        child.pid = 100 + children.length;
        child.exitCode = child.signalCode = null;
        child.stdout = new EventEmitter();
        child.stderr = new EventEmitter();
        child.stdout.setEncoding = child.stderr.setEncoding = () => {};
        child.stdin = new EventEmitter();
        child.writes = [];
        child.stdin.write = value => child.writes.push(value);
        child.kills = 0;
        child.kill = () => { child.kills++; };
        children.push({ file, args, options, child });
        return child;
    };
    const fakeFs = {
        statSync(f) { if (!lookup(f)) throw new Error('missing'); return { isFile: () => true }; },
        readFileSync(f) { const found = lookup(f); if (!found) throw new Error('missing'); return found[1]; },
        realpathSync(f) { return realPaths[f] ?? f; },
    };
    class LanguageClient {
        _pendingChangeDelayer = { cancel: () => { this.cancelledDelivery = true; } };
        constructor(id, name, server, options) { this.server = server; this.options = options; this.id = clients.length; clients.push(this); }
        async start() { lifecycle.push(`start ${this.id}`); if (this.id < failStarts) throw new Error('start failed'); }
        async dispose() { lifecycle.push(`stop ${this.id}`); await this.holdDispose; }
    }
    const sandbox = { module: { exports: {} }, setTimeout, clearTimeout, process: { platform, env, kill: (pid, signal) => { signals.push([pid, signal]); if (killGroupFails) throw new Error('group kill failed'); } }, require: name => ({
        child_process: { spawn }, fs: fakeFs, os: { homedir: () => platform === 'win32' ? 'C:\\home' : '/home/user' },
        path: paths, vscode, 'vscode-languageclient/node': { LanguageClient },
    })[name] };
    vm.createContext(sandbox);
    vm.runInContext(source, sandbox);
    return { api: vm.runInContext('({findServer, settingProgram, gameFolder, pickGame, Run, run, showPage, restart, deactivate, lensCommand})', sandbox), vscode, children, terminals, notifications, picks, clients, lifecycle, signals };
}

test('Restricted Mode skips workspace servers on PATH and keeps searching', () => {
    const e = editor({ trusted: false, env: { PATH: 'C:\\game\\bin;C:\\tide\\bin' }, files: ['C:\\game\\bin\\tidels.exe', 'C:\\tide\\bin\\tidels.exe'] });
    assert.equal(e.api.findServer().file, 'C:\\tide\\bin\\tidels.exe');
});

test('Restricted Mode checks the resolved executable and workspace paths', () => {
    for (const [folders, file, realPaths] of [
        [['C:\\game'], 'C:\\outside\\tidels.exe', { 'C:\\outside\\tidels.exe': 'C:\\game\\tidels.exe' }],
        [['C:\\linked-game'], 'C:\\actual-game\\tidels.exe', { 'C:\\linked-game': 'C:\\actual-game' }],
    ]) {
        const e = editor({ trusted: false, folders, files: [file], realPaths, settings: { 'server.path': { globalValue: file } } });
        assert.equal(e.api.settingProgram('server.path', 'language server').untrusted, true);
        assert.equal(e.api.findServer().file, undefined);
    }
});

test('Trusted workspaces retain configured and built server precedence', () => {
    const configured = 'C:\\game\\custom.exe';
    const e = editor({ files: [configured, 'C:\\game\\build\\tools\\tidels.exe'], settings: { 'server.path': { workspaceFolderValue: configured } } });
    assert.equal(e.api.findServer().file, configured);
    const built = editor({ files: ['C:\\game\\build\\tools\\tidels.exe'] });
    assert.equal(built.api.findServer().file, 'C:\\game\\build\\tools\\tidels.exe');
});

test('POSIX Restricted Mode excludes workspace PATH entries too', () => {
    const e = editor({ platform: 'linux', trusted: false, folders: ['/game'], env: { PATH: '/game/bin:/usr/local/bin' }, files: ['/game/bin/tidels', '/usr/local/bin/tidels'] });
    assert.equal(e.api.findServer().file, '/usr/local/bin/tidels');
});

test('A missing or relative installation root never selects a program in the current directory', () => {
    for (const local of [undefined, 'relative']) {
        const e = editor({ env: { LOCALAPPDATA: local }, files: [local ? 'relative\\Tide\\bin\\tidels.exe' : 'Tide\\bin\\tidels.exe'] });
        assert.equal(e.api.findServer().file, undefined);
    }
});

test('Web runs wait for the complete address at every possible output split', () => {
    const line = 'tide: the game is at http://localhost:12345/index.html\r\n';
    for (let split = 0; split < line.length - 1; split++) {
        const e = editor(), pages = [];
        const run = new e.api.Run('tide.exe', 'C:\\game', 'web', page => pages.push(page));
        e.terminals[0].options.pty.open();
        const child = e.children[0].child;
        child.stdout.emit('data', line.slice(0, split));
        assert.equal(pages.length, 0, `split ${split}`);
        child.stdout.emit('data', line.slice(split));
        child.stdout.emit('data', line);
        assert.deepEqual(pages, ['http://localhost:12345/index.html']);
        run.dispose();
    }
});

test('A disposed run never starts late or opens a stale page', () => {
    const e = editor(), pages = [];
    const run = new e.api.Run('tide.exe', 'C:\\game', 'web', page => pages.push(page));
    e.terminals[0].options.pty.open();
    run.dispose();
    e.children[0].child.stdout.emit('data', 'tide: the game is at http://localhost:1234/\n');
    assert.deepEqual(pages, []);
    const unopened = new e.api.Run('tide.exe', 'C:\\game', 'web', () => {});
    unopened.dispose();
    const before = e.children.length;
    e.terminals[1].options.pty.open();
    assert.equal(e.children.length, before);
});

test('A failed asynchronous page command is handled and explains the failure', async () => {
    const e = editor();
    const run = new e.api.Run('tide.exe', 'C:\\game', 'web', async () => { throw new Error('browser unavailable'); });
    e.terminals[0].options.pty.open();
    e.children[0].child.stdout.emit('data', 'tide: the game is at http://localhost:1234/\n');
    await new Promise(resolve => setImmediate(resolve));
    assert.match(e.notifications[0], /couldn't open.*browser unavailable/);
    run.dispose();
});

test('Windows cleanup falls back on both taskkill errors and nonzero exits', () => {
    for (const event of ['error', 'close']) {
        const e = editor(), run = new e.api.Run('tide.exe', 'C:\\game', 'desktop', null);
        e.terminals[0].options.pty.open();
        const child = e.children[0].child;
        run.stop();
        if (event === 'error') e.children[1].child.emit(event, new Error('missing taskkill'));
        else e.children[1].child.emit(event, 1);
        assert.equal(child.kills, 1);
        e.children[1].child.emit('close', 1);
        assert.equal(child.kills, 1);
    }
});

test('A late taskkill failure does not signal an exited process', () => {
    const e = editor(), run = new e.api.Run('tide.exe', 'C:\\game', 'desktop', null);
    e.terminals[0].options.pty.open();
    const child = e.children[0].child;
    run.stop();
    child.exitCode = 0;
    child.emit('close', 0);
    e.children[1].child.emit('error', new Error('missing taskkill'));
    assert.equal(child.kills, 0);
});

test('Repeated stop/terminal disposal sends one tree-kill request', () => {
    const e = editor(), run = new e.api.Run('tide.exe', 'C:\\game', 'desktop', null);
    e.terminals[0].options.pty.open();
    run.stop();
    run.dispose();
    assert.equal(e.children.length, 2);
});

test('Closing a terminal before its open callback never launches a process', () => {
    const e = editor(), run = new e.api.Run('tide.exe', 'C:\\game', 'desktop', null);
    e.terminals[0].options.pty.close();
    e.terminals[0].options.pty.open();
    assert.equal(e.children.length, 0);
    run.dispose();
});

test('Disposal waits for both the original child and Windows tree cleanup', async () => {
    for (const treeFirst of [false, true]) {
        const e = editor(), run = new e.api.Run('tide.exe', 'C:\\game', 'desktop', null);
        e.terminals[0].options.pty.open();
        let stopped = false;
        const pending = run.dispose().then(() => { stopped = true; });
        const tree = () => e.children[1].child.emit('close', 0);
        const child = () => { e.children[0].child.exitCode = 0; e.children[0].child.emit('close', 0); };
        (treeFirst ? tree : child)();
        await new Promise(resolve => setImmediate(resolve));
        assert.equal(stopped, false);
        (treeFirst ? child : tree)();
        await pending;
        assert.equal(stopped, true);
    }
});

test('A failed tree kill cannot report successful cleanup just because the parent exited', async () => {
    const e = editor(), run = new e.api.Run('tide.exe', 'C:\\game', 'desktop', null);
    e.terminals[0].options.pty.open();
    const stopped = run.stop();
    e.children[1].child.emit('close', 1);
    e.children[0].child.exitCode = 0;
    e.children[0].child.emit('close', 0);
    await assert.rejects(stopped, /Couldn't confirm cleanup.*process tree/);
    assert.equal(e.children[0].child.kills, 1);
});

test('Unconfirmed cleanup has a deadline and prevents a replacement run', async t => {
    t.mock.timers.enable({ apis: ['setTimeout'] });
    const e = editor({ files: ['C:\\tide.exe'], settings: { path: { globalValue: 'C:\\tide.exe' } } });
    await e.api.run('desktop');
    e.terminals[0].options.pty.open();
    const replacement = e.api.run('desktop');
    await new Promise(resolve => setImmediate(resolve));
    t.mock.timers.tick(4999);
    await new Promise(resolve => setImmediate(resolve));
    assert.equal(e.notifications.length, 0);
    t.mock.timers.tick(1);
    await replacement;
    assert.equal(e.terminals.length, 1);
    assert.match(e.notifications[0], /Couldn't confirm cleanup/);
    await e.api.deactivate();
});

test('POSIX stop forcefully ends the detached group, not just its parent', async () => {
    const e = editor({ platform: 'linux', folders: ['/game'] });
    const run = new e.api.Run('/bin/tide', '/game', 'desktop', null);
    e.terminals[0].options.pty.open();
    const stopped = run.stop();
    assert.equal(e.children[0].options.detached, true);
    assert.deepEqual(e.signals, [[-100, 'SIGKILL']]);
    e.children[0].child.signalCode = 'SIGKILL';
    e.children[0].child.emit('close', null);
    await stopped;
});

test('A failed POSIX group kill cannot claim the descendants were stopped', async () => {
    const e = editor({ platform: 'linux', folders: ['/game'], killGroupFails: true });
    const run = new e.api.Run('/bin/tide', '/game', 'desktop', null);
    e.terminals[0].options.pty.open();
    const stopped = run.stop();
    e.children[0].child.signalCode = 'SIGKILL';
    e.children[0].child.emit('close', null);
    await assert.rejects(stopped, /Couldn't confirm cleanup/);
    assert.equal(e.children[0].child.kills, 1);
});

test('Replacement run commands serialize through completed process-tree cleanup', async () => {
    const e = editor({ files: ['C:\\tide.exe'], settings: { path: { globalValue: 'C:\\tide.exe' } } });
    await e.api.run('desktop');
    e.terminals[0].options.pty.open();
    const replacement = e.api.run('desktop');
    await new Promise(resolve => setImmediate(resolve));
    assert.equal(e.terminals.length, 1);
    e.children[0].child.exitCode = 0;
    e.children[0].child.emit('close', 0);
    await new Promise(resolve => setImmediate(resolve));
    assert.equal(e.terminals.length, 1);
    e.children[1].child.emit('close', 0);
    await replacement;
    assert.equal(e.terminals.length, 2);
    await e.api.deactivate();
    e.terminals[1].options.pty.open();
    assert.equal(e.children.length, 2);
});

test('Page opening rechecks the run after asynchronous URI/command discovery', async () => {
    for (const delayed of ['uri', 'commands']) {
        const e = editor();
        let release, current = true;
        if (delayed === 'uri') e.vscode.env.asExternalUri = value => new Promise(resolve => { release = () => resolve(value); });
        else e.vscode.commands.getCommands = () => new Promise(resolve => { release = () => resolve(['workbench.action.browser.open']); });
        const showing = e.api.showPage('http://localhost:1234/', null, () => current);
        await new Promise(resolve => setImmediate(resolve));
        current = false;
        release();
        assert.equal(await showing, null);
        assert.deepEqual(e.vscode.commands.executed, []);
    }
});

test('The game picker offers each manifest folder once', async () => {
    const e = editor({ files: [['C:\\game\\build\\tools\\games.txt', 'one\tC:/game/one/\none\tC:/game/one/\ntwo\tC:/game/two/\n']] });
    await e.api.pickGame(null);
    assert.equal(e.picks[0].length, 2);
});

test('Typed restart and Android answers reach tide as complete lines', () => {
    const e = editor(), run = new e.api.Run('tide.exe', 'C:\\game', 'android', null);
    e.terminals[0].options.pty.open();
    run.type('r\r');
    run.type('n\x7fy\r');
    assert.deepEqual(e.children[0].child.writes, ['r\n', 'y\n']);
    assert.equal(e.children[0].options.env.TIDE_INTERACTIVE, '1');
});

test('Rapid server restarts wait for the previous server to finish stopping', async () => {
    const e = editor({ files: ['C:\\game\\build\\tools\\tidels.exe'] });
    await e.api.restart();
    let release;
    e.clients[0].holdDispose = new Promise(resolve => { release = resolve; });
    const first = e.api.restart(), second = e.api.restart();
    await new Promise(resolve => setImmediate(resolve));
    assert.equal(e.clients.length, 1);
    release();
    await Promise.all([first, second]);
    assert.deepEqual(e.lifecycle, ['start 0', 'stop 0', 'start 1', 'stop 1', 'start 2']);
    await e.api.deactivate();
    assert.equal(e.lifecycle.at(-1), 'stop 2');
    assert.ok(e.clients.every(c => c.cancelledDelivery));
});

test('A failed server start still allows restart and deactivation', async () => {
    const e = editor({ files: ['C:\\game\\build\\tools\\tidels.exe'], failStarts: 1 });
    await e.api.restart();
    await e.api.restart();
    await e.api.deactivate();
    assert.deepEqual(e.lifecycle, ['start 0', 'stop 0', 'start 1', 'stop 1']);
});

test('Clickable reference and schedule lenses use editor URI, position and location types', () => {
    for (const name of ['editor.action.showReferences', 'editor.action.goToLocations']) {
        const e = editor();
        const command = { command: name, arguments: ['file:///game.tide', { line: 2, character: 3 }, [
            { uri: 'file:///other.tide', range: { start: { line: 4, character: 5 }, end: { line: 4, character: 8 } } },
        ], 'goto'] };
        e.api.lensCommand(command);
        assert.equal(command.arguments[0].fsPath, 'file:///game.tide');
        assert.ok(command.arguments[1] instanceof e.vscode.Position);
        assert.ok(command.arguments[2][0] instanceof e.vscode.Location);
        assert.ok(command.arguments[2][0].range instanceof e.vscode.Range);
        assert.deepEqual(command.arguments[2][0].range.end, { line: 4, character: 8 });
        assert.equal(command.arguments[3], 'goto');
    }
});
