#include <stdlib.h>
#include <string.h>

#include "tide/entity.h"
#include "tide_test.h"

static tide_entities projected_entities(const tide_entity e)
{
    tide_entities t = {0};
    const uint32_t page = e.index >> TIDE_ENTITY_PAGE_SHIFT;
    t.next_unused = e.index + 1u;
    t.pages = t.room = page + 1u;
    tide_page **allocation = tide_alloc_zeroed((size_t)t.room * 2u + 1u, sizeof *allocation);
    t.page = allocation + 1;
    t.page[page] = tide_page_new((1u << TIDE_ENTITY_PAGE_SHIFT) * sizeof(tide_entity_slot));
    tide_entity_slot *slots = tide_page_data(t.page[page]);
    slots[e.index & ((1u << TIDE_ENTITY_PAGE_SHIFT) - 1u)] = (tide_entity_slot){e.generation, 3, 7, 2};
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
    TIDE_CHECK(predicted.index == e.index + 1u);
    TIDE_CHECK(!tide_entity_alive(&snapshot, predicted));
    TIDE_CHECK(tide_entity_destroy(&t, e));
    TIDE_CHECK(tide_entity_alive(&snapshot, e));
    TIDE_CHECK(tide_entity_create(&t).index == e.index);
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
    TIDE_CHECK(loaded.page[0] == NULL);

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
