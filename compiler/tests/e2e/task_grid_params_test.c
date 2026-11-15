#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/pack.h"
#include "tide/text.h"
#include "tide_test.h"
#ifdef TASK_GRID_MIGRATION
#include "tide/migrate.h"
extern const tide_layout tide_game_layout;
#endif

// Grids an async function takes (task_grid_params.tide): the task's own copies, made where it's called, in its
// frame across its waits.

// The pack, as C (tide_add_game's --pack-c for HOST programs).
extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

static tide_world w;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

static void frame(void)
{
    tide_local_frame_time(&local, 1.0f / 60.0f);
    tide_draw_reset(&draw);
    tide_frame(&w, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    tide_text_use(NULL, &local.heap);
}

static bool done(const tide_world *a)
{
    return a->Field.stage == 3;
}

static uint32_t holding(const tide_world *a)
{
    return a->tide_tasks_function_Hold.count;
}

// Each Plot's task read its grid as the system that started it had it, before it changed it, but the third's, whose
// entity went while it waited
static bool plots_read(const tide_world *a)
{
    int found = 0;
    for (uint32_t i = 0; i < a->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&a->entities, i);
        if (!tide_has_Plot(a, e)) continue;
        const Plot p = tide_get_Plot(a, e);
        if (p.number == 3 || p.read != p.number * 10 + 9) return false;
        found++;
    }
    return found == 2 && a->tide_tasks_function_Survey.count == 0;
}

static uint64_t packed_into(tide_world *to, const tide_world *from)
{
    const uint32_t bytes = tide_world_pack(from, NULL, 0);
    uint8_t *packed = malloc(bytes);
    if (!packed || tide_world_pack(from, packed, bytes) != bytes) {
        free(packed);
        return 0;
    }
    tide_world_init(to, 1.0f / 60.0f);
    const bool ok = tide_world_unpack(to, packed, bytes);
    free(packed);
    return ok ? tide_world_hash(to) : 0;
}

// A match task's grids: the copies made as Begin and Plant started them, which those systems then changed, held in
// the match's heap while the tasks wait, so the world's hash takes them in, and a snapshot, a packed world and a hot
// reload's migration have them
TIDE_TEST(task_grid_params_of_a_match_task_are_its_own)
{
    static tide_world a, c, d, packed, snap;
    tide_world_init(&a, 1.0f / 60.0f);
    tide_world_init(&c, 1.0f / 60.0f);
    tide_world_init(&d, 1.0f / 60.0f);
    d.Field.first = 5; // Its task's copy says 5 where a's says 3
    tide_world_tick(&a);
    tide_world_tick(&c);
    tide_world_tick(&d);
    TIDE_CHECK(a.Field.stage == 1 && holding(&a) == 1);
    // The caller cleared its grid after the call: what's left of the difference is the frame's copy
    d.Field.first = 3;
    TIDE_CHECK(tide_world_hash(&a) == tide_world_hash(&c));
    TIDE_CHECK(tide_world_hash(&a) != tide_world_hash(&d));
    tide_world_tick(&a);
    tide_world_tick(&c);
    TIDE_CHECK(a.Field.stage == 2 && a.Field.seen == 3 + 4 * 10 && a.Field.later == 7 + 5 * 10);
    // Mid-wait: a snapshot, and a world packed and unpacked
    tide_world_copy(&snap, &a);
    TIDE_CHECK(packed_into(&packed, &a) == tide_world_hash(&a));
#ifdef TASK_GRID_MIGRATION
    // A hot reload's migration of the world's bytes, to the same layout, keeps every waiting task
    static tide_world migrated;
    {
        const uint32_t size = tide_world_pack(&a, NULL, 0);
        uint8_t *bytes = malloc(size);
        TIDE_REQUIRE(bytes != NULL && tide_world_pack(&a, bytes, size) == size);
        void *carried = NULL;
        uint32_t carried_size = 0;
        tide_migration m = {0};
        TIDE_REQUIRE(tide_migrate_world(&tide_game_layout, &tide_game_layout.match, bytes, size, &tide_game_layout,
                                        &tide_game_layout.match, &carried, &carried_size, &m));
        TIDE_CHECK(m.tasks_dropped == 0 && m.entities_dropped == 0);
        tide_world_init(&migrated, 1.0f / 60.0f);
        TIDE_REQUIRE(tide_world_unpack(&migrated, carried, carried_size));
        free(carried);
        free(bytes);
        TIDE_CHECK(tide_world_hash(&migrated) == tide_world_hash(&a) && holding(&migrated) == 1);
    }
#endif
    for (int i = 0; i < 6; i++) {
        tide_world_tick(&a);
        tide_world_tick(&c);
        tide_world_tick(&packed);
        TIDE_CHECK(tide_world_hash(&a) == tide_world_hash(&c) && tide_world_hash(&a) == tide_world_hash(&packed));
#ifdef TASK_GRID_MIGRATION
        tide_world_tick(&migrated);
        TIDE_CHECK(tide_world_hash(&a) == tide_world_hash(&migrated));
#endif
    }
    TIDE_CHECK(done(&a) && holding(&a) == 0 && a.Field.ended == 0);
    TIDE_CHECK(a.Field.deep == 3 * 1000 + 4 * 2 && a.Field.copied == 4 * 100 + 3);
    TIDE_CHECK(done(&packed) && packed.Field.deep == a.Field.deep && packed.Field.copied == a.Field.copied);
    TIDE_CHECK(plots_read(&a) && plots_read(&packed));
#ifdef TASK_GRID_MIGRATION
    TIDE_CHECK(done(&migrated) && migrated.Field.deep == a.Field.deep && plots_read(&migrated));
    tide_world_free(&migrated);
#endif
    // Back to the snapshot, as a rollback: the same ticks again
    const uint64_t end = tide_world_hash(&a);
    tide_world_copy(&a, &snap);
    TIDE_CHECK(a.Field.stage == 2 && holding(&a) == 1);
    for (int i = 0; i < 6; i++) tide_world_tick(&a);
    TIDE_CHECK(tide_world_hash(&a) == end && a.Field.deep == 3008 && plots_read(&a));
    tide_world_free(&a);
    tide_world_free(&c);
    tide_world_free(&d);
    tide_world_free(&packed);
    tide_world_free(&snap);
}

static bool color_is(const tide_color x, const tide_color y)
{
    return memcmp(&x, &y, sizeof x) == 0;
}

// Another pack, as tide run loads one when a file is saved: run's grids 5 and 6 wide, 2 high, of one color
static uint8_t *other_pack(size_t *size)
{
    tide_pack_builder b = {0};
    static tide_color pixels[6 * 2];
    for (int i = 0; i < 12; i++) pixels[i] = (tide_color){0.5f, 0.25f, 0.125f, 1.0f};
    const uint8_t content5[32] = {5}, content6[32] = {6};
    const uint64_t five = tide_pack_grid(&b, content5, 5, 2, pixels, &tide_shape_Grid2_Color);
    const uint64_t six = tide_pack_grid(&b, content6, 6, 2, pixels, &tide_shape_Grid2_Color);
    const uint64_t list = tide_pack_list(&b, (const uint64_t[]){five, six}, 2);
    tide_pack_name(&b, "run", TIDE_PACK_LIST, list, 2, TIDE_PACK_GRID);
    return tide_pack_end(&b, size);
}

static void start_local(void)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&w, 1.0f / 60.0f);
    tide_world_tick(&w);
    tide_local_init(&local);
}

static void finish_local(void)
{
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&w);
}

// A local task's grids: the singleton's, which the view changed after the call, and the pack's, which is loaded
// again while the task waits, each its own copy in the local state's heap, and an awaited function's of its own
TIDE_TEST(task_grid_params_of_a_local_task_are_its_own)
{
    size_t other_size;
    uint8_t *other = other_pack(&other_size);
    TIDE_REQUIRE(other != NULL);
    start_local();
    frame(); // Start: Show begins and waits a frame
    TIDE_CHECK(local.tide_tasks_function_Show.count == 1 && local.Painted.stage == 0);
    const tide_color want = local.Painted.want;
    TIDE_REQUIRE(tide_assets_load(other, other_size)); // Its picture's pack is another now
    for (int k = 0; k < 12 && local.Painted.stage < 1; k++) frame();
    TIDE_CHECK(local.Painted.stage == 1 && local.tide_tasks_function_Show.count == 0);
    TIDE_CHECK(local.Painted.seen == 5 + 6 * 10 && local.Painted.deep == 5 * 1000 + 6 * 3);
    TIDE_CHECK(local.Painted.width == 2 && color_is(local.Painted.pixel, want));
    // Another, with the new pack's run[1]
    local.Painted.startNow = true;
    for (int k = 0; k < 12 && local.Painted.stage < 2; k++) frame();
    TIDE_CHECK(local.Painted.stage == 2 && local.Painted.width == 6);
    TIDE_CHECK(color_is(local.Painted.pixel, (tide_color){0.5f, 0.25f, 0.125f, 1.0f}));
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    free(other);
    finish_local();
}

// The local state as bytes, as a hot reload carries it, while the task waits with its grids
TIDE_TEST(task_grid_params_in_the_local_states_bytes)
{
    start_local();
    frame();
    frame();
    TIDE_CHECK(local.tide_tasks_function_Show.count == 1 && local.Painted.seen == 65 && local.Painted.stage == 0);
    const uint32_t size = tide_local_pack(&local, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(bytes != NULL);
    TIDE_CHECK(tide_local_pack(&local, bytes, size) == size);
    TIDE_CHECK(tide_local_unpack(&local, bytes, size));
    free(bytes);
    tide_text_use(NULL, &local.heap);
    TIDE_CHECK(local.tide_tasks_function_Show.count == 1);
    for (int k = 0; k < 12 && local.Painted.stage < 1; k++) frame();
    TIDE_CHECK(local.Painted.stage == 1 && local.Painted.deep == 5018 && local.Painted.width == 2);
    finish_local();
}

static tide_entity chunk_at(const int32_t x)
{
    for (uint32_t i = 0; i < w.entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w.entities, i);
        if (tide_has_Chunk(&w, e) && tide_get_Chunk(&w, e).at.x == x) return e;
    }
    return (tide_entity){0};
}

// The view finds each chunk's neighbour by an indexed loop over the chunks' places, though it hands its chunk to a
// task as `mut`, which changes only the chunk's local fields; the task's copy of the neighbour's grid is the one
// the view had, though the match changes the neighbour while it waits
TIDE_TEST(task_grid_params_from_a_view_that_indexes_its_chunks)
{
    start_local();
    tide_world_tick(&w); // Make's chunks are there
    const tide_entity first = chunk_at(0), second = chunk_at(1), last = chunk_at(2);
    TIDE_REQUIRE(!tide_entity_is_null(first) && !tide_entity_is_null(second) && !tide_entity_is_null(last));
    frame(); // Each view with a neighbour starts Look, which waits a frame
    TIDE_CHECK(local.tide_tasks_function_Look.count == 2);
    w.Ground.dig = true; // The chunk at x = 1 changes: Look of the chunk at 0 has its own copy of it
    tide_world_tick(&w);
    w.Ground.dig = false;
    for (int k = 0; k < 8 && local.tide_tasks_function_Look.count; k++) frame();
    TIDE_CHECK(local.tide_tasks_function_Look.count == 0);
    const Chunk_local a = tide_get_Chunk_local(&local, first), b = tide_get_Chunk_local(&local, second);
    const Chunk_local c = tide_get_Chunk_local(&local, last);
    TIDE_CHECK(a.asked == 1 && b.asked == 1 && c.asked == 0);
    TIDE_CHECK(a.seen == 256 * (4 * 100 + 2) + 2 * 10000);
    TIDE_CHECK(b.seen == 256 * (5 * 100 + 3) + 3 * 10000);
    TIDE_CHECK(c.seen == 0);
    finish_local();
}

// A chunk that goes while its local task waits with the neighbour's grid it was handed: the task ends at the next
// frame's task pass, its frame and its copy of the grid let go of with it (the sanitizers' leak and use checks), and
// the other chunk's task goes on as before. The voxels mesher's commonest end: a chunk leaves while its faces are made
TIDE_TEST(task_grid_params_of_a_local_task_whose_entity_goes)
{
    start_local();
    tide_world_tick(&w);
    const tide_entity first = chunk_at(0), second = chunk_at(1);
    TIDE_REQUIRE(!tide_entity_is_null(first) && !tide_entity_is_null(second));
    frame(); // Both Looks wait a frame, each with its own copy of its neighbour's grid
    TIDE_CHECK(local.tide_tasks_function_Look.count == 2);
    w.Ground.drop = true;
    tide_world_tick(&w);
    w.Ground.drop = false;
    TIDE_CHECK(!tide_entity_alive(&w.entities, first) && tide_entity_alive(&w.entities, second));
    frame();
    TIDE_CHECK(local.tide_tasks_function_Look.count <= 1);
    for (int k = 0; k < 8 && local.tide_tasks_function_Look.count; k++) frame();
    TIDE_CHECK(local.tide_tasks_function_Look.count == 0);
    const Chunk_local b = tide_get_Chunk_local(&local, second);
    TIDE_CHECK(b.asked == 1 && b.seen == 256 * (5 * 100 + 3) + 3 * 10000);
    finish_local();
}
