// Alley with no window: what the game promises, on one world; what a machine
// shows of it and where its lobby leaves it, a frame at a time; and four
// players in a match on the loopback network, two of whom join after balls
// were thrown, whose server's world is pinned.

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
void b3t_forget(void);

#define DT (1.0f / 60.0f)
#define TARGETS 72     // 55 pins, 14 crates and 3 kingpins
#define LANE_END 16.5f
#define PARK -2.8f     // Where the bar waits

// The server's world in the match below, on every platform and in every
// configuration: Box3D's math, its image and the netcode, byte for byte.
// Once with the lane in the air, and once after a Reset set it up again.
#define THROWN_HASH UINT64_C(0x768d20e0fb8237a0)
#define MATCH_HASH UINT64_C(0x9fd0f346722dc9bb)

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

// A seat's bowler, or no entity
static tide_entity bowler_at(const tide_world *w, const int seat)
{
    for (uint32_t i = 0; i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        if (tide_has_Bowler(w, e) && tide_get_Bowler(w, e).seat == seat) return e;
    }
    return (tide_entity){0};
}

static int score(const tide_world *w, const int seat)
{
    return tide_get_Bowler(w, bowler_at(w, seat)).score;
}

// How many targets a seat's balls have painted.
static int painted(const tide_world *w, const int seat)
{
    int found = 0;
    for (uint32_t i = 0; i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        if (tide_has_Target(w, e) && tide_get_Target(w, e).last == seat) found++;
    }
    return found;
}

// How far a target is from where a round puts it, the furthest of them.
static float disturbed(const tide_world *w, const tide_world *built)
{
    float most = 0;
    for (uint32_t i = 0; i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        if (!tide_has_Target(w, e) || !tide_has_Target(built, e)) continue;
        const tide_float3 now = tide_get_Box3D_Body(w, e).position;
        const tide_float3 then = tide_get_Box3D_Body(built, e).position;
        const float moved = tide_abs_f(now.x - then.x) + tide_abs_f(now.y - then.y) + tide_abs_f(now.z - then.z);
        if (moved > most) most = moved;
    }
    return most;
}

// A match of three players, with its first round set up and at rest.
static void start(void)
{
    b3t_forget();
    tide_world_free(&world);
    tide_world_init(&world, DT);
    for (int i = 0; i < 3; i++) tide_world_player_joined(&world, player(i));
    for (int i = 0; i < 120; i++) tide_world_tick(&world);
}

static void hold(const int who, const float x, const float z, const float spin, const bool fire)
{
    tide_world_set_input(&world, player(who), (Controls){.target = {x, z}, .spin = spin, .aiming = true, .fire = fire});
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
    return tide_game_api.command(w, player(sender), 0, tide_game_api.command_sequence(w, player(sender), 0) + 1, bytes, size);
}

TIDE_TEST(alley_sets_the_lane_up_and_seats_its_players)
{
    start();
    tide_world built = {0};
    tide_world_copy(&built, &world);
    TIDE_CHECK(world.Lane.phase == Phase_Play && world.Lane.round == 1);
    TIDE_CHECK(count(&world, tide_has_Target) == TARGETS && world.Lane.standing == TARGETS);
    TIDE_CHECK(count(&world, tide_has_Bowler) == 3 && count(&world, tide_has_Sweeper) == 1);
    for (int seat = 0; seat < 3; seat++) {
        TIDE_CHECK(tide_get_Owner(&world, bowler_at(&world, seat)).player.id == player(seat).id);
    }
    // Pins stand on their feet: with nobody throwing, the world comes to rest, and stays there
    tick(240);
    TIDE_CHECK(!world.Box3D_Physics.moved);
    TIDE_CHECK(disturbed(&world, &built) < 0.01f);
    TIDE_CHECK(count(&world, tide_has_Ball) == 0 && world.Lane.through == 0);
    // A player who leaves takes their bowler with them, and the next to join has the seat
    tide_world_player_left(&world, player(1));
    tick(1);
    TIDE_CHECK(count(&world, tide_has_Bowler) == 2 && !tide_has_Bowler(&world, bowler_at(&world, 1)));
    tide_world_player_joined(&world, player(5));
    tick(1);
    TIDE_CHECK(tide_get_Owner(&world, bowler_at(&world, 1)).player.id == player(5).id);
    tide_world_free(&built);
    tide_world_free(&world);
}

TIDE_TEST(alley_a_ball_knocks_pins_and_paints_them)
{
    start();
    tide_world built = {0};
    tide_world_copy(&built, &world);
    hold(0, 0, 9, 0, true);
    tick(2);
    // It's thrown with an impulse down the lane, and the roll that goes with it
    const tide_entity ball = the(&world, tide_has_Ball);
    TIDE_REQUIRE(tide_has_Ball(&world, ball));
    const Box3D_Body thrown = tide_get_Box3D_Body(&world, ball);
    TIDE_CHECK(thrown.velocity.z > 15.0f && thrown.angularVelocity.x > 30.0f);
    tick(118);
    hold(0, 0, 9, 0, false);
    printf("    after two seconds of balls, a target is %.2f m from where it stood, and %d are the first seat's color\n",
           (double)disturbed(&world, &built), painted(&world, 0));
    TIDE_CHECK(disturbed(&world, &built) > 2.0f);
    TIDE_CHECK(painted(&world, 0) >= 8 && painted(&world, 1) == 0 && painted(&world, 2) == 0);
    TIDE_CHECK(count(&world, tide_has_Ball) >= 4);
    // Balls go after seven seconds
    tick(440);
    TIDE_CHECK(count(&world, tide_has_Ball) == 0);
    tide_world_free(&built);
    tide_world_free(&world);
}

// Where a ball thrown straight down the lane is across it, a second later.
static float curve(const float spin)
{
    start();
    // Nothing in its way
    for (uint32_t i = 0; i < world.entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&world.entities, i);
        if (tide_has_Target(&world, e)) {
            Box3D_Body body = tide_get_Box3D_Body(&world, e);
            body.position.y = -20.0f;
            tide_set_Box3D_Body(&world, e, body);
        }
    }
    const float from = (0.0f - 3.5f) * 0.72f; // The first seat's place on the line
    hold(0, from, 30, spin, true);
    tick(2);
    hold(0, from, 30, spin, false);
    const tide_entity ball = the(&world, tide_has_Ball);
    tick(50);
    const float across = tide_has_Ball(&world, ball) ? tide_get_Box3D_Body(&world, ball).position.x - from : 0.0f;
    tide_world_free(&world);
    return across;
}

TIDE_TEST(alley_spin_curves_a_ball)
{
    const float straight = curve(0);
    const float right = curve(1);
    const float left = curve(-1);
    printf("    a second down the lane, a ball is %.2f m across it thrown straight, %.2f with all the spin one way and"
           " %.2f the other\n",
           (double)straight, (double)right, (double)left);
    TIDE_CHECK(tide_abs_f(straight) < 0.02f);
    TIDE_CHECK(right > 0.3f && left < -0.3f);
}

TIDE_TEST(alley_what_goes_through_the_goal_scores_for_who_painted_it)
{
    start();
    const tide_entity goal = the(&world, tide_has_Goal);
    // A pin the second seat painted and a kingpin the third did, put over the goal past the lane's end, and a
    // pin the first seat painted, put off the lane's side: writing a body's position moves it
    int moved = 0;
    for (int want = 0; want < 3; want++) {
        for (uint32_t i = 0; i < world.entities.next_unused; i++) {
            const tide_entity e = tide_entity_in_slot(&world.entities, i);
            if (!tide_has_Target(&world, e) || !tide_has_Box3D_Capsule(&world, e)) continue;
            Target target = tide_get_Target(&world, e);
            if (target.last >= 0 || (target.worth == 5) != (want == 1)) continue;
            target.last = want == 0 ? 1 : want == 1 ? 2 : 0;
            tide_set_Target(&world, e, target);
            Box3D_Body body = tide_get_Box3D_Body(&world, e);
            body.position = want == 2 ? tide_f3(6.0f, 1.0f, 5.0f) : tide_f3((float)want * 2.0f - 1.0f, 1.0f, LANE_END + 4.0f);
            tide_set_Box3D_Body(&world, e, body);
            moved++;
            break;
        }
    }
    TIDE_REQUIRE(moved == 3);
    int most = 0;
    for (int i = 0; i < 240; i++) {
        tide_world_tick(&world);
        const int inside = tide_get_Goal(&world, goal).inside;
        if (inside > most) most = inside;
    }
    // The sensor heard them come in, and leave: a body that's destroyed in it parts from it
    printf("    the goal had %d shapes in it at once, and has %d now\n", most, tide_get_Goal(&world, goal).inside);
    TIDE_CHECK(most >= 2 && tide_get_Goal(&world, goal).inside == 0);
    TIDE_CHECK(world.Lane.through == 2);
    TIDE_CHECK(score(&world, 1) == 1 && score(&world, 2) == 5 && score(&world, 0) == 0);
    TIDE_CHECK(world.Lane.standing == TARGETS - 3 && count(&world, tide_has_Target) == TARGETS - 3);
    tide_world_free(&world);
}

static float highest(const tide_world *w)
{
    float most = 0;
    for (uint32_t i = 0; i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        if (tide_has_Target(w, e) && tide_get_Box3D_Body(w, e).position.y > most) most = tide_get_Box3D_Body(w, e).position.y;
    }
    return most;
}

TIDE_TEST(alley_gravity_lets_go_and_everything_hops)
{
    start();
    const float rest = highest(&world);
    TIDE_CHECK(world.Box3D_Physics.gravity.y == -10.0f && !world.Box3D_Physics.moved);
    TIDE_REQUIRE(command(&world, TIDE_COMMAND_ID_Float, 2));
    tick(1);
    TIDE_CHECK(world.Lane.low && world.Lane.hop);
    tick(1);
    TIDE_CHECK(world.Box3D_Physics.gravity.y == -1.6f && !world.Lane.hop);
    float top = 0;
    for (int i = 0; i < 120; i++) {
        tide_world_tick(&world);
        if (highest(&world) > top) top = highest(&world);
    }
    printf("    the highest target went from %.2f m to %.2f\n", (double)rest, (double)top);
    TIDE_CHECK(top > rest + 1.0f);
    // And weighs again
    TIDE_REQUIRE(command(&world, TIDE_COMMAND_ID_Float, 0));
    tick(2);
    TIDE_CHECK(!world.Lane.low && world.Box3D_Physics.gravity.y == -10.0f);
    tide_world_free(&world);
}

TIDE_TEST(alley_the_bar_sweeps_the_lane_and_the_next_round_is_set_up)
{
    start();
    const tide_entity bar = the(&world, tide_has_Sweeper);
    TIDE_CHECK(tide_get_Box3D_Body(&world, bar).position.z == PARK);
    TIDE_REQUIRE(command(&world, TIDE_COMMAND_ID_Sweep, 1));
    tick(3);
    TIDE_CHECK(tide_get_Sweeper(&world, bar).sweeping && tide_get_Box3D_Body(&world, bar).velocity.z == 9.0f);
    bool back = false;
    float furthest = PARK;
    for (int i = 0; i < 400 && !back; i++) {
        tide_world_tick(&world);
        const Box3D_Body body = tide_get_Box3D_Body(&world, bar);
        if (body.position.z > furthest) furthest = body.position.z;
        // From the lane's end to where it waits in one tick: its position was written
        back = !tide_get_Sweeper(&world, bar).sweeping;
        if (back) TIDE_CHECK(body.position.z == PARK && body.velocity.z == 0.0f);
    }
    TIDE_CHECK(back && furthest > LANE_END);
    TIDE_CHECK(tide_get_Sweeper(&world, bar).rest > 0);
    // Asked again too soon, it stays where it is
    TIDE_REQUIRE(command(&world, TIDE_COMMAND_ID_Sweep, 1));
    tick(3);
    TIDE_CHECK(!tide_get_Sweeper(&world, bar).sweeping);
    int through = world.Lane.through;
    for (int i = 0; i < 300; i++) {
        tide_world_tick(&world);
        if (world.Lane.through > through) through = world.Lane.through;
    }
    printf("    the bar pushed %d of %d targets through the goal\n", through, TARGETS);
    TIDE_CHECK(through > TARGETS / 2);
    // Nobody painted them, so nobody scored
    TIDE_CHECK(score(&world, 0) == 0 && score(&world, 1) == 0 && score(&world, 2) == 0);
    // With next to nothing left, the round was over, and the next one is set up
    TIDE_CHECK(world.Lane.round == 2 && world.Lane.phase == Phase_Play);
    TIDE_CHECK(count(&world, tide_has_Target) == TARGETS && world.Lane.standing == TARGETS && world.Lane.through == 0);
    tide_world_free(&world);
}

TIDE_TEST(alley_reset_sets_the_lane_up_again)
{
    start();
    tide_world built = {0};
    tide_world_copy(&built, &world);
    hold(0, -0.5f, 9, 0.5f, true);
    hold(1, 0.8f, 8, -0.5f, true);
    tick(200);
    hold(0, -0.5f, 9, 0.5f, false);
    hold(1, 0.8f, 8, -0.5f, false);
    TIDE_CHECK(disturbed(&world, &built) > 2.0f && painted(&world, 0) > 0 && painted(&world, 1) > 0);
    Bowler ahead = tide_get_Bowler(&world, bowler_at(&world, 0));
    ahead.score = 9;
    tide_set_Bowler(&world, bowler_at(&world, 0), ahead);
    TIDE_REQUIRE(command(&world, TIDE_COMMAND_ID_Rebuild, 2));
    tick(2); // The command's tick, and the one that clears and sets up
    TIDE_CHECK(world.Lane.round == 1 && world.Lane.phase == Phase_Play && score(&world, 0) == 0);
    tick(240);
    TIDE_CHECK(count(&world, tide_has_Target) == TARGETS && count(&world, tide_has_Ball) == 0);
    TIDE_CHECK(painted(&world, -1) == TARGETS && world.Lane.standing == TARGETS);
    // A round that's set up is the round that was: every target where the first one's settled
    float apart = 0;
    for (uint32_t i = 0; i < world.entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&world.entities, i);
        if (!tide_has_Target(&world, e)) continue;
        const tide_float3 at = tide_get_Box3D_Body(&world, e).position;
        float nearest = 1000;
        for (uint32_t j = 0; j < built.entities.next_unused; j++) {
            const tide_entity other = tide_entity_in_slot(&built.entities, j);
            if (!tide_has_Target(&built, other)) continue;
            const tide_float3 was = tide_get_Box3D_Body(&built, other).position;
            const float d = tide_abs_f(at.x - was.x) + tide_abs_f(at.y - was.y) + tide_abs_f(at.z - was.z);
            if (d < nearest) nearest = d;
        }
        if (nearest > apart) apart = nearest;
    }
    TIDE_CHECK(apart < 0.01f);
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

// Whether this machine's player throws, holding the button down with the pointer on the lane.
static bool throws(void)
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

TIDE_TEST(alley_opens_on_its_lobby_unless_it_was_told_which_match)
{
    start();
    // Started with nothing said: a match of its own, closed, behind the title.
    // The player's bowler does nothing while the lobby is up
    machine(TIDE_SESSION_CONNECTED, 0, true, false, "");
    frame(&world);
    frame(&world);
    TIDE_CHECK(local.Lobby.page == Page_Title && !local.Hand.playing && !throws());
    TIDE_CHECK(local.Lobby.players == 3 && local.Lobby.mine == 0 && local.Lobby.manned == 7);
    TIDE_CHECK(asked() == TIDE_REQUEST_NONE);
    tide_local_free(&local);

    // --host, or a page's ?host: the match is open by the first frame, and no lobby is in the way
    machine(TIDE_SESSION_CONNECTED, 0, true, true, "K7QF2M");
    frame(&world);
    frame(&world);
    TIDE_CHECK(local.Lobby.page == Page_None && local.Hand.playing && throws());
    // The button is held as the menu closes: nothing is thrown until it's let go
    local.Lobby.page = Page_Room;
    frame(&world);
    TIDE_CHECK(!throws());
    local.Lobby.page = Page_None;
    devices.pointer.press.pressed = devices.pointer.press.held = true;
    frame(&world);
    TIDE_CHECK(!throws());
    devices.pointer.press.pressed = devices.pointer.press.held = false;
    frame(&world);
    TIDE_CHECK(throws());
    tide_local_free(&local);

    // --join <code>, or ?join=<code>: joining by the first frame. The page says so until the match's world is here
    machine(TIDE_SESSION_CONNECTING, -1, false, false, "K7QF2M");
    frame(NULL);
    TIDE_CHECK(local.Lobby.page == Page_Join && local.Lobby.joining && !throws());
    tide_local_connected(&local);
    tide_local_set_session(&local, TIDE_SESSION_CONNECTED, player(1), 30, false, false, "K7QF2M");
    frame(&world); // The page closes as this frame ends...
    frame(&world); // ...and the frame after it says nothing of what's held
    frame(&world);
    TIDE_CHECK(local.Lobby.page == Page_None && !local.Lobby.joining && throws());
    TIDE_CHECK(local.Lobby.mine == 1);
    // That match ends: back on the title, with a line that says so, and a match of its own asked for
    tide_local_disconnected(&local, TIDE_DISCONNECT_ENDED, "");
    tide_local_set_session(&local, TIDE_SESSION_OFFLINE, (tide_player_id){0}, 0, false, false, "");
    frame(NULL);
    frame(NULL);
    TIDE_CHECK(local.Lobby.page == Page_Title && says_something() && !throws());
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

static float popped(const int seat)
{
    return *(const float *)tide_list_read(&local.heap, local.Fx.pops, seat, sizeof(float));
}

TIDE_TEST(alley_effects_come_from_what_the_match_sends)
{
    start();
    machine(TIDE_SESSION_CONNECTED, 0, true, true, "K7QF2M");
    frame(&world);
    frame(&world);
    TIDE_CHECK(tide_list_read_count(&local.heap, local.Fx.sparks) == 0 && local.Fx.seat == 0);
    // Every target has a shadow on the lane, and the first seat's place has its marks
    TIDE_CHECK(tide_list_read_count(&local.heap, local.Fx.decals) > TARGETS);

    // This machine's player's ball leaves their hand: the camera feels it
    hold(0, 0, 9, 0, true);
    tick_and_show();
    hold(0, 0, 9, 0, false);
    TIDE_CHECK(count(&world, tide_has_Ball) == 1 && local.Fx.trauma > 0.0f);
    // It hits a pin, which flashes, and sparks fly
    bool flashed = false;
    for (int i = 0; i < 90 && !flashed; i++) {
        tick_and_show();
        for (uint32_t j = 0; j < world.entities.next_unused; j++) {
            const tide_entity e = tide_entity_in_slot(&world.entities, j);
            if (tide_has_Target(&world, e) && tide_get_Target_local(&local, e).hit > 0.0f) flashed = true;
        }
    }
    TIDE_CHECK(flashed && tide_list_read_count(&local.heap, local.Fx.sparks) > 0);

    // A kingpin the first seat painted goes through the goal: the goal lights up, what it was worth floats up
    // and the seat's score jumps
    for (uint32_t i = 0; i < world.entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&world.entities, i);
        if (!tide_has_Target(&world, e) || tide_get_Target(&world, e).worth != 5) continue;
        Target king = tide_get_Target(&world, e);
        king.last = 0;
        tide_set_Target(&world, e, king);
        Box3D_Body body = tide_get_Box3D_Body(&world, e);
        body.position = tide_f3(0.0f, 1.0f, LANE_END + 4.0f);
        tide_set_Box3D_Body(&world, e, body);
        break;
    }
    bool labelled = false;
    for (int i = 0; i < 120 && !labelled; i++) {
        tick_and_show();
        labelled = tide_list_read_count(&local.heap, local.Fx.labels) > 0;
    }
    TIDE_CHECK(labelled && popped(0) > 0.5f && local.Room.goal > 0.5f && score(&world, 0) == 5);
    TIDE_CHECK(tide_get_Bowler(&world, bowler_at(&world, 0)).round == 5);

    // Gravity lets go: the room's light follows
    TIDE_REQUIRE(command(&world, TIDE_COMMAND_ID_Float, 1));
    tick_and_show();
    TIDE_CHECK(local.Flow.lifted < 0.1f);
    for (int i = 0; i < 30; i++) tick_and_show();
    TIDE_CHECK(local.Room.low > 0.9f);

    // A round ends, and the next is set up: the words over the lane know who took it
    world.Lane.standing = 6;
    tick_and_show();
    TIDE_CHECK(world.Lane.phase == Phase_Over);
    TIDE_CHECK(local.Flow.winner == 0 && local.Flow.best == 5 && local.Flow.round == 1 && local.Flow.since < 0.1f);
    for (int i = 0; i < 200 && world.Lane.round == 1; i++) tick_and_show();
    TIDE_CHECK(world.Lane.round == 2 && local.Flow.set == 2 && local.Flow.begun < 0.1f);
    // What was scored stays, and the next round starts from nothing
    TIDE_CHECK(score(&world, 0) == 5 && tide_get_Bowler(&world, bowler_at(&world, 0)).round == 0);
    tide_event_log_free(&heard);
    tide_local_free(&local);
    tide_draw_free(&draw);
    tide_world_free(&world);
}

// ---------------------------------------------------------------------------
// Four players in a match, over a network that's late and loses packets

// What a player does at each tick of the match: where it points, the spin it
// has set and whether it throws. The first two throw from early on, one in
// bursts. The other two join while balls are on the lane, and throw then.
static void play(void *user, const uint32_t tick, void *input)
{
    const int who = *(const int *)user;
    Controls controls = {.aiming = true};
    if (who == 0) {
        controls.target = tide_f2(-0.5f, 9);
        controls.spin = 0.6f;
        controls.fire = tick >= 150 && tick < 330;
    } else if (who == 1) {
        controls.target = tide_f2(0.8f, 8);
        controls.spin = -0.6f;
        controls.fire = tick >= 170 && tick < 330 && tick % 60 < 40;
    } else if (who == 2) {
        controls.target = tide_f2(1.0f, 10);
        controls.fire = tick >= 330 && tick < 430;
    } else {
        controls.target = tide_f2(-0.6f, 10);
        controls.spin = 0.3f;
        controls.fire = tick >= 350 && tick < 430;
    }
    memcpy(input, &controls, sizeof controls);
}

#define PLAYERS 4

typedef struct match {
    tide_loopback *net;
    tide_server *server;
    tide_client *players[PLAYERS];
    double now;
} match;

static void advance(match *m)
{
    m->now += 1.0 / 60;
    tide_loopback_set_time(m->net, m->now);
    for (int i = 0; i < PLAYERS; i++) {
        if (m->players[i]) tide_client_update(m->players[i], m->now);
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

static int seat_of(const tide_world *w, const tide_client *client)
{
    const tide_player_id id = tide_client_status_of(client).player;
    for (int seat = 0; seat < 8; seat++) {
        const tide_entity e = bowler_at(w, seat);
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

TIDE_TEST(alley_four_players_stay_in_one_match_over_a_bad_network)
{
    b3t_forget();
    match m = {.net = tide_loopback_create(9102)};
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

    // Balls down the lane, which two more players join while they roll: the
    // world they're sent has Box3D's state in it, with every pin that's in the air
    int balls = 0;
    bool low = false;
    bool swept = false;
    while (tide_server_tick(m.server) < 520) {
        const uint32_t tick = tide_server_tick(m.server);
        if (!m.players[2] && tick >= 260) {
            TIDE_CHECK(count(tide_server_world(m.server), tide_has_Ball) >= 3);
            join(&m, 2);
        }
        if (!m.players[3] && tick >= 300) join(&m, 3);
        // The second player lets gravity go, and the first asks for the bar: commands, which the server gives a tick
        if (!low && tick >= 440) low = send(&m, 1, TIDE_COMMAND_ID_Float);
        if (!swept && tick >= 470) swept = send(&m, 0, TIDE_COMMAND_ID_Sweep);
        advance(&m);
        authority = tide_server_world(m.server);
        const int rolling = count(authority, tide_has_Ball);
        if (rolling > balls) balls = rolling;
    }
    TIDE_REQUIRE(m.players[2] != NULL && m.players[3] != NULL && low && swept);
    for (int i = 0; i < PLAYERS; i++) {
        TIDE_REQUIRE(tide_client_status_of(m.players[i]).state == TIDE_SESSION_CONNECTED);
        TIDE_CHECK(seat_of(authority, m.players[i]) == i);
        TIDE_CHECK(painted(authority, i) + score(authority, i) > 0);
    }
    TIDE_CHECK(balls >= 8);
    TIDE_CHECK(authority->Lane.low && authority->Box3D_Physics.gravity.y == -1.6f);
    TIDE_CHECK(tide_get_Sweeper(authority, the(authority, tide_has_Sweeper)).sweeping);
    const uint64_t thrown = tide_world_hash(authority);
    printf("    at tick 520: seats scored %d, %d, %d and %d, %d went through the goal and %d targets are left, %d balls"
           " at most\n",
           score(authority, 0), score(authority, 1), score(authority, 2), score(authority, 3), authority->Lane.through,
           authority->Lane.standing, balls);
    printf("    the third player was sent %llu bytes of the world it joined, whose physics are %d of them\n",
           (unsigned long long)tide_client_status_of(m.players[2]).world_bytes, authority->Box3D_Physics.size);
    printf("    the server's world at tick %u: 0x%016llx\n", tide_server_tick(m.server), (unsigned long long)thrown);
    TIDE_CHECK(tide_server_tick(m.server) == 520);
    TIDE_CHECK(thrown == THROWN_HASH);

    // The third player presses Reset
    TIDE_REQUIRE(send(&m, 2, TIDE_COMMAND_ID_Rebuild));
    while (tide_server_tick(m.server) < 640) advance(&m);
    authority = tide_server_world(m.server);
    TIDE_CHECK(authority->Lane.round == 1 && score(authority, 0) == 0 && score(authority, 3) == 0);

    // No machine's world ever went another way than the server's: what each
    // predicted wrong it ran again, and every tick it confirmed had the
    // server's hash, Box3D's state in it. The two who joined late too.
    for (int i = 0; i < PLAYERS; i++) {
        const tide_client_status status = tide_client_status_of(m.players[i]);
        printf("    player %d ran %u predicted ticks again, was sent the world again %u times, and is %u ticks ahead of"
               " what's confirmed (its inputs wait %.0f ms at the server)\n",
               i + 1, status.again_ticks, status.resyncs, status.predicted_tick - status.verified_tick,
               (double)status.early_ms);
        TIDE_CHECK(status.state == TIDE_SESSION_CONNECTED && status.resyncs == 0 && status.again_ticks > 0);
        const tide_world *confirmed = tide_client_verified_world(m.players[i]);
        TIDE_CHECK(confirmed != NULL && confirmed->Lane.round == 1);
    }

    const uint64_t hash = tide_world_hash(authority);
    printf("    the server's world at tick %u: 0x%016llx\n", tide_server_tick(m.server), (unsigned long long)hash);
    TIDE_CHECK(tide_server_tick(m.server) == 640);
    TIDE_CHECK(hash == MATCH_HASH);

    for (int i = PLAYERS - 1; i >= 0; i--) tide_client_destroy(m.players[i]);
    tide_server_destroy(m.server);
    tide_loopback_destroy(m.net);
}

// What a tick costs with three players throwing at once, which is what a
// client that predicts pays several times a frame. Timings aren't checked:
// machines differ too much.
TIDE_TEST(alley_costs)
{
    start();
    hold(0, -0.5f, 9, 0.5f, true);
    hold(1, 0.8f, 8, -0.5f, true);
    hold(2, 1.5f, 12, 0, true);
    tick(30);
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
        const int bodies = count(&world, tide_has_Box3D_Body);
        if (bodies > most) most = bodies;
    }
    printf("    up to %d bodies, three players throwing: %.3f ms a tick, and %.3f to keep and hash its world (%d bytes of"
           " physics)\n",
           most, (double)ticking / ticks / 1e6, (double)keeping / ticks / 1e6, world.Box3D_Physics.size);
    tide_world_free(&kept);
    tide_world_free(&world);
}
