#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

static bool matrix_near(tide_float4x4 a, tide_float4x4 b)
{
    const tide_float4 ac[] = {a.c0, a.c1, a.c2, a.c3};
    const tide_float4 bc[] = {b.c0, b.c1, b.c2, b.c3};
    for (int i = 0; i < 4; i++) {
        if (tide_abs_f(ac[i].x - bc[i].x) > 0.001f || tide_abs_f(ac[i].y - bc[i].y) > 0.001f
            || tide_abs_f(ac[i].z - bc[i].z) > 0.001f || tide_abs_f(ac[i].w - bc[i].w) > 0.001f) return false;
    }
    return true;
}

typedef struct presentation_case {
    tide_world before, after;
    tide_local local;
    tide_draw_list draw;
    tide_gui gui;
} presentation_case;

static void presentation_start(presentation_case *c)
{
    memset(c, 0, sizeof *c);
    tide_world_init(&c->before, 0.1f);
    tide_world_copy(&c->after, &c->before);
    tide_local_init(&c->local);
}

static void presentation_end(presentation_case *c)
{
    tide_world_free(&c->before);
    tide_world_free(&c->after);
    tide_local_free(&c->local);
    tide_draw_free(&c->draw);
}

static void presentation_frame(presentation_case *c, const tide_world *previous, float alpha)
{
    tide_draw_reset(&c->draw);
    tide_frame(&c->after, previous, alpha, &c->after, NULL, 1, &c->local, &c->draw, &c->gui);
}

static void presentation_correct(presentation_case *c)
{
    const tide_view_correction correction = {&c->before, NULL, &c->after, NULL, 0, 1, false, false};
    tide_local_correct(&c->local, &correction);
}

static bool rotation_near(tide_quaternion a, tide_quaternion b)
{
    return tide_abs_f(tide_dot_q(tide_normalize_q(a), tide_normalize_q(b))) > 0.99999f;
}

static void change_presentation(presentation_case *c)
{
    Transform root = tide_get_Transform(&c->before, c->before.Control.a);
    root.localRotation = tide_euler_q(tide_f3(0, 0.8f, 0));
    TIDE_REQUIRE(tide_set_Transform(&c->before, c->before.Control.a, root));
    root.localPosition.x = 4;
    root.localRotation = tide_identity_q();
    root.localScale = tide_f3(3, 3, 3);
    TIDE_REQUIRE(tide_set_Transform(&c->after, c->after.Control.a, root));
    Transform child = tide_get_Transform(&c->after, c->after.Control.b);
    child.localPosition.x = 1;
    TIDE_REQUIRE(tide_set_Transform(&c->after, c->after.Control.b, child));
}

TIDE_TEST(transform_default_corrections_preserve_position_rotation_and_hashes)
{
    presentation_case c;
    presentation_start(&c);
    change_presentation(&c);
    const uint64_t before_hash = tide_world_hash(&c.before), after_hash = tide_world_hash(&c.after);
    const uint32_t bytes = tide_local_pack(&c.local, NULL, 0);
    void *a = malloc(bytes), *b = malloc(bytes);
    TIDE_REQUIRE(a && b);
    TIDE_REQUIRE(tide_local_pack(&c.local, a, bytes) == bytes);
    presentation_correct(&c);
    TIDE_REQUIRE(tide_local_pack(&c.local, b, bytes) == bytes);
    TIDE_CHECK(memcmp(a, b, bytes) == 0); // Errors are transient presentation state.
    free(a); free(b);
    presentation_frame(&c, NULL, 1);
    TIDE_CHECK(c.local.Observed.root.x == 10 && c.local.Observed.child.x == 2);
    TIDE_CHECK(rotation_near(c.local.Observed.rotation, tide_euler_q(tide_f3(0, 0.8f, 0))));
    TIDE_CHECK(c.local.Observed.scale.x == 3 && c.local.Observed.confirmed.x == 4);
    // A binding's property uses its presented local value and stored ancestors;
    // World queries retain the existing stored-state contract.
    TIDE_CHECK(c.local.Observed.property.x == 10 && c.local.Observed.stored.x == 7);
    TIDE_CHECK(c.local.tide_corrections_Transform.rows.count == 2);
    tide_local_frame_time(&c.local, 0.075f);
    presentation_frame(&c, NULL, 1);
    TIDE_CHECK(tide_abs_f(c.local.Observed.root.x - 7) < 0.00002f);
    TIDE_CHECK(tide_abs_f(c.local.Observed.child.x - 1.5f) < 0.00002f);
    TIDE_CHECK(rotation_near(c.local.Observed.rotation, tide_euler_q(tide_f3(0, 0.4f, 0))));
    TIDE_CHECK(tide_world_hash(&c.before) == before_hash && tide_world_hash(&c.after) == after_hash);
    presentation_end(&c);
}

TIDE_TEST(transform_tick_interpolation_and_zero_error_keep_scale_and_short_rotation)
{
    presentation_case c;
    presentation_start(&c);
    Transform root = tide_get_Transform(&c.before, c.before.Control.a);
    const tide_quaternion from = tide_euler_q(tide_f3(0, 3.0f, 0));
    const tide_quaternion to = tide_euler_q(tide_f3(0, -3.0f, 0));
    root.localRotation = from;
    TIDE_REQUIRE(tide_set_Transform(&c.before, c.before.Control.a, root));
    root.localPosition.x = 14;
    root.localRotation = to;
    root.localScale = tide_f3(3, 3, 3);
    TIDE_REQUIRE(tide_set_Transform(&c.after, c.after.Control.a, root));
    const tide_view_correction correction = {&c.after, &c.before, &c.after, &c.before, 0, 0.5f, false, false};
    tide_local_correct(&c.local, &correction);
    presentation_frame(&c, &c.before, 0.5f);
    TIDE_CHECK(c.local.tide_corrections_Transform.rows.count == 0);
    TIDE_CHECK(c.local.Observed.root.x == 12 && c.local.Observed.scale.x == 2);
    TIDE_CHECK(rotation_near(c.local.Observed.rotation, tide_nlerp_q(from, to, 0.5f)));
    TIDE_CHECK(c.local.Observed.confirmed.x == 14);
    presentation_end(&c);
}

TIDE_TEST(transform_corrections_snap_reparent_reset_and_leave_local_placement_current)
{
    presentation_case c;
    presentation_start(&c);
    change_presentation(&c);
    presentation_correct(&c);
    presentation_frame(&c, NULL, 1);
    presentation_frame(&c, NULL, 1);
    TIDE_CHECK(c.local.Observed.localRoot.x == 20);
    TIDE_CHECK(c.local.tide_corrections_Transform.rows.count == 2);
    c.after.Control.action = 9; // Keep world placement, with an intentional Snap.
    tide_world_tick(&c.after);
    presentation_frame(&c, NULL, 1);
    TIDE_CHECK(c.local.Observed.child.x == 7 && c.local.Observed.stored.x == 7);
    TIDE_CHECK(c.local.Observed.root.x == 10 && c.local.tide_corrections_Transform.rows.count == 1);
    const tide_view_correction reset = {.reset = true};
    tide_local_correct(&c.local, &reset);
    presentation_frame(&c, NULL, 1);
    TIDE_CHECK(c.local.Observed.root.x == 4 && c.local.tide_corrections_Transform.rows.count == 0);
    presentation_end(&c);
}

TIDE_TEST(transform_rotation_scale_and_singular_reparent)
{
    tide_world w = {0};
    tide_world_init(&w, 0.1f);
    Transform root = tide_get_Transform(&w, w.Control.a);
    root.localRotation = tide_euler_q(tide_f3(0, 1.5707963267948966f, 0));
    root.localScale = tide_f3(2, 2, 2);
    tide_set_Transform(&w, w.Control.a, root);
    tide_world_tick(&w);
    const tide_float4x4 before = w.Control.matrix;
    const tide_float4x4 expected = tide_mul_f4x4(tide_trs_f4x4(root.localPosition, root.localRotation, root.localScale),
                                               tide_translate_f4x4(tide_f3(5, 0, 0)));
    TIDE_CHECK(matrix_near(before, expected));
    w.Control.action = 2;
    tide_world_tick(&w);
    tide_world_tick(&w);
    TIDE_CHECK(matrix_near(w.Control.matrix, before));
    TIDE_CHECK(tide_entity_is_null(w.Control.parentB));
    w.Control.action = 6;
    tide_world_tick(&w);
    tide_world_tick(&w);
    TIDE_CHECK(matrix_near(w.Control.matrix, before));
    TIDE_CHECK(tide_entity_equal(w.Control.parentB, w.Control.a));
    w.Control.action = 2;
    tide_world_tick(&w);
    root.localScale = tide_f3(0, 0, 0);
    tide_set_Transform(&w, w.Control.a, root);
    w.Control.action = 6;
    tide_world_tick(&w);
    tide_world_tick(&w);
    TIDE_CHECK(tide_entity_is_null(w.Control.parentB));
    TIDE_CHECK(matrix_near(w.Control.matrix, before));
    tide_world_free(&w);
}

TIDE_TEST(transform_deferred_parenting_and_dead_handles)
{
    tide_world w = {0};
    tide_world_init(&w, 0.1f);
    w.Control.action = 7;
    tide_world_tick(&w);
    tide_world_tick(&w);
    TIDE_CHECK(tide_entity_equal(w.Control.parent, w.Control.a));
    TIDE_CHECK(w.Control.matrix.c3.x == 13.0f);
    w.Control.action = 8;
    tide_world_tick(&w);
    TIDE_CHECK(!tide_entity_alive(&w.entities, w.Control.a));
    TIDE_CHECK(!tide_entity_alive(&w.entities, w.Control.b));
    TIDE_CHECK(!tide_entity_alive(&w.entities, w.Control.c));
    tide_world_free(&w);
}

TIDE_TEST(transform_parenting_and_local_component_placement)
{
    tide_world w = {0};
    tide_local local = {0};
    tide_draw_list draw = {0};
    tide_gui gui = {0};
    tide_world_init(&w, 0.1f);
    tide_world_tick(&w);
    TIDE_CHECK(w.Control.matrix.c3.x == 15.0f);
    TIDE_CHECK(tide_entity_equal(w.Control.parent, w.Control.b));
    TIDE_CHECK(tide_get_Transform(&w, w.Control.b).localScale.x == 1.0f);
    TIDE_CHECK(tide_get_Transform(&w, w.Control.b).localRotation.value.w == 1.0f);
    tide_local_init(&local);
    const uint64_t hash = tide_world_hash(&w);
    tide_frame(&w, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    tide_frame(&w, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    TIDE_CHECK(local.LocalControl.matrix.c3.x == 24.0f);
    TIDE_CHECK(local.LocalControl.value == 16);
    tide_frame(&w, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    TIDE_CHECK(local.LocalControl.value == 18);
    local.LocalControl.later = true;
    tide_frame(&w, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    tide_frame(&w, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    tide_frame(&w, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    local.LocalControl.detach = true;
    tide_frame(&w, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    tide_frame(&w, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    TIDE_CHECK(tide_entity_is_null(local.LocalControl.parent));
    TIDE_CHECK(local.LocalControl.matrix.c3.x == 24.0f);
    TIDE_CHECK(tide_world_hash(&w) == hash);
    TIDE_CHECK(tide_get_Marker(&w, w.Control.a).value == 7);
    tide_world_free(&w);
    tide_local_free(&local);
    tide_draw_free(&draw);
}

TIDE_TEST(transform_cycle_rejection_and_reparenting)
{
    tide_world w = {0};
    tide_world_init(&w, 0.1f);
    w.Control.action = 1;
    tide_world_tick(&w);
    tide_world_tick(&w);
    TIDE_CHECK(w.Control.matrix.c3.x == 15.0f);
    w.Control.action = 2;
    tide_world_tick(&w);
    tide_world_tick(&w);
    TIDE_CHECK(w.Control.matrix.c3.x == 15.0f);
    TIDE_CHECK(tide_get_Transform(&w, w.Control.b).localPosition.x == 12.0f);
    w.Control.action = 3;
    tide_world_tick(&w);
    tide_world_tick(&w);
    TIDE_CHECK(w.Control.matrix.c3.x == 25.0f);
    w.Control.action = 5;
    tide_world_tick(&w);
    tide_world_tick(&w);
    TIDE_CHECK(w.Control.matrix.c3.x == 25.0f);
    w.Control.action = 4;
    tide_world_tick(&w);
    TIDE_CHECK(!tide_entity_alive(&w.entities, w.Control.a));
    TIDE_CHECK(!tide_entity_alive(&w.entities, w.Control.b));
    TIDE_CHECK(!tide_entity_alive(&w.entities, w.Control.c));
    tide_world_free(&w);
}

TIDE_TEST(transform_hierarchy_snapshots_and_packed_worlds)
{
    tide_world w = {0}, snapshot = {0}, decoded = {0};
    tide_world_init(&w, 0.1f);
    tide_world_tick(&w);
    const uint64_t hash = tide_world_hash(&w);
    tide_world_copy(&snapshot, &w);
    const uint32_t bytes = tide_world_pack(&w, NULL, 0);
    void *packed = malloc(bytes);
    TIDE_REQUIRE(packed != NULL);
    TIDE_CHECK(tide_world_pack(&w, packed, bytes) == bytes);
    TIDE_REQUIRE(tide_world_unpack(&decoded, packed, bytes));
    free(packed);
    TIDE_CHECK(tide_world_hash(&decoded) == hash);
    w.Control.action = 4;
    tide_world_tick(&w);
    TIDE_CHECK(tide_world_hash(&snapshot) == hash);
    tide_world_tick(&snapshot);
    tide_world_tick(&decoded);
    TIDE_CHECK(snapshot.Control.matrix.c3.x == 15.0f);
    TIDE_CHECK(decoded.Control.matrix.c3.x == 15.0f);
    TIDE_CHECK(tide_world_hash(&decoded) == tide_world_hash(&snapshot));
    tide_world_free(&w);
    tide_world_free(&snapshot);
    tide_world_free(&decoded);
}
