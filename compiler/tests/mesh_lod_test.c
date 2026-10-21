// A mesh's levels of detail (compiler/src/mesh_lod.h), on their own: that
// each level is fewer of the mesh's own triangles' corners, that open edges
// and seams stay whole, and that what a level says it's off by is so.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mesh_lod.h"
#include "tide_test.h"

typedef struct shape {
    uint32_t vertices, indices;
    float *position, *normal, *uv, *color;
    uint32_t *index;
} shape;

static void shape_free(shape *s)
{
    free(s->position), free(s->normal), free(s->uv), free(s->color), free(s->index);
    *s = (shape){0};
}

static mesh_lod_source source_of(const shape *s)
{
    return (mesh_lod_source){.vertices = s->vertices, .position = s->position, .normal = s->normal, .uv = s->uv,
                             .color = s->color, .index = s->index, .indices = s->indices};
}

// A flat square of `side` by `side` cells in x and z, from 0 to 1, facing up, its uvs its place.
static shape grid(const uint32_t side)
{
    shape s = {.vertices = (side + 1u) * (side + 1u), .indices = side * side * 6u};
    s.position = calloc(s.vertices * 3u, sizeof(float));
    s.normal = calloc(s.vertices * 3u, sizeof(float));
    s.uv = calloc(s.vertices * 2u, sizeof(float));
    s.index = calloc(s.indices, sizeof(uint32_t));
    for (uint32_t z = 0; z <= side; z++) {
        for (uint32_t x = 0; x <= side; x++) {
            const uint32_t v = z * (side + 1u) + x;
            s.position[v * 3u] = (float)x / (float)side;
            s.position[v * 3u + 2u] = (float)z / (float)side;
            s.normal[v * 3u + 1u] = 1.0f;
            s.uv[v * 2u] = (float)x / (float)side;
            s.uv[v * 2u + 1u] = (float)z / (float)side;
        }
    }
    uint32_t *i = s.index;
    for (uint32_t z = 0; z < side; z++) {
        for (uint32_t x = 0; x < side; x++) {
            const uint32_t a = z * (side + 1u) + x, b = a + 1u, c = a + side + 1u, d = c + 1u;
            *i++ = a, *i++ = c, *i++ = b;
            *i++ = b, *i++ = c, *i++ = d;
        }
    }
    return s;
}

// A ball of radius 1 about the origin, its corners shared, with their normals.
static shape ball(const uint32_t rings, const uint32_t around)
{
    shape s = {.vertices = (rings - 1u) * around + 2u, .indices = (rings - 1u) * around * 6u};
    s.position = calloc(s.vertices * 3u, sizeof(float));
    s.normal = calloc(s.vertices * 3u, sizeof(float));
    s.index = calloc(s.indices, sizeof(uint32_t));
    const double pi = 3.14159265358979323846;
    for (uint32_t r = 1; r < rings; r++) {
        for (uint32_t a = 0; a < around; a++) {
            const uint32_t v = (r - 1u) * around + a;
            const double down = pi * r / rings, turn = 2.0 * pi * a / around;
            s.position[v * 3u] = (float)(sin(down) * cos(turn));
            s.position[v * 3u + 1u] = (float)cos(down);
            s.position[v * 3u + 2u] = (float)(sin(down) * sin(turn));
        }
    }
    const uint32_t top = s.vertices - 2u, bottom = s.vertices - 1u;
    s.position[top * 3u + 1u] = 1.0f;
    s.position[bottom * 3u + 1u] = -1.0f;
    memcpy(s.normal, s.position, s.vertices * 3u * sizeof(float));
    uint32_t *i = s.index;
    for (uint32_t a = 0; a < around; a++) {
        const uint32_t next = (a + 1u) % around;
        *i++ = top, *i++ = next, *i++ = a;
        *i++ = bottom, *i++ = (rings - 2u) * around + a, *i++ = (rings - 2u) * around + next;
    }
    for (uint32_t r = 0; r + 2u < rings; r++) {
        for (uint32_t a = 0; a < around; a++) {
            const uint32_t next = (a + 1u) % around;
            const uint32_t p = r * around + a, q = r * around + next, u = p + around, w = q + around;
            *i++ = p, *i++ = q, *i++ = u;
            *i++ = q, *i++ = w, *i++ = u;
        }
    }
    s.indices = (uint32_t)(i - s.index);
    return s;
}

static double area_of(const shape *s, const uint32_t *index, const uint32_t count)
{
    double total = 0.0;
    for (uint32_t i = 0; i + 3u <= count; i += 3u) {
        const float *a = &s->position[index[i] * 3u], *b = &s->position[index[i + 1u] * 3u], *c = &s->position[index[i + 2u] * 3u];
        const double ab[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, ac[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        const double n[3] = {ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2], ab[0] * ac[1] - ab[1] * ac[0]};
        total += 0.5 * sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    }
    return total;
}

// Whether every level names the mesh's own vertices, in whole triangles, each no more than three
// quarters of the one before and off by no less.
static bool levels_hold(const shape *s, const mesh_lod *lod)
{
    uint32_t before = s->indices;
    float off = 0.0f;
    for (uint32_t level = 0; level < lod->levels; level++) {
        if (lod->count[level] % 3u != 0 || lod->count[level] == 0 || lod->first[level] + lod->count[level] > lod->indices) return false;
        if (lod->count[level] > before - before / 4u) return false;
        if (!(lod->error[level] >= off)) return false;
        for (uint32_t i = 0; i < lod->count[level]; i++) {
            if (lod->index[lod->first[level] + i] >= s->vertices) return false;
        }
        before = lod->count[level];
        off = lod->error[level];
    }
    return true;
}

// A flat square loses nearly all of its triangles and none of its surface:
// its edge stays where it was, and its uvs are what they were over all of it.
TIDE_TEST(mesh_lod_flat_square_keeps_its_surface)
{
    shape s = grid(32);
    const mesh_lod_source source = source_of(&s);
    mesh_lod lod;
    mesh_lod_make(&source, &lod);
    TIDE_REQUIRE(lod.levels >= 5u);
    TIDE_CHECK(levels_hold(&s, &lod));
    TIDE_CHECK(lod.count[lod.levels - 1u] <= s.indices / 32u);
    for (uint32_t level = 0; level < lod.levels; level++) {
        TIDE_CHECK(lod.error[level] < 1e-5f);
        TIDE_CHECK(fabs(area_of(&s, &lod.index[lod.first[level]], lod.count[level]) - 1.0) < 1e-9);
    }
    TIDE_CHECK(fabsf(lod.center[0] - 0.5f) < 1e-6f && fabsf(lod.center[1]) < 1e-6f && fabsf(lod.center[2] - 0.5f) < 1e-6f);
    TIDE_CHECK(lod.radius >= 0.70710677f && lod.radius < 0.7072f);
    mesh_lod_free(&lod);
    shape_free(&s);
}

// A ball stays closed at every level (each edge is two triangles'), and each
// level is off by more than the one before, and by what it says: no corner a
// level keeps is off the ball, so what's lost is how far its flats are in.
TIDE_TEST(mesh_lod_ball_stays_closed_and_says_how_far_off_it_is)
{
    shape s = ball(32, 64);
    const mesh_lod_source source = source_of(&s);
    mesh_lod lod;
    mesh_lod_make(&source, &lod);
    TIDE_REQUIRE(lod.levels >= 4u);
    TIDE_CHECK(levels_hold(&s, &lod));
    TIDE_CHECK(fabsf(lod.radius - 1.0f) < 1e-4f);
    for (uint32_t level = 0; level < lod.levels; level++) {
        const uint32_t *index = &lod.index[lod.first[level]];
        const uint32_t count = lod.count[level];
        // Each edge one way is matched by itself the other way, once
        uint32_t unmatched = 0;
        for (uint32_t i = 0; i < count; i++) {
            const uint32_t a = index[i], b = index[i - i % 3u + (i + 1u) % 3u];
            uint32_t back = 0;
            for (uint32_t j = 0; j < count; j++) {
                if (index[j] == b && index[j - j % 3u + (j + 1u) % 3u] == a) back++;
            }
            if (back != 1u) unmatched++;
        }
        TIDE_CHECK(unmatched == 0);
        TIDE_CHECK(lod.error[level] > 0.0f && lod.error[level] < 0.5f);
        // The flats of a ball with this few triangles are in by about its area's loss
        const double lost = 1.0 - area_of(&s, index, count) / (4.0 * 3.14159265358979323846);
        TIDE_CHECK(lost > 0.0 && lost < 0.6);
    }
    TIDE_CHECK(lod.error[lod.levels - 1u] > lod.error[0]);
    mesh_lod_free(&lod);
    shape_free(&s);
}

// Two colors meeting along a line, each side with corners of its own there:
// no triangle of any level has corners of both, and each color covers what
// it did.
TIDE_TEST(mesh_lod_keeps_a_seam_of_colors_whole)
{
    const uint32_t side = 16;
    shape left = grid(side);
    shape s = {.vertices = left.vertices * 2u, .indices = left.indices * 2u};
    s.position = calloc(s.vertices * 3u, sizeof(float));
    s.normal = calloc(s.vertices * 3u, sizeof(float));
    s.color = calloc(s.vertices * 4u, sizeof(float));
    s.index = calloc(s.indices, sizeof(uint32_t));
    for (uint32_t half = 0; half < 2u; half++) {
        for (uint32_t v = 0; v < left.vertices; v++) {
            const uint32_t to = half * left.vertices + v;
            memcpy(&s.position[to * 3u], &left.position[v * 3u], 3u * sizeof(float));
            s.position[to * 3u] += (float)half; // The second square starts where the first ends
            s.normal[to * 3u + 1u] = 1.0f;
            s.color[to * 4u + half] = 1.0f;
            s.color[to * 4u + 3u] = 1.0f;
        }
        for (uint32_t i = 0; i < left.indices; i++) s.index[half * left.indices + i] = left.index[i] + half * left.vertices;
    }
    const mesh_lod_source source = source_of(&s);
    mesh_lod lod;
    mesh_lod_make(&source, &lod);
    TIDE_REQUIRE(lod.levels >= 3u);
    TIDE_CHECK(levels_hold(&s, &lod));
    for (uint32_t level = 0; level < lod.levels; level++) {
        const uint32_t *index = &lod.index[lod.first[level]];
        uint32_t torn = 0;
        double red = 0.0;
        for (uint32_t i = 0; i < lod.count[level]; i += 3u) {
            const bool first = index[i] < left.vertices;
            if ((index[i + 1u] < left.vertices) != first || (index[i + 2u] < left.vertices) != first) torn++;
            if (first) red += area_of(&s, &index[i], 3);
        }
        TIDE_CHECK(torn == 0);
        TIDE_CHECK(fabs(red - 1.0) < 1e-9);
        TIDE_CHECK(fabs(area_of(&s, index, lod.count[level]) - 2.0) < 1e-9);
    }
    mesh_lod_free(&lod);
    shape_free(&left);
    shape_free(&s);
}

// A mesh whose every triangle has corners of its own, with the way it faces
// as their normal, is still a surface: the corners at one place are one
// position, so it loses triangles as a mesh with shared corners does.
TIDE_TEST(mesh_lod_takes_triangles_with_corners_of_their_own)
{
    shape shared = grid(16);
    shape s = {.vertices = shared.indices, .indices = shared.indices};
    s.position = calloc(s.vertices * 3u, sizeof(float));
    s.normal = calloc(s.vertices * 3u, sizeof(float));
    s.index = calloc(s.indices, sizeof(uint32_t));
    for (uint32_t i = 0; i < shared.indices; i++) {
        memcpy(&s.position[i * 3u], &shared.position[shared.index[i] * 3u], 3u * sizeof(float));
        s.normal[i * 3u + 1u] = 1.0f;
        s.index[i] = i;
    }
    const mesh_lod_source source = source_of(&s);
    mesh_lod lod;
    mesh_lod_make(&source, &lod);
    TIDE_REQUIRE(lod.levels >= 4u);
    TIDE_CHECK(levels_hold(&s, &lod));
    for (uint32_t level = 0; level < lod.levels; level++) {
        TIDE_CHECK(fabs(area_of(&s, &lod.index[lod.first[level]], lod.count[level]) - 1.0) < 1e-9);
    }
    mesh_lod_free(&lod);
    shape_free(&shared);
    shape_free(&s);
}

// What's simple already has no levels, and still has its ball.
TIDE_TEST(mesh_lod_leaves_a_simple_mesh_alone)
{
    shape s = grid(1);
    const mesh_lod_source source = source_of(&s);
    mesh_lod lod;
    mesh_lod_make(&source, &lod);
    TIDE_CHECK(lod.levels == 0 && lod.indices == 0 && lod.index == NULL);
    TIDE_CHECK(lod.radius >= 0.70710677f && lod.radius < 0.7072f);
    mesh_lod_free(&lod);
    shape_free(&s);
}

// The same mesh gives the same levels, bit for bit.
TIDE_TEST(mesh_lod_is_the_same_each_time)
{
    shape s = ball(24, 48);
    const mesh_lod_source source = source_of(&s);
    mesh_lod a, b;
    mesh_lod_make(&source, &a);
    mesh_lod_make(&source, &b);
    TIDE_REQUIRE(a.levels == b.levels && a.indices == b.indices && a.levels > 0);
    TIDE_CHECK(memcmp(a.index, b.index, a.indices * sizeof(uint32_t)) == 0);
    TIDE_CHECK(memcmp(a.error, b.error, sizeof a.error) == 0 && memcmp(a.count, b.count, sizeof a.count) == 0);
    mesh_lod_free(&a);
    mesh_lod_free(&b);
    shape_free(&s);
}

// A file is somebody else's numbers: indices past the vertices, triangles
// with no area, positions that are no number and a mesh with no size are all
// read without going anywhere they shouldn't.
TIDE_TEST(mesh_lod_takes_whatever_a_mesh_holds)
{
    shape s = grid(8);
    s.index[0] = 1u << 30;
    s.index[7] = s.index[6];
    s.position[3 * 5 + 1] = NAN;
    s.position[3 * 9] = INFINITY;
    mesh_lod_source source = source_of(&s);
    mesh_lod lod;
    mesh_lod_make(&source, &lod);
    for (uint32_t i = 0; i < lod.indices; i++) TIDE_CHECK(lod.index[i] < s.vertices);
    mesh_lod_free(&lod);
    for (uint32_t v = 0; v < s.vertices * 3u; v++) s.position[v] = 2.0f; // Every corner in one place
    mesh_lod_make(&source, &lod);
    TIDE_CHECK(lod.levels == 0 && lod.radius >= 0.0f);
    mesh_lod_free(&lod);
    source.vertices = 0;
    mesh_lod_make(&source, &lod);
    TIDE_CHECK(lod.levels == 0 && lod.radius == 0.0f);
    source = source_of(&s);
    source.indices = 0;
    mesh_lod_make(&source, &lod);
    TIDE_CHECK(lod.levels == 0);
    shape_free(&s);
}

// A skinned mesh says how differently two vertices follow their bones: a
// collapse between two that don't follow alike costs its edge's length more,
// so the level says it's off by that much where it would otherwise say none.
// Here the square is four bands, each following a bone of its own.
static float halves_apart(void *user, const uint32_t from, const uint32_t to)
{
    const shape *s = user;
    return (int)(s->position[from * 3u] * 3.999f) != (int)(s->position[to * 3u] * 3.999f) ? 1.0f : 0.0f;
}

TIDE_TEST(mesh_lod_counts_what_a_source_says_is_apart)
{
    shape s = grid(16);
    mesh_lod_source source = source_of(&s);
    mesh_lod plain, bent;
    mesh_lod_make(&source, &plain);
    source.apart = halves_apart;
    source.user = &s;
    mesh_lod_make(&source, &bent);
    TIDE_REQUIRE(plain.levels > 0 && bent.levels > 0);
    TIDE_CHECK(levels_hold(&s, &bent));
    TIDE_CHECK(plain.error[plain.levels - 1u] < 1e-5f);
    // Its first levels have plenty to take inside each band; with a column of corners left a band, the next has to
    // move one into another band
    TIDE_CHECK(bent.error[0] < 1e-5f);
    TIDE_CHECK(bent.levels < plain.levels || bent.error[bent.levels - 1u] > 0.01f);
    mesh_lod_free(&plain);
    mesh_lod_free(&bent);
    shape_free(&s);
}

// Levels are made once a mesh: kept for the process by their key, and in the
// game's .tide/ for the builds after it. A file there that's cut, or says
// what the mesh hasn't got, is made again rather than believed.
static void key_path(char *path, const size_t size, const uint8_t key[32])
{
    char hex[65];
    for (uint32_t i = 0; i < 32u; i++) snprintf(hex + i * 2u, 3, "%02x", key[i]);
    snprintf(path, size, "%s/.tide/assets/lod/%s.lod", MESH_LOD_WORK, hex);
}

static long file_size(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fclose(f);
    return size;
}

static void copy_file(const char *from, const char *to, const long keep, const long spoil_at)
{
    FILE *in = fopen(from, "rb"), *out = fopen(to, "wb");
    TIDE_REQUIRE(in && out);
    long at = 0;
    for (int c; (c = fgetc(in)) != EOF && (keep < 0 || at < keep); at++) fputc(at == spoil_at ? c ^ 0x55 : c, out);
    fclose(in);
    fclose(out);
}

TIDE_TEST(mesh_lod_is_kept_for_later_builds)
{
    shape s = ball(12, 24);
    const mesh_lod_source source = source_of(&s);
    // Keys nothing else has: the tests may run again over the same folder
    uint8_t first[32] = {1, 2, 3}, second[32] = {4, 5, 6}, third[32] = {7, 8, 9}, fourth[32] = {10, 11, 12}, none[32] = {13};
    char path[4][600];
    key_path(path[0], sizeof path[0], first), key_path(path[1], sizeof path[1], second);
    key_path(path[2], sizeof path[2], third), key_path(path[3], sizeof path[3], fourth);
    for (int i = 0; i < 4; i++) remove(path[i]);

    const uint32_t before = mesh_lod_made();
    const mesh_lod *made = mesh_lod_cached(first, &source, MESH_LOD_WORK);
    TIDE_REQUIRE(made && made->levels >= 3 && mesh_lod_made() == before + 1u);
    const long size = file_size(path[0]);
    TIDE_REQUIRE(size > 0);
    TIDE_CHECK(mesh_lod_cached(first, &source, MESH_LOD_WORK) == made && mesh_lod_made() == before + 1u);

    // A file another build left is read, not made
    copy_file(path[0], path[1], -1, -1);
    const mesh_lod *read = mesh_lod_cached(second, &source, MESH_LOD_WORK);
    TIDE_REQUIRE(read && read != made && mesh_lod_made() == before + 1u);
    TIDE_CHECK(read->levels == made->levels && read->indices == made->indices && read->radius == made->radius);
    TIDE_CHECK(memcmp(read->index, made->index, made->indices * sizeof(uint32_t)) == 0);
    TIDE_CHECK(memcmp(read->error, made->error, sizeof made->error) == 0 && memcmp(read->first, made->first, sizeof made->first) == 0);

    // One that's cut short is made again, and written whole
    copy_file(path[0], path[2], size - 5, -1);
    const mesh_lod *again = mesh_lod_cached(third, &source, MESH_LOD_WORK);
    TIDE_REQUIRE(again && mesh_lod_made() == before + 2u);
    TIDE_CHECK(again->indices == made->indices && memcmp(again->index, made->index, made->indices * sizeof(uint32_t)) == 0);
    TIDE_CHECK(file_size(path[2]) == size);

    // ...and so is one of a mesh with fewer vertices than its indices name
    copy_file(path[0], path[3], -1, -1);
    shape small = ball(4, 6);
    const mesh_lod_source other = source_of(&small);
    const mesh_lod *checked = mesh_lod_cached(fourth, &other, MESH_LOD_WORK);
    TIDE_REQUIRE(checked && mesh_lod_made() == before + 3u);
    for (uint32_t i = 0; i < checked->indices; i++) TIDE_CHECK(checked->index[i] < small.vertices);

    // With no game folder, they're made and kept for the process alone
    const mesh_lod *loose = mesh_lod_cached(none, &source, NULL);
    TIDE_CHECK(loose && loose->levels == made->levels && mesh_lod_made() == before + 4u);
    TIDE_CHECK(mesh_lod_cached(none, &source, NULL) == loose && mesh_lod_made() == before + 4u);
    for (int i = 0; i < 4; i++) remove(path[i]);
    shape_free(&small);
    shape_free(&s);
}

// A level only pays by the triangles it takes off, so none is made with
// fewer than a source says: the chain stops above it, and a mesh of under
// twice as many has no levels at all.
TIDE_TEST(mesh_lod_makes_no_level_under_the_fewest_triangles)
{
    shape s = ball(32, 64); // 3,968 triangles
    mesh_lod_source source = source_of(&s);
    mesh_lod all, some, none;
    mesh_lod_make(&source, &all);
    source.triangles_least = MESH_LOD_TRIANGLES_LEAST;
    mesh_lod_make(&source, &some);
    // 1,984 and 992, and not the 496 after them
    TIDE_REQUIRE(all.levels > 2u && some.levels == 2u);
    TIDE_CHECK(some.count[0] == all.count[0] && some.count[1] == all.count[1] && some.error[1] == all.error[1]);
    TIDE_CHECK(some.count[1] >= MESH_LOD_TRIANGLES_LEAST * 3u && all.count[2] < MESH_LOD_TRIANGLES_LEAST * 3u);
    TIDE_CHECK(memcmp(some.index, all.index, some.indices * sizeof(uint32_t)) == 0);
    shape light = ball(16, 32); // 960
    mesh_lod_source other = source_of(&light);
    other.triangles_least = MESH_LOD_TRIANGLES_LEAST;
    mesh_lod_make(&other, &none);
    TIDE_CHECK(none.levels == 0 && none.index == NULL && none.radius > 0.99f);
    other.triangles_least = 480; // Just enough for one
    mesh_lod_make(&other, &none);
    TIDE_CHECK(none.levels == 1 && none.count[0] <= 480u * 3u);
    mesh_lod_free(&all), mesh_lod_free(&some), mesh_lod_free(&none);
    shape_free(&light);
    shape_free(&s);
}
