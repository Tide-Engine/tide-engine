// Opt-in real VS Code host test. Uses a private profile and extensions.
// node test/run-host.mjs <Code executable> <built tidels> <Node executable>
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { build } from 'esbuild';

const [code, server, node = process.execPath, tide] = process.argv.slice(2).map(p => path.resolve(p));
assert.ok(code && server, 'Pass the Code executable and this checkout\'s built tidels');
const source = fileURLToPath(new URL('..', import.meta.url));
const scratch = fs.mkdtempSync(path.join(os.tmpdir(), 'tide-vscode-host-'));
const extension = path.join(scratch, 'extension'), workspace = path.join(scratch, 'game');
fs.mkdirSync(extension);
fs.mkdirSync(path.join(workspace, '.vscode'), { recursive: true });
for (const file of ['package.json', 'icons', 'icon.png']) fs.cpSync(path.join(source, file), path.join(extension, file), { recursive: true });
for (const file of ['syntaxes', 'language-configuration.json']) fs.cpSync(path.join(source, '../tide-syntax', file), path.join(extension, file), { recursive: true });
fs.copyFileSync(path.join(source, 'test/host-suite.cjs'), path.join(extension, 'host-suite.cjs'));
await build({ entryPoints: [path.join(source, 'src/extension.js')], outfile: path.join(extension, 'out/extension.js'), bundle: true, minify: true, platform: 'node', format: 'cjs', external: ['vscode'] });
fs.writeFileSync(path.join(workspace, '.vscode/settings.json'), JSON.stringify({ 'tide.server.path': server, 'tide.path': node, 'workbench.startupEditor': 'none', 'security.workspace.trust.enabled': false }));
fs.writeFileSync(path.join(workspace, 'main.tide'), 'component Body { float radius; }\nscene Main { }\nsystem Move(mut Body body)\n{\n    body.radius = 1;\n}\n');
// The real terminal launches Node as a stand-in CLI. Its child and grandchild
// stay alive until the adapter kills the tree; the page proves embedded JS ran.
fs.copyFileSync(path.join(source, 'test/host-process.cjs'), path.join(workspace, 'run'));
const env = { ...process.env, TIDE_HIDDEN: '1', TIDE_HOST_WORKSPACE: workspace, TIDE_HOST_RUNNER_PID: String(process.pid),
    TIDE_HOST_TIDE: tide ?? '', TIDE_HOST_LOGS: path.join(scratch,'profile/logs') };
delete env.ELECTRON_RUN_AS_NODE;
const child = spawn(code, [workspace, '--new-window', '--mute-audio', '--disable-workspace-trust', '--disable-extensions', '--skip-welcome', '--skip-release-notes',
    `--user-data-dir=${path.join(scratch, 'profile')}`, `--extensions-dir=${path.join(scratch, 'installed')}`,
    `--extensionDevelopmentPath=${extension}`, `--extensionTestsPath=${path.join(extension, 'host-suite.cjs')}`], { env, windowsHide: true, stdio: 'inherit' });
const timer = setTimeout(() => { child.kill(); }, 180_000);
try {
    const code = await new Promise((resolve, reject) => { child.on('error', reject); child.on('close', resolve); });
    if (fs.existsSync(path.join(workspace, 'host.log'))) console.log(fs.readFileSync(path.join(workspace, 'host.log'), 'utf8'));
    assert.equal(code, 0, `VS Code host failed; isolated artifacts: ${scratch}`);
    assert.ok(fs.existsSync(path.join(workspace, 'passed')), 'The host must actually finish the suite');
    const logs = path.join(scratch, 'profile/logs');
    for (const date of fs.readdirSync(logs)) {
        const log = path.join(logs, date, 'window1/exthost/tide-engine.tide/Tide.log');
        if (fs.existsSync(log)) assert.doesNotMatch(fs.readFileSync(log, 'utf8'), /\[error\]/, 'The real language client must not log delivery/lifecycle errors');
    }
    for (const file of fs.readdirSync(workspace).filter(f => /^pids-\d+\.json$/.test(f))) {
        for (const pid of JSON.parse(fs.readFileSync(path.join(workspace, file)))) {
            let running = false;
            try { process.kill(pid, 0); running = true; } catch {}
            assert.equal(running, false, `Deactivation left test process ${pid} alive`);
        }
    }
    if (tide) for (const pid of JSON.parse(fs.readFileSync(path.join(workspace,'prefab-pids.json')))) {
        let running=false;try { process.kill(pid,0);running=true; } catch {}
        assert.equal(running,false,'Deactivation must close the actual native prefab backend');
    }
    console.log(`Real VS Code host passed. Isolated artifacts: ${scratch}`);
} finally {
    clearTimeout(timer);
    // Failed fixtures exit when this runner exits; never signal an old numeric
    // PID which the OS might already have reused for an unrelated process.
}
