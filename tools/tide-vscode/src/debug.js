// Debugging a Tide game in VS Code: breakpoints on .tide lines (the manifest's
// `breakpoints`), and the `tide` debug type, whose configurations become a
// native debugger's. A game's C maps each line back to its .tide line, and a
// debug build carries how its values look: its natvis (in its .pdb on Windows,
// and beside it) and its LLDB views (game_lldb.py, imported here).
//
// - Windows: Microsoft's C/C++ extension (cppvsdbg).
// - macOS and Linux: LLVM's LLDB DAP extension (lldb-dap), else CodeLLDB.
//
// "Attach to Unity (Tide)" attaches to the Unity editor running the project,
// whose game is Library/Tide's game-<n> library; "Debug Tide game" builds the
// game for debugging (tide build) and starts it under the debugger; "Attach
// to Tide game (tide run)" attaches to the game Tide: Run plays, which goes on
// reloading as files are saved.

const fs = require('fs');
const path = require('path');

const DEBUGGERS = {
    cppvsdbg: 'ms-vscode.cpptools',
    'lldb-dap': 'llvm-vs-code-extensions.lldb-dap',
    lldb: 'vadimcn.vscode-lldb',
};

// The native debugger for this platform, of the extensions installed: its type, or null.
function nativeDebugger(platform, installed) {
    if (platform === 'win32') return installed(DEBUGGERS.cppvsdbg) ? 'cppvsdbg' : null;
    if (installed(DEBUGGERS['lldb-dap'])) return 'lldb-dap';
    if (installed(DEBUGGERS.lldb)) return 'lldb';
    return null;
}

function missingDebugger(platform) {
    return platform === 'win32'
        ? "Tide: debugging needs Microsoft's C/C++ extension (ms-vscode.cpptools), whose debugger reads the game's .pdb."
        : "Tide: debugging needs an LLDB extension: LLVM's LLDB DAP (llvm-vs-code-extensions.lldb-dap) or CodeLLDB (vadimcn.vscode-lldb).";
}

// Unity editors in a process list: Windows' `tasklist /fo csv /nh`, or `ps -axo pid=,args=` elsewhere.
// Each is { pid, project } where its command line says the project (-projectPath).
function unityProcesses(platform, listing, commandLines = new Map()) {
    const found = [];
    for (const line of listing.split(/\r?\n/)) {
        if (platform === 'win32') {
            const cells = line.match(/"([^"]*)"/g)?.map(c => c.slice(1, -1));
            if (!cells || cells.length < 2 || cells[0].toLowerCase() !== 'unity.exe') continue;
            const pid = Number(cells[1]);
            if (Number.isInteger(pid)) found.push({ pid, project: projectOf(commandLines.get(pid) || '') });
        } else {
            const m = line.match(/^\s*(\d+)\s+(.*)$/);
            if (!m) continue;
            const program = m[2].split(/\s+-/)[0];
            const name = path.posix.basename(program);
            if (name !== 'Unity') continue; // Unity Hub's is "Unity Hub"
            found.push({ pid: Number(m[1]), project: projectOf(m[2]) });
        }
    }
    return found;
}

// The hosts `tide run` started for the game in `folder`, from [{ pid, executable }]: programs in its
// .tide/<configuration>/run/<n>/, which load its game-<n> libraries and reload them as files are saved.
function tideRunHosts(platform, processes, folder) {
    const norm = p => {
        const s = p.replace(/\\/g, '/').replace(/\/+$/, '');
        return platform === 'win32' ? s.toLowerCase() : s;
    };
    const prefix = norm(folder) + '/.tide/';
    return processes.filter(p => {
        const exe = norm(p.executable || '');
        return exe.startsWith(prefix) && exe.slice(prefix.length).includes('/run/');
    });
}

// The project a Unity command line opens.
function projectOf(commandLine) {
    const m = commandLine.match(/-projectpath\s+(?:"([^"]+)"|(\S+))/i);
    return m ? (m[1] || m[2]) : null;
}

// The Unity project a file or folder is in: the folder with ProjectSettings/ProjectVersion.txt.
function unityProject(start, exists = f => fs.existsSync(f), paths = path) {
    for (let at = start; at; ) {
        if (exists(paths.join(at, 'ProjectSettings', 'ProjectVersion.txt'))) return at;
        const up = paths.dirname(at);
        if (up === at) return null;
        at = up;
    }
    return null;
}

function samePath(platform, a, b) {
    const norm = p => (platform === 'win32' ? path.win32.normalize(p).toLowerCase() : path.posix.normalize(p)).replace(/[\\/]+$/, '');
    return !!a && !!b && norm(a) === norm(b);
}

// What the native debugger needs to show the game's values: LLDB imports the
// LLDB views beside the game; cppvsdbg reads the natvis (also in each .pdb).
function views(type, folder, stem, paths = path, exists = f => fs.existsSync(f)) {
    const natvis = paths.join(folder, `${stem}.natvis`);
    const lldb = paths.join(folder, `${stem}_lldb.py`);
    if (type === 'cppvsdbg') return exists(natvis) ? { visualizerFile: natvis } : {};
    return exists(lldb) ? { initCommands: [`command script import "${lldb}"`] } : {};
}

// The attach configuration for a Unity editor's process.
function attachConfig(type, config, pid, library, paths = path, exists = f => fs.existsSync(f)) {
    const shown = library ? views(type, library, 'game', paths, exists) : {};
    if (type === 'cppvsdbg') {
        return { ...shown, type, request: 'attach', name: config.name, processId: String(pid),
            ...(library ? { symbolSearchPath: library } : {}) };
    }
    return { ...shown, type, request: 'attach', name: config.name, pid };
}

// The launch configuration for a program `tide build` made.
function launchConfig(type, config, program, game, paths = path, exists = f => fs.existsSync(f)) {
    const stem = paths.basename(program).replace(/\.exe$/i, '');
    const shown = views(type, paths.dirname(program), stem, paths, exists);
    const base = { ...shown, type, request: 'launch', name: config.name, program, args: config.args || [], cwd: game };
    if (type === 'cppvsdbg') return { ...base, console: 'integratedTerminal' };
    return base;
}

function register(vscode, helpers) {
    const { output, findTide, gameOf } = helpers;
    const platform = process.platform;
    const installed = id => !!vscode.extensions.getExtension(id);
    const exec = (file, args) => new Promise(resolve => {
        require('child_process').execFile(file, args, { windowsHide: true, maxBuffer: 1 << 24 }, (error, stdout) => resolve(error ? '' : stdout));
    });

    async function unityEditors() {
        if (platform !== 'win32') return unityProcesses(platform, await exec('ps', ['-axo', 'pid=,args=']));
        const listing = await exec('tasklist', ['/fo', 'csv', '/nh', '/fi', 'imagename eq Unity.exe']);
        // Each editor's project, from its command line, when there's more than one to tell apart
        const lines = new Map();
        const json = await exec('powershell', ['-NoProfile', '-Command',
            "Get-CimInstance Win32_Process -Filter \"Name='Unity.exe'\" | Select-Object ProcessId,CommandLine | ConvertTo-Json"]);
        try {
            const parsed = JSON.parse(json || '[]');
            for (const p of Array.isArray(parsed) ? parsed : [parsed]) lines.set(p.ProcessId, p.CommandLine || '');
        } catch { /* Without them each editor is shown by its process ID */ }
        return unityProcesses(platform, listing, lines);
    }

    // Every process's ID and executable
    async function processes() {
        if (platform !== 'win32') {
            return (await exec('ps', ['-axo', 'pid=,args='])).split('\n').map(l => l.match(/^\s*(\d+)\s+(\S+)/))
                .filter(Boolean).map(m => ({ pid: Number(m[1]), executable: m[2] }));
        }
        const json = await exec('powershell', ['-NoProfile', '-Command',
            'Get-CimInstance Win32_Process | Where-Object ExecutablePath | Select-Object ProcessId,ExecutablePath | ConvertTo-Json']);
        try {
            const parsed = JSON.parse(json || '[]');
            return (Array.isArray(parsed) ? parsed : [parsed]).map(p => ({ pid: p.ProcessId, executable: p.ExecutablePath }));
        } catch {
            return [];
        }
    }

    // The game `tide run` plays: Tide: Run starts it, and saving a file reloads it as it runs, under the debugger too
    async function resolveGameAttach(config, type) {
        const game = config.game || gameOf();
        if (!game) {
            vscode.window.showErrorMessage('Tide: open a .tide file of the game to debug, or set "game" in the configuration.');
            return undefined;
        }
        const hosts = tideRunHosts(platform, await processes(), game);
        if (!hosts.length) {
            vscode.window.showErrorMessage(`Tide: ${path.basename(game)} isn't running. Start it with Tide: Run, then attach.`);
            return undefined;
        }
        const host = hosts[hosts.length - 1];
        return attachConfig(type, config, host.pid, path.dirname(host.executable));
    }

    async function resolveAttach(config, type) {
        if (config.target === 'game') return resolveGameAttach(config, type);
        const folder = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
        const file = vscode.window.activeTextEditor?.document.uri.fsPath;
        const project = config.project || unityProject(file ? path.dirname(file) : folder) || unityProject(folder);
        let editors = await unityEditors();
        if (config.processId) editors = [{ pid: Number(config.processId), project }];
        const mine = editors.filter(e => samePath(platform, e.project, project));
        if (mine.length) editors = mine;
        if (!editors.length) {
            vscode.window.showErrorMessage('Tide: no Unity editor is running. Open the project in Unity, then attach.');
            return undefined;
        }
        let chosen = editors[0];
        if (editors.length > 1) {
            const pick = await vscode.window.showQuickPick(editors.map(e => ({ label: e.project ? path.basename(e.project) : 'Unity',
                description: `process ${e.pid}`, detail: e.project || undefined, editor: e })), { placeHolder: 'The Unity editor to attach to' });
            if (!pick) return undefined;
            chosen = pick.editor;
        }
        const where = chosen.project || project;
        const library = where ? path.join(where, 'Library', 'Tide') : null;
        return attachConfig(type, config, chosen.pid, library);
    }

    async function resolveLaunch(config, type) {
        const game = config.game || gameOf();
        if (!game) {
            vscode.window.showErrorMessage('Tide: open a .tide file of the game to debug, or set "game" in the configuration.');
            return undefined;
        }
        const tide = findTide(game);
        if (!tide) {
            vscode.window.showErrorMessage("Tide: couldn't find tide. Install it, or set tide.path.");
            return undefined;
        }
        const name = path.basename(game).replace(/[^A-Za-z0-9_-]/g, '') || 'game';
        const program = path.join(game, '.tide', 'debugger', name + (platform === 'win32' ? '.exe' : ''));
        const built = await vscode.window.withProgress({ location: vscode.ProgressLocation.Notification, title: 'Tide: building the game to debug' },
            () => new Promise(resolve => {
                const child = require('child_process').spawn(tide, ['build', game, '-o', program], { cwd: game, windowsHide: true });
                child.stdout.on('data', d => output?.append(String(d)));
                child.stderr.on('data', d => output?.append(String(d)));
                child.on('error', () => resolve(false));
                child.on('close', code => resolve(code === 0));
            }));
        if (!built) {
            output?.show(true);
            vscode.window.showErrorMessage('Tide: the game didn\'t build: its errors are in the Tide output.');
            return undefined;
        }
        return launchConfig(type, config, program, game);
    }

    const provider = {
        provideDebugConfigurations() {
            return [
                { type: 'tide', request: 'attach', name: 'Attach to Unity (Tide)' },
                { type: 'tide', request: 'launch', name: 'Debug Tide game', game: '${workspaceFolder}' },
                { type: 'tide', request: 'attach', name: 'Attach to Tide game (tide run)', target: 'game', game: '${workspaceFolder}' },
            ];
        },
        // Before VS Code substitutes variables, so that the native debugger's own provider resolves what this
        // becomes (VS Code resolves a configuration again when its type changes): ${workspaceFolder} is done here.
        async resolveDebugConfiguration(folder, config) {
            if (!config.type && !config.request) config = { type: 'tide', request: 'launch', name: 'Debug Tide game' };
            // Debugging runs the game's code, and LLDB its views' Python, from the workspace
            if (!vscode.workspace.isTrusted) {
                vscode.window.showErrorMessage('Tide: debugging runs the game\'s code, so it only runs once you trust this workspace.');
                return undefined;
            }
            const root = folder?.uri.fsPath ?? vscode.workspace.workspaceFolders?.[0]?.uri.fsPath ?? '';
            for (const key of ['game', 'project']) {
                if (typeof config[key] === 'string') config = { ...config, [key]: config[key].split('${workspaceFolder}').join(root) };
            }
            const type = nativeDebugger(platform, installed);
            if (!type) {
                vscode.window.showErrorMessage(missingDebugger(platform));
                return undefined;
            }
            return config.request === 'attach' ? resolveAttach(config, type) : resolveLaunch(config, type);
        },
    };
    return vscode.debug.registerDebugConfigurationProvider('tide', provider);
}

module.exports = { register, nativeDebugger, missingDebugger, unityProcesses, tideRunHosts, projectOf, unityProject, attachConfig, launchConfig, views };
