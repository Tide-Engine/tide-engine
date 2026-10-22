set(newton_package "${PROJECT_SOURCE_DIR}/packages/newton")
set(microduck_sources model.tide microduck.tide visuals.tide "${newton_package}/bridge.c")
set(microduck_packages PACKAGES Newton "${newton_package}/newton.tide"
    Rooms "${PROJECT_SOURCE_DIR}/packages/rooms/rooms.tide")

if(TARGET tide_platform)
    tide_add_game(newton_microduck SOURCES ${microduck_sources} ${microduck_packages})
    target_link_libraries(newton_microduck PRIVATE tide_newton)
endif()

tide_add_game(tide_newton_microduck SOURCES ${microduck_sources} NAME microduck ${microduck_packages}
    HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" "${newton_package}/test/microduck_test.c")
target_include_directories(tide_newton_microduck PRIVATE "${PROJECT_SOURCE_DIR}/tests"
    "${CMAKE_CURRENT_SOURCE_DIR}/test")
target_link_libraries(tide_newton_microduck PRIVATE tide_newton)
if(TARGET tide_platform)
    target_compile_definitions(tide_newton_microduck PRIVATE NEWTON_TEST_JOBS)
    target_link_libraries(tide_newton_microduck PRIVATE tide_platform)
endif()
add_test(NAME tide_newton_microduck COMMAND tide_newton_microduck)
set_tests_properties(tide_newton_microduck PROPERTIES TIMEOUT 180)

tide_add_game(tide_newton_microduck_join SOURCES ${microduck_sources} NAME microduck ${microduck_packages}
    HOST "${newton_package}/test/microduck_join.c")
target_link_libraries(tide_newton_microduck_join PRIVATE tide_newton)
add_test(NAME tide_newton_microduck_join
    COMMAND "${CMAKE_COMMAND}" "-DPROGRAM=$<TARGET_FILE:tide_newton_microduck_join>"
        "-DEMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}" "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/join"
        -P "${newton_package}/test/join.cmake")
set_tests_properties(tide_newton_microduck_join PROPERTIES TIMEOUT 120)
