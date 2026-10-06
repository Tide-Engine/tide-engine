# Alley: bowling with far too many pins, on Box3D. A game to play in a window
# or in the docs site (docs/guide/alley.md), alone or up to eight in a match.
#
# Its folder is what `tide run` builds: game.tide is the match, play.tide what
# a machine shows of it, and tide.packages lists box3d. tide_add_game reads no
# tide.packages, so the package is named here, and built with the game as
# tide builds a game that lists it: its Tide file before the game's, its C
# with the game's, and Box3D's own sources, which packages/ builds as
# tide_box3d.
set(ALLEY_SOURCES game.tide play.tide)
set(ALLEY_PACKAGE "${PROJECT_SOURCE_DIR}/packages/box3d")

if(TARGET tide_platform)
    tide_add_game(alley SOURCES ${ALLEY_SOURCES} "${ALLEY_PACKAGE}/bridge.c"
        PACKAGES Box3D "${ALLEY_PACKAGE}/box3d.tide")
    target_link_libraries(alley PRIVATE tide_box3d)
endif()

# Players in a match on the loopback network, with no window. The test is in
# a folder of its own, with a tide.packages, so that `tide run` here leaves
# its C out of the game.
tide_add_game(alley_tests SOURCES ${ALLEY_SOURCES} "${ALLEY_PACKAGE}/bridge.c" NAME game
    PACKAGES Box3D "${ALLEY_PACKAGE}/box3d.tide"
    HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" test/alley_test.c)
target_include_directories(alley_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(alley_tests PRIVATE tide_box3d)
add_test(NAME alley_tests COMMAND alley_tests)
# A step in 3D is several of Box2D's, and a web build has no SIMD for it
set_tests_properties(alley_tests PROPERTIES TIMEOUT 300)
