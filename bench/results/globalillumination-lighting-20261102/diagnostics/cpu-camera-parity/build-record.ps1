$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$env:TIDE_HIDDEN = '1'
$giDiagnosticRoot = (& git -C $PSScriptRoot rev-parse --show-toplevel).Trim()
$giDiagnosticDirectory = $PSScriptRoot
$giDiagnosticSource = Join-Path $giDiagnosticDirectory 'record_room.c'
$giDiagnosticRows = @()

function SplitArguments([string]$giArgumentText) {
    return @([regex]::Matches($giArgumentText, '"([^"\\]*(?:\\.[^"\\]*)*)"|([^\s]+)') | ForEach-Object {
        if ($_.Groups[1].Success) { $_.Groups[1].Value } else { $_.Groups[2].Value }
    })
}
function CacheValue([string]$giCacheText, [string]$giKey) {
    $giMatch = [regex]::Match($giCacheText, '(?m)^' + [regex]::Escape($giKey) + ':[^=]+=(.+)$')
    if (!$giMatch.Success) { throw "Missing $giKey in CMake cache" }
    return $giMatch.Groups[1].Value.Trim()
}
function ObjectFlags([string]$giNinjaText) {
    $giMatch = [regex]::Match($giNinjaText, '(?ms)^build packages/CMakeFiles/tide_globalillumination_test\.dir/globalillumination/test/gi_test\.c\.obj:.*?\r?\n(?<properties>(?:  .*\r?\n)+)')
    if (!$giMatch.Success) { throw 'Missing exact fixture compile entry' }
    $giValues = @()
    foreach ($giProperty in @('DEFINES','INCLUDES','FLAGS')) {
        $giLine = [regex]::Match($giMatch.Groups['properties'].Value, '(?m)^  ' + $giProperty + ' = (.*)$')
        if ($giLine.Success) { $giValues += SplitArguments $giLine.Groups[1].Value.Trim() }
    }
    return $giValues
}
Push-Location $giDiagnosticRoot
try {
    foreach ($giKind in @('native','wasi')) {
        $giPreset = if ($giKind -eq 'native') {'sanitize'} else {'web-debug'}
        $giBuild = Join-Path $giDiagnosticRoot "build/$giPreset"
        $giCacheText = Get-Content -LiteralPath (Join-Path $giBuild 'CMakeCache.txt') -Raw
        $giCompilerInfo = Get-ChildItem -LiteralPath (Join-Path $giBuild 'CMakeFiles') -Filter 'CMakeCCompiler.cmake' -Recurse | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
        $giCompiler = [regex]::Match((Get-Content -LiteralPath $giCompilerInfo.FullName -Raw), '(?m)^set\(CMAKE_C_COMPILER "([^"]+)"\)').Groups[1].Value
        if (!$giCompiler) { throw 'Missing configured compiler' }
        $giNinja = CacheValue $giCacheText 'CMAKE_MAKE_PROGRAM'
        $giReference = @(& $giNinja -C $giBuild -t commands tide_globalillumination_test)
        if ($LASTEXITCODE -ne 0) { throw 'Ninja command inventory failed' }
        $giReference | Set-Content -LiteralPath (Join-Path $giDiagnosticDirectory "reference-$giKind-commands.txt") -Encoding utf8NoBOM
        $giCompileFlags = ObjectFlags (Get-Content -LiteralPath (Join-Path $giBuild 'build.ninja') -Raw)
        $giTargetFlags = @()
        if ($giKind -eq 'wasi') {
            $giReferenceCompile = $giReference | Where-Object { $_ -match 'gi_test\.c\.obj.* -c ' } | Select-Object -First 1
            $giTargetMatch = [regex]::Match($giReferenceCompile, '(--target=[^\s]+)\s+(--sysroot=[^\s]+)')
            if (!$giTargetMatch.Success) { throw 'Missing exact WASI compiler target/sysroot' }
            $giTargetFlags = @($giTargetMatch.Groups[1].Value,$giTargetMatch.Groups[2].Value)
        }
        $giObject = Join-Path $giDiagnosticDirectory "record-room-$giKind.c.obj"
        $giProgram = Join-Path $giDiagnosticDirectory $(if ($giKind -eq 'native') {'cpu-room-record-native.exe'} else {'cpu-room-record-wasi.wasm'})
        $giCompileArguments = @($giTargetFlags) + @($giCompileFlags) + @('-c',$giDiagnosticSource,'-o',$giObject)
        & $giCompiler @giCompileArguments *> (Join-Path $giDiagnosticDirectory "compile-$giKind.log")
        if ($LASTEXITCODE -ne 0) { throw "Diagnostic $giKind helper compile failed" }
        $giInputs = @(
            "$giBuild/packages/CMakeFiles/tide_globalillumination_test.dir/tide/tide_globalillumination_test/game.c.obj",
            "$giBuild/packages/CMakeFiles/tide_globalillumination_test.dir/tide/tide_globalillumination_test/game_pack.c.obj")
        if ($giKind -eq 'native') {
            $giInputs += @("$giBuild/engine/tide.lib","$giBuild/tide_font_bold.lib","$giBuild/packages/tide_globalillumination.lib","$giBuild/packages/tide_render3d.lib")
            $giLinkArguments = @('-nostartfiles','-nostdlib','-O2','-DNDEBUG','-D_DLL','-D_MT','-Xclang','--dependent-lib=msvcrt','-g','-Xclang','-gcodeview',
                '-Xlinker','/subsystem:console','-Xlinker','/OPT:NOICF','-Xlinker','/STACK:8388608','-fsanitize=address','-fuse-ld=lld-link',$giObject) + $giInputs + @('-o',$giProgram,
                '-Xlinker','/MANIFEST:EMBED','-lkernel32','-luser32','-lgdi32','-lwinspool','-lshell32','-lole32','-loleaut32','-luuid','-lcomdlg32','-ladvapi32','-loldnames')
        } else {
            $giInputs += @("$giBuild/engine/libtide.a","$giBuild/libtide_font_bold.a","$giBuild/packages/libtide_globalillumination.a","$giBuild/packages/libtide_render3d.a","$giBuild/engine/libtide.a")
            $giBuiltins = [regex]::Match(($giReference | Where-Object { $_ -match ' -o bin\\tide_globalillumination_test\.wasm ' } | Select-Object -First 1), '"([^\"]+libclang_rt\.builtins-wasm32\.a)"').Groups[1].Value
            if (!$giBuiltins) { throw 'Missing exact configured WASI builtins library' }
            $giInputs += $giBuiltins
            $giLinkArguments = @($giTargetFlags) + @('-g','-nodefaultlibs','-Wl,-z,stack-size=1048576','-Wl,--import-memory','-Wl,--export-memory','-Wl,--shared-memory','-Wl,--max-memory=4294967296',
                $giObject) + @($giInputs | Select-Object -SkipLast 1) + @('-lc',$giBuiltins,'-o',$giProgram)
        }
        & $giCompiler @giLinkArguments *> (Join-Path $giDiagnosticDirectory "link-$giKind.log")
        if ($LASTEXITCODE -ne 0) { throw "Diagnostic $giKind helper link failed" }
        $giDiagnosticRows += [ordered]@{
            kind=$giKind; preset=$giPreset; compiler=$giCompiler; compile_arguments=$giCompileArguments; link_arguments=$giLinkArguments
            executable=$giProgram; executable_sha256=(Get-FileHash -LiteralPath $giProgram -Algorithm SHA256).Hash.ToLowerInvariant()
            header_sha256=(Get-FileHash -LiteralPath "$giBuild/packages/tide/tide_globalillumination_test/game.h" -Algorithm SHA256).Hash.ToLowerInvariant()
            generated_c_sha256=(Get-FileHash -LiteralPath "$giBuild/packages/tide/tide_globalillumination_test/game.c" -Algorithm SHA256).Hash.ToLowerInvariant()
            inputs=@($giInputs | ForEach-Object { [ordered]@{path=$_;sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant()} })
        }
    }
    Copy-Item -LiteralPath "$giDiagnosticRoot/build/sanitize/bin/clang_rt.asan_dynamic-x86_64.dll" -Destination $giDiagnosticDirectory
    [ordered]@{
        method='Exactly one tide_frame records commands per helper execution. No platform/window/audio/device or GPU submission. No helper execution performed by this build script.'
        source_sha=(& git rev-parse HEAD).Trim(); source_status=@(& git status --porcelain --untracked-files=no)
        host_sha256=(Get-FileHash -LiteralPath $giDiagnosticSource -Algorithm SHA256).Hash.ToLowerInvariant()
        compiled_utc=[DateTimeOffset]::UtcNow.ToString('o'); programs=$giDiagnosticRows
    } | ConvertTo-Json -Depth 15 | Set-Content -LiteralPath (Join-Path $giDiagnosticDirectory 'build-metadata.json') -Encoding utf8NoBOM
    Write-Output 'Prepared native ASan and WASI record-only helpers. Neither helper has been executed.'
} finally { Pop-Location }
