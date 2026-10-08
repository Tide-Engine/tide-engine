# Container fields migrate as heap bytes. A transitive map must distinguish
# its entry representation even when the field or a task frame is one handle.
set(control [=[
struct Box { int value; }
singleton State
{
    List<Box> saved;
    List<int> plain;
    int done;
}
]=])
set(mapped [=[
struct Box { Map<int, int> value; }
singleton State
{
    Map<int, int> direct;
    Set<int> keys;
    List<Map<int, int>> nested;
    List<Set<int>> nestedSets;
    List<Box> saved;
    Box boxed;
    List<int> plain;
    int done;
}
]=])
set(common [=[
scene Main { }
async void Visit(List<Box> boxes, mut State state)
{
    await Wait.Ticks(1);
    state.done = boxes.count;
}
system Begin(mut State state)
{
    Visit(state.saved, state);
}
]=])
foreach(kind IN ITEMS control mapped)
    file(MAKE_DIRECTORY "${OUT}/${kind}")
    file(WRITE "${OUT}/${kind}.tide" "${${kind}}${common}")
    execute_process(COMMAND "${TIDEC}" "${OUT}/${kind}.tide" -o "${OUT}/${kind}" --name game --layout
        RESULT_VARIABLE result ERROR_VARIABLE diagnostics)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${kind} map layout failed: ${diagnostics}")
    endif()
    file(READ "${OUT}/${kind}/game.c" ${kind}_generated)
    file(READ "${OUT}/${kind}/game.h" ${kind}_header)
    string(REGEX MATCH "\"Visit [0-9A-F]+\"" fingerprint "${${kind}_generated}")
    if(NOT fingerprint)
        message(FATAL_ERROR "${kind} map layout has no Visit task fingerprint")
    endif()
    set(${kind}_fingerprint "${fingerprint}")
endforeach()

foreach(field IN ITEMS
        "\"direct\", \"Map<int, int>#map-order-2\""
        "\"keys\", \"Set<int>#map-order-2\""
        "\"nested\", \"List<Map<int, int>>#map-order-2\""
        "\"nestedSets\", \"List<Set<int>>#map-order-2\""
        "\"saved\", \"List<Box>#map-order-2\""
        "\"value\", \"Map<int, int>#map-order-2\""
        "\"boxed\", \"Box\""
        "\"plain\", \"List<int>\"")
    string(FIND "${mapped_generated}" "${field}" found)
    if(found LESS 0)
        message(FATAL_ERROR "mapped layout lacks ${field}")
    endif()
endforeach()
string(FIND "${mapped_header}" "// Map ABI: signed 64-bit insertion order (2)." found)
if(found LESS 0)
    message(FATAL_ERROR "mapped header lacks the entry ABI marker for matchmaking")
endif()
if(control_header MATCHES "Map ABI" OR control_generated MATCHES "#map-order-2")
    message(FATAL_ERROR "the map-free control must retain its unmarked header and layout")
endif()
if(control_fingerprint STREQUAL mapped_fingerprint)
    message(FATAL_ERROR "a map inside a task's List<Box> must change its fingerprint despite unchanged task text")
endif()
