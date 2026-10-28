param(
    [Parameter(Mandatory=$true)][string]$OutputDir,
    [int]$Rounds = 5,
    [int]$Warmup = 200,
    [int]$Ticks = 400,
    [int]$QuietSeconds = 15,
    [string]$BuildDir = 'build/release',
    [string]$BevyExe = 'build/bevy/release/tide-bevy-bench.exe',
    # Optional adjacent before/current/Bevy triples; the frozen directory contains bin/versus_*.exe.
    [string]$BeforeBuildDir = '',
    [string]$BeforeRevision = '',
    [switch]$FilterSweep,
    [string[]]$OnlyCases = @()
)
$env:TIDE_HIDDEN = '1'
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../..").Path
Set-Location $root
if ($Rounds -lt 1 -or $Warmup -lt 1 -or $Ticks -lt 1 -or $QuietSeconds -lt 1) { throw 'Counts must be positive' }
if ([string]::IsNullOrWhiteSpace($BeforeBuildDir) -ne [string]::IsNullOrWhiteSpace($BeforeRevision)) {
    throw 'Supply both BeforeBuildDir and BeforeRevision, or neither'
}
$hasBefore = ![string]::IsNullOrWhiteSpace($BeforeBuildDir)
if (Test-Path -LiteralPath $OutputDir) { throw "Evidence directory already exists: $OutputDir" }
$bevy = (Resolve-Path -LiteralPath $BevyExe).Path
$tideBins = @{}
$beforeBins = @{}
foreach ($workload in @('particles','wide','churn')) {
    $tideBins[$workload] = (Resolve-Path -LiteralPath "$BuildDir/bin/versus_$workload.exe").Path
    if ($hasBefore) { $beforeBins[$workload] = (Resolve-Path -LiteralPath "$BeforeBuildDir/bin/versus_$workload.exe").Path }
}
$cases = @(
    @{id='particles-10k-1'; workload='particles'; count=10000; threads=1},
    @{id='particles-100k-1'; workload='particles'; count=100000; threads=1},
    @{id='particles-1m-1'; workload='particles'; count=1000000; threads=1},
    @{id='particles-1m-4'; workload='particles'; count=1000000; threads=4},
    @{id='wide-1m-1'; workload='wide'; count=1000000; threads=1},
    @{id='wide-1m-4'; workload='wide'; count=1000000; threads=4},
    @{id='filtered-1m-1'; workload='wide'; count=1000000; threads=1; tagged=1; frozen=50},
    @{id='filtered-1m-4'; workload='wide'; count=1000000; threads=4; tagged=1; frozen=50},
    @{id='churn-2k-1'; workload='churn'; rate=2000; threads=1},
    @{id='churn-2k-4'; workload='churn'; rate=2000; threads=4}
)
if ($FilterSweep) {
    foreach ($frozen in @(1,10,90,95,99)) {
        $cases += @{id="filtered-frozen$frozen-1m-1"; workload='wide'; count=1000000; threads=1; tagged=1; frozen=$frozen}
    }
    $cases += @{id='filtered-frozen99-1m-4'; workload='wide'; count=1000000; threads=4; tagged=1; frozen=99}
}
if ($OnlyCases.Count) {
    foreach ($requested in $OnlyCases) {
        if ($requested -notin @($cases | ForEach-Object { $_.id })) { throw "Unknown benchmark case: $requested" }
    }
    $cases = @($cases | Where-Object { $_.id -in $OnlyCases })
}
foreach ($case in $cases) {
    foreach ($name in @('count','rate','tagged','frozen')) { if (!$case.ContainsKey($name)) { $case[$name] = 0 } }
}
New-Item -ItemType Directory -Path $OutputDir | Out-Null
$output = (Resolve-Path -LiteralPath $OutputDir).Path
New-Item -ItemType Directory -Path "$output/raw" | Out-Null
$utf8 = [Text.UTF8Encoding]::new($false)
function SaveJson($path, $value) { [IO.File]::WriteAllText($path, ($value | ConvertTo-Json -Depth 30), $utf8) }
function Hash($path) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Busy($ownedPid = 0) {
    $found = @(Get-Process -ErrorAction Stop | Where-Object {
        $_.Id -ne $ownedPid -and (
            $_.ProcessName -in @('ninja','clang','clang++','clang-cl','lld-link','link','cmake','ctest','cargo','rustc','tide_tests','tidec') -or
            $_.ProcessName -match '^(bench_|versus_|tidec_|tide_platform_|tide-bevy-bench)'
        )
    } | ForEach-Object { "$($_.ProcessName):$($_.Id)" })
    # Node services alone are not contention; include the known test/benchmark runners.
    $nodes = @(Get-CimInstance Win32_Process -Filter "Name = 'node.exe'" -ErrorAction Stop | Where-Object {
        $_.CommandLine -match '(run_wasi|run_web|web_threads|web_rooms|bench[/\\]|--test)'
    } | ForEach-Object { "node:$($_.ProcessId)" })
    @($found) + @($nodes)
}
function Quiet {
    $quiet = [Diagnostics.Stopwatch]::StartNew()
    $deadline = [Diagnostics.Stopwatch]::StartNew()
    while ($quiet.Elapsed.TotalSeconds -lt $QuietSeconds) {
        $busy = @(Busy)
        if ($busy.Count) {
            $quiet.Restart()
            $script:quietResets += ,@{at=[DateTime]::UtcNow.ToString('o'); processes=$busy}
        }
        if ($deadline.Elapsed.TotalMinutes -gt 10) { throw 'Quiet guard timed out after ten minutes' }
        Start-Sleep -Milliseconds 250
    }
}
function RunOne($engine, $case, $round, $order) {
    if (@(Busy).Count) { Quiet }
    $arguments = @('--threads', "$($case.threads)", '--warmup', "$Warmup", '--ticks', "$Ticks")
    if ($case.workload -eq 'churn') { $arguments += @('--rate', "$($case.rate)") }
    else { $arguments += @('--count', "$($case.count)") }
    if ($case.workload -eq 'wide') { $arguments += @('--tagged', "$($case.tagged)", '--frozen', "$($case.frozen)") }
    if ($engine -in @('Before','Tide')) {
        $program = if ($engine -eq 'Before') { $beforeBins[$case.workload] } else { $tideBins[$case.workload] }
        $arguments += '--json'
    } else {
        $program = $bevy
        $arguments += @('--workload', $case.workload)
    }
    $relative = "raw/$($case.id)-r$round-$engine.txt"
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $program
    # Every argument is a numeric value, a fixed option or a fixed workload name.
    $info.Arguments = $arguments -join ' '
    $info.WorkingDirectory = $root
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.EnvironmentVariables['TIDE_BENCH_REVISION'] = if ($engine -eq 'Before') { $BeforeRevision } else { "$($metadata.revision)+local" }
    $started = [DateTime]::UtcNow
    $process = [Diagnostics.Process]::Start($info)
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    $contention = @()
    try {
        while (!$process.WaitForExit(250)) {
            $busy = @(Busy $process.Id)
            if ($busy.Count) { $contention += ,@{at=[DateTime]::UtcNow.ToString('o'); processes=$busy} }
            if (([DateTime]::UtcNow - $started).TotalMinutes -gt 5) {
                $process.Kill()
                throw "Owned benchmark timed out: $relative"
            }
        }
        $outText = $stdout.GetAwaiter().GetResult()
        $errText = $stderr.GetAwaiter().GetResult()
        [IO.File]::WriteAllText("$output/$relative", $outText, $utf8)
        [IO.File]::WriteAllText("$output/$relative.stderr", $errText, $utf8)
        $json = @($outText -split '\r?\n' | Where-Object { $_.StartsWith('{') })
        $parsed = if ($json.Count -eq 1) { $json[0] | ConvertFrom-Json } else { $null }
        $record = @{case=$case.id; round=$round; engine=$engine; order=$order; stdout=$relative;
            stdout_sha256=(Hash "$output/$relative"); result=$parsed; contended=($contention.Count -gt 0);
            contention=$contention; executable=$program; executable_sha256=(Hash $program); arguments=$arguments; exit_code=$process.ExitCode;
            started=$started.ToString('o'); finished=[DateTime]::UtcNow.ToString('o')}
        [IO.File]::AppendAllText("$output/records.jsonl", (($record | ConvertTo-Json -Depth 20 -Compress) + "`n"), $utf8)
        Write-Output "$($case.id) round $round $engine exit $($process.ExitCode) contended $($record.contended)"
        if ($process.ExitCode -ne 0 -or !$parsed -or $record.contended) { throw "Failed or contended attempt retained: $relative" }
        return $record
    } finally {
        if (!$process.HasExited) { $process.Kill(); $process.WaitForExit() }
        $process.Dispose()
    }
}
$quietResets = @()
$metadata = @{schema=1; rounds=$Rounds; warmup=$Warmup; ticks=$Ticks; cases=$cases;
    quiet_seconds=$QuietSeconds; contention_poll_ms=250; complete=$false;
    started=[DateTime]::UtcNow.ToString('o'); revision=(& git rev-parse HEAD);
    git_status=@(& git status --short); os=[Environment]::OSVersion.VersionString;
    cpu=@(Get-CimInstance Win32_Processor | Select-Object Name,NumberOfCores,NumberOfLogicalProcessors);
    rustc=@(& rustc +1.95.0 -vV); cargo=(& cargo +1.95.0 --version);
    rustflags='-C target-cpu=x86-64 -C target-feature=-fma';
    methodology='Median per scheduled ECS tick; setup/digest/snapshots/network/render excluded; alternating process pairs; default affinity/clocks.';
    files=@{}; binaries=@{Tide=@{}; Bevy=(Hash $bevy)}}
foreach ($workload in @('particles','wide','churn')) { $metadata.binaries.Tide[$workload] = Hash $tideBins[$workload] }
if ($hasBefore) {
    $metadata.schema = 2
    $metadata.before = @{revision=$BeforeRevision; build_dir=(Resolve-Path -LiteralPath $BeforeBuildDir).Path; binaries=@{}}
    foreach ($workload in @('particles','wide','churn')) { $metadata.before.binaries[$workload] = Hash $beforeBins[$workload] }
    $metadata.methodology = $metadata.methodology.Replace('alternating process pairs', 'alternating adjacent before/current/Bevy process triples')
}
$env:TIDE_BENCH_REVISION = "$($metadata.revision)+local"
$env:TIDE_BENCH_MACHINE = ($metadata.cpu | ForEach-Object { $_.Name }) -join ', '
$inputs = @('bench/bevy/Cargo.toml','bench/bevy/Cargo.lock','bench/bevy/src/main.rs','bench/bevy/run.ps1',
    'bench/versus/versus.c','bench/versus/versus.h','compiler/src/codegen.c','compiler/src/rows_cache_codegen.inc','compiler/src/check.c','compiler/src/ast.h',
    'engine/src/table.c','engine/include/tide/table.h','engine/src/entity.c','engine/include/tide/entity.h',
    'engine/src/jobs.c','engine/include/tide/jobs.h','cmake/TideFlags.cmake',"$BuildDir/compile_commands.json",
    "$BuildDir/CMakeCache.txt",$BevyExe)
foreach ($workload in @('particles','wide','churn')) {
    $inputs += @("bench/versus/$workload/$workload.tide","bench/versus/$workload/$workload.c",
        "bench/versus/$workload/main.c","$BuildDir/bench/versus/tide/versus_$workload/$workload.c",
        "$BuildDir/bench/versus/tide/versus_$workload/$workload.h", "$BuildDir/bin/versus_$workload.exe")
}
if ($hasBefore) {
    # Preserve identities for all baseline evidence supplied alongside its executables.
    $inputs += @(Get-ChildItem -LiteralPath $BeforeBuildDir -File -Recurse | ForEach-Object { $_.FullName })
}
foreach ($path in $inputs) { $metadata.files[$path] = Hash $path }
SaveJson "$output/metadata.json" $metadata
$gate = [Threading.Mutex]::new($false, 'Local\TideClaudeContinuationBenchmark')
$held = $false
try {
    try { $held = $gate.WaitOne([TimeSpan]::FromMinutes(10)) } catch [Threading.AbandonedMutexException] { $held=$true }
    if (!$held) { throw 'Benchmark gate stayed busy for ten minutes' }
    Quiet
    for ($round=1; $round -le $Rounds; $round++) {
        foreach ($case in $cases) {
            $engines = if ($hasBefore) {
                if ($round % 2) { @('Before','Tide','Bevy') } else { @('Bevy','Tide','Before') }
            } else {
                if ($round % 2) { @('Tide','Bevy') } else { @('Bevy','Tide') }
            }
            $pair = @()
            for ($order=0; $order -lt $engines.Count; $order++) {
                # Progress goes to the host; only the record enters the pair.
                $items = @(RunOne $engines[$order] $case $round $order)
                foreach ($item in $items) { if ($item -is [string]) { Write-Host $item } else { $pair += $item } }
            }
            if ($pair.Count -ne $engines.Count -or @($pair | Where-Object { $_.result.digest -ne $pair[0].result.digest }).Count) {
                throw "Digest mismatch retained: $($case.id), round $round"
            }
        }
    }
    foreach ($path in $inputs) { if ($metadata.files[$path] -ne (Hash $path)) { throw "Measured input changed: $path" } }
    $metadata.complete = $true
} finally {
    $metadata.finished = [DateTime]::UtcNow.ToString('o')
    $metadata.quiet_resets = $quietResets
    SaveJson "$output/metadata.json" $metadata
    if ($held) { $gate.ReleaseMutex() }
    $gate.Dispose()
}
