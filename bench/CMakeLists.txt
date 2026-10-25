# Benchmarks: games that stress the engine, played by bots on one machine.
# `bench_<name> --help` lists a run's options.
# Tests run each one --quick, and fail when a world goes wrong, a bot can't
# join, or the match ends with another hash than every platform agrees on.
add_library(tide_bench STATIC bench.c)
target_include_directories(tide_bench PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(tide_bench PUBLIC tide tide_platform) # tide_platform_jobs
# --save adds a run's results to results/<name>.jsonl, in the repo, to compare
# with later (node bench/compare.mjs).
target_compile_definitions(tide_bench PRIVATE TIDE_BENCH_RESULTS="${CMAKE_CURRENT_SOURCE_DIR}/results")

tide_add_game(bench_sand SOURCES sand/sand.tide NAME sand HOST sand/main.c)
target_link_libraries(bench_sand PRIVATE tide_bench)
add_test(NAME bench_sand COMMAND bench_sand --quick --expect 0x6ac505971b099ca5)
# Sand to play, with its views and the mouse: `sand` in a window, sand.html on
# the web, which the docs site shows (docs/guide/sand.md).
tide_add_game(sand SOURCES sand/sand.tide sand/play.tide TITLE "Tide sand" STATS)

tide_add_game(bench_gallery SOURCES gallery/gallery.tide NAME gallery HOST gallery/main.c)
target_link_libraries(bench_gallery PRIVATE tide_bench)
add_test(NAME bench_gallery COMMAND bench_gallery --quick --expect 0xe8641c8f2826ff50)

# The home page's planet (examples/earth), its match as it is but for taking
# up to 256 players rather than the page's 64, to see how far it goes. Its
# views come too, as the input's Sample reads one of their singletons; bots
# draw nothing.
set(earth_dir "${PROJECT_SOURCE_DIR}/examples/earth")
file(READ "${earth_dir}/game.tide" earth_game)
string(REPLACE "maxPlayers = 64;" "maxPlayers = 256;" bench_earth_game "${earth_game}")
if(bench_earth_game STREQUAL earth_game)
    message(FATAL_ERROR "bench/CMakeLists.txt: examples/earth/game.tide no longer says `maxPlayers = 64;`; say what to change here")
endif()
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/earth/game.tide" CONTENT "${bench_earth_game}" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${earth_dir}/game.tide")
tide_add_game(bench_earth NAME game HOST earth/main.c
    SOURCES "${CMAKE_CURRENT_BINARY_DIR}/earth/game.tide" "${earth_dir}/city.tide" "${earth_dir}/layout.tide"
            "${earth_dir}/globe.tide" "${earth_dir}/shapes.tide" "${earth_dir}/show.tide")
target_link_libraries(bench_earth PRIVATE tide_bench)
add_test(NAME bench_earth COMMAND bench_earth --quick --expect 0x642d3bbe3c5fb58c)
# A town hop changes shard and what the machine sees at once: corrections put it up to two seconds behind, as
# the engine's visibility tests allow
add_test(NAME bench_earth_split COMMAND bench_earth --quick --split 2 --lag 120 --expect 0xdb65d449fe39c077)
# Comparing one bot's world with what the server says it is, tick by tick
add_test(NAME bench_earth_diverge COMMAND bench_earth --quick --diverge 1 --expect 0x642d3bbe3c5fb58c)

# Page hashes: the engine's beside candidates for its place, timed on the
# shapes a world's hash reads (`bench_hash`, natively and under Node); the test
# checks each SIMD way gives its plain definition's bits.
add_executable(bench_hash hash/hash.c)
target_link_libraries(bench_hash PRIVATE tide)
add_test(NAME bench_hash COMMAND bench_hash --quick)

# Presentation correction costs, without changing Transform's defaults.
add_subdirectory(corrections)

tide_add_game(bench_workers SOURCES workers/game.tide NAME game HOST workers/main.c)
target_link_libraries(bench_workers PRIVATE tide_platform)
add_test(NAME bench_workers COMMAND bench_workers --quick)
add_test(NAME bench_workers_fallback COMMAND bench_workers --quick --fallback --pending 1)
add_test(NAME bench_workers_simulation COMMAND bench_workers --quick --simulate)

# C against Tide: concepts written both ways, as fast as each language allows,
# timed on the same work (see versus/versus.h).
add_subdirectory(versus)

# A generated Tide view exercising CPU pose preparation and GPU deformation.
# Timings are measured explicitly; CTest checks that the workloads draw.
if(NOT TIDE_ANDROID)
    tide_add_game(bench_skinned SOURCES skinned/game.tide NAME game HOST skinned/main.c)
    target_link_libraries(bench_skinned PRIVATE tide_platform)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/skinned/model.gltf")
    file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/skinned/model.gltf" skinned_model_sha256)
    target_compile_definitions(bench_skinned PRIVATE
        TIDE_BENCH_RESULTS="${CMAKE_CURRENT_SOURCE_DIR}/results"
        TIDE_SKINNED_VERTICES=3072 TIDE_SKINNED_TRIANGLES=6048
        TIDE_SKINNED_BONES=64 TIDE_SKINNED_INFLUENCES=4
        TIDE_SKINNED_MODEL_SHA256="${skinned_model_sha256}")
    if(TIDE_WEB)
        tide_web_page(bench_skinned SHELL "${PROJECT_SOURCE_DIR}/platform/web/test_shell.html")
        tide_add_web_test(bench_skinned TARGET bench_skinned
            QUERY "counts=1,10&frames=2&warmup=1&repeat=1" WEBGPU EXPECT "renderer: WebGPU")
        tide_add_web_test(bench_skinned_gpu_clip TARGET bench_skinned
            QUERY "counts=10&frames=2&warmup=1&repeat=1&modes=gpu&verify-gpu&subframe=0.5" WEBGPU EXPECT "GPU clip reference:")
    else()
        add_test(NAME bench_skinned COMMAND bench_skinned --counts 1,10 --frames 2 --warmup 1 --repeat 1)
        tide_gpu_test(bench_skinned)
        add_test(NAME bench_skinned_gpu_clip COMMAND bench_skinned --counts 10 --modes gpu --frames 2 --warmup 1 --repeat 1 --verify-gpu --subframe 0.5)
        tide_gpu_test(bench_skinned_gpu_clip)
    endif()
endif()

# Static Render3D PBR and equivalent cached public-C direct submission.
# All referenced sources exist before this registration is added; no common
# compiler/runtime/platform build settings change for this benchmark.
if(NOT TIDE_ANDROID)
    tide_add_game(bench_render3d NAME game SOURCES render3d/game.tide
        PACKAGES Render3D "${PROJECT_SOURCE_DIR}/packages/render3d/pbr.tide"
                          "${PROJECT_SOURCE_DIR}/packages/render3d/render3d.tide"
        HOST render3d/main.c render3d/direct.c)
    target_link_libraries(bench_render3d PRIVATE tide_platform)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/render3d/mesh.obj")
    file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/render3d/mesh.obj" render3d_mesh_sha256)
    target_compile_definitions(bench_render3d PRIVATE
        TIDE_BENCH_RESULTS="${CMAKE_CURRENT_SOURCE_DIR}/results"
        TIDE_RENDER3D_MESH_SHA256="${render3d_mesh_sha256}")
    if(TIDE_WEB)
        tide_web_page(bench_render3d SHELL "${PROJECT_SOURCE_DIR}/platform/web/test_shell.html")
        tide_add_web_test(bench_render3d TARGET bench_render3d QUERY "quick" WEBGPU EXPECT "Render3D benchmark correctness passed")
    else()
        add_test(NAME bench_render3d COMMAND bench_render3d --quick)
        add_test(NAME bench_render3d_opengl COMMAND bench_render3d --quick)
        tide_gpu_test(bench_render3d)
        tide_gpu_test(bench_render3d_opengl OPENGL)
    endif()
endif()

# Tide's drawing benchmark's workloads by hand in OpenGL and Vulkan, timed the same way
if(NOT TIDE_WEB AND NOT TIDE_ANDROID)
    add_subdirectory(draw)
endif()

# Sumo's real-time native delay measurement; invoked explicitly, never a
# timing assertion in CTest. Its machines share a pool capped at four threads.
if(NOT TIDE_WEB AND NOT TIDE_ANDROID)
    set(lag_sumo_sources "")
    foreach(file arena effects game hud light play)
        list(APPEND lag_sumo_sources "${PROJECT_SOURCE_DIR}/examples/sumo/${file}.tide")
    endforeach()
    tide_add_game(bench_lag_native SOURCES ${lag_sumo_sources} "${PROJECT_SOURCE_DIR}/packages/box3d/bridge.c" NAME game
        PACKAGES Box3D "${PROJECT_SOURCE_DIR}/packages/box3d/box3d.tide" Rooms "${PROJECT_SOURCE_DIR}/packages/rooms/rooms.tide"
        HOST lag_native.c lag_wait.c)
    target_sources(bench_lag_native PRIVATE "${PROJECT_SOURCE_DIR}/platform/src/jobs.c")
    target_compile_definitions(bench_lag_native PRIVATE MOST_THREADS=4)
    target_link_libraries(bench_lag_native PRIVATE tide_platform tide_box3d)
endif()

if(TIDE_NODE)
    add_test(NAME bench_compile_validation COMMAND "${TIDE_NODE}" --test "${CMAKE_CURRENT_SOURCE_DIR}/compile.test.mjs")
    if(NOT TIDE_WEB AND NOT TIDE_ANDROID)
        add_test(NAME bench_harness COMMAND "${TIDE_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/harness.mjs"
            $<TARGET_FILE:bench_sand> $<TARGET_FILE:versus_events>)
    endif()
endif()

# A sampling profiler for Windows that needs no administrator rights:
# `tide_sample -- build/release/bin/versus_life.exe --threads 1` prints where a
# program's own code spent its samples, by function and line (through
# llvm-symbolizer and the program's PDB: profile release builds).
if(WIN32 AND NOT TIDE_WEB AND NOT TIDE_ANDROID)
    add_executable(tide_sample EXCLUDE_FROM_ALL profile/sample.c)
    target_link_libraries(tide_sample PRIVATE psapi)
    target_compile_definitions(tide_sample PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
