# One bounded cohort. Own the shared gate here; do not wrap this in gate.ps1.
# Run from the checkout root after builds/correctness checks finish.
param([int]$QuietTimeoutSeconds = 90)
$env:TIDE_HIDDEN = '1'
$env:TIDE_BACKEND = 'vulkan'
$env:VK_LOADER_LAYERS_DISABLE = '~implicit~'
$env:TIDE_BENCH_MACHINE = 'Windows; AMD Ryzen 9 7900X; NVIDIA RTX 4080 SUPER; driver 617.42'
$ErrorActionPreference = 'Stop'
$exe = Join-Path (Get-Location) 'build/release/bin/bench_forest.exe'
$attempt = Join-Path $PSScriptRoot ('attempt-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ'))
New-Item -ItemType Directory -Path $attempt | Out-Null
$metadata = [ordered]@{
    status = 'waiting'; started_utc = [DateTime]::UtcNow.ToString('o'); completed_utc = $null
    executable_sha256 = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
    source_head = (& git rev-parse HEAD); source_dirty = [bool](& git status --porcelain -- bench/forest packages/render3d compiler/src engine/include/tide/texture_blocks.h platform)
    quiet_timeout_seconds = $QuietTimeoutSeconds; processes = 0; gpu_samples = 0
    contention = @(); arguments = '--count 100000 --width 1920 --height 1080 --warmup 20 --frames 100; ground/canopy/wide; impostors 0/1; two alternating rounds'
    source_files = @()
}
foreach ($path in @('bench/forest/game.tide','bench/forest/main.c','packages/render3d/impostor.tide','packages/render3d/pbr.tide','packages/render3d/materials.tide')) {
    $metadata.source_files += @{path=$path; sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()}
}
$env:TIDE_BENCH_REVISION = $metadata.source_head + '+foliage-quality-' + $metadata.executable_sha256.Substring(0,12)
function Contenders([int]$ChildPid = 0) {
    @(Get-CimInstance Win32_Process | Where-Object {
        $_.ProcessId -ne $ChildPid -and (
            $_.Name -match '^(ninja|clang|clang-cl|clang\+\+|lld-link|cmake|ctest|tidec|tide_tests)\.exe$' -or
            $_.Name -match '^(bench_|versus_|tide_platform_|tide_render3d_|tidec_)' -or
            ($_.Name -eq 'node.exe' -and $_.CommandLine -match '(test[\\/]|browser\.mjs|web-test|webgpu|webgl)')
        )
    } | Select-Object ProcessId,Name)
}
$mutex = [Threading.Mutex]::new($false, 'Local\TideClaudeContinuationBenchmark')
$held = $false
try {
    try { $held = $mutex.WaitOne([TimeSpan]::FromSeconds($QuietTimeoutSeconds)) }
    catch [Threading.AbandonedMutexException] { $held = $true }
    if (!$held) { $metadata.status = 'mutex_timeout'; return }
    Write-Output ('Shared gate acquired ' + [DateTime]::UtcNow.ToString('o'))
    $deadline = [Diagnostics.Stopwatch]::StartNew()
    $quiet = [Diagnostics.Stopwatch]::StartNew()
    while ($quiet.Elapsed.TotalSeconds -lt 5) {
        $busy = @(Contenders)
        if ($busy.Count) { $quiet.Restart(); $metadata.contention = @($busy) }
        if ($deadline.Elapsed.TotalSeconds -ge $QuietTimeoutSeconds) { $metadata.status = 'quiet_timeout'; return }
        Start-Sleep -Milliseconds 500
    }
    $metadata.contention = @()
    $metadata.status = 'measuring'
    foreach ($round in 1..2) {
        foreach ($camera in @('ground','canopy','wide')) {
            $order = if ($round -eq 1) { @(0,1) } else { @(1,0) }
            foreach ($mode in $order) {
                $busy = @(Contenders)
                if ($busy.Count) { $metadata.status='contended'; $metadata.contention += @($busy); return }
                $label = "r$round-$camera-mode$mode"
                $json = Join-Path $attempt ($label+'.jsonl')
                $params = @('--count','100000','--camera',$camera,'--impostors',"$mode",'--width','1920','--height','1080',
                    '--frames','100','--warmup','20','--output',$json)
                $child = Start-Process -FilePath $exe -ArgumentList $params -WindowStyle Hidden -PassThru `
                    -RedirectStandardOutput (Join-Path $attempt ($label+'.log')) -RedirectStandardError (Join-Path $attempt ($label+'.stderr.log'))
                $watch = [Diagnostics.Stopwatch]::StartNew()
                try {
                    while (!$child.WaitForExit(1000)) {
                        $busy = @(Contenders $child.Id)
                        if ($busy.Count) { $metadata.contention += @($busy) }
                        if ($watch.Elapsed.TotalSeconds -gt 90) { $child.Kill(); $child.WaitForExit(); throw "Owned benchmark $label exceeded 90 seconds" }
                    }
                    if ($child.ExitCode -ne 0) { throw "Benchmark $label exited $($child.ExitCode)" }
                } finally { $child.Dispose() }
                $row = Get-Content -LiteralPath $json -Raw | ConvertFrom-Json
                if ($row.trees -ne 100000 -or $row.camera -ne $camera -or $row.impostor_mode -ne $mode -or
                    $row.frames -ne 100 -or $row.warmup -ne 20 -or $row.gpu_samples -ne 100 -or !$row.world_unchanged -or
                    !$row.optimized -or $row.renderer -ne 'Vulkan' -or !$row.shadows -or !$row.wind -or !$row.fog -or
                    !$row.distance_levels -or $row.texture_upload_bytes_mean -ne 0 -or
                    ($row.primary_level_min | Measure-Object -Sum).Sum -ne 100000 -or
                    ($row.primary_level_max | Measure-Object -Sum).Sum -ne 100000) { throw "Settings/sample invariant failed for $label" }
                $metadata.processes++
                $metadata.gpu_samples += $row.gpu_samples
                Write-Output "Completed $label"
                if ($metadata.contention.Count) { $metadata.status='contended'; return }
            }
        }
    }
    if ((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant() -ne $metadata.executable_sha256) { throw 'Executable changed during cohort' }
    $metadata.status = 'passed'
} catch {
    $metadata.status = 'failed'; $metadata.error = $_.Exception.Message; throw
} finally {
    if ($held) { $mutex.ReleaseMutex() }
    $mutex.Dispose()
    $metadata.completed_utc = [DateTime]::UtcNow.ToString('o')
    $metadata | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $attempt 'status.json') -Encoding utf8
    Write-Output ('Gate closed ' + $metadata.status + ' ' + $metadata.completed_utc)
}
