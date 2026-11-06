#include <stdlib.h>
#include <string.h>

#include "tide/entity.h"
#include "tide_test.h"

static tide_entities projected_entities(const tide_entity e)
{
    tide_entities t = {0};
    tide_entity_install(&t, e);
    tide_entity_set_location(&t, e, (tide_location){3, 7});
    tide_entity_set_snaps(&t, e, 2);
    return t;
}

TIDE_TEST(entity_projection_reserves_only_known_pages)
{
    const tide_entity e = {(1u << 20) + 123u, 7};
    tide_entities t = projected_entities(e);
    TIDE_CHECK(tide_entity_alive(&t, e));
    TIDE_CHECK(!tide_entity_alive(&t, (tide_entity){1, 1}));
    TIDE_CHECK(tide_entity_is_null(tide_entity_in_slot(&t, 1)));
    TIDE_CHECK(tide_entity_location(&t, e).row == 7);
    uint32_t populated = 0;
    for (uint32_t p = 0; p < t.pages; p++) populated += t.page[p] != NULL;
    TIDE_CHECK(populated == 1);

    tide_entities snapshot = {0};
    tide_entities_copy(&snapshot, &t);
    tide_entity_set_born(&t, e, 12345);
    tide_entity_snap(&t, e);
    TIDE_CHECK(tide_entity_snaps(&snapshot, e) == 2);
    TIDE_CHECK(tide_entity_snaps(&t, e) == 3);
    TIDE_CHECK(tide_entity_born(&t, e) == 12345);
    const tide_entity predicted = tide_entity_create(&t);
    TIDE_CHECK(tide_entity_slot_index(&t, predicted) == 1);
    TIDE_CHECK(!tide_entity_alive(&snapshot, predicted));
    TIDE_CHECK(tide_entity_destroy(&t, e));
    TIDE_CHECK(tide_entity_alive(&snapshot, e));
    TIDE_CHECK(tide_entity_slot_index(&t, tide_entity_create(&t)) == 0);
    tide_entities_free(&snapshot);
    tide_entities_free(&t);
}

TIDE_TEST(entity_projection_roundtrips_sparse_pages_and_deltas)
{
    const tide_entity e = {(1u << 20) + 123u, 7};
    tide_entities t = projected_entities(e);
    const uint64_t expected = tide_entities_hash(123, &t);
    const uint32_t size = tide_entities_packed_size(&t);
    TIDE_CHECK(size < 4096);
    uint8_t *bytes = tide_alloc(size);
    tide_writer out = {bytes, size, 0, false, false};
    tide_entities_pack(&t, &out);
    TIDE_REQUIRE(!out.overflow && out.size == size);
    tide_entities loaded = {0};
    tide_reader in = {bytes, size, 0, false};
    TIDE_REQUIRE(tide_entities_unpack(&loaded, &in));
    TIDE_CHECK(in.at == size && tide_entities_hash(123, &loaded) == expected);
    TIDE_CHECK(loaded.pages == 1 && loaded.next_unused == 1);

    const uint32_t cuts[] = {0, 4, 8, 12, 16, 20, size - 1u};
    for (uint32_t i = 0; i < sizeof cuts / sizeof cuts[0]; i++) {
        tide_entities broken = {0};
        tide_reader cut = {bytes, cuts[i], 0, false};
        TIDE_CHECK(!tide_entities_unpack(&broken, &cut));
        tide_entities_free(&broken);
    }

    tide_entity_snap(&t, e);
    tide_delta_writer delta;
    tide_delta_begin(&delta, true, NULL, 0, 91);
    tide_entities_pack_delta(&t, &loaded, &delta);
    uint32_t delta_size;
    uint8_t *packet = tide_delta_end(&delta, &delta_size);
    TIDE_CHECK(delta_size < 256);
    tide_delta_reader reader;
    uint64_t hash = 0;
    tide_entities repaired = {0};
    TIDE_REQUIRE(tide_delta_open(&reader, packet, delta_size, true, &hash));
    TIDE_REQUIRE(tide_entities_unpack_delta(&repaired, &loaded, &reader));
    TIDE_CHECK(hash == 91 && !reader.bytes.failed && reader.bytes.at == delta_size);
    TIDE_CHECK(tide_entity_snaps(&repaired, e) == 3);
    TIDE_CHECK(tide_entities_hash(123, &repaired) == tide_entities_hash(123, &t));
    free(packet);

    tide_writer hashes = {.grows = true};
    tide_entities_hash_pages(&t, &hashes);
    TIDE_CHECK(hashes.size < 32);
    uint8_t needed[16];
    tide_needs needs;
    tide_needs_begin(&needs, hashes.data, hashes.size, needed, sizeof needed);
    tide_entities_need_pages(&loaded, &needs);
    const uint32_t need_size = tide_needs_end(&needs);
    tide_delta_begin(&delta, false, needed, need_size, 92);
    tide_entities_pack_delta(&t, NULL, &delta);
    packet = tide_delta_end(&delta, &delta_size);
    tide_entities by_need = {0};
    TIDE_REQUIRE(tide_delta_open(&reader, packet, delta_size, true, &hash));
    TIDE_REQUIRE(tide_entities_unpack_delta(&by_need, &loaded, &reader));
    TIDE_CHECK(tide_entity_snaps(&by_need, e) == 3);
    TIDE_CHECK(tide_entities_hash(123, &by_need) == tide_entities_hash(123, &t));
    free(hashes.data);
    free(packet);
    tide_entities_free(&by_need);
    free(bytes);
    tide_entities_free(&repaired);
    tide_entities_free(&loaded);
    tide_entities_free(&t);
}

TIDE_TEST(entity_clock_registry_delta_preserves_large_seeded_baselines)
{
    tide_entities base = {0}, current = {0}, loaded = {0};
    for (uint32_t i = 0; i < 20000; i++) tide_entity_create_key(&base, (tide_entity){1, 2u + i * 2u});
    tide_writer hashes = {.grows = true};
    tide_entities_hash_pages(&base, &hashes);
    uint8_t needs_bytes[256];
    tide_needs needs;
    tide_needs_begin(&needs, hashes.data, hashes.size, needs_bytes, sizeof needs_bytes);
    tide_entities_need_pages(&base, &needs);
    const uint32_t need_size = tide_needs_end(&needs);
    tide_delta_writer by_need;
    tide_delta_begin(&by_need, false, needs_bytes, need_size, 36);
    tide_entities_pack_delta(&base, NULL, &by_need);
    uint32_t need_packet_size;
    uint8_t *need_packet = tide_delta_end(&by_need, &need_packet_size);
    TIDE_REQUIRE(need_packet_size < 256); // The receiver already has all 320KB of keys.
    tide_delta_reader need_reader;
    uint64_t need_hash;
    TIDE_REQUIRE(tide_delta_open(&need_reader, need_packet, need_packet_size, true, &need_hash));
    TIDE_REQUIRE(tide_entities_unpack_delta(&loaded, &base, &need_reader));
    TIDE_CHECK(tide_entities_hash(5, &loaded) == tide_entities_hash(5, &base));
    const uint64_t before = tide_entities_hash(5, &loaded);
    TIDE_REQUIRE(tide_delta_open(&need_reader, need_packet, need_packet_size - 1u, true, &need_hash));
    TIDE_CHECK(!tide_entities_unpack_delta(&loaded, &base, &need_reader));
    TIDE_CHECK(tide_entities_hash(5, &loaded) == before);
    free(need_packet);
    free(hashes.data);
    tide_entities_free(&loaded);
    tide_entities_copy(&current, &base);
    // A sorted-prefix insertion used to retransmit all 320KB of registry keys.
    tide_entity_create_key(&current, (tide_entity){1, 1});
    tide_delta_writer writer;
    tide_delta_begin(&writer, true, NULL, 0, 37);
    tide_entities_pack_delta(&current, &base, &writer);
    uint32_t size;
    uint8_t *bytes = tide_delta_end(&writer, &size);
    TIDE_REQUIRE(size < 1024);
    tide_delta_reader reader;
    uint64_t hash;
    TIDE_REQUIRE(tide_delta_open(&reader, bytes, size, true, &hash));
    TIDE_REQUIRE(tide_entities_unpack_delta(&loaded, &base, &reader));
    TIDE_CHECK(hash == 37 && tide_entities_hash(5, &loaded) == tide_entities_hash(5, &current));
    const uint32_t cuts[] = {0, 3, 8, size - 1u};
    for (uint32_t i = 0; i < sizeof cuts / sizeof *cuts; i++) {
        tide_entities broken = {0};
        if (tide_delta_open(&reader, bytes, cuts[i], true, &hash))
            TIDE_CHECK(!tide_entities_unpack_delta(&broken, &base, &reader));
        tide_entities_free(&broken);
    }
    free(bytes);
    tide_entities_free(&loaded);
    tide_entities_free(&current);
    // An independently constructed same-clock fork removes one key and adds
    // another; edits must preserve the exact set, not merely append a suffix.
    for (uint32_t i = 0; i < 20000; i++)
        tide_entity_create_key(&current, (tide_entity){1, i == 1u ? 3u : 2u + i * 2u});
    tide_delta_begin(&writer, true, NULL, 0, 38);
    tide_entities_pack_delta(&current, &base, &writer);
    bytes = tide_delta_end(&writer, &size);
    TIDE_REQUIRE(size < 1024);
    TIDE_REQUIRE(tide_delta_open(&reader, bytes, size, true, &hash));
    TIDE_REQUIRE(tide_entities_unpack_delta(&loaded, &base, &reader));
    TIDE_CHECK(tide_entities_hash(5, &loaded) == tide_entities_hash(5, &current));
    tide_entities wrong_clock = {0};
    tide_entity_create_key(&wrong_clock, (tide_entity){2, 1});
    tide_entities rejected = {0};
    TIDE_REQUIRE(tide_delta_open(&reader, bytes, size, true, &hash));
    TIDE_CHECK(!tide_entities_unpack_delta(&rejected, &wrong_clock, &reader));
    tide_entities_free(&rejected);
    tide_entities_free(&wrong_clock);
    free(bytes);
    tide_entities_free(&loaded);
    tide_entities_free(&current);
    tide_entities_free(&base);
}

TIDE_TEST(entity_clock_registry_regions_reject_cross_boundary_duplicates_and_clocks)
{
    tide_entities target = {0};
    tide_entity_create_key(&target, (tide_entity){7, 8});
    const uint64_t before = tide_entities_hash(13, &target);
    tide_entity first[1024];
    for (uint32_t i = 0; i < 1024; i++) first[i] = (tide_entity){1, i + 1u};
    const tide_entity endings[] = {{1, 1024}, {1, 0}, {2, 1025}};
    for (uint32_t attempt = 0; attempt < sizeof endings / sizeof *endings; attempt++) {
        tide_delta_writer writer;
        tide_delta_begin(&writer, false, NULL, 0, 9);
        tide_delta_number(&writer, 0); // No physical slots; all births already destroyed.
        tide_delta_number(&writer, 0);
        tide_delta_number64(&writer, 1);
        tide_delta_number64(&writer, 0);
        tide_delta_number(&writer, 1025);
        tide_delta_number(&writer, 2); // Registry hash/need regions.
        tide_delta_region(&writer, first, sizeof first, NULL, 0, false, 16);
        tide_delta_region(&writer, &endings[attempt], sizeof(tide_entity), NULL, 0, false, 16);
        tide_delta_close(&writer);
        tide_delta_close(&writer);
        uint32_t size;
        uint8_t *bytes = tide_delta_end(&writer, &size);
        tide_delta_reader reader;
        uint64_t hash;
        TIDE_REQUIRE(tide_delta_open(&reader, bytes, size, false, &hash));
        TIDE_CHECK(!tide_entities_unpack_delta(&target, NULL, &reader));
        TIDE_CHECK(tide_entities_hash(13, &target) == before);
        free(bytes);
    }
    tide_entities_free(&target);
}
