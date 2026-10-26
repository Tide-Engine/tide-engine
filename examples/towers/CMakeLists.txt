# Towers: four plinths and a race to build the tallest tower of bricks, on
# Box2D, alone against three bots or up to four in a room.
#
# Its folder is what `tide run` builds: game.tide is the match, bots.tide how a
# tower nobody steers is built, the other files what a machine shows of it, and
# tide.packages lists box2d. tide_add_game reads no tide.packages, so the
# packages (box2d and rooms) are named here and built as tide builds a game
# that lists them: their Tide files before the game's, Box2D's C with the
# game's, Box2D's own sources as tide_box2d.
set(TOWERS_SOURCES bots.tide effects.tide game.tide hud.tide play.tide)
set(TOWERS_PACKAGE "${PROJECT_SOURCE_DIR}/packages/box2d")
set(TOWERS_PACKAGES PACKAGES Box2D "${TOWERS_PACKAGE}/box2d.tide" Rooms "${PROJECT_SOURCE_DIR}/packages/rooms/rooms.tide")

if(TARGET tide_platform)
    tide_add_game(towers SOURCES ${TOWERS_SOURCES} "${TOWERS_PACKAGE}/bridge.c" ${TOWERS_PACKAGES})
    target_link_libraries(towers PRIVATE tide_box2d)
endif()

# Three players in a match on the loopback network, with no window. The test is
# in a folder of its own, with a tide.packages, so that `tide run` here leaves
# its C out of the game.
tide_add_game(towers_tests SOURCES ${TOWERS_SOURCES} "${TOWERS_PACKAGE}/bridge.c" NAME game ${TOWERS_PACKAGES}
    HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" test/towers_test.c)
target_include_directories(towers_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(towers_tests PRIVATE tide_box2d)
add_test(NAME towers_tests COMMAND towers_tests)
set_tests_properties(towers_tests PROPERTIES TIMEOUT 300)
