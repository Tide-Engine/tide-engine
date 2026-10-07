# Benchmarks: games that stress the engine, played by bots on one machine
# (see AGENTS.md, Benchmarks). `bench_<name> --help` lists a run's options.
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
add_test(NAME bench_sand COMMAND bench_sand --quick --expect 0xe0e885af116662f5)
# Sand to play, with its views and the mouse: `sand` in a window, sand.html on
# the web, which the docs site shows (docs/guide/sand.md).
tide_add_game(sand SOURCES sand/sand.tide sand/play.tide TITLE "Tide sand" STATS)

tide_add_game(bench_gallery SOURCES gallery/gallery.tide NAME gallery HOST gallery/main.c)
target_link_libraries(bench_gallery PRIVATE tide_bench)
add_test(NAME bench_gallery COMMAND bench_gallery --quick --expect 0xaa539cfdc1935346)

# C against Tide: concepts written both ways, as fast as each language allows,
# timed on the same work (see versus/versus.h).
add_subdirectory(versus)

# Sumo's real-time native delay measurement; invoked explicitly, never a
# timing assertion in CTest. Its machines share a pool capped at four threads.
if(NOT TIDE_WEB AND NOT TIDE_ANDROID)
    set(lag_sumo_sources "")
    foreach(file arena effects game hud light lobby play)
        list(APPEND lag_sumo_sources "${PROJECT_SOURCE_DIR}/examples/sumo/${file}.tide")
    endforeach()
    tide_add_game(bench_lag_native SOURCES ${lag_sumo_sources} "${PROJECT_SOURCE_DIR}/packages/box3d/bridge.c" NAME game
        PACKAGES Box3D "${PROJECT_SOURCE_DIR}/packages/box3d/box3d.tide" HOST lag_native.c lag_wait.c)
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
