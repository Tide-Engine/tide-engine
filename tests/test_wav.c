#include <stdlib.h>
#include <string.h>

#include "tide/wav.h"
#include "tide_test.h"

// The engine's WAV reader (tide/wav.h), against files made by hand of every
// kind it reads, and against what the writer writes.

typedef struct bytes {
    uint8_t *data;
    size_t size;
} bytes;

static void put(bytes *b, const void *data, const size_t size)
{
    b->data = realloc(b->data, b->size + size);
    memcpy(b->data + b->size, data, size);
    b->size += size;
}

static void put_u16(bytes *b, const uint32_t v) { put(b, (uint8_t[2]){(uint8_t)v, (uint8_t)(v >> 8)}, 2); }
static void put_u32(bytes *b, const uint32_t v) { put(b, (uint8_t[4]){(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)}, 4); }

// A WAV of `format` (1 PCM, 3 float), `channels` of `bits`, with `data`'s bytes as its samples; `extensible`
// writes the format as WAVE_FORMAT_EXTENSIBLE with the real one in the subformat, and `odd_chunk` puts a
// chunk of an odd size before the data, padded as the format says.
static bytes make(const uint32_t format, const uint32_t channels, const uint32_t rate, const uint32_t bits, const void *data,
                  const uint32_t size, const bool extensible, const bool odd_chunk)
{
    bytes fmt = {0};
    const uint32_t block = channels * bits / 8;
    put_u16(&fmt, extensible ? 0xFFFE : format);
    put_u16(&fmt, channels);
    put_u32(&fmt, rate);
    put_u32(&fmt, rate * block);
    put_u16(&fmt, block);
    put_u16(&fmt, bits);
    if (extensible) {
        put_u16(&fmt, 22); // cbSize
        put_u16(&fmt, bits);
        put_u32(&fmt, 0); // Channel mask
        put_u16(&fmt, format); // The subformat GUID starts with the format
        put(&fmt, (uint8_t[14]){0}, 14);
    }
    bytes w = {0};
    put(&w, "RIFF", 4);
    put_u32(&w, 0); // Patched below
    put(&w, "WAVEfmt ", 8);
    put_u32(&w, (uint32_t)fmt.size);
    put(&w, fmt.data, fmt.size);
    if (odd_chunk) {
        put(&w, "LIST", 4);
        put_u32(&w, 3);
        put(&w, "abc\0", 4); // 3 bytes and a pad
    }
    put(&w, "data", 4);
    put_u32(&w, size);
    put(&w, data, size);
    const uint32_t riff = (uint32_t)w.size - 8u;
    memcpy(w.data + 4, (uint8_t[4]){(uint8_t)riff, (uint8_t)(riff >> 8), (uint8_t)(riff >> 16), (uint8_t)(riff >> 24)}, 4);
    free(fmt.data);
    return w;
}

static bool near(const float a, const float b) { return a - b < 1e-6f && b - a < 1e-6f; }

TIDE_TEST(wav_reads_every_depth_it_says_it_does)
{
    tide_sound s;
    // 8-bit unsigned mono
    bytes w = make(1, 1, 8000, 8, (uint8_t[4]){0, 128, 192, 255}, 4, false, false);
    TIDE_REQUIRE(tide_wav_decode(w.data, w.size, &s));
    TIDE_CHECK(s.frames == 4 && s.channels == 1 && s.rate == 8000);
    TIDE_CHECK(s.samples[0] == -1.0f && s.samples[1] == 0.0f && s.samples[2] == 0.5f && near(s.samples[3], 127.0f / 128.0f));
    tide_sound_free(&s);
    free(w.data);
    // 16-bit stereo
    w = make(1, 2, 44100, 16, (uint8_t[8]){0x00, 0x80, 0xFF, 0x7F, 0x00, 0x40, 0x00, 0x00}, 8, false, false);
    TIDE_REQUIRE(tide_wav_decode(w.data, w.size, &s));
    TIDE_CHECK(s.frames == 2 && s.channels == 2 && s.rate == 44100);
    TIDE_CHECK(s.samples[0] == -1.0f && near(s.samples[1], 32767.0f / 32768.0f) && s.samples[2] == 0.5f && s.samples[3] == 0.0f);
    tide_sound_free(&s);
    free(w.data);
    // 24-bit mono: 0x400000 is 0.5, 0xC00000 is -0.5, 0x800000 is -1
    w = make(1, 1, 48000, 24, (uint8_t[9]){0x00, 0x00, 0x40, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x80}, 9, false, false);
    TIDE_REQUIRE(tide_wav_decode(w.data, w.size, &s));
    TIDE_CHECK(s.frames == 3 && s.samples[0] == 0.5f && s.samples[1] == -0.5f && s.samples[2] == -1.0f);
    tide_sound_free(&s);
    free(w.data);
    // 32-bit mono: 0x40000000 is 0.5
    w = make(1, 1, 48000, 32, (uint8_t[8]){0, 0, 0, 0x40, 0, 0, 0, 0x80}, 8, false, false);
    TIDE_REQUIRE(tide_wav_decode(w.data, w.size, &s));
    TIDE_CHECK(s.frames == 2 && s.samples[0] == 0.5f && s.samples[1] == -1.0f);
    tide_sound_free(&s);
    free(w.data);
    // Floats of 32 bits, extensible
    const float f[3] = {0.25f, -0.75f, 2.0f};
    w = make(3, 1, 22050, 32, f, sizeof f, true, false);
    TIDE_REQUIRE(tide_wav_decode(w.data, w.size, &s));
    TIDE_CHECK(s.frames == 3 && s.rate == 22050 && s.samples[0] == 0.25f && s.samples[1] == -0.75f && s.samples[2] == 2.0f);
    tide_sound_free(&s);
    free(w.data);
    // Floats of 64 bits, with an odd chunk before the data
    const double d[2] = {0.125, -1.0};
    w = make(3, 1, 96000, 64, d, sizeof d, false, true);
    TIDE_REQUIRE(tide_wav_decode(w.data, w.size, &s));
    TIDE_CHECK(s.frames == 2 && s.rate == 96000 && s.samples[0] == 0.125f && s.samples[1] == -1.0f);
    tide_sound_free(&s);
    free(w.data);
    // Extensible PCM of 16 bits
    w = make(1, 2, 48000, 16, (uint8_t[4]){0x00, 0x40, 0x00, 0xC0}, 4, true, false);
    TIDE_REQUIRE(tide_wav_decode(w.data, w.size, &s));
    TIDE_CHECK(s.frames == 1 && s.channels == 2 && s.samples[0] == 0.5f && s.samples[1] == -0.5f);
    tide_sound_free(&s);
    free(w.data);
    // No frames at all
    w = make(1, 1, 8000, 16, NULL, 0, false, false);
    TIDE_REQUIRE(tide_wav_decode(w.data, w.size, &s));
    TIDE_CHECK(s.frames == 0 && s.samples != NULL);
    tide_sound_free(&s);
    free(w.data);
}

TIDE_TEST(wav_refuses_what_it_cant_read)
{
    tide_sound s = {.samples = (float *)1};
    bytes w = make(1, 3, 48000, 16, (uint8_t[6]){0}, 6, false, false); // Three channels
    TIDE_CHECK(!tide_wav_decode(w.data, w.size, &s) && s.samples == NULL);
    free(w.data);
    w = make(1, 1, 48000, 12, (uint8_t[3]){0}, 3, false, false); // 12 bits
    TIDE_CHECK(!tide_wav_decode(w.data, w.size, &s));
    free(w.data);
    w = make(2, 1, 48000, 8, (uint8_t[2]){0}, 2, false, false); // ADPCM
    TIDE_CHECK(!tide_wav_decode(w.data, w.size, &s));
    free(w.data);
    w = make(3, 1, 48000, 16, (uint8_t[2]){0}, 2, false, false); // Floats of 16 bits
    TIDE_CHECK(!tide_wav_decode(w.data, w.size, &s));
    free(w.data);
    w = make(1, 1, 48000, 16, (uint8_t[4]){0}, 4, false, false);
    TIDE_CHECK(!tide_wav_decode(w.data, w.size - 1, &s)); // Cut short in its data
    TIDE_CHECK(!tide_wav_decode(w.data, 30, &s));         // Cut short in its header
    TIDE_CHECK(!tide_wav_decode(w.data, 11, &s));         // Not even RIFF's
    w.data[8] = 'X';
    TIDE_CHECK(!tide_wav_decode(w.data, w.size, &s)); // Not WAVE
    free(w.data);
    // A data chunk before any fmt
    bytes w2 = {0};
    put(&w2, "RIFF", 4);
    put_u32(&w2, 20);
    put(&w2, "WAVEdata", 8);
    put_u32(&w2, 4);
    put(&w2, (uint8_t[4]){0}, 4);
    TIDE_CHECK(!tide_wav_decode(w2.data, w2.size, &s));
    free(w2.data);
    TIDE_CHECK(!tide_wav_decode((const uint8_t *)"not a wav at all", 16, &s));
}

TIDE_TEST(wav_reads_what_it_writes)
{
    const float stereo[] = {0.0f, 1.0f, -1.0f, 0.5f, 2.0f, -3.0f, 0.00001f, -0.00001f};
    size_t size = 0;
    uint8_t *wav = tide_wav_encode(stereo, 4, 48000, &size);
    TIDE_REQUIRE(wav != NULL);
    tide_sound s;
    TIDE_REQUIRE(tide_wav_decode(wav, size, &s));
    TIDE_CHECK(s.frames == 4 && s.channels == 2 && s.rate == 48000);
    TIDE_CHECK(s.samples[0] == 0.0f && near(s.samples[1], 32767.0f / 32768.0f) && near(s.samples[2], -32767.0f / 32768.0f));
    TIDE_CHECK(near(s.samples[3], 16384.0f / 32768.0f)); // 0.5 rounds away from zero
    TIDE_CHECK(near(s.samples[4], 32767.0f / 32768.0f) && near(s.samples[5], -32767.0f / 32768.0f)); // Clamped
    TIDE_CHECK(s.samples[6] == 0.0f && s.samples[7] == 0.0f);
    tide_sound_free(&s);
    free(wav);
    TIDE_CHECK(tide_wav_encode(stereo, 0xFFFFFFFFu, 48000, &size) == NULL); // Past what a file holds
}
