// Web only: the cursor's lock is the browser's to give and to take back.
//
// A browser gives the lock only when asked from a click or a key press, lets
// it go when the player presses Escape, and refuses it for a moment after.
// A headless one gives none to events a script made, so the page stands in for
// the browser here: it counts what the canvas is asked, and gives the lock,
// refuses it or takes it back as a browser would. What's checked is the
// program's side: when it asks, what it says it has, how the mouse reads while
// the cursor is locked, and that it never asks again on its own.
//
// Each frame checks what happened at the end of the previous one, the way real
// events arrive between frames, then tells the platform whether it wants the
// lock, as a host does every frame.

#include <stdio.h>

#include "tide/platform.h"
#include "tide_web.h"

static tide_devices devices;
static int failures;

static void check(const bool ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) failures++;
}

// The browser's side. `lock` counts what the page asked of it: `asks` for the
// lock, with `options` the last one's, and `exits` to let it go. `held` is the
// element that has the lock, which document.pointerLockElement says, and
// `active` whether a click or a key press still counts
// (navigator.userActivation).
static void stand_in_for_the_browser(void)
{
    tide_web_eval(
        "{ const c = document.querySelector('canvas');"
        "  window.lock = { asks: 0, exits: 0, options: undefined, held: null, active: false, resolve: null, reject: null };"
        "  Object.defineProperty(document, 'pointerLockElement', { get: () => lock.held, configurable: true });"
        "  Object.defineProperty(navigator, 'userActivation', { get: () => ({ isActive: lock.active }), configurable: true });"
        "  c.requestPointerLock = options => { lock.asks++; lock.options = options;"
        "    return new Promise((resolve, reject) => { lock.resolve = resolve; lock.reject = reject; }); };"
        "  document.exitPointerLock = () => { lock.exits++; lock.held = null;"
        "    document.dispatchEvent(new Event('pointerlockchange')); }; }");
}

// The browser gives the canvas the lock it was asked for.
static void browser_gives(void)
{
    tide_web_eval("{ lock.held = document.querySelector('canvas'); lock.resolve();"
                  " document.dispatchEvent(new Event('pointerlockchange')); }");
}

// ...or refuses it, saying why.
static void browser_refuses(const char *error)
{
    char script[200];
    snprintf(script, sizeof script,
             "{ lock.reject(new DOMException('refused', '%s')); document.dispatchEvent(new Event('pointerlockerror')); }",
             error);
    tide_web_eval(script);
}

// ...or takes it back, as when the player presses Escape.
static void browser_takes_back(void)
{
    tide_web_eval("{ lock.held = null; document.dispatchEvent(new Event('pointerlockchange')); }");
}

// A mouse event on the canvas: at `x` and `y`, CSS pixels from its top left,
// having moved by `moved_x` and `moved_y`.
static void mouse_event(const char *type, const int x, const int y, const int moved_x, const int moved_y)
{
    char script[400];
    snprintf(script, sizeof script,
             "{ const c = document.querySelector('canvas'); const r = c.getBoundingClientRect();"
             " c.dispatchEvent(new MouseEvent('%s', {button: 0, clientX: r.left + %d, clientY: r.top + %d,"
             " movementX: %d, movementY: %d, bubbles: true})); }",
             type, x, y, moved_x, moved_y);
    tide_web_eval(script);
}

static void key_event(const char *type, const char *code, const bool repeat)
{
    char script[200];
    snprintf(script, sizeof script, "dispatchEvent(new KeyboardEvent('%s', {code: '%s', key: '%s', repeat: %s}))", type,
             code, code, repeat ? "true" : "false");
    tide_web_eval(script);
}

static bool asked(const int times)
{
    char script[64];
    snprintf(script, sizeof script, "lock.asks === %d", times);
    return tide_web_eval(script);
}

static int frame(void *user, const float seconds)
{
    (void)user, (void)seconds;
    static int step;
    static bool want; // Whether the game wants the lock, which it says every frame
    tide_platform_poll(&devices);
    const tide_mouse *m = &devices.mouse;
    const tide_float2 size = tide_platform_screen_size();
    int code = TIDE_KEEP_RUNNING;

    switch (step++) {
    case 0:
        stand_in_for_the_browser();
        check(!m->locked, "the cursor is free until the game asks");
        mouse_event("mousemove", 10, 20, 0, 0);
        want = true; // With no click or key press that counts
        break;
    case 1:
        check(asked(0), "wanting the lock asks nothing of the browser without a click or a key press");
        check(!m->locked, "and it isn't locked");
        mouse_event("mousedown", 10, 20, 0, 0);
        mouse_event("mouseup", 10, 20, 0, 0);
        break;
    case 2:
        check(asked(1), "a click on the canvas asks");
        check(tide_web_eval("lock.options && lock.options.unadjustedMovement === true"),
              "for the mouse's own movement, where the browser has it");
        check(!m->locked, "asking isn't having: the browser hasn't answered");
        check(m->position.x == 10.0f && m->position.y == size.y - 20.0f, "the mouse is where it clicked");
        browser_gives();
        break;
    case 3:
        check(m->locked, "it's locked once the browser says so");
        check(asked(1), "and nothing more is asked");
        // Its events say how it moved, and where the cursor was locked or anywhere else
        mouse_event("mousemove", 50, 60, 7, -3);
        break;
    case 4:
        check(m->delta.x == 7.0f && m->delta.y == 3.0f, "locked, the mouse's movement is its delta, y up");
        check(m->position.x == 10.0f && m->position.y == size.y - 20.0f, "and its position stays where it was");
        check(devices.pointer.position.x == 10.0f && devices.pointer.delta.x == 7.0f, "the pointer's too");
        mouse_event("mousemove", 50, 60, 500, 0);
        mouse_event("mousemove", 50, 60, 500, 0);
        break;
    case 5:
        check(m->delta.x == 1000.0f, "it goes on past the canvas, 64 pixels wide");
        check(m->position.x == 10.0f, "with the position still");
        // Escape: the browser takes the lock back, and some send the key too
        browser_takes_back();
        key_event("keydown", "Escape", false);
        key_event("keyup", "Escape", false);
        break;
    case 6:
        check(!m->locked, "the browser took the lock back, and the game is told");
        check(asked(1), "Escape doesn't ask for it again");
        check(m->delta.x == 0.0f && m->delta.y == 0.0f, "nothing moved");
        mouse_event("mousemove", 30, 40, 0, 0);
        break;
    case 7:
        check(m->position.x == 30.0f && m->position.y == size.y - 40.0f, "free, the mouse goes where the cursor does");
        check(m->delta.x == 20.0f && m->delta.y == -20.0f, "and its delta is how far");
        break;
    case 8:
    case 9:
    case 10:
    case 11:
        break; // Frames go by, the game still wanting the lock
    case 12:
        check(asked(1), "nothing asks again on its own, however many frames go by");
        check(!m->locked, "and it stays free");
        key_event("keydown", "KeyW", false);
        break;
    case 13:
        check(asked(2), "a key press asks");
        browser_refuses("SecurityError"); // As browsers do for a moment after Escape
        break;
    case 14:
        check(!m->locked && asked(2), "refused, it isn't locked, and isn't asked for again");
        key_event("keydown", "KeyW", true); // The key, still held, repeats
        break;
    case 15:
    case 16:
    case 17:
        break;
    case 18:
        check(asked(2), "a held key's repeats don't ask, nor do frames going by");
        key_event("keyup", "KeyW", false);
        mouse_event("mousedown", 30, 40, 0, 0);
        mouse_event("mouseup", 30, 40, 0, 0);
        break;
    case 19:
        check(asked(3), "the next click asks");
        browser_gives();
        break;
    case 20:
        check(m->locked, "and it's locked again");
        want = false; // The game stops asking: its pause menu is up
        break;
    case 21:
        check(tide_web_eval("lock.exits === 1"), "the game stops wanting it, and the page lets the cursor go");
        check(!m->locked, "so it's free");
        mouse_event("mousedown", 30, 40, 0, 0);
        mouse_event("mouseup", 30, 40, 0, 0);
        break;
    case 22:
        check(asked(3), "a click asks nothing while the game doesn't want the lock");
        // Wanted again right after a click, like a Resume button's: that click still counts
        tide_web_eval("lock.active = true");
        want = true;
        break;
    case 23:
        check(asked(4), "wanted while a click still counts, it's asked for at once");
        browser_refuses("NotSupportedError"); // A system with no movement of the mouse's own
        break;
    case 24:
        check(asked(5) && tide_web_eval("lock.options === undefined"), "refused the mouse's own movement, it asks for the usual");
        want = false; // The game changes its mind before the browser answers
        break;
    case 25:
        browser_gives();
        break;
    case 26:
        check(tide_web_eval("lock.exits === 2"), "a lock given after the game stopped wanting it is let go");
        check(!m->locked, "and never read as locked");
        want = true;
        break;
    case 27:
        check(asked(6), "wanted again, it's asked for");
        browser_gives();
        break;
    case 28:
        check(m->locked, "and given");
        // The page is hidden: another tab in front
        tide_web_eval("Object.defineProperty(document, 'hidden', { get: () => true, configurable: true })");
        break;
    case 29:
        check(!m->locked, "a hidden page isn't locked");
        tide_web_eval("delete document.hidden");
        browser_takes_back(); // As browsers do then
        tide_web_eval("lock.active = false");
        break;
    default:
        check(!m->locked && asked(6), "back in front, it waits for a click or a key press");
        code = failures == 0 ? 0 : 1;
        break;
    }
    tide_platform_cursor_lock(want);
    tide_devices_consume(&devices);
    return code;
}

int main(void)
{
    tide_platform_open(&(tide_window_desc){.title = "web cursor", .width = 64, .height = 64, .hidden = true});
    tide_platform_run(frame, NULL);
}
