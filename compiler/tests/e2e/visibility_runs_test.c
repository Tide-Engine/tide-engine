// Heaps per owner and runs in a game that hides state from players
// (visibility_runs.tide): its systems that change their rows' grids, lists and
// text go across threads, its world is one thread's tick by tick, and what
// players get of it (projections, their hashes, repairs by what changed) is the
// same wherever each value is laid out: in the server's heaps and runs, in a
// world a projection is loaded into, and in a player's own run of a tick.

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/jobs.h"
#include "tide/time.h"
#include "tide_test.h"
#ifdef TIDE_VISIBILITY_RUNS_THREADS
#include "tide/platform.h"
#endif

#define TICKS 20 // 12 of them past what the web's pool wakes its threads for (visibility_runs.tide, Stream)
#define PLAYERS 4u
// Every player's projection hash over TICKS ticks, hashed together, and the world's hash after them: the same on
// every platform, build and thread count, memo on and off (the _memo_off and _checked variants pin them too)
#define PROJECTIONS UINT64_C(0xf6f3458680485366)
#define WORLD UINT64_C(0x6b6c361e1cf8925e)

// A pool that's one thread saying it has eight: a round runs its work as
// thread 1 first, which takes every task there is, and then as the caller's.
static uint32_t rounds;

static void second_first(void *self, void (*work)(void *context, uint32_t thread), void *context)
{
    (void)self;
    rounds++;
    work(context, 1);
    work(context, 0);
}

static tide_player_id player(const uint32_t i) { return tide_player_from_index(i); }

// The world as bytes: its length, in memory the caller frees.
static uint8_t *packed(const tide_world *w, uint32_t *size)
{
    *size = tide_world_pack(w, NULL, 0);
    uint8_t *bytes = malloc(*size);
    if (bytes && tide_world_pack(w, bytes, *size) != *size) *size = 0;
    return bytes;
}

static bool same_bytes(const tide_world *a, const tide_world *b)
{
    uint32_t na, nb;
    uint8_t *x = packed(a, &na), *y = packed(b, &nb);
    const bool same = x && y && na && na == nb && memcmp(x, y, na) == 0;
    free(x);
    free(y);
    return same;
}

// Every player's projection hash, at once and one by one
static void hashes(const tide_world *w, uint64_t out[PLAYERS])
{
    tide_player_id recipients[PLAYERS];
    for (uint32_t i = 0; i < PLAYERS; i++) recipients[i] = player(i);
    tide_game_api.client_hashes(w, recipients, PLAYERS, out);
    for (uint32_t i = 0; i < PLAYERS; i++) TIDE_CHECK(tide_game_api.client_hash(w, player(i)) == out[i]);
}

// Where a world's chunks' values are: how many grids, lists and texts are in Chunk's own heap, how many of those
// in a run's heap past the first, and how many anywhere else
typedef struct census {
    uint32_t chunks, own, in_runs, elsewhere;
} census;

static void count(census *c, const tide_handle at)
{
    if (!at) return;
    const uint32_t where = tide_handle_where(at);
    if ((where & TIDE_WHERE_OWNER) == TIDE_WHERE_Chunk) {
        c->own++;
        c->in_runs += where >> TIDE_WHERE_RUN_SHIFT != 0;
    } else {
        c->elsewhere++;
    }
}

#define COUNT_ROWS(c, w, arch)                                                                                                   \
    for (uint32_t row = 0; row < (w)->arch.count; row++) {                                                                       \
        if (TIDE_ROW_GONE(w, arch, row)) continue;                                                                               \
        const Chunk chunk = TIDE_AT(w, arch, Chunk, row);                                                                        \
        (c)->chunks++;                                                                                                           \
        count(c, chunk.blocks.at);                                                                                               \
        count(c, chunk.heights.at);                                                                                              \
        count(c, chunk.name.at);                                                                                                 \
    }

static census census_of(const tide_world *w)
{
    census c = {0};
    COUNT_ROWS(&c, w, arch0_Chunk_Visibility); // (Seen, which can be denied, is kept beside it: a sparse component)
    return c;
}

// Which players have each chunk, by its group's audience in the directory: what the directory came to
static uint32_t sees(const tide_world *w, const tide_entity e)
{
    uint32_t bits = 0;
    for (uint32_t p = 0; p < PLAYERS; p++) bits |= (uint32_t)tide_world_visible(w, e, player(p)) << p;
    return bits;
}

static uint64_t directory(const tide_world *w)
{
    uint64_t h = TIDE_HASH_START;
    for (uint32_t row = 0; row < w->arch0_Chunk_Visibility.count; row++) {
        if (TIDE_ROW_GONE(w, arch0_Chunk_Visibility, row)) continue;
        const Seen seen = TIDE_AT(w, arch0_Chunk_Visibility, Seen, row);
        const uint32_t bits = sees(w, TIDE_ENTITY_AT(w, arch0_Chunk_Visibility, row));
        h = tide_hash_more(h, &seen.group, sizeof seen.group);
        h = tide_hash_more(h, &bits, sizeof bits);
    }
    return tide_hash_end(h);
}

// An audience's players, as bits
static uint32_t audience_bits(const Audience a)
{
    uint32_t words[sizeof a / sizeof(uint32_t)];
    memcpy(words, &a, sizeof words);
    return words[0] & ((1u << PLAYERS) - 1u);
}

TIDE_TEST(visibility_runs_owners_have_heaps_and_runs)
{
    static tide_world world;
    tide_world_init(&world, 1.0f);
    for (int i = 0; i < 4; i++) tide_world_tick(&world);
    // Chunk's values are in its own heap, many in its runs' (one thread has the runs too), none in the match's
    TIDE_CHECK(TIDE_WHERE_Chunk != TIDE_IN_MATCH && TIDE_WHERE_Streams != TIDE_IN_MATCH);
    const census c = census_of(&world);
    TIDE_CHECK(c.chunks >= 160 && c.own >= 2u * c.chunks && c.in_runs > c.own / 2 && c.elsewhere == 0);
    TIDE_CHECK(tide_handle_where(world.Streams.made.at) == TIDE_WHERE_Streams);
    TIDE_CHECK(!tide_heaps_pending(&world.heap));
    tide_world_free(&world);
}

// On threads: the world, its bytes, the directory of groups and every player's projection hash are one thread's,
// tick by tick, with a snapshot of the tick before sharing pages as a session keeps one
TIDE_TEST(visibility_runs_on_threads_are_one_threads)
{
    static tide_world one, eight, kept;
    tide_world_init(&one, 1.0f);
    tide_world_init(&eight, 1.0f);
    const tide_jobs fake = {.threads = 8u, .run = second_first};
    rounds = 0;
    uint64_t projections = TIDE_HASH_START;
    for (int i = 0; i < TICKS; i++) {
        tide_world_copy(&kept, &eight);
        tide_world_tick(&one);
        tide_world_tick_on(&eight, &fake);
        TIDE_CHECK(tide_world_hash(&one) == tide_world_hash(&eight));
        TIDE_CHECK(same_bytes(&one, &eight));
        TIDE_CHECK(directory(&one) == directory(&eight));
        uint64_t a[PLAYERS], b[PLAYERS];
        hashes(&one, a);
        hashes(&eight, b);
        for (uint32_t p = 0; p < PLAYERS; p++) TIDE_CHECK(a[p] == b[p]);
        projections = tide_hash_more(projections, a, sizeof a);
        const census ca = census_of(&one), cb = census_of(&eight);
        TIDE_CHECK(ca.chunks == cb.chunks && ca.own == cb.own && ca.in_runs == cb.in_runs);
    }
    TIDE_CHECK(rounds >= (uint32_t)TICKS / 2u); // (A tick goes to threads by what its systems took last)
    // The same on every platform and build: what players get, and the world as it's laid out
    projections = tide_hash_end(projections);
    const uint64_t world = tide_world_hash(&one);
    if (projections != PROJECTIONS) printf("    projections 0x%016" PRIx64 "\n", projections);
    if (world != WORLD) printf("    world 0x%016" PRIx64 "\n", world);
    TIDE_CHECK(projections == PROJECTIONS && world == WORLD);
    tide_world_free(&kept);
    tide_world_free(&eight);
    tide_world_free(&one);
}

// Two rows of a pair set their one group's audience in one tick: it's the later one's in the entities' order, as
// one thread setting each where it is leaves it
TIDE_TEST(visibility_runs_a_shared_group_goes_by_the_entities_order)
{
    static tide_world world;
    const tide_jobs fake = {.threads = 8u, .run = second_first};
    tide_world_init(&world, 1.0f);
    uint32_t pairs = 0;
    for (int t = 0; t < 8; t++) {
        tide_world_tick_on(&world, &fake);
        // The pairs (ids 2n and 2n+1 of a shared group), in the entities' order
        for (uint32_t r = 0; r < world.arch0_Chunk_Visibility.count; r++) {
            if (TIDE_ROW_GONE(&world, arch0_Chunk_Visibility, r)) continue;
            const Chunk a = TIDE_AT(&world, arch0_Chunk_Visibility, Chunk, r);
            if (a.id % 20 != 2 || !a.stage) continue;
            const tide_entity ea = TIDE_ENTITY_AT(&world, arch0_Chunk_Visibility, r);
            for (uint32_t q = 0; q < world.arch0_Chunk_Visibility.count; q++) {
                if (TIDE_ROW_GONE(&world, arch0_Chunk_Visibility, q)) continue;
                if (TIDE_AT(&world, arch0_Chunk_Visibility, Chunk, q).id != a.id + 1) continue;
                const tide_entity eb = TIDE_ENTITY_AT(&world, arch0_Chunk_Visibility, q);
                const Seen sa = TIDE_AT(&world, arch0_Chunk_Visibility, Seen, r);
                const Seen sb = TIDE_AT(&world, arch0_Chunk_Visibility, Seen, q);
                TIDE_CHECK(sa.group.id == sb.group.id);
                const Seen last = tide_entity_compare(ea, eb) < 0 ? sb : sa;
                TIDE_CHECK(sees(&world, ea) == audience_bits(last.audience) && sees(&world, eb) == sees(&world, ea));
                pairs++;
            }
        }
    }
    TIDE_CHECK(pairs > 20);
    tide_world_free(&world);
}

// A player's machine: its projection loaded, ticked on threads as the server's world is, and repaired by what
// changed (client_changes), its values in its own heaps and runs. Its projection hashes as the server's says
TIDE_TEST(visibility_runs_a_players_world_is_its_projection)
{
    static tide_world server, before, client;
    const tide_jobs fake = {.threads = 8u, .run = second_first};
    tide_world_init(&server, 1.0f);
    for (int i = 0; i < 3; i++) tide_world_tick_on(&server, &fake);
    const tide_player_id me = player(1);
    uint32_t size = 0;
    // Nothing a machine does with what it's sent stays in its thread's scratch area, which nothing after it gives
    // back: a grid read left the 64 bytes it was made in, for each chunk that ever came (notes/netcode.md, top section)
    const uint64_t scratch = tide_scratch_mark();
    uint8_t *bytes = tide_game_api.client_state(&server, me, &size);
    TIDE_REQUIRE(bytes && tide_game_api.load_client_state(&client, bytes, size));
    free(bytes);
    TIDE_CHECK(tide_scratch_mark() == scratch);
    TIDE_CHECK(tide_game_api.client_hash(&client, me) == tide_game_api.client_hash(&server, me));
    // What it loaded is in its owners' heaps, as its own code would have made it
    census c = census_of(&client);
    TIDE_CHECK(c.chunks > 0 && c.own >= 3u * c.chunks && c.elsewhere == 0);
    uint32_t repairs = 0, in_runs = 0;
    for (int t = 0; t < TICKS + 6; t++) {
        const bool still = t >= TICKS;
        tide_world_copy(&before, &server);
        if (t == TICKS) server.Control.still = true; // From this tick on nothing comes, goes or changes hands
        tide_world_tick_on(&server, &fake);
        (void)tide_world_hash(&server); // As a server hashes its world first
        const uint64_t want = tide_game_api.client_hash(&server, me);
        // Its own run of the tick (Grow on the chunks it has, in runs, as the server's): once nothing comes, goes or
        // changes hands, its projection is the server's
        static tide_world predicted;
        tide_world_copy(&predicted, &client);
        tide_world_tick_on(&predicted, &fake);
        const census p = census_of(&predicted);
        TIDE_CHECK(p.elsewhere == 0);
        in_runs += p.in_runs;
        if (still && t > TICKS) TIDE_CHECK(tide_game_api.client_hash(&predicted, me) == want);
        tide_world_free(&predicted);
        // The repair: what changed for it since the world it has, which it makes again itself
        uint32_t plan_size = 0, now_size = 0, was_size = 0, changed = 0;
        uint8_t *now = NULL, *was = NULL;
        uint8_t *plan = tide_game_api.client_changes(&server, &before, &server, me, &plan_size, &now, &now_size, &was, &was_size, &changed);
        TIDE_REQUIRE(plan != NULL);
        uint32_t mine_size = 0;
        uint8_t *mine = tide_game_api.client_changes_base(&client, me, plan, plan_size, &mine_size);
        TIDE_REQUIRE(mine != NULL);
        TIDE_CHECK(mine_size == was_size && memcmp(mine, was, was_size) == 0); // Its values, wherever they are
        static tide_world next;
        tide_world_copy(&next, &client);
        TIDE_CHECK(tide_game_api.apply_client_changes(&next, plan, plan_size, now, now_size, mine, mine_size));
        TIDE_CHECK(tide_game_api.client_hash(&next, me) == want);
        repairs += plan_size > 6u; // More than the plan's head and its two counts: entities came, went or changed
        // ...and that's its world now
        tide_world_free(&client);
        tide_world_copy(&client, &next);
        tide_world_free(&next);
        free(mine);
        free(plan);
        free(now);
        free(was);
        // Loaded whole, the same
        uint32_t whole_size = 0;
        uint8_t *whole = tide_game_api.client_state(&server, me, &whole_size);
        static tide_world fresh;
        TIDE_CHECK(tide_game_api.load_client_state(&fresh, whole, whole_size));
        TIDE_CHECK(tide_game_api.client_hash(&fresh, me) == want);
        tide_world_free(&fresh);
        free(whole);
        tide_world_free(&before);
        TIDE_CHECK(tide_scratch_mark() == scratch); // The ticks, the hashes, the repair made and taken, the load
    }
    c = census_of(&client);
    TIDE_CHECK(repairs >= TICKS && in_runs > 0 && c.elsewhere == 0);
    tide_world_free(&client);
    tide_world_free(&server);
}

#ifdef TIDE_VISIBILITY_RUNS_THREADS
TIDE_TEST(visibility_runs_on_the_pool_are_one_threads)
{
    const tide_jobs *pool = tide_platform_jobs();
    if (!pool || pool->threads < 2u) {
        printf("    one core: nothing to compare\n");
        return;
    }
    static tide_world one, many, kept;
    for (int attempt = 0; attempt < 5; attempt++) {
        tide_world_init(&one, 1.0f);
        tide_world_init(&many, 1.0f);
        for (int i = 0; i < TICKS; i++) {
            tide_world_copy(&kept, &many);
            tide_world_tick(&one);
            tide_world_tick_on(&many, pool);
            TIDE_CHECK(tide_world_hash(&one) == tide_world_hash(&many));
            TIDE_CHECK(same_bytes(&one, &many));
            TIDE_CHECK(directory(&one) == directory(&many));
            uint64_t a[PLAYERS], b[PLAYERS];
            hashes(&one, a);
            hashes(&many, b);
            for (uint32_t p = 0; p < PLAYERS; p++) TIDE_CHECK(a[p] == b[p]);
        }
        tide_world_free(&kept);
        tide_world_free(&many);
        tide_world_free(&one);
    }
}

static void alone(void *self, void (*work)(void *context, uint32_t thread), void *context)
{
    (void)self;
    work(context, 0);
}

// For measuring, from a release build with TIDE_VISIBILITY_RUNS_TIME set: a tick's mean time on one thread and on
// the pool over 200 ticks after 20 (three rounds, a snapshot kept and the world and every player's projection hashed
// each tick, as a server does), and each system's from the jobs' profile
TIDE_TEST(visibility_runs_time)
{
    const tide_jobs *pool = tide_platform_jobs();
    if (!getenv("TIDE_VISIBILITY_RUNS_TIME") || !pool) return;
    static tide_world world, kept;
    static tide_profile profile;
    for (int round = 0; round < 3; round++) {
        for (int threads = 0; threads < 2; threads++) {
            tide_jobs jobs = threads ? *pool : (tide_jobs){.threads = 1u, .run = alone};
            jobs.profile = &profile;
            tide_world_init(&world, 1.0f);
            uint64_t began = 0;
            for (int i = 0; i < 220; i++) {
                if (i == 20) {
                    began = tide_time_now_ns();
                    tide_profile_clear(&profile);
                }
                tide_world_copy(&kept, &world);
                tide_world_tick_on(&world, &jobs);
                (void)tide_world_hash(&world);
                uint64_t h[PLAYERS];
                hashes(&world, h);
            }
            printf("    %s: %.3f ms a tick (%u chunks), systems %.3f:", threads ? "the pool" : "one thread",
                   (double)(tide_time_now_ns() - began) / 200.0 / 1e6, census_of(&world).chunks, profile.systems_ms / profile.ticks);
            for (uint32_t k = 0; k < profile.systems.count; k++) {
                printf(" %s %.3f", profile.systems.items[k].name, profile.systems.items[k].total / profile.ticks);
            }
            printf("\n");
            tide_world_free(&kept);
            tide_world_free(&world);
            tide_profile_free(&profile);
        }
    }
}
#endif
