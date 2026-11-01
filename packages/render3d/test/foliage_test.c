#include <math.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

static void near3(tide_float3 value, float x, float y, float z)
{
    TIDE_CHECK(isfinite(value.x) && fabsf(value.x - x) < 0.000003f);
    TIDE_CHECK(isfinite(value.y) && fabsf(value.y - y) < 0.000003f);
    TIDE_CHECK(isfinite(value.z) && fabsf(value.z - z) < 0.000003f);
}

TIDE_TEST(render3d_thin_leaf_diffuse_budget_backlight_shadows_and_capture)
{
    static tide_world world;
    static tide_local local;
    static tide_draw_list draw;
    static tide_gui gui;
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    const uint64_t hash = tide_world_hash(&world);
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    const FoliageResults *r = &local.FoliageResults;
    const float p = 0.3183098861837907f;
    // At normal incidence, dielectric F=.04 and specular=.01/pi.
    // Half the .48/pi diffuse budget remains reflected; the back lobe
    // receives .25/pi for albedo .5 and transmission .5.
    near3(r->ordinary, .49f * p, .49f * p, .49f * p);
    TIDE_CHECK(memcmp(&r->ordinary, &r->disabled, sizeof r->ordinary) == 0);
    near3(r->light.at[0], .25f * p, .25f * p, .25f * p);
    near3(r->light.at[1], .25f * p, .25f * p, .25f * p);
    near3(r->light.at[2], .125f * p, .25f * p, .5f * p);
    for (int i = 3; i <= 6; i++) near3(r->light.at[i], 0, 0, 0);
    near3(r->light.at[7], .25f * p, .25f * p, .25f * p);
    near3(r->light.at[8], 0, 0, 0);
    near3(r->light.at[9], 0, 0, 0);
    near3(r->transmitted.at[0], .125f, .25f, .0625f);
    near3(r->light.at[10], .0625f * p, .125f * p, .03125f * p);
    near3(r->transmitted.at[1], 0, .5f, 1);
    near3(r->light.at[11], 0, .5f * p, 2 * p);
    near3(r->transmitted.at[2], .25f, .125f, .0625f);
    near3(r->light.at[12], .125f * p, .0625f * p, .03125f * p);
    TIDE_CHECK(r->transmitted.at[3].x > 0 && r->transmitted.at[3].x < 1e-30f);
    TIDE_CHECK(r->light.at[13].x > 0 && r->light.at[13].x < 1e-30f);
    near3(r->light.at[14], 2, 3, 4);
    near3(r->light.at[15], .25f * p, .25f * p, .25f * p);
    TIDE_CHECK(tide_world_hash(&world) == hash);
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
}
