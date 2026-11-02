#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "tide/draw.h"
#include "tide/heap.h"
#include "tide/page.h"
#include "tide/list.h"
#include "tide/mesh.h"
#include "tide/text.h"
#include "tide_test.h"

// Instances of a list the draw list keeps (tide_draw_mesh_shaded_list): the runs it records are the runs the same
// instances' bytes make (tide_draw_mesh_shaded_instances), culled, merged and in order, with places for bytes.

static const tide_vertex3 corners[] = {
    {.position = {-0.25f, -0.25f, 0}}, {.position = {0, 0.25f, 0}}, {.position = {0.25f, -0.25f, 0}},
};
static const uint32_t faces[] = {0, 1, 2};

// A transform, a float4 and an int4 an instance: five float packs and one of ints.
static const tide_shader listed_shader = {
    .name = "listed", .dims = 3, .vertex_size = sizeof(tide_vertex3), .instance_floats = 5, .instance_ints = 1,
    .bounds_position = 1 + offsetof(tide_vertex3, position), .glsl_listed_vertex = "listed",
};
// The same, with no listed vertex stage: a game built before it, or a shader whose instances aren't its list's
static const tide_shader unlisted_shader = {
    .name = "unlisted", .dims = 3, .vertex_size = sizeof(tide_vertex3), .instance_floats = 5, .instance_ints = 1,
    .bounds_position = 1 + offsetof(tide_vertex3, position),
};

typedef struct item {
    tide_float4x4 transform;
    float value;
    int32_t kind;
} item;

typedef struct instance {
    float f[20];
    int32_t i[4];
} instance;

static void write_item(const void *from, int32_t *words)
{
    const item *it = from;
    instance out;
    memset(&out, 0, sizeof out);
    memcpy(out.f, &it->transform, sizeof it->transform);
    out.f[16] = it->value;
    out.f[17] = it->value * 2.0f;
    out.i[0] = it->kind;
    memcpy(words, &out, sizeof out);
}

static tide_float4x4 at(const float x, const float y, const float z)
{
    tide_float4x4 m = tide_identity_f4x4();
    m.c3 = tide_f4(x, y, z, 1);
    return m;
}

static void camera(tide_draw_list *d)
{
    tide_draw_screen_size(d, tide_f2(400, 400));
    tide_draw_set_camera_matrices(d, tide_identity_f4x4(), tide_identity_f4x4());
}

// A list of `count` items, every third outside the view and every fifth mirrored, in the local world's heap.
static tide_list items_of(const int32_t count, const int32_t seed)
{
    tide_list list = {0};
    tide_list_resize(&list, count, sizeof(item), TIDE_IN_LOCAL);
    for (int32_t i = 0; i < count; i++) {
        item *it = tide_list_at_mut(list, i, sizeof(item));
        memset(it, 0, sizeof *it);
        it->transform = at(i % 3 == 0 ? 3.0f : (float)(i % 7) * 0.1f - 0.3f, 0, 0);
        if (i % 5 == 0) it->transform.c0.x = -1.0f;
        it->value = (float)(i + seed);
        it->kind = i * 3 + seed;
    }
    return list;
}

// What the caller packs on the copying path: each item's instance, one after the other.
static instance *packed_of(const tide_list list)
{
    const int32_t count = tide_list_count(list);
    instance *out = tide_alloc((size_t)count * sizeof *out);
    for (int32_t i = 0; i < count; i++) write_item(tide_list_at(list, i, sizeof(item)), (int32_t *)(void *)&out[i]);
    return out;
}

// The two draw lists have the same commands and runs, run for run, with each instance's bytes the same: one's
// runs listed (`listed` of them, or all where -1), the other's not.
static void same_runs(const tide_draw_list *listed, const tide_draw_list *copied, const int32_t expect_listed)
{
    TIDE_REQUIRE(listed->count == copied->count);
    TIDE_REQUIRE(listed->run_count == copied->run_count);
    int32_t lists = 0;
    for (uint32_t i = 0; i < listed->count; i++) {
        const tide_draw_command *a = &listed->commands[i], *b = &copied->commands[i];
        TIDE_CHECK(a->kind == b->kind && a->blend == b->blend && a->depth == b->depth && a->cull == b->cull);
        if (a->kind != TIDE_DRAW_SHADED) continue;
        const tide_draw_run *x = &listed->runs[a->shaded.run], *y = &copied->runs[b->shaded.run];
        TIDE_CHECK(x->count == y->count && x->shader == y->shader && !y->listed);
        TIDE_CHECK(listed->meshes[x->mesh - 1u].index_count == copied->meshes[y->mesh - 1u].index_count);
        lists += x->listed != 0;
        for (uint32_t k = 0; k < x->count; k++) {
            const void *p = tide_draw_run_instance(listed, x, k), *q = tide_draw_run_instance(copied, y, k);
            TIDE_REQUIRE(p && q);
            TIDE_CHECK(memcmp(p, q, sizeof(instance)) == 0);
        }
        TIDE_CHECK(tide_draw_run_instance(listed, x, x->count) == NULL);
    }
    if (expect_listed >= 0) TIDE_CHECK(lists == expect_listed);
}

static void draw_both(tide_draw_list *listed, tide_draw_list *copied, const tide_shader *shader, const tide_list list,
                      const tide_list vertices, const tide_list indices, const bool expect)
{
    TIDE_CHECK(tide_draw_mesh_shaded_list(listed, shader, vertices, indices, list, sizeof(item), write_item, 0x80000001u,
                                          NULL, NULL, 0) == expect);
    instance *packed = packed_of(list);
    if (!expect) {
        tide_draw_mesh_shaded_instances(listed, shader, vertices, indices, packed, (uint32_t)tide_list_count(list), NULL,
                                        NULL, 0);
    }
    tide_draw_mesh_shaded_instances(copied, shader, vertices, indices, packed, (uint32_t)tide_list_count(list), NULL,
                                    NULL, 0);
    free(packed);
}

TIDE_TEST(draw_listed_runs_are_the_copied_runs)
{
    const uint64_t mark = tide_scratch_mark();
    tide_heap heap = {0};
    tide_text_use(NULL, &heap);
    const tide_list vertices = tide_list_from(corners, 3, sizeof(tide_vertex3));
    const tide_list indices = tide_list_from(faces, 3, sizeof(uint32_t));
    tide_list first = items_of(40, 1), second = items_of(25, 7);
    tide_draw_list listed = {0}, copied = {0};
    for (int frame = 0; frame < 6; frame++) {
        tide_draw_reset(&listed);
        tide_draw_reset(&copied);
        camera(&listed);
        camera(&copied);
        if (frame == 3) { // A list that changed: its numbers are the frame's own again, and right
            item *it = tide_list_at_mut(first, 4, sizeof(item));
            it->value = -5.0f;
            it->transform = at(0.05f, 0, 0);
        }
        // Two lists one after the other through one shader and mesh are one run, as their bytes were; the
        // instances culled are left out of both, and the mirrored ones are runs of their own
        draw_both(&listed, &copied, &listed_shader, first, vertices, indices, true);
        draw_both(&listed, &copied, &listed_shader, second, vertices, indices, true);
        // ...and the first again, then culling turned off
        draw_both(&listed, &copied, &listed_shader, first, vertices, indices, true);
        tide_draw_set_frustum_culling(&listed, false);
        tide_draw_set_frustum_culling(&copied, false);
        draw_both(&listed, &copied, &listed_shader, second, vertices, indices, true);
        same_runs(&listed, &copied, -1);
        uint32_t runs = 0;
        for (uint32_t i = 0; i < listed.count; i++) {
            if (listed.commands[i].kind != TIDE_DRAW_SHADED) continue;
            TIDE_CHECK(listed.runs[listed.commands[i].shaded.run].listed);
            runs++;
        }
        TIDE_CHECK(runs > 1 && runs < 12); // Merged, and split where mirrored
        // Places, 4 bytes an instance drawn, where the copies took the instances' bytes
        TIDE_CHECK(listed.instance_used * sizeof(instance) == copied.instance_used * sizeof(uint32_t));
    }
    tide_draw_free(&listed);
    tide_draw_free(&copied);
    tide_list_release(&first, TIDE_IN_LOCAL);
    tide_list_release(&second, TIDE_IN_LOCAL);
    tide_scratch_reset(mark);
    tide_text_use(NULL, NULL);
    tide_heap_free(&heap);
}

// What a page sends: nothing for a list that stayed as it was, and only the rows of one that changed.
TIDE_TEST(draw_listed_numbers_are_sent_once)
{
    const uint64_t mark = tide_scratch_mark();
    tide_heap heap = {0};
    tide_text_use(NULL, &heap);
    const tide_list vertices = tide_list_from(corners, 3, sizeof(tide_vertex3));
    const tide_list indices = tide_list_from(faces, 3, sizeof(uint32_t));
    tide_list list = items_of(3000, 2);
    tide_draw_list d = {0}, copied = {0};
    uint64_t versions[5] = {0};
    uint32_t slots[5] = {0};
    for (int frame = 0; frame < 5; frame++) {
        tide_draw_reset(&d);
        tide_draw_reset(&copied);
        camera(&d);
        camera(&copied);
        if (frame == 3) ((item *)tide_list_at_mut(list, 2999, sizeof(item)))->kind = 12345;
        draw_both(&d, &copied, &listed_shader, list, vertices, indices, true);
        same_runs(&d, &copied, -1);
        const tide_draw_run *run = &d.runs[d.commands[d.count - 1u].shaded.run];
        TIDE_REQUIRE(run->listed);
        slots[frame] = run->textures[0];
        TIDE_REQUIRE(slots[frame] && slots[frame] <= d.texture_count);
        versions[frame] = d.textures[slots[frame] - 1u].version;
    }
    // Frame 0 had it as the frame's own, frame 1 kept it, frame 2 sent nothing, frame 3 had it as the frame's own
    // again, as it changed, and frame 4 kept it again in its place: only its rows are sent
    TIDE_CHECK(versions[2] == versions[1] && versions[3] != versions[2] && versions[4] != versions[3]);
    const tide_draw_texture *page = &d.textures[slots[4] - 1u];
    const tide_draw_area *areas;
    uint32_t count;
    TIDE_REQUIRE(tide_draw_texture_since(page, versions[3], &areas, &count));
    TIDE_CHECK(count >= 1 && areas[0].y == 0 && areas[0].height <= 18 && areas[0].height < page->height);
    // A frame that draws none of it lets its page go, and the next makes it again, right
    tide_draw_reset(&d);
    tide_draw_reset(&d);
    tide_draw_reset(&copied);
    camera(&d);
    camera(&copied);
    draw_both(&d, &copied, &listed_shader, list, vertices, indices, true);
    same_runs(&d, &copied, -1);
    // Forgetting forgets them all
    tide_draw_forget(&d);
    tide_draw_reset(&d);
    tide_draw_reset(&copied);
    camera(&d);
    camera(&copied);
    draw_both(&d, &copied, &listed_shader, list, vertices, indices, true);
    same_runs(&d, &copied, -1);
    tide_draw_free(&d);
    TIDE_CHECK(d.listed == NULL);
    tide_draw_free(&copied);
    tide_list_release(&list, TIDE_IN_LOCAL);
    tide_scratch_reset(mark);
    tide_text_use(NULL, NULL);
    tide_heap_free(&heap);
}

// Appending another list's listed runs, or unlisting one, gives the instances' bytes.
TIDE_TEST(draw_listed_runs_append_and_unlist_as_bytes)
{
    const uint64_t mark = tide_scratch_mark();
    tide_heap heap = {0};
    tide_text_use(NULL, &heap);
    const tide_list vertices = tide_list_from(corners, 3, sizeof(tide_vertex3));
    const tide_list indices = tide_list_from(faces, 3, sizeof(uint32_t));
    tide_list list = items_of(30, 3);
    tide_draw_list d = {0}, copied = {0}, appended = {0};
    for (int frame = 0; frame < 2; frame++) {
        tide_draw_reset(&d);
        tide_draw_reset(&copied);
        camera(&d);
        camera(&copied);
        draw_both(&d, &copied, &listed_shader, list, vertices, indices, true);
    }
    tide_draw_append(&appended, &d);
    same_runs(&appended, &copied, 0);
    for (uint32_t i = 0; i < d.run_count; i++) {
        const uint32_t texture = d.runs[i].textures[listed_shader.texture_count];
        tide_draw_run_unlist(&d, i);
        TIDE_CHECK(!d.runs[i].listed && d.runs[i].textures[listed_shader.texture_count] == 0 && texture != 0);
    }
    same_runs(&d, &copied, 0);
    tide_draw_free(&appended);
    tide_draw_free(&d);
    tide_draw_free(&copied);
    tide_list_release(&list, TIDE_IN_LOCAL);
    tide_scratch_reset(mark);
    tide_text_use(NULL, NULL);
    tide_heap_free(&heap);
}

// What draws its instances' bytes instead: a shader with no listed stage, a 3D transform, nothing to draw. A list
// made along the way is the frame's own.
TIDE_TEST(draw_listed_declines_what_it_cannot_draw)
{
    const uint64_t mark = tide_scratch_mark();
    tide_heap heap = {0};
    tide_text_use(NULL, &heap);
    const tide_list vertices = tide_list_from(corners, 3, sizeof(tide_vertex3));
    const tide_list indices = tide_list_from(faces, 3, sizeof(uint32_t));
    tide_list list = items_of(12, 4);
    tide_draw_list d = {0}, copied = {0};
    camera(&d);
    camera(&copied);
    draw_both(&d, &copied, &unlisted_shader, list, vertices, indices, false);
    tide_draw_set_transform_3d(&d, at(0.01f, 0, 0));
    tide_draw_set_transform_3d(&copied, at(0.01f, 0, 0));
    draw_both(&d, &copied, &listed_shader, list, vertices, indices, false);
    tide_draw_unset_transforms(&d);
    tide_draw_unset_transforms(&copied);
    same_runs(&d, &copied, 0);
    tide_list empty = {0};
    TIDE_CHECK(!tide_draw_mesh_shaded_list(&d, &listed_shader, vertices, indices, empty, sizeof(item), write_item, 1u,
                                           NULL, NULL, 0));
    item made[4];
    for (int i = 0; i < 4; i++) memcpy(&made[i], tide_list_at(list, i + 1, sizeof(item)), sizeof(item));
    const tide_list scratch = tide_list_from(made, 4, sizeof(item));
    draw_both(&d, &copied, &listed_shader, scratch, vertices, indices, true);
    same_runs(&d, &copied, 1);
    tide_draw_free(&d);
    tide_draw_free(&copied);
    tide_list_release(&list, TIDE_IN_LOCAL);
    tide_scratch_reset(mark);
    tide_text_use(NULL, NULL);
    tide_heap_free(&heap);
}
