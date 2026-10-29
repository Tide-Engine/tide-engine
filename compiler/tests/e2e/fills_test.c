#include <math.h>

#include "game.h"
#include "tide_test.h"

static int near(const float a, const float b)
{
    return fabsf(a - b) < 0.0001f;
}

static int color_is(const tide_color c, const float r, const float g, const float b, const float a)
{
    return near(c.r, r) && near(c.g, g) && near(c.b, b) && near(c.a, a);
}

// The fills drawn one after another are one run of one shader, an instance
// each, whatever function colors each one; a fill with no size draws nothing.
TIDE_TEST(fills_draw_as_one_run_of_instances)
{
    static tide_world world;
    static tide_local local;
    static tide_draw_list draw;
    static tide_gui gui;
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    const uint64_t hash = tide_world_hash(&world);
    for (int frame = 0; frame < 3; frame++) {
        tide_draw_reset(&draw);
        tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
        TIDE_CHECK(local.Probe.frames == frame + 1);
        TIDE_REQUIRE(draw.run_count == 1);
        TIDE_CHECK(draw.runs[0].count == 4);
        TIDE_CHECK(draw.runs[0].shader->instance_ints > 0); // The functions' tags
        TIDE_CHECK(tide_world_hash(&world) == hash);
    }
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
}

// Gradient's functions are values like any Func<float2, Color>: called on the
// CPU, they give what a pixel there is given.
TIDE_TEST(fills_gradients_give_their_colors)
{
    static tide_world world;
    static tide_local local;
    static tide_draw_list draw;
    static tide_gui gui;
    tide_world_init(&world, 1.0f / 60.0f);
    tide_local_init(&local);
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    const tide_color *seen = local.Probe.seen.at;
    TIDE_CHECK(color_is(seen[0], 1, 0, 0, 1));         // Vertical: its top
    TIDE_CHECK(color_is(seen[1], 0, 0, 1, 1));         // ...its bottom
    TIDE_CHECK(color_is(seen[2], 0.5f, 0, 0.5f, 1));   // ...between
    TIDE_CHECK(color_is(seen[3], 0, 1, 0, 1));         // Three colors: the middle one at the middle
    TIDE_CHECK(color_is(seen[4], 0, 0.5f, 0.5f, 1));   // ...and halfway to the last
    TIDE_CHECK(color_is(seen[5], 1, 1, 1, 0.5f));      // To clear: white, fading, never gray
    TIDE_CHECK(color_is(seen[6], 1, 0, 0, 1));         // Radial: its middle
    TIDE_CHECK(color_is(seen[7], 0, 0, 1, 1));         // ...the middle of a side
    TIDE_CHECK(color_is(seen[8], 1, 0, 0, 1));         // Linear at 0: its left edge
    TIDE_CHECK(color_is(seen[9], 1, 1, 1, 1));         // Four colors: the last at the end
    TIDE_CHECK(color_is(seen[10], 1, 0, 0, 1));        // A uv that isn't a number: the start
    TIDE_CHECK(color_is(seen[11], 0, 0, 0, 1));        // A function of the game's
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
}
