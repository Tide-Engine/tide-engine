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

# Page hashes: the engine's beside candidates for its place, timed on the
# shapes a world's hash reads (`bench_hash`, natively and under Node); the test
# checks each SIMD way gives its plain definition's bits.
add_executable(bench_hash hash/hash.c)
target_link_libraries(bench_hash PRIVATE tide)
add_test(NAME bench_hash COMMAND bench_hash --quick)

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
