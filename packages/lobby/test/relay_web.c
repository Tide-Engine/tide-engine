// The lobby package against the relay's lobbies, in a page: the page's fetch
// (platform/web/tide.js) against the server that serves the page
// (cmake/run_web_served_test.mjs with --routes relay_routes.mjs), whose port
// the page's address says, and the program gets as --port=<port>.
// relay_check.h has the players and what they do.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/platform.h"

// Frames a step may take: a page's frames are a display's, so this is most
// of a minute, and the runner's own limit is under two.
#define LOBBY_WAIT_FRAMES 3000
#include "relay_check.h"

static int frame(void *user, const float seconds)
{
    (void)user;
    const int result = lobby_relay_step(seconds);
    if (result == 0) return TIDE_KEEP_RUNNING;
    printf(result == 1 ? "lobby: every step as expected\n" : "lobby: %d checks failed, or a step was never reached\n", lobby_failures);
    for (int i = 0; i < 4; i++) seat_free(&seats[i]);
    return result == 1 ? 0 : 1;
}

int main(int argc, char **argv)
{
    const char *port = NULL;
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--port=", 7) == 0) port = argv[i] + 7;
    }
    if (!port) {
        fprintf(stderr, "lobby: --port=<the server's port> is missing from the page's address\n");
        return 2;
    }
    const tide_io *io = tide_platform_io_init("tests.tide.lobby");
    char origin[64];
    snprintf(origin, sizeof origin, "http://127.0.0.1:%s", port);
    for (int i = 0; i < 4; i++) seat_init(&seats[i], io, origin);
    tide_platform_open(&(tide_window_desc){.title = "lobby", .width = 64, .height = 64, .hidden = true});
    tide_platform_run(frame, NULL);
}
