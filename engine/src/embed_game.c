// A frame's view packet, as a game's generated C writes it (tide/embed_game.h).

#include "tide/embed_game.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/page.h"

// Room for `more` bytes past `*size` in a buffer of `*capacity`, which grows by doubling; false past 4 GiB.
static bool room(uint8_t **bytes, uint32_t *capacity, const uint32_t size, const uint32_t more)
{
    if ((uint64_t)size + more > UINT32_MAX - 8u) return false;
    if (size + more <= *capacity) return true;
    uint64_t grown = *capacity ? *capacity : 4096u;
    while (grown < (uint64_t)size + more) grown *= 2u;
    if (grown > UINT32_MAX - 8u) grown = UINT32_MAX - 8u;
    *bytes = tide_realloc(*bytes, *capacity, (size_t)grown);
    *capacity = (uint32_t)grown;
    return true;
}

static void put_u32(uint8_t *at, const uint32_t v)
{
    memcpy(at, &v, sizeof v);
}

void tide_packet_begin(tide_packet *p)
{
    p->size = 0;
    p->sections = 0;
    p->text_size = 0;
    p->entry_count = 0;
    tide_packet_grow(p, TIDE_PACKET_HEADER, false);
}

uint8_t *tide_packet_grow(tide_packet *p, const uint32_t size, const bool align8)
{
    const uint32_t start = align8 ? (p->size + 7u) & ~7u : p->size;
    if (!room(&p->bytes, &p->capacity, p->size, (start - p->size) + size)) tide_out_of_memory();
    memset(p->bytes + p->size, 0, (size_t)(start - p->size) + size);
    p->size = start + size;
    return p->bytes + start;
}

uint8_t *tide_packet_section(tide_packet *p, const tide_packet_kind kind, const uint32_t type, const uint32_t count,
                             const uint32_t stride, uint32_t **rows)
{
    const bool has_rows = kind == TIDE_PACKET_COMPONENT || kind == TIDE_PACKET_COMPONENT_LOCAL;
    const uint64_t row_bytes = has_rows ? ((uint64_t)count * 4u + 7u) & ~7ull : 0u;
    const uint64_t bytes = row_bytes + (uint64_t)count * stride;
    if (bytes > UINT32_MAX / 2u) tide_out_of_memory();
    uint8_t *header = tide_packet_grow(p, TIDE_PACKET_SECTION + (uint32_t)bytes, true);
    put_u32(header, (uint32_t)kind);
    put_u32(header + 4, type);
    put_u32(header + 8, count);
    put_u32(header + 12, stride);
    put_u32(header + 16, (uint32_t)bytes);
    p->sections++;
    if (rows) *rows = has_rows ? (uint32_t *)(void *)(header + TIDE_PACKET_SECTION) : NULL;
    return header + TIDE_PACKET_SECTION + row_bytes;
}

// An entry for `size` bytes at `offset` of STRINGS' bytes: what a field holds for them.
static uint32_t add_entry(tide_packet *p, const uint32_t offset, const uint32_t size)
{
    if (p->entry_count + 1u >= p->entry_capacity) {
        const uint32_t grown = p->entry_capacity ? p->entry_capacity * 2u : 256u;
        if (grown < p->entry_capacity || grown > UINT32_MAX / 8u) tide_out_of_memory();
        p->entries = tide_realloc(p->entries, (size_t)p->entry_capacity * 2u * sizeof *p->entries,
                                  (size_t)grown * 2u * sizeof *p->entries);
        p->entry_capacity = grown;
    }
    p->entries[p->entry_count * 2u] = offset;
    p->entries[p->entry_count * 2u + 1u] = size;
    return ++p->entry_count;
}

// STRINGS' bytes to a multiple of 8, zeroed, so each entry starts where any element can be read in place.
static void align_text(tide_packet *p)
{
    const uint32_t pad = (8u - (p->text_size & 7u)) & 7u;
    if (!pad) return;
    if (!room(&p->text, &p->text_capacity, p->text_size, pad)) tide_out_of_memory();
    memset(p->text + p->text_size, 0, pad);
    p->text_size += pad;
}

uint32_t tide_packet_text(tide_packet *p, const void *bytes, const uint32_t size)
{
    if (!size || !bytes) return 0;
    align_text(p);
    if (!room(&p->text, &p->text_capacity, p->text_size, size)) tide_out_of_memory();
    memcpy(p->text + p->text_size, bytes, size);
    const uint32_t offset = p->text_size;
    p->text_size += size;
    return add_entry(p, offset, size);
}

uint32_t tide_packet_list(tide_packet *p, const tide_list list, const uint32_t element)
{
    const int32_t count = tide_list_count(list);
    if (count <= 0 || !element || (uint64_t)count * element > UINT32_MAX / 4u) return 0;
    const uint32_t size = (uint32_t)count * element;
    align_text(p);
    if (!room(&p->text, &p->text_capacity, p->text_size, size)) tide_out_of_memory();
    uint8_t *to = p->text + p->text_size;
    for (int32_t i = 0; i < count; i++) {
        const void *at = tide_list_at(list, i, element);
        if (at) memcpy(to + (size_t)i * element, at, element);
        else memset(to + (size_t)i * element, 0, element);
    }
    const uint32_t offset = p->text_size;
    p->text_size += size;
    return add_entry(p, offset, size);
}

uint32_t *tide_packet_places(tide_packet *p, const uint32_t count)
{
    if (count > p->place_capacity) {
        p->place = tide_realloc(p->place, (size_t)p->place_capacity * sizeof *p->place, (size_t)count * sizeof *p->place);
        p->place_capacity = count;
    }
    memset(p->place, 0xFF, (size_t)count * sizeof *p->place);
    return p->place;
}

void tide_packet_end(tide_packet *p, const uint64_t hash, const bool in_match, const uint32_t tick, const uint32_t verified_tick,
                     const float alpha, const uint32_t entities)
{
    if (p->entry_count) {
        const uint64_t bytes = (uint64_t)p->entry_count * 8u + p->text_size;
        if (bytes > UINT32_MAX / 2u) tide_out_of_memory();
        uint8_t *header = tide_packet_grow(p, TIDE_PACKET_SECTION + (uint32_t)bytes, true);
        put_u32(header, TIDE_PACKET_STRINGS);
        put_u32(header + 8, p->entry_count);
        put_u32(header + 12, 8u);
        put_u32(header + 16, (uint32_t)bytes);
        memcpy(header + TIDE_PACKET_SECTION, p->entries, (size_t)p->entry_count * 8u);
        memcpy(header + TIDE_PACKET_SECTION + (size_t)p->entry_count * 8u, p->text, p->text_size);
        p->sections++;
    }
    uint8_t *h = p->bytes;
    put_u32(h, TIDE_PACKET_MAGIC);
    put_u32(h + 4, TIDE_PACKET_VERSION);
    memcpy(h + 8, &hash, sizeof hash);
    put_u32(h + 16, p->size);
    put_u32(h + 20, in_match ? 1u : 0u);
    put_u32(h + 24, tick);
    put_u32(h + 28, verified_tick);
    memcpy(h + 32, &alpha, sizeof alpha);
    put_u32(h + 36, entities);
    put_u32(h + 40, p->sections);
    put_u32(h + 44, 0);
}

void tide_packet_free(tide_packet *p)
{
    free(p->bytes);
    free(p->text);
    free(p->entries);
    free(p->place);
    *p = (tide_packet){0};
}

// ---------------------------------------------------------------------------
// The schema: the layout's types (tide/layout.h), and what the game said of
// them for another program, as JSON.

typedef struct json {
    char *bytes;
    uint32_t size, capacity;
} json;

static void put(json *j, const char *text, const size_t n)
{
    if ((uint64_t)j->size + n + 1u > UINT32_MAX / 2u) tide_out_of_memory();
    if (j->size + n + 1u > j->capacity) {
        uint32_t grown = j->capacity ? j->capacity : 4096u;
        while (grown < j->size + n + 1u) grown *= 2u;
        j->bytes = tide_realloc(j->bytes, j->capacity, grown);
        j->capacity = grown;
    }
    memcpy(j->bytes + j->size, text, n);
    j->size += (uint32_t)n;
    j->bytes[j->size] = '\0';
}

static void putf(json *j, const char *format, ...) __attribute__((format(printf, 2, 3)));

static void putf(json *j, const char *format, ...)
{
    char line[512];
    va_list args;
    va_start(args, format);
    const int n = vsnprintf(line, sizeof line, format, args);
    va_end(args);
    if (n > 0) put(j, line, (size_t)n < sizeof line ? (size_t)n : sizeof line - 1u);
}

static void put_string(json *j, const char *text)
{
    put(j, "\"", 1);
    for (const char *c = text ? text : ""; *c; c++) {
        if (*c == '"' || *c == '\\') {
            const char escaped[2] = {'\\', *c};
            put(j, escaped, 2);
        } else if ((unsigned char)*c < 32u) {
            putf(j, "\\u%04x", (unsigned)(unsigned char)*c);
        } else {
            put(j, c, 1);
        }
    }
    put(j, "\"", 1);
}

static bool listed(const int32_t *items, const uint32_t count, const int32_t type, uint32_t *at)
{
    for (uint32_t i = 0; items && i < count; i++) {
        if (items[i] == type) {
            if (at) *at = i;
            return true;
        }
    }
    return false;
}

static const tide_embed_field *field_said(const tide_embed_game *embed, const int32_t type, const char *name)
{
    for (uint32_t i = 0; embed && i < embed->field_count; i++) {
        if (embed->fields[i].type == type && strcmp(embed->fields[i].name, name) == 0) return &embed->fields[i];
    }
    return NULL;
}

static const char *field_kind(const tide_layout_field *f, const tide_embed_field *said)
{
    static const char *const said_kinds[] = {"plain", "text", "list", "map", "grid", "entity"};
    if (said && said->kind < sizeof said_kinds / sizeof said_kinds[0]) return said_kinds[said->kind];
    switch (f->kind) {
    case TIDE_LAYOUT_INT: return "int";
    case TIDE_LAYOUT_FLOAT: return "float";
    case TIDE_LAYOUT_LONG: return "long";
    case TIDE_LAYOUT_ENUM: return "enum";
    case TIDE_LAYOUT_STRUCT: return "struct";
    case TIDE_LAYOUT_BYTE: return "byte";
    case TIDE_LAYOUT_DOUBLE: return "double";
    default: return f->type && strcmp(f->type, "bool") == 0 ? "bool" : "plain";
    }
}

static void put_type(json *j, const tide_embed_game *embed, const tide_layout_type *t, const int32_t index, const char *kind,
                     const int32_t local, const bool scene)
{
    put(j, "{\"name\":", 8);
    put_string(j, t->name);
    putf(j, ",\"kind\":\"%s\",\"size\":%u,\"local\":%d", kind, (unsigned)t->size, (int)local);
    if (scene) put(j, ",\"scene\":true", 13);
    if (embed && embed->defaults && (strcmp(kind, "component") == 0 || strcmp(kind, "command") == 0) && t->size > 0) {
        uint8_t *value = tide_realloc(NULL, 0, t->size);
        if (embed->defaults(index, value, t->size)) {
            put(j, ",\"defaults\":\"", 13);
            for (uint32_t i = 0; i < t->size; i++) putf(j, "%02x", (unsigned)value[i]);
            put(j, "\"", 1);
        }
        free(value);
    }
    put(j, ",\"fields\":[", 11);
    for (uint32_t i = 0; i < t->field_count; i++) {
        const tide_layout_field *f = &t->fields[i];
        const tide_embed_field *said = field_said(embed, index, f->name);
        if (i) put(j, ",", 1);
        put(j, "{\"name\":", 8);
        put_string(j, f->name);
        put(j, ",\"type\":", 8);
        put_string(j, f->type);
        putf(j, ",\"kind\":\"%s\",\"dim\":%u,\"ref\":%d,\"offset\":%u,\"size\":%u,\"element\":%u}", field_kind(f, said),
             (unsigned)f->dim, (int)(said && said->kind == TIDE_EMBED_LIST ? said->element_type : f->decl), (unsigned)f->offset,
             (unsigned)f->size, (unsigned)(said ? said->element : 0u));
    }
    put(j, "]}", 2);
}

static void put_names(json *j, const char *key, const char *const *names, const uint32_t count)
{
    putf(j, ",\"%s\":[", key);
    for (uint32_t i = 0; names && i < count; i++) {
        if (i) put(j, ",", 1);
        put_string(j, names[i]);
    }
    put(j, "]", 1);
}

static void put_indices(json *j, const char *key, const int32_t *items, const uint32_t count)
{
    putf(j, ",\"%s\":[", key);
    for (uint32_t i = 0; items && i < count; i++) putf(j, "%s%d", i ? "," : "", (int)items[i]);
    put(j, "]", 1);
}

uint32_t tide_schema_json(const tide_layout *layout, const tide_embed_game *embed, const uint64_t hash, const char *game,
                          const uint32_t tick_rate, char *out, const uint32_t capacity)
{
    if (!layout) return 0;
    json j = {0};
    putf(&j, "{\"abi\":%u,\"hash\":\"%016llX\",\"game\":", (unsigned)TIDE_SCHEMA_ABI, (unsigned long long)hash);
    put_string(&j, game);
    putf(&j, ",\"tickRate\":%u,\"types\":[", (unsigned)tick_rate);
    for (uint32_t i = 0; i < layout->type_count; i++) {
        uint32_t at = 0;
        const char *kind = "struct";
        int32_t local = -1;
        bool scene = false;
        if (embed && listed(embed->components, embed->component_count, (int32_t)i, &at)) {
            kind = "component";
            local = embed->component_locals ? embed->component_locals[at] : -1;
            scene = embed->component_scenes && embed->component_scenes[at];
        } else if (embed && listed(embed->singletons, embed->singleton_count, (int32_t)i, &at)) {
            kind = "singleton";
            local = embed->singleton_locals ? embed->singleton_locals[at] : -1;
        } else if (embed && (listed(embed->component_locals, embed->component_count, (int32_t)i, NULL)
                             || listed(embed->singleton_locals, embed->singleton_count, (int32_t)i, NULL))) {
            kind = "local";
        }
        if (i) put(&j, ",", 1);
        put_type(&j, embed, &layout->types[i], (int32_t)i, kind, local, scene);
    }
    for (uint32_t i = 0; embed && i < embed->extra_type_count; i++) {
        put(&j, ",", 1);
        const int32_t type = (int32_t)(layout->type_count + i);
        bool event = false;
        for (uint32_t k = 0; embed->events && k < embed->event_count; k++) event |= embed->events[k].type == type;
        put_type(&j, embed, &embed->extra_types[i], type, event ? "event" : "command", -1, false);
    }
    put(&j, "],\"enums\":[", 11);
    for (uint32_t i = 0; i < layout->enum_count; i++) {
        const tide_layout_enum *en = &layout->enums[i];
        if (i) put(&j, ",", 1);
        put(&j, "{\"name\":", 8);
        put_string(&j, en->name);
        put(&j, ",\"members\":[", 12);
        for (uint32_t k = 0; k < en->member_count; k++) {
            if (k) put(&j, ",", 1);
            put_string(&j, en->members[k].name);
        }
        put(&j, "],\"values\":[", 12);
        for (uint32_t k = 0; k < en->member_count; k++) putf(&j, "%s%d", k ? "," : "", (int)en->members[k].value);
        put(&j, "]}", 2);
    }
    put(&j, "]", 1);
    put_indices(&j, "components", embed ? embed->components : NULL, embed ? embed->component_count : 0);
    put_indices(&j, "singletons", embed ? embed->singletons : NULL, embed ? embed->singleton_count : 0);
    put(&j, ",\"commands\":[", 13);
    for (uint32_t i = 0; embed && i < embed->command_count; i++) {
        if (i) put(&j, ",", 1);
        put(&j, "{\"name\":", 8);
        put_string(&j, embed->commands[i].name);
        putf(&j, ",\"type\":%d}", (int)embed->commands[i].type);
    }
    put(&j, "]", 1);
    put(&j, ",\"events\":[", 11);
    for (uint32_t i = 0; embed && embed->events && i < embed->event_count; i++) {
        if (i) put(&j, ",", 1);
        put(&j, "{\"name\":", 8);
        put_string(&j, embed->events[i].name);
        putf(&j, ",\"type\":%d}", (int)embed->events[i].type);
    }
    put(&j, "]", 1);
    put_names(&j, "prefabs", embed ? embed->prefabs : NULL, embed ? embed->prefab_count : 0);
    put_names(&j, "scenes", embed ? embed->scenes : NULL, embed ? embed->scene_count : 0);
    put(&j, "}", 1);
    const uint32_t size = j.size;
    if (out && capacity) {
        const uint32_t n = size < capacity - 1u ? size : capacity - 1u;
        memcpy(out, j.bytes, n);
        out[n] = '\0';
    }
    free(j.bytes);
    return size;
}
