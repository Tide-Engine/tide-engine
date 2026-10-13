tide_add_game(skyworks SOURCES world.tide look.tide TITLE "SKYWORKS | A factory above the clouds")
tide_add_game(skyworks_test SOURCES world.tide look.tide HOST test/skyworks_test.c NAME skyworks)
add_test(NAME skyworks_simulation COMMAND skyworks_test)
tide_add_game(skyworks_render SOURCES world.tide look.tide HOST test/render.c NAME skyworks)
target_link_libraries(skyworks_render PRIVATE tide_platform)
if(TIDE_WEB)
    add_custom_target(skyworks_page
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_SOURCE_DIR}/docs/static/prototypes/skyworks"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE_DIR:skyworks>/skyworks.html" "${PROJECT_SOURCE_DIR}/docs/static/prototypes/skyworks/index.html"
        DEPENDS skyworks)
    tide_web_page(skyworks_render SHELL "${PROJECT_SOURCE_DIR}/platform/web/test_shell.html")
    tide_add_web_test(skyworks_render TARGET skyworks_render WEBGPU EXPECT "SKYWORKS render passed")
else()
    add_test(NAME skyworks_render COMMAND skyworks_render --frames 4)
    set_tests_properties(skyworks_render PROPERTIES LABELS gpu)
endif()
