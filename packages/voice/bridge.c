#include "voice.h"

#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wlanguage-extension-token"
#include "opus/include/opus.h"
#pragma clang diagnostic pop
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wstrict-prototypes"
#include "rnnoise/include/rnnoise.h"
#pragma clang diagnostic pop

#define FRAME 960u
#define PACKET 400u
#define SPEAKERS 32u
#define JITTER 16u
#define CAPTURE 32u
#define PCM 8192u
#define VOICE_ID UINT64_C(0x766f696365000000)

typedef struct capture_block {
    uint32_t generation;
    float samples[TIDE_AUDIO_BLOCK];
} capture_block;

typedef struct pcm_ring {
    atomic_uint head, tail;
    float samples[PCM];
} pcm_ring;

typedef struct packet {
    uint32_t sequence, size;
    uint8_t data[PACKET];
} packet;

typedef struct speaker {
    tide_player_id player;
    uint64_t connection, retired_connection;
    uint32_t stream, expected, newest;
    uint32_t cutoff;
    atomic_uint generation;
    bool started;
    double last, due;
    OpusDecoder *decoder;
    packet packets[JITTER];
    pcm_ring pcm;
} speaker;

typedef struct gain {
    tide_player_id player;
    float volume;
} gain;

typedef struct route {
    tide_player_id player;
    uint32_t bus;
} route;

struct tide_voice_client {
    bool enabled, transmitting, was_transmitting;
    bool noise_suppression, was_suppressing;
    float volume;
    uint32_t bus;
    tide_session *session;
    tide_player_id player;
    uint64_t cookie;
    uint64_t match_generation;
    uint32_t stream, sequence, filled;
    atomic_uint generation;
    atomic_uint capture_head, capture_tail;
    capture_block capture[CAPTURE];
    float input[FRAME];
    OpusEncoder *encoder;
    DenoiseState *denoiser;
    speaker speakers[SPEAKERS];
    gain gains[SPEAKERS];
    route routes[SPEAKERS];
    double now;
};

typedef struct capture_knobs {
    tide_voice_client *client;
    uint32_t generation;
} capture_knobs;

typedef struct playback_knobs {
    speaker *speaker;
    uint32_t generation, cutoff;
} playback_knobs;

static float safe_gain(const float value)
{
    return isfinite(value) && value > 0.0f ? (value < 4.0f ? value : 4.0f) : 0.0f;
}

static bool before(const uint32_t a, const uint32_t b)
{
    const uint32_t distance = b - a;
    return distance != 0u && distance < UINT32_C(0x80000000);
}

// Audio thread: bounded copies only. No codec, allocation, lock or network.
static void capture_audio(float *samples, void *state, const void *knobs)
{
    (void)state;
    const capture_knobs *k = knobs;
    tide_voice_client *v = k->client;
    if (atomic_load_explicit(&v->generation, memory_order_acquire) == k->generation) {
        const uint32_t head = atomic_load_explicit(&v->capture_head, memory_order_relaxed);
        const uint32_t tail = atomic_load_explicit(&v->capture_tail, memory_order_acquire);
        if (head - tail < CAPTURE) {
            capture_block *block = &v->capture[head % CAPTURE];
            block->generation = k->generation;
            for (uint32_t i = 0; i < TIDE_AUDIO_BLOCK; i++) {
                const float sample = samples[2u * i] * 0.5f + samples[2u * i + 1u] * 0.5f;
                block->samples[i] = !isfinite(sample) ? 0.0f : sample < -1.0f ? -1.0f : sample > 1.0f ? 1.0f : sample;
            }
            atomic_store_explicit(&v->capture_head, head + 1u, memory_order_release);
        }
    }
    memset(samples, 0, TIDE_AUDIO_SAMPLES * sizeof *samples);
}

static void playback_audio(float *samples, void *state, const void *knobs)
{
    (void)state;
    const playback_knobs *k = knobs;
    speaker *s = k->speaker;
    memset(samples, 0, TIDE_AUDIO_SAMPLES * sizeof *samples);
    if (atomic_load_explicit(&s->generation, memory_order_acquire) != k->generation) return;
    const uint32_t head = atomic_load_explicit(&s->pcm.head, memory_order_acquire);
    uint32_t tail = atomic_load_explicit(&s->pcm.tail, memory_order_relaxed);
    if (before(tail, k->cutoff)) tail = k->cutoff;
    if (head - tail > 4u * FRAME) tail = head - 2u * FRAME;
    uint32_t count = head - tail;
    if (count > TIDE_AUDIO_BLOCK) count = TIDE_AUDIO_BLOCK;
    for (uint32_t i = 0; i < count; i++) {
        const float sample = s->pcm.samples[(tail + i) % PCM];
        samples[2u * i] = samples[2u * i + 1u] = sample;
    }
    atomic_store_explicit(&s->pcm.tail, tail + count, memory_order_release);
}

static void reset_speaker(speaker *s)
{
    atomic_fetch_add_explicit(&s->generation, 1u, memory_order_release);
    s->cutoff = atomic_load_explicit(&s->pcm.head, memory_order_acquire);
    s->player = (tide_player_id){0};
    s->connection = s->retired_connection = 0;
    s->started = false;
    memset(s->packets, 0, sizeof s->packets);
    if (s->decoder) opus_decoder_ctl(s->decoder, OPUS_RESET_STATE);
}

tide_voice_client *tide_voice_client_create(void)
{
    tide_voice_client *v = calloc(1, sizeof *v);
    if (!v) return NULL;
    atomic_init(&v->generation, 1u);
    atomic_init(&v->capture_head, 0u);
    atomic_init(&v->capture_tail, 0u);
    for (uint32_t i = 0; i < SPEAKERS; i++) {
        atomic_init(&v->speakers[i].generation, 1u);
        atomic_init(&v->speakers[i].pcm.head, 0u);
        atomic_init(&v->speakers[i].pcm.tail, 0u);
    }
    v->volume = 1.0f;
    return v;
}

void tide_voice_client_configure(tide_voice_client *v, const bool enabled, const bool transmitting,
                                 const bool noise_suppression, const float volume)
{
    if (!v) return;
    v->enabled = enabled;
    v->transmitting = enabled && transmitting;
    v->noise_suppression = noise_suppression;
    v->volume = safe_gain(volume);
}

void tide_voice_client_volume(tide_voice_client *v, const tide_player_id player, const float volume)
{
    if (!v || tide_player_index(player) < 0) return;
    gain *empty = NULL;
    for (uint32_t i = 0; i < SPEAKERS; i++) {
        gain *g = &v->gains[i];
        if (tide_player_equal(g->player, player)) { g->volume = safe_gain(volume); return; }
        if (!g->player.id && !empty) empty = g;
    }
    if (empty) *empty = (gain){player, safe_gain(volume)};
}

static float player_gain(const tide_voice_client *v, const tide_player_id player)
{
    for (uint32_t i = 0; i < SPEAKERS; i++) {
        if (tide_player_equal(v->gains[i].player, player)) return v->gains[i].volume;
    }
    return 1.0f;
}

static uint32_t safe_bus(const int32_t bus)
{
    return bus >= 0 && (uint32_t)bus < TIDE_AUDIO_BUSES ? (uint32_t)bus : 0u;
}

void tide_voice_client_bus(tide_voice_client *v, const int32_t bus)
{
    if (v) v->bus = safe_bus(bus);
}

void tide_voice_client_player_bus(tide_voice_client *v, const tide_player_id player, const int32_t bus)
{
    if (!v || tide_player_index(player) < 0) return;
    route *empty = NULL;
    for (uint32_t i = 0; i < SPEAKERS; i++) {
        route *r = &v->routes[i];
        if (tide_player_equal(r->player, player)) {
            if (bus < 0) *r = (route){0};
            else r->bus = safe_bus(bus);
            return;
        }
        if (!r->player.id && !empty) empty = r;
    }
    if (bus >= 0 && empty) *empty = (route){player, safe_bus(bus)};
}

static uint32_t player_bus(const tide_voice_client *v, const tide_player_id player)
{
    for (uint32_t i = 0; i < SPEAKERS; i++) {
        if (tide_player_equal(v->routes[i].player, player)) return v->routes[i].bus;
    }
    return v->bus;
}

static void receive_voice(void *user, const tide_player_id player, const uint64_t connection,
                            const uint8_t *data, const uint32_t size)
{
    tide_voice_client *v = user;
    if (!v->enabled || size <= 9u || size > PACKET + 9u || data[0] != 1u) return;
    tide_reader r = {data, size, 1u, false};
    const uint32_t stream = tide_read_u32(&r);
    const uint32_t sequence = tide_read_u32(&r);
    const uint8_t *bytes = data + 9u;
    const uint32_t count = size - 9u;
    if (!stream || opus_packet_get_nb_channels(bytes) != 1
        || opus_packet_get_nb_samples(bytes, (opus_int32)count, 48000) != (int)FRAME) return;
    speaker *s = NULL, *available = NULL;
    for (uint32_t i = 0; i < SPEAKERS; i++) {
        speaker *candidate = &v->speakers[i];
        if (tide_player_equal(candidate->player, player)) { s = candidate; break; }
        if (!candidate->player.id || v->now - candidate->last > 0.5) {
            if (!available || candidate->last < available->last) available = candidate;
        }
    }
    if (!s) {
        if (!available) return;
        s = available;
        reset_speaker(s);
        s->player = player;
    }
    if (connection == s->retired_connection) return;
    if (connection == s->connection && s->started && before(stream, s->stream) && v->now - s->last <= 0.3) return;
    if (!s->started || s->connection != connection || s->stream != stream || v->now - s->last > 0.3) {
        if (!s->decoder) {
            int error;
            s->decoder = opus_decoder_create(48000, 1, &error);
            if (!s->decoder) return;
        }
        opus_decoder_ctl(s->decoder, OPUS_RESET_STATE);
        if (s->connection != connection) s->retired_connection = s->connection;
        s->connection = connection;
        s->stream = stream;
        s->expected = s->newest = sequence;
        s->due = v->now + 0.04;
        s->cutoff = atomic_load_explicit(&s->pcm.head, memory_order_acquire);
        atomic_fetch_add_explicit(&s->generation, 1u, memory_order_release);
        memset(s->packets, 0, sizeof s->packets);
        s->started = true;
    }
    if (before(sequence, s->expected)) {
        if (v->now < s->due && s->expected - sequence < JITTER) s->expected = sequence;
        else return;
    }
    if (sequence - s->expected >= JITTER) {
        s->expected = sequence;
        s->due = v->now + 0.04;
        memset(s->packets, 0, sizeof s->packets);
    }
    if (before(s->newest, sequence)) s->newest = sequence;
    packet *p = &s->packets[sequence % JITTER];
    if (p->size && p->sequence == sequence) return;
    p->sequence = sequence;
    p->size = count;
    memcpy(p->data, bytes, count);
    s->last = v->now;
}

static bool make_encoder(tide_voice_client *v)
{
    if (v->encoder) return true;
    int error;
    v->encoder = opus_encoder_create(48000, 1, OPUS_APPLICATION_VOIP, &error);
    if (!v->encoder) return false;
    opus_encoder_ctl(v->encoder, OPUS_SET_BITRATE(24000));
    opus_encoder_ctl(v->encoder, OPUS_SET_COMPLEXITY(5));
    opus_encoder_ctl(v->encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    opus_encoder_ctl(v->encoder, OPUS_SET_INBAND_FEC(1));
    opus_encoder_ctl(v->encoder, OPUS_SET_PACKET_LOSS_PERC(10));
    opus_encoder_ctl(v->encoder, OPUS_SET_DTX(1));
    return true;
}

static bool make_denoiser(tide_voice_client *v)
{
    if (v->denoiser) return true;
    if (rnnoise_get_frame_size() != (int)(FRAME / 2u)) return false;
    // Allocate explicitly: upstream rnnoise_create doesn't check malloc before init.
    v->denoiser = malloc((size_t)rnnoise_get_size());
    if (!v->denoiser) return false;
    if (rnnoise_init(v->denoiser, NULL) == 0) return true;
    rnnoise_destroy(v->denoiser);
    v->denoiser = NULL;
    return false;
}

static void suppress_capture(tide_voice_client *v)
{
    // RNNoise expects 48 kHz mono float samples in PCM16's amplitude range.
    // Its two 10 ms blocks fit the existing 20 ms Opus frame, with no new queue.
    for (uint32_t i = 0; i < FRAME; i++) v->input[i] *= 32768.0f;
    for (uint32_t at = 0; at < FRAME; at += FRAME / 2u) {
        rnnoise_process_frame(v->denoiser, v->input + at, v->input + at);
    }
    for (uint32_t i = 0; i < FRAME; i++) {
        const float sample = v->input[i] / 32768.0f;
        v->input[i] = !isfinite(sample) ? 0.0f : sample < -1.0f ? -1.0f : sample > 1.0f ? 1.0f : sample;
    }
}

static void send_capture(tide_voice_client *v, tide_session *session, tide_audio_frame *audio)
{
    const bool sending = v->enabled && v->transmitting && make_encoder(v)
        && (!v->noise_suppression || make_denoiser(v));
    if (sending != v->was_transmitting || (sending && v->noise_suppression != v->was_suppressing)) {
        atomic_fetch_add_explicit(&v->generation, 1u, memory_order_release);
        v->filled = 0;
        if (sending) {
            opus_encoder_ctl(v->encoder, OPUS_RESET_STATE);
            if (v->noise_suppression) rnnoise_init(v->denoiser, NULL);
            v->stream++;
            if (!v->stream) v->stream++;
            v->sequence = 0;
        }
        v->was_transmitting = sending;
        v->was_suppressing = sending && v->noise_suppression;
    }
    const uint32_t generation = atomic_load_explicit(&v->generation, memory_order_acquire);
    const uint32_t head = atomic_load_explicit(&v->capture_head, memory_order_acquire);
    uint32_t tail = atomic_load_explicit(&v->capture_tail, memory_order_relaxed);
    if (!sending) tail = head;
    if (head - tail > 12u) { tail = head - 12u; v->filled = 0; }
    while (tail != head) {
        const capture_block *block = &v->capture[tail % CAPTURE];
        if (block->generation == generation) {
            for (uint32_t i = 0; i < TIDE_AUDIO_BLOCK; i++) {
                v->input[v->filled++] = block->samples[i];
                if (v->filled == FRAME) {
                    if (v->noise_suppression) suppress_capture(v);
                    uint8_t data[PACKET + 9u];
                    tide_writer w = {data, sizeof data, 0, false, false};
                    tide_write_u8(&w, 1u);
                    tide_write_u32(&w, v->stream);
                    tide_write_u32(&w, v->sequence++);
                    const int size = opus_encode_float(v->encoder, v->input, (int)FRAME, data + 9u, PACKET);
                    if (size > 0) tide_session_send_data(session, data, (uint32_t)size + 9u);
                    v->filled = 0;
                }
            }
        }
        tail++;
    }
    atomic_store_explicit(&v->capture_tail, tail, memory_order_release);
    if (!sending) return;
    const capture_knobs knobs = {v, generation};
    const tide_voice voice = {.id = VOICE_ID, .clip = tide_clip_of(TIDE_CLIP_MICROPHONE),
        .playing = true, .volume = 0.0f, .pitch = 1.0f,
        .effect = {.fn = capture_audio, .id = VOICE_ID, .knobs_size = sizeof knobs, .knobs = &knobs}};
    tide_audio_voice(audio, &voice);
}

static void play_speaker(tide_voice_client *v, speaker *s, tide_audio_frame *audio)
{
    if (!s->started || v->now - s->last > 0.3) return;
    if (v->now - s->due > 0.12) {
        s->expected = s->newest;
        s->due = v->now;
    }
    for (uint32_t work = 0; work < 4u && s->due <= v->now; work++) {
        const uint32_t head = atomic_load_explicit(&s->pcm.head, memory_order_relaxed);
        const uint32_t tail = atomic_load_explicit(&s->pcm.tail, memory_order_acquire);
        if (head - tail > PCM - FRAME) break;
        packet *p = &s->packets[s->expected % JITTER];
        const bool have = p->size && p->sequence == s->expected;
        packet *next = &s->packets[(s->expected + 1u) % JITTER];
        const bool fec = !have && next->size && next->sequence == s->expected + 1u;
        float samples[FRAME];
        const int count = opus_decode_float(s->decoder, have ? p->data : fec ? next->data : NULL,
            (opus_int32)(have ? p->size : fec ? next->size : 0u), samples, (int)FRAME, fec ? 1 : 0);
        if (have) p->size = 0;
        for (uint32_t i = 0; i < FRAME; i++) {
            const float sample = count == (int)FRAME ? samples[i] : 0.0f;
            s->pcm.samples[(head + i) % PCM] = isfinite(sample) ? sample : 0.0f;
        }
        atomic_store_explicit(&s->pcm.head, head + FRAME, memory_order_release);
        s->expected++;
        s->due += 0.02;
    }
    const playback_knobs knobs = {s, atomic_load_explicit(&s->generation, memory_order_acquire), s->cutoff};
    const uint32_t index = (uint32_t)(s - v->speakers);
    const tide_voice voice = {.id = VOICE_ID + 1u + index, .playing = true, .pitch = 1.0f,
        .volume = v->volume * player_gain(v, s->player),
        .bus = player_bus(v, s->player),
        .effect = {.fn = playback_audio, .id = VOICE_ID + 1u + index, .knobs_size = sizeof knobs, .knobs = &knobs}};
    tide_audio_voice(audio, &voice);
}

void tide_voice_client_frame(tide_voice_client *v, tide_session *session, tide_audio_frame *audio, const double now)
{
    if (!v || !session || !isfinite(now)) return;
    const tide_session_status status = tide_session_status_of(session);
    const bool connected = status.client.state == TIDE_SESSION_CONNECTED;
    v->now = now;
    const uint64_t match_generation = tide_session_data_generation(session);
    if (session != v->session || match_generation != v->match_generation
        || status.client.cookie != v->cookie || !tide_player_equal(status.client.player, v->player)) {
        if (v->session && v->session != session) tide_session_listen_data(v->session, NULL, NULL);
        for (uint32_t i = 0; i < SPEAKERS; i++) reset_speaker(&v->speakers[i]);
        if (v->session && (session != v->session || match_generation != v->match_generation)) {
            memset(v->gains, 0, sizeof v->gains);
            memset(v->routes, 0, sizeof v->routes);
        }
        v->session = session;
        v->cookie = status.client.cookie;
        v->match_generation = match_generation;
        v->player = status.client.player;
        v->was_transmitting = false;
        v->filled = 0;
        atomic_fetch_add_explicit(&v->generation, 1u, memory_order_release);
    }
    tide_session_listen_data(session, v->enabled && connected ? receive_voice : NULL, v);
    if (!connected || !v->enabled) {
        if (v->was_transmitting) atomic_fetch_add_explicit(&v->generation, 1u, memory_order_release);
        v->was_transmitting = false;
        v->filled = 0;
        atomic_store_explicit(&v->capture_tail, atomic_load_explicit(&v->capture_head, memory_order_acquire), memory_order_release);
        for (uint32_t i = 0; i < SPEAKERS; i++) {
            if (v->speakers[i].started) reset_speaker(&v->speakers[i]);
        }
        return;
    }
    send_capture(v, session, audio);
    for (uint32_t i = 0; i < SPEAKERS; i++) play_speaker(v, &v->speakers[i], audio);
}

void tide_voice_client_free(tide_voice_client *v, tide_session *session)
{
    if (!v) return;
    tide_session_listen_data(session, NULL, NULL);
    if (v->encoder) opus_encoder_destroy(v->encoder);
    if (v->denoiser) rnnoise_destroy(v->denoiser);
    for (uint32_t i = 0; i < SPEAKERS; i++) {
        if (v->speakers[i].decoder) opus_decoder_destroy(v->speakers[i].decoder);
    }
    free(v);
}

// The standard host runs one player per process/library. Custom hosts can
// use the explicit client API above instead of this registration.
static tide_voice_client *client;
static bool enabled, transmitting, noise_suppression;
static float volume = 1.0f;
static int32_t bus;

static void frame(void *self, tide_session *session, tide_audio_frame *audio, const double now)
{
    (void)self;
    if (!client && enabled) client = tide_voice_client_create();
    if (!client) return;
    tide_voice_client_configure(client, enabled, transmitting, noise_suppression, volume);
    tide_voice_client_bus(client, bus);
    tide_voice_client_frame(client, session, audio, now);
}

static void stop(void *self, tide_session *session)
{
    (void)self;
    tide_voice_client_free(client, session);
    client = NULL;
}

void TideVoiceConfigure(const bool listen, const bool send, const bool suppress, const float gain, const int32_t receive_bus,
                        const tide_clip_ref microphone)
{
    (void)microphone; // The Tide reference requests microphone permission metadata.
    enabled = listen;
    transmitting = send;
    noise_suppression = suppress;
    volume = gain;
    bus = receive_bus;
    const tide_voice_provider provider = {.frame = frame, .stop = stop};
    tide_voice_set(&provider);
}

void TideVoiceSetVolume(const tide_player_id player, const float gain)
{
    if (!client && tide_player_index(player) >= 0) client = tide_voice_client_create();
    if (client) tide_voice_client_volume(client, player, gain);
}

void TideVoiceSetBus(const tide_player_id player, const int32_t receive_bus)
{
    if (!client && receive_bus >= 0 && tide_player_index(player) >= 0) client = tide_voice_client_create();
    if (client) tide_voice_client_player_bus(client, player, receive_bus);
}
