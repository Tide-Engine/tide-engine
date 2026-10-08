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

// The command the view sends for `key`, as its bytes: the oldest one waiting.
static uint32_t typed(const int32_t key, uint8_t bytes[TIDE_COMMAND_BYTES])
{
    local.UI.pending = key;
    local.UI.send = true;
    frame();
    uint32_t size = 0;
    const uint8_t *data = tide_local_command(&local, 0, &size);
    TIDE_CHECK(data && size > 0 && size <= TIDE_COMMAND_BYTES);
    if (!data || !size || size > TIDE_COMMAND_BYTES) return 0;
    memcpy(bytes, data, size);
    tide_local_command_sent(&local, 0);
    return size;
}

TIDE_TEST(chars_all_checks_pass)
{
    start();
    tide_world_tick(&world);
    const Results r = tide_get_Results(&world, (tide_entity){1, 1});
    TIDE_REQUIRE(tide_has_Results(&world, (tide_entity){1, 1}));
    if (r.firstFailure) printf("    check %d failed\n", (int)r.firstFailure);
    TIDE_CHECK(r.checks == 41);
    TIDE_CHECK(r.passed == r.checks);
    TIDE_CHECK(tide_scratch_mark() == 0); // Every run cleared what it made
    tide_world_free(&world);
}

// A char is its code point in an int's four bytes, the same on every machine:
// a world with them in fields, arrays, lists, maps, sets and a grid's cells
// copies, packs and hashes like any other, and to the hash every platform has.
TIDE_TEST(chars_are_the_same_bytes_everywhere)
{
    start();
    tide_world_tick(&world);
    const Board board = tide_get_Board(&world, (tide_entity){1, 1});
    TIDE_CHECK(sizeof board.first == 4 && board.first == 0xE9); // 'é'
    TIDE_CHECK(board.row.at[0] == 'a' && board.row.at[1] == 0 && board.row.at[3] == 'd');
    TIDE_CHECK(board.glyph.mark == '!');

    tide_world copy = {0};
    tide_world_copy(&copy, &world);
    TIDE_CHECK(tide_world_hash(&copy) == tide_world_hash(&world));
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(tide_world_pack(&world, bytes, size) == size);
    tide_world restored = {0};
    TIDE_REQUIRE(tide_world_unpack(&restored, bytes, size));
    free(bytes);
    TIDE_CHECK(tide_world_hash(&restored) == tide_world_hash(&world));
    TIDE_CHECK(tide_get_Board(&restored, (tide_entity){1, 1}).first == 0xE9);
    printf("    hash: %llu\n", (unsigned long long)tide_world_hash(&world));
    TIDE_CHECK(tide_world_hash(&world) == CHARS_HASH);
    tide_world_free(&copy);
    tide_world_free(&restored);
    tide_world_free(&world);
}

// An input's chars come from another machine: a number that's no character's
// (past U+10FFFF, half of a UTF-16 pair, under 0) is the field's default
// before any code reads it, an array's element by element.
TIDE_TEST(chars_in_an_input_are_repaired)
{
    start();
    Keys keys = {.key = 0x110000, .pair = {.at = {0xD800, 'p'}}};
    tide_world_set_server_input(&world, keys);
    tide_world_tick(&world);
    TIDE_CHECK(world.inputs[TIDE_SERVER_INPUT].key == 'k'); // Its default
    TIDE_CHECK(world.inputs[TIDE_SERVER_INPUT].pair.at[0] == 0 && world.inputs[TIDE_SERVER_INPUT].pair.at[1] == 'p');
    TIDE_CHECK(world.Typed.presses == 1);

    keys = (Keys){.key = -7, .pair = {.at = {0x10FFFF, 0x1F600}}}; // The highest character, and one past the 16 bits
    tide_world_set_server_input(&world, keys);
    tide_world_tick(&world);
    TIDE_CHECK(world.inputs[TIDE_SERVER_INPUT].key == 'k');
    TIDE_CHECK(world.inputs[TIDE_SERVER_INPUT].pair.at[0] == 0x10FFFF && world.inputs[TIDE_SERVER_INPUT].pair.at[1] == 0x1F600);
    TIDE_CHECK(world.Typed.presses == 1);

    // Packed, an input's char is 21 bits, and comes back as it went, whole and as what changed
    uint8_t packed[64];
    const Keys sent = {.key = 0x20AC, .pair = {.at = {'x', 0x10FFFF}}};
    uint32_t size = tide_game_api.write_input(&sent, packed, sizeof packed);
    TIDE_REQUIRE(size > 0 && size <= 8); // Three of 21 bits
    Keys back = {0};
    TIDE_REQUIRE(tide_game_api.read_input(packed, size, &back));
    TIDE_CHECK(back.key == 0x20AC && back.pair.at[0] == 'x' && back.pair.at[1] == 0x10FFFF);
    const Keys next = {.key = 0x20AC, .pair = {.at = {'y', 0x10FFFF}}};
    size = tide_game_api.write_input_delta(&next, &sent, packed, sizeof packed);
    TIDE_REQUIRE(size > 0 && size <= 4); // A bit for each that didn't change, and the one that did
    memset(&back, 0, sizeof back);
    TIDE_REQUIRE(tide_game_api.read_input_delta(packed, size, &sent, &back));
    TIDE_CHECK(back.key == 0x20AC && back.pair.at[0] == 'y' && back.pair.at[1] == 0x10FFFF);
    tide_world_free(&world);
}

// A command carries one as 21 bits, and its handler gets it; with bits no
// character has, which only a machine that isn't running the game's code
// could send, it's the field's default.
TIDE_TEST(chars_in_a_command_arrive_and_are_repaired)
{
    start();
    uint8_t bytes[TIDE_COMMAND_BYTES];
    uint32_t size = typed('w', bytes);
    TIDE_REQUIRE(tide_game_api.command(&world, tide_player_from_index(0), 0, 1, bytes, size));
    tide_world_tick(&world);
    TIDE_CHECK(world.Typed.last == 'w');
    tide_world_tick(&world); // Its event's handler, a tick on
    TIDE_CHECK(world.Typed.presses == 100);

    // Where the char's bits are: what differs between the one that's none and the highest. All of them set is
    // past any character
    uint8_t none[TIDE_COMMAND_BYTES];
    uint8_t highest[TIDE_COMMAND_BYTES];
    const uint32_t none_size = typed(0, none);
    size = typed(0x10FFFF, highest);
    TIDE_REQUIRE(size == none_size);
    uint32_t first = size * 8u, last = 0;
    for (uint32_t bit = 0; bit < size * 8u; bit++) {
        if (((none[bit / 8u] ^ highest[bit / 8u]) >> (bit % 8u) & 1u) == 0) continue;
        if (bit < first) first = bit;
        if (bit > last) last = bit;
    }
    TIDE_REQUIRE(last - first == 20u);
    for (uint32_t bit = first; bit <= last; bit++) highest[bit / 8u] |= (uint8_t)(1u << (bit % 8u));
    TIDE_REQUIRE(tide_game_api.command(&world, tide_player_from_index(0), 0, 2, highest, size));
    tide_world_tick(&world);
    TIDE_CHECK(world.Typed.last == '?'); // The field's default
    frame();
    TIDE_CHECK(local.UI.seen == '?'); // And a view's copy of it
    tide_world_free(&world);
}

// A task's loop over a text goes on where it was after each wait, and steps
// on threads change a list's chars.
TIDE_TEST(chars_in_tasks_and_parallel_steps)
{
    start();
    tide_text_use(&world.heap, NULL);
    for (int i = 0; i < 6; i++) tide_world_tick(&world);
    TIDE_CHECK(strcmp(tide_str_c(tide_text_read(&world.heap, world.Spelled.word)), "h\xC3\xA9y!") == 0);
    TIDE_REQUIRE(tide_list_count(world.Field.cells) == 8);
    const Cell *cells = tide_list_at(world.Field.cells, 0, sizeof(Cell));
    TIDE_REQUIRE(cells);
    TIDE_CHECK(cells[0].mark == 'a' + 6 && cells[0].hits == 1); // 'f' at its sixth step
    TIDE_CHECK(cells[7].mark == 'h' + 6 && cells[7].hits == 6);
    tide_world_free(&world);
}
