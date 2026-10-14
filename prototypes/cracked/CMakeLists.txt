# CRACKED: a port of the owner's Unity game, an opt-in prototype with the
# same native/browser path as the other games. Its folder is what `tide run`
# builds; tide_add_game reads no tide.packages, so rooms is named here.
file(GLOB CRACKED_SOURCES LIST_DIRECTORIES false CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.tide")
list(SORT CRACKED_SOURCES)
set(CRACKED_ROOMS PACKAGES Rooms "${PROJECT_SOURCE_DIR}/packages/rooms/rooms.tide")

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test/test.c")
tide_add_game(cracked_tests SOURCES ${CRACKED_SOURCES} NAME game ${CRACKED_ROOMS}
    HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" test/test.c)
target_include_directories(cracked_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests")
add_test(NAME cracked_tests COMMAND cracked_tests)
set_tests_properties(cracked_tests PROPERTIES TIMEOUT 240 LABELS "private-prototype")
endif()

if(NOT TARGET tide_platform)
    return()
endif()
tide_add_game(cracked SOURCES ${CRACKED_SOURCES} ${CRACKED_ROOMS} STATS)
if(TIDE_WEB)
    set(destination "${PROJECT_SOURCE_DIR}/docs/static/prototypes/cracked")
    add_custom_target(cracked_page
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${destination}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE_DIR:cracked>/cracked.html" "${destination}/index.html"
        DEPENDS cracked VERBATIM)
endif()
