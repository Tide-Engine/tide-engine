#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "tide/draw.h"
#include "tide/draw_profile.h"
#include "tide_test.h"

static const tide_vertex3 profile_vertices[] = {
    {.position = {-0.25f, -0.25f, 0}}, {.position = {0, 0.25f, 0}}, {.position = {0.25f, -0.25f, 0}},
};
static const uint32_t profile_indices[] = {0, 1, 2};
static const tide_mesh profile_triangle = {profile_vertices, 3, profile_indices, 3, 1};
static const tide_profile_source grass = {.name = "Grass", .file = "game/grass.tide", .line = 17, .column = 6};

static tide_float4x4 placed(const float x)
{
    tide_float4x4 m = tide_identity_f4x4();
    m.c3.x = x;
    return m;
}

static void camera(tide_draw_list *d)
{
    tide_draw_screen_size(d, tide_f2(400, 400));
    tide_draw_set_camera_matrices(d, tide_identity_f4x4(), tide_identity_f4x4());
}

static void triangle(tide_draw_list *d, const tide_float4x4 m)
{
    tide_draw_mesh_3d(d, &profile_triangle, m, NULL, TIDE_FILTER_POINT);
}

static void record(tide_draw_list *d)
{
    camera(d);
    d->profile_source = &grass;
    triangle(d, placed(0));
    triangle(d, placed(20)); // Header-inline stored-instance path.
    const uint32_t mesh = d->mesh_last;
    tide_float4x4 matrices[] = {placed(-0.5f), placed(20), placed(0.5f)};
    tide_draw_instances(d, mesh, matrices, 3, 0, TIDE_FILTER_POINT, TIDE_WRAP_CLAMP);
    tide_draw_set_frustum_culling(d, false);
    tide_draw_instances(d, mesh, matrices, 3, 0, TIDE_FILTER_POINT, TIDE_WRAP_CLAMP);
    tide_draw_set_frustum_culling(d, true);
    tide_float4x4 projective = placed(100);
    projective.c0.w = 1; // Conservatively kept; no usable affine test.
    triangle(d, projective);
    tide_draw_set_bounds(d, tide_f3(0, 0, 0), tide_f3(-1, 1, 1));
    triangle(d, placed(100)); // Explicit invalid bounds are also kept.
}

TIDE_TEST(draw_profile_counts_culling_and_preserves_commands)
{
    tide_draw_diagnostics p = {0};
    tide_draw_list measured = {.diagnostics = &p}, plain = {0};
    record(&measured);
    record(&plain);
    TIDE_REQUIRE(p.count == 1);
    const tide_draw_diagnostic *row = &p.rows[0];
    TIDE_CHECK(row->calls == 6 && row->candidates == 10);
    TIDE_CHECK(row->tested == 5 && row->rejected == 2);
    TIDE_CHECK(row->bypassed == 3 && row->untestable == 2 && row->submitted == 8);
    TIDE_CHECK(row->bounds_builds == 1 && row->bounds_cache_hits == 3 && row->frustum_builds == 1);
    TIDE_CHECK(row->camera_slot == 1 && row->target == 0);
    TIDE_CHECK(strcmp(row->source.name, grass.name) == 0 && strcmp(row->source.file, grass.file) == 0);
    TIDE_CHECK(row->source.line == 17 && row->source.column == 6);
    TIDE_REQUIRE(measured.count == plain.count && measured.matrix_count == plain.matrix_count);
    TIDE_CHECK(memcmp(measured.commands, plain.commands, measured.count * sizeof *measured.commands) == 0);
    TIDE_CHECK(memcmp(measured.matrices, plain.matrices, measured.matrix_count * sizeof *measured.matrices) == 0);
    TIDE_CHECK(measured.profile_active == NULL && plain.profile_active == NULL);

    tide_draw_reset(&measured);
    TIDE_CHECK(measured.diagnostics == &p && measured.profile_source == NULL);
    // The camera moves, but remains this view/destination's first camera.
    camera(&measured);
    tide_draw_set_camera_matrices(&measured, placed(1), tide_identity_f4x4());
    measured.profile_source = &grass;
    triangle(&measured, placed(1));
    TIDE_CHECK(p.count == 1 && p.rows[0].calls == 7 && p.rows[0].candidates == 11);
    TIDE_CHECK(p.rows[0].bounds_builds == 1 && p.rows[0].bounds_cache_hits == 4 && p.rows[0].frustum_builds == 2);
    tide_draw_free(&measured);
    TIDE_CHECK(p.count == 1 && p.rows[0].source.name != NULL); // The host owns the report.
    tide_draw_free(&plain);
    tide_draw_diagnostics_free(&p);
    TIDE_CHECK(p.rows == NULL && p.count == 0);
}

TIDE_TEST(draw_profile_shaded_bounds_and_batches)
{
    tide_draw_diagnostics p = {0};
    tide_draw_list d = {.diagnostics = &p};
    camera(&d);
    d.profile_source = &grass;
    bool fill;
    const uint32_t mesh = tide_draw_mesh_slot(&d, TIDE_MESH_BYTES, 1, 2, 3, &fill);
    tide_draw_mesh_fill_bytes(&d, mesh, profile_vertices, sizeof(tide_vertex3), 3, profile_indices, 3);
    tide_shader shader = {.dims = 3, .vertex_size = sizeof(tide_vertex3), .instance_floats = 4};
    tide_float4x4 matrices[] = {placed(-0.5f), placed(20), placed(0.5f)};
    tide_draw_shaded_instances(&d, &shader, mesh, matrices, 3, NULL, NULL, 0);
    // A deforming shader lacks automatic bounds. Explicit bounds permit testing.
    tide_draw_set_bounds(&d, tide_f3(0, 0, 0), tide_f3(0.5f, 0.5f, 0));
    tide_draw_shaded_instances(&d, &shader, mesh, matrices, 3, NULL, NULL, 0);
    tide_draw_unset_bounds(&d);
    shader.bounds_position = 1 + offsetof(tide_vertex3, position);
    tide_draw_shaded_instances(&d, &shader, mesh, matrices, 3, NULL, NULL, 0);
    TIDE_REQUIRE(p.count == 1);
    const tide_draw_diagnostic *row = &p.rows[0];
    TIDE_CHECK(row->calls == 3 && row->candidates == 9 && row->untestable == 3);
    TIDE_CHECK(row->tested == 6 && row->rejected == 2 && row->submitted == 7);
    TIDE_CHECK(row->bounds_builds == 1 && row->frustum_builds == 1);
    // Replaying recorded commands does not cull or count those instances again.
    tide_draw_list copy = {.diagnostics = &p, .profile_source = &grass};
    tide_draw_append(&copy, &d);
    TIDE_CHECK(row->calls == 3 && row->submitted == 7);
    tide_draw_free(&copy);
    tide_draw_free(&d);
    tide_draw_diagnostics_free(&p);
}

TIDE_TEST(draw_profile_sources_targets_and_camera_slots)
{
    tide_draw_diagnostics p = {0};
    tide_draw_list d = {.diagnostics = &p};
    char name[257];
    memset(name, 'a', sizeof name - 1u);
    name[sizeof name - 1u] = 0;
    char file[] = "game/long-view.tide";
    tide_profile_source source = {.name = name, .file = file, .line = 3, .column = 9};
    camera(&d);
    d.profile_source = &source;
    triangle(&d, placed(0));
    const uint32_t mark = tide_draw_push_camera_matrices(&d, placed(2), tide_identity_f4x4());
    triangle(&d, placed(2));
    tide_draw_pop(&d, mark);
    triangle(&d, placed(0));
    TIDE_REQUIRE(p.count == 2);
    TIDE_CHECK(p.rows[0].camera_slot == 1 && p.rows[0].calls == 2);
    TIDE_CHECK(p.rows[1].camera_slot == 2 && p.rows[1].calls == 1);
    TIDE_CHECK(strlen(p.rows[0].source.name) == 256 && p.rows[0].source.name != name);
    name[0] = 'b';
    file[0] = 'G';
    triangle(&d, placed(0)); // Same source pointer, changed contents: a new identity.
    TIDE_REQUIRE(p.count == 3);
    TIDE_CHECK(p.rows[0].source.name[0] == 'a' && p.rows[0].source.file[0] == 'g');
    TIDE_CHECK(p.rows[2].source.name[0] == 'b' && p.rows[2].source.file[0] == 'G');
    const tide_render_target target = tide_render_target_make(400, 400);
    tide_draw_render_target(&d, target);
    camera(&d);
    triangle(&d, placed(0));
    TIDE_REQUIRE(p.count == 4);
    TIDE_CHECK(p.rows[3].target != 0 && p.rows[3].camera_slot == 1);
    d.profile_source = NULL;
    triangle(&d, placed(0));
    TIDE_REQUIRE(p.count == 5);
    TIDE_CHECK(strcmp(p.rows[4].source.name, "(unattributed)") == 0 && p.rows[4].source.line == 0);
    tide_draw_diagnostics_clear(&p);
    TIDE_CHECK(p.count == 0 && p.source_bytes == 0 && p.rows != NULL);
    d.diagnostics = NULL;
    triangle(&d, placed(0));
    TIDE_CHECK(p.count == 0 && p.dropped_calls == 0);
    tide_draw_free(&d);
    tide_draw_diagnostics_free(&p);
    tide_render_target_dispose(target);
    tide_render_targets_free();
}

TIDE_TEST(draw_profile_limits_report_dropped_work)
{
    tide_draw_diagnostics p = {0};
    tide_draw_list d = {.diagnostics = &p}, plain = {0};
    tide_profile_source source = grass;
    camera(&d);
    camera(&plain);
    d.profile_source = &source;
    for (uint32_t i = 0; i <= TIDE_DRAW_PROFILE_ROWS; i++) {
        source.line = i + 1u;
        triangle(&d, placed(0));
        triangle(&plain, placed(0));
    }
    TIDE_CHECK(p.count == TIDE_DRAW_PROFILE_ROWS && p.dropped_calls == 1 && p.dropped_candidates == 1);
    TIDE_CHECK(d.matrix_count == plain.matrix_count && d.profile_active == NULL);
    TIDE_CHECK(memcmp(d.matrices, plain.matrices, d.matrix_count * sizeof *d.matrices) == 0);
    // Existing rows continue to collect after the new-row limit is reached.
    source.line = 1;
    triangle(&d, placed(0));
    TIDE_CHECK(p.rows[0].calls == 2 && p.dropped_calls == 1);
    tide_draw_diagnostics_clear(&p);
    char *large = malloc(TIDE_DRAW_PROFILE_SOURCE_BYTES + 1u);
    TIDE_REQUIRE(large != NULL);
    memset(large, 'x', TIDE_DRAW_PROFILE_SOURCE_BYTES);
    large[TIDE_DRAW_PROFILE_SOURCE_BYTES] = 0;
    source.name = large;
    triangle(&d, placed(0));
    TIDE_CHECK(p.count == 0 && p.dropped_calls == 1 && p.dropped_candidates == 1);
    free(large);
    tide_draw_free(&d);
    tide_draw_free(&plain);
    tide_draw_diagnostics_free(&p);
}
