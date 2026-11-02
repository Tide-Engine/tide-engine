#include <math.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

static void near3(tide_float3 value, tide_float3 expected, float tolerance)
{
    TIDE_CHECK(isfinite(value.x) && fabsf(value.x - expected.x) <= tolerance);
    TIDE_CHECK(isfinite(value.y) && fabsf(value.y - expected.y) <= tolerance);
    TIDE_CHECK(isfinite(value.z) && fabsf(value.z - expected.z) <= tolerance);
}

static void evaluate(tide_world *world, tide_local *local, tide_draw_list *draw, tide_gui *gui)
{
    const uint64_t hash = tide_world_hash(world);
    tide_draw_reset(draw);
    tide_frame(world, NULL, 1, NULL, NULL, 1, local, draw, gui);
    TIDE_CHECK(tide_world_hash(world) == hash);
}

TIDE_TEST(render3d_wind_roots_world_direction_and_deformed_frames)
{
    static tide_world world;
    static tide_local local;
    static tide_draw_list draw;
    static tide_gui gui;
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    WindProbe *p = &local.WindProbe;
    p->input.localToWorld = tide_identity_f4x4();
    p->wind.direction = tide_f2(1, 0);
    p->wind.strength = 0.3f;
    p->wind.speed = 1;
    p->wind.gustScale = 0.04f;
    p->wind.gustSpeed = 0.8f;
    p->wind.gustStrength = 0.5f;
    p->vertex = (Render3D_MeshVertex){.position = {1, 0, 2}, .normal = {1, 0, 0},
        .tangent = {0, 1, 0, -1}, .uv = {0.25f, 0.75f}, .color = {0.2f, 0.3f, 0.4f, 0.5f}};
    evaluate(&world, &local, &draw, &gui);
    TIDE_CHECK(memcmp(&p->vertex, &p->result, sizeof p->vertex) == 0);
    p->vertex.position.y = -2;
    evaluate(&world, &local, &draw, &gui);
    TIDE_CHECK(memcmp(&p->vertex, &p->result, sizeof p->vertex) == 0);
    p->vertex.position.y = 8;
    p->wind.strength = 0;
    evaluate(&world, &local, &draw, &gui);
    TIDE_CHECK(memcmp(&p->vertex, &p->result, sizeof p->vertex) == 0);
    p->wind.strength = 0.3f;
    p->wind.direction = tide_f2(0, 0);
    evaluate(&world, &local, &draw, &gui);
    TIDE_CHECK(memcmp(&p->vertex, &p->result, sizeof p->vertex) == 0);
    p->wind.direction = tide_f2(1, 0);

    // Independent exact landmarks of the smooth triangle and its derivative.
    const float waves[] = {-1, -0.6875f, 0, 0.6875f, 1, 0.6875f, 0, -0.6875f, -1};
    const float derivatives[] = {0, 4.5f, 6, 4.5f, 0, -4.5f, -6, -4.5f, 0};
    for (int frame = 0; frame < 5; frame++) {
        p->wind.time = 0.137f + (float)frame * 0.281f;
        evaluate(&world, &local, &draw, &gui);
        TIDE_CHECK(memcmp(&p->legacy, &p->result, sizeof p->result) == 0);
        for (int i = 0; i < 9; i++) {
            TIDE_CHECK(fabsf(p->wave.at[i] - waves[i]) < 0.000001f);
            TIDE_CHECK(fabsf(p->derivative.at[i] - derivatives[i]) < 0.000001f);
        }
        TIDE_CHECK(p->result.position.x != p->vertex.position.x);
        TIDE_CHECK(p->result.position.y == p->vertex.position.y && p->result.position.z == p->vertex.position.z);
        TIDE_CHECK(memcmp(&p->vertex.color, &p->result.color, sizeof p->vertex.color) == 0);
        TIDE_CHECK(memcmp(&p->vertex.uv, &p->result.uv, sizeof p->vertex.uv) == 0);
        TIDE_CHECK(p->result.tangent.w == -1);
        const tide_float3 tangent = tide_f3(p->result.tangent.x, p->result.tangent.y, p->result.tangent.z);
        // The deformed normal must be perpendicular to a finite-difference
        // tangent plane, independently of ApplyWind's analytic Jacobian.
        TIDE_CHECK(fabsf(tide_dot_f3(p->result.normal, p->dy)) < 0.003f);
        TIDE_CHECK(fabsf(tide_dot_f3(p->result.normal, p->dz)) < 0.003f);
        TIDE_CHECK(fabsf(tide_dot_f3(p->result.normal, tangent)) < 0.00001f);
        TIDE_CHECK(fabsf(tide_length_f3(p->result.normal) - 1) < 0.00001f);
        TIDE_CHECK(fabsf(tide_length_f3(tangent) - 1) < 0.00001f);
    }
    // Main/shadow eye changes cannot change geometry. Translation changes gust
    // phase, while moving vertices within one object cannot change that phase.
    const Render3D_MeshVertex saved = p->result;
    p->input.eye = tide_f3(400, 10, -900);
    evaluate(&world, &local, &draw, &gui);
    TIDE_CHECK(memcmp(&saved, &p->result, sizeof saved) == 0);
    p->input.localToWorld.c3.x = 31;
    evaluate(&world, &local, &draw, &gui);
    TIDE_CHECK(p->result.position.x != saved.position.x);
    p->input.localToWorld = tide_identity_f4x4();
    evaluate(&world, &local, &draw, &gui);
    const tide_float3 displacement = tide_sub_f3(p->result.position, p->vertex.position);
    // A rotated, nonuniformly scaled tree still moves along world +X by the
    // same world distance; the inverse linear mapping handles its placement.
    p->input.localToWorld.c0 = tide_f4(0, 0, -2, 0);
    p->input.localToWorld.c1 = tide_f4(0, 3, 0, 0);
    p->input.localToWorld.c2 = tide_f4(4, 0, 0, 0);
    evaluate(&world, &local, &draw, &gui);
    const tide_float4 placed4 = tide_mul_f4x4_f4(p->input.localToWorld,
        tide_f4(p->result.position.x - p->vertex.position.x,
            p->result.position.y - p->vertex.position.y, p->result.position.z - p->vertex.position.z, 0));
    near3(tide_f3(placed4.x, placed4.y, placed4.z), displacement, 0.000003f);
    p->input.localToWorld = (tide_float4x4){0};
    evaluate(&world, &local, &draw, &gui);
    TIDE_CHECK(memcmp(&p->vertex, &p->result, sizeof p->vertex) == 0);
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
}

static void response_frame(const WindProbe *p)
{
    const tide_float3 tangent = tide_f3(p->result.tangent.x, p->result.tangent.y, p->result.tangent.z);
    TIDE_CHECK(fabsf(tide_dot_f3(p->result.normal, p->dy)) < 0.003f);
    TIDE_CHECK(fabsf(tide_dot_f3(p->result.normal, p->dz)) < 0.003f);
    TIDE_CHECK(fabsf(tide_dot_f3(p->result.normal, tangent)) < 0.00001f);
    TIDE_CHECK(fabsf(tide_length_f3(p->result.normal) - 1) < 0.00001f);
    TIDE_CHECK(fabsf(tide_length_f3(tangent) - 1) < 0.00001f);
    TIDE_CHECK(p->result.tangent.w == -1);
    TIDE_CHECK(memcmp(&p->vertex.color, &p->result.color, sizeof p->vertex.color) == 0);
    TIDE_CHECK(memcmp(&p->vertex.uv, &p->result.uv, sizeof p->vertex.uv) == 0);
}

TIDE_TEST(render3d_authored_wind_stiffness_branch_leaf_phase_and_frames)
{
    static tide_world world;
    static tide_local local;
    static tide_draw_list draw;
    static tide_gui gui;
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    WindProbe *p = &local.WindProbe;
    p->authored = true;
    p->input.localToWorld = tide_identity_f4x4();
    p->wind.direction = tide_f2(1, 0);
    p->wind.strength = 1;
    p->wind.speed = 0;
    p->wind.gustScale = 0;
    p->wind.gustStrength = 0;
    p->vertex = (Render3D_MeshVertex){.position = {1, 2, 2}, .normal = {1, 0, 0},
        .tangent = {0, 1, 0, -1}, .uv = {0.25f, 0.75f}, .color = {0.2f, 0.3f, 0.4f, 0.5f}};
    p->bend = p->response.bend = .1f;
    p->flutter = 0;
    evaluate(&world, &local, &draw, &gui);
    TIDE_CHECK(memcmp(&p->legacy, &p->result, sizeof p->result) == 0);
    // With no gust or clock motion, the slow wave's crest changes the branch
    // envelope from .3 to 1. A bend of .1 at height 2 moves .4 in world X.
    p->response.branchPhase = .5f;
    evaluate(&world, &local, &draw, &gui);
    near3(p->result.position, tide_f3(1.4f, 2, 2), .000003f);
    TIDE_CHECK(fabsf(p->dy.x - .4f) < .003f);
    response_frame(p);
    const Render3D_MeshVertex crest = p->result;
    p->response.branchPhase = 1.5f;
    evaluate(&world, &local, &draw, &gui);
    TIDE_CHECK(memcmp(&crest, &p->result, sizeof crest) == 0);
    p->response.bend = .05f;
    evaluate(&world, &local, &draw, &gui);
    near3(p->result.position, tide_f3(1.2f, 2, 2), .000003f);
    response_frame(p);
    // Separate leaf phases put its height wave exactly at opposite extrema.
    // A flutter coefficient of .1 at height 2 gives +/-.2 displacement.
    p->response.bend = 0;
    p->response.flutter = .1f;
    p->response.leafPhase = .04f;
    evaluate(&world, &local, &draw, &gui);
    near3(p->result.position, tide_f3(1.2f, 2, 2), .000003f);
    TIDE_CHECK(fabsf(p->dy.x - .1f) < .003f);
    response_frame(p);
    p->response.leafPhase = .54f;
    evaluate(&world, &local, &draw, &gui);
    near3(p->result.position, tide_f3(.8f, 2, 2), .000003f);
    TIDE_CHECK(fabsf(p->dy.x + .1f) < .003f);
    response_frame(p);
    p->response = (Render3D_WindResponse){.bend = .018f, .flutter = .014f, .branchPhase = .123f, .leafPhase = .431f};
    p->wind.strength = .3f;
    p->wind.speed = 1;
    p->wind.gustScale = .04f;
    p->wind.gustStrength = .5f;
    p->wind.gustSpeed = .8f;
    p->vertex.position.y = 8;
    for (int i = 0; i < 5; i++) {
        p->wind.time = .137f + (float)i * .281f;
        evaluate(&world, &local, &draw, &gui);
        response_frame(p);
        const Render3D_MeshVertex saved = p->result;
        p->input.eye = tide_f3(400, 10, -900);
        evaluate(&world, &local, &draw, &gui);
        TIDE_CHECK(memcmp(&saved, &p->result, sizeof saved) == 0);
    }
    p->response.bend = p->response.flutter = 0;
    evaluate(&world, &local, &draw, &gui);
    TIDE_CHECK(memcmp(&p->vertex, &p->result, sizeof p->vertex) == 0);
    p->response.bend = .018f;
    p->vertex.position.y = 0;
    evaluate(&world, &local, &draw, &gui);
    TIDE_CHECK(memcmp(&p->vertex, &p->result, sizeof p->vertex) == 0);
    p->vertex.position.y = 8;
    p->input.localToWorld = (tide_float4x4){0};
    evaluate(&world, &local, &draw, &gui);
    TIDE_CHECK(memcmp(&p->vertex, &p->result, sizeof p->vertex) == 0);
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
}

TIDE_TEST(render3d_wind_bounds_cull_color_and_opaque_cutout_shadows)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    static tide_draw_list draw;
    static tide_gui gui;
    for (int cutout = 0; cutout < 2; cutout++) {
        static tide_world world;
        static tide_local local;
        tide_world_init(&world, 1.0f / 60.0f);
        world.Simulation.cutout = cutout != 0;
        tide_world_tick(&world);
        tide_local_init(&local);
        local.WindProbe.render = true;
        gui.width = 128; gui.height = 96;
        tide_draw_screen_size(&draw, tide_f2(128, 96));
        for (int bounded = 0; bounded < 2; bounded++) {
            local.WindProbe.bounds = bounded != 0;
            for (int frame = 0; frame < 2; frame++) {
                const float time = 3.14159f + (float)frame;
                local.WindProbe.wind.time = time;
                evaluate(&world, &local, &draw, &gui);
                tide_draw_prepare(&draw, NULL);
                uint64_t destination = 0;
                unsigned color = 0, shadow = 0;
                for (uint32_t at = 0; at < draw.count; at++) {
                    const tide_draw_command *command = &draw.commands[at];
                    if (command->kind == TIDE_DRAW_TARGET)
                        destination = command->target ? draw.targets[command->target - 1u].id : 0;
                    if (command->kind != TIDE_DRAW_SHADED) continue;
                    const tide_draw_run *run = &draw.runs[command->shaded.run];
                    if (!run->count || run->shader->dims != 3) continue;
                    TIDE_CHECK(strstr(run->shader->glsl_vertex, "ApplyWind") != NULL);
                    // Captured frame time must reach BOTH passes on each
                    // frame, without a stale per-material/shadow snapshot.
                    bool captured = false;
                    const uint32_t bytes = tide_shader_uniform_size(run->shader);
                    for (uint32_t offset = 0; offset + sizeof(float) <= bytes; offset += sizeof(float))
                        captured |= memcmp(draw.uniforms + run->uniforms + offset, &time, sizeof time) == 0;
                    TIDE_CHECK(captured);
                    if (destination == local.Render3D_Renderer.hdr.id) color += run->count;
                    else if (destination == local.Render3D_Renderer.shadowMap.id) shadow += run->count;
                    else TIDE_CHECK(false);
                }
                TIDE_CHECK(color == (bounded ? 1u : 2u));
                TIDE_CHECK(shadow == (bounded ? 1u : 2u));
            }
        }
        local.WindProbe.render = false;
        evaluate(&world, &local, &draw, &gui);
        tide_draw_forget(&draw);
        tide_local_free(&local);
        tide_world_free(&world);
        tide_render_targets_free();
    }
    tide_draw_free(&draw);
    tide_draw_free(&gui.list);
}

TIDE_TEST(render3d_wind_shadow_batches_split_different_and_mixed_bounds)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    static tide_world world;
    static tide_local local;
    static tide_draw_list draw;
    static tide_gui gui;
    tide_world_init(&world, 1.0f / 60.0f);
    world.Simulation.splitBounds = true;
    tide_world_tick(&world);
    tide_local_init(&local);
    local.WindProbe.render = true;
    local.WindProbe.bounds = true;
    gui.width = 128; gui.height = 96;
    tide_draw_screen_size(&draw, tide_f2(128, 96));
    // The first caster's geometry is translated +10000 by its callback and
    // -10000 by its placement. Both real quads are near the origin, but their
    // valid post-deformation local bounds differ. Applying the final caster's
    // bounds to the entire opaque batch would incorrectly reject the first.
    for (int mixed = 0; mixed < 2; mixed++) {
        local.WindProbe.mixedBounds = mixed != 0;
        evaluate(&world, &local, &draw, &gui);
        tide_draw_prepare(&draw, NULL);
        uint64_t destination = 0;
        unsigned color = 0, shadow = 0;
        for (uint32_t at = 0; at < draw.count; at++) {
            const tide_draw_command *command = &draw.commands[at];
            if (command->kind == TIDE_DRAW_TARGET)
                destination = command->target ? draw.targets[command->target - 1u].id : 0;
            if (command->kind != TIDE_DRAW_SHADED) continue;
            const tide_draw_run *run = &draw.runs[command->shaded.run];
            if (!run->count || run->shader->dims != 3) continue;
            if (destination == local.Render3D_Renderer.hdr.id) color += run->count;
            else if (destination == local.Render3D_Renderer.shadowMap.id) shadow += run->count;
            else TIDE_CHECK(false);
        }
        TIDE_CHECK(color == 2 && shadow == 2);
    }
    local.WindProbe.render = false;
    evaluate(&world, &local, &draw, &gui);
    tide_draw_forget(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_render_targets_free();
    tide_draw_free(&draw);
    tide_draw_free(&gui.list);
}
