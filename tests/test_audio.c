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
