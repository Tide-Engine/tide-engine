# tide_add_game(<target> [SOURCES <file.tide|file.c|file.cpp>...] [HOST <file.c>...] [WINDOW] [NAME <name>]
#               [TITLE <title>] [STATS] [LAYOUT] [EMBED] [VISUALIZERS] [WARNINGS <text>...]
#               [PACKAGES <Name> <file.tide>... [<Name> <file.tide>...]]
#               [DEFINES <SYMBOL>...] [SHELL <page.html>])
#
# Builds a Tide game as the program <target>. The game is every .tide file
# in the current source folder and its subfolders (new ones are picked up by
# the next build), or only the files listed after SOURCES. Its C files, which
# define its extern functions, compile with it: every .c file in the folder
# but the HOST ones, or the .c files listed after SOURCES. So do its C++
# files (.cpp, .cc and .cxx), with no C++ runtime (TIDE_CPP_FLAGS).
#
# PACKAGES lists files of packages the game builds with, each package's after
# its name (which is its namespace): what isn't a .tide file is a name. They
# compile before the game's, in the order the packages are named, as tide
# builds a game with what its tide.packages lists. It's for tests of what
# packages do to a game: this doesn't read tide.packages, and a game with real
# packages builds with tide.
#
# DEFINES lists symbols the game's #if lines read as defined, as tide's
# --define does (docs/spec.md, Conditional compilation). DEBUG is defined in a
# Debug build, and PACKAGE_<NAME> for each package after PACKAGES.
#
# Without HOST, the game is the whole program: it opens a window titled TITLE,
# or the game's title setting, or <target>, and runs (see
# platform/include/tide/run.h). STATS shows the stats in a corner, as the
# game's stats setting does: the frame rate, and at a click on it or F8, what
# a frame's work takes, ping, bandwidth, tick, entity count, the threads ticks
# run on, memory, draw calls and the slowest systems. On the web it's
# <target>.html, around the canvas SHELL makes (see tide_web_page), and on
# Android an app (see tide_android_app).
#
# With HOST, those C files are the program instead, for tests and custom hosts.
# WINDOW says they open a window, so that on Android they're an app too, rather
# than a program that runs from a shell.
# They include the game's generated header, <name>.h (NAME defaults to
# <target>), and drive the game through its API. LAYOUT also
# describes the game's data layout, as tide run does for hot reloading
# (tide_game_layout, see engine/include/tide/layout.h). EMBED adds what
# another program needs to run it (tide_game_embed, tide/embed_game.h, which
# tide/run.h's table then has: TIDE_HAS_EMBED is defined for the HOST files),
# and the layout with it.
#
# VISUALIZERS writes how debuggers show the game's values beside its generated
# files, as tide's debug builds do: <name>.natvis, which a Windows program's
# .pdb carries, and <name>_lldb.py for LLDB (`command script import` it).
# The TIDE_VISUALIZERS option gives every native game it.
#
# The generated files land in the build tree and are regenerated whenever a
# .tide file or tidec changes. Every game is also listed in
# TIDE_GAMES_MANIFEST, so editors know which files belong together.
#
# A game's assets (its `asset` declarations: docs/spec.md, Assets) are packed
# by tidec beside the generated files, <name>.pack, which the files they name
# regenerate too. A game with a window gets it beside its program, as
# <target>.pack (tide/run.h loads it), inside its web page, and in its APK. A
# HOST program gets it as C instead (<name>_pack.c: tide_game_pack and
# tide_game_pack_size), to hand tide_assets_load itself; a game with no assets
# has an empty one.
#
# tidec's warnings are errors here (cmake/run_tidec.cmake), as the compiler's
# are. WARNINGS lists the ones a game means to have, such as tests of what
# tidec generates for programs it warns about: each is part of a warning's
# text, and has to be there.

# The bold weight's file (platform/font/Tuffy_Bold.ttf) as a library of that
# one object. A game that says Font.Bold names the file in its C
# (tide_font_bold_ttf, tide/draw.h), and a linker takes it out of a library for
# such a game alone: out of the platform layer's, which has it too, for a game
# with a window, and out of this one for a HOST's program, which may link no
# platform layer. So a game builds whichever program it's in, and one that
# never says Font.Bold carries no file in any.
set(TIDE_FONT_BOLD_C "${PROJECT_BINARY_DIR}/tide_font_bold/tide_font_bold.c")
add_custom_command(OUTPUT "${TIDE_FONT_BOLD_C}"
    COMMAND "${CMAKE_COMMAND}" "-DINPUT=${PROJECT_SOURCE_DIR}/platform/font/Tuffy_Bold.ttf"
        "-DOUTPUT=${TIDE_FONT_BOLD_C}" -DNAME=tide_font_bold_ttf -P "${CMAKE_CURRENT_LIST_DIR}/embed.cmake"
    DEPENDS "${PROJECT_SOURCE_DIR}/platform/font/Tuffy_Bold.ttf" "${CMAKE_CURRENT_LIST_DIR}/embed.cmake"
    COMMENT "The bold font as C"
    VERBATIM)
add_library(tide_font_bold STATIC "${TIDE_FONT_BOLD_C}")

set(TIDE_GAMES_MANIFEST "${PROJECT_SOURCE_DIR}/build/tools/games.txt")
set(TIDE_RUN_TIDEC "${CMAKE_CURRENT_LIST_DIR}/run_tidec.cmake")

# A game's C++ is the language alone, as tide builds it (cpp_flags in
# compiler/cli/build.c): no standard library, exceptions or RTTI, since tide
# brings no C++ runtime. This project is C, so the C
# compiler compiles those files, and programs link as C: -x c++ and -std=c++20
# go over what CMake says for a C file, which comes before them.
set(TIDE_CPP_FLAGS -x c++ -std=c++20 -nostdinc++ -fno-exceptions -fno-rtti -fno-threadsafe-statics)

# tide_web_page(<target> [SHELL <page.html>] [PACK <game.pack>])
#
# Web builds: makes <target>.html next to <target>.wasm, one self-contained
# page with the program and platform/web/tide.js inside, which opens straight
# from disk. SHELL is the page around the canvas (default platform/web/shell.html;
# tests use platform/web/test_shell.html). PACK is the game's pack of assets
# (tide_add_game), which goes inside the page when there is one. Does nothing
# in native builds, so it can be called unconditionally. Here, beside
# tide_add_game, which pages the games it builds with it, so any folder's CMake
# can page a host of its own.
set(TIDE_WEB_SHELL "${PROJECT_SOURCE_DIR}/platform/web/shell.html" CACHE INTERNAL "")
set(TIDE_WEB_SCRIPT "${PROJECT_SOURCE_DIR}/platform/web/tide.js" CACHE INTERNAL "")
set(TIDE_WEB_RELEASE_SCRIPT "${PROJECT_SOURCE_DIR}/platform/web/tide.release.js" CACHE INTERNAL "")
include("${PROJECT_SOURCE_DIR}/cmake/WebRuntime.cmake")
function(tide_web_page target)
    if(NOT TIDE_WEB)
        return()
    endif()
    cmake_parse_arguments(ARG "" "SHELL;PACK" "" ${ARGN})
    if(ARG_SHELL)
        get_filename_component(shell "${ARG_SHELL}" ABSOLUTE)
    else()
        set(shell "${TIDE_WEB_SHELL}")
    endif()
    set(release "$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>")
    set(script "$<IF:${release},${TIDE_WEB_RELEASE_SCRIPT},${TIDE_WEB_SCRIPT}>")
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${TIDE_NODE}" "${PROJECT_SOURCE_DIR}/cmake/web_page.mjs"
            "$<TARGET_FILE:${target}>" "${shell}" "${script}"
            "$<TARGET_FILE_DIR:${target}>/${target}.html" ${ARG_PACK} "$<${release}:--release>"
        COMMENT "Web page for ${target}"
        VERBATIM)
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${shell}" "${TIDE_WEB_SCRIPT}"
        "${TIDE_WEB_RELEASE_SCRIPT}" "${PROJECT_SOURCE_DIR}/cmake/web_page.mjs")
endfunction()

function(tide_add_game target)
    cmake_parse_arguments(ARG "STATS;LAYOUT;EMBED;WINDOW;SERVER;STRIP_SERVER_ASSETS;VISUALIZERS" "NAME;TITLE;SHELL" "SOURCES;HOST;WARNINGS;PACKAGES;DEFINES" ${ARGN})
    if(ARG_SERVER AND (TIDE_WEB OR TIDE_ANDROID OR ARG_HOST OR ARG_WINDOW OR ARG_LAYOUT))
        message(FATAL_ERROR "tide_add_game(${target} SERVER) is a standalone native server")
    endif()
    if(ARG_STRIP_SERVER_ASSETS AND NOT ARG_SERVER)
        message(FATAL_ERROR "STRIP_SERVER_ASSETS requires SERVER")
    endif()
    if("SOURCES" IN_LIST ARG_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR "tide_add_game(${target}): list the game's .tide files after SOURCES, or leave "
                            "SOURCES out to use every .tide file in ${CMAKE_CURRENT_SOURCE_DIR} and its subfolders")
    endif()
    set(name "${target}")
    if(ARG_NAME)
        set(name "${ARG_NAME}")
    endif()
    # TITLE goes over the game's title setting; the target's name only stands in for it
    set(title_field "game_name")
    set(title "${target}")
    if(ARG_TITLE)
        set(title_field "title")
        set(title "${ARG_TITLE}")
    endif()

    set(sources "")
    set(c_sources "")
    if(ARG_SOURCES)
        foreach(file IN LISTS ARG_SOURCES)
            get_filename_component(path "${file}" ABSOLUTE)
            if(path MATCHES "\\.(c|cpp|cc|cxx)$")
                list(APPEND c_sources "${path}")
            else()
                list(APPEND sources "${path}")
            endif()
        endforeach()
        set(manifest_paths ${sources})
    else()
        # CONFIGURE_DEPENDS: every build checks for new or deleted files first.
        # Not in hidden folders, like .tide/ and .build/, where tide keeps what it makes.
        file(GLOB_RECURSE found CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.tide" "${CMAKE_CURRENT_SOURCE_DIR}/*.tprefab")
        set(sources "")
        foreach(path IN LISTS found)
            file(RELATIVE_PATH relative "${CMAKE_CURRENT_SOURCE_DIR}" "${path}")
            if(NOT relative MATCHES "(^|/)\\.")
                list(APPEND sources "${path}")
            endif()
        endforeach()
        if(NOT sources)
            message(FATAL_ERROR "tide_add_game(${target}): there are no .tide files in ${CMAKE_CURRENT_SOURCE_DIR} "
                                "or its subfolders. Add one, or list the game's files after SOURCES")
        endif()
        # Nor C, generated C included.
        file(GLOB_RECURSE found CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/*.cc" "${CMAKE_CURRENT_SOURCE_DIR}/*.cxx")
        foreach(path IN LISTS found)
            file(RELATIVE_PATH relative "${CMAKE_CURRENT_SOURCE_DIR}" "${path}")
            if(NOT relative MATCHES "(^|/)\\.")
                list(APPEND c_sources "${path}")
            endif()
        endforeach()
        foreach(file IN LISTS ARG_HOST)
            get_filename_component(path "${file}" ABSOLUTE)
            list(REMOVE_ITEM c_sources "${path}")
        endforeach()
        # The folder rather than its files, so editors see new files before the next build.
        set(manifest_paths "${CMAKE_CURRENT_SOURCE_DIR}/")
    endif()

    # The packages' files, as tidec's list says them: `--package <Name>`, then its files
    set(package_lines "")
    set(package_sources "")
    set(manifest_packages "")
    set(package "")
    foreach(item IN LISTS ARG_PACKAGES)
        if(item MATCHES "\\.(tide|tprefab)$")
            if(package STREQUAL "")
                message(FATAL_ERROR "tide_add_game(${target}): name the package before its files, after PACKAGES")
            endif()
            get_filename_component(path "${item}" ABSOLUTE)
            list(APPEND package_lines "${path}")
            list(APPEND package_sources "${path}")
            list(APPEND manifest_packages "${path}\t${package}")
        else()
            set(package "${item}")
            list(APPEND package_lines "--package ${item}")
        endif()
    endforeach()

    foreach(path IN LISTS c_sources)
        if(path MATCHES "\\.(cpp|cc|cxx)$")
            set_source_files_properties("${path}" PROPERTIES LANGUAGE C COMPILE_OPTIONS "${TIDE_CPP_FLAGS}")
        endif()
    endforeach()

    set(layout "")
    if(ARG_EMBED)
        set(layout --embed)
    elseif(ARG_LAYOUT)
        set(layout --layout)
    endif()
    # The symbols the game's #if lines read as defined: DEFINES', and DEBUG in a Debug build, as tide
    # defines it without --release
    set(defines "")
    foreach(symbol IN LISTS ARG_DEFINES)
        list(APPEND defines --define "${symbol}")
    endforeach()
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        list(APPEND defines --define DEBUG)
    endif()

    set(out_dir "${CMAKE_CURRENT_BINARY_DIR}/tide/${target}")
    set(out_c "${out_dir}/${name}.c")
    set(out_h "${out_dir}/${name}.h")
    set(out_pack "${out_dir}/${name}.pack")
    set(out_pack_c "${out_dir}/${name}_pack.c")
    set(pack_c "")
    set(pack_outputs "")
    if(ARG_HOST)
        set(pack_c --pack-c)
        set(pack_outputs "${out_pack_c}")
    endif()
    # The files its assets name: a change to one regenerates the pack. They're
    # found when the game is configured, as its .tide files are with no SOURCES.
    set(asset_files "")
    foreach(path IN LISTS sources package_sources)
        file(STRINGS "${path}" asset_lines REGEX "^asset ")
        foreach(line IN LISTS asset_lines)
            if(line MATCHES "= *\"([^\"]*)\"")
                set(asset_path "${CMAKE_MATCH_1}")
                if(NOT asset_path MATCHES "^https?://")
                    get_filename_component(asset_dir "${path}" DIRECTORY)
                    if(asset_path MATCHES "[*?]")
                        string(REGEX REPLACE "/[^/]*[*?].*$" "" asset_base "${asset_path}")
                        if(asset_base STREQUAL asset_path)
                            set(asset_base ".")
                        endif()
                        file(GLOB_RECURSE matched CONFIGURE_DEPENDS "${asset_dir}/${asset_base}/*")
                        list(APPEND asset_files ${matched})
                    else()
                        list(APPEND asset_files "${asset_dir}/${asset_path}")
                    endif()
                endif()
            endif()
        endforeach()
    endforeach()
    # The warnings it means to have, written only when they change, so tidec
    # runs again when they do.
    set(expected "${CMAKE_CURRENT_BINARY_DIR}/tide/${target}.warnings")
    list(JOIN ARG_WARNINGS "\n" warnings)
    set(written "-")
    if(EXISTS "${expected}")
        file(READ "${expected}" written)
    endif()
    if(NOT written STREQUAL warnings)
        file(WRITE "${expected}" "${warnings}")
    endif()
    # The game's files go to tidec as a list in a file (@list), written only
    # when it changes: a big game's are more than a command line holds
    # (32,767 characters on Windows).
    set(listed "${CMAKE_CURRENT_BINARY_DIR}/tide/${target}.sources")
    list(JOIN sources "\n" source_lines)
    if(package_lines)
        list(JOIN package_lines "\n" more_lines)
        string(APPEND source_lines "\n${more_lines}")
    endif()
    set(written "-")
    if(EXISTS "${listed}")
        file(READ "${listed}" written)
    endif()
    if(NOT written STREQUAL source_lines)
        file(WRITE "${listed}" "${source_lines}")
    endif()
    # Custom C and hosts may find assets by name through the C API, which
    # Tide expressions cannot describe. Keep their declarations, and layouts
    # used for reloading; prune only ordinary standalone optimized games.
    set(strip_assets OFF)
    set(server_flags)
    if(ARG_SERVER)
        list(APPEND server_flags --server)
        if(NOT c_sources OR ARG_STRIP_SERVER_ASSETS)
            list(APPEND server_flags --strip-server-assets)
        endif()
    endif()
    if(NOT ARG_HOST AND NOT c_sources AND NOT ARG_LAYOUT AND NOT ARG_EMBED)
        set(strip_assets "$<CONFIG:Release,RelWithDebInfo,MinSizeRel>")
    endif()
    set(strip_names OFF)
    # RelWithDebInfo and explicit visualizers are developer/profiling builds.
    if(NOT ARG_HOST AND NOT ARG_LAYOUT AND NOT ARG_EMBED AND NOT ARG_VISUALIZERS AND NOT TIDE_VISUALIZERS)
        set(strip_names "$<CONFIG:Release,MinSizeRel>")
    endif()
    # VISUALIZERS: how debuggers show the game's values, <name>.natvis (in a Windows program's .pdb) and
    # <name>_lldb.py, as tide's debug builds have them (compiler/src/debug_views_codegen.inc)
    set(visualizers "")
    set(visualizer_outputs "")
    if((ARG_VISUALIZERS OR TIDE_VISUALIZERS) AND NOT TIDE_WEB AND NOT TIDE_ANDROID)
        set(visualizers --visualizers)
        set(visualizer_outputs "${out_dir}/${name}.natvis" "${out_dir}/${name}_lldb.py")
    endif()
    add_custom_command(
        OUTPUT "${out_c}" "${out_h}" ${pack_outputs} ${visualizer_outputs}
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${out_dir}"
        COMMAND "${CMAKE_COMMAND}" "-DTIDEC=${TIDEC_PATH}" "-DEXPECTED=${expected}" "-DSTRIP_UNUSED_ASSETS=${strip_assets}" "-DSTRIP_NAMES=${strip_names}" -P "${TIDE_RUN_TIDEC}"
            -- "@${listed}" -o "${out_dir}" --name "${name}" ${layout} ${pack_c} ${defines} ${server_flags} ${visualizers}
        DEPENDS ${TIDEC_DEPENDS} ${sources} ${package_sources} ${asset_files} "${expected}" "${listed}" "${TIDE_RUN_TIDEC}"
        COMMENT "tidec ${target}"
        VERBATIM)

    if(ARG_HOST AND ARG_WINDOW AND TIDE_ANDROID)
        add_library(${target} SHARED ${ARG_HOST} "${out_c}" "${out_h}" "${out_pack_c}" ${c_sources})
        target_link_libraries(${target} PRIVATE tide tide_platform)
        tide_android_app(${target} LABEL "${title}")
    elseif(ARG_HOST)
        add_executable(${target} ${ARG_HOST} "${out_c}" "${out_h}" "${out_pack_c}" ${c_sources})
        if(ARG_EMBED)
            target_compile_definitions(${target} PRIVATE TIDE_HAS_EMBED=1)
        endif()
        target_link_libraries(${target} PRIVATE tide tide_font_bold)
    else()
        if(NOT TARGET tide_platform)
            message(FATAL_ERROR "tide_add_game(${target}): a game without HOST opens a window, which needs the "
                                "platform layer (TIDE_PLATFORM=ON, and platform/ added before this directory)")
        endif()
        set(stats false)
        if(ARG_STATS)
            set(stats true)
        endif()
        set(main "${out_dir}/main.c")
        if(ARG_SERVER)
            file(CONFIGURE OUTPUT "${main}" @ONLY CONTENT [[
// Generated by tide_add_game. Do not edit.
#include "@name@.h"
#include "tide/server_run.h"
int main(int argc, char **argv) { return tide_server_run(argc, argv, "@target@"); }
]])
        else()
        file(CONFIGURE OUTPUT "${main}" @ONLY CONTENT [[
// Generated by tide_add_game for @target@. Do not edit.
#include "@name@.h"
#include "tide/run.h"

int main(int argc, char **argv)
{
    tide_run(&(tide_run_desc){.@title_field@ = "@title@", .stats = @stats@, .argc = argc, .argv = argv});
}
]])
        endif()
        if(TIDE_ANDROID)
            add_library(${target} SHARED "${main}" "${out_c}" "${out_h}" ${c_sources})
            target_link_libraries(${target} PRIVATE tide_platform)
            tide_android_app(${target} LABEL "${title}" PACK "${out_pack}")
        else()
            add_executable(${target} "${main}" "${out_c}" "${out_h}" ${c_sources})
            target_link_libraries(${target} PRIVATE tide_platform)
            # Its assets beside it, as <target>.pack, when it has any (tide/run.h loads it)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND "${CMAKE_COMMAND}" "-DFROM=${out_pack}" "-DTO=$<TARGET_FILE_DIR:${target}>/${target}.pack"
                    -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/copy_pack.cmake"
                VERBATIM)
            if(ARG_SHELL)
                get_filename_component(shell "${ARG_SHELL}" ABSOLUTE)
                tide_web_page(${target} SHELL "${shell}" PACK "${out_pack}")
            else()
                tide_web_page(${target} PACK "${out_pack}")
            endif()
        endif()
    endif()
    if(NOT ARG_HOST)
        # Keep profiling information in RelWithDebInfo. Shipped Release and
        # MinSizeRel executables also omit linker symbol/debug tables.
        if(TIDE_WEB)
            target_link_options(${target} PRIVATE "$<$<CONFIG:Release,MinSizeRel>:LINKER:-s>")
        elseif(APPLE)
            target_link_options(${target} PRIVATE "$<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:LINKER:-dead_strip>"
                "$<$<CONFIG:Release,MinSizeRel>:LINKER:-S,-x>")
        elseif(WIN32 AND NOT MINGW)
            target_link_options(${target} PRIVATE "$<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:LINKER:/OPT:REF>")
        else()
            target_link_options(${target} PRIVATE "$<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:LINKER:--gc-sections>"
                "$<$<CONFIG:Release,MinSizeRel>:LINKER:-s>")
        endif()
    endif()
    target_include_directories(${target} PRIVATE "${out_dir}")
    if(visualizers)
        target_sources(${target} PRIVATE ${visualizer_outputs})
        # Every type the game declares: one only the generated plumbing names (a list's element, a map's
        # entry) would otherwise be left out with the plumbing's debug info, as tide's debug builds keep it;
        # and no columns, as generated C's would point into .tide lines at places that mean nothing there
        target_compile_options(${target} PRIVATE -fno-eliminate-unused-debug-types -gno-column-info)
        if(MINGW)
            target_link_options(${target} PRIVATE "LINKER:-Xlink=-natvis:${out_dir}/${name}.natvis")
        elseif(WIN32)
            target_link_options(${target} PRIVATE "LINKER:/NATVIS:${out_dir}/${name}.natvis")
        endif()
    endif()
    if(ARG_SERVER)
        target_compile_options(${target} PRIVATE -ffunction-sections -fdata-sections)
        if(APPLE)
            target_link_options(${target} PRIVATE "LINKER:-dead_strip")
        elseif(WIN32 AND NOT MINGW)
            target_link_options(${target} PRIVATE "LINKER:/OPT:REF")
        else()
            target_link_options(${target} PRIVATE "LINKER:--gc-sections")
        endif()
    endif()

    # `cmake --build --preset <preset> --target <target>_schedule` prints which
    # systems can run at the same time, and why the others wait.
    add_custom_target(${target}_schedule
        COMMAND ${TIDEC_COMMAND} "@${listed}" --schedule --name "${name}"
        DEPENDS ${TIDEC_DEPENDS}
        VERBATIM
        USES_TERMINAL)

    foreach(path IN LISTS manifest_paths)
        set_property(GLOBAL APPEND PROPERTY TIDE_GAME_LINES "${target}\t${path}")
    endforeach()
    foreach(line IN LISTS manifest_packages)
        set_property(GLOBAL APPEND PROPERTY TIDE_GAME_LINES "${target}\t${line}")
    endforeach()
    get_property(scheduled GLOBAL PROPERTY TIDE_GAMES_MANIFEST_SCHEDULED)
    if(NOT scheduled)
        set_property(GLOBAL PROPERTY TIDE_GAMES_MANIFEST_SCHEDULED TRUE)
        cmake_language(DEFER DIRECTORY "${PROJECT_SOURCE_DIR}" CALL tide_write_games_manifest)
    endif()
endfunction()

# Lists every game and its files, one "<game>\t<path>" per line, once all the
# games are known. A path ending in '/' is a folder: every .tide file in it and
# its subfolders. A file of a package the game is built with (PACKAGES) has the
# package's name after it: "<game>\t<path>\t<package>". tidels reads it to
# analyze a game's files together.
function(tide_write_games_manifest)
    get_property(lines GLOBAL PROPERTY TIDE_GAME_LINES)
    list(JOIN lines "\n" content)
    file(CONFIGURE OUTPUT "${TIDE_GAMES_MANIFEST}" CONTENT "${content}\n" @ONLY)
endfunction()
