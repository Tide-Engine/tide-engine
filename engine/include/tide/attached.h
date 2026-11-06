#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "tide/entity.h"
#include "tide/net.h"
#include "tide/table.h"

// State one world keeps for another world's entities: the local fields of the
// match's components, which are this machine's (docs/spec.md, Local state).
// Generated code calls these with each one's tide_columns.
//
// It's a table with three columns: the entity each row is of, its value,
// which is one column, so code reads and writes a value where it is, and what
// the entity was born as when the row was made (tide_entity_born). A row is
// found from its entity's handle. Only entities something was kept for have a
// row, so what's kept costs nothing for the others.
//
// The entities are another world's, so nothing here says whether they're
// alive: whoever keeps the rows takes out the ones whose entities are gone.
// The legacy born column is presentation metadata, never an alternate key.
// Distinct logical entities can both have rows, even when their worlds use
// different private storage slots.

typedef struct tide_attached {
    tide_table rows;     // Column 0: the entity each row is of. Column 1: its value. Column 2: what it was born as
    uint32_t *index;     // Open-addressed exact logical keys, storing row + 1
    uint32_t indexed;    // Power-of-two index capacity
} tide_attached;

#define TIDE_ATTACHED_NONE 0xFFFFFFFFu

// The entity a row is of.
static inline tide_entity tide_attached_entity(const tide_attached *a, const tide_columns *c, const uint32_t row)
{
    return *(const tide_entity *)tide_table_get(&a->rows, c, row, 0);
}

// An entity's row, or TIDE_ATTACHED_NONE when nothing is kept for it.
uint32_t tide_attached_find(const tide_attached *a, const tide_columns *c, tide_entity e);

// A row's value, to read, and to change.
static inline const void *tide_attached_get(const tide_attached *a, const tide_columns *c, const uint32_t row)
{
    return tide_table_get(&a->rows, c, row, 1);
}

static inline void *tide_attached_cell(tide_attached *a, const tide_columns *c, const uint32_t row)
{
    return tide_table_cell(&a->rows, c, row, 1);
}

// What a row's entity was born as when the row was made, or 0 if it wasn't known.
static inline uint32_t tide_attached_born(const tide_attached *a, const tide_columns *c, const uint32_t row)
{
    return *(const uint32_t *)tide_table_get(&a->rows, c, row, 2);
}

// A new row for `e`, which has none, born as `born` (0: not known), its value
// zeroed: its number.
uint32_t tide_attached_add(tide_attached *a, const tide_columns *c, tide_entity e, uint32_t born);

// Takes a row out, moving the last row into its place. Release what its value
// holds first.
void tide_attached_remove(tide_attached *a, const tide_columns *c, uint32_t row);

// Lets go of its rows, leaving it empty.
void tide_attached_free(tide_attached *a, const tide_columns *c);

// Its rows as bytes, as a table's (tide_table_pack), to carry local state
// over to another build. Reading them back into an empty one is false when
// the bytes aren't its rows, or two rows are of one entity.
uint32_t tide_attached_packed_size(const tide_attached *a, const tide_columns *c);
void tide_attached_pack(const tide_attached *a, const tide_columns *c, tide_writer *w);
bool tide_attached_unpack(tide_attached *a, const tide_columns *c, tide_reader *r);
