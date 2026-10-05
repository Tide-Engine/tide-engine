# A small 2D duel to play in a window or in the docs site.
# The match is game.tide and physics.tide. The rest is what a machine shows of it.
set(DUEL_SOURCES effects.tide game.tide hud.tide paint.tide physics.tide play.tide shapes.tide)
tide_add_game(duel SOURCES ${DUEL_SOURCES} TITLE "Tide duel")

tide_add_game(duel_tests SOURCES ${DUEL_SOURCES}
    HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" duel_test.c NAME game)
target_include_directories(duel_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests")
add_test(NAME duel_tests COMMAND duel_tests)
