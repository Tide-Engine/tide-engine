# Siege: two towers of crates and a cannon each, on Box2D. A game to play in a
# window or in the docs site (docs/guide/siege.md), alone or two in a match.
#
# Its folder is what `tide run` builds: game.tide is the match, play.tide what
# a machine shows of it, and tide.packages lists box2d. tide_add_game reads no
# tide.packages, so the package is named here, and built with the game as
# tide builds a game that lists it: its Tide file before the game's, its C
# with the game's, and Box2D's own sources, which packages/ builds as
# tide_box2d.
set(SIEGE_SOURCES game.tide play.tide)
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
