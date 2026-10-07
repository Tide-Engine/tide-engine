// Desktop only: a window holds the cursor only while it has the focus.
//
// This window is hidden, as every test's is, so it never has the focus: the
// game asks for the lock, frame after frame, and the platform says it isn't
// locked, as it does for a game the player went away from with Alt+Tab. When a
// window with the focus holds the cursor, cursor_rules.c checks. What it does
// with one it holds is GLFW's, which nothing here can check without taking the
// screen and the mouse from whoever is at them.

#include <stdio.h>

#include "tide/platform.h"

static tide_devices devices;
static int failures;

static void check(const bool ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) failures++;
}

static int frame(void *user, const float seconds)
{
    (void)user, (void)seconds;
    static int step;
    tide_platform_poll(&devices);
    int code = TIDE_KEEP_RUNNING;
    bool want = true;

    switch (step++) {
    case 0:
        check(!devices.mouse.locked, "the cursor is free until the game asks");
        break;
    case 1:
    case 2:
    case 3:
        check(!devices.mouse.locked, "asked for, a window without the focus doesn't hold the cursor");
        break;
    case 4:
        want = false;
        break;
    default:
        check(!devices.mouse.locked, "nor once the game stops asking");
        want = false;
        code = failures == 0 ? 0 : 1;
        break;
    }
    tide_platform_cursor_lock(want);
    tide_devices_consume(&devices);
    return code;
}

int main(void)
{
    tide_platform_open(&(tide_window_desc){.title = "cursor", .width = 64, .height = 64, .hidden = true});
    tide_platform_run(frame, NULL);
}
