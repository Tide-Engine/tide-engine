#include <string.h>

#include "game.h"
#include "tide_test.h"

// Setup spawns the Other, then six Fats, in the tick's order: slots after the Main scene's.
enum { OTHER = 1, FAT = 2, FATS = 6, ROW_TICKS = 3 };

static tide_entity fat(const tide_world *w, const int i)
{
    return tide_entity_in_slot(&w->entities, (uint32_t)(FAT + i));
}

// What every system leaves after `ticks` ticks with rows (the first tick only spawns them)
static void check_world(const tide_world *w, const int ticks)
{
    TIDE_REQUIRE(tide_has_Other(w, tide_entity_in_slot(&w->entities, OTHER)));
    for (int i = 0; i < FATS; i++) {
        TIDE_REQUIRE(tide_has_Fat(w, fat(w, i)));
        const Fat f = tide_get_Fat(w, fat(w, i));
        const bool live = i % 2 == 0;
        // Step: for the live ones alone, behind its test
        TIDE_CHECK(f.steps == (live ? ticks : 0));
        TIDE_CHECK(f.at.x == (float)i + (live ? 0.5f * (float)(i + 1) * (float)ticks : 0.0f) && f.at.y == 0.0f);
        // Flag: written when the tick's number is odd (the third), and left as it was on the others
        TIDE_CHECK(f.flag == (ticks >= 2 ? 3 : -1));
        // What no system writes is as it was spawned
        TIDE_CHECK(f.spare.at[7] == 100 + i && f.spare.at[0] == 0 && f.speed == 0.5f * (float)(i + 1) && f.live == live);
        TIDE_CHECK(f.kind == (i == 0 ? Kind_Human : i == 5 ? Kind_Hauler : Kind_Bot));
        // Reach: the one with a target, through a helper handed the world and the ground
        TIDE_CHECK(f.marks == (i == 3 ? ticks : 0));
        // Plan: the live bots with a path of three, once; the helper read its own copy after changing the path
        TIDE_CHECK(f.next == (i == 2 || i == 4 ? 5 : 0));
        // Pick: the chosen one that's live
        TIDE_CHECK(f.picked == (i == 0 ? 10 * ticks : 0));
        // Lead: the one whose target is the first Fat, by that one's steps and its own, read through the World
        TIDE_CHECK(f.until == (i == 4 ? 2.0 * (double)ticks : 0.0));
    }
    const Other o = tide_get_Other(w, tide_entity_in_slot(&w->entities, OTHER));
    TIDE_CHECK(o.n == 1 + 70 * ticks); // The ground's 64 cells, and the fourth of them, each tick
    TIDE_CHECK(w->Sum.humans == 1 && w->Sum.total == ticks);
    TIDE_CHECK(w->Sum.logged == ticks - 1); // As Append's call began, a row before what it Set
    TIDE_CHECK(w->Sum.left == 0 && w->Sum.routed == 2);
}

// A world written in place, and one written apart beside a snapshot kept each tick, as a client's is: the
// rows a guard leaves out carry the lanes their system changes across as they were.
TIDE_TEST(row_lanes_in_place_and_apart_agree)
{
    tide_world plain = {0}, apart = {0}, kept = {0};
    tide_world_init(&plain, 1.0f / 60.0f);
    tide_world_init(&apart, 1.0f / 60.0f);
    for (int tick = 0; tick <= ROW_TICKS; tick++) {
        tide_world_free(&kept);
        tide_world_copy(&kept, &apart);
        tide_world_tick(&apart);
        tide_world_tick(&plain);
        TIDE_CHECK(tide_world_hash(&plain) == tide_world_hash(&apart));
        if (tick) {
            check_world(&plain, tick);
            check_world(&apart, tick);
        }
    }
    // The snapshot is the tick before
    check_world(&kept, ROW_TICKS - 1);
    tide_world_free(&kept);
    tide_world_free(&apart);
    tide_world_free(&plain);
}

static tide_world before;
static tide_world now;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_devices devices;

static void frame(const tide_world *previous, const float alpha)
{
    tide_draw_reset(&draw);
    tide_gui_begin(&gui, &devices, tide_f2(640.0f, 360.0f), NULL);
    tide_frame(&now, previous, alpha, NULL, NULL, 1.0f, &local, &draw, &gui);
    tide_gui_end(&gui, &draw);
}

// A view makes its query's rows where its code first names it: none while it returns before that, and the
// rows a whole copy would have given once it goes on, a [Position] field between the two ticks among them.
TIDE_TEST(row_lanes_view_rows_made_when_named)
{
    tide_local_init(&local);
    tide_world_init(&now, 1.0f / 60.0f);
    for (int tick = 0; tick < ROW_TICKS; tick++) tide_world_tick(&now);
    tide_world_copy(&before, &now);
    tide_world_tick(&now);
    const uint64_t now_hash = tide_world_hash(&now), before_hash = tide_world_hash(&before);

    frame(&before, 0.5f);
    const Seen *s = &local.Seen;
    TIDE_CHECK(s->early == 1 && s->rows == 0 && s->x == 0.0f); // Shut: it returned first
    TIDE_CHECK(s->live == 3);                                   // Another view's rows, through a helper
    TIDE_CHECK(s->steps == ROW_TICKS && s->eachX == 1.25f);     // Halfway from 1 to 1.5

    local.Gate.open = true;
    frame(&before, 0.5f);
    // Each live one halfway between its last two ticks: 1.25, 5.75 and 10.25; the others at 1, 3 and 5
    TIDE_CHECK(s->early == 2 && s->rows == FATS && s->x == 26.25f);
    TIDE_CHECK(s->live == 3 && s->steps == ROW_TICKS && s->eachX == 1.25f);

    frame(NULL, 1.0f); // No tick before: the latest as it is
    TIDE_CHECK(s->early == 3 && s->rows == FATS && s->x == 15.0f + 4.5f * (float)ROW_TICKS && s->eachX == 1.5f);
    TIDE_CHECK(tide_world_hash(&now) == now_hash && tide_world_hash(&before) == before_hash);

    tide_local_free(&local);
    tide_world_free(&before);
    tide_world_free(&now);
    tide_draw_free(&draw);
}
