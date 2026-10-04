// Web only: the standard host's debug views (tide/host.h), in its own loop.
// F7 shows the profiler where they're on, by the command line (the page's
// address has `debug-views`) or by the game's debugViews setting, and hides it
// again; where they're off, it does nothing. The page draws with Tide.checkGL,
// so the panel also has to draw without a mistake.
//
// Frames are a 60th of a second each, whatever the page's clock says, so the
// profiler takes stock after the same number of them every run.

#include <stdio.h>
#include <string.h>

#include "game.h"
#include "tide/run.h"
#include "tide_web.h"

static int failures;
static int step;
static int wait;     // Frames to run before the next step
static int patience; // Frames spent waiting for the profiler to take stock
static bool from_arguments;
static tide_game game_with_setting;
static tide_host_game host_with_setting;

static void check(const bool ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) failures++;
}

static void f7(const char *type)
{
    char script[120];
    snprintf(script, sizeof script, "dispatchEvent(new KeyboardEvent('%s', {code: 'F7', key: 'F7', keyCode: 118}))", type);
    tide_web_eval(script);
}

// Whether the last frame drew `text`, as one of its texts.
static bool drew(const char *text)
{
    for (uint32_t i = 0; i < tide_run_draw.count; i++) {
        const tide_draw_command *c = &tide_run_draw.commands[i];
        if (c->kind == TIDE_DRAW_TEXT && strcmp(tide_run_draw.text + c->text, text) == 0) return true;
    }
    return false;
}

// A key sent after a frame is in the devices at the next one's poll, which
// the frame after that reads: a step checks what its key did three frames on.
static int frame(void *user, const float seconds)
{
    (void)seconds;
    const int code = tide_run_frame(user, 1.0f / 60.0f);
    if (code != TIDE_KEEP_RUNNING) return code;
    if (wait > 0) {
        wait--;
        return TIDE_KEEP_RUNNING;
    }
    const tide_profile_times *systems = &tide_run_profiler.profile.systems;
    const tide_profile_times *views = &tide_run_profiler.profile.views;
    switch (step++) {
    case 0: // Off: no setting, and as if the command line said nothing
        check(from_arguments, "--debug-views on the command line turns the debug views on");
        tide_run_settings.debug_views = false;
        f7("keydown");
        wait = 2;
        break;
    case 1:
        check(!tide_run_profiling && !drew("measuring"), "with the debug views off, F7 shows nothing");
        f7("keyup");
        tide_run_settings.debug_views = from_arguments; // As the command line said
        wait = 2;
        break;
    case 2:
        f7("keydown");
        wait = 2;
        break;
    case 3:
        check(tide_run_profiling && drew("measuring"), "with them on, F7 shows the profiler");
        check(tide_run_gui.profile == &tide_run_profiler.profile, "and the views are given its profile to fill");
        f7("keyup");
        break;
    case 4: // Half a second of frames, and it takes stock
        if (!drew("Tick") && ++patience < 600) {
            step--;
            break;
        }
        check(drew("Frame") && drew("Tick") && drew("Ticks"), "the profiler says where a frame and a tick went");
        // The page's clock stands still while a test's frame runs (virtual
        // time), so nothing took any time, and what took none has no row:
        // the game's system and view are in the profile, by name
        check(systems->count == 1 && strcmp(systems->items[0].name, "Drift") == 0,
              "the game's ticks said how long its system took, by name");
        check(views->count == 1 && strcmp(views->items[0].name, "Dots") == 0, "and its frames, its view");
        f7("keydown");
        wait = 2;
        break;
    case 5:
        check(!tide_run_profiling && !drew("Frame") && !tide_run_gui.profile, "F7 again hides it, and nothing is measured");
        f7("keyup");
        wait = 2;
        break;
    case 6: // A build of the game with the setting, as a reload brings one
        tide_run_settings.debug_views = false;
        tide_run_game = &host_with_setting;
        f7("keydown");
        wait = 2;
        break;
    case 7:
        check(tide_run_profiling && drew("measuring"), "the game's debugViews setting turns them on too");
        f7("keyup");
        tide_run_game = &tide_host_game_api; // ...and one without it again
        wait = 2;
        break;
    default:
        check(!tide_run_profiling && !drew("measuring"), "and a build without it hides what was shown");
        return failures == 0 ? 0 : 1;
    }
    return TIDE_KEEP_RUNNING;
}

int main(const int argc, char **argv)
{
    game_with_setting = tide_game_api;
    game_with_setting.debug_views = true;
    host_with_setting = tide_host_game_api;
    host_with_setting.game = &game_with_setting;

    tide_run_game = &tide_host_game_api;
    tide_run_configure(&(tide_run_desc){.title = "debug views", .argc = argc, .argv = argv});
    from_arguments = tide_run_settings.debug_views;
    tide_platform_open(&(tide_window_desc){.title = tide_run_settings.title,
                                           .width = tide_run_settings.width,
                                           .height = tide_run_settings.height,
                                           .hidden = true});
    tide_run_begin();
    tide_platform_run(frame, NULL);
}
