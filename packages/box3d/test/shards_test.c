#include "game.h"
#include "tide/shards.h"
#include "tide_test.h"

#include <math.h>
#include <string.h>

// box3d/test/shards.tide on the engine's orchestrator, its regions split at
// x = 0: every crate is owned by one shard at every step, its Box3D body made
// by whichever shard owns it and no other; the crates that cross keep their
// speed, and the two that meet on the right push each other there.

static tide_shards *shards;
static double now;

typedef struct crates {
    int owned[28];      // By id: how many shards own it
    Box3D_Body body[28]; // ...and its Body on the one that does
    int shard[28];
    int hits[28];
    int bodies;          // Bodies the shards' Box3D worlds have, by their maps of whose each is
} crates;

static crates count(void)
{
    crates c;
    memset(&c, 0, sizeof c);
    for (uint32_t i = 0; i < tide_shards_count(shards); i++) {
        const tide_world *w = tide_shards_world(shards, i);
        if (!w) continue;
        c.bodies += (int)tide_map_count(w->Box3D_Physics.owners);
        for (uint32_t index = 0; index < w->entities.next_unused; index++) {
            if (!w->entities.page[index >> TIDE_ENTITY_PAGE_SHIFT]) {
                index |= (1u << TIDE_ENTITY_PAGE_SHIFT) - 1u;
                continue;
            }
            const tide_entity e = tide_entity_in_slot(&w->entities, index);
            if (tide_entity_is_null(e) || !tide_has_Crate(w, e) || tide_has_tide_ghost(w, e)) continue;
            const int id = tide_get_Crate(w, e).id;
            if (id < 0 || id >= 28) continue;
            c.owned[id]++;
            if (tide_has_Box3D_Body(w, e)) c.body[id] = tide_get_Box3D_Body(w, e);
            c.shard[id] = (int)i;
            c.hits[id] = tide_get_Crate(w, e).hits;
        }
    }
    return c;
}

// Whether a shard holds a copy of the crate with that id
static bool ghost_of(const uint32_t shard, const int id)
{
    const tide_world *w = tide_shards_world(shards, shard);
    for (uint32_t index = 0; w && index < w->entities.next_unused; index++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, index);
        if (!tide_entity_is_null(e) && tide_has_Crate(w, e) && tide_has_tide_ghost(w, e) && tide_get_Crate(w, e).id == id) return true;
    }
    return false;
}

static void step(const double seconds)
{
    const double until = now + seconds;
    while (now < until) {
        now += 1.0 / 120.0;
        tide_shards_update(shards, now);
        const crates c = count();
        for (int id = 0; id < 28; id++) TIDE_CHECK(c.owned[id] <= 1); // Never two shards' at once
        TIDE_CHECK(c.bodies <= 21); // ...and never a body for a crate on two shards' worlds
    }
}

TIDE_TEST(box3d_bodies_cross_shards_and_keep_their_speed)
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
    TIDE_CHECK(c.bodies == 21); // One body for each crate, on the shard that owns it
    for (int id = 0; id < 4; id++) {
        TIDE_CHECK(c.owned[id] == 1 && c.shard[id] == 2);
        TIDE_CHECK(c.body[id].id != 0);
        TIDE_CHECK(fabsf(c.body[id].velocity.x - 6.0f) < 1e-3f); // Its speed went with it
        TIDE_CHECK(fabsf(c.body[id].position.x - 12.0f) < 0.25f); // ...and so did where it was
        TIDE_CHECK(fabsf(c.body[id].position.y) < 1e-3f); // Weightless on every shard: each set its own gravity
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
    // ...and the one that slid into the hull wall's copy
    TIDE_CHECK(c.owned[15] == 1 && c.shard[15] == 1);
    TIDE_CHECK(c.owned[14] == 1 && c.shard[14] == 2 && c.body[14].position.x > -0.6f && c.hits[14] > 0);
    // The part went with its crate, though it has no position of its own
    TIDE_CHECK(c.owned[6] == 1 && c.shard[6] == 2 && c.owned[7] == 1 && c.shard[7] == 2);
    // The jointed crates and their joint are one shard's, and the joint held them two meters apart
    TIDE_CHECK(c.owned[8] == 1 && c.owned[9] == 1 && c.owned[5] == 1);
    TIDE_CHECK(c.shard[8] == 2 && c.shard[9] == 2 && c.shard[5] == 2);
    const float apart = c.body[8].position.x - c.body[9].position.x;
    TIDE_CHECK(fabsf(apart - 2.0f) < 0.05f);
    // The ring of three: its crates and joints are one shard's, each crate two meters from the next
    for (int id = 16; id < 22; id++) TIDE_CHECK(c.owned[id] == 1 && c.shard[id] == 2);
    for (int k = 0; k < 3; k++) {
        const Box3D_Body *p = &c.body[16 + k], *q = &c.body[16 + (k + 1) % 3];
        const float dx = p->position.x - q->position.x, dz = p->position.z - q->position.z;
        TIDE_CHECK(fabsf(sqrtf(dx * dx + dz * dz) - 2.0f) < 0.05f);
    }
    // The crate that ran into the one across the border: one shard has both, and they share its speed, rather
    // than it stopping dead against a copy that can't move while the other leaves as fast as it came
    TIDE_CHECK(c.owned[22] == 1 && c.owned[23] == 1 && c.shard[22] == c.shard[23]);
    TIDE_CHECK(fabsf(c.body[22].velocity.x - 2.0f) < 0.3f && fabsf(c.body[23].velocity.x - 2.0f) < 0.3f);
    // The crate that fell onto the long floor on the right: the left's floor reaches there, so the right has a copy
    // of it, and the crate rests on it
    TIDE_CHECK(c.owned[24] == 1 && c.shard[24] == 1 && c.owned[25] == 1 && c.shard[25] == 2);
    TIDE_CHECK(c.body[25].position.y > 0.4f && c.body[25].position.y < 1.0f); // Weightless, it stops as it meets the floor
    TIDE_CHECK(c.body[25].velocity.y > -0.1f);
    // ...and the part on the post by the border: the right holds a copy of it, which goes with its body
    TIDE_CHECK(ghost_of(2, 27));
    // What the shards sent each other of the bodies near their border: what changed of each, about a quarter of
    // whole trees' 419,326 bytes
    TIDE_CHECK(tide_shards_ghost_bytes(shards) < 150000u);
    tide_shards_destroy(shards);
    shards = NULL;
}
