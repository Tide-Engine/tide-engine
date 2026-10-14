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
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
}

// The `at`th run through the shader of this name, in the order they were drawn.
static const tide_draw_run *through(const char *name, uint32_t at)
{
    for (uint32_t i = 0; i < draw.run_count; i++) {
        const tide_draw_run *r = &draw.runs[i];
        if (r->shader && strcmp(r->shader->name, name) == 0 && at-- == 0) return r;
    }
    return NULL;
}

static uint32_t runs_through(const char *name)
{
    uint32_t count = 0;
    while (through(name, count)) count++;
    return count;
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

static float instance_float(const tide_draw_run *r, const uint32_t instance, const uint32_t word)
{
    float v;
    memcpy(&v, draw.instances + r->first + instance * tide_shader_instance_size(r->shader) + word * 4u, sizeof v);
    return v;
}

TIDE_TEST(a_draws_values_go_once_with_the_draw)
{
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    frame();

    // Two blits through one frag, each with its values: a run each. Its
    // direction and the singleton's strength are floats, its taps an int, and
    // the matrix and the long the frag never reads take no room.
    TIDE_REQUIRE(runs_through("Blur on Vertex") == 2);
    const tide_draw_run *across = through("Blur on Vertex", 0);
    const tide_draw_run *down = through("Blur on Vertex", 1);
    TIDE_CHECK(across->shader == down->shader && across->uniforms != down->uniforms);
    TIDE_CHECK(across->shader->uniform_floats == 1 && across->shader->uniform_ints == 1);
    TIDE_CHECK(across->shader->instance_floats == 0 && across->shader->instance_ints == 0);
    TIDE_CHECK(uniform_float(across, 0) == 1.0f && uniform_float(across, 1) == 0.0f && uniform_int(across, 0) == 3);
    TIDE_CHECK(uniform_float(down, 0) == 0.0f && uniform_float(down, 1) == 1.0f && uniform_int(down, 0) == 5);
    TIDE_CHECK(uniform_float(across, 2) == 0.75f && uniform_float(down, 2) == 0.75f);

    // Meshes: the same values twice in a row are one run of two instances;
    // other values are another; and the last two, which differ in a field
    // nothing reads, are one
    TIDE_REQUIRE(runs_through("Raise and Paint on Corner3") == 3);
    const tide_draw_run *red = through("Raise and Paint on Corner3", 0);
    const tide_draw_run *lifted = through("Raise and Paint on Corner3", 1);
    const tide_draw_run *green = through("Raise and Paint on Corner3", 2);
    const tide_shader *s = red->shader;
    TIDE_CHECK(s->uniform_floats == 3 && s->uniform_ints == 1); // lift, tint, shine and the light; flip and layers
    TIDE_CHECK(s->instance_floats == 4 && s->instance_ints == 0); // The transform alone: nothing of the values
    TIDE_CHECK(red->count == 2 && lifted->count == 1 && green->count == 2);
    TIDE_CHECK(red->mesh == lifted->mesh && red->mesh == green->mesh);
    TIDE_CHECK(instance_float(red, 0, 12) == 0.0f && instance_float(red, 1, 12) == 5.0f);
    // The vert's lift, then the frag's tint, shine and the singleton
    TIDE_CHECK(uniform_float(red, 0) == 0.0f && uniform_float(red, 1) == 2.0f && uniform_float(red, 2) == 0.0f);
    TIDE_CHECK(uniform_float(red, 3) == 1.0f && uniform_float(red, 4) == 0.0f && uniform_float(red, 6) == 1.0f);
    TIDE_CHECK(uniform_float(red, 7) == 0.5f && uniform_float(red, 8) == 0.75f);
    TIDE_CHECK(uniform_int(red, 0) == 1 && uniform_int(red, 1) == 3);
    // What the literal left out is the struct's defaults
    TIDE_CHECK(uniform_float(lifted, 1) == 4.0f && uniform_float(lifted, 3) == 1.0f && uniform_float(lifted, 4) == 1.0f);
    TIDE_CHECK(uniform_float(lifted, 7) == 1.0f && uniform_int(lifted, 0) == 0 && uniform_int(lifted, 1) == 1);
    // The call that made the values ran once
    TIDE_CHECK(local.Art.rolls == 1);
    TIDE_CHECK(uniform_float(green, 0) == 1.0f && uniform_float(green, 3) == 0.0f && uniform_float(green, 4) == 1.0f);

    // Instances beside values: the list's struct for each instance, the
    // draw's once. The same values in a row join, as any instanced draws do
    TIDE_REQUIRE(runs_through("Glow on Corner3") == 2);
    const tide_draw_run *quarter = through("Glow on Corner3", 0);
    const tide_draw_run *half = through("Glow on Corner3", 1);
    TIDE_CHECK(quarter->shader->instance_floats == 5 && quarter->shader->instance_ints == 0);
    TIDE_CHECK(quarter->shader->uniform_floats == 1 && quarter->shader->uniform_ints == 1);
    TIDE_CHECK(quarter->count == 4 && half->count == 2);
    TIDE_CHECK(uniform_float(quarter, 0) == 0.25f && uniform_int(quarter, 0) == 4);
    TIDE_CHECK(uniform_float(half, 0) == 0.5f && uniform_int(half, 0) == 2);
    TIDE_CHECK(instance_float(quarter, 0, 16) == 1.0f && instance_float(quarter, 1, 18) == 1.0f); // Red, then blue
    TIDE_CHECK(instance_float(quarter, 1, 12) == 3.0f && instance_float(half, 1, 12) == 3.0f);

    // A vert alone, in 2D
    const tide_draw_run *flat = through("Slide on Corner2", 0);
    TIDE_REQUIRE(flat && flat->count == 1);
    TIDE_CHECK(flat->shader->dims == 2 && flat->shader->uniform_floats == 1 && flat->shader->uniform_ints == 0);
    TIDE_CHECK(flat->shader->instance_floats == 0 && uniform_float(flat, 0) == 7.0f);

    TIDE_CHECK(draw.run_count == 8);

    // Another frame draws the same runs
    frame();
    TIDE_CHECK(draw.run_count == 8 && local.Art.rolls == 2);
    green = through("Raise and Paint on Corner3", 2);
    TIDE_REQUIRE(green && green->count == 2);
    TIDE_CHECK(uniform_float(green, 0) == 2.0f);
}
