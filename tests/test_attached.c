#include <stdlib.h>
#include <string.h>

#include "tide/attached.h"
#include "tide_test.h"

// What's kept for another world's entities: the entity, an int and a float,
// and what the entity was born as.
typedef struct kept {
    int32_t baked;
    float timer;
} kept;

static const uint32_t sizes[] = {sizeof(tide_entity), sizeof(kept), sizeof(uint32_t)};
static const tide_columns columns = {3, 4, sizes};

static kept *value(tide_attached *a, const tide_entity e)
{
    const uint32_t row = tide_attached_find(a, &columns, e);
    return row == TIDE_ATTACHED_NONE ? NULL : tide_attached_cell(a, &columns, row);
}

static void keep(tide_attached *a, const tide_entity e, const int32_t baked)
{
    kept *v = tide_attached_cell(a, &columns, tide_attached_add(a, &columns, e, 0u));
    v->baked = baked;
}

// A row remembers what its entity was born as when it was made, through the
// rows that move as others go, and as bytes.
TIDE_TEST(attached_rows_remember_what_their_entity_was_born_as)
{
    tide_attached a = {0};
    for (uint32_t i = 0; i < 30; i++) tide_attached_add(&a, &columns, (tide_entity){i, 1}, i % 5u ? 1000u + i : 0u);
    for (uint32_t i = 0; i < 30; i += 4) tide_attached_remove(&a, &columns, tide_attached_find(&a, &columns, (tide_entity){i, 1}));
    for (uint32_t i = 0; i < 30; i++) {
        const uint32_t row = tide_attached_find(&a, &columns, (tide_entity){i, 1});
        if (i % 4 == 0) {
            TIDE_CHECK(row == TIDE_ATTACHED_NONE);
            continue;
        }
        TIDE_REQUIRE(row != TIDE_ATTACHED_NONE);
        TIDE_CHECK(tide_attached_born(&a, &columns, row) == (i % 5u ? 1000u + i : 0u)); // 0: it wasn't known
    }

    const uint32_t size = tide_attached_packed_size(&a, &columns);
    uint8_t *bytes = malloc(size);
    tide_writer w = {bytes, size, 0, false, false};
    tide_attached_pack(&a, &columns, &w);
    tide_attached b = {0};
    tide_reader r = {bytes, size, 0, false};
    TIDE_REQUIRE(tide_attached_unpack(&b, &columns, &r));
    TIDE_CHECK(tide_attached_born(&b, &columns, tide_attached_find(&b, &columns, (tide_entity){7, 1})) == 1007u);
    TIDE_CHECK(tide_attached_born(&b, &columns, tide_attached_find(&b, &columns, (tide_entity){5, 1})) == 0u);
    free(bytes);
    tide_attached_free(&a, &columns);
    tide_attached_free(&b, &columns);
}

TIDE_TEST(attached_finds_rows_by_entity)
{
    tide_attached a = {0};
    TIDE_CHECK(tide_attached_find(&a, &columns, (tide_entity){3, 1}) == TIDE_ATTACHED_NONE);
    for (uint32_t i = 1; i <= 100; i++) keep(&a, (tide_entity){i * 7u, 1}, (int32_t)i);
    TIDE_CHECK(a.rows.count == 100);
    for (uint32_t i = 1; i <= 100; i++) {
        const kept *v = value(&a, (tide_entity){i * 7u, 1});
        TIDE_REQUIRE(v);
        TIDE_CHECK(v->baked == (int32_t)i && v->timer == 0.0f);
    }
    TIDE_CHECK(!value(&a, (tide_entity){8, 1}));   // Nothing kept for it
    TIDE_CHECK(!value(&a, (tide_entity){7, 3}));   // Another entity in its slot
    TIDE_CHECK(!value(&a, (tide_entity){9000, 1})); // Past every index it has
    TIDE_CHECK(!value(&a, (tide_entity){0, 0}));
    tide_attached_free(&a, &columns);
    TIDE_CHECK(a.rows.count == 0 && !a.by_index && !a.others);
}

TIDE_TEST(attached_remove_moves_the_last_row_in)
{
    tide_attached a = {0};
    for (uint32_t i = 0; i < 40; i++) keep(&a, (tide_entity){i, 1}, (int32_t)i);
    // From the middle, the end and the start, until none is left
    for (uint32_t i = 0; i < 40; i += 3) {
        tide_attached_remove(&a, &columns, tide_attached_find(&a, &columns, (tide_entity){i, 1}));
    }
    for (uint32_t i = 0; i < 40; i++) {
        const kept *v = value(&a, (tide_entity){i, 1});
        if (i % 3 == 0) {
            TIDE_CHECK(!v);
        } else {
            TIDE_REQUIRE(v);
            TIDE_CHECK(v->baked == (int32_t)i);
        }
    }
    TIDE_CHECK(a.rows.count == 26);
    while (a.rows.count) tide_attached_remove(&a, &columns, a.rows.count / 2u);
    for (uint32_t i = 0; i < 40; i++) TIDE_CHECK(!value(&a, (tide_entity){i, 1}));
    keep(&a, (tide_entity){5, 3}, 55); // And it takes rows again
    TIDE_CHECK(value(&a, (tide_entity){5, 3})->baked == 55);
    tide_attached_free(&a, &columns);
}

// The world views draw and the last one the server confirmed can each have
// another entity in one slot: both keep their rows.
TIDE_TEST(attached_keeps_two_generations_of_an_index)
{
    tide_attached a = {0};
    keep(&a, (tide_entity){4, 1}, 10);
    keep(&a, (tide_entity){9, 1}, 90);
    keep(&a, (tide_entity){4, 3}, 30);
    keep(&a, (tide_entity){4, 5}, 50);
    TIDE_CHECK(value(&a, (tide_entity){4, 1})->baked == 10);
    TIDE_CHECK(value(&a, (tide_entity){4, 3})->baked == 30);
    TIDE_CHECK(value(&a, (tide_entity){4, 5})->baked == 50);
    TIDE_CHECK(!value(&a, (tide_entity){4, 7}));

    // The one its index finds goes: another takes its place
    tide_attached_remove(&a, &columns, tide_attached_find(&a, &columns, (tide_entity){4, 1}));
    TIDE_CHECK(!value(&a, (tide_entity){4, 1}));
    TIDE_CHECK(value(&a, (tide_entity){4, 3})->baked == 30);
    TIDE_CHECK(value(&a, (tide_entity){4, 5})->baked == 50);
    TIDE_CHECK(value(&a, (tide_entity){9, 1})->baked == 90);
    TIDE_CHECK(a.other_count == 1);

    // One of the others goes, then a row before them, which moves them
    tide_attached_remove(&a, &columns, tide_attached_find(&a, &columns, (tide_entity){4, 3}));
    TIDE_CHECK(!value(&a, (tide_entity){4, 3}) && a.other_count == 0);
    keep(&a, (tide_entity){4, 7}, 70);
    tide_attached_remove(&a, &columns, tide_attached_find(&a, &columns, (tide_entity){9, 1}));
    TIDE_CHECK(value(&a, (tide_entity){4, 5})->baked == 50);
    TIDE_CHECK(value(&a, (tide_entity){4, 7})->baked == 70);
    TIDE_CHECK(a.rows.count == 2);
    tide_attached_free(&a, &columns);
}

TIDE_TEST(attached_packs_and_unpacks)
{
    tide_attached a = {0};
    for (uint32_t i = 0; i < 50; i++) keep(&a, (tide_entity){i * 3u, 1}, (int32_t)i);
    keep(&a, (tide_entity){6, 3}, 600);
    const uint32_t size = tide_attached_packed_size(&a, &columns);
    uint8_t *bytes = malloc(size);
    tide_writer w = {bytes, size, 0, false, false};
    tide_attached_pack(&a, &columns, &w);
    TIDE_CHECK(w.size == size && !w.overflow);

    tide_attached b = {0};
    tide_reader r = {bytes, size, 0, false};
    TIDE_REQUIRE(tide_attached_unpack(&b, &columns, &r));
    TIDE_CHECK(r.at == size && b.rows.count == 51);
    for (uint32_t i = 0; i < 50; i++) TIDE_CHECK(value(&b, (tide_entity){i * 3u, 1})->baked == (int32_t)i);
    TIDE_CHECK(value(&b, (tide_entity){6, 3})->baked == 600);

    // Two rows of one entity aren't its rows
    memcpy(bytes + 4 + sizeof(tide_entity), bytes + 4, sizeof(tide_entity));
    tide_attached c = {0};
    tide_reader again = {bytes, size, 0, false};
    TIDE_CHECK(!tide_attached_unpack(&c, &columns, &again));
    TIDE_CHECK(c.rows.count == 0);

    free(bytes);
    tide_attached_free(&a, &columns);
    tide_attached_free(&b, &columns);
}
