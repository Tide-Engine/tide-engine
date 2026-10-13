file(GLOB hot_swap_sources CONFIGURE_DEPENDS LIST_DIRECTORIES false "${CMAKE_CURRENT_SOURCE_DIR}/*.tide")
set(hot_swap_packages PACKAGES Rooms "${PROJECT_SOURCE_DIR}/packages/rooms/rooms.tide")
tide_add_game(hot_swap_tests SOURCES ${hot_swap_sources} ${hot_swap_packages}
    NAME game HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" test/simulation.c)
target_include_directories(hot_swap_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests")
add_test(NAME hot_swap_tests COMMAND hot_swap_tests)
set_tests_properties(hot_swap_tests PROPERTIES LABELS private-prototype TIMEOUT 180)
if(NOT TARGET tide_platform)
    return()
endif()
set(TIDE_WEB_SHELL "${CMAKE_CURRENT_SOURCE_DIR}/web.html")
tide_add_game(hot_swap SOURCES ${hot_swap_sources} ${hot_swap_packages} TITLE "HOT SWAP" STATS)
if(TIDE_WEB)
    set(destination "${PROJECT_SOURCE_DIR}/docs/static/prototypes/hot-swap")
    add_custom_target(hot_swap_page
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${destination}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE_DIR:hot_swap>/hot_swap.html" "${destination}/index.html"
        DEPENDS hot_swap VERBATIM)
    if(TIDE_NODE)
        foreach(backend webgl webgpu)
            add_test(NAME hot_swap_play_${backend}
                COMMAND "${CMAKE_COMMAND}" -E env "TIDE_BROWSER=${TIDE_BROWSER}" "${TIDE_NODE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/test/browser.mjs" "$<TARGET_FILE_DIR:hot_swap>/hot_swap.html"
                    "${CMAKE_CURRENT_BINARY_DIR}/captures" "${backend}")
            set_tests_properties(hot_swap_play_${backend} PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE
                LABELS private-prototype SKIP_REGULAR_EXPRESSION "SKIPPED:")
        endforeach()
        set_tests_properties(hot_swap_play_webgpu PROPERTIES LABELS "private-prototype;webgpu")
    endif()
endif()
