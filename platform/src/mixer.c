#include "mixer.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "tide/page.h"
#include "tide/time.h"
#include "tide/vorbis.h"

// ---------------------------------------------------------------------------
// What a frame hands over

// A frame's voices, listener and buses, copied. Three of these: the frame's
// thread fills the one it holds, the renderer reads the one it holds, and
// `pending` holds the third, which is either the newest frame not yet taken
// (PENDING_NEW set) or a slot neither side has.
typedef struct handed {
    tide_voice *voices;
    uint32_t count, capacity;
    uint8_t *knobs;
    uint32_t knobs_used, knobs_capacity;
    tide_float3 listener_position;
    tide_quaternion listener_rotation;
    bool listener_set;
    float falloff_near, falloff_far;
    float bus_volume[TIDE_AUDIO_BUSES];
    tide_audio_effect bus_effect[TIDE_AUDIO_BUSES];
    uint32_t bus_knobs_at[TIDE_AUDIO_BUSES];
    bool forget;
    uint32_t forget_serial; // Which forget it is, for the ask that waits for it
} handed;

#define PENDING_NEW 4u
#define TIDE_MIXER_CAPTURE 16384u                       // The microphone's ring: a third of a second
#define TIDE_MIXER_CAPTURE_PRIME (4u * TIDE_AUDIO_BLOCK) // Blocks the ring holds before the renderer starts taking from it
#define TIDE_MIXER_CAPTURE_LAG (8u * TIDE_AUDIO_BLOCK)   // Behind by more than this, the renderer skips to the newest, a prime back

// A one-shot waiting for the next block, with its effect's knobs.
typedef struct shot {
    tide_voice voice;
    uint8_t knobs[TIDE_AUDIO_KNOBS_BYTES];
} shot;

// ---------------------------------------------------------------------------
// What plays

typedef struct voice {
    uint64_t id;      // 0 for a slot nothing plays in
    bool one_shot;    // Owned by the mixer once started: no frame names it again
    bool named;       // Named by the frame being applied (scratch while applying)
    bool releasing;   // Fading out, then gone
    bool ended;       // Its clip played to its end: silent until the frame starts it over, or stops naming it
    bool playing;     // The frame's `playing` as last said
    tide_clip clip;
    double cursor;   // Frames into the clip; for a stream, how far from frame `a` to frame `b`
    tide_vorbis_reader *reader; // A streamed clip's decoder, made at its first block
    float a[2], b[2];           // A stream's last two frames
    bool drained;               // The stream gave its last frame
    bool spatial, loop;
    tide_float3 position;
    float volume, pitch;
    uint32_t bus;
    float gain_l, gain_r; // As the last block ended: ramped towards the targets over a block
    tide_audio_effect effect;
    uint8_t knobs[TIDE_AUDIO_KNOBS_BYTES];
    uint8_t state[TIDE_AUDIO_STATE_BYTES];
} voice;

typedef struct bus {
    float volume, gain; // The target, and the gain as the last block ended
    tide_audio_effect effect;
    uint8_t knobs[TIDE_AUDIO_KNOBS_BYTES];
    uint8_t state[TIDE_AUDIO_STATE_BYTES];
    bool used; // A voice went into it this block
    float mix[TIDE_AUDIO_SAMPLES];
} bus;

static struct {
    bool made;
    handed slots[3];
    uint32_t writer, reader;      // Which slot each side holds
    _Atomic uint32_t pending;     // The third, with PENDING_NEW when it's a frame to take
    shot *shots;                  // The one-shots' ring
    _Atomic uint32_t shot_head;   // The frame's: where the next one goes
    _Atomic uint32_t shot_tail;   // The renderer's: the next to take
    float *capture;                 // The microphone's ring, TIDE_MIXER_CAPTURE mono frames
    _Atomic uint32_t capture_head;  // The device's: where the next frame goes
    _Atomic uint32_t capture_tail;  // The renderer's: the next to take
    float mic[TIDE_AUDIO_BLOCK];    // The block the microphone's voices play this block
    bool capture_primed;            // The ring had a prime's worth: taking goes on until it runs dry
    _Atomic uint32_t dropped;
    _Atomic uint32_t playing;
    _Atomic bool elsewhere;       // A device's thread renders
    uint32_t forget_asked;        // The frame's: forgets asked so far
    _Atomic uint32_t forget_done; // The renderer's: the last it applied
    voice *voices;
    bus buses[TIDE_AUDIO_BUSES];
    tide_float3 listener_position;
    tide_quaternion listener_rotation;
    float falloff_near, falloff_far;
    float left[TIDE_AUDIO_SAMPLES]; // What's left of the last block rendered
    uint32_t left_from;             // Its first frame not yet given out (TIDE_AUDIO_BLOCK for none)
    float temp[TIDE_AUDIO_SAMPLES];
} m;

#define FALLOFF_NEAR 1.0f
#define FALLOFF_FAR 50.0f

void tide_mixer_init(void)
{
    if (m.made) return;
    memset(&m, 0, sizeof m);
    m.made = true;
    m.writer = 0;
    m.reader = 1;
    atomic_store(&m.pending, 2);
    m.shots = tide_alloc_zeroed(TIDE_MIXER_ONE_SHOTS, sizeof *m.shots);
    m.capture = tide_alloc_zeroed(TIDE_MIXER_CAPTURE, sizeof *m.capture);
    m.voices = tide_alloc_zeroed(TIDE_MIXER_VOICES, sizeof *m.voices);
    for (uint32_t b = 0; b < TIDE_AUDIO_BUSES; b++) m.buses[b].volume = m.buses[b].gain = 1.0f;
    m.listener_rotation = tide_identity_q();
    m.falloff_near = FALLOFF_NEAR;
    m.falloff_far = FALLOFF_FAR;
    m.left_from = TIDE_AUDIO_BLOCK;
}

// ---------------------------------------------------------------------------
// The frame's side

// Copies knobs into the handed buffer, and says where they went.
static uint32_t handed_knobs(handed *h, const void *knobs, const uint32_t size)
{
    if (!size) return 0;
    if (h->knobs_used + size > h->knobs_capacity) {
        uint32_t c = h->knobs_capacity ? h->knobs_capacity : 256;
        while (c < h->knobs_used + size) c *= 2;
        h->knobs = tide_realloc(h->knobs, h->knobs_capacity, c);
        h->knobs_capacity = c;
    }
    const uint32_t at = h->knobs_used;
    memcpy(h->knobs + at, knobs, size);
    h->knobs_used += size;
    return at;
}

static void handed_voice(handed *h, const tide_voice *v, const tide_audio_frame *f)
{
    if (h->count == h->capacity) {
        const uint32_t c = h->capacity ? h->capacity * 2 : 16;
        h->voices = tide_realloc(h->voices, h->capacity * sizeof *h->voices, c * sizeof *h->voices);
        h->capacity = c;
    }
    tide_voice *copy = &h->voices[h->count++];
    *copy = *v;
    const uint32_t size = copy->effect.knobs_size;
    copy->knobs_at = handed_knobs(h, tide_audio_knobs(f, v->knobs_at, size), size);
}

void tide_mixer_submit(const tide_audio_frame *f)
{
    tide_mixer_init();
    handed *h = &m.slots[m.writer];
    h->count = 0;
    h->knobs_used = 0;
    h->listener_position = f->listener_position;
    h->listener_rotation = f->listener_rotation;
    h->listener_set = f->listener_set;
    h->falloff_near = f->falloff_near;
    h->falloff_far = f->falloff_far;
    h->forget = f->forget;
    h->forget_serial = f->forget ? m.forget_asked : 0;
    for (uint32_t b = 0; b < TIDE_AUDIO_BUSES; b++) {
        h->bus_volume[b] = f->bus_volume[b];
        h->bus_effect[b] = f->bus_effect[b];
        const uint32_t size = h->bus_effect[b].knobs_size;
        h->bus_knobs_at[b] = handed_knobs(h, tide_audio_knobs(f, f->bus_knobs_at[b], size), size);
    }
    for (uint32_t i = 0; i < f->voice_count; i++) {
        const tide_voice *v = &f->voices[i];
        if (v->id) {
            handed_voice(h, v, f);
            continue;
        }
        // A one-shot: into the ring, or dropped when it's full
        const uint32_t head = atomic_load_explicit(&m.shot_head, memory_order_relaxed);
        const uint32_t tail = atomic_load_explicit(&m.shot_tail, memory_order_acquire);
        if (head - tail >= TIDE_MIXER_ONE_SHOTS) {
            atomic_fetch_add_explicit(&m.dropped, 1, memory_order_relaxed);
            continue;
        }
        shot *s = &m.shots[head % TIDE_MIXER_ONE_SHOTS];
        s->voice = *v;
        if (v->effect.knobs_size) memcpy(s->knobs, tide_audio_knobs(f, v->knobs_at, v->effect.knobs_size), v->effect.knobs_size);
        atomic_store_explicit(&m.shot_head, head + 1, memory_order_release);
    }
    // Publish: take back whatever was pending, which is free either way (a
    // frame the renderer never took, or the slot it let go of)
    const uint32_t was = atomic_exchange_explicit(&m.pending, m.writer | PENDING_NEW, memory_order_acq_rel);
    m.writer = was & 3u;
}

// ---------------------------------------------------------------------------
// The renderer's side

static float loudness(const voice *v) { return v->gain_l > v->gain_r ? v->gain_l : v->gain_r; }

// A slot for a new voice: a free one, or the quietest's.
static voice *free_voice(void)
{
    voice *quietest = NULL;
    for (uint32_t i = 0; i < TIDE_MIXER_VOICES; i++) {
        voice *v = &m.voices[i];
        if (!v->id) return v;
        if (!quietest || loudness(v) < loudness(quietest)) quietest = v;
    }
    return quietest;
}

static voice *find_voice(const uint64_t id)
{
    for (uint32_t i = 0; i < TIDE_MIXER_VOICES; i++)
        if (m.voices[i].id == id && !m.voices[i].one_shot) return &m.voices[i];
    return NULL;
}

// Gains a voice is heard at, by its volume and, for a spatial one, where it
// is against the listener.
static void gains_of(const voice *v, float *l, float *r)
{
    float volume = v->volume > 0.0f ? v->volume : 0.0f;
    if (!v->spatial) {
        *l = *r = volume;
        return;
    }
    const tide_float3 to = {v->position.x - m.listener_position.x, v->position.y - m.listener_position.y,
                            v->position.z - m.listener_position.z};
    const float distance = tide_sqrt_f(tide_dot_f3(to, to));
    const float near = m.falloff_near, far = m.falloff_far;
    if (distance >= far) {
        *l = *r = 0.0f;
        return;
    }
    if (distance > near) volume *= (near / distance) * ((far - distance) / (far - near));
    float side = 0.0f; // -1 at the listener's left, 1 at its right
    if (distance > 1e-6f) {
        const tide_float3 right = tide_rotate_q(m.listener_rotation, (tide_float3){1.0f, 0.0f, 0.0f});
        side = tide_dot_f3(to, right) / distance;
        if (side < -1.0f) side = -1.0f;
        if (side > 1.0f) side = 1.0f;
    }
    *l = volume * tide_sqrt_f((1.0f - side) * 0.5f);
    *r = volume * tide_sqrt_f((1.0f + side) * 0.5f);
}

// A slot emptied, with what its stream held let go of
static void clear_voice(voice *v)
{
    tide_vorbis_reader_close(v->reader);
    memset(v, 0, sizeof *v);
}

// Starts a voice in `slot` as `v` says, with its gains at their targets: no ramp in.
static void start_voice(voice *slot, const tide_voice *v, const uint8_t *knobs, const bool one_shot)
{
    clear_voice(slot);
    slot->id = one_shot ? UINT64_MAX : v->id;
    slot->one_shot = one_shot;
    slot->playing = v->playing;
    slot->clip = v->clip;
    slot->spatial = v->spatial;
    slot->loop = v->loop;
    slot->position = v->position;
    slot->volume = v->volume;
    slot->pitch = v->pitch;
    slot->bus = v->bus < TIDE_AUDIO_BUSES ? v->bus : 0;
    slot->effect = v->effect;
    if (slot->effect.knobs_size) memcpy(slot->knobs, knobs, slot->effect.knobs_size);
    gains_of(slot, &slot->gain_l, &slot->gain_r);
}

// What the frame says of a voice it names again.
static void update_voice(voice *slot, const tide_voice *v, const uint8_t *knobs)
{
    if (slot->clip.id != v->clip.id || (!slot->playing && v->playing)) {
        slot->clip = v->clip;
        slot->cursor = 0.0;
        slot->ended = false;
        tide_vorbis_reader_close(slot->reader); // A stream starts over with a reader made again
        slot->reader = NULL;
        slot->drained = false;
    } else {
        slot->clip = v->clip; // The same clip, whose samples may have moved
    }
    slot->playing = v->playing;
    slot->spatial = v->spatial;
    slot->loop = v->loop;
    slot->position = v->position;
    slot->volume = v->volume;
    slot->pitch = v->pitch;
    slot->bus = v->bus < TIDE_AUDIO_BUSES ? v->bus : 0;
    if (slot->effect.id != v->effect.id || slot->effect.fn != v->effect.fn) memset(slot->state, 0, sizeof slot->state);
    slot->effect = v->effect;
    if (slot->effect.knobs_size) memcpy(slot->knobs, knobs, slot->effect.knobs_size);
    slot->releasing = !v->playing;
}

static void apply_handed(const handed *h)
{
    if (h->forget) {
        for (uint32_t i = 0; i < TIDE_MIXER_VOICES; i++) clear_voice(&m.voices[i]);
        for (uint32_t b = 0; b < TIDE_AUDIO_BUSES; b++) {
            m.buses[b].effect = (tide_audio_effect){0};
            memset(m.buses[b].state, 0, sizeof m.buses[b].state);
        }
        // What's queued names clips the host let go of, and what was captured is nobody's
        atomic_store_explicit(&m.shot_tail, atomic_load_explicit(&m.shot_head, memory_order_acquire), memory_order_release);
        atomic_store_explicit(&m.capture_tail, atomic_load_explicit(&m.capture_head, memory_order_acquire), memory_order_release);
        m.capture_primed = false;
        memset(m.mic, 0, sizeof m.mic);
        atomic_store_explicit(&m.forget_done, h->forget_serial, memory_order_release);
        return;
    }
    // Every frame starts from the defaults, as a frame's draw settings do
    m.listener_position = h->listener_set ? h->listener_position : (tide_float3){0.0f, 0.0f, 0.0f};
    m.listener_rotation = h->listener_set ? h->listener_rotation : tide_identity_q();
    m.falloff_near = h->falloff_near > 0.0f ? h->falloff_near : FALLOFF_NEAR;
    m.falloff_far = h->falloff_far > m.falloff_near ? h->falloff_far : (m.falloff_near < FALLOFF_FAR ? FALLOFF_FAR : m.falloff_near * 2.0f);
    for (uint32_t b = 0; b < TIDE_AUDIO_BUSES; b++) {
        bus *bus = &m.buses[b];
        bus->volume = h->bus_volume[b] > 0.0f ? h->bus_volume[b] : 0.0f;
        const tide_audio_effect *e = &h->bus_effect[b];
        if (bus->effect.id != e->id || bus->effect.fn != e->fn) memset(bus->state, 0, sizeof bus->state);
        bus->effect = *e;
        if (e->knobs_size) memcpy(bus->knobs, h->knobs + h->bus_knobs_at[b], e->knobs_size);
    }
    for (uint32_t i = 0; i < TIDE_MIXER_VOICES; i++) m.voices[i].named = false;
    for (uint32_t i = 0; i < h->count; i++) {
        const tide_voice *v = &h->voices[i];
        const uint8_t *knobs = h->knobs + v->knobs_at;
        voice *slot = find_voice(v->id);
        if (slot) {
            update_voice(slot, v, knobs);
        } else {
            if (!v->playing) continue; // Named as stopped before it ever played: nothing to fade
            slot = free_voice();
            start_voice(slot, v, knobs, false);
        }
        slot->named = true;
    }
    for (uint32_t i = 0; i < TIDE_MIXER_VOICES; i++) {
        voice *v = &m.voices[i];
        if (v->id && !v->one_shot && !v->named) v->releasing = true;
    }
}

void tide_mixer_take(void)
{
    tide_mixer_init();
    uint32_t pending = atomic_load_explicit(&m.pending, memory_order_acquire);
    if (pending & PENDING_NEW) {
        // Give the slot held back as the one neither side has, and take the new one
        pending = atomic_exchange_explicit(&m.pending, m.reader, memory_order_acq_rel);
        m.reader = pending & 3u;
        apply_handed(&m.slots[m.reader]);
    }
    uint32_t tail = atomic_load_explicit(&m.shot_tail, memory_order_relaxed);
    const uint32_t head = atomic_load_explicit(&m.shot_head, memory_order_acquire);
    for (; tail != head; tail++) {
        const shot *s = &m.shots[tail % TIDE_MIXER_ONE_SHOTS];
        if (s->voice.playing) start_voice(free_voice(), &s->voice, s->knobs, true);
    }
    atomic_store_explicit(&m.shot_tail, tail, memory_order_release);
}

// The stream's next frame into `b`: false at its end, where a loop starts over
static bool stream_next(voice *v)
{
    float frame[2];
    uint32_t got = tide_vorbis_read(v->reader, frame, 1);
    if (!got) {
        if (!v->loop) return false;
        tide_vorbis_rewind(v->reader);
        got = tide_vorbis_read(v->reader, frame, 1);
        if (!got) return false;
    }
    v->b[0] = frame[0];
    v->b[1] = v->clip.channels >= 2 ? frame[1] : frame[0];
    return true;
}

// The microphone's block, from the ring, as the block begins: the newest
// whole block when the device has run ahead by more than a few, silence when
// it hasn't given one yet
static void take_capture(void)
{
    uint32_t tail = atomic_load_explicit(&m.capture_tail, memory_order_relaxed);
    const uint32_t head = atomic_load_explicit(&m.capture_head, memory_order_acquire);
    const uint32_t available = head - tail;
    if (!m.capture_primed && available >= TIDE_MIXER_CAPTURE_PRIME) m.capture_primed = true;
    if (!m.capture_primed || available < TIDE_AUDIO_BLOCK) {
        memset(m.mic, 0, sizeof m.mic); // Nothing yet, or run dry: silence, until there's a prime's worth again
        m.capture_primed = false;
        return;
    }
    if (available > TIDE_MIXER_CAPTURE_LAG) tail = head - TIDE_MIXER_CAPTURE_PRIME; // Far behind: skip to the newest
    for (uint32_t i = 0; i < TIDE_AUDIO_BLOCK; i++) m.mic[i] = m.capture[(tail + i) % TIDE_MIXER_CAPTURE];
    atomic_store_explicit(&m.capture_tail, tail + TIDE_AUDIO_BLOCK, memory_order_release);
}

void tide_mixer_capture(const float *mono, const uint32_t frames)
{
    tide_mixer_init();
    uint32_t head = atomic_load_explicit(&m.capture_head, memory_order_relaxed);
    const uint32_t tail = atomic_load_explicit(&m.capture_tail, memory_order_acquire);
    for (uint32_t i = 0; i < frames && head - tail < TIDE_MIXER_CAPTURE; i++) m.capture[head++ % TIDE_MIXER_CAPTURE] = mono[i];
    atomic_store_explicit(&m.capture_head, head, memory_order_release);
}

// A streamed clip's block: decoded as it's read, two frames at a time, so a
// pitch other than 1 and the clip's own rate go through the same interpolation
// a decoded clip's samples do. The reader is made at the first block.
static bool read_stream(voice *v, float *out)
{
    if (!v->reader) {
        v->reader = tide_vorbis_reader_open(v->clip.stream);
        if (!v->reader || !stream_next(v)) return false;
        v->a[0] = v->b[0];
        v->a[1] = v->b[1];
        if (!stream_next(v)) v->drained = true;
        v->cursor = 0.0;
    }
    const double step = (double)v->pitch * (double)v->clip.rate / (double)TIDE_AUDIO_RATE;
    if (!(step > 0.0)) return true;
    for (uint32_t i = 0; i < TIDE_AUDIO_BLOCK; i++) {
        while (v->cursor >= 1.0) {
            if (v->drained) return false; // Past the last frame
            v->a[0] = v->b[0];
            v->a[1] = v->b[1];
            if (!stream_next(v)) v->drained = true; // `b` stays the last frame, held to the end
            v->cursor -= 1.0;
        }
        const float t = (float)v->cursor;
        out[2 * i] = v->a[0] + (v->b[0] - v->a[0]) * t;
        out[2 * i + 1] = v->a[1] + (v->b[1] - v->a[1]) * t;
        v->cursor += step;
    }
    return true;
}

// Reads a voice's clip for a block into m.temp, advancing its cursor. False
// once the clip ended (and doesn't loop): the block is silence from there.
static bool read_clip(voice *v, float *out)
{
    memset(out, 0, TIDE_AUDIO_SAMPLES * sizeof *out);
    const tide_clip *c = &v->clip;
    if (c->microphone) { // What the device captured, in both channels, at its own pace: pitch doesn't apply
        for (uint32_t i = 0; i < TIDE_AUDIO_BLOCK; i++) out[2 * i] = out[2 * i + 1] = m.mic[i];
        return true;
    }
    if (c->stream && c->rate && c->channels) return read_stream(v, out);
    if (!c->samples || !c->frames || c->channels == 0) return v->effect.fn != NULL; // A source goes on; silence ends
    const double step = (double)v->pitch * (double)c->rate / (double)TIDE_AUDIO_RATE;
    if (!(step > 0.0)) return true; // Held: silence, going nowhere
    const uint32_t stride = c->channels >= 2 ? 2 : 1;
    for (uint32_t i = 0; i < TIDE_AUDIO_BLOCK; i++) {
        double at = v->cursor;
        if (at >= (double)c->frames) {
            if (!v->loop) return false;
            while (at >= (double)c->frames) at -= (double)c->frames;
            v->cursor = at;
        }
        const uint32_t i0 = (uint32_t)at;
        const float t = (float)(at - (double)i0);
        uint32_t i1 = i0 + 1;
        if (i1 >= c->frames) i1 = v->loop ? 0 : c->frames - 1;
        const float *a = c->samples + (size_t)i0 * stride;
        const float *b = c->samples + (size_t)i1 * stride;
        const float l = a[0] + (b[0] - a[0]) * t;
        const float r = stride == 2 ? a[1] + (b[1] - a[1]) * t : l;
        out[2 * i] = l;
        out[2 * i + 1] = r;
        v->cursor = at + step;
    }
    return true;
}

static void render_block(float *out)
{
    tide_mixer_take();
    take_capture();
    for (uint32_t b = 0; b < TIDE_AUDIO_BUSES; b++) {
        m.buses[b].used = false;
        memset(m.buses[b].mix, 0, sizeof m.buses[b].mix);
    }
    uint32_t playing = 0;
    for (uint32_t i = 0; i < TIDE_MIXER_VOICES; i++) {
        voice *v = &m.voices[i];
        if (!v->id) continue;
        bool goes_on = true;
        if (v->ended) {
            memset(m.temp, 0, sizeof m.temp);
        } else {
            goes_on = read_clip(v, m.temp);
        }
        if (v->effect.fn) v->effect.fn(m.temp, v->state, v->effect.knobs_size ? v->knobs : NULL);
        float target_l, target_r;
        if (v->releasing) {
            target_l = target_r = 0.0f;
        } else {
            gains_of(v, &target_l, &target_r);
        }
        const float step_l = (target_l - v->gain_l) * (1.0f / (float)TIDE_AUDIO_BLOCK);
        const float step_r = (target_r - v->gain_r) * (1.0f / (float)TIDE_AUDIO_BLOCK);
        float gl = v->gain_l, gr = v->gain_r;
        float *mix = m.buses[v->bus].mix;
        for (uint32_t s = 0; s < TIDE_AUDIO_BLOCK; s++) {
            gl += step_l;
            gr += step_r;
            mix[2 * s] += m.temp[2 * s] * gl;
            mix[2 * s + 1] += m.temp[2 * s + 1] * gr;
        }
        v->gain_l = target_l;
        v->gain_r = target_r;
        m.buses[v->bus].used = true;
        if (v->releasing && target_l == 0.0f && target_r == 0.0f) {
            clear_voice(v); // Faded out
        } else if (!goes_on) {
            // Its clip ended: a one-shot is done, and a named voice stays,
            // silent, until the frame starts it over or stops naming it
            if (v->one_shot) {
                clear_voice(v);
            } else {
                v->ended = true;
            }
        }
        if (v->id) playing++; // Still there after the block
    }
    atomic_store_explicit(&m.playing, playing, memory_order_relaxed);
    // Each bus into the master, then the master out
    for (uint32_t b = TIDE_AUDIO_BUSES; b-- > 0;) {
        bus *bus = &m.buses[b];
        if (!bus->used && !bus->effect.fn && b != 0) continue;
        if (bus->effect.fn) bus->effect.fn(bus->mix, bus->state, bus->effect.knobs_size ? bus->knobs : NULL);
        const float step = (bus->volume - bus->gain) * (1.0f / (float)TIDE_AUDIO_BLOCK);
        float g = bus->gain;
        float *into = b == 0 ? out : m.buses[0].mix;
        if (b == 0) {
            for (uint32_t s = 0; s < TIDE_AUDIO_SAMPLES; s += 2) {
                g += step;
                into[s] = bus->mix[s] * g;
                into[s + 1] = bus->mix[s + 1] * g;
            }
        } else {
            for (uint32_t s = 0; s < TIDE_AUDIO_SAMPLES; s += 2) {
                g += step;
                into[s] += bus->mix[s] * g;
                into[s + 1] += bus->mix[s + 1] * g;
            }
            m.buses[0].used = true;
        }
        bus->gain = bus->volume;
    }
}

void tide_mixer_render(float *out, uint32_t frames)
{
    tide_mixer_init();
    while (frames) {
        if (m.left_from == TIDE_AUDIO_BLOCK) {
            render_block(m.left);
            m.left_from = 0;
        }
        uint32_t n = TIDE_AUDIO_BLOCK - m.left_from;
        if (n > frames) n = frames;
        memcpy(out, m.left + 2 * m.left_from, n * 2 * sizeof *out);
        m.left_from += n;
        out += 2 * n;
        frames -= n;
    }
}

void tide_mixer_forget(void)
{
    tide_mixer_init();
    tide_audio_frame f = {0};
    tide_audio_reset(&f);
    f.forget = true;
    const uint32_t serial = ++m.forget_asked;
    tide_mixer_submit(&f);
    tide_audio_free(&f);
    if (!atomic_load_explicit(&m.elsewhere, memory_order_acquire)) {
        tide_mixer_take(); // The frame's thread is the renderer: it takes it now
        return;
    }
    // The device's thread takes it at its next block; a device that stalled isn't waited for forever
    const uint64_t began = tide_time_now_ns();
    while (atomic_load_explicit(&m.forget_done, memory_order_acquire) != serial) {
        if (tide_time_now_ns() - began > UINT64_C(500000000)) break;
        tide_yield();
    }
}

void tide_mixer_rendered_elsewhere(const bool elsewhere)
{
    tide_mixer_init();
    atomic_store_explicit(&m.elsewhere, elsewhere, memory_order_release);
}

void tide_mixer_flush(void)
{
    tide_mixer_init();
    m.left_from = TIDE_AUDIO_BLOCK;
}

uint32_t tide_mixer_voices(void) { return m.made ? atomic_load_explicit(&m.playing, memory_order_relaxed) : 0; }

uint32_t tide_mixer_dropped(void) { return m.made ? atomic_load_explicit(&m.dropped, memory_order_relaxed) : 0; }
