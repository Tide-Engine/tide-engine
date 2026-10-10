#include "game.h"
#include "tide/pack.h"
#include "tide_test.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static uint32_t expected_mark;
static uint32_t observations;
static uint32_t bad_marks;

void skinned_draw_observe(const int32_t step)
{
    if (step != (int32_t)observations || tide_scratch_mark() != expected_mark) bad_marks++;
    observations++;
}

Shade skinned_draw_c_edit(void)
{
    tide_bone_transform *transform = tide_list_at_mut(local.Cases.pose.transforms, local.Cases.hand.index, sizeof *transform);
    if (transform) transform->position.x = -11.0f;
    local.Cases.changes++;
    return (Shade){.color = TIDE_COLOR_WHITE};
}

static void start(const int mode)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    tide_frame(&world, NULL, 1.0f, NULL, &local, &draw, &gui);
    local.Cases.mode = mode;
    expected_mark = tide_scratch_mark();
    observations = bad_marks = 0u;
    tide_draw_reset(&draw);
}

static void stop(void)
{
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_pack_unload();
}

static uint32_t instances(void)
{
    uint32_t count = 0u;
    for (uint32_t i = 0; i < draw.run_count; i++) count += draw.runs[i].count;
    return count;
}

static float palette_x(const uint32_t run_at)
{
    const tide_draw_run *run = &draw.runs[run_at];
    for (uint32_t i = 0; i < run->shader->texture_count; i++) {
        if (run->shader->texture_reads[i] != TIDE_SHADER_READ_DATA) continue;
        const tide_draw_texture *texture = &draw.textures[run->textures[i] - 1u];
        const float *matrices = (const float *)(const void *)texture->pixels;
        return matrices[12];
    }
    return 12345.0f;
}

static float pose_x(void)
{
    const tide_bone_transform *transform = tide_list_read(&local.heap, local.Cases.pose.transforms,
                                                       local.Cases.hand.index, sizeof *transform);
    return transform ? transform->position.x : 12345.0f;
}

TIDE_TEST(skinned_draw_borrows_unchanged_model_and_pose_without_owned_copies)
{
    start(0);
    tide_frame(&world, NULL, 1.0f, NULL, &local, &draw, &gui);
    TIDE_CHECK(observations == 3u && bad_marks == 0u);
    TIDE_CHECK(instances() == 2u);
    TIDE_CHECK(pose_x() == 0.0f);
    TIDE_CHECK(tide_scratch_mark() == expected_mark);
    stop();
}

TIDE_TEST(skinned_draw_snapshots_model_before_a_later_operand_replaces_it)
{
    start(1);
    tide_frame(&world, NULL, 1.0f, NULL, &local, &draw, &gui);
    TIDE_CHECK(local.Cases.changes == 1);
    TIDE_CHECK(tide_list_read_count(&local.heap, local.Cases.model.vertices) == 0);
    TIDE_REQUIRE(instances() == 1u);
    TIDE_CHECK(palette_x(0) == 0.0f);
    stop();
}

TIDE_TEST(skinned_draw_snapshots_pose_before_later_tide_and_c_operands_edit_it)
{
    for (int mode = 2; mode <= 3; mode++) {
        start(mode);
        tide_frame(&world, NULL, 1.0f, NULL, &local, &draw, &gui);
        TIDE_CHECK(local.Cases.changes == 1);
        TIDE_CHECK(pose_x() == (mode == 2 ? -7.0f : -11.0f));
        TIDE_REQUIRE(instances() == 1u);
        TIDE_CHECK(palette_x(0) == 0.0f);
        stop();
    }
}

TIDE_TEST(skinned_draw_captures_borrowed_pose_before_the_next_statement_edits_it)
{
    start(4);
    tide_frame(&world, NULL, 1.0f, NULL, &local, &draw, &gui);
    TIDE_REQUIRE(instances() == 2u && draw.run_count == 2u);
    TIDE_CHECK(palette_x(0) == 0.0f && palette_x(1) == -17.0f);
    TIDE_CHECK(pose_x() == -17.0f);
    stop();
}
