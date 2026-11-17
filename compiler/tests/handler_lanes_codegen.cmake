if(NOT DEFINED GAME_C OR NOT EXISTS "${GAME_C}")
    message(FATAL_ERROR "GAME_C must name the generated handler_lanes C file")
endif()
file(READ "${GAME_C}" generated)
# A handler's dispatch puts back the lanes its code can write, each only where its bytes are no longer the table's,
# and none it can't: OnPoke's `a`, OnDeep's through its helpers (pos, inner.y, slots[2]), OnGrow's handles, a sparse
# component's. The bytes compared are the page's the row was read from...
set(a0 "tide_table_column_mut(tide_t, &tide_columns_arch0_Part_Plain, tide_c,")
set(again "if (memcmp((const uint8_t *)tide_table_column(tide_t, &tide_columns_arch0_Part_Plain, tide_c,")
foreach(required IN ITEMS
    "if (memcmp(tide_col_Part[1] + (size_t)tide_i * 4, (const uint8_t *)&tide_v1 + 4, 4) != 0) memcpy((uint8_t *)${a0} 3) + (size_t)tide_i * 4, (const uint8_t *)&tide_v1 + 4, 4)"
    "if (memcmp(tide_col_Part[3] + (size_t)tide_i * 4, (const uint8_t *)&tide_v1 + 12, 4) != 0) memcpy((uint8_t *)${a0} 5) + (size_t)tide_i * 4, (const uint8_t *)&tide_v1 + 12, 4)"
    "if (memcmp(tide_col_Part[6] + (size_t)tide_i * 4, (const uint8_t *)&tide_v1 + 24, 4) != 0) memcpy((uint8_t *)${a0} 8) + (size_t)tide_i * 4, (const uint8_t *)&tide_v1 + 24, 4)"
    "if (memcmp(tide_col_Part[9] + (size_t)tide_i * 4, (const uint8_t *)&tide_v1 + 36, 4) != 0) memcpy((uint8_t *)${a0} 11) + (size_t)tide_i * 4, (const uint8_t *)&tide_v1 + 36, 4)"
    "if (memcmp(tide_col_Part[15] + (size_t)tide_i * 4, (const uint8_t *)&tide_v1 + 60, 4) != 0) memcpy((uint8_t *)${a0} 17) + (size_t)tide_i * 4, (const uint8_t *)&tide_v1 + 60, 4)"
    "if (memcmp(tide_col_Mark[0] + (size_t)tide_i * 4, (const uint8_t *)&tide_v1 + 0, 4) != 0) memcpy((uint8_t *)${a0} 25) + (size_t)tide_i * 4, (const uint8_t *)&tide_v1 + 0, 4)"
    # ...but where the code may have had the row written another way meanwhile, the page is asked for again:
    # OnNative and OnSly reach C (every lane, and the one), OnStart and OnUndo start a task
    "${again} 20) + (size_t)tide_i * 4, (const uint8_t *)&tide_v1 + 0, 4) != 0) memcpy((uint8_t *)${a0} 20) + (size_t)tide_i * 4, (const uint8_t *)&tide_v1 + 0, 4)"
    "${again} 23) + (size_t)tide_i * 4, (const uint8_t *)&tide_v1 + 12, 4) != 0) memcpy((uint8_t *)${a0} 23) + (size_t)tide_i * 4, (const uint8_t *)&tide_v1 + 12, 4)"
    # A component its handler Sets through the World goes back by its entity, as before
    "tide_store_Plain(tide_w, tide_ent[tide_i], &tide_v1)"
    "tide_store_Part(tide_w, tide_ent[tide_i], &tide_v1)"
    # More than sixteen lanes go a run of them a call
    "tide_table_scatter(tide_t, &tide_columns_arch2_Field_Wide, tide_loc.row, 9, 21u, 4u, (const uint8_t *)&tide_v1 + 0)"
    # A handler copies the lanes its code reads and writes alone, and takes only their columns: OnPoke's `a`
    "const uint8_t *const tide_col_Part[18] = {NULL, (const uint8_t *)tide_table_column(tide_t, &tide_columns_arch0_Part_Plain, tide_c, 3), NULL, NULL,"
    "memcpy((uint8_t *)&tide_v1 + 4, tide_col_Part[1] + (size_t)tide_i * 4, 4)")
    string(FIND "${generated}" "${required}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "handler_lanes is missing: ${required}")
    endif()
endforeach()
# No handler takes its lanes for writing before its code ran, or puts a lane back whatever it holds
foreach(refused IN ITEMS
    "= {(uint8_t *)tide_table_column_mut("
    "tide_scatter_Part(tide_col_Part"
    "tide_scatter_some_Part(tide_col_Part"
    "tide_scatter_Plain(tide_col_Plain"
    "tide_scatter_some_Plain(tide_col_Plain"
    "tide_scatter_some_Mark(tide_col_Mark")
    string(FIND "${generated}" "${refused}" found)
    if(NOT found EQUAL -1)
        message(FATAL_ERROR "handler_lanes still has: ${refused}")
    endif()
endforeach()
# ...and copies the component whole where its code hands it to C (OnNative) or a task (OnStart), not OnPoke
foreach(handler IN ITEMS OnNative OnStart)
    string(REGEX MATCH "tide_gather_Plain\\(&tide_v1, [^\n]*\n *tide_handler_${handler}\\(" whole "${generated}")
    if(NOT whole)
        message(FATAL_ERROR "handler_lanes doesn't copy all of Plain for ${handler}")
    endif()
endforeach()
string(REGEX MATCH "tide_gather_Part\\(&tide_v1, [^\n]*\n *tide_handler_OnPoke\\(" whole "${generated}")
if(whole)
    message(FATAL_ERROR "handler_lanes copies all of Part for OnPoke")
endif()
# Lane `b` is put back by the handlers that can write it and no other: OnMaybe and OnSame in four tables, OnJoin
# in the two an entity without Extra is in. (No `;` in the pattern: a match with one is two of a list in some CMakes)
string(REGEX MATCHALL "tide_c, 4\\) \\+ \\(size_t\\)tide_i \\* 4, \\(const uint8_t \\*\\)&tide_v1 \\+ 8, 4\\)" lane_b "${generated}")
list(LENGTH lane_b count)
if(NOT count EQUAL 10)
    message(FATAL_ERROR "handler_lanes puts back lane b in ${count} places, not OnMaybe's and OnSame's four each and OnJoin's two")
endif()
# The pages asked for again are those four handlers' alone, in four tables each: every lane (4) of what OnNative
# hands C and of what OnStart's and OnUndo's task puts back itself, and the one lane OnSly's code can write
string(REGEX MATCHALL "if \\(memcmp\\(\\(const uint8_t \\*\\)tide_table_column\\(" asked "${generated}")
list(LENGTH asked count)
if(NOT count EQUAL 52)
    message(FATAL_ERROR "handler_lanes asks for a page again in ${count} places, not 52")
endif()
