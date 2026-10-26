#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "tide/rows.h"
#include "tide_test.h"

// What a system written in C is handed (tide/rows.h), without the compiler: an
// archetype of entities with a place, a tag of plain values and a narrow
// component, in chunks of 16 rows, as generated code would describe it.

typedef struct place {
    float x;
    float y;
    int32_t hits;
} place;

typedef struct label {
    int64_t id;
    float weight;
    uint8_t lit;
    uint8_t pad[3];
} label;

typedef struct narrow {
    uint16_t a;
    uint16_t b;
    uint16_t c;
} narrow;

// The entity, then each component's lanes: place's three of 4 bytes, label's
// four of 4, and narrow's three of 2, as its size isn't a multiple of 4.
static const uint32_t sizes[] = {sizeof(tide_entity), 4, 4, 4, 4, 4, 4, 4, 2, 2, 2};
static const tide_columns columns = {11, 4, sizes};
enum { PLACE = 1, LABEL = 4, NARROW = 8, COLUMNS = 11 };

// A system that takes `mut place, label, mut narrow`, and one that reads all three.
static const tide_rows_component writes[] = {{PLACE, sizeof(place), 0}, {LABEL, sizeof(label), -1}, {NARROW, sizeof(narrow), 3}};
static const tide_rows_component reads[] = {{PLACE, sizeof(place), -1}, {LABEL, sizeof(label), -1}, {NARROW, sizeof(narrow), -1}};
enum { HELD = 6 }; // place's three lanes and narrow's three

static tide_page **page(tide_table *t, const uint32_t chunk, const uint32_t column)
{
    return &t->pages[chunk * COLUMNS + column];
}

// `n` entities: row r is at (r, -r) with r hits, its label r + 1000, and narrow (r, 2r, 3r).
static void fill(tide_table *t, tide_entities *entities, const uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        const tide_entity e = tide_entity_create(entities);
        const uint32_t row = tide_table_add(t, &columns);
        *(tide_entity *)tide_table_cell(t, &columns, row, 0) = e;
        const place p = {(float)row, -(float)row, (int32_t)row};
        const label l = {(int64_t)row + 1000, 0.5f * (float)row, row % 2 == 0, {0}};
        const narrow w = {(uint16_t)row, (uint16_t)(2 * row), (uint16_t)(3 * row)};
        tide_table_scatter(t, &columns, row, PLACE, 3, 4, &p);
        tide_table_scatter(t, &columns, row, LABEL, 4, 4, &l);
        tide_table_scatter(t, &columns, row, NARROW, 3, 2, &w);
        tide_entity_set_location(entities, e, (tide_location){0, row});
    }
}

static place place_at(const tide_table *t, const uint32_t row)
{
    place p;
    tide_table_gather(t, &columns, row, PLACE, 3, 4, &p);
    return p;
}

static uint32_t apart(tide_table *t, tide_table *snapshot, const uint32_t chunk)
{
    uint32_t count = 0;
    for (uint32_t k = 0; k < COLUMNS; k++) count += *page(t, chunk, k) != *page(snapshot, chunk, k);
    return count;
}

TIDE_TEST(rows_are_read_where_they_are)
{
    tide_table t = {0};
    tide_entities entities = {0};
    fill(&t, &entities, 40);
    tide_rows rows;
    tide_rows_begin(&rows, &t, &columns, 1, 0, 16, true, reads, 3, NULL, "Read");
    TIDE_CHECK(tide_rows_count(&rows) == 16);
    const float *x = tide_rows_floats(&rows, 0, offsetof(place, x));
    const float *y = tide_rows_floats(&rows, 0, offsetof(place, y));
    const int32_t *hits = tide_rows_ints(&rows, 0, offsetof(place, hits));
    TIDE_CHECK((const void *)x == tide_table_column(&t, &columns, 1, PLACE));
    TIDE_CHECK((const void *)y == tide_table_column(&t, &columns, 1, PLACE + 1));
    TIDE_CHECK((const void *)hits == tide_table_column(&t, &columns, 1, PLACE + 2));
    TIDE_CHECK(x[0] == 16.0f && y[0] == -16.0f && hits[0] == 16 && x[15] == 31.0f && hits[15] == 31);
    const int32_t *low = tide_rows_ints(&rows, 1, offsetof(label, id));
    const int32_t *high = tide_rows_ints(&rows, 1, offsetof(label, id) + 4);
    TIDE_CHECK(low[3] == 1019 && high[3] == 0);
    TIDE_CHECK(tide_rows_floats(&rows, 1, offsetof(label, weight))[4] == 10.0f);
    const tide_entity *who = tide_rows_entities(&rows);
    TIDE_CHECK(who == tide_table_column(&t, &columns, 1, 0) && who[2].index == 18);
    label l;
    tide_rows_get(&rows, 1, 5, &l);
    TIDE_CHECK(l.id == 1021 && l.weight == 10.5f && !l.lit);
    narrow w;
    tide_rows_get(&rows, 2, 5, &w);
    TIDE_CHECK(w.a == 21 && w.b == 42 && w.c == 63);
    tide_rows_end(&rows);

    tide_rows_begin(&rows, &t, &columns, 2, 3, 4, false, reads, 3, NULL, "Read");
    TIDE_CHECK(tide_rows_count(&rows) == 4);
    TIDE_CHECK(tide_rows_floats(&rows, 0, offsetof(place, x))[0] == 35.0f && tide_rows_entities(&rows)[3].index == 38);
    place p;
    tide_rows_get(&rows, 0, 3, &p);
    TIDE_CHECK(p.x == 38.0f && p.hits == 38);
    tide_rows_end(&rows);
    tide_table_free(&t, &columns);
    tide_entities_free(&entities);
}

TIDE_TEST(rows_written_as_they_were_stay_shared)
{
    tide_table t = {0};
    tide_table snapshot = {0};
    tide_entities entities = {0};
    fill(&t, &entities, 40);
    tide_table_copy(&snapshot, &t, &columns);
    const uint64_t before = tide_table_hash(1, &t, &columns);
    tide_page *held[HELD];
    tide_rows rows;

    tide_rows_begin(&rows, &t, &columns, 1, 0, 16, true, writes, 3, held, "Write");
    TIDE_CHECK(tide_rows_floats(&rows, 0, offsetof(place, x))[1] == 17.0f);
    tide_rows_end(&rows);
    TIDE_CHECK(apart(&t, &snapshot, 1) == 0);

    tide_rows_begin(&rows, &t, &columns, 1, 0, 16, true, writes, 3, held, "Write");
    float *x = tide_rows_floats_mut(&rows, 0, offsetof(place, x));
    TIDE_CHECK(*page(&t, 1, PLACE) != *page(&snapshot, 1, PLACE));
    TIDE_CHECK(apart(&t, &snapshot, 1) == 1);
    TIDE_CHECK(x[0] == 16.0f && x[15] == 31.0f);
    for (uint32_t i = 0; i < 16; i++) x[i] = (float)(16 + i);
    place p;
    tide_rows_get(&rows, 0, 2, &p);
    tide_rows_set(&rows, 0, 2, &p);
    TIDE_CHECK(apart(&t, &snapshot, 1) == 1);
    tide_rows_end(&rows);
    TIDE_CHECK(apart(&t, &snapshot, 1) == 0);
    TIDE_CHECK((*page(&t, 1, PLACE))->hashed != UINT32_MAX);
    TIDE_CHECK(tide_table_hash(1, &t, &columns) == before);

    tide_table_free(&t, &columns);
    tide_table_free(&snapshot, &columns);
    tide_entities_free(&entities);
}

TIDE_TEST(rows_written_make_their_lanes_their_own)
{
    tide_table t = {0};
    tide_table snapshot = {0};
    tide_entities entities = {0};
    fill(&t, &entities, 40);
    tide_table_copy(&snapshot, &t, &columns);
    const uint64_t before = tide_table_hash(1, &t, &columns);
    tide_page *held[HELD];
    tide_rows rows;

    tide_rows_begin(&rows, &t, &columns, 1, 0, 16, true, writes, 3, held, "Write");
    int32_t *hits = tide_rows_ints_mut(&rows, 0, offsetof(place, hits));
    hits[4] += 100;
    TIDE_CHECK(tide_rows_ints(&rows, 0, offsetof(place, hits))[4] == 120);
    place p;
    tide_rows_get(&rows, 0, 4, &p);
    TIDE_CHECK(p.hits == 120 && p.x == 20.0f);
    tide_rows_end(&rows);
    TIDE_CHECK(apart(&t, &snapshot, 1) == 1 && *page(&t, 1, PLACE + 2) != *page(&snapshot, 1, PLACE + 2));
    TIDE_CHECK(apart(&t, &snapshot, 0) == 0 && apart(&t, &snapshot, 2) == 0);
    TIDE_CHECK(place_at(&t, 20).hits == 120 && place_at(&t, 21).hits == 21 && place_at(&snapshot, 20).hits == 20);
    TIDE_CHECK(tide_table_hash(1, &t, &columns) != before && tide_table_hash(1, &snapshot, &columns) == before);

    tide_rows_begin(&rows, &t, &columns, 0, 0, 16, true, writes, 3, held, "Write");
    tide_rows_get(&rows, 0, 7, &p);
    p.y = 99.0f;
    tide_rows_set(&rows, 0, 7, &p);
    narrow w;
    tide_rows_get(&rows, 2, 7, &w);
    w.c = 5;
    tide_rows_set(&rows, 2, 7, &w);
    tide_rows_end(&rows);
    TIDE_CHECK(apart(&t, &snapshot, 0) == 2);
    TIDE_CHECK(*page(&t, 0, PLACE + 1) != *page(&snapshot, 0, PLACE + 1) && *page(&t, 0, NARROW + 2) != *page(&snapshot, 0, NARROW + 2));
    TIDE_CHECK(place_at(&t, 7).y == 99.0f && place_at(&t, 7).x == 7.0f && place_at(&t, 8).y == -8.0f);
    tide_table_gather(&t, &columns, 7, NARROW, 3, 2, &w);
    TIDE_CHECK(w.a == 7 && w.b == 14 && w.c == 5);

    const uint64_t changed = tide_table_hash(1, &t, &columns);
    const tide_page *own = *page(&t, 1, PLACE + 2);
    tide_rows_begin(&rows, &t, &columns, 1, 0, 16, true, writes, 3, held, "Write");
    hits = tide_rows_ints_mut(&rows, 0, offsetof(place, hits));
    TIDE_CHECK((const void *)hits == tide_page_data(own));
    hits[0] = -1;
    tide_rows_end(&rows);
    TIDE_CHECK(*page(&t, 1, PLACE + 2) == own && place_at(&t, 16).hits == -1);
    TIDE_CHECK(tide_table_hash(1, &t, &columns) != changed);

    tide_table_free(&t, &columns);
    tide_table_free(&snapshot, &columns);
    tide_entities_free(&entities);
}

TIDE_TEST(rows_leave_zeros_past_the_rows)
{
    tide_table t = {0};
    tide_table snapshot = {0};
    tide_entities entities = {0};
    fill(&t, &entities, 40);
    tide_table_copy(&snapshot, &t, &columns);
    tide_page *dirty = tide_page_take((*page(&t, 2, PLACE))->size);
    dirty->size = (*page(&t, 2, PLACE))->size;
    memset(tide_page_data(dirty), 0xff, dirty->size);
    tide_page_give(dirty);

    tide_page *held[HELD];
    tide_rows rows;
    tide_rows_begin(&rows, &t, &columns, 2, 0, 8, true, writes, 3, held, "Write");
    tide_rows_floats_mut(&rows, 0, offsetof(place, x))[7] = 1.5f;
    tide_rows_end(&rows);
    const float *x = tide_table_column(&t, &columns, 2, PLACE);
    TIDE_CHECK(x[6] == 38.0f && x[7] == 1.5f);
    bool zeros = true;
    for (uint32_t i = 8; i < 16; i++) zeros &= x[i] == 0.0f;
    TIDE_CHECK(zeros);
    const uint32_t row = tide_table_add(&t, &columns);
    TIDE_CHECK(row == 40 && place_at(&t, 40).x == 0.0f);

    tide_table_free(&t, &columns);
    tide_table_free(&snapshot, &columns);
    tide_entities_free(&entities);
}

TIDE_TEST(rows_in_pieces_write_in_place)
{
    tide_table t = {0};
    tide_table snapshot = {0};
    tide_entities entities = {0};
    fill(&t, &entities, 32);
    tide_table_copy(&snapshot, &t, &columns);
    for (uint32_t k = 0; k < 3; k++) tide_table_column_mut(&t, &columns, 1, PLACE + k);
    const tide_page *own = *page(&t, 1, PLACE);
    tide_page *held[2][HELD];
    tide_rows piece[2];
    tide_rows_begin(&piece[0], &t, &columns, 1, 0, 8, false, writes, 3, held[0], "Write");
    tide_rows_begin(&piece[1], &t, &columns, 1, 8, 8, false, writes, 3, held[1], "Write");
    for (uint32_t i = 0; i < 2; i++) {
        float *x = tide_rows_floats_mut(&piece[i], 0, offsetof(place, x));
        for (uint32_t r = 0; r < tide_rows_count(&piece[i]); r++) x[r] += 0.25f;
    }
    tide_rows_end(&piece[1]);
    tide_rows_end(&piece[0]);
    TIDE_CHECK(*page(&t, 1, PLACE) == own);
    TIDE_CHECK(place_at(&t, 16).x == 16.25f && place_at(&t, 23).x == 23.25f && place_at(&t, 24).x == 24.25f && place_at(&t, 31).x == 31.25f);
    TIDE_CHECK(place_at(&snapshot, 16).x == 16.0f && place_at(&t, 15).x == 15.0f);

    tide_table_free(&t, &columns);
    tide_table_free(&snapshot, &columns);
    tide_entities_free(&entities);
}

TIDE_TEST(rows_of_a_system_that_runs_once_are_none)
{
    tide_rows rows;
    tide_rows_begin(&rows, NULL, NULL, 0, 0, 0, true, NULL, 0, NULL, "Once");
    TIDE_CHECK(tide_rows_count(&rows) == 0 && tide_rows_entities(&rows) == NULL);
    tide_rows_end(&rows);
}
