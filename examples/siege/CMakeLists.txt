# Siege: two towers of crates and a cannon each, on Box2D. A game to play in a
# window or in the docs site (docs/guide/siege.md), alone or up to four in a
# room.
#
# Its folder is what `tide run` builds: game.tide is the match, the other
# files what a machine shows of it and which match it's in, and tide.packages
# lists box2d. tide_add_game reads no tide.packages, so the package is named
# here, and built with the game as tide builds a game that lists it: its Tide
# file before the game's, its C with the game's, and Box2D's own sources,
# which packages/ builds as tide_box2d. The files are in the order of their
# names, as tide compiles a folder's.
set(SIEGE_SOURCES effects.tide game.tide hud.tide lobby.tide play.tide)
set(SIEGE_PACKAGE "${PROJECT_SOURCE_DIR}/packages/box2d")

if(TARGET tide_platform)
    tide_add_game(siege SOURCES ${SIEGE_SOURCES} "${SIEGE_PACKAGE}/bridge.c"
        PACKAGES Box2D "${SIEGE_PACKAGE}/box2d.tide")
    target_link_libraries(siege PRIVATE tide_box2d)
endif()

# Two players in a match on the loopback network, with no window. The test is
# in a folder of its own, with a tide.packages, so that `tide run` here leaves
# its C out of the game.
tide_add_game(siege_tests SOURCES ${SIEGE_SOURCES} "${SIEGE_PACKAGE}/bridge.c" NAME game
    PACKAGES Box2D "${SIEGE_PACKAGE}/box2d.tide"
    HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" test/siege_test.c)
target_include_directories(siege_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(siege_tests PRIVATE tide_box2d)
add_test(NAME siege_tests COMMAND siege_tests)
set_tests_properties(siege_tests PROPERTIES TIMEOUT 120)
