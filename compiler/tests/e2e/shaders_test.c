#include <string.h>

#include "game.h"
#include "tide_test.h"

static tide_world before;
static tide_world now;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

static void frame(const tide_world *previous, const float alpha)
{
    tide_draw_reset(&draw);
    tide_frame(&now, previous, alpha, NULL, &local, &draw, &gui);
}

// The nth command of `kind`'s run.
static const tide_draw_run *run(int n)
{
    for (uint32_t i = 0; i < draw.count; i++) {
        if (draw.commands[i].kind == TIDE_DRAW_SHADED && n-- == 0) return &draw.runs[draw.commands[i].shaded.run];
    }
    return NULL;
}

// Word `word` of instance `instance` of a run: its floats come first, then its ints.
static float instance_float(const tide_draw_run *r, const uint32_t instance, const uint32_t word)
{
    float v;
    memcpy(&v, draw.instances + r->first + instance * tide_shader_instance_size(r->shader) + word * 4u, sizeof v);
    return v;
}

static int32_t instance_int(const tide_draw_run *r, const uint32_t instance, const uint32_t word)
{
    int32_t v;
    memcpy(&v, draw.instances + r->first + instance * tide_shader_instance_size(r->shader) + r->shader->instance_floats * 16u + word * 4u,
           sizeof v);
    return v;
}

static float uniform_float(const tide_draw_run *r, const uint32_t word)
{
    float v;
    memcpy(&v, draw.uniforms + r->uniforms + word * 4u, sizeof v);
    return v;
}

static int32_t uniform_int(const tide_draw_run *r, const uint32_t word)
{
    int32_t v;
    memcpy(&v, draw.uniforms + r->uniforms + r->shader->uniform_floats * 16u + word * 4u, sizeof v);
    return v;
}

static void start(void)
{
    tide_local_init(&local);
    tide_world_init(&now, 1.0f / 20.0f);
    tide_world_tick(&now); // Tick 0: the boxes are there, and glow 1
    tide_world_copy(&before, &now);
    tide_world_tick(&now); // Tick 1: they glow 2
}

TIDE_TEST(shaders_a_draw_sends_what_its_frag_and_vert_read)
{
    start();
    frame(&before, 0.5f);

    // Both boxes through Wave and Shade: one run, an instance each
    const tide_draw_run *boxes = run(0);
    TIDE_REQUIRE(boxes && boxes->shader && boxes->count == 2);
    const tide_shader *s = boxes->shader;
    TIDE_CHECK(s->dims == 3 && s->vertex_size == sizeof(BoxVertex) && s->attribute_count == 3);
    TIDE_CHECK(s->attributes[0].format == TIDE_SHADER_FLOAT3 && s->attributes[0].offset == offsetof(BoxVertex, position));
    TIDE_CHECK(s->attributes[1].format == TIDE_SHADER_FLOAT3 && s->attributes[1].offset == offsetof(BoxVertex, normal));
    TIDE_CHECK(s->attributes[2].format == TIDE_SHADER_FLOAT2 && s->attributes[2].offset == offsetof(BoxVertex, uv));
    TIDE_CHECK(strstr(s->glsl_vertex, "gl_Position") && strstr(s->glsl_fragment, "tide_pixel"));
    TIDE_CHECK(strcmp(s->name, "Wave and Shade on BoxVertex") == 0 && s->id != 0);

    // An instance: its transform, then the fields of Box the frag reads, in
    // order (tint, glow, finish.shine as floats; kind, lit, finish.layers and
    // wear as ints), and not the one it doesn't
    TIDE_CHECK(s->instance_floats == 6 && s->instance_ints == 1);
    int red = -1;
    for (uint32_t i = 0; i < 2; i++) {
        TIDE_CHECK(instance_float(boxes, i, 0) == 1.0f && instance_float(boxes, i, 14) == 5.0f); // Translate(0, 0, 5)
        if (instance_float(boxes, i, 16) == 1.0f) red = (int)i;
    }
    TIDE_REQUIRE(red >= 0);
    const uint32_t r = (uint32_t)red, g = 1u - r;
    TIDE_CHECK(instance_float(boxes, r, 17) == 0.0f && instance_float(boxes, r, 19) == 1.0f); // Color.red
    TIDE_CHECK(instance_float(boxes, g, 16) == 0.0f && instance_float(boxes, g, 17) == 1.0f); // Color.green
    // Its glow went from 1 to 2 over the tick: the frag gets it as the view sees it, halfway
    TIDE_CHECK(instance_float(boxes, r, 20) == 1.5f && instance_float(boxes, g, 20) == 1.5f);
    TIDE_CHECK(instance_float(boxes, r, 21) == 0.5f && instance_float(boxes, g, 21) == 0.75f); // finish.shine
    // A local field of the component: what a view wrote for one entity, and its default for the other
    TIDE_CHECK(instance_float(boxes, r, 22) == 3.0f && instance_float(boxes, g, 22) == 0.5f);
    TIDE_CHECK(instance_int(boxes, r, 0) == 2 && instance_int(boxes, g, 0) == 0);              // kind
    TIDE_CHECK(instance_int(boxes, r, 1) == 1 && instance_int(boxes, g, 1) == 0);              // lit, a bool
    TIDE_CHECK(instance_int(boxes, r, 2) == 3 && instance_int(boxes, g, 2) == 5);              // finish.layers
    TIDE_CHECK(instance_int(boxes, r, 3) == 1 && instance_int(boxes, g, 3) == 0);              // wear, an enum

    // The draw: Time, which the vert reads, then Sun's direction and strength,
    // its local field, and Models' fade, which the frag does
    TIDE_CHECK(s->uniform_floats == 2 && s->uniform_ints == 1);
    TIDE_CHECK(uniform_float(boxes, 0) == now.Time.dt && uniform_int(boxes, 0) == now.Time.tick && now.Time.tick == 2);
    TIDE_CHECK(uniform_float(boxes, 1) == 0.0f && uniform_float(boxes, 2) == -1.0f && uniform_float(boxes, 3) == 0.0f);
    TIDE_CHECK(uniform_float(boxes, 4) == 2.0f && uniform_float(boxes, 5) == 0.125f && uniform_float(boxes, 6) == 0.25f);

    // The grids it samples are textures: wood blended, moss the nearest cell
    TIDE_CHECK(s->texture_count == 2 && boxes->textures[0] != 0 && boxes->textures[1] != 0);
    TIDE_CHECK(boxes->textures[0] != boxes->textures[1] && boxes->blended == 1u);
    const tide_draw_texture *wood = &draw.textures[boxes->textures[0] - 1u];
    TIDE_CHECK(wood->width == 2 && wood->height == 2 && wood->pixels[0] == 255 && wood->pixels[1] == 0);

    // The mesh is its list's bytes
    const tide_draw_mesh_data *crate = &draw.meshes[boxes->mesh - 1u];
    TIDE_CHECK(crate->space == TIDE_MESH_BYTES && crate->vertex_size == sizeof(BoxVertex) && crate->vertex_count == 3);
    TIDE_CHECK(((const BoxVertex *)crate->bytes)[1].position.x == 1.0f && ((const BoxVertex *)crate->bytes)[2].uv.y == 1.0f);
}

TIDE_TEST(shaders_a_frag_or_a_vert_alone_and_neither)
{
    start();
    frame(NULL, 1.0f);

    // A frag that reads nothing: only the transforms go, which the view made from its own Box
    const tide_draw_run *blank = run(1);
    TIDE_REQUIRE(blank && blank->shader);
    TIDE_CHECK(strcmp(blank->shader->name, "Blank on BoxVertex") == 0 && blank->count == 1);
    TIDE_CHECK(blank->shader->instance_floats == 4 && blank->shader->instance_ints == 0 && blank->shader->uniform_floats == 0);
    TIDE_CHECK(blank->shader->texture_count == 0 && blank->mesh == run(0)->mesh);
    const float x = instance_float(blank, 0, 12);
    TIDE_CHECK((x == 1.0f || x == -1.0f) && instance_float(blank, 0, 14) == 5.0f);

    // A vert alone, with a texture the draw gives, read as it says
    const tide_draw_run *waved = run(2);
    TIDE_REQUIRE(waved && waved->shader);
    TIDE_CHECK(strcmp(waved->shader->name, "Wave on BoxVertex") == 0 && waved->shader->texture_count == 1);
    TIDE_CHECK(waved->textures[0] == run(0)->textures[0] && waved->blended == 0u);
    TIDE_CHECK(waved->shader->uniform_floats == 1 && waved->shader->uniform_ints == 1); // Time

    // Neither: a mesh of 12 bytes a vertex, drawn as it is
    const tide_draw_run *dots = run(5);
    TIDE_REQUIRE(dots && dots->shader);
    TIDE_CHECK(dots->shader->dims == 3 && dots->shader->vertex_size == 12 && dots->shader->attribute_count == 1);
    TIDE_CHECK(dots->shader->instance_floats == 4 && dots->shader->texture_count == 0);
    TIDE_CHECK(draw.meshes[dots->mesh - 1u].vertex_size == 12 && draw.meshes[dots->mesh - 1u].index_count == 3);

    // 2D meshes: no transform, so no instance; in order with the other 2D calls
    const tide_draw_run *flat = run(6);
    TIDE_REQUIRE(flat && flat->shader);
    TIDE_CHECK(flat->shader->dims == 2 && flat->shader->vertex_size == sizeof(Corner) && flat->shader->instance_floats == 0);
    TIDE_CHECK(flat->shader->attributes[2].format == TIDE_SHADER_FLOAT4 && flat->shader->attributes[2].offset == offsetof(Corner, color));
    uint32_t at = 0;
    while (at < draw.count && draw.commands[at].kind != TIDE_DRAW_RECT) at++;
    TIDE_REQUIRE(at > 0 && at + 1 < draw.count);
    TIDE_CHECK(draw.commands[at - 1].kind == TIDE_DRAW_SHADED && &draw.runs[draw.commands[at - 1].shaded.run] == flat);
    TIDE_CHECK(draw.commands[at + 1].kind == TIDE_DRAW_SHADED);

    const tide_draw_run *textured = run(7);
    TIDE_REQUIRE(textured && textured->shader);
    TIDE_CHECK(textured->shader != flat->shader && textured->shader->texture_count == 1 && textured->blended == 1u);
    TIDE_CHECK(textured->mesh == flat->mesh);

    const tide_draw_run *tinted = run(8);
    TIDE_REQUIRE(tinted && tinted->shader);
    TIDE_CHECK(strcmp(tinted->shader->name, "Tint on Corner") == 0 && tinted->shader->uniform_floats == 1);
    TIDE_CHECK(uniform_float(tinted, 0) == 0.25f);

    // A list made along the way is a mesh too, of its own
    const tide_draw_run *made = run(9);
    TIDE_REQUIRE(made && made->shader == flat->shader && made->mesh != flat->mesh);
    TIDE_CHECK(((const Corner *)draw.meshes[made->mesh - 1u].bytes)[1].position.x == 2.0f);

    // A vert that writes a struct for its frag: the draw sends what the two read of the game's, and nothing
    // for the struct, whose fields the frag reads go from the mesh's corners to its pixels
    const tide_draw_run *handed = run(10);
    TIDE_REQUIRE(handed && handed->shader);
    const tide_shader *h = handed->shader;
    TIDE_CHECK(strcmp(h->name, "Fan and Take on Corner") == 0 && handed->mesh == flat->mesh);
    TIDE_CHECK(h->instance_floats == 0 && h->instance_ints == 0 && h->uniform_floats == 1 && h->uniform_ints == 0);
    TIDE_CHECK(uniform_float(handed, 0) == 0.25f); // Models.fade, which the vert reads
    TIDE_CHECK(strstr(h->glsl_vertex, "out float tide_h0_0;") && strstr(h->glsl_fragment, "in float tide_h0_0;"));
    TIDE_CHECK(strstr(h->glsl_vertex, "out vec4 tide_h0_2;") && strstr(h->glsl_fragment, "in vec4 tide_h0_2;"));
    TIDE_CHECK(!strstr(h->glsl_vertex, "tide_h0_1") && !strstr(h->glsl_fragment, "tide_h0_1")); // No frag reads it
#ifdef __wasm__ // A shader has its WGSL on the web
    TIDE_CHECK(strstr(h->wgsl, "tide_h0_0: f32") && strstr(h->wgsl, "tide_h0_2: vec4f") && !strstr(h->wgsl, "tide_h0_1"));
#endif
    TIDE_CHECK(run(11) == NULL);

    // The next frame draws the same meshes and textures: nothing is copied again
    const uint32_t meshes = draw.mesh_count, textures = draw.texture_count;
    frame(NULL, 1.0f);
    TIDE_CHECK(draw.mesh_count == meshes && draw.texture_count == textures && run(0)->count == 2);
}
