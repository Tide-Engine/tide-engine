param([ValidateSet('before', 'after')][string]$Variant = 'before')
$env:TIDE_HIDDEN = '1'
$env:TIDE_BACKEND = 'vulkan'
$env:VK_LOADER_LAYERS_DISABLE = '~implicit~'
$env:TIDE_BENCH_MACHINE = 'Windows; AMD Ryzen 9 7900X; NVIDIA RTX 4080 SUPER; driver 617.42'
$ErrorActionPreference = 'Stop'
$root = (Get-Location).Path
$baseline = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'baseline-metadata.json') -Raw | ConvertFrom-Json
$variantDir = if ($Variant -eq 'before') { 'before' } else { 'after-final' }
$exe = Join-Path $root "build/forest-batch/$variantDir/bench_forest.exe"
$expected = $baseline.baseline_binary_sha256
$env:TIDE_BENCH_REVISION = $baseline.baseline_revision_full
if ($Variant -eq 'after') {
    $candidate = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'final-candidate-metadata.json') -Raw | ConvertFrom-Json
    if ($candidate.candidate_stage -ne 'final') { throw 'Final candidate metadata is required' }
    $expected = $candidate.binary_sha256
    $env:TIDE_BENCH_REVISION = $candidate.revision
}
if ((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) {
    throw "Frozen $Variant executable identity changed"
}
foreach ($camera in @('ground', 'wide')) {
    $log = Join-Path $PSScriptRoot "profile-$Variant-100000-$camera.log"
    if (Test-Path -LiteralPath $log) { throw "Refusing to overwrite profile diagnostic: $log" }
}
foreach ($camera in @('ground', 'wide')) {
    $log = Join-Path $PSScriptRoot "profile-$Variant-100000-$camera.log"
    & $exe --count 100000 --camera $camera --width 1920 --height 1080 --warmup 20 --frames 20 --profile *> $log
    if ($LASTEXITCODE) { throw "Forest diagnostic $Variant/100000/$camera exited $LASTEXITCODE" }
    Write-Output "Profiled $Variant 100000/$camera (instrumented diagnostic only)"
}
