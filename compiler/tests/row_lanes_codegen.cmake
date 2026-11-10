if(NOT DEFINED GAME_C OR NOT EXISTS "${GAME_C}")
    message(FATAL_ERROR "GAME_C must name the generated row_lanes C file")
endif()
file(READ "${GAME_C}" generated)

# The text of one function, from the line that names it to its closing brace
function(function_body start output)
    string(FIND "${generated}" "${start}" begin)
    if(begin EQUAL -1)
        message(FATAL_ERROR "row_lanes is missing: ${start}")
    endif()
    string(SUBSTRING "${generated}" ${begin} -1 tail)
    string(FIND "${tail}" "\n}\n" end)
    string(SUBSTRING "${tail}" 0 ${end} body)
    set(${output} "${body}" PARENT_SCOPE)
endfunction()

function(require body what why)
    string(FIND "${body}" "${what}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "row_lanes: ${why} (missing: ${what})")
    endif()
endfunction()

function(refuse body what why)
    string(FIND "${body}" "${what}" found)
    if(NOT found EQUAL -1)
        message(FATAL_ERROR "row_lanes: ${why} (found: ${what})")
    endif()
endfunction()

# Step's rows: the lane its test reads, the test, then the rest for the rows it lets through, and never the
# whole component
function_body("TIDE_ROWS void tide_rows_Step_0(" step)
require("${step}" "if (!tide_guard_Step(tide_w" "Step's rows don't ask its guard")
require("${step}" "memcpy((uint8_t *)&tide_v0 + " "Step's rows don't copy lanes of their own")
refuse("${step}" "tide_gather_Fat(" "Step's rows copy the whole component")
string(FIND "${step}" "if (!tide_guard_Step(" guard_at)
string(FIND "${step}" "tide_system_Step(" call_at)
string(FIND "${step}" "tide_scatter_some_Fat(" put_at)
if(NOT (guard_at LESS call_at AND call_at LESS put_at))
    message(FATAL_ERROR "row_lanes: Step's rows should test, then run, then put back")
endif()

# A guard is each test its code starts with, the parts of an `||` apart
function_body(" bool tide_guard_Count(" count)
require("${count}" "if ((f->kind != Kind_Human)) return true;" "Count's guard lacks its first test")
require("${count}" "if ((!f->live)) return true;" "Count's guard lacks its second test")
function_body(" bool tide_guard_Empty(" empty)
require("${empty}" "tide_list_count(f->cargo) == 0" "Empty's guard doesn't read its list's count")

# Only the plain part of a test is a guard: the rest is the code's, for the rows that part lets through
function_body(" bool tide_guard_Lead(" lead)
refuse("${lead}" "tide_has_Fat" "Lead's guard asks the world, which isn't a plain test")
# A system that reaches the world still puts back only the lanes it writes, and copies only those it reads
function_body("TIDE_ROWS void tide_rows_Reach_0(" reach)
refuse("${reach}" "tide_gather_Fat(" "Reach's rows copy the whole component")
refuse("${reach}" "tide_scatter_Fat(" "Reach's rows put back the whole component")
function_body("TIDE_ROWS void tide_rows_Lead_0(" lead_rows)
refuse("${lead_rows}" "tide_gather_Fat(" "Lead's rows copy the whole component")
require("${lead_rows}" "tide_scatter_some_Fat(" "Lead's rows don't put back the lanes it writes alone")

# A row its guard reads nothing of a component for is handed none of it there
function_body("TIDE_ROWS void tide_rows_Flag_0(" flag)
refuse("${flag}" "tide_guard_Flag" "Flag starts with no test, yet has a guard")
refuse("${flag}" "tide_gather_Fat(" "Flag's rows copy the whole component")

# Calls: a singleton's lists where they are beside a Set of another component, and beside a list of another
# thing to change; a copy where the Set, or the list changed, could be what was handed over
require("${generated}" "tide_function_Visit(tide_w, tide_bindings, &((*f)), TIDE_IN_MATCH, (*ground));"
    "Visit is handed a copy of the ground, though it Sets another component")
require("${generated}" "tide_function_Append(tide_w, tide_bindings, tide_copy_Other(row.o), row.entity"
    "Append isn't handed a copy of the component it Sets")
require("${generated}" "tide_function_Route((*ground), tide_copy_Fat((*f)), "
    "Route should read the ground where it is, and a copy of the Fat whose path it changes")

# A view that runs once makes its rows where its code first names its query: after the return before it,
# and not in the function that runs it
function_body(" void tide_view_Early(" early)
string(FIND "${early}" "if (!gate->open)" gate_at)
string(FIND "${early}" "tide_q0 = tide_query_Early_0(tide_vf->w" fill_at)
if(gate_at EQUAL -1 OR fill_at EQUAL -1 OR NOT gate_at LESS fill_at)
    message(FATAL_ERROR "row_lanes: Early should make its rows after its early return")
endif()
function_body("static void tide_run_view_Early(" early_run)
refuse("${early_run}" "tide_query_Early_0(" "Early's rows are made before it runs")
require("${early_run}" "const tide_view_frame tide_frame = {" "Early isn't handed what its rows are made from")
function_body(" void tide_view_Hands(" hands)
require("${hands}" "tide_q0 = tide_query_Hands_0(tide_vf->w" "Hands doesn't make its rows")

# A component with a [Position] field: a view copies some of its lanes, the corrected ones among them, and
# corrects the copy
function_body("static void tide_run_view_Each(" each)
require("${each}" "tide_gather_some_Fat(&tide_v0" "Each copies the whole component")
require("${each}" "tide_correct_view_Fat(tide_l, tide_w, tide_ent[tide_i], &tide_v0);" "Each doesn't correct its copy")
