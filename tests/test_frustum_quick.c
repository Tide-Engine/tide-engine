// The quick frustum test (engine/src/frustum_cull.h) against the test it
// replaced, kept here as it was: the same answer for every box, transform and
// camera, ordinary, extreme, non-finite or right at a plane.

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../engine/src/frustum_cull.h"
#include "tide_test.h"

// draw.c's planes and bounds_visible before the quick test, without the profile.
static void old_planes(tide_draw_frustum *f, const float c[16])
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
        if (p[0] != 0.0 || p[1] != 0.0 || p[2] != 0.0 || p[3] < 0.0) f->count++;
    }
}

static int old_visible(const tide_draw_bounds *bounds, const tide_draw_frustum *camera, const tide_float4x4 *own,
                       const tide_float4x4 *through)
{
    if (bounds->set != 1 || !camera->count) return CULL_UNTESTABLE;
    float w[16];
    if (through) {
        const tide_float4x4 world = tide_mul_f4x4(*through, *own);
        memcpy(w, &world, sizeof w);
    } else {
        memcpy(w, own, sizeof w);
    }
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

static uint64_t state = 0x9E3779B97F4A7C15u;

static uint32_t next(void)
{
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return (uint32_t)(state >> 32);
}

static float unit(void) { return (float)(next() >> 8) * 0x1p-24f; }    // [0, 1)
static float signed_unit(void) { return unit() * 2.0f - 1.0f; }       // [-1, 1)
static float scale_of(const float lo, const float hi) { return lo * powf(hi / lo, unit()); } // Log-uniform

static float odd_value(void)
{
    static const float odd[] = {INFINITY, -INFINITY, NAN, 0x1p-149f, -0x1p-149f, 0x1p-126f, 3.0e38f, -3.0e38f,
                                1e30f, -1e30f, 1e20f, 0.0f, -0.0f, 1e-30f};
    return odd[next() % (sizeof odd / sizeof odd[0])];
}

static tide_float4x4 rotation(void)
{
    const tide_float4 q = tide_f4(signed_unit(), signed_unit(), signed_unit(), signed_unit() + 0.01f);
    const float n = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    const tide_quaternion r = {{q.x / n, q.y / n, q.z / n, q.w / n}};
    return tide_trs_f4x4(tide_f3(0, 0, 0), r, tide_f3(1, 1, 1));
}

// An instance's transform: rotated, scaled (mirrored, flattened, sheared), anywhere; sometimes odd
static tide_float4x4 instance(const float reach)
{
    tide_float4x4 m = tide_mul_f4x4(rotation(), tide_scaling_f4x4(tide_f3(
        signed_unit() * scale_of(1e-3f, 1e3f), signed_unit() * scale_of(1e-3f, 1e3f), signed_unit() * scale_of(1e-3f, 1e3f))));
    if (next() % 4 == 0) m = tide_mul_f4x4(m, rotation());
    if (next() % 8 == 0) m.c1.x += signed_unit() * 4.0f; // Shear
    if (next() % 16 == 0) m.c2 = tide_f4(0, 0, 0, 0);  // Flat
    m.c3 = tide_f4(signed_unit() * reach, signed_unit() * reach, signed_unit() * reach, 1.0f);
    const uint32_t odd = next() % 64;
    float *f = (float *)&m;
    if (odd == 0) f[next() % 16] = odd_value();
    if (odd == 1) f[3 + 4 * (next() % 4)] = signed_unit(); // Projective
    if (odd == 2) for (int i = 0; i < 16; i++) if (i % 4 != 3) f[i] *= 1e30f;
    if (odd == 3) for (int i = 12; i < 15; i++) f[i] = signed_unit() * 3e38f;
    return m;
}

static tide_draw_bounds box(void)
{
    const float s = scale_of(1e-3f, 1e3f);
    tide_draw_bounds b = {.center = tide_f3(signed_unit() * s, signed_unit() * s, signed_unit() * s),
                          .extents = tide_f3(unit() * s, unit() * s, unit() * s), .set = 1};
    if (next() % 16 == 0) b.extents = tide_f3(0, 0, 0);
    if (next() % 32 == 0) b.center.x = next() % 2 ? 3e38f : -1e30f;
    if (next() % 32 == 0) b.extents.y = next() % 2 ? 3e38f : 1e25f;
    if (next() % 64 == 0) b.set = 2;
    return b;
}

// A camera's world-to-clip matrix: perspective, infinite far, orthographic, or odd numbers.
static tide_float4x4 camera_matrix(const uint32_t kind, const float reach)
{
    tide_float4x4 projection;
    const float near_z = scale_of(1e-3f, 10.0f);
    switch (kind % 5) {
    case 0:
        projection = tide_perspectivefov_f4x4(0.2f + unit() * 2.5f, 0.3f + unit() * 3.0f, near_z,
                                              near_z * scale_of(2.0f, 1e7f));
        break;
    case 1: // Infinite far: z and w the same but for the near term
        projection = tide_perspectivefov_f4x4(0.2f + unit() * 2.5f, 0.3f + unit() * 3.0f, near_z, 1000.0f);
        projection.c2.z = -1.0f;
        projection.c3.z = -2.0f * near_z;
        break;
    case 2:
        projection = tide_ortho_f4x4(scale_of(0.01f, 1e4f), scale_of(0.01f, 1e4f), -scale_of(0.01f, 100.0f),
                                     scale_of(0.1f, 1e5f));
        break;
    case 3: // Any numbers
        for (int i = 0; i < 16; i++) ((float *)&projection)[i] = signed_unit() * scale_of(1e-3f, 1e3f);
        break;
    default: // Extreme numbers
        for (int i = 0; i < 16; i++) ((float *)&projection)[i] = next() % 3 ? signed_unit() * scale_of(1e-30f, 1e30f) : 0.0f;
        break;
    }
    tide_float4x4 view = rotation();
    view.c3 = tide_f4(signed_unit() * reach, signed_unit() * reach, signed_unit() * reach, 1.0f);
    return tide_mul_f4x4(projection, view);
}

static void build(tide_draw_frustum *quick, tide_draw_frustum *old, const tide_float4x4 *clip)
{
    float c[16];
    memcpy(c, clip, sizeof c);
    memset(quick, 0, sizeof *quick);
    memset(old, 0, sizeof *old);
    cull_frustum_build(quick, c);
    old_planes(old, c);
}

typedef struct tally {
    uint32_t tried, wrong, quick, drawn, rejected;
} tally;

static void compare(tally *t, const tide_draw_frustum *quick, const tide_draw_frustum *old, const tide_draw_bounds *b,
                    const tide_float4x4 *own, const tide_float4x4 *through)
{
    const cull_box cb = cull_box_of(b, quick);
    const int got = cull_instance(quick, &cb, (const float *)own, through), want = old_visible(b, old, own, through);
    t->tried++;
    t->drawn += want == CULL_DRAWN;
    t->rejected += want == CULL_REJECTED;
    if (got != want) {
        if (t->wrong++ < 5) {
            const float *f = (const float *)own;
            fprintf(stderr, "frustum quick test: %d where it was %d; box %a %a %a / %a %a %a, transform", got, want,
                    b->center.x, b->center.y, b->center.z, b->extents.x, b->extents.y, b->extents.z);
            for (int i = 0; i < 16; i++) fprintf(stderr, " %a", f[i]);
            fprintf(stderr, "%s\n", through ? " (through)" : "");
        }
    }
    if (cb.testable && cb.quick) {
        cull_f4 c[4][4];
        for (int i = 0; i < 4; i++) cull_columns(c[i], (const float *)own, through);
        t->quick += (cull_quick(quick, &cb, c) & 0x11u) != 0;
    }
}

TIDE_TEST(frustum_quick_planes_are_the_old_ones)
{
    for (uint32_t i = 0; i < 2000; i++) {
        const tide_float4x4 clip = camera_matrix(i, scale_of(1.0f, 1e5f));
        tide_draw_frustum quick, old;
        build(&quick, &old, &clip);
        TIDE_CHECK(quick.count == old.count);
        TIDE_CHECK(memcmp(quick.plane, old.plane, sizeof old.plane) == 0);
        TIDE_CHECK(memcmp(quick.magnitude, old.magnitude, sizeof old.magnitude) == 0);
        if (i % 5 < 3) TIDE_CHECK(quick.quick_ready); // Ordinary cameras take the quick test
    }
}

TIDE_TEST(frustum_quick_answers_as_the_old_test)
{
    tally ordinary = {0}, all = {0};
    uint32_t batched = 0;
    for (uint32_t camera = 0; camera < 400; camera++) {
        const float reach = scale_of(1.0f, 1e5f);
        const tide_float4x4 clip = camera_matrix(camera, reach);
        tide_draw_frustum quick, old;
        build(&quick, &old, &clip);
        tide_float4x4 through = instance(reach);
        if (camera % 7 == 0) through.c3 = tide_f4(0, 0, 0, 1);
        for (uint32_t i = 0; i < 2500; i++) {
            const tide_draw_bounds b = box();
            const tide_float4x4 own = instance(reach * (next() % 4 ? 1.0f : 1e-3f));
            const bool plain = camera % 5 < 3;
            compare(plain ? &ordinary : &all, &quick, &old, &b, &own, i % 3 == 0 ? &through : NULL);
        }
        // Four different instances at once, of fewer, as draw.c's loops give them
        for (uint32_t i = 0; i < 400; i++) {
            const tide_draw_bounds b = box();
            const cull_box cb = cull_box_of(&b, &quick);
            tide_float4x4 own[4];
            const float *four[4];
            const uint32_t n = 1u + next() % 4u;
            for (uint32_t k = 0; k < 4; k++) {
                own[k] = instance(reach * (next() % 4 ? 1.0f : 1e-3f));
                four[k] = (const float *)&own[k < n ? k : n - 1u];
            }
            const tide_float4x4 *by = i % 3 == 0 ? &through : NULL;
            int result[4];
            cull_four(&quick, &cb, four, by, n, result);
            for (uint32_t k = 0; k < n; k++) {
                const int want = old_visible(&b, &old, &own[k], by);
                TIDE_CHECK(result[k] == want);
                batched += result[k] == CULL_REJECTED;
            }
        }
    }
    TIDE_CHECK(batched > 400 * 400 / 10);
    TIDE_CHECK(ordinary.wrong == 0);
    TIDE_CHECK(all.wrong == 0);
    // Both answers come up often, and the quick test answers nearly all ordinary instances itself
    TIDE_CHECK(ordinary.drawn > ordinary.tried / 20 && ordinary.rejected > ordinary.tried / 5);
    TIDE_CHECK(ordinary.quick > ordinary.tried * 8 / 10);
    printf("frustum quick: %u ordinary (%u drawn, %u rejected, %u answered quickly), %u odd cameras\n", ordinary.tried,
           ordinary.drawn, ordinary.rejected, ordinary.quick, all.tried);
}

// Boxes moved along a line across the frustum's edge, to where the old test's answer changes, then a few floats
// either side of it: where the quick test has to leave it to the exact one.
TIDE_TEST(frustum_quick_answers_as_the_old_test_at_planes)
{
    tally t = {0};
    for (uint32_t camera = 0; camera < 300; camera++) {
        const float reach = scale_of(1.0f, 1e4f);
        const tide_float4x4 clip = camera_matrix(camera % 3, reach);
        tide_draw_frustum quick, old;
        build(&quick, &old, &clip);
        for (uint32_t i = 0; i < 40; i++) {
            const tide_draw_bounds b = {.center = tide_f3(signed_unit(), signed_unit(), signed_unit()),
                                        .extents = tide_f3(unit(), unit(), unit()), .set = 1};
            tide_float4x4 own = instance(reach);
            if (own.c0.w != 0 || own.c1.w != 0 || own.c2.w != 0 || own.c3.w != 1) continue;
            const tide_float4 from = own.c3;
            const tide_float4 to = tide_f4(signed_unit() * reach * 100, signed_unit() * reach * 100,
                                           signed_unit() * reach * 100, 1);
            own.c3 = from;
            const int start = old_visible(&b, &old, &own, NULL);
            own.c3 = to;
            if (old_visible(&b, &old, &own, NULL) == start) continue;
            double lo = 0, hi = 1;
            for (int step = 0; step < 60; step++) {
                const double mid = (lo + hi) * 0.5;
                own.c3 = tide_f4((float)(from.x + (to.x - from.x) * mid), (float)(from.y + (to.y - from.y) * mid),
                                 (float)(from.z + (to.z - from.z) * mid), 1);
                if (old_visible(&b, &old, &own, NULL) == start) lo = mid;
                else hi = mid;
            }
            const tide_float4 edge = own.c3;
            for (int dx = -3; dx <= 3; dx++) {
                for (int axis = 0; axis < 3; axis++) {
                    own.c3 = edge;
                    float *v = axis == 0 ? &own.c3.x : axis == 1 ? &own.c3.y : &own.c3.z;
                    for (int s = 0; s < (dx < 0 ? -dx : dx); s++) *v = nextafterf(*v, dx < 0 ? -INFINITY : INFINITY);
                    compare(&t, &quick, &old, &b, &own, NULL);
                }
            }
        }
    }
    TIDE_CHECK(t.wrong == 0);
    TIDE_CHECK(t.tried > 10000 && t.drawn > t.tried / 10 && t.rejected > t.tried / 10);
}
