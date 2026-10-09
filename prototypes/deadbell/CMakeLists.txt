# Deliberately absent from the docs' sample_games glob and package installation.
set(DEADBELL_SOURCES audio.tide environment.tide game.tide input.tide light.tide
    play.tide shapes.tide show.tide ui.tide)
set(DEADBELL_ROOMS PACKAGES Rooms "${PROJECT_SOURCE_DIR}/packages/rooms/rooms.tide")
tide_add_game(deadbell SOURCES ${DEADBELL_SOURCES} ${DEADBELL_ROOMS})

tide_add_game(deadbell_tests SOURCES ${DEADBELL_SOURCES} ${DEADBELL_ROOMS} NAME game
    HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" test/deadbell_test.c)
target_include_directories(deadbell_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(deadbell_tests PRIVATE tide_platform)
add_test(NAME deadbell_tests COMMAND deadbell_tests)
set_tests_properties(deadbell_tests PROPERTIES LABELS private TIMEOUT 300)

tide_add_game(deadbell_smoke SOURCES ${DEADBELL_SOURCES} test/smoke.tide ${DEADBELL_ROOMS}
    NAME game HOST test/smoke.c WINDOW)
target_link_libraries(deadbell_smoke PRIVATE tide_platform)
if(TIDE_WEB)
    tide_web_page(deadbell_smoke SHELL "${PROJECT_SOURCE_DIR}/platform/web/test_shell.html")
    tide_add_web_test(deadbell_render TARGET deadbell_smoke WEBGPU EXPECT "DEADBELL_RENDER_PASS")
else()
    add_test(NAME deadbell_render COMMAND deadbell_smoke)
    set_tests_properties(deadbell_render PROPERTIES LABELS "private;gpu")
endif()
