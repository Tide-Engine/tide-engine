#include "game.h"
#include "tide_test.h"

static tide_world world;
static tide_local local;

// Main's Setup loads an arena and a lobby and spawns a counter of Main's own; the arena's spawns two counters and
// the lobby's one. Counters are found by trying the first handles, as each is a first generation's.
typedef struct counted {
    int arena, other;           // Counters by whether Born marked them
    int lobby_poked, arena_waited, wrong; // Poked's and Waited's counters, and any that ran for the wrong one
    bool arena_ok, other_ok;    // ...and whether each has what its scene's systems did
} counted;

static counted count_counters(const int ticks)
{
    counted c = {0, 0, 0, 0, 0, true, true};
    for (uint32_t i = 0; i < 16; i++) {
        const tide_entity e = {i, 1};
        if (!tide_entity_alive(&world.entities, e) || !tide_has_Counter(&world, e)) continue;
        const Counter counter = tide_get_Counter(&world, e);
        if (counter.poked) c.lobby_poked++;
        if (counter.waited) c.arena_waited++;
        if ((counter.poked && counter.born) || (counter.waited && !counter.born)) c.wrong++;
        if (counter.born) {
            c.arena++;
            c.arena_ok &= counter.ticks == ticks && counter.outside == 0;
        } else {
            c.other++;
            c.other_ok &= counter.ticks == 0 && counter.outside == ticks;
        }
    }
    return c;
}

// Systems run for the entities their scene filters take: InArena for the arena's counters, NotArena for the others
// (Main's and the lobby's), EveryArena once a tick for each of the arena's entities (its own too); Born ran for the
// arena's counters alone.
TIDE_TEST(scene_filters_systems_and_handlers)
{
    tide_world_init(&world, 1.0f);
    for (int i = 0; i < 3; i++) tide_world_tick(&world);
    const counted c = count_counters(3);
    TIDE_CHECK(c.arena == 2);
    TIDE_CHECK(c.other == 2);
    TIDE_CHECK(c.arena_ok);
    TIDE_CHECK(c.other_ok);
    TIDE_CHECK(world.Probe.everyArena == 3 * 3); // Its two counters, and the arena's own entity
    TIDE_CHECK(c.lobby_poked == 1);  // Poked ran for the lobby's counter alone
    TIDE_CHECK(c.arena_waited == 2); // Waited (async) for the arena's two
    TIDE_CHECK(c.wrong == 0);
}

// Views too: ShowLobby sees the lobby's counter alone, ShowNotArena every counter no arena owns.
TIDE_TEST(scene_filters_views)
{
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    tide_draw_list draw = {0};
    tide_gui gui = {0};
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    TIDE_CHECK(local.Seen.lobby == 1);    // Its counter's ticks (0), plus one
    TIDE_CHECK(local.Seen.notArena == 2); // Main's counter and the lobby's
}
