# Opt-in car soccer prototype, with the same native/browser path as the other games.
file(GLOB ROCKET_LEAGUE_SOURCES LIST_DIRECTORIES false CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.tide")
list(SORT ROCKET_LEAGUE_SOURCES)
set(ROCKET_LEAGUE_ROOMS PACKAGES Rooms "${PROJECT_SOURCE_DIR}/packages/rooms/rooms.tide")

tide_add_game(rocket_league_tests SOURCES ${ROCKET_LEAGUE_SOURCES} NAME game ${ROCKET_LEAGUE_ROOMS}
    HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" test/test.c)
target_include_directories(rocket_league_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests")
add_test(NAME rocket_league_tests COMMAND rocket_league_tests)
set_tests_properties(rocket_league_tests PROPERTIES TIMEOUT 120 LABELS "private-prototype")

if(NOT TARGET tide_platform)
    return()
endif()
tide_add_game(rocket_league SOURCES ${ROCKET_LEAGUE_SOURCES} ${ROCKET_LEAGUE_ROOMS})
if(TIDE_WEB)
    set(destination "${PROJECT_SOURCE_DIR}/docs/static/prototypes/rocket-league")
    add_custom_target(rocket_league_page
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${destination}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE_DIR:rocket_league>/rocket_league.html" "${destination}/index.html"
        DEPENDS rocket_league VERBATIM)
    foreach(backend webgl webgpu)
        add_test(NAME rocket_league_play_${backend}
            COMMAND "${CMAKE_COMMAND}" -E env "TIDE_BROWSER=${TIDE_BROWSER}" "${TIDE_NODE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/test/browser_test.mjs"
                "$<TARGET_FILE_DIR:rocket_league>/rocket_league.html"
                "${CMAKE_CURRENT_BINARY_DIR}/captures/${backend}" "${backend}")
        set_tests_properties(rocket_league_play_${backend} PROPERTIES TIMEOUT 180
            LABELS "private-prototype" SKIP_REGULAR_EXPRESSION "SKIPPED:")
    endforeach()
    set_property(TEST rocket_league_play_webgpu APPEND PROPERTY LABELS "webgpu")
endif()

if(NOT TIDE_ANDROID)
    tide_add_game(rocket_league_render SOURCES ${ROCKET_LEAGUE_SOURCES} NAME game ${ROCKET_LEAGUE_ROOMS}
        HOST test/render.c WINDOW)
    target_link_libraries(rocket_league_render PRIVATE tide_platform)
    if(TIDE_WEB)
        tide_web_page(rocket_league_render SHELL "${PROJECT_SOURCE_DIR}/platform/web/test_shell.html")
        tide_add_web_test(rocket_league_render TARGET rocket_league_render WEBGPU EXPECT "AFTERBURN_RENDER_PASS")
        set_property(TEST rocket_league_render rocket_league_render_webgpu APPEND PROPERTY LABELS "private-prototype")
    else()
        add_test(NAME rocket_league_render COMMAND rocket_league_render)
        set_tests_properties(rocket_league_render PROPERTIES TIMEOUT 90 LABELS "gpu;private-prototype")
    endif()
endif()
