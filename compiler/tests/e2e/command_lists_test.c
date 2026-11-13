#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

// Commands holding lists of values other than bytes (docs/spec.md, Commands):
// what goes on the wire and comes back, what's repaired and bounded, what a
// hostile count gets, and a big one in chunks through a session.

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_devices devices;
static tide_entity first;

static void start(void)
{
    tide_world_init(&world, 1.0f / 60.0f);
    first = tide_entity_in_slot(&world.entities, 1);
    tide_local_init(&local);
    tide_local_set_session(&local, TIDE_SESSION_CONNECTED, tide_player_from_index(0), 0, false, false, "");
    local.UI.target = first;
}

static void finish(void)
{
    tide_local_free(&local);
    tide_world_free(&world);
}

static void frame(void)
{
    tide_draw_reset(&draw);
    tide_gui_begin(&gui, &devices, tide_f2(640, 480), NULL);
    tide_frame(&world, NULL, 1, &world, NULL, 1.0f, &local, &draw, &gui);
    tide_gui_end(&gui, &draw);
}

// The oldest command waiting, as a copy to free(), checked as the server would
static uint8_t *take(uint32_t *size)
{
    const uint8_t *data = tide_local_command(&local, 0, size);
    TIDE_CHECK(data && *size > 0);
    if (!data) return NULL;
    uint8_t *copy = malloc(*size + 1u);
    memcpy(copy, data, *size);
    uint32_t channel = 7;
    TIDE_CHECK(tide_game_api.check_command(copy, *size, &channel));
    TIDE_CHECK(channel == 0);
    tide_local_command_sent(&local, 0);
    return copy;
}

static int32_t count_of(const tide_list l)
{
    return tide_list_read_count(&world.heap, l);
}

static int32_t int_at(const tide_list l, const int32_t i)
{
    const int32_t *v = tide_list_read(&world.heap, l, i, sizeof(int32_t));
    return v ? *v : INT32_MIN;
}

static Slot slot_at(const tide_list l, const int32_t i)
{
    const Slot *v = tide_list_read(&world.heap, l, i, sizeof(Slot));
    return v ? *v : (Slot){0};
}

TIDE_TEST(command_lists_go_over_the_wire_and_land_in_the_match)
{
    start();
    local.UI.send = true;
    frame();
    uint32_t size = 0;
    uint8_t *bytes = take(&size);
    TIDE_REQUIRE(bytes);
    TIDE_CHECK(size < TIDE_COMMAND_BYTES); // A small one still fits a datagram
    TIDE_REQUIRE(tide_game_api.command(&world, tide_player_from_index(0), 0, 1, bytes, size));
    tide_world_tick(&world);
    TIDE_CHECK(world.Seen.calls == 1);
    // [Clamp(0, 20)] bounds each element
    const int32_t attributes[] = {3, 20, 0, 7, 20, 6};
    TIDE_REQUIRE(count_of(world.Seen.attributes) == 6);
    for (int32_t i = 0; i < 6; i++) TIDE_CHECK(int_at(world.Seen.attributes, i) == attributes[i]);
    // Structs: each field as it was sent, defaults where none was, and the struct's own bounds
    TIDE_REQUIRE(count_of(world.Seen.slots) == 2);
    const Slot a = slot_at(world.Seen.slots, 0), b = slot_at(world.Seen.slots, 1);
    TIDE_CHECK(a.level == 4 && a.weight == 2.5f && a.kind == Kind_Heavy && a.locked && a.at.x == 1 && a.at.y == 2);
    TIDE_CHECK(b.level == 9 && b.weight == 1.0f && b.kind == Kind_Light && !b.locked && b.at.x == 0 && b.at.y == 0);
    TIDE_REQUIRE(count_of(world.Seen.weights) == 2);
    const float *w0 = tide_list_read(&world.heap, world.Seen.weights, 0, sizeof(float));
    const float *w1 = tide_list_read(&world.heap, world.Seen.weights, 1, sizeof(float));
    TIDE_CHECK(w0 && w1 && *w0 == 0.5f && *w1 == -1.25f);
    TIDE_REQUIRE(count_of(world.Seen.kinds) == 3);
    const Kind *k1 = tide_list_read(&world.heap, world.Seen.kinds, 1, sizeof(Kind));
    TIDE_CHECK(k1 && *k1 == Kind_Light);
    // A list in a struct, beside text
    TIDE_REQUIRE(count_of(world.Seen.picks) == 3);
    TIDE_CHECK(int_at(world.Seen.picks, 0) == 5 && int_at(world.Seen.picks, 2) == 7);
    TIDE_CHECK(strcmp(tide_str_c(tide_text_read(&world.heap, world.Seen.name)), "scout") == 0);
    TIDE_REQUIRE(count_of(world.Seen.marks) == 1);
    const tide_entity *mark = tide_list_read(&world.heap, world.Seen.marks, 0, sizeof(tide_entity));
    TIDE_CHECK(mark && tide_entity_equal(*mark, first));
    TIDE_CHECK(world.Seen.extra == 4); // A List<int>? with a value
    TIDE_CHECK(world.Seen.pairs == 33); // An array of lists
    // The match's lists are its own: the world copies, packs and hashes them as any
    tide_world copy = {0};
    tide_world_copy(&copy, &world);
    TIDE_CHECK(tide_world_hash(&copy) == tide_world_hash(&world));
    tide_world_free(&copy);
    free(bytes);
    finish();
}

TIDE_TEST(command_lists_cut_short_or_with_bytes_after_are_refused)
{
    start();
    local.UI.send = true;
    frame();
    uint32_t size = 0;
    uint8_t *bytes = take(&size);
    TIDE_REQUIRE(bytes);
    for (uint32_t n = 0; n < size; n++) TIDE_CHECK(!tide_game_api.check_command(bytes, n, NULL));
    bytes[size] = 0;
    TIDE_CHECK(!tide_game_api.check_command(bytes, size + 1u, NULL));
    TIDE_CHECK(!tide_game_api.command(&world, tide_player_from_index(0), 0, 1, bytes, size - 1u));
    TIDE_CHECK(tide_game_api.command_sequence(&world, tide_player_from_index(0), 0) == 0);
    free(bytes);
    finish();
}

// A command written by hand, as a hostile client could: its type and target,
// then `attributes` (`count` ints of `value`), one slot with what's given, and
// two weights; everything after empty
static uint32_t hand_written(uint8_t *out, const uint32_t capacity, const uint32_t type, const uint32_t count, const int32_t value,
                             const int32_t level, const float weight, const int32_t kind, const float x)
{
    tide_bits b = {out, capacity, 0, false};
    tide_bits_put(&b, type, 16);
    tide_bits_put(&b, 0, 32);
    tide_bits_put(&b, 0, 32);
    tide_bits_put(&b, 0, 32);
    tide_bits_put(&b, 0, 32);
    tide_bits_put_varint(&b, count); // attributes
    for (uint32_t i = 0; i < count && !b.overflow; i++) tide_bits_put_varint(&b, tide_zigzag(value));
    tide_bits_put_varint(&b, 1); // slots: one
    tide_bits_put_varint(&b, tide_zigzag(level));
    tide_bits_put_f32(&b, weight);
    tide_bits_put_varint(&b, tide_zigzag(kind));
    tide_bits_put_bool(&b, true);
    tide_bits_put_f32(&b, x);
    tide_bits_put_f32(&b, 2.0f);
    tide_bits_put_varint(&b, 2); // weights: NaN and 3
    tide_bits_put_f32(&b, NAN);
    tide_bits_put_f32(&b, 3.0f);
    tide_bits_put_varint(&b, 1); // kinds: one no member has
    tide_bits_put_varint(&b, tide_zigzag(5));
    tide_bits_put_varint(&b, 0); // loadout.picks
    tide_bits_put(&b, 0, 16);    // loadout.name
    tide_bits_put_varint(&b, 0); // marks
    tide_bits_put_bool(&b, false); // extra: none
    tide_bits_put_varint(&b, 0); // pairs
    tide_bits_put_varint(&b, 0);
    return tide_bits_end(&b);
}

static uint32_t enlist_type(void)
{
    start();
    local.UI.send = true;
    frame();
    uint32_t size = 0;
    uint8_t *bytes = take(&size);
    const uint32_t type = bytes ? (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8u) : 0u;
    free(bytes);
    finish();
    return type;
}

TIDE_TEST(command_lists_repair_nonfinite_floats_and_unknown_enums_before_the_handler)
{
    const uint32_t type = enlist_type();
    start();
    uint8_t bytes[256];
    const uint32_t size = hand_written(bytes, sizeof bytes, type, 2, -50, 99, NAN, 77, INFINITY);
    TIDE_REQUIRE(size > 0);
    TIDE_REQUIRE(tide_game_api.check_command(bytes, size, NULL));
    TIDE_REQUIRE(tide_game_api.command(&world, tide_player_from_index(0), 0, 1, bytes, size));
    tide_world_tick(&world);
    TIDE_REQUIRE(world.Seen.calls == 1);
    TIDE_REQUIRE(count_of(world.Seen.attributes) == 2);
    TIDE_CHECK(int_at(world.Seen.attributes, 0) == 0 && int_at(world.Seen.attributes, 1) == 0); // Clamped
    const Slot s = slot_at(world.Seen.slots, 0);
    TIDE_CHECK(s.level == 9);                         // [Clamp(0, 9)]
    TIDE_CHECK(s.weight == 1.0f);                     // NaN: the field's default
    TIDE_CHECK(s.kind == Kind_Light);                 // No member: the field's default
    TIDE_CHECK(s.at.x == 0.0f && s.at.y == 2.0f);     // Infinite: the field's default
    const float *w0 = tide_list_read(&world.heap, world.Seen.weights, 0, sizeof(float));
    const float *w1 = tide_list_read(&world.heap, world.Seen.weights, 1, sizeof(float));
    TIDE_CHECK(w0 && w1 && *w0 == 0.0f && *w1 == 3.0f);
    const Kind *kind = tide_list_read(&world.heap, world.Seen.kinds, 0, sizeof(Kind));
    TIDE_CHECK(kind && *kind == Kind_Light); // Kind has no 0: its first member
    TIDE_CHECK(world.Seen.extra == -1 && world.Seen.pairs == 0);
    finish();
}

TIDE_TEST(command_lists_refuse_counts_their_bits_cant_hold)
{
    const uint32_t type = enlist_type();
    start();
    // A count far past what's there, which would otherwise make a list that big
    uint8_t bytes[64];
    tide_bits b = {bytes, sizeof bytes, 0, false};
    tide_bits_put(&b, type, 16);
    tide_bits_put(&b, 0, 32);
    tide_bits_put(&b, 0, 32);
    tide_bits_put(&b, 0, 32);
    tide_bits_put(&b, 0, 32);
    tide_bits_put_varint(&b, 1000000);
    for (int i = 0; i < 8; i++) tide_bits_put_varint(&b, 1);
    uint32_t size = tide_bits_end(&b);
    TIDE_CHECK(!tide_game_api.check_command(bytes, size, NULL));
    // ...past the limit, and the most a varint says
    b = (tide_bits){bytes, sizeof bytes, 0, false};
    tide_bits_put(&b, type, 16);
    tide_bits_put(&b, 0, 32);
    tide_bits_put(&b, 0, 32);
    tide_bits_put(&b, 0, 32);
    tide_bits_put(&b, 0, 32);
    tide_bits_put_varint(&b, UINT32_MAX);
    size = tide_bits_end(&b);
    TIDE_CHECK(!tide_game_api.check_command(bytes, size, NULL));
    TIDE_CHECK(!tide_game_api.command(&world, tide_player_from_index(0), 0, 1, bytes, size));
    TIDE_CHECK(world.Seen.calls == 0);
    finish();
}

TIDE_TEST(command_lists_a_big_one_goes_in_chunks_through_a_session)
{
    start();
    enum { BIG = 200000 };
    double now = 0;
    tide_loopback *net = tide_loopback_create(7733);
    tide_loopback_set_conditions(net, (tide_net_conditions){.latency = 0.03, .jitter = 0.01, .loss = 0.05});
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
    local.UI.big = BIG;
    frame();
    uint32_t size = 0;
    uint8_t *bytes = take(&size);
    TIDE_REQUIRE(bytes);
    TIDE_CHECK(size > TIDE_COMMAND_BYTES && size < BIG + 64u); // Each element under 64: a byte
    TIDE_REQUIRE(tide_client_send_command(client, bytes, size));
    free(bytes);
    const tide_world *authoritative = tide_server_world(server);
    for (int i = 0; i < 1200 && authoritative->Seen.calls == 0; i++) {
        now += 1.0 / 60;
        tide_loopback_set_time(net, now);
        tide_client_update(client, now);
        tide_server_update(server, now);
    }
    TIDE_REQUIRE(authoritative->Seen.calls == 1);
    for (int i = 0; i < 120; i++) {
        now += 1.0 / 60;
        tide_loopback_set_time(net, now);
        tide_client_update(client, now);
        tide_server_update(server, now);
    }
    const tide_world *verified = tide_client_verified_world(client);
    TIDE_REQUIRE(verified && verified->Seen.calls == 1);
    const tide_world *worlds[] = {authoritative, verified};
    for (int k = 0; k < 2; k++) {
        const tide_heaps *heap = &worlds[k]->heap;
        const tide_list l = worlds[k]->Seen.attributes;
        TIDE_REQUIRE(tide_list_read_count(heap, l) == BIG);
        int64_t sum = 0, expected = 0;
        for (int32_t i = 0; i < BIG; i++) {
            const int32_t *v = tide_list_read(heap, l, i, sizeof(int32_t));
            sum += v ? *v : -1000;
            expected += i % 21 > 20 ? 20 : i % 21;
        }
        TIDE_CHECK(sum == expected);
    }
    TIDE_CHECK(tide_client_status_of(client).resyncs == 0);
    tide_client_destroy(client);
    tide_server_destroy(server);
    tide_loopback_destroy(net);
    finish();
}
