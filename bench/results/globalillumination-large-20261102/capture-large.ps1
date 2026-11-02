$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$env:TIDE_HIDDEN = '1'
$env:TIDE_BACKEND = 'vulkan'
$root = (git -C $PSScriptRoot rev-parse --show-toplevel).Trim()
Push-Location $root
try {
    $destination = Join-Path $root 'build/gi-large/captures/final'
    if (Test-Path -LiteralPath $destination) { throw 'Capture destination must be new' }
    New-Item -ItemType Directory -Path $destination | Out-Null
    foreach ($scene in @('atrium','courtyard')) {
        foreach ($view in @('hero','overview')) {
            foreach ($case in @('off','512','2048','probes')) {
                $stem = "$scene-$view-$case"
                $options = @('--scene', $scene, '--frames','1','--warmup','1',
                    '--mode', $(if ($case -eq 'off') {'off'} else {'update4'}),
                    '--probes', $(if ($case -eq '512' -or $case -eq 'off') {'512'} else {'2048'}),
                    '--output', (Join-Path $destination "$stem.jsonl"),
                    '--capture', (Join-Path $destination "$stem.png"))
                if ($view -eq 'overview') { $options += '--overview' }
                if ($case -eq 'probes') { $options += '--debug' }
                & build/release/bin/bench_globalillumination.exe @options *> (Join-Path $destination "$stem.log")
                if ($LASTEXITCODE -ne 0) { throw "Capture failed: $stem" }
                Write-Output "Captured $stem"
            }
        }
    }
} finally { Pop-Location }
