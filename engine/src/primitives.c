#include "tide/draw.h"

#include "tide/net.h"

// Every primitive is one unit mesh, colored when the list first needs it.
// Its dimensions and position belong to the instance, so drawing another
// size neither builds nor uploads geometry again. Colors are versions of
// the mesh: a later color cannot change what an earlier command draws.
enum { PRIMITIVE_CUBE, PRIMITIVE_SPHERE, PRIMITIVE_CYLINDER, PRIMITIVE_PLANE };
static const char primitive_ids[4] = {0};

enum { SEGMENTS = 24, SPHERE_RINGS = 12 };

static void fill_cube(tide_draw_list *d, const uint32_t slot, const tide_color color)
{
    const tide_vertex3 vertices[8] = {
        {{-0.5f, -0.5f, -0.5f}, {0, 1}, color}, {{0.5f, -0.5f, -0.5f}, {1, 1}, color},
        {{0.5f, 0.5f, -0.5f}, {1, 0}, color}, {{-0.5f, 0.5f, -0.5f}, {0, 0}, color},
        {{-0.5f, -0.5f, 0.5f}, {0, 1}, color}, {{0.5f, -0.5f, 0.5f}, {1, 1}, color},
        {{0.5f, 0.5f, 0.5f}, {1, 0}, color}, {{-0.5f, 0.5f, 0.5f}, {0, 0}, color},
    };
    static const uint32_t indices[36] = {0, 3, 2, 0, 2, 1, 4, 5, 6, 4, 6, 7,
                                       0, 1, 5, 0, 5, 4, 3, 7, 6, 3, 6, 2,
                                       0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5};
    tide_draw_mesh_fill(d, slot, vertices, 8, indices, 36);
}

static void fill_sphere(tide_draw_list *d, const uint32_t slot, const tide_color color)
{
    enum { VERTICES = 2 + (SPHERE_RINGS - 1) * SEGMENTS, INDICES = 6 * SEGMENTS * (SPHERE_RINGS - 1) };
    tide_vertex3 vertices[VERTICES];
    uint32_t indices[INDICES];
    vertices[0] = (tide_vertex3){{0, 1, 0}, {0.5f, 0}, color};
    vertices[VERTICES - 1] = (tide_vertex3){{0, -1, 0}, {0.5f, 1}, color};
    for (uint32_t ring = 1; ring < SPHERE_RINGS; ring++) {
        const float v = (float)ring / SPHERE_RINGS;
        const float angle = TIDE_PI_F * v;
        const float radius = tide_sin_f(angle), y = tide_cos_f(angle);
        for (uint32_t side = 0; side < SEGMENTS; side++) {
            const float u = (float)side / SEGMENTS, around = 2.0f * TIDE_PI_F * u;
            vertices[1u + (ring - 1u) * SEGMENTS + side] =
                (tide_vertex3){{radius * tide_cos_f(around), y, radius * tide_sin_f(around)}, {u, v}, color};
        }
    }
    uint32_t count = 0;
    for (uint32_t side = 0; side < SEGMENTS; side++) {
        const uint32_t next = (side + 1u) % SEGMENTS;
        indices[count++] = 0;
        indices[count++] = 1u + next;
        indices[count++] = 1u + side;
        for (uint32_t ring = 0; ring < SPHERE_RINGS - 2u; ring++) {
            const uint32_t a = 1u + ring * SEGMENTS + side, b = 1u + ring * SEGMENTS + next;
            indices[count++] = a;
            indices[count++] = b;
            indices[count++] = a + SEGMENTS;
            indices[count++] = b;
            indices[count++] = b + SEGMENTS;
            indices[count++] = a + SEGMENTS;
        }
        indices[count++] = VERTICES - 1u;
        indices[count++] = 1u + (SPHERE_RINGS - 2u) * SEGMENTS + side;
        indices[count++] = 1u + (SPHERE_RINGS - 2u) * SEGMENTS + next;
    }
    tide_draw_mesh_fill(d, slot, vertices, VERTICES, indices, count);
}

static void fill_cylinder(tide_draw_list *d, const uint32_t slot, const tide_color color)
{
    enum { VERTICES = 2 * SEGMENTS + 2, INDICES = 12 * SEGMENTS };
    tide_vertex3 vertices[VERTICES];
    uint32_t indices[INDICES];
    vertices[2 * SEGMENTS] = (tide_vertex3){{0, -0.5f, 0}, {0.5f, 0.5f}, color};
    vertices[2 * SEGMENTS + 1] = (tide_vertex3){{0, 0.5f, 0}, {0.5f, 0.5f}, color};
    uint32_t count = 0;
    for (uint32_t side = 0; side < SEGMENTS; side++) {
        const float u = (float)side / SEGMENTS, angle = 2.0f * TIDE_PI_F * u;
        const float x = tide_cos_f(angle), z = tide_sin_f(angle);
        vertices[side] = (tide_vertex3){{x, -0.5f, z}, {u, 1}, color};
        vertices[side + SEGMENTS] = (tide_vertex3){{x, 0.5f, z}, {u, 0}, color};
        const uint32_t next = (side + 1u) % SEGMENTS;
        indices[count++] = side;
        indices[count++] = side + SEGMENTS;
        indices[count++] = next;
        indices[count++] = next;
        indices[count++] = side + SEGMENTS;
        indices[count++] = next + SEGMENTS;
        indices[count++] = 2 * SEGMENTS;
        indices[count++] = side;
        indices[count++] = next;
        indices[count++] = 2 * SEGMENTS + 1;
        indices[count++] = next + SEGMENTS;
        indices[count++] = side + SEGMENTS;
    }
    tide_draw_mesh_fill(d, slot, vertices, VERTICES, indices, count);
}

static void fill_plane(tide_draw_list *d, const uint32_t slot, const tide_color color)
{
    const tide_vertex3 vertices[4] = {
        {{-0.5f, 0, -0.5f}, {0, 1}, color}, {{0.5f, 0, -0.5f}, {1, 1}, color},
        {{0.5f, 0, 0.5f}, {1, 0}, color}, {{-0.5f, 0, 0.5f}, {0, 0}, color},
    };
    static const uint32_t indices[6] = {0, 2, 1, 0, 3, 2};
    tide_draw_mesh_fill(d, slot, vertices, 4, indices, 6);
}

static uint32_t primitive(tide_draw_list *d, const uint32_t shape, const tide_color color)
{
    bool fill;
    const uint32_t slot = tide_draw_mesh_slot(d, TIDE_MESH_MEMORY, (uint64_t)(uintptr_t)&primitive_ids[shape], 0,
                                             tide_hash(&color, sizeof color), &fill);
    if (fill) {
        switch (shape) {
        case PRIMITIVE_CUBE: fill_cube(d, slot, color); break;
        case PRIMITIVE_SPHERE: fill_sphere(d, slot, color); break;
        case PRIMITIVE_CYLINDER: fill_cylinder(d, slot, color); break;
        case PRIMITIVE_PLANE: fill_plane(d, slot, color); break;
        }
    }
    return slot;
}

void tide_draw_cube_rotated(tide_draw_list *d, const tide_float3 center, const tide_float3 size,
                             const tide_quaternion rotation, const tide_color color)
{
    if (!(size.x > 0 && size.y > 0 && size.z > 0)) return;
    tide_draw_instance(d, primitive(d, PRIMITIVE_CUBE, color), tide_trs_f4x4(center, rotation, size), 0, TIDE_FILTER_POINT,
                       TIDE_WRAP_CLAMP);
}

void tide_draw_cube(tide_draw_list *d, const tide_float3 center, const tide_float3 size, const tide_color color)
{
    tide_draw_cube_rotated(d, center, size, tide_identity_q(), color);
}

void tide_draw_sphere(tide_draw_list *d, const tide_float3 center, const float radius, const tide_color color)
{
    if (!(radius > 0)) return;
    tide_draw_instance(d, primitive(d, PRIMITIVE_SPHERE, color),
                       tide_trs_f4x4(center, tide_identity_q(), tide_f3_splat(radius)), 0, TIDE_FILTER_POINT, TIDE_WRAP_CLAMP);
}

void tide_draw_cylinder_rotated(tide_draw_list *d, const tide_float3 center, const float radius, const float height,
                                 const tide_quaternion rotation, const tide_color color)
{
    if (!(radius > 0 && height > 0)) return;
    tide_draw_instance(d, primitive(d, PRIMITIVE_CYLINDER, color),
                       tide_trs_f4x4(center, rotation, tide_f3(radius, height, radius)), 0, TIDE_FILTER_POINT, TIDE_WRAP_CLAMP);
}

void tide_draw_cylinder(tide_draw_list *d, const tide_float3 center, const float radius, const float height,
                         const tide_color color)
{
    tide_draw_cylinder_rotated(d, center, radius, height, tide_identity_q(), color);
}

void tide_draw_plane_rotated(tide_draw_list *d, const tide_float3 center, const tide_float2 size,
                              const tide_quaternion rotation, const tide_color color)
{
    if (!(size.x > 0 && size.y > 0)) return;
    tide_draw_instance(d, primitive(d, PRIMITIVE_PLANE, color),
                       tide_trs_f4x4(center, rotation, tide_f3(size.x, 1, size.y)), 0, TIDE_FILTER_POINT, TIDE_WRAP_CLAMP);
}

void tide_draw_plane(tide_draw_list *d, const tide_float3 center, const tide_float2 size, const tide_color color)
{
    tide_draw_plane_rotated(d, center, size, tide_identity_q(), color);
}
