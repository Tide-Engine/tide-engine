#include <math.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

static void near_value(const float value, const float expected)
{
    TIDE_CHECK(isfinite(value) && fabsf(value - expected) < 0.00001f);
}

static void near3(const tide_float3 value, const float x, const float y, const float z)
{
    near_value(value.x, x); near_value(value.y, y); near_value(value.z, z);
}

static float dot(const tide_float3 a, const tide_float3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

TIDE_TEST(render3d_impostor_frames_depth_normal_mask_and_existing_vertex_payload)
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
    const ImpostorResults *r = &local.ImpostorResults;
    for (unsigned i = 0; i < 24; i++) {
        const Render3D_ImpostorBasis *basis = &r->basis.at[i];
        near_value(dot(basis->right, basis->right), 1);
        near_value(dot(basis->up, basis->up), 1);
        near_value(dot(basis->toward, basis->toward), 1);
        near_value(dot(basis->right, basis->up), 0);
        near_value(dot(basis->right, basis->toward), 0);
        near_value(dot(basis->up, basis->toward), 0);
        near_value(r->uv.at[i].x, ((float)(i % 8) + 0.5f) / 8.0f);
        near_value(r->uv.at[i].y, ((float)(i / 8) + 0.5f) / 4.0f);
    }
    TIDE_CHECK(r->blend.at[0].frames.x == 0 && r->blend.at[0].frames.y == 1
        && r->blend.at[0].frames.z == 8 && r->blend.at[0].frames.w == 9);
    near_value(r->blend.at[0].weights.x, 1);
    near_value(r->blend.at[1].weights.z, 1);
    TIDE_CHECK(r->blend.at[1].frames.z == 16);
    TIDE_CHECK(r->blend.at[2].frames.x == 7 && r->blend.at[2].frames.y == 0);
    for (unsigned i = 0; i < 4; i++) {
        const tide_float4 w = r->blend.at[i].weights;
        TIDE_CHECK(w.x >= 0 && w.y >= 0 && w.z >= 0 && w.w >= 0);
        near_value(w.x + w.y + w.z + w.w, 1);
    }
    near_value(r->blend.at[3].weights.x, .25f);
    near_value(r->blend.at[3].weights.y, .25f);
    near_value(r->blend.at[3].weights.z, .25f);
    near_value(r->blend.at[3].weights.w, .25f);
    near3(r->inverse, 1, 2, 3);
    for (unsigned i = 0; i < 16; i++) near3(r->recovered.at[i], 1.2f, -2.3f, .7f);
    // Ray (.6,0,.8) meets z=2 after 2.5 units, at x=1.5.
    near_value(r->parallax.x, (64.0f + 1.5f / 10 * 124) / 1024);
    near_value(r->parallax.y, .125f);
    near_value(r->offsetParallax.x, r->parallax.x);
    near_value(r->offsetParallax.y, r->parallax.y);
    near_value(r->disabledParallax.x, .0625f);
    near_value(r->disabledParallax.y, .125f);
    near_value(r->clamp.x, 16.0f / 1024);
    near_value(r->clamp.y, 112.0f / 512);
    near3(r->albedo, .5f, .5f, 0);
    near3(r->normal, .707106781f, 0, .707106781f);
    near_value(r->mask.x, .5f); near_value(r->mask.y, .8f);
    near_value(r->mask.z, 0); near_value(r->mask.w, .4f);
    near_value(r->coverage.x, 0); near_value(r->coverage.y, .25f);
    near_value(r->coverage.z, .125f); near_value(r->coverage.w, .0625f);
    TIDE_CHECK(memcmp(&r->original, &r->disabled, sizeof r->original) == 0);
    // A billboard under uniform x2 scaling lies in the plane facing eye(20,18,0)
    // around world center(0,12,0). Its two offsets keep the authored lengths.
    const tide_float3 offset = tide_f3(r->world.x, r->world.y - 12, r->world.z);
    near_value(offset.x * 20 + offset.y * 6, 0);
    near_value(dot(offset, offset), 4 * (2 * 2 + 3 * 3));
    near3(r->decodedPoint, r->billboard.position.x, r->billboard.position.y - 6, r->billboard.position.z);
    near3(r->decodedEye, 10, 3, 0);
    near_value(r->billboard.color.r, .25f);
    TIDE_CHECK(r->billboard.color.g > 0 && r->billboard.color.g < .25f);
    near_value(r->billboard.color.b, 3); near_value(r->billboard.color.a, 0);
    // At azimuth90/elevation0, the first view's right axis is -Z, up is +Y.
    near_value(r->billboard.uv.x, (320 - r->billboard.position.z / 16 * 124) / 1024);
    near_value(r->billboard.uv.y, (64 - (r->billboard.position.y - 6) / 16 * 124) / 512);
    near3(r->repacked, r->decodedPoint.x + .3f, r->decodedPoint.y, r->decodedPoint.z - .2f);
    TIDE_CHECK(tide_world_hash(&world) == hash);
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
}
