set(DEAD_SIGNAL_SOURCES game.tide render.tide world.tide)
set(DEAD_SIGNAL_ROOMS PACKAGES Rooms "${PROJECT_SOURCE_DIR}/packages/rooms/rooms.tide")

if(TARGET tide_platform)
    tide_add_game(dead_signal SOURCES ${DEAD_SIGNAL_SOURCES} ${DEAD_SIGNAL_ROOMS})
endif()

tide_add_game(dead_signal_tests SOURCES ${DEAD_SIGNAL_SOURCES} test/geometry.tide NAME game ${DEAD_SIGNAL_ROOMS}
    HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" test/dead_signal_test.c)
target_include_directories(dead_signal_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests")
add_test(NAME dead_signal_tests COMMAND dead_signal_tests)
set_tests_properties(dead_signal_tests PROPERTIES TIMEOUT 300 LABELS "private-prototype")

if(TARGET tide_platform AND NOT TIDE_ANDROID)
    tide_add_game(dead_signal_render_tests SOURCES ${DEAD_SIGNAL_SOURCES} test/geometry.tide NAME game ${DEAD_SIGNAL_ROOMS}
        HOST test/render_test.c)
    target_link_libraries(dead_signal_render_tests PRIVATE tide_platform)
    if(TIDE_WEB)
        tide_web_page(dead_signal_render_tests SHELL "${PROJECT_SOURCE_DIR}/platform/web/test_shell.html")
        tide_add_web_test(dead_signal_render_tests TARGET dead_signal_render_tests WEBGPU EXPECT "renderer: WebGPU")
        set_property(TEST dead_signal_render_tests dead_signal_render_tests_webgpu APPEND PROPERTY LABELS "private-prototype")
    else()
        add_test(NAME dead_signal_render_tests COMMAND dead_signal_render_tests)
        set_tests_properties(dead_signal_render_tests PROPERTIES TIMEOUT 120 LABELS "gpu;private-prototype")
    endif()
endif()
