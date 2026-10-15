#include "local_events_same.h"

// A game whose match sends events only local code handles: its world, and
// what its machines send, are those of the same game without them
// (local_events_same.h), whether a machine listens or not.

static tide_world world;
static tide_world base;
static tide_local local;
static tide_event_log events;
static tide_draw_list draw;
static tide_gui gui;

static void tick_quiet(tide_world *w)
{
    tide_world_tick(w);
}

// Recording what the tick sent, and hearing it
static void tick_heard(tide_world *w)
{
    tide_world_tick_events(w, NULL, &events);
    tide_local_events(&local, &events);
    tide_draw_reset(&draw);
    tide_frame(w, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
}

TIDE_TEST(local_events_cost_the_world_nothing)
{
    same_match_check(&world, &base, tick_quiet);
    tide_local_init(&local);
    same_match_check(&world, &base, tick_heard);
    // Every bounce was heard, and a push for each tick the second player pushed
    TIDE_CHECK(local.Displayed.bounces == world.Field.bounces && local.Displayed.sure == local.Displayed.bounces);
    TIDE_CHECK(local.Displayed.pushes == 38); // From the tick after the players joined
    tide_event_log_free(&events);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_world_free(&base);
}

static void frame(const tide_client *first)
{
    const tide_view_worlds view = tide_client_view(first);
    tide_local_events(&local, &events);
    tide_draw_reset(&draw);
    tide_frame(view.current, view.previous, view.alpha, view.verified, view.verified_previous, view.verified_alpha, &local, &draw, &gui);
}

TIDE_TEST(local_events_cost_the_network_nothing)
{
    same_traffic_check(NULL, NULL); // Nobody listens
    tide_local_init(&local);
    events = (tide_event_log){.taken = true};
    same_traffic_check(&events, frame); // The first player's machine does
    // It heard the bounces, each once, though it ran their ticks again, and
    // most of them confirmed (the last ones the server hadn't yet)
    TIDE_CHECK(local.Displayed.bounces > 6 && local.Displayed.sure > 4 && local.Displayed.sure <= local.Displayed.bounces);
    TIDE_CHECK(local.Displayed.pushes > 0);
    tide_event_log_free(&events);
    tide_local_free(&local);
}
