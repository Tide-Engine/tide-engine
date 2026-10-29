foreach(source IN ITEMS GAME_C GAME_H)
    if(NOT DEFINED ${source} OR NOT EXISTS "${${source}}")
        message(FATAL_ERROR "${source} must name its generated sparse_cache file")
    endif()
endforeach()
file(READ "${GAME_C}" generated)
file(READ "${GAME_H}" header)
foreach(required IN ITEMS "#define TIDE_ROWS_CACHE_ABI 1" "struct tide_rows_cache *tide_rows_cache;")
    string(FIND "${header}" "${required}" found)
    if(found LESS 0)
        message(FATAL_ERROR "missing private cache ownership marker: ${required}")
    endif()
endforeach()

function(function_body name output)
    string(REGEX MATCH "\n(void|bool|uint32_t|uint64_t) ${name}\\([^\n]*\n\\{" signature "${generated}")
    if(NOT signature)
        message(FATAL_ERROR "missing generated ${name} definition")
    endif()
    string(FIND "${generated}" "${signature}" begin)
    string(SUBSTRING "${generated}" ${begin} -1 tail)
    string(FIND "${tail}" "\n}\n" end)
    if(end LESS 0)
        message(FATAL_ERROR "unterminated generated ${name}")
    endif()
    string(SUBSTRING "${tail}" 0 ${end} body)
    set(${output} "${body}" PARENT_SCOPE)
endfunction()

function_body(tide_world_copy copy)
if(copy MATCHES "tide_rows_cache")
    message(FATAL_ERROR "world_copy must leave the destination's owned cache alone")
endif()
function_body(tide_world_hash hash)
if(hash MATCHES "tide_rows_cache")
    message(FATAL_ERROR "selection cache pointers and metadata must not affect world hashes")
endif()
function_body(tide_world_free free)
if(NOT free MATCHES "tide_rows_cache_free\\(")
    message(FATAL_ERROR "world_free must release its cache ownership")
endif()
function_body(tide_world_pack pack)
string(FIND "${pack}" "header->tide_rows_cache = NULL;" clear)
string(FIND "${pack}" "tide_write_bytes" write)
if(clear LESS 0 OR write LESS 0 OR NOT clear LESS write)
    message(FATAL_ERROR "raw packing must clear the cache pointer before writing its header")
endif()
function_body(tide_world_unpack unpack)
string(FIND "${unpack}" "memcpy(w, data, sizeof *w);" read)
string(FIND "${unpack}" "w->tide_rows_cache = NULL;" clear)
string(FIND "${unpack}" "tide_entities_unpack" decode)
if(read LESS 0 OR clear LESS read OR decode LESS clear)
    message(FATAL_ERROR "raw unpacking must discard a received cache pointer before decoding can fail")
endif()
function_body(tide_world_tick_on tick)
string(FIND "${tick}" "tide_rows_cache_prepare(" prepare)
string(FIND "${tick}" "tide_run_systems(" run)
if(prepare LESS 0 OR run LESS 0 OR NOT prepare LESS run)
    message(FATAL_ERROR "selection caches must be prepared serially before systems/tasks run")
endif()
foreach(required IN ITEMS "tide_rows_cache_find(" "tide_rows_plan_prepare(" "refs == UINT32_MAX"
        "static __attribute__((noinline)) void tide_task_Update_slow("
        "if (tide_pieces != 1u) goto tide_fallback;"
        "if (tide_pages_Item[tide_lane]->refs != 1u) goto tide_fallback;"
        "tide_page_now->hashed = UINT32_MAX;"
        "tide_task_Update_slow(tide_world_v, tide_task, tide_pieces);"
        "memcmp(p->filter, filter" "entry->rows != tide_table_rows"
        "// Update's rows, selecting cached sparse indices in order"
        "const tide_rows_chunk *tide_selection"
        "const uint16_t *tide_selected = tide_selection->selected;"
        "const uint32_t tide_i = tide_selected[tide_pick];"
        "#if !defined(__wasm__)"
        "const bool tide_prefetch = tide_selection->pass < (tide_selection->rows + 63u) / 64u;"
        "tide_prefetch && tide_warm < tide_to"
        "tide_warm < tide_to && tide_warm - tide_from < 4u"
        "if (tide_prefetch && tide_to - tide_pick > 4u) {"
        "const uint32_t tide_prefetch_i = tide_selected[tide_pick + 4u];"
        "__builtin_prefetch(tide_col_Item[0] + (size_t)tide_prefetch_i * 4u, 1, 3);"
        "tide_cached && tide_cached->pass < (tide_cached->rows + 15u) / 16u"
        "tide_rows_cache_lower(tide_cached, tide_from)"
        "tide_rows_cache_lower(tide_cached, tide_to)")
    string(FIND "${generated}" "${required}" found)
    if(found LESS 0)
        message(FATAL_ERROR "missing validated private cache path: ${required}")
    endif()
endforeach()
