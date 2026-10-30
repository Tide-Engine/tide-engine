#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "game.h"
#include "tide/pack.h"
#include "tide/skinned_data.h"
#include "tide_test.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

static tide_world world;
static tide_world copy;

static void start(void)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, 0.25f);
    tide_world_tick(&world);
}

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

static float position(const tide_world *w, const bool original)
{
    const tide_list transforms = original ? w->Probe.original.transforms : w->Probe.pose.transforms;
    const tide_bone_transform *at = tide_list_read(&w->heap, transforms, w->Probe.hand.index, sizeof *at);
    return at ? at->position.x : 12345.0f;
}

TIDE_TEST(skinned_checked_catalogs_contracts_sampling_and_decode)
{
    start();
    const Probe *p = &world.Probe;
    if (p->firstFailure) printf("    check %d of %d failed\n", p->firstFailure, p->checks);
    TIDE_CHECK(p->checks == 44 && p->passed == p->checks);
    TIDE_CHECK(p->position == -6.0f && position(&world, false) == -6.0f);
    TIDE_CHECK(position(&world, true) == 0.0f);
    TIDE_CHECK(tide_list_read_count(&world.heap, p->model.vertices) == 4);
    TIDE_CHECK(tide_scratch_mark() == 0);
    tide_world_free(&world);
    tide_pack_unload();
}

TIDE_TEST(skinned_persistent_values_snapshots_and_serialization)
{
    start();
    tide_world_copy(&copy, &world);
    const uint64_t old_hash = tide_world_hash(&copy);
    tide_world_tick(&world);
    TIDE_CHECK(position(&world, false) == -12.0f && position(&copy, false) == -6.0f);
    TIDE_CHECK(tide_world_hash(&copy) == old_hash);
    tide_world_tick(&copy);
    TIDE_CHECK(same(&world, &copy));
    for (int i = 0; i < 11; i++) {
        tide_world_tick(&world);
        tide_world_tick(&copy);
    }
    TIDE_CHECK(same(&world, &copy));
    tide_world_free(&copy);
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    uint8_t *bytes = malloc(size);
    tide_world_pack(&world, bytes, size);
    memset(&copy, 0, sizeof copy);
    TIDE_REQUIRE(tide_world_unpack(&copy, bytes, size));
    free(bytes);
    TIDE_CHECK(same(&world, &copy));
    tide_world_tick(&world);
    tide_world_tick(&copy);
    TIDE_CHECK(same(&world, &copy) && position(&world, true) == 0.0f);
    printf("    hash: %llu\n", (unsigned long long)tide_world_hash(&world));
    TIDE_CHECK(tide_world_hash(&world) == SKINNED_HASH);
    tide_world_free(&copy);
    tide_world_free(&world);
    tide_pack_unload();
}

TIDE_TEST(skinned_persistent_pose_sampling_reuses_storage)
{
    start();
    for (int i = 0; i < 60; i++) tide_world_tick(&world);
    const uint64_t used = world.heap.used;
    for (int i = 0; i < 100; i++) tide_world_tick(&world);
    TIDE_CHECK(world.heap.used == used && world.Probe.passed == world.Probe.checks);
    tide_world_free(&world);
    tide_pack_unload();
}

static uint32_t little_word(const uint8_t *at)
{
    return (uint32_t)at[0] | (uint32_t)at[1] << 8 | (uint32_t)at[2] << 16 | (uint32_t)at[3] << 24;
}

TIDE_TEST(skinned_assets_reject_changed_valid_content_without_replacing_the_pack)
{
    start();
    const tide_pack_entry *before = tide_pack_find("hero", TIDE_PACK_SKINNED);
    TIDE_REQUIRE(before != NULL);
    const uint64_t hash = tide_world_hash(&world);
    uint8_t *clone = malloc(tide_game_pack_size);
    memcpy(clone, tide_game_pack, tide_game_pack_size);
    size_t model = 0;
    for (; model + TIDE_SKINNED_HEADER_SIZE <= tide_game_pack_size; model++) {
        if (memcmp(clone + model, "TSKN", 4) == 0) break;
    }
    TIDE_REQUIRE(model + TIDE_SKINNED_HEADER_SIZE <= tide_game_pack_size);
    const uint32_t size = little_word(clone + model + 8), vertex = little_word(clone + model + 36);
    TIDE_REQUIRE(size <= tide_game_pack_size - model && vertex <= size - sizeof(float));
    // A finite vertex change preserves structural validity and skeleton identity.
    clone[model + vertex] = 0;
    clone[model + vertex + 1] = 0;
    clone[model + vertex + 2] = 128;
    clone[model + vertex + 3] = 63;
    TIDE_REQUIRE(tide_skinned_data_validate(clone + model, size));
    TIDE_CHECK(!tide_assets_load(clone, tide_game_pack_size));
    TIDE_CHECK(tide_pack_find("hero", TIDE_PACK_SKINNED) == before && tide_world_hash(&world) == hash);
    tide_world_tick(&world);
    TIDE_CHECK(world.Probe.position == -12.0f && world.Probe.passed == world.Probe.checks);
    memcpy(clone, tide_game_pack, tide_game_pack_size);
    TIDE_CHECK(tide_assets_load(clone, tide_game_pack_size));
    free(clone);
    tide_world_tick(&world);
    TIDE_CHECK(world.Probe.position == -18.0f);
    tide_world_free(&world);
    tide_pack_unload();
}
