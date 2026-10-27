# A file an asset glob matched, deleted, must leave the game's pack: Ninja
# never runs a command again because a dependency went away, so
# tide_add_game depends on the list of asset files (<target>.assets), written
# only when it changes. This makes a game of one text glob in a project of its
# own that includes this checkout's cmake/Tide.cmake, packs it, deletes one
# of the files and builds again, as an editor's user would.
#
# cmake -DTIDEC=<tidec> -DENGINE=<checkout> -DOUT=<folder> -DTOOLCHAIN=<toolchain file>
#       -DMAKE_PROGRAM=<ninja> -P asset_deletion.cmake

set(project "${OUT}/project")
set(build "${OUT}/build")
file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${project}/notes")
file(WRITE "${project}/notes/kept.txt" "KEPT-NOTE")
file(WRITE "${project}/notes/gone.txt" "GONE-NOTE")
file(WRITE "${project}/game.tide" "asset List<string> notes = \"notes/*.txt\";\nscene Main { }\n")
file(WRITE "${project}/host.c" "int main(void) { return 0; }\n")
file(WRITE "${project}/CMakeLists.txt" "cmake_minimum_required(VERSION 3.25)
project(asset_deletion C)
# Tide.cmake finds the engine's own files under PROJECT_SOURCE_DIR.
set(PROJECT_SOURCE_DIR \"${ENGINE}\")
set(TIDEC_PATH \"${TIDEC}\")
set(TIDEC_DEPENDS \"${TIDEC}\")
include(\"${ENGINE}/cmake/Tide.cmake\")
set(PROJECT_SOURCE_DIR \"${project}\")
tide_add_game(game SOURCES game.tide HOST host.c)
")

function(run)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${ARGN} failed:\n${output}")
    endif()
endfunction()

# tidec writes the pack beside the C array a HOST program embeds, the output
# the build knows: building that file runs tidec.
set(pack "${build}/tide/game/game.pack")
set(pack_c "${build}/tide/game/game_pack.c")
run("${CMAKE_COMMAND}" -S "${project}" -B "${build}" -G Ninja "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN}"
    "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
run("${CMAKE_COMMAND}" --build "${build}" --target "${pack_c}")
file(READ "${pack}" first HEX)
string(HEX "GONE-NOTE" gone)
string(HEX "KEPT-NOTE" kept)
string(FIND "${first}" "${gone}" before)
if(before EQUAL -1)
    message(FATAL_ERROR "the first pack hasn't got gone.txt")
endif()

file(REMOVE "${project}/notes/gone.txt")
run("${CMAKE_COMMAND}" --build "${build}" --target "${pack_c}")
file(READ "${pack}" second HEX)
string(FIND "${second}" "${gone}" after)
string(FIND "${second}" "${kept}" still)
if(NOT after EQUAL -1)
    message(FATAL_ERROR "the deleted file is still packed")
endif()
if(still EQUAL -1)
    message(FATAL_ERROR "the pack lost the file that's still there")
endif()

# A new file in the glob's folder is packed too.
file(WRITE "${project}/notes/new.txt" "NEW-NOTE")
run("${CMAKE_COMMAND}" --build "${build}" --target "${pack_c}")
file(READ "${pack}" third HEX)
string(HEX "NEW-NOTE" added)
string(FIND "${third}" "${added}" found)
if(found EQUAL -1)
    message(FATAL_ERROR "the added file isn't packed")
endif()
message(STATUS "a deleted asset file leaves the pack, an added one joins it")
