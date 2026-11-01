#pragma once

#include "tide/mesh.h"

// Derived, bounded, frame-local metadata only. No source storage is retained:
// heap is compared as an identity after resolving the current handle again.
enum { TIDE_LIST_DATA_CACHE_ENTRIES = 8 };
typedef struct tide_list_data_entry {
    uintptr_t heap;
    tide_handle at;
    uint64_t heap_used, page_hash, id, version, kind;
    uint32_t hashed, element_size, words, layout, slot;
    int32_t held, count, width, height;
    tide_list_words_fn *write;
} tide_list_data_entry;

typedef struct tide_list_data_cache {
    uint32_t next;
    tide_list_data_entry entries[TIDE_LIST_DATA_CACHE_ENTRIES];
} tide_list_data_cache;
