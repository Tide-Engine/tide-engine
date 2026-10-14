#include <string.h>

#include "game.h"
#include "tide/profile.h"
#include "tide_test.h"

static tide_world world;
static tide_world plain;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

static bool named(const tide_profile_times *times, const uint32_t i, const char *name)
{
    return i < times->count && strcmp(times->items[i].name, name) == 0;
}

// A tick whose jobs have a profile says what its systems took, each by its
// name in Tide, and what followed them.
TIDE_TEST(profile_ticks_say_their_systems)
{
    tide_profile profile = {0};
    const tide_jobs jobs = {.threads = 1, .profile = &profile}; // This thread, as a host without a pool has it
    tide_world_init(&world, 1.0f / 60.0f);
    for (int i = 0; i < 3; i++) tide_world_tick_on(&world, &jobs);
    TIDE_CHECK(profile.ticks == 3 && world.Combat_Tally.hits == 3);
    TIDE_REQUIRE(profile.systems.count == 2);
    TIDE_CHECK(named(&profile.systems, 0, "Move") && named(&profile.systems, 1, "Combat.Hit"));
    for (uint32_t i = 0; i < 2; i++) {
        const tide_profile_time *t = &profile.systems.items[i];
        TIDE_CHECK(t->ms >= 0.0 && t->total >= t->ms);
    }
    TIDE_CHECK(profile.systems_ms >= 0.0 && profile.changes_ms >= 0.0);

    // Measuring changes nothing: the same world as ticks nobody measured
    tide_world_init(&plain, 1.0f / 60.0f);
    for (int i = 0; i < 3; i++) tide_world_tick(&plain);
    TIDE_CHECK(tide_world_hash(&plain) == tide_world_hash(&world));
    const tide_jobs unmeasured = {.threads = 1};
    tide_world_tick_on(&world, &unmeasured);
    tide_world_tick(&world);
    TIDE_CHECK(profile.ticks == 3 && world.Combat_Tally.hits == 5);
    tide_profile_free(&profile);
    tide_world_free(&plain);
}

// A frame whose GUI has a profile says what its views took, in their order.
TIDE_TEST(profile_frames_say_their_views)
{
    tide_profile profile = {0};
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    gui.profile = &profile;
    for (int i = 0; i < 2; i++) {
        tide_draw_reset(&draw);
        tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    }
    TIDE_REQUIRE(profile.views.count == 2);
    TIDE_CHECK(named(&profile.views, 0, "Backdrop") && named(&profile.views, 1, "Bodies"));
    for (uint32_t i = 0; i < 2; i++) {
        const tide_profile_time *t = &profile.views.items[i];
        TIDE_CHECK(t->ms >= 0.0 && t->total >= t->ms);
    }
    TIDE_CHECK(profile.ticks == 0); // Views are no tick
    const uint32_t drawn = draw.count;
    TIDE_CHECK(drawn == 3); // The camera, and a circle for each body

    // Without one, the same frame, and nothing measured
    tide_profile_clear(&profile);
    gui.profile = NULL;
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    TIDE_CHECK(draw.count == drawn && profile.views.items[1].total == 0.0);
    tide_profile_free(&profile);
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
}
