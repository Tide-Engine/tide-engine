# Windows' own target (x86_64-pc-windows-msvc) built on another system: the
# tide and tidels of the Windows package, which link LLVM's Windows libraries
# and so Microsoft's C and C++ runtimes, statically. A Windows machine needs
# none of this: clang finds Visual Studio's there.
#
# Microsoft's headers and libraries aren't ours to download with the build:
# xwin (github.com/Jake-Shadle/xwin) fetches them from Microsoft, under the
# Visual Studio license its user accepts, laid out as a Windows machine has
# them (`xwin --accept-license splat --use-winsysroot-style --output <folder>`,
# then each library folder's `x86_64` also under the name clang looks for,
# `x64`: .github/runner/setup-linux.sh does both). TIDE_XWIN in the environment
# names that folder (~/xwin without it).
# clang is found like native builds find it (clang-toolchain.cmake), and links
# with lld-link.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

include("${CMAKE_CURRENT_LIST_DIR}/clang-toolchain.cmake")
set(CMAKE_C_COMPILER_TARGET x86_64-pc-windows-msvc)
set(CMAKE_CXX_COMPILER_TARGET x86_64-pc-windows-msvc)

# CMake's Windows clang setup wants LLVM's stand-ins for Microsoft's tools.
get_filename_component(_tide_clang_dir "${TIDE_CLANG}" DIRECTORY)
foreach(tool IN ITEMS RC:llvm-rc MT:llvm-mt AR:llvm-ar LINKER:lld-link)
    string(REPLACE ":" ";" tool "${tool}")
    list(GET tool 0 variable)
    list(GET tool 1 name)
    find_program(TIDE_MSVC_${variable} NAMES ${name} HINTS "${_tide_clang_dir}")
    if(NOT TIDE_MSVC_${variable})
        message(FATAL_ERROR "${name} not found next to clang (${_tide_clang_dir}): the Windows target needs LLVM's ${name}")
    endif()
endforeach()
set(CMAKE_RC_COMPILER "${TIDE_MSVC_RC}")
set(CMAKE_MT "${TIDE_MSVC_MT}")
set(CMAKE_AR "${TIDE_MSVC_AR}")
set(CMAKE_LINKER "${TIDE_MSVC_LINKER}")

if(DEFINED ENV{TIDE_XWIN})
    set(TIDE_XWIN "$ENV{TIDE_XWIN}")
else()
    set(TIDE_XWIN "$ENV{HOME}/xwin")
endif()
file(GLOB _tide_msvc_tools "${TIDE_XWIN}/VC/Tools/MSVC/*/include/vcruntime.h")
file(GLOB _tide_msvc_kit "${TIDE_XWIN}/Windows Kits/10/Include/*/ucrt/stdio.h")
if(NOT _tide_msvc_tools OR NOT _tide_msvc_kit)
    message(FATAL_ERROR "Microsoft's headers and libraries aren't in ${TIDE_XWIN}: fetch them with xwin "
                        "(.github/runner/setup-linux.sh), or set TIDE_XWIN to where they are")
endif()

# clang takes the folder as it takes a Windows machine's Visual Studio and SDK:
# their headers after its own, as system headers, and their libraries.
set(_tide_msvc_root "-Xmicrosoft-windows-sys-root \"${TIDE_XWIN}\"")
set(CMAKE_C_FLAGS_INIT "${_tide_msvc_root}")
set(CMAKE_CXX_FLAGS_INIT "${_tide_msvc_root}")
set(CMAKE_TRY_COMPILE_PLATFORM_VARIABLES TIDE_XWIN)
# Resources are read by tools of their own, which are told each folder.
get_filename_component(_tide_msvc_tools "${_tide_msvc_tools}" DIRECTORY)
get_filename_component(_tide_msvc_kit "${_tide_msvc_kit}" DIRECTORY)
get_filename_component(_tide_msvc_kit "${_tide_msvc_kit}" DIRECTORY)
set(CMAKE_RC_FLAGS_INIT "-I \"${_tide_msvc_tools}\" -I \"${_tide_msvc_kit}/ucrt\" -I \"${_tide_msvc_kit}/um\" -I \"${_tide_msvc_kit}/shared\"")

# Programs are the build machine's; headers and libraries only the target's.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
