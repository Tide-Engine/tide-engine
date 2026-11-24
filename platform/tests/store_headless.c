// A run with no window of a game that keeps the server's store, through the real standard host and the platform's
// files (in a folder of the test's): the run has no clock, so it repeats, and its store is carried out as each tick
// ends, its answers reaching the next tick every run. On the provider's own thread they reached whichever tick was
// running as the disk finished.
#include <string.h>

#include "game.h"
#include "tide/run.h"
#include "tide_test.h"
#include "embedded.h"

static void frame(void)
{
    tide_platform_embed_frame(NULL, tide_f2(320, 180), 1);
    TIDE_CHECK(tide_run_frame(NULL, 1.0f / 60.0f) == TIDE_KEEP_RUNNING);
}

TIDE_TEST(store_headless_answers_reach_the_tick_after_they_were_asked)
{
    tide_run_game = &tide_host_game_api;
    tide_run_now = tide_run_frame_now = 0;
    tide_run_embedded = true;
    memset(&tide_run_devices, 0, sizeof tide_run_devices);
    tide_platform_embed_frame(NULL, tide_f2(320, 180), 1);
    tide_run_configure(&(tide_run_desc){0});
    tide_run_headless = true; // As --frames, --capture and --script make it
    tide_run_data_directory = "store_headless_files";
    tide_run_begin();
    // The game's files, with no thread of their own
    TIDE_REQUIRE(tide_run_store != NULL && tide_run_store->read && tide_run_store->write);
    TIDE_CHECK(!tide_run_store->post && !tide_run_store->settle);
    (void)tide_run_store->remove(tide_run_store->user, "Tide", true, true); // What an earlier run left
    for (int i = 0; i < 40; i++) frame();
    const tide_world *w = tide_session_server_world(tide_run_session);
    TIDE_REQUIRE(w != NULL && w->Time.tick > 20);
    // Asked in tick 3, carried out as it ended: the first answer in tick 4, 64 a tick after it
    TIDE_CHECK(w->Kept.asked == 3);
    TIDE_CHECK(w->Kept.answered == 4);
    TIDE_CHECK(w->Kept.found == 400);
    const tide_io *files = tide_run_store;
    tide_run_end();
    (void)files->remove(files->user, "Tide", true, true);
    tide_draw_free(&tide_run_draw);
    tide_draw_free(&tide_run_over);
}
