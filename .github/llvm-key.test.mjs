import assert from 'node:assert/strict';
import { execFileSync, spawnSync } from 'node:child_process';
import { chmodSync, existsSync, mkdtempSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import test from 'node:test';

const script = fileURLToPath(new URL('./llvm-key.sh', import.meta.url));
const bash = process.env.TIDE_TEST_BASH || (process.platform === 'win32'
    ? resolve(execFileSync('git', ['--exec-path'], { encoding: 'utf8' }).trim(), '../../../bin/bash.exe') : 'bash');

function fixture(t, extra = {}) {
    const root = mkdtempSync(join(tmpdir(), 'tide-llvm-key-'));
    t.after(() => rmSync(root, { recursive: true, force: true }));
    mkdirSync(join(root, 'bin'));
    const stub = (name, text) => {
        const path = join(root, 'bin', name);
        writeFileSync(path, '#!/usr/bin/env bash\nset -euo pipefail\n' + text);
        chmodSync(path, 0o755);
    };
    stub('getent', '[[ "$*" == "ahostsv4 apt.llvm.org" ]]\nexit "${DNS_STATUS:-0}"\n');
    stub('curl', `printf '%s\\n' "$@" >> "$RUNNER_TEMP/curl-args"
output=; address=false
while (( $# )); do
  case "$1" in
    -o) output=$2; shift;;
    --write-out) address=true; shift;;
  esac
  shift
done
printf '%s' "complete signing key" > "$output"
if [[ "\${CURL_STATUS:-0}" != 0 ]]; then exit "$CURL_STATUS"; fi
if $address; then printf '%s' "\${PEER_ADDRESS-192.0.2.42}"; fi
`);
    stub('sudo', `case "$1" in
  tee)
    [[ "$2" == -a && "$3" == /etc/hosts && $# == 3 ]]
    cat > "$RUNNER_TEMP/hosts"
    ;;
  install)
    [[ "$2" == -m && "$3" == 644 && "$5" == /etc/apt/trusted.gpg.d/apt.llvm.org.asc && $# == 5 ]]
    cp "$4" "$RUNNER_TEMP/installed-key"
    ;;
  *) exit 99;;
esac
`);
    if (process.platform === 'win32') stub('python3', 'exec python "$@"\n');
    const result = spawnSync(bash, ['-c', `
fixture=$1
if command -v cygpath > /dev/null; then fixture=$(cygpath -u "$fixture"); fi
export PATH="$fixture/bin:$PATH" RUNNER_TEMP="$fixture"
bash "$2"
`, 'fixture', root, script], { encoding: 'utf8', env: { ...process.env, TIDE_HIDDEN: '1', ...extra } });
    const args = readFileSync(join(root, 'curl-args'), 'utf8').trim().split('\n');
    assert(args.includes('-fsSL') && args.includes('-4'));
    assert(!args.some(arg => /insecure|cacert|capath/.test(arg)), 'TLS trust must remain unchanged');
    return { root, result, args, has: name => existsSync(join(root, name)), read: name => readFileSync(join(root, name), 'utf8') };
}

test('working runner DNS installs the key without changing host resolution', t => {
    const f = fixture(t);
    assert.equal(f.result.status, 0, f.result.stderr);
    assert.equal(f.read('installed-key'), 'complete signing key');
    assert(!f.has('hosts'));
    assert(!f.args.includes('--connect-to'));
    assert.equal(f.args.at(-1), 'https://apt.llvm.org/llvm-snapshot.gpg.key');
});

test('failed runner DNS maps only LLVM to the authenticated download peer', t => {
    const f = fixture(t, { DNS_STATUS: '2' });
    assert.equal(f.result.status, 0, f.result.stderr);
    assert.equal(f.read('installed-key'), 'complete signing key');
    assert.equal(f.read('hosts'), '192.0.2.42 apt.llvm.org\n');
    const at = f.args.indexOf('--connect-to');
    assert(at >= 0);
    assert.equal(f.args[at + 1], 'apt.llvm.org:443:dualstack.l2.shared.us-eu.fastly.net:443');
    assert.equal(f.args[f.args.indexOf('--max-redirs') + 1], '0');
    assert.equal(f.args[f.args.indexOf('--noproxy') + 1], 'apt.llvm.org');
});

test('failed normal downloads never install partial key bytes', t => {
    const f = fixture(t, { CURL_STATUS: '6' });
    assert.equal(f.result.status, 6, f.result.stderr);
    assert(!f.has('installed-key') && !f.has('hosts'));
});

test('failed fallback TLS changes neither the key nor host mapping', t => {
    const f = fixture(t, { DNS_STATUS: '2', CURL_STATUS: '60' });
    assert.equal(f.result.status, 60, f.result.stderr);
    assert(!f.has('installed-key') && !f.has('hosts'));
});

test('malformed peer addresses cannot reach the privileged host install', t => {
    const f = fixture(t, { DNS_STATUS: '2', PEER_ADDRESS: '192.0.2.42\n127.0.0.1 other.invalid' });
    assert.notEqual(f.result.status, 0);
    assert(!f.has('installed-key') && !f.has('hosts'));
});

test('empty or non-IPv4 peer addresses cannot publish a host mapping', t => {
    for (const address of ['', '::1']) {
        const f = fixture(t, { DNS_STATUS: '2', PEER_ADDRESS: address });
        assert.notEqual(f.result.status, 0);
        assert(!f.has('installed-key') && !f.has('hosts'));
    }
});
