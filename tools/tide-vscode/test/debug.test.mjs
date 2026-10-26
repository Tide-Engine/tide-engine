// Debugging in VS Code: breakpoints in .tide files, the `tide` debug type and
// what its configurations become. No editor, debugger or game is launched.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { test } from 'node:test';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const debug = require('../src/debug.js');
const manifest = JSON.parse(fs.readFileSync(new URL('../package.json', import.meta.url), 'utf8'));

test('Breakpoints can be set in .tide files, and the tide debug type is contributed with its two configurations', () => {
    assert.deepEqual(manifest.contributes.breakpoints, [{ language: 'tide' }]);
    const tide = manifest.contributes.debuggers.find(d => d.type === 'tide');
    assert.ok(tide, 'the tide debug type');
    assert.deepEqual(tide.languages, ['tide']);
    assert.deepEqual(tide.initialConfigurations.map(c => [c.request, c.name, c.target]),
        [['attach', 'Attach to Unity (Tide)', undefined], ['launch', 'Debug Tide game', undefined],
         ['attach', 'Attach to Tide game (tide run)', 'game']]);
    assert.deepEqual(tide.configurationSnippets.map(s => [s.body.request, s.body.target]),
        [['attach', undefined], ['launch', undefined], ['attach', 'game']]);
    assert.ok(manifest.activationEvents.includes('onDebugResolve:tide'));
    assert.ok(manifest.activationEvents.includes('onDebugDynamicConfigurations:tide'));
});

test('The native debugger is the C/C++ extension on Windows, and an LLDB extension elsewhere', () => {
    const have = (...ids) => id => ids.includes(id);
    assert.equal(debug.nativeDebugger('win32', have('ms-vscode.cpptools')), 'cppvsdbg');
    assert.equal(debug.nativeDebugger('win32', have('vadimcn.vscode-lldb')), null);
    assert.equal(debug.nativeDebugger('darwin', have('llvm-vs-code-extensions.lldb-dap', 'vadimcn.vscode-lldb')), 'lldb-dap');
    assert.equal(debug.nativeDebugger('linux', have('vadimcn.vscode-lldb')), 'lldb');
    assert.equal(debug.nativeDebugger('darwin', have('ms-vscode.cpptools')), null);
    assert.match(debug.missingDebugger('win32'), /ms-vscode\.cpptools/);
    assert.match(debug.missingDebugger('darwin'), /lldb-dap.*vscode-lldb/);
});

test('Unity editors are found in the process list, with the projects their command lines open', () => {
    const tasklist = '"Unity.exe","4120","Console","1","1,024,000 K"\r\n"Unity Hub.exe","77","Console","1","9 K"\r\n'
        + '"Unity.exe","5000","Console","1","9 K"\r\n';
    const lines = new Map([[4120, '"C:\\Program Files\\Unity\\Editor\\Unity.exe" -projectpath "C:\\Projects\\My Game" -useHub']]);
    assert.deepEqual(debug.unityProcesses('win32', tasklist, lines),
        [{ pid: 4120, project: 'C:\\Projects\\My Game' }, { pid: 5000, project: null }]);
    const ps = '  310 /Applications/Unity/Hub/Editor/6000.6.0f1/Unity.app/Contents/MacOS/Unity -projectPath /Users/a/Sample\n'
        + '  311 /Applications/Unity Hub.app/Contents/MacOS/Unity Hub\n  312 /usr/bin/python3 Unity\n';
    assert.deepEqual(debug.unityProcesses('darwin', ps), [{ pid: 310, project: '/Users/a/Sample' }]);
    assert.equal(debug.projectOf('Unity -batchmode -projectPath=x'), null);
});

test("The game tide run plays is the host in its .tide's run folder, which loads its game libraries", () => {
    const hosts = [
        { pid: 1, executable: 'C:\\games\\Bounce\\.tide\\debug\\run\\4242\\Bounce.exe' },
        { pid: 2, executable: 'C:\\games\\Bounce\\.tide\\debug\\Bounce.exe' },
        { pid: 3, executable: 'C:\\games\\Bounce2\\.tide\\debug\\run\\1\\Bounce2.exe' },
        { pid: 4, executable: 'C:\\games\\Bounce\\build\\Bounce.exe' },
    ];
    assert.deepEqual(debug.tideRunHosts('win32', hosts, 'c:\\Games\\Bounce\\').map(h => h.pid), [1]);
    assert.deepEqual(debug.tideRunHosts('linux', [{ pid: 9, executable: '/g/.tide/debug/run/7/g' }], '/g').map(h => h.pid), [9]);
    assert.deepEqual(debug.tideRunHosts('linux', [{ pid: 9, executable: '/g/.tide/debug/run/7/g' }], '/G'), []);
});

test('A Unity project is the folder above a file with ProjectSettings/ProjectVersion.txt', () => {
    const p = path.win32;
    const exists = f => f === 'C:\\Projects\\Game\\ProjectSettings\\ProjectVersion.txt';
    assert.equal(debug.unityProject('C:\\Projects\\Game\\Assets\\Scripts', exists, p), 'C:\\Projects\\Game');
    assert.equal(debug.unityProject('C:\\Elsewhere', exists, p), null);
});

test("Attaching to Unity attaches to the editor's process with the game's symbols and views beside it", () => {
    const p = path.win32;
    const exists = f => f.endsWith('game.natvis') || f.endsWith('game_lldb.py');
    const library = 'C:\\Projects\\Game\\Library\\Tide';
    assert.deepEqual(debug.attachConfig('cppvsdbg', { name: 'Attach to Unity (Tide)' }, 4120, library, p, exists), {
        type: 'cppvsdbg', request: 'attach', name: 'Attach to Unity (Tide)', processId: '4120',
        symbolSearchPath: library, visualizerFile: 'C:\\Projects\\Game\\Library\\Tide\\game.natvis',
    });
    const q = path.posix;
    assert.deepEqual(debug.attachConfig('lldb-dap', { name: 'Attach to Unity (Tide)' }, 310, '/Users/a/Sample/Library/Tide', q, exists), {
        type: 'lldb-dap', request: 'attach', name: 'Attach to Unity (Tide)', pid: 310,
        initCommands: ['command script import "/Users/a/Sample/Library/Tide/game_lldb.py"'],
    });
    // A build that hasn't written its views yet still attaches
    assert.deepEqual(debug.attachConfig('lldb', { name: 'a' }, 9, '/p/Library/Tide', q, () => false),
        { type: 'lldb', request: 'attach', name: 'a', pid: 9 });
});

test('Launching runs the debug build with its views, its arguments, in the game folder', () => {
    const p = path.win32;
    const exists = () => true;
    assert.deepEqual(debug.launchConfig('cppvsdbg', { name: 'Debug Tide game', args: ['--host'] }, 'C:\\g\\.tide\\debugger\\g.exe', 'C:\\g', p, exists), {
        type: 'cppvsdbg', request: 'launch', name: 'Debug Tide game', program: 'C:\\g\\.tide\\debugger\\g.exe', args: ['--host'],
        cwd: 'C:\\g', console: 'integratedTerminal', visualizerFile: 'C:\\g\\.tide\\debugger\\g.natvis',
    });
    const q = path.posix;
    assert.deepEqual(debug.launchConfig('lldb', { name: 'n' }, '/g/.tide/debugger/g', '/g', q, exists), {
        type: 'lldb', request: 'launch', name: 'n', program: '/g/.tide/debugger/g', args: [], cwd: '/g',
        initCommands: ['command script import "/g/.tide/debugger/g_lldb.py"'],
    });
});
