#include <string.h>

#include "game.h"
#include "tide_test.h"

static tide_world world;
static tide_world saved;

TIDE_TEST(switch_values_choose_once_and_delayed_locals_initialize_on_each_path)
{
    tide_world_init(&world, 0.1f);
    TIDE_CHECK(world.Seen.selected == 2);
    TIDE_CHECK(world.Seen.calls == 25679);
    TIDE_CHECK(world.Seen.order == 21);
    TIDE_CHECK(world.Seen.delayed == 84);
    TIDE_CHECK(world.Seen.loops == 14);
    TIDE_CHECK(world.Seen.extras == 88);
    TIDE_CHECK(world.Seen.widened == 2.5f);
    TIDE_CHECK(world.Seen.pair.a == 10 && world.Seen.pair.b == 12);
    TIDE_CHECK(tide_list_count(world.Seen.items) == 4);
    const tide_str text = tide_text_read(&world.heap, world.Seen.text);
    TIDE_CHECK(text.bytes == 3 && memcmp(text.ptr, "yes", 3) == 0);
    tide_world_copy(&saved, &world);
    for (int i = 0; i < 6; i++) tide_world_tick(&world);
    TIDE_CHECK(world.Seen.asyncValue == 40 && world.Seen.asyncCalls == 1);
    const uint64_t hash = tide_world_hash(&world);
    tide_world_free(&world);
    tide_world_copy(&world, &saved);
    for (int i = 0; i < 6; i++) tide_world_tick(&world);
    TIDE_CHECK(tide_world_hash(&world) == hash);
    tide_world_free(&saved);
    tide_world_free(&world);
}
