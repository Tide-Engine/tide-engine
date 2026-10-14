#include <inttypes.h>
#include <stdio.h>

#include "game.h"
#include "tide_test.h"

// Random numbers whose state is the world's (tide/random.h): what the engine
// seeds and from what, what a game seeds itself, what a snapshot keeps, and
// the bits, which every platform has to agree on.

static tide_world world;
static tide_world other;
static tide_world snapshot;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

#define DT (1.0f / 60.0f)

// The entities with a Roller, in the order they were spawned.
static int rollers(const tide_world *w, tide_entity *out, const int room)
{
    int count = 0;
    for (uint32_t i = 0; i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        if (tide_has_Roller(w, e) && count < room) out[count++] = e;
    }
    return count;
}

static uint64_t hash_after(tide_world *w, const int ticks)
{
    for (int i = 0; i < ticks; i++) tide_world_tick(w);
    return tide_world_hash(w);
}

// Started plainly, a match's seed is 0: the same match every time.
TIDE_TEST(random_a_plain_start_repeats)
{
    tide_world_init(&world, DT);
    TIDE_CHECK(world.Time.seed == 0 && world.Deck.seed == 0);
    const uint64_t first = hash_after(&world, 60);
    tide_world_init(&world, DT);
    TIDE_CHECK(hash_after(&world, 60) == first);
    tide_world_start(&other, DT, NULL);
    TIDE_CHECK(hash_after(&other, 60) == first);
    tide_world_start_seeded(&other, DT, NULL, 0u);
    TIDE_CHECK(hash_after(&other, 60) == first);
}

// The seed is the match's: code reads it, and everything the engine seeds
// comes from it, so another seed is another match and the same one the same.
TIDE_TEST(random_the_seed_makes_the_match)
{
    tide_world_init(&world, DT);
    const uint64_t plain = hash_after(&world, 60);
    tide_world_start_seeded(&world, DT, NULL, 12345u);
    TIDE_CHECK(world.Time.seed == 12345 && world.Deck.seed == 12345);
    const uint64_t seeded = hash_after(&world, 60);
    TIDE_CHECK(seeded != plain);
    tide_world_start_seeded(&other, DT, NULL, 12345u);
    TIDE_CHECK(hash_after(&other, 60) == seeded);
    tide_world_start_seeded(&other, DT, NULL, 12346u);
    TIDE_CHECK(hash_after(&other, 60) != seeded);
    // 31 bits, so % of it never goes negative
    tide_world_start_seeded(&other, DT, NULL, 0x80000001u);
    TIDE_CHECK(other.Time.seed == 1);
    tide_world_start_seeded(&other, DT, NULL, 0xFFFFFFFFu);
    TIDE_CHECK(other.Time.seed == 2147483647);
    // A seed that's kept: a tick doesn't change it
    TIDE_CHECK(world.Time.seed == 12345);
}

// Every generator the game left unseeded has a state of its own from the
// start: each entity's, twins included, each field's, and the singleton's.
// The ones it seeded are as it seeded them.
TIDE_TEST(random_the_engine_seeds_what_the_game_did_not)
{
    tide_world_start_seeded(&world, DT, NULL, 777u);
    tide_entity all[16];
    const int count = rollers(&world, all, 16);
    TIDE_REQUIRE(count == 10);
    int64_t states[21];
    int n = 0;
    for (int i = 0; i < count; i++) {
        const Roller r = tide_get_Roller(&world, all[i]);
        states[n++] = r.random.state;
        states[n++] = r.loot.dice.state;
    }
    states[n++] = world.Deck.random.state;
    for (int a = 0; a < n; a++) {
        TIDE_CHECK(states[a] != 0);
        for (int b = a + 1; b < n; b++) TIDE_CHECK(states[a] != states[b]);
    }
    // The twins, alike in every field the game wrote
    const Roller twin = tide_get_Roller(&world, all[8]);
    const Roller sibling = tide_get_Roller(&world, all[9]);
    TIDE_CHECK(twin.id == 100 && sibling.id == 100 && twin.random.state != sibling.random.state);
    // What the game seeded
    TIDE_CHECK(world.Deck.fixed.state == tide_random_seeded(7).state);
    int fixed = 0;
    for (uint32_t i = 0; i < world.entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&world.entities, i);
        if (!tide_has_Fixed(&world, e)) continue;
        TIDE_CHECK(tide_get_Fixed(&world, e).random.state == tide_random_seeded(42).state);
        fixed++;
    }
    TIDE_CHECK(fixed == 2);
    // The same seed again: the same states. Another: others, but for the game's own
    tide_world_start_seeded(&other, DT, NULL, 777u);
    TIDE_CHECK(tide_world_hash(&other) == tide_world_hash(&world));
    tide_world_start_seeded(&other, DT, NULL, 778u);
    tide_entity theirs[16];
    TIDE_REQUIRE(rollers(&other, theirs, 16) == 10);
    for (int i = 0; i < count; i++) {
        TIDE_CHECK(tide_get_Roller(&other, theirs[i]).random.state != tide_get_Roller(&world, all[i]).random.state);
    }
    TIDE_CHECK(other.Deck.random.state != world.Deck.random.state);
    TIDE_CHECK(other.Deck.fixed.state == world.Deck.fixed.state);
}

// What a tick rolls is what tide/random.h gives from the state the entity
// had, in the order the code is written: a call's arguments left to right,
// and a function's rolls where it's called.
TIDE_TEST(random_rolls_follow_the_code)
{
    tide_world_start_seeded(&world, DT, NULL, 31u);
    tide_entity all[16];
    const int count = rollers(&world, all, 16);
    for (int tick = 0; tick < 5; tick++) {
        Roller before[16];
        for (int i = 0; i < count; i++) before[i] = tide_get_Roller(&world, all[i]);
        tide_random deck = world.Deck.random;
        tide_world_tick(&world);
        for (int i = 0; i < count; i++) {
            const Roller after = tide_get_Roller(&world, all[i]);
            tide_random r = before[i].random;
            tide_random dice = before[i].loot.dice;
            TIDE_CHECK(after.last == tide_random_next_int_between(&r, 1, 7));
            const float x = tide_random_next_float(&r);
            const float y = tide_random_next_float(&r);
            TIDE_CHECK(after.point.x == x && after.point.y == y);
            int32_t pair = 0;
            for (int digit = 0; digit < 4; digit++) pair = pair * 10 + tide_random_next_int_below(&r, 10);
            TIDE_CHECK(after.pair == pair);
            TIDE_CHECK(after.loot.tier == tide_random_next_int_below(&dice, 100));
            TIDE_CHECK(after.random.state == r.state && after.loot.dice.state == dice.state);
            TIDE_CHECK(after.last >= 1 && after.last <= 6 && after.rolls == tick + 1);
        }
        TIDE_CHECK(world.Deck.top == tide_random_next_int_below(&deck, 52) && world.Deck.random.state == deck.state);
    }
    // The two the game gave one seed roll alike, as tide_random_seeded(42) does
    tide_random same = tide_random_seeded(42);
    int32_t last = 0;
    for (int tick = 0; tick < 5; tick++) last = tide_random_next_int_below(&same, 1000);
    for (uint32_t i = 0; i < world.entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&world.entities, i);
        if (tide_has_Fixed(&world, e)) TIDE_CHECK(tide_get_Fixed(&world, e).last == last);
    }
}

// Entities spawned as the match goes get generators of their own too, and so
// does a component each time it's added.
TIDE_TEST(random_spawns_and_adds_are_seeded_as_they_apply)
{
    tide_world_start_seeded(&world, DT, NULL, 5u);
    for (int i = 0; i < 50; i++) tide_world_tick(&world);
    tide_entity all[16];
    const int count = rollers(&world, all, 16);
    TIDE_REQUIRE(count == 13);
    for (int a = 0; a < count; a++) {
        const Roller r = tide_get_Roller(&world, all[a]);
        TIDE_CHECK(r.random.state != 0 && r.loot.dice.state != 0);
        for (int b = a + 1; b < count; b++) TIDE_CHECK(r.random.state != tide_get_Roller(&world, all[b]).random.state);
    }
    // The buff came twice, at ticks 5 and 15: seeded each time, and not alike
    TIDE_CHECK(world.Deck.firstBuff != 0 && world.Deck.secondBuff != 0 && world.Deck.firstBuff != world.Deck.secondBuff);
}

// A snapshot has the generators, so ticks run again from it roll the same:
// what rolling back to it rests on. The spawn at tick 40 is seeded the same
// the second time too.
TIDE_TEST(random_a_snapshot_rolls_the_same_again)
{
    tide_world_start_seeded(&world, DT, NULL, 99u);
    for (int i = 0; i < 30; i++) tide_world_tick(&world);
    tide_world_copy(&snapshot, &world);
    const uint64_t at_30 = tide_world_hash(&world);
    const uint64_t at_70 = hash_after(&world, 40);
    TIDE_CHECK(at_70 != at_30);
    TIDE_CHECK(tide_world_hash(&snapshot) == at_30); // The snapshot stayed where it was
    tide_world_copy(&world, &snapshot);
    TIDE_CHECK(hash_after(&world, 40) == at_70);
    // Packed and unpacked, as a world that's sent is: the generators come along
    static uint8_t bytes[1 << 20];
    const uint32_t size = tide_world_pack(&world, bytes, sizeof bytes);
    TIDE_REQUIRE(size > 0 && size <= sizeof bytes);
    TIDE_REQUIRE(tide_world_unpack(&other, bytes, size));
    TIDE_CHECK(other.Time.seed == 99 && tide_world_hash(&other) == at_70);
    TIDE_CHECK(hash_after(&other, 10) == hash_after(&world, 10));
}

// The bits, on every platform: native, WebAssembly and Android have to give
// this hash. If the generator or the seeding changes on purpose, update it
// from one platform and check the others agree.
TIDE_TEST(random_crossplatform_match)
{
    tide_world_start_seeded(&world, DT, NULL, 20261005u);
    const uint64_t h = hash_after(&world, 200);
    const uint64_t expected = 0xF262A52EC302F966ull;
    if (h != expected) printf("    random match: hash is 0x%016" PRIX64 ", expected 0x%016" PRIX64 "\n", h, expected);
    TIDE_CHECK(h == expected);
}

// Local state: a view's generators, which the match never sees. A local
// singleton's and each local entity's start as their own.
TIDE_TEST(random_local_state_rolls_too)
{
    tide_world_init(&world, DT);
    tide_local_init(&local);
    TIDE_CHECK(local.Fx.random.state != 0 && local.Fx.fixed.state == tide_random_seeded(5).state);
    tide_random fx = local.Fx.random;
    tide_random fixed = tide_random_seeded(5);
    const uint64_t match = tide_world_hash(&world);
    for (int frame = 0; frame < 10; frame++) {
        tide_draw_reset(&draw);
        tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
        TIDE_CHECK(local.Fx.last == tide_random_next_float_between(&fx, -1.0f, 1.0f));
        TIDE_CHECK(local.Fx.coin == (tide_random_next_bool(&fixed) ? 1 : 0));
    }
    TIDE_CHECK(tide_world_hash(&world) == match); // Rolling in views changes nothing of the match
    int sparks = 0;
    int32_t firsts[8];
    for (uint32_t i = 0; i < local.entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&local.entities, i);
        if (!tide_has_tide_local_component_Spark(&local, e)) continue;
        const Spark s = tide_get_tide_local_component_Spark(&local, e);
        TIDE_CHECK(s.random.state != 0 && s.first != 0);
        for (int k = 0; k < sparks; k++) TIDE_CHECK(firsts[k] != s.first);
        if (sparks < 8) firsts[sparks++] = s.first;
    }
    TIDE_CHECK(sparks == 6);
    // The rollers' local fields: each one's generator its own, on this machine alone
    tide_entity all[16];
    const int count = rollers(&world, all, 16);
    TIDE_REQUIRE(count == 10);
    for (int a = 0; a < count; a++) {
        TIDE_REQUIRE(tide_has_Roller_local(&local, all[a]));
        const Roller_local mine = tide_get_Roller_local(&local, all[a]);
        TIDE_CHECK(mine.glint.state != 0 && mine.shine != 0);
        for (int b = a + 1; b < count; b++) TIDE_CHECK(mine.shine != tide_get_Roller_local(&local, all[b]).shine);
    }
    tide_local_free(&local);
}

// The game has a seed: what sessions pick one by.
TIDE_TEST(random_the_game_says_it_has_a_seed)
{
    TIDE_CHECK(tide_game_api.seeded);
    TIDE_CHECK(sizeof(Time) == 20 && sizeof(tide_random) == 8 && sizeof(Loot) == 16);
}
