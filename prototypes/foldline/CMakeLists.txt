# Keep the game's sources separate from its C test host and CLI test folder.
file(GLOB foldline_sources CONFIGURE_DEPENDS LIST_DIRECTORIES false
    "${CMAKE_CURRENT_SOURCE_DIR}/*.tide")

# Every prototype shows its frame rate, and the rest of the stats at a click on it.
tide_add_game(foldline SOURCES ${foldline_sources} TITLE "FOLDLINE" STATS)

if(TIDE_WEB)
    set(foldline_page_dir "${PROJECT_SOURCE_DIR}/docs/static/prototypes/foldline")
    add_custom_target(foldline_page
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${foldline_page_dir}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE_DIR:foldline>/foldline.html" "${foldline_page_dir}/index.html"
        DEPENDS foldline
        COMMENT "Stage the public FOLDLINE prototype"
        VERBATIM)
endif()

tide_add_game(foldline_test SOURCES ${foldline_sources} HOST test/test.c)
add_test(NAME foldline_test COMMAND foldline_test)

# Exercise the actual destination-camera textures and portal composition.
tide_add_game(foldline_render_test SOURCES ${foldline_sources} HOST test/render_test.c WINDOW)
target_link_libraries(foldline_render_test PRIVATE tide_platform)
if(TIDE_WEB)
    tide_web_page(foldline_render_test SHELL "${PROJECT_SOURCE_DIR}/platform/web/test_shell.html")
    tide_add_web_test(foldline_render_test TARGET foldline_render_test WEBGPU
        EXPECT "FOLDLINE portal rendering checks passed")
else()
    add_test(NAME foldline_render_test COMMAND foldline_render_test)
    set_tests_properties(foldline_render_test PROPERTIES LABELS gpu TIMEOUT 120)
endif()
