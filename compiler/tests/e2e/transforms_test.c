#include <stdlib.h>

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
