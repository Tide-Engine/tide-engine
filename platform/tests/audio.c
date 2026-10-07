// The mixer (platform/src/mixer.c) and the WAV writer, with no device: what
// frames say plays as the same bytes on every platform.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mixer.h"
#include "tide/net.h"
#include "wav.h"
#include "tide_test.h"

#define BLOCK TIDE_AUDIO_BLOCK
#define SAMPLES TIDE_AUDIO_SAMPLES

// Clips the tests play: a ramp of 1024 stereo frames (left counts up, right
// counts down), and a mono square of 100 frames.
static float ramp_samples[1024 * 2];
static float square_samples[100];

static tide_clip ramp(void)
{
    for (uint32_t i = 0; i < 1024; i++) {
        ramp_samples[2 * i] = (float)i / 1024.0f;
        ramp_samples[2 * i + 1] = 1.0f - (float)i / 1024.0f;
    }
    return (tide_clip){.samples = ramp_samples, .frames = 1024, .channels = 2, .rate = TIDE_AUDIO_RATE, .id = 1};
}

static tide_clip square(const uint32_t rate)
{
    for (uint32_t i = 0; i < 100; i++) square_samples[i] = i < 50 ? 0.5f : -0.5f;
    return (tide_clip){.samples = square_samples, .frames = 100, .channels = 1, .rate = rate, .id = 2};
}

// A mixer that starts over for each test: the mixer is one per process, so a
// test hands it a frame that forgets everything and renders a block for it to take.
static void fresh(void)
{
    tide_mixer_flush(); // Whatever a render of an odd size left of its last block
    tide_audio_frame f = {0};
    tide_audio_reset(&f);
    f.forget = true;
    tide_mixer_submit(&f);
    float out[SAMPLES];
    tide_mixer_render(out, BLOCK);
    tide_audio_reset(&f);
    tide_mixer_submit(&f); // Then a frame of nothing, so the next render starts from silence with no releases pending
    tide_mixer_render(out, BLOCK);
    tide_audio_free(&f);
}

static bool near(const float a, const float b) { return fabsf(a - b) < 1e-5f; }

static bool silent(const float *out, const uint32_t count)
{
    for (uint32_t i = 0; i < count; i++)
        if (out[i] != 0.0f) return false;
    return true;
}

TIDE_TEST(audio_a_one_shot_plays_its_clip_as_it_is)
{
    fresh();
    tide_audio_frame f = {0};
    tide_audio_reset(&f);
    tide_audio_play(&f, ramp(), 1.0f, 1.0f);
    tide_mixer_submit(&f);
    float out[SAMPLES];
    tide_mixer_render(out, BLOCK);
    for (uint32_t i = 0; i < BLOCK; i++) {
        TIDE_CHECK(out[2 * i] == ramp_samples[2 * i]);
        TIDE_CHECK(out[2 * i + 1] == ramp_samples[2 * i + 1]);
    }
    TIDE_CHECK(tide_mixer_voices() == 1);
    // Four blocks are the whole clip; the fifth is silence, and the voice is gone
    for (int b = 0; b < 3; b++) tide_mixer_render(out, BLOCK);
    TIDE_CHECK(out[2 * 255] == ramp_samples[2 * 1023]);
    tide_mixer_render(out, BLOCK);
    TIDE_CHECK(silent(out, SAMPLES));
    TIDE_CHECK(tide_mixer_voices() == 0);
    tide_audio_free(&f);
}

TIDE_TEST(audio_volume_pitch_and_rate_shape_a_clip)
{
    fresh();
    tide_audio_frame f = {0};
    tide_audio_reset(&f);
    tide_audio_play(&f, ramp(), 0.5f, 2.0f); // Half as loud, an octave up: every other sample
    tide_mixer_submit(&f);
    float out[SAMPLES];
    tide_mixer_render(out, BLOCK);
    for (uint32_t i = 0; i < BLOCK; i++) TIDE_CHECK(near(out[2 * i], 0.5f * ramp_samples[2 * (2 * i)]));

    // A mono clip at half the mixer's rate: each sample twice over, blended between, on both channels
    fresh();
    tide_audio_reset(&f);
    tide_audio_play(&f, square(TIDE_AUDIO_RATE / 2), 1.0f, 1.0f);
    tide_mixer_submit(&f);
    tide_mixer_render(out, BLOCK);
    TIDE_CHECK(out[0] == 0.5f && out[1] == 0.5f);
    TIDE_CHECK(out[2 * 98] == 0.5f);    // Frame 49 of the clip
    TIDE_CHECK(near(out[2 * 99], 0.0f)); // Halfway between 49 and 50
    TIDE_CHECK(out[2 * 100] == -0.5f);
    TIDE_CHECK(out[2 * 199] == -0.5f && out[2 * 199 + 1] == -0.5f);
    TIDE_CHECK(silent(out + 2 * 200, SAMPLES - 2 * 200)); // 100 frames at half rate are 200 of the mixer's
    tide_audio_free(&f);
}

TIDE_TEST(audio_a_voice_goes_on_loops_and_fades_when_it_isnt_named)
{
    fresh();
    tide_audio_frame f = {0};
    float out[SAMPLES];
    tide_voice v = {.id = 7, .clip = square(TIDE_AUDIO_RATE), .loop = true, .playing = true, .volume = 1.0f, .pitch = 1.0f};
    for (int frame = 0; frame < 3; frame++) {
        tide_audio_reset(&f);
        tide_audio_voice(&f, &v);
        tide_mixer_submit(&f);
        tide_mixer_render(out, BLOCK);
        // 256 frames of a 100-frame loop: the square wave goes round
        for (uint32_t i = 0; i < BLOCK; i++) {
            const uint32_t at = (frame * BLOCK + i) % 100;
            TIDE_CHECK(out[2 * i] == (at < 50 ? 0.5f : -0.5f));
        }
    }
    TIDE_CHECK(tide_mixer_voices() == 1);
    // Not named: it fades out over the next block, from 1 to 0, and is gone after
    tide_audio_reset(&f);
    tide_mixer_submit(&f);
    tide_mixer_render(out, BLOCK);
    TIDE_CHECK(fabsf(out[0]) < 0.5f && fabsf(out[0]) > 0.49f);
    TIDE_CHECK(out[2 * 255] == 0.0f);
    tide_mixer_render(out, BLOCK);
    TIDE_CHECK(silent(out, SAMPLES));
    TIDE_CHECK(tide_mixer_voices() == 0);

    // A voice that doesn't loop ends and stays silent while it's named, and starts over when playing goes false then true
    fresh();
    v.loop = false;
    v.clip = ramp();
    for (int frame = 0; frame < 6; frame++) {
        tide_audio_reset(&f);
        tide_audio_voice(&f, &v);
        tide_mixer_submit(&f);
        tide_mixer_render(out, BLOCK);
    }
    TIDE_CHECK(silent(out, SAMPLES));
    TIDE_CHECK(tide_mixer_voices() == 1); // Still there, silent
    v.playing = false;
    tide_audio_reset(&f);
    tide_audio_voice(&f, &v);
    tide_mixer_submit(&f);
    tide_mixer_render(out, BLOCK);
    v.playing = true;
    tide_audio_reset(&f);
    tide_audio_voice(&f, &v);
    tide_mixer_submit(&f);
    tide_mixer_render(out, BLOCK);
    TIDE_CHECK(out[2] == ramp_samples[2]); // From its start again
    tide_audio_free(&f);
}

TIDE_TEST(audio_a_volume_change_ramps_over_a_block)
{
    fresh();
    tide_audio_frame f = {0};
    float out[SAMPLES];
    float ones[64];
    for (int i = 0; i < 64; i++) ones[i] = 1.0f;
    const tide_clip dc = {.samples = ones, .frames = 32, .channels = 2, .rate = TIDE_AUDIO_RATE, .id = 3};
    tide_voice v = {.id = 9, .clip = dc, .loop = true, .playing = true, .volume = 1.0f, .pitch = 1.0f};
    tide_audio_reset(&f);
    tide_audio_voice(&f, &v);
    tide_mixer_submit(&f);
    tide_mixer_render(out, BLOCK);
    TIDE_CHECK(out[0] == 1.0f && out[SAMPLES - 1] == 1.0f);
    v.volume = 0.0f;
    tide_audio_reset(&f);
    tide_audio_voice(&f, &v);
    tide_mixer_submit(&f);
    tide_mixer_render(out, BLOCK);
    TIDE_CHECK(near(out[0], 1.0f - 1.0f / BLOCK));
    TIDE_CHECK(near(out[2 * 127], 0.5f));
    TIDE_CHECK(near(out[2 * 255], 0.0f));
    tide_mixer_render(out, BLOCK);
    TIDE_CHECK(silent(out, SAMPLES));
    TIDE_CHECK(tide_mixer_voices() == 1); // At volume 0, not released: still named
    tide_audio_free(&f);
}

TIDE_TEST(audio_spatial_sounds_pan_and_fall_off)
{
    fresh();
    tide_audio_frame f = {0};
    float out[SAMPLES];
    float ones[64];
    for (int i = 0; i < 64; i++) ones[i] = 1.0f;
    const tide_clip dc = {.samples = ones, .frames = 32, .channels = 2, .rate = TIDE_AUDIO_RATE, .id = 3};
    tide_voice v = {.id = 11, .clip = dc, .loop = true, .playing = true, .spatial = true, .volume = 1.0f, .pitch = 1.0f};
    // A frame says its listener and falloff each time, as every frame starts from the defaults
    const tide_quaternion facing_z = tide_identity_q();
    const tide_quaternion facing_x = tide_axisangle_q((tide_float3){0, 1, 0}, 1.5707963f);
#define HEAR(where, rotation, blocks)                                                                                  \
    do {                                                                                                               \
        tide_audio_reset(&f);                                                                                          \
        tide_audio_set_listener(&f, (tide_float3){0, 0, 0}, rotation);                                                 \
        tide_audio_set_falloff(&f, 2.0f, 10.0f);                                                                       \
        v.position = where;                                                                                            \
        tide_audio_voice(&f, &v);                                                                                      \
        tide_mixer_submit(&f);                                                                                         \
        for (int b = 0; b < (blocks); b++) tide_mixer_render(out, BLOCK); /* The first block ramps there */           \
    } while (0)
    // At the listener's right, within the near distance: all right, nothing left
    HEAR(((tide_float3){1.0f, 0.0f, 0.0f}), facing_z, 1);
    TIDE_CHECK(near(out[0], 0.0f) && near(out[1], 1.0f));
    // In front: equal power each side
    HEAR(((tide_float3){0.0f, 0.0f, 1.0f}), facing_z, 2);
    TIDE_CHECK(near(out[0], tide_sqrt_f(0.5f)) && near(out[1], tide_sqrt_f(0.5f)));
    // At the far distance: silence; halfway (6 of 2..10): near/d * (far-d)/(far-near) = 2/6 * 4/8
    HEAR(((tide_float3){0.0f, 0.0f, 10.0f}), facing_z, 2);
    TIDE_CHECK(silent(out, SAMPLES));
    HEAR(((tide_float3){0.0f, 0.0f, 6.0f}), facing_z, 2);
    TIDE_CHECK(near(out[0], (2.0f / 6.0f) * 0.5f * tide_sqrt_f(0.5f)));
    // The listener turned to face +x: a sound at +z is now at its left
    HEAR(((tide_float3){0.0f, 0.0f, 1.0f}), facing_x, 2);
    TIDE_CHECK(near(out[0], 1.0f) && near(out[1], 0.0f));
    // A frame that says no listener and no falloff: the origin facing +z, at its volume within 1 and silent past 50
    tide_audio_reset(&f);
    v.position = (tide_float3){0.0f, 0.0f, 60.0f};
    tide_audio_voice(&f, &v);
    tide_mixer_submit(&f);
    tide_mixer_render(out, BLOCK);
    tide_mixer_render(out, BLOCK);
    TIDE_CHECK(silent(out, SAMPLES));
    tide_audio_reset(&f);
    v.position = (tide_float3){-0.5f, 0.0f, 0.0f};
    tide_audio_voice(&f, &v);
    tide_mixer_submit(&f);
    tide_mixer_render(out, BLOCK);
    tide_mixer_render(out, BLOCK);
    TIDE_CHECK(near(out[0], 1.0f) && near(out[1], 0.0f));
#undef HEAR
    tide_audio_free(&f);
}

// An effect that halves what it's given, counting the blocks it saw in its
// state, and adding its knob.
typedef struct half_state { uint32_t blocks; } half_state;
typedef struct half_knobs { float add; } half_knobs;
static void half(float *samples, void *state, const void *knobs)
{
    half_state *s = state;
    const half_knobs *k = knobs;
    s->blocks++;
    for (uint32_t i = 0; i < SAMPLES; i++) samples[i] = samples[i] * 0.5f + (k ? k->add : 0.0f);
}
static uint32_t half_blocks_seen;
static void half_tell(float *samples, void *state, const void *knobs)
{
    half(samples, state, knobs);
    half_blocks_seen = ((half_state *)state)->blocks;
}

TIDE_TEST(audio_effects_run_on_voices_and_buses_with_state_and_knobs)
{
    fresh();
    tide_audio_frame f = {0};
    float out[SAMPLES];
    float ones[64];
    for (int i = 0; i < 64; i++) ones[i] = 1.0f;
    const tide_clip dc = {.samples = ones, .frames = 32, .channels = 2, .rate = TIDE_AUDIO_RATE, .id = 3};
    half_knobs k = {.add = 0.25f};
    tide_voice v = {.id = 13, .clip = dc, .loop = true, .playing = true, .volume = 1.0f, .pitch = 1.0f,
                    .effect = {.fn = half_tell, .id = 100, .state_size = sizeof(half_state), .knobs_size = sizeof k, .knobs = &k}};
    for (int frame = 0; frame < 3; frame++) {
        tide_audio_reset(&f);
        tide_audio_voice(&f, &v);
        tide_mixer_submit(&f);
        tide_mixer_render(out, BLOCK);
        TIDE_CHECK(near(out[0], 0.75f)); // 1 * 0.5 + 0.25
        TIDE_CHECK(half_blocks_seen == (uint32_t)frame + 1); // The state persists
    }
    // Another knob next frame, and another effect id starts the state over
    k.add = 0.0f;
    v.effect.id = 101;
    tide_audio_reset(&f);
    tide_audio_voice(&f, &v);
    tide_mixer_submit(&f);
    tide_mixer_render(out, BLOCK);
    TIDE_CHECK(near(out[0], 0.5f));
    TIDE_CHECK(half_blocks_seen == 1);
    // A bus effect over its mix, and the bus's volume after it; a voice on another bus is untouched
    tide_audio_reset(&f);
    v.effect = (tide_audio_effect){0};
    v.bus = 2;
    tide_audio_voice(&f, &v);
    tide_voice plain = {.id = 14, .clip = dc, .loop = true, .playing = true, .volume = 1.0f, .pitch = 1.0f, .bus = 3};
    tide_audio_voice(&f, &plain);
    k.add = 0.25f;
    tide_audio_set_bus_effect(&f, 2, &(tide_audio_effect){.fn = half, .id = 7, .state_size = sizeof(half_state), .knobs_size = sizeof k, .knobs = &k});
    tide_audio_set_bus_volume(&f, 2, 0.5f);
    tide_mixer_submit(&f);
    tide_mixer_render(out, BLOCK); // The bus's volume ramps from 1 to 0.5 over this block
    tide_mixer_render(out, BLOCK);
    TIDE_CHECK(near(out[0], 0.75f * 0.5f + 1.0f)); // (1 * 0.5 + 0.25) * 0.5, plus the plain voice
    // A source: a voice with no clip, whose effect makes the sound
    fresh();
    tide_audio_reset(&f);
    tide_voice source = {.id = 15, .playing = true, .volume = 1.0f, .pitch = 1.0f,
                         .effect = {.fn = half, .id = 100, .state_size = sizeof(half_state), .knobs_size = sizeof k, .knobs = &k}};
    tide_audio_voice(&f, &source);
    tide_mixer_submit(&f);
    tide_mixer_render(out, BLOCK);
    TIDE_CHECK(near(out[0], 0.25f) && near(out[SAMPLES - 1], 0.25f));
    TIDE_CHECK(tide_mixer_voices() == 1);
    tide_audio_free(&f);
}

TIDE_TEST(audio_one_shots_from_frames_between_blocks_are_all_played)
{
    fresh();
    tide_audio_frame f = {0};
    float out[SAMPLES];
    float ones[64];
    for (int i = 0; i < 64; i++) ones[i] = 1.0f;
    const tide_clip dc = {.samples = ones, .frames = 32, .channels = 2, .rate = TIDE_AUDIO_RATE, .id = 3};
    // Three frames with no block between: a one-shot by tide_audio_play, and
    // two by a voice with id 0, which loop, so they're still there after the block
    const tide_voice looping = {.clip = dc, .loop = true, .playing = true, .volume = 1.0f, .pitch = 1.0f};
    for (int frame = 0; frame < 3; frame++) {
        tide_audio_reset(&f);
        if (frame == 0) tide_audio_play(&f, dc, 1.0f, 1.0f);
        else tide_audio_voice(&f, &looping);
        tide_mixer_submit(&f);
    }
    tide_mixer_render(out, BLOCK);
    TIDE_CHECK(out[0] == 3.0f);
    TIDE_CHECK(tide_mixer_voices() == 2); // The 32-frame one ended within the block
    TIDE_CHECK(tide_mixer_dropped() == 0);
    // Past the ring's room they're dropped, counted
    tide_audio_reset(&f);
    for (uint32_t i = 0; i < TIDE_MIXER_ONE_SHOTS + 5; i++) tide_audio_voice(&f, &looping);
    tide_mixer_submit(&f);
    TIDE_CHECK(tide_mixer_dropped() == 5);
    tide_mixer_render(out, BLOCK);
    TIDE_CHECK(tide_mixer_voices() == TIDE_MIXER_VOICES); // As many as there are slots: the rest took the quietest's
    tide_audio_free(&f);
}

TIDE_TEST(audio_renders_the_same_in_any_sizes_and_the_same_bytes_everywhere)
{
    // A scene of everything, rendered in 800-frame pieces (a 60th of a second,
    // as a headless run asks) against whole blocks, and hashed
    static float a[4800 * 2], b[4800 * 2];
    for (int way = 0; way < 2; way++) {
        fresh();
        tide_audio_frame f = {0};
        float *out = way == 0 ? a : b;
        uint32_t done = 0;
        tide_voice v = {.id = 21, .clip = square(44100), .loop = true, .playing = true, .spatial = true, .volume = 0.8f, .pitch = 1.1f};
        half_knobs k = {.add = 0.1f};
        for (int frame = 0; frame < 6; frame++) {
            tide_audio_reset(&f);
            tide_audio_set_listener(&f, (tide_float3){0, 0, 0}, tide_axisangle_q((tide_float3){0, 1, 0}, 0.3f * (float)frame));
            v.position = (tide_float3){(float)frame - 2.0f, 0.0f, 3.0f};
            tide_audio_voice(&f, &v);
            if (frame == 1) tide_audio_play_at(&f, ramp(), (tide_float3){-2, 0, 1}, 0.7f, 0.9f);
            if (frame == 2) tide_audio_play(&f, ramp(), 0.3f, 1.7f);
            tide_audio_set_bus_effect(&f, 0, &(tide_audio_effect){.fn = half, .id = 1, .state_size = sizeof(half_state), .knobs_size = sizeof k, .knobs = &k});
            tide_audio_set_bus_volume(&f, 0, 0.9f);
            tide_mixer_submit(&f);
            if (way == 0) {
                tide_mixer_render(out + done * 2, 800);
                done += 800;
            } else {
                // The same frame's worth, but a block at a time, and the odd 32 frames apart
                uint32_t left = 800;
                while (left) {
                    const uint32_t n = left >= BLOCK ? BLOCK : left;
                    tide_mixer_render(out + done * 2, n);
                    done += n;
                    left -= n;
                }
            }
        }
        tide_audio_free(&f);
    }
    TIDE_CHECK(memcmp(a, b, sizeof a) == 0);
    const uint64_t hash = tide_hash(a, sizeof a);
    printf("audio mix hash %016llx\n", (unsigned long long)hash);
    TIDE_CHECK(hash == UINT64_C(0x721523a2f7eebcb3)); // The same on every platform: native, the web, Android
}

TIDE_TEST(audio_wav_holds_the_mix_as_16_bit_pcm)
{
    const float stereo[] = {0.0f, 1.0f, -1.0f, 0.5f, 2.0f, -3.0f, 0.00001f, -0.00001f};
    size_t size = 0;
    uint8_t *wav = tide_wav_encode(stereo, 4, &size);
    TIDE_REQUIRE(wav != NULL);
    TIDE_CHECK(size == 44 + 16);
    uint32_t channels = 0, rate = 0, frames = 0;
    const int16_t *samples = NULL;
    TIDE_REQUIRE(tide_wav_header(wav, size, &channels, &rate, &frames, &samples));
    TIDE_CHECK(channels == 2 && rate == TIDE_AUDIO_RATE && frames == 4);
    TIDE_CHECK(samples[0] == 0 && samples[1] == 32767 && samples[2] == -32767);
    TIDE_CHECK(samples[3] == 16384); // 0.5 * 32767 = 16383.5, rounded away from zero
    TIDE_CHECK(samples[4] == 32767 && samples[5] == -32767); // Clamped
    TIDE_CHECK(samples[6] == 0 && samples[7] == 0);
    TIDE_CHECK(!tide_wav_header(wav, 20, &channels, &rate, &frames, &samples)); // Cut short
    free(wav);
    uint8_t *empty = tide_wav_encode(NULL, 0, &size);
    TIDE_REQUIRE(empty != NULL);
    TIDE_CHECK(size == 44 && tide_wav_header(empty, size, &channels, &rate, &frames, &samples) && frames == 0);
    free(empty);
}
