#include "tide/devices.h"
#include "tide/player.h"
#include "tide_test.h"

TIDE_TEST(devices_quick_tap_between_samples_is_not_lost)
{
    tide_devices d = {0};
    tide_button_set(&d.keyboard.space, true);  // frame 1: down
    tide_button_set(&d.keyboard.space, false); // frame 2: up again, before any sample
    TIDE_CHECK(d.keyboard.space.pressed);
    TIDE_CHECK(d.keyboard.space.down);
    TIDE_CHECK(d.keyboard.space.up);

    tide_devices_consume(&d);
    TIDE_CHECK(!d.keyboard.space.pressed);
    TIDE_CHECK(!d.keyboard.space.down);
    TIDE_CHECK(!d.keyboard.space.up);
}

TIDE_TEST(devices_held_button_stays_pressed_but_goes_down_once)
{
    tide_devices d = {0};
    tide_button_set(&d.gamepad.buttonSouth, true);
    tide_devices_consume(&d);
    tide_button_set(&d.gamepad.buttonSouth, true);
    TIDE_CHECK(d.gamepad.buttonSouth.pressed);
    TIDE_CHECK(!d.gamepad.buttonSouth.down);
}

TIDE_TEST(devices_consume_resets_mouse_motion)
{
    tide_devices d = {0};
    d.mouse.position = tide_f2(10, 20);
    d.mouse.delta = tide_f2(3, 4);
    d.mouse.scroll = tide_f2(0, 1);
    tide_devices_consume(&d);
    TIDE_CHECK(d.mouse.position.x == 10.0f);
    TIDE_CHECK(d.mouse.delta.x == 0.0f && d.mouse.scroll.y == 0.0f);
}

// One poll of the platform: its touch events, then the pointer.
typedef struct touch_step {
    tide_touch_phase phase;
    uint32_t source;
    float x, y;
} touch_step;

static void poll_touches(tide_devices *d, const touch_step *steps, const int count)
{
    tide_touches_poll(&d->touchscreen);
    for (int i = 0; i < count; i++) {
        tide_touch_event(&d->touchscreen, steps[i].phase, steps[i].source, tide_f2(steps[i].x, steps[i].y));
    }
    tide_pointer_poll(d, false);
}

TIDE_TEST(devices_touch_moves_and_lifts)
{
    tide_devices d = {0};
    poll_touches(&d, (touch_step[]){{TIDE_TOUCH_BEGAN, 7, 10, 20}}, 1);
    const tide_touch *t = &d.touchscreen.touches.at[0];
    TIDE_CHECK(t->press.pressed && t->press.down && t->press.held && !t->press.up);
    TIDE_CHECK(t->id == 1 && t->position.x == 10.0f && t->startPosition.y == 20.0f);
    TIDE_CHECK(d.touchscreen.primaryTouch.id == 1 && d.touchscreen.primaryTouch.press.down);
    TIDE_CHECK(d.pointer.touch && d.pointer.press.down && d.pointer.position.x == 10.0f);

    tide_devices_consume(&d);
    poll_touches(&d, (touch_step[]){{TIDE_TOUCH_MOVED, 7, 15, 18}, {TIDE_TOUCH_MOVED, 7, 16, 18}}, 2);
    TIDE_CHECK(t->press.pressed && !t->press.down);
    TIDE_CHECK(t->delta.x == 6.0f && t->delta.y == -2.0f && t->poll_delta.x == 6.0f);
    TIDE_CHECK(t->startPosition.x == 10.0f && t->position.x == 16.0f);
    TIDE_CHECK(d.touchscreen.primaryTouch.delta.x == 6.0f && d.pointer.delta.x == 6.0f);

    tide_devices_consume(&d);
    poll_touches(&d, (touch_step[]){{TIDE_TOUCH_ENDED, 7, 16, 18}}, 1);
    TIDE_CHECK(t->press.pressed && t->press.up && !t->press.held && t->id == 1);
    TIDE_CHECK(d.touchscreen.primaryTouch.press.up && d.pointer.press.up);

    // Once a sample saw it lift, its slot is free.
    tide_devices_consume(&d);
    TIDE_CHECK(t->id == 0 && !t->press.pressed && d.touchscreen.primaryTouch.id == 0);
}

TIDE_TEST(devices_touch_tap_between_polls_reads_as_held_for_one)
{
    tide_devices d = {0};
    poll_touches(&d, (touch_step[]){{TIDE_TOUCH_BEGAN, 3, 5, 5}, {TIDE_TOUCH_ENDED, 3, 5, 5}}, 2);
    const tide_touch *t = &d.touchscreen.touches.at[0];
    TIDE_CHECK(t->press.held && t->press.down && !t->press.up);
    TIDE_CHECK(d.pointer.press.held);

    poll_touches(&d, NULL, 0);
    TIDE_CHECK(!t->press.held && t->press.up && t->press.down);
    TIDE_CHECK(!d.touchscreen.primaryTouch.press.held && d.touchscreen.primaryTouch.press.up);
    TIDE_CHECK(!d.pointer.press.held && d.pointer.press.up);
}

TIDE_TEST(devices_fingers_keep_their_slots)
{
    tide_devices d = {0};
    poll_touches(&d, (touch_step[]){{TIDE_TOUCH_BEGAN, 100, 1, 1}, {TIDE_TOUCH_BEGAN, 200, 2, 2}}, 2);
    const tide_touches *s = &d.touchscreen.touches;
    TIDE_CHECK(s->at[0].id == 1 && s->at[1].id == 2);
    TIDE_CHECK(d.touchscreen.primaryTouch.id == 1);

    // The first lifts: the second keeps its slot, and isn't the primary touch.
    tide_devices_consume(&d);
    poll_touches(&d, (touch_step[]){{TIDE_TOUCH_ENDED, 100, 1, 1}, {TIDE_TOUCH_MOVED, 200, 3, 2}}, 2);
    tide_devices_consume(&d);
    TIDE_CHECK(s->at[0].id == 0 && s->at[1].id == 2 && s->at[1].position.x == 3.0f);
    TIDE_CHECK(d.touchscreen.primaryTouch.id == 0);

    // A new finger takes the free slot, with a new id though the platform's number comes again, and it's the
    // primary touch, as none is.
    poll_touches(&d, (touch_step[]){{TIDE_TOUCH_BEGAN, 100, 9, 9}}, 1);
    TIDE_CHECK(s->at[0].id == 3 && s->at[0].source == 100);
    TIDE_CHECK(d.touchscreen.primaryTouch.id == 3 && d.touchscreen.primaryTouch.press.down);

    // Moves of a finger that isn't touching are left out.
    poll_touches(&d, (touch_step[]){{TIDE_TOUCH_MOVED, 555, 50, 50}, {TIDE_TOUCH_ENDED, 556, 50, 50}}, 2);
    TIDE_CHECK(s->at[2].id == 0);
}

TIDE_TEST(devices_touches_past_the_last_slot_are_left_out)
{
    tide_devices d = {0};
    tide_touches_poll(&d.touchscreen);
    for (uint32_t i = 0; i < TIDE_TOUCHES + 2; i++) {
        tide_touch_event(&d.touchscreen, TIDE_TOUCH_BEGAN, i, tide_f2((float)i, 0.0f));
    }
    TIDE_CHECK(d.touchscreen.touches.at[TIDE_TOUCHES - 1].id == TIDE_TOUCHES);
    TIDE_CHECK(d.touchscreen.last_id == TIDE_TOUCHES);
    TIDE_CHECK(tide_touch_at(d.touchscreen.touches, TIDE_TOUCHES).id == 0);
    TIDE_CHECK(tide_touch_at(d.touchscreen.touches, -1).id == 0);
    TIDE_CHECK(tide_touch_at(d.touchscreen.touches, 4).id == 5);
}

// A tap: a finger touches in one poll and lifts in the next.
static void tap(tide_devices *d, const uint32_t source, const float x, const float y)
{
    poll_touches(d, (touch_step[]){{TIDE_TOUCH_BEGAN, source, x, y}}, 1);
    poll_touches(d, (touch_step[]){{TIDE_TOUCH_ENDED, source, x, y}}, 1);
}

// A game with no input, and a program outside any match, sample nothing: a
// finger that lifted is never cleared from its slot. Fingers keep coming all
// the same, each in the slot of the one that lifted longest ago.
TIDE_TEST(devices_fingers_keep_coming_where_nothing_samples)
{
    tide_devices d = {0};
    const tide_touches *s = &d.touchscreen.touches;
    for (int32_t i = 1; i <= 25; i++) {
        const tide_touch *t = &s->at[(i - 1) % TIDE_TOUCHES]; // The slots in turn
        poll_touches(&d, (touch_step[]){{TIDE_TOUCH_BEGAN, 7, (float)i, 5}}, 1);
        TIDE_CHECK(t->id == i && t->press.held && t->press.down && !t->press.up && t->position.x == (float)i);
        TIDE_CHECK(d.touchscreen.primaryTouch.id == i && d.touchscreen.primaryTouch.press.held);
        TIDE_CHECK(d.pointer.touch && d.pointer.press.held && d.pointer.position.x == (float)i);
        poll_touches(&d, (touch_step[]){{TIDE_TOUCH_ENDED, 7, (float)i, 5}}, 1);
        TIDE_CHECK(t->id == i && !t->press.held && t->press.up && !d.pointer.press.held);
    }
    // The slots have the last ten, until a sample clears them
    for (int32_t i = 0; i < TIDE_TOUCHES; i++) TIDE_CHECK(s->at[i].id > 15 && s->at[i].press.pressed);
    tide_devices_consume(&d);
    for (int32_t i = 0; i < TIDE_TOUCHES; i++) TIDE_CHECK(s->at[i].id == 0 && s->at[i].rested == 0);
    TIDE_CHECK(d.touchscreen.primaryTouch.id == 0);
}

TIDE_TEST(devices_the_finger_that_lifted_longest_ago_gives_its_slot)
{
    tide_devices d = {0};
    const tide_touches *s = &d.touchscreen.touches;
    // One finger rests on the screen while nine others tap, and nothing samples: every slot is taken
    poll_touches(&d, (touch_step[]){{TIDE_TOUCH_BEGAN, 1, 0, 0}}, 1);
    for (uint32_t i = 2; i <= TIDE_TOUCHES; i++) tap(&d, i, (float)i, 0);
    TIDE_CHECK(s->at[0].id == 1 && s->at[0].press.held && s->at[9].id == 10 && !s->at[9].press.held);

    // It lifts as another touches, in one poll. The new finger takes the slot
    // of the tap that lifted longest ago, not of the finger that lifted just
    // now, whose lift is still there to read.
    poll_touches(&d, (touch_step[]){{TIDE_TOUCH_ENDED, 1, 0, 0}, {TIDE_TOUCH_BEGAN, 11, 50, 0}}, 2);
    TIDE_CHECK(s->at[0].id == 1 && !s->at[0].press.held && s->at[0].press.up);
    TIDE_CHECK(s->at[1].id == 11 && s->at[1].press.held && s->at[1].press.down && s->at[1].position.x == 50.0f);
    TIDE_CHECK(s->at[2].id == 3 && s->at[2].press.pressed); // The other taps are as they were
    // The primary touch lifted and touched again: an up and a down
    const tide_touch *primary = &d.touchscreen.primaryTouch;
    TIDE_CHECK(primary->id == 11 && primary->press.held && primary->press.up && primary->press.down);

    // Fingers that lifted give their slots, and fingers that touch never do:
    // nine more fingers come down, and with ten on the screen, another is left out
    touch_step more[TIDE_TOUCHES];
    for (uint32_t i = 0; i < TIDE_TOUCHES; i++) more[i] = (touch_step){TIDE_TOUCH_BEGAN, 20 + i, (float)i, 9};
    poll_touches(&d, more, TIDE_TOUCHES);
    for (int32_t i = 0; i < TIDE_TOUCHES; i++) TIDE_CHECK(s->at[i].press.held && s->at[i].id >= 11);
    TIDE_CHECK(s->at[1].id == 11 && d.touchscreen.last_id == 20 && primary->id == 11);
    for (int32_t i = 0; i < TIDE_TOUCHES; i++) TIDE_CHECK(s->at[i].source != 29); // The tenth of them
    // Its moves and its lift are nothing, as it never touched
    poll_touches(&d, (touch_step[]){{TIDE_TOUCH_MOVED, 29, 70, 70}, {TIDE_TOUCH_ENDED, 29, 70, 70}}, 2);
    for (int32_t i = 0; i < TIDE_TOUCHES; i++) TIDE_CHECK(s->at[i].press.held && s->at[i].position.x != 70.0f);
}

// With a slot free, a tap no sample has seen keeps its own: the next sample reads it.
TIDE_TEST(devices_tap_between_samples_keeps_its_slot_while_one_is_free)
{
    tide_devices d = {0};
    const tide_touches *s = &d.touchscreen.touches;
    tap(&d, 1, 5, 5);
    for (uint32_t i = 2; i <= TIDE_TOUCHES; i++) poll_touches(&d, (touch_step[]){{TIDE_TOUCH_BEGAN, i, (float)i, 0}}, 1);
    TIDE_CHECK(s->at[0].id == 1 && s->at[0].press.pressed && s->at[0].press.down && s->at[0].press.up);
    for (int32_t i = 1; i < TIDE_TOUCHES; i++) TIDE_CHECK(s->at[i].id == i + 1 && s->at[i].press.held);
    tide_devices_consume(&d);
    TIDE_CHECK(s->at[0].id == 0 && s->at[1].id == 2 && s->at[1].press.pressed && !s->at[1].press.down);
    // ...and now that slot is the next finger's
    poll_touches(&d, (touch_step[]){{TIDE_TOUCH_BEGAN, 40, 1, 1}}, 1);
    TIDE_CHECK(s->at[0].id == 11 && s->at[0].press.down);
}

TIDE_TEST(devices_pointer_follows_what_was_used_last)
{
    tide_devices d = {0};
    d.mouse.position = tide_f2(40, 50);
    tide_touches_poll(&d.touchscreen);
    tide_pointer_poll(&d, false);
    TIDE_CHECK(!d.pointer.touch && d.pointer.position.x == 40.0f); // The mouse until a touch

    poll_touches(&d, (touch_step[]){{TIDE_TOUCH_BEGAN, 1, 7, 8}}, 1);
    TIDE_CHECK(d.pointer.touch && d.pointer.position.x == 7.0f && d.pointer.press.held);

    // The mouse moving while a finger is still down takes the pointer back.
    d.mouse.poll_delta = tide_f2(1, 0);
    d.mouse.position = tide_f2(41, 50);
    tide_touches_poll(&d.touchscreen);
    tide_pointer_poll(&d, true);
    TIDE_CHECK(!d.pointer.touch && d.pointer.position.x == 41.0f && !d.pointer.press.held && d.pointer.press.up);
}

TIDE_TEST(player_ids)
{
    const tide_player_id none = {0};
    TIDE_CHECK(tide_player_is_null(none));
    TIDE_CHECK(tide_player_index(none) == -1);
    TIDE_CHECK(tide_player_index(tide_player_from_index(0)) == 0);
    TIDE_CHECK(tide_player_index(tide_player_from_index(3)) == 3);
    TIDE_CHECK(tide_player_index(tide_player_from_index((int32_t)TIDE_PLAYERS_MOST)) == -1);
    TIDE_CHECK(tide_player_is_null(tide_player_from_index(-1)));
    TIDE_CHECK(tide_player_equal(tide_player_from_index(2), tide_player_from_index(2)));
}
