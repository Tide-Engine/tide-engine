#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "voice.h"
#include "tide_test.h"
#include "tide/wav.h"
#include "mixer.h"

extern const unsigned char tide_voice_test_speech[];
extern const unsigned int tide_voice_test_speech_size;

typedef struct player {
    tide_session *session;
    tide_voice_client *voice;
    tide_audio_frame audio;
    double energy;
    uint32_t captured, played;
} player;

static player players[3];
static tide_loopback *network;
static double now;
static uint32_t sample_number;
static tide_transport endpoints[3];
static bool noise_source, silent_source;
static uint32_t noise_random;
static tide_sound speech_source;
static int mixed_player = -1;
static float bus_gain, unrelated[TIDE_AUDIO_SAMPLES];
static uint32_t effect_blocks;

static void dynamic_bus(float *samples, void *state, const void *knobs)
{
    uint32_t *blocks = state;
    effect_blocks = ++*blocks;
    const float gain = *(const float *)knobs;
    for (uint32_t i = 0; i < TIDE_AUDIO_SAMPLES; i++) samples[i] *= gain;
}

static void begin(const tide_net_conditions conditions)
{
    memset(players, 0, sizeof players);
    now = 0;
    sample_number = 0;
    noise_source = silent_source = false;
    noise_random = 19u;
    mixed_player = -1;
    bus_gain = 1.0f;
    effect_blocks = 0u;
    for (uint32_t i = 0; i < TIDE_AUDIO_SAMPLES; i++) unrelated[i] = 0.03125f;
    network = tide_loopback_create(19);
    tide_loopback_set_conditions(network, conditions);
    for (uint32_t i = 0; i < 3; i++) {
        players[i].session = tide_session_create(&(tide_session_desc){.game = &tide_game_api});
        players[i].voice = tide_voice_client_create();
        TIDE_CHECK(players[i].session && players[i].voice);
    }
    tide_session_start(players[0].session, NULL, now);
    TIDE_CHECK(tide_session_open(players[0].session, tide_loopback_endpoint(network, 1)));
    for (uint32_t i = 1; i < 3; i++) {
        endpoints[i] = tide_loopback_endpoint(network, i + 1u);
        tide_session_join(players[i].session, endpoints[i], tide_loopback_address(1), now);
    }
}

static void render(player *p, const float *input)
{
    if (mixed_player >= 0 && p == &players[mixed_player]) {
        const tide_voice dry = {.id = 123u,
            .clip = {.samples = unrelated, .frames = TIDE_AUDIO_BLOCK, .channels = 2u,
                     .rate = TIDE_AUDIO_RATE, .id = 123u},
            .playing = true, .loop = true, .volume = 1.0f, .pitch = 1.0f, .bus = 3u};
        tide_audio_voice(&p->audio, &dry);
        const tide_audio_effect effect = {.fn = dynamic_bus, .id = 456u,
            .state_size = sizeof(uint32_t), .knobs_size = sizeof bus_gain, .knobs = &bus_gain};
        tide_audio_set_bus_effect(&p->audio, 2u, &effect);
        float output[TIDE_AUDIO_SAMPLES];
        tide_mixer_submit(&p->audio);
        tide_mixer_render(output, TIDE_AUDIO_BLOCK);
        for (uint32_t i = 0; i < TIDE_AUDIO_SAMPLES; i++) {
            TIDE_CHECK(isfinite(output[i]));
            // The unrelated dry voice must remain, even while voice bus 2 is muted.
            const double difference = (double)output[i] - unrelated[i];
            p->energy += difference * difference;
        }
        return;
    }
    for (uint32_t i = 0; i < p->audio.voice_count; i++) {
        const tide_voice *voice = &p->audio.voices[i];
        if (!voice->effect.fn) continue;
        float samples[TIDE_AUDIO_SAMPLES] = {0};
        if (voice->clip.microphone) {
            if (input) memcpy(samples, input, sizeof samples);
            p->captured++;
        }
        voice->effect.fn(samples, NULL, tide_audio_knobs(&p->audio, voice->knobs_at, voice->effect.knobs_size));
        if (voice->clip.microphone) {
            for (uint32_t j = 0; j < TIDE_AUDIO_SAMPLES; j++) TIDE_CHECK(samples[j] == 0.0f);
        } else {
            p->played++;
            for (uint32_t j = 0; j < TIDE_AUDIO_SAMPLES; j++) {
                TIDE_CHECK(isfinite(samples[j]));
                p->energy += (double)samples[j] * samples[j] * voice->volume * voice->volume;
            }
        }
    }
}

static void step(void)
{
    now += (double)TIDE_AUDIO_BLOCK / TIDE_AUDIO_RATE;
    tide_loopback_set_time(network, now);
    for (uint32_t i = 0; i < 3; i++) tide_session_update(players[i].session, now);
    for (uint32_t i = 0; i < 3; i++) {
        tide_audio_reset(&players[i].audio);
        tide_voice_client_frame(players[i].voice, players[i].session, &players[i].audio, now);
    }
    float input[TIDE_AUDIO_SAMPLES];
    for (uint32_t i = 0; i < TIDE_AUDIO_BLOCK; i++) {
        noise_random = noise_random * 1664525u + 1013904223u;
        const float noise = 0.08f * ((float)(noise_random >> 8u) / 8388608.0f - 1.0f);
        const float tone = 0.2f * tide_sin_f((float)(sample_number++ % 48000u) * (2.0f * 3.14159265f * 440.0f / 48000.0f));
        const float source = speech_source.samples ? speech_source.samples[(sample_number - 1u) % speech_source.frames] : tone;
        const float value = silent_source ? 0.0f : noise_source ? noise : source;
        input[2u * i] = input[2u * i + 1u] = value;
    }
    for (uint32_t i = 0; i < 3; i++) render(&players[i], input);
}

static void steps(const uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) step();
}

static void end(void)
{
    if (mixed_player >= 0) tide_mixer_forget();
    for (uint32_t i = 0; i < 3; i++) {
        tide_voice_client_free(players[i].voice, players[i].session);
        tide_session_destroy(players[i].session);
        tide_audio_free(&players[i].audio);
    }
    tide_loopback_destroy(network);
    tide_sound_free(&speech_source);
    speech_source = (tide_sound){0};
    mixed_player = -1;
}

TIDE_TEST(voice_is_optional_and_push_to_talk_is_local)
{
    begin((tide_net_conditions){0});
    steps(150);
    for (uint32_t i = 0; i < 3; i++) {
        TIDE_CHECK(tide_session_status_of(players[i].session).client.state == TIDE_SESSION_CONNECTED);
        TIDE_CHECK(players[i].captured == 0u && players[i].played == 0u);
    }
    tide_voice_client_configure(players[0].voice, true, true, false, 1.0f);
    tide_voice_client_configure(players[1].voice, true, false, false, 1.0f);
    steps(375);
    TIDE_CHECK(players[0].captured > 300u);
    TIDE_CHECK(players[0].played == 0u); // No local monitoring/echo.
    TIDE_CHECK(players[1].energy > 30.0);
    TIDE_CHECK(players[1].captured == 0u);
    TIDE_CHECK(players[2].played == 0u); // Unsubscribed player receives no audio.
    TIDE_CHECK(tide_session_status_of(players[1].session).client.resyncs == 0u);
    const uint32_t captured = players[0].captured;
    tide_voice_client_configure(players[0].voice, true, false, false, 1.0f);
    steps(120);
    TIDE_CHECK(players[0].captured == captured);
    TIDE_CHECK(!players[0].audio.microphone);
    TIDE_CHECK(!tide_session_send_data(players[0].session, NULL, 1u));
    const uint8_t data[TIDE_LOCAL_DATA_BYTES + 1u] = {0};
    TIDE_CHECK(!tide_session_send_data(players[0].session, data, sizeof data));
    TIDE_CHECK(!tide_session_send_data(players[0].session, data, 0u));
    end();
}

TIDE_TEST(voice_survives_loss_reordering_mute_and_resume)
{
    begin((tide_net_conditions){.latency = 0.015, .jitter = 0.012, .loss = 0.15});
    steps(200);
    tide_voice_client_configure(players[0].voice, true, true, false, 1.0f);
    tide_voice_client_configure(players[1].voice, true, true, false, 1.0f);
    tide_voice_client_configure(players[2].voice, true, false, false, 1.0f);
    steps(600);
    for (uint32_t i = 0; i < 3; i++) TIDE_CHECK(players[i].energy > 20.0);
    const tide_player_id sender = tide_session_status_of(players[0].session).client.player;
    tide_voice_client_volume(players[1].voice, sender, 0.0f);
    const double energy = players[1].energy;
    steps(120);
    TIDE_CHECK(players[1].energy == energy);
    tide_voice_client_configure(players[0].voice, false, false, false, 1.0f);
    steps(120);
    TIDE_CHECK(!players[0].audio.microphone && players[0].audio.voice_count == 0u);
    tide_voice_client_volume(players[1].voice, sender, 1.0f);
    tide_voice_client_configure(players[0].voice, true, true, false, 1.0f);
    steps(250);
    TIDE_CHECK(players[1].energy > energy + 10.0);
    for (uint32_t i = 0; i < 3; i++) TIDE_CHECK(tide_session_status_of(players[i].session).client.resyncs == 0u);
    end();
}

typedef struct received {
    uint32_t count, size;
    tide_player_id sender;
    uint64_t connection;
    uint8_t data[TIDE_LOCAL_DATA_BYTES];
} received;

static void take_data(void *user, const tide_player_id sender, const uint64_t connection,
                       const uint8_t *data, const uint32_t size)
{
    received *r = user;
    r->count++;
    r->size = size;
    r->sender = sender;
    r->connection = connection;
    memcpy(r->data, data, size);
}

static void data_steps(const uint32_t count)
{
    for (uint32_t frame = 0; frame < count; frame++) {
        now += 0.02;
        tide_loopback_set_time(network, now);
        for (uint32_t i = 0; i < 3; i++) tide_session_update(players[i].session, now);
    }
}

static uint32_t raw_packet(uint8_t *data, const uint8_t kind, const uint64_t cookie, const uint32_t size)
{
    tide_writer w = {data, TIDE_NET_MTU, 0, false, false};
    tide_write_u16(&w, 0x5449u);
    tide_write_u8(&w, 17u);
    tide_write_u8(&w, kind);
    tide_write_u64(&w, cookie);
    for (uint32_t i = 0; i < size; i++) tide_write_u8(&w, (uint8_t)i);
    return w.size;
}

TIDE_TEST(voice_session_data_validates_identity_subscription_sizes_and_rate)
{
    begin((tide_net_conditions){0});
    steps(100);
    received received[3] = {0};
    tide_session_listen_data(players[0].session, take_data, &received[0]);
    tide_session_listen_data(players[1].session, take_data, &received[1]);
    data_steps(30);
    const uint8_t payload[] = {255, 254, 253};
    TIDE_CHECK(tide_session_send_data(players[1].session, payload, sizeof payload));
    data_steps(2);
    TIDE_CHECK(received[0].count == 1u && received[0].size == sizeof payload);
    TIDE_CHECK(memcmp(received[0].data, payload, sizeof payload) == 0);
    TIDE_CHECK(tide_player_equal(received[0].sender, tide_session_status_of(players[1].session).client.player));
    TIDE_CHECK(received[0].connection != 0u);
    TIDE_CHECK(received[1].count == 0u && received[2].count == 0u);
    uint8_t data[TIDE_NET_MTU];
    const uint64_t cookie = tide_session_status_of(players[1].session).client.cookie;
    uint32_t size = raw_packet(data, 13u, cookie ^ 1u, 1u);
    endpoints[1].send(endpoints[1].self, tide_loopback_address(1), data, size);
    data_steps(1);
    TIDE_CHECK(received[0].count == 1u);
    size = raw_packet(data, 13u, cookie, TIDE_LOCAL_DATA_BYTES + 1u);
    endpoints[1].send(endpoints[1].self, tide_loopback_address(1), data, size);
    data_steps(1);
    TIDE_CHECK(received[0].count == 1u);
    // Malformed/truncated interest packets cannot unsubscribe another player.
    size = raw_packet(data, 12u, cookie, 1u);
    for (uint32_t cut = 1; cut < size; cut++) endpoints[1].send(endpoints[1].self, tide_loopback_address(1), data, cut);
    data_steps(1);
    size = raw_packet(data, 13u, cookie, 1u);
    for (uint32_t i = 0; i < 100; i++) endpoints[1].send(endpoints[1].self, tide_loopback_address(1), data, size);
    const uint32_t count = received[0].count;
    data_steps(1);
    TIDE_CHECK(received[0].count > count && received[0].count <= count + 4u);
    tide_session_listen_data(players[0].session, NULL, NULL);
    data_steps(2);
    const uint32_t stopped = received[0].count;
    TIDE_CHECK(tide_session_send_data(players[1].session, payload, sizeof payload));
    data_steps(2);
    TIDE_CHECK(received[0].count == stopped);
    end();
}

TIDE_TEST(voice_reconnect_and_replacement_discard_old_capture_and_decoder_state)
{
    begin((tide_net_conditions){0});
    steps(100);
    tide_voice_client_configure(players[0].voice, true, true, false, 1.0f);
    tide_voice_client_configure(players[1].voice, true, false, false, 1.0f);
    steps(250);
    const double energy = players[1].energy;
    const uint64_t generation = tide_session_data_generation(players[1].session);
    tide_session_leave(players[1].session);
    endpoints[1] = tide_loopback_endpoint(network, 4);
    tide_session_join(players[1].session, endpoints[1], tide_loopback_address(1), now);
    TIDE_CHECK(tide_session_data_generation(players[1].session) != generation);
    steps(250);
    TIDE_CHECK(players[1].energy > energy + 10.0);
    // A reloaded sender starts its stream numbering over while peers stay.
    tide_voice_client_free(players[0].voice, players[0].session);
    players[0].voice = tide_voice_client_create();
    tide_voice_client_configure(players[0].voice, true, true, false, 1.0f);
    const double resumed = players[1].energy;
    steps(250);
    TIDE_CHECK(players[1].energy > resumed + 10.0);
    end();
}

TIDE_TEST(voice_initial_player_mute_survives_connection_setup)
{
    begin((tide_net_conditions){0});
    tide_voice_client_volume(players[1].voice, tide_player_from_index(0), 0.0f);
    tide_voice_client_configure(players[0].voice, true, true, false, 1.0f);
    tide_voice_client_configure(players[1].voice, true, false, false, 1.0f);
    steps(350);
    TIDE_CHECK(players[1].played > 0u);
    TIDE_CHECK(players[1].energy == 0.0);
    tide_voice_client_volume(players[1].voice, tide_player_from_index(0), 1.0f);
    steps(150);
    TIDE_CHECK(players[1].energy > 10.0);
    end();
}

TIDE_TEST(voice_noise_suppression_filters_capture_and_can_be_toggled)
{
    begin((tide_net_conditions){0});
    noise_source = true;
    tide_voice_client_configure(players[0].voice, true, true, false, 1.0f);
    tide_voice_client_configure(players[1].voice, true, false, false, 1.0f);
    steps(250);
    players[1].energy = 0;
    steps(375);
    const double raw = players[1].energy;
    TIDE_CHECK(raw > 10.0);
    tide_voice_client_configure(players[0].voice, true, true, true, 1.0f);
    steps(250);
    players[1].energy = 0;
    steps(375);
    TIDE_CHECK(players[1].energy < raw * 0.2);
    TIDE_CHECK(players[0].energy == 0.0 && players[2].played == 0u);
    // Bypass resumes the unfiltered capture while transmission remains held.
    tide_voice_client_configure(players[0].voice, true, true, false, 1.0f);
    steps(250);
    players[1].energy = 0;
    steps(375);
    TIDE_CHECK(players[1].energy > raw * 0.8);
    // A fresh suppressed utterance cannot replay the previous filter's history.
    tide_voice_client_configure(players[0].voice, true, false, true, 1.0f);
    steps(120);
    silent_source = true;
    tide_voice_client_configure(players[0].voice, true, true, true, 1.0f);
    steps(100);
    players[1].energy = 0;
    steps(200);
    TIDE_CHECK(players[1].energy < 0.00001);
    TIDE_CHECK(tide_session_status_of(players[1].session).client.resyncs == 0u);
    end();
}

TIDE_TEST(voice_noise_suppression_preserves_speech)
{
    begin((tide_net_conditions){0});
    TIDE_CHECK(tide_wav_decode(tide_voice_test_speech, tide_voice_test_speech_size, &speech_source));
    TIDE_CHECK(speech_source.rate == TIDE_AUDIO_RATE && speech_source.channels == 1u);
    tide_voice_client_configure(players[0].voice, true, true, false, 1.0f);
    tide_voice_client_configure(players[1].voice, true, false, false, 1.0f);
    steps(250);
    players[1].energy = 0;
    steps(470); // One complete 2.5-second speech loop.
    const double raw = players[1].energy;
    TIDE_CHECK(raw > 10.0);
    tide_voice_client_configure(players[0].voice, true, true, true, 1.0f);
    steps(250);
    players[1].energy = 0;
    steps(470);
    TIDE_CHECK(players[1].energy > raw * 0.25);
    TIDE_CHECK(players[1].energy < raw * 2.0);
    TIDE_CHECK(players[0].energy == 0.0 && players[2].played == 0u);
    end();
}

static void check_receive_bus(const player *p, const uint32_t bus)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < p->audio.voice_count; i++) {
        const tide_voice *voice = &p->audio.voices[i];
        if (voice->clip.microphone || voice->clip.samples) continue;
        TIDE_CHECK(voice->bus == bus);
        count++;
    }
    TIDE_CHECK(count > 0u);
}

TIDE_TEST(voice_receive_buses_process_live_audio_without_affecting_other_sounds)
{
    begin((tide_net_conditions){0});
    mixed_player = 1;
    bus_gain = 0.0f;
    tide_voice_client_configure(players[0].voice, true, true, false, 1.0f);
    tide_voice_client_configure(players[1].voice, true, false, false, 1.0f);
    const tide_player_id sender = tide_player_from_index(0);
    tide_voice_client_bus(players[1].voice, 1);
    // An override set before initial connection setup must survive it.
    tide_voice_client_player_bus(players[1].voice, sender, 2);
    steps(250);
    check_receive_bus(&players[1], 2u);
    players[1].energy = 0;
    steps(120);
    TIDE_CHECK(players[1].energy < 0.00000001);
    bus_gain = 1.0f;
    steps(20);
    players[1].energy = 0;
    steps(120);
    const double full = players[1].energy;
    const uint32_t blocks = effect_blocks;
    TIDE_CHECK(full > 20.0);
    // Updating knobs changes the sound while preserving the same effect state.
    bus_gain = 0.25f;
    steps(20);
    players[1].energy = 0;
    steps(120);
    TIDE_CHECK(players[1].energy > full * 0.04 && players[1].energy < full * 0.09);
    TIDE_CHECK(effect_blocks > blocks);
    // Clear the override: the same speaker follows the clean default bus.
    tide_voice_client_player_bus(players[1].voice, sender, -1);
    bus_gain = 0.0f;
    steps(20);
    check_receive_bus(&players[1], 1u);
    players[1].energy = 0;
    steps(120);
    TIDE_CHECK(players[1].energy > full * 0.8);
    // Changing the default re-routes an already playing speaker immediately.
    tide_voice_client_bus(players[1].voice, 2);
    steps(20);
    check_receive_bus(&players[1], 2u);
    players[1].energy = 0;
    steps(120);
    TIDE_CHECK(players[1].energy < 0.00000001);
    tide_voice_client_player_bus(players[1].voice, sender, INT32_MAX);
    steps(5);
    check_receive_bus(&players[1], 0u);
    tide_voice_client_player_bus(players[1].voice, sender, -1);
    tide_voice_client_bus(players[1].voice, INT32_MIN);
    steps(5);
    check_receive_bus(&players[1], 0u);
    // Reconnect clears a player override but keeps this machine's default bus.
    tide_voice_client_bus(players[1].voice, 1);
    tide_voice_client_player_bus(players[1].voice, sender, 2);
    tide_session_leave(players[1].session);
    endpoints[1] = tide_loopback_endpoint(network, 4);
    tide_session_join(players[1].session, endpoints[1], tide_loopback_address(1), now);
    steps(250);
    check_receive_bus(&players[1], 1u);
    TIDE_CHECK(tide_session_status_of(players[1].session).client.resyncs == 0u);
    end();
}
