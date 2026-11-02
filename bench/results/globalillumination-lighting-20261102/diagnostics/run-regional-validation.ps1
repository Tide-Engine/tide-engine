param([Parameter(Mandatory=$true)][ValidateSet('sanitize','web-release')][string]$Preset)
$env:TIDE_HIDDEN = '1'
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$giRoot = (Get-Location).Path
$giWork = Join-Path $giRoot 'build/gi-quality-20261102'
$giOutput = Join-Path $giWork "$Preset-regional-validation"
if (Test-Path -LiteralPath $giOutput) { throw 'Validation output must be new' }
New-Item -ItemType Directory -Path $giOutput | Out-Null
$giScope = if ($Preset -eq 'sanitize') {'^tide_globalillumination_pixels(_opengl)?$'} else {'^tide_globalillumination_pixels_regional(512|2048)($|_)'}
$giList = & 'C:/Program Files/CMake/bin/ctest.exe' --test-dir "build/$Preset" --show-only=json-v1 -R $giScope
if ($LASTEXITCODE -ne 0) { throw 'Could not enumerate scoped tests' }
$giList | Set-Content -LiteralPath (Join-Path $giOutput 'tests.json') -Encoding utf8
$giTests = @((($giList -join "`n") | ConvertFrom-Json).tests)
$giMinimum = if ($Preset -eq 'sanitize') {2} else {6}
if ($giTests.Count -lt $giMinimum) { throw "Missing scoped registrations: $($giTests.Count) < $giMinimum" }
$giGateName = [regex]::Match((Get-Content -LiteralPath (Join-Path $giRoot 'bench/results/perf-pass/gate.ps1') -Raw), "'(Local[^']+Benchmark)'").Groups[1].Value
if (!$giGateName) { throw 'Missing shared mutex name' }
$giGate = [Threading.Mutex]::new($false,$giGateName)
$giResults = @()
$giFailed = $false
try {
    foreach ($giTest in $giTests) {
        $giName = [string]$giTest.name
        $giHeld = $false
        $giRow = [ordered]@{preset=$Preset;test=$giName;started_utc=[DateTime]::UtcNow.ToString('o');gate_name=$giGateName;timing_status='correctness-only; excluded from performance claims'}
        $giLog = Join-Path $giOutput "$giName.log"
        try {
            try { $giHeld=$giGate.WaitOne([TimeSpan]::FromMinutes(5)) } catch [Threading.AbandonedMutexException] {$giHeld=$true}
            if (!$giHeld) { throw 'Shared gate stayed occupied for five minutes' }
            $giRow.gate_acquired_utc=[DateTime]::UtcNow.ToString('o')
            $giExact='^'+[regex]::Escape($giName)+'$'
            & 'C:/Program Files/CMake/bin/ctest.exe' --preset $Preset -R $giExact --no-tests=error --verbose -j1 *> $giLog
            $giRow.ctest_exit=$LASTEXITCODE
            $giText=Get-Content -LiteralPath $giLog -Raw
            $giRow.passed=$LASTEXITCODE -eq 0 -and $giText -match '100% tests passed, 0 tests failed out of 1' -and $giText -notmatch 'SKIPPED:|No tests were found|Could not find executable|\*\*\*Not Run'
            if (!$giRow.passed) {$giFailed=$true}
        } catch {
            $giRow.passed=$false;$giRow.error=$_.Exception.Message;$giFailed=$true
        } finally {
            if ($giHeld) {$giGate.ReleaseMutex();$giRow.gate_released_utc=[DateTime]::UtcNow.ToString('o')}
            $giRow.finished_utc=[DateTime]::UtcNow.ToString('o')
            $giResults += $giRow
            $giResults | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $giOutput 'results.json') -Encoding utf8
            Write-Output "$Preset $giName passed=$($giRow.passed)"
        }
    }
} finally {$giGate.Dispose();Write-Output ('All validation gates released/disposed '+[DateTime]::UtcNow.ToString('o'))}
if ($giFailed) {exit 1}
