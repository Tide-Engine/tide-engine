#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/audio.h"
#include "tide/pack.h"
#include "tide_test.h"

// Audio declarations (effects.tide): what a frame records of a Sound's and a
// bus's effect, and what the recorded code does to a block when it's called
// as the mixer calls it.

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_audio_frame audio;

static void frame(void)
{
    tide_audio_reset(&audio);
    tide_draw_reset(&draw);
    static tide_devices devices;
    tide_gui_begin(&gui, &devices, tide_f2(800, 600), NULL);
    tide_local_audio(&local, &audio);
    tide_frame(&world, NULL, 1.0f, NULL, &local, &draw, &gui);
    tide_gui_end(&gui, &draw);
}

static bool near(const float a, const float b) { return a - b < 1e-6f && b - a < 1e-6f; }

TIDE_TEST(effects_run_tide_code_on_a_block_with_state_and_knobs)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, 1.0f / 60.0f);
    tide_local_init(&local);
    local.Mix.gain = 3.0f;
    frame();
    TIDE_REQUIRE(audio.voice_count == 2);
    const tide_voice *track = audio.voices[0].clip.samples ? &audio.voices[0] : &audio.voices[1];
    const tide_voice *synth = track == &audio.voices[0] ? &audio.voices[1] : &audio.voices[0];
    // The track's effect: Scale, with its knobs copied and its state's size
    TIDE_REQUIRE(track->effect.fn != NULL);
    TIDE_CHECK(track->effect.id != 0 && track->effect.state_size == sizeof(Counter) && track->effect.knobs_size == sizeof(Knobs));
    const Knobs *knobs = tide_audio_knobs(&audio, track->knobs_at, track->effect.knobs_size);
    TIDE_REQUIRE(knobs != NULL);
    TIDE_CHECK(knobs->gain == 3.0f && knobs->add == 0.25f);
    // Called as the mixer calls it: the block scaled and offset, and the state counting
    float block[TIDE_AUDIO_SAMPLES];
    for (uint32_t i = 0; i < TIDE_AUDIO_SAMPLES; i++) block[i] = 1.0f;
    Counter state = {0};
    track->effect.fn(block, &state, knobs);
    TIDE_CHECK(near(block[0], 3.25f) && near(block[TIDE_AUDIO_SAMPLES - 1], 3.25f));
    TIDE_CHECK(state.blocks == 1 && near(state.last, 3.25f));
    track->effect.fn(block, &state, knobs);
    TIDE_CHECK(near(block[0], 10.0f) && state.blocks == 2);
    // The synth: a source with no clip and no knobs, which calls a function
    TIDE_REQUIRE(synth->effect.fn != NULL && synth->clip.samples == NULL);
    TIDE_CHECK(synth->effect.knobs_size == 0 && synth->effect.state_size == sizeof(Counter) && synth->effect.id != track->effect.id);
    memset(block, 0, sizeof block);
    Counter synth_state = {0};
    synth->effect.fn(block, &synth_state, NULL);
    TIDE_CHECK(block[0] == 0.25f && block[2 * 49] == 0.25f && block[2 * 50] == -0.25f && synth_state.blocks == 1);
    // The bus's effect, with other knobs
    TIDE_REQUIRE(audio.bus_effect[1].fn == track->effect.fn);
    const Knobs *bus_knobs = tide_audio_knobs(&audio, audio.bus_knobs_at[1], audio.bus_effect[1].knobs_size);
    TIDE_CHECK(bus_knobs->gain == 2.0f && bus_knobs->add == 0.0f);
    // The master's: noise, which rolls. Its generator is kept after its own state, on the next 8 bytes of what
    // the mixer keeps for it, so each voice has a sequence of its own, which goes on from block to block
    TIDE_REQUIRE(audio.bus_effect[0].fn != NULL && audio.bus_effect[0].state_size == sizeof(Counter) + 8);
    static _Alignas(8) uint8_t voice_a[sizeof(Counter) + 8], voice_b[sizeof(Counter) + 8];
    static float first[TIDE_AUDIO_SAMPLES], second[TIDE_AUDIO_SAMPLES], beside[TIDE_AUDIO_SAMPLES];
    audio.bus_effect[0].fn(first, voice_a, NULL);
    audio.bus_effect[0].fn(beside, voice_b, NULL);
    audio.bus_effect[0].fn(second, voice_a, NULL);
    int loud = 0, same_voice = 0, same_block = 0;
    for (uint32_t i = 0; i < TIDE_AUDIO_SAMPLES; i++) {
        TIDE_CHECK(first[i] >= -0.5f && first[i] < 0.5f);
        loud += first[i] != 0.0f;
        same_voice += first[i] == beside[i];
        same_block += first[i] == second[i];
    }
    TIDE_CHECK(loud > (int)TIDE_AUDIO_SAMPLES - 4 && same_voice < 4 && same_block < 4);
    TIDE_CHECK(((const Counter *)(const void *)voice_a)->blocks == 2 && ((const Counter *)(const void *)voice_b)->blocks == 1);
    // The frame after: the same effect, other knobs
    local.Mix.gain = 0.5f;
    frame();
    TIDE_REQUIRE(audio.voice_count == 2);
    const tide_voice *again = audio.voices[0].clip.samples ? &audio.voices[0] : &audio.voices[1];
    TIDE_CHECK(again->effect.id == track->effect.id && ((const Knobs *)tide_audio_knobs(&audio, again->knobs_at, sizeof(Knobs)))->gain == 0.5f);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_draw_free(&draw);
    tide_audio_free(&audio);
    tide_clips_free();
    tide_pack_unload();
}

// A state of the bytes an effect says it keeps, between bytes that have to
// stay as they were: what the mixer makes for a voice whose effect's state is
// more than it holds itself.
#define GUARD 64u
static uint8_t *guarded_state(const uint32_t size)
{
    uint8_t *bytes = calloc(1, (size_t)GUARD + size + GUARD);
    memset(bytes, 0xA5, GUARD);
    memset(bytes + GUARD + size, 0xA5, GUARD);
    return bytes;
}

static bool guards_stand(const uint8_t *bytes, const uint32_t size)
{
    for (uint32_t i = 0; i < GUARD; i++) {
        if (bytes[i] != 0xA5 || bytes[GUARD + size + i] != 0xA5) return false;
    }
    return true;
}

TIDE_TEST(effects_keep_a_delay_line_far_longer_than_any_other_array)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, 1.0f / 60.0f);
    tide_local_init(&local);
    local.Mix.echoes = true;
    frame();
    TIDE_REQUIRE(audio.voice_count == 4);
    const tide_voice *far = NULL, *crackle = NULL;
    for (uint32_t i = 0; i < audio.voice_count; i++) {
        if (audio.voices[i].effect.state_size == sizeof(Tail)) far = &audio.voices[i];
        if (audio.voices[i].effect.state_size == sizeof(Tail) + 4 + 8) crackle = &audio.voices[i]; // To the next 8 bytes, then its generator
    }
    // The echo's state is its struct's bytes: 384 KB, which the mixer makes for the voice
    TIDE_REQUIRE(far && crackle && sizeof(Tail) == 4 + 96000 * sizeof(float));
    TIDE_CHECK(far->effect.state_size > TIDE_AUDIO_STATE_SMALL && far->effect.state_size <= TIDE_AUDIO_STATE_BYTES);
    const Delay *delay = tide_audio_knobs(&audio, far->knobs_at, far->effect.knobs_size);
    TIDE_REQUIRE(delay && delay->frames == 512 && delay->wet == 0.5f);
    uint8_t *state = guarded_state(far->effect.state_size);
    float block[TIDE_AUDIO_SAMPLES];
    // A block of 1s, a block of nothing, and the first block again at half: 512 frames later
    for (uint32_t i = 0; i < TIDE_AUDIO_SAMPLES; i++) block[i] = 1.0f;
    far->effect.fn(block, state + GUARD, delay);
    TIDE_CHECK(near(block[0], 1.0f) && near(block[TIDE_AUDIO_SAMPLES - 1], 1.0f));
    memset(block, 0, sizeof block);
    far->effect.fn(block, state + GUARD, delay);
    TIDE_CHECK(block[0] == 0.0f && block[TIDE_AUDIO_SAMPLES - 1] == 0.0f);
    memset(block, 0, sizeof block);
    far->effect.fn(block, state + GUARD, delay);
    TIDE_CHECK(near(block[0], 0.5f) && near(block[TIDE_AUDIO_SAMPLES - 1], 0.5f));
    const Tail *tail = (const Tail *)(const void *)(state + GUARD);
    TIDE_CHECK(tail->at == 3 * 256 && tail->line.at[0] == 1.0f && tail->line.at[2 * 256] == 0.0f && tail->line.at[95999] == 0.0f);
    TIDE_CHECK(guards_stand(state, far->effect.state_size)); // Indexes past its ends wrote nothing
    // Through a second and more of blocks: around the line's end and back to its start
    for (int n = 0; n < 200; n++) {
        for (uint32_t i = 0; i < TIDE_AUDIO_SAMPLES; i++) block[i] = (float)n;
        far->effect.fn(block, state + GUARD, delay);
        if (n >= 2) TIDE_CHECK(near(block[0], (float)n + (float)(n - 2) * 0.5f));
    }
    TIDE_CHECK(tail->at == (203 * 256) % 48000 && guards_stand(state, far->effect.state_size));
    free(state);
    // Code that rolls beside a long state: the generator is after it, in what the effect says it keeps
    uint8_t *rolled = guarded_state(crackle->effect.state_size);
    crackle->effect.fn(block, rolled + GUARD, NULL);
    const float first = block[0];
    crackle->effect.fn(block, rolled + GUARD, NULL);
    TIDE_CHECK(first != block[0] && first >= -1.0f && first < 1.0f);
    int64_t generator;
    memcpy(&generator, rolled + GUARD + crackle->effect.state_size - 8, sizeof generator);
    TIDE_CHECK(generator != 0 && guards_stand(rolled, crackle->effect.state_size));
    free(rolled);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_draw_free(&draw);
    tide_audio_free(&audio);
    tide_clips_free();
    tide_pack_unload();
}
