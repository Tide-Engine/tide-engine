$env:TIDE_HIDDEN = '1'
$ErrorActionPreference = 'Stop'
$giRoot = (Get-Location).Path
$giWork = [IO.Path]::GetFullPath((Join-Path $giRoot 'build/gi-quality-20261102'))
$giProfile = [IO.Path]::GetFullPath((Join-Path $giWork 'minimal-capture-near-sweep-profile-one'))
if (!$giProfile.StartsWith($giWork.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Profile escaped work directory' }
if (Test-Path -LiteralPath $giProfile) { throw 'Minimal repro has already been run' }
$giArgs = @('-DNODE=C:/Program Files/nodejs/node.exe', '-DBROWSER=C:/Program Files/Google/Chrome/Application/chrome.exe',
    "-DPAGE=$giWork/minimal-capture-near-sweep.html", '-DANGLE_BACKEND=swiftshader', "-DPROFILE=$giProfile", '-P', "$giRoot/cmake/run_web_test.cmake")
& 'C:/Program Files/CMake/bin/cmake.exe' @giArgs *> (Join-Path $giWork 'minimal-capture-near-sweep.log')
if ($LASTEXITCODE -ne 0) { throw "Minimal repro exited $LASTEXITCODE" }
