set(rooms PACKAGES Rooms "${PROJECT_SOURCE_DIR}/packages/rooms/rooms.tide")
file(GLOB sources LIST_DIRECTORIES false CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.tide")
list(SORT sources)
tide_add_game(last_light_tests SOURCES game.tide NAME game ${rooms}
    HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" test/last_light_test.c)
target_include_directories(last_light_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests")
if(TIDE_WEB)
    add_test(NAME last_light_tests COMMAND "${TIDE_NODE}" "${PROJECT_SOURCE_DIR}/cmake/run_wasi.mjs" "$<TARGET_FILE:last_light_tests>")
else()
    add_test(NAME last_light_tests COMMAND last_light_tests)
endif()
set_tests_properties(last_light_tests PROPERTIES TIMEOUT 60)
if(TIDE_NODE)
    add_test(NAME last_light_browser_process
        COMMAND "${TIDE_NODE}" --test "${CMAKE_CURRENT_SOURCE_DIR}/test/browser-process.test.mjs")
    # Windows fault cases each exercise several real two-second cleanup waits.
    set_tests_properties(last_light_browser_process PROPERTIES TIMEOUT 60)
    add_test(NAME last_light_frame_pacing
        COMMAND "${TIDE_NODE}" --test "${CMAKE_CURRENT_SOURCE_DIR}/test/frame-pacing.test.mjs")
    set_tests_properties(last_light_frame_pacing PROPERTIES TIMEOUT 30)
    add_test(NAME last_light_browser_devtools
        COMMAND "${TIDE_NODE}" --test "${CMAKE_CURRENT_SOURCE_DIR}/test/browser-devtools.test.mjs")
    set_tests_properties(last_light_browser_devtools PROPERTIES TIMEOUT 30)
endif()
if(NOT TARGET tide_platform)
    return()
endif()
tide_add_game(last_light SOURCES ${sources} ${rooms})
if(TIDE_WEB)
    set(destination "${PROJECT_SOURCE_DIR}/docs/static/prototypes/the-last-light")
    add_custom_target(last_light_page
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${destination}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE_DIR:last_light>/last_light.html" "${destination}/index.html"
        DEPENDS last_light VERBATIM)
    foreach(backend webgl webgpu)
        add_test(NAME last_light_play_${backend}
            COMMAND "${CMAKE_COMMAND}" -E env "TIDE_BROWSER=${TIDE_BROWSER}" "${TIDE_NODE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/test/web-play.mjs"
                "$<TARGET_FILE_DIR:last_light>/last_light.html"
                "${CMAKE_CURRENT_BINARY_DIR}/captures/${backend}" "${backend}")
        # Each playtest already renders two players in software and needs
        # the runner's CPU/GPU budget for timely frames and match packets.
        set_tests_properties(last_light_play_${backend} PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE
            SKIP_REGULAR_EXPRESSION "SKIPPED:")
    endforeach()
    set_tests_properties(last_light_play_webgpu PROPERTIES LABELS webgpu)
endif()
