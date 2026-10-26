#include "audio_out.h"

#include <string.h>

#include "mixer.h"

// The mixer's stereo at TIDE_AUDIO_RATE, pulled a block at a time, and read
// out at a device's rate with linear interpolation between two of the mixer's
// frames, the one before the device's place (`prev`) and the one after
// (`cur`), `phase` of the way from the first to the second. One thread at a
// time: the device's.
static struct {
    float block[TIDE_AUDIO_SAMPLES]; // The mixer's last block
    uint32_t at;                     // Frames of it taken into `cur` so far
    float prev_l, prev_r, cur_l, cur_r;
    double phase;
    bool started;
} pull;

// The next of the mixer's frames into `cur`, the last into `prev`.
static void next_frame(void)
{
    if (pull.at >= TIDE_AUDIO_BLOCK) {
        tide_mixer_render(pull.block, TIDE_AUDIO_BLOCK);
        pull.at = 0;
    }
    pull.prev_l = pull.cur_l;
    pull.prev_r = pull.cur_r;
    pull.cur_l = pull.block[2 * pull.at];
    pull.cur_r = pull.block[2 * pull.at + 1];
    pull.at++;
}

void tide_audio_out_reset(void)
{
    memset(&pull, 0, sizeof pull);
}

void tide_audio_out_fill(float *out, const uint32_t frames, const uint32_t rate, const uint32_t channels)
{
    if (!pull.started) {
        pull.at = TIDE_AUDIO_BLOCK;
        pull.started = true;
        next_frame(); // `cur` is the mixer's first frame, `prev` silence
    }
    const double step = rate ? (double)TIDE_AUDIO_RATE / (double)rate : 1.0;
    const uint32_t wide = channels < 1 ? 1 : channels > 8 ? 8 : channels;
    for (uint32_t f = 0; f < frames; f++) {
        const float t = (float)pull.phase;
        const float l = pull.prev_l + (pull.cur_l - pull.prev_l) * t;
        const float r = pull.prev_r + (pull.cur_r - pull.prev_r) * t;
        float *o = out + (size_t)f * wide;
        if (wide == 1) {
            o[0] = (l + r) * 0.5f;
        } else {
            o[0] = l;
            o[1] = r;
            for (uint32_t c = 2; c < wide; c++) o[c] = 0.0f;
        }
        pull.phase += step;
        while (pull.phase >= 1.0) {
            pull.phase -= 1.0;
            next_frame();
        }
    }
}
