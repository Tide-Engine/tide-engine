#include "game.h"
#include "tide_test.h"

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

// The Main scene is slot 0, and its Setup spawns these in order.
static const tide_entity FIRST = {1, 1};  // A body owned by player 0, at x = 1
static const tide_entity SECOND = {2, 1}; // A body owned by player 1, at x = 2
// x = 3: a body with no Owner. x = 4: one whose Owner is no player. Then player 0's flag.

static void start(void)
{
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
}

// A frame on the machine of player `index`.
static void frame_between(const tide_world *previous, const float alpha, const int32_t index)
{
    tide_local_set_session(&local, TIDE_SESSION_CONNECTED, tide_player_from_index(index), 0, false, false, "");
    local.Tally.mine = 0;
    local.Tally.rest = 0;
    tide_draw_reset(&draw);
    tide_frame(&world, previous, alpha, NULL, NULL, 1.0f, &local, &draw, &gui);
}

static void frame_as(const int32_t index)
{
    frame_between(NULL, 1.0f, index);
}

// How many commands of `kind` the frame drew, and whether one is at x.
static uint32_t count(const uint32_t kind)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < draw.count; i++) n += draw.commands[i].kind == kind;
    return n;
}

static const tide_draw_command *drawn(const uint32_t kind, const float x)
{
    for (uint32_t i = 0; i < draw.count; i++) {
        if (draw.commands[i].kind == kind && draw.commands[i].a.x == x) return &draw.commands[i];
    }
    return NULL;
}

TIDE_TEST(ownership_with_runs_for_this_players_entities)
{
    start();
    frame_as(0);
    TIDE_CHECK(count(TIDE_DRAW_CIRCLE) == 1 && drawn(TIDE_DRAW_CIRCLE, 1.0f));
    frame_as(1);
    TIDE_CHECK(count(TIDE_DRAW_CIRCLE) == 1 && drawn(TIDE_DRAW_CIRCLE, 2.0f));
    frame_as(2); // A player who owns nothing
    TIDE_CHECK(count(TIDE_DRAW_CIRCLE) == 0);
}

TIDE_TEST(ownership_without_runs_for_every_other_entity)
{
    start();
    // Another player's, nobody's, and the one with no Owner
    frame_as(0);
    TIDE_CHECK(count(TIDE_DRAW_RECT) == 3);
    TIDE_CHECK(drawn(TIDE_DRAW_RECT, 2.0f) && drawn(TIDE_DRAW_RECT, 3.0f) && drawn(TIDE_DRAW_RECT, 4.0f));
    frame_as(1);
    TIDE_CHECK(count(TIDE_DRAW_RECT) == 3);
    TIDE_CHECK(drawn(TIDE_DRAW_RECT, 1.0f) && drawn(TIDE_DRAW_RECT, 3.0f) && drawn(TIDE_DRAW_RECT, 4.0f));
    frame_as(2);
    TIDE_CHECK(count(TIDE_DRAW_RECT) == 4);
}

TIDE_TEST(ownership_beside_other_filters)
{
    start();
    // `with Owner, without ownership`: an Owner, and not this player
    frame_as(0);
    TIDE_CHECK(count(TIDE_DRAW_WIRE_CIRCLE) == 2);
    TIDE_CHECK(drawn(TIDE_DRAW_WIRE_CIRCLE, 2.0f) && drawn(TIDE_DRAW_WIRE_CIRCLE, 4.0f));
    frame_as(2);
    TIDE_CHECK(count(TIDE_DRAW_WIRE_CIRCLE) == 3 && !drawn(TIDE_DRAW_WIRE_CIRCLE, 3.0f));

    // `with ownership, without Hidden`: a sparse component's rows too
    frame_as(1);
    TIDE_CHECK(count(TIDE_DRAW_LINE) == 1 && drawn(TIDE_DRAW_LINE, 2.0f));
    world.Trick.hide = true;
    tide_world_tick(&world);
    TIDE_REQUIRE(tide_has_Hidden(&world, SECOND));
    frame_as(1);
    TIDE_CHECK(count(TIDE_DRAW_LINE) == 0);
    TIDE_CHECK(count(TIDE_DRAW_CIRCLE) == 1); // Still its own
    frame_as(0);
    TIDE_CHECK(count(TIDE_DRAW_LINE) == 1 && drawn(TIDE_DRAW_LINE, 1.0f));
}

TIDE_TEST(ownership_beside_the_owner_as_a_parameter)
{
    start();
    frame_as(0);
    TIDE_REQUIRE(count(TIDE_DRAW_WIRE_RECT) == 1 && drawn(TIDE_DRAW_WIRE_RECT, 1.0f));
    TIDE_CHECK(drawn(TIDE_DRAW_WIRE_RECT, 1.0f)->b.x == 10.0f);
    frame_as(1);
    TIDE_REQUIRE(count(TIDE_DRAW_WIRE_RECT) == 1 && drawn(TIDE_DRAW_WIRE_RECT, 2.0f));
    TIDE_CHECK(drawn(TIDE_DRAW_WIRE_RECT, 2.0f)->b.x == 20.0f);
}

TIDE_TEST(ownership_alone_runs_once_for_each_entity)
{
    start();
    // Six entities: four bodies, player 0's flag, and the scene
    frame_as(0);
    TIDE_CHECK(local.Tally.mine == 2 && local.Tally.rest == 4);
    frame_as(1);
    TIDE_CHECK(local.Tally.mine == 1 && local.Tally.rest == 5);
    frame_as(2);
    TIDE_CHECK(local.Tally.mine == 0 && local.Tally.rest == 6);
}

TIDE_TEST(ownership_follows_the_owner)
{
    start();
    frame_as(1);
    TIDE_CHECK(count(TIDE_DRAW_CIRCLE) == 1);
    // The first body changes hands
    TIDE_REQUIRE(tide_set_Owner(&world, FIRST, (Owner){.player = tide_player_from_index(1)}));
    frame_as(1);
    TIDE_CHECK(count(TIDE_DRAW_CIRCLE) == 2 && drawn(TIDE_DRAW_CIRCLE, 1.0f) && drawn(TIDE_DRAW_CIRCLE, 2.0f));
    TIDE_CHECK(count(TIDE_DRAW_RECT) == 2);
    frame_as(0);
    TIDE_CHECK(count(TIDE_DRAW_CIRCLE) == 0 && count(TIDE_DRAW_RECT) == 4);
    TIDE_CHECK(local.Tally.mine == 1); // Its flag
}

TIDE_TEST(ownership_views_blend_like_any_other)
{
    start();
    static tide_world before;
    tide_world_copy(&before, &world);
    tide_world_tick(&world); // Drift: y from 1 to 2
    frame_between(&before, 0.5f, 0);
    TIDE_REQUIRE(count(TIDE_DRAW_CIRCLE) == 1 && drawn(TIDE_DRAW_CIRCLE, 1.0f));
    TIDE_CHECK(drawn(TIDE_DRAW_CIRCLE, 1.0f)->a.y == 1.5f);
    TIDE_CHECK(count(TIDE_DRAW_RECT) == 3);
    tide_world_free(&before);
}

TIDE_TEST(ownership_views_wait_for_a_match)
{
    start();
    tide_local_set_session(&local, TIDE_SESSION_OFFLINE, (tide_player_id){0}, 0, false, false, "");
    tide_draw_reset(&draw);
    tide_frame(NULL, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    TIDE_CHECK(draw.count == 0);
    TIDE_CHECK(local.Tally.mine == 0 && local.Tally.rest == 0);
}

// `ownership` as a singleton's name, a field's, a parameter's and a local's.
TIDE_TEST(ownership_is_a_name_everywhere_else)
{
    start();
    TIDE_CHECK(local.ownership.ownership == 3);
    frame_as(0);
    TIDE_CHECK(local.Tally.share == 15);
}
