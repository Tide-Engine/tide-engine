# A Unity project's game with Box3D and Box2D (tests/unity_physics: bodies, shapes by the scale, parts, a joint, a
# mesh), checked by tide schedule --assets against the real packages: their unity.json's rules bake prefabs and scenes
# that compile with the packages as they are, and their Tide for a Unity project compiles with them.
#
#   cmake -DTIDE=<tide> -DSOURCE=<repository> -DWORK=<scratch folder> -P unity_physics_test.cmake

file(REMOVE_RECURSE "${WORK}")
file(COPY "${SOURCE}/compiler/cli/tests/unity_physics/" DESTINATION "${WORK}")
file(WRITE "${WORK}/ProjectSettings/ProjectVersion.txt" "m_EditorVersion: 6000.6.0f1\n")
file(WRITE "${WORK}/Assets/tide.packages" "${SOURCE}/packages/box3d\n${SOURCE}/packages/box2d\n")
file(WRITE "${WORK}/Assets/Game.tide" "scene Main { }\n")
set(ENV{TIDE_NO_UPDATE_CHECK} 1)

execute_process(COMMAND "${TIDE}" schedule Assets --assets Assets WORKING_DIRECTORY "${WORK}"
    RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT code EQUAL 0)
    message(FATAL_ERROR "tide schedule --assets failed (${code}):\n${out}${err}")
endif()
foreach(text IN ITEMS "Box3D.StartAtTransform" "Box3D.PartOfTheBodyAbove" "Box2D.PartOfTheBodyAbove")
    string(FIND "${out}" "${text}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "the schedule should have ${text}, and is:\n${out}")
    endif()
endforeach()
# A part turned against its body, and one that's a part of nothing, are said
foreach(text IN ITEMS "warning: Fin is turned against Cart" "warning: Stray's Box3D.Part is part of nothing")
    string(FIND "${err}" "${text}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "tide should have said '${text}', and said:\n${err}")
    endif()
endforeach()
