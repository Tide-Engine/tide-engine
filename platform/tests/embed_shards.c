// A sharded game inside another program (tide/embed.h): platform/tests/embed_shards.tide,
// split in two along x, its view watching the whole world.

#include <stdio.h>
#include <string.h>

#include "game.h"
#include "tide/embed.h"
#include "tide/embed_game.h"
#include "tide/run.h"
#include "tide_test.h"

static char program[] = "embed", shards[] = "--shards", two[] = "2", width[] = "--shard-width", twenty[] = "20";

// Where the packet last read had each rock
static float rock_x[6];

// The packet's Rock section: each rock's id, by how many times it's there
static uint32_t rocks(const uint8_t *packet, const uint32_t size, int seen[6])
{
    memset(seen, 0, 6 * sizeof *seen);
    uint32_t entities, sections;
    memcpy(&entities, packet + 36, sizeof entities);
    memcpy(&sections, packet + 40, sizeof sections);
    uint32_t at = TIDE_PACKET_HEADER + entities * TIDE_PACKET_ENTITY;
    uint32_t found = 0;
    for (uint32_t s = 0; s < sections && at + TIDE_PACKET_SECTION <= size; s++) {
        at = (at + 7u) & ~7u;
        uint32_t head[6];
        memcpy(head, packet + at, sizeof head);
        const uint32_t body = at + TIDE_PACKET_SECTION;
        if (head[0] == TIDE_PACKET_COMPONENT && head[3] == sizeof(Rock)) {
            const uint32_t values = body + ((head[2] * 4u + 7u) & ~7u);
            for (uint32_t i = 0; i < head[2] && values + (i + 1u) * sizeof(Rock) <= size; i++) {
                Rock rock;
                memcpy(&rock, packet + values + i * sizeof(Rock), sizeof rock);
                if (rock.id >= 0 && rock.id < 6) {
                    seen[rock.id]++;
                    rock_x[rock.id] = rock.position.x;
                }
                found++;
            }
        }
        at = body + head[4];
    }
    return found;
}

// The match ran nothing while its kept ticks were shown: its latest is still `tick`
static bool verified_same(const tide_embed *e, const uint32_t tick)
{
    tide_embed_travel t;
    tide_embed_travel_of(e, &t);
    return t.last == tick;
}

TIDE_TEST(embed_draws_every_shard_it_watches)
{
    char *argv[] = {program, shards, two, width, twenty};
    tide_embed *e = tide_embed_create(&(tide_embed_desc){.game = &tide_host_game_api, .argc = 5, .argv = argv});
    TIDE_REQUIRE(e != NULL);
    for (int i = 0; i < 240; i++) tide_embed_frame(e, 1.0f / 60.0f, NULL, 800.0f, 600.0f, 1.0f);
    uint32_t size = 0;
    const uint8_t *packet = tide_embed_view(e, &size);
    TIDE_REQUIRE(packet != NULL && size >= TIDE_PACKET_HEADER);
    int seen[6];
    TIDE_CHECK(rocks(packet, size, seen) == 6); // Every rock, from whichever region has it...
    for (int id = 0; id < 6; id++) TIDE_CHECK(seen[id] == 1); // ...once
    tide_embed_destroy(e);
}

static void frames(tide_embed *e, const int count)
{
    for (int i = 0; i < count; i++) tide_embed_frame(e, 1.0f / 60.0f, NULL, 800.0f, 600.0f, 1.0f);
}

static tide_embed_travel travel_of(const tide_embed *e)
{
    tide_embed_travel t;
    tide_embed_travel_of(e, &t);
    return t;
}

// A sharded match is followed as its player has it: the ticks kept are its home shard's worlds with the other shards
// it follows beside them, so a tick shown again has every rock once, where that tick had it, through a rock's
// crossing from one shard to the other. Alone on this machine it really pauses, and steps every shard a tick.
TIDE_TEST(embed_shows_a_sharded_matchs_kept_ticks)
{
    char *argv[] = {program, shards, two, width, twenty};
    tide_embed *e = tide_embed_create(&(tide_embed_desc){.game = &tide_host_game_api, .argc = 5, .argv = argv});
    TIDE_REQUIRE(e != NULL);
    tide_embed_travel_keep(e, 10.0f);
    frames(e, 300); // Rock 2, 3 left of the border, is across it by now
    tide_embed_travel t = travel_of(e);
    TIDE_CHECK(t.state == TIDE_TRAVEL_LIVE && t.followed == 2u && !t.background);
    if (t.why[0]) fprintf(stderr, "why: %s\n", t.why);
    TIDE_REQUIRE(tide_embed_pause(e, true));
    frames(e, 5);
    t = travel_of(e);
    TIDE_CHECK(t.state == TIDE_TRAVEL_PAUSED && !t.background && t.shown == t.last);
    const uint32_t paused_at = t.last;
    frames(e, 60); // A second of frames: no shard ticks
    TIDE_CHECK(travel_of(e).last == paused_at);
    TIDE_REQUIRE(tide_embed_step(e));
    frames(e, 5);
    TIDE_CHECK(travel_of(e).last == paused_at + 1u); // Every shard, one tick, once its player's clients confirmed it
    frames(e, 30);
    t = travel_of(e);
    TIDE_CHECK(t.last == paused_at + 1u);
    TIDE_REQUIRE(t.last - t.first > 200u);

    // Back through the ticks kept. Its first few are from before this machine followed the regions (it joins at the
    // root, which has no rock, and each region after it): the home's world alone
    enum { JOINING = 8 };
    static float xs[1024][6];
    uint32_t whole = 0, steady = 0, compared = 0;
    for (uint32_t tick = t.last; tick >= t.first && tick <= t.last; tick--) {
        TIDE_REQUIRE(tide_embed_show(e, tick));
        frames(e, 1);
        uint32_t size = 0;
        const uint8_t *packet = tide_embed_view(e, &size);
        TIDE_REQUIRE(packet != NULL);
        int seen[6];
        const uint32_t found = rocks(packet, size, seen);
        bool once = found == 6;
        for (int id = 0; id < 6; id++) once = once && seen[id] == 1;
        whole += once || tick < t.first + JOINING;
        memcpy(xs[tick - t.first], rock_x, sizeof rock_x);
    }
    const uint32_t count = t.last - t.first + 1u;
    TIDE_CHECK(whole == count); // Every rock once at every tick, whichever shard had it then
    // ...each a tick's drift on from the tick before, on either shard: but the last, which is the match's own frame
    for (uint32_t i = JOINING + 1u; i + 1u < count; i++) {
        for (int id = 0; id < 6; id++) {
            compared++;
            steady += xs[i][id] == xs[i - 1][id] + 0.02f;
        }
    }
    if (steady != compared) fprintf(stderr, "steady %u of %u\n", (unsigned)steady, (unsigned)compared);
    TIDE_CHECK(steady == compared);
    TIDE_CHECK(xs[JOINING][2] < 0.0f && xs[count - 2u][2] > 0.0f); // Rock 2 crossed the border within them
    TIDE_CHECK(verified_same(e, paused_at + 1u));

    // It isn't taken back or recorded from here; going on, it's followed again
    TIDE_REQUIRE(tide_embed_show(e, t.first + 10u));
    TIDE_CHECK(!tide_embed_continue(e));
    uint32_t size = 7;
    TIDE_CHECK(tide_embed_recording(e, &size) == NULL && size == 0);
    TIDE_REQUIRE(tide_embed_pause(e, false));
    frames(e, 60);
    t = travel_of(e);
    TIDE_CHECK(t.state == TIDE_TRAVEL_LIVE && t.followed == 2u && t.last > paused_at + 40u);
    tide_embed_destroy(e);
}
