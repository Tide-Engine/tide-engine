set(newton_package "${PROJECT_SOURCE_DIR}/packages/newton")
set(newton_packages PACKAGES Newton "${newton_package}/newton.tide"
    Rooms "${PROJECT_SOURCE_DIR}/packages/rooms/rooms.tide")

if(TARGET tide_platform)
    tide_add_game(newton_arm SOURCES arm.tide "${newton_package}/bridge.c" ${newton_packages})
    target_link_libraries(newton_arm PRIVATE tide_newton)
endif()

tide_add_game(tide_newton_robot SOURCES arm.tide "${newton_package}/bridge.c" NAME robot ${newton_packages}
    HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" "${newton_package}/test/robot_test.c")
target_include_directories(tide_newton_robot PRIVATE "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(tide_newton_robot PRIVATE tide_newton)
if(TARGET tide_platform)
    target_compile_definitions(tide_newton_robot PRIVATE NEWTON_TEST_JOBS)
    target_link_libraries(tide_newton_robot PRIVATE tide_platform)
endif()
add_test(NAME tide_newton_robot COMMAND tide_newton_robot)
set_tests_properties(tide_newton_robot PROPERTIES TIMEOUT 120)

tide_add_game(tide_newton_join SOURCES arm.tide "${newton_package}/bridge.c" NAME robot ${newton_packages}
    HOST "${newton_package}/test/join.c")
target_link_libraries(tide_newton_join PRIVATE tide_newton)
add_test(NAME tide_newton_join
    COMMAND "${CMAKE_COMMAND}" "-DPROGRAM=$<TARGET_FILE:tide_newton_join>"
        "-DEMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}" "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/join"
        -P "${newton_package}/test/join.cmake")
set_tests_properties(tide_newton_join PROPERTIES TIMEOUT 120)
