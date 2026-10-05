# The packages that come with tide (see AGENTS.md, Packaging and releases):
# games get one by listing it in their tide.packages, and tide compiles it
# with them. Here, each one's tests build it the way tide would.

# Box2D's own sources, as they came: a library the tests share, with its
# warnings off. The package's bridge is ours, and compiles with each test.
file(GLOB box2d_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/box2d/box2d/src/*.c")
add_library(tide_box2d STATIC ${box2d_sources})
# With the flags a game's C gets, which keep float math the same everywhere:
# without them, clang fuses a multiply and an add on ARM, and Box2D steps
# otherwise there
target_link_libraries(tide_box2d PRIVATE tide_flags)
target_compile_options(tide_box2d PRIVATE -w)
if(TIDE_SANITIZE)
    # As it came, it hands memset no array where it has none to clear (0
    # bytes of it), which the sanitizer would stop on: that check is off for
    # its sources, like its warnings. The rest stays on.
    target_compile_options(tide_box2d PRIVATE -fno-sanitize=nonnull-attribute)
endif()

tide_add_game(tide_box2d_stack SOURCES box2d/box2d.tide box2d/bridge.c box2d/test/stack.tide NAME game
    HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" box2d/test/stack_test.c)
target_include_directories(tide_box2d_stack PRIVATE "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(tide_box2d_stack PRIVATE tide_box2d)
if(TARGET tide_platform)
    # Its steps on the tick's threads
    target_compile_definitions(tide_box2d_stack PRIVATE BOX2D_TEST_JOBS)
    target_link_libraries(tide_box2d_stack PRIVATE tide_platform)
endif()
add_test(NAME tide_box2d_stack COMMAND tide_box2d_stack)
set_tests_properties(tide_box2d_stack PROPERTIES TIMEOUT 120)

# Box3D's, the same way, and with the same flags for the same reason.
file(GLOB box3d_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/box3d/box3d/src/*.c")
add_library(tide_box3d STATIC ${box3d_sources})
target_link_libraries(tide_box3d PRIVATE tide_flags)
target_compile_options(tide_box3d PRIVATE -w)
if(TIDE_SANITIZE)
    target_compile_options(tide_box3d PRIVATE -fno-sanitize=nonnull-attribute)
endif()

tide_add_game(tide_box3d_stack SOURCES box3d/box3d.tide box3d/bridge.c box3d/test/stack.tide NAME game
    HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" box3d/test/stack_test.c)
target_include_directories(tide_box3d_stack PRIVATE "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(tide_box3d_stack PRIVATE tide_box3d)
if(TARGET tide_platform)
    # Its steps on the tick's threads
    target_compile_definitions(tide_box3d_stack PRIVATE BOX3D_TEST_JOBS)
    target_link_libraries(tide_box3d_stack PRIVATE tide_platform)
endif()
add_test(NAME tide_box3d_stack COMMAND tide_box3d_stack)
# A step in 3D is several of Box2D's, and a web build has no SIMD for it
set_tests_properties(tide_box3d_stack PROPERTIES TIMEOUT 300)
