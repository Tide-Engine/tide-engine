#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "tide/pack.h"
#include "tide_test.h"
extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;
_Static_assert(sizeof(((Saved *)0)->single) == 8, "a mesh value is one ID");
_Static_assert(sizeof(((Saved *)0)->pair) == 16, "arrays contain only IDs");
TIDE_TEST(mesh_collections_keep_ids_across_snapshots_packing_and_reload)
{
    tide_world world = {0}, snapshot = {0}, restored = {0};
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, .1f);
    tide_world_tick(&world);
    TIDE_CHECK(world.Saved.count == 45 && world.Saved.copied == 0);
    tide_world_copy(&snapshot, &world);
    const uint64_t hash = tide_world_hash(&snapshot);
    const int first = world.Saved.firstCount;
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(bytes != NULL);
    TIDE_REQUIRE(tide_world_pack(&world, bytes, size) == size);
    // A replacement pack can enumerate a glob differently. Retained element IDs
    // must keep selecting the same file, rather than its old physical slot.
    const tide_pack_entry *geometry = tide_pack_find("models", TIDE_PACK_MESH_LIST);
    const tide_pack_entry *ids = tide_pack_find("models#mesh-values", TIDE_PACK_LIST);
    TIDE_REQUIRE(geometry && ids && geometry->size == 2 && ids->size == 2);
    uint8_t *replacement = malloc(tide_game_pack_size);
    TIDE_REQUIRE(replacement != NULL);
    memcpy(replacement, tide_game_pack, tide_game_pack_size);
    tide_pack_header header;
    memcpy(&header, replacement, sizeof header);
    const uint64_t offsets[] = {geometry->offset, ids->offset};
    // Their two elements swapped: a mesh's two lists, and an ID
    const size_t element[] = {sizeof(tide_mesh_lists), sizeof(int64_t)};
    for (unsigned i = 0; i < 2; i++) {
        const size_t at = (size_t)header.region_at + offsets[i] + sizeof(tide_block);
        TIDE_REQUIRE(at + 2u * element[i] <= tide_game_pack_size);
        uint8_t held[sizeof(tide_mesh_lists)];
        memcpy(held, replacement + at, element[i]);
        memcpy(replacement + at, replacement + at + element[i], element[i]);
        memcpy(replacement + at + element[i], held, element[i]);
    }
    TIDE_REQUIRE(tide_assets_load(replacement, tide_game_pack_size));
    free(replacement);
    TIDE_REQUIRE(tide_world_unpack(&restored, bytes, size));
    free(bytes);
    tide_world_tick(&world);
    tide_world_tick(&restored);
    TIDE_CHECK(world.Saved.count == 45 && restored.Saved.count == 45);
    TIDE_CHECK(tide_world_hash(&world) == tide_world_hash(&restored));
    TIDE_CHECK(world.Saved.asyncCount == 9);
    TIDE_CHECK(world.Saved.firstCount == first && restored.Saved.firstCount == first);
    TIDE_CHECK(tide_world_hash(&snapshot) == hash);
    world.Saved.single.id = INT64_MAX;
    tide_world_tick(&world);
    TIDE_CHECK(world.Saved.count == 36); // Unknown received IDs resolve to empty geometry.
    tide_world_free(&snapshot);
    tide_world_free(&restored);
    tide_world_free(&world);
    tide_pack_unload();
}
