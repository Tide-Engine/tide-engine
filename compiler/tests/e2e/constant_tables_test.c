#include "game.h"
#include "tide_test.h"
#include "tide/time.h"
#include "tide/worker.h"

#include <string.h>

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

static const Tables *ticked(const int ticks)
{
    tide_world_init(&world, 1.0f);
    for (int i = 0; i < ticks; i++) tide_world_tick(&world);
    return &world.Tables;
}

TIDE_TEST(constant_arrays_and_lists_read_by_index)
{
    const Tables *t = ticked(2); // tick 1: i = 1
    TIDE_CHECK(t->size == 2.0f);
    TIDE_CHECK(t->corner == 0); // QUAD[3]
    TIDE_CHECK(t->quadSum == 8);
    TIDE_CHECK(t->stepped.x == 0 && t->stepped.y == 0);
    TIDE_CHECK(t->cornerSum.x == 2.0f && t->cornerSum.y == 1.0f && t->cornerSum.z == 0.0f);
    TIDE_CHECK(t->counts == 64);
}

TIDE_TEST(constant_tables_of_structs_enums_and_arrays)
{
    const Tables *t = ticked(1);
    TIDE_CHECK(t->spawnSum == -1.0f); // -3 + 2
    TIDE_CHECK(t->spawnCount == 6);   // 1 + 3 + Cedar's 2
    TIDE_CHECK(t->moreCount == 7);    // 1 + 5 + Birch's 1 + Ash's 0
    TIDE_CHECK(t->kinds == 210);      // Birch 1, Ash 0, two of them
    TIDE_CHECK(t->order == 201);      // Cedar, Ash, Birch
    TIDE_CHECK(t->perKind == 9.0f);   // Cedar: 2 * SIZES[3], Ash: 0.5 * SIZES[1]
    TIDE_CHECK(t->grid == 262);       // 6 * 10 + 2 + 200
    TIDE_CHECK(t->masks == 254129);   // 128 + 1 + 4000, PRIMES[2] and TRIANGLE[2] through aliases
}

TIDE_TEST(constant_tables_are_zero_past_either_end)
{
    const Tables *t = ticked(4); // i = 3
    TIDE_CHECK(t->outside == 0);
    TIDE_CHECK(t->none == 0);
}

TIDE_TEST(constant_tables_work_out_constants_and_cases)
{
    const Tables *t = ticked(4); // i = 3: QUAD[5]'s case
    TIDE_CHECK(t->fromConstants == 2 + 30 + 400 + 2000 + 60000);
    TIDE_CHECK(t->cased == 3);
    TIDE_CHECK(t->firstSize == 5);
    TIDE_CHECK(sizeof(t->sized.at) / sizeof(t->sized.at[0]) == 6);
    t = ticked(2);
    TIDE_CHECK(t->cased == 1);
}

TIDE_TEST(list_constants_are_lists_where_a_list_goes)
{
    const Tables *t = ticked(1);
    TIDE_CHECK(t->found);
    TIDE_CHECK(t->index == 15);         // QUAD.IndexOf(3) is 5, CORNERS.IndexOf is 1
    TIDE_CHECK(t->same == 9);           // SAME_QUAD[5] + its count
    TIDE_CHECK(t->helper == 808.0f);    // Through a function taking a List<int>
    TIDE_CHECK(t->kept.count == 3 && t->kept.kind == Kind_Cedar && t->kept.at.x == -3.0f);
}

TIDE_TEST(a_list_constant_copied_into_the_world_is_its_own)
{
    const Tables *t = ticked(2); // Copied again each tick, then added to: never the constant's
    TIDE_CHECK(t->copiedCount == 67); // Seven in the world's copy, six in the constant
    TIDE_CHECK(t->copiedLast == 9);   // The copy's seventh; the constant has none
    TIDE_CHECK(t->quadSum == 8);
}

TIDE_TEST(views_and_workers_read_constant_tables)
{
    tide_world_init(&world, 1.0f);
    tide_local_init(&local);
    tide_world_tick(&world);
    gui.jobs = NULL; // The worker runs as the frame waits for it
    const tide_worker_limits limits = {.pending = 1, .transfer_bytes = 1};
    gui.worker_limits = &limits;
    const uint64_t until = tide_time_now_ns() + 10000000000ull;
    while (!local.Display.done && tide_time_now_ns() < until) {
        tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
        tide_yield();
    }
    TIDE_CHECK(local.Display.done);
    TIDE_CHECK(local.Display.size == 2.0f + world.Tables.size); // SIZES[Birch], and what the match read
    TIDE_CHECK(local.Display.quad == 9);                        // QUAD[5] + six
    TIDE_CHECK(local.Display.worker == 27);
    TIDE_CHECK(draw.count > 0); // The view drew the constants' mesh
    tide_local_free(&local);
    tide_world_free(&world);
    tide_draw_free(&draw);
    gui.worker_limits = NULL;
}
