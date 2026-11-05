#include <string.h>

#include "game.h"
#include "tide_test.h"

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

static void frame(void)
{
    tide_draw_reset(&draw);
    tide_draw_screen_size(&draw, tide_f2(400, 400)); // A frustum to cull by
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
}

// The nth command of `kind`.
static const tide_draw_command *command(const tide_draw_kind kind, int n)
{
    for (uint32_t i = 0; i < draw.count; i++) {
        if (draw.commands[i].kind == kind && n-- == 0) return &draw.commands[i];
    }
    return NULL;
}

// Where the nth command of `kind` is among all of them.
static int place(const tide_draw_kind kind, int n)
{
    for (uint32_t i = 0; i < draw.count; i++) {
        if (draw.commands[i].kind == kind && n-- == 0) return (int)i;
    }
    return -1;
}

static float uniform_float(const tide_draw_compute *r, const uint32_t word)
{
    float v;
    memcpy(&v, draw.uniforms + r->uniforms + word * 4u, sizeof v);
    return v;
}

static int32_t uniform_int(const tide_draw_compute *r, const uint32_t word)
{
    int32_t v;
    memcpy(&v, draw.uniforms + r->uniforms + r->shader->uniform_floats * 16u + word * 4u, sizeof v);
    return v;
}

TIDE_TEST(compute_runs_and_gpu_lists_are_recorded_in_frame_order)
{
    tide_local_init(&local);
    tide_world_init(&world, 1.0f / 20.0f);
    tide_world_tick(&world);
    frame();
    const Meadow *m = &local.Meadow;
    TIDE_REQUIRE(m->blades.id && m->shown.id && m->counts.id && m->blades.id != m->shown.id);
    // Made with its capacity, an element's words (float3, float, then int and bool), and resized
    TIDE_CHECK(tide_gpu_list_capacity(m->blades) == 16 && tide_gpu_list_state_of(m->blades.id)->stride == 6);
    TIDE_CHECK(m->capacity == 32 && tide_gpu_list_capacity(m->shown) == 32 && m->revision > 0);
    TIDE_CHECK(tide_gpu_list_state_of(m->counts.id)->stride == 1);

    // Clear, then Set, Clear and SetCount, then three runs, then the indirect draw: in that order
    const int set = place(TIDE_DRAW_GPU_LIST, 0), first_run = place(TIDE_DRAW_COMPUTE, 0);
    TIDE_CHECK(place(TIDE_DRAW_CLEAR, 0) < set && set < place(TIDE_DRAW_GPU_LIST, 1)
               && place(TIDE_DRAW_GPU_LIST, 2) < first_run && first_run < place(TIDE_DRAW_COMPUTE, 1)
               && place(TIDE_DRAW_COMPUTE, 2) < place(TIDE_DRAW_SHADED, 0));
    TIDE_REQUIRE(draw.gpu_op_count == 3 && draw.compute_count == 3);
    const tide_draw_gpu_op *sent = &draw.gpu_ops[command(TIDE_DRAW_GPU_LIST, 0)->gpu_list.op];
    TIDE_CHECK(sent->kind == TIDE_GPU_LIST_SET && sent->list == m->blades.id && sent->count == 2);
    // The elements' words: floats' bits, then ints, a bool as 0 or 1
    float f[4];
    memcpy(f, draw.gpu_words + sent->words, sizeof f);
    TIDE_CHECK(f[0] == 1.0f && f[1] == 2.0f && f[2] == 3.0f && f[3] == 0.5f);
    TIDE_CHECK((int32_t)draw.gpu_words[sent->words + 4] == 7 && draw.gpu_words[sent->words + 5] == 1);
    memcpy(f, draw.gpu_words + sent->words + 6, sizeof f);
    TIDE_CHECK(f[0] == 4.0f && f[3] == 1.5f && (int32_t)draw.gpu_words[sent->words + 10] == -2 && draw.gpu_words[sent->words + 11] == 0);
    const tide_draw_gpu_op *cleared = &draw.gpu_ops[command(TIDE_DRAW_GPU_LIST, 1)->gpu_list.op];
    TIDE_CHECK(cleared->kind == TIDE_GPU_LIST_CLEAR && cleared->list == m->shown.id);
    const tide_draw_gpu_op *counted = &draw.gpu_ops[command(TIDE_DRAW_GPU_LIST, 2)->gpu_list.op];
    TIDE_CHECK(counted->kind == TIDE_GPU_LIST_COUNT && counted->list == m->counts.id && counted->count == 3);

    // Thin: over the blades' count on the GPU, its lists, and the meadow's scale and the run's values as uniforms
    const tide_draw_compute *cull = &draw.computes[command(TIDE_DRAW_COMPUTE, 0)->compute.run];
    TIDE_REQUIRE(cull->shader && cull->shader->compute);
    const tide_shader *k = cull->shader;
    TIDE_CHECK(strcmp(k->name, "compute Thin") == 0);
#ifdef __wasm__
    TIDE_CHECK(k->wgsl && strstr(k->wgsl, "@compute @workgroup_size(32, 1, 1)") && !k->spirv); // WebGPU's
#else
    TIDE_CHECK(k->wgsl == NULL && k->spirv && k->spirv_words > 0); // Vulkan's
#endif
    TIDE_CHECK(k->group[0] == 32 && k->group[1] == 1 && k->group[2] == 1);
    TIDE_CHECK(cull->over == m->blades.id && k->storage_count == 2 && cull->storage);
    TIDE_CHECK(draw.storage[cull->storage - 1u] == m->blades.id && draw.storage[cull->storage] == m->shown.id);
    TIDE_CHECK(k->storage_writes == 2u && k->add_count == 1 && k->adds[0].list == 1 && k->adds[0].scratch == 2
               && k->adds[0].stride == 6 && tide_shader_compute_storage(k) == 4);
    TIDE_CHECK(uniform_float(cull, 0) == 2.0f && uniform_float(cull, 1) == 2.0f && uniform_int(cull, 0) == 1);

    // Count: over a size, twice, its list written with atomics
    const tide_draw_compute *grid = &draw.computes[command(TIDE_DRAW_COMPUTE, 1)->compute.run];
    const tide_draw_compute *line = &draw.computes[command(TIDE_DRAW_COMPUTE, 2)->compute.run];
    TIDE_CHECK(grid->shader == line->shader && grid->shader->group[0] == 4 && grid->shader->group[1] == 4);
    TIDE_CHECK(grid->over == 0 && grid->size[0] == 5 && grid->size[1] == 6 && grid->size[2] == 1);
    TIDE_CHECK(line->size[0] == 7 && line->size[1] == 1 && line->size[2] == 1);
    TIDE_CHECK(grid->shader->storage_count == 1 && grid->shader->storage_writes == 1u && grid->shader->add_count == 0);
    TIDE_CHECK(draw.storage[grid->storage - 1u] == m->counts.id);

    // The indirect draw: its instances are the shown list's, as many as it has on the GPU
    const tide_draw_run *drawn = &draw.runs[command(TIDE_DRAW_SHADED, 0)->shaded.run];
    TIDE_CHECK(drawn->indirect == m->shown.id && drawn->count == 1 && drawn->shader->instance_storage == 1);
    TIDE_CHECK(drawn->shader->storage_count == 1 && drawn->storage && draw.storage[drawn->storage - 1u] == m->shown.id);
    TIDE_CHECK(drawn->shader->instance_floats == 0 && drawn->shader->instance_ints == 0);
    TIDE_CHECK(strstr(drawn->shader->wgsl ? drawn->shader->wgsl : "", "") != NULL);
    // ...with Draw.Transform's matrix in its uniforms, which the GPU puts each transform through: the identity here,
    // then a Draw.Transform's, and one that mirrors has its triangles' front the other way
    const tide_shader *placed = drawn->shader;
    TIDE_REQUIRE(placed->instance_through && (placed->instance_through - 1u) % 4u == 0
                 && (placed->instance_through - 1u + 16u) * 4u <= tide_shader_uniform_size(placed));
    const tide_draw_run *moved = &draw.runs[command(TIDE_DRAW_SHADED, 1)->shaded.run];
    const tide_draw_run *mirrored = &draw.runs[command(TIDE_DRAW_SHADED, 2)->shaded.run];
    tide_float4x4 through[3];
    const tide_draw_run *indirect[3] = {drawn, moved, mirrored};
    for (int i = 0; i < 3; i++)
        memcpy(&through[i], draw.uniforms + indirect[i]->uniforms + (placed->instance_through - 1u) * 4u, sizeof through[i]);
    const tide_float4x4 identity = tide_identity_f4x4();
    TIDE_CHECK(memcmp(&through[0], &identity, sizeof identity) == 0);
    TIDE_CHECK(through[1].c3.x == 5.0f && through[1].c3.y == 6.0f && through[1].c3.z == 7.0f && through[1].c0.x == 1.0f);
    TIDE_CHECK(through[2].c0.x == -1.0f && through[2].c1.y == 1.0f && through[2].c3.x == 0.0f);
    TIDE_CHECK(moved->indirect == m->shown.id && mirrored->indirect == m->shown.id);
    TIDE_CHECK(!(command(TIDE_DRAW_SHADED, 0)->cull & TIDE_DRAW_MIRRORED) && !(command(TIDE_DRAW_SHADED, 1)->cull & TIDE_DRAW_MIRRORED)
               && (command(TIDE_DRAW_SHADED, 2)->cull & TIDE_DRAW_MIRRORED));

    // A mesh of a GPU list's vertices and indices: both its lists, read on the GPU, and a mesh of one unused triangle
    const tide_draw_run *made = &draw.runs[command(TIDE_DRAW_SHADED, 3)->shaded.run];
    const tide_shader *pulled = made->shader;
    TIDE_CHECK(pulled->vertex_storage == 1 && pulled->index_storage == 2 && pulled->attribute_count == 0
               && pulled->storage_count == 2 && pulled->instance_storage == 0 && pulled->bounds_position == 0);
    TIDE_CHECK(made->storage && draw.storage[made->storage - 1u] == m->points.id && draw.storage[made->storage] == m->order.id);
    TIDE_CHECK(made->indirect == 0 && made->count == 1 && pulled->instance_floats == 4); // Its transform, from the CPU
    TIDE_CHECK(draw.meshes[made->mesh - 1u].index_count == 3 && draw.meshes[made->mesh - 1u].vertex_count == 1
               && draw.meshes[made->mesh - 1u].vertex_size == sizeof(Corner));
    float x;
    memcpy(&x, draw.instances + made->first + 12u * sizeof(float), sizeof x);
    TIDE_CHECK(x == 1.0f);
    // ...by the CPU's indices: whole triangles of them as they are, the vertices the GPU's to count
    const tide_draw_run *picked = &draw.runs[command(TIDE_DRAW_SHADED, 4)->shaded.run];
    TIDE_CHECK(picked->shader->vertex_storage == 1 && picked->shader->index_storage == 0 && picked->shader->storage_count == 1);
    TIDE_CHECK(picked->storage && draw.storage[picked->storage - 1u] == m->points.id);
    const tide_draw_mesh_data *by_cpu = &draw.meshes[picked->mesh - 1u];
    TIDE_CHECK(by_cpu->index_count == 6 && by_cpu->vertex_count == 1 && by_cpu->indices[3] == 2 && by_cpu->indices[5] == 3);
    // ...instanced over a CPU list: its instances as any list's
    const tide_draw_run *many = &draw.runs[command(TIDE_DRAW_SHADED, 5)->shaded.run];
    TIDE_CHECK(many->shader->vertex_storage == 1 && many->count == 2 && many->indirect == 0);
    // Instances with transforms of their own: a run that keeps those that don't mirror the mesh, then one wound the
    // other way that keeps those that do, which the GPU tells apart (instance_sides); one run where nothing is culled
    const tide_draw_run *kept = &draw.runs[command(TIDE_DRAW_SHADED, 6)->shaded.run];
    const tide_draw_run *other = &draw.runs[command(TIDE_DRAW_SHADED, 7)->shaded.run];
    const tide_draw_run *all = &draw.runs[command(TIDE_DRAW_SHADED, 8)->shaded.run];
    TIDE_REQUIRE(kept->shader == other->shader && kept->shader == all->shader && kept->shader->instance_sides
                 && kept->shader->instance_sides == kept->shader->instance_through + 16u);
    TIDE_CHECK(kept->indirect == m->turned.id && other->indirect == m->turned.id && all->indirect == m->turned.id);
    TIDE_CHECK(placed->instance_sides == 0); // Blades have no transform
    float sides[3];
    const tide_draw_run *by_side[3] = {kept, other, all};
    for (int i = 0; i < 3; i++)
        memcpy(&sides[i], draw.uniforms + by_side[i]->uniforms + (kept->shader->instance_sides - 1u) * 4u, sizeof sides[i]);
    TIDE_CHECK(sides[0] == 1.0f && sides[1] == 2.0f && sides[2] == 0.0f);
    TIDE_CHECK(!(command(TIDE_DRAW_SHADED, 6)->cull & TIDE_DRAW_MIRRORED) && (command(TIDE_DRAW_SHADED, 7)->cull & TIDE_DRAW_MIRRORED)
               && (command(TIDE_DRAW_SHADED, 6)->cull & 3u) == TIDE_CULL_BACK && (command(TIDE_DRAW_SHADED, 8)->cull & 3u) == TIDE_CULL_NONE);
    // A GPU list's vertices over CPU instances: frustum culled by Draw.Bounds, as the CPU knows no bounds of its own
    // (the far one of the first draw's two), and not without them (both of the second's), in one run
    const tide_draw_run *bounded = &draw.runs[command(TIDE_DRAW_SHADED, 9)->shaded.run];
    TIDE_CHECK(bounded->shader->vertex_storage == 1 && bounded->count == 3 && command(TIDE_DRAW_SHADED, 10) == NULL);

    // A frame later the same, with the lists kept; a disposed list records nothing more
    frame();
    TIDE_CHECK(draw.compute_count == 3 && draw.gpu_op_count == 3 && m->revision == tide_gpu_list_content_revision(m->shown));
    frame(); // Frame 3 disposes of the counts after recording
    frame();
    TIDE_CHECK(tide_gpu_list_capacity(m->counts) == 0 && draw.gpu_op_count == 2);
    TIDE_CHECK(draw.compute_count == 3); // A run's lists that are gone are empty to it: still recorded
    tide_draw_free(&draw);
    tide_gpu_lists_free();
}
