# Opt-in puzzle for one or two; one match implementation on desktop and web.
file(GLOB UNDERFOOT_SOURCES LIST_DIRECTORIES false CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.tide")
list(SORT UNDERFOOT_SOURCES)
set(UNDERFOOT_ROOMS PACKAGES Rooms "${PROJECT_SOURCE_DIR}/packages/rooms/rooms.tide")

tide_add_game(underfoot_tests SOURCES game.tide levels.tide NAME game ${UNDERFOOT_ROOMS}
    HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" test/simulation.c test/campaign.c)
target_include_directories(underfoot_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests")
add_test(NAME underfoot_tests COMMAND underfoot_tests)
set_tests_properties(underfoot_tests PROPERTIES TIMEOUT 300 LABELS "private-prototype")

if(NOT TARGET tide_platform)
    return()
endif()
set(TIDE_WEB_SHELL "${CMAKE_CURRENT_SOURCE_DIR}/web.html")
tide_add_game(underfoot SOURCES ${UNDERFOOT_SOURCES} ${UNDERFOOT_ROOMS} STATS)
if(TIDE_WEB)
    set(destination "${PROJECT_SOURCE_DIR}/docs/static/prototypes/underfoot")
    add_custom_target(underfoot_page
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${destination}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE_DIR:underfoot>/underfoot.html" "${destination}/index.html"
        DEPENDS underfoot VERBATIM)
    if(TIDE_NODE)
        foreach(backend webgl webgpu)
            add_test(NAME underfoot_play_${backend}
                COMMAND "${CMAKE_COMMAND}" -E env "TIDE_BROWSER=${TIDE_BROWSER}" "${TIDE_NODE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/test/browser.mjs"
                    "$<TARGET_FILE_DIR:underfoot>/underfoot.html"
                    "${CMAKE_CURRENT_BINARY_DIR}/captures/${backend}" "${backend}")
            set_tests_properties(underfoot_play_${backend} PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE
                LABELS "private-prototype" SKIP_REGULAR_EXPRESSION "SKIPPED:")
        endforeach()
        set_tests_properties(underfoot_play_webgpu PROPERTIES LABELS "private-prototype;webgpu")
    endif()
endif()
