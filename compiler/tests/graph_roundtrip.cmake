# Every .tide file of the repository made a graph, and the graph's Tide parsed again: the same syntax tree as the
# file's (tidec --graph-roundtrip, notes/visual-scripting.md). Files that don't parse, as error tests' don't, are
# left out by tidec. Hidden folders and build trees aren't the repository's source.
#   cmake -DTIDEC=<tidec> -DROOT=<repository> -DLIST=<scratch file> -P graph_roundtrip.cmake
# A tidec lets go of nothing until it ends, so the files go to it in batches, each a tidec of its own: all of them in
# one took more memory than a Linux runner has, which stopped the runner.
set(batch 200)
file(GLOB_RECURSE found LIST_DIRECTORIES false "${ROOT}/*.tide")
set(paths "")
foreach(path IN LISTS found)
    file(RELATIVE_PATH relative "${ROOT}" "${path}")
    if(relative MATCHES "(^|/)\\." OR relative MATCHES "^build/" OR relative MATCHES "(^|/)node_modules/")
        continue()
    endif()
    list(APPEND paths "${path}")
endforeach()
list(LENGTH paths count)
set(failed FALSE)
set(start 0)
while(start LESS count)
    list(SUBLIST paths ${start} ${batch} some)
    list(JOIN some "\n" lines)
    file(WRITE "${LIST}" "${lines}\n")
    execute_process(COMMAND "${TIDEC}" --graph-roundtrip "@${LIST}" RESULT_VARIABLE result ERROR_VARIABLE said)
    message("${said}")
    if(NOT result EQUAL 0)
        set(failed TRUE)
    endif()
    math(EXPR start "${start} + ${batch}")
endwhile()
if(failed)
    message(FATAL_ERROR "some files aren't the same as graphs: see above")
endif()
