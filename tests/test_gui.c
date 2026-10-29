#include <float.h>
#include <math.h>
#include <string.h>

#include "tide/gui.h"
#include "tide/text.h"
#include "tide_test.h"
#include "draw_test.h"

// The GUI runtime driven by made-up devices, on a 1920 x 1080 window. The
// mouse is given from the top left, y down, like the GUI.

static tide_gui gui;
static tide_devices devices;
static tide_draw_list draw;

static void start(void)
{
    memset(&gui, 0, sizeof gui);
    memset(&devices, 0, sizeof devices);
}

static void mouse(const float x, const float y, const bool held)
{
    devices.mouse.position = tide_f2(x, 1080.0f - y);
    tide_button_set(&devices.mouse.left, held);
}

static void key(tide_button *b, const bool held)
{
    tide_button_set(b, held);
}

static void begin(void)
{
    tide_pointer_poll(&devices, true);
    tide_gui_begin(&gui, &devices, tide_f2(1920.0f, 1080.0f), NULL);
}

static void end(void)
{
    tide_draw_reset(&draw);
    tide_gui_end(&gui, &draw);
    tide_test_flatten(&draw);
    devices.keyboard.text.count = 0;
}

static void type(const char *text)
{
    for (const char *c = text; *c; c++) devices.keyboard.text.chars[devices.keyboard.text.count++] = (uint32_t)*c;
}

TIDE_TEST(gui_custom_control_reports_pointer_edges_and_cancels_once)
{
    start();
    const tide_rect rect = {10, 10, 100, 40};
    mouse(20, 20, false);
    begin();
    tide_gui_control(&gui, 10, rect);
    end();
    mouse(20, 20, true);
    begin();
    tide_control c = tide_gui_control(&gui, 10, rect);
    TIDE_CHECK(c.down && c.pressed && !c.up && !c.clicked && !c.cancelled);
    TIDE_CHECK(c.position.x == 20 && c.position.y == 20);
    end();
    mouse(500, 400, true);
    begin();
    c = tide_gui_control(&gui, 10, rect);
    TIDE_CHECK(!c.down && c.pressed && !c.hovered && !c.up);
    TIDE_CHECK(c.position.x == 500 && c.position.y == 400);
    end();
    mouse(500, 400, false);
    begin();
    c = tide_gui_control(&gui, 10, rect);
    TIDE_CHECK(c.up && !c.pressed && !c.clicked && !c.cancelled);
    end();

    mouse(20, 20, true);
    begin();
    tide_gui_control(&gui, 10, rect);
    end();
    begin();
    const int disabled = tide_gui_begin_disabled(&gui, true);
    c = tide_gui_control(&gui, 10, rect);
    TIDE_CHECK(c.cancelled && !c.pressed && !c.down && !c.up && !c.clicked);
    tide_gui_close(&gui, disabled);
    end();
    begin();
    c = tide_gui_control(&gui, 10, rect);
    TIDE_CHECK(!c.cancelled && !c.pressed && !c.clicked);
    end();
}

TIDE_TEST(gui_reserved_space_has_explicit_height_and_safe_dimensions)
{
    start();
    begin();
    tide_rect a = tide_gui_layout_reserve(&gui, tide_f2(120, 44));
    tide_rect b = tide_gui_layout_reserve_flexible(&gui, tide_f2(2000, 60), 80, false);
    tide_rect invalid = tide_gui_layout_reserve(&gui, tide_f2(NAN, -INFINITY));
    TIDE_CHECK(a.x == 0 && a.y == 0 && a.width == 120 && a.height == 44);
    TIDE_CHECK(b.y == 49 && b.width == 1920 && b.height == 60);
    TIDE_CHECK(invalid.width == 0 && invalid.height == 0);
    TIDE_CHECK(gui.rect_count_next == 0 && gui.nav_count == 0);
    end();
}

TIDE_TEST(gui_draw_scopes_restore_settings_and_preserve_enclosing_scopes)
{
    start();
    begin();
    const int keyed = tide_gui_begin_key(&gui, 20, 0);
    const uint32_t key_before = gui.key;
    const int drawing = tide_gui_begin_draw(&gui);
    tide_draw_set_tint(&gui.list, TIDE_COLOR_RED);
    tide_draw_set_blend(&gui.list, TIDE_BLEND_ADD);
    tide_draw_set_clip(&gui.list, (tide_rect){10, 20, 30, 40});
    tide_draw_rect(&gui.list, tide_f2(20, 30), tide_f2(5, 5), TIDE_COLOR_WHITE);
    const int disabled = tide_gui_begin_disabled(&gui, false);
    tide_gui_layout_reserve(&gui, tide_f2(100, 44));
    tide_gui_close(&gui, disabled);
    TIDE_CHECK(gui.drawing_count == 1 && gui.key == key_before);
    tide_gui_close(&gui, drawing);
    TIDE_CHECK(gui.drawing_count == 0 && gui.key == key_before);
    TIDE_CHECK(!gui.list.place.tinted && gui.list.place.blend == TIDE_BLEND_ALPHA && !gui.list.place.clip);
    tide_gui_close(&gui, keyed);
    TIDE_CHECK(gui.key == 0 && gui.depth == 0);
    end();
}

TIDE_TEST(gui_draw_at_the_nesting_ceiling_skips_without_changing_settings)
{
    start();
    begin();
    for (int i = 0; i < TIDE_GUI_MAX_DEPTH; i++) tide_gui_begin_disabled(&gui, false);
    TIDE_CHECK(tide_gui_begin_draw(&gui) == -1);
    TIDE_CHECK(gui.depth == TIDE_GUI_MAX_DEPTH && gui.drawing_count == 0);
    end();
    TIDE_CHECK(gui.depth == 0 && !gui.list.place.tinted);
}

TIDE_TEST(gui_anchored_custom_meshes_quads_and_clips_move_with_the_panel)
{
    start();
    begin();
    const int area = tide_gui_begin_area_at(&gui, 10, TIDE_ANCHOR_MIDDLE_CENTER);
    const tide_rect r = tide_gui_layout_reserve(&gui, tide_f2(100, 60));
    const int drawing = tide_gui_begin_draw(&gui);
    tide_draw_set_clip(&gui.list, r);
    const tide_vertex vertices[] = {
        {{r.x, r.y}, {0, 0}, TIDE_COLOR_WHITE},
        {{r.x + 100, r.y}, {1, 0}, TIDE_COLOR_WHITE},
        {{r.x, r.y + 60}, {0, 1}, TIDE_COLOR_WHITE},
    };
    const uint32_t indices[] = {0, 1, 2, 0, 1, 2};
    tide_draw_mesh(&gui.list, vertices, 3, indices, 6, NULL, TIDE_FILTER_BILINEAR);
    tide_draw_sprite(&gui.list, (tide_texture_ref){0}, tide_f2(r.x + 50, r.y + 30), tide_f2(100, 60), TIDE_COLOR_WHITE);
    tide_gui_close(&gui, drawing);
    tide_gui_close(&gui, area);
    TIDE_REQUIRE(gui.list.vertex_count >= 3);
    TIDE_CHECK(gui.list.vertices[0].position.x == 910 && gui.list.vertices[0].position.y == 510);
    TIDE_REQUIRE(gui.list.quad_count == 1);
    TIDE_CHECK(gui.list.quads[0].from.x == 910 && gui.list.quads[0].from.y == 510);
    TIDE_REQUIRE(gui.list.clip_count == 1);
    TIDE_CHECK(gui.list.clips[0].rect.x == 910 && gui.list.clips[0].rect.y == 510);
    end();
}

// A frame with two layout buttons; returns which were pressed (bit 0 and 1).
static int two_buttons(void)
{
    begin();
    const bool a = tide_gui_layout_button(&gui, 10, "Play");
    const bool b = tide_gui_layout_button(&gui, 20, "Quit");
    end();
    return (a ? 1 : 0) | (b ? 2 : 0);
}

TIDE_TEST(gui_button_clicks_on_release)
{
    start();
    mouse(20.0f, 47.0f, false);
    TIDE_CHECK(two_buttons() == 0);
    mouse(20.0f, 47.0f, true);
    TIDE_CHECK(two_buttons() == 0);
    mouse(20.0f, 47.0f, false);
    TIDE_CHECK(two_buttons() == 2);
    TIDE_CHECK(two_buttons() == 0);

    mouse(20.0f, 20.0f, true);
    TIDE_CHECK(two_buttons() == 0);
    mouse(900.0f, 900.0f, true);
    TIDE_CHECK(two_buttons() == 0);
    mouse(900.0f, 900.0f, false);
    TIDE_CHECK(two_buttons() == 0);
}

TIDE_TEST(gui_draws_over_the_world)
{
    start();
    begin();
    tide_gui_layout_label(&gui, "Hello");
    tide_gui_layout_button(&gui, 1, "Go");
    end();
    TIDE_REQUIRE(draw.count >= 4);
    TIDE_CHECK(draw.commands[0].kind == TIDE_DRAW_SCREEN);
    TIDE_CHECK(draw.commands[1].kind == TIDE_DRAW_TEXT && strcmp(draw.text + draw.commands[1].text, "Hello") == 0);
    TIDE_CHECK(draw.commands[2].kind == TIDE_DRAW_RECT);
    TIDE_CHECK(draw.commands[2].a.y == 28.0f + 5.0f + 14.0f);

    begin();
    end();
    TIDE_CHECK(draw.count == 0);
}

TIDE_TEST(gui_isnt_drawn_with_what_the_views_set)
{
    start();
    begin();
    tide_gui_layout_label(&gui, "Hello");
    const tide_draw_command label = gui.list.commands[gui.list.count - 1u];
    tide_draw_reset(&draw);
    tide_draw_set_camera(&draw, tide_f2(100.0f, 100.0f), 5.0f);
    tide_draw_set_clip(&draw, (tide_rect){0.0f, 0.0f, 10.0f, 10.0f});
    tide_draw_set_blend(&draw, TIDE_BLEND_ADD);
    tide_draw_set_tint(&draw, (tide_color){1.0f, 0.0f, 0.0f, 0.5f});
    tide_draw_set_transform(&draw, tide_draw_transform_trs(tide_f2(30.0f, 40.0f), 1.0f, tide_f2(2.0f, 3.0f)));
    tide_draw_rect(&draw, tide_f2(5.0f, 5.0f), tide_f2(2.0f, 2.0f), TIDE_COLOR_WHITE);
    const uint32_t block = tide_draw_push_tint(&draw, TIDE_COLOR_BLUE);
    tide_gui_end(&gui, &draw);
    tide_test_flatten(&draw);
    TIDE_REQUIRE(draw.count == 6);
    TIDE_CHECK(draw.commands[0].kind == TIDE_DRAW_CAMERA && draw.commands[1].kind == TIDE_DRAW_CLIP);
    TIDE_CHECK(draw.commands[2].kind == TIDE_DRAW_SHAPE && draw.commands[2].blend == TIDE_BLEND_ADD);
    TIDE_CHECK(draw.commands[3].kind == TIDE_DRAW_SCREEN && draw.commands[4].kind == TIDE_DRAW_NO_CLIP);
    const tide_draw_command *text = &draw.commands[5];
    TIDE_REQUIRE(text->kind == TIDE_DRAW_TEXT);
    TIDE_CHECK(text->blend == TIDE_BLEND_ALPHA && text->words.turned == 0);
    TIDE_CHECK(text->a.x == label.a.x && text->a.y == label.a.y && text->b.x == label.b.x);
    TIDE_CHECK(memcmp(&text->color, &label.color, sizeof label.color) == 0);
    tide_draw_pop(&draw, block);
    TIDE_CHECK(draw.place.tinted && draw.place.tint.r == 1.0f && draw.place.tint.b == 0.0f && draw.place.tint.a == 0.5f);
}

TIDE_TEST(gui_toggle_and_slider_change_values)
{
    start();
    bool on = false;
    float volume = 0.5f;
    for (int frame = 0; frame < 3; frame++) {
        mouse(10.0f, 14.0f, frame == 1);
        begin();
        const bool toggled = tide_gui_layout_toggle(&gui, 1, "Fullscreen", &on);
        tide_gui_layout_slider(&gui, 2, "", &volume, 0.0f, 1.0f);
        end();
        TIDE_CHECK(toggled == (frame == 2));
    }
    TIDE_CHECK(on);

    const float left = 6.0f;
    const float width = 180.0f - 12.0f;
    for (int frame = 0; frame < 3; frame++) {
        mouse(frame == 0 ? left + 10.0f : left + width * 0.25f, 28.0f + 5.0f + 14.0f, frame < 2);
        begin();
        tide_gui_layout_toggle(&gui, 1, "Fullscreen", &on);
        tide_gui_layout_slider(&gui, 2, "", &volume, 0.0f, 1.0f);
        end();
    }
    TIDE_CHECK(volume == 0.25f);
}

static bool int_slider_frame(int32_t *value, const int32_t min, const int32_t max)
{
    begin();
    const bool changed = tide_gui_int_slider(&gui, 10, (tide_rect){0, 0, 300, 28}, "", value, min, max);
    end();
    return changed;
}

TIDE_TEST(gui_int_sliders_step_by_one_across_the_int_range)
{
    static const struct {
        int32_t min, max, initial, right, left;
    } cases[] = {
        {2000000000, 2000000010, 2000000005, 2000000006, 2000000005},
        {-2000000010, -2000000000, -2000000005, -2000000004, -2000000005},
        {INT32_MIN, INT32_MAX, 0, 1, 0},
        {0, INT32_MAX, 1073741823, 1073741824, 1073741823},
        {INT32_MIN, INT32_MAX, INT32_MAX, INT32_MAX, INT32_MAX - 1},
        {INT32_MIN, INT32_MAX, INT32_MIN, INT32_MIN + 1, INT32_MIN},
        {INT32_MAX, INT32_MIN, 0, -1, 0},
        {2000000010, 2000000000, 2000000005, 2000000004, 2000000005},
        {7, 7, 7, 7, 7},
        {7, 7, 0, 7, 7},
        {10, 20, 0, 11, 10},
        {10, 20, 30, 20, 19},
    };
    for (int pad = 0; pad < 2; pad++) {
        for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
            start();
            gui.focus = 10;
            devices.gamepad.connected = pad != 0;
            int32_t value = cases[i].initial;
            TIDE_CHECK(!int_slider_frame(&value, cases[i].min, cases[i].max));
            TIDE_CHECK(value == cases[i].initial);
            tide_button *right = pad ? &devices.gamepad.dpad.right : &devices.keyboard.rightArrow;
            tide_button *left = pad ? &devices.gamepad.dpad.left : &devices.keyboard.leftArrow;
            key(right, true);
            TIDE_CHECK(int_slider_frame(&value, cases[i].min, cases[i].max) == (cases[i].right != cases[i].initial));
            TIDE_CHECK(value == cases[i].right);
            key(right, false);
            int_slider_frame(&value, cases[i].min, cases[i].max);
            key(left, true);
            TIDE_CHECK(int_slider_frame(&value, cases[i].min, cases[i].max) == (cases[i].left != cases[i].right));
            TIDE_CHECK(value == cases[i].left);
            TIDE_CHECK(gui.focus == 10);
        }
    }
}

TIDE_TEST(gui_int_sliders_place_large_values_on_the_track)
{
    for (int reversed = 0; reversed < 2; reversed++) {
        start();
        const int32_t min = reversed ? 2000000010 : 2000000000;
        const int32_t max = reversed ? 2000000000 : 2000000010;
        int32_t value = 2000000005;
        TIDE_CHECK(!int_slider_frame(&value, min, max));
        TIDE_REQUIRE(draw.count > 0);
        TIDE_CHECK(draw.commands[draw.count - 1u].a.x == 122.0f);
        mouse(6.0f, 14.0f, true);
        TIDE_CHECK(int_slider_frame(&value, min, max));
        TIDE_CHECK(value == min);
        mouse(238.0f, 14.0f, true);
        TIDE_CHECK(int_slider_frame(&value, min, max));
        TIDE_CHECK(value == max);
        mouse(122.0f, 14.0f, true);
        TIDE_CHECK(int_slider_frame(&value, min, max));
        TIDE_CHECK(value == 2000000005);
    }
}

static bool float_slider_frame(float *value, const float min, const float max)
{
    begin();
    const bool changed = tide_gui_slider(&gui, 10, (tide_rect){0, 0, 300, 28}, "", value, min, max);
    end();
    return changed;
}

TIDE_TEST(gui_float_sliders_keep_finite_ranges_finite)
{
    for (int reversed = 0; reversed < 2; reversed++) {
        start();
        const float min = reversed ? FLT_MAX : -FLT_MAX;
        const float max = -min;
        float value = 0.0f;
        TIDE_CHECK(!float_slider_frame(&value, min, max));
        TIDE_REQUIRE(draw.count > 0);
        TIDE_CHECK(draw.commands[draw.count - 1u].a.x == 122.0f);
        mouse(6.0f, 14.0f, true);
        TIDE_CHECK(float_slider_frame(&value, min, max));
        TIDE_CHECK(value == min);
        mouse(238.0f, 14.0f, true);
        TIDE_CHECK(float_slider_frame(&value, min, max));
        TIDE_CHECK(value == max);
        mouse(122.0f, 14.0f, true);
        TIDE_CHECK(float_slider_frame(&value, min, max));
        TIDE_CHECK(value == 0.0f);
        mouse(64.0f, 14.0f, true);
        TIDE_CHECK(float_slider_frame(&value, min, max));
        TIDE_CHECK(value == min * 0.5f);
        mouse(180.0f, 14.0f, true);
        TIDE_CHECK(float_slider_frame(&value, min, max));
        TIDE_CHECK(value == max * 0.5f);
    }
}

TIDE_TEST(gui_float_sliders_step_with_extreme_finite_bounds)
{
    for (int reversed = 0; reversed < 2; reversed++) {
        start();
        gui.focus = 10;
        const float min = reversed ? FLT_MAX : -FLT_MAX;
        const float max = -min;
        float value = 0.0f;
        TIDE_CHECK(!float_slider_frame(&value, min, max));
        key(&devices.keyboard.rightArrow, true);
        TIDE_CHECK(float_slider_frame(&value, min, max));
        TIDE_CHECK(value >= -FLT_MAX && value <= FLT_MAX);
        TIDE_CHECK(reversed ? value < 0.0f : value > 0.0f);
        key(&devices.keyboard.rightArrow, false);
        float_slider_frame(&value, min, max);
        key(&devices.keyboard.leftArrow, true);
        TIDE_CHECK(float_slider_frame(&value, min, max));
        TIDE_CHECK(value >= -FLT_MAX && value <= FLT_MAX);
        TIDE_CHECK(value > -FLT_MAX * 0.000001f && value < FLT_MAX * 0.000001f);
    }
}

TIDE_TEST(gui_int_slider_steps_keep_their_input_precedence)
{
    start();
    int32_t value = 2000000005;
    gui.focus = 10;
    int_slider_frame(&value, 2000000000, 2000000010);
    mouse(6.0f, 14.0f, true);
    int_slider_frame(&value, 2000000000, 2000000010);
    value = 2000000005;
    gui.focus = 10;
    key(&devices.keyboard.leftArrow, true);
    key(&devices.keyboard.rightArrow, true);
    TIDE_CHECK(int_slider_frame(&value, 2000000000, 2000000010));
    TIDE_CHECK(value == 2000000006);
}

TIDE_TEST(gui_ids_tell_loops_apart)
{
    start();
    begin();
    const uint32_t a = tide_gui_id(&gui, 7, 3);
    const uint32_t b = tide_gui_id(&gui, 7, 3);
    const uint32_t c = tide_gui_id(&gui, 8, 3);
    end();
    TIDE_CHECK(a != b && a != c && b != c);
    TIDE_CHECK(a != 0 && b != 0 && c != 0);
    begin();
    TIDE_CHECK(tide_gui_id(&gui, 7, 3) == a);
    TIDE_CHECK(tide_gui_id(&gui, 7, 3) == b);
    end();
}

TIDE_TEST(gui_keyboard_navigation)
{
    start();
    TIDE_CHECK(two_buttons() == 0);
    key(&devices.keyboard.downArrow, true);
    TIDE_CHECK(two_buttons() == 0);
    TIDE_CHECK(gui.focus == 10);
    key(&devices.keyboard.downArrow, false);
    TIDE_CHECK(two_buttons() == 0);
    key(&devices.keyboard.downArrow, true);
    TIDE_CHECK(two_buttons() == 0);
    TIDE_CHECK(gui.focus == 20);
    key(&devices.keyboard.downArrow, false);
    key(&devices.keyboard.enter, true);
    TIDE_CHECK(two_buttons() == 2);
    key(&devices.keyboard.enter, false);

    tide_devices sampled = devices;
    key(&sampled.keyboard.w, true);
    tide_gui_hide(&gui, &sampled);
    TIDE_CHECK(!sampled.keyboard.w.held && !sampled.keyboard.w.pressed);

    key(&devices.keyboard.escape, true);
    TIDE_CHECK(two_buttons() == 0);
    TIDE_CHECK(gui.focus == 0);
    key(&devices.keyboard.escape, false);
    sampled = devices;
    key(&sampled.keyboard.w, true);
    tide_gui_hide(&gui, &sampled);
    TIDE_CHECK(sampled.keyboard.w.held);
}

TIDE_TEST(gui_arrows_wait_while_the_game_reads_them)
{
    start();
    TIDE_CHECK(two_buttons() == 0);
    tide_devices sampled = devices;
    tide_gui_hide(&gui, &sampled);
    key(&devices.keyboard.downArrow, true);
    TIDE_CHECK(two_buttons() == 0);
    TIDE_CHECK(gui.focus == 0);
    key(&devices.keyboard.downArrow, false);
    tide_gui_hide(&gui, &sampled);
    key(&devices.keyboard.tab, true);
    TIDE_CHECK(two_buttons() == 0);
    TIDE_CHECK(gui.focus == 10);
}

// A frame of two_buttons with a finger: the touchscreen polled as the platform
// does, then up to two events at (x, y) from the top left (TIDE_TOUCHES for
// none).
static int touch_frame(const tide_touch_phase first, const tide_touch_phase second, const float x, const float y)
{
    tide_touches_poll(&devices.touchscreen);
    const tide_touch_phase phases[] = {first, second};
    for (int i = 0; i < 2; i++) {
        if ((int)phases[i] != TIDE_TOUCHES) tide_touch_event(&devices.touchscreen, phases[i], 1, tide_f2(x, 1080.0f - y));
    }
    tide_pointer_poll(&devices, false);
    tide_gui_begin(&gui, &devices, tide_f2(1920.0f, 1080.0f), NULL);
    const bool a = tide_gui_layout_button(&gui, 10, "Play");
    const bool b = tide_gui_layout_button(&gui, 20, "Quit");
    end();
    return (a ? 1 : 0) | (b ? 2 : 0);
}

#define NONE ((tide_touch_phase)TIDE_TOUCHES)

TIDE_TEST(gui_fingers_press_buttons)
{
    start();
    TIDE_CHECK(touch_frame(TIDE_TOUCH_BEGAN, NONE, 20.0f, 47.0f) == 0);
    TIDE_CHECK(gui.active == 20);
    tide_devices sampled = devices;
    tide_gui_hide(&gui, &sampled);
    TIDE_CHECK(!sampled.touchscreen.primaryTouch.press.pressed && !sampled.touchscreen.touches.at[0].press.pressed);
    TIDE_CHECK(!sampled.pointer.press.pressed);
    TIDE_CHECK(touch_frame(TIDE_TOUCH_ENDED, NONE, 20.0f, 47.0f) == 2);
    TIDE_CHECK(touch_frame(NONE, NONE, 0.0f, 0.0f) == 0);
    TIDE_CHECK(gui.hot == 0);

    TIDE_CHECK(touch_frame(TIDE_TOUCH_BEGAN, TIDE_TOUCH_ENDED, 20.0f, 20.0f) == 0);
    TIDE_CHECK(touch_frame(NONE, NONE, 0.0f, 0.0f) == 1);
}

TIDE_TEST(gui_buttons_answer_every_tap_where_nothing_samples)
{
    start();
    for (int i = 0; i < 3 * TIDE_TOUCHES; i++) {
        const bool quit = i % 2 == 0;
        const float y = quit ? 47.0f : 20.0f;
        TIDE_CHECK(touch_frame(TIDE_TOUCH_BEGAN, NONE, 20.0f, y) == 0);
        TIDE_CHECK(gui.active == (quit ? 20u : 10u));
        TIDE_CHECK(touch_frame(TIDE_TOUCH_ENDED, NONE, 20.0f, y) == (quit ? 2 : 1));
    }
    for (int i = 0; i < 2 * TIDE_TOUCHES; i++) {
        TIDE_CHECK(touch_frame(TIDE_TOUCH_BEGAN, TIDE_TOUCH_ENDED, 20.0f, 20.0f) == 0);
        TIDE_CHECK(touch_frame(NONE, NONE, 0.0f, 0.0f) == 1);
    }
}

// What a host does before a frame's GUI, with a finger: polls the touchscreen,
// with up to two events at (x, y) from the top left (TIDE_TOUCHES for none),
// then samples the game's input. Returns whether the sample has the finger's
// press.
static bool finger_sample(const tide_touch_phase first, const tide_touch_phase second, const float x, const float y)
{
    tide_touches_poll(&devices.touchscreen);
    const tide_touch_phase phases[] = {first, second};
    for (int i = 0; i < 2; i++) {
        if ((int)phases[i] != TIDE_TOUCHES) tide_touch_event(&devices.touchscreen, phases[i], 1, tide_f2(x, 1080.0f - y));
    }
    tide_pointer_poll(&devices, false);
    tide_devices sampled = devices;
    tide_gui_hide(&gui, &sampled);
    tide_devices_consume(&devices);
    return sampled.pointer.press.pressed || sampled.touchscreen.primaryTouch.press.pressed
        || sampled.touchscreen.touches.at[0].press.pressed;
}

// The same with the mouse at (x, y) from the top left: whether the sample has
// its left button's press.
static bool mouse_sample(const float x, const float y, const bool held)
{
    tide_touches_poll(&devices.touchscreen);
    mouse(x, y, held);
    tide_pointer_poll(&devices, true);
    tide_devices sampled = devices;
    tide_gui_hide(&gui, &sampled);
    tide_devices_consume(&devices);
    return sampled.mouse.left.pressed || sampled.pointer.press.pressed;
}

// Whether views read a press this frame: the pointer's, the primary touch's
// or the mouse's left button's.
static bool views_read_a_press(void)
{
    const tide_devices *d = &gui.devices;
    return d->pointer.press.pressed || d->pointer.press.down || d->pointer.press.up
        || d->touchscreen.primaryTouch.press.pressed || d->touchscreen.primaryTouch.press.down
        || d->mouse.left.pressed || d->mouse.left.down || d->mouse.left.up;
}

// ...and then the frame's GUI, two_buttons': which were pressed (bits 0 and 1),
// and whether views read a press (bit 2).
static int sampled_frame(void)
{
    tide_gui_begin(&gui, &devices, tide_f2(1920.0f, 1080.0f), NULL);
    const bool read = views_read_a_press();
    const bool a = tide_gui_layout_button(&gui, 10, "Play");
    const bool b = tide_gui_layout_button(&gui, 20, "Quit");
    end();
    return (a ? 1 : 0) | (b ? 2 : 0) | (read ? 4 : 0);
}

TIDE_TEST(gui_a_finger_on_a_button_is_never_the_games)
{
    start();
    TIDE_CHECK(!finger_sample(NONE, NONE, 0.0f, 0.0f));
    TIDE_CHECK(sampled_frame() == 0);
    TIDE_CHECK(!finger_sample(TIDE_TOUCH_BEGAN, NONE, 20.0f, 47.0f));
    TIDE_CHECK(sampled_frame() == 0);
    TIDE_CHECK(gui.active == 20);
    TIDE_CHECK(!finger_sample(NONE, NONE, 0.0f, 0.0f));
    TIDE_CHECK(sampled_frame() == 0);
    TIDE_CHECK(!finger_sample(TIDE_TOUCH_ENDED, NONE, 20.0f, 47.0f));
    TIDE_CHECK(sampled_frame() == 2);
    TIDE_CHECK(!finger_sample(NONE, NONE, 0.0f, 0.0f));
    TIDE_CHECK(sampled_frame() == 0);

    TIDE_CHECK(!finger_sample(TIDE_TOUCH_BEGAN, TIDE_TOUCH_ENDED, 20.0f, 20.0f));
    TIDE_CHECK(sampled_frame() == 0);
    TIDE_CHECK(!finger_sample(NONE, NONE, 0.0f, 0.0f));
    TIDE_CHECK(sampled_frame() == 1);
    TIDE_CHECK(!finger_sample(NONE, NONE, 0.0f, 0.0f));
    TIDE_CHECK(sampled_frame() == 0);

    TIDE_CHECK(finger_sample(TIDE_TOUCH_BEGAN, NONE, 900.0f, 900.0f));
    TIDE_CHECK(sampled_frame() == 4);
}

TIDE_TEST(gui_views_see_a_finger_lift_whenever_the_game_samples)
{
    const tide_touch *primary = &gui.devices.touchscreen.primaryTouch;
    const tide_touch *slot = &gui.devices.touchscreen.touches.at[0];
    const tide_pointer *pointer = &gui.devices.pointer;

    start();
    finger_sample(TIDE_TOUCH_BEGAN, NONE, 900.0f, 600.0f);
    sampled_frame();
    TIDE_CHECK(primary->press.down && slot->press.down && primary->id == 1);
    finger_sample(TIDE_TOUCH_MOVED, NONE, 910.0f, 600.0f);
    sampled_frame();
    TIDE_CHECK(primary->press.pressed && !primary->press.down && !primary->press.up);
    finger_sample(TIDE_TOUCH_ENDED, NONE, 910.0f, 600.0f);
    TIDE_CHECK(devices.touchscreen.primaryTouch.id == 0);
    sampled_frame();
    TIDE_CHECK(primary->press.up && !primary->press.pressed && !primary->press.down && primary->id == 1);
    TIDE_CHECK(slot->press.up && !slot->press.pressed && slot->id == 1);
    TIDE_CHECK(primary->position.x == 910.0f && slot->position.x == 910.0f);
    TIDE_CHECK(pointer->press.up && pointer->position.x == 910.0f && pointer->position.y == 480.0f);
    finger_sample(NONE, NONE, 0.0f, 0.0f);
    sampled_frame();
    TIDE_CHECK(!primary->press.up && !slot->press.up && primary->id == 0 && slot->id == 0);
    TIDE_CHECK(!pointer->press.up && pointer->position.x == 910.0f);

    finger_sample(TIDE_TOUCH_BEGAN, TIDE_TOUCH_ENDED, 900.0f, 600.0f);
    sampled_frame();
    TIDE_CHECK(primary->press.down && !primary->press.up && slot->press.down);
    finger_sample(NONE, NONE, 0.0f, 0.0f);
    sampled_frame();
    TIDE_CHECK(primary->press.up && !primary->press.down && slot->press.up && primary->id == 2);

    start();
    touch_frame(TIDE_TOUCH_BEGAN, NONE, 900.0f, 600.0f);
    TIDE_CHECK(primary->press.down && slot->press.down);
    touch_frame(TIDE_TOUCH_ENDED, NONE, 900.0f, 600.0f);
    TIDE_CHECK(primary->press.up && slot->press.up && !primary->press.pressed && primary->id == 1);
    touch_frame(NONE, NONE, 0.0f, 0.0f);
    TIDE_CHECK(!primary->press.up && !slot->press.up);
}

TIDE_TEST(gui_where_the_mouse_is_now_says_whose_click_it_is)
{
    start();
    TIDE_CHECK(!mouse_sample(900.0f, 900.0f, false));
    TIDE_CHECK(sampled_frame() == 0);
    TIDE_CHECK(!mouse_sample(20.0f, 47.0f, true));
    TIDE_CHECK(sampled_frame() == 0);
    TIDE_CHECK(gui.active == 20);
    TIDE_CHECK(!mouse_sample(20.0f, 47.0f, false));
    TIDE_CHECK(sampled_frame() == 2);

    TIDE_CHECK(!mouse_sample(20.0f, 47.0f, false));
    TIDE_CHECK(sampled_frame() == 0);
    TIDE_CHECK(mouse_sample(900.0f, 900.0f, true));
    TIDE_CHECK(sampled_frame() == 4);
}

TIDE_TEST(gui_a_locked_cursor_is_on_no_widget)
{
    start();
    TIDE_CHECK(!mouse_sample(20.0f, 47.0f, false));
    TIDE_CHECK(sampled_frame() == 0);
    TIDE_CHECK(gui.hot == 20);

    devices.mouse.locked = true;
    TIDE_CHECK(mouse_sample(20.0f, 47.0f, true));
    TIDE_CHECK(sampled_frame() == 4);
    TIDE_CHECK(gui.hot == 0 && gui.active == 0);
    TIDE_CHECK(gui.devices.mouse.locked);
    TIDE_CHECK(mouse_sample(20.0f, 47.0f, false));
    TIDE_CHECK(sampled_frame() == 4);

    TIDE_CHECK(mouse_sample(20.0f, 47.0f, true));
    TIDE_CHECK(sampled_frame() == 4);
    devices.mouse.locked = false;
    mouse_sample(20.0f, 47.0f, true);
    TIDE_CHECK(sampled_frame() == 0);
    TIDE_CHECK(gui.active == 0);
    TIDE_CHECK(!mouse_sample(20.0f, 47.0f, false));
    TIDE_CHECK(sampled_frame() == 0);

    TIDE_CHECK(!mouse_sample(20.0f, 47.0f, true));
    TIDE_CHECK(sampled_frame() == 0);
    TIDE_CHECK(gui.active == 20);
    TIDE_CHECK(!mouse_sample(20.0f, 47.0f, false));
    TIDE_CHECK(sampled_frame() == 2);

    devices.mouse.locked = true;
    TIDE_CHECK(!finger_sample(TIDE_TOUCH_BEGAN, NONE, 20.0f, 20.0f));
    TIDE_CHECK(sampled_frame() == 0);
    TIDE_CHECK(gui.active == 10);
    TIDE_CHECK(!finger_sample(TIDE_TOUCH_ENDED, NONE, 20.0f, 20.0f));
    TIDE_CHECK(sampled_frame() == 1);
}

// A frame with a button in an anchored area, if it's `up`, and a disabled
// button at a rect. Returns whether views read a press.
static bool area_frame(const bool up)
{
    tide_gui_begin(&gui, &devices, tide_f2(1920.0f, 1080.0f), NULL);
    const bool read = views_read_a_press();
    if (up) {
        const int area = tide_gui_begin_area_at(&gui, 99, TIDE_ANCHOR_MIDDLE_CENTER);
        tide_gui_layout_button(&gui, 1, "Play");
        tide_gui_close(&gui, area);
    }
    const int off = tide_gui_begin_disabled(&gui, true);
    tide_gui_button(&gui, 2, (tide_rect){100.0f, 100.0f, 200.0f, 40.0f}, "Locked");
    tide_gui_close(&gui, off);
    end();
    return read;
}

TIDE_TEST(gui_a_finger_on_an_area_or_a_disabled_widget_is_never_the_games)
{
    start();
    for (int frame = 0; frame < 3; frame++) {
        finger_sample(NONE, NONE, 0.0f, 0.0f);
        area_frame(true);
    }
    TIDE_CHECK(!finger_sample(TIDE_TOUCH_BEGAN, NONE, 962.0f, 518.0f));
    TIDE_CHECK(!area_frame(true));
    TIDE_CHECK(gui.active == 0);
    TIDE_CHECK(!finger_sample(TIDE_TOUCH_ENDED, NONE, 962.0f, 518.0f));
    TIDE_CHECK(!area_frame(true));
    TIDE_CHECK(!finger_sample(TIDE_TOUCH_BEGAN, NONE, 150.0f, 120.0f));
    TIDE_CHECK(!area_frame(true));
    TIDE_CHECK(!finger_sample(TIDE_TOUCH_ENDED, NONE, 150.0f, 120.0f));
    TIDE_CHECK(!area_frame(true));

    finger_sample(NONE, NONE, 0.0f, 0.0f);
    area_frame(false);
    TIDE_CHECK(finger_sample(TIDE_TOUCH_BEGAN, NONE, 962.0f, 518.0f));
    TIDE_CHECK(area_frame(true));
    TIDE_CHECK(!finger_sample(NONE, NONE, 0.0f, 0.0f));
    TIDE_CHECK(!area_frame(true));
}

// A frame's GUI after a sample: a menu in the middle of the screen, a title
// over two buttons. Returns which were pressed (bits 0 and 1).
static int titled_frame(void)
{
    tide_gui_begin(&gui, &devices, tide_f2(1920.0f, 1080.0f), NULL);
    const int area = tide_gui_begin_area_at(&gui, 99, TIDE_ANCHOR_MIDDLE_CENTER);
    tide_gui_layout_label(&gui, "Tag");
    const bool a = tide_gui_layout_button(&gui, 10, "Play");
    const bool b = tide_gui_layout_button(&gui, 20, "Quit");
    tide_gui_close(&gui, area);
    end();
    return (a ? 1 : 0) | (b ? 2 : 0);
}

TIDE_TEST(gui_a_finger_presses_a_button_of_the_menu_the_mouse_rests_on)
{
    start();
    for (int frame = 0; frame < 3; frame++) {
        TIDE_CHECK(!mouse_sample(960.0f, 507.0f, false));
        TIDE_CHECK(titled_frame() == 0);
    }
    TIDE_CHECK(gui.hot == 99);
    TIDE_CHECK(!finger_sample(TIDE_TOUCH_BEGAN, NONE, 960.0f, 540.0f));
    TIDE_CHECK(titled_frame() == 0);
    TIDE_CHECK(gui.active == 10);
    TIDE_CHECK(!finger_sample(TIDE_TOUCH_ENDED, NONE, 960.0f, 540.0f));
    TIDE_CHECK(titled_frame() == 1);
    TIDE_CHECK(!finger_sample(NONE, NONE, 0.0f, 0.0f));
    TIDE_CHECK(titled_frame() == 0);

    TIDE_CHECK(!mouse_sample(960.0f, 510.0f, false));
    TIDE_CHECK(titled_frame() == 0);
    TIDE_CHECK(gui.hot == 99);
    TIDE_CHECK(!finger_sample(TIDE_TOUCH_BEGAN, TIDE_TOUCH_ENDED, 960.0f, 573.0f));
    TIDE_CHECK(titled_frame() == 0);
    TIDE_CHECK(gui.active == 20);
    TIDE_CHECK(!finger_sample(NONE, NONE, 0.0f, 0.0f));
    TIDE_CHECK(titled_frame() == 2);

    TIDE_CHECK(!mouse_sample(960.0f, 507.0f, false));
    TIDE_CHECK(titled_frame() == 0);
    TIDE_CHECK(gui.hot == 99);
    TIDE_CHECK(!mouse_sample(960.0f, 573.0f, true));
    TIDE_CHECK(titled_frame() == 0);
    TIDE_CHECK(gui.active == 20);
    TIDE_CHECK(!mouse_sample(960.0f, 573.0f, false));
    TIDE_CHECK(titled_frame() == 2);
}

// A frame's GUI after a sample, with widgets over one another: a view's
// control with a button on it, and a button with an area's panel over it,
// which has a button of its own.
static void stacked_frame(void)
{
    tide_gui_begin(&gui, &devices, tide_f2(1920.0f, 1080.0f), NULL);
    tide_gui_control(&gui, 1, (tide_rect){0.0f, 0.0f, 400.0f, 400.0f});
    tide_gui_button(&gui, 2, (tide_rect){100.0f, 100.0f, 200.0f, 40.0f}, "On the control");
    tide_gui_button(&gui, 3, (tide_rect){900.0f, 600.0f, 200.0f, 40.0f}, "Under the panel");
    const int area = tide_gui_begin_area(&gui, 99, (tide_rect){800.0f, 400.0f, 400.0f, 300.0f});
    tide_gui_layout_button(&gui, 4, "On the panel");
    tide_gui_close(&gui, area);
    end();
}

TIDE_TEST(gui_a_finger_presses_the_widget_on_top)
{
    start();
    for (int frame = 0; frame < 2; frame++) {
        finger_sample(NONE, NONE, 0.0f, 0.0f);
        stacked_frame();
    }
    TIDE_CHECK(!finger_sample(TIDE_TOUCH_BEGAN, NONE, 150.0f, 120.0f));
    stacked_frame();
    TIDE_CHECK(gui.active == 2);
    finger_sample(TIDE_TOUCH_ENDED, NONE, 150.0f, 120.0f);
    stacked_frame();
    TIDE_CHECK(!finger_sample(TIDE_TOUCH_BEGAN, NONE, 50.0f, 50.0f));
    stacked_frame();
    TIDE_CHECK(gui.active == 1);
    finger_sample(TIDE_TOUCH_ENDED, NONE, 50.0f, 50.0f);
    stacked_frame();
    TIDE_CHECK(!finger_sample(TIDE_TOUCH_BEGAN, NONE, 950.0f, 620.0f));
    stacked_frame();
    TIDE_CHECK(gui.active == 0);
    finger_sample(TIDE_TOUCH_ENDED, NONE, 950.0f, 620.0f);
    stacked_frame();
    TIDE_CHECK(!finger_sample(TIDE_TOUCH_BEGAN, NONE, 820.0f, 420.0f));
    stacked_frame();
    TIDE_CHECK(gui.active == 4);
}

// A frame's GUI after a sample: the Play button, and two views, the first with
// a widget of its own at `rect`, which it says each frame. Returns whether the
// first view read a press (bit 0), and the second (bit 1).
static int widget_frame(const tide_rect rect)
{
    int read = 0;
    tide_gui_begin(&gui, &devices, tide_f2(1920.0f, 1080.0f), NULL);
    tide_gui_view(&gui, 1);
    if (views_read_a_press()) read |= 1;
    tide_gui_claim_pointer_at(&gui, rect);
    tide_gui_view(&gui, 2);
    if (views_read_a_press()) read |= 2;
    tide_gui_layout_button(&gui, 10, "Play");
    end();
    return read;
}

TIDE_TEST(gui_a_press_on_a_views_own_widget_is_the_views)
{
    start();
    const tide_rect widget = {0.0f, 0.0f, 400.0f, 400.0f};
    TIDE_CHECK(!finger_sample(NONE, NONE, 0.0f, 0.0f));
    TIDE_CHECK(widget_frame(widget) == 0);
    TIDE_CHECK(!finger_sample(TIDE_TOUCH_BEGAN, NONE, 300.0f, 300.0f));
    TIDE_CHECK(widget_frame(widget) == 1);
    TIDE_CHECK(!finger_sample(NONE, NONE, 0.0f, 0.0f));
    TIDE_CHECK(widget_frame(widget) == 1);
    TIDE_CHECK(finger_sample(TIDE_TOUCH_MOVED, NONE, 900.0f, 900.0f));
    TIDE_CHECK(widget_frame(widget) == 3);
    finger_sample(TIDE_TOUCH_ENDED, NONE, 900.0f, 900.0f);
    widget_frame(widget);

    TIDE_CHECK(!finger_sample(TIDE_TOUCH_BEGAN, NONE, 20.0f, 14.0f));
    TIDE_CHECK(widget_frame(widget) == 0);
    TIDE_CHECK(gui.active == 10);
    finger_sample(TIDE_TOUCH_ENDED, NONE, 20.0f, 14.0f);
    widget_frame(widget);

    TIDE_CHECK(!mouse_sample(900.0f, 900.0f, false));
    TIDE_CHECK(widget_frame(widget) == 0);
    TIDE_CHECK(!mouse_sample(300.0f, 300.0f, true));
    TIDE_CHECK(widget_frame(widget) == 1);
    TIDE_CHECK(!mouse_sample(300.0f, 300.0f, true));
    TIDE_CHECK(widget_frame((tide_rect){0}) == 1);
    TIDE_CHECK(mouse_sample(300.0f, 300.0f, true));
    TIDE_CHECK(widget_frame((tide_rect){0}) == 3);
}

TIDE_TEST(gui_places_claimed_past_the_limit_hide_a_frame_late)
{
    start();
    const tide_rect widget = {800.0f, 800.0f, 100.0f, 100.0f};
    for (int frame = 0; frame < 3; frame++) {
        mouse_sample(850.0f, 850.0f, frame == 2);
        tide_gui_begin(&gui, &devices, tide_f2(1920.0f, 1080.0f), NULL);
        tide_gui_view(&gui, 1);
        for (uint32_t i = 0; i < TIDE_GUI_MAX_CLAIM_RECTS; i++) {
            tide_gui_claim_pointer_at(&gui, (tide_rect){(float)i, 0.0f, 1.0f, 1.0f});
        }
        tide_gui_claim_pointer_at(&gui, widget);
        end();
        TIDE_CHECK(gui.taken == TIDE_GUI_POINTER);
    }
    TIDE_CHECK(!mouse_sample(850.0f, 850.0f, true));
}

#undef NONE

// A frame of more buttons than a frame remembers the places of, 28 wide, in
// rows of 64.
static void many_buttons(void)
{
    tide_gui_begin(&gui, &devices, tide_f2(1920.0f, 1080.0f), NULL);
    for (uint32_t i = 0; i < TIDE_GUI_MAX_RECTS + 6; i++) {
        tide_gui_button(&gui, i + 1, (tide_rect){(float)(i % 64) * 30.0f, (float)(i / 64) * 30.0f, 28.0f, 28.0f}, "");
    }
    end();
}

TIDE_TEST(gui_widgets_past_those_remembered_hide_a_frame_late)
{
    start();
    const uint32_t last = TIDE_GUI_MAX_RECTS + 5;
    const float x = (float)(last % 64) * 30.0f + 10.0f;
    const float y = (float)(last / 64) * 30.0f + 10.0f;
    mouse_sample(1900.0f, 1000.0f, false);
    many_buttons();
    TIDE_CHECK(gui.rects_full);
    TIDE_CHECK(!mouse_sample(40.0f, 10.0f, true));
    many_buttons();
    mouse_sample(40.0f, 10.0f, false);
    many_buttons();
    TIDE_CHECK(!mouse_sample(x, y, false));
    many_buttons();
    TIDE_CHECK(gui.over && gui.active == 0);
    TIDE_CHECK(!mouse_sample(x, y, true));
}

// A frame of a big button, as many small ones as a frame remembers the places
// of, and one more on the big one, whose place isn't remembered.
static void button_past_those_remembered(void)
{
    tide_gui_begin(&gui, &devices, tide_f2(1920.0f, 1080.0f), NULL);
    tide_gui_button(&gui, 5000, (tide_rect){0.0f, 600.0f, 400.0f, 300.0f}, "");
    for (uint32_t i = 0; i + 1 < TIDE_GUI_MAX_RECTS; i++) {
        tide_gui_button(&gui, i + 1, (tide_rect){(float)(i % 64) * 30.0f, (float)(i / 64) * 30.0f, 28.0f, 28.0f}, "");
    }
    tide_gui_button(&gui, 6000, (tide_rect){100.0f, 700.0f, 100.0f, 40.0f}, "");
    end();
}

TIDE_TEST(gui_a_widget_past_those_remembered_is_on_top_a_frame_late)
{
    start();
    mouse_sample(150.0f, 720.0f, false);
    button_past_those_remembered();
    TIDE_CHECK(gui.rects_full && gui.hot == 6000);
    TIDE_CHECK(!mouse_sample(150.0f, 720.0f, true));
    button_past_those_remembered();
    TIDE_CHECK(gui.active == 6000);
}

TIDE_TEST(gui_mouse_over_the_gui_is_hidden)
{
    start();
    mouse(20.0f, 20.0f, false);
    two_buttons();
    tide_devices sampled = devices;
    tide_button_set(&sampled.mouse.left, true);
    tide_gui_hide(&gui, &sampled);
    TIDE_CHECK(!sampled.mouse.left.pressed);

    mouse(900.0f, 900.0f, false);
    two_buttons();
    sampled = devices;
    tide_button_set(&sampled.mouse.left, true);
    tide_gui_hide(&gui, &sampled);
    TIDE_CHECK(sampled.mouse.left.pressed);
}

// A frame with an int field under a label column.
static bool int_field(int32_t *value)
{
    begin();
    const bool changed = tide_gui_layout_int_field(&gui, 5, "Players", value);
    end();
    return changed;
}

TIDE_TEST(gui_typing_into_a_field)
{
    start();
    int32_t players = 4;
    mouse(170.0f, 14.0f, false);
    int_field(&players);
    mouse(170.0f, 14.0f, true);
    int_field(&players);
    TIDE_CHECK(gui.editing == 5);
    mouse(170.0f, 14.0f, false);
    type("1x2");
    TIDE_CHECK(!int_field(&players));
    TIDE_CHECK(players == 4);
    key(&devices.keyboard.enter, true);
    TIDE_CHECK(int_field(&players));
    TIDE_CHECK(players == 12);
    TIDE_CHECK(gui.editing == 0);
    key(&devices.keyboard.enter, false);

    key(&devices.keyboard.enter, true);
    int_field(&players);
    key(&devices.keyboard.enter, false);
    type("99");
    int_field(&players);
    key(&devices.keyboard.escape, true);
    TIDE_CHECK(!int_field(&players));
    TIDE_CHECK(players == 12);
    key(&devices.keyboard.escape, false);

    key(&devices.keyboard.rightArrow, true);
    TIDE_CHECK(int_field(&players));
    TIDE_CHECK(players == 13);
}

TIDE_TEST(gui_anchored_area_centers_its_content)
{
    start();
    for (int frame = 0; frame < 2; frame++) {
        begin();
        const int depth = tide_gui_begin_area_at(&gui, 99, TIDE_ANCHOR_MIDDLE_CENTER);
        tide_gui_layout_button(&gui, 1, "Play");
        tide_gui_close(&gui, depth);
        end();
    }
    TIDE_REQUIRE(draw.count >= 4);
    // Its shadow first, a quarter of its size lower, then the panel and the button
    TIDE_CHECK(draw.commands[1].kind == TIDE_DRAW_SHAPE && draw.commands[1].a.x == 960.0f && draw.commands[1].a.y == 544.0f);
    TIDE_CHECK(draw.commands[2].a.x == 960.0f && draw.commands[2].a.y == 540.0f);
    TIDE_CHECK(draw.commands[3].a.x == 960.0f && draw.commands[3].a.y == 540.0f);
    TIDE_CHECK(draw.commands[2].b.y == 28.0f + 2.0f * 12.0f);
}

// The draw list's rects, in the GUI's order: each one's left and right edges.
static void rect_edges(const int index, float *left, float *right)
{
    int seen = 0;
    for (uint32_t i = 0; i < draw.count; i++) {
        const tide_draw_command *c = &draw.commands[i];
        if (c->kind != TIDE_DRAW_RECT || seen++ != index) continue;
        *left = c->a.x - c->b.x * 0.5f;
        *right = c->a.x + c->b.x * 0.5f;
        return;
    }
    *left = *right = -1.0f;
}

TIDE_TEST(gui_rows_shrink_to_fit_the_screen)
{
    start();
    tide_str local = TIDE_STR_EMPTY;
    const tide_textref ip = {.local = &local};
    for (int frame = 0; frame < 3; frame++) {
        tide_gui_begin(&gui, &devices, tide_f2(400.0f, 600.0f), NULL);
        const int area = tide_gui_begin_area_at(&gui, 99, TIDE_ANCHOR_MIDDLE_CENTER);
        tide_gui_layout_button(&gui, 1, "Start Host");
        const int row = tide_gui_begin_horizontal(&gui, 2);
        tide_gui_layout_text_field(&gui, 3, "IP", ip);
        tide_gui_layout_button(&gui, 4, "Connect");
        tide_gui_close(&gui, row);
        tide_gui_close(&gui, area);
        end();
    }
    float left, right;
    rect_edges(0, &left, &right);
    TIDE_CHECK(left > 11.99f && left < 12.01f);
    TIDE_CHECK(right > 387.99f && right < 388.01f);
    rect_edges(1, &left, &right);
    TIDE_CHECK(left > 23.99f && right < 376.01f && right > 375.99f);
    rect_edges(2, &left, &right);
    TIDE_CHECK(right - left > 179.99f && right - left < 180.01f);
    TIDE_CHECK(left > 24.0f + 19.2f + 10.0f);
    rect_edges(3, &left, &right);
    TIDE_CHECK(right - left < 87.2f && right - left > 77.2f);
    TIDE_CHECK(right > 375.99f && right < 376.01f);
}

TIDE_TEST(gui_horizontal_groups_sit_side_by_side)
{
    start();
    begin();
    const int depth = tide_gui_begin_horizontal(&gui, 3);
    tide_gui_layout_button(&gui, 1, "A");
    tide_gui_layout_button(&gui, 2, "B");
    tide_gui_close(&gui, depth);
    tide_gui_layout_button(&gui, 4, "C");
    end();
    TIDE_REQUIRE(draw.count >= 7);
    const tide_draw_command *a = &draw.commands[1];
    const tide_draw_command *b = &draw.commands[3];
    const tide_draw_command *c = &draw.commands[5];
    TIDE_CHECK(a->a.y == b->a.y && b->a.x > a->a.x);
    TIDE_CHECK(c->a.y == a->a.y + 28.0f + 5.0f);
}

TIDE_TEST(gui_colliding_containers_keep_their_layout_sizes)
{
    start();
    const uint32_t first = 17, second = first + TIDE_GUI_MAX_SIZES;
    const float first_width = tide_draw_guess_text("XXXXXXXXXX", 16.0f, NULL, TIDE_FONT_REGULAR).x + 20.0f;
    const float second_width = tide_draw_guess_text("YYYYYYYYYYYYYYYY", 16.0f, NULL, TIDE_FONT_REGULAR).x + 20.0f;
    for (int frame = 0; frame < 4; frame++) {
        begin();
        int depth = tide_gui_begin_vertical(&gui, first);
        if (frame) TIDE_CHECK(gui.groups[gui.depth].stretch == first_width);
        tide_gui_layout_button(&gui, 10, "A");
        tide_gui_layout_button(&gui, 20, "XXXXXXXXXX");
        tide_gui_close(&gui, depth);
        depth = tide_gui_begin_vertical(&gui, second);
        if (frame) TIDE_CHECK(gui.groups[gui.depth].stretch == second_width);
        tide_gui_layout_button(&gui, 30, "B");
        tide_gui_layout_button(&gui, 40, "YYYYYYYYYYYYYYYY");
        tide_gui_close(&gui, depth);
        end();
        if (frame) {
            float left, right;
            rect_edges(0, &left, &right);
            TIDE_CHECK(right - left == first_width);
            rect_edges(2, &left, &right);
            TIDE_CHECK(right - left == second_width);
        }
    }
}

TIDE_TEST(gui_size_cache_reuses_old_containers_when_full)
{
    start();
    begin();
    for (uint32_t i = 0; i < TIDE_GUI_MAX_SIZES - 1; i++) {
        const int depth = tide_gui_begin_vertical(&gui, 2 + i);
        tide_gui_layout_button(&gui, 1000 + i, "Old");
        tide_gui_close(&gui, depth);
    }
    end();
    for (int frame = 0; frame < 3; frame++) {
        begin();
        const int depth = tide_gui_begin_vertical(&gui, 1 + TIDE_GUI_MAX_SIZES);
        if (frame) TIDE_CHECK(gui.groups[gui.depth].stretch > 0.0f);
        tide_gui_layout_button(&gui, 10, "New");
        tide_gui_close(&gui, depth);
        end();
    }
    begin();
    TIDE_CHECK(gui.groups[0].stretch > 0.0f);
    end();
}

// A frame with Play, Quit in a Disabled block, then Back; returns which were
// pressed (bits 0 to 2).
static int quit_disabled(const bool disabled)
{
    begin();
    const bool a = tide_gui_layout_button(&gui, 10, "Play");
    const int depth = tide_gui_begin_disabled(&gui, disabled);
    const bool b = tide_gui_layout_button(&gui, 20, "Quit");
    tide_gui_close(&gui, depth);
    const bool c = tide_gui_layout_button(&gui, 30, "Back");
    end();
    return (a ? 1 : 0) | (b ? 2 : 0) | (c ? 4 : 0);
}

TIDE_TEST(gui_disabled_widgets_dont_work)
{
    start();
    mouse(20.0f, 47.0f, false);
    TIDE_CHECK(quit_disabled(true) == 0);
    mouse(20.0f, 47.0f, true);
    TIDE_CHECK(quit_disabled(true) == 0);
    TIDE_CHECK(gui.active == 0);
    tide_devices sampled = devices;
    tide_button_set(&sampled.mouse.left, true);
    tide_gui_hide(&gui, &sampled);
    TIDE_CHECK(!sampled.mouse.left.pressed);
    mouse(20.0f, 47.0f, false);
    TIDE_CHECK(quit_disabled(true) == 0);

    TIDE_REQUIRE(draw.count >= 7);
    const tide_draw_command *play = &draw.commands[1];
    const tide_draw_command *quit = &draw.commands[3];
    const tide_draw_command *back = &draw.commands[5];
    TIDE_CHECK(tide_test_color_is(quit->color, quit->color.r, quit->color.g, quit->color.b, play->color.a * 0.5f));
    TIDE_CHECK(back->color.a == play->color.a);
    TIDE_CHECK(draw.commands[4].kind == TIDE_DRAW_TEXT && draw.commands[4].color.a == 0.5f);
    TIDE_CHECK(back->a.y == quit->a.y + 28.0f + 5.0f);

    key(&devices.keyboard.tab, true);
    quit_disabled(true);
    TIDE_CHECK(gui.focus == 10);
    key(&devices.keyboard.tab, false);
    quit_disabled(true);
    key(&devices.keyboard.tab, true);
    quit_disabled(true);
    TIDE_CHECK(gui.focus == 30);
    key(&devices.keyboard.tab, false);

    mouse(20.0f, 47.0f, true);
    TIDE_CHECK(quit_disabled(false) == 0);
    TIDE_CHECK(gui.active == 20);
    mouse(20.0f, 47.0f, false);
    TIDE_CHECK(quit_disabled(false) == 2);

    mouse(20.0f, 47.0f, true);
    quit_disabled(false);
    TIDE_CHECK(gui.active == 20);
    quit_disabled(true);
    TIDE_CHECK(gui.active == 0);
    mouse(20.0f, 47.0f, false);
    TIDE_CHECK(quit_disabled(false) == 0);
}

// A frame with an int field, in a Disabled block.
static bool int_field_disabled(int32_t *value, const bool disabled)
{
    begin();
    const int depth = tide_gui_begin_disabled(&gui, disabled);
    const bool changed = tide_gui_layout_int_field(&gui, 5, "Players", value);
    tide_gui_close(&gui, depth);
    end();
    return changed;
}

TIDE_TEST(gui_disabled_fields_stop_typing)
{
    start();
    int32_t players = 4;
    mouse(170.0f, 14.0f, false);
    int_field_disabled(&players, false);
    mouse(170.0f, 14.0f, true);
    int_field_disabled(&players, false);
    TIDE_CHECK(gui.editing == 5 && gui.focus == 5);
    mouse(170.0f, 14.0f, false);
    type("9");
    int_field_disabled(&players, false);

    TIDE_CHECK(!int_field_disabled(&players, true));
    TIDE_CHECK(players == 4);
    TIDE_CHECK(gui.editing == 0 && gui.focus == 0);
    key(&devices.keyboard.enter, true);
    TIDE_CHECK(!int_field_disabled(&players, false));
    TIDE_CHECK(gui.editing == 0);
}

TIDE_TEST(gui_disabled_blocks_nest)
{
    start();
    begin();
    const int outer = tide_gui_begin_disabled(&gui, true);
    tide_gui_begin_disabled(&gui, false);
    TIDE_CHECK(gui.groups[gui.depth].disabled);
    tide_gui_begin_horizontal(&gui, 1);
    TIDE_CHECK(gui.groups[gui.depth].disabled);
    tide_gui_close(&gui, outer);
    TIDE_CHECK(gui.depth == 0 && !gui.groups[0].disabled);
    end();

    begin();
    const int row = tide_gui_begin_horizontal(&gui, 3);
    tide_gui_layout_button(&gui, 1, "A");
    const int off = tide_gui_begin_disabled(&gui, true);
    tide_gui_layout_button(&gui, 2, "B");
    tide_gui_close(&gui, off);
    tide_gui_layout_button(&gui, 4, "C");
    tide_gui_close(&gui, row);
    tide_gui_layout_button(&gui, 5, "D");
    end();
    TIDE_REQUIRE(draw.count >= 8);
    const tide_draw_command *a = &draw.commands[1];
    const tide_draw_command *b = &draw.commands[3];
    const tide_draw_command *c = &draw.commands[5];
    const tide_draw_command *d = &draw.commands[7];
    TIDE_CHECK(a->a.y == b->a.y && b->a.y == c->a.y);
    TIDE_CHECK(a->a.x < b->a.x && b->a.x < c->a.x);
    TIDE_CHECK(d->a.y == a->a.y + 28.0f + 5.0f);
}

TIDE_TEST(gui_close_ends_what_was_left_open)
{
    start();
    begin();
    const int outer = tide_gui_begin_vertical(&gui, 1);
    tide_gui_begin_horizontal(&gui, 2);
    tide_gui_close(&gui, outer);
    TIDE_CHECK(gui.depth == 0);
    tide_gui_begin_vertical(&gui, 3);
    end();
    TIDE_CHECK(gui.depth == 0);
}

TIDE_TEST(gui_views_read_what_is_typed)
{
    start();
    int32_t players = 4;
    mouse(900.0f, 900.0f, false);
    devices.keyboard.text.count = 3;
    devices.keyboard.text.chars[0] = 'h';
    devices.keyboard.text.chars[1] = 0xE9;
    devices.keyboard.text.chars[2] = 0xD800;
    begin();
    tide_gui_layout_int_field(&gui, 5, "Players", &players);
    const tide_str typed = tide_str_typed(&gui.devices.keyboard.text);
    TIDE_CHECK(typed.bytes == 3 && typed.chars == 2 && memcmp(typed.ptr, "h\xC3\xA9", 3) == 0);
    tide_devices sampled = devices;
    tide_gui_hide(&gui, &sampled);
    TIDE_CHECK(sampled.keyboard.text.count == 0);
    end();
    begin();
    tide_gui_layout_int_field(&gui, 5, "Players", &players);
    TIDE_CHECK(tide_str_typed(&gui.devices.keyboard.text).bytes == 0);
    end();

    mouse(170.0f, 14.0f, true);
    int_field(&players);
    mouse(170.0f, 14.0f, false);
    TIDE_CHECK(gui.editing == 5);
    type("12");
    begin();
    tide_gui_layout_int_field(&gui, 5, "Players", &players);
    TIDE_CHECK(gui.devices.keyboard.text.count == 0);
    end();
}

// What `d` has of the mouse's left button, the pointer's press and the scroll
// (bits 0 to 2), the W key (bit 3) and what's typed (bit 4).
static int has(const tide_devices *d)
{
    return (d->mouse.left.pressed ? 1 : 0) | (d->pointer.press.pressed ? 2 : 0) | (d->mouse.scroll.y != 0.0f ? 4 : 0)
         | (d->keyboard.w.pressed ? 8 : 0) | (d->keyboard.text.count ? 16 : 0);
}

#define POINTER 7  // has: the pointer's bits
#define KEYBOARD 24 // ...and the keyboard's

// A frame of two views, the first with a widget of its own that it claims
// `what` for: what each read, the first view's in the low byte and the
// second's in the next.
static int two_views(const uint32_t what)
{
    int seen = 0;
    begin();
    for (uint32_t view = 1; view <= 2; view++) {
        tide_gui_view(&gui, view);
        seen |= has(&gui.devices) << (8 * (view - 1));
        if (view == 1 && (what & TIDE_GUI_POINTER)) tide_gui_claim_pointer(&gui);
        if (view == 1 && (what & TIDE_GUI_KEYBOARD)) tide_gui_claim_keyboard(&gui);
    }
    end();
    return seen;
}

// What the game's next sample has. What's typed is never in one.
static int sample(void)
{
    tide_pointer_poll(&devices, true);
    tide_devices d = devices;
    tide_gui_hide(&gui, &d);
    return has(&d);
}

// The mouse pressed and scrolled away from the GUI, W held and typed.
static void use_everything(void)
{
    mouse(900.0f, 900.0f, true);
    devices.mouse.poll_scroll = devices.mouse.scroll = tide_f2(0.0f, 1.0f);
    key(&devices.keyboard.w, true);
    type("w");
}

TIDE_TEST(gui_claims_hide_the_pointer_from_the_game_and_other_views)
{
    start();
    mouse(900.0f, 900.0f, false);
    TIDE_CHECK(two_views(TIDE_GUI_POINTER) == 0);
    use_everything();
    TIDE_CHECK(sample() == 8);
    TIDE_CHECK(two_views(TIDE_GUI_POINTER) == ((POINTER | KEYBOARD) | KEYBOARD << 8));
    TIDE_CHECK(sample() == 8);
    tide_devices sampled = devices;
    tide_gui_hide(&gui, &sampled);
    TIDE_CHECK(sampled.mouse.position.x == 900.0f && sampled.pointer.position.x == 900.0f);

    use_everything();
    TIDE_CHECK(two_views(0) == ((POINTER | KEYBOARD) | KEYBOARD << 8));
    TIDE_CHECK(sample() == (POINTER | 8));
    use_everything();
    TIDE_CHECK(two_views(0) == ((POINTER | KEYBOARD) | (POINTER | KEYBOARD) << 8));
}

TIDE_TEST(gui_claims_hide_the_keyboard_from_the_game_and_other_views)
{
    start();
    mouse(900.0f, 900.0f, false);
    TIDE_CHECK(two_views(TIDE_GUI_KEYBOARD) == 0);
    use_everything();
    key(&devices.gamepad.buttonSouth, true);
    tide_devices sampled = devices;
    tide_gui_hide(&gui, &sampled);
    TIDE_CHECK(!sampled.keyboard.w.pressed && !sampled.keyboard.w.held);
    TIDE_CHECK(sampled.gamepad.buttonSouth.pressed);
    TIDE_CHECK(sample() == POINTER);
    TIDE_CHECK(two_views(TIDE_GUI_KEYBOARD) == ((POINTER | KEYBOARD) | POINTER << 8));

    use_everything();
    TIDE_CHECK(two_views(TIDE_GUI_KEYBOARD | TIDE_GUI_POINTER) == ((POINTER | KEYBOARD) | POINTER << 8));
    TIDE_CHECK(sample() == 0);
    use_everything();
    TIDE_CHECK(two_views(0) == (POINTER | KEYBOARD));
    use_everything();
    TIDE_CHECK(two_views(0) == ((POINTER | KEYBOARD) | (POINTER | KEYBOARD) << 8));
}

#undef POINTER
#undef KEYBOARD

TIDE_TEST(gui_two_views_that_claim_both_keep_it)
{
    start();
    mouse(900.0f, 900.0f, true);
    for (int frame = 0; frame < 2; frame++) {
        begin();
        for (uint32_t view = 1; view <= 3; view++) {
            tide_gui_view(&gui, view);
            if (frame == 1) TIDE_CHECK(gui.devices.mouse.left.pressed == (view != 3));
            if (view != 3) tide_gui_claim_pointer(&gui);
        }
        end();
    }
    TIDE_CHECK(gui.claim_count == 2 && gui.taken == TIDE_GUI_POINTER);
}

TIDE_TEST(gui_use_comes_before_a_claim)
{
    start();
    for (int frame = 0; frame < 3; frame++) {
        mouse(20.0f, 20.0f, frame > 0);
        begin();
        tide_gui_view(&gui, 1);
        if (frame == 2) TIDE_CHECK(!gui.devices.mouse.left.pressed && !gui.devices.pointer.press.pressed);
        tide_gui_claim_pointer(&gui);
        tide_gui_layout_button(&gui, 10, "Play");
        end();
    }
    TIDE_CHECK(gui.active == 10);
}

TIDE_TEST(gui_claimed_keyboard_keeps_tab_and_the_arrows)
{
    start();
    TIDE_CHECK(two_buttons() == 0);
    for (int frame = 0; frame < 3; frame++) {
        key(&devices.keyboard.tab, frame == 1);
        key(&devices.keyboard.downArrow, frame == 2);
        begin();
        tide_gui_view(&gui, 1);
        tide_gui_claim_keyboard(&gui);
        if (frame == 1) TIDE_CHECK(gui.devices.keyboard.tab.down);
        tide_gui_layout_button(&gui, 10, "Play");
        tide_gui_layout_button(&gui, 20, "Quit");
        end();
        TIDE_CHECK(gui.focus == 0);
    }
    key(&devices.keyboard.downArrow, false);
    TIDE_CHECK(two_buttons() == 0);
    key(&devices.keyboard.tab, true);
    TIDE_CHECK(two_buttons() == 0);
    TIDE_CHECK(gui.focus == 10);
}

TIDE_TEST(gui_views_show_the_phone_keyboard)
{
    start();
    begin();
    end();
    TIDE_CHECK(!tide_gui_typing(&gui));
    begin();
    tide_gui_show_keyboard(&gui);
    end();
    TIDE_CHECK(tide_gui_typing(&gui));
    begin();
    end();
    TIDE_CHECK(!tide_gui_typing(&gui));
}

TIDE_TEST(gui_claims_past_the_limit_still_hide)
{
    start();
    mouse(900.0f, 900.0f, true);
    for (int frame = 0; frame < 2; frame++) {
        begin();
        for (uint32_t view = 1; view <= TIDE_GUI_MAX_CLAIMS + 1; view++) {
            tide_gui_view(&gui, view);
            if (frame == 1) TIDE_CHECK(gui.devices.mouse.left.pressed == (view <= TIDE_GUI_MAX_CLAIMS));
            tide_gui_claim_pointer(&gui);
        }
        end();
    }
    tide_devices sampled = devices;
    tide_gui_hide(&gui, &sampled);
    TIDE_CHECK(!sampled.mouse.left.pressed);
}

TIDE_TEST(gui_views_ask_for_the_cursors_lock)
{
    start();
    begin();
    end();
    TIDE_CHECK(!tide_gui_cursor_lock(&gui));
    begin();
    tide_gui_lock_cursor(&gui);
    end();
    TIDE_CHECK(tide_gui_cursor_lock(&gui));
    begin();
    end();
    TIDE_CHECK(!tide_gui_cursor_lock(&gui));

    bool open = true;
    for (int frame = 0; frame < 2; frame++) {
        begin();
        tide_gui_lock_cursor(&gui);
        const int depth = tide_gui_begin_modal(&gui, 30, TIDE_ANCHOR_MIDDLE_CENTER, &open);
        if (depth >= 0) tide_gui_close(&gui, depth);
        end();
        TIDE_CHECK(tide_gui_cursor_lock(&gui) == !open);
        open = false;
    }
}

// ---------------------------------------------------------------------------
// Controls: widgets a view draws itself, whose input the GUI handles

#define NONE ((tide_touch_phase)TIDE_TOUCHES)

static const tide_rect CARD = {600.0f, 600.0f, 200.0f, 100.0f};

// What a control said.
enum { HOVERED = 1, PRESSED = 2, FOCUSED = 4, CLICKED = 8, READ = 16 };

static int said(const tide_control c)
{
    return (c.hovered ? HOVERED : 0) | (c.pressed ? PRESSED : 0) | (c.focused ? FOCUSED : 0) | (c.clicked ? CLICKED : 0);
}

// The rest of a frame's GUI: Play, a control at CARD and Quit, in that order.
// What the control said.
static int card_widgets(void)
{
    tide_gui_layout_button(&gui, 10, "Play");
    const tide_control card = tide_gui_control(&gui, 50, CARD);
    tide_gui_layout_button(&gui, 20, "Quit");
    end();
    return said(card);
}

static int card_frame(void)
{
    begin();
    return card_widgets();
}

// ...after a sample, in a host's order: and whether views read a press (READ).
static int sampled_card_frame(void)
{
    tide_gui_begin(&gui, &devices, tide_f2(1920.0f, 1080.0f), NULL);
    const int read = views_read_a_press() ? READ : 0;
    return card_widgets() | read;
}

TIDE_TEST(gui_a_control_says_what_the_mouse_does_with_it)
{
    start();
    mouse(900.0f, 900.0f, false);
    TIDE_CHECK(card_frame() == 0);
    TIDE_CHECK(draw.count == 5);
    mouse(650.0f, 650.0f, false);
    TIDE_CHECK(card_frame() == HOVERED);
    TIDE_CHECK(draw.count == 5);
    mouse(650.0f, 650.0f, true);
    TIDE_CHECK(card_frame() == (HOVERED | PRESSED));
    TIDE_CHECK(gui.active == 50);
    mouse(900.0f, 900.0f, true);
    TIDE_CHECK(card_frame() == PRESSED);
    mouse(650.0f, 650.0f, true);
    TIDE_CHECK(card_frame() == (HOVERED | PRESSED));
    mouse(650.0f, 650.0f, false);
    TIDE_CHECK(card_frame() == (HOVERED | CLICKED));
    TIDE_CHECK(card_frame() == HOVERED);

    mouse(650.0f, 650.0f, true);
    card_frame();
    mouse(900.0f, 900.0f, true);
    card_frame();
    mouse(900.0f, 900.0f, false);
    TIDE_CHECK(card_frame() == 0);
    mouse(900.0f, 900.0f, true);
    card_frame();
    mouse(650.0f, 650.0f, true);
    TIDE_CHECK(card_frame() == HOVERED);
    mouse(650.0f, 650.0f, false);
    TIDE_CHECK(card_frame() == HOVERED);
    TIDE_CHECK(gui.focus == 0);
}

TIDE_TEST(gui_a_press_on_a_control_is_never_the_games)
{
    start();
    TIDE_CHECK(!finger_sample(NONE, NONE, 0.0f, 0.0f));
    TIDE_CHECK(sampled_card_frame() == 0);
    TIDE_CHECK(!finger_sample(TIDE_TOUCH_BEGAN, NONE, 650.0f, 650.0f));
    TIDE_CHECK(sampled_card_frame() == (HOVERED | PRESSED));
    TIDE_CHECK(!finger_sample(NONE, NONE, 0.0f, 0.0f));
    TIDE_CHECK(sampled_card_frame() == (HOVERED | PRESSED));
    TIDE_CHECK(!finger_sample(TIDE_TOUCH_ENDED, NONE, 650.0f, 650.0f));
    TIDE_CHECK(sampled_card_frame() == (HOVERED | CLICKED));
    TIDE_CHECK(!finger_sample(NONE, NONE, 0.0f, 0.0f));
    TIDE_CHECK(sampled_card_frame() == 0);

    TIDE_CHECK(!finger_sample(TIDE_TOUCH_BEGAN, TIDE_TOUCH_ENDED, 650.0f, 650.0f));
    TIDE_CHECK(sampled_card_frame() == (HOVERED | PRESSED));
    TIDE_CHECK(!finger_sample(NONE, NONE, 0.0f, 0.0f));
    TIDE_CHECK(sampled_card_frame() == (HOVERED | CLICKED));
    TIDE_CHECK(!finger_sample(NONE, NONE, 0.0f, 0.0f));
    TIDE_CHECK(sampled_card_frame() == 0);

    TIDE_CHECK(!mouse_sample(900.0f, 900.0f, false));
    TIDE_CHECK(sampled_card_frame() == 0);
    TIDE_CHECK(!mouse_sample(650.0f, 650.0f, true));
    TIDE_CHECK(sampled_card_frame() == (HOVERED | PRESSED));
    TIDE_CHECK(!mouse_sample(650.0f, 650.0f, false));
    TIDE_CHECK(sampled_card_frame() == (HOVERED | CLICKED));

    TIDE_CHECK(!mouse_sample(650.0f, 650.0f, false));
    TIDE_CHECK(sampled_card_frame() == HOVERED);
    TIDE_CHECK(mouse_sample(900.0f, 900.0f, true));
    TIDE_CHECK(sampled_card_frame() == READ);
}

// A key or a gamepad's button down for a frame of the card's GUI, then up for
// one: what the control said in the first.
static int card_press(tide_button *b)
{
    key(b, true);
    const int result = card_frame();
    key(b, false);
    card_frame();
    return result;
}

TIDE_TEST(gui_the_keyboard_moves_the_focus_to_controls_and_clicks_them)
{
    start();
    card_frame();
    TIDE_CHECK(card_press(&devices.keyboard.tab) == 0);
    TIDE_CHECK(gui.focus == 10);
    TIDE_CHECK(card_press(&devices.keyboard.tab) == FOCUSED);
    TIDE_CHECK(card_press(&devices.keyboard.enter) == (FOCUSED | CLICKED));
    TIDE_CHECK(card_press(&devices.keyboard.space) == (FOCUSED | CLICKED));
    TIDE_CHECK(card_press(&devices.keyboard.numpadEnter) == (FOCUSED | CLICKED));
    TIDE_CHECK(card_press(&devices.keyboard.upArrow) == 0);
    TIDE_CHECK(gui.focus == 20); // Geometrically above the card, nearer than Play.
    TIDE_CHECK(card_press(&devices.keyboard.rightArrow) == FOCUSED);
    key(&devices.keyboard.leftShift, true);
    TIDE_CHECK(card_press(&devices.keyboard.tab) == 0);
    TIDE_CHECK(gui.focus == 10); // Shift+Tab still follows declaration order.
    key(&devices.keyboard.leftShift, false);
    tide_devices sampled = devices;
    key(&sampled.keyboard.w, true);
    tide_gui_hide(&gui, &sampled);
    TIDE_CHECK(!sampled.keyboard.w.held && !sampled.keyboard.w.pressed);
    TIDE_CHECK(card_press(&devices.keyboard.escape) == 0);
    TIDE_CHECK(gui.focus == 0);
}

TIDE_TEST(gui_a_gamepad_moves_the_focus_to_controls_and_clicks_them)
{
    start();
    devices.gamepad.connected = true;
    card_frame();
    TIDE_CHECK(card_press(&devices.gamepad.dpad.down) == 0);
    TIDE_CHECK(gui.focus == 10);
    devices.gamepad.leftStick = tide_f2(1.0f, 0.0f);
    TIDE_CHECK(card_frame() == FOCUSED);
    TIDE_CHECK(card_frame() == FOCUSED);
    devices.gamepad.leftStick = tide_f2(0.0f, 0.0f);
    card_frame();
    TIDE_CHECK(card_press(&devices.gamepad.buttonSouth) == (FOCUSED | CLICKED));
    TIDE_CHECK(card_press(&devices.gamepad.dpad.up) == 0);
    TIDE_CHECK(gui.focus == 20);
    TIDE_CHECK(card_press(&devices.gamepad.dpad.right) == FOCUSED);
    tide_devices sampled = devices;
    key(&sampled.gamepad.buttonNorth, true);
    tide_gui_hide(&gui, &sampled);
    TIDE_CHECK(!sampled.gamepad.buttonNorth.held && !sampled.gamepad.buttonNorth.pressed);
    TIDE_CHECK(card_press(&devices.gamepad.buttonEast) == 0);
    TIDE_CHECK(gui.focus == 0);
}

// A frame with a slider, an int field and a button: left and right are the
// first two's, to step their values.
static void steppers(float *volume, int32_t *players)
{
    begin();
    tide_gui_layout_slider(&gui, 10, "Volume", volume, 0.0f, 1.0f);
    tide_gui_layout_int_field(&gui, 20, "Players", players);
    tide_gui_layout_button(&gui, 30, "Back");
    end();
}

TIDE_TEST(gui_left_and_right_stay_with_the_widgets_that_step)
{
    start();
    float volume = 0.5f;
    int32_t players = 4;
    steppers(&volume, &players);
    key(&devices.keyboard.rightArrow, true);
    steppers(&volume, &players);
    key(&devices.keyboard.rightArrow, false);
    steppers(&volume, &players);
    TIDE_CHECK(gui.focus == 10 && volume == 0.5f);
    key(&devices.keyboard.rightArrow, true);
    steppers(&volume, &players);
    key(&devices.keyboard.rightArrow, false);
    steppers(&volume, &players);
    TIDE_CHECK(gui.focus == 10 && volume > 0.5f);
    key(&devices.keyboard.downArrow, true);
    steppers(&volume, &players);
    key(&devices.keyboard.downArrow, false);
    steppers(&volume, &players);
    TIDE_CHECK(gui.focus == 20);
    key(&devices.keyboard.leftArrow, true);
    steppers(&volume, &players);
    key(&devices.keyboard.leftArrow, false);
    steppers(&volume, &players);
    TIDE_CHECK(gui.focus == 20 && players == 3);
    key(&devices.keyboard.downArrow, true);
    steppers(&volume, &players);
    key(&devices.keyboard.downArrow, false);
    steppers(&volume, &players);
    TIDE_CHECK(gui.focus == 30);
    key(&devices.keyboard.leftArrow, true);
    steppers(&volume, &players);
    key(&devices.keyboard.leftArrow, false);
    TIDE_CHECK(gui.focus == 30 && players == 3);
    key(&devices.keyboard.upArrow, true);
    steppers(&volume, &players);
    key(&devices.keyboard.upArrow, false);
    TIDE_CHECK(gui.focus == 20 && players == 3);
}

TIDE_TEST(gui_navigate_lets_the_arrows_start_while_the_game_reads_them)
{
    start();
    card_frame();
    devices.gamepad.connected = true;
    tide_devices sampled = devices;
    tide_gui_hide(&gui, &sampled);
    key(&devices.gamepad.dpad.down, true);
    card_frame();
    TIDE_CHECK(gui.focus == 0);
    key(&devices.gamepad.dpad.down, false);
    for (int frame = 0; frame < 3; frame++) {
        tide_gui_hide(&gui, &sampled);
        key(&devices.gamepad.dpad.down, frame == 1);
        begin();
        tide_gui_navigate(&gui);
        card_widgets();
        TIDE_CHECK(gui.focus == (frame == 0 ? 0u : 10u));
    }
    tide_gui_hide(&gui, &sampled);
    TIDE_CHECK(card_press(&devices.gamepad.dpad.right) == FOCUSED);
    TIDE_CHECK(card_press(&devices.gamepad.buttonEast) == 0);
    TIDE_CHECK(gui.focus == 0);
    tide_gui_hide(&gui, &sampled);
    TIDE_CHECK(card_press(&devices.gamepad.dpad.down) == 0);
    TIDE_CHECK(gui.focus == 0);
}

// A frame of two views: the first with a control at CARD, the second with the
// Play button. What each read of W, Enter, what's typed and the gamepad's
// north and south buttons (bits 0 to 4), the first view's in the low byte.
static int focus_views(void)
{
    int seen = 0;
    begin();
    for (uint32_t view = 1; view <= 2; view++) {
        tide_gui_view(&gui, view);
        const tide_devices *d = &gui.devices;
        const int read = (d->keyboard.w.pressed ? 1 : 0) | (d->keyboard.enter.pressed ? 2 : 0) | (d->keyboard.text.count ? 4 : 0)
                       | (d->gamepad.buttonNorth.pressed ? 8 : 0) | (d->gamepad.buttonSouth.pressed ? 16 : 0);
        seen |= read << (8 * (view - 1));
        if (view == 1) tide_gui_control(&gui, 50, CARD);
        else tide_gui_layout_button(&gui, 10, "Play");
    }
    end();
    return seen;
}

static void use_keys(const bool held)
{
    devices.gamepad.connected = true;
    key(&devices.keyboard.w, held);
    key(&devices.keyboard.enter, held);
    key(&devices.gamepad.buttonNorth, held);
    key(&devices.gamepad.buttonSouth, held);
    if (held) type("w");
}

// A menu whose Play goes once it's clicked, as a lobby over a running match
// does, in a host's order: the game's sample, then the frame.
static bool menu_open;
static tide_devices menu_sampled;
static tide_devices menu_viewed;

static void menu_sample(void)
{
    menu_sampled = devices;
    tide_gui_hide(&gui, &menu_sampled);
    tide_devices_consume(&devices);
}

// Whether Play was clicked.
static bool menu_frame(void)
{
    begin();
    menu_viewed = gui.devices;
    const bool clicked = menu_open && tide_gui_layout_button(&gui, 10, "Play");
    if (clicked) menu_open = false;
    end();
    return clicked;
}

static bool any_of(const tide_button b)
{
    return b.pressed || b.down || b.up || b.held;
}

// The menu up, with the focus on Play.
static void menu_start(void)
{
    start();
    menu_open = true;
    menu_sample();
    menu_frame();
    key(&devices.keyboard.tab, true);
    menu_sample();
    menu_frame();
    key(&devices.keyboard.tab, false);
    menu_sample();
    menu_frame();
}

TIDE_TEST(gui_a_key_that_clicked_a_widget_is_the_guis_until_its_let_go)
{
    menu_start();
    TIDE_REQUIRE(gui.focus == 10);
    key(&devices.keyboard.space, true);
    menu_sample();
    TIDE_CHECK(!any_of(menu_sampled.keyboard.space));
    TIDE_CHECK(menu_frame());
    TIDE_CHECK(!any_of(menu_viewed.keyboard.space));
    for (int i = 0; i < 5; i++) {
        menu_sample();
        TIDE_CHECK(!any_of(menu_sampled.keyboard.space));
        TIDE_CHECK(!menu_frame() && !any_of(menu_viewed.keyboard.space));
    }
    TIDE_CHECK(gui.focus == 0);

    key(&devices.keyboard.w, true);
    menu_sample();
    TIDE_CHECK(menu_sampled.keyboard.w.pressed && menu_sampled.keyboard.w.down);
    menu_frame();
    TIDE_CHECK(menu_viewed.keyboard.w.down && !any_of(menu_viewed.keyboard.space));

    key(&devices.keyboard.space, false);
    menu_sample();
    TIDE_CHECK(!any_of(menu_sampled.keyboard.space));
    menu_frame();
    TIDE_CHECK(!any_of(menu_viewed.keyboard.space));
    menu_sample();
    menu_frame();
    TIDE_CHECK(!any_of(menu_sampled.keyboard.space) && !any_of(menu_viewed.keyboard.space));

    key(&devices.keyboard.space, true);
    menu_sample();
    TIDE_CHECK(menu_sampled.keyboard.space.pressed && menu_sampled.keyboard.space.down);
    menu_frame();
    TIDE_CHECK(menu_viewed.keyboard.space.down);

    menu_start();
    devices.gamepad.connected = true;
    key(&devices.gamepad.buttonSouth, true);
    menu_sample();
    TIDE_CHECK(menu_frame());
    for (int i = 0; i < 3; i++) {
        menu_sample();
        menu_frame();
        TIDE_CHECK(!any_of(menu_sampled.gamepad.buttonSouth) && !any_of(menu_viewed.gamepad.buttonSouth));
    }
    key(&devices.gamepad.buttonSouth, false);
    menu_sample();
    menu_frame();
    TIDE_CHECK(!any_of(menu_sampled.gamepad.buttonSouth) && !any_of(menu_viewed.gamepad.buttonSouth));
    key(&devices.gamepad.buttonSouth, true);
    menu_sample();
    TIDE_CHECK(menu_sampled.gamepad.buttonSouth.down);
}

TIDE_TEST(gui_a_kept_key_is_kept_until_a_sample_has_seen_it_let_go)
{
    menu_start();
    key(&devices.keyboard.space, true);
    menu_sample();
    TIDE_CHECK(menu_frame());
    menu_frame();
    menu_frame();
    key(&devices.keyboard.space, false);
    menu_frame();
    TIDE_CHECK(!any_of(menu_viewed.keyboard.space));
    menu_frame();
    TIDE_CHECK(devices.keyboard.space.up && devices.keyboard.space.pressed);
    menu_sample();
    TIDE_CHECK(!any_of(menu_sampled.keyboard.space));
    menu_frame();
    key(&devices.keyboard.space, true);
    menu_sample();
    TIDE_CHECK(menu_sampled.keyboard.space.pressed && menu_sampled.keyboard.space.down);
    key(&devices.keyboard.space, false);
    menu_sample();
    menu_frame();

    menu_start();
    key(&devices.keyboard.space, true);
    TIDE_CHECK(menu_frame());
    tide_devices_consume(&devices);
    menu_frame();
    tide_devices_consume(&devices);
    key(&devices.keyboard.space, false);
    menu_frame();
    tide_devices_consume(&devices);
    menu_frame();
    tide_devices_consume(&devices);
    key(&devices.keyboard.space, true);
    menu_sample();
    TIDE_CHECK(menu_sampled.keyboard.space.pressed && menu_sampled.keyboard.space.down);
}

TIDE_TEST(gui_a_key_held_since_before_the_gui_had_the_keyboard_is_the_games_again)
{
    start();
    menu_open = true;
    menu_sample();
    menu_frame();
    key(&devices.keyboard.w, true);
    menu_sample();
    TIDE_CHECK(menu_sampled.keyboard.w.pressed);
    menu_frame();
    key(&devices.keyboard.tab, true);
    menu_sample();
    menu_frame();
    key(&devices.keyboard.tab, false);
    menu_sample();
    menu_frame();
    TIDE_REQUIRE(gui.focus == 10);
    menu_sample();
    TIDE_CHECK(!any_of(menu_sampled.keyboard.w));
    menu_frame();
    key(&devices.keyboard.escape, true);
    menu_sample();
    menu_frame();
    TIDE_CHECK(gui.focus == 0);
    menu_sample();
    TIDE_CHECK(menu_sampled.keyboard.w.pressed && !any_of(menu_sampled.keyboard.escape));
    menu_frame();
    TIDE_CHECK(menu_viewed.keyboard.w.pressed && !any_of(menu_viewed.keyboard.escape));
    key(&devices.keyboard.escape, false);
    menu_sample();
    menu_frame();
    TIDE_CHECK(!any_of(menu_sampled.keyboard.escape) && !any_of(menu_viewed.keyboard.escape));
}

TIDE_TEST(gui_the_view_of_a_focused_control_keeps_the_keys_the_gui_doesnt_use)
{
    start();
    focus_views();
    use_keys(true);
    TIDE_CHECK(focus_views() == (31 | 31 << 8));
    use_keys(false);
    focus_views();
    key(&devices.keyboard.tab, true);
    focus_views();
    key(&devices.keyboard.tab, false);
    focus_views();
    TIDE_CHECK(gui.focus == 50);
    use_keys(true);
    TIDE_CHECK(focus_views() == (1 | 4 | 8));
    tide_devices sampled = devices;
    tide_gui_hide(&gui, &sampled);
    TIDE_CHECK(!sampled.keyboard.w.pressed && !sampled.gamepad.buttonNorth.pressed);
    use_keys(false);
    focus_views();

    key(&devices.keyboard.tab, true);
    focus_views();
    key(&devices.keyboard.tab, false);
    focus_views();
    TIDE_CHECK(gui.focus == 10);
    use_keys(true);
    key(&devices.keyboard.enter, false);
    key(&devices.gamepad.buttonSouth, false);
    TIDE_CHECK(focus_views() == 0);
}

TIDE_TEST(gui_disabled_blocks_stop_a_control)
{
    start();
    int result = 0;
    for (int frame = 0; frame < 6; frame++) {
        mouse(650.0f, 650.0f, frame == 1 || frame == 2);
        key(&devices.keyboard.tab, frame == 4);
        begin();
        const int depth = tide_gui_begin_disabled(&gui, true);
        result |= said(tide_gui_control(&gui, 50, CARD));
        tide_gui_close(&gui, depth);
        end();
        if (frame == 2) {
            tide_devices sampled = devices;
            tide_gui_hide(&gui, &sampled);
            TIDE_CHECK(!sampled.mouse.left.pressed);
        }
    }
    TIDE_CHECK(result == 0 && gui.active == 0 && gui.focus == 0);

    for (int frame = 0; frame < 4; frame++) {
        mouse(650.0f, 650.0f, frame >= 1);
        begin();
        const int depth = tide_gui_begin_disabled(&gui, frame == 3);
        const int now = said(tide_gui_control(&gui, 50, CARD));
        tide_gui_close(&gui, depth);
        end();
        TIDE_CHECK(now == (frame == 0 ? HOVERED : frame == 3 ? 0 : (HOVERED | PRESSED)));
    }
    TIDE_CHECK(gui.active == 0);
}

TIDE_TEST(gui_a_modal_stops_the_controls_outside_it)
{
    start();
    const tide_rect inside = {900.0f, 500.0f, 120.0f, 60.0f};
    bool open = true;
    int out = 0, in = 0;
    for (int frame = 0; frame < 5; frame++) {
        mouse(frame < 3 ? 650.0f : 950.0f, frame < 3 ? 650.0f : 520.0f, frame == 1 || frame == 3);
        begin();
        const int outside = said(tide_gui_control(&gui, 50, CARD));
        const int depth = tide_gui_begin_modal(&gui, 30, TIDE_ANCHOR_MIDDLE_CENTER, &open);
        TIDE_REQUIRE(depth >= 0);
        const int within = said(tide_gui_control(&gui, 60, inside));
        tide_gui_close(&gui, depth);
        end();
        if (frame > 0) out |= outside;
        in |= within;
        if (frame == 2) TIDE_CHECK(gui.focus == 60);
    }
    TIDE_CHECK(out == 0);
    TIDE_CHECK(in == (HOVERED | PRESSED | FOCUSED | CLICKED));
}

// Three controls side by side from one call site, as a loop makes them, but
// for the one at `skip`: which said `what` (bits 0 to 2).
static int loop_frame(const int skip, const int what)
{
    int which = 0;
    begin();
    for (int i = 0; i < 3; i++) {
        if (i == skip) continue;
        const uint32_t id = tide_gui_id(&gui, 7, 3);
        const tide_control c = tide_gui_control(&gui, id, (tide_rect){600.0f + 110.0f * (float)i, 600.0f, 100.0f, 60.0f});
        if (said(c) & what) which |= 1 << i;
    }
    end();
    return which;
}

TIDE_TEST(gui_controls_made_in_a_loop_are_told_apart_by_their_turn)
{
    start();
    mouse(760.0f, 630.0f, false);
    loop_frame(-1, 0);
    TIDE_CHECK(loop_frame(-1, HOVERED) == 2);
    mouse(760.0f, 630.0f, true);
    TIDE_CHECK(loop_frame(-1, PRESSED) == 2);
    mouse(760.0f, 630.0f, false);
    TIDE_CHECK(loop_frame(-1, CLICKED) == 2);
    mouse(900.0f, 900.0f, false);
    loop_frame(-1, 0);
    for (int i = 0; i < 3; i++) {
        key(&devices.keyboard.rightArrow, true);
        TIDE_CHECK(loop_frame(-1, FOCUSED) == 1 << i);
        key(&devices.keyboard.rightArrow, false);
        TIDE_CHECK(loop_frame(-1, FOCUSED) == 1 << i);
    }
    key(&devices.keyboard.leftArrow, true);
    TIDE_CHECK(loop_frame(2, FOCUSED) == 2);
    key(&devices.keyboard.leftArrow, false);
    TIDE_CHECK(loop_frame(2, FOCUSED) == 2);
    TIDE_CHECK(loop_frame(0, FOCUSED) == 4);
}

#undef NONE

// ---------------------------------------------------------------------------
// Themes: how the GUI's own widgets look

static bool same_color(const tide_color a, const tide_color b)
{
    return tide_test_color_is(a, b.r, b.g, b.b, b.a);
}

// A frame with one of each kind of part: an area with a button, a toggle that's
// on, a slider and a field in it.
static void every_part(void)
{
    static bool on = true;
    static float volume = 0.5f;
    static int32_t players = 4;
    const int area = tide_gui_begin_area(&gui, 99, (tide_rect){400.0f, 400.0f, 500.0f, 300.0f});
    tide_gui_layout_button(&gui, 1, "Play");
    tide_gui_layout_toggle(&gui, 2, "Fullscreen", &on);
    tide_gui_layout_slider(&gui, 3, "", &volume, 0.0f, 1.0f);
    tide_gui_layout_int_field(&gui, 4, "", &players);
    tide_gui_close(&gui, area);
}

TIDE_TEST(gui_looks_as_it_always_did_with_no_theme)
{
    start();
    mouse(5.0f, 5.0f, false);
    begin();
    every_part();
    end();
    const tide_color text = {0.93f, 0.93f, 0.95f, 1.0f};
    const tide_color control = {0.2f, 0.2f, 0.25f, 1.0f};
    const tide_color accent = {1.0f, 0.77f, 0.24f, 1.0f};
    // The panel's shadow, a soft shape, then the rects the GUI always drew
    TIDE_REQUIRE(draw.count == 14);
    TIDE_CHECK(draw.shape_count == 1 && draw.commands[1].kind == TIDE_DRAW_SHAPE);
    TIDE_CHECK(same_color(draw.commands[1].color, (tide_color){0.0f, 0.0f, 0.0f, 0.35f}));
    const tide_draw_command *c = draw.commands + 1;
    TIDE_CHECK(c[1].kind == TIDE_DRAW_RECT && same_color(c[1].color, (tide_color){0.07f, 0.07f, 0.1f, 0.9f}));
    TIDE_CHECK(c[2].kind == TIDE_DRAW_RECT && same_color(c[2].color, control));
    TIDE_CHECK(c[3].kind == TIDE_DRAW_TEXT && same_color(c[3].color, text));
    TIDE_CHECK(c[4].kind == TIDE_DRAW_RECT && same_color(c[4].color, control));
    TIDE_CHECK(c[5].kind == TIDE_DRAW_RECT && same_color(c[5].color, accent));
    TIDE_CHECK(c[8].kind == TIDE_DRAW_RECT && same_color(c[8].color, control));
    TIDE_CHECK(c[9].kind == TIDE_DRAW_RECT && same_color(c[9].color, accent));
    TIDE_CHECK(c[10].kind == TIDE_DRAW_RECT && same_color(c[10].color, text));
    TIDE_CHECK(c[11].kind == TIDE_DRAW_RECT && same_color(c[11].color, (tide_color){0.12f, 0.12f, 0.15f, 1.0f}));

    mouse(420.0f, 420.0f, false);
    begin();
    every_part();
    end();
    begin();
    every_part();
    end();
    TIDE_CHECK(same_color(draw.commands[3].color, (tide_color){0.28f, 0.28f, 0.34f, 1.0f}));
    mouse(420.0f, 420.0f, true);
    begin();
    every_part();
    end();
    TIDE_CHECK(same_color(draw.commands[3].color, (tide_color){0.14f, 0.14f, 0.18f, 1.0f}));
    mouse(420.0f, 525.0f, false);
    begin();
    every_part();
    end();
    begin();
    every_part();
    end();
    TIDE_CHECK(same_color(draw.commands[12].color, (tide_color){0.16f, 0.16f, 0.2f, 1.0f}));
}

static tide_gui_theme loud_theme(void)
{
    tide_gui_theme theme = tide_gui_theme_default();
    theme.text = (tide_color){0.1f, 0.2f, 0.3f, 1.0f};
    theme.panel = (tide_color){0.9f, 0.8f, 0.7f, 1.0f};
    theme.control = (tide_color){0.0f, 0.5f, 1.0f, 1.0f};
    theme.hot = (tide_color){0.2f, 0.6f, 1.0f, 1.0f};
    theme.pressed = (tide_color){0.0f, 0.3f, 0.8f, 1.0f};
    theme.field = (tide_color){1.0f, 1.0f, 1.0f, 1.0f};
    theme.field_hot = (tide_color){0.9f, 0.9f, 0.9f, 1.0f};
    theme.accent = (tide_color){1.0f, 0.0f, 0.5f, 1.0f};
    theme.shadow_size = 0.0f; // Its parts alone
    return theme;
}

TIDE_TEST(gui_a_theme_colors_every_part)
{
    start();
    const tide_gui_theme theme = loud_theme();
    for (int frame = 0; frame < 4; frame++) {
        mouse(frame == 0 ? 5.0f : 420.0f, frame == 0 ? 5.0f : frame == 3 ? 525.0f : 420.0f, frame == 2);
        for (int twice = 0; twice < 2; twice++) {
            begin();
            tide_gui_set_theme(&gui, theme);
            every_part();
            end();
        }
        TIDE_REQUIRE(draw.count == 13);
        const tide_draw_command *c = draw.commands;
        TIDE_CHECK(draw.shape_count == 0);
        TIDE_CHECK(same_color(c[1].color, theme.panel));
        TIDE_CHECK(same_color(c[2].color, frame == 1 ? theme.hot : frame == 2 ? theme.pressed : theme.control));
        TIDE_CHECK(same_color(c[3].color, theme.text) && same_color(c[6].color, theme.text) && same_color(c[7].color, theme.text));
        TIDE_CHECK(same_color(c[4].color, theme.control) && same_color(c[5].color, theme.accent));
        TIDE_CHECK(same_color(c[8].color, theme.control) && same_color(c[9].color, theme.accent));
        TIDE_CHECK(same_color(c[10].color, theme.text));
        TIDE_CHECK(same_color(c[11].color, frame == 3 ? theme.field_hot : theme.field));
        TIDE_CHECK(same_color(c[12].color, theme.text));
    }
}

// The shape a command is, or NULL for a plain rect and the rest.
static const tide_draw_shape_data *shape_of(const tide_draw_command *c)
{
    return c->kind == TIDE_DRAW_SHAPE && c->shape < draw.shape_count ? &draw.shapes[c->shape] : NULL;
}

TIDE_TEST(gui_a_themes_radius_rounds_every_part)
{
    start();
    tide_gui_theme theme = tide_gui_theme_default();
    theme.radius = 6.0f;
    theme.shadow_size = 0.0f; // Its parts alone
    mouse(5.0f, 5.0f, false);
    for (int frame = 0; frame < 2; frame++) {
        key(&devices.keyboard.tab, frame == 1);
        begin();
        tide_gui_set_theme(&gui, theme);
        every_part();
        end();
    }
    TIDE_REQUIRE(draw.count == 14);
    const tide_draw_command *c = draw.commands;
    TIDE_REQUIRE(shape_of(&c[1]) != NULL);
    TIDE_CHECK(c[1].a.x == 650.0f && c[1].a.y == 550.0f && c[1].b.x == 500.0f && c[1].b.y == 300.0f);
    TIDE_CHECK(shape_of(&c[1])->radius == 6.0f && same_color(c[1].color, theme.panel));
    TIDE_REQUIRE(shape_of(&c[2]) && shape_of(&c[3]));
    TIDE_CHECK(same_color(c[2].color, theme.accent) && shape_of(&c[2])->radius == 8.0f);
    TIDE_CHECK(same_color(c[3].color, theme.control) && shape_of(&c[3])->radius == 6.0f);
    TIDE_CHECK(c[2].b.x == c[3].b.x + 4.0f && c[2].b.y == c[3].b.y + 4.0f);
    TIDE_REQUIRE(shape_of(&c[5]) && shape_of(&c[6]));
    TIDE_CHECK(shape_of(&c[5])->radius == 6.0f && shape_of(&c[6])->radius == 2.0f);
    TIDE_REQUIRE(shape_of(&c[9]) != NULL);
    TIDE_CHECK(c[9].b.y == 6.0f && shape_of(&c[9])->radius == 3.0f);
    TIDE_CHECK(shape_of(&c[12]) != NULL && shape_of(&c[12])->radius == 6.0f);

    begin();
    tide_gui_set_theme(&gui, theme);
    const int area = tide_gui_begin_area_at(&gui, 77, TIDE_ANCHOR_MIDDLE_CENTER);
    tide_gui_layout_button(&gui, 8, "Play");
    tide_gui_close(&gui, area);
    end();
    TIDE_REQUIRE(draw.count == 4 && shape_of(&draw.commands[1]) != NULL);
    TIDE_CHECK(draw.commands[1].b.y == 28.0f + 24.0f && shape_of(&draw.commands[1])->radius == 6.0f);
    TIDE_CHECK(draw.commands[1].a.x == 960.0f && draw.commands[1].a.y == 540.0f);

    begin();
    tide_gui_set_theme(&gui, theme);
    const int off = tide_gui_begin_disabled(&gui, true);
    tide_gui_button(&gui, 9, (tide_rect){100.0f, 100.0f, 200.0f, 40.0f}, "Locked");
    tide_gui_close(&gui, off);
    end();
    TIDE_REQUIRE(draw.count == 3 && shape_of(&draw.commands[1]) != NULL);
    TIDE_CHECK(draw.commands[1].color.a == 0.5f);
}

// The theme's control color in the list's `index`th command.
static bool drawn_with(const uint32_t index, const tide_gui_theme theme)
{
    return index < draw.count && same_color(draw.commands[index].color, theme.control);
}

TIDE_TEST(gui_a_theme_block_puts_back_what_it_found)
{
    start();
    const tide_gui_theme usual = tide_gui_theme_default();
    tide_gui_theme red = usual, green = usual, blue = usual;
    red.control = (tide_color){1.0f, 0.0f, 0.0f, 1.0f};
    green.control = (tide_color){0.0f, 1.0f, 0.0f, 1.0f};
    blue.control = (tide_color){0.0f, 0.0f, 1.0f, 1.0f};
    mouse(900.0f, 900.0f, false);

    begin();
    tide_gui_layout_button(&gui, 1, "A");
    const int outer = tide_gui_begin_theme(&gui, red);
    tide_gui_layout_button(&gui, 2, "B");
    const int inner = tide_gui_begin_theme(&gui, green);
    tide_gui_layout_button(&gui, 3, "C");
    tide_gui_close(&gui, inner);
    tide_gui_layout_button(&gui, 4, "D");
    tide_gui_close(&gui, outer);
    tide_gui_layout_button(&gui, 5, "E");
    end();
    TIDE_REQUIRE(draw.count == 11);
    TIDE_CHECK(drawn_with(1, usual) && drawn_with(3, red) && drawn_with(5, green) && drawn_with(7, red) && drawn_with(9, usual));
    TIDE_CHECK(draw.commands[9].a.y == draw.commands[1].a.y + 4.0f * (28.0f + 5.0f));
    TIDE_CHECK(draw.commands[5].a.x - draw.commands[5].b.x * 0.5f == 0.0f);

    begin();
    tide_gui_set_theme(&gui, blue);
    const int block = tide_gui_begin_theme(&gui, red);
    tide_gui_set_theme(&gui, green);
    tide_gui_layout_button(&gui, 1, "A");
    tide_gui_close(&gui, block);
    tide_gui_layout_button(&gui, 2, "B");
    end();
    TIDE_CHECK(drawn_with(1, green) && drawn_with(3, blue));

    begin();
    const int first = tide_gui_begin_theme(&gui, red);
    const int off = tide_gui_begin_disabled(&gui, true);
    tide_gui_begin_theme(&gui, green);
    tide_gui_begin_horizontal(&gui, 6);
    tide_gui_close(&gui, off);
    TIDE_CHECK(same_color(gui.theme.control, red.control) && !gui.groups[gui.depth].disabled);
    tide_gui_layout_button(&gui, 1, "A");
    tide_gui_close(&gui, first);
    TIDE_CHECK(same_color(gui.theme.control, usual.control) && gui.depth == 0);
    end();
    TIDE_CHECK(drawn_with(1, red) && draw.commands[1].color.a == 1.0f);

    begin();
    const int row = tide_gui_begin_horizontal(&gui, 7);
    tide_gui_layout_button(&gui, 1, "A");
    const int themed = tide_gui_begin_theme(&gui, red);
    tide_gui_layout_button(&gui, 2, "B");
    tide_gui_close(&gui, themed);
    tide_gui_layout_button(&gui, 3, "C");
    tide_gui_close(&gui, row);
    end();
    TIDE_REQUIRE(draw.count == 7);
    TIDE_CHECK(draw.commands[1].a.y == draw.commands[3].a.y && draw.commands[3].a.y == draw.commands[5].a.y);
    TIDE_CHECK(draw.commands[1].a.x < draw.commands[3].a.x && draw.commands[3].a.x < draw.commands[5].a.x);
    TIDE_CHECK(drawn_with(1, usual) && drawn_with(3, red) && drawn_with(5, usual));

    begin();
    for (int i = 0; i < TIDE_GUI_MAX_DEPTH; i++) tide_gui_begin_vertical(&gui, 100 + (uint32_t)i);
    const int deep = tide_gui_begin_theme(&gui, red);
    TIDE_CHECK(same_color(gui.theme.control, usual.control));
    tide_gui_close(&gui, deep);
    end();
}

TIDE_TEST(gui_a_set_theme_holds_for_the_views_after_and_ends_with_the_frame)
{
    start();
    const tide_gui_theme usual = tide_gui_theme_default();
    tide_gui_theme red = usual;
    red.control = (tide_color){1.0f, 0.0f, 0.0f, 1.0f};
    mouse(900.0f, 900.0f, false);
    begin();
    tide_gui_view(&gui, 1);
    tide_gui_layout_button(&gui, 1, "A");
    tide_gui_view(&gui, 2);
    tide_gui_set_theme(&gui, red);
    tide_gui_layout_button(&gui, 2, "B");
    tide_gui_view(&gui, 3);
    tide_gui_layout_button(&gui, 3, "C");
    end();
    TIDE_CHECK(drawn_with(1, usual) && drawn_with(3, red) && drawn_with(5, red));
    begin();
    tide_gui_view(&gui, 1);
    tide_gui_layout_button(&gui, 1, "A");
    tide_gui_view(&gui, 3);
    tide_gui_layout_button(&gui, 3, "C");
    end();
    TIDE_CHECK(drawn_with(1, usual) && drawn_with(3, usual));
}

// ---------------------------------------------------------------------------
// The fields that say whether the player entered what they typed: a text field,
// an int field and a float field, one under the other.

static tide_str entry_name;
static int32_t entry_count;
static float entry_ratio;
static int entry_shown;
static int entry_off;
static tide_devices entry_sampled;
static tide_devices entry_viewed;

// A field's bit: in what entries() returns, that it was entered, and times
// ENTRY_CHANGED, that it returned true.
enum { ENTRY_NAME = 1, ENTRY_COUNT = 2, ENTRY_RATIO = 4, ENTRY_CHANGED = 8 };

#define ENTRY_X 170.0f // Past the label column
#define NAME_Y 14.0f   // Each one's row, with all three shown
#define COUNT_Y 47.0f
#define RATIO_Y 80.0f
#define RATIO_ID tide_gui_seed(7, 1) // A float field's one box

static void entry_start(void)
{
    start();
    entry_name = TIDE_STR_EMPTY;
    entry_count = 4;
    entry_ratio = 0.5f;
    entry_shown = ENTRY_NAME | ENTRY_COUNT | ENTRY_RATIO;
    entry_off = 0;
}

// A frame of them in a host's order: the game's sample, then the frame.
static int entries(void)
{
    entry_sampled = devices;
    tide_gui_hide(&gui, &entry_sampled);
    tide_devices_consume(&devices);
    begin();
    entry_viewed = gui.devices;
    const tide_textref name = {.local = &entry_name};
    bool entered[3] = {true, true, true};
    bool changed[3] = {false, false, false};
    for (int i = 0; i < 3; i++) {
        if (!(entry_shown >> i & 1)) {
            entered[i] = false;
            continue;
        }
        const int depth = tide_gui_begin_disabled(&gui, (entry_off >> i & 1) != 0);
        if (i == 0) changed[i] = tide_gui_layout_text_field_entered(&gui, 5, "Name", name, &entered[i]);
        if (i == 1) changed[i] = tide_gui_layout_int_field_entered(&gui, 6, "Count", &entry_count, &entered[i]);
        if (i == 2) changed[i] = tide_gui_layout_float_field_entered(&gui, 7, "Ratio", &entry_ratio, &entered[i]);
        tide_gui_close(&gui, depth);
    }
    end();
    int said = 0;
    for (int i = 0; i < 3; i++) said |= ((entered[i] ? 1 : 0) | (changed[i] ? ENTRY_CHANGED : 0)) << i;
    return said;
}

// A click at a row: the press, then the release. What the two frames said.
static int entry_click(const float x, const float y)
{
    mouse(x, y, true);
    const int pressed = entries();
    mouse(x, y, false);
    const int released = entries();
    return pressed | released;
}

// A key down for a frame, then up for one. What the first frame said.
static int entry_tap(tide_button *b)
{
    key(b, true);
    const int said = entries();
    key(b, false);
    entries();
    return said;
}

static bool name_is(const char *text)
{
    return entry_name.bytes == (int32_t)strlen(text) && memcmp(entry_name.ptr, text, strlen(text)) == 0;
}

TIDE_TEST(gui_text_selection_edits_unicode_and_undoes_without_moving_focus)
{
    entry_start();
    entry_name = tide_str_add(TIDE_STR_EMPTY, tide_str_from_cstr("a\xc3\xa9z"));
    entries();
    gui.focus = 5;
    entry_tap(&devices.keyboard.enter);
    TIDE_REQUIRE(gui.editing == 5);
    TIDE_CHECK(gui.edit_caret == 4);
    entry_tap(&devices.keyboard.leftArrow);
    TIDE_CHECK(gui.focus == 5 && gui.edit_caret == 3);
    key(&devices.keyboard.leftShift, true);
    entry_tap(&devices.keyboard.leftArrow);
    key(&devices.keyboard.leftShift, false);
    TIDE_CHECK(gui.edit_caret == 1 && gui.edit_anchor == 3);
    type("Q");
    entries();
    TIDE_CHECK(name_is("aQz"));
    key(&devices.keyboard.leftCtrl, true);
    entry_tap(&devices.keyboard.z);
    TIDE_CHECK(name_is("a\xc3\xa9z"));
    entry_tap(&devices.keyboard.y);
    TIDE_CHECK(name_is("aQz"));
    entry_tap(&devices.keyboard.a);
    key(&devices.keyboard.leftCtrl, false);
    entry_tap(&devices.keyboard.delete);
    TIDE_CHECK(name_is("") && gui.edit_caret == 0);
}

TIDE_TEST(gui_spatial_focus_uses_geometry_and_tab_keeps_declaration_order)
{
    start();
    const tide_rect rects[] = {{0, 0, 40, 30}, {0, 80, 40, 30}, {100, 0, 40, 30}, {100, 80, 40, 30}};
    for (int frame = 0; frame < 4; frame++) {
        if (frame == 1) key(&devices.keyboard.tab, true);
        if (frame == 2) { key(&devices.keyboard.tab, false); key(&devices.keyboard.rightArrow, true); }
        if (frame == 3) { key(&devices.keyboard.rightArrow, false); key(&devices.keyboard.downArrow, true); }
        begin();
        for (uint32_t i = 0; i < 4; i++) tide_gui_control(&gui, i + 10, rects[i]);
        end();
        if (frame == 1) TIDE_CHECK(gui.focus == 10);
        if (frame == 2) TIDE_CHECK(gui.focus == 12);
        if (frame == 3) TIDE_CHECK(gui.focus == 13);
    }
    key(&devices.keyboard.downArrow, false);
    key(&devices.keyboard.tab, true);
    begin();
    for (uint32_t i = 0; i < 4; i++) tide_gui_control(&gui, i + 10, rects[i]);
    end();
    TIDE_CHECK(gui.focus == 10);
}

static void scroll_buttons(tide_float2 *offset, const bool focus_last)
{
    const int scroll = tide_gui_begin_scroll(&gui, 500, (tide_rect){100, 100, 120, 60}, offset);
    for (uint32_t i = 0; i < 5; i++) {
        if (focus_last && i == 4) tide_gui_focus_next(&gui);
        tide_gui_layout_button(&gui, 1000 + i, "item");
    }
    tide_gui_close(&gui, scroll);
}

TIDE_TEST(gui_scroll_clips_input_routes_wheel_and_reveals_requested_focus)
{
    start();
    tide_float2 offset = {0};
    mouse(110, 200, false); // On a hidden child below the viewport.
    begin(); scroll_buttons(&offset, false); end();
    tide_devices sampled = devices;
    tide_gui_hide(&gui, &sampled);
    mouse(110, 200, true);
    begin(); scroll_buttons(&offset, false); end();
    TIDE_CHECK(gui.active == 0);
    mouse(110, 110, false);
    devices.mouse.poll_scroll = tide_f2(0, -2);
    begin(); scroll_buttons(&offset, false); end();
    TIDE_CHECK(offset.y == 56 && offset.x == 0);
    TIDE_CHECK(gui.list.place.clip == 0 && gui.scrolling_count == 0);
    for (uint32_t i = 0; i < gui.rect_count; i++) {
        TIDE_CHECK(gui.rects[i].rect.height == 0 ||
                   (gui.rects[i].rect.y >= 100 && gui.rects[i].rect.y + gui.rects[i].rect.height <= 160));
    }
    devices.mouse.poll_scroll = tide_f2(0, 0);
    begin(); scroll_buttons(&offset, true); end();
    TIDE_CHECK(gui.focus == 1004 && offset.y == 100);
    TIDE_CHECK(gui.focus_rect.y + gui.focus_rect.height == 160);
    offset = tide_f2(NAN, INFINITY);
    begin(); scroll_buttons(&offset, false); end();
    TIDE_CHECK(offset.x == 0 && offset.y == 0);
}

TIDE_TEST(gui_nested_scroll_consumes_wheel_once_and_bubbles_at_the_edge)
{
    start();
    tide_float2 outer = {0}, inner = {0};
    mouse(30, 30, false);
    for (int frame = 0; frame < 4; frame++) {
        if (frame) devices.mouse.poll_scroll = tide_f2(0, -2);
        begin();
        const int a = tide_gui_begin_scroll(&gui, 10, (tide_rect){20, 20, 140, 100}, &outer);
        const int b = tide_gui_begin_layout_scroll(&gui, 20, tide_f2(120, 60), &inner);
        for (uint32_t i = 0; i < 4; i++) tide_gui_layout_button(&gui, i + 30, "item");
        tide_gui_close(&gui, b);
        tide_gui_layout_space(&gui, 200);
        tide_gui_close(&gui, a);
        end();
        if (frame == 1) TIDE_CHECK(inner.y == 56 && outer.y == 0);
        if (frame == 2) TIDE_CHECK(inner.y == 67 && outer.y == 45);
    }
    TIDE_CHECK(gui.scrolling_count == 0 && gui.list.place.clip == 0);
}

TIDE_TEST(gui_first_scroll_frame_clamps_large_offsets_without_losing_layout_precision)
{
    start();
    tide_float2 offset = {FLT_MAX, FLT_MAX};
    begin(); scroll_buttons(&offset, false); end();
    TIDE_CHECK(offset.x == 0 && offset.y == 100);
    TIDE_REQUIRE(gui.nav_last_count == 5);
    TIDE_CHECK(gui.nav_rects[4].y == 132 && gui.nav_rects[4].height == 28);
}

TIDE_TEST(gui_touch_scroll_cancels_the_child_gesture_without_clicking)
{
    start();
    tide_float2 offset = {0};
    for (int frame = 0; frame < 4; frame++) {
        tide_touches_poll(&devices.touchscreen);
        if (frame) tide_touch_event(&devices.touchscreen,
            frame == 1 ? TIDE_TOUCH_BEGAN : frame == 2 ? TIDE_TOUCH_MOVED : TIDE_TOUCH_ENDED,
            1, tide_f2(110, 1080 - (frame == 1 ? 140 : 110)));
        tide_pointer_poll(&devices, false);
        tide_gui_begin(&gui, &devices, tide_f2(1920, 1080), NULL);
        const int a = tide_gui_begin_scroll(&gui, 500, (tide_rect){100, 100, 120, 60}, &offset);
        tide_gui_layout_space(&gui, 33);
        const tide_rect r = tide_gui_layout_reserve(&gui, tide_f2(120, 28));
        const tide_control control = tide_gui_control(&gui, 1001, r);
        tide_gui_layout_space(&gui, 200);
        tide_gui_close(&gui, a);
        end();
        if (frame == 1) TIDE_CHECK(control.pressed && control.down);
        if (frame == 2) TIDE_CHECK(control.cancelled && !control.pressed && offset.y == 30);
        TIDE_CHECK(!control.clicked);
    }
}

TIDE_TEST(gui_touch_motion_does_not_steal_a_press_when_content_cannot_scroll)
{
    start();
    tide_float2 offset = {0};
    for (int frame = 0; frame < 4; frame++) {
        tide_touches_poll(&devices.touchscreen);
        if (frame) tide_touch_event(&devices.touchscreen,
            frame == 1 ? TIDE_TOUCH_BEGAN : frame == 2 ? TIDE_TOUCH_MOVED : TIDE_TOUCH_ENDED,
            1, tide_f2(110, 1080 - (frame == 1 ? 140 : 110)));
        tide_pointer_poll(&devices, false);
        tide_gui_begin(&gui, &devices, tide_f2(1920, 1080), NULL);
        const int a = tide_gui_begin_scroll(&gui, 500, (tide_rect){100, 100, 120, 60}, &offset);
        const tide_rect r = tide_gui_layout_reserve(&gui, tide_f2(120, 60));
        const tide_control control = tide_gui_control(&gui, 1001, r);
        tide_gui_close(&gui, a);
        end();
        TIDE_CHECK(!control.cancelled && offset.y == 0);
        if (frame == 2) TIDE_CHECK(control.pressed);
        if (frame == 3) TIDE_CHECK(control.clicked);
    }
}

TIDE_TEST(gui_editing_repeats_cursor_keys_without_repeating_focus_navigation)
{
    entry_start();
    entry_name = tide_str_from_cstr("abc");
    entries();
    gui.focus = 5;
    entry_tap(&devices.keyboard.enter);
    key(&devices.keyboard.leftArrow, true);
    entries();
    TIDE_CHECK(gui.edit_caret == 2 && gui.focus == 5);
    gui.edit_repeat_at = 0; // The repeat is due; no wall-clock sleep in a test.
    entries();
    TIDE_CHECK(gui.edit_caret == 1 && gui.focus == 5);
    key(&devices.keyboard.leftArrow, false);
    entries();
    TIDE_CHECK(gui.edit_repeat == 0);
}

TIDE_TEST(gui_float_fields_keep_untouched_values)
{
    static const float values[] = {
        1.23456776f, -1.23456776f, 0.123456791f, 1.0f + FLT_EPSILON,
        FLT_MAX, -FLT_MAX, FLT_MIN, FLT_TRUE_MIN, 0.0f, -0.0f,
    };
    for (size_t i = 0; i < sizeof values / sizeof values[0]; i++) {
        for (int leave = 0; leave < 3; leave++) {
            entry_start();
            entry_ratio = values[i];
            mouse(ENTRY_X, RATIO_Y, false);
            TIDE_CHECK(entries() == 0);
            TIDE_CHECK(entry_click(ENTRY_X, RATIO_Y) == 0);
            TIDE_REQUIRE(gui.editing == RATIO_ID);
            if (leave == 0) TIDE_CHECK(entry_tap(&devices.keyboard.enter) == ENTRY_RATIO);
            if (leave == 1) TIDE_CHECK(entry_tap(&devices.keyboard.tab) == 0);
            if (leave == 2) TIDE_CHECK(entry_click(ENTRY_X, NAME_Y) == 0);
            TIDE_CHECK(memcmp(&entry_ratio, &values[i], sizeof entry_ratio) == 0);
        }
    }
}

TIDE_TEST(gui_float_fields_accept_full_precision_numbers)
{
    static const struct {
        const char *text;
        float value;
    } cases[] = {
        {"1.23456776", 1.23456776f},
        {"3.40282347e+38", FLT_MAX},
        {"-3.40282347e+38", -FLT_MAX},
        {"1.40129846e-45", FLT_TRUE_MIN},
        {"-1.40129846e-45", -FLT_TRUE_MIN},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        entry_start();
        mouse(ENTRY_X, RATIO_Y, false);
        entries();
        entry_click(ENTRY_X, RATIO_Y);
        type(cases[i].text);
        TIDE_CHECK(entries() == 0 && entry_ratio == 0.5f);
        TIDE_CHECK(entry_tap(&devices.keyboard.enter) == (ENTRY_RATIO | ENTRY_RATIO * ENTRY_CHANGED));
        TIDE_CHECK(memcmp(&entry_ratio, &cases[i].value, sizeof entry_ratio) == 0);
    }
    static const char *const invalid[] = {"3.5e38", "-3.5e38", "1e999"};
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; i++) {
        entry_start();
        mouse(ENTRY_X, RATIO_Y, false);
        entries();
        entry_click(ENTRY_X, RATIO_Y);
        type(invalid[i]);
        entries();
        TIDE_CHECK(entry_tap(&devices.keyboard.enter) == ENTRY_RATIO);
        TIDE_CHECK(entry_ratio == 0.5f);
    }
}

TIDE_TEST(gui_a_field_says_when_its_typing_is_entered)
{
    entry_start();
    mouse(ENTRY_X, NAME_Y, false);
    TIDE_CHECK(entries() == 0);

    TIDE_CHECK(entry_click(ENTRY_X, NAME_Y) == 0);
    TIDE_REQUIRE(gui.editing == 5);
    type("K7");
    TIDE_CHECK(entries() == ENTRY_NAME * ENTRY_CHANGED);
    TIDE_CHECK(entries() == 0);
    key(&devices.keyboard.enter, true);
    TIDE_CHECK(entries() == ENTRY_NAME);
    TIDE_CHECK(gui.editing == 0 && gui.focus == 5 && name_is("K7"));
    TIDE_CHECK(entries() == 0);
    key(&devices.keyboard.enter, false);
    TIDE_CHECK(entries() == 0);
    TIDE_CHECK(entry_tap(&devices.keyboard.enter) == 0);
    TIDE_CHECK(gui.editing == 5);
    type("Q");
    TIDE_CHECK(entries() == ENTRY_NAME * ENTRY_CHANGED);
    TIDE_CHECK(entry_tap(&devices.keyboard.numpadEnter) == ENTRY_NAME);
    TIDE_CHECK(gui.editing == 0 && name_is("K7Q"));

    TIDE_CHECK(entry_click(ENTRY_X, COUNT_Y) == 0);
    TIDE_REQUIRE(gui.editing == 6);
    type("12");
    TIDE_CHECK(entries() == 0 && entry_count == 4);
    key(&devices.keyboard.enter, true);
    TIDE_CHECK(entries() == (ENTRY_COUNT | ENTRY_COUNT * ENTRY_CHANGED));
    TIDE_CHECK(entry_count == 12 && gui.editing == 0 && gui.focus == 6);
    TIDE_CHECK(entries() == 0);
    key(&devices.keyboard.enter, false);
    TIDE_CHECK(entries() == 0);
    TIDE_CHECK(entry_tap(&devices.keyboard.enter) == 0);
    TIDE_CHECK(gui.editing == 6);
    TIDE_CHECK(entry_tap(&devices.keyboard.numpadEnter) == ENTRY_COUNT);
    TIDE_CHECK(entry_count == 12 && gui.editing == 0);

    TIDE_CHECK(entry_click(ENTRY_X, RATIO_Y) == 0);
    TIDE_REQUIRE(gui.editing == RATIO_ID);
    type("0.25");
    TIDE_CHECK(entries() == 0 && entry_ratio == 0.5f);
    key(&devices.keyboard.enter, true);
    TIDE_CHECK(entries() == (ENTRY_RATIO | ENTRY_RATIO * ENTRY_CHANGED));
    TIDE_CHECK(entry_ratio == 0.25f && gui.editing == 0);
    TIDE_CHECK(entries() == 0);
    key(&devices.keyboard.enter, false);
    TIDE_CHECK(entries() == 0);
    TIDE_CHECK(entry_tap(&devices.keyboard.enter) == 0);
    TIDE_CHECK(gui.editing == RATIO_ID);
    TIDE_CHECK(entry_tap(&devices.keyboard.enter) == ENTRY_RATIO);
}

TIDE_TEST(gui_what_is_typed_in_the_frame_of_enter_is_entered)
{
    entry_start();
    entry_click(ENTRY_X, NAME_Y);
    type("K7QF2M");
    key(&devices.keyboard.enter, true);
    TIDE_CHECK(entries() == (ENTRY_NAME | ENTRY_NAME * ENTRY_CHANGED));
    TIDE_CHECK(name_is("K7QF2M") && gui.editing == 0);
    key(&devices.keyboard.enter, false);
    entries();

    entry_click(ENTRY_X, COUNT_Y);
    type("12");
    key(&devices.keyboard.enter, true);
    TIDE_CHECK(entries() == (ENTRY_COUNT | ENTRY_COUNT * ENTRY_CHANGED));
    TIDE_CHECK(entry_count == 12);
    key(&devices.keyboard.enter, false);
    entries();

    entry_click(ENTRY_X, RATIO_Y);
    type("2.5");
    key(&devices.keyboard.enter, true);
    TIDE_CHECK(entries() == (ENTRY_RATIO | ENTRY_RATIO * ENTRY_CHANGED));
    TIDE_CHECK(entry_ratio == 2.5f);
    key(&devices.keyboard.enter, false);
    entries();

    start();
    int32_t players = 4;
    mouse(ENTRY_X, NAME_Y, false);
    int_field(&players);
    mouse(ENTRY_X, NAME_Y, true);
    int_field(&players);
    mouse(ENTRY_X, NAME_Y, false);
    type("31");
    key(&devices.keyboard.enter, true);
    TIDE_CHECK(int_field(&players) && players == 31);
}

TIDE_TEST(gui_typing_that_ends_another_way_isnt_entered)
{
    entry_start();
    mouse(ENTRY_X, NAME_Y, false);
    entries();

    entry_click(ENTRY_X, NAME_Y);
    type("K7");
    entries();
    TIDE_CHECK(entry_tap(&devices.keyboard.escape) == 0);
    TIDE_CHECK(gui.editing == 0 && name_is("K7"));
    entry_click(ENTRY_X, COUNT_Y);
    type("99");
    entries();
    TIDE_CHECK(entry_tap(&devices.keyboard.escape) == 0);
    TIDE_CHECK(gui.editing == 0 && entry_count == 4);
    entry_click(ENTRY_X, RATIO_Y);
    type("9");
    entries();
    TIDE_CHECK(entry_tap(&devices.keyboard.escape) == 0);
    TIDE_CHECK(gui.editing == 0 && entry_ratio == 0.5f);

    entry_click(ENTRY_X, NAME_Y);
    type("Q");
    entries();
    TIDE_CHECK(entry_tap(&devices.keyboard.tab) == 0);
    TIDE_CHECK(gui.editing == 0 && gui.focus == 6 && name_is("K7Q"));
    entry_click(ENTRY_X, COUNT_Y);
    type("7");
    entries();
    TIDE_CHECK(entry_tap(&devices.keyboard.tab) == ENTRY_COUNT * ENTRY_CHANGED);
    TIDE_CHECK(gui.editing == 0 && gui.focus == RATIO_ID && entry_count == 7);
    entry_click(ENTRY_X, RATIO_Y);
    type("3");
    entries();
    TIDE_CHECK(entry_tap(&devices.keyboard.tab) == ENTRY_RATIO * ENTRY_CHANGED);
    TIDE_CHECK(gui.editing == 0 && entry_ratio == 3.0f);

    entry_click(ENTRY_X, NAME_Y);
    type("F");
    entries();
    TIDE_CHECK(entry_click(900.0f, 900.0f) == 0);
    TIDE_CHECK(gui.editing == 0 && name_is("K7QF"));
    entry_click(ENTRY_X, RATIO_Y);
    type("6");
    entries();
    TIDE_CHECK(entry_click(900.0f, 900.0f) == ENTRY_RATIO * ENTRY_CHANGED);
    TIDE_CHECK(gui.editing == 0 && entry_ratio == 6.0f);
    entry_click(ENTRY_X, COUNT_Y);
    type("8");
    entries();
    TIDE_CHECK(entry_click(ENTRY_X, RATIO_Y) == ENTRY_COUNT * ENTRY_CHANGED);
    TIDE_CHECK(gui.editing == RATIO_ID && entry_count == 8);
    type("2");
    entries();
    TIDE_CHECK(entry_click(ENTRY_X, NAME_Y) == ENTRY_RATIO * ENTRY_CHANGED);
    TIDE_CHECK(gui.editing == 5 && entry_ratio == 2.0f);
    entry_click(ENTRY_X, COUNT_Y);
    type("7");
    entries();
    TIDE_CHECK(entry_click(ENTRY_X, NAME_Y) == ENTRY_COUNT * ENTRY_CHANGED);
    TIDE_CHECK(gui.editing == 5 && entry_count == 7 && name_is("K7QF"));

    for (int field = 0; field < 3; field++) {
        const float y = field == 0 ? NAME_Y : field == 1 ? COUNT_Y : RATIO_Y;
        for (int enter = 0; enter < 2; enter++) {
            entry_off = 0;
            entry_click(900.0f, 900.0f);
            entry_click(ENTRY_X, y);
            TIDE_REQUIRE(gui.editing != 0);
            type("1");
            entries();
            entry_off = 1 << field;
            key(&devices.keyboard.enter, enter == 1);
            TIDE_CHECK(entries() == 0);
            TIDE_CHECK(gui.editing == 0);
            key(&devices.keyboard.enter, false);
            TIDE_CHECK(entries() == 0);
            TIDE_CHECK(entry_tap(&devices.keyboard.enter) == 0);
        }
    }
    TIDE_CHECK(entry_count == 7 && entry_ratio == 2.0f);
}

TIDE_TEST(gui_a_gamepads_south_button_enters_a_number)
{
    entry_start();
    devices.gamepad.connected = true;
    mouse(ENTRY_X, COUNT_Y, false);
    entries();
    entry_click(ENTRY_X, COUNT_Y);
    type("9");
    entries();
    TIDE_CHECK(entry_tap(&devices.gamepad.buttonSouth) == (ENTRY_COUNT | ENTRY_COUNT * ENTRY_CHANGED));
    TIDE_CHECK(entry_count == 9 && gui.editing == 0);
    entry_click(ENTRY_X, RATIO_Y);
    type("2");
    entries();
    TIDE_CHECK(entry_tap(&devices.gamepad.buttonSouth) == (ENTRY_RATIO | ENTRY_RATIO * ENTRY_CHANGED));
    TIDE_CHECK(entry_ratio == 2.0f && gui.editing == 0);

    entry_click(ENTRY_X, NAME_Y);
    type("K7");
    entries();
    TIDE_CHECK(entry_tap(&devices.gamepad.buttonSouth) == 0);
    TIDE_CHECK(gui.editing == 5);
}

// A frame with the text field outside a modal and the int field in it, the
// modal up while `up`. What entries() says.
static int modal_entries(bool *up)
{
    begin();
    const tide_textref name = {.local = &entry_name};
    bool entered[2] = {true, true};
    bool changed[2] = {false, false};
    changed[0] = tide_gui_layout_text_field_entered(&gui, 5, "Name", name, &entered[0]);
    const int depth = tide_gui_begin_modal(&gui, 30, TIDE_ANCHOR_MIDDLE_CENTER, up);
    if (depth >= 0) {
        changed[1] = tide_gui_layout_int_field_entered(&gui, 6, "Count", &entry_count, &entered[1]);
        tide_gui_close(&gui, depth);
    } else {
        entered[1] = false;
    }
    end();
    return (entered[0] ? ENTRY_NAME : 0) | (entered[1] ? ENTRY_COUNT : 0) | (changed[0] ? ENTRY_NAME * ENTRY_CHANGED : 0)
         | (changed[1] ? ENTRY_COUNT * ENTRY_CHANGED : 0);
}

TIDE_TEST(gui_a_field_under_a_modal_isnt_entered)
{
    entry_start();
    bool up = false;
    mouse(ENTRY_X, NAME_Y, false);
    modal_entries(&up);
    mouse(ENTRY_X, NAME_Y, true);
    modal_entries(&up);
    mouse(ENTRY_X, NAME_Y, false);
    type("K7");
    TIDE_CHECK(modal_entries(&up) == ENTRY_NAME * ENTRY_CHANGED);
    TIDE_REQUIRE(gui.editing == 5);
    up = true;
    TIDE_CHECK(modal_entries(&up) == 0);
    key(&devices.keyboard.enter, true);
    TIDE_CHECK(modal_entries(&up) == 0);
    TIDE_CHECK(gui.editing == 6 && name_is("K7"));
    key(&devices.keyboard.enter, false);
    type("3");
    TIDE_CHECK(modal_entries(&up) == 0);
    key(&devices.keyboard.enter, true);
    TIDE_CHECK(modal_entries(&up) == (ENTRY_COUNT | ENTRY_COUNT * ENTRY_CHANGED));
    TIDE_CHECK(entry_count == 3 && up);
}

TIDE_TEST(gui_the_enter_that_entered_a_field_is_never_the_games)
{
    entry_start();
    entry_shown = ENTRY_NAME;
    mouse(ENTRY_X, NAME_Y, false);
    entries();
    entry_click(ENTRY_X, NAME_Y);
    type("K7QF2M");
    entries();
    key(&devices.keyboard.enter, true);
    TIDE_CHECK(entries() == ENTRY_NAME);
    TIDE_CHECK(!any_of(entry_sampled.keyboard.enter) && !any_of(entry_viewed.keyboard.enter));
    entry_shown = 0;
    for (int i = 0; i < 5; i++) {
        TIDE_CHECK(entries() == 0);
        TIDE_CHECK(!any_of(entry_sampled.keyboard.enter) && !any_of(entry_viewed.keyboard.enter));
    }
    TIDE_CHECK(gui.focus == 0 && gui.editing == 0);
    key(&devices.keyboard.enter, false);
    for (int i = 0; i < 2; i++) {
        entries();
        TIDE_CHECK(!any_of(entry_sampled.keyboard.enter) && !any_of(entry_viewed.keyboard.enter));
    }
    key(&devices.keyboard.enter, true);
    entries();
    TIDE_CHECK(entry_sampled.keyboard.enter.pressed && entry_sampled.keyboard.enter.down && entry_viewed.keyboard.enter.down);

    entry_start();
    devices.gamepad.connected = true;
    entry_shown = ENTRY_COUNT;
    mouse(ENTRY_X, NAME_Y, false);
    entries();
    entry_click(ENTRY_X, NAME_Y);
    type("5");
    entries();
    key(&devices.gamepad.buttonSouth, true);
    key(&devices.keyboard.numpadEnter, true);
    TIDE_CHECK(entries() == (ENTRY_COUNT | ENTRY_COUNT * ENTRY_CHANGED));
    entry_shown = 0;
    for (int i = 0; i < 3; i++) {
        entries();
        TIDE_CHECK(!any_of(entry_sampled.gamepad.buttonSouth) && !any_of(entry_viewed.gamepad.buttonSouth));
        TIDE_CHECK(!any_of(entry_sampled.keyboard.numpadEnter) && !any_of(entry_viewed.keyboard.numpadEnter));
    }
}

TIDE_TEST(gui_fields_at_a_rect_say_when_they_are_entered)
{
    entry_start();
    const tide_rect rect = {600.0f, 400.0f, 300.0f, 30.0f};
    const tide_textref name = {.local = &entry_name};
    int said = 0;
    for (int field = 0; field < 3; field++) {
        for (int frame = 0; frame < 6; frame++) {
            mouse(700.0f, 415.0f, frame == 1);
            if (frame == 3) type("7");
            key(&devices.keyboard.enter, frame == 4);
            begin();
            bool entered = true;
            bool changed = false;
            if (field == 0) changed = tide_gui_text_field_entered(&gui, 5, rect, "", name, &entered);
            if (field == 1) changed = tide_gui_int_field_entered(&gui, 6, rect, "", &entry_count, &entered);
            if (field == 2) changed = tide_gui_float_field_entered(&gui, 7, rect, "", &entry_ratio, &entered);
            end();
            TIDE_CHECK(entered == (frame == 4));
            TIDE_CHECK(changed == (field == 0 ? frame == 3 : frame == 4));
            if (entered) said |= 1 << field;
        }
        mouse(100.0f, 100.0f, true);
        begin();
        end();
    }
    TIDE_CHECK(said == 7 && name_is("7") && entry_count == 7 && entry_ratio == 7.0f);
}

// ---------------------------------------------------------------------------
// What was typed into a field is kept wherever the player goes next, whichever
// of the two fields a frame calls first. The GUI has one buffer for what's
// typed, which the field the player goes to takes: a number is only its
// field's once the field keeps it, so a field that's left before it ran has
// its number all the same. Fields at rects of their own, so that a frame calls
// them in any order.

enum { FORM_NAME, FORM_COUNT, FORM_RATIO, FORM_SIZE, FORM_TINT, FORM_FIELDS };

static tide_str form_name;
static int32_t form_count;
static float form_ratio;
static tide_float3 form_size;
static tide_color form_tint;
static int form_order[FORM_FIELDS];
static int form_called;
static int form_off;
static int form_entered;

#define FORM_X 700.0f                                   // In a field of one box
#define FORM_Y(field) (115.0f + 40.0f * (float)(field)) // A field's row
#define SIZE_X(part) (640.0f + 110.0f * (float)(part))  // In each box of the float3's
#define TINT_X(part) (668.0f + 70.0f * (float)(part))   // ...and of the color's, past its swatch
#define SIZE_ID(part) tide_gui_seed(8, (uint32_t)(part) + 1u)
#define TINT_ID(part) tide_gui_seed(9, (uint32_t)(part) + 1u)

// Starts over, with a frame calling these fields in this order.
static void form_start(const int *order, const int count)
{
    start();
    form_name = TIDE_STR_EMPTY;
    form_count = 4;
    form_ratio = 0.5f;
    form_size = tide_f3(1.0f, 2.0f, 3.0f);
    form_tint = (tide_color){0.25f, 0.5f, 0.75f, 1.0f};
    for (int i = 0; i < count; i++) form_order[i] = order[i];
    form_called = count;
    form_off = 0;
    form_entered = 0;
}

#define FORM(...) form_start((const int[]){__VA_ARGS__}, (int)(sizeof((const int[]){__VA_ARGS__}) / sizeof(int)))

// A frame of them in a host's order: the game's sample, then the frame. The
// fields that returned true, a bit each.
static int form(void)
{
    tide_devices sampled = devices;
    tide_gui_hide(&gui, &sampled);
    tide_devices_consume(&devices);
    begin();
    const tide_textref name = {.local = &form_name};
    int changed = 0;
    for (int i = 0; i < form_called; i++) {
        const int field = form_order[i];
        const tide_rect rect = {600.0f, 100.0f + 40.0f * (float)field, 300.0f, 30.0f};
        const int depth = tide_gui_begin_disabled(&gui, (form_off >> field & 1) != 0);
        bool entered = false;
        bool said = false;
        if (field == FORM_NAME) said = tide_gui_text_field_entered(&gui, 5, rect, "", name, &entered);
        if (field == FORM_COUNT) said = tide_gui_int_field_entered(&gui, 6, rect, "", &form_count, &entered);
        if (field == FORM_RATIO) said = tide_gui_float_field_entered(&gui, 7, rect, "", &form_ratio, &entered);
        if (field == FORM_SIZE) said = tide_gui_float3_field(&gui, 8, rect, "", &form_size);
        if (field == FORM_TINT) said = tide_gui_color_field(&gui, 9, rect, "", &form_tint);
        tide_gui_close(&gui, depth);
        if (said) changed |= 1 << field;
        if (entered) form_entered |= 1 << field;
    }
    end();
    return changed;
}

// A click: the press, then the release. The fields that changed in either frame.
static int form_click(const float x, const float y)
{
    mouse(x, y, true);
    const int pressed = form();
    mouse(x, y, false);
    return pressed | form();
}

// A key down for a frame, then up for one. The fields that changed in the first.
static int form_tap(tide_button *b)
{
    key(b, true);
    const int changed = form();
    key(b, false);
    form();
    return changed;
}

static bool form_name_is(const char *text)
{
    return form_name.bytes == (int32_t)strlen(text) && memcmp(form_name.ptr, text, strlen(text)) == 0;
}

TIDE_TEST(gui_vector_and_color_fields_keep_untouched_values)
{
    const float original = 0.123456791f;
    for (int field = FORM_SIZE; field <= FORM_TINT; field++) {
        for (int earlier = 0; earlier < 2; earlier++) {
            for (int enter = 0; enter < 2; enter++) {
                if (earlier) FORM(FORM_NAME, field);
                else FORM(field, FORM_NAME);
                float *value = field == FORM_SIZE ? &form_size.y : &form_tint.b;
                *value = original;
                const float x = field == FORM_SIZE ? SIZE_X(1) : TINT_X(2);
                mouse(x, FORM_Y(field), false);
                form();
                TIDE_CHECK(form_click(x, FORM_Y(field)) == 0);
                TIDE_REQUIRE(gui.editing == (field == FORM_SIZE ? SIZE_ID(1) : TINT_ID(2)));
                if (enter) TIDE_CHECK(form_tap(&devices.keyboard.enter) == 0);
                else TIDE_CHECK(form_click(FORM_X, FORM_Y(FORM_NAME)) == 0);
                TIDE_CHECK(memcmp(value, &original, sizeof original) == 0);
            }
        }
    }
}

TIDE_TEST(gui_a_number_is_kept_when_the_player_clicks_another_field)
{
    for (int number = FORM_COUNT; number <= FORM_RATIO; number++) {
        for (int earlier = 0; earlier < 2; earlier++) {
            if (earlier) FORM(FORM_NAME, number);
            else FORM(number, FORM_NAME);
            mouse(FORM_X, FORM_Y(number), false);
            form();
            TIDE_CHECK(form_click(FORM_X, FORM_Y(number)) == 0);
            TIDE_REQUIRE(gui.editing != 0);
            type("7");
            TIDE_CHECK(form() == 0 && form_count == 4 && form_ratio == 0.5f);
            mouse(FORM_X, FORM_Y(FORM_NAME), true);
            TIDE_CHECK(form() == 1 << number);
            TIDE_CHECK(number == FORM_COUNT ? form_count == 7 : form_ratio == 7.0f);
            TIDE_CHECK(gui.editing == 5 && gui.focus == 5);
            mouse(FORM_X, FORM_Y(FORM_NAME), false);
            TIDE_CHECK(form() == 0);
            TIDE_CHECK(form_entered == 0);
            type("K7");
            TIDE_CHECK(form() == 1 << FORM_NAME && form_name_is("K7"));
            TIDE_CHECK(number == FORM_COUNT ? form_count == 7 : form_ratio == 7.0f);
        }
    }
}

TIDE_TEST(gui_a_click_between_two_number_fields_keeps_the_number)
{
    FORM(FORM_COUNT, FORM_RATIO);
    mouse(FORM_X, FORM_Y(FORM_RATIO), false);
    form();
    form_click(FORM_X, FORM_Y(FORM_RATIO));
    type("2.5");
    form();
    TIDE_CHECK(form_click(FORM_X, FORM_Y(FORM_COUNT)) == 1 << FORM_RATIO);
    TIDE_CHECK(form_ratio == 2.5f && form_count == 4 && gui.editing == 6);
    type("31");
    TIDE_CHECK(form() == 0 && form_count == 4);
    TIDE_CHECK(form_click(FORM_X, FORM_Y(FORM_RATIO)) == 1 << FORM_COUNT);
    TIDE_CHECK(form_count == 31 && form_ratio == 2.5f && gui.editing == RATIO_ID);
    type("-");
    form();
    TIDE_CHECK(form_click(FORM_X, FORM_Y(FORM_COUNT)) == 0);
    TIDE_CHECK(form_ratio == 2.5f && gui.editing == 6);
    type("+");
    form();
    TIDE_CHECK(form_click(FORM_X, FORM_Y(FORM_RATIO)) == 0);
    TIDE_CHECK(form_count == 31 && gui.editing == RATIO_ID);
    type("2.5");
    form();
    TIDE_CHECK(form_click(FORM_X, FORM_Y(FORM_COUNT)) == 0);
    TIDE_CHECK(form_ratio == 2.5f && form_count == 31);
    TIDE_CHECK(form_entered == 0);
}

TIDE_TEST(gui_a_number_field_and_a_text_field_keep_what_was_typed_between_them)
{
    for (int number_first = 0; number_first < 2; number_first++) {
        if (number_first) FORM(FORM_COUNT, FORM_NAME);
        else FORM(FORM_NAME, FORM_COUNT);
        mouse(FORM_X, FORM_Y(FORM_NAME), false);
        form();
        form_click(FORM_X, FORM_Y(FORM_NAME));
        type("K7");
        TIDE_CHECK(form() == 1 << FORM_NAME);
        TIDE_CHECK(form_click(FORM_X, FORM_Y(FORM_COUNT)) == 0);
        TIDE_CHECK(form_name_is("K7") && gui.editing == 6);
        type("9");
        TIDE_CHECK(form() == 0 && form_count == 4 && form_name_is("K7"));
        TIDE_CHECK(form_click(FORM_X, FORM_Y(FORM_NAME)) == 1 << FORM_COUNT);
        TIDE_CHECK(form_count == 9 && form_name_is("K7") && gui.editing == 5);
        type("Q");
        TIDE_CHECK(form() == 1 << FORM_NAME && form_name_is("K7Q"));
        TIDE_CHECK(form_click(FORM_X, FORM_Y(FORM_COUNT)) == 0);
        TIDE_CHECK(form_count == 9 && form_name_is("K7Q"));
        TIDE_CHECK(form_entered == 0);
    }
}

TIDE_TEST(gui_each_box_of_a_vector_or_a_color_field_keeps_its_number)
{
    FORM(FORM_SIZE, FORM_TINT);
    mouse(SIZE_X(2), FORM_Y(FORM_SIZE), false);
    form();
    form_click(SIZE_X(2), FORM_Y(FORM_SIZE));
    TIDE_REQUIRE(gui.editing == SIZE_ID(2));
    type("5");
    form();
    TIDE_CHECK(form_click(SIZE_X(0), FORM_Y(FORM_SIZE)) == 1 << FORM_SIZE);
    TIDE_CHECK(form_size.x == 1.0f && form_size.y == 2.0f && form_size.z == 5.0f && gui.editing == SIZE_ID(0));
    type("8");
    form();
    TIDE_CHECK(form_click(SIZE_X(1), FORM_Y(FORM_SIZE)) == 1 << FORM_SIZE);
    TIDE_CHECK(form_size.x == 8.0f && form_size.y == 2.0f && form_size.z == 5.0f && gui.editing == SIZE_ID(1));

    form_click(TINT_X(3), FORM_Y(FORM_TINT));
    TIDE_REQUIRE(gui.editing == TINT_ID(3));
    type("0.5");
    form();
    TIDE_CHECK(form_click(SIZE_X(1), FORM_Y(FORM_SIZE)) == 1 << FORM_TINT);
    TIDE_CHECK(form_tint.a == 0.5f && form_size.y == 2.0f);
    form_click(TINT_X(2), FORM_Y(FORM_TINT));
    TIDE_REQUIRE(gui.editing == TINT_ID(2));
    type("7");
    form();
    TIDE_CHECK(form_click(TINT_X(0), FORM_Y(FORM_TINT)) == 1 << FORM_TINT);
    TIDE_CHECK(form_tint.r == 0.25f && form_tint.g == 0.5f && form_tint.b == 1.0f && form_tint.a == 0.5f);
}

TIDE_TEST(gui_escape_drops_a_number_whichever_field_is_clicked_next)
{
    FORM(FORM_NAME, FORM_COUNT, FORM_RATIO);
    mouse(FORM_X, FORM_Y(FORM_COUNT), false);
    form();
    form_click(FORM_X, FORM_Y(FORM_COUNT));
    type("99");
    form();
    TIDE_CHECK(form_tap(&devices.keyboard.escape) == 0);
    TIDE_CHECK(gui.editing == 0 && form_count == 4);
    TIDE_CHECK(form_click(FORM_X, FORM_Y(FORM_NAME)) == 0);
    TIDE_CHECK(form_count == 4 && gui.editing == 5);
    form_click(FORM_X, FORM_Y(FORM_RATIO));
    type("9");
    form();
    key(&devices.keyboard.escape, true);
    mouse(FORM_X, FORM_Y(FORM_NAME), true);
    TIDE_CHECK(form() == 0);
    TIDE_CHECK(form_ratio == 0.5f);
    key(&devices.keyboard.escape, false);
    mouse(FORM_X, FORM_Y(FORM_NAME), false);
    TIDE_CHECK(form() == 0);
    form_click(FORM_X, FORM_Y(FORM_COUNT));
    type("99");
    form();
    form_tap(&devices.keyboard.escape);
    TIDE_CHECK(form_click(FORM_X, FORM_Y(FORM_RATIO)) == 0);
    TIDE_CHECK(form_count == 4 && form_ratio == 0.5f);
    TIDE_CHECK(form_entered == 0);
}

TIDE_TEST(gui_tab_keeps_a_number_whichever_way_the_focus_goes)
{
    FORM(FORM_NAME, FORM_COUNT, FORM_RATIO);
    mouse(FORM_X, FORM_Y(FORM_COUNT), false);
    form();
    form_click(FORM_X, FORM_Y(FORM_COUNT));
    type("7");
    form();
    TIDE_CHECK(form_tap(&devices.keyboard.tab) == 1 << FORM_COUNT);
    TIDE_CHECK(form_count == 7 && gui.focus == RATIO_ID && gui.editing == 0);
    form_click(FORM_X, FORM_Y(FORM_COUNT));
    type("8");
    form();
    key(&devices.keyboard.leftShift, true);
    TIDE_CHECK(form_tap(&devices.keyboard.tab) == 1 << FORM_COUNT);
    TIDE_CHECK(form_count == 8 && gui.focus == 5 && gui.editing == 0);
    key(&devices.keyboard.leftShift, false);
    form();

    form_click(FORM_X, FORM_Y(FORM_COUNT));
    type("9");
    form();
    key(&devices.keyboard.leftShift, true);
    key(&devices.keyboard.tab, true);
    type("Q");
    TIDE_CHECK(form() == (1 << FORM_COUNT | 1 << FORM_NAME));
    TIDE_CHECK(form_count == 9 && form_name_is("Q") && gui.editing == 5);
    key(&devices.keyboard.tab, false);
    key(&devices.keyboard.leftShift, false);
    form();
    form_click(FORM_X, FORM_Y(FORM_RATIO));
    type("1.5");
    form();
    key(&devices.keyboard.leftShift, true);
    key(&devices.keyboard.tab, true);
    type("3");
    TIDE_CHECK(form() == 1 << FORM_RATIO);
    TIDE_CHECK(form_ratio == 1.5f && form_count == 9 && gui.editing == 6);
    key(&devices.keyboard.tab, false);
    key(&devices.keyboard.leftShift, false);
    form();
    TIDE_CHECK(form_tap(&devices.keyboard.enter) == 1 << FORM_COUNT);
    TIDE_CHECK(form_count == 3 && form_ratio == 1.5f);
}

TIDE_TEST(gui_a_number_field_that_goes_while_its_typed_into_keeps_nothing)
{
    FORM(FORM_NAME, FORM_COUNT);
    mouse(FORM_X, FORM_Y(FORM_COUNT), false);
    form();
    form_click(FORM_X, FORM_Y(FORM_COUNT));
    type("7");
    form();
    TIDE_REQUIRE(gui.editing == 6);
    form_called = 1;
    TIDE_CHECK(form() == 0 && gui.editing == 0 && gui.focus == 0);
    form_called = 2;
    TIDE_CHECK(form() == 0 && form_count == 4 && gui.editing == 0);

    form_click(FORM_X, FORM_Y(FORM_COUNT));
    type("7");
    form();
    form_called = 1;
    mouse(FORM_X, FORM_Y(FORM_NAME), true);
    TIDE_CHECK(form() == 0 && gui.editing == 5);
    form_called = 2;
    mouse(FORM_X, FORM_Y(FORM_NAME), false);
    TIDE_CHECK(form() == 0 && form_count == 4);
    TIDE_CHECK(form() == 0 && form_count == 4);
    type("K7");
    TIDE_CHECK(form() == 1 << FORM_NAME && form_name_is("K7") && form_count == 4);
}

TIDE_TEST(gui_a_number_field_disabled_as_another_is_clicked_drops_its_number)
{
    for (int earlier = 0; earlier < 2; earlier++) {
        if (earlier) FORM(FORM_NAME, FORM_COUNT);
        else FORM(FORM_COUNT, FORM_NAME);
        mouse(FORM_X, FORM_Y(FORM_COUNT), false);
        form();
        form_click(FORM_X, FORM_Y(FORM_COUNT));
        type("7");
        form();
        form_off = 1 << FORM_COUNT;
        TIDE_CHECK(form_click(FORM_X, FORM_Y(FORM_NAME)) == 0);
        TIDE_CHECK(form_count == 4 && gui.editing == 5);
        form_off = 0;
        TIDE_CHECK(form() == 0 && form_count == 4);
    }
}

// A frame with an int field and a modal of one button, which the frame calls
// before the field or after it. Whether the field changed.
static bool count_under_modal(const bool modal_first, bool *up)
{
    begin();
    bool changed = false;
    for (int i = 0; i < 2; i++) {
        if ((i == 0) != modal_first) {
            const tide_rect rect = {600.0f, 100.0f, 300.0f, 30.0f};
            changed = tide_gui_int_field(&gui, 6, rect, "", &form_count);
            continue;
        }
        const int depth = tide_gui_begin_modal(&gui, 30, TIDE_ANCHOR_MIDDLE_CENTER, up);
        if (depth < 0) continue;
        tide_gui_layout_button(&gui, 31, "Close");
        tide_gui_close(&gui, depth);
    }
    end();
    return changed;
}

TIDE_TEST(gui_a_modal_over_a_number_field_keeps_what_was_typed)
{
    for (int modal_first = 0; modal_first < 2; modal_first++) {
        start();
        form_count = 4;
        bool up = false;
        mouse(FORM_X, 115.0f, false);
        count_under_modal(modal_first != 0, &up);
        mouse(FORM_X, 115.0f, true);
        count_under_modal(modal_first != 0, &up);
        mouse(FORM_X, 115.0f, false);
        type("7");
        TIDE_CHECK(!count_under_modal(modal_first != 0, &up));
        TIDE_REQUIRE(gui.editing == 6);
        up = true;
        TIDE_CHECK(!count_under_modal(modal_first != 0, &up) && form_count == 4);
        TIDE_CHECK(count_under_modal(modal_first != 0, &up));
        TIDE_CHECK(form_count == 7 && gui.editing == 0 && gui.focus == 31);
        TIDE_CHECK(!count_under_modal(modal_first != 0, &up) && form_count == 7);
    }
}

// A theme's sizes lay the widgets out, its border and shading make their parts
// shapes, and numbers that aren't numbers, or are out of range, are the nearest
// that are.
TIDE_TEST(gui_a_themes_sizes_lay_out_and_bad_numbers_are_fitted)
{
    start();
    tide_gui_theme theme = tide_gui_theme_default();
    theme.text_size = 24.0f;
    theme.spacing = 20.0f;
    theme.border_width = 1.0f;
    theme.shading = 0.5f;
    tide_rect first = {0}, second = {0};
    for (int frame = 0; frame < 2; frame++) {
        begin();
        tide_gui_set_theme(&gui, theme);
        first = tide_gui_layout_reserve(&gui, tide_f2(10.0f, 10.0f));
        tide_gui_layout_button(&gui, 1, "Play");
        second = tide_gui_layout_reserve(&gui, tide_f2(10.0f, 10.0f));
        end();
    }
    TIDE_CHECK(second.y - first.y == 10.0f + 20.0f + 36.0f + 20.0f); // A button is the text size and 12 tall
    bool shaded = false;
    for (uint32_t i = 0; i < draw.count; i++) {
        const tide_draw_command *c = &draw.commands[i];
        if (c->kind != TIDE_DRAW_SHAPE || c->shape >= draw.shape_count) continue;
        const tide_draw_shape_data *s = &draw.shapes[c->shape];
        shaded |= s->outline == 1.0f && s->color2.r == c->color.r * 0.5f;
    }
    TIDE_CHECK(shaded);

    theme = tide_gui_theme_default();
    theme.text_size = NAN;
    theme.spacing = -5.0f;
    theme.shading = 7.0f;
    theme.transition = INFINITY;
    theme.padding = NAN;
    begin();
    tide_gui_set_theme(&gui, theme);
    TIDE_CHECK(gui.theme.text_size == 16.0f && gui.theme.spacing == 0.0f && gui.theme.shading == 1.0f);
    TIDE_CHECK(gui.theme.transition == 0.0f && gui.theme.padding == 12.0f);
    end();
}

// An anchored area's panel eases to its content's new size rather than
// jumping, centered as it goes, with its content clipped to it meanwhile.
static float eased_area_frame(const int buttons)
{
    begin();
    gui.seconds = 0.05f;
    const int depth = tide_gui_begin_area_at(&gui, 99, TIDE_ANCHOR_MIDDLE_CENTER);
    for (int i = 0; i < buttons; i++) tide_gui_layout_button(&gui, (uint32_t)(i + 1), "Play");
    tide_gui_close(&gui, depth);
    end();
    for (uint32_t i = 0; i < draw.count; i++) { // The panel: the area's first shape in the panel's color
        const tide_draw_command *c = &draw.commands[i];
        if (same_color(c->color, (tide_color){0.07f, 0.07f, 0.1f, 0.9f})) return c->b.y;
    }
    return -1.0f;
}

TIDE_TEST(gui_an_anchored_area_eases_to_its_new_size)
{
    start();
    const float one = 28.0f + 2.0f * 12.0f, three = 3.0f * 28.0f + 2.0f * 5.0f + 2.0f * 12.0f;
    for (int frame = 0; frame < 3; frame++) TIDE_CHECK(eased_area_frame(1) == one);
    const float first = eased_area_frame(3);
    TIDE_CHECK(first > one && first < three); // On its way
    TIDE_CHECK(gui.list.clip_count == 0);     // Nothing was clipped the frame it started
    const float second = eased_area_frame(3);
    TIDE_CHECK(second > first && second < three);
    TIDE_CHECK(gui.list.clip_count == 1); // ...and its content is clipped to it while it grows
    TIDE_CHECK(gui.list.clips[0].rect.height == second);
    for (int frame = 0; frame < 60; frame++) eased_area_frame(3);
    TIDE_CHECK(eased_area_frame(3) == three); // There, exactly
    TIDE_CHECK(gui.list.clip_count == 0);
    // With no frame time it's at its size at once, as before
    begin();
    gui.seconds = 0.0f;
    const int depth = tide_gui_begin_area_at(&gui, 99, TIDE_ANCHOR_MIDDLE_CENTER);
    tide_gui_layout_button(&gui, 1, "Play");
    tide_gui_close(&gui, depth);
    end();
    TIDE_CHECK(draw.commands[2].b.y == one);
}
