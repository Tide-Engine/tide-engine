#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "tide/session.h"
#include "tide_test.h"

// An input-driven plain-data match makes replay and presentation timestamps
// observable without depending on any generated game's interpolation code.
typedef struct correction_world {
    uint32_t magic;
    uint32_t tick;
    uint32_t players;
    int32_t input[3];
    int32_t position;
} correction_world;

#define CORRECTION_MAGIC 0xC011EC7u
static uint32_t correction_copies, correction_frees;
static const void *correction_bad_hash;
static uint64_t correction_clock_ns;
static const void *correction_borrowed[2];
static uint32_t correction_borrowed_freed;

static void correction_start(void *world, float dt, const void *start, uint32_t seed)
{
    (void)dt;
    (void)start;
    (void)seed;
    *(correction_world *)world = (correction_world){.magic = CORRECTION_MAGIC};
}

static void correction_tick(void *world, const tide_jobs *jobs)
{
    (void)jobs;
    correction_world *w = world;
    w->position += w->input[0] + w->input[1] * 2;
    w->tick++;
}

static void correction_copy(void *to, const void *from)
{
    correction_copies++;
    memcpy(to, from, sizeof(correction_world));
}

static void correction_free(void *world)
{
    correction_frees++;
    for (uint32_t i = 0; i < 2u; i++) {
        if (correction_borrowed[i] == world) {
            correction_borrowed[i] = NULL;
            correction_borrowed_freed++;
        }
    }
    ((correction_world *)world)->magic = 0;
}

static uint64_t correction_hash(const void *world)
{
    const uint64_t hash = tide_hash(world, sizeof(correction_world));
    if (world != correction_bad_hash) return hash;
    correction_bad_hash = NULL; // One deliberate divergence asks for a new snapshot.
    return hash ^ 1u;
}

static uint8_t *correction_pack(const void *world, const void *base, const uint8_t *need,
                                uint32_t need_size, uint32_t *size)
{
    (void)base;
    (void)need;
    (void)need_size;
    *size = sizeof(correction_world);
    uint8_t *bytes = malloc(*size);
    if (bytes) memcpy(bytes, world, *size);
    return bytes;
}

static bool correction_unpack(void *world, const void *base, const uint8_t *bytes, uint32_t size)
{
    (void)base;
    if (size != sizeof(correction_world)) return false;
    memcpy(world, bytes, size);
    return ((correction_world *)world)->magic == CORRECTION_MAGIC;
}

static void correction_joined(void *world, tide_player_id player)
{
    ((correction_world *)world)->players |= 1u << (player.id - 1u);
}

static void correction_left(void *world, tide_player_id player)
{
    ((correction_world *)world)->players &= ~(1u << (player.id - 1u));
}

static void correction_input(void *world, tide_player_id player, const void *input)
{
    ((correction_world *)world)->input[player.id - 1u] = *(const int32_t *)input;
}

static void correction_server_input(void *world, const void *input)
{
    ((correction_world *)world)->input[2] = *(const int32_t *)input;
}

static uint32_t correction_write_input(const void *input, uint8_t *out, uint32_t capacity)
{
    if (capacity < 4u) return 0;
    tide_writer w = {out, capacity, 0, false, false};
    tide_write_u32(&w, (uint32_t)*(const int32_t *)input);
    return w.size;
}

static bool correction_read_input(const uint8_t *data, uint32_t size, void *input)
{
    if (size != 4u) return false;
    tide_reader r = {data, size, 0, false};
    *(int32_t *)input = (int32_t)tide_read_u32(&r);
    return true;
}

static void correction_world_input(const void *world, uint32_t slot, void *input)
{
    *(int32_t *)input = ((const correction_world *)world)->input[slot];
}

static const tide_game correction_game = {
    .hash = 0xC011EC7,
    .world_size = sizeof(correction_world),
    .input_size = sizeof(int32_t),
    .max_input_bytes = 4,
    .start = correction_start,
    .tick = correction_tick,
    .copy_world = correction_copy,
    .free_world = correction_free,
    .hash_world = correction_hash,
    .pack_delta = correction_pack,
    .unpack_delta = correction_unpack,
    .player_joined = correction_joined,
    .player_left = correction_left,
    .set_input = correction_input,
    .set_server_input = correction_server_input,
    .write_input = correction_write_input,
    .read_input = correction_read_input,
    .world_input = correction_world_input,
    .max_players = 2,
    .host_migration = true,
};

typedef struct correction_observer {
    tide_client *client;
    bool marked;
    bool has_previous;
    correction_world before, previous;
    uint32_t tick;
    float alpha;
    uint32_t corrections, resets, catchups;
} correction_observer;

static void correction_observe(void *user, const tide_view_correction *correction)
{
    correction_observer *o = user;
    if (correction->reset) {
        o->resets++;
        return;
    }
    o->corrections++;
    if (tide_client_status_of(o->client).verified_tick > correction->tick + 1u) o->catchups++;
    TIDE_REQUIRE(o->marked);
    TIDE_CHECK(correction->tick == o->tick);
    TIDE_CHECK(correction->alpha == o->alpha);
    const correction_world *before = correction->before;
    const correction_world *after = correction->after;
    TIDE_REQUIRE(before && after);
    TIDE_CHECK(before->magic == CORRECTION_MAGIC && after->magic == CORRECTION_MAGIC);
    TIDE_CHECK(before->tick == o->tick && after->tick == o->tick);
    TIDE_CHECK(memcmp(before, &o->before, sizeof *before) == 0);
    TIDE_CHECK(before != after && before != tide_client_world(o->client));
    TIDE_CHECK((correction->before_previous != NULL) == o->has_previous);
    TIDE_CHECK((correction->after_previous != NULL) == o->has_previous);
    if (o->has_previous) {
        const correction_world *was = correction->before_previous;
        const correction_world *now = correction->after_previous;
        TIDE_REQUIRE(was && now);
        TIDE_CHECK(was->magic == CORRECTION_MAGIC && now->magic == CORRECTION_MAGIC);
        TIDE_CHECK(was->tick + 1u == o->tick && now->tick + 1u == o->tick);
        TIDE_CHECK(memcmp(was, &o->previous, sizeof *was) == 0);
    }
    TIDE_CHECK(!correction_borrowed[0] && !correction_borrowed[1]);
    correction_borrowed[0] = correction->before;
    correction_borrowed[1] = correction->before_previous;
}

static void correction_sample(void *user, uint32_t tick, void *input)
{
    const bool changing = user != NULL;
    *(int32_t *)input = changing ? (int32_t)(tick / 7u % 3u) - 1 : 0;
}

static void correction_sample_zero(void *user, uint32_t tick, void *input)
{
    (void)user;
    (void)tick;
    *(int32_t *)input = 0;
}

typedef struct correction_result {
    uint32_t copies, frees, corrections, resets, borrowed_freed, delay, resyncs, catchups;
} correction_result;

static uint64_t correction_clock(void)
{
    correction_clock_ns += 2000000u;
    return correction_clock_ns;
}

// With `burst`, the client's next updates after frame 199 are 222 and 257:
// about 12 ticks, then 19. The first gap stays inside its sampled input lead,
// so its normal 8-tick allowance leaves confirmed work without replacing
// presentation. The second update has an allowance of about 25 ticks from
// twice the shorter interval: enough to reconstruct the pair drawn at 222
// and confirm past it. Equal 35-frame gaps would instead miss inputs on the
// first update, exhausting its allowance and correctly entering catch-up,
// which discards that presentation anchor rather than correcting it.
#define BURST_FIRST_GAP 23u
#define BURST_SECOND_GAP 35u

static bool correction_updates(const bool burst, const uint32_t frame)
{
    return !burst || frame < 200u || frame >= 199u + BURST_FIRST_GAP + BURST_SECOND_GAP
        || frame == 199u + BURST_FIRST_GAP;
}

static correction_result correction_run(bool callback, bool mark, bool changing, bool budget, bool resync, bool burst)
{
    correction_copies = correction_frees = correction_borrowed_freed = 0;
    correction_clock_ns = 0;
    correction_bad_hash = NULL;
    memset(correction_borrowed, 0, sizeof correction_borrowed);
    correction_observer observer = {0};
    tide_loopback *net = tide_loopback_create(88912);
    tide_loopback_set_conditions(net, (tide_net_conditions){.latency = 0.035});
    tide_server *server = tide_server_create(&(tide_server_desc){.game = &correction_game,
        .transports = {tide_loopback_endpoint(net, 1)}}, 0);
    tide_client *client = tide_client_create(&(tide_client_desc){.game = &correction_game,
        .transport = tide_loopback_endpoint(net, 2), .server = tide_loopback_address(1),
        .sample = correction_sample_zero, .lead = 8, .user = &observer,
        .correct_views = callback ? correction_observe : NULL,
        .now_ns = budget ? correction_clock : NULL, .resim_budget = 0.001}, 0);
    // Keep this machine's input constant; its user pointer belongs to the
    // observer. The other player's changing input is what it cannot predict.
    observer.client = client;
    tide_client *other = tide_client_create(&(tide_client_desc){.game = &correction_game,
        .transport = tide_loopback_endpoint(net, 3), .server = tide_loopback_address(1),
        .sample = correction_sample, .lead = 8, .user = changing ? net : NULL}, 0);
    uint32_t delay = 0;
    for (uint32_t frame = 1; frame <= 400u; frame++) {
        const double now = (double)frame * 0.009;
        tide_loopback_set_time(net, now);
        const uint32_t callbacks = observer.corrections;
        if (resync && frame == 200u) correction_bad_hash = tide_client_world(client);
        if (correction_updates(burst, frame)) tide_client_update(client, now);
        TIDE_CHECK(observer.corrections - callbacks <= 1u);
        TIDE_CHECK(!correction_borrowed[0] && !correction_borrowed[1]);
        tide_client_update(other, now);
        tide_server_update(server, now);
        const tide_view_worlds view = tide_client_view(client);
        const uint32_t now_delay = tide_client_status_of(client).delay_ticks;
        if (now_delay > delay) delay = now_delay;
        if (mark && frame > 80u && (!resync || frame <= 100u) && correction_updates(burst, frame) && view.current) {
            observer.marked = true;
            observer.before = *(const correction_world *)view.current;
            observer.has_previous = view.previous != NULL;
            if (view.previous) observer.previous = *(const correction_world *)view.previous;
            observer.tick = observer.before.tick;
            observer.alpha = view.alpha;
            tide_client_mark_presented(client);
        }
    }
    TIDE_CHECK(tide_client_status_of(client).state == TIDE_SESSION_CONNECTED);
    const uint32_t resyncs = tide_client_status_of(client).resyncs;
    TIDE_CHECK(resyncs == (resync ? 1u : 0u));
    tide_client_destroy(client);
    tide_client_destroy(other);
    tide_server_destroy(server);
    tide_loopback_destroy(net);
    return (correction_result){correction_copies, correction_frees,
        observer.corrections, observer.resets, correction_borrowed_freed, delay, resyncs, observer.catchups};
}

TIDE_TEST(view_corrections_match_the_last_drawn_timestamp_and_free_borrowed_snapshots)
{
    const correction_result result = correction_run(true, true, true, false, false, false);
    TIDE_CHECK(result.corrections > 10u);
    TIDE_CHECK(result.resets == 1u);
    TIDE_CHECK(result.borrowed_freed == result.corrections * 2u);
}

TIDE_TEST(view_corrections_dont_copy_worlds_without_a_presented_anchor)
{
    const correction_result disabled = correction_run(false, false, true, false, false, false);
    const correction_result unpresented = correction_run(true, false, true, false, false, false);
    TIDE_CHECK(disabled.copies == unpresented.copies);
    TIDE_CHECK(disabled.frees == unpresented.frees);
    TIDE_CHECK(unpresented.corrections == 0u && unpresented.borrowed_freed == 0u);
}

TIDE_TEST(view_corrections_dont_copy_worlds_for_unchanged_verification)
{
    const correction_result disabled = correction_run(false, false, false, false, false, false);
    const correction_result presented = correction_run(true, true, false, false, false, false);
    TIDE_CHECK(disabled.copies == presented.copies);
    TIDE_CHECK(disabled.frees == presented.frees);
    TIDE_CHECK(presented.corrections == 0u);
}

TIDE_TEST(view_corrections_still_use_presented_timestamps_when_the_replay_budget_adds_delay)
{
    const correction_result result = correction_run(true, true, true, true, false, false);
    TIDE_CHECK(result.delay > 0u);
    TIDE_CHECK(result.corrections > 3u);
    TIDE_CHECK(result.borrowed_freed == result.corrections * 2u);
}

TIDE_TEST(view_corrections_reset_when_resync_lacks_the_presented_history)
{
    const correction_result result = correction_run(true, true, false, false, true, false);
    TIDE_CHECK(result.resyncs == 1u);
    TIDE_CHECK(result.resets == 2u);
    TIDE_CHECK(result.corrections == 0u && result.borrowed_freed == 0u);
}

TIDE_TEST(view_corrections_keep_reconstructed_endpoints_when_a_burst_confirms_past_them)
{
    const correction_result result = correction_run(true, true, true, false, false, true);
    TIDE_CHECK(result.catchups > 0u);
    TIDE_CHECK(result.corrections > 10u);
    TIDE_CHECK(result.resets == 1u);
    TIDE_CHECK(result.borrowed_freed == result.corrections * 2u);
}

static void correction_observe_reset(void *user, const tide_view_correction *correction)
{
    correction_observer *observer = user;
    if (correction->reset) observer->resets++;
    else observer->corrections++;
}

TIDE_TEST(view_corrections_migration_keeps_stale_errors_then_resets_the_new_timeline)
{
    correction_observer observer = {0};
    tide_loopback *net = tide_loopback_create(3327);
    tide_session *host = tide_session_create(&(tide_session_desc){.game = &correction_game});
    tide_session *remote = tide_session_create(&(tide_session_desc){.game = &correction_game,
        .user = &observer, .correct_views = correction_observe_reset});
    tide_session_start(host, NULL, 0);
    TIDE_REQUIRE(tide_session_open(host, tide_loopback_endpoint(net, 1)));
    tide_session_set_room(host, "ABCDEF", "key");
    tide_session_join(remote, tide_loopback_endpoint(net, 2), tide_loopback_address(1), 0);
    double now = 0;
    for (uint32_t frame = 0; frame < 200u; frame++) {
        now += 0.01;
        tide_loopback_set_time(net, now);
        tide_session_update(host, now);
        tide_session_update(remote, now);
        tide_session_mark_presented(remote);
    }
    TIDE_REQUIRE(tide_session_status_of(remote).client.state == TIDE_SESSION_CONNECTED);
    const uint32_t resets = observer.resets;
    tide_session_leave(host);
    for (uint32_t frame = 0; frame < 10u; frame++) {
        now += 0.01;
        tide_loopback_set_time(net, now);
        tide_session_update(remote, now);
        tide_session_mark_presented(remote);
    }
    char code[TIDE_ROOM_CODE_LENGTH + 1], key[TIDE_ROOM_KEY_LENGTH + 1];
    TIDE_REQUIRE(tide_session_migrating(remote, code, key));
    TIDE_CHECK(observer.resets == resets);
    const tide_view_worlds stale = tide_session_view(remote);
    TIDE_REQUIRE(stale.current);
    TIDE_CHECK(stale.previous == NULL && stale.alpha == 1.0f);
    tide_session_take_over(remote, tide_loopback_endpoint(net, 3), now);
    for (uint32_t frame = 0; frame < 80u; frame++) {
        now += 0.01;
        tide_loopback_set_time(net, now);
        tide_session_update(remote, now);
        tide_session_mark_presented(remote);
    }
    TIDE_CHECK(tide_session_status_of(remote).server);
    TIDE_CHECK(tide_session_status_of(remote).client.state == TIDE_SESSION_CONNECTED);
    TIDE_CHECK(observer.resets == resets + 1u);
    tide_session_destroy(remote);
    tide_session_destroy(host);
    tide_loopback_destroy(net);
}

TIDE_TEST(view_corrections_session_replacement_resets_and_read_accessors_do_not_mark)
{
    correction_observer observer = {0};
    tide_session *session = tide_session_create(&(tide_session_desc){.game = &correction_game,
        .user = &observer, .correct_views = correction_observe});
    tide_session_start(session, NULL, 0);
    for (uint32_t frame = 1; frame <= 60u; frame++) {
        tide_session_update(session, (double)frame / 60.0);
        (void)tide_session_view(session);
    }
    TIDE_CHECK(observer.resets == 1u && observer.corrections == 0u);
    tide_session_mark_presented(session);
    tide_session_start(session, NULL, 1.0);
    TIDE_CHECK(observer.resets == 2u);
    for (uint32_t frame = 1; frame <= 20u; frame++) tide_session_update(session, 1.0 + (double)frame / 60.0);
    TIDE_CHECK(observer.resets == 3u);
    tide_session_leave(session);
    TIDE_CHECK(observer.resets == 4u);
    tide_session_destroy(session);
}
