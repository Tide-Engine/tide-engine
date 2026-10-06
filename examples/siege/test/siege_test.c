// Siege with no window: what the game promises, on one world; what a machine
// shows of it and where its lobby leaves it, a frame at a time; and three
// players in a match on the loopback network, one of whom joins after shots
// were fired, whose server's world is pinned.

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
#define PIECES 62 // Two towers', their kings included

// The server's world in the match below, on every platform and in every
// configuration: Box2D's math, its image and the netcode, byte for byte.
// Once with both towers shelled, and once after a Reset built them again.
#define SHELLED_HASH UINT64_C(0x383f13e51c3a9ffd)
#define MATCH_HASH UINT64_C(0xe58e44fe8b02e68f)

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

// The one entity with this component
static tide_entity the(const tide_world *w, has_fn *has)
{
    for (uint32_t i = 0; i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        if (has(w, e)) return e;
    }
    return (tide_entity){0};
}

// A seat's cannon: the match keeps them in a list, by seat.
static tide_entity cannon_at(const tide_world *w, const int seat)
{
    return *(const tide_entity *)tide_list_read(&w->heap, w->Siege.cannons, seat, sizeof(tide_entity));
}

static Cannon cannon(const tide_world *w, const int seat)
{
    return tide_get_Cannon(w, cannon_at(w, seat));
}

// How many crates a seat's shots have painted.
static int painted(const tide_world *w, const int seat)
{
    int found = 0;
    for (uint32_t i = 0; i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        if (tide_has_Crate(w, e) && tide_get_Crate(w, e).last == seat) found++;
    }
    return found;
}

// How far the crates of a tower are from where a round puts them, the furthest
// of them: 0 for a tower nothing touched. `side` is -1 for the left one.
static float disturbed(const tide_world *w, const tide_world *built, const int side)
{
    float most = 0;
    for (uint32_t i = 0; i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        if (!tide_has_Crate(w, e) || !tide_has_Crate(built, e)) continue;
        const tide_float2 then = tide_get_Box2D_Body(built, e).position;
        if ((then.x < 0) != (side < 0)) continue;
        const tide_float2 now = tide_get_Box2D_Body(w, e).position;
        const float moved = tide_abs_f(now.x - then.x) + tide_abs_f(now.y - then.y);
        if (moved > most) most = moved;
    }
    return most;
}

// A match of two players, with its first round built and at rest. The
// cannons nobody has stay still, unless a test says.
static void start(const bool bots)
{
    b2t_forget();
    tide_world_free(&world);
    tide_world_init(&world, DT);
    world.Siege.bots = bots;
    tide_world_player_joined(&world, player(0));
    tide_world_player_joined(&world, player(1));
    for (int i = 0; i < 60; i++) tide_world_tick(&world);
}

static void hold(const int who, const float x, const float y, const bool fire)
{
    tide_world_set_input(&world, player(who), (Controls){.target = {x, y}, .aiming = true, .fire = fire});
}

static void tick(const int ticks)
{
    for (int i = 0; i < ticks; i++) tide_world_tick(&world);
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

TIDE_TEST(siege_builds_two_towers_and_seats_its_players)
{
    start(false);
    TIDE_CHECK(world.Siege.phase == Phase_Play && world.Siege.round == 1);
    TIDE_CHECK(count(&world, tide_has_Crate) == PIECES && world.Siege.standing == PIECES);
    TIDE_CHECK(count(&world, tide_has_Cannon) == 4 && count(&world, tide_has_Bumper) == 3);
    TIDE_CHECK(tide_list_read_count(&world.heap, world.Siege.cannons) == 4);
    for (int seat = 0; seat < 4; seat++) TIDE_CHECK(cannon(&world, seat).seat == seat);
    // The first two seats are the players', and the other two nobody's
    TIDE_CHECK(tide_get_Owner(&world, cannon_at(&world, 0)).player.id == player(0).id);
    TIDE_CHECK(tide_get_Owner(&world, cannon_at(&world, 1)).player.id == player(1).id);
    TIDE_CHECK(!tide_has_Owner(&world, cannon_at(&world, 2)) && !tide_has_Owner(&world, cannon_at(&world, 3)));
    // A player who leaves gives their cannon up, and the next to join takes it
    tide_world_player_left(&world, player(0));
    tick(1);
    TIDE_CHECK(!tide_has_Owner(&world, cannon_at(&world, 0)));
    tide_world_player_joined(&world, player(2));
    tide_world_player_joined(&world, player(3));
    tick(1);
    TIDE_CHECK(tide_get_Owner(&world, cannon_at(&world, 0)).player.id == player(2).id);
    TIDE_CHECK(tide_get_Owner(&world, cannon_at(&world, 2)).player.id == player(3).id);
    // Nothing was fired, and the towers stand
    tick(120);
    TIDE_CHECK(count(&world, tide_has_Ball) == 0 && world.Siege.standing == PIECES);
    tide_world_free(&world);
}

TIDE_TEST(siege_a_shot_knocks_crates_and_paints_them)
{
    start(false);
    tick(60);
    tide_world built = {0};
    tide_world_copy(&built, &world);
    // The first seat's cannon at the middle of the right tower, for a second and a half
    hold(0, 6, 3.5f, true);
    bool landed = false;
    for (int i = 0; i < 90; i++) {
        tide_world_tick(&world);
        if (count(&world, tide_has_Blast) > 0) landed = true;
    }
    hold(0, 6, 3.5f, false);
    TIDE_CHECK(landed);
    TIDE_CHECK(count(&world, tide_has_Ball) >= 5);
    TIDE_CHECK(cannon(&world, 0).aim.x > 0.9f);
    printf("    after 90 ticks of fire, a crate of the right tower is %.2f m from where it stood, and of the left %.2f;"
           " %d crates are the first seat's color\n",
           (double)disturbed(&world, &built, 1), (double)disturbed(&world, &built, -1), painted(&world, 0));
    TIDE_CHECK(disturbed(&world, &built, 1) > 1.0f);
    TIDE_CHECK(disturbed(&world, &built, -1) == 0.0f);
    TIDE_CHECK(painted(&world, 0) >= 5 && painted(&world, 1) == 0 && painted(&world, -1) < PIECES);
    // The other player did nothing, and their cannon fired nothing
    for (uint32_t i = 0; i < world.entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&world.entities, i);
        if (tide_has_Ball(&world, e)) TIDE_CHECK(tide_get_Ball(&world, e).seat == 0);
    }
    // Shots go after five seconds
    tick(320);
    TIDE_CHECK(count(&world, tide_has_Ball) == 0 && count(&world, tide_has_Blast) == 0);
    tide_world_free(&built);
    tide_world_free(&world);
}

TIDE_TEST(siege_a_crate_off_the_map_scores_for_who_painted_it)
{
    start(false);
    // A crate the second seat painted, a king the third did and a crate nobody
    // did, put over the pit: writing a body's position moves it
    int moved = 0;
    for (uint32_t i = 0; i < world.entities.next_unused && moved < 3; i++) {
        const tide_entity e = tide_entity_in_slot(&world.entities, i);
        if (!tide_has_Crate(&world, e)) continue;
        Crate crate = tide_get_Crate(&world, e);
        if ((crate.worth == 5) != (moved == 1)) continue;
        crate.last = moved == 0 ? 1 : moved == 1 ? 2 : -1;
        tide_set_Crate(&world, e, crate);
        Box2D_Body body = tide_get_Box2D_Body(&world, e);
        body.position = tide_f2((float)moved - 1.0f, -2.0f);
        tide_set_Box2D_Body(&world, e, body);
        moved++;
    }
    TIDE_REQUIRE(moved == 3);
    tick(120);
    TIDE_CHECK(cannon(&world, 1).score == 1 && cannon(&world, 2).score == 5);
    TIDE_CHECK(cannon(&world, 0).score == 0 && cannon(&world, 3).score == 0);
    TIDE_CHECK(cannon(&world, 2).round == 5);
    TIDE_CHECK(world.Siege.standing == PIECES - 3 && count(&world, tide_has_Crate) == PIECES - 3);
    tide_world_free(&world);
}

// The bridge is in the way of a shot from the first seat's cannon to the orb
// while it's at the left end of its run: ticks until it's on its way right.
static void clear_the_way(void)
{
    const tide_entity bridge = the(&world, tide_has_Bridge);
    for (int i = 0; i < 400; i++) {
        const Box2D_Body body = tide_get_Box2D_Body(&world, bridge);
        if (body.position.x > 0.0f && body.velocity.x > 0.0f) return;
        tide_world_tick(&world);
    }
}

TIDE_TEST(siege_a_shot_through_the_orb_turns_gravity_over_and_back)
{
    start(false);
    const tide_entity orb = the(&world, tide_has_Orb);
    TIDE_CHECK(world.Box2D_Physics.gravity.y == -10.0f);
    // A shot up at the orb that no player fired: the sensor hears it come in
    // and leave, and nothing turns
    clear_the_way();
    hold(0, 0, 9.6f, true);
    tick(2);
    hold(0, 0, 9.6f, false);
    const tide_entity stray = the(&world, tide_has_Ball);
    TIDE_REQUIRE(tide_has_Ball(&world, stray));
    Ball unmanned = tide_get_Ball(&world, stray);
    unmanned.manned = false;
    tide_set_Ball(&world, stray, unmanned);
    int most = 0;
    bool left_it = false;
    for (int i = 0; i < 40; i++) {
        tide_world_tick(&world);
        const int inside = tide_get_Orb(&world, orb).inside;
        if (inside > most) most = inside;
        if (most == 1 && inside == 0) left_it = true;
    }
    TIDE_CHECK(most == 1 && left_it);
    tick(320); // It's gone
    TIDE_CHECK(world.Siege.flip == 0 && world.Box2D_Physics.gravity.y == -10.0f);
    TIDE_CHECK(tide_get_Orb(&world, orb).inside == 0);

    // A player's shot
    clear_the_way();
    hold(0, 0, 9.6f, true);
    tick(2);
    hold(0, 0, 9.6f, false);
    int flipped_at = -1;
    for (int i = 0; i < 90; i++) {
        tide_world_tick(&world);
        if (flipped_at < 0 && world.Siege.flip > 0) flipped_at = i;
    }
    TIDE_REQUIRE(flipped_at >= 0);
    TIDE_CHECK(world.Box2D_Physics.gravity.y == 7.0f);
    // Everything that was at rest is awake, and on its way up
    float highest = 0;
    for (uint32_t i = 0; i < world.entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&world.entities, i);
        if (tide_has_Crate(&world, e) && tide_get_Box2D_Body(&world, e).position.y > highest) {
            highest = tide_get_Box2D_Body(&world, e).position.y;
        }
    }
    printf("    gravity turned over %d ticks after the shot; the highest crate is at %.1f m\n", flipped_at + 2, (double)highest);
    TIDE_CHECK(highest > 11.0f);
    // A second shot through it while it rests does nothing more
    const int left = world.Siege.flip;
    hold(0, 0, 9.6f, true);
    tick(2);
    hold(0, 0, 9.6f, false);
    tick(40);
    TIDE_CHECK(world.Siege.flip == left - 42);
    tick(left);
    TIDE_CHECK(world.Siege.flip == 0 && world.Box2D_Physics.gravity.y == -10.0f);
    tide_world_free(&world);
}

TIDE_TEST(siege_a_shot_wraps_around_the_map_once)
{
    start(false);
    hold(0, 6, 3.5f, true);
    tick(2);
    hold(0, 6, 3.5f, false);
    // The shot, put where one that cleared the right tower would be: on its way out by the right side
    const tide_entity shot = the(&world, tide_has_Ball);
    TIDE_REQUIRE(tide_has_Ball(&world, shot));
    Box2D_Body flying = tide_get_Box2D_Body(&world, shot);
    flying.position = tide_f2(12.0f, 11.0f);
    flying.velocity = tide_f2(24.0f, 2.0f);
    tide_set_Box2D_Body(&world, shot, flying);
    bool wrapped = false;
    float furthest_left = 0;
    for (int i = 0; i < 300 && count(&world, tide_has_Ball) > 0; i++) {
        tide_world_tick(&world);
        const tide_entity ball = the(&world, tide_has_Ball);
        if (!tide_has_Ball(&world, ball)) break;
        if (tide_get_Ball(&world, ball).wrapped) {
            wrapped = true;
            const float x = tide_get_Box2D_Body(&world, ball).position.x;
            if (x < furthest_left) furthest_left = x;
        }
    }
    TIDE_CHECK(wrapped);
    TIDE_CHECK(furthest_left < -12.0f);
    tide_world_free(&world);
}

TIDE_TEST(siege_reset_rebuilds_and_few_crates_left_end_the_round)
{
    start(false);
    tick(60);
    tide_world built = {0};
    tide_world_copy(&built, &world);
    // Not at the same height: shots that meet over the pit stop each other
    hold(0, 6, 3.5f, true);
    hold(1, -6, 5.5f, true);
    tick(240);
    hold(0, 6, 3.5f, false);
    hold(1, -6, 5.5f, false);
    TIDE_CHECK(disturbed(&world, &built, -1) > 1.0f && disturbed(&world, &built, 1) > 1.0f);
    Cannon scored = cannon(&world, 0);
    scored.score = 9;
    tide_set_Cannon(&world, cannon_at(&world, 0), scored);
    TIDE_REQUIRE(command(&world, TIDE_COMMAND_ID_Rebuild, 1));
    tick(2); // The command's tick, and the one that clears and builds
    TIDE_CHECK(world.Siege.round == 1 && world.Siege.phase == Phase_Play);
    TIDE_CHECK(cannon(&world, 0).score == 0);
    tick(120);
    TIDE_CHECK(count(&world, tide_has_Crate) == PIECES && count(&world, tide_has_Ball) == 0);
    TIDE_CHECK(count(&world, tide_has_Bumper) == 3 && world.Siege.standing == PIECES);
    TIDE_CHECK(painted(&world, -1) == PIECES);
    // A round that's built is the round that was: every crate where the first one's settled
    float apart = 0;
    int compared = 0;
    for (uint32_t i = 0; i < world.entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&world.entities, i);
        if (!tide_has_Crate(&world, e)) continue;
        const tide_float2 at = tide_get_Box2D_Body(&world, e).position;
        float nearest = 1000;
        for (uint32_t j = 0; j < built.entities.next_unused; j++) {
            const tide_entity other = tide_entity_in_slot(&built.entities, j);
            if (!tide_has_Crate(&built, other)) continue;
            const tide_float2 was = tide_get_Box2D_Body(&built, other).position;
            const float d = tide_abs_f(at.x - was.x) + tide_abs_f(at.y - was.y);
            if (d < nearest) nearest = d;
        }
        if (nearest > apart) apart = nearest;
        compared++;
    }
    TIDE_CHECK(compared == PIECES && apart < 0.01f);

    // With a dozen pieces left on the map, the round is over: it's whoever's scored most in it
    Cannon ahead = cannon(&world, 2);
    ahead.round = 7;
    ahead.score = 7;
    tide_set_Cannon(&world, cannon_at(&world, 2), ahead);
    world.Siege.standing = 12;
    tick(1);
    TIDE_CHECK(world.Siege.phase == Phase_Over && world.Siege.winner == 2);
    hold(0, 6, 3.5f, true);
    tick(60);
    hold(0, 6, 3.5f, false);
    TIDE_CHECK(count(&world, tide_has_Ball) == 0); // Nobody fires between rounds
    tick(200);
    TIDE_CHECK(world.Siege.phase == Phase_Play && world.Siege.round == 2 && world.Siege.winner == -1);
    TIDE_CHECK(count(&world, tide_has_Crate) == PIECES);
    // What was scored stays, and the next round starts from nothing
    TIDE_CHECK(cannon(&world, 2).score == 7 && cannon(&world, 2).round == 0);
    tide_world_free(&built);
    tide_world_free(&world);
}

TIDE_TEST(siege_cannons_nobody_has_play_by_themselves)
{
    start(true);
    tide_world built = {0};
    tide_world_copy(&built, &world);
    bool seats[4] = {false, false, false, false};
    for (int i = 0; i < 900; i++) {
        tide_world_tick(&world);
        for (uint32_t j = 0; j < world.entities.next_unused; j++) {
            const tide_entity e = tide_entity_in_slot(&world.entities, j);
            if (tide_has_Ball(&world, e)) seats[tide_get_Ball(&world, e).seat] = true;
        }
    }
    // The two seats nobody took fired, and the players' cannons, whose players did nothing, didn't
    TIDE_CHECK(!seats[0] && !seats[1] && seats[2] && seats[3]);
    TIDE_CHECK(painted(&world, 2) + painted(&world, 3) > 0);
    TIDE_CHECK(world.Siege.flip == 0 && world.Box2D_Physics.gravity.y == -10.0f);
    // The Bots button stops them
    TIDE_REQUIRE(command(&world, TIDE_COMMAND_ID_Bots, 0));
    tick(400);
    TIDE_CHECK(!world.Siege.bots && count(&world, tide_has_Ball) == 0);
    tide_world_free(&built);
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

// Whether this machine's player fires, holding the button down with the pointer on the map.
static bool fires(void)
{
    tide_devices held = devices;
    held.pointer.press.pressed = true;
    return tide_input_sample(&held, &local).fire;
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

TIDE_TEST(siege_opens_on_its_lobby_unless_it_was_told_which_match)
{
    start(true);
    // Started with nothing said: a match of its own, closed, behind the title.
    // The player's cannon does nothing while the lobby is up
    machine(TIDE_SESSION_CONNECTED, 0, true, false, "");
    frame(&world);
    frame(&world);
    TIDE_CHECK(local.Lobby.page == Page_Title && !local.Pointer.playing && !fires());
    TIDE_CHECK(local.Lobby.players == 2 && local.Lobby.mine == 0 && local.Lobby.manned == 3);
    TIDE_CHECK(asked() == TIDE_REQUEST_NONE);
    tide_local_free(&local);

    // --host, or a page's ?host: the match is open by the first frame, and no lobby is in the way
    machine(TIDE_SESSION_CONNECTED, 0, true, true, "K7QF2M");
    frame(&world);
    frame(&world);
    TIDE_CHECK(local.Lobby.page == Page_None && local.Pointer.playing && fires());
    // The button is held as the menu closes: nothing fires until it's let go
    local.Lobby.page = Page_Room;
    frame(&world);
    TIDE_CHECK(!fires());
    local.Lobby.page = Page_None;
    devices.pointer.press.pressed = devices.pointer.press.held = true;
    frame(&world);
    TIDE_CHECK(!fires());
    devices.pointer.press.pressed = devices.pointer.press.held = false;
    frame(&world);
    TIDE_CHECK(fires());
    tide_local_free(&local);

    // --join <code>, or ?join=<code>: joining by the first frame. The page says so until the match's world is here
    machine(TIDE_SESSION_CONNECTING, -1, false, false, "K7QF2M");
    frame(NULL);
    TIDE_CHECK(local.Lobby.page == Page_Join && local.Lobby.joining && !fires());
    tide_local_connected(&local);
    tide_local_set_session(&local, TIDE_SESSION_CONNECTED, player(1), 30, false, false, "K7QF2M");
    frame(&world);
    frame(&world);
    TIDE_CHECK(local.Lobby.page == Page_None && !local.Lobby.joining && fires());
    TIDE_CHECK(local.Lobby.mine == 1);
    // That match ends: back on the title, with a line that says so, and a match of its own asked for
    tide_local_disconnected(&local, TIDE_DISCONNECT_ENDED, "");
    tide_local_set_session(&local, TIDE_SESSION_OFFLINE, (tide_player_id){0}, 0, false, false, "");
    frame(NULL);
    frame(NULL);
    TIDE_CHECK(local.Lobby.page == Page_Title && says_something() && !fires());
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

// The sparks, the numbers and the words come from what the match sends: a
// tick that tells this machine what it sent, and the frame that shows it.
static tide_event_log heard;

static void tick_and_show(void)
{
    tide_world_tick_events(&world, NULL, &heard);
    tide_local_events(&local, &heard);
    frame(&world);
}

TIDE_TEST(siege_effects_come_from_what_the_match_sends)
{
    start(false);
    machine(TIDE_SESSION_CONNECTED, 0, true, true, "K7QF2M");
    frame(&world);
    frame(&world);
    TIDE_CHECK(tide_list_read_count(&local.heap, local.Fx.bits) == 0 && local.Fx.seat == 0);

    // A shot leaves the first seat's cannon: sparks at its muzzle, and this machine's camera feels it
    hold(0, 6, 3.5f, true);
    tick_and_show();
    hold(0, 6, 3.5f, false);
    TIDE_CHECK(count(&world, tide_has_Ball) == 1);
    TIDE_CHECK(tide_list_read_count(&local.heap, local.Fx.bits) > 0 && local.Fx.trauma > 0.0f);
    // It lands on a crate, which flashes
    bool flashed = false;
    for (int i = 0; i < 90 && !flashed; i++) {
        tick_and_show();
        for (uint32_t j = 0; j < world.entities.next_unused; j++) {
            const tide_entity e = tide_entity_in_slot(&world.entities, j);
            if (tide_has_Crate(&world, e) && tide_get_Crate_local(&local, e).hit > 0.0f) flashed = true;
        }
    }
    TIDE_CHECK(flashed);

    // A king the first seat painted leaves the map: what it was worth floats up, and the seat's score jumps
    for (uint32_t i = 0; i < world.entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&world.entities, i);
        if (!tide_has_Crate(&world, e) || tide_get_Crate(&world, e).worth != 5) continue;
        Crate king = tide_get_Crate(&world, e);
        king.last = 0;
        tide_set_Crate(&world, e, king);
        Box2D_Body body = tide_get_Box2D_Body(&world, e);
        body.position = tide_f2(0.0f, -2.0f);
        tide_set_Box2D_Body(&world, e, body);
        break;
    }
    bool labelled = false;
    for (int i = 0; i < 120 && !labelled; i++) {
        tick_and_show();
        labelled = tide_list_read_count(&local.heap, local.Fx.labels) > 0;
    }
    TIDE_CHECK(labelled && local.Fx.pops.x > 0.5f && cannon(&world, 0).score == 5);

    // A round ends, and the next is built: the words over the map know who took it
    Cannon ahead = cannon(&world, 2);
    ahead.round = 7;
    tide_set_Cannon(&world, cannon_at(&world, 2), ahead);
    world.Siege.standing = 12;
    tick_and_show();
    TIDE_CHECK(local.Flow.winner == 2 && local.Flow.best == 7 && local.Flow.round == 1 && local.Flow.since < 0.1f);
    for (int i = 0; i < 200 && world.Siege.round == 1; i++) tick_and_show();
    TIDE_CHECK(world.Siege.round == 2 && local.Flow.built == 2 && local.Flow.begun < 0.1f);

    // A tick nobody listens to shows nothing, and the match goes the same
    const float since = local.Flow.begun;
    world.Siege.standing = 12;
    tide_world_tick(&world);
    frame(&world);
    TIDE_CHECK(world.Siege.phase == Phase_Over && local.Flow.round == 1 && local.Flow.begun > since);
    tide_event_log_free(&heard);
    tide_local_free(&local);
    tide_draw_free(&draw);
    tide_world_free(&world);
}

// ---------------------------------------------------------------------------
// Three players in a match, over a network that's late and loses packets

// What a player does at each tick of the match: where it points, and whether
// it fires. The first puts shots through the orb, and once gravity is back
// shells the right tower; the second shells the left one, in bursts; and the
// third, who joins while they're at it, shells the right one from above.
static void play(void *user, const uint32_t tick, void *input)
{
    const int who = *(const int *)user;
    Controls controls = {.aiming = true};
    if (who == 0) {
        controls.target = tick < 300 ? tide_f2(0, 9.6f) : tide_f2(6, 3.5f);
        controls.fire = (tick >= 150 && tick < 185) || (tick >= 420 && tick < 600);
    } else if (who == 1) {
        // Over the bridge, and under the bumper above it
        controls.target = tide_f2(-7.5f, 10.5f);
        controls.fire = tick >= 440 && tick < 600 && tick % 40 < 30;
    } else {
        controls.target = tide_f2(7, 7);
        controls.fire = tick >= 560 && tick < 640;
    }
    memcpy(input, &controls, sizeof controls);
}

typedef struct match {
    tide_loopback *net;
    tide_server *server;
    tide_client *players[3];
    double now;
} match;

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
    static int names[3] = {0, 1, 2};
    m->players[who] = tide_client_create(&(tide_client_desc){.game = &tide_game_api,
                                           .transport = tide_loopback_endpoint(m->net, (uint32_t)who + 2),
                                           .server = tide_loopback_address(1), .lead = 2,
                                           .sample = play, .user = &names[who]}, m->now);
}

static int seat_of(const tide_world *w, const tide_client *client)
{
    const tide_player_id id = tide_client_status_of(client).player;
    for (int seat = 0; seat < 4; seat++) {
        const tide_entity e = cannon_at(w, seat);
        if (tide_has_Owner(w, e) && tide_get_Owner(w, e).player.id == id.id) return seat;
    }
    return -1;
}

TIDE_TEST(siege_three_players_stay_in_one_match_over_a_bad_network)
{
    b2t_forget();
    match m = {.net = tide_loopback_create(4471)};
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
    TIDE_CHECK(seat_of(authority, m.players[0]) == 0 && seat_of(authority, m.players[1]) == 1);

    // Shots at the orb: the bridge may stop the first, when it's in the way
    // just then, and the next goes through. Gravity turns over, and comes back
    bool flipped = false;
    bool risen = false;
    while (tide_server_tick(m.server) < 420) {
        advance(&m);
        authority = tide_server_world(m.server);
        if (authority->Siege.flip > 0) flipped = true;
        if (authority->Box2D_Physics.gravity.y == 7.0f) risen = true;
    }
    TIDE_CHECK(flipped && risen);
    TIDE_CHECK(authority->Siege.flip == 0 && authority->Box2D_Physics.gravity.y == -10.0f);

    // The shelling, which the third player joins a second into: the world it's
    // sent has Box2D's state in it, with every crate that's in the air
    int shots = 0;
    while (tide_server_tick(m.server) < 680) {
        if (!m.players[2] && tide_server_tick(m.server) >= 480) {
            TIDE_CHECK(count(tide_server_world(m.server), tide_has_Ball) >= 4);
            join(&m, 2);
        }
        advance(&m);
        authority = tide_server_world(m.server);
        const int balls = count(authority, tide_has_Ball);
        if (balls > shots) shots = balls;
    }
    TIDE_REQUIRE(m.players[2] != NULL);
    TIDE_REQUIRE(tide_client_status_of(m.players[2]).state == TIDE_SESSION_CONNECTED);
    TIDE_CHECK(seat_of(authority, m.players[2]) == 2);
    TIDE_CHECK(shots >= 10);
    TIDE_CHECK(painted(authority, 0) + cannon(authority, 0).score > 0);
    TIDE_CHECK(painted(authority, 1) + cannon(authority, 1).score > 0);
    TIDE_CHECK(painted(authority, 2) + cannon(authority, 2).score > 0);
    const uint64_t shelled = tide_world_hash(authority);
    printf("    at tick 680: seats scored %d, %d, %d and %d, %d pieces stand, %d shots in the air at most\n",
           cannon(authority, 0).score, cannon(authority, 1).score, cannon(authority, 2).score,
           cannon(authority, 3).score, authority->Siege.standing, shots);
    printf("    the third player was sent %llu bytes of the world it joined, whose physics are %d of them\n",
           (unsigned long long)tide_client_status_of(m.players[2]).world_bytes, authority->Box2D_Physics.size);
    printf("    the server's world at tick %u: 0x%016llx\n", tide_server_tick(m.server), (unsigned long long)shelled);
    TIDE_CHECK(tide_server_tick(m.server) == 680);
    TIDE_CHECK(shelled == SHELLED_HASH);

    // The second player presses Reset: a command, which the server gives a tick
    uint8_t bytes[TIDE_COMMAND_BYTES];
    const uint32_t size = plain_command(TIDE_COMMAND_ID_Rebuild, bytes);
    TIDE_REQUIRE(tide_game_api.check_command(bytes, size));
    TIDE_REQUIRE(tide_client_send_command(m.players[1], bytes, size));
    while (tide_server_tick(m.server) < 760) advance(&m);
    authority = tide_server_world(m.server);
    TIDE_CHECK(authority->Siege.round == 1 && cannon(authority, 0).score == 0 && cannon(authority, 2).score == 0);
    TIDE_CHECK(count(authority, tide_has_Crate) + (PIECES - authority->Siege.standing) == PIECES);

    // No machine's world ever went another way than the server's: what each
    // predicted wrong it ran again, and every tick it confirmed had the
    // server's hash, Box2D's state in it. The one who joined late too.
    for (int i = 0; i < 3; i++) {
        const tide_client_status status = tide_client_status_of(m.players[i]);
        printf("    player %d ran %u predicted ticks again, and was sent the world again %u times\n", i + 1,
               status.again_ticks, status.resyncs);
        TIDE_CHECK(status.state == TIDE_SESSION_CONNECTED && status.resyncs == 0 && status.again_ticks > 0);
        const tide_world *confirmed = tide_client_verified_world(m.players[i]);
        TIDE_CHECK(confirmed != NULL && confirmed->Siege.round == 1);
    }

    const uint64_t hash = tide_world_hash(authority);
    printf("    the server's world at tick %u: 0x%016llx\n", tide_server_tick(m.server), (unsigned long long)hash);
    TIDE_CHECK(tide_server_tick(m.server) == 760);
    TIDE_CHECK(hash == MATCH_HASH);

    for (int i = 2; i >= 0; i--) tide_client_destroy(m.players[i]);
    tide_server_destroy(m.server);
    tide_loopback_destroy(m.net);
}

// What a tick costs with all four cannons firing, which is what a client
// that predicts pays several times a frame. Timings aren't checked: machines
// differ too much.
TIDE_TEST(siege_costs)
{
    start(true);
    hold(0, 6, 3.5f, true);
    hold(1, -6, 5.5f, true);
    tick(60);
    tide_world kept = {0};
    uint64_t ticking = 0;
    uint64_t keeping = 0;
    int most = 0;
    const int ticks = 300;
    for (int i = 0; i < ticks; i++) {
        // A client keeps each tick's world, and hashes it
        const uint64_t began = tide_time_now_ns();
        tide_world_tick(&world);
        const uint64_t ticked = tide_time_now_ns();
        tide_world_copy(&kept, &world);
        (void)tide_world_hash(&world);
        ticking += ticked - began;
        keeping += tide_time_now_ns() - ticked;
        const int bodies = count(&world, tide_has_Box2D_Body);
        if (bodies > most) most = bodies;
    }
    printf("    up to %d bodies, the towers under fire: %.3f ms a tick, and %.3f to keep and hash its world (%d bytes of"
           " physics)\n",
           most, (double)ticking / ticks / 1e6, (double)keeping / ticks / 1e6, world.Box2D_Physics.size);
    tide_world_free(&kept);
    tide_world_free(&world);
}
