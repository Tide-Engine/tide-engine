// Sumo's native input delay, with a real clock and three sessions on a
// loopback network. Frames run at the requested rate unless their work takes
// longer. Views record their draw lists; no window or GPU is opened. The
// machines run in turn on one four-thread pool, so their frame intervals
// include the other machines' work. See lag_native.mjs for saved summaries.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/platform.h"
#include "tide/time.h"

void tide_lag_sleep(double seconds);

typedef struct machine {
    tide_session *session;
    tide_local *local;
    tide_gui gui;
    tide_draw_list draw;
    tide_devices devices;
    Controls input;
    uint32_t random;
    uint32_t corrections, resets;
    double connected, turn_at, charge_at, charge_until, previous;
    bool playing;
} machine;

static machine machines[3];

static double roll(machine *m)
{
    m->random = m->random * 1664525u + 1013904223u;
    return (double)m->random / 4294967296.0;
}

static void hands(machine *m, const double now)
{
    static const float ways[9][2] = {{0, 1}, {0.70710677f, 0.70710677f}, {1, 0}, {0.70710677f, -0.70710677f},
        {0, -1}, {-0.70710677f, -0.70710677f}, {-1, 0}, {-0.70710677f, 0.70710677f}, {0, 0}};
    if (!m->playing) return;
    const double elapsed = now - m->connected;
    if (elapsed >= m->turn_at) {
        const uint32_t way = (uint32_t)(roll(m) * 9.0);
        m->input.move = tide_f2(ways[way][0], ways[way][1]);
        m->turn_at = elapsed + 0.3 + roll(m) * 0.9;
    }
    if (elapsed >= m->charge_at) {
        m->charge_until = elapsed + 0.4 + roll(m) * 0.5;
        m->charge_at = elapsed + 1.5 + roll(m) * 3.0;
    }
    m->input.charge = elapsed < m->charge_until;
}

static void sample(void *user, const uint32_t tick, void *input)
{
    (void)tick;
    const machine *m = user;
    memcpy(input, &m->input, sizeof m->input);
}

static void correct(void *user, const tide_view_correction *correction)
{
    machine *m = user;
    if (correction->reset) m->resets++;
    else m->corrections++;
    tide_local_correct(m->local, correction);
}

// Milliseconds a frame a client may spend running predicted ticks again
// before it takes input delay (--budget); 0 is the engine's own budget.
static double budget_ms;

static bool create(const int who, const tide_jobs *jobs)
{
    machine *m = &machines[who];
    m->random = (uint32_t)(who + 1) * 11u;
    m->local = calloc(1, sizeof *m->local);
    if (!m->local) return false;
    tide_local_init(m->local);
    m->session = tide_session_create(&(tide_session_desc){.game = &tide_game_api, .sample = sample, .user = m,
        .jobs = jobs, .now_ns = tide_time_now_ns, .correct_views = correct, .resim_budget = budget_ms * 1e-3});
    return m->session != NULL;
}

static void frame(FILE *raw, const int who, const uint64_t origin, tide_loopback *net)
{
    machine *m = &machines[who];
    const uint64_t began = tide_time_now_ns();
    const double now = (double)(began - origin) * 1e-9;
    const float seconds = (float)(now - m->previous);
    m->previous = now;
    tide_loopback_set_time(net, now);
    hands(m, now);
    tide_local_frame_time(m->local, seconds);
    tide_event_log *heard = tide_session_events(m->session);
    const uint64_t updating = tide_time_now_ns();
    tide_session_update(m->session, now);
    const double update_ms = (double)(tide_time_now_ns() - updating) * 1e-6;
    const tide_session_status status = tide_session_status_of(m->session);
    tide_session_event event;
    while (tide_session_next_event(m->session, &event)) {
        if (event.kind == TIDE_SESSION_CONNECTED_EVENT) tide_local_connected(m->local);
        else tide_local_disconnected(m->local, event.reason, event.message);
    }
    const tide_client_status *c = &status.client;
    if (!m->playing && c->state == TIDE_SESSION_CONNECTED) {
        m->playing = true;
        m->connected = now;
        m->charge_at = 1.0 + roll(m) * 2.0;
    }
    tide_local_set_session(m->local, c->state, c->player, c->ping_ms, status.server, status.open, "");
    const tide_view_worlds view = tide_session_view(m->session);
    tide_draw_reset(&m->draw);
    // Same logical canvas as lag.mjs. Measuring text needs no context.
    tide_gui_begin(&m->gui, &m->devices, tide_f2(960, 600), tide_platform_measure_text);
    tide_local_events(m->local, heard);
    tide_frame(view.current, view.previous, view.alpha, view.verified, view.verified_previous, view.verified_alpha, m->local, &m->draw, &m->gui);
    tide_session_mark_presented(m->session);
    tide_gui_end(&m->gui, &m->draw);
    const double work_ms = (double)(tide_time_now_ns() - began) * 1e-6;
    // Same frame layout as lag.mjs: time, then tide_run_netcode's values.
    fprintf(raw, "[%d,[%.3f,%u,%d,%u,%u,%u,%u,%u,%u,%.3f,%u,%u,%u,%u,%u,%u,%u,%.6f,%.6f,%.3f,%.3f,%.3f]]\n", who,
        now * 1000.0, (unsigned)c->state, status.server ? 1 : 0, c->ping_ms, c->answer_ms, c->verified_tick,
        c->predicted_tick, c->server_tick, c->delay_ticks, (double)c->early_ms, c->late_ticks, c->again_ticks,
        c->run_ticks, c->ahead_ticks, c->resyncs, m->corrections, m->resets, update_ms, work_ms, (double)c->rollback_ms,
        (double)c->rollback_most_ms, (double)c->resim_ms);
}

int main(const int argc, char **argv)
{
    double seconds = 150, join_at = 30, fps = 144, ping = 50;
    uint32_t frames = 0;
    const char *path = NULL;
    for (int i = 1; i < argc; i++) {
        if (i + 1 >= argc) return 2;
        const char *option = argv[i++];
        if (!strcmp(option, "--seconds")) seconds = strtod(argv[i], NULL);
        else if (!strcmp(option, "--join")) join_at = strtod(argv[i], NULL);
        else if (!strcmp(option, "--fps")) fps = strtod(argv[i], NULL);
        else if (!strcmp(option, "--ping")) ping = strtod(argv[i], NULL);
        else if (!strcmp(option, "--frames")) frames = (uint32_t)strtoul(argv[i], NULL, 10);
        else if (!strcmp(option, "--budget")) budget_ms = strtod(argv[i], NULL);
        else if (!strcmp(option, "--raw")) path = argv[i];
        else return 2;
    }
    if (!path || !(seconds > 0 && seconds <= 3600) || !(join_at >= 0 && join_at < seconds)
        || !(fps >= 1 && fps <= 1000) || !(ping >= 0 && ping <= 2000) || !(budget_ms >= 0 && budget_ms <= 1000)) {
        fprintf(stderr, "usage: bench_lag_native --raw <file.jsonl> [--ping 50] [--seconds 150] [--join 30] [--fps 144] [--frames n]"
                        " [--budget ms]\n");
        return 2;
    }
    FILE *raw = fopen(path, "wb");
    if (!raw) return 1;
    setvbuf(raw, NULL, _IOFBF, 1u << 20);
    tide_loopback *net = tide_loopback_create(9103);
    if (!net) { fclose(raw); return 1; }
    tide_loopback_set_conditions(net, (tide_net_conditions){.latency = ping * 0.0005});
    const tide_jobs *jobs = tide_platform_jobs(); // This target caps its pool at four.
    int made = 0;
    int failed = 0;
    for (; made < 2; made++) if (!create(made, jobs)) { failed = 1; break; }
    uint64_t origin = tide_time_now_ns();
    if (!failed) {
        tide_session_start(machines[0].session, NULL, 0);
        if (!tide_session_open(machines[0].session, tide_loopback_endpoint(net, 1))) failed = 1;
        tide_session_join(machines[1].session, tide_loopback_endpoint(net, 2), tide_loopback_address(1), 0);
    }
    uint32_t ran_frames = 0;
    int rounds = 0;
    double elapsed = 0;
    while (!failed && elapsed < seconds && (!frames || ran_frames < frames)) {
        const uint64_t began = tide_time_now_ns();
        elapsed = (double)(began - origin) * 1e-9;
        if (made == 2 && elapsed >= join_at) {
            if (!create(2, jobs)) { failed = 1; break; }
            made = 3;
            tide_session_join(machines[2].session, tide_loopback_endpoint(net, 3), tide_loopback_address(1), elapsed);
        }
        for (int who = 0; who < made; who++) frame(raw, who, origin, net);
        const tide_world *world = tide_session_server_world(machines[0].session);
        if (world && world->Ring.round > rounds) rounds = world->Ring.round;
        ran_frames++;
        const double work = (double)(tide_time_now_ns() - began) * 1e-9;
        if (work < 1.0 / fps) tide_lag_sleep(1.0 / fps - work);
    }
    const double took = (double)(tide_time_now_ns() - origin) * 1e-9;
    const tide_session_status host = machines[0].session ? tide_session_status_of(machines[0].session) : (tide_session_status){0};
    for (int who = 0; who < made; who++) if (!machines[who].playing) failed = 1;
    fprintf(raw, "{\"meta\":{\"platform\":\"native\",\"seconds\":%.3f,\"ping\":%.0f,\"join_at\":%.3f,\"fps\":%.0f,"
        "\"threads\":%u,\"resim_budget_ms\":%.3f,\"rounds_started\":%d,\"full_round\":%s,\"host_skipped_seconds\":%.3f,"
        "\"failed\":%s}}\n",
        took, ping, join_at, fps, jobs ? jobs->threads : 1u, budget_ms > 0 ? budget_ms : 4.0, rounds,
        rounds >= 2 ? "true" : "false", host.skipped, failed ? "true" : "false");
    if (fclose(raw)) failed = 1;
    for (int who = 0; who < 3; who++) {
        machine *m = &machines[who];
        if (m->session) tide_session_destroy(m->session);
        if (m->local) { tide_local_free(m->local); free(m->local); }
        tide_draw_free(&m->draw);
    }
    tide_loopback_destroy(net);
    fprintf(stderr, "native ping %.0f: %.1f s, %u frames, %d rounds started; %s\n", ping, took, ran_frames, rounds,
        failed ? "failed" : "complete");
    return failed ? 1 : 0;
}
