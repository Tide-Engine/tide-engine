#include <stddef.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

_Static_assert(sizeof(LoadBytes) == 3, "one-byte lanes");
_Static_assert(sizeof(LoadPair) == 2, "two-byte lane");
_Static_assert(sizeof(LoadWide) == 24, "four-byte lanes include padding");
_Static_assert(offsetof(LoadWide, value) == 8 && offsetof(LoadWide, b) == 16, "canonical padded offsets");
_Static_assert(sizeof(LoadTag) == 1, "an empty component keeps its existing C byte");

static LoadWide wide(const double value)
{
    LoadWide result;
    memset(&result, 0, sizeof result);
    result.a = 7;
    result.value = value;
    result.b = 211;
    return result;
}

TIDE_TEST(component_loads_copy_current_match_rows_and_complete_layouts)
{
    tide_world world = {0}, copy = {0};
    tide_world_init(&world, 0.1f);
    tide_world_tick(&world);
    const tide_entity selected = world.LoadProbe.selected;
    const LoadBytes bytes = tide_get_LoadBytes(&world, selected);
    const LoadPair pair = tide_get_LoadPair(&world, selected);
    TIDE_CHECK(bytes.a == 17 && bytes.b == 93 && bytes.c == 241);
    TIDE_CHECK(pair.a == 29 && pair.b == 157);
    const LoadWide before = wide(3.25);
    LoadWide actual = tide_get_LoadWide(&world, selected);
    TIDE_CHECK(memcmp(&actual, &before, sizeof actual) == 0);
    TIDE_CHECK(tide_get_LoadTag(&world, selected).tide_empty == 0);
    TIDE_CHECK(tide_has_LoadOptional(&world, selected));
    TIDE_CHECK(tide_get_LoadOptional(&world, selected).value == 2050);
    TIDE_CHECK(!world.LoadProbe.found && world.LoadProbe.missing == 0 && world.LoadProbe.fallback == 19);

    const uint64_t hash = tide_world_hash(&world);
    tide_world_copy(&copy, &world);
    const LoadWide changed = wide(-19.5);
    TIDE_REQUIRE(tide_set_LoadWide(&world, selected, changed));
    actual = tide_get_LoadWide(&world, selected);
    TIDE_CHECK(memcmp(&actual, &changed, sizeof actual) == 0);
    actual = tide_get_LoadWide(&copy, selected);
    TIDE_CHECK(memcmp(&actual, &before, sizeof actual) == 0);
    TIDE_CHECK(tide_world_hash(&copy) == hash);
    tide_world_tick(&world);
    TIDE_CHECK(world.LoadProbe.seen == -19.5); // Alias World.Get reads the replacement page.

    const tide_entity absent[] = {{0}, world.LoadProbe.other,
        {selected.index, selected.generation + 1u}, {UINT32_MAX, UINT32_MAX}};
    const LoadWide zero = {0};
    for (size_t i = 0; i < sizeof absent / sizeof absent[0]; i++) {
        TIDE_CHECK(!tide_has_LoadWide(&world, absent[i]));
        actual = tide_get_LoadWide(&world, absent[i]);
        TIDE_CHECK(memcmp(&actual, &zero, sizeof actual) == 0);
    }
    TIDE_CHECK(!tide_has_LoadOptional(&world, world.LoadProbe.absent));
    TIDE_CHECK(tide_get_LoadOptional(&world, world.LoadProbe.absent).value == 0);
    tide_world_free(&copy);
    tide_world_free(&world);
}

TIDE_TEST(component_loads_copy_local_rows_with_the_same_canonical_layout)
{
    tide_world world = {0};
    tide_local local = {0};
    tide_draw_list draw = {0};
    tide_gui gui = {0};
    tide_world_init(&world, 0.1f);
    tide_local_init(&local);
    for (int frame = 0; frame < 2; frame++)
        tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    const tide_entity selected = local.LoadLocalProbe.selected;
    const LoadBytes bytes = tide_get_tide_local_component_LoadBytes(&local, selected);
    const LoadPair pair = tide_get_tide_local_component_LoadPair(&local, selected);
    TIDE_CHECK(bytes.a == 17 && bytes.b == 93 && bytes.c == 241);
    TIDE_CHECK(pair.a == 29 && pair.b == 157);
    const LoadWide expected = wide(3.25);
    LoadWide actual = tide_get_tide_local_component_LoadWide(&local, selected);
    TIDE_CHECK(memcmp(&actual, &expected, sizeof actual) == 0);
    TIDE_CHECK(tide_get_tide_local_component_LoadTag(&local, selected).tide_empty == 0);
    const LoadWide changed = wide(27.75);
    TIDE_REQUIRE(tide_set_tide_local_component_LoadWide(&local, selected, changed));
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    TIDE_CHECK(local.LoadLocalProbe.seen == 27.75);
    actual = tide_get_tide_local_component_LoadWide(&local, local.LoadLocalProbe.other);
    const LoadWide zero = {0};
    TIDE_CHECK(memcmp(&actual, &zero, sizeof actual) == 0);
    tide_draw_free(&gui.list);
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
}
