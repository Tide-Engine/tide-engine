#include "tide/draw_profile.h"

#include <stdlib.h>
#include <string.h>

#include "tide/draw.h"
#include "tide/page.h"

// The geometry rows by a hash of their keys (key_hash), each 1 + its row; 0 is an empty place
typedef struct geometry_entry {
    uint64_t hash;
    uint32_t row;
} geometry_entry;

typedef struct tide_draw_geometry_index {
    geometry_entry *entries;
    uint32_t capacity; // A power of two
} tide_draw_geometry_index;

#define GEOMETRY_INDEX_CAPACITY (TIDE_DRAW_PROFILE_ROWS * 4u)

static const char *source_name(const tide_profile_source *source)
{
    return source && source->name ? source->name : "(unattributed)";
}

static const char *source_file(const tide_profile_source *source)
{
    return source && source->file ? source->file : "";
}

static bool same_source(const tide_draw_diagnostic *row, const tide_profile_source *source)
{
    return row->source.line == (source ? source->line : 0u)
        && row->source.column == (source ? source->column : 0u)
        && strcmp(row->source.name, source_name(source)) == 0
        && strcmp(row->source.file, source_file(source)) == 0;
}

static bool this_frame(const tide_draw_diagnostic *row, const tide_draw_list *d)
{
    return row->list_identity == (uintptr_t)d && row->frame == d->frame;
}

static tide_draw_diagnostic *diagnostic_row(tide_draw_list *d)
{
    tide_draw_diagnostics *p = d->diagnostics;
    const uint64_t target = d->target && d->target <= d->target_count ? d->targets[d->target - 1u].id : 0u;
    const tide_profile_source *source = d->profile_source;
    if (p->last && p->last <= p->count) {
        tide_draw_diagnostic *row = &p->rows[p->last - 1u];
        if (row->target == target && this_frame(row, d) && row->camera_index == d->place.camera_3d
            && same_source(row, source)) return row;
    }
    uint32_t slot = 1;
    for (uint32_t i = 0; i < p->count; i++) {
        tide_draw_diagnostic *row = &p->rows[i];
        if (row->target != target || !same_source(row, source) || !this_frame(row, d)) continue;
        if (row->camera_index == d->place.camera_3d) {
            p->last = i + 1u;
            return row;
        }
        if (row->camera_slot >= slot) slot = row->camera_slot + 1u;
    }
    for (uint32_t i = 0; i < p->count; i++) {
        tide_draw_diagnostic *row = &p->rows[i];
        if (row->target != target || row->camera_slot != slot || !same_source(row, source)) continue;
        row->list_identity = (uintptr_t)d;
        row->frame = d->frame;
        row->camera_index = d->place.camera_3d;
        p->last = i + 1u;
        return row;
    }
    if (p->count == TIDE_DRAW_PROFILE_ROWS) return NULL;
    const char *name = source_name(source), *file = source_file(source);
    const size_t name_bytes = strlen(name) + 1u, file_bytes = strlen(file) + 1u;
    const size_t remaining = TIDE_DRAW_PROFILE_SOURCE_BYTES - p->source_bytes;
    if (name_bytes > remaining || file_bytes > remaining - name_bytes) return NULL;
    if (!p->rows) {
        p->rows = tide_alloc(TIDE_DRAW_PROFILE_ROWS * sizeof *p->rows);
        memset(p->rows, 0, TIDE_DRAW_PROFILE_ROWS * sizeof *p->rows);
    }
    char *copy = tide_alloc(name_bytes + file_bytes);
    memcpy(copy, name, name_bytes);
    memcpy(copy + name_bytes, file, file_bytes);
    tide_draw_diagnostic *row = &p->rows[p->count++];
    *row = (tide_draw_diagnostic){
        .source = {.name = copy, .file = copy + name_bytes,
                   .line = source ? source->line : 0u, .column = source ? source->column : 0u},
        .target = target, .camera_slot = slot,
        .list_identity = (uintptr_t)d, .frame = d->frame, .camera_index = d->place.camera_3d,
    };
    p->source_bytes += (uint32_t)(name_bytes + file_bytes);
    p->last = p->count;
    return row;
}

void tide_draw_diagnostics_clear(tide_draw_diagnostics *p)
{
    if (!p) return;
    for (uint32_t i = 0; i < p->count; i++) free((void *)p->rows[i].source.name);
    for (uint32_t i = 0; i < p->geometry_count; i++) free((void *)p->geometry[i].source.name);
    p->revision++;
    // The rows go: so does what found them
    if (p->geometry_index) memset(p->geometry_index->entries, 0, (size_t)p->geometry_index->capacity * sizeof *p->geometry_index->entries);
    p->geometry_count = p->geometry_bytes = 0;
    p->dropped_geometry_instances = 0;
    p->count = p->last = p->source_bytes = 0;
    p->dropped_calls = p->dropped_candidates = 0;
}

void tide_draw_diagnostics_free(tide_draw_diagnostics *p)
{
    if (!p) return;
    tide_draw_diagnostics_clear(p);
    free(p->rows);
    free(p->geometry);
    if (p->geometry_index) free(p->geometry_index->entries);
    free(p->geometry_index);
    *p = (tide_draw_diagnostics){0};
}

tide_draw_diagnostic_scope tide_draw_diagnostics_begin(tide_draw_list *d, const uint32_t candidates)
{
    tide_draw_diagnostic_scope scope = {.previous = d->profile_active};
    if (!d->diagnostics || !candidates) return scope;
    scope.row = diagnostic_row(d);
    d->profile_active = scope.row;
    if (scope.row) {
        scope.row->calls++;
        scope.row->candidates += candidates;
        scope.began = tide_time_now_ns();
    } else {
        d->diagnostics->dropped_calls++;
        d->diagnostics->dropped_candidates += candidates;
    }
    return scope;
}

void tide_draw_diagnostics_end(tide_draw_list *d, const tide_draw_diagnostic_scope scope)
{
    if (scope.row) {
        const uint64_t now = tide_time_now_ns();
        if (now > scope.began) scope.row->mesh_recording_ns += now - scope.began;
    }
    d->profile_active = scope.previous;
}

static uint64_t text_hash(uint64_t h, const char *text)
{
    for (; *text; text++) {
        h ^= (uint8_t)*text;
        h *= 0x100000001B3ull;
    }
    return h * 0x100000001B3ull; // Its end too, so "ab" + "c" isn't "a" + "bc"
}

// A key's hash, of its names' text and its numbers: what a row is found by in the index
static uint64_t key_hash(const tide_draw_geometry *key, const char *name, const char *file)
{
    uint64_t h = text_hash(text_hash(text_hash(0xCBF29CE484222325ull, name), file), key->mesh);
    const uint64_t parts[] = {key->identity, key->target, key->geometry_id, key->geometry_other,
                              (uint64_t)key->lod << 32 | key->mesh_vertices,
                              (uint64_t)key->mesh_indices << 32 | key->source.line, key->source.column};
    for (size_t i = 0; i < sizeof parts / sizeof parts[0]; i++) {
        h ^= parts[i];
        h *= 0xBF58476D1CE4E5B9ull;
        h ^= h >> 31;
    }
    return h;
}

static bool same_row(const tide_draw_geometry *r, const tide_draw_geometry *key, const char *name, const char *file)
{
    return r->geometry_id == key->geometry_id && r->geometry_other == key->geometry_other && r->identity == key->identity
        && r->target == key->target && r->lod == key->lod && r->mesh_vertices == key->mesh_vertices
        && r->mesh_indices == key->mesh_indices && r->source.line == key->source.line
        && r->source.column == key->source.column && !strcmp(r->mesh, key->mesh) && !strcmp(r->source.name, name)
        && !strcmp(r->source.file, file);
}

// Where a key's row is in the index, or the empty place it would go in: the index has every row, so a key it
// hasn't is a key no row has, and a draw never goes through the rows one by one
static geometry_entry *index_slot(const tide_draw_diagnostics *p, const uint64_t hash, const tide_draw_geometry *key,
                                  const char *name, const char *file)
{
    const tide_draw_geometry_index *x = p->geometry_index;
    const uint32_t mask = x->capacity - 1u;
    for (uint32_t at = (uint32_t)hash & mask;; at = (at + 1u) & mask) {
        geometry_entry *e = &x->entries[at];
        if (!e->row || (e->hash == hash && same_row(&p->geometry[e->row - 1u], key, name, file))) return e;
    }
}

static uint32_t geometry_row(tide_draw_diagnostics *p, const tide_draw_geometry *key)
{
    const char *name = source_name(&key->source), *file = source_file(&key->source);
    if (!p->geometry_index) { // Room for every row at under half full: it's never made bigger
        p->geometry_index = tide_alloc(sizeof *p->geometry_index);
        p->geometry_index->capacity = GEOMETRY_INDEX_CAPACITY;
        p->geometry_index->entries = tide_alloc(GEOMETRY_INDEX_CAPACITY * sizeof *p->geometry_index->entries);
        memset(p->geometry_index->entries, 0, GEOMETRY_INDEX_CAPACITY * sizeof *p->geometry_index->entries);
    }
    const uint64_t hash = key_hash(key, name, file);
    geometry_entry *e = index_slot(p, hash, key, name, file);
    if (e->row) return e->row;
    const size_t n = strlen(name) + 1, f = strlen(file) + 1, m = strlen(key->mesh) + 1;
    const size_t left = TIDE_DRAW_PROFILE_SOURCE_BYTES - p->geometry_bytes;
    if (p->geometry_count == TIDE_DRAW_PROFILE_ROWS || n > left || f > left - n || m > left - n - f) return 0;
    if (!p->geometry) p->geometry = tide_alloc(TIDE_DRAW_PROFILE_ROWS * sizeof *p->geometry);
    char *copy = tide_alloc(n + f + m);
    memcpy(copy, name, n);
    memcpy(copy + n, file, f);
    memcpy(copy + n + f, key->mesh, m);
    tide_draw_geometry *r = &p->geometry[p->geometry_count++];
    *r = *key;
    r->source.name = copy;
    r->source.file = copy + n;
    r->mesh = copy + n + f;
    r->instances = r->vertices = r->triangles = 0;
    p->geometry_bytes += (uint32_t)(n + f + m);
    *e = (geometry_entry){.hash = hash, .row = p->geometry_count};
    return p->geometry_count;
}

static void geometry_link(tide_draw_list *d, tide_draw_command *c, const tide_draw_geometry *key, const uint32_t count)
{
    tide_draw_diagnostics *p = d->diagnostics;
    if (d->profile_revision != p->revision) {
        for (uint32_t i = 0; i < d->count; i++) {
            tide_draw_command *each = &d->commands[i];
            if (each->kind == TIDE_DRAW_MESH_3D || each->kind == TIDE_DRAW_SHADED || each->kind == TIDE_DRAW_BLIT)
                each->profile_geometry = 0;
        }
        d->profile_link_count = 0;
        d->profile_revision = p->revision;
    }
    const uint32_t row = geometry_row(p, key);
    if (row && c->profile_geometry) {
        tide_draw_geometry_link *last = &d->profile_links[c->profile_geometry - 1u];
        if (last->row == row && count <= UINT32_MAX - last->count) { last->count += count; return; }
    }
    if (!row || d->profile_link_count == TIDE_DRAW_PROFILE_LINKS) {
        p->dropped_geometry_instances += count;
        return;
    }
    if (d->profile_link_count == d->profile_link_capacity) {
        const uint32_t old = d->profile_link_capacity;
        d->profile_link_capacity = old ? old * 2u : 256u;
        d->profile_links = tide_realloc(d->profile_links, (size_t)old * sizeof *d->profile_links,
                                        (size_t)d->profile_link_capacity * sizeof *d->profile_links);
    }
    d->profile_links[d->profile_link_count++] = (tide_draw_geometry_link){row, count, c->profile_geometry};
    c->profile_geometry = d->profile_link_count;
}

// The key of a row for draws of the list's mesh `mesh` recorded now.
static tide_draw_geometry geometry_key(tide_draw_list *d, const uint32_t mesh)
{
    const tide_draw_mesh_data *m = &d->meshes[mesh - 1u];
    tide_draw_mesh_data *base = m->base && m->base <= d->mesh_count ? &d->meshes[m->base - 1u] : &d->meshes[mesh - 1u];
    // The generated name search goes through every mesh asset: asked once for each mesh, not for each draw
    if (base->profile_named_by != d->profile_mesh_name || base->profile_named_vertices != base->profile_vertices
        || base->profile_named_indices != base->profile_indices || base->profile_named_version != base->version) {
        base->profile_name = d->profile_mesh_name ? d->profile_mesh_name(base->profile_vertices, base->profile_indices) : NULL;
        base->profile_named_by = d->profile_mesh_name;
        base->profile_named_vertices = base->profile_vertices;
        base->profile_named_indices = base->profile_indices;
        base->profile_named_version = base->version;
    }
    const char *name = base->profile_name;
    const tide_draw_geometry key = {
        .source = d->profile_source ? *d->profile_source : (tide_profile_source){0},
        .mesh = name ? name : "(generated or C mesh)", .identity = base->version,
        .geometry_id = base->id, .geometry_other = base->other,
        .target = d->target && d->target <= d->target_count ? d->targets[d->target - 1u].id : 0,
        .lod = m->level, .mesh_vertices = m->vertex_count, .mesh_indices = m->index_count,
    };
    return key;
}

void tide_draw_geometry_record(tide_draw_list *d, tide_draw_command *c, const uint32_t mesh, const uint32_t count)
{
    if (!d->diagnostics || d->profile_copying || !count || !mesh || mesh > d->mesh_count) return;
    const tide_draw_geometry key = geometry_key(d, mesh);
    geometry_link(d, c, &key, count);
}

uint32_t tide_draw_geometry_row_of(tide_draw_list *d, const uint32_t mesh)
{
    if (!d->diagnostics || d->profile_copying || !mesh || mesh > d->mesh_count) return 0;
    const tide_draw_geometry key = geometry_key(d, mesh);
    return geometry_row(d->diagnostics, &key);
}

void tide_draw_geometry_add(tide_draw_diagnostics *p, const uint64_t revision, const uint32_t row, const uint64_t instances)
{
    if (!p || p->revision != revision || !row || row > p->geometry_count || !instances) return;
    tide_draw_geometry *r = &p->geometry[row - 1u];
    r->instances += instances;
    r->vertices += (uint64_t)r->mesh_indices * instances;
    r->triangles += (uint64_t)(r->mesh_indices / 3u) * instances;
}

void tide_draw_geometry_submit(const tide_draw_list *d, uint32_t chain)
{
    if (!d->diagnostics || d->profile_revision != d->diagnostics->revision) return;
    while (chain && chain <= d->profile_link_count) {
        const tide_draw_geometry_link *link = &d->profile_links[chain - 1u];
        if (link->row && link->row <= d->diagnostics->geometry_count) {
            tide_draw_geometry *r = &d->diagnostics->geometry[link->row - 1u];
            r->instances += link->count;
            r->vertices += (uint64_t)r->mesh_indices * link->count;
            r->triangles += (uint64_t)(r->mesh_indices / 3u) * link->count;
        }
        chain = link->next;
    }
}

uint32_t tide_draw_geometry_join(const tide_draw_list *d, const uint32_t a, const uint32_t b)
{
    if (!a) return b;
    uint32_t at = a;
    while (d->profile_links[at - 1u].next) at = d->profile_links[at - 1u].next;
    d->profile_links[at - 1u].next = b;
    return a;
}

void tide_draw_geometry_copy(tide_draw_list *d, tide_draw_command *to, const tide_draw_list *from, uint32_t chain)
{
    if (!d->diagnostics || !from->diagnostics || from->profile_revision != from->diagnostics->revision) return;
    while (chain && chain <= from->profile_link_count) {
        const tide_draw_geometry_link link = from->profile_links[chain - 1u];
        if (link.row && link.row <= from->diagnostics->geometry_count)
            geometry_link(d, to, &from->diagnostics->geometry[link.row - 1u], link.count);
        chain = link.next;
    }
}
