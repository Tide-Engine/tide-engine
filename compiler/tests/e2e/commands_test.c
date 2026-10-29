#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_devices devices;
static const tide_entity first = {1u, 1u};
static const tide_entity second = {2u, 1u};

static void start(void)
{
    tide_world_init(&world, 1.0f / 60.0f);
    tide_local_init(&local);
    tide_local_set_session(&local, TIDE_SESSION_CONNECTED, tide_player_from_index(0), 0, false, false, "");
    local.UI.target = first;
}

static void frame(void)
{
    tide_draw_reset(&draw);
    tide_gui_begin(&gui, &devices, tide_f2(640, 480), NULL);
    tide_frame(&world, NULL, 1, &world, NULL, 1.0f, &local, &draw, &gui);
    tide_gui_end(&gui, &draw);
}

// The oldest command waiting in the default channel, copied out
static uint32_t pending(uint8_t bytes[TIDE_COMMAND_BYTES])
{
    uint32_t size = 0;
    const uint8_t *data = tide_local_command(&local, 0, &size);
    TIDE_CHECK(data && size > 0 && size <= TIDE_COMMAND_BYTES);
    if (!data || !size || size > TIDE_COMMAND_BYTES) return 0;
    memcpy(bytes, data, size);
    uint32_t channel = 7;
    TIDE_CHECK(tide_game_api.check_command(bytes, size, &channel));
    TIDE_CHECK(channel == 0);
    return size;
}

TIDE_TEST(commands_view_submission_copies_text_and_waits_for_match_tick)
{
    start();
    local.UI.send = true;
    frame();
    uint8_t bytes[TIDE_COMMAND_BYTES];
    const uint32_t size = pending(bytes);
    // The next frame and changes to the field cannot invalidate submitted bytes.
    tide_text_use(&world.heap, &local.heap);
    tide_text_set(&local.UI.text, tide_str_from_cstr("changed"), TIDE_IN_LOCAL);
    frame();
    uint32_t again_size = 0;
    const uint8_t *again = tide_local_command(&local, 0, &again_size);
    TIDE_REQUIRE(again && again_size == size);
    TIDE_CHECK(memcmp(bytes, again, size) == 0);
    TIDE_REQUIRE(tide_game_api.command(&world, tide_player_from_index(0), 0, 1, bytes, size));
    TIDE_CHECK(world.Seen.calls == 0);
    TIDE_CHECK(tide_game_api.command_sequence(&world, tide_player_from_index(0), 0) == 1);
    tide_world_tick(&world);
    TIDE_CHECK(world.Seen.calls == 1);
    TIDE_CHECK(strcmp(tide_str_c(tide_text_read(&world.heap, tide_get_Name(&world, first).value)), "Alice") == 0);
    TIDE_CHECK(world.Seen.count == -2);
    TIDE_CHECK(world.Seen.claimed.id == 8);
    TIDE_CHECK(tide_entity_equal(world.Seen.other, first));
    TIDE_CHECK(strcmp(tide_str_c(tide_text_read(&world.heap, world.Seen.note)), "view") == 0);
    tide_local_command_sent(&local, 0);
    uint32_t left = 1;
    TIDE_CHECK(tide_local_command(&local, 0, &left) == NULL && left == 0);
}

TIDE_TEST(commands_sender_is_supplied_separately_and_guard_returns_reject)
{
    start();
    local.UI.target = second;
    local.UI.send = true;
    frame();
    uint8_t bytes[TIDE_COMMAND_BYTES];
    const uint32_t size = pending(bytes);
    TIDE_REQUIRE(tide_game_api.command(&world, tide_player_from_index(0), 0, 1, bytes, size));
    tide_world_tick(&world);
    TIDE_CHECK(world.Seen.calls == 0);
    TIDE_CHECK(strcmp(tide_str_c(tide_text_read(&world.heap, tide_get_Name(&world, second).value)), "before") == 0);
    // Consumed IDs include application-level guard returns; resending can't run again.
    TIDE_CHECK(!tide_game_api.command(&world, tide_player_from_index(0), 0, 1, bytes, size));
    TIDE_CHECK(!tide_game_api.command(&world, tide_player_from_index(-1), 0, 1, bytes, size));
    TIDE_CHECK(!tide_game_api.command(&world, tide_player_from_index(TIDE_GAME_PLAYERS), 0, 1, bytes, size));
}

TIDE_TEST(commands_world_submission_and_local_handler_submission)
{
    start();
    local.UI.announce = true;
    frame();
    uint8_t bytes[TIDE_COMMAND_BYTES];
    uint32_t size = pending(bytes);
    TIDE_REQUIRE(tide_game_api.command(&world, tide_player_from_index(0), 0, 1, bytes, size));
    tide_world_tick(&world);
    TIDE_CHECK(strcmp(tide_str_c(tide_text_read(&world.heap, world.Seen.announcement)), "Alice") == 0);
    tide_local_command_sent(&local, 0);
    local.UI.handler = true;
    frame();
    size = pending(bytes);
    TIDE_REQUIRE(tide_game_api.command(&world, tide_player_from_index(0), 0, 2, bytes, size));
    tide_world_tick(&world);
    TIDE_CHECK(world.Seen.calls == 1);
    TIDE_CHECK(world.Seen.count == 2);
    TIDE_CHECK(strcmp(tide_str_c(tide_text_read(&world.heap, world.Seen.note)), "handler") == 0);
}

TIDE_TEST(commands_wire_rejects_truncation_unknown_types_trailing_bytes_and_bad_utf8)
{
    start();
    local.UI.send = true;
    frame();
    uint8_t bytes[TIDE_COMMAND_BYTES];
    const uint32_t size = pending(bytes);
    for (uint32_t n = 0; n < size; n++) TIDE_CHECK(!tide_game_api.check_command(bytes, n, NULL));
    uint8_t altered[TIDE_COMMAND_BYTES];
    memcpy(altered, bytes, size);
    altered[0] = 255; altered[1] = 255;
    TIDE_CHECK(!tide_game_api.check_command(altered, size, NULL));
    memcpy(altered, bytes, size); altered[size] = 0;
    TIDE_CHECK(!tide_game_api.check_command(altered, size + 1, NULL));
    memcpy(altered, bytes, size); altered[12] = 0xC0;
    TIDE_CHECK(!tide_game_api.check_command(altered, size, NULL));
    memcpy(altered, bytes, size); altered[10] = 255; altered[11] = 255;
    TIDE_CHECK(!tide_game_api.check_command(altered, size, NULL));
    TIDE_CHECK(tide_game_api.command_sequence(&world, tide_player_from_index(0), 0) == 0);
}

TIDE_TEST(commands_delivery_ids_survive_copy_pack_delta_and_restore)
{
    start();
    local.UI.send = true;
    frame();
    uint8_t bytes[TIDE_COMMAND_BYTES];
    const uint32_t size = pending(bytes);
    TIDE_REQUIRE(tide_game_api.command(&world, tide_player_from_index(0), 0, 1, bytes, size));
    tide_world_tick(&world);
    tide_world copy = {0};
    tide_world_copy(&copy, &world);
    TIDE_CHECK(tide_game_api.command_sequence(&copy, tide_player_from_index(0), 0) == 1);
    TIDE_CHECK(tide_world_hash(&copy) == tide_world_hash(&world));
    uint32_t packed_size = tide_world_pack(&world, NULL, 0);
    uint8_t *packed = malloc(packed_size);
    TIDE_REQUIRE(tide_world_pack(&world, packed, packed_size) == packed_size);
    tide_world restored = {0};
    TIDE_REQUIRE(tide_world_unpack(&restored, packed, packed_size));
    TIDE_CHECK(tide_game_api.command_sequence(&restored, tide_player_from_index(0), 0) == 1);
    TIDE_CHECK(!tide_game_api.command(&restored, tide_player_from_index(0), 0, 1, bytes, size));
    TIDE_CHECK(tide_world_hash(&restored) == tide_world_hash(&world));
    free(packed);
    tide_world_free(&copy);
    tide_world_free(&restored);
}

TIDE_TEST(commands_disconnect_discards_local_outbox)
{
    start();
    local.UI.send = true;
    frame();
    uint32_t size = 0;
    TIDE_REQUIRE(tide_local_command(&local, 0, &size) != NULL && size > 0);
    TIDE_CHECK(tide_local_command(&local, 1, &size) == NULL); // No channel but the default in this game
    TIDE_CHECK(tide_local_command(&local, TIDE_COMMAND_UNORDERED, &size) == NULL);
    TIDE_CHECK(tide_local_command(&local, 0, NULL) != NULL); // The size can go unasked
    tide_local_disconnected(&local, TIDE_DISCONNECT_LEFT, "");
    TIDE_CHECK(tide_local_command(&local, 0, &size) == NULL);
    tide_local_set_session(&local, TIDE_SESSION_CONNECTED, tide_player_from_index(1), 0, false, false, "");
    frame();
    TIDE_CHECK(tide_local_command(&local, 0, &size) == NULL);
}

TIDE_TEST(commands_floats_enums_and_nested_bounds_repaired_before_handler)
{
    start();
    local.UI.send = true;
    frame();
    uint8_t bytes[TIDE_COMMAND_BYTES];
    (void)pending(bytes);
    tide_bits b = {bytes, sizeof bytes, 80, false};
    tide_bits_put(&b, 5, 16);
    for (const char *s = "Alice"; *s; s++) tide_bits_put(&b, (uint8_t)*s, 8);
    tide_bits_put_f32(&b, NAN);
    tide_bits_put_varint(&b, tide_zigzag(99));
    tide_bits_put_f32(&b, INFINITY);
    tide_bits_put_f32(&b, -INFINITY);
    tide_bits_put(&b, 3, 16);
    for (const char *s = "new"; *s; s++) tide_bits_put(&b, (uint8_t)*s, 8);
    tide_bits_put_varint(&b, tide_zigzag(99));
    tide_bits_put_varint(&b, 8); // Claimed PlayerID is data; permission checks use the injected sender.
    tide_bits_put_varint(&b, second.index);
    tide_bits_put_varint(&b, second.generation);
    const uint32_t size = tide_bits_end(&b);
    TIDE_REQUIRE(tide_game_api.check_command(bytes, size, NULL));
    TIDE_REQUIRE(tide_game_api.command(&world, tide_player_from_index(0), 0, 1, bytes, size));
    tide_world_tick(&world);
    TIDE_CHECK(world.Seen.calls == 1);
    TIDE_CHECK(world.Seen.rating == 3);
    TIDE_CHECK(world.Seen.choice == Choice_First);
    TIDE_CHECK(world.Seen.aim.x == 0 && world.Seen.aim.y == 0);
    TIDE_CHECK(world.Seen.count == 2);
}

// The outbox is packed after the local state's struct: its count, then each
// command's channel, size and bytes, each checked as the session would
TIDE_TEST(commands_local_pack_preserves_outbox_and_rejects_invalid_ring_metadata)
{
    start();
    local.UI.send = true;
    frame();
    const uint32_t size = tide_local_pack(&local, NULL, 0);
    uint8_t *packed = malloc(size);
    TIDE_REQUIRE(tide_local_pack(&local, packed, size) == size);
    tide_local restored = {0};
    TIDE_REQUIRE(tide_local_unpack(&restored, packed, size));
    uint8_t a[TIDE_COMMAND_BYTES];
    const uint32_t payload = pending(a);
    uint32_t b_size = 0;
    const uint8_t *b = tide_local_command(&restored, 0, &b_size);
    TIDE_REQUIRE(b && b_size == payload);
    TIDE_CHECK(memcmp(a, b, payload) == 0);
    tide_local_free(&restored);
    const size_t outbox = sizeof(tide_local); // The count: one, so a byte
    TIDE_REQUIRE(packed[outbox] == 1 && packed[outbox + 1] == 0 && packed[outbox + 2] == payload); // Channel 0, then the size
    packed[outbox] = 200; // More than the outbox holds
    TIDE_CHECK(!tide_local_unpack(&restored, packed, size));
    TIDE_REQUIRE(tide_local_pack(&local, packed, size) == size);
    packed[outbox + 1] = 5; // A channel the game doesn't have, and not the command's own
    TIDE_CHECK(!tide_local_unpack(&restored, packed, size));
    TIDE_REQUIRE(tide_local_pack(&local, packed, size) == size);
    packed[outbox + 2] = (uint8_t)(payload - 1u); // Cut short
    TIDE_CHECK(!tide_local_unpack(&restored, packed, size));
    TIDE_REQUIRE(tide_local_pack(&local, packed, size) == size);
    packed[outbox + 3] = 255; // The command's type, which the game has none of
    packed[outbox + 4] = 255;
    TIDE_CHECK(!tide_local_unpack(&restored, packed, size));
    free(packed);
    tide_local_free(&restored);
}

TIDE_TEST(commands_generated_game_without_input_survives_loss_over_network)
{
    start();
    double now = 0;
    tide_loopback *net = tide_loopback_create(7731);
    tide_loopback_set_conditions(net, (tide_net_conditions){.latency = 0.04, .jitter = 0.02, .loss = 0.15});
    tide_server *server = tide_server_create(&(tide_server_desc){.game = &tide_game_api, .tick_rate = 60,
                                               .transports = {tide_loopback_endpoint(net, 1)}}, now);
    tide_client *client = tide_client_create(&(tide_client_desc){.game = &tide_game_api,
                                               .transport = tide_loopback_endpoint(net, 2),
                                               .server = tide_loopback_address(1), .lead = 2}, now);
    for (int i = 0; i < 120; i++) {
        now += 1.0 / 60;
        tide_loopback_set_time(net, now);
        tide_client_update(client, now);
        tide_server_update(server, now);
    }
    TIDE_REQUIRE(tide_client_status_of(client).state == TIDE_SESSION_CONNECTED);
    const tide_player_id sender = tide_client_status_of(client).player;
    TIDE_REQUIRE(sender.id == 1);
    local.UI.send = true;
    frame();
    uint8_t bytes[TIDE_COMMAND_BYTES];
    uint32_t size = pending(bytes);
    TIDE_REQUIRE(tide_client_send_command(client, bytes, size));
    tide_local_command_sent(&local, 0);
    local.UI.announce = true;
    frame();
    size = pending(bytes);
    TIDE_REQUIRE(tide_client_send_command(client, bytes, size));
    tide_local_command_sent(&local, 0);
    for (int i = 0; i < 240; i++) {
        now += 1.0 / 60;
        tide_loopback_set_time(net, now);
        tide_client_update(client, now);
        tide_server_update(server, now);
    }
    const tide_world *authoritative = tide_server_world(server);
    const tide_world *verified = tide_client_verified_world(client);
    TIDE_REQUIRE(authoritative && verified);
    TIDE_CHECK(authoritative->Seen.calls == 1);
    TIDE_CHECK(verified->Seen.calls == 1);
    TIDE_CHECK(tide_game_api.command_sequence(authoritative, sender, 0) == 2);
    TIDE_CHECK(tide_game_api.command_sequence(verified, sender, 0) == 2);
    TIDE_CHECK(strcmp(tide_str_c(tide_text_read(&authoritative->heap, authoritative->Seen.announcement)), "Alice") == 0);
    TIDE_CHECK(tide_client_status_of(client).resyncs == 0);
    tide_client_destroy(client);
    tide_server_destroy(server);
    tide_loopback_destroy(net);
}

TIDE_TEST(commands_carry_arrays_of_text)
{
    start();
    local.UI.tag = true;
    frame();
    uint8_t bytes[TIDE_COMMAND_BYTES];
    const uint32_t size = pending(bytes);
    TIDE_REQUIRE(size > 0);
    TIDE_REQUIRE(tide_game_api.command(&world, tide_player_from_index(0), 0, 1, bytes, size));
    tide_world_tick(&world);
    TIDE_CHECK(strcmp(tide_str_c(tide_text_read(&world.heap, world.Seen.tagged.at[0])), "Alice") == 0);
    TIDE_CHECK(strcmp(tide_str_c(tide_text_read(&world.heap, world.Seen.tagged.at[1])), "tag") == 0);
    // A truncated one is refused, as any command's text is
    TIDE_CHECK(!tide_game_api.check_command(bytes, size - 1, NULL));
}
