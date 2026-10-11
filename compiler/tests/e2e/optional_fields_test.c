#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_devices devices;

// The Main scene is slot 0; its Setup spawns these in order.
static const tide_entity HOLDER = {1, 1};
static const tide_entity CLEARED = {2, 1};
static const tide_entity BODY = {3, 1};

// The game's C: a T? field is its value and whether there is one, zeros when there's none
int32_t PickItem(const Plain value)
{
    return value.item.ok ? value.item.value : -1;
}

void FillItem(Plain *value)
{
    value->item = (tide_optional_int){.value = 41, .ok = true};
    value->rank = (tide_optional_Rank){0};
}

static bool is(const tide_str s, const char *expected)
{
    return s.bytes == (int32_t)strlen(expected) && memcmp(s.ptr, expected, strlen(expected)) == 0;
}

static void frame(const tide_world *now, const tide_world *previous, const float alpha)
{
    tide_draw_reset(&draw);
    tide_gui_begin(&gui, &devices, tide_f2(640, 480), NULL);
    tide_frame((tide_world *)now, previous, alpha, (tide_world *)now, NULL, 1.0f, &local, &draw, &gui);
    tide_gui_end(&gui, &draw);
}

TIDE_TEST(optional_fields_all_checks_pass)
{
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    TIDE_REQUIRE(tide_has_Results(&world, HOLDER));
    const Results r = tide_get_Results(&world, HOLDER);
    if (r.firstFailure) printf("    check %d failed\n", (int)r.firstFailure);
    if (r.checks != 86) printf("    %d checks\n", (int)r.checks);
    TIDE_CHECK(r.checks == 86);
    TIDE_CHECK(r.passed == r.checks);
    TIDE_CHECK(tide_scratch_mark() == 0); // Every run cleared what it made
    // Events: one with its fields and one with none of them
    TIDE_CHECK(world.Game.hits == 102);
    TIDE_CHECK(world.Game.motto.ok && is(tide_text_read(&world.heap, world.Game.motto.value), "check!"));
    TIDE_CHECK(world.Game.round == 1 && world.Game.titled);
    TIDE_CHECK(world.Game.fast == 4 && world.Game.got == 1);
    tide_world_tick(&world);
    TIDE_CHECK(world.Game.round == 2 && !world.Game.titled);
    TIDE_CHECK(world.Game.fast == 0 && world.Game.got == 0);
    // A task that waits, with the world as bytes and back in the middle of it
    TIDE_CHECK(!world.Game.winner.ok && !world.Game.waited.ok);
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(bytes && tide_world_pack(&world, bytes, size) == size);
    tide_world restored = {0};
    TIDE_REQUIRE(tide_world_unpack(&restored, bytes, size));
    free(bytes);
    for (int i = 0; i < 4; i++) {
        tide_world_tick(&world);
        tide_world_tick(&restored);
    }
    TIDE_CHECK(world.Game.waited.ok && world.Game.waited.value == 12);
    TIDE_CHECK(world.Game.winner.ok && world.Game.winner.value == 2);
    TIDE_CHECK(world.Game.said.ok && is(tide_text_read(&world.heap, world.Game.said.value), "check! waited"));
    TIDE_CHECK(tide_world_hash(&restored) == tide_world_hash(&world));
    tide_world_free(&restored);
    tide_world_free(&world);
}

// A field with no value is zeros, whatever it held before: the bytes of a
// component whose fields were set and then taken away are those of one nothing
// ever set, and so is the world's heap.
TIDE_TEST(optional_fields_with_no_value_are_zeros)
{
    tide_world_init(&world, 1.0f / 60.0f);
    const Cleared fresh = tide_get_Cleared(&world, CLEARED);
    tide_world_tick(&world);
    const Cleared set = tide_get_Cleared(&world, CLEARED);
    TIDE_CHECK(set.number.ok && set.number.value == 7);
    TIDE_CHECK(set.aim.ok && set.aim.value.z == 3.0f);
    TIDE_CHECK(set.limits.ok && set.limits.value.min == 4 && set.limits.value.max == 5);
    TIDE_CHECK(set.text.ok && is(tide_text_read(&world.heap, set.text.value), "some text"));
    TIDE_CHECK(set.list.ok && tide_list_read_count(&world.heap, set.list.value) == 3);
    TIDE_CHECK(set.slot.item.ok && set.slot.label.ok);
    TIDE_CHECK(memcmp(&set, &fresh, sizeof set) != 0);
    tide_world_tick(&world);
    const Cleared cleared = tide_get_Cleared(&world, CLEARED);
    for (size_t i = 0; i < sizeof cleared; i++) {
        if (((const uint8_t *)&cleared)[i] != ((const uint8_t *)&fresh)[i]) printf("    byte %d of Cleared is %d\n", (int)i, (int)((const uint8_t *)&cleared)[i]);
    }
    TIDE_CHECK(memcmp(&cleared, &fresh, sizeof cleared) == 0);
    static const Cleared zeros = {.slot = {.amount = 1.0f}};
    TIDE_CHECK(memcmp(&cleared, &zeros, sizeof cleared) == 0);
    // And the whole world is the one where nothing ever set the ones that keep nothing in the heap: a heap that gave
    // text and lists out and took them back is as it is after any text or list
    static tide_world quiet;
    tide_world_init(&quiet, 1.0f / 60.0f);
    quiet.Game.quiet = true;
    tide_world_tick(&quiet);
    TIDE_CHECK(tide_world_hash(&quiet) != tide_world_hash(&world));
    tide_world_tick(&quiet);
    quiet.Game.quiet = false;
    TIDE_CHECK(tide_world_hash(&quiet) == tide_world_hash(&world));
    tide_world_free(&quiet);
    tide_world_free(&world);
}

// The world with T? fields in it, through a copy, as bytes and back, and a
// tick more on each. The hash is every platform's.
TIDE_TEST(optional_fields_snapshot_pack_and_hash_the_same_everywhere)
{
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    const Holder holder = tide_get_Holder(&world, HOLDER);
    TIDE_CHECK(holder.target.ok && holder.target.value == 9);
    TIDE_CHECK(!holder.name.ok && holder.name.value.at == 0);
    TIDE_CHECK(holder.kinds.inner.ok && holder.kinds.inner.value.item.ok && holder.kinds.inner.value.item.value == 12);
    TIDE_CHECK(is(tide_text_read(&world.heap, holder.kinds.inner.value.label.value), "read"));
    TIDE_CHECK(holder.kinds.picks.ok && tide_list_read_count(&world.heap, holder.kinds.picks.value) == 0);
    TIDE_CHECK(holder.kinds.counts.ok && !holder.picks.ok);
    TIDE_CHECK(sizeof(holder.target) == 8 && sizeof(holder.kinds.flag) == 2 && sizeof(holder.kinds.big) == 16);

    tide_world copy = {0};
    tide_world_copy(&copy, &world);
    TIDE_CHECK(tide_world_hash(&copy) == tide_world_hash(&world));
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(bytes && tide_world_pack(&world, bytes, size) == size);
    tide_world restored = {0};
    TIDE_REQUIRE(tide_world_unpack(&restored, bytes, size));
    free(bytes);
    TIDE_CHECK(tide_world_hash(&restored) == tide_world_hash(&world));
    const Holder back = tide_get_Holder(&restored, HOLDER);
    TIDE_CHECK(memcmp(&back, &holder, sizeof back) == 0);
    tide_world_tick(&world);
    tide_world_tick(&restored);
    tide_world_tick(&copy);
    const uint64_t hash = tide_world_hash(&world);
    TIDE_CHECK(tide_world_hash(&restored) == hash && tide_world_hash(&copy) == hash);
    if (hash != OPTIONAL_FIELDS_HASH) printf("    world: hash is %" PRIu64 "ull, expected %" PRIu64 "ull\n", hash, (uint64_t)OPTIONAL_FIELDS_HASH);
    TIDE_CHECK(hash == OPTIONAL_FIELDS_HASH);

    // What changed from one tick to the next, as a delta, comes out the same world
    tide_world base = {0};
    tide_world_copy(&base, &world);
    tide_world_tick(&world);
    uint32_t delta_size = 0;
    uint8_t *delta = tide_world_pack_delta(&world, &base, NULL, 0, &delta_size);
    tide_world made = {0};
    TIDE_REQUIRE(delta && tide_world_unpack_delta(&made, &base, delta, delta_size));
    free(delta);
    TIDE_CHECK(tide_world_hash(&made) == tide_world_hash(&world));
    tide_world_free(&made);
    tide_world_free(&base);
    tide_world_free(&restored);
    tide_world_free(&copy);
    tide_world_free(&world);
}

// The input's T? fields: from C as a struct, repaired on the way in, and as
// bits, whole and as what changed.
TIDE_TEST(optional_fields_in_the_input_pack_and_repair)
{
    tide_world_init(&world, 1.0f / 60.0f);
    Controls in = {0};
    in.aim = (tide_optional_float){.value = 0.25f, .ok = true};
    in.choice = (tide_optional_int){.value = -7, .ok = true};
    tide_world_set_input(&world, tide_player_from_index(0), in);
    tide_world_tick(&world);
    TIDE_CHECK(world.Game.aim.ok && world.Game.aim.value == 0.25f);
    TIDE_CHECK(!world.Game.fire.ok && world.Game.choice.ok && world.Game.choice.value == -7);

    // One with no value that came with bytes beside it, and padding that isn't zeros: zeros in the world
    Controls dirty = {0};
    dirty.aim.value = 9.0f;
    dirty.choice.value = 12;
    dirty.fire.value = true;
    memset(dirty.aim.tide_pad0, 0xa5, sizeof dirty.aim.tide_pad0);
    memset(dirty.choice.tide_pad0, 0xa5, sizeof dirty.choice.tide_pad0);
    tide_world_set_input(&world, tide_player_from_index(0), dirty);
    tide_world_tick(&world);
    static const Controls none = {0};
    TIDE_CHECK(memcmp(&world.inputs[0], &none, sizeof none) == 0);
    TIDE_CHECK(!world.Game.aim.ok && world.Game.aim.value == 0.0f && !world.Game.choice.ok && world.Game.choice.value == 0);

    uint8_t bytes[64];
    Controls out;
    memset(&out, 0xff, sizeof out);
    const uint32_t empty = tide_game_api.write_input(&none, bytes, sizeof bytes);
    if (empty != 2) printf("    an input with nothing is %d bytes\n", (int)empty);
    TIDE_REQUIRE(empty == 2 && tide_game_api.read_input(bytes, empty, &out)); // A bit for each that's not there, and the slot's byte
    TIDE_CHECK(memcmp(&out, &none, sizeof out) == 0);
    in.fire = (tide_optional_bool){.value = false, .ok = true};
    in.slot = 3;
    const uint32_t full = tide_game_api.write_input(&in, bytes, sizeof bytes);
    memset(&out, 0xff, sizeof out);
    TIDE_REQUIRE(full > empty && tide_game_api.read_input(bytes, full, &out));
    TIDE_CHECK(memcmp(&out, &in, sizeof out) == 0);
    for (int k = 0; k < 4; k++) { // To a value, to another, to nothing, and from nothing
        Controls from = k == 3 ? none : in;
        Controls to = in;
        if (k == 0) to.aim.value = 0.5f;
        if (k == 1) to.choice = (tide_optional_int){.value = 8, .ok = true};
        if (k == 2) to = none;
        const uint32_t delta = tide_game_api.write_input_delta(&to, &from, bytes, sizeof bytes);
        memset(&out, 0xff, sizeof out);
        TIDE_REQUIRE(delta > 0 && tide_game_api.read_input_delta(bytes, delta, &from, &out));
        TIDE_CHECK(memcmp(&out, &to, sizeof out) == 0);
    }
    tide_world_free(&world);
}

// Commands with T? fields: packed with a bit for each, and the value only
// where there is one; local events and local entities keep theirs too.
TIDE_TEST(optional_fields_cross_the_wire_in_commands)
{
    tide_world_init(&world, 1.0f / 60.0f);
    tide_local_init(&local);
    tide_local_set_session(&local, TIDE_SESSION_CONNECTED, tide_player_from_index(0), 0, false, false, "");
    tide_world_tick(&world);
    local.Seen.send = true;
    frame(&world, NULL, 1.0f);
    TIDE_CHECK(local.Seen.winner.ok && local.Seen.winner.value == 2);
    TIDE_CHECK(local.Seen.motto.ok && is(tide_text_read(&local.heap, local.Seen.motto.value), "check!"));
    TIDE_CHECK(local.Seen.best == 2.5f);
    uint8_t bytes[TIDE_COMMAND_BYTES];
    uint32_t sizes[2] = {0, 0};
    for (uint32_t i = 0; i < 2; i++) {
        const uint8_t *data = tide_local_command(&local, 0, &sizes[i]);
        TIDE_REQUIRE(data && sizes[i] > 0 && sizes[i] <= TIDE_COMMAND_BYTES);
        memcpy(bytes, data, sizes[i]);
        TIDE_CHECK(tide_game_api.check_command(bytes, sizes[i], NULL));
        for (uint32_t n = 0; n < sizes[i]; n++) TIDE_CHECK(!tide_game_api.check_command(bytes, n, NULL));
        TIDE_REQUIRE(tide_game_api.command(&world, tide_player_from_index(0), 0, i + 1, bytes, sizes[i]));
        tide_local_command_sent(&local, 0);
        tide_world_tick(&world);
        if (i == 0) {
            TIDE_CHECK(world.Game.renamed == 21);
            TIDE_CHECK(world.Game.renamedTo.ok && is(tide_text_read(&world.heap, world.Game.renamedTo.value), "Ada"));
            TIDE_CHECK(!world.Game.renamedLevel.ok && world.Game.renamedLevel.value == 0);
        } else {
            TIDE_CHECK(world.Game.renamed == 121);
            TIDE_CHECK(!world.Game.renamedTo.ok && world.Game.renamedTo.value.at == 0);
            TIDE_CHECK(world.Game.renamedLevel.ok && world.Game.renamedLevel.value == 5);
        }
    }
    // 10 bytes of type and target, then: a name's bit, 16 of its length and 3 bytes, a bit, a bit and two ints of a byte,
    // an int (67 bits); and a bit, a bit and an int, a bit, an int of two bytes (27 bits)
    if (sizes[0] != 19 || sizes[1] != 14) printf("    commands of %d and %d bytes\n", (int)sizes[0], (int)sizes[1]);
    TIDE_CHECK(sizes[0] == 19 && sizes[1] == 14);
    // The frame's local events and local entity
    frame(&world, NULL, 1.0f);
    TIDE_CHECK(!local.Seen.heard.ok && local.Seen.heard.value.at == 0); // The second Heard, with none, was the last
    TIDE_CHECK(local.Seen.localSeen.ok && local.Seen.localSeen.value == 1); // A local field, kept from the frame before
    TIDE_CHECK(is(tide_text_read(&local.heap, local.Seen.shown), "seen 2"));
    tide_local_free(&local);
    tide_world_free(&world);
}

// A view between two ticks: a T? field is the later tick's, value or nothing, while the float beside it blends.
TIDE_TEST(optional_fields_snap_in_views)
{
    static tide_world before;
    tide_world_init(&world, 1.0f / 60.0f);
    tide_local_init(&local);
    tide_world_tick(&world); // x 10, a speed
    tide_world_copy(&before, &world);
    tide_world_tick(&world); // x 20, none
    frame(&world, &before, 0.5f);
    TIDE_CHECK(local.Seen.bodyX == 15.0f);
    TIDE_CHECK(!local.Seen.hasSpeed && local.Seen.bodySpeed == -1.0f);
    tide_world_copy(&before, &world);
    tide_world_tick(&world); // x 30, a speed again
    frame(&world, &before, 0.5f);
    TIDE_CHECK(local.Seen.bodyX == 25.0f);
    TIDE_CHECK(local.Seen.hasSpeed && local.Seen.bodySpeed == 4.0f);
    TIDE_CHECK(tide_get_Body(&world, BODY).speed.ok);
    tide_local_free(&local);
    tide_world_free(&before);
    tide_world_free(&world);
}
