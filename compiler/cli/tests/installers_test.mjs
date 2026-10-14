// The installers against a made-up site's releases and an installed tide.
// Stop a fresh install before unpacking: no tools, profiles or user PATH change.
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { chmodSync, existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { availableParallelism, tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import test, { before } from 'node:test';

const source = resolve(dirname(fileURLToPath(import.meta.url)), '../../..');
const payload = 'installer regression package';
const sum = createHash('sha256').update(payload).digest('hex');
const shells = process.platform === 'win32' ? ['powershell'] : ['sh'];
if (process.env.INSTALL_TEST_SH) shells.push('sh');
const cases = [
    { name: 'a fresh install takes the stable release' },
    { name: 'a fresh install takes the nightly channel when asked', channel: 'nightly' },
    { name: 'checksum failure refuses installation', damaged: true },
    { name: 'installed updater receives checked local files on its channel', installed: true, nightly: true },
    { name: 'explicit channel is passed to the installed updater', installed: true, nightly: true, channel: 'stable' },
    { name: 'installed stable updater stays on stable', installed: true },
    { name: 'installed updater failure is reported, and the override restored', installed: true, failUpgrade: true },
    { name: 'checksum failure never invokes an installed updater', installed: true, damaged: true },
    { name: 'optional Windows prefab Open With registration preserves defaults', installed: true, associate: true },
];

const powershell = String.raw`
$ErrorActionPreference = 'Stop'
$testClock = [Diagnostics.Stopwatch]::StartNew()
$testProcess = [Diagnostics.Process]::GetCurrentProcess()
function Phase([string]$name) {
    [Console]::Error.WriteLine(('Installer fixture: wall={0:F3}s cpu={1:F3}s {2}' -f
        $testClock.Elapsed.TotalSeconds, $testProcess.TotalProcessorTime.TotalSeconds, $name))
}
Phase 'started'
$testTemp = [IO.Path]::GetTempPath()
$testMarker = [IO.Path]::Combine($testTemp, 'tide-installer-fixture.txt')
# Windows may expand an inherited 8.3 TEMP path. Verify directory identity
# through its private marker, rather than comparing two path spellings.
if (-not [IO.File]::Exists($testMarker) -or [IO.File]::ReadAllText($testMarker) -ne $env:TEST_WORK) {
    Phase "scratch path actual=$testTemp; fixture=$env:TEST_WORK"
    throw "Installer scratch path $testTemp escaped the fixture $env:TEST_WORK."
}
# Windows PowerShell prepends vendor module paths at startup, even when the
# child environment lists only its stock modules. Isolate discovery again
# before the first command can trigger module analysis.
$env:PSModulePath = [IO.Path]::Combine($PSHOME, 'Modules')
Phase 'import Utility begin'
Import-Module ([IO.Path]::Combine($env:PSModulePath, 'Microsoft.PowerShell.Utility', 'Microsoft.PowerShell.Utility.psd1'))
Phase 'import Utility end; import Management begin'
Import-Module ([IO.Path]::Combine($env:PSModulePath, 'Microsoft.PowerShell.Management', 'Microsoft.PowerShell.Management.psd1'))
Phase 'import Management end'
$null = Get-Command Get-FileHash
function Record($entry) {
    Phase "record $($entry.op) begin"
    $entry | ConvertTo-Json -Compress -Depth 8 | Add-Content $env:TEST_LOG
    Phase "record $($entry.op) end"
}
function Invoke-RestMethod {
    param($Uri, $Headers)
    Phase 'release list mock begin'
    Record @{ op = 'request'; url = $Uri; auth = $Headers.Authorization }
    $list = Get-Content $env:TEST_LIST -Raw | ConvertFrom-Json
    Phase 'release list mock end'
    $list
}
function Invoke-WebRequest {
    param($Uri, $Headers, $OutFile, [switch]$UseBasicParsing)
    Phase "download mock begin: $([IO.Path]::GetFileName($OutFile))"
    Record @{ op = 'request'; url = $Uri; auth = $Headers.Authorization; output = $OutFile }
    if ($OutFile.EndsWith('SHA256SUMS')) { [IO.File]::WriteAllText($OutFile, $env:TEST_SUMS) }
    else { [IO.File]::WriteAllText($OutFile, $env:TEST_PAYLOAD) }
    Phase 'download mock end'
}
# Keep the checksum real, while distinguishing a hash stall from mock I/O or
# shell startup when a hosted runner reaches the unchanged process guard.
function Get-FileHash {
    param($Path, $Algorithm)
    Phase 'checksum begin'
    $hash = Microsoft.PowerShell.Utility\Get-FileHash @PSBoundParameters
    Phase 'checksum end'
    $hash
}
function Expand-Archive {
    param($Path, $DestinationPath, [switch]$Force)
    Phase 'unpack mock begin'
    Record @{ op = 'unpack' }
    throw 'TEST_UNPACK'
}
foreach ($mock in @('Invoke-RestMethod', 'Invoke-WebRequest', 'Expand-Archive')) {
    if ((Get-Command $mock).CommandType -ne 'Function') { throw "The $mock mock was not selected." }
}
Phase 'mock command resolution verified'
# Intercept registry operations before running the actual installer. No test
# changes this user's file associations, even when registration is requested.
function New-Item {
    param($Path, $ItemType, [switch]$Force)
    if ($Path.StartsWith('HKCU:')) { Record @{ op = 'registry-key'; path = $Path }; return }
    Microsoft.PowerShell.Management\New-Item @PSBoundParameters
}
function Set-Item {
    param($LiteralPath, $Value)
    if ($LiteralPath.StartsWith('HKCU:')) { Record @{ op = 'registry-default'; path = $LiteralPath; value = $Value }; return }
    Microsoft.PowerShell.Management\Set-Item @PSBoundParameters
}
function New-ItemProperty {
    param($LiteralPath, $Name, $Value, $PropertyType, [switch]$Force)
    if (-not $LiteralPath.StartsWith('HKCU:')) { throw 'Unexpected registry fixture path' }
    Record @{ op = 'registry-value'; path = $LiteralPath; name = $Name; value = $Value }
}
$testTide = Join-Path $env:LOCALAPPDATA 'Tide/bin/tide.exe'
Set-Item -LiteralPath "Function:global:$testTide" -Value {
    if ($args[0] -eq 'upgrade') {
        $manifestText = [IO.File]::ReadAllText(([Uri]$env:TIDE_RELEASES_URL).LocalPath)
        if (-not $manifestText.TrimStart().StartsWith('[')) { throw 'The local manifest must be an array.' }
        $manifest = $manifestText | ConvertFrom-Json
        foreach ($asset in $manifest.assets) {
            if (-not $asset.browser_download_url.StartsWith('file:')) { throw 'The updater received a remote URL.' }
            if (-not (Microsoft.PowerShell.Management\Test-Path -LiteralPath ([Uri]$asset.browser_download_url).LocalPath)) { throw 'The updater received a missing asset.' }
        }
        Record @{ op = 'upgrade'; args = @($args); manifestText = $manifestText }
        if ($env:TEST_FAIL_UPGRADE) { $global:LASTEXITCODE = 17; return }
    } else { Record @{ op = 'editors' } }
    $global:LASTEXITCODE = 0
}
Phase 'installer begin'
try { . $env:TEST_INSTALLER }
catch {
    Phase 'installer exception caught'
    if ($_.Exception.Message -eq 'TEST_UNPACK') { 'Stopped before unpacking.' }
    elseif ($_.Exception.Message.Contains('checksum does not match')) { 'Checksum refused.' }
    elseif ($_.Exception.Message -eq 'tide could not finish the upgrade.') { 'Upgrade refused.' }
    else { throw }
}
if ($env:TIDE_RELEASES_URL -ne 'file:///original-override.json') { throw 'The installer changed TIDE_RELEASES_URL.' }
Phase 'finished'
`;

const shell = String.raw`
python3() { command "$TEST_PYTHON" "$@"; }
record() {
    python3 - "$@" >> "$TEST_LOG" <<'PY'
import json, sys
print(json.dumps(dict(zip(sys.argv[1::2], sys.argv[2::2]))))
PY
}
uname() { case "$1" in -s) echo Linux ;; -m) echo x86_64 ;; esac; }
mktemp() { printf '%s\n' "$TEST_WORK"; }
curl() {
    url= output=
    while [ "$#" -gt 0 ]; do
        case "$1" in
            -o) output="$2"; shift ;;
            -H) echo 'The installer sent a header of its own.' >&2; return 1 ;;
            --proto-redir) shift ;;
            -q|-fsSL) ;;
            *) url="$1" ;;
        esac
        shift
    done
    record op request url "$url"
    case "$url" in
        */releases.json) cp "$TEST_LIST" "$output" ;;
        */SHA256SUMS) printf '%s' "$TEST_SUMS" > "$output" ;;
        *.tar.gz) printf '%s' "$TEST_PAYLOAD" > "$output" ;;
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

// Windows PowerShell's first start on a machine loads and checks the
// assemblies its module imports, its JSON cmdlets (System.Web.Extensions)
// and its file and hash cmdlets use, which a fresh hosted runner, with 2 CPUs
// and other tests beside it, did in 20 seconds and more, its process all but
// idle: waiting on the disk and the machine's scanner, not running. Later
// processes take a second. That's the runner's, not the installer's, and the
// cases' 30-second guard measured it in whichever case came first (Build runs
// 38623397915, 38637433618). So one process does all of it first, with a guard
// of its own for a real hang, and the cases' guard measures the installer.
if (shells.includes('powershell')) before(() => {
    const work = mkdtempSync(join(tmpdir(), 'tide-installers-warm-'));
    try {
        const warm = String.raw`
$ErrorActionPreference = 'Stop'
$env:PSModulePath = [IO.Path]::Combine($PSHOME, 'Modules')
Import-Module ([IO.Path]::Combine($env:PSModulePath, 'Microsoft.PowerShell.Utility', 'Microsoft.PowerShell.Utility.psd1'))
Import-Module ([IO.Path]::Combine($env:PSModulePath, 'Microsoft.PowerShell.Management', 'Microsoft.PowerShell.Management.psd1'))
$file = Join-Path $env:TEST_WORK 'warm.json'
@{ warm = @(1, 'two') } | ConvertTo-Json -Compress -Depth 8 | Add-Content $file
$null = Get-Content $file -Raw | ConvertFrom-Json
$null = Get-FileHash $file -Algorithm SHA256
'warm'
`;
        const script = join(work, 'warm.ps1');
        writeFileSync(script, warm);
        const env = { ...process.env, TEST_WORK: work, PSModuleAnalysisCachePath: join(work, 'ModuleAnalysisCache') };
        for (const key of Object.keys(env)) if (key.toUpperCase() === 'PSMODULEPATH') delete env[key];
        env.PSModulePath = join(process.env.SystemRoot, 'System32', 'WindowsPowerShell', 'v1.0', 'Modules');
        const started = Date.now();
        const result = spawnSync('powershell.exe', ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', script],
            { env, encoding: 'utf8', windowsHide: true, timeout: 300_000 });
        const said = `${result.stdout}\n${result.stderr}`;
        assert.ok(!result.error, `PowerShell's first start: ${result.error}\n${said}`);
        assert.equal(result.status, 0, `PowerShell's first start failed:\n${said}`);
        console.log(`Windows PowerShell's first start took ${((Date.now() - started) / 1000).toFixed(1)} s`);
    } finally { rmSync(work, { recursive: true, force: true }); }
});

for (const runtime of shells) for (const c of cases) {
    if (c.associate && runtime !== 'powershell') continue;
    test(`${runtime}: ${c.name}`, () => {
        const work = mkdtempSync(join(tmpdir(), 'tide-installers-test-'));
        let passed = false;
        let result;
        try {
            const packageName = runtime === 'powershell' ? 'tide-windows-x64.zip' : 'tide-linux-x64.tar.gz';
            const site = 'https://tide-engine.dev/releases';
            const release = (tag, prerelease = false) => ({
                tag_name: tag, prerelease, draft: false, assets: [
                    { name: packageName, size: payload.length, browser_download_url: `${site}/${tag}/${packageName}` },
                    { name: 'SHA256SUMS', size: 1, browser_download_url: `${site}/${tag}/SHA256SUMS` },
                ],
            });
            writeFileSync(join(work, 'list.json'), JSON.stringify([release('v1.3.0-nightly.1', true), release('v1.2.0'), release('v1.2.1')]));
            mkdirSync(join(work, 'temporary'));
            writeFileSync(join(work, 'temporary', 'tide-installer-fixture.txt'), join(work, 'temporary'));
            const installation = runtime === 'powershell' ? join(work, 'local', 'Tide') : join(work, 'user', '.tide');
            if (c.installed) {
                mkdirSync(join(installation, 'bin'), { recursive: true });
                writeFileSync(join(installation, 'channel'), c.nightly ? 'nightly' : 'stable');
                if (runtime === 'sh') {
                    writeFileSync(join(installation, 'bin', 'tide'), oldTide);
                    chmodSync(join(installation, 'bin', 'tide'), 0o755);
                } else {
                    writeFileSync(join(installation, 'bin', 'tide.exe'), '');
                    writeFileSync(join(installation, 'bin', 'tide-editor.exe'), '');
                }
            }
            const env = { ...process.env, GH_TOKEN: 'a-token-nobody-is-sent', GITHUB_TOKEN: 'another-token-nobody-is-sent',
                TIDE_CHANNEL: c.channel || '', TIDE_RELEASES_URL: 'file:///original-override.json',
                TIDE_ASSOCIATE_PREFABS: c.associate ? '1' : '',
                LOCALAPPDATA: join(work, 'local'), HOME: join(work, 'user'),
                TEST_LOG: join(work, 'requests.jsonl'), TEST_WORK: join(work, 'temporary'),
                TEST_INSTALLER: join(source, `install.${runtime === 'powershell' ? 'ps1' : 'sh'}`),
                TEST_LIST: join(work, 'list.json'), TEST_INSTALLED: c.installed ? 'yes' : '',
                TEST_FAIL_UPGRADE: c.failUpgrade ? 'yes' : '',
                TEST_PAYLOAD: payload, TEST_SUMS: `${c.damaged ? '0'.repeat(64) : sum}  ${packageName}\n`,
                TEST_PYTHON: process.env.INSTALL_TEST_PYTHON || 'python3', TIDE_HIDDEN: '1' };
            if (runtime === 'sh') for (const key of ['HOME', 'TEST_LOG', 'TEST_WORK', 'TEST_INSTALLER', 'TEST_LIST']) env[key] = env[key].replaceAll('\\', '/');
            // Use Windows PowerShell's stock modules, independent of PowerShell
            // 7 or vendor modules installed on the host. Every case starts with
            // an empty analysis cache instead of depending on a warmed user one.
            if (runtime === 'powershell') {
                for (const key of Object.keys(env)) {
                    if (['PSMODULEPATH', 'PSMODULEANALYSISCACHEPATH'].includes(key.toUpperCase())) delete env[key];
                }
                env.PSModulePath = join(process.env.SystemRoot, 'System32', 'WindowsPowerShell', 'v1.0', 'Modules');
                env.PSModuleAnalysisCachePath = join(work, 'ModuleAnalysisCache');
                // Keep real installer scratch files with the fixture too, so
                // a killed child leaves no untracked downloads in user TEMP.
                env.TEMP = env.TMP = join(work, 'temporary');
            }
            const harness = join(work, runtime === 'powershell' ? 'case.ps1' : 'case.sh');
            writeFileSync(harness, runtime === 'powershell' ? powershell : shell);
            const executable = runtime === 'powershell' ? 'powershell.exe' : process.env.INSTALL_TEST_SH || 'sh';
            const args = runtime === 'powershell' ? ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', harness] : [harness];
            result = spawnSync(executable, args, { env, encoding: 'utf8', windowsHide: true, timeout: 30_000 });
            const said = `${result.stdout}\n${result.stderr}`;
            assert.ok(!result.error, `${result.error}\n${said}`);
            const expectedStatus = runtime === 'sh' && c.failUpgrade ? 17 : runtime === 'sh' && c.damaged ? 1 : runtime === 'sh' && !c.installed ? 87 : 0;
            assert.equal(result.status, expectedStatus, said);
            assert.ok(!said.includes('nobody-is-sent'), 'A token appeared in installer output.');
            const entries = existsSync(env.TEST_LOG) ? readFileSync(env.TEST_LOG, 'utf8').trim().split(/\r?\n/).map((line) => JSON.parse(line)) : [];
            const requests = entries.filter((entry) => entry.op === 'request');
            for (const request of requests) if (request.output) assert.ok(!existsSync(request.output), 'The temporary download was not removed.');
            // The list, the package and its checksums, from the site, with no credentials.
            const wanted = c.channel === 'nightly' || (c.nightly && !c.channel) ? 'v1.3.0-nightly.1' : 'v1.2.1';
            assert.deepEqual(requests.map((request) => request.url).sort(),
                [`${site}.json`, `${site}/${wanted}/${packageName}`, `${site}/${wanted}/SHA256SUMS`].sort());
            for (const request of requests) assert.equal(request.auth || '', '');
            const upgrades = entries.filter((entry) => entry.op === 'upgrade');
            assert.equal(upgrades.length, c.installed && !c.damaged ? 1 : 0);
            if (upgrades.length) {
                assert.deepEqual(upgrades[0].args, c.channel ? ['upgrade', `--${c.channel}`] : ['upgrade']);
                const manifest = runtime === 'powershell' ? JSON.parse(upgrades[0].manifestText)[0] : upgrades[0].manifest[0];
                assert.equal(manifest.tag_name, wanted);
                assert.equal(manifest.assets.length, 2);
            }
            if (runtime === 'powershell' && c.failUpgrade) assert.ok(said.includes('Upgrade refused.'), said);
            assert.equal(entries.filter((entry) => entry.op === 'editors').length, c.installed && !c.damaged && !c.failUpgrade ? 1 : 0);
            const registry = entries.filter(entry => entry.op.startsWith('registry-'));
            assert.equal(registry.length, runtime === 'powershell' && c.associate ? 6 : 0);
            if (registry.length) {
                const defaults = registry.filter(entry => entry.op === 'registry-default');
                assert.ok(defaults.every(entry => entry.path.includes('Tide.Prefab')), 'never replace an extension default or UserChoice');
                assert.equal(defaults.find(entry => entry.path.endsWith('command')).value,
                    `"${join(installation, 'bin', 'tide-editor.exe')}" "%1"`);
                const value = registry.find(entry => entry.op === 'registry-value');
                assert.ok(value.path.endsWith('.tprefab\\OpenWithProgids')); assert.equal(value.name, 'Tide.Prefab');
            }
            assert.equal(entries.filter((entry) => entry.op === 'unpack').length, !c.installed && !c.damaged ? 1 : 0);
            if (runtime === 'sh') assert.ok(!existsSync(env.TEST_WORK), 'The temporary downloads were not removed.');
            passed = true;
        } catch (error) {
            const log = join(work, 'requests.jsonl');
            const entries = existsSync(log) ? readFileSync(log, 'utf8') : '(no mock calls recorded)';
            // The fixture uses dummy tokens only. Preserve its private files
            // and surface partial calls even when the child died mid-record.
            if (result) {
                writeFileSync(join(work, 'stdout.log'), result.stdout || '');
                writeFileSync(join(work, 'stderr.log'), result.stderr || '');
            }
            const detail = `\nFixture preserved at ${work}\nCPUs available: ${availableParallelism()}; CTEST_PARALLEL_LEVEL=${process.env.CTEST_PARALLEL_LEVEL || '(unset)'}\nPartial mock calls:\n${entries}`;
            // Node's assertion reporter uses the stack captured at construction.
            error.stack += detail;
            error.message += detail;
            throw error;
        } finally { if (passed) rmSync(work, { recursive: true, force: true }); }
    });
}
