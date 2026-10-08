#include <string.h>

#include "game.h"
#include "tide_test.h"

// Random's rolls and Random.seed in frags and verts. The GPU rolls the
// numbers; what a draw sends for them is the frame's: a number tide_frame
// rolls with this machine's generator as it starts, which the generators on
// the GPU are made of, and the seed of the match it draws. Both are words of
// the draw's ints, the same for every draw of a frame, so draws that roll
// still join into runs.

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

#define DT (1.0f / 60.0f)

static void frame(const tide_world *w)
{
    tide_draw_reset(&draw);
    tide_frame(w, NULL, 1.0f, NULL, &local, &draw, &gui);
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

static int32_t uniform_int(const tide_draw_run *r, const uint32_t word)
{
    int32_t v;
    memcpy(&v, draw.uniforms + r->uniforms + r->shader->uniform_floats * 16u + word * 4u, sizeof v);
    return v;
}

// What the frame that starts with the generator at `state` rolls for the GPU: its next 32 bits.
static int32_t rolled_from(const int64_t state)
{
    tide_random dice = {state};
    return (int32_t)tide_random_next(&dice);
}

TIDE_TEST(gpu_rolls_send_the_frames_number_and_the_seed)
{
    tide_world_start_seeded(&world, DT, NULL, 4242u);
    tide_world_tick(&world);
    tide_local_init(&local);
    local.tide_dice.state = 1234;
    frame(&world);
    const int32_t first = rolled_from(1234);

    // Three draws through a frag that rolls are one run of three: the number they send is the frame's
    TIDE_REQUIRE(runs_through("Grain on Vertex") == 1);
    const tide_draw_run *grain = through("Grain on Vertex", 0);
    TIDE_CHECK(grain->count == 3);
    TIDE_CHECK(grain->shader->uniform_floats == 0 && grain->shader->uniform_ints == 1);
    TIDE_CHECK(uniform_int(grain, 0) == first);

    // A vert that rolls, with no frag: the same number, and none of the seed
    const tide_draw_run *shake = through("Shake on Vertex", 0);
    TIDE_REQUIRE(shake != NULL);
    TIDE_CHECK(shake->shader->uniform_ints == 1 && uniform_int(shake, 0) == first && uniform_int(shake, 1) == 0);

    // A frag that rolls and reads the seed through a function, behind a vert that rolls: both words
    const tide_draw_run *mixed = through("Shake and Mixed on Vertex", 0);
    TIDE_REQUIRE(mixed != NULL);
    TIDE_CHECK(mixed->shader->uniform_ints == 1 && uniform_int(mixed, 0) == first && uniform_int(mixed, 1) == 4242);

    // The seed alone is a word of its own, with no number of the frame's
    const tide_draw_run *seeded = through("Seeded on Vertex", 0);
    TIDE_REQUIRE(seeded != NULL);
    TIDE_CHECK(seeded->shader->uniform_ints == 1 && uniform_int(seeded, 0) == 4242);

    // ...and a frag that does neither sends nothing
    const tide_draw_run *plain = through("Plain on Vertex", 0);
    TIDE_REQUIRE(plain != NULL);
    TIDE_CHECK(plain->shader->uniform_ints == 0 && plain->shader->uniform_floats == 0);

    // Each stage that rolls has the generator, and only those: a frag behind a vert that rolls has none of its own
    TIDE_CHECK(strstr(grain->shader->glsl_fragment, "uint tide_dice;") && !strstr(grain->shader->glsl_vertex, "tide_dice"));
    TIDE_CHECK(strstr(grain->shader->glsl_vertex, "tide_nth = gl_InstanceID;"));
    TIDE_CHECK(strstr(shake->shader->glsl_vertex, "uint tide_dice;") && !strstr(shake->shader->glsl_fragment, "tide_dice"));
    TIDE_CHECK(strstr(mixed->shader->glsl_vertex, "uint tide_dice;") && strstr(mixed->shader->glsl_fragment, "uint tide_dice;"));
    TIDE_CHECK(!strstr(seeded->shader->glsl_fragment, "tide_dice") && !strstr(plain->shader->glsl_fragment, "tide_dice"));
    // Which frag or vert rolls is part of what its generator is made of, so two that draw one pixel roll apart
    TIDE_CHECK(grain->shader->id != mixed->shader->id);

    // The view read the seed its draws sent, and rolled with the generator the frame's number came from
    TIDE_CHECK(local.Art.seen == 4242 && local.Art.own >= 0 && local.Art.own < 1000000);
    tide_random dice = {1234};
    (void)tide_random_next(&dice);
    TIDE_CHECK(local.Art.own == tide_random_next_int_below(&dice, 1000000));

    // The next frame rolls another number, which all of its draws send
    const int64_t carried = local.tide_dice.state;
    frame(&world);
    const int32_t second = rolled_from(carried);
    grain = through("Grain on Vertex", 0);
    mixed = through("Shake and Mixed on Vertex", 0);
    TIDE_REQUIRE(grain && mixed && runs_through("Grain on Vertex") == 1);
    TIDE_CHECK(second != first && uniform_int(grain, 0) == second && uniform_int(mixed, 0) == second);
    TIDE_CHECK(uniform_int(mixed, 1) == 4242);

    // A frame's length goes into the generator (tide_local_frame_time), so its numbers follow it too
    local.tide_dice.state = 1234;
    tide_local_frame_time(&local, DT);
    TIDE_CHECK(local.tide_dice.state != 1234);
    const int64_t stirred = local.tide_dice.state;
    frame(&world);
    TIDE_CHECK(uniform_int(through("Grain on Vertex", 0), 0) == rolled_from(stirred) && rolled_from(stirred) != first);

    // With no match the seed is 0, as a view reads it, and rolls go on
    local.tide_dice.state = 99;
    frame(NULL);
    seeded = through("Seeded on Vertex", 0);
    grain = through("Grain on Vertex", 0);
    TIDE_REQUIRE(seeded && grain);
    TIDE_CHECK(uniform_int(seeded, 0) == 0 && uniform_int(grain, 0) == rolled_from(99) && local.Art.seen == 0);

    // Another match, another seed
    tide_world_start_seeded(&world, DT, NULL, 7u);
    frame(&world);
    TIDE_CHECK(uniform_int(through("Seeded on Vertex", 0), 0) == 7);

    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
}
