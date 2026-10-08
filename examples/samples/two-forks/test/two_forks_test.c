// Two Forks with no window: the rules a move at a time, on boards made up
// here, and then every level of the game tried out in full. A level's board is
// small enough that every position two chefs can get it into can be visited,
// by pressing every key in every one of them, in the game's own match: so the
// test knows the fewest moves a level takes, and that there's no cooking it
// with one chef's moves alone, whatever they do.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/net.h"
#include "tide/session.h"
#include "tide_test.h"

#define DT (1.0f / 60.0f)
#define W 12
#define STEP 8       // Ticks a move takes
#define HOLD 15      // Ticks a key is held before it repeats
#define CHEER 190    // Ticks a cooked level is looked at

enum { NORTH, EAST, SOUTH, WEST };

static tide_world world;

static tide_player_id player(const int index)
{
    return tide_player_from_index(index);
}

static Controls keys(const int way)
{
    return (Controls){.north = way == NORTH, .east = way == EAST, .south = way == SOUTH, .west = way == WEST};
}

// A match of this many players, with its first level laid out.
static void start(const int players)
{
    tide_world_free(&world);
    tide_world_init(&world, DT);
    for (int i = 0; i < players; i++) tide_world_player_joined(&world, player(i));
    tide_world_tick(&world);
}

static void tick(const int ticks)
{
    for (int i = 0; i < ticks; i++) tide_world_tick(&world);
}

// A key goes down for one tick, and is let go: a chef's move, when it's theirs
// to make. The other player presses nothing.
static void press(const int who, const int way)
{
    tide_world_set_input(&world, player(who), keys(way));
    tide_world_tick(&world);
    tide_world_set_input(&world, player(who), (Controls){0});
    tide_world_tick(&world);
}

// ...and once the move it made is over, so the next one isn't kept waiting.
static void move(const int who, const int way)
{
    press(who, way);
    tick(STEP);
}

// A board made up here, in the plans' own characters, in place of the level's.
static void lay(const char *plan)
{
    Board *board = &world.Kitchen.board;
    memset(board, 0, sizeof *board);
    int x = 0;
    int y = 0;
    for (const char *c = plan; *c; c++) {
        if (*c == '/') {
            x = 0;
            y++;
            continue;
        }
        Tile tile = Tile_Grass;
        if (*c == '~') tile = Tile_Water;
        if (*c == '#') tile = Tile_Coal;
        if (*c == '=') tile = Tile_Gas;
        if (*c == 'o') tile = Tile_Pedal;
        if (*c == '^') tile = Tile_Rock;
        board->tiles.at[y * W + x] = tile;
        if (x + 1 > board->size.x) board->size.x = x + 1;
        board->size.y = y + 1;
        const char *first = strchr("nesw", *c);
        const char *second = strchr("NESW", *c);
        if (first) board->chefs.at[0] = (Chef){.at = {x, y}, .facing = (int32_t)(first - "nesw"), .spin = (int32_t)(first - "nesw")};
        if (second) board->chefs.at[1] = (Chef){.at = {x, y}, .facing = (int32_t)(second - "NESW"), .spin = (int32_t)(second - "NESW")};
        if (*c == '-' || *c == '|') {
            bool begun = false;
            for (int i = 0; i < board->count; i++) {
                const Sausage *s = &board->sausages.at[i];
                if (s->at.x + (s->upright ? 0 : 1) == x && s->at.y + (s->upright ? 1 : 0) == y) begun = true;
            }
            if (!begun) board->sausages.at[board->count++] = (Sausage){.at = {x, y}, .upright = *c == '|'};
        }
        x++;
    }
    world.Kitchen.cleared = 0;
    world.Kitchen.moves = 0;
    memset(&world.Kitchen.grips, 0, sizeof world.Kitchen.grips);
}

static const Chef *chef(const int who)
{
    return &world.Kitchen.board.chefs.at[who];
}

static const Sausage *sausage(const int which)
{
    return &world.Kitchen.board.sausages.at[which];
}

static bool chef_at(const int who, const int x, const int y, const int facing)
{
    return chef(who)->at.x == x && chef(who)->at.y == y && chef(who)->facing == facing;
}

static bool sausage_at(const int which, const int x, const int y)
{
    return sausage(which)->at.x == x && sausage(which)->at.y == y;
}

// How often a side of a sausage has been on a grill: its first half's side
// that's down as it's laid, its other side, then the other half's two.
static int cooked(const int which, const int side)
{
    return sausage(which)->cooked.at[side];
}

TIDE_TEST(two_forks_a_step_pushes_and_a_turn_sweeps)
{
    start(2);
    // Forward, the fork pushes what's ahead of it: across its length a sausage rolls onto its other side
    lay("~~~~~~/e.|...E/..|.../......");
    move(0, EAST);
    TIDE_CHECK(chef_at(0, 1, 1, EAST));
    TIDE_CHECK(sausage_at(0, 3, 1) && sausage(0)->rolled == 1);
    move(0, EAST);
    TIDE_CHECK(chef_at(0, 2, 1, EAST));
    TIDE_CHECK(sausage_at(0, 4, 1) && sausage(0)->rolled == 2);
    TIDE_CHECK(world.Kitchen.moves == 2);
    // ...and back onto the first when it's pushed the other way
    lay("~~~~~~/E..|.w/...|..");
    move(0, WEST);
    TIDE_CHECK(sausage_at(0, 2, 1) && sausage(0)->rolled == -1);

    // Along its length it slides, and stays on the side it was on
    lay("~~~~~~/e.--..E/......");
    move(0, EAST);
    TIDE_CHECK(sausage_at(0, 3, 1) && sausage(0)->rolled == 0);

    // Backwards, a chef goes without turning, and its own back pushes
    lay("~~~~~~~/..w..E~");
    move(0, EAST);
    TIDE_CHECK(chef_at(0, 3, 1, WEST));
    lay("~~~~~~~/.|.e..W/.|.....");
    move(0, WEST);
    TIDE_CHECK(chef_at(0, 2, 1, EAST) && sausage_at(0, 1, 1));
    move(0, WEST);
    TIDE_CHECK(chef_at(0, 1, 1, EAST) && sausage_at(0, 0, 1) && sausage(0)->rolled == -1);

    // A turn takes the fork through the corner, where it pushes the way it
    // turns, and on to the chef's side, where it pushes back the way it came from
    lay("....../..--../.n|..E/..|.../......");
    move(0, EAST);
    TIDE_CHECK(chef_at(0, 1, 2, EAST) && chef(0)->spin == 1);
    TIDE_CHECK(sausage_at(0, 3, 1) && sausage(0)->rolled == 0); // The one in the corner went east, along its length
    TIDE_CHECK(sausage_at(1, 2, 3) && sausage(1)->rolled == 0); // ...and the one beside the chef south, along its own
    lay("..|.../..|.../.n...E");
    move(0, EAST);
    TIDE_CHECK(sausage_at(0, 3, 0) && sausage(0)->rolled == 1); // In the corner, across its length: it rolls
    lay(".|..../.|..../..n..E");
    move(0, WEST);
    TIDE_CHECK(chef_at(0, 2, 2, WEST) && chef(0)->spin == -1);
    TIDE_CHECK(sausage_at(0, 0, 0) && sausage(0)->rolled == -1);
    lay("....../....../.n--.E/....../......");
    move(0, EAST);
    TIDE_CHECK(chef_at(0, 1, 2, EAST));
    TIDE_CHECK(sausage_at(0, 2, 3) && sausage(0)->rolled == 1); // Beside the chef, across its length: it rolls south

    tide_world_free(&world);
}

TIDE_TEST(two_forks_what_stops_a_move_stops_all_of_it)
{
    start(2);
    // A rock stops a sausage, and so the chef
    lay("e.|^..E/..|....");
    move(0, EAST);
    TIDE_CHECK(chef_at(0, 0, 0, EAST) && sausage_at(0, 2, 0));
    TIDE_CHECK(world.Kitchen.moves == 0);

    // A sausage pushes the next one, and both stop where the second can't go
    lay("e.||..E/..||...");
    move(0, EAST);
    TIDE_CHECK(sausage_at(0, 3, 0) && sausage_at(1, 4, 0));
    lay("e.||^.E/..||...");
    move(0, EAST);
    TIDE_CHECK(chef_at(0, 0, 0, EAST) && sausage_at(0, 2, 0) && sausage_at(1, 3, 0));

    // The other chef is in the way with its body, and with its fork
    lay("e.|E../..|...");
    move(0, EAST);
    TIDE_CHECK(sausage_at(0, 2, 0));
    lay("e.|.W./..|...");
    move(0, EAST);
    TIDE_CHECK(sausage_at(0, 2, 0));
    lay("e.|..W/..|...");
    move(0, EAST);
    TIDE_CHECK(sausage_at(0, 3, 0));

    // A turn whose second push is stopped takes back its first
    lay(".|..../.|..../n--..E/.^....");
    move(0, EAST);
    TIDE_CHECK(chef_at(0, 0, 2, NORTH));
    TIDE_CHECK(sausage_at(0, 1, 0) && sausage_at(1, 1, 2));

    // Nobody walks on water or on a grill, though a fork goes over both
    lay("e.~./...N");
    move(0, EAST);
    TIDE_CHECK(chef_at(0, 1, 0, EAST));
    move(0, EAST);
    TIDE_CHECK(chef_at(0, 1, 0, EAST));
    lay("e.#./...N");
    move(0, EAST);
    move(0, EAST);
    TIDE_CHECK(chef_at(0, 1, 0, EAST));

    tide_world_free(&world);
}

TIDE_TEST(two_forks_a_grill_cooks_the_side_that_lands_on_it)
{
    start(2);
    // Rolled onto coals, a sausage is cooked on the side that's now down, both halves
    lay("e.|#../..|#.E");
    move(0, EAST);
    TIDE_CHECK(cooked(0, 0) == 0 && cooked(0, 1) == 1 && cooked(0, 2) == 0 && cooked(0, 3) == 1);
    TIDE_CHECK(world.Kitchen.cleared == 0);
    // ...and it doesn't go on cooking there
    tick(200);
    TIDE_CHECK(cooked(0, 1) == 1);

    // Its two sides on two rows of coals, and it's cooked
    lay("e.|##./..|##E");
    move(0, EAST);
    move(0, EAST);
    TIDE_CHECK(cooked(0, 0) == 1 && cooked(0, 1) == 1 && cooked(0, 2) == 1 && cooked(0, 3) == 1);
    TIDE_CHECK(world.Kitchen.cleared != 0);

    // Slid over one coal, each half lands on it in turn, on the same side
    lay("e.--#../......E");
    move(0, EAST);
    TIDE_CHECK(cooked(0, 0) == 0 && cooked(0, 2) == 1);
    move(0, EAST);
    TIDE_CHECK(cooked(0, 0) == 1 && cooked(0, 2) == 1 && cooked(0, 1) == 0);

    // The same side twice is burnt
    lay("e.--##./......E");
    move(0, EAST);
    move(0, EAST);
    TIDE_CHECK(cooked(0, 2) == 2 && cooked(0, 0) == 1);

    // Half over the water it stays, and with no land under it it's gone
    lay("e.--~~./......E");
    move(0, EAST);
    TIDE_CHECK(!sausage(0)->sunk);
    move(0, EAST);
    TIDE_CHECK(sausage(0)->sunk);

    tide_world_free(&world);
}

TIDE_TEST(two_forks_the_pedal_cuts_the_gas)
{
    start(2);
    // Lit, gas cooks what lands on it, as coals do
    lay("e.|=../..|=.E/...o..");
    move(0, EAST);
    TIDE_CHECK(cooked(0, 1) == 1 && cooked(0, 3) == 1);

    // With a chef on the pedal it cooks nothing...
    lay("e.|=../..|=../....oW");
    move(1, WEST);
    TIDE_CHECK(chef_at(1, 4, 2, WEST));
    move(0, EAST);
    TIDE_CHECK(sausage_at(0, 3, 0) && cooked(0, 1) == 0 && cooked(0, 3) == 0);
    // ...until they step off it: what's lying on the gas is cooked then, once
    move(1, EAST);
    TIDE_CHECK(chef_at(1, 5, 2, WEST));
    TIDE_CHECK(cooked(0, 1) == 1 && cooked(0, 3) == 1);
    tick(200);
    TIDE_CHECK(cooked(0, 1) == 1);
    move(1, WEST);
    move(1, EAST);
    TIDE_CHECK(cooked(0, 1) == 2); // Lit again under it: burnt

    tide_world_free(&world);
}

static void swap_hands(const int who)
{
    tide_world_set_input(&world, player(who), (Controls){.swap = true});
    tide_world_tick(&world);
    tide_world_set_input(&world, player(who), (Controls){0});
    tide_world_tick(&world);
}

TIDE_TEST(two_forks_a_player_alone_has_both_chefs)
{
    start(1);
    lay("e...../....../.....W");
    TIDE_CHECK(world.Kitchen.lead == 0);
    move(0, EAST);
    TIDE_CHECK(chef_at(0, 1, 0, EAST) && chef_at(1, 5, 2, WEST));
    // The other chef, at a press
    swap_hands(0);
    TIDE_CHECK(world.Kitchen.lead == 1);
    move(0, WEST);
    TIDE_CHECK(chef_at(0, 1, 0, EAST) && chef_at(1, 4, 2, WEST));

    // A second player has the other chef, and nobody changes hands any more
    tide_world_player_joined(&world, player(1));
    tide_world_tick(&world);
    swap_hands(0);
    move(0, EAST);
    move(1, WEST);
    TIDE_CHECK(chef_at(0, 2, 0, EAST) && chef_at(1, 3, 2, WEST));

    // Whoever stays when one leaves has both again
    tide_world_player_left(&world, player(0));
    tide_world_tick(&world);
    world.Kitchen.lead = 0;
    move(1, EAST);
    TIDE_CHECK(chef_at(0, 3, 0, EAST));
    swap_hands(1);
    move(1, WEST);
    TIDE_CHECK(chef_at(1, 2, 2, WEST));

    tide_world_free(&world);
}

TIDE_TEST(two_forks_a_held_key_repeats_and_a_quick_one_waits_its_turn)
{
    start(2);
    lay("e........../........../.........W");
    // Held: a move at once, the next after the wait, and then one as each ends
    tide_world_set_input(&world, player(0), keys(EAST));
    tide_world_tick(&world);
    TIDE_CHECK(chef_at(0, 1, 0, EAST));
    tick(HOLD - 1);
    TIDE_CHECK(chef_at(0, 1, 0, EAST));
    tide_world_tick(&world);
    TIDE_CHECK(chef_at(0, 2, 0, EAST));
    tick(STEP);
    TIDE_CHECK(chef_at(0, 3, 0, EAST));
    tide_world_set_input(&world, player(0), (Controls){0});
    tick(STEP * 2);
    TIDE_CHECK(chef_at(0, 3, 0, EAST));

    // Pressed again before the move is over, the second press isn't lost
    press(0, EAST);
    press(0, EAST);
    TIDE_CHECK(chef_at(0, 4, 0, EAST));
    tick(STEP);
    TIDE_CHECK(chef_at(0, 5, 0, EAST));

    // Both players in one tick: both chefs move
    tick(STEP);
    tide_world_set_input(&world, player(0), keys(EAST));
    tide_world_set_input(&world, player(1), keys(WEST));
    tide_world_tick(&world);
    TIDE_CHECK(chef_at(0, 6, 0, EAST) && chef_at(1, 8, 2, WEST));

    tide_world_free(&world);
}

// A command for the whole match, which says the level it was sent in.
static bool command(const uint32_t id, const int sender, const int level)
{
    uint8_t bytes[TIDE_COMMAND_BYTES];
    tide_bits bits = {bytes, sizeof bytes, 0, false};
    tide_bits_put(&bits, id, 16);
    tide_bits_put(&bits, 0, 32);
    tide_bits_put(&bits, 0, 32);
    tide_bits_put_varint(&bits, tide_zigzag(level));
    const uint32_t size = tide_bits_end(&bits);
    return tide_game_api.command(&world, player(sender), 0,
        tide_game_api.command_sequence(&world, player(sender), 0) + 1, bytes, size);
}

TIDE_TEST(two_forks_moves_are_taken_back)
{
    start(2);
    lay("e.|#../..|#.E");
    const int level = world.Kitchen.level;
    move(0, EAST);
    move(0, EAST);
    TIDE_CHECK(cooked(0, 1) == 1 && sausage_at(0, 4, 0) && world.Kitchen.moves == 2);

    // Either player takes back the last move, whoever made it
    TIDE_CHECK(command(TIDE_COMMAND_ID_Undo, 1, level));
    tide_world_tick(&world);
    TIDE_CHECK(sausage_at(0, 3, 0) && cooked(0, 1) == 1 && chef_at(0, 1, 0, EAST) && world.Kitchen.moves == 1);
    TIDE_CHECK(command(TIDE_COMMAND_ID_Undo, 0, level));
    tide_world_tick(&world);
    TIDE_CHECK(sausage_at(0, 2, 0) && cooked(0, 1) == 0 && chef_at(0, 0, 0, EAST) && world.Kitchen.moves == 0);
    // With none left, nothing happens
    TIDE_CHECK(command(TIDE_COMMAND_ID_Undo, 0, level));
    tide_world_tick(&world);
    TIDE_CHECK(sausage_at(0, 2, 0) && world.Kitchen.moves == 0);

    // One sent in another level is too late for this one
    move(0, EAST);
    TIDE_CHECK(command(TIDE_COMMAND_ID_Undo, 0, level + 1));
    tide_world_tick(&world);
    TIDE_CHECK(sausage_at(0, 3, 0));

    tide_world_free(&world);
}

TIDE_TEST(two_forks_a_level_starts_over_and_that_is_taken_back_too)
{
    start(2);
    const int level = world.Kitchen.level;
    const Board laid = world.Kitchen.board;
    // Nothing moved: there's nothing to start over
    TIDE_CHECK(command(TIDE_COMMAND_ID_Restart, 0, level));
    tide_world_tick(&world);
    TIDE_CHECK(world.Kitchen.moves == 0);

    // Any move there is
    for (int way = 0; way < 8 && memcmp(&world.Kitchen.board, &laid, sizeof laid) == 0; way++) move(way / 4, way % 4);
    const Board moved = world.Kitchen.board;
    TIDE_REQUIRE(memcmp(&moved, &laid, sizeof laid) != 0);
    TIDE_CHECK(command(TIDE_COMMAND_ID_Restart, 1, level));
    tide_world_tick(&world);
    TIDE_CHECK(memcmp(&world.Kitchen.board, &laid, sizeof laid) == 0);
    TIDE_CHECK(command(TIDE_COMMAND_ID_Undo, 0, level));
    tide_world_tick(&world);
    TIDE_CHECK(memcmp(&world.Kitchen.board, &moved, sizeof moved) == 0);

    tide_world_free(&world);
}

static void go_to(const int level)
{
    world.Kitchen.level = level;
    world.Kitchen.laid = false;
    tide_world_tick(&world);
}

// How many levels there are: the game goes no further than its last.
static int levels(void)
{
    go_to(1000);
    return world.Kitchen.level + 1;
}

TIDE_TEST(two_forks_a_cooked_level_is_followed_by_the_next)
{
    start(2);
    const int count = levels();
    TIDE_REQUIRE(count >= 2);
    go_to(0);
    TIDE_CHECK(command(TIDE_COMMAND_ID_Goto, 0, 1));
    tide_world_tick(&world);
    TIDE_CHECK(world.Kitchen.level == 1 && world.Kitchen.done == 0);
    TIDE_CHECK(command(TIDE_COMMAND_ID_Goto, 0, count));
    tide_world_tick(&world);
    TIDE_CHECK(world.Kitchen.level == 1);

    // Cooked: it's looked at for a while, with no more moves, and then the next is laid
    lay("e.|##./..|##E");
    move(0, EAST);
    move(0, EAST);
    TIDE_REQUIRE(world.Kitchen.cleared != 0);
    TIDE_CHECK(world.Kitchen.done == 1 << 1);
    move(1, WEST);
    TIDE_CHECK(chef_at(1, 5, 1, EAST));
    tick(CHEER);
    TIDE_CHECK(world.Kitchen.level == 2 % count && world.Kitchen.cleared == 0 && world.Kitchen.moves == 0);

    // After the last one, the first
    go_to(count - 1);
    lay("e.|##./..|##E");
    move(0, EAST);
    move(0, EAST);
    tick(CHEER);
    TIDE_CHECK(world.Kitchen.level == 0);

    tide_world_free(&world);
}

// ------------------------------------------------- every position of a level

// A position: where the chefs are and which way they face, where each sausage
// is, which side it's on and how done its sides are, and whether it's cooked.
#define KEY 35

typedef struct seen {
    uint8_t (*keys)[KEY];
    uint32_t *slots;     // A key's number plus one, by its hash
    uint32_t count;
    uint32_t room;
    uint32_t mask;
} seen;

static void key_of(uint8_t key[KEY])
{
    const Board *board = &world.Kitchen.board;
    memset(key, 0, KEY);
    for (int who = 0; who < 2; who++) {
        key[who * 3] = (uint8_t)board->chefs.at[who].at.x;
        key[who * 3 + 1] = (uint8_t)board->chefs.at[who].at.y;
        key[who * 3 + 2] = (uint8_t)board->chefs.at[who].facing;
    }
    for (int i = 0; i < board->count; i++) {
        const Sausage *s = &board->sausages.at[i];
        uint8_t *at = key + 6 + i * 7;
        at[0] = (uint8_t)(s->at.x + 2);
        at[1] = (uint8_t)(s->at.y + 2);
        at[2] = (uint8_t)(s->rolled & 1);
        for (int side = 0; side < 4; side++) at[3 + side] = (uint8_t)s->cooked.at[side];
    }
    key[KEY - 1] = world.Kitchen.cleared != 0;
}

// The board put into a position, as a move would have left it.
static void put(const uint8_t key[KEY])
{
    Board *board = &world.Kitchen.board;
    for (int who = 0; who < 2; who++) {
        board->chefs.at[who] = (Chef){.at = {key[who * 3], key[who * 3 + 1]}, .facing = key[who * 3 + 2], .spin = key[who * 3 + 2]};
    }
    for (int i = 0; i < board->count; i++) {
        const uint8_t *at = key + 6 + i * 7;
        Sausage *s = &board->sausages.at[i];
        s->at = (tide_int2){at[0] - 2, at[1] - 2};
        s->rolled = at[2];
        s->sunk = false;
        for (int side = 0; side < 4; side++) s->cooked.at[side] = at[3 + side];
    }
    world.Kitchen.cleared = 0;
    memset(&world.Kitchen.grips, 0, sizeof world.Kitchen.grips);
}

static bool spoiled(void)
{
    const Board *board = &world.Kitchen.board;
    for (int i = 0; i < board->count; i++) {
        if (board->sausages.at[i].sunk) return true;
        for (int side = 0; side < 4; side++) {
            if (board->sausages.at[i].cooked.at[side] > 1) return true;
        }
    }
    return false;
}

static uint64_t hash_of(const uint8_t key[KEY])
{
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    for (int i = 0; i < KEY; i++) hash = (hash ^ key[i]) * UINT64_C(0x100000001b3);
    return hash ^ hash >> 29;
}

// Whether a position is new, and then it's kept.
static bool add(seen *all, const uint8_t key[KEY])
{
    if (all->count * 2 >= all->room) {
        all->room = all->room ? all->room * 2 : 1 << 12;
        all->mask = all->room - 1;
        all->keys = realloc(all->keys, (size_t)all->room / 2 * sizeof *all->keys);
        free(all->slots);
        all->slots = calloc(all->room, sizeof *all->slots);
        for (uint32_t i = 0; i < all->count; i++) {
            uint32_t at = (uint32_t)hash_of(all->keys[i]) & all->mask;
            while (all->slots[at]) at = (at + 1) & all->mask;
            all->slots[at] = i + 1;
        }
    }
    uint32_t at = (uint32_t)hash_of(key) & all->mask;
    while (all->slots[at]) {
        if (memcmp(all->keys[all->slots[at] - 1], key, KEY) == 0) return false;
        at = (at + 1) & all->mask;
    }
    memcpy(all->keys[all->count], key, KEY);
    all->slots[at] = ++all->count;
    return true;
}

typedef struct found {
    int positions;   // Every position that can be reached with nothing spoiled
    int moves;       // The fewest moves to a cooked one, or -1 when there's none
} found;

// Every position a level can be got into by these chefs' moves: each key is
// pressed in each one, in the match, and what it leaves is looked up. A
// position with a sausage burnt or sunk is a dead end, as nothing unburns one,
// and a cooked one is the end: the match takes no more moves there.
static found explore(const int level, const bool first, const bool second)
{
    go_to(level);
    seen all = {0};
    uint8_t key[KEY];
    key_of(key);
    add(&all, key);
    found result = {.moves = -1};
    uint32_t from = 0;
    for (int depth = 1; from < all.count; depth++) {
        const uint32_t until = all.count;
        for (; from < until; from++) {
            if (all.keys[from][KEY - 1]) continue;
            for (int who = 0; who < 2; who++) {
                if (who == 0 ? !first : !second) continue;
                for (int way = 0; way < 4; way++) {
                    put(all.keys[from]);
                    press(who, way);
                    if (spoiled()) continue;
                    key_of(key);
                    if (add(&all, key) && key[KEY - 1] && result.moves < 0) result.moves = depth;
                }
            }
        }
    }
    result.positions = (int)all.count;
    free(all.keys);
    free(all.slots);
    return result;
}

// What each level takes: the fewest moves with two chefs, and how many
// positions there are to look through, with both chefs moving and with only
// the first or only the second, where none is cooked. The numbers are from
// the search the levels were designed with (tools/solve.mjs), which has the
// rules written a second time: that they agree here, position for position,
// is what says the two are the same game.
typedef struct expected {
    int moves;
    int positions;
    int first_alone;
    int second_alone;
} expected;

static const expected LEVEL[] = {
    {5, 8624, 81, 36},     // 1. Pass it over
    {4, 10890, 236, 16},   // 2. Over the coals
    {5, 4230, 33, 12},     // 3. Hold the gas
    {8, 255, 27, 8},       // 4. Too many cooks
    {9, 3706, 16, 92},     // 5. One burner
    {13, 1601, 8, 24},     // 6. Neighbours
    {12, 10761, 62, 90},   // 7. Two for dinner
    {22, 3674, 244, 4},    // 8. The narrows
};

TIDE_TEST(two_forks_every_level_takes_both_chefs)
{
    start(2);
    const int count = levels();
    TIDE_CHECK(count == (int)(sizeof LEVEL / sizeof LEVEL[0]));
    for (int level = 0; level < count && level < (int)(sizeof LEVEL / sizeof LEVEL[0]); level++) {
        const found both = explore(level, true, true);
        const found first = explore(level, true, false);
        const found second = explore(level, false, true);
        const expected want = LEVEL[level];
        const bool right = both.moves == want.moves && both.positions == want.positions
                        && first.moves < 0 && first.positions == want.first_alone
                        && second.moves < 0 && second.positions == want.second_alone;
        if (!right) {
            printf("    level %d: two chefs cook it in %d moves (%d positions), the first alone in %d (%d), the second alone in %d (%d)\n",
                   level + 1, both.moves, both.positions, first.moves, first.positions, second.moves, second.positions);
        }
        TIDE_CHECK(right);
    }
    tide_world_free(&world);
}
