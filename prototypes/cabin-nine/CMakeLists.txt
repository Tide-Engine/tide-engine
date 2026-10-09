# Opt-in private game. Browser staging stays behind the site's admin guard.
set(CABIN_NINE_SOURCES game.tide hud.tide show.tide story.tide)
set(CABIN_NINE_ROOMS PACKAGES Rooms "${PROJECT_SOURCE_DIR}/packages/rooms/rooms.tide")

tide_add_game(cabin_nine SOURCES ${CABIN_NINE_SOURCES} ${CABIN_NINE_ROOMS})
if(TIDE_WEB)
    set(destination "${PROJECT_SOURCE_DIR}/docs/static/admin/prototypes/cabin-nine")
    add_custom_target(cabin_nine_page
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${destination}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE_DIR:cabin_nine>/cabin_nine.html" "${destination}/index.html"
        DEPENDS cabin_nine VERBATIM)
endif()
tide_add_game(cabin_nine_tests SOURCES ${CABIN_NINE_SOURCES} NAME cabin ${CABIN_NINE_ROOMS}
    HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" test/smoke.c)
target_include_directories(cabin_nine_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests")
add_test(NAME cabin_nine_tests COMMAND cabin_nine_tests)
set_tests_properties(cabin_nine_tests PROPERTIES TIMEOUT 60 LABELS "private-prototype")

tide_add_game(cabin_nine_render SOURCES ${CABIN_NINE_SOURCES} NAME game ${CABIN_NINE_ROOMS}
    HOST test/render.c WINDOW)
target_link_libraries(cabin_nine_render PRIVATE tide_platform)
if(TIDE_WEB)
    tide_web_page(cabin_nine_render SHELL "${PROJECT_SOURCE_DIR}/platform/web/test_shell.html")
    tide_add_web_test(cabin_nine_render_web TARGET cabin_nine_render WEBGPU EXPECT "Cabin render passed")
else()
    add_test(NAME cabin_nine_render COMMAND cabin_nine_render)
    set_tests_properties(cabin_nine_render PROPERTIES TIMEOUT 60 LABELS "gpu;private-prototype")
endif()
