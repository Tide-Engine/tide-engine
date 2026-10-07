#include "tide/wav.h"

#include <stdlib.h>
#include <string.h>

#include "tide/page.h"

static uint32_t get_u16(const uint8_t *at) { return (uint32_t)at[0] | ((uint32_t)at[1] << 8); }
static uint32_t get_u32(const uint8_t *at) { return get_u16(at) | (get_u16(at + 2) << 16); }

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

enum { FORMAT_PCM = 1, FORMAT_FLOAT = 3, FORMAT_EXTENSIBLE = 0xFFFE };

#define MOST_SAMPLES (1u << 30) // Past which no sound is meant: a file that says more is refused

bool tide_wav_decode(const uint8_t *b, const size_t size, tide_sound *out)
{
    *out = (tide_sound){0};
    if (size < 12 || memcmp(b, "RIFF", 4) != 0 || memcmp(b + 8, "WAVE", 4) != 0) return false;
    size_t at = 12;
    uint32_t format = 0, channels = 0, rate = 0, bits = 0, block = 0;
    bool fmt = false;
    while (at + 8 <= size) {
        const uint32_t length = get_u32(b + at + 4);
        if (length > size - at - 8) return false; // Cut short
        const uint8_t *chunk = b + at + 8;
        if (memcmp(b + at, "fmt ", 4) == 0) {
            if (length < 16) return false;
            format = get_u16(chunk);
            channels = get_u16(chunk + 2);
            rate = get_u32(chunk + 4);
            block = get_u16(chunk + 12);
            bits = get_u16(chunk + 14);
            if (format == FORMAT_EXTENSIBLE) {
                // The real format is the first two bytes of the subformat's GUID, after cbSize and two more fields
                if (length < 40 || get_u16(chunk + 16) < 22) return false;
                format = get_u16(chunk + 24);
            }
            fmt = true;
        } else if (memcmp(b + at, "data", 4) == 0) {
            if (!fmt) return false;
            if (channels < 1 || channels > 2 || rate == 0 || block != channels * (bits / 8u)) return false;
            if (format == FORMAT_PCM ? !(bits == 8 || bits == 16 || bits == 24 || bits == 32)
                                     : format == FORMAT_FLOAT ? !(bits == 32 || bits == 64) : true) return false;
            const uint32_t frames = length / block;
            if ((uint64_t)frames * channels > MOST_SAMPLES) return false;
            const size_t count = (size_t)frames * channels;
            float *samples = tide_alloc(count ? count * sizeof *samples : sizeof *samples);
            const uint32_t width = bits / 8u;
            for (size_t i = 0; i < count; i++) {
                const uint8_t *s = chunk + i * width;
                float v;
                if (format == FORMAT_FLOAT) {
                    if (bits == 32) {
                        uint32_t u = get_u32(s);
                        memcpy(&v, &u, sizeof v);
                    } else {
                        uint64_t u = (uint64_t)get_u32(s) | ((uint64_t)get_u32(s + 4) << 32);
                        double d;
                        memcpy(&d, &u, sizeof d);
                        v = (float)d;
                    }
                } else if (bits == 8) {
                    v = ((float)s[0] - 128.0f) / 128.0f; // Unsigned, as 8-bit WAV is
                } else if (bits == 16) {
                    v = (float)(int16_t)get_u16(s) / 32768.0f;
                } else if (bits == 24) {
                    const int32_t i24 = (int32_t)(get_u16(s) | ((uint32_t)s[2] << 16) | ((s[2] & 0x80u) ? 0xFF000000u : 0u));
                    v = (float)i24 / 8388608.0f;
                } else {
                    v = (float)(int32_t)get_u32(s) / 2147483648.0f;
                }
                samples[i] = v;
            }
            *out = (tide_sound){.samples = samples, .frames = frames, .channels = channels, .rate = rate};
            return true;
        }
        at += 8 + length + (length & 1u); // Chunks are padded to even sizes
    }
    return false;
}

void tide_sound_free(tide_sound *sound)
{
    free(sound->samples);
    *sound = (tide_sound){0};
}

// A float sample as 16 bits: clamped, rounded to the nearest, ties away from zero.
static int16_t pcm16(const float x)
{
    const float c = x > 1.0f ? 1.0f : (x < -1.0f ? -1.0f : x);
    const float scaled = c * 32767.0f;
    return (int16_t)(int)(scaled + (scaled >= 0.0f ? 0.5f : -0.5f));
}

#define HEADER 44u

uint8_t *tide_wav_encode(const float *stereo, const uint32_t frames, const uint32_t rate, size_t *size)
{
    const uint64_t data = (uint64_t)frames * 4u;
    if (data + HEADER > 0xFFFFFFFFu) return NULL;
    uint8_t *w = tide_alloc(HEADER + (size_t)data);
    memcpy(w, "RIFF", 4);
    put_u32(w + 4, (uint32_t)(HEADER - 8u + data));
    memcpy(w + 8, "WAVEfmt ", 8);
    put_u32(w + 16, 16);           // The fmt chunk's size
    put_u16(w + 20, FORMAT_PCM);
    put_u16(w + 22, 2);            // Channels
    put_u32(w + 24, rate);
    put_u32(w + 28, rate * 4u);    // Bytes a second
    put_u16(w + 32, 4);            // Bytes a frame
    put_u16(w + 34, 16);           // Bits a sample
    memcpy(w + 36, "data", 4);
    put_u32(w + 40, (uint32_t)data);
    uint8_t *at = w + HEADER;
    for (uint64_t i = 0; i < (uint64_t)frames * 2u; i++, at += 2) put_u16(at, (uint16_t)pcm16(stereo[i]));
    *size = HEADER + (size_t)data;
    return w;
}
