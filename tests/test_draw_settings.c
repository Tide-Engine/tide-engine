// The draw list's settings: how what's drawn after them comes out (blend,
// tint, depth) and where it lands (transforms, clip, cameras), in their two
// forms: set for the commands to come, or pushed around some and popped.

#include <string.h>

#include "tide/draw.h"
#include "tide_test.h"

static bool about(const float a, const float b)
{
    return a - b < 1e-4f && b - a < 1e-4f;
}

static bool color_is(const tide_color c, const float r, const float g, const float b, const float a)
{
    return about(c.r, r) && about(c.g, g) && about(c.b, b) && about(c.a, a);
}

static const tide_draw_command *last(const tide_draw_list *d)
{
    return &d->commands[d->count - 1u];
}

static const tide_float2 origin = {0.0f, 0.0f}, unit = {1.0f, 1.0f};

static const tide_vertex3 corners[4] = {
    {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
    {{1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
    {{1.0f, 1.0f, 0.0f}, {1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
    {{0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
};
static const uint32_t triangles[6] = {0, 1, 2, 0, 2, 3};
static const tide_mesh quad = {corners, 4, triangles, 6, 1};

// A mesh for a shader of `vertex_size` bytes a vertex, as the list has it.
static uint32_t shader_mesh(tide_draw_list *d, const uint32_t vertex_size)
{
    static const float bytes[4 * 8] = {0};
    bool fill;
    const uint32_t mesh = tide_draw_mesh_slot(d, TIDE_MESH_BYTES, 77, vertex_size, 1, &fill);
    if (fill) tide_draw_mesh_fill_bytes(d, mesh, bytes, vertex_size, 4, triangles, 6);
    return mesh;
}

// Every frame starts from the defaults, whatever the last one left set.
TIDE_TEST(draw_settings_start_over_each_frame)
{
    tide_draw_list d = {0};
    tide_draw_rect(&d, origin, unit, TIDE_COLOR_RED);
    TIDE_REQUIRE(d.count == 1 && d.commands[0].kind == TIDE_DRAW_RECT);
    TIDE_CHECK(d.commands[0].blend == TIDE_BLEND_ALPHA && color_is(d.commands[0].color, 1, 0, 0, 1));

    tide_draw_set_blend(&d, TIDE_BLEND_ADD);
    tide_draw_set_depth(&d, TIDE_DEPTH_NONE);
    tide_draw_set_tint(&d, (tide_color){0.5f, 0.5f, 0.5f, 0.5f});
    tide_draw_set_transform(&d, tide_draw_transform_trs(tide_f2(10.0f, 20.0f), 0.0f, tide_f2(2.0f, 2.0f)));
    tide_draw_set_transform_3d(&d, tide_translate_f4x4(tide_f3(1.0f, 2.0f, 3.0f)));
    tide_draw_set_clip(&d, (tide_rect){0.0f, 0.0f, 4.0f, 4.0f});
    tide_draw_set_screen(&d);
    tide_draw_set_camera_3d(&d, tide_f3(0.0f, 0.0f, -5.0f), tide_identity_q(), 90.0f);
    tide_draw_push_tint(&d, TIDE_COLOR_BLUE); // A block the frame never closed
    tide_draw_rect(&d, origin, unit, TIDE_COLOR_WHITE);
    TIDE_CHECK(last(&d)->blend == TIDE_BLEND_ADD && d.count > 2);

    tide_draw_reset(&d);
    TIDE_CHECK(d.blend == TIDE_BLEND_ALPHA && !d.tinted && !d.place.moved && !d.place.clip && !d.saved_count);
    TIDE_CHECK(!d.place.camera_2d && !d.place.camera_3d && d.depth == TIDE_DEPTH_READ_WRITE);
    tide_draw_rect(&d, origin, unit, TIDE_COLOR_RED);
    // Nothing but the rect, as it was given
    TIDE_REQUIRE(d.count == 1 && d.commands[0].kind == TIDE_DRAW_RECT && d.commands[0].blend == TIDE_BLEND_ALPHA);
    TIDE_CHECK(color_is(d.commands[0].color, 1, 0, 0, 1) && d.commands[0].a.x == 0.0f && d.commands[0].b.x == 1.0f);
    tide_draw_free(&d);
}

// A block puts back what it found, a Set inside it or not, and leaving
// several at once puts back what the outermost found.
TIDE_TEST(draw_settings_blocks_put_back_what_they_found)
{
    tide_draw_list d = {0};
    tide_draw_set_blend(&d, TIDE_BLEND_MULTIPLY);
    const uint32_t add = tide_draw_push_blend(&d, TIDE_BLEND_ADD);
    tide_draw_rect(&d, origin, unit, TIDE_COLOR_WHITE);
    TIDE_CHECK(last(&d)->blend == TIDE_BLEND_ADD);
    tide_draw_set_blend(&d, TIDE_BLEND_REPLACE); // Set inside the block: for the rest of it
    tide_draw_rect(&d, origin, unit, TIDE_COLOR_WHITE);
    TIDE_CHECK(last(&d)->blend == TIDE_BLEND_REPLACE);
    tide_draw_pop(&d, add);
    tide_draw_rect(&d, origin, unit, TIDE_COLOR_WHITE);
    TIDE_CHECK(last(&d)->blend == TIDE_BLEND_MULTIPLY && d.saved_count == 0);
    tide_draw_unset_blend(&d);
    TIDE_CHECK(d.blend == TIDE_BLEND_ALPHA);

    // Tints multiply, one inside the other
    tide_draw_set_tint(&d, (tide_color){1.0f, 0.5f, 1.0f, 1.0f});
    const uint32_t outer = tide_draw_push_tint(&d, (tide_color){0.5f, 1.0f, 1.0f, 0.5f});
    const uint32_t inner = tide_draw_push_tint(&d, (tide_color){1.0f, 1.0f, 0.5f, 0.5f});
    TIDE_CHECK(inner == outer + 1u);
    tide_draw_rect(&d, origin, unit, TIDE_COLOR_WHITE);
    TIDE_CHECK(color_is(last(&d)->color, 0.5f, 0.5f, 0.5f, 0.25f));
    tide_draw_pop(&d, inner);
    tide_draw_rect(&d, origin, unit, TIDE_COLOR_WHITE);
    TIDE_CHECK(color_is(last(&d)->color, 0.5f, 0.5f, 1.0f, 0.5f));
    // Left early, from inside both: the outer mark puts back what the outer found
    tide_draw_push_tint(&d, TIDE_COLOR_RED);
    tide_draw_set_tint(&d, TIDE_COLOR_GREEN);
    tide_draw_pop(&d, outer);
    tide_draw_rect(&d, origin, unit, TIDE_COLOR_WHITE);
    TIDE_CHECK(color_is(last(&d)->color, 1.0f, 0.5f, 1.0f, 1.0f) && d.saved_count == 0);
    tide_draw_unset_tint(&d);
    tide_draw_rect(&d, origin, unit, TIDE_COLOR_WHITE);
    TIDE_CHECK(color_is(last(&d)->color, 1, 1, 1, 1) && !d.altered);

    // A block that found no tint leaves none
    const uint32_t only = tide_draw_push_tint(&d, TIDE_COLOR_RED);
    tide_draw_pop(&d, only);
    TIDE_CHECK(!d.tinted);

    // Transforms: the inner one is inside the outer
    const uint32_t moved = tide_draw_push_transform(&d, tide_draw_transform_moved(tide_f2(10.0f, 0.0f)));
    const uint32_t scaled = tide_draw_push_transform(&d, tide_draw_transform_scaled(tide_f2(0.0f, 5.0f), 0.0f, 2.0f));
    tide_draw_rect(&d, tide_f2(1.0f, 1.0f), unit, TIDE_COLOR_WHITE);
    TIDE_CHECK(last(&d)->kind == TIDE_DRAW_RECT && last(&d)->a.x == 12.0f && last(&d)->a.y == 7.0f && last(&d)->b.x == 2.0f);
    tide_draw_pop(&d, scaled);
    tide_draw_rect(&d, tide_f2(1.0f, 1.0f), unit, TIDE_COLOR_WHITE);
    TIDE_CHECK(last(&d)->a.x == 11.0f && last(&d)->a.y == 1.0f && last(&d)->b.x == 1.0f);
    tide_draw_pop(&d, moved);
    tide_draw_rect(&d, tide_f2(1.0f, 1.0f), unit, TIDE_COLOR_WHITE);
    TIDE_CHECK(last(&d)->a.x == 1.0f && !(d.place.moved & TIDE_DRAW_MOVED) && !d.altered);

    // Cameras: the screen around a label, and the camera it found is back
    tide_draw_set_camera(&d, tide_f2(3.0f, 4.0f), 5.0f);
    const uint32_t camera = d.place.camera_2d;
    const uint32_t screen = tide_draw_push_screen(&d);
    TIDE_CHECK(d.place.camera_2d != camera && d.cameras[d.place.camera_2d - 1u].screen == 1);
    tide_draw_pop(&d, screen);
    TIDE_CHECK(d.place.camera_2d == camera && d.cameras[camera - 1u].size == 5.0f);
    const uint32_t first = tide_draw_push_camera_3d(&d, tide_f3(0.0f, 0.0f, -5.0f), tide_identity_q(), 90.0f);
    TIDE_CHECK(d.place.camera_3d == 1);
    tide_draw_pop(&d, first);
    TIDE_CHECK(d.place.camera_3d == 0);
    tide_draw_free(&d);
}

// A clip in a block is what of its rect is inside the clip there was; set, it
// stands alone. A command names the clip it draws through.
TIDE_TEST(draw_settings_clips_nest)
{
    tide_draw_list d = {0};
    const uint32_t panel = tide_draw_push_clip(&d, (tide_rect){0.0f, 0.0f, 100.0f, 100.0f});
    const uint32_t outer = d.place.clip;
    TIDE_REQUIRE(outer && d.clips[outer - 1u].parent == 0 && d.count == 0);
    const uint32_t row = tide_draw_push_clip(&d, (tide_rect){50.0f, 50.0f, 100.0f, 10.0f});
    const uint32_t inner = d.place.clip;
    TIDE_REQUIRE(inner != outer && d.clips[inner - 1u].parent == outer && d.clips[inner - 1u].rect.x == 50.0f);
    tide_draw_rect(&d, origin, unit, TIDE_COLOR_WHITE);
    TIDE_REQUIRE(d.count == 2 && d.commands[0].kind == TIDE_DRAW_CLIP && d.commands[0].clip == inner);
    tide_draw_set_clip(&d, (tide_rect){1.0f, 2.0f, 3.0f, 4.0f}); // Set: not inside the panel's
    TIDE_CHECK(d.clips[d.place.clip - 1u].parent == 0);
    tide_draw_pop(&d, row);
    tide_draw_rect(&d, origin, unit, TIDE_COLOR_WHITE);
    TIDE_CHECK(d.count == 4 && d.commands[2].kind == TIDE_DRAW_CLIP && d.commands[2].clip == outer);
    tide_draw_pop(&d, panel);
    tide_draw_rect(&d, origin, unit, TIDE_COLOR_WHITE);
    TIDE_CHECK(d.count == 6 && d.commands[4].kind == TIDE_DRAW_NO_CLIP && d.place.clip == 0);

    // Inside a transform, it's the upright rect around what its rect becomes, under the camera there is
    tide_draw_set_camera(&d, tide_f2(7.0f, 8.0f), 9.0f);
    tide_draw_set_transform(&d, tide_draw_transform_turned(tide_f2(10.0f, 0.0f), TIDE_PI_F * 0.5f));
    tide_draw_set_clip(&d, (tide_rect){0.0f, 0.0f, 4.0f, 2.0f});
    const tide_draw_clip_data *turned = &d.clips[d.place.clip - 1u];
    TIDE_CHECK(about(turned->rect.x, 8.0f) && about(turned->rect.y, 0.0f) && about(turned->rect.width, 2.0f)
               && about(turned->rect.height, 4.0f));
    TIDE_CHECK(turned->camera == d.place.camera_2d && d.cameras[turned->camera - 1u].size == 9.0f);
    tide_draw_free(&d);
}

// A setting that's set to what it was, or set and put back around nothing,
// records nothing: instances drawn one after another are still one command.
TIDE_TEST(draw_settings_never_split_a_run)
{
    tide_draw_list d = {0};
    const tide_rect arena = {0.0f, 0.0f, 10.0f, 10.0f};
    // A view that runs for each entity: the same settings around each one's mesh
    for (int entity = 0; entity < 3; entity++) {
        const uint32_t mark = tide_draw_push_clip(&d, arena);
        tide_draw_push_camera_3d(&d, tide_f3(0.0f, 0.0f, -5.0f), tide_identity_q(), 60.0f);
        tide_draw_push_blend(&d, TIDE_BLEND_ADD);
        tide_draw_push_tint(&d, (tide_color){1.0f, 0.5f, 0.25f, 1.0f});
        tide_draw_push_transform_3d(&d, tide_translate_f4x4(tide_f3(0.0f, 10.0f, 0.0f)));
        tide_draw_mesh_3d(&d, &quad, tide_translate_f4x4(tide_f3((float)entity, 0.0f, 0.0f)), NULL, TIDE_FILTER_BILINEAR);
        tide_draw_pop(&d, mark);
    }
    TIDE_REQUIRE(d.count == 3 && d.clip_count == 1 && d.camera_3d_count == 1);
    TIDE_CHECK(d.commands[0].kind == TIDE_DRAW_CAMERA_3D && d.commands[1].kind == TIDE_DRAW_CLIP);
    const tide_draw_command *run = &d.commands[2];
    TIDE_REQUIRE(run->kind == TIDE_DRAW_MESH_3D && run->mesh3.count == 3 && run->blend == TIDE_BLEND_ADD);
    TIDE_CHECK(color_is(run->color, 1.0f, 0.5f, 0.25f, 1.0f));
    // Each instance through the block's transform, on its way to the world
    TIDE_CHECK(d.matrices[run->mesh3.first + 2u].c3.x == 2.0f && d.matrices[run->mesh3.first + 2u].c3.y == 10.0f);
    TIDE_CHECK(d.saved_count == 0 && d.place.clip == 0 && d.place.camera_3d == 0);

    // Set and put back with nothing drawn: nothing recorded, and the run goes on
    const uint32_t empty = tide_draw_push_camera(&d, tide_f2(5.0f, 5.0f), 3.0f);
    tide_draw_push_clip(&d, (tide_rect){1.0f, 1.0f, 2.0f, 2.0f});
    tide_draw_push_camera_matrices(&d, tide_identity_f4x4(), tide_identity_f4x4());
    tide_draw_pop(&d, empty);
    const uint32_t again = tide_draw_push_clip(&d, arena);
    tide_draw_push_camera_3d(&d, tide_f3(0.0f, 0.0f, -5.0f), tide_identity_q(), 60.0f);
    tide_draw_push_blend(&d, TIDE_BLEND_ADD);
    tide_draw_push_tint(&d, (tide_color){1.0f, 0.5f, 0.25f, 1.0f});
    tide_draw_mesh_3d(&d, &quad, tide_identity_f4x4(), NULL, TIDE_FILTER_BILINEAR);
    tide_draw_pop(&d, again);
    TIDE_CHECK(d.count == 3 && d.commands[2].mesh3.count == 4);

    // Another tint, or another blend, is another run
    tide_draw_set_clip(&d, arena);
    tide_draw_set_camera_3d(&d, tide_f3(0.0f, 0.0f, -5.0f), tide_identity_q(), 60.0f);
    tide_draw_set_blend(&d, TIDE_BLEND_ADD);
    tide_draw_set_tint(&d, (tide_color){1.0f, 0.5f, 0.25f, 0.5f});
    tide_draw_mesh_3d(&d, &quad, tide_identity_f4x4(), NULL, TIDE_FILTER_BILINEAR);
    TIDE_CHECK(d.count == 4 && last(&d)->mesh3.count == 1);
    tide_draw_set_blend(&d, TIDE_BLEND_ALPHA);
    tide_draw_mesh_3d(&d, &quad, tide_identity_f4x4(), NULL, TIDE_FILTER_BILINEAR);
    TIDE_CHECK(d.count == 5 && last(&d)->blend == TIDE_BLEND_ALPHA);

    // The same camera set again is the camera there is
    const uint32_t cameras = d.camera_count;
    tide_draw_set_camera(&d, tide_f2(1.0f, 2.0f), 3.0f);
    tide_draw_rect(&d, origin, unit, TIDE_COLOR_WHITE);
    const uint32_t count = d.count;
    tide_draw_set_camera(&d, tide_f2(1.0f, 2.0f), 3.0f);
    tide_draw_rect(&d, origin, unit, TIDE_COLOR_WHITE);
    TIDE_CHECK(d.count == count + 1u && d.camera_count == cameras + 1u);
    tide_draw_free(&d);
}

// Where commands land belongs to their destination; how they come out is the
// same wherever they go. A block's end puts back its own destination's.
TIDE_TEST(draw_settings_of_a_destination_are_its_own)
{
    tide_draw_list d = {0};
    tide_draw_set_blend(&d, TIDE_BLEND_ADD);
    tide_draw_set_tint(&d, (tide_color){0.5f, 1.0f, 1.0f, 1.0f});
    tide_draw_set_transform(&d, tide_draw_transform_moved(tide_f2(100.0f, 0.0f)));
    tide_draw_set_camera(&d, tide_f2(1.0f, 1.0f), 4.0f);
    tide_draw_set_clip(&d, (tide_rect){0.0f, 0.0f, 8.0f, 8.0f});
    tide_draw_to(&d, 17, 64, 64);
    TIDE_CHECK(!d.place.moved && !d.place.clip && !d.place.camera_2d); // A target's own: a frame's first
    tide_draw_rect(&d, tide_f2(1.0f, 2.0f), unit, TIDE_COLOR_WHITE);
    TIDE_CHECK(last(&d)->a.x == 1.0f && last(&d)->blend == TIDE_BLEND_ADD && color_is(last(&d)->color, 0.5f, 1, 1, 1));
    const uint32_t block = tide_draw_push_clip(&d, (tide_rect){0.0f, 0.0f, 2.0f, 2.0f});
    tide_draw_push_transform(&d, tide_draw_transform_moved(tide_f2(0.0f, 50.0f)));
    tide_draw_to(&d, 0, 0, 0);
    TIDE_CHECK(d.place.clip && d.place.moved && d.place.camera_2d); // The window's, as it left them
    tide_draw_rect(&d, tide_f2(1.0f, 2.0f), unit, TIDE_COLOR_WHITE);
    TIDE_CHECK(last(&d)->a.x == 101.0f && last(&d)->a.y == 2.0f);
    const uint32_t window_clip = d.place.clip;
    tide_draw_pop(&d, block); // The target's block ends while the window is selected
    TIDE_CHECK(d.place.clip == window_clip && (d.place.moved & TIDE_DRAW_MOVED));
    tide_draw_to(&d, 17, 64, 64);
    TIDE_CHECK(!d.place.clip && !d.place.moved);
    tide_draw_free(&d);
}

// A 2D transform is recorded into what it moves: shapes stay shapes.
TIDE_TEST(draw_transforms_move_shapes)
{
    tide_draw_list d = {0};
    const tide_color red = TIDE_COLOR_RED;
    // Moved and scaled each way: rects, their outlines and lines fill whole pixels still
    tide_draw_set_transform(&d, tide_draw_transform_trs(tide_f2(10.0f, 20.0f), 0.0f, tide_f2(2.0f, 3.0f)));
    tide_draw_rect(&d, tide_f2(1.0f, 1.0f), tide_f2(4.0f, 2.0f), red);
    TIDE_CHECK(last(&d)->kind == TIDE_DRAW_RECT && last(&d)->a.x == 12.0f && last(&d)->a.y == 23.0f);
    TIDE_CHECK(last(&d)->b.x == 8.0f && last(&d)->b.y == 6.0f);
    tide_draw_wire_rect(&d, tide_f2(1.0f, 1.0f), tide_f2(4.0f, 2.0f), red);
    TIDE_CHECK(last(&d)->kind == TIDE_DRAW_WIRE_RECT && last(&d)->b.y == 6.0f);
    tide_draw_line(&d, tide_f2(0.0f, 0.0f), tide_f2(1.0f, 1.0f), red);
    TIDE_CHECK(last(&d)->kind == TIDE_DRAW_LINE && last(&d)->a.x == 10.0f && last(&d)->b.x == 12.0f && last(&d)->b.y == 23.0f);
    // ...and a circle, stretched, is the ellipse it becomes
    tide_draw_circle(&d, tide_f2(0.0f, 0.0f), 5.0f, red);
    TIDE_REQUIRE(last(&d)->kind == TIDE_DRAW_SHAPE);
    TIDE_CHECK(last(&d)->b.x == 20.0f && last(&d)->b.y == 30.0f);
    TIDE_CHECK((d.shapes[last(&d)->shape].form & TIDE_SHAPE_FORMS) == TIDE_SHAPE_ELLIPSE);

    // As much each way: circles stay circles, and text is bigger text
    tide_draw_set_transform(&d, tide_draw_transform_scaled(tide_f2(10.0f, 20.0f), 0.0f, 2.0f));
    tide_draw_circle(&d, tide_f2(1.0f, 1.0f), 5.0f, red);
    TIDE_CHECK(last(&d)->kind == TIDE_DRAW_CIRCLE && last(&d)->a.x == 12.0f && last(&d)->b.x == 10.0f);
    tide_draw_text(&d, "big", tide_f2(1.0f, 1.0f), 8.0f, red);
    TIDE_CHECK(last(&d)->kind == TIDE_DRAW_TEXT && last(&d)->b.x == 16.0f && last(&d)->words.turned == 0);
    TIDE_CHECK(last(&d)->a.x == 12.0f && last(&d)->a.y == 22.0f);
    tide_draw_round_rect(&d, tide_f2(0.0f, 0.0f), tide_f2(8.0f, 4.0f), 1.0f, red);
    TIDE_CHECK(last(&d)->b.x == 16.0f && d.shapes[last(&d)->shape].radius == 2.0f);

    // Turned a quarter: a rect is the smooth box it also is, its outline a pixel wide; a circle is a circle
    tide_draw_set_transform(&d, tide_draw_transform_turned(tide_f2(10.0f, 0.0f), TIDE_PI_F * 0.5f));
    tide_draw_rect(&d, tide_f2(2.0f, 0.0f), tide_f2(4.0f, 2.0f), red);
    TIDE_REQUIRE(last(&d)->kind == TIDE_DRAW_SHAPE);
    const tide_draw_shape_data *box = &d.shapes[last(&d)->shape];
    TIDE_CHECK(about(last(&d)->a.x, 10.0f) && about(last(&d)->a.y, 2.0f) && about(last(&d)->b.x, 4.0f));
    TIDE_CHECK(about(box->turn.x, 0.0f) && about(box->turn.y, 1.0f) && box->form == TIDE_SHAPE_BOX && box->outline == 0.0f);
    tide_draw_wire_rect(&d, tide_f2(2.0f, 0.0f), tide_f2(4.0f, 2.0f), red);
    const tide_draw_shape_data *wire = &d.shapes[last(&d)->shape];
    TIDE_CHECK((wire->form & TIDE_SHAPE_HAIRLINE) && color_is(last(&d)->color, 0, 0, 0, 0) && color_is(wire->outline_color, 1, 0, 0, 1));
    tide_draw_circle(&d, tide_f2(2.0f, 0.0f), 3.0f, red);
    TIDE_CHECK(last(&d)->kind == TIDE_DRAW_CIRCLE && about(last(&d)->a.x, 10.0f) && about(last(&d)->a.y, 2.0f)
               && about(last(&d)->b.x, 3.0f));
    // ...and text is turned text, laid out at the size it was given
    tide_draw_text(&d, "turned", tide_f2(2.0f, 0.0f), 8.0f, red);
    TIDE_REQUIRE(last(&d)->kind == TIDE_DRAW_TEXT && last(&d)->words.turned == d.turn_count && d.turn_count == 1);
    TIDE_CHECK(last(&d)->b.x == 8.0f && about(last(&d)->a.x, 10.0f) && about(last(&d)->a.y, 2.0f));
    TIDE_CHECK(about(d.turns[0].c0.y, 1.0f) && about(d.turns[0].c1.x, -1.0f));
    // A shape that's turned already is turned further
    tide_draw_rect_rotated(&d, origin, tide_f2(4.0f, 2.0f), TIDE_PI_F * 0.5f, red);
    TIDE_CHECK(about(d.shapes[last(&d)->shape].turn.x, -1.0f) && about(d.shapes[last(&d)->shape].turn.y, 0.0f));

    // Mirrored: a fill's top is where its bottom was
    tide_draw_set_transform(&d, tide_draw_transform_trs(origin, 0.0f, tide_f2(1.0f, -1.0f)));
    tide_shape fade = TIDE_SHAPE_WHITE;
    fade.size = tide_f2(4.0f, 4.0f);
    fade.color = TIDE_COLOR_RED;
    fade.color2 = TIDE_COLOR_BLUE;
    tide_draw_shape(&d, fade);
    TIDE_CHECK(color_is(last(&d)->color, 0, 0, 1, 1) && color_is(d.shapes[last(&d)->shape].color2, 1, 0, 0, 1));

    // A matrix that skews: a circle is still an ellipse, the one it becomes
    tide_draw_set_transform(&d, tide_draw_transform_matrix((tide_float3x3){{2.0f, 0.0f, 0.0f}, {2.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}));
    tide_draw_circle(&d, origin, 1.0f, red);
    TIDE_REQUIRE(last(&d)->kind == TIDE_DRAW_SHAPE);
    // Its area is the circle's times the matrix's: pi * a * b = pi * 2
    TIDE_CHECK(about(last(&d)->b.x * last(&d)->b.y * 0.25f, 2.0f) && last(&d)->b.x > last(&d)->b.y);

    // Meshes go through it as they are, corner by corner
    static const tide_vertex triangle[3] = {{{0.0f, 0.0f}, {0.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
                                            {{1.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
                                            {{0.0f, 1.0f}, {0.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}}};
    tide_draw_set_tint(&d, (tide_color){1.0f, 0.5f, 0.0f, 1.0f});
    const uint32_t base = tide_draw_vertices(&d, triangle, 3);
    TIDE_CHECK(d.vertices[base + 2u].position.x == 2.0f && d.vertices[base + 2u].position.y == 1.0f);
    TIDE_CHECK(color_is(d.vertices[base].color, 1.0f, 0.5f, 0.0f, 1.0f) && d.vertices[base + 1u].uv.x == 1.0f);
    tide_draw_free(&d);
}

// A tint multiplies every color a command has; clears and blits have none.
TIDE_TEST(draw_tints_multiply_colors)
{
    tide_draw_list d = {0};
    tide_draw_set_tint(&d, (tide_color){0.5f, 1.0f, 0.5f, 0.5f});
    tide_draw_clear(&d, TIDE_COLOR_WHITE);
    TIDE_CHECK(color_is(last(&d)->color, 1, 1, 1, 1));
    tide_draw_circle(&d, origin, 1.0f, TIDE_COLOR_WHITE);
    tide_draw_text(&d, "a", origin, 8.0f, (tide_color){1.0f, 1.0f, 0.0f, 1.0f});
    TIDE_CHECK(color_is(d.commands[1].color, 0.5f, 1.0f, 0.5f, 0.5f) && color_is(d.commands[2].color, 0.5f, 1.0f, 0.0f, 0.5f));
    tide_shape shape = TIDE_SHAPE_WHITE;
    shape.size = tide_f2(4.0f, 4.0f);
    shape.color2 = TIDE_COLOR_RED;
    shape.outline = 1.0f;
    tide_draw_shape(&d, shape);
    const tide_draw_shape_data *data = &d.shapes[last(&d)->shape];
    TIDE_CHECK(color_is(last(&d)->color, 0.5f, 1.0f, 0.5f, 0.5f) && color_is(data->color2, 0.5f, 0.0f, 0.0f, 0.5f));
    TIDE_CHECK(color_is(data->outline_color, 0.5f, 1.0f, 0.5f, 0.5f));

    // A shader of the game's: its run's, for its pixels; a 3D one's instances go through the 3D transform
    static const tide_shader flat = {.dims = 2, .vertex_size = 32, .instance_floats = 1};
    static const tide_shader solid = {.dims = 3, .vertex_size = 32, .instance_floats = 4};
    const float instance[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    tide_draw_set_transform(&d, tide_draw_transform_moved(tide_f2(3.0f, 4.0f)));
    tide_draw_set_transform_3d(&d, tide_translate_f4x4(tide_f3(0.0f, 0.0f, 9.0f)));
    tide_draw_set_blend(&d, TIDE_BLEND_MULTIPLY);
    const uint32_t mesh = shader_mesh(&d, 32);
    tide_draw_shaded(&d, &flat, mesh, instance, NULL, NULL, 0);
    TIDE_REQUIRE(last(&d)->kind == TIDE_DRAW_SHADED && last(&d)->blend == TIDE_BLEND_MULTIPLY);
    const tide_draw_run *run = &d.runs[last(&d)->shaded.run];
    TIDE_CHECK(color_is(run->tint, 0.5f, 1.0f, 0.5f, 0.5f) && run->moved && run->transform.offset.x == 3.0f);
    TIDE_CHECK(memcmp(d.instances + run->first, instance, sizeof instance) == 0); // A 2D shader's instance is its own
    const tide_float4x4 placed = tide_translate_f4x4(tide_f3(1.0f, 0.0f, 0.0f));
    tide_draw_shaded(&d, &solid, mesh, &placed, NULL, NULL, 0);
    const tide_draw_run *deep = &d.runs[last(&d)->shaded.run];
    tide_float4x4 recorded;
    memcpy(&recorded, d.instances + deep->first, sizeof recorded);
    TIDE_CHECK(!deep->moved && recorded.c3.x == 1.0f && recorded.c3.z == 9.0f);
    // The same again is more of the run; another tint is another
    tide_draw_shaded(&d, &solid, mesh, &placed, NULL, NULL, 0);
    TIDE_CHECK(d.runs[last(&d)->shaded.run].count == 2);
    tide_draw_set_tint(&d, TIDE_COLOR_RED);
    tide_draw_shaded(&d, &solid, mesh, &placed, NULL, NULL, 0);
    TIDE_CHECK(d.runs[last(&d)->shaded.run].count == 1);

    // A blit is the whole destination as its frag writes it, blended as it's asked to be
    static const tide_shader pass = {.dims = 2, .vertex_size = sizeof(tide_vertex)};
    tide_draw_blit(&d, &pass, TIDE_BLEND_REPLACE, NULL, NULL, NULL, 0);
    TIDE_REQUIRE(last(&d)->kind == TIDE_DRAW_BLIT && last(&d)->blend == TIDE_BLEND_REPLACE);
    TIDE_CHECK(color_is(d.runs[last(&d)->shaded.run].tint, 1, 1, 1, 1) && !d.runs[last(&d)->shaded.run].moved);
    tide_draw_blit(&d, &pass, TIDE_BLEND_ADD, NULL, NULL, NULL, 0);
    TIDE_CHECK(last(&d)->blend == TIDE_BLEND_ADD);
    tide_draw_free(&d);
}

// Appended, a list's commands are as it recorded them: nothing the list they
// join has set changes them, and they leave none of their own behind but
// their cameras and clips.
TIDE_TEST(draw_settings_stay_with_their_list)
{
    tide_draw_list world = {0}, over = {0};
    tide_draw_set_blend(&over, TIDE_BLEND_ADD);
    tide_draw_set_depth(&over, TIDE_DEPTH_READ);
    tide_draw_set_tint(&over, (tide_color){1.0f, 1.0f, 0.0f, 1.0f});
    tide_draw_set_screen(&over);
    tide_draw_push_clip(&over, (tide_rect){0.0f, 0.0f, 10.0f, 10.0f});
    tide_draw_push_clip(&over, (tide_rect){5.0f, 5.0f, 10.0f, 10.0f});
    tide_draw_set_transform(&over, tide_draw_transform_turned(tide_f2(1.0f, 0.0f), 1.0f));
    tide_draw_rect(&over, tide_f2(2.0f, 0.0f), tide_f2(4.0f, 2.0f), TIDE_COLOR_WHITE);
    tide_draw_text(&over, "over", origin, 8.0f, TIDE_COLOR_WHITE);
    tide_draw_mesh_3d(&over, &quad, tide_identity_f4x4(), NULL, TIDE_FILTER_BILINEAR);

    tide_draw_set_blend(&world, TIDE_BLEND_MULTIPLY);
    tide_draw_set_depth(&world, TIDE_DEPTH_NONE);
    tide_draw_set_tint(&world, TIDE_COLOR_RED);
    tide_draw_set_transform(&world, tide_draw_transform_scaled(tide_f2(100.0f, 100.0f), 0.0f, 3.0f));
    tide_draw_set_transform_3d(&world, tide_translate_f4x4(tide_f3(50.0f, 0.0f, 0.0f)));
    tide_draw_set_clip(&world, (tide_rect){0.0f, 0.0f, 1.0f, 1.0f});
    tide_draw_rect(&world, origin, unit, TIDE_COLOR_WHITE);
    const uint32_t before = world.count;
    tide_draw_append(&world, &over);
    TIDE_REQUIRE(world.count == before + over.count);
    for (uint32_t i = 0; i < over.count; i++) {
        const tide_draw_command *theirs = &over.commands[i], *ours = &world.commands[before + i];
        TIDE_CHECK(ours->kind == theirs->kind && ours->blend == theirs->blend && ours->depth == theirs->depth);
        if (theirs->kind == TIDE_DRAW_SHAPE || theirs->kind == TIDE_DRAW_TEXT) {
            TIDE_CHECK(ours->a.x == theirs->a.x && ours->a.y == theirs->a.y && ours->b.x == theirs->b.x);
            TIDE_CHECK(color_is(ours->color, 1, 1, 0, 1));
        }
        if (theirs->kind == TIDE_DRAW_TEXT) {
            TIDE_REQUIRE(ours->words.turned && ours->words.turned <= world.turn_count);
            TIDE_CHECK(memcmp(&world.turns[ours->words.turned - 1u], &over.turns[theirs->words.turned - 1u], sizeof(tide_float2x2)) == 0);
        }
        if (theirs->kind == TIDE_DRAW_MESH_3D) {
            TIDE_CHECK(color_is(ours->color, 1, 1, 0, 1) && world.matrices[ours->mesh3.first].c3.x == 0.0f);
            TIDE_CHECK(ours->depth == TIDE_DEPTH_READ);
        }
        if (theirs->kind == TIDE_DRAW_CLIP) { // The clip it was made in comes with it
            const tide_draw_clip_data *clip = &world.clips[ours->clip - 1u];
            TIDE_REQUIRE(clip->parent && clip->parent <= world.clip_count);
            TIDE_CHECK(clip->rect.x == 5.0f && world.clips[clip->parent - 1u].rect.width == 10.0f);
        }
    }
    // What the appended list drew with isn't left set, but its cameras and its clip
    TIDE_CHECK(world.blend == TIDE_BLEND_ALPHA && !world.tinted && !world.place.moved);
    TIDE_CHECK(world.depth == TIDE_DEPTH_READ_WRITE);
    TIDE_CHECK(world.place.clip && world.cameras[world.place.camera_2d - 1u].screen == 1);
    tide_draw_free(&over);
    tide_draw_free(&world);
}

// What a 3D mesh does with depth is a setting like how it blends: its command
// carries it, a block puts back what it found, and it never comes between
// draws that would go at once. Nothing else has depth, whatever is set.
TIDE_TEST(draw_depth_is_the_3d_meshes_own)
{
    tide_draw_list d = {0};
    tide_draw_mesh_3d(&d, &quad, tide_identity_f4x4(), NULL, TIDE_FILTER_BILINEAR);
    TIDE_REQUIRE(d.count == 1 && last(&d)->kind == TIDE_DRAW_MESH_3D);
    TIDE_CHECK(last(&d)->depth == TIDE_DEPTH_READ_WRITE && d.depth == TIDE_DEPTH_READ_WRITE); // As it always was

    // A block puts back what it found, a Set inside it or not
    tide_draw_set_depth(&d, TIDE_DEPTH_NONE);
    const uint32_t read = tide_draw_push_depth(&d, TIDE_DEPTH_READ);
    tide_draw_mesh_3d(&d, &quad, tide_identity_f4x4(), NULL, TIDE_FILTER_BILINEAR);
    TIDE_CHECK(d.count == 2 && last(&d)->depth == TIDE_DEPTH_READ);
    tide_draw_set_depth(&d, TIDE_DEPTH_READ_WRITE); // Set inside the block: for the rest of it
    tide_draw_mesh_3d(&d, &quad, tide_identity_f4x4(), NULL, TIDE_FILTER_BILINEAR);
    TIDE_CHECK(d.count == 3 && last(&d)->depth == TIDE_DEPTH_READ_WRITE);
    tide_draw_pop(&d, read);
    TIDE_CHECK(d.depth == TIDE_DEPTH_NONE && d.saved_count == 0);
    tide_draw_mesh_3d(&d, &quad, tide_identity_f4x4(), NULL, TIDE_FILTER_BILINEAR);
    TIDE_CHECK(d.count == 4 && last(&d)->depth == TIDE_DEPTH_NONE);
    // Left early from inside two: the outer mark puts back what the outer found, the blend with it
    const uint32_t outer = tide_draw_push_depth(&d, TIDE_DEPTH_READ);
    tide_draw_push_blend(&d, TIDE_BLEND_ADD);
    tide_draw_push_depth(&d, TIDE_DEPTH_READ_WRITE);
    tide_draw_pop(&d, outer);
    TIDE_CHECK(d.depth == TIDE_DEPTH_NONE && d.blend == TIDE_BLEND_ALPHA && d.saved_count == 0);
    tide_draw_unset_depth(&d);
    TIDE_CHECK(d.depth == TIDE_DEPTH_READ_WRITE);
    tide_draw_set_depth(&d, (tide_depth)77); // None of them: as a frame starts
    TIDE_CHECK(d.depth == TIDE_DEPTH_READ_WRITE);

    // The same setting around each of a view's entities' meshes: still one run
    tide_draw_reset(&d);
    for (int entity = 0; entity < 3; entity++) {
        const uint32_t mark = tide_draw_push_depth(&d, TIDE_DEPTH_READ);
        tide_draw_mesh_3d(&d, &quad, tide_translate_f4x4(tide_f3((float)entity, 0.0f, 0.0f)), NULL, TIDE_FILTER_BILINEAR);
        tide_draw_pop(&d, mark);
    }
    TIDE_REQUIRE(d.count == 1 && last(&d)->mesh3.count == 3 && last(&d)->depth == TIDE_DEPTH_READ);
    // Set and put back around nothing: the run goes on
    tide_draw_pop(&d, tide_draw_push_depth(&d, TIDE_DEPTH_NONE));
    tide_draw_set_depth(&d, TIDE_DEPTH_READ);
    tide_draw_mesh_3d(&d, &quad, tide_identity_f4x4(), NULL, TIDE_FILTER_BILINEAR);
    TIDE_CHECK(d.count == 1 && last(&d)->mesh3.count == 4);
    // Another depth is another run
    tide_draw_set_depth(&d, TIDE_DEPTH_READ_WRITE);
    tide_draw_mesh_3d(&d, &quad, tide_identity_f4x4(), NULL, TIDE_FILTER_BILINEAR);
    TIDE_CHECK(d.count == 2 && last(&d)->mesh3.count == 1 && last(&d)->depth == TIDE_DEPTH_READ_WRITE);

    // A mesh through a 3D shader of the game's has it too: the same again is more of its run, another is another
    static const tide_shader solid = {.dims = 3, .vertex_size = 32, .instance_floats = 4};
    static const tide_shader flat = {.dims = 2, .vertex_size = 32, .instance_floats = 1};
    static const tide_shader pass = {.dims = 2, .vertex_size = sizeof(tide_vertex)};
    const tide_float4x4 placed = tide_translate_f4x4(tide_f3(1.0f, 0.0f, 0.0f));
    const float instance[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    const uint32_t mesh = shader_mesh(&d, 32);
    tide_draw_set_depth(&d, TIDE_DEPTH_READ);
    tide_draw_shaded(&d, &solid, mesh, &placed, NULL, NULL, 0);
    tide_draw_pop(&d, tide_draw_push_depth(&d, TIDE_DEPTH_NONE));
    tide_draw_shaded(&d, &solid, mesh, &placed, NULL, NULL, 0);
    TIDE_REQUIRE(last(&d)->kind == TIDE_DRAW_SHADED && last(&d)->depth == TIDE_DEPTH_READ);
    TIDE_CHECK(d.runs[last(&d)->shaded.run].count == 2);
    tide_draw_set_depth(&d, TIDE_DEPTH_NONE);
    tide_draw_shaded(&d, &solid, mesh, &placed, NULL, NULL, 0);
    TIDE_CHECK(last(&d)->depth == TIDE_DEPTH_NONE && d.runs[last(&d)->shaded.run].count == 1);

    // Nothing else has depth: a 2D command says none whatever is set, so the setting never comes between two
    const uint32_t count = d.count;
    tide_draw_shaded(&d, &flat, mesh, instance, NULL, NULL, 0);
    tide_draw_set_depth(&d, TIDE_DEPTH_READ);
    tide_draw_shaded(&d, &flat, mesh, instance, NULL, NULL, 0);
    TIDE_CHECK(d.count == count + 1u && last(&d)->depth == 0 && d.runs[last(&d)->shaded.run].count == 2);
    tide_draw_rect(&d, origin, unit, TIDE_COLOR_WHITE);
    TIDE_CHECK(last(&d)->kind == TIDE_DRAW_RECT && last(&d)->depth == 0);
    tide_draw_text(&d, "a", origin, 8.0f, TIDE_COLOR_WHITE);
    TIDE_CHECK(last(&d)->kind == TIDE_DRAW_TEXT && last(&d)->depth == 0);
    tide_draw_blit(&d, &pass, TIDE_BLEND_REPLACE, NULL, NULL, NULL, 0);
    TIDE_CHECK(last(&d)->kind == TIDE_DRAW_BLIT && last(&d)->depth == 0);
    tide_draw_clear(&d, TIDE_COLOR_BLACK);
    TIDE_CHECK(last(&d)->kind == TIDE_DRAW_CLEAR && last(&d)->depth == 0);

    // It's the same wherever a mesh is drawn: a target's meshes have what the window's do
    tide_draw_to(&d, 17, 64, 64);
    tide_draw_mesh_3d(&d, &quad, tide_identity_f4x4(), NULL, TIDE_FILTER_BILINEAR);
    TIDE_CHECK(last(&d)->kind == TIDE_DRAW_MESH_3D && last(&d)->depth == TIDE_DEPTH_READ);

    // The GUI's and a host's own start from the defaults
    tide_draw_defaults(&d);
    TIDE_CHECK(d.depth == TIDE_DEPTH_READ_WRITE && d.target == 0);
    tide_draw_free(&d);
}
