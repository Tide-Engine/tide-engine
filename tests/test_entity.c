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

TIDE_TEST(entity_reuse_keeps_identity_independent_of_storage)
{
    tide_entities t = {0};
    const tide_entity a = tide_entity_create(&t);
    tide_entity_destroy(&t, a);
    const tide_entity b = tide_entity_create(&t);
    TIDE_CHECK(tide_entity_slot_index(&t, b) == 0);
    TIDE_CHECK(!tide_entity_equal(b, a));
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
    TIDE_CHECK(tide_entity_slot_index(&t, tide_entity_create(&t)) == 1);
    TIDE_CHECK(tide_entity_slot_index(&t, tide_entity_create(&t)) == 0);
    tide_entities_free(&t);
}

TIDE_TEST(entity_table_grows)
{
    tide_entities t = {0};
    const uint32_t n = 5u << TIDE_ENTITY_PAGE_SHIFT;
    for (uint32_t i = 0; i < n; i++) {
        const tide_entity e = tide_entity_create(&t);
        TIDE_REQUIRE(tide_entity_slot_index(&t, e) == i);
        tide_entity_set_location(&t, e, (tide_location){1, i});
    }
    TIDE_CHECK(t.pages == 5);
    TIDE_CHECK(tide_entity_location(&t, (tide_entity){0, n}).row == n - 1u);
    tide_entities_free(&t);
    TIDE_CHECK(t.pages == 0 && t.next_unused == 0);
}

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

    tide_entities_copy(&t, &snapshot);
    TIDE_CHECK(tide_entity_alive(&t, a));
    TIDE_CHECK(tide_entities_hash(1, &snapshot) == tide_entities_hash(1, &t));
    tide_entities_free(&t);
    tide_entities_free(&snapshot);
}

TIDE_TEST(entity_born_is_kept_beside_the_table)
{
    tide_entities t = {0};
    const tide_entity a = tide_entity_create(&t);
    const tide_entity b = tide_entity_create(&t);
    TIDE_CHECK(tide_entity_born(&t, a) == 0u);
    TIDE_CHECK(tide_entity_born(&t, (tide_entity){0}) == 0u);
    tide_entity_set_born(&t, a, 0xA11CEu);
    TIDE_CHECK(tide_entity_born(&t, a) == 0xA11CEu && tide_entity_born(&t, b) == 0u);

    const uint32_t n = 9u << TIDE_ENTITY_PAGE_SHIFT;
    for (uint32_t i = 2; i < n; i++) {
        const tide_entity e = tide_entity_create(&t);
        if (i % 3u == 0) tide_entity_set_born(&t, e, i);
    }
    TIDE_CHECK(tide_entity_born(&t, a) == 0xA11CEu);
    for (uint32_t i = 2; i < n; i += 97u) TIDE_CHECK(tide_entity_born(&t, (tide_entity){0, i + 1u}) == (i % 3u == 0 ? i : 0u));

    tide_entity_destroy(&t, a);
    TIDE_CHECK(tide_entity_born(&t, a) == 0u);
    const tide_entity again = tide_entity_create(&t);
    TIDE_REQUIRE(tide_entity_slot_index(&t, again) == 0 && !tide_entity_equal(again, a));
    TIDE_CHECK(tide_entity_born(&t, again) == 0);
    tide_entity_set_born(&t, a, 5u);
    tide_entity_set_born(&t, again, 6u);
    TIDE_CHECK(tide_entity_born(&t, a) == 0u && tide_entity_born(&t, again) == 6u);

    TIDE_CHECK(tide_born_same(6u, 6u) && tide_born_same(0u, 6u) && tide_born_same(6u, 0u) && tide_born_same(0u, 0u));
    TIDE_CHECK(!tide_born_same(6u, 7u));
    tide_entities_free(&t);
    TIDE_CHECK(t.pages == 0 && !t.page);
}

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

    tide_entity_set_born(&t, b, 22u);
    tide_entity_set_born(&t, a, 33u);
    TIDE_CHECK(snapshot.page[snapshot.room] != t.page[t.room]);
    TIDE_CHECK(tide_entity_born(&snapshot, a) == 11u && tide_entity_born(&snapshot, b) == 0u);
    TIDE_CHECK(tide_entity_born(&t, a) == 33u && tide_entity_born(&t, b) == 22u);

    tide_entities_copy(&t, &snapshot);
    TIDE_CHECK(tide_entity_born(&t, a) == 11u && tide_entity_born(&t, b) == 0u);
    tide_entities bare = {0};
    const tide_entity c = tide_entity_create(&bare);
    tide_entities_copy(&t, &bare);
    TIDE_CHECK(tide_entity_born(&t, c) == 0u && t.page[t.room] == NULL);
    tide_entities_free(&bare);
    tide_entities_free(&t);
    tide_entities_free(&snapshot);
}

TIDE_TEST(entity_born_is_never_hashed_or_packed)
{
    tide_entities plain = {0};
    tide_entities born = {0};
    for (uint32_t i = 0; i < 1500; i++) {
        tide_entity_create(&plain);
        tide_entity_set_born(&born, tide_entity_create(&born), i + 1u);
    }
    tide_entity_destroy(&plain, (tide_entity){0, 10});
    tide_entity_destroy(&born, (tide_entity){0, 10});
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
    TIDE_CHECK(tide_entity_born(&born, (tide_entity){0, 41}) == 41u);
    TIDE_CHECK(tide_entity_born(&back, (tide_entity){0, 41}) == 0u);
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
    tide_entity_destroy(&t, (tide_entity){0, 8});
    tide_entity_destroy(&t, (tide_entity){0, 1201});
    const uint32_t size = tide_entities_packed_size(&t);
    uint8_t *bytes = malloc(size);
    tide_writer w = {bytes, size, 0, false, false};
    tide_entities_pack(&t, &w);
    TIDE_CHECK(!w.overflow && w.size == size);

    tide_entities back = {0};
    tide_reader r = {bytes, size, 0, false};
    TIDE_REQUIRE(tide_entities_unpack(&back, &r));
    TIDE_CHECK(tide_entities_hash(1, &back) == tide_entities_hash(1, &t));
    TIDE_CHECK(tide_entity_slot_index(&back, tide_entity_create(&back)) == 1200);
    TIDE_CHECK(tide_entity_slot_index(&back, tide_entity_create(&back)) == 7);

    tide_entities broken = {0};
    const size_t slot_offset = 28u + 7u * sizeof(tide_entity_slot);
    tide_entity_slot slot;
    memcpy(&slot, bytes + slot_offset, sizeof slot);
    slot.row = 1201;
    memcpy(bytes + slot_offset, &slot, sizeof slot);
    tide_reader again = {bytes, size, 0, false};
    TIDE_CHECK(!tide_entities_unpack(&broken, &again));
    tide_entities_free(&broken);
    free(bytes);
    tide_entities_free(&t);
    tide_entities_free(&back);
}

// An erased entity leaves nothing of its key: the table still packs, unpacks and takes the key again
TIDE_TEST(entity_erase_leaves_a_table_that_packs_and_takes_the_key_again)
{
    tide_entities t = {0};
    tide_entity kept[40];
    for (uint32_t i = 0; i < 40; i++) kept[i] = tide_entity_create(&t);
    TIDE_REQUIRE(tide_entity_erase(&t, kept[3]));
    TIDE_REQUIRE(tide_entity_erase(&t, kept[17]));
    TIDE_CHECK(!tide_entity_erase(&t, kept[17]));
    TIDE_CHECK(!tide_entity_alive(&t, kept[3]) && !tide_entity_died_here(&t, kept[3]));
    const uint32_t size = tide_entities_packed_size(&t);
    uint8_t *bytes = malloc(size);
    tide_writer w = {bytes, size, 0, false, false};
    tide_entities_pack(&t, &w);
    tide_entities back = {0};
    tide_reader r = {bytes, size, 0, false};
    TIDE_REQUIRE(tide_entities_unpack(&back, &r));
    TIDE_CHECK(tide_entities_hash(1, &back) == tide_entities_hash(1, &t));
    // Its key can be installed again, in either table, and a slot freed this way is reused
    TIDE_CHECK(tide_entity_install(&back, kept[17]) && tide_entity_alive(&back, kept[17]));
    TIDE_CHECK(tide_entity_install(&t, kept[3]) && tide_entity_alive(&t, kept[3]));
    TIDE_CHECK(tide_entity_slot_index(&t, kept[3]) == 17u);
    free(bytes);
    tide_entities_free(&t);
    tide_entities_free(&back);
}

TIDE_TEST(entity_logical_keys_ignore_hidden_allocations_and_recycled_slots)
{
    tide_entities server = {0}, client = {0};
    const tide_entity public = {1, 0x1234567800000001ull};
    const tide_entity hidden = {1, 0x1234567800000002ull};
    tide_entity_create_key(&server, hidden);
    tide_entity_create_key(&server, public);
    TIDE_REQUIRE(tide_entity_install(&client, public));
    TIDE_CHECK(tide_entity_slot_index(&server, public) == 1);
    TIDE_CHECK(tide_entity_slot_index(&client, public) == 0);
    tide_entity_destroy(&server, hidden);
    const tide_entity bullet = {2, 0xFEDCBA9876543210ull};
    TIDE_CHECK(tide_entity_equal(tide_entity_create_key(&server, bullet), tide_entity_create_key(&client, bullet)));
    TIDE_CHECK(tide_entity_slot_index(&server, bullet) == 0);
    TIDE_CHECK(tide_entity_slot_index(&client, bullet) == 1);
    TIDE_CHECK(tide_entity_equal(tide_entity_in_slot(&server, 1), public));
    TIDE_CHECK(tide_entity_compare(public, bullet) < 0);
    TIDE_CHECK(!tide_entity_equal(public, (tide_entity){public.birth + (1ull << 32), public.lineage}));
    TIDE_CHECK(tide_entity_hash(1, public) != tide_entity_hash(1, (tide_entity){public.birth, public.lineage ^ (1ull << 63)}));
    tide_entities_free(&server);
    tide_entities_free(&client);
}

TIDE_TEST(entity_key_registry_survives_destroy_snapshot_and_wire)
{
    tide_entities t = {0}, copy = {0}, loaded = {0};
    const tide_entity a = {0x100000001ull, 3}, b = {0x100000001ull, 7};
    tide_entity_create_key(&t, a);
    tide_entity_create_key(&t, b);
    tide_entity_destroy(&t, a);
    tide_entities_copy(&copy, &t);
    const uint32_t size = tide_entities_packed_size(&t);
    uint8_t *bytes = tide_alloc(size);
    tide_writer w = {bytes, size, 0, false, false};
    tide_entities_pack(&t, &w);
    tide_reader r = {bytes, size, 0, false};
    TIDE_REQUIRE(tide_entities_unpack(&loaded, &r));
    TIDE_CHECK(loaded.key_clock == a.birth);
    TIDE_CHECK(tide_entities_hash(3, &loaded) == tide_entities_hash(3, &copy));
    // Destroyed keys remain in the current-clock registry, including on wire.
    TIDE_CHECK(size == 28u + 2u * sizeof(tide_entity_slot) + 2u * sizeof(tide_entity));
    for (uint32_t cut = 0; cut < size; cut++) {
        tide_entities bad = {0};
        tide_reader short_read = {bytes, cut, 0, false};
        TIDE_CHECK(!tide_entities_unpack(&bad, &short_read));
        tide_entities_free(&bad);
    }
    // A repeated logical key in physical storage is rejected transactionally.
    const size_t slots_offset = 28u + 2u * sizeof(tide_entity);
    tide_entity_slot duplicate_slot;
    memcpy(&duplicate_slot, bytes + slots_offset + sizeof duplicate_slot, sizeof duplicate_slot);
    memcpy(bytes + slots_offset, &duplicate_slot, sizeof duplicate_slot);
    tide_reader duplicate = {bytes, size, 0, false};
    const uint64_t before = tide_entities_hash(3, &loaded);
    TIDE_CHECK(!tide_entities_unpack(&loaded, &duplicate));
    TIDE_CHECK(tide_entities_hash(3, &loaded) == before);
    free(bytes);
    tide_entities_free(&loaded);
    tide_entities_free(&copy);
    tide_entities_free(&t);
}

TIDE_TEST(entity_lookup_capacity_tracks_live_population_not_lifetime_churn)
{
    tide_entities t = {0};
    tide_entity live[32];
    for (uint32_t i = 0; i < 32; i++) {
        live[i] = tide_entity_create(&t);
        tide_entity_set_location(&t, live[i], (tide_location){1, i});
    }
    // The private index descriptor starts with its capacity; inspect bytes rather
    // than exposing storage policy as a public API or aliasing its private type.
    uint64_t capacity = 0;
    memcpy(&capacity, t.index, sizeof capacity);
    for (uint32_t tick = 0; tick < 20000; tick++) {
        const uint32_t at = tick % 32u;
        TIDE_REQUIRE(tide_entity_destroy(&t, live[at]));
        live[at] = tide_entity_create(&t);
        TIDE_REQUIRE(tide_entity_alive(&t, live[at]));
        tide_entities_settle(&t);
    }
    uint64_t after = 0;
    memcpy(&after, t.index, sizeof after);
    TIDE_CHECK(after == capacity && t.next_unused == 32u);
    for (uint32_t i = 0; i < 32; i++) TIDE_CHECK(tide_entity_alive(&t, live[i]));
    tide_entities_free(&t);
}

TIDE_TEST(entity_death_requires_the_exact_retained_key)
{
    tide_entities t = {0};
    const tide_entity e = tide_entity_create(&t);
    TIDE_CHECK(!tide_entity_died_here(&t, (tide_entity){7, 9}));
    TIDE_CHECK(!tide_entity_died_here(&t, e));
    tide_entity_destroy(&t, e);
    TIDE_CHECK(tide_entity_died_here(&t, e));
    const tide_entity next = tide_entity_create(&t);
    TIDE_CHECK(!tide_entity_died_here(&t, e));
    TIDE_CHECK(tide_entity_alive(&t, next));
    tide_entities_free(&t);
}

TIDE_TEST(entity_ordered_cache_survives_changes_and_snapshots)
{
    tide_entities t = {0}, snapshot = {0};
    tide_entity_install(&t, (tide_entity){3, 7});
    tide_entity_install(&t, (tide_entity){1, 9});
    tide_entity_install(&t, (tide_entity){1, 2});
    uint32_t count;
    const tide_entity *first = tide_entities_ordered(&t, &count);
    TIDE_REQUIRE(count == 3);
    TIDE_CHECK(tide_entity_equal(first[0], (tide_entity){1, 2}));
    TIDE_CHECK(tide_entity_equal(first[1], (tide_entity){1, 9}));
    TIDE_CHECK(tide_entity_equal(first[2], (tide_entity){3, 7}));
    tide_entity_set_location(&t, first[0], (tide_location){4, 10});
    tide_entity_snap(&t, first[1]);
    tide_entity_set_born(&t, first[2], 7);
    TIDE_CHECK(first == tide_entities_ordered(&t, &count));
    const uint64_t hash = tide_entities_hash(3, &t);
    tide_entities_copy(&snapshot, &t);
    const tide_entity *saved = tide_entities_ordered(&snapshot, &count);
    TIDE_CHECK(saved == first && count == 3); // A copy has its source's order until either changes
    TIDE_CHECK(tide_entities_hash(3, &t) == hash);
    tide_entity_forget(&t, first[1], true);
    tide_entity_destroy(&t, first[0]);
    tide_entity_create_key(&t, (tide_entity){4, 8});
    const tide_entity *current = tide_entities_ordered(&t, &count);
    TIDE_CHECK(current != first && count == 2);
    TIDE_CHECK(tide_entity_equal(current[0], (tide_entity){3, 7}));
    TIDE_CHECK(tide_entity_equal(current[1], (tide_entity){4, 8}));
    // An outer query keeps its immutable original list until settlement.
    TIDE_CHECK(tide_entity_equal(first[0], (tide_entity){1, 2}));
    TIDE_CHECK(tide_entity_equal(saved[1], (tide_entity){1, 9}));
    tide_entities_settle(&t);
    TIDE_CHECK(current == tide_entities_ordered(&t, &count));
    tide_entities_free(&snapshot);
    tide_entities_free(&t);
}

TIDE_TEST(entity_ordered_archetypes_cache_membership_lifetime_and_snapshot)
{
    tide_entities t = {0}, copy = {0};
    const tide_entity a = tide_entity_create_key(&t, (tide_entity){1, 9});
    const tide_entity b = tide_entity_create_key(&t, (tide_entity){1, 3});
    const tide_entity c = tide_entity_create_key(&t, (tide_entity){1, 7});
    tide_entity_set_location(&t, a, (tide_location){2, 10});
    tide_entity_set_location(&t, b, (tide_location){4, 20});
    tide_entity_set_location(&t, c, (tide_location){6, 30});
    uint32_t count;
    uint32_t arches[] = {2, 4};
    const tide_entity *first = tide_entities_ordered_archetypes(&t, arches, 2, &count);
    TIDE_REQUIRE(count == 2 && tide_entity_equal(first[0], b) && tide_entity_equal(first[1], a));
    arches[0] = 0; // Caller storage is not retained, including across code reload.
    const uint32_t same[] = {2, 4};
    TIDE_CHECK(first == tide_entities_ordered_archetypes(&t, same, 2, &count));
    tide_entity_set_location(&t, a, (tide_location){2, 100});
    TIDE_CHECK(first == tide_entities_ordered_archetypes(&t, same, 2, &count));
    const uint64_t hash = tide_entities_hash(7, &t);
    tide_entities_copy(&copy, &t);
    const tide_entity *saved = tide_entities_ordered_archetypes(&copy, same, 2, &count);
    TIDE_CHECK(saved == first && count == 2 && tide_entities_hash(7, &t) == hash);
    const tide_entity *all = tide_entities_ordered(&t, &count);
    tide_entity_set_location(&t, c, (tide_location){2, 30});
    TIDE_CHECK(all == tide_entities_ordered(&t, &count));
    const tide_entity *next = tide_entities_ordered_archetypes(&t, same, 2, &count);
    TIDE_REQUIRE(next != first && count == 3);
    TIDE_CHECK(tide_entity_equal(next[0], b) && tide_entity_equal(next[1], c));
    TIDE_CHECK(tide_entity_equal(first[1], a) && tide_entity_equal(saved[1], a));
    tide_entity_destroy(&t, b);
    TIDE_CHECK(tide_entities_ordered_archetypes(&t, same, 2, &count) != next && count == 2);
    TIDE_CHECK(tide_entities_ordered_archetypes(&t, NULL, 0, &count) == NULL && count == 0);
    tide_entities_settle(&t);
    tide_entities_free(&copy);
    tide_entities_free(&t);
}

// A copy of a table has the orders made of it, sorted once for both: a world copied each tick sorts its entities
// only when some came or went. Each side's changes are its own, and an order outlives whichever table lets go first.
TIDE_TEST(entity_orders_are_shared_with_copies_until_either_changes)
{
    tide_entities t = {0}, copy = {0}, other = {0};
    tide_entity made[6];
    for (uint32_t i = 0; i < 6; i++) {
        made[i] = tide_entity_create_key(&t, (tide_entity){1, 60 - i * 10});
        tide_entity_set_location(&t, made[i], (tide_location){2 + i % 2, i});
    }
    const uint32_t two[] = {2}, both[] = {2, 3};
    uint32_t count, copied;
    const uint32_t *slots, *copied_slots;
    const tide_entity *all = tide_entities_ordered(&t, &count);
    TIDE_REQUIRE(count == 6 && tide_entity_equal(all[0], made[5]));
    const tide_entity *evens = tide_entities_ordered_archetypes_slots(&t, two, 1, &count, &slots);
    TIDE_REQUIRE(count == 3 && tide_entity_equal(evens[0], made[4]) && slots[0] == 4);

    // The copy has both, and makes the one its source never made
    tide_entity_create_key(&copy, (tide_entity){9, 9}); // What it had goes
    (void)tide_entities_ordered(&copy, &copied);
    tide_entities_copy(&copy, &t);
    TIDE_CHECK(tide_entities_ordered(&copy, &copied) == all && copied == 6);
    TIDE_CHECK(tide_entities_ordered_archetypes_slots(&copy, two, 1, &copied, &copied_slots) == evens && copied == 3
               && copied_slots == slots);
    const tide_entity *every = tide_entities_ordered_archetypes(&copy, both, 2, &copied);
    TIDE_CHECK(copied == 6 && every != all && tide_entity_equal(every[5], made[0]));
    TIDE_CHECK(tide_entities_ordered_archetypes(&t, both, 2, &count) != every && count == 6);

    // The copy changes: its orders are made again, its source's are as they were
    TIDE_REQUIRE(tide_entity_destroy(&copy, made[4]));
    const tide_entity *fewer = tide_entities_ordered_archetypes_slots(&copy, two, 1, &copied, &copied_slots);
    TIDE_CHECK(fewer != evens && copied == 2 && tide_entity_equal(fewer[0], made[2]) && copied_slots[0] == 2);
    TIDE_CHECK(tide_entities_ordered(&copy, &copied) != all && copied == 5);
    TIDE_CHECK(tide_entities_ordered(&t, &count) == all && count == 6);
    TIDE_CHECK(tide_entities_ordered_archetypes_slots(&t, two, 1, &count, &slots) == evens && count == 3);
    tide_entities_settle(&copy);
    TIDE_CHECK(tide_entity_equal(evens[0], made[4]) && tide_entity_equal(all[0], made[5])); // Still the source's

    // The source changes: a copy made before keeps what it had, one made after has the new
    tide_entities_copy(&other, &t);
    const tide_entity added = tide_entity_create_key(&t, (tide_entity){1, 5});
    tide_entity_set_location(&t, added, (tide_location){2, 6});
    const tide_entity *more = tide_entities_ordered_archetypes_slots(&t, two, 1, &count, &slots);
    TIDE_CHECK(more != evens && count == 4 && tide_entity_equal(more[0], added) && slots[0] == 6);
    TIDE_CHECK(tide_entities_ordered_archetypes(&other, two, 1, &copied) == evens && copied == 3);
    tide_entities_copy(&copy, &t);
    TIDE_CHECK(tide_entities_ordered_archetypes(&copy, two, 1, &copied) == more && copied == 4);

    // An entity that moves to another archetype changes the subsets alone
    tide_entity_set_location(&other, made[0], (tide_location){3, 9});
    TIDE_CHECK(tide_entities_ordered(&other, &copied) == all && copied == 6);
    const tide_entity *moved = tide_entities_ordered_archetypes(&other, two, 1, &copied);
    TIDE_CHECK(moved != evens && copied == 2 && tide_entity_equal(moved[0], made[4]));

    // The source settles and goes: the copies' orders are theirs
    tide_entities_settle(&t);
    tide_entities_free(&t);
    TIDE_CHECK(tide_entity_equal(more[0], added) && tide_entity_equal(more[3], made[0]));
    TIDE_CHECK(tide_entities_ordered_archetypes(&copy, two, 1, &copied) == more && copied == 4);
    TIDE_CHECK(tide_entities_ordered(&other, &copied) == all && tide_entity_equal(all[5], made[0]));
    tide_entities_free(&copy);
    tide_entities_free(&other);
}

// A walk in the keys' order reads each entity's place from its slot, and a slot that's no longer the key's own
// (destroyed since the order was made, and maybe another entity's now) gives what looking the key up gives.
TIDE_TEST(entity_ordered_archetypes_slots_give_each_keys_place)
{
    tide_entities t = {0};
    tide_entity made[40];
    for (uint32_t i = 0; i < 40; i++)
    {
        made[i] = tide_entity_create_key(&t, (tide_entity){1, 1000u - i * 7u});
        tide_entity_set_location(&t, made[i], (tide_location){i % 3u, 100u + i});
    }
    const uint32_t arches[] = {0, 2};
    uint32_t count = 0, plain_count = 0;
    const uint32_t *slots = NULL;
    const tide_entity *keys = tide_entities_ordered_archetypes_slots(&t, arches, 2, &count, &slots);
    TIDE_REQUIRE(count == 27 && slots != NULL);
    TIDE_CHECK(keys == tide_entities_ordered_archetypes(&t, arches, 2, &plain_count) && plain_count == count);
    for (uint32_t i = 0; i < count; i++)
    {
        TIDE_CHECK(i == 0 || tide_entity_compare(keys[i - 1], keys[i]) < 0);
        TIDE_CHECK(slots[i] == tide_entity_slot_index(&t, keys[i]));
        const tide_location by_key = tide_entity_location(&t, keys[i]);
        const tide_location by_slot = tide_entity_location_at(&t, slots[i], keys[i]);
        TIDE_CHECK(by_key.archetype == by_slot.archetype && by_key.row == by_slot.row && by_key.archetype != 1u);
    }
    // A row that moves keeps its slot: the same arrays, the new place
    tide_entity_set_location(&t, keys[3], (tide_location){2, 7777});
    TIDE_CHECK(tide_entity_location_at(&t, slots[3], keys[3]).row == 7777u);
    // Destroyed, its slot taken by a newer entity: the old arrays stay (until settle), the old key finds nothing
    // and never the newcomer's place
    const tide_entity gone = keys[5];
    const uint32_t gone_slot = slots[5];
    TIDE_CHECK(tide_entity_destroy(&t, gone));
    const tide_entity newer = tide_entity_create_key(&t, (tide_entity){1, 5000});
    tide_entity_set_location(&t, newer, (tide_location){0, 4242});
    TIDE_CHECK(tide_entity_slot_index(&t, newer) == gone_slot);
    TIDE_CHECK(tide_entity_location_at(&t, gone_slot, gone).archetype == TIDE_ARCHETYPE_NONE);
    TIDE_CHECK(tide_entity_location_at(&t, gone_slot, newer).row == 4242u);
    // A slot that isn't the key's at all (or none the table has) is looked up by the key
    TIDE_CHECK(tide_entity_location_at(&t, slots[8], keys[9]).row == tide_entity_location(&t, keys[9]).row);
    TIDE_CHECK(tide_entity_location_at(&t, UINT32_MAX, keys[9]).row == tide_entity_location(&t, keys[9]).row);
    TIDE_CHECK(tide_entity_born_at(&t, UINT32_MAX) == 0u);
    tide_entity_set_born(&t, keys[9], 31u);
    TIDE_CHECK(tide_entity_born_at(&t, slots[9]) == 31u && tide_entity_born(&t, keys[9]) == 31u);
    const tide_entity *next = tide_entities_ordered_archetypes_slots(&t, arches, 2, &count, &slots);
    TIDE_REQUIRE(next != keys && count == 27);
    for (uint32_t i = 0; i < count; i++)
        TIDE_CHECK(slots[i] == tide_entity_slot_index(&t, next[i]));
    TIDE_CHECK(tide_entities_ordered_archetypes_slots(&t, NULL, 0, &count, &slots) == NULL && count == 0 && !slots);
    tide_entities_settle(&t);
    tide_entities_free(&t);
}

// The keys of `arches` (every alive one for none) by going through every slot and sorting them, with their slots:
// what an order has to be, however it was come by
typedef struct sorted_key
{
    tide_entity key;
    uint32_t slot;
} sorted_key;
static int sorted_key_compare(const void *a, const void *b)
{
    return tide_entity_compare(((const sorted_key *)a)->key, ((const sorted_key *)b)->key);
}
static bool order_is_the_slots(const tide_entities *t, const uint32_t *arches, const uint32_t n)
{
    sorted_key *want = malloc(((size_t)t->next_unused + 1u) * sizeof *want);
    uint32_t wanted = 0;
    for (uint32_t at = 0; at < t->next_unused; at++)
    {
        const tide_entity_slot *slot = tide_entity_slot_of(t, at);
        bool in = slot->state == TIDE_ENTITY_ALIVE && !arches;
        for (uint32_t k = 0; k < n && slot->state == TIDE_ENTITY_ALIVE; k++)
            in = in || slot->archetype == arches[k];
        if (in)
            want[wanted++] = (sorted_key){slot->entity, at};
    }
    qsort(want, wanted, sizeof *want, sorted_key_compare);
    uint32_t count = 0;
    const uint32_t *slots = NULL;
    const tide_entity *keys = arches ? tide_entities_ordered_archetypes_slots(t, arches, n, &count, &slots)
                                     : tide_entities_ordered(t, &count);
    bool same = count == wanted;
    for (uint32_t i = 0; i < wanted && same; i++)
        same = tide_entity_equal(keys[i], want[i].key) && (!arches || slots[i] == want[i].slot);
    free(want);
    return same;
}

static uint32_t order_roll(uint32_t *state)
{
    *state ^= *state << 13;
    *state ^= *state >> 17;
    *state ^= *state << 5;
    return *state;
}

// An order asked for after entities came, went or changed archetype is the one before with those taken in. Through
// changes picked at random, each order is what sorting the slots gives: asked for every round or only now and then,
// in the table and in copies of it made between a change and the next asking, across settles, with keys that go
// and come back in another slot before anything asks, and past more changes than are kept.
TIDE_TEST(entity_orders_take_in_what_came_and_went)
{
    tide_entities t = {0}, copy = {0};
    enum { MOST = 600 };
    static tide_entity alive[MOST], away[MOST];
    uint32_t alive_count = 0, away_count = 0, made = 0, state = 0x9e3779b9u;
    const uint32_t ones[] = {1}, evens[] = {0, 2}, rare[] = {3};
    for (uint32_t round = 0; round < 400; round++)
    {
        const uint32_t changes = round % 37u == 36u ? 150u : order_roll(&state) % 12u;
        for (uint32_t c = 0; c < changes; c++)
        {
            const uint32_t what = order_roll(&state) % 8u;
            if ((what < 3u || alive_count < 20u) && alive_count < MOST)
            {
                // Born out of order: an older clock than ones there are, or a newer
                const tide_entity key = {1u + order_roll(&state) % 9u, ((uint64_t)order_roll(&state) << 20) | ++made};
                TIDE_REQUIRE(tide_entity_install(&t, key));
                tide_entity_set_location(&t, key, (tide_location){order_roll(&state) % 4u, made});
                alive[alive_count++] = key;
            }
            else if (what < 5u)
            {
                const uint32_t at = order_roll(&state) % alive_count;
                TIDE_REQUIRE(tide_entity_destroy(&t, alive[at]));
                alive[at] = alive[--alive_count];
            }
            else if (what == 5u)
            {
                const uint32_t at = order_roll(&state) % alive_count;
                tide_entity_set_location(&t, alive[at], (tide_location){order_roll(&state) % 4u, c});
            }
            else if (what == 6u)
            {
                // Gone leaving nothing and installed again, in whichever slot is free then
                const uint32_t at = order_roll(&state) % alive_count;
                const tide_entity key = alive[at];
                TIDE_REQUIRE(tide_entity_erase(&t, key));
                if (order_roll(&state) % 2u && alive_count < MOST)
                {
                    const tide_entity other = {2u, ((uint64_t)order_roll(&state) << 20) | ++made};
                    TIDE_REQUIRE(tide_entity_install(&t, other));
                    tide_entity_set_location(&t, other, (tide_location){1u, made});
                    alive[alive_count++] = other;
                }
                TIDE_REQUIRE(tide_entity_install(&t, key));
                tide_entity_set_location(&t, key, (tide_location){order_roll(&state) % 4u, c});
            }
            else if (away_count && alive_count < MOST && order_roll(&state) % 2u)
            {
                const tide_entity key = away[--away_count];
                TIDE_REQUIRE(tide_entity_install(&t, key));
                tide_entity_set_location(&t, key, (tide_location){order_roll(&state) % 4u, c});
                alive[alive_count++] = key;
            }
            else if (away_count < MOST)
            {
                const uint32_t at = order_roll(&state) % alive_count;
                tide_entity_forget(&t, alive[at], true);
                away[away_count++] = alive[at];
                alive[at] = alive[--alive_count];
            }
        }
        if (round % 5u == 2u)
        {
            // A copy made before its source's orders took the changes in takes them in itself
            tide_entities_copy(&copy, &t);
            TIDE_CHECK(order_is_the_slots(&copy, ones, 1));
            TIDE_CHECK(order_is_the_slots(&copy, NULL, 0));
            if (alive_count)
            {
                TIDE_REQUIRE(tide_entity_destroy(&copy, alive[0]));
                TIDE_CHECK(order_is_the_slots(&copy, evens, 2));
                TIDE_CHECK(order_is_the_slots(&copy, ones, 1));
            }
            tide_entities_settle(&copy);
        }
        TIDE_CHECK(order_is_the_slots(&t, ones, 1));
        if (round % 3u == 0u)
            TIDE_CHECK(order_is_the_slots(&t, evens, 2));
        if (round % 4u == 1u)
            TIDE_CHECK(order_is_the_slots(&t, NULL, 0));
        if (round % 50u == 49u)
            TIDE_CHECK(order_is_the_slots(&t, rare, 1)); // Let go while nobody asked, and made again
        if (round % 2u)
            tide_entities_settle(&t);
    }
    TIDE_CHECK(order_is_the_slots(&t, evens, 2) && order_is_the_slots(&t, NULL, 0));

    // Changes that leave a subset as it was leave its arrays: nothing is made
    tide_entities_settle(&t);
    uint32_t count = 0, again = 0;
    const tide_entity *before = tide_entities_ordered_archetypes(&t, ones, 1, &count);
    const tide_entity *whole = tide_entities_ordered(&t, &again);
    const tide_entity passing = {3u, 12345u};
    TIDE_REQUIRE(tide_entity_install(&t, passing));
    tide_entity_set_location(&t, passing, (tide_location){2u, 1u});
    TIDE_CHECK(tide_entities_ordered_archetypes(&t, ones, 1, &again) == before && again == count);
    TIDE_CHECK(tide_entities_ordered(&t, &again) != whole);
    whole = tide_entities_ordered(&t, &again);
    tide_entity_set_location(&t, passing, (tide_location){1u, 1u});
    TIDE_CHECK(tide_entity_destroy(&t, passing)); // In and out again before anything asked
    TIDE_CHECK(tide_entities_ordered_archetypes(&t, ones, 1, &again) == before && again == count);
    TIDE_CHECK(tide_entities_ordered(&t, &again) != whole && order_is_the_slots(&t, NULL, 0));

    // More changes than are kept: every order is made from the slots, and is right
    for (uint32_t i = 0; i < 70000u; i++)
        tide_entity_set_location(&t, alive[i % alive_count], (tide_location){i % 4u, i});
    TIDE_CHECK(order_is_the_slots(&t, ones, 1) && order_is_the_slots(&t, evens, 2) && order_is_the_slots(&t, NULL, 0));
    tide_entities_settle(&t);
    TIDE_CHECK(order_is_the_slots(&t, ones, 1));
    tide_entities_free(&copy);
    tide_entities_free(&t);
}

// Where an entity is by going through every slot: what a lookup has to say
static uint32_t slot_by_scan(const tide_entities *t, const tide_entity e)
{
    for (uint32_t i = 0; i < t->next_unused; i++)
    {
        if (tide_entity_equal(tide_entity_in_slot(t, i), e))
            return i;
    }
    return UINT32_MAX;
}

// A lookup first tries the slot this thread last found one in and the one a
// stride on (tide_entity_slot_index), in whichever table it's asked of. Asked
// in every order of two tables that share some slots and differ in others, it
// gives each key's own slot or none, never a slot another entity has.
TIDE_TEST(entity_slot_guesses_give_only_the_keys_own_slot)
{
    tide_entities a = {0}, b = {0};
    tide_entity keys[96];
    for (uint32_t i = 0; i < 64; i++)
        keys[i] = tide_entity_create_key(&a, (tide_entity){1, 9000u - i * 13u});
    tide_entities_copy(&b, &a);
    // The copy loses every third and takes others into their slots; the first loses a few of its own
    for (uint32_t i = 0; i < 64; i += 3)
        TIDE_CHECK(tide_entity_destroy(&b, keys[i]));
    for (uint32_t i = 64; i < 96; i++)
        keys[i] = tide_entity_create_key(&b, (tide_entity){2, 100u + i});
    for (uint32_t i = 5; i < 64; i += 11)
        TIDE_CHECK(tide_entity_destroy(&a, keys[i]));
    uint32_t wrong = 0, found = 0;
    for (uint32_t pass = 0; pass < 6; pass++)
    {
        const uint32_t step = pass == 2 ? 2u : pass == 3 ? 7u : pass == 4 ? 37u : 1u;
        for (uint32_t n = 0; n < 96; n++)
        {
            const uint32_t i = pass == 1 ? 95u - n : n * step % 96u;
            const tide_entities *first = (n + pass) % 2u ? &a : &b, *second = first == &a ? &b : &a;
            const uint32_t one = tide_entity_slot_index(first, keys[i]), two = tide_entity_slot_index(second, keys[i]);
            wrong += one != slot_by_scan(first, keys[i]) || two != slot_by_scan(second, keys[i]);
            wrong += tide_entity_slot_index(first, keys[i]) != one; // Asked again, from its own slot
            found += one != UINT32_MAX;
        }
    }
    TIDE_CHECK(wrong == 0u);
    TIDE_CHECK(found > 96u);
    TIDE_CHECK(tide_entity_slot_index(&a, (tide_entity){7, 7}) == UINT32_MAX);
    TIDE_CHECK(tide_entity_slot_index(&a, (tide_entity){0, 0}) == UINT32_MAX);
    tide_entities empty = {0};
    TIDE_CHECK(tide_entity_slot_index(&empty, keys[1]) == UINT32_MAX);
    tide_entities_settle(&a);
    tide_entities_settle(&b);
    tide_entities_free(&b);
    tide_entities_free(&a);
}

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef struct entity_order_reader { tide_entities *entities; uint32_t failed; } entity_order_reader;
static DWORD WINAPI read_entity_order(void *context)
{
    entity_order_reader *reader = context;
    for (uint32_t run = 0; run < 500; run++) {
        uint32_t count;
        const uint32_t archetypes[] = {1};
        const tide_entity *keys = run % 2u ? tide_entities_ordered(reader->entities, &count)
            : tide_entities_ordered_archetypes(reader->entities, archetypes, 1, &count);
        for (uint32_t i = 0; i < count; i++) {
            if (tide_entity_is_null(keys[i]) || (i && tide_entity_compare(keys[i - 1], keys[i]) >= 0))
                __atomic_store_n(&reader->failed, 1u, __ATOMIC_RELAXED);
        }
    }
    return 0;
}
TIDE_TEST(entity_ordered_cache_publishes_safely_to_concurrent_readers)
{
    tide_entities t = {0};
    tide_entity live[32];
    for (uint32_t i = 0; i < 32; i++) {
        live[i] = tide_entity_create(&t);
        tide_entity_set_location(&t, live[i], (tide_location){1, i});
    }
    entity_order_reader reader = {&t, 0};
    HANDLE threads[4];
    for (uint32_t i = 0; i < 4; i++) { threads[i] = CreateThread(NULL, 0, read_entity_order, &reader, 0, NULL); TIDE_REQUIRE(threads[i] != NULL); }
    for (uint32_t run = 0; run < 500; run++) {
        const uint32_t i = run % 32u;
        TIDE_REQUIRE(tide_entity_destroy(&t, live[i]));
        live[i] = tide_entity_create(&t);
        tide_entity_set_location(&t, live[i], (tide_location){run % 2u, i});
    }
    for (uint32_t i = 0; i < 4; i++) { TIDE_CHECK(WaitForSingleObject(threads[i], INFINITE) == WAIT_OBJECT_0); CloseHandle(threads[i]); }
    TIDE_CHECK(__atomic_load_n(&reader.failed, __ATOMIC_RELAXED) == 0);
    tide_entities_settle(&t);
    tide_entities_free(&t);
}
#endif


// Lines written (tide/page.h): a page of slots says which of its slots were written between the hash it had before and
// the one it has, as each change writes its slot alone (a projection's check of its rows' snaps goes by it)
#define SLOT_PAGE ((1u << TIDE_ENTITY_PAGE_SHIFT) * (uint32_t)sizeof(tide_entity_slot))
#define SLOT_PAGES_KEPT 8u

typedef struct kept_slots {
    uint64_t hash;
    uint32_t used;
    uint8_t bytes[SLOT_PAGE];
} kept_slots;

static uint32_t slot_used_on(const tide_entities *t, const uint32_t p)
{
    const uint32_t left = t->next_unused - (p << TIDE_ENTITY_PAGE_SHIFT);
    return (left < (1u << TIDE_ENTITY_PAGE_SHIFT) ? left : 1u << TIDE_ENTITY_PAGE_SHIFT) * (uint32_t)sizeof(tide_entity_slot);
}

TIDE_TEST(entity_pages_say_which_slots_were_written)
{
    kept_slots *kept = calloc(SLOT_PAGES_KEPT, sizeof *kept);
    TIDE_REQUIRE(kept != NULL);
    tide_entities t = {0}, snapshot = {0}, old = {0};
    static tide_entity alive[6000], old_alive[6000];
    uint32_t count = 0, old_count = 0, state = 99u, told = 0;
    bool held = true;
    for (uint32_t tick = 0; tick < 300u; tick++) {
        for (uint32_t op = 0; op < 40u; op++) {
            state = state * 1664525u + 1013904223u;
            const uint32_t roll = state >> 8, what = roll % 9u;
            if ((what < 4u || !count) && count < 6000u) {
                alive[count++] = tide_entity_create(&t);
            } else if (what == 4u) {
                const uint32_t i = (roll >> 4) % count;
                TIDE_CHECK(tide_entity_destroy(&t, alive[i]));
                alive[i] = alive[--count];
            } else if (what == 5u) {
                tide_entity_snap(&t, alive[(roll >> 4) % count]);
            } else if (what == 6u) {
                const uint32_t i = (roll >> 4) % count;
                TIDE_CHECK(tide_entity_erase(&t, alive[i]));
                alive[i] = alive[--count];
            } else {
                tide_entity_set_location(&t, alive[(roll >> 4) % count], (tide_location){roll % 5u, roll >> 12});
            }
        }
        if (tick % 3u == 0u) tide_entities_copy(&snapshot, &t); // (Before the hash: its pages are copied written)
        (void)tide_entities_hash(0, &t);
        if (tick % 3u != 0u) tide_entities_copy(&snapshot, &t);
        // Every page that hashes to what was kept of it, now or before, is those bytes in the lines it doesn't say
        for (uint32_t p = 0; p < t.pages && p < SLOT_PAGES_KEPT; p++) {
            const tide_page *page = t.page[p];
            if (!page || !kept[p].hash) continue;
            uint64_t wrote;
            if (page->hash == kept[p].hash) wrote = 0;
            else if (page->before == kept[p].hash) wrote = page->wrote;
            else continue;
            if (wrote && wrote != TIDE_PAGE_ALL_LINES) told++;
            for (uint32_t line = 0; line < 64u; line++) {
                const uint32_t from = line * (SLOT_PAGE / 64u), to = from + SLOT_PAGE / 64u < kept[p].used ? from + SLOT_PAGE / 64u : kept[p].used;
                if (wrote >> line & 1u || from >= to) continue;
                if (memcmp((const uint8_t *)tide_page_data(page) + from, kept[p].bytes + from, to - from) != 0) held = false;
            }
        }
        TIDE_REQUIRE(held);
        // Now and then a world of ticks before (a rollback), whose pages are older ones
        if (tick % 40u == 10u) {
            tide_entities_free(&old);
            tide_entities_copy(&old, &t);
            memcpy(old_alive, alive, count * sizeof *alive);
            old_count = count;
        } else if (tick % 40u == 30u) {
            tide_entities_free(&t);
            tide_entities_copy(&t, &old);
            memcpy(alive, old_alive, old_count * sizeof *alive);
            count = old_count;
            (void)tide_entities_hash(0, &t);
        }
        for (uint32_t p = 0; p < SLOT_PAGES_KEPT; p++) {
            const tide_page *page = p < t.pages ? t.page[p] : NULL;
            kept[p].hash = page ? page->hash : 0u;
            if (!page) continue;
            kept[p].used = slot_used_on(&t, p);
            memcpy(kept[p].bytes, tide_page_data(page), SLOT_PAGE);
        }
    }
    TIDE_CHECK(told > 300u);
    tide_entities_free(&t);
    tide_entities_free(&snapshot);
    tide_entities_free(&old);
    free(kept);
}
