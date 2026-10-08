// The mixer (platform/src/mixer.c) and the WAV writer, with no device: what
// frames say plays as the same bytes on every platform.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio_in.h"
#include "audio_out.h"
#include "mixer.h"
#include "tide/net.h"
#include "tide/vorbis.h"
#include "tide/wav.h"
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

// An echo: what its voice played ECHO_FRAMES frames ago, added at half. Its
// delay line is a second of stereo, 384 KB, far more than a voice holds itself,
// so it's memory the mixer makes for the voice. `blocks` counts, and the last
// float of the line is written every block: all of it is the effect's.
#define ECHO_FRAMES (4u * BLOCK)
#define ECHO_LINE 48000u
typedef struct echo_state {
    uint32_t blocks;
    uint32_t at;
    float line[2 * ECHO_LINE];
} echo_state;
static uint32_t echo_blocks_seen;
static void echo(float *samples, void *state, const void *knobs)
{
    (void)knobs;
    echo_state *s = state;
    for (uint32_t i = 0; i < BLOCK; i++) {
        const uint32_t from = (s->at + ECHO_LINE - ECHO_FRAMES) % ECHO_LINE;
        for (uint32_t c = 0; c < 2; c++) {
            s->line[2 * s->at + c] = samples[2 * i + c];
            samples[2 * i + c] += s->line[2 * from + c] * 0.5f;
        }
        s->at = (s->at + 1) % ECHO_LINE;
    }
    s->line[2 * ECHO_LINE - 1] += 0.0f;
    echo_blocks_seen = ++s->blocks;
}

// One block of 1s, then silence
static float burst_samples[2 * BLOCK];
static tide_clip burst(void)
{
    for (uint32_t i = 0; i < 2 * BLOCK; i++) burst_samples[i] = 1.0f;
    return (tide_clip){.samples = burst_samples, .frames = BLOCK, .channels = 2, .rate = TIDE_AUDIO_RATE, .id = 5};
}

static void play(tide_audio_frame *f, const tide_voice *v, float *out)
{
    tide_audio_reset(f);
    if (v) tide_audio_voice(f, v);
    tide_mixer_submit(f);
    tide_mixer_render(out, BLOCK);
}

TIDE_TEST(audio_a_big_state_is_memory_of_the_voices_own)
{
    fresh();
    TIDE_REQUIRE(sizeof(echo_state) > TIDE_AUDIO_STATE_SMALL && sizeof(echo_state) <= TIDE_AUDIO_STATE_BYTES);
    TIDE_CHECK(tide_mixer_states() == 0);
    tide_audio_frame f = {0};
    float out[SAMPLES];
    tide_voice v = {.id = 21, .clip = burst(), .playing = true, .volume = 1.0f, .pitch = 1.0f,
                    .effect = {.fn = echo, .id = 200, .state_size = sizeof(echo_state)}};
    // The burst, three blocks of nothing, and then its echo: the line is kept between blocks
    play(&f, &v, out);
    TIDE_CHECK(near(out[0], 1.0f) && near(out[SAMPLES - 1], 1.0f) && echo_blocks_seen == 1);
    TIDE_CHECK(tide_mixer_states() == 1);
    for (int block = 1; block < 4; block++) {
        play(&f, &v, out);
        TIDE_CHECK(silent(out, SAMPLES));
    }
    play(&f, &v, out);
    TIDE_CHECK(near(out[0], 0.5f) && near(out[SAMPLES - 1], 0.5f) && echo_blocks_seen == 5);
    TIDE_CHECK(tide_mixer_states() == 1);
    // A second voice with the same effect has a line of its own, and the first one's goes on
    tide_voice other = v;
    other.id = 22;
    tide_audio_reset(&f);
    tide_audio_voice(&f, &v);
    tide_audio_voice(&f, &other);
    tide_mixer_submit(&f);
    tide_mixer_render(out, BLOCK);
    TIDE_CHECK(near(out[0], 1.0f) && tide_mixer_states() == 2); // The second one's burst, with no echo of the first's in it
    for (int block = 1; block < 5; block++) {
        tide_audio_reset(&f);
        tide_audio_voice(&f, &v);
        tide_audio_voice(&f, &other);
        tide_mixer_submit(&f);
        tide_mixer_render(out, BLOCK);
        TIDE_CHECK(block == 4 ? near(out[0], 0.5f) : silent(out, SAMPLES)); // Its echo, four blocks after its own burst
    }
    tide_audio_free(&f);
}

TIDE_TEST(audio_a_big_state_starts_over_and_is_freed_when_its_voice_stops)
{
    fresh();
    tide_audio_frame f = {0};
    float out[SAMPLES];
    tide_voice v = {.id = 23, .clip = burst(), .loop = true, .playing = true, .volume = 1.0f, .pitch = 1.0f,
                    .effect = {.fn = echo, .id = 200, .state_size = sizeof(echo_state)}};
    play(&f, &v, out);
    play(&f, &v, out);
    TIDE_CHECK(echo_blocks_seen == 2 && tide_mixer_states() == 1);
    // A frame that doesn't name it: it fades out through its effect, with the state it had, and is gone
    play(&f, NULL, out);
    TIDE_CHECK(echo_blocks_seen == 3);
    TIDE_CHECK(tide_mixer_states() == 1); // The renderer had it until that block was done
    play(&f, NULL, out);
    TIDE_CHECK(echo_blocks_seen == 3 && tide_mixer_states() == 0); // ...which the next frame knows
    // Named again: another state, from zeros
    play(&f, &v, out);
    TIDE_CHECK(echo_blocks_seen == 1 && tide_mixer_states() == 1);
    // Stopped and started: the same
    v.playing = false;
    play(&f, &v, out);
    TIDE_CHECK(echo_blocks_seen == 2); // Its fade
    v.playing = true;
    play(&f, &v, out);
    TIDE_CHECK(echo_blocks_seen == 1);
    play(&f, &v, out);
    TIDE_CHECK(echo_blocks_seen == 2 && tide_mixer_states() == 1); // The one before it has been freed
    // Other code starts it over, a small state in the voice itself, and back
    half_knobs k = {.add = 0.0f};
    tide_voice small = v;
    small.effect = (tide_audio_effect){.fn = half_tell, .id = 100, .state_size = sizeof(half_state), .knobs_size = sizeof k, .knobs = &k};
    play(&f, &small, out);
    TIDE_CHECK(half_blocks_seen == 1);
    play(&f, &small, out);
    TIDE_CHECK(half_blocks_seen == 2 && tide_mixer_states() == 0);
    play(&f, &v, out);
    TIDE_CHECK(echo_blocks_seen == 1 && tide_mixer_states() == 1);
    v.effect.id = 201; // The same code under another name
    play(&f, &v, out);
    TIDE_CHECK(echo_blocks_seen == 1);
    play(&f, &v, out);
    TIDE_CHECK(echo_blocks_seen == 2 && tide_mixer_states() == 1);
    // A forget frees everything at once
    tide_mixer_forget();
    TIDE_CHECK(tide_mixer_states() == 0);
    tide_audio_free(&f);
}

TIDE_TEST(audio_a_bus_has_a_big_state_too)
{
    fresh();
    tide_audio_frame f = {0};
    float out[SAMPLES];
    const tide_voice v = {.id = 24, .clip = burst(), .playing = true, .volume = 1.0f, .pitch = 1.0f, .bus = 3};
    const tide_audio_effect e = {.fn = echo, .id = 200, .state_size = sizeof(echo_state)};
    for (int block = 0; block < 5; block++) {
        tide_audio_reset(&f);
        tide_audio_voice(&f, &v);
        tide_audio_set_bus_effect(&f, 3, &e);
        tide_mixer_submit(&f);
        tide_mixer_render(out, BLOCK);
        TIDE_CHECK(block == 0 ? near(out[0], 1.0f) : block == 4 ? near(out[0], 0.5f) : silent(out, SAMPLES));
        TIDE_CHECK(echo_blocks_seen == (uint32_t)block + 1 && tide_mixer_states() == 1);
    }
    // A frame that doesn't set it lets the state go, and the next that does starts from zeros
    play(&f, &v, out);
    play(&f, &v, out);
    TIDE_CHECK(tide_mixer_states() == 0);
    tide_audio_reset(&f);
    tide_audio_voice(&f, &v);
    tide_audio_set_bus_effect(&f, 3, &e);
    tide_mixer_submit(&f);
    tide_mixer_render(out, BLOCK);
    TIDE_CHECK(echo_blocks_seen == 1 && tide_mixer_states() == 1);
    tide_audio_free(&f);
}

TIDE_TEST(audio_big_states_nobody_renders_are_freed_as_their_voices_stop)
{
    fresh();
    tide_audio_frame f = {0};
    float out[SAMPLES];
    tide_voice v = {.id = 25, .clip = burst(), .loop = true, .playing = true, .volume = 1.0f, .pitch = 1.0f,
                    .effect = {.fn = echo, .id = 200, .state_size = sizeof(echo_state)}};
    // Frames with no block rendered between them: a machine with no device, or one that stalled
    for (int frame = 0; frame < 4; frame++) {
        tide_audio_reset(&f);
        tide_audio_voice(&f, &v);
        tide_mixer_submit(&f);
        TIDE_CHECK(tide_mixer_states() == 1);
    }
    tide_audio_reset(&f);
    tide_mixer_submit(&f);
    TIDE_CHECK(tide_mixer_states() == 0); // The renderer never had it, so nothing waits for it
    // Voices that come and go with nothing rendered hold nothing
    for (uint64_t id = 100; id < 140; id++) {
        v.id = id;
        tide_audio_reset(&f);
        tide_audio_voice(&f, &v);
        tide_mixer_submit(&f);
        TIDE_CHECK(tide_mixer_states() == 1);
    }
    // Once a block is rendered, the renderer has the last one, and it's kept until a block after a frame without it
    tide_mixer_render(out, BLOCK);
    tide_audio_reset(&f);
    tide_mixer_submit(&f);
    TIDE_CHECK(tide_mixer_states() == 1);
    tide_audio_reset(&f);
    tide_mixer_submit(&f);
    TIDE_CHECK(tide_mixer_states() == 1); // Still no block since
    tide_mixer_render(out, BLOCK);
    tide_audio_reset(&f);
    tide_mixer_submit(&f);
    TIDE_CHECK(tide_mixer_states() == 0);
    // Two frames between two blocks, the first without the voice: the renderer never saw it stop, and the state is another all the same
    v.id = 26;
    play(&f, &v, out);
    play(&f, &v, out);
    TIDE_CHECK(echo_blocks_seen == 2);
    tide_audio_reset(&f);
    tide_mixer_submit(&f);
    play(&f, &v, out);
    TIDE_CHECK(echo_blocks_seen == 1);
    play(&f, &v, out);
    TIDE_CHECK(echo_blocks_seen == 2 && tide_mixer_states() == 1);
    tide_audio_free(&f);
}

TIDE_TEST(audio_states_have_their_sizes)
{
    fresh();
    tide_audio_frame f = {0};
    float out[SAMPLES];
    half_knobs k = {.add = 0.25f};
    // All a voice holds itself, and a byte more
    tide_voice v = {.id = 27, .clip = burst(), .loop = true, .playing = true, .volume = 1.0f, .pitch = 1.0f,
                    .effect = {.fn = half_tell, .id = 300, .state_size = TIDE_AUDIO_STATE_SMALL, .knobs_size = sizeof k, .knobs = &k}};
    play(&f, &v, out);
    play(&f, &v, out);
    TIDE_CHECK(half_blocks_seen == 2 && tide_mixer_states() == 0);
    v.effect.id = 301;
    v.effect.state_size = TIDE_AUDIO_STATE_SMALL + 1;
    play(&f, &v, out);
    play(&f, &v, out);
    TIDE_CHECK(half_blocks_seen == 2 && tide_mixer_states() == 1 && near(out[0], 0.75f));
    // The most there is
    v.effect.id = 302;
    v.effect.state_size = TIDE_AUDIO_STATE_BYTES;
    play(&f, &v, out);
    TIDE_CHECK(half_blocks_seen == 1 && near(out[0], 0.75f));
    // More than that is no effect: its code would write past what it's given
    v.effect.id = 303;
    v.effect.state_size = TIDE_AUDIO_STATE_BYTES + 1;
    half_blocks_seen = 0;
    play(&f, &v, out);
    TIDE_CHECK(half_blocks_seen == 0 && near(out[0], 1.0f));
    play(&f, &v, out);
    TIDE_CHECK(tide_mixer_states() == 0);
    // A one-shot has no big state, so it plays without such an effect; with a small one, it has it
    fresh();
    tide_audio_reset(&f);
    tide_voice shot = {.clip = burst(), .playing = true, .volume = 1.0f, .pitch = 1.0f,
                       .effect = {.fn = half_tell, .id = 304, .state_size = TIDE_AUDIO_STATE_SMALL + 1, .knobs_size = sizeof k, .knobs = &k}};
    tide_audio_voice(&f, &shot);
    tide_mixer_submit(&f);
    tide_mixer_render(out, BLOCK);
    TIDE_CHECK(half_blocks_seen == 0 && near(out[0], 1.0f) && tide_mixer_states() == 0);
    shot.effect.state_size = sizeof(half_state);
    tide_audio_reset(&f);
    tide_audio_voice(&f, &shot);
    tide_mixer_submit(&f);
    tide_mixer_render(out, BLOCK);
    TIDE_CHECK(half_blocks_seen == 1 && near(out[0], 0.75f));
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
    uint8_t *wav = tide_wav_encode(stereo, 4, TIDE_AUDIO_RATE, &size);
    TIDE_REQUIRE(wav != NULL);
    TIDE_CHECK(size == 44 + 16);
    // The samples as 16 bits, little-endian, after the 44-byte header
    const int16_t s3 = (int16_t)(wav[44 + 6] | (wav[44 + 7] << 8));
    TIDE_CHECK(s3 == 16384); // 0.5 * 32767 = 16383.5, rounded away from zero
    tide_sound back;
    TIDE_REQUIRE(tide_wav_decode(wav, size, &back));
    TIDE_CHECK(back.channels == 2 && back.rate == TIDE_AUDIO_RATE && back.frames == 4);
    TIDE_CHECK(back.samples[0] == 0.0f && near(back.samples[1], 32767.0f / 32768.0f) && near(back.samples[2], -32767.0f / 32768.0f));
    TIDE_CHECK(near(back.samples[4], 32767.0f / 32768.0f) && near(back.samples[5], -32767.0f / 32768.0f)); // Clamped
    TIDE_CHECK(back.samples[6] == 0.0f && back.samples[7] == 0.0f);
    tide_sound_free(&back);
    TIDE_CHECK(!tide_wav_decode(wav, 20, &back)); // Cut short
    free(wav);
    uint8_t *empty = tide_wav_encode(NULL, 0, TIDE_AUDIO_RATE, &size);
    TIDE_REQUIRE(empty != NULL);
    TIDE_CHECK(size == 44 && tide_wav_decode(empty, size, &back) && back.frames == 0);
    tide_sound_free(&back);
    free(empty);
}

TIDE_TEST(audio_a_device_is_filled_at_its_rate_and_channels)
{
    // A ramp playing at the mixer's rate comes out a frame late, as the pull
    // interpolates between the last two frames; at half the rate every other
    // frame; mono averaged; six channels the two and silence
    fresh();
    tide_audio_frame f = {0};
    tide_audio_reset(&f);
    tide_audio_voice(&f, &(tide_voice){.id = 3, .clip = ramp(), .loop = true, .playing = true, .volume = 1.0f, .pitch = 1.0f});
    tide_mixer_submit(&f);
    float out[6 * 300];
    tide_audio_out_fill(out, 300, TIDE_AUDIO_RATE, 2);
    TIDE_CHECK(out[0] == 0.0f && out[1] == 0.0f); // Before the mixer's first frame
    for (uint32_t i = 1; i < 300; i++) {
        TIDE_CHECK(out[2 * i] == ramp_samples[2 * (i - 1)]);
        TIDE_CHECK(out[2 * i + 1] == ramp_samples[2 * (i - 1) + 1]);
    }
    // Half the rate: the mixer's frames 299, 301, 303... (one late, then every other)
    tide_audio_out_fill(out, 100, TIDE_AUDIO_RATE / 2, 2);
    TIDE_CHECK(out[0] == ramp_samples[2 * 299] && out[2] == ramp_samples[2 * 301] && out[2 * 99] == ramp_samples[2 * 497]);
    // Mono: the two averaged, which for the ramp is always a half
    tide_audio_out_fill(out, 10, TIDE_AUDIO_RATE, 1);
    for (uint32_t i = 0; i < 10; i++) TIDE_CHECK(near(out[i], 0.5f));
    // Six channels: left, right, then silence
    tide_audio_out_fill(out, 4, TIDE_AUDIO_RATE, 6);
    for (uint32_t i = 0; i < 4; i++) {
        TIDE_CHECK(out[6 * i] + out[6 * i + 1] > 0.99f && out[6 * i] + out[6 * i + 1] < 1.01f);
        TIDE_CHECK(out[6 * i + 2] == 0.0f && out[6 * i + 5] == 0.0f);
    }
    tide_audio_free(&f);
}

extern const unsigned char tide_ref_noise_ogg[];
extern const unsigned int tide_ref_noise_ogg_size;

TIDE_TEST(audio_a_streamed_clip_plays_as_its_decode_does_and_loops)
{
    // Stereo noise at the mixer's rate: streamed at pitch 1 it's the decode's own
    // samples, frame for frame, and a loop starts the file over at its granule
    tide_sound whole;
    TIDE_REQUIRE(tide_vorbis_decode(tide_ref_noise_ogg, tide_ref_noise_ogg_size, &whole));
    TIDE_REQUIRE(whole.frames == 28800 && whole.channels == 2 && whole.rate == TIDE_AUDIO_RATE);
    tide_vorbis *file = tide_vorbis_open(tide_ref_noise_ogg, tide_ref_noise_ogg_size, NULL);
    TIDE_REQUIRE(file != NULL);
    const tide_clip clip = {.stream = file, .frames = 28800, .channels = 2, .rate = TIDE_AUDIO_RATE, .id = 77};
    fresh();
    tide_audio_frame f = {0};
    tide_audio_reset(&f);
    tide_audio_voice(&f, &(tide_voice){.id = 9, .clip = clip, .loop = true, .playing = true, .volume = 1.0f, .pitch = 1.0f});
    tide_mixer_submit(&f);
    const uint32_t frames = 28800 + 3 * BLOCK;
    float *out = calloc((size_t)frames * 2, sizeof(float));
    tide_mixer_render(out, frames);
    uint32_t wrong = 0;
    for (uint32_t i = 0; i < frames; i++) {
        const uint32_t j = i % 28800u; // Looped
        if (out[2 * i] != whole.samples[2 * j] || out[2 * i + 1] != whole.samples[2 * j + 1]) wrong++;
    }
    TIDE_CHECK(wrong == 0);
    TIDE_CHECK(tide_mixer_voices() == 1);
    // Not looping, as a one-shot: it ends at the file's end, and the voice is gone
    fresh();
    tide_audio_reset(&f);
    tide_audio_play(&f, clip, 1.0f, 1.0f);
    tide_mixer_submit(&f);
    tide_mixer_render(out, frames);
    wrong = 0;
    for (uint32_t i = 0; i < 28800; i++) {
        if (out[2 * i] != whole.samples[2 * i] || out[2 * i + 1] != whole.samples[2 * i + 1]) wrong++;
    }
    TIDE_CHECK(wrong == 0);
    TIDE_CHECK(silent(out + 2 * (28800 + BLOCK), 2 * BLOCK)); // Silence from the block after the end
    TIDE_CHECK(tide_mixer_voices() == 0);
    // At another pitch it goes through the same interpolation a decoded clip's samples do
    const tide_clip decoded = {.samples = whole.samples, .frames = whole.frames, .channels = 2, .rate = TIDE_AUDIO_RATE, .id = 78};
    float *a = calloc(4 * SAMPLES, sizeof(float)), *b = calloc(4 * SAMPLES, sizeof(float));
    fresh();
    tide_audio_reset(&f);
    tide_audio_voice(&f, &(tide_voice){.id = 9, .clip = clip, .playing = true, .volume = 1.0f, .pitch = 1.37f});
    tide_mixer_submit(&f);
    tide_mixer_render(a, 4 * BLOCK);
    fresh();
    tide_audio_reset(&f);
    tide_audio_voice(&f, &(tide_voice){.id = 9, .clip = decoded, .playing = true, .volume = 1.0f, .pitch = 1.37f});
    tide_mixer_submit(&f);
    tide_mixer_render(b, 4 * BLOCK);
    wrong = 0;
    for (uint32_t i = 0; i < 4 * SAMPLES; i++)
        if (fabsf(a[i] - b[i]) > 1e-6f) wrong++;
    TIDE_CHECK(wrong == 0);
    // A forget lets the stream's reader go; the file is the engine's to close after
    fresh();
    free(a);
    free(b);
    free(out);
    tide_audio_free(&f);
    tide_vorbis_close(file);
    tide_sound_free(&whole);
}

TIDE_TEST(audio_the_microphone_plays_what_the_device_captured)
{
    fresh();
    const tide_clip mic = {.microphone = true, .channels = 1, .rate = TIDE_AUDIO_RATE, .id = TIDE_CLIP_MICROPHONE_NAME,
                           .name = TIDE_CLIP_MICROPHONE_NAME};
    tide_audio_frame f = {0};
    tide_audio_reset(&f);
    TIDE_CHECK(!f.microphone);
    tide_audio_voice(&f, &(tide_voice){.id = 4, .clip = mic, .playing = true, .volume = 1.0f, .pitch = 1.0f});
    TIDE_CHECK(f.microphone); // The platform keeps the device open for it
    tide_mixer_submit(&f);
    float out[SAMPLES * 4];
    // Nothing captured yet: silence, with the voice there
    tide_mixer_render(out, 2 * BLOCK);
    TIDE_CHECK(silent(out, 2 * SAMPLES) && tide_mixer_voices() == 1);
    // A ramp through the device's path at 24 kHz, stereo: averaged to one
    // channel and resampled to 48 kHz, each frame as it came and one between
    // each two, which plays once the ring holds four blocks
    static float device[2 * 600];
    for (uint32_t i = 0; i < 600; i++) device[2 * i] = device[2 * i + 1] = (float)i / 600.0f;
    tide_audio_in_push(device, 600, 24000, 2);
    tide_mixer_render(out, 4 * BLOCK);
    uint32_t wrong = 0;
    for (uint32_t k = 0; k < 4 * BLOCK; k++) {
        const float expected = ((float)(k / 2) + (k & 1 ? 0.5f : 0.0f)) / 600.0f;
        if (!near(out[2 * k], expected) || !near(out[2 * k + 1], expected)) wrong++;
    }
    TIDE_CHECK(wrong == 0);
    // Run dry (174 frames left of 1198): silence, and it waits for a prime again
    tide_mixer_render(out, BLOCK);
    TIDE_CHECK(silent(out, SAMPLES));
    // Two voices of it play the same frames, through their own volumes
    fresh();
    tide_audio_reset(&f);
    tide_audio_voice(&f, &(tide_voice){.id = 4, .clip = mic, .playing = true, .volume = 0.5f, .pitch = 1.0f});
    tide_audio_voice(&f, &(tide_voice){.id = 5, .clip = mic, .playing = true, .volume = 0.25f, .pitch = 7.0f}); // Pitch doesn't apply
    tide_mixer_submit(&f);
    static float level[1100];
    for (uint32_t i = 0; i < 1100; i++) level[i] = 0.4f;
    tide_mixer_capture(level, 1100);
    tide_mixer_render(out, BLOCK);
    wrong = 0;
    for (uint32_t i = 0; i < SAMPLES; i++)
        if (!near(out[i], 0.4f * 0.5f + 0.4f * 0.25f)) wrong++;
    TIDE_CHECK(wrong == 0);
    // Far behind (the device ran on while nothing rendered): the newest frames play, a prime back from the head
    static float late[4000];
    for (uint32_t i = 0; i < 4000; i++) late[i] = i < 2900 ? 0.1f : 0.9f;
    tide_mixer_capture(late, 4000);
    tide_mixer_render(out, BLOCK);
    wrong = 0;
    for (uint32_t i = 0; i < SAMPLES; i++)
        if (!near(out[i], 0.9f * 0.75f)) wrong++;
    TIDE_CHECK(wrong == 0);
    // A one-shot of the microphone records nothing, and a voice of it that isn't playing asks for no device
    tide_audio_reset(&f);
    tide_audio_play(&f, mic, 1.0f, 1.0f);
    tide_audio_play_at(&f, mic, tide_f3(1, 2, 3), 1.0f, 1.0f);
    TIDE_CHECK(f.voice_count == 0 && !f.microphone);
    tide_audio_voice(&f, &(tide_voice){.id = 4, .clip = mic, .playing = false, .volume = 1.0f, .pitch = 1.0f});
    TIDE_CHECK(f.voice_count == 1 && !f.microphone);
    // A forget empties the ring: what was captured before it isn't heard after
    tide_mixer_capture(level, 1100);
    fresh();
    tide_audio_reset(&f);
    tide_audio_voice(&f, &(tide_voice){.id = 4, .clip = mic, .playing = true, .volume = 1.0f, .pitch = 1.0f});
    tide_mixer_submit(&f);
    tide_mixer_render(out, BLOCK);
    TIDE_CHECK(silent(out, SAMPLES));
    fresh();
    tide_audio_free(&f);
}
