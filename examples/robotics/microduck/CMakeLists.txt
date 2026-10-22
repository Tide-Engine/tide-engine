set(newton_package "${PROJECT_SOURCE_DIR}/packages/newton")
set(microduck_sources model.tide microduck.tide visuals.tide sandbox.tide "${newton_package}/bridge.c")
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
# These long simulation/rollback cases need independent CTest budgets on the
# Android emulator, rather than sharing one deadline for the whole executable.
set(microduck_test_source "${newton_package}/test/microduck_test.c")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${microduck_test_source}")
file(STRINGS "${microduck_test_source}" microduck_cases REGEX "^TIDE_TEST\\([A-Za-z0-9_]+\\)$")
if(NOT microduck_cases)
    message(FATAL_ERROR "No MicroDuck test cases found in ${microduck_test_source}")
endif()
foreach(declaration IN LISTS microduck_cases)
    string(REGEX REPLACE "^TIDE_TEST\\(([A-Za-z0-9_]+)\\)$" "\\1" case "${declaration}")
    if(case STREQUAL "microduck_threaded_ticks_match_serial_worlds" AND NOT TARGET tide_platform)
        continue()
    endif()
    add_test(NAME tide_newton_${case} COMMAND tide_newton_microduck "${case}")
    set_tests_properties(tide_newton_${case} PROPERTIES TIMEOUT 180 PASS_REGULAR_EXPRESSION "1/1 passed")
endforeach()

tide_add_game(tide_newton_microduck_join SOURCES ${microduck_sources} NAME microduck ${microduck_packages}
    HOST "${newton_package}/test/microduck_join.c")
target_link_libraries(tide_newton_microduck_join PRIVATE tide_newton)
add_test(NAME tide_newton_microduck_join
    COMMAND "${CMAKE_COMMAND}" "-DPROGRAM=$<TARGET_FILE:tide_newton_microduck_join>"
        "-DEMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}" "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/join"
        -P "${newton_package}/test/join.cmake")
set_tests_properties(tide_newton_microduck_join PROPERTIES TIMEOUT 120)
