// Systems written in C (extern_system.tide, extern_system_c.c): what they
// read and write of the match, what they leave shared with its snapshots, and
// that a tick on threads, with chunks in pieces, comes out as on one.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/jobs.h"
#include "tide_test.h"

#ifdef TIDE_EXTERN_SYSTEM_THREADS
#include "tide/platform.h"
#endif

#define DT (1.0f / 60.0f)

// Main is in slot 0, then what its Setup spawns, in order
enum { PLAIN = 1, HEAVY = 5001, FROZEN = 5301, BADGED = 5341, DOTTED = 5441, WHEELS = 5451 };

// extern_system_c.c
extern int xs_spin_least;
extern int xs_spin_most;
extern int xs_spin_calls;

static tide_world world;
static tide_world many;
static tide_world kept;

static tide_entity in_slot(const tide_world *w, const uint32_t slot)
{
    return tide_entity_in_slot(&w->entities, slot);
}

TIDE_TEST(extern_systems_read_and_write_their_rows)
{
    enum { TICKS = 10 };
    tide_world_init(&world, DT);
    for (int t = 0; t < TICKS; t++) tide_world_tick(&world);

    // Arrays: each place moved by its speed, in the second chunk as in the first
    for (int i = 0; i < 5000; i += 617) {
        float x = (float)i;
        float y = (float)(2 * i);
        for (int t = 0; t < TICKS; t++) {
            x += 1.0f * DT;
            y += 0.5f * DT;
        }
        const Place place = tide_get_Place(&world, in_slot(&world, (uint32_t)(PLAIN + i)));
        TIDE_CHECK(place.at.x == x && place.at.y == y && place.id == i);
    }
    // A filter by a tag, and a singleton to read: the heavy ones' speeds fell, after they moved
    for (int i = 0; i < 300; i += 37) {
        float y = (float)-i;
        float fall = 1.0f;
        for (int t = 0; t < TICKS; t++) {
            y += fall * DT;
            fall += -2.0f;
        }
        const tide_entity e = in_slot(&world, (uint32_t)(HEAVY + i));
        TIDE_CHECK(tide_get_Place(&world, e).at.y == y && tide_get_Place(&world, e).at.x == (float)i);
        TIDE_CHECK(tide_get_Speed(&world, e).by.y == fall && tide_get_Speed(&world, e).by.x == 0.0f);
    }
    // `without Frozen`: left where they were
    for (int i = 0; i < 40; i++) {
        const Place place = tide_get_Place(&world, in_slot(&world, (uint32_t)(FROZEN + i)));
        TIDE_CHECK(place.at.x == (float)-i && place.at.y == (float)i && place.id == 20000 + i);
    }
    // A row at a time: a long, the row's entity and a bool
    for (int i = 0; i < 100; i++) {
        const tide_entity e = in_slot(&world, (uint32_t)(BADGED + i));
        const Badge badge = tide_get_Badge(&world, e);
        TIDE_CHECK(badge.serial == INT64_C(1000000000000) + i + (int64_t)TICKS * (30000 + i));
        TIDE_CHECK(tide_entity_equal(badge.me, e) && badge.lit == (TICKS % 2 == 1));
    }
    // ...and a component of three bytes
    for (int i = 0; i < 10; i++) {
        const Dots dots = tide_get_Dots(&world, in_slot(&world, (uint32_t)(DOTTED + i)));
        TIDE_CHECK(dots.a == i + TICKS && dots.b == 2 && dots.c == i + TICKS + 2);
    }
    // Rows it only reads, in the order a system written in Tide goes through
    // them: a call for each chunk of each archetype with a Place
    TIDE_CHECK(world.Tally.runs == 7 * TICKS && world.Tally.rows == 5450 * TICKS);
    TIDE_CHECK(world.Tally.order == world.Tally.orderInTide && world.Tally.order != 0);
    TIDE_CHECK(world.Tally.bonus == 5);
    // Once a tick, with no rows
    TIDE_CHECK(world.Tally.once == TICKS && world.Tally.onceRows == 0 && world.Tally.tick == TICKS - 1);
    // Filters alone: entities, and no components
    TIDE_CHECK(world.Tally.wheels == 4096 * TICKS);
    // [Before] and [After] against systems written in Tide
    TIDE_CHECK(world.Order.seen == 4 * TICKS);
    TIDE_CHECK(world.Order.zero == 4 * TICKS - 3 && world.Order.first == 4 * TICKS - 2);
    TIDE_CHECK(world.Order.inC == 4 * TICKS - 1 && world.Order.last == 4 * TICKS);
    tide_world_free(&world);
}

// Whether the world and a snapshot still share the page of a chunk's column.
static bool shares(const tide_table *now, const tide_table *then, const tide_columns *c, const uint32_t chunk, const uint32_t column)
{
    return now->pages[chunk * c->count + column] == then->pages[chunk * c->count + column];
}

// ...and its hash is the one it had: nothing asked to write it.
static bool hashed(const tide_table *t, const tide_columns *c, const uint32_t chunk, const uint32_t column)
{
    return t->pages[chunk * c->count + column]->hashed != UINT32_MAX;
}

// A client keeps a snapshot of every tick, which shares the world's pages:
// what C doesn't write stays shared, lane by lane.
TIDE_TEST(extern_systems_leave_what_they_dont_write_shared)
{
    tide_world_init(&world, DT);
    for (int t = 0; t < 3; t++) tide_world_tick(&world);
    tide_world_copy(&kept, &world);
    const uint64_t before = tide_world_hash(&kept); // Every page is hashed now
    tide_world_tick(&world);

    // Idle asked for arrays to write and wrote what was there: every lane is the snapshot's again, its hash good
    const tide_columns *frozen = &tide_columns_arch2_Place_Speed_Frozen;
    for (uint32_t k = 0; k < frozen->count; k++) {
        TIDE_CHECK(shares(&world.arch2_Place_Speed_Frozen, &kept.arch2_Place_Speed_Frozen, frozen, 0, k));
        TIDE_CHECK(hashed(&world.arch2_Place_Speed_Frozen, frozen, 0, k));
    }
    // Move wrote x and y: those lanes are the world's own in every chunk, and
    // the id beside them, in the same component, is still shared, as the speeds are
    const tide_columns *plain = &tide_columns_arch0_Place_Speed;
    const uint32_t place = tide_column_arch0_Place_Speed_Place;
    const uint32_t speed = tide_column_arch0_Place_Speed_Speed;
    TIDE_CHECK(world.arch0_Place_Speed.chunks == 3);
    for (uint32_t chunk = 0; chunk < 3; chunk++) {
        TIDE_CHECK(!shares(&world.arch0_Place_Speed, &kept.arch0_Place_Speed, plain, chunk, place));
        TIDE_CHECK(!shares(&world.arch0_Place_Speed, &kept.arch0_Place_Speed, plain, chunk, place + 1));
        TIDE_CHECK(shares(&world.arch0_Place_Speed, &kept.arch0_Place_Speed, plain, chunk, place + 2));
        TIDE_CHECK(hashed(&world.arch0_Place_Speed, plain, chunk, place + 2));
        TIDE_CHECK(shares(&world.arch0_Place_Speed, &kept.arch0_Place_Speed, plain, chunk, speed));
        TIDE_CHECK(shares(&world.arch0_Place_Speed, &kept.arch0_Place_Speed, plain, chunk, speed + 1));
        TIDE_CHECK(shares(&world.arch0_Place_Speed, &kept.arch0_Place_Speed, plain, chunk, 0)); // Its entities
    }
    // Weigh wrote one field of the heavy ones' speeds
    const tide_columns *heavy = &tide_columns_arch1_Place_Speed_Heavy;
    TIDE_CHECK(shares(&world.arch1_Place_Speed_Heavy, &kept.arch1_Place_Speed_Heavy, heavy, 0, tide_column_arch1_Place_Speed_Heavy_Speed));
    TIDE_CHECK(!shares(&world.arch1_Place_Speed_Heavy, &kept.arch1_Place_Speed_Heavy, heavy, 0, tide_column_arch1_Place_Speed_Heavy_Speed + 1));
    // Stamp put each row back whole: the lanes that came out the same are
    // shared (the serial's high half, the entity it set three ticks ago), and
    // the ones that changed aren't (the serial's low half, the bool's)
    const tide_columns *badged = &tide_columns_arch3_Place_Speed_Badge;
    const uint32_t badge = tide_column_arch3_Place_Speed_Badge_Badge;
    TIDE_CHECK(!shares(&world.arch3_Place_Speed_Badge, &kept.arch3_Place_Speed_Badge, badged, 0, badge));
    TIDE_CHECK(shares(&world.arch3_Place_Speed_Badge, &kept.arch3_Place_Speed_Badge, badged, 0, badge + 1));
    TIDE_CHECK(shares(&world.arch3_Place_Speed_Badge, &kept.arch3_Place_Speed_Badge, badged, 0, badge + 2));
    TIDE_CHECK(shares(&world.arch3_Place_Speed_Badge, &kept.arch3_Place_Speed_Badge, badged, 0, badge + 3));
    TIDE_CHECK(!shares(&world.arch3_Place_Speed_Badge, &kept.arch3_Place_Speed_Badge, badged, 0, badge + 4));
    // The snapshot is as it was
    TIDE_CHECK(tide_world_hash(&kept) == before && tide_world_hash(&world) != before);
    float x = 0.0f;
    for (int t = 0; t < 3; t++) x += 1.0f * DT;
    TIDE_CHECK(tide_get_Place(&kept, in_slot(&kept, PLAIN)).at.x == x);
    TIDE_CHECK(tide_get_Place(&world, in_slot(&world, PLAIN)).at.x == x + 1.0f * DT);
    tide_world_free(&world);
    tide_world_free(&kept);
}

// A pool of one thread that says it has four: the tick takes the path it
// takes on threads, with its chunks in pieces, and runs it all here, in order.
static void alone_run(void *self, void (*work)(void *context, uint32_t thread), void *context)
{
    (void)self;
    work(context, 0);
}

// A tick on `jobs` against one on this thread, tick by tick, each with a
// snapshot of the tick before sharing its pages, as a client's does.
static void compare(const tide_jobs *jobs, const int ticks, const bool pieces)
{
    tide_world_init(&world, DT);
    tide_world_init(&many, DT);
    for (int t = 0; t < ticks; t++) {
        tide_world_copy(&kept, &many);
        const uint64_t before = tide_world_hash(&kept);
        tide_world_tick(&world);
        xs_spin_least = xs_spin_most = xs_spin_calls = 0;
        tide_world_tick_on(&many, jobs);
        if (tide_world_hash(&world) != tide_world_hash(&many)) printf("    tick %d came out different\n", t);
        TIDE_REQUIRE(tide_world_hash(&world) == tide_world_hash(&many));
        TIDE_REQUIRE(tide_world_hash(&kept) == before); // Its pages stayed as they were
        // Spin's two full chunks went in two pieces each, every row once
        if (pieces) TIDE_REQUIRE(xs_spin_calls == 4 && xs_spin_least == 1024 && xs_spin_most == 1024);
    }
    const Wheel wheel = tide_get_Wheel(&many, in_slot(&many, WHEELS + 3000));
    float angle = 3000.0f * 0.25f;
    float turns = 0.0f;
    for (int t = 0; t < ticks; t++) {
        angle += 0.125f * (float)(3000 % 7);
        if (angle >= 16.0f) {
            angle -= 16.0f;
            turns += 1.0f;
        }
    }
    TIDE_CHECK(wheel.angle == angle && wheel.turns == turns && wheel.id == 3000);
    TIDE_CHECK(many.Tally.order == many.Tally.orderInTide && many.Tally.rows == 5450 * ticks);
    TIDE_CHECK(many.Load.spun == 0); // What made the tick worth threads never ran
    tide_world_free(&world);
    tide_world_free(&many);
    tide_world_free(&kept);
}

TIDE_TEST(extern_systems_in_pieces_come_out_as_whole)
{
    const tide_jobs four = {.threads = 4, .run = alone_run};
    compare(&four, 12, true);
}

#ifdef TIDE_EXTERN_SYSTEM_THREADS
TIDE_TEST(extern_systems_on_threads_come_out_as_on_one)
{
    const tide_jobs *jobs = tide_platform_jobs();
    if (!jobs) {
        printf("    one core: nothing to compare\n");
        return;
    }
    compare(jobs, 60, jobs->threads > 2);
    printf("    %u threads\n", (unsigned)jobs->threads);
}
#endif

// A world that went as bytes goes on as the one it was: C keeps nothing of
// its own between ticks.
TIDE_TEST(extern_systems_go_on_from_a_packed_world)
{
    tide_world_init(&world, DT);
    for (int t = 0; t < 5; t++) tide_world_tick(&world);
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(tide_world_pack(&world, bytes, size) == size);
    TIDE_REQUIRE(tide_world_unpack(&many, bytes, size));
    free(bytes);
    TIDE_CHECK(tide_world_hash(&many) == tide_world_hash(&world));
    for (int t = 0; t < 5; t++) {
        tide_world_tick(&world);
        tide_world_tick(&many);
        TIDE_REQUIRE(tide_world_hash(&many) == tide_world_hash(&world));
    }
    TIDE_CHECK(many.Tally.once == 10 && tide_get_Badge(&many, in_slot(&many, BADGED)).serial == INT64_C(1000000300000));
    tide_world_free(&world);
    tide_world_free(&many);
}
