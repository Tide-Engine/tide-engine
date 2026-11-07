#include <string.h>

#include "tide/draw.h"
#include "tide_test.h"

static bool same_matrix(tide_float4x4 a, tide_float4x4 b)
{
    return memcmp(&a, &b, sizeof a) == 0;
}

TIDE_TEST(draw_camera_relative_projection_keeps_original_parts)
{
    tide_draw_list d = {0};
    const tide_float3 eye = {3000, 200, 3000};
    const tide_quaternion rotation = tide_euler_q(tide_f3(0.1f, 0.37f, 0));
    tide_draw_set_camera_3d(&d, tide_f3(0, 0, 0), rotation, 60);
    const tide_draw_camera_3d_data zero = d.cameras_3d[d.place.camera_3d - 1u];
    tide_draw_set_camera_3d(&d, eye, rotation, 60);
    const tide_draw_camera_3d_data away = d.cameras_3d[d.place.camera_3d - 1u];
    TIDE_CHECK(same_matrix(zero.relative, away.relative));
    TIDE_CHECK(away.origin.x == eye.x && away.origin.y == eye.y && away.origin.z == eye.z && away.origin.w == 0);
    TIDE_CHECK(same_matrix(away.matrix, tide_draw_camera_3d_matrix(eye, rotation, 60)));
    TIDE_CHECK(!same_matrix(away.matrix, away.relative));

    // Custom affine cameras include nonuniform scale, shear and reflection.
    tide_float4x4 transform = tide_trs_f4x4(tide_f3(0, 0, 0), rotation, tide_f3(-2, 3, 0.5f));
    transform.c1.x += 0.125f;
    const tide_float4x4 projection = {{1.5f, 0, 0, 0}, {0, 1.5f, 0, 0},
                                     {0, 0, -1.000001f, -1}, {0, 0, -0.08000004f, 0}};
    tide_draw_set_camera_matrices(&d, transform, projection);
    const tide_draw_camera_3d_data affine_zero = d.cameras_3d[d.place.camera_3d - 1u];
    transform.c3 = tide_f4(eye.x, eye.y, eye.z, 1);
    tide_draw_set_camera_matrices(&d, transform, projection);
    const tide_draw_camera_3d_data affine_away = d.cameras_3d[d.place.camera_3d - 1u];
    TIDE_CHECK(same_matrix(affine_zero.relative, affine_away.relative));
    TIDE_CHECK(same_matrix(transform, affine_away.transform) && same_matrix(projection, affine_away.projection));
    TIDE_CHECK(affine_away.origin.x == eye.x && affine_away.origin.y == eye.y && affine_away.origin.z == eye.z);

    // A projective camera keeps the original homogeneous mapping, with no rebasing.
    transform.c0.w = 0.125f;
    tide_draw_set_camera_matrices(&d, transform, projection);
    const tide_draw_camera_3d_data projective = d.cameras_3d[d.place.camera_3d - 1u];
    TIDE_CHECK(same_matrix(projective.relative, projective.matrix));
    TIDE_CHECK(projective.origin.x == 0 && projective.origin.y == 0 && projective.origin.z == 0);
    tide_draw_free(&d);
}

TIDE_TEST(draw_camera_append_retains_relative_projection)
{
    tide_draw_list from = {0}, to = {0};
    tide_draw_set_camera_3d(&from, tide_f3(3000, 200, 3000), tide_euler_q(tide_f3(0, 0.37f, 0)), 60);
    tide_draw_clear(&from, TIDE_COLOR_BLACK); // Settle the camera into a command.
    TIDE_REQUIRE(from.count == 2 && from.commands[0].camera.data != 0);
    const tide_draw_camera_3d_data camera = from.cameras_3d[from.commands[0].camera.data - 1u];
    tide_draw_append(&to, &from);
    TIDE_REQUIRE(to.count == 2 && to.commands[0].camera.data != 0);
    const tide_draw_camera_3d_data copied = to.cameras_3d[to.commands[0].camera.data - 1u];
    tide_draw_free(&from);
    TIDE_CHECK(same_matrix(camera.relative, copied.relative) && same_matrix(camera.matrix, copied.matrix));
    TIDE_CHECK(same_matrix(camera.transform, copied.transform) && same_matrix(camera.projection, copied.projection));
    TIDE_CHECK(memcmp(&camera.origin, &copied.origin, sizeof camera.origin) == 0);

    // Handwritten old-style commands have only the combined matrix; malformed data slots
    // take that same bounded fallback when appended.
    to.commands[0].camera.data = UINT32_MAX;
    tide_draw_append(&from, &to);
    TIDE_REQUIRE(from.count == 2 && from.commands[0].camera.data != 0);
    const tide_draw_camera_3d_data fallback = from.cameras_3d[from.commands[0].camera.data - 1u];
    TIDE_CHECK(same_matrix(fallback.matrix, fallback.relative) && same_matrix(camera.matrix, fallback.matrix));
    TIDE_CHECK(fallback.origin.x == 0 && fallback.origin.y == 0 && fallback.origin.z == 0);
    tide_draw_free(&from);
    tide_draw_free(&to);
}
