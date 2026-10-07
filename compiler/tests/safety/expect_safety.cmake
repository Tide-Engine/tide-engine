# Runs a program of safety/ (see ../CMakeLists.txt) and checks how it ended.
# Its .tide file starts with lines that say what's expected:
#
#   // expect: <text>   The program says this, anywhere in what it prints. One or more.
#   // stops: <text>    It stops there, having said this: the engine's own stop.
#   // ticks: <n>       How many ticks its host runs (6 without).
#   // draws            It draws: where tests have a GPU, a second test puts its frames through the renderer.
#
# A program that isn't expected to stop has to run to the end, which its host
# says. Either way it says nothing a sanitizer would: a report, there or after
# what was expected, fails the test.
#
# cmake -DRUN=<program> -DEMULATOR=<how this platform runs one, parts split by |> -DSOURCE=<file.tide> [-DGPU=gpu]
#       -P expect_safety.cmake

cmake_minimum_required(VERSION 3.25) # Its policies: a quoted "ticks" is those letters, not a variable of that name
file(READ "${SOURCE}" content)
string(REPLACE "\r" "" content "${content}")
set(expected "")
set(stops "")
set(tick_count "")
while(content MATCHES "^// (expect|stops|ticks): ([^\n]*)\n(.*)$")
    set(content "${CMAKE_MATCH_3}")
    # A ';' in a text would split the list
    string(REPLACE ";" "\;" text "${CMAKE_MATCH_2}")
    if(CMAKE_MATCH_1 STREQUAL "expect")
        list(APPEND expected "${text}")
    elseif(CMAKE_MATCH_1 STREQUAL "stops")
        set(stops "${text}")
    elseif(CMAKE_MATCH_1 STREQUAL "ticks")
        set(tick_count "${text}")
    endif()
endwhile()
if(expected STREQUAL "" AND stops STREQUAL "")
    message(FATAL_ERROR "${SOURCE}: its first lines say what's expected: `// expect: <text>` or `// stops: <text>`")
endif()

string(REPLACE "|" ";" emulator "${EMULATOR}")
execute_process(
    COMMAND ${emulator} "${RUN}" ${tick_count} ${GPU}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
    ENCODING UTF8 # What the program says is UTF-8, whatever the console's own is
    TIMEOUT 240)
set(said "${stdout}${stderr}")

foreach(report IN ITEMS "runtime error:" "Sanitizer" "SUMMARY:")
    string(FIND "${said}" "${report}" found)
    if(NOT found EQUAL -1)
        message(FATAL_ERROR "a sanitizer's report:\n${said}")
    endif()
endforeach()

foreach(text IN LISTS expected)
    string(FIND "${said}" "${text}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "expected it to say: ${text}\nit said (and ended with ${result}):\n${said}")
    endif()
endforeach()

string(FIND "${said}" "safety: ran to the end" ended)
if(stops STREQUAL "")
    if(NOT result EQUAL 0 OR ended EQUAL -1)
        message(FATAL_ERROR "expected it to run to the end; it ended with ${result}, having said:\n${said}")
    endif()
else()
    string(FIND "${said}" "${stops}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "expected it to stop saying: ${stops}\nit said (and ended with ${result}):\n${said}")
    endif()
    if(result EQUAL 0 OR NOT ended EQUAL -1)
        message(FATAL_ERROR "expected it to stop, and it ran to the end:\n${said}")
    endif()
endif()
