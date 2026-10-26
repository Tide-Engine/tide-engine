#if !defined(_WIN32) && !defined(__APPLE__)
#define _DEFAULT_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <sys/utime.h>
#else
#include <utime.h>
#endif

#include "asset_files.h"
#include "audio_files.h"
#include "folders.h"
#include "tide/vorbis.h"
#include "tide/wav.h"
#include "tide_test.h"

static uint8_t *fixture(const char *name, size_t *size)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", AUDIO_FIXTURES, name);
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END)) { fclose(file); return NULL; }
    const long length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET)) { fclose(file); return NULL; }
    uint8_t *bytes = malloc(length ? (size_t)length : 1);
    const bool ok = bytes && fread(bytes, 1, (size_t)length, file) == (size_t)length;
    fclose(file);
    if (!ok) { free(bytes); return NULL; }
    *size = (size_t)length;
    return bytes;
}

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }

TIDE_TEST(audio_import_flac_preserves_24_bit_samples_in_float_wav)
{
    size_t size;
    uint8_t *raw = fixture("precise24.flac", &size);
    TIDE_REQUIRE(raw != NULL);
    uint8_t *before = malloc(size);
    TIDE_REQUIRE(before != NULL);
    memcpy(before, raw, size);
    char why[256];
    uint8_t *wav = NULL;
    size_t wav_size = 0;
    TIDE_REQUIRE(audio_file_read(raw, size, &wav, &wav_size, why));
    TIDE_REQUIRE(wav != NULL && wav_size == 58 + 4096 * 2 * 4);
    TIDE_CHECK(memcmp(raw, before, size) == 0);
    TIDE_CHECK(wav[20] == 3 && wav[34] == 32); // IEEE float, without PCM16 quantization.
    tide_sound sound;
    TIDE_REQUIRE(tide_wav_decode(wav, wav_size, &sound));
    TIDE_CHECK(sound.frames == 4096 && sound.channels == 2 && sound.rate == 16000);
    for (int i = 0; i < 4096; i++) {
        const int32_t left = ((i * 73) % 4096 - 2048) * 256 + 17;
        const int32_t right = ((i * 131) % 2048 - 1024) * 1024 - 31;
        TIDE_CHECK(sound.samples[i * 2] == (float)left / 8388608.0f);
        TIDE_CHECK(sound.samples[i * 2 + 1] == (float)right / 8388608.0f);
    }
    uint8_t *again = NULL;
    size_t again_size = 99;
    TIDE_CHECK(audio_file_read(wav, wav_size, &again, &again_size, why));
    TIDE_CHECK(again == NULL && again_size == 0); // Already in the runtime's format.
    TIDE_CHECK(audio_file_read(raw, size, NULL, NULL, why));
    tide_sound_free(&sound);
    free(wav); free(before); free(raw);
}

TIDE_TEST(audio_import_mp3_cbr_vbr_mono_stereo_and_gapless_lengths)
{
    const char *names[] = {"stereo_cbr", "mono_vbr"};
    const uint32_t channels[] = {2, 1}, rates[] = {44100, 22050}, frames[] = {8820, 4410};
    for (unsigned kind = 0; kind < 2; kind++) {
        char name[64];
        snprintf(name, sizeof name, "%s.mp3", names[kind]);
        size_t size;
        uint8_t *raw = fixture(name, &size);
        TIDE_REQUIRE(raw != NULL);
        uint8_t *wav = NULL;
        size_t wav_size;
        char why[256];
        const bool imported = audio_file_read(raw, size, &wav, &wav_size, why);
        if (!imported) fprintf(stderr, "%s: %s\n", name, why);
        TIDE_REQUIRE(imported);
        tide_sound sound;
        TIDE_REQUIRE(tide_wav_decode(wav, wav_size, &sound));
        TIDE_CHECK(sound.channels == channels[kind] && sound.rate == rates[kind] && sound.frames == frames[kind]);
        snprintf(name, sizeof name, "%s.f32", names[kind]);
        size_t reference_size;
        uint8_t *reference = fixture(name, &reference_size);
        TIDE_REQUIRE(reference != NULL && reference_size == (size_t)sound.frames * sound.channels * 4);
        float loudest = 0;
        for (size_t i = 0; i < reference_size / 4; i++) {
            uint32_t bits = le32(reference + i * 4);
            float sample;
            memcpy(&sample, &bits, sizeof sample);
            const float delta = sound.samples[i] - sample;
            TIDE_CHECK(delta > -0.0002f && delta < 0.0002f);
            const float absolute = sound.samples[i] < 0 ? -sound.samples[i] : sound.samples[i];
            if (absolute > loudest) loudest = absolute;
        }
        TIDE_CHECK(loudest > 0.05f);
        tide_sound_free(&sound);
        free(reference); free(wav); free(raw);
    }
}

TIDE_TEST(audio_import_rejects_truncated_tags_frames_and_flac_checksum_damage)
{
    const char *names[] = {"stereo_cbr.mp3", "mono_vbr.mp3", "precise24.flac"};
    for (unsigned kind = 0; kind < 3; kind++) {
        size_t size;
        uint8_t *raw = fixture(names[kind], &size);
        TIDE_REQUIRE(raw != NULL);
        const size_t cuts[] = {4, 9, size / 2, size - 1};
        for (unsigned i = 0; i < sizeof cuts / sizeof cuts[0]; i++) {
            char why[256];
            uint8_t *out = (uint8_t *)(uintptr_t)1;
            size_t out_size = 99;
            TIDE_CHECK(!audio_file_read(raw, cuts[i], &out, &out_size, why));
            TIDE_CHECK(why[0] && out == NULL && out_size == 0);
        }
        if (kind == 2) {
            raw[size - 1] ^= 1; // Last FLAC frame's CRC16.
            char why[256];
            TIDE_CHECK(!audio_file_read(raw, size, NULL, NULL, why));
            TIDE_CHECK(strstr(why, "checksum") != NULL);
        }
        free(raw);
    }
}

TIDE_TEST(audio_import_flac_bounds_declared_length_before_decoded_allocation)
{
    size_t size;
    uint8_t *raw = fixture("precise24.flac", &size);
    TIDE_REQUIRE(raw != NULL);
    // STREAMINFO's low 36 bits are its total sample count.
    raw[21] |= 15;
    memset(raw + 22, 255, 4);
    char why[256];
    TIDE_CHECK(!audio_file_read(raw, size, NULL, NULL, why));
    TIDE_CHECK(strstr(why, "256 MiB") != NULL);
    raw[21] &= 0xf0;
    memset(raw + 22, 0, 4);
    TIDE_CHECK(!audio_file_read(raw, size, NULL, NULL, why));
    TIDE_CHECK(strstr(why, "no declared sample count") != NULL);
    free(raw);
}

static void tiny_wave(uint8_t bytes[48])
{
    memset(bytes, 0, 48);
    memcpy(bytes, "RIFF", 4); put32(bytes + 4, 40); memcpy(bytes + 8, "WAVEfmt ", 8);
    put32(bytes + 16, 16); put16(bytes + 20, 3); put16(bytes + 22, 1); put32(bytes + 24, 48000);
    put32(bytes + 28, 48000 * 4); put16(bytes + 32, 4); put16(bytes + 34, 32);
    memcpy(bytes + 36, "data", 4); put32(bytes + 40, 4); put32(bytes + 44, 0x3e800000); // 0.25f.
}

TIDE_TEST(audio_existing_wav_diagnostics_identify_codec_channels_depth_truncation_and_nonfinite)
{
    uint8_t bytes[48];
    tiny_wave(bytes);
    char why[256];
    TIDE_CHECK(audio_file_read(bytes, sizeof bytes, NULL, NULL, why));
    put16(bytes + 20, 0x11);
    TIDE_CHECK(!audio_file_read(bytes, sizeof bytes, NULL, NULL, why) && strstr(why, "codec 0x0011"));
    tiny_wave(bytes); put16(bytes + 22, 6);
    TIDE_CHECK(!audio_file_read(bytes, sizeof bytes, NULL, NULL, why) && strstr(why, "6 channels"));
    tiny_wave(bytes); put16(bytes + 34, 16);
    TIDE_CHECK(!audio_file_read(bytes, sizeof bytes, NULL, NULL, why) && strstr(why, "float sample width 16"));
    tiny_wave(bytes);
    TIDE_CHECK(!audio_file_read(bytes, sizeof bytes - 1, NULL, NULL, why) && strstr(why, "cut short"));
    put32(bytes + 44, 0x7fc00000);
    TIDE_CHECK(!audio_file_read(bytes, sizeof bytes, NULL, NULL, why) && strstr(why, "nonfinite"));
    tiny_wave(bytes); put32(bytes + 4, 39); put32(bytes + 40, 3);
    TIDE_CHECK(!audio_file_read(bytes, sizeof bytes, NULL, NULL, why) && strstr(why, "partway"));
}

TIDE_TEST(audio_unsupported_containers_identify_opus_aiff_and_big_endian_wave)
{
    uint8_t opus[36] = {0};
    memcpy(opus, "OggS", 4); opus[26] = 1; opus[27] = 8; memcpy(opus + 28, "OpusHead", 8);
    char why[256];
    TIDE_CHECK(!audio_file_read(opus, sizeof opus, NULL, NULL, why) && strstr(why, "Ogg Opus is unsupported"));
    const uint8_t aiff[] = {'F','O','R','M',0,0,0,4,'A','I','F','F'};
    TIDE_CHECK(!audio_file_read(aiff, sizeof aiff, NULL, NULL, why) && strstr(why, "AIFF"));
    const uint8_t rifx[] = {'R','I','F','X',0,0,0,4,'W','A','V','E'};
    TIDE_CHECK(!audio_file_read(rifx, sizeof rifx, NULL, NULL, why) && strstr(why, "RIFX"));
}

static uint32_t ogg_crc(const uint8_t *p, size_t size)
{
    uint32_t crc = 0;
    for (size_t i = 0; i < size; i++) {
        crc ^= (uint32_t)p[i] << 24;
        for (int k = 0; k < 8; k++) crc = (crc & 0x80000000u) ? (crc << 1) ^ 0x04c11db7u : crc << 1;
    }
    return crc;
}

// An Ogg page of one stream, its packets each under 255 bytes, appended at `*size`.
static void ogg_page(uint8_t *out, size_t *size, uint8_t type, const uint8_t *const *packets, const uint32_t *sizes, uint32_t count)
{
    uint8_t *h = out + *size;
    memset(h, 0, 27); memcpy(h, "OggS", 4); h[5] = type; h[14] = 1; h[18] = (uint8_t)(type == 0x02 ? 0 : 1); h[26] = (uint8_t)count;
    size_t at = 27u + count;
    for (uint32_t i = 0; i < count; i++) { h[27 + i] = (uint8_t)sizes[i]; memcpy(h + at, packets[i], sizes[i]); at += sizes[i]; }
    put32(h + 22, ogg_crc(h, at));
    *size += at;
}

TIDE_TEST(audio_ogg_vorbis_codebooks_past_the_limit_are_refused_at_build)
{
    // Mono 8 kHz Vorbis headers whose one codebook says a million entries more than any file's may have:
    // a few bytes asking for a tree of millions of nodes on every player's machine
    uint8_t id[30] = {1, 'v', 'o', 'r', 'b', 'i', 's', 0, 0, 0, 0, 1};
    put32(id + 12, 8000); id[28] = 0x88; id[29] = 1;
    const uint8_t comment[16] = {3, 'v', 'o', 'r', 'b', 'i', 's', 0, 0, 0, 0, 0, 0, 0, 0, 1};
    uint8_t setup[16] = {5, 'v', 'o', 'r', 'b', 'i', 's', 0, 0x42, 0x43, 0x56, 1, 0}; // One codebook: sync, 1 dimension,
    const uint32_t entries = TIDE_VORBIS_MAX_ENTRIES + 1u;
    setup[13] = (uint8_t)entries; setup[14] = (uint8_t)(entries >> 8); setup[15] = (uint8_t)(entries >> 16); // and its entries
    uint8_t file[160];
    size_t size = 0;
    const uint8_t *first[1] = {id}, *second[2] = {comment, setup};
    const uint32_t first_size[1] = {30}, second_size[2] = {16, 16};
    ogg_page(file, &size, 0x02, first, first_size, 1);
    ogg_page(file, &size, 0x04, second, second_size, 2);
    char why[256];
    TIDE_CHECK(!audio_file_read(file, size, NULL, NULL, why));
    TIDE_CHECK(strcmp(why, "Ogg Vorbis: the file's codebooks have more entries in all than a file's may (TIDE_VORBIS_MAX_ENTRIES)") == 0);
}

// Each page's checksum again, after a test changed a file's bytes; gives where the last page starts.
static size_t ogg_recheck(uint8_t *b, size_t size)
{
    size_t last = 0;
    for (size_t at = 0; at + 27u <= size;) {
        size_t body = 0;
        for (uint32_t i = 0; i < b[at + 26]; i++) body += b[at + 27 + i];
        const size_t page = 27u + b[at + 26] + body;
        memset(b + at + 22, 0, 4);
        put32(b + at + 22, ogg_crc(b + at, page));
        last = at;
        at += page;
    }
    return last;
}

TIDE_TEST(audio_ogg_vorbis_is_decoded_once_at_build)
{
    size_t size;
    uint8_t *raw = fixture("../assets/blip.ogg", &size); // Mono, 8 kHz, 2400 frames
    TIDE_REQUIRE(raw != NULL);
    char why[256];
    uint8_t *wav = NULL;
    size_t wav_size = 0;
    TIDE_CHECK(audio_file_read(raw, size, &wav, &wav_size, why) && wav == NULL); // Kept as it is
    // Its last page's granule saying 2^31 frames, which its packets don't give:
    // encoders write granules past their packets, and such a file plays as its
    // packets give, which is what the runtime sizes a clip by
    uint8_t *lying = malloc(size);
    TIDE_REQUIRE(lying != NULL);
    memcpy(lying, raw, size);
    size_t last = ogg_recheck(lying, size);
    put32(lying + last + 6, 0x80000000u);
    ogg_recheck(lying, size);
    TIDE_CHECK(audio_file_read(lying, size, NULL, NULL, why));
    // An audio packet that says it isn't one: a player would skip it, a gap of silence
    memcpy(lying, raw, size);
    TIDE_REQUIRE(!(lying[last + 5] & 0x01u) && lying[last + 26] > 0); // The last page starts a packet
    lying[last + 27u + lying[last + 26]] |= 1u;
    ogg_recheck(lying, size);
    TIDE_CHECK(!audio_file_read(lying, size, NULL, NULL, why));
    TIDE_CHECK(strcmp(why, "Ogg Vorbis audio packet is cut short or corrupt") == 0);
    free(lying);
    free(raw);
}

static bool write_pinned(const char *path, const uint8_t *bytes, size_t size)
{
    FILE *file = fopen(path, "wb");
    if (!file) return false;
    const bool ok = fwrite(bytes, 1, size, file) == size;
    if (fclose(file) || !ok) return false;
#ifdef _WIN32
    struct _utimbuf time = {1700000000, 1700000000};
    return _utime(path, &time) == 0;
#else
    const struct utimbuf time = {1700000000, 1700000000};
    return utime(path, &time) == 0;
#endif
}

static uint32_t clip_frames(const asset_value *value)
{
    tide_sound sound;
    if (!value || value->count != 1 || !tide_wav_decode(value->items[0].data, value->items[0].size, &sound)) return 0;
    const uint32_t frames = sound.frames;
    tide_sound_free(&sound);
    return frames;
}

TIDE_TEST(audio_asset_notification_refreshes_conversion_without_changing_raw_byte_assets)
{
    size_t size;
    uint8_t *raw = fixture("stereo_cbr.mp3", &size);
    TIDE_REQUIRE(raw != NULL);
    // The Info/Xing frame contains the encoder's 12-bit delay and padding.
    // Increasing padding by one is a valid same-size edit with one fewer
    // decoded frame, independently visible in the converted WAV.
    size_t lame = 0;
    for (size_t i = 0; i + 8 < size; i++) {
        if (memcmp(raw + i, "Info", 4) && memcmp(raw + i, "Xing", 4)) continue;
        const uint8_t flags = raw[i + 7];
        lame = i + 8 + ((flags & 1) ? 4 : 0) + ((flags & 2) ? 4 : 0) + ((flags & 4) ? 100 : 0) + ((flags & 8) ? 4 : 0);
        break;
    }
    TIDE_REQUIRE(lame && lame + 24 <= size && raw[lame + 23] < 255);
    char source[1024], path[1024];
    snprintf(source, sizeof source, "%s/main.tide", AUDIO_WORK);
    snprintf(path, sizeof path, "%s/cached.mp3", AUDIO_WORK);
    TIDE_REQUIRE(folder_make(AUDIO_WORK) && write_pinned(path, raw, size));
    asset_files_forget(); diag_reset();
    asset_value *value = asset_resolve(source, str_from("cached.mp3"), ASSET_CLIP, false, false, (loc){0}, NULL);
    TIDE_CHECK(clip_frames(value) == 8820);
    value = asset_resolve(source, str_from("cached.mp3"), ASSET_BYTES, false, false, (loc){0}, NULL);
    TIDE_REQUIRE(value && value->count == 1 && value->items[0].size == size);
    TIDE_CHECK(memcmp(value->items[0].data, raw, size) == 0);
    raw[lame + 23]++;
    TIDE_REQUIRE(write_pinned(path, raw, size));
    value = asset_resolve(source, str_from("cached.mp3"), ASSET_CLIP, false, false, (loc){0}, NULL);
    TIDE_CHECK(clip_frames(value) == 8820); // Deliberately unchanged filesystem metadata.
    asset_files_changed(path);
    value = asset_resolve(source, str_from("cached.mp3"), ASSET_CLIP, false, false, (loc){0}, NULL);
    TIDE_CHECK(clip_frames(value) == 8819);
    value = asset_resolve(source, str_from("cached.mp3"), ASSET_BYTES, false, false, (loc){0}, NULL);
    TIDE_REQUIRE(value && value->count == 1 && value->items[0].size == size);
    TIDE_CHECK(memcmp(value->items[0].data, raw, size) == 0);
    TIDE_CHECK(diag_error_count() == 0);
    asset_files_forget(); arena_reset(); diag_reset();
    TIDE_CHECK(remove(path) == 0);
    free(raw);
}
