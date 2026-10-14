#include "interaction.h"
#include "tide_test.h"
#include <math.h>

TIDE_TEST(editor_mesh_picking_checks_triangles_depth_and_nested_transforms)
{
    tide_vertex3 vertices[] = {{.position={0,0,0}},{.position={2,0,0}},{.position={0,2,0}}};
    uint32_t indices[] = {0,1,2};
    tide_mesh mesh = {vertices,3,indices,3,1};
    const editor_camera camera = {{100,100},40,true};
    double depth = INFINITY;
    TIDE_CHECK(editor_pick_mesh(&mesh,tide_identity_f4x4(),camera,tide_f2(120,80),&depth));
    TIDE_CHECK(fabs(depth+1) < 1e-6);
    // Inside the bounds but outside the triangle is not a hit.
    depth = INFINITY;
    TIDE_CHECK(!editor_pick_mesh(&mesh,tide_identity_f4x4(),camera,tide_f2(170,30),&depth));
    const tide_float4x4 parent = tide_trs_f4x4(tide_f3(4,3,0),tide_euler_q(tide_f3(0,0,.5f)),tide_f3(2,3,1));
    const tide_float4x4 child = tide_trs_f4x4(tide_f3(1,0,0),tide_q(0,0,0,1),tide_f3(-1,1,1));
    const tide_float4x4 world = tide_mul_f4x4(parent,child);
    const tide_float2 at = editor_project(camera,tide_transform_f4x4(world,tide_f3(.5f,.5f,0)));
    depth = INFINITY;
    TIDE_REQUIRE(editor_pick_mesh(&mesh,world,camera,at,&depth));
    const double near = depth;
    TIDE_CHECK(!editor_pick_mesh(&mesh,tide_mul_f4x4(tide_translate_f4x4(tide_f3(0,0,2)),world),camera,at,&depth));
    TIDE_CHECK(depth == near);
    TIDE_CHECK(editor_pick_mesh(&mesh,tide_mul_f4x4(tide_translate_f4x4(tide_f3(0,0,-2)),world),camera,at,&depth));
    indices[2] = 3; depth = INFINITY;
    TIDE_CHECK(!editor_pick_mesh(&mesh,world,camera,at,&depth));
    indices[2] = 2; vertices[2].position.x = NAN;
    TIDE_CHECK(!editor_pick_mesh(&mesh,world,camera,at,&depth));
}

TIDE_TEST(editor_gizmo_drag_uses_parent_axes_and_rejects_edge_on_rotation)
{
    const editor_camera camera = {{0,0},48,false};
    const tide_float3 axis = tide_f3(0,2,0);
    float amount;
    TIDE_REQUIRE(editor_axis_drag(camera,axis,tide_f2(0,-48),&amount));
    TIDE_CHECK(amount == .5f);
    TIDE_CHECK(!editor_axis_drag(camera,tide_f3(0,0,0),tide_f2(20,20),&amount));
    const editor_camera xy = {{0,0},48,true};
    float angle;
    TIDE_REQUIRE(editor_plane_angle(xy,tide_f3(1,0,0),tide_f3(0,1,0),tide_f2(0,-48),&angle));
    TIDE_CHECK(fabsf(angle-1.57079632679f) < 1e-6f);
    TIDE_CHECK(!editor_plane_angle(xy,tide_f3(0,1,0),tide_f3(0,0,1),tide_f2(0,-48),&angle));
    TIDE_CHECK(!editor_plane_angle(xy,tide_f3(1,0,0),tide_f3(0,1,0),tide_f2(0,0),&angle));
}
