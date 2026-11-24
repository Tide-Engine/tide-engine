# A graph's errors name the node they're about (notes/visual-scripting.md, Diagnostics): tidec fails on GRAPH, and
# what it says matches EXPECT, a regular expression.
#   cmake -DTIDEC=<tidec> -DGRAPH=<file.tidegraph> -DOUT=<scratch folder> -DEXPECT=<regex> -P graph_errors.cmake
file(MAKE_DIRECTORY "${OUT}")
execute_process(COMMAND "${TIDEC}" "${GRAPH}" -o "${OUT}" --name game RESULT_VARIABLE result ERROR_VARIABLE said OUTPUT_VARIABLE printed)
if(result EQUAL 0)
    message(FATAL_ERROR "tidec built ${GRAPH}, which has errors")
endif()
if(NOT said MATCHES "${EXPECT}")
    message(FATAL_ERROR "tidec said:\n${said}\nwhich doesn't match: ${EXPECT}")
endif()
