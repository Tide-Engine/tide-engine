#include <stdlib.h>
#include "game.h"
#include "tide/pack.h"
#include "tide_test.h"

extern const uint8_t tide_game_pack[];
extern const size_t tide_game_pack_size;

TIDE_TEST(alias_assets_keep_mesh_shapes_borrows_and_checked_references)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world world = {0}, copy = {0}, decoded = {0};
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    TIDE_CHECK(world.Probe.vertices == 4);
    TIDE_CHECK(world.Probe.listCount == 2);
    TIDE_CHECK(world.Probe.mapCount == 4);
    TIDE_CHECK(world.Probe.referenced == 12);
    TIDE_CHECK(world.Probe.qualifiedIndices == 9);
    TIDE_CHECK(world.Probe.listVertices == 6);
    TIDE_CHECK(world.Probe.mapIndices == 6);
    TIDE_CHECK(world.Probe.oneListCount == 1);
    TIDE_CHECK(world.Probe.oneMapCount == 1);
    TIDE_CHECK(world.Probe.forwarded == 6);
    TIDE_CHECK(world.Probe.empty == 0);

    const uint64_t hash = tide_world_hash(&world);
    tide_world_copy(&copy, &world);
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    void *bytes = malloc(size);
    TIDE_REQUIRE(bytes != NULL);
    TIDE_REQUIRE(tide_world_pack(&world, bytes, size) == size);
    TIDE_REQUIRE(tide_world_unpack(&decoded, bytes, size));
    free(bytes);
    TIDE_CHECK(tide_world_hash(&decoded) == hash);
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_tick(&copy);
    tide_world_tick(&decoded);
    TIDE_CHECK(copy.Probe.referenced == 12);
    TIDE_CHECK(tide_world_hash(&copy) == tide_world_hash(&decoded));
    TIDE_CHECK(tide_world_hash(&world) == hash);
    tide_world_free(&decoded);
    tide_world_free(&copy);
    tide_world_free(&world);
    tide_pack_unload();
}
