#include <string.h>
#include "game.h"
#include "tide_test.h"
void canonical_c_reset(void);
int canonical_c_unordered(void);

static void check_rows(const tide_world *w, int writes)
{
    int count = 0, sum = 0;
    for (uint32_t i = 0; i < w->entities.next_unused; i++) {
        const tide_entity key = tide_entity_in_slot(&w->entities, i);
        if (!tide_has_Cell(w, key)) continue;
        const Cell cell = tide_get_Cell(w, key);
        TIDE_CHECK(cell.tideWrites == writes && cell.cWrites == writes);
        TIDE_CHECK(cell.untouched.x == 1 && cell.untouched.y == 2 && cell.untouched.z == 3);
        TIDE_CHECK(tide_entity_is_null(cell.reference) && cell.marked);
        Cell expected;
        memset(&expected, 0, sizeof expected);
        expected.number = cell.number;
        expected.tideWrites = expected.cWrites = writes;
        expected.untouched = (tide_float3){1, 2, 3};
        expected.marked = true;
        TIDE_CHECK(memcmp(&cell, &expected, sizeof cell) == 0);
        count++; sum += cell.number;
    }
    TIDE_CHECK(count == 3000 && sum == 4498500);
}
TIDE_TEST(canonical_partial_runs_preserve_other_rows_and_shared_snapshots)
{
    tide_world world = {0}, snapshot = {0};
    tide_world_init(&world, 1.0f / 60.0f);
    int inversions = 0;
    tide_entity previous = {0};
    for (uint32_t i = 0; i < world.entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&world.entities, i);
        if (!tide_has_Cell(&world, e)) continue;
        if (tide_entity_compare(previous, e) > 0) inversions++;
        previous = e;
    }
    TIDE_CHECK(inversions > 32);
    for (int tick = 1; tick <= 4; tick++) {
        tide_world_copy(&snapshot, &world);
        const uint64_t snapshot_hash = tide_world_hash(&snapshot);
        canonical_c_reset();
        tide_world_tick(&world);
        TIDE_CHECK(canonical_c_unordered() == 0);
        check_rows(&world, tick);
        check_rows(&snapshot, tick - 1);
        TIDE_CHECK(tide_world_hash(&snapshot) == snapshot_hash);
        TIDE_CHECK(snapshot.Order.writes == 3000 * (tick - 1));
        TIDE_CHECK(world.Order.writes == 3000 * tick);
    }
    tide_world_free(&snapshot);
    tide_world_free(&world);
}
