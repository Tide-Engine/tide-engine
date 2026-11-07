// Android only: an app that opens its window and ends, through exit() as
// every app does (android/activity.c's __wrap_exit). On a device with Vulkan
// 1.3 it ends at once; on one without, after the screen saying what the game
// needs, which the test's back button leaves. Either way with 0, and
// run_android.mjs --app fails it if anything in its process crashed as it
// ended (until 2026-11-05, Android's own HWUI threads aborted at every app's
// exit).

#include <stdio.h>

#include "window.h"

int main(void)
{
    const tide_window_desc desc = {.title = "exit", .width = 64, .height = 64};
    if (!tide_window_open(&desc)) {
        printf("app exit: left the screen saying what's needed\n");
        return 0;
    }
    printf("app exit: opened Vulkan on the window\n");
    tide_window_close();
    return 0;
}
