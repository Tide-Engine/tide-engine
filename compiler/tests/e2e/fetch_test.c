// await Web.Fetch(url) natively: the platform's provider (platform/src/fetch.c,
// an HTTP client of our own on a thread of its own) against a server on this
// machine, which fetch.mjs starts and says the port of (TIDE_FETCH_PORT).
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/platform.h"
#include "tide/time.h"
#include "tide_test.h"
#include "fetch_check.h"

static void frame(tide_local *local)
{
    tide_local_frame_time(local, 1.0f / 60.0f);
    tide_draw_list draw = {0};
    tide_gui gui = {0};
    tide_frame(NULL, NULL, 1, NULL, NULL, 1.0f, local, &draw, &gui);
    tide_draw_free(&draw);
    tide_draw_free(&gui.list);
}

TIDE_TEST(fetch_each_case_against_a_server_on_this_machine)
{
    const char *port = getenv("TIDE_FETCH_PORT");
    TIDE_REQUIRE(port && *port);
    const tide_io *io = tide_platform_io_init("tests.tide.fetch", NULL);
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
    const tide_io *io = tide_platform_io_init("tests.tide.fetch", NULL);
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

// A provider that only notes the time limit it's handed
static float limit_given;

static uint32_t note_limit(void *user, const char *method, const char *url, const char *headers, const uint8_t *body,
                           const uint32_t size, const float timeout)
{
    (void)user;
    (void)method;
    (void)url;
    (void)headers;
    (void)body;
    (void)size;
    limit_given = timeout;
    return 7;
}

TIDE_TEST(fetch_time_limits_reach_a_provider_as_seconds_or_none)
{
    const tide_io fake = {.request_begin = note_limit};
    const tide_str url = tide_str_from_cstr("http://example.com/");
    const tide_str method = tide_str_from_cstr("GET");
    const tide_str headers = tide_str_from_cstr("");
    const tide_list body = {0};
    // 0, less and no number are no limit, and none is longer than the longest
    static const float given[] = {5.0f, 0.25f, 0.0f, -1.0f, NAN, -INFINITY, INFINITY, 2000000.0f};
    static const float want[] = {5.0f, 0.25f, 0.0f, 0.0f, 0.0f, 0.0f, TIDE_REQUEST_TIMEOUT_MOST, TIDE_REQUEST_TIMEOUT_MOST};
    for (size_t i = 0; i < sizeof given / sizeof given[0]; i++) {
        limit_given = -2.0f;
        TIDE_CHECK(tide_web_request_begin(&fake, url, method, headers, body, given[i]) == 7);
        TIDE_CHECK(limit_given == want[i]);
    }
}

TIDE_TEST(fetch_a_request_past_its_time_limit_is_timed_out_where_its_polled)
{
    const char *port = getenv("TIDE_FETCH_PORT");
    TIDE_REQUIRE(port && *port);
    const tide_io *io = tide_platform_io_init("tests.tide.fetch", NULL);
    char url[64];
    snprintf(url, sizeof url, "http://127.0.0.1:%s/silent", port);
    const tide_list body = {0};
    const uint64_t began = tide_time_now_ns();
    const uint32_t ticket = tide_web_request_begin(io, tide_str_from_cstr(url), tide_str_from_cstr("GET"), tide_str_from_cstr(""),
                                                   body, 0.2f);
    TIDE_REQUIRE(ticket && ticket != TIDE_FETCH_INVALID_URL && ticket != TIDE_REQUEST_INVALID);
    tide_web_answer answer;
    TIDE_CHECK(!tide_web_request_poll(io, ticket, &answer)); // Not before its limit
    while (!tide_web_request_poll(io, ticket, &answer)) tide_yield();
    TIDE_CHECK(!answer.ok && answer.error == TIDE_IO_TIMED_OUT && answer.status == 0);
    TIDE_CHECK(tide_time_now_ns() - began >= 200000000ull); // The limit itself, and nothing sooner
    tide_web_fetch_forget(io, ticket); // As a task does once it has its answer: the ticket is gone either way
    // The same server with no limit goes on, until it's forgotten
    const uint32_t unlimited = tide_web_request_begin(io, tide_str_from_cstr(url), tide_str_from_cstr("GET"),
                                                      tide_str_from_cstr(""), body, 0.0f);
    TIDE_REQUIRE(unlimited);
    for (int i = 0; i < 1000; i++) TIDE_CHECK(!tide_web_request_poll(io, unlimited, &answer));
    tide_web_fetch_forget(io, unlimited);
}
