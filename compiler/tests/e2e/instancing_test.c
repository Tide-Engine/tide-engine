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
    tide_frame(&world, NULL, 1.0f, NULL, &local, &draw, &gui);
}

static const tide_draw_run *through(const char *name, const bool textured)
{
    for (uint32_t i = 0; i < draw.run_count; i++) {
        const tide_draw_run *r = &draw.runs[i];
        if (r->shader && strcmp(r->shader->name, name) == 0 && (r->shader->texture_count != 0) == textured) return r;
    }
    return NULL;
}

static float instance_float(const tide_draw_run *r, const uint32_t instance, const uint32_t word)
{
    float v;
    memcpy(&v, draw.instances + r->first + instance * tide_shader_instance_size(r->shader) + word * 4u, sizeof v);
    return v;
}

static int32_t instance_int(const tide_draw_run *r, const uint32_t instance, const uint32_t word)
{
    int32_t v;
    memcpy(&v, draw.instances + r->first + instance * tide_shader_instance_size(r->shader)
               + r->shader->instance_floats * 16u + word * 4u, sizeof v);
    return v;
}

TIDE_TEST(manual_instances_are_bulk_draws_without_entities)
{
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    frame();
    TIDE_CHECK(tide_world_entity_count(&world) == 2); // Main and the batch's Banner
    TIDE_CHECK(tide_local_entity_count(&local) == 0);

    // The built-in mesh stays on its existing renderer path. The empty
    // list adds nothing, and the following compatible bulk call joins it.
    TIDE_REQUIRE(draw.count > 0 && draw.commands[0].kind == TIDE_DRAW_MESH_3D);
    const tide_draw_command *plain = &draw.commands[0];
    TIDE_CHECK(plain->mesh3.count == 258 && plain->mesh3.filter == TIDE_FILTER_POINT && plain->mesh3.texture != 0);
    TIDE_CHECK(draw.matrices[plain->mesh3.first].c3.x == 0.0f);
    TIDE_CHECK(draw.matrices[plain->mesh3.first + 256u].c3.x == 256.0f);
    TIDE_CHECK(draw.matrices[plain->mesh3.first + 257u].c3.x == 500.0f);
    TIDE_CHECK(draw.commands[1].kind == TIDE_DRAW_RECT); // Kept in drawing order

    const tide_draw_run *batch = through("Place and Paint on Corner3", false);
    TIDE_REQUIRE(batch && batch->shader && batch->count == 257);
    const tide_shader *s = batch->shader;
    TIDE_CHECK(s->dims == 3 && s->attribute_count == 3 && s->vertex_size == sizeof(Corner3));
    // transform, offset, tint, finish.shine and the entity's tint; the
    // unread long takes no space. kind, lit and finish.layers are ints.
    TIDE_CHECK(s->instance_floats == 7 && s->instance_ints == 1);
    TIDE_CHECK(s->uniform_floats == 1 && s->uniform_ints == 0);
    float strength;
    memcpy(&strength, draw.uniforms + batch->uniforms, sizeof strength);
    TIDE_CHECK(strength == 0.75f);
    const uint32_t last = 256;
    TIDE_CHECK(instance_float(batch, last, 12) == 256.0f && instance_float(batch, last, 13) == 2.0f);
    TIDE_CHECK(instance_float(batch, last, 16) == 256.0f && instance_float(batch, last, 17) == -256.0f);
    TIDE_CHECK(instance_float(batch, last, 18) == 1.0f && instance_float(batch, last, 19) == 0.25f);
    TIDE_CHECK(instance_float(batch, last, 22) == 0.5f && instance_float(batch, last, 23) == 0.5f);
    TIDE_CHECK(instance_int(batch, last, 0) == 256 && instance_int(batch, last, 1) == 1 && instance_int(batch, last, 2) == 2);

    const tide_draw_run *custom = through("meshes of Corner3", false);
    const tide_draw_run *textured = through("meshes of Corner3", true);
    TIDE_REQUIRE(custom && textured && custom->count == 257 && textured->count == 257);
    TIDE_CHECK(custom->mesh == textured->mesh && custom->mesh == batch->mesh);
    TIDE_CHECK(custom->shader->instance_floats == 4 && custom->shader->instance_ints == 0);
    TIDE_CHECK(textured->sampling == 1u && textured->textures[0] == plain->mesh3.texture);

    const tide_draw_run *identity = through("Identity on Corner3", false);
    TIDE_REQUIRE(identity && identity->count == 2);
    TIDE_CHECK(instance_float(identity, 0, 0) == 1.0f && instance_float(identity, 0, 5) == 1.0f);
    TIDE_CHECK(instance_float(identity, 0, 10) == 1.0f && instance_float(identity, 0, 15) == 1.0f);
    TIDE_CHECK(instance_float(identity, 0, 12) == 0.0f && instance_float(identity, 1, 12) == 0.0f);

    const tide_draw_run *shared = through("SharedPaint on Corner3", false);
    TIDE_REQUIRE(shared && shared->count == 258);
    TIDE_CHECK(shared->shader->instance_floats == 4 && shared->shader->instance_ints == 0);
    TIDE_CHECK(instance_float(shared, 0, 12) == 1000.0f && instance_float(shared, 1, 12) == 0.0f);
    TIDE_CHECK(instance_float(shared, 257, 12) == 256.0f && shared->mesh == batch->mesh);
    memcpy(&strength, draw.uniforms + shared->uniforms, sizeof strength);
    TIDE_CHECK(strength == 0.75f && shared->shader->uniform_floats == 1);

    const tide_draw_run *flat = through("FlatPlace and FlatPaint on Corner2", false);
    const tide_draw_run *flat_texture = through("FlatPlace on Corner2", true);
    TIDE_REQUIRE(flat && flat_texture && flat->count == 2 && flat_texture->count == 2);
    TIDE_CHECK(flat->shader->dims == 2 && flat->shader->instance_floats == 2 && flat->shader->instance_ints == 1);
    TIDE_CHECK(instance_float(flat, 0, 0) == 10.0f && instance_float(flat, 1, 1) == 40.0f);
    TIDE_CHECK(instance_float(flat, 0, 2) == 1.0f && instance_float(flat, 1, 3) == 1.0f);
    TIDE_CHECK(instance_int(flat, 0, 0) == 1 && instance_int(flat, 1, 0) == 2);
    TIDE_CHECK(flat_texture->sampling == 0u && flat_texture->textures[0] == plain->mesh3.texture);
    TIDE_CHECK(draw.run_count == 7); // Empty lists produce no runs

    // A second frame reuses the mesh and texture copies while drawing all
    // the same instances, including those beyond the first list chunk.
    const uint32_t meshes = draw.mesh_count, textures = draw.texture_count;
    frame();
    TIDE_CHECK(draw.mesh_count == meshes && draw.texture_count == textures && draw.run_count == 7);
    batch = through("Place and Paint on Corner3", false);
    TIDE_REQUIRE(batch && batch->count == 257);
    TIDE_CHECK(instance_int(batch, last, 0) == 256 && instance_float(batch, last, 12) == 256.0f);
}
