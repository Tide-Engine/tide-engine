#include <stdio.h>
#include <stdlib.h>

#include "game.h"
#include "tide_test.h"

#ifdef TIDE_SET_READ_THREADS
#include "tide/platform.h"
#endif

static tide_world world, shared, kept, replay;

static tide_entity at(tide_world *w, const tide_list list, const int i)
{
    tide_text_use(&w->heap, NULL);
    return *(const tide_entity *)tide_list_at(list, i, sizeof(tide_entity));
}

TIDE_TEST(world_set_read_parameters_refresh_when_tasks_resume)
{
    tide_world_init(&world, 1.0f);
    world.Results.action = 2;
    for (int t = 0; t < 2; t++) {
        tide_world_copy(&kept, &world);
        const uint64_t before = tide_world_hash(&kept);
        tide_world_tick(&world);
        TIDE_CHECK(tide_world_hash(&kept) == before);
        TIDE_CHECK(world.Results.resumes == t + 1 && world.Results.asyncStale == 0);
        TIDE_CHECK(tide_scratch_mark() == 0);
    }
    TIDE_CHECK(tide_get_Record(&world, at(&world, world.Roster.records, 0)).value == 13);
    tide_world_free(&world);
    tide_world_free(&kept);
}

TIDE_TEST(world_set_read_parameters_in_local_views)
{
    static tide_local local;
    static tide_draw_list draw;
    static tide_gui gui;
    tide_world_init(&world, 1.0f);
    tide_local_init(&local);
    for (int frame = 0; frame < 3; frame++) {
        tide_draw_reset(&draw);
        tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    }
    TIDE_CHECK(local.Viewer.sum == 10);
    TIDE_CHECK(tide_get_tide_local_component_LocalValue(&local, local.Viewer.first).value == 5);
    TIDE_CHECK(tide_get_tide_local_component_LocalValue(&local, local.Viewer.second).value == 4);
    tide_world_free(&world);
    tide_local_free(&local);
}

static void inputs(tide_world *w)
{
    tide_world_set_input(w, tide_player_from_index(0), (Controls){.value = 10});
    tide_world_set_input(w, tide_player_from_index(1), (Controls){.value = 20});
}

typedef struct keyed_row { tide_entity key; int index; } keyed_row;

static int compare_keyed_row(const void *a, const void *b)
{
    return tide_entity_compare(((const keyed_row *)a)->key, ((const keyed_row *)b)->key);
}

static void check_rows(tide_world *w, const int ticks)
{
    // Independent integer propagation in the defined logical Entity order.
    // The source's ring links retain list order, which differs from query order.
    typedef struct expected_rows {
        keyed_row order[5000];
        int direct[5000], entered[5000], filtered[5000], selected[5000];
    } expected_rows;
    expected_rows *expected = calloc(1, sizeof *expected);
    TIDE_REQUIRE(expected != NULL);
    for (int i = 0; i < 5000; i++) {
        expected->order[i] = (keyed_row){at(w, w->Roster.links, i), i};
        expected->direct[i] = expected->filtered[i] = 1;
    }
    qsort(expected->order, 5000, sizeof expected->order[0], compare_keyed_row);
    keyed_row drivers[3];
    int owner[3] = {0, 1, 1}, driven[3] = {0};
    for (int i = 0; i < 3; i++) drivers[i] = (keyed_row){at(w, w->Roster.drivers, i), i};
    qsort(drivers, 3, sizeof drivers[0], compare_keyed_row);
    for (int tick = 0; tick < ticks; tick++) {
        for (int k = 0; k < 5000; k++) {
            const int i = expected->order[k].index;
            expected->entered[i] = expected->direct[i];
            expected->direct[(i + 1) % 5000] = expected->direct[i] + 1;
        }
        for (int k = 0; k < 5000; k++) {
            const int i = expected->order[k].index;
            if (i < 2048 && i % 2 != 0) continue;
            expected->selected[i] = expected->filtered[i];
            const int next = i + (i < 2048 ? 2 : 1);
            expected->filtered[next % 5000] = expected->filtered[i] + 1;
        }
        for (int k = 0; k < 3; k++) {
            const int i = drivers[k].index;
            driven[i] = owner[i] == 0 ? 10 : 20;
            owner[(i + 1) % 3] = owner[i];
        }
    }
    for (int i = 0; i < 5000; i++) {
        const tide_entity e = at(w, w->Roster.links, i);
        const Link link = tide_get_Link(w, e);
        const Seen seen = tide_get_Seen(w, e);
        TIDE_REQUIRE(link.direct == expected->direct[i]);
        TIDE_REQUIRE(link.helper == link.direct);
        TIDE_REQUIRE(seen.direct == expected->entered[i] && seen.helper == expected->entered[i]);
        const Filtered filtered = tide_get_Filtered(w, e);
        if (i >= 2048 || i % 2 == 0) {
            TIDE_REQUIRE(filtered.value == expected->filtered[i]);
            TIDE_REQUIRE(seen.filtered == expected->selected[i]);
        } else {
            TIDE_REQUIRE(filtered.value == 1 && seen.filtered == 0);
        }
    }
    for (int i = 0; i < 3; i++) {
        const tide_entity e = at(w, w->Roster.drivers, i);
        TIDE_REQUIRE(tide_get_Driven(w, e).seen == driven[i]);
        TIDE_REQUIRE(tide_player_index(tide_get_Owner(w, e).player) == owner[i]);
    }
    free(expected);
}

TIDE_TEST(world_set_reads_follow_other_rows_and_replaced_pages)
{
    tide_world_init(&world, 1.0f);
    tide_world_init(&shared, 1.0f);
    for (int tick = 1; tick <= 4; tick++) {
        inputs(&world);
        inputs(&shared);
        tide_world_copy(&kept, &shared);
        const uint64_t before = tide_world_hash(&kept);
        tide_world_tick(&world);
        tide_world_tick(&shared);
        check_rows(&world, tick);
        check_rows(&shared, tick);
        TIDE_REQUIRE(tide_world_hash(&world) == tide_world_hash(&shared));
        TIDE_REQUIRE(tide_world_hash(&kept) == before);
    }
    // Roll back to the previous tick and repeat it, then round-trip all state.
    tide_world_copy(&replay, &kept);
    tide_world_tick(&replay);
    TIDE_REQUIRE(tide_world_hash(&replay) == tide_world_hash(&world));
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(bytes != NULL);
    tide_world_pack(&world, bytes, size);
    tide_world_free(&replay);
    TIDE_REQUIRE(tide_world_unpack(&replay, bytes, size));
    free(bytes);
    inputs(&world);
    inputs(&replay);
    tide_world_tick(&world);
    tide_world_tick(&replay);
    check_rows(&replay, 5);
    TIDE_CHECK(tide_world_hash(&world) == tide_world_hash(&replay));
    tide_world_free(&world);
    tide_world_free(&shared);
    tide_world_free(&kept);
    tide_world_free(&replay);
}

TIDE_TEST(world_set_read_parameters_keep_their_values_in_handlers)
{
    tide_world_init(&world, 1.0f);
    tide_world_copy(&kept, &world);
    const uint64_t before = tide_world_hash(&kept);
    world.Results.action = 1;
    tide_world_tick(&world);
    // First sees 10 and gives the second 13; the second sees that 13, then
    // gives the first 16 and itself 14. Its read parameter remains 13.
    if (world.Results.before != 23 || world.Results.after != 25 || world.Results.stale != 0)
        printf("before %d, after %d, stale %d\n", world.Results.before, world.Results.after, world.Results.stale);
    TIDE_CHECK(world.Results.before == 23 && world.Results.after == 25 && world.Results.stale == 0);
    const Record first = tide_get_Record(&world, at(&world, world.Roster.records, 0));
    const Record second = tide_get_Record(&world, at(&world, world.Roster.records, 1));
    TIDE_CHECK(first.value == 16 && second.value == 14);
    tide_text_use(&world.heap, NULL);
    TIDE_CHECK(*(const int32_t *)tide_list_at(first.data, 0, sizeof(int32_t)) == 16);
    TIDE_CHECK(*(const int32_t *)tide_list_at(second.data, 0, sizeof(int32_t)) == 14);
    TIDE_CHECK(tide_world_hash(&kept) == before);
    TIDE_CHECK(tide_scratch_mark() == 0);
    tide_world_free(&world);
    tide_world_free(&kept);
}

#ifdef TIDE_SET_READ_THREADS
TIDE_TEST(world_set_read_parameters_on_the_threaded_scheduler)
{
    tide_world_init(&world, 1.0f);
    inputs(&world);
    tide_world_copy(&kept, &world);
    const uint64_t before = tide_world_hash(&kept);
    tide_world_tick_on(&world, tide_platform_jobs());
    check_rows(&world, 1);
    TIDE_CHECK(tide_world_hash(&kept) == before);
    tide_world_free(&world);
    tide_world_free(&kept);
}
#endif
