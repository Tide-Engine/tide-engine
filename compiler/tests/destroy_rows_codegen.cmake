if(NOT DEFINED GAME_C OR NOT EXISTS "${GAME_C}")
    message(FATAL_ERROR "GAME_C must name the generated destroy_rows C file")
endif()
file(READ "${GAME_C}" generated)
foreach(required IN ITEMS
    "bool tide_update_Move("
    "bool tide_destroy[128]"
    "tide_destroy[tide_i - tide_base] = tide_update_Move("
    "if (tide_destroy[tide_i - tide_base]) tide_cmd_destroy(tide_w, tide_ent[tide_i]);"
    "tide_destroy_at(w, c->entity, loc);")
    string(FIND "${generated}" "${required}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "destroy_rows is missing its optimized path: ${required}")
    endif()
endforeach()
# Observe has an external C observer; CustomStep calls an event-sending helper
# and uses a user operator.
# Both must retain the original interleaved update/command recording.
foreach(fallback IN ITEMS Observe CustomStep)
    string(FIND "${generated}" "bool tide_update_${fallback}(" found)
    if(NOT found EQUAL -1)
        message(FATAL_ERROR "destroy_rows optimized ineligible system ${fallback}")
    endif()
endforeach()
