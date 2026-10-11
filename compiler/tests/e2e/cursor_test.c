#include <string.h>

#include "game.h"
#include "tide_test.h"

// Frames of cursor.tide's views on a 1920 x 1080 window, and the samples a
// host takes before each, played with made-up devices. The test is the host
// and the window at once: it reads what the frame asked (tide_gui_cursor_lock)
// as a host does to pass it on, and says whether the window holds the cursor
// (devices.mouse.locked) as the platform does when it polls.

static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_devices devices;

static tide_input sample(void)
{
    tide_pointer_poll(&devices, true); // As the platform polls it: the pointer follows the mouse
    tide_devices sampled = devices;
    tide_gui_hide(&gui, &sampled);
    const tide_input in = tide_input_sample(&sampled, &local);
    tide_devices_consume(&devices);
    return in;
}

static void frame(void)
{
    tide_draw_reset(&draw);
    tide_gui_begin(&gui, &devices, tide_f2(1920.0f, 1080.0f), NULL);
    tide_frame(NULL, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    tide_gui_end(&gui, &draw);
}

// A sample, then a frame, as a host's loop goes.
static tide_input step(void)
{
    const tide_input in = sample();
    frame();
    return in;
}

// The mouse moved by `by` since the last poll.
static void move(const tide_float2 by)
{
    devices.mouse.delta = tide_f2(devices.mouse.delta.x + by.x, devices.mouse.delta.y + by.y);
    devices.mouse.poll_delta = by;
}

static void start(void)
{
    memset(&gui, 0, sizeof gui);
    memset(&devices, 0, sizeof devices);
    tide_local_init(&local);
    devices.mouse.position = tide_f2(900.0f, 500.0f); // Away from the pause button
}

TIDE_TEST(cursor_lock_is_asked_each_frame_and_held_when_the_window_says)
{
    start();
    TIDE_CHECK(!tide_gui_cursor_lock(&gui)); // Nothing asked yet

    // The view asks. The window doesn't hold the cursor yet, so nothing looks.
    move(tide_f2(5.0f, 0.0f));
    tide_input in = step();
    TIDE_CHECK(tide_gui_cursor_lock(&gui));
    TIDE_CHECK(!in.locked && in.look.x == 0.0f);
    TIDE_CHECK(local.Menu.looked == 0 && !local.Menu.held);

    // The window holds it: the sample and the view both read that, and look.
    devices.mouse.locked = true;
    move(tide_f2(3.0f, -2.0f));
    in = step();
    TIDE_CHECK(in.locked && in.look.x == 3.0f && in.look.y == -2.0f);
    TIDE_CHECK(local.Menu.looked == 1 && local.Menu.turned.x == 3.0f && local.Menu.turned.y == -2.0f);
    TIDE_CHECK(tide_gui_cursor_lock(&gui)); // Asked again: it's asked every frame

    // Paused, the view stops asking: the ask is gone with the frame that didn't make it.
    local.Menu.paused = true;
    move(tide_f2(0.0f, 0.0f));
    step();
    TIDE_CHECK(!tide_gui_cursor_lock(&gui));
    TIDE_CHECK(local.Menu.looked == 1);
    // What the window says is still what's read, until it lets the cursor go
    TIDE_CHECK(step().locked);
    devices.mouse.locked = false;
    TIDE_CHECK(!step().locked);

    // Resumed, it asks again.
    local.Menu.paused = false;
    local.Menu.held = false;
    step();
    TIDE_CHECK(tide_gui_cursor_lock(&gui));
}

TIDE_TEST(cursor_lock_that_goes_away_is_read_not_assumed)
{
    start();
    step();
    devices.mouse.locked = true;
    step();
    TIDE_CHECK(local.Menu.held && local.Menu.dropped == 0);

    // The system takes the cursor back while the game still asks, as a browser
    // does when the player presses Escape: the game reads it, and pauses.
    devices.mouse.locked = false;
    move(tide_f2(9.0f, 9.0f));
    const tide_input in = step();
    TIDE_CHECK(!in.locked && in.look.x == 0.0f);
    TIDE_CHECK(local.Menu.dropped == 1 && local.Menu.paused);
    TIDE_CHECK(local.Menu.looked == 1); // Only the frame it held
    TIDE_CHECK(tide_gui_cursor_lock(&gui)); // That frame still asked...
    step();
    TIDE_CHECK(!tide_gui_cursor_lock(&gui)); // ...and the paused one after it didn't
}

TIDE_TEST(cursor_locked_is_on_no_widget)
{
    start();
    step();
    // The cursor stays where it was locked: here, on the pause button.
    devices.mouse.position = tide_f2(100.0f, 1080.0f - 50.0f);
    devices.mouse.locked = true;
    step();

    // A click is the game's, and presses no button.
    tide_button_set(&devices.mouse.left, true);
    TIDE_CHECK(step().fire);
    tide_button_set(&devices.mouse.left, false);
    step();
    TIDE_CHECK(local.Menu.presses == 0);

    // Let go, the same click is the button's, and not the game's.
    devices.mouse.locked = false;
    local.Menu.paused = true;
    step();
    tide_button_set(&devices.mouse.left, true);
    TIDE_CHECK(!step().fire);
    tide_button_set(&devices.mouse.left, false);
    step();
    TIDE_CHECK(local.Menu.presses == 1);
}

TIDE_TEST(cursor_lock_gives_way_to_a_modal)
{
    start();
    step();
    TIDE_CHECK(tide_gui_cursor_lock(&gui));

    // A modal's widgets need the pointer: while it's up, the lock isn't asked
    // for, though the view still calls Cursor.Lock.
    local.Menu.modal = true;
    step();
    TIDE_CHECK(!tide_gui_cursor_lock(&gui));
    local.Menu.modal = false;
    step();
    TIDE_CHECK(tide_gui_cursor_lock(&gui));
}
