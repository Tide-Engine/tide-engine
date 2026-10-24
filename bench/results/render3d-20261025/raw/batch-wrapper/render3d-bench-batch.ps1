$env:TIDE_HIDDEN = '1'
$ErrorActionPreference = 'Stop'
Set-Location 'C:/Users/blenm/.t3/worktrees/PurrEngine/t3-eeefbfdc'
$batchRoot = Join-Path (Get-Location) 'build/render3d-measurements/20261025-batch'
if (Test-Path -LiteralPath $batchRoot) { throw 'Measurement batch directory already exists' }
New-Item -ItemType Directory -Path $batchRoot -Force | Out-Null
$revision = 'a1d962c99d93657d23f79ba54b0c836f635905e8'
$plans = @(
    @{label='20261025-mesh'; counts='100,1000,10000'; cases='unshadowed,dir'; directional=1; point=0; spot=0; maps=1},
    @{label='20261025-light1'; counts='1000'; cases='unshadowed'; directional=0; point=1; spot=0; maps=0},
    @{label='20261025-light8'; counts='1000'; cases='unshadowed'; directional=0; point=8; spot=0; maps=0},
    @{label='20261025-light32'; counts='1000'; cases='unshadowed'; directional=0; point=32; spot=0; maps=0},
    @{label='20261025-point'; counts='1000'; cases='unshadowed,point'; directional=0; point=1; spot=0; maps=6},
    @{label='20261025-mixed'; counts='1000'; cases='unshadowed,mixed'; directional=1; point=2; spot=1; maps=8}
)
$batch = [ordered]@{revision=$revision; startedUtc=[DateTime]::UtcNow.ToString('o'); maximumSeconds=180; stopAtSeconds=170; state='running'; runs=@()}
function Save-Batch { $batch | ConvertTo-Json -Depth 9 | Set-Content -LiteralPath (Join-Path $batchRoot 'batch.json') -Encoding utf8 }
$watch = [Diagnostics.Stopwatch]::StartNew()
$absoluteDeadline = [DateTime]::UtcNow.Date.AddHours(23).AddMinutes(30)
$stopSeconds = [Math]::Min(170, ($absoluteDeadline - [DateTime]::UtcNow).TotalSeconds - 3)
$batch.absoluteDeadlineUtc = $absoluteDeadline.ToString('o')
$batch.stopAtSeconds = $stopSeconds
if ($stopSeconds -le 0) { throw 'Explicit quiet window has already ended' }
$process = $null
try {
    foreach ($plan in $plans) {
        if ($watch.Elapsed.TotalSeconds -ge $stopSeconds - 25) { $batch.state='partial_time_budget'; break }
        $prefix = Join-Path $batchRoot $plan.label
        $run = [ordered]@{label=$plan.label; state='starting'; startedSeconds=$watch.Elapsed.TotalSeconds; exitCode=$null}
        $batch.runs += $run
        Save-Batch
        $quiet = if ($batch.runs.Count -eq 1) { 5 } else { 1 }
        $arguments = @('-NoProfile','-File','bench/render3d/run.ps1','-Label',$plan.label,'-Revision',$revision,
            '-Counts',$plan.counts,'-Cases',$plan.cases,'-Directional',"$($plan.directional)",'-Point',"$($plan.point)",
            '-Spot',"$($plan.spot)",'-ShadowMaps',"$($plan.maps)",'-Frames','100','-Warmup','20','-QuietSeconds',"$quiet")
        Write-Output "Starting $($plan.label) at $([math]::Round($watch.Elapsed.TotalSeconds,1)) seconds"
        $process = Start-Process -FilePath (Get-Command pwsh -CommandType Application).Source -ArgumentList $arguments -WindowStyle Hidden -PassThru -RedirectStandardOutput "$prefix.log" -RedirectStandardError "$prefix.err"
        $run.pid=$process.Id; $run.state='running'
        while (!$process.WaitForExit(250)) {
            if ($watch.Elapsed.TotalSeconds -ge $stopSeconds) {
                $run.state='stopped_at_batch_deadline'; $batch.state='partial_time_budget'
                # This is exclusively the process tree launched immediately above.
                & taskkill.exe /PID $process.Id /T /F *> "$prefix-stop.log"
                $process.WaitForExit()
                break
            }
        }
        $process.Refresh(); $run.exitCode=$process.ExitCode; $run.finishedSeconds=$watch.Elapsed.TotalSeconds
        if ($run.state -eq 'running') { $run.state=if($process.ExitCode -eq 0){'completed'}else{'failed'} }
        $process.Dispose(); $process=$null
        Save-Batch
        if ($run.state -ne 'completed') { if($batch.state -eq 'running'){$batch.state='failed'}; break }
    }
    if ($batch.state -eq 'running') { $batch.state='completed' }
} catch { $batch.state='failed'; $batch.error=$_.Exception.Message; throw }
finally {
    if ($process -and !$process.HasExited) { & taskkill.exe /PID $process.Id /T /F *> (Join-Path $batchRoot 'cleanup.log'); $process.WaitForExit() }
    $batch.wallSeconds=$watch.Elapsed.TotalSeconds; $batch.finishedUtc=[DateTime]::UtcNow.ToString('o'); Save-Batch
    Write-Output "Batch $($batch.state): $([math]::Round($watch.Elapsed.TotalSeconds,2)) seconds"
}
