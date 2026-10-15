// inside_out_solve: a solver and inspector for Inside Out's puzzles.
//
//   inside_out_solve <level> --print
//       Every room of the puzzle as a map in levels.tide's notation (tiles,
//       then what stands on them), with where each box stands, its outline,
//       its doorways and where the robots are.
//   inside_out_solve <level> [--limit <positions>]
//       The shortest solution, breadth first: its length, its moves in
//       test/solutions.h's notation (NESW Amber, seat 0; nesw Teal, seat 1;
//       a space where the other robot takes over), how many positions were
//       explored, whether that was every reachable one, and the time taken.
//   inside_out_solve <level> --alone <seat> [--limit <positions>]
//       The other robot taken off the board: whether seat 0 or 1 alone can
//       reach a position with every battery seated, every socket filled and
//       every box on its outline (goals are not asked), and how many
//       positions it can reach.
//   inside_out_solve <level> --play "<moves>"
//       Plays moves in that notation from the start and prints the board
//       after them, or names the move that was refused and prints the board
//       before it.
//   inside_out_solve all [--limit <positions>]
//       The shortest search on every puzzle, as a table.
//   Add --no-battery-plates or --keep-docked for a design counterfactual:
//       forbid batteries on plates, or any departure from an occupied dock.
//       Exhaustion proves impossibility only under that additional rule.
//
// Levels are numbered from 1, as the game shows them. A search stops at
// --limit distinct positions (30,000,000 unless given, about 30 bytes
// each) and reports its progress on stderr every few seconds. It exits with
// 1 when a --play move is refused or a solution found fails its replay, 2
// for arguments it cannot read, 3 when the game or memory fails it, and 0
// otherwise, with or without a solution.
//
// Every move goes through the game's own rule, the generated Move command
// handler, sent and ticked as in a match (as test/simulation.c does): its
// MovePuzzle is static in the generated C, out of a host's reach. A position
// is what that rule reads: where each robot, box and battery stands, and
// whether a battery is active, a byte each. Facing, the trails (rebuilt from
// where the boxes stand after every move), the move count and the solved
// flag are left out, and since the rule treats batteries alike, theirs are
// kept in sorted order.
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "game.h"
#include "tide/net.h"

#define DEFAULT_LIMIT 30000000u
// A link holds the earlier position's index above the move's 3 bits.
#define MOST_LIMIT (1u << 29)
enum {
    KEY_BYTES = 16,
    // A thing on a grid is (room + 1) * 49 + y * 7 + x, rooms -1 to 3 and
    // cells 0 to 6, so under 245. These are the rest.
    OFF_BOARD = 254, // A robot taken off the board (--alone)
    SPENT = 255,     // A battery that is not active
};
static const char *const MOVE_MARKS = "NESWnesw";
static const char *const TILE_MARKS = ".#oxaAbB"; // In Tile's order
static const char *const SIDES[4] = {"north", "east", "south", "west"};
static const char *const ROBOTS[2] = {"Amber", "Teal"};

static tide_world world;
static int world_level = -1;
// Design probes remove one strategy without changing the production rules.
// A failed bounded search is never an impossibility proof.
static bool no_battery_plates;
static bool keep_docked;

// What levels.tide says of a puzzle, handed over by test/solve.tide's
// Describe handler.
static struct {
    int puzzles, level, chapter;
    char name[128], chapter_name[64], lesson[512];
} words;

void inside_out_solve_words(int32_t puzzles, int32_t level, int32_t chapter, const char *name, const char *chapter_name,
    const char *lesson);
void inside_out_solve_words(const int32_t puzzles, const int32_t level, const int32_t chapter, const char *const name,
    const char *const chapter_name, const char *const lesson)
{
    words.puzzles = puzzles;
    words.level = level;
    words.chapter = chapter;
    snprintf(words.name, sizeof words.name, "%s", name ? name : "");
    snprintf(words.chapter_name, sizeof words.chapter_name, "%s", chapter_name ? chapter_name : "");
    snprintf(words.lesson, sizeof words.lesson, "%s", lesson ? lesson : "");
}

static double now(void)
{
    struct timespec t;
    if (!timespec_get(&t, TIME_UTC)) return 0.0;
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

// 1234567 as "1,234,567".
static const char *grouped(const uint64_t value, char *const text, const size_t size)
{
    char digits[32];
    const int count = snprintf(digits, sizeof digits, "%" PRIu64, value);
    size_t at = 0;
    for (int i = 0; i < count && at + 2 < size; i++) {
        if (i > 0 && (count - i) % 3 == 0) text[at++] = ',';
        text[at++] = digits[i];
    }
    text[at] = 0;
    return text;
}

static bool same_cell(const tide_int2 a, const tide_int2 b) { return a.x == b.x && a.y == b.y; }

// Sends a command from a seat's player and runs the tick that handles it.
static bool send(const uint32_t id, const int seat, const int32_t *const values, const int count)
{
    uint8_t bytes[64];
    tide_bits bits = {bytes, sizeof bytes, 0, false};
    tide_bits_put(&bits, id, 16);
    tide_bits_put(&bits, 0, 32);
    tide_bits_put(&bits, 0, 32);
    for (int i = 0; i < count; i++) tide_bits_put_varint(&bits, tide_zigzag(values[i]));
    const uint32_t size = tide_bits_end(&bits);
    const tide_player_id sender = tide_player_from_index(seat);
    const uint32_t sequence = tide_game_api.command_sequence(&world, sender, 0) + 1u;
    const bool taken = tide_game_api.command(&world, sender, 0, sequence, bytes, size);
    tide_world_tick(&world);
    return taken;
}

// A new match with both players in it, on a puzzle (from 0). Returns
// whether the game opened that puzzle.
static bool open_level(const int level)
{
    tide_world_free(&world);
    tide_world_init(&world, 1.0f / 30.0f);
    for (int seat = 0; seat < 2; seat++) tide_world_player_joined(&world, tide_player_from_index(seat));
    tide_world_tick(&world);
    tide_world_tick(&world);
    const int serial = world.Workshop.serial;
    send(TIDE_COMMAND_ID_ChooseLevel, 0, (const int32_t[]){serial, level}, 2);
    world_level = level;
    return world.Workshop.level == level && world.Workshop.serial != serial;
}

// Fills `words` for a puzzle. The match is left as it was.
static bool describe(const int level)
{
    words.puzzles = 0;
    send(TIDE_COMMAND_ID_Describe, 0, (const int32_t[]){level}, 1);
    return words.puzzles > 0 && words.level == level;
}

// One move through the Move handler, from a position. Returns whether the
// handler took it, and the position after it.
static bool step(const Puzzle *const from, const int seat, const int direction, Puzzle *const to)
{
    // Time.tick is an int: start the match again long before it could wrap.
    if (world.Time.tick > (1 << 30)) open_level(world_level);
    world.Workshop.puzzle = *from;
    world.Workshop.historyCount = 0;
    send(TIDE_COMMAND_ID_Move, seat, (const int32_t[]){world.Workshop.serial, direction}, 2);
    if (world.Workshop.puzzle.moves == from->moves) return false;
    *to = world.Workshop.puzzle;
    return true;
}

static Tile tile_at(const LevelLayout *const layout, const int room, const tide_int2 at)
{
    if (room < -1 || room >= layout->roomCount || room >= 4) return Tile_Wall;
    const int width = room < 0 ? layout->width : layout->rooms.at[room].width;
    const int height = room < 0 ? layout->height : layout->rooms.at[room].height;
    if (at.x < 0 || at.y < 0 || at.x >= width || at.y >= height || width > 7 || height > 7) return Tile_Wall;
    const int cell = at.y * width + at.x;
    return room < 0 ? layout->tiles.at[cell] : layout->rooms.at[room].tiles.at[cell];
}
static int room_width(const LevelLayout *const layout, const int room)
{
    return room < 0 ? layout->width : layout->rooms.at[room].width;
}
static int room_height(const LevelLayout *const layout, const int room)
{
    return room < 0 ? layout->height : layout->rooms.at[room].height;
}
static int box_count(const LevelLayout *const layout)
{
    return layout->roomCount < 0 ? 0 : layout->roomCount > 4 ? 4 : layout->roomCount;
}
static int battery_count(const LevelLayout *const layout)
{
    return layout->crateCount < 0 ? 0 : layout->crateCount > 8 ? 8 : layout->crateCount;
}
static bool battery_at(const LevelLayout *const layout, const Puzzle *const puzzle, const int room, const tide_int2 at)
{
    for (int crate = 0; crate < battery_count(layout); crate++) {
        const Cargo *const battery = &puzzle->crates.at[crate];
        if (battery->active && battery->room == room && same_cell(battery->position, at)) return true;
    }
    return false;
}

// Every battery seated, every socket filled and every box on its outline:
// what IsSolved asks but the robots' goals.
static bool in_place(const LevelLayout *const layout, const Puzzle *const puzzle)
{
    for (int crate = 0; crate < battery_count(layout); crate++) {
        const Cargo *const battery = &puzzle->crates.at[crate];
        if (battery->active && tile_at(layout, battery->room, battery->position) != Tile_Target) return false;
    }
    for (int room = -1; room < box_count(layout); room++)
        for (int y = 0; y < room_height(layout, room); y++)
            for (int x = 0; x < room_width(layout, room); x++)
                if (tile_at(layout, room, tide_i2(x, y)) == Tile_Target && !battery_at(layout, puzzle, room, tide_i2(x, y)))
                    return false;
    for (int box = 0; box < box_count(layout); box++) {
        const RoomLayout *const inside = &layout->rooms.at[box];
        if (inside->dock.x >= 0 &&
            (puzzle->rooms.at[box].room != inside->dockRoom || !same_cell(puzzle->rooms.at[box].position, inside->dock)))
            return false;
    }
    return true;
}

static bool strategy_allowed(const LevelLayout *const layout, const Puzzle *const from, const Puzzle *const to)
{
    if (no_battery_plates)
        for (int battery = 0; battery < battery_count(layout); battery++) {
            const Cargo cargo = to->crates.at[battery];
            const Tile tile = tile_at(layout, cargo.room, cargo.position);
            if (cargo.active && (tile == Tile_PlateA || tile == Tile_PlateB)) return false;
        }
    if (keep_docked)
        for (int box = 0; box < box_count(layout); box++) {
            const RoomLayout room = layout->rooms.at[box];
            const RoomShell before = from->rooms.at[box], after = to->rooms.at[box];
            if (room.dock.x >= 0 && before.room == room.dockRoom && same_cell(before.position, room.dock)
                && (after.room != before.room || !same_cell(after.position, before.position))) return false;
        }
    return true;
}

// What a search keeps of a puzzle.
typedef struct Setup {
    LevelLayout layout;
    // As loaded, with the robot that is not playing taken off the board.
    Puzzle start;
    int boxes, batteries;
    int absent; // The seat taken off, or -1
} Setup;

static void take_off(Traveler *const robot)
{
    memset(robot, 0, sizeof *robot);
    robot->room = -1;
    // Off every grid: TileAt calls it wall, and no step ever reaches it.
    robot->position = tide_i2(-16, -16);
    robot->facing = 2;
    for (int i = 0; i < 8; i++) robot->trail.at[i] = (Entry){.box = -1};
}

// The open puzzle, with seat `absent` (or neither, for -1) off the board.
static Setup setup_of(const int absent)
{
    Setup setup = {.layout = world.Workshop.layout, .start = world.Workshop.puzzle, .absent = absent};
    setup.boxes = box_count(&setup.layout);
    setup.batteries = battery_count(&setup.layout);
    if (absent >= 0) take_off(&setup.start.actors.at[absent]);
    return setup;
}

typedef struct Key {
    uint64_t word[2];
} Key;

static uint8_t place_code(const int room, const tide_int2 at)
{
    if (room < -1 || room > 3 || at.x < 0 || at.x > 6 || at.y < 0 || at.y > 6) {
        fprintf(stderr, "Something stands at (%d, %d) of room %d, which is on no grid this solver knows.\n", at.x, at.y, room);
        exit(3);
    }
    return (uint8_t)((room + 1) * 49 + at.y * 7 + at.x);
}
static void place_of(const uint8_t code, int32_t *const room, tide_int2 *const at)
{
    *room = code / 49 - 1;
    *at = tide_i2(code % 7, code % 49 / 7);
}

static Key key_of(const Setup *const setup, const Puzzle *const puzzle)
{
    uint8_t bytes[KEY_BYTES] = {0};
    int count = 0;
    for (int seat = 0; seat < 2; seat++) {
        const Traveler *const robot = &puzzle->actors.at[seat];
        bytes[count++] = seat == setup->absent ? OFF_BOARD : place_code(robot->room, robot->position);
    }
    for (int box = 0; box < setup->boxes; box++)
        bytes[count++] = place_code(puzzle->rooms.at[box].room, puzzle->rooms.at[box].position);
    const int first = count;
    for (int crate = 0; crate < setup->batteries; crate++) {
        const Cargo *const battery = &puzzle->crates.at[crate];
        const uint8_t code = battery->active ? place_code(battery->room, battery->position) : SPENT;
        int at = count++;
        for (; at > first && bytes[at - 1] > code; at--) bytes[at] = bytes[at - 1];
        bytes[at] = code;
    }
    Key key;
    memcpy(key.word, bytes, sizeof key.word);
    return key;
}

// A position as a puzzle the Move handler can take: the start, with
// everything the key holds put where it says.
static void position_of(const Setup *const setup, const Key key, Puzzle *const puzzle)
{
    uint8_t bytes[KEY_BYTES];
    memcpy(bytes, key.word, sizeof bytes);
    *puzzle = setup->start;
    puzzle->moves = 0;
    puzzle->solved = false;
    int at = 0;
    for (int seat = 0; seat < 2; seat++, at++)
        if (bytes[at] != OFF_BOARD) place_of(bytes[at], &puzzle->actors.at[seat].room, &puzzle->actors.at[seat].position);
    for (int box = 0; box < setup->boxes; box++, at++)
        place_of(bytes[at], &puzzle->rooms.at[box].room, &puzzle->rooms.at[box].position);
    for (int crate = 0; crate < setup->batteries; crate++, at++) {
        Cargo *const battery = &puzzle->crates.at[crate];
        battery->active = bytes[at] != SPENT;
        if (battery->active) place_of(bytes[at], &battery->room, &battery->position);
    }
}

// Every position found, in the order found, and an open-addressing table
// of them.
typedef struct Search {
    Key *keys;
    // The position it was reached from, shifted left 3, and the move from
    // there: seat * 4 + direction.
    uint32_t *links;
    uint32_t count, room, limit;
    uint32_t *slots; // A position's index + 1, or 0
    uint32_t mask;
} Search;

static void *grow(void *const block, const size_t count, const size_t size)
{
    void *const grown = realloc(block, count * size);
    if (!grown) {
        fprintf(stderr, "Out of memory asking for %zu bytes; try a lower --limit.\n", count * size);
        exit(3);
    }
    return grown;
}

static uint64_t mix(uint64_t x)
{
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdull;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ull;
    return x ^ x >> 33;
}
static uint32_t slot_of(const Search *const search, const Key key)
{
    return (uint32_t)mix(key.word[0] ^ mix(key.word[1] ^ 0x9e3779b97f4a7c15ull)) & search->mask;
}

static void make_table(Search *const search, const uint32_t slots)
{
    free(search->slots);
    search->slots = calloc(slots, sizeof *search->slots);
    if (!search->slots) {
        fprintf(stderr, "Out of memory asking for %zu bytes; try a lower --limit.\n", (size_t)slots * sizeof *search->slots);
        exit(3);
    }
    search->mask = slots - 1;
    for (uint32_t index = 0; index < search->count; index++) {
        uint32_t slot = slot_of(search, search->keys[index]);
        while (search->slots[slot]) slot = (slot + 1) & search->mask;
        search->slots[slot] = index + 1;
    }
}

static void begin_search(Search *const search, const uint32_t limit)
{
    search->count = 0;
    search->limit = limit;
    if (!search->keys) {
        search->room = limit < 4096 ? limit : 4096;
        search->keys = grow(NULL, search->room, sizeof *search->keys);
        search->links = grow(NULL, search->room, sizeof *search->links);
    }
    if (search->slots) memset(search->slots, 0, ((size_t)search->mask + 1) * sizeof *search->slots);
    else make_table(search, 8192);
}

// Adds a position unless it is known already. Returns whether it was new.
// There must be room under the limit for it.
static bool add(Search *const search, const Key key, const uint32_t link)
{
    if ((uint64_t)search->count * 2 >= (uint64_t)search->mask + 1) make_table(search, (search->mask + 1) * 2);
    uint32_t slot = slot_of(search, key);
    for (uint32_t index; (index = search->slots[slot]) != 0; slot = (slot + 1) & search->mask) {
        const Key *const known = &search->keys[index - 1];
        if (known->word[0] == key.word[0] && known->word[1] == key.word[1]) return false;
    }
    if (search->count == search->room) {
        if (search->room >= search->limit) {
            fputs("A position was added past the limit.\n", stderr);
            exit(3);
        }
        search->room = search->room < search->limit / 2 ? search->room * 2 : search->limit;
        search->keys = grow(search->keys, search->room, sizeof *search->keys);
        search->links = grow(search->links, search->room, sizeof *search->links);
    }
    search->keys[search->count] = key;
    search->links[search->count] = link;
    search->count++;
    search->slots[slot] = search->count;
    return true;
}

typedef struct Outcome {
    // A solution or, alone, a position with everything in place.
    bool found;
    // Every reachable position was explored.
    bool exhausted;
    uint32_t solution; // The solved position's index
    uint32_t positions;
    uint64_t tried; // Moves asked of the handler
    int depth;      // Moves from the start to the last position explored
    double seconds;
} Outcome;

// Breadth first from the start. The shortest search stops at the first
// solved position; alone, it goes on through every reachable position.
static Outcome explore(const Setup *const setup, Search *const search, const uint32_t limit, const bool alone,
    const char *const label)
{
    Outcome outcome = {0};
    const double began = now();
    double reported = began;
    begin_search(search, limit);
    add(search, key_of(setup, &setup->start), 0);
    if (alone && in_place(&setup->layout, &setup->start)) outcome.found = true;
    uint32_t depth_end = 1;
    bool stopped = search->count >= limit;
    for (uint32_t index = 0; index < search->count && !stopped; index++) {
        if (index == depth_end) {
            outcome.depth++;
            depth_end = search->count;
        }
        Puzzle from;
        position_of(setup, search->keys[index], &from);
        for (uint32_t move = 0; move < 8 && !stopped; move++) {
            const int seat = (int)(move >> 2);
            if (seat == setup->absent) continue;
            Puzzle to;
            outcome.tried++;
            if (!step(&from, seat, (int)(move & 3u), &to)) continue;
            if (!strategy_allowed(&setup->layout, &from, &to)) continue;
            if (!add(search, key_of(setup, &to), index << 3 | move)) continue;
            if (alone) {
                if (!outcome.found && in_place(&setup->layout, &to)) {
                    outcome.found = true;
                    outcome.solution = search->count - 1;
                }
            } else if (to.solved) {
                outcome.found = true;
                outcome.solution = search->count - 1;
                stopped = true;
            }
            if (search->count >= limit) stopped = true;
        }
        if ((index & 1023u) == 1023u) {
            const double at = now();
            if (at - reported >= 3.0) {
                char found[32], waiting[32], rate[32], moves[32];
                fprintf(stderr, "%s: %s positions, %d moves deep, %s waiting; %s positions and %s moves a second\n", label,
                    grouped(search->count, found, sizeof found), outcome.depth,
                    grouped(search->count - index - 1, waiting, sizeof waiting),
                    grouped((uint64_t)(search->count / (at - began)), rate, sizeof rate),
                    grouped((uint64_t)((double)outcome.tried / (at - began)), moves, sizeof moves));
                reported = at;
            }
        }
    }
    outcome.exhausted = !stopped;
    outcome.positions = search->count;
    outcome.seconds = now() - began;
    return outcome;
}

// The moves from the start to a position, in test/solutions.h's notation.
// The caller frees the text.
static char *moves_to(const Search *const search, const uint32_t at, int *const length)
{
    int count = 0;
    for (uint32_t index = at; index != 0; index = search->links[index] >> 3) count++;
    uint8_t *const moves = grow(NULL, (size_t)count + 1, 1);
    int filled = count;
    for (uint32_t index = at; index != 0; index = search->links[index] >> 3)
        moves[--filled] = (uint8_t)(search->links[index] & 7u);
    char *const text = grow(NULL, (size_t)count * 2 + 1, 1);
    size_t written = 0;
    for (int i = 0; i < count; i++) {
        if (i > 0 && moves[i] >> 2 != moves[i - 1] >> 2) text[written++] = ' ';
        text[written++] = MOVE_MARKS[moves[i]];
    }
    text[written] = 0;
    free(moves);
    *length = count;
    return text;
}

static bool is_space(const char mark) { return mark == ' ' || mark == '\t' || mark == '\r' || mark == '\n'; }

// The index in `moves` of the first mark that is neither a move nor a
// space, or -1.
static int unreadable_at(const char *const moves)
{
    for (int i = 0; moves[i]; i++)
        if (!is_space(moves[i]) && !strchr(MOVE_MARKS, moves[i])) return i;
    return -1;
}

// Plays moves, read already, from where the open puzzle is. Returns the
// index in `moves` of the first one refused, or -1 when every one was taken.
static int play(const char *const moves, int *const taken)
{
    *taken = 0;
    for (int i = 0; moves[i]; i++) {
        if (is_space(moves[i])) continue;
        const int code = (int)(strchr(MOVE_MARKS, moves[i]) - MOVE_MARKS);
        const int before = world.Workshop.puzzle.moves;
        send(TIDE_COMMAND_ID_Move, code / 4, (const int32_t[]){world.Workshop.serial, code % 4}, 2);
        if (world.Workshop.puzzle.moves == before) return i;
        (*taken)++;
    }
    return -1;
}

static char tile_mark(const LevelLayout *const layout, const int room, const tide_int2 at)
{
    const Tile tile = tile_at(layout, room, at);
    if (tile == Tile_Floor)
        for (int box = 0; box < box_count(layout); box++) {
            const RoomLayout *const inside = &layout->rooms.at[box];
            if (inside->dock.x >= 0 && inside->dockRoom == room && same_cell(inside->dock, at)) return (char)('0' + box);
        }
    return tile >= 0 && tile < (Tile)strlen(TILE_MARKS) ? TILE_MARKS[tile] : '?';
}
static char thing_mark(const LevelLayout *const layout, const Puzzle *const puzzle, const int room, const tide_int2 at)
{
    char mark = '.';
    int things = 0;
    for (int seat = 0; seat < 2; seat++)
        if (puzzle->actors.at[seat].room == room && same_cell(puzzle->actors.at[seat].position, at)) {
            mark = seat == 0 ? '@' : '&';
            things++;
        }
    for (int box = 0; box < box_count(layout); box++)
        if (puzzle->rooms.at[box].room == room && same_cell(puzzle->rooms.at[box].position, at)) {
            mark = (char)('0' + box);
            things++;
        }
    if (battery_at(layout, puzzle, room, at)) {
        mark = '+';
        things++;
    }
    return things > 1 ? '*' : mark;
}

static void print_map(const LevelLayout *const layout, const Puzzle *const puzzle, const int room)
{
    for (int y = 0; y < room_height(layout, room); y++) {
        fputs("    ", stdout);
        for (int x = 0; x < room_width(layout, room); x++) putchar(tile_mark(layout, room, tide_i2(x, y)));
        fputs("  ", stdout);
        for (int x = 0; x < room_width(layout, room); x++) putchar(thing_mark(layout, puzzle, room, tide_i2(x, y)));
        putchar('\n');
    }
}

static const char *room_name(const int room, char *const text, const size_t size)
{
    if (room < 0) snprintf(text, size, "on the table");
    else snprintf(text, size, "in room %d", room);
    return text;
}

static void print_board(const LevelLayout *const layout, const Puzzle *const puzzle)
{
    bool bare = true;
    for (int y = 0; y < room_height(layout, -1); y++)
        for (int x = 0; x < room_width(layout, -1); x++)
            if (tile_mark(layout, -1, tide_i2(x, y)) != '#' || thing_mark(layout, puzzle, -1, tide_i2(x, y)) != '.') bare = false;
    printf("The table, %dx%d", room_width(layout, -1), room_height(layout, -1));
    if (bare) puts(": all wall, with nothing on it.");
    else {
        puts(":");
        print_map(layout, puzzle, -1);
    }
    for (int room = 0; room < box_count(layout); room++) {
        const RoomLayout *const inside = &layout->rooms.at[room];
        const RoomShell box = puzzle->rooms.at[room];
        char where[32];
        printf("\nRoom %d (Box %d in the game), %dx%d. Its box stands %s at (%d, %d)%s", room, room + 1, inside->width,
            inside->height, room_name(box.room, where, sizeof where), box.position.x, box.position.y,
            box.room == room ? ", inside itself" : "");
        if (inside->dock.x < 0) fputs("; it has no outline", stdout);
        else
            printf("; its outline is %s at (%d, %d)%s", room_name(inside->dockRoom, where, sizeof where), inside->dock.x,
                inside->dock.y, box.room == inside->dockRoom && same_cell(box.position, inside->dock) ? ", where it stands" : "");
        const int offsets[4] = {inside->north, inside->east, inside->south, inside->west};
        int doorways = 0;
        for (int side = 0; side < 4; side++) {
            if (!(inside->doors & 1 << side)) continue;
            const int along = offsets[side] == -1 ? (side % 2 == 0 ? inside->width : inside->height) / 2 : offsets[side];
            const tide_int2 door = side == 0 ? tide_i2(along, 0) : side == 1 ? tide_i2(inside->width - 1, along)
                : side == 2 ? tide_i2(along, inside->height - 1) : tide_i2(0, along);
            printf("%s %s at (%d, %d)", doorways++ ? "," : "; doorways", SIDES[side], door.x, door.y);
        }
        puts(doorways ? "." : "; no doorways.");
        print_map(layout, puzzle, room);
    }
    putchar('\n');
    for (int seat = 0; seat < 2; seat++) {
        const Traveler *const robot = &puzzle->actors.at[seat];
        char where[32];
        if (robot->position.x < 0) printf("%s (%c, seat %d) is off the board.\n", ROBOTS[seat], seat ? '&' : '@', seat);
        else
            printf("%s (%c, seat %d) is %s at (%d, %d), facing %s.\n", ROBOTS[seat], seat ? '&' : '@', seat,
                room_name(robot->room, where, sizeof where), robot->position.x, robot->position.y,
                robot->facing >= 0 && robot->facing < 4 ? SIDES[robot->facing] : "nowhere");
    }
    printf("%d moves made; %s.\n", puzzle->moves, puzzle->solved ? "solved" : "not solved");
}

static void print_title(const int level)
{
    if (!describe(level)) return;
    printf("Puzzle %d of %d: %s (chapter %d, %s)\n%s\n", level + 1, words.puzzles, words.name, words.chapter + 1,
        words.chapter_name, words.lesson);
    if (no_battery_plates) puts("Design probe: battery placement on either channel's plate is forbidden.");
    if (keep_docked) puts("Design probe: a box may not leave an outline after occupying it.");
}

static Search search;

static int print_level(const int level)
{
    print_title(level);
    puts("Maps are drawn as in levels.tide: tiles, then what stands on them. Cells are (x, y) from the top left.\n");
    print_board(&world.Workshop.layout, &world.Workshop.puzzle);
    return 0;
}

static int play_level(const int level, const char *const moves)
{
    print_title(level);
    int taken = 0;
    const int refused = play(moves, &taken);
    // A refused move changes nothing, so this is the board before it.
    const Puzzle *const board = &world.Workshop.puzzle;
    if (refused < 0) printf("Played all %d moves.\n\n", taken);
    else {
        const int code = (int)(strchr(MOVE_MARKS, moves[refused]) - MOVE_MARKS);
        printf("Move %d, '%c' (%s %s) at character %d, was refused%s. The board before it:\n\n", taken + 1, moves[refused],
            ROBOTS[code / 4], SIDES[code % 4], refused + 1, board->solved ? ": the puzzle was already solved" : "");
    }
    print_board(&world.Workshop.layout, board);
    return refused < 0 ? 0 : 1;
}

static int solve_level(const int level, const uint32_t limit)
{
    print_title(level);
    const Setup setup = setup_of(-1);
    char label[32], positions[32], tried[32], rate[32], moves_rate[32], most[32];
    snprintf(label, sizeof label, "Puzzle %d", level + 1);
    const Outcome outcome = explore(&setup, &search, limit, false, label);
    const double seconds = outcome.seconds > 1e-9 ? outcome.seconds : 1e-9;
    int status = 0;
    if (outcome.found) {
        int length = 0;
        char *const moves = moves_to(&search, outcome.solution, &length);
        printf("Shortest: %d moves\nMoves: %s\n", length, moves);
        int taken = 0;
        const bool replayed = open_level(level) && play(moves, &taken) < 0 && world.Workshop.puzzle.solved;
        printf("Replayed from the start through the Move handler: %s.\n", replayed ? "solved" : "NOT SOLVED");
        if (!replayed) status = 1;
        free(moves);
    } else if (outcome.exhausted)
        printf("No solution: every reachable position was explored, the farthest %d moves from the start.\n", outcome.depth);
    else
        printf("No solution within the limit of %s positions (%d moves deep); a higher --limit looks further.\n",
            grouped(limit, most, sizeof most), outcome.depth);
    printf("Positions explored: %s (%s moves asked of the handler) in %.2f s: %s positions and %s moves a second.\n",
        grouped(outcome.positions, positions, sizeof positions), grouped(outcome.tried, tried, sizeof tried), outcome.seconds,
        grouped((uint64_t)(outcome.positions / seconds), rate, sizeof rate),
        grouped((uint64_t)((double)outcome.tried / seconds), moves_rate, sizeof moves_rate));
    printf("Every reachable position explored: %s.\n",
        outcome.exhausted ? "yes" : outcome.found ? "no, the search stops at the first solution" : "no, the limit stopped it");
    return status;
}

static int alone_level(const int level, const int seat, const uint32_t limit)
{
    print_title(level);
    const Setup setup = setup_of(1 - seat);
    char label[48], positions[32], most[32];
    snprintf(label, sizeof label, "Puzzle %d, %s alone", level + 1, ROBOTS[seat]);
    const Outcome outcome = explore(&setup, &search, limit, true, label);
    if (outcome.found && outcome.solution > 0) {
        int length = 0;
        char *const moves = moves_to(&search, outcome.solution, &length);
        printf("Alone placement witness (%d moves): %s\n", length, moves);
        free(moves);
    }
    printf("%s (seat %d) alone, %s off the board: every battery seated, every socket filled and every box on its outline "
           "is %s.\n",
        ROBOTS[seat], seat, ROBOTS[1 - seat], outcome.found ? "REACHABLE" : outcome.exhausted ? "unreachable" : "not reached");
    if (outcome.exhausted)
        printf("Positions: %s, every one reachable, the farthest %d moves from the start (%.2f s).\n",
            grouped(outcome.positions, positions, sizeof positions), outcome.depth, outcome.seconds);
    else
        printf("Positions: %s before the limit of %s stopped the search, %d moves deep (%.2f s); not every position was "
               "explored.\n",
            grouped(outcome.positions, positions, sizeof positions), grouped(limit, most, sizeof most), outcome.depth,
            outcome.seconds);
    return 0;
}

static int solve_all(const int puzzles, const uint32_t limit)
{
    int width = 4;
    for (int level = 0; level < puzzles; level++)
        if (describe(level) && (int)strlen(words.name) > width) width = (int)strlen(words.name);
    printf("Puzzle  %-*s  Shortest     Positions  Exhausted  Seconds\n", width, "Name");
    double total = 0.0;
    int status = 0;
    for (int level = 0; level < puzzles; level++) {
        if (!open_level(level) || !describe(level)) {
            printf("%6d  the game did not open it\n", level + 1);
            status = 1;
            continue;
        }
        char name[128], label[32], positions[32], shortest[16];
        snprintf(name, sizeof name, "%s", words.name);
        snprintf(label, sizeof label, "Puzzle %d", level + 1);
        const Setup setup = setup_of(-1);
        const Outcome outcome = explore(&setup, &search, limit, false, label);
        int length = 0;
        if (outcome.found) free(moves_to(&search, outcome.solution, &length));
        if (outcome.found) snprintf(shortest, sizeof shortest, "%d", length);
        else snprintf(shortest, sizeof shortest, "%s", outcome.exhausted ? "none" : "-");
        printf("%6d  %-*s  %8s  %12s  %-9s  %7.2f\n", level + 1, width, name, shortest,
            grouped(outcome.positions, positions, sizeof positions), outcome.exhausted ? "yes" : "no", outcome.seconds);
        fflush(stdout);
        total += outcome.seconds;
    }
    printf("%.2f s in all. Shortest '-' is none within the limit; 'none' is none at all.\n", total);
    return status;
}

static int usage(void)
{
    fputs("inside_out_solve <level> --print\n"
          "inside_out_solve <level> [--limit <positions>]\n"
          "inside_out_solve <level> --alone <seat> [--limit <positions>]\n"
          "inside_out_solve <level> --play \"<moves>\"\n"
          "inside_out_solve all [--limit <positions>]\n"
          "Design probes: --no-battery-plates or --keep-docked remove that strategy from search.\n"
          "Levels count from 1; moves are NESW for Amber (seat 0), nesw for Teal (seat 1).\n",
        stderr);
    return 2;
}

// A whole number from `least` to `most`, digits alone.
static bool read_number(const char *const text, const long long least, const long long most, long long *const value)
{
    if (!text[0]) return false;
    long long number = 0;
    for (const char *at = text; *at; at++) {
        if (*at < '0' || *at > '9') return false;
        number = number * 10 + (*at - '0');
        if (number > most) return false;
    }
    if (number < least) return false;
    *value = number;
    return true;
}

int main(const int argc, char **const argv)
{
    if (argc < 2) return usage();
    bool print = false;
    int alone = -1;
    const char *moves = NULL;
    long long limit = DEFAULT_LIMIT;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--print") == 0) print = true;
        else if (strcmp(argv[i], "--no-battery-plates") == 0) no_battery_plates = true;
        else if (strcmp(argv[i], "--keep-docked") == 0) keep_docked = true;
        else if (strcmp(argv[i], "--alone") == 0 && i + 1 < argc) {
            long long seat = 0;
            if (!read_number(argv[++i], 0, 1, &seat)) {
                fprintf(stderr, "--alone takes a seat, 0 (Amber) or 1 (Teal), not '%s'.\n", argv[i]);
                return 2;
            }
            alone = (int)seat;
        } else if (strcmp(argv[i], "--play") == 0 && i + 1 < argc) moves = argv[++i];
        else if (strcmp(argv[i], "--limit") == 0 && i + 1 < argc) {
            if (!read_number(argv[++i], 1, MOST_LIMIT - 1, &limit)) {
                fprintf(stderr, "--limit takes a number of positions from 1 to %u, not '%s'.\n", MOST_LIMIT - 1, argv[i]);
                return 2;
            }
        } else {
            fprintf(stderr, "'%s' is not an option here.\n", argv[i]);
            return usage();
        }
    }
    if ((print ? 1 : 0) + (alone >= 0 ? 1 : 0) + (moves ? 1 : 0) > 1) {
        fputs("--print, --alone and --play go one at a time.\n", stderr);
        return 2;
    }
    const int unreadable = moves ? unreadable_at(moves) : -1;
    if (unreadable >= 0) {
        fprintf(stderr, "'%c' at character %d is not a move: NESW are Amber's (seat 0), nesw Teal's (seat 1).\n",
            moves[unreadable], unreadable + 1);
        return 2;
    }
    if (!open_level(0) || !describe(0)) {
        fputs("The game did not open its first puzzle.\n", stderr);
        return 3;
    }
    const int puzzles = words.puzzles;
    int status = 0;
    if (strcmp(argv[1], "all") == 0) {
        if (print || alone >= 0 || moves) return usage();
        status = solve_all(puzzles, (uint32_t)limit);
    } else {
        long long chosen = 0;
        if (!read_number(argv[1], 1, puzzles, &chosen)) {
            fprintf(stderr, "'%s' is not a puzzle: they are 1 to %d, or all.\n", argv[1], puzzles);
            return 2;
        }
        const int level = (int)chosen - 1;
        if (!open_level(level)) {
            fprintf(stderr, "The game did not open puzzle %d.\n", level + 1);
            return 3;
        }
        if (print) status = print_level(level);
        else if (moves) status = play_level(level, moves);
        else if (alone >= 0) status = alone_level(level, alone, (uint32_t)limit);
        else status = solve_level(level, (uint32_t)limit);
    }
    free(search.keys);
    free(search.links);
    free(search.slots);
    tide_world_free(&world);
    return status;
}
