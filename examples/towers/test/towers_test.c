// Towers with no window: what the game promises, on one world; what a machine
// shows of it and where its lobby leaves it, a frame at a time; and three
// players in a match on the loopback network, one of whom joins in the middle
// of a round, whose server's world is pinned.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "game.h"
#include "tide/list.h"
#include "tide/math.h"
#include "tide/net.h"
#include "tide/session.h"
#include "tide/time.h"
#include "tide_test.h"

// The package's bridge: C's worlds, which a test starts without
void b2t_forget(void);

#define DT (1.0f / 60.0f)

// The world's measures, as game.tide has them
#define SEATS 4
#define PITCH 11.0f
#define DEPTH 80.0f
#define FINISH 40.0f
#define HOLD 180
#define LIMIT 5400
#define LOWEST 12.0f
#define DROP 9.0f
#define FALL 3.2f
#define FAST 18.0f
#define REACH 8
#define PAUSE 14
#define STUN 40
#define STANDS 170
#define RUBBLE 600
#define GUST 8
#define QUAKE 80
#define OVER 420

// The server's world in the match below, on every platform and in every
// configuration: Box2D's math, its image and the netcode, byte for byte.
// Once in the middle of the first round, and once after a new game.
#define RACING_HASH UINT64_C(0xde23545dcb0aef39)
#define MATCH_HASH UINT64_C(0xf5684195941ec9dd)

static tide_world world;

static tide_player_id player(const int index)
{
    return tide_player_from_index(index);
}

typedef bool has_fn(const tide_world *, tide_entity);

static int count(const tide_world *w, has_fn *has)
{
    int found = 0;
    for (uint32_t i = 0; i < w->entities.next_unused; i++) {
        if (has(w, tide_entity_in_slot(&w->entities, i))) found++;
    }
    return found;
}

static float home(const int seat)
{
    return ((float)seat - (SEATS - 1) * 0.5f) * PITCH;
}

// A seat's plinth: the match keeps them in a list, by seat.
static tide_entity plinth(const tide_world *w, const int seat)
{
    return *(const tide_entity *)tide_list_read(&w->heap, w->Towers.plinths, seat, sizeof(tide_entity));
}

static Seat seat_of(const tide_world *w, const int seat)
{
    return tide_get_Seat(w, plinth(w, seat));
}

// A seat's falling brick, or no entity.
static tide_entity falling(const tide_world *w, const int seat)
{
    for (uint32_t i = 0; i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        if (tide_has_Falling(w, e) && tide_get_Falling(w, e).seat == seat) return e;
    }
    return (tide_entity){0};
}

// The bricks that landed over a seat's plinth, and aren't off it.
static int standing(const tide_world *w, const int seat)
{
    int found = 0;
    for (uint32_t i = 0; i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        if (!tide_has_Brick(w, e) || tide_get_Brick(w, e).down) continue;
        if (tide_abs_f(tide_get_Box2D_Body(w, e).position.x - home(seat)) < PITCH * 0.5f) found++;
    }
    return found;
}

static int bricks_down(const tide_world *w)
{
    int found = 0;
    for (uint32_t i = 0; i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        if (tide_has_Brick(w, e) && tide_get_Brick(w, e).down) found++;
    }
    return found;
}

static int bricks_set(const tide_world *w)
{
    int found = 0;
    for (uint32_t i = 0; i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        if (tide_has_Brick(w, e) && tide_get_Brick(w, e).set) found++;
    }
    return found;
}

// ...and of them, the ones over a seat's plinth.
static int set_over(const tide_world *w, const int seat)
{
    int found = 0;
    for (uint32_t i = 0; i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        if (!tide_has_Brick(w, e) || !tide_get_Brick(w, e).set) continue;
        if (tide_abs_f(tide_get_Box2D_Body(w, e).position.x - home(seat)) < PITCH * 0.5f) found++;
    }
    return found;
}

// A match with its first round started, and as many players as asked for in
// its first seats. Towers nobody steers stay empty, unless a test says.
static void start(const bool bots, const int players)
{
    b2t_forget();
    tide_world_free(&world);
    tide_world_init(&world, DT);
    world.Towers.bots = bots;
    for (int i = 0; i < players; i++) tide_world_player_joined(&world, player(i));
    for (int i = 0; i < 3; i++) tide_world_tick(&world);
}

static void tick(const int ticks)
{
    for (int i = 0; i < ticks; i++) tide_world_tick(&world);
}

// What a player asks of the brick that's falling for them: where, how turned,
// and whether faster. Their input names the brick, as the game's view has it.
static void steer(tide_world *w, const int who, const int seat, const int column, const int quarter, const bool drop)
{
    const tide_entity brick = falling(w, seat);
    const int serial = tide_has_Falling(w, brick) ? tide_get_Falling(w, brick).serial : 0;
    tide_world_set_input(w, player(who), (Controls){.hands = true, .serial = serial, .column = column, .quarter = quarter, .drop = drop});
}

// Ticks until a seat has a brick falling, or has none: false when it didn't come to that.
static bool until_falling(const int seat, const bool there, const int most)
{
    for (int i = 0; i < most; i++) {
        if (tide_has_Falling(&world, falling(&world, seat)) == there) return true;
        tide_world_tick(&world);
    }
    return tide_has_Falling(&world, falling(&world, seat)) == there;
}

// A player puts a brick down: steers it there, and drops it once it's over its place.
static bool put(const int who, const int column, const int quarter)
{
    steer(&world, who, who, column, quarter, false);
    if (!until_falling(who, true, 300)) return false;
    for (int i = 0; i < 600; i++) {
        const tide_entity brick = falling(&world, who);
        if (!tide_has_Falling(&world, brick)) {
            // Their hands are off again: the seat is dealt no more until they're asked for another
            tide_world_set_input(&world, player(who), (Controls){0});
            return true;
        }
        const Falling now = tide_get_Falling(&world, brick);
        steer(&world, who, who, column, quarter, now.column == column && now.quarter == quarter);
        tide_world_tick(&world);
    }
    return false;
}

// Nobody's hands are on a brick: with the bots off, nothing more is dealt.
static void hands_off(void)
{
    for (int i = 0; i < SEATS; i++) tide_world_set_input(&world, player(i), (Controls){0});
}

// Towers to test with: every seat builds by itself for a while, then nothing
// more comes, and what's there comes to rest.
static void grow(const int ticks)
{
    hands_off();
    world.Towers.bots = true;
    for (int i = 0; i < ticks; i++) tide_world_tick(&world);
    world.Towers.bots = false;
    for (int i = 0; i < 600; i++) tide_world_tick(&world);
}

// A command with no fields, sent to the match and no entity
static uint32_t plain_command(const uint32_t id, uint8_t bytes[TIDE_COMMAND_BYTES])
{
    tide_bits bits = {bytes, TIDE_COMMAND_BYTES, 0, false};
    tide_bits_put(&bits, id, 16);
    tide_bits_put(&bits, 0, 32);
    tide_bits_put(&bits, 0, 32);
    return tide_bits_end(&bits);
}

static bool command(tide_world *w, const uint32_t id, const int sender)
{
    uint8_t bytes[TIDE_COMMAND_BYTES];
    const uint32_t size = plain_command(id, bytes);
    return tide_game_api.command(w, player(sender), 0, tide_game_api.command_sequence(w, player(sender), 0) + 1, bytes, size);
}

TIDE_TEST(towers_builds_four_plinths_and_seats_its_players)
{
    start(false, 2);
    TIDE_CHECK(world.Towers.phase == Phase_Play && world.Towers.round == 1);
    TIDE_CHECK(count(&world, tide_has_Seat) == SEATS && count(&world, tide_has_Ground) == 1);
    TIDE_CHECK(tide_list_read_count(&world.heap, world.Towers.plinths) == SEATS);
    for (int seat = 0; seat < SEATS; seat++) {
        TIDE_CHECK(seat_of(&world, seat).index == seat && seat_of(&world, seat).home == home(seat));
        const Box2D_Body body = tide_get_Box2D_Body(&world, plinth(&world, seat));
        TIDE_CHECK(body.type == Box2D_BodyType_Kinematic && body.position.x == home(seat) && body.position.y == -DEPTH / 2);
    }
    // The first two seats are the players', and the other two nobody's
    TIDE_CHECK(tide_get_Owner(&world, plinth(&world, 0)).player.id == player(0).id);
    TIDE_CHECK(tide_get_Owner(&world, plinth(&world, 1)).player.id == player(1).id);
    TIDE_CHECK(!tide_has_Owner(&world, plinth(&world, 2)) && !tide_has_Owner(&world, plinth(&world, 3)));
    // A player who leaves gives their seat up, and the next to join takes it
    tide_world_player_left(&world, player(0));
    tick(1);
    TIDE_CHECK(!tide_has_Owner(&world, plinth(&world, 0)));
    tide_world_player_joined(&world, player(2));
    tide_world_player_joined(&world, player(3));
    tick(1);
    TIDE_CHECK(tide_get_Owner(&world, plinth(&world, 0)).player.id == player(2).id);
    TIDE_CHECK(tide_get_Owner(&world, plinth(&world, 2)).player.id == player(3).id);
    // With the bots off and nobody's hands on a brick, nothing is dealt
    tick(200);
    TIDE_CHECK(count(&world, tide_has_Falling) == 0 && count(&world, tide_has_Brick) == 0);
    tide_world_free(&world);
}

TIDE_TEST(towers_a_brick_is_steered_then_becomes_a_body)
{
    start(false, 2);
    steer(&world, 0, 0, 0, 0, false);
    TIDE_REQUIRE(until_falling(0, true, 120));
    tide_entity brick = falling(&world, 0);
    Falling steered = tide_get_Falling(&world, brick);
    Box2D_Body body = tide_get_Box2D_Body(&world, brick);
    const Kind kind = tide_get_Piece(&world, brick).kind;
    // It's the first player's, a kinematic body over their plinth, and nothing of the second's is dealt
    TIDE_CHECK(tide_get_Owner(&world, brick).player.id == player(0).id);
    TIDE_CHECK(body.type == Box2D_BodyType_Kinematic && body.position.y == DROP);
    TIDE_CHECK(body.position.x == home(0) + (float)steered.column * 0.5f);
    TIDE_CHECK(!tide_has_Falling(&world, falling(&world, 1)));
    // Five half cells to the right, and a quarter turn: half a cell every other tick, and the turn at once
    const int from = steered.column;
    for (int i = 0; i < 14; i++) {
        steer(&world, 0, 0, from + 5, 1, false);
        tide_world_tick(&world);
    }
    steered = tide_get_Falling(&world, brick);
    body = tide_get_Box2D_Body(&world, brick);
    TIDE_CHECK(steered.column == from + 5 && steered.quarter == 1);
    TIDE_CHECK(body.position.x == home(0) + (float)(from + 5) * 0.5f);
    TIDE_CHECK(tide_abs_f(body.angle + TIDE_PI_F / 2) < 0.001f);
    // It falls at its own pace whatever it's asked for across, and faster when asked
    TIDE_CHECK(tide_abs_f(body.velocity.y + FALL) < 0.001f && body.position.y < DROP - 0.5f && body.position.y > DROP - 1.5f);
    // An input that names another brick steers nothing
    tide_world_set_input(&world, player(0), (Controls){.hands = true, .serial = steered.serial + SEATS, .column = -8});
    tick(6);
    TIDE_CHECK(tide_get_Falling(&world, brick).column == from + 5);
    steer(&world, 0, 0, from + 5, 1, true);
    tick(2);
    TIDE_CHECK(tide_abs_f(tide_get_Box2D_Body(&world, brick).velocity.y + FAST) < 0.001f);
    // While it falls it has a body and no shape: it touches nothing, and what's in its way is asked
    const int shapes = world.Box2D_Physics.shapes;
    const int bodies = world.Box2D_Physics.bodies;
    TIDE_CHECK(!tide_has_Box2D_Box(&world, brick) && count(&world, tide_has_Box2D_Part) == 0);
    // It falls at that speed until the tick it would reach the plinth, and is put on it there, at rest
    float lowest = DROP;
    for (int i = 0; i < 120 && tide_has_Falling(&world, brick); i++) {
        steer(&world, 0, 0, from + 5, 1, true);
        lowest = tide_get_Box2D_Body(&world, brick).position.y;
        TIDE_CHECK(tide_abs_f(tide_get_Box2D_Body(&world, brick).velocity.y + FAST) < 0.001f);
        tide_world_tick(&world);
    }
    // It landed: the same entity, a brick now and no falling one, and its body the same one, a dynamic body
    TIDE_REQUIRE(tide_has_Brick(&world, brick) && !tide_has_Falling(&world, brick) && !tide_has_Owner(&world, brick));
    const tide_entity landed = brick;
    Box2D_Body lands = tide_get_Box2D_Body(&world, landed);
    printf("    falling at %g m/s, it was last free at %.3f and was put at %.3f\n", (double)FAST, (double)lowest, (double)lands.position.y);
    TIDE_CHECK(lands.id == body.id && lands.type == Box2D_BodyType_Dynamic && lands.velocity.y == 0.0f && lands.velocity.x == 0.0f);
    TIDE_CHECK(lands.position.y < lowest && lands.position.y > lowest - FAST / 60.0f);
    TIDE_CHECK(count(&world, tide_has_Brick) == 1 && count(&world, tide_has_Falling) == 0);
    TIDE_CHECK(tide_get_Brick(&world, landed).seat == 0 && tide_get_Piece(&world, landed).kind == kind);
    TIDE_CHECK(world.Box2D_Physics.bodies == bodies);
    // Its boxes are on its body from the next step: the first is its own, and a brick of two has the second as a
    // part, an entity of its own that names the brick
    const bool two = kind != Kind_I && kind != Kind_O;
    tick(2);
    TIDE_CHECK(tide_has_Box2D_Box(&world, landed) && world.Box2D_Physics.shapes == shapes + (two ? 2 : 1));
    TIDE_CHECK(count(&world, tide_has_Box2D_Part) == (two ? 1 : 0) && world.Box2D_Physics.bodies == bodies);
    for (uint32_t i = 0; i < world.entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&world.entities, i);
        if (!tide_has_Box2D_Part(&world, e)) continue;
        TIDE_CHECK(tide_entity_equal(tide_get_Box2D_Part(&world, e).of, landed) && tide_get_Box2D_Part(&world, e).box != 0);
    }
    tick(90);
    const Box2D_Body rests = tide_get_Box2D_Body(&world, landed);
    printf("    a brick of kind %d steered to %.1f m right of its plinth's middle rests at (%.3f, %.3f), turned %.3f\n", (int)kind,
           (double)(from + 5) * 0.5, (double)(rests.position.x - home(0)), (double)rests.position.y, (double)rests.angle);
    TIDE_CHECK(tide_abs_f(rests.position.x - (home(0) + (float)(from + 5) * 0.5f)) < 0.1f);
    TIDE_CHECK(rests.position.y > 0.4f && rests.position.y < 2.6f && tide_abs_f(rests.angle + TIDE_PI_F / 2) < 0.05f);
    TIDE_CHECK(tide_abs_f(rests.velocity.x) + tide_abs_f(rests.velocity.y) < 0.01f);
    // It came to rest where it was put, to the centimeter: it landed where it touched
    TIDE_CHECK(tide_abs_f(rests.position.y - lands.position.y) < 0.02f);
    // The tower is as high as the brick, and the seat's next one is on its way
    TIDE_CHECK(seat_of(&world, 0).top > 0.9f && seat_of(&world, 0).top < 4.1f);
    TIDE_CHECK(tide_has_Falling(&world, falling(&world, 0)) && seat_of(&world, 0).dealt == 2);
    tide_world_free(&world);
}

// A falling brick only goes where it has room: asked across into a brick
// that's beside it, or to turn where it would be in the plinth, it stays as it
// is, and falls on.
TIDE_TEST(towers_a_falling_brick_keeps_out_of_what_is_in_its_way)
{
    start(false, 1);
    // A first brick, two meters to the right of the plinth's middle. Just over the plinth it's asked to turn: a
    // brick that's longer than it's high would be in the plinth, so it isn't turned
    steer(&world, 0, 0, 4, 0, false);
    TIDE_REQUIRE(until_falling(0, true, 300));
    const tide_entity first = falling(&world, 0);
    int upright = 0;
    for (int i = 0; i < 600 && tide_has_Falling(&world, first); i++) {
        steer(&world, 0, 0, 4, tide_get_Box2D_Body(&world, first).position.y < 1.4f ? 1 : 0, false);
        tide_world_tick(&world);
        if (tide_has_Falling(&world, first)) upright = tide_get_Falling(&world, first).quarter;
    }
    TIDE_REQUIRE(tide_has_Brick(&world, first));
    if (tide_get_Piece(&world, first).kind != Kind_O) TIDE_CHECK(upright == 0);
    hands_off();
    tick(60);
    const Box2D_Body stood = tide_get_Box2D_Body(&world, first);
    TIDE_CHECK(tide_abs_f(stood.angle) < 0.01f && stood.position.y < 1.1f);
    // The next one comes down two meters to the left, until it's beside the first
    steer(&world, 0, 0, -4, 0, false);
    TIDE_REQUIRE(until_falling(0, true, 300));
    const tide_entity brick = falling(&world, 0);
    const Kind kind = tide_get_Piece(&world, brick).kind;
    for (int i = 0; i < 600 && tide_get_Box2D_Body(&world, brick).position.y > 1.7f; i++) {
        steer(&world, 0, 0, -4, 0, false);
        tide_world_tick(&world);
    }
    TIDE_REQUIRE(tide_has_Falling(&world, brick) && tide_get_Falling(&world, brick).column == -4);
    // Asked all the way across to where the first is: it goes as far as the first lets it
    int reached = -4;
    for (int i = 0; i < 120 && tide_has_Falling(&world, brick); i++) {
        steer(&world, 0, 0, 4, 0, false);
        tide_world_tick(&world);
        if (tide_has_Falling(&world, brick)) reached = tide_get_Falling(&world, brick).column;
    }
    TIDE_REQUIRE(tide_has_Brick(&world, brick));
    hands_off();
    tick(90);
    const Box2D_Body rests = tide_get_Box2D_Body(&world, brick);
    const Box2D_Body after = tide_get_Box2D_Body(&world, first);
    printf("    asked 4 m across into a brick at %.2f, a falling brick of kind %d got to %.2f, and rests at (%.2f, %.2f)\n",
           (double)(stood.position.x - home(0)), (int)kind, (double)reached * 0.5, (double)(rests.position.x - home(0)),
           (double)rests.position.y);
    TIDE_CHECK(reached > -4 && reached < 4);
    // It landed on the plinth beside the first, which it never touched on its way: that one is where it was
    TIDE_CHECK(rests.position.y < 1.6f && rests.position.x < stood.position.x - 0.9f);
    TIDE_CHECK(tide_abs_f(after.position.x - stood.position.x) + tide_abs_f(after.position.y - stood.position.y) < 0.02f);
    TIDE_CHECK(count(&world, tide_has_Brick) == 2 && bricks_down(&world) == 0);
    tide_world_free(&world);
}

TIDE_TEST(towers_a_brick_lands_on_the_bricks_under_it)
{
    start(false, 1);
    grow(600);
    const int there = standing(&world, 0);
    const float top = seat_of(&world, 0).top;
    TIDE_REQUIRE(there >= 3 && top > 1.5f && count(&world, tide_has_Falling) == 0);
    tide_world before = {0};
    tide_world_copy(&before, &world);
    // The player's brick comes down on their tower, in its middle: it's a body where it met the tower, not on the plinth
    TIDE_REQUIRE(put(0, 0, 0));
    hands_off();
    tick(90);
    TIDE_CHECK(standing(&world, 0) == there + 1 && bricks_down(&world) == bricks_down(&before));
    for (uint32_t i = 0; i < world.entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&world.entities, i);
        if (!tide_has_Brick(&world, e) || tide_has_Brick(&before, e)) continue;
        const Box2D_Body body = tide_get_Box2D_Body(&world, e);
        printf("    on a tower %.2f m high, the brick rests at %.2f m\n", (double)top, (double)body.position.y);
        TIDE_CHECK(body.position.y > 1.4f && body.position.y < top + 2.1f && tide_abs_f(body.position.x - home(0)) < 3.5f);
        TIDE_CHECK(tide_abs_f(body.velocity.x) + tide_abs_f(body.velocity.y) < 0.05f);
    }
    // The tower took it, and stands as it did: no brick of it moved far
    int compared = 0;
    for (uint32_t i = 0; i < before.entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&before.entities, i);
        if (!tide_has_Brick(&before, e) || tide_get_Brick(&before, e).down) continue;
        const tide_float2 was = tide_get_Box2D_Body(&before, e).position;
        const tide_float2 is = tide_get_Box2D_Body(&world, e).position;
        TIDE_CHECK(tide_abs_f(was.x - is.x) + tide_abs_f(was.y - is.y) < 0.25f);
        compared++;
    }
    TIDE_CHECK(compared >= there);
    tide_world_free(&before);
    tide_world_free(&world);
}

TIDE_TEST(towers_a_tower_over_the_line_for_long_enough_takes_the_round)
{
    start(false, 2);
    TIDE_REQUIRE(put(0, 0, 0));
    TIDE_REQUIRE(put(1, 0, 0));
    hands_off();
    tick(30);
    TIDE_CHECK(count(&world, tide_has_Brick) == 2 && count(&world, tide_has_Falling) == 0);
    // The second seat's plinth, put so that its top is over the line, as a
    // tower's would be: a kinematic body goes where its position is written
    const tide_entity high = plinth(&world, 1);
    Box2D_Body body = tide_get_Box2D_Body(&world, high);
    body.position.y = FINISH + 1 - DEPTH / 2;
    tide_set_Box2D_Body(&world, high, body);
    tick(40);
    TIDE_CHECK(seat_of(&world, 1).top > FINISH && seat_of(&world, 1).above > 20 && seat_of(&world, 0).above == 0);
    // Back under it before three seconds are up: it counts from nothing again
    body = tide_get_Box2D_Body(&world, high);
    body.position.y = FINISH - 4 - DEPTH / 2;
    tide_set_Box2D_Body(&world, high, body);
    tick(40);
    TIDE_CHECK(seat_of(&world, 1).above == 0 && world.Towers.phase == Phase_Play);
    body = tide_get_Box2D_Body(&world, high);
    body.position.y = FINISH + 1 - DEPTH / 2;
    tide_set_Box2D_Body(&world, high, body);
    int took = 0;
    while (world.Towers.phase == Phase_Play && took < 400) {
        tide_world_tick(&world);
        took++;
    }
    printf("    the round was over %d ticks after the tower was put over the line\n", took);
    TIDE_CHECK(world.Towers.phase == Phase_Over && world.Towers.winner == 1 && took >= HOLD && took < HOLD + 10);
    tick(1);
    TIDE_CHECK(seat_of(&world, 1).wins == 1 && seat_of(&world, 0).wins == 0);
    // Nothing is dealt while a round is over, and what was falling is gone
    tick(30);
    TIDE_CHECK(count(&world, tide_has_Falling) == 0);
    // The plinths sink, the winner's last, and the bricks come down with them
    const float winners = tide_get_Box2D_Body(&world, high).position.y;
    TIDE_CHECK(tide_get_Box2D_Body(&world, plinth(&world, 0)).position.y < -DEPTH / 2 - 5.0f);
    TIDE_CHECK(winners == FINISH + 1 - DEPTH / 2);
    tick(200);
    TIDE_CHECK(tide_get_Box2D_Body(&world, high).position.y < winners - 5.0f);
    TIDE_CHECK(count(&world, tide_has_Brick) == 2);
    // And the next round starts from nothing, with the plinths back
    tick(OVER);
    TIDE_CHECK(world.Towers.phase == Phase_Play && world.Towers.round == 2 && world.Towers.winner == -1);
    TIDE_CHECK(count(&world, tide_has_Brick) == 0);
    for (int seat = 0; seat < SEATS; seat++) {
        const Box2D_Body back = tide_get_Box2D_Body(&world, plinth(&world, seat));
        TIDE_CHECK(back.position.x == home(seat) && back.position.y == -DEPTH / 2);
        TIDE_CHECK(seat_of(&world, seat).above == 0 && seat_of(&world, seat).dealt <= 1);
    }
    TIDE_CHECK(seat_of(&world, 1).wins == 1);
    tide_world_free(&world);
}

TIDE_TEST(towers_a_brick_off_its_tower_costs_and_is_cleared)
{
    start(false, 2);
    TIDE_REQUIRE(put(0, 0, 0));
    TIDE_REQUIRE(put(1, 0, 0));
    hands_off();
    tick(20);
    TIDE_CHECK(seat_of(&world, 0).lost == 0 && seat_of(&world, 0).wait <= PAUSE);
    // The first player's brick, put over the ground beside their plinth
    tide_entity fell = (tide_entity){0};
    for (uint32_t i = 0; i < world.entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&world.entities, i);
        if (tide_has_Brick(&world, e) && tide_get_Brick(&world, e).seat == 0) fell = e;
    }
    TIDE_REQUIRE(tide_has_Brick(&world, fell));
    Box2D_Body body = tide_get_Box2D_Body(&world, fell);
    body.position = tide_f2(home(0) + 5.0f, 1.0f);
    tide_set_Box2D_Body(&world, fell, body);
    const int waited = seat_of(&world, 0).wait;
    for (int i = 0; i < 120 && !tide_get_Brick(&world, fell).down; i++) tide_world_tick(&world);
    tick(1);
    // It's off its tower: whoever put it there waits longer for their next, and nobody else does
    TIDE_CHECK(tide_get_Brick(&world, fell).down && seat_of(&world, 0).lost == 1 && seat_of(&world, 1).lost == 0);
    TIDE_CHECK(seat_of(&world, 0).wait >= STUN && seat_of(&world, 0).wait > waited);
    TIDE_CHECK(seat_of(&world, 0).top == 0.0f);
    // It lies on the ground a while, and is cleared
    tick(120);
    TIDE_CHECK(tide_has_Brick(&world, fell) && tide_get_Box2D_Body(&world, fell).position.y < -5.0f);
    tick(RUBBLE);
    TIDE_CHECK(!tide_has_Brick(&world, fell) && count(&world, tide_has_Brick) == 1);
    tide_world_free(&world);
}

TIDE_TEST(towers_the_line_comes_down_on_a_round_that_goes_on_long)
{
    start(false, 2);
    tick(60);
    TIDE_CHECK(world.Towers.line == FINISH && world.Towers.clock > 50);
    // A minute and a half in, it starts down, a meter every four seconds or so
    world.Towers.clock = LIMIT - 10;
    tick(10);
    TIDE_CHECK(world.Towers.line == FINISH);
    tick(250);
    TIDE_CHECK(world.Towers.line < FINISH - 0.9f && world.Towers.line > FINISH - 1.1f);
    // A tower that was under the line is over it once the line has come down past it, and takes the round
    Box2D_Body body = tide_get_Box2D_Body(&world, plinth(&world, 1));
    body.position.y = FINISH - 6 - DEPTH / 2;
    tide_set_Box2D_Body(&world, plinth(&world, 1), body);
    tick(60);
    TIDE_CHECK(seat_of(&world, 1).above == 0 && world.Towers.phase == Phase_Play);
    world.Towers.clock = LIMIT + 1700;
    for (int i = 0; i < 400 && world.Towers.phase == Phase_Play; i++) tide_world_tick(&world);
    TIDE_CHECK(world.Towers.phase == Phase_Over && world.Towers.winner == 1 && world.Towers.line < FINISH - 6.0f);
    // It never comes lower than this, and the next round's is back where it was
    world.Towers.phase = Phase_Play;
    world.Towers.clock = LIMIT + 100000;
    tick(2);
    TIDE_CHECK(world.Towers.line == LOWEST);
    TIDE_REQUIRE(command(&world, TIDE_COMMAND_ID_Rebuild, 0));
    tick(4);
    TIDE_CHECK(world.Towers.line == FINISH && world.Towers.clock < 10 && world.Towers.round == 1);
    tide_world_free(&world);
}

// How far a seat's bricks are from where they were, the furthest of them.
static float disturbed(const tide_world *w, const tide_world *before, const int seat)
{
    float most = 0;
    for (uint32_t i = 0; i < before->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&before->entities, i);
        if (!tide_has_Brick(before, e) || !tide_has_Brick(w, e)) continue;
        const tide_float2 then = tide_get_Box2D_Body(before, e).position;
        if (tide_abs_f(then.x - home(seat)) > PITCH * 0.5f) continue;
        const tide_float2 now = tide_get_Box2D_Body(w, e).position;
        const float moved = tide_abs_f(now.x - then.x) + tide_abs_f(now.y - then.y);
        if (moved > most) most = moved;
    }
    return most;
}

static void earn(const int seat, const Power power)
{
    Seat has = seat_of(&world, seat);
    has.power = power;
    tide_set_Seat(&world, plinth(&world, seat), has);
}

// The fastest any brick over a seat's plinth goes across, the way the wind blows.
static float blown(const tide_world *w, const int seat)
{
    float fastest = 0;
    for (uint32_t i = 0; i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        if (!tide_has_Brick(w, e) || tide_get_Brick(w, e).down) continue;
        const Box2D_Body body = tide_get_Box2D_Body(w, e);
        if (tide_abs_f(body.position.x - home(seat)) < PITCH * 0.5f && body.velocity.x > fastest) fastest = body.velocity.x;
    }
    return fastest;
}

TIDE_TEST(towers_what_a_tower_earns_acts_on_the_others)
{
    // A gust: against the tallest of the other towers, away from whoever sent it. Two players with a brick each
    // on their plinths, and nothing on the other two: the gust the first sends is against the second's
    start(false, 2);
    TIDE_REQUIRE(put(1, 0, 0));
    TIDE_REQUIRE(put(0, 0, 0));
    hands_off();
    tick(240);
    TIDE_REQUIRE(standing(&world, 1) == 1 && standing(&world, 0) == 1 && seat_of(&world, 1).top > 0.9f);
    tide_world rest = {0};
    tide_world_copy(&rest, &world);
    // Nothing at rest moves by itself
    tick(120);
    for (int seat = 0; seat < SEATS; seat++) TIDE_CHECK(disturbed(&world, &rest, seat) < 0.005f);
    earn(0, Power_Gust);
    TIDE_REQUIRE(command(&world, TIDE_COMMAND_ID_Use, 0));
    tick(2);
    TIDE_CHECK(seat_of(&world, 0).power == Power_None);
    const int gusts[SEATS] = {world.Towers.gusts.x, world.Towers.gusts.y, world.Towers.gusts.z, world.Towers.gusts.w};
    const int winds[SEATS] = {world.Towers.winds.x, world.Towers.winds.y, world.Towers.winds.z, world.Towers.winds.w};
    for (int seat = 0; seat < SEATS; seat++) TIDE_CHECK(gusts[seat] == (seat == 1 ? GUST - 1 : 0));
    TIDE_CHECK(winds[1] == 1);
    // While it blows, the bricks at the top of that tower are pushed along it, faster each tick, and nothing else is
    float fastest = 0;
    for (int i = 0; i < GUST; i++) {
        tide_world_tick(&world);
        if (blown(&world, 1) > fastest) fastest = blown(&world, 1);
        TIDE_CHECK(blown(&world, 0) < 0.001f);
    }
    tick(90);
    printf("    a gust had a brick of the tower it was against going %.2f m/s and moved one by %.2f m, and of the tower that sent it by %.2f\n",
           (double)fastest, (double)disturbed(&world, &rest, 1), (double)disturbed(&world, &rest, 0));
    TIDE_CHECK(fastest > 0.8f && disturbed(&world, &rest, 1) > 0.1f && disturbed(&world, &rest, 0) < 0.005f);
    TIDE_CHECK(world.Towers.gusts.x + world.Towers.gusts.y + world.Towers.gusts.z + world.Towers.gusts.w == 0);
    // Using what hasn't been earned does nothing
    TIDE_REQUIRE(command(&world, TIDE_COMMAND_ID_Use, 0));
    tick(3);
    TIDE_CHECK(world.Towers.gusts.x + world.Towers.gusts.y + world.Towers.gusts.z + world.Towers.gusts.w == 0);

    // Two players again, and four towers that built themselves and came to rest
    start(false, 2);
    grow(900);
    for (int seat = 0; seat < SEATS; seat++) TIDE_REQUIRE(standing(&world, seat) >= 5 && seat_of(&world, seat).top > 3.0f);
    TIDE_CHECK(bricks_set(&world) == 0);

    // Cement: every brick at rest in the tower that used it is set solid, a kinematic body, and no other tower's
    tick(300);
    const int there = standing(&world, 1);
    TIDE_REQUIRE(there >= 4);
    earn(1, Power_Cement);
    TIDE_REQUIRE(command(&world, TIDE_COMMAND_ID_Use, 1));
    tick(3);
    printf("    cement set %d of a tower's %d bricks solid\n", bricks_set(&world), there);
    TIDE_CHECK(bricks_set(&world) == there && standing(&world, 1) == there);
    for (uint32_t i = 0; i < world.entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&world.entities, i);
        if (!tide_has_Brick(&world, e) || tide_get_Brick(&world, e).down) continue;
        const bool set = tide_get_Brick(&world, e).set;
        TIDE_CHECK(tide_get_Box2D_Body(&world, e).type == (set ? Box2D_BodyType_Kinematic : Box2D_BodyType_Dynamic));
        TIDE_CHECK(set == (tide_abs_f(tide_get_Box2D_Body(&world, e).position.x - home(1)) < PITCH * 0.5f));
    }

    // A quake: every plinth but the one that sent it shakes, and is back where it was. What's loose on
    // one is shaken about; what's set solid goes there and back with its plinth, and no further
    tick(60);
    tide_world_copy(&rest, &world);
    earn(0, Power_Quake);
    TIDE_REQUIRE(command(&world, TIDE_COMMAND_ID_Use, 0));
    tick(2);
    TIDE_CHECK(world.Towers.quakes.x == 0 && world.Towers.quakes.y == QUAKE - 1 && world.Towers.quakes.z == QUAKE - 1);
    float swung = 0;
    float carried = 0;
    for (int i = 0; i < QUAKE + 4; i++) {
        tide_world_tick(&world);
        const float off = tide_abs_f(tide_get_Box2D_Body(&world, plinth(&world, 1)).position.x - home(1));
        if (off > swung) swung = off;
        if (disturbed(&world, &rest, 1) > carried) carried = disturbed(&world, &rest, 1);
        TIDE_CHECK(tide_get_Box2D_Body(&world, plinth(&world, 0)).position.x == home(0));
    }
    tick(120);
    printf("    a quake swung a plinth %.2f m, what was set on it %.2f m, and moved a loose brick of another by %.2f m\n", (double)swung,
           (double)carried, (double)disturbed(&world, &rest, 2));
    TIDE_CHECK(swung > 0.07f && carried > swung - 0.03f && carried < swung + 0.03f);
    TIDE_CHECK(disturbed(&world, &rest, 2) > 0.01f && disturbed(&world, &rest, 0) < 0.005f && disturbed(&world, &rest, 1) < 0.005f);
    TIDE_CHECK(tide_get_Box2D_Body(&world, plinth(&world, 1)).position.x == home(1));
    TIDE_CHECK(tide_get_Box2D_Body(&world, plinth(&world, 1)).velocity.x == 0.0f);

    // When the round is over, what was set is let go with the rest, from the bottom up, and comes down
    Box2D_Body body = tide_get_Box2D_Body(&world, plinth(&world, 0));
    body.position.y = FINISH + 1 - DEPTH / 2;
    tide_set_Box2D_Body(&world, plinth(&world, 0), body);
    for (int i = 0; i < 400 && world.Towers.phase == Phase_Play; i++) tide_world_tick(&world);
    TIDE_CHECK(world.Towers.phase == Phase_Over && world.Towers.winner == 0 && set_over(&world, 1) == there);
    tick(40);
    TIDE_CHECK(set_over(&world, 1) == 0);
    tick(200);
    printf("    with the round over, the tower that was set lies %.1f m under where its plinth's top was\n", (double)-seat_of(&world, 1).top);
    TIDE_CHECK(seat_of(&world, 1).top < -3.0f);
    for (uint32_t i = 0; i < world.entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&world.entities, i);
        if (tide_has_Brick(&world, e)) TIDE_CHECK(tide_get_Box2D_Body(&world, e).type == Box2D_BodyType_Dynamic);
    }
    tide_world_free(&rest);
    tide_world_free(&world);
}

// A tower's bricks are loose all the way down, however tall it is: nothing sets
// by itself, and the tower stands
TIDE_TEST(towers_a_tall_tower_is_loose_all_the_way_down)
{
    start(true, 0);
    int ticks = 0;
    while (seat_of(&world, 0).top < 24.0f && ticks < 6000) {
        // What a tower earns isn't used here: Cement would set it, and the others' gusts and quakes shake it
        for (int seat = 0; seat < SEATS; seat++) earn(seat, Power_None);
        tide_world_tick(&world);
        ticks++;
    }
    TIDE_REQUIRE(seat_of(&world, 0).top >= 24.0f);
    world.Towers.bots = false;
    for (int i = 0; i < 900; i++) {
        for (int seat = 0; seat < SEATS; seat++) earn(seat, Power_None);
        tide_world_tick(&world);
    }
    const float top = seat_of(&world, 0).top;
    const int lost = seat_of(&world, 0).lost;
    int loose = 0;
    int parts = 0;
    for (uint32_t i = 0; i < world.entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&world.entities, i);
        if (tide_has_Box2D_Part(&world, e)) parts++;
        if (!tide_has_Brick(&world, e) || tide_get_Brick(&world, e).down) continue;
        const Box2D_Body body = tide_get_Box2D_Body(&world, e);
        TIDE_CHECK(!tide_get_Brick(&world, e).set && body.type == Box2D_BodyType_Dynamic);
        if (tide_abs_f(body.position.x - home(0)) < PITCH * 0.5f) loose++;
    }
    printf("    a tower %.1f m high after %.0f seconds is %d loose bricks and none set, having lost %d; the four towers' bricks have %d "
           "parts\n",
           (double)top, ticks / 60.0, loose, lost, parts);
    TIDE_CHECK(top >= 22.0f && loose >= 25 && bricks_set(&world) == 0 && parts > 20);
    // It stands: at rest, a minute on, with every brick where it was
    tide_world before = {0};
    tide_world_copy(&before, &world);
    tick(3600);
    TIDE_CHECK(disturbed(&world, &before, 0) < 0.01f && seat_of(&world, 0).lost == lost && !world.Box2D_Physics.moved);
    tide_world_free(&before);
    tide_world_free(&world);
}

TIDE_TEST(towers_nobodys_towers_build_by_themselves)
{
    start(true, 1);
    // The player's menu is up: their tower builds by itself too
    tide_world_set_input(&world, player(0), (Controls){0});
    tick(1500);
    for (int seat = 0; seat < SEATS; seat++) {
        printf("    after 25 seconds seat %d stands %.1f m high, of %d bricks, with %d lost\n", seat, (double)seat_of(&world, seat).top,
               standing(&world, seat), seat_of(&world, seat).lost);
        TIDE_CHECK(seat_of(&world, seat).top > 4.0f && standing(&world, seat) >= 7);
    }
    // The Bots button stops them: what's falling lands, and nothing more comes
    TIDE_REQUIRE(command(&world, TIDE_COMMAND_ID_Bots, 0));
    tick(400);
    TIDE_CHECK(!world.Towers.bots && count(&world, tide_has_Falling) == 0);
    int dealt = 0;
    for (int seat = 0; seat < SEATS; seat++) dealt += seat_of(&world, seat).dealt;
    tick(200);
    for (int seat = 0; seat < SEATS; seat++) dealt -= seat_of(&world, seat).dealt;
    TIDE_CHECK(dealt == 0 && count(&world, tide_has_Falling) == 0);
    // ...and starts them again. One of them gets over the line in the end, and takes the round
    TIDE_REQUIRE(command(&world, TIDE_COMMAND_ID_Bots, 0));
    int ticks = 2100;
    int most = 0;
    while (world.Towers.phase == Phase_Play && ticks < 30000) {
        tide_world_tick(&world);
        ticks++;
        const int bodies = count(&world, tide_has_Box2D_Body);
        if (bodies > most) most = bodies;
    }
    printf("    a round of four towers that build by themselves took %.0f seconds, and %d bodies at the most\n", ticks / 60.0, most);
    TIDE_CHECK(world.Towers.phase == Phase_Over && world.Towers.winner >= 0);
    tide_world_free(&world);
}

TIDE_TEST(towers_a_new_game_starts_from_nothing)
{
    start(true, 1);
    tide_world_set_input(&world, player(0), (Controls){0});
    tick(900);
    Seat won = seat_of(&world, 2);
    won.wins = 3;
    tide_set_Seat(&world, plinth(&world, 2), won);
    TIDE_CHECK(count(&world, tide_has_Brick) > 12);
    TIDE_REQUIRE(command(&world, TIDE_COMMAND_ID_Rebuild, 0));
    tick(3);
    TIDE_CHECK(world.Towers.round == 1 && world.Towers.phase == Phase_Play);
    TIDE_CHECK(count(&world, tide_has_Brick) == 0 && count(&world, tide_has_Falling) == 0);
    for (int seat = 0; seat < SEATS; seat++) {
        TIDE_CHECK(seat_of(&world, seat).wins == 0 && seat_of(&world, seat).dealt == 0 && seat_of(&world, seat).top == 0.0f);
    }
    // And builds again
    tick(600);
    TIDE_CHECK(count(&world, tide_has_Brick) > 8);
    tide_world_free(&world);
}

TIDE_TEST(towers_a_player_who_joins_late_starts_level)
{
    start(false, 1);
    // The first player's tower stands eight meters and a brick high: their plinth is put there, for the test
    Box2D_Body body = tide_get_Box2D_Body(&world, plinth(&world, 0));
    body.position.y = 8.0f - DEPTH / 2;
    tide_set_Box2D_Body(&world, plinth(&world, 0), body);
    TIDE_REQUIRE(put(0, 0, 0));
    hands_off();
    tick(60);
    const float theirs = seat_of(&world, 0).top;
    TIDE_REQUIRE(theirs > 8.9f && theirs < 10.1f);
    // A second joins: their plinth rises until its top is level with that tower's, to the meter under
    tide_world_player_joined(&world, player(1));
    tick(2);
    TIDE_CHECK(tide_get_Owner(&world, plinth(&world, 1)).player.id == player(1).id);
    TIDE_CHECK(seat_of(&world, 1).lifted && seat_of(&world, 1).lift > 7.0f);
    tick(400);
    const Seat risen = seat_of(&world, 1);
    printf("    the first tower stands %.2f m high, and the plinth of a player who joined rose to %.2f\n", (double)theirs, (double)risen.base);
    TIDE_CHECK(risen.lift <= 0.0f && risen.base > theirs - 1.1f && risen.base < theirs + 0.1f && risen.top == risen.base);
    TIDE_CHECK(tide_get_Box2D_Body(&world, plinth(&world, 1)).velocity.y == 0.0f);
    // They build from there, and the first player's plinth stayed where it was
    TIDE_REQUIRE(put(1, 0, 0));
    hands_off();
    tick(40);
    TIDE_CHECK(seat_of(&world, 1).top > risen.base + 0.9f && seat_of(&world, 0).base == 8.0f);
    // It doesn't rise again in the same round, for one who leaves and comes back
    tide_world_player_left(&world, player(1));
    tick(2);
    body = tide_get_Box2D_Body(&world, plinth(&world, 0));
    body.position.y = 14.0f - DEPTH / 2;
    tide_set_Box2D_Body(&world, plinth(&world, 0), body);
    tick(10);
    tide_world_player_joined(&world, player(1));
    tick(120);
    TIDE_CHECK(seat_of(&world, 1).lift <= 0.0f && seat_of(&world, 1).base == risen.base);
    // A new round puts every plinth back
    TIDE_REQUIRE(command(&world, TIDE_COMMAND_ID_Rebuild, 0));
    tick(4);
    TIDE_CHECK(seat_of(&world, 1).base == 0.0f && seat_of(&world, 0).base == 0.0f && !seat_of(&world, 1).lifted);
    tide_world_free(&world);
}

// ---------------------------------------------------------------------------
// What one machine shows of the match, and which match it's in: its views and
// local handlers, as a host runs a frame, with no window

static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_devices devices;

static void frame(const tide_world *w)
{
    tide_local_frame_time(&local, 1.0f / 60.0f);
    tide_draw_reset(&draw);
    tide_gui_begin(&gui, &devices, tide_f2(376, 400), NULL);
    tide_frame(w, NULL, 1.0f, w, &local, &draw, &gui);
    tide_gui_end(&gui, &draw);
}

// A machine as it starts: its local state, and where the host says it stands.
static void machine(const uint32_t state, const int who, const bool server, const bool open, const char *room)
{
    tide_local_init(&local);
    memset(&gui, 0, sizeof gui);
    memset(&devices, 0, sizeof devices);
    tide_local_set_session(&local, state, who < 0 ? (tide_player_id){0} : player(who), 0, server, open, room);
}

// Whether this machine's player's hands are on their brick: what its input says.
static bool plays(void)
{
    return tide_input_sample(&devices, &local).hands;
}

static bool says_something(void)
{
    return tide_text_read(&local.heap, local.Lobby.message).bytes > 0;
}

// What local code asked of the session since it was last asked
static uint32_t asked(void)
{
    tide_session_request request = {0};
    tide_start from;
    return tide_local_take_request(&local, &request, &from) ? request.kind : (uint32_t)TIDE_REQUEST_NONE;
}

TIDE_TEST(towers_opens_on_its_lobby_unless_it_was_told_which_match)
{
    start(true, 2);
    // Started with nothing said: a match of its own, closed, behind the title.
    // The player's tower builds by itself while the lobby is up
    machine(TIDE_SESSION_CONNECTED, 0, true, false, "");
    frame(&world);
    frame(&world);
    TIDE_CHECK(local.Lobby.page == Page_Title && !local.Hand.playing && !plays());
    TIDE_CHECK(local.Lobby.players == 2 && local.Lobby.mine == 0 && local.Lobby.manned == 3);
    TIDE_CHECK(asked() == TIDE_REQUEST_NONE);
    tide_local_free(&local);

    // --host, or a page's ?host: the match is open by the first frame, and no lobby is in the way
    machine(TIDE_SESSION_CONNECTED, 0, true, true, "K7QF2M");
    frame(&world);
    frame(&world);
    TIDE_CHECK(local.Lobby.page == Page_None && local.Hand.playing && plays());
    // The button is held as the menu closes: the player's hands aren't on the brick until it's let go
    local.Lobby.page = Page_Room;
    frame(&world);
    TIDE_CHECK(!plays());
    local.Lobby.page = Page_None;
    devices.pointer.press.pressed = devices.pointer.press.held = true;
    frame(&world);
    TIDE_CHECK(!plays());
    devices.pointer.press.pressed = devices.pointer.press.held = false;
    frame(&world);
    TIDE_CHECK(plays());
    tide_local_free(&local);

    // --join <code>, or ?join=<code>: joining by the first frame. The page says so until the match's world is here
    machine(TIDE_SESSION_CONNECTING, -1, false, false, "K7QF2M");
    frame(NULL);
    TIDE_CHECK(local.Lobby.page == Page_Join && local.Lobby.joining && !plays());
    tide_local_connected(&local);
    tide_local_set_session(&local, TIDE_SESSION_CONNECTED, player(1), 30, false, false, "K7QF2M");
    frame(&world);
    frame(&world);
    frame(&world);
    TIDE_CHECK(local.Lobby.page == Page_None && !local.Lobby.joining && plays());
    TIDE_CHECK(local.Lobby.mine == 1);
    // That match ends: back on the title, with a line that says so, and a match of its own asked for
    tide_local_disconnected(&local, TIDE_DISCONNECT_ENDED, "");
    tide_local_set_session(&local, TIDE_SESSION_OFFLINE, (tide_player_id){0}, 0, false, false, "");
    frame(NULL);
    frame(NULL);
    TIDE_CHECK(local.Lobby.page == Page_Title && says_something() && !plays());
    TIDE_CHECK(asked() == TIDE_REQUEST_START);
    tide_local_free(&local);

    // A code nobody has: the page it was typed on stays, to try again from
    machine(TIDE_SESSION_CONNECTING, -1, false, false, "");
    frame(NULL);
    tide_local_disconnected(&local, TIDE_DISCONNECT_FAILED, "");
    tide_local_set_session(&local, TIDE_SESSION_OFFLINE, (tide_player_id){0}, 0, false, false, "");
    frame(NULL);
    frame(NULL);
    TIDE_CHECK(local.Lobby.page == Page_Join && !local.Lobby.joining && says_something());
    TIDE_CHECK(asked() == TIDE_REQUEST_START);
    // This machine leaving a match, as when it starts or joins another, says nothing
    tide_local_disconnected(&local, TIDE_DISCONNECT_LEFT, "");
    frame(NULL);
    frame(NULL);
    TIDE_CHECK(local.Lobby.page == Page_Join && asked() == TIDE_REQUEST_NONE);
    tide_local_free(&local);
    tide_draw_free(&draw);
    tide_world_free(&world);
}

// A frame in which this machine's player holds what `held` says, and the tick its input goes to.
static tide_event_log heard;

static void tick_and_show(void)
{
    // As a host samples: what the GUI is using isn't the game's
    tide_devices sampled = devices;
    tide_gui_hide(&gui, &sampled);
    tide_world_set_input(&world, player(0), tide_input_sample(&sampled, &local));
    tide_devices_consume(&devices);
    tide_world_tick_events(&world, NULL, &heard);
    tide_local_events(&local, &heard);
    frame(&world);
}

static void key(tide_button *button, const bool down)
{
    button->down = down && !button->held;
    button->up = !down && button->held;
    button->pressed = button->held = down;
}

TIDE_TEST(towers_a_players_hand_steers_their_brick)
{
    start(false, 1);
    machine(TIDE_SESSION_CONNECTED, 0, true, true, "K7QF2M");
    for (int i = 0; i < 60 && !tide_has_Falling(&world, falling(&world, 0)); i++) tick_and_show();
    tick_and_show();
    TIDE_REQUIRE(tide_has_Falling(&world, falling(&world, 0)));
    const Falling dealt = tide_get_Falling(&world, falling(&world, 0));
    // The hand is on the brick that's falling, where it was dealt
    TIDE_CHECK(local.Hand.serial == dealt.serial && local.Hand.column == dealt.column && local.Hand.quarter == 0 && !local.Hand.drop);
    // The right arrow, pressed and let go, three times: three half cells
    for (int i = 0; i < 3; i++) {
        key(&devices.keyboard.rightArrow, true);
        tick_and_show();
        key(&devices.keyboard.rightArrow, false);
        tick_and_show();
    }
    TIDE_CHECK(local.Hand.column == dealt.column + 3);
    // Up turns it, a quarter each press
    key(&devices.keyboard.upArrow, true);
    tick_and_show();
    key(&devices.keyboard.upArrow, false);
    for (int i = 0; i < 6; i++) tick_and_show();
    TIDE_CHECK(local.Hand.quarter == 1);
    Falling now = tide_get_Falling(&world, falling(&world, 0));
    TIDE_CHECK(now.column == dealt.column + 3 && now.quarter == 1);
    // A drag steps it across by how far it goes: 66 pixels to the left is six half cells here
    devices.pointer.position = tide_f2(200, 200);
    key(&devices.pointer.press, true);
    tick_and_show();
    key(&devices.pointer.press, true);
    for (int i = 1; i <= 6; i++) {
        devices.pointer.position = tide_f2(200 - (float)i * 11, 200);
        tick_and_show();
    }
    TIDE_CHECK(local.Hand.column == dealt.column - 3 && local.Hand.quarter == 1);
    key(&devices.pointer.press, false);
    for (int i = 0; i < 12; i++) tick_and_show();
    TIDE_CHECK(tide_get_Falling(&world, falling(&world, 0)).column == dealt.column - 3);
    TIDE_CHECK(local.Hand.quarter == 1); // A drag that's let go isn't a tap
    // A tap turns it
    key(&devices.pointer.press, true);
    tick_and_show();
    key(&devices.pointer.press, false);
    tick_and_show();
    tick_and_show();
    TIDE_CHECK(local.Hand.quarter == 2 && tide_get_Falling(&world, falling(&world, 0)).quarter == 2);
    // Down drops it, and goes on dropping nothing once the next brick is there, until it's let go
    key(&devices.keyboard.downArrow, true);
    tick_and_show();
    key(&devices.keyboard.downArrow, true);
    tick_and_show();
    TIDE_CHECK(local.Hand.drop && tide_get_Box2D_Body(&world, falling(&world, 0)).velocity.y == -FAST);
    for (int i = 0; i < 200 && count(&world, tide_has_Brick) == 0; i++) tick_and_show();
    TIDE_REQUIRE(count(&world, tide_has_Brick) == 1);
    for (int i = 0; i < 60 && !tide_has_Falling(&world, falling(&world, 0)); i++) tick_and_show();
    for (int i = 0; i < 4; i++) tick_and_show();
    now = tide_get_Falling(&world, falling(&world, 0));
    TIDE_CHECK(now.serial != dealt.serial && local.Hand.serial == now.serial && local.Hand.quarter == 0);
    TIDE_CHECK(!local.Hand.drop && tide_get_Box2D_Body(&world, falling(&world, 0)).velocity.y == -FALL);
    key(&devices.keyboard.downArrow, false);
    tick_and_show();
    key(&devices.keyboard.downArrow, true);
    tick_and_show();
    tick_and_show();
    TIDE_CHECK(local.Hand.drop && tide_get_Box2D_Body(&world, falling(&world, 0)).velocity.y == -FAST);
    tide_event_log_free(&heard);
    tide_local_free(&local);
    tide_draw_free(&draw);
    tide_world_free(&world);
}

// The dust, the words and the banners come from what the match sends: a tick
// that tells this machine what it sent, and the frame that shows it.
TIDE_TEST(towers_effects_come_from_what_the_match_sends)
{
    start(false, 1);
    machine(TIDE_SESSION_CONNECTED, 0, true, true, "K7QF2M");
    frame(&world);
    frame(&world);
    TIDE_CHECK(tide_list_read_count(&local.heap, local.Fx.bits) == 0);
    for (int i = 0; i < 60 && !tide_has_Falling(&world, falling(&world, 0)); i++) tick_and_show();
    TIDE_CHECK(local.Fx.seat == 0);
    // The brick lands: dust under it, this machine's camera feels it, and the brick is lit
    key(&devices.keyboard.downArrow, true);
    for (int i = 0; i < 200 && count(&world, tide_has_Brick) == 0; i++) tick_and_show();
    key(&devices.keyboard.downArrow, false);
    tick_and_show();
    TIDE_REQUIRE(count(&world, tide_has_Brick) == 1);
    TIDE_CHECK(tide_list_read_count(&local.heap, local.Fx.bits) > 0 && local.Fx.trauma > 0.0f);
    bool lit = false;
    tide_entity landed = (tide_entity){0};
    for (uint32_t i = 0; i < world.entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&world.entities, i);
        if (!tide_has_Brick(&world, e)) continue;
        landed = e;
        if (tide_get_Brick_local(&local, e).lit > 0.0f) lit = true;
    }
    TIDE_CHECK(lit);
    // It's put off its tower: a word floats up from where it went
    Box2D_Body body = tide_get_Box2D_Body(&world, landed);
    body.position = tide_f2(home(0) + 5.0f, -1.0f);
    tide_set_Box2D_Body(&world, landed, body);
    bool labelled = false;
    for (int i = 0; i < 60 && !labelled; i++) {
        tick_and_show();
        labelled = tide_list_read_count(&local.heap, local.Fx.labels) > 0;
    }
    TIDE_CHECK(labelled && seat_of(&world, 0).lost == 1);
    // What the tower earned is used: the words over the world know what, and whose
    earn(0, Power_Quake);
    TIDE_REQUIRE(command(&world, TIDE_COMMAND_ID_Use, 0));
    for (int i = 0; i < 4; i++) tick_and_show();
    TIDE_CHECK(local.Flow.struck == Power_Quake && local.Flow.striker == 0 && local.Flow.cast < 0.2f);
    // A round ends, and the next starts: the words know who took it
    body = tide_get_Box2D_Body(&world, plinth(&world, 2));
    body.position.y = FINISH - DEPTH / 2;
    tide_set_Box2D_Body(&world, plinth(&world, 2), body);
    for (int i = 0; i < 400 && world.Towers.phase == Phase_Play; i++) tick_and_show();
    tick_and_show();
    TIDE_CHECK(world.Towers.phase == Phase_Over && local.Flow.winner == 2 && local.Flow.round == 1 && local.Flow.since < 0.1f);
    for (int i = 0; i < OVER + 10 && world.Towers.round == 1; i++) tick_and_show();
    tick_and_show();
    TIDE_CHECK(world.Towers.round == 2 && local.Flow.built == 2 && local.Flow.begun < 0.1f);
    tide_event_log_free(&heard);
    tide_local_free(&local);
    tide_draw_free(&draw);
    tide_world_free(&world);
}

// ---------------------------------------------------------------------------
// Three players in a match, over a network that's late and loses packets

typedef struct match match;

typedef struct seat_player {
    match *in;
    int who;
} seat_player;

struct match {
    tide_loopback *net;
    tide_server *server;
    tide_client *players[3];
    seat_player names[3];
    double now;
};

// What a player does at each tick of the match: they look at their own
// machine's world for the brick that's falling for them, as the game's view
// does, and put it where a list says, by how many bricks they've been dealt:
// flat and upright in turn, in the plinth's middle, with one now and then to a
// side. They drop it once it's over its place.
static void play(void *user, const uint32_t tick, void *input)
{
    (void)tick;
    const seat_player *me = user;
    Controls controls = {.hands = true};
    const tide_world *mine = tide_client_world(me->in->players[me->who]);
    const tide_player_id id = tide_client_status_of(me->in->players[me->who]).player;
    if (mine != NULL) {
        for (uint32_t i = 0; i < mine->entities.next_unused; i++) {
            const tide_entity e = tide_entity_in_slot(&mine->entities, i);
            if (!tide_has_Falling(mine, e) || !tide_has_Owner(mine, e) || tide_get_Owner(mine, e).player.id != id.id) continue;
            const Falling brick = tide_get_Falling(mine, e);
            const int dealt = brick.serial / SEATS;
            static const int across[6] = {0, 0, -2, 0, 2, 0};
            controls.serial = brick.serial;
            controls.column = across[(dealt + me->who) % 6];
            controls.quarter = (dealt + me->who) % 2;
            controls.drop = brick.column == controls.column && brick.quarter == controls.quarter && brick.age > 20 + me->who * 10;
        }
    }
    memcpy(input, &controls, sizeof controls);
}

static void advance(match *m)
{
    m->now += 1.0 / 60;
    tide_loopback_set_time(m->net, m->now);
    for (int i = 0; i < 3; i++) {
        if (m->players[i]) tide_client_update(m->players[i], m->now);
    }
    tide_server_update(m->server, m->now);
}

static void join(match *m, const int who)
{
    m->names[who] = (seat_player){m, who};
    m->players[who] = tide_client_create(&(tide_client_desc){.game = &tide_game_api,
                                           .transport = tide_loopback_endpoint(m->net, (uint32_t)who + 2),
                                           .server = tide_loopback_address(1), .lead = 2,
                                           .sample = play, .user = &m->names[who]}, m->now);
}

static int seat_with(const tide_world *w, const tide_client *client)
{
    const tide_player_id id = tide_client_status_of(client).player;
    for (int seat = 0; seat < SEATS; seat++) {
        const tide_entity e = plinth(w, seat);
        if (tide_has_Owner(w, e) && tide_get_Owner(w, e).player.id == id.id) return seat;
    }
    return -1;
}

static bool send(match *m, const int who, const uint32_t id)
{
    uint8_t bytes[TIDE_COMMAND_BYTES];
    const uint32_t size = plain_command(id, bytes);
    return tide_game_api.check_command(bytes, size, NULL) && tide_client_send_command(m->players[who], bytes, size);
}

TIDE_TEST(towers_three_players_stay_in_one_match_over_a_bad_network)
{
    b2t_forget();
    static match m;
    m = (match){.net = tide_loopback_create(4471)};
    tide_loopback_set_conditions(m.net, (tide_net_conditions){.latency = 0.04, .jitter = 0.015, .loss = 0.05});
    m.server = tide_server_create(&(tide_server_desc){.game = &tide_game_api, .tick_rate = 60,
                                    .transports = {tide_loopback_endpoint(m.net, 1)}}, m.now);
    join(&m, 0);
    for (int i = 0; i < 60; i++) advance(&m);
    join(&m, 1);
    for (int i = 0; i < 60; i++) advance(&m);
    TIDE_REQUIRE(tide_client_status_of(m.players[0]).state == TIDE_SESSION_CONNECTED);
    TIDE_REQUIRE(tide_client_status_of(m.players[1]).state == TIDE_SESSION_CONNECTED);
    const tide_world *authority = tide_server_world(m.server);
    TIDE_REQUIRE(authority != NULL);
    TIDE_CHECK(seat_with(authority, m.players[0]) == 0 && seat_with(authority, m.players[1]) == 1);

    // They build, and the two towers nobody has build by themselves. The
    // third player joins in the middle of it: the world it's sent has Box2D's
    // state in it, with every brick that's in the air. Their plinth is the
    // third seat's, with the tower that built itself there.
    bool used = false;
    uint32_t joined_at = 0;
    while (tide_server_tick(m.server) < 1500) {
        if (!m.players[2] && tide_server_tick(m.server) >= 900) {
            TIDE_CHECK(count(tide_server_world(m.server), tide_has_Brick) >= 20);
            join(&m, 2);
        }
        if (m.players[2] && joined_at == 0 && tide_client_status_of(m.players[2]).state == TIDE_SESSION_CONNECTED) {
            joined_at = tide_server_tick(m.server);
        }
        // A second after it's in, the one who joined late is no further ahead of what's confirmed than the others
        if (joined_at != 0 && tide_server_tick(m.server) == joined_at + 60) {
            const tide_client_status late = tide_client_status_of(m.players[2]);
            printf("    a second after joining, the third player is %u ticks ahead of what's confirmed\n",
                   late.predicted_tick - late.verified_tick);
            TIDE_CHECK(late.predicted_tick - late.verified_tick <= 15);
        }
        // The first player uses what their tower earned, once it has: a command, which the server gives a tick
        authority = tide_server_world(m.server);
        if (!used && seat_of(authority, 0).power != Power_None && tide_server_tick(m.server) > 700) used = send(&m, 0, TIDE_COMMAND_ID_Use);
        advance(&m);
    }
    authority = tide_server_world(m.server);
    TIDE_REQUIRE(m.players[2] != NULL && joined_at != 0);
    TIDE_REQUIRE(tide_client_status_of(m.players[2]).state == TIDE_SESSION_CONNECTED);
    TIDE_CHECK(seat_with(authority, m.players[2]) == 2);
    TIDE_CHECK(used);
    for (int seat = 0; seat < SEATS; seat++) TIDE_CHECK(seat_of(authority, seat).top > 3.0f && standing(authority, seat) >= 4);
    const uint64_t racing = tide_world_hash(authority);
    printf("    at tick 1500: the towers stand %.1f, %.1f, %.1f and %.1f m high, of %d bricks, %d of them down\n",
           (double)seat_of(authority, 0).top, (double)seat_of(authority, 1).top, (double)seat_of(authority, 2).top,
           (double)seat_of(authority, 3).top, count(authority, tide_has_Brick), bricks_down(authority));
    printf("    the third player was sent %llu bytes of the world it joined, whose physics are %d of them\n",
           (unsigned long long)tide_client_status_of(m.players[2]).world_bytes, authority->Box2D_Physics.size);
    printf("    the server's world at tick %u: 0x%016llx\n", tide_server_tick(m.server), (unsigned long long)racing);
    TIDE_CHECK(tide_server_tick(m.server) == 1500);
    TIDE_CHECK(racing == RACING_HASH);

    // The second player presses New game: a first round again
    TIDE_REQUIRE(send(&m, 1, TIDE_COMMAND_ID_Rebuild));
    while (tide_server_tick(m.server) < 1800) advance(&m);
    authority = tide_server_world(m.server);
    TIDE_CHECK(authority->Towers.round == 1 && count(authority, tide_has_Brick) < 20);

    // No machine's world ever went another way than the server's: what each
    // predicted wrong it ran again, and every tick it confirmed had the
    // server's hash, Box2D's state in it. The one who joined late too.
    for (int i = 0; i < 3; i++) {
        const tide_client_status status = tide_client_status_of(m.players[i]);
        printf("    player %d ran %u predicted ticks again, and was sent the world again %u times\n", i + 1,
               status.again_ticks, status.resyncs);
        TIDE_CHECK(status.state == TIDE_SESSION_CONNECTED && status.resyncs == 0 && status.again_ticks > 0);
        const tide_world *confirmed = tide_client_verified_world(m.players[i]);
        TIDE_CHECK(confirmed != NULL && confirmed->Towers.round == 1);
    }

    const uint64_t hash = tide_world_hash(authority);
    printf("    the server's world at tick %u: 0x%016llx\n", tide_server_tick(m.server), (unsigned long long)hash);
    TIDE_CHECK(tide_server_tick(m.server) == 1800);
    TIDE_CHECK(hash == MATCH_HASH);

    for (int i = 2; i >= 0; i--) tide_client_destroy(m.players[i]);
    tide_server_destroy(m.server);
    tide_loopback_destroy(m.net);
}

// What a tick costs, which is what a client that predicts pays several times
// a frame: late in a round with nothing falling, in play, and with every
// tower coming down at once. Timings aren't checked: machines differ too much.
static void cost(const char *what, const int ticks)
{
    tide_world kept = {0};
    uint64_t ticking = 0;
    uint64_t keeping = 0;
    uint64_t worst = 0;
    int most = 0;
    int stepped = 0;
    for (int i = 0; i < ticks; i++) {
        // A client keeps each tick's world, and hashes it
        const uint64_t began = tide_time_now_ns();
        tide_world_tick(&world);
        const uint64_t ticked = tide_time_now_ns();
        if (world.Box2D_Physics.moved) stepped++;
        tide_world_copy(&kept, &world);
        (void)tide_world_hash(&world);
        ticking += ticked - began;
        keeping += tide_time_now_ns() - ticked;
        if (ticked - began > worst) worst = ticked - began;
        const int bodies = count(&world, tide_has_Box2D_Body);
        if (bodies > most) most = bodies;
    }
    printf("    %s, %d bodies: %.3f ms a tick (%.3f the longest), and %.3f to keep and hash its world (%d bytes of physics), "
           "the world stepped in %d ticks of %d\n",
           what, most, (double)ticking / ticks / 1e6, (double)worst / 1e6, (double)keeping / ticks / 1e6, world.Box2D_Physics.size, stepped,
           ticks);
    tide_world_free(&kept);
}

// What a guess that was wrong costs: ten ticks run again from the world as it
// was, each kept and hashed, with Box2D's world made again from the match's
// image of it first. The least of twenty goes.
static void again(const char *what)
{
    tide_world was = {0};
    tide_world replay = {0};
    tide_world kept = {0};
    uint64_t least = UINT64_MAX;
    for (int round = 0; round < 20; round++) {
        tide_world_copy(&was, &world);
        for (int i = 0; i < 10; i++) tide_world_tick(&world);
        const uint64_t began = tide_time_now_ns();
        tide_world_copy(&replay, &was);
        for (int i = 0; i < 10; i++) {
            tide_world_tick(&replay);
            tide_world_copy(&kept, &replay);
            (void)tide_world_hash(&replay);
        }
        const uint64_t took = tide_time_now_ns() - began;
        if (took < least) least = took;
    }
    printf("    %s: ten ticks run again take %.2f ms\n", what, (double)least / 1e6);
    tide_world_free(&kept);
    tide_world_free(&replay);
    tide_world_free(&was);
}

TIDE_TEST(towers_costs)
{
    start(true, 0);
    // Until a tower is most of the way to the line
    int ticks = 0;
    float tallest = 0;
    while (tallest < FINISH - 8 && ticks < 30000 && world.Towers.phase == Phase_Play) {
        tide_world_tick(&world);
        ticks++;
        for (int seat = 0; seat < SEATS; seat++) {
            if (seat_of(&world, seat).top > tallest) tallest = seat_of(&world, seat).top;
        }
    }
    int set = 0;
    for (uint32_t i = 0; i < world.entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&world.entities, i);
        if (tide_has_Brick(&world, e) && tide_get_Brick(&world, e).set) set++;
    }
    printf("    %.0f seconds into a round, %d bricks are on the towers, %d of them set solid\n", ticks / 60.0,
           standing(&world, 0) + standing(&world, 1) + standing(&world, 2) + standing(&world, 3), set);
    cost("late in a round, the towers building", 300);
    again("the towers building");
    // The same world with the bots off: what's falling lands, what fell is cleared, and everything comes to rest
    tide_world playing = {0};
    tide_world_copy(&playing, &world);
    world.Towers.bots = false;
    tick(RUBBLE + 400);
    // ...which is once nothing has stepped for a second with no brick left on the ground: one that's cleared from
    // there wakes what lay on it
    for (int i = 0, quiet = 0; i < 3000 && quiet < 60; i++) {
        tide_world_tick(&world);
        bool fallen = false;
        for (uint32_t slot = 0; slot < world.entities.next_unused && !fallen; slot++) {
            const tide_entity e = tide_entity_in_slot(&world.entities, slot);
            fallen = tide_has_Brick(&world, e) && tide_get_Brick(&world, e).down;
        }
        quiet = world.Box2D_Physics.moved || fallen ? 0 : quiet + 1;
    }
    cost("the same towers at rest", 300);
    // And with the round over: every plinth sinks, and every tower comes down
    tide_world_copy(&world, &playing);
    for (int seat = 0; seat < SEATS; seat++) {
        Seat over = seat_of(&world, seat);
        over.above = HOLD;
        over.top = FINISH + 1;
        tide_set_Seat(&world, plinth(&world, seat), over);
    }
    world.Towers.bots = false;
    Box2D_Body body = tide_get_Box2D_Body(&world, plinth(&world, 0));
    body.position.y += FINISH;
    tide_set_Box2D_Body(&world, plinth(&world, 0), body);
    for (int i = 0; i < HOLD + 30 && world.Towers.phase == Phase_Play; i++) tide_world_tick(&world);
    cost("the round over, every tower coming down", 150);
    again("every tower coming down");
    tide_world_free(&playing);
    tide_world_free(&world);

    // And what a player who joins late in a round is sent: the world, with Box2D's state in it
    b2t_forget();
    static match m;
    m = (match){.net = tide_loopback_create(77)};
    m.server = tide_server_create(&(tide_server_desc){.game = &tide_game_api, .tick_rate = 60,
                                    .transports = {tide_loopback_endpoint(m.net, 1)}}, m.now);
    while (tide_server_tick(m.server) < 4500) advance(&m);
    const int physics = ((const tide_world *)tide_server_world(m.server))->Box2D_Physics.size;
    join(&m, 0);
    for (int i = 0; i < 600 && tide_client_status_of(m.players[0]).state != TIDE_SESSION_CONNECTED; i++) advance(&m);
    TIDE_CHECK(tide_client_status_of(m.players[0]).state == TIDE_SESSION_CONNECTED);
    printf("    a player who joins 75 seconds into a round is sent %llu bytes of the world, whose physics are %d of them\n",
           (unsigned long long)tide_client_status_of(m.players[0]).world_bytes, physics);
    tide_client_destroy(m.players[0]);
    tide_server_destroy(m.server);
    tide_loopback_destroy(m.net);
}
