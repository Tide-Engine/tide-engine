# Native Windows fixed-work comparison. Requires optimized native engine libraries.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$BaselineCompiler,
    [Parameter(Mandatory)][string]$CandidateCompiler,
    [Parameter(Mandatory)][string]$RuntimeInclude,
    [Parameter(Mandatory)][string]$RuntimeLibrary,
    [string]$Clang = 'clang',
    [string]$Output,
    [ValidateRange(1,10000)][int]$Ticks = 2000,
    [switch]$Unsplit
)
$env:TIDE_HIDDEN = '1'
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (!$Output) { $Output = Join-Path $repo ('build/canonical-query-cpu-' + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
$Output = [IO.Path]::GetFullPath($Output)
if (Test-Path $Output) { throw "Choose a new output directory to preserve earlier artifacts: $Output" }
New-Item -ItemType Directory $Output | Out-Null
$BaselineCompiler = (Resolve-Path $BaselineCompiler).Path
$CandidateCompiler = (Resolve-Path $CandidateCompiler).Path
$RuntimeInclude = (Resolve-Path $RuntimeInclude).Path
$RuntimeLibrary = (Resolve-Path $RuntimeLibrary).Path
$Clang = (Get-Command $Clang).Source
$fixture = Join-Path $Output 'query_narrow.tide'
Copy-Item (Join-Path $repo 'compiler/tests/e2e/query_narrow.tide') $fixture
$main = Join-Path $PSScriptRoot 'canonical-query-cpu/main.c'
$clock = Join-Path $PSScriptRoot 'canonical-query-cpu/clock.c'

function Invoke-Checked([string]$Program, [string[]]$Arguments, [string]$Log) {
    & $Program @Arguments *> $Log
    if ($LASTEXITCODE -ne 0) { Get-Content $Log; throw "$Program exited $LASTEXITCODE" }
}
function Hash([string]$Path) { (Get-FileHash -Algorithm SHA256 $Path).Hash.ToLowerInvariant() }

$manifest = [ordered]@{
    schema = 'tide.canonical-query-cpu.v1'
    mode = $(if ($Unsplit) { 'unsplit control' } else { 'split-code ThinLTO' })
    clang = (& $Clang --version | Select-Object -First 1)
    common_compile_flags = @('-std=c17','-O2','-g','-Wall','-Wextra','-Wpedantic','-Werror',
        '-fno-fast-math','-ffp-contract=off','-D_CRT_SECURE_NO_WARNINGS')
    split_flags = $(if ($Unsplit) { @() } else { @('-flto=thin','-fuse-ld=lld') })
    fixture_sha256 = (Hash $fixture)
    main_sha256 = (Hash $main)
    clock_sha256 = (Hash $clock)
    runtime_library_sha256 = (Hash $RuntimeLibrary)
    warmup_ticks = 20
    measured_ticks = $Ticks
    variants = @()
}
foreach ($name in @('baseline','candidate')) {
    $compiler = $(if ($name -eq 'baseline') { $BaselineCompiler } else { $CandidateCompiler })
    $dir = Join-Path $Output $name
    New-Item -ItemType Directory $dir | Out-Null
    $generate = @($fixture, '-o', $dir, '--name', 'game', '--no-line')
    if (!$Unsplit) { $generate += '--split-code' }
    Invoke-Checked $compiler $generate (Join-Path $dir 'generate.log')
    # EntitiesOnly intentionally binds a component without consuming its value.
    $sources = @(Join-Path $dir 'game.c')
    if (!$Unsplit) {
        $sources = @(Get-Content (Join-Path $dir 'game_parts.txt') | ForEach-Object {
            Join-Path $dir (($_ -split ' ')[2])
        })
    }
    $exe = Join-Path $dir 'query.exe'
    $flags = @('-std=c17','-O2','-g','-Wall','-Wextra','-Wpedantic','-Werror',
        '-fno-fast-math','-ffp-contract=off','-D_CRT_SECURE_NO_WARNINGS',"-I$dir","-I$RuntimeInclude")
    if (!$Unsplit) { $flags += @('-flto=thin','-fuse-ld=lld') }
    Invoke-Checked $Clang ($flags + @($main,$clock) + $sources + @($RuntimeLibrary,'-o',$exe)) (Join-Path $dir 'compile.log')
    $manifest.variants += [ordered]@{
        name = $name
        compiler_sha256 = (Hash $compiler)
        executable_sha256 = (Hash $exe)
        generated = @($sources | ForEach-Object { @{file=[IO.Path]::GetFileName($_);sha256=(Hash $_)} })
    }
}
$manifest | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $Output 'manifest.json')
Write-Host 'Run with competing builds/tests stopped. Owner processes are left untouched.'
$results = @()
foreach ($name in @('baseline','candidate','candidate','baseline','candidate','baseline','baseline','candidate')) {
    $raw = & (Join-Path $Output "$name/query.exe") $Ticks
    if ($LASTEXITCODE -ne 0) { throw "$name exited $LASTEXITCODE" }
    $result = $raw | ConvertFrom-Json
    $result | Add-Member -NotePropertyName variant -NotePropertyValue $name
    $results += $result
    $results | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $Output 'results.json')
    Write-Host "$name $raw"
}
foreach ($field in @('initial_hash','final_hash','sequence_hash','sum','ticks','warmup')) {
    if (@($results | ForEach-Object { $_.$field } | Select-Object -Unique).Count -ne 1) {
        throw "Equivalent-work check failed: $field differs"
    }
}
$results | Group-Object variant | ForEach-Object {
    [pscustomobject]@{
        variant = $_.Name
        mean_wall_seconds = ($_.Group.wall_seconds | Measure-Object -Average).Average
        mean_thread_cpu_seconds = ($_.Group.thread_cpu_seconds | Measure-Object -Average).Average
        mean_thread_cycles = ($_.Group.thread_cycles | Measure-Object -Average).Average
    }
} | ConvertTo-Json | Set-Content (Join-Path $Output 'summary.json')
Write-Host "Equivalent-work checks passed; artifacts: $Output"
