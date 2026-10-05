// The stats' drawer (tide/stats.h): what it shows closed and open, what opens
// and closes it, and whose a press on it is. Its numbers are made up, and so
// are the devices: each frame here goes in a host's order, the poll, the
// drawer's input, the game's sample, the GUI and the views, then the drawer.

#include <string.h>

#include "tide/gui.h"
#include "tide/stats.h"
#include "tide_test.h"

#define MS UINT64_C(1000000) // Nanoseconds

static tide_devices devices;
static tide_stats stats;
static tide_gui gui;
static tide_draw_list list;
static tide_profile profile;
static tide_stats_values values;
static tide_float2 screen;
static tide_devices sampled; // What the last frame's sample of the game's input read

// Every character 8 pixels wide.
static tide_float2 font(const char *text, const float size, const tide_target_size *target, const unsigned char *weight)
{
    (void)target;
    (void)weight;
    return tide_f2((float)strlen(text) * 8.0f, size);
}

// A match this machine runs, at 60 frames a second, with four systems.
static void start(const bool open)
{
    memset(&devices, 0, sizeof devices);
    memset(&gui, 0, sizeof gui);
    stats = (tide_stats){.open = open};
    screen = tide_f2(960.0f, 540.0f);
    tide_profile_free(&profile);
    tide_profile_count(&profile.systems, 4);
    tide_profile_add(&profile.systems, 0, "Fall", 0, 310000);
    tide_profile_add(&profile.systems, 1, "Combat.Hit", 0, 1500000);
    tide_profile_add(&profile.systems, 2, "Land", 0, 0);
    tide_profile_add(&profile.systems, 3, "Move", 0, 200000);
    values = (tide_stats_values){
        .fps = 59.6,
        .work_ms = 2.14,
        .threads = 12,
        .memory = 48u * 1024u * 1024u,
        .renderer = "OpenGL 3.3",
        .draw_calls = 7,
        .match = true,
        .ping_ms = 34,
        .up = 2048.0,
        .down = 512.0,
        .tick = 1234,
        .entities = 56,
        .profile = &profile,
    };
}

static void draw(void)
{
    tide_draw_reset(&list);
    tide_stats_draw(&stats, &values, &list, screen, font);
}

static bool line_is(const uint32_t i, const char *text)
{
    return i < stats.line_count && strcmp(stats.lines[i].text, text) == 0;
}

static const tide_draw_command *text_command(const char *text)
{
    for (uint32_t i = 0; i < list.count; i++) {
        const tide_draw_command *c = &list.commands[i];
        if (c->kind == TIDE_DRAW_TEXT && strcmp(list.text + c->text, text) == 0) return c;
    }
    return NULL;
}

// The drawer's panel: the first rect it drew.
static tide_rect panel(void)
{
    for (uint32_t i = 0; i < list.count; i++) {
        const tide_draw_command *c = &list.commands[i];
        if (c->kind == TIDE_DRAW_RECT) return (tide_rect){c->a.x - c->b.x * 0.5f, c->a.y - c->b.y * 0.5f, c->b.x, c->b.y};
    }
    return (tide_rect){0};
}

// ---------------------------------------------------------------------------
// What it shows

TIDE_TEST(stats_closed_are_one_line_in_the_corner)
{
    start(false);
    tide_draw_reset(&list);
    tide_draw_set_clip(&list, (tide_rect){0, 0, 10, 10}); // A list's own, left on, with a tint
    tide_draw_set_tint(&list, TIDE_COLOR_RED);
    tide_stats_draw(&stats, &values, &list, screen, font);
    TIDE_REQUIRE(stats.line_count == 1 && stats.shown);
    TIDE_CHECK(line_is(0, "60 fps")); // The frame rate, and nothing a host would have to measure
    TIDE_CHECK(!list.tinted && text_command("60 fps") && !text_command("cpu 2.1 ms"));

    // A panel around the line, 8 pixels from the screen's right and bottom edges
    const tide_rect box = panel();
    TIDE_CHECK(box.x + box.width == 952.0f && box.y + box.height == 532.0f && box.height == 32.0f);
    TIDE_CHECK(box.width == 48.0f + 16.0f + 16.0f); // The line, the arrow before it, and room beside them
    const tide_draw_command *line = text_command("60 fps");
    TIDE_CHECK(line->a.x + 48.0f == 944.0f && line->a.y == 508.0f && line->b.x == 16.0f);
    // The pointer is on it from the panel out to the screen's corner
    TIDE_CHECK(stats.rect.x == box.x && stats.rect.y == box.y);
    TIDE_CHECK(stats.rect.x + stats.rect.width == 960.0f && stats.rect.y + stats.rect.height == 540.0f);

    // A match another machine runs: its ping is worth a glance too
    values.remote = true;
    draw();
    TIDE_CHECK(stats.line_count == 1 && line_is(0, "60 fps   ping 34 ms"));
    // Outside any match, the frame rate alone
    values.match = false;
    draw();
    TIDE_CHECK(stats.line_count == 1 && line_is(0, "60 fps"));
    tide_draw_free(&list);
}

TIDE_TEST(stats_open_list_the_rest_above_their_line)
{
    start(false);
    draw();
    const tide_float2 closed = text_command("60 fps")->a;
    stats.open = true;
    draw();
    const char *all[] = {"60 fps", "cpu 2.1 ms", "ping 34 ms", "up 2.0 KB/s", "down 0.5 KB/s", "tick 1234", "56 entities",
                         "threads 12", "memory 48 MB", "OpenGL 3.3", "7 draw calls",
                         "Combat.Hit 1.50 ms", "Fall 0.31 ms", "Move 0.20 ms"}; // The three slowest systems, slowest first
    TIDE_REQUIRE(stats.line_count == 14);
    for (uint32_t i = 0; i < 14; i++) TIDE_CHECK(line_is(i, all[i]) && text_command(all[i]));

    // The line that's always there stays where it was, under the list: a
    // click in the same place closes the drawer again
    const tide_draw_command *line = text_command("60 fps");
    TIDE_CHECK(line->a.x == closed.x && line->a.y == closed.y);
    const tide_draw_command *first = text_command("cpu 2.1 ms");
    const tide_draw_command *last = text_command("Move 0.20 ms");
    TIDE_CHECK(first->a.y == closed.y - 13.0f * 20.0f && last->a.y == closed.y - 20.0f);
    TIDE_CHECK(first->a.x + 10.0f * 8.0f == closed.x + 48.0f); // Every line ends at the same edge
    const tide_rect box = panel();
    TIDE_CHECK(box.x + box.width == 952.0f && box.y + box.height == 532.0f && box.height == 14.0f * 20.0f + 12.0f);
    TIDE_CHECK(box.width == 18.0f * 8.0f + 16.0f && stats.rect.y == box.y); // As wide as its widest line

    // What isn't known has no line: a frame's work before one was timed, the
    // memory where the system doesn't say, a delay of no ticks
    values.work_ms = -1.0;
    values.memory = 0;
    values.renderer = NULL;
    values.delay_ticks = 3;
    draw();
    TIDE_CHECK(stats.line_count == 12 && line_is(1, "ping 34 ms") && line_is(2, "delay 3 ticks") && line_is(7, "threads 12"));
    TIDE_CHECK(line_is(8, "7 draw calls"));
    // The ping of another machine's match is in the line, so not in the list too
    values.remote = true;
    draw();
    TIDE_CHECK(line_is(0, "60 fps   ping 34 ms") && line_is(1, "delay 3 ticks") && stats.line_count == 11);
    // Outside any match, only what's the program's
    values = (tide_stats_values){.fps = 144.2, .work_ms = 0.5, .threads = 1, .draw_calls = 2, .profile = &profile};
    draw();
    TIDE_CHECK(stats.line_count == 4 && line_is(0, "144 fps") && line_is(1, "cpu 0.5 ms") && line_is(2, "threads 1")
               && line_is(3, "2 draw calls"));
    tide_draw_free(&list);
}

TIDE_TEST(stats_fit_a_small_screen)
{
    // A low one: the list keeps the lines there's room for, from its top
    start(true);
    screen = tide_f2(360.0f, 202.0f);
    draw();
    TIDE_REQUIRE(stats.line_count == 8);
    TIDE_CHECK(line_is(0, "60 fps") && line_is(1, "cpu 2.1 ms") && line_is(7, "threads 12"));
    tide_rect box = panel();
    TIDE_CHECK(box.y >= 8.0f && box.y + box.height == 194.0f && box.x >= 8.0f);
    TIDE_CHECK(!text_command("7 draw calls"));

    // A narrow one: a system's name is cut, and what it took stays
    start(true);
    tide_profile_add(&profile.systems, 1, "Combat.Projectiles.ResolveEveryCollisionThereIs", 0, 9 * MS);
    screen = tide_f2(232.0f, 540.0f);
    draw();
    TIDE_REQUIRE(stats.line_count == 14);
    box = panel();
    TIDE_CHECK(box.x == 8.0f && box.width == 216.0f); // No wider than the screen: 200 pixels of text
    const char *cut = stats.lines[11].text;
    TIDE_CHECK(strlen(cut) == 25 && strncmp(cut, "Combat.Projectile", 17) == 0 && strcmp(cut + 17, " 9.00 ms") == 0);
    TIDE_CHECK(text_command(cut) && text_command(cut)->a.x == 16.0f);

    // Too low for a list at all: the line alone, and still all of it
    start(true);
    screen = tide_f2(960.0f, 50.0f);
    draw();
    TIDE_CHECK(stats.line_count == 1 && line_is(0, "60 fps"));
    tide_draw_free(&list);
}

// ---------------------------------------------------------------------------
// What opens it, and whose a press is

// Whether devices say anything of a press: the mouse's, the pointer's or a finger's.
static bool has_press(const tide_devices *d)
{
    bool any = false;
#define ANY(b) any |= (b).pressed || (b).down || (b).up || (b).held;
#define MOUSE(name) ANY(d->mouse.name)
    TIDE_MOUSE_BUTTONS(MOUSE)
#undef MOUSE
    ANY(d->pointer.press)
    ANY(d->touchscreen.primaryTouch.press)
    for (int i = 0; i < TIDE_TOUCHES; i++) ANY(d->touchscreen.touches.at[i].press)
#undef ANY
    return any;
}

#define FLIPPED 1 // The drawer opened or closed
#define SAMPLED 2 // The game's input read a press
#define CLICKED 4 // The GUI's button under the drawer was clicked
#define VIEWED 8  // The views read a press

// The rest of a host's frame, once the devices were polled: what came of it.
// With `tick`, a tick samples the game's input in it.
static int frame(const bool key, const bool tick)
{
    int what = tide_stats_input(&stats, &devices, screen, key) ? FLIPPED : 0;
    sampled = devices;
    tide_gui_hide(&gui, &sampled);
    tide_stats_hide(&stats, &sampled);
    if (tick) {
        if (has_press(&sampled)) what |= SAMPLED;
        tide_devices_consume(&devices);
    }
    tide_devices seen = devices;
    tide_stats_hide(&stats, &seen);
    tide_gui_begin(&gui, &seen, screen, font);
    if (has_press(&gui.devices)) what |= VIEWED;
    // A wide button of the game's, which the drawer is drawn over
    if (tide_gui_button(&gui, 7, (tide_rect){700, 480, 260, 60}, "Menu")) what |= CLICKED;
    tide_draw_reset(&list);
    tide_gui_end(&gui, &list);
    draw();
    return what;
}

// A frame with the mouse at (x, y) from the top left, its `button` held or not.
static int mouse_frame(const float x, const float y, tide_button *button, const bool held)
{
    tide_touches_poll(&devices.touchscreen);
    devices.mouse.position = tide_f2(x, screen.y - y);
    tide_button_set(button, held);
    tide_pointer_poll(&devices, true);
    return frame(false, true);
}

#define NONE ((tide_touch_phase)TIDE_TOUCHES)

// A frame in which finger `source` did up to two things at (x, y) from the top left.
static int finger_frame(const uint32_t source, const tide_touch_phase first, const tide_touch_phase second, const float x,
                        const float y)
{
    tide_touches_poll(&devices.touchscreen);
    const tide_touch_phase phases[] = {first, second};
    for (int i = 0; i < 2; i++) {
        if (phases[i] != NONE) tide_touch_event(&devices.touchscreen, phases[i], source, tide_f2(x, screen.y - y));
    }
    tide_pointer_poll(&devices, false);
    return frame(false, true);
}

TIDE_TEST(stats_key_opens_and_closes_them)
{
    start(false);
    tide_touches_poll(&devices.touchscreen);
    tide_pointer_poll(&devices, false);
    TIDE_CHECK(frame(false, true) == 0 && !stats.open);
    TIDE_CHECK(frame(true, true) == FLIPPED && stats.open && stats.line_count == 14); // The list is there that frame
    TIDE_CHECK(frame(true, true) == 0 && stats.open); // Held: once
    TIDE_CHECK(frame(false, true) == 0 && stats.open);
    TIDE_CHECK(frame(true, true) == FLIPPED && !stats.open && stats.line_count == 1);
    tide_draw_free(&list);
}

TIDE_TEST(stats_click_opens_them_and_is_never_the_games)
{
    start(false);
    tide_button *left = &devices.mouse.left;
    TIDE_CHECK(mouse_frame(100, 100, left, false) == 0); // The drawer is drawn: its line is at (920, 516)
    // Between two frames, the mouse comes onto the line and presses. The game
    // samples before anything else sees the press, and doesn't get it; nor do
    // the views, nor the button the drawer is over.
    TIDE_CHECK(mouse_frame(920, 516, left, true) == 0);
    TIDE_CHECK(tide_stats_pressed(&stats) && stats.hovered && gui.active == 0);
    TIDE_CHECK(mouse_frame(920, 516, left, true) == 0);
    TIDE_CHECK(mouse_frame(920, 516, left, false) == FLIPPED); // Let go on it: a click
    TIDE_CHECK(stats.open && stats.line_count == 14);
    TIDE_CHECK(mouse_frame(920, 516, left, false) == 0 && !tide_stats_pressed(&stats));
    // The same place closes it again
    TIDE_CHECK(mouse_frame(920, 516, left, true) == 0);
    TIDE_CHECK(mouse_frame(920, 516, left, false) == FLIPPED && !stats.open);

    // Open, all of the list is the drawer's
    stats.open = true;
    mouse_frame(100, 100, left, false);
    TIDE_CHECK(mouse_frame(900, 300, left, true) == 0);
    TIDE_CHECK(mouse_frame(900, 300, left, false) == FLIPPED && !stats.open);
    TIDE_CHECK(mouse_frame(900, 300, left, false) == 0);
    // ...and closed, that place is the game's again
    TIDE_CHECK(mouse_frame(900, 300, left, true) == (SAMPLED | VIEWED));
    TIDE_CHECK(mouse_frame(900, 300, left, false) == (SAMPLED | VIEWED) && !stats.open); // It read the button go up

    // The game's button beside the drawer works as ever, and the world too
    mouse_frame(720, 500, left, false);
    TIDE_CHECK(mouse_frame(720, 500, left, true) == 0);
    TIDE_CHECK(mouse_frame(720, 500, left, false) == CLICKED && !stats.open);
    stats.open = true;
    mouse_frame(100, 100, left, false);
    TIDE_CHECK(mouse_frame(100, 100, left, true) == (SAMPLED | VIEWED));
    // A press that began in the world stays the game's as it crosses the
    // drawer, and letting it go there is no click
    TIDE_CHECK(mouse_frame(900, 300, left, true) == (SAMPLED | VIEWED) && !tide_stats_pressed(&stats));
    TIDE_CHECK(mouse_frame(900, 300, left, false) == (SAMPLED | VIEWED) && stats.open);
    mouse_frame(900, 300, left, false);
    // ...and one that began on the drawer stays the drawer's wherever it
    // goes, and is no click when it's let go elsewhere
    TIDE_CHECK(mouse_frame(900, 300, left, true) == 0);
    TIDE_CHECK(mouse_frame(100, 100, left, true) == 0 && tide_stats_pressed(&stats));
    TIDE_CHECK(mouse_frame(100, 100, left, false) == 0 && stats.open);
    TIDE_CHECK(mouse_frame(100, 100, left, false) == 0 && !tide_stats_pressed(&stats));

    // Another button on it is the drawer's too, and closes nothing
    tide_button *right = &devices.mouse.right;
    mouse_frame(900, 300, right, false);
    TIDE_CHECK(mouse_frame(900, 300, right, true) == 0 && tide_stats_pressed(&stats));
    TIDE_CHECK(mouse_frame(900, 300, right, false) == 0 && stats.open);
    TIDE_CHECK(mouse_frame(100, 100, right, false) == 0);
    TIDE_CHECK(mouse_frame(100, 100, right, true) == (SAMPLED | VIEWED));
    mouse_frame(100, 100, right, false);
    mouse_frame(100, 100, right, false);

    // A cursor the window holds is hidden, and on nothing: its click is the game's
    devices.mouse.locked = true;
    TIDE_CHECK(mouse_frame(900, 300, left, true) == (SAMPLED | VIEWED) && !stats.hovered);
    TIDE_CHECK(mouse_frame(900, 300, left, false) == (SAMPLED | VIEWED) && stats.open);
    tide_draw_free(&list);
}

// A game with no input, or a frame with no tick, samples nothing: what the
// devices say of a button stays until something does.
TIDE_TEST(stats_click_is_the_drawers_until_a_sample_is_done_with_it)
{
    start(false);
    tide_button *left = &devices.mouse.left;
    mouse_frame(100, 100, left, false);
    for (int held = 1; held >= 0; held--) {
        tide_touches_poll(&devices.touchscreen);
        devices.mouse.position = tide_f2(920, screen.y - 516);
        tide_button_set(left, held == 1);
        tide_pointer_poll(&devices, true);
        TIDE_CHECK(frame(false, false) == (held ? 0 : FLIPPED));
    }
    // The button is up, and the devices still say it went down and up: the
    // tick that samples them next would take that for a click
    TIDE_CHECK(stats.open && left->pressed && left->up && tide_stats_pressed(&stats));
    TIDE_CHECK(mouse_frame(920, 516, left, false) == 0); // ...and it doesn't get it
    TIDE_CHECK(mouse_frame(920, 516, left, false) == 0 && !tide_stats_pressed(&stats));

    // Never sampled, it's the drawer's until the button goes down elsewhere
    for (int held = 1; held >= 0; held--) {
        tide_touches_poll(&devices.touchscreen);
        tide_button_set(left, held == 1);
        tide_pointer_poll(&devices, true);
        frame(false, false);
    }
    TIDE_CHECK(!stats.open && tide_stats_pressed(&stats));
    tide_touches_poll(&devices.touchscreen);
    devices.mouse.position = tide_f2(100, screen.y - 100);
    tide_button_set(left, true);
    tide_pointer_poll(&devices, true);
    TIDE_CHECK(frame(false, false) == VIEWED && !tide_stats_pressed(&stats));
    tide_draw_free(&list);
}

TIDE_TEST(stats_tap_opens_them_and_is_never_the_games)
{
    start(false);
    TIDE_CHECK(finger_frame(1, NONE, NONE, 0, 0) == 0);
    // A finger touches the line: the drawer's, from the frame it lands in
    TIDE_CHECK(finger_frame(1, TIDE_TOUCH_BEGAN, NONE, 920, 516) == 0);
    TIDE_CHECK(tide_stats_pressed(&stats) && !stats.hovered && sampled.touchscreen.touches.at[0].id == 0);
    TIDE_CHECK(finger_frame(1, NONE, NONE, 0, 0) == 0);
    TIDE_CHECK(finger_frame(1, TIDE_TOUCH_ENDED, NONE, 920, 516) == FLIPPED && stats.open);
    TIDE_CHECK(finger_frame(1, NONE, NONE, 0, 0) == 0 && !tide_stats_pressed(&stats));

    // A tap shorter than a frame closes it again: it reads as held for one
    TIDE_CHECK(finger_frame(2, TIDE_TOUCH_BEGAN, TIDE_TOUCH_ENDED, 920, 516) == 0 && stats.open);
    TIDE_CHECK(finger_frame(2, NONE, NONE, 0, 0) == FLIPPED && !stats.open);
    TIDE_CHECK(finger_frame(2, NONE, NONE, 0, 0) == 0 && !tide_stats_pressed(&stats));

    // A finger that slides off the drawer and lifts elsewhere was the
    // drawer's all along, and no tap
    TIDE_CHECK(finger_frame(3, TIDE_TOUCH_BEGAN, NONE, 920, 516) == 0);
    TIDE_CHECK(finger_frame(3, TIDE_TOUCH_MOVED, NONE, 400, 300) == 0);
    TIDE_CHECK(finger_frame(3, TIDE_TOUCH_ENDED, NONE, 400, 300) == 0 && !stats.open);
    finger_frame(3, NONE, NONE, 0, 0);

    // While one finger plays, another taps the drawer: the first is still the
    // game's, and the second never is
    TIDE_CHECK(finger_frame(4, TIDE_TOUCH_BEGAN, NONE, 100, 300) == (SAMPLED | VIEWED));
    TIDE_CHECK(finger_frame(5, TIDE_TOUCH_BEGAN, NONE, 920, 516) == (SAMPLED | VIEWED));
    TIDE_CHECK(sampled.touchscreen.touches.at[0].press.held && sampled.touchscreen.touches.at[1].id == 0);
    TIDE_CHECK(sampled.touchscreen.primaryTouch.press.held && sampled.pointer.press.held); // The first finger is the pointer
    TIDE_CHECK(finger_frame(5, TIDE_TOUCH_ENDED, NONE, 920, 516) == (FLIPPED | SAMPLED | VIEWED) && stats.open);
    TIDE_CHECK(sampled.touchscreen.touches.at[1].id == 0);
    TIDE_CHECK(finger_frame(4, TIDE_TOUCH_ENDED, NONE, 100, 300) == (SAMPLED | VIEWED) && stats.open);
    tide_draw_free(&list);
}

TIDE_TEST(stats_that_are_off_take_no_press)
{
    start(true);
    tide_button *left = &devices.mouse.left;
    mouse_frame(100, 100, left, false);
    TIDE_CHECK(mouse_frame(920, 516, left, true) == 0 && tide_stats_pressed(&stats));
    // The game's build stops asking for them, with a press on them
    tide_stats_off(&stats);
    TIDE_CHECK(!stats.shown && !tide_stats_pressed(&stats) && stats.open); // Open or closed is the host's, and stays
    tide_touches_poll(&devices.touchscreen);
    tide_button_set(left, false);
    tide_pointer_poll(&devices, true);
    TIDE_CHECK(!tide_stats_input(&stats, &devices, screen, false) && stats.open);
    tide_devices_consume(&devices);
    // Where they were is the game's
    tide_touches_poll(&devices.touchscreen);
    tide_button_set(left, true);
    tide_pointer_poll(&devices, true);
    TIDE_CHECK(!tide_stats_input(&stats, &devices, screen, false) && !tide_stats_pressed(&stats));
    tide_devices copy = devices;
    tide_stats_hide(&stats, &copy);
    TIDE_CHECK(copy.mouse.left.held && copy.pointer.press.held);
    tide_draw_free(&list);
    tide_draw_free(&gui.list);
    tide_profile_free(&profile);
}
