# Opt-in co-op horror prototype; one match implementation on desktop and web.
file(GLOB SUNDAY_SUPPER_SOURCES LIST_DIRECTORIES false CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.tide")
list(SORT SUNDAY_SUPPER_SOURCES)
set(SUNDAY_SUPPER_ROOMS PACKAGES Rooms "${PROJECT_SOURCE_DIR}/packages/rooms/rooms.tide")

# The story played through with no window: both travellers, from the road to the barn.
tide_add_game(sunday_supper_test SOURCES ${SUNDAY_SUPPER_SOURCES} ${SUNDAY_SUPPER_ROOMS} HOST test/test.c)
add_test(NAME sunday_supper_test COMMAND sunday_supper_test)
set_tests_properties(sunday_supper_test PROPERTIES TIMEOUT 300 LABELS "private-prototype")

if(NOT TARGET tide_platform)
    return()
endif()
# Every prototype shows its frame rate, and the rest of the stats at a click on it.
tide_add_game(sunday_supper SOURCES ${SUNDAY_SUPPER_SOURCES} TITLE "SUNDAY SUPPER" ${SUNDAY_SUPPER_ROOMS} STATS)

if(TIDE_WEB)
    set(sunday_supper_page_dir "${PROJECT_SOURCE_DIR}/docs/static/prototypes/sunday-supper")
    add_custom_target(sunday_supper_page
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${sunday_supper_page_dir}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE_DIR:sunday_supper>/sunday_supper.html" "${sunday_supper_page_dir}/index.html"
        DEPENDS sunday_supper
        COMMENT "Stage the public SUNDAY SUPPER prototype"
        VERBATIM)
    if(TIDE_NODE)
        foreach(backend webgl webgpu)
            add_test(NAME sunday_supper_play_${backend}
                COMMAND "${CMAKE_COMMAND}" -E env "TIDE_BROWSER=${TIDE_BROWSER}" "${TIDE_NODE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/test/browser.mjs"
                    "$<TARGET_FILE_DIR:sunday_supper>/sunday_supper.html"
                    "${CMAKE_CURRENT_BINARY_DIR}/captures/${backend}" "${backend}")
            set_tests_properties(sunday_supper_play_${backend} PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE
                LABELS "private-prototype" SKIP_REGULAR_EXPRESSION "SKIPPED:")
        endforeach()
        set_tests_properties(sunday_supper_play_webgpu PROPERTIES LABELS "private-prototype;webgpu")
    endif()
endif()
