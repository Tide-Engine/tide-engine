#pragma once

// The frustum test of an instance's box (draw.c). The exact test works in
// double precision, an instance at a time. The quick test works in float, four
// instances at once, and answers only where its error bound proves the exact
// test gives the same answer: anything closer to a plane than that, or too
// large for float, or not affine, goes to the exact test. So the answers are
// the exact test's, whichever gave them.

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "tide/draw.h"

// What testing one instance gives
enum { CULL_DRAWN, CULL_REJECTED, CULL_UNTESTABLE };

typedef float cull_f4 __attribute__((ext_vector_type(4)));
typedef float cull_f8 __attribute__((ext_vector_type(8)));
typedef int32_t cull_i4 __attribute__((ext_vector_type(4)));
typedef int32_t cull_i8 __attribute__((ext_vector_type(8)));

// Each plane's rows in tide_draw_frustum.quick: its constant, normal and the normal's magnitude, then for the
// pass and the reject test the coefficient of the box's size and the bound compared with.
enum {
    CULL_W, CULL_NX, CULL_NY, CULL_NZ, CULL_AX, CULL_AY, CULL_AZ,
    CULL_PASS_K, CULL_PASS, CULL_REJECT_K, CULL_REJECT,
    CULL_ROWS
};
_Static_assert(CULL_ROWS * 4 * sizeof(float) == sizeof(((tide_draw_frustum *)0)->quick[0])
               && CULL_ROWS * 8 * sizeof(float) == sizeof(((tide_draw_frustum *)0)->lanes),
               "tide_draw_frustum.quick has each row in four lanes, and lanes a plane to a lane");

// The quick test's slack: its float rounding is under 2^-20 of the magnitudes it sums (fewer than 16 roundings
// of 2^-24 each), and the exact test's double rounding is far under that. Planes or boxes past these sizes could
// overflow a float, and go to the exact test.
#define CULL_SLACK 0x1p-16
#define CULL_PLANE_LIMIT 1e8
#define CULL_BOX_LIMIT 1e28f

// The camera's float coefficients for the quick test, from its double planes and rounding coefficients.
// The exact test (cull_exact) rejects a box when, for a plane, distance < -1e-5 * (error + 1), with
// error = r3 + sum(r[axis] * magnitude[axis]) >= r3, magnitude being the box's along each axis. With
// S = |p3| + sum(|n[axis]| * magnitude[axis]), the exact test's distance and the quick one's differ by
// under 2^-20 * S. With `size` the sum of the three magnitudes, S <= |p3| + max|n| * size and
// error <= r3 + max(r) * size, so the quick test passes a plane where
//   distance - 2^-16 * (|p3| + max|n| * size) >= -1e-5 * (1 - 2^-12) * (1 + r3)
// and rejects where
//   distance + 2^-16 * (|p3| + max|n| * size) < -1e-5 * (1 + 2^-12) * (1 + r3 + max(r) * size),
// each as distance + k * size compared with a constant.
static inline void cull_quick_build(tide_draw_frustum *f)
{
    f->quick_ready = 0;
    // Lanes past the planes pass: 1 >= 0, never 1 < -1
    for (uint32_t i = 0; i < CULL_ROWS; i++) {
        for (uint32_t lane = 0; lane < 8; lane++) f->lanes[i][lane] = 0.0f;
    }
    for (uint32_t lane = 0; lane < 8; lane++) f->lanes[CULL_W][lane] = 1.0f, f->lanes[CULL_REJECT][lane] = -1.0f;
    const double low = 1e-5 * (1.0 - 0x1p-12), high = 1e-5 * (1.0 + 0x1p-12);
    for (uint32_t plane = 0; plane < f->count; plane++) {
        const double *p = f->plane[plane], *r = f->magnitude[plane];
        for (uint32_t axis = 0; axis < 4; axis++) {
            if (!(p[axis] <= CULL_PLANE_LIMIT && p[axis] >= -CULL_PLANE_LIMIT && r[axis] <= CULL_PLANE_LIMIT
                  && r[axis] >= 0.0)) return;
        }
        double row[CULL_ROWS], normal = 0.0, rounding = 0.0;
        for (uint32_t axis = 0; axis < 3; axis++) {
            const double n = p[axis] < 0.0 ? -p[axis] : p[axis];
            row[CULL_NX + axis] = p[axis];
            row[CULL_AX + axis] = n;
            if (n > normal) normal = n;
            if (r[axis] > rounding) rounding = r[axis];
        }
        const double w = p[3] < 0.0 ? -p[3] : p[3];
        row[CULL_W] = p[3];
        row[CULL_PASS_K] = -CULL_SLACK * normal;
        row[CULL_PASS] = CULL_SLACK * w - low * (1.0 + r[3]);
        row[CULL_REJECT_K] = CULL_SLACK * normal + high * rounding;
        row[CULL_REJECT] = -CULL_SLACK * w - high * (1.0 + r[3]);
        for (uint32_t i = 0; i < CULL_ROWS; i++) {
            for (uint32_t lane = 0; lane < 4; lane++) f->quick[plane][i][lane] = (float)row[i];
            f->lanes[i][plane] = (float)row[i];
        }
    }
    f->quick_ready = 1;
}

// A camera's planes from its world-to-clip matrix (column-major).
static inline void cull_frustum_build(tide_draw_frustum *f, const float c[16])
{
    f->count = 0;
    bool valid = true;
    for (uint32_t i = 0; i < 16; i++) valid &= tide_is_finite_f(c[i]);
    for (uint32_t plane = 0; valid && plane < 6; plane++) {
        const uint32_t row = plane / 2u;
        const double sign = plane & 1u ? -1.0 : 1.0;
        double *p = f->plane[f->count], *error = f->magnitude[f->count];
        for (uint32_t axis = 0; axis < 4; axis++) {
            const double a = c[axis * 4 + 3], b = c[axis * 4 + row];
            p[axis] = a + sign * b;
            error[axis] = (a < 0.0 ? -a : a) + (b < 0.0 ? -b : b);
        }
        // Infinite far projections (and an unknown size's zero matrix) have no far rejection plane.
        if (p[0] != 0.0 || p[1] != 0.0 || p[2] != 0.0 || p[3] < 0.0) f->count++;
    }
    cull_quick_build(f);
}

// Conservative support test of the transformed box against the clip planes.
// Double intermediates avoid overflow for arbitrary finite game values.
// The error allowance also keeps boxes touching a plane despite GPU rounding.
// `w` is the whole transform, column-major.
static inline int cull_exact(const tide_draw_bounds *bounds, const tide_draw_frustum *camera, const float w[16])
{
    for (uint32_t i = 0; i < 16; i++) {
        if (!tide_is_finite_f(w[i])) return CULL_UNTESTABLE;
    }
    if (w[3] != 0.0f || w[7] != 0.0f || w[11] != 0.0f || w[15] != 1.0f) return CULL_UNTESTABLE;
    const double center[3] = {bounds->center.x, bounds->center.y, bounds->center.z};
    const double extent[3] = {bounds->extents.x, bounds->extents.y, bounds->extents.z};
    double at[3], radius[3];
    for (uint32_t row = 0; row < 3; row++) {
        at[row] = w[12 + row];
        radius[row] = 0.0;
        for (uint32_t axis = 0; axis < 3; axis++) {
            const double scale = w[axis * 4 + row];
            at[row] += scale * center[axis];
            radius[row] += (scale < 0.0 ? -scale : scale) * extent[axis];
        }
    }
    // The rounding allowance is never less than 0, so a box on a plane's inner side, or on it, is in front of
    // it whatever the allowance is: it's worked out only for one past a plane, once
    double magnitude[3];
    bool measured = false;
    for (uint32_t plane = 0; plane < camera->count; plane++) {
        const double *p = camera->plane[plane], *rounding = camera->magnitude[plane];
        double distance = p[3];
        for (uint32_t axis = 0; axis < 3; axis++) {
            const double n = p[axis];
            distance += n * at[axis] + (n < 0.0 ? -n : n) * radius[axis];
        }
        if (!(distance < 0.0)) continue;
        if (!measured) {
            for (uint32_t row = 0; row < 3; row++) {
                magnitude[row] = tide_abs_f(w[12 + row]);
                for (uint32_t axis = 0; axis < 3; axis++) {
                    const double scale = w[axis * 4 + row];
                    const double part = scale * center[axis];
                    magnitude[row] += (part < 0.0 ? -part : part) + (scale < 0.0 ? -scale : scale) * extent[axis];
                }
            }
            measured = true;
        }
        double error = rounding[3];
        for (uint32_t axis = 0; axis < 3; axis++) error += rounding[axis] * magnitude[axis];
        if (distance < -1e-5 * (error + 1.0)) return CULL_REJECTED;
    }
    return CULL_DRAWN;
}

// One box's numbers, for the instances of a call.
typedef struct cull_box {
    const tide_draw_bounds *bounds;
    float cx, cy, cz, ex, ey, ez, ux, uy, uz; // Center, extents, |center|
    bool testable, quick;
} cull_box;

static inline cull_box cull_box_of(const tide_draw_bounds *bounds, const tide_draw_frustum *camera)
{
    cull_box b = {.bounds = bounds};
    b.testable = bounds->set == 1 && camera->count;
    if (!b.testable) return b;
    b.cx = bounds->center.x, b.cy = bounds->center.y, b.cz = bounds->center.z;
    b.ex = bounds->extents.x, b.ey = bounds->extents.y, b.ez = bounds->extents.z;
    b.ux = tide_abs_f(b.cx), b.uy = tide_abs_f(b.cy), b.uz = tide_abs_f(b.cz);
    // Bounds are set only when finite with extents at least 0 (tide_draw_set_bounds, mesh_bounds)
    b.quick = camera->quick_ready && b.ex >= 0.0f && b.ey >= 0.0f && b.ez >= 0.0f && tide_is_finite_f(b.cx)
        && tide_is_finite_f(b.cy) && tide_is_finite_f(b.cz) && tide_is_finite_f(b.ex) && tide_is_finite_f(b.ey)
        && tide_is_finite_f(b.ez);
    return b;
}

static inline cull_f4 cull_abs4(const cull_f4 v) { return (cull_f4)((cull_i4)v & 0x7FFFFFFF); }

static inline cull_f4 cull_load4(const float *v)
{
    cull_f4 r;
    memcpy(&r, v, sizeof r);
    return r;
}

// An instance's columns, under `through` when there's one, with tide_mul_f4x4's arithmetic lane for lane:
// ((t0 * x + t1 * y) + (t2 * z + t3 * w)) for each column.
static inline __attribute__((always_inline)) void cull_columns(cull_f4 c[4], const float *own,
                                                               const tide_float4x4 *through)
{
#pragma clang loop unroll(full)
    for (uint32_t i = 0; i < 4; i++) c[i] = cull_load4(own + i * 4u);
    if (!through) return;
    const float *t = (const float *)through;
    const cull_f4 t0 = cull_load4(t), t1 = cull_load4(t + 4), t2 = cull_load4(t + 8), t3 = cull_load4(t + 12);
#pragma clang loop unroll(full)
    for (uint32_t i = 0; i < 4; i++) c[i] = (t0 * c[i].x + t1 * c[i].y) + (t2 * c[i].z + t3 * c[i].w);
}

// An instance, as cull_exact of its whole transform says
static __attribute__((noinline)) int cull_slow(const tide_draw_frustum *f, const cull_box *b, const float *own,
                                               const tide_float4x4 *through)
{
    cull_f4 c[4];
    cull_columns(c, own, through);
    float w[16];
    memcpy(w, c, sizeof w);
    return cull_exact(b->bounds, f, w);
}

// Where one instance's box is (`at`), its radius along each axis, and the sum of its magnitudes along the three
// (`size`, over CULL_BOX_LIMIT or NaN where an infinity or NaN is among the numbers: an infinity times 0 is NaN).
// -1 for an affine transform within the limits, 0 for any other.
static inline __attribute__((always_inline)) int32_t cull_measure(const cull_box *b, const cull_f4 c[4], cull_f4 *at,
                                                                  cull_f4 *radius, float *size)
{
    const cull_f4 a0 = cull_abs4(c[0]), a1 = cull_abs4(c[1]), a2 = cull_abs4(c[2]);
    *at = ((c[3] + c[0] * b->cx) + c[1] * b->cy) + c[2] * b->cz;
    *radius = (a0 * b->ex + a1 * b->ey) + a2 * b->ez;
    const cull_f4 magnitude = (((cull_abs4(c[3]) + a0 * b->ux) + a1 * b->uy) + a2 * b->uz) + *radius;
    *size = (magnitude.x + magnitude.y) + magnitude.z;
    const cull_f4 last = {c[0].w, c[1].w, c[2].w, c[3].w}, affine = {0.0f, 0.0f, 0.0f, 1.0f};
    const cull_f4 sized = {magnitude.x, magnitude.y, magnitude.z, 0.0f};
    const cull_i4 ok = (sized <= CULL_BOX_LIMIT) & (last == affine);
    return ok.x & ok.y & ok.z & ok.w;
}

static inline uint32_t cull_bits(const cull_i4 m)
{
    return (uint32_t)(m.x & 1) | (uint32_t)(m.y & 2) | (uint32_t)(m.z & 4) | (uint32_t)(m.w & 8);
}

static inline cull_f4 cull_row(const tide_draw_frustum *f, const uint32_t plane, const uint32_t row)
{
    return cull_load4(f->quick[plane][row]);
}

// The quick test of four instances' columns: bits 0 to 3 for those it rejects, 4 to 7 for those it draws.
static inline __attribute__((always_inline)) uint32_t cull_quick(const tide_draw_frustum *f, const cull_box *b,
                                                                 const cull_f4 c[4][4])
{
    cull_f4 at[4], radius[4];
    float sizes[4];
    cull_i4 ok;
#pragma clang loop unroll(full)
    for (uint32_t i = 0; i < 4; i++) ok[i] = cull_measure(b, c[i], &at[i], &radius[i], &sizes[i]);
    const cull_f4 size = cull_load4(sizes);
    // A lane each
    const cull_f4 xy01 = __builtin_shufflevector(at[0], at[1], 0, 4, 1, 5), xy23 = __builtin_shufflevector(at[2], at[3], 0, 4, 1, 5);
    const cull_f4 zw01 = __builtin_shufflevector(at[0], at[1], 2, 6, 3, 7), zw23 = __builtin_shufflevector(at[2], at[3], 2, 6, 3, 7);
    const cull_f4 x = __builtin_shufflevector(xy01, xy23, 0, 1, 4, 5), y = __builtin_shufflevector(xy01, xy23, 2, 3, 6, 7);
    const cull_f4 z = __builtin_shufflevector(zw01, zw23, 0, 1, 4, 5);
    const cull_f4 rxy01 = __builtin_shufflevector(radius[0], radius[1], 0, 4, 1, 5);
    const cull_f4 rxy23 = __builtin_shufflevector(radius[2], radius[3], 0, 4, 1, 5);
    const cull_f4 rzw01 = __builtin_shufflevector(radius[0], radius[1], 2, 6, 3, 7);
    const cull_f4 rzw23 = __builtin_shufflevector(radius[2], radius[3], 2, 6, 3, 7);
    const cull_f4 rx = __builtin_shufflevector(rxy01, rxy23, 0, 1, 4, 5), ry = __builtin_shufflevector(rxy01, rxy23, 2, 3, 6, 7);
    const cull_f4 rz = __builtin_shufflevector(rzw01, rzw23, 0, 1, 4, 5);
    cull_i4 rejected = {0, 0, 0, 0}, drawn = ok;
    for (uint32_t plane = 0; plane < f->count; plane++) {
        const cull_f4 distance = cull_row(f, plane, CULL_W) + cull_row(f, plane, CULL_NX) * x
            + cull_row(f, plane, CULL_NY) * y + cull_row(f, plane, CULL_NZ) * z + cull_row(f, plane, CULL_AX) * rx
            + cull_row(f, plane, CULL_AY) * ry + cull_row(f, plane, CULL_AZ) * rz;
        rejected |= distance + cull_row(f, plane, CULL_REJECT_K) * size < cull_row(f, plane, CULL_REJECT);
        drawn &= distance + cull_row(f, plane, CULL_PASS_K) * size >= cull_row(f, plane, CULL_PASS);
    }
    return cull_bits(rejected & ok) | cull_bits(drawn & ~rejected) << 4;
}

// Four instances (one can be there more than once), `own` their 16 floats each (column-major, anywhere), under
// `through` (or none): CULL_DRAWN, CULL_REJECTED or CULL_UNTESTABLE for each of the first `count`, as cull_exact
// of tide_mul_f4x4(*through, own) says.
static inline __attribute__((always_inline)) void cull_four(const tide_draw_frustum *f, const cull_box *b,
                                                            const float *const own[4], const tide_float4x4 *through,
                                                            const uint32_t count, int result[4])
{
    if (!b->testable) {
        for (uint32_t i = 0; i < 4; i++) result[i] = CULL_UNTESTABLE;
        return;
    }
    uint32_t quick = 0;
    if (b->quick) {
        cull_f4 c[4][4];
#pragma clang loop unroll(full)
        for (uint32_t i = 0; i < 4; i++) cull_columns(c[i], own[i], through);
        quick = cull_quick(f, b, c);
    }
#pragma clang loop unroll(full)
    for (uint32_t i = 0; i < 4; i++) {
        result[i] = quick >> i & 1u ? CULL_REJECTED
            : quick >> (i + 4u) & 1u ? CULL_DRAWN
            : i < count ? cull_slow(f, b, own[i], through) : CULL_UNTESTABLE;
    }
}

static inline cull_f8 cull_lanes(const tide_draw_frustum *f, const uint32_t row)
{
    cull_f8 r;
    memcpy(&r, f->lanes[row], sizeof r);
    return r;
}

static inline bool cull_any8(const cull_i8 m)
{
    const cull_i4 both = m.s0123 | m.s4567;
    return (both.x | both.y | both.z | both.w) != 0;
}

static inline bool cull_all8(const cull_i8 m)
{
    const cull_i4 both = m.s0123 & m.s4567;
    return (both.x & both.y & both.z & both.w) != 0;
}

// One instance, as cull_four's lanes would say it, with the planes in lanes instead
static inline int cull_instance(const tide_draw_frustum *f, const cull_box *b, const float *own,
                                const tide_float4x4 *through)
{
    if (!b->testable) return CULL_UNTESTABLE;
    if (b->quick) {
        cull_f4 c[4], at, radius;
        float size;
        cull_columns(c, own, through);
        const bool ok = cull_measure(b, c, &at, &radius, &size) != 0;
        const cull_f8 distance = cull_lanes(f, CULL_W) + cull_lanes(f, CULL_NX) * at.x + cull_lanes(f, CULL_NY) * at.y
            + cull_lanes(f, CULL_NZ) * at.z + cull_lanes(f, CULL_AX) * radius.x + cull_lanes(f, CULL_AY) * radius.y
            + cull_lanes(f, CULL_AZ) * radius.z;
        const bool rejected = cull_any8(distance + cull_lanes(f, CULL_REJECT_K) * size < cull_lanes(f, CULL_REJECT));
        const bool drawn = cull_all8(distance + cull_lanes(f, CULL_PASS_K) * size >= cull_lanes(f, CULL_PASS));
        if (ok && rejected) return CULL_REJECTED;
        if (ok && drawn) return CULL_DRAWN;
    }
    return cull_slow(f, b, own, through);
}
