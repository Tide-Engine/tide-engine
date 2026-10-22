#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "voice.h"
#include "tide_test.h"

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

static void begin(const tide_net_conditions conditions)
{
    memset(players, 0, sizeof players);
    now = 0;
    sample_number = 0;
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
        const float value = 0.2f * tide_sin_f((float)(sample_number++ % 48000u) * (2.0f * 3.14159265f * 440.0f / 48000.0f));
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
    for (uint32_t i = 0; i < 3; i++) {
        tide_voice_client_free(players[i].voice, players[i].session);
        tide_session_destroy(players[i].session);
        tide_audio_free(&players[i].audio);
    }
    tide_loopback_destroy(network);
}

TIDE_TEST(voice_is_optional_and_push_to_talk_is_local)
{
    begin((tide_net_conditions){0});
    steps(150);
    for (uint32_t i = 0; i < 3; i++) {
        TIDE_CHECK(tide_session_status_of(players[i].session).client.state == TIDE_SESSION_CONNECTED);
        TIDE_CHECK(players[i].captured == 0u && players[i].played == 0u);
    }
    tide_voice_client_configure(players[0].voice, true, true, 1.0f);
    tide_voice_client_configure(players[1].voice, true, false, 1.0f);
    steps(375);
    TIDE_CHECK(players[0].captured > 300u);
    TIDE_CHECK(players[0].played == 0u); // No local monitoring/echo.
    TIDE_CHECK(players[1].energy > 30.0);
    TIDE_CHECK(players[1].captured == 0u);
    TIDE_CHECK(players[2].played == 0u); // Unsubscribed player receives no audio.
    TIDE_CHECK(tide_session_status_of(players[1].session).client.resyncs == 0u);
    const uint32_t captured = players[0].captured;
    tide_voice_client_configure(players[0].voice, true, false, 1.0f);
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
    tide_voice_client_configure(players[0].voice, true, true, 1.0f);
    tide_voice_client_configure(players[1].voice, true, true, 1.0f);
    tide_voice_client_configure(players[2].voice, true, false, 1.0f);
    steps(600);
    for (uint32_t i = 0; i < 3; i++) TIDE_CHECK(players[i].energy > 20.0);
    const tide_player_id sender = tide_session_status_of(players[0].session).client.player;
    tide_voice_client_volume(players[1].voice, sender, 0.0f);
    const double energy = players[1].energy;
    steps(120);
    TIDE_CHECK(players[1].energy == energy);
    tide_voice_client_configure(players[0].voice, false, false, 1.0f);
    steps(120);
    TIDE_CHECK(!players[0].audio.microphone && players[0].audio.voice_count == 0u);
    tide_voice_client_volume(players[1].voice, sender, 1.0f);
    tide_voice_client_configure(players[0].voice, true, true, 1.0f);
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
    tide_voice_client_configure(players[0].voice, true, true, 1.0f);
    tide_voice_client_configure(players[1].voice, true, false, 1.0f);
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
    tide_voice_client_configure(players[0].voice, true, true, 1.0f);
    const double resumed = players[1].energy;
    steps(250);
    TIDE_CHECK(players[1].energy > resumed + 10.0);
    end();
}

TIDE_TEST(voice_initial_player_mute_survives_connection_setup)
{
    begin((tide_net_conditions){0});
    tide_voice_client_volume(players[1].voice, tide_player_from_index(0), 0.0f);
    tide_voice_client_configure(players[0].voice, true, true, 1.0f);
    tide_voice_client_configure(players[1].voice, true, false, 1.0f);
    steps(350);
    TIDE_CHECK(players[1].played > 0u);
    TIDE_CHECK(players[1].energy == 0.0);
    tide_voice_client_volume(players[1].voice, tide_player_from_index(0), 1.0f);
    steps(150);
    TIDE_CHECK(players[1].energy > 10.0);
    end();
}
