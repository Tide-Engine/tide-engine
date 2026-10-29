$env:TIDE_HIDDEN='1'
$ErrorActionPreference='Stop'
$dir=$PSScriptRoot
$out=Join-Path $dir 'measurements'
if(Test-Path -LiteralPath $out) { throw 'Evidence already exists' }
New-Item -ItemType Directory -Path $out | Out-Null
function Busy {
    @(Get-Process -ErrorAction SilentlyContinue | Where-Object {
        $_.ProcessName -in @('ninja','clang','clang++','clang-cl','lld-link','link','cmake','ctest','cargo','rustc','tide_tests','tidec') -or
        $_.ProcessName -match '^(bench_|versus_|tidec_|tide_platform_|tide-bevy-bench)'
    } | ForEach-Object { "$($_.ProcessName):$($_.Id)" })
}
$mutex=[Threading.Mutex]::new($false,'Local\TideClaudeContinuationBenchmark')
$held=$false
$records=@()
try {
    try { $held=$mutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $held=$true }
    if(!$held) { throw 'Another Tide benchmark owns the timing mutex' }
    $quiet=[Diagnostics.Stopwatch]::StartNew()
    while($quiet.Elapsed.TotalSeconds -lt 15) {
        if(@(Busy).Count) { throw 'Concurrent build/test work: retry when quiet' }
        Start-Sleep -Milliseconds 250
    }
    $exe=Join-Path $dir 'check.exe'
    $sha=(Get-FileHash $exe -Algorithm SHA256).Hash.ToLowerInvariant()
    $cases=@(@(1000000,0),@(1000000,50),@(1000000,95),@(1000000,99),@(1000000,100),@(17,95),@(4099,95))
    for($round=0;$round -lt 3;$round++) {
        foreach($case in $cases) { foreach($hash in 0,1) { foreach($snapshot in 0,1) {
            $order=if($round%2) { @(1,0) } else { @(0,1) }
            foreach($packed in $order) {
                if(@(Busy).Count) { throw 'Concurrent work detected before measurement' }
                $name="r$round-n$($case[0])-f$($case[1])-h$hash-s$snapshot-p$packed"
                $argsList=@("$($case[0])","$($case[1])","$packed","$hash","$snapshot")
                $began=[DateTime]::UtcNow.ToString('o')
                $p=Start-Process -FilePath $exe -ArgumentList $argsList -WindowStyle Hidden -PassThru -RedirectStandardOutput "$out/$name.json" -RedirectStandardError "$out/$name.err"
                $contention=@()
                while(!$p.WaitForExit(250)) { $contention+=@(Busy) }
                $p.Refresh()
                if($p.ExitCode -ne 0) { throw "Process failed $name" }
                $records+=@{name=$name; round=$round; args=$argsList; began=$began; ended=[DateTime]::UtcNow.ToString('o'); contention=$contention; sha256=$sha}
                $records | ConvertTo-Json -Depth 8 | Set-Content "$out/processes.json"
                if($contention.Count) { throw "Concurrent work detected during $name" }
            }
        } } }
        Write-Output "Round $($round+1)/3 complete"
    }
} finally { if($held) { $mutex.ReleaseMutex() }; $mutex.Dispose() }
