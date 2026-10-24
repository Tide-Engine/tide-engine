#include <stdlib.h>
#include "game.h"
#include "tide_test.h"

TIDE_TEST(type_aliases_use_canonical_values_storage_and_lexical_names)
{
    tide_world world = {0}, copy = {0}, decoded = {0};
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    TIDE_CHECK(world.Probe.conversions == 49);
    TIDE_CHECK(world.Probe.primitives == 73);
    TIDE_CHECK(world.Probe.statics == 28);
    TIDE_CHECK(world.Probe.values == 29);
    TIDE_CHECK(world.Probe.foldForward == 2);
    TIDE_CHECK(world.Probe.foldChecked == 2);
    TIDE_CHECK(world.Probe.foldAlias == 4);
    TIDE_CHECK(world.Probe.containers == 28);
    TIDE_CHECK(world.Probe.forward == 17);
    TIDE_CHECK(world.Probe.worldValue == 711);
    TIDE_CHECK(world.Probe.queries == 3);
    TIDE_CHECK(world.Probe.lexical == 63);
    const AliasTypes_Unit unit = tide_get_AliasTypes_Unit(&world, world.Probe.selected);
    TIDE_CHECK(unit.pair.left == 3 && unit.pair.right == 8);
    TIDE_CHECK(unit.value.ok && unit.value.value == 7);
    TIDE_CHECK(sizeof(AliasTypes_State) == sizeof(uint8_t));

    const uint64_t hash = tide_world_hash(&world);
    tide_world_copy(&copy, &world);
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    void *bytes = malloc(size);
    TIDE_REQUIRE(bytes != NULL);
    TIDE_REQUIRE(tide_world_pack(&world, bytes, size) == size);
    TIDE_REQUIRE(tide_world_unpack(&decoded, bytes, size));
    free(bytes);
    TIDE_CHECK(tide_world_hash(&decoded) == hash);
    tide_world_tick(&copy);
    tide_world_tick(&decoded);
    TIDE_CHECK(tide_world_hash(&copy) == tide_world_hash(&decoded));
    TIDE_CHECK(tide_world_hash(&world) == hash);
    tide_world_free(&decoded);
    tide_world_free(&copy);
    tide_world_free(&world);
}

TIDE_TEST(type_aliases_forward_callables_and_configure_shader_bindings)
{
    tide_world world = {0};
    tide_local local = {0};
    tide_draw_list draw = {0};
    tide_gui gui = {0};
    tide_world_init(&world, 1.0f / 60.0f);
    tide_local_init(&local);
    const uint64_t hash = tide_world_hash(&world);
    for (int frame = 0; frame < 2; frame++) {
        tide_draw_reset(&draw);
        tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
        TIDE_CHECK(local.LocalProbe.frames == frame + 1);
        TIDE_CHECK(local.LocalProbe.result == 23);
        TIDE_CHECK(local.LocalProbe.forwardResult == 6);
        unsigned shaded = 0;
        for (uint32_t i = 0; i < draw.count; i++) {
            if (draw.commands[i].kind != TIDE_DRAW_SHADED) continue;
            const tide_draw_run *run = &draw.runs[draw.commands[i].shaded.run];
            TIDE_REQUIRE(run->shader != NULL);
            TIDE_CHECK(run->count == 1);
            TIDE_CHECK(run->shader->vertex_size == sizeof(tide_vertex));
            shaded++;
        }
        TIDE_CHECK(shaded == 1);
        TIDE_CHECK(tide_world_hash(&world) == hash);
    }
    tide_draw_free(&draw);
    tide_draw_free(&gui.list);
    tide_local_free(&local);
    tide_world_free(&world);
}
