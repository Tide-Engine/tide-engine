#include <stdio.h>
#include <stdlib.h>

#include "game.h"
#include "tide_test.h"

static tide_world world;
static tide_world other;
static tide_world decoded;
static tide_local local;

static int32_t height(const tide_world *w, const int32_t x, const int32_t y)
{
    const int32_t *c = tide_grid_read(&w->heap, w->Terrain.heights, x, y, 0, &tide_shape_Grid2_int);
    return c ? *c : 0;
}

TIDE_TEST(grid_values_all_checks_pass)
{
    tide_world_init(&world, 1.0f);
    for (int i = 0; i < 3; i++) tide_world_tick(&world);
    const Tally *t = &world.Results.tally;
    if (t->firstFailure) printf("    check %d failed\n", (int)t->firstFailure);
    TIDE_CHECK(t->checks == 15);
    TIDE_CHECK(t->passed == t->checks);
    TIDE_CHECK(tide_scratch_mark() == 0); // A local's grid is the scratch area's, gone with the system
    // The field's own cells, in the world's heap, as a host reads them
    TIDE_CHECK(height(&world, 2, 2) == 202 && height(&world, 0, 0) == 9);
    tide_world_free(&world);
}

// A copy and a packed world have the field's cells, which are the world's own
TIDE_TEST(grid_values_fields_snapshot)
{
    tide_world_init(&world, 1.0f);
    for (int i = 0; i < 3; i++) tide_world_tick(&world);
    tide_world_copy(&other, &world);
    TIDE_CHECK(tide_world_hash(&other) == tide_world_hash(&world));
    TIDE_CHECK(height(&other, 1, 2) == 201);
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    void *bytes = malloc(size);
    TIDE_REQUIRE(bytes != NULL);
    TIDE_REQUIRE(tide_world_pack(&world, bytes, size) == size);
    TIDE_REQUIRE(tide_world_unpack(&decoded, bytes, size));
    free(bytes);
    TIDE_CHECK(tide_world_hash(&decoded) == tide_world_hash(&world));
    TIDE_CHECK(height(&decoded, 2, 0) == 9);
    tide_world_free(&decoded);
    tide_world_free(&other);
    tide_world_free(&world);
}

TIDE_TEST(grid_values_in_views)
{
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    tide_draw_list draw = {0};
    tide_gui gui = {0};
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    TIDE_CHECK(local.Seen.total == 615);
    tide_local_free(&local);
    tide_world_free(&world);
}
