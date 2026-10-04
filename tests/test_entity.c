#include <stdlib.h>
#include <string.h>

#include "tide/entity.h"
#include "tide_test.h"

TIDE_TEST(entity_null_is_never_alive)
{
    tide_entities t = {0};
    const tide_entity null = {0};
    TIDE_CHECK(tide_entity_is_null(null));
    TIDE_CHECK(!tide_entity_alive(&t, null));
    tide_entities_free(&t);
}

TIDE_TEST(entity_create_is_alive_and_pending)
{
    tide_entities t = {0};
    const tide_entity e = tide_entity_create(&t);
    TIDE_CHECK(!tide_entity_is_null(e));
    TIDE_CHECK(tide_entity_alive(&t, e));
    TIDE_CHECK(tide_entity_location(&t, e).archetype == TIDE_ARCHETYPE_NONE);
    tide_entities_free(&t);
}

TIDE_TEST(entity_location_round_trips)
{
    tide_entities t = {0};
    const tide_entity e = tide_entity_create(&t);
    tide_entity_set_location(&t, e, (tide_location){3, 42});
    const tide_location loc = tide_entity_location(&t, e);
    TIDE_CHECK(loc.archetype == 3);
    TIDE_CHECK(loc.row == 42);
    tide_entities_free(&t);
}

TIDE_TEST(entity_destroy_invalidates_handle)
{
    tide_entities t = {0};
    const tide_entity e = tide_entity_create(&t);
    TIDE_CHECK(tide_entity_destroy(&t, e));
    TIDE_CHECK(!tide_entity_alive(&t, e));
    TIDE_CHECK(!tide_entity_destroy(&t, e));
    TIDE_CHECK(tide_entity_location(&t, e).archetype == TIDE_ARCHETYPE_NONE);
    tide_entities_free(&t);
}

TIDE_TEST(entity_reuse_bumps_generation)
{
    tide_entities t = {0};
    const tide_entity a = tide_entity_create(&t);
    tide_entity_destroy(&t, a);
    const tide_entity b = tide_entity_create(&t);
    TIDE_CHECK(b.index == a.index);
    TIDE_CHECK(b.generation != a.generation);
    TIDE_CHECK(tide_entity_alive(&t, b));
    TIDE_CHECK(!tide_entity_alive(&t, a));
    tide_entities_free(&t);
}

TIDE_TEST(entity_reuse_order_is_lifo)
{
    tide_entities t = {0};
    const tide_entity a = tide_entity_create(&t);
    const tide_entity b = tide_entity_create(&t);
    tide_entity_destroy(&t, a);
    tide_entity_destroy(&t, b);
    TIDE_CHECK(tide_entity_create(&t).index == b.index);
    TIDE_CHECK(tide_entity_create(&t).index == a.index);
    tide_entities_free(&t);
}

// It grows as entities are made, page after page, with no limit but memory.
TIDE_TEST(entity_table_grows)
{
    tide_entities t = {0};
    const uint32_t n = 5u << TIDE_ENTITY_PAGE_SHIFT;
    for (uint32_t i = 0; i < n; i++) {
        const tide_entity e = tide_entity_create(&t);
        TIDE_REQUIRE(e.index == i);
        tide_entity_set_location(&t, e, (tide_location){1, i});
    }
    TIDE_CHECK(t.pages == 5);
    TIDE_CHECK(tide_entity_location(&t, (tide_entity){n - 1u, 1}).row == n - 1u);
    tide_entities_free(&t);
    TIDE_CHECK(t.pages == 0 && t.next_unused == 0);
}

// A snapshot shares the table's pages, and changing either leaves the other
// as it was.
TIDE_TEST(entity_snapshots_share_until_changed)
{
    tide_entities t = {0};
    tide_entities snapshot = {0};
    const tide_entity a = tide_entity_create(&t);
    const tide_entity b = tide_entity_create(&t);
    tide_entities_copy(&snapshot, &t);
    TIDE_CHECK(snapshot.page[0] == t.page[0]);
    TIDE_CHECK(tide_entities_hash(1, &snapshot) == tide_entities_hash(1, &t));

    tide_entity_destroy(&t, a);
    tide_entity_set_location(&t, b, (tide_location){2, 7});
    TIDE_CHECK(snapshot.page[0] != t.page[0]);
    TIDE_CHECK(tide_entity_alive(&snapshot, a) && !tide_entity_alive(&t, a));
    TIDE_CHECK(tide_entity_location(&snapshot, b).archetype == TIDE_ARCHETYPE_NONE);
    TIDE_CHECK(tide_entities_hash(1, &snapshot) != tide_entities_hash(1, &t));

    // Back to the snapshot: the same entities, and the same hash
    tide_entities_copy(&t, &snapshot);
    TIDE_CHECK(tide_entity_alive(&t, a));
    TIDE_CHECK(tide_entities_hash(1, &snapshot) == tide_entities_hash(1, &t));
    tide_entities_free(&t);
    tide_entities_free(&snapshot);
}

// What an entity was spawned as is kept beside its slot: known once it's set,
// only while the entity is alive, and through the table growing.
TIDE_TEST(entity_born_is_kept_beside_the_table)
{
    tide_entities t = {0};
    const tide_entity a = tide_entity_create(&t);
    const tide_entity b = tide_entity_create(&t);
    TIDE_CHECK(tide_entity_born(&t, a) == 0u); // Nothing said what it was born as
    TIDE_CHECK(tide_entity_born(&t, (tide_entity){0}) == 0u);
    tide_entity_set_born(&t, a, 0xA11CEu);
    TIDE_CHECK(tide_entity_born(&t, a) == 0xA11CEu && tide_entity_born(&t, b) == 0u);

    // The pages of slots outgrow the room for them several times
    const uint32_t n = 9u << TIDE_ENTITY_PAGE_SHIFT;
    for (uint32_t i = 2; i < n; i++) {
        const tide_entity e = tide_entity_create(&t);
        if (i % 3u == 0) tide_entity_set_born(&t, e, i);
    }
    TIDE_CHECK(tide_entity_born(&t, a) == 0xA11CEu);
    for (uint32_t i = 2; i < n; i += 97u) TIDE_CHECK(tide_entity_born(&t, (tide_entity){i, 1}) == (i % 3u == 0 ? i : 0u));

    // An entity that's gone was born as nothing, and so is a handle that isn't its slot's
    tide_entity_destroy(&t, a);
    TIDE_CHECK(tide_entity_born(&t, a) == 0u);
    const tide_entity again = tide_entity_create(&t);
    TIDE_REQUIRE(again.index == a.index && again.generation != a.generation);
    tide_entity_set_born(&t, a, 5u); // Not alive: nothing is set
    tide_entity_set_born(&t, again, 6u);
    TIDE_CHECK(tide_entity_born(&t, a) == 0u && tide_entity_born(&t, again) == 6u);

    // Two of them can be one entity's when they're the same, or one isn't known
    TIDE_CHECK(tide_born_same(6u, 6u) && tide_born_same(0u, 6u) && tide_born_same(6u, 0u) && tide_born_same(0u, 0u));
    TIDE_CHECK(!tide_born_same(6u, 7u));
    tide_entities_free(&t);
    TIDE_CHECK(t.pages == 0 && !t.page);
}

// Snapshots share what entities were born as, as they share the slots, and
// one that changes leaves the other's.
TIDE_TEST(entity_born_is_shared_with_snapshots)
{
    tide_entities t = {0};
    tide_entities snapshot = {0};
    const tide_entity a = tide_entity_create(&t);
    const tide_entity b = tide_entity_create(&t);
    tide_entity_set_born(&t, a, 11u);
    tide_entities_copy(&snapshot, &t);
    TIDE_CHECK(snapshot.page[snapshot.room] == t.page[t.room]);
    TIDE_CHECK(tide_entity_born(&snapshot, a) == 11u && tide_entity_born(&snapshot, b) == 0u);

    // A replay spawns something else in b's slot: the snapshot keeps what it had
    tide_entity_set_born(&t, b, 22u);
    tide_entity_set_born(&t, a, 33u);
    TIDE_CHECK(snapshot.page[snapshot.room] != t.page[t.room]);
    TIDE_CHECK(tide_entity_born(&snapshot, a) == 11u && tide_entity_born(&snapshot, b) == 0u);
    TIDE_CHECK(tide_entity_born(&t, a) == 33u && tide_entity_born(&t, b) == 22u);

    // Back to the snapshot, and onto a table that knew more
    tide_entities_copy(&t, &snapshot);
    TIDE_CHECK(tide_entity_born(&t, a) == 11u && tide_entity_born(&t, b) == 0u);
    tide_entities bare = {0};
    const tide_entity c = tide_entity_create(&bare);
    tide_entities_copy(&t, &bare); // Nothing was set in it: nothing comes along
    TIDE_CHECK(tide_entity_born(&t, c) == 0u && t.page[t.room] == NULL);
    tide_entities_free(&bare);
    tide_entities_free(&t);
    tide_entities_free(&snapshot);
}

// It's beside the world, not in it: the table's hash and its bytes are the
// same with it or without, and a table read back from bytes knows none.
TIDE_TEST(entity_born_is_never_hashed_or_packed)
{
    tide_entities plain = {0};
    tide_entities born = {0};
    for (uint32_t i = 0; i < 1500; i++) {
        tide_entity_create(&plain);
        tide_entity_set_born(&born, tide_entity_create(&born), i + 1u);
    }
    tide_entity_destroy(&plain, (tide_entity){9, 1});
    tide_entity_destroy(&born, (tide_entity){9, 1});
    TIDE_CHECK(tide_entities_hash(1, &plain) == tide_entities_hash(1, &born));

    const uint32_t size = tide_entities_packed_size(&born);
    TIDE_REQUIRE(size == tide_entities_packed_size(&plain));
    uint8_t *bytes = malloc(size);
    uint8_t *other = malloc(size);
    tide_writer w = {bytes, size, 0, false, false};
    tide_writer v = {other, size, 0, false, false};
    tide_entities_pack(&born, &w);
    tide_entities_pack(&plain, &v);
    TIDE_CHECK(w.size == size && v.size == size && memcmp(bytes, other, size) == 0);

    tide_writer pages = {bytes, size, 0, false, false};
    tide_writer others = {other, size, 0, false, false};
    tide_entities_hash_pages(&born, &pages);
    tide_entities_hash_pages(&plain, &others);
    TIDE_CHECK(pages.size == others.size && memcmp(bytes, other, pages.size) == 0);

    tide_writer again = {bytes, size, 0, false, false};
    tide_entities_pack(&born, &again);
    tide_entities back = {0};
    tide_reader r = {bytes, size, 0, false};
    TIDE_REQUIRE(tide_entities_unpack(&back, &r));
    TIDE_CHECK(tide_entity_born(&born, (tide_entity){40, 1}) == 41u);
    TIDE_CHECK(tide_entity_born(&back, (tide_entity){40, 1}) == 0u); // It came as bytes
    free(bytes);
    free(other);
    tide_entities_free(&plain);
    tide_entities_free(&born);
    tide_entities_free(&back);
}

TIDE_TEST(entity_table_packs_and_unpacks)
{
    tide_entities t = {0};
    for (uint32_t i = 0; i < 1500; i++) tide_entity_create(&t);
    tide_entity_destroy(&t, (tide_entity){7, 1});
    tide_entity_destroy(&t, (tide_entity){1200, 1});
    const uint32_t size = tide_entities_packed_size(&t);
    uint8_t *bytes = malloc(size);
    tide_writer w = {bytes, size, 0, false, false};
    tide_entities_pack(&t, &w);
    TIDE_CHECK(!w.overflow && w.size == size);

    tide_entities back = {0};
    tide_reader r = {bytes, size, 0, false};
    TIDE_REQUIRE(tide_entities_unpack(&back, &r));
    TIDE_CHECK(tide_entities_hash(1, &back) == tide_entities_hash(1, &t));
    TIDE_CHECK(tide_entity_create(&back).index == 1200); // Free slots come back in the same order
    TIDE_CHECK(tide_entity_create(&back).index == 7);

    // A free list that goes round isn't a table
    tide_entities broken = {0};
    tide_entity_slot *slot = (tide_entity_slot *)(bytes + 8u + 7u * sizeof(tide_entity_slot));
    slot->row = 1201; // Slot 7 leads back to 1200
    tide_reader again = {bytes, size, 0, false};
    TIDE_CHECK(!tide_entities_unpack(&broken, &again));
    tide_entities_free(&broken);
    free(bytes);
    tide_entities_free(&t);
    tide_entities_free(&back);
}
