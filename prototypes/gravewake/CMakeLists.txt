# Opt-in medieval zombie prototype; one match implementation on desktop and web.
file(GLOB GRAVEWAKE_SOURCES LIST_DIRECTORIES false CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.tide")
list(SORT GRAVEWAKE_SOURCES)
set(GRAVEWAKE_ROOMS PACKAGES Rooms "${PROJECT_SOURCE_DIR}/packages/rooms/rooms.tide")

# Rounds played through with no window: survivors fighting, buying and reviving.
tide_add_game(gravewake_test SOURCES ${GRAVEWAKE_SOURCES} ${GRAVEWAKE_ROOMS} HOST test/test.c)
add_test(NAME gravewake_test COMMAND gravewake_test)
set_tests_properties(gravewake_test PROPERTIES TIMEOUT 300 LABELS "private-prototype")
# A tick's cost with a horde, to run from a release build: not a test.
tide_add_game(gravewake_perf SOURCES ${GRAVEWAKE_SOURCES} ${GRAVEWAKE_ROOMS} HOST test/perf.c)
# Whole runs played by bots, for the survivors' balance and the items' pace: not a test.
tide_add_game(gravewake_balance SOURCES ${GRAVEWAKE_SOURCES} ${GRAVEWAKE_ROOMS} HOST test/balance.c)

if(NOT TARGET tide_platform)
    return()
endif()
# Every prototype shows its frame rate, and the rest of the stats at a click on it.
tide_add_game(gravewake SOURCES ${GRAVEWAKE_SOURCES} TITLE "GRAVEWAKE" ${GRAVEWAKE_ROOMS} STATS)

if(TIDE_WEB)
    set(gravewake_page_dir "${PROJECT_SOURCE_DIR}/docs/static/prototypes/gravewake")
    add_custom_target(gravewake_page
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${gravewake_page_dir}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE_DIR:gravewake>/gravewake.html" "${gravewake_page_dir}/index.html"
        DEPENDS gravewake
        COMMENT "Stage the public GRAVEWAKE prototype"
        VERBATIM)
    if(TIDE_NODE)
        foreach(backend webgl webgpu)
            add_test(NAME gravewake_play_${backend}
                COMMAND "${CMAKE_COMMAND}" -E env "TIDE_BROWSER=${TIDE_BROWSER}" "${TIDE_NODE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/test/browser.mjs"
                    "$<TARGET_FILE_DIR:gravewake>/gravewake.html"
                    "${CMAKE_CURRENT_BINARY_DIR}/captures/${backend}" "${backend}")
            set_tests_properties(gravewake_play_${backend} PROPERTIES TIMEOUT 900 RUN_SERIAL TRUE
                LABELS "private-prototype" SKIP_REGULAR_EXPRESSION "SKIPPED:")
        endforeach()
        set_tests_properties(gravewake_play_webgpu PROPERTIES LABELS "private-prototype;webgpu")
    endif()
endif()
