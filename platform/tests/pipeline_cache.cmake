# Runs tide_platform_pipeline_cache in its order (pipeline_cache.c): a run
# with no file, one that reads the file the last left, and each way a file
# can be wrong, each followed by a run that reads the file it was written
# over with. The game's temporary cache folder is DIR, not the user's.
#
#   cmake -DPROGRAM=<tide_platform_pipeline_cache> -DDIR=<folder> -P pipeline_cache.cmake

file(REMOVE_RECURSE "${DIR}")
file(MAKE_DIRECTORY "${DIR}")
set(ENV{TMP} "${DIR}")
set(ENV{TEMP} "${DIR}")
set(ENV{TMPDIR} "${DIR}")
foreach(mode fresh warm corrupt warm foreign warm cut warm)
    execute_process(COMMAND "${PROGRAM}" ${mode} RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
    message("--- ${mode}\n${out}${err}")
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "the ${mode} run failed (${result})")
    endif()
endforeach()
