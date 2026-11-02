$env:TIDE_HIDDEN='1'
$ErrorActionPreference='Stop'
$PSNativeCommandUseErrorActionPreference=$false
$giGateText=Get-Content bench/results/perf-pass/gate.ps1 -Raw
$giGateName=[regex]::Match($giGateText,"'(Local[^']+Benchmark)'").Groups[1].Value
if(!$giGateName){throw 'Missing shared mutex.'}
$giReceipt=[ordered]@{validation='original WebGL regional CTest, original120 guard';gate_name=$giGateName;started_utc=[DateTime]::UtcNow.ToString('o');wasm_sha256=(Get-FileHash build/web-debug/bin/tide_globalillumination_pixels.wasm).Hash}
$giGate=[Threading.Mutex]::new($false,$giGateName)
$giHeld=$false
try {
 try{$giHeld=$giGate.WaitOne([TimeSpan]::FromSeconds(60))} catch [Threading.AbandonedMutexException]{$giHeld=$true}
 if(!$giHeld){throw 'Shared gate unavailable.'}
 $giReceipt.gate_acquired_utc=[DateTime]::UtcNow.ToString('o')
 & 'C:/Program Files/CMake/bin/ctest.exe' --preset web-debug -R '^tide_globalillumination_pixels_regional$' --output-on-failure --no-tests=error -j1 *> build/gi-quality-20261102/axial-depth-webgl-regional.log
 $giReceipt.ctest_exit=$LASTEXITCODE
 $giReceipt.validation_pass=$LASTEXITCODE -eq 0
}finally{
 if($giHeld){$giGate.ReleaseMutex();$giReceipt.gate_released_utc=[DateTime]::UtcNow.ToString('o')}
 $giGate.Dispose();$giReceipt.gate_disposed=$true
 $giReceipt.finished_utc=[DateTime]::UtcNow.ToString('o')
 $giReceipt|ConvertTo-Json -Depth 6|Set-Content build/gi-quality-20261102/axial-depth-webgl-regional-result.json
}
$giReceipt|ConvertTo-Json -Depth 6
