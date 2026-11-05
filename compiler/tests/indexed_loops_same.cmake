# Runs one game built with its loops' indexes (INDEXED) and without (PLAIN), and checks that what they print as
# `ixloops` lines (indexed_loops_test.c: their worlds' hashes and what their views drew, tick by tick) is the same,
# and that both passed their own tests.
#
# cmake -DINDEXED=<program> -DPLAIN=<program> -DEMULATOR=<how this platform runs one, parts split by |>
#       -P indexed_loops_same.cmake

string(REPLACE "|" ";" emulator "${EMULATOR}")
set(lines "")
foreach(program IN ITEMS "${INDEXED}" "${PLAIN}")
    execute_process(COMMAND ${emulator} "${program}" RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${program} failed (${result}):\n${out}\n${err}")
    endif()
    string(REPLACE "\r" "" out "${out}")
    string(REGEX MATCHALL "ixloops [^\n]*" found "${out}")
    list(LENGTH found count)
    if(count LESS 80)
        message(FATAL_ERROR "${program} printed ${count} ticks:\n${out}")
    endif()
    list(APPEND lines "${found}")
endforeach()
list(LENGTH lines total)
math(EXPR half "${total} / 2")
math(EXPR end "${half} - 1")
foreach(i RANGE ${end})
    math(EXPR j "${i} + ${half}")
    list(GET lines ${i} a)
    list(GET lines ${j} b)
    if(NOT a STREQUAL b)
        message(FATAL_ERROR "indexed and plain loops differ:\n  indexed: ${a}\n  plain:   ${b}")
    endif()
endforeach()
message(STATUS "${half} ticks the same")
