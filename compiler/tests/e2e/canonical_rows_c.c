#include <stddef.h>
#include "game.h"
#include "tide/rows.h"
static tide_entity previous;
static int unordered;
void canonical_c_reset(void) { previous = (tide_entity){0}; unordered = 0; }
int canonical_c_unordered(void) { return unordered; }
void canonical_c_step(tide_rows *rows)
{
    const tide_entity *keys = tide_rows_entities(rows);
    int32_t *writes = tide_rows_ints_mut(rows, 0, offsetof(Cell, cWrites));
    for (uint32_t i = 0; i < tide_rows_count(rows); i++) {
        if (!tide_entity_is_null(previous) && tide_entity_compare(previous, keys[i]) >= 0) unordered++;
        previous = keys[i];
        writes[i]++;
    }
}
