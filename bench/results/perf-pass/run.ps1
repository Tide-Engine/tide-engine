# Runs a set of benchmarks under the shared benchmark gate, after a quiet
# period with no builds, tests or other benchmarks, and saves each run under
# "<Label>-r<round>" in the ledgers of bench/results. Binaries are copied
# first, so rebuilding while it runs measures nothing new by accident.
#   pwsh bench/results/perf-pass/run.ps1 -Label perf-pass-baseline -Rounds 3
#   pwsh bench/results/perf-pass/run.ps1 -Label x -Only versus_life,versus_events
param(
    [Parameter(Mandatory)][string]$Label,
    [int]$Rounds = 3,
    [string[]]$Only = @(),
    [string]$Bin = 'build/release/bin',
    [int]$QuietSeconds = 60,
    [int]$QuietDeadlineMinutes = 30,
    [string]$BuiltFrom = '' # The revision the binaries were built from, when they are not this checkout's (a frozen earlier build)
)
$env:TIDE_HIDDEN = '1'
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
Set-Location $root
$evidence = Join-Path $root "build/perf-pass/$Label"
$frozen = Join-Path $evidence 'bin'
New-Item -ItemType Directory -Path $frozen -Force | Out-Null

# name, program, arguments (each gets --save), environment
$all = @(
    @{name='versus_particles'; exe='versus_particles'; args=@()},
    @{name='versus_particles_kept'; exe='versus_particles'; args=@('--kept','1')},
    @{name='versus_wide'; exe='versus_wide'; args=@()},
    @{name='versus_sand'; exe='versus_sand'; args=@()},
    @{name='versus_life'; exe='versus_life'; args=@()},
    @{name='versus_churn'; exe='versus_churn'; args=@()},
    @{name='versus_events'; exe='versus_events'; args=@()},
    @{name='versus_nbody'; exe='versus_nbody'; args=@()},
    @{name='versus_buckets'; exe='versus_buckets'; args=@()},
    @{name='versus_lookup'; exe='versus_lookup'; args=@()},
    @{name='versus_lookup_steady'; exe='versus_lookup'; args=@('--changes','0')},
    @{name='versus_rollback'; exe='versus_rollback'; args=@()},
    @{name='versus_rollback_spread'; exe='versus_rollback'; args=@('--spread','1')},
    @{name='versus_rollback_scaling'; exe='versus_rollback'; args=@('--movers','1000000','--turrets','40000','--threads-list','1,2,4,8,12')},
    @{name='versus_systems'; exe='versus_systems'; args=@()},
    @{name='versus_systems_empty'; exe='versus_systems'; args=@('--count','0')},
    @{name='versus_systems_small'; exe='versus_systems'; args=@('--count','64')},
    @{name='versus_systems_mid'; exe='versus_systems'; args=@('--count','16384','--threads-list','1,2,4,8,12')},
    @{name='versus_systems_scaling'; exe='versus_systems'; args=@('--count','65536','--threads-list','1,2,4,8,12')},
    # Every versus program's ways on 1 to 12 threads
    @{name='versus_particles_scaling'; exe='versus_particles'; args=@('--threads-list','1,2,4,8,12')},
    @{name='versus_wide_scaling'; exe='versus_wide'; args=@('--threads-list','1,2,4,8,12')},
    @{name='versus_sand_scaling'; exe='versus_sand'; args=@('--threads-list','1,2,4,8,12')},
    @{name='versus_life_scaling'; exe='versus_life'; args=@('--threads-list','1,2,4,8,12')},
    @{name='versus_churn_scaling'; exe='versus_churn'; args=@('--threads-list','1,2,4,8,12')},
    @{name='versus_events_scaling'; exe='versus_events'; args=@('--threads-list','1,2,4,8,12')},
    @{name='versus_nbody_scaling'; exe='versus_nbody'; args=@('--threads-list','1,2,4,8,12')},
    @{name='versus_buckets_scaling'; exe='versus_buckets'; args=@('--threads-list','1,2,4,8,12')},
    @{name='versus_lookup_scaling'; exe='versus_lookup'; args=@('--threads-list','1,2,4,8,12')},
    @{name='bench_sand'; exe='bench_sand'; args=@()},
    @{name='bench_gallery'; exe='bench_gallery'; args=@()},
    @{name='draw_opengl'; exe='tide_platform_draw'; args=@('--bench'); backend='opengl'},
    @{name='draw_vulkan'; exe='tide_platform_draw'; args=@('--bench'); backend='vulkan'},
    @{name='versus_draw_opengl'; exe='versus_draw'; args=@(); backend='opengl'}
)
$set = @($all | Where-Object { ($Only.Count -eq 0 -or $Only -contains $_.name) -and (Test-Path (Join-Path $Bin "$($_.exe).exe")) })
if ($set.Count -eq 0) { throw 'Nothing to run' }
foreach ($exe in ($set | ForEach-Object { $_.exe } | Sort-Object -Unique)) { Copy-Item -LiteralPath (Join-Path $Bin "$exe.exe") -Destination $frozen -Force }

$revision = (git rev-parse HEAD).Trim()
$dirty = @(git status --porcelain --untracked-files=no).Count -gt 0
$env:TIDE_BENCH_REVISION = if ($BuiltFrom) { $BuiltFrom } elseif ($dirty) { "$revision+local" } else { $revision }
$env:TIDE_BENCH_MACHINE = 'AMD Ryzen 9 7900X 12C/24T; NVIDIA GeForce RTX 4080 SUPER; Windows; Clang 23.1.2'
$meta = [ordered]@{label=$Label; revision=$env:TIDE_BENCH_REVISION; machine=$env:TIDE_BENCH_MACHINE; startedUtc=(Get-Date).ToUniversalTime().ToString('o'); runs=@()
    binaries=@(Get-ChildItem $frozen -File | ForEach-Object { @{name=$_.Name; sha256=(Get-FileHash $_.FullName).Hash.ToLower()} })}
function Save-Metadata { $meta | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $evidence 'metadata.json') -Encoding utf8 }
function Busy([int]$allowed = 0) { @(Get-Process | Where-Object { $_.Id -ne $allowed -and ($_.ProcessName -in @('ninja','clang','clang++','clang-cl','lld-link','cmake','ctest','tide_tests','tidec') -or $_.ProcessName -match '^(bench_|versus_|tidec_)') }) }

$gate = [Threading.Mutex]::new($false, 'Local\TideClaudeContinuationBenchmark')
$held = $false; $process = $null
try {
    try { $held = $gate.WaitOne([TimeSpan]::FromMinutes($QuietDeadlineMinutes)) } catch [Threading.AbandonedMutexException] { $held = $true }
    if (!$held) { throw 'Benchmark gate stayed busy' }
    $quiet = [Diagnostics.Stopwatch]::StartNew(); $deadline = [Diagnostics.Stopwatch]::StartNew()
    while ($quiet.Elapsed.TotalSeconds -lt $QuietSeconds) {
        if (@(Busy).Count) { $quiet.Restart() }
        if ($deadline.Elapsed.TotalMinutes -gt $QuietDeadlineMinutes) { throw 'Quiet guard timed out' }
        Start-Sleep -Seconds 5
    }
    foreach ($round in 1..$Rounds) {
        foreach ($b in $set) {
            # Wait out builds/tests that started meanwhile, rather than measure under them
            $wait = [Diagnostics.Stopwatch]::StartNew()
            while (@(Busy).Count) { if ($wait.Elapsed.TotalMinutes -gt $QuietDeadlineMinutes) { throw 'Competing work never stopped' }; Start-Sleep -Seconds 5 }
            $saveLabel = "$Label-r$round"
            $arguments = @($b.args) + @('--save', $saveLabel)
            if ($b.backend) { $env:TIDE_BACKEND = $b.backend } else { Remove-Item Env:TIDE_BACKEND -ErrorAction SilentlyContinue }
            $prefix = Join-Path $evidence "$($b.name)-r$round"
            Write-Output "$($b.name) round $round"
            $watch = [Diagnostics.Stopwatch]::StartNew()
            $process = Start-Process -FilePath (Join-Path $frozen "$($b.exe).exe") -ArgumentList $arguments -WindowStyle Hidden -PassThru -RedirectStandardOutput "$prefix.log" -RedirectStandardError "$prefix.err"
            $interrupted = $false
            while (!$process.WaitForExit(1000)) {
                if (@(Busy $process.Id).Count) { $interrupted = $true }
                if ($watch.Elapsed.TotalMinutes -gt 15) { throw "$($b.name) took over 15 minutes" }
            }
            $process.Refresh()
            $meta.runs += @{name=$b.name; round=$round; label=$saveLabel; arguments=$arguments; backend=$b.backend; exitCode=$process.ExitCode; wallSeconds=$watch.Elapsed.TotalSeconds; competingWork=$interrupted}
            Save-Metadata
            if ($process.ExitCode -ne 0) { Get-Content "$prefix.log" -Tail 20; Get-Content "$prefix.err"; throw "$($b.name) failed" }
            if ($interrupted) { Write-Output "  (other builds/tests ran during it: marked competingWork)" }
            $process = $null
        }
    }
    $meta.finishedUtc = (Get-Date).ToUniversalTime().ToString('o')
    Save-Metadata
} finally {
    if ($process -and !$process.HasExited) { Stop-Process -Id $process.Id }
    Remove-Item Env:TIDE_BACKEND -ErrorAction SilentlyContinue
    if ($held) { $gate.ReleaseMutex() }
    $gate.Dispose()
}
