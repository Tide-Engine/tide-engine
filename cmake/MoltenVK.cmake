# MoltenVK: Vulkan on Metal, which macOS games draw with where the Mac has no
# Vulkan loader of its own (platform/desktop/glfw.c finds it beside the
# program). Downloaded at configure time, pinned to a release, as GLFW is, and
# only on macOS. It goes beside this build's programs (bin/), in the package
# (lib/native), and from there beside every game tide builds
# (compiler/cli/build.c), with its license (Apache 2.0).
include(FetchContent)

FetchContent_Declare(moltenvk
    URL https://github.com/KhronosGroup/MoltenVK/releases/download/v1.4.2/MoltenVK-macos.tar
    URL_HASH SHA256=f95765a6229cb7b915990a2890ce12ebe36a730b021545d3d52ae69ce4c4024e
    DOWNLOAD_EXTRACT_TIMESTAMP ON
    SOURCE_SUBDIR none) # Prebuilt: nothing to configure
FetchContent_MakeAvailable(moltenvk)

# The universal library (x86_64 and arm64), which tide thins to a game's CPU
set(TIDE_MOLTENVK "${moltenvk_SOURCE_DIR}/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib")
if(NOT EXISTS "${TIDE_MOLTENVK}")
    message(FATAL_ERROR "MoltenVK's release has no ${TIDE_MOLTENVK}")
endif()
file(COPY "${TIDE_MOLTENVK}" DESTINATION "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
install(FILES "${TIDE_MOLTENVK}" DESTINATION lib/native COMPONENT tide)
install(FILES "${moltenvk_SOURCE_DIR}/LICENSE" DESTINATION licenses/moltenvk COMPONENT tide)
