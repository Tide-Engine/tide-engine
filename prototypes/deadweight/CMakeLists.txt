file(GLOB deadweight_sources CONFIGURE_DEPENDS LIST_DIRECTORIES false "${CMAKE_CURRENT_SOURCE_DIR}/*.tide")
set(deadweight_packages PACKAGES
    Box3D "${PROJECT_SOURCE_DIR}/packages/box3d/box3d.tide"
    Rooms "${PROJECT_SOURCE_DIR}/packages/rooms/rooms.tide")
list(APPEND deadweight_sources "${PROJECT_SOURCE_DIR}/packages/box3d/bridge.c")
set(TIDE_WEB_SHELL "${CMAKE_CURRENT_SOURCE_DIR}/web.html")
tide_add_game(deadweight SOURCES ${deadweight_sources} TITLE "DEADWEIGHT" STATS ${deadweight_packages})
target_link_libraries(deadweight PRIVATE tide_box3d)
tide_add_game(deadweight_test SOURCES ${deadweight_sources} HOST test/simulation.c ${deadweight_packages})
target_link_libraries(deadweight_test PRIVATE tide_box3d)
add_test(NAME deadweight_test COMMAND deadweight_test)
set_tests_properties(deadweight_test PROPERTIES TIMEOUT 120 LABELS "private-prototype")
tide_add_game(deadweight_network_test SOURCES ${deadweight_sources} HOST test/network.c ${deadweight_packages})
target_link_libraries(deadweight_network_test PRIVATE tide_box3d)
add_test(NAME deadweight_network_test COMMAND deadweight_network_test)
set_tests_properties(deadweight_network_test PROPERTIES TIMEOUT 120 LABELS "private-prototype")
tide_add_game(deadweight_render_test SOURCES ${deadweight_sources} HOST test/render.c WINDOW ${deadweight_packages})
target_link_libraries(deadweight_render_test PRIVATE tide_platform tide_box3d)
if(TIDE_WEB)
    tide_web_page(deadweight_render_test SHELL "${PROJECT_SOURCE_DIR}/platform/web/test_shell.html")
    tide_add_web_test(deadweight_render_test TARGET deadweight_render_test WEBGPU
        EXPECT "DEADWEIGHT rendering checks passed")
else()
    add_test(NAME deadweight_render_test COMMAND deadweight_render_test)
    set_tests_properties(deadweight_render_test PROPERTIES TIMEOUT 120 LABELS private-prototype)
    tide_gpu_test(deadweight_render_test)
endif()
if(TIDE_WEB)
    set(destination "${PROJECT_SOURCE_DIR}/docs/static/prototypes/deadweight")
    add_custom_target(deadweight_page
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${destination}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE_DIR:deadweight>/deadweight.html" "${destination}/index.html"
        DEPENDS deadweight VERBATIM)
endif()
