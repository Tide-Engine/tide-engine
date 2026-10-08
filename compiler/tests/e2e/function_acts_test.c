#include <string.h>

#include "game.h"
#include "tide/jobs.h"
#include "tide_test.h"

static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_devices devices;

// A pool of one thread that says it has four: a system that splits its
// entities runs them in pieces, each recording into a queue of its own.
static void work(void *self, void (*run)(void *, uint32_t), void *context)
{
    (void)self;
    run(context, 0u);
}

static int32_t heard(const tide_world *w, const int32_t at)
{
    return *(const int32_t *)tide_list_read(&w->heap, w->Tally.order, (uint32_t)at, sizeof(int32_t));
}

static void check_heard(const tide_world *w, const int32_t *want, const int32_t count)
{
    TIDE_REQUIRE((int32_t)tide_list_count(w->Tally.order) == count);
    for (int32_t i = 0; i < count; i++) TIDE_CHECK(heard(w, i) == want[i]);
}

static void frame(const tide_world *w)
{
    tide_draw_reset(&draw);
    tide_gui_begin(&gui, &devices, tide_f2(640, 480), NULL);
    tide_frame(w, NULL, 1, w, &local, &draw, &gui);
    tide_gui_end(&gui, &draw);
}

TIDE_TEST(function_acts_record_in_the_order_written)
{
    tide_world w = {0};
    tide_world_init(&w, 1.0f);
    tide_world_tick(&w);
    // Order's own sends and its functions', a sum's calls left to right, and a block's between its function's
    static const int32_t first[] = {10, 11, 12, 1, 2, 3, 7, 20, 30, 21, 30};
    check_heard(&w, first, 11);
    TIDE_CHECK(strcmp(tide_str_c(tide_text_read(&w.heap, w.Tally.last)), "said") == 0);
    tide_world_free(&w);
}

TIDE_TEST(function_acts_from_methods_tasks_and_handlers)
{
    tide_world w = {0};
    tide_world_init(&w, 1.0f);
    tide_world_tick(&w);
    TIDE_CHECK(tide_world_entity_count(&w) == 305); // Main, the arena and its two crates, the enemy, 300 scouts
    tide_world_tick(&w);
    TIDE_CHECK(w.Tally.hits == 2);                  // The enemy's own method sent it a Hit, through `this`
    TIDE_CHECK(heard(&w, 11) == 7);                 // ...and its bell's method rang
    tide_world_tick(&w);
    TIDE_CHECK(w.Tally.hits == 5);                  // Volley's two, through Poke
    static const int32_t third[] = {102, 1000, 1050, 1100, 1150, 1200, 1250, 900}; // Volley, the scouts in order, the task
    TIDE_REQUIRE(tide_list_count(w.Tally.order) == 20);
    for (int32_t i = 0; i < 8; i++) TIDE_CHECK(heard(&w, 12 + i) == third[i]);
    tide_world_tick(&w);
    TIDE_CHECK(w.Tally.marked == 4);                // Tag's Add, there for one tick
    tide_world_tick(&w);
    TIDE_CHECK(w.Tally.marked == 4);                // ...until Untag's Remove
    TIDE_CHECK(tide_world_entity_count(&w) == 404); // End's Destroy
    TIDE_CHECK(!tide_entity_alive(&w.entities, w.Tally.first));
    tide_world_free(&w);
}

TIDE_TEST(function_acts_spawn_into_their_callers_scene)
{
    tide_world w = {0};
    tide_world_init(&w, 1.0f);
    for (int tick = 0; tick < 6; tick++) tide_world_tick(&w);
    TIDE_CHECK(tide_world_entity_count(&w) == 404);
    tide_world_tick(&w); // The arena unloads, and the two crates its handler's function spawned go with it
    TIDE_CHECK(tide_world_entity_count(&w) == 401);
    TIDE_CHECK(w.Tally.dents == 100); // The scouts' crates, in no scene, stay
    tide_world_free(&w);
}

TIDE_TEST(function_acts_are_the_same_on_threads)
{
    tide_world one = {0}, many = {0}, again = {0};
    tide_world_init(&one, 1.0f);
    tide_world_copy(&many, &one);
    const tide_jobs jobs = {NULL, 4u, work, NULL, NULL, NULL};
    for (int tick = 1; tick <= 8; tick++) {
        tide_world_copy(&again, &one);
        tide_world_tick(&one);
        tide_world_tick_on(&many, &jobs);
        tide_world_tick(&again); // A tick run again records the same
        TIDE_CHECK(tide_world_hash(&many) == tide_world_hash(&one));
        TIDE_CHECK(tide_world_hash(&again) == tide_world_hash(&one));
        if (tick == 2) TIDE_CHECK(many.Tally.dents == 100); // A Hit sent to a crate spawned on a thread reached it
        if (tick == 3) TIDE_CHECK(many.Tally.matched == 100); // ...and each scout kept its crate's real handle
    }
    TIDE_CHECK(tide_list_count(many.Tally.order) == 20);
    tide_world_free(&again);
    tide_world_free(&many);
    tide_world_free(&one);
}

TIDE_TEST(function_acts_on_this_machine_from_a_view)
{
    tide_world w = {0};
    tide_world_init(&w, 1.0f);
    tide_local_init(&local);
    tide_local_set_session(&local, TIDE_SESSION_CONNECTED, tide_player_from_index(0), 0, false, false, "");
    tide_world_tick(&w);
    frame(&w);
    TIDE_CHECK(local.Seen.blinks == 4);                 // Flash's local event, handled at the frame's end
    TIDE_CHECK(tide_local_entity_count(&local) == 1);   // Two puffs spawned, one destroyed
    uint32_t size = 0;
    const uint8_t *command = tide_local_command(&local, 0, &size); // Ask's command, waiting to go to the match
    TIDE_REQUIRE(command && size > 0 && size <= TIDE_COMMAND_BYTES);
    uint8_t bytes[TIDE_COMMAND_BYTES];
    memcpy(bytes, command, size);
    TIDE_REQUIRE(tide_game_api.command(&w, tide_player_from_index(0), 0, 1, bytes, size));
    tide_world_tick(&w);
    TIDE_CHECK(w.Tally.cheers == 9);
    TIDE_CHECK(heard(&w, (int32_t)tide_list_count(w.Tally.order) - 1) == -9); // Its handler's function sent, last in the tick
    tide_local_free(&local);
    tide_world_free(&w);
}
