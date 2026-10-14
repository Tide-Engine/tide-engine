// The audio device (platform/src/audio_out.h): opened as a window's is, a
// voice of no volume played through it, so nobody hears the test, voices with
// effects whose states are memory of their own coming and going meanwhile,
// and the mixer forgotten while the device's thread renders, which has to come
// back once that thread took it. On a machine with no device nothing opens, which
// is what every other program gets there, and the test passes all the same.

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "audio_in.h"
#include "audio_out.h"
#include "mixer.h"
#include "tide/time.h"
#include "tide_test.h"

// An effect whose state is far more than a voice holds itself, which it
// writes all of every block: memory the frame's thread makes and frees. Each
// block it finds what its last block left, or zeros, in every byte: a state
// freed under it, or handed to two voices, wouldn't be.
typedef struct wide_state {
    uint32_t blocks;
    uint8_t rest[200000];
} wide_state;
static _Atomic uint32_t wide_blocks;
static _Atomic bool wide_torn;
static void wide(float *samples, void *state, const void *knobs)
{
    (void)samples;
    (void)knobs;
    wide_state *s = state;
    const uint8_t was = (uint8_t)s->blocks;
    for (size_t i = 0; i < sizeof s->rest; i++) {
        if (s->rest[i] != was) atomic_store(&wide_torn, true);
    }
    s->blocks++;
    memset(s->rest, (int)(uint8_t)s->blocks, sizeof s->rest);
    atomic_fetch_add(&wide_blocks, 1);
}

static void wait_ms(const uint32_t ms)
{
    const uint64_t began = tide_time_now_ns();
    while (tide_time_now_ns() - began < (uint64_t)ms * 1000000u) tide_yield();
}

TIDE_TEST(audio_device_plays_from_the_mixer_on_its_own_thread)
{
    // As a window opens it: with nothing made the mixer first, so the device's thread can be the first to ask it for anything
    const bool opened = tide_audio_out_open();
    printf("audio device: %s\n", opened ? "opened" : "none (nothing is heard)");
    static float ones[2 * 480];
    for (int i = 0; i < 2 * 480; i++) ones[i] = 1.0f;
    const tide_clip dc = {.samples = ones, .frames = 480, .channels = 2, .rate = TIDE_AUDIO_RATE, .id = 1};
    tide_audio_frame f = {0};
    tide_audio_reset(&f);
    tide_audio_voice(&f, &(tide_voice){.id = 5, .clip = dc, .loop = true, .playing = true, .volume = 0.0f, .pitch = 1.0f});
    tide_mixer_submit(&f);
    if (opened) {
        // The device's thread takes the frame and plays the voice, silently
        bool played = false;
        for (int i = 0; i < 50 && !played; i++) {
            wait_ms(10);
            played = tide_mixer_voices() == 1;
        }
        TIDE_CHECK(played);
        // Voices whose effects' states are memory of their own come and go while that thread renders: each
        // state is made on this thread, written all over on that one, and freed here once that one is done
        // with it, which neither waits for
        for (int round = 0; round < 60; round++) {
            tide_audio_reset(&f);
            tide_audio_voice(&f, &(tide_voice){.id = 5, .clip = dc, .loop = true, .playing = true, .volume = 0.0f, .pitch = 1.0f});
            for (uint64_t k = 0; k < 4; k++) {
                if ((round + (int)k) % 3 == 0) continue; // Each stops every third frame, and starts over
                tide_audio_voice(&f, &(tide_voice){.id = 100 + k, .playing = true, .volume = 0.0f, .pitch = 1.0f,
                                                   .effect = {.fn = wide, .id = 9, .state_size = sizeof(wide_state)}});
            }
            if (round % 2) tide_audio_set_bus_effect(&f, 2, &(tide_audio_effect){.fn = wide, .id = 9, .state_size = sizeof(wide_state)});
            tide_mixer_submit(&f);
            TIDE_CHECK(tide_mixer_states() <= 16); // The ones that play, and the ones that thread hasn't let go of yet
            wait_ms(4);
        }
        TIDE_CHECK(atomic_load(&wide_blocks) > 0 && !atomic_load(&wide_torn));
        tide_audio_reset(&f);
        tide_audio_voice(&f, &(tide_voice){.id = 5, .clip = dc, .loop = true, .playing = true, .volume = 0.0f, .pitch = 1.0f});
        tide_mixer_submit(&f);
        // A forget is taken by that thread, and the call comes back once it has
        const uint64_t began = tide_time_now_ns();
        tide_mixer_forget();
        const uint64_t took_ms = (tide_time_now_ns() - began) / 1000000u;
        TIDE_CHECK(took_ms < 400);
        TIDE_CHECK(tide_mixer_states() == 0); // ...and every state is freed by then
        bool gone = false;
        for (int i = 0; i < 50 && !gone; i++) {
            wait_ms(10);
            gone = tide_mixer_voices() == 0;
        }
        TIDE_CHECK(gone);
    } else {
        tide_mixer_forget(); // At once, with no thread to wait for
    }
    tide_audio_out_close();
    tide_audio_out_close(); // Twice is fine
    tide_audio_free(&f);
    // The capture device: opened where the machine has one the program may
    // use (a muted one captures silence, which is fine), and closed
    const bool heard = tide_audio_in_open();
    printf("capture device: %s\n", heard ? "opened" : "none (the microphone is silent)");
    if (heard) wait_ms(100);
    tide_audio_in_close();
    tide_audio_in_close();
}
