#include "tide/draw.h"

static bool finite2(const tide_float2 p)
{
    return tide_is_finite_f(p.x) && tide_is_finite_f(p.y);
}

static bool finite3(const tide_float3 p)
{
    return tide_is_finite_f(p.x) && tide_is_finite_f(p.y) && tide_is_finite_f(p.z);
}

static bool finite4(const tide_float4 p)
{
    return tide_is_finite_f(p.x) && tide_is_finite_f(p.y) && tide_is_finite_f(p.z) && tide_is_finite_f(p.w);
}

static bool screen_valid(const tide_float2 screen)
{
    return finite2(screen) && screen.x > 0.0f && screen.y > 0.0f;
}

static tide_float2 target_size(const tide_draw_list *d, const tide_float2 window_size)
{
    if (!d->target) return window_size;
    if (d->target > d->target_count) return tide_f2(0.0f, 0.0f);
    const tide_draw_target_data *target = &d->targets[d->target - 1u];
    if (!tide_render_target_live(target->id)) return tide_f2(0.0f, 0.0f);
    return tide_f2((float)target->width, (float)target->height);
}

// The last 2D camera or screen command is what commands recorded now draw
// through. A 3D camera never changes it, nor does it depend on a rendered frame.
static bool camera2(const tide_draw_list *d, const tide_float2 screen, tide_float2 *center, float *scale, bool *pixels)
{
    *center = tide_f2(0.0f, 0.0f);
    *scale = 1.0f;
    *pixels = false;
    if (!d->camera_2d) return true;
    const tide_draw_command *c = &d->commands[d->camera_2d - 1u];
    if (c->kind == TIDE_DRAW_SCREEN) {
        *pixels = true;
        return true;
    }
    if (!finite2(c->a) || !tide_is_finite_f(c->b.x)) return false;
    *center = c->a;
    *scale = c->b.x > 0.0f ? screen.y / (2.0f * c->b.x) : 1.0f;
    return tide_is_finite_f(*scale) && *scale > 0.0f;
}

tide_float2 tide_draw_world_to_screen(const tide_draw_list *d, const tide_float2 point, const tide_float2 window_size)
{
    const tide_float2 screen_size = target_size(d, window_size);
    const tide_float2 zero = tide_f2(0.0f, 0.0f);
    if (!screen_valid(screen_size) || !finite2(point)) return zero;
    tide_float2 center;
    float scale;
    bool pixels;
    if (!camera2(d, screen_size, &center, &scale, &pixels)) return zero;
    if (pixels) return point;
    const tide_float2 result = tide_f2(screen_size.x * 0.5f + (point.x - center.x) * scale,
                                       screen_size.y * 0.5f - (point.y - center.y) * scale);
    return finite2(result) ? result : zero;
}

tide_float2 tide_draw_screen_to_world(const tide_draw_list *d, const tide_float2 point, const tide_float2 window_size)
{
    const tide_float2 screen_size = target_size(d, window_size);
    const tide_float2 zero = tide_f2(0.0f, 0.0f);
    if (!screen_valid(screen_size) || !finite2(point)) return zero;
    tide_float2 center;
    float scale;
    bool pixels;
    if (!camera2(d, screen_size, &center, &scale, &pixels)) return zero;
    if (pixels) return point;
    const tide_float2 result = tide_f2(center.x + (point.x - screen_size.x * 0.5f) / scale,
                                       center.y - (point.y - screen_size.y * 0.5f) / scale);
    return finite2(result) ? result : zero;
}

// Match the renderer: ordinary perspective cameras are recorded for a square
// target and fitted across; matrix cameras already have their target's aspect.
static bool camera3(const tide_draw_list *d, const tide_float2 screen, tide_float4x4 *matrix, bool *infinite)
{
    if (!screen_valid(screen)) return false;
    bool fit = true;
    if (d->camera_3d) {
        const tide_draw_command *c = &d->commands[d->camera_3d - 1u];
        *matrix = d->matrices[c->camera.matrix];
        fit = c->camera.fit != 0;
    } else {
        *matrix = tide_draw_camera_3d_matrix(tide_f3(0.0f, 0.0f, 0.0f), tide_identity_q(), 60.0f);
    }
    if (fit) {
        const float across = screen.y / screen.x;
        matrix->c0.x *= across;
        matrix->c1.x *= across;
        matrix->c2.x *= across;
        matrix->c3.x *= across;
    }
    if (!finite4(matrix->c0) || !finite4(matrix->c1) || !finite4(matrix->c2) || !finite4(matrix->c3)) return false;
    const float determinant = tide_determinant_f4x4(*matrix);
    if (infinite) *infinite = fit;
    return tide_is_finite_f(determinant) && determinant != 0.0f;
}

static bool divide(const tide_float4 point, tide_float3 *result)
{
    if (!finite4(point) || point.w == 0.0f) return false;
    *result = tide_f3(point.x / point.w, point.y / point.w, point.z / point.w);
    return finite3(*result);
}

static bool unproject(const tide_float4x4 inverse, const tide_float3 point, const tide_float2 screen, tide_float3 *result)
{
    const tide_float4 clip = tide_f4(2.0f * (point.x / screen.x) - 1.0f,
                                    1.0f - 2.0f * (point.y / screen.y), 2.0f * point.z - 1.0f, 1.0f);
    return divide(tide_mul_f4x4_f4(inverse, clip), result);
}

tide_float3 tide_draw_world_to_screen_3d(const tide_draw_list *d, const tide_float3 point, const tide_float2 window_size)
{
    const tide_float2 screen_size = target_size(d, window_size);
    const tide_float3 zero = tide_f3(0.0f, 0.0f, 0.0f);
    tide_float4x4 matrix;
    tide_float3 clip;
    if (!finite3(point) || !camera3(d, screen_size, &matrix, NULL)
        || !divide(tide_mul_f4x4_f4(matrix, tide_f4(point.x, point.y, point.z, 1.0f)), &clip)) return zero;
    const tide_float3 result = tide_f3((clip.x * 0.5f + 0.5f) * screen_size.x,
                                       (0.5f - clip.y * 0.5f) * screen_size.y, clip.z * 0.5f + 0.5f);
    return finite3(result) ? result : zero;
}

tide_float3 tide_draw_screen_to_world_3d(const tide_draw_list *d, const tide_float3 point, const tide_float2 window_size)
{
    const tide_float2 screen_size = target_size(d, window_size);
    tide_float4x4 matrix;
    tide_float3 result;
    bool infinite;
    if (!finite3(point) || !camera3(d, screen_size, &matrix, &infinite)
        // Inverting a moved or rotated camera can round infinity's w away
        // from zero. Its far depth still has no finite world position.
        || (infinite && point.z == 1.0f)
        || !unproject(tide_inverse_f4x4(matrix), point, screen_size, &result)) return tide_f3(0.0f, 0.0f, 0.0f);
    return result;
}

tide_float3 tide_draw_screen_ray_origin(const tide_draw_list *d, const tide_float2 point, const tide_float2 screen_size)
{
    return tide_draw_screen_to_world_3d(d, tide_f3(point.x, point.y, 0.0f), screen_size);
}

tide_float3 tide_draw_screen_ray_direction(const tide_draw_list *d, const tide_float2 point, const tide_float2 window_size)
{
    const tide_float2 screen_size = target_size(d, window_size);
    const tide_float3 zero = tide_f3(0.0f, 0.0f, 0.0f);
    tide_float4x4 matrix;
    tide_float3 near, middle;
    if (!finite2(point) || !camera3(d, screen_size, &matrix, NULL)) return zero;
    const tide_float4x4 inverse = tide_inverse_f4x4(matrix);
    // A depth of one is at infinity for the standard perspective camera.
    if (!unproject(inverse, tide_f3(point.x, point.y, 0.0f), screen_size, &near)
        || !unproject(inverse, tide_f3(point.x, point.y, 0.5f), screen_size, &middle)) return zero;
    const tide_float3 direction = tide_sub_f3(middle, near);
    if (!finite3(direction)) return zero;
    const float largest = tide_max_f(tide_abs_f(direction.x), tide_max_f(tide_abs_f(direction.y), tide_abs_f(direction.z)));
    if (largest == 0.0f) return zero;
    // Scale first so long rays do not overflow the length's square.
    return tide_normalize_f3(tide_div_f3(direction, tide_f3_splat(largest)));
}
