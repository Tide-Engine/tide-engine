// Witnesses exercise generated command handlers; geometry is read only for
// evidence, and no second movement implementation participates in replay.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "tide_test.h"
#include "test_helpers.h"
#include "solutions.h"
#define world inside_out_test_world
typedef struct CampaignEvidence {
    int recursive_entries, box_parent_changes, occupied_box_moves;
    int reference_moves, self_nests, target_unfills, cargo_transfers, max_depth;
    int partner_backstops;
    int primary_self_nests, copied_self_nests, self_extractions;
    int cross_cycle_formations, cross_cycle_extractions;
} CampaignEvidence;
static bool same_cell(tide_int2 a, tide_int2 b) { return a.x == b.x && a.y == b.y; }
static int destination(const LevelLayout *layout, int box)
{
    return box < 4 ? box : layout->references.at[box - 4].destination;
}
static bool in_primary_cycle(const LevelLayout *layout, const Puzzle *puzzle, int room)
{
    int parent = puzzle->rooms.at[room].room;
    for (int depth = 0; depth < 8 && parent >= 0 && parent < layout->roomCount; depth++) {
        if (parent == room) return true;
        parent = puzzle->rooms.at[parent].room;
    }
    return false;
}
static Tile fixture_tile(const LevelLayout *layout, int room, tide_int2 at)
{
    const int width = room < 0 ? layout->width : layout->rooms.at[room].width;
    const int height = room < 0 ? layout->height : layout->rooms.at[room].height;
    if (at.x < 0 || at.y < 0 || at.x >= width || at.y >= height) return Tile_Wall;
    return room < 0 ? layout->tiles.at[at.y * width + at.x] : layout->rooms.at[room].tiles.at[at.y * width + at.x];
}
static bool record_evidence(const Puzzle *before, int seat, int direction, CampaignEvidence *evidence)
{
    const Puzzle *after = &world.Workshop.puzzle;
    const LevelLayout *layout = &world.Workshop.layout;
    const Traveler *traveler = &after->actors.at[seat];
    if (traveler->depth > evidence->max_depth) evidence->max_depth = traveler->depth;
    if (traveler->depth > before->actors.at[seat].depth) {
        bool recursive = before->actors.at[seat].room == traveler->room;
        for (int entry = 0; entry < before->actors.at[seat].depth; entry++)
            if (destination(layout, before->actors.at[seat].trail.at[entry].box) == traveler->room) recursive = true;
        if (recursive) evidence->recursive_entries++;
    }
    // Identify direct box entries, then try the same production command
    // with the partner absent. A changed box placement proves that the
    // partner's body selected doorway entry over the automatic push.
    const Traveler original_actor = before->actors.at[seat];
    const tide_int2 delta = direction == 0 ? tide_i2(0, -1) : direction == 1 ? tide_i2(1, 0)
        : direction == 2 ? tide_i2(0, 1) : tide_i2(-1, 0);
    if (traveler->depth > original_actor.depth) {
        const int entered = traveler->trail.at[traveler->depth - 1].box;
        const RoomShell placement = entered < 4 ? before->rooms.at[entered] : before->references.at[entered - 4];
        if (placement.room == original_actor.room && same_cell(placement.position,
            tide_i2(original_actor.position.x + delta.x, original_actor.position.y + delta.y))) {
            const Puzzle accepted = *after;
            const int history_count = world.Workshop.historyCount;
            if (history_count >= 128) return false;
            const Puzzle spare = world.Workshop.history.at[history_count];
            world.Workshop.puzzle = *before;
            world.Workshop.puzzle.actors.at[1 - seat].room = -1;
            world.Workshop.puzzle.actors.at[1 - seat].position = tide_i2(0, 0);
            if (!inside_out_test_act(TIDE_COMMAND_ID_Move, seat, direction)) return false;
            const RoomShell fallback = entered < 4 ? world.Workshop.puzzle.rooms.at[entered] : world.Workshop.puzzle.references.at[entered - 4];
            if (fallback.room != placement.room || !same_cell(fallback.position, placement.position)) evidence->partner_backstops++;
            world.Workshop.puzzle = accepted;
            world.Workshop.historyCount = history_count;
            world.Workshop.history.at[history_count] = spare;
        }
    }
    for (int box = 0; box < 4 + layout->referenceCount; box++) {
        if (box < 4 && box >= layout->roomCount) continue;
        const RoomShell old_box = box < 4 ? before->rooms.at[box] : before->references.at[box - 4];
        const RoomShell new_box = box < 4 ? after->rooms.at[box] : after->references.at[box - 4];
        if (old_box.room == new_box.room && same_cell(old_box.position, new_box.position)) continue;
        if (old_box.room != new_box.room) evidence->box_parent_changes++;
        if (box >= 4) evidence->reference_moves++;
        const int room = destination(layout, box);
        if (new_box.room == room && old_box.room != room) {
            evidence->self_nests++;
            if (box < 4) evidence->primary_self_nests++;
            else evidence->copied_self_nests++;
        }
        if (old_box.room == room && new_box.room != room) evidence->self_extractions++;
        if (box < 4 && old_box.room != new_box.room) {
            if (new_box.room != room && !in_primary_cycle(layout, before, room) && in_primary_cycle(layout, after, room))
                evidence->cross_cycle_formations++;
            if (old_box.room != room && in_primary_cycle(layout, before, room) && !in_primary_cycle(layout, after, room))
                evidence->cross_cycle_extractions++;
        }
        const Traveler *partner = &before->actors.at[1 - seat];
        int containing = partner->room;
        for (int depth = 0; depth < 8 && containing >= 0; depth++) {
            if (containing == room) {
                evidence->occupied_box_moves++;
                break;
            }
            containing = before->rooms.at[containing].room;
        }
    }
    for (int item = 0; item < layout->crateCount; item++) {
        const Cargo old_item = before->crates.at[item], new_item = after->crates.at[item];
        if (old_item.room == new_item.room && same_cell(old_item.position, new_item.position)) continue;
        if (old_item.room != new_item.room) evidence->cargo_transfers++;
        if (fixture_tile(layout, old_item.room, old_item.position) == Tile_Target) evidence->target_unfills++;
    }
    return true;
}
static bool play_witness(int level, const char *witness, int *actions, CampaignEvidence *evidence)
{
    int count = 0;
    unsigned seats = 0;
    const char *const upper_keys = "NESW", *const lower_keys = "nesw";
    for (const char *action = witness; *action; action++) {
        const char *upper = strchr(upper_keys, *action), *lower = strchr(lower_keys, *action);
        if (!upper && !lower) continue;
        const int seat = upper ? 0 : 1;
        const int direction = upper ? (int)(upper - upper_keys) : (int)(lower - lower_keys);
        const Puzzle before = world.Workshop.puzzle;
        if (!inside_out_test_act(TIDE_COMMAND_ID_Move, seat, direction) || world.Workshop.puzzle.moves != before.moves + 1) {
            fprintf(stderr, "Level %d action %d (%c) blocked: actor r%d(%d,%d) depth%d.\n", level + 1, count + 1, *action,
                before.actors.at[seat].room, before.actors.at[seat].position.x, before.actors.at[seat].position.y, before.actors.at[seat].depth);
            return false;
        }
        seats |= 1u << seat;
        count++;
        if (evidence && !record_evidence(&before, seat, direction, evidence)) return false;
    }
    if (actions) *actions = count;
    return world.Workshop.puzzle.solved && seats == 3;
}
TIDE_TEST(inside_out_every_campaign_puzzle_has_a_production_rules_solution)
{
    static const CampaignEvidence minimum[12] = {
        {.cargo_transfers = 2}, {.occupied_box_moves = 1, .cargo_transfers = 1},
        {.occupied_box_moves = 1, .cargo_transfers = 2}, {.target_unfills = 1, .cargo_transfers = 1},
        {.cargo_transfers = 1, .partner_backstops = 1}, {.reference_moves = 1, .recursive_entries = 1, .max_depth = 3},
        {.occupied_box_moves = 1, .box_parent_changes = 1, .cargo_transfers = 1},
        {.target_unfills = 1, .box_parent_changes = 1, .cargo_transfers = 1},
        {.recursive_entries = 1, .reference_moves = 2, .max_depth = 4},
        {.reference_moves = 3, .copied_self_nests = 1, .self_extractions = 1, .target_unfills = 1, .cargo_transfers = 1},
        {.primary_self_nests = 1, .self_extractions = 1, .target_unfills = 1},
        {.cross_cycle_formations = 1, .cross_cycle_extractions = 1, .cargo_transfers = 2}
    };
    for (int level = 0; level < 12; level++) {
        inside_out_test_start(2);
        TIDE_REQUIRE(inside_out_test_act(TIDE_COMMAND_ID_ChooseLevel, 0, level));
        const Puzzle initial = world.Workshop.puzzle;
        const LevelLayout layout = world.Workshop.layout;
        TIDE_CHECK(initial.actors.at[0].room >= 0 && initial.actors.at[1].room >= 0);
        TIDE_CHECK(initial.actors.at[0].depth >= 2 && initial.actors.at[1].depth >= 2);
        TIDE_CHECK(!initial.solved);
        TIDE_CHECK(layout.roomCount >= 2 && layout.roomCount <= 4 && layout.crateCount <= 8);
        for (int room = 0; room < layout.roomCount; room++) {
            TIDE_CHECK(layout.rooms.at[room].width >= 3 && layout.rooms.at[room].width <= 7);
            TIDE_CHECK(layout.rooms.at[room].height >= 3 && layout.rooms.at[room].height <= 7);
        }
        for (int room = layout.roomCount; room < 4; room++) TIDE_CHECK(layout.rooms.at[room].dock.x == -1);
        int actions = 0;
        CampaignEvidence evidence = {0};
        TIDE_REQUIRE(play_witness(level, inside_out_solutions[level], &actions, &evidence));
        TIDE_CHECK(evidence.recursive_entries >= minimum[level].recursive_entries);
        TIDE_CHECK(evidence.box_parent_changes >= minimum[level].box_parent_changes);
        TIDE_CHECK(evidence.occupied_box_moves >= minimum[level].occupied_box_moves);
        TIDE_CHECK(evidence.reference_moves >= minimum[level].reference_moves);
        TIDE_CHECK(evidence.self_nests >= minimum[level].self_nests);
        TIDE_CHECK(evidence.target_unfills >= minimum[level].target_unfills);
        TIDE_CHECK(evidence.cargo_transfers >= minimum[level].cargo_transfers);
        TIDE_CHECK(evidence.max_depth >= minimum[level].max_depth);
        TIDE_CHECK(evidence.partner_backstops >= minimum[level].partner_backstops);
        TIDE_CHECK(evidence.primary_self_nests >= minimum[level].primary_self_nests);
        TIDE_CHECK(evidence.copied_self_nests >= minimum[level].copied_self_nests);
        TIDE_CHECK(evidence.self_extractions >= minimum[level].self_extractions);
        TIDE_CHECK(evidence.cross_cycle_formations >= minimum[level].cross_cycle_formations);
        TIDE_CHECK(evidence.cross_cycle_extractions >= minimum[level].cross_cycle_extractions);
        if (level == 9 || level == 10) TIDE_CHECK(evidence.self_nests == 1 && evidence.self_extractions == 1);
        if (level == 11) TIDE_CHECK(evidence.cross_cycle_formations == 1 && evidence.cross_cycle_extractions == 1);
        const Puzzle solved = world.Workshop.puzzle;
        for (int seat = 0; seat < 2; seat++) TIDE_CHECK(solved.actors.at[seat].depth < initial.actors.at[seat].depth);
        TIDE_REQUIRE(inside_out_test_act(TIDE_COMMAND_ID_Undo, 1, 0));
        TIDE_CHECK(!world.Workshop.puzzle.solved && world.Workshop.puzzle.moves == actions - 1);
        TIDE_REQUIRE(inside_out_test_act(TIDE_COMMAND_ID_Reset, 0, 0));
        TIDE_CHECK(memcmp(&initial, &world.Workshop.puzzle, sizeof initial) == 0);
        TIDE_REQUIRE(play_witness(level, inside_out_solutions[level], NULL, NULL));
        TIDE_CHECK(memcmp(&solved, &world.Workshop.puzzle, sizeof solved) == 0);
        printf("  Outward %2d: %2d moves; recursive %d, parent changes %d, occupied moves %d, reference moves %d, self nests %d, unfills %d, cargo transfers %d, depth %d, partner backstops %d.\n",
            level + 1, actions, evidence.recursive_entries, evidence.box_parent_changes, evidence.occupied_box_moves,
            evidence.reference_moves, evidence.self_nests, evidence.target_unfills, evidence.cargo_transfers, evidence.max_depth, evidence.partner_backstops);
        if (level >= 9) printf("    primary self %d, copied self %d, self extractions %d, cross-cycle formation/extraction %d/%d.\n",
            evidence.primary_self_nests, evidence.copied_self_nests, evidence.self_extractions,
            evidence.cross_cycle_formations, evidence.cross_cycle_extractions);
        tide_world_free(&world);
    }
}
TIDE_TEST(inside_out_campaign_players_can_exchange_their_jobs)
{
    for (int level = 0; level < 12; level++) {
        inside_out_test_start(2);
        TIDE_REQUIRE(inside_out_test_act(TIDE_COMMAND_ID_ChooseLevel, 0, level));
        const Traveler first = world.Workshop.puzzle.actors.at[0];
        world.Workshop.puzzle.actors.at[0] = world.Workshop.puzzle.actors.at[1];
        world.Workshop.puzzle.actors.at[1] = first;
        char swapped[512];
        const size_t length = strlen(inside_out_solutions[level]);
        TIDE_REQUIRE(length < sizeof swapped);
        for (size_t i = 0; i <= length; i++) {
            const char mark = inside_out_solutions[level][i];
            swapped[i] = mark >= 'A' && mark <= 'Z' ? (char)(mark + ('a' - 'A'))
                : mark >= 'a' && mark <= 'z' ? (char)(mark - ('a' - 'A')) : mark;
        }
        TIDE_REQUIRE(play_witness(level, swapped, NULL, NULL));
        tide_world_free(&world);
    }
}

TIDE_TEST(inside_out_carried_delivery_requires_both_initial_makers)
{
    // Exhaust the tiny authored second puzzle with either maker as the only
    // acting body. Ignore maker goals completely: the question is whether
    // one body can both dock the home and deliver its parcel. Removing the
    // other body gives more standing space than merely freezing it.
    for (int seat = 0; seat < 2; seat++) {
        inside_out_test_start(2);
        TIDE_REQUIRE(inside_out_test_act(TIDE_COMMAND_ID_ChooseLevel, 0, 1));
        Puzzle *const states = calloc(512, sizeof *states);
        TIDE_REQUIRE(states != NULL);
        states[0] = world.Workshop.puzzle;
        states[0].actors.at[1 - seat].room = -1;
        states[0].actors.at[1 - seat].position = tide_i2(0, 0);
        int count = 1;
        for (int index = 0; index < count; index++) {
            const Puzzle current = states[index];
            TIDE_CHECK(!(current.rooms.at[0].room == 1 && same_cell(current.rooms.at[0].position, tide_i2(3, 2))
                && current.crates.at[0].room == 1 && same_cell(current.crates.at[0].position, tide_i2(5, 2))));
            for (int direction = 0; direction < 4; direction++) {
                world.Workshop.puzzle = current;
                world.Workshop.historyCount = 0;
                TIDE_REQUIRE(inside_out_test_act(TIDE_COMMAND_ID_Move, seat, direction));
                Puzzle next = world.Workshop.puzzle;
                if (next.moves == current.moves) {
                    TIDE_CHECK(memcmp(&current, &next, sizeof next) == 0);
                    continue;
                }
                next.moves = 0;
                next.solved = false;
                bool seen = false;
                for (int prior = 0; prior < count; prior++)
                    if (memcmp(&next, &states[prior], sizeof next) == 0) { seen = true; break; }
                if (seen) continue;
                TIDE_REQUIRE(count < 512);
                states[count++] = next;
            }
        }
        TIDE_CHECK(count == 167);
        printf("  Carried delivery seat%d alone: exhausted %d reachable production-command states; no parcel+dock solution.\n", seat, count);
        free(states);
        tide_world_free(&world);
    }
}
