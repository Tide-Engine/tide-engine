// Web only: the standard host's stats (tide/host.h, tide/stats.h), in its own
// loop, over stats.tide, which asks for them with its setting. They're a
// drawer: closed, one line, for which nothing is measured; F8, a click and a
// tap open it and close it; a press on it never reaches the game; and whether
// it's open is the host's, whatever build of the game runs. The page draws
// with Tide.checkGL, so the drawer also has to draw without a mistake.
//
// Frames are a 60th of a second each, whatever the page's clock says, so the
// host takes stock after the same number of them every run. What a step sends
// the page is in the devices at the next frame's poll, which the drawer reads
// in that frame.

#include <stdio.h>
#include <string.h>

#include "game.h"
#include "tide/run.h"
#include "tide_web.h"

static int failures;
static int step;
static int wait; // Frames to run before the next step
static bool starts_open, starts_closed;
static tide_game game_without;
static tide_host_game host_without;

static void check(const bool ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) failures++;
}

static void f8(const char *type)
{
    char script[120];
    snprintf(script, sizeof script, "dispatchEvent(new KeyboardEvent('%s', {code: 'F8', key: 'F8', keyCode: 119}))", type);
    tide_web_eval(script);
}

// The mouse's left button on the canvas, `x` and `y` CSS pixels from its top left.
static void mouse_event(const char *type, const float x, const float y)
{
    char script[400];
    snprintf(script, sizeof script,
             "{ const c = document.querySelector('canvas'); const r = c.getBoundingClientRect();"
             " c.dispatchEvent(new MouseEvent('%s', {button: 0, clientX: r.left + %d, clientY: r.top + %d,"
             " bubbles: true})); }",
             type, (int)x, (int)y);
    tide_web_eval(script);
}

// A finger's pointer event there, as a touchscreen sends it.
static void finger_event(const char *type, const float x, const float y)
{
    char script[400];
    snprintf(script, sizeof script,
             "{ const c = document.querySelector('canvas'); const r = c.getBoundingClientRect();"
             " c.dispatchEvent(new PointerEvent('%s', {pointerId: 5, pointerType: 'touch', clientX: r.left + %d,"
             " clientY: r.top + %d, bubbles: true, cancelable: true})); }",
             type, (int)x, (int)y);
    tide_web_eval(script);
}

// Whether the last frame's drawer had a line that starts with `text`.
static bool drew(const char *text)
{
    for (uint32_t i = 0; i < tide_run_over.count; i++) {
        const tide_draw_command *c = &tide_run_over.commands[i];
        if (c->kind == TIDE_DRAW_TEXT && strncmp(tide_run_over.text + c->text, text, strlen(text)) == 0) return true;
    }
    return false;
}

static int frame(void *user, const float seconds)
{
    (void)seconds;
    const int code = tide_run_frame(user, 1.0f / 60.0f);
    if (code != TIDE_KEEP_RUNNING) return code;
    if (wait > 0) {
        wait--;
        return TIDE_KEEP_RUNNING;
    }
    const tide_stats *drawer = &tide_run_drawer;
    const tide_world *world = tide_session_view(tide_run_session).current;
    const Seen *seen = &((const tide_local *)tide_run_local)->Seen;
    const tide_profile *measured = tide_run_jobs()->profile; // Where ticks say what their systems took, or nowhere
    // The middle of the drawer's one line, which stays where it is
    static float x, y;
    switch (step++) {
    case 0:
        check(starts_open, "a host that asks for the stats open, as tide run --stats does, starts them open");
        check(starts_closed, "and a game's stats setting starts them closed");
        check(!drawer->shown && tide_run_over.count == 0, "nothing shows before the host has a frame rate");
        wait = 20;
        break;
    case 1:
        check(drawer->shown && !drawer->open && drawer->line_count == 1 && drew("60 fps"),
              "the game's stats setting shows them closed: one line, the frame rate");
        check(!drew("threads") && !drew("cpu"), "and no list");
        check(!measured && tide_run_work_ms < 0.0 && tide_run_memory == 0,
              "closed, nothing is measured for them: no system's time, no frame's work, no memory");
        x = drawer->rect.x + drawer->rect.width * 0.5f;
        y = drawer->rect.y + drawer->rect.height * 0.5f;
        f8("keydown");
        wait = 1;
        break;
    case 2:
        check(drawer->open && drew("60 fps") && drew("threads") && drew("WebGL 2") && drew("tick "),
              "F8 opens them: the list is above the line");
        check(measured == &tide_run_profiler.profile && tide_run_memory > 0, "and what the list shows is measured from then on");
        check(seen->key, "the game sees the key too");
        f8("keyup");
        wait = 70; // More than a second: the host takes stock of what it timed
        break;
    case 3:
        check(drew("cpu ") && drew("Count "), "the list has what a frame's work takes, and the game's system by name");
        mouse_event("mousedown", x, y);
        break;
    case 4:
        check(tide_stats_pressed(drawer) && drawer->open, "a press on the line is the drawer's");
        mouse_event("mouseup", x, y);
        break;
    case 5:
        check(!drawer->open && drawer->shown && drawer->line_count == 1 && !drew("threads"), "a click on the line closes them");
        check(world && world->Read.presses == 0 && !seen->pressed, "and the game read no press: neither its match nor its views");
        wait = 1;
        break;
    case 6:
        check(!measured, "closed again, nothing is measured");
        finger_event("pointerdown", x, y);
        break;
    case 7:
        finger_event("pointerup", x, y);
        break;
    case 8:
        check(drawer->open && drew("threads"), "a tap on the line opens them");
        check(world && world->Read.presses == 0 && !seen->pressed, "and the game read no touch");
        tide_run_game = &host_without; // A build without the setting, as a reload brings one
        wait = 1;
        break;
    case 9:
        check(!drawer->shown && !tide_stats_pressed(drawer), "a build without the setting shows none");
        tide_run_game = &tide_host_game_api; // ...and one with it again
        wait = 20;
        break;
    case 10:
        check(drawer->shown && drawer->open && drew("threads"),
              "and with it back, they're open as the player left them: that's the host's, whatever build runs");
        mouse_event("mousedown", 20.0f, 20.0f);
        break;
    case 11:
        mouse_event("mouseup", 20.0f, 20.0f);
        wait = 2;
        break;
    default:
        check(world && world->Read.presses > 0 && seen->pressed, "away from them, a click is the game's: its match and its views read it");
        check(drawer->open, "and opens or closes nothing");
        return failures == 0 ? 0 : 1;
    }
    return TIDE_KEEP_RUNNING;
}

int main(const int argc, char **argv)
{
    game_without = tide_game_api;
    game_without.stats = false;
    host_without = tide_host_game_api;
    host_without.game = &game_without;

    tide_run_game = &tide_host_game_api;
    tide_run_configure(&(tide_run_desc){.title = "stats", .stats = true, .stats_open = true, .argc = argc, .argv = argv});
    starts_open = tide_run_drawer.open;
    tide_run_configure(&(tide_run_desc){.title = "stats", .argc = argc, .argv = argv}); // The game's setting alone
    starts_closed = !tide_run_drawer.open;
    tide_platform_open(&(tide_window_desc){.title = tide_run_settings.title,
                                           .width = tide_run_settings.width,
                                           .height = tide_run_settings.height,
                                           .hidden = true});
    tide_run_begin();
    tide_platform_run(frame, NULL);
}
