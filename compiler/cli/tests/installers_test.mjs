// The installers against fake GitHub requests and an older installed tide.
// Stop a fresh install before unpacking: no tools, profiles or user PATH change.
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { chmodSync, existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import test from 'node:test';

const source = resolve(dirname(fileURLToPath(import.meta.url)), '../../..');
const payload = 'installer regression package';
const sum = createHash('sha256').update(payload).digest('hex');
const shells = process.platform === 'win32' ? ['powershell'] : ['sh'];
if (process.env.INSTALL_TEST_SH) shells.push('sh');
const cases = [
    { name: 'public releases without credentials' },
    { name: 'GH_TOKEN takes precedence', ghToken: 'fixture-first', githubToken: 'fixture-second', login: 'fixture-login', auth: 'fixture-first' },
    { name: 'GITHUB_TOKEN takes precedence over gh login', githubToken: 'fixture-second', login: 'fixture-login', auth: 'fixture-second' },
    { name: 'gh login supplies authentication', login: 'fixture-login', auth: 'fixture-login' },
    { name: 'token control characters are refused', ghToken: 'fixture\nheader', invalid: true },
    { name: 'asset URL on another host never receives credentials', ghToken: 'fixture-first', auth: 'fixture-first', remoteAssets: true },
    { name: 'checksum failure refuses installation', ghToken: 'fixture-first', auth: 'fixture-first', damaged: true },
    { name: 'old installed updater receives checked local files on its channel', ghToken: 'fixture-first', auth: 'fixture-first', installed: true, nightly: true },
    { name: 'explicit channel is passed to the old updater', ghToken: 'fixture-first', auth: 'fixture-first', installed: true, nightly: true, channel: 'stable' },
    { name: 'public installed updater keeps its existing path', installed: true },
    { name: 'public installed updater failure is reported', installed: true, failUpgrade: true },
    { name: 'authenticated installed updater failure restores the override', ghToken: 'fixture-first', auth: 'fixture-first', installed: true, failUpgrade: true },
    { name: 'checksum failure never invokes an installed updater', ghToken: 'fixture-first', auth: 'fixture-first', installed: true, damaged: true },
];

const powershell = String.raw`
$ErrorActionPreference = 'Stop'
$null = Get-Command Get-FileHash
function Record($entry) { $entry | ConvertTo-Json -Compress -Depth 8 | Add-Content $env:TEST_LOG }
function gh {
    Record @{ op = 'gh'; args = @($args) }
    if ($env:TEST_LOGIN) { $global:LASTEXITCODE = 0; $env:TEST_LOGIN }
    else { $global:LASTEXITCODE = 1; Write-Error 'Not logged in.' }
}
function Invoke-RestMethod {
    param($Uri, $Headers)
    Record @{ op = 'request'; url = $Uri; auth = $Headers.Authorization; accept = $Headers.Accept }
    if ($Uri.EndsWith('/latest')) { Get-Content $env:TEST_LATEST -Raw | ConvertFrom-Json }
    else { Get-Content $env:TEST_LIST -Raw | ConvertFrom-Json }
}
function Invoke-WebRequest {
    param($Uri, $Headers, $OutFile, [switch]$UseBasicParsing)
    Record @{ op = 'request'; url = $Uri; auth = $Headers.Authorization; accept = $Headers.Accept; output = $OutFile }
    if ($OutFile.EndsWith('SHA256SUMS')) { [IO.File]::WriteAllText($OutFile, $env:TEST_SUMS) }
    else { [IO.File]::WriteAllText($OutFile, $env:TEST_PAYLOAD) }
}
function Expand-Archive {
    param($Path, $DestinationPath, [switch]$Force)
    Record @{ op = 'unpack' }
    throw 'TEST_UNPACK'
}
$testTide = Join-Path $env:LOCALAPPDATA 'Tide/bin/tide.exe'
Set-Item -LiteralPath "Function:$testTide" -Value {
    if ($args[0] -eq 'upgrade') {
        $manifest = $null
        if ($env:TEST_AUTH) {
            $manifestText = [IO.File]::ReadAllText(([Uri]$env:TIDE_RELEASES_URL).LocalPath)
            if (-not $manifestText.TrimStart().StartsWith('[')) { throw 'The local manifest must be an array.' }
            $manifest = $manifestText | ConvertFrom-Json
            foreach ($asset in $manifest.assets) {
                if (-not $asset.browser_download_url.StartsWith('file:')) { throw 'The old updater received a remote URL.' }
                if (-not (Microsoft.PowerShell.Management\Test-Path -LiteralPath ([Uri]$asset.browser_download_url).LocalPath)) { throw 'The old updater received a missing asset.' }
            }
        }
        Record @{ op = 'upgrade'; args = @($args); manifestText = $manifestText }
        if ($env:TEST_FAIL_UPGRADE) { $global:LASTEXITCODE = 17; return }
    } else { Record @{ op = 'editors' } }
    $global:LASTEXITCODE = 0
}
try { . $env:TEST_INSTALLER }
catch {
    if ($_.Exception.Message -eq 'TEST_UNPACK') { 'Stopped before unpacking.' }
    elseif ($_.Exception.Message.Contains('checksum does not match')) { 'Checksum refused.' }
    elseif ($_.Exception.Message -eq 'tide could not finish the upgrade.') { 'Upgrade refused.' }
    else { throw }
}
if ($env:TIDE_RELEASES_URL -ne 'file:///original-override.json') { throw 'The installer changed TIDE_RELEASES_URL.' }
`;

const shell = String.raw`
python3() { "$TEST_PYTHON" "$@"; }
record() {
    python3 - "$@" >> "$TEST_LOG" <<'PY'
import json, sys
print(json.dumps(dict(zip(sys.argv[1::2], sys.argv[2::2]))))
PY
}
gh() {
    record op gh
    [ -n "$TEST_LOGIN" ] || { echo 'Not logged in.' >&2; return 1; }
    printf '%s\n' "$TEST_LOGIN"
}
uname() { case "$1" in -s) echo Linux ;; -m) echo x86_64 ;; esac; }
mktemp() { printf '%s\n' "$TEST_WORK"; }
curl() {
    url= auth= accept= output=
    while [ "$#" -gt 0 ]; do
        case "$1" in
            -o) output="$2"; shift ;;
            -H)
                case "$2" in
                    @*) auth="$(cat "$(printf '%s' "$2" | cut -c2-)" | cut -c16-)" ;;
                    'Accept: '*) accept="$(printf '%s' "$2" | cut -c9-)" ;;
                    *) echo 'Credentials were put on the command line.' >&2; return 1 ;;
                esac
                shift ;;
            --proto-redir) shift ;;
            -q|-fsSL) ;;
            *) url="$1" ;;
        esac
        shift
    done
    record op request url "$url" auth "$auth" accept "$accept"
    case "$url" in
        */releases/latest) cp "$TEST_LATEST" "$output" ;;
        *'/releases?per_page=100') cp "$TEST_LIST" "$output" ;;
        */assets/102|*/SHA256SUMS) printf '%s' "$TEST_SUMS" > "$output" ;;
        */assets/101|*.tar.gz) printf '%s' "$TEST_PAYLOAD" > "$output" ;;
        *) echo 'Unexpected request.' >&2; return 1 ;;
    esac
}
tar() { record op unpack; echo 'Stopped before unpacking.'; exit 87; }
. "$TEST_INSTALLER"
`;

const oldTide = String.raw`#!/bin/sh
if [ "$1" = upgrade ]; then
    "$TEST_PYTHON" - "$@" >> "$TEST_LOG" <<'PY'
import json, os, pathlib, sys, urllib.parse, urllib.request
manifest = None
if os.environ.get('TEST_AUTH'):
    url = urllib.parse.urlparse(os.environ['TIDE_RELEASES_URL'])
    manifest = json.loads(pathlib.Path(urllib.request.url2pathname(url.path)).read_text())
    for asset in manifest[0]['assets']:
        local = urllib.parse.urlparse(asset['browser_download_url'])
        assert local.scheme == 'file'
        assert pathlib.Path(urllib.request.url2pathname(local.path)).is_file()
print(json.dumps({'op': 'upgrade', 'args': sys.argv[1:], 'manifest': manifest}))
PY
    [ -z "$TEST_FAIL_UPGRADE" ] || exit 17
else
    printf '{"op":"editors"}\n' >> "$TEST_LOG"
fi
`;

for (const runtime of shells) for (const c of cases) {
    test(`${runtime}: ${c.name}`, () => {
        const work = mkdtempSync(join(tmpdir(), 'tide-installers-test-'));
        try {
            const packageName = runtime === 'powershell' ? 'tide-windows-x64.zip' : 'tide-linux-x64.tar.gz';
            const release = (tag, prerelease = false) => ({
                tag_name: tag, prerelease, draft: false, assets: [
                    { name: packageName, url: `https://${c.remoteAssets ? 'api.github.com.other.example' : 'api.github.com'}/repos/fixture/private/releases/assets/101`, browser_download_url: `https://github.com/fixture/public/releases/download/${tag}/${packageName}` },
                    { name: 'SHA256SUMS', url: `https://${c.remoteAssets ? 'api.github.com.other.example' : 'api.github.com'}/repos/fixture/private/releases/assets/102`, browser_download_url: `https://github.com/fixture/public/releases/download/${tag}/SHA256SUMS` },
                ],
            });
            writeFileSync(join(work, 'list.json'), JSON.stringify([release('v1.2.0'), release('v1.3.0-nightly.1', true)]));
            writeFileSync(join(work, 'latest.json'), JSON.stringify(release('v1.2.1')));
            mkdirSync(join(work, 'temporary'));
            const installation = runtime === 'powershell' ? join(work, 'local', 'Tide') : join(work, 'user', '.tide');
            if (c.installed) {
                mkdirSync(join(installation, 'bin'), { recursive: true });
                writeFileSync(join(installation, 'channel'), c.nightly ? 'nightly' : 'stable');
                if (runtime === 'sh') {
                    writeFileSync(join(installation, 'bin', 'tide'), oldTide);
                    chmodSync(join(installation, 'bin', 'tide'), 0o755);
                } else writeFileSync(join(installation, 'bin', 'tide.exe'), '');
            }
            const env = { ...process.env, GH_TOKEN: c.ghToken || '', GITHUB_TOKEN: c.githubToken || '',
                TIDE_CHANNEL: c.channel || '', TIDE_RELEASES_URL: 'file:///original-override.json',
                LOCALAPPDATA: join(work, 'local'), HOME: join(work, 'user'),
                TEST_LOG: join(work, 'requests.jsonl'), TEST_WORK: join(work, 'temporary'),
                TEST_INSTALLER: join(source, `install.${runtime === 'powershell' ? 'ps1' : 'sh'}`),
                TEST_LIST: join(work, 'list.json'), TEST_LATEST: join(work, 'latest.json'),
                TEST_LOGIN: c.login || '', TEST_AUTH: c.auth || '', TEST_INSTALLED: c.installed ? 'yes' : '',
                TEST_FAIL_UPGRADE: c.failUpgrade ? 'yes' : '',
                TEST_PAYLOAD: payload, TEST_SUMS: `${c.damaged ? '0'.repeat(64) : sum}  ${packageName}\n`,
                TEST_PYTHON: process.env.INSTALL_TEST_PYTHON || 'python3', TIDE_HIDDEN: '1' };
            if (runtime === 'sh') for (const key of ['HOME', 'TEST_LOG', 'TEST_WORK', 'TEST_INSTALLER', 'TEST_LIST', 'TEST_LATEST']) env[key] = env[key].replaceAll('\\', '/');
            // Windows PowerShell must discover its own modules when the test
            // runner was started by PowerShell 7.
            if (runtime === 'powershell') for (const key of Object.keys(env)) if (key.toUpperCase() === 'PSMODULEPATH') delete env[key];
            const harness = join(work, runtime === 'powershell' ? 'case.ps1' : 'case.sh');
            writeFileSync(harness, runtime === 'powershell' ? powershell : shell);
            const executable = runtime === 'powershell' ? 'powershell.exe' : process.env.INSTALL_TEST_SH || 'sh';
            const args = runtime === 'powershell' ? ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', harness] : [harness];
            const result = spawnSync(executable, args, { env, encoding: 'utf8', windowsHide: true, timeout: 30_000 });
            assert.ifError(result.error);
            const said = `${result.stdout}\n${result.stderr}`;
            const expectedStatus = c.invalid ? 1 : runtime === 'sh' && c.failUpgrade ? 17 : runtime === 'sh' && c.damaged ? 1 : runtime === 'sh' && !c.installed ? 87 : 0;
            assert.equal(result.status, expectedStatus, said);
            for (const credential of [c.ghToken, c.githubToken, c.login].filter(Boolean)) assert.ok(!said.includes(credential), 'Credentials appeared in installer output.');
            const entries = existsSync(env.TEST_LOG) ? readFileSync(env.TEST_LOG, 'utf8').trim().split(/\r?\n/).map((line) => JSON.parse(line)) : [];
            assert.equal(entries.filter((entry) => entry.op === 'gh').length, c.ghToken || c.githubToken ? 0 : 1);
            const requests = entries.filter((entry) => entry.op === 'request');
            for (const request of requests) if (request.output) assert.ok(!existsSync(request.output), 'The temporary download was not removed.');
            const delegates = c.installed && !c.auth;
            assert.equal(requests.length, delegates || c.invalid ? 0 : c.installed && c.nightly && !c.channel ? 3 : 4);
            for (const request of requests) {
                const authenticated = c.auth && request.url.startsWith('https://api.github.com/');
                assert.equal(request.auth || '', authenticated ? `Bearer ${c.auth}` : '');
                if (request.url.includes('/assets/')) assert.equal(request.accept || '', authenticated ? 'application/octet-stream' : '');
                else if (c.auth) assert.equal(request.accept, 'application/vnd.github+json');
            }
            if (!delegates && !c.invalid) assert.equal(requests.filter((request) => request.url.includes('/assets/')).length, c.auth ? 2 : 0);
            const upgrades = entries.filter((entry) => entry.op === 'upgrade');
            assert.equal(upgrades.length, c.installed && !c.damaged ? 1 : 0);
            if (upgrades.length) {
                assert.deepEqual(upgrades[0].args, c.channel ? ['upgrade', `--${c.channel}`] : ['upgrade']);
                if (c.auth) {
                    const manifest = runtime === 'powershell' ? JSON.parse(upgrades[0].manifestText)[0] : upgrades[0].manifest[0];
                    assert.equal(manifest.tag_name, c.nightly && !c.channel ? 'v1.3.0-nightly.1' : 'v1.2.1');
                    assert.equal(manifest.assets.length, 2);
                }
            }
            if (runtime === 'powershell' && c.failUpgrade) assert.ok(said.includes('Upgrade refused.'), said);
            assert.equal(entries.filter((entry) => entry.op === 'editors').length, c.installed && !c.damaged && !c.failUpgrade ? 1 : 0);
            assert.equal(entries.filter((entry) => entry.op === 'unpack').length, !c.installed && !c.damaged && !c.invalid ? 1 : 0);
            if (runtime === 'sh' && !delegates && !c.invalid) assert.ok(!existsSync(env.TEST_WORK), 'The temporary credentials were not removed.');
        } finally { rmSync(work, { recursive: true, force: true }); }
    });
}
