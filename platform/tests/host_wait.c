// Synthetic presentation waits exercise the real standard host/session path.
// The embedded platform supplies input and a screen, with no GPU or window.
#include "game.h"
#include "tide/time.h"

// Only this host copy uses the fixture clock. Session/game code is unchanged;
// accounting must not depend on a browser distinguishing two very short reads.
static uint64_t host_work_time(void);
#define tide_time_now_ns host_work_time
#include "tide/run.h"
#undef tide_time_now_ns
#include "tide_test.h"
#include "embedded.h"

static bool use_work_clock;
static uint64_t work_clock;
static uint32_t work_clock_reads;

static uint64_t host_work_time(void)
{
    if (!use_work_clock) return tide_time_now_ns();
    work_clock_reads++;
    const uint64_t now = work_clock;
    work_clock += UINT64_C(1250000);
    return now;
}

static tide_host_game host;
static uint32_t frame_times, event_deliveries, age_calls;
static double aged;
static float last_frame_seconds;

static void frame_time(void *local, const float seconds)
{
    frame_times++;
    last_frame_seconds = seconds;
    tide_host_game_api.frame_time(local, seconds);
}

static void events(void *local, tide_event_log *heard)
{
    event_deliveries++;
    tide_host_game_api.events(local, heard);
}

static void age_views(void *local, const float seconds)
{
    age_calls++;
    aged += seconds;
    tide_host_game_api.age_views(local, seconds);
}

static bool near(const double a, const double b)
{
    const double difference = a - b;
    return difference > -0.00002 && difference < 0.00002;
}

static tide_local *local_state(void) { return tide_run_local; }
static const tide_world *world(void) { return tide_session_view(tide_run_session).current; }

static void frame(const float seconds, const bool held)
{
    tide_embed_input input = {0};
    if (held) input.keys[36u / 8u] |= (uint8_t)(1u << (36u % 8u)); // Space, after the letters and digits.
    tide_platform_embed_frame(&input, tide_f2(320, 180), 1);
    TIDE_CHECK(tide_run_frame(NULL, seconds) == TIDE_KEEP_RUNNING);
}

static void start(void)
{
    host = tide_host_game_api;
    host.frame_time = frame_time;
    host.events = events;
    host.age_views = age_views;
    tide_run_game = &host;
    tide_run_now = tide_run_frame_now = 0;
    tide_run_embedded = true;
    memset(&tide_run_devices, 0, sizeof tide_run_devices);
    tide_platform_embed_frame(NULL, tide_f2(320, 180), 1);
    tide_run_configure(&(tide_run_desc){0});
    tide_run_headless = true; // Fixed seed and no measured rollback budget.
    tide_run_begin();
    frame_times = event_deliveries = age_calls = 0;
    aged = 0;
    for (int i = 0; i < 8; i++) frame(1.0f / 64.0f, false);
    TIDE_CHECK(tide_session_status_of(tide_run_session).client.state == TIDE_SESSION_CONNECTED);
    TIDE_CHECK(world() != NULL);
}

static void finish(void)
{
    tide_run_end();
    tide_draw_free(&tide_run_draw);
    tide_draw_free(&tide_run_over);
}

TIDE_TEST(host_wait_ticks_keep_events_and_views_at_frame_boundaries)
{
    start();
    frame(1.0f / 64.0f, true);
    const double began = tide_run_now;
    const State before = world()->State;
    const tide_view_worlds presented = tide_session_view(tide_run_session);
    const Observed seen = local_state()->Observed;
    const uint32_t old_frames = frame_times, old_events = event_deliveries;
    // A release is ready for the next poll; the wait deliberately retains the last poll's input.
    tide_platform_embed_frame(NULL, tide_f2(320, 180), 1);
    for (int i = 1; i <= 8; i++) {
        const double until = tide_run_wait_update(NULL, (double)i / 64.0);
        TIDE_CHECK(until >= 0 && until <= 1.0 / 64.0);
        TIDE_CHECK(tide_run_now == began + (double)i / 64.0);
        TIDE_CHECK(world()->State.shown.valid);
        TIDE_CHECK(world()->State.shown.tick == (int32_t)presented.verified_tick);
        TIDE_CHECK(world()->State.shown.alpha == presented.verified_alpha);
    }
    const State after = world()->State;
    TIDE_CHECK(after.ticks >= before.ticks + 7);
    TIDE_CHECK(after.held - before.held == after.ticks - before.ticks);
    TIDE_CHECK(after.pressed == before.pressed); // The last input's edge was consumed only once.
    TIDE_CHECK(local_state()->Observed.views == seen.views);
    TIDE_CHECK(local_state()->Observed.heard == seen.heard);
    TIDE_CHECK(local_state()->Observed.verified == seen.verified);
    TIDE_CHECK(frame_times == old_frames && event_deliveries == old_events);
    TIDE_CHECK(tide_run_frame_now == began);

    frame(0.125f, false);
    TIDE_CHECK(tide_run_now == began + 0.125);
    TIDE_CHECK(tide_run_frame_now == tide_run_now);
    TIDE_CHECK(last_frame_seconds == 0.125f && local_state()->Observed.seconds == 0.125f);
    TIDE_CHECK(local_state()->Observed.views == seen.views + 1);
    TIDE_CHECK(local_state()->Observed.heard - seen.heard == after.ticks - before.ticks);
    TIDE_CHECK(!local_state()->Observed.outOfOrder);
    const Observed delivered = local_state()->Observed;
    frame(0, false);
    TIDE_CHECK(local_state()->Observed.heard == delivered.heard);
    TIDE_CHECK(local_state()->Observed.verified == delivered.verified);
    finish();
}

TIDE_TEST(host_wait_clock_is_monotonic_and_never_counts_elapsed_time_twice)
{
    start();
    const double began = tide_run_now, old_aged = aged;
    const uint32_t old_age_calls = age_calls;
    const uint64_t old_work = tide_run_stats_work;
    const double old_match_ms = tide_run_profiler.part_ms[TIDE_FRAME_MATCH];
    const uint32_t old_worked = tide_run_stats_worked, old_stats_frames = tide_run_stats_frames;
    const uint32_t old_profile_frames = tide_run_profiler.frames;
    work_clock = UINT64_C(1000000000);
    work_clock_reads = 0;
    use_work_clock = true;
    tide_run_timing = tide_run_profiling = true;
    tide_run_wait_update(NULL, 0.125);
    TIDE_CHECK(work_clock_reads == 2);
    TIDE_CHECK(tide_run_stats_work - old_work == UINT64_C(1250000));
    TIDE_CHECK(near(tide_run_profiler.part_ms[TIDE_FRAME_MATCH] - old_match_ms, 1.25));
    TIDE_CHECK(tide_run_stats_worked == old_worked && tide_run_stats_frames == old_stats_frames);
    TIDE_CHECK(tide_run_profiler.frames == old_profile_frames);
    TIDE_CHECK(tide_run_now == began + 0.125);
    TIDE_CHECK(near(aged - old_aged, 0.125));
    const uint64_t nan_bits = UINT64_C(0x7ff8000000000000);
    double not_a_number;
    memcpy(&not_a_number, &nan_bits, sizeof not_a_number);
    tide_run_wait_update(NULL, 0.125);
    tide_run_wait_update(NULL, 0.0625);
    tide_run_wait_update(NULL, -1);
    tide_run_wait_update(NULL, not_a_number);
    TIDE_CHECK(work_clock_reads == 2);
    TIDE_CHECK(tide_run_stats_work - old_work == UINT64_C(1250000));
    TIDE_CHECK(near(tide_run_profiler.part_ms[TIDE_FRAME_MATCH] - old_match_ms, 1.25));
    use_work_clock = false;
    tide_run_timing = tide_run_profiling = false;
    TIDE_CHECK(tide_run_now == began + 0.125);
    TIDE_CHECK(age_calls == old_age_calls + 1);
    frame(0.0625f, false); // Even a frame time rounded below the serviced clock cannot rewind it.
    TIDE_CHECK(tide_run_frame_now == began + 0.0625);
    TIDE_CHECK(tide_run_now == began + 0.125);
    frame(0.0625f, false);
    TIDE_CHECK(tide_run_frame_now == tide_run_now);
    TIDE_CHECK(near(aged - old_aged, 0.125));
    frame(0.03125f, false);
    TIDE_CHECK(tide_run_now == began + 0.15625);
    TIDE_CHECK(near(aged - old_aged, 0.15625));
    finish();
}

static void correct(const float error)
{
    tide_world before = {0}, after = {0};
    tide_world_copy(&before, world());
    tide_world_copy(&after, world());
    before.State.position = error;
    after.State.position = 0;
    const tide_view_correction correction = {.before = &before, .after = &after, .alpha = 1};
    tide_run_correct_views(NULL, &correction);
    tide_world_free(&before);
    tide_world_free(&after);
}

TIDE_TEST(host_wait_ages_fresh_corrections_only_since_their_arrival)
{
    start();
    const double old_aged = aged;
    const uint32_t old_frames = frame_times;
    correct(8);
    tide_run_wait_update(NULL, 0.075);
    correct(4); // Arrived halfway through the 150 ms presentation wait.
    tide_run_wait_update(NULL, 0.1125);
    TIDE_CHECK(frame_times == old_frames);
    frame(0.15f, false);
    // The old error has aged two half-lives, the new error only one: 2 + 2.
    TIDE_CHECK(near(local_state()->Observed.position, 4));
    TIDE_CHECK(near(aged - old_aged, 0.15));
    TIDE_CHECK(frame_times == old_frames + 1);
    TIDE_CHECK(last_frame_seconds == 0.15f && local_state()->Observed.seconds == 0.15f);
    finish();
}
