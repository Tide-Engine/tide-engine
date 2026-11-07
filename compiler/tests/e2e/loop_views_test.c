#include <stdio.h>
#include <string.h>

#include "game.h"
#include "tide/pack.h"
#include "tide_test.h"

// The pack, as C (tide_add_game's --pack-c for HOST programs): the quad mesh asset
extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

static tide_world world;
static tide_world snapshot;

static float weight_at(const tide_world *w, const int32_t i)
{
    const Cell *c = tide_list_read(&w->heap, w->Data.big, i, sizeof(Cell));
    return c ? c->w : -1.0f;
}

TIDE_TEST(loop_views_all_checks_pass)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, 1.0f);
    for (int t = 0; t < 4; t++) tide_world_tick(&world);
    const Data *d = &world.Data;
    if (d->firstFailure) printf("    check %d failed\n", (int)d->firstFailure);
    TIDE_CHECK(d->checks == 12 * 3);
    TIDE_CHECK(d->passed == d->checks);
    TIDE_CHECK(tide_scratch_mark() == 0);
    tide_world_free(&world);
    tide_pack_unload();
}

// A loop's view writes a list in chunks shared with a snapshot: the chunks it
// writes are made the world's own first, so the snapshot keeps what it had,
// and rolling back to it gives the same world again.
TIDE_TEST(loop_views_leave_snapshots_as_they_were)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    tide_world_tick(&world);
    tide_world_copy(&snapshot, &world);
    const uint64_t before = tide_world_hash(&snapshot);
    const float w7 = weight_at(&world, 7);
    tide_world_tick(&world);
    TIDE_CHECK(weight_at(&world, 7) == w7 + 1);
    TIDE_CHECK(weight_at(&snapshot, 7) == w7);
    TIDE_CHECK(tide_world_hash(&snapshot) == before);
    const uint64_t after = tide_world_hash(&world);
    tide_world_copy(&world, &snapshot);
    tide_world_tick(&world);
    TIDE_CHECK(tide_world_hash(&world) == after);
    tide_world_free(&snapshot);
    tide_world_free(&world);
    tide_pack_unload();
}
