param([ValidateSet('before', 'after')][string]$Variant = 'before')
$env:TIDE_HIDDEN = '1'
$env:TIDE_BACKEND = 'vulkan'
$env:VK_LOADER_LAYERS_DISABLE = '~implicit~'
$env:TIDE_BENCH_MACHINE = 'Windows; AMD Ryzen 9 7900X; NVIDIA RTX 4080 SUPER; driver 617.42'
$ErrorActionPreference = 'Stop'
$root = (Get-Location).Path
$metadata = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'baseline-metadata.json') -Raw | ConvertFrom-Json
$variantDir = if ($Variant -eq 'before') { 'before' } else { 'after-final' }
$exe = Join-Path $root "build/forest-batch/$variantDir/bench_forest.exe"
if (!(Test-Path -LiteralPath $exe)) { throw "Missing frozen $Variant executable: $exe" }
$expected = $metadata.baseline_binary_sha256
if ($Variant -eq 'after') {
    $candidate = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'final-candidate-metadata.json') -Raw | ConvertFrom-Json
    if ($candidate.candidate_stage -ne 'final') { throw 'Final candidate metadata is required' }
    $expected = $candidate.binary_sha256
}
if ((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) {
    throw "Frozen $Variant executable identity changed"
}
$captureDir = Join-Path $root "build/forest-batch/captures-$Variant"
if (Test-Path -LiteralPath $captureDir) { throw "Refusing to overwrite existing capture directory: $captureDir" }
New-Item -ItemType Directory -Path $captureDir | Out-Null
$env:TIDE_BENCH_REVISION = if ($Variant -eq 'before') { $metadata.baseline_revision_full } else { $candidate.revision }
foreach ($case in @(@(10000, 'canopy'), @(100000, 'wide'), @(100000, 'ground'))) {
    $count = $case[0]; $camera = $case[1]
    $log = Join-Path $PSScriptRoot "capture-$Variant-$count-$camera.log"
    if (Test-Path -LiteralPath $log) { throw "Refusing to overwrite existing capture log: $log" }
    $capture = Join-Path $captureDir "$count-$camera.png"
    # The final frame is exactly the prior report's 2 warmup + 4 measured frame capture.
    & $exe --count $count --camera $camera --width 1920 --height 1080 --warmup 2 --frames 4 --capture $capture *> $log
    if ($LASTEXITCODE) { throw "Forest capture $Variant/$count/$camera exited $LASTEXITCODE" }
    Write-Output "Captured $Variant $count/$camera"
}
