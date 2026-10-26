#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/session.h"
#include "tide_test.h"

typedef struct repair_input {
    uint32_t sequence;
    tide_view_time view;
} repair_input;

typedef struct repair_world {
    uint32_t tick, secret, changes, shown;
    repair_input input[2];
} repair_world;

typedef struct repair_probe {
    tide_view_time presented;
    tide_view_time sampled[1024];
    uint32_t samples, loads, skips, lost_stamps, first_tick, loaded_tick, adopted_stamps;
} repair_probe;

static repair_probe probe;

static void repair_start(void *world, float dt, const void *start, uint32_t seed)
{
    (void)dt; (void)start; (void)seed;
    memset(world, 0, sizeof(repair_world));
    ((repair_world *)world)->secret = 1;
}

static void repair_tick(void *world, const tide_jobs *jobs)
{
    (void)jobs;
    repair_world *w = world;
    w->tick++;
    // The client cannot predict these hidden consequences or availability changes.
    if (w->secret && w->tick >= 60 && w->tick < 420) {
        w->changes++;
        w->shown = (w->tick / 30u) % 2u;
    }
}

static void repair_copy(void *to, const void *from) { memcpy(to, from, sizeof(repair_world)); }
static uint64_t repair_hash(const void *world) { return tide_hash(world, sizeof(repair_world)); }
static void repair_player(void *world, tide_player_id player) { (void)world; (void)player; }

static uint8_t *repair_state(const void *world, tide_player_id player, uint32_t *size)
{
    (void)player;
    repair_world *out = malloc(sizeof *out);
    if (!out) return NULL;
    *out = *(const repair_world *)world;
    out->secret = 0;
    *size = sizeof *out;
    return (uint8_t *)out;
}

static bool repair_load(void *world, const uint8_t *bytes, uint32_t size)
{
    if (size != sizeof(repair_world)) return false;
    repair_world value;
    memcpy(&value, bytes, size);
    if (value.secret) return false;
    if (!probe.loads) probe.first_tick = value.tick;
    else if (value.tick > probe.loaded_tick + 1u) probe.skips++;
    probe.loaded_tick = value.tick;
    probe.loads++;
    memcpy(world, &value, size);
    return true;
}

static uint64_t repair_client_hash(const void *world, tide_player_id player)
{
    (void)player;
    repair_world value = *(const repair_world *)world;
    value.secret = 0;
    return repair_hash(&value);
}

static void repair_set(void *world, tide_player_id player, const void *input)
{
    ((repair_world *)world)->input[tide_player_index(player)] = *(const repair_input *)input;
}

static void repair_set_server(void *world, const void *input) { ((repair_world *)world)->input[1] = *(const repair_input *)input; }
static void repair_get(const void *world, uint32_t slot, void *input) { *(repair_input *)input = ((const repair_world *)world)->input[slot]; }
static tide_view_time repair_view(const void *input) { return ((const repair_input *)input)->view; }

static void repair_set_view(void *input, tide_view_time view)
{
    repair_input *value = input;
    memset(&value->view, 0, sizeof value->view);
    value->view.tick = view.tick;
    value->view.alpha = view.alpha;
    value->view.valid = view.valid;
}

static uint32_t repair_write(const void *input, uint8_t *bytes, uint32_t capacity)
{
    const repair_input *value = input;
    tide_writer w = {bytes, capacity, 0, false, false};
    uint32_t alpha;
    memcpy(&alpha, &value->view.alpha, sizeof alpha);
    tide_write_u32(&w, value->sequence);
    tide_write_u32(&w, (uint32_t)value->view.tick);
    tide_write_u32(&w, alpha);
    tide_write_u8(&w, value->view.valid);
    return w.overflow ? 0 : w.size;
}

static bool repair_read(const uint8_t *bytes, uint32_t size, void *input)
{
    if (size != 13) return false;
    repair_input *value = input;
    memset(value, 0, sizeof *value);
    tide_reader r = {bytes, size, 0, false};
    value->sequence = tide_read_u32(&r);
    value->view.tick = (int32_t)tide_read_u32(&r);
    const uint32_t alpha = tide_read_u32(&r);
    memcpy(&value->view.alpha, &alpha, sizeof alpha);
    const uint8_t valid = tide_read_u8(&r);
    value->view.valid = valid != 0;
    return !r.failed && valid <= 1;
}

static uint32_t repair_sample_write(const void *input, uint8_t *bytes, uint32_t capacity)
{
    const repair_input *value = input;
    if (value->sequence < 1024 && value->sequence > probe.samples) {
        probe.samples = value->sequence;
        probe.sampled[value->sequence] = value->view;
        // An adopted correction has not drawn anything: the next Sample must
        // still name the actual preceding frame, including on a skipped interval.
        if (probe.presented.valid && (!value->view.valid || value->view.tick != probe.presented.tick
            || value->view.alpha != probe.presented.alpha)) probe.lost_stamps++;
        if (value->view.valid && value->view.tick < (int32_t)probe.loaded_tick) probe.adopted_stamps++;
    }
    return repair_write(input, bytes, capacity);
}

static void repair_sample(void *user, uint32_t tick, void *input)
{
    (void)user;
    memset(input, 0, sizeof(repair_input));
    ((repair_input *)input)->sequence = tick + 1u;
}

static const tide_game repair_game = {
    .hash = 0x84F941Eu, .world_size = sizeof(repair_world), .max_players = 1,
    .input_size = sizeof(repair_input), .max_input_bytes = 13, .max_rewind = 0.25f,
    .start = repair_start, .tick = repair_tick, .copy_world = repair_copy, .hash_world = repair_hash,
    .player_joined = repair_player, .player_left = repair_player,
    .set_input = repair_set, .set_server_input = repair_set_server, .world_input = repair_get,
    .view_time = repair_view, .set_view_time = repair_set_view,
    .write_input = repair_write, .read_input = repair_read,
    .client_state = repair_state, .load_client_state = repair_load, .client_hash = repair_client_hash,
};

TIDE_TEST(session_view_time_survives_consecutive_visibility_repairs)
{
    memset(&probe, 0, sizeof probe);
    tide_game client_game = repair_game;
    client_game.write_input = repair_sample_write;
    tide_loopback *net = tide_loopback_create(731);
    tide_loopback_set_conditions(net, (tide_net_conditions){.latency = 0.02, .jitter = 0.005, .loss = 0.03});
    tide_server *server = tide_server_create(&(tide_server_desc){.game = &repair_game, .tick_rate = 60,
        .transports = {tide_loopback_endpoint(net, 1)}}, 0);
    // Joining a running match gives pre-admission stamps a nonzero boundary.
    tide_server_update(server, 0.1);
    tide_client *client = tide_client_create(&(tide_client_desc){.game = &client_game,
        .transport = tide_loopback_endpoint(net, 2), .server = tide_loopback_address(1),
        .sample = repair_sample, .lead = 2}, 0.1);
    TIDE_REQUIRE(net && server && client);
    uint32_t previous = 0, accepted = 0, expired = 0, corrected = 0, shown = 0, hidden = 0;
    bool was_shown = false;
    for (uint32_t step = 1; step <= 960; step++) {
        const double now = 0.1 + step / 120.0;
        tide_loopback_set_time(net, now);
        tide_client_update(client, now);
        tide_server_update(server, now);
        const repair_world *world = tide_server_world(server);
        const repair_input *input = &world->input[0];
        if (input->sequence && input->sequence != previous) {
            previous = input->sequence;
            TIDE_REQUIRE(previous < 1024);
            const tide_view_time sent = probe.sampled[previous];
            const double at = (double)sent.tick - 1.0 + sent.alpha;
            const double age = ((double)world->tick - 1.0 - at) / 60.0;
            const bool valid = sent.valid && at >= probe.first_tick && age <= repair_game.max_rewind;
            TIDE_CHECK(input->view.valid == valid);
            if (valid) {
                TIDE_CHECK(input->view.tick == sent.tick && input->view.alpha == sent.alpha);
                accepted++;
                if (world->tick >= 90 && world->tick < 420) corrected++;
            } else {
                TIDE_CHECK(input->view.tick == 0 && input->view.alpha == 0);
                if (sent.valid) expired++;
            }
        }
        // CPU presentation: obtain the same confirmed endpoints as a view,
        // then mark them only after this frame, following the host's ordering.
        const tide_view_worlds view = tide_client_view(client);
        // Updates run at 120 Hz, drawing at 20 Hz, with a 2/3-second render
        // pause while repairs continue. The real held stamp must expire.
        if (view.verified && step % 6u == 0 && !(step >= 400 && step < 480)) {
            const bool is_shown = ((const repair_world *)view.verified)->shown != 0;
            if (is_shown != was_shown) { if (is_shown) shown++; else hidden++; }
            was_shown = is_shown;
            probe.presented = (tide_view_time){(int32_t)view.verified_tick, view.verified_alpha, true};
            tide_client_mark_presented(client);
        }
    }
    const tide_client_status status = tide_client_status_of(client);
    printf("    admitted %u, loads %u, skips %u, accepted %u (%u during repairs), expired %u, lost stamps %u\n",
        probe.first_tick, probe.loads, probe.skips, accepted, corrected, expired, probe.lost_stamps);
    TIDE_CHECK(probe.loads > 20 && probe.skips > 20);
    TIDE_CHECK(probe.lost_stamps == 0 && probe.adopted_stamps > 50);
    TIDE_CHECK(accepted > 300 && corrected > 180);
    TIDE_CHECK(expired > 40);
    TIDE_CHECK(shown >= 4 && hidden >= 4);
    TIDE_CHECK(status.resyncs == 0 && status.predicted_tick > 470 && status.verified_tick > 460);
    tide_client_destroy(client);
    tide_server_destroy(server);
    tide_loopback_destroy(net);
}

typedef struct repair_peer {
    tide_loopback *net;
    tide_server *server;
    tide_transport transport;
    double now;
    uint64_t cookie;
    uint32_t nonce, sequence;
} repair_peer;

static tide_writer repair_header(repair_peer *p, uint8_t *bytes, uint8_t type)
{
    tide_writer w = {bytes, TIDE_NET_MTU, 0, false, false};
    tide_write_u16(&w, 0x5449);
    tide_write_u8(&w, 19);
    tide_write_u8(&w, type);
    if (type != 1) {
        tide_write_u64(&w, p->cookie);
        tide_write_u32(&w, p->nonce);
    }
    return w;
}

static void repair_hello(repair_peer *p)
{
    uint8_t bytes[TIDE_NET_MTU];
    tide_writer w = repair_header(p, bytes, 1);
    const uint8_t zero[32] = {0};
    tide_write_u64(&w, repair_game.hash);
    tide_write_bytes(&w, zero, sizeof zero);
    tide_write_u32(&w, p->nonce);
    tide_write_u16(&w, (uint16_t)(p->now * 1000));
    tide_write_u64(&w, p->cookie);
    tide_write_u8(&w, 0);
    tide_write_bytes(&w, zero, sizeof zero);
    p->transport.send(p->transport.self, tide_loopback_address(1), bytes, w.size);
    tide_server_update(p->server, p->now);
    p->cookie = tide_server_cookie(p->server, (tide_player_id){1});
    TIDE_CHECK(p->cookie && !w.overflow);
}

static void repair_claim(repair_peer *p, tide_view_time view, uint32_t ticks, bool wants_world)
{
    uint8_t bytes[TIDE_NET_MTU], packed[13];
    const uint32_t ack = tide_server_tick(p->server);
    const repair_input input = {.sequence = ++p->sequence, .view = view};
    tide_writer w = repair_header(p, bytes, 5);
    tide_write_u16(&w, (uint16_t)(p->now * 1000));
    tide_write_u16(&w, 0);
    tide_write_u8(&w, wants_world ? 1 : 0);
    tide_write_varint(&w, 0); // No chunk acknowledgement.
    tide_write_varint(&w, ack);
    tide_write_varint64(&w, 0);
    tide_write_varint(&w, ticks ? ticks - 1u : 0u); // Input runs at the last tick of this update.
    tide_write_u8(&w, ticks ? 1 : 0);
    if (ticks) {
        tide_write_varint(&w, repair_write(&input, packed, sizeof packed));
        tide_write_bytes(&w, packed, sizeof packed);
    }
    TIDE_CHECK(!w.overflow);
    p->transport.send(p->transport.self, tide_loopback_address(1), bytes, w.size);
    p->now += ticks / 60.0;
    tide_loopback_set_time(p->net, p->now);
    tide_server_update(p->server, p->now);
}

static void repair_check_claim(repair_peer *p, tide_view_time view, uint32_t ticks, bool valid)
{
    repair_claim(p, view, ticks, false);
    const repair_input *got = &((const repair_world *)tide_server_world(p->server))->input[0];
    // Rejecting the stamp must never drop the ordinary authenticated input.
    TIDE_CHECK(got->sequence == p->sequence && got->view.valid == valid);
    if (valid) TIDE_CHECK(got->view.tick == view.tick && got->view.alpha == view.alpha);
    else TIDE_CHECK(got->view.tick == 0 && got->view.alpha == 0);
}

TIDE_TEST(session_view_time_keeps_admission_advertisement_and_age_guards)
{
    for (uint32_t initial = 0; initial <= 120; initial += 120) {
        memset(&probe, 0, sizeof probe);
        repair_peer p = {.nonce = 77};
        p.net = tide_loopback_create(811);
        p.transport = tide_loopback_endpoint(p.net, 2);
        p.server = tide_server_create(&(tide_server_desc){.game = &repair_game, .tick_rate = 60,
            .transports = {tide_loopback_endpoint(p.net, 1)}}, 0);
        TIDE_REQUIRE(p.net && p.server);
        for (uint32_t i = 1; i <= initial; i++) {
            p.now = i / 60.0;
            tide_loopback_set_time(p.net, p.now);
            tide_server_update(p.server, p.now);
        }
        const uint32_t admission = tide_server_tick(p.server);
        TIDE_CHECK(admission == initial);
        repair_hello(&p);
        repair_claim(&p, (tide_view_time){0}, 0, true); // Cookie proof starts this attempt's admission.
        repair_check_claim(&p, (tide_view_time){(int32_t)admission, 1, true}, 1, true);
        if (initial) repair_check_claim(&p, (tide_view_time){(int32_t)admission - 1, 1, true}, 1, false);
        const int32_t tick = (int32_t)tide_server_tick(p.server);
        repair_check_claim(&p, (tide_view_time){tick + 100, 1, true}, 1, false);
        repair_check_claim(&p, (tide_view_time){tick, -0.5f, true}, 1, false);
        repair_check_claim(&p, (tide_view_time){tick, 1.5f, true}, 1, false);
        repair_check_claim(&p, (tide_view_time){tick, NAN, true}, 1, false);
        repair_check_claim(&p, (tide_view_time){tick, INFINITY, true}, 1, false);
        repair_check_claim(&p, (tide_view_time){-1, 1, true}, 1, false);
        // This stamp is before execution but after the last advertised server
        // tick: six ticks execute together before another SERVER can be sent.
        const int32_t advertised = (int32_t)tide_server_tick(p.server);
        repair_check_claim(&p, (tide_view_time){advertised + 2, 1, true}, 6, false);
        repair_check_claim(&p, (tide_view_time){(int32_t)tide_server_tick(p.server), 1, true}, 1, true);
        // Age, rather than the admission floor, rejects this once-valid stamp.
        const int32_t held = (int32_t)tide_server_tick(p.server);
        for (uint32_t i = 0; i < 3; i++) repair_claim(&p, (tide_view_time){0}, 8, false);
        repair_check_claim(&p, (tide_view_time){held, 1, true}, 1, false);
        // A live cookie reconnect begins a new authenticated attempt. A very
        // recent frame from the previous attempt is now before admission.
        const int32_t reconnect = (int32_t)tide_server_tick(p.server);
        p.nonce++;
        repair_hello(&p);
        repair_claim(&p, (tide_view_time){0}, 0, true);
        repair_check_claim(&p, (tide_view_time){reconnect - 1, 1, true}, 1, false);
        repair_check_claim(&p, (tide_view_time){reconnect, 1, true}, 1, true);
        tide_server_destroy(p.server);
        tide_loopback_destroy(p.net);
    }
}
