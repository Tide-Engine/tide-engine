#include <stdlib.h>
#include <string.h>
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
    const Links loaded = tide_get_Links(&w, w.Probe.loaded);
    const tide_player_id player = tide_player_from_index(0);
    TIDE_CHECK(tide_world_visible(&w, w.Probe.root, player));
    TIDE_CHECK(!tide_world_visible(&w, w.Probe.loaded, player));
    TIDE_CHECK(!tide_world_visible(&w, loaded.child, player));
    TIDE_CHECK(!tide_world_visible(&w, loaded.tip, player));
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

static bool prefab_label_is(const tide_heap *heap, const tide_text label, const char *expected)
{
    const tide_str text = tide_text_read(heap, label);
    return text.bytes == (int32_t)strlen(expected) && memcmp(text.ptr, expected, (size_t)text.bytes) == 0;
}

static void prefab_check_descendant_tree(const tide_world *w, const tide_entity root)
{
    const DescendantRoots roots = tide_get_DescendantRoots(w, root);
    const DescendantRefs first = tide_get_DescendantRefs(w, roots.first);
    const DescendantRefs second = tide_get_DescendantRefs(w, roots.second);
    const DescendantRefs source = tide_get_DescendantRefs(w, roots.source);
    const DescendantRefs first_child = tide_get_DescendantRefs(w, first.kept);
    const DescendantRefs second_child = tide_get_DescendantRefs(w, second.kept);
    const DescendantRefs source_child = tide_get_DescendantRefs(w, source.kept);
    TIDE_CHECK(!tide_entity_equal(first.kept, second.kept) && !tide_entity_equal(first.kept, source.kept));
    TIDE_CHECK(tide_entity_equal(first.self, roots.first) && tide_entity_equal(first_child.self, first.kept));
    TIDE_CHECK(tide_entity_equal(second_child.self, second.kept) && tide_entity_equal(source_child.self, source.kept));
    // Unwritten references retain their source instance; written references name
    // this composition's Anchor, including overwrites of SceneRequired fields.
    TIDE_CHECK(tide_entity_equal(first_child.kept, roots.firstTip) && tide_entity_equal(first_child.changed, roots.anchor));
    TIDE_CHECK(tide_entity_equal(second_child.kept, roots.anchor) && tide_entity_equal(second_child.changed, second.changed));
    TIDE_CHECK(tide_entity_equal(source_child.kept, roots.sourceTip) && tide_entity_equal(source_child.changed, source.changed));
    TIDE_CHECK(tide_get_Data(w, roots.first).value == 41);
    TIDE_CHECK(prefab_label_is(&w->heap, tide_get_Data(w, roots.first).label, "first-root"));
    TIDE_CHECK(tide_get_Data(w, first.kept).value == 42 && tide_get_Data(w, second.kept).value == 84);
    TIDE_CHECK(tide_get_Data(w, source.kept).value == 41);
    TIDE_CHECK(prefab_label_is(&w->heap, tide_get_Data(w, first.kept).label, "inherited-child"));
    TIDE_CHECK(prefab_label_is(&w->heap, tide_get_Data(w, second.kept).label, "inherited-child"));
    TIDE_CHECK(tide_get_Data(w, roots.firstTip).value == 90 && tide_get_Data(w, roots.secondTip).value == 42);
    TIDE_CHECK(tide_get_Data(w, roots.sourceTip).value == 42);
    TIDE_CHECK(prefab_label_is(&w->heap, tide_get_Data(w, roots.firstTip).label, "default"));
    TIDE_CHECK(prefab_label_is(&w->heap, tide_get_Data(w, roots.secondTip).label, "second-tip"));
    TIDE_CHECK(prefab_label_is(&w->heap, tide_get_Data(w, roots.sourceTip).label, "default"));
    TIDE_CHECK(prefab_label_is(&w->heap, tide_get_Data(w, roots.source).label, "source-root"));
    TIDE_CHECK(tide_get_Data(w, first.changed).value == 7 && tide_get_Data(w, second.changed).value == 7);
    TIDE_CHECK(prefab_label_is(&w->heap, tide_get_Data(w, source.changed).label, "sibling"));
    TIDE_CHECK(tide_get_LevelSettings(w, first.kept).difficulty == 9);
    TIDE_CHECK(tide_get_LevelSettings(w, second.kept).difficulty == 1 && tide_get_LevelSettings(w, source.kept).difficulty == 1);
    TIDE_CHECK(tide_get_LevelSettings(w, roots.first).difficulty == 9);
    TIDE_CHECK(tide_get_Data(w, roots.added).value == 31 && tide_get_Data(w, roots.anchor).value == 66);
}

TIDE_TEST(prefab_descendant_overrides_merge_values_and_preserve_instances)
{
    tide_world w = {0}, snapshot = {0}, decoded = {0};
    tide_world_init(&w, 0.1f);
    TIDE_CHECK(w.Probe.order == 112530122530LL);
    const uint32_t before = w.entities.next_unused;
    w.Probe.spawnDescendants = true;
    tide_world_tick(&w);
    // Each tree has one root, three independent four-entity base instances,
    // one added child and one Anchor. Overrides never create extra children.
    TIDE_CHECK(w.entities.next_unused - before == 30);
    TIDE_CHECK(w.Probe.descendantReady == 2 && !w.Probe.invalidDescendants);
    prefab_check_descendant_tree(&w, w.Probe.descendants);
    prefab_check_descendant_tree(&w, w.Probe.descendantsAgain);
    const DescendantRoots roots = tide_get_DescendantRoots(&w, w.Probe.descendants);
    const DescendantRoots other = tide_get_DescendantRoots(&w, w.Probe.descendantsAgain);
    TIDE_CHECK(!tide_entity_equal(roots.first, other.first) && !tide_entity_equal(roots.anchor, other.anchor));
    const uint64_t hash = tide_world_hash(&w);
    tide_world_copy(&snapshot, &w);
    const uint32_t bytes = tide_world_pack(&w, NULL, 0);
    void *packed = malloc(bytes);
    TIDE_REQUIRE(packed != NULL);
    TIDE_CHECK(tide_world_pack(&w, packed, bytes) == bytes);
    TIDE_REQUIRE(tide_world_unpack(&decoded, packed, bytes));
    free(packed);
    const DescendantRefs first = tide_get_DescendantRefs(&w, roots.first);
    Data changed = tide_get_Data(&w, first.kept);
    changed.value = 123;
    tide_set_Data(&w, first.kept, changed);
    TIDE_CHECK(tide_get_Data(&w, tide_get_DescendantRefs(&w, roots.second).kept).value == 84);
    TIDE_CHECK(tide_get_Data(&w, tide_get_DescendantRefs(&w, roots.source).kept).value == 41);
    prefab_check_descendant_tree(&w, w.Probe.descendantsAgain);
    TIDE_CHECK(tide_world_hash(&snapshot) == hash && tide_world_hash(&decoded) == hash);
    prefab_check_descendant_tree(&snapshot, snapshot.Probe.descendants);
    prefab_check_descendant_tree(&decoded, decoded.Probe.descendantsAgain);
    tide_world_tick(&snapshot);
    tide_world_tick(&decoded);
    TIDE_CHECK(tide_world_hash(&snapshot) == tide_world_hash(&decoded));
    tide_world_free(&decoded);
    tide_world_free(&snapshot);
    tide_world_free(&w);
}

static void prefab_check_local_descendant_tree(const tide_local *local, const tide_entity root)
{
    const tide_local_component_LocalDescendantRoots roots = tide_get_tide_local_component_LocalDescendantRoots(local, root);
    const tide_local_component_LocalDescendantRefs item = tide_get_tide_local_component_LocalDescendantRefs(local, roots.item);
    const tide_local_component_LocalDescendantRefs source = tide_get_tide_local_component_LocalDescendantRefs(local, roots.source);
    const tide_local_component_LocalDescendantRefs child = tide_get_tide_local_component_LocalDescendantRefs(local, item.kept);
    const tide_local_component_LocalDescendantRefs source_child = tide_get_tide_local_component_LocalDescendantRefs(local, source.kept);
    TIDE_CHECK(tide_entity_equal(item.self, roots.item) && tide_entity_equal(child.self, item.kept));
    TIDE_CHECK(tide_entity_equal(child.kept, roots.tip) && tide_entity_equal(child.changed, roots.anchor));
    TIDE_CHECK(tide_entity_equal(source_child.changed, source.changed));
    TIDE_CHECK(!tide_entity_equal(item.kept, source.kept) && !tide_entity_equal(item.changed, source.changed));
    TIDE_CHECK(tide_get_tide_local_component_Data(local, roots.item).value == 2);
    TIDE_CHECK(prefab_label_is(&local->heap, tide_get_tide_local_component_Data(local, roots.item).label, "local-root"));
    TIDE_CHECK(tide_get_tide_local_component_Data(local, item.kept).value == 9);
    TIDE_CHECK(prefab_label_is(&local->heap, tide_get_tide_local_component_Data(local, item.kept).label, "local-child"));
    TIDE_CHECK(tide_get_tide_local_component_Data(local, roots.tip).value == 10);
    TIDE_CHECK(prefab_label_is(&local->heap, tide_get_tide_local_component_Data(local, roots.tip).label, "default"));
    TIDE_CHECK(tide_get_tide_local_component_Data(local, roots.source).value == 1);
    TIDE_CHECK(tide_get_tide_local_component_Data(local, source.kept).value == 4);
    TIDE_CHECK(tide_get_tide_local_component_Data(local, source_child.kept).value == 6);
    TIDE_CHECK(tide_get_tide_local_component_Data(local, item.changed).value == 7);
    TIDE_CHECK(prefab_label_is(&local->heap, tide_get_tide_local_component_Data(local, source.changed).label, "local-sibling"));
    TIDE_CHECK(tide_get_tide_local_component_LevelSettings(local, roots.item).difficulty == 5);
    TIDE_CHECK(tide_get_tide_local_component_Data(local, roots.added).value == 3);
}

TIDE_TEST(prefab_descendant_overrides_support_local_spawn_and_scene_load)
{
    tide_world w = {0};
    tide_local local = {0};
    tide_draw_list draw = {0};
    tide_gui gui = {0};
    tide_world_init(&w, 0.1f);
    tide_local_init(&local);
    const uint64_t hash = tide_world_hash(&w);
    tide_frame(&w, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    const uint32_t before = local.entities.next_unused;
    local.LocalProbe.spawnDescendants = true;
    tide_frame(&w, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    TIDE_CHECK(local.entities.next_unused - before == 22);
    TIDE_CHECK(local.LocalProbe.descendantReady == 2 && !local.LocalProbe.invalidDescendants);
    prefab_check_local_descendant_tree(&local, local.LocalProbe.descendants);
    prefab_check_local_descendant_tree(&local, local.LocalProbe.descendantsAgain);
    TIDE_CHECK(tide_world_hash(&w) == hash);
    tide_draw_free(&draw);
    tide_local_free(&local);
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
