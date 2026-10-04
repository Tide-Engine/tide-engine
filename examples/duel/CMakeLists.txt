# A small 2D duel to play in a window or in the docs site.
tide_add_game(duel SOURCES game.tide physics.tide play.tide TITLE "Tide duel")

tide_add_game(duel_tests SOURCES game.tide physics.tide play.tide
    HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" duel_test.c NAME game)
target_include_directories(duel_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests")
add_test(NAME duel_tests COMMAND duel_tests)
