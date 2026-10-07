#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// WAV files, read and written by the engine's own code (from the RIFF and
// WAVE formats as Microsoft published them), so games carry no license terms.

// A decoded sound: interleaved samples of `channels` at `rate` frames a second.
typedef struct tide_sound {
    float *samples; // malloc'd: frames * channels floats, -1 to 1
    uint32_t frames;
    uint32_t channels; // 1 or 2
    uint32_t rate;
} tide_sound;

// Reads a WAV: PCM of 8, 16, 24 or 32 bits, or floats of 32 or 64, plain or
// WAVE_FORMAT_EXTENSIBLE, of one or two channels. A sample becomes a float by
// one division, so a file decodes to the same bits on every machine. False
// for anything else, or a file cut short, with nothing allocated.
bool tide_wav_decode(const uint8_t *bytes, size_t size, tide_sound *out);

// Lets a decoded sound's samples go.
void tide_sound_free(tide_sound *sound);

// A WAV file of stereo 16-bit PCM at `rate`, of `frames` interleaved stereo
// frames of floats: each clamped to -1..1 and rounded to the nearest of what
// 16 bits hold. Returns the file's bytes, malloc'd, with their count in
// `*size`; NULL for a count of frames no file can hold.
uint8_t *tide_wav_encode(const float *stereo, uint32_t frames, uint32_t rate, size_t *size);
