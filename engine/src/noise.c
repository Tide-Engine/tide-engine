#include "tide/math.h"

// Noise for simulation code: gradient (Perlin) noise, simplex noise and
// cellular (Worley) noise, in 2D and 3D, each from a point and a seed.
//
// Every one is basic operations in a fixed order (+ - * / sqrt, floor, and
// the integer hash), so a point gives the same bits on every platform. The
// GPU runs the same formulas, line for line (compiler/src/shader.c, which
// spells them in GLSL and WGSL): a frag that shades by noise draws what the
// match computed, up to how its GPU rounds. A change here goes there too.
//
// A cell's gradient, or its feature point, comes from Math.Hash of the cell
// and the seed (xxHash32, tide/math.h), so there's no permutation table and
// nothing repeats: the same cell is the same everywhere, and another seed is
// another world.

#pragma STDC FP_CONTRACT OFF

static float fade(const float t)
{
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

// A unit gradient from a hash, in 8 directions, dotted with (x, y): the axes
// and the diagonals.
static float grad2(const int32_t h, const float x, const float y)
{
    float gx = 1.0f;
    float gy = 0.0f;
    if ((h & 4) != 0) {
        gx = 0.70710678f;
        gy = 0.70710678f;
        if ((h & 1) != 0) gx = -0.70710678f;
        if ((h & 2) != 0) gy = -0.70710678f;
    } else if ((h & 2) != 0) {
        gx = 0.0f;
        gy = 1.0f;
        if ((h & 1) != 0) gy = -1.0f;
    } else if ((h & 1) != 0) {
        gx = -1.0f;
    }
    return gx * x + gy * y;
}

// A unit gradient from a hash, in the 12 directions of a cube's edges (Perlin's
// 16, four of them twice), dotted with (x, y, z).
static float grad3(const int32_t h, const float x, const float y, const float z)
{
    const int32_t k = h & 15;
    float u = x;
    float v = y;
    if (k >= 8) u = y;
    if (k >= 4) v = z;
    if (k == 12 || k == 14) v = x;
    if ((k & 1) != 0) u = -u;
    if ((k & 2) != 0) v = -v;
    return u + v;
}

static int32_t hash2(const int32_t x, const int32_t y, const int32_t seed)
{
    return tide_hash_i3(tide_i3(x, y, seed));
}

static int32_t hash3(const int32_t x, const int32_t y, const int32_t z, const int32_t seed)
{
    return tide_hash_i4(tide_i4(x, y, z, seed));
}

// ---------------------------------------------------------------------------
// Gradient noise: 0 at every whole number, smooth between. Unit gradients
// keep it within sqrt(n / 4), scaled here to -1..1.

float tide_perlin_f2(const tide_float2 p, const int32_t seed)
{
    const float fx = tide_floor_f(p.x);
    const float fy = tide_floor_f(p.y);
    const int32_t ix = tide_i_from_f(fx);
    const int32_t iy = tide_i_from_f(fy);
    const int32_t jx = tide_add_i(ix, 1);
    const int32_t jy = tide_add_i(iy, 1);
    const float x = p.x - fx;
    const float y = p.y - fy;
    const float u = fade(x);
    const float v = fade(y);
    const float n00 = grad2(hash2(ix, iy, seed), x, y);
    const float n10 = grad2(hash2(jx, iy, seed), x - 1.0f, y);
    const float n01 = grad2(hash2(ix, jy, seed), x, y - 1.0f);
    const float n11 = grad2(hash2(jx, jy, seed), x - 1.0f, y - 1.0f);
    const float a = n00 + u * (n10 - n00);
    const float b = n01 + u * (n11 - n01);
    return (a + v * (b - a)) * 1.41421356f;
}

float tide_perlin_f3(const tide_float3 p, const int32_t seed)
{
    const float fx = tide_floor_f(p.x);
    const float fy = tide_floor_f(p.y);
    const float fz = tide_floor_f(p.z);
    const int32_t ix = tide_i_from_f(fx);
    const int32_t iy = tide_i_from_f(fy);
    const int32_t iz = tide_i_from_f(fz);
    const int32_t jx = tide_add_i(ix, 1);
    const int32_t jy = tide_add_i(iy, 1);
    const int32_t jz = tide_add_i(iz, 1);
    const float x = p.x - fx;
    const float y = p.y - fy;
    const float z = p.z - fz;
    const float u = fade(x);
    const float v = fade(y);
    const float w = fade(z);
    const float n000 = grad3(hash3(ix, iy, iz, seed), x, y, z);
    const float n100 = grad3(hash3(jx, iy, iz, seed), x - 1.0f, y, z);
    const float n010 = grad3(hash3(ix, jy, iz, seed), x, y - 1.0f, z);
    const float n110 = grad3(hash3(jx, jy, iz, seed), x - 1.0f, y - 1.0f, z);
    const float n001 = grad3(hash3(ix, iy, jz, seed), x, y, z - 1.0f);
    const float n101 = grad3(hash3(jx, iy, jz, seed), x - 1.0f, y, z - 1.0f);
    const float n011 = grad3(hash3(ix, jy, jz, seed), x, y - 1.0f, z - 1.0f);
    const float n111 = grad3(hash3(jx, jy, jz, seed), x - 1.0f, y - 1.0f, z - 1.0f);
    const float a0 = n000 + u * (n100 - n000);
    const float a1 = n010 + u * (n110 - n010);
    const float b0 = n001 + u * (n101 - n001);
    const float b1 = n011 + u * (n111 - n011);
    const float a = a0 + v * (a1 - a0);
    const float b = b0 + v * (b1 - b0);
    return a + w * (b - a);
}

// ---------------------------------------------------------------------------
// Simplex noise (Gustavson's): the point's simplex is found by skewing, and
// each corner adds its gradient under a bump that ends before the next.

// One corner's share: its bump (r2 past the bump's reach gives 0), times its
// gradient dotted with the offset.
static float corner2(const int32_t h, const float x, const float y)
{
    const float t = 0.5f - x * x - y * y;
    if (t < 0.0f) return 0.0f;
    const float tt = t * t;
    return tt * tt * grad2(h, x, y);
}

static float corner3(const int32_t h, const float x, const float y, const float z)
{
    const float t = 0.6f - x * x - y * y - z * z;
    if (t < 0.0f) return 0.0f;
    const float tt = t * t;
    return tt * tt * grad3(h, x, y, z);
}

float tide_simplex_f2(const tide_float2 p, const int32_t seed)
{
    const float s = (p.x + p.y) * 0.36602540f;
    const float fi = tide_floor_f(p.x + s);
    const float fj = tide_floor_f(p.y + s);
    const float t = (fi + fj) * 0.21132487f;
    const float x0 = p.x - (fi - t);
    const float y0 = p.y - (fj - t);
    int32_t i1 = 0;
    int32_t j1 = 1;
    if (x0 > y0) {
        i1 = 1;
        j1 = 0;
    }
    const float x1 = x0 - (float)i1 + 0.21132487f;
    const float y1 = y0 - (float)j1 + 0.21132487f;
    const float x2 = x0 - 1.0f + 0.42264973f;
    const float y2 = y0 - 1.0f + 0.42264973f;
    const int32_t i = tide_i_from_f(fi);
    const int32_t j = tide_i_from_f(fj);
    const float n0 = corner2(hash2(i, j, seed), x0, y0);
    const float n1 = corner2(hash2(tide_add_i(i, i1), tide_add_i(j, j1), seed), x1, y1);
    const float n2 = corner2(hash2(tide_add_i(i, 1), tide_add_i(j, 1), seed), x2, y2);
    return (n0 + n1 + n2) * 99.2f;
}

float tide_simplex_f3(const tide_float3 p, const int32_t seed)
{
    const float s = (p.x + p.y + p.z) * 0.33333334f;
    const float fi = tide_floor_f(p.x + s);
    const float fj = tide_floor_f(p.y + s);
    const float fk = tide_floor_f(p.z + s);
    const float t = (fi + fj + fk) * 0.16666667f;
    const float x0 = p.x - (fi - t);
    const float y0 = p.y - (fj - t);
    const float z0 = p.z - (fk - t);
    // Which of the six simplexes of the cell the point is in: the order of x0, y0 and z0
    int32_t i1 = 0;
    int32_t j1 = 0;
    int32_t k1 = 0;
    int32_t i2 = 0;
    int32_t j2 = 0;
    int32_t k2 = 0;
    if (x0 >= y0) {
        if (y0 >= z0) {
            i1 = 1;
            i2 = 1;
            j2 = 1;
        } else if (x0 >= z0) {
            i1 = 1;
            i2 = 1;
            k2 = 1;
        } else {
            k1 = 1;
            i2 = 1;
            k2 = 1;
        }
    } else {
        if (y0 < z0) {
            k1 = 1;
            j2 = 1;
            k2 = 1;
        } else if (x0 < z0) {
            j1 = 1;
            j2 = 1;
            k2 = 1;
        } else {
            j1 = 1;
            i2 = 1;
            j2 = 1;
        }
    }
    const float x1 = x0 - (float)i1 + 0.16666667f;
    const float y1 = y0 - (float)j1 + 0.16666667f;
    const float z1 = z0 - (float)k1 + 0.16666667f;
    const float x2 = x0 - (float)i2 + 0.33333334f;
    const float y2 = y0 - (float)j2 + 0.33333334f;
    const float z2 = z0 - (float)k2 + 0.33333334f;
    const float x3 = x0 - 0.5f;
    const float y3 = y0 - 0.5f;
    const float z3 = z0 - 0.5f;
    const int32_t i = tide_i_from_f(fi);
    const int32_t j = tide_i_from_f(fj);
    const int32_t k = tide_i_from_f(fk);
    const float n0 = corner3(hash3(i, j, k, seed), x0, y0, z0);
    const float n1 = corner3(hash3(tide_add_i(i, i1), tide_add_i(j, j1), tide_add_i(k, k1), seed), x1, y1, z1);
    const float n2 = corner3(hash3(tide_add_i(i, i2), tide_add_i(j, j2), tide_add_i(k, k2), seed), x2, y2, z2);
    const float n3 = corner3(hash3(tide_add_i(i, 1), tide_add_i(j, 1), tide_add_i(k, 1), seed), x3, y3, z3);
    return (n0 + n1 + n2 + n3) * 32.5f;
}

// ---------------------------------------------------------------------------
// Cellular noise: a feature point in each cell, placed by its hash, and the
// distances to the nearest and the second nearest of the cells around.

// The two shortest squared distances so far, with `d` among them.
static tide_float2 nearest(const tide_float2 best, const float d)
{
    if (d < best.x) return tide_f2(d, best.x);
    if (d < best.y) return tide_f2(best.x, d);
    return best;
}

// The squared distance from the point (at `f` in its cell) to the feature
// point of the cell `o` cells over.
static float feature2(const int32_t ix, const int32_t iy, const int32_t ox, const int32_t oy, const int32_t seed,
                      const float fx, const float fy)
{
    const int32_t h = hash2(tide_add_i(ix, ox), tide_add_i(iy, oy), seed);
    const float px = (float)ox + (float)(h & 32767) * 0.000030518509f;
    const float py = (float)oy + (float)((h >> 15) & 32767) * 0.000030518509f;
    const float dx = px - fx;
    const float dy = py - fy;
    return dx * dx + dy * dy;
}

static float feature3(const int32_t ix, const int32_t iy, const int32_t iz, const int32_t ox, const int32_t oy,
                      const int32_t oz, const int32_t seed, const float fx, const float fy, const float fz)
{
    const int32_t h = hash3(tide_add_i(ix, ox), tide_add_i(iy, oy), tide_add_i(iz, oz), seed);
    const float px = (float)ox + (float)(h & 1023) * 0.00097751711f;
    const float py = (float)oy + (float)((h >> 10) & 1023) * 0.00097751711f;
    const float pz = (float)oz + (float)((h >> 20) & 1023) * 0.00097751711f;
    const float dx = px - fx;
    const float dy = py - fy;
    const float dz = pz - fz;
    return dx * dx + dy * dy + dz * dz;
}

tide_float2 tide_cellular_f2(const tide_float2 p, const int32_t seed)
{
    const float fx = tide_floor_f(p.x);
    const float fy = tide_floor_f(p.y);
    const int32_t ix = tide_i_from_f(fx);
    const int32_t iy = tide_i_from_f(fy);
    const float x = p.x - fx;
    const float y = p.y - fy;
    tide_float2 best = tide_f2(8.0f, 8.0f);
    for (int32_t oy = -1; oy <= 1; oy++) {
        for (int32_t ox = -1; ox <= 1; ox++) best = nearest(best, feature2(ix, iy, ox, oy, seed, x, y));
    }
    return tide_f2(tide_sqrt_f(best.x), tide_sqrt_f(best.y));
}

tide_float2 tide_cellular_f3(const tide_float3 p, const int32_t seed)
{
    const float fx = tide_floor_f(p.x);
    const float fy = tide_floor_f(p.y);
    const float fz = tide_floor_f(p.z);
    const int32_t ix = tide_i_from_f(fx);
    const int32_t iy = tide_i_from_f(fy);
    const int32_t iz = tide_i_from_f(fz);
    const float x = p.x - fx;
    const float y = p.y - fy;
    const float z = p.z - fz;
    tide_float2 best = tide_f2(8.0f, 8.0f);
    for (int32_t oz = -1; oz <= 1; oz++) {
        for (int32_t oy = -1; oy <= 1; oy++) {
            for (int32_t ox = -1; ox <= 1; ox++) best = nearest(best, feature3(ix, iy, iz, ox, oy, oz, seed, x, y, z));
        }
    }
    return tide_f2(tide_sqrt_f(best.x), tide_sqrt_f(best.y));
}
