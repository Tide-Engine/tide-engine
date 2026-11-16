#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/pack.h"
#include "tide_test.h"

// The pack, as C (tide_add_game's --pack-c for HOST programs).
extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

static tide_world w;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

static tide_gpu_read_request *asked(void)
{
    tide_gpu_read_request *found = NULL;
    for (uint32_t i = 0; i < TIDE_GPU_READS; i++) {
        tide_gpu_read_request *r = &tide_gpu_read_requests()[i];
        if (r->state != TIDE_GPU_READ_ASKED) continue;
        if (found) return NULL; // The reads here are one after another
        found = r;
    }
    return found;
}

// What a renderer does with a 2x2 Color read: pixel (x, y) is (base + x + 10y, 1, 2, 3).
static void answer(tide_gpu_read_request *r, const float base)
{
    r->width = 2, r->height = 2;
    r->bytes = (4u * 4u + TIDE_GPU_LIST_HEADER_WORDS) * 4u;
    r->data = tide_alloc_zeroed(1, r->bytes);
    for (int32_t y = 0; y < 2; y++) for (int32_t x = 0; x < 2; x++) {
        const float pixel[4] = {base + (float)x + 10.0f * (float)y, 1, 2, 3};
        memcpy(r->data + TIDE_GPU_LIST_HEADER_WORDS + (y * 2 + x) * 4, pixel, sizeof pixel);
    }
    r->state = TIDE_GPU_READ_BEGUN;
    tide_gpu_read_finished(r, true);
}

static void frame(void)
{
    tide_draw_reset(&draw);
    tide_frame(&w, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
}

static bool color_is(const tide_color c, const float r, const float g, const float b, const float a)
{
    return c.r == r && c.g == g && c.b == b && c.a == a;
}

TIDE_TEST(task_grids_kept_across_waits_are_the_tasks_own)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&w, 1.0f / 60.0f);
    tide_world_tick(&w);
    tide_local_init(&local);
    frame();
    // Each read in ReadAll's order, and which target: a's are answered from 100, 300 and 500, b's from 200, 400, 600
    const struct { bool a; float base; int idle; } reads[] = {
        {true, 100, 2}, {false, 200, 2}, {true, 300, 2}, {false, 400, 3}, {true, 500, 2}, {false, 600, 2},
    };
    for (int i = 0; i < (int)(sizeof reads / sizeof reads[0]); i++) {
        for (int k = 0; k < 8 && !asked(); k++) frame(); // A wait of the task's own before it reads
        tide_gpu_read_request *r = asked();
        TIDE_REQUIRE(r != NULL);
        TIDE_CHECK(r->target.id == (reads[i].a ? local.Reads.a.id : local.Reads.b.id));
        // Frames go by while the read is out, each using the scratch area as other code would
        for (int k = 0; k < reads[i].idle; k++) frame();
        answer(r, reads[i].base);
        frame();
    }
    for (int k = 0; k < 12 && local.Reads.stage < 6; k++) frame();
    TIDE_CHECK(local.Reads.stage == 6 && local.Reads.failed == 0 && asked() == NULL && local.Reads.litter > 10);
    // a's (0, 0) and b's (1, 0): what each read gave, the first kept across the second's wait
    TIDE_CHECK(color_is(local.Reads.sum, 100 + 201, 2, 4, 6));
    const tide_color *picked = tide_grid_read(&local.heap, local.Reads.picked, 1, 1, 0, &tide_shape_Grid2_Color);
    TIDE_CHECK(picked && color_is(*picked, 311, 1, 2, 3));
    TIDE_CHECK(color_is(local.Reads.failable, 410, 1, 2, 3));
    TIDE_CHECK(color_is(local.Reads.bound, 511, 1, 2, 3) && color_is(local.Reads.nested, 611, 1, 2, 3));
    // Made, changed after a wait, changed again, cleared, and its own after each
    TIDE_CHECK(local.Reads.made == 1304);
    int32_t size[3];
    tide_grid_size(local.Reads.kept, size);
    TIDE_CHECK(size[0] == 4 && size[1] == 4);
    const int32_t *kept = tide_grid_read(&local.heap, local.Reads.kept, 3, 3, 0, &tide_shape_Grid2_int);
    const int32_t *cleared = tide_grid_read(&local.heap, local.Reads.kept, 1, 1, 0, &tide_shape_Grid2_int);
    TIDE_CHECK(kept && *kept == 9 && (!cleared || *cleared == 0)); // Cleared after the frame's copy was, and its own
    // The pack's grids, kept across waits, read as they are without one
    TIDE_CHECK(local.Reads.framesSeen == 3 && local.Reads.namedSeen == 3);
    TIDE_CHECK(memcmp(&local.Reads.frames, &local.Reads.framesWant, sizeof(tide_color)) == 0 && local.Reads.framesWant.a > 0);
    TIDE_CHECK(memcmp(&local.Reads.named, &local.Reads.namedWant, sizeof(tide_color)) == 0 && local.Reads.namedWant.a > 0);
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&w);
    tide_render_targets_free();
}

// Another pack of the game's assets, as tide run loads one when a file is saved: run and walk two grids, 5 and 6
// wide, and runByName and walkByName the same two. Its region is smaller than the game's own.
static uint8_t *other_pack(size_t *size)
{
    tide_pack_builder b = {0};
    static tide_color pixels[6 * 2];
    for (int i = 0; i < 12; i++) pixels[i] = (tide_color){0.5f, 0.25f, 0.125f, 1.0f};
    const uint8_t content5[32] = {5}, content6[32] = {6}, key_a[32] = {10}, key_b[32] = {11};
    const uint64_t five = tide_pack_grid(&b, content5, 5, 2, pixels, &tide_shape_Grid2_Color);
    const uint64_t six = tide_pack_grid(&b, content6, 6, 2, pixels, &tide_shape_Grid2_Color);
    const uint64_t list = tide_pack_list(&b, (const uint64_t[]){five, six}, 2);
    tide_pack_name(&b, "run", TIDE_PACK_LIST, list, 2, TIDE_PACK_GRID);
    tide_pack_name(&b, "walk", TIDE_PACK_LIST, list, 2, TIDE_PACK_GRID);
    uint32_t chars;
    const uint64_t a = tide_pack_string(&b, key_a, "a", 1, &chars), bb = tide_pack_string(&b, key_b, "b", 1, &chars);
    uint64_t index;
    const uint64_t map = tide_pack_map(&b, (const uint64_t[]){a, bb}, (const uint64_t[]){five, six}, 2, &index);
    tide_pack_name(&b, "runByName", TIDE_PACK_MAP, map, index, 2);
    tide_pack_name(&b, "walkByName", TIDE_PACK_MAP, map, index, 2);
    return tide_pack_end(&b, size);
}

// A pack loaded again while a task waits inside a loop over the pack's grids: a loop over an asset goes on through
// the asset as it is now, and a loop over a list or a map it worked out (a choice of two assets) through the frame's
// own copy of it, whose grids are all its own. Kept raw, the frame named the old pack's grids by their places in it,
// which in the new one are other bytes or none.
TIDE_TEST(task_grids_pack_loaded_again_as_a_task_waits)
{
    size_t other_size;
    uint8_t *other = other_pack(&other_size);
    TIDE_REQUIRE(other != NULL);
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&w, 1.0f / 60.0f);
    tide_world_tick(&w);
    tide_local_init(&local);
    local.Reads.started = true; // ReadAll's reads aren't these
    frame();
    // Choose goes through walk, the game's three 2 by 2 grids, and the other pack is loaded as it waits at the first
    local.Reads.chooseNow = true;
    frame();
    TIDE_CHECK(local.Reads.chosenWant == 6 && local.Reads.chosen == 0);
    TIDE_REQUIRE(tide_assets_load(other, other_size));
    for (int k = 0; k < 12 && local.Reads.chosenNamedWant == 0; k++) frame();
    TIDE_CHECK(local.Reads.walkStage == 1 && local.Reads.chosen == 3 && local.Reads.chosenWidths == 6);
    // ...then through walkByName, the other pack's two, and the game's own is loaded again as it waits at the first
    TIDE_CHECK(local.Reads.chosenNamedWant == 11 && local.Reads.chosenNamed == 0);
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    for (int k = 0; k < 12 && local.Reads.walkStage < 2; k++) frame();
    TIDE_CHECK(local.Reads.walkStage == 2 && local.Reads.chosenNamed == 2 && local.Reads.chosenNamedWidths == 11);
    // Walk goes through run and runByName as they are: the game's first grid, then the other pack's second, and the
    // other pack's two
    local.Reads.walkNow = true;
    frame();
    TIDE_CHECK(local.Reads.firstWidth == 2 && local.Reads.walked == 0);
    TIDE_REQUIRE(tide_assets_load(other, other_size));
    for (int k = 0; k < 12 && local.Reads.walkStage < 3; k++) frame();
    TIDE_CHECK(local.Reads.walkStage == 3 && local.Reads.walked == 2 && local.Reads.walkedWidths == 2 + 6);
    TIDE_CHECK(local.Reads.walkedNamed == 2 && local.Reads.walkedNamedWidths == 11);
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    free(other);
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&w);
    tide_render_targets_free();
}

// The local world's heaps' bytes, as far as they were ever used.
static uint64_t local_heap_used(void)
{
    uint64_t used = 0;
    for (uint32_t i = 0; local.heap.all && i < tide_heap_count; i++) used += local.heap.all[i].most;
    return used;
}

// A 256 by 256 Color read answered with no zero pixel, so its grid has every chunk.
static void answer_big(tide_gpu_read_request *r, const float base)
{
    r->width = 256, r->height = 256;
    r->bytes = (256u * 256u * 4u + TIDE_GPU_LIST_HEADER_WORDS) * 4u;
    r->data = tide_alloc_zeroed(1, r->bytes);
    for (int32_t y = 0; y < 256; y++) for (int32_t x = 0; x < 256; x++) {
        const float pixel[4] = {base + (float)x + 1000.0f * (float)y, 1, 2, 3};
        memcpy(r->data + TIDE_GPU_LIST_HEADER_WORDS + (y * 256 + x) * 4, pixel, sizeof pixel);
    }
    r->state = TIDE_GPU_READ_BEGUN;
    tide_gpu_read_finished(r, true);
}

// A read's grid that a task keeps across its waits is one copy in its frame, for `var r = await ...` and for an `is`
// name alike: the value the await gave was a second, kept as long as the block it was in.
TIDE_TEST(task_grids_a_read_kept_across_waits_is_one_copy)
{
    tide_world_init(&w, 1.0f / 60.0f);
    tide_world_tick(&w);
    tide_local_init(&local);
    local.Reads.started = true;
    frame();
    local.Reads.big = tide_render_target_make_scaled_as(256, 256, 1, TIDE_TARGET_RGBA8);
    local.Reads.keepNow = true;
    frame();
    const uint64_t grid = 256u * 256u * sizeof(tide_color); // Its cells, beside which its chunks' headers are little
    for (int i = 0; i < 2; i++) {
        tide_gpu_read_request *r = NULL;
        for (int k = 0; k < 8 && !(r = asked()); k++) frame();
        TIDE_REQUIRE(r != NULL);
        const uint64_t before = local_heap_used();
        answer_big(r, i ? 500.0f : 100.0f);
        frame(); // The task has the grid, and waits two frames with it
        const uint64_t kept = local_heap_used() - before;
        TIDE_CHECK(kept >= grid && kept < grid + grid / 2);
        for (int k = 0; k < 8 && local.Reads.keepStage < i + 1; k++) frame();
        TIDE_CHECK(local.Reads.keepStage == i + 1);
    }
    TIDE_CHECK(color_is(local.Reads.keptCell, 100 + 3 + 3000, 1, 2, 3));
    TIDE_CHECK(color_is(local.Reads.boundCell, 500 + 4 + 4000, 1, 2, 3));
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&w);
    tide_render_targets_free();
}

TIDE_TEST(task_grids_of_the_match_are_in_its_snapshots_and_hash)
{
    static tide_world a, b, c;
    tide_world_init(&a, 1.0f / 60.0f);
    tide_world_init(&c, 1.0f / 60.0f);
    tide_world_tick(&a); // Hold starts, and waits with its grid in the frame
    tide_world_tick(&c);
    tide_world_tick(&a);
    tide_world_tick(&c);
    TIDE_CHECK(a.Field.progress == 1 && a.Field.sum == 0);
    TIDE_CHECK(tide_world_hash(&a) == tide_world_hash(&c));
    // A snapshot as it waits: the copy goes on as the original does, the grid with it
    const uint32_t bytes = tide_world_pack(&a, NULL, 0);
    uint8_t *packed = malloc(bytes);
    TIDE_REQUIRE(packed != NULL);
    TIDE_REQUIRE(tide_world_pack(&a, packed, bytes) == bytes);
    tide_world_init(&b, 1.0f / 60.0f);
    TIDE_REQUIRE(tide_world_unpack(&b, packed, bytes));
    free(packed);
    TIDE_CHECK(tide_world_hash(&a) == tide_world_hash(&b));
    for (int i = 0; i < 3; i++) {
        tide_world_tick(&a);
        tide_world_tick(&b);
        tide_world_tick(&c);
        TIDE_CHECK(tide_world_hash(&a) == tide_world_hash(&b) && tide_world_hash(&a) == tide_world_hash(&c));
    }
    TIDE_CHECK(a.Field.sum == 15 && b.Field.sum == 15 && c.Field.sum == 15 && a.Field.progress == 2);
    tide_world_free(&a);
    tide_world_free(&b);
    tide_world_free(&c);
}
