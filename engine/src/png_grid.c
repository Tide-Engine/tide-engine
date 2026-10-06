// grid.Decode(bytes): a PNG into a Grid2<Color> field of a world, with the
// engine's own reader (tide/png.h), so it's the same pixels on every machine.
// Apart from the reader, which tidec compiles too to pack a game's assets and
// needs nothing of the worlds.

#include "tide/png.h"

#include <stdlib.h>
#include <string.h>


typedef struct grid_rows {
    tide_grid grid; // The new grid, in the world's heap, until it's done
    const tide_grid_shape *shape;
    uint32_t where;
} grid_rows;

static bool grid_began(void *user, const uint32_t width, const uint32_t height)
{
    grid_rows *g = user;
    g->grid = tide_grid_new(2, (int32_t)width, (int32_t)height, 0);
    tide_grid_own(&g->grid, g->shape, g->where);
    return g->grid.at != 0;
}

static inline bool color_is_zero(const tide_color *c)
{
    uint32_t bits[4];
    memcpy(bits, c, sizeof bits);
    return !(bits[0] | bits[1] | bits[2] | bits[3]);
}

static bool grid_row(void *user, const tide_png_row *row)
{
    grid_rows *g = user;
    const int32_t y = (int32_t)row->y;
    if (row->dx != 1) { // An interlaced pass: its pixels are apart
        for (uint32_t i = 0; i < row->count; i++) {
            const tide_color *c = &row->pixels[i];
            if (color_is_zero(c)) continue;
            void *cell = tide_grid_poke(&g->grid, (int32_t)(row->x0 + i * row->dx), y, 0, g->shape, true, g->where);
            if (!cell) return false;
            memcpy(cell, c, sizeof *c);
        }
        return true;
    }
    // A whole row: the cells of one chunk are side by side, so each chunk's
    // span goes in one copy, and a span of zeros makes no chunk
    const uint32_t across = 1u << g->shape->shift[0];
    for (uint32_t x = 0; x < row->count;) {
        uint32_t n = across - x % across;
        if (n > row->count - x) n = row->count - x;
        bool any = false;
        for (uint32_t i = 0; i < n && !any; i++) any = !color_is_zero(&row->pixels[x + i]);
        if (any) {
            void *cell = tide_grid_poke(&g->grid, (int32_t)x, y, 0, g->shape, true, g->where);
            if (!cell) return false;
            memcpy(cell, row->pixels + x, (size_t)n * sizeof(tide_color));
        }
        x += n;
    }
    return true;
}

tide_io_error tide_grid_decode(tide_grid *g, const tide_grid_shape *s, const uint32_t where, const tide_list bytes)
{
    if (s->dims != 2 || s->cell != sizeof(tide_color) || !tide_heap_of(where)) return TIDE_IO_INVALID_DATA;
    const int32_t count = tide_list_count(bytes);
    bool copied = false;
    const uint8_t *data = count ? tide_list_flatten(bytes, 1, false, &copied) : NULL;
    grid_rows rows = {.shape = s, .where = where};
    const bool ok = tide_png_decode_rows(data, (size_t)(count > 0 ? count : 0), grid_began, grid_row, &rows);
    if (count) tide_list_unflatten(bytes, (void *)data, 1, false, copied);
    if (!ok) {
        tide_grid_release(&rows.grid, s, where);
        return TIDE_IO_INVALID_DATA;
    }
    tide_grid_release(g, s, where);
    *g = rows.grid;
    return TIDE_IO_NONE;
}
