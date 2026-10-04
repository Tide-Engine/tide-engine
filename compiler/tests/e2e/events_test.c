#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

static tide_world world;

// Entity handles are allocated as spawns are recorded. The Main scene is first;
// its Setup spawns a, b, c, a vanisher, a bomb, the player and a mirror; then,
// while those changes apply, the player's weapon and the loot it drops.
static const tide_entity A = {1, 1};
static const tide_entity C = {3, 1};
static const tide_entity PLAYER = {6, 1};
static const tide_entity WEAPON = {8, 1};
static const tide_entity PLAYER_LOOT = {9, 1};

static void ticks(int n)
{
    for (int i = 0; i < n; i++) tide_world_tick(&world);
}

TIDE_TEST(events_sent_in_main_are_handled_as_it_applies)
{
    tide_world_init(&world, 1.0f);
    // a took 1, then 2 more that the mirror passed on in a later round.
    TIDE_REQUIRE(tide_has_Health(&world, A));
    TIDE_CHECK(tide_get_Health(&world, A).value == 7);
    // a, b, the mirror, the player, and the mirror's Hit passed on to a.
    TIDE_CHECK(world.Stats.hits == 5);
    TIDE_CHECK(world.Stats.pings == 1);
    TIDE_CHECK(world.commands.count == 0 && world.commands.pages == 0);
}

TIDE_TEST(events_handlers_run_in_order)
{
    tide_world_init(&world, 1.0f);
    TIDE_CHECK(world.Stats.order == 213); // Zeroth has [Before(First)]
}

TIDE_TEST(events_spawned_and_destroyed_run_as_changes_apply)
{
    tide_world_init(&world, 1.0f);
    // The Main scene, seven from its Setup, the player's weapon, and the loot the player dropped.
    TIDE_CHECK(world.Stats.spawned == 10);
    TIDE_REQUIRE(tide_has_Weapon(&world, WEAPON));
    TIDE_CHECK(tide_entity_equal(tide_get_Weapon(&world, WEAPON).owner, PLAYER));
    // The player's Hit took all its health, so TakeHit destroyed it, and
    // DropLoot read its Target as it went.
    TIDE_CHECK(!tide_entity_alive(&world.entities, PLAYER));
    TIDE_CHECK(world.Stats.destroyed == 1);
    TIDE_REQUIRE(tide_has_Loot(&world, PLAYER_LOOT));
    TIDE_CHECK(tide_get_Loot(&world, PLAYER_LOOT).from == 7);
}

TIDE_TEST(events_follow_the_order_they_were_recorded_in)
{
    tide_world_init(&world, 1.0f);
    ticks(1);
    // The bomb's Hit came before its Destroy.
    TIDE_CHECK(tide_get_Health(&world, A).value == 3);
    // c was destroyed before its Hit's turn, so that Hit was dropped.
    TIDE_CHECK(!tide_entity_alive(&world.entities, C));
    TIDE_CHECK(world.Stats.hits == 6);
    TIDE_CHECK(world.Stats.destroyed == 2);
    TIDE_CHECK(world.Stats.spawned == 11);
}

TIDE_TEST(events_sent_to_the_world)
{
    tide_world_init(&world, 1.0f);
    ticks(2);
    TIDE_CHECK(world.Stats.rounds == 0);
    ticks(1);
    TIDE_CHECK(world.Stats.rounds == 1);
}

TIDE_TEST(events_player_joined_waits_for_the_next_tick)
{
    tide_world_init(&world, 1.0f);
    tide_world_player_joined(&world, tide_player_from_index(2));
    TIDE_CHECK(world.Stats.joined == 0);
    ticks(1);
    TIDE_CHECK(world.Stats.joined == 1);
    TIDE_CHECK(tide_player_index(world.Stats.lastJoined) == 2);
}

// A change is queued with the bytes it has, not the biggest change's: a game's
// fattest spawn would otherwise be what every event costs to record and read.
TIDE_TEST(events_are_queued_with_their_own_bytes)
{
    tide_world_init(&world, 1.0f);
    const uint32_t joined = (TIDE_COMMAND_SIZE(event_PlayerJoined) + 7u) & ~7u; // In whole eights
    TIDE_CHECK(joined < sizeof(tide_command)); // The spawns take more
    // Players joining wait in the queue for the next tick
    tide_world_player_joined(&world, tide_player_from_index(1));
    tide_world_player_joined(&world, tide_player_from_index(2));
    TIDE_REQUIRE(world.commands.count == 2 && world.commands.pages == 1);
    TIDE_CHECK(world.commands.page[0]->used == 2u * joined);
    tide_queue_place at = {0, 0};
    const tide_command *first = tide_queue_next(&world.commands, &at);
    const tide_command *second = tide_queue_next(&world.commands, &at);
    TIDE_CHECK(first->size == joined && tide_player_index(first->data.event_PlayerJoined.player) == 1);
    TIDE_CHECK(second->size == joined && tide_player_index(second->data.event_PlayerJoined.player) == 2);
    TIDE_CHECK((const uint8_t *)second - (const uint8_t *)first == (ptrdiff_t)joined);

    // They're in its snapshots and its bytes, and a world unpacked from them has them
    static tide_world copy;
    tide_world_copy(&copy, &world);
    TIDE_CHECK(copy.commands.count == 2 && tide_world_hash(&copy) == tide_world_hash(&world));
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(tide_world_pack(&world, bytes, size) == size);
    static tide_world back;
    TIDE_REQUIRE(tide_world_unpack(&back, bytes, size));
    TIDE_CHECK(back.commands.count == 2 && tide_world_hash(&back) == tide_world_hash(&world));
    // Bytes that say a change is shorter than its kind needs are refused:
    // applying it would read past it. The first one's bytes are in the
    // world's, as they are in the queue.
    uint32_t queued_at = 0;
    for (uint32_t i = 0; i + joined <= size && !queued_at; i++) {
        if (memcmp(bytes + i, first, joined) == 0) queued_at = i;
    }
    TIDE_REQUIRE(queued_at != 0);
    const uint32_t too_short = 8;
    memcpy(bytes + queued_at, &too_short, sizeof too_short);
    static tide_world refused;
    TIDE_CHECK(!tide_world_unpack(&refused, bytes, size));
    tide_world_free(&refused);
    free(bytes);
    ticks(1);
    TIDE_CHECK(world.commands.count == 0);
    tide_world_free(&copy);
    tide_world_free(&back);
}
