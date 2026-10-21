# A field a component no longer has. A Unity prefab's (Tide text named after the asset, as tide unity gives it) is
# left out, saying nothing, and the game builds: code that still uses the field is what fails. A .tprefab's is an
# error, as in text written by hand it's a mistake, and though a prefab's values are checked on their own and again
# at each Spawn of it, it's said once. Run with TIDEC and OUT.
file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}/out")
file(WRITE "${OUT}/game.tide" "scene Main { }\n"
     "component Spin { float speed = 1; }\n"
     "event(Spawned) Go(with Main) { Spawn(Cube); Spawn(Cube); }\n"
     "system Turn(mut Spin spin) { spin.speed += 1; }\n")
set(prefab "prefab Cube\n{\n    Spin { speed = 2, test = 5 };\n}\n")

function(compile prefab_file expect_built times)
    file(WRITE "${OUT}/${prefab_file}" "${prefab}")
    execute_process(
        COMMAND "${TIDEC}" "${OUT}/game.tide" "${OUT}/${prefab_file}" -o "${OUT}/out"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr)
    file(REMOVE "${OUT}/${prefab_file}")
    if(expect_built AND NOT result EQUAL 0)
        message(FATAL_ERROR "tidec refused a Unity prefab setting a field its component no longer has:\n${stderr}")
    elseif(NOT expect_built AND result EQUAL 0)
        message(FATAL_ERROR "tidec built a .tprefab setting a field its component hasn't")
    endif()
    string(REGEX MATCHALL "has no field 'test'" said "${stderr}")
    list(LENGTH said count)
    if(NOT count EQUAL times)
        message(FATAL_ERROR "${prefab_file}: it was said ${count} times, not ${times}:\n${stderr}")
    endif()
    set(stderr "${stderr}" PARENT_SCOPE)
endfunction()

compile(Cube.prefab TRUE 0)

compile(cube.tprefab FALSE 1)
string(FIND "${stderr}" "error: component 'Spin' has no field 'test'" found)
string(FIND "${stderr}" "in Unity" unity)
if(found EQUAL -1 OR NOT unity EQUAL -1)
    message(FATAL_ERROR "the .tprefab's error isn't the plain one:\n${stderr}")
endif()
