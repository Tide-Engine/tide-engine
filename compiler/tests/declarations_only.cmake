# tidec --declarations: the game's types build while its code doesn't, as tide unity builds them for the C# of a Unity
# project's components. A game whose handler spawns a Unity prefab that isn't made yet fails as a whole and builds
# declared alone, with its component and event in the header and none of its code; a component's own error still
# fails. Run with TIDEC and OUT.
file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}/out")
file(WRITE "${OUT}/game.tide" "scene Main { }\n"
     "component Ball { float bounce = 0.5; }\n"
     "event Bounced { float speed; }\n"
     "event(Spawned) Go(with Main) { Spawn(UnityEngine.Prefabs.Roller); }\n"
     "system Roll(mut Ball ball) { ball.bounce += Missing(); }\n"
     "float Twice(float x) { return x * Unknown; }\n")

function(compile expect_built)
    execute_process(
        COMMAND "${TIDEC}" "${OUT}/game.tide" -o "${OUT}/out" --embed ${ARGN}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr)
    if(expect_built AND NOT result EQUAL 0)
        message(FATAL_ERROR "tidec ${ARGN} didn't build:\n${stderr}")
    elseif(NOT expect_built AND result EQUAL 0)
        message(FATAL_ERROR "tidec ${ARGN} built a game it shouldn't have")
    endif()
    set(stderr "${stderr}" PARENT_SCOPE)
endfunction()

compile(FALSE)
compile(TRUE --declarations)
# Warnings it may have (nothing sends an event, with the code left out): tide unity runs it quietly
string(FIND "${stderr}" "error" said)
if(NOT said EQUAL -1)
    message(FATAL_ERROR "the declarations' build said an error:\n${stderr}")
endif()
file(READ "${OUT}/out/game.h" header)
foreach(name Ball Bounced)
    string(FIND "${header}" "typedef struct ${name} {" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "the declarations' header has no ${name}")
    endif()
endforeach()
file(READ "${OUT}/out/game.c" code)
foreach(name "Roll[^b]" Twice) # Not Prediction's Rollback
    string(REGEX MATCH "${name}" found "${code}")
    if(found)
        message(FATAL_ERROR "the declarations' code has ${name}, which is the game's code")
    endif()
endforeach()

file(APPEND "${OUT}/game.tide" "component Broken { Nothing value; }\n")
compile(FALSE --declarations)
string(FIND "${stderr}" "Nothing" said)
if(said EQUAL -1)
    message(FATAL_ERROR "a component's own error wasn't said:\n${stderr}")
endif()
