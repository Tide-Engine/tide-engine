// Web only: the standard host (tide/host.h), in its own loop, over
// first_tick.tide, which starts in a menu and starts its match at Enter. The
// devices keep what happened for the next tick's sample, and outside a match
// no tick samples: what was pressed there, and how far the mouse moved, must
// not be what the match's first tick reads. A key still held as the match
// starts is. In the match, a key that goes down and up between two ticks is
// still the next tick's.
//
// Frames are as long as the test says, whatever the page's clock does: a 60th
// of a second, a tick each, and then a 240th, four to a tick. What a step
// sends the page is in the devices at the next frame's poll.

#include <stdio.h>
#include <string.h>

#include "game.h"
#include "tide/run.h"
#include "tide_web.h"

static int failures;
static int step;
static int wait;                      // Frames to run before the next step
static float length = 1.0f / 60.0f;   // A frame's, in seconds
static int32_t ticks_before;          // The match's ticks at the frame before this one
static int32_t ticks_then, jumps_then; // ...and what it had read as a key went down
static bool between;                  // No tick ran while the key was down
static int tries;

static void check(const bool ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) failures++;
}

static void key(const char *type, const char *code, const char *typed)
{
    char script[160];
    snprintf(script, sizeof script, "dispatchEvent(new KeyboardEvent('%s', {code: '%s', key: '%s'}))", type, code, typed);
    tide_web_eval(script);
}

// The mouse moves over the canvas, to `x` and `y` CSS pixels from its top left.
static void mouse_to(const int x, const int y)
{
    char script[320];
    snprintf(script, sizeof script,
             "{ const c = document.querySelector('canvas'); const r = c.getBoundingClientRect();"
             " c.dispatchEvent(new MouseEvent('mousemove', {clientX: r.left + %d, clientY: r.top + %d, bubbles: true})); }",
             x, y);
    tide_web_eval(script);
}

static int frame(void *user, const float seconds)
{
    (void)seconds;
    const int code = tide_run_frame(user, length);
    if (code != TIDE_KEEP_RUNNING) return code;
    const tide_world *world = tide_session_view(tide_run_session).current;
    const int32_t ticks = world ? world->Read.ticks : 0;
    const bool ticked = ticks != ticks_before; // A tick ran in this frame
    ticks_before = ticks;
    if (wait > 0) {
        wait--;
        return TIDE_KEEP_RUNNING;
    }
    const tide_devices *d = &tide_run_devices;
    switch (step++) {
    case 0: // In the menu: Space goes down, and the mouse moves
        check(!world, "the game starts in its menu, in no match");
        mouse_to(20, 20);
        wait = 1;
        break;
    case 1:
        key("keydown", "Space", " ");
        mouse_to(320, 140);
        break;
    case 2:
        key("keyup", "Space", " ");
        wait = 3;
        break;
    case 3:
        check(!world, "nothing started a match");
        check(!d->keyboard.space.pressed && !d->keyboard.space.down && !d->keyboard.space.up,
              "outside a match, the devices keep nothing for a sample that won't come: not a key that went down and up");
        check(d->mouse.delta.x == 0.0f && d->mouse.delta.y == 0.0f && d->mouse.position.x == 320.0f,
              "nor how far the mouse moved");
        key("keydown", "KeyH", "h"); // Held from here on
        wait = 2;
        break;
    case 4:
        check(d->keyboard.h.held && d->keyboard.h.pressed && !d->keyboard.h.down, "a key that's held stays pressed there");
        key("keydown", "Enter", "Enter"); // The menu starts the match
        wait = 8;
        break;
    case 5:
        check(world && ticks > 0, "Enter starts the match from the menu");
        check(world && world->Read.jumps == 0, "its first tick doesn't read a key that went down and up before it");
        check(world && world->Read.holds == ticks, "and reads one that's held as it starts, from the first tick on");
        check(world && world->Read.first == 0.0f, "nor how far the mouse moved before it");
        key("keyup", "Enter", "Enter");
        length = 1.0f / 240.0f; // Four frames to a tick from here
        wait = 8;
        break;
    case 6: // A frame whose tick just ran: the next ones have none
        if (!world || !ticked) {
            step--;
            break;
        }
        ticks_then = ticks;
        jumps_then = world->Read.jumps;
        key("keydown", "Space", " ");
        break;
    case 7:
        between = ticks == ticks_then;
        key("keyup", "Space", " ");
        break;
    case 8:
        between = between && ticks == ticks_then;
        wait = 12; // Past the next ticks
        if (!between && ++tries < 20) step = 6; // A tick came between after all: once more
        break;
    default:
        check(between, "a key went down and up between two ticks");
        check(world && world->Read.jumps == jumps_then + 1, "and the next tick reads it, once: nothing of a running match's input is dropped");
        return failures == 0 ? 0 : 1;
    }
    return TIDE_KEEP_RUNNING;
}

int main(const int argc, char **argv)
{
    tide_run_game = &tide_host_game_api;
    tide_run_configure(&(tide_run_desc){.title = "first tick", .argc = argc, .argv = argv});
    tide_platform_open(&(tide_window_desc){.title = tide_run_settings.title,
                                           .width = tide_run_settings.width,
                                           .height = tide_run_settings.height,
                                           .hidden = true});
    tide_run_begin();
    tide_platform_run(frame, NULL);
}
