set(night_route_sources game.tide show.tide)
tide_add_game(night_route_tests SOURCES ${night_route_sources} NAME night_route_rules
    HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" test/simulation.c)
target_include_directories(night_route_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests")
add_test(NAME night_route_tests COMMAND night_route_tests)
set_tests_properties(night_route_tests PROPERTIES LABELS "private-prototype" TIMEOUT 120)
if(NOT TARGET tide_platform)
    return()
endif()
set(TIDE_WEB_SHELL "${CMAKE_CURRENT_SOURCE_DIR}/web.html")
tide_add_game(night_route SOURCES ${night_route_sources} STATS TITLE "NIGHT ROUTE")
if(TIDE_WEB)
    set(destination "${PROJECT_SOURCE_DIR}/docs/static/prototypes/night-route")
    add_custom_target(night_route_page
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${destination}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE_DIR:night_route>/night_route.html" "${destination}/index.html"
        DEPENDS night_route VERBATIM)
endif()
