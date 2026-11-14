#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

#ifdef TIDE_EDIT_THREADS
#include "tide/platform.h"
#endif

enum { PARTIES = 3000 };

static tide_world world, shared, kept, replay;

static tide_entity party_at(tide_world *w, const int i)
{
    tide_text_use(&w->heap, NULL);
    return *(const tide_entity *)tide_list_at(w->Roster.parties, i, sizeof(tide_entity));
}

static tide_entity twin_at(tide_world *w, const int i)
{
    tide_text_use(&w->heap, NULL);
    return *(const tide_entity *)tide_list_at(w->Roster.twins, i, sizeof(tide_entity));
}

static int32_t element(tide_world *w, const tide_list list, const int i)
{
    tide_text_use(&w->heap, NULL);
    return *(const int32_t *)tide_list_at(list, i, sizeof(int32_t));
}

static bool skipped(const int i) { return i < 2048 && i % 2 != 0; }

// A world wired into its ring (Wire), in `mode` from then on
static void start(tide_world *w, const Mode mode)
{
    tide_world_init(w, 1.0f);
    w->Settings.mode = Mode_None;
    tide_world_tick(w);
    w->Settings.mode = mode;
}

// Each party is its twin, which the same code changed with Get and Set: Edit is what they did, in place
static void check_ring(tide_world *w, const int ticks)
{
    for (int i = 0; i < PARTIES; i++) {
        const tide_entity e = party_at(w, i);
        const Party party = tide_get_Party(w, e);
        const Twin twin = tide_get_Twin(w, twin_at(w, i));
        TIDE_REQUIRE(party.index == i && twin.index == i);
        TIDE_REQUIRE(party.gold == twin.gold && party.given == twin.given);
        TIDE_REQUIRE(party.given == (skipped(i) ? 0 : ticks));
        TIDE_REQUIRE(tide_list_count(party.log) == tide_list_count(twin.log));
        for (int k = 0; k < (int)tide_list_count(party.log); k++) TIDE_REQUIRE(element(w, party.log, k) == element(w, twin.log, k));
        TIDE_REQUIRE(strcmp(tide_str_c(tide_text_read(&w->heap, party.name)), tide_str_c(tide_text_read(&w->heap, twin.name))) == 0);
        // Self() on the Edit's component is the entity it edits
        if (party.gold > 0) TIDE_REQUIRE(tide_entity_equal(party.self, e));
    }
}

TIDE_TEST(world_edit_matches_get_and_set_across_rows_snapshots_and_replay)
{
    start(&world, Mode_Ring);
    start(&shared, Mode_Ring);
    TIDE_REQUIRE(tide_world_hash(&world) == tide_world_hash(&shared));
    for (int tick = 0; tick < 4; tick++) {
        tide_world_copy(&kept, &shared);
        const uint64_t before = tide_world_hash(&kept);
        tide_world_tick(&world);
        tide_world_tick(&shared);
        check_ring(&world, tick + 1);
        check_ring(&shared, tick + 1);
        TIDE_REQUIRE(tide_world_hash(&world) == tide_world_hash(&shared));
        TIDE_REQUIRE(tide_world_hash(&kept) == before); // Written apart from the snapshot it shared pages with
        TIDE_REQUIRE(tide_scratch_mark() == 0);
    }
    // A rollback: the kept tick run again gives the same world
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
    TIDE_REQUIRE(tide_world_hash(&replay) == tide_world_hash(&world));
    tide_world_tick(&replay);
    tide_world_tick(&world);
    TIDE_REQUIRE(tide_world_hash(&replay) == tide_world_hash(&world));
    check_ring(&replay, 5);
    tide_world_free(&world);
    tide_world_free(&shared);
    tide_world_free(&kept);
    tide_world_free(&replay);
}

TIDE_TEST(world_edit_puts_changes_back_on_continue_break_and_return_and_skips_missing_entities)
{
    start(&world, Mode_Exits);
    tide_world_tick(&world);
    for (int i = 0; i < PARTIES; i++) {
        const int32_t gold = tide_get_Party(&world, party_at(&world, i)).gold;
        const int32_t want = i > 7 ? 0 : i % 3 == 0 ? 1 : i == 7 ? 11 : 111;
        TIDE_REQUIRE(gold == want + (i == 1 ? 1000 : 0));
    }
    TIDE_CHECK(world.Results.bumped == 1111); // The value `return` read, put back before it left
    TIDE_CHECK(world.Results.ran == 0 && world.Results.failures == 0);
    TIDE_CHECK(!tide_has_Party(&world, world.Roster.gone) && !tide_has_Party(&world, world.Roster.lonely));
    TIDE_CHECK(tide_scratch_mark() == 0);
    tide_world_free(&world);
}

TIDE_TEST(world_edit_of_the_codes_own_binding_and_of_one_entity_twice)
{
    start(&world, Mode_Alias);
    static int32_t gold[PARTIES], logs[PARTIES];
    memset(gold, 0, sizeof gold);
    memset(logs, 0, sizeof logs);
    for (int tick = 1; tick <= 3; tick++) {
        tide_world_copy(&kept, &world);
        const uint64_t before = tide_world_hash(&kept);
        tide_world_tick(&world);
        for (int i = 0; i < PARTIES; i++) {
            if (skipped(i)) continue;
            gold[i] += 111; // Its own: 10, then 1 through Pay's Edit of this entity, then 100
            const int next = (i * 7 + 1) % PARTIES;
            gold[next] += 7; // Its next's, through two Edits of that one entity
            logs[next] += 2;
        }
        for (int i = 0; i < PARTIES; i++) {
            const Party party = tide_get_Party(&world, party_at(&world, i));
            TIDE_REQUIRE(party.gold == gold[i]);
            TIDE_REQUIRE((int32_t)tide_list_count(party.log) == logs[i]);
            for (int k = 0; k + 1 < logs[i]; k += 2) {
                TIDE_REQUIRE(element(&world, party.log, k) == 7 && element(&world, party.log, k + 1) == k + 1);
            }
        }
        TIDE_REQUIRE(tide_world_hash(&kept) == before);
        TIDE_REQUIRE(tide_scratch_mark() == 0);
    }
    tide_world_copy(&replay, &kept);
    tide_world_tick(&replay);
    TIDE_REQUIRE(tide_world_hash(&replay) == tide_world_hash(&world));
    tide_world_free(&world);
    tide_world_free(&kept);
    tide_world_free(&replay);
}

TIDE_TEST(world_edit_of_containers_beside_a_set_of_the_same_entity)
{
    start(&world, Mode_Containers);
    for (int tick = 0; tick < 3; tick++) {
        tide_world_copy(&kept, &world);
        const uint64_t before = tide_world_hash(&kept);
        tide_world_tick(&world);
        const Bag bag = tide_get_Bag(&world, world.Roster.bag);
        TIDE_CHECK(bag.value == 21 && tide_list_count(bag.data) == 0);
        TIDE_CHECK(strcmp(tide_str_c(tide_text_read(&world.heap, bag.label)), "cleared") == 0);
        TIDE_CHECK(tide_get_Field(&world, world.Roster.field).marks == tick + 1);
        TIDE_CHECK(world.Results.failures == 0);
        TIDE_CHECK(tide_world_hash(&kept) == before && tide_scratch_mark() == 0);
    }
    tide_world_copy(&replay, &kept);
    tide_world_tick(&replay);
    TIDE_CHECK(tide_world_hash(&replay) == tide_world_hash(&world));
    tide_world_free(&world);
    tide_world_free(&kept);
    tide_world_free(&replay);
}

TIDE_TEST(world_edit_in_handlers_tasks_commands_and_views)
{
    static tide_local local;
    static tide_draw_list draw;
    static tide_gui gui;
    start(&world, Mode_Events);
    tide_world_tick(&world); // Poke: 5; Resume: 1, and waits
    TIDE_CHECK(tide_get_Party(&world, party_at(&world, 2)).gold == 5);
    TIDE_CHECK(tide_get_Party(&world, party_at(&world, 3)).gold == 1);
    TIDE_CHECK(world.Results.started == 1 && world.Results.ended == 0);
    world.Settings.mode = Mode_None;
    tide_world_copy(&kept, &world);
    tide_world_tick(&world); // Resume goes on: 2 more
    TIDE_CHECK(tide_get_Party(&world, party_at(&world, 3)).gold == 3);
    TIDE_CHECK(world.Results.ended == 1);
    tide_world_copy(&replay, &kept);
    tide_world_tick(&replay);
    TIDE_CHECK(tide_world_hash(&replay) == tide_world_hash(&world));

    // A command's handler, and a view's Edit of a local component
    tide_local_init(&local);
    tide_local_set_session(&local, TIDE_SESSION_CONNECTED, tide_player_from_index(0), 0, false, false, "");
    const uint64_t match = tide_world_hash(&world);
    for (int frame = 0; frame < 4; frame++) {
        tide_draw_reset(&draw);
        tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    }
    TIDE_CHECK(local.Viewer.seen == 12);
    TIDE_CHECK(tide_get_tide_local_component_LocalValue(&local, local.Viewer.entity).value == 6);
    TIDE_CHECK(tide_world_hash(&world) == match); // A view changes nothing of the match
    local.Viewer.submit = true;
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    uint32_t size = 0;
    const uint8_t *bytes = tide_local_command(&local, 0, &size);
    TIDE_REQUIRE(bytes && size);
    TIDE_REQUIRE(tide_game_api.command(&world, tide_player_from_index(0), 0, 1, bytes, size));
    tide_world_tick(&world);
    TIDE_CHECK(tide_get_Party(&world, party_at(&world, 4)).gold == -3);
    tide_world_free(&world);
    tide_world_free(&kept);
    tide_world_free(&replay);
    tide_local_free(&local);
}

#ifdef TIDE_EDIT_THREADS
TIDE_TEST(world_edit_on_threads_is_one_threads)
{
    const tide_jobs *jobs = tide_platform_jobs();
    start(&world, Mode_Ring);
    start(&shared, Mode_Ring);
    for (int tick = 0; tick < 3; tick++) {
        tide_world_copy(&kept, &shared);
        const uint64_t before = tide_world_hash(&kept);
        tide_world_tick(&world);
        tide_world_tick_on(&shared, jobs);
        TIDE_REQUIRE(tide_world_hash(&world) == tide_world_hash(&shared));
        TIDE_REQUIRE(tide_world_hash(&kept) == before);
    }
    check_ring(&shared, 3);
    world.Settings.mode = shared.Settings.mode = Mode_Alias;
    for (int tick = 0; tick < 2; tick++) {
        tide_world_tick(&world);
        tide_world_tick_on(&shared, jobs);
        TIDE_REQUIRE(tide_world_hash(&world) == tide_world_hash(&shared));
    }
    tide_world_free(&world);
    tide_world_free(&shared);
    tide_world_free(&kept);
}
#endif
