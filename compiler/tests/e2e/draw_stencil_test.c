#include <string.h>

#include "game.h"
#include "tide_test.h"
#include "draw_test.h"

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

// A command that draws, the destination the commands before it left it to draw into, and what that
// destination's commands do with its stencil by then.
typedef struct drawn {
    const tide_draw_command *c;
    uint32_t target;  // 0 for the window, or 1 + its place in the list's targets
    uint32_t stencil; // A tide_stencil, packed
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
    uint32_t stencils[8] = {0}; // Each destination's own, as a renderer keeps them
    for (uint32_t i = 0; i < draw.count; i++) {
        const tide_draw_command *c = &draw.commands[i];
        switch (c->kind) {
        case TIDE_DRAW_TARGET: target = c->target; break;
        case TIDE_DRAW_STENCIL: stencils[target < 8 ? target : 7] = c->stencil; break;
        case TIDE_DRAW_CLIP:
        case TIDE_DRAW_NO_CLIP:
        case TIDE_DRAW_SCREEN:
        case TIDE_DRAW_CAMERA:
        case TIDE_DRAW_CAMERA_3D: break;
        default:
            if (draw_count < 64) draws[draw_count++] = (drawn){c, target, stencils[target < 8 ? target : 7]};
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

enum { DRAWS = 25, NONE = 0 };

// A stencil as a place keeps it.
static uint32_t stencil(const tide_stencil_test test, const int value, const tide_stencil_op op)
{
    return tide_stencil_packed(tide_stencil_stored(test, value, op));
}

// Whether what was drawn `i`th is a command of `kind` at (x, y), under `with`.
static bool shape(const int i, const tide_draw_kind kind, const float x, const float y, const uint32_t with)
{
    const drawn *d = &draws[i];
    return d->c->kind == kind && d->c->a.x == x && d->c->a.y == y && d->stencil == with;
}

TIDE_TEST(draw_stencil_wraps_blocks)
{
    whole_frame(false);
    TIDE_REQUIRE(draw_count == DRAWS);
    TIDE_CHECK(draw.saved_count == 0); // Every block put back what it found

    // As a frame starts, around a block, one inside another, and what each found after it
    TIDE_CHECK(shape(0, TIDE_DRAW_RECT, 0, 9, NONE));
    TIDE_CHECK(draw.commands[0].kind == TIDE_DRAW_RECT); // Nothing is said before it: there's none to say
    TIDE_CHECK(shape(1, TIDE_DRAW_CIRCLE, 0, 8, stencil(TIDE_STENCIL_ALWAYS, 1, TIDE_STENCIL_REPLACE)));
    TIDE_CHECK(shape(2, TIDE_DRAW_RECT, 0, 7, stencil(TIDE_STENCIL_EQUAL, 1, TIDE_STENCIL_KEEP)));
    TIDE_CHECK(shape(3, TIDE_DRAW_RECT, 0, 6, stencil(TIDE_STENCIL_ALWAYS, 1, TIDE_STENCIL_REPLACE)));
    TIDE_CHECK(shape(4, TIDE_DRAW_RECT, 0, 5, NONE));

    // Listed with commas, beside a blend and a depth: a 3D mesh and text take it as a shape does
    const uint32_t less = stencil(TIDE_STENCIL_LESS, 2, TIDE_STENCIL_KEEP);
    TIDE_REQUIRE(draws[5].c->kind == TIDE_DRAW_MESH_3D && draw.matrices[draws[5].c->mesh3.first].c3.z == 4.0f);
    TIDE_CHECK(draws[5].stencil == less && draws[5].c->blend == TIDE_BLEND_ADD && draws[5].c->depth == TIDE_DEPTH_READ);
    TIDE_CHECK(draws[6].c->kind == TIDE_DRAW_TEXT && draws[6].stencil == less && draws[6].c->blend == TIDE_BLEND_ADD);
    TIDE_CHECK(shape(7, TIDE_DRAW_RECT, 0, 3, NONE) && draws[7].c->blend == TIDE_BLEND_ALPHA);

    // Around each of three entities' shapes: said once, with nothing between them, so a renderer draws them at once
    const uint32_t three = stencil(TIDE_STENCIL_EQUAL, 3, TIDE_STENCIL_KEEP);
    TIDE_CHECK(shape(8, TIDE_DRAW_RECT, 5, 9, three) && shape(9, TIDE_DRAW_RECT, 5, 8, three));
    TIDE_CHECK(shape(10, TIDE_DRAW_RECT, 5, 7, three));
    TIDE_CHECK(draws[10].c - draws[8].c == 2 && draws[8].c[-1].kind == TIDE_DRAW_STENCIL && draws[8].c[-1].stencil == three);
}

TIDE_TEST(draw_stencil_takes_values)
{
    whole_frame(false);
    TIDE_REQUIRE(draw_count == DRAWS);
    // Blocks that ran to their end, in the functions a view calls
    TIDE_CHECK(shape(11, TIDE_DRAW_RECT, 1, 9, stencil(TIDE_STENCIL_GREATER, 4, TIDE_STENCIL_ZERO)));
    TIDE_CHECK(draws[11].c->blend == TIDE_BLEND_ADD);
    TIDE_CHECK(shape(12, TIDE_DRAW_RECT, 2, 8, NONE) && draws[12].c->blend == TIDE_BLEND_ALPHA);

    // A mesh through a frag, under a test and an operation the game keeps in fields: a number past what a pixel
    // stores is the most it does
    TIDE_REQUIRE(draws[13].c->kind == TIDE_DRAW_SHADED && draw.runs[draws[13].c->shaded.run].shader->dims == 3);
    TIDE_CHECK(draws[13].stencil == stencil(TIDE_STENCIL_NOT_EQUAL, 255, TIDE_STENCIL_INVERT));
    TIDE_CHECK(local.Look.kept == StencilTest_NotEqual && local.Look.does == StencilOp_Invert && local.Look.layer == 300);
    // `default` is the test every pixel passes, whose number nothing reads unless it's what's stored
    TIDE_CHECK(shape(14, TIDE_DRAW_RECT, 2, 9, stencil(TIDE_STENCIL_ALWAYS, 0, TIDE_STENCIL_INCREMENT)));
    // One that neither tests nor stores is none: nothing is said for it
    TIDE_CHECK(shape(15, TIDE_DRAW_RECT, 2, 8, NONE) && draws[15].c[-1].kind == TIDE_DRAW_STENCIL);
    TIDE_CHECK(draws[15].c[-1].stencil == NONE);
    // By name, in any order
    TIDE_CHECK(shape(16, TIDE_DRAW_RECT, 2, 7, stencil(TIDE_STENCIL_ALWAYS, 6, TIDE_STENCIL_REPLACE)));

    // The enums' numbers are the engine's
    TIDE_CHECK(StencilTest_Always == TIDE_STENCIL_ALWAYS && StencilTest_Never == TIDE_STENCIL_NEVER);
    TIDE_CHECK(StencilTest_Less == TIDE_STENCIL_LESS && StencilTest_Equal == TIDE_STENCIL_EQUAL);
    TIDE_CHECK(StencilTest_LessEqual == TIDE_STENCIL_LESS_EQUAL && StencilTest_Greater == TIDE_STENCIL_GREATER);
    TIDE_CHECK(StencilTest_NotEqual == TIDE_STENCIL_NOT_EQUAL && StencilTest_GreaterEqual == TIDE_STENCIL_GREATER_EQUAL);
    TIDE_CHECK(StencilOp_Keep == TIDE_STENCIL_KEEP && StencilOp_Replace == TIDE_STENCIL_REPLACE);
    TIDE_CHECK(StencilOp_Increment == TIDE_STENCIL_INCREMENT && StencilOp_Decrement == TIDE_STENCIL_DECREMENT);
    TIDE_CHECK(StencilOp_Zero == TIDE_STENCIL_ZERO && StencilOp_Invert == TIDE_STENCIL_INVERT);
}

TIDE_TEST(draw_stencil_is_each_destinations_own)
{
    whole_frame(false);
    TIDE_REQUIRE(draw_count == DRAWS);
    const uint32_t never = stencil(TIDE_STENCIL_NEVER, 0, TIDE_STENCIL_KEEP);
    const uint32_t two = stencil(TIDE_STENCIL_ALWAYS, 2, TIDE_STENCIL_REPLACE);
    // A target's draws start as a frame does, whatever the window's block says
    TIDE_REQUIRE(draws[17].target == 1 && draw.targets[0].id == 11);
    TIDE_CHECK(shape(17, TIDE_DRAW_RECT, 3, 9, NONE));
    // Its own block, for its shapes and its 3D meshes
    TIDE_CHECK(shape(18, TIDE_DRAW_CIRCLE, 3, 8, two) && draws[18].target == 1);
    TIDE_CHECK(draws[19].c->kind == TIDE_DRAW_MESH_3D && draws[19].stencil == two && draws[19].target == 1);
    // Set, and put back as a frame starts
    TIDE_CHECK(shape(20, TIDE_DRAW_RECT, 3, 7, stencil(TIDE_STENCIL_EQUAL, 2, TIDE_STENCIL_KEEP)) && draws[20].target == 1);
    TIDE_CHECK(shape(21, TIDE_DRAW_RECT, 3, 6, NONE) && draws[21].target == 1);
    // ...and the window's is what its own block says, all the while
    TIDE_CHECK(shape(22, TIDE_DRAW_RECT, 3, 5, never) && draws[22].target == 0);

    // Set, it holds for the views that follow, until it's set again
    TIDE_CHECK(shape(23, TIDE_DRAW_RECT, 4, 9, stencil(TIDE_STENCIL_LESS_EQUAL, 8, TIDE_STENCIL_KEEP)) && draws[23].target == 0);
    TIDE_CHECK(shape(24, TIDE_DRAW_RECT, 4, 8, NONE));
    // Every frame starts over
    TIDE_CHECK(draw.place.stencil == NONE);
    frame(false);
    TIDE_REQUIRE(draw_count == DRAWS);
    TIDE_CHECK(shape(0, TIDE_DRAW_RECT, 0, 9, NONE) && draw.commands[0].kind == TIDE_DRAW_RECT);
}

TIDE_TEST(draw_stencil_blocks_left_early_put_back)
{
    whole_frame(true);
    // The guarded block's rect isn't drawn: its error left through both of its settings
    TIDE_REQUIRE(draw_count == DRAWS - 1);
    TIDE_CHECK(draw.saved_count == 0);
    // What's drawn after a return, a continue and a break out of blocks is drawn with none, and no blend
    TIDE_CHECK(shape(11, TIDE_DRAW_RECT, 1, 8, NONE) && draws[11].c->blend == TIDE_BLEND_ALPHA);
    // ...and nothing was said for the blocks that drew nothing
    TIDE_CHECK(draws[11].c[-1].kind == TIDE_DRAW_STENCIL && draws[11].c[-1].stencil == NONE);
    TIDE_CHECK(draws[10].c == draws[11].c - 2);
    TIDE_CHECK(draws[12].c->kind == TIDE_DRAW_SHADED);
    tide_draw_free(&draw);
}
