# End-to-end tests: e2e/<name>.tide (or the SOURCES given) is transpiled and
# compiled together with e2e/<name>_test.c and the shared test harness, then run.
# WARNINGS are the ones tidec has to give, and PACKAGES the files of packages
# the game builds with, each after its package's name, as for tide_add_game.
function(tide_e2e_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;WARNINGS;PACKAGES" ${ARGN})
    set(sources "e2e/${name}.tide")
    if(ARG_SOURCES)
        set(sources ${ARG_SOURCES})
    endif()
    set(target tidec_e2e_${name})
    tide_add_game(${target} SOURCES ${sources} NAME game WARNINGS ${ARG_WARNINGS} PACKAGES ${ARG_PACKAGES}
        HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" "${CMAKE_CURRENT_SOURCE_DIR}/e2e/${name}_test.c")
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/tests")
    add_test(NAME ${target} COMMAND ${target})
endfunction()

tide_e2e_test(move_clamp)
tide_e2e_test(structural)
tide_e2e_test(reach)
tide_e2e_test(sparse)
tide_e2e_test(world) # Other entities' components through the World
tide_e2e_test(world_set_own) # ...and this entity's own, beside a parameter that reads it
tide_e2e_test(world_functions) # ...and through functions and methods the World is passed on to
tide_e2e_test(queries) # Read-only iteration over a separate entity set
if(TIDE_PLATFORM)
    target_link_libraries(tidec_e2e_queries PRIVATE tide_platform)
    target_compile_definitions(tidec_e2e_queries PRIVATE TIDE_QUERY_THREADS=1)
endif()
set_tests_properties(tidec_e2e_queries PROPERTIES TIMEOUT 120)
tide_e2e_test(world_local) # ...in views and local handlers
tide_e2e_test(debug)
tide_e2e_test(long)
tide_e2e_test(io)
tide_e2e_test(byte)
tide_e2e_test(maps)
tide_e2e_test(sort)
tide_e2e_test(edge_cases WARNINGS "'settings' is never used" "system 'NeverRuns' never runs")
tide_e2e_test(empty_world WARNINGS "system 'PlayerMovement' never runs")
tide_e2e_test(unused_locals WARNINGS "local 'fwd' is never used" "local 'spark' is never used"
    "local 'bumped' is never used" "local 'total' is never used") # Locals nothing reads: warned about, and run as written
tide_e2e_test(defaults)
tide_e2e_test(default_value)
tide_e2e_test(math)
tide_e2e_test(input)
tide_e2e_test(crossplatform)
tide_e2e_test(evaluation_order SOURCES e2e/evaluation_order.tide e2e/evaluation_order_c.c) # Left to right, whatever order C would pick
tide_e2e_test(random) # Random's state in the world, and the match's seed: the same bits on every platform
tide_e2e_test(views)
tide_e2e_test(shapes) # The shapes with smooth edges: each Draw2D call, the general Shape, and target receivers
tide_e2e_test(draw_settings) # Blend, tint, transform, clip and cameras: around blocks, listed with commas, and set
tide_e2e_test(stages) # Enum phases, local offsets and whole-phase ordering in every schedule
tide_e2e_test(render_targets) # Target receivers, explicit Texture sources and distributed rendering phases
tide_e2e_test(render_lifecycle) # Shared target copies, resizing and explicit disposal from local code
tide_e2e_test(ownership) # Views `with ownership` and `without ownership`: this machine's player's entities, and the others
tide_e2e_test(conditional)
tide_e2e_test(input_bounds)
tide_e2e_test(structs)
tide_e2e_test(methods)
tide_e2e_test(overloads) # Default values, named arguments, and functions, methods and systems that share a name
tide_e2e_test(overloads_extern SOURCES e2e/overloads_extern.tide e2e/overloads_extern_c.c) # ...and C functions, which have neither
# virtual and override: a package's overrides of the game's code, and the game's of the package's. What only
# replaced code spawned is in no archetype, so the system that needs it matches nothing
tide_e2e_test(overrides SOURCES e2e/overrides/game.tide PACKAGES Expansion e2e/overrides/Expansion/expansion.tide
    WARNINGS "system 'Haunt' never runs")
tide_e2e_test(singleton_params) # Explicit singleton values, caller capabilities and local-field sidecars
tide_e2e_test(operators)
tide_e2e_test(events)
tide_e2e_test(events_lookup) # Verified event target rows and fallback after structural changes
tide_e2e_test(events_batch) # Queue-page runs preserve recording order, filters, defaults and C observers
tide_e2e_test(commands)
add_test(NAME tidec_command_schema
    COMMAND "${CMAKE_COMMAND}" "-DTIDEC=${TIDEC_PATH}"
        "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/command_schema"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/command_schema.cmake")
tide_e2e_test(local)
# `local` fields of the match's components and singletons: this machine's, outside the match
tide_e2e_test(local_fields WARNINGS "'box' is declared mut but never written")
tide_e2e_test(local_fields_plain) # ...whose match is the same without them
tide_e2e_test(local_fields_net) # ...and what a client's wrong predictions do to them, on a network
tide_e2e_test(local_events) # Local handlers of the match's events: heard once, whatever a client predicted
tide_e2e_test(local_events_same) # Events only local code handles: the match they cost nothing,
tide_e2e_test(local_events_plain) # ...which is the same without them
tide_e2e_test(verified_views) # `verified` parameters: what views read of the world the server confirmed
tide_e2e_test(verified_heap) # ...its text, lists, maps, sets and grids, from its own heap
tide_e2e_test(view_queries) # Queries in views and local handlers: blended, `verified`, or as an event's tick left them
tide_e2e_test(enums)
tide_e2e_test(scenes)
tide_e2e_test(scenes_local)
tide_e2e_test(scenes_fallback)
tide_e2e_test(gui)
tide_e2e_test(actions)
tide_e2e_test(tasks)
tide_e2e_test(tasks_local)
tide_e2e_test(tasks_flow)
tide_e2e_test(loops)
tide_e2e_test(strings)
tide_e2e_test(show_values)
tide_e2e_test(text_fields)
tide_e2e_test(lists)
tide_e2e_test(lists_held) # Lists in structs, and what stands in for lists inside lists
tide_e2e_test(lists_big SOURCES e2e/lists_big.tide e2e/lists_big_c.c) # Lists in chunks, and C reading them
tide_e2e_test(lists_resize) # Resize: a list sized in one call, a few elements or chunks of them
tide_e2e_test(lists_out SOURCES e2e/lists_out.tide e2e/lists_out_c.c) # `out List<T>`: lists C makes
tide_e2e_test(out_params SOURCES e2e/out_params.tide e2e/out_params_c.c) # `out` parameters and arguments, as C#'s
tide_e2e_test(grids SOURCES e2e/grids.tide e2e/grids_c.c) # Grids
tide_e2e_test(parallel) # Parallel loops over grids and lists
tide_e2e_test(parallel_list_batch) # Independent output lanes preserve exact reductions, calls and snapshots
tide_e2e_test(window_indices) # Task-local windows with global positions, offsets and partial writes
tide_e2e_test(parallel_work) # Unknown nested loop work must still select the worker pool
# These share the same behavioral check, with a different nested-loop form.
foreach(variant IN ITEMS condition step block constant)
    set(target tidec_e2e_parallel_work_${variant})
    tide_add_game(${target} SOURCES e2e/parallel_work_${variant}.tide NAME game
        HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" "${CMAKE_CURRENT_SOURCE_DIR}/e2e/parallel_work_test.c")
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/tests")
    if(variant STREQUAL "constant")
        target_compile_definitions(${target} PRIVATE EXPECTED_ROUNDS=0u EXPECTED_TOTAL=45)
    endif()
    add_test(NAME ${target} COMMAND ${target})
endforeach()
tide_e2e_test(mesh SOURCES e2e/mesh.tide e2e/mesh_c.c) # Meshes, grids as textures, and C that draws
tide_e2e_test(mesh3d) # 3D meshes, their cameras, and instances
tide_e2e_test(draw_apis SOURCES e2e/draw_apis.tide e2e/draw_apis_c.c) # Separate drawing APIs and coordinates
tide_e2e_test(measure_text) # Draw2D.MeasureText: on the screen, under a camera and in a render target
tide_e2e_test(text) # Draw2D.Text at an anchor and in a weight, and measuring in one
tide_e2e_test(shaders) # frag and vert: what a mesh draw through them records
tide_e2e_test(instancing) # Instanced: lists of transforms and ordinary instance structs
tide_e2e_test(crossplatform_text)
tide_e2e_test(devices)
tide_e2e_test(pause)
tide_e2e_test(claims) # Widgets a view draws itself: what's typed, and claiming the pointer and the keyboard
tide_e2e_test(cursor) # Cursor.Lock, asked each frame, and Cursor.locked, which the window says
tide_e2e_test(frame_time) # Frame.dt: the frame's length a host gives, read by local code with a match and with none
tide_e2e_test(net)
tide_e2e_test(net_no_input)
tide_e2e_test(net_ended)
tide_e2e_test(net_join_big) # A world that takes longer to send than the ticks kept
tide_e2e_test(guess) # Other players' inputs, guessed: their last ones for a while, then let go, but for [Absolute] fields
tide_e2e_test(seen) # How late other players see what one does: from a key read to another's view
tide_e2e_test(random_net) # Generators through prediction and rollback, and the seed a server picks for its match
tide_e2e_test(blend)
tide_e2e_test(view_corrections) # Tagged presentation fields: correction accumulation, rotation and private lifetime
tide_e2e_test(big_world)
tide_e2e_test(crlf) # Windows line endings, kept by .gitattributes
tide_e2e_test(directives) # #if and the rest: what other versions of tide read is left out
tide_e2e_test(multi_file SOURCES e2e/multi_file/a_physics.tide e2e/multi_file/b_main.tide
    e2e/multi_file/c_combat.tide e2e/multi_file/d_items.tide)
# What a game declares itself comes before what a namespace it imports has by that name
tide_e2e_test(using_own SOURCES e2e/using_own/a_lib.tide e2e/using_own/a_other.tide e2e/using_own/b_game.tide
    e2e/using_own/c_inner.tide)
tide_e2e_test(extern SOURCES e2e/extern.tide e2e/extern_noise.tide e2e/extern_c.c) # C functions, from the game's C file
tide_e2e_test(extern_system SOURCES e2e/extern_system.tide e2e/extern_system_c.c) # Systems written in C, and their rows
if(TIDE_PLATFORM) # ...on the platform layer's threads, where there are some
    target_link_libraries(tidec_e2e_extern_system PRIVATE tide_platform)
    target_compile_definitions(tidec_e2e_extern_system PRIVATE TIDE_EXTERN_SYSTEM_THREADS=1)
endif()
tide_e2e_test(cpp SOURCES e2e/cpp.tide e2e/cpp_code.cpp) # ...and from its C++ file, with no C++ runtime
tide_e2e_test(fails SOURCES e2e/fails.tide e2e/fails_c.c) # Errors as values, and T?
tide_e2e_test(constants SOURCES e2e/constants/a_tuning.tide e2e/constants/b_main.tide)
tide_e2e_test(settings)
tide_e2e_test(profile SOURCES e2e/profile.tide e2e/profile_combat.tide) # What ticks and frames say to a host's profiler
tide_e2e_test(handover)
tide_e2e_test(players) # A match of 150 players, in a game whose maxPlayers is 200
tide_e2e_test(delta)

# An endless chain of events stops the program, naming the event: here past
# 1000 deep, so it stops quickly.
tide_add_game(tidec_e2e_chain_endless SOURCES e2e/chain_endless.tide NAME game
    HOST "${CMAKE_CURRENT_SOURCE_DIR}/e2e/chain_endless_main.c")
target_compile_definitions(tidec_e2e_chain_endless PRIVATE TIDE_MAX_CHAIN=1000u)
add_test(NAME tidec_e2e_chain_endless COMMAND tidec_e2e_chain_endless)
set_tests_properties(tidec_e2e_chain_endless PROPERTIES TIMEOUT 30
    PASS_REGULAR_EXPRESSION "an endless chain of events: P[io]ng is 1000 deep in one tick")

# Safety tests: each safety/<name>.tide is a game of Tide alone that tries to
# make the generated C or the engine do something undefined: an index anywhere,
# a handle to nothing, numbers at their limits, a comparison that lies. No Tide
# code can corrupt memory (see AGENTS.md, Language), so each ends in a defined
# way: it runs to the end having said what its first lines expect, or stops
# with the engine's own message (safety/expect_safety.cmake). They mean the
# most under sanitizers (-DTIDE_SANITIZE=ON), where a read or write out of
# bounds or an undefined operation is a report, and a report fails the test;
# without them they still pin what each case gives, the same on every platform.
# safety/host.c runs them as a session would: snapshots that share the world's
# pages, views between ticks, the world as bytes and back; on the platform
# layer's threads where there's one.
file(GLOB TIDE_SAFETY_TESTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/safety/*.tide")
string(REPLACE ";" "|" TIDE_SAFETY_EMULATOR "${CMAKE_CROSSCOMPILING_EMULATOR}")
foreach(source IN LISTS TIDE_SAFETY_TESTS)
    get_filename_component(name "${source}" NAME_WE)
    set(target tidec_safety_${name})
    tide_add_game(${target} SOURCES safety/${name}.tide NAME game
        HOST "${CMAKE_CURRENT_SOURCE_DIR}/safety/host.c")
    if(TIDE_PLATFORM)
        target_link_libraries(${target} PRIVATE tide_platform)
        target_compile_definitions(${target} PRIVATE TIDE_SAFETY_THREADS=1)
        if(NOT TIDE_WEB AND NOT TIDE_ANDROID)
            target_compile_definitions(${target} PRIVATE TIDE_SAFETY_RENDERER=1)
        endif()
    endif()
    add_test(NAME ${target}
        COMMAND "${CMAKE_COMMAND}"
            "-DRUN=$<TARGET_FILE:${target}>"
            "-DEMULATOR=${TIDE_SAFETY_EMULATOR}"
            "-DSOURCE=${source}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/safety/expect_safety.cmake")
    set_tests_properties(${target} PROPERTIES TIMEOUT 300 LABELS safety)
    # One that draws (`// draws` among its first lines) goes through the
    # renderer too, where tests have a window and a GPU for it (label `gpu`)
    file(STRINGS "${source}" draws LIMIT_COUNT 1 REGEX "^// draws")
    if(draws AND TIDE_PLATFORM AND NOT TIDE_WEB AND NOT TIDE_ANDROID)
        add_test(NAME ${target}_gpu
            COMMAND "${CMAKE_COMMAND}"
                "-DRUN=$<TARGET_FILE:${target}>"
                "-DEMULATOR=${TIDE_SAFETY_EMULATOR}"
                "-DSOURCE=${source}"
                -DGPU=gpu
                -P "${CMAKE_CURRENT_SOURCE_DIR}/safety/expect_safety.cmake")
        set_tests_properties(${target}_gpu PROPERTIES TIMEOUT 300 LABELS "safety;gpu")
    endif()
endforeach()

# Error tests: each errors/<name>.tide must fail to compile, and tidec's output
# must contain the text on the file's first line, written as `// expect: <text>`.
# A folder errors/<name>/ is one program of several files.
file(GLOB TIDE_ERROR_TESTS CONFIGURE_DEPENDS LIST_DIRECTORIES true "${CMAKE_CURRENT_SOURCE_DIR}/errors/*")
foreach(source IN LISTS TIDE_ERROR_TESTS)
    if(NOT IS_DIRECTORY "${source}" AND NOT source MATCHES "\\.tide$")
        continue()
    endif()
    get_filename_component(name "${source}" NAME_WE)
    add_test(NAME tidec_error_${name}
        COMMAND "${CMAKE_COMMAND}"
            "-DTIDEC=${TIDEC_PATH}"
            "-DSOURCE=${source}"
            "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/errors"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_error.cmake")
endforeach()

# Warning tests: each warnings/<name>.tide must compile, with as many warnings
# as its first line says, `// warnings: <count>`, and each text on its
# `// expect: <text>` lines somewhere in what tidec says: warnings and notes.
file(GLOB TIDE_WARNING_TESTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/warnings/*.tide")
foreach(source IN LISTS TIDE_WARNING_TESTS)
    get_filename_component(name "${source}" NAME_WE)
    add_test(NAME tidec_warning_${name}
        COMMAND "${CMAKE_COMMAND}"
            "-DTIDEC=${TIDEC_PATH}"
            "-DSOURCE=${source}"
            "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/warnings"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_warnings.cmake")
endforeach()

# Scale test: a big game of many namespaces, made up at test time, which tidec
# has to compile within a generous time (see scale/scale.cmake). It took
# minutes, and tens of gigabytes, while codegen's time grew with the game's
# size squared.
add_test(NAME tidec_scale
    COMMAND "${CMAKE_COMMAND}"
        "-DTIDEC=${TIDEC_PATH}"
        "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/scale"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/scale/scale.cmake")
set_tests_properties(tidec_scale PROPERTIES TIMEOUT 600)

# Schedule tests: `tidec --schedule` on schedule/<name>.tide must print
# schedule/<name>.txt exactly.
file(GLOB TIDE_SCHEDULE_TESTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/schedule/*.tide")
foreach(source IN LISTS TIDE_SCHEDULE_TESTS)
    get_filename_component(name "${source}" NAME_WE)
    get_filename_component(dir "${source}" DIRECTORY)
    add_test(NAME tidec_schedule_${name}
        COMMAND "${CMAKE_COMMAND}"
            "-DTIDEC=${TIDEC_PATH}"
            "-DSOURCE=${source}"
            "-DEXPECTED=${dir}/${name}.txt"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_schedule.cmake")
endforeach()

# Shader tests: `tidec --shaders` on shaders/<name>.tide must print
# shaders/<name>.txt exactly: the GLSL and WGSL of each shader its mesh draw
# calls draw through.
file(GLOB TIDE_SHADER_TESTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.tide")
foreach(source IN LISTS TIDE_SHADER_TESTS)
    get_filename_component(name "${source}" NAME_WE)
    get_filename_component(dir "${source}" DIRECTORY)
    add_test(NAME tidec_shaders_${name}
        COMMAND "${CMAKE_COMMAND}"
            "-DTIDEC=${TIDEC_PATH}"
            "-DSOURCE=${source}"
            "-DEXPECTED=${dir}/${name}.txt"
            "-DOPTION=--shaders"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_schedule.cmake")
endforeach()
