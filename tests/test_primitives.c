#include <string.h>

#include "tide/draw.h"
#include "tide_test.h"

static bool close_float(const float a, const float b)
{
    return tide_abs_f(a - b) < 0.00001f;
}

static bool close_point(const tide_float3 a, const tide_float3 b)
{
    return close_float(a.x, b.x) && close_float(a.y, b.y) && close_float(a.z, b.z);
}

static tide_float3 transformed(const tide_float4x4 matrix, const tide_float3 point)
{
    const tide_float4 p = tide_mul_f4x4_f4(matrix, tide_f4(point.x, point.y, point.z, 1));
    return tide_f3(p.x, p.y, p.z);
}

static void check_triangles(const tide_draw_mesh_data *mesh, const tide_color color, const bool plane)
{
    TIDE_CHECK(mesh->vertex_size == sizeof(tide_vertex3));
    TIDE_CHECK(mesh->index_count > 0 && mesh->index_count % 3u == 0);
    for (uint32_t i = 0; i < mesh->vertex_count; i++) {
        TIDE_CHECK(memcmp(&mesh->vertices[i].color, &color, sizeof color) == 0);
    }
    for (uint32_t i = 0; i < mesh->index_count; i += 3u) {
        const uint32_t a = mesh->indices[i], b = mesh->indices[i + 1u], c = mesh->indices[i + 2u];
        TIDE_REQUIRE(a < mesh->vertex_count && b < mesh->vertex_count && c < mesh->vertex_count);
        const tide_float3 pa = mesh->vertices[a].position, pb = mesh->vertices[b].position, pc = mesh->vertices[c].position;
        const tide_float3 normal = tide_cross_f3(tide_sub_f3(pb, pa), tide_sub_f3(pc, pa));
        TIDE_CHECK(tide_dot_f3(normal, normal) > 0.00000001f);
        // Solid faces point out of the centered unit mesh; the plane's point up.
        if (plane) TIDE_CHECK(normal.y > 0);
        else TIDE_CHECK(tide_dot_f3(normal, tide_add_f3(tide_add_f3(pa, pb), pc)) > 0);
    }
}

TIDE_TEST(draw_primitives_geometry)
{
    tide_draw_list d = {0};
    const tide_float3 origin = tide_f3(0, 0, 0);
    tide_draw_cube(&d, origin, tide_f3(1, 1, 1), TIDE_COLOR_RED);
    tide_draw_sphere(&d, origin, 1, TIDE_COLOR_GREEN);
    tide_draw_cylinder(&d, origin, 1, 1, TIDE_COLOR_BLUE);
    tide_draw_plane(&d, origin, tide_f2(1, 1), TIDE_COLOR_WHITE);
    TIDE_REQUIRE(d.count == 4 && d.mesh_count == 4);
    const tide_draw_mesh_data *cube = &d.meshes[d.commands[0].mesh3.mesh - 1u];
    const tide_draw_mesh_data *sphere = &d.meshes[d.commands[1].mesh3.mesh - 1u];
    const tide_draw_mesh_data *cylinder = &d.meshes[d.commands[2].mesh3.mesh - 1u];
    const tide_draw_mesh_data *plane = &d.meshes[d.commands[3].mesh3.mesh - 1u];
    check_triangles(cube, TIDE_COLOR_RED, false);
    check_triangles(sphere, TIDE_COLOR_GREEN, false);
    check_triangles(cylinder, TIDE_COLOR_BLUE, false);
    check_triangles(plane, TIDE_COLOR_WHITE, true);
    TIDE_CHECK(cube->vertex_count == 8 && cube->index_count == 36);
    TIDE_CHECK(plane->vertex_count == 4 && plane->index_count == 6);
    for (uint32_t i = 0; i < cube->vertex_count; i++) {
        const tide_float3 p = cube->vertices[i].position;
        TIDE_CHECK(tide_abs_f(p.x) == 0.5f && tide_abs_f(p.y) == 0.5f && tide_abs_f(p.z) == 0.5f);
    }
    bool north = false, south = false;
    for (uint32_t i = 0; i < sphere->vertex_count; i++) {
        const tide_float3 p = sphere->vertices[i].position;
        TIDE_CHECK(close_float(tide_dot_f3(p, p), 1));
        north = north || p.y == 1;
        south = south || p.y == -1;
    }
    TIDE_CHECK(north && south);
    uint32_t cap_centers = 0;
    for (uint32_t i = 0; i < cylinder->vertex_count; i++) {
        const tide_float3 p = cylinder->vertices[i].position;
        TIDE_CHECK(tide_abs_f(p.y) == 0.5f);
        const float radius_squared = p.x * p.x + p.z * p.z;
        if (radius_squared == 0) cap_centers++;
        else TIDE_CHECK(close_float(radius_squared, 1));
    }
    TIDE_CHECK(cap_centers == 2);
    for (uint32_t i = 0; i < plane->vertex_count; i++) {
        const tide_float3 p = plane->vertices[i].position;
        TIDE_CHECK(p.y == 0 && tide_abs_f(p.x) == 0.5f && tide_abs_f(p.z) == 0.5f);
    }
    tide_draw_free(&d);
}

TIDE_TEST(draw_primitives_dimensions_and_rotation)
{
    tide_draw_list d = {0};
    const tide_float3 center = tide_f3(5, 6, 7);
    const tide_quaternion turn_z = tide_axisangle_q(tide_f3(0, 0, 1), TIDE_PI_F * 0.5f);
    const tide_quaternion turn_x = tide_axisangle_q(tide_f3(1, 0, 0), TIDE_PI_F * 0.5f);
    tide_draw_cube_rotated(&d, center, tide_f3(2, 4, 6), turn_z, TIDE_COLOR_RED);
    tide_draw_sphere(&d, center, 3, TIDE_COLOR_RED);
    tide_draw_cylinder_rotated(&d, center, 2, 4, turn_z, TIDE_COLOR_RED);
    tide_draw_plane_rotated(&d, center, tide_f2(3, 8), turn_x, TIDE_COLOR_RED);
    TIDE_REQUIRE(d.count == 4 && d.matrix_count == 4);
    const tide_float4x4 cube = d.matrices[d.commands[0].mesh3.first];
    const tide_float4x4 sphere = d.matrices[d.commands[1].mesh3.first];
    const tide_float4x4 cylinder = d.matrices[d.commands[2].mesh3.first];
    const tide_float4x4 plane = d.matrices[d.commands[3].mesh3.first];
    TIDE_CHECK(close_point(transformed(cube, tide_f3(0.5f, 0, 0)), tide_f3(5, 7, 7)));
    TIDE_CHECK(close_point(transformed(cube, tide_f3(0, 0.5f, 0)), tide_f3(3, 6, 7)));
    TIDE_CHECK(close_point(transformed(cube, tide_f3(0, 0, 0.5f)), tide_f3(5, 6, 10)));
    TIDE_CHECK(close_point(transformed(sphere, tide_f3(0, 1, 0)), tide_f3(5, 9, 7)));
    TIDE_CHECK(close_point(transformed(cylinder, tide_f3(0, 0.5f, 0)), tide_f3(3, 6, 7)));
    TIDE_CHECK(close_point(transformed(cylinder, tide_f3(1, 0, 0)), tide_f3(5, 8, 7)));
    TIDE_CHECK(close_point(transformed(plane, tide_f3(0, 0, 0.5f)), tide_f3(5, 2, 7)));
    TIDE_CHECK(close_point(transformed(plane, tide_f3(0.5f, 0, 0)), tide_f3(6.5f, 6, 7)));
    tide_draw_free(&d);
}

TIDE_TEST(draw_primitives_are_cached_and_instanced_by_color)
{
    tide_draw_list d = {0};
    const tide_float3 origin = tide_f3(0, 0, 0);
    tide_draw_cube(&d, origin, tide_f3(1, 1, 1), TIDE_COLOR_RED);
    tide_draw_cube_rotated(&d, tide_f3(2, 3, 4), tide_f3(2, 4, 6), tide_axisangle_q(tide_f3(0, 1, 0), 1), TIDE_COLOR_RED);
    tide_draw_cube(&d, origin, tide_f3(1, 1, 1), TIDE_COLOR_BLUE);
    tide_draw_cube(&d, origin, tide_f3(3, 3, 3), TIDE_COLOR_RED);
    TIDE_REQUIRE(d.count == 3 && d.mesh_count == 2 && d.matrix_count == 4);
    TIDE_CHECK(d.commands[0].mesh3.count == 2 && d.commands[1].mesh3.count == 1 && d.commands[2].mesh3.count == 1);
    const uint32_t red_slot = d.commands[0].mesh3.mesh, blue_slot = d.commands[1].mesh3.mesh;
    TIDE_CHECK(red_slot != blue_slot && d.commands[2].mesh3.mesh == red_slot);
    check_triangles(&d.meshes[red_slot - 1u], TIDE_COLOR_RED, false);
    check_triangles(&d.meshes[blue_slot - 1u], TIDE_COLOR_BLUE, false);
    tide_vertex3 *const red_vertices = d.meshes[red_slot - 1u].vertices;
    tide_vertex3 *const blue_vertices = d.meshes[blue_slot - 1u].vertices;
    red_vertices[0].uv.x = 77; // A cached copy would lose this if it were filled again.
    for (int frame = 0; frame < 3; frame++) {
        tide_draw_reset(&d);
        for (uint32_t i = 0; i < 128; i++) tide_draw_cube(&d, tide_f3((float)i, 0, 0), tide_f3(1, 1, 1), TIDE_COLOR_RED);
        tide_draw_cube(&d, origin, tide_f3(1, 1, 1), TIDE_COLOR_BLUE);
        TIDE_CHECK(d.count == 2 && d.mesh_count == 2 && d.commands[0].mesh3.count == 128);
        TIDE_CHECK(d.commands[0].mesh3.mesh == red_slot && d.commands[1].mesh3.mesh == blue_slot);
        TIDE_CHECK(d.meshes[red_slot - 1u].vertices == red_vertices && red_vertices[0].uv.x == 77);
        TIDE_CHECK(d.meshes[blue_slot - 1u].vertices == blue_vertices);
    }
    tide_draw_free(&d);
}

TIDE_TEST(draw_primitives_unused_colors_are_released)
{
    tide_draw_list d = {0};
    const tide_float3 origin = tide_f3(0, 0, 0);
    tide_draw_sphere(&d, origin, 1, TIDE_COLOR_RED);
    tide_draw_sphere(&d, origin, 1, TIDE_COLOR_BLUE);
    TIDE_REQUIRE(d.mesh_count == 2);
    const uint32_t blue_slot = d.commands[1].mesh3.mesh;
    tide_draw_reset(&d);
    tide_draw_sphere(&d, origin, 2, TIDE_COLOR_RED);
    tide_draw_reset(&d);
    TIDE_CHECK(d.meshes[blue_slot - 1u].space == TIDE_MESH_NONE);
    TIDE_CHECK(d.meshes[blue_slot - 1u].vertices == NULL && d.meshes[blue_slot - 1u].indices == NULL);
    tide_draw_sphere(&d, origin, 3, TIDE_COLOR_GREEN);
    TIDE_CHECK(d.mesh_count == 2 && d.commands[0].mesh3.mesh == blue_slot);
    check_triangles(&d.meshes[blue_slot - 1u], TIDE_COLOR_GREEN, false);
    tide_draw_free(&d);
}

TIDE_TEST(draw_primitives_append_and_forget)
{
    tide_draw_list d = {0}, world = {0};
    const tide_float3 center = tide_f3(1, 2, 3);
    tide_draw_cube(&d, center, tide_f3(2, 2, 2), TIDE_COLOR_RED);
    tide_draw_sphere(&d, center, 2, TIDE_COLOR_BLUE);
    tide_draw_cylinder(&d, center, 2, 4, TIDE_COLOR_GREEN);
    tide_draw_plane(&d, center, tide_f2(3, 4), TIDE_COLOR_WHITE);
    tide_draw_clear(&world, TIDE_COLOR_BLACK);
    tide_draw_append(&world, &d);
    TIDE_REQUIRE(world.count == 5 && world.mesh_count == 4 && world.matrix_count == 4);
    for (uint32_t i = 0; i < 4; i++) {
        const tide_draw_command *from = &d.commands[i], *to = &world.commands[i + 1u];
        TIDE_CHECK(to->kind == TIDE_DRAW_MESH_3D && to->mesh3.count == 1);
        const tide_draw_mesh_data *original = &d.meshes[from->mesh3.mesh - 1u], *copy = &world.meshes[to->mesh3.mesh - 1u];
        TIDE_CHECK(copy->vertex_count == original->vertex_count && copy->index_count == original->index_count);
        TIDE_CHECK(memcmp(copy->vertices, original->vertices, (size_t)copy->vertex_count * sizeof(tide_vertex3)) == 0);
        TIDE_CHECK(memcmp(&world.matrices[to->mesh3.first], &d.matrices[from->mesh3.first], sizeof(tide_float4x4)) == 0);
    }
    tide_draw_forget(&d);
    TIDE_CHECK(d.mesh_count == 0);
    for (uint32_t i = 0; i < 4; i++) TIDE_CHECK(d.commands[i].mesh3.mesh == 0);
    tide_draw_append(&world, &d);
    TIDE_CHECK(world.count == 5);
    tide_draw_cube(&d, center, tide_f3(2, 2, 2), TIDE_COLOR_RED);
    TIDE_REQUIRE(d.count == 5 && d.mesh_count == 1);
    TIDE_CHECK(d.meshes[0].epoch == d.epoch);
    check_triangles(&d.meshes[0], TIDE_COLOR_RED, false);
    tide_draw_free(&world);
    tide_draw_free(&d);
}

TIDE_TEST(draw_primitives_nonpositive_dimensions_draw_nothing)
{
    tide_draw_list d = {0};
    const tide_float3 origin = tide_f3(0, 0, 0);
    const tide_quaternion rotation = tide_identity_q();
    tide_draw_cube(&d, origin, tide_f3(1, 0, 1), TIDE_COLOR_RED);
    tide_draw_cube_rotated(&d, origin, tide_f3(-1, 1, 1), rotation, TIDE_COLOR_RED);
    tide_draw_sphere(&d, origin, 0, TIDE_COLOR_RED);
    tide_draw_sphere(&d, origin, -1, TIDE_COLOR_RED);
    tide_draw_cylinder(&d, origin, 0, 1, TIDE_COLOR_RED);
    tide_draw_cylinder_rotated(&d, origin, 1, -1, rotation, TIDE_COLOR_RED);
    tide_draw_plane(&d, origin, tide_f2(0, 1), TIDE_COLOR_RED);
    tide_draw_plane_rotated(&d, origin, tide_f2(1, -1), rotation, TIDE_COLOR_RED);
    TIDE_CHECK(d.count == 0 && d.mesh_count == 0 && d.matrix_count == 0);
    tide_draw_free(&d);
}
