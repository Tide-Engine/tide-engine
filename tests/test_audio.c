#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/audio.h"
#include "tide/jobs.h"
#include "tide/pack.h"
#include "tide/sha256.h"
#include "tide/wav.h"
#include "tide_test.h"

// Exercise preparation's jobs path without platform threads or a device.
static void prepare_jobs(void *self, void (*work)(void *, uint32_t), void *context)
{
    uint32_t *calls = self;
    (*calls)++;
    for (uint32_t i = 0; i < 3; i++) work(context, i);
}

TIDE_TEST(audio_prepare_keeps_entries_when_the_clip_cache_grows)
{
    tide_clips_free();
    uint32_t calls = 0;
    const tide_jobs jobs = {.self = &calls, .threads = 3, .run = prepare_jobs};
    tide_clip kept = {0};
    float kept_sample = 0;
    for (uint32_t batch = 0; batch < 2; batch++) {
        tide_pack_builder builder = {0};
        float expected[40];
        for (uint32_t i = 0; i < 40; i++) {
            const float sample = (float)(batch * 40u + i + 1u) / 128.0f;
            const float stereo[4] = {sample, 0, sample, 0};
            size_t size;
            uint8_t *wav = tide_wav_encode(stereo, 2, 48000, &size);
            TIDE_REQUIRE(wav != NULL);
            tide_sound decoded;
            TIDE_REQUIRE(tide_wav_decode(wav, size, &decoded));
            expected[i] = decoded.samples[0];
            tide_sound_free(&decoded);
            uint8_t content[32];
            tide_sha256_of(wav, size, content);
            const uint32_t at = tide_pack_clip(&builder, content, wav, (uint32_t)size);
            char name[32];
            snprintf(name, sizeof name, "sound%u", i);
            tide_pack_name(&builder, name, TIDE_PACK_CLIP, at, (uint32_t)size, 0);
            // Another declaration of the same content must prepare it once.
            snprintf(name, sizeof name, "alias%u", i);
            tide_pack_name(&builder, name, TIDE_PACK_CLIP, at, (uint32_t)size, 0);
            free(wav);
        }
        size_t size;
        uint8_t *bytes = tide_pack_end(&builder, &size);
        TIDE_REQUIRE(tide_pack_load(bytes, size));
        free(bytes);
        tide_clips_prepare(batch == 0 ? NULL : &jobs);
        for (uint32_t i = 0; i < 40; i++) {
            char name[32];
            snprintf(name, sizeof name, "sound%u", i);
            const tide_clip_ref ref = tide_pack_clip_of(tide_pack_find(name, TIDE_PACK_CLIP));
            const uint8_t *source;
            uint32_t source_size;
            uint64_t version;
            TIDE_REQUIRE(tide_pack_clip_find(ref.name, &source, &source_size, &version));
            TIDE_REQUIRE(source_size == 52);
            // Preparation must own the decoded samples already. Change only
            // the valid source's PCM to silence to expose a deferred decode.
            memset((void *)(uintptr_t)(source + 44), 0, 8);
            const tide_clip clip = tide_clip_of(ref);
            TIDE_REQUIRE(clip.samples != NULL && clip.frames == 2 && clip.channels == 2 && clip.rate == 48000);
            TIDE_CHECK(clip.samples[0] == expected[i] && clip.samples[2] == expected[i]);
            snprintf(name, sizeof name, "alias%u", i);
            const tide_clip alias = tide_clip_of(tide_pack_clip_of(tide_pack_find(name, TIDE_PACK_CLIP)));
            TIDE_CHECK(alias.samples == clip.samples && alias.id == clip.id);
            if (batch == 0 && i == 0) kept = clip, kept_sample = expected[i];
        }
        TIDE_CHECK(kept.samples[0] == kept_sample); // Active voices retain the prior pack's samples.
        tide_clips_prepare(batch == 0 ? NULL : &jobs);
        TIDE_CHECK(calls == batch); // Repeated preparation has nothing to decode.
    }
    tide_pack_unload();
    TIDE_CHECK(kept.samples[0] == kept_sample);
    tide_clips_free();
}

extern const unsigned char tide_ref_low_ogg[];
extern const unsigned int tide_ref_low_ogg_size;

// Each Ogg page of a file its checksum again, after a test changed its bytes
static void fix_ogg_crcs(uint8_t *b, const size_t size)
{
    for (size_t at = 0; at + 27u <= size;) {
        size_t body = 0;
        for (uint32_t i = 0; i < b[at + 26]; i++) body += b[at + 27 + i];
        const size_t page = 27u + b[at + 26] + body;
        memset(b + at + 22, 0, 4);
        uint32_t crc = 0;
        for (size_t i = 0; i < page; i++) {
            crc ^= (uint32_t)b[at + i] << 24;
            for (int k = 0; k < 8; k++) crc = (crc & 0x80000000u) ? (crc << 1) ^ 0x04c11db7u : crc << 1;
        }
        for (int i = 0; i < 4; i++) b[at + 22 + i] = (uint8_t)(crc >> (8 * i));
        at += page;
    }
}

// The test's file, low.ogg (mono, 8 kHz, 2400 frames), with what its headers say changed
static uint8_t *lying_ogg(const uint64_t granule, const uint32_t rate, const uint8_t channels)
{
    const size_t size = tide_ref_low_ogg_size;
    uint8_t *b = malloc(size);
    memcpy(b, tide_ref_low_ogg, size);
    size_t last = 0;
    for (size_t at = 0; at + 4 <= size; at++)
        if (memcmp(b + at, "OggS", 4) == 0) last = at;
    for (int i = 0; i < 8; i++) b[last + 6 + i] = (uint8_t)(granule >> (8 * i)); // The last page's granule
    b[39] = channels; // The identification header, in the first page's one packet
    for (int i = 0; i < 4; i++) b[40 + i] = (uint8_t)(rate >> (8 * i));
    fix_ogg_crcs(b, size);
    return b;
}

static void add_clip(tide_pack_builder *builder, const char *name, const uint8_t *file, const uint32_t size)
{
    uint8_t content[32];
    tide_sha256_of(file, size, content);
    const uint32_t at = tide_pack_clip(builder, content, file, size);
    tide_pack_name(builder, name, TIDE_PACK_CLIP, at, size, 0);
}

static tide_clip clip_named(const char *name) { return tide_clip_of(tide_pack_clip_of(tide_pack_find(name, TIDE_PACK_CLIP))); }

// A Vorbis clip's file says how many frames it holds, at what rate, in how
// many channels, and a few kilobytes can say 2^31 frames at 4 GHz: whole, that
// asked for 8 GiB (on the web a size_t that wrapped, and a reader writing past
// it). Preparation sizes a clip by what its packets give and streams past the
// limits, and the samples are the honest file's.
TIDE_TEST(audio_prepare_sizes_a_vorbis_clip_by_its_packets_not_its_word)
{
    tide_clips_free();
    const uint32_t size = tide_ref_low_ogg_size;
    uint8_t *lying = lying_ogg(UINT64_C(1) << 31, UINT32_MAX, 1);
    uint8_t *wide = lying_ogg(UINT64_C(1) << 40, UINT32_MAX, 255); // 255 channels: no Vorbis file the engine opens
    for (int streamed = 0; streamed < 2; streamed++) {
        tide_clips_whole_seconds = streamed ? 0 : 10;
        tide_pack_builder builder = {0};
        add_clip(&builder, "honest", tide_ref_low_ogg, size);
        add_clip(&builder, "lying", lying, size);
        add_clip(&builder, "wide", wide, size);
        size_t pack_size;
        uint8_t *bytes = tide_pack_end(&builder, &pack_size);
        TIDE_REQUIRE(tide_pack_load(bytes, pack_size));
        free(bytes);
        tide_clips_prepare(NULL);
        const tide_clip honest = clip_named("honest"), clip = clip_named("lying"), none = clip_named("wide");
        TIDE_CHECK(none.samples == NULL && none.stream == NULL && none.frames == 0);
        TIDE_CHECK(clip.channels == 1 && clip.rate == UINT32_MAX);
        // Every block its packets have: past the 2400 frames the honest file stops at, far short of 2^31
        TIDE_CHECK(clip.frames > honest.frames && clip.frames < 4000u);
        if (!streamed) {
            TIDE_REQUIRE(clip.samples != NULL && honest.samples != NULL && clip.stream == NULL);
            TIDE_CHECK(memcmp(clip.samples, honest.samples, (size_t)honest.frames * sizeof(float)) == 0);
        } else {
            TIDE_CHECK(clip.samples == NULL && clip.stream != NULL && honest.stream != NULL);
        }
        tide_pack_unload();
        tide_clips_free();
    }
    tide_clips_whole_seconds = 10;
    free(lying);
    free(wide);
}
