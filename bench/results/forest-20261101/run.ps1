$env:TIDE_HIDDEN = '1'
$env:TIDE_BACKEND = 'vulkan'
$env:VK_LOADER_LAYERS_DISABLE = '~implicit~'
$env:TIDE_BENCH_MACHINE = 'Windows; AMD Ryzen 9 7900X; NVIDIA RTX 4080 SUPER; driver 617.42'
$env:TIDE_BENCH_REVISION = ((git rev-parse HEAD).Trim() + '+foliage-worktree')
$ErrorActionPreference = 'Stop'
$resultDir = Join-Path $PSScriptRoot '.'
$exe = Join-Path (Get-Location) 'build/release/bin/bench_forest.exe'
foreach ($round in 1..3) {
    foreach ($count in @(1000, 10000, 50000, 100000)) {
        $runArgs = @('--count', "$count", '--camera', 'wide', '--frames', '100', '--warmup', '20', '--output', (Join-Path $resultDir "round-$round.jsonl"))
        if ($round -eq 3 -and $count -eq 100000) { $runArgs += @('--capture', (Join-Path (Get-Location) 'build/forest-100k-wide.png')) }
        & $exe @runArgs *> (Join-Path $resultDir "r$round-$count.log")
        if ($LASTEXITCODE) { throw "forest r$round/$count exited $LASTEXITCODE" }
        Write-Output "Completed round $round, $count trees"
    }
}
