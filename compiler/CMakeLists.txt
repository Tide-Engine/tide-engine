# tidec: the Tide transpiler. It's a build-time tool, so it doesn't link the engine.
# Everything but main.c is a library, which the language server shares.
file(GLOB TIDEC_SOURCES CONFIGURE_DEPENDS src/*.c)
list(REMOVE_ITEM TIDEC_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.c")

# Web and Android builds use a native tidec instead (cmake/HostTidec.cmake),
# and the language server and tide run natively, so only native builds have them.
if(NOT TIDE_WEB AND NOT TIDE_ANDROID)
    # Assets: tidec reads a game's PNGs with the engine's own reader and writes
    # its pack with the engine's own builder (tide/png.h, tide/pack.h), so what
    # it packs is what the game reads; those files bring the engine's pages and
    # hashes. The same objects are in the engine's library, so a program that
    # links both (tide_cli_tests) finds each symbol once.
    # Meshes: tidec reads a game's glTF and OBJ files itself (src/mesh_files.c),
    # and a file's colors become the screen's with the engine's own pow
    # (tide/math.h), which gives the same bits whichever machine builds.
    set(TIDEC_ENGINE_SOURCES "${PROJECT_SOURCE_DIR}/engine/src/png_read.c" "${PROJECT_SOURCE_DIR}/engine/src/wav.c" "${PROJECT_SOURCE_DIR}/engine/src/vorbis.c" "${PROJECT_SOURCE_DIR}/engine/src/pack.c"
        "${PROJECT_SOURCE_DIR}/engine/src/sha256.c" "${PROJECT_SOURCE_DIR}/engine/src/page.c" "${PROJECT_SOURCE_DIR}/engine/src/net.c"
        "${PROJECT_SOURCE_DIR}/engine/src/math.c")
    add_library(tidec_core STATIC ${TIDEC_SOURCES} ${TIDEC_ENGINE_SOURCES})
    # `#if TIDE_0_3_OR_NEWER` and packages' `tide 0.3` compare with it (version.h).
    set_source_files_properties(src/version.c PROPERTIES COMPILE_DEFINITIONS "TIDE_VERSION=\"${TIDE_VERSION}\"")
    target_link_libraries(tidec_core PUBLIC tide_flags)
    # tidec reads the device member lists from tide/devices.h so Tide and C agree.
    target_include_directories(tidec_core PUBLIC src "${PROJECT_SOURCE_DIR}/engine/include")

    add_executable(tidec src/main.c)
    target_link_libraries(tidec PRIVATE tidec_core)

    # Android apps (cli/apk.c), which tide makes itself, for this repo's
    # Android builds (tide_android_app), and their keys and signatures
    # (cli/sign.c, cli/rsa.c). Hashes and random bytes are the platform layer's.
    set(TIDE_APK_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/cli/apk.c" "${CMAKE_CURRENT_SOURCE_DIR}/cli/sign.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/cli/rsa.c" "${PROJECT_SOURCE_DIR}/platform/src/rtc/crypto.c"
        "${PROJECT_SOURCE_DIR}/engine/src/sha256.c")
    set(TIDE_APK_SOURCES ${TIDE_APK_SOURCES} PARENT_SCOPE)
    add_executable(tide_apk cli/apk_main.c ${TIDE_APK_SOURCES})
    target_include_directories(tide_apk PRIVATE cli "${PROJECT_SOURCE_DIR}/platform/src/rtc" "${PROJECT_SOURCE_DIR}/engine/include")
    target_link_libraries(tide_apk PRIVATE tide_flags)
    if(TIDE_TOOLS_ONLY)
        return()
    endif()
    add_subdirectory(lsp)
    add_subdirectory(cli)
endif()

add_subdirectory(tests)
