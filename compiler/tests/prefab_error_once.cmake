# A prefab's values are checked on their own and again at each Spawn of it: an error in them is said once, not once
# a check. One in a Unity prefab (Tide text named after the asset, as tide unity gives it) says to remove it in Unity's
# inspector. Run with TIDEC and OUT.
file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}")
file(WRITE "${OUT}/game.tide" "scene Main { }\n"
     "component Spin { float speed = 1; }\n"
     "event(Spawned) Go(with Main) { Spawn(Cube); Spawn(Cube); local Spawn(Cube); }\n"
     "system Turn(mut Spin spin) { spin.speed += 1; }\n")
file(WRITE "${OUT}/Cube.prefab" "prefab Cube\n{\n    Spin { speed = 2, test = 5 };\n}\n")
execute_process(
    COMMAND "${TIDEC}" "${OUT}/game.tide" "${OUT}/Cube.prefab" -o "${OUT}/out"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)
if(result EQUAL 0)
    message(FATAL_ERROR "tidec built a prefab with a field its component hasn't")
endif()
string(REGEX MATCHALL "has no field 'test'" said "${stderr}")
list(LENGTH said count)
if(NOT count EQUAL 1)
    message(FATAL_ERROR "the error was said ${count} times, not once:\n${stderr}")
endif()
string(FIND "${stderr}" "it's set in Unity, on a TideEntity's Spin" note)
if(note EQUAL -1)
    message(FATAL_ERROR "the error doesn't say to remove it in Unity:\n${stderr}")
endif()
