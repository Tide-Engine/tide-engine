#include "game.h"
#include "tide_test.h"

// Every length here is a sum of powers of two, so what the views add up is exact.

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

static const tide_start arena = {.scene = 0};

static void draw_frame(const tide_world *w)
{
    tide_draw_reset(&draw);
    tide_frame(w, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
}

// A frame as a host runs one: its length first, then the views.
static void frame(const tide_world *w, const float seconds)
{
    tide_local_frame_time(&local, seconds);
    draw_frame(w);
}

static Spark spark(const int32_t i)
{
    return *(const Spark *)tide_list_read(&local.heap, local.Clock.sparks, i, sizeof(Spark));
}

TIDE_TEST(frame_time_is_zero_until_a_host_gives_one)
{
    tide_local_init(&local);
    TIDE_CHECK(local.Clock.before == 1.0f); // Main's handler ran before any frame, and read 0
    draw_frame(NULL);
    TIDE_CHECK(local.Clock.frames == 1);
    TIDE_CHECK(local.Clock.seen == 0.0f);
    TIDE_CHECK(local.Clock.total == 0.0f);
    tide_local_free(&local);
}

TIDE_TEST(frame_time_is_the_hosts_with_no_match)
{
    tide_local_init(&local);
    frame(NULL, 0.25f);
    TIDE_CHECK(local.Clock.seen == 0.25f);
    TIDE_CHECK(local.Clock.helper == 0.5f); // A function, through another
    TIDE_CHECK(local.Clock.nearest == 2);   // A comparison: 8 a second gets 2 far in a quarter of one
    TIDE_CHECK(local.Field.corner == 0.25f); // A parallel loop's steps
    frame(NULL, 0.125f);
    TIDE_CHECK(local.Clock.seen == 0.125f);
    TIDE_CHECK(local.Clock.helper == 0.25f);
    TIDE_CHECK(local.Clock.nearest == 1);
    TIDE_CHECK(local.Clock.total == 0.375f);
    TIDE_CHECK(local.Field.corner == 0.375f);
    TIDE_CHECK(local.Clock.inMatch == 0.0f); // No match: the view of its balls never ran
    tide_local_free(&local);
}

TIDE_TEST(frame_time_is_the_same_in_a_match)
{
    tide_local_init(&local);
    frame(NULL, 0.5f); // The menu, before the match
    local.Clock.play = true;
    frame(NULL, 0.5f);
    tide_session_request request;
    tide_start start;
    TIDE_REQUIRE(tide_local_take_request(&local, &request, &start));
    TIDE_CHECK(request.kind == TIDE_REQUEST_START && start.scene == arena.scene);

    tide_world_start(&world, 1.0f / 60.0f, &start);
    for (int tick = 0; tick < 3; tick++) tide_world_tick(&world);
    frame(&world, 0.25f);
    TIDE_CHECK(local.Clock.seen == 0.25f);
    TIDE_CHECK(local.Clock.inMatch == 0.25f); // The ball's view, once
    TIDE_CHECK(world.Time.dt == 1.0f / 60.0f); // A tick's length is the match's own
    for (int tick = 0; tick < 40; tick++) tide_world_tick(&world);
    frame(&world, 0.0625f); // However many ticks went by
    TIDE_CHECK(local.Clock.seen == 0.0625f);
    TIDE_CHECK(local.Clock.inMatch == 0.3125f);
    TIDE_CHECK(local.Clock.total == 1.3125f);

    // The match is gone: the frames go on
    frame(NULL, 0.5f);
    TIDE_CHECK(local.Clock.seen == 0.5f);
    TIDE_CHECK(local.Clock.inMatch == 0.3125f);
    tide_local_free(&local);
    tide_world_free(&world);
}

TIDE_TEST(frame_time_moves_sparks_by_time_not_frames)
{
    // The same three quarters of a second, in 3 frames and in 12
    for (int run = 0; run < 2; run++) {
        const int count = run == 0 ? 3 : 12;
        const float seconds = 0.75f / (float)count;
        tide_local_init(&local);
        local.Clock.burst = true;
        for (int i = 0; i < count; i++) frame(NULL, seconds);
        // The one that lasts half a second is gone, and the other is where 8 across and 4 down a second get it
        TIDE_REQUIRE(tide_list_read_count(&local.heap, local.Clock.sparks) == 1);
        const Spark left = spark(0);
        TIDE_CHECK(left.age == 0.75f);
        TIDE_CHECK(left.position.x == 6.0f && left.position.y == -3.0f);
        for (int i = 0; i < count / 3; i++) frame(NULL, seconds);
        TIDE_CHECK(tide_list_read_count(&local.heap, local.Clock.sparks) == 0); // A second old
        tide_local_free(&local);
    }
}

TIDE_TEST(frame_time_is_read_by_handlers_and_tasks)
{
    tide_local_init(&local);
    local.Clock.tween = true;
    frame(NULL, 0.25f); // The view starts the task, which reads the frame until it first waits
    TIDE_CHECK(local.Clock.tweened == 0.25f);
    TIDE_CHECK(local.Clock.handled == 0.25f); // The handler ran at the frame's end
    frame(NULL, 0.5f);
    TIDE_CHECK(local.Clock.tweened == 0.75f);
    TIDE_CHECK(local.Clock.handled == 0.25f);
    frame(NULL, 0.125f);
    TIDE_CHECK(local.Clock.tweened == 0.875f);
    frame(NULL, 1.0f); // The task is done
    TIDE_CHECK(local.Clock.tweened == 0.875f);
    TIDE_CHECK(local.tide_tasks_function_Tween.count == 0);
    tide_local_free(&local);
}

TIDE_TEST(frame_time_is_never_a_length_that_isnt_one)
{
    tide_local_init(&local);
    frame(NULL, 0.25f);
    frame(NULL, -1.0f);
    TIDE_CHECK(local.Clock.seen == 0.0f);
    volatile float zero = 0.0f;
    frame(NULL, zero / zero); // NaN
    TIDE_CHECK(local.Clock.seen == 0.0f);
    TIDE_CHECK(local.Clock.total == 0.25f);
    tide_local_free(&local);
}
