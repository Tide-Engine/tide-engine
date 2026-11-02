#include <string.h>

#include "game.h"
#include "tide/profile.h"
#include "tide/draw_profile.h"
#include "tide_test.h"
#include "draw_test.h"

static tide_world world;
static tide_world plain;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static uint32_t observed_views;

void CheckProfileView(const int32_t index)
{
    if (!draw.diagnostics) return;
    TIDE_REQUIRE(draw.profile_source);
    TIDE_CHECK(strcmp(draw.profile_source->name, index == 0 ? "Backdrop" : "Bodies") == 0);
    TIDE_CHECK(draw.profile_source->file && strstr(draw.profile_source->file, "profile.tide"));
    observed_views++;
}

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
    TIDE_CHECK(strcmp(profile.systems.items[0].source.name, "Move") == 0);
    TIDE_CHECK(strstr(profile.systems.items[0].source.file, "profile.tide") != NULL);
    TIDE_CHECK(profile.systems.items[0].source.line == 18u && profile.systems.items[0].source.column == 8u);
    TIDE_CHECK(strstr(profile.systems.items[1].source.file, "profile_combat.tide") != NULL);
    TIDE_CHECK(profile.systems.items[1].source.line == 9u && profile.systems.items[1].source.column == 8u);
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
    tide_world_free(&world);
}

// A frame whose GUI has a profile says what its views took, in their order.
TIDE_TEST(profile_frames_say_their_views)
{
    tide_profile profile = {0};
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    gui.profile = &profile;
    tide_draw_diagnostics diagnostics = {0};
    const tide_profile_source previous_source = {.name = "Host"};
    observed_views = 0;
    for (int i = 0; i < 2; i++) {
        tide_draw_reset(&draw);
        draw.diagnostics = &diagnostics;
        draw.profile_source = &previous_source;
        tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
        TIDE_CHECK(draw.profile_source == &previous_source);
        tide_test_flatten(&draw);
    }
    TIDE_CHECK(observed_views == 6);
    TIDE_REQUIRE(profile.views.count == 3);
    TIDE_CHECK(named(&profile.views, 0, "Backdrop") && named(&profile.views, 1, "Bodies"));
    TIDE_CHECK(profile.views.items[0].source.line == 24u && profile.views.items[0].source.column == 6u);
    TIDE_CHECK(strcmp(profile.views.items[2].source.name, "Combat.PresentationWithANameLongerThanTheProfilerPanelCanDisplay") == 0);
    TIDE_CHECK(strlen(profile.views.items[2].name) == TIDE_PROFILE_NAME - 1u);
    TIDE_CHECK(strstr(profile.views.items[2].source.file, "profile_combat.tide") != NULL);
    for (uint32_t i = 0; i < 3; i++) {
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
    draw.diagnostics = NULL;
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    tide_test_flatten(&draw);
    TIDE_CHECK(draw.count == drawn && profile.views.items[1].total == 0.0);
    tide_profile_free(&profile);
    tide_draw_diagnostics_free(&diagnostics);
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
}
