// What every capture backend pushes through (audio_in.h): the device's frames,
// whatever their channels and rate, averaged to one channel and resampled to
// the mixer's rate, into the mixer's capture ring. The backends themselves
// are in desktop/audio.c, android/audio.c and web/audio.c, under the output's.

#include "audio_in.h"

#include <string.h>

#include "mixer.h"

#define PUSH_MOST 1024u // Frames resampled at a time before they go to the ring

static struct {
    float last;    // The device's last frame, for the interpolation into the next call
    double cursor; // How far from `last` to the first frame of the next push, in the device's frames
    bool primed;
} in;

void tide_audio_in_reset(void) { memset(&in, 0, sizeof in); }

void tide_audio_in_push(const float *samples, const uint32_t frames, const uint32_t rate, const uint32_t channels)
{
    if (!samples || !frames || !rate || !channels || channels > 8u) return;
    float out[PUSH_MOST];
    uint32_t count = 0;
    const double step = (double)rate / (double)TIDE_AUDIO_RATE; // Device frames an output frame
    for (uint32_t i = 0; i < frames; i++) {
        float mono = 0.0f;
        for (uint32_t c = 0; c < channels; c++) mono += samples[(size_t)i * channels + c];
        mono /= (float)channels;
        if (!in.primed) { // The first frame: nothing before it to interpolate from
            in.primed = true;
            in.last = mono;
            in.cursor = 0.0;
            continue;
        }
        // Output frames from the last device frame up to this one: at the mixer's own rate, each frame as it came, a frame late
        while (in.cursor < 1.0) {
            out[count++] = in.last + (mono - in.last) * (float)in.cursor;
            in.cursor += step;
            if (count == PUSH_MOST) {
                tide_mixer_capture(out, count);
                count = 0;
            }
        }
        in.cursor -= 1.0;
        in.last = mono;
    }
    if (count) tide_mixer_capture(out, count);
}
