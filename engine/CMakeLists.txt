# New .c files under src/ are picked up automatically on the next build.
file(GLOB_RECURSE TIDE_ENGINE_SOURCES CONFIGURE_DEPENDS src/*.c)

add_library(tide STATIC ${TIDE_ENGINE_SOURCES})
target_include_directories(tide PUBLIC include)
target_link_libraries(tide PUBLIC tide_flags)

# The engine as a shared library's own copy, as `tide run`'s game libraries have one (tests of the host and a
# library together). Windows' and Apple's code is position-independent anyway; elsewhere it's built again so.
if(WIN32 OR APPLE OR CMAKE_POSITION_INDEPENDENT_CODE)
    add_library(tide_pic ALIAS tide)
else()
    add_library(tide_pic_objects STATIC EXCLUDE_FROM_ALL ${TIDE_ENGINE_SOURCES})
    target_include_directories(tide_pic_objects PUBLIC include)
    target_link_libraries(tide_pic_objects PUBLIC tide_flags)
    set_target_properties(tide_pic_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)
    add_library(tide_pic ALIAS tide_pic_objects)
endif()

# In the package, the engine is sources: tide compiles them with each game.
install(DIRECTORY include/tide DESTINATION include COMPONENT tide)
# Sources include private headers relative to their own folders.
install(DIRECTORY src/ DESTINATION src/engine COMPONENT tide
    FILES_MATCHING PATTERN "*.c" PATTERN "*.h")
# The double math in src/math64 is musl's, under its license (src/math64/README.md).
install(FILES src/math64/COPYRIGHT.h DESTINATION licenses/musl COMPONENT tide)
