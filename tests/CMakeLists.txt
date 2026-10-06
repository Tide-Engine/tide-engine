# New .c files in this folder are picked up automatically on the next build.
file(GLOB TIDE_TEST_SOURCES CONFIGURE_DEPENDS *.c)

# The PNG reader's tests read what the platform layer's writer wrote: the
# writer's one file comes along, with no window or GPU in it.
add_executable(tide_tests ${TIDE_TEST_SOURCES} "${PROJECT_SOURCE_DIR}/platform/src/png.c")
target_include_directories(tide_tests PRIVATE "${PROJECT_SOURCE_DIR}/platform/src")
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

# The calls a system written in C can get wrong (tide/rows.h) stop the program,
# each saying which function it was, in which system, and what to write
# instead: a program that makes one of them, and what it has to say.
add_executable(tide_rows_stops stops/rows_stops.c)
target_link_libraries(tide_rows_stops PRIVATE tide)
function(tide_rows_stop call says)
    add_test(NAME tide_rows_stops_${call} COMMAND tide_rows_stops ${call})
    set_tests_properties(tide_rows_stops_${call} PROPERTIES TIMEOUT 30 PASS_REGULAR_EXPRESSION "${says}")
endfunction()
tide_rows_stop(component "tide_rows_floats, in the system 'Game.Move': it takes 3 components, and this asks for component 3")
tide_rows_stop(none "tide_rows_ints, in the system 'Game.Step': it takes no components")
tide_rows_stop(narrow "tide_rows_ints, in the system 'Game.Move': component 2 is 6 bytes, which isn't a multiple of 4.*tide_rows_get and tide_rows_set")
tide_rows_stop(offset "tide_rows_floats, in the system 'Game.Move': offset 2 isn't where a 4-byte field of component 0 .8 bytes. starts.*offsetof")
tide_rows_stop(past "tide_rows_floats_mut, in the system 'Game.Move': offset 8 isn't where a 4-byte field of component 1")
tide_rows_stop(mut "tide_rows_floats_mut, in the system 'Game.Move': component 0 isn't 'mut' there.*write 'mut' before it")
tide_rows_stop(set "tide_rows_set, in the system 'Game.Move': component 0 isn't 'mut' there")
tide_rows_stop(row "tide_rows_get, in the system 'Game.Move': row 4 is past the run's 4 rows")
