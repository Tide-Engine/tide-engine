#include "game.h"
#include "tide_test.h"

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

static void start(void)
{
    tide_world_init(&world, 1.0f);
    tide_local_init(&local);
}

static void frame(const tide_world *w)
{
    tide_draw_reset(&draw);
    tide_frame(w, NULL, 1.0f, NULL, &local, &draw, &gui);
}

TIDE_TEST(world_views_read_the_match)
{
    start();
    frame(&world);
    TIDE_CHECK(local.Seen.leaderHealth == 42);
    TIDE_CHECK(tide_entity_equal(local.Seen.picked, (tide_entity){1, 1}));
    TIDE_CHECK(local.Seen.pickedFrames == 1);
    frame(&world);
    TIDE_CHECK(local.Seen.pickedFrames == 2);
    tide_world_free(&world);
    tide_local_free(&local);
}

TIDE_TEST(world_local_code_changes_local_entities)
{
    start();
    frame(&world); // Make's lamp and toggle are there from the next frame
    TIDE_CHECK(tide_local_entity_count(&local) == 2);
    TIDE_CHECK(tide_get_Lamp(&local, local.Seen.lamp).glow == 5);
    frame(&world);
    TIDE_CHECK(local.Seen.glow == 6 && tide_get_Lamp(&local, local.Seen.lamp).glow == 6);
    frame(&world);
    frame(&world); // At 8, the toggle is sent Dim, whose handler takes 6 off at the end of the frame
    TIDE_CHECK(local.Seen.glow == 8 && tide_get_Lamp(&local, local.Seen.lamp).glow == 2);
    frame(&world);
    TIDE_CHECK(local.Seen.glow == 3);
    tide_world_free(&world);
    tide_local_free(&local);
}

TIDE_TEST(world_views_of_the_match_wait_for_one)
{
    start();
    frame(NULL);
    frame(NULL);
    // Watch and Picked read the match, Picked only through its World: neither ran
    TIDE_CHECK(local.Seen.pickedFrames == 0 && local.Seen.leaderHealth == 0);
    // Flip only reads local entities, so it did
    TIDE_CHECK(local.Seen.glow == 6);
    tide_world_free(&world);
    tide_local_free(&local);
}
