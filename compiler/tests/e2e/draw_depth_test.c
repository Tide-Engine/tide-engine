#include <string.h>

#include "game.h"
#include "tide_test.h"

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

// Whether what was drawn `i`th is a run of `count` instances of a 3D mesh that does `depth` with depth, the first
// `z` away.
static bool mesh_3d(const int i, const tide_depth depth, const uint32_t count, const float z)
{
    const tide_draw_command *c = draws[i].c;
    return c->kind == TIDE_DRAW_MESH_3D && c->depth == depth && c->mesh3.count == count
        && draw.matrices[c->mesh3.first].c3.z == z;
}

TIDE_TEST(draw_depth_wraps_blocks)
{
    whole_frame(false);
    TIDE_REQUIRE(draw_count == 25);
    TIDE_CHECK(draw.saved_count == 0); // Every block put back what it found

    // As a frame starts, around a block, one inside another, and what each found after it
    TIDE_CHECK(mesh_3d(0, TIDE_DEPTH_READ_WRITE, 1, 9.0f));
    TIDE_CHECK(mesh_3d(1, TIDE_DEPTH_READ, 1, 8.0f));
    TIDE_CHECK(mesh_3d(2, TIDE_DEPTH_NONE, 1, 7.0f));
    TIDE_CHECK(mesh_3d(3, TIDE_DEPTH_READ, 1, 6.0f));
    TIDE_CHECK(mesh_3d(4, TIDE_DEPTH_READ_WRITE, 1, 5.0f));
    // Listed with commas, beside a blend and a tint
    TIDE_CHECK(mesh_3d(5, TIDE_DEPTH_READ, 1, 4.0f) && draws[5].c->blend == TIDE_BLEND_ADD);
    TIDE_CHECK(draws[5].c->color.g == 0.5f && draws[5].c->color.b == 0.25f);
    TIDE_CHECK(mesh_3d(6, TIDE_DEPTH_READ_WRITE, 1, 3.0f) && draws[6].c->blend == TIDE_BLEND_ALPHA);

    // Around each of three entities' meshes: one run, which a renderer draws at once
    TIDE_CHECK(mesh_3d(7, TIDE_DEPTH_READ, 3, 5.0f));

    // Blocks that ran to their end, in the functions a view calls
    TIDE_CHECK(mesh_3d(8, TIDE_DEPTH_NONE, 1, 9.0f) && draws[8].c->blend == TIDE_BLEND_ADD);
    TIDE_CHECK(mesh_3d(9, TIDE_DEPTH_READ_WRITE, 1, 8.0f) && draw.matrices[draws[9].c->mesh3.first].c3.x == 2.0f);
}

TIDE_TEST(draw_depth_reaches_shaders)
{
    whole_frame(false);
    TIDE_REQUIRE(draw_count == 25);
    // A mesh through a frag, and a list's instances: their commands', here from a value the game keeps
    const tide_draw_command *mesh = draws[10].c, *spots = draws[11].c, *after = draws[12].c;
    TIDE_REQUIRE(mesh->kind == TIDE_DRAW_SHADED && spots->kind == TIDE_DRAW_SHADED && after->kind == TIDE_DRAW_SHADED);
    TIDE_CHECK(mesh->depth == TIDE_DEPTH_READ && draw.runs[mesh->shaded.run].count == 1);
    TIDE_CHECK(spots->depth == TIDE_DEPTH_READ && draw.runs[spots->shaded.run].count == 2);
    TIDE_CHECK(after->depth == TIDE_DEPTH_READ_WRITE && draw.runs[after->shaded.run].shader->dims == 3);

    // Nothing else has depth: a shape, a 2D mesh through a frag and a clear say none, whatever is set
    TIDE_CHECK(draws[13].c->kind == TIDE_DRAW_RECT && draws[13].c->depth == 0);
    TIDE_REQUIRE(draws[14].c->kind == TIDE_DRAW_SHADED && draw.runs[draws[14].c->shaded.run].shader->dims == 2);
    TIDE_CHECK(draws[14].c->depth == 0);
    TIDE_CHECK(draws[15].c->kind == TIDE_DRAW_CLEAR && draws[15].c->depth == 0);

    // Set in one view, it holds in the next
    TIDE_CHECK(mesh_3d(16, TIDE_DEPTH_READ, 1, 9.0f) && draws[16].target == 0);
    // A Set inside a block is for the rest of the block, which still puts back what it found
    TIDE_CHECK(mesh_3d(17, TIDE_DEPTH_NONE, 1, 7.0f) && draws[17].target == 0);
    TIDE_CHECK(mesh_3d(18, TIDE_DEPTH_READ, 1, 6.0f));
}

// A render target's is its own, as its cameras are: the window's doesn't
// reach its meshes, nor its the window's.
TIDE_TEST(draw_depth_of_a_target_is_its_own)
{
    whole_frame(false);
    TIDE_REQUIRE(draw_count == 25);
    // The window's is set to read alone by now; the target's meshes start as a frame does
    TIDE_CHECK(mesh_3d(19, TIDE_DEPTH_READ_WRITE, 1, 9.0f) && draws[19].target == 1);
    // Around a block of the target's: its meshes', and not the window's inside it
    TIDE_CHECK(mesh_3d(20, TIDE_DEPTH_NONE, 1, 8.0f) && draws[20].target == 1);
    TIDE_CHECK(mesh_3d(21, TIDE_DEPTH_READ, 1, 7.0f) && draws[21].target == 0);
    // Set, and with no arguments, as a frame starts: each destination's own
    TIDE_CHECK(mesh_3d(22, TIDE_DEPTH_READ, 1, 6.0f) && draws[22].target == 1);
    TIDE_CHECK(mesh_3d(23, TIDE_DEPTH_READ_WRITE, 1, 5.0f) && draws[23].target == 1);
    TIDE_CHECK(mesh_3d(24, TIDE_DEPTH_READ_WRITE, 1, 4.0f) && draws[24].target == 0);
    TIDE_REQUIRE(draw.target_count == 1 && draw.target == 0);
    TIDE_CHECK(draw.place.depth == TIDE_DEPTH_NONE); // The window's, left set by the last view
    TIDE_CHECK(draw.targets[0].place.depth == TIDE_DEPTH_READ_WRITE);
}

// Left through `try`, `return`, `break` and `continue`, a block still puts
// back what it found, and the next frame starts over.
TIDE_TEST(draw_depth_survives_leaving_early)
{
    whole_frame(false);
    frame(true);
    TIDE_CHECK(draw.saved_count == 0);
    // The frame before left a depth set: this one started without it
    TIDE_CHECK(mesh_3d(0, TIDE_DEPTH_READ_WRITE, 1, 9.0f));
    TIDE_REQUIRE(draw_count == 24);
    // Guarded's cube never drew. What came next read and wrote depth, over what's there: not Guarded's
    // depth and blend, Counted's depth nor the loop's
    TIDE_CHECK(mesh_3d(8, TIDE_DEPTH_READ_WRITE, 1, 8.0f) && draws[8].c->blend == TIDE_BLEND_ALPHA);
    TIDE_CHECK(draw.matrices[draws[8].c->mesh3.first].c3.x == 1.0f);
    TIDE_CHECK(draws[9].c->kind == TIDE_DRAW_SHADED && draws[9].c->depth == TIDE_DEPTH_READ);
}

// The enum is in the game's C, as it names it: a value like any other's.
TIDE_TEST(draw_depth_is_a_value)
{
    tide_local_init(&local);
    TIDE_CHECK(local.Look.kept == Depth_Read && Depth_ReadWrite == TIDE_DEPTH_READ_WRITE);
    TIDE_CHECK(Depth_Read == TIDE_DEPTH_READ && Depth_None == TIDE_DEPTH_NONE);
}
