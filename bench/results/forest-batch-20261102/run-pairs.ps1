# Run only after all candidate code, builds and correctness tests have finished.
# Wrap this script with bench/results/perf-pass/gate.ps1; it holds the shared
# benchmark mutex and waits for its five-second initial quiet guard.
$env:TIDE_HIDDEN = '1'
$env:TIDE_BACKEND = 'vulkan'
$env:VK_LOADER_LAYERS_DISABLE = '~implicit~'
$env:TIDE_BENCH_MACHINE = 'Windows; AMD Ryzen 9 7900X; NVIDIA RTX 4080 SUPER; driver 617.42'
$ErrorActionPreference = 'Stop'
$root = (Get-Location).Path
$baseline = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'baseline-metadata.json') -Raw | ConvertFrom-Json
$candidate = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'final-candidate-metadata.json') -Raw | ConvertFrom-Json
if ($candidate.candidate_stage -ne 'final') { throw 'Final candidate metadata is required' }
$captures = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'baseline-captures.json') -Raw | ConvertFrom-Json
$records = @{}
$invariantOutput = Join-Path $PSScriptRoot 'measurement-invariants.json'
if (Test-Path -LiteralPath $invariantOutput) { throw 'Invariant report already exists; refusing to overwrite an earlier batch' }
$executables = @{
    before = Join-Path $root 'build/forest-batch/before/bench_forest.exe'
    after = Join-Path $root 'build/forest-batch/after-final/bench_forest.exe'
}
foreach ($variant in @('before', 'after')) {
    $expected = if ($variant -eq 'before') { $baseline.baseline_binary_sha256 } else { $candidate.binary_sha256 }
    if ((Get-FileHash -LiteralPath $executables[$variant] -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) {
        throw "Frozen $variant executable identity changed"
    }
    foreach ($round in 1..3) {
        if (Test-Path -LiteralPath (Join-Path $PSScriptRoot "$variant-r$round.jsonl")) {
            throw 'Paired output already exists; select a fresh evidence directory instead of appending another batch'
        }
        foreach ($case in @(@(10000, 'canopy'), @(100000, 'wide'), @(100000, 'ground'))) {
            if (Test-Path -LiteralPath (Join-Path $PSScriptRoot "$variant-r$round-$($case[0])-$($case[1]).log")) {
                throw 'Paired log already exists; refusing to overwrite earlier attempts'
            }
        }
    }
}
foreach ($round in 1..3) {
    foreach ($case in @(@(10000, 'canopy'), @(100000, 'wide'), @(100000, 'ground'))) {
        $count = $case[0]; $camera = $case[1]
        $order = if ($round -eq 2) { @('after', 'before') } else { @('before', 'after') }
        foreach ($variant in $order) {
            $env:TIDE_BENCH_REVISION = if ($variant -eq 'before') { $baseline.baseline_revision_full } else { $candidate.revision }
            $runArgs = @('--count', "$count", '--camera', $camera, '--width', '1920', '--height', '1080',
                '--frames', '100', '--warmup', '20', '--output', (Join-Path $PSScriptRoot "$variant-r$round.jsonl"))
            & $executables[$variant] @runArgs *> (Join-Path $PSScriptRoot "$variant-r$round-$count-$camera.log")
            if ($LASTEXITCODE) { throw "Forest $variant r$round/$count/$camera exited $LASTEXITCODE" }
            $rows = @(Get-Content -LiteralPath (Join-Path $PSScriptRoot "$variant-r$round.jsonl") | Where-Object { $_.Trim() } | ForEach-Object { $_ | ConvertFrom-Json })
            $row = $rows[-1]
            $expectedCase = @($captures | Where-Object { $_.case -eq "$count-$camera" })
            if ($expectedCase.Count -ne 1) { throw "Missing baseline capture witness for $count/$camera" }
            $expectedWork = $expectedCase[0]
            if ($row.trees -ne $count -or $row.camera -ne $camera -or !$row.optimized -or !$row.world_unchanged -or $row.renderer -ne 'Vulkan' `
                -or $row.width -ne 1920 -or $row.height -ne 1080 -or $row.warmup -ne 20 -or $row.frames -ne 100 -or $row.gpu_samples -ne 100 `
                -or !$row.distance_levels -or !$row.shadows -or !$row.wind -or $row.shadow_resolution -ne 1024 -or $row.shadow_views -ne 4 `
                -or $row.mesh_entities -ne ($count * 2 + 1) -or ($row.near_trees + $row.middle_trees + $row.far_trees) -ne $count `
                -or $row.texture_upload_bytes_mean -ne 0 -or $row.world_hash -ne $expectedWork.world_hash `
                -or $row.main_bark_instances_min -ne $expectedWork.main_foliage_instances -or $row.main_bark_instances_max -ne $expectedWork.main_foliage_instances `
                -or $row.main_foliage_instances_min -ne $expectedWork.main_foliage_instances -or $row.main_foliage_instances_max -ne $expectedWork.main_foliage_instances `
                -or $row.draw_calls_min -ne $expectedWork.draw_calls -or $row.draw_calls_max -ne $expectedWork.draw_calls `
                -or $row.submitted_vertices_min -ne $expectedWork.submitted_vertices -or $row.submitted_vertices_max -ne $expectedWork.submitted_vertices) {
                throw "Forest work/settings/frame-query invariant failed: $variant r$round/$count/$camera"
            }
            $records["$variant-$round-$count-$camera"] = $row
            $other = if ($variant -eq 'before') { 'after' } else { 'before' }
            $paired = $records["$other-$round-$count-$camera"]
            if ($paired) {
                foreach ($key in @('trees','camera','near_trees','middle_trees','far_trees','world_hash','shadow_views','shadow_resolution',
                    'draw_calls_min','draw_calls_max','submitted_vertices_min','submitted_vertices_max','main_bark_instances_min',
                    'main_bark_instances_max','main_foliage_instances_min','main_foliage_instances_max','texture_upload_bytes_mean','extent_metres')) {
                    if ($row.$key -ne $paired.$key) { throw "Forest paired invariant $key differs: r$round/$count/$camera" }
                }
            }
            Write-Output "Completed $variant r$round $count/$camera"
        }
    }
}
if ($records.Count -ne 18) { throw "Expected exactly 18 completed processes, found $($records.Count)" }
foreach ($variant in @('before', 'after')) {
    foreach ($round in 1..3) {
        $savedRows = @(Get-Content -LiteralPath (Join-Path $PSScriptRoot "$variant-r$round.jsonl") | Where-Object { $_.Trim() })
        if ($savedRows.Count -ne 3) { throw "Expected exactly three retained rows in $variant-r$round.jsonl, found $($savedRows.Count)" }
    }
}
$report = [ordered]@{
    processes = $records.Count
    measured_frames = 1800
    completed_gpu_queries = 1800
    settings_match = $true
    paired_world_hashes_match = $true
    baseline_capture_work_matches = $true
    zero_warmed_texture_upload_bytes = $true
}
$report | ConvertTo-Json | Set-Content -LiteralPath $invariantOutput -Encoding UTF8
