#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "tide/session.h"
#include "tide/shard.h"
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
    uint32_t held; // Private predicted-world Far marker, excluded from projections.
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
static uint32_t visibility_differs_from = 90, visibility_differs_to = 570; // The ticks a continuous match's differ in
static bool visibility_large_spread;
static bool visibility_large_far;
static uint32_t visibility_far_full_ticks, visibility_far_held_ticks;

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
    if (visibility_continuous && w->secret && w->tick >= visibility_differs_from && w->tick < visibility_differs_to) w->health++;
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
    out->held = 0;
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

// A public derived payload with shifting list lengths and changing record
// fields. Same-tick client predictions reproduce it; acknowledged-tick repairs
// otherwise resend substantial evolution even for a one-field hidden effect.
static uint8_t *visibility_large_state(const void *world, tide_player_id player, uint32_t *size)
{
    uint32_t small_size = 0;
    uint8_t *small = visibility_state(world, player, &small_size);
    const visibility_world *w = world;
    const uint32_t prefix = 1u + w->tick % 7u;
    *size = small_size + prefix + 1000u * 512u;
    uint8_t *out = malloc(*size);
    if (!out) { free(small); return NULL; }
    memcpy(out, small, small_size);
    free(small);
    memset(out + small_size, 0, prefix);
    uint8_t *records = out + small_size + prefix;
    uint32_t x = 0x72ac5319u ^ (visibility_large_far ? w->padding[1] : 0u);
    for (uint32_t i = 0; i < 1000u * 512u; i++) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        records[i] = (uint8_t)x;
    }
    for (uint32_t i = 0; i < 1000u; i++) for (uint32_t j = 0; j < 12u; j++)
        records[i * 512u + j] ^= (uint8_t)((w->tick * 13u + j) >> (j % 4u * 8u));
    if (visibility_large_spread) for (uint32_t i = 0; i < 8u; i++)
        records[i * 16384u + 16u] ^= (uint8_t)w->health;
    return out;
}

static bool visibility_large_load(void *world, const uint8_t *data, uint32_t size)
{
    if (size < sizeof(visibility_world)) return false;
    visibility_world header;
    memcpy(&header, data, sizeof header);
    if (size != sizeof header + 1u + header.tick % 7u + 1000u * 512u) return false;
    return visibility_load(world, data, sizeof header);
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
    for (uint32_t i = 0; i < count; i++) died |= tide_entity_equal(destroyed[i], (tide_entity){75, 1});
    if (died) visibility_deaths++;
    else visibility_hides++;
}

static void visibility_tick_journal(void *world, const tide_jobs *jobs, tide_event_log *per_player, tide_event_log *all)
{
    (void)all; // No local handler of its own machine hears it
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
                || !tide_entity_equal(event.target, (tide_entity){75, 1})) return false;
            at += event.size;
            (*count)++;
            continue;
        }
        if (event.size != sizeof(tide_heard) + 8u || event.size > size - at || event.event != 91
            || event.handlers != 1 || event.wants != (TIDE_HEARD_PREDICTED | TIDE_HEARD_VERIFIED | TIDE_HEARD_LIFECYCLE)
            || !tide_entity_equal(event.target, (tide_entity){75, 1})) return false;
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

typedef struct visibility_replay {
    const tide_game *game;
    visibility_world world;
    uint32_t records;
} visibility_replay;

static void visibility_replay_record(void *user, uint32_t tick, const uint8_t *record, uint32_t size)
{
    visibility_replay *replay = user;
    TIDE_CHECK(tick == replay->records);
    TIDE_CHECK(tide_replay(replay->game, &replay->world, record, size, NULL));
    replay->records++;
}

static bool visibility_receive_shard(void *world, const uint8_t *data, uint32_t size)
{
    visibility_world *w = world;
    for (uint32_t i = 0; i < size; i++) w->health += data[i];
    return true;
}

TIDE_TEST(visibility_journal_replays_private_small_and_large_shard_messages)
{
    const tide_shard_game shards = {.receive = visibility_receive_shard};
    tide_game game = visibility_game;
    game.shards = &shards;
    visibility_replay replay = {.game = &game};
    visibility_continuous = false;
    tide_server *server = tide_server_create(&(tide_server_desc){.game = &game,
        .journal = visibility_replay_record, .journal_user = &replay}, 0);
    TIDE_REQUIRE(server);
    replay.world = *(const visibility_world *)tide_server_world(server);
    uint8_t message[5000];
    memset(message, 7, sizeof message);
    TIDE_CHECK(tide_server_inject(server, message, 3));
    TIDE_CHECK(tide_server_inject(server, message, sizeof message));
    for (uint32_t i = 1; i <= 120; i++) tide_server_update(server, i / 60.0);
    TIDE_CHECK(replay.records > 100);
    TIDE_CHECK(visibility_hash(&replay.world) == visibility_hash(tide_server_world(server)));
    tide_server_destroy(server);
}

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
    visibility_replay replay = {.game = &visibility_game};
    tide_server *server = tide_server_create(&(tide_server_desc){.game = &visibility_game,
        .transports = {tide_loopback_endpoint(net, 1)},
        .journal = visibility_replay_record, .journal_user = &replay}, 0);
    TIDE_REQUIRE(server);
    replay.world = *(const visibility_world *)tide_server_world(server);
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
    TIDE_CHECK(replay.records > 600);
    TIDE_CHECK(visibility_hash(&replay.world) == visibility_hash(tide_server_world(server)));
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
                TIDE_CHECK(tide_entity_equal(heard->target, (tide_entity){75, 1}) && heard->tick == 259u);
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

static uint32_t visibility_recorded_ticks;

static void visibility_record_tick(void *world, const tide_jobs *jobs, tide_event_log *events)
{
    visibility_world *w = world;
    const uint32_t tick = w->tick;
    visibility_tick(world, jobs);
    visibility_recorded_ticks++;
    tide_heard *heard = tide_event_log_push(events, sizeof *heard, 0x991);
    heard->tick = tick;
    heard->event = 92;
    heard->handlers = 1;
    heard->wants = TIDE_HEARD_PREDICTED | TIDE_HEARD_VERIFIED;
    heard->target = (tide_entity){1, 1};
}

TIDE_TEST(visibility_delay_repair_candidates_do_not_deliver_future_events)
{
    visibility_continuous = true;
    visibility_recorded_ticks = 0;
    tide_game game = visibility_game;
    game.prediction = TIDE_PREDICTION_DELAY;
    game.tick_events = visibility_record_tick;
    game.client_state = visibility_large_state;
    game.load_client_state = visibility_large_load;
    tide_loopback *net = tide_loopback_create(91741);
    TIDE_REQUIRE(net);
    tide_loopback_set_conditions(net, (tide_net_conditions){.latency = 0.08, .jitter = 0.04, .loss = 0.1});
    tide_server *server = tide_server_create(&(tide_server_desc){.game = &game, .tick_rate = 60,
        .transports = {tide_loopback_endpoint(net, 1)}}, 0);
    uint32_t sampled = 3;
    tide_event_log events = {.taken = true};
    tide_client *client = tide_client_create(&(tide_client_desc){.game = &game,
        .transport = tide_loopback_endpoint(net, 2), .server = tide_loopback_address(1),
        .sample = visibility_sample, .user = &sampled, .lead = 2, .events = &events}, 0);
    TIDE_REQUIRE(server && client);
    uint32_t read = 0;
    bool predicted[800] = {false}, verified[800] = {false};
    for (uint32_t step = 1; step <= 1400; step++) {
        const double now = (double)step / 120.0;
        tide_loopback_set_time(net, now);
        tide_client_update(client, now);
        tide_server_update(server, now);
        const tide_client_status status = tide_client_status_of(client);
        for (; read < events.size;) {
            const tide_heard *heard = (const tide_heard *)(events.bytes + read);
            read += heard->size;
            if (heard->event != 92) continue;
            TIDE_REQUIRE(heard->tick < 800u);
            // Quiet comparison ticks must neither present nor publish events.
            TIDE_CHECK(heard->tick <= status.verified_tick);
            bool *seen = heard->as == TIDE_HEARD_VERIFIED ? verified : predicted;
            TIDE_CHECK(!seen[heard->tick]);
            seen[heard->tick] = true;
        }
    }
    const tide_client_status status = tide_client_status_of(client);
    TIDE_CHECK(status.state == TIDE_SESSION_CONNECTED && status.resyncs == 0);
    TIDE_CHECK(status.repairs > 5u && status.verified_tick > 600u);
    // Delay normally records every tick it runs. Additional work here is the
    // private repair comparison replay, which must not use that recording path.
    TIDE_CHECK(status.run_ticks > visibility_recorded_ticks + 10u);
    tide_client_destroy(client);
    tide_event_log_free(&events);
    tide_server_destroy(server);
    tide_loopback_destroy(net);
    visibility_continuous = false;
}

// The large game's host and one client, whose builds a test swaps under a
// stream of its repairs: every tick from 90 to 570 differs on the client, and
// each repair is half a megabyte's hashes and then a patch.
enum { RELOAD_HOST = 1, RELOAD_CLIENT = 2 };

// The client's end of the network, which says what the server names in its
// WELCOMEs, and can give the client one of them again, late
typedef struct visibility_wire {
    tide_transport inner;
    uint32_t number, tick; // What the newest WELCOME read names, and its kind (SEND_*: 2 a whole
    uint8_t kind;          // projection, 3 a repair, 4 a repair's hashes, 5 its patch)
    uint8_t held[64];      // The newest WELCOME of `hold`'s kind
    uint32_t held_size, held_number;
    tide_address held_from;
    uint8_t hold;
    bool again; // The held one is what's read next
    bool lose_hashes;     // No chunk comes of the next repair's hashes a WELCOME names, nor of any after it
    uint32_t lose_number; // ...which is this one, the last so far
    // For the spare chunks' tests: every `lose_servers`th SERVER is lost (so the server counts a network that loses
    // datagrams); of the transfer `watch`, the chunk `lose_index` is lost `lose_times` times and its spares are
    // lost with `lose_spares`; and what came of it is counted, lost or not
    uint32_t lose_servers, servers;
    uint32_t watch, watch_chunks, lose_index, lose_times;
    bool lose_spares;
    uint32_t chunks_seen, index_seen, spares_seen, spares_all; // (spares_all: of any transfer)
    uint32_t worlds_again; // Whole projections named after it
    uint8_t spare[1100]; // Its spare that's of the lost chunk, as it came
    uint32_t spare_size;
    tide_address spare_from;
    uint8_t forged[1100]; // A datagram a test made, read next
    uint32_t forged_size;
} visibility_wire;

static void visibility_wire_send(void *self, tide_address to, const void *data, uint32_t size)
{
    visibility_wire *w = self;
    w->inner.send(w->inner.self, to, data, size);
}

static uint32_t visibility_wire_receive(void *self, tide_address *from, void *data, uint32_t capacity)
{
    visibility_wire *w = self;
    if (w->again && w->held_size && w->held_size <= capacity) {
        w->again = false;
        memcpy(data, w->held, w->held_size);
        *from = w->held_from;
        return w->held_size;
    }
    if (w->forged_size && w->forged_size <= capacity) {
        memcpy(data, w->forged, w->forged_size);
        *from = w->spare_from;
        const uint32_t forged = w->forged_size;
        w->forged_size = 0;
        return forged;
    }
    uint32_t size = w->inner.receive(w->inner.self, from, data, capacity);
    const uint8_t *bytes = data;
    // A CHUNK: its header, the cookie, a nonce, then what it's a chunk of and which. A SPARE: the same, then its
    // block's first chunk, its stride and its group. A SERVER is the sixth kind
    for (; size >= 4u; size = w->inner.receive(w->inner.self, from, data, capacity)) {
        uint32_t number = 0, index = 0;
        if (size >= 24u && (bytes[3] == 4u || bytes[3] == 14u)) {
            memcpy(&number, bytes + 16, 4);
            memcpy(&index, bytes + 20, 4);
        }
        if (bytes[3] == 6u && w->lose_servers && ++w->servers % w->lose_servers == 0u) continue;
        if (bytes[3] == 4u && w->lose_number && number == w->lose_number) continue;
        if (bytes[3] == 4u && w->watch && number == w->watch) {
            w->chunks_seen++;
            if (index == w->lose_index) {
                w->index_seen++;
                if (w->lose_times) {
                    w->lose_times--;
                    continue;
                }
            }
        }
        if (bytes[3] == 14u) w->spares_all++;
        if (bytes[3] == 14u && w->watch && number == w->watch) {
            w->spares_seen++;
            // (The block the lost chunk is in, and the spare of that block it's in: its stride at 24, its group at 25)
            if (size >= 26u && size <= sizeof w->spare && index == w->lose_index / 64u * 64u && bytes[24]
                && bytes[25] == w->lose_index % 64u % bytes[24]) {
                memcpy(w->spare, bytes, size);
                w->spare_size = size;
                w->spare_from = *from;
            }
            if (w->lose_spares) continue;
        }
        break;
    }
    // A WELCOME: its header, a nonce, the player, flags, the tick rate, the server's tick, then what's sent
    if (size >= 27u && size <= sizeof w->held && bytes[3] == 2u) {
        uint32_t number, tick;
        memcpy(&tick, bytes + 18, 4);
        memcpy(&number, bytes + 22, 4);
        if (number > w->number) {
            w->worlds_again += w->watch && number > w->watch && bytes[26] == 2u;
            w->number = number;
            w->tick = tick;
            w->kind = bytes[26];
            if (w->lose_hashes && w->kind == 4u) w->lose_number = number;
            if (number == w->watch && size >= 35u) memcpy(&w->watch_chunks, bytes + 31, 4);
        }
        if (number && bytes[26] == w->hold && number >= w->held_number) {
            memcpy(w->held, bytes, size);
            w->held_size = size;
            w->held_number = number;
            w->held_from = *from;
        }
    }
    return size;
}

static void visibility_wire_close(void *self)
{
    visibility_wire *w = self;
    w->inner.close(w->inner.self);
}

static double visibility_wire_clock(void *self)
{
    visibility_wire *w = self;
    return w->inner.clock(w->inner.self);
}

static double visibility_wire_arrived(void *self)
{
    visibility_wire *w = self;
    return w->inner.arrived(w->inner.self);
}

typedef struct visibility_match {
    tide_game template, games[2][2]; // Each side has its own pair of tables, as two machines have
    uint32_t active[2], sampled, step;
    tide_session *host, *client;
    tide_loopback *net;
    visibility_wire wire;
} visibility_match;

static void visibility_match_start(visibility_match *m, const uint32_t seed, const tide_net_conditions conditions,
                                   const bool large)
{
    memset(m, 0, sizeof *m);
    visibility_continuous = large;
    m->template = visibility_game;
    if (large) {
        m->template.client_state = visibility_large_state;
        m->template.load_client_state = visibility_large_load;
    }
    for (uint32_t i = 0; i < 4u; i++) m->games[i / 2u][i % 2u] = m->template;
    m->sampled = 3;
    m->host = tide_session_create(&(tide_session_desc){.game = &m->games[0][0],
        .sample = visibility_sample, .user = &m->sampled});
    m->client = tide_session_create(&(tide_session_desc){.game = &m->games[1][0],
        .sample = visibility_sample, .user = &m->sampled});
    m->net = tide_loopback_create(seed);
    TIDE_REQUIRE(m->host && m->client && m->net);
    tide_loopback_set_conditions(m->net, conditions);
    tide_session_start(m->host, NULL, 0);
    TIDE_REQUIRE(tide_session_open(m->host, tide_loopback_endpoint(m->net, 1)));
    m->wire.inner = tide_loopback_endpoint(m->net, 2);
    const tide_transport wire = {.self = &m->wire, .send = visibility_wire_send, .receive = visibility_wire_receive,
        .close = m->wire.inner.close ? visibility_wire_close : NULL,
        .clock = m->wire.inner.clock ? visibility_wire_clock : NULL,
        .arrived = m->wire.inner.arrived ? visibility_wire_arrived : NULL};
    tide_session_join(m->client, wire, tide_loopback_address(1), 0);
}

static void visibility_match_step(visibility_match *m)
{
    const double now = (double)++m->step / 120.0;
    tide_loopback_set_time(m->net, now);
    tide_session_update(m->host, now);
    tide_session_update(m->client, now);
}

// The sides in `sides` take their other table, and the one they had is zeroed
static void visibility_match_reload(visibility_match *m, const uint32_t sides)
{
    tide_session *sessions[2] = {m->host, m->client};
    for (uint32_t side = 0; side < 2u; side++) {
        if (!(sides >> side & 1u)) continue;
        const uint32_t next = 1u - m->active[side];
        m->games[side][next] = m->template;
        tide_session_set_game(sessions[side], &m->games[side][next]);
        memset(&m->games[side][m->active[side]], 0, sizeof m->games[side][0]); // No retained callback may use it.
        m->active[side] = next;
    }
}

static tide_client_status visibility_match_client(const visibility_match *m)
{
    return tide_session_status_of(m->client).client;
}

// The client went on to the match's end: every hidden effect, and the
// server's ticks to within `behind` of them. False where it stopped.
static bool visibility_match_caught_up(const visibility_match *m, const uint32_t behind)
{
    const tide_session_status status = tide_session_status_of(m->client);
    const visibility_world *world = tide_session_view(m->client).verified;
    const uint32_t tick = tide_session_status_of(m->host).client.verified_tick; // The server's, on its machine
    return world && status.client.state == TIDE_SESSION_CONNECTED && world->health == 563u
        && tick >= 600u && status.client.verified_tick + behind >= tick;
}

static void visibility_match_end(visibility_match *m)
{
    tide_session_destroy(m->client);
    tide_session_destroy(m->host);
    tide_loopback_destroy(m->net);
    visibility_continuous = false;
    visibility_differs_from = 90;
    visibility_differs_to = 570;
}

// From `first` to `last`, every `every` steps (120 a second), on a network
// that loses a datagram in ten. Whether the client went on to the end.
static bool visibility_reload_case(const uint32_t seed, const uint32_t sides, const uint32_t first,
                                   const uint32_t last, const uint32_t every, uint32_t *repairs)
{
    visibility_match *m = malloc(sizeof *m);
    if (!m) return false;
    visibility_match_start(m, seed, (tide_net_conditions){.latency = 0.08, .jitter = 0.04, .loss = 0.1}, true);
    while (m->step < 1800u) {
        visibility_match_step(m);
        // Hash offers, quiet candidate construction and patches overlap this
        // stream. Replacing the tables repeatedly exercises each boundary.
        if (m->step >= first && m->step <= last && (m->step - first) % every == 0u) visibility_match_reload(m, sides);
    }
    const bool ok = visibility_match_caught_up(m, 30u);
    if (repairs) *repairs = visibility_match_client(m).repairs;
    visibility_match_end(m);
    free(m);
    return ok;
}

TIDE_TEST(visibility_large_repairs_release_old_callbacks_on_compatible_reload)
{
    uint32_t repairs = 0;
    TIDE_CHECK(visibility_reload_case(61579, RELOAD_HOST | RELOAD_CLIENT, 400, 440, 4, &repairs));
    TIDE_CHECK(repairs > 5u);
}

// Which datagrams are lost decides what a reload finds: a repair's hashes or
// its patch on their way, one loaded whose acknowledgement hasn't come, the
// whole projection the server started for the last reload. Whatever it finds,
// the client goes on: with the session before, 3 of these 64 stopped for good
// where the reload found them (seeds 25, 45 and 48).
TIDE_TEST(visibility_reloads_under_repairs_stop_no_client_whatever_is_lost)
{
    uint32_t stopped = 0;
    for (uint32_t seed = 1; seed <= 64u; seed++) {
        if (visibility_reload_case(seed, RELOAD_HOST | RELOAD_CLIENT, 400, 440, 4, NULL)) continue;
        printf("    seed %u stopped\n", seed);
        stopped++;
    }
    TIDE_CHECK(stopped == 0);
}

// Two machines don't swap their builds in the same update. A server's alone:
// eleven in a third of a second (the odd seeds), and one every third of a
// second all through the stream (the even ones). 45 of these 64 stopped with
// the session before.
TIDE_TEST(visibility_reloads_of_the_server_alone_stop_no_client_whatever_is_lost)
{
    uint32_t stopped = 0;
    for (uint32_t seed = 1; seed <= 64u; seed++) {
        const bool often = seed % 2u == 0u;
        if (visibility_reload_case(seed, RELOAD_HOST, often ? 200 : 400, often ? 1100 : 440, often ? 37 : 4, NULL)) continue;
        printf("    seed %u stopped\n", seed);
        stopped++;
    }
    TIDE_CHECK(stopped == 0);
}

// The cases beside those: the client's build alone, two reloads in one
// twelfth of a second, reloads while the first whole projection comes, and
// either side's or both all through the stream
TIDE_TEST(visibility_reloads_of_either_side_at_any_time_stop_no_client)
{
    static const struct { uint32_t sides, first, last, every; } cases[] = {
        {RELOAD_CLIENT, 400, 440, 4}, {RELOAD_HOST | RELOAD_CLIENT, 400, 401, 1}, {RELOAD_HOST, 400, 401, 1},
        {RELOAD_HOST | RELOAD_CLIENT, 4, 60, 4}, {RELOAD_HOST, 4, 60, 4}, {RELOAD_CLIENT, 4, 60, 4},
        {RELOAD_HOST | RELOAD_CLIENT, 200, 1100, 37}, {RELOAD_CLIENT, 200, 1100, 37},
    };
    uint32_t stopped = 0;
    for (uint32_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        for (uint32_t seed = 101; seed <= 108u; seed++) {
            if (visibility_reload_case(seed, cases[i].sides, cases[i].first, cases[i].last, cases[i].every, NULL)) continue;
            printf("    case %u seed %u stopped\n", i, seed);
            stopped++;
        }
    }
    TIDE_CHECK(stopped == 0);
}

// The state that stopped a client, on a network that loses nothing: the
// server's build changes while it is comparing a repair with the player's
// world (it has the worlds it compares, made by the old build), so it lets go
// of them and starts the whole projection. The client isn't told to want one:
// it has the repair whole and its acknowledgement is on its way (`loaded`),
// or the repair's hashes have just come (`!loaded`). It ran past the
// projection's tick, and used to refuse it: the server sent it for good, and
// answered no repair meanwhile.
static void visibility_reload_restarts(const uint32_t sides, const bool loaded)
{
    visibility_match *m = malloc(sizeof *m);
    TIDE_REQUIRE(m);
    visibility_match_start(m, 7, (tide_net_conditions){.latency = 0.08}, true);
    uint32_t repairs = 0, number = 0;
    bool found = false;
    while (!found && m->step < 900u) {
        visibility_match_step(m);
        const tide_client_status status = visibility_match_client(m);
        found = m->step >= 300u && (loaded ? status.repairs > repairs && m->wire.kind == 5u
                                           : m->wire.number > number && m->wire.kind == 4u);
        repairs = status.repairs;
        number = m->wire.number;
    }
    TIDE_REQUIRE(found);
    const uint32_t verified = visibility_match_client(m).verified_tick;
    visibility_match_reload(m, sides);
    // The whole projection, named after the repair, of a tick the client ran past
    for (uint32_t i = 0; i < 60u && m->wire.number == number; i++) visibility_match_step(m);
    TIDE_REQUIRE(m->wire.number == number + 1u && m->wire.kind == 2u && m->wire.tick > verified);
    const uint32_t world = m->wire.tick;
    // ...which the client takes: its verified world is that tick's or later within two seconds
    for (uint32_t i = 0; i < 240u && visibility_match_client(m).verified_tick < world; i++) visibility_match_step(m);
    TIDE_CHECK(visibility_match_client(m).verified_tick >= world);
    while (m->step < 1800u) visibility_match_step(m);
    TIDE_CHECK(visibility_match_caught_up(m, 30u));
    TIDE_CHECK(visibility_match_client(m).repairs > repairs);
    visibility_match_end(m);
    free(m);
}

TIDE_TEST(visibility_reload_whole_projection_is_taken_after_a_repair_the_server_hasnt_heard_of)
{
    visibility_reload_restarts(RELOAD_HOST, true);
    visibility_reload_restarts(RELOAD_HOST | RELOAD_CLIENT, true);
}

TIDE_TEST(visibility_reload_whole_projection_is_taken_in_place_of_a_repairs_hashes)
{
    visibility_reload_restarts(RELOAD_HOST, false);
    visibility_reload_restarts(RELOAD_HOST | RELOAD_CLIENT, false);
}

// The other way a client stopped: the WELCOME of the repair's hashes the
// reload ended comes again, late (they go unordered), while the client has
// some of the whole projection that took their place. It used to go back to
// the hashes, which nobody sends any more, and then start the projection
// again with none of the chunks it had said it has, which the server doesn't
// send twice.
TIDE_TEST(visibility_reload_late_welcome_of_the_repair_it_ended_costs_no_chunk)
{
    visibility_match *m = malloc(sizeof *m);
    TIDE_REQUIRE(m);
    visibility_match_start(m, 7, (tide_net_conditions){.latency = 0.08}, true);
    m->wire.hold = 4u;
    while (!m->wire.lose_number && m->step < 900u) {
        m->wire.lose_hashes = m->step >= 300u; // The hashes are named, and none of them comes
        visibility_match_step(m);
    }
    const uint32_t number = m->wire.lose_number;
    TIDE_REQUIRE(number && m->wire.number == number && m->wire.held_number == number);
    for (uint32_t i = 0; i < 2u; i++) visibility_match_step(m);
    const uint32_t repairs = visibility_match_client(m).repairs;
    m->wire.lose_hashes = false;
    visibility_match_reload(m, RELOAD_HOST);
    for (uint32_t i = 0; i < 60u && m->wire.number == number; i++) visibility_match_step(m);
    TIDE_REQUIRE(m->wire.number == number + 1u && m->wire.kind == 2u);
    const uint32_t world = m->wire.tick;
    const uint64_t before = visibility_match_client(m).world_bytes;
    for (uint32_t i = 0; i < 3u; i++) visibility_match_step(m);
    // Some of the projection, not all of it
    TIDE_REQUIRE(visibility_match_client(m).world_bytes > before && visibility_match_client(m).verified_tick < world);
    TIDE_REQUIRE(visibility_match_client(m).repairs == repairs && m->wire.held_number == number);
    m->wire.again = true;
    for (uint32_t i = 0; i < 240u && visibility_match_client(m).verified_tick < world; i++) visibility_match_step(m);
    TIDE_CHECK(!m->wire.again); // It was read
    TIDE_CHECK(visibility_match_client(m).verified_tick >= world);
    // Nothing of the projection came twice: half a megabyte of it, and not as much again
    TIDE_CHECK(visibility_match_client(m).world_bytes - before < 700000u);
    while (m->step < 1800u) visibility_match_step(m);
    TIDE_CHECK(visibility_match_caught_up(m, 30u));
    visibility_match_end(m);
    free(m);
}

// Spare chunks (session.c, MSG_SPARE). The large game's client on a network the test makes lose one SERVER in
// `lose_servers` (20: the server counts 5% lost) and nothing else. Its ticks differ from 400 on, so the loss is
// counted first (800 updates). Then the server's build swaps under a repair, which starts the whole projection
// (half a megabyte, 545 chunks): the transfer the wire watches. `lose_times` of its chunk 7 are lost. The match is
// at step 2,200 after.
enum { FORGE_NONE, FORGE_REFUSED, FORGE_OTHER_BYTES };

static void visibility_spares_run(visibility_match *m, const uint32_t lose_servers, const uint32_t lose_times,
                                  const bool lose_spares, const uint32_t forge)
{
    // (Forged spares have to come before the lost chunk is sent again: a longer round trip gives them the time)
    visibility_match_start(m, 7, (tide_net_conditions){.latency = forge ? 0.12 : 0.03}, true);
    visibility_differs_from = 400;
    visibility_differs_to = 880;
    m->wire.lose_servers = lose_servers;
    uint32_t repairs = 0;
    bool found = false;
    while (!found && m->step < 1500u) {
        visibility_match_step(m);
        const tide_client_status status = visibility_match_client(m);
        found = m->step >= 1000u && status.repairs > repairs && m->wire.kind == 5u;
        repairs = status.repairs;
    }
    TIDE_REQUIRE(found);
    m->wire.watch = m->wire.number + 1u;
    m->wire.lose_index = 7u;
    m->wire.lose_times = lose_times;
    m->wire.lose_spares = lose_spares;
    visibility_match_reload(m, RELOAD_HOST);
    for (uint32_t i = 0; i < 120u && m->wire.number < m->wire.watch; i++) visibility_match_step(m);
    TIDE_REQUIRE(m->wire.number == m->wire.watch && m->wire.kind == 2u && m->wire.watch_chunks >= 400u);
    const uint32_t world = m->wire.tick, forgeries = forge == FORGE_REFUSED ? 7u : forge == FORGE_OTHER_BYTES ? 1u : 0u;
    uint32_t forged = 0;
    for (uint32_t i = 0; i < 480u && (visibility_match_client(m).verified_tick < world || forged < forgeries); i++) {
        // What isn't a spare of this transfer, each read once before a step, once every other chunk has come and
        // while the lost one hasn't been sent again (its own spares are lost with it): the one chunk a spare taken
        // would make. Each is the lost chunk's own spare as it came (the cookie and the nonce are the connection's)
        // with one thing of it another. Offsets: the transfer's number at 16, the block's first chunk at 20, the
        // stride at 24, the group at 25, then a chunk's bytes
        const bool forging = m->wire.spare_size && forged < forgeries && m->wire.chunks_seen >= m->wire.watch_chunks;
        const uint64_t had = visibility_match_client(m).world_bytes;
        if (forging) {
            TIDE_CHECK(m->wire.index_seen == 1u); // Not sent again yet: what's forged comes first
            memcpy(m->wire.forged, m->wire.spare, m->wire.spare_size);
            m->wire.forged_size = m->wire.spare_size;
            uint8_t *f = m->wire.forged;
            const uint32_t chunks = m->wire.watch_chunks, next = m->wire.watch + 1u, uneven = 7u, past = chunks / 64u * 64u + 64u;
            if (forge == FORGE_OTHER_BYTES) memset(f + 26, 0xA5, 1000); // Its own spare, with other bytes
            else switch (forged) {
            case 0: f[24] = 0; break;                                   // No stride
            case 1: f[24] = 17; f[25] = 7; break;                       // More spares than a block has
            case 2: f[25] = f[24]; break;                               // A group past its stride
            case 3: memcpy(f + 20, &uneven, 4); break;                  // A block that starts where none does
            case 4: memcpy(f + 20, &past, 4); break;                    // ...and past the transfer's last chunk
            case 5: m->wire.forged_size -= 1u; break;                   // A chunk's bytes cut short
            default: memcpy(f + 16, &next, 4); break;                   // Of another transfer
            }
            forged++;
        }
        visibility_match_step(m);
        // Refused, it made no chunk: the client has the bytes it had
        if (forging && forge == FORGE_REFUSED) TIDE_CHECK(visibility_match_client(m).world_bytes == had);
    }
    TIDE_CHECK(visibility_match_client(m).verified_tick >= world);
    TIDE_CHECK(forged == forgeries);
    while (m->step < 2200u) visibility_match_step(m);
    TIDE_CHECK(visibility_match_caught_up(m, 30u));
}

static visibility_match *visibility_spares_case(const uint32_t lose_servers, const uint32_t lose_times, const bool lose_spares,
                                                const uint32_t forge)
{
    visibility_match *m = malloc(sizeof *m);
    if (!m) abort();
    visibility_spares_run(m, lose_servers, lose_times, lose_spares, forge);
    return m;
}

// A chunk lost once is made again from its block's spare: the server never sends it a second time. Without the
// spares (lost too), it does.
TIDE_TEST(visibility_spares_make_a_lost_chunk_again_before_its_asked_for)
{
    visibility_match *m = visibility_spares_case(20, 1, false, FORGE_NONE);
    // Five in a hundred lost: a spare for each 8 chunks, so 8 to a block of 64
    const uint32_t blocks = m->wire.watch_chunks / 64u, rest = m->wire.watch_chunks % 64u;
    TIDE_CHECK(m->wire.spares_seen == blocks * 8u + (rest + 7u) / 8u);
    // ...and none of anything else: the first world came before the loss was counted, and what's a few chunks has none
    TIDE_CHECK(m->wire.spares_all == m->wire.spares_seen);
    TIDE_CHECK(m->wire.index_seen == 1u);
    TIDE_CHECK(m->wire.chunks_seen == m->wire.watch_chunks);
    visibility_match_end(m);
    free(m);
    m = visibility_spares_case(20, 1, true, FORGE_NONE);
    TIDE_CHECK(m->wire.index_seen == 2u);
    visibility_match_end(m);
    free(m);
}

// A network that loses nothing gets no spares, and a chunk it does lose is sent again
TIDE_TEST(visibility_spares_go_only_where_datagrams_are_lost)
{
    visibility_match *m = visibility_spares_case(0, 1, false, FORGE_NONE);
    TIDE_CHECK(m->wire.spares_all == 0u && m->wire.index_seen == 2u);
    visibility_match_end(m);
    free(m);
}

// Spares that aren't ones are left: each is the lost chunk's own spare with one thing of it another, and taken it
// would make the chunk, or a chunk of other bytes (so the world would fail its check and be sent again). None makes
// one: the chunk comes when it's sent again, and the world once.
TIDE_TEST(visibility_spares_that_arent_ones_are_refused)
{
    visibility_match *m = visibility_spares_case(20, 1, true, FORGE_REFUSED);
    TIDE_CHECK(m->wire.index_seen == 2u);
    TIDE_CHECK(m->wire.worlds_again == 0u);
    visibility_match_end(m);
    free(m);
}

// The lost chunk's own spare with other bytes is taken (nothing says they aren't the server's), and makes a chunk of
// them: the world fails its check, is asked for and sent again, and the client has it
TIDE_TEST(visibility_a_spare_of_other_bytes_costs_the_world_again_and_no_more)
{
    visibility_match *m = visibility_spares_case(20, 1, true, FORGE_OTHER_BYTES);
    TIDE_CHECK(m->wire.worlds_again == 1u);
    visibility_match_end(m);
    free(m);
}

// A CLIENT says which chunks it has for 512 past the first it lacks, where it said 64: with a chunk lost twice (as
// it first goes, and as it's sent again at the first word of it), the chunks after it aren't sent again while the
// server waits for it. They were, every one past the 64th, a round trip and a half after they first went.
TIDE_TEST(visibility_chunks_past_a_lost_one_arent_sent_again)
{
    visibility_match *m = visibility_spares_case(0, 2, false, FORGE_NONE);
    TIDE_CHECK(m->wire.index_seen == 3u);
    // (The 25 chunks past the 512 it says go again as before, and the lost one twice)
    TIDE_CHECK(m->wire.chunks_seen <= m->wire.watch_chunks + 40u);
    visibility_match_end(m);
    free(m);
}

// A big command goes up in chunks of its own beside the world's, which a
// build of the same layout leaves as they are on both sides: one from each
// machine while builds swap, on a network that loses a datagram in ten. (Its
// way down, as a store's answers and shard messages go, isn't taken here: a
// player with a projection runs no command.)
TIDE_TEST(visibility_reloads_during_big_command_uploads_lose_no_command)
{
    uint8_t *large = malloc(100000);
    visibility_match *m = malloc(sizeof *m);
    TIDE_REQUIRE(large && m);
    memset(large, 0xCC, 100000);
    large[0] = 0xA5;
    visibility_private_commands = 0;
    for (uint32_t sides = RELOAD_HOST; sides <= (RELOAD_HOST | RELOAD_CLIENT); sides++) {
        for (uint32_t seed = 1; seed <= 4u; seed++) {
            visibility_match_start(m, seed, (tide_net_conditions){.latency = 0.08, .jitter = 0.04, .loss = 0.1}, false);
            while (m->step < 1500u) {
                visibility_match_step(m);
                if (m->step == 300u) {
                    TIDE_REQUIRE(visibility_match_client(m).state == TIDE_SESSION_CONNECTED);
                    large[1] = 9;
                    TIDE_CHECK(tide_session_send_command(m->client, large, 100000));
                    large[1] = 11;
                    TIDE_CHECK(tide_session_send_command(m->host, large, 100000));
                }
                if (m->step >= 302u && m->step <= 342u && m->step % 4u == 2u) visibility_match_reload(m, sides);
            }
            const tide_client_status status = visibility_match_client(m);
            const visibility_world *world = tide_session_view(m->client).verified;
            TIDE_REQUIRE(world);
            TIDE_CHECK(status.state == TIDE_SESSION_CONNECTED && status.verified_tick > 700u);
            TIDE_CHECK(world->health == 100u - 17u + 9u + 11u);
            TIDE_CHECK(world->sequence[0] == 1u && world->sequence[1] == 1u);
            visibility_match_end(m);
        }
    }
    TIDE_CHECK(visibility_private_commands == 0);
    free(m);
    free(large);
}

// One client on a network that takes no time, so a stream of repairs would
// go as fast as the session updates, while every tick from 90 to 570 differs
// on the client: how many projections it loads, how many of them in the last
// four seconds of it, and how far its verified world falls behind.
static void visibility_count_repairs(const tide_game *game, uint32_t *loads, uint32_t *late_loads, uint32_t *most_lag)
{
    visibility_loads = 0;
    visibility_continuous = true;
    tide_loopback *net = tide_loopback_create(98123);
    TIDE_REQUIRE(net);
    tide_server *server = tide_server_create(&(tide_server_desc){.game = game, .tick_rate = 60,
        .transports = {tide_loopback_endpoint(net, 1)}}, 0);
    uint32_t sampled = 3;
    tide_client *client = tide_client_create(&(tide_client_desc){.game = game,
        .transport = tide_loopback_endpoint(net, 2), .server = tide_loopback_address(1),
        .sample = visibility_sample, .user = &sampled, .lead = 2}, 0);
    TIDE_REQUIRE(server && client);
    uint32_t before = 0, late = 0;
    *most_lag = 0;
    for (uint32_t step = 1; step <= 1400; step++) {
        const double now = (double)step / 120.0;
        tide_loopback_set_time(net, now);
        tide_client_update(client, now);
        tide_server_update(server, now);
        if (step == 170) before = visibility_loads; // Tick 85: joined, and nothing hidden has happened yet
        if (step == 660) late = visibility_loads;   // Tick 330, four seconds in
        if (step == 1140) *late_loads = visibility_loads - late; // Tick 570, the last of them
        const visibility_world *confirmed = tide_client_verified_world(client);
        if (step >= 170 && confirmed && tide_server_tick(server) >= confirmed->tick) {
            const uint32_t lag = tide_server_tick(server) - confirmed->tick;
            if (lag > *most_lag) *most_lag = lag;
        }
    }
    *loads = visibility_loads - before;
    const tide_client_status status = tide_client_status_of(client);
    TIDE_CHECK(status.state == TIDE_SESSION_CONNECTED && status.resyncs == 0);
    TIDE_CHECK(status.repairs > 0 && status.repairs == visibility_loads - 1u);
    const visibility_world *w = tide_client_verified_world(client);
    TIDE_REQUIRE(w);
    TIDE_CHECK(w->tick > 600 && w->health == 563u);
    tide_client_destroy(client);
    tide_server_destroy(server);
    tide_loopback_destroy(net);
    visibility_continuous = false;
}

TIDE_TEST(visibility_repairs_back_off_while_a_client_goes_on_differing)
{
    // Its first stream repairs every tick; each that has to start again as the
    // last ends repairs half as often, to one a quarter of a second. So the
    // last four of the eight seconds take a projection every 14 ticks or so,
    // where they took one a tick (240 of them, and 489 in all)
    uint32_t loads = 0, late = 0, most_lag = 0;
    visibility_count_repairs(&visibility_game, &loads, &late, &most_lag);
    if (getenv("TIDE_VISIBILITY_METRICS")) printf("continuous: %u loads, %u in the last 4 s, most lag %u ticks\n", loads, late, most_lag);
    TIDE_CHECK(late <= 24u);
    TIDE_CHECK(loads < 180u);
    TIDE_CHECK(most_lag <= 40u); // A quarter of a second's gap, and what's on its way

    // A game that rewinds for a quarter of a second backs off to a quarter of
    // that, 3 ticks: what its clients present stays young enough to rewind to
    tide_game rewinding = visibility_game;
    rewinding.max_rewind = 0.25f;
    visibility_count_repairs(&rewinding, &loads, &late, &most_lag);
    if (getenv("TIDE_VISIBILITY_METRICS")) printf("rewinding: %u loads, %u in the last 4 s, most lag %u ticks\n", loads, late, most_lag);
    TIDE_CHECK(late >= 60u && late <= 90u);
    TIDE_CHECK(most_lag <= 8u);
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

static bool visibility_mark_far(void *world, tide_player_id player)
{
    (void)player;
    ((visibility_world *)world)->held = 1;
    return true;
}

static void visibility_far_tick(void *world, const tide_jobs *jobs)
{
    visibility_world *w = world;
    visibility_tick(world, jobs);
    if (w->held) visibility_far_held_ticks++;
    else visibility_far_full_ticks++;
    if (!w->held) w->padding[1] = w->tick;
}

static void visibility_far_tick_journal(void *world, const tide_jobs *jobs, tide_event_log *per_player, tide_event_log *all)
{
    visibility_tick_journal(world, jobs, per_player, all);
    visibility_world *w = world;
    visibility_far_full_ticks++;
    if (!w->held) w->padding[1] = w->tick;
}

static void visibility_large_repairs(uint8_t prediction, bool recover)
{
    visibility_continuous = true;
    visibility_loads = 0;
    visibility_far_full_ticks = visibility_far_held_ticks = 0;
    tide_game game = visibility_game;
    game.client_state = visibility_large_state;
    game.load_client_state = visibility_large_load;
    game.prediction = prediction;
    game.max_rewind = 0.5f;
    if (visibility_large_far) {
        game.predict_far = visibility_mark_far;
        game.tick = visibility_far_tick;
        game.tick_client_events = visibility_far_tick_journal;
    }
    tide_loopback *net = tide_loopback_create(652311);
    TIDE_REQUIRE(net);
    tide_loopback_set_conditions(net, (tide_net_conditions){.latency = 0.04, .jitter = 0.01, .loss = 0.03});
    tide_server *server = tide_server_create(&(tide_server_desc){.game = &game, .tick_rate = 60,
        .transports = {tide_loopback_endpoint(net, 1)}}, 0);
    uint32_t input = 3;
    tide_client *client = tide_client_create(&(tide_client_desc){.game = &game,
        .transport = tide_loopback_endpoint(net, 2), .server = tide_loopback_address(1),
        .sample = visibility_sample, .user = &input, .lead = 2}, 0);
    TIDE_REQUIRE(server && client);
    uint32_t max_lag = 0;
    for (uint32_t step = 1; step <= 1440; step++) {
        const double now = (double)step / 120.0;
        tide_loopback_set_time(net, now);
        if (!(recover && step >= 400u && step < 904u)) tide_client_update(client, now);
        tide_server_update(server, now);
        const visibility_world *w = tide_client_verified_world(client);
        if (w && step > 360 && tide_server_tick(server) - w->tick > max_lag) max_lag = tide_server_tick(server) - w->tick;
    }
    const tide_client_status status = tide_client_status_of(client);
    const visibility_world *w = tide_client_verified_world(client);
    TIDE_REQUIRE(w);
    if (visibility_large_far && getenv("TIDE_VISIBILITY_METRICS")) printf("far work mode %u: client full ticks %u, held ticks %u\n",
        prediction, visibility_far_full_ticks - ((const visibility_world *)tide_server_world(server))->tick, visibility_far_held_ticks);
    if (getenv("TIDE_VISIBILITY_METRICS")) printf("large recipient mode %u: verified %u, repairs %u, resyncs %u, bytes %llu, max lag %u\n",
        prediction, status.verified_tick, status.repairs, status.resyncs, (unsigned long long)status.world_bytes, max_lag);
    TIDE_CHECK(status.state == TIDE_SESSION_CONNECTED && status.resyncs == (recover ? 1u : 0u));
    TIDE_CHECK(status.verified_tick > 650u && status.repairs > (recover ? 2u : 10u));
    TIDE_CHECK(recover || max_lag < 90u);
    TIDE_CHECK(status.world_bytes < 4u * 1024u * 1024u);
    if (visibility_large_spread) TIDE_CHECK(status.world_bytes < 2u * 1024u * 1024u);
    TIDE_CHECK(w->health == 563u);
    tide_client_destroy(client);
    tide_server_destroy(server);
    tide_loopback_destroy(net);
    visibility_continuous = false;
}

TIDE_TEST(visibility_large_predicted_endpoint_repairs_reuse_matching_blocks)
{
    visibility_large_repairs(TIDE_PREDICTION_ROLLBACK, false);
}

TIDE_TEST(visibility_far_predictions_reconstruct_full_repair_comparisons)
{
    visibility_large_far = true;
    visibility_large_repairs(TIDE_PREDICTION_ROLLBACK, false);
    visibility_large_far = false;
}

TIDE_TEST(visibility_owned_predictions_reconstruct_full_repair_comparisons)
{
    visibility_large_far = true;
    visibility_large_repairs(TIDE_PREDICTION_OWNED, false);
    visibility_large_far = false;
}

TIDE_TEST(visibility_delay_reconstructs_same_changing_far_repair_payload)
{
    visibility_large_far = true;
    visibility_large_repairs(TIDE_PREDICTION_DELAY, false);
    visibility_large_far = false;
}

TIDE_TEST(visibility_large_delayed_endpoint_repairs_reuse_matching_blocks)
{
    visibility_large_repairs(TIDE_PREDICTION_DELAY, false);
}

TIDE_TEST(visibility_large_repair_stale_candidate_recovers)
{
    visibility_large_repairs(TIDE_PREDICTION_ROLLBACK, true);
}

TIDE_TEST(visibility_large_repairs_choose_cheaper_acknowledged_delta)
{
    // A handful of logical changes touch >64 KiB of random comparison blocks,
    // but remain cheap against the acknowledged endpoint. Counting missing
    // blocks alone must not force the larger patch.
    visibility_large_spread = true;
    visibility_large_repairs(TIDE_PREDICTION_ROLLBACK, false);
    visibility_large_spread = false;
}


// Destruction frame metadata is a full logical key, even when the caller only
// skips it while applying an authenticated frame or replaying a journal record.
static const tide_entity full_destroyed_key = {UINT64_C(0x123456789abcdef0), UINT64_C(0xfedcba9876543210)};
static void full_destroyed_tick(void *world, const tide_jobs *jobs)
{
    (void)jobs;
    visibility_world *w = world;
    w->tick++;
    if (w->tick == 180u) w->token_alive = 0;
}
static uint32_t full_destroyed(const void *world, const void *previous, tide_player_id player,
                               tide_entity *out, uint32_t capacity)
{
    (void)player;
    const visibility_world *w = world, *had = previous;
    if (!had || !had->token_alive || w->token_alive) return 0;
    if (out && capacity) out[0] = full_destroyed_key;
    return 1;
}

TIDE_TEST(visibility_full_entity_destroyed_frames_verify_and_replay)
{
    tide_game game = visibility_game;
    game.tick = full_destroyed_tick;
    game.tick_client_events = NULL;
    game.check_client_events = NULL;
    game.hear_visibility = NULL;
    game.client_destroyed = full_destroyed;
    visibility_continuous = false;
    tide_loopback *net = tide_loopback_create(6311);
    tide_server *server = tide_server_create(&(tide_server_desc){.game = &game,
        .transports = {tide_loopback_endpoint(net, 1)}}, 0);
    tide_client *client = tide_client_create(&(tide_client_desc){.game = &game,
        .transport = tide_loopback_endpoint(net, 2), .server = tide_loopback_address(1), .lead = 2}, 0);
    TIDE_REQUIRE(server && client);
    uint64_t repairs_before = 0;
    for (uint32_t step = 1; step <= 720; step++) {
        const double now = (double)step / 60.0;
        tide_loopback_set_time(net, now);
        tide_server_update(server, now);
        tide_client_update(client, now);
        if (step == 120u) repairs_before = tide_client_status_of(client).repairs;
    }
    const tide_client_status status = tide_client_status_of(client);
    TIDE_CHECK(status.state == TIDE_SESSION_CONNECTED && status.verified_tick > 650u);
    TIDE_CHECK(status.repairs == repairs_before && status.resyncs == 0);
    const visibility_world *verified = tide_client_verified_world(client);
    TIDE_CHECK(verified && !verified->token_alive);
    tide_client_destroy(client);
    tide_server_destroy(server);
    tide_loopback_destroy(net);

    visibility_world replay = {0}, expected = {0};
    visibility_start(&replay, 1.0f / 60.0f, NULL, 0);
    expected = replay;
    full_destroyed_tick(&expected, NULL);
    tide_writer frame = {.grows = true}, record = {.grows = true};
    tide_write_u64(&frame, visibility_hash(&expected));
    tide_write_u8(&frame, 16u); // FRAME_DESTROYED, with no input/command payload.
    tide_write_varint(&frame, 1u);
    tide_write_varint64(&frame, full_destroyed_key.birth);
    tide_write_varint64(&frame, full_destroyed_key.lineage);
    tide_write_u32(&record, 0u);
    tide_write_u32(&record, frame.size);
    tide_write_bytes(&record, frame.data, frame.size);
    tide_write_u32(&record, 0u); // No downloads.
    uint64_t wanted = 0, got = 0;
    const tide_replay_result replay_result = tide_replay_check(&game, &replay, record.data, record.size, NULL, &wanted, &got);
    if (replay_result != TIDE_REPLAY_SAME) printf("    destroyed replay: result%d tick%u frame%u record%u expected%llu got%llu\n", replay_result, replay.tick, frame.size, record.size, (unsigned long long)wanted, (unsigned long long)got);
    TIDE_CHECK(replay_result == TIDE_REPLAY_SAME);
    TIDE_CHECK(visibility_hash(&replay) == visibility_hash(&expected));
    free(frame.data);
    free(record.data);
}

// A transport that goes on sending while it hears nothing: what a player's machine is when datagrams reach the
// server and none come back.
typedef struct visibility_deaf {
    tide_transport inner;
    bool deaf;
} visibility_deaf;

static void visibility_deaf_send(void *self, tide_address to, const void *data, uint32_t size)
{
    visibility_deaf *d = self;
    d->inner.send(d->inner.self, to, data, size);
}

static uint32_t visibility_deaf_receive(void *self, tide_address *from, void *data, uint32_t capacity)
{
    visibility_deaf *d = self;
    for (;;) {
        const uint32_t size = d->inner.receive(d->inner.self, from, data, capacity);
        if (!size || !d->deaf) return size;
    }
}

static void visibility_deaf_close(void *self)
{
    visibility_deaf *d = self;
    if (d->inner.close) d->inner.close(d->inner.self);
}

// A match with no secret: nothing a player's own run of a tick gets wrong, so no player asks for a repair.
static void visibility_calm_start(void *world, float dt, const void *start, uint32_t seed)
{
    visibility_start(world, dt, start, seed);
    ((visibility_world *)world)->secret = 0;
}

TIDE_TEST(visibility_a_player_too_far_behind_takes_the_world_it_never_asked_for)
{
    // A player that hears nothing for longer than the server keeps ticks (4 s) and goes on sending is sent its world
    // again, unasked. What it sends before it hears of that says it wants nothing, as it did all along: taken as
    // "it needs nothing", that stopped the transfer before any of it went, and left the player with neither the
    // world nor the ticks before it, the same every 4 s for good. (In a match where nothing has it ask for a repair:
    // one in the midst of a repair says it wants one all along, which kept the transfer going.)
    visibility_loads = visibility_starts = visibility_private_inputs = 0;
    visibility_private_commands = 0;
    visibility_deaths = visibility_hides = 0;
    visibility_bulk_filters = visibility_slot_filters = 0;
    visibility_continuous = false;
    tide_loopback *net = tide_loopback_create(541254);
    TIDE_REQUIRE(net);
    tide_loopback_set_conditions(net, (tide_net_conditions){.latency = 0.05});
    tide_game calm = visibility_game;
    calm.start = visibility_calm_start;
    tide_server *server = tide_server_create(&(tide_server_desc){.game = &calm,
        .transports = {tide_loopback_endpoint(net, 1)}}, 0);
    TIDE_REQUIRE(server);
    uint32_t sampled[2] = {3, 7};
    visibility_deaf deaf = {.inner = tide_loopback_endpoint(net, 2)};
    tide_client *clients[2];
    clients[0] = tide_client_create(&(tide_client_desc){.game = &calm,
        .transport = {.self = &deaf, .send = visibility_deaf_send, .receive = visibility_deaf_receive,
                      .close = visibility_deaf_close},
        .server = tide_loopback_address(1), .sample = visibility_sample, .user = &sampled[0], .lead = 2}, 0);
    clients[1] = tide_client_create(&(tide_client_desc){.game = &calm,
        .transport = tide_loopback_endpoint(net, 3), .server = tide_loopback_address(1),
        .sample = visibility_sample, .user = &sampled[1], .lead = 2}, 0);
    TIDE_REQUIRE(clients[0] && clients[1]);
    uint32_t behind_after = 0;
    for (uint32_t step = 1; step <= 1320; step++) { // 11 s: deaf from 3 s to 7.5 s, less than a connection's timeout
        const double now = (double)step / 120.0;
        tide_loopback_set_time(net, now);
        deaf.deaf = step >= 360 && step < 900;
        for (uint32_t p = 0; p < 2; p++) tide_client_update(clients[p], now);
        tide_server_update(server, now);
        if (step == 1080) { // A second and a half after it hears again
            const tide_client_status status = tide_client_status_of(clients[0]);
            behind_after = tide_server_tick(server) - status.verified_tick;
        }
    }
    for (uint32_t p = 0; p < 2; p++) {
        const tide_client_status status = tide_client_status_of(clients[p]);
        TIDE_CHECK(status.state == TIDE_SESSION_CONNECTED);
        TIDE_CHECK(status.resyncs == (p == 0 ? 1u : 0u)); // Its world again, once
        TIDE_CHECK(tide_server_tick(server) - status.verified_tick < 30u);
    }
    TIDE_CHECK(behind_after < 30u);
    for (uint32_t p = 0; p < 2; p++) tide_client_destroy(clients[p]);
    tide_server_destroy(server);
    tide_loopback_destroy(net);
}

// Everything a player may have, and 5 MiB after it: more than an update reads of whole projections before the rest
// wait (session.c's WHOLE_UPDATE_MOST, 4 MiB), so each is an update's allowance by itself. Which player's the
// session asked for, in order.
#define VISIBILITY_BULK (5u * 1024u * 1024u)
static uint8_t visibility_bulky_asked[4096];
static uint32_t visibility_bulky_count;

static uint8_t *visibility_bulky_state(const void *world, tide_player_id player, uint32_t *size)
{
    uint32_t small_size = 0;
    uint8_t *small = visibility_state(world, player, &small_size);
    if (!small) return NULL;
    uint8_t *out = calloc(1, small_size + VISIBILITY_BULK);
    if (out) memcpy(out, small, small_size);
    free(small);
    if (!out) return NULL;
    if (visibility_bulky_count < sizeof visibility_bulky_asked) {
        visibility_bulky_asked[visibility_bulky_count] = (uint8_t)tide_player_index(player);
    }
    visibility_bulky_count++;
    *size = small_size + VISIBILITY_BULK;
    return out;
}

static bool visibility_bulky_load(void *world, const uint8_t *data, uint32_t size)
{
    if (size != sizeof(visibility_world) + VISIBILITY_BULK) return false;
    return visibility_load(world, data, sizeof(visibility_world));
}

TIDE_TEST(visibility_whole_projections_wait_their_turn_past_an_updates_allowance)
{
    // Two players join in the same update, and at tick 90 both find their worlds wrong in the same tick (a hidden
    // effect neither can predict), which this game repairs by a hash offer: a whole projection each, and another
    // when each says what it lacks. An update prepares the one that asked first and the other waits for the next
    // update, whatever asked for it: no update reads whole projections for two players.
    visibility_loads = visibility_starts = visibility_private_inputs = 0;
    visibility_private_commands = 0;
    visibility_deaths = visibility_hides = 0;
    visibility_bulk_filters = visibility_slot_filters = 0;
    visibility_continuous = false;
    visibility_bulky_count = 0;
    tide_game bulky = visibility_game;
    bulky.client_state = visibility_bulky_state;
    bulky.load_client_state = visibility_bulky_load;
    tide_loopback *net = tide_loopback_create(541254);
    TIDE_REQUIRE(net);
    tide_server *server = tide_server_create(&(tide_server_desc){.game = &bulky,
        .transports = {tide_loopback_endpoint(net, 1)}}, 0);
    TIDE_REQUIRE(server);
    uint32_t sampled[2] = {3, 7};
    tide_client *clients[2];
    for (uint32_t p = 0; p < 2; p++) clients[p] = tide_client_create(&(tide_client_desc){.game = &bulky,
        .transport = tide_loopback_endpoint(net, p + 2u), .server = tide_loopback_address(1),
        .sample = visibility_sample, .user = &sampled[p], .lead = 2}, 0);
    TIDE_REQUIRE(clients[0] && clients[1]);
    uint32_t updates_for_both = 0, updates_with_any = 0, first_asked = UINT32_MAX;
    bool asked[2] = {false, false};
    for (uint32_t step = 1; step <= 480; step++) {
        const double now = (double)step / 120.0;
        tide_loopback_set_time(net, now);
        for (uint32_t p = 0; p < 2; p++) tide_client_update(clients[p], now);
        const uint32_t before = visibility_bulky_count;
        tide_server_update(server, now);
        TIDE_REQUIRE(visibility_bulky_count <= sizeof visibility_bulky_asked);
        bool of[2] = {false, false};
        for (uint32_t i = before; i < visibility_bulky_count; i++) of[visibility_bulky_asked[i]] = true;
        updates_for_both += of[0] && of[1];
        updates_with_any += of[0] || of[1];
        if (first_asked == UINT32_MAX && (of[0] || of[1])) first_asked = step;
        // The second to join has its world the update after the first's: it waited one update, not longer
        if (first_asked != UINT32_MAX && step <= first_asked + 1u) {
            asked[0] |= of[0];
            asked[1] |= of[1];
        }
    }
    TIDE_CHECK(updates_for_both == 0);
    TIDE_CHECK(asked[0] && asked[1]);
    TIDE_CHECK(updates_with_any >= 6u); // The two worlds, and each one's repair at tick 90: its offer and what it lacked
    for (uint32_t p = 0; p < 2; p++) {
        const tide_client_status status = tide_client_status_of(clients[p]);
        TIDE_CHECK(status.state == TIDE_SESSION_CONNECTED);
        TIDE_CHECK(status.resyncs == 0);
        TIDE_CHECK(status.repairs >= 1u);
        TIDE_CHECK(tide_server_tick(server) - status.verified_tick < 30u);
        const visibility_world *w = tide_client_verified_world(clients[p]);
        TIDE_REQUIRE(w);
        TIDE_CHECK(w->health == 83u); // The hidden effect, repaired
    }
    for (uint32_t p = 0; p < 2; p++) tide_client_destroy(clients[p]);
    tide_server_destroy(server);
    tide_loopback_destroy(net);
}
