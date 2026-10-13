set(SWINDLE_ROOMS PACKAGES Rooms "${PROJECT_SOURCE_DIR}/packages/rooms/rooms.tide")
tide_add_game(swindle_tests SOURCES game.tide NAME game ${SWINDLE_ROOMS}
    HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" test/simulation.c)
target_include_directories(swindle_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests")
add_test(NAME swindle_tests COMMAND swindle_tests)
set_tests_properties(swindle_tests PROPERTIES TIMEOUT 60 LABELS "private-prototype")
if(NOT TARGET tide_platform)
    return()
endif()
set(TIDE_WEB_SHELL "${CMAKE_CURRENT_SOURCE_DIR}/web.html")
tide_add_game(swindle SOURCES game.tide art.tide ui.tide sound.tide ${SWINDLE_ROOMS} STATS)
if(TIDE_WEB)
    set(destination "${PROJECT_SOURCE_DIR}/docs/static/prototypes/swindle")
    add_custom_target(swindle_page
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${destination}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE_DIR:swindle>/swindle.html" "${destination}/index.html"
        DEPENDS swindle VERBATIM)
    if(TIDE_NODE)
        foreach(backend webgl webgpu)
            add_test(NAME swindle_play_${backend}
                COMMAND "${CMAKE_COMMAND}" -E env "TIDE_BROWSER=${TIDE_BROWSER}" "${TIDE_NODE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/test/browser.mjs"
                    "$<TARGET_FILE_DIR:swindle>/swindle.html"
                    "${CMAKE_CURRENT_BINARY_DIR}/captures/${backend}" "${backend}")
            set_tests_properties(swindle_play_${backend} PROPERTIES TIMEOUT 240 RUN_SERIAL TRUE
                LABELS "private-prototype" SKIP_REGULAR_EXPRESSION "SKIPPED:")
        endforeach()
        set_tests_properties(swindle_play_webgpu PROPERTIES LABELS "private-prototype;webgpu")
    endif()
endif()
