// Desktop only: hidden windows never hold the cursor, regardless of focus.
// The pure rules test covers focused/visible windows without taking the
// screen or mouse from whoever is using them.

#include <stdio.h>

#include "tide/platform.h"

// GLFW's actual cursor mode, without bringing its header into the host API.
typedef struct GLFWwindow GLFWwindow;
GLFWwindow *tide_glfw_window(void);
int glfwGetInputMode(GLFWwindow *window, int mode);
#define GLFW_CURSOR 0x00033001
#define GLFW_CURSOR_NORMAL 0x00034001

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
        check(!devices.mouse.locked, "asked for, a hidden window doesn't hold the cursor");
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
    check(glfwGetInputMode(tide_glfw_window(), GLFW_CURSOR) == GLFW_CURSOR_NORMAL,
          "a hidden window leaves GLFW's actual cursor mode normal");
    tide_devices_consume(&devices);
    return code;
}

int main(void)
{
    tide_platform_open(&(tide_window_desc){.title = "cursor", .width = 64, .height = 64, .hidden = true});
    tide_platform_run(frame, NULL);
}
