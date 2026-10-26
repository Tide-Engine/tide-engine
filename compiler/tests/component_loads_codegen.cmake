# The E2E pins values/layouts. Also guard the constant-size entity-load path:
# correctness alone would accept the old generic per-lane memcpy calls.
file(READ "${GAME_C}" generated)
foreach(component IN ITEMS LoadBytes LoadPair LoadWide LoadTag LoadOptional
        tide_local_component_LoadBytes tide_local_component_LoadPair tide_local_component_LoadWide tide_local_component_LoadTag)
    string(FIND "${generated}" "TIDE_HELPER bool tide_load_${component}(" begin)
    if(begin LESS 0)
        message(FATAL_ERROR "missing ${component} load helper")
    endif()
    string(SUBSTRING "${generated}" ${begin} -1 tail)
    string(FIND "${tail}" "\n}\n" end)
    if(end LESS 0)
        message(FATAL_ERROR "unterminated ${component} load helper")
    endif()
    string(SUBSTRING "${tail}" 0 ${end} body)
    if(body MATCHES "tide_table_gather" OR NOT body MATCHES "memcpy\\([^\n]*tide_table_get\\([^\n]*, [124]\\);")
        message(FATAL_ERROR "${component} must load through fixed-size lane copies")
    endif()
endforeach()
