#include "tweaks.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "builtins.h"
#include "lexer.h"

// tweaks.json: an object of the game's tweaks that it gives other values than the code does, by their qualified
// names, `{ "KickUp": 0.00015, "Combat.Fog": [0.6, 0.65, 0.7, 1] }`. A number, true or false, a list of numbers
// (a vector or a Color, by its components) or an enum's member by its name. Each value becomes the code that writes
// it, which the tweak's constant then has in place of the code's (decl.code_value keeps that), so the checker holds
// it to the tweak's type with what it says of any constant, pointing into the file.

typedef struct reader {
    const char *text;
    size_t at, len;
    int line, col, file;
} reader;

static loc here(const reader *r)
{
    return (loc){r->line, r->col, r->file};
}

static void step(reader *r)
{
    if (r->at >= r->len) return;
    if (r->text[r->at] == '\n') {
        r->line++;
        r->col = 1;
    } else r->col++;
    r->at++;
}

static void blank(reader *r)
{
    while (r->at < r->len && (r->text[r->at] == ' ' || r->text[r->at] == '\t' || r->text[r->at] == '\r' || r->text[r->at] == '\n')) step(r);
}

static bool at_char(reader *r, const char ch)
{
    blank(r);
    return r->at < r->len && r->text[r->at] == ch;
}

static bool expect_char(reader *r, const char ch, const char *what)
{
    if (at_char(r, ch)) {
        step(r);
        return true;
    }
    diag_error(here(r), "tweaks.json: expected %s", what);
    return false;
}

// A string's characters, which names and members are: no escapes are needed in either
static bool read_string(reader *r, sb *out)
{
    if (!expect_char(r, '"', "'\"'")) return false;
    while (r->at < r->len && r->text[r->at] != '"') {
        if (r->text[r->at] == '\\' || r->text[r->at] == '\n') {
            diag_error(here(r), "tweaks.json: a tweak's name or an enum member is plain text, with no escapes");
            return false;
        }
        sb_putn(out, &r->text[r->at], 1);
        step(r);
    }
    return expect_char(r, '"', "'\"' to end the text");
}

static bool read_number(reader *r, sb *out)
{
    blank(r);
    const size_t from = r->at;
    while (r->at < r->len && strchr("+-0123456789.eE", r->text[r->at])) step(r);
    if (r->at == from) {
        diag_error(here(r), "tweaks.json: expected a number, true, false, a list of numbers or an enum member's name");
        return false;
    }
    sb_putn(out, &r->text[from], r->at - from);
    return true;
}

static const char *const builtin_types[] = {"bool", "int", "long", "byte", "float", "double", "float2", "float3", "float4",
                                            "int2", "int3", "int4", "double2", "double3", "double4", "Color"};

static bool builtin_type(const str name)
{
    for (size_t i = 0; i < sizeof builtin_types / sizeof builtin_types[0]; i++) {
        if (str_eq_c(name, builtin_types[i])) return true;
    }
    return false;
}

// A tweak's type, as code in the global namespace names it
static void put_type(sb *o, const decl *d)
{
    const str t = d->return_type_name;
    bool dotted = false;
    for (int i = 0; i < t.len; i++) dotted |= t.ptr[i] == '.';
    if (!builtin_type(t) && !dotted && d->unit && d->unit->ns.len) sb_printf(o, STR_FMT ".", STR_ARG(d->unit->ns));
    sb_printf(o, STR_FMT, STR_ARG(t));
}

static void put_qualified(sb *o, const decl *d, const char dot)
{
    if (d->unit && d->unit->ns.len) {
        for (int i = 0; i < d->unit->ns.len; i++) sb_printf(o, "%c", d->unit->ns.ptr[i] == '.' ? dot : d->unit->ns.ptr[i]);
        sb_printf(o, "%c", dot);
    }
    sb_printf(o, STR_FMT, STR_ARG(d->name));
}

static decl *tweak_named(const program *prog, const char *name)
{
    for (int i = 0; i < prog->decls.count; i++) {
        decl *d = prog->decls.items[i];
        if (!d->is_tweak) continue;
        sb q = {0};
        put_qualified(&q, d, '.');
        const bool same = strcmp(q.data, name) == 0;
        if (same) return d;
    }
    return NULL;
}

// Every expression in a value read from the file points at it there
static void place(expr *e, const loc at)
{
    if (!e) return;
    e->at = e->qual_at = at;
    place(e->object, at);
    for (int i = 0; i < e->args.count; i++) place(e->args.items[i], at);
}

bool tweaks_read(program *prog, const char *folder)
{
    bool any = false;
    for (int i = 0; i < prog->decls.count && !any; i++) any = prog->decls.items[i]->is_tweak;
    sb path = {0};
    sb_printf(&path, "%s%s%s", folder && folder[0] ? folder : ".", folder && folder[0] && !strchr("/\\", folder[strlen(folder) - 1]) ? "/" : "",
              "tweaks.json");
    FILE *f = fopen(path.data, "rb");
    if (!f) return true; // No file: every tweak has the code's value
    source *src = NEW(source);
    src->path = path.data;
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *text = arena_alloc((size_t)(size > 0 ? size : 0) + 1);
    src->len = size > 0 ? fread(text, 1, (size_t)size, f) : 0;
    text[src->len] = '\0';
    fclose(f);
    src->text = text;
    diag_add_source(src);
    prog->tweaks_file = src->path;
    reader r = {.text = text, .len = src->len, .line = 1, .col = 1, .file = src->file};
    if (src->len >= 3 && (unsigned char)text[0] == 0xEF) r.at = 3; // A byte-order mark
    if (!expect_char(&r, '{', "'{': the file is an object of tweaks' values by their names")) return false;
    bool ok = true;
    bool first = true;
    while (!at_char(&r, '}')) {
        if (!first && !expect_char(&r, ',', "',' between two tweaks")) return false;
        first = false;
        const loc key_at = (blank(&r), here(&r));
        sb key = {0};
        if (!read_string(&r, &key) || !expect_char(&r, ':', "':' after the tweak's name")) return false;
        const loc value_at = (blank(&r), here(&r));
        sb value = {0};
        decl *d = key.data ? tweak_named(prog, key.data) : NULL;
        if (at_char(&r, '[')) {
            step(&r);
            if (d) put_type(&value, d);
            sb_put(&value, "(");
            for (int n = 0; !at_char(&r, ']'); n++) {
                if (n && !expect_char(&r, ',', "',' between two numbers")) return false;
                if (n) sb_put(&value, ", ");
                if (!read_number(&r, &value)) return false;
            }
            step(&r);
            sb_put(&value, ")");
        } else if (at_char(&r, '"')) {
            sb member = {0};
            if (!read_string(&r, &member)) return false;
            if (d) put_type(&value, d);
            sb_printf(&value, ".%s", member.data ? member.data : "");
        } else if (r.at + 4 <= r.len && strncmp(&r.text[r.at], "true", 4) == 0) {
            for (int k = 0; k < 4; k++) step(&r);
            sb_put(&value, "true");
        } else if (r.at + 5 <= r.len && strncmp(&r.text[r.at], "false", 5) == 0) {
            for (int k = 0; k < 5; k++) step(&r);
            sb_put(&value, "false");
        } else if (!read_number(&r, &value)) return false;
        if (!d) {
            diag_warning(key_at, "tweaks.json sets '%s', which isn't a tweak of the game's", key.data ? key.data : "");
            diag_note("a tweak is declared in code, like 'tweak float %s = 1;'; take the line out if it's gone", key.data ? key.data : "Name");
            continue;
        }
        source *vsrc = NEW(source);
        vsrc->path = src->path;
        vsrc->text = value.data;
        vsrc->len = value.len;
        diag_add_source(vsrc);
        token *toks = lex(vsrc);
        expr *e = toks ? parse_expression_text(prog, toks) : NULL;
        if (!e) {
            ok = false;
            continue;
        }
        place(e, value_at);
        d->code_value = d->value;
        d->value = e;
        d->tweaked_at = key_at;
    }
    (void)any;
    return ok;
}

bool tweaks_prelude(program *prog)
{
    sb text = {0};
    int count = 0;
    for (int i = 0; i < prog->decls.count; i++) {
        const decl *d = prog->decls.items[i];
        if (!d->is_tweak) continue;
        if (!count++) sb_put(&text, "struct TideTweakValues\n{\n");
        sb_put(&text, "    ");
        put_type(&text, d);
        sb_put(&text, " ");
        put_qualified(&text, d, '_');
        sb_put(&text, " = ");
        put_qualified(&text, d, '.');
        sb_put(&text, ";\n");
    }
    if (!count) return true;
    sb_put(&text, "}\n"
                  "singleton TideTweaks { TideTweakValues values; }\n"
                  "local singleton TideTweaksHere { TideTweakValues values; }\n"
                  "command TideSetTweaks { TideTweakValues values; }\n"
                  "command(TideSetTweaks set, PlayerID sender) TideApplyTweaks(mut TideTweaks tweaks)\n"
                  "{\n    tweaks.values = set.values;\n}\n");
    source *src = NEW(source);
    src->path = TWEAKS_PRELUDE_PATH;
    src->text = text.data;
    src->len = text.len;
    diag_add_source(src);
    token *toks = lex(src);
    if (!toks || !parse_file(prog, src, toks, false)) return false;
    prog->tweaks_live = true;
    return true;
}
