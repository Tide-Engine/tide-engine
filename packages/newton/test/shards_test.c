#include "game.h"
#include "tide/shards.h"
#include "tide_test.h"

#include <math.h>
#include <string.h>

// newton/test/shards.tide on the engine's orchestrator, its regions split at
// x = 0: every ball is owned by one shard at every step; the one that crosses
// keeps its speed, the light one that runs into the heavy one across the
// border shares its momentum with it, and the jointed pair crosses together.

static tide_shards *shards;
static double now;

typedef struct balls {
    int owned[6];        // By id: how many shards own it
    Newton_Body body[6]; // ...and its Body on the one that does
    int shard[6];
} balls;

static balls count(void)
{
    balls b;
    memset(&b, 0, sizeof b);
    for (uint32_t i = 0; i < tide_shards_count(shards); i++) {
        const tide_world *w = tide_shards_world(shards, i);
        if (!w) continue;
        for (uint32_t index = 0; index < w->entities.next_unused; index++) {
            if (!w->entities.page[index >> TIDE_ENTITY_PAGE_SHIFT]) {
                index |= (1u << TIDE_ENTITY_PAGE_SHIFT) - 1u;
                continue;
            }
            const tide_entity e = tide_entity_in_slot(&w->entities, index);
            if (tide_entity_is_null(e) || !tide_has_Ball(w, e) || tide_has_tide_ghost(w, e)) continue;
            const int id = tide_get_Ball(w, e).id;
            if (id < 0 || id >= 6) continue;
            b.owned[id]++;
            if (tide_has_Newton_Body(w, e)) b.body[id] = tide_get_Newton_Body(w, e);
            b.shard[id] = (int)i;
        }
    }
    return b;
}

static void step(const double seconds)
{
    const double until = now + seconds;
    while (now < until) {
        now += 1.0 / 120.0;
        tide_shards_update(shards, now);
        const balls b = count();
        for (int id = 0; id < 6; id++) TIDE_CHECK(b.owned[id] <= 1); // Never two shards' at once
    }
}

TIDE_TEST(newton_bodies_cross_shards_and_push_across_borders)
{
    now = 0;
    const tide_shards_desc d = {.game = &tide_game_api, .tick_rate = 60, .world = {{-40.0, -10.0, -40.0}, {40.0, 10.0, 40.0}},
                                .split = {2, 1, 1}};
    shards = tide_shards_create(&d, now);
    TIDE_REQUIRE(shards != NULL);
    step(0.5);
    balls b = count();
    TIDE_CHECK(b.owned[0] == 1 && b.shard[0] == 1); // On the left, by its position
    TIDE_CHECK(b.owned[2] == 1 && b.shard[2] == 2);
    // Four seconds on: from x = -12 at 6 m/s, across the border to about 12
    step(3.5);
    b = count();
    for (int id = 0; id < 6; id++) TIDE_CHECK(b.owned[id] == 1);
    TIDE_CHECK(b.shard[0] == 2);
    TIDE_CHECK(fabsf(b.body[0].velocity.x - 6.0f) < 1e-3f); // Its speed went with it
    TIDE_CHECK(fabsf(b.body[0].position.x - 12.0f) < 0.25f); // ...and so did where it was
    // The light ball and the heavy one: the light one's momentum (4) shared, rather than it bouncing off a
    // copy that can't move while the other is kicked away twice as fast
    const float momentum = b.body[1].velocity.x * 1.0f + b.body[2].velocity.x * 3.0f;
    TIDE_CHECK(fabsf(momentum - 4.0f) < 1.0f);
    TIDE_CHECK(b.body[2].velocity.x > 1.0f && b.body[2].velocity.x < 3.0f);
    // The jointed pair and its joint are one shard's, a meter and a half apart
    TIDE_CHECK(b.shard[3] == 2 && b.shard[4] == 2 && b.shard[5] == 2);
    const float apart = b.body[3].position.x - b.body[4].position.x;
    TIDE_CHECK(fabsf(apart - 1.5f) < 0.05f);
    tide_shards_destroy(shards);
    shards = NULL;
}
