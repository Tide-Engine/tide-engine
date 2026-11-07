#include "tide/attached.h"

#include <stdlib.h>
#include <string.h>

#include "tide/page.h"

// Derived local index. Entity bits never determine allocation sizes.
static uint32_t index_slot(const tide_attached *a, const tide_entity e)
{
    return (uint32_t)tide_entity_hash(0, e) & (a->indexed - 1u);
}

uint32_t tide_attached_find(const tide_attached *a, const tide_columns *c, const tide_entity e)
{
    if (!a->indexed) return TIDE_ATTACHED_NONE;
    for (uint32_t at = index_slot(a, e);; at = (at + 1u) & (a->indexed - 1u)) {
        const uint32_t row = a->index[at];
        if (!row) return TIDE_ATTACHED_NONE;
        if (tide_entity_equal(tide_attached_entity(a, c, row - 1u), e)) return row - 1u;
    }
}

static void insert_row(tide_attached *a, const tide_columns *c, const uint32_t row)
{
    uint32_t at = index_slot(a, tide_attached_entity(a, c, row));
    while (a->index[at]) at = (at + 1u) & (a->indexed - 1u);
    a->index[at] = row + 1u;
}

static void index_row(tide_attached *a, const tide_columns *c, const uint32_t row)
{
    if (((uint64_t)row + 1u) * 2u > a->indexed) {
        const uint32_t old = a->indexed;
        if (old > UINT32_MAX / 2u) tide_out_of_memory();
        a->indexed = old ? old * 2u : 16u;
        free(a->index);
        a->index = tide_alloc_zeroed(a->indexed, sizeof *a->index);
        for (uint32_t i = 0; i < row; i++) insert_row(a, c, i);
    }
    insert_row(a, c, row);
}

uint32_t tide_attached_add(tide_attached *a, const tide_columns *c, const tide_entity e, const uint32_t born)
{
    const uint32_t row = tide_table_add(&a->rows, c);
    *(tide_entity *)tide_table_cell(&a->rows, c, row, 0) = e;
    *(uint32_t *)tide_table_cell(&a->rows, c, row, 2) = born;
    index_row(a, c, row);
    return row;
}

void tide_attached_remove(tide_attached *a, const tide_columns *c, const uint32_t row)
{
    const tide_entity e = tide_attached_entity(a, c, row);
    uint32_t at = index_slot(a, e);
    while (a->index[at] != row + 1u) at = (at + 1u) & (a->indexed - 1u);
    a->index[at] = 0;
    // Reinsert the following cluster after deletion; keys remain exact.
    for (uint32_t next = (at + 1u) & (a->indexed - 1u); a->index[next]; next = (next + 1u) & (a->indexed - 1u)) {
        const uint32_t moved = a->index[next] - 1u;
        a->index[next] = 0;
        insert_row(a, c, moved);
    }
    const uint32_t last = a->rows.count - 1u;
    if (row != last) {
        at = index_slot(a, tide_attached_entity(a, c, last));
        while (a->index[at] != last + 1u) at = (at + 1u) & (a->indexed - 1u);
        a->index[at] = row + 1u;
    }
    tide_table_remove_row(&a->rows, c, row);
}

void tide_attached_free(tide_attached *a, const tide_columns *c)
{
    tide_table_free(&a->rows, c);
    free(a->index);
    memset(a, 0, sizeof *a);
}

uint32_t tide_attached_packed_size(const tide_attached *a, const tide_columns *c)
{
    return tide_table_packed_size(&a->rows, c);
}

void tide_attached_pack(const tide_attached *a, const tide_columns *c, tide_writer *w)
{
    tide_table_pack(&a->rows, c, w);
}

bool tide_attached_unpack(tide_attached *a, const tide_columns *c, tide_reader *r)
{
    if (!tide_table_unpack(&a->rows, c, r)) return false;
    for (uint32_t row = 0; row < a->rows.count; row++) {
        const tide_entity e = tide_attached_entity(a, c, row);
        if (tide_entity_is_null(e) || tide_attached_find(a, c, e) != TIDE_ATTACHED_NONE) {
            tide_attached_free(a, c);
            return false;
        }
        index_row(a, c, row);
    }
    return true;
}
