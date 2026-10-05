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

TIDE_TEST(draw_coordinates_targets_2d)
{
    tide_draw_list d = {0};
    const tide_float2 window = tide_f2(800.0f, 400.0f);
    const tide_float2 point = tide_f2(40.0f, 0.0f);
    tide_draw_camera(&d, tide_f2(10.0f, -5.0f), 20.0f);
    tide_draw_to(&d, 101, 200, 100);
    TIDE_CHECK(close2(tide_draw_world_to_screen(&d, point, window), tide_f2(140.0f, 50.0f)));
    tide_draw_camera(&d, tide_f2(1.0f, 2.0f), 5.0f);
    const tide_float2 target_point = tide_f2(4.0f, 3.0f), pixel = tide_f2(130.0f, 40.0f);
    TIDE_CHECK(close2(tide_draw_world_to_screen(&d, target_point, window), pixel));
    TIDE_CHECK(close2(tide_draw_screen_to_world(&d, pixel, window), target_point));
    // A target's size is independent of the caller's window-size fallback.
    TIDE_CHECK(close2(tide_draw_world_to_screen(&d, target_point, tide_f2(0.0f, 0.0f)), pixel));

    tide_draw_to(&d, 102, 64, 128);
    TIDE_CHECK(close2(tide_draw_world_to_screen(&d, point, window), tide_f2(72.0f, 64.0f)));
    tide_draw_screen(&d);
    tide_draw_camera_3d(&d, tide_f3(0.0f, 0.0f, -10.0f), tide_identity_q(), 90.0f);
    TIDE_CHECK(close2(tide_draw_world_to_screen(&d, target_point, window), target_point));
    TIDE_CHECK(close2(tide_draw_screen_to_world(&d, target_point, window), target_point));
    tide_draw_to(&d, 101, 200, 100);
    TIDE_CHECK(close2(tide_draw_world_to_screen(&d, target_point, window), pixel));
    tide_draw_to(&d, 0, 0, 0);
    TIDE_CHECK(close2(tide_draw_world_to_screen(&d, point, window), tide_f2(700.0f, 150.0f)));
    tide_draw_to(&d, 102, 64, 128);
    TIDE_CHECK(close2(tide_draw_world_to_screen(&d, target_point, window), target_point));
    tide_draw_free(&d);
}

TIDE_TEST(draw_coordinates_targets_3d)
{
    tide_draw_list d = {0};
    const tide_float2 window = tide_f2(800.0f, 400.0f);
    const tide_float3 point = tide_f3(2.0f, 1.0f, 2.0f);
    tide_draw_camera_3d(&d, tide_f3(0.0f, 0.0f, -5.0f), tide_identity_q(), 90.0f);
    const tide_float3 window_pixel = tide_draw_world_to_screen_3d(&d, point, window);
    tide_draw_to(&d, 201, 200, 100);
    // A new target starts with the default camera, independent of the window.
    TIDE_CHECK(close3(tide_draw_screen_ray_origin(&d, tide_f2(100.0f, 50.0f), window), tide_f3(0.0f, 0.0f, 0.3f)));
    tide_draw_camera_3d(&d, tide_f3(0.0f, 0.0f, 0.0f), tide_identity_q(), 90.0f);
    const tide_float3 pixel = tide_f3(150.0f, 25.0f, 0.85f);
    TIDE_CHECK(close3(tide_draw_world_to_screen_3d(&d, point, window), pixel));
    TIDE_CHECK(close3(tide_draw_screen_to_world_3d(&d, pixel, window), point));
    TIDE_CHECK(close3(tide_draw_screen_ray_origin(&d, tide_f2(200.0f, 0.0f), window), tide_f3(0.6f, 0.3f, 0.3f)));
    TIDE_CHECK(close3(tide_draw_screen_ray_direction(&d, tide_f2(200.0f, 0.0f), window),
                     tide_normalize_f3(tide_f3(2.0f, 1.0f, 1.0f))));
    tide_draw_camera(&d, tide_f2(30.0f, 40.0f), 2.0f);
    tide_draw_screen(&d);
    TIDE_CHECK(close3(tide_draw_world_to_screen_3d(&d, point, window), pixel));

    tide_draw_to(&d, 202, 300, 150);
    tide_draw_camera_matrices(&d, tide_translate_f4x4(tide_f3(3.0f, 4.0f, -5.0f)),
                              tide_ortho_f4x4(8.0f, 4.0f, 1.0f, 11.0f));
    const tide_float3 matrix_point = tide_f3(5.0f, 5.0f, 1.0f), matrix_pixel = tide_f3(225.0f, 37.5f, 0.5f);
    TIDE_CHECK(close3(tide_draw_world_to_screen_3d(&d, matrix_point, window), matrix_pixel));
    TIDE_CHECK(close3(tide_draw_screen_to_world_3d(&d, matrix_pixel, window), matrix_point));
    TIDE_CHECK(close3(tide_draw_screen_ray_origin(&d, tide_f2(150.0f, 75.0f), window), tide_f3(3.0f, 4.0f, -4.0f)));
    TIDE_CHECK(close3(tide_draw_screen_ray_direction(&d, tide_f2(300.0f, 0.0f), window), tide_f3(0.0f, 0.0f, 1.0f)));
    tide_draw_to(&d, 0, 0, 0);
    TIDE_CHECK(close3(tide_draw_world_to_screen_3d(&d, point, window), window_pixel));
    tide_draw_to(&d, 201, 200, 100);
    TIDE_CHECK(close3(tide_draw_world_to_screen_3d(&d, point, window), pixel));
    tide_draw_to(&d, 202, 300, 150);
    TIDE_CHECK(close3(tide_draw_world_to_screen_3d(&d, matrix_point, window), matrix_pixel));
    tide_draw_free(&d);
}

TIDE_TEST(draw_coordinates_targets_append_reset)
{
    tide_draw_list d = {0}, from = {0};
    const tide_float2 window = tide_f2(800.0f, 400.0f);
    tide_draw_camera(&d, tide_f2(100.0f, 100.0f), 1.0f);
    tide_draw_to(&d, 301, 200, 100);
    tide_draw_camera(&d, tide_f2(-20.0f, -30.0f), 2.0f);
    tide_draw_camera(&from, tide_f2(10.0f, -5.0f), 20.0f);
    tide_draw_to(&from, 301, 200, 100);
    tide_draw_camera(&from, tide_f2(3.0f, -2.0f), 10.0f);
    tide_draw_camera_matrices(&from, tide_translate_f4x4(tide_f3(0.0f, 0.0f, -2.0f)),
                              tide_ortho_f4x4(8.0f, 4.0f, 1.0f, 11.0f));
    tide_draw_to(&from, 302, 80, 60);
    tide_draw_screen(&from);
    tide_draw_to(&from, 301, 200, 100);
    tide_draw_append(&d, &from);
    tide_draw_free(&from);
    for (uint32_t i = 0; i < 8192; i++) tide_draw_circle(&d, tide_f2(0.0f, 0.0f), 1.0f, TIDE_COLOR_WHITE);
    TIDE_CHECK(close2(tide_draw_world_to_screen(&d, tide_f2(5.0f, 0.0f), window), tide_f2(110.0f, 40.0f)));
    TIDE_CHECK(close3(tide_draw_world_to_screen_3d(&d, tide_f3(2.0f, 1.0f, 3.0f), window), tide_f3(150.0f, 25.0f, 0.4f)));
    tide_draw_to(&d, 0, 0, 0);
    TIDE_CHECK(close2(tide_draw_world_to_screen(&d, tide_f2(40.0f, 0.0f), window), tide_f2(700.0f, 150.0f)));
    tide_draw_to(&d, 302, 80, 60);
    TIDE_CHECK(close2(tide_draw_world_to_screen(&d, tide_f2(5.0f, 0.0f), window), tide_f2(5.0f, 0.0f)));
    tide_draw_reset(&d);
    tide_draw_to(&d, 301, 200, 100);
    TIDE_CHECK(close2(tide_draw_world_to_screen(&d, tide_f2(5.0f, 0.0f), window), tide_f2(105.0f, 50.0f)));
    TIDE_CHECK(close3(tide_draw_screen_ray_origin(&d, tide_f2(100.0f, 50.0f), window), tide_f3(0.0f, 0.0f, 0.3f)));
    tide_draw_free(&d);
}

TIDE_TEST(draw_coordinates_targets_lifecycle)
{
    tide_draw_list d = {0};
    const tide_float2 window = tide_f2(800.0f, 400.0f), zero2 = tide_f2(0.0f, 0.0f);
    const tide_float3 zero3 = tide_f3(0.0f, 0.0f, 0.0f);
    const tide_render_target target = tide_render_target_make(200, 100), alias = target;
    tide_draw_render_target(&d, alias);
    tide_draw_camera(&d, tide_f2(10.0f, 20.0f), 5.0f);
    TIDE_CHECK(close2(tide_draw_world_to_screen(&d, tide_f2(10.0f, 20.0f), window), tide_f2(100.0f, 50.0f)));
    tide_render_target_resize(target, 120, 240);
    tide_draw_render_target(&d, alias);
    // Resize selects a fresh attachment and the same defaults as playback.
    TIDE_CHECK(close2(tide_draw_world_to_screen(&d, zero2, window), tide_f2(60.0f, 120.0f)));
    tide_draw_cube(&d, tide_f3(0.0f, 0.0f, 2.0f), tide_f3_splat(1.0f), TIDE_COLOR_WHITE);
    TIDE_CHECK(d.commands[d.count - 1u].kind == TIDE_DRAW_MESH_3D && d.target != 0);
    tide_render_target_dispose(target);
    TIDE_CHECK(close2(tide_draw_world_to_screen(&d, zero2, window), zero2));
    TIDE_CHECK(close2(tide_draw_screen_to_world(&d, zero2, window), zero2));
    TIDE_CHECK(close3(tide_draw_world_to_screen_3d(&d, tide_f3(0.0f, 0.0f, 2.0f), window), zero3));
    TIDE_CHECK(close3(tide_draw_screen_to_world_3d(&d, tide_f3(60.0f, 120.0f, 0.5f), window), zero3));
    TIDE_CHECK(close3(tide_draw_screen_ray_origin(&d, zero2, window), zero3));
    TIDE_CHECK(close3(tide_draw_screen_ray_direction(&d, zero2, window), zero3));
    tide_draw_render_target(&d, (tide_render_target){0});
    TIDE_CHECK(close2(tide_draw_world_to_screen(&d, zero2, window), zero2));
    tide_draw_to(&d, 401, 0, 100);
    TIDE_CHECK(close2(tide_draw_world_to_screen(&d, zero2, window), zero2));
    tide_draw_render_target(&d, (tide_render_target){.id = TIDE_RENDER_WINDOW});
    TIDE_CHECK(close2(tide_draw_world_to_screen(&d, zero2, window), tide_f2(400.0f, 200.0f)));
    tide_draw_free(&d);
    tide_render_targets_free();
}

TIDE_TEST(draw_coordinates_targets_forget)
{
    tide_draw_list d = {0};
    const tide_float2 window = tide_f2(800.0f, 400.0f);
    tide_draw_camera(&d, tide_f2(10.0f, -5.0f), 20.0f);
    tide_draw_camera_3d(&d, tide_f3(0.0f, 0.0f, -5.0f), tide_identity_q(), 90.0f);
    tide_draw_clip(&d, (tide_rect){0.0f, 0.0f, 10.0f, 10.0f});
    tide_draw_to(&d, 501, 200, 100);
    tide_draw_screen(&d);
    tide_draw_camera_3d(&d, tide_f3(100.0f, 200.0f, 300.0f), tide_identity_q(), 45.0f);
    tide_draw_forget(&d);
    TIDE_REQUIRE(d.count >= 2);
    TIDE_CHECK(d.commands[d.count - 2u].kind == TIDE_DRAW_TARGET && d.commands[d.count - 2u].target == 0);
    TIDE_CHECK(d.commands[d.count - 1u].kind == TIDE_DRAW_NO_CLIP && d.clipped == 0);
    TIDE_CHECK(close2(tide_draw_world_to_screen(&d, tide_f2(40.0f, 0.0f), window), tide_f2(700.0f, 150.0f)));
    TIDE_CHECK(close3(tide_draw_screen_ray_origin(&d, tide_f2(400.0f, 200.0f), window), tide_f3(0.0f, 0.0f, -4.7f)));
    tide_draw_circle(&d, tide_f2(40.0f, 0.0f), 1.0f, TIDE_COLOR_WHITE);
    TIDE_CHECK(d.commands[d.count - 1u].kind == TIDE_DRAW_CIRCLE && d.target == 0);
    tide_draw_free(&d);
}

// What says how big text is drawn, standing in for a renderer: it notes where
// it was asked to measure.
static tide_target_size measured_in;
static bool measured_in_window;

static tide_float2 note_measure(const char *text, const float size, const tide_target_size *target, const unsigned char *font)
{
    (void)text, (void)font;
    measured_in_window = target == NULL;
    if (target) measured_in = *target;
    return tide_f2(size * 3.0f, size);
}

// A target is in logical pixels, like the window, whatever its image's scale:
// conversions don't read the scale, and one that follows the window is the
// window's size times its factor, as the window is now.
TIDE_TEST(draw_coordinates_targets_are_in_logical_pixels)
{
    tide_draw_list d = {0};
    const tide_float2 window = tide_f2(800.0f, 400.0f), wider = tide_f2(1000.0f, 600.0f);
    const tide_render_target dense = tide_render_target_make_scaled(200.0f, 100.0f, 3.0f);
    const tide_render_target half = tide_render_target_screen(0.5f);

    tide_draw_render_target(&d, dense);
    tide_draw_camera(&d, tide_f2(1.0f, 2.0f), 5.0f);
    TIDE_CHECK(close2(tide_draw_world_to_screen(&d, tide_f2(4.0f, 3.0f), window), tide_f2(130.0f, 40.0f)));
    TIDE_CHECK(close2(tide_draw_screen_to_world(&d, tide_f2(130.0f, 40.0f), window), tide_f2(4.0f, 3.0f)));
    // Its text is measured where it's drawn: in the target, at its scale
    const tide_float2 text = tide_draw_measure_text(&d, "Tide", 2.0f, TIDE_FONT_REGULAR, window, note_measure);
    TIDE_CHECK(close2(text, tide_f2(6.0f, 2.0f)));
    TIDE_CHECK(!measured_in_window && measured_in.width == 200.0f && measured_in.scale == 3.0f);

    // Half the window: 400 by 200 of an 800 by 400 one, with its middle at (200, 100)
    tide_draw_render_target(&d, half);
    tide_draw_camera(&d, tide_f2(0.0f, 0.0f), 50.0f);
    TIDE_CHECK(close2(tide_draw_world_to_screen(&d, tide_f2(0.0f, 0.0f), window), tide_f2(200.0f, 100.0f)));
    TIDE_CHECK(close2(tide_draw_world_to_screen(&d, tide_f2(50.0f, 50.0f), window), tide_f2(300.0f, 0.0f)));
    // ...and 500 by 300 of a 1000 by 600 one, with no Resize in between
    TIDE_CHECK(close2(tide_draw_world_to_screen(&d, tide_f2(0.0f, 0.0f), wider), tide_f2(250.0f, 150.0f)));
    TIDE_CHECK(close2(tide_draw_world_to_screen(&d, tide_f2(50.0f, 50.0f), wider), tide_f2(400.0f, 0.0f)));
    TIDE_CHECK(close3(tide_draw_world_to_screen_3d(&d, tide_f3(0.0f, 0.0f, 2.0f), wider), tide_f3(250.0f, 150.0f, 0.85f)));
    tide_draw_measure_text(&d, "Tide", 2.0f, TIDE_FONT_REGULAR, window, note_measure);
    TIDE_CHECK(!measured_in_window && measured_in.window == 0.5f);
    // With no window to follow, it has no size
    TIDE_CHECK(close2(tide_draw_world_to_screen(&d, tide_f2(5.0f, 5.0f), tide_f2(0.0f, 0.0f)), tide_f2(0.0f, 0.0f)));

    tide_draw_render_target(&d, (tide_render_target){.id = TIDE_RENDER_WINDOW});
    tide_draw_measure_text(&d, "Tide", 2.0f, TIDE_FONT_REGULAR, window, note_measure);
    TIDE_CHECK(measured_in_window);
    tide_draw_free(&d);
    tide_render_targets_free();
}
