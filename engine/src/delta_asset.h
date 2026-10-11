#pragma once

#include "asset_store.h"

// Heap-only framing. Ordinary ECS regions retain the existing delta format.
void tide_delta_asset_region(tide_delta_writer *d, const void *now, uint32_t size,
    const void *base, uint32_t base_size, bool same, const tide_asset_origin *origin);
tide_page *tide_delta_asset_page(tide_delta_reader *d, uint32_t size, uint32_t page_size,
    uint32_t refs, tide_page *base, uint32_t base_size, bool share, tide_asset_origin *origin);
