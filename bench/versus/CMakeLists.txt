# C against Tide: each concept written both ways, as fast as each language
# allows, and timed on the same work, on one thread and on every core (see
# versus.h and AGENTS.md, Benchmarks). `versus_<concept> --help` lists a run's
# options. Tests run each one --quick, and fail when the ways end differently.
add_library(tide_versus STATIC versus.c)
target_include_directories(tide_versus PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(tide_versus PUBLIC tide tide_platform) # tide_platform_jobs
# --save adds a run's results to bench/results/versus.jsonl.
target_compile_definitions(tide_versus PRIVATE TIDE_BENCH_RESULTS="${CMAKE_CURRENT_SOURCE_DIR}/../results")

set(versus_concepts "")

# versus_<concept>: the game in the .tide files given, and <concept>/main.c,
# which runs it beside the C ways in <concept>/<concept>.c.
function(versus_add concept)
    tide_add_game(versus_${concept} SOURCES ${ARGN} NAME ${concept} HOST ${concept}/main.c ${concept}/${concept}.c)
    target_link_libraries(versus_${concept} PRIVATE tide_versus)
    add_test(NAME versus_${concept} COMMAND versus_${concept} --quick)
    set(versus_concepts ${versus_concepts} versus_${concept} PARENT_SCOPE)
endfunction()

versus_add(particles particles/particles.tide)
versus_add(wide wide/wide.tide) # A dozen arrays of one component at once
add_test(NAME versus_wide_kept COMMAND versus_wide --quick --kept 1 --tagged 1 --frozen 50)
versus_add(sand ../sand/sand.tide) # The game itself
versus_add(life life/life.tide)
versus_add(churn churn/churn.tide)
versus_add(events events/events.tide)
versus_add(nbody nbody/nbody.tide)
versus_add(buckets buckets/buckets.tide) # A list of lists against one flat list, and C

# Opt-in diagnostics instrument a copy of generated C, leaving shipped code,
# runtime libraries and normal versus executables untouched. A control uses
# the same harness without any phase clocks, to expose measurement overhead.
if(TIDE_NODE)
    foreach(concept IN ITEMS life events)
        set(game_dir "${CMAKE_CURRENT_BINARY_DIR}/tide/versus_${concept}")
        set(instrumented "${game_dir}/${concept}-phases.c")
        add_custom_command(OUTPUT "${instrumented}"
            COMMAND "${TIDE_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/phases/instrument.mjs"
                "${concept}" "${game_dir}/${concept}.c" "${instrumented}"
            DEPENDS "${game_dir}/${concept}.c" phases/instrument.mjs phases/life.mjs phases/events.mjs
            VERBATIM)
        foreach(mode IN ITEMS phases control)
            if(mode STREQUAL "phases")
                set(source "${instrumented}")
                set(instrument 1)
            else()
                set(source "${game_dir}/${concept}.c")
                set(instrument 0)
            endif()
            add_executable(versus_${concept}_${mode} EXCLUDE_FROM_ALL
                "${concept}/main.c" "${concept}/${concept}.c" phases/phases.c
                "${source}" "${game_dir}/${concept}.h")
            target_include_directories(versus_${concept}_${mode} PRIVATE "${game_dir}" phases)
            target_compile_definitions(versus_${concept}_${mode} PRIVATE
                versus_main=versus_phases_main TIDE_PHASE_INSTRUMENTED=${instrument})
            target_link_libraries(versus_${concept}_${mode} PRIVATE tide_versus)
        endforeach()
    endforeach()
endif()

# Every one in turn, in full: cmake --build --preset release --target versus
if(NOT TIDE_WEB)
    set(commands "")
    foreach(program IN LISTS versus_concepts)
        list(APPEND commands COMMAND ${program})
    endforeach()
    add_custom_target(versus ${commands} DEPENDS ${versus_concepts} USES_TERMINAL VERBATIM)
endif()
