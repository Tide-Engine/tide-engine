#include <math.h>

#include "game.h"
#include "tide_test.h"

static void near3(const tide_float3 value, const float x, const float y, const float z)
{
    const float tolerance = 0.000003f;
    TIDE_CHECK(isfinite(value.x) && fabsf(value.x - x) < tolerance);
    TIDE_CHECK(isfinite(value.y) && fabsf(value.y - y) < tolerance);
    TIDE_CHECK(isfinite(value.z) && fabsf(value.z - z) < tolerance);
}

TIDE_TEST(render3d_pbr_linear_light_and_surface_callbacks)
{
    static tide_world world;
    static tide_local local;
    static tide_draw_list draw;
    static tide_gui gui;
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    const uint64_t hash = tide_world_hash(&world);
    const float inv_pi = 0.3183098861837907f;
    // Independent analytic cases, not a second implementation of the BRDF.
    const float expected[] = {
        0, 1, 0.21404114048223255f, 0.00313080495356037f,
        0, 1, 0.5f, 0.040449936f
    };
    for (int frame = 0; frame < 3; frame++) {
        local.PbrResults.specialRadiance = tide_f3(INFINITY, -INFINITY, NAN);
        local.PbrResults.specialExposure = frame == 0 ? INFINITY : frame == 1 ? NAN : 0;
        tide_draw_reset(&draw);
        tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
        const PbrResults *r = &local.PbrResults;
        const tide_float3 *lit = r->lighting.at;
        const float dielectric = 0.49f * inv_pi;
        const float metal = 0.125f * inv_pi;
        near3(lit[0], dielectric, dielectric, dielectric);
        near3(lit[1], metal, metal, metal);
        // N.H = 1, N.V = N.L = V.H = 1/2: Fd+Fr is 1/(2*pi)
        // for the dielectric, before multiplying by N.L. Correlated Smith
        // gives V = 1/2 here; an uncorrelated approximation does not.
        near3(lit[2], 0.25f * inv_pi, 0.25f * inv_pi, 0.25f * inv_pi);
        near3(lit[3], 0.12890625f * inv_pi, 0.12890625f * inv_pi, 0.12890625f * inv_pi);
        near3(lit[4], 0, 0, 0);
        near3(lit[5], 0, 0, 0);
        for (int i = 6; i <= 8; i++) near3(lit[i], 0.1f, 0.2f, 0.3f);
        TIDE_CHECK(isfinite(lit[9].x) && lit[9].x > 0);
        TIDE_CHECK(lit[10].x == lit[11].x && lit[12].x == lit[11].x);
        TIDE_CHECK(lit[11].x > 100); // Smooth surfaces retain their specular peak.
        near3(lit[13], dielectric, dielectric, dielectric);
        near3(lit[14], dielectric, dielectric, dielectric);
        near3(lit[15], metal, metal, metal);
        near3(lit[16], 0.1f, 0.2f, 0.3f);
        near3(lit[17], 0.1f + dielectric * 0.25f, 0.2f + dielectric * 0.25f, 0.3f + dielectric * 0.25f);
        near3(lit[18], 49 * inv_pi, 49 * inv_pi, 49 * inv_pi);
        near3(lit[19], 0.01f * inv_pi, 0.01f * inv_pi, 0.01f * inv_pi);
        near3(lit[20], 0.97f * inv_pi, 0.97f * inv_pi, 0.97f * inv_pi);
        near3(lit[21], 0.25f * inv_pi, 0.0625f * inv_pi, 0);
        near3(lit[22], dielectric, dielectric, dielectric);
        near3(lit[23], 0.25f * inv_pi, 0.25f * inv_pi, 0.25f * inv_pi);
        near3(lit[24], 2 * inv_pi, 2 * inv_pi, 2 * inv_pi);
        near3(lit[25], 0.64f * inv_pi, 0.64f * inv_pi, 0.64f * inv_pi);
        // N.H = 1, N.V = N.L = V.H = 1/4, roughness 1/2:
        // (2343/25600 + 857/(50*sqrt(31))) / pi, including N.L.
        near3(lit[26], 1.0090290702f, 1.0090290702f, 1.0090290702f);
        for (unsigned i = 0; i < sizeof expected / sizeof expected[0]; i++)
            TIDE_CHECK(fabsf(r->transfer.at[i] - expected[i]) < 0.000002f);
        near3(r->roundtrip, 0.02f, 0.5f, 1);
        near3(r->presentation.at[0], 0, 2.0f / 3.0f, 8.0f / 9.0f);
        near3(r->presentation.at[1], 0, 0.8360069707f, 0.9494744828f);
        const float mapped = 0.9397488168f; // (49/pi) / (1 + 49/pi)
        near3(r->presentation.at[2], mapped, mapped, mapped);
        near3(r->presentation.at[3], frame == 0 ? 1.0f : 0.0f, 0, 0);
        near3(r->presentation.at[4], 0, 0, 0);
        for (int i = 0; i < 4; i += 3) {
            const Render3D_Surface s = r->callbacks.at[i];
            near3(s.albedo, 0.2f, 0.3f, 0.4f);
            near3(s.normal, 0, 2, 0);
            near3(s.emission, 0.1f, 0.1f, 0.1f);
            TIDE_CHECK(s.roughness == 0.6f && s.metallic == 0.4f);
        }
        const Render3D_Surface custom = r->callbacks.at[1];
        near3(custom.albedo, 0.2140411405f, 0.05087608817f, 0.522521554f);
        near3(custom.normal, 0, 1, 0);
        near3(custom.emission, 0.2f, 0.4f, 1);
        TIDE_CHECK(custom.roughness == 0.75f && custom.metallic == 0.25f);
        const Render3D_Surface captured = r->callbacks.at[2];
        near3(captured.albedo, 0.1f, 0.15f, 0.2f);
        near3(captured.normal, 0, 2, 0);
        near3(captured.emission, 1, 2, 3);
        TIDE_CHECK(captured.roughness == 0.6f && captured.metallic == 0.4f);
        const Render3D_Surface defaults = r->callbacks.at[4];
        near3(defaults.albedo, 1, 1, 1);
        near3(defaults.normal, 0, 1, 0);
        near3(defaults.emission, 0, 0, 0);
        TIDE_CHECK(defaults.roughness == 0.5f && defaults.metallic == 0);
        TIDE_CHECK(r->frames == frame + 1);
        TIDE_CHECK(tide_world_hash(&world) == hash);
    }
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
}
