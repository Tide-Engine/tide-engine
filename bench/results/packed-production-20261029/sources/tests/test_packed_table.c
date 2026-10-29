#include <stdlib.h>
#include <string.h>

#include "tide/delta.h"
#include "tide/table.h"
#include "tide_test.h"

#if TIDE_PACKED_TABLES
// A different data chunk size from the map, and mixed widths, catch accidental
// use of one coordinate space or column width for another.
static const uint32_t packed_sizes[] = {8u, 4u, 4u, 2u, 1u};
static const tide_columns packed_columns = {5u, 4u, packed_sizes};

static void packed_fill(tide_table *table, tide_entities *entities, uint32_t count, uint32_t percent)
{
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t row = tide_table_add(table, &packed_columns);
        const tide_entity entity = tide_entity_create(entities);
        memcpy(tide_table_cell(table, &packed_columns, row, 0u), &entity, sizeof entity);
        const uint32_t lanes[] = {row * 17u, row ^ 0x9e3779b9u};
        tide_table_scatter(table, &packed_columns, row, 1u, 2u, 4u, lanes);
        *(uint16_t *)tide_table_cell(table, &packed_columns, row, 3u) = (uint16_t)(row * 13u);
        *(uint8_t *)tide_table_cell(table, &packed_columns, row, 4u) = row % 100u < percent;
        tide_entity_set_location(entities, entity, (tide_location){0u, row});
    }
}

static void packed_equal(const tide_table *a, const tide_table *b)
{
    TIDE_REQUIRE(a->count == b->count);
    for (uint32_t row = 0; row < a->count; row++) {
        const uint32_t physical = tide_table_physical_row(b, row);
        TIDE_REQUIRE(physical < b->count);
        TIDE_CHECK(tide_table_logical_row(b, physical) == row);
        for (uint32_t k = 0; k < packed_columns.count; k++)
            TIDE_CHECK(memcmp(tide_table_get(a, &packed_columns, row, k), tide_table_get(b, &packed_columns, row, k), packed_sizes[k]) ==
                       0);
        uint32_t av[2], bv[2];
        tide_table_gather(a, &packed_columns, row, 1u, 2u, 4u, av);
        tide_table_gather(b, &packed_columns, row, 1u, 2u, 4u, bv);
        TIDE_CHECK(memcmp(av, bv, sizeof av) == 0);
    }
    TIDE_CHECK(tide_table_hash(42u, a, &packed_columns) == tide_table_hash(42u, b, &packed_columns));
}

static void packed_roundtrip(const tide_table *identity, const tide_table *packed, const tide_table *base)
{
    tide_writer aw = {.grows = true}, bw = {.grows = true};
    tide_table_pack(identity, &packed_columns, &aw);
    tide_table_pack(packed, &packed_columns, &bw);
    TIDE_REQUIRE(aw.size == bw.size);
    TIDE_CHECK(memcmp(aw.data, bw.data, aw.size) == 0);
    tide_table restored = {0};
    tide_reader r = {bw.data, bw.size, 0u, false};
    TIDE_CHECK(tide_table_unpack(&restored, &packed_columns, &r));
    TIDE_CHECK(!r.failed && r.at == r.size && !restored.mapping);
    packed_equal(identity, &restored);
    tide_table_free(&restored, &packed_columns);
    free(aw.data);
    free(bw.data);

    const uint64_t hash = tide_table_hash(42u, identity, &packed_columns);
    tide_delta_writer d;
    tide_delta_begin(&d, base != NULL, NULL, 0u, hash);
    tide_table_pack_delta(packed, base, &packed_columns, &d);
    uint32_t size;
    uint8_t *data = tide_delta_end(&d, &size);
    tide_delta_reader reader;
    uint64_t got_hash;
    TIDE_REQUIRE(tide_delta_open(&reader, data, size, base != NULL, &got_hash));
    TIDE_CHECK(got_hash == hash);
    TIDE_CHECK(tide_table_unpack_delta(&restored, base, &packed_columns, &reader));
    TIDE_CHECK(!reader.bytes.failed && reader.bytes.at == reader.bytes.size && !restored.mapping);
    packed_equal(identity, &restored);
    tide_table_free(&restored, &packed_columns);
    free(data);

    tide_writer ah = {.grows = true}, bh = {.grows = true};
    tide_table_hash_pages(identity, &packed_columns, &ah);
    tide_table_hash_pages(packed, &packed_columns, &bh);
    TIDE_REQUIRE(ah.size == bh.size);
    TIDE_CHECK(memcmp(ah.data, bh.data, ah.size) == 0);
    uint8_t an[1024], bn[1024];
    tide_needs na, nb;
    tide_needs_begin(&na, ah.data, ah.size, an, sizeof an);
    tide_needs_begin(&nb, ah.data, ah.size, bn, sizeof bn);
    tide_table_need_pages(identity, &packed_columns, &na);
    tide_table_need_pages(packed, &packed_columns, &nb);
    const uint32_t as = tide_needs_end(&na), bs = tide_needs_end(&nb);
    TIDE_CHECK(as == bs && memcmp(an, bn, as) == 0);
    free(ah.data);
    free(bh.data);
}

static void packed_partition_check(tide_table *table, bool need)
{
    uint32_t expected = 0u;
    for (uint32_t row = 0; row < table->count; row++)
        expected += (*(const uint8_t *)tide_table_get(table, &packed_columns, row, 4u) != 0u) == need;
    TIDE_CHECK(tide_table_partition(table, &packed_columns, 4u, need) == expected);
    for (uint32_t row = 0; row < table->count; row++) {
        const uint8_t *column = tide_table_column(table, &packed_columns, row >> 4u, 4u);
        TIDE_CHECK(((column[row & 15u] != 0u) == need) == (row < expected));
    }
}

TIDE_TEST(packed_table_lifecycle_and_canonical_streams)
{
    static const uint32_t counts[] = {0u, 1u, 3u, 17u, 2047u, 2048u, 2049u, 4099u};
    static const uint32_t densities[] = {0u, 50u, 95u, 99u, 100u};
    for (uint32_t n = 0; n < sizeof counts / sizeof counts[0]; n++) {
        for (uint32_t f = 0; f < sizeof densities / sizeof densities[0]; f++) {
            tide_table identity = {0}, packed = {0}, snapshot = {0};
            tide_entities entities = {0};
            packed_fill(&identity, &entities, counts[n], densities[f]);
            tide_table_copy(&packed, &identity, &packed_columns);
            TIDE_CHECK(tide_table_partition(&packed, &packed_columns, UINT32_MAX, true) == 0u);
            TIDE_CHECK(tide_table_partition(&packed, &packed_columns, 1u, true) == 0u);
            TIDE_CHECK(!packed.mapping);
            packed_partition_check(&packed, false);
            if (densities[f] == 0u || densities[f] == 100u) TIDE_CHECK(!packed.mapping);
            packed_equal(&identity, &packed);
            packed_roundtrip(&identity, &packed, NULL);
            tide_table_copy(&snapshot, &packed, &packed_columns);
            const uint64_t before = tide_table_hash(42u, &snapshot, &packed_columns);
            // Write through logical gathers and physical lanes, while the saved
            // world and canonical cache retain the original source pages.
            for (uint32_t row = 0; row < packed.count; row += 7u) {
                uint32_t values[2];
                tide_table_gather(&packed, &packed_columns, row, 1u, 2u, 4u, values);
                values[0] += 3u;
                values[1] ^= 42u;
                tide_table_scatter(&identity, &packed_columns, row, 1u, 2u, 4u, values);
                tide_table_scatter(&packed, &packed_columns, row, 1u, 2u, 4u, values);
                const uint32_t physical = tide_table_physical_row(&packed, row);
                uint16_t *lane = tide_table_column_mut(&packed, &packed_columns, physical >> 4u, 3u);
                lane[physical & 15u] ^= 99u;
                *(uint16_t *)tide_table_cell(&identity, &packed_columns, row, 3u) ^= 99u;
            }
            tide_stale_pages stale = {0};
            tide_table_stale(&packed, &packed_columns, &stale);
            for (uint32_t i = 0; i < stale.count; i++) tide_page_hash(stale.pages[i].page, stale.pages[i].bytes);
            free(stale.pages);
            packed_equal(&identity, &packed);
            packed_roundtrip(&identity, &packed, &snapshot);
            TIDE_CHECK(tide_table_hash(42u, &snapshot, &packed_columns) == before);
            for (uint32_t row = 0; row < packed.count; row += 11u) {
                *(uint8_t *)tide_table_cell(&identity, &packed_columns, row, 4u) ^= 1u;
                *(uint8_t *)tide_table_cell(&packed, &packed_columns, row, 4u) ^= 1u;
            }
            packed_partition_check(&packed, true);
            packed_equal(&identity, &packed);
            for (uint32_t i = 0; i < 23u; i++) {
                const uint32_t row = tide_table_add(&identity, &packed_columns);
                TIDE_CHECK(tide_table_add(&packed, &packed_columns) == row);
                const uint32_t value = i + 10000u;
                memcpy(tide_table_cell(&identity, &packed_columns, row, 1u), &value, sizeof value);
                memcpy(tide_table_cell(&packed, &packed_columns, row, 1u), &value, sizeof value);
            }
            packed_equal(&identity, &packed);
            for (uint32_t i = 0; i < 25u && identity.count; i++) {
                const uint32_t row = (i * 419u) % identity.count;
                tide_table_remove_row(&identity, &packed_columns, row);
                tide_table_remove_row(&packed, &packed_columns, row);
                packed_equal(&identity, &packed);
            }
            packed_roundtrip(&identity, &packed, &snapshot);
            TIDE_CHECK(tide_table_hash(42u, &snapshot, &packed_columns) == before);
            tide_table_free(&identity, &packed_columns);
            tide_table_free(&packed, &packed_columns);
            tide_table_free(&snapshot, &packed_columns);
            tide_entities_free(&entities);
        }
    }
}

TIDE_TEST(packed_table_moves_preserve_logical_entity_locations)
{
    tide_table from = {0}, to = {0};
    tide_entities entities = {0};
    packed_fill(&from, &entities, 4099u, 95u);
    packed_partition_check(&from, false);
    const int32_t columns[] = {0, 1, 2, 3, 4};
    for (uint32_t i = 0; i < 70u; i++) {
        const uint32_t row = i * 41u % from.count;
        tide_entity entity;
        memcpy(&entity, tide_table_get(&from, &packed_columns, row, 0u), sizeof entity);
        const uint32_t value = *(const uint32_t *)tide_table_get(&from, &packed_columns, row, 1u);
        const uint32_t moved = tide_table_move(&from, &packed_columns, row, 0u, &to, &packed_columns, 1u, columns, &entities);
        TIDE_CHECK(tide_entity_location(&entities, entity).row == moved);
        TIDE_CHECK(tide_entity_location(&entities, entity).archetype == 1u);
        TIDE_CHECK(*(const uint32_t *)tide_table_get(&to, &packed_columns, moved, 1u) == value);
        packed_partition_check(&to, true);
        for (uint32_t j = 0; j < from.count; j++) {
            memcpy(&entity, tide_table_get(&from, &packed_columns, j, 0u), sizeof entity);
            TIDE_CHECK(tide_entity_location(&entities, entity).row == j);
        }
    }
    // Remove the last mapped rows, hash/cache the empty table, and reuse it.
    // A zero-row source has no physical page but can still own map metadata.
    while (to.count) tide_table_remove_row(&to, &packed_columns, to.count / 2u);
    tide_table empty = {0};
    packed_equal(&empty, &to);
    packed_roundtrip(&empty, &to, NULL);
    TIDE_CHECK(tide_table_partition(&to, &packed_columns, 4u, true) == 0u);
    const tide_columns no_columns = {0u, 4u, NULL};
    TIDE_CHECK(tide_table_partition(&empty, &no_columns, 0u, true) == 0u);
    packed_fill(&to, &entities, 19u, 50u);
    TIDE_CHECK(to.count == 19u);
    for (uint32_t row = 0; row < to.count; row++) {
        TIDE_CHECK(tide_table_physical_row(&to, row) == row);
        TIDE_CHECK(*(const uint32_t *)tide_table_get(&to, &packed_columns, row, 1u) == row * 17u);
    }
    packed_partition_check(&to, false);
    tide_table_free(&from, &packed_columns);
    tide_table_free(&to, &packed_columns);
    tide_entities_free(&entities);
}
#endif
