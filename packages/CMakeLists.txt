# The packages that come with tide (see AGENTS.md, Packaging and releases):
# games get one by listing it in their tide.packages, and tide compiles it
# with them. Here, each one's tests build it the way tide would.

# A package keeps its systems in their order whatever a game orders its own
# by: `tidec --schedule` on <package>/test/order.tide, with the package as
# tide builds a game that lists it, puts every system in the order
# <package>/test/order.txt lists, a name a line. The order alone is compared,
# and not what tidec says of each wait.
set(TIDE_PACKAGE_ORDER "${CMAKE_CURRENT_BINARY_DIR}/expect_order.cmake")
file(WRITE "${TIDE_PACKAGE_ORDER}" [=[
execute_process(
    COMMAND "${TIDEC}" "${GAME}" --package "${PACKAGE}" "${SOURCE}" --schedule
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "tidec --schedule failed:\n${stderr}")
endif()
string(REPLACE "\r\n" "\n" stdout "${stdout}")
string(REGEX MATCHALL "(^|\n)stage +[0-9]+ +[^ \n]+" lines "${stdout}")
set(order "")
foreach(line IN LISTS lines)
    string(REGEX REPLACE "^\n?stage +[0-9]+ +" "" name "${line}")
    string(APPEND order "${name}\n")
endforeach()
file(READ "${EXPECTED}" expected)
string(REPLACE "\r\n" "\n" expected "${expected}")
if(NOT order STREQUAL expected)
    message(FATAL_ERROR "the order of the systems changed. Expected (${EXPECTED}):\n${expected}\nGot:\n${order}")
endif()
]=])

function(tide_package_order_test name namespace source folder)
    add_test(NAME ${name}
        COMMAND "${CMAKE_COMMAND}" "-DTIDEC=${TIDEC_PATH}" "-DGAME=${folder}/order.tide" "-DPACKAGE=${namespace}"
            "-DSOURCE=${source}" "-DEXPECTED=${folder}/order.txt" -P "${TIDE_PACKAGE_ORDER}")
    # The game is nothing's to build, so tide_add_game doesn't list it: for editors, it's a game of these two
    # files (see tide_write_games_manifest in cmake/Tide.cmake)
    set_property(GLOBAL APPEND PROPERTY TIDE_GAME_LINES "${name}\t${folder}/order.tide" "${name}\t${source}\t${namespace}")
endfunction()

# Box2D's own sources, as they came: a library the tests share, with its
# warnings off. The package's bridge is ours, and compiles with each test.
file(GLOB box2d_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/box2d/box2d/src/*.c")
add_library(tide_box2d STATIC ${box2d_sources})
# With the flags a game's C gets, which keep float math the same everywhere:
# without them, clang fuses a multiply and an add on ARM, and Box2D steps
# otherwise there
target_link_libraries(tide_box2d PRIVATE tide_flags)
target_compile_options(tide_box2d PRIVATE -w)
# Optimized in debug builds too: they are the libraries as they came, which nobody here steps through, and
# their steps are most of what the tests of physics take (a debug run of Sumo's tests went from 302 s to 108).
# The results are the same either way, which the pins say: they agree in debug and release. Asserts stay on.
target_compile_options(tide_box2d PRIVATE $<$<CONFIG:Debug>:-O2>)
if(TIDE_SANITIZE)
    # As it came, it hands memset no array where it has none to clear (0
    # bytes of it), which the sanitizer would stop on: that check is off for
    # its sources, like its warnings. The rest stays on.
    target_compile_options(tide_box2d PRIVATE -fno-sanitize=nonnull-attribute)
endif()

# Each of its games is built with the package as tide builds one that lists it,
# the package's file before the game's own: a package's systems that a game has
# no entities for (parts, in a game with none) say nothing, where a game's own
# would be warned about.
tide_add_game(tide_box2d_stack SOURCES box2d/bridge.c box2d/test/stack.tide NAME game PACKAGES Box2D box2d/box2d.tide
    HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" box2d/test/stack_test.c)
target_include_directories(tide_box2d_stack PRIVATE "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(tide_box2d_stack PRIVATE tide_box2d)
if(TARGET tide_platform)
    # Its steps on the tick's threads
    target_compile_definitions(tide_box2d_stack PRIVATE BOX2D_TEST_JOBS)
    target_link_libraries(tide_box2d_stack PRIVATE tide_platform)
endif()
add_test(NAME tide_box2d_stack COMMAND tide_box2d_stack)
set_tests_properties(tide_box2d_stack PROPERTIES TIMEOUT 120)

tide_package_order_test(tide_box2d_order Box2D "${CMAKE_CURRENT_SOURCE_DIR}/box2d/box2d.tide" "${CMAKE_CURRENT_SOURCE_DIR}/box2d/test")

# What a game does with it: systems ordered against the step, views, impulses,
# bodies at rest that code moves, and sensors. A game that's built with the
# package as tide builds one that lists it, the package's files before its own.
tide_add_game(tide_box2d_course SOURCES box2d/bridge.c box2d/test/course.tide NAME course PACKAGES Box2D box2d/box2d.tide
    HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" box2d/test/course_test.c)
target_include_directories(tide_box2d_course PRIVATE "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(tide_box2d_course PRIVATE tide_box2d)
add_test(NAME tide_box2d_course COMMAND tide_box2d_course)
set_tests_properties(tide_box2d_course PROPERTIES TIMEOUT 120)

# Its image written as what changed of the last one, against the image written
# whole: a match of its own, so that the one above keeps its pinned hashes, and
# Box2D by itself.
tide_add_game(tide_box2d_parts SOURCES box2d/bridge.c box2d/test/yard.tide NAME yard PACKAGES Box2D box2d/box2d.tide
    HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" box2d/test/parts_test.c)
target_include_directories(tide_box2d_parts PRIVATE "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(tide_box2d_parts PRIVATE tide_box2d)
if(TARGET tide_platform)
    target_compile_definitions(tide_box2d_parts PRIVATE BOX2D_TEST_JOBS)
    target_link_libraries(tide_box2d_parts PRIVATE tide_platform)
endif()
add_test(NAME tide_box2d_parts COMMAND tide_box2d_parts)
set_tests_properties(tide_box2d_parts PROPERTIES TIMEOUT 300)

# What a body is made of and what code asks of the world: parts, bodies that
# change what they are, rays, shapes cast and looked into, what's in a sensor,
# hits, and how the world is tuned.
tide_add_game(tide_box2d_kit SOURCES box2d/bridge.c box2d/test/kit.tide NAME kit PACKAGES Box2D box2d/box2d.tide
    HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" box2d/test/kit_test.c)
target_include_directories(tide_box2d_kit PRIVATE "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(tide_box2d_kit PRIVATE tide_box2d)
if(TARGET tide_platform)
    # What a system asks on the tick's threads
    target_compile_definitions(tide_box2d_kit PRIVATE BOX2D_TEST_JOBS)
    target_link_libraries(tide_box2d_kit PRIVATE tide_platform)
endif()
add_test(NAME tide_box2d_kit COMMAND tide_box2d_kit)
set_tests_properties(tide_box2d_kit PROPERTIES TIMEOUT 120)

# Bodies that code puts to sleep and wakes, and what `asleep` reads of them: a
# scene of its own, through what a match does to a world.
tide_add_game(tide_box2d_sleep SOURCES box2d/bridge.c box2d/test/sleep.tide NAME sleeping PACKAGES Box2D box2d/box2d.tide
    HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" box2d/test/sleep_test.c)
target_include_directories(tide_box2d_sleep PRIVATE "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(tide_box2d_sleep PRIVATE tide_box2d)
add_test(NAME tide_box2d_sleep COMMAND tide_box2d_sleep)
set_tests_properties(tide_box2d_sleep PROPERTIES TIMEOUT 120)

# Rooms: the menu a game opens on and the way back to it, in Tide alone. Its
# test's game is built with the package as tide builds one that lists it.
tide_add_game(tide_rooms_lobby SOURCES rooms/test/game.tide NAME game PACKAGES Rooms rooms/rooms.tide
    HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" rooms/test/rooms_test.c)
target_include_directories(tide_rooms_lobby PRIVATE "${PROJECT_SOURCE_DIR}/tests")
add_test(NAME tide_rooms_lobby COMMAND tide_rooms_lobby)
set_tests_properties(tide_rooms_lobby PROPERTIES TIMEOUT 120)
# A game that draws pages of its own: an override of one, and a view of its own for another
tide_add_game(tide_rooms_pages SOURCES rooms/test/pages.tide NAME game PACKAGES Rooms rooms/rooms.tide
    HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" rooms/test/pages_test.c)
target_include_directories(tide_rooms_pages PRIVATE "${PROJECT_SOURCE_DIR}/tests")
add_test(NAME tide_rooms_pages COMMAND tide_rooms_pages)
set_tests_properties(tide_rooms_pages PROPERTIES TIMEOUT 120)

# Lobby: the client of the relay's lobbies and its menu, in Tide alone. Its
# tests' games are built with the package as tide builds one that lists it.
set(lobby_files lobby/lobby.tide lobby/service.tide lobby/pages.tide)
# With no service: a game that fills the identity, lobby and matchmaking slots itself, and draws a page its own way
tide_add_game(tide_lobby_menu SOURCES lobby/test/game.tide NAME game PACKAGES Lobbies ${lobby_files}
    HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" lobby/test/menu_test.c)
target_include_directories(tide_lobby_menu PRIVATE "${PROJECT_SOURCE_DIR}/tests")
add_test(NAME tide_lobby_menu COMMAND tide_lobby_menu)
set_tests_properties(tide_lobby_menu PROPERTIES TIMEOUT 120)
# With a service and a network of the game's own (tide/network.h), in its C: every slot overridden, and four
# players from a quick match's lobby into a match on that network, which changes hands when its host goes.
# Each player is run as a host runs one (test/network_players.h), with a session of its own
tide_add_game(tide_lobby_own SOURCES lobby/test/own.tide lobby/test/own.c NAME game PACKAGES Lobbies ${lobby_files}
    HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" lobby/test/own_test.c)
target_include_directories(tide_lobby_own PRIVATE "${PROJECT_SOURCE_DIR}/tests" "${CMAKE_CURRENT_SOURCE_DIR}/test")
add_test(NAME tide_lobby_own COMMAND tide_lobby_own)
set_tests_properties(tide_lobby_own PROPERTIES TIMEOUT 120)
# A game that lists it beside rooms, with both namespaces imported: tidec checks it, with no name of one in the
# other's way
add_test(NAME tide_lobby_beside_rooms
    COMMAND "${TIDEC_PATH}" "${CMAKE_CURRENT_SOURCE_DIR}/lobby/test/both.tide"
        --package Rooms "${CMAKE_CURRENT_SOURCE_DIR}/rooms/rooms.tide"
        --package Lobbies "${CMAKE_CURRENT_SOURCE_DIR}/lobby/lobby.tide" "${CMAKE_CURRENT_SOURCE_DIR}/lobby/service.tide"
        "${CMAKE_CURRENT_SOURCE_DIR}/lobby/pages.tide" --schedule)
set_tests_properties(tide_lobby_beside_rooms PROPERTIES FAIL_REGULAR_EXPRESSION "warning:")
# With Tide's own service: four players through the relay's lobbies on this machine (relay/lobbies.mjs), natively
# through the platform's HTTP client against the relay lobby/test/relay.mjs starts, and in a page through the
# page's fetch, with the lobbies answering beside the test's own server
if(TIDE_PLATFORM)
    if(TIDE_WEB)
        tide_add_game(tide_lobby_relay_web SOURCES lobby/test/relay.tide NAME game PACKAGES Lobbies ${lobby_files}
            HOST lobby/test/relay_web.c WINDOW)
        target_link_libraries(tide_lobby_relay_web PRIVATE tide_platform)
        tide_web_page(tide_lobby_relay_web SHELL "${PROJECT_SOURCE_DIR}/platform/web/test_shell.html")
        add_test(NAME tide_lobby_relay_web
            COMMAND "${TIDE_NODE}" "${PROJECT_SOURCE_DIR}/cmake/run_web_served_test.mjs" "${TIDE_BROWSER}"
                "$<TARGET_FILE_DIR:tide_lobby_relay_web>/tide_lobby_relay_web.html"
                "${CMAKE_CURRENT_BINARY_DIR}/tide_lobby_relay_web-browser" "backend=webgl"
                --routes "${CMAKE_CURRENT_SOURCE_DIR}/lobby/test/relay_routes.mjs")
        # The guard around the runner's own 100 seconds and what it asks the page after them (cmake/WebTest.cmake)
        set_tests_properties(tide_lobby_relay_web PROPERTIES SKIP_REGULAR_EXPRESSION "SKIPPED:" TIMEOUT 150)
    elseif(NOT TIDE_ANDROID AND TIDE_NODE)
        tide_add_game(tide_lobby_relay SOURCES lobby/test/relay.tide NAME game PACKAGES Lobbies ${lobby_files}
            HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" lobby/test/relay_test.c)
        target_include_directories(tide_lobby_relay PRIVATE "${PROJECT_SOURCE_DIR}/tests")
        target_link_libraries(tide_lobby_relay PRIVATE tide_platform)
        add_test(NAME tide_lobby_relay
            COMMAND "${TIDE_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/lobby/test/relay.mjs" "$<TARGET_FILE:tide_lobby_relay>")
        set_tests_properties(tide_lobby_relay PROPERTIES TIMEOUT 120)
    endif()
endif()

# Steam: the lobby package's four slots over Steamworks' flat API, in Tide and C (steam/bridge.c). The SDK
# isn't here and nothing builds the package but this test, which links a Steam that isn't in the SDK's
# library's place (steam/test/fake_steam.c): four users in one program, each with a session of its own,
# from Steam's lobbies into a match on Steam's messages. Desktop only, as the package is.
if(NOT TIDE_WEB AND NOT TIDE_ANDROID)
    tide_add_game(tide_steam_lobby SOURCES steam/test/game.tide steam/bridge.c steam/test/fake_steam.c NAME game
        PACKAGES Lobbies ${lobby_files} Steam steam/steam.tide
        HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" steam/test/steam_test.c)
    target_include_directories(tide_steam_lobby PRIVATE "${PROJECT_SOURCE_DIR}/tests" "${CMAKE_CURRENT_SOURCE_DIR}/test"
        "${CMAKE_CURRENT_SOURCE_DIR}/steam")
    # One program is one Steam user to the package: the test's four each have its state
    target_compile_definitions(tide_steam_lobby PRIVATE TIDE_STEAM_USERS=4)
    add_test(NAME tide_steam_lobby COMMAND tide_steam_lobby)
    set_tests_properties(tide_steam_lobby PROPERTIES TIMEOUT 120)
endif()
if(TIDE_WEB)
    # A web build that lists it (a game's tide.packages with the symbol defined for the wrong target) is told
    # so by the package's own C, in words: its bridge is compiled for the page as tide would
    add_test(NAME tide_steam_web_refused
        COMMAND "${CMAKE_C_COMPILER}" --target=wasm32-wasi -fsyntax-only "-I${PROJECT_SOURCE_DIR}/engine/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/steam/bridge.c")
    set_tests_properties(tide_steam_web_refused PROPERTIES
        PASS_REGULAR_EXPRESSION "The steam package is for desktop builds, where Steam is")
endif()

# Box3D's, the same way, and with the same flags for the same reason. Its tests'
# games are each built with the package as tide builds a game that lists it,
# the package's files before their own: a game needn't use all of a package.
file(GLOB box3d_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/box3d/box3d/src/*.c")
add_library(tide_box3d STATIC ${box3d_sources})
target_link_libraries(tide_box3d PRIVATE tide_flags)
target_compile_options(tide_box3d PRIVATE -w)
target_compile_options(tide_box3d PRIVATE $<$<CONFIG:Debug>:-O2>)
if(TIDE_SANITIZE)
    target_compile_options(tide_box3d PRIVATE -fno-sanitize=nonnull-attribute)
endif()

tide_add_game(tide_box3d_stack SOURCES box3d/bridge.c box3d/test/stack.tide NAME game PACKAGES Box3D box3d/box3d.tide
    HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" box3d/test/stack_test.c)
target_include_directories(tide_box3d_stack PRIVATE "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(tide_box3d_stack PRIVATE tide_box3d)
if(TARGET tide_platform)
    # Its steps on the tick's threads
    target_compile_definitions(tide_box3d_stack PRIVATE BOX3D_TEST_JOBS)
    target_link_libraries(tide_box3d_stack PRIVATE tide_platform)
endif()
add_test(NAME tide_box3d_stack COMMAND tide_box3d_stack)
# A step in 3D is several of Box2D's
set_tests_properties(tide_box3d_stack PROPERTIES TIMEOUT 300)

# A joining process has never ticked Box3D: its resident kind has to be
# registered before either form of the first world can be unpacked.
tide_add_game(tide_box3d_join SOURCES box3d/bridge.c box3d/test/stack.tide NAME game PACKAGES Box3D box3d/box3d.tide
    HOST box3d/test/join_test.c)
target_link_libraries(tide_box3d_join PRIVATE tide_box3d)
add_test(NAME tide_box3d_join
    COMMAND "${CMAKE_COMMAND}" "-DPROGRAM=$<TARGET_FILE:tide_box3d_join>"
        "-DEMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}" "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/box3d_join"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/box3d/test/join.cmake")
set_tests_properties(tide_box3d_join PROPERTIES TIMEOUT 120)

tide_package_order_test(tide_box3d_order Box3D "${CMAKE_CURRENT_SOURCE_DIR}/box3d/box3d.tide" "${CMAKE_CURRENT_SOURCE_DIR}/box3d/test")

# What a game does with it, as tide_box2d_course is for Box2D.
tide_add_game(tide_box3d_course SOURCES box3d/bridge.c box3d/test/course.tide NAME course PACKAGES Box3D box3d/box3d.tide
    HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" box3d/test/course_test.c)
target_include_directories(tide_box3d_course PRIVATE "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(tide_box3d_course PRIVATE tide_box3d)
add_test(NAME tide_box3d_course COMMAND tide_box3d_course)
set_tests_properties(tide_box3d_course PROPERTIES TIMEOUT 300)

# Bodies that code puts to sleep and wakes, as tide_box2d_sleep is for Box2D,
# with every image in parts compared with the image written whole.
tide_add_game(tide_box3d_sleep SOURCES box3d/bridge.c box3d/test/sleep.tide NAME sleeping PACKAGES Box3D box3d/box3d.tide
    HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" box3d/test/sleep_test.c)
target_include_directories(tide_box3d_sleep PRIVATE "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(tide_box3d_sleep PRIVATE tide_box3d)
add_test(NAME tide_box3d_sleep COMMAND tide_box3d_sleep)
set_tests_properties(tide_box3d_sleep PROPERTIES TIMEOUT 300)

# Its image written as what changed of the last one, against the image written
# whole: a match of its own, so that the one above keeps its pinned hashes, and
# Box3D by itself.
tide_add_game(tide_box3d_parts SOURCES box3d/bridge.c box3d/test/yard.tide NAME yard PACKAGES Box3D box3d/box3d.tide
    HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" box3d/test/parts_test.c)
target_include_directories(tide_box3d_parts PRIVATE "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(tide_box3d_parts PRIVATE tide_box3d)
if(TARGET tide_platform)
    target_compile_definitions(tide_box3d_parts PRIVATE BOX3D_TEST_JOBS)
    target_link_libraries(tide_box3d_parts PRIVATE tide_platform)
endif()
add_test(NAME tide_box3d_parts COMMAND tide_box3d_parts)
set_tests_properties(tide_box3d_parts PROPERTIES TIMEOUT 300)

tide_add_game(tide_box2d_rewind SOURCES box2d/bridge.c box2d/test/rewind.tide NAME rewind PACKAGES Box2D box2d/box2d.tide
    HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" box2d/test/rewind_test.c)
target_include_directories(tide_box2d_rewind PRIVATE "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(tide_box2d_rewind PRIVATE tide_box2d)
add_test(NAME tide_box2d_rewind COMMAND tide_box2d_rewind)

tide_add_game(tide_box3d_rewind SOURCES box3d/bridge.c box3d/test/rewind.tide NAME rewind PACKAGES Box3D box3d/box3d.tide
    HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" box3d/test/rewind_test.c)
target_include_directories(tide_box3d_rewind PRIVATE "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(tide_box3d_rewind PRIVATE tide_box3d)
add_test(NAME tide_box3d_rewind COMMAND tide_box3d_rewind)

tide_add_game(tide_box3d_mesh SOURCES box3d/bridge.c box3d/test/mesh.tide NAME mesh PACKAGES Box3D box3d/box3d.tide
    HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" box3d/test/mesh_test.c)
target_include_directories(tide_box3d_mesh PRIVATE "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(tide_box3d_mesh PRIVATE tide_box3d)
add_test(NAME tide_box3d_mesh COMMAND tide_box3d_mesh)

tide_add_game(tide_box3d_mesh_join SOURCES box3d/bridge.c box3d/test/mesh.tide NAME mesh PACKAGES Box3D box3d/box3d.tide
    HOST box3d/test/join_test.c)
target_compile_definitions(tide_box3d_mesh_join PRIVATE BOX3D_JOIN_HEADER="mesh.h")
target_link_libraries(tide_box3d_mesh_join PRIVATE tide_box3d)
add_test(NAME tide_box3d_mesh_join
    COMMAND "${CMAKE_COMMAND}" "-DPROGRAM=$<TARGET_FILE:tide_box3d_mesh_join>"
        "-DEMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}" "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/box3d_mesh_join"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/box3d/test/join.cmake")
set_tests_properties(tide_box3d_mesh_join PROPERTIES TIMEOUT 120)

# A client's actual confirmed frame -> sampled input -> server's historical hit,
# with server ticks, each client's updates and presentation paced independently.
foreach(dimension 2 3)
    tide_add_game(tide_box${dimension}d_rewind_network
        SOURCES box${dimension}d/bridge.c box${dimension}d/test/rewind_network.tide
        NAME rewind_network PACKAGES Box${dimension}D box${dimension}d/box${dimension}d.tide
        HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" test/rewind_network_test.c)
    target_include_directories(tide_box${dimension}d_rewind_network PRIVATE "${PROJECT_SOURCE_DIR}/tests")
    target_link_libraries(tide_box${dimension}d_rewind_network PRIVATE tide_box${dimension}d)
    add_test(NAME tide_box${dimension}d_rewind_network COMMAND tide_box${dimension}d_rewind_network)
    set_tests_properties(tide_box${dimension}d_rewind_network PROPERTIES TIMEOUT 120)
endforeach()
