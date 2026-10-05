# New .c files in this folder are picked up automatically on the next build.
file(GLOB TIDE_TEST_SOURCES CONFIGURE_DEPENDS *.c)

add_executable(tide_tests ${TIDE_TEST_SOURCES})
target_link_libraries(tide_tests PRIVATE tide)

add_test(NAME tide_tests COMMAND tide_tests)

# A run nobody watches (TIDE_HIDDEN, see tide/page.h): what the variable does
# to a program with no window, with it set and with it empty, whichever the
# one who runs the tests has.
if(NOT TIDE_WEB AND NOT TIDE_ANDROID)
    add_test(NAME tide_tests_hidden COMMAND tide_tests hidden_is_what_the_environment_says)
    set_tests_properties(tide_tests_hidden PROPERTIES ENVIRONMENT TIDE_HIDDEN=1)
    add_test(NAME tide_tests_watched COMMAND tide_tests hidden_is_what_the_environment_says)
    set_tests_properties(tide_tests_watched PROPERTIES ENVIRONMENT TIDE_HIDDEN=)
endif()
