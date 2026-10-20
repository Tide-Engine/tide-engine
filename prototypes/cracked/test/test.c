// CRACKED with no window: what the match promises, a rule at a time. The
// numbers are the Unity game's (its prefabs', by way of state.tide and
// player.tide), and what's expected of them here is worked out from the
// tick's own order: a force is a push, the body's step adds gravity and takes
// its damping, then the body moves.
//
// Most tests are in a place made up here (lab): one floor whose top is y = 0,
// and whatever walls a test adds, with the first two seats' players on it
// and the round running. The others play the game's own maps.
//
// How a test has the map it wants: the match picks its next map as the warmup
// starts and makes it a tick later (or two seconds later, with an old map to
// take down), so `arena.futureMap` is written between the two (raise_map). Cards
// are given the same way, before the map's players are made: a seat's cards
// are `arena.picks`, applied as it spawns (give). The match's seed is 0 for
// every world a test starts (tide_world_init), so a run repeats.

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/list.h"
#include "tide/math.h"
#include "tide/net.h"
#include "tide/session.h"
#include "tide/text.h"
#include "tide_test.h"

#define RATE 30
#define DT (1.0f / 30.0f)
#define SEATS 8
#define MAPS 7
#define UPGRADES 38
#define CARDS 5

// The body's numbers (player.tide).
#define MASS 10.0f
#define GRAVITY 9.81f
#define GROUND_KEEPS (1.0f - 5.0f * DT)     // What a tick's damping leaves of a body's speed on the ground
#define AIR_KEEPS (1.0f - 0.5f * DT)        // ...and in the air
#define FLOAT_HEIGHT 0.35f
#define FLOAT_STRENGTH 500.0f
// Where the float spring holds the feet: under the surface, as deep as makes
// its push gravity's (500 * depth / 0.35 = 10 * 9.81).
#define REST (FLOAT_HEIGHT * MASS * GRAVITY / FLOAT_STRENGTH)

// The cards these tests take, by their place in the upgrade database.
enum {
    ARCANE_ORBS = 1, ARCSTORM = 2, BLAST_RUNE = 3, CRYSTAL_BASTION = 4, DASH_GUARD = 5, ECHO_GUARD = 6, FROST_GUARD = 9,
    LINGERING_PAIN = 11, REBOUND_HEX = 16, SEEKER_SPARK = 18, SHATTERBURST = 19, SPELL_BLOOM = 21, GHOST_ROUND = 24,
    JETBURST = 25, POISON_CLOUD = 26, POWER_KICK = 27, REFLEX_RELOAD = 29, SKY_BOUND = 30, STAGGER_SHOT = 31,
    STATIC_SURGE = 32, STRAIGHT_SHOT = 33, VITAL_LEECH = 35, VITAL_MIST = 36
};

static tide_world world;

static tide_player_id player(const int index)
{
    return tide_player_from_index(index);
}

static Arena *arena(void)
{
    return &world.Arena;
}

static Seat *seat(const int s)
{
    return &world.Arena.seats.at[s];
}

static int count_in(const tide_world *w, const tide_list list)
{
    return tide_list_read_count(&w->heap, list);
}

static int bullets(void)
{
    return count_in(&world, world.Arena.bullets);
}

static const Bullet *bullet(const int i)
{
    return tide_list_read(&world.heap, world.Arena.bullets, i, sizeof(Bullet));
}

static int pieces(void)
{
    return count_in(&world, world.Arena.pieces);
}

static const Piece *piece(const int i)
{
    return tide_list_read(&world.heap, world.Arena.pieces, i, sizeof(Piece));
}

static const Mover *mover(const int i)
{
    return tide_list_read(&world.heap, world.Arena.movers, i, sizeof(Mover));
}

static int picked(const int i)
{
    const int32_t *pick = tide_list_read(&world.heap, world.Arena.picks, i, sizeof(int32_t));
    return pick ? *pick : -1;
}

// How many of a seat's effects are of a kind.
static int traits(const int s, const TraitKind kind)
{
    int found = 0;
    for (int i = 0; i < count_in(&world, world.Arena.traits); i++) {
        const Trait *trait = tide_list_read(&world.heap, world.Arena.traits, i, sizeof(Trait));
        if (trait->seat == s && trait->kind == kind) found++;
    }
    return found;
}

static void tick(const int ticks)
{
    for (int i = 0; i < ticks; i++) tide_world_tick(&world);
}

static float flat(const tide_float3 v)
{
    return tide_sqrt_f(v.x * v.x + v.z * v.z);
}

static float length(const tide_float3 v)
{
    return tide_sqrt_f(v.x * v.x + v.y * v.y + v.z * v.z);
}

static tide_float3 minus(const tide_float3 a, const tide_float3 b)
{
    return tide_f3(a.x - b.x, a.y - b.y, a.z - b.z);
}

static bool near(const float a, const float b, const float within)
{
    return fabsf(a - b) <= within;
}

static bool finite3(const tide_float3 v)
{
    return isfinite(v.x) && isfinite(v.y) && isfinite(v.z);
}

static tide_quaternion upright(void)
{
    return (tide_quaternion){{0, 0, 0, 1}};
}

// A vector turned by a rotation, and by its inverse.
static tide_float3 turned(const tide_quaternion q, const tide_float3 v)
{
    const tide_float3 u = {q.value.x, q.value.y, q.value.z};
    const float w = q.value.w;
    const tide_float3 a = {u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
    const tide_float3 b = {u.y * a.z - u.z * a.y, u.z * a.x - u.x * a.z, u.x * a.y - u.y * a.x};
    return tide_f3(v.x + 2 * (w * a.x + b.x), v.y + 2 * (w * a.y + b.y), v.z + 2 * (w * a.z + b.z));
}

static tide_float3 unturned(const tide_quaternion q, const tide_float3 v)
{
    return turned((tide_quaternion){{-q.value.x, -q.value.y, -q.value.z, q.value.w}}, v);
}

// Where a point is in a piece's own box.
static tide_float3 inside(const Piece *box, const tide_float3 at)
{
    return unturned(box->rotation, minus(at, box->position));
}

// ---------------------------------------------------------------------------
// Starting a match

// A match these players have joined, as its first warmup starts: each has the
// seat of their number, the match has picked its first map, and nothing is
// made yet.
static void seated(const int players)
{
    tide_world_free(&world);
    tide_world_init(&world, DT);
    for (int i = 0; i < players; i++) tide_world_player_joined(&world, player(i));
    tick(2);
}

// A seat is given a card, as if its player had picked it: applied as the seat spawns.
static void give_in(tide_world *w, const int s, const int upgrade)
{
    tide_text_use(&w->heap, NULL);
    *(int32_t *)tide_list_add(&w->Arena.picks, sizeof(int32_t), TIDE_IN_MATCH) = s * 64 + upgrade;
    tide_text_use(NULL, NULL);
}

static void give(const int s, const int upgrade)
{
    give_in(&world, s, upgrade);
}

// The warmup's map is made, this one of the rotation (under 0: the match's
// pick), and its players spawn.
static void raise_map(const int map)
{
    if (map >= 0) arena()->futureMap = map;
    tick(1);
}

// The warmup, to the tick the round runs from.
static void warm(void)
{
    for (int i = 0; i < 400 && arena()->phase != Phase_Running; i++) tick(1);
}

static void begin(const int players, const int map)
{
    seated(players);
    raise_map(map);
    warm();
}

static Controls idle(void)
{
    return (Controls){.aim = {0, 0, 1}};
}

static void hold(const int who, const Controls controls)
{
    tide_world_set_input(&world, player(who), controls);
}

// ---------------------------------------------------------------------------
// The lab: a floor, and what a test puts on it

static void box(const float x, const float y, const float z, const float hx, const float hy, const float hz, const bool floor)
{
    tide_text_use(&world.heap, NULL);
    Piece *made = tide_list_add(&world.Arena.pieces, sizeof(Piece), TIDE_IN_MATCH);
    *made = (Piece){.position = {x, y, z}, .rotation = upright(), .half = {hx, hy, hz}, .solid = true, .damageFloor = floor,
                    .mover = -1, .block = -1, .localRotation = upright(), .radius = tide_sqrt_f(hx * hx + hy * hy + hz * hz)};
    tide_text_use(NULL, NULL);
}

static void wall(const float x, const float y, const float z, const float hx, const float hy, const float hz)
{
    box(x, y, z, hx, hy, hz, false);
}

// A seat's body somewhere, still and upright.
static void put(const int s, const float x, const float y, const float z)
{
    Seat *it = seat(s);
    it->position = tide_f3(x, y, z);
    it->velocity = tide_f3(0, 0, 0);
    it->spin = tide_f3(0, 0, 0);
    it->push = tide_f3(0, 0, 0);
    it->twist = tide_f3(0, 0, 0);
    it->rotation = upright();
}

// ...standing on the lab's floor, at rest on its spring.
static void stand(const int s, const float x, const float z)
{
    put(s, x, -REST, z);
    seat(s)->grounded = true;
    seat(s)->jumpCount = 0;
}

// The map's pieces are taken away, and a floor is all there is: its top at
// y = 0, 120 m across. The round is running; the first seat stands in the
// middle, and the others 20 m and more away along z.
static void floor_only(void)
{
    tide_text_use(&world.heap, NULL);
    tide_list_clear(&world.Arena.pieces, sizeof(Piece));
    tide_list_clear(&world.Arena.movers, sizeof(Mover));
    tide_text_use(NULL, NULL);
    wall(0, -0.5f, 0, 60, 0.5f, 60);
    for (int s = 0; s < SEATS; s++)
        if (seat(s)->alive) stand(s, 0, 20.0f * (float)s);
}

static void lab(void)
{
    begin(2, 0);
    floor_only();
    hold(0, idle());
    hold(1, idle());
}

// The lab, with cards: each pair is a seat and its card.
static void lab_with(const int count, const int cards[][2])
{
    seated(2);
    for (int i = 0; i < count; i++) give(cards[i][0], cards[i][1]);
    raise_map(0);
    warm();
    floor_only();
    hold(0, idle());
    hold(1, idle());
}

// ---------------------------------------------------------------------------
// Commands

static uint32_t zigzag(const int32_t v)
{
    return ((uint32_t)v << 1) ^ (uint32_t)(v >> 31);
}

static uint32_t pick_bytes(const int card, const int revision, uint8_t bytes[TIDE_COMMAND_BYTES])
{
    tide_bits bits = {bytes, TIDE_COMMAND_BYTES, 0, false};
    tide_bits_put(&bits, TIDE_COMMAND_ID_PickCard, 16);
    tide_bits_put(&bits, 0, 32);
    tide_bits_put(&bits, 0, 32);
    tide_bits_put_varint(&bits, zigzag(card));
    tide_bits_put_varint(&bits, zigzag(revision));
    return tide_bits_end(&bits);
}

static uint32_t restart_bytes(uint8_t bytes[TIDE_COMMAND_BYTES])
{
    tide_bits bits = {bytes, TIDE_COMMAND_BYTES, 0, false};
    tide_bits_put(&bits, TIDE_COMMAND_ID_Restart, 16);
    tide_bits_put(&bits, 0, 32);
    tide_bits_put(&bits, 0, 32);
    return tide_bits_end(&bits);
}

static bool command(tide_world *w, const int sender, const uint8_t *bytes, const uint32_t size)
{
    return tide_game_api.command(w, player(sender), 0, tide_game_api.command_sequence(w, player(sender), 0) + 1, bytes, size);
}

// A player picks a card: the command, and the tick that takes it.
static void pick(const int sender, const int card, const int revision)
{
    uint8_t bytes[TIDE_COMMAND_BYTES];
    const uint32_t size = pick_bytes(card, revision, bytes);
    if (tide_game_api.check_command(bytes, size, NULL)) command(&world, sender, bytes, size);
    tick(1);
}

// ---------------------------------------------------------------------------
// A round's waits, cut short. What they are is counted by the tests of the
// warmup, the round's end and the cards; the rest only want what's after them.

// One tick, with whatever wait the match is in nearly over. A player whose
// turn it is to pick takes their first card, and a bot's four seconds are up.
static void hurry(void)
{
    Arena *a = arena();
    if (a->mapPhase != MapPhase_Idle && a->mapTimer > 0.05f) a->mapTimer = 0.05f;
    if (a->phase == Phase_Warmup && a->mapPhase == MapPhase_Idle && a->phaseTime > 0.05f) a->phaseTime = 0.05f;
    if (a->phase == Phase_RoundEnd && a->phaseTime < 3.95f) a->phaseTime = 3.95f;
    if (a->phase == Phase_GameOver && a->phaseTime < 11.95f) a->phaseTime = 11.95f;
    if (a->phase == Phase_Upgrade && a->upgrading >= 0) {
        if (seat(a->upgrading)->bot) {
            if (a->selectionAge < 3.95f) a->selectionAge = 3.95f;
        } else {
            pick(a->upgrading, 0, a->revision);
            return;
        }
    }
    tick(1);
}

static void hurry_to(const Phase phase)
{
    for (int i = 0; i < 2000 && (arena()->phase != phase || (phase == Phase_Running && arena()->mapPhase != MapPhase_Idle)); i++) hurry();
}

// The round's point goes to a team: everyone of the other is dead.
static void point(const int team)
{
    for (int s = 0; s < SEATS; s++)
        if (seat(s)->alive && s % 2 != team) seat(s)->health = 0;
    tick(2);
}

// ---------------------------------------------------------------------------
// A fresh match

TIDE_TEST(cracked_a_lone_player_has_the_first_seat_and_a_bot_the_other_team)
{
    tide_world_free(&world);
    tide_world_init(&world, DT);
    tide_world_player_joined(&world, player(0));
    tick(1);
    // Nobody had joined when that tick's round was looked at
    TIDE_CHECK(arena()->phase == Phase_Teams && !arena()->hasMap);
    tick(1);
    TIDE_CHECK(seat(0)->taken && !seat(0)->bot && seat(0)->team == 0 && seat(0)->player.id == player(0).id);
    TIDE_CHECK(seat(1)->taken && seat(1)->bot && seat(1)->team == 1);
    for (int s = 2; s < SEATS; s++) TIDE_CHECK(!seat(s)->taken);
    // The teams are made, and the warmup starts: a map of the rotation is next
    TIDE_CHECK(arena()->phase == Phase_Warmup && arena()->mapPhase == MapPhase_DestroyOld);
    TIDE_CHECK(arena()->futureMap >= 0 && arena()->futureMap < MAPS && !seat(0)->alive);
    const int map = arena()->futureMap;
    tick(1);
    TIDE_CHECK(arena()->hasMap && arena()->map == map && arena()->mapPhase == MapPhase_CreateNew);
    TIDE_CHECK(pieces() > 10 && count_in(&world, world.Arena.spawns) == 2);
    // Each is on their team's line: alone on it, at its start
    for (int s = 0; s < 2; s++) {
        TIDE_REQUIRE(seat(s)->alive && !seat(s)->dead && seat(s)->health == 100 && seat(s)->maxHealth == 100);
        bool found = false;
        for (int i = 0; i < 2; i++) {
            const SpawnLine *line = tide_list_read(&world.heap, world.Arena.spawns, i, sizeof(SpawnLine));
            if (line->team != s) continue;
            const tide_float3 right = turned(line->rotation, tide_f3(1, 0, 0));
            const tide_float3 start = {line->position.x - right.x * line->width * 0.5f, line->position.y - right.y * line->width * 0.5f,
                                       line->position.z - right.z * line->width * 0.5f};
            found = length(minus(seat(s)->position, start)) < 0.001f;
        }
        TIDE_CHECK(found);
        TIDE_CHECK(seat(s)->magazine == 5 && !seat(s)->reloading && !seat(s)->shieldActive);
    }
}

TIDE_TEST(cracked_players_fill_the_teams_in_turn_and_take_the_bots_seats)
{
    seated(5);
    // Team 0 has seats 0, 2 and 4, team 1 has 1 and 3: no bot is left
    for (int s = 0; s < 5; s++) TIDE_CHECK(seat(s)->taken && !seat(s)->bot && seat(s)->team == s % 2 && seat(s)->player.id == player(s).id);
    for (int s = 5; s < SEATS; s++) TIDE_CHECK(!seat(s)->taken);
    raise_map(-1);
    for (int s = 0; s < 5; s++) TIDE_CHECK(seat(s)->alive);
    // No two of a team spawn in one place
    TIDE_CHECK(length(minus(seat(0)->position, seat(2)->position)) > 1 && length(minus(seat(0)->position, seat(4)->position)) > 1);
    TIDE_CHECK(length(minus(seat(2)->position, seat(4)->position)) > 1 && length(minus(seat(1)->position, seat(3)->position)) > 1);

    // The player who leaves frees their seat at once, and a team left with nobody has a bot
    tide_world_player_left(&world, player(1));
    tide_world_player_left(&world, player(3));
    tick(2);
    TIDE_CHECK(seat(1)->taken && seat(1)->bot && !seat(3)->taken);
    TIDE_CHECK(seat(0)->taken && !seat(0)->bot);
}

TIDE_TEST(cracked_nobody_moves_fires_or_blocks_until_the_round_runs)
{
    seated(2);
    const Controls all = {.move = {1, 0}, .jump = true, .fire = true, .shield = true, .aim = {0, 0.5f, 1}};
    hold(0, all);
    hold(1, all);
    raise_map(0);
    const tide_float3 spawned = seat(0)->position;
    int unlocked = 0;
    int running = 0;
    for (int t = 1; t <= 200 && !running; t++) {
        tick(1);
        if (!unlocked && arena()->mapPhase == MapPhase_Idle) unlocked = t;
        if (arena()->phase == Phase_Running) running = t;
        if (running) break;
        // Until the map is up, nothing falls either
        if (!unlocked) TIDE_CHECK(length(minus(seat(0)->position, spawned)) == 0);
        TIDE_CHECK(near(seat(0)->position.x, spawned.x, 0.001f) && near(seat(0)->position.z, spawned.z, 0.001f));
        TIDE_CHECK(bullets() == 0 && arena()->nextBullet == 0 && !seat(0)->shieldActive && !seat(1)->shieldActive);
        TIDE_CHECK(seat(0)->jumpCount == 0 && seat(0)->magazine == 5);
    }
    // The map takes two seconds to come up, and the warmup is six from its start: a tick ago here
    printf("    the map was up %d ticks after it was made, and the round ran after %d\n", unlocked, running);
    TIDE_CHECK(unlocked >= 60 && unlocked <= 61);
    TIDE_CHECK(running >= 179 && running <= 180);
    // By then it stands on its platform, under where it was put
    TIDE_CHECK(seat(0)->grounded && seat(0)->position.y < spawned.y);
    tick(15);
    TIDE_CHECK(arena()->nextBullet >= 2 && seat(0)->magazine < 5);
    TIDE_CHECK(seat(0)->shieldActive && seat(1)->shieldActive);
    TIDE_CHECK(seat(0)->position.x > spawned.x + 0.3f && seat(0)->jumpCount == 1);
}

// ---------------------------------------------------------------------------
// Standing

// The top of the flat piece a seat's feet are in or over, or -1000.
static float surface_under(const int s)
{
    float top = -1000;
    for (int i = 0; i < pieces(); i++) {
        const Piece *it = piece(i);
        if (!it->solid || it->damageFloor) continue;
        if (turned(it->rotation, tide_f3(0, 1, 0)).y < 0.9999f) continue;
        const tide_float3 at = inside(it, seat(s)->position);
        if (fabsf(at.x) > it->half.x || fabsf(at.z) > it->half.z) continue;
        const float here = it->position.y + it->half.y;
        if (here > seat(s)->position.y - 0.01f && here < seat(s)->position.y + 0.3f && here > top) top = here;
    }
    return top;
}

// Whether something that moves is near a seat's feet: a platform, or a bridge's half.
static bool by_a_mover(const int s)
{
    for (int i = 0; i < pieces(); i++) {
        const Piece *it = piece(i);
        if (it->mover >= 0 && length(minus(seat(s)->position, it->position)) < it->radius + 1) return true;
    }
    return false;
}

TIDE_TEST(cracked_a_player_at_rest_stands_on_every_map)
{
    int still = 0;
    for (int map = 0; map < MAPS; map++) {
        begin(2, map);
        TIDE_REQUIRE(arena()->map == map && arena()->phase == Phase_Running);
        tide_float3 was[2];
        bool moving[2];
        for (int s = 0; s < 2; s++) {
            was[s] = seat(s)->position;
            // Map7's lines are by its bridges, and the second's start is on one, which lifts whoever
            // spawns there: they're kept, not still
            moving[s] = by_a_mover(s);
            TIDE_CHECK(moving[s] == (map == 4));
            if (!moving[s]) still++;
        }
        // Ten seconds
        for (int t = 0; t < 300; t++) {
            tick(1);
            for (int s = 0; s < 2; s++) {
                TIDE_CHECK(seat(s)->health == 100 && !seat(s)->dead && seat(s)->position.y > was[s].y - 0.1f);
                if (moving[s]) continue;
                TIDE_CHECK(seat(s)->grounded && length(minus(seat(s)->position, was[s])) < 0.002f);
            }
        }
        for (int s = 0; s < 2; s++) {
            if (moving[s]) continue;
            const float top = surface_under(s);
            TIDE_CHECK(top > -1000);
            // On the spring: as deep under the surface as makes its push gravity's, of the float's 0.35 m
            TIDE_CHECK(near(top - seat(s)->position.y, REST, 0.002f));
            TIDE_CHECK(length(seat(s)->velocity) < 0.001f);
            if (map == 0 && s == 0)
                printf("    feet rest %.4f m under the surface (%.4f worked out)\n", (double)(top - seat(s)->position.y), (double)REST);
        }
    }
    TIDE_CHECK(still == 12);
}

// ---------------------------------------------------------------------------
// Walking

TIDE_TEST(cracked_walking_reaches_its_top_speed)
{
    lab();
    tick(5);
    // A tick adds 200 N / 10 kg / 30, and the ground's damping leaves five sixths
    const float push = 200.0f / MASS * DT;
    hold(0, (Controls){.move = {1, 0}, .aim = {0, 0, 1}});
    tick(1);
    TIDE_CHECK(near(seat(0)->velocity.x, push * GROUND_KEEPS, 0.0005f));
    tick(89);
    // Where what's added is what's taken: v = (v + push) * 5/6, so v = 5 * push = 3.33 m/s
    const float top = push * GROUND_KEEPS / (1 - GROUND_KEEPS);
    printf("    top speed %.4f m/s (%.4f worked out)\n", (double)seat(0)->velocity.x, (double)top);
    TIDE_CHECK(near(top, 10.0f / 3.0f, 0.0001f));
    TIDE_CHECK(near(seat(0)->velocity.x, top, 0.002f) && near(seat(0)->velocity.z, 0, 0.0001f));
    const float before = seat(0)->position.x;
    tick(30);
    TIDE_CHECK(near(seat(0)->position.x - before, top, 0.005f));
    TIDE_CHECK(seat(0)->grounded && near(seat(0)->position.y, -REST, 0.002f));

    // A stick pushed further than all the way is all the way, any way it points
    stand(0, 0, 0);
    hold(0, (Controls){.move = {1, 1}, .aim = {0, 0, 1}});
    tick(90);
    TIDE_CHECK(near(flat(seat(0)->velocity), top, 0.002f) && near(seat(0)->velocity.x, seat(0)->velocity.z, 0.001f));

    // Let go, it stops: a sixth of its speed a tick
    hold(0, idle());
    tick(1);
    TIDE_CHECK(near(flat(seat(0)->velocity), top * GROUND_KEEPS, 0.002f));
    tick(60);
    TIDE_CHECK(flat(seat(0)->velocity) < 0.001f);
}

TIDE_TEST(cracked_the_air_gives_a_quarter_of_the_grounds_push)
{
    lab();
    put(0, 0, 50, 0);
    seat(0)->grounded = false;
    hold(0, (Controls){.move = {1, 0}, .aim = {0, 0, 1}});
    tick(1);
    const float push = 200.0f / MASS * DT;
    TIDE_CHECK(!seat(0)->grounded);
    TIDE_CHECK(near(seat(0)->velocity.x, push / 4 * AIR_KEEPS, 0.0005f));
    // ...and falls as gravity and the air's damping have it
    TIDE_CHECK(near(seat(0)->velocity.y, -GRAVITY * DT * AIR_KEEPS, 0.0005f));
}

TIDE_TEST(cracked_speed_over_the_ground_is_capped)
{
    lab();
    // A body going faster than 8.5 m/s is brought to it before its tick's push...
    seat(0)->velocity = tide_f3(30, 0, 0);
    tick(1);
    TIDE_CHECK(near(seat(0)->velocity.x, 8.5f * GROUND_KEEPS, 0.001f));
    // ...in the air too, where the push alone would end near 9.8 m/s
    put(0, 0, 300, 0);
    seat(0)->grounded = false;
    seat(0)->velocity = tide_f3(0, 0, 30);
    tick(1);
    TIDE_CHECK(near(seat(0)->velocity.z, 8.5f * AIR_KEEPS, 0.001f));
    hold(0, (Controls){.move = {0, 1}, .aim = {0, 0, 1}});
    float most = 0;
    for (int t = 0; t < 150; t++) {
        tick(1);
        if (flat(seat(0)->velocity) > most) most = flat(seat(0)->velocity);
    }
    const float push = 200.0f / MASS * DT / 4;
    TIDE_CHECK(near(most, (8.5f + push) * AIR_KEEPS, 0.001f));
    // Straight down isn't capped
    TIDE_CHECK(seat(0)->velocity.y < -15);
}

// ---------------------------------------------------------------------------
// Jumping

// How high a jump from rest goes over where it started, by the tick's own
// sums: the jump's speed and the ground's damping the tick it's made, then
// gravity and the air's damping until it turns.
static float jump_height(const float speed)
{
    float v = speed * GROUND_KEEPS;
    float y = v * DT;
    float top = y;
    while (v > 0) {
        v = (v - GRAVITY * DT) * AIR_KEEPS;
        y += v * DT;
        if (y > top) top = y;
    }
    return top;
}

// Ticks a seat with jump held from now until it's in the air, or -1.
static int jumps_after(const int most)
{
    for (int t = 1; t <= most; t++) {
        tick(1);
        if (!seat(0)->grounded || seat(0)->velocity.y > 1) return t;
    }
    return -1;
}

TIDE_TEST(cracked_a_jump_goes_as_high_as_its_speed_takes_it)
{
    lab();
    tick(5);
    const float from = seat(0)->position.y;
    hold(0, (Controls){.jump = true, .aim = {0, 0, 1}});
    tick(1);
    // 6 m/s, less the sixth the ground's damping takes the tick it leaves
    TIDE_CHECK(near(seat(0)->velocity.y, 6 * GROUND_KEEPS, 0.002f));
    TIDE_CHECK(seat(0)->jumpCount == 1 && seat(0)->timeSinceJump > 0 && seat(0)->timeSinceJump < 0.04f);
    hold(0, idle());
    float top = from;
    int landed = 0;
    for (int t = 2; t < 90 && !landed; t++) {
        tick(1);
        if (seat(0)->position.y > top) top = seat(0)->position.y;
        if (seat(0)->grounded) landed = t;
    }
    const float worked = jump_height(6);
    printf("    a jump rose %.4f m (%.4f worked out) and landed after %d ticks\n", (double)(top - from), (double)worked, landed);
    TIDE_CHECK(near(top - from, worked, 0.002f));
    TIDE_CHECK(near(worked, 1.183f, 0.002f));
    TIDE_CHECK(landed == 31 && seat(0)->jumpCount == 0);
    // The spring it lands on throws it back up a little, a few times, before it's at rest
    int hops = 0;
    for (int t = 0; t < 150; t++) {
        const bool was = seat(0)->grounded;
        tick(1);
        if (was && !seat(0)->grounded) hops++;
        TIDE_CHECK(seat(0)->position.y < 0.25f && seat(0)->position.y > -0.345f);
    }
    printf("    it left the ground %d times more before it was at rest\n", hops);
    TIDE_CHECK(seat(0)->grounded && near(seat(0)->position.y, -REST, 0.003f) && length(seat(0)->velocity) < 0.01f);
}

TIDE_TEST(cracked_a_jump_waits_a_fifth_of_a_second_for_the_next)
{
    lab();
    tick(5);
    hold(0, (Controls){.jump = true, .aim = {0, 0, 1}});
    // On the ground with jump held, 0.1 s since the last: 0.1 s to go, and the tick after
    seat(0)->timeSinceJump = 0.1f;
    const int waited = jumps_after(12);
    TIDE_CHECK(waited == 4 || waited == 5);
    // ...and from nothing, 0.2 s
    stand(0, 0, 0);
    seat(0)->timeSinceJump = 0;
    const int whole = jumps_after(12);
    printf("    held, the next jump came %d ticks after 0.1 s, and %d after none\n", waited, whole);
    TIDE_CHECK(whole == 7 || whole == 8);
    // Held all the way down, it jumps again the tick it lands
    int again = -1;
    for (int t = 1; t < 90 && again < 0; t++) {
        tick(1);
        if (seat(0)->timeSinceJump < 0.04f) again = t;
    }
    // What it lands with is still in the speed it leaves with: the jump adds to it
    TIDE_CHECK(again == 30 && seat(0)->jumpCount == 1 && seat(0)->velocity.y > 1 && seat(0)->velocity.y < 2.5f);
}

// A tap of jump, and the ticks after it with the button up.
static void tap_jump(const int after)
{
    hold(0, (Controls){.jump = true, .aim = {0, 0, 1}});
    tick(1);
    hold(0, idle());
    tick(after);
}

TIDE_TEST(cracked_there_is_no_second_jump_in_the_air_without_sky_bound)
{
    lab();
    tick(5);
    tap_jump(9);
    TIDE_CHECK(seat(0)->jumpCount == 1 && !seat(0)->grounded);
    const float before = seat(0)->velocity.y;
    tap_jump(0);
    // It fell on as it was falling
    TIDE_CHECK(seat(0)->jumpCount == 1 && near(seat(0)->velocity.y, (before - GRAVITY * DT) * AIR_KEEPS, 0.001f));
}

TIDE_TEST(cracked_sky_bound_gives_one_more_jump_in_the_air)
{
    lab_with(1, (const int[][2]){{0, SKY_BOUND}});
    tick(5);
    TIDE_CHECK(seat(0)->settings.extraJumps == 1 && seat(1)->settings.extraJumps == 0);
    tap_jump(9);
    TIDE_CHECK(seat(0)->jumpCount == 1 && !seat(0)->grounded);
    float before = seat(0)->velocity.y;
    tap_jump(0);
    TIDE_CHECK(seat(0)->jumpCount == 2 && near(seat(0)->velocity.y, (before + 6 - GRAVITY * DT) * AIR_KEEPS, 0.001f));
    // ...and no third
    tick(9);
    before = seat(0)->velocity.y;
    tap_jump(0);
    TIDE_CHECK(seat(0)->jumpCount == 2 && near(seat(0)->velocity.y, (before - GRAVITY * DT) * AIR_KEEPS, 0.001f));
    // The second needs the button pressed again: held from the first, there's none
    stand(0, 0, 0);
    tick(40);
    TIDE_REQUIRE(seat(0)->grounded && seat(0)->jumpCount == 0);
    hold(0, (Controls){.jump = true, .aim = {0, 0, 1}});
    tick(12);
    TIDE_CHECK(seat(0)->jumpCount == 1 && !seat(0)->grounded);
}

TIDE_TEST(cracked_a_jump_is_still_there_half_a_second_after_the_ground_goes)
{
    lab();
    tick(5);
    // The ground is gone from under it: 10 ticks on (0.33 s) it can still jump...
    put(0, 0, 30, 0);
    tick(10);
    TIDE_REQUIRE(!seat(0)->grounded && seat(0)->jumpCount == 0 && near(seat(0)->timeSinceGrounded, 10 * DT, 0.001f));
    float before = seat(0)->velocity.y;
    tap_jump(0);
    TIDE_CHECK(seat(0)->jumpCount == 1 && near(seat(0)->velocity.y, (before + 6 - GRAVITY * DT) * AIR_KEEPS, 0.001f));
    // ...once
    tick(9);
    before = seat(0)->velocity.y;
    tap_jump(0);
    TIDE_CHECK(seat(0)->jumpCount == 1 && near(seat(0)->velocity.y, (before - GRAVITY * DT) * AIR_KEEPS, 0.001f));

    // ...and 17 ticks on (0.57 s) it can't
    stand(0, 0, 0);
    tick(20);
    put(0, 0, 30, 0);
    tick(17);
    TIDE_REQUIRE(!seat(0)->grounded && seat(0)->jumpCount == 0);
    before = seat(0)->velocity.y;
    tap_jump(0);
    TIDE_CHECK(seat(0)->jumpCount == 0 && near(seat(0)->velocity.y, (before - GRAVITY * DT) * AIR_KEEPS, 0.001f));
}

// ---------------------------------------------------------------------------
// Shooting

// The fire point, from the feet of a body that faces yaw 0: 0.683 m to its
// right, 1.407 m up and 0.177 m ahead.
static const tide_float3 FIRE_POINT = {0.683f, 1.407f, 0.177f};

static Controls firing(const float x, const float y, const float z)
{
    return (Controls){.fire = true, .aim = {x, y, z}};
}

TIDE_TEST(cracked_three_shots_a_second_five_to_a_magazine_and_three_seconds_to_reload)
{
    lab();
    hold(0, firing(0, 0.6f, 0.8f));
    int shots[12];
    int shot = 0;
    for (int t = 1; t <= 330 && shot < 12; t++) {
        const int before = arena()->nextBullet;
        tick(1);
        if (arena()->nextBullet == before) continue;
        TIDE_CHECK(arena()->nextBullet == before + 1);
        shots[shot++] = t;
        // The fifth empties it, and the reload starts with that shot
        TIDE_CHECK(seat(0)->magazine == 5 - shot % 5 || (shot % 5 == 0 && seat(0)->magazine == 0 && seat(0)->reloading));
    }
    TIDE_REQUIRE(shot == 12);
    // A shot every ten ticks, and the one after a reload 3 s on: 90 ticks, and the tick the magazine is full in
    for (int i = 1; i < 12; i++) {
        if (i % 5 != 0) TIDE_CHECK(shots[i] - shots[i - 1] == 10);
        else TIDE_CHECK(shots[i] - shots[i - 1] == 90 || shots[i] - shots[i - 1] == 91);
    }
    printf("    the shot after a reload came %d ticks after the magazine's last\n", shots[5] - shots[4]);
    // The other player fired nothing
    TIDE_CHECK(seat(1)->magazine == 5 && !seat(1)->reloading);
}

TIDE_TEST(cracked_reload_fills_a_magazine_that_isnt_full)
{
    lab();
    // Full: there's nothing to reload
    hold(0, (Controls){.reload = true, .aim = {0, 0, 1}});
    tick(3);
    TIDE_CHECK(!seat(0)->reloading && seat(0)->magazine == 5);
    hold(0, firing(0, 0.6f, 0.8f));
    tick(11);
    TIDE_REQUIRE(seat(0)->magazine == 3);
    hold(0, (Controls){.reload = true, .aim = {0, 0, 1}});
    tick(1);
    TIDE_CHECK(seat(0)->reloading && seat(0)->magazine == 3 && near(seat(0)->reloadLeft, 3 - DT, 0.001f));
    // It can't fire while it reloads
    hold(0, firing(0, 0.6f, 0.8f));
    const int fired = arena()->nextBullet;
    tick(87);
    TIDE_CHECK(seat(0)->reloading && seat(0)->magazine == 3 && arena()->nextBullet == fired);
    tick(4);
    TIDE_CHECK(!seat(0)->reloading && arena()->nextBullet == fired + 1 && seat(0)->magazine == 4);
}

TIDE_TEST(cracked_a_bullet_leaves_the_fire_point_along_the_aim_and_drops)
{
    lab();
    tick(5);
    const tide_float3 feet = seat(0)->position;
    hold(0, firing(0, 0, 1));
    tick(1);
    hold(0, idle());
    TIDE_REQUIRE(bullets() == 1);
    // Made 1 cm along its way from the fire point, it has flown its first tick: 30 m/s, a meter
    tide_float3 at = {feet.x + FIRE_POINT.x, feet.y + FIRE_POINT.y, feet.z + FIRE_POINT.z + 0.01f + 30 * DT};
    TIDE_CHECK(length(minus(bullet(0)->position, at)) < 0.0005f);
    TIDE_CHECK(bullet(0)->seat == 0 && bullet(0)->original && bullet(0)->bounces == 0 && near(bullet(0)->props.damage, 15, 0));
    // Gravity is 10: a third of a meter a second more, each tick, after the tick's move
    float fall = -10 * DT;
    TIDE_CHECK(near(bullet(0)->velocity.z, 30, 0.0001f) && near(bullet(0)->velocity.y, fall, 0.0001f) && bullet(0)->velocity.x == 0);
    for (int t = 0; t < 8; t++) {
        tick(1);
        at.y += fall * DT;
        at.z += 30 * DT;
        fall -= 10 * DT;
    }
    TIDE_REQUIRE(bullets() == 1);
    TIDE_CHECK(length(minus(bullet(0)->position, at)) < 0.001f && near(bullet(0)->velocity.y, fall, 0.0005f));
    // Whoever shot it wasn't in its way
    TIDE_CHECK(seat(0)->health == 100 && length(seat(0)->velocity) < 0.001f);

    // Turned to the right, the fire point turns with the camera
    stand(0, 0, 0);
    tick(30);
    TIDE_REQUIRE(bullets() == 0);
    hold(0, (Controls){.fire = true, .aim = {1, 0, 0}, .yaw = 90});
    tick(1);
    TIDE_REQUIRE(bullets() == 1);
    at = tide_f3(seat(0)->position.x + FIRE_POINT.z + 0.01f + 30 * DT, seat(0)->position.y + FIRE_POINT.y, seat(0)->position.z - FIRE_POINT.x);
    TIDE_CHECK(length(minus(bullet(0)->position, at)) < 0.001f);
}

TIDE_TEST(cracked_a_bullet_is_gone_at_the_first_wall_it_hits)
{
    lab();
    wall(0, 5, 6, 10, 5, 0.5f);
    tick(5);
    hold(0, firing(0, 0, 1));
    tick(1);
    hold(0, idle());
    // 5.5 m to the wall's face, less its own 0.2: the sixth tick's move meets it
    for (int t = 0; t < 4; t++) {
        tick(1);
        TIDE_CHECK(bullets() == 1 && bullet(0)->bounces == 0);
    }
    tick(1);
    // The Unity game's bullet counts its hits against maxBounces, which is 1: its first hit is its last
    TIDE_CHECK(bullets() == 0);
}

TIDE_TEST(cracked_rebound_hex_bounces_twice_and_is_gone_at_the_third_hit)
{
    lab_with(1, (const int[][2]){{0, REBOUND_HEX}});
    wall(0, 5, 4, 30, 5, 0.5f);
    wall(0, 5, -4, 30, 5, 0.5f);
    tick(5);
    TIDE_CHECK(near(seat(0)->settings.reloadTime, 3.25f, 0.0001f));
    // A little to the right, so it doesn't come back through who shot it
    hold(0, firing(0.4f, 0, 1));
    tick(1);
    hold(0, idle());
    TIDE_REQUIRE(bullets() == 1 && bullet(0)->props.maxBounces == 3);
    int seen = 0;
    int last = -1;
    float way = bullet(0)->velocity.z;
    for (int t = 0; t < 60 && bullets() == 1; t++) {
        if (bullet(0)->bounces != seen) {
            // Off a wall across z: that part of its speed is turned back, the rest kept
            TIDE_CHECK(bullet(0)->bounces == seen + 1 && near(bullet(0)->velocity.z, -way, 0.001f) && bullet(0)->velocity.x > 10);
            seen = bullet(0)->bounces;
            way = bullet(0)->velocity.z;
        }
        last = t;
        tick(1);
    }
    TIDE_CHECK(seen == 2 && bullets() == 0 && last < 30);
    TIDE_CHECK(seat(0)->health == 100);
}

TIDE_TEST(cracked_a_bullet_lasts_three_seconds)
{
    lab();
    tick(5);
    // Up and away, over the floor's end
    hold(0, firing(0, 0.6f, 0.8f));
    tick(1);
    hold(0, idle());
    tick(88);
    TIDE_REQUIRE(bullets() == 1);
    TIDE_CHECK(bullet(0)->lifetime > 2.9f && bullet(0)->lifetime < 3 && bullet(0)->position.z > 60);
    tick(2);
    TIDE_CHECK(bullets() == 0);
}

// The first seat's shot at the second, who stands along the way the fire point
// is from the first's middle: the bullet is on a line through both, so one
// sent back comes back to who shot it.
static const float OUT_X = 0.96802f;
static const float OUT_Z = 0.25086f;

static void face_off(const float far)
{
    stand(0, 0, 0);
    stand(1, FIRE_POINT.x + OUT_X * far, FIRE_POINT.z + OUT_Z * far);
}

static void shoot_once(void)
{
    hold(0, (Controls){.fire = true, .aim = {OUT_X, 0, OUT_Z}});
    tick(1);
    hold(0, (Controls){.aim = {OUT_X, 0, OUT_Z}});
}

// Ticks until the bullets are gone, 60 at the most.
static int until_gone(void)
{
    int t = 0;
    while (bullets() > 0 && t < 60) {
        tick(1);
        t++;
    }
    return t;
}

TIDE_TEST(cracked_a_hit_takes_fifteen_health_and_knocks_back)
{
    lab();
    face_off(6);
    tick(5);
    shoot_once();
    TIDE_REQUIRE(bullets() == 1);
    const int flew = until_gone();
    TIDE_CHECK(flew >= 4 && flew <= 6 && bullets() == 0);
    TIDE_CHECK(seat(1)->health == 85 && seat(0)->health == 100 && near(seat(1)->timeSinceWasShot, 0, 0.0001f));
    // 100 N s on 10 kg: 10 m/s along the way it was going, a tenth of that up, as one push
    // (and 0.15 N s more where it hit), of which the ground's damping takes its sixth that tick
    const tide_float3 knock = seat(1)->velocity;
    const float along = knock.x * OUT_X + knock.z * OUT_Z;
    const float worked = (100 / tide_sqrt_f(1.01f) + 0.15f / tide_sqrt_f(1.01f)) / MASS * GROUND_KEEPS;
    printf("    knocked back at %.3f m/s (%.3f worked out), %.3f up\n", (double)along, (double)worked, (double)knock.y);
    TIDE_CHECK(near(along, worked, 0.05f) && fabsf(knock.x * OUT_Z - knock.z * OUT_X) < 0.05f);
    TIDE_CHECK(near(worked, 8.30f, 0.02f) && knock.y > 0.5f && knock.y < 1.5f);
    // The round goes on: nobody's dead
    TIDE_CHECK(arena()->phase == Phase_Running && !seat(1)->dead);
}

TIDE_TEST(cracked_a_dead_players_team_loses_the_round)
{
    lab();
    face_off(6);
    seat(1)->health = 15;
    tick(5);
    shoot_once();
    until_gone();
    TIDE_CHECK(seat(1)->health == 0 && arena()->score.at[0] == 0);
    // It's dead the tick after, and the round is the other team's
    tick(1);
    TIDE_CHECK(seat(1)->dead && seat(1)->alive && !seat(0)->dead);
    TIDE_CHECK(arena()->phase == Phase_RoundEnd && arena()->score.at[0] == 1 && arena()->score.at[1] == 0 && arena()->lastWinningTeam == 0);
    // The dead do nothing
    hold(1, (Controls){.move = {1, 0}, .fire = true, .shield = true, .jump = true, .aim = {0, 0, 1}});
    const int fired = arena()->nextBullet;
    tick(20);
    TIDE_CHECK(arena()->nextBullet == fired && !seat(1)->shieldActive && seat(1)->jumpCount == 0);
    // ...and whoever's left still moves until the round is over
    hold(0, (Controls){.move = {0, -1}, .aim = {0, 0, 1}});
    tick(20);
    TIDE_CHECK(seat(0)->position.z < -1);
}

// ---------------------------------------------------------------------------
// The shield

TIDE_TEST(cracked_a_shield_is_up_for_six_tenths_of_a_second_and_back_in_five)
{
    lab();
    tick(5);
    hold(0, (Controls){.shield = true, .aim = {0, 0, 1}});
    int up = 0;
    int down = 0;
    int again = 0;
    for (int t = 1; t <= 200 && !again; t++) {
        tick(1);
        if (!down && seat(0)->shieldActive) up = t;
        if (up && !down && !seat(0)->shieldActive) down = t;
        if (down && seat(0)->shieldActive) again = t;
    }
    printf("    held, a shield was up for %d ticks and up again %d ticks after it went\n", up, again - down);
    TIDE_CHECK(up == 17 || up == 18);
    // Five seconds from when it went, and the tick that finds it ready (the count is in floats: a tick more)
    TIDE_CHECK(again - down == 151 || again - down == 152);
    TIDE_CHECK(near(seat(0)->shieldActual, (float)(up + 2) * DT, 0.01f));
}

TIDE_TEST(cracked_a_shield_sends_a_bullet_back_to_who_shot_it)
{
    lab();
    face_off(6);
    tick(5);
    hold(1, (Controls){.shield = true, .aim = {0, 0, 1}});
    shoot_once();
    hold(1, idle());
    TIDE_REQUIRE(bullets() == 1 && seat(1)->shieldActive);
    const float speed = length(bullet(0)->velocity);
    int back = 0;
    for (int t = 1; t < 12 && !back; t++) {
        tick(1);
        TIDE_REQUIRE(bullets() == 1);
        if (bullet(0)->bounces == 1) back = t;
    }
    TIDE_REQUIRE(back > 0);
    // Its speed is turned around, all of it, and it counts as a bounce: now it can hit who shot it
    const tide_float3 v = bullet(0)->velocity;
    TIDE_CHECK(v.x * OUT_X + v.z * OUT_Z < -29 && near(length(v), speed, 0.2f));
    TIDE_CHECK(seat(1)->health == 100 && length(seat(1)->velocity) < 0.001f);
    // The shield's time starts again with what it stopped
    TIDE_CHECK(seat(1)->shieldActive && seat(1)->shieldTimer == 0.6f && seat(1)->shieldCooldown == 5);
    until_gone();
    TIDE_CHECK(seat(0)->health == 85 && seat(1)->health == 100);
}

TIDE_TEST(cracked_a_shield_that_has_been_up_too_long_isnt_kept_up_by_what_it_stops)
{
    lab();
    face_off(6);
    tick(5);
    hold(1, (Controls){.shield = true, .aim = {0, 0, 1}});
    tick(1);
    hold(1, idle());
    // The Unity game counts all the time a shield has ever been up, and never starts the count again:
    // past three shields' worth (1.8 s), stopping something no longer renews it
    TIDE_CHECK(near(seat(1)->shieldActual, DT, 0.0001f));
    seat(1)->shieldActual = 1.85f;
    shoot_once();
    for (int t = 0; t < 8 && bullets() == 1 && bullet(0)->bounces == 0; t++) tick(1);
    TIDE_REQUIRE(bullets() == 1 && bullet(0)->bounces == 1);
    // Sent back all the same, with the shield's time as it was
    TIDE_CHECK(seat(1)->health == 100 && seat(1)->shieldActive && seat(1)->shieldTimer < 0.45f);
    // ...and the count isn't started again when the shield goes
    tick(30);
    TIDE_CHECK(!seat(1)->shieldActive && seat(1)->shieldActual > 2.3f);
}

// ---------------------------------------------------------------------------
// The floor under the map

// The piece that's the floor under the map.
static const Piece *damage_floor(void)
{
    for (int i = 0; i < pieces(); i++)
        if (piece(i)->damageFloor) return piece(i);
    return NULL;
}

TIDE_TEST(cracked_the_floor_under_a_map_hurts_and_throws_and_three_landings_kill)
{
    begin(2, 0);
    const Piece *floor = damage_floor();
    TIDE_REQUIRE(floor != NULL);
    const float top = floor->position.y + floor->half.y;
    TIDE_CHECK(near(top, -7, 0.001f) && arena()->floorDamage == 35);
    // Off the map: 60 m out, where there's nothing over the floor
    put(0, 60, 2, 60);
    int landings = 0;
    int health = 100;
    float before = 0;
    float depth = 0;
    for (int t = 0; t < 600 && !seat(0)->dead; t++) {
        before = seat(0)->velocity.y;
        depth = top - seat(0)->position.y;
        tick(1);
        if (seat(0)->health == health) continue;
        landings++;
        // 35 a landing, with its feet in the floor's top
        TIDE_CHECK(seat(0)->health == (health > 35 ? health - 35 : 0));
        TIDE_CHECK(depth >= 0 && depth <= 0.35f);
        // 33 m/s more than it fell at, with the spring's push, then the tick's gravity and the ground's damping
        const float worked = (before + 33 + FLOAT_STRENGTH * (depth / FLOAT_HEIGHT) / MASS * DT - GRAVITY * DT) * GROUND_KEEPS;
        if (landings == 1)
            printf("    it fell at %.2f m/s, %.3f m into the floor, and left at %.2f m/s (%.2f worked out)\n", (double)before, (double)depth,
                   (double)seat(0)->velocity.y, (double)worked);
        if (seat(0)->health > 0) TIDE_CHECK(near(seat(0)->velocity.y, worked, 0.02f) && seat(0)->velocity.y > 12);
        health = seat(0)->health;
    }
    TIDE_CHECK(landings == 3 && seat(0)->dead && seat(0)->health == 0);
    // ...and the round is the other team's
    TIDE_CHECK(arena()->phase == Phase_RoundEnd && arena()->score.at[1] == 1);
}

TIDE_TEST(cracked_a_shield_stops_the_floors_damage_but_not_its_throw)
{
    lab();
    // A floor that hurts, in place of the lab's
    tide_text_use(&world.heap, NULL);
    tide_list_clear(&world.Arena.pieces, sizeof(Piece));
    tide_text_use(NULL, NULL);
    box(0, -0.5f, 0, 60, 0.5f, 60, true);
    put(0, 0, 1, 0);
    put(1, 0, 1, 20);
    hold(0, (Controls){.shield = true, .aim = {0, 0, 1}});
    tick(1);
    hold(0, idle());
    int landed = 0;
    for (int t = 1; t < 40 && !landed; t++) {
        const float before = seat(0)->velocity.y;
        tick(1);
        if (seat(0)->velocity.y > before + 10) landed = t;
    }
    TIDE_REQUIRE(landed > 0);
    TIDE_CHECK(seat(0)->health == 100 && seat(0)->velocity.y > 15 && seat(0)->shieldActive);
    // What it stopped starts the shield's time again
    TIDE_CHECK(near(seat(0)->shieldTimer, 0.6f - DT, 0.001f));
    // The other, with no shield, is hurt as it's thrown
    TIDE_CHECK(seat(1)->health == 65 && seat(1)->velocity.y > 15);
}

// ---------------------------------------------------------------------------
// Rounds and cards

// Ticks until the match's phase isn't this one, 4000 at the most.
static int ticks_in(const Phase phase)
{
    int t = 0;
    while (arena()->phase == phase && t < 4000) {
        tick(1);
        t++;
    }
    return t;
}

TIDE_TEST(cracked_a_point_goes_to_the_team_left_standing_and_two_make_a_round)
{
    begin(2, 3);
    point(0);
    TIDE_CHECK(arena()->phase == Phase_RoundEnd && arena()->lastWinningTeam == 0);
    TIDE_CHECK(arena()->score.at[0] == 1 && arena()->score.at[1] == 0 && arena()->rounds.at[0] == 0);
    TIDE_CHECK(count_in(&world, world.Arena.needsUpgrades) == 0);
    // Four seconds of the round's end (one tick of them is behind), then the next warmup, on the same map
    const int ended = ticks_in(Phase_RoundEnd);
    TIDE_CHECK(ended == 119 || ended == 120);
    TIDE_CHECK(arena()->phase == Phase_Warmup && arena()->futureMap == 3 && arena()->map == 3);
    TIDE_CHECK(!seat(0)->alive && !seat(1)->alive && bullets() == 0);
    // The old map takes two seconds to go, and its players come with the new one
    int made = 0;
    for (int t = 1; t < 100 && !made; t++) {
        tick(1);
        if (seat(0)->alive) made = t;
    }
    TIDE_CHECK(made == 60 || made == 61);
    TIDE_CHECK(seat(1)->alive && !seat(1)->dead && seat(1)->health == 100 && arena()->map == 3 && arena()->mapPhase == MapPhase_CreateNew);
    const int warmed = made + ticks_in(Phase_Warmup);
    TIDE_CHECK(warmed == 180 || warmed == 181);
    TIDE_CHECK(arena()->phase == Phase_Running && arena()->mapPhase == MapPhase_Idle);

    // The second point is the round: the points start again, and the losers are owed a card
    point(0);
    TIDE_CHECK(arena()->phase == Phase_RoundEnd && arena()->rounds.at[0] == 1 && arena()->rounds.at[1] == 0);
    TIDE_CHECK(arena()->score.at[0] == 0 && arena()->score.at[1] == 0 && count_in(&world, world.Arena.needsUpgrades) == 1);
    ticks_in(Phase_RoundEnd);
    TIDE_REQUIRE(arena()->phase == Phase_Upgrade);
    TIDE_CHECK(arena()->upgrading == 1 && arena()->revision == 1 && arena()->selectionAge < 0.1f);
    // Five cards, no two the same
    for (int i = 0; i < CARDS; i++) {
        TIDE_CHECK(arena()->cards.at[i] >= 0 && arena()->cards.at[i] < UPGRADES);
        for (int j = 0; j < i; j++) TIDE_CHECK(arena()->cards.at[i] != arena()->cards.at[j]);
    }
}

TIDE_TEST(cracked_both_dead_is_nobodys_point)
{
    begin(2, 1);
    seat(0)->health = 0;
    seat(1)->health = 0;
    tick(2);
    TIDE_CHECK(arena()->phase == Phase_RoundEnd && arena()->score.at[0] == 0 && arena()->score.at[1] == 0);
    // With no points on the board, the next warmup's map is a new one
    ticks_in(Phase_RoundEnd);
    TIDE_CHECK(arena()->phase == Phase_Warmup && arena()->futureMap != 1 && arena()->futureMap >= 0);
}

// A match of two players, at the cards the second is dealt for a lost round.
static void to_cards(void)
{
    begin(2, -1);
    point(0);
    hurry_to(Phase_Running);
    point(0);
    hurry_to(Phase_Upgrade);
}

TIDE_TEST(cracked_a_card_is_picked_by_whose_turn_it_is_from_the_cards_dealt)
{
    to_cards();
    TIDE_REQUIRE(arena()->phase == Phase_Upgrade && arena()->upgrading == 1);
    const int revision = arena()->revision;
    // Not theirs to pick
    pick(0, 0, revision);
    // Cards that were dealt before these, or never
    pick(1, 0, revision - 1);
    pick(1, 0, revision + 1);
    // Not a card
    pick(1, CARDS, revision);
    pick(1, -1, revision);
    pick(1, 1000000, revision);
    TIDE_CHECK(arena()->phase == Phase_Upgrade && arena()->upgrading == 1 && arena()->revision == revision);
    TIDE_CHECK(count_in(&world, world.Arena.picks) == 0);

    const int third = arena()->cards.at[2];
    pick(1, 2, revision);
    TIDE_CHECK(count_in(&world, world.Arena.picks) == 1 && picked(0) == 1 * 64 + third);
    // Nobody else is owed one: the next warmup
    TIDE_CHECK(arena()->phase == Phase_Warmup && arena()->upgrading == -1);
    // Picked again, it's too late
    pick(1, 0, revision);
    TIDE_CHECK(count_in(&world, world.Arena.picks) == 1);
}

TIDE_TEST(cracked_a_card_is_the_players_from_their_next_spawn)
{
    to_cards();
    arena()->cards.at[3] = CRYSTAL_BASTION;
    pick(1, 3, arena()->revision);
    TIDE_REQUIRE(picked(0) == 64 + CRYSTAL_BASTION);
    hurry_to(Phase_Running);
    // Crystal Bastion: twice the health, three quarters of the fire rate rounded up (3 again), a slower reload
    TIDE_CHECK(seat(1)->maxHealth == 200 && seat(1)->health == 200 && seat(1)->settings.fireRate == 3);
    TIDE_CHECK(near(seat(1)->settings.reloadTime, 3.25f, 0.0001f) && seat(1)->magazine == 5);
    TIDE_CHECK(seat(0)->maxHealth == 100 && near(seat(0)->settings.reloadTime, 3, 0));

    // Another round lost, another card: Arcstorm, over the first
    point(0);
    hurry_to(Phase_Running);
    TIDE_CHECK(seat(1)->maxHealth == 200);
    point(0);
    hurry_to(Phase_Upgrade);
    TIDE_REQUIRE(arena()->upgrading == 1 && arena()->revision == 2);
    arena()->cards.at[0] = ARCSTORM;
    pick(1, 0, 2);
    hurry_to(Phase_Running);
    TIDE_CHECK(seat(1)->maxHealth == 200 && seat(1)->settings.magazineSize == 10 && seat(1)->magazine == 10);
    TIDE_CHECK(seat(1)->settings.fireRate == 9 && seat(1)->settings.spread == 5 && near(seat(1)->settings.reloadTime, 3.5f, 0.0001f));
    // Nine shots a second is one every three ticks, each of 0.3 of the damage, within 2.5 degrees of the aim
    // across and 2.5 up or down
    hold(1, (Controls){.fire = true, .aim = {0, 0.6f, 0.8f}});
    int shots = 0;
    for (int t = 0; t < 9; t++) {
        const int before = arena()->nextBullet;
        tick(1);
        if (arena()->nextBullet == before) continue;
        TIDE_CHECK(t % 3 == 0 && arena()->nextBullet == before + 1);
        const Bullet *shot = bullet(bullets() - 1);
        TIDE_CHECK(shot->seat == 1 && near(shot->props.damage, 4.5f, 0.0001f));
        // cos(3.6 degrees) is 0.998; a bullet's tick of gravity is in its speed by now
        const float along = (shot->velocity.y + 10 * DT) * 0.6f + shot->velocity.z * 0.8f;
        TIDE_CHECK(along > 30 * 0.998f && along < 30 * 0.99999f);
        shots++;
    }
    TIDE_CHECK(shots == 3 && seat(1)->magazine == 7);
}

TIDE_TEST(cracked_a_bot_takes_its_first_card_after_four_seconds)
{
    begin(1, -1);
    point(0);
    hurry_to(Phase_Running);
    point(0);
    ticks_in(Phase_RoundEnd);
    TIDE_REQUIRE(arena()->phase == Phase_Upgrade && arena()->upgrading == 1 && seat(1)->bot);
    const int first = arena()->cards.at[0];
    const int took = ticks_in(Phase_Upgrade);
    TIDE_CHECK(took == 120 || took == 121);
    TIDE_CHECK(count_in(&world, world.Arena.picks) == 1 && picked(0) == 64 + first && arena()->phase == Phase_Warmup);
}

TIDE_TEST(cracked_a_player_who_doesnt_pick_has_their_first_card_after_a_minute)
{
    to_cards();
    const int first = arena()->cards.at[0];
    tick(1795);
    TIDE_CHECK(arena()->phase == Phase_Upgrade && count_in(&world, world.Arena.picks) == 0);
    const int took = 1795 + ticks_in(Phase_Upgrade);
    TIDE_CHECK(took >= 1800 && took <= 1802);
    TIDE_CHECK(count_in(&world, world.Arena.picks) == 1 && picked(0) == 64 + first && arena()->phase == Phase_Warmup);
}

TIDE_TEST(cracked_everyone_of_a_losing_team_picks_in_turn)
{
    begin(4, -1);
    point(1);
    hurry_to(Phase_Running);
    point(1);
    hurry_to(Phase_Upgrade);
    // The first team's two, the lower seat first, each with cards dealt for them
    TIDE_REQUIRE(arena()->upgrading == 0 && arena()->revision == 1);
    const int first = arena()->cards.at[1];
    pick(0, 1, 1);
    TIDE_REQUIRE(arena()->phase == Phase_Upgrade && arena()->upgrading == 2 && arena()->revision == 2);
    // The first's cards aren't the second's to pick from
    pick(2, 1, 1);
    TIDE_CHECK(arena()->upgrading == 2);
    const int second = arena()->cards.at[4];
    pick(2, 4, 2);
    TIDE_CHECK(arena()->phase == Phase_Warmup && count_in(&world, world.Arena.picks) == 2);
    TIDE_CHECK(picked(0) == first && picked(1) == 2 * 64 + second);
}

TIDE_TEST(cracked_five_rounds_win_the_game_and_a_new_one_starts)
{
    begin(1, -1);
    int points = 0;
    while (arena()->phase != Phase_GameOver && points < 30) {
        hurry_to(Phase_Running);
        TIDE_CHECK(arena()->rounds.at[0] == points / 2 && arena()->score.at[0] == points % 2);
        point(0);
        points++;
    }
    // Ten points: the tenth is the fifth round, and nobody picks a card after it
    TIDE_CHECK(points == 10 && arena()->phase == Phase_GameOver && arena()->rounds.at[0] == 5 && arena()->rounds.at[1] == 0);
    TIDE_CHECK(count_in(&world, world.Arena.picks) == 4 && arena()->upgrading == -1);
    // Twelve seconds on (one tick of them is behind), a new game: no rounds, no cards, and a warmup
    const int over = ticks_in(Phase_GameOver);
    TIDE_CHECK(over >= 358 && over <= 360);
    tick(1);
    TIDE_CHECK(arena()->phase == Phase_Warmup && arena()->rounds.at[0] == 0 && arena()->rounds.at[1] == 0);
    TIDE_CHECK(count_in(&world, world.Arena.picks) == 0 && count_in(&world, world.Arena.needsUpgrades) == 0);
    hurry_to(Phase_Running);
    TIDE_CHECK(seat(0)->alive && seat(1)->alive && seat(1)->maxHealth == 100 && count_in(&world, world.Arena.bulletMods) == 0);
}

TIDE_TEST(cracked_restart_starts_the_game_again)
{
    to_cards();
    pick(1, 0, 1);
    hurry_to(Phase_Running);
    point(1);
    TIDE_REQUIRE(arena()->rounds.at[0] == 1 && arena()->score.at[1] == 1 && count_in(&world, world.Arena.picks) == 1);
    uint8_t bytes[TIDE_COMMAND_BYTES];
    const uint32_t size = restart_bytes(bytes);
    // From somebody who isn't in the match, it's nothing
    command(&world, 5, bytes, size);
    tick(1);
    TIDE_CHECK(arena()->rounds.at[0] == 1 && arena()->score.at[1] == 1);
    command(&world, 1, bytes, size);
    tick(2);
    TIDE_CHECK(arena()->rounds.at[0] == 0 && arena()->score.at[0] == 0 && arena()->score.at[1] == 0 && arena()->lastWinningTeam == -1);
    TIDE_CHECK(count_in(&world, world.Arena.picks) == 0 && arena()->phase == Phase_Warmup);
    hurry_to(Phase_Running);
    TIDE_CHECK(seat(0)->alive && seat(1)->alive && !seat(1)->dead && seat(1)->maxHealth == 100);
}

// ---------------------------------------------------------------------------
// Maps

TIDE_TEST(cracked_every_map_is_played_before_any_is_played_again)
{
    begin(1, -1);
    int played[3 * MAPS];
    for (int round = 0; round < 3 * MAPS; round++) {
        hurry_to(Phase_Running);
        played[round] = arena()->map;
        // The teams take the rounds in turn, so no game is over for a while. The map is kept between a round's points
        point(round % 2);
        hurry_to(Phase_Running);
        TIDE_CHECK(arena()->map == played[round] && arena()->score.at[round % 2] == 1);
        point(round % 2);
    }
    for (int bag = 0; bag < 3; bag++) {
        int seen = 0;
        for (int i = 0; i < MAPS; i++) {
            TIDE_CHECK(played[bag * MAPS + i] >= 0 && played[bag * MAPS + i] < MAPS);
            seen |= 1 << played[bag * MAPS + i];
        }
        TIDE_CHECK(seen == (1 << MAPS) - 1);
    }
    // ...and none twice in a row, where one bag ends and the next starts either
    for (int round = 1; round < 3 * MAPS; round++) TIDE_CHECK(played[round] != played[round - 1]);
    printf("    the first bag: %d %d %d %d %d %d %d\n", played[0], played[1], played[2], played[3], played[4], played[5], played[6]);
}

// The widest flat piece a mover has, or -1.
static int deck_of(const int m)
{
    int best = -1;
    float widest = 0;
    for (int i = 0; i < pieces(); i++) {
        const Piece *it = piece(i);
        if (it->mover != m || !it->solid || it->ignoreRaycast) continue;
        if (it->half.x * it->half.z > widest) {
            widest = it->half.x * it->half.z;
            best = i;
        }
    }
    return best;
}

// A place on a piece's top, in the world: `along` is -1 to 1 across its z.
static tide_float3 on_top(const Piece *it, const float along)
{
    const tide_float3 from = turned(it->rotation, tide_f3(0, it->half.y, it->half.z * along));
    return tide_f3(it->position.x + from.x, it->position.y + from.y, it->position.z + from.z);
}

TIDE_TEST(cracked_a_platform_that_moves_carries_who_stands_on_it)
{
    // Map1's four platforms go across and along; Map4's go up and down too
    for (int map = 0; map <= 2; map += 2) {
        begin(2, map);
        const int movers = count_in(&world, world.Arena.movers);
        TIDE_CHECK(movers == (map == 0 ? 4 : 10));
        float worst = 0;
        float furthest = 0;
        for (int m = 0; m < movers; m++) {
            TIDE_REQUIRE(mover(m)->kind == MoverKind_Slide);
            const int deck = deck_of(m);
            TIDE_REQUIRE(deck >= 0);
            const tide_float3 top = on_top(piece(deck), 0);
            const bool lift = mover(m)->travel.y != 0;
            put(0, top.x, top.y - REST, top.z);
            int airborne = 0;
            float gone = 0;
            float over = 0;
            float under = 0;
            tide_float3 last = piece(deck)->position;
            // Five seconds: far enough for every one of them to have gone a few meters
            for (int t = 0; t < 150; t++) {
                tick(1);
                const tide_float3 now = on_top(piece(deck), 0);
                const float off = flat(minus(seat(0)->position, now));
                if (off > worst) worst = off;
                if (!seat(0)->grounded) airborne++;
                gone += length(minus(piece(deck)->position, last));
                last = piece(deck)->position;
                if (seat(0)->position.y - now.y > over) over = seat(0)->position.y - now.y;
                if (now.y - seat(0)->position.y > under) under = now.y - seat(0)->position.y;
            }
            if (gone > furthest) furthest = gone;
            TIDE_CHECK(gone > 5 && seat(0)->health == 100);
            // On one that goes across, a player stays on its spring. One that goes up and down at 2 m/s
            // throws them half a meter where it turns at its top, and leaves them behind on its way down,
            // where they fall after it: off it for a part of the way, never far over it, never through it
            if (lift) TIDE_CHECK(airborne < 50 && over < 0.6f && under < 0.34f);
            else TIDE_CHECK(airborne == 0 && over == 0 && under < REST + 0.01f);
        }
        printf("    on Map%d's platforms a player was never more than %.3f m from where they stood, over up to %.1f m\n", map == 0 ? 1 : 4,
               (double)worst, (double)furthest);
        TIDE_CHECK(worst < 0.05f);
    }
}

TIDE_TEST(cracked_a_bridge_keeps_who_stands_on_it)
{
    begin(2, 4);
    const int movers = count_in(&world, world.Arena.movers);
    TIDE_REQUIRE(movers == 9);
    int halves = 0;
    for (int m = 0; m < movers; m++) {
        if (mover(m)->kind != MoverKind_HingeA && mover(m)->kind != MoverKind_HingeB) continue;
        halves++;
        const int deck = deck_of(m);
        TIDE_REQUIRE(deck >= 0);
        // Half way out along the half
        const float along = 0.5f;
        const tide_float3 top = on_top(piece(deck), along);
        put(0, top.x, top.y + 0.05f, top.z);
        float low = 1000;
        float high = -1000;
        int airborne = 0;
        // Sixteen seconds: down to 20 degrees one way, and back up to 20 the other
        for (int t = 0; t < 480; t++) {
            tick(1);
            const Piece *it = piece(deck);
            // Where the bottom of its body is, in the half's own box: over the top, never in it
            const tide_float3 body = inside(it, tide_f3(seat(0)->position.x, seat(0)->position.y + 0.59f, seat(0)->position.z));
            TIDE_CHECK(fabsf(body.x) < it->half.x && fabsf(body.z) < it->half.z);
            TIDE_CHECK(body.y > it->half.y + 0.2f && body.y < it->half.y + 1.2f);
            if (seat(0)->position.y < low) low = seat(0)->position.y;
            if (seat(0)->position.y > high) high = seat(0)->position.y;
            if (!seat(0)->grounded) airborne++;
        }
        // Its half going down leaves it behind a little, and it falls after it: off it for a part of the way
        TIDE_CHECK(seat(0)->health == 100 && !seat(0)->dead && airborne < 72);
        // It went up and down with it
        TIDE_CHECK(high - low > 1);
        if (m == 0) printf("    on a bridge's half a player rode between y = %.2f and %.2f, off it for %d ticks of 480\n", (double)low, (double)high, airborne);
    }
    TIDE_CHECK(halves == 6);
}

TIDE_TEST(cracked_a_jump_pad_throws_a_player_once_as_they_come_in)
{
    // Map9's pads give 180 N s, Map10's 200 and 250: 18, 20 and 25 m/s on 10 kg
    for (int map = 5; map <= 6; map++) {
        begin(2, map);
        int pads = 0;
        for (int i = 0; i < pieces(); i++) {
            if (!piece(i)->jumpPad) continue;
            pads++;
            const Piece pad = *piece(i);
            const float force = length(pad.force);
            TIDE_CHECK(map == 5 ? near(force, 180, 0.01f) : near(force, 200, 0.01f) || near(force, 250, 0.01f));
            // Dropped on it from just over it, still
            put(0, pad.position.x, pad.position.y + pad.half.y + 0.6f, pad.position.z);
            seat(0)->inPads = 0;
            int throws = 0;
            int entries = 0;
            for (int t = 0; t < 45; t++) {
                const int in = seat(0)->inPads;
                const tide_float3 before = seat(0)->velocity;
                const bool grounded = seat(0)->grounded;
                tick(1);
                if (!in && seat(0)->inPads) entries++;
                if (length(minus(seat(0)->velocity, before)) < 10) continue;
                throws++;
                // The tick after it came in: what it fell at is gone, and the pad's push is its speed, less the tick's gravity and damping
                TIDE_CHECK(in && entries == 1 && !grounded);
                const float keeps = seat(0)->grounded ? GROUND_KEEPS : AIR_KEEPS;
                const tide_float3 worked = {(before.x + pad.force.x / MASS) * keeps, (pad.force.y / MASS - GRAVITY * DT) * keeps, before.z * keeps};
                // (the Unity game adds a push of up to 0.1 m/s any way across, a random one)
                TIDE_CHECK(near(seat(0)->velocity.y, worked.y, 0.02f) && near(seat(0)->velocity.x, worked.x, 0.25f) && near(seat(0)->velocity.z, worked.z, 0.25f));
                if (pads == 1) printf("    Map%d's first pad threw a player up at %.2f m/s\n", map == 5 ? 9 : 10, (double)seat(0)->velocity.y);
            }
            TIDE_CHECK(throws == 1 && entries == 1);
        }
        TIDE_CHECK(pads == (map == 5 ? 7 : 6));
    }
}

TIDE_TEST(cracked_a_jump_pad_throws_again_when_a_player_comes_back)
{
    begin(2, 5);
    int found = -1;
    for (int i = 0; i < pieces() && found < 0; i++)
        if (piece(i)->jumpPad && piece(i)->force.x == 0) found = i;
    TIDE_REQUIRE(found >= 0);
    const Piece pad = *piece(found);
    put(0, pad.position.x, pad.position.y + pad.half.y + 0.6f, pad.position.z);
    seat(0)->inPads = 0;
    int throws = 0;
    float top = -1000;
    bool came = false;
    for (int t = 0; t < 400; t++) {
        const int in = seat(0)->inPads;
        tick(1);
        // The tick after it comes in, it's on its way up at the pad's speed, however fast it fell in
        if (came) {
            throws++;
            TIDE_CHECK(near(seat(0)->velocity.y, (18 - GRAVITY * DT) * AIR_KEEPS, 0.02f));
        }
        came = !in && seat(0)->inPads;
        if (seat(0)->position.y > top) top = seat(0)->position.y;
    }
    printf("    left on a pad, a player was thrown %d times in 400 ticks, %.1f m over it at the most\n", throws, (double)(top - pad.position.y - pad.half.y));
    TIDE_CHECK(throws == 5 && seat(0)->health == 100);
    // 17.4 m/s goes about 10 m up through the air's damping
    TIDE_CHECK(top - pad.position.y - pad.half.y > 9.5f && top - pad.position.y - pad.half.y < 11);
}

// ---------------------------------------------------------------------------
// Cards that change how a player moves and shoots

TIDE_TEST(cracked_shatterburst_fires_six_bullets_a_shot)
{
    lab_with(1, (const int[][2]){{0, SHATTERBURST}});
    tick(5);
    // Half the magazine, rounded up
    TIDE_CHECK(seat(0)->settings.magazineSize == 3 && seat(0)->magazine == 3 && near(seat(0)->settings.reloadTime, 3.25f, 0.0001f));
    hold(0, firing(0, 0.6f, 0.8f));
    tick(1);
    hold(0, idle());
    // The shot itself, and five more within 10 degrees of it each way: one bullet of the magazine
    TIDE_REQUIRE(bullets() == 6 && arena()->nextBullet == 6 && seat(0)->magazine == 2);
    int straight = 0;
    for (int i = 0; i < 6; i++) {
        const Bullet *it = bullet(i);
        TIDE_CHECK(near(it->props.damage, 9, 0.0001f) && it->original && it->id == i);
        const float along = ((it->velocity.y + 10 * DT) * 0.6f + it->velocity.z * 0.8f) / 30;
        // cos(14.2 degrees): 10 across and 10 up or down
        TIDE_CHECK(along > 0.969f && along < 1.00001f);
        if (along > 0.999999f) straight++;
        for (int j = 0; j < i; j++) TIDE_CHECK(length(minus(it->velocity, bullet(j)->velocity)) > 0.01f);
    }
    TIDE_CHECK(straight == 1);
}

TIDE_TEST(cracked_jetburst_flies_for_two_seconds_of_fuel)
{
    lab_with(1, (const int[][2]){{0, JETBURST}});
    TIDE_CHECK(seat(0)->settings.hasJetpack && near(seat(0)->settings.jetpackCapacity, 2, 0) && !seat(1)->settings.hasJetpack);
    // On the ground it fills: a second a second
    seat(0)->jetpackFuel = 0;
    tick(30);
    TIDE_CHECK(near(seat(0)->jetpackFuel, 1, 0.001f));
    tick(60);
    TIDE_CHECK(near(seat(0)->jetpackFuel, 2, 0));
    // Jump held: the jump, then a fifth of a second on the jet's push, 5.5 N s a tick while it rises under 8.5 m/s
    hold(0, (Controls){.jump = true, .aim = {0, 0, 1}});
    float top = 0;
    float fastest = 0;
    int burned = 0;
    for (int t = 1; t <= 120; t++) {
        const float fuel = seat(0)->jetpackFuel;
        const float before = seat(0)->velocity.y;
        tick(1);
        if (seat(0)->jetpackFuel < fuel) {
            burned++;
            TIDE_CHECK(t > 6 && near(fuel - seat(0)->jetpackFuel, DT, 0.0001f) || seat(0)->jetpackFuel == 0);
            if (before < 8.5f) TIDE_CHECK(near(seat(0)->velocity.y, (before + 5.5f / MASS - GRAVITY * DT) * AIR_KEEPS, 0.001f));
            else TIDE_CHECK(near(seat(0)->velocity.y, (before - GRAVITY * DT) * AIR_KEEPS, 0.001f));
        }
        if (seat(0)->position.y > top) top = seat(0)->position.y;
        if (seat(0)->velocity.y > fastest) fastest = seat(0)->velocity.y;
    }
    printf("    a jet burned for %d ticks and took its player %.1f m up, at %.2f m/s at the most\n", burned, (double)top, (double)fastest);
    TIDE_CHECK(burned == 60 || burned == 61);
    TIDE_CHECK(seat(0)->jetpackFuel == 0 && top > 8 && fastest < 8.5f + 5.5f / MASS && fastest > 8);
    TIDE_CHECK(top > 5 * jump_height(6));
}

TIDE_TEST(cracked_straight_shot_has_no_gravity_and_half_the_speed_again)
{
    lab_with(1, (const int[][2]){{0, STRAIGHT_SHOT}});
    tick(5);
    hold(0, firing(0, 0.6f, 0.8f));
    tick(1);
    hold(0, idle());
    tick(20);
    TIDE_REQUIRE(bullets() == 1);
    TIDE_CHECK(near(bullet(0)->velocity.y, 45 * 0.6f, 0.001f) && near(bullet(0)->velocity.z, 45 * 0.8f, 0.001f) && bullet(0)->props.gravity == 0);
}

TIDE_TEST(cracked_spell_bloom_grows_a_bullet_and_its_damage_as_it_flies)
{
    lab_with(1, (const int[][2]){{0, SPELL_BLOOM}});
    tick(5);
    hold(0, firing(0, 0.6f, 0.8f));
    tick(1);
    hold(0, idle());
    tick(29);
    TIDE_REQUIRE(bullets() == 1);
    // A second of flight: 5 more damage, and half a meter more of it
    TIDE_CHECK(near(bullet(0)->props.damage, 20, 0.001f) && near(bullet(0)->props.size, 0.7f, 0.001f));
    TIDE_CHECK(near(bullet(0)->lifetime, 1, 0.001f));
}

TIDE_TEST(cracked_blast_rune_breaks_a_bullet_into_six_where_it_hits)
{
    lab_with(1, (const int[][2]){{0, BLAST_RUNE}});
    wall(0, 5, 6, 10, 5, 0.5f);
    tick(5);
    hold(0, firing(0, 0, 1));
    tick(1);
    hold(0, idle());
    // Seven tenths of the speed
    TIDE_REQUIRE(bullets() == 1 && near(bullet(0)->velocity.z, 21, 0.001f));
    int t = 0;
    while (bullets() == 1 && t < 20) {
        tick(1);
        t++;
    }
    // The bullet is gone, and six pellets leave the wall: small, slow, no bullet of the magazine
    TIDE_REQUIRE(bullets() == 6 && arena()->nextBullet == 7 && seat(0)->magazine == 4);
    for (int i = 0; i < 6; i++) {
        const Bullet *it = bullet(i);
        TIDE_CHECK(!it->original && it->seat == 0 && near(it->props.size, 0.1f, 0) && near(it->props.damage, 17, 0));
        TIDE_CHECK(it->props.maxBounces == 0 && near(length(it->velocity), 5, 0.01f));
        // Away from the wall they came off
        TIDE_CHECK(it->velocity.z <= 0.01f && it->position.z < 5.5f && it->position.z > 4.5f);
    }
    // A pellet's own hit makes no more
    tick(90);
    TIDE_CHECK(bullets() == 0 && arena()->nextBullet == 7);
}

TIDE_TEST(cracked_ghost_round_goes_through_one_wall_slowly)
{
    lab_with(1, (const int[][2]){{0, GHOST_ROUND}});
    // A wall a meter thick from z = 3 to 4, and another behind it
    wall(0, 5, 3.5f, 10, 5, 0.5f);
    wall(0, 5, 9.5f, 10, 5, 0.5f);
    tick(5);
    hold(0, firing(0, 0, 1));
    tick(1);
    hold(0, idle());
    TIDE_REQUIRE(bullets() == 1 && bullet(0)->props.maxWallPenetrations == 1 && near(bullet(0)->props.damage, 18.75f, 0.0001f));
    int inside_for = 0;
    bool through = false;
    for (int t = 0; t < 40 && bullets() == 1; t++) {
        const float was = bullet(0)->position.z;
        const bool slow = bullet(0)->insideWall;
        tick(1);
        if (bullets() != 1) break;
        TIDE_CHECK(bullet(0)->velocity.z > 29.9f && bullet(0)->bounces == 0);
        if (slow && bullet(0)->insideWall) {
            // A fifth of its speed while it's in the wall
            inside_for++;
            TIDE_CHECK(near(bullet(0)->position.z - was, 30 * DT * 0.2f, 0.001f));
        }
        if (bullet(0)->position.z > 4.3f && !bullet(0)->insideWall) through = true;
    }
    printf("    a ghost round was in a meter of wall for %d ticks\n", inside_for);
    TIDE_CHECK(through && inside_for >= 4 && inside_for <= 8);
    // The second wall is its end, as any bullet's first
    TIDE_CHECK(bullets() == 0);
}

TIDE_TEST(cracked_seeker_spark_turns_a_bullet_to_an_enemy)
{
    for (int seeking = 0; seeking < 2; seeking++) {
        lab_with(seeking, (const int[][2]){{0, SEEKER_SPARK}});
        stand(1, 2.1f, 14);
        tick(5);
        hold(0, firing(0, 0.05f, 1));
        tick(1);
        hold(0, idle());
        TIDE_REQUIRE(bullets() == 1);
        float most = 0;
        for (int t = 0; t < 40 && bullets() == 1; t++) {
            // For its first fifth of a second it goes as it was shot
            if (bullet(0)->lifetime < 0.2f) TIDE_CHECK(bullet(0)->velocity.x == 0);
            if (bullet(0)->velocity.x > most) most = bullet(0)->velocity.x;
            // It turns, and keeps its speed (gravity's part of it too)
            tick(1);
        }
        if (seeking) {
            // Three quarters of the damage: 11.25, which the Unity game takes as -11.25 rounded up, 11
            TIDE_CHECK(most > 3 && seat(1)->health == 89);
        } else {
            TIDE_CHECK(most == 0 && seat(1)->health == 100);
        }
    }
}

TIDE_TEST(cracked_power_kick_knocks_back_three_times_as_hard)
{
    lab_with(1, (const int[][2]){{0, POWER_KICK}});
    face_off(6);
    tick(5);
    shoot_once();
    until_gone();
    // Four fifths of the damage
    TIDE_CHECK(seat(1)->health == 88);
    const float along = seat(1)->velocity.x * OUT_X + seat(1)->velocity.z * OUT_Z;
    TIDE_CHECK(near(along, (300.15f / tide_sqrt_f(1.01f)) / MASS * GROUND_KEEPS, 0.1f));
    // ...which the cap on its speed over the ground has the tick after
    tick(1);
    TIDE_CHECK(flat(seat(1)->velocity) < 8.5f);
}

TIDE_TEST(cracked_reflex_reload_fills_the_magazine_at_a_hit)
{
    lab_with(1, (const int[][2]){{0, REFLEX_RELOAD}});
    face_off(6);
    tick(5);
    shoot_once();
    hold(0, (Controls){.reload = true, .aim = {OUT_X, 0, OUT_Z}});
    tick(1);
    hold(0, (Controls){.aim = {OUT_X, 0, OUT_Z}});
    TIDE_REQUIRE(seat(0)->magazine == 4 && seat(0)->reloading && bullets() == 1);
    until_gone();
    TIDE_CHECK(seat(1)->health == 85 && seat(0)->magazine == 5 && !seat(0)->reloading);

    // A miss does nothing of the kind
    tick(10);
    shoot_once();
    stand(1, 0, 30);
    tick(40);
    TIDE_CHECK(seat(0)->magazine == 4);
}

TIDE_TEST(cracked_vital_leech_heals_half_of_what_a_hit_takes)
{
    lab_with(1, (const int[][2]){{0, VITAL_LEECH}});
    TIDE_CHECK(seat(0)->settings.fireRate == 4 && near(seat(0)->settings.reloadTime, 2.4f, 0.0001f));
    face_off(6);
    seat(0)->health = 50;
    tick(5);
    shoot_once();
    until_gone();
    // Half of 15, rounded up
    TIDE_CHECK(seat(1)->health == 85 && seat(0)->health == 58);
    // Nothing from a hit a shield stops. That bullet comes back, and its hit on who shot it heals them
    // as any hit of theirs does, before it hurts: 58 + 8 - 15
    tick(10);
    hold(1, (Controls){.shield = true, .aim = {0, 0, 1}});
    shoot_once();
    hold(1, idle());
    until_gone();
    TIDE_CHECK(seat(1)->health == 85 && seat(0)->health == 51);
    // ...and never over its health's most
    seat(0)->health = 97;
    tick(60);
    shoot_once();
    until_gone();
    TIDE_CHECK(seat(0)->health == 100);
}

TIDE_TEST(cracked_stagger_shot_stops_who_it_hits_for_a_moment)
{
    lab_with(1, (const int[][2]){{0, STAGGER_SHOT}});
    TIDE_CHECK(near(seat(0)->settings.reloadTime, 2.4f, 0.0001f));
    face_off(6);
    tick(5);
    hold(1, (Controls){.move = {0, 1}, .aim = {0, 0, 1}});
    shoot_once();
    until_gone();
    TIDE_REQUIRE(seat(1)->health == 85 && traits(1, TraitKind_Daze) == 1);
    // 0.45 s: each tick what speed it has is taken, so it goes no faster than one tick's push
    int dazed = 0;
    while (traits(1, TraitKind_Daze) == 1 && dazed < 40) {
        tick(1);
        dazed++;
        if (traits(1, TraitKind_Daze) == 1) TIDE_CHECK(flat(seat(1)->velocity) < 200.0f / MASS * DT * GROUND_KEEPS + 0.001f);
    }
    TIDE_CHECK(dazed == 14 || dazed == 15);
    tick(60);
    TIDE_CHECK(near(seat(1)->velocity.z, 10.0f / 3.0f, 0.01f));
}

TIDE_TEST(cracked_lingering_pain_takes_a_hit_ten_a_second)
{
    lab_with(1, (const int[][2]){{1, LINGERING_PAIN}});
    face_off(6);
    tick(5);
    shoot_once();
    until_gone();
    // Nothing yet: the hit is owed
    TIDE_CHECK(seat(1)->health == 100 && near(seat(1)->timeSinceWasShot, 0, 0.0001f));
    int last = 100;
    for (int t = 1; t <= 45; t++) {
        tick(1);
        // One every third tick
        TIDE_CHECK(seat(1)->health == last || seat(1)->health == last - 1);
        TIDE_CHECK(seat(1)->health >= 100 - (t + 2) / 3 && seat(1)->health <= 100 - (t - 2) / 3);
        last = seat(1)->health;
    }
    TIDE_CHECK(seat(1)->health == 85);
    tick(30);
    TIDE_CHECK(seat(1)->health == 85);
}

TIDE_TEST(cracked_static_surge_shocks_who_is_near_every_second)
{
    lab_with(1, (const int[][2]){{0, STATIC_SURGE}});
    TIDE_CHECK(near(seat(0)->settings.speed, 340, 0.001f));
    // 4 m from its feet to the other's body, which is half a meter thick: 4.4 m apart is in reach, 4.6 isn't
    stand(1, 0, 4.4f);
    int shocks[6];
    int shocked = 0;
    int health = seat(1)->health;
    for (int t = 0; t < 180 && shocked < 6; t++) {
        tick(1);
        if (seat(1)->health == health) continue;
        TIDE_CHECK(seat(1)->health == health - 10);
        health = seat(1)->health;
        shocks[shocked++] = t;
    }
    TIDE_REQUIRE(shocked >= 5);
    // A second (30 ticks of a count in floats, which is over 1 by then), and the tick that finds it over
    for (int i = 1; i < shocked; i++) TIDE_CHECK(shocks[i] - shocks[i - 1] == 31);
    TIDE_CHECK(seat(0)->health == 100);
    stand(1, 0, 4.6f);
    health = seat(1)->health;
    tick(70);
    TIDE_CHECK(seat(1)->health == health);
    // A shield stops a shock
    stand(1, 0, 2);
    hold(1, (Controls){.shield = true, .aim = {0, 0, 1}});
    tick(17);
    hold(1, idle());
    TIDE_CHECK(seat(1)->health == health);
}

TIDE_TEST(cracked_frost_guard_slows_and_hurts_who_is_near_a_block)
{
    lab_with(1, (const int[][2]){{0, FROST_GUARD}});
    TIDE_CHECK(near(seat(0)->settings.speed, 340, 0.001f));
    stand(1, 0, 4.9f);
    hold(1, (Controls){.move = {1, 0}, .aim = {0, 0, 1}});
    tick(60);
    TIDE_REQUIRE(near(seat(1)->velocity.x, 10.0f / 3.0f, 0.01f));
    stand(1, 0, 4.9f);
    hold(0, (Controls){.shield = true, .aim = {0, 0, 1}});
    tick(1);
    hold(0, idle());
    // 4.5 m, to a body half a meter thick: 20 health, and half its speed for two seconds
    TIDE_CHECK(seat(1)->health == 80 && near(seat(1)->movementMultiplier, 0.5f, 0) && traits(1, TraitKind_Freeze) == 1);
    TIDE_CHECK(seat(0)->health == 100 && near(seat(0)->movementMultiplier, 1, 0));
    int slowed = 0;
    while (seat(1)->movementMultiplier < 1 && slowed < 100) {
        tick(1);
        slowed++;
    }
    TIDE_CHECK(slowed == 60 || slowed == 61);
    TIDE_CHECK(traits(1, TraitKind_Freeze) == 0 && near(seat(1)->movementMultiplier, 1, 0));
    // Out of reach, a block does nothing to them
    stand(1, 0, 30);
    tick(150);
    TIDE_REQUIRE(seat(0)->shieldCooldown == 0);
    seat(1)->health = 100;
    hold(0, (Controls){.shield = true, .aim = {0, 0, 1}});
    tick(1);
    hold(0, idle());
    TIDE_CHECK(seat(1)->health == 100 && traits(1, TraitKind_Freeze) == 0);
    // Slowed, a player has half the push: half the top speed
    tick(175);
    stand(1, 0, 3);
    hold(0, (Controls){.shield = true, .aim = {0, 0, 1}});
    tick(1);
    hold(0, idle());
    TIDE_REQUIRE(traits(1, TraitKind_Freeze) == 1);
    tick(45);
    TIDE_CHECK(near(seat(1)->velocity.x, 10.0f / 6.0f, 0.01f));
}

TIDE_TEST(cracked_dash_guard_dashes_the_way_the_camera_looks_and_blocks_again)
{
    lab_with(1, (const int[][2]){{0, DASH_GUARD}});
    tick(5);
    // Looking level along z
    hold(0, (Controls){.shield = true, .aim = {0, 0, 1}});
    tick(1);
    hold(0, (Controls){.aim = {0, 0, 1}});
    // 10 m/s more each tick for 0.2 s, with no cap on its speed
    TIDE_CHECK(seat(0)->shieldActive && seat(0)->uncapVelocity == 1);
    float fastest = 0;
    int dashed = 0;
    float before = seat(0)->velocity.z;
    while (seat(0)->uncapVelocity == 1 && dashed < 20) {
        tick(1);
        dashed++;
        // (its last tick's push is on a speed the cap has had again)
        if (seat(0)->uncapVelocity == 1) TIDE_CHECK(near(seat(0)->velocity.z, (before + 10) * GROUND_KEEPS, 0.01f));
        else TIDE_CHECK(near(seat(0)->velocity.z, (8.5f + 10) * GROUND_KEEPS, 0.01f));
        before = seat(0)->velocity.z;
        if (before > fastest) fastest = before;
    }
    printf("    a dash went on for %d ticks, to %.1f m/s\n", dashed, (double)fastest);
    TIDE_CHECK((dashed == 6 || dashed == 7) && fastest > 30);
    // As it ends the shield is up again, for all of its time
    TIDE_CHECK(seat(0)->shieldActive && near(seat(0)->shieldTimer, 0.6f - DT, 0.001f) && seat(0)->uncapVelocity == 0);
    int up = 0;
    while (seat(0)->shieldActive && up < 40) {
        tick(1);
        up++;
    }
    TIDE_CHECK(up == 17 || up == 18);
    // The cap is back
    TIDE_CHECK(flat(seat(0)->velocity) < 8.5f && seat(0)->position.z > 4);
    // ...and the second block was no dash
    tick(30);
    TIDE_CHECK(seat(0)->uncapVelocity == 0 && flat(seat(0)->velocity) < 0.1f);
}

TIDE_TEST(cracked_echo_guard_blocks_again_after_the_first_block)
{
    lab_with(1, (const int[][2]){{0, ECHO_GUARD}});
    tick(5);
    hold(0, (Controls){.shield = true, .aim = {0, 0, 1}});
    tick(1);
    hold(0, idle());
    int rises = 1;
    int down = 0;
    int second = 0;
    int second_down = 0;
    TIDE_REQUIRE(seat(0)->shieldActive);
    for (int t = 2; t <= 120; t++) {
        const bool was = seat(0)->shieldActive;
        tick(1);
        if (was && !seat(0)->shieldActive && !down) down = t;
        else if (was && !seat(0)->shieldActive) second_down = t;
        if (!was && seat(0)->shieldActive) {
            rises++;
            second = t;
        }
    }
    printf("    the first block ended at tick %d, the second came at %d and ended at %d\n", down, second, second_down);
    // 0.35 s after the first is over: the count waits while the shield is up. Once, not again after the second
    TIDE_CHECK(rises == 2 && (down == 18 || down == 19));
    TIDE_CHECK(second - down == 11 || second - down == 12);
    TIDE_CHECK(second_down - second == 17 || second_down - second == 18);
    // The five seconds are from the second
    TIDE_CHECK(seat(0)->shieldCooldown > 5 - (float)(120 - second_down + 1) * DT - 0.01f);
}

TIDE_TEST(cracked_vital_mist_heals_who_stands_in_it_unhurt)
{
    lab_with(1, (const int[][2]){{0, VITAL_MIST}});
    TIDE_CHECK(near(seat(0)->settings.reloadTime, 2.4f, 0.0001f));
    // The other stands in it too, hurt just now
    stand(1, 0, 2.5f);
    tick(5);
    seat(0)->health = 50;
    seat(1)->health = 50;
    seat(1)->timeSinceWasShot = 0;
    TIDE_REQUIRE(seat(0)->timeSinceWasShot > 2);
    hold(0, (Controls){.shield = true, .aim = {0, 0, 1}});
    tick(1);
    hold(0, idle());
    TIDE_REQUIRE(count_in(&world, world.Arena.clouds) == 1);
    const Cloud *cloud = tide_list_read(&world.heap, world.Arena.clouds, 0, sizeof(Cloud));
    TIDE_CHECK(cloud->kind == CloudKind_Heal && near(cloud->radius, 2.5f, 0) && near(cloud->perSecond, 10, 0));
    TIDE_CHECK(length(minus(cloud->position, seat(0)->position)) < 0.01f);
    // Ten a second for the three seconds it works, to whoever wasn't hurt in the last two
    tick(30);
    TIDE_CHECK(seat(0)->health == 60 && seat(1)->health == 50);
    tick(60);
    printf("    a mist healed its player %d, and one hurt as it started %d\n", seat(0)->health - 50, seat(1)->health - 50);
    TIDE_CHECK(seat(0)->health == 79 || seat(0)->health == 80);
    TIDE_CHECK(seat(1)->health >= 58 && seat(1)->health <= 60);
    const int healed = seat(0)->health;
    // It's gone after five
    tick(59);
    TIDE_CHECK(count_in(&world, world.Arena.clouds) == 1 && seat(0)->health == healed);
    tick(2);
    TIDE_CHECK(count_in(&world, world.Arena.clouds) == 0);
}

TIDE_TEST(cracked_poison_cloud_is_left_where_a_bullet_hits)
{
    lab_with(1, (const int[][2]){{0, POISON_CLOUD}});
    face_off(6);
    tick(5);
    shoot_once();
    until_gone();
    TIDE_REQUIRE(count_in(&world, world.Arena.clouds) == 1 && seat(1)->health == 85);
    const Cloud *cloud = tide_list_read(&world.heap, world.Arena.clouds, 0, sizeof(Cloud));
    TIDE_CHECK(cloud->kind == CloudKind_Poison && near(cloud->radius, 1.25f, 0) && near(cloud->perSecond, 2, 0));
    // Who stands in it is poisoned more each tick, and poison takes health every half second and a tick:
    // up to 3 at a time, as the poison is under 1 or over
    stand(1, cloud->position.x, cloud->position.z);
    int last = 85;
    int hurts = 0;
    int gap = 0;
    float most = 0;
    for (int t = 0; t < 300; t++) {
        tick(1);
        gap++;
        if (seat(1)->poisonLevel > most) most = seat(1)->poisonLevel;
        if (seat(1)->health == last) continue;
        TIDE_CHECK(last - seat(1)->health >= 1 && last - seat(1)->health <= 3);
        if (hurts > 0) TIDE_CHECK(gap == 16);
        hurts++;
        gap = 0;
        last = seat(1)->health;
    }
    printf("    a poison cloud took %d health in %d bites, its poison %.2f at the most\n", 85 - last, hurts, (double)most);
    // In it for its three seconds, the poison is 2 a second less the 1 a second it fades by: 3 at the most, six seconds to go
    TIDE_CHECK(most > 2.8f && most < 3.1f && seat(1)->poisonLevel == 0);
    TIDE_CHECK(hurts >= 10 && hurts <= 12 && 85 - last >= 25 && 85 - last <= 33);
    TIDE_CHECK(count_in(&world, world.Arena.clouds) == 0 && seat(0)->health == 100);
}

// ---------------------------------------------------------------------------
// Whatever players do

TIDE_TEST(cracked_input_that_isnt_a_number_is_repaired)
{
    lab();
    tick(5);
    const float nan = NAN;
    const float inf = INFINITY;
    const Controls bad[4] = {
        {.move = {nan, nan}, .jump = true, .fire = true, .shield = true, .aim = {nan, nan, nan}, .yaw = nan, .pitch = nan},
        {.move = {inf, -inf}, .fire = true, .aim = {inf, -inf, inf}, .yaw = inf, .pitch = -inf},
        {.move = {1e30f, -1e30f}, .fire = true, .aim = {1e30f, 1e30f, -1e30f}, .yaw = 1e30f, .pitch = 1e30f},
        {.move = {-3e38f, 3e38f}, .fire = true, .aim = {0, 0, 0}, .yaw = -3e38f, .pitch = -3e38f},
    };
    for (int round = 0; round < 4; round++) {
        stand(0, 0, 0);
        hold(0, bad[round]);
        for (int t = 0; t < 12; t++) {
            tick(1);
            const Seat *it = seat(0);
            // What the match read: a stick no further than all the way each way, a camera's pitch in its
            // range, and a way to aim that's a way
            TIDE_CHECK(fabsf(it->pad.move.x) <= 1 && fabsf(it->pad.move.y) <= 1);
            TIDE_CHECK(it->pad.pitch >= -10 && it->pad.pitch <= 45 && isfinite(it->pad.yaw));
            TIDE_CHECK(finite3(it->pad.aim) && near(length(it->aim), 1, 0.001f));
            TIDE_CHECK(finite3(it->position) && finite3(it->velocity) && finite3(it->spin) && isfinite(it->rotation.value.w));
            TIDE_CHECK(flat(it->velocity) < 9 && fabsf(it->position.x) < 5 && fabsf(it->position.z) < 5);
            for (int i = 0; i < bullets(); i++) {
                TIDE_CHECK(finite3(bullet(i)->position) && finite3(bullet(i)->velocity));
                TIDE_CHECK(near(length(bullet(i)->velocity), 30, 4));
            }
        }
        TIDE_CHECK(arena()->nextBullet >= round + 1);
    }
    // What isn't a number is the input's own default: no stick, aim along z
    stand(0, 0, 0);
    hold(0, bad[0]);
    tick(30);
    TIDE_CHECK(seat(0)->pad.move.x == 0 && seat(0)->pad.move.y == 0 && seat(0)->pad.yaw == 0);
    TIDE_CHECK(seat(0)->pad.aim.x == 0 && seat(0)->pad.aim.y == 0 && seat(0)->pad.aim.z == 1);
    TIDE_CHECK(flat(seat(0)->velocity) < 0.001f);
}

static uint32_t mixed(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

// What a player does at a tick of a match that's made up: a third of a second
// of one thing, then another. Every number is one of a few written here, none
// worked out by this machine's own sine or square root, so every machine
// plays the same match.
static Controls scripted(const uint32_t at, const int who)
{
    static const float ways[8][2] = {{0, 1}, {0.7071f, 0.7071f}, {1, 0}, {0.7071f, -0.7071f}, {0, -1}, {-0.7071f, -0.7071f}, {-1, 0},
                                     {-0.7071f, 0.7071f}};
    const uint32_t h = mixed(at / 10u * 16u + (uint32_t)who + 1u);
    const uint32_t way = h % 8u;
    const uint32_t look = (h >> 19) % 8u;
    Controls controls = {0};
    if ((h >> 3) % 4u) controls.move = tide_f2(ways[way][0], ways[way][1]);
    controls.jump = (h >> 5) % 5u == 0;
    controls.fire = (h >> 8) % 3u != 0;
    controls.shield = (h >> 10) % 16u == 0;
    controls.reload = (h >> 14) % 32u == 0;
    controls.aim = tide_f3(ways[look][0], ((float)((h >> 22) % 5u) - 1) * 0.15f, ways[look][1]);
    controls.yaw = (float)look * 45;
    controls.pitch = (float)((h >> 25) % 40u);
    return controls;
}

// A tick of that match in a world: every player's input, and a card for
// whoever has had a second to look at theirs.
static void play_tick(tide_world *w, const uint32_t at, const int players)
{
    for (int i = 0; i < players; i++) tide_world_set_input(w, player(i), scripted(at, i));
    const Arena *a = &w->Arena;
    if (a->phase == Phase_Upgrade && a->upgrading >= 0 && a->upgrading < players && a->selectionAge > 1) {
        uint8_t bytes[TIDE_COMMAND_BYTES];
        const uint32_t size = pick_bytes((int)(at % CARDS), a->revision, bytes);
        command(w, a->upgrading, bytes, size);
    }
    tide_world_tick(w);
}

TIDE_TEST(cracked_eight_players_doing_anything_for_a_minute_break_nothing)
{
    seated(8);
    for (int s = 0; s < SEATS; s++) TIDE_REQUIRE(seat(s)->taken && !seat(s)->bot);
    // Every card of the 38 is somebody's, six to a seat
    for (int s = 0; s < SEATS; s++)
        for (int i = 0; i < 6; i++) give(s, (s * 6 + i) % UPGRADES);
    int most_bullets = 0;
    int fired = 0;
    int deaths = 0;
    int maps = 0;
    int last_map = -1;
    float lowest = 0;
    float furthest = 0;
    for (uint32_t t = 0; t < 1800; t++) {
        play_tick(&world, t, 8);
        if (arena()->map != last_map) {
            last_map = arena()->map;
            maps++;
        }
        for (int s = 0; s < SEATS; s++) {
            const Seat *it = seat(s);
            if (!it->alive) continue;
            TIDE_CHECK(finite3(it->position) && finite3(it->velocity) && finite3(it->spin) && finite3(it->aim));
            TIDE_CHECK(isfinite(it->rotation.value.x) && isfinite(it->rotation.value.w));
            TIDE_CHECK(it->health >= 0 && it->health <= it->maxHealth && it->maxHealth >= 1);
            TIDE_CHECK(it->magazine >= 0 && it->magazine <= it->settings.magazineSize);
            TIDE_CHECK(isfinite(it->poisonLevel) && it->poisonLevel >= 0 && it->shieldCooldown >= 0);
            TIDE_CHECK(it->position.y > -200 && it->position.y < 200 && flat(it->position) < 500);
            if (it->position.y < lowest) lowest = it->position.y;
            if (flat(it->position) > furthest) furthest = flat(it->position);
            if (it->dead && it->ragdollTimer == 0 && it->health == 0) deaths++;
        }
        const int flying = count_in(&world, world.Arena.bullets);
        if (flying > most_bullets) most_bullets = flying;
        for (int i = 0; i < flying; i++) TIDE_CHECK(finite3(bullet(i)->position) && finite3(bullet(i)->velocity) && bullet(i)->props.size > 0);
        TIDE_CHECK(count_in(&world, world.Arena.clouds) < 200 && count_in(&world, world.Arena.traits) < 200);
    }
    fired = arena()->nextBullet;
    printf("    in a minute eight players fired %d bullets, %d in the air at the most; %d rounds and %d points on %d maps; the lowest"
           " anyone was is y = %.1f, the furthest out %.1f m\n",
           fired, most_bullets, arena()->rounds.at[0] + arena()->rounds.at[1], arena()->score.at[0] + arena()->score.at[1], maps,
           (double)lowest, (double)furthest);
    // Eight firing three a second, each bullet three seconds at the most
    TIDE_CHECK(fired > 200 && most_bullets > 8 && most_bullets <= 8 * 9 * 6);
    TIDE_CHECK(deaths > 0 && maps >= 1);
}

TIDE_TEST(cracked_a_player_and_a_bot_play_a_game_to_its_end)
{
    seated(1);
    uint32_t t = 0;
    int points = 0;
    int was = 0;
    while (arena()->phase != Phase_GameOver && t < 120000) {
        play_tick(&world, t, 1);
        t++;
        const int now = arena()->rounds.at[0] * 2 + arena()->rounds.at[1] * 2 + arena()->score.at[0] + arena()->score.at[1];
        if (now != was) points++;
        was = now;
    }
    printf("    a game was over after %u ticks (%.0f s): %d rounds to %d, %d cards taken, %d bullets fired\n", t, (double)t / RATE,
           arena()->rounds.at[0], arena()->rounds.at[1], count_in(&world, world.Arena.picks), arena()->nextBullet);
    TIDE_REQUIRE(arena()->phase == Phase_GameOver);
    const int winner = arena()->lastWinningTeam;
    TIDE_CHECK(winner >= 0 && arena()->rounds.at[winner] == 5 && arena()->rounds.at[1 - winner] < 5);
    // Whoever lost a round has a card for it
    TIDE_CHECK(count_in(&world, world.Arena.picks) == arena()->rounds.at[0] + arena()->rounds.at[1] - 1);
    TIDE_CHECK(points >= 10 && arena()->nextBullet > 0);
}

// ---------------------------------------------------------------------------
// The same match on every machine

// The made-up match of four players, each with five cards from its start,
// after 3,000 ticks. Native and the web's build (WASI) must agree on it, in
// every configuration. It only changes when the match itself does (a rule, a
// number, a map, the order of a tick): then the test prints the new one, to be
// pinned here once the other platforms print the same.
#define MATCH_TICKS 3000
#define MATCH_HASH UINT64_C(0x0a216d532fa6445c)

static tide_world other;

// That match in a world, as its first warmup starts.
static void start_match(tide_world *w)
{
    static const int cards[4][5] = {{SHATTERBURST, REBOUND_HEX, POISON_CLOUD, STATIC_SURGE, ARCANE_ORBS},
                                    {BLAST_RUNE, GHOST_ROUND, SEEKER_SPARK, FROST_GUARD, POWER_KICK},
                                    {ARCSTORM, DASH_GUARD, ECHO_GUARD, VITAL_MIST, JETBURST},
                                    {LINGERING_PAIN, STAGGER_SHOT, VITAL_LEECH, SPELL_BLOOM, SKY_BOUND}};
    tide_world_free(w);
    tide_world_init(w, DT);
    // The match's dice, set here. A singleton's dice are seeded from the match's seed and the singleton's
    // place among the program's, so left alone they'd roll otherwise for every singleton a file that
    // only draws adds before Arena: this match is the same whatever is drawn of it
    w->Arena.mapDice = tide_random_seeded(1);
    w->Arena.cardDice = tide_random_seeded(2);
    for (int i = 0; i < 4; i++) tide_world_player_joined(w, player(i));
    tide_world_tick(w);
    tide_world_tick(w);
    for (int s = 0; s < 4; s++)
        for (int i = 0; i < 5; i++) give_in(w, s, cards[s][i]);
}

TIDE_TEST(cracked_the_same_inputs_make_the_same_match)
{
    start_match(&world);
    start_match(&other);
    TIDE_REQUIRE(tide_world_hash(&world) == tide_world_hash(&other));
    for (uint32_t t = 0; t < MATCH_TICKS; t++) {
        play_tick(&world, t, 4);
        play_tick(&other, t, 4);
        if (t % 100 == 99) TIDE_REQUIRE(tide_world_hash(&world) == tide_world_hash(&other));
    }
    const uint64_t hash = tide_world_hash(&world);
    printf("    after 3,000 ticks: %d bullets fired, %d rounds and %d points, map %d; the world is 0x%016llx\n", arena()->nextBullet,
           arena()->rounds.at[0] + arena()->rounds.at[1], arena()->score.at[0] + arena()->score.at[1], arena()->map,
           (unsigned long long)hash);
    TIDE_CHECK(arena()->nextBullet > 300 && count_in(&world, world.Arena.picks) >= 20);
    TIDE_CHECK(hash == MATCH_HASH);
    tide_world_free(&other);
}

TIDE_TEST(cracked_a_snapshot_taken_in_a_round_plays_on_the_same)
{
    start_match(&world);
    for (uint32_t t = 0; t < 400; t++) play_tick(&world, t, 4);
    TIDE_REQUIRE(arena()->phase == Phase_Running && bullets() > 0);
    tide_world_free(&other);
    tide_world_copy(&other, &world);
    const uint64_t kept = tide_world_hash(&other);
    TIDE_CHECK(kept == tide_world_hash(&world));
    static uint64_t hashes[300];
    for (uint32_t t = 0; t < 300; t++) {
        play_tick(&world, 400 + t, 4);
        hashes[t] = tide_world_hash(&world);
    }
    // The snapshot is as it was, and from it the same 300 ticks are the same
    TIDE_CHECK(tide_world_hash(&other) == kept && hashes[299] != kept);
    tide_world_copy(&world, &other);
    TIDE_CHECK(tide_world_hash(&world) == kept);
    int same = 0;
    for (uint32_t t = 0; t < 300; t++) {
        play_tick(&world, 400 + t, 4);
        if (tide_world_hash(&world) == hashes[t]) same++;
    }
    TIDE_CHECK(same == 300);
    tide_world_free(&other);
}

TIDE_TEST(cracked_a_world_packed_for_a_joiner_plays_on_the_same)
{
    start_match(&world);
    for (uint32_t t = 0; t < 400; t++) play_tick(&world, t, 4);
    TIDE_REQUIRE(arena()->phase == Phase_Running && bullets() > 0);
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(bytes != NULL && tide_world_pack(&world, bytes, size) == size);
    tide_world_free(&other);
    TIDE_REQUIRE(tide_world_unpack(&other, bytes, size));
    free(bytes);
    printf("    a world in a round packs to %u bytes\n", size);
    TIDE_CHECK(tide_world_hash(&other) == tide_world_hash(&world));
    TIDE_CHECK(count_in(&other, other.Arena.pieces) == pieces() && count_in(&other, other.Arena.bullets) == bullets());
    int same = 0;
    for (uint32_t t = 0; t < 300; t++) {
        play_tick(&world, 400 + t, 4);
        play_tick(&other, 400 + t, 4);
        if (tide_world_hash(&world) == tide_world_hash(&other)) same++;
    }
    TIDE_CHECK(same == 300);
    tide_world_free(&other);
}

// ---------------------------------------------------------------------------
// Two players and a server, over a network that's late and loses packets

#define PLAYERS 2
#define NET_TICKS 1500

typedef struct match {
    tide_loopback *net;
    tide_server *server;
    tide_client *players[PLAYERS];
    double now;
} match;

static void play(void *user, const uint32_t at, void *input)
{
    const int *who = user;
    const Controls controls = scripted(at, *who);
    memcpy(input, &controls, sizeof controls);
}

static void advance(match *m)
{
    m->now += 1.0 / RATE;
    tide_loopback_set_time(m->net, m->now);
    for (int i = 0; i < PLAYERS; i++)
        if (m->players[i]) tide_client_update(m->players[i], m->now);
    tide_server_update(m->server, m->now);
}

TIDE_TEST(cracked_two_players_stay_in_one_match_over_a_bad_network)
{
    static match m;
    static int who[PLAYERS] = {0, 1};
    static uint64_t hashes[NET_TICKS + 1];
    memset(hashes, 0, sizeof hashes);
    m = (match){.net = tide_loopback_create(7341)};
    tide_loopback_set_conditions(m.net, (tide_net_conditions){.latency = 0.04, .jitter = 0.015, .loss = 0.05});
    m.server = tide_server_create(&(tide_server_desc){.game = &tide_game_api, .tick_rate = RATE,
                                    .transports = {tide_loopback_endpoint(m.net, 1)}}, m.now);
    for (int i = 0; i < PLAYERS; i++)
        m.players[i] = tide_client_create(&(tide_client_desc){.game = &tide_game_api,
                                            .transport = tide_loopback_endpoint(m.net, (uint32_t)i + 2),
                                            .server = tide_loopback_address(1), .lead = 2,
                                            .sample = play, .user = &who[i]}, m.now);
    int compared = 0;
    int differed = 0;
    while (tide_server_tick(m.server) < NET_TICKS) {
        advance(&m);
        const tide_world *authority = tide_server_world(m.server);
        if (!authority) continue;
        // The server's world after each of its ticks, by the tick
        const int at = authority->Time.tick;
        if (at >= 0 && at <= NET_TICKS) hashes[at] = tide_world_hash(authority);
        // ...which is what each player's confirmed world is, when it has that tick
        for (int i = 0; i < PLAYERS; i++) {
            const tide_world *confirmed = tide_client_verified_world(m.players[i]);
            if (!confirmed || confirmed->Time.tick < 0 || confirmed->Time.tick > NET_TICKS || !hashes[confirmed->Time.tick]) continue;
            compared++;
            if (tide_world_hash(confirmed) != hashes[confirmed->Time.tick]) differed++;
        }
    }
    const tide_world *authority = tide_server_world(m.server);
    TIDE_REQUIRE(authority != NULL);
    // Both are in it, each on their own team, and they've fought
    const Arena *a = &authority->Arena;
    TIDE_CHECK(a->seats.at[0].taken && !a->seats.at[0].bot && a->seats.at[1].taken && !a->seats.at[1].bot);
    TIDE_CHECK(a->hasMap && a->nextBullet > 30);
    printf("    at tick %u: %d bullets fired, %d rounds and %d points; %d confirmed worlds compared with the server's\n",
           tide_server_tick(m.server), a->nextBullet, a->rounds.at[0] + a->rounds.at[1], a->score.at[0] + a->score.at[1], compared);
    TIDE_CHECK(compared > NET_TICKS && differed == 0);
    for (int i = 0; i < PLAYERS; i++) {
        const tide_client_status status = tide_client_status_of(m.players[i]);
        printf("    player %d ran %u predicted ticks again, was sent the world again %u times, and is %u ticks ahead of what's"
               " confirmed\n",
               i + 1, status.again_ticks, status.resyncs, status.predicted_tick - status.verified_tick);
        // No machine's world ever went another way than the server's: what each guessed wrong of the
        // other's input it ran again
        TIDE_CHECK(status.state == TIDE_SESSION_CONNECTED && status.resyncs == 0 && status.again_ticks > 0);
        TIDE_CHECK(status.verified_tick > NET_TICKS - 30 && status.predicted_tick - status.verified_tick <= 15);
    }
    printf("    the server's world at tick %u: 0x%016llx\n", tide_server_tick(m.server), (unsigned long long)tide_world_hash(authority));
    for (int i = PLAYERS - 1; i >= 0; i--) tide_client_destroy(m.players[i]);
    tide_server_destroy(m.server);
    tide_loopback_destroy(m.net);
}
