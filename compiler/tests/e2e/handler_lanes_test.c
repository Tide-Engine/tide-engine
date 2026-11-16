#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

// handler_lanes.tide: what each handler leaves of the components it takes `mut`, worked out here, and which pages
// of the tables are still the ones a snapshot taken before the tick has. A page is another page exactly when its
// bytes are other bytes: a handler that took a lane for writing and wrote what was there, or nothing, would copy
// the page (pages() fails), and one that didn't put back a lane it changed would lose the value (check() fails).

enum { PARTS = 2500 };

typedef struct model {
    int a, b;
    float px, py;
    int ix, iy;
    int slots[4];
    int items, item;
    int hits;
    bool extra;
    int extra_n;
    int q, s;
    float r;
} model;

static tide_world world, kept;
extern tide_world *handler_lanes_world; // handler_lanes_c.c: the world OnSly's C Sets in
static model now[PARTS];
static int slow_due[PARTS]; // Ticks until OnSlow's second half
static int later_by[PARTS]; // What Later adds once it has waited a tick
static int painted;

static int pick(const int k) { return (k * 97 + 5) % PARTS; }

static tide_entity part_at(tide_world *w, const int i)
{
    tide_text_use(&w->heap, NULL);
    return *(const tide_entity *)tide_list_at(w->Plan.parts, i, sizeof(tide_entity));
}

static void know_start(void)
{
    memset(now, 0, sizeof now);
    memset(slow_due, 0, sizeof slow_due);
    memset(later_by, 0, sizeof later_by);
    painted = 0;
    for (int i = 0; i < PARTS; i++) now[i].item = -1;
}

// What a tick in `step` does, as the handlers are written: events are taken in the order Drive sent them, and
// what a handler adds or removes is there from the tick's end
static void know(const Step step)
{
    const bool all = step == Step_All;
    bool joined[PARTS] = {false}, left[PARTS] = {false};
    for (int i = 0; i < PARTS; i++) {
        if (slow_due[i] && --slow_due[i] == 0) now[i].b += 5;
        now[i].s += later_by[i];
        later_by[i] = 0;
    }
    if (step == Step_Poke || all) for (int k = 0; k < 10; k++) now[pick(k)].a += k + 1;
    if (step == Step_Maybe || all) for (int k = 0; k < 20; k += 2) now[pick(k)].b += 3;
    if (step == Step_Deep || all) {
        for (int k = 0; k < 10; k++) {
            model *m = &now[pick(k + 3)];
            m->iy += k + 2;
            m->slots[2] += k + 2;
            m->px += (float)(k + 2);
            m->py -= 1.0f;
        }
    }
    if (step == Step_Grow || all) {
        for (int k = 0; k < 6; k++) {
            now[pick(k * 2)].items++;
            now[pick(k * 2)].item = 100 + k;
        }
    }
    if (step == Step_Tap || all) for (int k = 0; k < 40; k++) if (pick(k) % 5 == 0) now[pick(k)].hits++;
    if (step == Step_Join || all) {
        for (int k = 0; k < 6; k++) {
            model *m = &now[pick(k + 20)];
            if (m->extra) continue;
            m->b += k + 1;
            m->extra_n = k + 1;
            joined[pick(k + 20)] = true;
        }
    }
    if (step == Step_Leave || all) {
        for (int k = 0; k < 6; k++) {
            model *m = &now[pick(k + 20)];
            if (!m->extra) continue;
            m->a += m->extra_n;
            left[pick(k + 20)] = true;
        }
    }
    if (step == Step_Reset || all) {
        for (int k = 0; k < 8; k++) {
            now[pick(k + 1)].q = 50 + k;
            now[pick(k + 1)].s++;
        }
    }
    if (step == Step_Far || all) {
        for (int k = 0; k < 5; k++) {
            now[pick(k)].a++;
            now[pick(k + 40)].ix += k + 1;
            now[pick(k + 40)].r += 0.5f;
        }
    }
    if (step == Step_Native || all) {
        for (int k = 0; k < 8; k++) {
            if (k & 1) now[pick(k + 2)].q += k;
            else if (k) now[pick(k + 2)].r += 1.0f;
        }
    }
    if (step == Step_Slow || all) {
        for (int k = 0; k < 4; k++) {
            if (slow_due[pick(k + 7)]) continue; // (The steps below never start one while another waits)
            now[pick(k + 7)].a += 5;
            slow_due[pick(k + 7)] = 2;
        }
    }
    if (step == Step_Paint || all) painted++;
    if (step == Step_Start || all) {
        for (int k = 0; k < 3; k++) { // The task's first half runs in the handler, between its two writes
            model *m = &now[pick(k + 30)];
            m->q++;
            m->s += k + 2;
            m->r += 1.0f;
            later_by[pick(k + 30)] = k + 2;
        }
    }
    if (step == Step_Undo || all) for (int k = 0; k < 3; k++) later_by[pick(k + 33)] = k + 4; // Its first half is written over
    for (int i = 0; i < PARTS; i++) {
        if (joined[i]) now[i].extra = true;
        if (left[i]) now[i].extra = false;
    }
}

static bool text_is(tide_world *w, const tide_text t, const char *expected)
{
    const tide_str s = tide_text_read(&w->heap, t);
    return s.bytes == (int32_t)strlen(expected) && memcmp(s.ptr, expected, strlen(expected)) == 0;
}

// Every entity of `w` against what's known of it
static bool check(tide_world *w, const model *m, const int paints)
{
    int wrong = 0;
    for (int i = 0; i < PARTS; i++) {
        const tide_entity e = part_at(w, i);
        if (!tide_has_Part(w, e) || !tide_has_Plain(w, e)) {
            wrong++;
            continue;
        }
        const Part p = tide_get_Part(w, e);
        const Plain plain = tide_get_Plain(w, e);
        bool ok = p.id == i && p.quiet == i * 3 && p.a == m[i].a && p.b == m[i].b && p.pos.x == m[i].px && p.pos.y == m[i].py;
        ok = ok && p.inner.x == m[i].ix && p.inner.y == m[i].iy;
        for (int k = 0; k < 4; k++) ok = ok && p.slots.at[k] == m[i].slots[k];
        tide_text_use(&w->heap, NULL);
        ok = ok && (int)tide_list_count(p.items) == m[i].items;
        for (int k = 0; ok && k < m[i].items; k++) ok = *(const int32_t *)tide_list_at(p.items, k, sizeof(int32_t)) == m[i].item;
        char label[32];
        if (m[i].items) snprintf(label, sizeof label, "grown %d", m[i].items);
        else snprintf(label, sizeof label, "part");
        ok = ok && text_is(w, p.label, label);
        ok = ok && plain.p == i && plain.q == m[i].q && plain.r == m[i].r && plain.s == m[i].s;
        ok = ok && tide_has_Mark(w, e) == (i % 5 == 0);
        if (i % 5 == 0) {
            const Mark mark = tide_get_Mark(w, e);
            ok = ok && mark.hits == m[i].hits && mark.other == i;
        }
        ok = ok && tide_has_Extra(w, e) == m[i].extra;
        if (m[i].extra) {
            const Extra extra = tide_get_Extra(w, e);
            ok = ok && extra.n == m[i].extra_n && extra.m == 0;
        }
        ok = ok && tide_has_Kept(w, e) == (i % 3 != 0);
        if (!ok) {
            if (!wrong) printf("    entity %d isn't what its handlers leave: a %d (%d), b %d (%d)\n", i, p.a, m[i].a, p.b, m[i].b);
            wrong++;
        }
    }
    const Field field = tide_get_Field(w, w->Plan.field);
    if (field.marks != paints || field.still != 7) wrong++;
    const Wide wide = tide_get_Wide(w, w->Plan.field); // Filled from 9 and its paints before, by the last of them
    if (wide.sum != paints) wrong++;
    for (int k = 0; k < 20; k++) if (wide.cells.at[k] != (paints ? 9 + paints - 1 + k : 0)) wrong++;
    return wrong == 0;
}

// The tables entities with a Part are in, in the world and in the snapshot taken before the tick
typedef struct arch {
    const tide_table *now, *then;
    const tide_columns *columns;
    uint32_t part, plain, mark, extra; // Their first lanes' columns; UINT32_MAX for none
} arch;

#define ARCH(name, extra_column) \
    {&world.name, &kept.name, &tide_columns_##name, tide_column_##name##_Part, tide_column_##name##_Plain, tide_column_##name##_Mark, extra_column}

static const arch archs[4] = {
    ARCH(arch0_Part_Plain, UINT32_MAX),
    ARCH(arch1_Part_Plain_Kept, UINT32_MAX),
    ARCH(arch3_Part_Plain_Extra, tide_column_arch3_Part_Plain_Extra_Extra),
    ARCH(arch4_Part_Plain_Kept_Extra, tide_column_arch4_Part_Plain_Kept_Extra_Extra),
};

#define PART_LANE(field) (1u << (offsetof(Part, field) / 4u))
#define PLAIN_LANE(field) (1u << (offsetof(Plain, field) / 4u))
#define MARK_LANE(field) (1u << (offsetof(Mark, field) / 4u))

// What a tick changed of the tables, by their pages
typedef struct changed {
    uint32_t part, plain, mark; // The lanes of each that are another page in some chunk
    uint32_t other;             // Pages of other columns that are (entities, scenes, presence, Kept, Extra)
    uint32_t pages;             // Pages that are another, in all
    uint32_t chunks, quiet;     // Chunks looked at, and those with every page the snapshot's
    bool exact;                 // Every page is another exactly when its bytes are, and one that isn't is still hashed
    bool moved;                 // A table has other rows than it had: entities came or went
    uint32_t first;             // The first page that isn't exact: its table, chunk and column, three digits each
} changed;

static changed pages(void)
{
    changed c = {.exact = true};
    for (int a = 0; a < 4; a++) {
        const arch *t = &archs[a];
        if (t->now->chunks != t->then->chunks) c.moved = true;
        for (uint32_t chunk = 0; chunk < t->now->chunks && chunk < t->then->chunks; chunk++) {
            const uint32_t rows = tide_table_rows(t->now, t->columns->shift, chunk);
            if (rows != tide_table_rows(t->then, t->columns->shift, chunk)) { // A chunk that grew or shrank is written
                c.moved = true;
                continue;
            }
            c.chunks++;
            uint32_t here = 0;
            for (uint32_t column = 0; column < t->columns->count; column++) {
                const uint32_t at = chunk * t->columns->count + column;
                const size_t bytes = (size_t)rows * t->columns->sizes[column];
                const bool same = memcmp(tide_page_data(t->now->pages[at]), tide_page_data(t->then->pages[at]), bytes) == 0;
                const bool shared = t->now->pages[at] == t->then->pages[at];
                // (A page the two share that isn't hashed any more was taken for writing where it is)
                if (shared != same || (shared && t->now->pages[at]->hashed == UINT32_MAX)) {
                    if (c.exact) c.first = (uint32_t)a * 1000000u + chunk * 1000u + column;
                    c.exact = false;
                }
                if (shared) continue;
                here++;
                if (column >= t->part && column < t->part + sizeof(Part) / 4u) c.part |= 1u << (column - t->part);
                else if (column >= t->plain && column < t->plain + sizeof(Plain) / 4u) c.plain |= 1u << (column - t->plain);
                else if (column >= t->mark && column < t->mark + sizeof(Mark) / 4u) c.mark |= 1u << (column - t->mark);
                else c.other++;
            }
            c.pages += here;
            if (!here) c.quiet++;
        }
    }
    return c;
}

static void start(tide_world *w)
{
    tide_world_init(w, 1.0f / 60.0f);
    w->Plan.step = Step_Quiet;
    tide_world_tick(w);
}

// A tick of `world` in `step` beside a snapshot of it, which every page is shared with and hashed in
static uint64_t snapshot_hash;

static changed tick_beside(const Step step)
{
    tide_world_free(&kept);
    tide_world_copy(&kept, &world);
    snapshot_hash = tide_world_hash(&kept);
    world.Plan.step = step;
    handler_lanes_world = &world;
    tide_world_tick(&world);
    return pages();
}

TIDE_TEST(handler_lanes_each_handler_takes_the_pages_it_changed_and_no_other)
{
    start(&world);
    know_start();
    TIDE_REQUIRE(tide_list_count(world.Plan.parts) == PARTS && check(&world, now, 0));
    TIDE_REQUIRE(world.arch0_Part_Plain.chunks == 1 && world.arch1_Part_Plain_Kept.chunks == 2);
    static model before[PARTS];
    static const Step steps[] = {
        Step_Quiet, Step_Poke, Step_Maybe, Step_Same, Step_Deep, Step_Grow, Step_Grow, Step_Tap, Step_Reset, Step_Far, Step_Native,
        Step_Slow, Step_Quiet, Step_Quiet, Step_Quiet, Step_Paint, Step_Paint, Step_Join, Step_Poke, Step_Deep, Step_Tap, Step_Leave,
        Step_Quiet, Step_Poke, Step_Start, Step_Quiet, Step_Undo, Step_Quiet, Step_Start, Step_Undo, Step_Quiet, Step_Sly,
        Step_Native, Step_Sly,
    };
    int grown = 0;
    for (size_t n = 0; n < sizeof steps / sizeof steps[0]; n++) {
        const Step step = steps[n];
        memcpy(before, now, sizeof before);
        const int painted_before = painted;
        const changed c = tick_beside(step);
        know(step);
        TIDE_CHECK(check(&world, now, painted));
        // The snapshot is the world as it was: its values, and its hash
        TIDE_CHECK(check(&kept, before, painted_before));
        TIDE_CHECK(tide_world_hash(&kept) == snapshot_hash);
        // A page is another exactly when its bytes are. (Not asked of a tick that fills a hole an entity left: the
        // row is zeroed and written whole, whatever was there; nor of Undo's and Sly's, where a field is written
        // and written back)
        const bool asked = step != Step_Leave && step != Step_Undo && step != Step_Sly;
        if (asked) TIDE_CHECK(c.exact);
        if (asked && !c.exact) {
            printf("    tick %d, step %d: table %u's chunk %u's column %u is another page with the same bytes, or the same with others\n",
                   (int)n, (int)step, c.first / 1000000u, c.first / 1000u % 1000u, c.first % 1000u);
        }
        const uint32_t handles = PART_LANE(items) | PART_LANE(items) << 1 | PART_LANE(label) | PART_LANE(label) << 1;
        switch (step) {
        case Step_Quiet: // Nothing sent: nothing taken (but by a task that goes on, below)
            TIDE_CHECK(!c.mark && !c.other && !(c.part & ~PART_LANE(b)) && !(c.plain & ~PLAIN_LANE(s)));
            break;
        case Step_Start: // The handler's two fields and the task's
            TIDE_CHECK(c.plain == (PLAIN_LANE(q) | PLAIN_LANE(r) | PLAIN_LANE(s)) && !c.part && !c.mark && !c.other);
            break;
        case Step_Undo: // The task wrote the field's page and the handler wrote what was there over it
        case Step_Sly:  // ...as C did
            TIDE_CHECK(!(c.plain & ~PLAIN_LANE(s)) && !c.part && !c.mark && !c.other);
            break;
        case Step_Poke: { // One lane, in the chunks the ten are in
            bool in[5][4] = {{false}};
            uint32_t chunks = 0;
            const uint32_t shift = archs[0].columns->shift; // The four tables' chunks hold as many rows
            for (int a = 1; a < 4; a++) TIDE_REQUIRE(archs[a].columns->shift == shift);
            for (int k = 0; k < 10; k++) {
                const tide_location loc = tide_entity_location(&world.entities, part_at(&world, pick(k)));
                TIDE_REQUIRE(loc.archetype < 5 && loc.row >> shift < 4);
                if (!in[loc.archetype][loc.row >> shift]) chunks++;
                in[loc.archetype][loc.row >> shift] = true;
            }
            TIDE_CHECK(c.part == PART_LANE(a) && !c.plain && !c.mark && !c.other && !c.moved);
            TIDE_CHECK(c.pages == chunks && c.chunks - c.quiet == chunks);
            break;
        }
        case Step_Maybe:
            TIDE_CHECK(c.part == PART_LANE(b) && !c.plain && !c.mark && !c.other);
            break;
        case Step_Same: // Its code writes five lanes, each with what was there
            TIDE_CHECK(!c.pages && c.quiet == c.chunks);
            break;
        case Step_Deep:
            TIDE_CHECK(c.part == (PART_LANE(pos) | PART_LANE(pos) << 1 | PART_LANE(inner.y) | PART_LANE(slots) << 2));
            TIDE_CHECK(!c.plain && !c.mark && !c.other);
            break;
        case Step_Grow: // A list's first element gives it a block; its second is in the block it has
            TIDE_CHECK(c.part && !(c.part & ~handles) && !c.plain && !c.mark && !c.other);
            if (grown++) TIDE_CHECK(!(c.part & (PART_LANE(items) | PART_LANE(items) << 1)));
            else TIDE_CHECK(c.part & PART_LANE(items));
            break;
        case Step_Tap:
            TIDE_CHECK(c.mark == MARK_LANE(hits) && !c.part && !c.plain && !c.other);
            break;
        case Step_Reset: // A Set of every field, two of them other values
            TIDE_CHECK(c.plain == (PLAIN_LANE(q) | PLAIN_LANE(s)) && !c.part && !c.mark && !c.other);
            break;
        case Step_Far:
            TIDE_CHECK(c.part == (PART_LANE(a) | PART_LANE(inner.x)) && c.plain == PLAIN_LANE(r) && !c.mark && !c.other);
            break;
        case Step_Native: // C could have written any lane
            TIDE_CHECK(c.plain == (PLAIN_LANE(q) | PLAIN_LANE(r)) && !c.part && !c.mark && !c.other);
            break;
        case Step_Slow:
            TIDE_CHECK(c.part == PART_LANE(a) && !c.plain && !c.mark && !c.other);
            break;
        case Step_Paint: // The grid is another table's, and the heap's
            TIDE_CHECK(!c.pages);
            break;
        case Step_Join: // They move to other tables at the tick's end: their rows there are new, and holes here
            TIDE_CHECK(c.moved && c.part == PART_LANE(b) && !c.plain && !c.mark);
            break;
        case Step_Leave:
            TIDE_CHECK(c.moved);
            break;
        default: break;
        }
    }
    TIDE_CHECK(now[pick(7)].b == 5 && now[pick(20)].a == 1 && !now[pick(20)].extra); // The task ended, and they left
    tide_world_free(&kept);
    tide_world_free(&world);
}

// The same ticks alone, beside a snapshot of each tick, and run again from a snapshot kept on the way: one world
TIDE_TEST(handler_lanes_same_alone_beside_snapshots_and_run_again)
{
    static const Step steps[] = {
        Step_Poke, Step_All, Step_Deep, Step_All, Step_Join, Step_All, Step_Leave, Step_Grow, Step_All, Step_Slow, Step_All,
        Step_Quiet, Step_All, Step_Reset, Step_Far, Step_All, Step_Quiet, Step_Quiet, Step_All, Step_Start, Step_Undo, Step_All,
        Step_Sly,        Step_Quiet,
    };
    enum { TICKS = sizeof steps / sizeof steps[0], FROM = 4, RING = 3 };
    static tide_world alone, base, replay, ring[RING];
    uint64_t hashes[TICKS];
    start(&alone);
    for (int t = 0; t < TICKS; t++) {
        alone.Plan.step = steps[t];
        handler_lanes_world = &alone;
        tide_world_tick(&alone);
        hashes[t] = tide_world_hash(&alone);
    }
    start(&world);
    uint64_t base_hash = 0;
    for (int t = 0; t < TICKS; t++) {
        if (t == FROM) {
            tide_world_copy(&base, &world);
            base_hash = tide_world_hash(&base);
        }
        (void)tick_beside(steps[t]);
        TIDE_CHECK(tide_world_hash(&world) == hashes[t]);
        TIDE_CHECK(tide_world_hash(&kept) == snapshot_hash);
    }
    // Again from the kept one, with a ring of the last ticks' worlds beside it as a client has
    tide_world_copy(&replay, &base);
    for (int t = FROM; t < TICKS; t++) {
        tide_world_free(&ring[t % RING]);
        tide_world_copy(&ring[t % RING], &replay);
        replay.Plan.step = steps[t];
        handler_lanes_world = &replay;
        tide_world_tick(&replay);
        TIDE_CHECK(tide_world_hash(&replay) == hashes[t]);
    }
    TIDE_CHECK(tide_world_hash(&base) == base_hash);
    for (int k = 0; k < RING; k++) tide_world_free(&ring[k]);
    tide_world_free(&replay);
    tide_world_free(&base);
    tide_world_free(&kept);
    tide_world_free(&world);
    tide_world_free(&alone);
}
