// await Web.Fetch(url) in a page: the page's fetch (platform/web/tide.js)
// against the server that serves the page (cmake/run_web_served_test.mjs with
// --routes platform/tests/fetch_routes.mjs), whose port the page's address
// says, and the program gets as --port=<port>.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/platform.h"
#include "fetch_check.h"

static tide_local local;

static int frame(void *user, const float seconds)
{
    (void)user;
    tide_local_frame_time(&local, seconds);
    tide_draw_list draw = {0};
    tide_gui gui = {0};
    tide_frame(NULL, NULL, 1, NULL, NULL, 1.0f, &local, &draw, &gui);
    tide_draw_free(&draw);
    tide_draw_free(&gui.list);
    if (!local.Fetches.done) return TIDE_KEEP_RUNNING;
    const int failures = check_fetches(&local);
    printf(failures ? "fetch: %d checks failed\n" : "fetch: every case as expected\n", failures);
    tide_local_free(&local);
    return failures ? 1 : 0;
}

int main(int argc, char **argv)
{
    const char *port = NULL;
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--port=", 7) == 0) port = argv[i] + 7;
    }
    if (!port) {
        fprintf(stderr, "fetch: --port=<the server's port> is missing from the page's address\n");
        return 2;
    }
    const tide_io *io = tide_platform_io_init("tests.tide.fetch");
    tide_local_set_io(&local, io);
    tide_local_init(&local);
    tide_text_use(NULL, &local.heap);
    char origin[64];
    snprintf(origin, sizeof origin, "http://127.0.0.1:%s", port);
    tide_text_set(&local.Fetches.origin, tide_str_from_cstr(origin), TIDE_IN_LOCAL);
    tide_platform_open(&(tide_window_desc){.title = "fetch", .width = 64, .height = 64, .hidden = true});
    tide_platform_run(frame, NULL);
}
