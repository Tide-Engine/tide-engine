#include "game.h"
#include "tide_test.h"

static tide_world world, kept;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

int32_t ScratchMark(void) { return (int32_t)tide_scratch_mark(); }

TIDE_TEST(foreach_borrows_read_only_elements_and_copies_where_the_body_changes_them)
{
    tide_world_init(&world, 1.0f / 60.0f);
    tide_local_init(&local);
    for (int tick = 0; tick < 3; tick++) {
        tide_world_tick(&world);
        TIDE_CHECK(world.Results.checks == 14);
        TIDE_CHECK(world.Results.passed == world.Results.checks);
        TIDE_CHECK(world.Results.firstFailure == 0);
        TIDE_CHECK(world.Results.borrowedSum == 8 * 128 + 36);
        TIDE_CHECK(world.Results.borrowedScratch == 0); // Read only: nothing copied
        TIDE_CHECK(world.Results.copiedScratch > 0);    // The body changes it: the element copied
        TIDE_CHECK(world.Results.mapBorrowedScratch == 0 && world.Results.mapCopiedScratch > 0);
        TIDE_CHECK(world.Results.arrayBorrowedScratch == 0 && world.Results.arrayCopiedScratch > 0);
        // A snapshot keeps its pieces whatever the next tick does to the list
        tide_world_copy(&kept, &world);
        const uint64_t before = tide_world_hash(&kept);
        tide_draw_reset(&draw);
        tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
        TIDE_CHECK(local.Seen.sum == 7 * 128); // The list as the last case left it: its first piece replaced by an empty one
        TIDE_CHECK(local.Seen.scratch == 0);
        TIDE_CHECK(tide_world_hash(&kept) == before);
        TIDE_CHECK(tide_scratch_mark() == 0);
    }
    tide_local_free(&local);
    tide_world_free(&world);
    tide_world_free(&kept);
    tide_draw_free(&draw);
}
