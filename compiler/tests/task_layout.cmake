# Equal physical handle/list sizes must not make changed result schemas
# compatible across a reload.
set(common [=[
struct Box { Leaf value; }
local singleton State { LocalTask<List<Box>> build; bool started; }
async List<Box> Build()
{
    await Wait.Frames(1);
    return [Box { value = Leaf { amount = 7 } }];
}
view Start(mut State state)
{
    if (!state.started) { state.started = true; state.build = Build(); }
}
scene Main;
]=])
foreach(kind IN ITEMS integer floating)
    if(kind STREQUAL integer)
        set(leaf "struct Leaf { int amount; }\n")
    else()
        set(leaf "struct Leaf { float amount; }\n")
    endif()
    file(MAKE_DIRECTORY "${OUT}/${kind}")
    file(WRITE "${OUT}/${kind}.tide" "${leaf}${common}")
    execute_process(COMMAND "${TIDEC}" "${OUT}/${kind}.tide" -o "${OUT}/${kind}" --name game --layout
        RESULT_VARIABLE result ERROR_VARIABLE diagnostics)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${kind} task layout failed: ${diagnostics}")
    endif()
    file(READ "${OUT}/${kind}/game.c" generated)
    string(REGEX MATCH "\"Build [0-9A-F]+\"" running "${generated}")
    string(REGEX MATCH "\"tide_completed_function_Build [0-9A-F]+\"" completed "${generated}")
    if(NOT running OR NOT completed)
        message(FATAL_ERROR "${kind} layout lacks running/completed fingerprints")
    endif()
    set(${kind}_running "${running}")
    set(${kind}_completed "${completed}")
endforeach()
if(integer_running STREQUAL floating_running OR integer_completed STREQUAL floating_completed)
    message(FATAL_ERROR "nested result schema change must invalidate running and cached task rows")
endif()
