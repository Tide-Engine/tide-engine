#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/pack.h"
#include "tide_test.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

static tide_world world, copy;

static bool same(const tide_world *a, const tide_world *b)
{
    const uint32_t size = tide_world_pack(a, NULL, 0);
    if (tide_world_pack(b, NULL, 0) != size) return false;
    uint8_t *x = malloc(size), *y = malloc(size);
    tide_world_pack(a, x, size);
    tide_world_pack(b, y, size);
    const bool equal = memcmp(x, y, size) == 0 && tide_world_hash(a) == tide_world_hash(b);
    free(x);
    free(y);
    return equal;
}

static bool start(void)
{
    if (!tide_assets_load(tide_game_pack, tide_game_pack_size)) return false;
    tide_world_init(&world, 0.25f);
    tide_world_tick(&world);
    if (world.FbxProbe.firstFailure) printf("    FBX check %d of %d failed\n", world.FbxProbe.firstFailure, world.FbxProbe.checks);
    return true;
}

TIDE_TEST(fbx_assets_static_skinned_collections_contracts_and_match_sampling)
{
    TIDE_REQUIRE(start());
    const FbxProbe *p = &world.FbxProbe;
    TIDE_CHECK(p->checks == 28 && p->passed == p->checks);
    TIDE_CHECK(p->position == -2 && p->modelPosition == -4);
    TIDE_CHECK(tide_list_read_count(&world.heap, p->geometry) == 3);
    TIDE_CHECK(tide_list_read_count(&world.heap, p->indices) == 3);
    const tide_bone_transform *rest = tide_list_read(&world.heap, p->rest.transforms, p->hand.index, sizeof *rest);
    TIDE_REQUIRE(rest);
    TIDE_CHECK(rest->position.x == -1);
    tide_local local = {0};
    tide_local_init(&local);
    tide_draw_list draw = {0};
    tide_gui gui = {0};
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1, NULL, NULL, 1.0f, &local, &draw, &gui);
    // The built-in static mesh records a plain mesh command; skinning uses
    // one shaded run with its captured palette and influence textures.
    TIDE_CHECK(draw.run_count == 1 && draw.mesh_count == 2);
    TIDE_CHECK(draw.texture_count >= 2); // Captured skin palettes and influences.
    tide_draw_free(&draw);
    tide_local_free(&local);
    TIDE_CHECK(tide_scratch_mark() == 0);
    tide_world_free(&world);
    tide_pack_unload();
}

TIDE_TEST(fbx_poses_survive_snapshots_serialization_and_repeated_sampling)
{
    TIDE_REQUIRE(start());
    tide_world_copy(&copy, &world);
    const uint64_t old_hash = tide_world_hash(&copy);
    tide_world_tick(&world);
    TIDE_CHECK(world.FbxProbe.position == -3 && copy.FbxProbe.position == -2);
    TIDE_CHECK(tide_world_hash(&copy) == old_hash);
    tide_world_tick(&copy);
    TIDE_CHECK(same(&world, &copy));
    tide_world_free(&copy);
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    uint8_t *bytes = malloc(size);
    tide_world_pack(&world, bytes, size);
    memset(&copy, 0, sizeof copy);
    TIDE_REQUIRE(tide_world_unpack(&copy, bytes, size));
    free(bytes);
    TIDE_CHECK(same(&world, &copy));
    for (int i = 0; i < 20; i++) {
        tide_world_tick(&world);
        tide_world_tick(&copy);
    }
    TIDE_CHECK(same(&world, &copy));
    printf("    FBX match hash: %llu\n", (unsigned long long)tide_world_hash(&world));
    TIDE_CHECK(tide_world_hash(&world) == FBX_HASH);
    const uint64_t used = world.heap.used;
    for (int i = 0; i < 100; i++) tide_world_tick(&world);
    TIDE_CHECK(world.heap.used == used);
    tide_world_free(&copy);
    tide_world_free(&world);
    tide_pack_unload();
}
