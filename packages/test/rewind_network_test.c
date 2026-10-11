#include <math.h>
#include <stdio.h>
#include <string.h>

#include "rewind_network.h"
#include "tide_test.h"

// The same harness is compiled against each package's generated game. Nothing
// fabricates a ViewTime: actual Tide views draw, the host marks presentation,
// Sample produces input, and normal packing/transport/server validation run.
enum { SHOTS_MOST = 128 };
typedef struct expected_shot {
    tide_view_time time;
    float x;
    double drawn_at;
    double sampled_at;
    bool hit;
    bool received;
} expected_shot;

typedef struct player {
    tide_client *client;
    tide_local local;
    tide_draw_list draw;
    tide_gui gui;
    expected_shot shots[SHOTS_MOST];
    tide_view_time shown;
    float shown_x;
    double shown_at;
    double now;
    double next_update;
    double next_draw;
    double next_shot;
    int sampled;
    int frames;
    int fractional;
    int accepted;
    int rejected;
    int misses;
    int differs_from_current;
    double max_age;
    float max_error;
} player;

typedef struct scenario {
    double fps;
    double render_fps; // 0: draw at every update
    uint32_t tick_rate;
    tide_net_conditions network;
    bool variable_frames;
    bool update_stall;
    bool render_stall;
    bool expect_expired;
    bool boundary;
} scenario;

static void sample(void *user, uint32_t tick, void *input)
{
    (void)tick;
    player *p = user;
    Controls *controls = input;
    *controls = (Controls){.direction = (int)p->now % 2 == 0 ? 1 : -1};
    if (!p->shown.valid || p->now < p->next_shot || p->now > 4.5) return;
    if (p->sampled + 1 >= SHOTS_MOST) { TIDE_CHECK(false); return; }
    controls->shot = ++p->sampled;
    controls->aimX = p->shown_x;
    controls->aimY = controls->shot % 3 == 0 ? 0.3f : 0;
    p->shots[controls->shot] = (expected_shot){.time = p->shown, .x = p->shown_x,
        .drawn_at = p->shown_at, .sampled_at = p->now, .hit = controls->aimY == 0};
    p->next_shot = p->now + 0.1;
}

static void present(player *p)
{
    const tide_view_worlds v = tide_client_view(p->client);
    const uint64_t before = v.verified ? tide_world_hash(v.verified) : 0;
    p->local.Display_local.ready = false;
    tide_draw_reset(&p->draw);
    tide_frame(v.current, v.previous, v.alpha, v.verified, v.verified_previous,
               v.verified_alpha, &p->local, &p->draw, &p->gui);
    if (!p->local.Display_local.ready) return;
    TIDE_CHECK(v.verified != NULL && tide_world_hash(v.verified) == before);
    p->shown = (tide_view_time){(int32_t)v.verified_tick, v.verified_alpha, true};
    p->shown_x = p->local.Display_local.x;
    p->shown_at = p->now;
    p->frames++;
    p->fractional += v.verified_alpha > 0 && v.verified_alpha < 1;
    // The oracle is the draw command's actual position, not a reconstruction
    // from velocity or from the latest confirmed/predicted world.
    int rectangles = 0;
    for (uint32_t i = 0; i < p->draw.count; i++) {
        if (p->draw.commands[i].kind != TIDE_DRAW_RECT) continue;
        TIDE_CHECK(p->draw.commands[i].a.x == p->shown_x);
        rectangles++;
    }
    TIDE_CHECK(rectangles == 1);
    tide_client_mark_presented(p->client);
}

static void check_results(const tide_world *world, player players[2], const scenario s)
{
    tide_text_use((tide_heap *)&world->heap, NULL);
    const int count = tide_list_count(world->Shots.items);
    for (int i = 0; i < count; i++) {
        const Shot shot = *(const Shot *)tide_list_at(world->Shots.items, i, sizeof(Shot));
        int who = -1;
        for (int j = 0; j < 2; j++)
            if (tide_player_equal(tide_client_status_of(players[j].client).player, shot.player)) who = j;
        TIDE_REQUIRE(who >= 0 && who < 2);
        player *p = &players[who];
        TIDE_REQUIRE(shot.id > 0 && shot.id <= p->sampled);
        expected_shot *expected = &p->shots[shot.id];
        TIDE_CHECK(!expected->received);
        expected->received = true;
        const double at = (double)expected->time.tick - 1 + (double)expected->time.alpha;
        const double age = ((double)shot.tick - at) / (double)s.tick_rate;
        TIDE_CHECK(age >= 0 && expected->sampled_at >= expected->drawn_at);
        if (age > p->max_age) p->max_age = age;
        // Range validation uses the server execution tick, so client FPS,
        // input lead and both network directions all contribute to this age.
        if (age > (double)tide_game_api.max_rewind + 1e-6) {
            TIDE_CHECK(!shot.time.valid && !shot.prepared && !shot.hit);
        } else if (age < (double)tide_game_api.max_rewind - 1e-6) {
            TIDE_CHECK(shot.time.valid && shot.prepared);
        }
        if (!shot.time.valid) {
            TIDE_CHECK(shot.time.tick == 0 && shot.time.alpha == 0 && !shot.prepared && !shot.hit);
            p->rejected++;
            continue;
        }
        // Stamping must preserve the frame Sample saw, even when the client's
        // next update has advanced its view or several ticks sample in a frame.
        TIDE_CHECK(shot.time.tick == expected->time.tick && shot.time.alpha == expected->time.alpha);
        TIDE_CHECK(shot.prepared && shot.hit == expected->hit);
        p->accepted++;
        if (!expected->hit) { p->misses++; continue; }
        const float error = fabsf(shot.x - (expected->x - 0.1f));
        if (error > p->max_error) p->max_error = error;
        TIDE_CHECK(error < 0.0001f && fabsf(shot.fraction - 0.45f) < 0.0001f);
        p->differs_from_current += !shot.currentHit || fabsf(shot.currentFraction - shot.fraction) > 0.001f;
    }
}

static void run_scenario(const scenario s, const uint64_t seed)
{
    tide_loopback *net = tide_loopback_create(seed);
    tide_loopback_set_conditions(net, s.network);
    const tide_server_desc sd = {.game = &tide_game_api, .tick_rate = s.tick_rate,
        .transports = {tide_loopback_endpoint(net, 1)}};
    tide_server *server = tide_server_create(&sd, 0);
    player players[2] = {0};
    // The second machine has a different rate in every profile.
    const double fps[2] = {s.fps, s.fps >= 60 ? 30 : 240};
    for (int i = 0; i < 2; i++) {
        players[i].next_shot = 1.5;
        tide_local_init(&players[i].local);
        const tide_client_desc cd = {.game = &tide_game_api,
            .transport = tide_loopback_endpoint(net, (uint32_t)i + 2),
            .server = tide_loopback_address(1), .sample = sample, .user = &players[i],
            .lead = 2, .lead_frame = true};
        players[i].client = tide_client_create(&cd, 0);
    }
    double server_at = 0;
    uint32_t server_updates = 0;
    int updates[2] = {0};
    for (;;) {
        double now = server_at;
        for (int i = 0; i < 2; i++) if (players[i].next_update < now) now = players[i].next_update;
        if (now > 6) break;
        tide_loopback_set_time(net, now);
        if (server_at <= now + 1e-9) {
            tide_server_update(server, now);
            server_at = (double)++server_updates / (double)s.tick_rate;
        }
        for (int i = 0; i < 2; i++) {
            player *p = &players[i];
            if (p->next_update > now + 1e-9) continue;
            p->now = now;
            tide_client_update(p->client, now);
            const bool skip_draw = i == 0 && s.render_stall && now >= 2 && now < 2.6;
            if (!skip_draw && now >= p->next_draw) {
                present(p);
                p->next_draw = now + (i == 0 && s.render_fps > 0 ? 1 / s.render_fps : 0);
            }
            const double scale = i == 0 && s.variable_frames ? (updates[i] % 3 == 0 ? 0.5 : 1.25) : 1;
            updates[i]++;
            p->next_update = now + scale / fps[i];
            if (i == 0 && s.update_stall && now < 2 && p->next_update >= 2) p->next_update += 0.6;
        }
    }
    check_results(tide_server_world(server), players, s);
    for (int i = 0; i < 2; i++) {
        player *p = &players[i];
        const tide_client_status status = tide_client_status_of(p->client);
        TIDE_CHECK(status.state == TIDE_SESSION_CONNECTED && status.resyncs == 0);
        TIDE_CHECK(p->sampled >= 15 && p->accepted + p->rejected >= 12 && p->frames > 30);
        if (s.expect_expired) TIDE_CHECK(p->rejected >= 10 && p->accepted == 0);
        else if (s.boundary) TIDE_CHECK(p->rejected > 0);
        else if (s.network.loss == 0)
            TIDE_CHECK(p->accepted >= 8 && p->misses >= 3 && p->differs_from_current >= 4);
        if (i == 0 && s.render_stall) TIDE_CHECK(p->rejected > 0);
        printf("    %u Hz, %.0f FPS%s, %.0f ms/way + %.0f jitter, %.0f%% loss: %d valid (%d misses), %d expired, "
               "max age %.1f ms, error %.8f\n", s.tick_rate, fps[i],
               i == 0 && s.render_fps ? " (separate rendering)" : "", s.network.latency * 1000,
               s.network.jitter * 1000, s.network.loss * 100, p->accepted, p->misses, p->rejected,
               p->max_age * 1000, (double)p->max_error);
        tide_client_destroy(p->client);
        tide_local_free(&p->local);
        tide_draw_free(&p->draw);
    }
    if (!s.expect_expired && !s.boundary) {
        // Loss/lead can make all of the slower machine's shots too old for
        // this intentionally short history. The per-shot age oracle above
        // still requires every in-window shot to work; each profile must also
        // exercise valid hits/misses rather than pass through rejection alone.
        TIDE_CHECK(players[0].accepted + players[1].accepted >= 8);
        TIDE_CHECK(players[0].misses + players[1].misses >= 3);
        TIDE_CHECK(players[0].differs_from_current + players[1].differs_from_current >= 4);
    }
    tide_server_destroy(server);
    tide_loopback_destroy(net);
}

TIDE_TEST(rewind_network_matches_the_displayed_frame_across_fps_and_latency)
{
    const double fps[] = {20, 30, 60, 144, 240};
    const tide_net_conditions networks[] = {{0}, {.latency = 0.02}, {.latency = 0.05},
        {.latency = 0.02, .jitter = 0.01, .loss = 0.1}};
    for (int f = 0; f < 5; f++)
        for (int n = 0; n < 4; n++)
            run_scenario((scenario){.fps = fps[f], .tick_rate = 60, .network = networks[n]},
                         (uint64_t)(31 + f * 4 + n));
}

TIDE_TEST(rewind_network_tracks_variable_frames_stalls_and_independent_rendering)
{
    const tide_net_conditions network = {.latency = 0.04, .jitter = 0.015, .loss = 0.05};
    run_scenario((scenario){.fps = 144, .tick_rate = 30, .network = network, .variable_frames = true}, 71);
    run_scenario((scenario){.fps = 30, .tick_rate = 120, .network = network, .variable_frames = true}, 72);
    run_scenario((scenario){.fps = 60, .tick_rate = 60, .network = network, .update_stall = true}, 73);
    run_scenario((scenario){.fps = 240, .render_fps = 20, .tick_rate = 60, .network = network}, 74);
    run_scenario((scenario){.fps = 120, .tick_rate = 60, .network = network, .render_stall = true}, 75);
}

TIDE_TEST(rewind_network_rejects_shots_older_than_the_history_window)
{
    // Around the window boundary, every individual shot is checked against
    // its actual execution age rather than expecting every FPS to accept it.
    run_scenario((scenario){.fps = 20, .tick_rate = 60,
        .network = {.latency = 0.08, .jitter = 0.03, .loss = 0.1}, .boundary = true}, 34);
    const tide_net_conditions network = {.latency = 0.18, .jitter = 0.02, .loss = 0.05};
    run_scenario((scenario){.fps = 30, .tick_rate = 60, .network = network, .expect_expired = true}, 81);
    run_scenario((scenario){.fps = 240, .tick_rate = 60, .network = network, .expect_expired = true}, 82);
}
