#include <inttypes.h>
#include <stdio.h>

#include "game.h"
#include "tide_test.h"

// Random's rolls on the type (Random.NextInt and the like, Random.seed): the
// match's are made from its seed, the tick, the entity and which code rolls,
// with nothing of the world written, and local code's are this machine's.

static tide_world world;
static tide_world other;
static tide_local local;
static tide_local twin;
static tide_draw_list draw;
static tide_gui gui;

#define DT (1.0f / 60.0f)

static int walkers(const tide_world *w, tide_entity *out, const int room)
{
    int count = 0;
    for (uint32_t i = 0; i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        if (tide_has_Walker(w, e) && count < room) out[count++] = e;
    }
    return count;
}

static uint64_t hash_after(tide_world *w, const int ticks)
{
    for (int i = 0; i < ticks; i++) tide_world_tick(w);
    return tide_world_hash(w);
}

// The seed makes the match: the same one rolls the same, another one otherwise.
TIDE_TEST(rolls_come_from_the_seed)
{
    TIDE_CHECK(tide_game_api.seeded);
    tide_world_start_seeded(&world, DT, NULL, 4242u);
    const uint64_t first = hash_after(&world, 30);
    TIDE_CHECK(world.Board.seed == 4242 && world.Board.through == 4242);
    tide_world_start_seeded(&other, DT, NULL, 4242u);
    TIDE_CHECK(hash_after(&other, 30) == first);
    tide_world_start_seeded(&other, DT, NULL, 4243u);
    TIDE_CHECK(hash_after(&other, 30) != first);
    tide_world_init(&other, DT); // Started plainly: seed 0, the same every time
    const uint64_t plain = hash_after(&other, 30);
    tide_world_init(&other, DT);
    TIDE_CHECK(hash_after(&other, 30) == plain && plain != first);
}

// Each entity rolls for itself, twins too; each roll of a run is another
// number; two systems roll differently for one entity; and a tick later it's
// all other numbers.
TIDE_TEST(rolls_are_an_entitys_a_systems_and_a_ticks)
{
    tide_world_start_seeded(&world, DT, NULL, 99u);
    tide_world_tick(&world); // The handlers' spawns apply
    tide_world_tick(&world);
    tide_entity all[16];
    const int count = walkers(&world, all, 16);
    TIDE_REQUIRE(count == 8);
    Walker before[8];
    for (int a = 0; a < count; a++) {
        const Walker w = tide_get_Walker(&world, all[a]);
        const Mood m = tide_get_Mood(&world, all[a]);
        before[a] = w;
        TIDE_CHECK(w.first != w.second && w.first != m.roll && w.first != w.method);
        TIDE_CHECK(w.first >= 0 && w.first < 1000000 && w.looped >= 0 && w.looped < 10000 && w.helped >= 0 && w.helped < 1000000);
        const float length = w.direction.x * w.direction.x + w.direction.y * w.direction.y;
        TIDE_CHECK(length > 0.999f && length < 1.001f);
        for (int b = a + 1; b < count; b++) {
            const Walker o = tide_get_Walker(&world, all[b]);
            TIDE_CHECK(w.first != o.first && w.second != o.second && w.helped != o.helped);
        }
    }
    const int32_t once = world.Board.once;
    tide_world_tick(&world);
    TIDE_CHECK(world.Board.once != once);
    for (int a = 0; a < count; a++) {
        const Walker w = tide_get_Walker(&world, all[a]);
        TIDE_CHECK(w.first != before[a].first && w.second != before[a].second);
    }
}

// Nothing of the world is stepped by a roll, so a tick run again from a
// snapshot rolls what it rolled: what prediction and rollback rest on.
TIDE_TEST(rolls_repeat_when_a_tick_runs_again)
{
    tide_world_start_seeded(&world, DT, NULL, 7u);
    for (int i = 0; i < 10; i++) tide_world_tick(&world);
    tide_world_copy(&other, &world);
    const uint64_t ahead = hash_after(&world, 5);
    for (int again = 0; again < 3; again++) {
        tide_world scratch = {0};
        tide_world_copy(&scratch, &other);
        TIDE_CHECK(hash_after(&scratch, 5) == ahead);
        tide_world_free(&scratch);
    }
}

// One event sent twice to one entity in one tick: each run of its handler
// rolls for itself.
TIDE_TEST(rolls_differ_between_a_handlers_runs)
{
    tide_world_start_seeded(&world, DT, NULL, 31u);
    for (int i = 0; i < 6; i++) tide_world_tick(&world);
    tide_entity all[16];
    TIDE_REQUIRE(walkers(&world, all, 16) == 8);
    const Walker poked = tide_get_Walker(&world, all[0]);
    TIDE_CHECK(poked.pokes == 2);
    TIDE_CHECK(poked.poked / 1000 != poked.poked % 1000);
    // And the same again in the same match
    tide_world_start_seeded(&other, DT, NULL, 31u);
    for (int i = 0; i < 6; i++) tide_world_tick(&other);
    TIDE_REQUIRE(walkers(&other, all, 16) == 8);
    TIDE_CHECK(tide_get_Walker(&other, all[0]).poked == poked.poked);
}

// The bits, on every platform: native, WebAssembly and Android have to give
// this hash. If the rolls change on purpose, update it from one platform and
// check the others agree.
TIDE_TEST(rolls_crossplatform_match)
{
    tide_world_start_seeded(&world, DT, NULL, 20261008u);
    const uint64_t h = hash_after(&world, 120);
    const uint64_t expected = 0x22D7734DCF1C61CFull;
    if (h != expected) printf("    rolls match: hash is 0x%016" PRIX64 ", expected 0x%016" PRIX64 "\n", h, expected);
    TIDE_CHECK(h == expected);
}

static void frame(tide_local *l, const float seconds)
{
    tide_local_frame_time(l, seconds);
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1.0f, NULL, l, &draw, &gui);
}

// Local code rolls with this machine's generator: nothing of the match, the
// same again where frames are as long (a run with no window), and otherwise
// where they aren't.
TIDE_TEST(rolls_in_views_are_this_machines)
{
    tide_world_start_seeded(&world, DT, NULL, 555u);
    tide_world_tick(&world);
    tide_world_tick(&world);
    const uint64_t match = tide_world_hash(&world);
    tide_local_init(&local);
    tide_local_init(&twin);
    float last = -1.0f;
    for (int i = 0; i < 20; i++) {
        frame(&local, DT);
        frame(&twin, DT);
        TIDE_CHECK(local.Fx.last >= 0.0f && local.Fx.last < 1.0f && local.Fx.helped >= 10.0f && local.Fx.helped < 20.0f);
        TIDE_CHECK(local.Fx.last != last);
        TIDE_CHECK(local.Fx.last == twin.Fx.last && local.Fx.helped == twin.Fx.helped);
        TIDE_CHECK(local.Fx.seed == 555);
        last = local.Fx.last;
    }
    TIDE_CHECK(local.Fx.frames == 20);
    TIDE_CHECK(tide_world_hash(&world) == match);
    // Each entity a view runs for gets its own number
    int32_t shines[8];
    int count = 0;
    for (uint32_t i = 0; i < world.entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&world.entities, i);
        if (!tide_has_Glint(&world, e)) continue;
        TIDE_REQUIRE(tide_has_Glint_local(&local, e) && count < 8);
        shines[count] = tide_get_Glint_local(&local, e).shine;
        for (int k = 0; k < count; k++) TIDE_CHECK(shines[k] != shines[count]);
        count++;
    }
    TIDE_CHECK(count == 4);
    // Frames of another length: other numbers
    frame(&local, DT);
    frame(&twin, DT * 1.5f);
    TIDE_CHECK(local.Fx.last != twin.Fx.last);
    // With no match, a view still rolls, and the seed reads 0
    tide_draw_reset(&draw);
    tide_local_frame_time(&local, DT);
    tide_frame(NULL, NULL, 1.0f, NULL, &local, &draw, &gui);
    TIDE_CHECK(local.Fx.seed == 0 && local.Fx.frames == 22);
    tide_local_free(&local);
    tide_local_free(&twin);
}
