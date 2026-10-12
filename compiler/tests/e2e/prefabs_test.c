#include <stdlib.h>
#include "game.h"
#include "tide_test.h"

TIDE_TEST(prefab_complete_tree_and_root_overrides)
{
    tide_world w = {0};
    tide_world_init(&w, 0.1f);
    TIDE_CHECK(w.Probe.ready);
    TIDE_CHECK(tide_get_Data(&w, w.Probe.root).value == 11);
    TIDE_CHECK(tide_get_Data(&w, w.Probe.loaded).value == 12);
    TIDE_CHECK(w.Probe.order == 112530122530LL);
    TIDE_CHECK(w.Probe.overrideCalls == 2);
    TIDE_CHECK(tide_get_LevelSettings(&w, w.Probe.root).difficulty == 2);
    TIDE_CHECK(tide_get_LevelSettings(&w, w.Probe.loaded).difficulty == 3);
    tide_world_free(&w);
}

TIDE_TEST(prefab_destroy_subtree_and_unload_scene)
{
    tide_world w = {0};
    tide_world_init(&w, 0.1f);
    const Links ordinary = tide_get_Links(&w, w.Probe.root);
    const Links loaded = tide_get_Links(&w, w.Probe.loaded);
    w.Probe.destroy = true;
    tide_world_tick(&w);
    TIDE_CHECK(!tide_entity_alive(&w.entities, w.Probe.root));
    TIDE_CHECK(!tide_entity_alive(&w.entities, ordinary.child));
    TIDE_CHECK(!tide_entity_alive(&w.entities, ordinary.tip));
    TIDE_CHECK(tide_entity_alive(&w.entities, w.Probe.loaded));
    TIDE_CHECK(tide_entity_alive(&w.entities, loaded.tip));
    w.Probe.unload = true;
    tide_world_tick(&w);
    TIDE_CHECK(!tide_entity_alive(&w.entities, w.Probe.loaded));
    TIDE_CHECK(!tide_entity_alive(&w.entities, loaded.child));
    TIDE_CHECK(!tide_entity_alive(&w.entities, loaded.tip));
    tide_world_free(&w);
}

TIDE_TEST(prefab_references_snapshot_and_pack)
{
    tide_world w = {0}, snapshot = {0}, decoded = {0};
    tide_world_init(&w, 0.1f);
    const uint64_t hash = tide_world_hash(&w);
    tide_world_copy(&snapshot, &w);
    const uint32_t bytes = tide_world_pack(&w, NULL, 0);
    void *packed = malloc(bytes);
    TIDE_REQUIRE(packed != NULL);
    TIDE_CHECK(tide_world_pack(&w, packed, bytes) == bytes);
    TIDE_REQUIRE(tide_world_unpack(&decoded, packed, bytes));
    free(packed);
    w.Probe.destroy = true;
    tide_world_tick(&w);
    TIDE_CHECK(tide_world_hash(&snapshot) == hash);
    TIDE_CHECK(tide_world_hash(&decoded) == hash);
    const Links links = tide_get_Links(&decoded, decoded.Probe.root);
    TIDE_CHECK(tide_get_Data(&decoded, links.child).value == 25);
    TIDE_CHECK(tide_entity_alive(&decoded.entities, links.tip));
    tide_world_tick(&snapshot);
    tide_world_tick(&decoded);
    TIDE_CHECK(tide_world_hash(&snapshot) == tide_world_hash(&decoded));
    tide_world_free(&w);
    tide_world_free(&snapshot);
    tide_world_free(&decoded);
}

TIDE_TEST(prefab_local_placement_keeps_match_unchanged)
{
    tide_world w = {0};
    tide_local local = {0};
    tide_draw_list draw = {0};
    tide_gui gui = {0};
    tide_world_init(&w, 0.1f);
    tide_local_init(&local);
    const uint64_t hash = tide_world_hash(&w);
    tide_frame(&w, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    TIDE_CHECK(local.LocalProbe.ready);
    TIDE_CHECK(tide_entity_alive(&local.entities, local.LocalProbe.root));
    TIDE_CHECK(tide_entity_alive(&local.entities, local.LocalProbe.loaded));
    local.LocalProbe.unload = true;
    tide_frame(&w, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    TIDE_CHECK(!tide_entity_alive(&local.entities, local.LocalProbe.loaded));
    TIDE_CHECK(tide_entity_alive(&local.entities, local.LocalProbe.root));
    TIDE_CHECK(tide_world_hash(&w) == hash);
    tide_world_free(&w);
    tide_local_free(&local);
    tide_draw_free(&draw);
}

static void prefab_run_jobs(void *self, void (*work)(void *, uint32_t), void *context)
{
    *(bool *)self = true;
    work(context, 0);
}

TIDE_TEST(prefab_inherited_values_keep_source_namespace)
{
    tide_world w = {0};
    tide_world_init(&w, 0.1f);
    w.Probe.spawnScoped = true;
    tide_world_tick(&w);
    const ScopedLinks links = tide_get_ScopedLinks(&w, w.Probe.scoped);
    TIDE_CHECK(tide_get_Data(&w, links.child).value == 42);
    TIDE_CHECK(tide_get_Data(&w, links.tip).value == 41);
    tide_world_free(&w);
}

TIDE_TEST(prefab_temporary_handles_and_serial_order)
{
    tide_world serial = {0}, split = {0};
    tide_world_init(&serial, 0.1f);
    tide_world_init(&split, 0.1f);
    serial.Probe.batchCount = split.Probe.batchCount = 4096;
    bool pooled = false;
    const tide_jobs jobs = {.self = &pooled, .threads = 8, .run = prefab_run_jobs};
    for (int tick = 0; tick < 3; tick++) {
        tide_world_tick(&serial);
        tide_world_tick_on(&split, &jobs);
        TIDE_REQUIRE(tide_world_hash(&serial) == tide_world_hash(&split));
    }
    TIDE_CHECK(pooled);
    TIDE_CHECK(split.Probe.ready);
    TIDE_CHECK(!split.Probe.invalidWorker);
    tide_world_free(&serial);
    tide_world_free(&split);
}
