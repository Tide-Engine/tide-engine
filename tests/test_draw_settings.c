// The draw list's settings: how what's drawn after them comes out (blend,
// tint) and what belongs to where it's drawn (transforms, clip, cameras, what
// 3D meshes do with depth and which side of their triangles draws), in their
// two forms: set for the commands to come, or pushed around some and popped.

#include <string.h>

#include "tide/draw.h"
#include "tide_test.h"
#include "draw_test.h"

static bool about(const float a, const float b)
{
    return a - b < 1e-4f && b - a < 1e-4f;
}

static bool color_is(const tide_color c, const float r, const float g, const float b, const float a)
{
    return tide_test_color_is(c, r, g, b, a);
}

// The list as it's drawn, each plain shape a command of its own
static tide_draw_list shown(const tide_draw_list *d)
{
    return tide_test_flat(d);
}

static const tide_draw_command *last(const tide_draw_list *d)
{
    const tide_draw_list flat = shown(d);
    return &flat.commands[flat.count - 1u];
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

TIDE_TEST(draw_settings_start_over_each_frame)
{
    tide_draw_list d = {0};
    tide_draw_rect(&d, origin, unit, TIDE_COLOR_RED);
    TIDE_REQUIRE(shown(&d).count == 1 && shown(&d).commands[0].kind == TIDE_DRAW_RECT);
    TIDE_CHECK(shown(&d).commands[0].blend == TIDE_BLEND_ALPHA && color_is(shown(&d).commands[0].color, 1, 0, 0, 1));

    tide_draw_set_blend(&d, TIDE_BLEND_ADD);
    tide_draw_set_depth(&d, TIDE_DEPTH_NONE);
    tide_draw_set_tint(&d, (tide_color){0.5f, 0.5f, 0.5f, 0.5f});
    tide_draw_set_transform(&d, tide_draw_transform_trs(tide_f2(10.0f, 20.0f), 0.0f, tide_f2(2.0f, 2.0f)));
    tide_draw_set_transform_3d(&d, tide_translate_f4x4(tide_f3(1.0f, 2.0f, 3.0f)));
    tide_draw_set_clip(&d, (tide_rect){0.0f, 0.0f, 4.0f, 4.0f});
    tide_draw_set_screen(&d);
    tide_draw_set_camera_3d(&d, tide_f3(0.0f, 0.0f, -5.0f), tide_identity_q(), 90.0f);
    tide_draw_push_tint(&d, TIDE_COLOR_BLUE);
    tide_draw_rect(&d, origin, unit, TIDE_COLOR_WHITE);
    TIDE_CHECK(last(&d)->blend == TIDE_BLEND_ADD && shown(&d).count > 2);

    tide_draw_reset(&d);
    TIDE_CHECK(d.place.blend == TIDE_BLEND_ALPHA && !d.place.tinted && !d.place.moved && !d.place.clip && !d.saved_count);
    TIDE_CHECK(!d.place.camera_2d && !d.place.camera_3d && d.place.depth == TIDE_DEPTH_READ_WRITE);
    tide_draw_rect(&d, origin, unit, TIDE_COLOR_RED);
    TIDE_REQUIRE(shown(&d).count == 1 && shown(&d).commands[0].kind == TIDE_DRAW_RECT && shown(&d).commands[0].blend == TIDE_BLEND_ALPHA);
    TIDE_CHECK(color_is(shown(&d).commands[0].color, 1, 0, 0, 1) && shown(&d).commands[0].a.x == 0.0f && shown(&d).commands[0].b.x == 1.0f);
    tide_draw_free(&d);
}

TIDE_TEST(draw_settings_blocks_put_back_what_they_found)
{
    tide_draw_list d = {0};
    tide_draw_set_blend(&d, TIDE_BLEND_MULTIPLY);
    const uint32_t add = tide_draw_push_blend(&d, TIDE_BLEND_ADD);
    tide_draw_rect(&d, origin, unit, TIDE_COLOR_WHITE);
    TIDE_CHECK(last(&d)->blend == TIDE_BLEND_ADD);
    tide_draw_set_blend(&d, TIDE_BLEND_REPLACE);
    tide_draw_rect(&d, origin, unit, TIDE_COLOR_WHITE);
    TIDE_CHECK(last(&d)->blend == TIDE_BLEND_REPLACE);
    tide_draw_pop(&d, add);
    tide_draw_rect(&d, origin, unit, TIDE_COLOR_WHITE);
    TIDE_CHECK(last(&d)->blend == TIDE_BLEND_MULTIPLY && d.saved_count == 0);
    tide_draw_unset_blend(&d);
    TIDE_CHECK(d.place.blend == TIDE_BLEND_ALPHA);

    tide_draw_set_tint(&d, (tide_color){1.0f, 0.5f, 1.0f, 1.0f});
    const uint32_t outer = tide_draw_push_tint(&d, (tide_color){0.5f, 1.0f, 1.0f, 0.5f});
    const uint32_t inner = tide_draw_push_tint(&d, (tide_color){1.0f, 1.0f, 0.5f, 0.5f});
    TIDE_CHECK(inner == outer + 1u);
    tide_draw_rect(&d, origin, unit, TIDE_COLOR_WHITE);
    TIDE_CHECK(color_is(last(&d)->color, 0.5f, 0.5f, 0.5f, 0.25f));
    tide_draw_pop(&d, inner);
    tide_draw_rect(&d, origin, unit, TIDE_COLOR_WHITE);
    TIDE_CHECK(color_is(last(&d)->color, 0.5f, 0.5f, 1.0f, 0.5f));
    tide_draw_push_tint(&d, TIDE_COLOR_RED);
    tide_draw_set_tint(&d, TIDE_COLOR_GREEN);
    tide_draw_pop(&d, outer);
    tide_draw_rect(&d, origin, unit, TIDE_COLOR_WHITE);
    TIDE_CHECK(color_is(last(&d)->color, 1.0f, 0.5f, 1.0f, 1.0f) && d.saved_count == 0);
    tide_draw_unset_tint(&d);
    tide_draw_rect(&d, origin, unit, TIDE_COLOR_WHITE);
    TIDE_CHECK(color_is(last(&d)->color, 1, 1, 1, 1) && !d.altered);

    const uint32_t only = tide_draw_push_tint(&d, TIDE_COLOR_RED);
    tide_draw_pop(&d, only);
    TIDE_CHECK(!d.place.tinted);

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
    tide_draw_set_clip(&d, (tide_rect){1.0f, 2.0f, 3.0f, 4.0f});
    TIDE_CHECK(d.clips[d.place.clip - 1u].parent == 0);
    tide_draw_pop(&d, row);
    tide_draw_rect(&d, origin, unit, TIDE_COLOR_WHITE);
    TIDE_CHECK(d.count == 4 && d.commands[2].kind == TIDE_DRAW_CLIP && d.commands[2].clip == outer);
    tide_draw_pop(&d, panel);
    tide_draw_rect(&d, origin, unit, TIDE_COLOR_WHITE);
    TIDE_CHECK(d.count == 6 && d.commands[4].kind == TIDE_DRAW_NO_CLIP && d.place.clip == 0);

    tide_draw_set_camera(&d, tide_f2(7.0f, 8.0f), 9.0f);
    tide_draw_set_transform(&d, tide_draw_transform_turned(tide_f2(10.0f, 0.0f), TIDE_PI_F * 0.5f));
    tide_draw_set_clip(&d, (tide_rect){0.0f, 0.0f, 4.0f, 2.0f});
    const tide_draw_clip_data *turned = &d.clips[d.place.clip - 1u];
    TIDE_CHECK(about(turned->rect.x, 8.0f) && about(turned->rect.y, 0.0f) && about(turned->rect.width, 2.0f)
               && about(turned->rect.height, 4.0f));
    TIDE_CHECK(turned->camera == d.place.camera_2d && d.cameras[turned->camera - 1u].size == 9.0f);
    tide_draw_free(&d);
}

TIDE_TEST(draw_settings_never_split_a_run)
{
    tide_draw_list d = {0};
    const tide_rect arena = {0.0f, 0.0f, 10.0f, 10.0f};
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
    TIDE_CHECK(d.matrices[run->mesh3.first + 2u].c3.x == 2.0f && d.matrices[run->mesh3.first + 2u].c3.y == 10.0f);
    TIDE_CHECK(d.saved_count == 0 && d.place.clip == 0 && d.place.camera_3d == 0);

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

    tide_draw_set_clip(&d, arena);
    tide_draw_set_camera_3d(&d, tide_f3(0.0f, 0.0f, -5.0f), tide_identity_q(), 60.0f);
    tide_draw_set_blend(&d, TIDE_BLEND_ADD);
    tide_draw_set_tint(&d, (tide_color){1.0f, 0.5f, 0.25f, 0.5f});
    tide_draw_mesh_3d(&d, &quad, tide_identity_f4x4(), NULL, TIDE_FILTER_BILINEAR);
    TIDE_CHECK(d.count == 4 && last(&d)->mesh3.count == 1);
    tide_draw_set_blend(&d, TIDE_BLEND_ALPHA);
    tide_draw_mesh_3d(&d, &quad, tide_identity_f4x4(), NULL, TIDE_FILTER_BILINEAR);
    TIDE_CHECK(d.count == 5 && last(&d)->blend == TIDE_BLEND_ALPHA);

    const uint32_t cameras = d.camera_count;
    tide_draw_set_camera(&d, tide_f2(1.0f, 2.0f), 3.0f);
    tide_draw_rect(&d, origin, unit, TIDE_COLOR_WHITE);
    const uint32_t count = shown(&d).count;
    tide_draw_set_camera(&d, tide_f2(1.0f, 2.0f), 3.0f);
    tide_draw_rect(&d, origin, unit, TIDE_COLOR_WHITE);
    TIDE_CHECK(shown(&d).count == count + 1u && d.camera_count == cameras + 1u);
    tide_draw_free(&d);
}

TIDE_TEST(draw_settings_of_a_destination_are_its_own)
{
    tide_draw_list d = {0};
    tide_draw_set_blend(&d, TIDE_BLEND_ADD);
    tide_draw_set_tint(&d, (tide_color){0.5f, 1.0f, 1.0f, 1.0f});
    tide_draw_set_transform(&d, tide_draw_transform_moved(tide_f2(100.0f, 0.0f)));
    tide_draw_set_camera(&d, tide_f2(1.0f, 1.0f), 4.0f);
    tide_draw_set_clip(&d, (tide_rect){0.0f, 0.0f, 8.0f, 8.0f});
    tide_draw_to(&d, 17, 64, 64);
    TIDE_CHECK(!d.place.moved && !d.place.clip && !d.place.camera_2d && !d.place.tinted && d.place.blend == TIDE_BLEND_ALPHA);
    tide_draw_rect(&d, tide_f2(1.0f, 2.0f), unit, TIDE_COLOR_WHITE);
    TIDE_CHECK(last(&d)->a.x == 1.0f && last(&d)->blend == TIDE_BLEND_ALPHA && color_is(last(&d)->color, 1, 1, 1, 1));
    tide_draw_set_blend(&d, TIDE_BLEND_MULTIPLY);
    tide_draw_set_tint(&d, (tide_color){1.0f, 0.0f, 0.0f, 1.0f});
    tide_draw_rect(&d, tide_f2(1.0f, 2.0f), unit, TIDE_COLOR_WHITE);
    TIDE_CHECK(last(&d)->blend == TIDE_BLEND_MULTIPLY && color_is(last(&d)->color, 1, 0, 0, 1));
    tide_draw_to(&d, 0, 0, 0);
    tide_draw_rect(&d, tide_f2(1.0f, 2.0f), unit, TIDE_COLOR_WHITE);
    TIDE_CHECK(last(&d)->a.x == 101.0f && last(&d)->blend == TIDE_BLEND_ADD && color_is(last(&d)->color, 0.5f, 1, 1, 1));
    tide_draw_to(&d, 17, 64, 64);
    const uint32_t tinted = tide_draw_push_tint(&d, (tide_color){1.0f, 1.0f, 0.0f, 1.0f});
    tide_draw_push_blend(&d, TIDE_BLEND_REPLACE);
    tide_draw_rect(&d, tide_f2(1.0f, 2.0f), unit, TIDE_COLOR_WHITE);
    TIDE_CHECK(last(&d)->blend == TIDE_BLEND_REPLACE && color_is(last(&d)->color, 1, 0, 0, 1));
    tide_draw_to(&d, 0, 0, 0);
    tide_draw_pop(&d, tinted);
    tide_draw_rect(&d, tide_f2(1.0f, 2.0f), unit, TIDE_COLOR_WHITE);
    TIDE_CHECK(last(&d)->blend == TIDE_BLEND_ADD && color_is(last(&d)->color, 0.5f, 1, 1, 1));
    tide_draw_to(&d, 17, 64, 64);
    tide_draw_rect(&d, tide_f2(1.0f, 2.0f), unit, TIDE_COLOR_WHITE);
    TIDE_CHECK(last(&d)->blend == TIDE_BLEND_MULTIPLY && color_is(last(&d)->color, 1, 0, 0, 1));
    tide_draw_to(&d, 0, 0, 0);
    tide_draw_set_blend(&d, TIDE_BLEND_ADD);
    tide_draw_set_tint(&d, (tide_color){0.5f, 1.0f, 1.0f, 1.0f});
    tide_draw_to(&d, 17, 64, 64);
    tide_draw_unset_blend(&d);
    tide_draw_unset_tint(&d);
    const uint32_t block = tide_draw_push_clip(&d, (tide_rect){0.0f, 0.0f, 2.0f, 2.0f});
    tide_draw_push_transform(&d, tide_draw_transform_moved(tide_f2(0.0f, 50.0f)));
    tide_draw_to(&d, 0, 0, 0);
    TIDE_CHECK(d.place.clip && d.place.moved && d.place.camera_2d);
    tide_draw_rect(&d, tide_f2(1.0f, 2.0f), unit, TIDE_COLOR_WHITE);
    TIDE_CHECK(last(&d)->a.x == 101.0f && last(&d)->a.y == 2.0f);
    const uint32_t window_clip = d.place.clip;
    tide_draw_pop(&d, block);
    TIDE_CHECK(d.place.clip == window_clip && (d.place.moved & TIDE_DRAW_MOVED));
    tide_draw_to(&d, 17, 64, 64);
    TIDE_CHECK(!d.place.clip && !d.place.moved);
    tide_draw_free(&d);
}

TIDE_TEST(draw_transforms_move_shapes)
{
    tide_draw_list d = {0};
    const tide_color red = TIDE_COLOR_RED;
    tide_draw_set_transform(&d, tide_draw_transform_trs(tide_f2(10.0f, 20.0f), 0.0f, tide_f2(2.0f, 3.0f)));
    tide_draw_rect(&d, tide_f2(1.0f, 1.0f), tide_f2(4.0f, 2.0f), red);
    TIDE_CHECK(last(&d)->kind == TIDE_DRAW_RECT && last(&d)->a.x == 12.0f && last(&d)->a.y == 23.0f);
    TIDE_CHECK(last(&d)->b.x == 8.0f && last(&d)->b.y == 6.0f);
    tide_draw_wire_rect(&d, tide_f2(1.0f, 1.0f), tide_f2(4.0f, 2.0f), red);
    TIDE_CHECK(last(&d)->kind == TIDE_DRAW_WIRE_RECT && last(&d)->b.y == 6.0f);
    tide_draw_line(&d, tide_f2(0.0f, 0.0f), tide_f2(1.0f, 1.0f), red);
    TIDE_CHECK(last(&d)->kind == TIDE_DRAW_LINE && last(&d)->a.x == 10.0f && last(&d)->b.x == 12.0f && last(&d)->b.y == 23.0f);
    tide_draw_circle(&d, tide_f2(0.0f, 0.0f), 5.0f, red);
    TIDE_REQUIRE(last(&d)->kind == TIDE_DRAW_SHAPE);
    TIDE_CHECK(last(&d)->b.x == 20.0f && last(&d)->b.y == 30.0f);
    TIDE_CHECK((d.shapes[last(&d)->shape].form & TIDE_SHAPE_FORMS) == TIDE_SHAPE_ELLIPSE);

    tide_draw_set_transform(&d, tide_draw_transform_scaled(tide_f2(10.0f, 20.0f), 0.0f, 2.0f));
    tide_draw_circle(&d, tide_f2(1.0f, 1.0f), 5.0f, red);
    TIDE_CHECK(last(&d)->kind == TIDE_DRAW_CIRCLE && last(&d)->a.x == 12.0f && last(&d)->b.x == 10.0f);
    tide_draw_text(&d, "big", tide_f2(1.0f, 1.0f), 8.0f, red);
    TIDE_CHECK(last(&d)->kind == TIDE_DRAW_TEXT && last(&d)->b.x == 16.0f && last(&d)->words.turned == 0);
    TIDE_CHECK(last(&d)->a.x == 12.0f && last(&d)->a.y == 22.0f);
    tide_draw_round_rect(&d, tide_f2(0.0f, 0.0f), tide_f2(8.0f, 4.0f), 1.0f, red);
    TIDE_CHECK(last(&d)->b.x == 16.0f && d.shapes[last(&d)->shape].radius == 2.0f);

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
    tide_draw_text(&d, "turned", tide_f2(2.0f, 0.0f), 8.0f, red);
    TIDE_REQUIRE(last(&d)->kind == TIDE_DRAW_TEXT && last(&d)->words.turned == d.turn_count && d.turn_count == 1);
    TIDE_CHECK(last(&d)->b.x == 8.0f && about(last(&d)->a.x, 10.0f) && about(last(&d)->a.y, 2.0f));
    TIDE_CHECK(about(d.turns[0].c0.y, 1.0f) && about(d.turns[0].c1.x, -1.0f));
    tide_draw_rect_rotated(&d, origin, tide_f2(4.0f, 2.0f), TIDE_PI_F * 0.5f, red);
    TIDE_CHECK(about(d.shapes[last(&d)->shape].turn.x, -1.0f) && about(d.shapes[last(&d)->shape].turn.y, 0.0f));

    tide_draw_set_transform(&d, tide_draw_transform_trs(origin, 0.0f, tide_f2(1.0f, -1.0f)));
    tide_shape fade = TIDE_SHAPE_WHITE;
    fade.size = tide_f2(4.0f, 4.0f);
    fade.color = TIDE_COLOR_RED;
    fade.color2 = TIDE_COLOR_BLUE;
    tide_draw_shape(&d, fade);
    TIDE_CHECK(color_is(last(&d)->color, 0, 0, 1, 1) && color_is(d.shapes[last(&d)->shape].color2, 1, 0, 0, 1));

    tide_draw_set_transform(&d, tide_draw_transform_matrix((tide_float3x3){{2.0f, 0.0f, 0.0f}, {2.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}));
    tide_draw_circle(&d, origin, 1.0f, red);
    TIDE_REQUIRE(last(&d)->kind == TIDE_DRAW_SHAPE);
    // Its area is the circle's times the matrix's: pi * a * b = pi * 2
    TIDE_CHECK(about(last(&d)->b.x * last(&d)->b.y * 0.25f, 2.0f) && last(&d)->b.x > last(&d)->b.y);

    static const tide_vertex triangle[3] = {{{0.0f, 0.0f}, {0.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
                                            {{1.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
                                            {{0.0f, 1.0f}, {0.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}}};
    tide_draw_set_tint(&d, (tide_color){1.0f, 0.5f, 0.0f, 1.0f});
    const uint32_t base = tide_draw_vertices(&d, triangle, 3);
    TIDE_CHECK(d.vertices[base + 2u].position.x == 2.0f && d.vertices[base + 2u].position.y == 1.0f);
    TIDE_CHECK(color_is(d.vertices[base].color, 1.0f, 0.5f, 0.0f, 1.0f) && d.vertices[base + 1u].uv.x == 1.0f);
    tide_draw_free(&d);
}

TIDE_TEST(draw_tints_multiply_colors)
{
    tide_draw_list d = {0};
    tide_draw_set_tint(&d, (tide_color){0.5f, 1.0f, 0.5f, 0.5f});
    tide_draw_clear(&d, TIDE_COLOR_WHITE);
    TIDE_CHECK(color_is(last(&d)->color, 1, 1, 1, 1));
    tide_draw_circle(&d, origin, 1.0f, TIDE_COLOR_WHITE);
    tide_draw_text(&d, "a", origin, 8.0f, (tide_color){1.0f, 1.0f, 0.0f, 1.0f});
    TIDE_CHECK(color_is(shown(&d).commands[1].color, 0.5f, 1.0f, 0.5f, 0.5f) && color_is(shown(&d).commands[2].color, 0.5f, 1.0f, 0.0f, 0.5f));
    tide_shape shape = TIDE_SHAPE_WHITE;
    shape.size = tide_f2(4.0f, 4.0f);
    shape.color2 = TIDE_COLOR_RED;
    shape.outline = 1.0f;
    tide_draw_shape(&d, shape);
    const tide_draw_shape_data *data = &d.shapes[last(&d)->shape];
    TIDE_CHECK(color_is(last(&d)->color, 0.5f, 1.0f, 0.5f, 0.5f) && color_is(data->color2, 0.5f, 0.0f, 0.0f, 0.5f));
    TIDE_CHECK(color_is(data->outline_color, 0.5f, 1.0f, 0.5f, 0.5f));

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
    TIDE_CHECK(memcmp(d.instances + run->first, instance, sizeof instance) == 0);
    const tide_float4x4 placed = tide_translate_f4x4(tide_f3(1.0f, 0.0f, 0.0f));
    tide_draw_shaded(&d, &solid, mesh, &placed, NULL, NULL, 0);
    const tide_draw_run *deep = &d.runs[last(&d)->shaded.run];
    tide_float4x4 recorded;
    memcpy(&recorded, d.instances + deep->first, sizeof recorded);
    TIDE_CHECK(!deep->moved && recorded.c3.x == 1.0f && recorded.c3.z == 9.0f);
    tide_draw_shaded(&d, &solid, mesh, &placed, NULL, NULL, 0);
    TIDE_CHECK(d.runs[last(&d)->shaded.run].count == 2);
    tide_draw_set_tint(&d, TIDE_COLOR_RED);
    tide_draw_shaded(&d, &solid, mesh, &placed, NULL, NULL, 0);
    TIDE_CHECK(d.runs[last(&d)->shaded.run].count == 1);

    static const tide_shader pass = {.dims = 2, .vertex_size = sizeof(tide_vertex)};
    tide_draw_blit(&d, &pass, TIDE_BLEND_REPLACE, NULL, NULL, NULL, 0);
    TIDE_REQUIRE(last(&d)->kind == TIDE_DRAW_BLIT && last(&d)->blend == TIDE_BLEND_REPLACE);
    TIDE_CHECK(color_is(d.runs[last(&d)->shaded.run].tint, 1, 1, 1, 1) && !d.runs[last(&d)->shaded.run].moved);
    tide_draw_blit(&d, &pass, TIDE_BLEND_ADD, NULL, NULL, NULL, 0);
    TIDE_CHECK(last(&d)->blend == TIDE_BLEND_ADD);
    tide_draw_free(&d);
}

TIDE_TEST(draw_settings_stay_with_their_list)
{
    tide_draw_list world = {0}, over = {0};
    tide_draw_set_blend(&over, TIDE_BLEND_ADD);
    tide_draw_set_depth(&over, TIDE_DEPTH_READ);
    tide_draw_set_cull(&over, TIDE_CULL_FRONT);
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
    tide_draw_set_cull(&world, TIDE_CULL_NONE);
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
        TIDE_CHECK(ours->cull == theirs->cull);
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
            TIDE_CHECK(ours->depth == TIDE_DEPTH_READ && ours->cull == TIDE_CULL_FRONT);
        }
        if (theirs->kind == TIDE_DRAW_CLIP) {
            const tide_draw_clip_data *clip = &world.clips[ours->clip - 1u];
            TIDE_REQUIRE(clip->parent && clip->parent <= world.clip_count);
            TIDE_CHECK(clip->rect.x == 5.0f && world.clips[clip->parent - 1u].rect.width == 10.0f);
        }
    }
    TIDE_CHECK(world.place.blend == TIDE_BLEND_ALPHA && !world.place.tinted && !world.place.moved);
    TIDE_CHECK(world.place.depth == TIDE_DEPTH_READ_WRITE && world.place.cull == TIDE_CULL_BACK);
    TIDE_CHECK(world.place.clip && world.cameras[world.place.camera_2d - 1u].screen == 1);
    tide_draw_free(&over);
    tide_draw_free(&world);
}

TIDE_TEST(draw_depth_is_the_3d_meshes_own)
{
    tide_draw_list d = {0};
    tide_draw_mesh_3d(&d, &quad, tide_identity_f4x4(), NULL, TIDE_FILTER_BILINEAR);
    TIDE_REQUIRE(d.count == 1 && last(&d)->kind == TIDE_DRAW_MESH_3D);
    TIDE_CHECK(last(&d)->depth == TIDE_DEPTH_READ_WRITE && d.place.depth == TIDE_DEPTH_READ_WRITE);

    tide_draw_set_depth(&d, TIDE_DEPTH_NONE);
    const uint32_t read = tide_draw_push_depth(&d, TIDE_DEPTH_READ);
    tide_draw_mesh_3d(&d, &quad, tide_identity_f4x4(), NULL, TIDE_FILTER_BILINEAR);
    TIDE_CHECK(d.count == 2 && last(&d)->depth == TIDE_DEPTH_READ);
    tide_draw_set_depth(&d, TIDE_DEPTH_READ_WRITE);
    tide_draw_mesh_3d(&d, &quad, tide_identity_f4x4(), NULL, TIDE_FILTER_BILINEAR);
    TIDE_CHECK(d.count == 3 && last(&d)->depth == TIDE_DEPTH_READ_WRITE);
    tide_draw_pop(&d, read);
    TIDE_CHECK(d.place.depth == TIDE_DEPTH_NONE && d.saved_count == 0);
    tide_draw_mesh_3d(&d, &quad, tide_identity_f4x4(), NULL, TIDE_FILTER_BILINEAR);
    TIDE_CHECK(d.count == 4 && last(&d)->depth == TIDE_DEPTH_NONE);
    const uint32_t outer = tide_draw_push_depth(&d, TIDE_DEPTH_READ);
    tide_draw_push_blend(&d, TIDE_BLEND_ADD);
    tide_draw_push_depth(&d, TIDE_DEPTH_READ_WRITE);
    tide_draw_pop(&d, outer);
    TIDE_CHECK(d.place.depth == TIDE_DEPTH_NONE && d.place.blend == TIDE_BLEND_ALPHA && d.saved_count == 0);
    tide_draw_unset_depth(&d);
    TIDE_CHECK(d.place.depth == TIDE_DEPTH_READ_WRITE);
    tide_draw_set_depth(&d, (tide_depth)77);
    TIDE_CHECK(d.place.depth == TIDE_DEPTH_READ_WRITE);

    tide_draw_reset(&d);
    for (int entity = 0; entity < 3; entity++) {
        const uint32_t mark = tide_draw_push_depth(&d, TIDE_DEPTH_READ);
        tide_draw_mesh_3d(&d, &quad, tide_translate_f4x4(tide_f3((float)entity, 0.0f, 0.0f)), NULL, TIDE_FILTER_BILINEAR);
        tide_draw_pop(&d, mark);
    }
    TIDE_REQUIRE(d.count == 1 && last(&d)->mesh3.count == 3 && last(&d)->depth == TIDE_DEPTH_READ);
    tide_draw_pop(&d, tide_draw_push_depth(&d, TIDE_DEPTH_NONE));
    tide_draw_set_depth(&d, TIDE_DEPTH_READ);
    tide_draw_mesh_3d(&d, &quad, tide_identity_f4x4(), NULL, TIDE_FILTER_BILINEAR);
    TIDE_CHECK(d.count == 1 && last(&d)->mesh3.count == 4);
    tide_draw_set_depth(&d, TIDE_DEPTH_READ_WRITE);
    tide_draw_mesh_3d(&d, &quad, tide_identity_f4x4(), NULL, TIDE_FILTER_BILINEAR);
    TIDE_CHECK(d.count == 2 && last(&d)->mesh3.count == 1 && last(&d)->depth == TIDE_DEPTH_READ_WRITE);

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

    TIDE_CHECK(d.place.depth == TIDE_DEPTH_READ);
    tide_draw_to(&d, 17, 64, 64);
    TIDE_CHECK(d.place.depth == TIDE_DEPTH_READ_WRITE);
    tide_draw_set_frustum_culling(&d, false); // Inspect depth state on a mesh at the camera origin
    tide_draw_mesh_3d(&d, &quad, tide_identity_f4x4(), NULL, TIDE_FILTER_BILINEAR);
    TIDE_CHECK(last(&d)->kind == TIDE_DRAW_MESH_3D && last(&d)->depth == TIDE_DEPTH_READ_WRITE);
    const uint32_t block = tide_draw_push_depth(&d, TIDE_DEPTH_NONE);
    tide_draw_mesh_3d(&d, &quad, tide_translate_f4x4(tide_f3(5.0f, 0.0f, 0.0f)), NULL, TIDE_FILTER_BILINEAR);
    TIDE_CHECK(last(&d)->depth == TIDE_DEPTH_NONE);
    tide_draw_to(&d, 0, 0, 0);
    tide_draw_mesh_3d(&d, &quad, tide_identity_f4x4(), NULL, TIDE_FILTER_BILINEAR);
    TIDE_CHECK(last(&d)->depth == TIDE_DEPTH_READ);
    tide_draw_pop(&d, block);
    TIDE_CHECK(d.place.depth == TIDE_DEPTH_READ && d.saved_count == 0);
    tide_draw_to(&d, 17, 64, 64);
    TIDE_CHECK(d.place.depth == TIDE_DEPTH_READ_WRITE);
    tide_draw_set_depth(&d, TIDE_DEPTH_NONE);

    tide_draw_defaults(&d);
    TIDE_CHECK(d.place.depth == TIDE_DEPTH_READ_WRITE && d.target == 0);
    tide_draw_to(&d, 17, 64, 64);
    TIDE_CHECK(d.place.depth == TIDE_DEPTH_READ_WRITE);
    tide_draw_free(&d);
}

TIDE_TEST(draw_cull_is_the_3d_meshes_own)
{
    tide_draw_list d = {0};
    const tide_float4x4 same = tide_identity_f4x4();
    const tide_float4x4 mirror = tide_trs_f4x4(tide_f3(0.0f, 0.0f, 0.0f), tide_identity_q(), tide_f3(-1.0f, 1.0f, 1.0f));
    tide_draw_mesh_3d(&d, &quad, same, NULL, TIDE_FILTER_BILINEAR);
    TIDE_REQUIRE(d.count == 1 && last(&d)->kind == TIDE_DRAW_MESH_3D);
    TIDE_CHECK(last(&d)->cull == TIDE_CULL_BACK && d.place.cull == TIDE_CULL_BACK);

    tide_draw_set_cull(&d, TIDE_CULL_NONE);
    const uint32_t front = tide_draw_push_cull(&d, TIDE_CULL_FRONT);
    tide_draw_mesh_3d(&d, &quad, same, NULL, TIDE_FILTER_BILINEAR);
    TIDE_CHECK(d.count == 2 && last(&d)->cull == TIDE_CULL_FRONT);
    tide_draw_set_cull(&d, TIDE_CULL_BACK);
    tide_draw_mesh_3d(&d, &quad, same, NULL, TIDE_FILTER_BILINEAR);
    TIDE_CHECK(d.count == 3 && last(&d)->cull == TIDE_CULL_BACK);
    tide_draw_pop(&d, front);
    TIDE_CHECK(d.place.cull == TIDE_CULL_NONE && d.saved_count == 0);
    tide_draw_mesh_3d(&d, &quad, same, NULL, TIDE_FILTER_BILINEAR);
    TIDE_CHECK(d.count == 4 && last(&d)->cull == TIDE_CULL_NONE);
    const uint32_t outer = tide_draw_push_cull(&d, TIDE_CULL_FRONT);
    tide_draw_push_blend(&d, TIDE_BLEND_ADD);
    tide_draw_push_cull(&d, TIDE_CULL_BACK);
    tide_draw_pop(&d, outer);
    TIDE_CHECK(d.place.cull == TIDE_CULL_NONE && d.place.blend == TIDE_BLEND_ALPHA && d.saved_count == 0);
    tide_draw_unset_cull(&d);
    TIDE_CHECK(d.place.cull == TIDE_CULL_BACK);
    tide_draw_set_cull(&d, TIDE_CULL_FRONT);
    tide_draw_set_cull(&d, (tide_cull)77);
    TIDE_CHECK(d.place.cull == TIDE_CULL_BACK);

    tide_draw_reset(&d);
    for (int entity = 0; entity < 3; entity++) {
        const uint32_t mark = tide_draw_push_cull(&d, TIDE_CULL_FRONT);
        tide_draw_mesh_3d(&d, &quad, tide_translate_f4x4(tide_f3((float)entity, 0.0f, 0.0f)), NULL, TIDE_FILTER_BILINEAR);
        tide_draw_pop(&d, mark);
    }
    TIDE_REQUIRE(d.count == 1 && last(&d)->mesh3.count == 3 && last(&d)->cull == TIDE_CULL_FRONT);
    tide_draw_pop(&d, tide_draw_push_cull(&d, TIDE_CULL_NONE));
    tide_draw_set_cull(&d, TIDE_CULL_FRONT);
    tide_draw_mesh_3d(&d, &quad, same, NULL, TIDE_FILTER_BILINEAR);
    TIDE_CHECK(d.count == 1 && last(&d)->mesh3.count == 4);
    tide_draw_unset_cull(&d);
    tide_draw_mesh_3d(&d, &quad, same, NULL, TIDE_FILTER_BILINEAR);
    TIDE_CHECK(d.count == 2 && last(&d)->mesh3.count == 1 && last(&d)->cull == TIDE_CULL_BACK);

    tide_draw_reset(&d);
    tide_draw_mesh_3d(&d, &quad, same, NULL, TIDE_FILTER_BILINEAR);
    tide_draw_mesh_3d(&d, &quad, mirror, NULL, TIDE_FILTER_BILINEAR);
    tide_draw_mesh_3d(&d, &quad, mirror, NULL, TIDE_FILTER_BILINEAR);
    tide_draw_mesh_3d(&d, &quad, same, NULL, TIDE_FILTER_BILINEAR);
    TIDE_REQUIRE(d.count == 3);
    TIDE_CHECK(d.commands[0].cull == TIDE_CULL_BACK && d.commands[0].mesh3.count == 1);
    TIDE_CHECK(d.commands[1].cull == (TIDE_CULL_BACK | TIDE_DRAW_MIRRORED) && d.commands[1].mesh3.count == 2);
    TIDE_CHECK(d.commands[2].cull == TIDE_CULL_BACK && d.commands[2].mesh3.count == 1);
    const tide_float4x4 mixed[5] = {same, same, mirror, same, mirror};
    const uint32_t slot = d.commands[0].mesh3.mesh;
    tide_draw_instances(&d, slot, mixed, 5, 0, TIDE_FILTER_BILINEAR, TIDE_WRAP_CLAMP);
    TIDE_REQUIRE(d.count == 6 && d.commands[2].mesh3.count == 3);
    TIDE_CHECK(d.commands[3].cull == (TIDE_CULL_BACK | TIDE_DRAW_MIRRORED) && d.commands[3].mesh3.count == 1);
    TIDE_CHECK(d.commands[4].cull == TIDE_CULL_BACK && d.commands[4].mesh3.count == 1);
    TIDE_CHECK(d.commands[5].cull == (TIDE_CULL_BACK | TIDE_DRAW_MIRRORED) && d.commands[5].mesh3.count == 1);
    for (uint32_t i = 0; i < 5; i++) {
        TIDE_CHECK(memcmp(&d.matrices[d.commands[2].mesh3.first + 1u + i], &mixed[i], sizeof mixed[i]) == 0);
    }
    tide_draw_set_cull(&d, TIDE_CULL_FRONT);
    tide_draw_instances(&d, slot, mixed + 1, 2, 0, TIDE_FILTER_BILINEAR, TIDE_WRAP_CLAMP);
    TIDE_REQUIRE(d.count == 8);
    TIDE_CHECK(d.commands[6].cull == TIDE_CULL_FRONT && last(&d)->cull == (TIDE_CULL_FRONT | TIDE_DRAW_MIRRORED));
    tide_draw_set_cull(&d, TIDE_CULL_NONE);
    tide_draw_instances(&d, slot, mixed, 5, 0, TIDE_FILTER_BILINEAR, TIDE_WRAP_CLAMP);
    TIDE_CHECK(d.count == 9 && last(&d)->cull == TIDE_CULL_NONE && last(&d)->mesh3.count == 5);
    tide_draw_unset_cull(&d);

    tide_draw_reset(&d);
    const uint32_t through = tide_draw_push_transform_3d(&d, mirror);
    TIDE_CHECK(d.place.moved & TIDE_DRAW_MOVED_MIRROR);
    tide_draw_mesh_3d(&d, &quad, same, NULL, TIDE_FILTER_BILINEAR);
    TIDE_CHECK(d.count == 1 && last(&d)->cull == (TIDE_CULL_BACK | TIDE_DRAW_MIRRORED));
    tide_draw_mesh_3d(&d, &quad, mirror, NULL, TIDE_FILTER_BILINEAR);
    TIDE_CHECK(d.count == 2 && last(&d)->cull == TIDE_CULL_BACK);
    const uint32_t again = tide_draw_push_transform_3d(&d, tide_mul_f4x4(mirror, tide_translate_f4x4(tide_f3(3.0f, 0.0f, 0.0f))));
    TIDE_CHECK(!(d.place.moved & TIDE_DRAW_MOVED_MIRROR));
    tide_draw_mesh_3d(&d, &quad, same, NULL, TIDE_FILTER_BILINEAR);
    TIDE_CHECK(d.count == 2 && last(&d)->mesh3.count == 2);
    tide_draw_pop(&d, again);
    TIDE_CHECK(d.place.moved & TIDE_DRAW_MOVED_MIRROR);
    tide_draw_mesh_3d(&d, &quad, same, NULL, TIDE_FILTER_BILINEAR);
    TIDE_CHECK(d.count == 3 && last(&d)->cull == (TIDE_CULL_BACK | TIDE_DRAW_MIRRORED));
    tide_draw_pop(&d, through);
    TIDE_CHECK(!(d.place.moved & (TIDE_DRAW_MOVED_3D | TIDE_DRAW_MOVED_MIRROR)));
    tide_draw_mesh_3d(&d, &quad, same, NULL, TIDE_FILTER_BILINEAR);
    TIDE_CHECK(d.count == 4 && last(&d)->cull == TIDE_CULL_BACK);
    tide_draw_set_transform_3d(&d, mirror);
    tide_draw_pop(&d, tide_draw_push_transform(&d, tide_draw_transform_moved(tide_f2(4.0f, 0.0f))));
    tide_draw_set_transform(&d, tide_draw_transform_moved(tide_f2(4.0f, 0.0f)));
    tide_draw_unset_transform(&d);
    TIDE_CHECK((d.place.moved & TIDE_DRAW_MOVED_MIRROR) && (d.place.moved & TIDE_DRAW_MOVED_3D));
    tide_draw_set_transform_3d(&d, tide_trs_f4x4(tide_f3(0.0f, 0.0f, 0.0f), tide_identity_q(), tide_f3(1.0f, 0.0f, 1.0f)));
    TIDE_CHECK((d.place.moved & TIDE_DRAW_MOVED_3D) && !(d.place.moved & TIDE_DRAW_MOVED_MIRROR));
    tide_draw_mesh_3d(&d, &quad, same, NULL, TIDE_FILTER_BILINEAR);
    TIDE_CHECK(last(&d)->cull == TIDE_CULL_BACK);
    tide_draw_unset_transform_3d(&d);

    static const tide_shader solid = {.dims = 3, .vertex_size = 32, .instance_floats = 4};
    static const tide_shader bare = {.dims = 3, .vertex_size = 32};
    static const tide_shader flat = {.dims = 2, .vertex_size = 32, .instance_floats = 1};
    static const tide_shader pass = {.dims = 2, .vertex_size = sizeof(tide_vertex)};
    const float instance[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    tide_draw_reset(&d);
    const uint32_t mesh = shader_mesh(&d, 32);
    tide_draw_shaded(&d, &solid, mesh, &same, NULL, NULL, 0);
    tide_draw_shaded(&d, &solid, mesh, &mirror, NULL, NULL, 0);
    tide_draw_shaded(&d, &solid, mesh, &mirror, NULL, NULL, 0);
    tide_draw_shaded(&d, &solid, mesh, &same, NULL, NULL, 0);
    TIDE_REQUIRE(d.count == 3 && d.commands[1].kind == TIDE_DRAW_SHADED);
    TIDE_CHECK(d.commands[0].cull == TIDE_CULL_BACK && d.runs[d.commands[0].shaded.run].count == 1);
    TIDE_CHECK(d.commands[1].cull == (TIDE_CULL_BACK | TIDE_DRAW_MIRRORED) && d.runs[d.commands[1].shaded.run].count == 2);
    TIDE_CHECK(d.commands[2].cull == TIDE_CULL_BACK && d.runs[d.commands[2].shaded.run].count == 1);
    tide_draw_shaded_instances(&d, &solid, mesh, mixed, 5, NULL, NULL, 0);
    TIDE_REQUIRE(d.count == 6 && d.runs[d.commands[2].shaded.run].count == 3);
    TIDE_CHECK(d.commands[3].cull == (TIDE_CULL_BACK | TIDE_DRAW_MIRRORED) && d.commands[4].cull == TIDE_CULL_BACK);
    TIDE_CHECK(last(&d)->cull == (TIDE_CULL_BACK | TIDE_DRAW_MIRRORED) && d.runs[last(&d)->shaded.run].count == 1);
    const tide_draw_run *third = &d.runs[d.commands[3].shaded.run];
    TIDE_CHECK(third->count == 1 && memcmp(d.instances + third->first, &mirror, sizeof mirror) == 0);
    tide_draw_set_transform_3d(&d, mirror);
    tide_draw_shaded(&d, &solid, mesh, &same, NULL, NULL, 0);
    TIDE_CHECK(d.count == 6 && d.runs[last(&d)->shaded.run].count == 2);
    tide_draw_unset_transform_3d(&d);
    tide_draw_set_cull(&d, TIDE_CULL_NONE);
    tide_draw_shaded_instances(&d, &solid, mesh, mixed, 5, NULL, NULL, 0);
    TIDE_CHECK(d.count == 7 && last(&d)->cull == TIDE_CULL_NONE && d.runs[last(&d)->shaded.run].count == 5);
    tide_draw_set_cull(&d, TIDE_CULL_FRONT);
    tide_draw_shaded(&d, &bare, mesh, NULL, NULL, NULL, 0);
    TIDE_CHECK(d.count == 8 && last(&d)->cull == TIDE_CULL_FRONT);

    const uint32_t count = d.count;
    tide_draw_shaded(&d, &flat, mesh, instance, NULL, NULL, 0);
    tide_draw_set_cull(&d, TIDE_CULL_NONE);
    tide_draw_shaded(&d, &flat, mesh, instance, NULL, NULL, 0);
    TIDE_CHECK(d.count == count + 1u && last(&d)->cull == 0 && d.runs[last(&d)->shaded.run].count == 2);
    tide_draw_rect(&d, origin, unit, TIDE_COLOR_WHITE);
    TIDE_CHECK(last(&d)->kind == TIDE_DRAW_RECT && last(&d)->cull == 0);
    tide_draw_text(&d, "a", origin, 8.0f, TIDE_COLOR_WHITE);
    TIDE_CHECK(last(&d)->kind == TIDE_DRAW_TEXT && last(&d)->cull == 0);
    tide_draw_blit(&d, &pass, TIDE_BLEND_REPLACE, NULL, NULL, NULL, 0);
    TIDE_CHECK(last(&d)->kind == TIDE_DRAW_BLIT && last(&d)->cull == 0);
    tide_draw_clear(&d, TIDE_COLOR_BLACK);
    TIDE_CHECK(last(&d)->kind == TIDE_DRAW_CLEAR && last(&d)->cull == 0);

    TIDE_CHECK(d.place.cull == TIDE_CULL_NONE);
    tide_draw_to(&d, 17, 64, 64);
    TIDE_CHECK(d.place.cull == TIDE_CULL_BACK);
    tide_draw_set_frustum_culling(&d, false); // Inspect face state on a mesh at the camera origin
    tide_draw_mesh_3d(&d, &quad, same, NULL, TIDE_FILTER_BILINEAR);
    TIDE_CHECK(last(&d)->kind == TIDE_DRAW_MESH_3D && last(&d)->cull == TIDE_CULL_BACK);
    const uint32_t block = tide_draw_push_cull(&d, TIDE_CULL_FRONT);
    tide_draw_mesh_3d(&d, &quad, tide_translate_f4x4(tide_f3(5.0f, 0.0f, 0.0f)), NULL, TIDE_FILTER_BILINEAR);
    TIDE_CHECK(last(&d)->cull == TIDE_CULL_FRONT);
    tide_draw_to(&d, 0, 0, 0);
    tide_draw_mesh_3d(&d, &quad, same, NULL, TIDE_FILTER_BILINEAR);
    TIDE_CHECK(last(&d)->cull == TIDE_CULL_NONE);
    tide_draw_pop(&d, block);
    TIDE_CHECK(d.place.cull == TIDE_CULL_NONE && d.saved_count == 0);
    tide_draw_to(&d, 17, 64, 64);
    TIDE_CHECK(d.place.cull == TIDE_CULL_BACK);
    tide_draw_set_cull(&d, TIDE_CULL_FRONT);

    tide_draw_defaults(&d);
    TIDE_CHECK(d.place.cull == TIDE_CULL_BACK && d.target == 0);
    tide_draw_to(&d, 17, 64, 64);
    TIDE_CHECK(d.place.cull == TIDE_CULL_BACK);

    tide_draw_list from = {0}, to = {0};
    tide_draw_mesh_3d(&from, &quad, same, NULL, TIDE_FILTER_BILINEAR);
    tide_draw_mesh_3d(&from, &quad, mirror, NULL, TIDE_FILTER_BILINEAR);
    tide_draw_set_cull(&from, TIDE_CULL_FRONT);
    tide_draw_shaded(&from, &solid, shader_mesh(&from, 32), &mirror, NULL, NULL, 0);
    tide_draw_set_cull(&from, TIDE_CULL_NONE);
    tide_draw_mesh_3d(&from, &quad, mirror, NULL, TIDE_FILTER_BILINEAR);
    tide_draw_set_cull(&to, TIDE_CULL_FRONT);
    tide_draw_set_transform_3d(&to, mirror);
    tide_draw_append(&to, &from);
    TIDE_REQUIRE(from.count == 4 && to.count == 4);
    TIDE_CHECK(to.commands[0].cull == TIDE_CULL_BACK && to.commands[1].cull == (TIDE_CULL_BACK | TIDE_DRAW_MIRRORED));
    TIDE_CHECK(to.commands[2].kind == TIDE_DRAW_SHADED && to.commands[2].cull == (TIDE_CULL_FRONT | TIDE_DRAW_MIRRORED));
    TIDE_CHECK(to.commands[3].cull == TIDE_CULL_NONE && to.place.cull == TIDE_CULL_BACK);
    tide_draw_free(&from);
    tide_draw_free(&to);

    // A camera that shows the world mirrored says so, by what it's made of: the ones the engine places
    // never do, and one placed by a matrix does when that mirrors, or its projection isn't OpenGL's way round
    const tide_float4x4 lens = tide_ortho_f4x4(16.0f, 9.0f, 0.1f, 100.0f);
    tide_draw_reset(&d);
    tide_draw_set_camera_3d(&d, tide_f3(1.0e7f, 0.0f, -3.0e7f), tide_identity_q(), 60.0f);
    TIDE_REQUIRE(d.camera_3d_count == 1 && d.cameras_3d[0].mirrored == 0);
    tide_draw_set_camera_matrices(&d, tide_translate_f4x4(tide_f3(1.0e7f, 2.0f, 3.0f)), lens);
    TIDE_REQUIRE(d.camera_3d_count == 2 && d.cameras_3d[1].mirrored == 0);
    tide_draw_set_camera_matrices(&d, mirror, lens);
    TIDE_REQUIRE(d.camera_3d_count == 3 && d.cameras_3d[2].mirrored == 1);
    tide_draw_mesh_3d(&d, &quad, same, NULL, TIDE_FILTER_BILINEAR);
    TIDE_REQUIRE(d.count == 2 && d.commands[0].kind == TIDE_DRAW_CAMERA_3D && d.commands[0].camera.mirrored == 1);
    TIDE_CHECK(last(&d)->cull == TIDE_CULL_BACK);
    tide_draw_set_camera_matrices(&d, same, same); // The identity looks back along z
    TIDE_REQUIRE(d.camera_3d_count == 4 && d.cameras_3d[3].mirrored == 1);
    tide_draw_set_camera_matrices(&d, mirror, same);
    TIDE_REQUIRE(d.camera_3d_count == 5 && d.cameras_3d[4].mirrored == 0);
    tide_draw_unset_camera_3d(&d);
    tide_draw_mesh_3d(&d, &quad, same, NULL, TIDE_FILTER_BILINEAR);
    TIDE_CHECK(d.commands[d.count - 2u].kind == TIDE_DRAW_CAMERA_3D && d.commands[d.count - 2u].camera.mirrored == 0);
    tide_draw_append(&to, &d);
    TIDE_REQUIRE(to.count == d.count && to.commands[0].kind == TIDE_DRAW_CAMERA_3D);
    TIDE_CHECK(to.commands[0].camera.mirrored == 1 && to.commands[2].camera.mirrored == 0);
    tide_draw_free(&to);
    tide_draw_free(&d);
}

TIDE_TEST(draw_stencil_is_a_destinations_own)
{
    tide_draw_list d = {0};
    const tide_stencil mask = tide_stencil_stored(TIDE_STENCIL_ALWAYS, 1, TIDE_STENCIL_REPLACE);
    const tide_stencil inside = tide_stencil_tested(TIDE_STENCIL_EQUAL, 1);
    tide_draw_rect(&d, origin, unit, TIDE_COLOR_WHITE);
    TIDE_REQUIRE(shown(&d).count == 1 && d.place.stencil == 0);

    TIDE_CHECK(mask.test == TIDE_STENCIL_ALWAYS && mask.operation == TIDE_STENCIL_REPLACE && mask.value == 1);
    TIDE_CHECK(inside.test == TIDE_STENCIL_EQUAL && inside.operation == TIDE_STENCIL_KEEP && inside.value == 1);
    TIDE_CHECK(tide_stencil_tested(TIDE_STENCIL_EQUAL, 999).value == 255 && tide_stencil_tested(TIDE_STENCIL_EQUAL, -3).value == 0);
    TIDE_CHECK(tide_stencil_stored(77, 4, 77).test == TIDE_STENCIL_ALWAYS && tide_stencil_stored(77, 4, 77).operation == TIDE_STENCIL_KEEP);
    TIDE_CHECK(tide_stencil_packed(tide_stencil_tested(TIDE_STENCIL_ALWAYS, 9)) == 0);
    TIDE_CHECK(tide_stencil_packed(tide_stencil_stored(TIDE_STENCIL_ALWAYS, 9, TIDE_STENCIL_INCREMENT))
               == tide_stencil_packed(tide_stencil_stored(TIDE_STENCIL_ALWAYS, 0, TIDE_STENCIL_INCREMENT)));
    TIDE_CHECK(tide_stencil_packed(tide_stencil_stored(TIDE_STENCIL_NEVER, 9, TIDE_STENCIL_REPLACE))
               == tide_stencil_packed(tide_stencil_tested(TIDE_STENCIL_NEVER, 0)));
    TIDE_CHECK(tide_stencil_packed(mask) != tide_stencil_packed(tide_stencil_stored(TIDE_STENCIL_ALWAYS, 2, TIDE_STENCIL_REPLACE)));
    const tide_stencil back =
        tide_stencil_unpacked(tide_stencil_packed(tide_stencil_stored(TIDE_STENCIL_GREATER_EQUAL, 200, TIDE_STENCIL_INVERT)));
    TIDE_CHECK(back.test == TIDE_STENCIL_GREATER_EQUAL && back.value == 200 && back.operation == TIDE_STENCIL_INVERT);

    tide_draw_set_stencil(&d, mask);
    TIDE_CHECK(shown(&d).count == 1);
    tide_draw_circle(&d, origin, 4.0f, TIDE_COLOR_WHITE);
    tide_draw_circle(&d, unit, 4.0f, TIDE_COLOR_WHITE);
    TIDE_REQUIRE(shown(&d).count == 4 && shown(&d).commands[1].kind == TIDE_DRAW_STENCIL && shown(&d).commands[1].stencil == tide_stencil_packed(mask));
    TIDE_CHECK(shown(&d).commands[2].kind == TIDE_DRAW_CIRCLE && shown(&d).commands[3].kind == TIDE_DRAW_CIRCLE);
    tide_draw_set_stencil(&d, mask);
    tide_draw_rect(&d, origin, unit, TIDE_COLOR_WHITE);
    TIDE_CHECK(shown(&d).count == 5 && last(&d)->kind == TIDE_DRAW_RECT);

    const uint32_t block = tide_draw_push_stencil(&d, inside);
    tide_draw_rect(&d, origin, unit, TIDE_COLOR_WHITE);
    TIDE_REQUIRE(shown(&d).count == 7 && shown(&d).commands[5].kind == TIDE_DRAW_STENCIL && shown(&d).commands[5].stencil == tide_stencil_packed(inside));
    tide_draw_set_stencil(&d, tide_stencil_tested(TIDE_STENCIL_NOT_EQUAL, 1));
    tide_draw_pop(&d, block);
    TIDE_CHECK(d.place.stencil == tide_stencil_packed(mask) && d.saved_count == 0);
    tide_draw_rect(&d, origin, unit, TIDE_COLOR_WHITE);
    TIDE_REQUIRE(shown(&d).count == 9 && shown(&d).commands[7].kind == TIDE_DRAW_STENCIL && shown(&d).commands[7].stencil == tide_stencil_packed(mask));
    const uint32_t outer = tide_draw_push_stencil(&d, inside);
    tide_draw_push_blend(&d, TIDE_BLEND_ADD);
    tide_draw_push_stencil(&d, tide_stencil_tested(TIDE_STENCIL_LESS, 3));
    tide_draw_pop(&d, outer);
    TIDE_CHECK(d.place.stencil == tide_stencil_packed(mask) && d.place.blend == TIDE_BLEND_ALPHA && d.saved_count == 0);
    tide_draw_unset_stencil(&d);
    TIDE_CHECK(d.place.stencil == 0 && shown(&d).count == 9);

    tide_draw_reset(&d);
    TIDE_CHECK(d.place.stencil == 0 && d.place.played_stencil == 0);
    for (int entity = 0; entity < 3; entity++) {
        const uint32_t mark = tide_draw_push_stencil(&d, inside);
        tide_draw_rect(&d, tide_f2((float)entity, 0.0f), unit, TIDE_COLOR_WHITE);
        tide_draw_pop(&d, mark);
    }
    TIDE_REQUIRE(shown(&d).count == 4 && shown(&d).commands[0].kind == TIDE_DRAW_STENCIL);
    TIDE_CHECK(shown(&d).commands[1].kind == TIDE_DRAW_RECT && shown(&d).commands[2].kind == TIDE_DRAW_RECT && shown(&d).commands[3].kind == TIDE_DRAW_RECT);
    tide_draw_pop(&d, tide_draw_push_stencil(&d, mask));
    tide_draw_set_stencil(&d, inside);
    tide_draw_rect(&d, origin, unit, TIDE_COLOR_WHITE);
    TIDE_CHECK(shown(&d).count == 5 && last(&d)->kind == TIDE_DRAW_RECT);
    tide_draw_mesh_3d(&d, &quad, tide_identity_f4x4(), NULL, TIDE_FILTER_BILINEAR);
    tide_draw_pop(&d, tide_draw_push_stencil(&d, mask));
    tide_draw_mesh_3d(&d, &quad, tide_translate_f4x4(tide_f3(1.0f, 0.0f, 0.0f)), NULL, TIDE_FILTER_BILINEAR);
    TIDE_REQUIRE(shown(&d).count == 6 && last(&d)->kind == TIDE_DRAW_MESH_3D && last(&d)->mesh3.count == 2);
    tide_draw_set_stencil(&d, mask);
    tide_draw_mesh_3d(&d, &quad, tide_identity_f4x4(), NULL, TIDE_FILTER_BILINEAR);
    TIDE_REQUIRE(shown(&d).count == 8 && shown(&d).commands[6].kind == TIDE_DRAW_STENCIL && last(&d)->mesh3.count == 1);
    static const tide_shader pass = {.dims = 2, .vertex_size = sizeof(tide_vertex)};
    tide_draw_text(&d, "a", origin, 8.0f, TIDE_COLOR_WHITE);
    tide_draw_clear(&d, TIDE_COLOR_BLACK);
    tide_draw_blit(&d, &pass, TIDE_BLEND_REPLACE, NULL, NULL, NULL, 0);
    TIDE_CHECK(shown(&d).count == 11 && shown(&d).commands[8].kind == TIDE_DRAW_TEXT && shown(&d).commands[9].kind == TIDE_DRAW_CLEAR);
    TIDE_CHECK(last(&d)->kind == TIDE_DRAW_BLIT && d.place.stencil == tide_stencil_packed(mask));

    tide_draw_to(&d, 17, 64, 64);
    TIDE_CHECK(d.place.stencil == 0);
    uint32_t count = shown(&d).count;
    tide_draw_rect(&d, origin, unit, TIDE_COLOR_WHITE);
    TIDE_CHECK(shown(&d).count == count + 1u && last(&d)->kind == TIDE_DRAW_RECT);
    const uint32_t theirs = tide_draw_push_stencil(&d, inside);
    tide_draw_rect(&d, origin, unit, TIDE_COLOR_WHITE);
    TIDE_REQUIRE(shown(&d).count == count + 3u && shown(&d).commands[count + 1u].kind == TIDE_DRAW_STENCIL);
    tide_draw_to(&d, 0, 0, 0);
    TIDE_CHECK(d.place.stencil == tide_stencil_packed(mask));
    count = shown(&d).count;
    tide_draw_rect(&d, origin, unit, TIDE_COLOR_WHITE);
    TIDE_CHECK(shown(&d).count == count + 1u && last(&d)->kind == TIDE_DRAW_RECT);
    tide_draw_pop(&d, theirs);
    TIDE_CHECK(d.place.stencil == tide_stencil_packed(mask) && d.saved_count == 0);
    tide_draw_to(&d, 17, 64, 64);
    TIDE_CHECK(d.place.stencil == 0 && d.place.played_stencil == tide_stencil_packed(inside));
    count = shown(&d).count;
    tide_draw_rect(&d, origin, unit, TIDE_COLOR_WHITE);
    TIDE_REQUIRE(shown(&d).count == count + 2u && shown(&d).commands[count].kind == TIDE_DRAW_STENCIL && shown(&d).commands[count].stencil == 0);
    tide_draw_set_stencil(&d, inside);

    tide_draw_defaults(&d);
    TIDE_CHECK(d.place.stencil == 0 && d.target == 0);
    count = shown(&d).count;
    tide_draw_rect(&d, origin, unit, TIDE_COLOR_WHITE);
    TIDE_REQUIRE(shown(&d).count == count + 2u && shown(&d).commands[count].kind == TIDE_DRAW_STENCIL && shown(&d).commands[count].stencil == 0);
    tide_draw_to(&d, 17, 64, 64);
    TIDE_CHECK(d.place.stencil == 0);

    tide_draw_list from = {0}, to = {0};
    tide_draw_rect(&to, origin, unit, TIDE_COLOR_WHITE);
    tide_draw_set_stencil(&to, mask);
    tide_draw_rect(&to, origin, unit, TIDE_COLOR_WHITE);
    tide_draw_rect(&from, origin, unit, TIDE_COLOR_WHITE);
    tide_draw_set_stencil(&from, inside);
    tide_draw_rect(&from, origin, unit, TIDE_COLOR_WHITE);
    tide_draw_set_stencil(&from, tide_stencil_tested(TIDE_STENCIL_GREATER, 7));
    tide_draw_append(&to, &from);
    TIDE_REQUIRE(shown(&to).count == 7 && shown(&to).commands[1].kind == TIDE_DRAW_STENCIL && shown(&to).commands[1].stencil == tide_stencil_packed(mask));
    TIDE_CHECK(shown(&to).commands[3].kind == TIDE_DRAW_STENCIL && shown(&to).commands[3].stencil == 0 && shown(&to).commands[4].kind == TIDE_DRAW_RECT);
    TIDE_CHECK(shown(&to).commands[5].kind == TIDE_DRAW_STENCIL && shown(&to).commands[5].stencil == tide_stencil_packed(inside));
    TIDE_CHECK(shown(&to).commands[6].kind == TIDE_DRAW_RECT);
    TIDE_CHECK(to.place.stencil == tide_stencil_packed(tide_stencil_tested(TIDE_STENCIL_GREATER, 7)));
    tide_draw_free(&from);
    tide_draw_free(&to);
    tide_draw_free(&d);
}
