# The docs playground runs the same transpiler in a browser worker. Only its
# host filesystem/process glue differs; game generation and language rules do
# not. Native tidec still generates the initial example during this build.
tide_add_game(tide_playground_demo SOURCES demo.tide HOST main.c NAME game LAYOUT)
target_link_libraries(tide_playground_demo PRIVATE tide_platform)
if(NOT TIDE_WEB)
    return()
endif()

file(GLOB browser_sources CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/compiler/src/*.c")
set(fingerprint "${CMAKE_CURRENT_BINARY_DIR}/pack_fingerprint_source.c")
add_custom_command(OUTPUT "${fingerprint}"
    COMMAND "${CMAKE_COMMAND}" "-DINPUT=${PROJECT_SOURCE_DIR}/compiler/src/pack_fingerprint.inc"
        "-DOUTPUT=${fingerprint}" -DNAME=tide_pack_fingerprint_source
        -P "${PROJECT_SOURCE_DIR}/cmake/embed.cmake"
    DEPENDS "${PROJECT_SOURCE_DIR}/compiler/src/pack_fingerprint.inc" "${PROJECT_SOURCE_DIR}/cmake/embed.cmake"
    VERBATIM)
add_library(tide_playground_importers STATIC
    "${PROJECT_SOURCE_DIR}/compiler/third_party/ufbx/ufbx.c"
    "${PROJECT_SOURCE_DIR}/compiler/third_party/ufbx/ufbx_math.c"
    "${PROJECT_SOURCE_DIR}/compiler/third_party/dr_libs/dr_decoders.c")
target_compile_definitions(tide_playground_importers PRIVATE UFBX_EXTERNAL_MATH UFBX_STANDARD_C UFBX_NO_STDIO
    UFBX_NO_FORMAT_OBJ UFBX_NO_SUBDIVISION UFBX_NO_TESSELLATION UFBX_NO_GEOMETRY_CACHE)
target_compile_options(tide_playground_importers PRIVATE -w)
target_link_libraries(tide_playground_importers PRIVATE tide_flags)
add_executable(tidec_browser ${browser_sources} "${fingerprint}" host.c)
target_include_directories(tidec_browser PRIVATE "${PROJECT_SOURCE_DIR}/compiler/src")
target_include_directories(tidec_browser SYSTEM PRIVATE
    "${PROJECT_SOURCE_DIR}/compiler/third_party/ufbx"
    "${PROJECT_SOURCE_DIR}/compiler/third_party/dr_libs"
    "${PROJECT_SOURCE_DIR}/compiler/third_party/stb_image"
    "${PROJECT_SOURCE_DIR}/compiler/third_party/stb_dxt")
target_compile_options(tidec_browser PRIVATE -include "${CMAKE_CURRENT_SOURCE_DIR}/host.h")
target_compile_options(tidec_browser PRIVATE -mexception-handling -mllvm -wasm-enable-sjlj)
target_compile_definitions(tidec_browser PRIVATE _WASI_EMULATED_PROCESS_CLOCKS "TIDE_VERSION=\"${TIDE_VERSION}\"")
target_link_libraries(tidec_browser PRIVATE tide tide_playground_importers wasi-emulated-process-clocks setjmp)

# Staging and pinned third-party downloads are explicit, like docs_examples.
add_custom_target(docs_playground
    COMMAND "${TIDE_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/stage.mjs"
        --sysroot "${TIDE_WASI_SYSROOT}" --builtins "${TIDE_WASI_BUILTINS}"
        --compiler "$<TARGET_FILE:tidec_browser>" --demo "$<TARGET_FILE:tide_playground_demo>"
        --engine "$<TARGET_FILE:tide>" --platform "$<TARGET_FILE:tide_platform>"
    DEPENDS tidec_browser tide_playground_demo tide tide_platform
    VERBATIM)
