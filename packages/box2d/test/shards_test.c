#include "game.h"
#include "tide/shards.h"
#include "tide_test.h"

#include <math.h>
#include <string.h>

// box2d/test/shards.tide on the engine's orchestrator, its regions split at
// x = 0: every crate is owned by one shard at every step, its Box2D body made
// by whichever shard owns it and no other; the crates that cross keep their
// speed, and the two that meet on the right push each other there.

static tide_shards *shards;
static double now;

typedef struct crates {
    int owned[16];      // By id: how many shards own it
    Box2D_Body body[16]; // ...and its Body on the one that does
    int shard[16];
    int hits[16];
    int bodies;          // Bodies the shards' Box2D worlds have, by their maps of whose each is
} crates;

static crates count(void)
{
    crates c;
    memset(&c, 0, sizeof c);
    for (uint32_t i = 0; i < tide_shards_count(shards); i++) {
        const tide_world *w = tide_shards_world(shards, i);
        if (!w) continue;
        c.bodies += (int)tide_map_count(w->Box2D_Physics.owners);
        for (uint32_t index = 0; index < w->entities.next_unused; index++) {
            if (!w->entities.page[index >> TIDE_ENTITY_PAGE_SHIFT]) {
                index |= (1u << TIDE_ENTITY_PAGE_SHIFT) - 1u;
                continue;
            }
            const tide_entity e = tide_entity_in_slot(&w->entities, index);
            if (tide_entity_is_null(e) || !tide_has_Crate(w, e) || tide_has_tide_ghost(w, e)) continue;
            const int id = tide_get_Crate(w, e).id;
            if (id < 0 || id >= 16) continue;
            c.owned[id]++;
            c.body[id] = tide_get_Box2D_Body(w, e);
            c.shard[id] = (int)i;
            c.hits[id] = tide_get_Crate(w, e).hits;
        }
    }
    return c;
}

static void step(const double seconds)
{
    const double until = now + seconds;
    while (now < until) {
        now += 1.0 / 120.0;
        tide_shards_update(shards, now);
        const crates c = count();
        for (int id = 0; id < 16; id++) TIDE_CHECK(c.owned[id] <= 1); // Never two shards' at once
        TIDE_CHECK(c.bodies <= 8); // ...and never a body for a crate on two shards' worlds
    }
}

TIDE_TEST(box2d_bodies_cross_shards_and_keep_their_speed)
{
    now = 0;
    const tide_shards_desc d = {.game = &tide_game_api, .tick_rate = 60, .world = {{-40.0, -10.0, -40.0}, {40.0, 10.0, 40.0}},
                                .split = {2, 1, 1}};
    shards = tide_shards_create(&d, now);
    TIDE_REQUIRE(shards != NULL);
    step(0.5);
    crates c = count();
    for (int id = 0; id < 4; id++) {
        TIDE_CHECK(c.owned[id] == 1 && c.shard[id] == 1); // On the left, by their bodies' positions
        TIDE_CHECK(c.body[id].id != 0);                    // ...whose shard made their bodies
    }
    TIDE_CHECK(c.owned[11] == 1 && c.shard[11] == 2);
    // Four seconds on: from x = -12 at 6 m/s, across the border at x = 0 to about 12
    step(3.5);
    c = count();
    TIDE_CHECK(c.bodies == 8); // One body for each crate, on the shard that owns it
    for (int id = 0; id < 4; id++) {
        TIDE_CHECK(c.owned[id] == 1 && c.shard[id] == 2);
        TIDE_CHECK(c.body[id].id != 0);
        TIDE_CHECK(fabsf(c.body[id].velocity.x - 6.0f) < 1e-3f); // Its speed went with it
        TIDE_CHECK(fabsf(c.body[id].position.x - 12.0f) < 0.25f); // ...and so did where it was
        TIDE_CHECK(fabsf(c.body[id].position.y - (float)(id * 3)) < 1e-3f); // Weightless on every shard: each set its own gravity
    }
    // The two that met on the right: the one coming from the left pushed the other back the way it came
    TIDE_CHECK(c.owned[10] == 1 && c.shard[10] == 2 && c.owned[11] == 1 && c.shard[11] == 2);
    TIDE_CHECK(c.body[11].velocity.x > 0.5f);
    TIDE_CHECK(c.body[10].velocity.x < 5.5f);
    // The crate that slid left into the wall's copy on the right: it stopped against it there, and heard it hit it
    TIDE_CHECK(c.owned[13] == 1 && c.shard[13] == 1);
    TIDE_CHECK(c.owned[12] == 1 && c.shard[12] == 2);
    TIDE_CHECK(c.body[12].position.x > -0.6f);
    TIDE_CHECK(c.body[12].velocity.x > -0.01f);
    TIDE_CHECK(c.hits[12] > 0);
    tide_shards_destroy(shards);
    shards = NULL;
}
