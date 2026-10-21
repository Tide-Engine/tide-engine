# New .c files under src/ are picked up automatically on the next build.
file(GLOB_RECURSE TIDE_ENGINE_SOURCES CONFIGURE_DEPENDS src/*.c)

add_library(tide STATIC ${TIDE_ENGINE_SOURCES})
target_include_directories(tide PUBLIC include)
target_link_libraries(tide PUBLIC tide_flags)

# In the package, the engine is sources: tide compiles them with each game.
install(DIRECTORY include/tide DESTINATION include COMPONENT tide)
# Sources include private headers relative to their own folders.
install(DIRECTORY src/ DESTINATION src/engine COMPONENT tide
    FILES_MATCHING PATTERN "*.c" PATTERN "*.h")
# The double math in src/math64 is musl's, under its license (src/math64/README.md).
install(FILES src/math64/COPYRIGHT.h DESTINATION licenses/musl COMPONENT tide)
