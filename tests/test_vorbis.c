// The Ogg Vorbis reader (tide/vorbis.h) against ffmpeg: three files its
// encoder wrote (tests/audio: a stereo tone at 44.1 kHz, a mono one at 8 kHz
// with one block size, and stereo noise at 48 kHz, whose transients take short
// blocks), each decoded by ffmpeg to floats, which the reader has to give to
// within a float's rounding of a lossy stream's arithmetic. Then the transform
// against its definition, reading in pieces, and what's refused.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/vorbis.h"
#include "tide/wav.h"
#include "tide_test.h"

extern const unsigned char tide_ref_tone_ogg[], tide_ref_tone_pcm[], tide_ref_low_ogg[], tide_ref_low_pcm[], tide_ref_noise_ogg[],
    tide_ref_noise_pcm[];
extern const unsigned int tide_ref_tone_ogg_size, tide_ref_tone_pcm_size, tide_ref_low_ogg_size, tide_ref_low_pcm_size,
    tide_ref_noise_ogg_size, tide_ref_noise_pcm_size;

typedef struct reference {
    const char *name;
    const unsigned char *ogg;
    uint32_t ogg_size;
    const unsigned char *pcm;
    uint32_t pcm_size;
    uint32_t channels, rate, granule;
} reference;

static const reference refs[3] = {
    {"tone", tide_ref_tone_ogg, 0, tide_ref_tone_pcm, 0, 2, 44100, 17640},
    {"low", tide_ref_low_ogg, 0, tide_ref_low_pcm, 0, 1, 8000, 2400},
    {"noise", tide_ref_noise_ogg, 0, tide_ref_noise_pcm, 0, 2, 48000, 28800},
};

static reference ref(const int i)
{
    reference r = refs[i];
    const unsigned int ogg_sizes[3] = {tide_ref_tone_ogg_size, tide_ref_low_ogg_size, tide_ref_noise_ogg_size};
    const unsigned int pcm_sizes[3] = {tide_ref_tone_pcm_size, tide_ref_low_pcm_size, tide_ref_noise_pcm_size};
    r.ogg_size = ogg_sizes[i];
    r.pcm_size = pcm_sizes[i];
    return r;
}

static float ref_sample(const reference *r, const uint32_t i)
{
    float v;
    memcpy(&v, r->pcm + (size_t)i * 4u, 4);
    return v;
}

TIDE_TEST(vorbis_decodes_what_ffmpeg_decodes)
{
    for (int k = 0; k < 3; k++) {
        const reference r = ref(k);
        tide_sound s;
        TIDE_CHECK(tide_vorbis_decode(r.ogg, r.ogg_size, &s));
        TIDE_CHECK(s.channels == r.channels);
        TIDE_CHECK(s.rate == r.rate);
        const uint32_t ref_frames = r.pcm_size / 4u / r.channels;
        // The reader stops at the granule, which says what the file holds; ffmpeg gives what the blocks
        // complete, 128 frames fewer for the two the encoder padded nothing after and 160 more for the third
        TIDE_CHECK(s.frames == r.granule);
        float worst = 0.0f;
        double sum = 0.0;
        const uint32_t count = (s.frames < ref_frames ? s.frames : ref_frames) * r.channels;
        for (uint32_t i = 0; i < count; i++) {
            const float d = fabsf(s.samples[i] - ref_sample(&r, i));
            if (d > worst) worst = d;
            sum += d;
        }
        printf("        "); printf("%s: %u frames (ffmpeg %u), worst difference %g, mean %g\n", r.name, s.frames, ref_frames, (double)worst,
                       count ? sum / count : 0.0);
        TIDE_CHECK(worst < 2e-4f);
        float loudest = 0.0f;
        for (uint32_t i = 0; i < count; i++)
            if (fabsf(s.samples[i]) > loudest) loudest = fabsf(s.samples[i]);
        TIDE_CHECK(loudest > 0.05f);
        tide_sound_free(&s);
    }
}

TIDE_TEST(vorbis_says_what_a_file_holds)
{
    const reference r = ref(0);
    const char *why = NULL;
    tide_vorbis *f = tide_vorbis_open(r.ogg, r.ogg_size, &why);
    TIDE_CHECK(f != NULL);
    TIDE_CHECK(tide_vorbis_channels(f) == 2);
    TIDE_CHECK(tide_vorbis_rate(f) == 44100);
    TIDE_CHECK(tide_vorbis_frames(f) == 17640); // The granule: 0.4 s, of which the encoder finished 17512
    TIDE_CHECK(tide_vorbis_reader_size(f) > 4096);
    tide_vorbis_close(f);
    tide_vorbis_close(NULL);
}

TIDE_TEST(vorbis_reads_in_pieces_and_rewinds)
{
    const reference r = ref(2);
    tide_sound whole;
    TIDE_CHECK(tide_vorbis_decode(r.ogg, r.ogg_size, &whole));
    tide_vorbis *f = tide_vorbis_open(r.ogg, r.ogg_size, NULL);
    tide_vorbis_reader *rd = tide_vorbis_reader_open(f);
    float *got = calloc((size_t)whole.frames * 2u + 16u, sizeof(float));
    const uint32_t sizes[4] = {1, 7, 100, 1000};
    uint32_t at = 0, i = 0;
    for (;;) {
        const uint32_t n = tide_vorbis_read(rd, got + (size_t)at * 2u, sizes[i++ % 4u]);
        if (!n) break;
        at += n;
        TIDE_CHECK(at <= whole.frames);
    }
    TIDE_CHECK(at == whole.frames);
    TIDE_CHECK(memcmp(got, whole.samples, (size_t)whole.frames * 2u * sizeof(float)) == 0);
    TIDE_CHECK(tide_vorbis_read(rd, got, 10) == 0);
    tide_vorbis_rewind(rd);
    TIDE_CHECK(tide_vorbis_read(rd, got, 3000) == 3000);
    TIDE_CHECK(memcmp(got, whole.samples, 3000u * 2u * sizeof(float)) == 0);
    free(got);
    tide_vorbis_reader_close(rd);
    tide_vorbis_close(f);
    tide_sound_free(&whole);
}

TIDE_TEST(vorbis_imdct_is_its_definition)
{
    static const uint32_t sizes[3] = {64, 256, 2048};
    uint32_t seed = 12345;
    for (int k = 0; k < 3; k++) {
        const uint32_t n = sizes[k], M = n / 2;
        float *x = calloc(M, sizeof(float)), *y = calloc(n, sizeof(float));
        for (uint32_t i = 0; i < M; i++) {
            seed = seed * 1664525u + 1013904223u;
            x[i] = (float)(seed >> 8) / (float)(1u << 24) - 0.5f;
        }
        tide_vorbis_imdct_check(n, x, y);
        float worst = 0.0f, biggest = 0.0f;
        for (uint32_t i = 0; i < n; i++) {
            double v = 0.0;
            for (uint32_t j = 0; j < M; j++) {
                v += (double)x[j] * cos(2.0 * 3.14159265358979323846 / (double)n * ((double)i + 0.5 + (double)n / 4.0) * ((double)j + 0.5));
            }
            const float d = fabsf(y[i] - (float)v);
            if (d > worst) worst = d;
            if (fabs(v) > biggest) biggest = (float)fabs(v);
        }
        printf("        "); printf("n %u: worst %g of values up to %g\n", n, (double)worst, (double)biggest);
        TIDE_CHECK(worst < 1e-3f * (biggest > 1.0f ? biggest : 1.0f) / 10.0f);
        free(x);
        free(y);
    }
}

TIDE_TEST(vorbis_refuses_what_isnt_one)
{
    const reference r = ref(1);
    const char *why = NULL;
    tide_sound s;
    uint8_t garbage[300];
    for (int i = 0; i < 300; i++) garbage[i] = (uint8_t)(i * 7 + 3);
    TIDE_CHECK(tide_vorbis_open(garbage, sizeof garbage, &why) == NULL);
    TIDE_CHECK(why && why[0]);
    TIDE_CHECK(!tide_vorbis_decode(garbage, sizeof garbage, &s) && s.samples == NULL);
    float stereo[20] = {0};
    size_t wav_size;
    uint8_t *wav = tide_wav_encode(stereo, 10, 48000, &wav_size);
    TIDE_CHECK(tide_vorbis_open(wav, wav_size, &why) == NULL);
    free(wav);
    TIDE_CHECK(tide_vorbis_open(r.ogg, 0, &why) == NULL);
    const uint32_t cuts[5] = {3, 30, 100, r.ogg_size / 2u, r.ogg_size - 1u};
    for (int i = 0; i < 5; i++) {
        TIDE_CHECK(tide_vorbis_open(r.ogg, cuts[i], &why) == NULL);
        TIDE_CHECK(strcmp(why, "") != 0);
    }
    uint8_t *broken = malloc(r.ogg_size);
    memcpy(broken, r.ogg, r.ogg_size);
    broken[r.ogg_size - 10u] ^= 0x55u;
    TIDE_CHECK(tide_vorbis_open(broken, r.ogg_size, &why) == NULL);
    TIDE_CHECK(strcmp(why, "a page's checksum is wrong") == 0);
    memcpy(broken, r.ogg, r.ogg_size);
    broken[0] = 'X';
    TIDE_CHECK(tide_vorbis_open(broken, r.ogg_size, &why) == NULL);
    free(broken);
    TIDE_CHECK(tide_vorbis_reader_open(NULL) == NULL);
    TIDE_CHECK(tide_vorbis_read(NULL, stereo, 1) == 0);
    tide_vorbis_reader_close(NULL);
}
