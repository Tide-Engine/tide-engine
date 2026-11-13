# Runs one game built with its repeated pure calls merged and its cached functions remembering (MERGED) and with
# neither (PLAIN), and checks that what they print as `purecalls` lines (pure_calls_test.c: their worlds' hashes
# and what their view drew, tick by tick) is the same, and that both passed their own tests. Then reads the C
# tidec made of each (MERGED_C, PLAIN_C): the merged one has the calls it should have made one of, and no more.
#
# cmake -DMERGED=<program> -DPLAIN=<program> -DMERGED_C=<game.c> -DPLAIN_C=<game.c>
#       -DEMULATOR=<how this platform runs one, parts split by |> -P pure_calls_same.cmake

string(REPLACE "|" ";" emulator "${EMULATOR}")
set(lines "")
foreach(program IN ITEMS "${MERGED}" "${PLAIN}")
    execute_process(COMMAND ${emulator} "${program}" RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${program} failed (${result}):\n${out}\n${err}")
    endif()
    string(REPLACE "\r" "" out "${out}")
    string(REGEX MATCHALL "purecalls [^\n]*" found "${out}")
    list(LENGTH found count)
    if(count LESS 48)
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
        message(FATAL_ERROR "merged and cached calls differ from the calls as written:\n  merged: ${a}\n  plain:  ${b}")
    endif()
endforeach()
message(STATUS "${half} ticks the same")

file(READ "${MERGED_C}" merged)
file(READ "${PLAIN_C}" plain)
# The calls one was made of, each by the local it's worked out into
set(wanted
    "tide_same1 = tide_function_Bounds\\(catalog, part\\)"                 # Width: both sides of a subtraction
    "tide_same1 = tide_function_Corners\\(catalog, part, scale\\)"         # Spread: two loops over one list
    "tide_same1 = tide_function_Width\\(\\(\\*catalog\\), body->part\\)"   # Measure: four calls in two statements
    "tide_same2 = tide_function_Half\\(tide_same1\\)"                      # ...a call of one that became a local
    "tide_same3 = tide_function_Spread\\("
    "tide_same4 = tide_function_Half\\(step\\)"                            # ...inside a loop's body
    "tide_same5 = tide_function_Scaled\\(body->reach, 2.0f\\)"             # ...named and left-out arguments
    "tide_same6 = tide_function_Scatter\\("
    "tide_same1 = tide_function_Curve\\(body->reach, 40\\)"                # Remember: a cached one's too
    "tide_same1 = tide_function_Outline\\("
    "tide_same2 = tide_function_Heaviest\\("
    "tide_same1 = tide_function_Curve\\(0.75f, 12\\)")                     # Show: a view's
foreach(call IN LISTS wanted)
    if(NOT merged MATCHES "${call}")
        message(FATAL_ERROR "no one call for: ${call}")
    endif()
endforeach()
string(REGEX MATCHALL "tide_same[0-9]+ = " made "${merged}")
list(LENGTH made count)
list(LENGTH wanted expected)
if(NOT count EQUAL expected)
    message(FATAL_ERROR "${count} calls were made one of, where ${expected} are the same: one that isn't was merged, or one that is wasn't")
endif()
# What must stay as written: after a local changed, a call that may not have run, a function that rolls
if(merged MATCHES "tide_same[0-9]+ = tide_function_Width\\(\\(\\*catalog\\), part\\)")
    message(FATAL_ERROR "Width(catalog, part) was merged across a change of 'part'")
endif()
if(merged MATCHES "tide_same[0-9]+ = tide_function_Half\\(body->reach\\)")
    message(FATAL_ERROR "Half(body.reach) was taken from a call on the right of '&&', which may not run")
endif()
if(merged MATCHES "tide_same[0-9]+ = tide_function_Roll")
    message(FATAL_ERROR "Roll was merged: each call rolls")
endif()
# The plain build has neither
if(plain MATCHES "tide_same[0-9]+ = " OR plain MATCHES "tide_cached_")
    message(FATAL_ERROR "the build with neither still merges calls or keeps results")
endif()
if(NOT merged MATCHES "static tide_cached tide_cached_table_function_Curve;")
    message(FATAL_ERROR "the cached function Curve has no table")
endif()
message(STATUS "${count} calls made one of")
