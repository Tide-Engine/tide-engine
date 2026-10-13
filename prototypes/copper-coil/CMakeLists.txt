file(GLOB COPPER_COIL_SOURCES LIST_DIRECTORIES false CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.tide")
list(SORT COPPER_COIL_SOURCES)
set(COPPER_COIL_ROOMS PACKAGES Rooms "${PROJECT_SOURCE_DIR}/packages/rooms/rooms.tide")
tide_add_game(copper_coil_tests SOURCES game.tide levels.tide NAME game ${COPPER_COIL_ROOMS}
    HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" test/simulation.c)
target_include_directories(copper_coil_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests")
add_test(NAME copper_coil_tests COMMAND copper_coil_tests)
set_tests_properties(copper_coil_tests PROPERTIES LABELS "private-prototype")
if(NOT TARGET tide_platform)
    return()
endif()
set(TIDE_WEB_SHELL "${CMAKE_CURRENT_SOURCE_DIR}/web.html")
tide_add_game(copper_coil SOURCES ${COPPER_COIL_SOURCES} ${COPPER_COIL_ROOMS} STATS)
if(TIDE_WEB)
    set(destination "${PROJECT_SOURCE_DIR}/docs/static/prototypes/copper-coil")
    add_custom_target(copper_coil_page
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${destination}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE_DIR:copper_coil>/copper_coil.html" "${destination}/index.html"
        DEPENDS copper_coil VERBATIM)
    if(TIDE_NODE)
        foreach(backend webgl webgpu)
            add_test(NAME copper_coil_play_${backend}
                COMMAND "${CMAKE_COMMAND}" -E env "TIDE_BROWSER=${TIDE_BROWSER}" "${TIDE_NODE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/test/browser.mjs"
                    "$<TARGET_FILE_DIR:copper_coil>/copper_coil.html"
                    "${CMAKE_CURRENT_BINARY_DIR}/captures/${backend}" "${backend}")
            set_tests_properties(copper_coil_play_${backend} PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE
                LABELS "private-prototype" SKIP_REGULAR_EXPRESSION "SKIPPED:")
        endforeach()
        set_tests_properties(copper_coil_play_webgpu PROPERTIES LABELS "private-prototype;webgpu")
    endif()
endif()
