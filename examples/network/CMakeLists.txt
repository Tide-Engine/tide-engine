# A game on a network of its own (tide/network.h): the docs' walkthrough
# (docs/multiplayer/own-network.md), whose files are these. Its test is two
# players of it in one program, in a match over udp.c's sockets on this
# machine; for the web, where there's no UDP, that the game builds and
# registers nothing.
tide_add_game(tide_network_udp SOURCES game.tide test/drive.tide udp.c NAME game
    HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" test/udp_test.c)
target_include_directories(tide_network_udp PRIVATE "${PROJECT_SOURCE_DIR}/tests" "${PROJECT_SOURCE_DIR}/packages/test")
if(WIN32)
    target_link_libraries(tide_network_udp PRIVATE ws2_32)
endif()
add_test(NAME tide_network_udp COMMAND tide_network_udp)
set_tests_properties(tide_network_udp PROPERTIES TIMEOUT 120)

# The same game under the standard host (tide/run.h), in a run with no
# window: what the host itself does with the game's network. Nothing shows,
# but such a run needs a desktop session with OpenGL (platform/CMakeLists.txt).
if(TARGET tide_platform AND NOT TIDE_WEB AND NOT TIDE_ANDROID)
    tide_add_game(tide_network_hosted SOURCES game.tide test/hosted.tide udp.c test/hosted.c)
    add_test(NAME tide_network_hosted COMMAND tide_network_hosted --frames 20)
    set_tests_properties(tide_network_hosted PROPERTIES TIMEOUT 120
        PASS_REGULAR_EXPRESSION "the standard host opened the match on the game's network"
        FAIL_REGULAR_EXPRESSION "didn't open the match")
    tide_gpu_test(tide_network_hosted)
endif()
