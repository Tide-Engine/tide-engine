#include "tide/audio.h"

#include <stdlib.h>
#include <string.h>

#include "tide/page.h"

static uint32_t grown(const uint32_t capacity, const uint32_t least)
{
    uint32_t c = capacity ? capacity : 8;
    while (c < least) c *= 2;
    return c;
}

static tide_voice *add_voice(tide_audio_frame *f)
{
    if (f->voice_count == f->voice_capacity) {
        const uint32_t c = grown(f->voice_capacity, f->voice_count + 1);
        f->voices = tide_realloc(f->voices, f->voice_capacity * sizeof *f->voices, c * sizeof *f->voices);
        f->voice_capacity = c;
    }
    tide_voice *v = &f->voices[f->voice_count++];
    memset(v, 0, sizeof *v);
    return v;
}

// Copies an effect's knobs into the frame and says where they went. An effect
// with no code has no knobs, whatever it says.
static uint32_t copy_knobs(tide_audio_frame *f, tide_audio_effect *e)
{
    if (!e->fn) {
        *e = (tide_audio_effect){0};
        return 0;
    }
    if (e->state_size > TIDE_AUDIO_STATE_BYTES) e->state_size = TIDE_AUDIO_STATE_BYTES;
    if (!e->knobs) e->knobs_size = 0;
    if (e->knobs_size > TIDE_AUDIO_KNOBS_BYTES) e->knobs_size = TIDE_AUDIO_KNOBS_BYTES;
    const uint32_t at = f->knobs_used;
    if (e->knobs_size) {
        if (f->knobs_used + e->knobs_size > f->knobs_capacity) {
            const uint32_t c = grown(f->knobs_capacity, f->knobs_used + e->knobs_size);
            f->knobs = tide_realloc(f->knobs, f->knobs_capacity, c);
            f->knobs_capacity = c;
        }
        memcpy(f->knobs + at, e->knobs, e->knobs_size);
        f->knobs_used += e->knobs_size;
    }
    e->knobs = NULL; // The frame's copy is what's read from here on
    return at;
}

void tide_audio_reset(tide_audio_frame *f)
{
    f->voice_count = 0;
    f->knobs_used = 0;
    f->listener_set = false;
    f->falloff_near = f->falloff_far = 0.0f;
    f->forget = false;
    for (uint32_t b = 0; b < TIDE_AUDIO_BUSES; b++) {
        f->bus_volume[b] = 1.0f;
        f->bus_effect[b] = (tide_audio_effect){0};
        f->bus_knobs_at[b] = 0;
    }
    f->frame++;
}

void tide_audio_free(tide_audio_frame *f)
{
    free(f->voices);
    free(f->knobs);
    memset(f, 0, sizeof *f);
}

void tide_audio_play(tide_audio_frame *f, const tide_clip clip, const float volume, const float pitch)
{
    tide_voice *v = add_voice(f);
    v->clip = clip;
    v->playing = true;
    v->volume = volume;
    v->pitch = pitch;
}

void tide_audio_play_at(tide_audio_frame *f, const tide_clip clip, const tide_float3 position, const float volume,
                        const float pitch)
{
    tide_voice *v = add_voice(f);
    v->clip = clip;
    v->playing = true;
    v->spatial = true;
    v->position = position;
    v->volume = volume;
    v->pitch = pitch;
}

void tide_audio_voice(tide_audio_frame *f, const tide_voice *voice)
{
    tide_voice *v = add_voice(f);
    *v = *voice;
    if (v->bus >= TIDE_AUDIO_BUSES) v->bus = 0;
    v->knobs_at = copy_knobs(f, &v->effect);
}

void tide_audio_set_listener(tide_audio_frame *f, const tide_float3 position, const tide_quaternion rotation)
{
    f->listener_position = position;
    f->listener_rotation = rotation;
    f->listener_set = true;
}

void tide_audio_set_falloff(tide_audio_frame *f, const float near, const float far)
{
    f->falloff_near = near;
    f->falloff_far = far;
}

void tide_audio_set_bus_volume(tide_audio_frame *f, const uint32_t bus, const float volume)
{
    if (bus < TIDE_AUDIO_BUSES) f->bus_volume[bus] = volume;
}

void tide_audio_set_bus_effect(tide_audio_frame *f, const uint32_t bus, const tide_audio_effect *effect)
{
    if (bus >= TIDE_AUDIO_BUSES) return;
    f->bus_effect[bus] = *effect;
    f->bus_knobs_at[bus] = copy_knobs(f, &f->bus_effect[bus]);
}
