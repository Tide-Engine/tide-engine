# Windows builds on the MinGW-w64 target (x86_64-w64-windows-gnu) instead of
# Visual Studio's. Games built by tide use this target: its C runtime can ship
# with tide, and Microsoft's can't. The C library is the UCRT, part of Windows
# since 10, so games need no DLLs besides Windows' own.
#
# The headers, import libraries and compiler runtime come from llvm-mingw,
# downloaded once into build/llvm-mingw-<version> and pinned. clang is found like
# native builds find it (clang-toolchain.cmake), and links with lld.

# On another system than Windows it's a cross build (the Windows package's
# platform layer and C runtime, built on Linux).
if(NOT CMAKE_HOST_WIN32)
    set(CMAKE_SYSTEM_NAME Windows)
    set(CMAKE_SYSTEM_PROCESSOR x86_64)
endif()

set(TIDE_MINGW_VERSION 20260922)
set(TIDE_MINGW_TARGET x86_64-w64-windows-gnu)

include("${CMAKE_CURRENT_LIST_DIR}/clang-toolchain.cmake")
set(CMAKE_C_COMPILER_TARGET ${TIDE_MINGW_TARGET})
# CMake's MinGW setup expects windres, which LLVM has under its own name.
get_filename_component(_tide_clang_dir "${TIDE_CLANG}" DIRECTORY)
find_program(TIDE_LLVM_WINDRES NAMES llvm-windres HINTS "${_tide_clang_dir}")
if(TIDE_LLVM_WINDRES)
    set(CMAKE_RC_COMPILER "${TIDE_LLVM_WINDRES}")
endif()

# Shared by every MinGW build tree, and by CMake's own test projects. The Linux
# build of llvm-mingw is the smallest download; the files taken from it are the
# same in every build.
get_filename_component(_tide_mingw_dir "${CMAKE_CURRENT_LIST_DIR}/../build/llvm-mingw-${TIDE_MINGW_VERSION}" ABSOLUTE)
# Its licenses too, which the package carries (platform/CMakeLists.txt):
# MinGW-w64's, and LLVM's for the compiler runtime. A download from before
# they were taken is taken again.
if(NOT EXISTS "${_tide_mingw_dir}/done" OR NOT EXISTS "${_tide_mingw_dir}/licenses")
    set(_tide_mingw_name "llvm-mingw-${TIDE_MINGW_VERSION}-ucrt-ubuntu-22.04-x86_64")
    set(_tide_mingw_file "${_tide_mingw_dir}/${_tide_mingw_name}.tar.xz")
    message(STATUS "Downloading llvm-mingw ${TIDE_MINGW_VERSION}")
    file(DOWNLOAD "https://github.com/mstorsjo/llvm-mingw/releases/download/${TIDE_MINGW_VERSION}/${_tide_mingw_name}.tar.xz"
        "${_tide_mingw_file}" EXPECTED_HASH SHA256=bb7bb7654b33d5aa8712acb837c963b2e0c56352560c76105270a3268c665c21
        STATUS status)
    list(GET status 0 code)
    if(NOT code EQUAL 0)
        message(FATAL_ERROR "Downloading llvm-mingw failed: ${status}")
    endif()
    # Its x86_64 include folder is a link to the shared one, which Windows
    # can't extract; take the shared one.
    file(ARCHIVE_EXTRACT INPUT "${_tide_mingw_file}" DESTINATION "${_tide_mingw_dir}/extract"
        PATTERNS "${_tide_mingw_name}/generic-w64-mingw32/include/*" "${_tide_mingw_name}/x86_64-w64-mingw32/lib/*"
                 "${_tide_mingw_name}/lib/clang/*/lib/windows/libclang_rt.builtins-x86_64.a"
                 "${_tide_mingw_name}/LICENSE.TXT" "${_tide_mingw_name}/x86_64-w64-mingw32/share/mingw32/COPYING*")
    set(_tide_mingw_from "${_tide_mingw_dir}/extract/${_tide_mingw_name}")
    file(REMOVE_RECURSE "${_tide_mingw_dir}/sysroot" "${_tide_mingw_dir}/licenses")
    file(MAKE_DIRECTORY "${_tide_mingw_dir}/sysroot")
    # winpthreads' and winstorecompat's aren't: the package has neither.
    file(MAKE_DIRECTORY "${_tide_mingw_dir}/licenses/mingw-w64" "${_tide_mingw_dir}/licenses/llvm-mingw")
    foreach(_tide_name COPYING COPYING.MinGW-w64.txt COPYING.MinGW-w64-runtime.txt)
        file(RENAME "${_tide_mingw_from}/x86_64-w64-mingw32/share/mingw32/${_tide_name}" "${_tide_mingw_dir}/licenses/mingw-w64/${_tide_name}")
    endforeach()
    file(RENAME "${_tide_mingw_from}/LICENSE.TXT" "${_tide_mingw_dir}/licenses/llvm-mingw/LICENSE.TXT")
    file(REMOVE_RECURSE "${_tide_mingw_from}/x86_64-w64-mingw32/share")
    file(RENAME "${_tide_mingw_from}/x86_64-w64-mingw32" "${_tide_mingw_dir}/sysroot/x86_64-w64-mingw32")
    file(RENAME "${_tide_mingw_from}/generic-w64-mingw32/include" "${_tide_mingw_dir}/sysroot/x86_64-w64-mingw32/include")
    file(GLOB _tide_mingw_builtins "${_tide_mingw_from}/lib/clang/*/lib/windows/libclang_rt.builtins-x86_64.a")
    file(RENAME "${_tide_mingw_builtins}" "${_tide_mingw_dir}/libclang_rt.builtins-x86_64.a")
    file(REMOVE_RECURSE "${_tide_mingw_dir}/extract")
    file(REMOVE "${_tide_mingw_file}")
    file(TOUCH "${_tide_mingw_dir}/done")
endif()

set(TIDE_MINGW_SYSROOT "${_tide_mingw_dir}/sysroot")
set(TIDE_MINGW_BUILTINS "${_tide_mingw_dir}/libclang_rt.builtins-x86_64.a")
set(TIDE_MINGW_LICENSES "${_tide_mingw_dir}/licenses")
set(CMAKE_SYSROOT "${TIDE_MINGW_SYSROOT}")

# The compiler runtime is passed by path, and the C runtime's libraries by name
# (what clang links by default for MinGW, and what CMake adds on Windows): LLVM's
# releases default to GCC's runtime for this target, and don't include ours.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld -nodefaultlibs")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-fuse-ld=lld -nodefaultlibs")
set(CMAKE_C_STANDARD_LIBRARIES "-lmingw32 -lmingwex -lmoldname -lmsvcrt -lkernel32 -luser32 -lgdi32 -lwinspool \
-lshell32 -lole32 -loleaut32 -luuid -lcomdlg32 -ladvapi32 \"${TIDE_MINGW_BUILTINS}\"")
