#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/shard_client.h"
#include "tide/shards.h"
#include "tide_test.h"

#define PLANET 3000.0f

static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_devices devices;

static void draw_world(const tide_world *w, const tide_player_id who)
{
    tide_local_set_session(&local, TIDE_SESSION_CONNECTED, who, 0, true, false, "");
    tide_local_frame_time(&local, 1.0f / 60.0f);
    tide_draw_reset(&draw);
    tide_gui_begin(&gui, &devices, tide_f2(640, 480), NULL);
    tide_frame(w, NULL, 1, w, NULL, 1, &local, &draw, &gui);
    tide_gui_end(&gui, &draw);
}

static void sample_local(void *user, const uint32_t tick, void *input)
{
    (void)user;
    (void)tick;
    *(Controls *)input = tide_input_sample(&devices, &local);
}

// A failed hosted join starts a local, one-region match. Exercise the actual
// views and sampled controls as well as the simulation: a direct set_input
// test cannot catch a landing request being lost by presentation.
TIDE_TEST(earth_a_local_fallback_lands_from_its_views)
{
    tide_local_init(&local);
    tide_session *s = tide_session_create(&(tide_session_desc){.game = &tide_game_api, .sample = sample_local});
    TIDE_REQUIRE(s != NULL);
    tide_session_start(s, NULL, 0);
    for (int i = 1; i <= 300; i++) {
        tide_session_update(s, i / 60.0);
        const tide_session_status status = tide_session_status_of(s);
        draw_world(tide_session_world(s), status.client.player);
        if (i == 60) {
            local.Pilot.drop = (tide_float3){1829.01f, 2364.03f, 257.05f};
            local.Pilot.moves++;
            local.Pilot.walking = true;
        }
        if (i >= 220) TIDE_CHECK(local.Pilot.blend == 1);
    }
    TIDE_CHECK(local.Pilot.had);
    TIDE_CHECK(local.Pilot.placed);
    TIDE_CHECK(local.Pilot.landed == local.Pilot.moves);
    TIDE_CHECK(local.Pilot.blend == 1);
    tide_session_destroy(s);
    tide_draw_free(&draw);
    tide_local_free(&local);
}

TIDE_TEST(earth_the_globe_can_turn_while_a_landing_is_pending)
{
    tide_local_init(&local);
    memset(&gui, 0, sizeof gui);
    memset(&devices, 0, sizeof devices);
    local.Pilot.walking = true;
    local.Pilot.moves = 1;
    local.Pilot.drop = (tide_float3){1, 0, 0};
    local.Pilot.pressing = true;
    local.Pilot.pressed = (tide_float2){200, 200};
    devices.pointer.position = (tide_float2){240, 240};
    devices.pointer.poll_delta = (tide_float2){40, 0};
    devices.pointer.press.pressed = true;
    devices.pointer.press.held = true;
    gui.last = devices; // The drag began in the previous frame.
    const float yaw = local.Pilot.yaw;
    draw_world(NULL, tide_player_from_index(0));
    TIDE_CHECK(local.Pilot.yaw != yaw);
    TIDE_CHECK(local.Pilot.walking && local.Pilot.moves == 1);
    tide_draw_free(&draw);
    tide_local_free(&local);
    memset(&devices, 0, sizeof devices);
}

TIDE_TEST(earth_a_disconnect_continues_locally_where_the_walker_was)
{
    tide_local_init(&local);
    memset(&gui, 0, sizeof gui);
    const tide_float3 spot = {0, 0, 3000};
    local.Pilot.walking = local.Pilot.placed = true;
    local.Pilot.spot = spot;
    local.Pilot.drop = (tide_float3){3000, 0, 0};
    local.Pilot.moves = local.Pilot.landed = 7;
    tide_local_disconnected(&local, TIDE_DISCONNECT_TIMED_OUT, "");
    draw_world(NULL, tide_player_from_index(0));
    tide_session_request request;
    tide_start start;
    TIDE_REQUIRE(tide_local_take_request(&local, &request, &start));
    TIDE_CHECK(request.kind == TIDE_REQUEST_START);
    tide_session *s = tide_session_create(&(tide_session_desc){.game = &tide_game_api, .sample = sample_local});
    TIDE_REQUIRE(s);
    tide_session_start(s, &start, 0);
    for (int i = 1; i <= 300; i++) {
        tide_session_update(s, i / 60.0);
        draw_world(tide_session_world(s), tide_session_status_of(s).client.player);
    }
    TIDE_CHECK(local.Pilot.placed && local.Pilot.blend == 1);
    const tide_float3 at = local.Pilot.spot;
    TIDE_CHECK(fabsf(at.x - spot.x) < 3 && fabsf(at.y - spot.y) < 3 && fabsf(at.z - spot.z) < 3);
    TIDE_CHECK(local.Pilot.moves == 1 && local.Pilot.landed == 1);
    tide_session_destroy(s);
    tide_draw_free(&draw);
    tide_local_free(&local);
}

// Every live entity of a world, in index order
#define EACH(w, e)                                                                                                    \
    for (uint32_t index_ = 0; index_ < (w)->entities.next_unused; index_++)                                           \
        for (tide_entity e = tide_entity_in_slot(&(w)->entities, index_); !tide_entity_is_null(e); e = (tide_entity){0})

static tide_world world;
static tide_entity walker_of(const tide_world *w, tide_player_id who);
static void ticks(int n);

static tide_player_id player(const int n)
{
    return tide_player_from_index(n);
}

// Straight up at a latitude and longitude, as city.tide has a town's middle
static tide_float3 up_at(const float lat, const float lon)
{
    const float a = lat * 3.14159265f / 180.0f, o = lon * 3.14159265f / 180.0f;
    return (tide_float3){cosf(a) * cosf(o), sinf(a), cosf(a) * sinf(o)};
}

static float length3(const tide_float3 v)
{
    return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

static tide_float3 minus(const tide_float3 a, const tide_float3 b)
{
    return (tide_float3){a.x - b.x, a.y - b.y, a.z - b.z};
}

static tide_float3 times(const tide_float3 a, const float k)
{
    return (tide_float3){a.x * k, a.y * k, a.z * k};
}

// The terrain can finish an opaque plaza without visiting any other town:
// no other town's lawn or paving can reach it. Check the actual atlas that
// the view hands the shader, so changing the layout cannot break that rule.
TIDE_TEST(earth_town_ground_regions_do_not_overlap)
{
    tide_local_init(&local);
    memset(&devices, 0, sizeof devices);
    memset(&gui, 0, sizeof gui);
    draw_world(NULL, player(0));
    TIDE_REQUIRE(local.Atlas.made);
    for (int a = 0; a < 10; a++) {
        for (int b = a + 1; b < 10; b++) {
            const float chord = length3(minus(local.Atlas.towns.at[a], local.Atlas.towns.at[b])) * PLANET;
            TIDE_CHECK(chord > 2 * (110 + 20));
        }
    }
    tide_draw_free(&draw);
    tide_local_free(&local);
}

static int tree_positions(tide_float3 out[512])
{
    tide_text_use(NULL, &local.heap);
    int count = 0;
    for (int i = 0; i < tide_list_count(local.Scenery.boxes) && count < 512; i++) {
        const Placed *p = tide_list_at(local.Scenery.boxes, i, sizeof *p);
        if (p->paint.r != 0.4f || p->paint.g != 0.3f || p->paint.b != 0.22f) continue;
        out[count++] = (tide_float3){p->transform.c3.x + local.Pilot.eye.x,
                                    p->transform.c3.y + local.Pilot.eye.y,
                                    p->transform.c3.z + local.Pilot.eye.z};
    }
    return count;
}

// Compare the actual drawn trunks in world coordinates as the eye moves a
// metre north, and across the longitude seam. Existing trees must stay put;
// only the few at the edge of the drawing radius may appear/disappear.
TIDE_TEST(earth_trees_stay_in_place_when_the_eye_moves)
{
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_player_joined(&world, player(0));
    ticks(1);
    const tide_entity me = walker_of(&world, player(0));
    tide_local_init(&local);
    memset(&devices, 0, sizeof devices);
    memset(&gui, 0, sizeof gui);
    for (int seam = 0; seam < 2; seam++) {
        tide_float3 before[512], after[512];
        int a = 0, b = 0;
        for (int step = 0; step < 2; step++) {
            Walker w = tide_get_Walker(&world, me);
            w.position = times(up_at(40.0f + step * 0.02f, seam ? 179.999f + step * 0.002f : 90), PLANET);
            w.forward = (tide_float3){0, 1, 0};
            w.placed = true;
            w.moves = 1;
            tide_set_Walker(&world, me, w);
            local.Pilot.walking = true;
            local.Pilot.moves = 1;
            local.Pilot.blend = 1;
            local.Pilot.arrived = 1;
            draw_world(&world, player(0));
            if (!step) a = tree_positions(before);
            else b = tree_positions(after);
        }
        int same = 0;
        for (int i = 0; i < a; i++) {
            for (int j = 0; j < b; j++) {
                if (length3(minus(before[i], after[j])) >= 0.003f) continue;
                same++;
                break;
            }
        }
        TIDE_CHECK(a > 10 && b > 10);
        TIDE_CHECK(same > a * 0.9f);
    }
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
}

// A player's walker: the one their PlayerJoined spawned, which no Person has
static tide_entity walker_of(const tide_world *w, const tide_player_id who)
{
    EACH(w, e)
    {
        if (tide_has_Walker(w, e) && !tide_has_Person(w, e) && !tide_has_tide_ghost(w, e) && tide_has_Owner(w, e)
            && tide_get_Owner(w, e).player.id == who.id)
            return e;
    }
    return (tide_entity){0};
}

static void ticks(const int n)
{
    for (int i = 0; i < n; i++) tide_world_tick(&world);
}

TIDE_TEST(earth_building_foundations_reach_below_the_curved_ground)
{
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_player_joined(&world, player(0));
    ticks(1);
    const tide_entity me = walker_of(&world, player(0));
    Walker w = tide_get_Walker(&world, me);
    w.position = times(up_at(52, 8), PLANET);
    w.placed = true;
    w.moves = 1;
    tide_set_Walker(&world, me, w);
    tide_local_init(&local);
    memset(&devices, 0, sizeof devices);
    memset(&gui, 0, sizeof gui);
    local.Pilot.walking = true;
    local.Pilot.moves = local.Pilot.arrived = 1;
    local.Pilot.blend = 1;
    draw_world(&world, player(0));
    tide_text_use(NULL, &local.heap);
    const int count = tide_list_count(local.Scenery.towers);
    TIDE_CHECK(count > 10);
    for (int i = 0; i < count; i++) {
        const Tower *tower = tide_list_at(local.Scenery.towers, i, sizeof *tower);
        const tide_float4x4 m = tower->transform;
        for (int x = -1; x <= 1; x += 2) for (int z = -1; z <= 1; z += 2) {
            const tide_float3 corner = {
                local.Pilot.eye.x + m.c3.x + (m.c0.x * x - m.c1.x + m.c2.x * z) * 0.5f,
                local.Pilot.eye.y + m.c3.y + (m.c0.y * x - m.c1.y + m.c2.y * z) * 0.5f,
                local.Pilot.eye.z + m.c3.z + (m.c0.z * x - m.c1.z + m.c2.z * z) * 0.5f};
            TIDE_CHECK(length3(corner) < PLANET - 0.04f);
        }
    }
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
}

static tide_transport measured_wire;
static unsigned measured_packets;
static void measured_send(void *self, tide_address to, const void *data, uint32_t size)
{
    (void)self;
    measured_packets++;
    measured_wire.send(measured_wire.self, to, data, size);
}
static uint32_t measured_receive(void *self, tide_address *from, void *data, uint32_t capacity)
{
    (void)self;
    return measured_wire.receive(measured_wire.self, from, data, capacity);
}
static void moving_sample(void *user, uint32_t tick, void *input)
{
    (void)user;
    (void)tick;
    *(Controls *)input = (Controls){.drop = {0, 0, PLANET}, .moves = 1, .move = {0, 1, 0}};
}
TIDE_TEST(earth_a_fast_moving_client_stays_connected_with_bounded_traffic)
{
    tide_loopback *wire = tide_loopback_create(123);
    tide_loopback_set_conditions(wire, (tide_net_conditions){.latency = 0.03, .jitter = 0.005});
    tide_server *server = tide_server_create(&(tide_server_desc){.game = &tide_game_api,
        .transports = {tide_loopback_endpoint(wire, 1)}, .tick_rate = 60}, 0);
    measured_wire = tide_loopback_endpoint(wire, 2);
    measured_packets = 0;
    tide_transport measured = {.send = measured_send, .receive = measured_receive};
    tide_client *client = tide_client_create(&(tide_client_desc){.game = &tide_game_api,
        .transport = measured, .server = tide_loopback_address(1), .sample = moving_sample, .lead = 2}, 0);
    TIDE_REQUIRE(server && client);
    unsigned last_server = 0;
    for (unsigned frame = 1; frame <= 240 * 65; frame++) {
        const double seconds = frame / 240.0;
        tide_loopback_set_time(wire, seconds);
        const unsigned server_tick = (unsigned)(seconds * 60);
        if (server_tick != last_server) {
            tide_server_update(server, seconds);
            last_server = server_tick;
        }
        tide_client_update(client, seconds);
        if (frame == 240 * 5) measured_packets = 0;
    }
    TIDE_CHECK(tide_client_status_of(client).state == TIDE_SESSION_CONNECTED);
    TIDE_CHECK(tide_client_status_of(client).verified_tick > 3800);
    TIDE_CHECK(measured_packets < 9000); // Input/ack news can exceed one packet a tick, but not follow the 240 Hz renderer.
    tide_client_destroy(client);
    tide_server_destroy(server);
    tide_loopback_destroy(wire);
}

// S is a brake regardless of where the orbit camera looks. Exercise the
// sampled key through the match, including the final stop without reversing.
TIDE_TEST(earth_driving_controls_follow_the_car_while_the_camera_turns)
{
    tide_local_init(&local);
    memset(&devices, 0, sizeof devices);
    local.Pilot.walking = local.Pilot.driving = true;
    local.Pilot.spot = (tide_float3){0, 0, PLANET};
    local.Pilot.facing = (tide_float3){1, 0, 0};
    const tide_float3 looks[3] = {{1,0,0},{0,1,0},{-1,0,0}};
    for (int angle = 0; angle < 3; angle++) {
        local.Pilot.ahead = looks[angle];
        local.Pilot.right = (tide_float3){-looks[angle].y,looks[angle].x,0};
        devices.keyboard.w.pressed = true;
        devices.keyboard.d.pressed = false;
        Controls input = tide_input_sample(&devices, &local);
        TIDE_CHECK(input.move.x == 1 && input.move.y == 0 && input.move.z == 0);
        devices.keyboard.d.pressed = true;
        input = tide_input_sample(&devices, &local);
        TIDE_CHECK(input.move.x == 1 && input.move.y == 1 && input.move.z == 0);
    }
    tide_local_free(&local);
    memset(&devices, 0, sizeof devices);
}

TIDE_TEST(earth_a_car_camera_can_turn_around_before_driving_forward)
{
    tide_world_init(&world, 1.0f / 60.0f);
    world.Roster.seeded = true;
    tide_world_player_joined(&world, player(0)); ticks(1);
    const tide_entity me = walker_of(&world, player(0));
    Walker w = tide_get_Walker(&world, me);
    w.position = (tide_float3){0, 0, PLANET};
    w.forward = (tide_float3){1, 0, 0};
    w.placed = w.driving = true;
    w.moves = 1;
    tide_set_Walker(&world, me, w);
    tide_local_init(&local);
    memset(&gui, 0, sizeof gui); memset(&devices, 0, sizeof devices);
    local.Pilot.walking = true;
    local.Pilot.moves = local.Pilot.arrived = 1;
    local.Pilot.blend = 1;
    local.Pilot.look = (tide_float3){-1, 0, 0}; // Walked backwards, then became a car.
    for (int frame = 0; frame < 180; frame++) draw_world(&world, player(0));
    TIDE_CHECK(local.Pilot.look.x > 0.99f);
    devices.keyboard.w.pressed = true;
    for (int frame = 0; frame < 120; frame++) {
        draw_world(&world, player(0));
        tide_world_set_input(&world, player(0), tide_input_sample(&devices, &local));
        ticks(1);
    }
    w = tide_get_Walker(&world, me);
    TIDE_CHECK(w.position.x > 20 && w.speed > 20);
    tide_draw_free(&draw); tide_local_free(&local); tide_world_free(&world);
    memset(&devices, 0, sizeof devices);
}

TIDE_TEST(earth_s_brakes_a_car_without_turning_it)
{
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_player_joined(&world, player(0));
    ticks(1);
    const tide_entity me = walker_of(&world, player(0));
    Walker w = tide_get_Walker(&world, me);
    w.position = (tide_float3){0, 0, PLANET};
    w.forward = (tide_float3){1, 0, 0};
    w.speed = 30;
    w.velocity = times(w.forward, w.speed);
    w.placed = w.driving = true;
    tide_set_Walker(&world, me, w);
    tide_local_init(&local);
    memset(&devices, 0, sizeof devices);
    local.Pilot.walking = local.Pilot.driving = true;
    local.Pilot.ahead = (tide_float3){0, 1, 0}; // Looking sideways at the car.
    devices.keyboard.s.pressed = true;
    const Controls input = tide_input_sample(&devices, &local);
    TIDE_CHECK(input.brake && length3(input.move) == 0);
    tide_world_set_input(&world, player(0), input);
    ticks(1);
    w = tide_get_Walker(&world, me);
    TIDE_CHECK(w.speed < 30 && w.speed > 29);
    TIDE_CHECK(fabsf(w.forward.y) < 0.00001f);
    ticks(120);
    w = tide_get_Walker(&world, me);
    TIDE_CHECK(w.speed == 0 && length3(w.velocity) == 0);
    local.Pilot.driving = false;
    const Controls walking = tide_input_sample(&devices, &local);
    TIDE_CHECK(!walking.brake && walking.move.y == -1);
    tide_local_free(&local);
    tide_world_free(&world);
    memset(&devices, 0, sizeof devices);
}

typedef struct city_trip { tide_float3 drop, forward; bool car; } city_trip;
static void leaving_city(void *user, uint32_t tick, void *input)
{
    const city_trip *trip = user;
    *(Controls *)input = (Controls){.drop = trip->drop, .moves = 1, .move = trip->forward, .car = trip->car && tick >= 240};
}

static float city_progress(const tide_world *w, tide_player_id who, const city_trip *trip)
{
    if (!w) return -1;
    const tide_entity avatar = walker_of(w, who);
    if (tide_entity_is_null(avatar)) return -1;
    const tide_float3 delta = minus(tide_get_Walker(w, avatar).position, trip->drop);
    return delta.x * trip->forward.x + delta.y * trip->forward.y + delta.z * trip->forward.z;
}

TIDE_TEST(earth_networked_players_can_leave_a_city)
{
    const tide_float3 centers[10] = {up_at(48,-75), up_at(12,-118), up_at(-28,-62), up_at(52,8), up_at(2,32),
        up_at(-38,48), up_at(38,92), up_at(6,138), up_at(-42,128), up_at(66,165)};
    const tide_float3 destinations[10] = {centers[1],centers[0],centers[0],centers[4],centers[3],
        centers[4],centers[7],centers[6],centers[7],centers[6]};
    for (int city = 0; city < 10; city++) for (int car = 0; car < 2; car++) {
        const tide_float3 up = centers[city], to = destinations[city];
        const float dot = up.x * to.x + up.y * to.y + up.z * to.z;
        tide_float3 forward = minus(to, times(up, dot));
        forward = times(forward, 1 / length3(forward));
        const tide_float3 projected = {up.x * PLANET + forward.x * 24,
            up.y * PLANET + forward.y * 24, up.z * PLANET + forward.z * 24};
        city_trip trip = {.drop = times(projected, PLANET / length3(projected)), .forward = forward, .car = car != 0};
        tide_loopback *wire = tide_loopback_create(123);
        tide_loopback_set_conditions(wire, (tide_net_conditions){.latency = 0.05, .jitter = 0.01, .loss = 0.01});
        tide_server *server = tide_server_create(&(tide_server_desc){.game = &tide_game_api,
            .transports = {tide_loopback_endpoint(wire, 1)}, .tick_rate = 60}, 0);
        tide_client *client = tide_client_create(&(tide_client_desc){.game = &tide_game_api,
            .transport = tide_loopback_endpoint(wire, 2), .server = tide_loopback_address(1),
            .sample = leaving_city, .user = &trip, .lead = 2}, 0);
        TIDE_REQUIRE(server && client);
        unsigned last_server = 0;
        for (unsigned frame = 1; frame <= 144 * 45; frame++) {
            const double seconds = frame / 144.0;
            tide_loopback_set_time(wire, seconds);
            const unsigned server_tick = (unsigned)(seconds * 60);
            if (server_tick != last_server) { tide_server_update(server, seconds); last_server = server_tick; }
            tide_client_update(client, seconds);
            if (frame % 144 == 0) {
                const tide_player_id who = tide_client_status_of(client).player;
                if (city_progress(tide_server_world(server), who, &trip) > 160
                    && city_progress(tide_client_view(client).current, who, &trip) > 160) break;
            }
        }
        const tide_client_status status = tide_client_status_of(client);
        TIDE_CHECK(status.state == TIDE_SESSION_CONNECTED && status.verified_tick > 300);
        const tide_world *worlds[2] = {tide_server_world(server), tide_client_view(client).current};
        for (int side = 0; side < 2; side++) {
            const float progress = city_progress(worlds[side], status.player, &trip);
            if (progress < 130) printf("network city %d car %d side %d progress %.3f\n", city, car, side, progress);
            TIDE_CHECK(progress > 130);
        }
        tide_client_destroy(client); tide_server_destroy(server); tide_loopback_destroy(wire);
    }
}

TIDE_TEST(earth_every_road_leads_out_of_its_city)
{
    static const float towns[10][2] = {{48,-75},{12,-118},{-28,-62},{52,8},{2,32},
        {-38,48},{38,92},{6,138},{-42,128},{66,165}};
    static const int roads[11][2] = {{0,1},{0,2},{0,3},{0,9},{1,2},{3,4},{4,5},{5,8},{6,7},{6,9},{7,8}};
    for (int road = 0; road < 11; road++) for (int end = 0; end < 2; end++) for (int car = 0; car < 2; car++) {
        const int town = roads[road][end], other = roads[road][1-end];
        const tide_float3 up = up_at(towns[town][0], towns[town][1]);
        const tide_float3 to = up_at(towns[other][0], towns[other][1]);
        const float dot = up.x * to.x + up.y * to.y + up.z * to.z;
        tide_float3 forward = minus(to, times(up, dot));
        forward = times(forward, 1 / length3(forward));
        const tide_float3 right = {up.y * forward.z - up.z * forward.y,
            up.z * forward.x - up.x * forward.z, up.x * forward.y - up.y * forward.x};
        const tide_float3 projected = {up.x * PLANET + forward.x * 24 + right.x * 2.5f,
            up.y * PLANET + forward.y * 24 + right.y * 2.5f,
            up.z * PLANET + forward.z * 24 + right.z * 2.5f};
        tide_world_init(&world, 1.0f / 60.0f);
        world.Roster.seeded = true; // Isolate the street/building layout from moving traffic.
        tide_world_player_joined(&world, player(0)); ticks(1);
        const tide_entity me = walker_of(&world, player(0));
        Walker w = tide_get_Walker(&world, me);
        w.position = times(projected, PLANET / length3(projected));
        w.forward = forward; w.placed = true; w.driving = car != 0;
        tide_set_Walker(&world, me, w);
        tide_world_set_input(&world, player(0), (Controls){.move = forward});
        float distance = 0;
        for (int tick = 0; tick < 2400 && distance < 160; tick++) {
            ticks(1);
            w = tide_get_Walker(&world, me);
            distance = length3(minus(w.position, times(up, PLANET)));
        }
        if (distance < 160) printf("road %d town %d car %d stopped at %.3f m, speed %.3f\n", road, town, car, distance, w.speed);
        TIDE_CHECK(distance >= 160);
        tide_world_free(&world);
    }
}

TIDE_TEST(earth_walkers_and_cars_can_leave_the_edge_of_a_town)
{
    const tide_float3 up = up_at(66, 165);
    const tide_float3 east = {up.z / sqrtf(up.x * up.x + up.z * up.z), 0,
                              -up.x / sqrtf(up.x * up.x + up.z * up.z)};
    const tide_float3 north = {-east.z * up.y, east.z * up.x - east.x * up.z, east.x * up.y};
    for (int car = 0; car < 2; car++) {
        tide_world_init(&world, 1.0f / 60.0f);
        tide_world_player_joined(&world, player(0)); ticks(1);
        const tide_entity me = walker_of(&world, player(0));
        const tide_float3 projected = {up.x * PLANET + north.x * 115,
            up.y * PLANET + north.y * 115, up.z * PLANET + north.z * 115};
        const tide_float3 position = times(projected, PLANET / length3(projected));
        const tide_float3 n = times(position, 1 / PLANET);
        const float dot = north.x * n.x + north.y * n.y + north.z * n.z;
        const tide_float3 tangent = {north.x - n.x * dot, north.y - n.y * dot, north.z - n.z * dot};
        const tide_float3 forward = times(tangent, 1 / length3(tangent));
        Walker w = tide_get_Walker(&world, me);
        w.position = position; w.forward = forward; w.placed = true; w.driving = car != 0;
        w.speed = car ? 10 : 0; w.velocity = times(forward, w.speed);
        tide_set_Walker(&world, me, w);
        tide_world_set_input(&world, player(0), (Controls){.move = forward});
        ticks(180);
        w = tide_get_Walker(&world, me);
        const tide_float3 moved = minus(w.position, position);
        TIDE_CHECK(moved.x * forward.x + moved.y * forward.y + moved.z * forward.z > (car ? 20 : 10));
        if (car) TIDE_CHECK(w.speed > 20);
        tide_world_free(&world);
    }
}

// One world, as a match that isn't split: the townsfolk and the traffic; a
// player who picks a town lands by its fountain, walks, can't go through the
// fountain, turns into a car and drives, stops a car by standing in front of
// it, and high-fives another who waves
TIDE_TEST(earth_walkers_walk_drive_and_high_five_and_traffic_stops_for_them)
{
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_player_joined(&world, player(0));
    tide_world_player_joined(&world, player(1));
    ticks(1);
    int people = 0, cars = 0;
    EACH(&world, e)
    {
        people += tide_has_Person(&world, e);
        cars += tide_has_Car(&world, e);
    }
    TIDE_CHECK(people == 10 * 6);
    TIDE_CHECK(cars == 11 * 2);
    TIDE_CHECK(world.Roster.online == 2);
    const tide_entity me = walker_of(&world, player(0)), you = walker_of(&world, player(1));
    TIDE_REQUIRE(!tide_entity_is_null(me) && !tide_entity_is_null(you));
    TIDE_CHECK(!tide_get_Walker(&world, me).placed);

    // Both to town 3: by its fountain, facing it
    const tide_float3 town = up_at(52, 8);
    const tide_float3 square = times(town, PLANET);
    tide_world_set_input(&world, player(0), (Controls){.drop = square, .moves = 1});
    tide_world_set_input(&world, player(1), (Controls){.drop = square, .moves = 1});
    ticks(2);
    Walker a = tide_get_Walker(&world, me);
    TIDE_CHECK(a.placed);
    TIDE_CHECK(length3(minus(a.position, square)) < 18.0f);
    TIDE_CHECK(length3(minus(a.position, square)) >= 4.0f + 0.45f - 0.05f); // Out of the fountain
    TIDE_CHECK(fabsf(length3(a.position) - PLANET) < 0.01f);

    // Walking ahead for a second: about 5 m, less the start, and still on the ground
    const tide_float3 was = a.position;
    tide_world_set_input(&world, player(0), (Controls){.drop = square, .moves = 1, .move = times(a.forward, -1)});
    ticks(60);
    a = tide_get_Walker(&world, me);
    const float walked = length3(minus(a.position, was));
    TIDE_CHECK(walked > 3.5f && walked < 5.5f);
    TIDE_CHECK(fabsf(length3(a.position) - PLANET) < 0.01f);

    // Into a car, and off: faster than walking
    tide_world_set_input(&world, player(0), (Controls){.drop = square, .moves = 1, .car = true});
    ticks(1);
    TIDE_CHECK(tide_get_Walker(&world, me).driving);
    // Out in the country, between two towns no road joins, heading east
    a = tide_get_Walker(&world, me);
    const tide_float3 between = up_at(60, 90);
    a.position = times(between, PLANET);
    a.forward = (tide_float3){-between.z / sqrtf(between.x * between.x + between.z * between.z), 0,
                              between.x / sqrtf(between.x * between.x + between.z * between.z)};
    tide_set_Walker(&world, me, a);
    tide_world_set_input(&world, player(0), (Controls){.drop = square, .moves = 1, .move = a.forward});
    ticks(180);
    a = tide_get_Walker(&world, me);
    TIDE_CHECK(a.speed > 15.0f);
    TIDE_CHECK(fabsf(length3(a.position) - PLANET) < 0.01f);
    // ...and out again
    tide_world_set_input(&world, player(0), (Controls){.drop = square, .moves = 1});
    ticks(1);
    tide_world_set_input(&world, player(0), (Controls){.drop = square, .moves = 1, .car = true});
    ticks(1);
    tide_world_set_input(&world, player(0), (Controls){.drop = square, .moves = 1});
    ticks(1);
    TIDE_CHECK(!tide_get_Walker(&world, me).driving);

    // In front of a townsperson's car on its road, standing still: it stops short
    tide_entity car = {0};
    EACH(&world, e)
    {
        if (tide_has_Car(&world, e) && tide_entity_is_null(car)) car = e;
    }
    TIDE_REQUIRE(!tide_entity_is_null(car));
    ticks(30); // Up to speed
    const Car c = tide_get_Car(&world, car);
    a = tide_get_Walker(&world, me);
    a.position = (tide_float3){c.position.x + c.forward.x * 7, c.position.y + c.forward.y * 7, c.position.z + c.forward.z * 7};
    a.velocity = (tide_float3){0, 0, 0};
    tide_set_Walker(&world, me, a);
    ticks(120);
    const Car stopped = tide_get_Car(&world, car);
    TIDE_CHECK(stopped.speed < 0.5f);
    TIDE_CHECK(length3(minus(tide_get_Walker(&world, me).position, stopped.position)) > 1.5f);
    // Out of the way, it goes on
    a = tide_get_Walker(&world, me);
    a.position = times(square, 1.0f);
    tide_set_Walker(&world, me, a);
    ticks(120);
    TIDE_CHECK(tide_get_Car(&world, car).speed > 5.0f);

    // Side by side by the fountain, both waving: a high five each
    a = tide_get_Walker(&world, me);
    Walker b = tide_get_Walker(&world, you);
    b.position = minus(a.position, times(a.forward, -1.2f));
    tide_set_Walker(&world, you, b);
    tide_world_set_input(&world, player(0), (Controls){.drop = square, .moves = 1, .wave = true});
    tide_world_set_input(&world, player(1), (Controls){.drop = square, .moves = 1, .wave = true});
    ticks(3);
    TIDE_CHECK(tide_get_Walker(&world, me).fives == 1);
    TIDE_CHECK(tide_get_Walker(&world, you).fives == 1);
    ticks(30); // Not again while it lasts
    TIDE_CHECK(tide_get_Walker(&world, me).fives == 1);

    tide_world_player_left(&world, player(1));
    ticks(2);
    TIDE_CHECK(world.Roster.online == 1);
    TIDE_CHECK(tide_entity_is_null(walker_of(&world, player(1))));
    tide_world_free(&world);
}

// What a player's machine is sent of the world, loaded, hashes as the server
// says it does, all players' at once or one at a time: for a player near
// another (who has their Walker) and one far off (who has their Beacon alone)
TIDE_TEST(earth_a_machine_hashes_its_world_as_the_server_does)
{
    tide_world_init(&world, 1.0f / 60.0f);
    for (int p = 0; p < 3; p++) tide_world_player_joined(&world, player(p));
    ticks(1);
    const tide_float3 town = times(up_at(52, 8), PLANET), far = times(up_at(6, 138), PLANET);
    tide_world_set_input(&world, player(0), (Controls){.drop = town, .moves = 1});
    tide_world_set_input(&world, player(1), (Controls){.drop = town, .moves = 1});
    tide_world_set_input(&world, player(2), (Controls){.drop = far, .moves = 1});
    ticks(61); // Landed, and looked at who's near
    const tide_player_id all[3] = {player(0), player(1), player(2)};
    uint64_t together[3];
    tide_game_api.client_hashes(&world, all, 3, together);
    for (int p = 0; p < 3; p++) {
        TIDE_CHECK(together[p] == tide_game_api.client_hash(&world, all[p]));
        uint32_t size = 0;
        uint8_t *bytes = tide_game_api.client_state(&world, all[p], &size);
        tide_world *machine = calloc(1, sizeof *machine);
        TIDE_REQUIRE(bytes && machine && tide_game_api.load_client_state(machine, bytes, size));
        TIDE_CHECK(tide_game_api.client_hash(machine, all[p]) == together[p]);
        // Players 0 and 1 have each other's Walker; player 2 has neither's
        TIDE_CHECK(tide_has_Walker(machine, walker_of(&world, player(p == 0 ? 1 : 0))) == (p != 2));
        tide_world_free(machine);
        free(machine);
        free(bytes);
    }
    TIDE_CHECK(together[0] != together[1] && together[1] != together[2]);
    // Whose inputs each may have, all at once, is what each one's filter says
    enum { SLOTS = TIDE_GAME_PLAYERS + 1 };
    static uint8_t allowed[3 * SLOTS];
    static Controls filtered[SLOTS];
    Controls none;
    tide_game_api.client_input_masks(&world, all, 3, allowed, &none);
    for (int p = 0; p < 3; p++) {
        for (uint32_t slot = 0; slot < SLOTS; slot++) tide_game_api.world_input(&world, slot, &filtered[slot]);
        tide_game_api.filter_client_inputs(&world, all[p], filtered);
        for (uint32_t slot = 0; slot < SLOTS; slot++) {
            Controls raw;
            tide_game_api.world_input(&world, slot, &raw);
            const Controls *expect = allowed[p * SLOTS + slot] ? &raw : &none;
            TIDE_CHECK(memcmp(&filtered[slot], expect, sizeof raw) == 0);
        }
    }
    TIDE_CHECK(allowed[0 * SLOTS + 1] && !allowed[2 * SLOTS + 0]); // Players 0 and 1 have each other's; 2 has neither's
    tide_world_free(&world);
}

// The planet in four regions along x, 1,600 m wide: a player looks at the
// globe a moment, then goes to town 3 (in the region furthest along +x), then
// to town 7 (furthest along -x), their machine following their walker from
// one shard to another; once they've gone for good, the root's PlayerLeft
// destroys it where it is. A shard that has the walker before its player's
// input guesses an input of zeros, which mustn't read as a request to go
// anywhere: until the walker took only newer requests, it went back and forth
// between two shards, and neither owned it. A second player, in town 4 on the
// same shard but 700 m off, is only a Beacon on the first one's machine until
// they come to town 3 too.
typedef struct visitor {
    tide_shard_client *client;
    uint32_t endpoints; // Its clients' loopback endpoints: this and the shard's number
    tide_float3 drop;
    int moves;
} visitor;

static tide_loopback *net;
static tide_shards *shards;
static tide_world replayed[5];

static void replay_record(void *user, uint32_t shard, uint32_t tick, const uint8_t *record, uint32_t size)
{
    (void)user;
    (void)tick;
    TIDE_REQUIRE(shard < 5);
    TIDE_CHECK(tide_replay(&tide_game_api, &replayed[shard], record, size, NULL));
}
static visitor first = {.endpoints = 100}, second = {.endpoints = 200};
static double now;

static void transports(void *user, const uint32_t shard, tide_transport out[2])
{
    (void)user;
    out[0] = tide_loopback_endpoint(net, 10u + shard);
}

static bool reach(void *user, const uint32_t shard, const uint64_t match, tide_transport *transport, tide_address *server)
{
    const visitor *v = user;
    (void)match;
    *transport = tide_loopback_endpoint(net, v->endpoints + shard);
    *server = tide_loopback_address(10u + shard);
    return true;
}

static void sample(void *user, const uint32_t tick, void *input)
{
    const visitor *v = user;
    (void)tick;
    *(Controls *)input = (Controls){.drop = v->drop, .moves = v->moves};
}

static void join(visitor *v)
{
    const tide_shard_client_desc pd = {
        .client = {.game = &tide_game_api, .transport = tide_loopback_endpoint(net, v->endpoints), .server = tide_loopback_address(10u),
                   .sample = sample, .user = v, .lead = 2},
        .reach = reach,
        .user = v,
    };
    v->client = tide_shard_client_create(&pd, now);
}

static const tide_world *home_world(const visitor *v)
{
    return tide_client_view(tide_shard_client_home(v->client)).current;
}

// The townsfolk the player's machine has of its home shard's world: only
// those near enough for it to see them (Seen, Visibility)
static int people_seen(void)
{
    const tide_world *w = home_world(&first);
    int count = 0;
    if (!w) return -1;
    EACH(w, e) count += tide_has_Person(w, e);
    return count;
}

// The players the first player's machine has, as Beacons, and of them those it has the Walker of
static void players_seen(int *beacons, int *walkers)
{
    const tide_world *w = home_world(&first);
    *beacons = *walkers = 0;
    if (!w) return;
    EACH(w, e)
    {
        if (!tide_has_Beacon(w, e) || tide_has_tide_ghost(w, e)) continue;
        *beacons += 1;
        *walkers += tide_has_Walker(w, e);
    }
}

// A player's walker, by its tag (the order they came in), as the shards that own it have it
static int owners(const int tag, Walker *found)
{
    int count = 0;
    for (uint32_t i = 0; i < tide_shards_count(shards); i++) {
        const tide_world *w = tide_shards_world(shards, i);
        if (!w) continue;
        EACH(w, e)
        {
            if (!tide_has_Walker(w, e) || tide_has_Person(w, e) || tide_has_tide_ghost(w, e)) continue;
            if (tide_get_Walker(w, e).tag != tag) continue;
            *found = tide_get_Walker(w, e);
            count++;
        }
    }
    return count;
}

static void run(const double seconds)
{
    const double until = now + seconds;
    while (now < until) {
        now += 1.0 / 60.0;
        tide_loopback_set_time(net, now);
        tide_shards_update(shards, now);
        if (first.client) tide_shard_client_update(first.client, now);
        if (second.client) tide_shard_client_update(second.client, now);
        Walker w;
        TIDE_CHECK(owners(0, &w) <= 1);
        TIDE_CHECK(owners(1, &w) <= 1);
    }
}

static tide_writer lifecycle_records;
static uint8_t *lifecycle_checkpoint;
static uint32_t lifecycle_checkpoint_size;
static unsigned lifecycle_saves;
static void lifecycle_record(void *user, uint32_t shard, uint32_t tick, const uint8_t *data, uint32_t size)
{
    (void)user; (void)tick;
    TIDE_REQUIRE(shard == 0);
    tide_write_u32(&lifecycle_records, size);
    tide_write_bytes(&lifecycle_records, data, size);
}
static void lifecycle_saved(void *user, const uint8_t *data, uint32_t size)
{
    (void)user;
    free(lifecycle_checkpoint);
    lifecycle_checkpoint = malloc(size);
    TIDE_REQUIRE(lifecycle_checkpoint);
    memcpy(lifecycle_checkpoint, data, size);
    lifecycle_checkpoint_size = size;
    lifecycle_saves++;
    lifecycle_records.size = 0;
}
static bool lifecycle_read(void *user, uint32_t shard, uint32_t index, const uint8_t **record, uint32_t *size)
{
    (void)user;
    if (shard != 0) return false;
    tide_reader r = {lifecycle_records.data, lifecycle_records.size, 0, false};
    for (uint32_t i = 0; i <= index && !r.failed && r.at < r.size; i++) {
        *size = tide_read_u32(&r);
        *record = tide_read_bytes(&r, *size);
        if (i == index) return *record != NULL;
    }
    return false;
}

TIDE_TEST(earth_recovery_does_not_repeat_a_departure_from_the_journal)
{
    now = 0;
    first = (visitor){.endpoints = 100}; second = (visitor){.endpoints = 200};
    lifecycle_records = (tide_writer){.grows = true};
    lifecycle_checkpoint = NULL; lifecycle_checkpoint_size = 0;
    net = tide_loopback_create(3);
    const tide_shards_desc desc = {.game = &tide_game_api, .tick_rate = 60,
        .world = {{-3000, -3000, -3000}, {3000, 3000, 3000}}, .split = {1, 1, 1},
        .transports = transports, .journal = lifecycle_record, .checkpoint = lifecycle_saved, .checkpoint_seconds = 3};
    shards = tide_shards_create(&desc, now); TIDE_REQUIRE(shards);
    join(&first); run(2); join(&second); run(2);
    const uint64_t cookie = tide_client_status_of(tide_shard_client_home(second.client)).cookie;
    const tide_player_id id = tide_client_status_of(tide_shard_client_home(second.client)).player;
    tide_shard_client_destroy(first.client); first.client = NULL;
    const double deadline = now + 22;
    while (((const tide_world *)tide_shards_world(shards, 0))->Roster.online == 2 && now < deadline) run(1.0 / 60.0);
    TIDE_REQUIRE(((const tide_world *)tide_shards_world(shards, 0))->Roster.online == 1);
    tide_shard_client_destroy(second.client); second.client = NULL;
    tide_shards_destroy(shards);
    TIDE_REQUIRE(lifecycle_checkpoint);
    shards = tide_shards_recover(&desc, lifecycle_checkpoint, lifecycle_checkpoint_size, lifecycle_read, NULL, now);
    TIDE_REQUIRE(shards);
    second.client = tide_shard_client_create(&(tide_shard_client_desc){
        .client = {.game = &tide_game_api, .transport = tide_loopback_endpoint(net, second.endpoints),
            .server = tide_loopback_address(10), .sample = sample, .user = &second, .lead = 2, .cookie = cookie}, .reach = reach, .user = &second}, now);
    TIDE_REQUIRE(second.client);
    run(25);
    const tide_world *w = tide_shards_world(shards, 0);
    TIDE_CHECK(w->Roster.online == 1);
    tide_text_use((tide_heap *)&w->heap, NULL);
    TIDE_CHECK(tide_map_count(w->Roster.walkers) == 1);
    TIDE_CHECK(tide_client_status_of(tide_shard_client_home(second.client)).player.id == id.id);
    tide_shard_client_destroy(second.client); second.client = NULL;
    tide_shards_destroy(shards); tide_loopback_destroy(net);
    free(lifecycle_checkpoint); lifecycle_checkpoint = NULL;
    free(lifecycle_records.data); lifecycle_records = (tide_writer){0};
}

static void check_join_recovery(const bool applied)
{
    now = 0; first = (visitor){.endpoints = 100}; second = (visitor){.endpoints = 200};
    lifecycle_records = (tide_writer){.grows = true};
    lifecycle_checkpoint = NULL; lifecycle_checkpoint_size = 0; lifecycle_saves = 0;
    net = tide_loopback_create(19);
    const tide_shards_desc desc = {.game = &tide_game_api, .tick_rate = 60,
        .world = {{-3000, -3000, -3000}, {3000, 3000, 3000}}, .split = {1, 1, 1},
        .transports = transports, .journal = lifecycle_record, .checkpoint = lifecycle_saved, .checkpoint_seconds = 3};
    shards = tide_shards_create(&desc, now); TIDE_REQUIRE(shards);
    run(2);
    const unsigned saved = lifecycle_saves;
    join(&first);
    const double deadline = now + 1;
    while (now < deadline && (applied
        ? ((const tide_world *)tide_shards_world(shards, 0))->Roster.online == 0
            || tide_client_status_of(tide_shard_client_home(first.client)).state != TIDE_SESSION_CONNECTED
        : lifecycle_saves == saved)) run(1.0 / 60.0);
    const tide_client_status before = tide_client_status_of(tide_shard_client_home(first.client));
    TIDE_REQUIRE(before.cookie != 0);
    TIDE_REQUIRE(lifecycle_saves > saved);
    TIDE_CHECK(((const tide_world *)tide_shards_world(shards, 0))->Roster.online == (applied ? 1 : 0));
    const tide_entity avatar = walker_of(tide_shards_world(shards, 0), before.player);
    TIDE_CHECK(tide_entity_is_null(avatar) != applied);
    tide_shard_client_destroy(first.client); first.client = NULL;
    tide_shards_destroy(shards);
    shards = tide_shards_recover(&desc, lifecycle_checkpoint, lifecycle_checkpoint_size, lifecycle_read, NULL, now);
    TIDE_REQUIRE(shards);
    first.client = tide_shard_client_create(&(tide_shard_client_desc){
        .client = {.game = &tide_game_api, .transport = tide_loopback_endpoint(net, first.endpoints),
            .server = tide_loopback_address(10), .sample = sample, .user = &first, .lead = 2, .cookie = before.cookie},
        .reach = reach, .user = &first}, now);
    TIDE_REQUIRE(first.client); run(3);
    const tide_world *w = tide_shards_world(shards, 0);
    const tide_client_status after = tide_client_status_of(tide_shard_client_home(first.client));
    const tide_entity restored = walker_of(w, before.player);
    TIDE_CHECK(w->Roster.online == 1 && w->Roster.next == 1);
    TIDE_CHECK(!tide_entity_is_null(restored));
    if (applied) TIDE_CHECK(restored.index == avatar.index && restored.generation == avatar.generation);
    TIDE_CHECK(after.state == TIDE_SESSION_CONNECTED && after.player.id == before.player.id && after.cookie == before.cookie);
    tide_shard_client_destroy(first.client); first.client = NULL;
    if (applied) {
        TIDE_REQUIRE(tide_shards_restart(shards, 0, lifecycle_checkpoint, lifecycle_checkpoint_size, lifecycle_read, NULL, now));
        first.client = tide_shard_client_create(&(tide_shard_client_desc){
            .client = {.game = &tide_game_api, .transport = tide_loopback_endpoint(net, first.endpoints),
                .server = tide_loopback_address(10), .sample = sample, .user = &first, .lead = 2, .cookie = before.cookie},
            .reach = reach, .user = &first}, now);
        TIDE_REQUIRE(first.client); run(3);
        const tide_world *root = tide_shards_world(shards, 0);
        TIDE_CHECK(root->Roster.online == 1 && root->Roster.next == 1);
        TIDE_CHECK(tide_client_status_of(tide_shard_client_home(first.client)).cookie == before.cookie);
        tide_shard_client_destroy(first.client); first.client = NULL;
    }
    tide_shards_destroy(shards); tide_loopback_destroy(net);
    free(lifecycle_checkpoint); lifecycle_checkpoint = NULL;
    free(lifecycle_records.data); lifecycle_records = (tide_writer){0};
}

TIDE_TEST(earth_a_join_is_recoverable_with_its_original_cookie_and_avatar) { check_join_recovery(true); }
TIDE_TEST(earth_a_saved_join_survives_a_crash_before_its_first_game_tick) { check_join_recovery(false); }

TIDE_TEST(earth_a_shared_world_counts_players_through_leaves_and_rejoins)
{
    now = 0;
    first = (visitor){.endpoints = 100};
    second = (visitor){.endpoints = 200};
    net = tide_loopback_create(3);
    shards = tide_shards_create(&(tide_shards_desc){.game = &tide_game_api, .tick_rate = 60,
        .world = {{-3000, -3000, -3000}, {3000, 3000, 3000}}, .split = {1, 1, 1}, .transports = transports}, now);
    TIDE_REQUIRE(shards);
    join(&first);
    run(2);
    TIDE_CHECK(((const tide_world *)tide_shards_world(shards, 0))->Roster.online == 1);
    join(&second);
    run(2);
    TIDE_CHECK(((const tide_world *)tide_shards_world(shards, 0))->Roster.online == 2);
    tide_shards_player(shards, player(0), true); // A repeated lifecycle notification is not another arrival.
    run(1);
    TIDE_CHECK(((const tide_world *)tide_shards_world(shards, 0))->Roster.online == 2);
    tide_shard_client_destroy(first.client); first.client = NULL;
    run(25);
    TIDE_CHECK(((const tide_world *)tide_shards_world(shards, 0))->Roster.online == 1);
    tide_shards_player(shards, player(0), false);
    run(1);
    TIDE_CHECK(((const tide_world *)tide_shards_world(shards, 0))->Roster.online == 1);
    tide_shard_client_destroy(second.client); second.client = NULL;
    run(25);
    TIDE_CHECK(((const tide_world *)tide_shards_world(shards, 0))->Roster.online == 0);
    join(&first);
    run(2);
    TIDE_CHECK(((const tide_world *)tide_shards_world(shards, 0))->Roster.online == 1);
    tide_shard_client_destroy(first.client); first.client = NULL;
    tide_shards_destroy(shards);
    tide_loopback_destroy(net);
}

TIDE_TEST(earth_a_player_visits_towns_on_two_shards_and_leaves)
{
    now = 0;
    net = tide_loopback_create(3);
    const double half = 2.0 * 1600.0;
    const tide_shards_desc d = {.game = &tide_game_api, .tick_rate = 60, .world = {{-half, -half, -half}, {half, half, half}},
                                .split = {4, 1, 1}, .transports = transports, .journal = replay_record};
    shards = tide_shards_create(&d, now);
    TIDE_REQUIRE(shards != NULL);
    for (uint32_t i = 0; i < tide_shards_count(shards); i++)
        tide_world_copy(&replayed[i], tide_shards_world(shards, i));
    first.moves = second.moves = 0;
    join(&first);
    TIDE_REQUIRE(first.client != NULL);
    run(2.0);
    join(&second);
    TIDE_REQUIRE(second.client != NULL);
    run(2.0);
    first.drop = times(up_at(52, 8), PLANET);
    first.moves = 1;
    second.drop = times(up_at(2, 32), PLANET);
    second.moves = 1;
    run(3.0);
    Walker w, w2;
    TIDE_CHECK(owners(0, &w) == 1);
    TIDE_CHECK(w.placed && w.position.x > 1600.0f);
    TIDE_CHECK(owners(1, &w2) == 1);
    TIDE_CHECK(w2.placed && w2.position.x > 1600.0f && length3(minus(w.position, w2.position)) > 600.0f);
    const uint32_t home = tide_shard_client_home_shard(first.client);
    TIDE_CHECK(tide_shard_client_home_shard(second.client) == home);
    // Every townsperson and car is some shard's, once
    int people = 0, cars = 0;
    for (uint32_t i = 0; i < tide_shards_count(shards); i++) {
        const tide_world *s = tide_shards_world(shards, i);
        if (!s) continue;
        EACH(s, e)
        {
            if (tide_has_tide_ghost(s, e)) continue;
            people += tide_has_Person(s, e);
            cars += tide_has_Car(s, e);
        }
    }
    TIDE_CHECK(people == 10 * 6);
    TIDE_CHECK(cars == 11 * 2);
    // ...but the player's machine has only the townsfolk of the town it's in: the rest are past its horizon
    TIDE_CHECK(people_seen() == 6);
    // ...and of the other player, past its horizon, where they are and not their Walker
    int beacons, walkers;
    players_seen(&beacons, &walkers);
    TIDE_CHECK(beacons == 2 && walkers == 1);

    // The other player comes to town 3: now they're near, their Walker too
    second.drop = first.drop;
    second.moves = 2;
    run(3.0);
    players_seen(&beacons, &walkers);
    TIDE_CHECK(beacons == 2 && walkers == 2);

    first.drop = times(up_at(6, 138), PLANET);
    first.moves = 2;
    run(3.0);
    TIDE_CHECK(owners(0, &w) == 1);
    TIDE_CHECK(w.placed && w.position.x < -1600.0f);
    TIDE_CHECK(tide_shard_client_home_shard(first.client) != home);
    const tide_client_status status = tide_client_status_of(tide_shard_client_home(first.client));
    TIDE_CHECK(status.state == TIDE_SESSION_CONNECTED && status.resyncs == 0);
    TIDE_CHECK(people_seen() == 6); // The new town's, on the new shard

    tide_shard_client_destroy(first.client);
    first.client = NULL;
    tide_shard_client_destroy(second.client);
    second.client = NULL;
    run(25.0);
    TIDE_CHECK(owners(0, &w) == 0);
    TIDE_CHECK(owners(1, &w) == 0);

    for (uint32_t i = 0; i < tide_shards_count(shards); i++) {
        TIDE_CHECK(tide_world_hash(&replayed[i]) == tide_world_hash(tide_shards_world(shards, i)));
        tide_world_free(&replayed[i]);
    }

    tide_shards_destroy(shards);
    tide_loopback_destroy(net);
}
