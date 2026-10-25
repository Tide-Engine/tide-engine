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

# Removing every producer while keeping declared handles must preserve the
# serial registry, or a later reload could reuse one of their identities.
file(MAKE_DIRECTORY "${OUT}/no_producers")
file(WRITE "${OUT}/no_producers.tide" "local singleton State { LocalTask<int> kept; }\nscene Main;\n")
execute_process(COMMAND "${TIDEC}" "${OUT}/no_producers.tide" -o "${OUT}/no_producers" --name game --layout
    RESULT_VARIABLE result ERROR_VARIABLE diagnostics)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "handle-only layout failed: ${diagnostics}")
endif()
file(READ "${OUT}/no_producers/game.h" header)
if(NOT header MATCHES "tide_local_task_slots tide_local_task_slots;")
    message(FATAL_ERROR "declared handles must retain their serial registry without producers")
endif()
