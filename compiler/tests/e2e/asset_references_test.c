#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "tide/pack.h"
#include "tide_test.h"
extern const uint8_t tide_game_pack[];
extern const size_t tide_game_pack_size;

static void equality(const tide_world *w)
{
    TIDE_CHECK(w->Probe.equal && w->Probe.different && w->Probe.defaults);
    TIDE_CHECK(w->Probe.sameGeometryDifferentNames);
    TIDE_CHECK(w->Probe.beforeEffect && w->Probe.afterEffect);
    TIDE_CHECK(w->Probe.orderedEqual && w->Probe.orderedDifferent);
    TIDE_CHECK(w->Probe.order == 1234 && w->Probe.calls == 4);
    TIDE_CHECK(w->Probe.folded == 9);
}

TIDE_TEST(asset_reference_prefab_snapshot_pack_and_reload)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world w = {0}, copy = {0}, decoded = {0};
    tide_world_init(&w, .1f);
    tide_world_tick(&w);
    equality(&w);
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
    equality(&copy);
    equality(&decoded);
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

TIDE_TEST(asset_reference_equality_preserves_unknown_received_identity_and_geometry_reload)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world world = {0}, decoded = {0};
    tide_world_init(&world, 0.1f);
    world.Probe.left.id = world.Probe.right.id = INT64_MAX;
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    void *bytes = malloc(size);
    TIDE_REQUIRE(bytes != NULL);
    TIDE_REQUIRE(tide_world_pack(&world, bytes, size) == size);
    TIDE_REQUIRE(tide_world_unpack(&decoded, bytes, size));
    free(bytes);
    tide_world_tick(&decoded);
    equality(&decoded);
    TIDE_CHECK(decoded.Probe.receivedEqual); // Unknown nonzero IDs compare as IDs, not empty resolved meshes.
    decoded.Probe.right.id = 0;
    tide_world_tick(&decoded);
    TIDE_CHECK(!decoded.Probe.receivedEqual);
    decoded.Probe.right.id = INT64_MAX - 1;
    tide_world_tick(&decoded);
    TIDE_CHECK(!decoded.Probe.receivedEqual);

    const tide_pack_entry *entry = tide_pack_find("box", TIDE_PACK_MESH);
    TIDE_REQUIRE(entry != NULL);
    const uint32_t offset = entry->offset;
    uint8_t *changed = malloc(tide_game_pack_size);
    TIDE_REQUIRE(changed != NULL);
    memcpy(changed, tide_game_pack, tide_game_pack_size);
    tide_pack_header header;
    memcpy(&header, changed, sizeof header);
    const size_t at = (size_t)header.region_at + offset + sizeof(tide_block);
    TIDE_REQUIRE(at + sizeof(float) <= tide_game_pack_size);
    float x;
    memcpy(&x, changed + at, sizeof x);
    x += 0.25f;
    memcpy(changed + at, &x, sizeof x);
    const float before = decoded.Probe.firstX;
    TIDE_REQUIRE(tide_assets_load(changed, tide_game_pack_size));
    free(changed);
    tide_world_tick(&decoded);
    equality(&decoded);
    TIDE_CHECK(decoded.Probe.firstX == x && decoded.Probe.firstX != before);
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_free(&decoded);
    tide_world_free(&world);
}
