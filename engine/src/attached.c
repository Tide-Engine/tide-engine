#include "tide/attached.h"

#include <stdlib.h>
#include <string.h>

#include "tide/page.h"

// Entity indices rows unpacked from bytes may have: far more than a world has
// entities, and few enough that the index of them fits in memory.
#define MOST_INDEXED (1u << 28)

uint32_t tide_attached_find_other(const tide_attached *a, const tide_columns *c, const tide_entity e)
{
    for (uint32_t i = 0; i < a->other_count; i++) {
        if (tide_entity_equal(tide_attached_entity(a, c, a->others[i]), e)) return a->others[i];
    }
    return TIDE_ATTACHED_NONE;
}

// Where `row` is found from: its entity's index, or the list of the others.
static void index_row(tide_attached *a, const tide_entity e, const uint32_t row)
{
    if (e.index >= a->indexed) {
        uint32_t room = a->indexed ? a->indexed : 16u;
        while (room <= e.index) {
            if (room > UINT32_MAX / 2u) tide_out_of_memory();
            room *= 2u;
        }
        a->by_index = tide_realloc(a->by_index, (size_t)a->indexed * sizeof *a->by_index, (size_t)room * sizeof *a->by_index);
        memset(a->by_index + a->indexed, 0, (size_t)(room - a->indexed) * sizeof *a->by_index);
        a->indexed = room;
    }
    if (!a->by_index[e.index]) {
        a->by_index[e.index] = row + 1u;
        return;
    }
    if (a->other_count == a->other_room) {
        const uint32_t room = a->other_room ? a->other_room * 2u : 8u;
        a->others = tide_realloc(a->others, (size_t)a->other_room * sizeof *a->others, (size_t)room * sizeof *a->others);
        a->other_room = room;
    }
    a->others[a->other_count++] = row;
}

uint32_t tide_attached_add(tide_attached *a, const tide_columns *c, const tide_entity e, const uint32_t born)
{
    const uint32_t row = tide_table_add(&a->rows, c);
    *(tide_entity *)tide_table_cell(&a->rows, c, row, 0) = e;
    *(uint32_t *)tide_table_cell(&a->rows, c, row, 2) = born;
    index_row(a, e, row);
    return row;
}

void tide_attached_remove(tide_attached *a, const tide_columns *c, const uint32_t row)
{
    const tide_entity e = tide_attached_entity(a, c, row);
    const uint32_t last = a->rows.count - 1u;
    const tide_entity moved = tide_attached_entity(a, c, last);

    // Its place goes to another generation of its index, if one has a row
    if (a->by_index[e.index] == row + 1u) {
        a->by_index[e.index] = 0;
        for (uint32_t i = 0; i < a->other_count; i++) {
            if (tide_attached_entity(a, c, a->others[i]).index != e.index) continue;
            a->by_index[e.index] = a->others[i] + 1u;
            a->others[i] = a->others[--a->other_count];
            break;
        }
    } else {
        for (uint32_t i = 0; i < a->other_count; i++) {
            if (a->others[i] != row) continue;
            a->others[i] = a->others[--a->other_count];
            break;
        }
    }
    // The last row moves into its place
    if (row != last) {
        if (a->by_index[moved.index] == last + 1u) {
            a->by_index[moved.index] = row + 1u;
        } else {
            for (uint32_t i = 0; i < a->other_count; i++) {
                if (a->others[i] == last) a->others[i] = row;
            }
        }
    }
    tide_table_remove_row(&a->rows, c, row);
}

void tide_attached_free(tide_attached *a, const tide_columns *c)
{
    tide_table_free(&a->rows, c);
    free(a->by_index);
    free(a->others);
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
        // An index no world could have would only make `by_index` huge
        if (tide_entity_is_null(e) || e.index >= MOST_INDEXED || tide_attached_find(a, c, e) != TIDE_ATTACHED_NONE) {
            tide_attached_free(a, c);
            return false;
        }
        index_row(a, e, row);
    }
    return true;
}
