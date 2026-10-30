// A system's loop over its entities' rows (rows.tide), every way generated code runs it: the world
// must come out bit-identical whether its pages are its own or a snapshot's, whether a chunk goes
// whole or in pieces, and on any thread count. A page no row changed stays the snapshot's, with its hash.

#include <stdio.h>

#include "game.h"
#include "tide/jobs.h"
#include "tide_test.h"

#ifdef TIDE_ROWS_THREADS
#include "tide/platform.h"
#endif

#define DT (1.0f / 64.0f)
#define TICKS 24

static tide_world world, kept;

// The world's hash after each tick, with no snapshot and on this thread.
static uint64_t plain[TICKS];
static bool plain_known;

// The two players' input for a tick: their entities are driven by it.
static void drive(tide_world *w, const int tick)
{
    Controls input = {0};
    input.move.x = (float)(tick % 5 - 2) * 0.25f;
    input.jump = tick % 7 == 3;
    tide_world_set_input(w, tide_player_from_index(0), input);
    input.move.x = -input.move.x;
    input.jump = tick % 3 == 1;
    tide_world_set_input(w, tide_player_from_index(1), input);
}

static void tick(const tide_jobs *jobs)
{
    if (jobs) tide_world_tick_on(&world, jobs);
    else tide_world_tick(&world);
}

// The match from its start: each tick's hash. With `keep`, a snapshot of the tick before shares
// the world's pages while each tick runs, as a client's does, and must stay as it was.
static void play(const tide_jobs *jobs, const bool keep, uint64_t *hashes)
{
    tide_world_init(&world, DT);
    for (int t = 0; t < TICKS; t++) {
        drive(&world, t);
        uint64_t before = 0;
        if (keep) {
            tide_world_copy(&kept, &world);
            before = tide_world_hash(&kept);
        }
        tick(jobs);
        hashes[t] = tide_world_hash(&world);
        if (keep) TIDE_REQUIRE(tide_world_hash(&kept) == before);
    }
    tide_world_free(&world);
    tide_world_free(&kept);
}

static void know_plain(void)
{
    if (plain_known) return;
    play(NULL, false, plain);
    plain_known = true;
}

static void same_as_plain(const tide_jobs *jobs, const bool keep)
{
    know_plain();
    uint64_t hashes[TICKS];
    play(jobs, keep, hashes);
    for (int t = 0; t < TICKS; t++) {
        if (hashes[t] != plain[t]) printf("    tick %d came out different\n", t);
        TIDE_REQUIRE(hashes[t] == plain[t]);
    }
}

// A pool of one thread that says it has eight: the tick takes its threaded path and runs it all
// here. A system's four chunks (three unowned, one the players') are fewer than the threads, so each goes in pieces.
static bool pooled;

static void alone_run(void *self, void (*work)(void *context, uint32_t thread), void *context)
{
    (void)self;
    pooled = true;
    work(context, 0);
}

static const tide_jobs eight = {.threads = 8, .run = alone_run};

TIDE_TEST(rows_are_the_same_on_every_platform)
{
    know_plain();
    // The cases below need entities in three chunks, some of every kind
    tide_world_init(&world, DT);
    for (int t = 0; t < TICKS; t++) {
        drive(&world, t);
        tide_world_tick(&world);
    }
    TIDE_CHECK(world.arch0_Body_Stats_Label_Cargo_Still_Bulk_Hull.count == 4990 && world.arch0_Body_Stats_Label_Cargo_Still_Bulk_Hull.chunks == 3);
    TIDE_CHECK(world.arch2_Owner_Body_Stats_Label_Cargo_Still_Bulk_Hull.count == 10);
    TIDE_CHECK(world.arch1_Spark.count > 100);
    if (tide_world_hash(&world) != UINT64_C(0xda6b56b7c7ee87e4)) printf("    the match ends as 0x%016llx\n", (unsigned long long)tide_world_hash(&world));
    TIDE_CHECK(tide_world_hash(&world) == UINT64_C(0xda6b56b7c7ee87e4));
    TIDE_CHECK(tide_world_hash(&world) == plain[TICKS - 1]);
    tide_world_free(&world);
}

TIDE_TEST(rows_written_apart_come_out_as_in_place)
{
    same_as_plain(NULL, true);
}

TIDE_TEST(rows_in_pieces_come_out_as_whole)
{
    pooled = false;
    same_as_plain(&eight, false);
    TIDE_CHECK(pooled);
}

TIDE_TEST(rows_in_pieces_written_apart_come_out_as_in_place)
{
    same_as_plain(&eight, true);
}

TIDE_TEST(rows_on_threads_come_out_as_on_one)
{
#ifdef TIDE_ROWS_THREADS
    const tide_jobs *jobs = tide_platform_jobs();
    if (!jobs) {
        printf("    one thread here: nothing to check\n");
        return;
    }
    same_as_plain(jobs, false);
    same_as_plain(jobs, true);
    printf("    %u threads\n", (unsigned)jobs->threads);
#else
    printf("    built without the platform layer's threads\n");
#endif
}

// Whether the world and its snapshot still share a chunk's column page, with its hash: nothing wrote it or has to rehash it.
static bool shared(const uint32_t chunk, const uint32_t column)
{
    const tide_table *now = &world.arch0_Body_Stats_Label_Cargo_Still_Bulk_Hull, *then = &kept.arch0_Body_Stats_Label_Cargo_Still_Bulk_Hull;
    const uint32_t at = chunk * tide_columns_arch0_Body_Stats_Label_Cargo_Still_Bulk_Hull.count + column;
    return now->pages[at] == then->pages[at] && now->pages[at]->hashed != UINT32_MAX;
}

// A tick with a snapshot sharing every page: which pages are still the snapshot's afterwards.
// Tick 9 is odd, so Rest changes no row.
static void pages_stay(const tide_jobs *jobs)
{
    enum {
        CARGO = tide_column_arch0_Body_Stats_Label_Cargo_Still_Bulk_Hull_Cargo,
        STILL = tide_column_arch0_Body_Stats_Label_Cargo_Still_Bulk_Hull_Still,
        LABEL = tide_column_arch0_Body_Stats_Label_Cargo_Still_Bulk_Hull_Label,
        STATS = tide_column_arch0_Body_Stats_Label_Cargo_Still_Bulk_Hull_Stats,
        BODY = tide_column_arch0_Body_Stats_Label_Cargo_Still_Bulk_Hull_Body,
    };
    tide_world_init(&world, DT);
    for (int t = 0; t < 10; t++) {
        drive(&world, t);
        tide_world_copy(&kept, &world);
        tide_world_hash(&kept); // Every page hashed, as a client's are once it has compared a tick
        tick(jobs);
    }
    // The tick that just ran was tick 9 (Time.tick counts from 0)
    TIDE_REQUIRE(world.Time.tick == 10);
    for (uint32_t chunk = 0; chunk < 3; chunk++) {
        // Stats: only read
        TIDE_CHECK(shared(chunk, STATS) && shared(chunk, STATS + 1));
        // Still: taken to change by Rest, which changed no row in an odd tick
        for (uint32_t lane = 0; lane < 7; lane++) TIDE_CHECK(shared(chunk, STILL + lane));
        // Body: Move changes every body's position
        TIDE_CHECK(!shared(chunk, BODY) && !shared(chunk, BODY + 1));
        // Label: Weigh changes its hits and its weight (lanes 0 and 6: its name and its list are a handle each,
        // two lanes, after 4 bytes of padding)
        TIDE_CHECK(!shared(chunk, LABEL) && !shared(chunk, LABEL + 6));
    }
    // ...and its name and its list are Rename's to change, for the frozen: the second chunk has none. A handle's high
    // word is the same zero before and after, so only its low word's lane is the table's own
    TIDE_CHECK(shared(1, LABEL + 2) && shared(1, LABEL + 4));
    TIDE_CHECK(!shared(0, LABEL + 2) && !shared(0, LABEL + 4));
    // Cargo, which Shift changes unless the entity is frozen: the first
    // chunk's all are, so Shift took none of its pages, and Thaw, which is
    // for the frozen and changes nothing in an odd tick, wrote them all as
    // they were; the second's none are; the third's every third is, and
    // those rows went through as they were
    for (uint32_t lane = 0; lane < 6; lane++) TIDE_CHECK(shared(0, CARGO + lane));
    TIDE_CHECK(!shared(1, CARGO) && !shared(1, CARGO + 5));
    TIDE_CHECK(!shared(2, CARGO) && !shared(2, CARGO + 5));
    for (uint32_t row = 4096; row < 4990; row++) {
        const Cargo now = TIDE_AT(&world, arch0_Body_Stats_Label_Cargo_Still_Bulk_Hull, Cargo, row);
        const Cargo then = TIDE_AT(&kept, arch0_Body_Stats_Label_Cargo_Still_Bulk_Hull, Cargo, row);
        const bool frozen = tide_has_Frozen(&world, TIDE_ENTITY_AT(&world, arch0_Body_Stats_Label_Cargo_Still_Bulk_Hull, row));
        TIDE_REQUIRE(frozen == (row % 3 == 0));
        TIDE_REQUIRE(now.moves == then.moves + (frozen ? 0 : 1));
        TIDE_REQUIRE(!frozen || (now.at.x == then.at.x && now.size.x == then.size.x && now.size.y == then.size.y));
    }
    // An even tick: Rest changes the rows of one kind in nine, so the lanes
    // it changed are the world's own now, and the ones it didn't still aren't
    drive(&world, 10);
    tide_world_copy(&kept, &world);
    tide_world_hash(&kept);
    tick(jobs);
    for (uint32_t chunk = 0; chunk < 3; chunk++) {
        TIDE_CHECK(shared(chunk, STILL) && shared(chunk, STILL + 1) && shared(chunk, STILL + 2)); // Where it's at
        TIDE_CHECK(shared(chunk, STILL + 3) && shared(chunk, STILL + 4) && shared(chunk, STILL + 5)); // Where it was: the same since tick 0
        TIDE_CHECK(!shared(chunk, STILL + 6)); // Its count
    }
    tide_world_free(&world);
    tide_world_free(&kept);
}

TIDE_TEST(rows_leave_the_pages_they_change_nothing_of)
{
    pages_stay(NULL);
}

TIDE_TEST(rows_in_pieces_leave_the_pages_they_change_nothing_of)
{
    pages_stay(&eight);
}
