$env:TIDE_HIDDEN = '1'
$env:TIDE_BACKEND = 'vulkan'
$env:VK_LOADER_LAYERS_DISABLE = '~implicit~'
$env:TIDE_BENCH_MACHINE = 'Windows; AMD Ryzen 9 7900X; NVIDIA RTX 4080 SUPER; driver 617.42'
$env:TIDE_BENCH_REVISION = ((git rev-parse HEAD).Trim() + '+foliage-worktree')
$ErrorActionPreference = 'Stop'
$exe = Join-Path (Get-Location) 'build/release/bin/bench_forest.exe'
foreach ($round in 1..3) {
    foreach ($case in @('canopy','ground','detail')) {
        $count = if ($case -eq 'ground') { 100000 } else { 10000 }
        $camera = if ($case -eq 'ground') { 'ground' } else { 'canopy' }
        $runArgs = @('--count', "$count", '--camera', $camera, '--frames', '100', '--warmup', '20', '--output', (Join-Path $PSScriptRoot "supplement-$round.jsonl"))
        if ($case -eq 'detail') { $runArgs += @('--levels', '0') }
        if ($round -eq 3 -and $case -ne 'detail') { $runArgs += @('--capture', (Join-Path (Get-Location) "build/forest-$count-$camera.png")) }
        & $exe @runArgs *> (Join-Path $PSScriptRoot "r$round-$case.log")
        if ($LASTEXITCODE) { throw "forest r$round/$case exited $LASTEXITCODE" }
        Write-Output "Completed supplement round $round, $case"
    }
}
