#include <string.h>

#include "game.h"
#include "tide_test.h"

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

// A command that draws, with what the commands before it left it to draw
// with: its destination, and that destination's clip and cameras.
typedef struct drawn {
    const tide_draw_command *c;
    uint32_t target; // 0 for the window, or 1 + its place in the list's targets
    uint32_t clip;   // 1 + its place in the list's clips, or 0 for none
    int camera;      // 0: a frame's first; 1: a camera; 2: the screen
    tide_float2 center;
    float size;
    uint32_t camera_3d; // 1 + its matrix's place, or 0 for a frame's first
} drawn;

static drawn draws[64];
static int draw_count;

static void frame(const bool leave)
{
    local.Look.leave = leave;
    tide_draw_reset(&draw);
    static tide_devices devices;
    tide_gui_begin(&gui, &devices, tide_f2(800, 600), NULL); // A screen, for the conversions
    tide_frame(&world, NULL, 1.0f, NULL, &local, &draw, &gui);
    draw_count = 0;
    drawn places[8] = {0};
    uint32_t target = 0;
    for (uint32_t i = 0; i < draw.count; i++) {
        const tide_draw_command *c = &draw.commands[i];
        drawn *place = &places[target];
        switch (c->kind) {
        case TIDE_DRAW_TARGET: target = c->target < 8u ? c->target : 0u; break;
        case TIDE_DRAW_CLIP: place->clip = c->clip; break;
        case TIDE_DRAW_NO_CLIP: place->clip = 0; break;
        case TIDE_DRAW_SCREEN: place->camera = 2; break;
        case TIDE_DRAW_CAMERA:
            place->camera = c->a.x == 0.0f && c->a.y == 0.0f && c->b.x == 0.0f ? 0 : 1;
            place->center = c->a;
            place->size = c->b.x;
            break;
        case TIDE_DRAW_CAMERA_3D:
            place->camera_3d = c->camera.matrix == TIDE_DRAW_FIRST_CAMERA ? 0u : c->camera.matrix + 1u;
            break;
        default:
            if (draw_count < 64) {
                draws[draw_count] = *place;
                draws[draw_count].c = c;
                draws[draw_count++].target = target;
            }
            break;
        }
    }
}

static bool colored(const tide_color c, const float r, const float g, const float b, const float a)
{
    return c.r == r && c.g == g && c.b == b && c.a == a;
}

// Whether what was drawn `i`th is a rect around (x, y) as it was given: white, over what's there, through
// nothing.
static bool plain_rect(const int i, const float x, const float y)
{
    const drawn *d = &draws[i];
    return d->c->kind == TIDE_DRAW_RECT && d->c->a.x == x && d->c->a.y == y && d->c->b.x == 1.0f && d->c->b.y == 1.0f
        && d->c->blend == TIDE_BLEND_ALPHA && colored(d->c->color, 1, 1, 1, 1) && d->target == 0 && d->clip == 0
        && d->camera == 0;
}

// A frame whose blocks run to their end, from the start: each test's own, as they run in any order.
static void whole_frame(void)
{
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    local.Look.target = (tide_render_target){.id = 11, .size = {.width = 16, .height = 12}};
    frame(false);
}

TIDE_TEST(draw_settings_wrap_blocks)
{
    whole_frame();
    TIDE_REQUIRE(draw_count == 37);
    TIDE_CHECK(draw.saved_count == 0); // Every block put back what it found

    // Around a block, and one inside another
    TIDE_CHECK(plain_rect(0, 0, 0));
    TIDE_CHECK(draws[1].c->blend == TIDE_BLEND_ADD && colored(draws[1].c->color, 1, 1, 1, 1));
    TIDE_CHECK(draws[2].c->blend == TIDE_BLEND_ADD && colored(draws[2].c->color, 1.0f, 0.5f, 0.5f, 0.5f));
    TIDE_CHECK(plain_rect(3, 3, 0));

    // Listed with commas: each inside the one before, the clip made before the transform it isn't in
    const drawn *listed = &draws[4];
    TIDE_REQUIRE(listed->c->kind == TIDE_DRAW_RECT && listed->clip);
    TIDE_CHECK(listed->c->blend == TIDE_BLEND_MULTIPLY && colored(listed->c->color, 0.5f, 1, 1, 1));
    TIDE_CHECK(listed->c->a.x == 12.0f && listed->c->a.y == 22.0f && listed->c->b.x == 2.0f && listed->c->b.y == 2.0f);
    const tide_draw_clip_data *panel = &draw.clips[listed->clip - 1u];
    TIDE_CHECK(panel->rect.x == 10.0f && panel->rect.y == 20.0f && panel->rect.width == 200.0f && panel->parent == 0);
    // A clip in a clip is inside it
    const drawn *nested = &draws[5];
    TIDE_REQUIRE(nested->c->kind == TIDE_DRAW_CIRCLE && nested->clip && nested->clip != listed->clip);
    TIDE_CHECK(nested->c->a.x == 10.0f && nested->c->a.y == 20.0f && nested->c->b.x == 2.0f);
    const tide_draw_clip_data *row = &draw.clips[nested->clip - 1u];
    TIDE_CHECK(row->parent == listed->clip && row->rect.x == 110.0f && row->rect.y == 120.0f && row->rect.height == 200.0f);
    TIDE_CHECK(plain_rect(6, 4, 0));
    // A function that takes a block is one of the list
    TIDE_CHECK(draws[7].c->kind == TIDE_DRAW_RECT && draws[7].c->a.x == 101.0f && colored(draws[7].c->color, 1, 0, 0, 1));
    TIDE_CHECK(draws[7].c->blend == TIDE_BLEND_ALPHA && draws[7].clip == 0);

    // Blocks that ran to their end, in a view and in the functions it calls
    TIDE_CHECK(local.Look.left == 2 && plain_rect(8, 9, 9));
    TIDE_CHECK(draws[9].c->blend == TIDE_BLEND_ADD && draws[9].clip && draws[9].c->a.y == 5.0f);
    TIDE_CHECK(plain_rect(10, 2, 0));
}

TIDE_TEST(draw_settings_reach_shaders_and_targets)
{
    whole_frame();
    TIDE_REQUIRE(draw_count == 37);
    // A mesh through a frag: its run's tint and transform, its command's blend
    const drawn *shaded = &draws[11];
    TIDE_REQUIRE(shaded->c->kind == TIDE_DRAW_SHADED && shaded->c->blend == TIDE_BLEND_ADD);
    const tide_draw_run *run = &draw.runs[shaded->c->shaded.run];
    TIDE_CHECK(colored(run->tint, 0.25f, 0.5f, 1, 1) && run->moved && run->transform.offset.x == 3.0f
               && run->transform.offset.y == 4.0f && run->transform.x.x == 1.0f);

    // Set in one view, they hold in the next
    const drawn *follows = &draws[12];
    TIDE_REQUIRE(follows->c->kind == TIDE_DRAW_RECT && follows->clip);
    TIDE_CHECK(follows->c->blend == TIDE_BLEND_ADD && colored(follows->c->color, 1, 1, 0, 1));
    TIDE_CHECK(follows->c->a.x == 7.0f && follows->c->a.y == 8.0f && follows->c->b.x == 2.0f && follows->c->b.y == 3.0f);
    TIDE_CHECK(follows->camera == 1 && follows->center.x == 8.0f && follows->size == 10.0f);
    // The clip went through the transform there was when it was set, under the camera there was then
    const tide_draw_clip_data *clip = &draw.clips[follows->clip - 1u];
    TIDE_CHECK(clip->rect.x == 7.0f && clip->rect.y == 11.0f && clip->rect.width == 6.0f && clip->rect.height == 12.0f);
    TIDE_CHECK(clip->camera == 0);
    // The screen around a label, which a transform that stretches leaves no pixels to fit
    const drawn *label = &draws[13];
    TIDE_REQUIRE(label->c->kind == TIDE_DRAW_TEXT && label->camera == 2);
    TIDE_CHECK(label->c->a.x == 13.0f && label->c->a.y == 17.0f && label->c->b.x == 8.0f && label->c->words.turned);
    TIDE_CHECK(draws[14].camera == 1 && draws[14].size == 10.0f && draws[14].c->a.x == 7.0f); // The camera it found
    // A Set inside a block is for the rest of the block
    TIDE_CHECK(colored(draws[15].c->color, 0, 0, 1, 1) && colored(draws[16].c->color, 1, 1, 0, 1));
    // ...and with no arguments, each is as a frame starts
    TIDE_CHECK(plain_rect(17, 1, 1));

    // A target's own: its clip and transform aren't the window's, nor the window's its
    const drawn *on_target = &draws[18];
    TIDE_REQUIRE(on_target->target == 1 && on_target->clip && on_target->camera == 1);
    TIDE_CHECK(on_target->c->a.x == 1.0f && on_target->c->a.y == 1.0f && on_target->size == 3.0f);
    TIDE_CHECK(draw.clips[on_target->clip - 1u].rect.width == 2.0f && draw.clips[on_target->clip - 1u].rect.x == 0.0f);
    TIDE_CHECK(plain_rect(19, 0, 0));
    TIDE_CHECK(draws[20].target == 1 && draws[20].clip == 0 && draws[20].c->a.x == 3.0f && draws[20].camera == 1);
    TIDE_REQUIRE(draw.target_count == 1);
    const tide_draw_place *place = &draw.targets[0].place; // The window is selected by now
    TIDE_CHECK(place->camera_2d == 0 && place->camera_3d == 0 && place->moved == 0 && place->clip == 0);

    // 3D meshes go through the transform on their way to the world, and through the camera of their block
    const drawn *moved = &draws[21], *seen = &draws[22];
    TIDE_REQUIRE(moved->c->kind == TIDE_DRAW_MESH_3D && seen->c->kind == TIDE_DRAW_MESH_3D);
    TIDE_CHECK(draw.matrices[moved->c->mesh3.first].c3.x == 1.0f && moved->camera_3d == 0 && moved->target == 0);
    TIDE_CHECK(draw.matrices[seen->c->mesh3.first].c3.y == 2.0f && seen->camera_3d != 0);
    TIDE_CHECK(draw.place.camera_3d == 0 && !(draw.place.moved & TIDE_DRAW_MOVED_3D));

    // A blit is its destination's, whatever is set
    const drawn *replaced = &draws[23], *added = &draws[24];
    TIDE_REQUIRE(replaced->c->kind == TIDE_DRAW_BLIT && added->c->kind == TIDE_DRAW_BLIT);
    TIDE_CHECK(replaced->c->blend == TIDE_BLEND_REPLACE && added->c->blend == TIDE_BLEND_ADD);
    TIDE_CHECK(colored(draw.runs[replaced->c->shaded.run].tint, 1, 1, 1, 1) && !draw.runs[added->c->shaded.run].moved);
    TIDE_CHECK(draw.place.blend == TIDE_BLEND_MULTIPLY && draw.place.tinted); // Left set by the last view

    // One Draw: a target's blend and tint are its own, and the window's its own
    TIDE_CHECK(draws[25].target == 1 && draws[25].c->blend == TIDE_BLEND_ALPHA && colored(draws[25].c->color, 1, 1, 1, 1));
    TIDE_CHECK(draws[26].target == 1 && draws[26].c->blend == TIDE_BLEND_MULTIPLY && colored(draws[26].c->color, 1, 0, 0, 1));
    TIDE_CHECK(draws[27].target == 0 && draws[27].c->blend == TIDE_BLEND_ADD && colored(draws[27].c->color, 0.5f, 0.5f, 0.5f, 1));
    TIDE_CHECK(draws[28].target == 1 && draws[28].c->blend == TIDE_BLEND_REPLACE && colored(draws[28].c->color, 1, 1, 1, 1));
    TIDE_CHECK(draws[29].target == 0 && draws[29].c->blend == TIDE_BLEND_ADD && colored(draws[29].c->color, 0.5f, 0.5f, 0.5f, 1));
    // The 2D camera from a center and a size, and the conversions through it
    TIDE_CHECK(draws[30].camera == 1 && draws[30].center.x == 3.0f && draws[30].center.y == 4.0f && draws[30].size == 5.0f);
    TIDE_CHECK(draws[31].c->kind == TIDE_DRAW_RECT && draws[31].c->a.x == 3.0f && draws[31].c->a.y == 4.0f);
    // The 3D camera from a position, a rotation and a field of view, with a 3D transform, and from matrices
    TIDE_REQUIRE(draws[32].c->kind == TIDE_DRAW_MESH_3D && draws[33].c->kind == TIDE_DRAW_MESH_3D);
    TIDE_CHECK(draws[32].camera_3d != 0 && draw.matrices[draws[32].c->mesh3.first].c3.y == 2.0f);
    TIDE_CHECK(local.Look.depth > 0.0f && local.Look.depth < 1.0f);
    TIDE_CHECK(draws[33].camera_3d != 0 && draws[33].camera_3d != draws[32].camera_3d);
    // Every Set form took, and with no arguments both cameras and both transforms went back
    TIDE_CHECK(plain_rect(34, 0, 0) && draws[34].camera_3d == 0);
    TIDE_CHECK(draws[35].c->kind == TIDE_DRAW_MESH && draws[36].c->kind == TIDE_DRAW_MESH_3D && draws[36].camera_3d == 0);
}

// Left through `return`, `try`, `fail`, `break` and `continue`, a block still
// puts back what it found, and the next frame starts over.
TIDE_TEST(draw_settings_survive_leaving_early)
{
    whole_frame();
    frame(true);
    TIDE_CHECK(draw.saved_count == 0);
    // The frame before left a blend and a tint set: this one started without them
    TIDE_CHECK(plain_rect(0, 0, 0));
    TIDE_REQUIRE(draw_count == 23);
    TIDE_CHECK(local.Look.left == 1);
    // Early's rect and Guarded's never drew. What came next drew through nothing: not Early's tint and transform,
    // Guarded's blend and clip, Failing's tint, Counted's transform nor the loop's tint
    TIDE_CHECK(plain_rect(8, 1, 0));
    TIDE_CHECK(draws[9].c->kind == TIDE_DRAW_SHADED);
}
