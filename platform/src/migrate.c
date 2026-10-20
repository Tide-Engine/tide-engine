#include "tide/migrate.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/entity.h"
#include "tide/heap.h"
#include "tide/net.h"
#include "tide/table.h"

// See tide/migrate.h.

typedef struct carry {
    const tide_layout *from;
    const tide_layout *to;
    int32_t *types; // For each of `to`'s types, `from`'s of the same name, or -1
} carry;

static int32_t find_type(const tide_layout *l, const char *name)
{
    for (uint32_t i = 0; i < l->type_count; i++) {
        if (strcmp(l->types[i].name, name) == 0) return (int32_t)i;
    }
    return -1;
}

static const tide_layout_field *find_field(const tide_layout_type *t, const char *name)
{
    for (uint32_t i = 0; i < t->field_count; i++) {
        if (strcmp(t->fields[i].name, name) == 0) return &t->fields[i];
    }
    return NULL;
}

// Whether a field's value carries over to a field of the same name: the same
// type, or a number that converts as Tide converts them implicitly (int to
// float and to long) or by a widening cast (long to float).
static bool carries(const tide_layout_field *to, const tide_layout_field *from)
{
    if (strcmp(to->type, from->type) == 0) return to->kind != TIDE_LAYOUT_PLAIN || to->size == from->size;
    if (from->dim != to->dim) return false;
    if (to->kind == TIDE_LAYOUT_DOUBLE) return from->kind == TIDE_LAYOUT_BYTE || from->kind == TIDE_LAYOUT_INT
        || from->kind == TIDE_LAYOUT_LONG || from->kind == TIDE_LAYOUT_FLOAT;
    if (from->kind == TIDE_LAYOUT_BYTE) return to->kind == TIDE_LAYOUT_INT || to->kind == TIDE_LAYOUT_LONG || to->kind == TIDE_LAYOUT_FLOAT;
    if (from->kind == TIDE_LAYOUT_INT) return to->kind == TIDE_LAYOUT_FLOAT || to->kind == TIDE_LAYOUT_LONG;
    return from->kind == TIDE_LAYOUT_LONG && to->kind == TIDE_LAYOUT_FLOAT;
}

static void carry_fields(const carry *c, int32_t to_type, int32_t from_type, uint8_t *to, const uint8_t *from);

// An enum's value, as wide as its field: a byte, a ushort or an int.
static int32_t member_at(const uint8_t *at, const uint32_t size)
{
    if (size == 1) return at[0];
    if (size == 2) {
        uint16_t v;
        memcpy(&v, at, sizeof v);
        return v;
    }
    int32_t v;
    memcpy(&v, at, sizeof v);
    return v;
}

static void set_member(uint8_t *at, const uint32_t size, const int32_t value)
{
    if (size == 1) {
        at[0] = (uint8_t)value;
    } else if (size == 2) {
        const uint16_t v = (uint16_t)value;
        memcpy(at, &v, sizeof v);
    } else {
        memcpy(at, &value, sizeof value);
    }
}

// An enum's member, by its name: the new build's number for it, or false when
// it has none (or it doesn't fit the field, which a new build made narrower).
static bool carry_member(const carry *c, const tide_layout_field *tf, uint8_t *to, const tide_layout_field *ff,
                         const uint8_t *from)
{
    if (tf->decl < 0 || ff->decl < 0) return false;
    const tide_layout_enum *te = &c->to->enums[tf->decl];
    const tide_layout_enum *fe = &c->from->enums[ff->decl];
    const int32_t value = member_at(from, ff->size);
    for (uint32_t i = 0; i < fe->member_count; i++) {
        if (fe->members[i].value != value) continue;
        for (uint32_t k = 0; k < te->member_count; k++) {
            if (strcmp(te->members[k].name, fe->members[i].name) != 0) continue;
            set_member(to, tf->size, te->members[k].value);
            return true;
        }
        return false;
    }
    return false;
}

// One field, into one that has its default already: false leaves it so.
static void carry_field(const carry *c, const tide_layout_field *tf, uint8_t *to, const tide_layout_field *ff,
                        const uint8_t *from)
{
    if (!carries(tf, ff)) return;
    if (strcmp(tf->type, ff->type) != 0) { // A number converting (see carries)
        for (uint32_t i = 0; i < tf->dim; i++) {
            if (tf->kind == TIDE_LAYOUT_DOUBLE) {
                double value;
                if (ff->kind == TIDE_LAYOUT_BYTE) value = from[i];
                else if (ff->kind == TIDE_LAYOUT_INT) {
                    int32_t n;
                    memcpy(&n, from + 4u * i, sizeof n);
                    value = (double)n;
                } else if (ff->kind == TIDE_LAYOUT_LONG) {
                    int64_t n;
                    memcpy(&n, from + 8u * i, sizeof n);
                    value = (double)n;
                } else {
                    float n;
                    memcpy(&n, from + 4u * i, sizeof n);
                    value = (double)n;
                }
                memcpy(to + 8u * i, &value, sizeof value);
            } else if (ff->kind == TIDE_LAYOUT_BYTE) {
                const int32_t n = from[i];
                if (tf->kind == TIDE_LAYOUT_LONG) {
                    const int64_t l = n;
                    memcpy(to + 8u * i, &l, sizeof l);
                } else if (tf->kind == TIDE_LAYOUT_FLOAT) {
                    const float f = (float)n;
                    memcpy(to + 4u * i, &f, sizeof f);
                } else {
                    memcpy(to + 4u * i, &n, sizeof n);
                }
            } else if (ff->kind == TIDE_LAYOUT_LONG) { // To a float
                int64_t n;
                memcpy(&n, from + 8u * i, sizeof n);
                const float f = (float)n;
                memcpy(to + 4u * i, &f, sizeof f);
            } else if (tf->kind == TIDE_LAYOUT_LONG) { // An int to a long
                int32_t n;
                memcpy(&n, from + 4u * i, sizeof n);
                const int64_t l = n;
                memcpy(to + 8u * i, &l, sizeof l);
            } else { // An int to a float
                int32_t n;
                memcpy(&n, from + 4u * i, sizeof n);
                const float f = (float)n;
                memcpy(to + 4u * i, &f, sizeof f);
            }
        }
        return;
    }
    switch (tf->kind) {
    case TIDE_LAYOUT_STRUCT:
        carry_fields(c, tf->decl, ff->decl, to, from);
        break;
    case TIDE_LAYOUT_ENUM:
        carry_member(c, tf, to, ff, from);
        break;
    default:
        memcpy(to, from, tf->size);
        break;
    }
}

// The fields of a value of `from_type` into one of `to_type` that has its
// defaults already.
static void carry_fields(const carry *c, const int32_t to_type, const int32_t from_type, uint8_t *to,
                         const uint8_t *from)
{
    if (to_type < 0 || from_type < 0) return;
    const tide_layout_type *tt = &c->to->types[to_type];
    const tide_layout_type *ft = &c->from->types[from_type];
    for (uint32_t i = 0; i < tt->field_count; i++) {
        const tide_layout_field *ff = find_field(ft, tt->fields[i].name);
        if (ff) carry_field(c, &tt->fields[i], to + tt->fields[i].offset, ff, from + ff->offset);
    }
}

// A whole value: the new type's defaults, then what carries over. `from` may
// be NULL, for a value that's new.
static void carry_value(const carry *c, const int32_t to_type, const int32_t from_type, uint8_t *to,
                        const uint8_t *from)
{
    c->to->types[to_type].defaults(to);
    if (from) carry_fields(c, to_type, from_type, to, from);
}

// The component of `from`'s archetype `a` that carries over to `to_type`, or
// NULL.
static const tide_layout_place *old_component(const carry *c, const tide_layout_archetype *a, const int32_t to_type)
{
    const int32_t from_type = c->types[to_type];
    for (uint32_t i = 0; from_type >= 0 && i < a->component_count; i++) {
        if (a->components[i].type == from_type) return &a->components[i];
    }
    return NULL;
}

// Where the entities of `from`'s archetype `a` go: the new archetype with the
// components of theirs the new build has, or -1.
// Whether the new build keeps the component at `p` as part of its archetypes
// (a sparse one in the new build is a row's own, so it tells no archetype apart).
static bool solid_kept(const carry *c, const tide_layout_world *tw, const tide_layout_place *p)
{
    const int32_t to_type = find_type(c->to, c->from->types[p->type].name);
    if (to_type < 0) return false;
    for (uint32_t k = 0; k < tw->archetype_count; k++) {
        const tide_layout_archetype *b = &tw->archetypes[k];
        for (uint32_t i = 0; i < b->component_count; i++) {
            if (b->components[i].type == to_type) return !b->components[i].sparse;
        }
    }
    return true;
}

static int32_t new_archetype(const carry *c, const tide_layout_world *tw, const tide_layout_archetype *a)
{
    uint32_t kept = 0; // Its components the new build keeps as parts of archetypes
    for (uint32_t i = 0; i < a->component_count; i++) kept += solid_kept(c, tw, &a->components[i]);
    for (uint32_t k = 0; k < tw->archetype_count; k++) {
        const tide_layout_archetype *b = &tw->archetypes[k];
        uint32_t solid = 0;
        bool all = true;
        for (uint32_t i = 0; all && i < b->component_count; i++) {
            if (b->components[i].sparse) continue; // A row's own: there whether the old entity had it or not
            solid++;
            all = old_component(c, a, b->components[i].type) != NULL;
        }
        if (all && solid == kept) return (int32_t)k;
    }
    return -1;
}

// The scene component of `a`'s entities, when the new build has no scene of
// that name or nowhere to keep them: the scene is gone.
static const char *lost_scene(const carry *c, const tide_layout_world *tw, const tide_layout_archetype *a)
{
    for (uint32_t i = 0; i < a->component_count; i++) {
        const tide_layout_type *t = &c->from->types[a->components[i].type];
        if (!t->scene) continue;
        const int32_t now = find_type(c->to, t->name);
        if (now < 0 || !c->to->types[now].scene || new_archetype(c, tw, a) < 0) return t->name;
    }
    return NULL;
}

// An archetype's columns, as a world's bytes have them (tide/table.h): the
// entity, the scene each is in, then its components' lanes (each component
// `tide_lane_width` bytes at a time, from the column its place says), each
// column's values one after another.
// Its columns: a presence byte first, for a sparse one.
static uint32_t lanes_of(const tide_layout *l, const tide_layout_place *p)
{
    const uint32_t size = l->types[p->type].size;
    return (p->sparse ? 1u : 0u) + size / tide_lane_width(size);
}

// Whether column `column` of the archetype is `p`'s presence byte.
static bool is_presence(const tide_layout_place *p, const uint32_t column)
{
    return p->sparse && column == p->offset;
}

static uint32_t column_count(const tide_layout *l, const tide_layout_archetype *a)
{
    uint32_t count = 1u + (a->scenes ? 1u : 0u);
    for (uint32_t i = 0; i < a->component_count; i++) count += lanes_of(l, &a->components[i]);
    return count;
}

// The component whose lanes take in column `column`, or NULL.
static const tide_layout_place *component_at(const tide_layout *l, const tide_layout_archetype *a, const uint32_t column)
{
    for (uint32_t i = 0; i < a->component_count; i++) {
        const tide_layout_place *p = &a->components[i];
        if (column >= p->offset && column < p->offset + lanes_of(l, p)) return p;
    }
    return NULL;
}

static uint32_t column_size(const tide_layout *l, const tide_layout_archetype *a, const uint32_t column)
{
    if (column == 0 || (a->scenes && column == 1)) return (uint32_t)sizeof(tide_entity);
    const tide_layout_place *p = component_at(l, a, column);
    if (!p) return 0u;
    return is_presence(p, column) ? 1u : tide_lane_width(l->types[p->type].size);
}

static uint64_t row_size(const tide_layout *l, const tide_layout_archetype *a)
{
    uint64_t size = 0;
    for (uint32_t k = 0; k < column_count(l, a); k++) size += column_size(l, a, k);
    return size;
}

// Where row `row` of column `column` is in an archetype's `count` rows.
static const uint8_t *cell(const tide_layout *l, const tide_layout_archetype *a, const uint8_t *rows,
                           const uint32_t count, const uint32_t column, const uint32_t row)
{
    uint64_t at = 0;
    for (uint32_t k = 0; k < column; k++) at += (uint64_t)count * column_size(l, a, k);
    return rows + at + (uint64_t)row * column_size(l, a, column);
}

// Row `row`'s component at `p`, gathered from its lanes (after its presence
// byte, for a sparse one) into `value`.
static void gather(const tide_layout *l, const tide_layout_archetype *a, const uint8_t *rows, const uint32_t count,
                   const tide_layout_place *p, const uint32_t row, uint8_t *value)
{
    const uint32_t width = tide_lane_width(l->types[p->type].size);
    const uint32_t first = p->offset + (p->sparse ? 1u : 0u);
    for (uint32_t k = 0; k < lanes_of(l, p) - (p->sparse ? 1u : 0u); k++) {
        memcpy(value + k * width, cell(l, a, rows, count, first + k, row), width);
    }
}

// Whether row `row` has the component at `p`: a sparse one by its presence byte, any other always.
static bool present(const tide_layout *l, const tide_layout_archetype *a, const uint8_t *rows, const uint32_t count,
                    const tide_layout_place *p, const uint32_t row)
{
    return !p->sparse || *cell(l, a, rows, count, p->offset, row) != 0;
}

// The new bytes, `n` more of them.
static uint8_t *take(tide_writer *w, const uint32_t n)
{
    uint8_t *at = w->data + w->size;
    w->size += n;
    return at;
}

// What's read of the old world's bytes.
typedef struct old_world {
    tide_entities entities;
    const uint8_t **rows; // Each archetype's
    uint32_t *counts;
    const uint8_t **task_rows; // Each table of tasks'
    uint32_t *task_counts;
    const uint8_t **attached_rows; // Each table of local fields': its entities, their values, then what each was born as
    uint32_t *attached_counts;
    const uint8_t **resident_parts; // Each resident's canonical bytes, with their kind and size before them
    uint32_t *resident_sizes;
    const uint8_t *heap;  // Its heap's bytes, the rest of them
    uint32_t heap_size;
} old_world;

static void free_old(old_world *w)
{
    tide_entities_free(&w->entities);
    free(w->rows);
    free(w->counts);
    free(w->task_rows);
    free(w->task_counts);
    free(w->attached_rows);
    free(w->attached_counts);
    free(w->resident_parts);
    free(w->resident_sizes);
}

// The old table of local fields that `p`'s carries over from: the one of the
// type of the same name ("<Component> local"), or -1.
static int32_t old_attached(const carry *c, const tide_layout_world *fw, const tide_layout_place *p)
{
    for (uint32_t i = 0; i < fw->attached_count && c->types[p->type] >= 0; i++) {
        if (fw->attached[i].type == c->types[p->type]) return (int32_t)i;
    }
    return -1;
}

// The old table of tasks that `t` carries over from: one of the same name,
// whose code and frame are the same, or -1.
static int32_t old_tasks(const tide_layout_world *fw, const tide_layout_tasks *t)
{
    for (uint32_t i = 0; i < fw->task_count; i++) {
        if (strcmp(fw->tasks[i].name, t->name) == 0 && fw->tasks[i].size == t->size) return (int32_t)i;
    }
    return -1;
}

// The old resident `name` carries over from: the one of the same name, or -1.
static int32_t old_resident(const tide_layout_world *fw, const char *name)
{
    for (uint32_t i = 0; i < fw->resident_count; i++) {
        if (strcmp(fw->residents[i], name) == 0) return (int32_t)i;
    }
    return -1;
}

static bool read_old(const tide_layout *l, const tide_layout_world *fw, const uint8_t *from, const uint32_t size,
                     old_world *w, tide_migration *m)
{
    w->rows = calloc(fw->archetype_count ? fw->archetype_count : 1u, sizeof *w->rows);
    w->counts = calloc(fw->archetype_count ? fw->archetype_count : 1u, sizeof *w->counts);
    w->task_rows = calloc(fw->task_count ? fw->task_count : 1u, sizeof *w->task_rows);
    w->task_counts = calloc(fw->task_count ? fw->task_count : 1u, sizeof *w->task_counts);
    w->attached_rows = calloc(fw->attached_count ? fw->attached_count : 1u, sizeof *w->attached_rows);
    w->attached_counts = calloc(fw->attached_count ? fw->attached_count : 1u, sizeof *w->attached_counts);
    w->resident_parts = calloc(fw->resident_count ? fw->resident_count : 1u, sizeof *w->resident_parts);
    w->resident_sizes = calloc(fw->resident_count ? fw->resident_count : 1u, sizeof *w->resident_sizes);
    if (!w->rows || !w->counts || !w->task_rows || !w->task_counts || !w->attached_rows || !w->attached_counts || !w->resident_parts
        || !w->resident_sizes) {
        snprintf(m->failed, sizeof m->failed, "there wasn't enough memory");
        return false;
    }
    tide_reader r = {from, size, fw->size, size < fw->size};
    bool ok = !r.failed && tide_entities_unpack(&w->entities, &r);
    for (uint32_t a = 0; ok && a < fw->archetype_count; a++) {
        w->counts[a] = tide_read_u32(&r);
        const uint64_t bytes = (uint64_t)w->counts[a] * row_size(l, &fw->archetypes[a]);
        ok = !r.failed && bytes <= r.size - r.at;
        if (ok) w->rows[a] = tide_read_bytes(&r, (uint32_t)bytes);
    }
    const uint32_t commands = ok ? tide_read_u32(&r) : 0u;
    ok = ok && !r.failed;
    if (ok && commands) {
        snprintf(m->failed, sizeof m->failed, "it had changes waiting to be applied");
        return false;
    }
    for (uint32_t t = 0; ok && t < fw->task_count; t++) {
        w->task_counts[t] = tide_read_u32(&r);
        const uint64_t bytes = (uint64_t)w->task_counts[t] * fw->tasks[t].size;
        ok = !r.failed && bytes <= r.size - r.at;
        if (ok) w->task_rows[t] = tide_read_bytes(&r, (uint32_t)bytes);
    }
    for (uint32_t t = 0; ok && t < fw->attached_count; t++) {
        w->attached_counts[t] = tide_read_u32(&r);
        const uint64_t bytes = (uint64_t)w->attached_counts[t]
                             * (sizeof(tide_entity) + l->types[fw->attached[t].type].size + sizeof(uint32_t));
        ok = !r.failed && bytes <= r.size - r.at;
        if (ok) w->attached_rows[t] = tide_read_bytes(&r, (uint32_t)bytes);
    }
    for (uint32_t t = 0; ok && t < fw->resident_count; t++) { // Its kind, its size, its bytes
        const uint32_t start = r.at;
        (void)tide_read_u64(&r);
        const uint32_t bytes = tide_read_u32(&r);
        ok = !r.failed && bytes <= r.size - r.at;
        if (ok) {
            tide_read_bytes(&r, bytes);
            w->resident_parts[t] = from + start;
            w->resident_sizes[t] = r.at - start;
        }
    }
    w->heap = from + r.at;
    w->heap_size = ok ? size - r.at : 0u;
    if (!ok || (!fw->heap && w->heap_size)) {
        snprintf(m->failed, sizeof m->failed, "its bytes weren't a world of the old build");
        return false;
    }
    return true;
}

bool tide_migrate_world(const tide_layout *from_layout, const tide_layout_world *fw, const void *from_bytes,
                        const uint32_t from_size, const tide_layout *to_layout, const tide_layout_world *tw, void **to_bytes,
                        uint32_t *to_size, tide_migration *m)
{
    const uint8_t *from = from_bytes;
    *m = (tide_migration){0};
    *to_bytes = NULL;
    *to_size = 0;
    old_world old = {0};
    carry c = {from_layout, to_layout, calloc(to_layout->type_count ? to_layout->type_count : 1u, sizeof(int32_t))};
    uint32_t *counts = calloc(tw->archetype_count ? tw->archetype_count : 1u, sizeof *counts);
    int32_t *goes = calloc(fw->archetype_count ? fw->archetype_count : 1u, sizeof *goes);
    uint32_t largest = 1; // A component's value, whole, on its way between lanes
    for (uint32_t i = 0; i < from_layout->type_count; i++) largest = from_layout->types[i].size > largest ? from_layout->types[i].size : largest;
    for (uint32_t i = 0; i < to_layout->type_count; i++) largest = to_layout->types[i].size > largest ? to_layout->types[i].size : largest;
    uint8_t *old_value = malloc(largest);
    uint8_t *new_value = malloc(largest);
    bool ok = c.types && counts && goes && old_value && new_value;
    if (!ok) snprintf(m->failed, sizeof m->failed, "there wasn't enough memory");
    ok = ok && read_old(from_layout, fw, from, from_size, &old, m);
    for (uint32_t i = 0; ok && i < to_layout->type_count; i++) c.types[i] = find_type(from_layout, to_layout->types[i].name);

    // A scene that's gone takes the match with it
    for (uint32_t a = 0; ok && a < fw->archetype_count; a++) {
        const char *scene = old.counts[a] ? lost_scene(&c, tw, &fw->archetypes[a]) : NULL;
        if (scene) {
            snprintf(m->failed, sizeof m->failed, "the scene %s is gone", scene);
            ok = false;
        }
    }

    // Entities keep their IDs, and go to the storage for their components, in
    // the order of the old archetypes and their rows
    for (uint32_t a = 0; ok && a < fw->archetype_count; a++) {
        const tide_layout_archetype *fa = &fw->archetypes[a];
        goes[a] = new_archetype(&c, tw, fa);
        for (uint32_t row = 0; row < old.counts[a]; row++) {
            tide_entity e;
            memcpy(&e, cell(from_layout, fa, old.rows[a], old.counts[a], 0, row), sizeof e);
            if (goes[a] < 0) {
                tide_entity_destroy(&old.entities, e);
                m->entities_dropped++;
            } else {
                tide_entity_set_location(&old.entities, e, (tide_location){(uint32_t)goes[a], counts[goes[a]]++});
            }
        }
    }

    // Tasks carry over to tables of the same name; the rest are dropped
    for (uint32_t t = 0; ok && t < fw->task_count; t++) {
        bool kept = false;
        for (uint32_t k = 0; k < tw->task_count && !kept; k++) kept = old_tasks(fw, &tw->tasks[k]) == (int32_t)t;
        if (!kept) m->tasks_dropped += old.task_counts[t];
    }

    // The new world's bytes (see tide_world_pack): its struct, the entities, the
    // tables, an empty queue, the tasks and the heap as it was
    const uint32_t empty_heap = (uint32_t)(sizeof(uint32_t) * (3u + TIDE_HEAP_CLASSES));
    uint64_t size = tw->size + (ok ? tide_entities_packed_size(&old.entities) : 0u) + 4u;
    for (uint32_t k = 0; k < tw->archetype_count; k++) size += 4u + (uint64_t)counts[k] * row_size(to_layout, &tw->archetypes[k]);
    for (uint32_t k = 0; ok && k < tw->task_count; k++) {
        const int32_t t = old_tasks(fw, &tw->tasks[k]);
        size += 4u + (t >= 0 ? (uint64_t)old.task_counts[t] * tw->tasks[k].size : 0u);
    }
    for (uint32_t k = 0; ok && k < tw->attached_count; k++) {
        const int32_t t = old_attached(&c, fw, &tw->attached[k]);
        const uint64_t row = sizeof(tide_entity) + to_layout->types[tw->attached[k].type].size + sizeof(uint32_t);
        size += 4u + (t >= 0 ? old.attached_counts[t] * row : 0u);
    }
    for (uint32_t k = 0; ok && k < tw->resident_count; k++) {
        const int32_t t = old_resident(fw, tw->residents[k]);
        size += t >= 0 ? old.resident_sizes[t] : 12u;
    }
    if (tw->heap) size += fw->heap ? old.heap_size : empty_heap;
    uint8_t *to = ok && size <= UINT32_MAX ? calloc(1, (size_t)size) : NULL;
    if (ok && !to) {
        snprintf(m->failed, sizeof m->failed, "there wasn't enough memory");
        ok = false;
    }
    if (!ok) {
        free_old(&old);
        free(c.types);
        free(counts);
        free(goes);
        free(old_value);
        free(new_value);
        return false;
    }
    tide_writer w = {to, (uint32_t)size, tw->size, false, false};

    for (uint32_t i = 0; i < tw->singleton_count; i++) {
        const tide_layout_place *ts = &tw->singletons[i];
        const tide_layout_place *fs = NULL;
        for (uint32_t k = 0; k < fw->singleton_count && c.types[ts->type] >= 0; k++) {
            if (fw->singletons[k].type == c.types[ts->type]) fs = &fw->singletons[k];
        }
        carry_value(&c, ts->type, fs ? fs->type : -1, to + ts->offset, fs ? from + fs->offset : NULL);
    }

    // The input, whatever its name
    if (tw->input >= 0) {
        const uint32_t input = to_layout->types[tw->input].size;
        const uint32_t old_input = fw->input >= 0 ? from_layout->types[fw->input].size : 0u;
        // The players' by index, and the server's, the slot after theirs in
        // each: a build that takes another number of players has it elsewhere
        const uint32_t server = tw->input_count ? tw->input_count - 1u : 0u;
        const uint32_t old_server = fw->input >= 0 && fw->input_count ? fw->input_count - 1u : 0u;
        for (uint32_t i = 0; i < tw->input_count; i++) {
            const uint32_t at = i == server ? old_server : i;
            const bool had = fw->input >= 0 && fw->input_count && (i == server || i < old_server);
            carry_value(&c, tw->input, fw->input, to + tw->inputs + i * input, had ? from + fw->inputs + at * old_input : NULL);
            carry_value(&c, tw->input, fw->input, to + tw->previous + i * input,
                        had ? from + fw->previous + at * old_input : NULL);
        }
    }

    tide_entities_pack(&old.entities, &w);

    for (uint32_t k = 0; k < tw->archetype_count; k++) {
        const tide_layout_archetype *ta = &tw->archetypes[k];
        tide_write_u32(&w, counts[k]);
        for (uint32_t column = 0; column < column_count(to_layout, ta); column++) {
            const uint32_t bytes = column_size(to_layout, ta, column);
            const tide_layout_place *tc = component_at(to_layout, ta, column);
            for (uint32_t a = 0; a < fw->archetype_count; a++) {
                if (goes[a] != (int32_t)k) continue;
                const tide_layout_archetype *fa = &fw->archetypes[a];
                for (uint32_t row = 0; row < old.counts[a]; row++) {
                    uint8_t *value = take(&w, bytes);
                    if (tc) { // The whole value carried over, for the lane of it this column holds
                        const tide_layout_place *fc = old_component(&c, fa, tc->type);
                        // A sparse component the old entity hadn't got stays absent: zeros, presence 0
                        const bool had = fc && present(from_layout, fa, old.rows[a], old.counts[a], fc, row);
                        if (is_presence(tc, column)) {
                            *value = had ? 1u : 0u;
                            continue;
                        }
                        if (!had) {
                            if (!tc->sparse) carry_value(&c, tc->type, -1, new_value, NULL); // New here: its defaults
                            else continue;
                        } else {
                            gather(from_layout, fa, old.rows[a], old.counts[a], fc, row, old_value);
                            carry_value(&c, tc->type, fc->type, new_value, old_value);
                        }
                        memcpy(value, new_value + (column - tc->offset - (tc->sparse ? 1u : 0u)) * bytes, bytes);
                    } else if (column == 0 || fa->scenes) { // The entity, and the scene it's in
                        memcpy(value, cell(from_layout, fa, old.rows[a], old.counts[a], column, row), bytes);
                    }
                }
            }
        }
    }
    tide_write_u32(&w, 0); // No changes waiting
    for (uint32_t k = 0; k < tw->task_count; k++) {
        const int32_t t = old_tasks(fw, &tw->tasks[k]);
        tide_write_u32(&w, t >= 0 ? old.task_counts[t] : 0u);
        if (t >= 0) tide_write_bytes(&w, old.task_rows[t], old.task_counts[t] * tw->tasks[k].size);
    }
    // The tasks' order, and the frames local ones count, go on from where they were
    if (tw->task_count && fw->task_count) {
        memcpy(to + tw->task_order, from + fw->task_order, sizeof(uint32_t));
        if (tw->frames && fw->frames) memcpy(to + tw->frames, from + fw->frames, sizeof(int32_t));
    }
    // The local fields kept for the match's entities carry over like any value,
    // by their component's name: the entities as they are, each one's value,
    // then what each was born as
    for (uint32_t k = 0; k < tw->attached_count; k++) {
        const int32_t t = old_attached(&c, fw, &tw->attached[k]);
        const uint32_t count = t >= 0 ? old.attached_counts[t] : 0u;
        tide_write_u32(&w, count);
        if (!count) continue;
        const int32_t new_type = tw->attached[k].type;
        const int32_t old_type = fw->attached[t].type;
        const uint32_t old_size = from_layout->types[old_type].size;
        tide_write_bytes(&w, old.attached_rows[t], count * (uint32_t)sizeof(tide_entity));
        const uint8_t *values = old.attached_rows[t] + (size_t)count * sizeof(tide_entity);
        for (uint32_t row = 0; row < count; row++) {
            carry_value(&c, new_type, old_type, take(&w, to_layout->types[new_type].size), values + (size_t)row * old_size);
        }
        tide_write_bytes(&w, values + (size_t)count * old_size, count * (uint32_t)sizeof(uint32_t));
    }
    // Resident state carries over as its canonical bytes, by its field's name; a new one holds nothing
    for (uint32_t k = 0; k < tw->resident_count; k++) {
        const int32_t t = old_resident(fw, tw->residents[k]);
        if (t >= 0) {
            tide_write_bytes(&w, old.resident_parts[t], old.resident_sizes[t]);
        } else {
            tide_write_u64(&w, 0);
            tide_write_u32(&w, 0);
        }
    }
    if (tw->heap && fw->heap) tide_write_bytes(&w, old.heap, old.heap_size);
    else if (tw->heap) take(&w, empty_heap); // Zeros: an empty heap

    free_old(&old);
    free(c.types);
    free(counts);
    free(goes);
    free(old_value);
    free(new_value);
    *to_bytes = to;
    *to_size = w.size;
    return true;
}

uint32_t tide_layout_fields_reset(const tide_layout *from, const tide_layout *to)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < to->type_count; i++) {
        const int32_t old = find_type(from, to->types[i].name);
        if (old < 0) continue;
        const tide_layout_type *tt = &to->types[i];
        for (uint32_t k = 0; k < tt->field_count; k++) {
            const tide_layout_field *ff = find_field(&from->types[old], tt->fields[k].name);
            n += ff && !carries(&tt->fields[k], ff);
        }
    }
    return n;
}

// ---------------------------------------------------------------------------
// A packed layout: the structures one after another, 8-aligned, with each
// pointer an offset from the block's start plus one (0 for NULL).

typedef struct packer {
    uint8_t *data;
    uint32_t size;
    uint32_t cap;
    bool failed;
} packer;

// Where `n` bytes went, copied from `bytes` (NULL: zeros).
static uint32_t put(packer *p, const void *bytes, const uint32_t n)
{
    const uint32_t at = (p->size + 7u) & ~7u;
    if (p->failed) return 0;
    if (at + n > p->cap) {
        uint32_t cap = p->cap ? p->cap : 4096u;
        while (cap < at + n) cap *= 2u;
        uint8_t *grown = realloc(p->data, cap);
        if (!grown) {
            p->failed = true;
            return 0;
        }
        memset(grown + p->cap, 0, cap - p->cap);
        p->data = grown;
        p->cap = cap;
    }
    if (bytes && n) memcpy(p->data + at, bytes, n);
    p->size = at + n;
    return at;
}

// The pointer at `at` becomes one to `target`.
static void point(packer *p, const uint32_t at, const uint32_t target)
{
    const uintptr_t v = (uintptr_t)target + 1u;
    if (!p->failed) memcpy(p->data + at, &v, sizeof v);
}

// The pointer at `at` becomes one to an array copied from `items`, or NULL
// when there are none. Returns where the array went.
static uint32_t put_array(packer *p, const uint32_t at, const void *items, const uint32_t count, const size_t size)
{
    const uintptr_t none = 0;
    if (!count) {
        if (!p->failed) memcpy(p->data + at, &none, sizeof none);
        return 0;
    }
    const uint32_t array = put(p, items, count * (uint32_t)size);
    point(p, at, array);
    return array;
}

static void put_string(packer *p, const uint32_t at, const char *s)
{
    point(p, at, put(p, s, (uint32_t)strlen(s) + 1u));
}

static void pack_world(packer *p, const uint32_t at, const tide_layout_world *w)
{
    put_array(p, at + offsetof(tide_layout_world, singletons), w->singletons, w->singleton_count,
              sizeof *w->singletons);
    const uint32_t archetypes = put_array(p, at + offsetof(tide_layout_world, archetypes), w->archetypes,
                                          w->archetype_count, sizeof *w->archetypes);
    for (uint32_t i = 0; i < w->archetype_count; i++) {
        const uint32_t a = archetypes + i * (uint32_t)sizeof(tide_layout_archetype);
        put_array(p, a + offsetof(tide_layout_archetype, components), w->archetypes[i].components,
                  w->archetypes[i].component_count, sizeof *w->archetypes[i].components);
    }
    const uint32_t tasks = put_array(p, at + offsetof(tide_layout_world, tasks), w->tasks, w->task_count, sizeof *w->tasks);
    for (uint32_t i = 0; i < w->task_count; i++) {
        put_string(p, tasks + i * (uint32_t)sizeof(tide_layout_tasks) + offsetof(tide_layout_tasks, name), w->tasks[i].name);
    }
    put_array(p, at + offsetof(tide_layout_world, attached), w->attached, w->attached_count, sizeof *w->attached);
    const uint32_t residents = put_array(p, at + offsetof(tide_layout_world, residents), w->residents, w->resident_count,
                                         sizeof *w->residents);
    for (uint32_t i = 0; i < w->resident_count; i++) put_string(p, residents + i * (uint32_t)sizeof(const char *), w->residents[i]);
}

void *tide_layout_pack(const tide_layout *l, uint32_t *size)
{
    packer p = {0};
    const uint32_t root = put(&p, l, sizeof *l);
    const uint32_t types = put_array(&p, root + offsetof(tide_layout, types), l->types, l->type_count, sizeof *l->types);
    for (uint32_t i = 0; i < l->type_count; i++) {
        const tide_layout_type *type = &l->types[i];
        const uint32_t t = types + i * (uint32_t)sizeof *type;
        put_string(&p, t + offsetof(tide_layout_type, name), type->name);
        const uint32_t fields = put_array(&p, t + offsetof(tide_layout_type, fields), type->fields, type->field_count,
                                          sizeof *type->fields);
        if (!p.failed) memset(p.data + t + offsetof(tide_layout_type, defaults), 0, sizeof type->defaults);
        for (uint32_t k = 0; k < type->field_count; k++) {
            const uint32_t f = fields + k * (uint32_t)sizeof *type->fields;
            put_string(&p, f + offsetof(tide_layout_field, name), type->fields[k].name);
            put_string(&p, f + offsetof(tide_layout_field, type), type->fields[k].type);
        }
    }
    const uint32_t enums = put_array(&p, root + offsetof(tide_layout, enums), l->enums, l->enum_count, sizeof *l->enums);
    for (uint32_t i = 0; i < l->enum_count; i++) {
        const tide_layout_enum *e = &l->enums[i];
        const uint32_t at = enums + i * (uint32_t)sizeof *e;
        put_string(&p, at + offsetof(tide_layout_enum, name), e->name);
        const uint32_t members = put_array(&p, at + offsetof(tide_layout_enum, members), e->members, e->member_count,
                                           sizeof *e->members);
        for (uint32_t k = 0; k < e->member_count; k++) {
            put_string(&p, members + k * (uint32_t)sizeof *e->members + offsetof(tide_layout_member, name),
                       e->members[k].name);
        }
    }
    pack_world(&p, root + offsetof(tide_layout, match), &l->match);
    pack_world(&p, root + offsetof(tide_layout, local), &l->local);
    if (p.failed) {
        free(p.data);
        return NULL;
    }
    *size = p.size;
    return p.data;
}

// A packed pointer, at `field`, made a real one to `count` items of `size`
// bytes in the block. False if they aren't in it.
static bool unpack_pointer(uint8_t *block, const uint32_t block_size, void *field, const uint32_t count,
                           const size_t size)
{
    uintptr_t v;
    memcpy(&v, field, sizeof v);
    if (v == 0) return count == 0;
    const uintptr_t at = v - 1u;
    if (at > block_size || (block_size - at) / (size ? size : 1u) < count) return false;
    void *target = block + at;
    memcpy(field, &target, sizeof target);
    return true;
}

// A packed string: in the block, and ending in it.
static bool unpack_string(uint8_t *block, const uint32_t block_size, const char **field)
{
    if (!unpack_pointer(block, block_size, (void *)field, 1, 1)) return false;
    return memchr(*field, '\0', block_size - (uint32_t)((const uint8_t *)*field - block)) != NULL;
}

static bool unpack_world(uint8_t *b, const uint32_t size, tide_layout_world *w)
{
    if (!unpack_pointer(b, size, (void *)&w->singletons, w->singleton_count, sizeof *w->singletons)) return false;
    if (!unpack_pointer(b, size, (void *)&w->archetypes, w->archetype_count, sizeof *w->archetypes)) return false;
    tide_layout_archetype *archetypes = (tide_layout_archetype *)w->archetypes;
    for (uint32_t i = 0; i < w->archetype_count; i++) {
        tide_layout_archetype *a = &archetypes[i];
        if (!unpack_pointer(b, size, (void *)&a->components, a->component_count, sizeof *a->components)) return false;
    }
    if (!unpack_pointer(b, size, (void *)&w->tasks, w->task_count, sizeof *w->tasks)) return false;
    tide_layout_tasks *tasks = (tide_layout_tasks *)w->tasks;
    for (uint32_t i = 0; i < w->task_count; i++) {
        if (!unpack_string(b, size, &tasks[i].name)) return false;
    }
    if (!unpack_pointer(b, size, (void *)&w->attached, w->attached_count, sizeof *w->attached)) return false;
    if (!unpack_pointer(b, size, (void *)&w->residents, w->resident_count, sizeof *w->residents)) return false;
    const char **residents = (const char **)w->residents;
    for (uint32_t i = 0; i < w->resident_count; i++) {
        if (!unpack_string(b, size, &residents[i])) return false;
    }
    return true;
}

const tide_layout *tide_layout_unpack(void *block, const uint32_t size)
{
    uint8_t *b = block;
    if (size < sizeof(tide_layout)) return NULL;
    tide_layout *l = block;
    if (!unpack_pointer(b, size, (void *)&l->types, l->type_count, sizeof *l->types)) return NULL;
    if (!unpack_pointer(b, size, (void *)&l->enums, l->enum_count, sizeof *l->enums)) return NULL;
    tide_layout_type *types = (tide_layout_type *)l->types;
    for (uint32_t i = 0; i < l->type_count; i++) {
        tide_layout_type *t = &types[i];
        if (!unpack_string(b, size, &t->name)) return NULL;
        if (!unpack_pointer(b, size, (void *)&t->fields, t->field_count, sizeof *t->fields)) return NULL;
        tide_layout_field *fields = (tide_layout_field *)t->fields;
        for (uint32_t k = 0; k < t->field_count; k++) {
            if (!unpack_string(b, size, &fields[k].name) || !unpack_string(b, size, &fields[k].type)) return NULL;
        }
    }
    tide_layout_enum *enums = (tide_layout_enum *)l->enums;
    for (uint32_t i = 0; i < l->enum_count; i++) {
        tide_layout_enum *e = &enums[i];
        if (!unpack_string(b, size, &e->name)) return NULL;
        if (!unpack_pointer(b, size, (void *)&e->members, e->member_count, sizeof *e->members)) return NULL;
        tide_layout_member *members = (tide_layout_member *)e->members;
        for (uint32_t k = 0; k < e->member_count; k++) {
            if (!unpack_string(b, size, &members[k].name)) return NULL;
        }
    }
    if (!unpack_world(b, size, &l->match) || !unpack_world(b, size, &l->local)) return NULL;
    return l;
}
