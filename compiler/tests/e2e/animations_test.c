#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/pack.h"
#include "tide/skinned_data.h"
#include "tide_test.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

static tide_world world, copy;

static void start(void)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, 0.25f);
    tide_world_tick(&world);
}

static float position(const tide_world *w)
{
    const tide_bone_transform *at = tide_list_read(&w->heap, w->Probe.pose.transforms, 1, sizeof *at);
    return at ? at->position.x : 12345.0f;
}

static float fbx_position(tide_world *w)
{
    tide_text_use(&w->heap, NULL);
    const int32_t bone = tide_skinned_find_bone(w->Probe.fbxPose.data, tide_str_from_cstr("Bone0"));
    const tide_bone_transform *at = tide_list_read(&w->heap, w->Probe.fbxPose.transforms, bone, sizeof *at);
    return at ? at->position.x : 12345.0f;
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

TIDE_TEST(animations_import_checked_catalogs_without_geometry)
{
    start();
    if (world.Probe.firstFailure) printf("    check %d failed\n", world.Probe.firstFailure);
    TIDE_CHECK(world.Probe.checks == 21 && world.Probe.passed == 21);
    TIDE_CHECK(position(&world) == -6.0f && fbx_position(&world) == -2.0f);
    static const char *const names[] = {"movement", "fbxAscii", "fbxBinary"};
    for (uint32_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        const tide_pack_entry *entry = tide_pack_find(names[i], TIDE_PACK_LIST);
        TIDE_REQUIRE(entry != NULL);
        const tide_list *source = tide_list_at((tide_list){tide_pack_handle(entry)}, 0, sizeof(tide_list));
        TIDE_REQUIRE(source != NULL);
        const tide_list data = *source;
        const uint8_t *bytes = tide_list_at(data, 0, 1u);
        tide_skinned_data_view view;
        TIDE_REQUIRE(tide_skinned_data_open(bytes, (size_t)tide_list_count(data), &view));
        TIDE_CHECK(view.vertices == 0 && view.indices == 0 && view.clips == 1);
    }
    tide_world_free(&world);
    tide_pack_unload();
}

TIDE_TEST(animations_owned_catalogs_survive_pack_unload_snapshots_and_serialization)
{
    start();
    tide_world_copy(&copy, &world);
    const uint64_t old_hash = tide_world_hash(&copy);
    tide_pack_unload();
    tide_world_tick(&world);
    TIDE_CHECK(position(&world) == -12.0f && position(&copy) == -6.0f);
    TIDE_CHECK(fbx_position(&world) == -3.0f && fbx_position(&copy) == -2.0f);
    TIDE_CHECK(tide_world_hash(&copy) == old_hash);
    tide_world_tick(&copy);
    TIDE_CHECK(same(&world, &copy));
    tide_world_free(&copy);
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    uint8_t *bytes = malloc(size);
    tide_world_pack(&world, bytes, size);
    TIDE_REQUIRE(tide_world_unpack(&copy, bytes, size));
    free(bytes);
    TIDE_CHECK(same(&world, &copy));
    for (int i = 0; i < 60; i++) tide_world_tick(&world);
    const uint64_t used = world.heap.used;
    for (int i = 0; i < 100; i++) tide_world_tick(&world);
    TIDE_CHECK(world.heap.used == used && world.Probe.passed == world.Probe.checks);
    tide_world_free(&copy);
    tide_world_free(&world);
}

TIDE_TEST(animations_changed_canonical_data_is_rejected_before_pack_replacement)
{
    start();
    const tide_pack_entry *before = tide_pack_find("movement", TIDE_PACK_LIST);
    TIDE_REQUIRE(before != NULL);
    uint8_t *clone = malloc(tide_game_pack_size);
    memcpy(clone, tide_game_pack, tide_game_pack_size);
    size_t model = 0;
    for (; model + TIDE_SKINNED_HEADER_SIZE <= tide_game_pack_size; model++) {
        if (memcmp(clone + model, "TSKN", 4) == 0) break;
    }
    TIDE_REQUIRE(model + TIDE_SKINNED_HEADER_SIZE <= tide_game_pack_size);
    tide_skinned_data_view view;
    uint32_t size;
    memcpy(&size, clone + model + 8, sizeof size);
    TIDE_REQUIRE(size <= tide_game_pack_size - model && tide_skinned_data_open(clone + model, size, &view));
    // A finite duration change remains a valid portable animation blob.
    const float duration = 2.0f;
    memcpy(clone + model + view.clip_at + 16u, &duration, sizeof duration);
    TIDE_REQUIRE(tide_skinned_data_validate(clone + model, size));
    TIDE_CHECK(!tide_assets_load(clone, tide_game_pack_size));
    TIDE_CHECK(tide_pack_find("movement", TIDE_PACK_LIST) == before);
    tide_world_tick(&world);
    TIDE_CHECK(position(&world) == -12.0f && world.Probe.passed == world.Probe.checks);
    TIDE_CHECK(tide_assets_load(tide_game_pack, tide_game_pack_size));
    free(clone);
    tide_world_free(&world);
    tide_pack_unload();
}
