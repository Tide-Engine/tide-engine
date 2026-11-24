# Every .tide file of the repository made a graph, and the graph's Tide parsed again: the same syntax tree as the
# file's (tidec --graph-roundtrip, notes/visual-scripting.md). Files that don't parse, as error tests' don't, are
# left out by tidec. Hidden folders and build trees aren't the repository's source.
#   cmake -DTIDEC=<tidec> -DROOT=<repository> -DLIST=<scratch file> -P graph_roundtrip.cmake
file(GLOB_RECURSE found LIST_DIRECTORIES false "${ROOT}/*.tide")
set(lines "")
foreach(path IN LISTS found)
    file(RELATIVE_PATH relative "${ROOT}" "${path}")
    if(relative MATCHES "(^|/)\\." OR relative MATCHES "^build/" OR relative MATCHES "(^|/)node_modules/")
        continue()
    endif()
    string(APPEND lines "${path}\n")
endforeach()
file(WRITE "${LIST}" "${lines}")
execute_process(COMMAND "${TIDEC}" --graph-roundtrip "@${LIST}" RESULT_VARIABLE result ERROR_VARIABLE said)
message("${said}")
if(NOT result EQUAL 0)
    message(FATAL_ERROR "some files aren't the same as graphs: see above")
endif()
