#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

static tide_world world, kept, replay;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_entity mine, theirs, nobodys; // Player 0's, player 1's, no player's

typedef struct sent {
    uint8_t bytes[TIDE_COMMAND_BYTES];
    uint32_t size;
} sent;

static void start(void)
{
    tide_world_init(&world, 1.0f / 60.0f);
    mine = tide_entity_in_slot(&world.entities, 1);
    theirs = tide_entity_in_slot(&world.entities, 2);
    nobodys = tide_entity_in_slot(&world.entities, 3);
    TIDE_REQUIRE(tide_get_Owner(&world, mine).player.id == tide_player_from_index(0).id);
    TIDE_REQUIRE(tide_get_Owner(&world, theirs).player.id == tide_player_from_index(1).id);
    TIDE_REQUIRE(!tide_has_Owner(&world, nobodys));
    tide_local_init(&local);
    tide_local_set_session(&local, TIDE_SESSION_CONNECTED, tide_player_from_index(0), 0, false, false, "");
}

// A view's command for `target`, as the session would take it: its bytes, whoever then sends them
static void command(sent *out, const int which, const tide_entity target)
{
    local.UI.which = which;
    local.UI.target = target;
    local.UI.send = true;
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    out->size = 0;
    const uint8_t *bytes = tide_local_command(&local, 0, &out->size);
    TIDE_REQUIRE(bytes && out->size > 0 && out->size <= TIDE_COMMAND_BYTES);
    memcpy(out->bytes, bytes, out->size);
    tide_local_command_sent(&local, 0);
}

static void from(const int player, const uint32_t sequence, const sent *c)
{
    TIDE_REQUIRE(tide_game_api.command(&world, tide_player_from_index(player), 0, sequence, c->bytes, c->size));
}

TIDE_TEST(command_with_ownership_runs_only_for_the_senders_entity)
{
    start();
    sent poke_mine, poke_theirs, poke_nobodys;
    command(&poke_mine, 0, mine);
    command(&poke_theirs, 0, theirs);
    command(&poke_nobodys, 0, nobodys);
    from(0, 1, &poke_mine);    // Its own: runs
    from(1, 1, &poke_mine);    // Another player's entity: dropped
    from(1, 2, &poke_theirs);  // Player 1's own: runs
    from(0, 2, &poke_nobodys); // Nobody's: dropped
    tide_world_tick(&world);
    TIDE_CHECK(tide_get_Counter(&world, mine).value == 5 && tide_get_Counter(&world, mine).pokes == 1);
    TIDE_CHECK(tide_get_Counter(&world, theirs).value == 5 && tide_get_Counter(&world, theirs).pokes == 1);
    TIDE_CHECK(tide_get_Counter(&world, nobodys).value == 0 && tide_get_Counter(&world, nobodys).pokes == 0);
    // Dropped as a guard drops one: it was handled, and sending it again is a resend
    TIDE_CHECK(tide_game_api.command_sequence(&world, tide_player_from_index(1), 0) == 2);
    TIDE_CHECK(!tide_game_api.command(&world, tide_player_from_index(1), 0, 1, poke_mine.bytes, poke_mine.size));
    tide_world_free(&world);
    tide_local_free(&local);
}

TIDE_TEST(command_without_ownership_runs_for_everyone_elses_and_names_its_sender)
{
    start();
    sent steal_mine, steal_theirs, steal_nobodys;
    command(&steal_mine, 1, mine);
    command(&steal_theirs, 1, theirs);
    command(&steal_nobodys, 1, nobodys);
    from(0, 1, &steal_mine);    // Its own: dropped
    from(0, 2, &steal_theirs);  // Player 1's: runs
    from(1, 1, &steal_nobodys); // Nobody's: runs
    from(1, 2, &steal_theirs);  // Player 1's own: dropped
    tide_world_copy(&kept, &world);
    tide_world_tick(&world);
    TIDE_CHECK(tide_get_Counter(&world, mine).value == 0);
    TIDE_CHECK(tide_get_Counter(&world, theirs).value == -2);
    TIDE_CHECK(tide_get_Counter(&world, nobodys).value == -2);
    TIDE_CHECK(world.Log.stolen == 4 && world.Log.seen == 2);
    TIDE_CHECK(world.Log.last.id == tide_player_from_index(1).id); // The last that ran was player 1's
    // The same commands again, from the copy taken before the tick: the same world
    tide_world_copy(&replay, &kept);
    tide_world_tick(&replay);
    TIDE_CHECK(tide_world_hash(&replay) == tide_world_hash(&world));
    tide_world_free(&world);
    tide_world_free(&kept);
    tide_world_free(&replay);
    tide_local_free(&local);
}

// `Poke(...)` in a view that runs for an entity, with no entity named: the command is for that one
TIDE_TEST(command_called_with_no_entity_is_for_the_entity_the_code_runs_for)
{
    start();
    local.UI.own = true;
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    sent own = {0};
    const uint8_t *bytes = tide_local_command(&local, 0, &own.size);
    TIDE_REQUIRE(bytes && own.size > 0 && own.size <= TIDE_COMMAND_BYTES);
    memcpy(own.bytes, bytes, own.size);
    tide_local_command_sent(&local, 0);
    TIDE_CHECK(tide_local_command(&local, 0, &(uint32_t){0}) == NULL); // One call, of the one entity player 0 owns
    from(0, 1, &own);
    tide_world_tick(&world);
    TIDE_CHECK(tide_get_Counter(&world, mine).value == 9 && tide_get_Counter(&world, mine).pokes == 1);
    TIDE_CHECK(tide_get_Counter(&world, theirs).value == 0 && tide_get_Counter(&world, nobodys).value == 0);
    tide_world_free(&world);
    tide_local_free(&local);
}

// A system calls a command as a view does. Nothing is sent: it's queued, and runs at the end of the tick for
// every entity it was called for, whoever owns it, with no player for its sender
TIDE_TEST(command_called_by_the_match_runs_with_no_player_and_no_network)
{
    start();
    world.Log.tithe = true;
    tide_world_copy(&kept, &world);
    tide_world_tick(&world);
    TIDE_CHECK(tide_get_Counter(&world, mine).value == 1 && tide_get_Counter(&world, mine).pokes == 1);
    TIDE_CHECK(tide_get_Counter(&world, theirs).value == 1 && tide_get_Counter(&world, theirs).pokes == 1);
    TIDE_CHECK(tide_get_Counter(&world, nobodys).value == 1 && tide_get_Counter(&world, nobodys).pokes == 1);
    TIDE_CHECK(!world.Log.tithe);
    // No player's sequence moved: no player sent anything
    TIDE_CHECK(tide_game_api.command_sequence(&world, tide_player_from_index(0), 0) == 0);
    tide_world_tick(&world); // Once: the tick after calls nothing
    TIDE_CHECK(tide_get_Counter(&world, mine).pokes == 1);
    world.Log.levy = true;
    world.Log.last = tide_player_from_index(1);
    tide_world_tick(&world);
    TIDE_CHECK(tide_get_Counter(&world, mine).value == -2 && tide_get_Counter(&world, theirs).value == -2);
    TIDE_CHECK(tide_get_Counter(&world, nobodys).value == -2);
    TIDE_CHECK(world.Log.stolen == 9 && world.Log.seen == 3 && world.Log.last.id == 0);
    // The same ticks again from a copy: the same world
    tide_world_copy(&replay, &kept);
    tide_world_tick(&replay);
    tide_world_tick(&replay);
    replay.Log.levy = true;
    replay.Log.last = tide_player_from_index(1);
    tide_world_tick(&replay);
    TIDE_CHECK(tide_world_hash(&replay) == tide_world_hash(&world));
    tide_world_free(&world);
    tide_world_free(&kept);
    tide_world_free(&replay);
    tide_local_free(&local);
}
