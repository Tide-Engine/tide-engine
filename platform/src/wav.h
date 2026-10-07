#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// A WAV file of stereo 16-bit PCM at TIDE_AUDIO_RATE, of `frames` interleaved
// stereo frames of floats: each clamped to -1..1 and rounded to the nearest
// of what 16 bits hold. Returns the file's bytes, malloc'd, with their count
// in `*size`; NULL for a count of frames no file can hold.
uint8_t *tide_wav_encode(const float *stereo, uint32_t frames, size_t *size);

// Reads such a file's header back: true when `bytes` are a WAV of 16-bit
// PCM, with its channels, rate and frame count in the out parameters, and
// `*samples` at its first sample. For tests.
bool tide_wav_header(const uint8_t *bytes, size_t size, uint32_t *channels, uint32_t *rate, uint32_t *frames,
                     const int16_t **samples);
