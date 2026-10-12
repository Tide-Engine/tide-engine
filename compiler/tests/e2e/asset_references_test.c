#include <stdlib.h>
#include "game.h"
#include "tide_test.h"
extern const uint8_t tide_game_pack[];
extern const size_t tide_game_pack_size;

TIDE_TEST(asset_reference_prefab_snapshot_pack_and_reload)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world w = {0}, copy = {0}, decoded = {0};
    tide_world_init(&w, .1f);
    tide_world_tick(&w);
    TIDE_CHECK(w.Probe.count == 6);
    TIDE_CHECK(w.Probe.empty == 0);
    const uint64_t hash = tide_world_hash(&w);
    tide_world_copy(&copy, &w);
    const uint32_t size = tide_world_pack(&w, NULL, 0);
    void *bytes = malloc(size);
    TIDE_REQUIRE(bytes != NULL);
    TIDE_REQUIRE(tide_world_pack(&w, bytes, size) == size);
    TIDE_REQUIRE(tide_world_unpack(&decoded, bytes, size));
    free(bytes);
    TIDE_CHECK(tide_world_hash(&decoded) == hash);
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_tick(&copy);
    tide_world_tick(&decoded);
    TIDE_CHECK(copy.Probe.count == 6);
    TIDE_CHECK(tide_world_hash(&copy) == tide_world_hash(&decoded));
    decoded.Probe.held.id = INT64_MAX; // Arbitrary received/trusted-C identity is safe and reads empty.
    tide_world_tick(&decoded);
    TIDE_CHECK(decoded.Probe.heldCount == 0);
    tide_local local = {0}; tide_draw_list draw = {0}; tide_gui gui = {0};
    tide_local_init(&local);
    tide_frame(&w, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    tide_frame(&w, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    TIDE_CHECK(local.LocalProbe.count == 6);
    TIDE_CHECK(tide_world_hash(&w) == hash);
    tide_local_free(&local); tide_draw_free(&draw); tide_draw_free(&gui.list);
    tide_world_free(&w); tide_world_free(&copy); tide_world_free(&decoded);
}
