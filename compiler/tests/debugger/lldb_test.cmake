# A debugger stopping in Tide code with LLDB and the game's LLDB views
# (<name>_lldb.py, which tide's debug builds put beside a game): breakpoints on
# .tide lines, the call stack as Tide functions at .tide lines, a step landing
# on the next Tide line, locals under their Tide names, and text, lists, maps,
# sets, enums and entities shown as Tide's. LLDB reads the heaps' thread-locals
# on macOS and Linux; Windows uses the natvis (natvis_test.cmake).
#
#   cmake -DLLDB=<lldb> -DPROGRAM=<fixture program> -DVIEWS=<game_lldb.py> -DFIXTURE=<fixture.tide>
#         -DWORK=<scratch folder> -P lldb_test.cmake

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

file(STRINGS "${FIXTURE}" lines)
set(number 0)
foreach(line IN LISTS lines)
    math(EXPR number "${number} + 1")
    if(line MATCHES "// \\[([a-z]+)\\]$")
        set(at_${CMAKE_MATCH_1} ${number})
    endif()
endforeach()
foreach(marker IN ITEMS sum gained recent call pairs)
    if(NOT at_${marker})
        message(FATAL_ERROR "${FIXTURE} has no line marked [${marker}]")
    endif()
endforeach()

file(WRITE "${WORK}/commands.txt" "\
command script import \"${VIEWS}\"
breakpoint set --file fixture.tide --line ${at_pairs} --ignore-count 2
run
bt 3
frame variable
frame variable *player
frame variable *totals
breakpoint delete --force
breakpoint set --file fixture.tide --line ${at_gained}
continue
next
bt 1
breakpoint delete --force
breakpoint set --file fixture.tide --line ${at_sum}
continue
bt 2
frame variable
kill
")
execute_process(COMMAND "${LLDB}" --batch --no-lldbinit --source "${WORK}/commands.txt" -- "${PROGRAM}"
    RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 300)
file(WRITE "${WORK}/said.txt" "${out}${err}")

# What LLDB said for the `index`th command that was `command` (from 1): its
# batch output echoes each as "(lldb) <command>"
function(said_for command index result)
    set(rest "\n${out}")
    set(found "")
    foreach(i RANGE 1 ${index})
        string(FIND "${rest}" "\n(lldb) ${command}\n" at)
        if(at EQUAL -1)
            set(${result} "" PARENT_SCOPE)
            return()
        endif()
        math(EXPR at "${at} + 1")
        string(SUBSTRING "${rest}" ${at} -1 rest)
        set(found "${rest}")
        string(SUBSTRING "${rest}" 1 -1 rest)
    endforeach()
    string(FIND "${found}" "\n(lldb) " next)
    if(NOT next EQUAL -1)
        string(SUBSTRING "${found}" 0 ${next} found)
    endif()
    set(${result} "${found}" PARENT_SCOPE)
endfunction()

set(failures "")
function(expect command index pattern why)
    said_for("${command}" ${index} text)
    if(NOT text MATCHES "${pattern}")
        set(failures "${failures}\n- ${why}: `${command}` should match `${pattern}`, and said:\n${text}" PARENT_SCOPE)
    endif()
endfunction()

expect("bt 3" 1 "frame #0: [^\n]*tide_system_Score[^\n]* at fixture\\.tide:${at_pairs}" "the stopped frame is the system, at its .tide line")
expect("frame variable" 1 "\\) gained = " "a local has its Tide name")
expect("frame variable" 1 "tide_lc = the engine's\n" "the engine's caches beside a function's code show nothing of theirs")
expect("bt 3" 1 "at fixture\\.tide:${at_pairs}\n" "no column: generated C's would point into the .tide line at a place that means nothing")
expect("frame variable" 1 "recent = count = 3 {\n *\\[0\\] = 1\n *\\[1\\] = 2\n" "a local list shows its elements")
expect("frame variable *player" 1 "history = count = [1-9][0-9]* {\n *\\[0\\] = [0-9]+" "a list field shows its elements")
expect("frame variable *player" 1 "name = \"(ana|bo)\"" "a text field is its text, from the heap")
expect("frame variable *player" 1 "mode = Running" "an enum field is its member's name")
expect("frame variable *player" 1 "buddy = (none|Entity\\([0-9]+:[0-9]+\\))" "an entity field is an entity")
expect("frame variable *player" 1 "scores = count = 1 {\n *\\[\"ann\"\\] = [0-9]+" "a map's entries are its values by key")
expect("frame variable *player" 1 "seen = count = [1-9]" "a set says its count")
expect("frame variable *player" 1 "facing = \\(0, 0, 0, 1\\)" "a quaternion is its x, y, z and w")
expect("frame variable *totals" 1 "pairs = count = [1-9][0-9]* {\n *\\[0\\] = [({][^)}]*a = 2[^)}]*b = 0\\.5" "a list of structs shows their fields")
expect("bt 1" 1 "frame #0: [^\n]*tide_system_Score[^\n]* at fixture\\.tide:${at_recent}"
    "a step from a statement lands on the next Tide line, not on generated C")
expect("bt 2" 1 "frame #0: [^\n]*tide_function_Sum[^\n]* at fixture\\.tide:${at_sum}\n[^\n]*frame #1: [^\n]*tide_system_Score[^\n]* at fixture\\.tide:${at_call}(\n|$)"
    "the stack is the Tide function at its line, then its caller at the call")
expect("frame variable" 2 "values = count = 3 {\n *\\[0\\] = 1" "a list parameter shows its elements")
if(failures)
    message(FATAL_ERROR "LLDB didn't show the game as Tide:${failures}\n\nAll it said is in ${WORK}/said.txt")
endif()
