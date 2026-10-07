# tide run in a run nobody watches (TIDE_HIDDEN in the environment, as an
# agent's shell and a build server have it): the game would show no window, so
# a run that only its window's closing would end is refused before anything is
# built, with what to give it instead. A run with no window, or on the web,
# goes on as ever, and so does every run without the variable.
#
# The tide here is alone in its folder, with no engine files to build with, so
# a run that goes on stops there, saying so: nothing is built, and no game
# starts, whatever this test gets wrong.
#
# cmake -DTIDE=<the tide program> -DWORK=<a scratch folder> -P hidden_test.cmake

cmake_minimum_required(VERSION 3.25)
foreach(var TIDE WORK)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "Pass -D${var}=...")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORK}")
file(COPY "${TIDE}" DESTINATION "${WORK}/install/bin")
get_filename_component(tide_name "${TIDE}" NAME)
set(tide "${WORK}/install/bin/${tide_name}")
set(game "${WORK}/game")
file(WRITE "${game}/game.tide" "scene Main { }\n")

# Runs `tide run` on the game with the arguments after `hidden`: TIDE_HIDDEN's
# value, or `unset` for an environment without it. Its exit code and what it
# said go to `code` and `said`.
function(tide_run hidden)
    if(hidden STREQUAL "unset")
        set(variable --unset=TIDE_HIDDEN)
    else()
        set(variable "TIDE_HIDDEN=${hidden}")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${variable} TIDE_NO_UPDATE_CHECK=1 "${tide}" run "${game}" ${ARGN}
        RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 60)
    set(code "${result}" PARENT_SCOPE)
    set(said "${out}${err}" PARENT_SCOPE)
endfunction()

# Refused: exit code 2, with what a run takes instead, and nothing made in the game's folder.
function(refused what hidden)
    tide_run("${hidden}" ${ARGN})
    if(NOT code EQUAL 2)
        message(FATAL_ERROR "${what} should be refused with exit code 2, not ${code}: ${said}")
    endif()
    foreach(text "TIDE_HIDDEN is set" "--frames <n>" "--capture <file.png>" "--script <file>")
        string(FIND "${said}" "${text}" at)
        if(at EQUAL -1)
            message(FATAL_ERROR "${what} should say '${text}': ${said}")
        endif()
    endforeach()
    if(EXISTS "${game}/.tide")
        message(FATAL_ERROR "${what} should build nothing, but the game has a .tide folder")
    endif()
    message(STATUS "ok: ${what} is refused")
endfunction()

# Not refused: it goes on to build, which this tide can't.
function(goes_on what hidden)
    tide_run("${hidden}" ${ARGN})
    string(FIND "${said}" "the engine files aren't next to tide" at)
    string(FIND "${said}" "TIDE_HIDDEN" refusal)
    if(NOT code EQUAL 1 OR at EQUAL -1 OR NOT refusal EQUAL -1)
        message(FATAL_ERROR "${what} should go on to build (exit code ${code}): ${said}")
    endif()
    message(STATUS "ok: ${what} goes on")
endfunction()

refused("a hidden run with nothing to end it" 1)
refused("one that hosts a match" 1 --host)
refused("one given only a size" 1 --size 320 200)
refused("an optimized one" yes --release --stats)
refused("one that watches files" 1 --watch)

goes_on("a hidden run of a counted number of frames" 1 --frames 3)
goes_on("one with a script" 1 --script play.txt)
goes_on("one on the web" 1 --web)
goes_on("one watching files on the web" 1 --web --watch)
goes_on("a run with the variable unset" unset)
goes_on("one with it at 0" 0)
goes_on("one that hosts a match, with it unset" unset --host)
goes_on("one watching files with it unset" unset --watch)

# --watch is opt-in for interactive runs. A counted run or a script always
# runs once, so asking to watch one is refused before it builds anything.
foreach(option --frames --capture --script)
    tide_run(1 --watch "${option}" 3)
    string(FIND "${said}" "leave out --watch" at)
    if(NOT code EQUAL 2 OR at EQUAL -1 OR EXISTS "${game}/.tide")
        message(FATAL_ERROR "--watch with ${option} should be refused before building: ${said}")
    endif()
endforeach()

execute_process(COMMAND "${CMAKE_COMMAND}" -E env TIDE_NO_UPDATE_CHECK=1 "${tide}" build "${game}" --watch
    RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 60)
string(FIND "${err}" "build doesn't take '--watch'" at)
if(NOT code EQUAL 2 OR at EQUAL -1 OR EXISTS "${game}/.tide")
    message(FATAL_ERROR "--watch is run's, not build's: ${out}${err}")
endif()
