cmake_minimum_required(VERSION 3.25)
# Static libraries listed once more only for linkers that need it: Apple's
# warns about a repeated one, as a program that links the engine and the
# platform layer would get otherwise. CMake 3.29 and later.
if(POLICY CMP0156)
    cmake_policy(SET CMP0156 NEW)
endif()

# Always build with clang, however CMake is invoked (presets, IDE profiles, plain `cmake -B`).
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/clang-toolchain.cmake")
endif()

project(Tide LANGUAGES C)

# AppleClang is Apple's build of clang.
if(NOT CMAKE_C_COMPILER_ID MATCHES "^(Apple)?Clang$" OR NOT CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "GNU")
    message(FATAL_ERROR "Tide builds with clang only (see AGENTS.md). "
                        "Found ${CMAKE_C_COMPILER_ID} (${CMAKE_C_COMPILER_FRONTEND_VARIANT} frontend).")
endif()

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Web builds: WebAssembly through clang and wasi-libc (cmake/wasi-toolchain.cmake).
if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
    set(TIDE_WEB ON)
    set(CMAKE_EXECUTABLE_SUFFIX .wasm) # CMake's WASI platform leaves it empty
    # Threads, which older clang doesn't take from the target's name. Only when
    # compiling: older clang warns that -pthread does nothing for a link. And
    # WebAssembly's SIMD, whose operations give the same results on every
    # machine (relaxed SIMD's don't, and stays off for determinism).
    add_compile_options(-pthread -msimd128)
else()
    set(TIDE_WEB OFF)
endif()
# Android builds: through clang with Android's NDK (cmake/android-toolchain.cmake).
# Games are shared libraries the app loads, which link the engine's static
# libraries, so everything is position-independent.
if(CMAKE_C_COMPILER_TARGET MATCHES "android")
    set(TIDE_ANDROID ON)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
else()
    set(TIDE_ANDROID OFF)
endif()
# iOS builds: through Xcode's clang (cmake/ios-toolchain.cmake). A game there runs inside another program's window,
# Unity's (tide/embed.h), so this builds the engine, the platform layer with no window of its own and the tests
# that need none, which run in the simulator.
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(TIDE_IOS ON)
else()
    set(TIDE_IOS OFF)
endif()

include(cmake/Sanitizers.cmake)
include(cmake/TideFlags.cmake)
include(cmake/Tide.cmake)
include(cmake/WebTest.cmake)
include(cmake/VulkanTests.cmake)
include(cmake/HlslTests.cmake)
find_program(TIDE_NODE NAMES node) # Tests that run Node: web builds need it anyway

option(TIDE_PLATFORM "Build the platform layer and the demo. Desktop builds download GLFW." ON)
option(TIDE_VISUALIZERS "Build every native game as tide's debug builds are, with its natvis and LLDB views (tide_add_game VISUALIZERS)" OFF)
option(TIDE_TOOLS_ONLY "Build only tidec, as web builds do for their host (cmake/HostTidec.cmake)" OFF)
# Folders outside this repo built as part of it, after the engine's own games:
# a list of paths, each with a CMakeLists.txt that uses tide_add_game and the
# engine's targets as the games here do. Defaults to the environment's
# TIDE_EXTERNAL_DIRS (a list separated as the system's PATH is).
set(tide_external_default "")
if(DEFINED ENV{TIDE_EXTERNAL_DIRS})
    cmake_path(CONVERT "$ENV{TIDE_EXTERNAL_DIRS}" TO_CMAKE_PATH_LIST tide_external_default NORMALIZE)
endif()
set(TIDE_EXTERNAL_DIRS "${tide_external_default}" CACHE STRING "Folders outside this repo built with it (games and their tests)")

# What the tide command says it is, and where it looks for updates. Release
# builds get the version semantic-release picked (.github/workflows/build.yml);
# anything else is a development build, which never looks for updates. tidec
# has it too, for `#if TIDE_0_3_OR_NEWER`, so web and Android builds' host tidec
# gets the same (cmake/HostTidec.cmake).
# A development build is the newest version tagged in this checkout's history
# (a nightly's or a stable's, without its pre-release part); VERSION is the floor
# for a checkout with no tags, such as CI's shallow ones. Releases don't commit
# VERSION back, so release and dev never wait on each other.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" TIDE_BASE_VERSION LIMIT_COUNT 1)
find_package(Git QUIET)
if(GIT_FOUND AND NOT DEFINED CACHE{TIDE_VERSION})
    execute_process(COMMAND "${GIT_EXECUTABLE}" describe --tags --abbrev=0 --match "v[0-9]*"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" OUTPUT_VARIABLE tide_tag
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE tide_tag_result)
    if(tide_tag_result EQUAL 0 AND tide_tag MATCHES "^v([0-9]+\\.[0-9]+\\.[0-9]+)")
        if(CMAKE_MATCH_1 VERSION_GREATER TIDE_BASE_VERSION)
            set(TIDE_BASE_VERSION "${CMAKE_MATCH_1}")
        endif()
    endif()
endif()
set(TIDE_VERSION "${TIDE_BASE_VERSION}-dev" CACHE STRING "tide's version")

# tide_add_game runs tidec: this build's, or in web and Android builds a native
# one, since a WebAssembly or Android tidec can't run during the build. Nor can
# a Windows one on another system (the Windows package, built on Linux).
if(TIDE_WEB OR TIDE_ANDROID OR CMAKE_CROSSCOMPILING)
    include(cmake/HostTidec.cmake)
else()
    set(TIDEC_COMMAND tidec)
    set(TIDEC_DEPENDS tidec)
    set(TIDEC_PATH "$<TARGET_FILE:tidec>")
    set(TIDE_SPIRV_SHADERS_PATH "$<TARGET_FILE:tide_spirv_shaders>")
    set(TIDE_SPIRV_SHADERS_DEPENDS tide_spirv_shaders)
endif()

if(TIDE_TOOLS_ONLY)
    # Asset baking uses the same CPU pose implementation as a running game.
    # The engine library has no platform/window dependency.
    add_subdirectory(engine)
    add_subdirectory(compiler)
    return()
endif()

set(TIDE_CHANNEL "nightly" CACHE STRING "The update channel tide starts on: stable or nightly")

# The package (see cmake/package.cmake) is everything installed as the `tide`
# component: tide and tidels, the engine's headers and sources, and the
# prebuilt platform layer.
file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/package/VERSION" CONTENT "${TIDE_VERSION}\n")
install(FILES "${CMAKE_BINARY_DIR}/package/VERSION" DESTINATION . COMPONENT tide)
# Tide's own license, beside those of what it redistributes (licenses/<name>/).
install(FILES LICENSE-MIT LICENSE-APACHE DESTINATION licenses/tide COMPONENT tide)
# The packages that come with tide (packages/, a folder each) are sources too,
# beside the engine's in the package: tide compiles one with the games that
# list it by its name, and with no other. They install as they are, but for
# what tide makes in a game's folder (an example's, in a package). OPTIONAL:
# there's no folder until there's a package.
install(DIRECTORY packages/ DESTINATION src/packages COMPONENT tide OPTIONAL
    PATTERN ".tide" EXCLUDE PATTERN "build" EXCLUDE
    # This repo's own: how it builds the packages' tests, and the tests
    PATTERN "CMakeLists.txt" EXCLUDE PATTERN "test" EXCLUDE)
if(NOT TIDE_WEB AND NOT TIDE_ANDROID AND NOT TIDE_IOS)
    add_subdirectory(tools) # Editor support
endif()

add_subdirectory(engine)

enable_testing()
if(TIDE_IOS)
    # A test starts in the simulator, from its own folder (cmake/run_ios.sh, tests/ios/ios_run.c)
    add_executable(tide_ios_run tests/ios/ios_run.c)
    set(CMAKE_CROSSCOMPILING_EMULATOR sh "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_ios.sh" "${CMAKE_BINARY_DIR}/bin/tide_ios_run")
endif()
add_subdirectory(tests)
add_subdirectory(compiler)

# The relay (relay/), on Node's own test runner, wherever there's Node.
if(TIDE_NODE)
    # Adapter tests use editor/process doubles and run without VS Code on any host.
    add_test(NAME tide_vscode_adapter COMMAND "${TIDE_NODE}" --test
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/tide-vscode/test/extension.test.mjs"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/tide-vscode/test/debug.test.mjs")
    # The graph editor's (tools/tide-graph): its model (tidec makes the graphs it reads), the page in a headless muted
    # browser (skipped without Chrome or Edge), and VS Code's side of it with VS Code replaced
    if(TARGET tidec)
        add_test(NAME tide_graph_editor COMMAND "${TIDE_NODE}" --test
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/tide-graph/test/model.test.mjs"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/tide-graph/test/browser.test.mjs"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/tide-vscode/test/graph.test.mjs")
        set_tests_properties(tide_graph_editor PROPERTIES
            ENVIRONMENT "TIDE_TIDEC=$<TARGET_FILE:tidec>;$<$<TARGET_EXISTS:tidels>:TIDE_TIDELS=$<TARGET_FILE:tidels>;>TIDE_HIDDEN=1"
            TIMEOUT 600)
    endif()
    add_test(NAME relay COMMAND "${TIDE_NODE}" --test "${CMAKE_CURRENT_SOURCE_DIR}/relay/relay.test.mjs"
        "${CMAKE_CURRENT_SOURCE_DIR}/relay/lobbies.test.mjs")
    # The Android wrapper's file transfers, with adb mocked and no device.
    add_test(NAME tide_android_runner COMMAND "${TIDE_NODE}" --test "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_android.test.mjs")
endif()

if(TIDE_PLATFORM)
    add_subdirectory(platform)
endif()
if(TIDE_IOS)
    # Games with a window of their own and the examples aren't iOS's: what's built is what a game inside another
    # program is made of, with the packages whose C such a game compiles, and `tide_ios_tests` is the tests of it
    # (cmake/ios_tests.cmake)
    add_subdirectory(packages)
    include(cmake/ios_tests.cmake)
    return()
endif()
if(TIDE_PLATFORM)
    if(NOT TIDE_WEB AND NOT TIDE_ANDROID)
        tide_add_game(tide_server_smoke SERVER NAME game SOURCES "${PROJECT_SOURCE_DIR}/compiler/tests/e2e/server_build.tide")
        add_test(NAME tide_server_smoke COMMAND tide_server_smoke --anonymous --udp-only --frames 2)
        if(TIDE_NODE)
            tide_add_game(tide_server_pack_client NAME game
                SOURCES "${PROJECT_SOURCE_DIR}/compiler/tests/e2e/server_build.tide"
                HOST "${PROJECT_SOURCE_DIR}/platform/tests/server_pack_client.c")
            target_link_libraries(tide_server_pack_client PRIVATE tide_platform)
            add_test(NAME tide_server_pack COMMAND "${TIDE_NODE}" "${PROJECT_SOURCE_DIR}/platform/tests/server_pack.mjs"
                "$<TARGET_FILE:tide_server_smoke>" "$<TARGET_FILE:tide_server_pack_client>")
            set_tests_properties(tide_server_pack PROPERTIES TIMEOUT 20)
            add_test(NAME tide_server_room COMMAND "${TIDE_NODE}" "${PROJECT_SOURCE_DIR}/platform/tests/server_pack.mjs"
                "$<TARGET_FILE:tide_server_smoke>" "$<TARGET_FILE:tide_server_pack_client>" room)
            set_tests_properties(tide_server_room PROPERTIES TIMEOUT 20)
            # A sharded world on a dedicated server, and a player walking across its border
            tide_add_game(tide_server_shards SERVER NAME game SOURCES "${PROJECT_SOURCE_DIR}/compiler/tests/e2e/shards_players.tide"
                "${PROJECT_SOURCE_DIR}/compiler/tests/e2e/shards_players_walk.tide")
            tide_add_game(tide_server_shards_client NAME game
                SOURCES "${PROJECT_SOURCE_DIR}/compiler/tests/e2e/shards_players.tide"
                "${PROJECT_SOURCE_DIR}/compiler/tests/e2e/shards_players_walk.tide"
                HOST "${PROJECT_SOURCE_DIR}/platform/tests/server_shards_client.c")
            target_link_libraries(tide_server_shards_client PRIVATE tide_platform)
            add_test(NAME tide_server_shards COMMAND "${TIDE_NODE}" "${PROJECT_SOURCE_DIR}/platform/tests/server_pack.mjs"
                "$<TARGET_FILE:tide_server_shards>" "$<TARGET_FILE:tide_server_shards_client>" shards)
            add_test(NAME tide_server_shards_room COMMAND "${TIDE_NODE}" "${PROJECT_SOURCE_DIR}/platform/tests/server_pack.mjs"
                "$<TARGET_FILE:tide_server_shards>" "$<TARGET_FILE:tide_server_shards_client>" shards-room)
            # ...and the same server killed outright, going on from its journal
            # Then verified, sealed as it stops, and taken up by the game's next build (the same layout, another rule)
            tide_add_game(tide_server_shards_next SERVER NAME game SOURCES "${PROJECT_SOURCE_DIR}/compiler/tests/e2e/shards_players.tide"
                "${PROJECT_SOURCE_DIR}/compiler/tests/e2e/shards_players_walk_next.tide")
            add_test(NAME tide_server_shards_journal COMMAND "${TIDE_NODE}" "${PROJECT_SOURCE_DIR}/platform/tests/server_journal.mjs"
                "$<TARGET_FILE:tide_server_shards>" "$<TARGET_FILE:tide_server_shards_next>")
            # A sharded world whose match uses the store: kept in the journal's folder, there after a stop and a crash
            tide_add_game(tide_server_shards_store SERVER NAME game
                SOURCES "${PROJECT_SOURCE_DIR}/compiler/tests/e2e/storage_await_shards.tide")
            add_test(NAME tide_server_shards_store COMMAND "${TIDE_NODE}" "${PROJECT_SOURCE_DIR}/platform/tests/server_store.mjs"
                "$<TARGET_FILE:tide_server_shards_store>")
            set_tests_properties(tide_server_shards_store PROPERTIES TIMEOUT 130 RUN_SERIAL TRUE)
            set_tests_properties(tide_server_shards tide_server_shards_room PROPERTIES TIMEOUT 30)
            # Do not overlap disk-synchronous recovery with installers and
            # other server/process tests on shared CI runners.
            # The 2.5-second crash run, two 60-second recoveries, the short refused and
            # verifying starts, the next build's run, and cleanup.
            set_tests_properties(tide_server_shards_journal PROPERTIES TIMEOUT 200 RUN_SERIAL TRUE)
        endif()
        tide_add_game(tide_server_entry SERVER NAME game SOURCES
            "${PROJECT_SOURCE_DIR}/compiler/tests/e2e/server_menu.tide"
            "${PROJECT_SOURCE_DIR}/compiler/tests/e2e/server_entry.c")
        add_test(NAME tide_server_entry COMMAND tide_server_entry --anonymous --udp-only --frames 2 --port 7778)
        tide_add_game(tide_server_visual SERVER NAME game SOURCES "${PROJECT_SOURCE_DIR}/compiler/tests/e2e/server_visual.tide")
        add_test(NAME tide_server_visual COMMAND tide_server_visual --anonymous --udp-only --frames 2 --port 7779)
        set_tests_properties(tide_server_smoke tide_server_entry tide_server_visual PROPERTIES TIMEOUT 30)
        target_sources(tidec_e2e_hosting PRIVATE "${PROJECT_SOURCE_DIR}/platform/tests/hosting.c")
        target_link_libraries(tidec_e2e_hosting PRIVATE tide_platform)
    endif()
    add_subdirectory(demo)
    add_subdirectory(examples/duel)
    add_subdirectory(examples/earth) # A sharded planet, on the docs site's home page
    add_subdirectory(bench)   # Ticks on the platform layer's threads
endif()

add_subdirectory(packages) # The packages that come with tide, and their tests

# The physics games are built with a package each, so they come after them.
add_subdirectory(examples/towers)
add_subdirectory(examples/sumo)
add_subdirectory(examples/samples)
add_subdirectory(examples/network) # A game on a network of its own, and its test
# Each external folder builds under external/<its folder's name> in this tree.
foreach(dir IN LISTS TIDE_EXTERNAL_DIRS)
    cmake_path(ABSOLUTE_PATH dir BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE OUTPUT_VARIABLE external)
    string(REGEX REPLACE "(.)/+$" "\\1" external "${external}")
    if(NOT EXISTS "${external}/CMakeLists.txt")
        message(FATAL_ERROR "TIDE_EXTERNAL_DIRS names ${external}, which has no CMakeLists.txt")
    endif()
    cmake_path(GET external FILENAME name)
    add_subdirectory("${external}" "external/${name}")
endforeach()

# The docs' live examples open a window, so they need the platform layer, and
# come after the packages: the physics examples are built with one each.
if(TIDE_PLATFORM)
    option(TIDE_DOC_EXAMPLES "Build the interactive examples in the documentation" OFF)
    if(TIDE_DOC_EXAMPLES)
        add_subdirectory(docs/examples)
    endif()
    option(TIDE_DOC_PLAYGROUND "Build the browser compiler and runtime bundle for the docs playground" ${TIDE_DOC_EXAMPLES})
    if(TIDE_DOC_PLAYGROUND)
        add_subdirectory(docs/playground)
        if(TARGET docs_examples AND TARGET docs_playground)
            add_dependencies(docs_examples docs_playground)
        endif()
    endif()
endif()
