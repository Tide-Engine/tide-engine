# Runs tide_platform_prewarm in its order (prewarm.c): a run with no file of
# how the game's shaders drew, one that reads the file the first left, and
# each way the file can be wrong, each followed by a run that reads the file
# it was written over with. The game's temporary cache folder is DIR, not the
# user's.
#
#   cmake -DPROGRAM=<tide_platform_prewarm> -DDIR=<folder> -P prewarm.cmake

file(REMOVE_RECURSE "${DIR}")
file(MAKE_DIRECTORY "${DIR}")
set(ENV{TMP} "${DIR}")
set(ENV{TEMP} "${DIR}")
set(ENV{TMPDIR} "${DIR}")
foreach(mode first again corrupt again cut again)
    execute_process(COMMAND "${PROGRAM}" ${mode} RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
    message("--- ${mode}\n${out}${err}")
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "the ${mode} run failed (${result})")
    endif()
endforeach()
