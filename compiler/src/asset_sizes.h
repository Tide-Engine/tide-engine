#pragma once
#include <stdint.h>
#include <stdio.h>

// What each asset declaration adds to a pack (codegen_options.asset_size_out),
// gathered, and printed biggest first: tidec --asset-sizes and tide build's.
typedef struct asset_sizes {
    struct asset_size {
        char *name, *what;
        uint64_t bytes;
    } *items;
    int count, room;
} asset_sizes;

// codegen_options.asset_size_out, with an asset_sizes as its user.
void asset_sizes_add(void *sizes, const char *name, const char *what, uint64_t bytes);
// A line for each, biggest first (ties by name), the total first. Nothing for a pack with no assets.
void asset_sizes_print(FILE *to, asset_sizes *sizes);
void asset_sizes_free(asset_sizes *sizes);
