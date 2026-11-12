// Ticks beside the frame (the ticksAlongsideViews setting, tide_session_desc.alongside) through the standard
// host and the platform's pool: a busy match (jobs.tide) whose tick runs on the pool's leading thread while the
// frame's local handlers and views read the worlds before it. Everything a frame shows and hears has to be what
// the frame before showed and heard with the ticks in the update, bit for bit, and the match the same match.
// The embedded platform supplies input and a screen, with no GPU or window.
#include "game.h"
#include "tide/run.h"
#include "tide_test.h"
#include "embedded.h"

typedef struct shown_frame {
    bool in_match;
    uint64_t world; // The hash of the world the frame's views were given
    float alpha;
    Drawn drawn;
} shown_frame;

#define FRAMES 180

static void start(const bool alongside)
{
    tide_run_game = &tide_host_game_api;
    tide_run_now = tide_run_frame_now = 0;
    tide_run_embedded = true;
    memset(&tide_run_devices, 0, sizeof tide_run_devices);
    tide_platform_embed_frame(NULL, tide_f2(320, 180), 1);
    tide_run_configure(&(tide_run_desc){0});
    tide_run_headless = true; // Fixed seed and no measured rollback budget.
    tide_run_alongside = alongside;
    tide_run_begin();
}

static void finish(void)
{
    tide_run_end();
    tide_draw_free(&tide_run_draw);
    tide_draw_free(&tide_run_over);
}

static shown_frame frame(const float seconds)
{
    tide_platform_embed_frame(NULL, tide_f2(320, 180), 1);
    TIDE_CHECK(tide_run_frame(NULL, seconds) == TIDE_KEEP_RUNNING);
    // As the frame's views were given it: asked again in the frame, it's the same moment
    const tide_view_worlds view = tide_session_view(tide_run_session);
    shown_frame f = {.in_match = view.current != NULL, .alpha = view.alpha};
    if (view.current) f.world = tide_game_api.hash_world(view.current);
    f.drawn = ((const tide_local *)tide_run_local)->Drawn;
    return f;
}

// The match after `frames` frames of `seconds` each, and what each showed.
static uint64_t run(const bool alongside, const float seconds, const uint32_t frames, shown_frame *out, uint32_t *ticks,
                    uint32_t *beside)
{
    start(alongside);
    for (uint32_t i = 0; i < frames; i++) out[i] = frame(seconds);
    tide_session_alongside_ns(tide_run_session, beside);
    const tide_world *world = tide_session_server_world(tide_run_session); // After the tick that's running
    TIDE_CHECK(world != NULL);
    *ticks = world ? tide_server_tick(tide_session_server(tide_run_session)) : 0u;
    const uint64_t hash = world ? tide_game_api.hash_world(world) : 0u;
    finish();
    return hash;
}

// What the views drew, and with heard, what the local handlers have heard by then too.
static bool same_shown(const Drawn *a, const Drawn *b, const bool heard)
{
    return a->bodies == b->bodies && memcmp(&a->sum, &b->sum, sizeof a->sum) == 0 && a->letters == b->letters
        && a->sand == b->sand && memcmp(&a->swarm, &b->swarm, sizeof a->swarm) == 0 && a->hits == b->hits
        && (!heard || (a->heard == b->heard && a->damage == b->damage && a->chipped == b->chipped && a->confirmed == b->confirmed));
}

// each: no frame has more than one tick, so every tick runs beside a frame, and a frame's handlers hear what
// the frame before's did. A frame of several ticks runs all but its last in its update, whose handlers hear them
// then: a frame's worth of ticks before the views show them.
static void compare(const float seconds, const bool each)
{
    static shown_frame in_update[FRAMES + 1], beside[FRAMES + 1];
    uint32_t ticks_in_update, ticks_beside, none, flown;
    const uint64_t match_in_update = run(false, seconds, FRAMES, in_update, &ticks_in_update, &none);
    const uint64_t match_beside = run(true, seconds, FRAMES, beside, &ticks_beside, &flown);
    if (!tide_platform_jobs() || !tide_platform_jobs()->lead) {
        printf("    no leading thread on this machine: ticks ran in the update\n");
        TIDE_CHECK(match_in_update == match_beside);
        return;
    }
    TIDE_CHECK(none == 0 && flown > (each ? ticks_beside - 20u : FRAMES - 20u)); // Every one that could, once the player is level
    TIDE_CHECK(ticks_in_update == ticks_beside && ticks_beside > 20u);
    TIDE_CHECK(match_in_update == match_beside);
    uint32_t compared = 0, moved = 0, heard = 0;
    for (uint32_t f = 1; f < FRAMES; f++) {
        const shown_frame *was = &in_update[f - 1u], *is = &beside[f];
        if (!was->in_match) continue;
        TIDE_CHECK(is->in_match);
        const bool same = is->world == was->world && memcmp(&is->alpha, &was->alpha, sizeof is->alpha) == 0
            && same_shown(&is->drawn, &was->drawn, each);
        if (!same) {
            printf("    frame %u: world %016llx/%016llx alpha %.9g/%.9g bodies %d/%d sum %.9g/%.9g letters %d/%d "
                   "sand %d/%d heard %d/%d chipped %d/%d confirmed %d/%d\n", (unsigned)f,
                   (unsigned long long)is->world, (unsigned long long)was->world, (double)is->alpha, (double)was->alpha,
                   is->drawn.bodies, was->drawn.bodies, (double)is->drawn.sum, (double)was->drawn.sum,
                   is->drawn.letters, was->drawn.letters, is->drawn.sand, was->drawn.sand, is->drawn.heard,
                   was->drawn.heard, is->drawn.chipped, was->drawn.chipped, is->drawn.confirmed, was->drawn.confirmed);
            TIDE_CHECK(same);
            break;
        }
        compared++;
        if (f > 1 && is->world != beside[f - 1u].world) moved++;
        heard = (uint32_t)is->drawn.heard;
    }
    TIDE_CHECK(compared > FRAMES - 30u && moved > 20u && heard > 0u);
    TIDE_CHECK(beside[FRAMES - 1u].drawn.bodies > 18000 && beside[FRAMES - 1u].drawn.letters > 0);
    TIDE_CHECK(beside[FRAMES - 1u].drawn.inside == 0); // C the tick and the views share, asked once the tick has run
    // Heard in the same order whenever it's heard: a frame's handlers are never behind the frame before's
    for (uint32_t f = 1; f < FRAMES; f++) TIDE_CHECK(beside[f].drawn.heard >= in_update[f - 1u].drawn.heard);
}

TIDE_TEST(alongside_frames_show_and_hear_what_the_frame_before_did_with_ticks_in_the_update)
{
    compare(1.0f / 144.0f, true); // Frames with a tick and frames without, as a 144 Hz display's
}

TIDE_TEST(alongside_frames_with_a_tick_each)
{
    compare(1.0f / 60.0f, true);
}

TIDE_TEST(alongside_slow_frames_with_several_ticks_each)
{
    compare(1.0f / 24.0f, false); // All but the last of a frame's ticks run in its update
}
