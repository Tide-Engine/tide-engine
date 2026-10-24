# Earth: a paper plane for everyone who opens the page, around one planet at
# its real size, split into shards. The match is game.tide; globe.tide draws
# the planet and show.tide the rest.
set(EARTH_SOURCES game.tide globe.tide show.tide)
tide_add_game(earth SOURCES ${EARTH_SOURCES} TITLE "Earth" SHELL page.html)

# The test is in a folder of its own, with a tide.packages, so that `tide run`
# and `tide deploy` here leave its C out of the game.
tide_add_game(earth_tests SOURCES ${EARTH_SOURCES}
    HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" test/earth_test.c NAME game)
target_include_directories(earth_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests")
add_test(NAME earth_tests COMMAND earth_tests)
