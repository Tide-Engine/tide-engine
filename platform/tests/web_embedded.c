// Web only: a game that's part of a page that scrolls (`embedded` in its page's
// address, platform/web/tide.js) leaves that page its scrolling.
//
// The wheel is the page's. So is a finger, until one taps the game: the
// program hears of a tap once it lifted, and never of a finger the page
// scrolled with. From the tap on, fingers are the game's as they land, until
// the page takes the focus back. Each frame checks the events sent at the end
// of the previous one, then samples the devices, as a tick would.

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

// A pointer event on the canvas, `x` and `y` CSS pixels from its top left.
static void pointer_event(const char *type, const int id, const int x, const int y)
{
    char script[400];
    snprintf(script, sizeof script,
             "{ const c = document.querySelector('canvas'); const r = c.getBoundingClientRect();"
             " c.dispatchEvent(new PointerEvent('%s', {pointerId: %d, pointerType: 'touch', clientX: r.left + %d,"
             " clientY: r.top + %d, bubbles: true, cancelable: true})); }",
             type, id, x, y);
    tide_web_eval(script);
}

// Whether the canvas kept an event of this kind from the browser, which then
// does nothing of its own with it: no scrolling.
static bool kept(const char *event)
{
    char script[300];
    snprintf(script, sizeof script,
             "(() => { const e = %s; document.querySelector('canvas').dispatchEvent(e); return e.defaultPrevented; })()",
             event);
    return tide_web_eval(script);
}

static bool scrolls(void)
{
    return tide_web_eval("document.querySelector('canvas').style.touchAction === 'pan-y pinch-zoom'");
}

static bool takes_gestures(void)
{
    return tide_web_eval("document.querySelector('canvas').style.touchAction === 'none'");
}

#define WHEEL "new WheelEvent('wheel', {deltaY: 120, bubbles: true, cancelable: true})"
#define TOUCH_START "new Event('touchstart', {bubbles: true, cancelable: true})"

static int frame(void *user, const float seconds)
{
    (void)user, (void)seconds;
    static int step;
    tide_platform_poll(&devices);
    const tide_touch *first = &devices.touchscreen.touches.at[0];
    int code = TIDE_KEEP_RUNNING;

    switch (step++) {
    case 0:
        check(scrolls(), "a finger that swipes up or down the canvas scrolls the page");
        check(!kept(WHEEL), "the wheel is left to the page");
        check(!kept(TOUCH_START), "and so is a finger that lands");
        pointer_event("pointerdown", 5, 10, 20);
        break;
    case 1:
        check(devices.mouse.scroll.x == 0.0f && devices.mouse.scroll.y == 0.0f, "the program hears of no wheel");
        check(!first->press.held && first->id == 0, "nor yet of the finger: the page may scroll with it");
        pointer_event("pointercancel", 5, 10, 60);
        break;
    case 2:
        check(!first->press.held && !first->press.up && first->id == 0, "one the page scrolled with never touched the game");
        // A tap, which went a little sideways before it lifted.
        pointer_event("pointerdown", 6, 30, 30);
        pointer_event("pointermove", 6, 32, 30);
        pointer_event("pointermove", 6, 34, 30);
        pointer_event("pointerup", 6, 34, 30);
        break;
    case 3:
        check(first->press.held && first->press.down && first->startPosition.x == 30.0f,
              "a tap is heard of once it lifted, from where it landed");
        check(takes_gestures(), "and the game has the touchscreen: the canvas takes no gesture of the page's");
        check(tide_web_eval("document.activeElement === document.querySelector('canvas')"), "with the focus");
        break;
    case 4:
        check(!first->press.held && first->press.up, "the tap lifts");
        pointer_event("pointerdown", 7, 40, 40);
        break;
    case 5:
        check(first->press.held && first->press.down && first->position.x == 40.0f,
              "from then a finger is the game's as it lands");
        check(kept(TOUCH_START), "and the page scrolls with none");
        check(!kept(WHEEL), "but with the wheel, still");
        pointer_event("pointerup", 7, 40, 40);
        // A press on the page elsewhere takes the focus from the game's frame.
        tide_web_eval("window.dispatchEvent(new Event('blur'))");
        break;
    case 6:
        check(scrolls(), "a press elsewhere in the page gives it its scrolling back");
        pointer_event("pointerdown", 8, 50, 50);
        break;
    case 7:
        check(!first->press.held, "and a finger waits to lift again");
        pointer_event("pointercancel", 8, 50, 90);
        // The page can say otherwise as the game runs: a game that's the whole page.
        tide_web_eval("Tide.embedded = false");
        check(takes_gestures(), "no longer embedded, the canvas takes every gesture");
        check(kept(WHEEL), "and the wheel");
        pointer_event("pointerdown", 9, 60, 60);
        break;
    case 8:
        check(devices.mouse.scroll.y == -1.0f, "which the program hears of");
        check(first->press.held && first->press.down, "with every finger, as it lands");
        code = failures == 0 ? 0 : 1;
        break;
    }
    tide_devices_consume(&devices);
    return code;
}

int main(void)
{
    tide_platform_open(&(tide_window_desc){.title = "web embedded", .width = 64, .height = 64, .hidden = true});
    tide_platform_run(frame, NULL);
}
