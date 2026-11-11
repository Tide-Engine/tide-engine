import assert from 'node:assert/strict';
import { execFileSync, spawnSync } from 'node:child_process';
import { chmodSync, mkdtempSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import test from 'node:test';

const script = fileURLToPath(new URL('./ubuntu-mirror.sh', import.meta.url));
const bash = process.env.TIDE_TEST_BASH || (process.platform === 'win32'
    ? resolve(execFileSync('git', ['--exec-path'], { encoding: 'utf8' }).trim(), '../../../bin/bash.exe') : 'bash');
const hosted = 'http://azure.archive.ubuntu.com/ubuntu/\tpriority:1\nhttps://archive.ubuntu.com/ubuntu/\tpriority:2\n' +
    'https://security.ubuntu.com/ubuntu/\tpriority:3\n';

// The script with a list of its own, and a resolver that answers for the hosts named
function run(t, resolving, list = hosted) {
    const root = mkdtempSync(join(tmpdir(), 'tide-ubuntu-mirror-'));
    t.after(() => rmSync(root, { recursive: true, force: true }));
    mkdirSync(join(root, 'bin'));
    const stub = (name, text) => {
        const path = join(root, 'bin', name);
        writeFileSync(path, '#!/usr/bin/env bash\nset -euo pipefail\n' + text);
        chmodSync(path, 0o755);
    };
    stub('getent', '[[ "$1" == ahostsv4 && $# == 2 ]]\n[[ " $RESOLVING " == *" $2 "* ]]\n');
    stub('sudo', '[[ "$1" == tee && $# == 2 ]]\ncat > "$2"\n');
    if (list !== null) writeFileSync(join(root, 'mirrors'), list);
    const result = spawnSync(bash, ['-c', `
fixture=$1
if command -v cygpath > /dev/null; then fixture=$(cygpath -u "$fixture"); fi
export PATH="$fixture/bin:$PATH" TIDE_APT_MIRRORS="$fixture/mirrors"
bash "$2"
`, 'fixture', root, script], { encoding: 'utf8', env: { ...process.env, TIDE_HIDDEN: '1', RESOLVING: resolving.join(' ') } });
    return { result, list: () => readFileSync(join(root, 'mirrors'), 'utf8') };
}

test('a runner that resolves any of its archive hosts keeps its list', t => {
    for (const host of ['azure.archive.ubuntu.com', 'archive.ubuntu.com', 'security.ubuntu.com']) {
        const d = run(t, [host, 'mirrors.edge.kernel.org']);
        assert.equal(d.result.status, 0, d.result.stderr);
        assert.equal(d.list(), hosted);
        assert.equal(d.result.stdout, '');
    }
});

test('a runner that resolves none of them fetches from the mirror, and says so', t => {
    const d = run(t, ['mirrors.edge.kernel.org']);
    assert.equal(d.result.status, 0, d.result.stderr);
    assert.equal(d.list(), 'https://mirrors.edge.kernel.org/ubuntu/\tpriority:1\n');
    assert.match(d.result.stdout, /^::warning::/);
});

test('with no mirror to reach either, it fails and leaves the list', t => {
    const d = run(t, []);
    assert.notEqual(d.result.status, 0);
    assert.equal(d.list(), hosted);
});

test('a machine with no such list is left alone', t => {
    const d = run(t, [], null);
    assert.equal(d.result.status, 0, d.result.stderr);
    assert.equal(d.result.stdout, '');
});
