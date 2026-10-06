#include <string.h>

#include "tide/gui.h"
#include "tide/text.h"
#include "tide_test.h"

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
    tide_pointer_poll(&devices, true); // As the platform polls it: the pointer follows the mouse
    tide_gui_begin(&gui, &devices, tide_f2(1920.0f, 1080.0f), NULL);
}

static void end(void)
{
    tide_draw_reset(&draw);
    tide_gui_end(&gui, &draw);
    devices.keyboard.text.count = 0; // Typed characters last one frame
}

static void type(const char *text)
{
    for (const char *c = text; *c; c++) devices.keyboard.text.chars[devices.keyboard.text.count++] = (uint32_t)*c;
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
    // Play is at the top left, 28 tall; Quit below it after 5 of spacing.
    mouse(20.0f, 47.0f, false);
    TIDE_CHECK(two_buttons() == 0);
    mouse(20.0f, 47.0f, true);
    TIDE_CHECK(two_buttons() == 0); // Pressed, not released yet
    mouse(20.0f, 47.0f, false);
    TIDE_CHECK(two_buttons() == 2);
    TIDE_CHECK(two_buttons() == 0);

    // A press that leaves the button before it's released doesn't count.
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
    TIDE_CHECK(draw.commands[2].kind == TIDE_DRAW_RECT); // The button, below the label
    TIDE_CHECK(draw.commands[2].a.y == 28.0f + 5.0f + 14.0f);

    begin(); // A frame without a GUI adds nothing
    end();
    TIDE_CHECK(draw.count == 0);
}

// What the views left set is theirs: the GUI draws on the whole screen, in its
// pixels, with its own colors, over what's there.
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
    tide_draw_rect(&draw, tide_f2(5.0f, 5.0f), tide_f2(2.0f, 2.0f), TIDE_COLOR_WHITE); // A view's, through all of them
    const uint32_t block = tide_draw_push_tint(&draw, TIDE_COLOR_BLUE);                // ...and one whose block is open
    tide_gui_end(&gui, &draw);
    TIDE_REQUIRE(draw.count == 6);
    TIDE_CHECK(draw.commands[0].kind == TIDE_DRAW_CAMERA && draw.commands[1].kind == TIDE_DRAW_CLIP);
    TIDE_CHECK(draw.commands[2].kind == TIDE_DRAW_SHAPE && draw.commands[2].blend == TIDE_BLEND_ADD); // Turned: a smooth one
    TIDE_CHECK(draw.commands[3].kind == TIDE_DRAW_SCREEN && draw.commands[4].kind == TIDE_DRAW_NO_CLIP);
    const tide_draw_command *text = &draw.commands[5];
    TIDE_REQUIRE(text->kind == TIDE_DRAW_TEXT);
    TIDE_CHECK(text->blend == TIDE_BLEND_ALPHA && text->words.turned == 0);
    TIDE_CHECK(text->a.x == label.a.x && text->a.y == label.a.y && text->b.x == label.b.x);
    TIDE_CHECK(memcmp(&text->color, &label.color, sizeof label.color) == 0);
    // The block's end puts back what it found, as it would have without the GUI: the views' red
    tide_draw_pop(&draw, block);
    TIDE_CHECK(draw.tinted && draw.tint.r == 1.0f && draw.tint.b == 0.0f && draw.tint.a == 0.5f);
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

    // The slider is below the toggle: its track goes from x 6 to 6 + 180 + 56 - 56 - 12.
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
    TIDE_CHECK(tide_gui_id(&gui, 7, 3) == a); // The same next frame
    TIDE_CHECK(tide_gui_id(&gui, 7, 3) == b);
    end();
}

TIDE_TEST(gui_keyboard_navigation)
{
    start();
    TIDE_CHECK(two_buttons() == 0); // Lays them out, for the order
    key(&devices.keyboard.downArrow, true);
    TIDE_CHECK(two_buttons() == 0);
    TIDE_CHECK(gui.focus == 10); // The first one
    key(&devices.keyboard.downArrow, false);
    TIDE_CHECK(two_buttons() == 0);
    key(&devices.keyboard.downArrow, true);
    TIDE_CHECK(two_buttons() == 0);
    TIDE_CHECK(gui.focus == 20);
    key(&devices.keyboard.downArrow, false);
    key(&devices.keyboard.enter, true);
    TIDE_CHECK(two_buttons() == 2);
    key(&devices.keyboard.enter, false);

    // While a widget has the focus, the game doesn't see the keyboard.
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
    // The game sampled the devices: the arrows are its, not the GUI's.
    tide_devices sampled = devices;
    tide_gui_hide(&gui, &sampled);
    key(&devices.keyboard.downArrow, true);
    TIDE_CHECK(two_buttons() == 0);
    TIDE_CHECK(gui.focus == 0);
    key(&devices.keyboard.downArrow, false);
    // Tab always moves the focus.
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
    TIDE_CHECK(touch_frame(TIDE_TOUCH_BEGAN, NONE, 20.0f, 47.0f) == 0); // On Quit
    TIDE_CHECK(gui.active == 20);
    tide_devices sampled = devices;
    tide_gui_hide(&gui, &sampled);
    TIDE_CHECK(!sampled.touchscreen.primaryTouch.press.pressed && !sampled.touchscreen.touches.at[0].press.pressed);
    TIDE_CHECK(!sampled.pointer.press.pressed); // The finger is the GUI's
    TIDE_CHECK(touch_frame(TIDE_TOUCH_ENDED, NONE, 20.0f, 47.0f) == 2); // Lifted on it: a click
    TIDE_CHECK(touch_frame(NONE, NONE, 0.0f, 0.0f) == 0);
    TIDE_CHECK(gui.hot == 0); // Nothing stays hovered where the finger lifted

    // A tap between two frames clicks too.
    TIDE_CHECK(touch_frame(TIDE_TOUCH_BEGAN, TIDE_TOUCH_ENDED, 20.0f, 20.0f) == 0);
    TIDE_CHECK(touch_frame(NONE, NONE, 0.0f, 0.0f) == 1);
}

// A game with no input, and a menu outside any match, sample nothing, so
// nothing clears a touchscreen's slots of the fingers that lifted. Buttons
// answer every tap all the same, past as many as there are slots.
TIDE_TEST(gui_buttons_answer_every_tap_where_nothing_samples)
{
    start();
    for (int i = 0; i < 3 * TIDE_TOUCHES; i++) {
        const bool quit = i % 2 == 0; // Quit and Play, in turn
        const float y = quit ? 47.0f : 20.0f;
        TIDE_CHECK(touch_frame(TIDE_TOUCH_BEGAN, NONE, 20.0f, y) == 0);
        TIDE_CHECK(gui.active == (quit ? 20u : 10u));
        TIDE_CHECK(touch_frame(TIDE_TOUCH_ENDED, NONE, 20.0f, y) == (quit ? 2 : 1));
    }
    // ...and taps shorter than a frame
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
    tide_touches_poll(&devices.touchscreen); // No finger did anything this poll
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
    TIDE_CHECK(sampled_frame() == 0); // The buttons are up, and no finger is anywhere
    // A finger touches Quit. The game samples before the GUI sees the touch,
    // and still doesn't get it; nor do views.
    TIDE_CHECK(!finger_sample(TIDE_TOUCH_BEGAN, NONE, 20.0f, 47.0f));
    TIDE_CHECK(sampled_frame() == 0);
    TIDE_CHECK(gui.active == 20);
    TIDE_CHECK(!finger_sample(NONE, NONE, 0.0f, 0.0f));
    TIDE_CHECK(sampled_frame() == 0);
    TIDE_CHECK(!finger_sample(TIDE_TOUCH_ENDED, NONE, 20.0f, 47.0f));
    TIDE_CHECK(sampled_frame() == 2); // Lifted on it: a click, and not the game's
    TIDE_CHECK(!finger_sample(NONE, NONE, 0.0f, 0.0f));
    TIDE_CHECK(sampled_frame() == 0);

    // A tap between two frames, too.
    TIDE_CHECK(!finger_sample(TIDE_TOUCH_BEGAN, TIDE_TOUCH_ENDED, 20.0f, 20.0f));
    TIDE_CHECK(sampled_frame() == 0);
    TIDE_CHECK(!finger_sample(NONE, NONE, 0.0f, 0.0f));
    TIDE_CHECK(sampled_frame() == 1);
    TIDE_CHECK(!finger_sample(NONE, NONE, 0.0f, 0.0f));
    TIDE_CHECK(sampled_frame() == 0);

    // Away from the buttons, a finger is the game's and the views', from the frame it touches.
    TIDE_CHECK(finger_sample(TIDE_TOUCH_BEGAN, NONE, 900.0f, 900.0f));
    TIDE_CHECK(sampled_frame() == 4);
}

// A view reads a finger's lift in the frame it lifts, for that frame, with the
// finger's id and where it lifted: whether a sample emptied its slot before
// the frame's GUI began (a tick every frame) or none did (ticks rarer than
// frames).
TIDE_TEST(gui_views_see_a_finger_lift_whenever_the_game_samples)
{
    const tide_touch *primary = &gui.devices.touchscreen.primaryTouch;
    const tide_touch *slot = &gui.devices.touchscreen.touches.at[0];
    const tide_pointer *pointer = &gui.devices.pointer;

    start();
    finger_sample(TIDE_TOUCH_BEGAN, NONE, 900.0f, 600.0f); // Far from the buttons
    sampled_frame();
    TIDE_CHECK(primary->press.down && slot->press.down && primary->id == 1);
    finger_sample(TIDE_TOUCH_MOVED, NONE, 910.0f, 600.0f);
    sampled_frame();
    TIDE_CHECK(primary->press.pressed && !primary->press.down && !primary->press.up);
    finger_sample(TIDE_TOUCH_ENDED, NONE, 910.0f, 600.0f); // The sample sees it lift, and empties its slot
    TIDE_CHECK(devices.touchscreen.primaryTouch.id == 0);
    sampled_frame();
    TIDE_CHECK(primary->press.up && !primary->press.pressed && !primary->press.down && primary->id == 1);
    TIDE_CHECK(slot->press.up && !slot->press.pressed && slot->id == 1);
    TIDE_CHECK(primary->position.x == 910.0f && slot->position.x == 910.0f); // Where it was
    TIDE_CHECK(pointer->press.up && pointer->position.x == 910.0f && pointer->position.y == 480.0f);
    finger_sample(NONE, NONE, 0.0f, 0.0f);
    sampled_frame();
    TIDE_CHECK(!primary->press.up && !slot->press.up && primary->id == 0 && slot->id == 0);
    TIDE_CHECK(!pointer->press.up && pointer->position.x == 910.0f); // The pointer stays where the finger lifted

    // A tap shorter than a frame: down in one frame, up in the next.
    finger_sample(TIDE_TOUCH_BEGAN, TIDE_TOUCH_ENDED, 900.0f, 600.0f);
    sampled_frame();
    TIDE_CHECK(primary->press.down && !primary->press.up && slot->press.down);
    finger_sample(NONE, NONE, 0.0f, 0.0f);
    sampled_frame();
    TIDE_CHECK(primary->press.up && !primary->press.down && slot->press.up && primary->id == 2);

    // No sample between the lift and the frame.
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
    // Between two frames, the mouse comes onto Quit and presses: the GUI never
    // saw it there, and the click is still the GUI's.
    TIDE_CHECK(!mouse_sample(20.0f, 47.0f, true));
    TIDE_CHECK(sampled_frame() == 0);
    TIDE_CHECK(gui.active == 20);
    TIDE_CHECK(!mouse_sample(20.0f, 47.0f, false));
    TIDE_CHECK(sampled_frame() == 2);

    // And it leaves Quit and presses between two frames: the game's and the
    // views', though the GUI last saw it on the button.
    TIDE_CHECK(!mouse_sample(20.0f, 47.0f, false));
    TIDE_CHECK(sampled_frame() == 0);
    TIDE_CHECK(mouse_sample(900.0f, 900.0f, true));
    TIDE_CHECK(sampled_frame() == 4);
}

TIDE_TEST(gui_a_locked_cursor_is_on_no_widget)
{
    start();
    // In the host's order: the sample, then the frame. The mouse is on Quit.
    TIDE_CHECK(!mouse_sample(20.0f, 47.0f, false));
    TIDE_CHECK(sampled_frame() == 0);
    TIDE_CHECK(gui.hot == 20);

    // The window holds the cursor, which stays where it was: on Quit. It's
    // hidden, so it hovers nothing, and a click is the game's and the views'.
    devices.mouse.locked = true;
    TIDE_CHECK(mouse_sample(20.0f, 47.0f, true));
    TIDE_CHECK(sampled_frame() == 4);
    TIDE_CHECK(gui.hot == 0 && gui.active == 0);
    TIDE_CHECK(gui.devices.mouse.locked); // Views read that it's locked
    TIDE_CHECK(mouse_sample(20.0f, 47.0f, false)); // Let go: the game's too
    TIDE_CHECK(sampled_frame() == 4); // ...and the views', and no button's click

    // The cursor comes back with the button held, as when a browser takes the
    // lock back: it was pressed for the game, so it presses no widget, and
    // letting it go on Quit is no click.
    TIDE_CHECK(mouse_sample(20.0f, 47.0f, true));
    TIDE_CHECK(sampled_frame() == 4);
    devices.mouse.locked = false;
    mouse_sample(20.0f, 47.0f, true);
    TIDE_CHECK(sampled_frame() == 0);
    TIDE_CHECK(gui.active == 0);
    TIDE_CHECK(!mouse_sample(20.0f, 47.0f, false));
    TIDE_CHECK(sampled_frame() == 0);

    // From then on it's a pointer like any other.
    TIDE_CHECK(!mouse_sample(20.0f, 47.0f, true));
    TIDE_CHECK(sampled_frame() == 0);
    TIDE_CHECK(gui.active == 20);
    TIDE_CHECK(!mouse_sample(20.0f, 47.0f, false));
    TIDE_CHECK(sampled_frame() == 2);

    // A finger isn't the mouse: it presses what it touches while the mouse's
    // cursor is locked.
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
    // The area is in the middle of the screen, 52 tall: its button, and 12 of padding around it.
    for (int frame = 0; frame < 3; frame++) {
        finger_sample(NONE, NONE, 0.0f, 0.0f);
        area_frame(true);
    }
    // On the area's padding, which no widget has.
    TIDE_CHECK(!finger_sample(TIDE_TOUCH_BEGAN, NONE, 962.0f, 518.0f));
    TIDE_CHECK(!area_frame(true));
    TIDE_CHECK(gui.active == 0);
    TIDE_CHECK(!finger_sample(TIDE_TOUCH_ENDED, NONE, 962.0f, 518.0f));
    TIDE_CHECK(!area_frame(true));
    // On a disabled button: it doesn't work, and it's still the GUI's.
    TIDE_CHECK(!finger_sample(TIDE_TOUCH_BEGAN, NONE, 150.0f, 120.0f));
    TIDE_CHECK(!area_frame(true));
    TIDE_CHECK(!finger_sample(TIDE_TOUCH_ENDED, NONE, 150.0f, 120.0f));
    TIDE_CHECK(!area_frame(true));

    // An area that wasn't up last frame takes the finger a frame after it
    // comes up under it: nothing said it was coming.
    finger_sample(NONE, NONE, 0.0f, 0.0f);
    area_frame(false);
    TIDE_CHECK(finger_sample(TIDE_TOUCH_BEGAN, NONE, 962.0f, 518.0f));
    TIDE_CHECK(area_frame(true));
    TIDE_CHECK(!finger_sample(NONE, NONE, 0.0f, 0.0f));
    TIDE_CHECK(!area_frame(true));
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
    const tide_rect widget = {0.0f, 0.0f, 400.0f, 400.0f}; // Under Play, which is at the top left
    TIDE_CHECK(!finger_sample(NONE, NONE, 0.0f, 0.0f));
    TIDE_CHECK(widget_frame(widget) == 0);
    // A finger touches the widget, with nothing before it: the view's from the
    // frame it lands in, and never the game's or the other view's.
    TIDE_CHECK(!finger_sample(TIDE_TOUCH_BEGAN, NONE, 300.0f, 300.0f));
    TIDE_CHECK(widget_frame(widget) == 1);
    TIDE_CHECK(!finger_sample(NONE, NONE, 0.0f, 0.0f));
    TIDE_CHECK(widget_frame(widget) == 1);
    // Off the widget, it's everyone's: a view that drags claims the pointer wherever it goes.
    TIDE_CHECK(finger_sample(TIDE_TOUCH_MOVED, NONE, 900.0f, 900.0f));
    TIDE_CHECK(widget_frame(widget) == 3);
    finger_sample(TIDE_TOUCH_ENDED, NONE, 900.0f, 900.0f);
    widget_frame(widget);

    // The GUI comes first: on a button of the GUI's over the widget, the press is the GUI's.
    TIDE_CHECK(!finger_sample(TIDE_TOUCH_BEGAN, NONE, 20.0f, 14.0f));
    TIDE_CHECK(widget_frame(widget) == 0);
    TIDE_CHECK(gui.active == 10);
    finger_sample(TIDE_TOUCH_ENDED, NONE, 20.0f, 14.0f);
    widget_frame(widget);

    // A click the mouse made as it came onto the widget, too.
    TIDE_CHECK(!mouse_sample(900.0f, 900.0f, false));
    TIDE_CHECK(widget_frame(widget) == 0);
    TIDE_CHECK(!mouse_sample(300.0f, 300.0f, true));
    TIDE_CHECK(widget_frame(widget) == 1);
    // A widget that's gone leaves nothing claimed, a frame later.
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
        tide_gui_claim_pointer_at(&gui, widget); // One more than fit: the pointer is on it
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
    // A button whose place is remembered takes a click made as the mouse came.
    TIDE_CHECK(!mouse_sample(40.0f, 10.0f, true));
    many_buttons();
    mouse_sample(40.0f, 10.0f, false);
    many_buttons();
    // One past those takes it once the GUI saw the mouse on it, as every widget did before.
    TIDE_CHECK(!mouse_sample(x, y, false));
    many_buttons();
    TIDE_CHECK(gui.over && gui.active == 0);
    TIDE_CHECK(!mouse_sample(x, y, true));
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
    mouse(170.0f, 14.0f, false); // Past the 130 label column
    int_field(&players);
    mouse(170.0f, 14.0f, true);
    int_field(&players);
    TIDE_CHECK(gui.editing == 5);
    mouse(170.0f, 14.0f, false);
    type("1x2"); // The x isn't part of a number
    TIDE_CHECK(!int_field(&players));
    TIDE_CHECK(players == 4); // Not until it's kept
    key(&devices.keyboard.enter, true);
    TIDE_CHECK(int_field(&players));
    TIDE_CHECK(players == 12);
    TIDE_CHECK(gui.editing == 0);
    key(&devices.keyboard.enter, false);

    // Escape goes back to the old value.
    key(&devices.keyboard.enter, true); // It still has the focus: Enter types again
    int_field(&players);
    key(&devices.keyboard.enter, false);
    type("99");
    int_field(&players);
    key(&devices.keyboard.escape, true);
    TIDE_CHECK(!int_field(&players));
    TIDE_CHECK(players == 12);
    key(&devices.keyboard.escape, false);

    // With the focus, the arrows step it.
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
    // The panel, then the button, centered on the screen both frames.
    TIDE_REQUIRE(draw.count >= 3);
    TIDE_CHECK(draw.commands[1].a.x == 960.0f && draw.commands[1].a.y == 540.0f);
    TIDE_CHECK(draw.commands[2].a.x == 960.0f && draw.commands[2].a.y == 540.0f);
    TIDE_CHECK(draw.commands[1].b.y == 28.0f + 2.0f * 12.0f);
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
    // On a screen 400 wide, the row wants 402 and its area has 352: the text
    // field and the button shrink, the label's column first.
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
    rect_edges(0, &left, &right); // The panel, 12 from both edges
    TIDE_CHECK(left > 11.99f && left < 12.01f);
    TIDE_CHECK(right > 387.99f && right < 388.01f);
    rect_edges(1, &left, &right); // Start Host, as wide as the area's room
    TIDE_CHECK(left > 23.99f && right < 376.01f && right > 375.99f);
    rect_edges(2, &left, &right); // The text field's box: the column shrank, not the box
    TIDE_CHECK(right - left > 179.99f && right - left < 180.01f);
    TIDE_CHECK(left > 24.0f + 19.2f + 10.0f);
    rect_edges(3, &left, &right); // Connect, shrunk a little
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
    // A and B on one row, C under them.
    TIDE_REQUIRE(draw.count >= 7);
    const tide_draw_command *a = &draw.commands[1];
    const tide_draw_command *b = &draw.commands[3];
    const tide_draw_command *c = &draw.commands[5];
    TIDE_CHECK(a->a.y == b->a.y && b->a.x > a->a.x);
    TIDE_CHECK(c->a.y == a->a.y + 28.0f + 5.0f);
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
    // A click on Quit does nothing, but the mouse on it is still the GUI's.
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

    // Drawn faded, and laid out as usual: Back is under Quit.
    TIDE_REQUIRE(draw.count >= 7);
    const tide_draw_command *play = &draw.commands[1];
    const tide_draw_command *quit = &draw.commands[3];
    const tide_draw_command *back = &draw.commands[5];
    TIDE_CHECK(quit->color.a == play->color.a * 0.5f && back->color.a == play->color.a);
    TIDE_CHECK(draw.commands[4].kind == TIDE_DRAW_TEXT && draw.commands[4].color.a == 0.5f);
    TIDE_CHECK(back->a.y == quit->a.y + 28.0f + 5.0f);

    // Tab goes past it.
    key(&devices.keyboard.tab, true);
    quit_disabled(true);
    TIDE_CHECK(gui.focus == 10);
    key(&devices.keyboard.tab, false);
    quit_disabled(true);
    key(&devices.keyboard.tab, true);
    quit_disabled(true);
    TIDE_CHECK(gui.focus == 30);
    key(&devices.keyboard.tab, false);

    // Enabled, it works again.
    mouse(20.0f, 47.0f, true);
    TIDE_CHECK(quit_disabled(false) == 0);
    TIDE_CHECK(gui.active == 20);
    mouse(20.0f, 47.0f, false);
    TIDE_CHECK(quit_disabled(false) == 2);

    // Disabled while it's pressed, it lets go, and the release doesn't press it.
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

    // Disabled while typing: what was typed is dropped, and so is the focus.
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
    TIDE_CHECK(gui.groups[gui.depth].disabled); // Still disabled inside another
    tide_gui_begin_horizontal(&gui, 1);
    TIDE_CHECK(gui.groups[gui.depth].disabled); // And so are containers in it
    tide_gui_close(&gui, outer); // A return skipped the inner closes
    TIDE_CHECK(gui.depth == 0 && !gui.groups[0].disabled);
    end();

    // In a row, a Disabled block's widgets go on in the row.
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
    tide_gui_begin_horizontal(&gui, 2); // A return skipped its close
    tide_gui_close(&gui, outer);
    TIDE_CHECK(gui.depth == 0);
    tide_gui_begin_vertical(&gui, 3);
    end(); // Closes the rest
    TIDE_CHECK(gui.depth == 0);
}

TIDE_TEST(gui_views_read_what_is_typed)
{
    start();
    int32_t players = 4;
    mouse(900.0f, 900.0f, false); // Away from the field
    devices.keyboard.text.count = 3;
    devices.keyboard.text.chars[0] = 'h';
    devices.keyboard.text.chars[1] = 0xE9; // e with an acute accent: two bytes in UTF-8
    devices.keyboard.text.chars[2] = 0xD800; // Half of a UTF-16 pair: no character
    begin();
    tide_gui_layout_int_field(&gui, 5, "Players", &players);
    const tide_str typed = tide_str_typed(&gui.devices.keyboard.text);
    TIDE_CHECK(typed.bytes == 3 && typed.chars == 2 && memcmp(typed.ptr, "h\xC3\xA9", 3) == 0);
    // It's a frame's: the game's sample never has it.
    tide_devices sampled = devices;
    tide_gui_hide(&gui, &sampled);
    TIDE_CHECK(sampled.keyboard.text.count == 0);
    end();
    begin();
    tide_gui_layout_int_field(&gui, 5, "Players", &players);
    TIDE_CHECK(tide_str_typed(&gui.devices.keyboard.text).bytes == 0); // Typed once
    end();

    // While the player types into a field of the GUI's, it's the GUI's.
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
    tide_pointer_poll(&devices, true); // As the platform polls it, before a frame's samples
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
    // The pointer comes onto the view's widget, which claims it before any press.
    mouse(900.0f, 900.0f, false);
    TIDE_CHECK(two_views(TIDE_GUI_POINTER) == 0);
    use_everything();
    TIDE_CHECK(sample() == 8); // The click isn't the game's; the keyboard still is
    // The view that claimed it reads the press, and the other doesn't.
    TIDE_CHECK(two_views(TIDE_GUI_POINTER) == ((POINTER | KEYBOARD) | KEYBOARD << 8));
    TIDE_CHECK(sample() == 8);
    // The mouse's position is everyone's, as it is over the GUI.
    tide_devices sampled = devices;
    tide_gui_hide(&gui, &sampled);
    TIDE_CHECK(sampled.mouse.position.x == 900.0f && sampled.pointer.position.x == 900.0f);

    // A claim lasts a frame: the frame after the view stops is the last without the pointer.
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
    TIDE_CHECK(sampled.gamepad.buttonSouth.pressed); // The gamepad isn't the keyboard's
    TIDE_CHECK(sample() == POINTER);                 // Nor is the pointer
    // The view that claimed it reads the key and what's typed, and the other doesn't.
    TIDE_CHECK(two_views(TIDE_GUI_KEYBOARD) == ((POINTER | KEYBOARD) | POINTER << 8));

    // Both at once.
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
    // On a button of the GUI's, the pointer is the GUI's, whatever a view claimed.
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
    TIDE_CHECK(two_buttons() == 0); // Lays them out, for the order
    // A view has the keyboard: Tab and the arrows are its own, and don't start moving the focus.
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
    // Once it lets go, Tab moves the focus again, a frame later: the claim stood for one more.
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
    TIDE_CHECK(tide_gui_typing(&gui)); // While a view asks, each frame
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
            // The views that fit keep reading it; the one past them doesn't
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
    TIDE_CHECK(tide_gui_cursor_lock(&gui)); // While a view asks, each frame
    begin();
    end();
    TIDE_CHECK(!tide_gui_cursor_lock(&gui));

    // A modal's widgets need the pointer: while one is up, the lock isn't
    // asked for, whatever views ask.
    bool open = true;
    for (int frame = 0; frame < 2; frame++) {
        begin();
        tide_gui_lock_cursor(&gui);
        const int depth = tide_gui_begin_modal(&gui, 30, TIDE_ANCHOR_MIDDLE_CENTER, &open);
        if (depth >= 0) tide_gui_close(&gui, depth);
        end();
        TIDE_CHECK(tide_gui_cursor_lock(&gui) == !open);
        open = false; // Closed, the views' ask counts again
    }
}

// ---------------------------------------------------------------------------
// Controls: widgets a view draws itself, whose input the GUI handles

#define NONE ((tide_touch_phase)TIDE_TOUCHES)

static const tide_rect CARD = {600.0f, 600.0f, 200.0f, 100.0f}; // Away from the buttons, which are laid out at the top left

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
    // The GUI draws nothing for it: the screen, and two buttons with their text
    TIDE_CHECK(draw.count == 5);
    mouse(650.0f, 650.0f, false);
    TIDE_CHECK(card_frame() == HOVERED);
    TIDE_CHECK(draw.count == 5);
    mouse(650.0f, 650.0f, true);
    TIDE_CHECK(card_frame() == (HOVERED | PRESSED));
    TIDE_CHECK(gui.active == 50);
    // Held wherever the pointer goes meanwhile, as a handle that's dragged is
    mouse(900.0f, 900.0f, true);
    TIDE_CHECK(card_frame() == PRESSED);
    mouse(650.0f, 650.0f, true);
    TIDE_CHECK(card_frame() == (HOVERED | PRESSED));
    mouse(650.0f, 650.0f, false);
    TIDE_CHECK(card_frame() == (HOVERED | CLICKED)); // Let go on it: a click, once
    TIDE_CHECK(card_frame() == HOVERED);

    // A press that leaves it before it lets go is no click, nor is one that comes onto it held.
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
    TIDE_CHECK(gui.focus == 0); // A click doesn't give it the focus, as it doesn't a button
}

TIDE_TEST(gui_a_press_on_a_control_is_never_the_games)
{
    start();
    TIDE_CHECK(!finger_sample(NONE, NONE, 0.0f, 0.0f));
    TIDE_CHECK(sampled_card_frame() == 0);
    // A finger touches the control. The game samples before the GUI sees the
    // touch, and still doesn't get it; nor do the views.
    TIDE_CHECK(!finger_sample(TIDE_TOUCH_BEGAN, NONE, 650.0f, 650.0f));
    TIDE_CHECK(sampled_card_frame() == (HOVERED | PRESSED));
    TIDE_CHECK(!finger_sample(NONE, NONE, 0.0f, 0.0f));
    TIDE_CHECK(sampled_card_frame() == (HOVERED | PRESSED));
    TIDE_CHECK(!finger_sample(TIDE_TOUCH_ENDED, NONE, 650.0f, 650.0f));
    TIDE_CHECK(sampled_card_frame() == (HOVERED | CLICKED)); // Lifted on it: a click
    TIDE_CHECK(!finger_sample(NONE, NONE, 0.0f, 0.0f));
    TIDE_CHECK(sampled_card_frame() == 0); // Nothing stays hovered where the finger lifted

    // A tap between two frames clicks too.
    TIDE_CHECK(!finger_sample(TIDE_TOUCH_BEGAN, TIDE_TOUCH_ENDED, 650.0f, 650.0f));
    TIDE_CHECK(sampled_card_frame() == (HOVERED | PRESSED));
    TIDE_CHECK(!finger_sample(NONE, NONE, 0.0f, 0.0f));
    TIDE_CHECK(sampled_card_frame() == (HOVERED | CLICKED));
    TIDE_CHECK(!finger_sample(NONE, NONE, 0.0f, 0.0f));
    TIDE_CHECK(sampled_card_frame() == 0);

    // A click the mouse made as it came onto the control, between two frames.
    TIDE_CHECK(!mouse_sample(900.0f, 900.0f, false));
    TIDE_CHECK(sampled_card_frame() == 0);
    TIDE_CHECK(!mouse_sample(650.0f, 650.0f, true));
    TIDE_CHECK(sampled_card_frame() == (HOVERED | PRESSED));
    TIDE_CHECK(!mouse_sample(650.0f, 650.0f, false));
    TIDE_CHECK(sampled_card_frame() == (HOVERED | CLICKED));

    // Away from it, a press is the game's and the views'.
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
    card_frame(); // Lays them out, for the order: Play, the control, Quit
    TIDE_CHECK(card_press(&devices.keyboard.downArrow) == 0);
    TIDE_CHECK(gui.focus == 10);
    TIDE_CHECK(card_press(&devices.keyboard.downArrow) == FOCUSED);
    TIDE_CHECK(card_frame() == FOCUSED);
    TIDE_CHECK(draw.count == 5); // The GUI draws no outline for it: showing the focus is the view's

    // Enter and Space click it: once, as they go down.
    key(&devices.keyboard.enter, true);
    TIDE_CHECK(card_frame() == (FOCUSED | CLICKED));
    TIDE_CHECK(card_frame() == FOCUSED);
    key(&devices.keyboard.enter, false);
    card_frame();
    TIDE_CHECK(card_press(&devices.keyboard.space) == (FOCUSED | CLICKED));
    TIDE_CHECK(card_press(&devices.keyboard.numpadEnter) == (FOCUSED | CLICKED));

    // Right goes on and left goes back, from a control and from a button.
    TIDE_CHECK(card_press(&devices.keyboard.rightArrow) == 0);
    TIDE_CHECK(gui.focus == 20);
    TIDE_CHECK(card_press(&devices.keyboard.leftArrow) == FOCUSED);
    TIDE_CHECK(card_press(&devices.keyboard.upArrow) == 0);
    TIDE_CHECK(gui.focus == 10);
    // Tab, and Shift with it
    TIDE_CHECK(card_press(&devices.keyboard.tab) == FOCUSED);
    TIDE_CHECK(card_press(&devices.keyboard.tab) == 0);
    TIDE_CHECK(gui.focus == 20);
    key(&devices.keyboard.leftShift, true);
    TIDE_CHECK(card_press(&devices.keyboard.tab) == FOCUSED);
    key(&devices.keyboard.leftShift, false);

    // While it has the focus, the keyboard isn't the game's.
    tide_devices sampled = devices;
    key(&sampled.keyboard.w, true);
    tide_gui_hide(&gui, &sampled);
    TIDE_CHECK(!sampled.keyboard.w.held && !sampled.keyboard.w.pressed);
    // Escape lets go.
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
    // The left stick, to the right: once, however long it's held there
    devices.gamepad.leftStick = tide_f2(1.0f, 0.0f);
    TIDE_CHECK(card_frame() == FOCUSED);
    TIDE_CHECK(card_frame() == FOCUSED);
    devices.gamepad.leftStick = tide_f2(0.0f, 0.0f);
    card_frame();
    TIDE_CHECK(card_press(&devices.gamepad.buttonSouth) == (FOCUSED | CLICKED));
    TIDE_CHECK(card_press(&devices.gamepad.dpad.right) == 0);
    TIDE_CHECK(gui.focus == 20);
    TIDE_CHECK(card_press(&devices.gamepad.dpad.left) == FOCUSED);
    TIDE_CHECK(card_press(&devices.gamepad.dpad.up) == 0);
    TIDE_CHECK(gui.focus == 10);
    devices.gamepad.leftStick = tide_f2(0.0f, -1.0f); // Down
    TIDE_CHECK(card_frame() == FOCUSED);
    devices.gamepad.leftStick = tide_f2(0.0f, 0.0f);
    card_frame();

    // While it has the focus, the gamepad isn't the game's.
    tide_devices sampled = devices;
    key(&sampled.gamepad.buttonNorth, true);
    tide_gui_hide(&gui, &sampled);
    TIDE_CHECK(!sampled.gamepad.buttonNorth.held && !sampled.gamepad.buttonNorth.pressed);
    // East lets go.
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
    key(&devices.keyboard.rightArrow, true); // From nothing: the first widget
    steppers(&volume, &players);
    key(&devices.keyboard.rightArrow, false);
    steppers(&volume, &players);
    TIDE_CHECK(gui.focus == 10 && volume == 0.5f);
    key(&devices.keyboard.rightArrow, true); // On the slider, it steps the value, and the focus stays
    steppers(&volume, &players);
    key(&devices.keyboard.rightArrow, false);
    steppers(&volume, &players);
    TIDE_CHECK(gui.focus == 10 && volume > 0.5f);
    key(&devices.keyboard.downArrow, true);
    steppers(&volume, &players);
    key(&devices.keyboard.downArrow, false);
    steppers(&volume, &players);
    TIDE_CHECK(gui.focus == 20);
    key(&devices.keyboard.leftArrow, true); // And the field's
    steppers(&volume, &players);
    key(&devices.keyboard.leftArrow, false);
    steppers(&volume, &players);
    TIDE_CHECK(gui.focus == 20 && players == 3);
    key(&devices.keyboard.downArrow, true);
    steppers(&volume, &players);
    key(&devices.keyboard.downArrow, false);
    steppers(&volume, &players);
    TIDE_CHECK(gui.focus == 30);
    key(&devices.keyboard.leftArrow, true); // From the button, it goes back
    steppers(&volume, &players);
    key(&devices.keyboard.leftArrow, false);
    TIDE_CHECK(gui.focus == 20 && players == 3);
}

TIDE_TEST(gui_navigate_lets_the_arrows_start_while_the_game_reads_them)
{
    start();
    card_frame();
    // The game sampled the devices: the arrows and the d-pad are its own.
    devices.gamepad.connected = true;
    tide_devices sampled = devices;
    tide_gui_hide(&gui, &sampled);
    key(&devices.gamepad.dpad.down, true);
    card_frame();
    TIDE_CHECK(gui.focus == 0);
    key(&devices.gamepad.dpad.down, false);
    // A view says a menu is up, each frame it is: they start moving the focus.
    for (int frame = 0; frame < 3; frame++) {
        tide_gui_hide(&gui, &sampled);
        key(&devices.gamepad.dpad.down, frame == 1);
        begin();
        tide_gui_navigate(&gui);
        card_widgets();
        TIDE_CHECK(gui.focus == (frame == 0 ? 0u : 10u));
    }
    // Once a frame goes by without it, they don't: the focus that's there still moves.
    tide_gui_hide(&gui, &sampled);
    TIDE_CHECK(card_press(&devices.gamepad.dpad.down) == FOCUSED);
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
static tide_devices menu_sampled; // What the last sample had
static tide_devices menu_viewed;  // ...and what views read in the last frame

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

// A key that clicks a widget is the GUI's until it's let go, though the widget
// is gone the frame after: Space on a lobby's Play fired the game's cannon
// too, from the frame nothing had the focus any more.
TIDE_TEST(gui_a_key_that_clicked_a_widget_is_the_guis_until_its_let_go)
{
    menu_start();
    TIDE_REQUIRE(gui.focus == 10);
    key(&devices.keyboard.space, true);
    menu_sample();
    TIDE_CHECK(!any_of(menu_sampled.keyboard.space));
    TIDE_CHECK(menu_frame()); // Clicked: the menu goes, with the key still down
    TIDE_CHECK(!any_of(menu_viewed.keyboard.space));
    for (int i = 0; i < 5; i++) {
        menu_sample();
        TIDE_CHECK(!any_of(menu_sampled.keyboard.space));
        TIDE_CHECK(!menu_frame() && !any_of(menu_viewed.keyboard.space));
    }
    TIDE_CHECK(gui.focus == 0); // Nothing has the focus: it's the key that's kept

    // Another key is the game's meanwhile
    key(&devices.keyboard.w, true);
    menu_sample();
    TIDE_CHECK(menu_sampled.keyboard.w.pressed && menu_sampled.keyboard.w.down);
    menu_frame();
    TIDE_CHECK(menu_viewed.keyboard.w.down && !any_of(menu_viewed.keyboard.space));

    // Let go: neither the game nor the views hear of it
    key(&devices.keyboard.space, false);
    menu_sample();
    TIDE_CHECK(!any_of(menu_sampled.keyboard.space));
    menu_frame();
    TIDE_CHECK(!any_of(menu_viewed.keyboard.space));
    menu_sample();
    menu_frame();
    TIDE_CHECK(!any_of(menu_sampled.keyboard.space) && !any_of(menu_viewed.keyboard.space));

    // Pressed again, it's the game's
    key(&devices.keyboard.space, true);
    menu_sample();
    TIDE_CHECK(menu_sampled.keyboard.space.pressed && menu_sampled.keyboard.space.down);
    menu_frame();
    TIDE_CHECK(menu_viewed.keyboard.space.down);

    // A gamepad's button the same
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

// A sample reads what happened since the last one, which may be frames ago: a
// kept key that was let go meanwhile is still the GUI's in the sample that
// reads its release, and one let go where nothing samples is kept no longer
// than the host's clearing of the devices.
TIDE_TEST(gui_a_kept_key_is_kept_until_a_sample_has_seen_it_let_go)
{
    menu_start();
    key(&devices.keyboard.space, true);
    menu_sample();
    TIDE_CHECK(menu_frame());
    menu_frame(); // Frames with no tick to sample for
    menu_frame();
    key(&devices.keyboard.space, false);
    menu_frame();
    TIDE_CHECK(!any_of(menu_viewed.keyboard.space));
    menu_frame();
    TIDE_CHECK(devices.keyboard.space.up && devices.keyboard.space.pressed); // Nothing consumed it yet
    menu_sample();
    TIDE_CHECK(!any_of(menu_sampled.keyboard.space));
    menu_frame();
    key(&devices.keyboard.space, true);
    menu_sample();
    TIDE_CHECK(menu_sampled.keyboard.space.pressed && menu_sampled.keyboard.space.down);
    key(&devices.keyboard.space, false);
    menu_sample();
    menu_frame();

    // Where nothing samples, as outside a match, the host clears the devices each frame
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
    // ...and the match's first sample, whenever it comes, has the key if it's down then
    key(&devices.keyboard.space, true);
    menu_sample();
    TIDE_CHECK(menu_sampled.keyboard.space.pressed && menu_sampled.keyboard.space.down);
}

// Only a key that went down while the keyboard was the GUI's is kept: one the
// game had before is the game's again once the GUI lets the keyboard go.
TIDE_TEST(gui_a_key_held_since_before_the_gui_had_the_keyboard_is_the_games_again)
{
    start();
    menu_open = true;
    menu_sample();
    menu_frame();
    key(&devices.keyboard.w, true); // Held, as the player walks
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
    TIDE_CHECK(!any_of(menu_sampled.keyboard.w)); // The GUI's while Play has the focus
    menu_frame();
    // Escape lets the focus go, and is the GUI's while it's down; W is the game's again
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
    TIDE_CHECK(focus_views() == (31 | 31 << 8)); // Nothing has the focus: every view reads everything
    use_keys(false);
    focus_views();
    key(&devices.keyboard.tab, true); // The control, which is called first
    focus_views();
    key(&devices.keyboard.tab, false);
    focus_views();
    TIDE_CHECK(gui.focus == 50);
    // Its view goes on reading the keyboard and the gamepad, but for what moves
    // the focus and clicks, which is the GUI's. The other view reads none of it,
    // nor does the game.
    use_keys(true);
    TIDE_CHECK(focus_views() == (1 | 4 | 8));
    tide_devices sampled = devices;
    tide_gui_hide(&gui, &sampled);
    TIDE_CHECK(!sampled.keyboard.w.pressed && !sampled.gamepad.buttonNorth.pressed);
    use_keys(false);
    focus_views();

    // On a widget of the GUI's own, no view reads them, as ever.
    key(&devices.keyboard.tab, true);
    focus_views();
    key(&devices.keyboard.tab, false);
    focus_views();
    TIDE_CHECK(gui.focus == 10);
    use_keys(true);
    key(&devices.keyboard.enter, false); // It would press Play
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
        if (frame == 2) { // A press on it still isn't the game's
            tide_devices sampled = devices;
            tide_gui_hide(&gui, &sampled);
            TIDE_CHECK(!sampled.mouse.left.pressed);
        }
    }
    TIDE_CHECK(result == 0 && gui.active == 0 && gui.focus == 0); // Nothing of it is ever true, and Tab goes past it

    // Disabled while it's held or has the focus, it lets go.
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
        if (frame > 0) out |= outside; // The modal is up from the frame after it first drew
        in |= within;
        if (frame == 2) TIDE_CHECK(gui.focus == 60); // The modal's first widget has the focus
    }
    TIDE_CHECK(out == 0);
    TIDE_CHECK(in == (HOVERED | PRESSED | FOCUSED | CLICKED)); // The one in it works as ever
}

// Three controls side by side from one call site, as a loop makes them, but
// for the one at `skip`: which said `what` (bits 0 to 2).
static int loop_frame(const int skip, const int what)
{
    int which = 0;
    begin();
    for (int i = 0; i < 3; i++) {
        if (i == skip) continue;
        const uint32_t id = tide_gui_id(&gui, 7, 3); // The same site and seed each time round
        const tide_control c = tide_gui_control(&gui, id, (tide_rect){600.0f + 110.0f * (float)i, 600.0f, 100.0f, 60.0f});
        if (said(c) & what) which |= 1 << i;
    }
    end();
    return which;
}

TIDE_TEST(gui_controls_made_in_a_loop_are_told_apart_by_their_turn)
{
    start();
    // The mouse on the second: it alone is hovered, held and clicked.
    mouse(760.0f, 630.0f, false);
    loop_frame(-1, 0);
    TIDE_CHECK(loop_frame(-1, HOVERED) == 2);
    mouse(760.0f, 630.0f, true);
    TIDE_CHECK(loop_frame(-1, PRESSED) == 2);
    mouse(760.0f, 630.0f, false);
    TIDE_CHECK(loop_frame(-1, CLICKED) == 2);
    // The focus goes through them in the loop's order.
    mouse(900.0f, 900.0f, false);
    loop_frame(-1, 0);
    for (int i = 0; i < 3; i++) {
        key(&devices.keyboard.rightArrow, true);
        TIDE_CHECK(loop_frame(-1, FOCUSED) == 1 << i);
        key(&devices.keyboard.rightArrow, false);
        TIDE_CHECK(loop_frame(-1, FOCUSED) == 1 << i);
    }
    // A loop that leaves its last one out leaves the others as they were...
    key(&devices.keyboard.leftArrow, true);
    TIDE_CHECK(loop_frame(2, FOCUSED) == 2);
    key(&devices.keyboard.leftArrow, false);
    TIDE_CHECK(loop_frame(2, FOCUSED) == 2);
    // ...but one that leaves an earlier one out gives its turn to the next:
    // the focus is where the second one made was, which is now the third.
    TIDE_CHECK(loop_frame(0, FOCUSED) == 4);
}

#undef NONE

// ---------------------------------------------------------------------------
// Themes: how the GUI's own widgets look

static bool same_color(const tide_color a, const tide_color b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
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
    // The screen; the panel; the button and its text; the toggle's box, its
    // check and its text; the slider's value, its rail, its filled part and
    // its thumb; the field and its number.
    TIDE_REQUIRE(draw.count == 13);
    TIDE_CHECK(draw.shape_count == 0); // Plain rects, all of them
    const tide_draw_command *c = draw.commands;
    TIDE_CHECK(c[1].kind == TIDE_DRAW_RECT && same_color(c[1].color, (tide_color){0.07f, 0.07f, 0.1f, 0.9f}));
    TIDE_CHECK(c[2].kind == TIDE_DRAW_RECT && same_color(c[2].color, control));
    TIDE_CHECK(c[3].kind == TIDE_DRAW_TEXT && same_color(c[3].color, text));
    TIDE_CHECK(c[4].kind == TIDE_DRAW_RECT && same_color(c[4].color, control));
    TIDE_CHECK(c[5].kind == TIDE_DRAW_RECT && same_color(c[5].color, accent));
    TIDE_CHECK(c[8].kind == TIDE_DRAW_RECT && same_color(c[8].color, control));
    TIDE_CHECK(c[9].kind == TIDE_DRAW_RECT && same_color(c[9].color, accent));
    TIDE_CHECK(c[10].kind == TIDE_DRAW_RECT && same_color(c[10].color, text));
    TIDE_CHECK(c[11].kind == TIDE_DRAW_RECT && same_color(c[11].color, (tide_color){0.12f, 0.12f, 0.15f, 1.0f}));

    // Under the mouse and held: the hovered colors, and the pressed one.
    mouse(420.0f, 420.0f, false); // On the button
    begin();
    every_part();
    end();
    begin();
    every_part();
    end();
    TIDE_CHECK(same_color(draw.commands[2].color, (tide_color){0.28f, 0.28f, 0.34f, 1.0f}));
    mouse(420.0f, 420.0f, true);
    begin();
    every_part();
    end();
    TIDE_CHECK(same_color(draw.commands[2].color, (tide_color){0.14f, 0.14f, 0.18f, 1.0f}));
    mouse(420.0f, 525.0f, false); // On the field
    begin();
    every_part();
    end();
    begin();
    every_part();
    end();
    TIDE_CHECK(same_color(draw.commands[11].color, (tide_color){0.16f, 0.16f, 0.2f, 1.0f}));
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
    return theme;
}

TIDE_TEST(gui_a_theme_colors_every_part)
{
    start();
    const tide_gui_theme theme = loud_theme();
    for (int frame = 0; frame < 4; frame++) {
        // Nowhere, then on the button, held there, then on the field
        mouse(frame == 0 ? 5.0f : 420.0f, frame == 0 ? 5.0f : frame == 3 ? 525.0f : 420.0f, frame == 2);
        for (int twice = 0; twice < 2; twice++) { // Hovering takes a frame
            begin();
            tide_gui_set_theme(&gui, theme);
            every_part();
            end();
        }
        TIDE_REQUIRE(draw.count == 13);
        const tide_draw_command *c = draw.commands;
        TIDE_CHECK(draw.shape_count == 0); // Its corners are square still
        TIDE_CHECK(same_color(c[1].color, theme.panel));
        TIDE_CHECK(same_color(c[2].color, frame == 1 ? theme.hot : frame == 2 ? theme.pressed : theme.control));
        TIDE_CHECK(same_color(c[3].color, theme.text) && same_color(c[6].color, theme.text) && same_color(c[7].color, theme.text));
        TIDE_CHECK(same_color(c[4].color, theme.control) && same_color(c[5].color, theme.accent));
        TIDE_CHECK(same_color(c[8].color, theme.control) && same_color(c[9].color, theme.accent));
        TIDE_CHECK(same_color(c[10].color, theme.text)); // The slider's thumb
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
    mouse(5.0f, 5.0f, false);
    for (int frame = 0; frame < 2; frame++) {
        key(&devices.keyboard.tab, frame == 1); // The button has the focus in the second
        begin();
        tide_gui_set_theme(&gui, theme);
        every_part();
        end();
    }
    TIDE_REQUIRE(draw.count == 14);
    const tide_draw_command *c = draw.commands;
    // The panel: sized when its area closed, and round
    TIDE_REQUIRE(shape_of(&c[1]) != NULL);
    TIDE_CHECK(c[1].a.x == 650.0f && c[1].a.y == 550.0f && c[1].b.x == 500.0f && c[1].b.y == 300.0f);
    TIDE_CHECK(shape_of(&c[1])->radius == 6.0f && same_color(c[1].color, theme.panel));
    // The focus's outline goes around the button's corners
    TIDE_REQUIRE(shape_of(&c[2]) && shape_of(&c[3]));
    TIDE_CHECK(same_color(c[2].color, theme.accent) && shape_of(&c[2])->radius == 8.0f);
    TIDE_CHECK(same_color(c[3].color, theme.control) && shape_of(&c[3])->radius == 6.0f);
    TIDE_CHECK(c[2].b.x == c[3].b.x + 4.0f && c[2].b.y == c[3].b.y + 4.0f);
    // A toggle's box, and the check inside it, which is rounder by less
    TIDE_REQUIRE(shape_of(&c[5]) && shape_of(&c[6]));
    TIDE_CHECK(shape_of(&c[5])->radius == 6.0f && shape_of(&c[6])->radius == 2.0f);
    // A slider's rail is 6 tall: no rounder than half of that
    TIDE_REQUIRE(shape_of(&c[9]) != NULL);
    TIDE_CHECK(c[9].b.y == 6.0f && shape_of(&c[9])->radius == 3.0f);
    TIDE_CHECK(shape_of(&c[12]) != NULL && shape_of(&c[12])->radius == 6.0f); // The field

    // An area sized by its widgets, in the frame it first comes up: its panel is there, and fits them.
    begin();
    tide_gui_set_theme(&gui, theme);
    const int area = tide_gui_begin_area_at(&gui, 77, TIDE_ANCHOR_MIDDLE_CENTER);
    tide_gui_layout_button(&gui, 8, "Play");
    tide_gui_close(&gui, area);
    end();
    TIDE_REQUIRE(draw.count == 4 && shape_of(&draw.commands[1]) != NULL);
    TIDE_CHECK(draw.commands[1].b.y == 28.0f + 24.0f && shape_of(&draw.commands[1])->radius == 6.0f);
    TIDE_CHECK(draw.commands[1].a.x == 960.0f && draw.commands[1].a.y == 540.0f);

    // Disabled, a round part fades as a square one does.
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
    tide_gui_layout_button(&gui, 1, "A"); // The usual look
    const int outer = tide_gui_begin_theme(&gui, red);
    tide_gui_layout_button(&gui, 2, "B");
    const int inner = tide_gui_begin_theme(&gui, green);
    tide_gui_layout_button(&gui, 3, "C");
    tide_gui_close(&gui, inner);
    tide_gui_layout_button(&gui, 4, "D"); // The one around it again
    tide_gui_close(&gui, outer);
    tide_gui_layout_button(&gui, 5, "E");
    end();
    TIDE_REQUIRE(draw.count == 11);
    TIDE_CHECK(drawn_with(1, usual) && drawn_with(3, red) && drawn_with(5, green) && drawn_with(7, red) && drawn_with(9, usual));
    // It lays out nothing: the buttons are one under the other, as without it
    TIDE_CHECK(draw.commands[9].a.y == draw.commands[1].a.y + 4.0f * (28.0f + 5.0f));
    TIDE_CHECK(draw.commands[5].a.x - draw.commands[5].b.x * 0.5f == 0.0f);

    // Over a look that was set: that's what it puts back, and a look set inside it ends with it.
    begin();
    tide_gui_set_theme(&gui, blue);
    const int block = tide_gui_begin_theme(&gui, red);
    tide_gui_set_theme(&gui, green);
    tide_gui_layout_button(&gui, 1, "A");
    tide_gui_close(&gui, block);
    tide_gui_layout_button(&gui, 2, "B");
    end();
    TIDE_CHECK(drawn_with(1, green) && drawn_with(3, blue));

    // A return out of blocks closes them all, and other blocks in between are as they were.
    begin();
    const int first = tide_gui_begin_theme(&gui, red);
    const int off = tide_gui_begin_disabled(&gui, true);
    tide_gui_begin_theme(&gui, green);
    tide_gui_begin_horizontal(&gui, 6);
    tide_gui_close(&gui, off); // The Disabled block, and what was open in it
    TIDE_CHECK(same_color(gui.theme.control, red.control) && !gui.groups[gui.depth].disabled);
    tide_gui_layout_button(&gui, 1, "A");
    tide_gui_close(&gui, first);
    TIDE_CHECK(same_color(gui.theme.control, usual.control) && gui.depth == 0);
    end();
    TIDE_CHECK(drawn_with(1, red) && draw.commands[1].color.a == 1.0f);

    // In a row, its widgets go on in the row.
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

    // Deeper than blocks go, one changes nothing, rather than a look it couldn't put back.
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
    tide_gui_view(&gui, 3); // The next view's widgets have it too
    tide_gui_layout_button(&gui, 3, "C");
    end();
    TIDE_CHECK(drawn_with(1, usual) && drawn_with(3, red) && drawn_with(5, red));
    // The next frame starts from the usual look.
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
static int entry_shown;            // The fields a frame calls, a bit each
static int entry_off;              // ...and the ones in a Disabled block
static tide_devices entry_sampled; // What the frame's sample had of the devices
static tide_devices entry_viewed;  // ...and what its views read

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
    bool entered[3] = {true, true, true}; // Whatever the variable held, each call says
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

TIDE_TEST(gui_a_field_says_when_its_typing_is_entered)
{
    entry_start();
    mouse(ENTRY_X, NAME_Y, false);
    TIDE_CHECK(entries() == 0);

    // The text field: a click starts the typing, and it changes as it's typed
    TIDE_CHECK(entry_click(ENTRY_X, NAME_Y) == 0);
    TIDE_REQUIRE(gui.editing == 5);
    type("K7");
    TIDE_CHECK(entries() == ENTRY_NAME * ENTRY_CHANGED);
    TIDE_CHECK(entries() == 0);
    // Enter ends the typing: entered, in that frame and no other
    key(&devices.keyboard.enter, true);
    TIDE_CHECK(entries() == ENTRY_NAME);
    TIDE_CHECK(gui.editing == 0 && gui.focus == 5 && name_is("K7"));
    TIDE_CHECK(entries() == 0); // The key is still down
    key(&devices.keyboard.enter, false);
    TIDE_CHECK(entries() == 0);
    // It still has the focus, so Enter starts the typing again: that's not entering
    TIDE_CHECK(entry_tap(&devices.keyboard.enter) == 0);
    TIDE_CHECK(gui.editing == 5);
    type("Q");
    TIDE_CHECK(entries() == ENTRY_NAME * ENTRY_CHANGED);
    // The numpad's Enter is Enter
    TIDE_CHECK(entry_tap(&devices.keyboard.numpadEnter) == ENTRY_NAME);
    TIDE_CHECK(gui.editing == 0 && name_is("K7Q"));

    // The int field keeps its number as it's entered: changed, in the same call
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
    // Enter starts its typing, and Enter again enters the number it had: nothing changed
    TIDE_CHECK(entry_tap(&devices.keyboard.enter) == 0);
    TIDE_CHECK(gui.editing == 6);
    TIDE_CHECK(entry_tap(&devices.keyboard.numpadEnter) == ENTRY_COUNT);
    TIDE_CHECK(entry_count == 12 && gui.editing == 0);

    // The float field the same
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

// What's typed and Enter can come in one frame, a slow one or a script's: the
// field is entered with all of it.
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

    // The fields that don't say are entered the same way
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

    // Escape stops the typing: text stays as it was typed, and a number as it was
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

    // Tab moves the focus on: a number is kept, and nothing is entered
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

    // So does a click elsewhere, and on the field after it
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
    // ...and on a field before it, which a frame calls before the number's
    type("2");
    entries();
    TIDE_CHECK(entry_click(ENTRY_X, NAME_Y) == ENTRY_RATIO * ENTRY_CHANGED);
    TIDE_CHECK(gui.editing == 5 && entry_ratio == 2.0f);
    entry_click(ENTRY_X, COUNT_Y);
    type("7");
    entries();
    TIDE_CHECK(entry_click(ENTRY_X, NAME_Y) == ENTRY_COUNT * ENTRY_CHANGED);
    TIDE_CHECK(gui.editing == 5 && entry_count == 7 && name_is("K7QF"));

    // A field disabled while it's typed into isn't entered, with Enter down in that frame or not
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
            // ...nor while it stays disabled
            TIDE_CHECK(entry_tap(&devices.keyboard.enter) == 0);
        }
    }
    TIDE_CHECK(entry_count == 7 && entry_ratio == 2.0f); // What was typed into a number then is dropped
}

// A gamepad's south button ends a number field's typing, as Enter does, so it
// enters it. It doesn't end a text field's, where nothing is entered.
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
    TIDE_CHECK(gui.editing == 5); // Still typed into
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
    // A modal comes up over it: it's up from the frame after it first drew
    up = true;
    TIDE_CHECK(modal_entries(&up) == 0);
    key(&devices.keyboard.enter, true);
    TIDE_CHECK(modal_entries(&up) == 0); // The field under it stops being typed into, and isn't entered
    TIDE_CHECK(gui.editing == 6 && name_is("K7")); // The modal's first widget has the focus, and Enter started its typing
    key(&devices.keyboard.enter, false);
    type("3");
    TIDE_CHECK(modal_entries(&up) == 0);
    // The field in the modal is entered as any
    key(&devices.keyboard.enter, true);
    TIDE_CHECK(modal_entries(&up) == (ENTRY_COUNT | ENTRY_COUNT * ENTRY_CHANGED));
    TIDE_CHECK(entry_count == 3 && up);
}

// Enter while a field is typed into is the GUI's, in the frame it enters the
// field too, and until it's let go, though the field is gone the frame after:
// a lobby's page that joins as its code is entered.
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
    entry_shown = 0; // Joining: the page goes, with the key still down
    for (int i = 0; i < 5; i++) {
        TIDE_CHECK(entries() == 0);
        TIDE_CHECK(!any_of(entry_sampled.keyboard.enter) && !any_of(entry_viewed.keyboard.enter));
    }
    TIDE_CHECK(gui.focus == 0 && gui.editing == 0);
    // Let go: neither the game nor the views hear of it
    key(&devices.keyboard.enter, false);
    for (int i = 0; i < 2; i++) {
        entries();
        TIDE_CHECK(!any_of(entry_sampled.keyboard.enter) && !any_of(entry_viewed.keyboard.enter));
    }
    // Pressed again, it's the game's
    key(&devices.keyboard.enter, true);
    entries();
    TIDE_CHECK(entry_sampled.keyboard.enter.pressed && entry_sampled.keyboard.enter.down && entry_viewed.keyboard.enter.down);

    // The numpad's, and a number entered with a gamepad's south button, the same
    entry_start();
    devices.gamepad.connected = true;
    entry_shown = ENTRY_COUNT;
    mouse(ENTRY_X, NAME_Y, false);
    entries();
    entry_click(ENTRY_X, NAME_Y); // The one row
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

// GUI's fields, at a rect, say it as GUILayout's do.
TIDE_TEST(gui_fields_at_a_rect_say_when_they_are_entered)
{
    entry_start();
    const tide_rect rect = {600.0f, 400.0f, 300.0f, 30.0f};
    const tide_textref name = {.local = &entry_name};
    int said = 0;
    for (int field = 0; field < 3; field++) {
        // Six frames: the pointer comes, presses and lets go, the player types, Enter goes down, and up
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
        mouse(100.0f, 100.0f, true); // Away, for the next one
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
static int form_order[FORM_FIELDS]; // The fields a frame calls, first to last
static int form_called;             // ...and how many of them
static int form_off;                // The ones in a Disabled block, a bit each
static int form_entered;            // The ones a frame said were entered, a bit each

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

// The player types a number and clicks a text field: one the frame calls
// before the number's field, and one it calls after.
TIDE_TEST(gui_a_number_is_kept_when_the_player_clicks_another_field)
{
    for (int number = FORM_COUNT; number <= FORM_RATIO; number++) { // An int field, and a float field
        for (int earlier = 0; earlier < 2; earlier++) {
            if (earlier) FORM(FORM_NAME, number);
            else FORM(number, FORM_NAME);
            mouse(FORM_X, FORM_Y(number), false);
            form();
            TIDE_CHECK(form_click(FORM_X, FORM_Y(number)) == 0);
            TIDE_REQUIRE(gui.editing != 0);
            type("7");
            TIDE_CHECK(form() == 0 && form_count == 4 && form_ratio == 0.5f); // Not until it's kept
            // The number is kept in the frame of the press, and its field says it changed
            mouse(FORM_X, FORM_Y(FORM_NAME), true);
            TIDE_CHECK(form() == 1 << number);
            TIDE_CHECK(number == FORM_COUNT ? form_count == 7 : form_ratio == 7.0f);
            TIDE_CHECK(gui.editing == 5 && gui.focus == 5);
            mouse(FORM_X, FORM_Y(FORM_NAME), false);
            TIDE_CHECK(form() == 0);
            TIDE_CHECK(form_entered == 0); // Nothing was entered: the focus left by a click
            // What's typed from here is the text field's
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
    // From the one the frame calls second to the one it calls first
    form_click(FORM_X, FORM_Y(FORM_RATIO));
    type("2.5");
    form();
    TIDE_CHECK(form_click(FORM_X, FORM_Y(FORM_COUNT)) == 1 << FORM_RATIO);
    TIDE_CHECK(form_ratio == 2.5f && form_count == 4 && gui.editing == 6);
    // The field the player went to is typed into as any: its value is selected, so typing replaces it
    type("31");
    TIDE_CHECK(form() == 0 && form_count == 4);
    // ...and back, from the first to the second
    TIDE_CHECK(form_click(FORM_X, FORM_Y(FORM_RATIO)) == 1 << FORM_COUNT);
    TIDE_CHECK(form_count == 31 && form_ratio == 2.5f && gui.editing == RATIO_ID);
    // What isn't a number changes nothing, either way
    type("-");
    form();
    TIDE_CHECK(form_click(FORM_X, FORM_Y(FORM_COUNT)) == 0);
    TIDE_CHECK(form_ratio == 2.5f && gui.editing == 6);
    type("+");
    form();
    TIDE_CHECK(form_click(FORM_X, FORM_Y(FORM_RATIO)) == 0);
    TIDE_CHECK(form_count == 31 && gui.editing == RATIO_ID);
    // Nor does the number the field had
    type("2.5");
    form();
    TIDE_CHECK(form_click(FORM_X, FORM_Y(FORM_COUNT)) == 0);
    TIDE_CHECK(form_ratio == 2.5f && form_count == 31);
    TIDE_CHECK(form_entered == 0);
}

// Text is its field's as it's typed, so it never had the number's loss: from a
// text field to a number field and back, whichever the frame calls first.
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
        // To the number field: the text is as it was typed
        TIDE_CHECK(form_click(FORM_X, FORM_Y(FORM_COUNT)) == 0);
        TIDE_CHECK(form_name_is("K7") && gui.editing == 6);
        type("9");
        TIDE_CHECK(form() == 0 && form_count == 4 && form_name_is("K7"));
        // And back: the number is kept, and the text goes on from where it was
        TIDE_CHECK(form_click(FORM_X, FORM_Y(FORM_NAME)) == 1 << FORM_COUNT);
        TIDE_CHECK(form_count == 9 && form_name_is("K7") && gui.editing == 5);
        type("Q");
        TIDE_CHECK(form() == 1 << FORM_NAME && form_name_is("K7Q"));
        // Once more to the number field, which has what it kept
        TIDE_CHECK(form_click(FORM_X, FORM_Y(FORM_COUNT)) == 0);
        TIDE_CHECK(form_count == 9 && form_name_is("K7Q"));
        TIDE_CHECK(form_entered == 0);
    }
}

// A vector's and a color's boxes are number fields side by side, each called
// after the one on its left.
TIDE_TEST(gui_each_box_of_a_vector_or_a_color_field_keeps_its_number)
{
    FORM(FORM_SIZE, FORM_TINT);
    mouse(SIZE_X(2), FORM_Y(FORM_SIZE), false);
    form();
    // A float3's last box, then its first
    form_click(SIZE_X(2), FORM_Y(FORM_SIZE));
    TIDE_REQUIRE(gui.editing == SIZE_ID(2));
    type("5");
    form();
    TIDE_CHECK(form_click(SIZE_X(0), FORM_Y(FORM_SIZE)) == 1 << FORM_SIZE);
    TIDE_CHECK(form_size.x == 1.0f && form_size.y == 2.0f && form_size.z == 5.0f && gui.editing == SIZE_ID(0));
    // ...and on to its middle one
    type("8");
    form();
    TIDE_CHECK(form_click(SIZE_X(1), FORM_Y(FORM_SIZE)) == 1 << FORM_SIZE);
    TIDE_CHECK(form_size.x == 8.0f && form_size.y == 2.0f && form_size.z == 5.0f && gui.editing == SIZE_ID(1));

    // From a color's box to the vector's, which the frame calls before it
    form_click(TINT_X(3), FORM_Y(FORM_TINT));
    TIDE_REQUIRE(gui.editing == TINT_ID(3));
    type("0.5");
    form();
    TIDE_CHECK(form_click(SIZE_X(1), FORM_Y(FORM_SIZE)) == 1 << FORM_TINT);
    TIDE_CHECK(form_tint.a == 0.5f && form_size.y == 2.0f);
    // A color's box to the one before it: what's kept is from 0 to 1, as when it's entered
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
    // Escape, then a click on the field the frame calls before it
    form_click(FORM_X, FORM_Y(FORM_COUNT));
    type("99");
    form();
    TIDE_CHECK(form_tap(&devices.keyboard.escape) == 0);
    TIDE_CHECK(gui.editing == 0 && form_count == 4);
    TIDE_CHECK(form_click(FORM_X, FORM_Y(FORM_NAME)) == 0);
    TIDE_CHECK(form_count == 4 && gui.editing == 5);
    // Escape in the frame of that click
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
    // ...and before a click on the field after it
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
    // On, to the field the frame calls after it
    form_click(FORM_X, FORM_Y(FORM_COUNT));
    type("7");
    form();
    TIDE_CHECK(form_tap(&devices.keyboard.tab) == 1 << FORM_COUNT);
    TIDE_CHECK(form_count == 7 && gui.focus == RATIO_ID && gui.editing == 0);
    // Back, with Shift, to the one it calls before
    form_click(FORM_X, FORM_Y(FORM_COUNT));
    type("8");
    form();
    key(&devices.keyboard.leftShift, true);
    TIDE_CHECK(form_tap(&devices.keyboard.tab) == 1 << FORM_COUNT);
    TIDE_CHECK(form_count == 8 && gui.focus == 5 && gui.editing == 0);
    key(&devices.keyboard.leftShift, false);
    form();

    // A slow frame has the keys and what was typed after them at once: what's
    // typed is the field's the focus went to, whose typing it starts
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
    // The same from a number field back to a number field
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

// A field that isn't called any more has nowhere to keep a number: its typing
// ends, and nothing of it is left for a later frame.
TIDE_TEST(gui_a_number_field_that_goes_while_its_typed_into_keeps_nothing)
{
    FORM(FORM_NAME, FORM_COUNT);
    mouse(FORM_X, FORM_Y(FORM_COUNT), false);
    form();
    form_click(FORM_X, FORM_Y(FORM_COUNT));
    type("7");
    form();
    TIDE_REQUIRE(gui.editing == 6);
    form_called = 1; // The number's field goes
    TIDE_CHECK(form() == 0 && gui.editing == 0 && gui.focus == 0);
    form_called = 2; // Back, it has the number it had, and isn't typed into
    TIDE_CHECK(form() == 0 && form_count == 4 && gui.editing == 0);

    // It goes in the frame the player clicks the field before it
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
    // The text field's typing goes on
    type("K7");
    TIDE_CHECK(form() == 1 << FORM_NAME && form_name_is("K7") && form_count == 4);
}

// A field disabled while it's typed into drops what was typed, as Escape does,
// whichever field the player clicked in that frame.
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

// A modal that comes up over a number field that's typed into takes the focus
// from it: the field keeps its number, wherever the frame calls the modal.
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
        up = true; // It's up from the frame after it first drew
        TIDE_CHECK(!count_under_modal(modal_first != 0, &up) && form_count == 4);
        TIDE_CHECK(count_under_modal(modal_first != 0, &up));
        TIDE_CHECK(form_count == 7 && gui.editing == 0 && gui.focus == 31);
        TIDE_CHECK(!count_under_modal(modal_first != 0, &up) && form_count == 7);
    }
}
