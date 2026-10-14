// Tide for VS Code (and Cursor, VSCodium, Windsurf): the grammar colors
// the text, and tidels, the language server that comes with tide, does the rest.
// Tide: Run plays the game in a window, Run on the Web beside the code, and
// Run on Android on the phone or emulator that's connected.
//
// In Restricted Mode (an untrusted workspace), nothing from the workspace
// runs: the server is the one that comes with tide, and games don't run.

const { spawn } = require('child_process');
const net = require('net');
const fs = require('fs');
const os = require('os');
const path = require('path');
const vscode = require('vscode');
const { LanguageClient } = require('vscode-languageclient/node');
const { editorPage } = require('./prefab-client.js');

// Pinned client 10.1.2 cancels watched-file delivery on disposal, but leaves
// full-document delivery scheduled. After a restart that timer tries to reuse
// a disposed connection and fails in Delayer. The next client opens the latest
// document text, so pending delivery belongs only to the client being stopped.
class TideLanguageClient extends LanguageClient {
    dispose(timeout) {
        this._pendingChangeDelayer.cancel();
        return super.dispose(timeout);
    }
}

const suffix = process.platform === 'win32' ? '.exe' : '';
let client = null;
let output = null; // The Tide output channel, which every client shares
let trace = null; // Where tide.trace.server shows what VS Code and tidels say

function isFile(file) {
    try {
        return fs.statSync(file).isFile();
    } catch {
        return false;
    }
}

function openFolders() {
    return (vscode.workspace.workspaceFolders ?? []).map(f => f.uri.fsPath);
}

// Whether `file` is `folder` or in it.
function inFolder(file, folder) {
    const relative = path.relative(folder, file);
    return relative !== '..' && !relative.startsWith('..' + path.sep) && !path.isAbsolute(relative);
}

// Check both spellings: a PATH entry or a user setting may point through a
// symlink/junction, and an open workspace folder may itself be one.
function inWorkspace(file) {
    const real = p => {
        try { return fs.realpathSync(p); } catch { return p; }
    };
    const resolved = real(file);
    return openFolders().some(folder => inFolder(file, folder) || inFolder(resolved, real(folder)));
}

// The setting tide.<key> for `resource` (a game's folder, or none for the
// language server, which serves them all), and the workspace folder it comes
// from: the one whose settings set it (the resource's, or for the server, the
// first that does); else, for user and workspace settings, the resource's, or
// the first. Only the user's own settings count until the workspace is
// trusted.
function setting(key, resource) {
    const folders = vscode.workspace.workspaceFolders ?? [];
    const own = resource ? vscode.workspace.getWorkspaceFolder(resource) : undefined;
    const trusted = vscode.workspace.isTrusted;
    if (trusted) {
        for (const folder of resource ? (own ? [own] : []) : folders) {
            const value = vscode.workspace.getConfiguration('tide', folder.uri).inspect(key)?.workspaceFolderValue;
            if (value) return { value, folder: folder.uri.fsPath };
        }
    }
    const inspected = vscode.workspace.getConfiguration('tide', resource).inspect(key);
    const value = (trusted ? inspected?.workspaceValue : undefined) || inspected?.globalValue || '';
    return { value, folder: (own ?? folders[0])?.uri.fsPath };
}

// The path a setting holds: `~` is the home folder, `${workspaceFolder}` and
// relative paths are in `folder`, and `${workspaceFolder:name}` is the open
// folder of that name. null when it names a folder that isn't open.
function settingPath(value, folder) {
    const home = os.homedir();
    let missing = false;
    let file = value.trim()
        .replace(/^~(?=$|[\\/])/, home)
        .replace(/\$\{userHome\}/g, home)
        .replace(/\$\{workspaceFolder(?::([^}]*))?\}/g, (_, name) => {
            const found = name ? vscode.workspace.workspaceFolders?.find(f => f.name === name)?.uri.fsPath : folder;
            if (!found) missing = true;
            return found ?? '';
        });
    if (missing) return null;
    if (!path.isAbsolute(file)) {
        if (!folder) return null;
        file = path.resolve(folder, file);
    }
    return path.normalize(file);
}

// The program the setting tide.<key> names, `what` it is: null when it's
// empty; else { file }, or { error } saying why there's none to run, and
// whether that's only for want of trust (`untrusted`).
function settingProgram(key, what, resource) {
    const { value, folder } = setting(key, resource);
    if (!value) return null;
    const named = `tide.${key} is set to ${value}`;
    const file = settingPath(value, folder);
    if (!file) return { error: `${named}, which is in a folder that isn't open.` };
    const executable = process.platform === 'win32' && !isFile(file) && isFile(file + '.exe') ? file + '.exe' : file;
    if (!vscode.workspace.isTrusted && inWorkspace(executable)) {
        return { error: `${named}, which is in this workspace: it runs once you trust the workspace.`, untrusted: true };
    }
    return isFile(executable) ? { file: executable } : { error: `${named}, and there's no ${what} there.` };
}

// A program that comes with tide: on PATH, else where the installers put
// tide, as an editor started before tide was installed doesn't have it on its
// PATH yet. Only PATH's absolute folders: a relative one would be wherever
// the editor runs.
function installedProgram(name) {
    const exe = name + suffix;
    const allowed = file => isFile(file) && (vscode.workspace.isTrusted || !inWorkspace(file));
    for (const dir of (process.env.PATH ?? '').split(path.delimiter)) {
        if (dir && path.isAbsolute(dir) && allowed(path.join(dir, exe))) return path.join(dir, exe);
    }
    if (process.platform === 'win32' && !path.isAbsolute(process.env.LOCALAPPDATA ?? '')) return null;
    const installed = process.platform === 'win32'
        ? path.join(process.env.LOCALAPPDATA, 'Tide', 'bin', exe)
        : path.join(os.homedir(), '.tide', 'bin', exe);
    return allowed(installed) ? installed : null;
}

// Says a program is missing, offering to install tide or to change `setting`.
async function missing(message, setting) {
    const choice = await vscode.window.showErrorMessage(message, 'Install tide', 'Settings');
    if (choice === 'Install tide') {
        vscode.env.openExternal(vscode.Uri.parse('https://tide-engine.dev/guide/install'));
    } else if (choice === 'Settings') {
        vscode.commands.executeCommand('workbench.action.openSettings', setting);
    }
}

// The language server: the setting's; else the open folder's own
// build/tools/tidels, when it's Tide itself; else the one that comes with
// tide. In an untrusted workspace, never one in the workspace. { file }, or
// { error }.
function findServer() {
    const configured = settingProgram('server.path', 'language server');
    if (configured && !configured.untrusted) return configured;
    if (vscode.workspace.isTrusted) {
        for (const folder of openFolders()) {
            const built = path.join(folder, 'build', 'tools', 'tidels' + suffix);
            if (isFile(built)) return { file: built };
        }
    }
    const installed = installedProgram('tidels');
    if (installed) return { file: installed };
    return configured ?? { error: "couldn't find tidels, the language server that comes with tide. Install tide, or set tide.server.path." };
}

// The trace of what VS Code and tidels say to each other, as tide.trace.server
// asks. The language client only traces into a log channel whose level is
// Trace, so this one is at Trace while the setting isn't off.
function traceChannel() {
    const channel = vscode.window.createOutputChannel('Tide Trace');
    const changed = new vscode.EventEmitter();
    const level = () => vscode.workspace.getConfiguration('tide').get('trace.server') === 'off' ? vscode.LogLevel.Info : vscode.LogLevel.Trace;
    const listener = vscode.workspace.onDidChangeConfiguration(e => {
        if (e.affectsConfiguration('tide.trace.server')) changed.fire(level());
    });
    const write = message => channel.appendLine(`[${new Date().toLocaleTimeString()}] ${message instanceof Error ? message.message : message}`);
    return {
        name: channel.name,
        get logLevel() {
            return level();
        },
        onDidChangeLogLevel: changed.event,
        trace: write,
        debug: write,
        info: write,
        warn: write,
        error: write,
        append: value => channel.append(value),
        appendLine: value => channel.appendLine(value),
        replace: value => channel.replace(value),
        clear: () => channel.clear(),
        show: (...args) => channel.show(...args),
        hide: () => channel.hide(),
        dispose: () => {
            listener.dispose();
            changed.dispose();
            channel.dispose();
        },
    };
}

// A lens that goes somewhere when clicked runs one of VS Code's own commands,
// which tidels names with their arguments as JSON: a document, a position in
// it and the places to show. VS Code takes those as its own types.
function lensCommand(command) {
    if (command?.command !== 'editor.action.showReferences' && command?.command !== 'editor.action.goToLocations') return;
    const [uri, position, locations, ...rest] = command.arguments ?? [];
    if (typeof uri !== 'string' || !position || !Array.isArray(locations)) return;
    const range = r => new vscode.Range(r.start.line, r.start.character, r.end.line, r.end.character);
    command.arguments = [
        vscode.Uri.parse(uri),
        new vscode.Position(position.line, position.character),
        locations.map(l => new vscode.Location(vscode.Uri.parse(l.uri), range(l.range))),
        ...rest,
    ];
}

// Starts the language server. It never throws: what went wrong shows in a
// notification, so that Restart Language Server always has a server to stop.
async function start() {
    const server = findServer();
    if (server.error) {
        missing(`Tide: ${server.error}`, 'tide.server.path'); // Not awaited: the editor goes on meanwhile
        return;
    }
    const next = new TideLanguageClient('tide', 'Tide', { command: server.file }, {
        documentSelector: [
            { scheme: 'file', language: 'tide' },
            { scheme: 'untitled', language: 'tide' },
        ],
        outputChannel: output,
        traceOutputChannel: trace,
        middleware: {
            provideCodeLenses: async (document, token, next) => {
                const lenses = await next(document, token);
                for (const lens of lenses ?? []) lensCommand(lens.command);
                return lenses;
            },
        },
    });
    client = next;
    try {
        await next.start();
    } catch {
        // The client said why, in a notification and in the Tide output.
    }
}

// Stops the language server, whatever state it's in: a server that failed to
// start can't be stopped, only let go.
async function stop() {
    const running = client;
    client = null;
    if (!running) return;
    try {
        await running.dispose();
    } catch {
        // It never started, or it stopped already.
    }
}

// Starts and stops one after the other, as settings changes and the restart
// command come in.
let queue = Promise.resolve();
function serially(step) {
    queue = queue.then(step).catch(() => {});
    return queue;
}

function restart() {
    return serially(async () => {
        await stop();
        await start();
    });
}

// ---------------------------------------------------------------------------
// Run, Run on the Web and Run on Android

// The games of the open folders that build them with CMake, like Tide
// itself: tide_add_game lists them in build/tools/games.txt, a game and one of
// its files per line, or a folder ending in '/' (see cmake/Tide.cmake).
function manifestGames() {
    const games = [];
    for (const folder of openFolders()) {
        let text;
        try {
            text = fs.readFileSync(path.join(folder, 'build', 'tools', 'games.txt'), 'utf8');
        } catch {
            continue;
        }
        for (const line of text.split(/\r?\n/)) {
            const tab = line.indexOf('\t');
            if (tab > 0) games.push({ name: line.slice(0, tab), path: line.slice(tab + 1) });
        }
    }
    return games;
}

// A folder's tide.packages (see docs/tools/packages.md), as lines without
// their comments, or null when it has none.
function packagesLines(folder) {
    try {
        return fs.readFileSync(path.join(folder, 'tide.packages'), 'utf8').split(/\r?\n/)
            .map(line => line.replace(/(^|\s)#.*$/, '').trim()).filter(line => line);
    } catch {
        return null;
    }
}

// The package a folder is, by its `package` line, or null for a game.
function packageName(folder) {
    const line = (packagesLines(folder) ?? []).find(l => /^package\s/.test(l));
    return line ? line.split(/\s+/)[1] : null;
}

// Where tide keeps packages from git, as compiler/lsp/packages.c says.
function packagesCache() {
    if (process.env.TIDE_PACKAGES) return process.env.TIDE_PACKAGES;
    if (process.platform === 'win32') return path.join(process.env.LOCALAPPDATA ?? '.', 'Tide', 'packages');
    return path.join(os.homedir(), '.tide', 'packages');
}

// Whether the game in `game` lists the package in `folder`: a folder, or a
// repository's commit (with //sub for a folder in it) in tide's cache.
function listsPackage(game, folder) {
    for (const line of packagesLines(game) ?? []) {
        const [source, commit] = line.split(/\s+/);
        if (source === 'package' || source === 'tide') continue;
        let dir;
        if (/^(\.|\/|\\|[A-Za-z]:)/.test(source)) {
            dir = path.resolve(game, source);
        } else if (commit) {
            const slash = source.indexOf('/');
            const at = source.lastIndexOf('@');
            const plain = at > slash ? source.slice(0, at) : source;
            const [repo, sub = ''] = plain.split('//');
            dir = path.join(packagesCache(), ...repo.split('/'), commit, ...sub.split('/').filter(p => p));
        } else {
            continue;
        }
        if (path.relative(dir, folder) === '') return true;
    }
    return false;
}

// The folder `tide run` plays for the .tide file `file`, found as tidels finds
// the game a file is in (game_folder in compiler/lsp/server.c): the folder a
// manifest lists it in; else the innermost folder with a tide.packages it's
// in, up to the open folder it's in, and for a package, the open folder whose
// game lists it; else the innermost open folder it's in, unless a manifest
// lists games there, whose other files stand alone; else its own folder.
// { error } when tide run can't play it.
function gameFolder(file) {
    const games = manifestGames();
    const game = games.find(g => g.path.endsWith('/') ? inFolder(file, g.path) : path.relative(g.path, file) === '');
    if (game) {
        if (game.path.endsWith('/')) return { folder: path.resolve(game.path) };
        return { error: `${path.basename(file)} is in ${game.name}, which CMake builds from a list of files, and tide run plays a whole folder.` };
    }
    const root = openFolders().filter(f => inFolder(file, f)).sort((a, b) => b.length - a.length)[0];
    for (let dir = path.dirname(file); ; dir = path.dirname(dir)) {
        if (packagesLines(dir)) {
            const name = packageName(dir);
            if (!name) return { folder: dir };
            const user = openFolders().find(f => !packageName(f) && listsPackage(f, dir));
            if (user) return { folder: user };
            return { error: `${path.basename(file)} is in package ${name}, and tide run plays a game: run one whose tide.packages lists it.` };
        }
        if ((root && path.relative(root, dir) === '') || path.dirname(dir) === dir) break;
    }
    if (!root) return { folder: path.dirname(file) };
    if (games.some(g => inFolder(g.path, root))) {
        return { error: `${path.basename(file)} is in none of the games build/tools/games.txt lists.` };
    }
    return { folder: root };
}

// The game to run: the active .tide file's; else the last one run; else the
// one game there is, or the one picked from the open folders' games.
async function pickGame(last) {
    const document = vscode.window.activeTextEditor?.document;
    if (document?.languageId === 'tide' && document.uri.scheme === 'file') {
        const found = gameFolder(document.uri.fsPath);
        if (found.error) vscode.window.showErrorMessage(`Tide: ${found.error}`);
        return found.folder;
    }
    if (last) return last;
    const games = manifestGames();
    const folders = [...new Set([
        ...games.filter(g => g.path.endsWith('/')).map(g => path.resolve(g.path)),
        ...openFolders().filter(f => !games.some(g => inFolder(g.path, f))),
    ])];
    if (folders.length <= 1) {
        if (!folders.length) vscode.window.showErrorMessage('Tide: open a .tide file of the game to run.');
        return folders[0];
    }
    const picked = await vscode.window.showQuickPick(
        folders.map(f => ({ label: path.basename(f), description: f, folder: f })),
        { placeHolder: 'The game to run' });
    return picked?.folder;
}

// How each kind of run runs tide, and its terminal's icon
const KINDS = {
    desktop: { args: ['run'], icon: 'play' },
    web: { args: ['run', '--web', '--no-open'], icon: 'globe' },
    android: { args: ['run', '--android'], icon: 'device-mobile' },
};

// `tide run` in a terminal of its own, which shows tide's output and the
// game's, and sends tide what's typed there (r and Enter starts the game
// over, and y answers whether to download Android's tools). On the web, tide
// serves the page without opening a browser, and `onPage` gets its address
// once tide says where it is.
class Run {
    constructor(tide, folder, kind, onPage) {
        this.output = new vscode.EventEmitter();
        this.closed = new vscode.EventEmitter();
        this.process = null;
        this.line = ''; // Typed, not sent yet
        this.ended = false;
        this.disposed = false;
        this.stopping = false;
        this.stopped = null;
        const args = KINDS[kind].args;
        this.terminal = vscode.window.createTerminal({
            name: `${path.basename(folder)} (tide ${args.slice(0, 2).join(' ')})`,
            iconPath: new vscode.ThemeIcon(KINDS[kind].icon),
            pty: {
                onDidWrite: this.output.event,
                onDidClose: this.closed.event,
                open: () => this.start(tide, folder, args, onPage),
                close: () => this.stop(),
                handleInput: data => this.type(data),
            },
        });
        this.terminal.show(true);
    }

    print(text) {
        this.output.fire(text.replace(/\r?\n/g, '\r\n'));
    }

    start(tide, folder, args, onPage) {
        if (this.disposed || this.stopping) return;
        this.print(`${folder}> tide ${args.join(' ')}\n`);
        // Outside Windows, tide and the game it starts get a process group of
        // their own, which stop() ends. TIDE_INTERACTIVE: someone can answer
        // tide's questions here, though its input isn't a terminal.
        const child = spawn(tide, args, {
            cwd: folder,
            windowsHide: true,
            detached: process.platform !== 'win32',
            env: { ...process.env, TIDE_INTERACTIVE: '1' },
        });
        this.process = child;
        let seen = ''; // tide's output until it says where the page is
        child.stdout.setEncoding('utf8');
        child.stdout.on('data', text => {
            if (!this.disposed && onPage && seen !== null) {
                seen += text;
                const page = /tide: the game is at (http:\/\/\S+)\s/.exec(seen); // Wait for the address's delimiter, even across chunks
                if (page) {
                    seen = null;
                    const failed = error => {
                        if (!this.disposed) vscode.window.showErrorMessage(`Tide: couldn't open the game's page: ${error.message}`);
                    };
                    try { Promise.resolve(onPage(page[1])).catch(failed); } catch (error) { failed(error); }
                }
            }
            this.print(text);
        });
        child.stderr.setEncoding('utf8');
        child.stderr.on('data', text => this.print(text));
        child.stdin.on('error', () => {}); // Typed after tide stopped
        child.on('error', error => this.print(`Tide: couldn't run ${tide}: ${error.message}\n`));
        child.on('close', () => {
            this.process = null;
            this.ended = true;
            this.print('\ntide stopped. Press any key to close this terminal.\n');
        });
    }

    type(data) {
        if (this.ended) {
            this.closed.fire();
            return;
        }
        if (data === '\x03') { // Ctrl+C
            this.print('^C\n');
            this.stop();
            return;
        }
        if (data.startsWith('\x1b')) return; // Arrows and other keys that aren't text
        for (const c of data) {
            if (c === '\r') {
                this.print('\n');
                this.process?.stdin.write(this.line + '\n');
                this.line = '';
            } else if (c === '\x7f') {
                if (this.line) this.output.fire('\b \b');
                this.line = this.line.slice(0, -1);
            } else if (c >= ' ') {
                this.line += c;
                this.output.fire(c);
            }
        }
    }

    // Stops tide, and the game it started, which would keep running without it.
    stop() {
        if (this.stopped) return this.stopped;
        this.stopping = true;
        const child = this.process;
        const pid = child?.pid;
        if (!pid) return this.stopped = Promise.resolve();
        const ended = child.exitCode !== null || child.signalCode !== null
            ? Promise.resolve() : new Promise(resolve => child.once('close', resolve));
        let requested = Promise.resolve(true);
        if (process.platform === 'win32') {
            let attempted = false;
            const fallback = () => {
                if (attempted || child.exitCode !== null || child.signalCode !== null) return;
                attempted = true;
                try { child.kill(); } catch {} // The deadline reports an unconfirmed exit
            };
            requested = new Promise(resolve => {
                spawn('taskkill', ['/pid', String(pid), '/t', '/f'], { windowsHide: true })
                    .on('error', () => { fallback(); resolve(false); })
                    .on('close', code => { if (code !== 0) fallback(); resolve(code === 0); });
            });
        } else {
            try {
                process.kill(-pid, 'SIGKILL'); // Stop ends the whole group, including a child that ignores SIGTERM
            } catch {
                requested = Promise.resolve(false);
                try { child.kill('SIGKILL'); } catch {} // The deadline reports an unconfirmed exit
            }
        }
        // A successful taskkill closes the whole tree. Wait for it as well as
        // tide's pipes before allowing a replacement to bind the same resources.
        const problem = () => new Error(`Couldn't confirm cleanup of the game process tree (tide PID ${pid}). Stop the remaining game processes and reload the editor window before running again.`);
        let timer;
        const deadline = new Promise((_, reject) => {
            timer = setTimeout(() => reject(problem()), 5000);
            timer.unref();
        });
        this.stopped = Promise.race([Promise.all([requested, ended]), deadline]).then(([treeStopped]) => {
            if (!treeStopped) throw problem();
        }).finally(() => clearTimeout(timer));
        // Terminal close and subscription disposal cannot await promises. Keep
        // their failure visible and handled; replacement still awaits rejection.
        this.stopped.catch(error => {
            this.print(`Tide: ${error.message}\n`);
            vscode.window.showErrorMessage(`Tide: ${error.message}`);
        });
        return this.stopped;
    }

    dispose() {
        this.disposed = true;
        const stopped = this.stop();
        this.terminal.dispose();
        this.output.dispose();
        this.closed.dispose();
        return stopped;
    }
}

// Shows the page beside the code: in VS Code's integrated browser, in the tab
// of the last run's page if it's still open; else in the Simple Browser, which
// other editors built on VS Code have; else in the system's browser.
async function showPage(address, lastAddress, current) {
    const url = (await vscode.env.asExternalUri(vscode.Uri.parse(address))).toString(true);
    const commands = await vscode.commands.getCommands(true);
    if (!current()) return null;
    if (commands.includes('workbench.action.browser.open')) {
        await vscode.commands.executeCommand('workbench.action.browser.open',
            { url, openToSide: true, reuseUrlFilter: lastAddress ?? undefined });
        return url;
    }
    try {
        await vscode.commands.executeCommand('simpleBrowser.api.open', vscode.Uri.parse(url),
            { viewColumn: vscode.ViewColumn.Beside });
    } catch {
        if (!current()) return null;
        await vscode.env.openExternal(vscode.Uri.parse(url));
    }
    return url;
}

const runs = { desktop: null, web: null, android: null }; // Running, or that ran last
const runQueues = { desktop: Promise.resolve(), web: Promise.resolve(), android: Promise.resolve() };
let deactivating = false;
let lastGame = null;
let lastPage = null; // The web run's address in the editor

// Runs the game, starting it over if it's running: in a window of its own, on
// the web, in the editor, or on Android. Running a game runs its code, so not
// in an untrusted workspace.
function run(kind) {
    runQueues[kind] = runQueues[kind].then(() => beginRun(kind)).catch(error => {
        output?.error(error);
    });
    return runQueues[kind];
}

async function beginRun(kind) {
    if (deactivating) return;
    if (!vscode.workspace.isTrusted) {
        const choice = await vscode.window.showErrorMessage(
            "Tide: running a game runs its code, so it only runs once you trust this workspace.", 'Manage Workspace Trust');
        if (choice) vscode.commands.executeCommand('workbench.trust.manage');
        return;
    }
    const game = await pickGame(lastGame);
    if (!game || deactivating) return;
    const configured = settingProgram('path', 'tide', vscode.Uri.file(game));
    const tide = configured ? configured.file : installedProgram('tide');
    if (!tide) {
        await missing(configured ? `Tide: ${configured.error}` : "Tide: couldn't find tide. Install it, or set tide.path.", 'tide.path');
        return;
    }
    lastGame = game;
    await runs[kind]?.dispose();
    if (deactivating) return;
    const next = new Run(tide, game, kind, kind === 'web' ? async address => {
        const current = () => runs[kind] === next && !next.disposed && !deactivating;
        const page = await showPage(address, lastPage, current);
        if (page && current()) {
            lastPage = page;
            vscode.commands.executeCommand('setContext', 'tide.gamePage', true);
        }
    } : null);
    runs[kind] = next;
}

// The developer tools of the game's page, whose console shows what each
// reload did: the integrated browser's own, on the game's tab; or, for the
// Simple Browser, the window's, which reach into the pages it shows.
async function openDevTools() {
    if (!lastPage) {
        vscode.window.showInformationMessage('Tide: run the game on the web first (Tide: Run on the Web).');
        return;
    }
    const commands = await vscode.commands.getCommands(true);
    if (commands.includes('workbench.action.browser.open') && commands.includes('workbench.action.browser.toggleDevTools')) {
        // Without a url, the page's tab comes forward without loading again.
        await vscode.commands.executeCommand('workbench.action.browser.open', { openToSide: true, reuseUrlFilter: lastPage });
        await vscode.commands.executeCommand('workbench.action.browser.toggleDevTools');
        return;
    }
    await vscode.commands.executeCommand('workbench.action.webview.openDeveloperTools');
}

// The native authoring app remains open independently of this IDE window.
async function openPrefab(resource) {
    if (!vscode.workspace.isTrusted) {
        vscode.window.showErrorMessage('Tide: prefab preview can run project code; trust this workspace before opening the editor.');
        return;
    }
    const document = vscode.window.activeTextEditor?.document;
    const uri = resource ?? document?.uri;
    if (uri?.scheme !== 'file' || path.extname(uri.fsPath).toLowerCase() !== '.tprefab') {
        vscode.window.showErrorMessage('Tide: select or open a .tprefab file.'); return;
    }
    const found = gameFolder(uri.fsPath);
    if (found.error) { vscode.window.showErrorMessage(`Tide: ${found.error}`); return; }
    const configured = settingProgram('path', 'tide', vscode.Uri.file(found.folder));
    const tide = configured ? configured.file : installedProgram('tide');
    if (!tide) {
        await missing(configured ? `Tide: ${configured.error}` : "Tide: couldn't find tide. Install it, or set tide.path.", 'tide.path'); return;
    }
    if (document?.uri.fsPath === uri.fsPath && document.isDirty && !await document.save()) return;
    const child = spawn(tide, ['open', uri.fsPath, '--project', found.folder],
        { cwd: found.folder, windowsHide: true, detached: true, stdio: 'ignore' });
    child.on('error', error => vscode.window.showErrorMessage(`Tide: couldn't open the prefab editor: ${error.message}`));
    child.unref();
}

const prefabPanels = new Map();
const prefabProcesses = new Set();
class PrefabPanel {
    constructor(executable, uri, folder) {
        this.uri = uri;
        this.panel = vscode.window.createWebviewPanel('tide.prefabEditor', path.basename(uri.fsPath), vscode.ViewColumn.Active,
            { enableScripts: true, retainContextWhenHidden: true, localResourceRoots: [] });
        this.buffer = Buffer.alloc(0); this.size = 0; this.busy = false; this.disposed = false;
        const nonce = require('crypto').randomBytes(16).toString('hex');
        prefabProcesses.add(this);
        this.finished = new Promise(resolve => { this.finish = () => { prefabProcesses.delete(this); resolve(); }; });
        this.connections = new Set();
        this.server = net.createServer(socket => {
            if (this.disposed || this.transport || this.connections.size >= 4) { socket.destroy(); return; }
            this.connections.add(socket); socket.on('close', () => this.connections.delete(socket));
            socket.on('error', () => {}); socket.setTimeout(5000, () => socket.destroy());
            let hello = Buffer.alloc(0);
            const authenticate = data => {
                hello = Buffer.concat([hello,data]); const end = hello.indexOf(10);
                if (end < 0) { if (hello.length > 256) socket.destroy(); return; }
                if (hello.subarray(0,end).toString('ascii') !== `TIDEHELLO 1 ${nonce}` || this.disposed) { socket.destroy(); return; }
                this.transport = socket; socket.setTimeout(0); this.server.close();
                for (const other of this.connections) if (other !== socket) other.destroy();
                socket.removeListener('data',authenticate); socket.on('data',bytes => this.read(bytes));
                if (hello.length > end+1) this.read(hello.subarray(end+1));
                socket.on('close', () => { if (!this.disposed && !this.ended) this.fail('Native prefab frame connection closed. See Tide output.'); });
            };
            socket.on('data',authenticate);
        });
        this.panel.onDidDispose(() => this.dispose());
        this.ready = new Promise(resolve => {
            this.server.on('error', error => { this.fail(`Couldn't open the native frame connection: ${error.message}`); resolve(); });
            this.server.listen({host:'127.0.0.1',port:0}, () => {
                if (this.disposed) { this.server.close(); this.finish(); resolve(); return; }
                this.start(executable,uri,folder,this.server.address().port,nonce); resolve();
            });
        });
    }
    start(executable,uri,folder,port,nonce) {
        this.child = spawn(executable, [uri.fsPath, '--project', folder, '--embed',String(port),nonce],
            { cwd: folder, windowsHide: true, detached: process.platform !== 'win32', stdio: ['pipe', 'pipe', 'pipe'] });
        const child = this.child;
        prefabProcesses.add(this);
        child.once('close', () => { this.ended = true; this.transport?.destroy(); prefabProcesses.delete(this); clearTimeout(this.deadline); this.finish(); });
        child.stdout.on('data', bytes => output?.appendLine(bytes.toString()));
        child.stderr.on('data', bytes => output?.appendLine(bytes.toString()));
        child.stdin.on('error', () => {});
        child.on('error', error => this.fail(`Couldn't start the native prefab editor: ${error.message}`));
        child.on('close', code => { if (!this.disposed) this.fail(`Native prefab editor stopped (${code ?? 'signal'}). See Tide output.`); });
        this.panel.webview.onDidReceiveMessage(message => {
            if (!this.disposed && message?.type === 'presented' && this.received && !this.presented) {
                this.presented = true; output?.info(`Prefab editor displayed: ${uri.fsPath} (PID ${child.pid})`); return;
            }
            if (this.disposed || this.busy || message?.type !== 'frame' || !Array.isArray(message.events) || message.events.length > 200) return;
            if (message.events.some(event => typeof event !== 'string' || Buffer.byteLength(event) > 4000 || /[\r\n\0]/.test(event) || !/^(mouse|button|key|text|scroll) /.test(event))) {
                this.fail('Invalid prefab editor input.'); return;
            }
            this.busy = true;
            child.stdin.write(message.events.map(event => `INPUT ${event}\n`).join('') + 'FRAME 1\n');
        });
        this.panel.webview.html = editorPage('const ide=acquireVsCodeApi();const send=message=>ide.postMessage(message);', nonce);
    }
    read(bytes) {
        if (this.disposed) return;
        if (this.buffer.length + bytes.length > 32*1024*1024+256) { this.fail('Prefab frame exceeded the transport limit.'); return; }
        this.buffer = Buffer.concat([this.buffer, bytes]);
        if (!this.size) {
            const end = this.buffer.indexOf(10);
            if (end < 0) { if (this.buffer.length > 256) this.fail('Invalid prefab frame header.'); return; }
            const header = /^TIDEFRAME 1 (\d+) ([01]) (\d+) (\d+) (\d+)$/.exec(this.buffer.subarray(0,end).toString('ascii'));
            if (!header || !this.busy || +header[1] <= 0 || +header[1] > 32*1024*1024) { this.fail('Incompatible prefab editor protocol. Rebuild or upgrade Tide and its extension together.'); return; }
            this.size = +header[1]; this.dirty = header[2] === '1';
            this.buffer = this.buffer.subarray(end+1);
        }
        if (this.buffer.length < this.size) return;
        if (this.buffer.length !== this.size) { this.fail('Unexpected data after the prefab frame.'); return; }
        const png = this.buffer.toString('base64');
        this.buffer = Buffer.alloc(0); this.size = 0; this.busy = false;
        this.received = true;
        this.panel.title = path.basename(this.uri.fsPath) + (this.dirty ? ' *' : '');
        this.panel.webview.postMessage({ type: 'frame', png });
    }
    fail(message) {
        if (this.disposed) return;
        this.panel.webview.postMessage({ type: 'error', message: `Tide: ${message}` });
        this.dispose();
    }
    dispose() {
        if (this.disposed) return this.finished;
        this.disposed = true;
        prefabPanels.delete(this.uri.fsPath);
        try { this.server.close(); } catch {}
        for (const socket of this.connections) if (socket !== this.transport) socket.destroy();
        if (!this.child) { this.finish(); return this.finished; }
        // EOF lets the native editor save recovery and join any compiler worker.
        this.child.stdin.end();
        if (!this.ended) this.deadline = setTimeout(() => {
            if (this.ended) return;
            if (process.platform === 'win32') {
                const killer = spawn('taskkill', ['/pid', String(this.child.pid), '/T', '/F'], { windowsHide: true });
                killer.on('error', error => output?.appendLine(`Prefab cleanup failed: ${error.message}`));
                killer.on('close', code => { if (code) output?.appendLine(`Prefab cleanup failed (${code}).`); });
            } else { try { process.kill(-this.child.pid, 'SIGKILL'); } catch (error) { output?.appendLine(`Prefab cleanup failed: ${error.message}`); } }
        }, 2000);
        return this.finished;
    }
}

async function editPrefab(resource) {
    if (deactivating) return;
    if (!vscode.workspace.isTrusted) { vscode.window.showErrorMessage('Tide: trust this workspace before opening the prefab editor.'); return; }
    const document = vscode.window.activeTextEditor?.document, uri = resource ?? document?.uri;
    if (uri?.scheme !== 'file' || path.extname(uri.fsPath).toLowerCase() !== '.tprefab') { vscode.window.showErrorMessage('Tide: select or open a .tprefab file.'); return; }
    const previous = prefabPanels.get(uri.fsPath);
    if (previous && !previous.disposed) { previous.panel.reveal(); return; }
    const found = gameFolder(uri.fsPath);
    if (found.error) { vscode.window.showErrorMessage(`Tide: ${found.error}`); return; }
    const configured = settingProgram('path', 'tide', vscode.Uri.file(found.folder));
    const tide = configured ? configured.file : installedProgram('tide');
    if (!tide) { await missing(configured ? `Tide: ${configured.error}` : "Tide: couldn't find tide. Install it, or set tide.path.", 'tide.path'); return; }
    const executable = path.join(path.dirname(tide),'tide-editor'+suffix);
    if (!isFile(executable)) { vscode.window.showErrorMessage('Tide: tide-editor is missing beside tide. Install a desktop Tide package.'); return; }
    if (document?.uri.fsPath === uri.fsPath && document.isDirty && !await document.save()) return;
    if (deactivating) return;
    const opened = prefabPanels.get(uri.fsPath); // Another command may have awaited the same Save.
    if (opened && !opened.disposed) { opened.panel.reveal(); return; }
    const editor = new PrefabPanel(executable,uri,found.folder);
    prefabPanels.set(uri.fsPath, editor); await editor.ready;
}

function prefabAction(action) {
    for (const editor of prefabPanels.values()) if (editor.panel.active && !editor.disposed) editor.panel.webview.postMessage({type:'action',action});
}

function activate(context) {
    output = vscode.window.createOutputChannel('Tide', { log: true });
    trace = traceChannel();
    context.subscriptions.push(
        output,
        trace,
        vscode.commands.registerCommand('tide.restartServer', restart),
        vscode.commands.registerCommand('tide.run', () => run('desktop')),
        vscode.commands.registerCommand('tide.runWeb', () => run('web')),
        vscode.commands.registerCommand('tide.runAndroid', () => run('android')),
        vscode.commands.registerCommand('tide.openDevTools', openDevTools),
        vscode.commands.registerCommand('tide.openPrefab', openPrefab),
        vscode.commands.registerCommand('tide.editPrefab', editPrefab),
        vscode.commands.registerCommand('tide.prefabSave', () => prefabAction('save')),
        vscode.commands.registerCommand('tide.prefabUndo', () => prefabAction('undo')),
        vscode.commands.registerCommand('tide.prefabRedo', () => prefabAction('redo')),
        vscode.workspace.onDidChangeConfiguration(e => {
            if (e.affectsConfiguration('tide.server.path')) restart();
        }),
        // Trusted now: the workspace's own server and settings can run.
        vscode.workspace.onDidGrantWorkspaceTrust(restart),
        { dispose: () => Object.values(runs).forEach(r => r?.dispose()) },
        { dispose: () => [...prefabProcesses].forEach(editor => editor.dispose()) },
    );
    serially(start);
}

async function deactivate() {
    deactivating = true;
    await Promise.allSettled(Object.values(runs).map(r => r?.dispose()));
    await Promise.allSettled([...prefabProcesses].map(editor => editor.dispose()));
    await serially(stop);
}

module.exports = { activate, deactivate };
