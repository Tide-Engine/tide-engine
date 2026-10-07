// await Web.Fetch(url) natively: the platform's provider (platform/src/fetch.c,
// an HTTP client of our own on a thread of its own) against a server on this
// machine, which fetch.mjs starts and says the port of (TIDE_FETCH_PORT).
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/platform.h"
#include "tide_test.h"
#include "fetch_check.h"

static void frame(tide_local *local)
{
    tide_local_frame_time(local, 1.0f / 60.0f);
    tide_draw_list draw = {0};
    tide_gui gui = {0};
    tide_frame(NULL, NULL, 1, NULL, local, &draw, &gui);
    tide_draw_free(&draw);
    tide_draw_free(&gui.list);
}

TIDE_TEST(fetch_each_case_against_a_server_on_this_machine)
{
    const char *port = getenv("TIDE_FETCH_PORT");
    TIDE_REQUIRE(port && *port);
    const tide_io *io = tide_platform_io_init("tests.tide.fetch");
    tide_local local = {0};
    tide_local_set_io(&local, io);
    tide_local_init(&local);
    tide_text_use(NULL, &local.heap);
    char origin[64];
    snprintf(origin, sizeof origin, "http://127.0.0.1:%s", port);
    tide_text_set(&local.Fetches.origin, tide_str_from_cstr(origin), TIDE_IN_LOCAL);
    // Frames go on while the fetches do: the task polls each one once a frame
    while (!local.Fetches.done) frame(&local);
    TIDE_CHECK(check_fetches(&local) == 0);
    tide_local_free(&local);
}

TIDE_TEST(fetch_tickets_for_what_isnt_a_url_and_for_one_forgotten_in_flight)
{
    const char *port = getenv("TIDE_FETCH_PORT");
    TIDE_REQUIRE(port && *port);
    const tide_io *io = tide_platform_io_init("tests.tide.fetch");
    // Not a URL: refused by the engine, before any provider is asked
    const uint32_t bad = tide_web_fetch_begin(io, tide_str_from_cstr("file:///etc/passwd"));
    TIDE_CHECK(bad == TIDE_FETCH_INVALID_URL);
    tide_io_list_result result;
    TIDE_CHECK(tide_web_fetch_poll(io, bad, &result) && !result.ok && result.error == TIDE_IO_INVALID_PATH);
    TIDE_CHECK(tide_web_fetch_begin(io, tide_str_from_cstr("")) == TIDE_FETCH_INVALID_URL);
    TIDE_CHECK(tide_web_fetch_begin(io, tide_str_from_cstr("http://")) == TIDE_FETCH_INVALID_URL);
    // One that's going when nobody will poll it again (local state that goes):
    // forgotten, which stops it; nothing waits for that
    char url[64];
    snprintf(url, sizeof url, "http://127.0.0.1:%s/endless", port);
    const uint32_t ticket = tide_web_fetch_begin(io, tide_str_from_cstr(url));
    TIDE_REQUIRE(ticket && ticket != TIDE_FETCH_INVALID_URL);
    tide_web_fetch_forget(io, ticket);
    // A ticket nobody has is a fetch that failed
    TIDE_CHECK(tide_web_fetch_poll(io, ticket + 1000u, &result) && result.error == TIDE_IO_FAILED);
}
