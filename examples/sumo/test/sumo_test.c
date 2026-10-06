// Sumo with no window: what the game promises, on one world; what a machine
// shows of it and where its lobby leaves it, a frame at a time; and four
// players in a match on the loopback network, two of whom join in the middle
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

// The package's bridge: C's worlds, which a test starts without, and what it
// says of the images it wrote and the worlds it made again
void b3t_forget(void);
void b3t_images(int32_t *whole, int32_t *in_parts);
int32_t b3t_restores(void);
void b3t_measure(uint64_t *step, uint64_t *image);

#define DT (1.0f / 60.0f)
#define TILES 113      // The floor's, as a round starts
#define READY 180      // Ticks before a round starts
#define ROUND 7200     // ...and that it lasts
#define RESPAWN 120
#define COOLDOWN 150
#define HEAVY 480
#define BALL 0.8f
#define BIG 1.3f

// The server's world in the match below, on every platform and in every
// configuration: Box3D's math, its image and the netcode, byte for byte.
// Once in the middle of a round, and once after a new game was asked for.
#define BRAWL_HASH UINT64_C(0xb0b70507a8d9fd3b)
#define MATCH_HASH UINT64_C(0x1fe89cc6f8a5ae87)

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

// A seat's ball, or no entity
static tide_entity ball_of(const tide_world *w, const int seat)
{
    for (uint32_t i = 0; i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        if (tide_has_Ball(w, e) && tide_get_Ball(w, e).seat == seat) return e;
    }
    return (tide_entity){0};
}

static Seat seat_of(const tide_world *w, const int seat)
{
    for (uint32_t i = 0; i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        if (tide_has_Seat(w, e) && tide_get_Seat(w, e).seat == seat) return tide_get_Seat(w, e);
    }
    return (Seat){.seat = -1};
}

static int crates_of(const tide_world *w, const Kind kind)
{
    int found = 0;
    for (uint32_t i = 0; i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        if (tide_has_Crate(w, e) && tide_get_Crate(w, e).kind == kind) found++;
    }
    return found;
}

// How many crates have been knocked about: they break up soon
static int knocked(const tide_world *w)
{
    int found = 0;
    for (uint32_t i = 0; i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        if (tide_has_Crate(w, e) && tide_get_Crate(w, e).until != 0) found++;
    }
    return found;
}

// How far a crate is from where a round put it, the furthest of them.
static float disturbed(const tide_world *w, const tide_world *built)
{
    float most = 0;
    for (uint32_t i = 0; i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        if (!tide_has_Crate(w, e) || !tide_has_Crate(built, e)) continue;
        const tide_float3 now = tide_get_Box3D_Body(w, e).position;
        const tide_float3 then = tide_get_Box3D_Body(built, e).position;
        const float moved = tide_abs_f(now.x - then.x) + tide_abs_f(now.y - then.y) + tide_abs_f(now.z - then.z);
        if (moved > most) most = moved;
    }
    return most;
}

static float flat(const tide_float3 v)
{
    return tide_sqrt_f(v.x * v.x + v.z * v.z);
}

static void tick(const int ticks)
{
    for (int i = 0; i < ticks; i++) tide_world_tick(&world);
}

// A match of this many players, with bots or without, as its first round
// starts: the ring is raised, what's stacked has come to rest, and the count
// is over.
static void start(const int players, const bool bots)
{
    b3t_forget();
    tide_world_free(&world);
    tide_world_init(&world, DT);
    world.Ring.bots = bots;
    for (int i = 0; i < players; i++) tide_world_player_joined(&world, player(i));
    tick(READY + 2);
}

// Everything that's stacked goes: a body whose position is written is put
// there, and the pit takes what's in it.
static void clear_the_ring(void)
{
    for (uint32_t i = 0; i < world.entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&world.entities, i);
        if (!tide_has_Crate(&world, e)) continue;
        Box3D_Body body = tide_get_Box3D_Body(&world, e);
        body.position.y = -20.0f;
        tide_set_Box3D_Body(&world, e, body);
    }
    tick(3);
}

static void hold(const int who, const float x, const float y, const bool charge)
{
    tide_world_set_input(&world, player(who), (Controls){.move = {x, y}, .charge = charge});
}

// A ball put somewhere, still.
static void put(const int seat, const float x, const float y, const float z)
{
    const tide_entity ball = ball_of(&world, seat);
    Box3D_Body body = tide_get_Box3D_Body(&world, ball);
    body.position = tide_f3(x, y, z);
    body.velocity = tide_f3(0, 0, 0);
    body.angularVelocity = tide_f3(0, 0, 0);
    tide_set_Box3D_Body(&world, ball, body);
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
    return tide_game_api.command(w, player(sender), tide_game_api.command_sequence(w, player(sender)) + 1, bytes, size);
}

TIDE_TEST(sumo_raises_a_ring_that_stands)
{
    // Each of the three arrangements: thousands of crates, which come to rest where they were stacked
    for (int round = 1; round <= 3; round++) {
        b3t_forget();
        tide_world_free(&world);
        tide_world_init(&world, DT);
        world.Ring.round = round - 1;
        tide_world_player_joined(&world, player(0));
        tick(2);
        tide_world built = {0};
        tide_world_copy(&built, &world);
        TIDE_CHECK(world.Ring.round == round && world.Ring.phase == Phase_Ready);
        TIDE_CHECK(count(&world, tide_has_Tile) == TILES);
        const int crates = count(&world, tide_has_Crate);
        TIDE_CHECK(crates > 2000 && count(&world, tide_has_Keg) >= 8 && crates_of(&world, Kind_Plank) > 40);
        int rested = -1;
        for (int i = 0; i < READY && rested < 0; i++) {
            tide_world_tick(&world);
            if (!world.Box3D_Physics.moved) rested = i;
        }
        printf("    round %d: %d crates and planks, %d of them kegs, on %d tiles: at rest after %d ticks, the furthest %.2f m from where"
               " it was put; %d bytes of physics\n",
               round, crates, count(&world, tide_has_Keg), count(&world, tide_has_Tile), rested, (double)disturbed(&world, &built),
               world.Box3D_Physics.size);
        // Before the round starts, with nothing knocked
        TIDE_CHECK(rested >= 0 && rested < READY - 30);
        TIDE_CHECK(disturbed(&world, &built) < 0.3f && knocked(&world) == 0);
        // The balls wait where their seats start: bots have the three seats nobody has
        TIDE_CHECK(count(&world, tide_has_Ball) == 4 && count(&world, tide_has_Seat) == 8);
        const tide_float3 first = tide_get_Box3D_Body(&world, ball_of(&world, 0)).position;
        TIDE_CHECK(tide_abs_f(first.x - 3.0f) < 0.05f && tide_abs_f(first.z + 12.0f) < 0.05f && first.y < BALL + 0.05f);
        tide_world_free(&built);
    }
    tide_world_free(&world);
}

TIDE_TEST(sumo_seats_its_players_and_fills_the_ring_with_bots)
{
    start(1, true);
    TIDE_CHECK(world.Ring.phase == Phase_Play && world.Ring.manned == 1);
    // One player, and bots in the next three seats
    TIDE_CHECK(count(&world, tide_has_Ball) == 4);
    TIDE_CHECK(tide_get_Owner(&world, ball_of(&world, 0)).player.id == player(0).id);
    TIDE_CHECK(tide_get_Owner(&world, ball_of(&world, 1)).player.id != player(0).id);
    // A second player has the first seat no player has, and the bot's ball with it, as it is
    const tide_entity bots = ball_of(&world, 1);
    tide_world_player_joined(&world, player(5));
    tick(2);
    TIDE_CHECK(world.Ring.manned == 3 && count(&world, tide_has_Ball) == 4);
    TIDE_CHECK(ball_of(&world, 1).index == bots.index && tide_get_Owner(&world, bots).player.id == player(5).id);
    // Five players: the ring has its crowd, and no bot is left
    for (int i = 1; i <= 3; i++) tide_world_player_joined(&world, player(i));
    tick(RESPAWN + 4);
    TIDE_CHECK(world.Ring.manned == 31 && count(&world, tide_has_Ball) == 5);
    // One leaves: four are a crowd still, and their ball goes with them. Three more do: bots keep the ring at four
    tide_world_player_left(&world, player(5));
    tick(2);
    TIDE_CHECK(world.Ring.manned == 29 && count(&world, tide_has_Ball) == 4 && !tide_has_Ball(&world, ball_of(&world, 1)));
    for (int i = 1; i <= 3; i++) tide_world_player_left(&world, player(i));
    tick(4);
    TIDE_CHECK(world.Ring.manned == 1 && count(&world, tide_has_Ball) == 4);
    TIDE_CHECK(!tide_has_Ball(&world, ball_of(&world, 4)));
    // Bots off: the player's ball is alone. And on again
    TIDE_REQUIRE(command(&world, TIDE_COMMAND_ID_Bots, 0));
    tick(3);
    TIDE_CHECK(!world.Ring.bots && count(&world, tide_has_Ball) == 1);
    TIDE_REQUIRE(command(&world, TIDE_COMMAND_ID_Bots, 0));
    tick(4);
    TIDE_CHECK(world.Ring.bots && count(&world, tide_has_Ball) == 4);
    tide_world_free(&world);
}

TIDE_TEST(sumo_a_ball_is_driven_by_pushes)
{
    start(1, false);
    clear_the_ring();
    put(0, 0, BALL, -6);
    tick(2);
    const tide_entity ball = ball_of(&world, 0);
    const tide_float3 from = tide_get_Box3D_Body(&world, ball).position;
    // It answers in the tick its player pressed: the push is this tick's step's
    hold(0, 0, 1, false);
    tick(1);
    Box3D_Body body = tide_get_Box3D_Body(&world, ball);
    TIDE_CHECK(body.velocity.z > 0.2f && body.position.z > from.z);
    // It's never put anywhere: it gathers speed, up to what the floor's drag allows, and rolls as it goes
    float before = body.velocity.z;
    bool faster = true;
    for (int i = 0; i < 20; i++) {
        tide_world_tick(&world);
        body = tide_get_Box3D_Body(&world, ball);
        faster = faster && body.velocity.z > before;
        before = body.velocity.z;
    }
    TIDE_CHECK(faster);
    tick(70);
    body = tide_get_Box3D_Body(&world, ball);
    printf("    a ball that's driven for a second and a half goes %.1f m/s, turning at %.1f radians a second\n", (double)body.velocity.z,
           (double)body.angularVelocity.x);
    TIDE_CHECK(body.velocity.z > 9.0f && body.velocity.z < 13.5f && tide_abs_f(body.velocity.x) < 0.05f);
    TIDE_CHECK(body.angularVelocity.x > 8.0f);
    // Let go, it rolls on, slowing; pushed the other way, it stops much sooner
    hold(0, 0, 0, false);
    tick(30);
    const float coasting = tide_get_Box3D_Body(&world, ball).velocity.z;
    TIDE_CHECK(coasting > 5.0f && coasting < body.velocity.z);
    hold(0, 0, -1, false);
    tick(20);
    TIDE_CHECK(tide_get_Box3D_Body(&world, ball).velocity.z < 0.5f);
    // A player whose hands are off drives nothing: the ball plays by itself, with nobody to go for
    tide_world_set_input(&world, player(0), (Controls){.move = {1, 0}, .away = true});
    tick(30);
    TIDE_CHECK(tide_get_Ball(&world, ball).drive.x < 0.99f);
    tide_world_free(&world);
}

TIDE_TEST(sumo_a_charge_winds_up_and_bursts)
{
    start(1, false);
    clear_the_ring();
    put(0, 0, BALL, -8);
    tick(2);
    const tide_entity ball = ball_of(&world, 0);
    // Held, it winds up, and the ball drives less hard meanwhile
    hold(0, 0, 1, true);
    tick(24);
    Ball state = tide_get_Ball(&world, ball);
    TIDE_CHECK(state.winding && state.charge > 0.45f && state.charge < 0.55f);
    tick(40);
    state = tide_get_Ball(&world, ball);
    const float wound = tide_get_Box3D_Body(&world, ball).velocity.z;
    TIDE_CHECK(state.charge == 1.0f && wound < 9.0f);
    // Let go: one impulse, the way it's driven
    hold(0, 0, 1, false);
    tick(1);
    state = tide_get_Ball(&world, ball);
    const float burst = tide_get_Box3D_Body(&world, ball).velocity.z;
    printf("    a charge wound all the way up takes a ball from %.1f m/s to %.1f\n", (double)wound, (double)burst);
    TIDE_CHECK(burst > wound + 14.0f && !state.winding && state.charge == 0.0f);
    TIDE_CHECK(state.cooldown == COOLDOWN && state.dash > 0);
    // Not again until it's ready: held, nothing winds, and the ball is only driven, back the way it came here
    hold(0, 0, -1, true);
    tick(60);
    TIDE_CHECK(!tide_get_Ball(&world, ball).winding);
    hold(0, 0, 0, false);
    tick(COOLDOWN - 60);
    TIDE_REQUIRE(tide_has_Ball(&world, ball) && tide_get_Ball(&world, ball).cooldown == 0);
    hold(0, 1, 0, true);
    tick(10);
    TIDE_CHECK(tide_get_Ball(&world, ball).winding);
    // A tap is a burst too, a small one, across the ring this time
    const float across = tide_get_Box3D_Body(&world, ball).velocity.x;
    hold(0, 1, 0, false);
    tick(1);
    TIDE_CHECK(tide_get_Box3D_Body(&world, ball).velocity.x > across + 6.0f);
    tide_world_free(&world);
}

TIDE_TEST(sumo_a_ring_out_scores_for_who_touched_last)
{
    start(2, false);
    clear_the_ring();
    // The second player's ball near the rim, and the first's coming at it from inside, charged
    put(1, 0, BALL, 17);
    put(0, 0, BALL, 7);
    hold(0, 0, 1, true);
    tick(50);
    hold(0, 0, 1, false);
    int fell = -1;
    for (int i = 0; i < 240 && fell < 0; i++) {
        tide_world_tick(&world);
        if (!tide_has_Ball(&world, ball_of(&world, 1))) fell = i;
        // Once it has hit, the first one holds back from the rim itself
        if (tide_has_Ball(&world, ball_of(&world, 1)) && tide_get_Ball(&world, ball_of(&world, 1)).last == 0) hold(0, 0, -1, false);
    }
    printf("    a charged ball threw another over the rim in %d ticks\n", fell);
    TIDE_REQUIRE(fell >= 0);
    tick(1);
    TIDE_CHECK(seat_of(&world, 0).score == 1 && seat_of(&world, 0).round == 1 && seat_of(&world, 1).score == 0);
    TIDE_CHECK(!seat_of(&world, 1).up && seat_of(&world, 1).wait > RESPAWN - 5);
    TIDE_CHECK(tide_has_Ball(&world, ball_of(&world, 0)));
    // It's back a moment later, dropped from over the ring, and nobody's to blame for it yet
    hold(0, 0, 0, false);
    tick(RESPAWN + 2);
    const tide_entity back = ball_of(&world, 1);
    TIDE_REQUIRE(tide_has_Ball(&world, back));
    TIDE_CHECK(tide_get_Box3D_Body(&world, back).position.y > 6.0f && tide_get_Ball(&world, back).last == -1);
    TIDE_CHECK(seat_of(&world, 1).up);
    tick(120);
    TIDE_CHECK(tide_get_Box3D_Body(&world, back).position.y < 1.0f);
    // A ball that rolls off by itself scores for nobody
    hold(0, 0, 0, false);
    put(0, 0, BALL, 0);
    put(1, 17, BALL, 0);
    hold(1, 1, 0, false);
    tick(120);
    TIDE_CHECK(!seat_of(&world, 1).up && seat_of(&world, 0).score == 1 && seat_of(&world, 1).score == 0);
    tide_world_free(&world);
}

TIDE_TEST(sumo_two_balls_that_meet_are_thrown_apart)
{
    start(2, false);
    clear_the_ring();
    put(0, -4, BALL, 0);
    put(1, 2, BALL, 0);
    tick(2);
    hold(0, 1, 0, false);
    float closing = 0;
    float thrown = 0;
    int dazed = 0;
    for (int i = 0; i < 120; i++) {
        tide_world_tick(&world);
        const Box3D_Body mine = tide_get_Box3D_Body(&world, ball_of(&world, 0));
        const Box3D_Body theirs = tide_get_Box3D_Body(&world, ball_of(&world, 1));
        if (theirs.velocity.x < 0.01f) closing = mine.velocity.x;
        if (theirs.velocity.x > thrown) thrown = theirs.velocity.x;
        if (tide_get_Ball(&world, ball_of(&world, 1)).dazed > dazed) dazed = tide_get_Ball(&world, ball_of(&world, 1)).dazed;
    }
    printf("    a ball going %.1f m/s threw one that stood still off at %.1f, without its grip for %d ticks\n", (double)closing,
           (double)thrown, dazed);
    // Faster than what hit it: what Box3D makes of the hit, and the bump on top
    TIDE_CHECK(closing > 8.0f && thrown > closing && dazed > 10);
    // Each knows who touched it
    TIDE_CHECK(tide_get_Ball(&world, ball_of(&world, 0)).last == 1 && tide_get_Ball(&world, ball_of(&world, 1)).last == 0);
    tide_world_free(&world);
}

TIDE_TEST(sumo_the_floor_falls_away_from_the_rim_in)
{
    start(1, false);
    const int crates = count(&world, tide_has_Crate);
    put(0, 0, 12, 0); // On the pyramid in the middle, out of the way
    // The first rim is marked five seconds before it goes, and goes after 35 seconds
    while (world.Ring.timer > ROUND - 2100 + 301) tick(1);
    TIDE_CHECK(world.Ring.next == world.Ring.radius && world.Ring.gone == 0);
    tick(2);
    TIDE_CHECK(world.Ring.next < world.Ring.radius);
    tick(300);
    TIDE_CHECK(world.Ring.gone == 1 && world.Ring.radius == world.Ring.next);
    // Its tiles let go one after another: each static tile is gone, and a loose one falls in its place
    tick(20);
    const int falling = crates_of(&world, Kind_Slab);
    TIDE_CHECK(falling > 0 && count(&world, tide_has_Tile) > 81 && count(&world, tide_has_Tile) < TILES);
    tick(240);
    printf("    after the first rim: %d tiles of %d, and %d crates of %d\n", count(&world, tide_has_Tile), TILES, count(&world, tide_has_Crate),
           crates);
    // The pit took them, and what stood on them
    TIDE_CHECK(count(&world, tide_has_Tile) == 81 && crates_of(&world, Kind_Slab) == 0);
    TIDE_CHECK(count(&world, tide_has_Crate) < crates - 100);
    // All three: the floor that's left is the middle's 37 tiles
    while (world.Ring.timer > 200) tick(1);
    TIDE_CHECK(world.Ring.gone == 3 && count(&world, tide_has_Tile) == 37);
    // A ball on a tile that goes falls with it
    tide_world_free(&world);
    start(1, false);
    put(0, 6, BALL, -15); // A tile the first rim takes
    while (world.Ring.timer > ROUND - 2100) tick(1);
    TIDE_CHECK(seat_of(&world, 0).up);
    int fell = -1;
    for (int i = 0; i < 240 && fell < 0; i++) {
        tide_world_tick(&world);
        if (!seat_of(&world, 0).up) fell = i;
    }
    printf("    a ball on a tile of the first rim was out %d ticks after the rim went\n", fell);
    TIDE_CHECK(fell >= 0);
    tide_world_free(&world);
}

// A pickup comes, and is made to be this power: where it is.
static tide_float3 drop(const Power power)
{
    world.Ring.drop = 1;
    tick(2);
    world.Ring.drop = 100000;
    tide_float3 at = {0};
    for (uint32_t i = 0; i < world.entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&world.entities, i);
        if (!tide_has_Pickup(&world, e)) continue;
        Pickup pickup = tide_get_Pickup(&world, e);
        pickup.power = power;
        tide_set_Pickup(&world, e, pickup);
        at = tide_get_Box3D_Body(&world, e).position;
    }
    return at;
}

TIDE_TEST(sumo_a_heavy_ball_is_another_body)
{
    start(2, false);
    clear_the_ring();
    TIDE_CHECK(count(&world, tide_has_Pickup) == 0);
    const tide_entity small = ball_of(&world, 0);
    const tide_float3 where = drop(Power_Heavy);
    put(0, where.x, BALL, where.z);
    tick(3);
    // The pickup is gone, and the ball that took it is a bigger, heavier one: a body's shape is read once
    const tide_entity big = ball_of(&world, 0);
    TIDE_CHECK(count(&world, tide_has_Pickup) == 0 && count(&world, tide_has_Ball) == 2);
    TIDE_REQUIRE(tide_has_Ball(&world, big) && !tide_has_Ball(&world, small));
    Ball ball = tide_get_Ball(&world, big);
    TIDE_CHECK(ball.heavy > HEAVY - 5 && ball.radius == BIG && ball.mass > 8000.0f);
    TIDE_CHECK(tide_get_Box3D_Sphere(&world, big).radius == BIG && seat_of(&world, 0).up);
    TIDE_CHECK(tide_get_Owner(&world, big).player.id == player(0).id);
    // It answers its player as before, and the other ball is nothing to it
    const tide_float3 at = tide_get_Box3D_Body(&world, big).position;
    put(1, at.x + 6, BALL, at.z);
    hold(0, 1, 0, false);
    float thrown = 0;
    float slowed = 100;
    for (int i = 0; i < 90; i++) {
        tide_world_tick(&world);
        if (!tide_has_Ball(&world, ball_of(&world, 1))) break;
        const float theirs = tide_get_Box3D_Body(&world, ball_of(&world, 1)).velocity.x;
        if (theirs > thrown) thrown = theirs;
        if (thrown > 0.5f && tide_get_Box3D_Body(&world, big).velocity.x < slowed) slowed = tide_get_Box3D_Body(&world, big).velocity.x;
    }
    printf("    a heavy ball threw an ordinary one off at %.1f m/s, and went on at %.1f\n", (double)thrown, (double)slowed);
    TIDE_CHECK(thrown > 12.0f && slowed > 4.0f);
    // After eight seconds it's its usual size again, where it was and as fast
    hold(0, 0, 0, false);
    tick(HEAVY);
    const tide_entity again = ball_of(&world, 0);
    TIDE_REQUIRE(tide_has_Ball(&world, again) && !tide_has_Ball(&world, big));
    ball = tide_get_Ball(&world, again);
    TIDE_CHECK(ball.heavy == 0 && ball.radius == BALL && tide_get_Box3D_Sphere(&world, again).radius == BALL);
    tide_world_free(&world);
}

TIDE_TEST(sumo_a_shock_throws_everything_near)
{
    start(2, false);
    // The other ball close by, between the pickup and the rim
    const tide_float3 at = drop(Power_Shock);
    const tide_float3 out = tide_f3(at.x / flat(at), 0, at.z / flat(at));
    put(1, at.x + out.x * 2.5f, BALL, at.z + out.z * 2.5f);
    put(0, at.x, BALL, at.z);
    int near = 0;
    for (uint32_t i = 0; i < world.entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&world.entities, i);
        if (!tide_has_Crate(&world, e)) continue;
        const tide_float3 crate = tide_get_Box3D_Body(&world, e).position;
        if (flat(tide_f3(crate.x - at.x, 0, crate.z - at.z)) < 9.0f) near++;
    }
    tick(4);
    int thrown = 0;
    for (uint32_t i = 0; i < world.entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&world.entities, i);
        if (!tide_has_Crate(&world, e)) continue;
        const Box3D_Body body = tide_get_Box3D_Body(&world, e);
        const tide_float3 away = tide_f3(body.position.x - at.x, 0, body.position.z - at.z);
        if (flat(away) < 9.0f && body.velocity.x * away.x + body.velocity.z * away.z > 0 && flat(body.velocity) > 1.0f) thrown++;
    }
    const Box3D_Body mine = tide_get_Box3D_Body(&world, ball_of(&world, 0));
    const Box3D_Body theirs = tide_get_Box3D_Body(&world, ball_of(&world, 1));
    printf("    a shock threw %d of the %d crates within its reach outward, and the other ball off at %.1f m/s\n", thrown, near,
           (double)flat(theirs.velocity));
    TIDE_CHECK(near > 20 && thrown > near / 2);
    // The ball that set it off is left alone, and the other knows whose blast it was
    TIDE_CHECK(flat(mine.velocity) < 1.0f && theirs.velocity.x * out.x + theirs.velocity.z * out.z > 6.0f);
    TIDE_CHECK(tide_get_Ball(&world, ball_of(&world, 1)).last == 0);
    tide_world_free(&world);
}

TIDE_TEST(sumo_kegs_blow_up_one_after_another)
{
    start(1, false);
    // A keg, and the one beside it
    tide_entity keg = {0};
    for (uint32_t i = 0; i < world.entities.next_unused && !tide_has_Keg(&world, keg); i++) {
        const tide_entity e = tide_entity_in_slot(&world.entities, i);
        if (tide_has_Keg(&world, e)) keg = e;
    }
    TIDE_REQUIRE(tide_has_Keg(&world, keg));
    const int kegs = count(&world, tide_has_Keg);
    const tide_float3 at = tide_get_Box3D_Body(&world, keg).position;
    // A ball that only leans on it does nothing
    const tide_float3 out = tide_f3(at.x / flat(at), 0, at.z / flat(at));
    put(0, at.x - out.x * 1.3f, BALL, at.z - out.z * 1.3f);
    tick(20);
    TIDE_CHECK(count(&world, tide_has_Keg) == kegs);
    // One that hits it hard sets it off, and its blast lights the next one's fuse
    put(0, at.x - out.x * 5.0f, BALL, at.z - out.z * 5.0f);
    hold(0, out.x, out.z, false);
    int first = -1;
    int second = -1;
    for (int i = 0; i < 120 && second < 0; i++) {
        tide_world_tick(&world);
        if (first < 0 && count(&world, tide_has_Keg) == kegs - 1) first = i;
        if (count(&world, tide_has_Keg) <= kegs - 2) second = i;
    }
    printf("    a keg went off after %d ticks, and the one beside it %d ticks later\n", first, second - first);
    TIDE_CHECK(first >= 0 && second > first && second - first < 30);
    // What stood near was thrown, and breaks up
    TIDE_CHECK(knocked(&world) > 0);
    tide_world_free(&world);
}

TIDE_TEST(sumo_rubble_breaks_up)
{
    start(1, false);
    const int crates = count(&world, tide_has_Crate);
    // Into what stands in from where it starts
    hold(0, 0.5f, 1, true);
    tick(40);
    hold(0, 0.5f, 1, false);
    tick(70);
    hold(0, 0, 0, false);
    const int hit = knocked(&world);
    printf("    a ball knocked %d crates about\n", hit);
    TIDE_CHECK(hit > 10 && count(&world, tide_has_Crate) > crates - 4);
    // A few seconds later they're gone, and nothing else is
    tick(420);
    printf("    %d of %d crates are left, %d of them knocked since\n", count(&world, tide_has_Crate), crates, knocked(&world));
    TIDE_CHECK(count(&world, tide_has_Crate) <= crates - hit + 4 && count(&world, tide_has_Crate) > crates - hit - 120);
    tide_world_free(&world);
}

TIDE_TEST(sumo_bots_stay_on_and_fight_and_the_next_round_is_raised)
{
    start(1, true);
    // The player's hands are off for the whole round: their ball plays by itself too
    int outs = 0;
    int early = 0;
    bool up[4] = {true, true, true, true};
    int over = -1;
    for (int i = 0; i < ROUND + 60 && over < 0; i++) {
        tide_world_set_input(&world, player(0), (Controls){.away = true});
        tide_world_tick(&world);
        for (int seat = 0; seat < 4; seat++) {
            const bool now = seat_of(&world, seat).up;
            if (up[seat] && !now) {
                outs++;
                if (i < 1500) early++;
            }
            up[seat] = now;
        }
        if (world.Ring.phase == Phase_Over) over = i;
    }
    int scored = 0;
    int best = 0;
    for (int seat = 0; seat < 4; seat++) {
        scored += seat_of(&world, seat).score;
        if (seat_of(&world, seat).round > best) best = seat_of(&world, seat).round;
    }
    printf("    a round of four bots: %d balls went over the rim, %d of them thrown out by another, %d in the first 25 seconds; the best"
           " threw out %d\n",
           outs, scored, early, best);
    TIDE_REQUIRE(over >= 0);
    // They fight: balls are thrown out. And they stay on: while the whole floor is there, hardly any fall
    TIDE_CHECK(scored >= 4 && outs < 60 && early <= 3 && best >= 2);
    // The round is over for five seconds, and then the next ring is raised: another arrangement
    const int total = seat_of(&world, 0).score;
    while (world.Ring.round == 1) tick(1);
    tick(2);
    TIDE_CHECK(world.Ring.round == 2 && world.Ring.phase == Phase_Ready && world.Ring.gone == 0);
    TIDE_CHECK(count(&world, tide_has_Tile) == TILES && count(&world, tide_has_Crate) > 2000 && knocked(&world) == 0);
    TIDE_CHECK(count(&world, tide_has_Ball) == 4 && count(&world, tide_has_Pickup) == 0);
    // What was scored stays, and the next round starts from nothing
    TIDE_CHECK(seat_of(&world, 0).score == total && seat_of(&world, 0).round == 0);
    // A new game is asked for: everything from nothing
    TIDE_REQUIRE(command(&world, TIDE_COMMAND_ID_Rebuild, 0));
    tick(3);
    TIDE_CHECK(world.Ring.round == 1 && world.Ring.phase == Phase_Ready && seat_of(&world, 0).score == 0);
    TIDE_CHECK(count(&world, tide_has_Crate) > 2000);
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

// What this machine's player's input says, with the right key held: their hands are on their ball, or off.
static Controls sampled(void)
{
    tide_devices held = devices;
    held.keyboard.d.pressed = true;
    held.keyboard.space.pressed = true;
    return tide_input_sample(&held, &local);
}

static bool drives(void)
{
    const Controls controls = sampled();
    return !controls.away && controls.move.x > 0.9f && controls.charge;
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

TIDE_TEST(sumo_opens_on_its_lobby_unless_it_was_told_which_match)
{
    start(3, true);
    // Started with nothing said: a match of its own, closed, behind the title.
    // The player's ball plays by itself while the lobby is up
    machine(TIDE_SESSION_CONNECTED, 0, true, false, "");
    frame(&world);
    frame(&world);
    TIDE_CHECK(local.Lobby.page == Page_Title && !local.Hand.playing && sampled().away && !sampled().charge);
    TIDE_CHECK(local.Lobby.players == 3 && local.Lobby.mine == 0 && local.Lobby.manned == 7 && local.Lobby.bots);
    TIDE_CHECK(asked() == TIDE_REQUEST_NONE);
    tide_local_free(&local);

    // --host, or a page's ?host: the match is open by the first frame, and no lobby is in the way
    machine(TIDE_SESSION_CONNECTED, 0, true, true, "K7QF2M");
    frame(&world);
    frame(&world);
    TIDE_CHECK(local.Lobby.page == Page_None && local.Hand.playing && drives());
    // The button is held as the menu closes: the player's hands aren't on the ball until it's let go
    local.Lobby.page = Page_Room;
    frame(&world);
    TIDE_CHECK(!drives());
    local.Lobby.page = Page_None;
    devices.pointer.press.pressed = devices.pointer.press.held = true;
    frame(&world);
    TIDE_CHECK(!drives());
    devices.pointer.press.pressed = devices.pointer.press.held = false;
    frame(&world);
    TIDE_CHECK(drives());
    tide_local_free(&local);

    // --join <code>, or ?join=<code>: joining by the first frame. The page says so until the match's world is here
    machine(TIDE_SESSION_CONNECTING, -1, false, false, "K7QF2M");
    frame(NULL);
    TIDE_CHECK(local.Lobby.page == Page_Join && local.Lobby.joining && sampled().away);
    tide_local_connected(&local);
    tide_local_set_session(&local, TIDE_SESSION_CONNECTED, player(1), 30, false, false, "K7QF2M");
    frame(&world); // The page closes as this frame ends...
    frame(&world); // ...and the frame after it says nothing of what's held
    frame(&world);
    TIDE_CHECK(local.Lobby.page == Page_None && !local.Lobby.joining && drives());
    TIDE_CHECK(local.Lobby.mine == 1 && local.Fx.seat == 1);
    // That match ends: back on the title, with a line that says so, and a match of its own asked for
    tide_local_disconnected(&local, TIDE_DISCONNECT_ENDED, "");
    tide_local_set_session(&local, TIDE_SESSION_OFFLINE, (tide_player_id){0}, 0, false, false, "");
    frame(NULL);
    frame(NULL);
    TIDE_CHECK(local.Lobby.page == Page_Title && says_something() && sampled().away);
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

// A frame as a host runs one: the devices are polled, the game's input is
// sampled from what the GUI leaves of them, and the frame is drawn.
static void hosted(const tide_world *w)
{
    tide_pointer_poll(&devices, true);
    tide_devices left = devices;
    tide_gui_hide(&gui, &left);
    tide_devices_consume(&devices);
    frame(w);
    devices.keyboard.text.count = 0;
}

// Typing a room's code into the lobby's field, and Enter: the room is asked
// for, as the Join button beside the field asks for it.
TIDE_TEST(sumo_enter_joins_the_room_whose_code_was_typed)
{
    start(1, true);
    machine(TIDE_SESSION_CONNECTED, 0, true, false, "");
    hosted(&world);
    local.Lobby.page = Page_Join;
    hosted(&world);
    // A click in the field, where the page's layout puts it: the pointer comes, presses and lets go, a frame each
    const float y = (400.0f - (34 + 26 + 50 + 34 + 48)) * 0.42f + 34 + 26 + 25;
    devices.mouse.position = tide_f2(100, 400 - y);
    hosted(&world);
    tide_button_set(&devices.mouse.left, true);
    hosted(&world);
    tide_button_set(&devices.mouse.left, false);
    hosted(&world);
    for (const char *c = "k7qf2m"; *c; c++) devices.keyboard.text.chars[devices.keyboard.text.count++] = (uint32_t)*c;
    hosted(&world);
    const tide_str typed = tide_text_read(&local.heap, local.Lobby.code);
    TIDE_CHECK(typed.bytes == 6 && memcmp(typed.ptr, "K7QF2M", 6) == 0 && asked() == TIDE_REQUEST_NONE);
    tide_button_set(&devices.keyboard.enter, true);
    hosted(&world);
    tide_button_set(&devices.keyboard.enter, false);
    hosted(&world);
    TIDE_CHECK(local.Lobby.joining && asked() == TIDE_REQUEST_JOIN);
    tide_local_free(&local);
    tide_draw_free(&draw);
    tide_world_free(&world);
}

// The sparks, the numbers and the words come from what the match sends: a
// tick that tells this machine what it sent, and the frame that shows it.
static tide_event_log heard;

static void tick_and_show(void)
{
    tide_world_tick_events(&world, NULL, &heard);
    tide_local_events(&local, &heard);
    frame(&world);
}

static float popped(const int seat)
{
    return *(const float *)tide_list_read(&local.heap, local.Fx.pops, seat, sizeof(float));
}

TIDE_TEST(sumo_effects_come_from_what_the_match_sends)
{
    start(2, false);
    machine(TIDE_SESSION_CONNECTED, 0, true, true, "K7QF2M");
    frame(&world);
    frame(&world);
    TIDE_CHECK(local.Fx.seat == 0 && local.Fx.here && tide_list_read_count(&local.heap, local.Air.sparks) == 0);
    // Every crate is in the list that's drawn at once, and casts its shadow from it; every tile is in the floor's
    TIDE_CHECK(tide_list_read_count(&local.heap, local.Fx.solids) == count(&world, tide_has_Crate));
    TIDE_CHECK(tide_list_read_count(&local.heap, local.Fx.tiles) == TILES);
    TIDE_CHECK(tide_list_read_count(&local.heap, local.Fx.rounds) == 2);

    // This machine's player's ball goes into a stack: splinters, and the camera feels it
    hold(0, 0.5f, 1, true);
    for (int i = 0; i < 40; i++) tick_and_show();
    TIDE_CHECK(local.Fx.winding && local.Fx.charge > 0.5f && local.Flow.charged);
    hold(0, 0.5f, 1, false);
    tick_and_show();
    // The burst: a ring where it left from, and the wait for the next one shows
    TIDE_CHECK(tide_list_read_count(&local.heap, local.Air.waves) == 1 && local.Fx.cooldown > 0 && local.Fx.trauma > 0.0f);
    bool splintered = false;
    for (int i = 0; i < 90 && !splintered; i++) {
        tick_and_show();
        splintered = tide_list_read_count(&local.heap, local.Air.sparks) > 8;
    }
    TIDE_CHECK(splintered);
    hold(0, 0, 0, false);

    // It's thrown over the rim by the other one: once that's certain, the point floats up and their score jumps
    clear_the_ring();
    put(0, 0, BALL, 17);
    put(1, 0, BALL, 7);
    hold(1, 0, 1, true);
    for (int i = 0; i < 50; i++) tick_and_show();
    hold(1, 0, 1, false);
    bool clashed = false;
    for (int i = 0; i < 200 && seat_of(&world, 0).up; i++) {
        tick_and_show();
        clashed = clashed || tide_get_Ball_local(&local, ball_of(&world, 0)).flash > 0.0f;
        // Once it has hit, the other one holds back from the rim itself
        if (tide_has_Ball(&world, ball_of(&world, 0)) && tide_get_Ball(&world, ball_of(&world, 0)).last == 1) hold(1, 0, -1, false);
    }
    tick_and_show();
    TIDE_CHECK(clashed && !seat_of(&world, 0).up && seat_of(&world, 1).score == 1);
    TIDE_CHECK(popped(1) > 0.5f && tide_list_read_count(&local.heap, local.Air.labels) == 1);
    TIDE_CHECK(local.Flow.fell < 0.1f && local.Flow.by == 1 && !local.Fx.here);

    // A round ends, and the next is raised: the words know who took it
    world.Ring.timer = 2;
    tick_and_show();
    tick_and_show();
    TIDE_CHECK(world.Ring.phase == Phase_Over);
    TIDE_CHECK(local.Flow.winner == 1 && local.Flow.best == 1 && local.Flow.round == 1 && local.Flow.since < 0.1f);
    for (int i = 0; i < 400 && world.Ring.round == 1; i++) tick_and_show();
    TIDE_CHECK(world.Ring.round == 2 && local.Flow.set == 2 && local.Flow.begun < 0.1f);
    tide_event_log_free(&heard);
    tide_local_free(&local);
    tide_draw_free(&draw);
    tide_world_free(&world);
}

// ---------------------------------------------------------------------------
// Four players in a match, over a network that's late and loses packets

// What a player does at each tick of the match. They go for the middle of
// the ring, where the others are, round it one way or the other, and charge
// every few seconds, each to its own beat.
static void play(void *user, const uint32_t tick, void *input)
{
    const int who = *(const int *)user;
    Controls controls = {0};
    const uint32_t beat = (tick + (uint32_t)who * 37u) % 240u;
    const float turn = (float)((tick / 90u + (uint32_t)who * 3u) % 8u);
    // Eight ways, a while each
    static const float ways[8][2] = {{0, 1}, {0.7071f, 0.7071f}, {1, 0}, {0.7071f, -0.7071f}, {0, -1}, {-0.7071f, -0.7071f}, {-1, 0},
                                     {-0.7071f, 0.7071f}};
    const int way = ((int)turn + (who % 2 == 0 ? 0 : 4)) % 8;
    controls.move = tide_f2(ways[way][0], ways[way][1]);
    controls.charge = beat >= 150 && beat < 190;
    if (tick % 400u > 360u) controls.move = tide_f2(0, 0);
    memcpy(input, &controls, sizeof controls);
}

#define PLAYERS 4

typedef struct match {
    tide_loopback *net;
    tide_server *server;
    tide_client *players[PLAYERS];
    double now;
    uint64_t spent[PLAYERS];  // What each player's updates took, in nanoseconds
    uint64_t slowest[PLAYERS];
    uint32_t updates[PLAYERS];
} match;

static void advance(match *m)
{
    m->now += 1.0 / 60;
    tide_loopback_set_time(m->net, m->now);
    for (int i = 0; i < PLAYERS; i++) {
        if (!m->players[i]) continue;
        const uint64_t began = tide_time_now_ns();
        tide_client_update(m->players[i], m->now);
        const uint64_t took = tide_time_now_ns() - began;
        m->spent[i] += took;
        m->updates[i]++;
        if (took > m->slowest[i]) m->slowest[i] = took;
    }
    tide_server_update(m->server, m->now);
}

static void join(match *m, const int who)
{
    static int names[PLAYERS] = {0, 1, 2, 3};
    m->players[who] = tide_client_create(&(tide_client_desc){.game = &tide_game_api,
                                           .transport = tide_loopback_endpoint(m->net, (uint32_t)who + 2),
                                           .server = tide_loopback_address(1), .lead = 2,
                                           .sample = play, .user = &names[who]}, m->now);
}

// A player joins: how long until the match's world is theirs, and how far
// ahead of what's confirmed they run a second after that.
static void join_late(match *m, const int who)
{
    join(m, who);
    int waited = 0;
    while (tide_client_status_of(m->players[who]).state != TIDE_SESSION_CONNECTED && waited < 1200) {
        advance(m);
        waited++;
    }
    TIDE_REQUIRE(tide_client_status_of(m->players[who]).state == TIDE_SESSION_CONNECTED);
    for (int i = 0; i < 60; i++) advance(m);
    const tide_client_status status = tide_client_status_of(m->players[who]);
    printf("    player %d joined at tick %u: %llu bytes of world, of which %d are Box3D's state, in %d updates (%.1f s); a"
           " second later it's %u ticks ahead of what's confirmed\n",
           who + 1, tide_server_tick(m->server) - 60, (unsigned long long)status.world_bytes, ((const tide_world *)tide_server_world(m->server))->Box3D_Physics.size, waited,
           (double)waited / 60.0, status.predicted_tick - status.verified_tick);
    TIDE_CHECK(status.predicted_tick - status.verified_tick <= 15);
}

static int seat_by(const tide_world *w, const tide_client *client)
{
    const tide_player_id id = tide_client_status_of(client).player;
    for (int seat = 0; seat < 8; seat++) {
        const tide_entity e = ball_of(w, seat);
        if (tide_has_Owner(w, e) && tide_get_Owner(w, e).player.id == id.id) return seat;
    }
    return -1;
}

static bool send(match *m, const int who, const uint32_t id)
{
    uint8_t bytes[TIDE_COMMAND_BYTES];
    const uint32_t size = plain_command(id, bytes);
    return tide_game_api.check_command(bytes, size) && tide_client_send_command(m->players[who], bytes, size);
}

TIDE_TEST(sumo_four_players_stay_in_one_match_over_a_bad_network)
{
    b3t_forget();
    match m = {.net = tide_loopback_create(9102)};
    tide_loopback_set_conditions(m.net, (tide_net_conditions){.latency = 0.04, .jitter = 0.015, .loss = 0.05});
    m.server = tide_server_create(&(tide_server_desc){.game = &tide_game_api, .tick_rate = 60,
                                    .transports = {tide_loopback_endpoint(m.net, 1)}}, m.now);
    join_late(&m, 0);
    join_late(&m, 1);
    TIDE_REQUIRE(tide_client_status_of(m.players[0]).state == TIDE_SESSION_CONNECTED);
    TIDE_REQUIRE(tide_client_status_of(m.players[1]).state == TIDE_SESSION_CONNECTED);
    const tide_world *authority = tide_server_world(m.server);
    TIDE_REQUIRE(authority != NULL);
    TIDE_CHECK(authority->Ring.manned == 3 && authority->Ring.bots);

    // Two players and two bots go at the ring, and each other. Then two more
    // players join, one after the other, while crates are in the air: the
    // world they're sent has Box3D's state in it, every body where it is
    while (tide_server_tick(m.server) < 520) advance(&m);
    authority = tide_server_world(m.server);
    TIDE_CHECK(authority->Ring.phase == Phase_Play && knocked(authority) > 0);
    join_late(&m, 2);
    while (tide_server_tick(m.server) < 760) advance(&m);
    join_late(&m, 3);
    bool bots = false;
    while (tide_server_tick(m.server) < 1100) {
        // The second player turns the bots off: a command, which the server gives a tick
        if (!bots && tide_server_tick(m.server) >= 1000) bots = send(&m, 1, TIDE_COMMAND_ID_Bots);
        advance(&m);
    }
    authority = tide_server_world(m.server);
    TIDE_REQUIRE(bots);
    for (int i = 0; i < PLAYERS; i++) {
        TIDE_REQUIRE(tide_client_status_of(m.players[i]).state == TIDE_SESSION_CONNECTED);
        // Each has a seat, and its ball when it isn't on its way back
        TIDE_CHECK((authority->Ring.manned >> i & 1) == 1);
        if (seat_of(authority, i).up) TIDE_CHECK(seat_by(authority, m.players[i]) == i);
    }
    TIDE_CHECK(authority->Ring.manned == 15 && !authority->Ring.bots);
    const uint64_t brawl = tide_world_hash(authority);
    printf("    at tick %u: seats threw out %d, %d, %d and %d; %d crates stand, %d of them knocked about; %d bytes of physics\n",
           tide_server_tick(m.server), seat_of(authority, 0).score, seat_of(authority, 1).score, seat_of(authority, 2).score,
           seat_of(authority, 3).score, count(authority, tide_has_Crate), knocked(authority), authority->Box3D_Physics.size);
    printf("    the server's world at tick %u: 0x%016llx\n", tide_server_tick(m.server), (unsigned long long)brawl);
    TIDE_CHECK(tide_server_tick(m.server) == 1100);
    TIDE_CHECK(brawl == BRAWL_HASH);

    // The third player asks for a new game: the ring is raised again, whole
    TIDE_REQUIRE(send(&m, 2, TIDE_COMMAND_ID_Rebuild));
    while (tide_server_tick(m.server) < 1400) advance(&m);
    authority = tide_server_world(m.server);
    TIDE_CHECK(authority->Ring.round == 1 && authority->Ring.phase == Phase_Play && count(authority, tide_has_Tile) == TILES);
    TIDE_CHECK(count(authority, tide_has_Crate) > 1500 && count(authority, tide_has_Ball) == 4);

    // No machine's world ever went another way than the server's: what each
    // predicted wrong it ran again, and every tick it confirmed had the
    // server's hash, Box3D's state in it. The two who joined late too.
    for (int i = 0; i < PLAYERS; i++) {
        const tide_client_status status = tide_client_status_of(m.players[i]);
        printf("    player %d ran %u predicted ticks again, was sent the world again %u times, and is %u ticks ahead of"
               " what's confirmed (its inputs wait %.0f ms at the server); an update took %.2f ms, and %.1f at the most\n",
               i + 1, status.again_ticks, status.resyncs, status.predicted_tick - status.verified_tick, (double)status.early_ms,
               (double)m.spent[i] / m.updates[i] / 1e6, (double)m.slowest[i] / 1e6);
        TIDE_CHECK(status.state == TIDE_SESSION_CONNECTED && status.resyncs == 0 && status.again_ticks > 0);
        const tide_world *confirmed = tide_client_verified_world(m.players[i]);
        TIDE_CHECK(confirmed != NULL && confirmed->Ring.round == 1);
    }

    const uint64_t hash = tide_world_hash(authority);
    printf("    Box3D's world was made again from an image %d times: five matches in one program, and C has room for four worlds\n",
           (int)b3t_restores());
    printf("    the server's world at tick %u: 0x%016llx\n", tide_server_tick(m.server), (unsigned long long)hash);
    TIDE_CHECK(tide_server_tick(m.server) == 1400);
    TIDE_CHECK(hash == MATCH_HASH);

    for (int i = PLAYERS - 1; i >= 0; i--) tide_client_destroy(m.players[i]);
    tide_server_destroy(m.server);
    tide_loopback_destroy(m.net);
}

// What things cost, for whoever reads this test's output: timings aren't
// checked, as machines differ too much. A tick with the world kept and hashed
// after it, as a client's is: with a fresh ring at rest, while four bots are
// at it, and at the worst, a shock beside the middle's pyramid.
static void spend(const int ticks, const char *what, const bool away)
{
    tide_world kept = {0};
    tide_world_copy(&kept, &world);
    uint64_t ticking = 0;
    uint64_t keeping = 0;
    uint64_t slowest = 0;
    uint64_t step_ns, image_ns;
    int32_t whole0, parts0, whole, parts;
    b3t_measure(&step_ns, &image_ns);
    b3t_images(&whole0, &parts0);
    for (int i = 0; i < ticks; i++) {
        tide_world_set_input(&world, player(0), (Controls){.away = away});
        const uint64_t began = tide_time_now_ns();
        tide_world_tick(&world);
        const uint64_t ticked = tide_time_now_ns();
        tide_world_copy(&kept, &world);
        (void)tide_world_hash(&world);
        ticking += ticked - began;
        keeping += tide_time_now_ns() - ticked;
        if (ticked - began > slowest) slowest = ticked - began;
    }
    b3t_measure(&step_ns, &image_ns);
    b3t_images(&whole, &parts);
    printf("    %s: %.3f ms a tick (Box3D's step %.3f, its image %.3f), %.2f at the most, and %.3f to keep and hash the world; %d images"
           " written whole and %d in parts\n",
           what, (double)ticking / ticks / 1e6, (double)step_ns / ticks / 1e6, (double)image_ns / ticks / 1e6, (double)slowest / 1e6,
           (double)keeping / ticks / 1e6, whole - whole0, parts - parts0);
    tide_world_free(&kept);
}

// What a client pays when it guessed another player's input wrong: its world
// as it was `ahead` ticks ago, Box3D's made again from the image in it, and
// those ticks run again with another input, each kept and hashed. The world
// it's done to is a copy: the match goes on as it was. The least of the
// times it's done says the most on a machine that's busy with other things.
static void rerun(const int ahead, const int times, const char *what)
{
    tide_world then = {0};
    tide_world redo = {0};
    tide_world kept = {0};
    uint64_t spent = 0;
    uint64_t least = UINT64_MAX;
    uint64_t each[16] = {0}; // Each of the ticks, the least it took
    uint64_t forward = UINT64_MAX; // The same ticks as the match ran them the first time, the least
    for (int k = 0; k < 16; k++) each[k] = UINT64_MAX;
    for (int i = 0; i < times; i++) {
        tide_world_copy(&then, &world);
        for (int k = 0; k < ahead; k++) {
            tide_world_set_input(&world, player(0), (Controls){.away = true});
            const uint64_t began = tide_time_now_ns();
            tide_world_tick(&world);
            tide_world_copy(&kept, &world);
            (void)tide_world_hash(&world);
            const uint64_t took = tide_time_now_ns() - began;
            if (took < forward) forward = took;
        }
        const uint64_t began = tide_time_now_ns();
        tide_world_copy(&redo, &then);
        uint64_t last = began;
        for (int k = 0; k < ahead; k++) {
            // Another input than the match had: these ticks go another way, as a wrong guess put right does,
            // and C has no world waiting at any state they pass through
            tide_world_set_input(&redo, player(0), (Controls){.move = {(float)(i % 2) * 2.0f - 1.0f, 0}});
            tide_world_tick(&redo);
            tide_world_copy(&kept, &redo);
            (void)tide_world_hash(&redo);
            const uint64_t now = tide_time_now_ns();
            if (now - last < each[k]) each[k] = now - last;
            last = now;
        }
        const uint64_t took = last - began;
        spent += took;
        if (took < least) least = took;
    }
    printf("    %s: %.2f ms to run %d ticks again at the least, %.2f on the whole; tick by tick, at the least:", what, (double)least / 1e6,
           ahead, (double)spent / times / 1e6);
    for (int k = 0; k < ahead; k++) printf(" %.2f", (double)each[k] / 1e6);
    printf(" (and %.2f the first time round)\n", (double)forward / 1e6);
    tide_world_free(&then);
    tide_world_free(&redo);
    tide_world_free(&kept);
}

TIDE_TEST(sumo_costs)
{
    b3t_forget();
    tide_world_free(&world);
    tide_world_init(&world, DT);
    tide_world_player_joined(&world, player(0));
    uint64_t began = tide_time_now_ns();
    tide_world_tick(&world);
    const double plan = (double)(tide_time_now_ns() - began) / 1e6;
    began = tide_time_now_ns();
    tide_world_tick(&world);
    const double made = (double)(tide_time_now_ns() - began) / 1e6;
    printf("    raising a ring of %d bodies: %.2f ms to plan and spawn it, and %.2f for the tick that makes its bodies and steps them\n",
           count(&world, tide_has_Box3D_Body), plan, made);
    spend(60, "while what's stacked comes to rest", false);
    world.Ring.bots = false;
    tick(READY - 62 + RESPAWN);
    spend(120, "a fresh ring at rest, one ball that nobody drives", false);
    // A world made again from its image, as a client that predicted wrong does before it runs its ticks again
    double again = 1e9;
    for (int i = 0; i < 5; i++) {
        b3t_forget();
        began = tide_time_now_ns();
        tide_world_tick(&world);
        const double took = (double)(tide_time_now_ns() - began) / 1e6;
        if (took < again) again = took;
    }
    printf("    a tick that makes Box3D's world again from the match's image (%d bytes): %.2f ms\n", world.Box3D_Physics.size, again);
    world.Ring.bots = true;
    tick(4);
    spend(600, "four bots at a full ring, 10 seconds", true);
    rerun(5, 60, "a client that guessed wrong meanwhile, 5 ticks ahead of what's confirmed");
    rerun(10, 60, "the same, 10 ticks ahead");
    spend(1500, "the 25 seconds after", true);
    printf("    by then %d crates stand, %d of them knocked about, and Box3D's state is %d bytes\n", count(&world, tide_has_Crate),
           knocked(&world), world.Box3D_Physics.size);
    // The worst: a shock beside the middle's pyramid of a full ring
    start(1, false);
    // And what a frame's views take with all of it standing: every crate into the list that's drawn at once
    machine(TIDE_SESSION_CONNECTED, 0, true, true, "K7QF2M");
    frame(&world);
    uint64_t views = UINT64_MAX;
    for (int i = 0; i < 40; i++) {
        began = tide_time_now_ns();
        frame(&world);
        const uint64_t took = tide_time_now_ns() - began;
        if (took < views) views = took;
    }
    printf("    a frame's views, with %d crates standing: %.2f ms at the least, for %u commands in its draw list\n",
           count(&world, tide_has_Crate), (double)views / 1e6, (unsigned)draw.count);
    tide_local_free(&local);
    tide_draw_free(&draw);
    put(0, 0, BALL, -4.6f);
    world.Ring.drop = 1;
    tick(2);
    for (uint32_t i = 0; i < world.entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&world.entities, i);
        if (!tide_has_Pickup(&world, e)) continue;
        Pickup pickup = tide_get_Pickup(&world, e);
        pickup.power = Power_Shock;
        tide_set_Pickup(&world, e, pickup);
        Box3D_Body body = tide_get_Box3D_Body(&world, e);
        body.position = tide_f3(0, 1.1f, -4.6f);
        tide_set_Box3D_Body(&world, e, body);
    }
    spend(20, "a shock beside the middle's pyramid, its first 20 ticks", false);
    spend(120, "the two seconds after", false);
    printf("    %d crates were knocked about\n", knocked(&world));
    tide_world_free(&world);
}
