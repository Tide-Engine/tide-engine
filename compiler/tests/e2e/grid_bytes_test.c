#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/time.h"
#include "tide_test.h"

static tide_world world;

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

// The pack (an asset's grid is the pack's), then the world, two ticks in: the checks run in the first
static void start(void)
{
    TIDE_CHECK(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, 1.0f / 60.0f);
    for (int i = 0; i < 2; i++) tide_world_tick(&world);
}

// FNV-1a over a list's bytes, as the world keeps them
static uint64_t hash_bytes(uint64_t h, const tide_world *w, const tide_list l)
{
    const int32_t count = tide_list_read_count(&w->heap, l);
    for (int32_t i = 0; i < count; i++) {
        const uint8_t *b = tide_list_read(&w->heap, l, i, 1);
        h = (h ^ (b ? *b : 0u)) * 0x100000001B3u;
    }
    return (h ^ (uint64_t)count) * 0x100000001B3u;
}

static bool bytes_are(const tide_world *w, const tide_list l, const uint8_t *expected, const int32_t count)
{
    if (tide_list_read_count(&w->heap, l) != count) return false;
    for (int32_t i = 0; i < count; i++) {
        const uint8_t *b = tide_list_read(&w->heap, l, i, 1);
        if (!b || *b != expected[i]) return false;
    }
    return true;
}

TIDE_TEST(grid_bytes_all_checks_pass)
{
    start();
    const Tally *t = &world.Results.tally;
    if (t->firstFailure || t->checks != 46) printf("    %d checks, check %d failed\n", (int)t->checks, (int)t->firstFailure);
    TIDE_CHECK(t->checks == 46);
    TIDE_CHECK(t->passed == t->checks);
    TIDE_CHECK(tide_scratch_mark() == 0); // What the calls made was the scratch area's, gone with the system
    tide_world_free(&world);
}

// What ToBytes writes is pinned: a few encodings whole, and all of them by
// their hash, which every platform and configuration shares. A changed hash is
// a changed format: say so in the spec, and pin the new one.
TIDE_TEST(grid_bytes_are_the_same_everywhere)
{
    start();
    const Kept *k = &world.Kept;
    static const uint8_t empty[] = {0x47, 0x13, 1, 16, 16, 16, 0};
    static const uint8_t stone[] = {0x47, 0x13, 1, 16, 16, 16, 1, 0, 0, 0, 0xFF, 0x3F, 1};
    TIDE_CHECK(bytes_are(&world, k->empty, empty, (int32_t)sizeof empty));
    TIDE_CHECK(bytes_are(&world, k->stone, stone, (int32_t)sizeof stone));
    uint64_t h = 0xCBF29CE484222325u;
    const tide_list all[] = {k->empty, k->stone, k->sparse, k->dense, k->open, k->tall, k->tiles, k->things};
    for (size_t i = 0; i < sizeof all / sizeof all[0]; i++) {
        h = hash_bytes(h, &world, all[i]);
        printf("    %zu: %d bytes\n", i, (int)tide_list_read_count(&world.heap, all[i]));
    }
    printf("    bytes hash 0x%016" PRIX64 ", %d spoiled bytes read\n", h, (int)world.Results.spoiled);
    TIDE_CHECK(h == GRID_BYTES_HASH);
    TIDE_CHECK(world.Results.spoiled == GRID_BYTES_SPOILED);
    // The grids read back into fields are the world's own: a copy and the world as bytes have them
    const uint8_t *cell = tide_grid_read(&world.heap, k->blocks, 1, 2, 3, &tide_shape_Grid3_Block);
    TIDE_CHECK(cell && *cell == (uint8_t)((1 * 7 + 2 * 13 + 3 * 31 + 1 * 2 * 3 + 2 * 3) % 5));
    const int32_t *far = tide_grid_read(&world.heap, k->far, 2147483647, INT32_MIN, 0, &tide_shape_Grid2_int);
    TIDE_CHECK(far && *far == 3);
    tide_world copy = {0};
    tide_world_copy(&copy, &world);
    TIDE_CHECK(tide_world_hash(&copy) == tide_world_hash(&world));
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    void *packed = malloc(size);
    TIDE_REQUIRE(packed != NULL);
    TIDE_REQUIRE(tide_world_pack(&world, packed, size) == size);
    tide_world decoded = {0};
    TIDE_REQUIRE(tide_world_unpack(&decoded, packed, size));
    free(packed);
    TIDE_CHECK(tide_world_hash(&decoded) == tide_world_hash(&world));
    tide_world_free(&decoded);
    tide_world_free(&copy);
    tide_world_free(&world);
}

TIDE_TEST(grid_bytes_in_views)
{
    start();
    tide_local local = {0}; // tide_local_init lets go of what it had first
    tide_local_init(&local);
    tide_draw_list draw = {0};
    tide_gui gui = {0};
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    TIDE_CHECK(local.Seen.stone == 1 && local.Seen.size == 16 && local.Seen.same);
    for (int i = 0; i < 100 && !local.Seen.worked; i++) tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    TIDE_CHECK(local.Seen.ground > 7 && local.Seen.worked == local.Seen.ground); // A worker's bytes are the match's
    tide_local_free(&local);
    tide_draw_free(&draw);
    tide_world_free(&world);
}

// The built-in against the loops bench/voxels wrote, a 16-cube at a time: one
// of blocks mixed cell by cell, and one of ground, as the game's are (no check
// of the times: they're the machine's)
TIDE_TEST(grid_bytes_measure)
{
    start();
    static const char *const names[] = {"", "Pack (Tide loops)", "Unpack (Tide loops)", "ToBytes", "FromBytes"};
    static const char *const chunks[] = {"mixed", "ground"};
    const int rounds = 400;
    for (int which = 0; which < 2; which++) {
        for (int mode = 1; mode <= 4; mode++) {
            world.Bench.mode = 0; // A tick of nothing, to take off
            uint64_t start = tide_time_now_ns();
            tide_world_tick(&world);
            const uint64_t idle = tide_time_now_ns() - start;
            world.Bench.mode = mode;
            world.Bench.rounds = rounds;
            world.Bench.which = which;
            start = tide_time_now_ns();
            tide_world_tick(&world);
            const uint64_t took = tide_time_now_ns() - start;
            TIDE_CHECK(world.Bench.mode == 0 && world.Bench.sum > 0);
            printf("    %-6s %-20s %8.2f us a chunk", chunks[which], names[mode], (double)(took > idle ? took - idle : 0) / 1000.0 / rounds);
            if (mode == 1 || mode == 3) printf(", %d bytes", (int)(world.Bench.sum / rounds)); // What it wrote, about
            printf("\n");
        }
    }
    tide_world_free(&world);
}
