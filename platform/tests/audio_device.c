// The audio device (platform/src/audio_out.h): opened as a window's is, a
// voice of no volume played through it, so nobody hears the test, and the
// mixer forgotten while the device's thread renders, which has to come back
// once that thread took it. On a machine with no device nothing opens, which
// is what every other program gets there, and the test passes all the same.

#include <stdio.h>

#include "audio_out.h"
#include "mixer.h"
#include "tide/time.h"
#include "tide_test.h"

static void wait_ms(const uint32_t ms)
{
    const uint64_t began = tide_time_now_ns();
    while (tide_time_now_ns() - began < (uint64_t)ms * 1000000u) tide_yield();
}

TIDE_TEST(audio_device_plays_from_the_mixer_on_its_own_thread)
{
    tide_mixer_init();
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
        // A forget is taken by that thread, and the call comes back once it has
        const uint64_t began = tide_time_now_ns();
        tide_mixer_forget();
        const uint64_t took_ms = (tide_time_now_ns() - began) / 1000000u;
        TIDE_CHECK(took_ms < 400);
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
}
