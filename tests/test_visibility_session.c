#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "tide/session.h"
#include "tide_test.h"

// Plain data deliberately has no shared physical-world snapshot/packing API.
// Only canonical permitted bytes can get a client into this game.
typedef struct visibility_world {
    uint32_t tick;
    uint32_t joined;
    uint32_t inputs[3];
    uint32_t positions[2];
    uint32_t cards[2];
    uint32_t sequence[2];
    uint32_t health;
    uint32_t reveal;
    uint32_t secret;
    uint32_t partial;
    uint32_t recipient;
    uint32_t token_alive;
    uint32_t padding[8192]; // Sparse corrections should compress, even across page regions.
} visibility_world;

static uint32_t visibility_loads;
static uint32_t visibility_starts;
static uint32_t visibility_private_inputs;
static uint32_t visibility_private_commands;
static uint32_t visibility_deaths;
static uint32_t visibility_hides;
static uint32_t visibility_bulk_filters;
static uint32_t visibility_slot_filters;
static bool visibility_continuous;

static void visibility_start(void *world, float dt, const void *start, uint32_t seed)
{
    (void)dt; (void)start; (void)seed;
    visibility_world *w = world;
    memset(w, 0, sizeof *w);
    w->cards[0] = 1000;
    w->cards[1] = 2000;
    w->health = 100;
    w->secret = 0x6A59F8D3u;
    w->token_alive = 1;
    uint32_t word = 0xF163AD71u;
    for (uint32_t i = 0; i < 8192; i++) {
        word ^= word << 13;
        word ^= word >> 17;
        word ^= word << 5;
        w->padding[i] = word;
    }
    visibility_starts++;
}

static void visibility_tick(void *world, const tide_jobs *jobs)
{
    (void)jobs;
    visibility_world *w = world;
    w->tick++;
    for (uint32_t p = 0; p < 2; p++) {
        if (!w->partial || p == w->recipient || w->reveal) {
            w->positions[p] += w->inputs[p];
            w->cards[p]++;
        }
    }
    // A consequence of genuinely hidden state cannot be reproduced locally.
    if (w->tick == 90 && w->secret) w->health -= 17;
    if (visibility_continuous && w->secret && w->tick >= 90 && w->tick < 570) w->health++;
    if (w->tick == 150 && w->secret) w->reveal = 1;
    if (w->tick == 210 && w->secret) w->reveal = 0;
    if (w->tick == 260 && w->secret) w->token_alive = 0;
    w->secret += w->secret ? 17u : 0u;
}

static void visibility_copy(void *to, const void *from) { memcpy(to, from, sizeof(visibility_world)); }
static uint64_t visibility_hash(const void *world) { return tide_hash(world, sizeof(visibility_world)); }

static uint8_t *visibility_state(const void *world, tide_player_id player, uint32_t *size)
{
    visibility_world *out = malloc(sizeof *out);
    if (!out) return NULL;
    *out = *(const visibility_world *)world;
    const uint32_t p = (uint32_t)tide_player_index(player);
    out->partial = 1;
    out->recipient = p;
    out->secret = 0;
    if (!out->reveal) {
        out->cards[1u - p] = 0;
        out->positions[1u - p] = 0;
        out->inputs[1u - p] = 0;
    }
    out->inputs[2] = 0;
    if (p == 1 && !out->reveal) out->token_alive = 0;
    *size = sizeof *out;
    return (uint8_t *)out;
}

static bool visibility_load(void *world, const uint8_t *data, uint32_t size)
{
    if (size != sizeof(visibility_world)) return false;
    const visibility_world *w = (const visibility_world *)(const void *)data;
    if (w->secret || !w->partial || w->recipient > 1u) return false;
    if (!w->reveal && w->inputs[1u - w->recipient]) visibility_private_inputs++;
    memcpy(world, data, size);
    visibility_loads++;
    return true;
}

static uint64_t visibility_client_hash(const void *world, tide_player_id player)
{
    uint32_t size;
    uint8_t *bytes = visibility_state(world, player, &size);
    const uint64_t hash = tide_hash(bytes, size);
    free(bytes);
    return hash;
}

static void visibility_filter_value(const void *world, tide_player_id player, uint32_t slot, void *input)
{
    const visibility_world *w = world;
    if (slot == 2 || (!w->reveal && slot != (uint32_t)tide_player_index(player))) *(uint32_t *)input = 0;
}

static void visibility_filter(const void *world, tide_player_id player, uint32_t slot, void *input)
{
    visibility_slot_filters++;
    visibility_filter_value(world, player, slot, input);
}

static void visibility_filter_all(const void *world, tide_player_id player, void *all_slots)
{
    visibility_bulk_filters++;
    for (uint32_t slot = 0; slot < 3; slot++)
        visibility_filter_value(world, player, slot, (uint32_t *)all_slots + slot);
}

static void visibility_joined(void *world, tide_player_id player)
{
    (void)player;
    ((visibility_world *)world)->joined++;
}
static void visibility_left(void *world, tide_player_id player) { (void)world; (void)player; }
static void visibility_input(void *world, tide_player_id player, const void *input)
{
    ((visibility_world *)world)->inputs[tide_player_index(player)] = *(const uint32_t *)input;
}
static void visibility_server_input(void *world, const void *input) { ((visibility_world *)world)->inputs[2] = *(const uint32_t *)input; }
static void visibility_world_input(const void *world, uint32_t slot, void *input) { *(uint32_t *)input = ((const visibility_world *)world)->inputs[slot]; }
static uint32_t visibility_write_input(const void *input, uint8_t *out, uint32_t capacity)
{
    if (capacity < 4) return 0;
    memcpy(out, input, 4);
    return 4;
}
static bool visibility_read_input(const uint8_t *data, uint32_t size, void *input)
{
    if (size != 4) return false;
    memcpy(input, data, 4);
    return true;
}
static void visibility_sample(void *user, uint32_t tick, void *input) { (void)tick; *(uint32_t *)input = *(const uint32_t *)user; }

static bool visibility_check_command(const uint8_t *data, uint32_t size, uint32_t *channel)
{
    if (!data || size < 2 || size > TIDE_COMMAND_LIMIT || data[0] != 0xA5) return false;
    if (channel) *channel = 0;
    return true;
}
static uint32_t visibility_sequence(const void *world, tide_player_id player, uint32_t channel)
{
    return channel == 0 ? ((const visibility_world *)world)->sequence[tide_player_index(player)] : 0;
}
static bool visibility_command(void *world, tide_player_id sender, uint32_t channel, uint32_t sequence,
                               const uint8_t *data, uint32_t size)
{
    visibility_world *w = world;
    const uint32_t player = (uint32_t)tide_player_index(sender);
    if (w->partial) visibility_private_commands++;
    if (channel || player > 1 || !visibility_check_command(data, size, NULL) || sequence != w->sequence[player] + 1u) return false;
    w->sequence[player] = sequence;
    w->health += data[1];
    return true;
}

static uint32_t visibility_destroyed(const void *world, const void *had, tide_player_id player,
                                    tide_entity *out, uint32_t capacity)
{
    const visibility_world *now = world, *before = had;
    if (!before || !before->token_alive || now->token_alive || (tide_player_index(player) != 0 && !before->reveal)) return 0;
    if (out && capacity) out[0] = (tide_entity){75, 1};
    return 1;
}

static void visibility_hear(const void *world, const void *had, const tide_entity *destroyed,
                            uint32_t count, tide_event_log *events)
{
    (void)events;
    const visibility_world *now = world, *before = had;
    if (!before || !before->token_alive || now->token_alive) return;
    bool died = false;
    for (uint32_t i = 0; i < count; i++) died |= destroyed[i].index == 75 && destroyed[i].generation == 1;
    if (died) visibility_deaths++;
    else visibility_hides++;
}

static void visibility_tick_journal(void *world, const tide_jobs *jobs, tide_event_log *per_player)
{
    visibility_world *w = world;
    const uint32_t alive = w->token_alive;
    visibility_tick(world, jobs);
    if (alive && !w->token_alive) {
        tide_heard *cause = tide_event_log_push(&per_player[0], sizeof(tide_heard), 0x991);
        cause->tick = w->tick - 1u;
        cause->event = TIDE_HEARD_DESTROYED_CAUSE;
        cause->target = (tide_entity){75, 1};
        tide_heard *event = tide_event_log_push(&per_player[0], sizeof(tide_heard) + 8u, 0x991);
        event->tick = w->tick - 1u;
        event->event = 91;
        event->wants = TIDE_HEARD_PREDICTED | TIDE_HEARD_VERIFIED | TIDE_HEARD_LIFECYCLE;
        event->target = (tide_entity){75, 1};
        event->handlers = 1;
        // The final event-time value differs from the previous verified tick.
        memcpy(event + 1, &w->cards[0], sizeof(uint32_t));
    }
}

static bool visibility_check_events(const uint8_t *data, uint32_t size, uint32_t *build, uint32_t *count)
{
    *count = 0;
    for (uint32_t at = 0; at < size;) {
        if (size - at < sizeof(tide_heard)) return false;
        tide_heard event;
        memcpy(&event, data + at, sizeof event);
        if (event.event == TIDE_HEARD_DESTROYED_CAUSE) {
            if (event.size != sizeof(tide_heard) || event.wants || event.as || event.handlers || event.same
                || event.target.index != 75 || event.target.generation != 1) return false;
            at += event.size;
            (*count)++;
            continue;
        }
        if (event.size != sizeof(tide_heard) + 8u || event.size > size - at || event.event != 91
            || event.handlers != 1 || event.wants != (TIDE_HEARD_PREDICTED | TIDE_HEARD_VERIFIED | TIDE_HEARD_LIFECYCLE)
            || event.target.index != 75 || event.target.generation != 1) return false;
        at += event.size;
        (*count)++;
    }
    *build = 0x991;
    return true;
}

static const tide_game visibility_game = {
    .hash = 0x5B2B39157u, .world_size = sizeof(visibility_world),
    .input_size = 4, .max_input_bytes = 4, .max_players = 2,
    .start = visibility_start, .tick = visibility_tick, .copy_world = visibility_copy, .hash_world = visibility_hash,
    .player_joined = visibility_joined, .player_left = visibility_left,
    .set_input = visibility_input, .set_server_input = visibility_server_input,
    .write_input = visibility_write_input, .read_input = visibility_read_input, .world_input = visibility_world_input,
    .client_state = visibility_state, .load_client_state = visibility_load, .client_hash = visibility_client_hash,
    .filter_client_input = visibility_filter, .filter_client_inputs = visibility_filter_all, .pure_start = true,
    .check_command = visibility_check_command, .command_sequence = visibility_sequence, .command = visibility_command,
    .client_destroyed = visibility_destroyed, .hear_visibility = visibility_hear,
    .tick_client_events = visibility_tick_journal, .check_client_events = visibility_check_events,
};

static void visibility_run_case(tide_net_conditions conditions, bool commands, bool recover, bool continuous)
{
    visibility_loads = visibility_starts = visibility_private_inputs = 0;
    visibility_private_commands = 0;
    visibility_deaths = visibility_hides = 0;
    visibility_bulk_filters = visibility_slot_filters = 0;
    visibility_continuous = continuous;
    tide_loopback *net = tide_loopback_create(541254);
    TIDE_REQUIRE(net);
    tide_loopback_set_conditions(net, conditions);
    tide_server *server = tide_server_create(&(tide_server_desc){.game = &visibility_game,
        .transports = {tide_loopback_endpoint(net, 1)}}, 0);
    uint32_t sampled[2] = {3, 7};
    tide_client *clients[2];
    tide_event_log events[2] = {{.taken = true}, {.taken = true}};
    for (uint32_t p = 0; p < 2; p++) clients[p] = tide_client_create(&(tide_client_desc){.game = &visibility_game,
        .transport = tide_loopback_endpoint(net, p + 2u), .server = tide_loopback_address(1),
        .sample = visibility_sample, .user = &sampled[p], .lead = 2, .events = &events[p]}, 0);
    TIDE_REQUIRE(server && clients[0] && clients[1]);
    bool commands_sent = false;
    uint32_t most_lag = 0;
    for (uint32_t step = 1; step <= 1400; step++) {
        const double now = (double)step / 120.0;
        tide_loopback_set_time(net, now);
        for (uint32_t p = 0; p < 2; p++) {
            if (!(recover && p == 0 && step >= 480 && step < 984)) tide_client_update(clients[p], now);
        }
        tide_server_update(server, now);
        for (uint32_t p = 0; continuous && step >= 360 && p < 2; p++) {
            const visibility_world *confirmed = tide_client_verified_world(clients[p]);
            if (confirmed && tide_server_tick(server) >= confirmed->tick) {
                const uint32_t lag = tide_server_tick(server) - confirmed->tick;
                if (lag > most_lag) most_lag = lag;
            }
        }
        if (commands && !commands_sent && step >= 120
            && tide_client_status_of(clients[0]).state == TIDE_SESSION_CONNECTED) {
            commands_sent = true;
            const uint8_t small[] = {0xA5, 5};
            TIDE_CHECK(tide_client_send_command(clients[0], small, sizeof small));
            uint8_t *large = malloc(100000);
            TIDE_REQUIRE(large);
            memset(large, 0xCC, 100000);
            large[0] = 0xA5;
            large[1] = 9;
            TIDE_CHECK(tide_client_send_command(clients[0], large, 100000));
            large[1] = 11;
            TIDE_CHECK(tide_client_send_command(clients[0], large, 100000));
            free(large);
        }
    }
    TIDE_CHECK(visibility_starts == 1); // No client ever reconstructs private starting state.
    TIDE_CHECK(visibility_loads >= 8); // Initial joins plus hidden interaction/reveal/conceal corrections.
    TIDE_CHECK(visibility_private_inputs == 0);
    TIDE_CHECK(visibility_private_commands == 0);
    TIDE_CHECK(visibility_bulk_filters > 0 && visibility_slot_filters == 0);
    TIDE_CHECK(visibility_deaths == 1);
    TIDE_CHECK(visibility_hides == 1);
    if (continuous) TIDE_CHECK(most_lag < 120u);
    const uint32_t command_sender = (uint32_t)tide_player_index(tide_client_status_of(clients[0]).player);
    for (uint32_t p = 0; p < 2; p++) {
        const tide_client_status status = tide_client_status_of(clients[p]);
        if (continuous && getenv("TIDE_VISIBILITY_METRICS")) {
            printf("visibility client %u: server=%u verified=%u most_lag=%u world_bytes=%llu loads=%u resyncs=%u\n",
                p, tide_server_tick(server), status.verified_tick, most_lag,
                (unsigned long long)status.world_bytes, visibility_loads, status.resyncs);
        }
        TIDE_CHECK(status.state == TIDE_SESSION_CONNECTED);
        TIDE_CHECK(status.resyncs == (recover && p == 0 ? 1u : 0u));
        // Random unchanged regions are expensive to send whole. Historical
        // corrections must reuse their acknowledged bytes rather than do so.
        TIDE_CHECK(status.world_bytes < sizeof(visibility_world) * (recover && p == 0 ? 3u : 2u));
        const visibility_world *w = tide_client_verified_world(clients[p]);
        TIDE_REQUIRE(w);
        TIDE_CHECK(w->tick > 600);
        TIDE_CHECK(w->partial == 1 && w->secret == 0 && !w->reveal);
        TIDE_CHECK(w->health == (continuous ? 563u : commands ? 108u : 83u));
        if (commands) {
            TIDE_CHECK(w->sequence[command_sender] == 3);
        }
        TIDE_CHECK(w->cards[1u - w->recipient] == 0 && w->positions[1u - w->recipient] == 0);
        TIDE_CHECK(w->cards[w->recipient] == 1000u * (w->recipient + 1u) + w->tick);
        uint32_t predicted = 0, verified = 0, causes = 0;
        for (uint32_t at = 0; at < events[p].size;) {
            const tide_heard *heard = (const tide_heard *)(events[p].bytes + at);
            at += heard->size;
            if (heard->event == TIDE_HEARD_DESTROYED_CAUSE) {
                TIDE_CHECK(heard->size == sizeof *heard && heard->wants == 0 && heard->handlers == 0);
                TIDE_CHECK(heard->as == (TIDE_HEARD_PREDICTED | TIDE_HEARD_VERIFIED));
                TIDE_CHECK(heard->target.index == 75 && heard->target.generation == 1 && heard->tick == 259u);
                causes++;
                continue;
            }
            uint32_t card;
            memcpy(&card, heard + 1, sizeof card);
            TIDE_CHECK(heard->event == 91 && card == 1260u && heard->tick == 259u);
            predicted += heard->as == TIDE_HEARD_PREDICTED;
            verified += heard->as == TIDE_HEARD_VERIFIED;
        }
        const uint32_t expected_events = w->recipient == 0 && !(recover && p == 0) ? 1u : 0u;
        TIDE_CHECK(predicted == expected_events && verified == expected_events);
        TIDE_CHECK(causes == (w->recipient == 0 ? 1u : 0u));
        tide_client_destroy(clients[p]);
        tide_event_log_free(&events[p]);
    }
    tide_server_destroy(server);
    tide_loopback_destroy(net);
}

TIDE_TEST(visibility_inputs_and_historical_repairs_preserve_private_state)
{
    visibility_run_case((tide_net_conditions){0}, false, false, false);
}

TIDE_TEST(visibility_repairs_survive_packet_loss_and_reordering)
{
    visibility_run_case((tide_net_conditions){.latency = 0.08, .jitter = 0.06, .loss = 0.2}, false, false, false);
}

TIDE_TEST(visibility_private_commands_and_chunked_uploads_stay_on_server)
{
    visibility_run_case((tide_net_conditions){.latency = 0.08, .jitter = 0.06, .loss = 0.2}, true, false, false);
}

TIDE_TEST(visibility_recovery_retains_destruction_causes_past_frame_history)
{
    visibility_run_case((tide_net_conditions){0}, false, true, false);
}

TIDE_TEST(visibility_continuous_hidden_effects_coalesce_without_rtt_per_tick)
{
    visibility_run_case((tide_net_conditions){.latency = 0.08, .jitter = 0.06, .loss = 0.2}, false, false, true);
}

TIDE_TEST(visibility_host_migration_never_promotes_a_partial_world)
{
    visibility_continuous = false;
    tide_game game = visibility_game;
    game.host_migration = true;
    game.filter_client_inputs = NULL; // The optional single-slot fallback also works.
    uint32_t input = 3;
    tide_session *host = tide_session_create(&(tide_session_desc){.game = &game,
        .sample = visibility_sample, .user = &input});
    tide_session *client = tide_session_create(&(tide_session_desc){.game = &game,
        .sample = visibility_sample, .user = &input});
    tide_loopback *net = tide_loopback_create(5211);
    TIDE_REQUIRE(host && client && net);
    tide_session_start(host, NULL, 0);
    char key[TIDE_ROOM_KEY_LENGTH + 1];
    memset(key, 'a', sizeof key - 1u);
    key[sizeof key - 1u] = 0;
    tide_session_set_room(host, "TEST2", key);
    TIDE_REQUIRE(tide_session_open(host, tide_loopback_endpoint(net, 1)));
    tide_session_join(client, tide_loopback_endpoint(net, 2), tide_loopback_address(1), 0);
    for (uint32_t step = 1; step <= 240; step++) {
        const double now = (double)step / 120.0;
        tide_loopback_set_time(net, now);
        tide_session_update(host, now);
        tide_session_update(client, now);
    }
    TIDE_REQUIRE(tide_session_status_of(client).client.state == TIDE_SESSION_CONNECTED);
    tide_session_leave(host);
    for (uint32_t step = 241; step <= 300; step++) {
        const double now = (double)step / 120.0;
        tide_loopback_set_time(net, now);
        tide_session_update(client, now);
    }
    char code[TIDE_ROOM_CODE_LENGTH + 1];
    TIDE_CHECK(!tide_session_migrating(client, code, key));
    TIDE_CHECK(tide_session_server_world(client) == NULL);
    tide_session_take_over(client, tide_loopback_endpoint(net, 3), 2.5);
    TIDE_CHECK(tide_session_server_world(client) == NULL);
    tide_session_destroy(client);
    tide_session_destroy(host);
    tide_loopback_destroy(net);
}
