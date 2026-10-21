#include <string.h>

#include "game.h"
#include "tide_test.h"
#include "draw_test.h"

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

// A command that draws, and the destination the commands before it left it to draw into.
typedef struct drawn {
    const tide_draw_command *c;
    uint32_t target; // 0 for the window, or 1 + its place in the list's targets
} drawn;

static drawn draws[64];
static int draw_count;

static void frame(const bool leave)
{
    local.Look.leave = leave;
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    tide_test_flatten(&draw); // Its plain shapes a command each, as the checks read them
    draw_count = 0;
    uint32_t target = 0;
    for (uint32_t i = 0; i < draw.count; i++) {
        const tide_draw_command *c = &draw.commands[i];
        switch (c->kind) {
        case TIDE_DRAW_TARGET: target = c->target; break;
        case TIDE_DRAW_CLIP:
        case TIDE_DRAW_NO_CLIP:
        case TIDE_DRAW_SCREEN:
        case TIDE_DRAW_CAMERA:
        case TIDE_DRAW_CAMERA_3D: break;
        default:
            if (draw_count < 64) draws[draw_count++] = (drawn){c, target};
            break;
        }
    }
}

// A frame from the start: each test's own, as they run in any order.
static void whole_frame(const bool leave)
{
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    local.Look.target = (tide_render_target){.id = 11, .size = {.width = 16, .height = 12}};
    frame(leave);
}

enum { MIRRORED = TIDE_DRAW_MIRRORED, DRAWS = 31 };

// Whether what was drawn `i`th is a run of `count` instances of a 3D mesh with `cull` (a tide_cull, with MIRRORED
// where its instances are), the first `z` away.
static bool mesh_3d(const int i, const uint32_t cull, const uint32_t count, const float z)
{
    const tide_draw_command *c = draws[i].c;
    return c->kind == TIDE_DRAW_MESH_3D && c->cull == cull && c->mesh3.count == count
        && draw.matrices[c->mesh3.first].c3.z == z;
}

// ...or of a mesh through a shader of the game's.
static bool shaded(const int i, const uint32_t cull, const uint32_t count)
{
    const tide_draw_command *c = draws[i].c;
    return c->kind == TIDE_DRAW_SHADED && c->cull == cull && draw.runs[c->shaded.run].count == count;
}

TIDE_TEST(draw_cull_wraps_blocks)
{
    whole_frame(false);
    TIDE_REQUIRE(draw_count == DRAWS);
    TIDE_CHECK(draw.saved_count == 0); // Every block put back what it found

    // As a frame starts, around a block, one inside another, and what each found after it
    TIDE_CHECK(mesh_3d(0, TIDE_CULL_BACK, 1, 9.0f));
    TIDE_CHECK(mesh_3d(1, TIDE_CULL_FRONT, 1, 8.0f));
    TIDE_CHECK(mesh_3d(2, TIDE_CULL_NONE, 1, 7.0f));
    TIDE_CHECK(mesh_3d(3, TIDE_CULL_FRONT, 1, 6.0f));
    TIDE_CHECK(mesh_3d(4, TIDE_CULL_BACK, 1, 5.0f));
    // Listed with commas, beside a blend and a depth
    TIDE_CHECK(mesh_3d(5, TIDE_CULL_NONE, 1, 4.0f) && draws[5].c->blend == TIDE_BLEND_ADD);
    TIDE_CHECK(draws[5].c->depth == TIDE_DEPTH_READ);
    TIDE_CHECK(mesh_3d(6, TIDE_CULL_BACK, 1, 3.0f) && draws[6].c->blend == TIDE_BLEND_ALPHA);
    TIDE_CHECK(draws[6].c->depth == TIDE_DEPTH_READ_WRITE);

    // Around each of three entities' meshes: one run, which a renderer draws at once
    TIDE_CHECK(mesh_3d(7, TIDE_CULL_FRONT, 3, 5.0f));
}

// A triangle's front is its front through a transform that mirrors it: the
// list says which instances are mirrored, apart from those that aren't.
TIDE_TEST(draw_cull_tells_mirrored_instances)
{
    whole_frame(false);
    TIDE_REQUIRE(draw_count == DRAWS);
    TIDE_CHECK(mesh_3d(8, TIDE_CULL_BACK, 1, 9.0f));
    // By its own transform, then by the one it's drawn inside: one run, both mirrored
    TIDE_CHECK(mesh_3d(9, TIDE_CULL_BACK | MIRRORED, 2, 8.0f));
    TIDE_CHECK(draw.matrices[draws[9].c->mesh3.first + 1u].c3.z == 7.0f && draw.matrices[draws[9].c->mesh3.first + 1u].c0.x == -1.0f);
    // Mirrored twice, it's as it was
    TIDE_CHECK(mesh_3d(10, TIDE_CULL_BACK, 1, 6.0f) && draw.matrices[draws[10].c->mesh3.first].c0.x == 1.0f);
    // With both sides drawn they're one run, and nothing is said
    TIDE_CHECK(mesh_3d(11, TIDE_CULL_NONE, 2, 5.0f));

    // A list's instances through a frag: the one in the middle is a run of its own
    TIDE_CHECK(shaded(15, TIDE_CULL_BACK, 1) && shaded(16, TIDE_CULL_BACK | MIRRORED, 1) && shaded(17, TIDE_CULL_BACK, 1));
    const tide_draw_run *middle = &draw.runs[draws[16].c->shaded.run];
    tide_float4x4 own;
    memcpy(&own, draw.instances + middle->first, sizeof own);
    TIDE_CHECK(own.c0.x == -1.0f && own.c3.z == 1.0f);
    TIDE_CHECK(shaded(18, TIDE_CULL_NONE, 3));
}

TIDE_TEST(draw_cull_reaches_shaders)
{
    whole_frame(false);
    TIDE_REQUIRE(draw_count == DRAWS);
    // Blocks that ran to their end, in the functions a view calls
    TIDE_CHECK(mesh_3d(12, TIDE_CULL_NONE, 1, 9.0f) && draws[12].c->blend == TIDE_BLEND_ADD);
    TIDE_CHECK(mesh_3d(13, TIDE_CULL_BACK, 1, 8.0f) && draw.matrices[draws[13].c->mesh3.first].c3.x == 2.0f);
    // A mesh through a frag: its command's, here from a value the game keeps
    TIDE_CHECK(shaded(14, TIDE_CULL_FRONT, 1) && draw.runs[draws[14].c->shaded.run].shader->dims == 3);

    // Nothing else has a back: a shape, a 2D mesh through a frag and a clear say nothing, whatever is set
    TIDE_CHECK(draws[19].c->kind == TIDE_DRAW_RECT && draws[19].c->cull == 0);
    TIDE_REQUIRE(draws[20].c->kind == TIDE_DRAW_SHADED && draw.runs[draws[20].c->shaded.run].shader->dims == 2);
    TIDE_CHECK(draws[20].c->cull == 0);
    TIDE_CHECK(draws[21].c->kind == TIDE_DRAW_CLEAR && draws[21].c->cull == 0);

    // Set in one view, it holds in the next
    TIDE_CHECK(mesh_3d(22, TIDE_CULL_FRONT, 1, 9.0f) && draws[22].target == 0);
    // A Set inside a block is for the rest of the block, which still puts back what it found
    TIDE_CHECK(mesh_3d(23, TIDE_CULL_NONE, 1, 7.0f) && draws[23].target == 0);
    TIDE_CHECK(mesh_3d(24, TIDE_CULL_FRONT, 1, 6.0f));
}

// A render target's is its own, as its depth is: the window's doesn't reach
// its meshes, nor its the window's.
TIDE_TEST(draw_cull_of_a_target_is_its_own)
{
    whole_frame(false);
    TIDE_REQUIRE(draw_count == DRAWS);
    // The window's is set to draw no front by now; the target's meshes start as a frame does
    TIDE_CHECK(mesh_3d(25, TIDE_CULL_BACK, 1, 9.0f) && draws[25].target == 1);
    // Around a block of the target's: its meshes', and not the window's inside it
    TIDE_CHECK(mesh_3d(26, TIDE_CULL_NONE, 1, 8.0f) && draws[26].target == 1);
    TIDE_CHECK(mesh_3d(27, TIDE_CULL_FRONT, 1, 7.0f) && draws[27].target == 0);
    // Set, and with no arguments, as a frame starts: each destination's own
    TIDE_CHECK(mesh_3d(28, TIDE_CULL_FRONT, 1, 6.0f) && draws[28].target == 1);
    TIDE_CHECK(mesh_3d(29, TIDE_CULL_BACK, 1, 5.0f) && draws[29].target == 1);
    TIDE_CHECK(mesh_3d(30, TIDE_CULL_BACK, 1, 4.0f) && draws[30].target == 0);
    TIDE_REQUIRE(draw.target_count == 1 && draw.target == 0);
    TIDE_CHECK(draw.place.cull == TIDE_CULL_NONE); // The window's, left set by the last view
    TIDE_CHECK(draw.targets[0].place.cull == TIDE_CULL_BACK);
}

// Left through `try`, `return`, `break` and `continue`, a block still puts
// back what it found, and the next frame starts over.
TIDE_TEST(draw_cull_survives_leaving_early)
{
    whole_frame(false);
    frame(true);
    TIDE_CHECK(draw.saved_count == 0);
    // The frame before left a cull set: this one started without it
    TIDE_CHECK(mesh_3d(0, TIDE_CULL_BACK, 1, 9.0f));
    TIDE_REQUIRE(draw_count == DRAWS - 1);
    // Guarded's cube never drew. What came next has no back drawn, over what's there: not Guarded's cull and
    // blend, Counted's cull nor the loop's
    TIDE_CHECK(mesh_3d(12, TIDE_CULL_BACK, 1, 8.0f) && draws[12].c->blend == TIDE_BLEND_ALPHA);
    TIDE_CHECK(draw.matrices[draws[12].c->mesh3.first].c3.x == 1.0f);
    TIDE_CHECK(shaded(13, TIDE_CULL_FRONT, 1));
}

// The enum is in the game's C, as it names it: a value like any other's.
TIDE_TEST(draw_cull_is_a_value)
{
    tide_local_init(&local);
    TIDE_CHECK(local.Look.kept == Cull_Front && Cull_Back == TIDE_CULL_BACK);
    TIDE_CHECK(Cull_Front == TIDE_CULL_FRONT && Cull_None == TIDE_CULL_NONE);
}
