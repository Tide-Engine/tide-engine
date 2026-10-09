# Each sample is a self-contained tide run folder, whose tide.packages lists
# rooms: the menu that comes with tide. tide_add_game reads no tide.packages,
# so the package is named here, as tide builds a game that lists it. A
# sample's Tide files are all of its folder's, in the order of their names, as
# tide compiles a folder's: one file for most, and several for Tag and Two
# Forks.
set(SAMPLE_ROOMS PACKAGES Rooms "${PROJECT_SOURCE_DIR}/packages/rooms/rooms.tide")

# Two Forks' test: its rules, and every position of every level, with no
# window. It's in a folder of its own, with a tide.packages, so that `tide run`
# in the game's folder leaves its C out of the game.
file(GLOB two_forks_sources LIST_DIRECTORIES false CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/two-forks/*.tide")
list(SORT two_forks_sources)
tide_add_game(two_forks_tests SOURCES ${two_forks_sources} NAME game ${SAMPLE_ROOMS}
    HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" two-forks/test/two_forks_test.c
         two-forks/test/progression_test.c)
target_include_directories(two_forks_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests")
add_test(NAME two_forks_tests COMMAND two_forks_tests)
set_tests_properties(two_forks_tests PROPERTIES TIMEOUT 300)

if(NOT TARGET tide_platform)
    return()
endif()

add_custom_target(sample_games)
foreach(id pong snake tanks bomberman connect-four asteroids sumo tower-defense marbles tag two-forks)
    string(REPLACE "-" "_" name "${id}")
    set(target "sample_${name}")
    file(GLOB sources LIST_DIRECTORIES false CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${id}/*.tide")
    list(SORT sources)
    tide_add_game(${target} SOURCES ${sources} ${SAMPLE_ROOMS})
    if(TIDE_WEB)
        # Stage even an unchanged build, so clearing docs/static is recoverable.
        set(destination "${PROJECT_SOURCE_DIR}/docs/static/samples/${id}")
        if(id STREQUAL "two-forks")
            set(destination "${PROJECT_SOURCE_DIR}/docs/static/admin/prototypes/two-forks")
        endif()
        add_custom_target(${target}_page
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${destination}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE_DIR:${target}>/${target}.html" "${destination}/index.html"
            DEPENDS ${target}
            COMMENT "Staging the ${id} sample game"
            VERBATIM)
        add_dependencies(sample_games ${target}_page)
    else()
        add_dependencies(sample_games ${target})
    endif()
endforeach()
