#include "wav.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "tide/audio.h"
#include "tide/page.h"

static void put_u16(uint8_t *at, const uint32_t v)
{
    at[0] = (uint8_t)v;
    at[1] = (uint8_t)(v >> 8);
}

static void put_u32(uint8_t *at, const uint32_t v)
{
    put_u16(at, v & 0xFFFFu);
    put_u16(at + 2, v >> 16);
}

static uint32_t get_u16(const uint8_t *at) { return (uint32_t)at[0] | ((uint32_t)at[1] << 8); }
static uint32_t get_u32(const uint8_t *at) { return get_u16(at) | (get_u16(at + 2) << 16); }

// A float sample as 16 bits: clamped, rounded to the nearest, ties away from zero.
static int16_t pcm16(const float x)
{
    const float c = x > 1.0f ? 1.0f : (x < -1.0f ? -1.0f : x);
    const float scaled = c * 32767.0f;
    return (int16_t)(int)(scaled + (scaled >= 0.0f ? 0.5f : -0.5f));
}

#define HEADER 44u

uint8_t *tide_wav_encode(const float *stereo, const uint32_t frames, size_t *size)
{
    const uint64_t data = (uint64_t)frames * 4u;
    if (data + HEADER > 0xFFFFFFFFu) return NULL;
    uint8_t *w = tide_alloc(HEADER + (size_t)data);
    memcpy(w, "RIFF", 4);
    put_u32(w + 4, (uint32_t)(HEADER - 8u + data));
    memcpy(w + 8, "WAVEfmt ", 8);
    put_u32(w + 16, 16);                    // The fmt chunk's size
    put_u16(w + 20, 1);                     // PCM
    put_u16(w + 22, 2);                     // Channels
    put_u32(w + 24, TIDE_AUDIO_RATE);
    put_u32(w + 28, TIDE_AUDIO_RATE * 4u);  // Bytes a second
    put_u16(w + 32, 4);                     // Bytes a frame
    put_u16(w + 34, 16);                    // Bits a sample
    memcpy(w + 36, "data", 4);
    put_u32(w + 40, (uint32_t)data);
    uint8_t *at = w + HEADER;
    for (uint64_t i = 0; i < (uint64_t)frames * 2u; i++, at += 2) put_u16(at, (uint16_t)pcm16(stereo[i]));
    *size = HEADER + (size_t)data;
    return w;
}

bool tide_wav_header(const uint8_t *b, const size_t size, uint32_t *channels, uint32_t *rate, uint32_t *frames,
                     const int16_t **samples)
{
    if (size < 12 || memcmp(b, "RIFF", 4) != 0 || memcmp(b + 8, "WAVE", 4) != 0) return false;
    size_t at = 12;
    bool fmt = false;
    uint32_t block = 0;
    while (at + 8 <= size) {
        const uint32_t length = get_u32(b + at + 4);
        if (at + 8 + length > size) return false;
        if (memcmp(b + at, "fmt ", 4) == 0 && length >= 16) {
            if (get_u16(b + at + 8) != 1 || get_u16(b + at + 22) != 16) return false;
            *channels = get_u16(b + at + 10);
            *rate = get_u32(b + at + 12);
            block = get_u16(b + at + 20);
            fmt = *channels > 0 && block == *channels * 2u;
        } else if (memcmp(b + at, "data", 4) == 0) {
            if (!fmt) return false;
            *frames = length / block;
            *samples = (const int16_t *)(const void *)(b + at + 8);
            return true;
        }
        at += 8 + length + (length & 1u);
    }
    return false;
}
