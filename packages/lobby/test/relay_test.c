// The lobby package against a relay on this machine, natively: the
// platform's HTTP client (platform/src/fetch.c) and the relay relay.mjs
// starts, whose port it says in TIDE_LOBBY_PORT. relay_check.h has the players
// and what they do.

#if !defined(_WIN32) && !defined(__APPLE__)
#define _DEFAULT_SOURCE // nanosleep under strict C
#endif

#include <stdlib.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif

#include "tide/platform.h"
#include "tide_test.h"

// Frames a step may take: each lets a millisecond pass, so the relay's
// answers, which come on threads of their own, have a minute to.
#define LOBBY_WAIT_FRAMES 4000
#include "relay_check.h"

// Lets the answers' threads run between two frames. Nothing waits on how
// long it is: a step goes on when what it asked for is here.
static void breathe(void)
{
#ifdef _WIN32
    Sleep(1);
#else
    nanosleep(&(struct timespec){.tv_nsec = 1000000}, NULL);
#endif
}

TIDE_TEST(lobby_players_meet_through_a_relay_on_this_machine)
{
    const char *port = getenv("TIDE_LOBBY_PORT");
    TIDE_REQUIRE(port && *port);
    const tide_io *io = tide_platform_io_init("tests.tide.lobby");
    char origin[64];
    snprintf(origin, sizeof origin, "http://127.0.0.1:%s", port);
    for (int i = 0; i < 4; i++) seat_init(&seats[i], io, origin);
    int result = 0;
    while (result == 0) {
        result = lobby_relay_step(1.0f / 60.0f);
        breathe();
    }
    TIDE_CHECK(result == 1 && lobby_failures == 0);
    for (int i = 0; i < 4; i++) seat_free(&seats[i]);
}
