#include <stddef.h>
#include <string.h>

#include "tide/draw.h"
#include "tide_test.h"

static const tide_vertex3 vertices[] = {
    {.position = {-0.25f, -0.25f, 0}}, {.position = {0, 0.25f, 0}}, {.position = {0.25f, -0.25f, 0}},
};
static const uint32_t indices[] = {0, 1, 2};
static const tide_mesh triangle = {vertices, 3, indices, 3, 1};

static void draw(tide_draw_list *d, tide_float4x4 transform)
{
    tide_draw_mesh_3d(d, &triangle, transform, NULL, TIDE_FILTER_POINT);
}

static void camera(tide_draw_list *d)
{
    tide_draw_screen_size(d, tide_f2(400, 400));
    tide_draw_set_camera_matrices(d, tide_identity_f4x4(), tide_identity_f4x4());
}

static uint32_t instances(const tide_draw_list *d)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < d->count; i++) {
        const tide_draw_command *c = &d->commands[i];
        if (c->kind == TIDE_DRAW_MESH_3D) n += c->mesh3.count;
        if (c->kind == TIDE_DRAW_SHADED) n += d->runs[c->shaded.run].count;
    }
    return n;
}

static tide_float4x4 position(float x, float y, float z)
{
    tide_float4x4 m = tide_identity_f4x4();
    m.c3 = tide_f4(x, y, z, 1);
    return m;
}

TIDE_TEST(frustum_rejects_each_plane_and_keeps_intersections)
{
    tide_draw_list d = {0};
    camera(&d);
    for (int axis = 0; axis < 3; axis++) {
        for (int side = -1; side <= 1; side += 2) {
            tide_float4x4 m = tide_identity_f4x4();
            if (axis == 0) m.c3.x = (float)side * 2;
            if (axis == 1) m.c3.y = (float)side * 2;
            if (axis == 2) m.c3.z = (float)side * 2;
            draw(&d, m);
        }
    }
    TIDE_CHECK(instances(&d) == 0);
    draw(&d, position(1.1f, 0, 0)); // Straddles the right plane
    draw(&d, position(0, 0, 1)); // Exactly on a depth plane
    draw(&d, position(0, 0, -1));
    draw(&d, position(0, 0, 0));
    TIDE_CHECK(instances(&d) == 4);
    tide_draw_free(&d);
}

TIDE_TEST(frustum_instances_keep_order_and_batch_across_rejections)
{
    tide_draw_list d = {0};
    camera(&d);
    tide_float4x4 transforms[] = {position(-0.5f, 0, 0), position(20, 0, 0), position(0, 0, 0),
                                 position(0, 0, -20), position(0.5f, 0, 0)};
    draw(&d, transforms[0]);
    const uint32_t mesh = d.commands[d.count - 1].mesh3.mesh;
    tide_draw_reset(&d);
    camera(&d);
    tide_draw_instances(&d, mesh, transforms, 5, 0, TIDE_FILTER_POINT, TIDE_WRAP_CLAMP);
    TIDE_REQUIRE(instances(&d) == 3);
    TIDE_CHECK(d.commands[d.count - 1].mesh3.count == 3);
    const uint32_t at = d.commands[d.count - 1].mesh3.first;
    TIDE_CHECK(d.matrices[at].c3.x == -0.5f && d.matrices[at + 1].c3.x == 0 && d.matrices[at + 2].c3.x == 0.5f);
    tide_draw_free(&d);
}

TIDE_TEST(frustum_transforms_shear_mirror_and_near_plane)
{
    tide_draw_list d = {0};
    camera(&d);
    tide_float4x4 shear = tide_identity_f4x4();
    shear.c1.x = 20;
    shear.c0.x = -1;
    shear.c3.x = 4; // Sheared triangle crosses the frustum despite its center being outside
    draw(&d, shear);
    tide_draw_set_transform_3d(&d, position(-20, 0, 0));
    draw(&d, position(20, 0, 0));
    TIDE_CHECK(instances(&d) == 2);
    tide_draw_unset_transform_3d(&d);
    tide_draw_set_camera_3d(&d, tide_f3(0, 0, 0), tide_identity_q(), 90.0f);
    tide_draw_cube(&d, tide_f3(0, 0, 0.01f), tide_f3(1, 1, 1), TIDE_COLOR_WHITE);
    draw(&d, position(0, 0, -10));
    draw(&d, position(0, 0, 1e8f)); // Default camera has infinite far distance
    TIDE_CHECK(instances(&d) == 4);
    tide_draw_free(&d);
}

TIDE_TEST(frustum_settings_targets_reset_and_unknown_values)
{
    tide_draw_list d = {0};
    camera(&d);
    const uint32_t mark = tide_draw_push_frustum_culling(&d, false);
    draw(&d, position(100, 0, 0));
    tide_draw_pop(&d, mark);
    draw(&d, position(100, 0, 0));
    TIDE_CHECK(instances(&d) == 1);
    const uint32_t bounds = tide_draw_push_bounds(&d, tide_f3(-100, 0, 0), tide_f3(1, 1, 1));
    draw(&d, position(100, 0, 0));
    tide_draw_pop(&d, bounds);
    draw(&d, position(100, 0, 0));
    TIDE_CHECK(instances(&d) == 2);
    const tide_render_target target = tide_render_target_make(100, 100);
    tide_draw_render_target(&d, target);
    tide_draw_set_camera_matrices(&d, position(100, 0, 0), tide_identity_f4x4());
    draw(&d, position(100, 0, 0)); // A shadow/other camera sees it
    tide_draw_set_bounds(&d, tide_f3(0, 0, 0), tide_f3(0, 0, 0));
    tide_draw_render_target(&d, (tide_render_target){.id = TIDE_RENDER_WINDOW});
    draw(&d, position(100, 0, 0));
    TIDE_CHECK(instances(&d) == 3);
    tide_draw_set_bounds(&d, tide_f3(0, 0, 0), tide_f3(-1, 1, 1)); // Invalid keeps it
    draw(&d, position(100, 0, 0));
    TIDE_CHECK(instances(&d) == 4);
    tide_draw_reset(&d);
    TIDE_CHECK(d.place.bounds.set == 0 && d.place.no_frustum_culling == 0);
    tide_draw_screen_size(&d, tide_f2(0, 0)); // A custom host has not supplied a size
    draw(&d, position(100, 0, 0));
    TIDE_CHECK(instances(&d) == 1);
    tide_draw_free(&d);
    tide_render_target_dispose(target);
    tide_render_targets_free();
}

TIDE_TEST(frustum_shader_deformation_requires_bounds_and_compacts_instances)
{
    tide_draw_list d = {0};
    camera(&d);
    bool fill;
    const uint32_t mesh = tide_draw_mesh_slot(&d, TIDE_MESH_BYTES, 1, 2, 3, &fill);
    tide_draw_mesh_fill_bytes(&d, mesh, vertices, sizeof(tide_vertex3), 3, indices, 3);
    tide_shader shader = {.dims = 3, .vertex_size = sizeof(tide_vertex3), .instance_floats = 4};
    tide_float4x4 transforms[] = {position(0, 0, 0), position(10, 0, 0), position(0.5f, 0, 0)};
    tide_draw_shaded_instances(&d, &shader, mesh, transforms, 3, NULL, NULL, 0);
    TIDE_CHECK(instances(&d) == 3); // User verts and skinning advertise no geometry bounds
    const uint32_t mark = tide_draw_push_bounds(&d, tide_f3(0, 0, 0), tide_f3(0.5f, 0.5f, 0));
    tide_draw_shaded_instances(&d, &shader, mesh, transforms, 3, NULL, NULL, 0);
    tide_draw_pop(&d, mark);
    TIDE_CHECK(instances(&d) == 5);
    shader.bounds_position = 1 + offsetof(tide_vertex3, position);
    tide_draw_shaded_instances(&d, &shader, mesh, transforms, 3, NULL, NULL, 0);
    TIDE_CHECK(instances(&d) == 7);
    // Automatic and explicit bounds produce the same kept transforms, packed without gaps.
    tide_float4x4 kept[2];
    memcpy(kept, d.instances + d.instance_used - sizeof kept, sizeof kept);
    TIDE_CHECK(kept[0].c3.x == 0 && kept[1].c3.x == 0.5f);
    tide_draw_list copy = {0};
    tide_draw_screen_size(&copy, tide_f2(10, 1000));
    tide_draw_append(&copy, &d); // Replay owns the already filtered draws; never cull twice
    TIDE_CHECK(instances(&copy) == instances(&d));
    tide_draw_free(&copy);
    tide_draw_free(&d);
}

TIDE_TEST(frustum_updates_camera_aspect_geometry_and_lod_bounds)
{
    tide_draw_list d = {0};
    camera(&d);
    tide_draw_set_camera_3d(&d, tide_f3(0, 0, 0), tide_identity_q(), 90);
    draw(&d, position(5, 0, 2));
    TIDE_CHECK(instances(&d) == 0);
    tide_draw_screen_size(&d, tide_f2(1600, 400));
    draw(&d, position(5, 0, 2));
    TIDE_CHECK(instances(&d) == 1); // Cached camera follows aspect changes
    const uint32_t mesh = d.commands[d.count - 1].mesh3.mesh;
    const tide_mesh_levels levels = {.center = {100, 0, 0}, .radius = 0.01f, .count = 1,
        .error = {1}, .indices = {3}, .index = indices, .index_count = 3};
    tide_draw_mesh_levels(&d, mesh, &levels); // LOD's supplied sphere must not replace geometry culling bounds
    tide_draw_set_lod(&d, 1);
    const tide_float4x4 transforms[] = {position(0, 0, 2), position(100, 0, 2), position(0, 0, 3)};
    tide_draw_instances(&d, mesh, transforms, 3, 0, TIDE_FILTER_POINT, TIDE_WRAP_CLAMP);
    TIDE_CHECK(instances(&d) == 3);
    tide_draw_prepare(&d, NULL);
    TIDE_CHECK(instances(&d) == 3);
    // A changed version has a new owned geometry box, even at the same addresses.
    tide_vertex3 changed[3];
    memcpy(changed, vertices, sizeof changed);
    tide_mesh editable = {changed, 3, indices, 3, 1};
    tide_draw_mesh_3d(&d, &editable, position(100, 0, 2), NULL, TIDE_FILTER_POINT);
    TIDE_CHECK(instances(&d) == 3);
    for (int i = 0; i < 3; i++) changed[i].position.x -= 100;
    editable.version++;
    tide_draw_mesh_3d(&d, &editable, position(100, 0, 2), NULL, TIDE_FILTER_POINT);
    TIDE_CHECK(instances(&d) == 4);
    tide_draw_free(&d);
}

TIDE_TEST(frustum_uncertain_values_keep_draws_and_defaults_restore_all_destinations)
{
    tide_draw_list d = {0};
    camera(&d);
    tide_float4x4 nonfinite = position(100, 0, 0);
    nonfinite.c0.x = tide_f_from_bits(0x7f800000u);
    draw(&d, nonfinite);
    tide_float4x4 projective = position(100, 0, 0);
    projective.c0.w = 1;
    draw(&d, projective);
    tide_draw_set_bounds(&d, tide_f3(tide_f_from_bits(0x7fc00000u), 0, 0), tide_f3(1, 1, 1));
    draw(&d, position(100, 0, 0));
    TIDE_CHECK(instances(&d) == 3);
    tide_draw_set_bounds(&d, tide_f3(0, 0, 0), tide_f3(3e38f, 3e38f, 3e38f));
    draw(&d, position(100, 0, 0));
    TIDE_CHECK(instances(&d) == 4); // Large finite bounds do not overflow the rejection test
    const uint32_t mark = tide_draw_push_no_bounds(&d);
    draw(&d, position(100, 0, 0));
    TIDE_CHECK(instances(&d) == 4);
    tide_draw_pop(&d, mark);
    const tide_render_target target = tide_render_target_make(100, 100);
    tide_draw_render_target(&d, target);
    tide_draw_set_frustum_culling(&d, false);
    tide_draw_set_bounds(&d, tide_f3(0, 0, 0), tide_f3(1, 1, 1));
    tide_draw_defaults(&d);
    TIDE_CHECK(d.place.bounds.set == 0 && d.place.no_frustum_culling == 0);
    tide_draw_render_target(&d, target);
    TIDE_CHECK(d.place.bounds.set == 0 && d.place.no_frustum_culling == 0);
    tide_draw_free(&d);
    tide_render_target_dispose(target);
    tide_render_targets_free();
}
