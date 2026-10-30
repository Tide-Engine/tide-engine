foreach(source IN ITEMS GAME_C SCENE_C)
    if(NOT DEFINED ${source} OR NOT EXISTS "${${source}}")
        message(FATAL_ERROR "${source} must name its generated C file")
    endif()
endforeach()
file(READ "${GAME_C}" generated)

function(rows_body text system selection output)
    string(FIND "${text}" "// ${system}'s rows, ${selection}," begin)
    if(begin LESS 0)
        message(FATAL_ERROR "missing ${system} ${selection} rows")
    endif()
    string(SUBSTRING "${text}" ${begin} -1 tail)
    string(FIND "${tail}" "\n}\n" end)
    if(end LESS 0)
        message(FATAL_ERROR "unterminated ${system} ${selection} rows")
    endif()
    string(SUBSTRING "${tail}" 0 ${end} body)
    set(${output} "${body}" PARENT_SCOPE)
endfunction()

foreach(system IN ITEMS Update Record)
    rows_body("${generated}" "${system}" "selecting sparse matching bits in order" body)
    foreach(required IN ITEMS
        "tide_to - tide_base < 16u ? tide_to : tide_base + 16u"
        "if (tide_end - tide_base == 16u) {"
        "memcpy(&tide_presence, tide_pres_NeededA + tide_base, sizeof tide_presence);"
        "memcpy(&tide_presence, tide_pres_NeededB + tide_base, sizeof tide_presence);"
        "memcpy(&tide_presence, tide_pres_SkippedA + tide_base, sizeof tide_presence);"
        "memcpy(&tide_presence, tide_pres_SkippedB + tide_base, sizeof tide_presence);"
        "tide_mask = (tide_signs16)(tide_presence != (tide_bytes16){0});"
        "tide_mask &= (tide_signs16)(tide_presence == (tide_bytes16){0});"
        "__builtin_ia32_pmovmskb128(tide_mask)"
        "__builtin_wasm_bitmask_i8x16(tide_mask)"
        "(uint32_t)(tide_mask[tide_lane] != 0) << tide_lane"
        "tide_scan < tide_end"
        "<< (tide_scan - tide_base)"
        "while (tide_bits) {\n            const uint32_t tide_i = tide_base + (uint32_t)__builtin_ctz(tide_bits);"
        "tide_bits &= tide_bits - 1u;")
        string(FIND "${body}" "${required}" found)
        if(found LESS 0)
            message(FATAL_ERROR "${system} is missing its bounded sparse mask path: ${required}")
        endif()
    endforeach()
endforeach()
string(FIND "${generated}" "if (tide_pass < (tide_to - tide_from + 15u) / 16u) tide_rows_Update_" found)
if(found LESS 0)
    message(FATAL_ERROR "Update must select its sparse mask variant only for rare matches")
endif()
# Apart loops still visit/copy skipped rows, and scenes use the established
# index-selection/ordinary paths rather than the sparse-only vector mask.
string(FIND "${generated}" "// Update's rows, each lane it changes read in one place and written in another" begin)
if(begin LESS 0)
    message(FATAL_ERROR "missing Update's apart rows")
endif()
string(SUBSTRING "${generated}" ${begin} -1 tail)
string(FIND "${tail}" "\n}\n" end)
string(SUBSTRING "${tail}" 0 ${end} apart)
if(apart MATCHES "tide_bits" OR NOT apart MATCHES "do \\{" OR NOT apart MATCHES "break;")
    message(FATAL_ERROR "Update's apart rows must retain copying through skipped rows")
endif()
file(READ "${SCENE_C}" scenes)
foreach(system IN ITEMS ArenaVisit OutsideVisit)
    string(FIND "${scenes}" "// ${system}'s rows, selecting sparse matching bits" found)
    if(NOT found LESS 0)
        message(FATAL_ERROR "scene-filtered ${system} must retain its existing filtering paths")
    endif()
endforeach()
