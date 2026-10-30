#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "mesh.h"
#include "tide_test.h"

void b3t_check(bool enabled, bool whenever);
void b3t_forget(void);

_Static_assert(sizeof(Box3D_MeshCollider) == 56 && offsetof(Box3D_MeshCollider, vertices) == 0 &&
    offsetof(Box3D_MeshCollider, indices) == 8 && offsetof(Box3D_MeshCollider, scale) == 16 &&
    offsetof(Box3D_MeshCollider, offset) == 28 && offsetof(Box3D_MeshCollider, friction) == 40 &&
    offsetof(Box3D_MeshCollider, bounce) == 44 && sizeof(Box3D_Part) == 64 && offsetof(Box3D_Part, mesh) == 32,
    "mesh bridge must match the generated component layout");

static bool set_mesh(tide_world *w, tide_entity e, Box3D_MeshCollider mesh)
{
    Box3D_MeshCollider old=tide_get_Box3D_MeshCollider(w,e);
    tide_list_own(&mesh.vertices,sizeof(tide_float3),TIDE_IN_MATCH);
    tide_list_own(&mesh.indices,sizeof(int32_t),TIDE_IN_MATCH);
    tide_list_release(&old.vertices,TIDE_IN_MATCH);
    tide_list_release(&old.indices,TIDE_IN_MATCH);
    return tide_set_Box3D_MeshCollider(w,e,mesh);
}
static bool near(const float a, const float b) { return fabsf(a - b) < 0.0001f; }
static tide_entity tagged(tide_world *w, bool (*has)(const tide_world *, tide_entity))
{
    for (uint32_t i = 1; i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        if (has(w, e)) return e;
    }
    return (tide_entity){0};
}
static const Observed_local *probe(tide_world *w, tide_local *local)
{
    const uint64_t before = tide_world_hash(w);
    tide_draw_list draw = {0};
    tide_gui gui = {0};
    tide_frame(w, NULL, 1, NULL, NULL, 1, local, &draw, &gui);
    tide_draw_free(&draw);
    TIDE_CHECK(tide_world_hash(w) == before);
    return &local->Observed_local;
}
static void queries(tide_world *w, tide_local *local)
{
    const Observed_local *o = probe(w, local);
    TIDE_CHECK(o->current && !o->gap && !o->back && o->prepared && o->historical && !o->historicalGap && !o->historicalBack);
    TIDE_CHECK(near(o->historicalFraction, 0.5f));
    TIDE_CHECK(o->boxCast && o->sphereCast && o->capsuleCast && o->boxOverlap && o->sphereOverlap && o->capsuleOverlap && o->initial);
}
TIDE_TEST(box3d_mesh_queries_and_detached_history)
{
    b3t_check(true, true);
    tide_world w = {0}, saved = {0}, decoded = {0};
    tide_local local = {0};
    tide_world_init(&w, 1.0f / 60.0f);
    for (int i = 0; i < 4; i++) tide_world_tick(&w);
    w.Request.time = (ViewTime){.tick = 2, .alpha = 0.5f, .valid = true};
    TIDE_CHECK(w.Box3D_Physics.shapes == 4);
    queries(&w, &local);
    const tide_entity floor = tagged(&w, tide_has_Floor), ledge = tagged(&w, tide_has_Ledge);
    TIDE_CHECK(probe(&w, &local)->partEntity.index == ledge.index);
    Box3D_Body body = tide_get_Box3D_Body(&w, floor);
    body.position.y = 2;
    body.type = Box3D_BodyType_Kinematic;
    tide_set_Box3D_Body(&w, floor, body);
    tide_world_tick(&w);
    TIDE_CHECK(tide_get_Box3D_Body(&w, floor).type == Box3D_BodyType_Static);
    queries(&w, &local);
    TIDE_CHECK(near(probe(&w, &local)->fraction, 0.3f));
    tide_world_copy(&saved, &w);
    const tide_entity support = tagged(&w, tide_has_Support);
    Box3D_Body rotated = tide_get_Box3D_Body(&w, support);
    rotated.rotation = (tide_quaternion){{0, 0, 0.7071067811865475f, 0.7071067811865475f}};
    tide_set_Box3D_Body(&w, support, rotated);
    tide_world_tick(&w);
    const Observed_local *turned = probe(&w, &local);
    TIDE_CHECK(turned->rotated && turned->historicalRotated && !turned->part && !turned->historicalPart);
    tide_world_copy(&w, &saved);
    const uint32_t size = tide_world_pack(&w, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_CHECK(bytes != NULL);
    TIDE_CHECK(tide_world_pack(&w, bytes, size) == size);
    printf("  mesh canonical bytes: %u\n", size);
    b3t_forget();
    TIDE_CHECK(tide_world_unpack(&decoded, bytes, size));
    free(bytes);
    TIDE_CHECK(tide_world_hash(&decoded) == tide_world_hash(&w));
    queries(&decoded, &local);
    w.Request.removePart = true;
    tide_world_tick(&w);
    TIDE_CHECK(!probe(&w, &local)->part);
    tide_world_tick(&w);
    TIDE_CHECK(!probe(&w, &local)->part);
    Box3D_Body released = tide_get_Box3D_Body(&w, support);
    released.type = Box3D_BodyType_Kinematic;
    tide_set_Box3D_Body(&w, support, released);
    tide_world_tick(&w);
    TIDE_CHECK(tide_get_Box3D_Body(&w, support).type == Box3D_BodyType_Kinematic);
    tide_world_copy(&w, &saved);
    queries(&w, &local);
    TIDE_CHECK(probe(&w, &local)->part);
    w.Request.removeBody = true;
    tide_world_tick(&w);
    tide_world_tick(&w);
    TIDE_CHECK(!tide_has_Box3D_Part(&w, ledge));
    TIDE_CHECK(!probe(&w, &local)->part && probe(&w, &local)->savedHit);
    tide_world_free(&decoded); tide_world_free(&saved); tide_world_free(&w); tide_local_free(&local);
}
TIDE_TEST(box3d_mesh_contacts_preserve_concavity_and_replay)
{
    b3t_check(true, true);
    tide_world w = {0}, saved = {0}, decoded = {0};
    tide_world_init(&w, 1.0f / 60.0f);
    for (int i = 0; i < 10; i++) tide_world_tick(&w);
    tide_world_copy(&saved, &w);
    uint64_t sequence = 0;
    for (int i = 0; i < 110; i++) {
        tide_world_tick(&w);
        const uint64_t hash = tide_world_hash(&w);
        sequence = tide_hash_more(sequence, &hash, sizeof hash);
    }
    for (uint32_t i = 1; i < w.entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w.entities, i);
        if (!tide_has_Falling(&w, e)) continue;
        const Box3D_Body b = tide_get_Box3D_Body(&w, e);
        printf("  falling body gap=%d, y=%g\n", tide_get_Falling(&w, e).gap, (double)b.position.y);
        TIDE_CHECK(tide_get_Falling(&w, e).gap ? b.position.y < -10 : b.position.y >= 0.24f && b.position.y <= 0.27f);
    }
    printf("  mesh simulation sequence: %016llx\n", (unsigned long long)sequence);
    TIDE_CHECK(sequence == UINT64_C(0x271969b81c11eeb0));
    tide_world_copy(&decoded, &saved);
    for (int i = 0; i < 110; i++) tide_world_tick(&decoded);
    TIDE_CHECK(tide_world_hash(&decoded) == tide_world_hash(&w));
    tide_world_free(&decoded); tide_world_free(&saved); tide_world_free(&w);
}

TIDE_TEST(box3d_mesh_reads_chunked_source_lists)
{
    tide_world w = {0};
    tide_local local = {0};
    tide_world_init(&w, 1.0f / 60.0f);
    const int count = 4500;
    tide_float3 *vertices = malloc((size_t)count * sizeof *vertices);
    int32_t *indices = malloc((size_t)count * sizeof *indices);
    TIDE_REQUIRE(vertices && indices);
    for (int i = 0; i < count; i += 3) {
        const float x = i ? (float)i + 20 : -3;
        vertices[i] = (tide_float3){x, 0, -2};
        vertices[i + 1] = (tide_float3){x, 0, 2};
        vertices[i + 2] = (tide_float3){x + 1, 0, 2};
        indices[i] = i; indices[i + 1] = i + 1; indices[i + 2] = i + 2;
    }
    const uint64_t mark = tide_scratch_mark();
    const Box3D_MeshCollider mesh = {.vertices = tide_list_from(vertices, count, sizeof *vertices),
        .indices = tide_list_from(indices, count, sizeof *indices), .scale = {1, 1, 1}, .friction = 0.6f};
    TIDE_CHECK(set_mesh(&w, tagged(&w, tide_has_Floor), mesh));
    tide_scratch_reset(mark);
    free(vertices); free(indices);
    for (int i = 0; i < 4; i++) tide_world_tick(&w);
    w.Request.time = (ViewTime){.tick = 2, .alpha = 0.5f, .valid = true};
    TIDE_CHECK(w.Box3D_Physics.shapes == 4);
    queries(&w, &local);
    tide_world_free(&w); tide_local_free(&local);
}

TIDE_TEST(box3d_mesh_rejects_nonfinite_geometry_and_materials)
{
    for (int kind = 0; kind < 7; kind++) {
        tide_world w = {0};
        tide_world_init(&w, 1.0f / 60.0f);
        tide_float3 vertices[] = {{-3, 0, -2}, {-3, 0, 2}, {-1, 0, 2}};
        if (kind == 0) vertices[0].x = NAN;
        if (kind == 1) vertices[1].y = INFINITY;
        if (kind == 2) vertices[2].z = 1e30f;
        const int32_t indices[] = {0, 1, 2};
        const uint64_t mark = tide_scratch_mark();
        Box3D_MeshCollider mesh = {.vertices = tide_list_from(vertices, 3, sizeof *vertices),
            .indices = tide_list_from(indices, 3, sizeof *indices), .scale = {1, 1, 1}, .friction = 0.6f};
        if (kind == 3) mesh.scale.x = NAN;
        if (kind == 4) mesh.offset.z = INFINITY;
        if (kind == 5) mesh.friction = -1;
        if (kind == 6) mesh.bounce = NAN;
        TIDE_CHECK(set_mesh(&w, tagged(&w, tide_has_Floor), mesh));
        tide_scratch_reset(mark);
        tide_world_tick(&w);
        tide_world_tick(&w);
        TIDE_CHECK(w.Box3D_Physics.shapes == 3);
        tide_world_free(&w);
    }
}

TIDE_TEST(box3d_mesh_shape_counter_wraps_like_tide)
{
    tide_world w = {0};
    tide_world_init(&w, 1.0f / 60.0f);
    w.Box3D_Physics.shapes = INT32_MAX - 3;
    tide_world_tick(&w);
    tide_world_tick(&w);
    TIDE_CHECK(w.Box3D_Physics.shapes == INT32_MIN);
    tide_world_tick(&w);
    TIDE_CHECK(w.Box3D_Physics.shapes == INT32_MIN);
    tide_world_free(&w);
}
