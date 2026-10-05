#include <stdlib.h>
#include <string.h>

#include "tide/table.h"
#include "tide_test.h"

// An archetype of entities with a position: the entity column, then a float
// pair, in chunks of 16 rows.
static const uint32_t sizes[] = {sizeof(tide_entity), 2 * sizeof(float)};
static const tide_columns columns = {2, 4, sizes};

// Another, with a third column, for moving rows between them.
static const uint32_t wider_sizes[] = {sizeof(tide_entity), 2 * sizeof(float), sizeof(int32_t)};
static const tide_columns wider = {3, 4, wider_sizes};

static const float *position(const tide_table *t, const uint32_t row)
{
    return tide_table_get(t, &columns, row, 1);
}

static tide_entity entity_at(const tide_table *t, const tide_columns *c, const uint32_t row)
{
    tide_entity e;
    memcpy(&e, tide_table_get(t, c, row, 0), sizeof e);
    return e;
}

// `n` more entities, each with position (row, -row).
static void fill(tide_table *t, tide_entities *entities, const uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        const tide_entity e = tide_entity_create(entities);
        const uint32_t row = tide_table_add(t, &columns);
        *(tide_entity *)tide_table_cell(t, &columns, row, 0) = e;
        float *p = tide_table_cell(t, &columns, row, 1);
        p[0] = (float)row;
        p[1] = -(float)row;
        tide_entity_set_location(entities, e, (tide_location){0, row});
    }
}

TIDE_TEST(table_grows_in_chunks)
{
    tide_table t = {0};
    tide_entities entities = {0};
    fill(&t, &entities, 3);
    TIDE_CHECK(t.chunks == 1 && t.last_room == 4); // The first chunk starts small
    fill(&t, &entities, 2);
    TIDE_CHECK(t.chunks == 1 && t.last_room == 8);
    fill(&t, &entities, 40);
    TIDE_CHECK(t.count == 45 && t.chunks == 3 && t.last_room == 16);
    TIDE_CHECK(tide_table_rows(&t, columns.shift, 2) == 13);
    TIDE_CHECK(position(&t, 0)[0] == 0.0f && position(&t, 4)[0] == 4.0f && position(&t, 44)[1] == -44.0f);
    tide_table_free(&t, &columns);
    tide_entities_free(&entities);
}

TIDE_TEST(table_remove_moves_the_last_row_in)
{
    tide_table t = {0};
    tide_entities entities = {0};
    fill(&t, &entities, 17); // A second chunk with one row
    const tide_entity last = entity_at(&t, &columns, 16);
    tide_table_remove(&t, &columns, 3, &entities, 0);
    TIDE_CHECK(t.count == 16 && t.chunks == 1); // The chunk left empty goes
    TIDE_CHECK(position(&t, 3)[0] == 16.0f);
    TIDE_CHECK(tide_entity_equal(entity_at(&t, &columns, 3), last));
    TIDE_CHECK(tide_entity_location(&entities, last).row == 3);
    tide_table_remove(&t, &columns, 15, &entities, 0); // The last row: nothing moves
    TIDE_CHECK(t.count == 15 && position(&t, 14)[0] == 14.0f);
    tide_table_free(&t, &columns);
    tide_entities_free(&entities);
}

TIDE_TEST(table_move_keeps_shared_columns)
{
    tide_table from = {0};
    tide_table to = {0};
    tide_entities entities = {0};
    fill(&from, &entities, 5);
    const tide_entity moved = entity_at(&from, &columns, 1);
    const int32_t map[] = {0, 1, -1};
    const uint32_t row = tide_table_move(&from, &columns, 1, 0, &to, &wider, 1, map, &entities);
    TIDE_CHECK(row == 0 && to.count == 1 && from.count == 4);
    TIDE_CHECK(((const float *)tide_table_get(&to, &wider, 0, 1))[0] == 1.0f);
    TIDE_CHECK(*(const int32_t *)tide_table_get(&to, &wider, 0, 2) == 0);
    const tide_location at = tide_entity_location(&entities, moved);
    TIDE_CHECK(at.archetype == 1 && at.row == 0);
    TIDE_CHECK(tide_entity_location(&entities, entity_at(&from, &columns, 1)).row == 1); // The last one, moved in
    tide_table_free(&from, &columns);
    tide_table_free(&to, &wider);
    tide_entities_free(&entities);
}

// A snapshot shares the table's pages; changing a column of a chunk copies
// that page alone, and the hash only follows what's in use.
TIDE_TEST(table_snapshots_share_until_changed)
{
    tide_table t = {0};
    tide_table snapshot = {0};
    tide_entities entities = {0};
    fill(&t, &entities, 40);
    tide_table_copy(&snapshot, &t, &columns);
    const uint64_t before = tide_table_hash(1, &t, &columns);
    TIDE_CHECK(tide_table_hash(1, &snapshot, &columns) == before);

    float *p = tide_table_cell(&t, &columns, 20, 1);
    p[0] = 100.0f;
    TIDE_CHECK(t.pages[1 * 2 + 1] != snapshot.pages[1 * 2 + 1]); // Chunk 1's positions: copied
    TIDE_CHECK(t.pages[1 * 2 + 0] == snapshot.pages[1 * 2 + 0]); // Its entities: still shared
    TIDE_CHECK(t.pages[0 * 2 + 1] == snapshot.pages[0 * 2 + 1]); // Chunk 0: still shared
    TIDE_CHECK(position(&snapshot, 20)[0] == 20.0f);
    TIDE_CHECK(tide_table_hash(1, &t, &columns) != before);

    p = tide_table_cell(&t, &columns, 20, 1); // Changed through the table, which keeps the hash up to date
    p[0] = 20.0f;                            // As it was: the same hash again
    TIDE_CHECK(tide_table_hash(1, &t, &columns) == before);

    tide_table_copy(&t, &snapshot, &columns);
    TIDE_CHECK(t.pages[1 * 2 + 1] == snapshot.pages[1 * 2 + 1]);
    tide_table_free(&t, &columns);
    tide_table_free(&snapshot, &columns);
    tide_entities_free(&entities);
}

// A loop over a chunk's rows changes its lanes through tide_table_lanes_begin:
// a shared page is copied only if a row changed.
TIDE_TEST(table_lanes_stay_shared_unless_a_row_changed)
{
    tide_table t = {0};
    tide_table snapshot = {0};
    tide_entities entities = {0};
    fill(&t, &entities, 40);
    tide_table_copy(&snapshot, &t, &columns);
    const uint64_t before = tide_table_hash(1, &t, &columns);
    const uint8_t *was[1];
    uint8_t *lanes[1];
    TIDE_CHECK(tide_table_lanes_shared(&t, &columns, 1, 1, 1));

    // Written as they were: nothing moves
    tide_table_lanes_begin(&t, &columns, 1, 1, 1, false, was, lanes);
    TIDE_CHECK(lanes[0] != tide_page_data(t.pages[1 * 2 + 1])); // A copy, since the snapshot shares the page
    TIDE_CHECK(was[0] == lanes[0]);                             // ...which is read where it's written
    for (uint32_t i = 0; i < 16; i++) {
        float *p = (float *)(lanes[0] + i * 8);
        p[0] = (float)(16 + i);
        p[1] = -(float)(16 + i);
    }
    tide_table_lanes_end(&t, &columns, 1, 1, 1, lanes);
    TIDE_CHECK(t.pages[1 * 2 + 1] == snapshot.pages[1 * 2 + 1]);
    TIDE_CHECK(t.pages[1 * 2 + 1]->hashed != UINT32_MAX); // Its hash still good
    TIDE_CHECK(tide_table_hash(1, &t, &columns) == before);

    // One row changed: this table's own page, the snapshot's as it was
    tide_table_lanes_begin(&t, &columns, 1, 1, 1, false, was, lanes);
    ((float *)(lanes[0] + 4 * 8))[0] = 100.0f;
    tide_table_lanes_end(&t, &columns, 1, 1, 1, lanes);
    TIDE_CHECK(t.pages[1 * 2 + 1] != snapshot.pages[1 * 2 + 1]);
    TIDE_CHECK(position(&t, 20)[0] == 100.0f && position(&t, 21)[0] == 21.0f);
    TIDE_CHECK(position(&snapshot, 20)[0] == 20.0f);
    TIDE_CHECK(tide_table_hash(1, &t, &columns) != before);
    TIDE_CHECK(tide_table_hash(1, &snapshot, &columns) == before);

    // A page the table alone has is written in place
    TIDE_CHECK(!tide_table_lanes_shared(&t, &columns, 1, 1, 1));
    tide_table_lanes_begin(&t, &columns, 1, 1, 1, false, was, lanes);
    TIDE_CHECK(lanes[0] == tide_page_data(t.pages[1 * 2 + 1]) && was[0] == lanes[0]);
    tide_table_lanes_end(&t, &columns, 1, 1, 1, lanes);

    tide_table_free(&t, &columns);
    tide_table_free(&snapshot, &columns);
    tide_entities_free(&entities);
}

// A loop that writes every row reads a lane where it is and writes it apart:
// nothing is copied first, and a page that comes out as it was stays.
TIDE_TEST(table_lanes_are_written_apart)
{
    tide_table t = {0};
    tide_table snapshot = {0};
    tide_entities entities = {0};
    fill(&t, &entities, 40);
    tide_table_copy(&snapshot, &t, &columns);
    const uint64_t before = tide_table_hash(1, &t, &columns);
    const uint8_t *was[1];
    uint8_t *lanes[1];

    // Every row written as it was: the shared page stays, and its hash
    tide_table_lanes_begin(&t, &columns, 1, 1, 1, true, was, lanes);
    TIDE_CHECK(was[0] == tide_page_data(t.pages[1 * 2 + 1])); // Read where it is
    TIDE_CHECK(lanes[0] != was[0]);                           // ...and written somewhere else
    for (uint32_t i = 0; i < 16; i++) memcpy(lanes[0] + i * 8, was[0] + i * 8, 8);
    tide_table_lanes_end(&t, &columns, 1, 1, 1, lanes);
    TIDE_CHECK(t.pages[1 * 2 + 1] == snapshot.pages[1 * 2 + 1]);
    TIDE_CHECK(t.pages[1 * 2 + 1]->hashed != UINT32_MAX);
    TIDE_CHECK(tide_table_hash(1, &t, &columns) == before);

    // Every row written, one of them otherwise: the page that was written is the table's
    tide_table_lanes_begin(&t, &columns, 1, 1, 1, true, was, lanes);
    for (uint32_t i = 0; i < 16; i++) memcpy(lanes[0] + i * 8, was[0] + i * 8, 8);
    ((float *)(lanes[0] + 4 * 8))[0] = 100.0f;
    tide_table_lanes_end(&t, &columns, 1, 1, 1, lanes);
    TIDE_CHECK(t.pages[1 * 2 + 1] != snapshot.pages[1 * 2 + 1]);
    TIDE_CHECK(tide_page_data(t.pages[1 * 2 + 1]) == lanes[0]);
    TIDE_CHECK(position(&t, 20)[0] == 100.0f && position(&t, 21)[0] == 21.0f && position(&t, 39)[0] == 39.0f);
    TIDE_CHECK(position(&snapshot, 20)[0] == 20.0f);
    TIDE_CHECK(tide_table_hash(1, &snapshot, &columns) == before);

    // The same with a page the table alone has: apart too, as a loop that asks reads and writes two places
    const tide_page *own = t.pages[1 * 2 + 1];
    const uint64_t changed = tide_table_hash(1, &t, &columns);
    tide_table_lanes_begin(&t, &columns, 1, 1, 1, true, was, lanes);
    TIDE_CHECK(was[0] == tide_page_data(own) && lanes[0] != was[0]);
    for (uint32_t i = 0; i < 16; i++) memcpy(lanes[0] + i * 8, was[0] + i * 8, 8);
    tide_table_lanes_end(&t, &columns, 1, 1, 1, lanes);
    TIDE_CHECK(t.pages[1 * 2 + 1] == own && tide_table_hash(1, &t, &columns) == changed); // As it was: it stays
    tide_table_lanes_begin(&t, &columns, 1, 1, 1, true, was, lanes);
    for (uint32_t i = 0; i < 16; i++) memcpy(lanes[0] + i * 8, was[0] + i * 8, 8);
    ((float *)(lanes[0] + 5 * 8))[1] = 7.0f;
    tide_table_lanes_end(&t, &columns, 1, 1, 1, lanes);
    TIDE_CHECK(position(&t, 21)[1] == 7.0f && position(&t, 20)[0] == 100.0f && position(&t, 22)[0] == 22.0f);
    TIDE_CHECK(tide_table_hash(1, &t, &columns) != changed);

    tide_table_free(&t, &columns);
    tide_table_free(&snapshot, &columns);
    tide_entities_free(&entities);
}

TIDE_TEST(table_packs_and_unpacks)
{
    tide_table t = {0};
    tide_entities entities = {0};
    fill(&t, &entities, 37);
    const uint32_t size = tide_table_packed_size(&t, &columns);
    uint8_t *bytes = malloc(size);
    tide_writer w = {bytes, size, 0, false, false};
    tide_table_pack(&t, &columns, &w);
    TIDE_CHECK(!w.overflow && w.size == size);

    tide_table back = {0};
    tide_reader r = {bytes, size, 0, false};
    TIDE_REQUIRE(tide_table_unpack(&back, &columns, &r));
    TIDE_CHECK(back.count == 37 && back.chunks == 3);
    TIDE_CHECK(tide_table_hash(1, &back, &columns) == tide_table_hash(1, &t, &columns));
    TIDE_CHECK(position(&back, 36)[1] == -36.0f);

    tide_table short_of_rows = {0};
    tide_reader cut = {bytes, size - 1u, 0, false};
    TIDE_CHECK(!tide_table_unpack(&short_of_rows, &columns, &cut));
    free(bytes);
    tide_table_free(&t, &columns);
    tide_table_free(&back, &columns);
    tide_table_free(&short_of_rows, &columns);
    tide_entities_free(&entities);
}

// A queue's items start with their size, which pushing sets, and are as big
// as they're pushed: a note is 8 bytes, and one with `extra` bytes after it
// that much more.
typedef struct note {
    uint32_t size;
    int32_t value;
} note;

static note *push_note(tide_queue *q, const int32_t value, const uint32_t extra)
{
    note *n = tide_queue_push(q, (uint32_t)sizeof(note) + extra);
    n->value = value;
    return n;
}

// The notes' values are `first`, `first + 1` and so on, `count` of them, from `at`.
static bool notes_from(const tide_queue *q, tide_queue_place at, const int32_t first, const int32_t count)
{
    for (int32_t i = 0; i < count; i++) {
        const note *n = tide_queue_next(q, &at);
        if (!n || n->value != first + i) return false;
    }
    return tide_queue_next(q, &at) == NULL;
}

// Reserve runs, and record the same notes one at a time for comparison. Only
// the requested payload is written, so alignment padding must stay zero too.
static void push_note_runs(tide_queue *runs, tide_queue *ordinary, const int32_t first,
                           const uint32_t count, const uint32_t extra)
{
    for (uint32_t done = 0; done < count;) {
        uint32_t n = count - done;
        uint8_t *items = tide_queue_push_run(runs, (uint32_t)sizeof(note) + extra, &n);
        TIDE_REQUIRE(items && n > 0 && n <= count - done);
        uint32_t stride;
        memcpy(&stride, items, sizeof stride);
        for (uint32_t i = 0; i < n; i++) {
            note *a = (note *)(items + i * stride);
            note *b = tide_queue_push(ordinary, (uint32_t)sizeof(note) + extra);
            TIDE_REQUIRE(a->size == b->size);
            for (uint32_t byte = (uint32_t)sizeof(a->size); byte < a->size; byte++)
                TIDE_CHECK(((const uint8_t *)a)[byte] == 0);
            a->value = b->value = first + (int32_t)(done + i);
            const int pattern = 1 + a->value % 251;
            memset(a + 1, pattern, extra);
            memset(b + 1, pattern, extra);
        }
        done += n;
    }
}

static bool queues_equal(const tide_queue *a, const tide_queue *b)
{
    if (a->count != b->count || tide_queue_hash(7, a) != tide_queue_hash(7, b)) return false;
    tide_queue_place left = {0}, right = {0};
    for (uint32_t i = 0; i < a->count; i++) {
        const note *x = tide_queue_next(a, &left), *y = tide_queue_next(b, &right);
        if (!x || !y || x->size != y->size || memcmp(x, y, x->size)) return false;
    }
    if (tide_queue_next(a, &left) || tide_queue_next(b, &right)) return false;
    const uint32_t size = tide_queue_packed_size(a);
    if (size != tide_queue_packed_size(b)) return false;
    uint8_t *one = malloc(size), *two = malloc(size);
    if (!one || !two) { free(one); free(two); return false; }
    tide_writer w1 = {one, size, 0, false, false}, w2 = {two, size, 0, false, false};
    tide_queue_pack(a, &w1);
    tide_queue_pack(b, &w2);
    const bool equal = !w1.overflow && !w2.overflow && w1.size == size && w2.size == size &&
                       memcmp(one, two, size) == 0;
    free(one);
    free(two);
    return equal;
}

TIDE_TEST(table_queue_runs_keep_the_ordinary_format)
{
    tide_queue runs = {0}, ordinary = {0}, snapshot = {0};
    uint32_t zero = 0;
    TIDE_CHECK(tide_queue_push_run(&runs, 1, &zero) == NULL && zero == 0);
    TIDE_CHECK(runs.count == 0 && runs.pages == 0 && runs.kept == 0 && runs.page == NULL);
    const struct { uint32_t count, extra; } groups[] = {
        {1021, 0}, {17, 1}, {613, 25}, {1, TIDE_QUEUE_PAGE - (uint32_t)sizeof(note)},
        {2, 3u * TIDE_QUEUE_PAGE + 1u}, {1023, 7}
    };
    int32_t first = 0;
    for (uint32_t i = 0; i < sizeof groups / sizeof groups[0]; i++) {
        push_note_runs(&runs, &ordinary, first, groups[i].count, groups[i].extra);
        first += (int32_t)groups[i].count;
        // Runs and ordinary pushes may be interleaved without changing order.
        push_note(&runs, first, 9);
        push_note(&ordinary, first++, 9);
    }
    TIDE_CHECK(runs.pages > 5 && notes_from(&runs, (tide_queue_place){0}, 0, first));
    TIDE_CHECK(queues_equal(&runs, &ordinary));
    const tide_queue_place end = tide_queue_end(&runs);
    const uint32_t count = runs.count, pages = runs.pages, kept = runs.kept;
    tide_queue_page **const storage = runs.page;
    TIDE_CHECK(tide_queue_push_run(&runs, 3u * TIDE_QUEUE_PAGE, &zero) == NULL && zero == 0);
    const tide_queue_place after = tide_queue_end(&runs);
    TIDE_CHECK(runs.count == count && runs.pages == pages && runs.kept == kept && runs.page == storage);
    TIDE_CHECK(end.page == after.page && end.offset == after.offset);

    tide_queue_copy(&snapshot, &runs);
    TIDE_CHECK(queues_equal(&snapshot, &ordinary));
    // Dirty payload and padding before reuse: every run item must be zeroed,
    // even where a different size lands over a previous item's size header.
    tide_queue_place at = {0};
    note *item;
    while ((item = tide_queue_next(&runs, &at)) != NULL)
        memset((uint8_t *)item + sizeof item->size, 0xa5, item->size - (uint32_t)sizeof item->size);
    tide_queue_clear(&runs);
    TIDE_CHECK(queues_equal(&snapshot, &ordinary));
    tide_queue_clear(&ordinary);
    push_note_runs(&runs, &ordinary, 0, 1500, 13);
    push_note_runs(&runs, &ordinary, 1500, 1, 3u * TIDE_QUEUE_PAGE + 1u);
    TIDE_CHECK(queues_equal(&runs, &ordinary));
    TIDE_CHECK(snapshot.count == count && notes_from(&snapshot, (tide_queue_place){0}, 0, first));
    tide_queue_free(&runs);
    tide_queue_free(&ordinary);
    tide_queue_free(&snapshot);
}

TIDE_TEST(table_queue_runs_append_and_resume_reading)
{
    // The short append copies into a partial page, and the long one hands
    // pages over. A reader waiting at the end must see both kinds of append.
    const uint32_t counts[] = {1, 1300};
    for (uint32_t i = 0; i < sizeof counts / sizeof counts[0]; i++) {
        tide_queue to = {0}, from = {0}, ordinary = {0}, part = {0}, snapshot = {0};
        push_note_runs(&to, &ordinary, 0, 11, 9);
        note *const first = to.page[0] ? (note *)(to.page[0] + 1) : NULL;
        tide_queue_place at = tide_queue_end(&to);
        TIDE_CHECK(tide_queue_next(&to, &at) == NULL);
        const tide_queue_place end = at;
        push_note_runs(&from, &part, 11, counts[i], 17);
        tide_queue_copy(&snapshot, &from);
        tide_queue_append(&to, &from);
        tide_queue_append(&ordinary, &part);
        TIDE_CHECK(from.count == 0 && from.pages == 0 && queues_equal(&to, &ordinary));
        TIDE_CHECK(first && first->value == 0);
        TIDE_CHECK(notes_from(&to, end, 11, (int32_t)counts[i]));
        const note *next = tide_queue_next(&to, &at);
        TIDE_CHECK(next && next->value == 11);
        // A copied queue keeps its records while the source's empty pages
        // are reused, and appending never prevents a later run reservation.
        push_note_runs(&from, &part, 0, 700, 5);
        TIDE_CHECK(notes_from(&snapshot, (tide_queue_place){0}, 11, (int32_t)counts[i]));
        TIDE_CHECK(queues_equal(&from, &part));
        push_note_runs(&to, &ordinary, 11 + (int32_t)counts[i], 600, 0);
        TIDE_CHECK(queues_equal(&to, &ordinary));
        TIDE_CHECK(notes_from(&to, (tide_queue_place){0}, 0, 611 + (int32_t)counts[i]));
        tide_queue_free(&to);
        tide_queue_free(&from);
        tide_queue_free(&ordinary);
        tide_queue_free(&part);
        tide_queue_free(&snapshot);
    }
}

TIDE_TEST(table_queue_keeps_items_in_place)
{
    tide_queue q = {0};
    const tide_queue_place start = tide_queue_end(&q); // Of an empty queue: what's pushed is found from it
    note *first = push_note(&q, 5, 0);
    TIDE_CHECK(first->size == 8);
    for (int32_t i = 1; i < 3000; i++) push_note(&q, 5 + i, (uint32_t)i % 5u * 12u); // Of several sizes, through many pages
    TIDE_CHECK(q.count == 3000 && q.pages > 4);
    TIDE_CHECK(first->value == 5 && notes_from(&q, start, 5, 3000)); // The first didn't move as the queue grew
    tide_queue_place at = {0, 0};
    TIDE_CHECK(tide_queue_next(&q, &at) == first);
    const note *second = tide_queue_next(&q, &at);
    TIDE_CHECK(second->size == 24 && second->value == 6); // 8 and 12 more, in whole eights

    // From the end, the next is what's pushed after: on this page, or the one after
    const tide_queue_place end = tide_queue_end(&q);
    at = end;
    TIDE_CHECK(tide_queue_next(&q, &at) == NULL && tide_queue_next(&q, &at) == NULL);
    for (int32_t i = 0; i < 700; i++) push_note(&q, 9000 + i, 0);
    TIDE_CHECK(notes_from(&q, end, 9000, 700));
    const note *found = tide_queue_next(&q, &at); // The place that had reached the end goes on from it
    TIDE_CHECK(found && found->value == 9000);

    tide_queue copy = {0};
    tide_queue_copy(&copy, &q);
    TIDE_CHECK(copy.count == q.count && tide_queue_hash(1, &copy) == tide_queue_hash(1, &q));
    const uint32_t pages = q.pages;
    tide_queue_clear(&q);
    TIDE_CHECK(q.count == 0 && q.pages == 0 && q.kept == pages); // It keeps the pages it used
    at = (tide_queue_place){0, 0};
    TIDE_CHECK(tide_queue_next(&q, &at) == NULL);
    note *again = push_note(&q, 0, 0);
    TIDE_CHECK(again == first && again->value == 0 && again->size == 8); // Its page kept, the item zeroed as it's pushed again
    tide_queue_clear(&q);
    TIDE_CHECK(q.kept == 1); // ...and lets go of the ones this time didn't need
    tide_queue_free(&q);
    tide_queue_free(&copy);
    TIDE_CHECK(q.page == NULL && q.kept == 0);
}

TIDE_TEST(table_queue_holds_an_item_bigger_than_a_page)
{
    tide_queue q = {0};
    push_note(&q, 1, 0);
    note *big = push_note(&q, 2, 3u * TIDE_QUEUE_PAGE);
    memset(big + 1, 0x5a, 3u * TIDE_QUEUE_PAGE);
    push_note(&q, 3, 0);
    TIDE_CHECK(notes_from(&q, (tide_queue_place){0, 0}, 1, 3));
    TIDE_CHECK(((const uint8_t *)(big + 1))[3u * TIDE_QUEUE_PAGE - 1u] == 0x5a);
    // Its page is kept, and takes small items too once it's emptied
    tide_queue_clear(&q);
    for (int32_t i = 0; i < 2000; i++) push_note(&q, i, 0);
    TIDE_CHECK(notes_from(&q, (tide_queue_place){0, 0}, 0, 2000));
    tide_queue_free(&q);
}

TIDE_TEST(table_queue_appends_in_order)
{
    // A few items are copied onto the end, and full pages are handed over:
    // through both, with the page they land on empty, part full and full
    for (int32_t had = 0; had < 1400; had += 233) {
        for (int32_t more = 0; more < 2000; more += 371) {
            tide_queue to = {0}, from = {0};
            for (int32_t i = 0; i < had; i++) push_note(&to, i, (uint32_t)i % 3u * 8u);
            for (int32_t i = 0; i < more; i++) push_note(&from, had + i, (uint32_t)i % 4u * 8u);
            tide_queue_append(&to, &from);
            TIDE_CHECK(to.count == (uint32_t)(had + more) && from.count == 0 && from.pages == 0);
            TIDE_CHECK(notes_from(&to, (tide_queue_place){0, 0}, 0, had + more));
            // What's pushed next goes after them
            push_note(&to, had + more, 0);
            TIDE_CHECK(notes_from(&to, (tide_queue_place){0, 0}, 0, had + more + 1));
            tide_queue_free(&to);
            tide_queue_free(&from);
        }
    }
}

TIDE_TEST(table_queue_hands_pages_over_and_gets_empty_ones_back)
{
    // As a tick's tasks and its world do: the task fills its queue, the world
    // takes it and is cleared once it's applied. From the second tick on,
    // nothing is copied and no page is made.
    tide_queue world = {0}, task = {0};
    const tide_queue_page *task_page = NULL;
    const tide_queue_page *world_page = NULL;
    for (int tick = 0; tick < 4; tick++) {
        for (int32_t i = 0; i < 1000; i++) push_note(&task, i, 0);
        TIDE_CHECK(task.pages == 2);
        if (tick >= 2) TIDE_CHECK(task.page[0] == task_page || task.page[0] == world_page);
        const tide_queue_page *filled = task.page[0];
        tide_queue_append(&world, &task);
        TIDE_CHECK(world.count == 1000 && world.page[0] == filled); // The page itself
        TIDE_CHECK(task.count == 0 && task.pages == 0);
        TIDE_CHECK(notes_from(&world, (tide_queue_place){0, 0}, 0, 1000));
        if (tick == 0) TIDE_CHECK(task.kept == 0); // The world had no page to give back yet
        if (tick >= 1) TIDE_CHECK(task.kept == 2 && world.kept == 2);
        if (tick == 1) {
            task_page = task.page[0];
            world_page = world.page[0];
            TIDE_CHECK(task_page != world_page);
        }
        tide_queue_clear(&world);
    }
    // A burst's pages go once a tick doesn't need them
    for (int32_t i = 0; i < 20000; i++) push_note(&task, i, 0);
    tide_queue_append(&world, &task);
    tide_queue_clear(&world);
    TIDE_CHECK(world.kept > 30);
    push_note(&task, 1, 0);
    tide_queue_append(&world, &task);
    tide_queue_clear(&world);
    TIDE_CHECK(world.kept == 1 && task.kept <= 1);
    tide_queue_free(&world);
    tide_queue_free(&task);
}

static bool note_fits(const void *item)
{
    const note *n = item;
    return n->value >= 0; // Stands in for a game's check of each change's size
}

TIDE_TEST(table_queue_packs_whatever_its_pages)
{
    // The same items, pushed straight in and handed over from two others
    tide_queue straight = {0}, moved = {0}, a = {0}, b = {0};
    for (int32_t i = 0; i < 900; i++) push_note(&straight, i, (uint32_t)i % 7u * 8u);
    for (int32_t i = 0; i < 300; i++) push_note(&a, i, (uint32_t)i % 7u * 8u);
    for (int32_t i = 300; i < 900; i++) push_note(&b, i, (uint32_t)i % 7u * 8u);
    tide_queue_append(&moved, &a);
    tide_queue_append(&moved, &b);
    TIDE_CHECK(moved.page[2]->used != straight.page[2]->used); // Laid out otherwise
    TIDE_CHECK(tide_queue_hash(7, &moved) == tide_queue_hash(7, &straight));
    const uint32_t size = tide_queue_packed_size(&moved);
    TIDE_CHECK(size == tide_queue_packed_size(&straight));
    uint8_t *bytes = malloc(size);
    uint8_t *other = malloc(size);
    tide_writer w = {bytes, size, 0, false, false};
    tide_writer w2 = {other, size, 0, false, false};
    tide_queue_pack(&moved, &w);
    tide_queue_pack(&straight, &w2);
    TIDE_CHECK(!w.overflow && w.size == size && memcmp(bytes, other, size) == 0);

    tide_queue back = {0};
    tide_reader r = {bytes, size, 0, false};
    TIDE_CHECK(tide_queue_unpack(&back, &r, note_fits) && r.at == size);
    TIDE_CHECK(back.count == 900 && tide_queue_hash(7, &back) == tide_queue_hash(7, &straight));
    tide_queue_free(&back);

    // Bytes from another machine: cut short, a size that isn't one, an item its check refuses
    tide_reader cut = {bytes, size - 5u, 0, false};
    TIDE_CHECK(!tide_queue_unpack(&back, &cut, note_fits));
    tide_queue_free(&back);
    const uint32_t sizes[] = {0u, 4u, 12u, 0xfffffff8u};
    for (int i = 0; i < 4; i++) {
        memcpy(other, bytes, size);
        memcpy(other + 4, &sizes[i], 4); // The first item's size
        tide_reader bad = {other, size, 0, false};
        TIDE_CHECK(!tide_queue_unpack(&back, &bad, note_fits));
        tide_queue_free(&back);
    }
    memcpy(other, bytes, size);
    const int32_t refused = -1;
    memcpy(other + 8, &refused, 4); // The first item's value
    tide_reader bad = {other, size, 0, false};
    TIDE_CHECK(!tide_queue_unpack(&back, &bad, note_fits));
    tide_queue_free(&back);
    uint32_t many = 0xffffffffu;
    memcpy(other, &many, 4); // More items than there are bytes for
    tide_reader few = {other, size, 0, false};
    TIDE_CHECK(!tide_queue_unpack(&back, &few, NULL));
    tide_queue_free(&back);

    free(bytes);
    free(other);
    tide_queue_free(&straight);
    tide_queue_free(&moved);
    tide_queue_free(&a);
    tide_queue_free(&b);
}
