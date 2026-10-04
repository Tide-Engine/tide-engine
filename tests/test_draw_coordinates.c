#include "tide/draw.h"
#include "tide_test.h"

static bool close_float(const float a, const float b)
{
    return tide_abs_f(a - b) < 0.001f;
}

static bool close2(const tide_float2 a, const tide_float2 b)
{
    return close_float(a.x, b.x) && close_float(a.y, b.y);
}

static bool close3(const tide_float3 a, const tide_float3 b)
{
    return close_float(a.x, b.x) && close_float(a.y, b.y) && close_float(a.z, b.z);
}

TIDE_TEST(draw_coordinates_2d)
{
    tide_draw_list d = {0};
    const tide_float2 screen = tide_f2(800.0f, 400.0f);
    const tide_float2 world = tide_f2(30.0f, 20.0f);
    TIDE_CHECK(close2(tide_draw_world_to_screen(&d, world, screen), tide_f2(430.0f, 180.0f)));
    TIDE_CHECK(close2(tide_draw_screen_to_world(&d, tide_f2(430.0f, 180.0f), screen), world));

    tide_draw_camera(&d, tide_f2(10.0f, -5.0f), 20.0f);
    tide_draw_circle(&d, world, 1.0f, TIDE_COLOR_WHITE);
    const tide_float2 position = tide_f2(700.0f, 150.0f);
    const tide_float2 point = tide_f2(40.0f, 0.0f);
    TIDE_CHECK(close2(tide_draw_world_to_screen(&d, point, screen), position));
    TIDE_CHECK(close2(tide_draw_screen_to_world(&d, position, screen), point));
    TIDE_CHECK(close2(tide_draw_world_to_screen(&d, tide_f2(-50.0f, 35.0f), screen), tide_f2(-200.0f, -200.0f)));

    // The renderer treats non-positive orthographic size as one unit a pixel.
    tide_draw_camera(&d, tide_f2(10.0f, -5.0f), 0.0f);
    TIDE_CHECK(close2(tide_draw_world_to_screen(&d, point, screen), tide_f2(430.0f, 195.0f)));

    tide_draw_screen(&d);
    tide_draw_camera_3d(&d, tide_f3(0.0f, 0.0f, -5.0f), tide_identity_q(), 90.0f);
    TIDE_CHECK(close2(tide_draw_world_to_screen(&d, position, screen), position));
    TIDE_CHECK(close2(tide_draw_screen_to_world(&d, position, screen), position));
    tide_draw_camera(&d, tide_f2(0.0f, 0.0f), 2.0f);
    TIDE_CHECK(close2(tide_draw_world_to_screen(&d, tide_f2(1.0f, 1.0f), screen), tide_f2(500.0f, 100.0f)));
    tide_draw_free(&d);
}

TIDE_TEST(draw_coordinates_perspective)
{
    tide_draw_list d = {0};
    const tide_float2 screen = tide_f2(800.0f, 400.0f);
    tide_draw_camera_3d(&d, tide_f3(0.0f, 0.0f, 0.0f), tide_identity_q(), 90.0f);
    const tide_float3 world = tide_f3(2.0f, 1.0f, 2.0f);
    const tide_float3 position = tide_draw_world_to_screen_3d(&d, world, screen);
    TIDE_CHECK(close3(position, tide_f3(600.0f, 100.0f, 0.85f)));
    TIDE_CHECK(close3(tide_draw_screen_to_world_3d(&d, position, screen), world));
    TIDE_CHECK(close3(tide_draw_screen_to_world_3d(&d, tide_f3(400.0f, 200.0f, 0.0f), screen),
                      tide_f3(0.0f, 0.0f, 0.3f)));

    // Fitting to a portrait viewport changes x only, with no pixel-size state.
    const tide_float2 portrait = tide_f2(400.0f, 800.0f);
    TIDE_CHECK(close3(tide_draw_world_to_screen_3d(&d, world, portrait), tide_f3(600.0f, 200.0f, 0.85f)));
    TIDE_CHECK(close3(tide_draw_screen_to_world_3d(&d, tide_f3(600.0f, 200.0f, 0.85f), portrait), world));

    // A 2D camera and screen mode leave the 3D camera as it was.
    tide_draw_camera(&d, tide_f2(100.0f, 100.0f), 1.0f);
    tide_draw_screen(&d);
    TIDE_CHECK(close3(tide_draw_world_to_screen_3d(&d, world, screen), position));
    TIDE_CHECK(d.count == 3 && d.matrix_count == 1);
    tide_draw_free(&d);
}

TIDE_TEST(draw_coordinates_rotated_camera)
{
    tide_draw_list d = {0};
    const tide_float2 screen = tide_f2(800.0f, 400.0f);
    const tide_float3 position = tide_f3(2.0f, 3.0f, -4.0f);
    const tide_quaternion rotation = tide_axisangle_q(tide_f3(0.0f, 1.0f, 0.0f), TIDE_PI_F * 0.5f);
    tide_draw_camera_3d(&d, position, rotation, 90.0f);
    const tide_float3 world = tide_f3(7.0f, 4.0f, -6.0f);
    const tide_float3 pixel = tide_draw_world_to_screen_3d(&d, world, screen);
    TIDE_CHECK(close3(pixel, tide_f3(480.0f, 160.0f, 0.94f)));
    TIDE_CHECK(close3(tide_draw_screen_to_world_3d(&d, pixel, screen), world));
    TIDE_CHECK(close3(tide_draw_screen_ray_origin(&d, tide_f2(400.0f, 200.0f), screen), tide_f3(2.3f, 3.0f, -4.0f)));
    TIDE_CHECK(close3(tide_draw_screen_ray_direction(&d, tide_f2(400.0f, 200.0f), screen), tide_f3(1.0f, 0.0f, 0.0f)));
    TIDE_CHECK(close3(tide_draw_screen_to_world_3d(&d, tide_f3(480.0f, 160.0f, 1.0f), screen), tide_f3(0.0f, 0.0f, 0.0f)));
    tide_draw_free(&d);
}

TIDE_TEST(draw_coordinates_matrix_cameras)
{
    tide_draw_list d = {0};
    const tide_float2 screen = tide_f2(800.0f, 400.0f);
    const tide_float4x4 transform = tide_translate_f4x4(tide_f3(3.0f, 4.0f, -5.0f));
    tide_draw_camera_matrices(&d, transform, tide_ortho_f4x4(8.0f, 4.0f, 1.0f, 11.0f));
    const tide_float3 world = tide_f3(5.0f, 5.0f, 1.0f);
    TIDE_CHECK(close3(tide_draw_world_to_screen_3d(&d, world, screen), tide_f3(600.0f, 100.0f, 0.5f)));
    TIDE_CHECK(close3(tide_draw_screen_to_world_3d(&d, tide_f3(600.0f, 100.0f, 0.5f), screen), world));
    TIDE_CHECK(close3(tide_draw_screen_to_world_3d(&d, tide_f3(400.0f, 200.0f, 1.0f), screen), tide_f3(3.0f, 4.0f, 6.0f)));

    const tide_quaternion rotation = tide_axisangle_q(tide_f3(0.0f, 1.0f, 0.0f), TIDE_PI_F * 0.5f);
    const tide_float4x4 turned = tide_trs_f4x4(tide_f3(2.0f, 3.0f, -4.0f), rotation, tide_f3_splat(1.0f));
    // The supplied projection's aspect is used as-is, not fitted a second time.
    tide_draw_camera_matrices(&d, turned, tide_perspectivefov_f4x4(TIDE_PI_F * 0.5f, 2.0f, 0.5f, 10.5f));
    const tide_float3 forward = tide_f3(7.0f, 4.0f, -6.0f);
    const tide_float3 pixel = tide_draw_world_to_screen_3d(&d, forward, screen);
    TIDE_CHECK(close3(pixel, tide_f3(480.0f, 160.0f, 0.945f)));
    TIDE_CHECK(close3(tide_draw_screen_to_world_3d(&d, pixel, screen), forward));
    // An asymmetric projection's offset also survives both directions.
    tide_float4x4 shifted = tide_perspectivefov_f4x4(TIDE_PI_F * 0.5f, 2.0f, 0.5f, 10.5f);
    shifted.c2.x = 0.25f;
    tide_draw_camera_matrices(&d, turned, shifted);
    const tide_float3 shifted_pixel = tide_draw_world_to_screen_3d(&d, forward, screen);
    TIDE_CHECK(close_float(shifted_pixel.x, pixel.x - 100.0f));
    TIDE_CHECK(close3(tide_draw_screen_to_world_3d(&d, shifted_pixel, screen), forward));
    tide_draw_free(&d);
}

TIDE_TEST(draw_coordinates_rays)
{
    tide_draw_list d = {0};
    const tide_float2 screen = tide_f2(800.0f, 400.0f);
    tide_draw_camera_3d(&d, tide_f3(0.0f, 0.0f, 0.0f), tide_identity_q(), 90.0f);
    TIDE_CHECK(close3(tide_draw_screen_ray_origin(&d, tide_f2(400.0f, 200.0f), screen), tide_f3(0.0f, 0.0f, 0.3f)));
    TIDE_CHECK(close3(tide_draw_screen_ray_direction(&d, tide_f2(400.0f, 200.0f), screen), tide_f3(0.0f, 0.0f, 1.0f)));
    const tide_float2 corner = tide_f2(800.0f, 0.0f);
    const tide_float3 near = tide_draw_screen_ray_origin(&d, corner, screen);
    const tide_float3 direction = tide_draw_screen_ray_direction(&d, corner, screen);
    TIDE_CHECK(close3(near, tide_f3(0.6f, 0.3f, 0.3f)));
    TIDE_CHECK(close3(direction, tide_normalize_f3(tide_f3(2.0f, 1.0f, 1.0f))));
    TIDE_CHECK(close_float(tide_length_f3(direction), 1.0f));
    const tide_float3 along = tide_add_f3(near, tide_mul_f3(direction, tide_f3_splat(10.0f)));
    const tide_float3 pixel = tide_draw_world_to_screen_3d(&d, along, screen);
    TIDE_CHECK(close2(tide_f2(pixel.x, pixel.y), corner));

    tide_draw_camera_matrices(&d, tide_translate_f4x4(tide_f3(1.0f, 2.0f, 3.0f)),
                              tide_ortho_f4x4(8.0f, 4.0f, 0.5f, 10.5f));
    TIDE_CHECK(close3(tide_draw_screen_ray_origin(&d, tide_f2(600.0f, 100.0f), screen), tide_f3(3.0f, 3.0f, 3.5f)));
    TIDE_CHECK(close3(tide_draw_screen_ray_direction(&d, tide_f2(600.0f, 100.0f), screen), tide_f3(0.0f, 0.0f, 1.0f)));
    TIDE_CHECK(close3(tide_draw_screen_ray_direction(&d, tide_f2(0.0f, 400.0f), screen), tide_f3(0.0f, 0.0f, 1.0f)));
    tide_draw_free(&d);
}

TIDE_TEST(draw_coordinates_append_reset)
{
    tide_draw_list d = {0}, appended = {0};
    const tide_float2 screen = tide_f2(800.0f, 400.0f);
    tide_draw_camera(&d, tide_f2(100.0f, 100.0f), 1.0f);
    tide_draw_camera_3d(&d, tide_f3(0.0f, 0.0f, -5.0f), tide_identity_q(), 90.0f);
    tide_draw_camera(&appended, tide_f2(10.0f, -5.0f), 20.0f);
    tide_draw_camera_matrices(&appended, tide_translate_f4x4(tide_f3(0.0f, 0.0f, -2.0f)),
                              tide_ortho_f4x4(8.0f, 4.0f, 1.0f, 11.0f));
    tide_draw_append(&d, &appended);
    tide_draw_free(&appended);
    // Recording a large scene grows the command array without changing the
    // camera used by labels or picking afterward.
    for (uint32_t i = 0; i < 8192; i++) {
        tide_draw_circle(&d, tide_f2((float)i, 0.0f), 1.0f, TIDE_COLOR_WHITE);
    }
    TIDE_CHECK(close2(tide_draw_world_to_screen(&d, tide_f2(10.0f, -5.0f), screen), tide_f2(400.0f, 200.0f)));
    TIDE_CHECK(close3(tide_draw_world_to_screen_3d(&d, tide_f3(2.0f, 1.0f, 4.0f), screen), tide_f3(600.0f, 100.0f, 0.5f)));
    tide_draw_screen(&appended);
    tide_draw_append(&d, &appended);
    tide_draw_free(&appended);
    TIDE_CHECK(close2(tide_draw_world_to_screen(&d, tide_f2(10.0f, -5.0f), screen), tide_f2(10.0f, -5.0f)));
    TIDE_CHECK(close3(tide_draw_world_to_screen_3d(&d, tide_f3(2.0f, 1.0f, 4.0f), screen), tide_f3(600.0f, 100.0f, 0.5f)));
    tide_draw_reset(&d);
    TIDE_CHECK(close2(tide_draw_world_to_screen(&d, tide_f2(10.0f, -5.0f), screen), tide_f2(410.0f, 205.0f)));
    TIDE_CHECK(close3(tide_draw_screen_ray_origin(&d, tide_f2(400.0f, 200.0f), screen), tide_f3(0.0f, 0.0f, 0.3f)));
    TIDE_CHECK(close3(tide_draw_world_to_screen_3d(&d, tide_f3(0.0f, 0.0f, 3.0f), screen), tide_f3(400.0f, 200.0f, 0.9f)));
    tide_draw_free(&d);
}

TIDE_TEST(draw_coordinates_invalid)
{
    tide_draw_list d = {0};
    const tide_float2 screen = tide_f2(800.0f, 400.0f), zero2 = tide_f2(0.0f, 0.0f);
    const tide_float3 zero3 = tide_f3(0.0f, 0.0f, 0.0f);
    const float nan = tide_f_from_bits(0x7FC00000u), infinity = tide_f_from_bits(0x7F800000u);
    const tide_float2 invalid_screens[] = {tide_f2(0.0f, 400.0f), tide_f2(800.0f, -1.0f),
                                          tide_f2(nan, 400.0f), tide_f2(800.0f, infinity)};
    for (uint32_t i = 0; i < sizeof invalid_screens / sizeof invalid_screens[0]; i++) {
        TIDE_CHECK(close2(tide_draw_world_to_screen(&d, tide_f2(1.0f, 1.0f), invalid_screens[i]), zero2));
        TIDE_CHECK(close2(tide_draw_screen_to_world(&d, tide_f2(1.0f, 1.0f), invalid_screens[i]), zero2));
        TIDE_CHECK(close3(tide_draw_world_to_screen_3d(&d, tide_f3(1.0f, 1.0f, 1.0f), invalid_screens[i]), zero3));
        TIDE_CHECK(close3(tide_draw_screen_to_world_3d(&d, tide_f3(1.0f, 1.0f, 0.5f), invalid_screens[i]), zero3));
        TIDE_CHECK(close3(tide_draw_screen_ray_origin(&d, zero2, invalid_screens[i]), zero3));
        TIDE_CHECK(close3(tide_draw_screen_ray_direction(&d, zero2, invalid_screens[i]), zero3));
    }
    TIDE_CHECK(close2(tide_draw_world_to_screen(&d, tide_f2(nan, 1.0f), screen), zero2));
    TIDE_CHECK(close2(tide_draw_screen_to_world(&d, tide_f2(1.0f, infinity), screen), zero2));
    TIDE_CHECK(close3(tide_draw_world_to_screen_3d(&d, tide_f3(1.0f, 1.0f, nan), screen), zero3));
    TIDE_CHECK(close3(tide_draw_screen_to_world_3d(&d, tide_f3(infinity, 1.0f, 0.5f), screen), zero3));
    TIDE_CHECK(close3(tide_draw_screen_ray_direction(&d, tide_f2(nan, 1.0f), screen), zero3));
    // A perspective camera's position cannot be projected, nor its infinity
    // unprojected to a finite world position.
    TIDE_CHECK(close3(tide_draw_world_to_screen_3d(&d, zero3, screen), zero3));
    TIDE_CHECK(close3(tide_draw_screen_to_world_3d(&d, tide_f3(400.0f, 200.0f, 1.0f), screen), zero3));
    tide_draw_camera(&d, tide_f2(nan, 0.0f), 1.0f);
    TIDE_CHECK(close2(tide_draw_world_to_screen(&d, zero2, screen), zero2));
    TIDE_CHECK(close2(tide_draw_screen_to_world(&d, zero2, screen), zero2));
    tide_float4x4 singular = tide_identity_f4x4();
    singular.c0.x = 0.0f;
    tide_draw_camera_matrices(&d, tide_identity_f4x4(), singular);
    TIDE_CHECK(close3(tide_draw_world_to_screen_3d(&d, tide_f3(0.0f, 0.0f, 1.0f), screen), zero3));
    TIDE_CHECK(close3(tide_draw_screen_to_world_3d(&d, tide_f3(400.0f, 200.0f, 0.5f), screen), zero3));
    TIDE_CHECK(close3(tide_draw_screen_ray_origin(&d, zero2, screen), zero3));
    TIDE_CHECK(close3(tide_draw_screen_ray_direction(&d, zero2, screen), zero3));
    tide_draw_free(&d);
}
