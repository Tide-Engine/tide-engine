#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/session.h"
#include "tide_test.h"

// Ticks beside the frame (tide_session_desc.alongside), with a pool whose leading "thread" is this one: it runs
// the work it's handed when it's waited for (`defer`), which is the latest a real one could, or at once, which is
// the soonest. Either way a match has to come out as it does with its ticks in the update, and views a frame
// behind. A real pool's is platform/tests/alongside.c.

typedef struct beside_world {
    uint32_t ticks;
    uint32_t sequence;
    uint32_t count;
    uint64_t order; // Takes in the tick each command ran in
} beside_world;

static void beside_start(void *world, float dt, const void *start, uint32_t seed)
{
    (void)dt;
    (void)start;
    (void)seed;
    memset(world, 0, sizeof(beside_world));
}

static uint32_t beside_running; // Ticks inside tide_game.tick now: never 2
static void beside_tick(void *world, const tide_jobs *jobs)
{
    (void)jobs;
    beside_running++;
    ((beside_world *)world)->ticks++;
    beside_running--;
}

static void beside_copy(void *to, const void *from)
{
    memcpy(to, from, sizeof(beside_world));
}

static uint64_t beside_hash(const void *world)
{
    const uint8_t *data = world;
    uint64_t hash = 14695981039346656037ull;
    for (size_t i = 0; i < sizeof(beside_world); i++) hash = (hash ^ data[i]) * 1099511628211ull;
    return hash;
}

static uint32_t beside_pack(const void *world, uint8_t *out, uint32_t capacity)
{
    if (out && capacity >= sizeof(beside_world)) memcpy(out, world, sizeof(beside_world));
    return (uint32_t)sizeof(beside_world);
}

static bool beside_unpack(void *world, const uint8_t *data, uint32_t size)
{
    if (size != sizeof(beside_world)) return false;
    memcpy(world, data, size);
    return true;
}

static uint8_t *beside_pack_delta(const void *world, const void *base, const uint8_t *need, uint32_t need_size,
                                 bool assets, uint32_t *size)
{
    (void)base;
    (void)need;
    (void)need_size;
    (void)assets;
    *size = (uint32_t)sizeof(beside_world);
    uint8_t *data = malloc(*size);
    if (data) memcpy(data, world, *size);
    return data;
}

static bool beside_unpack_delta(void *world, const void *base, const uint8_t *data, uint32_t size)
{
    (void)base;
    return beside_unpack(world, data, size);
}

static void beside_player(void *world, tide_player_id player)
{
    (void)world;
    (void)player;
}

static bool beside_check(const uint8_t *data, uint32_t size, uint32_t *channel)
{
    if (!data || size != 2u || data[0] != 0xA5u) return false;
    if (channel) *channel = 0;
    return true;
}

static uint32_t beside_sequence(const void *world, tide_player_id player, uint32_t channel)
{
    return channel == 0 && tide_player_index(player) == 0 ? ((const beside_world *)world)->sequence : 0u;
}

static bool beside_command(void *world, tide_player_id sender, uint32_t channel, uint32_t sequence, const uint8_t *data,
                           uint32_t size)
{
    beside_world *w = world;
    if (channel != 0 || tide_player_index(sender) != 0 || !beside_check(data, size, NULL) || sequence != w->sequence + 1u) return false;
    w->sequence = sequence;
    w->count++;
    w->order = w->order * 65599u + data[1] + (uint64_t)w->ticks * 17u;
    return true;
}

static const tide_game beside_game = {
    .hash = 0xBE51DE,
    .world_size = sizeof(beside_world),
    .start = beside_start,
    .tick = beside_tick,
    .copy_world = beside_copy,
    .hash_world = beside_hash,
    .pack_world = beside_pack,
    .unpack_world = beside_unpack,
    .pack_delta = beside_pack_delta,
    .unpack_delta = beside_unpack_delta,
    .player_joined = beside_player,
    .player_left = beside_player,
    .check_command = beside_check,
    .command_sequence = beside_sequence,
    .command = beside_command,
    .max_players = 2,
};

typedef struct beside_pool {
    void (*work)(void *context);
    void *context;
    bool defer;
    uint32_t handed, waited, ran_late;
} beside_pool;

static void beside_run(void *self, void (*work)(void *context, uint32_t thread), void *context)
{
    (void)self;
    work(context, 0);
}

static bool beside_lead(void *self, void (*work)(void *context), void *context)
{
    beside_pool *p = self;
    TIDE_CHECK(!p->work); // One at a time
    p->handed++;
    if (p->defer) {
        p->work = work;
        p->context = context;
    } else work(context);
    return true;
}

static void beside_led(void *self)
{
    beside_pool *p = self;
    p->waited++;
    if (!p->work) return;
    void (*const work)(void *) = p->work;
    p->work = NULL;
    p->ran_late++;
    work(p->context);
}

static tide_jobs beside_jobs(beside_pool *pool)
{
    return (tide_jobs){.self = pool, .threads = 2, .run = beside_run, .lead = beside_lead, .led = beside_led};
}

#define BESIDE_FRAMES 600
#define BESIDE_STEP (1.0 / 144.0)

// A session with its match started, `frames` frames in: each an update and its views.
static tide_session *beside_session(const tide_jobs *jobs, const bool alongside)
{
    tide_session *s = tide_session_create(&(tide_session_desc){.game = &beside_game, .jobs = jobs, .alongside = alongside});
    tide_session_start(s, NULL, 0);
    return s;
}

static tide_view_worlds beside_frame(tide_session *s, const int frame)
{
    if (frame % 7 == 3) { // Something for the ticks to take in
        const uint8_t data[] = {0xA5, (uint8_t)(frame * 31)};
        (void)tide_session_send_command(s, data, sizeof data);
    }
    tide_session_update(s, (double)frame * BESIDE_STEP);
    return tide_session_view(s);
}

static void beside_same_match(const bool defer)
{
    beside_pool pool = {.defer = defer};
    const tide_jobs jobs = beside_jobs(&pool);
    tide_session *in_update = beside_session(NULL, false);
    tide_session *beside = beside_session(&jobs, true);
    for (int frame = 0; frame < BESIDE_FRAMES; frame++) {
        beside_frame(in_update, frame);
        beside_frame(beside, frame);
        TIDE_CHECK(beside_running == 0);
    }
    TIDE_CHECK(pool.handed > 200u); // 600 frames of 144 a second are 250 ticks, all but the first few beside
    // The server's world is asked for as it is after the tick that's running
    const beside_world *a = tide_session_server_world(in_update);
    const beside_world *b = tide_session_server_world(beside);
    TIDE_REQUIRE(a && b);
    TIDE_CHECK(a->ticks > 240u && a->count > 70u);
    TIDE_CHECK(memcmp(a, b, sizeof *a) == 0);
    TIDE_CHECK(!pool.work);
    if (defer) TIDE_CHECK(pool.ran_late == pool.handed); // None ran before it was needed, and each was
    tide_session_destroy(in_update);
    tide_session_destroy(beside);
}

TIDE_TEST(alongside_ticks_make_the_match_ticks_in_the_update_make)
{
    beside_same_match(true);
    beside_same_match(false);
}

// What views show with ticks beside the frame is what they showed a frame earlier without: the same worlds,
// the same way between them.
static void beside_views_behind(const bool defer)
{
    beside_pool pool = {.defer = defer};
    const tide_jobs jobs = beside_jobs(&pool);
    tide_session *in_update = beside_session(NULL, false);
    tide_session *beside = beside_session(&jobs, true);
    uint32_t before_ticks = 0;
    float before_alpha = 0.0f;
    bool before = false;
    uint32_t compared = 0, between = 0;
    for (int frame = 0; frame < BESIDE_FRAMES; frame++) {
        const tide_view_worlds plain = beside_frame(in_update, frame);
        const tide_view_worlds lagged = beside_frame(beside, frame);
        // Asked again in the frame, it's the same moment
        const tide_view_worlds again = tide_session_view(beside);
        TIDE_CHECK(again.current == lagged.current && again.alpha == lagged.alpha);
        if (frame > 100 && before && lagged.current) { // Once both are level and under way
            const beside_world *shown = lagged.current;
            if (shown->ticks != before_ticks) printf("    frame %d defer %d: ticks %u/%u alpha %.6f/%.6f\n", frame, (int)defer, (unsigned)shown->ticks, (unsigned)before_ticks, (double)lagged.alpha, (double)before_alpha);
            TIDE_CHECK(shown->ticks == before_ticks);
            TIDE_CHECK(lagged.alpha > before_alpha - 1e-4f && lagged.alpha < before_alpha + 1e-4f);
            compared++;
            if (lagged.alpha > 0.01f && lagged.alpha < 0.99f) between++;
        }
        before = plain.current != NULL;
        if (before) {
            before_ticks = ((const beside_world *)plain.current)->ticks;
            before_alpha = plain.alpha;
        }
    }
    TIDE_CHECK(compared > 400u && between > 300u); // And never held at a tick while the next one ran
    tide_session_destroy(in_update);
    tide_session_destroy(beside);
}

TIDE_TEST(alongside_views_show_the_frame_before)
{
    beside_views_behind(true);
    beside_views_behind(false);
}

// A tick begun by an update between two frames (a host that updates while it waits to present) is left to run
// through the next frame, whose views are of a moment before it: nothing waits for it there. The frame after
// needs it, and gets it.
TIDE_TEST(alongside_tick_begun_between_frames_is_not_waited_for_by_the_next)
{
    beside_pool pool = {.defer = true};
    const tide_jobs jobs = beside_jobs(&pool);
    tide_session *s = beside_session(&jobs, true);
    double now = 0.0;
    for (int frame = 0; frame < 200; frame++) { // Under way, on frames that fall on no tick's time
        now = (double)frame * BESIDE_STEP;
        tide_session_update(s, now);
        (void)tide_session_view(s);
    }
    // A frame the next tick is due some way after, and well before the frame after it
    const beside_world *world = NULL;
    double until = 0.0;
    for (int frame = 0; frame < 100; frame++) {
        now += BESIDE_STEP;
        tide_session_update(s, now);
        (void)tide_session_view(s);
        world = tide_session_server_world(s); // Nothing in the air
        until = tide_session_until_tick(s);
        if (until > 0.25 * BESIDE_STEP && until < 0.6 * BESIDE_STEP) break;
    }
    TIDE_REQUIRE(world && !pool.work && until > 0.25 * BESIDE_STEP && until < 0.6 * BESIDE_STEP);
    const uint32_t ticks = world->ticks;
    const uint32_t waited = pool.waited;
    tide_session_update(s, now + until + 1e-5); // Between frames: the tick begins
    TIDE_CHECK(pool.work != NULL);
    now += BESIDE_STEP;
    tide_session_update(s, now); // The next frame: it's left running
    const tide_view_worlds view = tide_session_view(s);
    TIDE_CHECK(pool.work != NULL && pool.waited == waited);
    TIDE_CHECK(view.current && ((const beside_world *)view.current)->ticks == ticks && view.alpha < 1.0f);
    now += BESIDE_STEP;
    tide_session_update(s, now); // The one after shows the moment of the frame before, which is past the tick
    TIDE_CHECK(!pool.work && pool.waited == waited + 1u);
    const tide_view_worlds next = tide_session_view(s);
    TIDE_CHECK(next.current && ((const beside_world *)next.current)->ticks == ticks + 1u);
    tide_session_destroy(s);
}

// A match that ends, is left or changes with a tick in the air ends that tick first.
TIDE_TEST(alongside_tick_in_the_air_ends_before_its_match_goes)
{
    beside_pool pool = {.defer = true};
    const tide_jobs jobs = beside_jobs(&pool);
    tide_session *s = beside_session(&jobs, true);
    int frame = 0;
    for (; frame < 300 || !pool.work; frame++) {
        tide_session_update(s, (double)frame * BESIDE_STEP);
        (void)tide_session_view(s);
    }
    TIDE_REQUIRE(pool.work != NULL);
    tide_session_leave(s);
    TIDE_CHECK(!pool.work && beside_running == 0);
    tide_session_start(s, NULL, 10.0);
    for (frame = 0; frame < 300 || !pool.work; frame++) {
        tide_session_update(s, 10.0 + (double)frame * BESIDE_STEP);
        (void)tide_session_view(s);
    }
    TIDE_REQUIRE(pool.work != NULL);
    tide_session_destroy(s);
    TIDE_CHECK(!pool.work);
}

// Without a thread to lead them, or unasked, ticks run in the update and views aren't behind.
TIDE_TEST(alongside_is_nothing_without_a_leading_thread)
{
    beside_pool pool = {0};
    tide_jobs jobs = beside_jobs(&pool);
    jobs.lead = NULL;
    tide_session *none = beside_session(&jobs, true);
    tide_session *plain = beside_session(NULL, false);
    for (int frame = 0; frame < 300; frame++) {
        const tide_view_worlds a = beside_frame(none, frame);
        const tide_view_worlds b = beside_frame(plain, frame);
        TIDE_CHECK((a.current != NULL) == (b.current != NULL) && a.alpha == b.alpha);
        if (a.current && b.current) TIDE_CHECK(((const beside_world *)a.current)->ticks == ((const beside_world *)b.current)->ticks);
    }
    TIDE_CHECK(pool.handed == 0);
    tide_session_destroy(none);
    tide_session_destroy(plain);
}
