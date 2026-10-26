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

// The three files' samples, bit for bit: what every machine has to give (the
// floor's table of amplitudes among what they rest on)
static uint64_t samples_hash(const tide_sound *s)
{
    uint64_t h = 0xcbf29ce484222325ull;
    const uint8_t *p = (const uint8_t *)s->samples;
    for (size_t i = 0; i < (size_t)s->frames * s->channels * sizeof(float); i++) h = (h ^ p[i]) * 0x100000001b3ull;
    return h;
}

TIDE_TEST(vorbis_decodes_the_same_bits)
{
    static const uint64_t pinned[3] = {0xca9c328d751643b9ull, 0x7325d60925ae0697ull, 0xe6708c03af318539ull};
    for (int k = 0; k < 3; k++) {
        const reference r = ref(k);
        tide_sound s;
        TIDE_REQUIRE(tide_vorbis_decode(r.ogg, r.ogg_size, &s));
        TIDE_CHECK(samples_hash(&s) == pinned[k]);
        tide_sound_free(&s);
    }
}

// Files made by hand: Ogg's checksum, Vorbis's bits, and a file of the three
// headers (mono at 8 kHz, blocks of 256) around a setup header's bits

static uint32_t page_crc(const uint8_t *p, const size_t size)
{
    uint32_t crc = 0;
    for (size_t i = 0; i < size; i++) {
        crc ^= (uint32_t)p[i] << 24;
        for (int k = 0; k < 8; k++) crc = (crc & 0x80000000u) ? (crc << 1) ^ 0x04c11db7u : crc << 1;
    }
    return crc;
}

// Each page of a file its checksum again, after a test changed its bytes
static void fix_crcs(uint8_t *b, const size_t size)
{
    for (size_t at = 0; at + 27u <= size;) {
        size_t body = 0;
        for (uint32_t i = 0; i < b[at + 26]; i++) body += b[at + 27 + i];
        const size_t page = 27u + b[at + 26] + body;
        memset(b + at + 22, 0, 4);
        const uint32_t crc = page_crc(b + at, page);
        for (int i = 0; i < 4; i++) b[at + 22 + i] = (uint8_t)(crc >> (8 * i));
        at += page;
    }
}

typedef struct bit_writer {
    uint8_t bytes[200];
    uint32_t at;
} bit_writer;

static void put_bits(bit_writer *w, const uint32_t v, const uint32_t n)
{
    for (uint32_t i = 0; i < n; i++, w->at++) {
        if ((v >> i) & 1u) w->bytes[w->at >> 3] |= (uint8_t)(1u << (w->at & 7u));
    }
}

static void put_page(uint8_t *out, size_t *size, const uint8_t type, const uint32_t sequence, const uint8_t *const *packets,
                     const uint32_t *sizes, const uint32_t count)
{
    uint8_t *h = out + *size;
    memset(h, 0, 27);
    memcpy(h, "OggS", 4);
    h[5] = type;
    h[14] = 1; // The stream's serial
    h[18] = (uint8_t)sequence;
    h[26] = (uint8_t)count;
    size_t at = 27u + count;
    for (uint32_t i = 0; i < count; i++) {
        h[27 + i] = (uint8_t)sizes[i]; // Each under 255 bytes: one segment
        memcpy(h + at, packets[i], sizes[i]);
        at += sizes[i];
    }
    const uint32_t crc = page_crc(h, at);
    for (int i = 0; i < 4; i++) h[22 + i] = (uint8_t)(crc >> (8 * i));
    *size += at;
}

static size_t made_file(uint8_t out[512], const bit_writer *setup)
{
    uint8_t id[30] = {1, 'v', 'o', 'r', 'b', 'i', 's', 0, 0, 0, 0, 1, 0x40, 0x1f, 0, 0};
    id[28] = 0x88; // Both blocks 256
    id[29] = 1;
    const uint8_t comment[16] = {3, 'v', 'o', 'r', 'b', 'i', 's', 0, 0, 0, 0, 0, 0, 0, 0, 1};
    uint8_t s[7 + sizeof setup->bytes] = {5, 'v', 'o', 'r', 'b', 'i', 's'};
    memcpy(s + 7, setup->bytes, (setup->at + 7u) / 8u);
    size_t size = 0;
    const uint8_t *first[1] = {id};
    const uint32_t first_size[1] = {30};
    put_page(out, &size, 0x02, 0, first, first_size, 1);
    const uint8_t *second[2] = {comment, s};
    const uint32_t second_size[2] = {16, 7u + (setup->at + 7u) / 8u};
    put_page(out, &size, 0x04, 1, second, second_size, 2);
    return size;
}

// A codebook's head: its sync pattern, dimensions and entries
static void put_codebook(bit_writer *w, const uint32_t dimensions, const uint32_t entries)
{
    put_bits(w, 0x564342u, 24);
    put_bits(w, dimensions, 16);
    put_bits(w, entries, 24);
}

TIDE_TEST(vorbis_refuses_codebooks_past_the_limits)
{
    uint8_t file[512];
    const char *why = NULL;
    // One codebook of more entries than a file's may have: refused at its head, before
    // millions of lengths and a tree of them are made (it took the fuzzer's file a second)
    bit_writer w = {0};
    put_bits(&w, 0, 8); // One codebook
    put_codebook(&w, 1, TIDE_VORBIS_MAX_ENTRIES + 1u);
    size_t size = made_file(file, &w);
    TIDE_CHECK(tide_vorbis_open(file, size, &why) == NULL);
    TIDE_CHECK(strcmp(why, "the file's codebooks have more entries in all than a file's may (TIDE_VORBIS_MAX_ENTRIES)") == 0);
    // As many in one as there may be is no limit's, and goes on to be cut short
    w = (bit_writer){0};
    put_bits(&w, 0, 8);
    put_codebook(&w, 1, TIDE_VORBIS_MAX_ENTRIES);
    size = made_file(file, &w);
    TIDE_CHECK(tide_vorbis_open(file, size, &why) == NULL);
    TIDE_CHECK(strcmp(why, "a codebook's lengths are cut short") == 0);
    // But the limit is the file's codebooks' together: one of two entries, then that many
    w = (bit_writer){0};
    put_bits(&w, 1, 8); // Two codebooks
    put_codebook(&w, 1, 2);
    put_bits(&w, 0, 1); // Lengths listed,
    put_bits(&w, 0, 1); // not sparse:
    put_bits(&w, 0, 5); // 1 bit
    put_bits(&w, 0, 5); // and 1 bit
    put_bits(&w, 0, 4); // No vectors
    put_codebook(&w, 1, TIDE_VORBIS_MAX_ENTRIES);
    size = made_file(file, &w);
    TIDE_CHECK(tide_vorbis_open(file, size, &why) == NULL);
    TIDE_CHECK(strcmp(why, "the file's codebooks have more entries in all than a file's may (TIDE_VORBIS_MAX_ENTRIES)") == 0);
    // Vectors of more numbers than there may be: 128 entries of 7 bits, each of 32769 dimensions
    w = (bit_writer){0};
    put_bits(&w, 0, 8);
    put_codebook(&w, 32769, 128);
    put_bits(&w, 1, 1);   // Lengths in order,
    put_bits(&w, 6, 5);   // from 7 bits:
    put_bits(&w, 128, 8); // all 128 of them
    put_bits(&w, 2, 4);   // Vectors of lookup type 2
    put_bits(&w, 0, 32);  // Their minimum,
    put_bits(&w, 0, 32);  // delta,
    put_bits(&w, 0, 5);   // bits a value and no sequence
    size = made_file(file, &w);
    TIDE_CHECK(128u * 32769u > TIDE_VORBIS_MAX_VALUES);
    TIDE_CHECK(tide_vorbis_open(file, size, &why) == NULL);
    TIDE_CHECK(strcmp(why, "the file's codebooks' vectors have more numbers in all than a file's may (TIDE_VORBIS_MAX_VALUES)") == 0);
}

// A file can say it holds any number of frames: a whole decode makes room by
// what its packets can give, never by what it says (on the web half a billion
// stereo floats were 4 GiB, which wrapped its size_t to almost nothing)
TIDE_TEST(vorbis_decode_sizes_by_its_packets_not_its_word)
{
    const reference r = ref(1);
    tide_sound honest;
    TIDE_REQUIRE(tide_vorbis_decode(r.ogg, r.ogg_size, &honest));
    uint8_t *lying = malloc(r.ogg_size);
    memcpy(lying, r.ogg, r.ogg_size);
    // Its last page's granule as 2^29 frames, and its rate as the most 32 bits say
    size_t last = 0;
    for (size_t at = 0; at + 4 <= r.ogg_size; at++)
        if (memcmp(lying + at, "OggS", 4) == 0) last = at;
    for (int i = 0; i < 8; i++) lying[last + 6 + i] = (uint8_t)((UINT64_C(1) << 29) >> (8 * i));
    TIDE_REQUIRE(memcmp(lying + 28, "\x01vorbis", 7) == 0);
    memset(lying + 40, 0xFF, 4);
    fix_crcs(lying, r.ogg_size);
    tide_vorbis *f = tide_vorbis_open(lying, r.ogg_size, NULL);
    TIDE_REQUIRE(f != NULL);
    TIDE_CHECK(tide_vorbis_frames(f) == (UINT64_C(1) << 29) && tide_vorbis_rate(f) == UINT32_MAX);
    tide_vorbis_close(f);
    tide_sound s;
    TIDE_REQUIRE(tide_vorbis_decode(lying, r.ogg_size, &s));
    // Every block the packets have, past the 2400 the honest file stops at, and those the same
    TIDE_CHECK(s.frames > honest.frames && s.frames < 4000u && s.channels == 1 && s.rate == UINT32_MAX);
    TIDE_CHECK(memcmp(s.samples, honest.samples, (size_t)honest.frames * sizeof(float)) == 0);
    tide_sound_free(&s);
    tide_sound_free(&honest);
    free(lying);
}
