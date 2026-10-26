# A debugger stopping in Tide code on Windows, through Windows' own debugger
# engine (dbgeng.dll, which WinDbg and cdb are, with the natvis support Visual
# Studio, VS Code and CLion's debuggers share): breakpoints on .tide lines,
# the call stack as Tide functions at .tide lines, a step landing on the next
# Tide line, locals under their Tide names, and the game's natvis, from the
# program's .pdb, showing text, lists, maps, sets, enums and entities.
#
#   cmake -DDRIVE=<tide_dbgeng_drive> -DPROGRAM=<fixture program> -DFIXTURE=<fixture.tide>
#         -DWORK=<scratch folder> -P natvis_test.cmake

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

# The lines the test stops at, by their markers
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
.lines -e
l+t
bp `fixture.tide:${at_pairs}` 3
g
.nvlist
k 3
dv
dx -r2 *player
dx -r3 *totals
bc *
bp `fixture.tide:${at_gained}`
g
p
k 1
bc *
bp `fixture.tide:${at_sum}`
g
k 2
dv
")
execute_process(COMMAND "${DRIVE}" "${WORK}/commands.txt" "${PROGRAM}"
    RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 300)
file(WRITE "${WORK}/said.txt" "${out}${err}")
if(NOT code EQUAL 0)
    message(FATAL_ERROR "the debugger driver failed (${code}):\n${out}${err}")
endif()

# What the debugger said for the `index`th command that was `command` (from 1)
function(said_for command index result)
    set(rest "\n${out}")
    set(found "")
    foreach(i RANGE 1 ${index})
        string(FIND "${rest}" "\n> ${command}\n" at)
        if(at EQUAL -1)
            set(${result} "" PARENT_SCOPE)
            return()
        endif()
        math(EXPR at "${at} + 1")
        string(SUBSTRING "${rest}" ${at} -1 rest)
        set(found "${rest}")
        string(SUBSTRING "${rest}" 1 -1 rest)
    endforeach()
    string(FIND "${found}" "\n> " next)
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

expect(".nvlist" 1 "embedded NatVis \"[^\"]*game\\.natvis\"" "the natvis comes from the program's .pdb")
expect("k 3" 1 "tide_system_Score\\+0x[0-9a-f]+ \\[[^]\n]*fixture\\.tide @ ${at_pairs}\\]"
    "the stopped frame is the system, at its .tide line")
expect("dv" 1 "\n +gained = " "a local has its Tide name")
expect("dv" 1 "\n +recent = { count = 0x3 }" "a local list says its count")
expect("dv" 1 "\n +player = " "a parameter has its Tide name")
expect("dx -r2 *player" 1 "\n +history +: { count = 0x[1-9]" "a list field says its count")
expect("dx -r2 *player" 1 "\n +\\[0x0\\] +: [0-9]+ \\[Type: int\\]" "a list field's elements are its element type")
expect("dx -r2 *player" 1 "\n +name +: \"(ana|bo)\"" "a text field is its text, from the heap")
expect("dx -r2 *player" 1 "\n +mode +: Running" "an enum field is its member's name")
expect("dx -r2 *player" 1 "\n +buddy +: (none|Entity\\(0x[0-9a-f]+:0x[0-9a-f]+\\))" "an entity field is an entity")
expect("dx -r2 *player" 1 "\n +scores +: { count = 0x1 }" "a map field says its count")
expect("dx -r2 *player" 1 "\n +\\[[^\n]*ann[^\n]*\\] +: [0-9]+ \\[Type: int\\]" "a map's entries are its values by key")
expect("dx -r2 *player" 1 "\n +seen +: { count = 0x[1-9]" "a set field says its count")
expect("dx -r3 *totals" 1 "\n +pairs +: { count = 0x[1-9]" "a singleton's list of structs says its count")
expect("dx -r3 *totals" 1 "\n +\\[0x0\\] +\\[Type: Pair\\]\n[^\n]* a +: 2 \\[Type: int\\]" "a list's struct elements show their fields")
expect("dx -r3 *totals" 1 "\n[^\n]* b +: 0\\.5" "a list's struct elements show their fields")
expect("k 1" 1 "tide_system_Score\\+0x[0-9a-f]+ \\[[^]\n]*fixture\\.tide @ ${at_recent}\\]"
    "a step from a statement lands on the next Tide line, not on generated C")
expect("k 2" 1 "tide_function_Sum\\+0x[0-9a-f]+ \\[[^]\n]*fixture\\.tide @ ${at_sum}\\][^\n]*\n[^\n]*tide_system_Score\\+0x[0-9a-f]+ \\[[^]\n]*fixture\\.tide @ ${at_call}\\]"
    "the stack is the Tide function at its line, then its caller at the call")
expect("dv" 2 "\n +values = { count = 0x3 }" "a list parameter says its count")
expect("dv" 2 "\n +total = " "a mut local has its Tide name")
if(failures)
    message(FATAL_ERROR "The debugger didn't show the game as Tide:${failures}\n\nAll it said is in ${WORK}/said.txt")
endif()
