#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

static tide_world before, previous, after, after_previous, verified;
static tide_local local, other_local;
static tide_draw_list draw;
static tide_gui gui;
static const tide_entity BODY = {1, 1};

static bool near(float a, float b) { return tide_abs_f(a - b) < 0.00002f; }
static bool same_rotation(tide_quaternion a, tide_quaternion b)
{
    return tide_abs_f(tide_dot_q(tide_normalize_q(a), tide_normalize_q(b))) > 0.99999f;
}

static void start(void)
{
    tide_local_init(&local);
    tide_local_init(&other_local);
    tide_world_init(&before, 1.0f / 60.0f);
    tide_world_copy(&previous, &before);
    tide_world_copy(&after, &before);
    tide_world_copy(&after_previous, &before);
    tide_world_copy(&verified, &before);
    tide_draw_reset(&draw);
    memset(&gui, 0, sizeof gui);
}

static void scalar(tide_world *w, float value)
{
    Body b = tide_get_Body(w, BODY);
    b.state.pose.scalar = value;
    b.state.pose.planar = tide_f2(value, value + 1);
    b.state.pose.spatial = tide_f3(value, value + 1, value + 2);
    b.state.pose.wide = tide_f4(value, value + 1, value + 2, value + 3);
    b.state.pose.ordinary = value;
    b.state.immediate = b.state.pose;
    tide_set_Body(w, BODY, b);
    w->Camera.center = tide_f3(value, value + 1, value + 2);
}

static void correct_on(tide_local *l, float alpha)
{
    const tide_view_correction c = {&before, &previous, &after, &after_previous, 0, alpha, false};
    tide_local_correct(l, &c);
}

static void frame_on(tide_local *l, const tide_world *w, const tide_world *prev, const tide_world *v, float alpha)
{
    tide_draw_reset(&draw);
    tide_frame(w, prev, alpha, v, NULL, 1.0f, l, &draw, &gui);
}

TIDE_TEST(corrections_accumulate_without_lagging_normal_motion_or_other_fields)
{
    start();
    scalar(&previous, 10); scalar(&before, 14);
    scalar(&after_previous, 4); scalar(&after, 8);
    const uint64_t hash = tide_world_hash(&after);
    correct_on(&local, 0.5f);
    frame_on(&local, &after, &after_previous, &verified, 0.5f);
    TIDE_CHECK(local.Observed.first.pose.scalar == 12);
    TIDE_CHECK(local.Observed.first.pose.planar.x == 12 && local.Observed.first.pose.planar.y == 13);
    TIDE_CHECK(local.Observed.first.pose.spatial.z == 14 && local.Observed.first.pose.wide.w == 15);
    TIDE_CHECK(local.Observed.first.pose.ordinary == 6 && local.Observed.first.immediate.scalar == 8);
    TIDE_CHECK(local.Observed.center.x == 12 && local.Observed.center.z == 14);
    TIDE_CHECK(local.Observed.second.pose.scalar == local.Observed.first.pose.scalar);
    TIDE_CHECK(tide_world_hash(&after) == hash && tide_get_Body(&after, BODY).state.pose.scalar == 8);

    scalar(&after, 10); // Two units of ordinary movement are all presented.
    frame_on(&local, &after, NULL, &after, 1);
    TIDE_CHECK(local.Observed.first.pose.scalar == 16);
    tide_world_copy(&before, &after);
    scalar(&after, 7); // A second correction adds three to the six still present.
    correct_on(&local, 1);
    frame_on(&local, &after, NULL, &after, 1);
    TIDE_CHECK(local.Observed.first.pose.scalar == 16);
    tide_local_frame_time(&local, 0.075f);
    frame_on(&local, &after, NULL, &after, 1);
    TIDE_CHECK(near(local.Observed.first.pose.scalar, 11.5f));
    TIDE_CHECK(local.Observed.first.pose.scalar == local.Observed.second.pose.scalar);

    const tide_draw_run *r = NULL;
    for (uint32_t i = 0; i < draw.count; i++) if (draw.commands[i].kind == TIDE_DRAW_SHADED) r = &draw.runs[draw.commands[i].shaded.run];
    TIDE_REQUIRE(r != NULL);
    float instance, uniform;
    memcpy(&instance, draw.instances + r->first, sizeof instance);
    memcpy(&uniform, draw.uniforms + r->uniforms, sizeof uniform);
    TIDE_CHECK(near(instance, 11.5f) && near(uniform, 11.5f));
}

TIDE_TEST(corrections_zero_error_preserves_exact_interpolation_and_snap_struct_boundary)
{
    start();
    scalar(&previous, 1); scalar(&before, 3);
    tide_world_copy(&after_previous, &previous); tide_world_copy(&after, &before);
    correct_on(&local, 0.333f);
    frame_on(&local, &after, &after_previous, &after, 0.333f);
    const float expected = tide_lerp_f(1, 3, 0.333f);
    TIDE_CHECK(local.Observed.first.pose.scalar == expected);
    TIDE_CHECK(local.Observed.first.pose.ordinary == expected);
    TIDE_CHECK(local.Observed.first.immediate.scalar == 3);
    TIDE_CHECK(local.tide_corrections_Body.rows.count == 0);
    TIDE_CHECK(!local.tide_correction_Camera.valid);
}

TIDE_TEST(corrections_rotations_wrap_normalize_accumulate_and_decay_independently_of_frames)
{
    start();
    Body b = tide_get_Body(&before, BODY);
    b.state.pose.angle = TIDE_PI_F - 0.1f;
    b.state.pose.rotation = tide_axisangle_q(tide_f3(1, 0, 0), 2.8f);
    tide_set_Body(&before, BODY, b);
    tide_world_copy(&previous, &before);
    b.state.pose.angle = -TIDE_PI_F + 0.1f;
    b.state.pose.rotation = tide_axisangle_q(tide_f3(0, 1, 0), 0.4f);
    tide_set_Body(&after, BODY, b);
    tide_world_copy(&after_previous, &after);
    before.Camera.angle = TIDE_PI_F - 0.1f;
    after.Camera.angle = -TIDE_PI_F + 0.1f;
    correct_on(&local, 1); correct_on(&other_local, 1);
    frame_on(&local, &after, NULL, &after, 1);
    TIDE_CHECK(near(tide_sin_f(local.Observed.first.pose.angle), tide_sin_f(before.Camera.angle)));
    TIDE_CHECK(same_rotation(local.Observed.first.pose.rotation, tide_get_Body(&before, BODY).state.pose.rotation));
    TIDE_CHECK(near(tide_dot_q(local.Observed.first.pose.rotation, local.Observed.first.pose.rotation), 1));
    tide_local_frame_time(&local, 0.15f);
    tide_local_frame_time(&other_local, 0.075f); tide_local_frame_time(&other_local, 0.075f);
    frame_on(&local, &after, NULL, &after, 1);
    frame_on(&other_local, &after, NULL, &after, 1);
    TIDE_CHECK(same_rotation(local.Observed.first.pose.rotation, other_local.Observed.first.pose.rotation));
    TIDE_CHECK(near(local.Observed.first.pose.angle, other_local.Observed.first.pose.angle));

    // The tagged float's ordinary interpolation crosses pi by the short route.
    const tide_view_correction reset = {.reset = true};
    tide_local_correct(&local, &reset);
    frame_on(&local, &after, &before, &after, 0.5f);
    TIDE_CHECK(near(local.Observed.first.pose.angle, TIDE_PI_F));
    TIDE_CHECK(!near(local.Observed.first.pose.angle, 0));
}

TIDE_TEST(corrections_quaternion_sign_and_repeated_noncommuting_corrections)
{
    start();
    Body b = tide_get_Body(&before, BODY);
    const tide_quaternion a = tide_axisangle_q(tide_f3(1, 0, 0), 0.8f);
    b.state.pose.rotation = a; tide_set_Body(&before, BODY, b);
    b.state.pose.rotation = (tide_quaternion){tide_neg_f4(a.value)}; tide_set_Body(&after, BODY, b);
    correct_on(&local, 1);
    TIDE_CHECK(local.tide_corrections_Body.rows.count == 0);
    b.state.pose.rotation = tide_axisangle_q(tide_f3(0, 1, 0), 0.7f); tide_set_Body(&after, BODY, b);
    correct_on(&local, 1);
    frame_on(&local, &after, NULL, &after, 1);
    TIDE_CHECK(same_rotation(local.Observed.first.pose.rotation, a));
    tide_world_copy(&before, &after);
    b.state.pose.rotation = tide_axisangle_q(tide_f3(0, 0, 1), -0.6f); tide_set_Body(&after, BODY, b);
    correct_on(&local, 1);
    frame_on(&local, &after, NULL, &after, 1);
    TIDE_CHECK(same_rotation(local.Observed.first.pose.rotation, a));
}

TIDE_TEST(corrections_large_finite_radians_reduce_without_precision_loss_or_overflow)
{
    start();
    Body b = tide_get_Body(&before, BODY);
    b.state.pose.angle = 1.0e20f;
    tide_set_Body(&before, BODY, b);
    correct_on(&local, 1);
    frame_on(&local, &after, NULL, &after, 1);
    TIDE_CHECK(near(tide_sin_f(local.Observed.first.pose.angle), tide_sin_f(1.0e20f)));
    TIDE_CHECK(near(tide_cos_f(local.Observed.first.pose.angle), tide_cos_f(1.0e20f)));
    const tide_view_correction reset = {.reset = true};
    tide_local_correct(&local, &reset);
    frame_on(&local, &after, &before, &after, 0.5f);
    const float angle = tide_atan2_f(tide_sin_f(1.0e20f), tide_cos_f(1.0e20f));
    TIDE_CHECK(near(local.Observed.first.pose.angle, angle * 0.5f));

    b.state.pose.angle = 3.0e38f;
    tide_set_Body(&before, BODY, b);
    b.state.pose.angle = -3.0e38f;
    tide_set_Body(&after, BODY, b);
    correct_on(&local, 1);
    frame_on(&local, &after, NULL, &after, 1);
    TIDE_CHECK(near(tide_sin_f(local.Observed.first.pose.angle), tide_sin_f(3.0e38f)));
    TIDE_CHECK(near(tide_cos_f(local.Observed.first.pose.angle), tide_cos_f(3.0e38f)));
    frame_on(&local, &after, &before, &after, 0.5f);
    TIDE_CHECK(tide_is_finite_f(local.Observed.first.pose.angle));
}

TIDE_TEST(corrections_clear_for_snaps_changed_identity_and_confirmed_removal)
{
    start(); scalar(&before, 20); scalar(&after, 10); correct_on(&local, 1);
    tide_entity_snap(&after.entities, BODY);
    after.Rules.snapCamera = true; tide_world_tick(&after);
    frame_on(&local, &after, NULL, &after, 1);
    TIDE_CHECK(local.Observed.first.pose.scalar == 10 && local.Observed.center.x == 10);
    TIDE_CHECK(local.tide_corrections_Body.rows.count == 0 && !local.tide_correction_Camera.valid);

    start(); scalar(&before, 20); scalar(&after, 10); correct_on(&local, 1);
    tide_entity_set_born(&after.entities, BODY, tide_entity_born(&after.entities, BODY) + 1u);
    frame_on(&local, &after, NULL, &after, 1);
    TIDE_CHECK(local.Observed.first.pose.scalar == 10 && local.tide_corrections_Body.rows.count == 0);

    start(); scalar(&before, 20); scalar(&after, 10); correct_on(&local, 1);
    tide_world_copy(&verified, &after);
    after.Rules.remove = true; tide_world_tick(&after);
    frame_on(&local, &after, NULL, &verified, 1);
    TIDE_CHECK(local.tide_corrections_Body.rows.count == 1);
    frame_on(&local, &after, NULL, &after, 1);
    TIDE_CHECK(local.tide_corrections_Body.rows.count == 0);
}

TIDE_TEST(corrections_private_cache_never_changes_world_or_local_packed_bytes)
{
    start(); scalar(&before, 20); scalar(&after, 10);
    const uint64_t hash = tide_world_hash(&after);
    const uint32_t size = tide_local_pack(&local, NULL, 0);
    uint8_t *a = malloc(size), *b = malloc(size);
    TIDE_REQUIRE(a && b);
    TIDE_REQUIRE(tide_local_pack(&local, a, size) == size);
    correct_on(&local, 1);
    TIDE_REQUIRE(local.tide_corrections_Body.rows.count == 1 && local.tide_correction_Camera.valid);
    TIDE_REQUIRE(tide_local_pack(&local, b, size) == size);
    TIDE_CHECK(memcmp(a, b, size) == 0 && tide_world_hash(&after) == hash);
    TIDE_REQUIRE(tide_local_unpack(&other_local, b, size));
    TIDE_CHECK(other_local.tide_corrections_Body.rows.count == 0 && !other_local.tide_correction_Camera.valid);
    const tide_view_correction reset = {.reset = true};
    tide_local_correct(&local, &reset);
    TIDE_CHECK(local.tide_corrections_Body.rows.count == 0 && !local.tide_correction_Camera.valid);
    free(a); free(b);
}
