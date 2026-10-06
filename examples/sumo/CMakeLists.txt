# Sumo: heavy balls in a ring stacked with crates, on Box3D. A game to play in
# a window or in the docs site (docs/guide/sumo.md), alone or up to eight in
# a room.
#
# Its folder is what `tide run` builds: game.tide and arena.tide are the
# match, the other files what a machine shows of it and which match it's in,
# and tide.packages lists box3d. tide_add_game reads no tide.packages, so the
# package is named here, and built with the game as tide builds a game that
# lists it: its Tide file before the game's, its C with the game's, and
# Box3D's own sources, which packages/ builds as tide_box3d. The files are in
# the order of their names, as tide compiles a folder's.
set(SUMO_SOURCES arena.tide effects.tide game.tide hud.tide light.tide lobby.tide play.tide)
set(SUMO_PACKAGE "${PROJECT_SOURCE_DIR}/packages/box3d")

if(TARGET tide_platform)
    tide_add_game(sumo SOURCES ${SUMO_SOURCES} "${SUMO_PACKAGE}/bridge.c"
        PACKAGES Box3D "${SUMO_PACKAGE}/box3d.tide")
    target_link_libraries(sumo PRIVATE tide_box3d)
endif()

# Players in a match on the loopback network, with no window. The test is in
# a folder of its own, with a tide.packages, so that `tide run` here leaves
# its C out of the game.
tide_add_game(sumo_tests SOURCES ${SUMO_SOURCES} "${SUMO_PACKAGE}/bridge.c" NAME game
    PACKAGES Box3D "${SUMO_PACKAGE}/box3d.tide"
    HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" test/sumo_test.c)
target_include_directories(sumo_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(sumo_tests PRIVATE tide_box3d)
add_test(NAME sumo_tests COMMAND sumo_tests)
# A step in 3D is several of Box2D's, and a web build has no SIMD for it
set_tests_properties(sumo_tests PROPERTIES TIMEOUT 300)
