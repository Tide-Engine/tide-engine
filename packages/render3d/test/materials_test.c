#include <math.h>

#include "game.h"
#include "tide_test.h"

static void near1(float actual, float expected)
{
    TIDE_CHECK(isfinite(actual) && fabsf(actual - expected) < 0.00001f);
}

static void near3(tide_float3 actual, float x, float y, float z)
{
    near1(actual.x, x);
    near1(actual.y, y);
    near1(actual.z, z);
}

static void near_frame(Render3D_SurfaceInput actual, tide_float3 n, tide_float3 t, tide_float3 b)
{
    near3(actual.normal, n.x, n.y, n.z);
    near3(actual.tangent, t.x, t.y, t.z);
    near3(actual.bitangent, b.x, b.y, b.z);
    near1(tide_dot_f3(actual.normal, actual.tangent), 0);
    near1(tide_dot_f3(actual.normal, actual.bitangent), 0);
    near1(tide_dot_f3(actual.tangent, actual.bitangent), 0);
}

TIDE_TEST(render3d_material_frames_hooks_and_indirect_light)
{
    static tide_world world;
    static tide_local local;
    static tide_draw_list draw;
    static tide_gui gui;
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    const uint64_t hash = tide_world_hash(&world);
    const tide_float3 x = tide_f3(1, 0, 0), y = tide_f3(0, 1, 0), z = tide_f3(0, 0, 1);
    for (int frame = 0; frame < 3; frame++) {
        tide_draw_reset(&draw);
        tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
        const MaterialResults *r = &local.MaterialResults;
        near_frame(r->frames.at[0], z, x, y);
        near_frame(r->frames.at[1], z, x, y);
        // A^-T(1,1,1) has direction (48,16,17), while A(1,-1,0)
        // has direction (1,-3,0). This checks shear as well as unequal scale.
        const float normal_length = sqrtf(2849), bitangent_length = sqrtf(28490);
        near_frame(r->frames.at[2], tide_f3(48 / normal_length, 16 / normal_length, 17 / normal_length),
            tide_f3(1 / sqrtf(10), -3 / sqrtf(10), 0),
            tide_f3(51 / bitangent_length, 17 / bitangent_length, -160 / bitangent_length));
        near_frame(r->frames.at[3], z, tide_f3(-1, 0, 0), y);
        near_frame(r->frames.at[4], z, tide_f3(-1, 0, 0), tide_f3(0, -1, 0));
        near_frame(r->frames.at[5], z, tide_f3(0, -1, 0), x);
        near_frame(r->frames.at[6], tide_f3(0, 0, 0), x, tide_f3(0, 0, -1));
        near_frame(r->frames.at[7], tide_f3(0, 0, -1), x, tide_f3(0, -1, 0));
        near3(r->normals.at[0], 0, 0, 1);
        near3(r->normals.at[1], 1, 0, 0);
        near3(r->normals.at[2], -1, 0, 0);
        near3(r->normals.at[3], 0, -1, 0);
        near3(r->normals.at[4], 0, -1, 0);
        near3(r->normals.at[5], 0, 0, 1);
        near3(r->normals.at[6], 0, 1, 0);
        near1(r->uvs.at[0].x, 0.25f); near1(r->uvs.at[0].y, 0.5f);
        near1(r->uvs.at[1].x, 0.375f); near1(r->uvs.at[1].y, 0.75f);
        near1(r->uvs.at[2].x, 0.275f); near1(r->uvs.at[2].y, 0.75f);
        for (int i = 3; i < 5; i++) {
            near1(r->uvs.at[i].x, 0.375f); near1(r->uvs.at[i].y, 0.75f);
        }
        near1(r->coverage.at[0].alpha, 0.25f); near1(r->coverage.at[0].cutoff, 0.5f);
        near1(r->coverage.at[1].alpha, 0); near1(r->coverage.at[1].cutoff, 1);
        near1(r->coverage.at[2].alpha, 1); near1(r->coverage.at[2].cutoff, 0);
        near1(r->coverage.at[3].alpha, 0.4f); near1(r->coverage.at[3].cutoff, 0.75f);
        near1(r->coverage.at[4].alpha, 0.5f); near1(r->coverage.at[4].cutoff, 0.5f);
        near3(r->indirect.at[0], 0.09375f, 0.1875f, 0.375f);
        for (int i = 1; i < 4; i++) near3(r->indirect.at[i], 0, 0, 0);
        near3(r->indirect.at[4], 0, 0.5f, 1);
        near3(r->direct.at[1], r->direct.at[0].x, r->direct.at[0].y, r->direct.at[0].z);
        near3(r->emission.at[0], 2, 4, 8);
        near3(r->emission.at[1], 2, 4, 8);
        TIDE_CHECK(r->framesEvaluated == frame + 1);
        TIDE_CHECK(tide_world_hash(&world) == hash);
    }
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
}
