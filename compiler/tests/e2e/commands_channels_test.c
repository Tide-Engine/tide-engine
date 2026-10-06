#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

// Commands in channels, unordered commands and big commands (see
// docs/spec.md, Commands): what the generated game says of each, and what
// sessions do with them on a loopback network that's slow and loses
// datagrams, in made-up time.

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_devices devices;

enum { UPLOADS = 1, CHAT = 2 }; // The channels [Channel] names, numbered after the default
#define BIG 3000000u            // Bytes of the big command the tests send

static void start(void)
{
    tide_world_init(&world, 1.0f / 60.0f);
    tide_local_init(&local);
    tide_local_set_session(&local, TIDE_SESSION_CONNECTED, tide_player_from_index(0), 0, false, false, "");
}

static void frame(void)
{
    tide_draw_reset(&draw);
    tide_gui_begin(&gui, &devices, tide_f2(640, 480), NULL);
    tide_frame(&world, NULL, 1, &world, &local, &draw, &gui);
    tide_gui_end(&gui, &draw);
}

// The oldest command waiting in `channel`, as a copy to free(), and its
// channel as the game reads it off its bytes
static uint8_t *take(const uint32_t channel, uint32_t *size)
{
    const uint8_t *data = tide_local_command(&local, channel, size);
    if (!data) return NULL;
    uint8_t *copy = malloc(*size);
    memcpy(copy, data, *size);
    uint32_t own = 12345;
    TIDE_CHECK(tide_game_api.check_command(copy, *size, &own));
    TIDE_CHECK(own == channel);
    tide_local_command_sent(&local, channel);
    return copy;
}

// What OnUpload makes of the bytes the view sends: byte i is i * 7 + 3
static int32_t upload_hash(const uint32_t count)
{
    uint32_t hash = 0;
    for (uint32_t i = 0; i < count; i++) hash = hash * 31u + (uint8_t)(i * 7u + 3u);
    return (int32_t)hash;
}

TIDE_TEST(commands_channels_are_the_types_own_and_numbered_in_order)
{
    start();
    TIDE_CHECK(tide_game_api.command_channels == 3u); // The default, Uploads and Chat
    TIDE_CHECK(tide_game_api.command_applied != NULL); // Ping is unordered
    local.UI.say = true;
    local.UI.whisper = true;
    local.UI.ping = true;
    local.UI.pingAt = 9;
    local.UI.upload = true;
    local.UI.uploadSize = 4;
    frame();
    uint32_t sizes[4];
    uint8_t *say = take(0, &sizes[0]);
    uint8_t *upload = take(UPLOADS, &sizes[1]);
    uint8_t *whisper = take(CHAT, &sizes[2]);
    uint8_t *ping = take(TIDE_COMMAND_UNORDERED, &sizes[3]);
    TIDE_REQUIRE(say && upload && whisper && ping);
    uint32_t none;
    TIDE_CHECK(tide_local_command(&local, 0, &none) == NULL && tide_local_command(&local, UPLOADS, &none) == NULL);
    TIDE_CHECK(tide_local_command(&local, CHAT, &none) == NULL && tide_local_command(&local, TIDE_COMMAND_UNORDERED, &none) == NULL);
    // Each channel's sequence is its own, and a command only goes in its own channel
    const tide_player_id p0 = tide_player_from_index(0);
    TIDE_CHECK(!tide_game_api.command(&world, p0, CHAT, 1, say, sizes[0]));
    TIDE_CHECK(!tide_game_api.command(&world, p0, TIDE_COMMAND_UNORDERED, 1, say, sizes[0]));
    TIDE_CHECK(tide_game_api.command(&world, p0, 0, 1, say, sizes[0]));
    TIDE_CHECK(tide_game_api.command(&world, p0, CHAT, 1, whisper, sizes[2]));
    TIDE_CHECK(!tide_game_api.command(&world, p0, UPLOADS, 2, upload, sizes[1])); // After a first that never came
    TIDE_CHECK(tide_game_api.command(&world, p0, UPLOADS, 1, upload, sizes[1]));
    TIDE_CHECK(tide_game_api.command_sequence(&world, p0, 0) == 1);
    TIDE_CHECK(tide_game_api.command_sequence(&world, p0, UPLOADS) == 1);
    TIDE_CHECK(tide_game_api.command_sequence(&world, p0, CHAT) == 1);
    TIDE_CHECK(tide_game_api.command_sequence(&world, p0, 3) == 0); // No such channel
    TIDE_CHECK(tide_game_api.command_sequence(&world, tide_player_from_index(1), 0) == 0);
    // An unordered command's id goes in a window: each once, in any order,
    // and one older than the window is taken as applied
    TIDE_CHECK(!tide_game_api.command(&world, p0, 0, 2, ping, sizes[3]));
    TIDE_CHECK(!tide_game_api.command(&world, p0, TIDE_COMMAND_UNORDERED, 0, ping, sizes[3])); // 0 is no id
    TIDE_CHECK(tide_game_api.command(&world, p0, TIDE_COMMAND_UNORDERED, 1, ping, sizes[3]));
    TIDE_CHECK(tide_game_api.command(&world, p0, TIDE_COMMAND_UNORDERED, 3, ping, sizes[3]));
    TIDE_CHECK(!tide_game_api.command_applied(&world, p0, 2));
    TIDE_CHECK(tide_game_api.command(&world, p0, TIDE_COMMAND_UNORDERED, 2, ping, sizes[3]));
    TIDE_CHECK(tide_game_api.command_applied(&world, p0, 2) && tide_game_api.command_applied(&world, p0, 3));
    TIDE_CHECK(!tide_game_api.command(&world, p0, TIDE_COMMAND_UNORDERED, 2, ping, sizes[3])); // Again: dropped
    TIDE_CHECK(tide_game_api.command_sequence(&world, p0, TIDE_COMMAND_UNORDERED) == 3);
    TIDE_CHECK(tide_game_api.command(&world, p0, TIDE_COMMAND_UNORDERED, 70, ping, sizes[3]));
    TIDE_CHECK(tide_game_api.command_applied(&world, p0, 5));  // 65 back: too old to tell, so taken as applied
    TIDE_CHECK(!tide_game_api.command_applied(&world, p0, 7)); // 63 back: in the window, and not applied
    TIDE_CHECK(!tide_game_api.command(&world, p0, TIDE_COMMAND_UNORDERED, 5, ping, sizes[3]));
    TIDE_CHECK(tide_game_api.command(&world, p0, TIDE_COMMAND_UNORDERED, 7, ping, sizes[3]));
    TIDE_CHECK(!tide_game_api.command_applied(&world, tide_player_from_index(1), 7)); // Another player's window
    tide_world_tick(&world);
    TIDE_CHECK(world.Log.says == 1 && world.Log.whispers == 1 && world.Log.uploads == 1);
    TIDE_CHECK(world.Log.uploadBytes == 4 && world.Log.uploadHash == upload_hash(4));
    TIDE_CHECK(world.Log.pings == 5 && world.Log.pingSum == 45);
    TIDE_CHECK(world.Log.duplicates == 4); // Five commands, each with `at` 9: the same bytes are five commands
    TIDE_CHECK(strcmp(tide_str_c(tide_text_read(&world.heap, world.Log.order)), "SWU") == 0);
    // The window is the world's: a copy has it, and a world from bytes
    tide_world copy = {0};
    tide_world_copy(&copy, &world);
    TIDE_CHECK(tide_game_api.command_applied(&copy, p0, 70) && !tide_game_api.command_applied(&copy, p0, 8));
    TIDE_CHECK(tide_game_api.command_sequence(&copy, p0, CHAT) == 1);
    TIDE_CHECK(tide_world_hash(&copy) == tide_world_hash(&world));
    const uint32_t packed_size = tide_world_pack(&world, NULL, 0);
    uint8_t *packed = malloc(packed_size);
    TIDE_REQUIRE(tide_world_pack(&world, packed, packed_size) == packed_size);
    tide_world restored = {0};
    TIDE_REQUIRE(tide_world_unpack(&restored, packed, packed_size));
    TIDE_CHECK(tide_game_api.command_applied(&restored, p0, 7) && tide_game_api.command_sequence(&restored, p0, UPLOADS) == 1);
    TIDE_CHECK(!tide_game_api.command(&restored, p0, TIDE_COMMAND_UNORDERED, 70, ping, sizes[3]));
    free(packed);
    tide_world_free(&copy);
    tide_world_free(&restored);
    free(say);
    free(upload);
    free(whisper);
    free(ping);
}

TIDE_TEST(commands_a_big_command_packs_its_bytes_whole_and_lands_in_the_match)
{
    start();
    local.UI.upload = true;
    local.UI.uploadSize = BIG;
    frame();
    uint32_t size = 0;
    uint8_t *upload = take(UPLOADS, &size);
    TIDE_REQUIRE(upload);
    TIDE_CHECK(size > BIG && size < BIG + 32u);
    TIDE_CHECK(!tide_game_api.check_command(upload, size - 1u, NULL)); // Cut short
    const tide_player_id p0 = tide_player_from_index(0);
    TIDE_CHECK(tide_game_api.command(&world, p0, UPLOADS, 1, upload, size));
    tide_world_tick(&world);
    TIDE_CHECK(world.Log.uploads == 1 && world.Log.uploadBytes == (int32_t)BIG);
    TIDE_CHECK(world.Log.uploadHash == upload_hash(BIG));
    // The outbox keeps a big command through the local state's bytes too
    local.UI.upload = true;
    local.UI.uploadSize = 5000;
    frame();
    const uint32_t packed_size = tide_local_pack(&local, NULL, 0);
    uint8_t *packed = malloc(packed_size);
    TIDE_REQUIRE(tide_local_pack(&local, packed, packed_size) == packed_size);
    tide_local restored = {0};
    TIDE_REQUIRE(tide_local_unpack(&restored, packed, packed_size));
    uint32_t again_size = 0;
    const uint8_t *again = tide_local_command(&restored, UPLOADS, &again_size);
    TIDE_REQUIRE(again && again_size > 5000u);
    uint32_t mine = 0;
    const uint8_t *waiting = tide_local_command(&local, UPLOADS, &mine);
    TIDE_CHECK(waiting && mine == again_size && memcmp(waiting, again, mine) == 0);
    tide_local_free(&restored);
    free(packed);
    free(upload);
}

// ---------------------------------------------------------------------------
// Sessions on a loopback network, in made-up time

static tide_loopback *net;
static tide_server *server;
static tide_client *clients[3];
static int who[3] = {0, 1, 2};
static double now;

static void sample(void *user, const uint32_t tick, void *input)
{
    const int player = *(const int *)user;
    Controls *c = input;
    *c = (Controls){0};
    c->fire = (tick + (uint32_t)player * 7u) % 45u < 5u;
}

static tide_client *join(const int number)
{
    const tide_client_desc desc = {
        .game = &tide_game_api,
        .transport = tide_loopback_endpoint(net, (uint32_t)number + 2u),
        .server = tide_loopback_address(1),
        .sample = sample,
        .user = &who[number],
        .lead = 2,
    };
    clients[number] = tide_client_create(&desc, now);
    return clients[number];
}

static void net_start(const tide_net_conditions conditions, const uint64_t seed)
{
    now = 0.0;
    net = tide_loopback_create(seed);
    tide_loopback_set_conditions(net, conditions);
    const tide_server_desc desc = {.game = &tide_game_api, .tick_rate = 60, .transports = {tide_loopback_endpoint(net, 1)}};
    server = tide_server_create(&desc, now);
    memset(clients, 0, sizeof clients);
}

// Runs everything `seconds` on, in frames of a 60th of a second
static void run(const double seconds)
{
    const double until = now + seconds;
    while (now < until) {
        now += 1.0 / 60.0;
        tide_loopback_set_time(net, now);
        for (int i = 0; i < 3; i++) {
            if (clients[i]) tide_client_update(clients[i], now);
        }
        tide_server_update(server, now);
    }
}

static void net_finish(void)
{
    for (int i = 0; i < 3; i++) {
        if (clients[i]) tide_client_destroy(clients[i]);
    }
    tide_server_destroy(server);
    tide_loopback_destroy(net);
}

// Sends what the view queued in `channel` through `client`: all of it
static void send_channel(tide_client *client, const uint32_t channel)
{
    uint32_t size = 0;
    uint8_t *bytes;
    while ((bytes = take(channel, &size)) != NULL) {
        TIDE_CHECK(tide_client_send_command(client, bytes, size));
        free(bytes);
    }
}

// A client in the match, whose world never went wrong, and which keeps up
// with the server: its verified tick is within half a second of the server's,
// so no tick of its waited for a transfer
static void check_in_step(const tide_client *client)
{
    const tide_client_status status = tide_client_status_of(client);
    TIDE_CHECK(status.state == TIDE_SESSION_CONNECTED);
    TIDE_CHECK(status.resyncs == 0);
    TIDE_CHECK(tide_server_tick(server) - status.verified_tick <= 30u);
}

// ...and, on a network that loses nothing, none of whose inputs came late
// since it settled in (a player joining samples inputs for ticks that can't
// arrive in time, which `late_before` counts): what it sampled waited at the
// server as it should, a transfer or not
static uint32_t late_before[3];

static void settled(void)
{
    for (int i = 0; i < 3; i++) late_before[i] = clients[i] ? tide_client_status_of(clients[i]).late_ticks : 0u;
}

static void check_on_time(const int number)
{
    check_in_step(clients[number]);
    const tide_client_status status = tide_client_status_of(clients[number]);
    TIDE_CHECK(status.late_ticks == late_before[number]);
    TIDE_CHECK(status.early_ms >= 0.0f);
}

TIDE_TEST(commands_a_chat_line_never_waits_behind_an_upload_which_delays_no_tick)
{
    start();
    net_start((tide_net_conditions){.latency = 0.05, .jitter = 0.02, .loss = 0.1}, 11);
    join(0);
    join(1);
    run(2.0);
    for (int i = 0; i < 2; i++) TIDE_REQUIRE(tide_client_status_of(clients[i]).state == TIDE_SESSION_CONNECTED);
    const uint32_t tick_before = tide_server_tick(server);
    const double now_before = now;
    // The upload first, then a Say and a Whisper right after it
    local.UI.upload = true;
    local.UI.uploadSize = BIG;
    local.UI.say = true;
    local.UI.whisper = true;
    frame();
    send_channel(clients[0], UPLOADS);
    send_channel(clients[0], 0);
    send_channel(clients[0], CHAT);
    run(0.5);
    const tide_world *authoritative = tide_server_world(server);
    TIDE_CHECK(authoritative->Log.says == 1 && authoritative->Log.whispers == 1); // Within half a second
    TIDE_CHECK(authoritative->Log.uploads == 0);                                  // ...while the upload is on its way
    double took = 0.5;
    while (authoritative->Log.uploads == 0 && took < 60.0) {
        run(0.5);
        took += 0.5;
        for (int i = 0; i < 2; i++) check_in_step(clients[i]); // Nobody waits for it meanwhile
    }
    TIDE_REQUIRE(authoritative->Log.uploads == 1);
    TIDE_CHECK(took < 20.0); // About 3 MB at a megabyte a second, with a tenth of it lost
    TIDE_CHECK(authoritative->Log.uploadBytes == (int32_t)BIG && authoritative->Log.uploadHash == upload_hash(BIG));
    TIDE_CHECK(strcmp(tide_str_c(tide_text_read(&authoritative->heap, authoritative->Log.order)), "SWU") == 0
               || strcmp(tide_str_c(tide_text_read(&authoritative->heap, authoritative->Log.order)), "WSU") == 0);
    // The server ticked at its pace all along: a tick a 60th of a second
    const uint32_t ticks = tide_server_tick(server) - tick_before;
    const uint32_t expected = (uint32_t)((now - now_before) * 60.0 + 0.5);
    TIDE_CHECK(ticks + 2u >= expected && ticks <= expected + 2u);
    run(2.0); // Both players' verified worlds have it, nobody fell behind
    for (int i = 0; i < 2; i++) {
        check_in_step(clients[i]);
        const tide_world *verified = tide_client_verified_world(clients[i]);
        TIDE_REQUIRE(verified);
        TIDE_CHECK(verified->Log.uploads == 1 && verified->Log.uploadHash == upload_hash(BIG));
        TIDE_CHECK(tide_game_api.command_sequence(verified, tide_client_status_of(clients[0]).player, UPLOADS) == 1);
        TIDE_CHECK(tide_game_api.command_sequence(verified, tide_client_status_of(clients[0]).player, CHAT) == 1);
        TIDE_CHECK(tide_game_api.command_sequence(verified, tide_client_status_of(clients[0]).player, 0) == 1);
    }
    // A late joiner gets the sequences with the world, and goes on from them
    join(2);
    run(2.0);
    TIDE_REQUIRE(tide_client_status_of(clients[2]).state == TIDE_SESSION_CONNECTED);
    const tide_world *late = tide_client_verified_world(clients[2]);
    TIDE_REQUIRE(late);
    TIDE_CHECK(late->Log.uploads == 1 && late->Log.uploadHash == upload_hash(BIG));
    TIDE_CHECK(tide_game_api.command_sequence(late, tide_client_status_of(clients[0]).player, UPLOADS) == 1);
    local.UI.say = true;
    tide_text_use(&world.heap, &local.heap);
    tide_text_set(&local.UI.text, tide_str_from_cstr("late"), TIDE_IN_LOCAL);
    frame();
    send_channel(clients[2], 0);
    run(1.0);
    TIDE_CHECK(authoritative->Log.says == 2);
    TIDE_CHECK(strcmp(tide_str_c(tide_text_read(&authoritative->heap, authoritative->Log.last)), "late") == 0);
    TIDE_CHECK(tide_game_api.command_sequence(authoritative, tide_client_status_of(clients[2]).player, 0) == 1);
    for (int i = 0; i < 3; i++) check_in_step(clients[i]);
    net_finish();
}

// With nothing lost, no input is ever late, a 3 MB upload or not: the
// transfer goes under the inputs, and the tick it lands in waits for
// nobody's download, as every player had it first
TIDE_TEST(commands_a_big_command_delays_no_tick_and_makes_no_input_late)
{
    start();
    net_start((tide_net_conditions){.latency = 0.05, .jitter = 0.02}, 13);
    join(0);
    join(1);
    run(2.0);
    for (int i = 0; i < 2; i++) TIDE_REQUIRE(tide_client_status_of(clients[i]).state == TIDE_SESSION_CONNECTED);
    settled();
    local.UI.upload = true;
    local.UI.uploadSize = BIG;
    frame();
    send_channel(clients[0], UPLOADS);
    const tide_world *authoritative = tide_server_world(server);
    double took = 0.0;
    while (authoritative->Log.uploads == 0 && took < 30.0) {
        run(0.5);
        took += 0.5;
        for (int i = 0; i < 2; i++) check_on_time(i);
    }
    TIDE_REQUIRE(authoritative->Log.uploads == 1);
    TIDE_CHECK(took >= 2.0 && took < 8.0); // 3 MB at about a megabyte a second, then down to both and back
    run(2.0);
    for (int i = 0; i < 2; i++) {
        check_on_time(i);
        const tide_world *verified = tide_client_verified_world(clients[i]);
        TIDE_REQUIRE(verified);
        TIDE_CHECK(verified->Log.uploads == 1 && verified->Log.uploadHash == upload_hash(BIG));
    }
    net_finish();
}

TIDE_TEST(commands_unordered_ones_are_handled_exactly_once_under_loss_and_reordering)
{
    start();
    net_start((tide_net_conditions){.latency = 0.08, .jitter = 0.06, .loss = 0.3}, 12);
    join(0);
    join(1);
    run(3.0);
    for (int i = 0; i < 2; i++) TIDE_REQUIRE(tide_client_status_of(clients[i]).state == TIDE_SESSION_CONNECTED);
    for (int at = 1; at <= 20; at++) {
        local.UI.ping = true;
        local.UI.pingAt = at;
        frame();
        send_channel(clients[at % 2], TIDE_COMMAND_UNORDERED);
    }
    run(6.0);
    const tide_world *authoritative = tide_server_world(server);
    TIDE_CHECK(authoritative->Log.pings == 20 && authoritative->Log.duplicates == 0 && authoritative->Log.pingSum == 210);
    for (int i = 0; i < 2; i++) {
        check_in_step(clients[i]);
        const tide_world *verified = tide_client_verified_world(clients[i]);
        TIDE_REQUIRE(verified);
        TIDE_CHECK(verified->Log.pings == 20 && verified->Log.duplicates == 0);
        TIDE_CHECK(tide_game_api.command_sequence(verified, tide_client_status_of(clients[i]).player, TIDE_COMMAND_UNORDERED) == 10);
    }
    net_finish();
}

// ---------------------------------------------------------------------------
// Host migration: sessions, as hosts run them

static tide_loopback *migration_net;
static tide_session *sessions[3];
static double migration_now;

static void migration_run(const double seconds)
{
    const double until = migration_now + seconds;
    while (migration_now < until) {
        migration_now += 0.01;
        tide_loopback_set_time(migration_net, migration_now);
        for (int i = 0; i < 3; i++) tide_session_update(sessions[i], migration_now);
    }
}

static void migration_setup(void)
{
    migration_now = 0;
    migration_net = tide_loopback_create(367);
    for (int i = 0; i < 3; i++) sessions[i] = tide_session_create(&(tide_session_desc){.game = &tide_game_api});
    tide_session_start(sessions[0], NULL, 0);
    tide_session_open(sessions[0], tide_loopback_endpoint(migration_net, 1));
    tide_session_set_room(sessions[0], "ABCDEF", "key");
    for (int i = 0; i < 2; i++) {
        tide_session_join(sessions[i + 1], tide_loopback_endpoint(migration_net, (uint32_t)i + 2u), tide_loopback_address(1), 0);
    }
    migration_run(2.0); // The host's handover record has reached both players
}

static void migration_finish(void)
{
    for (int i = 0; i < 3; i++) tide_session_destroy(sessions[i]);
    tide_loopback_destroy(migration_net);
}

static void session_send(tide_session *s, const uint32_t channel)
{
    uint32_t size = 0;
    uint8_t *bytes;
    while ((bytes = take(channel, &size)) != NULL) {
        TIDE_CHECK(tide_session_send_command(s, bytes, size));
        free(bytes);
    }
}

TIDE_TEST(commands_a_handover_keeps_every_channels_sequences_and_the_window)
{
    start();
    migration_setup();
    for (int i = 0; i < 3; i++) TIDE_REQUIRE(tide_session_status_of(sessions[i]).client.state == TIDE_SESSION_CONNECTED);
    const tide_player_id first = tide_session_status_of(sessions[1]).client.player;
    local.UI.upload = true;
    local.UI.uploadSize = 200000;
    local.UI.whisper = true;
    local.UI.ping = true;
    local.UI.pingAt = 5;
    frame();
    session_send(sessions[1], UPLOADS);
    session_send(sessions[1], CHAT);
    session_send(sessions[1], TIDE_COMMAND_UNORDERED);
    migration_run(3.0);
    const tide_world *old = tide_session_server_world(sessions[0]);
    TIDE_REQUIRE(old);
    TIDE_CHECK(old->Log.uploads == 1 && old->Log.whispers == 1 && old->Log.pings == 1);
    TIDE_CHECK(old->Log.uploadHash == upload_hash(200000));
    tide_session_leave(sessions[0]);
    migration_run(0.1);
    char code[TIDE_ROOM_CODE_LENGTH + 1];
    char key[TIDE_ROOM_KEY_LENGTH + 1];
    TIDE_REQUIRE(tide_session_migrating(sessions[1], code, key));
    TIDE_REQUIRE(tide_session_migrating(sessions[2], code, key));
    tide_session_take_over(sessions[2], tide_loopback_endpoint(migration_net, 4), migration_now);
    tide_session_join(sessions[1], tide_loopback_endpoint(migration_net, 5), tide_loopback_address(4), migration_now);
    migration_run(2.0);
    const tide_world *next = tide_session_server_world(sessions[2]);
    TIDE_REQUIRE(next);
    TIDE_CHECK(next->Log.uploads == 1 && next->Log.whispers == 1 && next->Log.pings == 1);
    TIDE_CHECK(tide_game_api.command_sequence(next, first, UPLOADS) == 1);
    TIDE_CHECK(tide_game_api.command_sequence(next, first, CHAT) == 1);
    TIDE_CHECK(tide_game_api.command_sequence(next, first, 0) == 0);
    TIDE_CHECK(tide_game_api.command_applied(next, first, 1));
    TIDE_CHECK(tide_session_status_of(sessions[1]).client.resyncs == 0);
    // The new host takes the next of each, from where the old one left them: a
    // big one goes up to it in chunks, an unordered one with its next id
    local.UI.upload = true;
    local.UI.uploadSize = 100000;
    local.UI.whisper = true;
    local.UI.ping = true;
    local.UI.pingAt = 6;
    frame();
    session_send(sessions[1], UPLOADS);
    session_send(sessions[1], CHAT);
    session_send(sessions[1], TIDE_COMMAND_UNORDERED);
    migration_run(3.0);
    TIDE_CHECK(next->Log.uploads == 2 && next->Log.whispers == 2 && next->Log.pings == 2 && next->Log.duplicates == 0);
    TIDE_CHECK(next->Log.uploadHash == upload_hash(100000));
    TIDE_CHECK(tide_game_api.command_sequence(next, first, UPLOADS) == 2);
    TIDE_CHECK(tide_game_api.command_sequence(next, first, TIDE_COMMAND_UNORDERED) == 2);
    TIDE_CHECK(tide_session_status_of(sessions[1]).client.resyncs == 0);
    migration_finish();
}
