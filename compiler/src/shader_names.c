#include "shader_names.h"

#include <string.h>

typedef struct shader_name {
    str text;
    const char *alias;
} shader_name;

typedef struct shader_names {
    VEC(shader_name) entries;
    int *slots;
    size_t room;
} shader_names;

static bool identifier_start(const char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool identifier_part(const char c)
{
    return identifier_start(c) || (c >= '0' && c <= '9');
}

static uint64_t name_hash(const str name)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    for (int i = 0; i < name.len; i++) hash = (hash ^ (uint8_t)name.ptr[i]) * UINT64_C(1099511628211);
    return hash;
}

static size_t name_slot(const shader_names *names, const str text)
{
    size_t slot = (size_t)name_hash(text) & (names->room - 1u);
    while (names->slots[slot] && !str_eq(names->entries.items[names->slots[slot] - 1].text, text))
        slot = (slot + 1u) & (names->room - 1u);
    return slot;
}

static void intern_name(shader_names *names, const str text)
{
    if (!names->room || (size_t)names->entries.count * 2u >= names->room) {
        names->room = names->room ? names->room * 2u : 32u;
        names->slots = arena_alloc(names->room * sizeof *names->slots);
        for (int i = 0; i < names->entries.count; i++)
            names->slots[name_slot(names, names->entries.items[i].text)] = i + 1;
    }
    const size_t slot = name_slot(names, text);
    if (names->slots[slot]) return;
    vec_push(names->entries, ((shader_name){.text = text}));
    names->slots[slot] = names->entries.count;
}

// All authored names go through shader.c's name_of. These prefixes are never
// used for renderer bindings, shader-language builtins or helper identifiers.
static bool authored_name(const str text)
{
    return str_starts_with_c(text, "t_") || str_starts_with_c(text, "m_")
        || str_starts_with_c(text, "tt_") || str_starts_with_c(text, "tf_") || str_starts_with_c(text, "ts_");
}

// Comments and quoted strings are not identifiers. WGSL allows nested block
// comments. Preserve strings verbatim, even though the emitter needs none now.
static const char *non_code_end(const char *p)
{
    if (p[0] == '/' && p[1] == '/') {
        p += 2;
        while (*p && *p != '\n') p++;
    } else if (p[0] == '/' && p[1] == '*') {
        size_t depth = 1;
        p += 2;
        while (*p && depth) {
            if (p[0] == '/' && p[1] == '*') { depth++; p += 2; }
            else if (p[0] == '*' && p[1] == '/') { depth--; p += 2; }
            else p++;
        }
    } else if (*p == '"' || *p == '\'') {
        const char quote = *p++;
        while (*p && *p != quote) {
            if (*p == '\\' && p[1]) p++;
            p++;
        }
        if (*p) p++;
    }
    return p;
}

const char *shader_minify_names(const char *source, sb *map)
{
    shader_names names = {0};
    for (const char *p = source; *p;) {
        const char *end = non_code_end(p);
        if (end != p) p = end;
        else if (identifier_start(*p)) {
            end = p + 1;
            while (identifier_part(*end)) end++;
            intern_name(&names, (str){p, (int)(end - p)});
            p = end;
        } else p++;
    }
    // Reserve every original identifier, including unchanged helper locals,
    // before choosing aliases. A future emitter spelling cannot collide.
    unsigned next = 0;
    for (int i = 0; i < names.entries.count; i++) {
        shader_name *name = &names.entries.items[i];
        if (!authored_name(name->text)) continue;
        sb alias;
        do {
            alias = (sb){0};
            sb_printf(&alias, "q%u", next++);
        } while (names.slots[name_slot(&names, str_from(alias.data))]);
        name->alias = alias.data;
        if (map) sb_printf(map, "%s\t" STR_FMT "\n", name->alias, STR_ARG(name->text));
    }
    sb out = {0};
    for (const char *p = source; *p;) {
        const char *end = non_code_end(p);
        if (end != p) {
            if (*p == '/') {
                sb_put(&out, " ");
                for (const char *c = p; c != end; c++) if (*c == '\n') sb_put(&out, "\n");
            } else sb_putn(&out, p, (size_t)(end - p));
        } else if (identifier_start(*p)) {
            end = p + 1;
            while (identifier_part(*end)) end++;
            const str text = {p, (int)(end - p)};
            const shader_name *name = &names.entries.items[names.slots[name_slot(&names, text)] - 1];
            if (name->alias) sb_put(&out, name->alias);
            else sb_putn(&out, p, (size_t)(end - p));
        } else {
            end = p + 1;
            sb_putn(&out, p, 1);
        }
        p = end;
    }
    return out.data ? out.data : "";
}
