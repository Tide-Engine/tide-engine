// Model a waiting frame thread resuming immediately when the audio callback
// publishes completion. This needs neither a physical device nor a lucky
// thread preemption between the callback's atomic stores.
#include <stdatomic.h>

#include "tide_test.h"

static void after_store(const volatile void *address);

// Clang's stdatomic store, with an observer after the actual publication.
// Only this test's copy of the mixer is instrumented.
#undef atomic_store_explicit
#define atomic_store_explicit(address, value, order) \
    do { \
        __c11_atomic_store((address), (value), (order)); \
        after_store(address); \
    } while (0)
#include "../src/mixer.c"

static bool waiting;
static unsigned completions;

static void after_store(const volatile void *address)
{
    if (!waiting || address != &m.forget_done) return;
    // The waiter's next operation in tide_mixer_forget. Every old state must
    // already be reclaimable when completion becomes observable.
    free_settled();
    TIDE_CHECK(tide_mixer_states() == 0);
    completions++;
}

static void effect(float *samples, void *state, const void *knobs)
{
    (void)samples;
    (void)state;
    (void)knobs;
}

TIDE_TEST(audio_forget_completion_releases_voice_and_bus_states)
{
    tide_audio_frame f = {0};
    tide_audio_reset(&f);
    const tide_audio_effect big = {.fn = effect, .id = 1, .state_size = 200000};
    tide_audio_voice(&f, &(tide_voice){.id = 1, .playing = true, .pitch = 1.0f, .effect = big});
    tide_audio_set_bus_effect(&f, 2, &big);
    tide_mixer_submit(&f);
    float out[TIDE_AUDIO_SAMPLES];
    tide_mixer_render(out, TIDE_AUDIO_BLOCK);
    TIDE_CHECK(tide_mixer_states() == 2);
    waiting = true;
    tide_mixer_forget();
    waiting = false;
    TIDE_CHECK(completions == 1);
    TIDE_CHECK(tide_mixer_states() == 0);
    tide_audio_free(&f);
}
