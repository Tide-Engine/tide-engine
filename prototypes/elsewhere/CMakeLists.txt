file(GLOB elsewhere_sources CONFIGURE_DEPENDS LIST_DIRECTORIES false "${CMAKE_CURRENT_SOURCE_DIR}/*.tide")
set(TIDE_WEB_SHELL "${CMAKE_CURRENT_SOURCE_DIR}/shell.html")
tide_add_game(elsewhere SOURCES ${elsewhere_sources} TITLE "Elsewhere" STATS)
if(TIDE_WEB)
    set(elsewhere_page_dir "${PROJECT_SOURCE_DIR}/docs/static/prototypes/elsewhere")
    add_custom_target(elsewhere_page
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${elsewhere_page_dir}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE_DIR:elsewhere>/elsewhere.html" "${elsewhere_page_dir}/index.html"
        DEPENDS elsewhere VERBATIM)
endif()
tide_add_game(elsewhere_test SOURCES ${elsewhere_sources} HOST test/test.c)
add_test(NAME elsewhere_test COMMAND elsewhere_test)
tide_add_game(elsewhere_render_test SOURCES ${elsewhere_sources} HOST test/render.c WINDOW)
target_link_libraries(elsewhere_render_test PRIVATE tide_platform)
if(TIDE_WEB)
    tide_web_page(elsewhere_render_test SHELL "${PROJECT_SOURCE_DIR}/platform/web/test_shell.html")
    tide_add_web_test(elsewhere_render_test TARGET elsewhere_render_test WEBGPU EXPECT "Elsewhere rendering checks passed")
else()
    add_test(NAME elsewhere_render_test COMMAND elsewhere_render_test)
    set_tests_properties(elsewhere_render_test PROPERTIES LABELS gpu TIMEOUT 120)
endif()
