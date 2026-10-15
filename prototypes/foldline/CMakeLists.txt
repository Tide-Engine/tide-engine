# Keep the game's sources separate from its C test host and CLI test folder.
file(GLOB foldline_sources CONFIGURE_DEPENDS LIST_DIRECTORIES false
    "${CMAKE_CURRENT_SOURCE_DIR}/*.tide")

# Map output is checked in for the ordinary Tide CLI. A changed source map
# must be regenerated explicitly; neither build may silently use stale output.
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
file(GLOB foldline_maps CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/maps/*.foldmap.json")
set(foldline_map_tool "${CMAKE_CURRENT_SOURCE_DIR}/tools/foldmap.py")
add_custom_target(foldline_maps_source_check
    COMMAND "${Python3_EXECUTABLE}" "${foldline_map_tool}" --check
    VERBATIM)

# The rooms, the operators and the cube are Box3D's: its Tide file before the
# game's and its C with the game's, as tide builds a game whose tide.packages
# lists it (tide_add_game reads none).
set(foldline_box3d "${PROJECT_SOURCE_DIR}/packages/box3d")
set(foldline_packages PACKAGES Box3D "${foldline_box3d}/box3d.tide")
list(APPEND foldline_sources "${foldline_box3d}/bridge.c")

# Every prototype shows its frame rate, and the rest of the stats at a click on it.
tide_add_game(foldline SOURCES ${foldline_sources} TITLE "FOLDLINE" STATS ${foldline_packages})
target_link_libraries(foldline PRIVATE tide_box3d)

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

tide_add_game(foldline_test SOURCES ${foldline_sources} HOST test/test.c ${foldline_packages})
target_link_libraries(foldline_test PRIVATE tide_box3d)
add_test(NAME foldline_test COMMAND foldline_test)

set(foldline_layout_check "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_layout.py")
set(foldline_layout_args --exporter "$<TARGET_FILE:foldline_test>")
if(TIDE_WEB)
    list(APPEND foldline_layout_args --node "${TIDE_NODE}")
endif()
set(foldline_layout_stamp "${CMAKE_CURRENT_BINARY_DIR}/foldline-layout.checked")
add_custom_command(OUTPUT "${foldline_layout_stamp}"
    COMMAND "${Python3_EXECUTABLE}" "${foldline_layout_check}" ${foldline_layout_args}
        --scene "${CMAKE_CURRENT_BINARY_DIR}/foldline-layout.json"
    COMMAND "${CMAKE_COMMAND}" -E touch "${foldline_layout_stamp}"
    DEPENDS foldline_test ${foldline_maps} "${foldline_map_tool}" "${foldline_layout_check}"
    COMMENT "Check Foldline's built doors, cable mounts and connections"
    VERBATIM)
add_custom_target(foldline_maps_validate DEPENDS "${foldline_layout_stamp}")
add_test(NAME foldline_layout
    COMMAND "${Python3_EXECUTABLE}" "${foldline_layout_check}" ${foldline_layout_args}
        --scene "${CMAKE_CURRENT_BINARY_DIR}/foldline-layout-test.json")
add_test(NAME foldline_map_tools
    COMMAND "${Python3_EXECUTABLE}" -m unittest discover -s "${CMAKE_CURRENT_SOURCE_DIR}/tools" -p "test_*.py")

# Exercise the actual destination-camera textures and portal composition.
tide_add_game(foldline_render_test SOURCES ${foldline_sources} HOST test/render_test.c WINDOW ${foldline_packages})
target_link_libraries(foldline_render_test PRIVATE tide_platform tide_box3d)
foreach(foldline_target IN ITEMS foldline foldline_test foldline_render_test)
    add_dependencies(${foldline_target} foldline_maps_source_check)
endforeach()
add_dependencies(foldline foldline_maps_validate)
add_dependencies(foldline_render_test foldline_maps_validate)
if(TIDE_WEB)
    tide_web_page(foldline_render_test SHELL "${PROJECT_SOURCE_DIR}/platform/web/test_shell.html")
    tide_add_web_test(foldline_render_test TARGET foldline_render_test WEBGPU
        EXPECT "FOLDLINE portal rendering checks passed")
else()
    add_test(NAME foldline_render_test COMMAND foldline_render_test)
    set_tests_properties(foldline_render_test PROPERTIES TIMEOUT 120)
    tide_gpu_test(foldline_render_test)
endif()
