#include "tide/audio.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "tide/jobs.h"
#include "tide/pack.h"
#include "tide/vorbis.h"
#include "tide/page.h"
#include "tide/wav.h"

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
// with no code has no knobs, whatever it says, and one whose state is more
// than there can be is no effect: its code would write past what it's given.
static uint32_t copy_knobs(tide_audio_frame *f, tide_audio_effect *e)
{
    if (!e->fn || e->state_size > TIDE_AUDIO_STATE_BYTES) {
        *e = (tide_audio_effect){0};
        return 0;
    }
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
    f->microphone = false;
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
    if (!f || clip.microphone) return; // The microphone plays through a Sound, which says when it stops
    tide_voice *v = add_voice(f);
    v->clip = clip;
    v->playing = true;
    v->volume = volume;
    v->pitch = pitch;
}

void tide_audio_play_at(tide_audio_frame *f, const tide_clip clip, const tide_float3 position, const float volume,
                        const float pitch)
{
    if (!f || clip.microphone) return;
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
    if (!f) return;
    tide_voice *v = add_voice(f);
    *v = *voice;
    if (v->bus >= TIDE_AUDIO_BUSES) v->bus = 0;
    v->knobs_at = copy_knobs(f, &v->effect);
    if (v->clip.microphone && v->playing) f->microphone = true;
}

void tide_audio_set_listener(tide_audio_frame *f, const tide_float3 position, const tide_quaternion rotation)
{
    if (!f) return;
    f->listener_position = position;
    f->listener_rotation = rotation;
    f->listener_set = true;
}

void tide_audio_set_falloff(tide_audio_frame *f, const float near, const float far)
{
    if (!f) return;
    f->falloff_near = near;
    f->falloff_far = far;
}

void tide_audio_set_bus_volume(tide_audio_frame *f, const uint32_t bus, const float volume)
{
    if (f && bus < TIDE_AUDIO_BUSES) f->bus_volume[bus] = volume;
}

void tide_audio_set_bus_effect(tide_audio_frame *f, const uint32_t bus, const tide_audio_effect *effect)
{
    if (!f || bus >= TIDE_AUDIO_BUSES) return;
    f->bus_effect[bus] = *effect;
    f->bus_knobs_at[bus] = copy_knobs(f, &f->bus_effect[bus]);
}

// ---------------------------------------------------------------------------
// The pack's clips, decoded

// A file's samples, by what's in the file: two declarations of one file are
// one entry, and a file edited under `tide run` is another beside it. An Ogg
// Vorbis file longer than tide_clips_whole_seconds is kept to stream instead.
typedef struct decoded_clip {
    uint64_t version;
    tide_sound sound;
    tide_vorbis *stream;
} decoded_clip;

static struct {
    decoded_clip *items;
    uint32_t count, capacity;
} clips;

uint32_t tide_clips_whole_seconds = 10;

// Limits: what a Vorbis clip's file can ask of its decode at load. A clip
// decoded whole is sized by the frames its packets give
// (tide_vorbis_frames_given), never what the file says it holds, which can
// be any 64 bits; and the rate its seconds are counted at can be any 32. So
// it's whole only up to tide_clips_whole_seconds at this rate at the most,
// and this many samples in all. Each is only its number: past either the
// clip streams instead, which gives the same samples a block at a time.

// The rate a whole clip's seconds are counted at, at the most: 10 seconds of
// stereo floats at it is 30 MiB.
#define TIDE_CLIP_WHOLE_MAX_RATE 384000u

// The most samples (frames times channels) a clip decoded whole holds, 512
// MiB of floats, for a tide_clips_whole_seconds set high.
#define TIDE_CLIP_WHOLE_MAX_SAMPLES (1u << 27)

// Decodes the file's bytes into the entry, whichever kind they are
static void decode_clip(decoded_clip *d, const uint8_t *bytes, const uint32_t size)
{
    if (tide_wav_decode(bytes, size, &d->sound)) return;
    tide_vorbis *v = tide_vorbis_open(bytes, size, NULL);
    if (!v) return; // Checked as the game was built: a pack by hand
    const uint64_t given = tide_vorbis_frames_given(v);
    const uint32_t ch = tide_vorbis_channels(v);
    const uint32_t rate = tide_vorbis_rate(v) < TIDE_CLIP_WHOLE_MAX_RATE ? tide_vorbis_rate(v) : TIDE_CLIP_WHOLE_MAX_RATE;
    if (given <= (uint64_t)tide_clips_whole_seconds * rate && given * ch <= TIDE_CLIP_WHOLE_MAX_SAMPLES) {
        tide_vorbis_reader *r = tide_vorbis_reader_open(v);
        const uint32_t most = (uint32_t)given;
        float *samples = tide_alloc(((size_t)most + 1u) * ch * sizeof *samples);
        uint32_t frames = 0;
        for (;;) {
            // A read writes no more frames than it's asked for, and this asks for no more than are left
            const uint32_t got = tide_vorbis_read(r, samples + (size_t)frames * ch, most - frames);
            if (!got || got > most - frames) break;
            frames += got;
        }
        d->sound = (tide_sound){.samples = samples, .frames = frames, .channels = ch, .rate = tide_vorbis_rate(v)};
        tide_vorbis_reader_close(r);
        tide_vorbis_close(v);
        return;
    }
    d->stream = v;
}

static decoded_clip *clip_entry(const uint64_t version)
{
    for (uint32_t i = 0; i < clips.count; i++) {
        if (clips.items[i].version == version) return &clips.items[i];
    }
    return NULL;
}

static decoded_clip *add_clip_entry(const uint64_t version)
{
    if (clips.count == clips.capacity) {
        const uint32_t c = clips.capacity ? clips.capacity * 2 : 16;
        clips.items = tide_realloc(clips.items, clips.capacity * sizeof *clips.items, c * sizeof *clips.items);
        clips.capacity = c;
    }
    decoded_clip *d = &clips.items[clips.count++];
    *d = (decoded_clip){.version = version};
    return d;
}

static tide_clip clip_from(const decoded_clip *d, const uint64_t name)
{
    if (d->stream) {
        const uint64_t frames = tide_vorbis_frames_given(d->stream); // What its reader gives, not what the file says
        return (tide_clip){.stream = d->stream, .frames = frames > UINT32_MAX ? UINT32_MAX : (uint32_t)frames,
                           .channels = tide_vorbis_channels(d->stream), .rate = tide_vorbis_rate(d->stream), .id = d->version,
                           .name = name};
    }
    return (tide_clip){.samples = d->sound.samples, .frames = d->sound.frames, .channels = d->sound.channels, .rate = d->sound.rate,
                       .id = d->version, .name = name};
}

tide_clip tide_clip_of(const tide_clip_ref ref)
{
    if (ref.name == TIDE_CLIP_MICROPHONE_NAME) {
        return (tide_clip){.microphone = true, .channels = 1, .rate = TIDE_AUDIO_RATE, .id = TIDE_CLIP_MICROPHONE_NAME, .name = ref.name};
    }
    const uint8_t *bytes;
    uint32_t size;
    uint64_t version;
    if (!ref.name || !tide_pack_clip_find(ref.name, &bytes, &size, &version)) return (tide_clip){.name = ref.name};
    decoded_clip *d = clip_entry(version);
    if (!d) {
        d = add_clip_entry(version);
        decode_clip(d, bytes, size);
    }
    return clip_from(d, ref.name);
}

// What tide_clips_prepare has to decode: the pack's clips the cache hasn't got, each file once
typedef struct pending_clip {
    const uint8_t *bytes;
    uint32_t size;
    uint32_t entry; // An index: gathering later clips can move clips.items
} pending_clip;

typedef struct preparing {
    pending_clip *items;
    uint32_t count, capacity;
    _Atomic uint32_t next;
} preparing;

static void gather_clip(const uint8_t *bytes, const uint32_t size, const uint64_t version, void *context)
{
    preparing *p = context;
    if (clip_entry(version)) return;
    if (p->count == p->capacity) {
        const uint32_t c = p->capacity ? p->capacity * 2 : 16;
        p->items = tide_realloc(p->items, p->capacity * sizeof *p->items, c * sizeof *p->items);
        p->capacity = c;
    }
    add_clip_entry(version);
    p->items[p->count++] = (pending_clip){.bytes = bytes, .size = size, .entry = clips.count - 1u};
}

static void prepare_work(void *context, const uint32_t thread)
{
    (void)thread;
    preparing *p = context;
    for (;;) {
        const uint32_t i = atomic_fetch_add_explicit(&p->next, 1u, memory_order_relaxed);
        if (i >= p->count) return;
        decode_clip(&clips.items[p->items[i].entry], p->items[i].bytes, p->items[i].size);
    }
}

void tide_clips_prepare(const tide_jobs *jobs)
{
    preparing p = {0};
    tide_pack_clips_each(gather_clip, &p); // Adds the entries on this thread: the threads only fill them
    if (p.count) {
        if (jobs && jobs->threads > 1 && p.count > 1) {
            jobs->run(jobs->self, prepare_work, &p);
            tide_memory_sync();
        } else {
            prepare_work(&p, 0);
        }
    }
    free(p.items);
}

void tide_clips_free(void)
{
    for (uint32_t i = 0; i < clips.count; i++) {
        tide_sound_free(&clips.items[i].sound);
        tide_vorbis_close(clips.items[i].stream);
    }
    free(clips.items);
    memset(&clips, 0, sizeof clips);
}
