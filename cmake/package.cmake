# Makes the tide package for this machine from the package build trees:
#
#   cmake --preset package && cmake --build --preset package
#   cmake --preset web-package && cmake --build --preset web-package
#   cmake --preset mingw-release && cmake --build --preset mingw-release  (Windows)
#   cmake --preset android-package-arm64 && cmake --build --preset android-package-arm64
#   cmake --preset android-package-x86_64 && cmake --build --preset android-package-x86_64
#   First, in tools/tide-jetbrains: ./gradlew buildCliInstaller -PpluginVersion=<TIDE_VERSION>
#   cmake -DNAME=tide-windows-x64 -DJETBRAINS=tools/tide-jetbrains/build/cli/tide-jetbrains-installer.zip -P cmake/package.cmake
#
# NAME is the archive's name: .zip for Windows, .tar.gz otherwise. It lands in
# build/dist, next to the unpacked package (build/dist/tide) and a SHA256SUMS
# line. NATIVE, WEB, MINGW, ANDROID_ARM64 and ANDROID_X86_64 override the build
# trees (default build/package, build/web-package, build/mingw-release,
# build/android-package-arm64 and build/android-package-x86_64). Without the web
# tree, the package can't build web games; on Windows, without the MinGW tree,
# native ones; without the Android trees, Android ones.

cmake_minimum_required(VERSION 3.25)
get_filename_component(source "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(NOT NAME)
    message(FATAL_ERROR "Pass -DNAME=<archive name>, like tide-windows-x64")
endif()
if(NOT NATIVE)
    set(NATIVE "${source}/build/package")
endif()
if(NOT WEB)
    set(WEB "${source}/build/web-package")
endif()
if(NOT MINGW)
    set(MINGW "${source}/build/mingw-release")
endif()
if(NOT ANDROID_ARM64)
    set(ANDROID_ARM64 "${source}/build/android-package-arm64")
endif()
if(NOT ANDROID_X86_64)
    set(ANDROID_X86_64 "${source}/build/android-package-x86_64")
endif()
set(dist "${source}/build/dist")
set(stage "${dist}/tide")

# On Windows, games build for MinGW (see cmake/mingw-toolchain.cmake): the
# MinGW tree brings their platform layer and C runtime. It goes first, so the
# native tree's tide and tidels, built for Visual Studio's target, replace its.
# Windows' package is told by its name, as another system builds it too (with
# NATIVE as the tree of the windows-package preset).
set(trees "${NATIVE}" "${WEB}" "${ANDROID_ARM64}" "${ANDROID_X86_64}")
if(NAME MATCHES "windows")
    list(PREPEND trees "${MINGW}")
endif()

file(REMOVE_RECURSE "${stage}")
foreach(tree IN LISTS trees)
    if(NOT EXISTS "${tree}/CMakeCache.txt")
        if(tree STREQUAL NATIVE)
            message(FATAL_ERROR "No native package build in ${tree}: run `cmake --preset package` and build it")
        elseif(tree STREQUAL MINGW)
            message(FATAL_ERROR "No MinGW build in ${tree}: Windows' package builds native games with it")
        elseif(tree STREQUAL ANDROID_ARM64 OR tree STREQUAL ANDROID_X86_64)
            message(WARNING "No Android package build in ${tree}: this package won't build Android games")
        else()
            message(WARNING "No web package build in ${tree}: this package won't build web games")
        endif()
        continue()
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" --install "${tree}" --prefix "${stage}" --component tide
        RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Installing ${tree} failed")
    endif()
endforeach()

# Every tree installs a VERSION, and the package's is its tide's: tide takes an
# installation whose VERSION isn't its own version for an upgrade that stopped
# partway (compiler/cli/install.c), whatever the other trees were configured as.
file(COPY_FILE "${NATIVE}/package/VERSION" "${stage}/VERSION")

# One Gradle build supplies the exact plugin and dependency for every OS.
if(NOT JETBRAINS)
    set(JETBRAINS "${source}/build/jetbrains/tide-jetbrains-installer.zip")
endif()
if(NOT EXISTS "${JETBRAINS}")
    message(FATAL_ERROR "No JetBrains CLI installer at ${JETBRAINS}: run buildCliInstaller and pass -DJETBRAINS=<archive>")
endif()
set(plugin_version_dir "${stage}/editors/.jetbrains-version")
file(ARCHIVE_EXTRACT INPUT "${JETBRAINS}" DESTINATION "${plugin_version_dir}"
    PATTERNS VERSION "lsp4ij/licenses/*")
if(NOT EXISTS "${plugin_version_dir}/VERSION")
    message(FATAL_ERROR "The JetBrains CLI installer has no VERSION; rebuild it with buildCliInstaller")
endif()
file(READ "${plugin_version_dir}/VERSION" plugin_version)
file(READ "${stage}/VERSION" native_version)
string(STRIP "${plugin_version}" plugin_version)
string(STRIP "${native_version}" native_version)
if(NOT plugin_version STREQUAL native_version)
    message(FATAL_ERROR "JetBrains plugin ${plugin_version} does not match Tide ${native_version}; rebuild it with -PpluginVersion=${native_version}")
endif()
file(COPY "${plugin_version_dir}/lsp4ij/licenses/" DESTINATION "${stage}/licenses/jetbrains")
file(REMOVE_RECURSE "${plugin_version_dir}")
file(COPY_FILE "${JETBRAINS}" "${stage}/editors/tide-jetbrains-installer.zip")

if(NAME MATCHES "windows")
    set(archive "${dist}/${NAME}.zip")
    set(format --format=zip)
else()
    set(archive "${dist}/${NAME}.tar.gz")
    set(format "")
endif()
file(REMOVE "${archive}")
file(GLOB entries RELATIVE "${stage}" "${stage}/*")
if(format)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E tar cf "${archive}" ${format} ${entries}
        WORKING_DIRECTORY "${stage}" RESULT_VARIABLE result)
else()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E tar czf "${archive}" ${entries}
        WORKING_DIRECTORY "${stage}" RESULT_VARIABLE result)
endif()
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Making ${archive} failed")
endif()

get_filename_component(file "${archive}" NAME)
file(SHA256 "${archive}" hash)
file(WRITE "${dist}/${NAME}.sha256" "${hash}  ${file}\n")
message(STATUS "Packaged ${archive}")
