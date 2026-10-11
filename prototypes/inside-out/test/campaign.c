// Campaign evidence uses the exact generated game commands, rather than a
// second implementation of crate, doorway or latch rules. Every individual
// move must succeed, every final state must satisfy production IsSolved, and
// undo/reset/replay must recover the same puzzle bytes.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"
#include "test_helpers.h"
#include "solutions.h"

#define world inside_out_test_world

static bool equal_cell(const tide_int2 a, const tide_int2 b)
{
    return a.x == b.x && a.y == b.y;
}

typedef struct CampaignEvidence {
    int recursive_entries;
    int shell_parent_changes;
    int partner_latch_pushes;
    int temporary_target_unfills;
    int cargo_parent_changes;
    int max_depth;
    int shell_moves[4];
} CampaignEvidence;

static Tile fixture_tile(const LevelLayout *const layout, const int room, const tide_int2 at)
{
    const int size = room < 0 ? 7 : 5;
    if (at.x < 0 || at.y < 0 || at.x >= size || at.y >= size) return Tile_Wall;
    return room < 0 ? layout->tiles.at[at.y * size + at.x] : layout->rooms.at[room].tiles.at[at.y * size + at.x];
}

static int entry_room(const LevelLayout *const layout, const Entry *const entry)
{
    if (entry->shell >= 0) return entry->shell;
    for (int i = 0; i < layout->portalCount; i++)
        if (layout->portals.at[i].room == entry->room && equal_cell(layout->portals.at[i].position, entry->position))
            return layout->portals.at[i].destination;
    return -2;
}

static bool record_evidence(const Puzzle *const before, const int seat, const int direction, CampaignEvidence *const evidence)
{
    const Puzzle after = world.Workshop.puzzle;
    const LevelLayout *const layout = &world.Workshop.layout;
    const Traveler *const traveler = &after.actors.at[seat];
    if (traveler->depth > evidence->max_depth) evidence->max_depth = traveler->depth;
    if (traveler->depth > before->actors.at[seat].depth) {
        for (int entry = 0; entry < before->actors.at[seat].depth; entry++) {
            if (entry_room(layout, &before->actors.at[seat].trail.at[entry]) == traveler->room) {
                evidence->recursive_entries++;
                break;
            }
        }
    }
    for (int room = 0; room < layout->roomCount; room++) {
        if (before->rooms.at[room].room == after.rooms.at[room].room && equal_cell(before->rooms.at[room].position, after.rooms.at[room].position)) continue;
        evidence->shell_moves[room]++;
        if (before->rooms.at[room].room != after.rooms.at[room].room) evidence->shell_parent_changes++;
        const Traveler *const partner = &before->actors.at[1 - seat];
        if (!layout->rooms.at[room].latched || partner->room != room || fixture_tile(layout, room, partner->position) != Tile_Switch) continue;
        bool parcel_holds_switch = false;
        for (int item = 0; item < layout->crateCount; item++)
            if (before->crates.at[item].active && before->crates.at[item].room == room
                && fixture_tile(layout, room, before->crates.at[item].position) == Tile_Switch) parcel_holds_switch = true;
        if (parcel_holds_switch) continue;
        evidence->partner_latch_pushes++;

        // A counter alone could count a decorative holding job. Re-run the
        // same real command with its sole holder removed. A blocked push can
        // enter its doorway, but the shell must not be moved by that fallback.
        const int history_count = world.Workshop.historyCount;
        if (history_count < 0 || history_count >= 128) {
            fprintf(stderr, "Campaign latch probes need a spare undo slot.\n");
            return false;
        }
        world.Workshop.puzzle = *before;
        world.Workshop.puzzle.actors.at[1 - seat].room = -1;
        world.Workshop.puzzle.actors.at[1 - seat].position = tide_i2(0, 0);
        const Puzzle without_holder = world.Workshop.puzzle;
        const Puzzle spare_history = world.Workshop.history.at[history_count];
        const bool accepted = inside_out_test_act(TIDE_COMMAND_ID_Move, seat, direction);
        const Puzzle fallback = world.Workshop.puzzle;
        const Traveler *const original_actor = &without_holder.actors.at[seat];
        const Traveler *const fallback_actor = &fallback.actors.at[seat];
        const bool unchanged = memcmp(&without_holder, &fallback, sizeof fallback) == 0
            && world.Workshop.historyCount == history_count;
        const bool entered = fallback_actor->room == room && fallback_actor->depth == original_actor->depth + 1
            && fallback_actor->trail.at[original_actor->depth].shell == room
            && equal_cell(fallback_actor->position, direction == 0 ? tide_i2(2, 4) : direction == 1 ? tide_i2(0, 2)
                : direction == 2 ? tide_i2(2, 0) : tide_i2(4, 2))
            && fallback.moves == without_holder.moves + 1 && world.Workshop.historyCount == history_count + 1;
        const bool shells_unchanged = memcmp(&without_holder.rooms, &fallback.rooms, sizeof fallback.rooms) == 0;
        const bool partner_unchanged = memcmp(&without_holder.actors.at[1 - seat], &fallback.actors.at[1 - seat], sizeof(Traveler)) == 0;
        bool cargo_valid = true;
        int shifted_cargo = 0;
        const tide_int2 delta = direction == 0 ? tide_i2(0, -1) : direction == 1 ? tide_i2(1, 0)
            : direction == 2 ? tide_i2(0, 1) : tide_i2(-1, 0);
        for (int item = 0; item < layout->crateCount; item++) {
            const Cargo *const old_item = &without_holder.crates.at[item];
            const Cargo *const new_item = &fallback.crates.at[item];
            if (memcmp(old_item, new_item, sizeof(Cargo)) == 0) continue;
            shifted_cargo++;
            // A legitimate entry can push its one doorway parcel inward.
            // This observes that actual command, rather than reimplementing
            // gateway resolution or substituting another movement engine.
            cargo_valid = cargo_valid && entered && old_item->active == new_item->active
                && old_item->room == room && new_item->room == room
                && equal_cell(old_item->position, fallback_actor->position)
                && equal_cell(new_item->position, tide_i2(old_item->position.x + delta.x, old_item->position.y + delta.y));
        }
        world.Workshop.puzzle = after;
        world.Workshop.historyCount = history_count;
        world.Workshop.history.at[history_count] = spare_history;
        if (!accepted || !shells_unchanged || !partner_unchanged || !cargo_valid || shifted_cargo > 1 || (!unchanged && !entered)) {
            fprintf(stderr, "A counted latch push did not remain a blocked shell move or valid doorway fallback without its partner.\n");
            return false;
        }
    }
    for (int item = 0; item < layout->crateCount; item++) {
        if (!before->crates.at[item].active) continue;
        if (before->crates.at[item].room == after.crates.at[item].room && equal_cell(before->crates.at[item].position, after.crates.at[item].position)) continue;
        if (before->crates.at[item].room != after.crates.at[item].room) evidence->cargo_parent_changes++;
        if (fixture_tile(layout, before->crates.at[item].room, before->crates.at[item].position) == Tile_Target) evidence->temporary_target_unfills++;
    }
    return true;
}

static bool play_witness(const int level, const char *const witness, int *const actions, CampaignEvidence *const evidence)
{
    int used = 0;
    unsigned seats = 0;
    unsigned dimensions = 0;
    const char *const upper_keys = "NESW";
    const char *const lower_keys = "nesw";
    for (const char *action = witness; *action; action++) {
        const char *const upper = strchr(upper_keys, *action);
        const char *const lower = strchr(lower_keys, *action);
        if (!upper && !lower) continue;
        const int seat = upper ? 0 : 1;
        const int direction = upper ? (int)(upper - upper_keys) : (int)(lower - lower_keys);
        const Puzzle before = world.Workshop.puzzle;
        if (!inside_out_test_act(TIDE_COMMAND_ID_Move, seat, direction) || world.Workshop.puzzle.moves != before.moves + 1) {
            fprintf(stderr, "Level %d witness action %d (%c) blocked: actor room %d (%d,%d), depth %d.\n",
                level + 1, used + 1, *action, before.actors.at[seat].room,
                before.actors.at[seat].position.x, before.actors.at[seat].position.y, before.actors.at[seat].depth);
            for (int other = 0; other < 2; other++) fprintf(stderr, "  actor%d room%d (%d,%d) depth%d\n", other,
                before.actors.at[other].room, before.actors.at[other].position.x, before.actors.at[other].position.y, before.actors.at[other].depth);
            for (int room = 0; room < world.Workshop.layout.roomCount; room++) fprintf(stderr, "  shell%d parent%d (%d,%d)\n", room,
                before.rooms.at[room].room, before.rooms.at[room].position.x, before.rooms.at[room].position.y);
            for (int item = 0; item < world.Workshop.layout.crateCount; item++) fprintf(stderr, "  cargo%d room%d (%d,%d)\n", item,
                before.crates.at[item].room, before.crates.at[item].position.x, before.crates.at[item].position.y);
            return false;
        }
        seats |= 1u << seat;
        dimensions |= before.actors.at[seat].room < 0 ? 1 : 2;
        if (evidence && !record_evidence(&before, seat, direction, evidence)) return false;
        used++;
    }
    if (actions) *actions = used;
    return world.Workshop.puzzle.solved && seats == 3 && dimensions == 3;
}

TIDE_TEST(inside_out_every_campaign_puzzle_has_a_production_rules_solution)
{
    // Minimum evidence tags deliberately describe actual replayed dependencies,
    // not optimality, human difficulty or the absence of alternative solutions.
    static const CampaignEvidence minimum[12] = {
        {0}, {.partner_latch_pushes = 2}, {.partner_latch_pushes = 2, .max_depth = 2},
        {.partner_latch_pushes = 4, .cargo_parent_changes = 1},
        {.recursive_entries = 1, .partner_latch_pushes = 1, .max_depth = 2},
        {.partner_latch_pushes = 1, .temporary_target_unfills = 1, .cargo_parent_changes = 1, .max_depth = 2},
        {.shell_parent_changes = 1, .partner_latch_pushes = 2, .cargo_parent_changes = 1, .max_depth = 2},
        {.recursive_entries = 1, .partner_latch_pushes = 1, .max_depth = 3},
        {.shell_parent_changes = 1, .partner_latch_pushes = 2, .temporary_target_unfills = 1, .cargo_parent_changes = 1, .max_depth = 2},
        {.shell_parent_changes = 2, .partner_latch_pushes = 2, .temporary_target_unfills = 1, .cargo_parent_changes = 1},
        {.recursive_entries = 1, .max_depth = 3},
        {.recursive_entries = 1, .shell_parent_changes = 1, .partner_latch_pushes = 2, .temporary_target_unfills = 1, .cargo_parent_changes = 2, .max_depth = 3}
    };
    for (int level = 0; level < 12; level++) {
        inside_out_test_start(2);
        TIDE_REQUIRE(inside_out_test_act(TIDE_COMMAND_ID_ChooseLevel, 0, level));
        TIDE_CHECK(world.Workshop.level == level);
        const Puzzle initial = world.Workshop.puzzle;
        const LevelLayout layout = world.Workshop.layout;

        TIDE_CHECK(initial.actors.at[0].room == -1 && initial.actors.at[1].room == -1);
        TIDE_CHECK(!initial.solved);
        TIDE_CHECK(layout.roomCount >= 1 && layout.roomCount <= 4);
        TIDE_CHECK(layout.crateCount <= 8);
        for (int room = layout.roomCount; room < 4; room++) TIDE_CHECK(layout.rooms.at[room].dock.x == -1);

        int actions = 0;
        CampaignEvidence evidence = {0};
        TIDE_REQUIRE(play_witness(level, inside_out_solutions[level], &actions, &evidence));
        TIDE_CHECK(evidence.recursive_entries >= minimum[level].recursive_entries);
        TIDE_CHECK(evidence.shell_parent_changes >= minimum[level].shell_parent_changes);
        TIDE_CHECK(evidence.partner_latch_pushes >= minimum[level].partner_latch_pushes);
        TIDE_CHECK(evidence.temporary_target_unfills >= minimum[level].temporary_target_unfills);
        TIDE_CHECK(evidence.cargo_parent_changes >= minimum[level].cargo_parent_changes);
        TIDE_CHECK(evidence.max_depth >= minimum[level].max_depth);
        if (level == 2 || level == 3) TIDE_CHECK(evidence.shell_moves[0] >= 2 || evidence.shell_moves[1] >= 2);
        for (int seat = 0; seat < 2; seat++)
            TIDE_CHECK(world.Workshop.puzzle.actors.at[seat].room != initial.actors.at[seat].room
                || !equal_cell(world.Workshop.puzzle.actors.at[seat].position, initial.actors.at[seat].position));
        const Puzzle solved = world.Workshop.puzzle;

        // A solved state is reviewable and reversible; no automatic level
        // advance steals the chance to inspect it or undo the final step.
        TIDE_REQUIRE(inside_out_test_act(TIDE_COMMAND_ID_Undo, 1, 0));
        TIDE_CHECK(!world.Workshop.puzzle.solved);
        TIDE_CHECK(world.Workshop.puzzle.moves == actions - 1);
        TIDE_REQUIRE(inside_out_test_act(TIDE_COMMAND_ID_Reset, 0, 0));
        TIDE_CHECK(memcmp(&initial, &world.Workshop.puzzle, sizeof initial) == 0);
        TIDE_REQUIRE(play_witness(level, inside_out_solutions[level], NULL, NULL));
        TIDE_CHECK(memcmp(&solved, &world.Workshop.puzzle, sizeof solved) == 0);
        printf("  Workshop %2d: %2d moves; recursive entries %d, shell parent changes %d, partner latch pushes %d, target unfills %d, cargo transfers %d, depth %d.\n",
            level + 1, actions, evidence.recursive_entries, evidence.shell_parent_changes, evidence.partner_latch_pushes,
            evidence.temporary_target_unfills, evidence.cargo_parent_changes, evidence.max_depth);
        tide_world_free(&world);
    }
}

TIDE_TEST(inside_out_campaign_players_can_exchange_their_jobs)
{
    // Independent avatars can take the other player's entire job. There are
    // no fixed large/small roles or a special ownership exchange operation.
    inside_out_test_start(2);
    TIDE_REQUIRE(inside_out_test_act(TIDE_COMMAND_ID_ChooseLevel, 0, 5));
    const Traveler first = world.Workshop.puzzle.actors.at[0];
    world.Workshop.puzzle.actors.at[0] = world.Workshop.puzzle.actors.at[1];
    world.Workshop.puzzle.actors.at[1] = first;
    char swapped[256];
    const size_t length = strlen(inside_out_solutions[5]);
    TIDE_REQUIRE(length < sizeof swapped);
    for (size_t i = 0; i <= length; i++) {
        const char mark = inside_out_solutions[5][i];
        swapped[i] = mark >= 'A' && mark <= 'Z' ? (char)(mark + ('a' - 'A'))
            : mark >= 'a' && mark <= 'z' ? (char)(mark - ('a' - 'A')) : mark;
    }
    TIDE_REQUIRE(play_witness(5, swapped, NULL, NULL));
    tide_world_free(&world);
}
