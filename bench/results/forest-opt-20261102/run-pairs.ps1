$env:TIDE_HIDDEN = '1'
$env:TIDE_BACKEND = 'vulkan'
$env:VK_LOADER_LAYERS_DISABLE = '~implicit~'
$env:TIDE_BENCH_MACHINE = 'Windows; AMD Ryzen 9 7900X; NVIDIA RTX 4080 SUPER; driver 617.42'
$ErrorActionPreference = 'Stop'
$root = (Get-Location).Path
$executables = @{
    before = Join-Path $root 'build/forest-opt/before/bench_forest.exe'
    after = Join-Path $root 'build/forest-opt/after/bench_forest.exe'
}
foreach ($round in 1..3) {
    foreach ($case in @(@(10000, 'canopy'), @(100000, 'wide'), @(100000, 'ground'))) {
        $count = $case[0]; $camera = $case[1]
        $order = if ($round -eq 2) { @('after', 'before') } else { @('before', 'after') }
        foreach ($variant in $order) {
            $env:TIDE_BENCH_REVISION = if ($variant -eq 'before') { '1f825bbe5' } else { '1f825bbe5+forest-preparation-optimization' }
            $runArgs = @('--count', "$count", '--camera', $camera, '--frames', '100', '--warmup', '20', '--output', (Join-Path $PSScriptRoot "$variant-r$round.jsonl"))
            & $executables[$variant] @runArgs *> (Join-Path $PSScriptRoot "$variant-r$round-$count-$camera.log")
            if ($LASTEXITCODE) { throw "forest $variant r$round/$count/$camera exited $LASTEXITCODE" }
            Write-Output "Completed $variant r$round $count/$camera"
        }
    }
}
