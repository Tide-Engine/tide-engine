# The docs playground runs the same transpiler in a browser worker. Only its
# host filesystem/process glue differs; game generation and language rules do
# not. Native tidec still generates the initial example during this build.
tide_add_game(tide_playground_demo SOURCES demo.tide HOST main.c NAME game LAYOUT)
target_link_libraries(tide_playground_demo PRIVATE tide_platform)
if(NOT TIDE_WEB)
    return()
endif()

file(GLOB browser_sources CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/compiler/src/*.c")
# Native build-code processes have no browser host; host.c reports that limit.
list(REMOVE_ITEM browser_sources "${PROJECT_SOURCE_DIR}/compiler/src/build_code.c")
set(fingerprint "${CMAKE_CURRENT_BINARY_DIR}/pack_fingerprint_source.c")
add_custom_command(OUTPUT "${fingerprint}"
    COMMAND "${CMAKE_COMMAND}" "-DINPUT=${PROJECT_SOURCE_DIR}/compiler/src/pack_fingerprint.inc"
        "-DOUTPUT=${fingerprint}" -DNAME=tide_pack_fingerprint_source
        -P "${PROJECT_SOURCE_DIR}/cmake/embed.cmake"
    DEPENDS "${PROJECT_SOURCE_DIR}/compiler/src/pack_fingerprint.inc" "${PROJECT_SOURCE_DIR}/cmake/embed.cmake"
    VERBATIM)
set(debug_views "${CMAKE_CURRENT_BINARY_DIR}/debug_views_py.c")
add_custom_command(OUTPUT "${debug_views}"
    COMMAND "${CMAKE_COMMAND}" "-DINPUT=${PROJECT_SOURCE_DIR}/compiler/src/debug_views.py"
        "-DOUTPUT=${debug_views}" -DNAME=tide_debug_views_py
        -P "${PROJECT_SOURCE_DIR}/cmake/embed.cmake"
    DEPENDS "${PROJECT_SOURCE_DIR}/compiler/src/debug_views.py" "${PROJECT_SOURCE_DIR}/cmake/embed.cmake"
    VERBATIM)
add_library(tide_playground_importers STATIC
    "${PROJECT_SOURCE_DIR}/compiler/third_party/ufbx/ufbx.c"
    "${PROJECT_SOURCE_DIR}/compiler/third_party/ufbx/ufbx_math.c"
    "${PROJECT_SOURCE_DIR}/compiler/third_party/dr_libs/dr_decoders.c")
target_compile_definitions(tide_playground_importers PRIVATE UFBX_EXTERNAL_MATH UFBX_STANDARD_C UFBX_NO_STDIO
    UFBX_NO_FORMAT_OBJ UFBX_NO_SUBDIVISION UFBX_NO_TESSELLATION UFBX_NO_GEOMETRY_CACHE)
target_compile_options(tide_playground_importers PRIVATE -w)
target_link_libraries(tide_playground_importers PRIVATE tide_flags)
# The transpiler's sources, shared by the browser compiler and the browser
# language server (main.c is the compiler's command line).
list(REMOVE_ITEM browser_sources "${PROJECT_SOURCE_DIR}/compiler/src/main.c")
add_library(tide_browser_compiler STATIC ${browser_sources} "${fingerprint}" "${debug_views}" host.c)
target_include_directories(tide_browser_compiler PUBLIC "${PROJECT_SOURCE_DIR}/compiler/src")
target_include_directories(tide_browser_compiler SYSTEM PRIVATE
    "${PROJECT_SOURCE_DIR}/compiler/third_party/ufbx"
    "${PROJECT_SOURCE_DIR}/compiler/third_party/dr_libs"
    "${PROJECT_SOURCE_DIR}/compiler/third_party/stb_image"
    "${PROJECT_SOURCE_DIR}/compiler/third_party/stb_dxt")
target_compile_options(tide_browser_compiler PUBLIC -include "${CMAKE_CURRENT_SOURCE_DIR}/host.h"
    -mexception-handling -mllvm -wasm-enable-sjlj)
target_compile_definitions(tide_browser_compiler PUBLIC _WASI_EMULATED_PROCESS_CLOCKS _WASI_EMULATED_GETPID)
target_compile_definitions(tide_browser_compiler PRIVATE
    "TIDE_VERSION=\"${TIDE_VERSION}\"" "TIDE_SIMULATION_RUNTIME=\"${TIDE_SIMULATION_RUNTIME}\""
    "TIDE_ASSET_CACHE_IMPORTERS=\"${TIDE_ASSET_CACHE_IMPORTERS}\"")
target_link_libraries(tide_browser_compiler PUBLIC tide tide_playground_importers
    wasi-emulated-process-clocks wasi-emulated-getpid setjmp)
add_executable(tidec_browser "${PROJECT_SOURCE_DIR}/compiler/src/main.c")
target_link_libraries(tidec_browser PRIVATE tide_browser_compiler)

# The language server for the editor's completion, hovers and diagnostics: a
# WASI reactor the worker calls a message at a time (tidels.c).
file(GLOB browser_lsp_sources CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/compiler/lsp/*.c")
list(REMOVE_ITEM browser_lsp_sources "${PROJECT_SOURCE_DIR}/compiler/lsp/main.c")
add_executable(tidels_browser ${browser_lsp_sources} tidels.c)
target_include_directories(tidels_browser PRIVATE "${PROJECT_SOURCE_DIR}/compiler/lsp")
target_compile_definitions(tidels_browser PRIVATE "TIDE_ENGINE_INCLUDE_DIR=\"/include\"")
target_link_libraries(tidels_browser PRIVATE tide_browser_compiler)
target_link_options(tidels_browser PRIVATE -mexec-model=reactor)

# Staging and pinned third-party downloads are explicit, like docs_examples.
add_custom_target(docs_playground
    COMMAND "${TIDE_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/stage.mjs"
        --sysroot "${TIDE_WASI_SYSROOT}" --builtins "${TIDE_WASI_BUILTINS}"
        --compiler "$<TARGET_FILE:tidec_browser>" --server "$<TARGET_FILE:tidels_browser>" --demo "$<TARGET_FILE:tide_playground_demo>"
        --engine "$<TARGET_FILE:tide>" --platform "$<TARGET_FILE:tide_platform>"
    DEPENDS tidec_browser tidels_browser tide_playground_demo tide tide_platform
    VERBATIM)
