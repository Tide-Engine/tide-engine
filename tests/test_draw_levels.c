// A mesh's levels of detail in the draw list (tide_draw_mesh_levels): which
// one an instance draws at, by how big the mesh is to the camera or as a
// setting says, that a level is a mesh with the full one's vertices, and that
// the instances of a level end up one run unless blending needs their order.

#include <math.h>
#include <string.h>

#include "tide/draw.h"
#include "tide_test.h"

// Four triangles over four corners, and two simpler ways of drawing them: two
// triangles, off by a hundredth of a unit, and one, off by a tenth.
static const tide_vertex3 corners[4] = {
    {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
    {{1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
    {{1.0f, 1.0f, 0.0f}, {1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
    {{0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
};
static const uint32_t whole[12] = {0, 1, 2, 0, 2, 3, 0, 1, 3, 1, 2, 3};
static const uint32_t simpler[9] = {0, 1, 2, 0, 2, 3, 0, 1, 2};
static const tide_mesh_levels two_levels = {
    .center = {0.5f, 0.5f, 0.0f}, .radius = 0.75f, .count = 2, .error = {0.01f, 0.1f}, .first = {0, 6}, .indices = {6, 3},
    .index = simpler, .index_count = 9};

static uint32_t leveled_mesh(tide_draw_list *d)
{
    bool fill;
    const uint32_t mesh = tide_draw_mesh_slot(d, TIDE_MESH_MEMORY, 41, 42, 1, &fill);
    if (fill) {
        tide_draw_mesh_fill(d, mesh, corners, 4, whole, 12);
        tide_draw_mesh_levels(d, mesh, &two_levels);
    }
    return mesh;
}

static uint32_t plain_mesh(tide_draw_list *d)
{
    bool fill;
    const uint32_t mesh = tide_draw_mesh_slot(d, TIDE_MESH_MEMORY, 51, 52, 1, &fill);
    if (fill) tide_draw_mesh_fill(d, mesh, corners, 4, whole, 12);
    return mesh;
}

// A window 600 pixels high, seen through a camera at the origin looking along z with 60 degrees of view: a
// unit of the world, a unit away, is 300 / tan(30 degrees) pixels, about 519.6. So the first level (off by
// 0.01) is within a pixel from 5.196 units off, and the second (off by 0.1) from 51.96. The mesh's ball
// comes 0.75 nearer than its center.
static void frame(tide_draw_list *d)
{
    tide_draw_reset(d);
    // These tests use offscreen x positions as ordering markers and inspect every submitted level.
    tide_draw_set_frustum_culling(d, false);
    tide_draw_screen_size(d, tide_f2(800.0f, 600.0f));
    tide_draw_set_camera_3d(d, tide_f3(0.0f, 0.0f, 0.0f), tide_identity_q(), 60.0f);
}

static tide_float4x4 at(const float x, const float z)
{
    return tide_translate_f4x4(tide_f3(x - 0.5f, -0.5f, z));
}

static const tide_draw_mesh_data *mesh_of(const tide_draw_list *d, const tide_draw_command *c)
{
    return &d->meshes[c->mesh3.mesh - 1u];
}

// The runs of 3D meshes the list has that draw something, in order.
static uint32_t runs_of(const tide_draw_list *d, const tide_draw_command *out[], const uint32_t most)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < d->count; i++) {
        const tide_draw_command *c = &d->commands[i];
        if (c->kind == TIDE_DRAW_MESH_3D && c->mesh3.mesh && c->mesh3.count && count < most) out[count++] = c;
    }
    return count;
}

TIDE_TEST(draw_levels_are_picked_by_how_big_a_mesh_is)
{
    tide_draw_list d = {0};
    frame(&d);
    const uint32_t mesh = leveled_mesh(&d);
    TIDE_REQUIRE(mesh && d.meshes[mesh - 1u].levels == 2);
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, at(0.0f, 2.0f)) == 0);
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, at(0.0f, 5.9f)) == 0);  // 5.15 off at its nearest
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, at(0.0f, 6.0f)) == 1);  // 5.25
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, at(0.0f, 52.6f)) == 1); // 51.85
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, at(0.0f, 52.8f)) == 2); // 52.05
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, at(0.0f, 1.0e6f)) == 2);
    // Around the camera and behind it, it draws whole
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, at(0.0f, 0.5f)) == 0);
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, at(0.0f, -100.0f)) == 0);
    // Ten times as big is ten times as far off, and a ball ten times as big
    const tide_float4x4 big = tide_trs_f4x4(tide_f3(0.0f, 0.0f, 100.0f), tide_identity_q(), tide_f3(10.0f, 10.0f, 10.0f));
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, big) == 1);
    // ...by the way it's made biggest
    const tide_float4x4 long_one = tide_trs_f4x4(tide_f3(0.0f, 0.0f, 100.0f), tide_identity_q(), tide_f3(1.0f, 10.0f, 1.0f));
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, long_one) == 1);
    // The destination's own transform is part of where an instance is
    tide_draw_set_transform_3d(&d, tide_translate_f4x4(tide_f3(0.0f, 0.0f, 60.0f)));
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, at(0.0f, 2.0f)) == 2);
    tide_draw_unset_transform_3d(&d);
    // Four pixels of leeway are four times nearer
    tide_draw_set_lod_pixels(&d, 4.0f);
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, at(0.0f, 2.0f)) == 0);
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, at(0.0f, 2.1f)) == 1);
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, at(0.0f, 14.0f)) == 2);
    tide_draw_set_lod_pixels(&d, -1.0f);
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, at(0.0f, 14.0f)) == 1);
    // A mesh with none, and a transform that's no number, draw whole
    TIDE_CHECK(tide_draw_mesh_level(&d, plain_mesh(&d), at(0.0f, 100.0f)) == 0);
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, at(0.0f, NAN)) == 0);
    TIDE_CHECK(tide_draw_mesh_level(&d, 0, at(0.0f, 100.0f)) == 0 && tide_draw_mesh_level(&d, 999, at(0.0f, 100.0f)) == 0);
    // With no window's size said, nothing is picked
    tide_draw_screen_size(&d, tide_f2(0.0f, 0.0f));
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, at(0.0f, 100.0f)) == 0);
    tide_draw_free(&d);
}

// The tolerance follows the selected destination and ordinary scoped restoration.
TIDE_TEST(draw_levels_asset_multiplier_composes_with_destination_tolerance)
{
    tide_draw_list d = {0};
    frame(&d);
    const uint32_t mesh = leveled_mesh(&d);
    tide_mesh_levels levels = two_levels;
    levels.multiplier = 2.0f;
    tide_draw_mesh_levels(&d, mesh, &levels);
    const tide_float4x4 transform = at(0.0f, 8.0f);
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, transform) == 1);
    tide_draw_instance(&d, mesh, transform, 0, TIDE_FILTER_BILINEAR, TIDE_WRAP_CLAMP);
    tide_draw_set_lod_pixels(&d, 4.0f);
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, transform) == 2); // Effective 8 pixels.
    tide_draw_instance(&d, mesh, transform, 0, TIDE_FILTER_BILINEAR, TIDE_WRAP_CLAMP);
    const tide_draw_command *runs[3];
    TIDE_REQUIRE(runs_of(&d, runs, 3) == 2);
    TIDE_CHECK(mesh_of(&d, runs[0])->level == 1 && mesh_of(&d, runs[1])->level == 2);
    levels.multiplier = 1.0f;
    tide_draw_mesh_levels(&d, mesh, &levels);
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, transform) == 1); // Draw's 4 alone is insufficient.
    levels.multiplier = 0.25f;
    tide_draw_mesh_levels(&d, mesh, &levels);
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, transform) == 1); // Effective 1 pixel.
    tide_draw_unset_lod_pixels(&d);
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, transform) == 0);
    tide_draw_set_lod(&d, 2);
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, transform) == 2);
    tide_draw_unset_lod(&d);
    const float invalid[] = {0.0f, -1.0f, NAN, INFINITY};
    for (uint32_t i = 0; i < sizeof invalid / sizeof invalid[0]; i++) {
        levels.multiplier = invalid[i];
        tide_draw_mesh_levels(&d, mesh, &levels);
        TIDE_CHECK(d.meshes[mesh - 1u].lod_multiplier == 1.0f);
        TIDE_CHECK(tide_draw_mesh_level(&d, mesh, transform) == 1);
    }
    tide_draw_free(&d);
}

TIDE_TEST(draw_levels_pixel_tolerance_is_scoped_and_destination_local)
{
    tide_draw_list d = {0};
    frame(&d);
    const uint32_t mesh = leveled_mesh(&d);
    const tide_float4x4 transform = at(0.0f, 14.0f);
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, transform) == 1);
    tide_draw_set_lod_pixels(&d, 4.0f);
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, transform) == 2);
    const uint32_t outer = tide_draw_push_lod_pixels(&d, 0.25f);
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, transform) == 0);
    const uint32_t inner = tide_draw_push_no_lod_pixels(&d);
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, transform) == 1);
    tide_draw_pop(&d, inner);
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, transform) == 0);
    tide_draw_pop(&d, outer);
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, transform) == 2);
    // A target with the same picking camera and pixels keeps its own tolerance.
    const uint64_t target = tide_render_target_make(800.0f, 600.0f).id;
    tide_draw_to(&d, target, 800.0f, 600.0f);
    tide_draw_set_lod_camera_3d(&d, tide_f3(0.0f, 0.0f, 0.0f), tide_identity_q(), 60.0f);
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, transform) == 1);
    tide_draw_set_lod_pixels(&d, 0.25f);
    const uint32_t target_mark = tide_draw_push_lod_pixels(&d, 100.0f);
    tide_draw_to(&d, 0, 0, 0);
    tide_draw_pop(&d, target_mark); // Restore the target even while the window is selected.
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, transform) == 2);
    tide_draw_to(&d, target, 800.0f, 600.0f);
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, transform) == 0);
    tide_draw_set_lod(&d, 2);
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, transform) == 2);
    tide_draw_unset_lod(&d);
    const float invalid[] = {0.0f, -1.0f, NAN, INFINITY, -INFINITY};
    for (uint32_t i = 0; i < sizeof invalid / sizeof invalid[0]; i++) {
        tide_draw_set_lod_pixels(&d, invalid[i]);
        TIDE_CHECK(tide_draw_mesh_level(&d, mesh, transform) == 1);
    }
    tide_draw_set_lod_pixels(&d, 4.0f);
    tide_draw_defaults(&d);
    TIDE_CHECK(d.place.lod_pixels == 0.0f && d.targets[0].place.lod_pixels == 0.0f);
    tide_draw_set_lod_pixels(&d, 4.0f);
    frame(&d);
    TIDE_CHECK(d.place.lod_pixels == 0.0f);
    tide_draw_free(&d);
}

// A level is a mesh of the list's that has triangles and no corners: the full
// mesh's are its own, and a frame that draws a level keeps the full mesh.
TIDE_TEST(draw_levels_are_meshes_with_the_full_meshes_vertices)
{
    tide_draw_list d = {0};
    frame(&d);
    const uint32_t mesh = leveled_mesh(&d);
    tide_draw_instance(&d, mesh, at(0.0f, 2.0f), 0, TIDE_FILTER_BILINEAR, TIDE_WRAP_CLAMP);
    tide_draw_instance(&d, mesh, at(0.0f, 10.0f), 0, TIDE_FILTER_BILINEAR, TIDE_WRAP_CLAMP);
    tide_draw_instance(&d, mesh, at(0.0f, 100.0f), 0, TIDE_FILTER_BILINEAR, TIDE_WRAP_CLAMP);
    const tide_draw_command *runs[4];
    TIDE_REQUIRE(runs_of(&d, runs, 4) == 3);
    TIDE_CHECK(runs[0]->mesh3.mesh == mesh && mesh_of(&d, runs[0])->base == 0 && mesh_of(&d, runs[0])->index_count == 12);
    for (uint32_t level = 1; level <= 2; level++) {
        const tide_draw_mesh_data *m = mesh_of(&d, runs[level]);
        TIDE_CHECK(m->base == mesh && m->level == level && m->vertices == NULL && m->vertex_count == 4);
        TIDE_CHECK(m->vertex_size == sizeof(tide_vertex3) && m->space == TIDE_MESH_MEMORY && m->levels == 0);
        TIDE_CHECK(m->index_count == two_levels.indices[level - 1u]);
        TIDE_CHECK(memcmp(m->indices, simpler + two_levels.first[level - 1u], m->index_count * sizeof(uint32_t)) == 0);
    }
    const uint32_t first = runs[1]->mesh3.mesh, second = runs[2]->mesh3.mesh;
    // The next frame draws the second level alone: it has its place still, and so has the full mesh, and the
    // first level's is free
    frame(&d);
    TIDE_REQUIRE(leveled_mesh(&d) == mesh && d.meshes[mesh - 1u].levels == 2);
    tide_draw_instance(&d, mesh, at(0.0f, 100.0f), 0, TIDE_FILTER_BILINEAR, TIDE_WRAP_CLAMP);
    TIDE_REQUIRE(runs_of(&d, runs, 4) == 1);
    TIDE_CHECK(runs[0]->mesh3.mesh == second);
    frame(&d);
    TIDE_CHECK(d.meshes[first - 1u].space == TIDE_MESH_NONE && d.meshes[second - 1u].space != TIDE_MESH_NONE);
    // ...and a frame that drew a level alone still kept the mesh it's a level of
    const uint32_t again = leveled_mesh(&d);
    TIDE_CHECK(again == mesh && d.meshes[mesh - 1u].vertices != NULL && d.meshes[mesh - 1u].levels == 2);
    tide_draw_instance(&d, mesh, at(0.0f, 10.0f), 0, TIDE_FILTER_BILINEAR, TIDE_WRAP_CLAMP);
    TIDE_REQUIRE(runs_of(&d, runs, 4) == 1);
    TIDE_CHECK(mesh_of(&d, runs[0])->base == mesh && mesh_of(&d, runs[0])->level == 1 && mesh_of(&d, runs[0])->index_count == 6);
    tide_draw_free(&d);
}

// What levels a mesh is given are checked as its own triangles are: one past
// the corners is left out, and a level with nothing left, or off by less
// than the one before it, ends them.
TIDE_TEST(draw_levels_are_checked_as_they_are_given)
{
    tide_draw_list d = {0};
    frame(&d);
    const uint32_t mesh = leveled_mesh(&d);
    static const uint32_t spoiled[12] = {0, 1, 2, 0, 2, 9, 0, 1, 2, 7, 8, 9};
    tide_mesh_levels given = {.radius = 1.0f, .count = 3, .error = {0.1f, 0.2f, 0.3f}, .first = {0, 6, 9}, .indices = {6, 3, 3},
                              .index = spoiled, .index_count = 12};
    tide_draw_mesh_levels(&d, mesh, &given);
    const tide_draw_mesh_data *m = &d.meshes[mesh - 1u];
    TIDE_CHECK(m->levels == 2 && m->level_count[0] == 3 && m->level_count[1] == 3 && m->level_index_count == 6);
    given.error[1] = 0.05f; // Off by less than the level before
    tide_draw_mesh_levels(&d, mesh, &given);
    TIDE_CHECK(d.meshes[mesh - 1u].levels == 1);
    given.error[0] = NAN;
    tide_draw_mesh_levels(&d, mesh, &given);
    TIDE_CHECK(d.meshes[mesh - 1u].levels == 0);
    given.error[0] = 0.1f;
    given.first[0] = 10, given.indices[0] = 6; // Past the indices
    tide_draw_mesh_levels(&d, mesh, &given);
    TIDE_CHECK(d.meshes[mesh - 1u].levels == 0);
    tide_draw_mesh_levels(&d, mesh, NULL);
    TIDE_CHECK(d.meshes[mesh - 1u].levels == 0);
    tide_draw_mesh_levels(&d, 0, &two_levels);
    tide_draw_mesh_levels(&d, 999, &two_levels);
    // With none, it draws whole wherever it is
    tide_draw_instance(&d, mesh, at(0.0f, 1000.0f), 0, TIDE_FILTER_BILINEAR, TIDE_WRAP_CLAMP);
    TIDE_CHECK(d.commands[d.count - 1u].mesh3.mesh == mesh);
    tide_draw_free(&d);
}

// The instances of one draw are a run a level, the finest first, each in the
// order the draw gave them.
TIDE_TEST(draw_levels_group_the_instances_of_a_draw)
{
    tide_draw_list d = {0};
    frame(&d);
    const uint32_t mesh = leveled_mesh(&d);
    const tide_float4x4 given[6] = {at(1.0f, 100.0f), at(2.0f, 2.0f), at(3.0f, 10.0f), at(4.0f, 100.0f), at(5.0f, 2.0f), at(6.0f, 100.0f)};
    tide_draw_instances(&d, mesh, given, 6, 0, TIDE_FILTER_BILINEAR, TIDE_WRAP_CLAMP);
    const tide_draw_command *runs[8];
    TIDE_REQUIRE(runs_of(&d, runs, 8) == 3);
    static const uint32_t counts[3] = {2, 1, 3};
    static const float order[6] = {2.0f, 5.0f, 3.0f, 1.0f, 4.0f, 6.0f};
    uint32_t next = 0;
    for (uint32_t level = 0; level < 3; level++) {
        TIDE_CHECK(mesh_of(&d, runs[level])->level == level && runs[level]->mesh3.count == counts[level]);
        for (uint32_t i = 0; i < runs[level]->mesh3.count; i++) {
            TIDE_CHECK(d.matrices[runs[level]->mesh3.first + i].c3.x == order[next++] - 0.5f);
        }
    }
    // All at one level, they're as they were given
    frame(&d);
    const tide_float4x4 far[3] = {at(1.0f, 100.0f), at(2.0f, 200.0f), at(3.0f, 300.0f)};
    tide_draw_instances(&d, leveled_mesh(&d), far, 3, 0, TIDE_FILTER_BILINEAR, TIDE_WRAP_CLAMP);
    TIDE_REQUIRE(runs_of(&d, runs, 8) == 1);
    TIDE_CHECK(mesh_of(&d, runs[0])->level == 2 && runs[0]->mesh3.count == 3);
    TIDE_CHECK(d.matrices[runs[0]->mesh3.first].c3.x == 0.5f && d.matrices[runs[0]->mesh3.first + 2u].c3.x == 2.5f);
    // A mesh with no levels is one run
    frame(&d);
    tide_draw_instances(&d, plain_mesh(&d), given, 6, 0, TIDE_FILTER_BILINEAR, TIDE_WRAP_CLAMP);
    TIDE_REQUIRE(runs_of(&d, runs, 8) == 1);
    TIDE_CHECK(runs[0]->mesh3.count == 6 && d.matrices[runs[0]->mesh3.first].c3.x == 0.5f && !d.leveled);
    tide_draw_free(&d);
}

// Draw.LOD: every mesh at a level, whatever its size. A number past a mesh's
// last level is its last, one under 0 is the full mesh, and a mesh with none
// isn't touched. It's a destination's own, and a block puts back what it found.
TIDE_TEST(draw_levels_are_named_by_a_setting)
{
    tide_draw_list d = {0};
    frame(&d);
    const uint32_t mesh = leveled_mesh(&d);
    tide_draw_set_lod(&d, 1);
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, at(0.0f, 2.0f)) == 1 && tide_draw_mesh_level(&d, mesh, at(0.0f, 1000.0f)) == 1);
    tide_draw_set_lod(&d, 0);
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, at(0.0f, 1000.0f)) == 0);
    tide_draw_set_lod(&d, 40);
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, at(0.0f, 2.0f)) == 2);
    tide_draw_set_lod(&d, -5);
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, at(0.0f, 1000.0f)) == 0);
    tide_draw_set_lod(&d, 2);
    TIDE_CHECK(tide_draw_mesh_level(&d, plain_mesh(&d), at(0.0f, 2.0f)) == 0);
    // It needs no camera and no window's size
    tide_draw_screen_size(&d, tide_f2(0.0f, 0.0f));
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, at(0.0f, 2.0f)) == 2);
    tide_draw_screen_size(&d, tide_f2(800.0f, 600.0f));
    // Every instance of a draw has it, so the draw is one run in the order it was given
    const tide_float4x4 given[3] = {at(1.0f, 100.0f), at(2.0f, 2.0f), at(3.0f, 10.0f)};
    tide_draw_instances(&d, mesh, given, 3, 0, TIDE_FILTER_BILINEAR, TIDE_WRAP_CLAMP);
    const tide_draw_command *runs[4];
    TIDE_REQUIRE(runs_of(&d, runs, 4) == 1);
    TIDE_CHECK(mesh_of(&d, runs[0])->level == 2 && runs[0]->mesh3.count == 3 && d.matrices[runs[0]->mesh3.first + 1u].c3.x == 1.5f);
    // Blocks, and going back to picking
    tide_draw_unset_lod(&d);
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, at(0.0f, 2.0f)) == 0);
    const uint32_t outer = tide_draw_push_lod(&d, 1);
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, at(0.0f, 2.0f)) == 1);
    const uint32_t inner = tide_draw_push_no_lod(&d);
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, at(0.0f, 2.0f)) == 0 && tide_draw_mesh_level(&d, mesh, at(0.0f, 100.0f)) == 2);
    tide_draw_pop(&d, inner);
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, at(0.0f, 100.0f)) == 1);
    // A target has its own, and the window's isn't its
    tide_draw_to(&d, tide_render_target_make(100.0f, 100.0f).id, 100.0f, 100.0f);
    TIDE_CHECK(d.place.lod == 0);
    tide_draw_set_lod(&d, 2);
    tide_draw_to(&d, 0, 0.0f, 0.0f);
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, at(0.0f, 2.0f)) == 1);
    tide_draw_pop(&d, outer);
    TIDE_CHECK(d.place.lod == 0);
    // ...and a frame starts with none
    tide_draw_set_lod(&d, 1);
    frame(&d);
    TIDE_CHECK(d.place.lod == 0 && d.place.lod_camera == 0);
    tide_draw_free(&d);
}

// A target picks by its own camera and its own pixels, unless it's told a
// camera to pick through, which is then taken as the window's.
TIDE_TEST(draw_levels_are_picked_through_another_camera_when_told)
{
    tide_draw_list d = {0};
    frame(&d);
    const uint32_t mesh = leveled_mesh(&d);
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, at(0.0f, 2.0f)) == 0);
    // A target a sixth of the window's height, through the same kind of camera: a unit is a sixth of the pixels,
    // so the first level is within one from 0.866 units off
    const uint64_t small = tide_render_target_make(100.0f, 100.0f).id;
    tide_draw_to(&d, small, 100.0f, 100.0f);
    tide_draw_set_frustum_culling(&d, false); // Inspect LOD submission even when this camera looks elsewhere
    tide_draw_set_camera_3d(&d, tide_f3(0.0f, 0.0f, 0.0f), tide_identity_q(), 60.0f);
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, at(0.0f, 2.0f)) == 1);
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, at(0.0f, 9.0f)) == 1 && tide_draw_mesh_level(&d, mesh, at(0.0f, 10.0f)) == 2);
    // Told the window's camera, it picks what the window shows
    tide_draw_set_lod_camera_3d(&d, tide_f3(0.0f, 0.0f, 0.0f), tide_identity_q(), 60.0f);
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, at(0.0f, 2.0f)) == 0 && tide_draw_mesh_level(&d, mesh, at(0.0f, 10.0f)) == 1);
    // ...wherever its own camera looks from: the draws still go through that one
    tide_draw_set_camera_3d(&d, tide_f3(0.0f, 500.0f, 0.0f), tide_identity_q(), 20.0f);
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, at(0.0f, 10.0f)) == 1 && d.place.camera_3d != d.place.lod_camera);
    tide_draw_instance(&d, mesh, at(0.0f, 10.0f), 0, TIDE_FILTER_BILINEAR, TIDE_WRAP_CLAMP);
    TIDE_CHECK(d.place.played_3d == d.place.camera_3d);
    // A camera from somewhere else picks by where it is
    tide_draw_set_lod_camera_3d(&d, tide_f3(0.0f, 0.0f, -90.0f), tide_identity_q(), 60.0f);
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, at(0.0f, 10.0f)) == 2);
    // Given as matrices, the same
    tide_draw_set_lod_camera_matrices(&d, tide_draw_camera_transform(&d, tide_f2(800.0f, 600.0f)),
                                      tide_draw_camera_projection(&d, tide_f2(800.0f, 600.0f)));
    const uint32_t own = tide_draw_mesh_level(&d, mesh, at(0.0f, 10.0f));
    tide_draw_unset_lod_camera(&d);
    TIDE_CHECK(d.place.lod_camera == 0);
    // Blocks put back what they found, and a named level wins over a named camera
    const uint32_t mark = tide_draw_push_lod_camera_3d(&d, tide_f3(0.0f, 0.0f, -90.0f), tide_identity_q(), 60.0f);
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, at(0.0f, 10.0f)) == 2);
    const uint32_t none = tide_draw_push_no_lod_camera(&d);
    TIDE_CHECK(d.place.lod_camera == 0);
    tide_draw_pop(&d, none);
    TIDE_CHECK(d.place.lod_camera != 0);
    tide_draw_set_lod(&d, 0);
    TIDE_CHECK(tide_draw_mesh_level(&d, mesh, at(0.0f, 10.0f)) == 0);
    tide_draw_unset_lod(&d);
    tide_draw_pop(&d, mark);
    TIDE_CHECK(d.place.lod_camera == 0);
    (void)own;
    // The window's own is untouched by a target's
    tide_draw_to(&d, 0, 0.0f, 0.0f);
    TIDE_CHECK(d.place.lod_camera == 0 && tide_draw_mesh_level(&d, mesh, at(0.0f, 2.0f)) == 0);
    tide_draw_free(&d);
}

// A view that runs for each entity draws an instance at a time, each at its
// own level: runs that tide_draw_gather_levels makes one a level, the finest
// first, with the instances of each in the order they were drawn.
TIDE_TEST(draw_levels_of_draws_in_a_row_are_gathered)
{
    tide_draw_list d = {0};
    frame(&d);
    const uint32_t mesh = leveled_mesh(&d);
    static const float depth[7] = {2.0f, 100.0f, 2.0f, 10.0f, 100.0f, 2.0f, 10.0f};
    for (uint32_t i = 0; i < 7; i++) {
        tide_draw_instance(&d, mesh, at((float)(i + 1u), depth[i]), 0, TIDE_FILTER_BILINEAR, TIDE_WRAP_CLAMP);
    }
    const tide_draw_command *runs[16];
    TIDE_CHECK(runs_of(&d, runs, 16) == 7 && d.leveled);
    tide_draw_prepare(&d, NULL);
    TIDE_REQUIRE(runs_of(&d, runs, 16) == 3);
    static const uint32_t counts[3] = {3, 2, 2};
    static const float order[7] = {1.0f, 3.0f, 6.0f, 4.0f, 7.0f, 2.0f, 5.0f};
    uint32_t next = 0;
    for (uint32_t level = 0; level < 3; level++) {
        TIDE_CHECK(mesh_of(&d, runs[level])->level == level && runs[level]->mesh3.count == counts[level]);
        for (uint32_t i = 0; i < runs[level]->mesh3.count; i++) {
            TIDE_CHECK(d.matrices[runs[level]->mesh3.first + i].c3.x == order[next++] - 0.5f);
        }
    }
    // Again changes nothing
    tide_draw_gather_levels(&d);
    TIDE_CHECK(runs_of(&d, runs, 16) == 3 && runs[2]->mesh3.count == 2);
    // What's drawn otherwise isn't gathered with them: another tint, another mesh between
    frame(&d);
    const uint32_t same = leveled_mesh(&d);
    tide_draw_instance(&d, same, at(1.0f, 100.0f), 0, TIDE_FILTER_BILINEAR, TIDE_WRAP_CLAMP);
    tide_draw_instance(&d, same, at(2.0f, 2.0f), 0, TIDE_FILTER_BILINEAR, TIDE_WRAP_CLAMP);
    tide_draw_set_tint(&d, TIDE_COLOR_RED);
    tide_draw_instance(&d, same, at(3.0f, 100.0f), 0, TIDE_FILTER_BILINEAR, TIDE_WRAP_CLAMP);
    tide_draw_unset_tint(&d);
    tide_draw_instance(&d, plain_mesh(&d), at(4.0f, 2.0f), 0, TIDE_FILTER_BILINEAR, TIDE_WRAP_CLAMP);
    tide_draw_instance(&d, same, at(5.0f, 100.0f), 0, TIDE_FILTER_BILINEAR, TIDE_WRAP_CLAMP);
    tide_draw_prepare(&d, NULL);
    TIDE_REQUIRE(runs_of(&d, runs, 16) == 5);
    // The first two changed places, the finest first; the rest are where they were
    TIDE_CHECK(mesh_of(&d, runs[0])->level == 0 && d.matrices[runs[0]->mesh3.first].c3.x == 1.5f);
    TIDE_CHECK(mesh_of(&d, runs[1])->level == 2 && d.matrices[runs[1]->mesh3.first].c3.x == 0.5f);
    TIDE_CHECK(mesh_of(&d, runs[2])->level == 2 && d.matrices[runs[2]->mesh3.first].c3.x == 2.5f);
    TIDE_CHECK(mesh_of(&d, runs[3])->levels == 0 && mesh_of(&d, runs[3])->base == 0);
    TIDE_CHECK(mesh_of(&d, runs[4])->level == 2 && d.matrices[runs[4]->mesh3.first].c3.x == 4.5f);
    tide_draw_free(&d);
}

// The same through a shader of the game's: each instance's transform starts
// its bytes, and the rest of them go with it.
TIDE_TEST(draw_levels_of_shaded_instances_are_picked_and_gathered)
{
    static const tide_shader shader = {.id = 7, .dims = 3, .vertex_size = 32, .instance_floats = 5};
    typedef struct instance {
        tide_float4x4 transform;
        float tag[4];
    } instance;
    static const float bytes[4 * 8] = {0};
    tide_draw_list d = {0};
    frame(&d);
    bool fill;
    const uint32_t mesh = tide_draw_mesh_slot(&d, TIDE_MESH_BYTES, 61, 62, 1, &fill);
    TIDE_REQUIRE(fill);
    tide_draw_mesh_fill_bytes(&d, mesh, bytes, 32, 4, whole, 12);
    tide_draw_mesh_levels(&d, mesh, &two_levels);
    static const float depth[5] = {100.0f, 2.0f, 10.0f, 100.0f, 2.0f};
    instance given[5];
    for (uint32_t i = 0; i < 5; i++) given[i] = (instance){.transform = at(0.0f, depth[i]), .tag = {(float)(i + 1u)}};
    tide_draw_shaded_instances(&d, &shader, mesh, given, 5, NULL, NULL, 0);
    TIDE_REQUIRE(d.run_count == 3);
    static const uint32_t counts[3] = {2, 1, 2};
    static const float order[5] = {2.0f, 5.0f, 3.0f, 1.0f, 4.0f};
    uint32_t next = 0;
    for (uint32_t level = 0; level < 3; level++) {
        const tide_draw_run *run = &d.runs[level];
        TIDE_CHECK(d.meshes[run->mesh - 1u].level == level && run->count == counts[level]);
        for (uint32_t i = 0; i < run->count; i++) {
            instance got;
            memcpy(&got, d.instances + run->first + i * sizeof got, sizeof got);
            TIDE_CHECK(got.tag[0] == order[next++]);
        }
    }
    // One at a time, as a view that runs for each entity draws them, then gathered
    frame(&d);
    const uint32_t again = tide_draw_mesh_slot(&d, TIDE_MESH_BYTES, 61, 62, 1, &fill);
    TIDE_REQUIRE(again == mesh && !fill);
    for (uint32_t i = 0; i < 5; i++) tide_draw_shaded(&d, &shader, mesh, &given[i], NULL, NULL, 0);
    TIDE_CHECK(d.run_count == 5);
    tide_draw_prepare(&d, NULL);
    uint32_t live = 0;
    next = 0;
    for (uint32_t i = 0; i < d.count; i++) {
        if (d.commands[i].kind != TIDE_DRAW_SHADED) continue;
        const tide_draw_run *run = &d.runs[d.commands[i].shaded.run];
        if (!run->shader || !run->count) continue;
        TIDE_CHECK(live < 3 && d.meshes[run->mesh - 1u].level == live && run->count == counts[live < 3 ? live : 0]);
        for (uint32_t k = 0; k < run->count; k++) {
            instance got;
            memcpy(&got, d.instances + run->first + k * sizeof got, sizeof got);
            TIDE_CHECK(got.tag[0] == order[next++]);
        }
        live++;
    }
    TIDE_CHECK(live == 3 && next == 5);
    // A setting names the level here too
    frame(&d);
    tide_draw_mesh_slot(&d, TIDE_MESH_BYTES, 61, 62, 1, &fill);
    tide_draw_set_lod(&d, 1);
    tide_draw_shaded_instances(&d, &shader, mesh, given, 5, NULL, NULL, 0);
    TIDE_REQUIRE(d.run_count == 1);
    TIDE_CHECK(d.meshes[d.runs[0].mesh - 1u].level == 1 && d.runs[0].count == 5);
    tide_draw_free(&d);
}

// A list appended to another takes its levels with it, each still drawing
// with its full mesh's corners, which the other list then has.
TIDE_TEST(draw_levels_follow_their_list_into_another)
{
    tide_draw_list from = {0}, to = {0};
    frame(&from);
    const uint32_t mesh = leveled_mesh(&from);
    tide_draw_instance(&from, mesh, at(1.0f, 100.0f), 0, TIDE_FILTER_BILINEAR, TIDE_WRAP_CLAMP);
    tide_draw_instance(&from, mesh, at(2.0f, 10.0f), 0, TIDE_FILTER_BILINEAR, TIDE_WRAP_CLAMP);
    tide_draw_instance(&from, mesh, at(3.0f, 100.0f), 0, TIDE_FILTER_BILINEAR, TIDE_WRAP_CLAMP);
    tide_draw_reset(&to);
    tide_draw_append(&to, &from);
    const tide_draw_command *runs[8];
    TIDE_REQUIRE(runs_of(&to, runs, 8) == 2);
    for (uint32_t i = 0; i < 2; i++) {
        const tide_draw_mesh_data *m = mesh_of(&to, runs[i]);
        TIDE_REQUIRE(m->base != 0 && m->base <= to.mesh_count);
        const tide_draw_mesh_data *base = &to.meshes[m->base - 1u];
        TIDE_CHECK(m->level == i + 1u && m->vertices == NULL && m->index_count == two_levels.indices[i]);
        TIDE_CHECK(base->vertices != NULL && base->vertex_count == 4 && base->index_count == 12 && base->frame == to.frame);
        TIDE_CHECK(runs[i]->mesh3.count == i + 1u);
    }
    TIDE_CHECK(to.matrices[runs[1]->mesh3.first].c3.x == 0.5f && to.matrices[runs[1]->mesh3.first + 1u].c3.x == 2.5f);
    tide_draw_free(&from);
    tide_draw_free(&to);
}

typedef struct level_instance {
    tide_float4x4 transform;
    float tag[4];
} level_instance;

static const tide_shader level_shader = {
    .id = 79, .dims = 3, .vertex_size = sizeof(tide_vertex3), .instance_floats = 5,
    .uniform_floats = 1, .bounds_position = 1,
};
static const float level_uniforms[4] = {0.25f, -2.0f, 3.5f, 4.0f};
static const uint32_t alternating_levels[5] = {2, 0, 1, 2, 0};

static void alternating_instances(level_instance given[5], const float tag)
{
    static const float depth[5] = {100.0f, 2.0f, 10.0f, 100.0f, 2.0f};
    for (uint32_t i = 0; i < 5; i++) {
        given[i] = (level_instance){.transform = at(0.1f * (float)(i + 1u), depth[i]),
                                    .tag = {tag + (float)i, -(float)(i + 1u), 0.25f * (float)i, -0.0f}};
        given[i].transform.c1.x = 0.03125f * (float)i;
    }
}

static void record_level_instances(tide_draw_list *d, const bool shaded, const bool individual,
                                    const level_instance *given, const uint32_t count)
{
    uint32_t mesh;
    if (shaded) {
        bool fill;
        mesh = tide_draw_mesh_slot(d, TIDE_MESH_BYTES, 71, 72, 1, &fill);
        if (fill) {
            tide_draw_mesh_fill_bytes(d, mesh, corners, sizeof(tide_vertex3), 4, whole, 12);
            tide_draw_mesh_levels(d, mesh, &two_levels);
        }
        if (individual) {
            for (uint32_t i = 0; i < count; i++) tide_draw_shaded(d, &level_shader, mesh, given + i, level_uniforms, NULL, 0);
        } else tide_draw_shaded_instances(d, &level_shader, mesh, given, count, level_uniforms, NULL, 0);
    } else {
        mesh = leveled_mesh(d);
        tide_float4x4 transforms[10];
        TIDE_REQUIRE(count <= 10);
        for (uint32_t i = 0; i < count; i++) transforms[i] = given[i].transform;
        if (individual) {
            for (uint32_t i = 0; i < count; i++) tide_draw_instance(d, mesh, transforms[i], 0, TIDE_FILTER_BILINEAR, TIDE_WRAP_CLAMP);
        } else tide_draw_instances(d, mesh, transforms, count, 0, TIDE_FILTER_BILINEAR, TIDE_WRAP_CLAMP);
    }
}

// Read live commands, including their recorded state and complete owned payload,
// rather than the backing runs that gathering can leave empty.
static void check_level_instances(const tide_draw_list *d, const bool shaded, const level_instance *expected,
                                   const uint32_t *levels, const uint32_t count, const uint32_t run_count,
                                   const tide_depth depth, const tide_blend blend)
{
    uint32_t next = 0, live = 0;
    for (uint32_t i = 0; i < d->count; i++) {
        const tide_draw_command *c = &d->commands[i];
        const tide_draw_run *run = NULL;
        uint32_t mesh, first, n;
        if (shaded) {
            if (c->kind != TIDE_DRAW_SHADED) continue;
            TIDE_REQUIRE(c->shaded.run < d->run_count);
            run = &d->runs[c->shaded.run];
            if (!run->shader || !run->count) continue;
            TIDE_CHECK(run->shader == &level_shader);
            TIDE_REQUIRE((uint64_t)run->uniforms + sizeof level_uniforms <= d->uniform_used);
            TIDE_CHECK(memcmp(d->uniforms + run->uniforms, level_uniforms, sizeof level_uniforms) == 0);
            mesh = run->mesh, first = run->first, n = run->count;
        } else {
            if (c->kind != TIDE_DRAW_MESH_3D || !c->mesh3.mesh || !c->mesh3.count) continue;
            mesh = c->mesh3.mesh, first = c->mesh3.first, n = c->mesh3.count;
        }
        TIDE_REQUIRE(mesh && mesh <= d->mesh_count);
        const tide_draw_mesh_data *m = &d->meshes[mesh - 1u];
        TIDE_REQUIRE(m->base <= d->mesh_count);
        const tide_draw_mesh_data *base = m->base ? &d->meshes[m->base - 1u] : m;
        TIDE_REQUIRE(base->bytes && base->indices && base->vertex_count == 4 && base->vertex_size == sizeof(tide_vertex3)
                     && base->index_count == 12);
        TIDE_CHECK(memcmp(base->bytes, corners, sizeof corners) == 0 && memcmp(base->indices, whole, sizeof whole) == 0);
        TIDE_CHECK(c->depth == depth && c->blend == blend);
        for (uint32_t k = 0; k < n; k++) {
            TIDE_REQUIRE(next < count);
            const uint32_t level = levels[next];
            TIDE_REQUIRE(level <= 2);
            TIDE_CHECK(m->level == level);
            const uint32_t indices = level ? two_levels.indices[level - 1u] : 12;
            const uint32_t *triangles = level ? simpler + two_levels.first[level - 1u] : whole;
            TIDE_REQUIRE(m->indices && m->index_count == indices);
            TIDE_CHECK(memcmp(m->indices, triangles, indices * sizeof(uint32_t)) == 0);
            const uint32_t cull = TIDE_CULL_BACK | (expected[next].transform.c0.x < 0.0f ? TIDE_DRAW_MIRRORED : 0u);
            TIDE_CHECK(c->cull == cull);
            if (shaded) {
                const uint64_t at_byte = (uint64_t)first + (uint64_t)k * sizeof(level_instance);
                TIDE_REQUIRE(at_byte + sizeof(level_instance) <= d->instance_used);
                TIDE_CHECK(memcmp(d->instances + at_byte, expected + next, sizeof(level_instance)) == 0);
            } else {
                TIDE_REQUIRE((uint64_t)first + k < d->matrix_count);
                TIDE_CHECK(memcmp(d->matrices + first + k, &expected[next].transform, sizeof(tide_float4x4)) == 0);
            }
            next++;
        }
        live++;
    }
    TIDE_CHECK(next == count && live == run_count);
}

TIDE_TEST(draw_levels_transparent_instances_keep_recorded_order)
{
    for (uint32_t shaded = 0; shaded < 2; shaded++) {
        for (uint32_t individual = 0; individual < 2; individual++) {
            for (uint32_t blend = TIDE_BLEND_ALPHA; blend <= TIDE_BLEND_MULTIPLY; blend++) {
                tide_draw_list d = {0};
                frame(&d);
                tide_draw_set_depth(&d, TIDE_DEPTH_READ);
                tide_draw_set_blend(&d, (tide_blend)blend);
                level_instance given[5], expected[5];
                alternating_instances(given, 10.0f);
                memcpy(expected, given, sizeof expected);
                record_level_instances(&d, shaded != 0, individual != 0, given, 5);
                memset(given, 0, sizeof given);
                // Preparation sees restored settings, while commands still read depth and blend.
                tide_draw_unset_depth(&d);
                tide_draw_unset_blend(&d);
                for (uint32_t pass = 0; pass < 3; pass++) {
                    if (pass) tide_draw_prepare(&d, NULL);
                    check_level_instances(&d, shaded != 0, expected, alternating_levels, 5, 5,
                                           TIDE_DEPTH_READ, (tide_blend)blend);
                }
                tide_draw_free(&d);
            }
        }
    }
}

TIDE_TEST(draw_levels_transparent_append_keeps_owned_order)
{
    for (uint32_t shaded = 0; shaded < 2; shaded++) {
        for (uint32_t individual = 0; individual < 2; individual++) {
            tide_draw_list from = {0}, to = {0};
            level_instance expected[10];
            uint32_t levels[10];
            // A different aspect must not recull the source's recorded instances.
            tide_draw_screen_size(&to, tide_f2(1.0f, 1000.0f));
            for (uint32_t copy = 0; copy < 2; copy++) {
                frame(&from);
                tide_draw_set_depth(&from, TIDE_DEPTH_READ);
                tide_draw_set_blend(&from, TIDE_BLEND_MULTIPLY);
                level_instance given[5];
                alternating_instances(given, 20.0f + 10.0f * (float)copy);
                for (uint32_t i = 0; i < 5; i++) given[i].transform.c3.y += 0.125f * (float)copy;
                memcpy(expected + 5u * copy, given, sizeof given);
                memcpy(levels + 5u * copy, alternating_levels, sizeof alternating_levels);
                record_level_instances(&from, shaded != 0, individual != 0, given, 5);
                memset(given, 0, sizeof given);
                tide_draw_unset_depth(&from);
                tide_draw_unset_blend(&from);
                // Append itself prepares the source, then owns the result across source reuse.
                tide_draw_append(&to, &from);
            }
            if (from.instance_used) memset(from.instances, 0, from.instance_used);
            if (from.matrix_count) memset(from.matrices, 0, (size_t)from.matrix_count * sizeof(tide_float4x4));
            for (uint32_t i = 0; i < from.mesh_count; i++) {
                tide_draw_mesh_data *m = &from.meshes[i];
                if (m->bytes) memset(m->bytes, 0, (size_t)m->vertex_count * m->vertex_size);
                if (m->indices) memset(m->indices, 0, (size_t)m->index_count * sizeof(uint32_t));
            }
            tide_draw_free(&from);
            for (uint32_t pass = 0; pass < 3; pass++) {
                if (pass) tide_draw_prepare(&to, NULL);
                check_level_instances(&to, shaded != 0, expected, levels, 10, 10, TIDE_DEPTH_READ, TIDE_BLEND_MULTIPLY);
            }
            tide_draw_free(&to);
        }
    }
}

TIDE_TEST(draw_levels_transparent_culling_keeps_mirrored_survivors_in_order)
{
    for (uint32_t shaded = 0; shaded < 2; shaded++) {
        for (uint32_t individual = 0; individual < 2; individual++) {
            tide_draw_list d = {0};
            frame(&d);
            tide_draw_set_frustum_culling(&d, true);
            tide_draw_set_depth(&d, TIDE_DEPTH_READ);
            const tide_float4x4 through = tide_scaling_f4x4(tide_f3(-1.0f, 1.0f, 1.0f));
            tide_draw_set_transform_3d(&d, through);
            level_instance visible[5], given[9], expected[5];
            alternating_instances(visible, 30.0f);
            for (uint32_t i = 0; i < 5; i++) {
                if (i == 0 || i == 1 || i == 4) visible[i].transform.c0.x = -1.0f;
                given[2u * i] = visible[i];
                expected[i] = visible[i];
                expected[i].transform = tide_mul_f4x4(through, visible[i].transform);
                if (i < 4) {
                    given[2u * i + 1u] = visible[i];
                    given[2u * i + 1u].transform.c3.x = 10000.0f;
                }
            }
            record_level_instances(&d, shaded != 0, individual != 0, given, 9);
            memset(given, 0, sizeof given);
            tide_draw_unset_transform_3d(&d);
            tide_draw_unset_depth(&d);
            tide_draw_unset_blend(&d);
            for (uint32_t pass = 0; pass < 3; pass++) {
                if (pass) tide_draw_prepare(&d, NULL);
                check_level_instances(&d, shaded != 0, expected, alternating_levels, 5, 5, TIDE_DEPTH_READ, TIDE_BLEND_ALPHA);
            }
            tide_draw_free(&d);
        }
    }
}

TIDE_TEST(draw_levels_read_write_and_read_replace_still_gather_finest_first)
{
    static const tide_depth depths[2] = {TIDE_DEPTH_READ_WRITE, TIDE_DEPTH_READ};
    static const tide_blend blends[2] = {TIDE_BLEND_ALPHA, TIDE_BLEND_REPLACE};
    static const uint32_t order[5] = {1, 4, 2, 0, 3}, levels[5] = {0, 0, 1, 2, 2};
    for (uint32_t shaded = 0; shaded < 2; shaded++) {
        for (uint32_t individual = 0; individual < 2; individual++) {
            for (uint32_t mode = 0; mode < 2; mode++) {
                tide_draw_list d = {0};
                frame(&d);
                tide_draw_set_depth(&d, depths[mode]);
                tide_draw_set_blend(&d, blends[mode]);
                level_instance given[5], expected[5];
                alternating_instances(given, 40.0f);
                for (uint32_t i = 0; i < 5; i++) expected[i] = given[order[i]];
                record_level_instances(&d, shaded != 0, individual != 0, given, 5);
                check_level_instances(&d, shaded != 0, individual ? given : expected,
                                       individual ? alternating_levels : levels, 5, individual ? 5 : 3, depths[mode], blends[mode]);
                memset(given, 0, sizeof given);
                tide_draw_unset_depth(&d);
                tide_draw_unset_blend(&d);
                for (uint32_t pass = 0; pass < 2; pass++) {
                    tide_draw_prepare(&d, NULL);
                    check_level_instances(&d, shaded != 0, expected, levels, 5, 3, depths[mode], blends[mode]);
                }
                tide_draw_free(&d);
            }
        }
    }
}
