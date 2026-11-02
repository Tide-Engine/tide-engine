#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

static tide_world world;

static void start(void)
{
    tide_world_free(&world);
    world = (tide_world){0};
    tide_world_init(&world, 0.1f);
    tide_world_tick(&world);
}

#define AT(type, list, i) ((const type *)tide_list_at((list), (i), sizeof(type)))
#define INDEX(list, i) (*AT(int32_t, (list), (i)))

static float dot3(const tide_float3 a, const tide_float3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static tide_float3 sub3(const tide_float3 a, const tide_float3 b) { return (tide_float3){a.x - b.x, a.y - b.y, a.z - b.z}; }
static tide_float3 cross3(const tide_float3 a, const tide_float3 b)
{
    return (tide_float3){a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
static bool unit3(const tide_float3 v) { return fabsf(sqrtf(dot3(v, v)) - 1.0f) < 1e-5f; }
static bool near3(const tide_float3 a, const tide_float3 b, const float most)
{
    return fabsf(a.x - b.x) <= most && fabsf(a.y - b.y) <= most && fabsf(a.z - b.z) <= most;
}

// Each triangle's front, clockwise as Tide's are, is the way its first corner's
// normal goes, and faces away from `center`.
#define FRONTS_OUT(type, vertices, indices, center, first_triangle)                                              \
    do {                                                                                                         \
        for (int32_t t = (first_triangle); t + 2 < tide_list_count(indices); t += 3) {                           \
            const type *va_ = AT(type, vertices, INDEX(indices, t));                                               \
            const type *vb_ = AT(type, vertices, INDEX(indices, t + 1));                                           \
            const type *vc_ = AT(type, vertices, INDEX(indices, t + 2));                                           \
            const tide_float3 facing = cross3(sub3(vb_->position, va_->position), sub3(vc_->position, va_->position));   \
            TIDE_CHECK(dot3(facing, va_->normal) > 0);                                                             \
            const tide_float3 middle = {(va_->position.x + vb_->position.x + vc_->position.x) / 3 - (center).x,        \
                                        (va_->position.y + vb_->position.y + vc_->position.y) / 3 - (center).y,        \
                                        (va_->position.z + vb_->position.z + vc_->position.z) / 3 - (center).z};       \
            TIDE_CHECK(dot3(facing, middle) > 0);                                                                \
        }                                                                                                        \
    } while (0)

// A Full vertex's normal and tangent: unit, at right angles, w a side.
static void check_frame(const Full *v)
{
    TIDE_CHECK(unit3(v->normal));
    const tide_float3 t = {v->tangent.x, v->tangent.y, v->tangent.z};
    TIDE_CHECK(unit3(t));
    TIDE_CHECK(fabsf(dot3(t, v->normal)) < 1e-5f);
    TIDE_CHECK(v->tangent.w == 1.0f || v->tangent.w == -1.0f);
}

TIDE_TEST(geometry_box_has_sharp_faces_facing_out)
{
    start();
    const Built *b = &world.Built;
    TIDE_CHECK(tide_list_count(b->box) == 24);
    TIDE_CHECK(tide_list_count(b->boxFaces) == 36);
    const tide_float3 origin = {0, 0, 0};
    FRONTS_OUT(Full, b->box, b->boxFaces, origin, 0);
    for (int32_t i = 0; i < 24; i++) {
        const Full *v = AT(Full, b->box, i);
        check_frame(v);
        TIDE_CHECK(v->color.r == 1 && v->color.g == 1 && v->color.b == 1 && v->color.a == 1); // The struct's default
        TIDE_CHECK(v->uv.x == 0 || v->uv.x == 1);
        // Its texture's u goes along its tangent: from the corner at u = 0 to the one at u = 1
    }
    TIDE_CHECK(near3(b->boxBounds.min, (tide_float3){-1, -2, -3}, 0));
    TIDE_CHECK(near3(b->boxBounds.max, (tide_float3){1, 2, 3}, 0));
    TIDE_CHECK(b->emptyHasNoBounds);
}

TIDE_TEST(geometry_tangents_follow_the_texture)
{
    start();
    const Built *b = &world.Built;
    // On each box face, from its u = 0 corner to its u = 1 corner is along the tangent
    for (int32_t f = 0; f < 6; f++) {
        const Full *tl = AT(Full, b->box, f * 4), *tr = AT(Full, b->box, f * 4 + 1), *bl = AT(Full, b->box, f * 4 + 3);
        const tide_float3 along = sub3(tr->position, tl->position);
        const tide_float3 t = {tl->tangent.x, tl->tangent.y, tl->tangent.z};
        TIDE_CHECK(dot3(along, t) > 0);
        // ...and v goes down the texture, from its top: the side w says
        const tide_float3 down = sub3(bl->position, tl->position);
        TIDE_CHECK(dot3(cross3(tl->normal, t), down) * tl->tangent.w > 0);
    }
}

TIDE_TEST(geometry_sphere_is_smooth_and_closed)
{
    start();
    const Built *b = &world.Built;
    TIDE_CHECK(tide_list_count(b->ball) == 9 * 13);
    TIDE_CHECK(tide_list_count(b->ballFaces) == (8 * 12 * 2 - 2 * 12) * 3); // No triangles of nothing at the poles
    const tide_float3 origin = {0, 0, 0};
    FRONTS_OUT(Full, b->ball, b->ballFaces, origin, 0);
    for (int32_t i = 0; i < tide_list_count(b->ball); i++) {
        const Full *v = AT(Full, b->ball, i);
        check_frame(v);
        TIDE_CHECK(near3(v->normal, v->position, 1e-6f)); // Radius 1: a normal is where it is
    }
}

TIDE_TEST(geometry_plane_and_cone)
{
    start();
    const Built *b = &world.Built;
    TIDE_CHECK(tide_list_count(b->plane) == 5 * 3);
    TIDE_CHECK(tide_list_count(b->planeFaces) == 4 * 2 * 6);
    const tide_float3 below = {0, -1, 0};
    FRONTS_OUT(Corner, b->plane, b->planeFaces, below, 0);
    TIDE_CHECK(near3(AT(Corner, b->plane, 0)->position, (tide_float3){-2, 0, 1}, 0)); // Top left: -x, +z
    TIDE_CHECK(tide_list_count(b->cone) == 18 + 10 + 10);
    TIDE_CHECK(tide_list_count(b->coneFaces) == 48 + 24 + 24);
    const tide_float3 origin = {0, 0, 0};
    FRONTS_OUT(Full, b->cone, b->coneFaces, origin, 0);
    for (int32_t i = 0; i < tide_list_count(b->cone); i++) check_frame(AT(Full, b->cone, i));
}

TIDE_TEST(geometry_2d_shapes_keep_the_structs_other_fields)
{
    start();
    const Built *b = &world.Built;
    TIDE_CHECK(tide_list_count(b->flat) == 7);
    TIDE_CHECK(tide_list_count(b->flatFaces) == 9);
    const Flat *v = AT(Flat, b->flat, 0);
    TIDE_CHECK(v->position.x == 0 && v->position.y == 1);
    TIDE_CHECK(v->color.r == 1 && v->color.g == 0);
    TIDE_CHECK(INDEX(b->flatFaces, 6) == 4); // The triangle's corners come after the quad's
}

TIDE_TEST(geometry_append_moves_and_mirrors)
{
    start();
    const Built *b = &world.Built;
    TIDE_CHECK(tide_list_count(b->merged) == 48);
    TIDE_CHECK(tide_list_count(b->mergedFaces) == 72);
    TIDE_CHECK(b->mirroredFirst == 24);
    const tide_float3 moved = {10, 0, 0};
    for (int32_t t = 0; t < 36; t += 3) {
        const Corner *a = AT(Corner, b->merged, INDEX(b->mergedFaces, t));
        TIDE_CHECK(INDEX(b->mergedFaces, t) < 24);
        TIDE_CHECK(dot3(a->normal, a->position) > 0);
    }
    // The mirrored copy's triangles still face out, as their normals do
    for (int32_t t = 36; t < 72; t += 3) TIDE_CHECK(INDEX(b->mergedFaces, t) >= 24);
    {
        const Corner *a = AT(Corner, b->merged, INDEX(b->mergedFaces, 36));
        TIDE_CHECK(dot3(a->normal, sub3(a->position, moved)) > 0);
    }
    for (int32_t t = 36; t + 2 < 72; t += 3) {
        const Corner *a = AT(Corner, b->merged, INDEX(b->mergedFaces, t));
        const Corner *c1 = AT(Corner, b->merged, INDEX(b->mergedFaces, t + 1));
        const Corner *c2 = AT(Corner, b->merged, INDEX(b->mergedFaces, t + 2));
        const tide_float3 facing = cross3(sub3(c1->position, a->position), sub3(c2->position, a->position));
        TIDE_CHECK(dot3(facing, a->normal) > 0);
        TIDE_CHECK(dot3(facing, sub3(a->position, moved)) > 0);
    }
    TIDE_CHECK(near3(b->mirroredBounds.min, (tide_float3){-0.5f, -0.5f, -0.5f}, 0));
    TIDE_CHECK(near3(b->mirroredBounds.max, (tide_float3){10.5f, 0.5f, 0.5f}, 0));
}

TIDE_TEST(geometry_weld_by_value_and_by_position)
{
    start();
    const Built *b = &world.Built;
    TIDE_CHECK(tide_list_count(b->welded) == 24); // No two of a box's corners are equal: their normals differ
    TIDE_CHECK(tide_list_count(b->near) == 8);
    TIDE_CHECK(tide_list_count(b->nearFaces) == 36);
    const tide_float3 origin = {0, 0, 0};
    FRONTS_OUT(Corner, b->near, b->nearFaces, origin, 0);
    for (int32_t i = 0; i < 8; i++) {
        const Corner *v = AT(Corner, b->near, i);
        TIDE_CHECK(unit3(v->normal));
        TIDE_CHECK(dot3(v->normal, v->position) > 0.5f); // Smooth, out of each corner
    }
}

TIDE_TEST(geometry_recalculated_frames_follow_the_ground)
{
    start();
    const Built *b = &world.Built;
    const float s = 1.0f / sqrtf(1.25f);
    for (int32_t i = 0; i < tide_list_count(b->ground); i++) {
        const Full *v = AT(Full, b->ground, i);
        check_frame(v);
        TIDE_CHECK(near3(v->normal, (tide_float3){-0.5f * s, s, 0}, 1e-5f));
        TIDE_CHECK(near3((tide_float3){v->tangent.x, v->tangent.y, v->tangent.z}, (tide_float3){s, 0.5f * s, 0}, 1e-5f));
        TIDE_CHECK(v->tangent.w == 1.0f); // As the flat plane's
    }
}

TIDE_TEST(geometry_remove_unused)
{
    start();
    const Built *b = &world.Built;
    TIDE_CHECK(tide_list_count(b->trimmed) == 3);
    TIDE_CHECK(tide_list_count(b->trimmedFaces) == 3);
    TIDE_CHECK(INDEX(b->trimmedFaces, 0) == 0 && INDEX(b->trimmedFaces, 1) == 1 && INDEX(b->trimmedFaces, 2) == 2);
    TIDE_CHECK(AT(Corner, b->trimmed, 2)->position.y == 1);
}

// The same bits on every machine: native, web and every configuration
TIDE_TEST(geometry_shapes_are_the_same_bits_everywhere)
{
    start();
    const Built *b = &world.Built;
    uint32_t h = 2166136261u;
    const tide_list lists[] = {b->ball, b->cone, b->ground, b->near};
    const uint32_t sizes[] = {sizeof(Full), sizeof(Full), sizeof(Full), sizeof(Corner)};
    for (int l = 0; l < 4; l++) {
        const uint8_t *bytes = tide_list_at(lists[l], 0, sizes[l]);
        for (size_t i = 0; i < (size_t)tide_list_count(lists[l]) * sizes[l]; i++) h = (h ^ bytes[i]) * 16777619u;
    }
    if (h != 2466882126u) printf("geometry hash %u\n", h);
    TIDE_CHECK(h == 2466882126u);
}
