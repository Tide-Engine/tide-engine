#include <math.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

static tide_local local;
static tide_gui gui;
static tide_draw_list draw;
static tide_devices devices;

// A frame `seconds` long, with the mouse at `at` in the GUI's pixels.
static void frame(const float seconds, const tide_float2 at)
{
    tide_draw_reset(&draw);
    devices.mouse.position = tide_f2(at.x, 600 - at.y);
    tide_pointer_poll(&devices, true);
    tide_gui_begin(&gui, &devices, tide_f2(800, 600), NULL);
    tide_local_frame_time(&local, seconds);
    tide_frame(NULL, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    tide_gui_end(&gui, &draw);
}

static void start(void)
{
    tide_draw_free(&gui.list);
    tide_draw_free(&draw);
    memset(&gui, 0, sizeof gui);
    memset(&devices, 0, sizeof devices);
    tide_local_free(&local);
    tide_local_init(&local);
}

static int near(const float a, const float b)
{
    return fabsf(a - b) <= 0.0001f * (1 + fabsf(b));
}

static const tide_float2 away = {500, 500};

// About 99% of the way in its seconds: a tenth of a second of half a second's
// animation covers 1 - e^(-0.1 * 4.6 / 0.5) of what's left.
#define STEP 0.601893f

TIDE_TEST(gui_animate_eases_values_toward_their_targets)
{
    start();
    frame(0.1f, away);
    TIDE_CHECK(local.Seen.opened == 0); // First asked: at its target
    local.Seen.open = true;
    frame(0.1f, away);
    TIDE_CHECK(near(local.Seen.opened, STEP));
    TIDE_CHECK(near(local.Seen.slid.x, 100 * STEP) && local.Seen.slid.y == 0);
    TIDE_CHECK(near(local.Seen.tint.r, STEP) && near(local.Seen.tint.a, 1));
    for (int i = 0; i < 3; i++) TIDE_CHECK(near(local.Seen.rows.at[i], (float)(i + 1) * STEP)); // A loop's turns apart
    frame(0.1f, away);
    TIDE_CHECK(near(local.Seen.opened, STEP + (1 - STEP) * STEP));
    // It settles on its target exactly, so `t > 0` ends a fade out
    for (int i = 0; i < 40; i++) frame(0.1f, away);
    TIDE_CHECK(local.Seen.opened == 1 && local.Seen.slid.x == 100);
    local.Seen.open = false;
    for (int i = 0; i < 40; i++) frame(0.1f, away);
    TIDE_CHECK(local.Seen.opened == 0);
    // No frame time, as a host that gives none: at the target at once
    local.Seen.open = true;
    frame(0, away);
    TIDE_CHECK(local.Seen.opened == 1);
}

TIDE_TEST(gui_animate_starts_from_its_from_and_over_after_a_frame_away)
{
    start();
    frame(0.1f, away);
    local.Seen.showToast = true;
    frame(0.1f, away);
    TIDE_CHECK(local.Seen.toast == 0); // From 0, the frame it appears
    frame(0.1f, away);
    TIDE_CHECK(near(local.Seen.toast, STEP));
    // A frame without it, and it starts over
    local.Seen.showToast = false;
    frame(0.1f, away);
    local.Seen.showToast = true;
    frame(0.1f, away);
    TIDE_CHECK(local.Seen.toast == 0);
}

TIDE_TEST(gui_animate_eases_a_controls_hover_and_press)
{
    start();
    frame(0.1f, away);
    frame(0.1f, away);
    TIDE_CHECK(local.Seen.hover == 0 && local.Seen.press == 0);
    frame(0.1f, tide_f2(50, 50));
    TIDE_CHECK(local.Seen.hovered);
    TIDE_CHECK(near(local.Seen.hover, STEP));
    tide_button_set(&devices.mouse.left, true);
    frame(0.1f, tide_f2(50, 50));
    TIDE_CHECK(near(local.Seen.press, STEP));
    TIDE_CHECK(near(local.Seen.hover, STEP + (1 - STEP) * STEP));
    tide_button_set(&devices.mouse.left, false);
    for (int i = 0; i < 40; i++) frame(0.1f, away);
    TIDE_CHECK(local.Seen.hover == 0 && local.Seen.press == 0);
}

TIDE_TEST(gui_tween_moves_at_a_constant_speed_and_turns_back)
{
    start();
    frame(0.1f, away);
    TIDE_CHECK(local.Seen.tweened == 0);
    local.Seen.open = true;
    frame(0.1f, away);
    TIDE_CHECK(near(local.Seen.tweened, 0.2f));                         // A tenth of the half second
    TIDE_CHECK(near(local.Seen.eased, 4 * 0.2f * 0.2f * 0.2f));         // Eased: slow at first
    frame(0.1f, away);
    frame(0.1f, away);
    TIDE_CHECK(near(local.Seen.tweened, 0.6f));
    local.Seen.open = false;                                           // Back from 0.6: as long as it went, 0.3 seconds
    frame(0.1f, away);
    TIDE_CHECK(near(local.Seen.tweened, 0.4f));
    frame(0.1f, away);
    TIDE_CHECK(near(local.Seen.tweened, 0.2f));
    frame(0.1f, away);
    TIDE_CHECK(local.Seen.tweened == 0);                               // There exactly, at 0.3 seconds
    frame(0, away);
    local.Seen.open = true;
    frame(0, away);                                                    // No frame time: at once
    TIDE_CHECK(local.Seen.tweened == 1);
}

TIDE_TEST(gui_animate_target_widgets_take_the_pointer_where_shown)
{
    start();
    frame(0.1f, tide_f2(400, 350));
    TIDE_CHECK(local.Seen.panelSize.x == 100 && local.Seen.panelSize.y == 50);
    frame(0.1f, tide_f2(400, 350));
    tide_button_set(&devices.mouse.left, true);
    frame(0.1f, tide_f2(400, 350));
    tide_button_set(&devices.mouse.left, false);
    frame(0.1f, tide_f2(400, 350));
    TIDE_CHECK(local.Seen.pressed == 1);
}
