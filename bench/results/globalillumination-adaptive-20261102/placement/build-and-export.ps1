$ErrorActionPreference = 'Stop'
$env:TIDE_HIDDEN = '1'
$giExportRoot = (& git -C $PSScriptRoot rev-parse --show-toplevel).Trim()
$giExportSource = (& git -C $giExportRoot rev-parse HEAD).Trim()
if ($giExportSource -ne 'b1c69748f47a800269fae11fbf7d4f3370df7c8c') { throw 'This evidence host is pinned to b1c69748' }
$giExportTracked = @(& git -C $giExportRoot status --porcelain --untracked-files=no)
if ($giExportTracked.Count) { throw 'Tracked source changed' }
$giExportBuild = Join-Path $giExportRoot 'build/release'
$giExportCompiler = 'C:/Program Files/LLVM/bin/clang.exe'
$giExportHost = Join-Path $PSScriptRoot 'probe_export.c'
$giExportObject = Join-Path $PSScriptRoot 'probe_export.c.obj'
$giExportProgram = Join-Path $PSScriptRoot 'bench_gi_probe_exporter.exe'
$giExportFlags = @('-O2','-DNDEBUG','-std=c17','-D_DLL','-D_MT','-D_CRT_SECURE_NO_WARNINGS',
    '-Xclang','--dependent-lib=msvcrt','-g','-Xclang','-gcodeview',
    '-Wall','-Wextra','-Wpedantic','-Werror','-fno-fast-math','-ffp-contract=off','-ffunction-sections','-fdata-sections',
    "-I$giExportBuild/bench/tide/bench_globalillumination","-I$giExportRoot/engine/include","-I$giExportRoot/platform/include")
$giExportInputs = @(
    "$giExportBuild/bench/CMakeFiles/bench_globalillumination.dir/tide/bench_globalillumination/game.c.obj",
    "$giExportBuild/bench/CMakeFiles/bench_globalillumination.dir/tide/bench_globalillumination/game_pack.c.obj",
    "$giExportBuild/engine/tide.lib","$giExportBuild/tide_font_bold.lib","$giExportBuild/platform/tide_platform.lib",
    "$giExportBuild/packages/tide_globalillumination.lib","$giExportBuild/packages/tide_render3d.lib")
$giExportCompile = @($giExportFlags) + @('-c',$giExportHost,'-o',$giExportObject)
& $giExportCompiler @giExportCompile
if ($LASTEXITCODE -ne 0) { throw 'Scratch placement host compilation failed' }
$giExportLink = @('-O2','-DNDEBUG','-D_DLL','-D_MT','-Xclang','--dependent-lib=msvcrt','-g','-Xclang','-gcodeview',
    $giExportObject) + $giExportInputs + @('-lws2_32.lib','-lsecur32.lib','-lole32.lib','-lkernel32','-luser32','-lgdi32',
    '-lwinspool','-lshell32','-lole32','-loleaut32','-luuid','-lcomdlg32','-ladvapi32','-loldnames',
    '-Xlinker','/subsystem:console','-fuse-ld=lld-link','-o',$giExportProgram)
& $giExportCompiler @giExportLink
if ($LASTEXITCODE -ne 0) { throw 'Scratch placement host linking failed' }
$giExportMetadata = [ordered]@{
    source_sha = $giExportSource; method = 'CPU-only production tide_frame recording; no platform/window/GPU submission'
    compiler = (& $giExportCompiler --version | Out-String).Trim()
    compiler_path = $giExportCompiler; compile_arguments = $giExportCompile; link_arguments = $giExportLink
    executable_sha256 = (Get-FileHash -LiteralPath $giExportProgram -Algorithm SHA256).Hash.ToLowerInvariant()
    host_sha256 = (Get-FileHash -LiteralPath $giExportHost -Algorithm SHA256).Hash.ToLowerInvariant()
    generated_header_sha256 = (Get-FileHash -LiteralPath "$giExportBuild/bench/tide/bench_globalillumination/game.h" -Algorithm SHA256).Hash.ToLowerInvariant()
    inputs = @($giExportInputs | ForEach-Object { [ordered]@{ path = $_; sha256 = (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant() } })
    started_utc = [DateTimeOffset]::UtcNow.ToString('o')
}
$giExportMetadata | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'build.json') -Encoding utf8NoBOM
$giExportCases = @(
    [ordered]@{ name = 'room'; options = @('--scene','room'); instances = 7; view_distance = 48 },
    [ordered]@{ name = 'district64-near'; options = @('--scene','district','--buildings','64'); instances = 448; view_distance = 48 },
    [ordered]@{ name = 'district64-overview'; options = @('--scene','district','--buildings','64','--overview'); instances = 448; view_distance = 400 })
$giExportRows = @()
foreach ($giExportCase in $giExportCases) {
    $giExportJson = Join-Path $PSScriptRoot "$($giExportCase.name).json"
    $giExportArguments = @($giExportJson,'--quick','--mode','update4','--frames','160') + $giExportCase.options
    & $giExportProgram @giExportArguments | Tee-Object -FilePath (Join-Path $PSScriptRoot "$($giExportCase.name).log")
    if ($LASTEXITCODE -ne 0) { throw "Placement export failed: $($giExportCase.name)" }
    $giExportSnapshot = Get-Content -LiteralPath $giExportJson -Raw | ConvertFrom-Json
    if ($giExportSnapshot.scene_instances -ne $giExportCase.instances -or $giExportSnapshot.view_distance -ne $giExportCase.view_distance -or
        $giExportSnapshot.probes.Count -ne $giExportSnapshot.active_probes -or $giExportSnapshot.blocks.Count -ne $giExportSnapshot.active_blocks) {
        throw "Placement snapshot accounting mismatch: $($giExportCase.name)"
    }
    $giExportRows += [ordered]@{ name = $giExportCase.name; arguments = $giExportArguments;
        json_sha256 = (Get-FileHash -LiteralPath $giExportJson -Algorithm SHA256).Hash.ToLowerInvariant()
        scene_instances = $giExportSnapshot.scene_instances; probes = $giExportSnapshot.active_probes;
        blocks = $giExportSnapshot.active_blocks; view_distance = $giExportSnapshot.view_distance }
}
$giExportTracked = @(& git -C $giExportRoot status --porcelain --untracked-files=no)
if ((& git -C $giExportRoot rev-parse HEAD).Trim() -ne $giExportSource -or $giExportTracked.Count) { throw 'Source changed during evidence export' }
$giExportMetadata['ended_utc'] = [DateTimeOffset]::UtcNow.ToString('o')
$giExportMetadata['snapshots'] = $giExportRows
$giExportMetadata | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'evidence.json') -Encoding utf8NoBOM
