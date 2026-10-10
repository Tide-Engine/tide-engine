# Opt-in car soccer prototype, with the same native/browser path as the other games.
file(GLOB AERIAL_SOURCES LIST_DIRECTORIES false CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.tide")
list(SORT AERIAL_SOURCES)
set(AERIAL_ROOMS PACKAGES Rooms "${PROJECT_SOURCE_DIR}/packages/rooms/rooms.tide")

tide_add_game(aerial_tests SOURCES ${AERIAL_SOURCES} NAME game ${AERIAL_ROOMS}
    HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" test/test.c)
target_include_directories(aerial_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests")
add_test(NAME aerial_tests COMMAND aerial_tests)
set_tests_properties(aerial_tests PROPERTIES TIMEOUT 240 LABELS "private-prototype")

if(NOT TARGET tide_platform)
    return()
endif()
tide_add_game(aerial SOURCES ${AERIAL_SOURCES} ${AERIAL_ROOMS})
if(TIDE_WEB)
    set(destination "${PROJECT_SOURCE_DIR}/docs/static/prototypes/aerial")
    add_custom_target(aerial_page
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${destination}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE_DIR:aerial>/aerial.html" "${destination}/index.html"
        DEPENDS aerial VERBATIM)
    foreach(backend webgl webgpu)
        add_test(NAME aerial_play_${backend}
            COMMAND "${CMAKE_COMMAND}" -E env "TIDE_BROWSER=${TIDE_BROWSER}" "${TIDE_NODE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/test/browser_test.mjs"
                "$<TARGET_FILE_DIR:aerial>/aerial.html"
                "${CMAKE_CURRENT_BINARY_DIR}/captures/${backend}" "${backend}")
        set_tests_properties(aerial_play_${backend} PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE
            LABELS "private-prototype" SKIP_REGULAR_EXPRESSION "SKIPPED:")
    endforeach()
    set_property(TEST aerial_play_webgpu APPEND PROPERTY LABELS "webgpu")
endif()

if(NOT TIDE_ANDROID)
    tide_add_game(aerial_render SOURCES ${AERIAL_SOURCES} NAME game ${AERIAL_ROOMS}
        HOST test/render.c WINDOW)
    target_link_libraries(aerial_render PRIVATE tide_platform)
    if(TIDE_WEB)
        tide_web_page(aerial_render SHELL "${PROJECT_SOURCE_DIR}/platform/web/test_shell.html")
        tide_add_web_test(aerial_render TARGET aerial_render WEBGPU EXPECT "AERIAL_RENDER_PASS")
        set_property(TEST aerial_render aerial_render_webgpu APPEND PROPERTY LABELS "private-prototype")
    else()
        add_test(NAME aerial_render COMMAND aerial_render)
        set_tests_properties(aerial_render PROPERTIES TIMEOUT 90 LABELS "gpu;private-prototype")
    endif()
endif()
