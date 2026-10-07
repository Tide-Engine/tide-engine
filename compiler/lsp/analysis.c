#include "analysis.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asset_files.h"
#include "assets.h"
#include "ast.h"
#include "builtins.h"
#include "cdefs.h"
#include "folders.h"
#include "lexer.h"
#include "types.h"

// Everything here points into the compiler's arena, so it all belongs to the
// last analysis and is reset by the next one.

// A note about another place in the code, which the editor can show there.
typedef struct related_note {
    loc at;
    const char *message;
} related_note;

typedef struct diagnostic {
    diag_severity severity;
    loc at;
    sb message; // Notes are appended on their own lines, but not those about another place that the editor shows there
    VEC(related_note) related; // Notes about another place
    loc fix_at; // A "did you mean": the name written, `fix_len` bytes, which `fix` replaces
    int fix_len;
    str fix;
} diagnostic;

// A name in the source and what it refers to.
typedef enum occ_kind {
    OCC_TYPE,     // Component, singleton, input, record, struct, event or built-in value type
    OCC_SYSTEM,   // System, view or event handler
    OCC_FIELD,
    OCC_PARAM,
    OCC_LOCAL,
    OCC_OWNER,    // Math, Draw
    OCC_FUNCTION, // Math.Dot, Draw.Circle, Spawn, Send, and the game's functions (with `decl`)
    OCC_CONSTANT, // Math.PI, Color.red
    OCC_METHOD,   // e.Add, e.Remove, e.Destroy, e.Send, and methods of structs and components (with `decl`)
    OCC_MEMBER,   // Swizzles, color channels, quaternion.value, matrix columns, input .down/.up
    OCC_NAMESPACE, // Combat in `namespace Combat;` or Combat.Health
    OCC_ATTRIBUTE, // Stage, Before, After, Clamp, Min, Max
    OCC_ENUM_MEMBER, // Title in Page.Title, and in `enum Page { Title }` (with `decl`, the enum)
    OCC_THIS,     // `this`, with `decl` the system, view, handler or method it's in
    OCC_DEFAULT,  // `default`, with the type it takes
    OCC_CONST,    // A constant the game declares, MAX_HEALTH, with `decl`
    OCC_SETTING,  // One of the engine's settings, tickRate in settings { ... }
    OCC_OWNERSHIP, // `ownership` after `with` or `without`, with `param` the filter
    OCC_VERIFIED, // `verified` before a parameter's or filter's type, or a trigger's event, with `param` the one it's on
    // `out` before a parameter's type (a declaration, with `param` the one it's on), or before an argument, with
    // `param` the parameter it's for where the game declares one, and `local` the variable the argument declares
    OCC_OUT,
    OCC_REPLACE,  // `virtual` or `override` before a declaration, with `decl` the declaration
} occ_kind;

typedef struct occurrence {
    loc at;
    int len;
    occ_kind kind;
    bool declaration;
    const decl *decl;   // The type, system, method or function, or a field's owner. NULL for built-in value types.
    const field *field;
    const param *param;
    const decl *param_of;
    const stmt *local;
    str owner;          // Functions and constants: "Math", "Draw", "Color"...
    str name;
    type type;
    type object_type;   // Members, and built-in methods: the type they're read from or called on
    const char *c_name; // Functions and constants: what they are in C, like tide_draw_circle
    builtin_call call;  // Built-in methods: which kind, CALL_LIST for items.Add(...)
    int order;          // When it was found, which decides between two at one position
    bool write;         // What an assignment changes: `x` in `x = 1`, `radius` in `body.radius += 1`; and what a call makes, `x` in `F(out x)`
    const decl *component; // The World's methods: the component in <>, Health in world.Get<Health>(e)
    bool backing;       // An enum's backing type: byte in `enum Voxel : byte`
} occurrence;

// One file of the game being analysed.
typedef struct afile {
    source src;
    const char *uri;
    token *toks;
    int tok_count;
    bool *query_keywords; // Contextual query keywords, by token, for highlighting and formatting
    int lex_errors; // Characters the lexer skipped
    VEC(const char *) lines; // Where each line starts
    unsigned char *inactive; // Each line, from 1: LINE_DIRECTIVE, LINE_LEFT_OUT, or 0 for code
} afile;

// Lines no code is read from (see lex_all_spans).
enum { LINE_DIRECTIVE = 1, LINE_LEFT_OUT = 2 };

// The file an `asset` declaration names (see Assets, at the end).
typedef struct asset_site {
    int file;           // The declaring file
    int string;         // Its path's token there
    str path;           // As written, between the quotes
    const char *folder; // The declaring file's, absolute, ending in '/'
    const char *abs;    // The path resolved from it, a glob as written where it's one; NULL for a URL
    bool glob;
    bool url;
    bool many;          // A List or Map: a glob's matches
    const char *format; // The files it takes: ".png" for a Grid2<Color> and a Texture; NULL for any
} asset_site;

// A path to write at a site whose file is missing, fixing its diagnostic.
typedef struct asset_fix {
    int site;
    int diag;
    const char *path;
    bool preferred;
} asset_fix;

typedef struct asset_file {
    const char *path;
    unsigned long long size;
} asset_file;

static struct {
    afile *files; // Index i is source file i: locations' `file` indexes this
    int file_count;
    int doc;           // The file requests are about
    int syntax_errors; // Lexer and parser errors in any file: code recovery skipped
    program *prog;
    VEC(diagnostic) diags;
    VEC(occurrence) occs;
    VEC(const expr *) calls; // Calls of the game's methods and functions, for inlay hints
    // Every declaration a file writes outside a type. The program's own list has what's built: an override
    // where what it replaced was, and nothing of what was replaced. This one has both, each where its file
    // has it, and an override of a method, which the program keeps in the method's type.
    VEC(const decl *) written;
    VEC(asset_site) assets;        // The files `asset` declarations name
    VEC(asset_fix) asset_fixes;
    VEC(asset_file) asset_files;   // Every file of the game's folders, once it's been listed
    bool asset_files_listed;
} A;

static void collect_assets(void);
static void check_assets(void);

// The folders of the analysed game: set by the server before each analysis,
// and kept across them (malloc'd).
static struct {
    char *game_folder; // Where its .tide/ is; NULL for a game a manifest lists
    char **roots;      // Where a moved file is looked for: the game's folder and its packages'
    int root_count;
} asset_roots;

// The document requests are about.
#define DOC (&A.files[A.doc])

// ---------------------------------------------------------------------------
// Positions

// Orders positions file by file, then by line and column.
static int loc_cmp(const loc a, const loc b)
{
    if (a.file != b.file) return a.file - b.file;
    return a.line != b.line ? a.line - b.line : a.col - b.col;
}

// 0-based UTF-16 column of a 1-based byte column.
static int utf16_col(const loc at)
{
    if (at.file < 0 || at.file >= A.file_count) return 0;
    const afile *f = &A.files[at.file];
    if (at.line < 1 || at.line > f->lines.count) return 0;
    const char *line = f->lines.items[at.line - 1];
    const char *end = f->src.text + f->src.len;
    int units = 0;
    for (int i = 0; i < at.col - 1 && line + i < end; i++) {
        const unsigned char c = (unsigned char)line[i];
        if ((c & 0xC0) == 0x80) continue; // Continuation byte
        units += c >= 0xF0 ? 2 : 1;       // Outside the BMP: a surrogate pair
    }
    return units;
}

// A position in the current document.
static loc from_lsp(const int line, const int character)
{
    loc at = {line + 1, 1, A.doc};
    if (line < 0 || line >= DOC->lines.count) return at;
    const char *p = DOC->lines.items[line];
    const char *end = DOC->src.text + DOC->src.len;
    int units = 0;
    while (p < end && *p != '\n' && units < character) {
        const unsigned char c = (unsigned char)*p;
        const int len = c < 0xC0 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        units += c >= 0xF0 ? 2 : 1;
        p += len;
        at.col += len;
    }
    return at;
}

static void write_position(jbuf *out, const loc at)
{
    jb_printf(out, "{\"line\":%d,\"character\":%d}", at.line > 0 ? at.line - 1 : 0, utf16_col(at));
}

// A range on one line, `len` bytes long.
static void write_range(jbuf *out, const loc at, const int len)
{
    jb_put(out, "{\"start\":");
    write_position(out, at);
    jb_put(out, ",\"end\":");
    write_position(out, (loc){at.line, at.col + len, at.file});
    jb_put(out, "}");
}

// Index of the token starting at `at` in its file, or -1.
static int token_at(const loc at)
{
    if (at.file < 0 || at.file >= A.file_count) return -1;
    const afile *f = &A.files[at.file];
    int lo = 0;
    int hi = f->tok_count - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        const int c = loc_cmp(f->toks[mid].at, at);
        if (c == 0) return mid;
        if (c < 0) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

static int token_len(const token *t)
{
    return t->kind == T_STRING ? t->text.len + 2 : t->text.len;
}

// ---------------------------------------------------------------------------
// Running the compiler

// Whether the editor shows diagnostics' notes about other places there
// (relatedInformation). From its capabilities, at initialization.
static bool related_information;

void analysis_set_related_information(const bool shows)
{
    related_information = shows;
}

static void collect(void *user, const diag_severity severity, const loc at, const char *message)
{
    (void)user;
    if (severity == DIAG_NOTE) {
        if (A.diags.count == 0) return;
        diagnostic *d = &A.diags.items[A.diags.count - 1];
        if (at.line > 0) {
            const related_note note = {at, str_to_cstr(str_from(message))};
            vec_push(d->related, note);
            if (related_information) return;
        }
        sb_printf(&d->message, "\nnote: %s", message);
        return;
    }
    diagnostic d = {severity, at, {0}, {0}, {0, 0, 0}, 0, {NULL, 0}};
    sb_put(&d.message, message);
    vec_push(A.diags, d);
}

static str last_part(str text);

// A "did you mean" for the last diagnostic: the name written, where it's a
// slice of a file's text, and the one meant, which a quick fix puts there.
static void collect_suggestion(void *user, const str wrong, const str best)
{
    (void)user;
    if (A.diags.count == 0) return;
    for (int f = 0; f < A.file_count; f++) {
        const afile *file = &A.files[f];
        if (wrong.ptr < file->src.text || wrong.ptr + wrong.len > file->src.text + file->src.len) continue;
        // Combat.Helth for Health: its last part. Nothing in its place: the whole of it goes
        const str written = best.len == 0 || memchr(best.ptr, '.', (size_t)best.len) ? wrong : last_part(wrong);
        int line = 0;
        while (line + 1 < file->lines.count && file->lines.items[line + 1] <= written.ptr) line++;
        diagnostic *d = &A.diags.items[A.diags.count - 1];
        d->fix_at = (loc){line + 1, (int)(written.ptr - file->lines.items[line]) + 1, f};
        d->fix_len = written.len;
        d->fix = best;
        return;
    }
}

// A declaration's fields as its source has them: its own, then its local
// ones, which the checker moves to its local part (decl.local_part).
static int local_field_count(const decl *d)
{
    return d->local_part ? d->local_part->fields.count : 0;
}

static const field *field_of(const decl *d, const int i)
{
    return i < d->fields.count ? &d->fields.items[i] : &d->local_part->fields.items[i - d->fields.count];
}

static void add_occ(occurrence o)
{
    o.order = A.occs.count;
    if (o.at.line > 0 && o.len > 0) vec_push(A.occs, o);
}

// A type's method as the type's body writes it: where an override replaced
// one, the program has the override there, and the source the one replaced.
static const decl *method_written(const decl *m)
{
    return m->replaces ? m->replaces : m;
}

// Whether a name finds `d`: an override that found nothing to replace declares
// none, and nothing can name it.
static bool has_name(const decl *d)
{
    return !d->is_override || d->placed;
}

// What an override and the declaration it replaced are, as one thing a name
// means: the one that was replaced.
static const decl *replaced_or_self(const decl *d)
{
    return d && d->replaces ? d->replaces : d;
}

static void list_written(void)
{
    for (int i = 0; i < A.prog->decls.count; i++) vec_push(A.written, A.prog->decls.items[i]);
    for (int i = 0; i < A.prog->replaced.count; i++) {
        const decl *v = A.prog->replaced.items[i];
        vec_push(A.written, v->owner ? v->replaced_by : v); // A method that was replaced is in its type's body
    }
}

// A type written by name: of a field, parameter or local.
// The last part of "Game.Combat.Health".
static str last_part(const str text)
{
    int dot = text.len - 1;
    while (dot >= 0 && text.ptr[dot] != '.') dot--;
    return (str){text.ptr + dot + 1, text.len - dot - 1};
}

// Each part of the namespace `path` written at `at`: Game and Game.Combat in
// `namespace Game.Combat;`. A part is named by the path up to it, so the Combat
// in Game.Combat is never another namespace's Combat.
static void namespace_parts(const loc at, const str path, const bool declaration)
{
    int t = token_at(at);
    if (t < 0) return;
    const afile *f = &A.files[at.file];
    for (int start = 0; start < path.len && t < f->tok_count; start++) {
        int end = start;
        while (end < path.len && path.ptr[end] != '.') end++;
        const token *part = &f->toks[t];
        if (part->kind != T_IDENT || !str_eq(part->text, (str){path.ptr + start, end - start})) return;
        add_occ((occurrence){.at = part->at, .len = part->text.len, .kind = OCC_NAMESPACE, .declaration = declaration,
                             .name = {path.ptr, end}});
        if (end < path.len && (t + 1 >= f->tok_count || f->toks[t + 1].kind != T_DOT)) return;
        t += 2; // And the dot
        start = end;
    }
}

// The namespace in front of a qualified name written at `qual_at`: Combat in
// Combat.Health. Nothing for a plain name.
static void qualifier_ref(const loc qual_at, const loc name_at, const str text)
{
    const str name = last_part(text);
    if (name.len == text.len || qual_at.line == 0 || loc_cmp(qual_at, name_at) == 0) return;
    namespace_parts(qual_at, (str){text.ptr, text.len - name.len - 1}, false);
}

// The namespace a part of Game.Combat.Health names in code: Game.Combat for Combat.
static str namespace_path(const expr *e)
{
    if (e->kind != E_MEMBER) return e->name;
    const str left = namespace_path(e->object);
    char *text = arena_alloc((size_t)left.len + (size_t)e->member.len + 2);
    memcpy(text, left.ptr, (size_t)left.len);
    text[left.len] = '.';
    memcpy(text + left.len + 1, e->member.ptr, (size_t)e->member.len);
    return (str){text, left.len + 1 + e->member.len};
}

// A type written by name alone, maybe qualified: `qual_at` is where it
// starts, `at` its last part. For the names the parser keeps as text (an
// override's, an enum's in an attribute); a type with a tree goes through
// type_tree.
static void type_written(const loc qual_at, const loc at, const str text, const type t)
{
    if (text.len == 0) return;
    if (t.kind == TY_WORLD) { // The engine's World, a parameter's type
        add_occ((occurrence){.at = at, .len = text.len, .kind = OCC_TYPE, .name = text, .type = t});
        return;
    }
    qualifier_ref(qual_at, at, text);
    const str name = last_part(text);
    occurrence o = {.at = at, .len = name.len, .kind = OCC_TYPE, .name = name, .type = t};
    if (t.kind == TY_COMPONENT || t.kind == TY_SINGLETON || t.kind == TY_INPUT || t.kind == TY_RECORD
        || t.kind == TY_STRUCT || t.kind == TY_EVENT || t.kind == TY_ENUM) {
        o.decl = t.decl;
    } else if (!builtin_type_named(name, &o.type)) {
        // Unresolved, for example a component used as a field type: still a type.
        for (int i = 0; i < A.prog->decls.count; i++) {
            const decl *d = A.prog->decls.items[i];
            if (d->kind != DECL_SYSTEM && str_eq(d->name, name)) o.decl = d;
        }
        if (!o.decl) return;
    }
    add_occ(o);
}

// A type as written (type_ref, ast.h): a generic name, each type in its <>
// and the one a T? holds, each where it's written.
static void type_tree(const type_ref *r, const type t)
{
    if (r->optional) { // int?: the type it holds
        const type inner = t.kind == TY_OPTIONAL && t.decl ? t.decl->fields.items[0].type : (type){TY_ERROR, NULL};
        type_ref plain = *r;
        plain.optional = false;
        plain.text = (str){r->text.ptr, r->text.len - 1};
        type_tree(&plain, inner);
        return;
    }
    if (r->sizes.count > 0) { // int[32]: the name is the element's
        type inner = t;
        for (int i = 0; i < r->sizes.count && inner.kind == TY_ARRAY && inner.decl; i++) inner = inner.decl->fields.items[0].type;
        type_ref plain = *r;
        plain.sizes.count = 0;
        plain.text = r->name;
        type_tree(&plain, r->sizes.count > 0 && inner.kind != TY_ARRAY ? inner : (type){TY_ERROR, NULL});
        return;
    }
    if (r->args.count > 0) { // List<T>, Map<K, V>, Set<K>, Grid2<T>, Grid3<T>: the generic type, then what it holds
        add_occ((occurrence){.at = r->at, .len = r->name.len, .kind = OCC_TYPE, .name = r->name, .type = t});
        const bool holds = (t.kind == TY_LIST || t.kind == TY_MAP || t.kind == TY_SET || t.kind == TY_GRID) && t.decl;
        for (int i = 0; i < r->args.count; i++) {
            const type part = holds && i < t.decl->fields.count ? t.decl->fields.items[i].type : (type){TY_ERROR, NULL};
            type_tree(r->args.items[i], part);
        }
        return;
    }
    type_written(r->at, r->name_at, r->name, t);
}

// A type where the parser kept a tree, or only its text (one the checker made).
static void type_place(const type_ref *r, const loc qual_at, const loc at, const str text, const type t)
{
    if (r) type_tree(r, t);
    else type_written(qual_at, at, text, t);
}

// A declaration as the type a name of it is: a component's, an enum's.
static type type_of_decl(const decl *d)
{
    if (!d) return (type){TY_ERROR, NULL};
    const type_kind kind = d->kind == DECL_COMPONENT ? TY_COMPONENT
                         : d->kind == DECL_SINGLETON ? TY_SINGLETON
                         : d->kind == DECL_INPUT     ? TY_INPUT
                         : d->kind == DECL_STRUCT    ? TY_STRUCT
                         : d->kind == DECL_EVENT     ? TY_EVENT
                         : d->kind == DECL_ENUM      ? TY_ENUM
                                                     : TY_ERROR;
    return (type){kind, kind == TY_ERROR ? NULL : (decl *)d};
}

// Whose fields plain names refer to in the code being walked: the input in its
// Sample and Sanitize, a type in its methods.
static const decl *walk_fields_of;

// The system, view, handler, method or function being walked: whose `this` it is.
static const decl *walk_code;

// What the assignment being walked changes: a name or a member.
static const expr *walk_written;

// The name or member an assignment to `target` changes: cells for cells[x].
static const expr *written_place(const expr *target)
{
    while (target && target->kind == E_INDEX) target = target->object;
    return target;
}

// A game's method, function or operator, which calls and declarations point at.
static bool is_routine(const decl *d)
{
    return d && (d->kind == DECL_METHOD || d->kind == DECL_FUNCTION);
}

static bool is_operator_decl(const decl *d)
{
    return is_routine(d) && d->is_operator;
}

// Whose function a draw call on a render target is: Draw's, as a target has the window's methods.
static str target_owner(const expr *e)
{
    (void)e;
    return str_from("Draw");
}

static bool is_shader(const decl *d)
{
    return d && d->kind == DECL_SHADER;
}

// How many characters an operator token has: 1 for +, 2 for == and +=, 3 for <<=.
static int operator_len(const tok_kind kind)
{
    switch (kind) {
    case T_EQ: case T_NE: case T_LE: case T_GE: case T_SHL: case T_SHR: case T_AND: case T_OR: case T_PLUS_ASSIGN:
    case T_MINUS_ASSIGN: case T_STAR_ASSIGN: case T_SLASH_ASSIGN: case T_PERCENT_ASSIGN: case T_AMP_ASSIGN:
    case T_PIPE_ASSIGN: case T_CARET_ASSIGN:
        return 2;
    case T_SHL_ASSIGN: case T_SHR_ASSIGN:
        return 3;
    default:
        return 1;
    }
}

static void walk_stmt(const stmt *s);

// The query that supplies this compiler-only row type, and one of its bindings.
static const param *query_for_row(const decl *row)
{
    return row && row->query_row ? row->query_source : NULL;
}

static const param *query_binding(const param *query, const str name)
{
    for (int i = 0; query && i < query->query.count; i++) {
        const param *p = &query->query.items[i];
        if (p->name.len && str_eq(p->name, name)) return p;
    }
    return NULL;
}

// Card(2) or Combat.Stance(2): a call that names an enum, converting an int
// to it. The checker says which enum even where it refused the argument.
static bool converts_enum(const expr *e)
{
    return e->ctor == CTOR_ENUM && (e->call == CALL_NONE || e->call == CALL_CONSTRUCT) && e->type_decl;
}

static void walk_expr(const expr *e);

// A call's arguments as its code wrote them. Where a call names arguments or
// leaves some out, the checker puts `args` in the parameters' order, with
// their defaults, and keeps what was written apart.
static int written_count(const expr *call)
{
    return call->settled ? call->written.count : call->args.count;
}

static const expr *written_arg(const expr *call, const int i)
{
    return call->settled ? call->written.items[i] : call->args.items[i];
}

// The name a written argument has, `crit` in `crit: true`, or empty.
static str written_name(const expr *call, const int i)
{
    return i < call->arg_names.count ? call->arg_names.items[i] : (str){"", 0};
}

// The parameter a written argument goes to, by its place among them.
static int written_param(const expr *call, const int i)
{
    if (!call->settled) return i;
    for (int k = 0; k < call->args.count; k++) {
        if (call->args.items[k] == call->written.items[i]) return k;
    }
    return i;
}

// The parameter of the game's that the argument being walked is for, and
// whose it is: an `out` argument's word says which.
static const param *walk_arg_param;
static const decl *walk_arg_of;

// A call's arguments, and the names the game's own parameters are passed by:
// each is a use of its parameter.
static void walk_args(const expr *e)
{
    const decl *m = e->call == CALL_METHOD || e->call == CALL_FUNCTION ? e->method : NULL;
    for (int i = 0; i < written_count(e); i++) {
        const str name = written_name(e, i);
        const int k = written_param(e, i);
        const param *p = m && k < m->params.count ? &m->params.items[k] : NULL;
        if (name.len > 0 && p && str_eq(p->name, name)) {
            add_occ((occurrence){.at = e->arg_name_at.items[i], .len = name.len, .kind = OCC_PARAM, .param = p,
                                 .param_of = m, .name = name, .type = p->type});
        }
        walk_arg_param = p;
        walk_arg_of = m;
        walk_expr(written_arg(e, i));
        walk_arg_param = NULL;
        walk_arg_of = NULL;
    }
}

static void walk_value(const expr *e);

// An expression, and for an argument written with `out`, its word and the
// variable the call makes. `out var half` and `out int half` declare that
// variable where they're written, as a statement does: its type, when it
// writes one, and its name. `out _` makes a place nothing names, so there's
// only a type it may write. Anything else is a variable or a field the code
// has, which the call writes as an assignment would.
static void walk_expr(const expr *e)
{
    if (!e) return;
    if (!e->is_out) {
        walk_value(e);
        return;
    }
    const stmt *local = e->out_local;
    if (e->out_at.line > 0) {
        add_occ((occurrence){.at = e->out_at, .len = 3, .kind = OCC_OUT, .param = walk_arg_param, .param_of = walk_arg_of,
                             .local = local, .name = str_from("out"), .type = e->type});
    }
    if (!local) {
        const expr *outer = walk_written;
        walk_written = written_place(e);
        walk_value(e);
        walk_written = outer;
        return;
    }
    type_place(local->type_ref, local->type_qual_at.line ? local->type_qual_at : local->type_at, local->type_at, local->type_name, local->type);
    if (local->discard) return;
    add_occ((occurrence){.at = local->name_at, .len = local->name.len, .kind = OCC_LOCAL, .declaration = true, .local = local,
                         .name = local->name, .type = local->type});
}

static void walk_value(const expr *e)
{
    switch (e->kind) {
    case E_INT:
    case E_FLOAT:
    case E_BOOL:
    case E_STRING:
        break;

    case E_THIS:
        if (walk_code) {
            add_occ((occurrence){.at = e->at, .len = 4, .kind = OCC_THIS, .decl = walk_code, .name = str_from("this"),
                                 .type = e->type});
        }
        break;

    case E_DEFAULT:
        if (e->type.kind != TY_ERROR) {
            add_occ((occurrence){.at = e->at, .len = 7, .kind = OCC_DEFAULT, .name = str_from("default"), .type = e->type});
        }
        break;

    case E_NULL:
        if (e->type.kind == TY_OPTIONAL) {
            add_occ((occurrence){.at = e->at, .len = 4, .kind = OCC_DEFAULT, .name = str_from("null"), .type = e->type});
        }
        break;

    case E_COALESCE:
    case E_TRY:
    case E_DEFAULTED:
    case E_AWAIT:
        walk_expr(e->lhs);
        walk_expr(e->rhs);
        break;

    case E_IS: // x is int score, x is ParseError.Empty
        walk_expr(e->lhs);
        if (e->looks_for == IS_MEMBER && e->enum_member) {
            const str type_part = {e->pattern.ptr, e->pattern.len - e->enum_member->name.len - 1};
            const str type_name_ = last_part(type_part);
            const loc type_at = {e->pattern_at.line, e->pattern_at.col - 1 - type_name_.len, e->pattern_at.file};
            type_written(e->pattern_qual_at, type_at, type_part, (type){TY_ENUM, e->type_decl});
            add_occ((occurrence){.at = e->pattern_at, .len = e->enum_member->name.len, .kind = OCC_ENUM_MEMBER,
                                 .decl = e->type_decl, .name = e->enum_member->name});
        } else {
            type_place(e->pattern_ref, e->pattern_qual_at, e->pattern_at, e->pattern, e->binding ? e->binding->type : (type){TY_ERROR, NULL});
        }
        if (e->binding) {
            const stmt *b = e->binding;
            add_occ((occurrence){.at = b->name_at, .len = b->name.len, .kind = OCC_LOCAL, .declaration = true, .local = b,
                                 .name = b->name, .type = b->type});
        }
        break;

    case E_NAME: {
        occurrence o = {.at = e->at, .len = e->name.len, .name = e->name, .type = e->type, .write = e == walk_written};
        switch (e->bind) {
        case BIND_PARAM:
            o.kind = OCC_PARAM;
            o.param = e->param;
            break;
        case BIND_LOCAL:
            o.kind = OCC_LOCAL;
            o.local = e->local;
            break;
        case BIND_TYPE:
            o.kind = OCC_TYPE;
            o.decl = e->type_decl;
            break;
        case BIND_FIELD:
            o.kind = OCC_FIELD;
            o.field = e->field;
            o.decl = walk_fields_of;
            break;
        case BIND_NAMESPACE:
            o.kind = OCC_NAMESPACE;
            break;
        case BIND_DEVICES: // This machine's devices, like Screen
            o.kind = OCC_OWNER;
            o.owner = e->name;
            break;
        case BIND_CONST:
        case BIND_ASSET: // An asset read by name: its declaration, as a constant's
            o.kind = OCC_CONST;
            o.decl = e->constant;
            break;
        case BIND_SHADER: // The vert or frag a Draw.Mesh names, maybe with its parameters' types: Shade(BoxVertex, Color)
            o.kind = OCC_SYSTEM;
            o.decl = e->type_decl;
            for (int i = 0; i < e->args.count; i++) walk_expr(e->args.items[i]);
            break;
        case BIND_NONE:
            // The owner in Math.Dot(...) or quaternion.identity, which the checker doesn't bind.
            if (!builtin_owner(e->name)) return;
            if (builtin_type_named(e->name, &o.type)) {
                o.kind = OCC_TYPE;
            } else {
                o.kind = OCC_OWNER;
                o.owner = e->name;
            }
            break;
        }
        add_occ(o);
        break;
    }

    case E_MEMBER: {
        walk_expr(e->object);
        occurrence o = {.at = e->at, .len = e->member.len, .name = e->member, .type = e->type,
                        .object_type = e->object->type, .write = e == walk_written};
        if (e->bind == BIND_TYPE && e->type_decl) { // Combat.Health, in Spawn(Combat.Health)
            o.kind = OCC_TYPE;
            o.decl = e->type_decl;
        } else if (e->bind == BIND_SHADER && e->type_decl) { // Effects.Shade, in Draw.Mesh(..., Effects.Shade)
            o.kind = OCC_SYSTEM;
            o.decl = e->type_decl;
            for (int i = 0; i < e->args.count; i++) walk_expr(e->args.items[i]); // Its parameters' types, where it's written with them
        } else if (e->enum_member) { // Title in Page.Title
            o.kind = OCC_ENUM_MEMBER;
            o.decl = e->type_decl;
        } else if (e->bind == BIND_NAMESPACE) { // Combat in Game.Combat.Health
            o.kind = OCC_NAMESPACE;
            o.name = namespace_path(e);
        } else if (e->bind == BIND_CONST || e->bind == BIND_ASSET) { // MAX_HEALTH in Combat.MAX_HEALTH, or an asset
            o.kind = OCC_CONST;
            o.decl = e->constant;
        } else if (e->c_constant && e->object->type.kind != TY_ARRAY) {
            o.kind = OCC_CONSTANT;
            o.owner = e->object->name;
            o.c_name = e->c_constant;
        } else if (e->field && e->object->type.decl && e->object->type.decl->query_row) {
            o.param = query_binding(query_for_row(e->object->type.decl), e->member);
            o.kind = o.param ? OCC_PARAM : OCC_MEMBER; // entity is built into the row
        } else if (e->field) {
            o.kind = OCC_FIELD;
            o.field = e->field;
            o.decl = e->object->type.decl;
        } else if (e->type.kind != TY_ERROR) {
            o.kind = OCC_MEMBER;
        } else {
            break;
        }
        add_occ(o);
        break;
    }

    case E_CALL: {
        occurrence o = {.at = e->at, .len = e->name.len, .name = e->name, .type = e->type, .call = e->call};
        // cells[x, y] is cells[int2(x, y)] to the checker: that int2 isn't in the source.
        const int name = token_at(e->at);
        const bool in_source = name >= 0 && str_eq(A.files[e->at.file].toks[name].text, e->name);
        if (e->call != CALL_NONE && in_source) vec_push(A.calls, e);
        if (converts_enum(e) && in_source) { // Card(2): the enum, even where its argument is refused
            o.kind = OCC_TYPE;
            o.decl = e->type_decl;
            add_occ(o);
        } else if ((e->call == CALL_CONSTRUCT || e->call == CALL_NEW_GRID || (e->resource_operation && e->type.kind == TY_RENDER_TARGET)) && in_source) {
            o.kind = OCC_TYPE;
            add_occ(o);
        } else if (e->call == CALL_SPAWN || e->call == CALL_SEND) {
            o.kind = OCC_FUNCTION;
            add_occ(o);
        } else if (e->call == CALL_BUILTIN && in_source && e->type.kind == TY_STRUCT && e->type.decl == A.prog->random) {
            o.kind = OCC_TYPE; // Random(seed): the type it makes
            o.decl = A.prog->random;
            add_occ(o);
        } else if (e->call == CALL_METHOD || e->call == CALL_FUNCTION) { // IsDead() in a method, or Heal(...)
            o.kind = e->call == CALL_METHOD ? OCC_METHOD : OCC_FUNCTION;
            o.decl = e->method;
            add_occ(o);
        } else if (e->call == CALL_ACTION) { // content(): the Action parameter
            o.kind = OCC_PARAM;
            o.param = e->param;
            add_occ(o);
        }
        walk_args(e);
        walk_stmt(e->block);
        break;
    }

    case E_METHOD: {
        walk_expr(e->object);
        for (int k = 0; k < 2; k++) { // items.Sort(a, b) { ... }: its two elements, declared by the call
            const stmt *local = e->compare[k];
            if (!local) continue;
            add_occ((occurrence){.at = local->name_at, .len = local->name.len, .kind = OCC_LOCAL, .declaration = true, .local = local,
                                 .name = local->name, .type = local->type});
        }
        occurrence o = {.at = e->at, .len = e->name.len, .name = e->name, .type = e->type, .object_type = e->object->type,
                        .call = e->call};
        if (e->call != CALL_NONE) vec_push(A.calls, e);
        // world.Get<Health>(e): the component in <>
        if (e->type_arg.len > 0) type_place(e->type_arg_ref, e->type_arg_qual_at, e->type_arg_at, e->type_arg, type_of_decl(e->type_decl));
        if (converts_enum(e)) { // Combat.Stance(2): the enum, after its namespace
            o.kind = OCC_TYPE;
            o.decl = e->type_decl;
            o.object_type = (type){TY_ERROR, NULL};
            add_occ(o);
        } else if (e->resource_operation && (e->object->type.kind == TY_RENDER_TARGET || e->object->type.kind == TY_CUBEMAP)) {
            o.kind = OCC_METHOD;
            add_occ(o);
        } else if (e->call == CALL_BUILTIN || e->call == CALL_DRAW || e->call == CALL_AUDIO || e->call == CALL_GUI
                   || e->call == CALL_TEXT || e->call == CALL_RANDOM) {
            o.kind = OCC_FUNCTION;
            o.owner = e->call == CALL_TEXT ? str_from("string") : e->call == CALL_RANDOM ? str_from("Random")
                      : e->targeted ? target_owner(e) : e->object->name;
            o.c_name = e->c_callee;
            add_occ(o);
        } else if (e->call == CALL_ADD || e->call == CALL_REMOVE || e->call == CALL_DESTROY || e->call == CALL_SEND
                   || e->call == CALL_LIST || e->call == CALL_MAP || e->call == CALL_GRID || e->call == CALL_SNAP
                   || e->call == CALL_DISCARD) {
            o.kind = OCC_METHOD; // e.Destroy(), items.Add(x), scores.TryGet(k, out v), cells.Clear(), this.Snap(), pixel.Discard()
            add_occ(o);
        } else if (e->call == CALL_LOAD || e->call == CALL_UNLOAD || e->call == CALL_SCENE_PLAYER) {
            add_occ((occurrence){.at = e->object->at, .len = 5, .kind = OCC_OWNER, .owner = str_from("Scene"),
                                 .name = str_from("Scene")});
            o.kind = OCC_FUNCTION;
            o.owner = str_from("Scene");
            add_occ(o);
        } else if (e->call == CALL_SESSION) {
            add_occ((occurrence){.at = e->object->at, .len = 7, .kind = OCC_OWNER, .owner = str_from("Session"),
                                 .name = str_from("Session")});
            o.kind = OCC_FUNCTION;
            o.owner = str_from("Session");
            add_occ(o);
        } else if (e->call == CALL_CLIPBOARD) {
            add_occ((occurrence){.at = e->object->at, .len = 9, .kind = OCC_OWNER, .owner = str_from("Clipboard"),
                                 .name = str_from("Clipboard")});
            o.kind = OCC_FUNCTION;
            o.owner = str_from("Clipboard");
            add_occ(o);
        } else if (e->call == CALL_WAIT) {
            add_occ((occurrence){.at = e->object->at, .len = 4, .kind = OCC_OWNER, .owner = str_from("Wait"),
                                 .name = str_from("Wait")});
            o.kind = OCC_FUNCTION;
            o.owner = str_from("Wait");
            add_occ(o);
        } else if (e->call == CALL_METHOD || e->call == CALL_FUNCTION) { // stats.IsDead(), Combat.Heal(...)
            o.kind = e->call == CALL_METHOD ? OCC_METHOD : OCC_FUNCTION;
            o.decl = e->method;
            add_occ(o);
        } else if (e->object->type.kind == TY_WORLD) { // world.Get<Health>(e), even one the checker refused
            o.kind = OCC_METHOD;
            o.call = CALL_WORLD;
            o.component = e->type_decl && e->type_decl->kind == DECL_COMPONENT ? e->type_decl : NULL;
            add_occ(o);
        }
        walk_args(e);
        walk_stmt(e->block);
        break;
    }

    case E_LITERAL:
        if (e->type_decl) {
            qualifier_ref(e->qual_at, e->at, e->name);
            const str name = last_part(e->name);
            add_occ((occurrence){.at = e->at, .len = name.len, .kind = OCC_TYPE, .decl = e->type_decl, .name = name});
        }
        for (int i = 0; i < e->inits.count; i++) {
            const field_init *init = &e->inits.items[i];
            if (init->field) {
                add_occ((occurrence){.at = init->at, .len = init->name.len, .kind = OCC_FIELD, .field = init->field,
                                     .decl = e->type_decl, .name = init->name, .type = init->field->type});
            }
            walk_expr(init->value);
        }
        break;

    case E_BINARY:
    case E_UNARY:
        if (e->method) { // A struct's operator
            add_occ((occurrence){.at = e->at, .len = operator_len(e->op), .kind = OCC_METHOD, .decl = e->method,
                                 .name = e->method->name, .type = e->type});
        }
        walk_expr(e->lhs);
        walk_expr(e->rhs);
        break;

    case E_CONDITIONAL:
        walk_expr(e->cond);
        walk_expr(e->lhs);
        walk_expr(e->rhs);
        break;

    case E_INTERP:
    case E_LIST:
        for (int i = 0; i < e->args.count; i++) walk_expr(e->args.items[i]);
        break;

    case E_INDEX:
        walk_expr(e->object);
        walk_expr(e->lhs);
        break;
    }
}

static void walk_stmt(const stmt *s)
{
    if (!s) return;
    switch (s->kind) {
    case S_BLOCK:
        for (int i = 0; i < s->stmts.count; i++) walk_stmt(s->stmts.items[i]);
        break;
    case S_IF:
        walk_expr(s->cond);
        walk_stmt(s->then_stmt);
        walk_stmt(s->else_stmt);
        break;
    case S_RETURN:
    case S_FAIL:
        walk_expr(s->value);
        break;
    case S_VAR:
        type_place(s->type_ref, s->type_qual_at.line ? s->type_qual_at : s->type_at, s->type_at, s->type_name, s->type);
        add_occ((occurrence){.at = s->name_at, .len = s->name.len, .kind = OCC_LOCAL, .declaration = true, .local = s,
                             .name = s->name, .type = s->type});
        walk_expr(s->value);
        break;
    case S_ASSIGN:
        if (s->operator_decl) { // `+=` with a struct's +
            add_occ((occurrence){.at = s->at, .len = operator_len(s->op), .kind = OCC_METHOD, .decl = s->operator_decl,
                                 .name = s->operator_decl->name});
        }
        walk_written = written_place(s->target);
        walk_expr(s->target);
        walk_written = NULL;
        walk_expr(s->value);
        break;
    case S_EXPR:
        walk_expr(s->value);
        break;
    case S_SWITCH:
        walk_expr(s->cond);
        for (int i = 0; i < s->cases.count; i++) {
            const switch_case *section = &s->cases.items[i];
            for (int k = 0; k < section->labels.count; k++) walk_expr(section->labels.items[k]);
            for (int k = 0; k < section->body.count; k++) walk_stmt(section->body.items[k]);
        }
        break;
    case S_BREAK:
    case S_CONTINUE:
        break;
    case S_WHILE:
        walk_expr(s->cond);
        walk_stmt(s->then_stmt);
        break;
    case S_FOREACH:
    case S_PARALLEL:
        type_place(s->type_ref, s->type_qual_at.line ? s->type_qual_at : s->type_at, s->type_at, s->type_name, s->type);
        add_occ((occurrence){.at = s->name_at, .len = s->name.len, .kind = OCC_LOCAL, .declaration = true, .local = s,
                             .name = s->name, .type = s->type});
        walk_expr(s->value);
        walk_expr(s->by);
        walk_expr(s->offset);
        walk_stmt(s->then_stmt);
        break;
    case S_FOR:
        walk_stmt(s->init);
        walk_expr(s->cond);
        walk_stmt(s->step);
        walk_stmt(s->then_stmt);
        break;
    }
}

static void walk_param(const param *p, const decl *d)
{
    if (p->mode == PARAM_QUERY) {
        const int keyword = token_at(p->at);
        if (keyword >= 0) A.files[p->at.file].query_keywords[keyword] = true;
        for (int i = 0; i < p->query.count; i++) walk_param(&p->query.items[i], d);
    } else {
        // `verified Health health`, `with verified Dead`, `event(verified Died died)`: the word, where the parser
        // read it as one, whatever the checker then made of it
        if (p->verified_at.line > 0) {
            add_occ((occurrence){.at = p->verified_at, .len = 8, .kind = OCC_VERIFIED, .param = p, .param_of = d,
                                 .name = str_from("verified")});
        }
        // `out Health health`, a function's or method's: the word, whatever the checker then made of it
        if (p->out_at.line > 0) {
            add_occ((occurrence){.at = p->out_at, .len = 3, .kind = OCC_OUT, .declaration = true, .param = p, .param_of = d,
                                 .name = str_from("out"), .type = p->type});
        }
        if (p->ownership) { // A word of its own there, not a type's name
            add_occ((occurrence){.at = p->type_at, .len = p->type_name.len, .kind = OCC_OWNERSHIP, .param = p,
                                 .param_of = d, .name = p->type_name});
            return;
        }
        type_place(p->type_ref, p->type_qual_at, p->type_at, p->type_name, p->type);
    }
    if (p->name.len > 0) {
        add_occ((occurrence){.at = p->name_at, .len = p->name.len, .kind = OCC_PARAM, .declaration = true, .param = p,
                             .param_of = d, .name = p->name, .type = p->type});
    }
}

static void walk_params(const decl *d)
{
    for (int i = 0; i < d->params.count; i++) walk_param(&d->params.items[i], d);
}

// A method or function: its name, signature and body.
static void walk_routine(const decl *m)
{
    // An operator's `at` is its `operator` keyword.
    add_occ((occurrence){.at = m->at, .len = m->is_operator ? 8 : m->name.len, .kind = m->owner ? OCC_METHOD : OCC_FUNCTION,
                         .declaration = true, .decl = m, .name = m->name});
    if (!str_eq_c(m->return_type_name, "void")) {
        type_place(m->return_type_ref, m->return_type_qual_at, m->return_type_at, m->return_type_name, m->return_type);
    }
    type_place(m->fails_ref, m->fails_type_qual_at, m->fails_type_at, m->fails_name, m->fails);
    walk_params(m);
    walk_code = m;
    walk_stmt(m->body);
    walk_code = NULL;
}

// The types a declaration's name is written with, where others share it:
// [After(Move(Position, with Player))]. Each is its parameter's.
static void signature_refs(const sig_item *items, const int count)
{
    for (int i = 0; i < count; i++) {
        const sig_item *item = &items[i];
        if (item->mode == PARAM_QUERY) signature_refs(item->query.items, item->query.count);
        else if (item->param && !item->param->ownership) type_place(item->type_ref, item->type_qual_at, item->type_at, item->type_name, item->param->type);
    }
}

// `virtual` and `override` before a declaration, and what an override's name
// is written with: the namespace of what it replaces, or a method's type.
static void replace_refs(const decl *d)
{
    if (d->virtual_at.line > 0) {
        add_occ((occurrence){.at = d->virtual_at, .len = 7, .kind = OCC_REPLACE, .decl = d, .name = str_from("virtual")});
    }
    if (d->override_at.line == 0) return;
    add_occ((occurrence){.at = d->override_at, .len = 8, .kind = OCC_REPLACE, .decl = d, .name = str_from("override")});
    const str name = last_part(d->override_name);
    if (name.len == d->override_name.len) return;
    const str before = {d->override_name.ptr, d->override_name.len - name.len - 1};
    const int name_token = token_at(d->at);
    if (d->owner && name_token >= 2) { // Combat.Stats.Hurt: the type, maybe with its namespace
        type_written(d->override_qual_at, A.files[d->at.file].toks[name_token - 2].at, before, type_of_decl(d->owner));
    } else if (!d->owner) {
        namespace_parts(d->override_qual_at, before, false);
    }
}

// In source order, and at one position in the order they were found: qsort
// leaves ties in any order.
static int occ_order(const void *a, const void *b)
{
    const occurrence *x = a;
    const occurrence *y = b;
    const int c = loc_cmp(x->at, y->at);
    return c != 0 ? c : x->order - y->order;
}

static void index_program(void)
{
    // `namespace X;` and `using Y;` at the top of each file
    for (int i = 0; i < A.prog->units.count; i++) {
        const unit *u = A.prog->units.items[i];
        if (u->ns.len > 0) namespace_parts(u->ns_at, u->ns, true);
        for (int k = 0; k < u->usings.count; k++) namespace_parts(u->using_at.items[k], u->usings.items[k], false);
    }

    for (int i = 0; i < A.written.count; i++) {
        const decl *d = A.written.items[i];
        if (d->builtin) continue;
        replace_refs(d);
        // Scheduling attributes: declarations or an enum and its phase member. An override in place has
        // those of what it replaced, which are walked with that one, where they're written
        for (int a = 0; a < (d->placed ? 0 : d->attributes.count); a++) {
            const attribute *at = &d->attributes.items[a];
            add_occ((occurrence){.at = at->at, .len = at->name.len, .kind = OCC_ATTRIBUTE, .name = at->name});
            for (int k = 0; k < d->attributes.items[a].args.count; k++) {
                const qname *q = &d->attributes.items[a].args.items[k];
                if (!q->decl) continue;
                const str name = last_part(q->text);
                if (q->enum_member) {
                    const str enum_name = {q->text.ptr, q->text.len - name.len - 1};
                    const int member_token = token_at(q->name_at);
                    if (member_token >= 2) {
                        const loc enum_at = A.files[q->at.file].toks[member_token - 2].at;
                        type_written(q->at, enum_at, enum_name, (type){TY_ENUM, q->decl});
                    }
                    add_occ((occurrence){.at = q->name_at, .len = name.len, .kind = OCC_ENUM_MEMBER,
                                         .decl = q->decl, .name = name});
                } else {
                    qualifier_ref(q->at, q->name_at, q->text);
                    add_occ((occurrence){.at = q->name_at, .len = name.len, .kind = OCC_SYSTEM,
                                         .decl = q->decl, .name = name});
                    signature_refs(q->signature.items, q->signature.count); // Move(Position, with Player): the types it's written with
                }
            }
            for (int v = 0; v < at->values.count; v++) walk_expr(at->values.items[v]);
        }
        if (d->kind == DECL_SYSTEM || d->kind == DECL_SHADER) {
            add_occ((occurrence){.at = d->at, .len = d->name.len, .kind = OCC_SYSTEM, .declaration = true, .decl = d,
                                 .name = d->name});
            walk_params(d);
            walk_code = d;
            walk_stmt(d->body);
            walk_code = NULL;
            continue;
        }
        if (d->kind == DECL_FUNCTION || d->kind == DECL_METHOD) { // A method here is an override of one, outside its type
            walk_fields_of = d->owner;
            walk_routine(d);
            walk_fields_of = NULL;
            continue;
        }
        if (d->kind == DECL_CONST || d->kind == DECL_ASSET) {
            add_occ((occurrence){.at = d->at, .len = d->name.len, .kind = OCC_CONST, .declaration = true, .decl = d,
                                 .name = d->name, .type = d->return_type});
            type_place(d->return_type_ref, d->return_type_qual_at, d->return_type_at, d->return_type_name, d->return_type);
            walk_expr(d->value);
            continue;
        }
        add_occ((occurrence){.at = d->at, .len = d->name.len, .kind = OCC_TYPE, .declaration = true, .decl = d,
                             .name = d->name});
        if (d->kind == DECL_ENUM && d->backing.len > 0) { // enum Voxel : byte
            add_occ((occurrence){.at = d->backing_at, .len = d->backing.len, .kind = OCC_TYPE, .name = d->backing,
                                 .type = {TY_INT, NULL}, .backing = true});
        }
        for (int m = 0; m < d->members.count; m++) {
            const enum_member *member = &d->members.items[m];
            add_occ((occurrence){.at = member->at, .len = member->name.len, .kind = OCC_ENUM_MEMBER, .declaration = true,
                                 .decl = d, .name = member->name});
            walk_expr(member->value);
        }
        // Its fields, then its local ones (`local List<int> faces;`), which the
        // checker keeps apart, in its local part
        for (int f = 0; f < d->fields.count + local_field_count(d); f++) {
            const field *fl = field_of(d, f);
            type_place(fl->type_ref, fl->type_qual_at.line ? fl->type_qual_at : fl->type_at, fl->type_at, fl->type_name, fl->type);
            add_occ((occurrence){.at = fl->at, .len = fl->name.len, .kind = OCC_FIELD, .declaration = true, .field = fl,
                                 .decl = d, .name = fl->name, .type = fl->type});
            walk_expr(fl->default_value);
            for (int a = 0; a < fl->attributes.count; a++) {
                const attribute *at = &fl->attributes.items[a];
                add_occ((occurrence){.at = at->at, .len = at->name.len, .kind = OCC_ATTRIBUTE, .name = at->name});
                for (int v = 0; v < at->values.count; v++) walk_expr(at->values.items[v]);
            }
        }
        walk_fields_of = d;
        if (d->body) { // The input's Sample
            add_occ((occurrence){.at = d->body_at, .len = 6, .kind = OCC_METHOD, .decl = d, .name = str_from("Sample")});
            walk_params(d);
            walk_stmt(d->body);
        }
        if (d->sanitize) {
            add_occ((occurrence){.at = d->sanitize_at, .len = 8, .kind = OCC_METHOD, .decl = d,
                                 .name = str_from("Sanitize")});
            walk_stmt(d->sanitize);
        }
        for (int k = 0; k < d->methods.count; k++) {
            replace_refs(method_written(d->methods.items[k]));
            walk_routine(method_written(d->methods.items[k]));
        }
        walk_fields_of = NULL;
    }

    for (int b = 0; b < A.prog->settings.count; b++) {
        const decl *block = A.prog->settings.items[b];
        for (int f = 0; f < block->fields.count; f++) {
            const field *fl = &block->fields.items[f];
            if (setting_named(fl->name)) {
                add_occ((occurrence){.at = fl->at, .len = fl->name.len, .kind = OCC_SETTING, .name = fl->name,
                                     .type = fl->type});
            }
            walk_expr(fl->default_value);
        }
    }

    // In source order, one per position: the first found there.
    if (A.occs.count > 0) qsort(A.occs.items, (size_t)A.occs.count, sizeof(occurrence), occ_order); // None is no array to hand it
    int n = 0;
    for (int i = 0; i < A.occs.count; i++) {
        if (n > 0 && loc_cmp(A.occs.items[n - 1].at, A.occs.items[i].at) == 0) continue;
        A.occs.items[n++] = A.occs.items[i];
    }
    A.occs.count = n;
}

void analysis_run(const analysis_file *files, const int count)
{
    arena_reset();
    memset(&A, 0, sizeof A);
    diag_reset();
    diag_set_sink(collect, NULL);
    diag_set_suggestion_sink(collect_suggestion, NULL);

    A.file_count = count;
    A.files = arena_alloc(sizeof(afile) * (size_t)(count > 0 ? count : 1));
    A.prog = program_new();
    A.prog->game_folder = asset_roots.game_folder; // The checker keeps the assets' hashes and fetched URLs there
    for (int i = 0; i < count; i++) {
        afile *f = &A.files[i];
        char *copy = arena_alloc(files[i].len + 1);
        memcpy(copy, files[i].text, files[i].len);
        f->src = (source){files[i].path, copy, files[i].len, 0, files[i].package};
        f->uri = files[i].uri;
        diag_add_source(&f->src);

        const char *p = copy;
        if (files[i].len >= 3 && memcmp(p, "\xEF\xBB\xBF", 3) == 0) p += 3; // The lexer counts columns after it.
        vec_push(f->lines, p);
        for (; p < copy + files[i].len; p++) {
            if (*p == '\n') vec_push(f->lines, p + 1);
        }

        const int errors_before = diag_error_count();
        lex_spans spans = {0};
        f->toks = lex_all_spans(&f->src, &spans);
        f->lex_errors = diag_error_count() - errors_before;
        f->inactive = arena_alloc((size_t)f->lines.count + 2);
        memset(f->inactive, 0, (size_t)f->lines.count + 2);
        for (int k = 0; k < spans.count; k++) {
            for (int l = spans.items[k].first; l <= spans.items[k].last && l <= f->lines.count; l++) {
                f->inactive[l] = spans.items[k].directive ? LINE_DIRECTIVE : LINE_LEFT_OUT;
            }
        }
        while (f->toks[f->tok_count].kind != T_EOF) f->tok_count++;
        f->query_keywords = arena_alloc(sizeof(bool) * ((size_t)f->tok_count + 1));
        parse_file(A.prog, &f->src, f->toks, true);
    }
    A.syntax_errors = diag_error_count();
    check(A.prog);
    diag_set_sink(NULL, NULL);
    diag_set_suggestion_sink(NULL, NULL);

    list_written();
    index_program();
    collect_assets();
    check_assets();
}

bool analysis_select(const char *uri)
{
    for (int i = 0; i < A.file_count; i++) {
        if (strcmp(A.files[i].uri, uri) == 0) {
            A.doc = i;
            return true;
        }
    }
    return false;
}

int analysis_file_count(void)
{
    return A.file_count;
}

const char *analysis_file_uri(const int file)
{
    return A.files[file].uri;
}

const char *analysis_file_path(const int file)
{
    return A.files[file].src.path;
}

// ---------------------------------------------------------------------------
// Diagnostics

// A range over the token at `at`, or a character where there's none.
static void write_token_range(jbuf *out, const loc at)
{
    const int t = token_at(at);
    write_range(out, at, t >= 0 ? token_len(&A.files[at.file].toks[t]) : 1);
}

// Whether a warning is about a local nothing reads. Editors fade it out.
static bool unused_local(const diagnostic *d)
{
    for (int i = 0; d->severity == DIAG_WARNING && i < A.prog->fixes.count; i++) {
        const fix *f = &A.prog->fixes.items[i];
        if (f->kind != FIX_UNUSED_LOCAL && f->kind != FIX_REMOVE_LOCAL && f->kind != FIX_KEEP_CALL && f->kind != FIX_DISCARD_OUT) continue;
        if (loc_cmp(d->at, f->at) == 0) return true;
    }
    return false;
}

// Whether a warning is about access a system, frag or vert declares and
// doesn't use: a parameter never used, or mut and never written. Editors fade
// it out.
static bool unused_param(const diagnostic *d, const param *p)
{
    if (p->mode == PARAM_QUERY) {
        for (int i = 0; i < p->query.count; i++) {
            if (unused_param(d, &p->query.items[i])) return true;
        }
    }
    if (p->name.len == 0 || (p->type.kind != TY_COMPONENT && p->type.kind != TY_SINGLETON
                            && p->type.kind != TY_WORLD && p->type.kind != TY_QUERY)) return false;
    if (!p->read && loc_cmp(d->at, p->name_at) == 0) return true;
    return p->read && p->mode == PARAM_MUT && !p->written && loc_cmp(d->at, p->at) == 0;
}

static bool unused_access(const diagnostic *d)
{
    for (int i = 0; d->severity == DIAG_WARNING && i < A.prog->decls.count; i++) {
        const decl *sys = A.prog->decls.items[i];
        for (int k = 0; (sys->kind == DECL_SYSTEM || is_shader(sys)) && k < sys->params.count; k++) {
            if (unused_param(d, &sys->params.items[k])) return true;
        }
    }
    return false;
}

static void write_diagnostic(jbuf *out, const diagnostic *d)
{
    const loc at = d->at.line > 0 ? d->at : (loc){1, 1, d->at.file};
    jb_put(out, "{\"range\":");
    write_token_range(out, at);
    jb_printf(out, ",\"severity\":%d,\"source\":\"tidec\",\"message\":", d->severity == DIAG_ERROR ? 1 : 2);
    jb_string(out, d->message.data ? d->message.data : "");
    if (unused_access(d) || unused_local(d)) jb_put(out, ",\"tags\":[1]"); // Unnecessary
    if (d->related.count > 0) {
        jb_put(out, ",\"relatedInformation\":[");
        for (int i = 0; i < d->related.count; i++) {
            const related_note *note = &d->related.items[i];
            jb_put(out, i ? ",{\"location\":{\"uri\":" : "{\"location\":{\"uri\":");
            jb_string(out, A.files[note->at.file].uri);
            jb_put(out, ",\"range\":");
            write_token_range(out, note->at);
            jb_put(out, "},\"message\":");
            jb_string(out, note->message);
            jb_put(out, "}");
        }
        jb_put(out, "]");
    }
    jb_put(out, "}");
}

void analysis_diagnostics(const int file, jbuf *out)
{
    jb_put(out, "[");
    int written = 0;
    for (int i = 0; i < A.diags.count; i++) {
        if (A.diags.items[i].at.file != file) continue;
        if (written++) jb_put(out, ",");
        write_diagnostic(out, &A.diags.items[i]);
    }
    jb_put(out, "]");
}

// ---------------------------------------------------------------------------
// Descriptions, for hovers and completion details

static const char *builtin_type_doc(const type_kind kind)
{
    switch (kind) {
    case TY_BOOL: return "true or false.";
    case TY_BYTE: return "An unsigned 8-bit integer, from 0 to 255. Stored as one byte; arithmetic promotes to int. "
                         "byte(x) keeps the low 8 bits. Converts to int, long and float by itself.";
    case TY_INT: return "A 32-bit integer. Arithmetic wraps on overflow; dividing by zero gives 0.";
    case TY_LONG: return "A 64-bit integer. Arithmetic wraps on overflow; dividing by zero gives 0. An int converts to it "
                         "by itself; floats convert with long(x) and float(x), and int(x) keeps its low 32 bits.";
    case TY_FLOAT: return "A 32-bit float. Results are identical on every platform.";
    case TY_INT2: case TY_INT3: case TY_INT4: return "A vector of ints: x, y, z, w.";
    case TY_FLOAT2: case TY_FLOAT3: case TY_FLOAT4: return "A vector of floats: x, y, z, w. Swizzles like `v.xz` work.";
    case TY_QUATERNION: return "A rotation. Combine rotations with Math.Mul and rotate vectors with Math.Rotate.";
    case TY_FLOAT2X2: case TY_FLOAT3X3: case TY_FLOAT4X4: return "A matrix, stored column by column (c0, c1, ...).";
    case TY_ENTITY: return "A handle to an entity. It stays safe to use after the entity is destroyed.";
    case TY_LOCAL_ENTITY: return "A handle to an entity of the local world: this machine's own, which only local code holds.";
    case TY_PLAYER: return "A player, independent of connections. `PlayerID(0)` names a player by index.";
    case TY_COLOR: return "A color: r, g, b and a from 0 to 1. `Color(r, g, b)` or `Color(r, g, b, a)`.";
    case TY_TEXTURE: return "A local image reference, sampled on the GPU with `Sample(uv[, filter[, wrap]])`. A Blit supplies its source to a read-only Texture shader parameter.";
    case TY_DEPTH_TEXTURE: return "A local reference to a render target's depth, `target.depth`, which a frag or vert reads where it's a field of a component or singleton it takes: `Sample(uv)` gives how far the nearest thing drawn there is, 0 at the near plane to 1 at the far limit, and `Compare(uv, value)` how much of what's stored around there isn't nearer than `value`, 0 to 1, as a shadow wants.";
    case TY_CUBEMAP: return "A local reference to a cube of six images, which a frag or vert reads by a direction from its middle, `Sample(direction)`, where it's an asset by its name or a field of a component or singleton it takes. An asset's is six files a glob matches, named for their faces: `asset Cubemap sky = \"art/sky_??.png\";`. `Cubemap(size)` is one a view draws into, a face at a time: `cube.faces[0]` to `cube.faces[5]` are RenderTargets, in the order +x, -x, +y, -y, +z, -z.";
    case TY_RENDER_TARGET: return "A local drawing destination. `RenderTarget(width, height)` is one of that size in logical pixels, like the window's, at the display's full resolution; `RenderTarget(width, height, 1)` has exactly that many pixels, whatever the display; `RenderTarget.Window()` is always the window's size. `samples: 4`, after everything else, draws each of its pixels with that many samples, which smooths the edges of the triangles drawn into it. Each says last what it holds when that isn't one color image: `Format.Depth` is depth alone, and a format for each makes several, `RenderTarget(width, height, Format.Rgba8, Format.Rgba16f)`, which a frag's `out Color` parameters go to in order. Drawing into one is as drawing into the window. Copies share the image; `Resize(width, height)` discards its contents and `Dispose()` releases it. Color and depth persist across frames. `target.color` is its Texture, `target.colors[1]` its second color image's, and `target.depth` its DepthTexture; `Draw.window` is the window destination.";
    case TY_RECT: return "A rectangle on the screen, for the GUI: x and y from the top left, y down, then width and height.";
    case TY_CLIP: return "A sound of the game's pack, this machine's to play: an `asset Clip` by its name, or a copy of one. `Audio.Play(clip[, position][, volume[, pitch]])` plays it once, from views, local handlers and the functions they call; the match can't use it.";
    case TY_STRING: return "Text, written in double quotes.";
    case TY_ACTION: return "Code the caller writes in braces after the call, run with `content();`.";
    case TY_DRAW_LIST: return "The frame's draw list, for a game's C to draw into with tide/draw.h: an extern "
                              "function takes one, and a view passes it `Draw.list`.";
    case TY_ARRAY: return "An array: a fixed number of values side by side in whatever holds it, with nothing in the heap. "
                          "`cells[i]`, `cells[i] = x`, `count`, `foreach`; past the end a read gives zero and a write does nothing.";
    case TY_LIST: return "A list of values, which grows and shrinks: `Count`, `items[i]`, `Add`, `RemoveAt`, `foreach`, "
                         "and `parallel` through its elements by index.";
    case TY_MAP: return "Values by key, in the order the keys were added: `scores[key]` reads (the value type's zero when "
                        "the key isn't there) and `scores[key] = value` writes, `Has`, `Get` (a `V?`), `Remove`, `Clear`, "
                        "`count`, and `foreach` through its entries, each with `key` and `value`. Keys are ints, longs, "
                        "enums, entities, players or text.";
    case TY_SET: return "Keys, each once, in the order they were added: `Add` (whether it was new), `Has`, `Remove`, "
                        "`Clear`, `count`, and `foreach` through them. `[a, b]` makes one where a set goes.";
    case TY_GRID: return "Cells at positions, kept in chunks only where something's set: `cells[x, y]`, `size`, `Clear()`, "
                         "and `foreach` or `parallel` through its cells. "
                         "Its size is given when it's made, `Grid2(1024, 1024)`; 0 or nothing is open.";
    case TY_WORLD: return "Any entity's components, by its handle, for a system, view or event handler that takes "
                          "`World world`, and for the functions and methods it passes it on to: "
                          "`world.Get<Health>(entity)` is a copy of that entity's `Health` (its zero value "
                          "when it has none), `world.Has<Health>(entity)` tells, `world.TryGet<Health>(entity, out var "
                          "health)` is both in one, and with `mut World`, "
                          "`world.Set<Health>(entity, value)` puts one back. What the code reaches this way, itself or "
                          "through a function, counts as access to every entity's component: the schedule shows it.";
    default: return NULL;
    }
}

static const char *decl_keyword(const decl *d)
{
    switch (d->kind) {
    case DECL_COMPONENT: return d->is_scene ? "scene" : "component";
    case DECL_SINGLETON: return "singleton";
    case DECL_INPUT: return "input";
    case DECL_RECORD: return "record";
    case DECL_STRUCT: return "struct";
    case DECL_METHOD: return "method";
    case DECL_FUNCTION: return "function";
    case DECL_EVENT: return d->is_command ? "command" : "event";
    case DECL_ENUM: return "enum";
    case DECL_LIST: return "list";
    case DECL_MAP: return "map";
    case DECL_GRID: return "grid";
    case DECL_ARRAY: return "array";
    case DECL_RESULT: return "result";
    case DECL_CONST: return "const";
    case DECL_ASSET: return "asset";
    case DECL_SETTINGS: return "settings";
    case DECL_SHADER: return d->is_frag ? "frag" : "vert";
    case DECL_SYSTEM: return d->is_view ? "view" : d->is_handler ? d->is_command ? "command" : "event" : "system";
    }
    return "";
}

// What a declaration is, in words: "event handler" rather than the keyword,
// and "local component" for what's local.
static const char *decl_what(const decl *d)
{
    const char *what = d->kind == DECL_SYSTEM && d->is_handler ? d->is_command ? "command handler" : "event handler"
                     : d->extern_system                        ? "extern system" // Written in C
                                                               : decl_keyword(d);
    if (!d->is_local) return what;
    sb b = {0};
    sb_printf(&b, "local %s", what);
    return b.data;
}

static void format_param(const param *p, sb *out)
{
    if (p->mode == PARAM_QUERY) {
        sb_put(out, "query (");
        for (int i = 0; i < p->query.count; i++) {
            if (i) sb_put(out, ", ");
            format_param(&p->query.items[i], out);
        }
        sb_printf(out, ") " STR_FMT, STR_ARG(p->name));
        return;
    }
    switch (p->mode) {
    case PARAM_MUT: sb_put(out, p->out_at.line > 0 ? "out " : "mut "); break; // As written, where the checker refused it too
    case PARAM_IN: sb_put(out, "in "); break;
    case PARAM_WITH: sb_put(out, "with "); break;
    case PARAM_WITHOUT: sb_put(out, "without "); break;
    case PARAM_READ: case PARAM_EVENT: case PARAM_QUERY: case PARAM_SENDER: break;
    }
    if (p->verified_at.line > 0) sb_put(out, "verified "); // As written, where the checker refused it too
    sb_printf(out, STR_FMT, STR_ARG(p->type_name));
    if (p->name.len > 0) sb_printf(out, " " STR_FMT, STR_ARG(p->name));
    if (p->default_value) sb_printf(out, " = " STR_FMT, STR_ARG(p->default_text)); // One a call can leave out
}

// `system Move(mut Body body)`, or a handler's `event(Hit hit) TakeHit(mut Health health)`.
static void format_header(const decl *d, sb *out)
{
    int first = 0;
    if (d->is_virtual) sb_put(out, "virtual ");
    if (d->is_override) sb_put(out, "override ");
    if (d->is_local) sb_put(out, "local ");
    if (d->is_async) sb_put(out, "async ");
    if (d->is_handler && d->params.count > 0) {
        sb_put(out, d->is_command ? "command(" : "event(");
        format_param(&d->params.items[0], out);
        if (d->is_command && d->params.count > 1) {
            sb_put(out, ", ");
            format_param(&d->params.items[1], out);
        }
        sb_printf(out, ") " STR_FMT "(", STR_ARG(d->name));
        first = d->is_command ? 2 : 1;
    } else {
        sb_printf(out, "%s%s " STR_FMT "(", d->extern_system ? "extern " : "", decl_keyword(d), STR_ARG(d->name));
    }
    for (int i = first; i < d->params.count; i++) {
        if (i > first) sb_put(out, ", ");
        format_param(&d->params.items[i], out);
    }
    sb_put(out, ")");
}

// Which of an extern system's parameters `p` is, as its C function counts
// them: its components from 0 (tide/rows.h), and its singletons from 0, each
// an argument after the rows. -1 for a filter.
static int extern_param_number(const decl *sys, const param *p)
{
    int number = 0;
    for (int i = 0; i < sys->params.count; i++) {
        const param *q = &sys->params.items[i];
        if (q->mode != PARAM_READ && q->mode != PARAM_MUT) continue;
        if (q == p) return number;
        if (q->type.kind == p->type.kind) number++;
    }
    return -1;
}

// What the engine's own events are for.
static const char *builtin_event_doc(const program *prog, const decl *d)
{
    if (d == prog->spawned) return "Sent to each entity as it's spawned: its handlers set new entities up.";
    if (d == prog->destroyed) return "Sent to each entity as it's destroyed, while its components can still be read.";
    if (d == prog->player_joined) return "Sent to the world when a player joins.";
    if (d == prog->player_left) return "Sent to the world when a player leaves.";
    if (d == prog->forgotten) {
        return "Sent as an entity's local fields go: the entity is in neither the world views draw nor the last one the "
               "server confirmed, or lost the component, or the match is over. A local handler takes the component (or "
               "singleton) whose local fields are going, and reads them one last time, to let go of what they hold.";
    }
    return NULL;
}

// `mut void Damage(float amount)`: a method's or function's signature.
static void format_routine(const decl *m, sb *out)
{
    sb_printf(out, "%s%s%s%s" STR_FMT " " STR_FMT "(", m->is_virtual ? "virtual " : m->is_override ? "override " : "",
              m->is_extern ? "extern " : "", m->is_async ? "async " : "", m->is_mut_method ? "mut " : "",
              STR_ARG(m->return_type_name), STR_ARG(m->name));
    for (int i = 0; i < m->params.count; i++) {
        if (i) sb_put(out, ", ");
        format_param(&m->params.items[i], out);
    }
    sb_put(out, ")");
    if (m->fails_name.len > 0) sb_printf(out, " fails " STR_FMT, STR_ARG(m->fails_name));
}

// The source text of a value after a name, from the tokens between '=' and
// ';': a field's default, or a constant's value.
static void format_value_after(const loc name_at, sb *out)
{
    const int name = token_at(name_at);
    if (name < 0) return;
    const token *toks = A.files[name_at.file].toks;
    if (toks[name + 1].kind != T_ASSIGN) return;
    const int first = name + 2;
    int last = first;
    // To the ';', or the '}' that closes the type the field is in: not one of a
    // value's own, like Stats { armor = 2 }.
    for (int depth = 0; toks[last].kind != T_EOF; last++) {
        if (depth == 0 && (toks[last].kind == T_SEMI || toks[last].kind == T_RBRACE)) break;
        if (toks[last].kind == T_LBRACE) depth++;
        else if (toks[last].kind == T_RBRACE) depth--;
    }
    if (last == first) return;
    const char *start = toks[first].text.ptr - (toks[first].kind == T_STRING ? 1 : 0);
    const token *end_tok = &toks[last - 1];
    const char *end = end_tok->text.ptr + token_len(end_tok) - (end_tok->kind == T_STRING ? 1 : 0);
    sb_put(out, " = ");
    sb_putn(out, start, (size_t)(end - start));
}

static void format_default(const field *f, sb *out)
{
    format_value_after(f->at, out);
}

static void format_data_decl(const decl *d, sb *out)
{
    sb_printf(out, "%s%s " STR_FMT, d->is_local ? "local " : "", decl_keyword(d), STR_ARG(d->name));
    if (d->kind == DECL_ENUM && d->backing.len > 0) sb_printf(out, " : " STR_FMT, STR_ARG(d->backing));
    sb_put(out, "\n{\n");
    for (int i = 0; i < d->members.count; i++) {
        sb_printf(out, "    " STR_FMT " = %lld,\n", STR_ARG(d->members.items[i].name), (long long)d->members.items[i].number);
    }
    for (int i = 0; i < d->fields.count + local_field_count(d); i++) {
        const field *f = field_of(d, i);
        if (f->hidden) continue;
        const char *type = f->type_name.len > 0 ? NULL : type_name(f->type);
        sb_printf(out, "    %s", f->is_local ? "local " : "");
        if (type) sb_printf(out, "%s " STR_FMT, type, STR_ARG(f->name));
        else sb_printf(out, STR_FMT " " STR_FMT, STR_ARG(f->type_name), STR_ARG(f->name));
        if (!d->builtin) format_default(f, out);
        sb_put(out, ";\n");
    }
    for (int i = 0; i < d->methods.count; i++) {
        sb_put(out, i == 0 && d->fields.count > 0 ? "\n    " : "    ");
        format_routine(d->methods.items[i], out);
        sb_put(out, ";\n");
    }
    if (d == A.prog->pixel) sb_put(out, "\n    void Discard();\n"); // Its one method (pixel_calls)
    sb_put(out, "}");
}

static void code_block(sb *out, const char *code)
{
    sb_printf(out, "```tide\n%s\n```", code);
}

static void describe_order(const decl *d, sb *out);

// What a Button's fields mean. Engines disagree on these names (raylib's
// "down" is Unity's "pressed"), so hovers and completions spell it out.
static const char *button_field_doc(const decl *d, const str name)
{
    if (!d || d->kind != DECL_RECORD || !str_eq_c(d->name, "Button")) return NULL;
    if (str_eq_c(name, "pressed")) {
        return "True while the button is held: down at any point since the last tick, so a quick tap is never missed.";
    }
    if (str_eq_c(name, "down")) return "True on the tick the button went down.";
    if (str_eq_c(name, "up")) return "True on the tick the button went up.";
    return NULL;
}

#define KEYBOARD_TEXT_DOC \
    "What was typed since the last frame, as text. Unlike keys, it follows the keyboard's layout, and what's " \
    "pasted comes through it too. Views read it, and the functions they call: it's never in the input. It's " \
    "empty while the GUI or another view has the keyboard."

static bool keyboard_text(const type object, const str name)
{
    return object.kind == TY_RECORD && object.decl && str_eq_c(object.decl->name, "Keyboard") && str_eq_c(name, "text");
}

// A built-in call the checker takes by name, for hovers, completion, signature
// help and inlay hints: its form and what it does.
typedef struct method_form {
    const char *name;
    const char *form; // In a list's and a grid's methods, T is the element or the cell
    const char *doc;
} method_form;

// "Start, Open and Close": the names of the calls in `forms`.
static void put_names(sb *out, const method_form *forms, const size_t count)
{
    for (size_t i = 0; i < count; i++) {
        sb_printf(out, "%s%s", i == 0 ? "" : i + 1 == count ? " and " : ", ", forms[i].name);
    }
}

// Scene's functions.
static const method_form scene_calls[] = {
    {"Load", "Scene.Load(scene, SceneVisibility visibility = SceneVisibility.Public)",
     "Loads a scene and returns its entity. Its `Spawned` handlers set it up at the end of the tick; it's public unless it's `SceneVisibility.Private`."},
    {"Unload", "Scene.Unload(scene)", "Unloads a scene at the end of the tick, destroying every entity in it."},
    {"AddPlayer", "Scene.AddPlayer(scene, PlayerID player)", "Lets a player see a private scene."},
    {"RemovePlayer", "Scene.RemovePlayer(scene, PlayerID player)", "Stops a player seeing a private scene."},
};

// Session's calls, from views and local handlers.
static const method_form session_calls[] = {
    {"Start", "Session.Start(scene = Main)",
     "Starts a match on this machine, in `scene`, or in `Main` with nothing named: it runs the server, and no one else "
     "joins until it's opened. It leaves the match it's in first."},
    {"Open", "Session.Open(int port = 7777)",
     "Lets others join the match this machine runs: in a room, whose code is `Session.room`, and on `port` too (7777 "
     "unless it says), except on the web."},
    {"Close", "Session.Close()", "No one else joins the match this machine runs from now on; the players in it stay."},
    {"Kick", "Session.Kick(PlayerID player, string message = \"\")",
     "Sends a player on another machine out of the match this machine runs. They get `Disconnected` with `Kicked` and "
     "`message`, and `PlayerLeft` follows. They can join again while the match is open."},
    {"KickAll", "Session.KickAll(string message = \"\")",
     "Sends every player on another machine out of the match this machine runs, with `message`, as `Kick` does."},
    {"Join", "Session.Join(string code)",
     "Joins the match in the room with `code`, like \"K7QF2M\": its host's `Session.room`. It leaves the match it's in first."},
    {"Connect", "Session.Connect(string address, int port = 7777)",
     "Joins the match at `address`, like \"192.168.1.5\" or \"localhost\", on `port` (7777 unless it says). It leaves the "
     "match it's in first."},
    {"Leave", "Session.Leave()",
     "Leaves the match: `Disconnected` follows, and views stop seeing it. With host migration, a match this machine "
     "runs goes on, with another machine as its server."},
    {"End", "Session.End()",
     "Ends the match this machine runs, for everyone: every player goes offline with `Ended`, and no other machine "
     "takes it over. On a client, it does nothing."},
};

#define GRID_SIZE_DOC                                                                                                  \
    "A grid of that size, in cells, whose cell type comes from where it goes: 0 or nothing leaves an axis open."
#define GRID_SIZE_FIELD_DOC "The grid's size, 0 on the axes where it's open."
// The built-in struct Random (its methods' docs are in builtins.c).
#define RANDOM_DOC                                                                                                     \
    "A random number generator's state, 8 bytes that live where the game puts them: a field of a component (a "       \
    "sequence for each entity), of a singleton (one for the match) or of local state, or a local. Each `Next` call "  \
    "steps it, so the code needs it `mut`, and its numbers are the same on every machine. One the game leaves "        \
    "unseeded in a component or a singleton is seeded by the engine, from the match's seed and its entity; "          \
    "`Random(seed)` is one for a seed of the game's."
#define RANDOM_SEEDED_DOC                                                                                              \
    "A generator for a seed: the same seed, the same numbers. Seeds next to each other (1, 2, 3) give sequences that " \
    "have nothing to do with each other."
#define TIME_SEED_DOC                                                                                                  \
    "The match's seed, from 0 to 2147483647: the machine that starts the match picks it, so no two matches go the "   \
    "same, and every player has it with the world. The engine seeds the match's `Random`s from it. 0 where no host "  \
    "picks one, as in tests."

#define LIST_COUNT_DOC "How many elements the list has."
#define ARRAY_COUNT_DOC "How many elements the array holds: its length, a constant."
#define MAP_COUNT_DOC "How many keys it has."
#define DEVICE_ARRAY_COUNT_DOC "How many fingers the touchscreen follows at once: every slot, touching or not."
#define TEXT_LENGTH_DOC "How many characters the text has."

#define CLIPBOARD_COPY_FORM "Clipboard.Copy(string text)"
#define CLIPBOARD_COPY_DOC                                                                                             \
    "Puts `text` on this machine's clipboard, after the frame: up to 255 bytes for now. A browser takes it only "     \
    "shortly after a click or a key, as when a button is pressed. Pasting needs no call: Ctrl+V types what's on the " \
    "clipboard into the text field that has the focus."

// What async code waits for, after `await`.
static const method_form wait_calls[] = {
    {"Ticks", "await Wait.Ticks(int ticks)",
     "Waits for the match's ticks: the task goes on `ticks` ticks later, in the task pass at the end of the tick. "
     "Match code only."},
    {"Frames", "await Wait.Frames(int frames)",
     "Waits for this machine's frames: the task goes on `frames` frames later, at the end of the frame. Local code only."},
    {"Seconds", "await Wait.Seconds(float seconds)",
     "Waits for `seconds`: in the match, the nearest whole number of ticks, the same on every machine; in local code, "
     "this machine's time, frame by frame."},
};

// A list's methods, as the checker takes them.
static const method_form list_calls[] = {
    {"Add", "Add(T item)", "Adds an element at the end."},
    {"Insert", "Insert(int index, T item)", "Adds an element at `index`, moving the ones after it along."},
    {"RemoveAt", "RemoveAt(int index)", "Removes the element at `index`, moving the ones after it back."},
    {"Remove", "Remove(T item) -> bool", "Removes the first element equal to `item`, if there is one, and says whether it did."},
    {"Clear", "Clear()", "Removes every element."},
    {"Resize", "Resize(int count)", "Makes the list `count` elements long, in one go: new ones are the element type's zero, and with fewer, the rest go. A count under 0 is 0."},
    {"Contains", "Contains(T item) -> bool", "Whether an element is equal to `item`."},
    {"IndexOf", "IndexOf(T item) -> int", "Where the first element equal to `item` is, or -1."},
    {"Sort", "Sort(a, b) { ... }", "Sorts the elements, stable: the block says whether `a` goes before `b`, as `return a.x < b.x;`. It changes nothing else."},
};

// A map's and a set's methods, as the checker takes them: reading and writing values is indexing.
static const method_form map_calls[] = {
    {"Has", "Has(K key) -> bool", "Whether the key is in the map."},
    {"Get", "Get(K key) -> V?", "The key's value, or nothing when it isn't there."},
    {"TryGet", "TryGet(K key, out V value) -> bool",
     "Whether the key is in the map, and through `out`, its value, or the value type's default when it isn't there: "
     "`if (scores.TryGet(key, out var score)) { ... }`."},
    {"Remove", "Remove(K key) -> bool", "Removes the key and its value, if it's there, and says whether it was."},
    {"Clear", "Clear()", "Removes every key."},
};

static const method_form set_calls[] = {
    {"Add", "Add(K key) -> bool", "Adds the key, and says whether it was new."},
    {"Has", "Has(K key) -> bool", "Whether the key is in the set."},
    {"Remove", "Remove(K key) -> bool", "Removes the key, if it's there, and says whether it was."},
    {"Clear", "Clear()", "Removes every key."},
};

static const method_form grid_calls[] = {
    {"Clear", "Clear()", "Sets every cell back to zero, keeping the size."},
    {"Sample", "Sample(float2 uv, Filter filter = the grid's, Wrap wrap = the grid's) -> Color",
     "Reads the grid as the texture it is, in a frag or vert: its color at `uv`, from (0, 0) at its first cell to "
     "(1, 1) past its last, blended between cells (`Filter.Bilinear`), the nearest cell's (`Filter.Point`), or "
     "blended between the grid's smaller images too where it's drawn smaller than it is (`Filter.Trilinear`, in a "
     "frag, of a `uv` the frag can work out as it starts). A "
     "`uv` past 0 or 1 reads the cells at the grid's edge (`Wrap.Clamp`), the grid again (`Wrap.Repeat`), or again "
     "the other way round each time (`Wrap.Mirror`). A Sample that leaves one out has what the grid's declaration "
     "says (`[Filter]`, `[Wrap]`), and blended and clamped without one. A `Grid2<float>` is a texture of one "
     "channel: its number is the color's `r`, with `g` and `b` 0 and `a` 1. The grid is a field of a component or "
     "singleton the frag or vert takes, or an asset by its name, and the filter and the wrap are written there: the "
     "GPU is told all three before any pixel runs."},
};

static const method_form texture_calls[] = {
    {"Sample", "Sample(float2 uv, Filter filter = the texture's, Wrap wrap = the texture's) -> Color",
     "Samples this texture in a frag or vert, with top-left UVs from (0, 0) to (1, 1). A `uv` past them reads the "
     "pixels at its edge (`Wrap.Clamp`), the texture again (`Wrap.Repeat`), or again the other way round each time "
     "(`Wrap.Mirror`). A Sample that leaves the filter or the wrap out reads the texture as it says itself: what "
     "its declaration says (`[Filter]`, `[Wrap]`), or what the draw that gave it says, and blended and clamped with "
     "neither. Written here, they're fixed for the draw. `Filter.Trilinear` also reads the texture's smaller "
     "images (its mipmaps) where it's drawn smaller than it is, so it doesn't shimmer: in a frag, of a `uv` made of "
     "the frag's parameters, constants and variables that are never assigned again. A vert reads the texture's own "
     "pixels."},
};

static const method_form depth_calls[] = {
    {"Sample", "Sample(float2 uv) -> float",
     "Reads a render target's depth in a frag or vert: how far the nearest thing drawn at `uv` is, from 0 at the "
     "camera's near plane to 1 at its far limit, as `pixel.depth` is, and 1 where nothing was drawn since the target "
     "was cleared. `uv` is from (0, 0) at the top left to (1, 1). It's the nearest pixel's: a depth blended between "
     "two would be neither's."},
    {"Compare", "Compare(float2 uv, float value) -> float",
     "Compares a render target's depth with `value` in a frag or vert: how much of what's stored around `uv` isn't "
     "nearer than `value`, from 0 to 1, which the GPU blends between pixels. For a shadow map, `value` is how far "
     "the pixel is from the lamp, a little less, so what the lamp sees isn't in its own shadow: 1 is lit, 0 in shadow."},
};

static const method_form cube_calls[] = {
    {"Sample", "Sample(float3 direction, Filter filter = Filter.Bilinear) -> Color",
     "Reads this cube in a frag or vert, towards `direction` from its middle, of any length: a sky's color that "
     "way, or what a reflection sees. One face goes on in the next, so there's no edge and no wrap. "
     "`Filter.Trilinear` also reads its smaller images, where a pixel covers many of its own, in a frag, of a "
     "direction made of the frag's parameters, constants and variables that are never assigned again. The cube "
     "is an asset by its name, or a field of a component or singleton the frag or vert takes."},
    {"Resize", "Resize(float size)",
     "Another side for all six faces of a cube made with `Cubemap(size)`, in pixels, and empty images."},
    {"Dispose", "Dispose()",
     "Lets a cube made with `Cubemap(size)` go, its six faces with it. Every copy of it then names no cube."},
};

static const method_form target_calls[] = {
    {"Resize", "Resize(float width, float height)", "Changes the shared image's size, in logical pixels, and discards its contents, even at the same size. It keeps its pixels per unit. Not for a target made with `RenderTarget.Window`, which follows the window."},
    {"Dispose", "Dispose()", "Releases the shared image. All target and color aliases become invalid; repeated disposal is harmless."},
};

#define BLIT_FORM "Draw.Blit(Texture source, RenderTarget destination[, Blend blend], frag shader[, values])"
#define BLIT_DOC "Runs the frag over the whole destination, supplying its Texture parameter from source. No setting changes it: not the camera, the clip, the transform, the tint nor Draw.Blend. It replaces the destination's color, or goes over it as `blend` says (`Blend.Add`), and preserves depth. Source cannot be destination.color. A struct's value after the frag is this blit's values, which the frag reads through a parameter of its type: `Draw.Blit(source, destination, Blur, BlurStep { along = float2(1, 0) })`."

// What Draw.Mesh takes beside the built-in signatures' lists of Vertex and
// Vertex3 (builtins.c): the checker takes those itself.
#define MESH_SHADER_DOC                                                                                                \
    "`vertices` can be a list of a struct of the game's too, one with a `position`: a float2 for a 2D mesh, a float3 " \
    "for a 3D one. A vert and a frag, named last, run on the GPU for each of the mesh's vertices and pixels: "         \
    "`Draw.Mesh(vertices, indices, transform, Wave, Shade)`. A struct's value after them is the draw's values, "       \
    "which they read through a parameter of its type, the same for the whole draw: "                                   \
    "`Draw.Mesh(vertices, indices, transform, Wave, Shade, Swell { height = 2 })`."

static bool is_mesh_draw(const str owner, const str name)
{
    return builtin_draw_owner(owner) && (str_eq_c(name, "Mesh") || str_eq_c(name, "Instanced"));
}

// A Pixel's one method, in a frag.
static const method_form pixel_calls[] = {
    {"Discard", "Discard()",
     "Leaves the pixel undrawn, and ends the frag there: what's behind it stays. A statement of its own, in the frag "
     "itself."},
};

// What a frag and a vert take beside the game's state: Pixel and Instance.
static const char *shader_record_doc(const decl *d)
{
    if (d && d == A.prog->pixel) {
        return "A frag's parameter: where its pixel is, on the screen and in the world, and `Discard()`, which leaves "
               "it undrawn. The GPU says it, so nothing is sent for it.";
    }
    if (d && d == A.prog->instance) {
        return "A frag's or vert's parameter: how the mesh was drawn. Its `transform` is sent to the GPU for each "
               "instance.";
    }
    return NULL;
}

// What the fields of the engine's own types are: Time's, Pixel's and Instance's.
static const char *builtin_field_doc(const decl *d, const str name)
{
    if (d && d == A.prog->time) {
        if (str_eq_c(name, "dt")) return "The length of a tick in seconds: the same every tick.";
        if (str_eq_c(name, "tick")) return "The tick the match is at, counting from 0.";
        if (str_eq_c(name, "t")) {
            return "The seconds since the match started: `tick * dt`. Views see it blended between ticks, so it moves "
                   "every frame. A float gets coarser as it grows: what has to be exact counts in `tick`.";
        }
        if (str_eq_c(name, "tickRate")) return "The ticks in a second: 1 / `dt` to the nearest whole number.";
        if (str_eq_c(name, "seed")) return TIME_SEED_DOC;
    }
    if (d && d == A.prog->random && str_eq_c(name, "state")) {
        return "All there is to the generator: copy it to keep where a sequence is, and set it to go on from there. 0 "
               "is one nothing seeded yet.";
    }
    if (d && d == A.prog->pixel) {
        if (str_eq_c(name, "position")) return "Where the pixel is on the screen: its pixels, from the top left.";
        if (str_eq_c(name, "depth")) {
            return "How deep the pixel is, as the depth test has it: from 0, the nearest the camera draws, to 1, the farthest.";
        }
        if (str_eq_c(name, "world")) return "Where the pixel is in the world.";
    }
    if (d && d == A.prog->instance && str_eq_c(name, "transform")) {
        return "The transform `Draw.Mesh` was given, which places the mesh in the world.";
    }
    if (d && d == A.prog->shape) {
        if (str_eq_c(name, "center")) return "Its middle, in the units the 2D draws are in: the world's under a camera, pixels on the screen.";
        if (str_eq_c(name, "size")) return "Its width and height. A shape with none draws nothing.";
        if (str_eq_c(name, "rotation")) {
            return "How far it's turned about its center, in radians, from +x towards +y: counterclockwise under a "
                   "camera, clockwise on the screen.";
        }
        if (str_eq_c(name, "radius")) {
            return "Its corners' radius, up to half its smaller side: that much makes a pill, or a circle of a square.";
        }
        if (str_eq_c(name, "ellipse")) return "Whether it's the ellipse that fits `size`, rather than a box.";
        if (str_eq_c(name, "color")) return "Its fill: at its top, when `color2` is another.";
        if (str_eq_c(name, "color2")) {
            return "Its fill at its bottom, for a fill that goes from `color` at its top to this one, as it is before "
                   "it's turned. `Color.none`, its default, is one color all over.";
        }
        if (str_eq_c(name, "outline")) return "How wide its outline is, inside its edge. 0 is none.";
        if (str_eq_c(name, "outlineColor")) return "Its outline's color.";
        if (str_eq_c(name, "soft")) {
            return "How far its edge fades. Above 0, outwards: its color goes on past its edge, as a glow or a shadow. "
                   "Under 0, inwards from its edge, leaving its middle clear. 0 is a sharp edge, smooth over a pixel.";
        }
    }
    if (d && d == A.prog->gui_theme) {
        if (str_eq_c(name, "text")) return "Text, and a slider's thumb.";
        if (str_eq_c(name, "panel")) return "An area's background.";
        if (str_eq_c(name, "control")) return "A button, a toggle's box, a slider's rail and a color field's frame.";
        if (str_eq_c(name, "hot")) return "A button, a toggle's box or a slider's rail with the pointer on it.";
        if (str_eq_c(name, "pressed")) return "A button while it's held down.";
        if (str_eq_c(name, "field")) return "A field to type into.";
        if (str_eq_c(name, "fieldHot")) return "A field with the pointer on it.";
        if (str_eq_c(name, "accent")) return "The outline of the widget that has the focus, a toggle's check and a slider's filled part.";
        if (str_eq_c(name, "radius")) {
            return "How round the widgets' corners are, in pixels, up to half a part's smaller side. 0, its default, is "
                   "square corners.";
        }
    }
    if (d && d == A.prog->gui_control) {
        if (str_eq_c(name, "hovered")) return "Whether the pointer is on it, with nothing of the GUI's over it.";
        if (str_eq_c(name, "pressed")) {
            return "Whether the pointer holds it: from a press on it until it lets go, wherever it goes meanwhile. A "
                   "button looks down while it's `pressed` and `hovered`; a handle follows the pointer while it's `pressed`.";
        }
        if (str_eq_c(name, "focused")) {
            return "Whether the keyboard and the gamepad are on it: Tab, the arrows, the d-pad and the left stick move "
                   "the focus between widgets.";
        }
        if (str_eq_c(name, "clicked")) {
            return "Whether it was clicked this frame: a press on it let go on it, or Enter, Space or the gamepad's "
                   "south button went down while it had the focus.";
        }
    }
    return NULL;
}

// An entity's methods, in match code for an Entity and in local code for a LocalEntity.
static const method_form entity_calls[] = {
    {"Add", "Add(components...)", "Adds components, or replaces their values. Applied at the end of the tick."},
    {"Remove", "Remove(components...)", "Removes components. Applied at the end of the tick."},
    {"Destroy", "Destroy()", "Destroys the entity at the end of the tick."},
    {"Send", "Send(event)",
     "Sends an event to the entity: handlers that take its components run for it, at the end of the tick."},
    {"Snap", "Snap()",
     "It jumped, like a respawn or a portal: views draw it as it is this tick, not blended from where it was."},
};

// The World's methods, in code that takes `World world`: T is the component in <>.
static const method_form world_calls[] = {
    {"Get", "Get<T>(Entity entity) -> T",
     "A copy of the entity's component as it is right now, or the component's zero value when the entity has none. It "
     "counts as reading every entity's, so systems that write the component run before or after."},
    {"Has", "Has<T>(Entity entity) -> bool",
     "Whether the entity has the component. It counts as reading every entity's."},
    {"TryGet", "TryGet<T>(Entity entity, out T value) -> bool",
     "Whether the entity has the component, and through `out`, a copy of it, or the component's default when the "
     "entity has none: `if (world.TryGet<Health>(target, out var health)) ...`. It counts as reading every entity's."},
    {"Set", "Set<T>(Entity entity, T value)",
     "Puts the component back on the entity, right away; nothing happens when the entity has none. It needs "
     "`mut World`, counts as writing every entity's, and keeps a system on one thread."},
};

static const method_form singleton_calls[] = {
    {"Snap", "Snap()", "It jumped, like a camera cut: views draw it as it is this tick, not blended from where it was."},
};

// The built-in methods of what has the type `t`, or NULL: a list's, a grid's,
// an entity's, a singleton's or a frag's Pixel's. Text's are in builtins.c,
// with Math's.
static const method_form *methods_of(const type t, size_t *count)
{
    const method_form *forms = NULL;
    *count = 0;
    switch (t.kind) {
    case TY_LIST: forms = list_calls; *count = sizeof list_calls / sizeof list_calls[0]; break;
    case TY_MAP: forms = map_calls; *count = sizeof map_calls / sizeof map_calls[0]; break;
    case TY_SET: forms = set_calls; *count = sizeof set_calls / sizeof set_calls[0]; break;
    case TY_GRID: forms = grid_calls; *count = sizeof grid_calls / sizeof grid_calls[0]; break;
    case TY_TEXTURE: forms = texture_calls; *count = sizeof texture_calls / sizeof texture_calls[0]; break;
    case TY_DEPTH_TEXTURE: forms = depth_calls; *count = sizeof depth_calls / sizeof depth_calls[0]; break;
    case TY_CUBEMAP: forms = cube_calls; *count = sizeof cube_calls / sizeof cube_calls[0]; break;
    case TY_RENDER_TARGET: forms = target_calls; *count = sizeof target_calls / sizeof target_calls[0]; break;
    case TY_ENTITY: case TY_LOCAL_ENTITY: forms = entity_calls; *count = sizeof entity_calls / sizeof entity_calls[0]; break;
    case TY_SINGLETON: forms = singleton_calls; *count = sizeof singleton_calls / sizeof singleton_calls[0]; break;
    case TY_WORLD: forms = world_calls; *count = sizeof world_calls / sizeof world_calls[0]; break;
    case TY_RECORD:
        if (t.decl != A.prog->pixel) break;
        forms = pixel_calls;
        *count = sizeof pixel_calls / sizeof pixel_calls[0];
        break;
    default: break;
    }
    return forms;
}

// "World.Get<Health>(Entity entity) -> Health" for the World's method `form`
// of `component`, with a LocalEntity for a local one; "World.Get<T>(...)"
// where the component isn't known.
static void format_world_method(const method_form *form, const decl *component, sb *out)
{
    sb_put(out, "World.");
    for (const char *p = form->form; *p; p++) {
        const bool alone = (p == form->form || !isalnum((unsigned char)p[-1])) && !isalnum((unsigned char)p[1]);
        if (*p == 'T' && alone && component) {
            sb_printf(out, STR_FMT, STR_ARG(component->name));
        } else if (component && component->is_local && strncmp(p, "Entity ", 7) == 0) {
            sb_put(out, "LocalEntity");
            p += 5;
        } else {
            sb_putn(out, p, 1);
        }
    }
}

// "List<int>.Add(int item)" for the method `form` called on a value of type
// `t`; "items.Add(T item)" where its type isn't known.
static void format_method(const method_form *form, const type t, sb *out)
{
    if (t.kind == TY_WORLD) {
        format_world_method(form, NULL, out);
        return;
    }
    const bool generic = (t.kind == TY_LIST || t.kind == TY_MAP || t.kind == TY_SET || t.kind == TY_GRID) && t.decl;
    const char *element = generic ? type_name(t.decl->fields.items[0].type) : "T";
    const char *value = generic && t.kind == TY_MAP ? type_name(t.decl->fields.items[1].type) : "V";
    sb_put(out, generic || t.kind == TY_RECORD || t.kind == TY_DEPTH_TEXTURE || t.kind == TY_CUBEMAP ? type_name(t) // Pixel.Discard()
                : t.kind == TY_LIST ? "items"
                : t.kind == TY_MAP || t.kind == TY_SET ? "keys"
                : t.kind == TY_GRID ? "cells"
                : t.kind == TY_SINGLETON ? "singleton"
                : "entity");
    sb_put(out, ".");
    for (const char *p = form->form; *p; p++) {
        const bool alone = (p == form->form || !isalnum((unsigned char)p[-1])) && !isalnum((unsigned char)p[1]);
        if ((*p == 'T' || *p == 'K') && alone) sb_put(out, element);
        else if (*p == 'V' && alone) sb_put(out, value);
        else sb_putn(out, p, 1);
    }
}

// The form named `name` in `forms`, or NULL.
static const method_form *find_form(const method_form *forms, const size_t count, const str name)
{
    for (size_t i = 0; i < count; i++) {
        if (str_eq_c(name, forms[i].name)) return &forms[i];
    }
    return NULL;
}

// What `default` is for type `t`, in words.
static void describe_default(const type t, sb *out)
{
    switch (t.kind) {
    case TY_BOOL: sb_put(out, "`false`"); break;
    case TY_BYTE: case TY_INT: case TY_LONG: case TY_FLOAT: sb_put(out, "`0`"); break;
    case TY_ENTITY: case TY_LOCAL_ENTITY: sb_put(out, "The null entity"); break;
    case TY_PLAYER: sb_put(out, "No player"); break;
    case TY_STRING: sb_put(out, "Empty text"); break;
    case TY_LIST: sb_put(out, "An empty list"); break;
    case TY_MAP: sb_put(out, "An empty map"); break;
    case TY_SET: sb_put(out, "An empty set"); break;
    case TY_GRID: sb_put(out, "An empty grid, open every way"); break;
    case TY_TEXTURE: case TY_RENDER_TARGET: sb_put(out, "An invalid image reference"); break;
    case TY_DEPTH_TEXTURE: sb_put(out, "No target's depth: it reads as the far limit, 1"); break;
    case TY_CUBEMAP: sb_put(out, "No cube: it reads as black"); break;
    case TY_ENUM: {
        const enum_member *zero = NULL;
        for (int i = 0; i < t.decl->members.count && !zero; i++) {
            if (t.decl->members.items[i].number == 0) zero = &t.decl->members.items[i];
        }
        if (zero) sb_printf(out, "`" STR_FMT "." STR_FMT "`, the member that's 0", STR_ARG(t.decl->name), STR_ARG(zero->name));
        else sb_put(out, "0, which isn't one of its members");
        break;
    }
    case TY_COMPONENT: case TY_SINGLETON: case TY_INPUT: case TY_STRUCT: case TY_EVENT:
        sb_printf(out, "`" STR_FMT " { }`: each field's default, and zero where it has none", STR_ARG(t.decl->name));
        break;
    case TY_OPTIONAL: sb_put(out, "Nothing"); break;
    default: sb_put(out, "All zeros"); break;
    }
    sb_printf(out, ": the default value of `%s`, the type where it goes.", type_name(t));
}

static const decl *decl_of_param(const param *p);

// Whether the checker worked out what a frag or vert reads, which it does
// once the game has no errors: a game with one plans no shaders.
static bool shader_planned(const decl *d)
{
    return d->gpu_state == 2;
}

// The fields of its parameter `p` a frag or vert reads, each in backquotes
// with `prefix` before it, onto the list `out`: the match's, then the local
// ones of a match component or singleton, which this machine keeps beside
// them. Grids aren't read: they're sampled (put_samples). Of a frag's vertex,
// `blended`, ints aren't either: they don't blend across a triangle, so its
// corners pass none on.
static void put_reads(const param *p, const char *prefix, const bool blended, sb *out)
{
    const decl *t = p->type.decl;
    for (int k = 0; t && k < t->fields.count; k++) {
        const field *f = &t->fields.items[k];
        if (f->hidden || f->type.kind == TY_GRID || !(p->whole || (p->reads && p->reads[k]))) continue;
        if (blended && type_is_int_based(f->type)) continue;
        sb_printf(out, "%s`%s" STR_FMT "`", out->len ? ", " : "", prefix, STR_ARG(f->name));
    }
    const decl *part = t && p->local_reads ? t->local_part : NULL;
    for (int k = 0; part && k < part->fields.count; k++) {
        const field *f = &part->fields.items[k];
        if (f->type.kind == TY_GRID || !p->local_reads[k]) continue;
        sb_printf(out, "%s`%s" STR_FMT "`", out->len ? ", " : "", prefix, STR_ARG(f->name));
    }
}

// The grids of its parameter `index` a frag or vert samples, likewise.
static void put_samples(const decl *d, const int index, const char *prefix, sb *out)
{
    const decl *t = d->params.items[index].type.decl;
    for (int i = 0; i < d->samples.count; i++) {
        const shader_sample *s = &d->samples.items[i];
        if (s->param != index) continue;
        const field *f = &(s->local ? t->local_part : t)->fields.items[s->field];
        static const char *const wraps[] = {"", " (`Wrap.Repeat`)", " (`Wrap.Mirror`)"}; // By its number
        sb_printf(out, "%s`%s" STR_FMT "`%s%s", out->len ? ", " : "", prefix, STR_ARG(f->name),
                  s->filter == 0 ? " (`Filter.Point`)" : s->filter == 2 ? " (`Filter.Trilinear`)" : "", wraps[s->wrap >= 0 && s->wrap < 3 ? s->wrap : 0]);
    }
}

// "Box." for a parameter of type Box: what its fields are listed with.
static const char *type_prefix(const param *p)
{
    sb prefix = {0};
    sb_printf(&prefix, STR_FMT ".", STR_ARG(p->type.decl->name));
    return prefix.data;
}

// Whether a vertex struct is a 3D mesh's: its position is a float3.
static bool vertex_is_3d(const decl *vertex)
{
    for (int i = 0; vertex && i < vertex->fields.count; i++) {
        const field *f = &vertex->fields.items[i];
        if (str_eq_c(f->name, "position")) return f->type.kind == TY_FLOAT3;
    }
    return false;
}

// Whether a struct is one a vert of the game writes for its frag, `out
// FromLamp lamp`: a frag's parameter of that type is what the vert it's drawn
// with hands it, and one of any other type an instance's.
static bool vert_writes(const decl *type)
{
    for (int i = 0; i < A.prog->decls.count; i++) {
        const decl *d = A.prog->decls.items[i];
        for (int k = 1; is_shader(d) && !d->is_frag && k < d->params.count; k++) {
            const param *p = &d->params.items[k];
            if (p->is_out && p->type.kind == TY_STRUCT && p->type.decl == type) return true;
        }
    }
    return false;
}

// A frag's struct that the vert it's drawn with writes for it.
static bool handed_param(const decl *d, const param *p)
{
    return d->is_frag && p->type.kind == TY_STRUCT && !p->is_out && vert_writes(p->type.decl);
}

// Whether a struct is one a draw of the game gives after its vert and frag,
// `Draw.Blit(source, destination, Blur, BlurStep { ... })`: a frag's or vert's
// parameter of that type is the draw's values, sent once for the draw.
static bool draw_gives(const decl *type)
{
    for (int i = 0; i < A.prog->mesh_draws.count; i++) {
        if (A.prog->mesh_draws.items[i].call->values_type == type) return true;
    }
    return false;
}

// A frag's or vert's struct that the draws it's drawn with give as their values.
static bool values_param(const decl *d, const param *p)
{
    return p->type.kind == TY_STRUCT && !p->is_out && !handed_param(d, p) && draw_gives(p->type.decl);
}

// A frag or vert: when it runs, and what it reads, grouped as the engine
// sends it to the GPU: components' fields for each instance drawn,
// singletons' for each draw, grids as textures, and of a frag's vertex and of
// what its vert wrote for it, the fields the mesh's corners pass on to its
// pixels.
static void describe_shader(const decl *d, sb *out)
{
    const bool three_d = d->params.count && vertex_is_3d(d->params.items[0].type.decl);
    bool instanced = false;
    bool hands = false;
    for (int i = 1; i < d->params.count; i++) {
        const param *p = &d->params.items[i];
        instanced |= p->type.kind == TY_STRUCT && !p->is_out && !handed_param(d, p) && !values_param(d, p);
        hands |= p->type.kind == TY_STRUCT && p->is_out;
    }
    sb_printf(out, d->is_frag ? "\n\nRuns on the GPU, for each pixel of a mesh a view draws through it: `Draw.%s(..., "
                                STR_FMT ")`, a %s one. What it writes to its `out Color` is the pixel."
                              : "\n\nRuns on the GPU, for each vertex of a mesh a view draws through it: `Draw.%s(..., "
                                STR_FMT ")`, a %s one. It changes the vertex in place, before its triangles are drawn.",
              instanced ? "Instanced" : "Mesh", STR_ARG(d->name), three_d ? "3D" : "2D");
    if (hands) sb_put(out, " What it writes to a struct it takes as `out`, the frag it's drawn with reads, blended across the triangle.");
    if (!shader_planned(d)) {
        sb_put(out, "\n\nWhat it reads is worked out once the game has no errors.");
        return;
    }
    sb instance = {0};
    sb draw = {0};
    sb textures = {0};
    sb vertex = {0};
    sb handed = {0};
    for (int i = 0; i < d->params.count; i++) {
        const param *p = &d->params.items[i];
        if (i == 0) { // A vert has its whole vertex
            if (d->is_frag) put_reads(p, "", true, &vertex);
            continue;
        }
        if (p->type.kind == TY_STRUCT && p->is_out) continue; // What a vert writes for its frag: nothing sends it
        if (handed_param(d, p)) { // ...and what the frag reads of it comes from the mesh's corners
            put_reads(p, type_prefix(p), true, &handed);
            continue;
        }
        const bool component = p->type.kind == TY_COMPONENT;
        const bool transform = p->type.kind == TY_RECORD && p->type.decl == A.prog->instance;
        const bool values = values_param(d, p); // The draw's own, once for the draw, as a singleton's
        const bool data = p->type.kind == TY_STRUCT && !values;
        if (!component && !transform && !data && !values && p->type.kind != TY_SINGLETON) continue; // Its pixel, which nothing sends
        put_reads(p, type_prefix(p), false, component || transform || data ? &instance : &draw);
        put_samples(d, i, type_prefix(p), &textures);
    }
    if (!instance.len && !draw.len && !textures.len && !vertex.len && !handed.len) {
        sb_put(out, "\n\nIt reads nothing the engine sends.");
        return;
    }
    sb_put(out, "\n\nOnly what it reads is sent to the GPU:");
    if (instance.len) sb_printf(out, "\n- for each instance: %s", instance.data);
    if (draw.len) sb_printf(out, "\n- for each draw: %s", draw.data);
    if (textures.len) sb_printf(out, "\n- as textures: %s", textures.data);
    if (vertex.len) sb_printf(out, "\n- of its vertex, blended across the triangle: %s", vertex.data);
    if (handed.len) sb_printf(out, "\n- of what its vert wrote for it, blended across the triangle: %s", handed.data);
}

// A frag's or vert's parameter: what it is to the GPU, and for a component
// or singleton, how often it's sent and which of its fields this code reads.
static void describe_shader_param(const decl *d, const param *p, sb *out)
{
    const decl *t = p->type.decl;
    const int index = (int)(p - d->params.items);
    const bool planned = shader_planned(d);
    sb fields = {0};
    sb grids = {0};
    if (index == 0 && d->is_frag) {
        sb_put(out, "\n\nThe vertex, blended across the triangle from its three corners. A frag reads it.");
        if (planned) put_reads(p, "", true, &fields);
        if (fields.len) sb_printf(out, " Fields this code reads, which the corners pass on: %s.", fields.data);
        return;
    }
    if (index == 0) {
        sb_put(out, "\n\nThe vertex the vert runs for, which it changes in place: what it leaves in it is what the "
                    "triangle is drawn with, and what a frag reads of it, blended.");
        return;
    }
    if (p->mode == PARAM_MUT && p->type.kind == TY_COLOR && d->is_frag && p->output > 0) {
        static const char *const which[] = {"first", "second", "third", "fourth"};
        const int at = p->output < 4 ? p->output : 3;
        sb_printf(out, "\n\nThe frag's %s output: a color it writes beside the pixel's. It goes to the %s color image of "
                       "a target made to hold as many, `RenderTarget(width, height, Format.Rgba8, Format.Rgba16f)`, which "
                       "`target.colors[%d]` reads; in the window, or in a target with fewer images, it goes nowhere. It "
                       "starts with no value: the frag writes it on every path that ends, before it reads it. It's "
                       "blended over what's in its image by its alpha, as the pixel's color is: `Color(x, y, z)` has "
                       "an alpha of 1, and replaces it.", which[at], which[at], at);
        return;
    }
    if (p->mode == PARAM_MUT && p->type.kind == TY_COLOR) {
        sb_put(out, "\n\nThe pixel's color, which the frag writes. It starts with no value: the frag writes it on every "
                    "path that ends, before it reads it, and a path that ends in `pixel.Discard()` draws no pixel. It "
                    "isn't what's on the screen already, which a GPU doesn't let a frag read. What the frag wrote is "
                    "blended over what's behind it by its alpha.");
        return;
    }
    if (p->is_out && p->type.kind == TY_STRUCT) {
        sb_printf(out, "\n\nWhat the vert writes for the frag it's drawn with. It starts with no value: the vert writes each "
                       "of its fields, on every path. A frag that takes a `" STR_FMT "` reads it blended across the "
                       "triangle from its three corners, and only the fields it reads go from the corners to the pixels.",
                  STR_ARG(t->name));
        return;
    }
    if (handed_param(d, p)) {
        sb_put(out, "\n\nWhat the vert this frag is drawn with wrote for it, an `out` parameter of the vert's, blended "
                    "across the triangle from its three corners. A draw whose vert writes none is an error at the draw.");
        if (planned) put_reads(p, "", true, &fields);
        if (fields.len) sb_printf(out, " Fields this code reads, which the corners pass on: %s.", fields.data);
        return;
    }
    if (shader_record_doc(t)) {
        sb_printf(out, "\n\n%s", shader_record_doc(t));
        return;
    }
    if (p->type.kind != TY_COMPONENT && p->type.kind != TY_SINGLETON && p->type.kind != TY_STRUCT) {
        sb_put(out, "\n\nParameter, read-only.");
        return;
    }
    sb_put(out, values_param(d, p)        ? "\n\nThe values of the draw: the struct a `Draw.Mesh`, `Draw.Instanced` or `Draw.Blit` "
                                            "gives after its vert and frag, sent to the GPU once for the draw."
              : p->type.kind == TY_STRUCT ? "\n\nAn element of the draw's instance list, sent to the GPU for each instance drawn."
              : p->type.kind == TY_COMPONENT ? "\n\nA component of the entity the view that draws the mesh runs for, sent "
                                               "to the GPU for each instance drawn."
                                             : "\n\nA singleton, sent to the GPU once for each draw.");
    if (!planned) {
        sb_put(out, " Which of its fields this code reads is worked out once the game has no errors.");
        return;
    }
    put_reads(p, "", false, &fields);
    put_samples(d, index, "", &grids);
    if (fields.len) sb_printf(out, " Fields this code reads, the only ones sent: %s.", fields.data);
    if (grids.len) sb_printf(out, " Grids it samples, sent as textures: %s.", grids.data);
    if (!fields.len && !grids.len) sb_put(out, " This code reads none of its fields, so nothing of it is sent.");
}

// Global component access through its World or query candidate sets.
static void describe_world(const decl *d, sb *out)
{
    sb reads = {0};
    sb writes = {0};
    const int read_count = world_use_names(d, false, "`", d, &reads);
    const int write_count = world_use_names(d, true, "`", d, &writes);
    if (read_count + write_count == 0) return;
    bool world = false;
    bool query = false;
    for (int i = 0; i < d->params.count; i++) {
        world |= d->params.items[i].type.kind == TY_WORLD;
        query |= d->params.items[i].mode == PARAM_QUERY;
    }
    sb_put(out, world && query ? "\n\nThrough its `World` and queries, it "
                : query ? "\n\nThrough its queries, it " : "\n\nThrough its `World`, it ");
    if (read_count) sb_printf(out, "reads any entity's %s", reads.data);
    if (read_count && write_count) sb_put(out, " and ");
    if (write_count) sb_printf(out, "writes any entity's %s", writes.data);
    sb_put(out, ".");
}

static const char *name_for(const decl *d);

// How many handlers `event` has: its local handlers that hear the match's
// (decl.listens), or the handlers its own world runs. From the program's
// handlers rather than the event's lists, which the checker only fills once
// the game has no errors.
static int handler_count(const decl *event, const bool listens)
{
    int count = 0;
    for (int i = 0; i < A.prog->handlers.count; i++) {
        const decl *h = A.prog->handlers.items[i];
        count += h->event == event && h->listens == listens;
    }
    return count;
}

// A match event's local handlers, after what its hover says of the match's:
// who hears it on each machine, and which of them wait for the server.
static void describe_listeners(const decl *event, sb *out)
{
    int count = 0;
    for (int i = 0; i < A.prog->handlers.count; i++) {
        const decl *h = A.prog->handlers.items[i];
        if (h->event != event || !h->listens) continue;
        sb_put(out, count++ ? ", " : "\n\nLocal handlers hear it on each machine, at the start of a frame: ");
        put_decl_name(out, h, "`", event);
        if (h->listens_verified) sb_put(out, " (once the server confirmed it)");
    }
    if (count) sb_put(out, ".");
}

// `verified` where it is: before a view's component or singleton, after its
// `with` or `without`, or before a local handler's event.
static void describe_verified(const param *p, const decl *of, sb *out)
{
    const bool heard = p->mode == PARAM_EVENT && of && of->is_handler && of->is_local;
    const bool seen = p->mode != PARAM_EVENT && of && of->is_view;
    if (!p->verified || (!heard && !seen)) { // Somewhere it can't be, which the error there explains
        code_block(out, "verified");
        sb_put(out, "\n\nFrom the world the server has confirmed. It goes before a component or singleton of the match "
                    "in a view's parameters, after a view's `with` or `without`, and before a local handler's event.");
        return;
    }
    sb code = {0};
    if (heard) sb_put(&code, "event(");
    format_param(p, &code);
    if (heard) sb_put(&code, ")");
    code_block(out, code.data);
    const str type = last_part(p->type_name);
    if (heard) {
        sb_printf(out, "\n\nThe handler hears each `" STR_FMT "` once, when the server confirmed the tick that sent it: "
                       "never one this machine only predicted. What it takes of the match is as that tick left it.",
                  STR_ARG(type));
    } else if (p->query_source && p->mode == PARAM_WITH) { // In a view's query, the same of the entities it goes through
        sb_printf(out, "\n\nFilter: the query goes through the entities that have `" STR_FMT "` in the world the server "
                       "has confirmed, whatever this machine predicts.", STR_ARG(type));
    } else if (p->query_source && p->mode == PARAM_WITHOUT) {
        sb_printf(out, "\n\nFilter: the query goes through the entities that are in the world the server has confirmed "
                       "and don't have `" STR_FMT "` there, whatever this machine predicts.", STR_ARG(type));
    } else if (p->query_source) {
        sb_printf(out, "\n\nThe entity's `" STR_FMT "` as the last tick the server confirmed has it: not as this machine "
                       "predicts it, and not blended between ticks. %s", STR_ARG(type),
                  p->query_source->query_verified_entities
                      ? "Everything in the query is `verified`, so it goes through the confirmed world's entities."
                      : "The query goes through the entities the view draws that are the same entity in the confirmed "
                        "world too.");
    } else if (p->mode == PARAM_WITH) {
        sb_printf(out, "\n\nFilter: the view runs for the entities that have `" STR_FMT "` in the world the server has "
                       "confirmed, whatever this machine predicts.", STR_ARG(type));
    } else if (p->mode == PARAM_WITHOUT) {
        sb_printf(out, "\n\nFilter: the view runs for the entities that are in the world the server has confirmed and "
                       "don't have `" STR_FMT "` there, whatever this machine predicts.", STR_ARG(type));
    } else if (p->type.kind == TY_SINGLETON) {
        sb_printf(out, "\n\n`" STR_FMT "` as the last tick the server confirmed has it: not as this machine predicts it, "
                       "and not blended between ticks.", STR_ARG(type));
    } else {
        sb_printf(out, "\n\nThe entity's `" STR_FMT "` as the last tick the server confirmed has it: not as this machine "
                       "predicts it, and not blended between ticks. %s", STR_ARG(type),
                  of->verified_entities ? "All the view's components are `verified`, so it runs for the confirmed world's "
                                          "entities."
                                        : "The view runs for the entities it draws that are the same entity in the "
                                          "confirmed world too.");
    }
}

// What Card(number) gives, for its hover and its signature.
static void describe_enum_conversion(const decl *d, sb *out)
{
    const char *name = name_for(d);
    sb_printf(out, "`%s(number)` is the `%s` with that number, as `int(value)` is a value's number. A number none of its "
                   "members has stays as it is", name, name);
    if (d->width == 1 || d->width == 2) {
        sb_printf(out, ", cut to the %s the enum is stored in: its low %d bits, as `byte(x)` keeps an int's low 8",
                  d->width == 1 ? "byte" : "ushort", d->width * 8);
    }
    sb_put(out, ".");
}

// What an `out` parameter of `of` is: the caller's variable, which the call
// makes. An extern function's is C's to make: a list through its writer, and
// anything else through a pointer.
static void describe_out_param(const param *p, const decl *of, sb *out)
{
    if (is_shader(of)) { // What a frag or a vert writes: nothing calls it with an argument
        describe_shader_param(of, p, out);
        return;
    }
    if (of && of->is_extern && p->type.kind == TY_LIST) {
        sb_put(out, "\n\nParameter, the caller's list, which C makes: it gets a `tide_list_out`, asks it for room, fills it, "
                    "and says how many elements there are (`tide/list.h`). What the list held isn't passed.");
    } else if (of && of->is_extern) {
        sb_put(out, "\n\nParameter, the caller's variable, which C makes: it gets a pointer, and writes the value through "
                    "it. What the variable held isn't passed.");
    } else {
        sb_put(out, "\n\nParameter, the caller's variable, which the call makes: it starts unassigned here, so the code "
                    "assigns it before it reads it, and on every path that returns. What the variable held isn't passed.");
    }
    sb_printf(out, " A call says `out` before its argument: `out var " STR_FMT "` declares the variable there, `out` before "
                   "one the code has assigns it, and `out _` leaves it out.", STR_ARG(p->name));
}

// Markdown for a hover over `o`.
static void describe_replacing(const occurrence *o, sb *out);

static void describe(const occurrence *o, sb *out)
{
    sb code = {0};
    switch (o->kind) {
    case OCC_DEFAULT: // `default`, or `null`
        sb_printf(&code, STR_FMT ": %s", STR_ARG(o->name), type_name(o->type));
        code_block(out, code.data);
        sb_put(out, "\n\n");
        describe_default(o->type, out);
        break;
    case OCC_TYPE:
        if (o->backing) {
            code_block(out, str_to_cstr(o->name));
            sb_put(out, "\n\nWhat the enum is stored as: ");
            sb_put(out, str_eq_c(o->name, "byte")     ? "one byte, so its members go from 0 to 255."
                      : str_eq_c(o->name, "ushort") ? "two bytes, so its members go from 0 to 65535."
                                                    : "four bytes, as it is without one.");
        } else if (o->decl) {
            format_data_decl(o->decl, &code);
            code_block(out, code.data);
            if (o->decl->kind == DECL_EVENT && o->decl->builtin) sb_printf(out, "\n\n%s", builtin_event_doc(A.prog, o->decl));
            else if (shader_record_doc(o->decl)) sb_printf(out, "\n\n%s", shader_record_doc(o->decl));
            else if (o->decl == A.prog->shape) {
                sb_put(out, "\n\nWhat `Draw.Shape` draws: a box with round corners, or an ellipse, turned, with a fill "
                            "of one color or from one to another, an outline, and an edge that's sharp or fades. "
                            "`Draw.RoundRect`, `Ellipse` and the thick outlines and lines are short for it.");
            } else if (o->decl == A.prog->gui_theme) {
                sb_put(out, "\n\nHow the GUI's own widgets look: their colors, and how round their corners are. Its "
                            "defaults are the look they have without one, so a theme names what it changes: "
                            "`GUITheme { control = panel, radius = 10 }`. `GUI.SetTheme(theme)` sets it for the widgets "
                            "that follow, and `GUI.Theme(theme) { ... }` for those in its block.");
            } else if (o->decl == A.prog->gui_control) {
                sb_put(out, "\n\nWhat `GUI.Control(rect)` says of a widget the view draws itself: whether it's hovered, "
                            "held, focused and clicked.");
            } else if (o->decl == A.prog->random) sb_put(out, "\n\n" RANDOM_DOC);
            else if (o->decl == A.prog->font) {
                sb_put(out, "\n\nThe weight `Draw.Text` draws in and `Draw.MeasureText` measures in, after their "
                            "other arguments: `Font.Regular` (what they use without one) or `Font.Bold`, the font's "
                            "own bold. A game only carries the bold weight's file when its code says `Font.Bold`.");
            } else if (o->decl == A.prog->anchor) {
                sb_put(out, "\n\nA place of a box, as Unity's `TextAnchor`: where `GUILayout.Area` puts an area on "
                            "the screen, and which place of a text's box is at the position `Draw.Text` takes "
                            "(`Anchor.MiddleCenter` centers it there). Upper is towards the top of the screen.");
            }
            else if (o->decl->builtin) sb_put(out, "\n\nBuilt into the engine.");
            else if (o->decl->is_scene) {
                sb_put(out, o->decl->is_local ? "\n\nA local scene: loaded with `Scene.Load` from views, on this machine only."
                                              : "\n\nA scene: its entities load and unload together. Load it with `Scene.Load`; "
                                                "systems that take it run once per loaded one.");
            } else if (o->decl->kind == DECL_EVENT && o->decl->is_command) {
                sb_put(out, "\n\nSent with `Send` from views or local handlers to one match handler. Its payload is untrusted; the connection supplies the sender's PlayerID.");
            } else if (o->decl->kind == DECL_EVENT && o->decl->is_local) {
                sb_put(out, "\n\nLocal: sent with `Send` from views, and handled by `local event(...)` handlers at the end of the frame.");
            } else if (o->decl->kind == DECL_EVENT) {
                // One that only local handlers take is the match's to send, and nothing of the match's to handle
                const bool heard_only = handler_count(o->decl, true) > 0 && handler_count(o->decl, false) == 0;
                sb_put(out, heard_only ? "\n\nSent with `Send`, and only for local code: no handler in the match takes it, "
                                         "so it costs the match nothing."
                                       : "\n\nSent with `Send`, and handled by `event(...)` handlers at the end of the tick.");
            } else if (o->decl->is_local) {
                sb_put(out, "\n\nLocal: this machine's own, never sent, rolled back or hashed. Views change it; the match can't see it.");
            }
            if (o->decl->kind == DECL_EVENT && !o->decl->is_local && !o->decl->is_command) describe_listeners(o->decl, out);
            if (o->decl->kind == DECL_ENUM && o->call == CALL_CONSTRUCT) { // Card(2)
                sb_put(out, "\n\n");
                describe_enum_conversion(o->decl, out);
            }
        } else {
            code_block(out, type_name(o->type));
            const char *doc = builtin_type_doc(o->type.kind);
            if (doc) sb_printf(out, "\n\n%s", doc);
        }
        break;
    case OCC_SYSTEM:
        if (o->decl->extern_system && o->decl->c_name && !str_eq_c(o->decl->name, o->decl->c_name)) {
            sb_printf(&code, "[NativeName(\"%s\")]\n", o->decl->c_name);
        }
        format_header(o->decl, &code);
        code_block(out, code.data);
        if (is_shader(o->decl)) { // Where it's declared, and where a Draw.Mesh names it
            describe_shader(o->decl, out);
            break;
        }
        if (o->decl->extern_system) { // Its code is C's: what C is called with, and how the tick takes it
            sb_printf(out, "\n\nA system written in C: the C function `%s`, which the game's C files or libraries define. ",
                      o->decl->c_name ? o->decl->c_name : "?");
            if (o->decl->per_entity) {
                sb_put(out, "It's called once per tick for each run of rows of the entities that match, a chunk of them or a "
                            "piece of one, and reads and writes their components as arrays (`tide/rows.h`), a field a lane. ");
            } else {
                sb_put(out, "It's called once per tick, with no rows. ");
            }
            sb_put(out, "The singletons it takes follow the rows, each a pointer. The tick takes it to read and write what "
                        "its declaration says.");
            describe_order(o->decl, out);
            break;
        }
        if (o->decl->is_handler && o->decl->event) {
            if (o->decl->is_command) sb_put(out, "\n\nHandles an untrusted player submission. The connection supplies its sender; check permissions and game rules before changing match state.");
            const str event = o->decl->event->name;
            const char *when = o->decl->is_local ? "frame" : "tick";
            const char *to = o->decl->per_entity ? " to an entity that matches its parameters" : "";
            if (o->decl->listens && o->decl->listens_verified) {
                sb_printf(out, "\n\nA local handler of the match's `" STR_FMT "`: it runs on this machine at the start of a "
                               "frame, before the views, for each `" STR_FMT "` sent%s in the ticks the server confirmed "
                               "since the last frame. It hears each once, when the server confirmed its tick, with what it "
                               "takes of the match as that tick left it.", STR_ARG(event), STR_ARG(event), to);
            } else if (o->decl->listens) {
                sb_printf(out, "\n\nA local handler of the match's `" STR_FMT "`: it runs on this machine at the start of a "
                               "frame, before the views, for each `" STR_FMT "` the ticks since the last frame sent%s. It "
                               "hears each once, whatever ticks are run again, with what it takes of the match as the "
                               "event's turn left it.", STR_ARG(event), STR_ARG(event), to);
            } else if (o->decl->per_entity) {
                sb_printf(out, "\n\nRuns when a `" STR_FMT "` is sent to an entity that matches its parameters, "
                               "at the end of the %s.", STR_ARG(event), when);
            } else {
                sb_printf(out, "\n\nRuns once for every `" STR_FMT "` sent, at the end of the %s.", STR_ARG(event), when);
            }
        } else {
            sb_put(out, o->decl->is_view ? "\n\nRuns once per frame. It reads the match and changes local state, never the match."
                                         : o->decl->per_entity ? "\n\nRuns once per tick for every matching entity."
                                         : "\n\nRuns once per tick.");
        }
        describe_world(o->decl, out);
        describe_order(o->decl, out);
        break;
    case OCC_OWNERSHIP: {
        const bool with = o->param->mode == PARAM_WITH;
        code_block(out, with ? "with ownership" : "without ownership");
        sb_put(out, with ? "\n\nFilter: the view runs for the entities this machine's player owns: those with an `Owner` "
                           "whose `player` is `Session.player`. For entities any player owns, write `with Owner`."
                         : "\n\nFilter: the view runs for the entities this machine's player doesn't own: another "
                           "player's, and those with no `Owner`. With `with Owner` too, only another player's.");
        break;
    }
    case OCC_VERIFIED:
        describe_verified(o->param, o->param_of, out);
        break;
    case OCC_OUT: {
        if (o->declaration) { // On a parameter: what the parameter is
            format_param(o->param, &code);
            code_block(out, code.data);
            describe_out_param(o->param, o->param_of ? o->param_of : decl_of_param(o->param), out);
            break;
        }
        // Before an argument: what the call makes there
        const stmt *made = o->local;
        if (made && made->discard) {
            code_block(out, "out _");
            sb_put(out, "\n\nLeaves out what the call makes there: no variable takes it.");
        } else if (made) {
            sb_printf(&code, "out %s%s " STR_FMT, made->is_mut ? "mut " : "", type_name(made->type), STR_ARG(made->name));
            code_block(out, code.data);
            sb_printf(out, "\n\nDeclares `" STR_FMT "` here, a local the call makes: %s.", STR_ARG(made->name),
                      made->is_mut ? "the code can change it afterwards" : "read-only afterwards, unless it's declared `out mut`");
        } else {
            sb_printf(&code, "out %s", type_name(o->type));
            code_block(out, code.data);
            sb_put(out, "\n\nThe call makes the variable after it: it's written, as by an assignment, and what it held "
                        "isn't passed.");
        }
        if (o->param && o->param_of) {
            sb_printf(out, " For `" STR_FMT "`'s `", STR_ARG(o->param_of->name));
            format_param(o->param, out);
            sb_put(out, "`.");
        }
        break;
    }
    case OCC_SETTING: {
        const setting *st = setting_named(o->name);
        sb_printf(&code, "%s %s", type_name((type){st->kind, NULL}), st->name);
        code_block(out, code.data);
        sb_printf(out, "\n\nSetting: %s Without it: %s.", st->doc, st->otherwise);
        break;
    }
    case OCC_CONST:
        if (o->decl->kind == DECL_ASSET) {
            sb_printf(&code, "asset " STR_FMT " " STR_FMT, STR_ARG(o->decl->return_type_name), STR_ARG(o->decl->name));
            format_value_after(o->decl->at, &code);
            code_block(out, code.data);
            sb_put(out, "\n\nAsset: the file's contents, read as the game is built and packed with it, the same on every machine. "
                        "Any code can read it, and nothing can change it.");
            if (o->decl->asset_kind == ASSET_CUBEMAP) {
                sb_put(out, " A `Cubemap` is six images, the files its glob matches, each named for its face by how "
                            "its name ends before the extension (`px`, `nx`, `py`, `ny`, `pz`, `nz`): squares of one "
                            "size, this machine's to draw with. A frag reads it by a direction, "
                            "`Sample(direction)`.");
            }
            if (o->decl->asset_kind == ASSET_TEXTURE) {
                sb_put(out, " A `Texture` is an image to draw with, this machine's: no code reads its pixels, and the match "
                            "can't use it. For cells code reads, the same file is a `Grid2<Color>`, which is a Texture "
                            "too wherever one is drawn with.");
            }
            if (o->decl->asset_kind == ASSET_CLIP) {
                sb_put(out, " A `Clip` is a sound to play, this machine's: `Audio.Play(name)` in a view or a local handler plays "
                            "it, and the match can't use it.");
            }
            if (o->decl->sample_raw) {
                sb_put(out, "\n\nIt's `[Raw]`: its pixels are numbers, kept as the file has them, which a frag or a vert reads "
                            "by this name. It's no texture to draw with or hand on.");
            }
            if (o->decl->sample_filter || o->decl->sample_wrap) { // [Filter] and [Wrap] before it
                sb_put(out, "\n\nWhere a draw or a `Sample` doesn't say, it's sampled with ");
                const decl *enums[2] = {A.prog->filter, A.prog->wrap};
                const int said[2] = {o->decl->sample_filter, o->decl->sample_wrap};
                bool first = true;
                for (int k = 0; k < 2; k++) {
                    for (int m = 0; said[k] && enums[k] && m < enums[k]->members.count; m++) {
                        const enum_member *member = &enums[k]->members.items[m];
                        if (member->number != said[k] - 1) continue;
                        sb_printf(out, "%s`" STR_FMT "." STR_FMT "`", first ? "" : " and ", STR_ARG(enums[k]->name), STR_ARG(member->name));
                        first = false;
                    }
                }
                sb_put(out, ".");
            }
            break;
        }
        sb_printf(&code, "const " STR_FMT " " STR_FMT, STR_ARG(o->decl->return_type_name), STR_ARG(o->decl->name));
        format_value_after(o->decl->at, &code);
        code_block(out, code.data);
        sb_put(out, "\n\nConstant: the same on every machine. Any code can read it, and reading it makes no system wait.");
        break;
    case OCC_FIELD:
        sb_printf(&code, "%s%s " STR_FMT, o->field->is_local ? "local " : "", type_name(o->field->type), STR_ARG(o->field->name));
        if (o->decl && !o->decl->builtin) format_default(o->field, &code);
        code_block(out, code.data);
        if (o->decl && o->field->is_local) {
            sb_printf(out, "\n\nLocal field of %s `" STR_FMT "`: this machine's, outside the match. It's never sent, hashed or "
                           "rolled back, and starts at its default on every machine. Views read and write it through their "
                           "parameter (`mut " STR_FMT "` to write), and the match can't see it.",
                      decl_keyword(o->decl), STR_ARG(o->decl->name), STR_ARG(o->decl->name));
        } else if (o->decl) {
            sb_printf(out, "\n\nField of %s `" STR_FMT "`.", decl_keyword(o->decl), STR_ARG(o->decl->name));
        }
        if (button_field_doc(o->decl, o->field->name)) sb_printf(out, " %s", button_field_doc(o->decl, o->field->name));
        if (builtin_field_doc(o->decl, o->field->name)) sb_printf(out, " %s", builtin_field_doc(o->decl, o->field->name));
        break;
    case OCC_PARAM: {
        format_param(o->param, &code);
        code_block(out, code.data);
        const decl *of = o->param_of ? o->param_of : decl_of_param(o->param);
        if (o->param->mode == PARAM_QUERY) {
            sb_printf(out, "\n\nA read-only second entity set, independent of the entity this %s runs for. "
                           "`foreach` yields rows with `entity` and the named component values. Traversal follows "
                           "the ECS; it does not sort by entity ID or distance.",
                      of && of->is_view ? "view" : of && of->is_handler ? "handler" : "system");
            // Local code reads the match as it sees it, or this machine's own entities as they are
            if (o->param->query_local && of && (of->is_view || of->is_local)) {
                sb_put(out, " These are this machine's own entities, as they are.");
            } else if (of && of->is_view && o->param->query_verified_entities) {
                sb_put(out, " Everything in it is `verified`: it goes through the entities of the world the server has "
                            "confirmed, as its last tick has them.");
            } else if (of && of->is_view && o->param->query_verified_join) {
                sb_put(out, " It goes through the entities the view draws that are the same entity in the world the "
                            "server has confirmed: what's `verified` is as the last confirmed tick has it, and the rest "
                            "blended between ticks, like the view's own parameters.");
            } else if (of && of->is_view) {
                sb_put(out, " Its rows are the match as the view draws it: blended between the last two ticks, like the "
                            "view's own parameters, once a frame however often the view runs. Outside a match the view "
                            "doesn't run.");
            } else if (of && of->listens) {
                sb_printf(out, " Its rows are the match as the tick that sent the event left it%s.",
                          of->listens_verified ? ", once the server confirmed that tick" : "");
            }
            break;
        }
        if (o->param->query_source) {
            sb_put(out, "\n\nRead-only component data from this query row.");
            if (o->param->verified) sb_put(out, " As the last tick the server confirmed has it.");
            break;
        }
        if (is_shader(of)) {
            describe_shader_param(of, o->param, out);
            break;
        }
        if (o->param->type.kind == TY_WORLD) {
            sb_printf(out, "\n\nThe World: any entity's components by its handle, with `Get<T>(entity)`, `Has<T>(entity)`, "
                           "`TryGet<T>(entity, out value)` and%s `Set<T>(entity, value)`.",
                      o->param->mode == PARAM_MUT ? "" : ", when it's `mut`,");
            const decl *code_of = decl_of_param(o->param);
            if (code_of) describe_world(code_of, out);
            break;
        }
        // An extern system's: which of the rows' components it is, or which pointer after them
        if (of && of->extern_system && o->param->type.kind == TY_COMPONENT && extern_param_number(of, o->param) >= 0) {
            const int number = extern_param_number(of, o->param);
            sb_printf(out, "\n\nComponent %d of the rows C is handed, %s: `tide_rows_floats%s(rows, %d, offsetof(...))` is a "
                           "field of it as an array, a value a row, and `tide_rows_get` a row's whole component.",
                      number, o->param->mode == PARAM_MUT ? "which it can change" : "read-only",
                      o->param->mode == PARAM_MUT ? "_mut" : "", number);
            break;
        }
        if (of && of->extern_system && o->param->type.kind == TY_SINGLETON && extern_param_number(of, o->param) >= 0) {
            sb_printf(out, "\n\nSingleton, %s: C gets a pointer to it, argument %d after the rows.",
                      o->param->mode == PARAM_MUT ? "writable" : "read-only", extern_param_number(of, o->param) + 1);
            break;
        }
        if (o->param->type.kind == TY_INPUT) sb_put(out, "\n\nThe input of the player who owns the entity.");
        else if (o->param->mode == PARAM_EVENT) sb_put(out, of && of->is_command ? "\n\nThe untrusted command payload being handled, read-only." : "\n\nThe event being handled, read-only.");
        else if (o->param->mode == PARAM_SENDER) sb_put(out, "\n\nThe command sender's PlayerID, authenticated by the connection. Check its permissions before changing match state.");
        else if (o->param->mode == PARAM_IN) sb_put(out, "\n\nParameter, read-only: C gets a pointer to the value.");
        else if (o->param->is_out) describe_out_param(o->param, of, out);
        else if (o->param->mode == PARAM_MUT) sb_put(out, "\n\nParameter, writable.");
        else sb_put(out, "\n\nParameter, read-only.");
        break;
    }
    case OCC_LOCAL:
        if (o->local->type.decl && o->local->type.decl->query_row) {
            const param *query = query_for_row(o->local->type.decl);
            if (query) format_param(query, &code);
            code_block(out, code.data);
            sb_put(out, "\n\nA read-only query row: `entity` and the named component values.");
            break;
        }
        sb_printf(&code, "%s%s " STR_FMT, o->local->is_mut ? "mut " : "", type_name(o->local->type),
                  STR_ARG(o->local->name));
        code_block(out, code.data);
        sb_put(out, o->local->is_mut ? "\n\nLocal variable." : "\n\nLocal variable, read-only.");
        if (o->local->out_decl) { // F(out var name)
            sb_put(out, " An `out` argument declares it, and the call it's in makes it: it has a value where that call has "
                        "run, from there to the end of the block its statement is in, or of the loop whose condition "
                        "declares it.");
            if (!o->local->is_mut) {
                sb_printf(out, " `out mut %s " STR_FMT "` declares one the code can change afterwards.",
                          o->local->type_name.len > 0 ? str_to_cstr(o->local->type_name) : "var", STR_ARG(o->local->name));
            }
        }
        break;
    case OCC_THIS: {
        const decl *d = o->decl;
        if (o->type.kind == TY_ERROR) {
            code_block(out, "this");
            sb_put(out, "\n\nThe entity the code runs for.");
            break;
        }
        sb_printf(&code, "%s this", type_name(o->type));
        code_block(out, code.data);
        if (d->kind == DECL_METHOD) {
            sb_printf(out, "\n\nThe entity whose `" STR_FMT "` this is.", STR_ARG(d->owner->name));
        } else if (d->is_handler && d->event) {
            sb_printf(out, "\n\nThe entity the `" STR_FMT "` was sent to.", STR_ARG(d->event->name));
        } else {
            sb_printf(out, "\n\nThe entity `" STR_FMT "` runs for.", STR_ARG(d->name));
        }
        break;
    }
    case OCC_OWNER:
        code_block(out, str_to_cstr(o->name));
        // Their calls from the tables completion offers, so the two can't disagree
        if (str_eq_c(o->name, "Scene")) {
            sb_put(out, "\n\nLoads and unloads scenes, groups of entities that come and go together: ");
            put_names(out, scene_calls, sizeof scene_calls / sizeof scene_calls[0]);
            sb_put(out, ".");
        } else if (str_eq_c(o->name, "Session")) {
            sb_put(out, "\n\nWhich match this machine is in, and who's in the one it runs: ");
            put_names(out, session_calls, sizeof session_calls / sizeof session_calls[0]);
            sb_put(out, ", from views and local handlers. Take `Session session` to read where it stands.");
        } else if (str_eq_c(o->name, "Wait")) {
            sb_put(out, "\n\nWhat async code waits for, after `await`: ");
            put_names(out, wait_calls, sizeof wait_calls / sizeof wait_calls[0]);
            sb_put(out, ", the match's ticks, this machine's frames, or seconds.");
        } else {
            sb_put(out, builtin_draw_owner(o->name)        ? "\n\nImmediate-mode drawing into the window, in views and the "
                                                         "functions they call: shapes, text, 2D and 3D meshes and primitives, "
                                                         "the cameras and the settings. A render target has the same, as its "
                                                         "methods."
                      : str_eq_c(o->name, "GUI")       ? "\n\nThe GUI's widgets, each at a Rect; `Control`, a widget a view "
                                                         "draws itself; the widgets' theme; and claims of the pointer and "
                                                         "the keyboard for widgets that read the devices themselves. In "
                                                         "views and the functions they call."
                      : str_eq_c(o->name, "GUILayout") ? "\n\nThe GUI's widgets, laid out one after another, and containers "
                                                         "that arrange them. In views and the functions they call."
                      : str_eq_c(o->name, "Screen")    ? "\n\nThe window's size, in pixels. `Screen.Flip` gives a "
                                                         "position counted from its other edge: a device's, from "
                                                         "the bottom left, as the screen's, from the top left."
                      : str_eq_c(o->name, "Frame")     ? "\n\nThis machine's frame. `Frame.dt` is the seconds since the "
                                                         "previous one started, for what isn't the match's: particles, "
                                                         "menus, a camera's shake. Local code reads it, and the "
                                                         "functions it calls. The match's is `Time.dt`."
                      : str_eq_c(o->name, "File") || str_eq_c(o->name, "Directory") ? "\n\nThis machine's filesystem, accessible from local code and its helpers. Fallible operations use IOError."
                      : str_eq_c(o->name, "Application") ? "\n\nPersistent save and temporary cache directories, selected by the game's stable appId."
                      : str_eq_c(o->name, "Storage") ? "\n\nAwait Storage.Flush() to confirm that preceding changes reached persistent storage."
                      : str_eq_c(o->name, "Web") ? "\n\nThis machine's network: `await Web.Fetch(url)` gives a URL's bytes, from local code, failing with IOError."
                      : str_eq_c(o->name, "Path") ? "\n\nPure lexical path operations, without accessing the filesystem."
                      : str_eq_c(o->name, "Cursor")    ? "\n\nThe mouse's cursor. `Cursor.Lock()` hides it and keeps it in "
                                                         "place, for a camera that looks around: a view calls it every "
                                                         "frame the game wants the mouse. `Cursor.locked` is whether the "
                                                         "window holds it, which the system decides: views read it, and "
                                                         "the input's Sample."
                      : str_eq_c(o->name, "Devices")   ? "\n\nThis machine's keyboard, mouse, gamepad, touchscreen and "
                                                         "pointer. Views read them once per frame, less what the GUI is "
                                                         "using and what other views claimed, and the input's Sample once "
                                                         "per tick. Both take a `Devices` parameter. In match code the "
                                                         "parameter receives the entity owner's devices, or the server's."
                      : str_eq_c(o->name, "Clipboard") ? "\n\nThis machine's clipboard: Copy, from views and local handlers. "
                                                         "Ctrl+V pastes into text fields by itself."
                      : str_eq_c(o->name, "Debug")     ? "\n\nOutput for whoever runs the game: Log prints a line, Assert stops "
                                                         "the program when its condition fails. Neither changes the match."
                                                       : "\n\nMath functions and constants, deterministic on every platform.");
        }
        break;
    case OCC_FUNCTION:
    case OCC_CONSTANT:
        if (is_routine(o->decl)) {
            const decl *m = o->decl;
            if (m->is_extern && m->c_name && !str_eq_c(m->name, m->c_name)) sb_printf(&code, "[NativeName(\"%s\")]\n", m->c_name);
            format_routine(m, &code);
            code_block(out, code.data);
            if (m->is_extern) {
                sb_printf(out, "\n\nC function `%s`, which the game's C files or libraries define.",
                          o->decl->c_name ? o->decl->c_name : "?");
            } else if (m->is_async) {
                sb_put(out, "\n\nAsync function: `await` it for its value, or call it as a statement to start it as a "
                            "task, which goes on by itself.");
            } else {
                sb_put(out, "\n\nFunction: runs when it's called.");
            }
            break;
        }
        if (o->kind == OCC_FUNCTION && str_eq_c(o->owner, "Scene")) {
            for (size_t i = 0; i < sizeof scene_calls / sizeof scene_calls[0]; i++) {
                if (!str_eq_c(o->name, scene_calls[i].name)) continue;
                code_block(out, scene_calls[i].form);
                sb_printf(out, "\n\n%s", scene_calls[i].doc);
            }
        } else if (o->kind == OCC_FUNCTION && str_eq_c(o->owner, "Clipboard")) {
            code_block(out, CLIPBOARD_COPY_FORM);
            sb_put(out, "\n\n" CLIPBOARD_COPY_DOC);
        } else if (o->kind == OCC_FUNCTION && str_eq_c(o->owner, "Draw") && str_eq_c(o->name, "Blit")) {
            code_block(out, BLIT_FORM);
            sb_put(out, "\n\n" BLIT_DOC);
        } else if (o->kind == OCC_FUNCTION && str_eq_c(o->owner, "Session")) {
            for (size_t i = 0; i < sizeof session_calls / sizeof session_calls[0]; i++) {
                if (!str_eq_c(o->name, session_calls[i].name)) continue;
                code_block(out, session_calls[i].form);
                sb_printf(out, "\n\n%s", session_calls[i].doc);
            }
        } else if (o->kind == OCC_FUNCTION && str_eq_c(o->owner, "Wait")) {
            for (size_t i = 0; i < sizeof wait_calls / sizeof wait_calls[0]; i++) {
                if (!str_eq_c(o->name, wait_calls[i].name)) continue;
                code_block(out, wait_calls[i].form);
                sb_printf(out, "\n\n%s", wait_calls[i].doc);
            }
        } else if (o->kind == OCC_FUNCTION && o->owner.len == 0 && str_eq_c(o->name, "Send")) {
            code_block(out, "Send(event)");
            sb_put(out, "\n\nSends an event to the whole world. Its handlers run at the end of the tick, in the order "
                        "everything was sent.");
        } else if (o->kind == OCC_FUNCTION && o->owner.len == 0) {
            code_block(out, "Spawn(components...) -> Entity");
            sb_put(out, "\n\nCreates an entity with these components. It's added at the end of the tick; the "
                        "handle works right away.");
        } else if (builtin_describe(o->owner, o->name, &code)) {
            // The first lines are signatures, the last paragraph the doc.
            char *doc = strstr(code.data, "\n\n");
            if (doc) *doc = '\0';
            code_block(out, code.data);
            if (doc) sb_printf(out, "\n\n%s", doc + 2);
            if (is_mesh_draw(o->owner, o->name)) sb_put(out, "\n" MESH_SHADER_DOC);
        }
        break;
    case OCC_METHOD:
        if (is_routine(o->decl)) {
            format_routine(o->decl, &code);
            code_block(out, code.data);
            if (o->decl->is_operator) {
                sb_printf(out, "\n\nOperator of struct `" STR_FMT "`.", STR_ARG(o->decl->owner->name));
                break;
            }
            sb_printf(out, "\n\nMethod of %s `" STR_FMT "`. %s", decl_keyword(o->decl->owner), STR_ARG(o->decl->owner->name),
                      o->decl->is_mut_method ? "It changes the fields, so it's called on something writable."
                                             : "It only reads the fields.");
            break;
        }
        if (o->call == CALL_NONE && str_eq_c(o->name, "Sample")) {
            sb_put(&code, "Sample(");
            for (int i = 0; o->decl && i < o->decl->params.count; i++) {
                if (i) sb_put(&code, ", ");
                format_param(&o->decl->params.items[i], &code);
            }
            sb_put(&code, ")");
            code_block(out, code.data);
            sb_put(out, "\n\nBuilds the player's input from this machine's `Devices`, once per tick on their machine. "
                        "Fields start at their defaults. It runs outside the simulation, so it only sees the devices and "
                        "the local singletons it takes.");
        } else if (o->call == CALL_NONE && str_eq_c(o->name, "Sanitize")) {
            code_block(out, "Sanitize()");
            sb_put(out, "\n\nRuns on every input before the simulation reads it, including input from other "
                        "players, so systems can rely on what it guarantees.");
        } else {
            // What it's called on decides: items.Clear() is a list's, e.Destroy() an entity's.
            size_t count;
            const method_form *forms = methods_of(o->object_type, &count);
            const method_form *form = find_form(forms, count, o->name);
            if (!form) break;
            if (o->object_type.kind == TY_WORLD) format_world_method(form, o->component, &code);
            else format_method(form, o->object_type, &code);
            code_block(out, code.data);
            sb_printf(out, "\n\n%s", form->doc);
        }
        break;
    case OCC_MEMBER:
        sb_printf(&code, "%s " STR_FMT, type_name(o->type), STR_ARG(o->name));
        code_block(out, code.data);
        if (o->object_type.decl && o->object_type.decl->query_row) {
            sb_put(out, "\n\nThe entity matched by this query row, read-only.");
        }
        if (str_eq_c(o->name, "down")) sb_put(out, "\n\nTrue on the tick it became true.");
        if (str_eq_c(o->name, "up")) sb_put(out, "\n\nTrue on the tick it became false.");
        if (o->object_type.kind == TY_STRING && str_eq_c(o->name, "length")) sb_put(out, "\n\n" TEXT_LENGTH_DOC);
        if (keyboard_text(o->object_type, o->name)) sb_put(out, "\n\n" KEYBOARD_TEXT_DOC);
        if (o->object_type.kind == TY_LIST && str_eq_c(o->name, "count")) sb_put(out, "\n\n" LIST_COUNT_DOC);
        if (o->object_type.kind == TY_ARRAY && str_eq_c(o->name, "count")) sb_put(out, "\n\n" ARRAY_COUNT_DOC);
        if ((o->object_type.kind == TY_MAP || o->object_type.kind == TY_SET) && str_eq_c(o->name, "count")) sb_put(out, "\n\n" MAP_COUNT_DOC);
        if (o->object_type.kind == TY_RECORD && o->object_type.decl && o->object_type.decl->array_of
            && str_eq_c(o->name, "count")) {
            sb_put(out, "\n\n" DEVICE_ARRAY_COUNT_DOC);
        }
        if (o->object_type.kind == TY_GRID && str_eq_c(o->name, "size")) sb_put(out, "\n\n" GRID_SIZE_FIELD_DOC);
        break;
    case OCC_ATTRIBUTE: {
        static const struct {
            const char *name;
            const char *form;
            const char *doc;
        } attributes[] = {
            {"Stage", "[Stage(Stages.Phase, offset = 0)]",
             "Places this declaration in an enum phase. Phases follow the enum's declaration order; signed integer "
             "offsets order work within a phase. The enum's numeric values don't affect phase order."},
            {"Before", "[Before(DeclarationOrPhase, ...)]",
             "Runs before the named declarations, or before every declaration in the named enum phases."},
            {"After", "[After(DeclarationOrPhase, ...)]",
             "Runs after the named declarations, or after every declaration in the named enum phases."},
            {"Clamp", "[Clamp(lo, hi)]", "Keeps this input field between lo and hi. The engine applies it to every "
                                         "input, after repairing NaN and before Sanitize."},
            {"Min", "[Min(x)]", "Keeps this input field at least x, before Sanitize runs."},
            {"Max", "[Max(x)]", "Keeps this input field at most x, before Sanitize runs."},
            {"Absolute", "[Absolute]", "This input field's default isn't rest, as it says where something is, "
                                       "like an aim or a pointer's place. A machine guesses another player's input by their "
                                       "last one for a while, then as if they'd let go, each field back to its default; an "
                                       "absolute field is kept."},
            {"Snap", "[Snap]", "Views see this field as it is at the latest tick, not blended between the last two: "
                               "for angles that wrap and values that jump."},
            {"Position", "[Position]", "Provisional: a float or floating-point vector presented with linear tick interpolation and accumulated visual correction error."},
            {"Rotation", "[Rotation]", "Provisional: float radians or a quaternion presented through the shortest rotation, with accumulated visual correction error."},
            {"NativeName", "[NativeName(\"c_function\")]",
             "The C function the extern function after it calls, or the extern system after it is, when its name isn't the declaration's own."},
            {"Channel", "[Channel(Channels.Uploads)]",
             "Puts the command after it in a channel of the game's, an enum member: a player's commands are handled in the "
             "order sent within a channel, and in no order against another channel's, so a chat line never waits behind an "
             "upload. Without it a command is in the default channel."},
            {"Unordered", "[Unordered]",
             "The command after it has no order at all: the server handles it in whichever tick it arrives, before or after "
             "commands sent earlier, exactly once."},
            {"Filter", "[Filter(Filter.Point)]",
             "How this texture is read between its cells wherever a draw or a `Sample` doesn't say: `Filter.Point` "
             "for the nearest cell, `Filter.Bilinear` (what's read without it) for the four around, blended, "
             "`Filter.Trilinear` for that and between the texture's smaller images where it's drawn smaller. It goes "
             "before an asset that's a `Texture` or a `Grid2<Color>` (alone, or a list or a map of them) and before a "
             "field that's a `Grid2<Color>`, a `Grid2<float>` or a `RenderTarget` (for its color). The `Texture` made "
             "there carries it: one kept in a field or a local, or passed to a function, is sampled the same."},
            {"Raw", "[Raw]",
             "This texture's pixels are numbers, not colors to draw: they're kept as the file or the grid has them, with "
             "no alpha worked into them, and a `Sample` gives them as they are. A texture of colors has no color in a "
             "pixel whose alpha is 0, which is what a mask or a lookup table kept beside an alpha can't have. It goes "
             "before an asset that's one `Texture` or one `Grid2<Color>` and before a field that's a `Grid2<Color>`. "
             "A frag or a vert reads it by its name, `masks.Sample(uv)`: it's no `Texture` to draw with or hand on."},
            {"Wrap", "[Wrap(Wrap.Repeat)]",
             "What a `uv` past this texture's edges reads wherever a draw or a `Sample` doesn't say: `Wrap.Clamp` "
             "(what's read without it) for the cells at the edge, `Wrap.Repeat` for the texture over again, "
             "`Wrap.Mirror` for it the other way round each time. It goes before an asset that's a `Texture` or a "
             "`Grid2<Color>` and before a field that's a `Grid2<Color>`, a `Grid2<float>` or a `RenderTarget`. The "
             "`Texture` made there carries it, wherever it's kept."},
        };
        for (size_t i = 0; i < sizeof attributes / sizeof attributes[0]; i++) {
            if (!str_eq_c(o->name, attributes[i].name)) continue;
            code_block(out, attributes[i].form);
            sb_printf(out, "\n\n%s", attributes[i].doc);
        }
        break;
    }
    case OCC_ENUM_MEMBER:
        for (int i = 0; o->decl && i < o->decl->members.count; i++) {
            const enum_member *m = &o->decl->members.items[i];
            if (!str_eq(m->name, o->name)) continue;
            sb_printf(&code, STR_FMT "." STR_FMT " = %lld", STR_ARG(o->decl->name), STR_ARG(m->name), (long long)m->number);
            code_block(out, code.data);
            sb_printf(out, "\n\nMember of enum `" STR_FMT "`.", STR_ARG(o->decl->name));
        }
        break;
    case OCC_NAMESPACE: {
        sb_printf(&code, "namespace " STR_FMT, STR_ARG(o->name));
        code_block(out, code.data);
        int count = 0;
        for (int i = 0; i < A.prog->decls.count; i++) {
            const decl *d = A.prog->decls.items[i];
            if (!d->unit || !str_eq(d->unit->ns, o->name)) continue;
            sb_printf(out, "%s`" STR_FMT "`", count++ ? ", " : "\n\nDeclares ", STR_ARG(d->name));
        }
        break;
    }
    case OCC_REPLACE:
        code_block(out, str_to_cstr(o->name));
        if (str_eq_c(o->name, "virtual")) {
            sb_put(out, "\n\nAnother package can replace this declaration, with an `override` of the same name and "
                        "parameters: the parameters' types in order, with their filters. Nothing is replaced that isn't marked.");
        } else {
            sb_put(out, "\n\nReplaces the `virtual` declaration of this name and these parameters, in another package. "
                        "It's built in that one's place: where it is in the order, with its Stage, Before and After, and "
                        "under its name, for everything that names it.");
        }
        break;
    }
    describe_replacing(o, out);
}

// What a package is to a reader: "package Fast", or "the game".
static const char *package_text(const decl *d)
{
    const char *package = d->unit && d->unit->src ? d->unit->src->package : NULL;
    if (!package) return "the game";
    sb text = {0};
    sb_printf(&text, "package %s", package);
    return text.data;
}

// A declaration with its type, when it's a method: "Combat.Move", "Combat.Stats.IsDead".
static const char *code_name(const decl *d)
{
    sb text = {0};
    if (d->owner) sb_printf(&text, STR_FMT "." STR_FMT, STR_ARG(d->owner->qualified), STR_ARG(d->name));
    else sb_printf(&text, STR_FMT, STR_ARG(d->qualified.len ? d->qualified : d->name));
    return text.data;
}

// What an override replaced and what replaced a declaration, after what the
// declaration is: on its name, a reference to it, and the word before it.
static void describe_replacing(const occurrence *o, sb *out)
{
    const decl *d = o->decl;
    if (!d || (o->kind != OCC_SYSTEM && o->kind != OCC_FUNCTION && o->kind != OCC_METHOD && o->kind != OCC_REPLACE)) return;
    if (!out->data) return;
    if (d->replaces) {
        sb_printf(out, "\n\nOverrides `%s` of %s: this is what's built under that name, and what was there isn't in the "
                       "build.", code_name(d->replaces), package_text(d->replaces));
    } else if (d->replaced_by) {
        sb_printf(out, "\n\n**Replaced** by %s's override: this code isn't in the build. It's still checked for errors.",
                  package_text(d->replaced_by));
    } else if (d->is_virtual && o->kind != OCC_REPLACE) {
        sb_put(out, "\n\n`virtual`: another package can replace it. Nothing does.");
    } else if (d->is_virtual) {
        sb_put(out, "\n\nNothing replaces it.");
    }
}

// "3rd"
static const char *ordinal(const int n)
{
    static char buf[16];
    const int tens = n % 100;
    const char *suffix = tens >= 11 && tens <= 13 ? "th" : n % 10 == 1 ? "st" : n % 10 == 2 ? "nd" : n % 10 == 3 ? "rd" : "th";
    snprintf(buf, sizeof buf, "%d%s", n, suffix);
    return buf;
}

// Where a system or view runs, and what it runs after: the schedule, visible.
static void describe_order(const decl *d, sb *out)
{
    if (d->order_enum && d->order_member) {
        sb_printf(out, "\n\nPhase `" STR_FMT "." STR_FMT "`, offset %d.",
                  STR_ARG(d->order_enum->qualified), STR_ARG(d->order_member->name), d->order_offset);
    }
    if (d->is_handler) {
        if (!d->event) return;
        // A match event's local handlers hear it among themselves, apart from the handlers the tick runs
        const int count = d->listens ? d->event->listeners.count : d->event->handlers.count;
        decl *const *among = d->listens ? d->event->listeners.items : d->event->handlers.items;
        int index = 0;
        while (index < count && among[index] != d) index++;
        if (count < 2 || index == count) return;
        sb_printf(out, "\n\nRuns %s of %d %shandlers of `" STR_FMT "`", ordinal(index + 1), count, d->listens ? "local " : "",
                  STR_ARG(d->event->name));
        for (int i = 0; i < d->after.count; i++) {
            sb_printf(out, "%s`" STR_FMT "`", i ? ", " : ", after ", STR_ARG(d->after.items[i]->qualified));
        }
        sb_put(out, ".");
        return;
    }
    const int count = d->is_view ? A.prog->views.count : A.prog->systems.count;
    sb_printf(out, "\n\nRuns %s of %d %s", ordinal(d->index + 1), count,
              d->is_view ? "views each frame (later views draw on top)" : "systems each tick");
    for (int i = 0; i < d->after.count; i++) {
        sb_printf(out, "%s`" STR_FMT "`", i ? ", " : ", after ", STR_ARG(d->after.items[i]->qualified));
    }
    sb_put(out, ".");
    if (d->is_view || d->stage == 0) return;

    sb_printf(out, "\n\n**Stage %d.** ", d->stage);
    if (d->waits.count == 0) sb_put(out, "It doesn't wait for any system.");
    else sb_put(out, "It waits for:");
    for (int i = 0; i < d->waits.count; i++) {
        const system_wait *w = &d->waits.items[i];
        sb_put(out, "\n- ");
        put_decl_name(out, w->on, "`", d);
        sb_put(out, ": ");
        describe_wait(d, w, "`", out);
    }
    if (d->waits_unlisted) sb_printf(out, "\n- %d more, through those", d->waits_unlisted);
    if (d->alongside_count == 0) {
        sb_put(out, "\n\nNo other system can run at the same time.");
    } else {
        // The analysis lists the first few
        const int more = d->alongside_count - d->alongside.count;
        sb_put(out, "\n\nCan run at the same time as ");
        for (int i = 0; i < d->alongside.count; i++) {
            sb_put(out, i == 0 ? "" : i == d->alongside.count - 1 && !more ? " and " : ", ");
            put_decl_name(out, d->alongside.items[i], "`", d);
        }
        if (more) sb_printf(out, " and %d more", more);
        sb_put(out, ".");
    }
}

// ---------------------------------------------------------------------------
// Code lenses: above each system, its stage and why it waits, above each
// local handler of a match event, when it hears it, and above a declaration
// an override replaced, and the override, which is which

static const char *package_text(const decl *d);
static const char *code_name(const decl *d);

// A lens with `title` above `d`'s name. It only says: it runs no command.
static void write_lens(jbuf *out, int *written, const decl *d, const char *title)
{
    if ((*written)++) jb_put(out, ",");
    jb_put(out, "{\"range\":");
    write_range(out, d->at, d->name.len);
    jb_put(out, ",\"command\":{\"title\":");
    jb_string(out, title);
    jb_put(out, ",\"command\":\"\"}}");
}

// What code reads and writes of every entity's through its World and its
// queries, for the line above it: "World/query: reads Body; writes Health",
// after `before`. False, with nothing written, when it reaches none.
static bool put_world_lens(sb *title, const decl *d, const char *before)
{
    sb reads = {0};
    sb writes = {0};
    const int read_count = world_use_names(d, false, "", d, &reads);
    const int write_count = world_use_names(d, true, "", d, &writes);
    if (read_count + write_count == 0) return false;
    bool world = false;
    bool query = false;
    for (int i = 0; i < d->params.count; i++) {
        world |= d->params.items[i].type.kind == TY_WORLD;
        query |= d->params.items[i].mode == PARAM_QUERY;
    }
    sb_printf(title, "%s%s", before, world && query ? "World/query: " : query ? "Query: " : "World: ");
    if (read_count) sb_printf(title, "reads %s", reads.data);
    if (read_count && write_count) sb_put(title, "; ");
    if (write_count) sb_printf(title, "writes %s", writes.data);
    return true;
}

void analysis_code_lenses(jbuf *out)
{
    jb_put(out, "[");
    int written = 0;
    for (int i = 0; i < A.prog->systems.count; i++) {
        const decl *d = A.prog->systems.items[i];
        if (d->at.file != A.doc || d->stage == 0) continue;
        sb title = {0};
        sb_printf(&title, "stage %d", d->stage);
        if (d->extern_system) sb_put(&title, " \u00B7 in C"); // Scheduled by what it declares: its code isn't read
        put_world_lens(&title, d, " \u00B7 "); // Access to every entity's, which its signature doesn't say
        for (int k = 0; k < d->waits.count; k++) {
            const system_wait *w = &d->waits.items[k];
            if (w->through) continue; // Shown on the system it goes through
            sb_put(&title, " \u00B7 after ");
            put_decl_name(&title, w->on, "", d);
            sb_put(&title, ": ");
            describe_wait(d, w, "", &title);
        }
        if (d->alongside_count == 0 && A.prog->systems.count > 1) {
            sb_put(&title, " \u00B7 nothing runs alongside");
        } else if (d->alongside_count > 0) {
            sb_put(&title, " \u00B7 alongside ");
            const int shown = d->alongside.count > 3 ? 3 : d->alongside.count;
            for (int k = 0; k < shown; k++) {
                if (k) sb_put(&title, ", ");
                put_decl_name(&title, d->alongside.items[k], "", d);
            }
            if (d->alongside_count > shown) sb_printf(&title, " and %d more", d->alongside_count - shown);
        }
        write_lens(out, &written, d, title.data);
    }
    // A view's access to every entity's, which its signature doesn't say either
    for (int i = 0; i < A.prog->views.count; i++) {
        const decl *d = A.prog->views.items[i];
        sb title = {0};
        if (d->at.file == A.doc && put_world_lens(&title, d, "")) write_lens(out, &written, d, title.data);
    }
    // What replaces a virtual declaration, above it, and what an override replaces
    for (int i = 0; i < A.prog->replaced.count; i++) {
        const decl *v = A.prog->replaced.items[i];
        const decl *o = v->replaced_by;
        if (v->at.file == A.doc) {
            sb title = {0};
            sb_printf(&title, "replaced by %s: not in the build", package_text(o));
            write_lens(out, &written, v, title.data);
        }
        if (o->at.file == A.doc) {
            sb title = {0};
            sb_printf(&title, "replaces %s of %s", code_name(v), package_text(v));
            write_lens(out, &written, o, title.data);
        }
    }
    // The program's handlers rather than each event's listeners, which the
    // checker only lists once the game has no errors
    for (int i = 0; i < A.prog->handlers.count; i++) {
        const decl *h = A.prog->handlers.items[i];
        if (h->at.file != A.doc || !h->listens || !h->event) continue;
        sb title = {0};
        sb_put(&title, "hears ");
        put_decl_name(&title, h->event, "", h);
        sb_put(&title, h->listens_verified ? " once the server confirmed it" : " at the start of a frame");
        put_world_lens(&title, h, " \u00B7 ");
        write_lens(out, &written, h, title.data);
    }
    jb_put(out, "]");
}

// ---------------------------------------------------------------------------
// Quick fixes

static loc type_start(const param *p)
{
    return p->type_qual_at.line > 0 ? p->type_qual_at : p->type_at;
}

// Where a parameter's `mut` goes, and what follows one it has: its `verified`
// (`mut verified Target target`), or its type.
static loc mut_end(const param *p)
{
    return p->verified_at.line > 0 ? p->verified_at : type_start(p);
}

static void write_edit_range(jbuf *out, const loc start, const loc end)
{
    jb_put(out, "{\"start\":");
    write_position(out, start);
    jb_put(out, ",\"end\":");
    write_position(out, end);
    jb_put(out, "}");
}

// The text of line `line` (1-based) of the document, without its line break.
static str doc_line(const int line)
{
    if (line < 1 || line > DOC->lines.count) return (str){"", 0};
    const char *start = DOC->lines.items[line - 1];
    const char *end = start;
    const char *text_end = DOC->src.text + DOC->src.len;
    while (end < text_end && *end != '\n') end++;
    if (end > start && end[-1] == '\r') end--;
    return (str){start, (int)(end - start)};
}

static bool blank_line(const int line)
{
    const str text = doc_line(line);
    for (int i = 0; i < text.len; i++) {
        if (!isspace((unsigned char)text.ptr[i])) return false;
    }
    return true;
}

static bool comment_line(const int line)
{
    str text = doc_line(line);
    while (text.len > 0 && isspace((unsigned char)text.ptr[0])) {
        text.ptr++;
        text.len--;
    }
    return text.len >= 2 && text.ptr[0] == '/' && text.ptr[1] == '/';
}

static bool can_create_files = true;

void analysis_set_can_create_files(const bool can)
{
    can_create_files = can;
}

// Refactoring: moves a type, function, frag or vert into a file of its own,
// named after it, next to this one. A game is every .tide file in its folder,
// so the program stays the same. Systems and views stay put: moving one would
// change the order they run in, which follows the files.
static void move_to_file_action(const int line, jbuf *out, int *written)
{
    for (int i = 0; can_create_files && i < A.written.count; i++) {
        const decl *d = A.written.items[i];
        if (d->builtin || d->at.file != A.doc || d->kind == DECL_SYSTEM || d->kind == DECL_CONST) continue;
        if (d->is_override) continue; // It's where its package has it, and names what it replaces from there
        // Its lines: the attributes and comments just above it, to its closing brace.
        int first = d->at.line;
        for (int a = 0; a < d->attributes.count; a++) {
            if (d->attributes.items[a].at.line < first) first = d->attributes.items[a].at.line;
        }
        while (first > 1 && comment_line(first - 1)) first--;
        const int last = d->end.line > 0 ? d->end.line : d->at.line;
        if (line + 1 < first || line + 1 > last) continue;

        // Beside this file: file:///D:/game/main.tide -> file:///D:/game/Health.tide
        const char *uri = DOC->uri;
        const char *slash = strrchr(uri, '/');
        if (!slash) return;
        sb target = {0};
        sb_printf(&target, "%.*s/" STR_FMT ".tide", (int)(slash - uri), uri, STR_ARG(d->name));
        if (strcmp(target.data, uri) == 0) return; // Already in its own file
        for (int f = 0; f < A.file_count; f++) {
            if (A.files[f].uri && strcmp(A.files[f].uri, target.data) == 0) return; // Taken
        }

        // The new file: this file's namespace and usings, then the declaration.
        sb text = {0};
        const unit *u = d->unit;
        if (u && u->ns.len > 0) sb_printf(&text, "namespace " STR_FMT ";\n", STR_ARG(u->ns));
        for (int k = 0; u && k < u->usings.count; k++) sb_printf(&text, "using " STR_FMT ";\n", STR_ARG(u->usings.items[k]));
        if (text.len > 0) sb_put(&text, "\n");
        for (int l = first; l <= last; l++) {
            const str line_text = doc_line(l);
            sb_putn(&text, line_text.ptr, (size_t)line_text.len);
            sb_put(&text, "\n");
        }
        // What's left here: no double blank line where it was.
        int remove_to = last + 1;
        if (blank_line(remove_to) && (first == 1 || blank_line(first - 1))) remove_to++;

        if ((*written)++) jb_put(out, ",");
        sb title = {0};
        sb_printf(&title, "Move '" STR_FMT "' to " STR_FMT ".tide", STR_ARG(d->name), STR_ARG(d->name));
        jb_put(out, "{\"title\":");
        jb_string(out, title.data);
        jb_put(out, ",\"kind\":\"refactor.move\",\"edit\":{\"documentChanges\":[{\"kind\":\"create\",\"uri\":");
        jb_string(out, target.data);
        jb_put(out, ",\"options\":{\"overwrite\":false,\"ignoreIfExists\":false}},{\"textDocument\":{\"uri\":");
        jb_string(out, target.data);
        jb_put(out, ",\"version\":null},\"edits\":[{\"range\":");
        write_edit_range(out, (loc){1, 1, A.doc}, (loc){1, 1, A.doc});
        jb_put(out, ",\"newText\":");
        jb_string(out, text.data);
        jb_put(out, "}]},{\"textDocument\":{\"uri\":");
        jb_string(out, uri);
        jb_put(out, ",\"version\":null},\"edits\":[{\"range\":");
        write_edit_range(out, (loc){first, 1, A.doc}, (loc){remove_to, 1, A.doc});
        jb_put(out, ",\"newText\":\"\"}]}]}}");
        return;
    }
}

// Where text added at the end of the document goes, and what comes before it
// so a declaration there stands apart from what's above.
static loc doc_end(const char **separator)
{
    const int last = DOC->lines.count > 0 ? DOC->lines.count : 1;
    const bool ends_with_newline = DOC->src.len > 0 && DOC->src.text[DOC->src.len - 1] == '\n';
    *separator = DOC->src.len == 0 ? "" : ends_with_newline ? "\n" : "\n\n";
    return (loc){last, doc_line(last).len + 1, A.doc};
}

// A parameter name for an argument of a function to create: the name of what's
// passed (unit.stats: stats), or its type's (Stats: stats, float: value).
static void argument_name(const expr *arg, sb *out)
{
    if (arg->kind == E_NAME) {
        sb_printf(out, STR_FMT, STR_ARG(arg->name));
    } else if (arg->kind == E_MEMBER && arg->field) {
        sb_printf(out, STR_FMT, STR_ARG(arg->member));
    } else if (arg->type.decl) {
        const str name = arg->type.decl->name;
        sb_printf(out, "%c%.*s", tolower((unsigned char)name.ptr[0]), name.len - 1, name.ptr + 1);
    } else {
        sb_put(out, "value");
    }
}

static loc token_end(const token *t)
{
    return (loc){t->at.line, t->at.col + token_len(t), t->at.file};
}

// An Entity or LocalEntity parameter: removes it, with the comma between it
// and its neighbor, and writes `this` where the code uses it.
static void use_this_action(const param *p, jbuf *out, int *written)
{
    const int first = token_at(p->at);
    const int name = token_at(p->name_at);
    if (first < 0 || name < 0) return;
    const token *toks = DOC->toks;
    loc start = p->at;
    loc end = token_end(&toks[name]);
    if (toks[name + 1].kind == T_COMMA) {
        end = toks[name + 2].at; // Up to the next parameter
    } else if (first > 1 && toks[first - 1].kind == T_COMMA) {
        start = token_end(&toks[first - 2]); // From the end of the one before
    }
    if ((*written)++) jb_put(out, ",");
    sb title = {0};
    sb_printf(&title, "Use 'this' instead of '" STR_FMT "'", STR_ARG(p->name));
    jb_put(out, "{\"title\":");
    jb_string(out, title.data);
    jb_put(out, ",\"kind\":\"quickfix\",\"isPreferred\":true,\"edit\":{\"changes\":{");
    jb_string(out, A.files[A.doc].uri);
    jb_put(out, ":[{\"range\":");
    write_edit_range(out, start, end);
    jb_put(out, ",\"newText\":\"\"}");
    for (int i = 0; i < A.occs.count; i++) {
        const occurrence *o = &A.occs.items[i];
        if (o->kind != OCC_PARAM || o->param != p || o->declaration || o->at.file != A.doc) continue;
        jb_put(out, ",{\"range\":");
        write_range(out, o->at, o->len);
        jb_put(out, ",\"newText\":\"this\"}");
    }
    jb_put(out, "]}}}");
}

// The diagnostic at `at`, or NULL.
static const diagnostic *diagnostic_at(const loc at)
{
    for (int i = 0; i < A.diags.count; i++) {
        if (loc_cmp(A.diags.items[i].at, at) == 0) return &A.diags.items[i];
    }
    return NULL;
}

// A quick fix that replaces `start` to `end` in the document with `text`, and
// fixes `d`, if it says which diagnostic.
static void quick_fix(jbuf *out, int *written, const char *title, const bool preferred, const diagnostic *d,
                      const loc start, const loc end, const char *text)
{
    if ((*written)++) jb_put(out, ",");
    jb_put(out, "{\"title\":");
    jb_string(out, title);
    jb_printf(out, ",\"kind\":\"quickfix\",\"isPreferred\":%s", preferred ? "true" : "false");
    if (d) {
        jb_put(out, ",\"diagnostics\":[");
        write_diagnostic(out, d);
        jb_put(out, "]");
    }
    jb_put(out, ",\"edit\":{\"changes\":{");
    jb_string(out, A.files[A.doc].uri);
    jb_put(out, ":[{\"range\":");
    write_edit_range(out, start, end);
    jb_put(out, ",\"newText\":");
    jb_string(out, text);
    jb_put(out, "}]}}}");
}

// One change of a quick fix that makes several, in any of the game's files.
typedef struct text_edit {
    loc start;
    loc end;
    const char *text;
} text_edit;

typedef VEC(text_edit) text_edits;

// A quick fix that changes several places at once, file by file.
static void quick_fix_edits(jbuf *out, int *written, const char *title, const bool preferred, const diagnostic *d,
                            const text_edits *edits)
{
    if ((*written)++) jb_put(out, ",");
    jb_put(out, "{\"title\":");
    jb_string(out, title);
    jb_printf(out, ",\"kind\":\"quickfix\",\"isPreferred\":%s", preferred ? "true" : "false");
    if (d) {
        jb_put(out, ",\"diagnostics\":[");
        write_diagnostic(out, d);
        jb_put(out, "]");
    }
    jb_put(out, ",\"edit\":{\"changes\":{");
    int files = 0;
    for (int file = 0; file < A.file_count; file++) {
        int in_file = 0;
        for (int i = 0; i < edits->count; i++) {
            const text_edit *e = &edits->items[i];
            if (e->start.file != file) continue;
            if (in_file++ == 0) {
                if (files++) jb_put(out, ",");
                jb_string(out, A.files[file].uri);
                jb_put(out, ":[");
            } else {
                jb_put(out, ",");
            }
            jb_put(out, "{\"range\":");
            write_edit_range(out, e->start, e->end);
            jb_put(out, ",\"newText\":");
            jb_string(out, e->text);
            jb_put(out, "}");
        }
        if (in_file) jb_put(out, "]");
    }
    jb_put(out, "}}}");
}

static loc expr_start(const expr *e);
static loc expr_end(const expr *e);
static const unit *doc_unit(void);
static bool visible_by_name(const decl *d);

// A failable call's or a T?'s value that nothing handles: carry on with its
// default (`!`), or in a function that fails with the same error, pass it on
// (`try`), as the checker's note says.
static void unwrap_actions(const fix *f, jbuf *out, int *written)
{
    const diagnostic *d = diagnostic_at(f->at);
    const loc end = expr_end(f->call);
    quick_fix(out, written, "Carry on with the default if there's none: add '!'", true, d, end, end, "!");
    if (!f->method) return;
    sb title = {0};
    sb_printf(&title, "Pass the error on to the caller of '" STR_FMT "': add 'try'", STR_ARG(f->method->name));
    const loc start = expr_start(f->call);
    quick_fix(out, written, title.data, false, d, start, start, "try ");
}

// A local nothing reads, as the checker's note says: its declaration goes, or
// when its value is a call that does something, only the local does and the
// call stays, a statement of its own.
static void unused_local_action(const fix *f, jbuf *out, int *written)
{
    const stmt *s = f->local;
    const token *toks = DOC->toks;
    const int first = token_at(s->at);
    const int name = token_at(s->name_at);
    if (first < 0 || name < 0 || name + 2 >= DOC->tok_count || toks[name + 1].kind != T_ASSIGN) return;
    const diagnostic *d = diagnostic_at(f->at);
    sb title = {0};
    if (f->kind == FIX_KEEP_CALL) {
        sb_printf(&title, "Keep the call, without '" STR_FMT "'", STR_ARG(s->name));
        quick_fix(out, written, title.data, true, d, s->at, toks[name + 2].at, "");
        return;
    }
    // The ';' that ends it: its value has no other outside brackets
    int last = name + 2;
    for (int depth = 0; last < DOC->tok_count; last++) {
        const tok_kind k = toks[last].kind;
        if (k == T_EOF || (k == T_SEMI && depth == 0)) break;
        if (k == T_LPAREN || k == T_LBRACKET || k == T_LBRACE) depth++;
        else if (k == T_RPAREN || k == T_RBRACKET || k == T_RBRACE) depth--;
    }
    if (last >= DOC->tok_count || toks[last].kind != T_SEMI) return;
    // Only a statement of a block: the body of an `if` without braces has to stay a statement
    const tok_kind before = first > 0 ? toks[first - 1].kind : T_EOF;
    if (before != T_LBRACE && before != T_RBRACE && before != T_SEMI && before != T_COLON) return;
    loc start = s->at;
    loc end = token_end(&toks[last]);
    const str first_line = doc_line(start.line);
    const str last_line = doc_line(end.line);
    bool alone = true; // On its lines: they go with it
    for (int i = 0; i < start.col - 1 && i < first_line.len; i++) alone &= isspace((unsigned char)first_line.ptr[i]) != 0;
    for (int i = end.col - 1; i < last_line.len; i++) alone &= isspace((unsigned char)last_line.ptr[i]) != 0;
    if (alone && end.line < DOC->lines.count) {
        start = (loc){start.line, 1, A.doc};
        end = (loc){end.line + 1, 1, A.doc};
    } else {
        while (end.col - 1 < last_line.len && last_line.ptr[end.col - 1] == ' ') end.col++;
    }
    sb_printf(&title, "Remove '" STR_FMT "'", STR_ARG(s->name));
    quick_fix(out, written, title.data, true, d, start, end, "");
}

// "did you mean" in a diagnostic, as the checker suggested it: the name meant
// in the place of the one written.
static void suggestion_actions(const int start_line, const int end_line, jbuf *out, int *written)
{
    for (int i = 0; i < A.diags.count; i++) {
        const diagnostic *d = &A.diags.items[i];
        if (d->fix_len == 0 || d->fix_at.file != A.doc || d->at.line - 1 < start_line || d->at.line - 1 > end_line) continue;
        sb title = {0};
        if (d->fix.len > 0) {
            sb_printf(&title, "Change to '" STR_FMT "'", STR_ARG(d->fix));
        } else { // What's written goes: a word, with the space after it
            const char *text = DOC->lines.items[d->fix_at.line - 1] + d->fix_at.col - 1;
            int len = d->fix_len;
            while (len > 0 && (text[len - 1] == ' ' || text[len - 1] == '\t')) len--;
            sb_printf(&title, "Remove '%.*s'", len, text);
        }
        const loc end = {d->fix_at.line, d->fix_at.col + d->fix_len, d->fix_at.file};
        quick_fix(out, written, title.data, true, d, d->fix_at, end, str_to_cstr(d->fix));
    }
}

// An unknown name that another namespace declares: write it with its
// namespace, or add a `using` for it.
static void namespace_actions(const int start_line, const int end_line, jbuf *out, int *written)
{
    const unit *u = doc_unit();
    for (int i = 0; i < A.diags.count; i++) {
        const diagnostic *d = &A.diags.items[i];
        if (d->severity != DIAG_ERROR || d->at.file != A.doc) continue;
        if (d->at.line - 1 < start_line || d->at.line - 1 > end_line) continue;
        const int t = token_at(d->at);
        if (t < 0 || DOC->toks[t].kind != T_IDENT || (t > 0 && DOC->toks[t - 1].kind == T_DOT)) continue;
        bool known = false; // The name means something there already: the error's about something else
        for (int k = 0; k < A.occs.count && !known; k++) known = loc_cmp(A.occs.items[k].at, d->at) == 0;
        const str name = DOC->toks[t].text;
        VEC(str) offered = {0};
        for (int k = 0; !known && k < A.prog->decls.count; k++) {
            const decl *other = A.prog->decls.items[k];
            if (other->builtin || !other->unit || other->unit->ns.len == 0 || !str_eq(other->name, name)) continue;
            if (visible_by_name(other)) continue;
            bool repeated = false;
            for (int j = 0; j < offered.count; j++) repeated |= str_eq(offered.items[j], other->unit->ns);
            if (repeated) continue;
            vec_push(offered, other->unit->ns);
        }
        for (int k = 0; k < offered.count; k++) {
            const str ns = offered.items[k];
            sb title = {0};
            sb text = {0};
            sb_printf(&text, STR_FMT "." STR_FMT, STR_ARG(ns), STR_ARG(name));
            sb_printf(&title, "Write '%s'", text.data);
            const loc end = {d->at.line, d->at.col + name.len, d->at.file};
            quick_fix(out, written, title.data, false, d, d->at, end, text.data);
            // After the file's usings, or its namespace, or at its top
            int line = 1;
            const char *after = "\n";
            for (int j = 0; u && j < u->using_at.count; j++) {
                if (u->using_at.items[j].line >= line) line = u->using_at.items[j].line + 1;
            }
            if (u && u->using_at.count == 0 && u->ns.len > 0) line = u->ns_at.line + 1;
            if (u && (u->using_at.count > 0 || u->ns.len > 0)) after = "";
            sb using_text = {0};
            sb title_using = {0};
            sb_printf(&using_text, "using " STR_FMT ";\n%s", STR_ARG(ns), after);
            sb_printf(&title_using, "Add 'using " STR_FMT ";'", STR_ARG(ns));
            const loc at = {line, 1, A.doc};
            quick_fix(out, written, title_using.data, offered.count == 1, d, at, at, using_text.data);
        }
    }
}

// An `out` parameter read before it's assigned, as the checker's note says:
// its default as the function starts, which keeps what its calls write, or
// the parameter as `mut`, to read what the caller has, with the word gone
// from every call. The second isn't offered where a call declares the
// variable there (`out var x`, `out _`), which a `mut` parameter can't take.
// What fills an `out` parameter where the code has nothing to give: its type's
// default, or for a frag's color white, which is what `mut Color color` started as.
static const char *filler(const decl *code, const param *p)
{
    return is_shader(code) && code->is_frag && p->type.kind == TY_COLOR ? "Color.white" : "default";
}

static void assign_first_actions(const fix *f, jbuf *out, int *written)
{
    const param *p = f->param;
    const decl *m = f->method;
    const diagnostic *d = diagnostic_at(f->at);
    const int open = m->body && m->body->at.file == A.doc ? token_at(m->body->at) : -1;
    if (open >= 0 && open + 1 < DOC->tok_count) {
        const token *next = &DOC->toks[open + 1]; // The body's first statement
        sb title = {0};
        sb text = {0};
        const char *value = filler(m, p);
        if (is_shader(m)) sb_printf(&title, "Write '" STR_FMT " = %s;' first", STR_ARG(p->name), value);
        else sb_printf(&title, "Assign '" STR_FMT " = %s;' first", STR_ARG(p->name), value);
        if (next->at.line > m->body->at.line) { // On a line of its own, as far in as the statement after it
            const str line = doc_line(next->at.line);
            const int indent = next->at.col - 1 < line.len ? next->at.col - 1 : line.len;
            sb_printf(&text, "%.*s" STR_FMT " = %s;\n", indent, line.ptr, STR_ARG(p->name), value);
            const loc at = {next->at.line, 1, A.doc};
            quick_fix(out, written, title.data, true, d, at, at, text.data);
        } else {
            sb_printf(&text, STR_FMT " = %s; ", STR_ARG(p->name), value);
            quick_fix(out, written, title.data, true, d, next->at, next->at, text.data);
        }
    }
    if (p->out_at.line == 0 || is_shader(m)) return; // What a frag or a vert writes is `out`, and nothing else
    const int k = (int)(p - m->params.items);
    text_edits edits = {0};
    const text_edit word = {p->out_at, {p->out_at.line, p->out_at.col + 3, p->out_at.file}, "mut"};
    vec_push(edits, word);
    int calls = 0;
    for (int i = 0; i < A.calls.count; i++) {
        const expr *call = A.calls.items[i];
        if ((call->call != CALL_FUNCTION && call->call != CALL_METHOD) || call->method != m || k >= call->args.count) continue;
        const expr *arg = call->args.items[k];
        if (!arg->is_out) continue; // Wrong already, and said so
        if (arg->out_local) return; // It makes the variable there
        const int t = token_at(arg->out_at);
        if (t < 0 || t + 1 >= A.files[arg->out_at.file].tok_count) return;
        const text_edit gone = {arg->out_at, A.files[arg->out_at.file].toks[t + 1].at, ""};
        vec_push(edits, gone);
        calls++;
    }
    sb title = {0};
    sb_printf(&title, "Take '" STR_FMT "' as 'mut'%s", STR_ARG(p->name), calls == 0 ? "" : calls == 1 ? ", at its call too" : ", at its calls too");
    quick_fix_edits(out, written, title.data, false, d, &edits);
}

// Where a declaration's header starts: its first word, before its name (`system`,
// `local`, a function's type), past a handler's trigger.
static loc header_start(const decl *d)
{
    const afile *f = &A.files[d->at.file];
    int i = token_at(d->at);
    if (i < 0) return (loc){0, 0, 0};
    int depth = 0;
    while (i > 0) {
        const tok_kind k = f->toks[i - 1].kind;
        if (k == T_RPAREN) depth++;
        else if (k == T_LPAREN) depth--;
        else if (depth == 0 && (k == T_RBRACE || k == T_SEMI || k == T_RBRACKET || k == T_LBRACE)) break;
        i--;
    }
    return f->toks[i].at;
}

// `virtual` before the declaration an override names, which may be in another
// file, or `override` before one that repeats a virtual declaration.
static void replace_word_action(const fix *f, jbuf *out, int *written)
{
    const decl *d = f->method;
    const loc start = header_start(d);
    if (start.line == 0) return;
    const bool virtual = f->kind == FIX_ADD_VIRTUAL;
    text_edits edits = {0};
    vec_push(edits, ((text_edit){start, start, virtual ? "virtual " : "override "}));
    sb title = {0};
    if (virtual) sb_printf(&title, "Mark '" STR_FMT "' virtual", STR_ARG(d->name));
    else sb_printf(&title, "Make it an override of '" STR_FMT "'", STR_ARG(d->qualified));
    quick_fix_edits(out, written, title.data, true, diagnostic_at(f->at), &edits);
}

static void asset_fix_actions(int start_line, int end_line, jbuf *out, int *written);

void analysis_code_actions(const int start_line, const int end_line, jbuf *out)
{
    jb_put(out, "[");
    int written = 0;
    suggestion_actions(start_line, end_line, out, &written);
    namespace_actions(start_line, end_line, out, &written);
    asset_fix_actions(start_line, end_line, out, &written);
    for (int i = 0; i < A.prog->fixes.count; i++) {
        const fix *f = &A.prog->fixes.items[i];
        if (f->at.file != A.doc || f->at.line - 1 < start_line || f->at.line - 1 > end_line) continue;
        if (f->kind == FIX_USE_THIS) {
            use_this_action(f->param, out, &written);
            continue;
        }
        if (f->kind == FIX_UNWRAP) {
            unwrap_actions(f, out, &written);
            continue;
        }
        if (f->kind == FIX_REMOVE_LOCAL || f->kind == FIX_KEEP_CALL) {
            unused_local_action(f, out, &written);
            continue;
        }
        if (f->kind == FIX_UNUSED_LOCAL) continue; // Nothing to offer: the note says what to do
        if (f->kind == FIX_ASSIGN_FIRST) {
            assign_first_actions(f, out, &written);
            continue;
        }
        if (f->kind == FIX_ADD_VIRTUAL || f->kind == FIX_ADD_OVERRIDE) {
            replace_word_action(f, out, &written);
            continue;
        }
        const param *p = f->param;
        sb title = {0};
        sb text = {0};
        loc start = p ? mut_end(p) : f->at;
        loc end = start;
        bool preferred = true;
        const char *separator = "";
        const diagnostic *fixed = NULL; // The diagnostic it fixes, where the fix is at it
        switch (f->kind) {
        case FIX_ADD_MUT:
            sb_printf(&title, "Declare '" STR_FMT "' as mut", STR_ARG(p->name));
            sb_put(&text, "mut ");
            break;
        case FIX_REMOVE_MUT:
            sb_printf(&title, "Remove 'mut' from '" STR_FMT "'", STR_ARG(p->name));
            start = p->at;
            sb_put(&text, "");
            break;
        case FIX_USE_WITH: // A view's `verified Health health` is still the confirmed world's: `with verified Health`
            sb_printf(&text, "with %s" STR_FMT, p->verified_at.line > 0 ? "verified " : "", STR_ARG(p->type_name));
            sb_printf(&title, "Only require it: '%s'", text.data);
            start = p->at;
            end = (loc){p->name_at.line, p->name_at.col + p->name.len, p->name_at.file};
            break;
        case FIX_MUT_LOCAL:
            sb_printf(&title, "Declare '" STR_FMT "' as mut", STR_ARG(f->local->name));
            start = end = f->local->at;
            sb_put(&text, "mut ");
            break;
        case FIX_MUT_METHOD:
            if (f->method->at.file != A.doc) continue;
            sb_printf(&title, "Make '" STR_FMT "' mut", STR_ARG(f->method->name));
            start = end = f->method->return_type_qual_at;
            sb_put(&text, "mut ");
            break;
        case FIX_CREATE_FUNCTION: {
            const expr *call = f->call;
            bool known = true;
            for (int k = 0; k < call->args.count; k++) known &= call->args.items[k]->type.kind != TY_ERROR;
            if (!known) continue;
            sb_printf(&title, "Create function '" STR_FMT "'", STR_ARG(call->name));
            start = end = doc_end(&separator);
            sb_printf(&text, "%svoid " STR_FMT "(", separator, STR_ARG(call->name));
            VEC(char *) names = {0};
            for (int k = 0; k < call->args.count; k++) {
                sb name = {0};
                if (written_name(call, k).len > 0) sb_printf(&name, STR_FMT, STR_ARG(written_name(call, k))); // Foo(count: 3)
                else argument_name(call->args.items[k], &name);
                for (int j = 0; j < names.count; j++) {
                    if (strcmp(names.items[j], name.data) == 0) sb_printf(&name, "%d", k + 1); // Two of the same
                }
                vec_push(names, name.data);
                sb_printf(&text, "%s%s%s %s", k ? ", " : "", call->args.items[k]->is_out ? "out " : "",
                          type_name(call->args.items[k]->type), name.data);
            }
            sb_put(&text, ")\n{\n");
            for (int k = 0; k < call->args.count; k++) { // What it makes through `out`, so it's whole as it's made
                if (call->args.items[k]->is_out) sb_printf(&text, "    %s = default;\n", names.items[k]);
            }
            sb_put(&text, "}\n");
            preferred = false;
            break;
        }
        case FIX_CREATE_STRUCT:
        case FIX_CREATE_COMPONENT: {
            if (memchr(f->name.ptr, '.', (size_t)f->name.len)) continue; // Another namespace's
            const char *keyword = f->kind == FIX_CREATE_STRUCT ? "struct" : "component";
            sb_printf(&title, "Create %s '" STR_FMT "'", keyword, STR_ARG(f->name));
            start = end = doc_end(&separator);
            sb_printf(&text, "%s%s " STR_FMT "\n{\n}\n", separator, keyword, STR_ARG(f->name));
            preferred = false;
            break;
        }
        case FIX_LOCAL_WORD: { // `local` on a field of a declaration that's local already: up to the field's type
            const int t = token_at(f->at);
            if (t < 0) continue;
            sb_put(&title, "Remove 'local'");
            end = DOC->toks[t + 1].at;
            sb_put(&text, "");
            break;
        }
        case FIX_ADD_OUT: // An argument for an `out` parameter: the word before it
            sb_put(&title, "Add 'out'");
            start = end = f->at;
            sb_put(&text, "out ");
            fixed = diagnostic_at(f->at);
            break;
        case FIX_REMOVE_OUT: { // `out` where no `out` parameter takes it: the word, up to the argument
            const int t = token_at(f->at);
            if (t < 0) continue;
            sb_put(&title, "Remove 'out'");
            start = f->at;
            end = DOC->toks[t + 1].at;
            sb_put(&text, "");
            fixed = diagnostic_at(f->at);
            break;
        }
        case FIX_DISCARD_OUT: { // `out var rest` that nothing reads: `out _`, from its `var`, `mut` or type to its name
            const stmt *l = f->local;
            sb_printf(&title, "Leave '" STR_FMT "' out: 'out _'", STR_ARG(l->name));
            start = l->at;
            end = (loc){l->name_at.line, l->name_at.col + l->name.len, l->name_at.file};
            sb_put(&text, "_");
            fixed = diagnostic_at(f->at);
            break;
        }
        case FIX_ASSIGN_OUT: { // A return, or a function's end, that leaves an `out` parameter unassigned
            const bool at_end = loc_cmp(f->at, f->method->at) == 0;
            const loc where = at_end ? f->method->end : f->at; // Its closing brace, or the return
            const int t = where.file == A.doc ? token_at(where) : -1;
            if (t <= 0) continue;
            const char *value = filler(f->method, p);
            sb_printf(&title, "%s '" STR_FMT " = %s;'", is_shader(f->method) ? "Write" : "Assign", STR_ARG(p->name), value);
            fixed = diagnostic_at(f->at);
            // A return that's an if's or a loop's body with no braces has to stay one statement: in braces, with
            // the assignment before it
            const tok_kind before = DOC->toks[t - 1].kind;
            if (before != T_LBRACE && before != T_RBRACE && before != T_SEMI && before != T_COLON) {
                int last = t; // The ';' that ends it
                for (int depth = 0; last < DOC->tok_count; last++) {
                    const tok_kind k = DOC->toks[last].kind;
                    if (k == T_EOF || (k == T_SEMI && depth == 0)) break;
                    if (k == T_LPAREN || k == T_LBRACKET || k == T_LBRACE) depth++;
                    else if (k == T_RPAREN || k == T_RBRACKET || k == T_RBRACE) depth--;
                }
                if (at_end || last >= DOC->tok_count || DOC->toks[last].kind != T_SEMI) continue;
                sb_printf(&text, "{ " STR_FMT " = %s; ", STR_ARG(p->name), value);
                const loc after = token_end(&DOC->toks[last]);
                text_edits both = {0};
                const text_edit opens = {where, where, text.data};
                const text_edit closes = {after, after, " }"};
                vec_push(both, opens);
                vec_push(both, closes);
                quick_fix_edits(out, &written, title.data, true, fixed, &both);
                continue;
            }
            const str line = doc_line(where.line);
            const int indent = where.col - 1 < line.len ? where.col - 1 : line.len;
            bool first = true; // On its line: the assignment gets a line of its own
            for (int i = 0; i < indent; i++) first &= isspace((unsigned char)line.ptr[i]) != 0;
            start = end = where;
            if (first && at_end) { // A step in from the brace
                start = end = (loc){where.line, 1, A.doc};
                sb_printf(&text, "%.*s    " STR_FMT " = %s;\n", indent, line.ptr, STR_ARG(p->name), value);
            } else if (first) {
                sb_printf(&text, STR_FMT " = %s;\n%.*s", STR_ARG(p->name), value, indent, line.ptr);
            } else {
                sb_printf(&text, STR_FMT " = %s; ", STR_ARG(p->name), value);
            }
            break;
        }
        case FIX_USE_THIS: case FIX_UNWRAP: case FIX_UNUSED_LOCAL: case FIX_REMOVE_LOCAL: case FIX_KEEP_CALL:
        case FIX_ASSIGN_FIRST: case FIX_ADD_VIRTUAL: case FIX_ADD_OVERRIDE:
            continue; // Their own actions, above
        }
        quick_fix(out, &written, title.data, preferred, fixed, start, end, text.data ? text.data : "");
    }
    move_to_file_action(start_line, out, &written);
    jb_put(out, "]");
}

static const occurrence *occurrence_at(const loc at)
{
    for (int i = 0; i < A.occs.count; i++) {
        const occurrence *o = &A.occs.items[i];
        if (o->at.file == at.file && o->at.line == at.line && o->at.col <= at.col && at.col <= o->at.col + o->len) {
            return o;
        }
    }
    return NULL;
}

void analysis_hover(const int line, const int character, jbuf *out)
{
    const occurrence *o = occurrence_at(from_lsp(line, character));
    if (!o) {
        jb_put(out, "null");
        return;
    }
    sb text = {0};
    describe(o, &text);
    if (!text.data) {
        jb_put(out, "null");
        return;
    }
    jb_put(out, "{\"contents\":{\"kind\":\"markdown\",\"value\":");
    jb_string(out, text.data);
    jb_put(out, "},\"range\":");
    write_range(out, o->at, o->len);
    jb_put(out, "}");
}

// Built-ins defined in C, in the engine headers: their definition is there.
// Things the compiler itself generates (Time, Owner, Spawn) have none.
static bool write_c_definition(const occurrence *o, jbuf *out)
{
    switch (o->kind) {
    case OCC_TYPE: // A device record's; Pixel and Instance are the GPU's, with no C struct
        if (o->decl) return o->decl->kind == DECL_RECORD && o->decl->c_name && cdefs_find(o->decl->c_name, out);
        return strncmp(type_c_name(o->type), "tide_", 5) == 0
            && cdefs_find(type_c_name(o->type), out);
    case OCC_FUNCTION:
    case OCC_CONSTANT: {
        if (!o->c_name) return false;
        char name[128];
        snprintf(name, sizeof name, "%s", o->c_name);
        char *call = strchr(name, '('); // tide_identity_q() for quaternion.identity
        if (call) *call = '\0';
        return cdefs_find(name, out);
    }
    case OCC_OWNER:
        if (str_eq_c(o->owner, "Devices")) return cdefs_find("tide_devices", out);
        if (str_eq_c(o->owner, "Frame")) return false; // The game's own local state keeps it: no engine header does
        return cdefs_find_header(builtin_draw_owner(o->owner)                                         ? "draw.h"
                                 : str_eq_c(o->owner, "File") || str_eq_c(o->owner, "Directory")
                                       || str_eq_c(o->owner, "Path") || str_eq_c(o->owner, "Application")
                                       || str_eq_c(o->owner, "Storage") || str_eq_c(o->owner, "Web") ? "io.h"
                                 : str_eq_c(o->owner, "GUI") || str_eq_c(o->owner, "GUILayout")
                                       || str_eq_c(o->owner, "Screen") || str_eq_c(o->owner, "Cursor") ? "gui.h"
                                                                                                    : "math.h",
                                 out);
    case OCC_FIELD:
        return o->decl && o->decl->kind == DECL_RECORD && o->decl->c_name
            && cdefs_find_member(o->decl->c_name, str_to_cstr(o->name), out);
    case OCC_MEMBER: {
        // Real struct members only: x, r, value, c0; not swizzles like xz.
        const type t = o->object_type;
        const bool vector_component = type_dim(t) >= 2 && o->name.len == 1;
        const bool other = (t.kind == TY_COLOR && o->name.len == 1) || t.kind == TY_QUATERNION || t.kind == TY_RECT
                        || matrix_dim(t) > 0;
        return (vector_component || other) && cdefs_find_member(type_c_name(t), str_to_cstr(o->name), out);
    }
    default:
        return false;
    }
}

void analysis_definition(const char *uri, const int line, const int character, jbuf *out)
{
    const occurrence *o = occurrence_at(from_lsp(line, character));
    loc target = {0, 0, 0};
    int len = 0;
    if (o) {
        if (o->kind == OCC_FIELD && o->decl && !o->decl->builtin) {
            target = o->field->at;
            len = o->field->name.len;
        } else if (o->kind == OCC_REPLACE) { // `override`: what it replaces; `virtual`: what replaces it
            const decl *d = o->decl->replaces ? o->decl->replaces : o->decl->replaced_by;
            if (d) {
                target = d->at;
                len = d->name.len;
            }
        } else if ((o->kind == OCC_TYPE || o->kind == OCC_SYSTEM || o->kind == OCC_CONST
                    || (is_routine(o->decl) && o->kind != OCC_THIS))
                   && o->decl && !o->decl->builtin) {
            // An override's own name is the name of what it replaces: from there, to that one
            const decl *d = o->declaration && o->decl->replaces ? o->decl->replaces : o->decl;
            target = d->at;
            len = d->name.len;
        } else if (o->kind == OCC_PARAM) {
            target = o->param->name_at;
            len = o->param->name.len;
        } else if (o->kind == OCC_LOCAL) {
            target = o->local->name_at;
            len = o->local->name.len;
        } else if (o->kind == OCC_ENUM_MEMBER && o->decl && !o->decl->builtin) { // Options in Page.Options
            for (int i = 0; i < o->decl->members.count && target.line == 0; i++) {
                const enum_member *m = &o->decl->members.items[i];
                if (!str_eq(m->name, o->name)) continue;
                target = m->at;
                len = m->name.len;
            }
        } else if (o->kind == OCC_NAMESPACE) {
            // The first file that declares it (or a namespace inside it)
            for (int i = 0; i < A.occs.count && target.line == 0; i++) {
                const occurrence *d = &A.occs.items[i];
                if (d->kind == OCC_NAMESPACE && d->declaration && str_eq(d->name, o->name)) {
                    target = d->at;
                    len = d->len;
                }
            }
        }
    }
    if (target.line == 0) {
        if (!o || !write_c_definition(o, out)) jb_put(out, "null");
        return;
    }
    (void)uri;
    jb_put(out, "{\"uri\":");
    jb_string(out, A.files[target.file].uri); // Maybe another file of the game
    jb_put(out, ",\"range\":");
    write_range(out, target, len);
    jb_put(out, "}");
}

// ---------------------------------------------------------------------------
// Document symbols: the outline

enum { SYMBOL_CLASS = 5, SYMBOL_METHOD = 6, SYMBOL_PROPERTY = 7, SYMBOL_FIELD = 8, SYMBOL_ENUM = 10, SYMBOL_INTERFACE = 11,
       SYMBOL_FUNCTION = 12, SYMBOL_CONSTANT = 14, SYMBOL_OBJECT = 19, SYMBOL_ENUM_MEMBER = 22, SYMBOL_STRUCT = 23,
       SYMBOL_EVENT = 24 };

static int symbol_kind(const decl *d)
{
    return d->kind == DECL_COMPONENT || d->kind == DECL_STRUCT ? SYMBOL_STRUCT
         : d->kind == DECL_SINGLETON                            ? SYMBOL_CLASS
         : d->kind == DECL_INPUT                                ? SYMBOL_INTERFACE
         : d->kind == DECL_EVENT                                ? SYMBOL_EVENT
         : d->kind == DECL_ENUM                                 ? SYMBOL_ENUM
         : d->kind == DECL_CONST || d->kind == DECL_ASSET       ? SYMBOL_CONSTANT
                                                                : SYMBOL_FUNCTION;
}

void analysis_symbols(jbuf *out)
{
    jb_put(out, "[");
    int written = 0;
    for (int i = 0; i < A.written.count; i++) {
        const decl *d = A.written.items[i];
        if (d->builtin || d->at.file != A.doc) continue; // This document's declarations
        const int kind = symbol_kind(d);
        const loc start = {d->at.line, 1, d->at.file};
        const loc end = d->end.line > 0 ? d->end : d->at;
        if (written++) jb_put(out, ",");
        jb_put(out, "{\"name\":");
        jb_string_n(out, d->name.ptr, (size_t)d->name.len);
        jb_printf(out, ",\"detail\":\"%s\",\"kind\":%d,\"range\":{\"start\":", decl_what(d), kind);
        write_position(out, start);
        jb_put(out, ",\"end\":");
        write_position(out, (loc){end.line, end.col + 1, end.file});
        jb_put(out, "},\"selectionRange\":");
        write_range(out, d->at, d->name.len);
        jb_put(out, ",\"children\":[");
        for (int m = 0; m < d->members.count; m++) {
            const enum_member *member = &d->members.items[m];
            if (m) jb_put(out, ",");
            jb_put(out, "{\"name\":");
            jb_string_n(out, member->name.ptr, (size_t)member->name.len);
            jb_printf(out, ",\"detail\":\"%lld\",\"kind\":%d,\"range\":", (long long)member->number, SYMBOL_ENUM_MEMBER);
            write_range(out, member->at, member->name.len);
            jb_put(out, ",\"selectionRange\":");
            write_range(out, member->at, member->name.len);
            jb_put(out, "}");
        }
        bool first_child = d->members.count == 0;
        for (int f = 0; f < d->fields.count + local_field_count(d); f++) {
            const field *fl = field_of(d, f);
            if (fl->hidden) continue;
            if (!first_child) jb_put(out, ",");
            first_child = false;
            jb_put(out, "{\"name\":");
            jb_string_n(out, fl->name.ptr, (size_t)fl->name.len);
            jb_put(out, ",\"detail\":");
            sb detail = {0};
            sb_printf(&detail, "%s" STR_FMT, fl->is_local ? "local " : "", STR_ARG(fl->type_name));
            jb_string(out, detail.data);
            jb_printf(out, ",\"kind\":%d,\"range\":", SYMBOL_FIELD);
            write_range(out, fl->at, fl->name.len);
            jb_put(out, ",\"selectionRange\":");
            write_range(out, fl->at, fl->name.len);
            jb_put(out, "}");
        }
        for (int k = 0; k < d->methods.count; k++) {
            const decl *m = method_written(d->methods.items[k]);
            sb detail = {0};
            format_routine(m, &detail);
            if (!first_child) jb_put(out, ",");
            first_child = false;
            jb_put(out, "{\"name\":");
            jb_string_n(out, m->name.ptr, (size_t)m->name.len);
            jb_put(out, ",\"detail\":");
            jb_string(out, detail.data);
            jb_printf(out, ",\"kind\":%d,\"range\":{\"start\":", SYMBOL_METHOD);
            write_position(out, m->at);
            jb_put(out, ",\"end\":");
            write_position(out, (loc){m->end.line, m->end.col + 1, m->end.file});
            jb_put(out, "},\"selectionRange\":");
            write_range(out, m->at, m->name.len);
            jb_put(out, "}");
        }
        // The input's Sample and Sanitize
        for (int k = 0; d->kind == DECL_INPUT && k < 2; k++) {
            const stmt *body = k == 0 ? d->body : d->sanitize;
            if (!body) continue;
            const loc at = k == 0 ? d->body_at : d->sanitize_at;
            sb detail = {0};
            sb_put(&detail, k == 0 ? "Sample(" : "Sanitize(");
            for (int i = 0; k == 0 && i < d->params.count; i++) {
                if (i) sb_put(&detail, ", ");
                format_param(&d->params.items[i], &detail);
            }
            sb_put(&detail, ")");
            if (!first_child) jb_put(out, ",");
            first_child = false;
            jb_printf(out, "{\"name\":\"%s\",\"detail\":", k == 0 ? "Sample" : "Sanitize");
            jb_string(out, detail.data);
            jb_printf(out, ",\"kind\":%d,\"range\":{\"start\":", SYMBOL_METHOD);
            write_position(out, at);
            jb_put(out, ",\"end\":");
            write_position(out, (loc){body->end.line, body->end.col + 1, body->end.file});
            jb_put(out, "},\"selectionRange\":");
            write_range(out, at, k == 0 ? 6 : 8);
            jb_put(out, "}");
        }
        jb_put(out, "]}");
    }
    // settings { ... }, with what it sets
    for (int b = 0; b < A.prog->settings.count; b++) {
        const decl *block = A.prog->settings.items[b];
        if (block->at.file != A.doc) continue;
        const loc end = block->end.line > 0 ? block->end : block->at;
        if (written++) jb_put(out, ",");
        jb_printf(out, "{\"name\":\"settings\",\"detail\":\"the engine's\",\"kind\":%d,\"range\":{\"start\":", SYMBOL_OBJECT);
        write_position(out, block->at);
        jb_put(out, ",\"end\":");
        write_position(out, (loc){end.line, end.col + 1, end.file});
        jb_put(out, "},\"selectionRange\":");
        write_range(out, block->at, 8);
        jb_put(out, ",\"children\":[");
        for (int f = 0; f < block->fields.count; f++) {
            const field *fl = &block->fields.items[f];
            if (f) jb_put(out, ",");
            jb_put(out, "{\"name\":");
            jb_string_n(out, fl->name.ptr, (size_t)fl->name.len);
            jb_printf(out, ",\"kind\":%d,\"range\":", SYMBOL_PROPERTY);
            write_range(out, fl->at, fl->name.len);
            jb_put(out, ",\"selectionRange\":");
            write_range(out, fl->at, fl->name.len);
            jb_put(out, "}");
        }
        jb_put(out, "]}");
    }
    jb_put(out, "]");
}

// ---------------------------------------------------------------------------
// Type definitions and implementations

static void write_location(jbuf *out, const loc at, const int len)
{
    jb_put(out, "{\"uri\":");
    jb_string(out, A.files[at.file].uri);
    jb_put(out, ",\"range\":");
    write_range(out, at, len);
    jb_put(out, "}");
}

void analysis_type_definition(const int line, const int character, jbuf *out)
{
    const occurrence *o = occurrence_at(from_lsp(line, character));
    type t = {TY_ERROR, NULL};
    if (o) {
        switch (o->kind) {
        case OCC_TYPE: t = o->type; break;
        case OCC_PARAM: t = o->param->type; break;
        case OCC_LOCAL: t = o->local->type; break;
        case OCC_FIELD: t = o->field->type; break;
        case OCC_FUNCTION: case OCC_METHOD: t = is_routine(o->decl) ? o->decl->return_type : o->type; break;
        case OCC_CONST: t = o->decl->return_type; break;
        default: t = o->type; break;
        }
    }
    // What a list, grid or T? holds, and a failable call's value
    while ((t.kind == TY_LIST || t.kind == TY_GRID || t.kind == TY_OPTIONAL || t.kind == TY_FAILABLE || t.kind == TY_MAP
            || t.kind == TY_SET) && t.decl) {
        t = t.decl->fields.items[t.kind == TY_MAP ? 1 : 0].type; // A map's value
    }
    const decl *d = t.kind == TY_COMPONENT || t.kind == TY_SINGLETON || t.kind == TY_INPUT || t.kind == TY_STRUCT
                         || t.kind == TY_EVENT || t.kind == TY_ENUM || t.kind == TY_RECORD
                     ? t.decl
                     : NULL;
    if (o && o->kind == OCC_TYPE && o->decl) d = o->decl; // A type: its own declaration
    if (d && d->query_row) jb_put(out, "null"); // There is no stored or named row type
    else if (d && !d->builtin) write_location(out, d->at, d->name.len);
    else if (!(d && d->kind == DECL_RECORD && d->c_name && cdefs_find(d->c_name, out))) jb_put(out, "null"); // Devices, in C
}

// An event's handlers, and the input's Sample and Sanitize: the code that
// gives it its behavior. A virtual declaration's is its override.
void analysis_implementation(const int line, const int character, jbuf *out)
{
    const occurrence *o = occurrence_at(from_lsp(line, character));
    const decl *d = NULL;
    if (o && o->decl && (o->kind == OCC_SYSTEM || o->kind == OCC_REPLACE || is_routine(o->decl))
        && (o->decl->is_virtual || o->decl->is_override)) {
        const decl *built = o->decl->replaced_by ? o->decl->replaced_by : o->decl->replaces ? o->decl : NULL;
        jb_put(out, "[");
        if (built) write_location(out, built->at, built->name.len);
        jb_put(out, "]");
        return;
    }
    if (o && o->kind == OCC_TYPE) d = o->decl;
    else if (o && o->kind == OCC_PARAM) d = o->param->type.decl; // A handler's event
    if (!d || (d->kind != DECL_EVENT && d->kind != DECL_INPUT)) {
        jb_put(out, "null");
        return;
    }
    jb_put(out, "[");
    int written = 0;
    // The program's handlers rather than the event's: the checker only lists
    // those once the game has no errors, in the order they run.
    for (int i = 0; d->kind == DECL_EVENT && i < A.prog->handlers.count; i++) {
        const decl *h = A.prog->handlers.items[i];
        if (h->event != d) continue;
        if (written++) jb_put(out, ",");
        write_location(out, h->at, h->name.len);
    }
    if (d->kind == DECL_INPUT && d->body) {
        if (written++) jb_put(out, ",");
        write_location(out, d->body_at, 6);
    }
    if (d->kind == DECL_INPUT && d->sanitize) {
        if (written++) jb_put(out, ",");
        write_location(out, d->sanitize_at, 8);
    }
    jb_put(out, "]");
}

// ---------------------------------------------------------------------------
// Call hierarchy: who calls the game's functions and methods, and what they
// call. A view that names a frag or vert at Draw.Mesh calls it, as far as the
// hierarchy goes: the GPU runs it for what the view draws.

static bool block_contains(const stmt *block, loc at);

// Code that calls: a system, view or handler, a function or method, a frag or
// vert, or the input's Sample or Sanitize, whose `body` holds the calls.
typedef struct caller {
    const decl *decl;
    const stmt *body;
    str name;
    loc name_at;
} caller;

// The code whose body holds `at`; its decl is NULL where there's none.
static caller caller_at(const loc at)
{
    for (int i = 0; i < A.written.count; i++) {
        const decl *d = A.written.items[i];
        if (d->builtin) continue;
        for (int k = 0; k < d->methods.count; k++) {
            const decl *m = method_written(d->methods.items[k]);
            if (block_contains(m->body, at)) return (caller){m, m->body, m->name, m->at};
        }
        if (d->kind == DECL_INPUT && block_contains(d->body, at)) return (caller){d, d->body, str_from("Sample"), d->body_at};
        if (d->kind == DECL_INPUT && block_contains(d->sanitize, at)) {
            return (caller){d, d->sanitize, str_from("Sanitize"), d->sanitize_at};
        }
        if ((d->kind == DECL_SYSTEM || is_routine(d) || is_shader(d)) && block_contains(d->body, at)) {
            return (caller){d, d->body, d->name, d->at};
        }
    }
    return (caller){NULL, NULL, {"", 0}, {0, 0, 0}};
}

// A CallHierarchyItem: its whole code, and its name, which says which it is.
static void write_call_item(jbuf *out, const caller *c)
{
    const decl *d = c->decl;
    sb detail = {0};
    if (is_routine(d)) format_routine(d, &detail);
    else if (d->kind == DECL_SYSTEM || is_shader(d)) format_header(d, &detail);
    else sb_printf(&detail, "input " STR_FMT, STR_ARG(d->name));
    const int kind = d->kind == DECL_METHOD || d->kind == DECL_INPUT ? SYMBOL_METHOD : SYMBOL_FUNCTION;
    const loc end = c->body ? c->body->end : d->end;
    jb_put(out, "{\"name\":");
    jb_string_n(out, c->name.ptr, (size_t)c->name.len);
    jb_printf(out, ",\"kind\":%d,\"detail\":", kind);
    jb_string(out, detail.data);
    jb_put(out, ",\"uri\":");
    jb_string(out, A.files[c->name_at.file].uri);
    jb_put(out, ",\"range\":");
    write_edit_range(out, (loc){c->name_at.line, 1, c->name_at.file}, (loc){end.line, end.col + 1, end.file});
    jb_put(out, ",\"selectionRange\":");
    write_range(out, c->name_at, c->name.len);
    jb_put(out, "}");
}

// The code named at the cursor, which the call hierarchy is about: a function
// or method where it's declared or called, a system where it's declared, or a
// frag or vert where it's declared or a Draw.Mesh names it.
static caller hierarchy_target(const int line, const int character)
{
    const occurrence *o = occurrence_at(from_lsp(line, character));
    if (o && (o->kind == OCC_FUNCTION || o->kind == OCC_METHOD) && is_routine(o->decl) && !o->decl->is_extern) {
        return (caller){o->decl, o->decl->body, o->decl->name, o->decl->at};
    }
    if (o && o->kind == OCC_METHOD && o->decl && o->decl->kind == DECL_INPUT) { // Sample, Sanitize
        return (caller){o->decl, str_eq_c(o->name, "Sample") ? o->decl->body : o->decl->sanitize, o->name, o->at};
    }
    if (o && o->kind == OCC_SYSTEM && o->decl) return (caller){o->decl, o->decl->body, o->decl->name, o->decl->at};
    if (o && (o->kind == OCC_FUNCTION || o->kind == OCC_METHOD) && is_routine(o->decl)) { // An extern function: no body
        return (caller){o->decl, NULL, o->decl->name, o->decl->at};
    }
    return (caller){NULL, NULL, {"", 0}, {0, 0, 0}};
}

void analysis_prepare_call_hierarchy(const int line, const int character, jbuf *out)
{
    const caller target = hierarchy_target(line, character);
    if (!target.decl) {
        jb_put(out, "null");
        return;
    }
    jb_put(out, "[");
    write_call_item(out, &target);
    jb_put(out, "]");
}

// A call of a game's function or method, by name or by operator, or a frag's
// or vert's name at a Draw.Mesh.
static bool is_call(const occurrence *o)
{
    if (o->kind == OCC_SYSTEM) return !o->declaration && is_shader(o->decl);
    return (o->kind == OCC_FUNCTION || o->kind == OCC_METHOD) && !o->declaration && is_routine(o->decl);
}

// Who calls the item at the cursor: each caller once, with where it does.
void analysis_incoming_calls(const int line, const int character, jbuf *out)
{
    const caller target = hierarchy_target(line, character);
    jb_put(out, "[");
    int written = 0;
    VEC(const stmt *) done = {0};
    for (int i = 0; target.decl && i < A.occs.count; i++) {
        const occurrence *o = &A.occs.items[i];
        if (!is_call(o) || o->decl != target.decl) continue;
        const caller from = caller_at(o->at);
        bool seen = !from.decl;
        for (int k = 0; k < done.count && !seen; k++) seen = done.items[k] == from.body;
        if (seen) continue;
        vec_push(done, from.body);
        if (written++) jb_put(out, ",");
        jb_put(out, "{\"from\":");
        write_call_item(out, &from);
        jb_put(out, ",\"fromRanges\":[");
        for (int k = i, ranges = 0; k < A.occs.count; k++) {
            const occurrence *call = &A.occs.items[k];
            if (!is_call(call) || call->decl != target.decl || caller_at(call->at).body != from.body) continue;
            if (ranges++) jb_put(out, ",");
            write_range(out, call->at, call->len);
        }
        jb_put(out, "]}");
    }
    jb_put(out, "]");
}

// What the item at the cursor calls: each function or method once, with
// where it does.
void analysis_outgoing_calls(const int line, const int character, jbuf *out)
{
    const caller source = hierarchy_target(line, character);
    jb_put(out, "[");
    int written = 0;
    VEC(const decl *) done = {0};
    for (int i = 0; source.body && i < A.occs.count; i++) {
        const occurrence *o = &A.occs.items[i];
        if (!is_call(o) || caller_at(o->at).body != source.body) continue;
        bool seen = false;
        for (int k = 0; k < done.count && !seen; k++) seen = done.items[k] == o->decl;
        if (seen) continue;
        vec_push(done, o->decl);
        const caller to = {o->decl, o->decl->body, o->decl->name, o->decl->at};
        if (written++) jb_put(out, ",");
        jb_put(out, "{\"to\":");
        write_call_item(out, &to);
        jb_put(out, ",\"fromRanges\":[");
        for (int k = i, ranges = 0; k < A.occs.count; k++) {
            const occurrence *call = &A.occs.items[k];
            if (!is_call(call) || call->decl != o->decl || caller_at(call->at).body != source.body) continue;
            if (ranges++) jb_put(out, ",");
            write_range(out, call->at, call->len);
        }
        jb_put(out, "]}");
    }
    jb_put(out, "]");
}

// ---------------------------------------------------------------------------
// Selection ranges: what holds the cursor, from its word out to its declaration

typedef struct span {
    loc start;
    loc end; // Just past it
} span;

typedef VEC(span) span_list;

static loc stmt_start(const stmt *s);
static loc stmt_end(const stmt *s);

static bool around(const loc at, const loc start, const loc end)
{
    return start.file == at.file && loc_cmp(start, at) <= 0 && loc_cmp(at, end) <= 0;
}

// Adds a span around the cursor, inside the last one: once, however many
// things it's the span of.
static void add_span(span_list *spans, const loc at, const loc start, const loc end)
{
    if (!around(at, start, end)) return;
    if (spans->count > 0) {
        const span *outer = &spans->items[spans->count - 1];
        if (loc_cmp(start, outer->start) < 0 || loc_cmp(outer->end, end) < 0) return;
        if (loc_cmp(outer->start, start) == 0 && loc_cmp(outer->end, end) == 0) return;
    }
    const span s = {start, end};
    vec_push(*spans, s);
}

static void select_in_stmt(const stmt *s, loc at, span_list *spans);

static void select_in_expr(const expr *e, const loc at, span_list *spans)
{
    if (!e || !around(at, expr_start(e), expr_end(e))) return;
    add_span(spans, at, expr_start(e), expr_end(e));
    select_in_expr(e->object, at, spans);
    select_in_expr(e->lhs, at, spans);
    select_in_expr(e->rhs, at, spans);
    select_in_expr(e->cond, at, spans);
    for (int i = 0; i < written_count(e); i++) select_in_expr(written_arg(e, i), at, spans);
    for (int i = 0; e->kind == E_LITERAL && i < e->inits.count; i++) select_in_expr(e->inits.items[i].value, at, spans);
    if (e->kind == E_CALL || e->kind == E_METHOD) select_in_stmt(e->block, at, spans);
}

static void select_in_stmt(const stmt *s, const loc at, span_list *spans)
{
    if (!s || !around(at, stmt_start(s), stmt_end(s))) return;
    add_span(spans, at, stmt_start(s), stmt_end(s));
    for (int i = 0; s->kind == S_BLOCK && i < s->stmts.count; i++) select_in_stmt(s->stmts.items[i], at, spans);
    select_in_stmt(s->init, at, spans);
    select_in_expr(s->cond, at, spans);
    select_in_stmt(s->step, at, spans);
    select_in_expr(s->target, at, spans);
    select_in_expr(s->value, at, spans);
    select_in_expr(s->by, at, spans);
    select_in_expr(s->offset, at, spans);
    select_in_stmt(s->then_stmt, at, spans);
    select_in_stmt(s->else_stmt, at, spans);
    for (int i = 0; s->kind == S_SWITCH && i < s->cases.count; i++) {
        const switch_case *section = &s->cases.items[i];
        for (int k = 0; k < section->labels.count; k++) select_in_expr(section->labels.items[k], at, spans);
        for (int k = 0; k < section->body.count; k++) select_in_stmt(section->body.items[k], at, spans);
    }
}

// A declaration's lines, from its attributes to its closing brace or ';'.
static void select_decl(const decl *d, const stmt *body, const loc at, span_list *spans)
{
    int first = d->at.line;
    for (int a = 0; a < d->attributes.count; a++) {
        if (d->attributes.items[a].at.line < first) first = d->attributes.items[a].at.line;
    }
    const loc end = body ? body->end : d->end.line > 0 ? d->end : d->at;
    add_span(spans, at, (loc){first, 1, d->at.file}, (loc){end.line, end.col + 1, end.file});
}

// The spans around `at`, outermost first.
static void selection_spans(const loc at, span_list *spans)
{
    for (int i = 0; i < A.written.count; i++) {
        const decl *d = A.written.items[i];
        if (d->builtin || d->at.file != at.file) continue;
        const int before = spans->count;
        select_decl(d, d->kind == DECL_SYSTEM || is_routine(d) || is_shader(d) ? d->body : NULL, at, spans);
        if (spans->count == before) continue;
        for (int k = 0; k < d->methods.count; k++) {
            const decl *m = method_written(d->methods.items[k]);
            select_decl(m, m->body, at, spans);
            select_in_stmt(m->body, at, spans);
        }
        if (d->kind == DECL_INPUT) { // Sample and Sanitize, from their names
            for (int k = 0; k < 2; k++) {
                const stmt *body = k == 0 ? d->body : d->sanitize;
                if (!body) continue;
                const loc end = {body->end.line, body->end.col + 1, d->at.file};
                add_span(spans, at, k == 0 ? d->body_at : d->sanitize_at, end);
                select_in_stmt(body, at, spans);
            }
        } else {
            select_in_stmt(d->body, at, spans);
        }
        select_in_expr(d->value, at, spans); // A constant's
        for (int f = 0; f < d->fields.count + local_field_count(d); f++) select_in_expr(field_of(d, f)->default_value, at, spans);
        break;
    }
    // The word at the cursor
    const afile *f = &A.files[at.file];
    for (int i = 0; i < f->tok_count; i++) {
        const token *t = &f->toks[i];
        if (t->at.line == at.line && t->at.col <= at.col && at.col <= t->at.col + token_len(t)) {
            add_span(spans, at, t->at, token_end(t));
            break;
        }
    }
}

void analysis_selection_ranges(const json *positions, jbuf *out)
{
    jb_put(out, "[");
    for (int p = 0; positions && positions->kind == JSON_ARRAY && p < positions->count; p++) {
        if (p) jb_put(out, ",");
        const loc at = from_lsp(json_int(json_get(positions->items[p], "line"), 0),
                                json_int(json_get(positions->items[p], "character"), 0));
        span_list spans = {0};
        selection_spans(at, &spans);
        if (spans.count == 0) add_span(&spans, at, at, at); // Each position gets one
        // Innermost first, each with the one around it as its parent
        for (int i = spans.count - 1; i >= 0; i--) {
            jb_put(out, "{\"range\":");
            write_edit_range(out, spans.items[i].start, spans.items[i].end);
            if (i > 0) jb_put(out, ",\"parent\":");
        }
        for (int i = 0; i < spans.count; i++) jb_put(out, "}");
    }
    jb_put(out, "]");
}

// ---------------------------------------------------------------------------
// Folding: blocks and literals over several lines, and runs of comment lines

static void folding_range(jbuf *out, int *written, const int first, const int last, const char *kind)
{
    if (last <= first) return;
    if ((*written)++) jb_put(out, ",");
    jb_printf(out, "{\"startLine\":%d,\"endLine\":%d%s%s%s}", first - 1, last - 1, kind ? ",\"kind\":\"" : "",
              kind ? kind : "", kind ? "\"" : "");
}

static const char *token_start(const token *t);
static int line_of(const char *p);
static const unit *doc_unit(void);

// At the start of a line no code is read from (see lex_all_spans), the start
// of the first line after it that has code, or `limit`; anywhere else `p`.
static const char *past_inactive(const char *p, const char *limit)
{
    if (p != DOC->src.text && p[-1] != '\n') return p;
    for (int line = line_of(p); line <= DOC->lines.count && DOC->inactive[line]; line++) {
        p = line < DOC->lines.count ? DOC->lines.items[line] : DOC->src.text + DOC->src.len;
    }
    return p < limit ? p : limit;
}

void analysis_folding_ranges(jbuf *out)
{
    jb_put(out, "[");
    int written = 0;
    // A block folds from the line that introduces it, even when its `{` is on
    // a line of its own below, and keeps its `}` in sight. So do arguments,
    // parameters and a list's elements over several lines, keeping their `)`
    // or `]` in sight when it starts a line.
    int *open = arena_alloc(sizeof(int) * ((size_t)DOC->tok_count + 1));
    int depth = 0;
    for (int i = 0; i < DOC->tok_count; i++) {
        const token *t = &DOC->toks[i];
        if (t->kind == T_LBRACE || t->kind == T_LPAREN || t->kind == T_LBRACKET) {
            open[depth++] = i;
        } else if ((t->kind == T_RBRACE || t->kind == T_RPAREN || t->kind == T_RBRACKET) && depth > 0) {
            const int o = open[--depth];
            int first = DOC->toks[o].at.line;
            if (t->kind != T_RBRACE) {
                const bool starts_line = DOC->toks[i - 1].at.line != t->at.line;
                folding_range(out, &written, first, starts_line ? t->at.line - 1 : t->at.line, NULL);
                continue;
            }
            if (o > 0 && DOC->toks[o - 1].at.line == first - 1 && DOC->toks[o - 1].kind != T_RBRACE
                && DOC->toks[o - 1].kind != T_SEMI) {
                first--; // component Body\n{
            }
            folding_range(out, &written, first, t->at.line - 1, NULL);
        }
    }
    // Comment lines one after another, three or more, and comments in /* */
    // over several lines: between tokens, where they are.
    for (int line = 1; line <= DOC->lines.count;) {
        int end = line;
        while (end <= DOC->lines.count && comment_line(end) && !DOC->inactive[end]) end++;
        if (end - line >= 3) folding_range(out, &written, line, end - 1, "comment");
        line = end > line ? end : line + 1;
    }
    // Each branch of an #if, from its directive to the next one of that #if.
    int *branch = arena_alloc(sizeof(int) * ((size_t)DOC->lines.count + 1)); // Open branches' first lines
    int branches = 0;
    for (int line = 1; line <= DOC->lines.count; line++) {
        if (DOC->inactive[line] != LINE_DIRECTIVE) continue;
        str text = doc_line(line);
        while (text.len > 0 && (text.ptr[0] == ' ' || text.ptr[0] == '\t' || text.ptr[0] == '#')) {
            text.ptr++;
            text.len--;
        }
        const bool opens = text.len >= 2 && memcmp(text.ptr, "if", 2) == 0 && (text.len == 2 || !isalnum((unsigned char)text.ptr[2]));
        const bool ends = text.len >= 5 && memcmp(text.ptr, "endif", 5) == 0;
        if (!opens && branches > 0) folding_range(out, &written, branch[--branches], line - 1, "region");
        if (!ends) branch[branches++] = line;
    }
    const char *p = DOC->src.text;
    for (int i = 0; i <= DOC->tok_count; i++) {
        const char *next = i < DOC->tok_count ? token_start(&DOC->toks[i]) : DOC->src.text + DOC->src.len;
        for (; p + 1 < next; p++) {
            p = past_inactive(p, next);
            if (p + 1 >= next) break;
            if (p[0] == '/' && p[1] == '/') {
                while (p < next && *p != '\n') p++;
            } else if (p[0] == '/' && p[1] == '*') {
                const char *start = p;
                p += 2;
                while (p + 1 < next && !(p[0] == '*' && p[1] == '/')) p++;
                folding_range(out, &written, line_of(start), line_of(p), "comment");
            }
        }
        if (i < DOC->tok_count) p = next + token_len(&DOC->toks[i]);
    }
    // using lines one after another
    const unit *u = doc_unit();
    for (int k = 0; u && k < u->using_at.count;) {
        const loc *at = u->using_at.items;
        int last = k;
        while (last + 1 < u->using_at.count && at[last + 1].line == at[last].line + 1) last++;
        folding_range(out, &written, at[k].line, at[last].line, "imports");
        k = last + 1;
    }
    jb_put(out, "]");
}

// ---------------------------------------------------------------------------
// Inlay hints: the type a `var` gets, and parameter names at literal arguments

static bool is_literal(const expr *e)
{
    if (e->kind == E_UNARY && e->op == T_MINUS) e = e->lhs;
    return e->kind == E_INT || e->kind == E_FLOAT || e->kind == E_BOOL || e->kind == E_STRING || e->kind == E_DEFAULT;
}

// Where an expression starts: its leftmost token. An argument written with
// `out` starts at the word.
static loc expr_start(const expr *e)
{
    if (e->is_out && e->out_at.line > 0) return e->out_at;
    for (;;) {
        if ((e->kind == E_MEMBER || e->kind == E_METHOD || e->kind == E_INDEX) && e->object) e = e->object;
        else if (e->kind == E_BINARY || e->kind == E_COALESCE || e->kind == E_IS || e->kind == E_DEFAULTED) e = e->lhs;
        else if (e->kind == E_CONDITIONAL) e = e->cond;
        else break;
    }
    return e->kind == E_LITERAL && e->qual_at.line ? e->qual_at : e->at;
}

// Where a statement starts: an assignment's and a call's `at` is past it.
static loc stmt_start(const stmt *s)
{
    if (s->kind == S_EXPR) return expr_start(s->value);
    if (s->kind == S_ASSIGN && s->target) {
        const loc target = expr_start(s->target);
        return loc_cmp(target, s->at) < 0 ? target : s->at; // i++, or ++i
    }
    return s->at;
}

// The token at or just after `at` in its file.
static int token_from(const loc at)
{
    if (at.file < 0 || at.file >= A.file_count) return -1;
    const afile *f = &A.files[at.file];
    int lo = 0;
    int hi = f->tok_count;
    while (lo < hi) {
        const int mid = (lo + hi) / 2;
        if (loc_cmp(f->toks[mid].at, at) < 0) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

static loc token_end(const token *t);

// The bracket that closes the one at token `open` of the expression's file:
// its index, or the last token's where a syntax error left it open.
static int closing(const loc at, const int open)
{
    const afile *f = &A.files[at.file];
    int depth = 0;
    for (int i = open; i < f->tok_count; i++) {
        const tok_kind k = f->toks[i].kind;
        if (k == T_LPAREN || k == T_LBRACKET || k == T_LBRACE) depth++;
        else if ((k == T_RPAREN || k == T_RBRACKET || k == T_RBRACE) && --depth == 0) return i;
    }
    return f->tok_count - 1;
}

static loc stmt_end(const stmt *s);

// Just past an expression's last token.
static loc expr_end(const expr *e)
{
    const afile *f = &A.files[e->at.file];
    const int t = token_at(e->at);
    switch (e->kind) {
    case E_NAME:
        if (e->out_local && e->out_local->discard) break; // `out _`: its name is generated code's, and its token the `_`
        return (loc){e->at.line, e->at.col + e->name.len, e->at.file};
    case E_MEMBER: return (loc){e->at.line, e->at.col + e->member.len, e->at.file};
    case E_BINARY: case E_COALESCE: case E_CONDITIONAL: return expr_end(e->rhs);
    case E_UNARY: case E_TRY: case E_AWAIT: return expr_end(e->lhs);
    case E_IS: { // x is int score: to the name, or to the type
        const stmt *b = e->binding;
        if (b) return (loc){b->name_at.line, b->name_at.col + b->name.len, e->at.file};
        return (loc){e->pattern_at.line, e->pattern_at.col + last_part(e->pattern).len, e->at.file};
    }
    case E_CALL: case E_METHOD: {
        if (e->block) return stmt_end(e->block); // Its block, after the call
        int open = t + 1;
        if (t >= 0 && e->type_arg.len > 0) { // world.Get<Health>(e): its '(' is past the type in <>
            while (open < f->tok_count && f->toks[open].kind != T_LPAREN) open++;
        }
        if (t < 0 || open >= f->tok_count || f->toks[open].kind != T_LPAREN) {
            return e->args.count > 0 ? expr_end(e->args.items[e->args.count - 1]) : e->at; // cells[x, y]'s position
        }
        return token_end(&f->toks[closing(e->at, open)]);
    }
    case E_LITERAL: // Body { ... }: the '{' after its name
        if (t >= 0 && t + 1 < f->tok_count && f->toks[t + 1].kind == T_LBRACE) {
            return token_end(&f->toks[closing(e->at, t + 1)]);
        }
        break;
    case E_INDEX: case E_LIST: // `at` is the '['
        if (t >= 0) return token_end(&f->toks[closing(e->at, t)]);
        break;
    case E_INTERP: // To the part that ends with the quote, past those of text in its values
        for (int i = t, depth = 0; i >= 0 && i < f->tok_count; i++) {
            const token *part = &f->toks[i];
            if (part->kind != T_INTERP && part->kind != T_INTERP_PART) continue;
            if (part->kind == T_INTERP) depth++;
            if (part->text.ptr[part->text.len - 1] == '"' && --depth == 0) return token_end(part);
        }
        break;
    default:
        break;
    }
    return t >= 0 ? token_end(&f->toks[t]) : e->at; // A literal, this, default, null, or a value's `!`
}

// Just past a statement's last token: its ';', or its block's '}'.
static loc stmt_end(const stmt *s)
{
    switch (s->kind) {
    case S_BLOCK:
    case S_SWITCH:
        return (loc){s->end.line, s->end.col + 1, s->end.file};
    case S_IF:
        return stmt_end(s->else_stmt ? s->else_stmt : s->then_stmt);
    case S_WHILE:
    case S_FOR:
    case S_FOREACH:
    case S_PARALLEL:
        if (s->then_stmt) return stmt_end(s->then_stmt);
        break;
    case S_EXPR:
        if (s->value->block) return stmt_end(s->value->block); // A call with a block after it
        break;
    default:
        break;
    }
    // To its ';', or to the '}' around it where a syntax error cut it short.
    const loc start = stmt_start(s);
    const afile *f = &A.files[start.file];
    int depth = 0;
    int i = token_from(start);
    for (; i < f->tok_count; i++) {
        const tok_kind k = f->toks[i].kind;
        if (k == T_LPAREN || k == T_LBRACKET || k == T_LBRACE) depth++;
        else if ((k == T_RPAREN || k == T_RBRACKET || k == T_RBRACE) && --depth < 0) break;
        else if (k == T_SEMI && depth == 0) return token_end(&f->toks[i]);
    }
    return i > 0 ? token_end(&f->toks[i - 1]) : start;
}

static void inlay_hint(jbuf *out, int *written, const loc at, const char *label, const int kind, const bool before)
{
    if ((*written)++) jb_put(out, ",");
    jb_put(out, "{\"position\":");
    write_position(out, at);
    jb_put(out, ",\"label\":");
    jb_string(out, label);
    jb_printf(out, ",\"kind\":%d,\"%s\":true}", kind, before ? "paddingRight" : "paddingLeft");
}

static int call_param_names(const expr *call, str *names, int max);

void analysis_inlay_hints(const int start_line, const int end_line, jbuf *out)
{
    enum { HINT_TYPE = 1, HINT_PARAMETER = 2 };
    jb_put(out, "[");
    int written = 0;
    for (int i = 0; i < A.occs.count; i++) { // `var speed = ...`: speed's type
        const occurrence *o = &A.occs.items[i];
        if (o->at.file != A.doc || o->kind != OCC_LOCAL || !o->declaration || o->local->type_name.len > 0) continue;
        if (o->at.line - 1 < start_line || o->at.line - 1 > end_line || o->type.kind == TY_ERROR) continue;
        sb label = {0};
        sb_printf(&label, ": %s", type_name(o->type));
        inlay_hint(out, &written, (loc){o->at.line, o->at.col + o->len, o->at.file}, label.data, HINT_TYPE, false);
    }
    for (int i = 0; i < A.calls.count; i++) { // Heal(unit.stats, amount: 2), Draw.Circle(p, radius: 3, color)
        const expr *call = A.calls.items[i];
        if (call->at.file != A.doc || call->at.line - 1 < start_line || call->at.line - 1 > end_line) continue;
        str names[16];
        const int count = call_param_names(call, names, 16);
        for (int n = 0; n < written_count(call); n++) {
            const int k = written_param(call, n);
            const expr *arg = written_arg(call, n);
            if (k >= count || written_name(call, n).len > 0) continue; // One written with its name says it already
            // A one-letter name (float3's x, y, z, Color's r, g, b) says no more than where the argument is.
            if (!is_literal(arg) || names[k].len <= 1) continue;
            sb label = {0};
            sb_printf(&label, STR_FMT ":", STR_ARG(names[k]));
            inlay_hint(out, &written, expr_start(arg), label.data, HINT_PARAMETER, true);
        }
    }
    jb_put(out, "]");
}

// Whether `name` holds the letters of `query` in order, ignoring case, as
// editors match symbols: "stHp" finds "StatsHelp".
static bool fuzzy_match(const str name, const char *query)
{
    int k = 0;
    for (const char *q = query; *q; q++) {
        while (k < name.len && tolower((unsigned char)name.ptr[k]) != tolower((unsigned char)*q)) k++;
        if (k == name.len) return false;
        k++;
    }
    return true;
}

static void workspace_symbol(jbuf *out, int *written, const str name, const int kind, const loc at, const int len,
                             const str container)
{
    if ((*written)++) jb_put(out, ",");
    jb_put(out, "{\"name\":");
    jb_string_n(out, name.ptr, (size_t)name.len);
    jb_printf(out, ",\"kind\":%d,\"location\":{\"uri\":", kind);
    jb_string(out, A.files[at.file].uri);
    jb_put(out, ",\"range\":");
    write_range(out, at, len);
    jb_put(out, "}");
    if (container.len > 0) {
        jb_put(out, ",\"containerName\":");
        jb_string_n(out, container.ptr, (size_t)container.len);
    }
    jb_put(out, "}");
}

void analysis_workspace_symbols(const char *query, jbuf *out, int *written)
{
    for (int i = 0; A.prog && i < A.written.count; i++) {
        const decl *d = A.written.items[i];
        if (d->builtin) continue;
        const int kind = symbol_kind(d);
        const str ns = decl_home(d) ? decl_home(d)->ns : (str){"", 0};
        if (fuzzy_match(d->name, query)) workspace_symbol(out, written, d->name, kind, d->at, d->name.len, ns);
        for (int k = 0; k < d->methods.count; k++) {
            const decl *m = method_written(d->methods.items[k]);
            if (m->is_operator || !fuzzy_match(m->name, query)) continue;
            workspace_symbol(out, written, m->name, SYMBOL_METHOD, m->at, m->name.len, d->qualified);
        }
    }
}

// ---------------------------------------------------------------------------
// Semantic tokens: highlighting from what names mean, not how they look

static const char *const token_types[] = {"namespace", "type",     "struct", "class",   "interface", "parameter",
                                          "variable",  "property", "enumMember", "function", "method", "keyword",
                                          "decorator", "enum", "comment", "macro"};
enum { ST_NAMESPACE, ST_TYPE, ST_STRUCT, ST_CLASS, ST_INTERFACE, ST_PARAMETER, ST_VARIABLE, ST_PROPERTY,
       ST_ENUM_MEMBER, ST_FUNCTION, ST_METHOD, ST_KEYWORD, ST_DECORATOR, ST_ENUM, ST_COMMENT, ST_MACRO };

// Lowercase built-in value types (float3, int, bool, ...) read as keywords,
// like C#'s float and int.
static bool is_keyword_type(const type t)
{
    switch (t.kind) {
    case TY_BOOL: case TY_BYTE: case TY_INT: case TY_LONG: case TY_INT2: case TY_INT3: case TY_INT4: case TY_FLOAT: case TY_FLOAT2:
    case TY_FLOAT3: case TY_FLOAT4: case TY_QUATERNION: case TY_FLOAT2X2: case TY_FLOAT3X3: case TY_FLOAT4X4:
        return true;
    default:
        return false;
    }
}

// `local` is Tide's own: a local field of the match's component or singleton,
// which editors can color apart from the match's fields.
static const char *const token_modifiers[] = {"declaration", "readonly", "static", "defaultLibrary", "local"};
enum { SM_DECLARATION = 1, SM_READONLY = 2, SM_STATIC = 4, SM_DEFAULT_LIBRARY = 8, SM_LOCAL = 16 };

void analysis_semantic_legend(jbuf *out)
{
    jb_put(out, "{\"tokenTypes\":[");
    for (size_t i = 0; i < sizeof token_types / sizeof token_types[0]; i++) {
        if (i) jb_put(out, ",");
        jb_string(out, token_types[i]);
    }
    jb_put(out, "],\"tokenModifiers\":[");
    for (size_t i = 0; i < sizeof token_modifiers / sizeof token_modifiers[0]; i++) {
        if (i) jb_put(out, ",");
        jb_string(out, token_modifiers[i]);
    }
    jb_put(out, "]}");
}

static void classify(const occurrence *o, int *type, int *mods)
{
    *mods = o->declaration ? SM_DECLARATION : 0;
    switch (o->kind) {
    case OCC_TYPE:
        if (o->backing) { // byte, like int
            *type = ST_KEYWORD;
            *mods = 0;
            break;
        }
        if (!o->decl) {
            *type = is_keyword_type(o->type) ? ST_KEYWORD : ST_TYPE; // Color, Entity, PlayerID are types
            *mods = is_keyword_type(o->type) ? 0 : *mods | SM_DEFAULT_LIBRARY;
            break;
        }
        // Components and structs are structs, singletons classes, inputs
        // interfaces, and the device records types: editors can color each kind.
        *type = o->decl->kind == DECL_COMPONENT || o->decl->kind == DECL_STRUCT || o->decl->kind == DECL_EVENT ? ST_STRUCT
              : o->decl->kind == DECL_ENUM      ? ST_ENUM
              : o->decl->kind == DECL_INPUT     ? ST_INTERFACE
              : o->decl->kind == DECL_RECORD    ? ST_TYPE
                                                : ST_CLASS;
        if (o->decl->builtin) *mods |= SM_DEFAULT_LIBRARY;
        break;
    case OCC_SYSTEM: *type = ST_FUNCTION; break;
    case OCC_FIELD:
        *type = ST_PROPERTY;
        if (o->decl && o->decl->builtin) *mods |= SM_DEFAULT_LIBRARY;
        if (o->field && o->field->is_local) *mods |= SM_LOCAL;
        break;
    case OCC_PARAM:
        *type = o->object_type.decl && o->object_type.decl->query_row ? ST_PROPERTY : ST_PARAMETER;
        if (o->param->mode != PARAM_MUT) *mods |= SM_READONLY;
        break;
    case OCC_LOCAL:
        *type = ST_VARIABLE;
        if (!o->local->is_mut) *mods |= SM_READONLY;
        break;
    case OCC_OWNER: *type = ST_NAMESPACE; *mods |= SM_DEFAULT_LIBRARY; break;
    case OCC_FUNCTION:
        *type = ST_FUNCTION;
        if (!is_routine(o->decl)) *mods |= SM_DEFAULT_LIBRARY | SM_STATIC;
        break;
    case OCC_CONSTANT: *type = ST_ENUM_MEMBER; *mods |= SM_DEFAULT_LIBRARY | SM_STATIC | SM_READONLY; break;
    case OCC_CONST: *type = ST_ENUM_MEMBER; *mods |= SM_STATIC | SM_READONLY; break; // Like Math.PI
    case OCC_SETTING: *type = ST_PROPERTY; *mods |= SM_DEFAULT_LIBRARY; break;
    case OCC_METHOD:
        if (is_operator_decl(o->decl)) {
            *type = ST_KEYWORD;
            *mods = 0;
        } else if (is_routine(o->decl)) {
            *type = ST_METHOD;
        } else if (str_eq_c(o->name, "Sample") || str_eq_c(o->name, "Sanitize")) { // The input's own members
            *type = ST_KEYWORD;
            *mods = 0;
        } else {
            *type = ST_METHOD;
            *mods |= SM_DEFAULT_LIBRARY;
        }
        break;
    case OCC_ATTRIBUTE: *type = ST_DECORATOR; *mods = 0; break;
    case OCC_MEMBER:
        *type = ST_PROPERTY;
        *mods |= SM_DEFAULT_LIBRARY;
        if (o->object_type.decl && o->object_type.decl->query_row) *mods |= SM_READONLY;
        break;
    case OCC_NAMESPACE: *type = ST_NAMESPACE; break;
    case OCC_ENUM_MEMBER: *type = ST_ENUM_MEMBER; *mods |= SM_READONLY; break;
    case OCC_THIS: *type = ST_KEYWORD; *mods = 0; break;
    case OCC_DEFAULT: *type = ST_KEYWORD; *mods = 0; break;
    case OCC_OWNERSHIP: *type = ST_KEYWORD; *mods = 0; break;
    case OCC_VERIFIED: *type = ST_KEYWORD; *mods = 0; break;
    case OCC_OUT: *type = ST_KEYWORD; *mods = 0; break; // A word only before a parameter's type and before an argument
    case OCC_REPLACE: *type = ST_KEYWORD; *mods = 0; break; // `virtual` and `override`, words only before a declaration
    }
}

typedef struct semantic_token {
    int line; // From 0
    int col;  // In UTF-16 units
    int len;
    int type;
    int mods;
} semantic_token;

typedef VEC(semantic_token) semantic_tokens;

// `n` bytes of UTF-8 in UTF-16 units, as editors count.
static int utf16_len(const char *p, const int n)
{
    int units = 0;
    for (int i = 0; i < n; i++) {
        const unsigned char c = (unsigned char)p[i];
        if ((c & 0xC0) != 0x80) units += c >= 0xF0 ? 2 : 1;
    }
    return units;
}

// A directive's line: its word (#if) a keyword and the symbols in it macros.
// Lines it leaves out read as comments, as editors gray out such code.
static void inactive_tokens(const int line, semantic_tokens *toks)
{
    const str text = doc_line(line);
    if (DOC->inactive[line] == LINE_LEFT_OUT) {
        int start = 0;
        while (start < text.len && (text.ptr[start] == ' ' || text.ptr[start] == '\t')) start++;
        if (start == text.len) return;
        const semantic_token t = {line - 1, utf16_len(text.ptr, start), utf16_len(text.ptr + start, text.len - start),
                                  ST_COMMENT, 0};
        vec_push(*toks, t);
        return;
    }
    int i = 0;
    while (i < text.len && text.ptr[i] != '#') i++;
    int word = i + 1;
    while (word < text.len && (text.ptr[word] == ' ' || text.ptr[word] == '\t')) word++;
    int end = word;
    while (end < text.len && isalnum((unsigned char)text.ptr[end])) end++;
    const semantic_token directive = {line - 1, utf16_len(text.ptr, i), end - i, ST_KEYWORD, 0};
    vec_push(*toks, directive);
    for (int k = end; k < text.len;) {
        if (text.ptr[k] == '/' && k + 1 < text.len && text.ptr[k + 1] == '/') break; // A comment after it
        if (!isalpha((unsigned char)text.ptr[k]) && text.ptr[k] != '_') {
            k++;
            continue;
        }
        int name = k;
        while (k < text.len && (isalnum((unsigned char)text.ptr[k]) || text.ptr[k] == '_')) k++;
        const bool literal = (k - name == 4 && memcmp(text.ptr + name, "true", 4) == 0)
                          || (k - name == 5 && memcmp(text.ptr + name, "false", 5) == 0);
        const semantic_token symbol = {line - 1, utf16_len(text.ptr, name), k - name, literal ? ST_KEYWORD : ST_MACRO, 0};
        vec_push(*toks, symbol);
    }
}

static int semantic_order(const void *a, const void *b)
{
    const semantic_token *x = a;
    const semantic_token *y = b;
    return x->line != y->line ? (x->line > y->line) - (x->line < y->line) : (x->col > y->col) - (x->col < y->col);
}

void analysis_semantic_tokens(jbuf *out)
{
    semantic_tokens toks = {0};
    for (int i = 0; i < A.occs.count; i++) {
        const occurrence *o = &A.occs.items[i];
        if (o->at.file != A.doc) continue;
        if (is_operator_decl(o->decl) && !o->declaration) continue; // `+` in code: the editor colors it as an operator
        semantic_token t = {o->at.line - 1, utf16_col(o->at), o->len, 0, 0};
        classify(o, &t.type, &t.mods);
        vec_push(toks, t);
    }
    // `frag` and `vert` are words only where a declaration starts, before its name, which an override
    // can write with a namespace
    for (int i = 0; i < A.written.count; i++) {
        const decl *d = A.written.items[i];
        int name = is_shader(d) && d->at.file == A.doc ? token_at(d->at) : -1;
        while (name >= 2 && DOC->toks[name - 1].kind == T_DOT && DOC->toks[name - 2].kind == T_IDENT) name -= 2;
        if (name < 1) continue;
        const token *word = &DOC->toks[name - 1];
        const semantic_token t = {word->at.line - 1, utf16_col(word->at), word->text.len, ST_KEYWORD, 0};
        vec_push(toks, t);
    }
    for (int i = 0; i < DOC->tok_count; i++) {
        if (!DOC->query_keywords[i]) continue;
        const token *word = &DOC->toks[i];
        vec_push(toks, ((semantic_token){word->at.line - 1, utf16_col(word->at), 5, ST_KEYWORD, 0}));
    }
    for (int line = 1; line <= DOC->lines.count; line++) {
        if (DOC->inactive[line]) inactive_tokens(line, &toks);
    }
    if (toks.count > 0) qsort(toks.items, (size_t)toks.count, sizeof(semantic_token), semantic_order);
    jb_put(out, "{\"data\":[");
    int prev_line = 0;
    int prev_col = 0;
    for (int i = 0; i < toks.count; i++) {
        const semantic_token *t = &toks.items[i];
        const int delta_col = t->line == prev_line ? t->col - prev_col : t->col;
        jb_printf(out, "%s%d,%d,%d,%d,%d", i ? "," : "", t->line - prev_line, delta_col, t->len, t->type, t->mods);
        prev_line = t->line;
        prev_col = t->col;
    }
    jb_put(out, "]}");
}

// ---------------------------------------------------------------------------
// Completion

enum {
    CK_METHOD = 2, CK_FUNCTION = 3, CK_FIELD = 5, CK_VARIABLE = 6, CK_CLASS = 7, CK_INTERFACE = 8, CK_MODULE = 9,
    CK_PROPERTY = 10, CK_ENUM = 13, CK_KEYWORD = 14, CK_SNIPPET = 15, CK_ENUM_MEMBER = 20, CK_CONSTANT = 21, CK_STRUCT = 22,
    CK_EVENT = 23,
};

typedef struct completion {
    jbuf *out;
    int count;
    VEC(const char *) seen;
} completion;

static void complete_asset_path(completion *c, int tok, loc at);

static void item(completion *c, const char *label, const int kind, const char *detail, const char *doc,
                 const char *snippet)
{
    for (int i = 0; i < c->seen.count; i++) {
        if (strcmp(c->seen.items[i], label) == 0) return;
    }
    vec_push(c->seen, label);
    if (c->count++) jb_put(c->out, ",");
    jb_put(c->out, "{\"label\":");
    jb_string(c->out, label);
    jb_printf(c->out, ",\"kind\":%d", kind);
    if (detail) {
        jb_put(c->out, ",\"detail\":");
        jb_string(c->out, detail);
    }
    if (doc) {
        jb_put(c->out, ",\"documentation\":{\"kind\":\"markdown\",\"value\":");
        jb_string(c->out, doc);
        jb_put(c->out, "}");
    }
    if (snippet) {
        jb_put(c->out, ",\"insertText\":");
        jb_string(c->out, snippet);
        jb_put(c->out, ",\"insertTextFormat\":2");
    }
    jb_put(c->out, "}");
}

// What surrounds the cursor.
typedef struct scope {
    const decl *decl;           // The system, view, method, function or input whose body holds the cursor, or NULL
    const decl *fields_of;      // Whose fields plain names refer to: the input in Sample, a type in its methods
    bool in_input;
    bool in_sanitize; // Sets in_input too, for the fields
    VEC(const stmt *) locals;   // Locals declared before the cursor, still in scope
} scope;

static bool block_contains(const stmt *block, const loc at)
{
    return block && block->kind == S_BLOCK && block->at.file == at.file && loc_cmp(block->at, at) < 0
        && loc_cmp(at, block->end) <= 0;
}

// Whether the cursor is in a statement's body: a block, or a statement of its
// own without braces, as in `foreach (var p in ps) Use(p);`.
static bool body_contains(const stmt *body, const loc at)
{
    if (!body || body->kind == S_BLOCK) return block_contains(body, at);
    return body->at.file == at.file && loc_cmp(stmt_start(body), at) <= 0 && loc_cmp(at, stmt_end(body)) <= 0;
}

static void collect_list(stmt *const *stmts, int count, loc at, scope *sc);

// The locals of a body the cursor is in: a block's, or a braceless statement's.
static void collect_body(stmt *const *body, const loc at, scope *sc)
{
    if ((*body)->kind == S_BLOCK) collect_list((*body)->stmts.items, (*body)->stmts.count, at, sc);
    else collect_list(body, 1, at, sc);
}

// The names `is` gives in a condition, joined by &&: in scope where it's true.
static void collect_bindings(const expr *cond, scope *sc)
{
    if (!cond) return;
    if (cond->kind == E_IS && cond->binding) vec_push(sc->locals, cond->binding);
    if (cond->kind != E_BINARY || cond->op != T_AND) return;
    collect_bindings(cond->lhs, sc);
    collect_bindings(cond->rhs, sc);
}

static void collect_locals(const stmt *block, const loc at, scope *sc)
{
    collect_list(block->stmts.items, block->stmts.count, at, sc);
}

typedef VEC(const stmt *) local_list;

// The locals `out` arguments declare in an expression, `out var half`, in the
// order they're written. Not those in a call's block: they're the block's.
static void out_locals(const expr *e, local_list *found)
{
    if (!e) return;
    if (e->is_out && e->out_local && !e->out_local->discard) vec_push(*found, e->out_local);
    out_locals(e->object, found);
    out_locals(e->cond, found);
    out_locals(e->lhs, found);
    out_locals(e->rhs, found);
    for (int i = 0; i < written_count(e); i++) out_locals(written_arg(e, i), found);
    for (int i = 0; e->kind == E_LITERAL && i < e->inits.count; i++) out_locals(e->inits.items[i].value, found);
}

// Those of them declared before the cursor: in scope from there on.
static void collect_outs(const expr *e, const loc at, scope *sc)
{
    local_list found = {0};
    out_locals(e, &found);
    for (int i = 0; i < found.count; i++) {
        if (loc_cmp(found.items[i]->name_at, at) < 0) vec_push(sc->locals, found.items[i]);
    }
}

// What the `out` arguments in a statement's own expressions declared before
// the cursor, as the checker has it: a statement's, an if's condition's, a
// switch's value's and a foreach's list's are the enclosing block's, so the
// list of statements they're in has them; a while's and a for's header's are
// the loop's own, in scope up to its end.
static void collect_stmt_outs(const stmt *s, const loc at, scope *sc)
{
    if (!s) return;
    if (s->kind == S_WHILE || s->kind == S_FOR) {
        if (loc_cmp(at, stmt_end(s)) > 0) return;
        collect_stmt_outs(s->init, at, sc);
        collect_outs(s->cond, at, sc);
        collect_stmt_outs(s->step, at, sc);
        return;
    }
    collect_outs(s->cond, at, sc);
    collect_outs(s->target, at, sc);
    collect_outs(s->value, at, sc);
    collect_outs(s->by, at, sc);
    collect_outs(s->offset, at, sc);
}

// The locals declared before the cursor in a list of statements, and in the
// ones around it.
static void collect_list(stmt *const *stmts, const int count, const loc at, scope *sc)
{
    for (int i = 0; i < count; i++) {
        const stmt *s = stmts[i];
        if (loc_cmp(s->at, at) >= 0) break;
        if (s->kind == S_VAR && loc_cmp(s->name_at, at) < 0) vec_push(sc->locals, s);
        collect_stmt_outs(s, at, sc); // if (TryHalf(8, out var half)): half, from there on
        if (block_contains(s, at)) collect_locals(s, at, sc);
        if (s->kind == S_IF) {
            if (body_contains(s->then_stmt, at)) {
                collect_bindings(s->cond, sc); // if (Parse(t) is int score)
                collect_body(&s->then_stmt, at, sc);
            }
            if (body_contains(s->else_stmt, at)) collect_body(&s->else_stmt, at, sc); // else if (...) too
        }
        if ((s->kind == S_FOREACH || s->kind == S_PARALLEL) && body_contains(s->then_stmt, at)) {
            vec_push(sc->locals, s); // foreach (var item in ...)
            collect_body(&s->then_stmt, at, sc);
        }
        if ((s->kind == S_WHILE || s->kind == S_FOR) && body_contains(s->then_stmt, at)) {
            if (s->init && s->init->kind == S_VAR) vec_push(sc->locals, s->init); // for (var i = 0; ...)
            collect_bindings(s->cond, sc);
            collect_body(&s->then_stmt, at, sc);
        }
        if (s->kind == S_EXPR && block_contains(s->value->block, at)) {
            for (int k = 0; k < 2; k++) { // items.Sort(a, b) { ... }: its two elements
                if (s->value->compare[k]) vec_push(sc->locals, s->value->compare[k]);
            }
            collect_locals(s->value->block, at, sc);
        }
        if (s->kind == S_SWITCH && loc_cmp(at, s->end) <= 0) {
            // The section the cursor is in: from its first label to the next section's.
            for (int k = 0; k < s->cases.count; k++) {
                const switch_case *section = &s->cases.items[k];
                const bool started = section->label_at.count > 0 && loc_cmp(section->label_at.items[0], at) < 0;
                const bool ended = k + 1 < s->cases.count && s->cases.items[k + 1].label_at.count > 0
                                && loc_cmp(s->cases.items[k + 1].label_at.items[0], at) < 0;
                if (started && !ended) collect_list(section->body.items, section->body.count, at, sc);
            }
        }
    }
}

static scope scope_at(const loc at)
{
    scope sc = {0};
    for (int i = 0; i < A.written.count; i++) {
        const decl *d = A.written.items[i];
        for (int k = 0; k < d->methods.count; k++) {
            const decl *m = method_written(d->methods.items[k]);
            if (!block_contains(m->body, at)) continue;
            sc.decl = m;
            sc.fields_of = m->is_operator ? NULL : d;
            collect_locals(m->body, at, &sc);
        }
        const bool sanitize = block_contains(d->sanitize, at);
        if (d->builtin || (!block_contains(d->body, at) && !sanitize)) continue;
        sc.decl = d;
        sc.in_input = d->kind == DECL_INPUT;
        sc.fields_of = sc.in_input ? d : d->kind == DECL_METHOD ? d->owner : NULL; // An override of a method: its type's
        sc.in_sanitize = sanitize;
        collect_locals(sanitize ? d->sanitize : d->body, at, &sc);
    }
    return sc;
}

// The type of `this` in scope: the entity the code runs for, or a component
// method's. TY_ERROR where there's none.
static type this_type(const scope *sc)
{
    const decl *d = sc->decl;
    if (!d || sc->in_input) return (type){TY_ERROR, NULL};
    if (d->kind == DECL_METHOD && !d->is_operator && !d->is_interpolate && d->owner && d->owner->kind == DECL_COMPONENT) {
        return (type){d->owner->is_local ? TY_LOCAL_ENTITY : TY_ENTITY, NULL};
    }
    if (d->kind == DECL_SYSTEM && d->per_entity) return (type){d->entity_local ? TY_LOCAL_ENTITY : TY_ENTITY, NULL};
    return (type){TY_ERROR, NULL};
}

// The type of a name in scope, or TY_ERROR.
static type name_type(const scope *sc, const str name, const param **param_out)
{
    *param_out = NULL;
    for (int i = sc->locals.count - 1; i >= 0; i--) {
        if (str_eq(sc->locals.items[i]->name, name)) return sc->locals.items[i]->type;
    }
    if (!sc->decl) return (type){TY_ERROR, NULL};
    for (int i = 0; i < sc->decl->params.count && !sc->in_sanitize; i++) {
        const param *p = &sc->decl->params.items[i];
        if (p->name.len > 0 && str_eq(p->name, name)) {
            *param_out = p;
            return p->type;
        }
    }
    if (sc->fields_of) {
        for (int i = 0; i < sc->fields_of->fields.count; i++) {
            if (str_eq(sc->fields_of->fields.items[i].name, name)) return sc->fields_of->fields.items[i].type;
        }
    }
    return (type){TY_ERROR, NULL};
}

static bool is_swizzle(const str member, const int dim)
{
    if (member.len < 1 || member.len > 4) return false;
    for (int i = 0; i < member.len; i++) {
        const char *at = strchr("xyzw", member.ptr[i]);
        if (!at || at - "xyzw" >= dim) return false;
    }
    return true;
}

// The type of `t.member`, or TY_ERROR.
static type member_type(const type t, const str member)
{
    if (t.kind == TY_RENDER_TARGET && str_eq_c(member, "color")) return (type){TY_TEXTURE, NULL};
    if (t.kind == TY_RENDER_TARGET && str_eq_c(member, "depth")) return (type){TY_DEPTH_TEXTURE, NULL};
    if (t.kind == TY_CUBEMAP && str_eq_c(member, "faces")) return t; // Of which an index is a RenderTarget
    if (t.decl && (t.kind == TY_COMPONENT || t.kind == TY_SINGLETON || t.kind == TY_INPUT || t.kind == TY_RECORD
                   || t.kind == TY_STRUCT || t.kind == TY_EVENT)) {
        for (int i = 0; i < t.decl->fields.count + local_field_count(t.decl); i++) {
            if (str_eq(field_of(t.decl, i)->name, member)) return field_of(t.decl, i)->type;
        }
    }
    const int dim = type_dim(t);
    if (dim >= 2 && is_swizzle(member, dim)) return vector_type(type_is_float_based(t), member.len);
    if (t.kind == TY_COLOR && member.len == 1 && strchr("rgba", member.ptr[0])) return (type){TY_FLOAT, NULL};
    if (t.kind == TY_RECT && (str_eq_c(member, "x") || str_eq_c(member, "y") || str_eq_c(member, "width")
                              || str_eq_c(member, "height"))) {
        return (type){TY_FLOAT, NULL};
    }
    if (t.kind == TY_STRING && str_eq_c(member, "length")) return (type){TY_INT, NULL};
    if ((t.kind == TY_LIST || t.kind == TY_MAP || t.kind == TY_SET) && str_eq_c(member, "count")) return (type){TY_INT, NULL};
    if (t.kind == TY_RECORD && t.decl->array_of && str_eq_c(member, "count")) return (type){TY_INT, NULL};
    if (t.kind == TY_ARRAY && str_eq_c(member, "count")) return (type){TY_INT, NULL};
    if (t.kind == TY_GRID && (str_eq_c(member, "size") || str_eq_c(member, "min") || str_eq_c(member, "max"))) {
        return (type){t.decl->dims == 3 ? TY_INT3 : TY_INT2, NULL};
    }
    if (t.kind == TY_QUATERNION && str_eq_c(member, "value")) return (type){TY_FLOAT4, NULL};
    const int n = matrix_dim(t);
    if (n > 0 && member.len == 2 && member.ptr[0] == 'c' && member.ptr[1] >= '0' && member.ptr[1] < '0' + n) {
        return vector_type(true, n);
    }
    return (type){TY_ERROR, NULL};
}

static bool loop_variable_named(int i);

// The type of the variable of a loop the cursor is in, named at token `use`,
// read from its header: for a body without braces that doesn't parse yet,
// like `foreach (var p in parts) Use(p.`, which has no loop to look at.
static type loop_variable_type(const scope *sc, const int use)
{
    const str name = DOC->toks[use].text;
    for (int k = use - 1; k >= 2; k--) {
        const token *t = &DOC->toks[k];
        if (t->kind == T_LBRACE || t->kind == T_RBRACE || t->kind == T_SEMI) break; // Out of the statement
        if (t->kind != T_IDENT || !str_eq(t->text, name) || !loop_variable_named(k)) continue;
        if (DOC->toks[k + 1].kind != T_IDENT || !str_eq_c(DOC->toks[k + 1].text, "in")) continue;
        int open = k - 1;
        while (DOC->toks[open].kind != T_LPAREN) open--;
        if (k - open == 2 && DOC->toks[open + 1].kind == T_IDENT) { // foreach (Stats s in
            type written;
            if (builtin_type_named(DOC->toks[open + 1].text, &written)) return written;
            for (int i = 0; i < A.prog->decls.count; i++) {
                const decl *d = A.prog->decls.items[i];
                if ((d->kind != DECL_STRUCT && d->kind != DECL_ENUM) || !str_eq(d->name, DOC->toks[open + 1].text)) continue;
                return (type){d->kind == DECL_STRUCT ? TY_STRUCT : TY_ENUM, (decl *)d};
            }
            return (type){TY_ERROR, NULL};
        }
        // var: an element of what it goes through, written as names: inv.items
        type of = {TY_ERROR, NULL};
        int i = k + 2;
        if (DOC->toks[i].kind == T_IDENT) {
            const param *ignored;
            of = name_type(sc, DOC->toks[i].text, &ignored);
            for (i++; DOC->toks[i].kind == T_DOT && DOC->toks[i + 1].kind == T_IDENT; i += 2) {
                of = member_type(of, DOC->toks[i + 1].text);
            }
        }
        if (of.kind == TY_LIST || of.kind == TY_SET) return of.decl->fields.items[0].type;
        if (of.kind == TY_MAP && of.decl->entry) return (type){TY_STRUCT, of.decl->entry}; // An entry: key and value
        if (of.kind == TY_GRID) return (type){of.decl->dims == 3 ? TY_INT3 : TY_INT2, NULL}; // A cell's position
        if (of.kind == TY_QUERY) return (type){TY_STRUCT, of.decl};
        if (of.kind == TY_RECORD && of.decl->array_of) return (type){TY_RECORD, of.decl->array_of}; // touches
        return (type){TY_ERROR, NULL};
    }
    return (type){TY_ERROR, NULL};
}

// A method or function, with its signature, as `name` (its name by default).
static void complete_routine(completion *c, const decl *m, const char *name)
{
    sb detail = {0};
    format_routine(m, &detail);
    sb snippet = {0};
    sb_printf(&snippet, "%s(%s)", name ? name : str_to_cstr(m->name), m->params.count > 0 ? "$1" : "");
    item(c, name ? name : str_to_cstr(m->name), m->owner ? CK_METHOD : CK_FUNCTION, detail.data, NULL, snippet.data);
}

typedef struct builtin_visit {
    completion *c;
    bool values_only; // Not its functions: where the code can only read
} builtin_visit;

static void add_builtin_member(void *user, const builtin_member *m)
{
    const builtin_visit *v = user;
    if (m->is_function && v->values_only) return;
    item(v->c, m->name, m->is_function ? CK_FUNCTION : CK_CONSTANT, m->detail, m->doc, m->snippet);
}

static void add_target_member(void *user, const builtin_member *m)
{
    if (!m->is_function || strcmp(m->name, "Blit") == 0) return;
    completion *c = user;
    item(c, m->name, CK_METHOD, m->detail, m->doc, m->snippet);
}

static int label_split(const char *label, int starts[16], int ends[16]);

// What a built-in method's call starts as: its name and parentheses, with the
// cursor inside when it takes arguments. One with an `out` parameter has its
// arguments laid out, as that one's says `out`: TryGet(key, out var value).
static void method_snippet(const method_form *form, sb *out)
{
    int starts[16];
    int ends[16];
    const int params = strstr(form->form, "out ") ? label_split(form->form, starts, ends) : 0;
    if (params <= 0) {
        sb_printf(out, "%s(%s)", form->name, strstr(form->form, "()") ? "" : "$1");
        return;
    }
    sb_printf(out, "%s(", form->name);
    for (int i = 0; i < params; i++) {
        const char *text = form->form + starts[i];
        const char *name = form->form + ends[i]; // Its name: the last word
        while (name > text && name[-1] != ' ') name--;
        const bool makes = strncmp(text, "out ", 4) == 0;
        sb_printf(out, "%s%s${%d:%.*s}", i ? ", " : "", makes ? "out var " : "", i + 1, (int)(form->form + ends[i] - name), name);
    }
    sb_put(out, ")");
}

static void list_members(completion *c, const type t, const bool edges, const scope *sc)
{
    if (t.decl && t.decl->query_row) {
        for (int i = 0; i < t.decl->fields.count; i++) {
            const field *f = &t.decl->fields.items[i];
            item(c, str_to_cstr(f->name), CK_FIELD, type_name(f->type),
                 str_eq_c(f->name, "entity") ? "The entity matched by this query row, read-only."
                                            : "Read-only component data from this query row.", NULL);
        }
        return;
    }
    // In a frag or vert: the GPU has no text, lists, maps or sets, and a grid there is a texture
    const bool gpu = is_shader(sc->decl);
    if (t.kind == TY_RENDER_TARGET) {
        item(c, "color", CK_PROPERTY, "Texture",
             "The target's GPU color image, without CPU readback: its first, where it holds several.", NULL);
        if (!gpu) {
            item(c, "colors", CK_PROPERTY, "Texture, by its number",
                 "The target's color images, in the order of the formats it was made with: `colors[1]` is its second, "
                 "as a Texture, and `colors[0]` is `color`. A frag's `out Color` parameters go to them in that order.",
                 "colors[$1]");
        }
        item(c, "depth", CK_PROPERTY, "DepthTexture",
             "The target's depth, for a frag to read: kept in a field of a component or singleton the frag takes, and "
             "read there with `Sample(uv)` or `Compare(uv, value)`.", NULL);
        if (!gpu) {
            for (size_t i = 0; i < sizeof target_calls / sizeof target_calls[0]; i++)
                item(c, target_calls[i].name, CK_METHOD, target_calls[i].form, target_calls[i].doc,
                     i == 0 ? "Resize($1, $2)" : "Dispose()");
        }
        if (!gpu && sc->decl && (sc->decl->is_view || sc->decl->kind == DECL_FUNCTION)) {
            builtin_list_members(str_from("Draw"), add_target_member, c); // The window's, as its own
        }
        return;
    }
    if (gpu && (t.kind == TY_STRING || t.kind == TY_LIST || t.kind == TY_MAP || t.kind == TY_SET)) return;
    if (t.kind == TY_CUBEMAP) { // A frag reads it; a view draws into its faces, resizes it and lets it go
        static const char *const snippets[] = {"Sample($1)", "Resize($1)", "Dispose()"};
        for (size_t i = gpu ? 0 : 1; i < (gpu ? 1 : sizeof cube_calls / sizeof cube_calls[0]); i++) {
            sb detail = {0};
            format_method(&cube_calls[i], t, &detail);
            item(c, cube_calls[i].name, CK_METHOD, detail.data, cube_calls[i].doc, snippets[i]);
        }
        if (!gpu) {
            item(c, "faces", CK_PROPERTY, "RenderTarget, by its number",
                 "The six faces of a cube made with `Cubemap(size)`, each a target to draw into: `faces[0]` to "
                 "`faces[5]`, in the order +x, -x, +y, -y, +z, -z. A face is what a camera at the cube's middle sees "
                 "along that axis with a field of view of 90: +y is up for the four around, and for the one above "
                 "and the one below, -z and +z.", "faces[$1]");
        }
        return;
    }
    if (gpu && t.kind == TY_DEPTH_TEXTURE) {
        for (size_t i = 0; i < sizeof depth_calls / sizeof depth_calls[0]; i++) {
            sb detail = {0};
            format_method(&depth_calls[i], t, &detail);
            item(c, depth_calls[i].name, CK_METHOD, detail.data, depth_calls[i].doc, i == 0 ? "Sample($1)" : "Compare($1, $2)");
        }
        return;
    }
    if (gpu && (t.kind == TY_GRID || t.kind == TY_TEXTURE)) {
        if (t.kind == TY_GRID && (t.decl->dims != 2 || t.decl->fields.items[0].type.kind != TY_COLOR)) return;
        const method_form *sample = t.kind == TY_TEXTURE ? &texture_calls[0]
            : find_form(grid_calls, sizeof grid_calls / sizeof grid_calls[0], str_from("Sample"));
        sb detail = {0};
        format_method(sample, t, &detail);
        item(c, sample->name, CK_METHOD, detail.data, sample->doc, "Sample($1)");
        return;
    }
    if (t.decl && (t.kind == TY_COMPONENT || t.kind == TY_SINGLETON || t.kind == TY_INPUT || t.kind == TY_RECORD
                   || t.kind == TY_STRUCT || t.kind == TY_EVENT)) {
        for (int i = 0; i < t.decl->fields.count; i++) {
            const field *f = &t.decl->fields.items[i];
            if (f->hidden) continue;
            if (f->typed) { // What's typed is a frame's: views read it, and the functions they call
                if (!sc->decl || !(sc->decl->is_view || sc->decl->kind == DECL_FUNCTION)) continue;
                item(c, str_to_cstr(f->name), CK_FIELD, type_name(f->type), KEYBOARD_TEXT_DOC, NULL);
                continue;
            }
            const char *doc = button_field_doc(t.decl, f->name);
            item(c, str_to_cstr(f->name), CK_FIELD, type_name(f->type), doc ? doc : builtin_field_doc(t.decl, f->name), NULL);
        }
        // Time.seed, which is only among Time's fields once something uses it
        if (t.decl == A.prog->time && !A.prog->uses_seed) item(c, "seed", CK_FIELD, "int", TIME_SEED_DOC, NULL);
        // A Random's methods: the code needs it mut, as each steps it
        if (t.kind == TY_STRUCT && t.decl == A.prog->random) {
            builtin_visit v = {c, false};
            builtin_list_members(str_from("Random"), add_builtin_member, &v);
        }
        // Its local fields, where code has them: views, local handlers and the
        // functions they call, and the frags and verts a view draws through.
        // The match can't see them.
        const bool local_code = sc->decl && !sc->decl->is_async && !sc->in_input
                             && (sc->decl->kind == DECL_FUNCTION || gpu
                                 || (sc->decl->kind == DECL_SYSTEM && (sc->decl->is_view || sc->decl->is_local)));
        for (int i = 0; local_code && i < local_field_count(t.decl); i++) {
            const field *f = field_of(t.decl, t.decl->fields.count + i);
            sb detail = {0};
            sb_printf(&detail, "local %s", type_name(f->type));
            item(c, str_to_cstr(f->name), CK_FIELD, detail.data,
                 gpu ? "A local field: this machine's, outside the match. A frag or vert reads it like any other field."
                     : "A local field: this machine's, outside the match. Views read and write it in place.",
                 NULL);
        }
        if (t.kind == TY_SINGLETON && t.decl->builtin && str_eq_c(t.decl->name, "Session")) {
            item(c, "room", CK_FIELD, "string",
                 "The code of the room the match is in, like \"K7QF2M\", or \"\" if it's in none. Others join it with "
                 "`Session.Join(code)`.",
                 NULL);
        }
        if (t.kind == TY_EVENT && t.decl->builtin && str_eq_c(t.decl->name, "Disconnected")) {
            item(c, "message", CK_FIELD, "string", "With `Kicked`: why, as the server's `Session.Kick` said. \"\" otherwise.",
                 NULL);
        }
        for (int i = 0; i < t.decl->methods.count; i++) {
            const decl *m = t.decl->methods.items[i];
            if (!m->is_operator && !m->is_interpolate) complete_routine(c, m, NULL);
        }
    }
    const int dim = type_dim(t);
    if (dim >= 2) {
        static const char *const components[] = {"x", "y", "z", "w"};
        static const char *const swizzles[] = {"xy", "xz", "yz", "xyz"};
        const char *scalar = type_name(vector_type(type_is_float_based(t), 1));
        for (int i = 0; i < dim; i++) item(c, components[i], CK_PROPERTY, scalar, NULL, NULL);
        for (size_t i = 0; i < sizeof swizzles / sizeof swizzles[0]; i++) {
            const str s = str_from(swizzles[i]);
            if (is_swizzle(s, dim) && s.len < dim) {
                item(c, swizzles[i], CK_PROPERTY, type_name(vector_type(type_is_float_based(t), s.len)), NULL, NULL);
            }
        }
    }
    if (t.kind == TY_COLOR) {
        item(c, "r", CK_PROPERTY, "float", "Red, 0 to 1.", NULL);
        item(c, "g", CK_PROPERTY, "float", "Green, 0 to 1.", NULL);
        item(c, "b", CK_PROPERTY, "float", "Blue, 0 to 1.", NULL);
        item(c, "a", CK_PROPERTY, "float", "Alpha, 0 to 1.", NULL);
    }
    if (t.kind == TY_LIST) item(c, "count", CK_PROPERTY, "int", LIST_COUNT_DOC, NULL);
    if (t.kind == TY_MAP || t.kind == TY_SET) item(c, "count", CK_PROPERTY, "int", MAP_COUNT_DOC, NULL);
    if (t.kind == TY_RECORD && t.decl->array_of) item(c, "count", CK_PROPERTY, "int", DEVICE_ARRAY_COUNT_DOC, NULL);
    if (t.kind == TY_ARRAY) item(c, "count", CK_PROPERTY, "int", ARRAY_COUNT_DOC, NULL);
    if (t.kind == TY_GRID) item(c, "size", CK_PROPERTY, t.decl->dims == 3 ? "int3" : "int2", GRID_SIZE_FIELD_DOC, NULL);
    if (t.kind == TY_STRING) {
        item(c, "length", CK_PROPERTY, "int", TEXT_LENGTH_DOC, NULL);
        builtin_visit v = {c, false};
        builtin_list_members(str_from("string"), add_builtin_member, &v);
    }
    if (t.kind == TY_RECT) {
        item(c, "x", CK_PROPERTY, "float", "The left side, from the screen's left.", NULL);
        item(c, "y", CK_PROPERTY, "float", "The top side, from the screen's top.", NULL);
        item(c, "width", CK_PROPERTY, "float", NULL, NULL);
        item(c, "height", CK_PROPERTY, "float", NULL, NULL);
    }
    if (t.kind == TY_QUATERNION) item(c, "value", CK_PROPERTY, "float4", "(x, y, z) is the vector part.", NULL);
    const int n = matrix_dim(t);
    for (int i = 0; i < n; i++) {
        static const char *const columns[] = {"c0", "c1", "c2", "c3"};
        item(c, columns[i], CK_PROPERTY, type_name(vector_type(true, n)), "A column.", NULL);
    }
    if (t.kind == TY_WORLD) { // world.: its methods, each with the component in <>
        size_t count;
        const method_form *forms = methods_of(t, &count);
        for (size_t i = 0; i < count; i++) {
            sb detail = {0};
            format_method(&forms[i], t, &detail);
            sb snippet = {0};
            if (strcmp(forms[i].name, "TryGet") == 0) sb_put(&snippet, "TryGet<${1:Component}>(${2:entity}, out var ${3:value})");
            else sb_printf(&snippet, "%s<$1>($2)", forms[i].name);
            item(c, forms[i].name, CK_METHOD, detail.data, forms[i].doc, snippet.data);
        }
        return;
    }
    // Built-in methods: a list's and a grid's, an entity's or a singleton's
    // where the code can change it, and a frag's Pixel's.
    const bool local = sc->decl && sc->decl->kind == DECL_SYSTEM && (sc->decl->is_view || sc->decl->is_local);
    const bool match = sc->decl && sc->decl->kind == DECL_SYSTEM && !local;
    const bool changes = (t.kind == TY_ENTITY && match) || (t.kind == TY_LOCAL_ENTITY && local)
                      || (t.kind == TY_SINGLETON && match && !t.decl->is_local);
    if (t.kind == TY_ENTITY && local) {
        item(c, "Send", CK_METHOD, "Send(command)", "Submits a command to this match entity; its handler checks the authenticated sender and untrusted payload.", "Send($1)");
    }
    const bool pixel = t.kind == TY_RECORD && t.decl == A.prog->pixel;
    if (t.kind == TY_LIST || t.kind == TY_MAP || t.kind == TY_SET || t.kind == TY_GRID || changes || pixel) {
        size_t count;
        const method_form *forms = methods_of(t, &count);
        for (size_t i = 0; i < count; i++) {
            if (t.kind == TY_LOCAL_ENTITY && strcmp(forms[i].name, "Snap") == 0) continue; // Local state isn't blended
            if (t.kind == TY_GRID && strcmp(forms[i].name, "Sample") == 0) continue; // A frag's or vert's, above
            sb detail = {0};
            format_method(&forms[i], t, &detail);
            sb snippet = {0};
            method_snippet(&forms[i], &snippet);
            item(c, forms[i].name, CK_METHOD, detail.data, forms[i].doc, snippet.data);
        }
    }
    if (edges && t.kind == TY_BOOL) {
        item(c, "down", CK_PROPERTY, "bool", "True on the tick it became true.", NULL);
        item(c, "up", CK_PROPERTY, "bool", "True on the tick it became false.", NULL);
    }
}

// Whether code here can call Session.Start and the like: views and local
// handlers, as the checker allows.
static bool decides_session(const scope *sc)
{
    return sc->decl && sc->decl->kind == DECL_SYSTEM && (sc->decl->is_view || sc->decl->is_local) && !sc->in_input;
}

// After `a.b.`: resolves the chain of names before the dot.
typedef enum order_completion {
    ORDER_NONE,   // Ordinary types and values
    ORDER_REFS,   // Before/After: declarations and enum phases
    ORDER_PHASE,  // Stage's first argument: an enum phase
    ORDER_OFFSET, // Stage's optional offset: an integer constant
} order_completion;

static bool complete_in_namespace(completion *c, str ns, order_completion order);
static const char *name_for(const decl *d);
static type out_variable_type(int use, loc at);

static void complete_members(completion *c, const int dot, const loc at, const order_completion order)
{
    if (dot >= 1 && DOC->toks[dot - 1].kind == T_THIS) { // this.: the entity's methods
        const scope sc = scope_at(at);
        const type t = this_type(&sc);
        if (t.kind != TY_ERROR) list_members(c, t, false, &sc);
        return;
    }
    int ids[16];
    int n = 0;
    for (int i = dot - 1; i >= 0 && DOC->toks[i].kind == T_IDENT && n < 16;) {
        ids[n++] = i;
        if (i >= 2 && DOC->toks[i - 1].kind == T_DOT) i -= 2;
        else break;
    }
    if (n == 0) return;

    if (order != ORDER_NONE) {
        sb written = {0};
        for (int k = n - 1; k >= 0; k--) {
            sb_printf(&written, "%s" STR_FMT, k == n - 1 ? "" : ".", STR_ARG(DOC->toks[ids[k]].text));
        }
        if (order != ORDER_OFFSET) {
            for (int i = 0; i < A.prog->decls.count; i++) {
                const decl *d = A.prog->decls.items[i];
                if (d->kind != DECL_ENUM || (strcmp(name_for(d), written.data) != 0
                                            && !str_eq_c(d->qualified, written.data))) continue;
                for (int m = 0; m < d->members.count; m++) {
                    item(c, str_to_cstr(d->members.items[m].name), CK_ENUM_MEMBER, "scheduling phase", NULL, NULL);
                }
                return;
            }
        }
        complete_in_namespace(c, (str){written.data, (int)written.len}, order);
        return;
    }

    const scope sc = scope_at(at);
    const bool gpu = is_shader(sc.decl); // A frag or vert: it has what it takes, and no devices, scenes or frame
    const str base = DOC->toks[ids[n - 1]].text;
    const param *p;
    type t = name_type(&sc, base, &p);
    if (t.kind == TY_ERROR) t = loop_variable_type(&sc, ids[n - 1]);
    if (t.kind == TY_ERROR) t = out_variable_type(ids[n - 1], at);
    if (t.kind == TY_ERROR && n == 2 && str_eq_c(base, "Draw") && str_eq_c(DOC->toks[ids[0]].text, "window") && !gpu) {
        list_members(c, (type){TY_RENDER_TARGET, NULL}, false, &sc);
        return;
    }
    if (t.kind == TY_ERROR) {
        // Session.: its calls, where local code runs
        if (n == 1 && str_eq_c(base, "Session") && decides_session(&sc)) {
            for (size_t i = 0; i < sizeof session_calls / sizeof session_calls[0]; i++) {
                sb snippet = {0};
                sb_printf(&snippet, "%s($1)", session_calls[i].name);
                item(c, session_calls[i].name, CK_FUNCTION, session_calls[i].form, session_calls[i].doc, snippet.data);
            }
            return;
        }
        // Clipboard.: its one call, where local code runs
        if (n == 1 && str_eq_c(base, "Clipboard") && decides_session(&sc)) {
            item(c, "Copy", CK_FUNCTION, CLIPBOARD_COPY_FORM, CLIPBOARD_COPY_DOC, "Copy($1)");
            return;
        }
        // Wait.: what async code waits for
        if (n == 1 && str_eq_c(base, "Wait") && sc.decl && sc.decl->is_async) {
            for (size_t i = 0; i < sizeof wait_calls / sizeof wait_calls[0]; i++) {
                sb snippet = {0};
                sb_printf(&snippet, "%s($1)", wait_calls[i].name);
                item(c, wait_calls[i].name, CK_FUNCTION, wait_calls[i].form, wait_calls[i].doc, snippet.data);
            }
            return;
        }
        if (n == 1 && str_eq_c(base, "Scene") && sc.decl && !sc.in_input && !gpu) {
            for (size_t i = 0; i < sizeof scene_calls / sizeof scene_calls[0]; i++) {
                sb snippet = {0};
                sb_printf(&snippet, "%s($1)", scene_calls[i].name);
                item(c, scene_calls[i].name, CK_FUNCTION, scene_calls[i].form, scene_calls[i].doc, snippet.data);
            }
            return;
        }
        // Page. or Game.Page.: an enum's members
        sb written = {0};
        for (int k = n - 1; k >= 0; k--) sb_printf(&written, "%s" STR_FMT, k == n - 1 ? "" : ".", STR_ARG(DOC->toks[ids[k]].text));
        for (int i = 0; i < A.prog->decls.count; i++) {
            const decl *d = A.prog->decls.items[i];
            if (d->kind != DECL_ENUM || strcmp(name_for(d), written.data) != 0) continue;
            for (int m = 0; m < d->members.count; m++) {
                char value[32];
                snprintf(value, sizeof value, "%lld", (long long)d->members.items[m].number);
                item(c, str_to_cstr(d->members.items[m].name), CK_ENUM_MEMBER, value, NULL, NULL);
            }
            return;
        }
        // ORIGIN. or Tuning.ORIGIN.: a constant's members
        for (int i = 0; i < A.prog->decls.count; i++) {
            const decl *d = A.prog->decls.items[i];
            if (d->kind != DECL_CONST || (strcmp(name_for(d), written.data) != 0 && !str_eq_c(d->qualified, written.data))) {
                continue;
            }
            list_members(c, d->return_type, false, &sc);
            return;
        }
        if (n == 1 && builtin_owner(base)) {
            // This machine's files and its frame's length: local code's, and its functions'
            const bool local_owner = str_eq_c(base, "File") || str_eq_c(base, "Directory")
                               || str_eq_c(base, "Application") || str_eq_c(base, "Storage") || str_eq_c(base, "Web")
                               || str_eq_c(base, "Frame");
            if (local_owner && (!sc.decl || (!sc.decl->is_view && !sc.decl->is_local && sc.decl->kind != DECL_FUNCTION))) return;
            const bool frame_owner = builtin_draw_owner(base) || str_eq_c(base, "GUI") || str_eq_c(base, "GUILayout")
                                  || str_eq_c(base, "Screen");
            const bool frame = sc.decl && (sc.decl->is_view || sc.decl->kind == DECL_FUNCTION);
            if (frame_owner && !frame) return;
            // On the GPU: Math, and what the built-in types have, like Color.white
            type owner;
            if (gpu && !str_eq_c(base, "Math") && !builtin_type_named(base, &owner)) return;
            // Cursor.: views lock it and read whether it holds; the input's Sample only reads
            const bool cursor = str_eq_c(base, "Cursor");
            if (cursor && !frame && !(sc.in_input && !sc.in_sanitize)) return;
            builtin_visit v = {c, cursor && !frame};
            builtin_list_members(base, add_builtin_member, &v);
            if (str_eq_c(base, "Draw")) {
                item(c, "window", CK_PROPERTY, "RenderTarget", "The window as a render target: what Draw's own calls draw into.", NULL);
                item(c, "list", CK_PROPERTY, "DrawList", "The frame's draw list, which an extern function takes to draw into with tide/draw.h.", NULL);
                item(c, "Blit", CK_FUNCTION, BLIT_FORM, BLIT_DOC, "Blit($1, $2, $3)");
            }
            return;
        }
        // Game.Combat.: a namespace containing types and values.
        sb ns = {0};
        for (int k = n - 1; k >= 0; k--) {
            sb_printf(&ns, "%s" STR_FMT, k == n - 1 ? "" : ".", STR_ARG(DOC->toks[ids[k]].text));
        }
        complete_in_namespace(c, (str){ns.data, (int)ns.len}, ORDER_NONE);
        return;
    }
    for (int k = n - 2; k >= 0 && t.kind != TY_ERROR; k--) t = member_type(t, DOC->toks[ids[k]].text);
    // input.jump.pressed, in systems
    const bool edges = n == 2 && p && p->type.kind == TY_INPUT;
    list_members(c, t, edges, &sc);
}

// The current document's file: its namespace and `using`s.
static const unit *doc_unit(void)
{
    for (int i = 0; i < A.prog->units.count; i++) {
        if (A.prog->units.items[i]->src == &DOC->src) return A.prog->units.items[i];
    }
    return NULL;
}

// Can the current document name `d` without its namespace?
static bool visible_by_name(const decl *d)
{
    const str ns = decl_home(d) ? decl_home(d)->ns : (str){"", 0};
    if (ns.len == 0) return true;
    const unit *u = doc_unit();
    if (!u) return false;
    // Its own namespace or one it's inside of
    if (u->ns.len >= ns.len && memcmp(u->ns.ptr, ns.ptr, (size_t)ns.len) == 0
        && (u->ns.len == ns.len || u->ns.ptr[ns.len] == '.')) {
        return true;
    }
    for (int i = 0; i < u->usings.count; i++) {
        if (str_eq(u->usings.items[i], ns)) return true;
    }
    return false;
}

// How the current document writes `d`: Health, or Combat.Health.
static const char *name_for(const decl *d)
{
    return str_to_cstr(visible_by_name(d) ? d->name : d->qualified);
}

static void complete_types(completion *c, const bool components, const bool singletons, const bool input)
{
    for (int i = 0; i < A.prog->decls.count; i++) {
        const decl *d = A.prog->decls.items[i];
        if (components && d->kind == DECL_COMPONENT) item(c, name_for(d), CK_STRUCT, decl_what(d), NULL, NULL);
        if (singletons && d->kind == DECL_SINGLETON) item(c, name_for(d), CK_CLASS, decl_what(d), NULL, NULL);
        if (input && d->kind == DECL_INPUT) item(c, name_for(d), CK_INTERFACE, "input", NULL, NULL);
    }
}

// The components code on one side can spawn and change: local code the local
// world's, match code the match's.
static void complete_components_of(completion *c, const bool local)
{
    for (int i = 0; i < A.prog->decls.count; i++) {
        const decl *d = A.prog->decls.items[i];
        if (d->kind == DECL_COMPONENT && d->is_local == local) item(c, name_for(d), CK_STRUCT, decl_what(d), NULL, NULL);
    }
}

// Events: what handlers handle and Send sends. `builtin` includes the engine's own.
static void complete_events(completion *c, const bool builtin, const bool command)
{
    for (int i = 0; i < A.prog->decls.count; i++) {
        const decl *d = A.prog->decls.items[i];
        if (d->kind != DECL_EVENT || d->is_command != command || (d->builtin && !builtin)) continue;
        item(c, name_for(d), CK_EVENT, decl_what(d), d->builtin ? builtin_event_doc(A.prog, d) : NULL, NULL);
    }
}

// The match's events, the engine's own among them: what a local handler hears
// `verified`, once the server confirmed the tick that sent one.
static void complete_match_events(completion *c)
{
    for (int i = 0; i < A.prog->decls.count; i++) {
        const decl *d = A.prog->decls.items[i];
        if (d->kind != DECL_EVENT || d->is_command || d->is_local) continue;
        item(c, name_for(d), CK_EVENT, decl_what(d), d->builtin ? builtin_event_doc(A.prog, d) : NULL, NULL);
    }
}

// The match's components and, with `singletons`, its singletons: what a view
// takes `verified`, from the world the server has confirmed.
static void complete_match_types(completion *c, const bool singletons)
{
    for (int i = 0; i < A.prog->decls.count; i++) {
        const decl *d = A.prog->decls.items[i];
        if (d->is_local) continue;
        if (d->kind == DECL_COMPONENT) item(c, name_for(d), CK_STRUCT, decl_what(d), NULL, NULL);
        if (singletons && d->kind == DECL_SINGLETON) item(c, name_for(d), CK_CLASS, decl_what(d), NULL, NULL);
    }
}

// The events code on one side can send.
static void complete_events_of(completion *c, const bool local)
{
    for (int i = 0; i < A.prog->decls.count; i++) {
        const decl *d = A.prog->decls.items[i];
        if (d->kind == DECL_EVENT && !d->builtin && (d->is_command ? local : d->is_local == local)) item(c, name_for(d), CK_EVENT, decl_what(d), NULL, NULL);
    }
}

// Structs: field and local types, and values like Stats { ... }. Enums too.
static void complete_structs(completion *c)
{
    for (int i = 0; i < A.prog->decls.count; i++) {
        const decl *d = A.prog->decls.items[i];
        if (d->kind == DECL_STRUCT) item(c, name_for(d), CK_STRUCT, "struct", d == A.prog->random ? RANDOM_DOC : NULL, NULL);
        if (d->kind == DECL_ENUM) { // With how a value of it is written: a member, or from its number
            sb doc = {0};
            if (d->members.count > 0) {
                sb_printf(&doc, "A member is written `%s." STR_FMT "`. ", name_for(d), STR_ARG(d->members.items[0].name));
            }
            describe_enum_conversion(d, &doc);
            item(c, name_for(d), CK_ENUM, "enum", doc.data, NULL);
        }
    }
}

// The game's constants, where a value goes: of type `only`, or of any with TY_ERROR.
static void complete_constants(completion *c, const type_kind only)
{
    for (int i = 0; i < A.prog->decls.count; i++) {
        const decl *d = A.prog->decls.items[i];
        if (d->kind != DECL_CONST || (only != TY_ERROR && d->return_type.kind != only)) continue;
        sb detail = {0};
        sb_printf(&detail, "const " STR_FMT, STR_ARG(d->return_type_name));
        item(c, name_for(d), CK_CONSTANT, detail.data, NULL, NULL);
    }
}

// In settings { ... } between tokens `open` and `last`: each setting it
// doesn't set yet.
static void complete_settings(completion *c, const int open, const int last)
{
    int count;
    const setting *all = settings_list(&count);
    for (int i = 0; i < count; i++) {
        bool set = false;
        for (int k = open + 1; k <= last && !set; k++) {
            set = DOC->toks[k].kind == T_IDENT && DOC->toks[k + 1].kind == T_ASSIGN && str_eq_c(DOC->toks[k].text, all[i].name);
        }
        if (set) continue;
        sb detail = {0};
        const type t = {all[i].kind, all[i].kind == TY_ENUM ? A.prog->prediction : NULL}; // The one enum setting's
        sb_printf(&detail, "%s %s", type_name(t), all[i].name);
        sb snippet = {0};
        sb_printf(&snippet, "%s = $0;", all[i].name);
        item(c, all[i].name, CK_PROPERTY, detail.data, all[i].doc, snippet.data);
    }
}

// Systems or views, for [Before(...)] and [After(...)].
static void complete_systems(completion *c, const bool views)
{
    for (int i = 0; i < A.prog->decls.count; i++) {
        const decl *d = A.prog->decls.items[i];
        if (d->kind == DECL_SYSTEM && d->is_view == views && has_name(d)) {
            item(c, name_for(d), CK_FUNCTION, views ? "view" : "system", NULL, NULL);
        }
    }
}

// Enums define scheduling phases, in Stage, Before and After.
static void complete_phases(completion *c)
{
    for (int i = 0; i < A.prog->decls.count; i++) {
        const decl *d = A.prog->decls.items[i];
        if (d->kind == DECL_ENUM) item(c, name_for(d), CK_ENUM, "scheduling phases", NULL, NULL);
    }
}

// Namespaces as written from the current document: every one for `using`, or
// just the outermost names ("Game" for Game.Combat) where code starts.
static void complete_namespaces(completion *c, const bool full)
{
    for (int i = 0; i < A.prog->units.count; i++) {
        str ns = A.prog->units.items[i]->ns;
        if (ns.len == 0) continue;
        if (!full) {
            const char *dot = memchr(ns.ptr, '.', (size_t)ns.len);
            if (dot) ns.len = (int)(dot - ns.ptr);
        }
        item(c, str_to_cstr(ns), CK_MODULE, "namespace", NULL, NULL);
    }
}

// After `Combat.`: types and values, or the relevant scheduling arguments.
static bool complete_in_namespace(completion *c, const str ns, const order_completion order)
{
    bool any = false;
    for (int i = 0; i < A.prog->units.count; i++) {
        const str u = A.prog->units.items[i]->ns;
        // Namespaces inside it: Game. offers Combat for Game.Combat
        if (u.len > ns.len && memcmp(u.ptr, ns.ptr, (size_t)ns.len) == 0 && u.ptr[ns.len] == '.') {
            str next = {u.ptr + ns.len + 1, u.len - ns.len - 1};
            const char *dot = memchr(next.ptr, '.', (size_t)next.len);
            if (dot) next.len = (int)(dot - next.ptr);
            item(c, str_to_cstr(next), CK_MODULE, "namespace", NULL, NULL);
            any = true;
        }
        if (str_eq(u, ns)) any = true;
    }
    for (int i = 0; i < A.prog->decls.count; i++) {
        const decl *d = A.prog->decls.items[i];
        if (!decl_home(d) || !str_eq(decl_home(d)->ns, ns) || !has_name(d)) continue;
        if (order == ORDER_NONE && d->kind == DECL_SYSTEM) continue;
        if (order == ORDER_REFS && d->kind != DECL_SYSTEM && d->kind != DECL_ENUM) continue;
        if (order == ORDER_PHASE && d->kind != DECL_ENUM) continue;
        if (order == ORDER_OFFSET && (d->kind != DECL_CONST || d->return_type.kind != TY_INT)) continue;
        const int kind = d->kind == DECL_COMPONENT || d->kind == DECL_STRUCT ? CK_STRUCT : d->kind == DECL_INPUT ? CK_INTERFACE
                       : d->kind == DECL_EVENT ? CK_EVENT : d->kind == DECL_ENUM ? CK_ENUM
                       : d->kind == DECL_SYSTEM || is_shader(d) ? CK_FUNCTION : d->kind == DECL_CONST ? CK_CONSTANT : CK_CLASS;
        item(c, str_to_cstr(d->name), kind, decl_keyword(d), NULL, NULL);
    }
    return any;
}

static const char *const value_types[] = {
    "bool", "byte", "int", "long", "int2", "int3", "int4", "float", "float2", "float3", "float4", "quaternion",
    "float2x2", "float3x3", "float4x4", "Entity", "LocalEntity", "PlayerID", "Color", "Rect",
    "Texture", "DepthTexture", "Cubemap", "RenderTarget", "Clip",
};

// Which built-in types to offer: each place takes its own.
enum {
    VT_VALUES = 1,       // Numbers, vectors, matrices, PlayerID, Color and Rect: those with constructors
    VT_BOOL = 2,
    VT_ENTITY = 4,
    VT_LOCAL_ENTITY = 8,
    VT_TEXT = 16,        // string
    VT_LIST = 32,
    VT_GRID = 64,        // Fields of components, singletons and scenes only
    VT_GPU = 128,        // In a frag or vert: of those, only what the GPU has values of
    VT_LOCALS = VT_VALUES | VT_BOOL | VT_ENTITY | VT_TEXT | VT_LIST, // What a local can be, but a LocalEntity
};

static void complete_value_types(completion *c, const unsigned which)
{
    for (size_t i = 0; i < sizeof value_types / sizeof value_types[0]; i++) {
        type t;
        builtin_type_named(str_from(value_types[i]), &t);
        const unsigned kind = t.kind == TY_BOOL           ? VT_BOOL
                            : t.kind == TY_ENTITY         ? VT_ENTITY
                            : t.kind == TY_LOCAL_ENTITY   ? VT_LOCAL_ENTITY
                                                          : VT_VALUES;
        // Its ints are 32 bits, and it has no handles into the world, nor rects
        if ((which & VT_GPU) && (t.kind == TY_BYTE || t.kind == TY_LONG || t.kind == TY_PLAYER || t.kind == TY_RECT || t.kind == TY_RENDER_TARGET
                                 || t.kind == TY_DEPTH_TEXTURE || t.kind == TY_CUBEMAP)) continue; // A depth and a cube are read where they're a field, never a parameter
        if ((t.kind == TY_TEXTURE || t.kind == TY_DEPTH_TEXTURE || t.kind == TY_CUBEMAP || t.kind == TY_RENDER_TARGET)
            && !(which & (VT_LOCAL_ENTITY | VT_GPU))) continue;
        if (which & kind) item(c, value_types[i], CK_STRUCT, "built-in type", builtin_type_doc(t.kind), NULL);
    }
    if (which & VT_TEXT) item(c, "string", CK_STRUCT, "built-in type", builtin_type_doc(TY_STRING), NULL);
    if (which & VT_LIST) {
        item(c, "List", CK_STRUCT, "List<T>", builtin_type_doc(TY_LIST), "List<$1>");
        item(c, "Map", CK_STRUCT, "Map<K, V>", builtin_type_doc(TY_MAP), "Map<$1, $2>");
        item(c, "Set", CK_STRUCT, "Set<K>", builtin_type_doc(TY_SET), "Set<$1>");
    }
    if (which & VT_GRID) {
        item(c, "Grid2", CK_STRUCT, "Grid2<T>", builtin_type_doc(TY_GRID), "Grid2<$1>");
        item(c, "Grid3", CK_STRUCT, "Grid3<T>", builtin_type_doc(TY_GRID), "Grid3<$1>");
    }
}

// What a function's or method's parameter can be, as the checker takes it:
// built-in values, text and lists, structs, enums, components and singletons;
// after `mut`, what can change. A function's last can be its Action.
static void complete_routine_param_types(completion *c, const bool after_mut, const bool function)
{
    complete_value_types(c, VT_LOCALS | VT_LOCAL_ENTITY);
    complete_structs(c);
    complete_types(c, true, true, false);
    complete_namespaces(c, false);
    if (after_mut) return;
    if (function) item(c, "Action", CK_STRUCT, "Action", builtin_type_doc(TY_ACTION), NULL);
    for (int i = 0; i < A.prog->records.count; i++) { // Devices, Keyboard, ..., Button: read-only
        item(c, str_to_cstr(A.prog->records.items[i]->name), CK_CLASS, "built-in device record", NULL, NULL);
    }
}

// Whether a function can run on the GPU, as far as its declaration says: C
// can't, nor a task, nor what draws or takes a block.
static bool gpu_callable(const decl *d)
{
    return d->kind == DECL_FUNCTION && !d->is_extern && !d->is_async && !d->takes_action && !d->draws;
}

// Names and keywords in a frag or vert, which the GPU runs: what it takes, its
// locals, the game's functions and constants, Math, and the types the GPU has
// values of. No loop but a for, which counts; nothing of the world or the
// frame, like Spawn, Draw or Devices.
static void complete_shader_code(completion *c, const scope *sc, const bool statement)
{
    if (statement) {
        static const char *const keywords[] = {"if", "else", "return", "var", "mut", "for", "break", "continue"};
        for (size_t i = 0; i < sizeof keywords / sizeof keywords[0]; i++) item(c, keywords[i], CK_KEYWORD, NULL, NULL, NULL);
    }
    item(c, "true", CK_KEYWORD, NULL, NULL, NULL);
    item(c, "false", CK_KEYWORD, NULL, NULL, NULL);
    if (!statement) item(c, "default", CK_KEYWORD, NULL, NULL, NULL);
    for (int i = sc->locals.count - 1; i >= 0; i--) {
        const stmt *s = sc->locals.items[i];
        item(c, str_to_cstr(s->name), CK_VARIABLE, type_name(s->type), NULL, NULL);
    }
    for (int i = 0; i < sc->decl->params.count; i++) {
        const param *p = &sc->decl->params.items[i];
        if (p->name.len == 0) continue;
        sb detail = {0};
        format_param(p, &detail);
        item(c, str_to_cstr(p->name), CK_VARIABLE, detail.data, NULL, NULL);
    }
    for (int i = 0; i < A.prog->decls.count; i++) {
        const decl *d = A.prog->decls.items[i];
        if (gpu_callable(d)) complete_routine(c, d, name_for(d));
    }
    complete_constants(c, TY_ERROR);
    complete_value_types(c, VT_VALUES | VT_GPU | (statement ? VT_BOOL : 0));
    complete_structs(c);
    item(c, "Math", CK_MODULE, "Math functions and constants", NULL, NULL);
    complete_namespaces(c, false);
}

// Where an argument starts in a call of one of the game's functions or
// methods: the names its parameters can be passed by, `crit:`, for those the
// call hasn't given yet, by place or by name. `open` is the call's '(' and
// `last` the token before the cursor.
static void complete_argument_names(completion *c, const int open, const int last)
{
    if (DOC->toks[open - 1].kind != T_IDENT) return;
    const str name = DOC->toks[open - 1].text;
    const bool method = open >= 2 && DOC->toks[open - 2].kind == T_DOT;
    // What the call wrote so far: arguments by place, then names
    int unnamed = 0;
    VEC(str) named = {0};
    for (int i = open + 1, depth = 0, start = open + 1; i <= last; i++) {
        const tok_kind k = DOC->toks[i].kind;
        if (k == T_LPAREN || k == T_LBRACKET || k == T_LBRACE) depth++;
        else if (k == T_RPAREN || k == T_RBRACKET || k == T_RBRACE) depth--;
        if (k != T_COMMA || depth != 0) continue; // Each ',' ends an argument
        if (DOC->toks[start].kind == T_IDENT && DOC->toks[start + 1].kind == T_COLON) vec_push(named, DOC->toks[start].text);
        else if (named.count == 0) unnamed++;
        start = i + 1;
    }
    for (int i = 0; i < A.prog->decls.count; i++) {
        const decl *d = A.prog->decls.items[i];
        for (int k = -1; k < d->methods.count; k++) {
            const decl *m = k < 0 ? d : d->methods.items[k];
            if (!is_routine(m) || m->is_operator || !str_eq(m->name, name) || (method && !m->owner)) continue;
            const int count = m->params.count - (m->takes_action ? 1 : 0); // An Action is the block after the call
            for (int p = unnamed; p < count; p++) {
                const param *prm = &m->params.items[p];
                bool given = false;
                for (int n = 0; n < named.count; n++) given |= str_eq(named.items[n], prm->name);
                if (given) continue;
                sb label = {0};
                sb detail = {0};
                sb insert = {0};
                sb_printf(&label, STR_FMT ":", STR_ARG(prm->name));
                format_param(prm, &detail);
                sb_printf(&insert, STR_FMT ": ", STR_ARG(prm->name));
                item(c, label.data, CK_VARIABLE, detail.data, "Passes this argument by its parameter's name.", insert.data);
            }
        }
    }
}

// Names and keywords that can start an expression or statement.
static void complete_expression(completion *c, const loc at, const bool statement)
{
    const scope sc = scope_at(at);
    if (is_shader(sc.decl)) {
        complete_shader_code(c, &sc, statement);
        return;
    }
    const bool view = sc.decl && sc.decl->is_view;

    if (statement) {
        static const char *const keywords[] = {"if", "else", "return", "var", "mut", "switch", "case", "default", "break",
                                               "while", "for", "foreach", "continue"};
        for (size_t i = 0; i < sizeof keywords / sizeof keywords[0]; i++) item(c, keywords[i], CK_KEYWORD, NULL, NULL, NULL);
        // Every cell of a grid or element of a list at once, in a system, view or handler that isn't async
        if (sc.decl && sc.decl->kind == DECL_SYSTEM && !sc.decl->is_async) {
            item(c, "parallel", CK_KEYWORD, "parallel (var at in cells) { ... }",
                 "Goes through a grid's cells or a list's elements (by index), or with `by`, their blocks, all at once: "
                 "each step reads them as the loop found them and changes only its own cell, element or block.",
                 NULL);
        }
        // A function that can fail ends with its error
        if (sc.decl && is_routine(sc.decl) && sc.decl->fails.kind != TY_VOID) {
            item(c, "fail", CK_KEYWORD, NULL, "Ends the function with an error.", NULL);
        }
    }
    item(c, "true", CK_KEYWORD, NULL, NULL, NULL);
    item(c, "false", CK_KEYWORD, NULL, NULL, NULL);
    item(c, "null", CK_KEYWORD, NULL, "Nothing, for a T?.", NULL);
    if (sc.decl && is_routine(sc.decl) && sc.decl->fails.kind != TY_VOID) {
        item(c, "try", CK_KEYWORD, NULL, "A call's value, or its error passed on to the caller.", NULL);
    }
    // Async code waits
    if (sc.decl && sc.decl->is_async) {
        item(c, "await", CK_KEYWORD, NULL, "Waits for an async call's value, or for Wait: the task goes on from here.", NULL);
        item(c, "Wait", CK_MODULE, "Ticks, frames and seconds to wait for", NULL, NULL);
    }
    if (!statement) item(c, "default", CK_KEYWORD, NULL, NULL, NULL); // A statement's list has it, for switches
    // this: the entity the code runs for, or whose component a method is called on
    const type entity = this_type(&sc);
    if (entity.kind != TY_ERROR) {
        item(c, "this", CK_KEYWORD, type_name(entity),
             sc.decl->kind == DECL_METHOD ? "The entity whose component this is." : "The entity the code runs for.", NULL);
    }

    for (int i = sc.locals.count - 1; i >= 0; i--) {
        const stmt *s = sc.locals.items[i];
        item(c, str_to_cstr(s->name), CK_VARIABLE, type_name(s->type), NULL, NULL);
    }
    if (sc.decl) {
        for (int i = 0; i < sc.decl->params.count && !sc.in_sanitize; i++) {
            const param *p = &sc.decl->params.items[i];
            if (p->name.len == 0) continue;
            sb detail = {0};
            format_param(p, &detail);
            item(c, str_to_cstr(p->name), CK_VARIABLE, detail.data, NULL, NULL);
        }
        if (sc.fields_of) {
            for (int i = 0; i < sc.fields_of->fields.count; i++) {
                const field *f = &sc.fields_of->fields.items[i];
                if (!f->hidden) item(c, str_to_cstr(f->name), CK_FIELD, type_name(f->type), NULL, NULL);
            }
            for (int i = 0; i < sc.fields_of->methods.count; i++) {
                const decl *m = sc.fields_of->methods.items[i];
                if (!m->is_operator && !m->is_interpolate) complete_routine(c, m, NULL);
            }
        }
    }
    for (int i = 0; i < A.prog->decls.count; i++) {
        const decl *d = A.prog->decls.items[i];
        if (d->kind == DECL_FUNCTION && has_name(d)) complete_routine(c, d, name_for(d));
    }
    complete_constants(c, TY_ERROR);

    const bool routine = sc.decl && (sc.decl->kind == DECL_METHOD || sc.decl->kind == DECL_FUNCTION);
    if (sc.decl && !sc.in_input && !routine) {
        const bool local = view || sc.decl->is_local;
        item(c, "Spawn", CK_FUNCTION, local ? "Spawn(local components...) -> LocalEntity" : "Spawn(components...) -> Entity",
             local ? "Creates a local entity with these components, at the end of the frame."
                   : "Creates an entity with these components.",
             "Spawn($1)");
        item(c, "Send", CK_FUNCTION, "Send(event)",
             local ? "Sends a local event, or submits a command to the match." : "Sends an event to the whole world, handled at the end of the tick.",
             "Send($1)");
        complete_components_of(c, local);
        complete_events_of(c, local);
        item(c, "Scene", CK_MODULE, "Loads and unloads scenes", NULL, NULL);
    }
    // Session.Start and the like are statements of their own.
    if (statement && decides_session(&sc)) {
        item(c, "Session", CK_MODULE, "Starts, joins and leaves matches", NULL, NULL);
        item(c, "Clipboard", CK_MODULE, "This machine's clipboard", NULL, NULL);
    }
    if (statement) item(c, "Debug", CK_MODULE, "Log prints a line; Assert stops the program when a condition fails", NULL, NULL);
    // Where a statement starts, what a local can be; elsewhere, what makes a value.
    const bool local_code = view || (sc.decl && sc.decl->is_local);
    item(c, "Path", CK_MODULE, "Pure path operations", NULL, NULL);
    if (local_code || (routine && sc.decl->kind == DECL_FUNCTION)) {
        item(c, "File", CK_MODULE, "Reads and writes this machine's files", NULL, NULL);
        item(c, "Directory", CK_MODULE, "Lists and manages this machine's directories", NULL, NULL);
        item(c, "Application", CK_MODULE, "Persistent save and temporary cache paths", NULL, NULL);
        item(c, "Frame", CK_MODULE, "This machine's frame: how long it is, for what isn't the match's", NULL, NULL);
        if (sc.decl->is_async) item(c, "Storage", CK_MODULE, "Await persistent storage commits", NULL, NULL);
        if (sc.decl->is_async) item(c, "Web", CK_MODULE, "Await a URL's bytes, fetched by this machine", NULL, NULL);
    }
    complete_value_types(c, !statement ? VT_VALUES : local_code ? VT_LOCALS | VT_LOCAL_ENTITY : VT_LOCALS);
    complete_structs(c);
    item(c, "Math", CK_MODULE, "Math functions and constants", NULL, NULL);
    complete_namespaces(c, false);
    // What views draw with, which the functions they call can use too.
    if (view || (routine && sc.decl->kind == DECL_FUNCTION)) {
        item(c, "Draw", CK_MODULE, "Drawing into the window: shapes, text, meshes, cameras and settings", NULL, NULL);
        item(c, "GUI", CK_MODULE, "Widgets at a Rect", NULL, NULL);
        item(c, "GUILayout", CK_MODULE, "Widgets laid out automatically", NULL, NULL);
        item(c, "Screen", CK_MODULE, "The window's size, in pixels, and Flip for the devices' positions", NULL, NULL);
    }
    // The mouse's cursor: views lock it, and they and the input's Sample read whether it's locked
    if (view || (routine && sc.decl->kind == DECL_FUNCTION) || (sc.in_input && !sc.in_sanitize)) {
        item(c, "Cursor", CK_MODULE, "The mouse's cursor: its lock, for a camera that looks around", NULL, NULL);
    }
    // content(): a function's Action
    for (int i = 0; sc.decl && i < sc.decl->params.count; i++) {
        const param *p = &sc.decl->params.items[i];
        if (p->type.kind == TY_ACTION) item(c, str_to_cstr(p->name), CK_FUNCTION, "Action", "Runs the caller's block.", "$0();");
    }
}

static void complete_declarations(completion *c)
{
    item(c, "component", CK_SNIPPET, "component Name { fields }", NULL, "component ${1:Name}\n{\n    $0\n}");
    item(c, "singleton", CK_SNIPPET, "singleton Name { fields }", NULL, "singleton ${1:Name}\n{\n    $0\n}");
    item(c, "struct", CK_SNIPPET, "struct Name { fields }", "A value type for fields and locals.",
         "struct ${1:Name}\n{\n    $0\n}");
    item(c, "enum", CK_SNIPPET, "enum Name { Members }", "A type with named values, like 'enum Page { Title, Options }'.",
         "enum ${1:Name}\n{\n    $0\n}");
    item(c, "system", CK_SNIPPET, "system Name(parameters) { ... }", NULL, "system ${1:Name}($2)\n{\n    $0\n}");
    item(c, "view", CK_SNIPPET, "view Name(parameters) { ... }", "Runs once per frame and draws.",
         "view ${1:Name}($2)\n{\n    $0\n}");
    item(c, "frag", CK_SNIPPET, "frag Name(Vertex vertex, out Color color, parameters) { ... }",
         "Runs on the GPU for each pixel of a mesh a view draws through it, and writes the pixel's color.",
         "frag ${1:Name}(${2:Vertex} vertex, out Color color$3)\n{\n    $0\n}");
    item(c, "vert", CK_SNIPPET, "vert Name(mut Vertex vertex, parameters) { ... }",
         "Runs on the GPU for each vertex of a mesh a view draws through it, and changes it in place.",
         "vert ${1:Name}(mut ${2:Vertex} vertex$3)\n{\n    $0\n}");
    item(c, "input", CK_SNIPPET, "input Name { fields; Sample }", "What a player sends each tick.",
         "input ${1:Name}\n{\n    $0\n\n    Sample(Devices devices)\n    {\n    }\n}");
    item(c, "event", CK_SNIPPET, "event Name { fields }", "Something that happened, sent with Send.",
         "event ${1:Name}\n{\n    $0\n}");
    item(c, "command", CK_SNIPPET, "command Name { fields }", "A player's untrusted submission, sent from local code to the match.",
         "command ${1:Name}\n{\n    $0\n}");
    item(c, "command handler", CK_SNIPPET, "command(Command payload, PlayerID sender) Name(parameters) { ... }",
         "Checks the authenticated sender and untrusted payload before changing match state.",
         "command(${1:Command} ${2:payload}, PlayerID sender) ${3:Name}($4)\n{\n    $0\n}");
    item(c, "scene", CK_SNIPPET, "scene Name { fields }", "Entities that load and unload together, like a level.",
         "scene ${1:Name}\n{\n    $0\n}");
    item(c, "local scene", CK_SNIPPET, "local scene Name { fields }", "A scene on this machine only, like a menu.",
         "local scene ${1:Name}\n{\n    $0\n}");
    item(c, "local component", CK_SNIPPET, "local component Name { fields }",
         "This machine's own: views change it, and the match never sees it.", "local component ${1:Name}\n{\n    $0\n}");
    item(c, "local singleton", CK_SNIPPET, "local singleton Name { fields }",
         "This machine's own, like settings or a menu's state.", "local singleton ${1:Name}\n{\n    $0\n}");
    item(c, "local event", CK_SNIPPET, "local event Name { fields }", "Sent from views, handled at the end of the frame.",
         "local event ${1:Name}\n{\n    $0\n}");
    item(c, "event handler", CK_SNIPPET, "event(Event e) Name(parameters) { ... }",
         "Runs when the event is sent, at the end of the tick.", "event(${1:Event} ${2:e}) ${3:Name}($4)\n{\n    $0\n}");
    item(c, "function", CK_SNIPPET, "Type Name(parameters) { ... }", "Code other code calls, like 'float Heal(mut Stats stats)'.",
         "${1:void} ${2:Name}($3)\n{\n    $0\n}");
    item(c, "async function", CK_SNIPPET, "async Type Name(parameters) { ... }",
         "A function that can wait, with 'await': calling it starts a task that goes on by itself.",
         "async ${1:void} ${2:Name}($3)\n{\n    $0\n}");
    item(c, "async event handler", CK_SNIPPET, "async event(Event e) Name(parameters) { ... }",
         "A handler that can wait, with 'await': each event starts a task.",
         "async event(${1:Event} ${2:e}) ${3:Name}($4)\n{\n    $0\n}");
    item(c, "extern", CK_SNIPPET, "extern Type Name(parameters);",
         "A function written in C, which the game's C files or libraries define.", "extern ${1:void} ${2:Name}($3);");
    item(c, "extern system", CK_SNIPPET, "extern system Name(mut Component value);",
         "A system written in C: called once a tick for each run of rows of the entities that match, with their components as "
         "arrays (tide/rows.h) and the singletons it takes.",
         "extern system ${1:Name}(${2:mut Component value});");
    item(c, "const", CK_SNIPPET, "const Type NAME = value;", "A value code reads by name, the same on every machine.",
         "const ${1:int} ${2:NAME} = $0;");
    item(c, "settings", CK_SNIPPET, "settings { name = value; }", "The engine's settings for the game, like its tickRate.",
         "settings\n{\n    $0\n}");
    item(c, "virtual", CK_KEYWORD, "virtual system Name(parameters) { ... }",
         "Before a declaration with code: another package can replace it, with an `override` of the same name and parameters.",
         NULL);
    item(c, "override", CK_KEYWORD, "override system Name(parameters) { ... }",
         "Before a declaration with code: it replaces the `virtual` one of that name and those parameters, in another package.",
         NULL);
    item(c, "namespace", CK_KEYWORD, "namespace Name;", "The namespace of everything in this file. Goes at the top.",
         "namespace ${1:Name};");
    item(c, "using", CK_KEYWORD, "using Name;", "Names from another namespace, without writing it. Goes at the top.",
         "using ${1:Name};");
}

// What `virtual` goes before: the declarations with code.
static void complete_code_declarations(completion *c)
{
    item(c, "system", CK_SNIPPET, "system Name(parameters) { ... }", NULL, "system ${1:Name}($2)\n{\n    $0\n}");
    item(c, "view", CK_SNIPPET, "view Name(parameters) { ... }", "Runs once per frame and draws.",
         "view ${1:Name}($2)\n{\n    $0\n}");
    item(c, "frag", CK_SNIPPET, "frag Name(Vertex vertex, out Color color, parameters) { ... }", NULL,
         "frag ${1:Name}(${2:Vertex} vertex, out Color color$3)\n{\n    $0\n}");
    item(c, "vert", CK_SNIPPET, "vert Name(mut Vertex vertex, parameters) { ... }", NULL,
         "vert ${1:Name}(mut ${2:Vertex} vertex$3)\n{\n    $0\n}");
    item(c, "event handler", CK_SNIPPET, "event(Event e) Name(parameters) { ... }", NULL,
         "event(${1:Event} ${2:e}) ${3:Name}($4)\n{\n    $0\n}");
    item(c, "local event handler", CK_SNIPPET, "local event(Event e) Name(parameters) { ... }", NULL,
         "local event(${1:Event} ${2:e}) ${3:Name}($4)\n{\n    $0\n}");
    item(c, "command handler", CK_SNIPPET, "command(Command payload, PlayerID sender) Name(parameters) { ... }", NULL,
         "command(${1:Command} ${2:payload}, PlayerID sender) ${3:Name}($4)\n{\n    $0\n}");
    item(c, "function", CK_SNIPPET, "Type Name(parameters) { ... }", NULL, "${1:void} ${2:Name}($3)\n{\n    $0\n}");
    item(c, "async function", CK_SNIPPET, "async Type Name(parameters) { ... }", NULL,
         "async ${1:void} ${2:Name}($3)\n{\n    $0\n}");
}

// A type as the current document writes it, for a declaration it's about to
// write: Health, or Combat.Health where it can't name it alone.
static void put_type_here(sb *out, const type t, const str written)
{
    switch (t.kind) {
    case TY_OPTIONAL:
        put_type_here(out, t.decl->fields.items[0].type, (str){"", 0});
        sb_put(out, "?");
        return;
    case TY_LIST: case TY_SET: case TY_GRID:
        sb_put(out, t.kind == TY_LIST ? "List<" : t.kind == TY_SET ? "Set<" : t.decl->dims == 3 ? "Grid3<" : "Grid2<");
        put_type_here(out, t.decl->fields.items[0].type, (str){"", 0});
        sb_put(out, ">");
        return;
    case TY_MAP:
        sb_put(out, "Map<");
        put_type_here(out, t.decl->fields.items[0].type, (str){"", 0});
        sb_put(out, ", ");
        put_type_here(out, t.decl->fields.items[1].type, (str){"", 0});
        sb_put(out, ">");
        return;
    case TY_COMPONENT: case TY_SINGLETON: case TY_INPUT: case TY_STRUCT: case TY_EVENT: case TY_ENUM:
        sb_put(out, t.decl->builtin ? str_to_cstr(t.decl->name) : name_for(t.decl));
        return;
    case TY_ERROR: case TY_RECORD: case TY_WORLD: case TY_ACTION: case TY_DRAW_LIST: case TY_QUERY:
        sb_printf(out, STR_FMT, STR_ARG(written)); // As its declaration writes it: the engine's own, the same everywhere
        return;
    default:
        sb_put(out, type_name(t));
        return;
    }
}

static void put_param_here(sb *out, const param *p)
{
    if (p->mode == PARAM_QUERY) {
        sb_put(out, "query (");
        for (int i = 0; i < p->query.count; i++) {
            if (i) sb_put(out, ", ");
            put_param_here(out, &p->query.items[i]);
        }
        sb_printf(out, ") " STR_FMT, STR_ARG(p->name));
        return;
    }
    sb_put(out, p->is_out ? "out " : p->mode == PARAM_MUT ? "mut " : p->mode == PARAM_WITH ? "with "
              : p->mode == PARAM_WITHOUT ? "without " : "");
    if (p->verified) sb_put(out, "verified ");
    if (p->ownership) sb_put(out, "ownership");
    else put_type_here(out, p->type, p->type_name);
    if (p->name.len > 0) sb_printf(out, " " STR_FMT, STR_ARG(p->name));
    if (p->default_value) sb_printf(out, " = " STR_FMT, STR_ARG(p->default_text));
}

// After `override`: the virtual declarations of other packages that nothing
// replaces yet, each as the override this file would write for it, with its
// name as it's written from here and its signature filled in.
static void complete_overrides(completion *c)
{
    const char *package = DOC->src.package;
    for (int i = 0; i < A.prog->decls.count; i++) {
        const decl *d = A.prog->decls.items[i];
        for (int k = -1; k < d->methods.count; k++) {
            const decl *v = k < 0 ? d : d->methods.items[k];
            if (!v->is_virtual || v->builtin || !v->unit) continue;
            const char *theirs = v->unit->src->package;
            if (theirs == package || (theirs && package && strcmp(theirs, package) == 0)) continue; // Its own: it changes it there
            sb header = {0};
            int first = 0;
            if (v->kind == DECL_SYSTEM && v->is_handler && v->params.count > 0) {
                sb_printf(&header, "%s%s%s(", v->is_local ? "local " : "", v->is_async ? "async " : "",
                          v->is_command ? "command" : "event");
                put_param_here(&header, &v->params.items[0]);
                if (v->is_command && v->params.count > 1) {
                    sb_put(&header, ", ");
                    put_param_here(&header, &v->params.items[1]);
                }
                sb_put(&header, ") ");
                first = v->is_command ? 2 : 1;
            } else if (v->kind == DECL_SYSTEM) {
                sb_put(&header, v->is_view ? "view " : "system ");
            } else if (v->kind == DECL_SHADER) {
                sb_put(&header, v->is_frag ? "frag " : "vert ");
            } else {
                sb_printf(&header, "%s%s", v->is_async ? "async " : "", v->is_mut_method ? "mut " : "");
                if (v->return_type.kind == TY_VOID) sb_put(&header, "void");
                else put_type_here(&header, v->return_type, v->return_type_name);
                sb_put(&header, " ");
            }
            if (v->owner) sb_printf(&header, "%s.", name_for(v->owner));
            sb_printf(&header, "%s(", v->owner ? str_to_cstr(v->name) : name_for(v));
            for (int j = first; j < v->params.count; j++) {
                if (j > first) sb_put(&header, ", ");
                put_param_here(&header, &v->params.items[j]);
            }
            sb_put(&header, ")");
            if (v->fails.kind != TY_VOID && v->fails.kind != TY_ERROR) {
                sb_put(&header, " fails ");
                put_type_here(&header, v->fails, v->fails_name);
            }
            sb snippet = {0};
            sb_printf(&snippet, "%s\n{\n    $0\n}", header.data);
            sb detail = {0};
            const char *of = theirs ? theirs : NULL;
            if (of) sb_printf(&detail, "replaces package %s's", of);
            else sb_put(&detail, "replaces the game's");
            item(c, header.data, CK_SNIPPET, detail.data, NULL, snippet.data);
        }
    }
}

// "PlayerInput" -> "playerInput", the usual name for a parameter of that type.
static void complete_param_name(completion *c, const str type_name_)
{
    if (type_name_.len == 0 || !isupper((unsigned char)type_name_.ptr[0])) return;
    char *name = str_to_cstr(type_name_);
    name[0] = (char)tolower((unsigned char)name[0]);
    item(c, name, CK_VARIABLE, NULL, NULL, NULL);
}

// Whether the game declares a type named `name`: Pixel and Instance are only
// built in where it doesn't.
static bool game_type_named(const char *name)
{
    for (int i = 0; i < A.prog->decls.count; i++) {
        const decl *d = A.prog->decls.items[i];
        const bool type = d->kind != DECL_SYSTEM && d->kind != DECL_FUNCTION && d->kind != DECL_CONST && d->kind != DECL_ASSET
                       && !is_shader(d);
        if (type && !d->builtin && str_eq_c(d->name, name)) return true;
    }
    return false;
}

// A frag's or vert's parameters, in the parentheses opened at token `open`,
// after token `last`. The vertex comes first, a struct with a position, which
// a vert takes mut; what each writes is `out`, a frag's pixel, its `out Color`,
// and the structs a vert hands its frag; the rest is what the engine supplies:
// components, singletons, Pixel (a frag's) and Instance.
static void complete_shader_header(completion *c, const int open, const int last)
{
    const token *toks = DOC->toks;
    const bool frag = str_eq_c(toks[open - 2].text, "frag");
    const bool after_mut = toks[last].kind == T_MUT;
    // `out`, a word only here: after '(' or ',', with nothing after it yet
    const bool after_out = toks[last].kind == T_IDENT && str_eq_c(toks[last].text, "out")
                        && (toks[last - 1].kind == T_COMMA || toks[last - 1].kind == T_LPAREN);
    const tok_kind before = toks[after_mut || after_out ? last - 1 : last].kind; // What the parameter starts after
    if (before != T_LPAREN && before != T_COMMA) {
        if (toks[last].kind == T_IDENT) complete_param_name(c, toks[last].text);
        return;
    }
    if (before == T_LPAREN) {
        if (frag && after_mut) return; // A frag reads its vertex
        if (!frag && !after_mut) item(c, "mut", CK_KEYWORD, "A vert changes its vertex in place", NULL, NULL);
        for (int i = 0; i < A.prog->decls.count; i++) { // What a mesh's list can hold: a struct with a position
            const decl *d = A.prog->decls.items[i];
            for (int k = 0; d->kind == DECL_STRUCT && k < d->fields.count; k++) {
                if (!str_eq_c(d->fields.items[k].name, "position")) continue;
                item(c, name_for(d), CK_STRUCT, "struct", "A mesh's vertex: it has a `position`.", NULL);
            }
        }
        complete_namespaces(c, false);
        return;
    }
    if (after_mut) return; // Only a vert's vertex is, its first
    if (after_out) {
        if (frag) {
            item(c, "Color", CK_STRUCT,
                 "A color the frag writes: the pixel's, then one for each color image after its target's first", NULL, NULL);
            return;
        }
        for (int i = 0; i < A.prog->decls.count; i++) { // What a vert can hand its frag: a struct
            const decl *d = A.prog->decls.items[i];
            if (d->kind == DECL_STRUCT && has_name(d)) {
                item(c, name_for(d), CK_STRUCT, "struct", "What the vert writes for its frag, which reads it blended.", NULL);
            }
        }
        complete_namespaces(c, false);
        return;
    }
    bool writes = false; // It has its `out Color` already: another is one more output
    for (int k = open + 2; k <= last; k++) {
        writes |= toks[k].kind == T_IDENT && str_eq_c(toks[k].text, "out") && toks[k - 1].kind == T_COMMA;
    }
    if (frag && !writes) item(c, "out", CK_KEYWORD, "out Color color: the pixel's color, which the frag writes", NULL, NULL);
    if (frag && writes) {
        item(c, "out", CK_KEYWORD, "out Color glow: one more color the frag writes, into the next color image of its target",
             NULL, NULL);
    }
    if (!frag) item(c, "out", CK_KEYWORD, "out FromLamp lamp: a struct the vert writes for its frag, which reads it blended", NULL, NULL);
    complete_types(c, true, true, false);
    item(c, "Texture", CK_STRUCT, "Read-only image supplied by the draw", builtin_type_doc(TY_TEXTURE), NULL);
    if (frag && !game_type_named("Pixel")) item(c, "Pixel", CK_CLASS, "built-in", shader_record_doc(A.prog->pixel), NULL);
    if (!game_type_named("Instance")) item(c, "Instance", CK_CLASS, "built-in", shader_record_doc(A.prog->instance), NULL);
    complete_namespaces(c, false);
}

// The frags and verts a Draw.Mesh can name after its other arguments: those
// that run on its mesh's vertex, `vertex`, or all of them where it isn't known.
static void complete_shaders(completion *c, const decl *vertex)
{
    for (int i = 0; i < A.prog->decls.count; i++) {
        const decl *d = A.prog->decls.items[i];
        if (!is_shader(d) || !has_name(d)) continue;
        const type on = d->params.count > 0 ? d->params.items[0].type : (type){TY_ERROR, NULL};
        if (vertex && on.kind == TY_STRUCT && on.decl != vertex) continue;
        sb detail = {0};
        format_header(d, &detail);
        item(c, name_for(d), CK_FUNCTION, detail.data,
             d->is_frag ? "Runs on the GPU for each pixel of the mesh, and writes its color. It comes last, after a vert."
                        : "Runs on the GPU for each vertex of the mesh, and changes it in place. A frag comes after it.",
             NULL);
    }
}

// The values a draw can give the vert or the frag it names, after their
// names: each struct one of them takes as a read-only parameter that no vert
// writes, as `Swell { }`.
static void complete_draw_values(completion *c, const decl *shader)
{
    for (int k = 1; shader && k < shader->params.count; k++) {
        const param *p = &shader->params.items[k];
        if (p->type.kind != TY_STRUCT || p->is_out || p->mode == PARAM_MUT || handed_param(shader, p)) continue;
        sb detail = {0};
        sb_printf(&detail, "%s { ... }", name_for(p->type.decl));
        sb doc = {0};
        sb_printf(&doc, "The values this draw gives %s `%s`, which reads them through its `" STR_FMT "` parameter: the same "
                        "for the whole draw, and only the fields it reads are sent.", shader->is_frag ? "frag" : "vert",
                  name_for(shader), STR_ARG(p->name));
        sb snippet = {0};
        sb_printf(&snippet, "%s { $1 }", name_for(p->type.decl));
        item(c, name_for(p->type.decl), CK_STRUCT, detail.data, doc.data, snippet.data);
    }
}

// Contexts the cursor can be in, found by scanning tokens.
typedef enum context_kind {
    CTX_TOP, CTX_DATA, CTX_HEADER, CTX_CODE, CTX_LITERAL, CTX_ATTRIBUTE, CTX_ATTRIBUTE_ARGS,
    CTX_FIELD_ATTRIBUTE, CTX_FIELD_ATTRIBUTE_ARGS, // [Clamp(-1, 1)] before an input field
} context_kind;

// Whose parameters a header's parentheses hold.
typedef enum header_kind {
    HEADER_NONE,
    HEADER_SYSTEM,  // A system's or view's, or a handler's after its trigger: system Move(mut Body body)
    HEADER_TRIGGER, // A handler's trigger: event(Hit hit)
    HEADER_ROUTINE, // A function's or method's: void Heal(mut Stats stats)
    HEADER_SAMPLE,  // The input's Sample: devices and local singletons; Sanitize takes nothing
    HEADER_SHADER,  // A frag's or vert's: frag Shade(BoxVertex vertex, out Color color, Sun sun)
    HEADER_QUERY,   // A system's second candidate set: query (Body body, with Fighter)
} header_kind;

typedef struct frame {
    context_kind kind;
    int open; // The token that opened it
    header_kind header; // CTX_HEADER's
} frame;

static bool is_word(const token *t);

// The header a '(' at the top level opens, at token `i`.
static header_kind top_header(const int i)
{
    if (i < 1) return HEADER_NONE;
    const token *name = &DOC->toks[i - 1];
    if (name->kind == T_IDENT && (str_eq_c(name->text, "event") || str_eq_c(name->text, "command"))) return HEADER_TRIGGER;
    if (i < 2) return HEADER_ROUTINE;
    int first = i - 1; // An override's name can be qualified: `override system Combat.Move(`
    while (first >= 2 && DOC->toks[first - 1].kind == T_DOT && DOC->toks[first - 2].kind == T_IDENT) first -= 2;
    if (first < 1) return HEADER_ROUTINE;
    const token *keyword = &DOC->toks[first - 1];
    if (keyword->kind == T_RPAREN || keyword->kind == T_SYSTEM) return HEADER_SYSTEM; // event(Hit hit) TakeHit(
    if (keyword->kind == T_IDENT && (str_eq_c(keyword->text, "frag") || str_eq_c(keyword->text, "vert"))) return HEADER_SHADER;
    return keyword->kind == T_IDENT && str_eq_c(keyword->text, "view") ? HEADER_SYSTEM : HEADER_ROUTINE;
}

// Whether the handler whose trigger opens at token `open` is local: `local
// event(`, with `async` before or after the word.
static bool local_trigger(const int open)
{
    for (int k = open - 2; k >= 0 && k >= open - 3 && DOC->toks[k].kind == T_IDENT; k--) {
        if (str_eq_c(DOC->toks[k].text, "local")) return true;
        if (!str_eq_c(DOC->toks[k].text, "async")) return false;
    }
    return false;
}

// Whether the handler whose parameters open at token `open` is local:
// `local event(Hit hit) ShowHit(`.
static bool local_handler(const int open)
{
    if (open < 2 || DOC->toks[open - 2].kind != T_RPAREN) return false;
    int depth = 0;
    for (int k = open - 2; k >= 0; k--) {
        if (DOC->toks[k].kind == T_RPAREN) depth++;
        else if (DOC->toks[k].kind == T_LPAREN && --depth == 0) return local_trigger(k);
    }
    return false;
}

// The header a '(' in a type's body opens, at token `i`: the input's Sample or
// Sanitize, or a method's (`bool IsDead(`, `Money operator +(`). None for a
// value, like a field's default.
static header_kind member_header(const int i)
{
    if (i < 2) return HEADER_NONE;
    const token *name = &DOC->toks[i - 1];
    const token *before = &DOC->toks[i - 2];
    if (name->kind == T_IDENT && (str_eq_c(name->text, "Sample") || str_eq_c(name->text, "Sanitize"))) return HEADER_SAMPLE;
    if (name->kind == T_IDENT && (before->kind == T_IDENT || before->kind == T_QUESTION || before->kind == T_GT)) {
        return HEADER_ROUTINE;
    }
    return before->kind == T_IDENT && str_eq_c(before->text, "operator") && !is_word(name) ? HEADER_ROUTINE : HEADER_NONE;
}

// Type Name( at column 1, the type maybe qualified: a function, or in a
// type's body, a method.
static bool starts_function(const int i)
{
    const token *t = &DOC->toks[i];
    if (t->kind != T_IDENT || t->at.col != 1) return false;
    int k = i + 1;
    while (DOC->toks[k].kind == T_DOT && DOC->toks[k + 1].kind == T_IDENT) k += 2;
    if (DOC->toks[k].kind == T_QUESTION) k++; // int? Find(
    return DOC->toks[k].kind == T_IDENT && DOC->toks[k + 1].kind == T_LPAREN;
}

// Whether the `{` at token `i` follows `) fails ParseError`: a function's body.
static bool after_fails(const int i)
{
    for (int k = i - 1; k > 0; k--) {
        const token *t = &DOC->toks[k];
        if (t->kind == T_IDENT && str_eq_c(t->text, "fails")) return DOC->toks[k - 1].kind == T_RPAREN;
        if (t->kind != T_IDENT && t->kind != T_DOT && t->kind != T_LT && t->kind != T_GT) return false;
    }
    return false;
}

// `virtual` or `override` at column 1, before a declaration's word or a
// function's type: `virtual system`, `override float Combat.Damage(`.
static bool starts_replace_word(const int i)
{
    const token *t = &DOC->toks[i];
    if (t->kind != T_IDENT || t->at.col != 1 || (!str_eq_c(t->text, "virtual") && !str_eq_c(t->text, "override"))) return false;
    const tok_kind next = DOC->toks[i + 1].kind;
    return next == T_IDENT || next == T_SYSTEM || next == T_MUT;
}

static bool starts_declaration(const int i)
{
    const token *t = &DOC->toks[i];
    if (t->kind == T_COMPONENT || t->kind == T_SINGLETON || t->kind == T_SYSTEM) return true;
    if (starts_replace_word(i)) return true;
    if (t->kind == T_LBRACKET && t->at.col == 1) return !attributes_before_field(DOC->toks, i); // Attributes
    if (t->kind == T_IDENT && t->at.col == 1 && DOC->toks[i + 1].kind == T_IDENT
        && (str_eq_c(t->text, "input") || str_eq_c(t->text, "view") || str_eq_c(t->text, "struct")
            || str_eq_c(t->text, "event") || str_eq_c(t->text, "command") || str_eq_c(t->text, "enum") || str_eq_c(t->text, "scene")
            || str_eq_c(t->text, "local") || str_eq_c(t->text, "namespace")
            || str_eq_c(t->text, "using") || str_eq_c(t->text, "extern") || str_eq_c(t->text, "frag")
            || str_eq_c(t->text, "vert"))) {
        return true;
    }
    if (t->kind == T_IDENT && t->at.col == 1 && str_eq_c(t->text, "local")
        && (DOC->toks[i + 1].kind == T_COMPONENT || DOC->toks[i + 1].kind == T_SINGLETON)) {
        return true;
    }
    if (t->kind == T_IDENT && t->at.col == 1 && DOC->toks[i + 1].kind == T_LPAREN
        && (str_eq_c(t->text, "event") || str_eq_c(t->text, "command"))) {
        return true; // event(Hit hit) TakeHit(...)
    }
    if (t->kind == T_IDENT && t->at.col == 1 && DOC->toks[i + 1].kind == T_IDENT && str_eq_c(t->text, "async")) {
        return true; // async void Countdown(), async event(...)
    }
    if (t->kind == T_IDENT && t->at.col == 1 && DOC->toks[i + 1].kind == T_LBRACE && str_eq_c(t->text, "settings")) {
        return true;
    }
    return starts_function(i);
}

static bool is_word(const token *t)
{
    return t->text.len > 0 && (isalpha((unsigned char)t->text.ptr[0]) || t->text.ptr[0] == '_') && t->kind != T_STRING;
}

static bool ends_operand(tok_kind k);

// Whether the name at token `i` is the variable a loop declares, before its
// `in`: foreach (var item, foreach (Stats s, parallel (var at.
static bool loop_variable_named(const int i)
{
    int k = i - 1;
    while (k > 0 && (DOC->toks[k].kind == T_VAR || DOC->toks[k].kind == T_IDENT || DOC->toks[k].kind == T_DOT
                     || DOC->toks[k].kind == T_LT || DOC->toks[k].kind == T_GT || DOC->toks[k].kind == T_QUESTION)) {
        if (DOC->toks[k].kind == T_IDENT && str_eq_c(DOC->toks[k].text, "in")) return false; // Past the name already
        k--;
    }
    return k < i - 1 && k >= 1 && DOC->toks[k].kind == T_LPAREN
        && (DOC->toks[k - 1].kind == T_FOREACH || DOC->toks[k - 1].kind == T_PARALLEL);
}

// The '(' of the parallel loop whose header holds token `last`, or -1.
static int parallel_header(const int last)
{
    int parens = 0;
    for (int k = last; k > 0; k--) {
        const tok_kind kind = DOC->toks[k].kind;
        if (kind == T_RPAREN) parens++;
        else if (kind == T_LPAREN && parens-- == 0) return DOC->toks[k - 1].kind == T_PARALLEL ? k : -1;
        else if (kind == T_SEMI || kind == T_LBRACE || kind == T_RBRACE) return -1;
    }
    return -1;
}

// The innermost call still open after token `last`: the token of its '(', or
// -1 where there's none, and how many of its commas come before.
static int open_call(const int last, int *commas)
{
    int parens = 0;
    int braces = 0;
    int brackets = 0;
    *commas = 0;
    for (int i = last; i >= 0; i--) {
        switch (DOC->toks[i].kind) {
        case T_RPAREN: parens++; break;
        case T_RBRACE: braces++; break;
        case T_RBRACKET: brackets++; break;
        case T_LPAREN:
            if (parens == 0) return i;
            parens--;
            break;
        case T_LBRACE:
            if (braces == 0) return -1; // A block: not inside a call
            braces--;
            break;
        case T_LBRACKET: // A list still open, [0, 1: its commas weren't the call's
            if (brackets == 0) *commas = 0;
            else brackets--;
            break;
        case T_SEMI:
            if (parens == 0 && braces == 0) return -1;
            break;
        case T_COMMA:
            if (parens == 0 && braces == 0 && brackets == 0) (*commas)++;
            break;
        default:
            break;
        }
    }
    return -1;
}

// Whether the '(' at token `open` is Draw.Mesh's.
static type receiver_type(int dot, loc at);
static bool opens_mesh_draw(const int open)
{
    const token *toks = DOC->toks;
    if (open >= 2 && toks[open - 1].kind == T_IDENT && str_eq_c(toks[open - 1].text, "Mesh")
        && toks[open - 2].kind == T_DOT
        && receiver_type(open - 2, toks[open].at).kind == TY_RENDER_TARGET) return true;
    return open >= 3 && toks[open - 1].kind == T_IDENT && toks[open - 2].kind == T_DOT && toks[open - 3].kind == T_IDENT
        && is_mesh_draw(toks[open - 3].text, toks[open - 1].text);
}

// The arguments written in the parentheses opened at token `open`: tokens
// `first[i]` to `last[i]` each, an empty one's last before its first. A call
// cut short has those written so far.
typedef struct call_args {
    int first[8];
    int last[8];
    int count;
} call_args;

static void call_arguments(const int open, call_args *args)
{
    args->count = 0;
    int depth = 0;
    int start = open + 1;
    for (int i = open + 1; i <= DOC->tok_count; i++) {
        const tok_kind k = i < DOC->tok_count ? DOC->toks[i].kind : T_EOF;
        const bool closes = k == T_RPAREN || k == T_RBRACKET || k == T_RBRACE;
        const bool ends = k == T_EOF || k == T_SEMI || (closes && depth == 0);
        if (ends || (k == T_COMMA && depth == 0)) {
            if (args->count < 8) {
                args->first[args->count] = start;
                args->last[args->count] = i - 1;
                args->count++;
            }
            if (ends) return;
            start = i + 1;
        } else if (k == T_LPAREN || k == T_LBRACKET || k == T_LBRACE) {
            depth++;
        } else if (closes) {
            depth--;
        }
    }
}

// The type of the names with dots between them at tokens `first` to `last`,
// like models.crate: what the checker found there, or where the code around
// them doesn't parse yet, what the names say. TY_ERROR for anything else.
static type names_type(const scope *sc, const int first, const int last)
{
    const token *toks = DOC->toks;
    if (first > last || (last - first) % 2 != 0) return (type){TY_ERROR, NULL};
    for (int i = first; i <= last; i++) {
        if (toks[i].kind != ((i - first) % 2 ? T_DOT : T_IDENT)) return (type){TY_ERROR, NULL};
    }
    const occurrence *o = occurrence_at(toks[last].at);
    if (o && loc_cmp(o->at, toks[last].at) == 0 && o->type.kind != TY_ERROR && o->type.kind != TY_VOID) return o->type;
    const param *ignored;
    type t = name_type(sc, toks[first].text, &ignored);
    for (int k = first + 2; k <= last && t.kind != TY_ERROR; k += 2) t = member_type(t, toks[k].text);
    return t;
}

// Draw.Mesh's parameters, in the order they're written:
// Draw.Mesh(vertices, indices[, transform][, texture[, filter[, wrap]]][, vert][, frag]).
// A 3D mesh has its transform, and a 2D one none.
enum { MESH_VERTICES, MESH_INDICES, MESH_TRANSFORM, MESH_TEXTURE, MESH_FILTER, MESH_WRAP, MESH_VERT, MESH_FRAG, MESH_INSTANCES,
       MESH_VALUES, MESH_PARAMS };

// What an argument of Draw.Mesh after its indices is, from what's written at
// tokens `first` to `last`: which of those parameters, or MESH_PARAMS where
// that doesn't say.
// The vert or frag the tokens `first` to `last` name: what the checker bound
// them to, or the game's one of that name. NULL for anything else.
static const decl *shader_written(const int first, const int last)
{
    const token *toks = DOC->toks;
    if (first > last || toks[first].kind != T_IDENT) return NULL;
    const occurrence *o = occurrence_at(toks[last].at);
    if (o && loc_cmp(o->at, toks[last].at) == 0 && o->kind == OCC_SYSTEM && is_shader(o->decl)) return o->decl;
    const str written = {toks[first].text.ptr, (int)(toks[last].text.ptr + toks[last].text.len - toks[first].text.ptr)};
    for (int i = 0; i < A.prog->decls.count; i++) {
        const decl *d = A.prog->decls.items[i];
        if (is_shader(d) && (str_eq_c(written, name_for(d)) || str_eq(written, d->qualified))) return d;
    }
    return NULL;
}

static int mesh_argument(const scope *sc, const int first, const int last)
{
    const token *toks = DOC->toks;
    if (first > last || toks[first].kind != T_IDENT) return MESH_PARAMS;
    // A vert or a frag, by name: what the checker bound it to, or the game's one of that name
    const decl *shader = shader_written(first, last);
    if (shader) return shader->is_frag ? MESH_FRAG : MESH_VERT;
    // Swell { height = 2 }: the draw's values, a struct's
    for (int brace = first + 1; brace <= last; brace++) {
        if (toks[brace].kind != T_LBRACE) continue;
        const str written = {toks[first].text.ptr, (int)(toks[brace - 1].text.ptr + toks[brace - 1].text.len - toks[first].text.ptr)};
        for (int i = 0; i < A.prog->decls.count; i++) {
            const decl *d = A.prog->decls.items[i];
            if (d->kind == DECL_STRUCT && (str_eq_c(written, name_for(d)) || str_eq(written, d->qualified))) return MESH_VALUES;
        }
        break;
    }
    // float4x4.Translate(...), Filter.Point, Wrap.Repeat
    if (str_eq_c(toks[first].text, "float4x4")) return MESH_TRANSFORM;
    if (str_eq_c(toks[first].text, "Filter") && last == first + 2) return MESH_FILTER;
    if (str_eq_c(toks[first].text, "Wrap") && last == first + 2) return MESH_WRAP;
    const type t = names_type(sc, first, last);
    if (t.kind == TY_ENUM) return t.decl == A.prog->wrap ? MESH_WRAP : t.decl == A.prog->filter ? MESH_FILTER : MESH_PARAMS;
    return t.kind == TY_FLOAT4X4 ? MESH_TRANSFORM : t.kind == TY_GRID || t.kind == TY_TEXTURE ? MESH_TEXTURE
         : t.kind == TY_STRUCT ? MESH_VALUES : MESH_PARAMS;
}

// The vertex struct of the mesh a Draw.Mesh draws, from its first argument,
// at tokens `first` to `last`: the element of the list that names say, like
// models.crate, or the struct a list written there starts with, like
// [Vertex3 { ... }]. NULL where that doesn't say.
static const decl *mesh_vertex(const scope *sc, const int first, const int last)
{
    const token *toks = DOC->toks;
    if (first > last) return NULL;
    if (toks[first].kind == T_LBRACKET && first + 1 <= last && toks[first + 1].kind == T_IDENT) {
        int name = first + 1; // Its last part, if it's qualified
        while (name + 2 <= last && toks[name + 1].kind == T_DOT && toks[name + 2].kind == T_IDENT) name += 2;
        const str written = {toks[first + 1].text.ptr, (int)(toks[name].text.ptr + toks[name].text.len - toks[first + 1].text.ptr)};
        for (int i = 0; i < A.prog->decls.count; i++) {
            const decl *d = A.prog->decls.items[i];
            if (d->kind == DECL_STRUCT && (str_eq_c(written, name_for(d)) || str_eq(written, d->qualified))) return d;
        }
        return NULL;
    }
    const type t = names_type(sc, first, last);
    if (t.kind != TY_LIST || !t.decl || t.decl->fields.items[0].type.kind != TY_STRUCT) return NULL;
    return t.decl->fields.items[0].type.decl;
}

// An argument being typed for an `out` parameter: `TryHalf(8, `, `TryHalf(8, out `.
typedef struct out_argument {
    int stage;             // What's written of it before the cursor: OUT_START and the rest, or OUT_NONE when it's no `out` parameter's
    bool only;             // Every function the call can mean makes its variable there: nothing but an `out` argument fits
    str name;              // The parameter's name: half
    const char *type_text; // ...and its type, as code writes it: "int"
    type type;             // ...as a type, or TY_ERROR where that isn't known
} out_argument;

enum { OUT_NONE = -1, OUT_START, OUT_WORD, OUT_MUT }; // Nothing yet, or its name and ':'; `out`; `out mut`

// One of the engine's own functions, like GUILayout.TextField(label, value,
// out entered): what each of its versions has where an argument is, read from
// its signature as editors show it.
typedef struct builtin_out {
    int place;           // The argument's place among those written
    str named;           // ...or the name it's passed by
    int candidates;      // The versions with a parameter there
    int makes;           // ...and those whose parameter there is `out`
    out_argument *found; // The first of those: its name and type
} builtin_out;

static void visit_builtin_out(void *user, const char *label, const char *doc)
{
    (void)doc;
    builtin_out *v = user;
    int starts[16];
    int ends[16];
    const int params = label_split(label, starts, ends);
    for (int p = 0; p < params; p++) {
        const char *text = label + starts[p];
        const char *end = label + ends[p];
        const char *word = end; // Its name: the last word
        while (word > text && word[-1] != ' ') word--;
        const str param_name = {word, (int)(end - word)};
        if (v->named.len > 0 ? !str_eq(param_name, v->named) : p != v->place) continue;
        v->candidates++;
        if (end - text < 4 || strncmp(text, "out ", 4) != 0 || word < text + 5) continue;
        if (v->makes++ > 0) continue;
        char *type_text = arena_alloc((size_t)(word - text) - 4);
        memcpy(type_text, text + 4, (size_t)(word - text) - 5);
        type_text[word - text - 5] = '\0';
        v->found->name = param_name;
        v->found->type_text = type_text;
        if (!builtin_type_named(str_from(type_text), &v->found->type)) v->found->type = (type){TY_ERROR, NULL};
    }
}

// What the argument the cursor is in is for, when that's an `out` parameter,
// in the call whose '(' is token `open`: `last` is the token before the cursor.
// The parameter is the one in the argument's place, or the one it's named for
// (`half: `), of the game's functions and methods of the call's name (the one
// it means, once it resolved) or of a built-in method, a map's TryGet.
static out_argument out_argument_at(const int open, const int last, const loc at)
{
    const out_argument none = {OUT_NONE, false, {"", 0}, NULL, {TY_ERROR, NULL}};
    const token *toks = DOC->toks;
    int callee = open - 1;
    str in_brackets = {"", 0}; // world.TryGet<Health>(: the component, which comes between the name and the '('
    if (callee >= 3 && toks[callee].kind == T_GT && toks[callee - 1].kind == T_IDENT) {
        int k = callee - 1;
        while (k >= 2 && toks[k - 1].kind == T_DOT && toks[k - 2].kind == T_IDENT) k -= 2;
        if (k >= 2 && toks[k - 1].kind == T_LT && toks[k - 2].kind == T_IDENT) {
            in_brackets = toks[callee - 1].text;
            callee = k - 2;
        }
    }
    if (callee < 0 || toks[callee].kind != T_IDENT) return none;
    // The argument's place among those written, and where it starts
    int place = 0;
    int first = open + 1;
    bool names = false; // An argument before it has a name: it needs one too
    for (int i = open + 1, depth = 0; i <= last; i++) {
        const tok_kind k = toks[i].kind;
        if (k == T_LPAREN || k == T_LBRACKET || k == T_LBRACE) depth++;
        else if (k == T_RPAREN || k == T_RBRACKET || k == T_RBRACE) depth--;
        if (k != T_COMMA || depth != 0) continue;
        names |= toks[first].kind == T_IDENT && toks[first + 1].kind == T_COLON;
        place++;
        first = i + 1;
    }
    str named = {"", 0};
    if (first + 1 <= last && toks[first].kind == T_IDENT && toks[first + 1].kind == T_COLON) {
        named = toks[first].text;
        first += 2;
    }
    if (names && named.len == 0) return none;
    out_argument found = none;
    if (first > last) found.stage = OUT_START;
    else if (toks[first].kind != T_IDENT || !str_eq_c(toks[first].text, "out")) return none;
    else if (first == last) found.stage = OUT_WORD;
    else if (first + 1 == last && toks[last].kind == T_MUT) found.stage = OUT_MUT;
    else return none;

    const str name = toks[callee].text;
    const bool method = callee >= 1 && toks[callee - 1].kind == T_DOT;
    const occurrence *o = occurrence_at(toks[callee].at);
    if (o && loc_cmp(o->at, toks[callee].at) != 0) o = NULL;
    const decl *meant = o && is_routine(o->decl) ? o->decl : NULL;
    int candidates = 0;
    int makes = 0;
    for (int i = 0; i < A.prog->decls.count; i++) {
        const decl *d = A.prog->decls.items[i];
        for (int k = -1; k < d->methods.count; k++) {
            const decl *m = k < 0 ? d : d->methods.items[k];
            if (!is_routine(m) || m->is_operator) continue;
            if (meant ? m != meant : !str_eq(m->name, name) || (method && !m->owner)) continue;
            const int count = m->params.count - (m->takes_action ? 1 : 0); // An Action is the block after the call
            int at_param = named.len > 0 ? -1 : place;
            for (int p = 0; named.len > 0 && p < count; p++) {
                if (str_eq(m->params.items[p].name, named)) at_param = p;
            }
            if (at_param < 0 || at_param >= count) continue;
            candidates++;
            const param *prm = &m->params.items[at_param];
            if (!prm->is_out) continue;
            if (makes++ > 0) continue; // The first one's name and type
            found.name = prm->name;
            found.type_text = str_to_cstr(prm->type_name);
            found.type = prm->type;
        }
    }
    // scores.TryGet(key, out value): what it's called on says whose method it is
    if (candidates == 0 && method) {
        const type t = o && o->call != CALL_NONE && o->object_type.kind != TY_ERROR ? o->object_type : receiver_type(callee - 1, at);
        size_t count;
        const method_form *forms = methods_of(t, &count);
        const method_form *form = find_form(forms, count, name);
        const decl *component = NULL; // The World's: the component its type names
        for (int i = 0; t.kind == TY_WORLD && !component && i < A.prog->components.count; i++) {
            if (str_eq(A.prog->components.items[i]->name, in_brackets)) component = A.prog->components.items[i];
        }
        sb label = {0};
        int starts[16];
        int ends[16];
        if (form && t.kind == TY_WORLD) format_world_method(form, component, &label);
        else if (form) format_method(form, t, &label);
        const int params = form ? label_split(label.data, starts, ends) : 0;
        for (int p = 0; p < params; p++) {
            const char *text = label.data + starts[p];
            const char *end = label.data + ends[p];
            const char *word = end; // Its name: the last word
            while (word > text && word[-1] != ' ') word--;
            const str param_name = {word, (int)(end - word)};
            if (named.len > 0 ? !str_eq(param_name, named) : p != place) continue;
            candidates++;
            if (end - text < 4 || strncmp(text, "out ", 4) != 0 || word < text + 5) continue;
            makes++;
            found.name = param_name;
            char *type_text = arena_alloc((size_t)(word - text) - 4);
            memcpy(type_text, text + 4, (size_t)(word - text) - 5);
            type_text[word - text - 5] = '\0';
            found.type_text = type_text;
            if (t.kind == TY_MAP && t.decl) found.type = t.decl->fields.items[1].type; // TryGet's: the map's value
            if (t.kind == TY_WORLD && component) found.type = type_of_decl(component); // ...or the World's: the component
        }
    }
    // GUILayout.TextField("Code", lobby.code, out entered): the versions of one of the engine's own functions
    if (candidates == 0 && method && callee >= 2 && toks[callee - 2].kind == T_IDENT && builtin_owner(toks[callee - 2].text)
        && (callee < 3 || toks[callee - 3].kind != T_DOT)) {
        builtin_out v = {place, named, 0, 0, &found};
        builtin_signatures(toks[callee - 2].text, name, visit_builtin_out, &v);
        candidates = v.candidates;
        makes = v.makes;
    }
    if (makes == 0) return none;
    found.only = makes == candidates;
    return found;
}

// The type of the local an `out` argument declares earlier in the statement
// the cursor is in, named at token `use`, read from the tokens: for a
// statement that doesn't parse yet, like `if (scores.TryGet(key, out var
// stats)) total += stats.`, which has no call to look at. `out var` takes its
// parameter's type, and a type written there is the local's own.
static type out_variable_type(const int use, const loc at)
{
    const token *toks = DOC->toks;
    const str name = toks[use].text;
    for (int k = use - 1; k >= 3; k--) {
        if (toks[k].kind == T_LBRACE || toks[k].kind == T_RBRACE || toks[k].kind == T_SEMI) break; // Out of the statement
        if (toks[k].kind != T_IDENT || !str_eq(toks[k].text, name)) continue;
        int word = k - 2; // `out` before `var stats` or `Stats stats`, maybe with `mut` between
        if (toks[word].kind == T_MUT) word--;
        if (word < 1 || toks[word].kind != T_IDENT || !str_eq_c(toks[word].text, "out")) continue;
        const tok_kind before = toks[word - 1].kind;
        if (before != T_LPAREN && before != T_COMMA && before != T_COLON) continue; // Where an argument starts
        const token *written = &toks[k - 1];
        if (written->kind == T_IDENT) {
            type t;
            if (builtin_type_named(written->text, &t)) return t;
            for (int i = 0; i < A.prog->decls.count; i++) {
                const decl *d = A.prog->decls.items[i];
                if (str_eq(d->name, written->text) && type_of_decl(d).kind != TY_ERROR) return type_of_decl(d);
            }
            return (type){TY_ERROR, NULL};
        }
        if (written->kind != T_VAR) continue;
        int commas;
        const int open = open_call(word - 1, &commas);
        return open >= 1 ? out_argument_at(open, word, at).type : (type){TY_ERROR, NULL};
    }
    return (type){TY_ERROR, NULL};
}

// What can follow in an argument for an `out` parameter, as far as it's
// written: `out var half`, which declares the variable there, `out` before one
// the code has, and `out _`; after the word, `var`, `mut`, `_`, the
// parameter's type, and the variables in scope the code can change.
static void complete_out_argument(completion *c, const out_argument *arg, const loc at)
{
    sb declares = {0};
    sb declared = {0};
    sb_printf(&declares, "out var " STR_FMT, STR_ARG(arg->name));
    sb_printf(&declared, "A read-only local of the parameter's type, `%s`, which the call makes: in scope from here to the "
                         "end of the block its statement is in.", arg->type_text);
    if (arg->stage == OUT_START) {
        sb snippet = {0};
        sb_printf(&snippet, "out var ${1:" STR_FMT "}", STR_ARG(arg->name));
        item(c, declares.data, CK_SNIPPET, "Declares the variable the call makes", declared.data, snippet.data);
        item(c, "out", CK_KEYWORD, "Before a variable or field the code has: the call assigns it", NULL, NULL);
        item(c, "out _", CK_KEYWORD, "Leaves out what the call makes", NULL, NULL);
        return;
    }
    sb name_snippet = {0};
    sb type_snippet = {0};
    sb_printf(&name_snippet, "var ${1:" STR_FMT "}", STR_ARG(arg->name));
    sb_printf(&type_snippet, "%s ${1:" STR_FMT "}", arg->type_text, STR_ARG(arg->name));
    item(c, "var", CK_KEYWORD, "Declares the variable here, of the parameter's type", declared.data, name_snippet.data);
    item(c, arg->type_text, CK_STRUCT, "The parameter's type: declares the variable here", NULL, type_snippet.data);
    if (arg->stage == OUT_MUT) return;
    item(c, "mut", CK_KEYWORD, "Declares a variable the code can change afterwards: 'out mut var'", NULL, NULL);
    item(c, "_", CK_KEYWORD, "Leaves out what the call makes", NULL, NULL);
    // What the call can assign: the code's `mut` locals of the parameter's type, and what it takes to change, whose
    // fields the call can make too
    const scope sc = scope_at(at);
    for (int i = sc.locals.count - 1; i >= 0; i--) {
        const stmt *s = sc.locals.items[i];
        if (!s->is_mut || s->kind != S_VAR) continue;
        const bool fits = arg->type.kind == TY_ERROR || s->type.kind == TY_STRUCT
                       || (s->type.kind == arg->type.kind && s->type.decl == arg->type.decl);
        if (fits) item(c, str_to_cstr(s->name), CK_VARIABLE, type_name(s->type), NULL, NULL);
    }
    for (int i = 0; sc.decl && !sc.in_input && i < sc.decl->params.count; i++) {
        const param *p = &sc.decl->params.items[i];
        if (p->name.len == 0 || p->mode != PARAM_MUT || p->type.kind == TY_WORLD) continue;
        sb detail = {0};
        format_param(p, &detail);
        item(c, str_to_cstr(p->name), CK_VARIABLE, detail.data, NULL, NULL);
    }
    for (int i = 0; sc.fields_of && sc.decl && sc.decl->is_mut_method && i < sc.fields_of->fields.count; i++) {
        const field *f = &sc.fields_of->fields.items[i];
        if (!f->hidden) item(c, str_to_cstr(f->name), CK_FIELD, type_name(f->type), NULL, NULL);
    }
}


void analysis_completion(const int line, const int character, jbuf *out)
{
    const loc at = from_lsp(line, character);
    completion c = {out, 0, {0}};
    jb_put(out, "{\"isIncomplete\":false,\"items\":[");
    if (at.line >= 1 && at.line <= DOC->lines.count && DOC->inactive[at.line]) goto done; // A directive, or no code

    // The last token before the cursor, skipping the word being typed.
    int last = -1;
    for (int i = 0; i < DOC->tok_count && loc_cmp(DOC->toks[i].at, at) < 0; i++) last = i;
    if (last >= 0) {
        const token *t = &DOC->toks[last];
        const bool inside = t->at.line == at.line && at.col <= t->at.col + token_len(t);
        if (inside && t->kind == T_STRING) { // No completion in text, but for an asset's path
            complete_asset_path(&c, last, at);
            goto done;
        }
        // In text with values: not in its text, but in its values.
        const bool text_end = at.col == t->at.col + token_len(t) && t->text.ptr[t->text.len - 1] == '"';
        if ((t->kind == T_INTERP || t->kind == T_INTERP_PART) && t->at.line == at.line
            && (at.col < t->at.col + token_len(t) || text_end)) {
            goto done;
        }
        if (inside && t->kind == T_INTERP_FORMAT) goto done;
        if (inside && is_word(t)) last--;
    }

    frame stack[64] = {{CTX_TOP, -1, HEADER_NONE}};
    int depth = 0;
    for (int i = 0; i <= last; i++) {
        // In a type's body a method starts no declaration, `virtual` before it or not
        if (starts_declaration(i) && !(stack[depth].kind == CTX_DATA && (starts_function(i) || starts_replace_word(i)))) {
            depth = 0;
            if (DOC->toks[i].kind != T_LBRACKET) continue;
        }
        const context_kind top = stack[depth].kind;
        const tok_kind kind = DOC->toks[i].kind;
        const tok_kind before = i > 0 ? DOC->toks[i - 1].kind : T_EOF;
        if (kind == T_LBRACKET && top == CTX_TOP && depth < 63) {
            stack[++depth] = (frame){CTX_ATTRIBUTE, i, HEADER_NONE};
        } else if (kind == T_RBRACKET && top == CTX_ATTRIBUTE) {
            depth--;
        } else if (kind == T_LPAREN && (top == CTX_ATTRIBUTE || top == CTX_ATTRIBUTE_ARGS) && depth < 63) {
            const int open = top == CTX_ATTRIBUTE_ARGS ? stack[depth].open : i;
            stack[++depth] = (frame){CTX_ATTRIBUTE_ARGS, open, HEADER_NONE};
        } else if (kind == T_RPAREN && top == CTX_ATTRIBUTE_ARGS) {
            depth--;
        } else if (kind == T_LBRACKET && top == CTX_DATA && depth < 63) {
            stack[++depth] = (frame){CTX_FIELD_ATTRIBUTE, i, HEADER_NONE};
        } else if (kind == T_RBRACKET && top == CTX_FIELD_ATTRIBUTE) {
            depth--;
        } else if (kind == T_LPAREN && (top == CTX_FIELD_ATTRIBUTE || top == CTX_FIELD_ATTRIBUTE_ARGS) && depth < 63) {
            stack[++depth] = (frame){CTX_FIELD_ATTRIBUTE_ARGS, i, HEADER_NONE}; // One frame per parenthesis: float2(0, 1)
        } else if (kind == T_RPAREN && top == CTX_FIELD_ATTRIBUTE_ARGS) {
            depth--;
        } else if (kind == T_LBRACE && depth < 63) {
            context_kind next = CTX_CODE;
            if (top == CTX_TOP && before == T_IDENT && !after_fails(i)) next = CTX_DATA;
            else if ((top == CTX_CODE || top == CTX_LITERAL) && before == T_IDENT) next = CTX_LITERAL;
            stack[++depth] = (frame){next, i, HEADER_NONE};
        } else if (kind == T_RBRACE && depth > 0) {
            depth--;
        } else if (kind == T_LPAREN && depth < 63) {
            const bool query = top == CTX_HEADER && stack[depth].header == HEADER_SYSTEM && before == T_IDENT
                            && str_eq_c(DOC->toks[i - 1].text, "query");
            const header_kind header = query ? HEADER_QUERY
                : top == CTX_TOP ? top_header(i) : top == CTX_DATA ? member_header(i) : HEADER_NONE;
            if (header != HEADER_NONE) stack[++depth] = (frame){CTX_HEADER, i, header};
        } else if (kind == T_RPAREN && top == CTX_HEADER) {
            depth--;
        }
    }

    const token *prev = last >= 0 ? &DOC->toks[last] : NULL;
    const tok_kind pk = prev ? prev->kind : T_EOF;
    const frame f = stack[depth];
    order_completion order = ORDER_NONE;
    if (f.kind == CTX_ATTRIBUTE_ARGS && f.open >= 1) {
        const str attribute_name = DOC->toks[f.open - 1].text;
        if (str_eq_c(attribute_name, "Before") || str_eq_c(attribute_name, "After")) order = ORDER_REFS;
        if (str_eq_c(attribute_name, "Stage")) {
            order = ORDER_PHASE;
            for (int i = f.open + 1; i <= last; i++) {
                if (DOC->toks[i].kind == T_COMMA) order = ORDER_OFFSET;
            }
        }
    }

    if (pk == T_DOT) {
        complete_members(&c, last, at, order);
        goto done;
    }
    // world.Get<: a component, of the worlds the code reads
    if (pk == T_LT && last >= 3 && DOC->toks[last - 1].kind == T_IDENT && DOC->toks[last - 2].kind == T_DOT
        && DOC->toks[last - 3].kind == T_IDENT) {
        const scope sc = scope_at(at);
        const param *world;
        if (name_type(&sc, DOC->toks[last - 3].text, &world).kind == TY_WORLD) {
            if (!sc.decl->is_local || sc.decl->is_view) complete_components_of(&c, false);
            if (sc.decl->is_local || sc.decl->is_view) complete_components_of(&c, true);
            complete_namespaces(&c, false);
            goto done;
        }
    }
    // int Parse(string text) fails ParseError: the error's type, one a function can return
    if (pk == T_IDENT && str_eq_c(prev->text, "fails") && last >= 1 && DOC->toks[last - 1].kind == T_RPAREN
        && (f.kind == CTX_TOP || f.kind == CTX_DATA)) {
        complete_structs(&c);
        complete_value_types(&c, VT_LOCALS);
        complete_namespaces(&c, false);
        goto done;
    }

    switch (f.kind) {
    case CTX_TOP:
        if (pk == T_EOF || pk == T_RBRACE || pk == T_SEMI || pk == T_RBRACKET) {
            complete_declarations(&c);
        } else if (pk == T_IDENT && str_eq_c(prev->text, "override")) { // What there is to replace, each written out
            complete_overrides(&c);
        } else if (pk == T_IDENT && str_eq_c(prev->text, "virtual")) { // What can be replaced: code
            complete_code_declarations(&c);
        } else if (pk == T_IDENT && str_eq_c(prev->text, "using")) {
            complete_namespaces(&c, true);
        } else if (pk == T_IDENT && str_eq_c(prev->text, "const")) { // What a constant can be
            complete_value_types(&c, VT_VALUES | VT_BOOL | VT_TEXT);
            complete_structs(&c);
            complete_namespaces(&c, false);
        } else if (pk == T_COLON && last >= 2 && DOC->toks[last - 1].kind == T_IDENT && DOC->toks[last - 2].kind == T_IDENT
                   && str_eq_c(DOC->toks[last - 2].text, "enum")) { // What the enum is stored as
            item(&c, "byte", CK_KEYWORD, "One byte: members from 0 to 255", NULL, NULL);
            item(&c, "ushort", CK_KEYWORD, "Two bytes: members from 0 to 65535", NULL, NULL);
            item(&c, "int", CK_KEYWORD, "Four bytes, as an enum is without one", NULL, NULL);
        } else if (pk == T_IDENT && str_eq_c(prev->text, "extern")) { // Its return type, or a system written in C
            item(&c, "system", CK_SNIPPET, "extern system Name(mut Component value);",
                 "A system written in C: called once a tick for each run of rows of the entities that match, with their "
                 "components as arrays (tide/rows.h) and the singletons it takes.",
                 "system ${1:Name}(${2:mut Component value});");
            item(&c, "void", CK_KEYWORD, "Returns nothing", NULL, NULL);
            complete_value_types(&c, VT_LOCALS | VT_LOCAL_ENTITY);
            complete_structs(&c);
            complete_namespaces(&c, false);
        }
        break;

    case CTX_ATTRIBUTE:
        if (pk == T_LBRACKET || pk == T_COMMA) {
            item(&c, "Stage", CK_FUNCTION, "[Stage(Stages.Phase, offset = 0)]",
                 "Places work in an enum phase, with an optional signed integer offset within that phase.", "Stage($1)");
            item(&c, "Before", CK_FUNCTION, "[Before(DeclarationOrPhase)]",
                 "Runs before the named declarations or entire enum phases.", "Before($1)");
            item(&c, "After", CK_FUNCTION, "[After(DeclarationOrPhase)]",
                 "Runs after the named declarations or entire enum phases.", "After($1)");
            item(&c, "NativeName", CK_FUNCTION, "[NativeName(\"c_function\")]",
                 "The C function the extern function after it calls, or the extern system after it is, when its name isn't the declaration's own.",
                 "NativeName(\"$1\")");
            item(&c, "Channel", CK_FUNCTION, "[Channel(Channels.Uploads)]",
                 "Puts the command in a channel of the game's: in order among that channel's commands, and in no order against the others.",
                 "Channel($1)");
            item(&c, "Unordered", CK_FUNCTION, "[Unordered]",
                 "The command has no order at all: handled in the tick it arrives, before or after commands sent earlier, once.",
                 "Unordered");
            item(&c, "Filter", CK_FUNCTION, "[Filter(Filter.Point)]",
                 "How an asset that's a Texture or a Grid2<Color> is read between its cells, where a draw doesn't say.",
                 "Filter(Filter.${1:Point})");
            item(&c, "Wrap", CK_FUNCTION, "[Wrap(Wrap.Repeat)]",
                 "What a uv past the edges of an asset that's a Texture or a Grid2<Color> reads, where a draw doesn't say.",
                 "Wrap(Wrap.${1:Repeat})");
            item(&c, "Raw", CK_FUNCTION, "[Raw]",
                 "An asset that's a Texture or a Grid2<Color> of numbers, kept as the file has them: a frag reads it by its name.",
                 "Raw");
        }
        break;

    case CTX_FIELD_ATTRIBUTE:
        if (pk == T_LBRACKET || pk == T_COMMA) {
            item(&c, "Clamp", CK_FUNCTION, "[Clamp(lo, hi)]", "Keeps the field between lo and hi, before Sanitize runs.",
                 "Clamp($1)");
            item(&c, "Min", CK_FUNCTION, "[Min(x)]", "Keeps the field at least x, before Sanitize runs.", "Min($1)");
            item(&c, "Max", CK_FUNCTION, "[Max(x)]", "Keeps the field at most x, before Sanitize runs.", "Max($1)");
            item(&c, "Absolute", CK_FUNCTION, "[Absolute]",
                 "This input field's default isn't rest, so a guess of another player's input keeps it.",
                 "Absolute");
            item(&c, "Snap", CK_FUNCTION, "[Snap]", "Views see the field as it is, not blended between ticks.", "Snap");
            item(&c, "Position", CK_FUNCTION, "[Position]", "Provisional: linear tick interpolation with smooth visual correction error.", "Position");
            item(&c, "Rotation", CK_FUNCTION, "[Rotation]", "Provisional: shortest rotation interpolation and smooth correction error: float radians or quaternion.", "Rotation");
            item(&c, "Filter", CK_FUNCTION, "[Filter(Filter.Point)]",
                 "How this grid, or this RenderTarget's color, is read between its cells as a texture, where a draw doesn't say.",
                 "Filter(Filter.${1:Point})");
            item(&c, "Wrap", CK_FUNCTION, "[Wrap(Wrap.Repeat)]",
                 "What a uv past the edges of this grid, or of this RenderTarget's color, reads, where a draw doesn't say.",
                 "Wrap(Wrap.${1:Repeat})");
            item(&c, "Raw", CK_FUNCTION, "[Raw]",
                 "This Grid2<Color>'s cells are numbers, kept as they are as a texture: a frag reads it by its name.", "Raw");
        }
        break;

    case CTX_FIELD_ATTRIBUTE_ARGS: // Constant bounds, like a default value
        complete_value_types(&c, VT_VALUES);
        item(&c, "Math", CK_MODULE, "Math functions and constants", NULL, NULL);
        break;

    case CTX_ATTRIBUTE_ARGS:
        if (order == ORDER_OFFSET) {
            complete_constants(&c, TY_INT);
            complete_namespaces(&c, false);
        } else if (pk == T_LPAREN || pk == T_COMMA) {
            if (order == ORDER_REFS) {
                complete_systems(&c, false);
                complete_systems(&c, true);
            }
            if (order == ORDER_PHASE || order == ORDER_REFS) complete_phases(&c);
            complete_namespaces(&c, false);
        }
        break;

    case CTX_HEADER: {
        if (f.header == HEADER_QUERY) {
            // view Bars(query (Health health, verified Health sure) both): a view's query reads the confirmed world too
            const frame outer = depth >= 1 ? stack[depth - 1] : f;
            const bool in_view = outer.kind == CTX_HEADER && outer.open >= 2 && DOC->toks[outer.open - 2].kind == T_IDENT
                              && str_eq_c(DOC->toks[outer.open - 2].text, "view");
            const bool confirmed = in_view && pk == T_IDENT && str_eq_c(prev->text, "verified") && last > f.open;
            const tok_kind marked = confirmed ? DOC->toks[last - 1].kind : T_EOF;
            if (marked == T_LPAREN || marked == T_COMMA || marked == T_WITH || marked == T_WITHOUT) {
                complete_match_types(&c, false);
                complete_namespaces(&c, false);
            } else if (pk == T_LPAREN || pk == T_COMMA || pk == T_WITH || pk == T_WITHOUT) {
                if (pk == T_LPAREN || pk == T_COMMA) {
                    item(&c, "with", CK_KEYWORD, "Query entities must have this component", NULL, NULL);
                    item(&c, "without", CK_KEYWORD, "Query entities must not have this component", NULL, NULL);
                }
                if (in_view) {
                    item(&c, "verified", CK_KEYWORD,
                         pk == T_WITH ? "Entities that have it in the world the server has confirmed"
                         : pk == T_WITHOUT ? "Entities that don't have it in the world the server has confirmed"
                                           : "As the server confirmed it, not as this machine predicts it", NULL, NULL);
                }
                complete_types(&c, true, false, false);
                complete_namespaces(&c, false);
            } else if (pk == T_IDENT) {
                complete_param_name(&c, prev->text);
            }
            break;
        }
        if (f.header == HEADER_SHADER) { // frag Shade(BoxVertex vertex, out Color color, Sun sun)
            complete_shader_header(&c, f.open, last);
            break;
        }
        // event(Hit hit): the trigger; event(Hit hit) TakeHit(...): a handler's parameters.
        const bool trigger = f.header == HEADER_TRIGGER;
        const bool sample = f.header == HEADER_SAMPLE;
        // extern float Noise(...): what C takes; async handlers also get self
        bool is_extern = false;
        bool is_async = false;
        for (int k = f.open - 1; k >= 0 && DOC->toks[k].at.line == DOC->toks[f.open].at.line; k--) {
            if (DOC->toks[k].kind != T_IDENT || DOC->toks[k].at.col != 1) continue;
            is_extern = str_eq_c(DOC->toks[k].text, "extern");
            is_async = str_eq_c(DOC->toks[k].text, "async");
        }
        // extern system Move(...): a system's parameters, less what C can't be handed
        const bool extern_system = is_extern && f.header == HEADER_SYSTEM;
        if (extern_system) is_extern = false;
        if (f.header == HEADER_ROUTINE && !is_extern) {
            // void Heal(mut Stats stats): values, and for a function its Action
            const bool method = depth >= 1 && stack[depth - 1].kind == CTX_DATA;
            const bool operator_ = f.open >= 2 && DOC->toks[f.open - 2].kind == T_IDENT
                                && str_eq_c(DOC->toks[f.open - 2].text, "operator");
            // `out` where a parameter starts is the word, and what's typed after it is the parameter's type: what
            // the call can make, as after `mut`
            const bool after_out = pk == T_IDENT && str_eq_c(prev->text, "out") && last > f.open
                                && (DOC->toks[last - 1].kind == T_LPAREN || DOC->toks[last - 1].kind == T_COMMA);
            if (pk == T_LPAREN || pk == T_COMMA || pk == T_MUT || after_out) {
                if (pk != T_MUT && !after_out && !operator_) {
                    item(&c, "mut", CK_KEYWORD, "The caller's variable itself, which it can change", NULL, NULL);
                    if (!is_async) {
                        item(&c, "World", CK_CLASS, "Its caller's World, passed on: world.Get<Health>(e)", builtin_type_doc(TY_WORLD), NULL);
                    }
                    // An operator gives one value, and an async function goes on after its call returned
                    if (!is_async) {
                        item(&c, "out", CK_KEYWORD, "The caller's variable, which the call makes: assigned here on every path that returns",
                             NULL, NULL);
                    }
                }
                complete_routine_param_types(&c, pk == T_MUT || after_out, !method);
            } else if (pk == T_IDENT) {
                complete_param_name(&c, prev->text);
            }
            break;
        }
        if (is_extern) {
            if (pk == T_LPAREN || pk == T_COMMA) {
                item(&c, "in", CK_KEYWORD, "C gets a read-only pointer to the value", NULL, NULL);
                item(&c, "mut", CK_KEYWORD, "C gets a pointer to the caller's variable, which it can change", NULL, NULL);
                item(&c, "out", CK_KEYWORD, "C makes the caller's variable: it gets a pointer to write through, or for a list, a tide_list_out to fill",
                     NULL, NULL);
            }
            // `out` where a parameter starts is the word, and what's typed after it is what C can make: not text
            const bool after_out = pk == T_IDENT && str_eq_c(prev->text, "out") && last > f.open
                                && (DOC->toks[last - 1].kind == T_LPAREN || DOC->toks[last - 1].kind == T_COMMA);
            if (after_out) {
                complete_value_types(&c, VT_VALUES | VT_BOOL | VT_ENTITY | VT_LOCAL_ENTITY | VT_LIST);
                complete_structs(&c);
                complete_namespaces(&c, false);
            } else if (pk == T_LPAREN || pk == T_COMMA || pk == T_MUT || (pk == T_IDENT && str_eq_c(prev->text, "in"))) {
                complete_value_types(&c, VT_LOCALS | VT_LOCAL_ENTITY);
                complete_structs(&c);
                complete_namespaces(&c, false);
                if (pk != T_MUT && pk != T_IDENT) item(&c, "DrawList", CK_STRUCT, "DrawList", builtin_type_doc(TY_DRAW_LIST), NULL);
            } else if (pk == T_IDENT) {
                complete_param_name(&c, prev->text);
            }
            break;
        }
        if (sample && str_eq_c(DOC->toks[f.open - 1].text, "Sanitize")) break; // It takes nothing
        // `verified` is a word before a parameter's type, and what was typed after it is that type
        const bool after_verified = pk == T_IDENT && str_eq_c(prev->text, "verified") && last > f.open;
        const tok_kind before_verified = after_verified ? DOC->toks[last - 1].kind : T_EOF;
        const bool view = f.header == HEADER_SYSTEM && f.open >= 2 && DOC->toks[f.open - 2].kind == T_IDENT
                       && str_eq_c(DOC->toks[f.open - 2].text, "view");
        if (trigger) {
            const bool command = str_eq_c(DOC->toks[f.open - 1].text, "command");
            // local event(verified Died died): a local handler hears the match's events, confirmed ones too
            const bool hears = !command && local_trigger(f.open);
            if (pk == T_LPAREN) {
                if (hears) {
                    item(&c, "verified", CK_KEYWORD, "Once the server confirmed the tick that sent the event", NULL, NULL);
                }
                complete_events(&c, true, command);
                complete_namespaces(&c, false);
            } else if (hears && before_verified == T_LPAREN) {
                complete_match_events(&c);
                complete_namespaces(&c, false);
            } else if (command && pk == T_COMMA) {
                item(&c, "PlayerID", CK_STRUCT, "Authenticated command sender", builtin_type_doc(TY_PLAYER), NULL);
            } else if (pk == T_IDENT) {
                complete_param_name(&c, prev->text);
            }
        } else if (sample) { // Sample(Devices devices, Settings settings)
            if (pk == T_LPAREN || pk == T_COMMA) {
                item(&c, "Devices", CK_CLASS, "This machine's sampled devices", NULL, NULL);
                for (int i = 0; i < A.prog->decls.count; i++) {
                    const decl *d = A.prog->decls.items[i];
                    if (d->kind == DECL_SINGLETON && d->is_local) item(&c, name_for(d), CK_CLASS, decl_what(d), NULL, NULL);
                }
            } else if (pk == T_IDENT) {
                complete_param_name(&c, prev->text);
            }
        } else if (pk == T_WITH || pk == T_WITHOUT) {
            // view Camera(Player player, with ownership): by this machine's player, which only views ask
            if (view) {
                item(&c, "ownership", CK_KEYWORD,
                     pk == T_WITH ? "Entities this machine's player owns" : "Entities this machine's player doesn't own", NULL, NULL);
                // ...and with verified Dead: by the world the server has confirmed, which only they see
                item(&c, "verified", CK_KEYWORD,
                     pk == T_WITH ? "Entities that have it in the world the server has confirmed"
                                  : "Entities that don't have it in the world the server has confirmed", NULL, NULL);
            }
            complete_types(&c, true, false, false);
        } else if (view && (before_verified == T_WITH || before_verified == T_WITHOUT)) {
            complete_match_types(&c, false);
            complete_namespaces(&c, false);
        } else if (view && (before_verified == T_LPAREN || before_verified == T_COMMA || before_verified == T_MUT)) {
            complete_match_types(&c, true);
            complete_namespaces(&c, false);
        } else if (pk == T_LPAREN || pk == T_COMMA || pk == T_MUT) {
            if (pk != T_MUT) {
                item(&c, "mut", CK_KEYWORD, "Write access", NULL, NULL);
                // view HealthBar(Health health, verified Health confirmed): where a view's parameter starts, like mut
                if (view) item(&c, "verified", CK_KEYWORD, "As the server confirmed it, not as this machine predicts it", NULL, NULL);
                item(&c, "with", CK_KEYWORD, "Entities must have this component", NULL, NULL);
                item(&c, "without", CK_KEYWORD, "Entities must not have this component", NULL, NULL);
                if (!extern_system) {
                    item(&c, "Devices", CK_CLASS, "This machine's frame in views; the entity owner's sample in match code", NULL, NULL);
                }
                // Systems, views and local handlers take a second set of entities to go through
                if (f.open >= 2 && !is_async && !extern_system
                    && (DOC->toks[f.open - 2].kind == T_SYSTEM || view || local_handler(f.open))) {
                    item(&c, "query", CK_SNIPPET, "query (Component value, with Tag) entities",
                         "A read-only second entity set, traversed with foreach independently of the outer entity.",
                         "query (${1:Component} ${2:value}) ${3:entities}");
                }
            }
            if (!extern_system) {
                item(&c, "World", CK_CLASS, "Any entity's components, by its handle: world.Get<Health>(e)", builtin_type_doc(TY_WORLD), NULL);
            }
            complete_types(&c, true, true, pk != T_MUT);
            complete_namespaces(&c, false);
        } else if (pk == T_IDENT) {
            complete_param_name(&c, prev->text);
        }
        break;
    }

    case CTX_DATA: {
        // settings { ... }: the settings it doesn't set yet, and after '=', a constant
        if (f.open >= 1 && DOC->toks[f.open - 1].kind == T_IDENT && str_eq_c(DOC->toks[f.open - 1].text, "settings")) {
            if (pk == T_LBRACE || pk == T_SEMI) {
                complete_settings(&c, f.open, last);
            } else if (pk != T_IDENT || (last >= 1 && DOC->toks[last - 1].kind != T_LBRACE && DOC->toks[last - 1].kind != T_SEMI)) {
                complete_value_types(&c, VT_VALUES);
                complete_constants(&c, TY_ERROR);
                item(&c, "Math", CK_MODULE, "Math functions and constants", NULL, NULL);
            }
            break;
        }
        // What the body belongs to: `struct Name {`, `component Name {`, `enum Name : byte {`, ...
        const token *keyword = f.open >= 2 ? &DOC->toks[f.open - 2] : NULL;
        const bool backed = keyword && keyword->kind == T_COLON && f.open >= 4;
        if (backed) keyword = &DOC->toks[f.open - 4];
        const bool is_struct = keyword && keyword->kind == T_IDENT && str_eq_c(keyword->text, "struct");
        const bool has_methods = is_struct || (keyword && keyword->kind == T_COMPONENT);
        if (keyword && keyword->kind == T_IDENT && str_eq_c(keyword->text, "enum")) {
            // Members are names; after '=', an int, which a constant can give.
            bool value = false;
            for (int k = last; k > f.open && DOC->toks[k].kind != T_COMMA && !value; k--) {
                value = DOC->toks[k].kind == T_ASSIGN;
            }
            if (value) complete_constants(&c, TY_INT);
            break;
        }
        const bool member_start = last >= 1 && (DOC->toks[last - 1].kind == T_LBRACE || DOC->toks[last - 1].kind == T_SEMI
                                                || DOC->toks[last - 1].kind == T_RBRACE);
        if (pk == T_LBRACE || pk == T_SEMI || pk == T_RBRACE) {
            // A field's type: inputs send numbers, and only ECS data keeps grids.
            const bool input = keyword && keyword->kind == T_IDENT && str_eq_c(keyword->text, "input");
            const bool grids = keyword && (keyword->kind == T_COMPONENT || keyword->kind == T_SINGLETON
                                           || (keyword->kind == T_IDENT && str_eq_c(keyword->text, "scene")));
            complete_value_types(&c, (input ? VT_VALUES | VT_BOOL | VT_ENTITY | VT_LOCAL_ENTITY : VT_LOCALS | VT_LOCAL_ENTITY)
                                         | (grids ? VT_GRID : 0));
            complete_structs(&c);
            complete_namespaces(&c, false);
            if (has_methods) {
                item(&c, "void", CK_KEYWORD, "Returns nothing", NULL, NULL);
                item(&c, "mut", CK_KEYWORD, "A method that changes the fields", NULL, NULL);
                item(&c, "method", CK_SNIPPET, "Type Name(parameters) { ... }", "Reads the fields, which are in scope by name.",
                     "${1:void} ${2:Name}($3)\n{\n    $0\n}");
                item(&c, "mut method", CK_SNIPPET, "mut void Name(parameters) { ... }", "Changes the fields.",
                     "mut void ${1:Name}($2)\n{\n    $0\n}");
                item(&c, "virtual", CK_KEYWORD, "virtual Type Name(parameters) { ... }",
                     "A method another package can replace, with an `override` written outside the type.", NULL);
            }
            if (is_struct) {
                sb snippet = {0};
                const str name = DOC->toks[f.open - 1].text;
                sb_printf(&snippet, STR_FMT " operator ${1:+}(" STR_FMT " a, " STR_FMT " b)\n{\n    return $0;\n}",
                          STR_ARG(name), STR_ARG(name), STR_ARG(name));
                item(&c, "operator", CK_SNIPPET, "Type operator +(Type a, Type b) { ... }", "Lets '+' and the others work on the struct.",
                     snippet.data);
            }
        } else if (pk == T_IDENT && member_start && has_methods && str_eq_c(prev->text, "virtual")) { // Its method's type
            item(&c, "void", CK_KEYWORD, "Returns nothing", NULL, NULL);
            item(&c, "mut", CK_KEYWORD, "A method that changes the fields", NULL, NULL);
            complete_value_types(&c, VT_LOCALS | VT_LOCAL_ENTITY);
            complete_structs(&c);
            complete_namespaces(&c, false);
        } else if (pk == T_IDENT && member_start && is_struct) {
            item(&c, "operator", CK_KEYWORD, "Type operator +(Type a, Type b)", NULL, NULL); // After the return type
        } else if (pk != T_IDENT || (last >= 1 && DOC->toks[last - 1].kind != T_LBRACE && DOC->toks[last - 1].kind != T_SEMI
                                   && DOC->toks[last - 1].kind != T_RBRACE)) {
            // A default value: constants only, and a grid field's size.
            complete_value_types(&c, VT_VALUES);
            complete_structs(&c);
            complete_constants(&c, TY_ERROR);
            item(&c, "Math", CK_MODULE, "Math functions and constants", NULL, NULL);
            int first = last; // The field's first token: its type
            while (first > f.open + 1 && DOC->toks[first - 1].kind != T_SEMI && DOC->toks[first - 1].kind != T_LBRACE
                   && DOC->toks[first - 1].kind != T_RBRACE && DOC->toks[first - 1].kind != T_RBRACKET) {
                first--;
            }
            const str type_text = DOC->toks[first].text;
            if (DOC->toks[first].kind == T_IDENT && (str_eq_c(type_text, "Grid2") || str_eq_c(type_text, "Grid3"))) {
                const bool flat = str_eq_c(type_text, "Grid2");
                item(&c, flat ? "Grid2" : "Grid3", CK_FUNCTION,
                     flat ? "Grid2(int width = 0, int height = 0)" : "Grid3(int width = 0, int height = 0, int depth = 0)", GRID_SIZE_DOC,
                     flat ? "Grid2($1)" : "Grid3($1)");
            }
        }
        break;
    }

    case CTX_LITERAL:
        if ((pk == T_LBRACE || pk == T_COMMA) && f.open >= 1) {
            const str name = DOC->toks[f.open - 1].text;
            for (int i = 0; i < A.prog->decls.count; i++) {
                const decl *d = A.prog->decls.items[i];
                if ((d->kind != DECL_COMPONENT && d->kind != DECL_STRUCT && d->kind != DECL_EVENT) || !str_eq(d->name, name)) continue;
                for (int k = 0; k < d->fields.count; k++) {
                    const field *fl = &d->fields.items[k];
                    if (fl->hidden) continue;
                    sb snippet = {0};
                    sb_printf(&snippet, STR_FMT " = $0", STR_ARG(fl->name));
                    item(&c, str_to_cstr(fl->name), CK_FIELD, type_name(fl->type), NULL, snippet.data);
                }
            }
        } else {
            complete_expression(&c, at, false);
        }
        break;

    case CTX_CODE: {
        if (pk == T_VAR) break; // Naming a local
        // TryHalf(8, out : what the argument for an `out` parameter goes on with
        int commas;
        const int call = open_call(last, &commas);
        const out_argument made = call >= 1 ? out_argument_at(call, last, at) : (out_argument){.stage = OUT_NONE};
        if (made.stage == OUT_WORD || made.stage == OUT_MUT) {
            complete_out_argument(&c, &made, at);
            break;
        }
        if (pk == T_MUT) { // mut var count = 0;, or mut int count = 0;
            const scope sc = scope_at(at);
            const bool local = sc.decl && (sc.decl->is_view || sc.decl->is_local);
            item(&c, "var", CK_KEYWORD, NULL, NULL, NULL);
            complete_value_types(&c, is_shader(sc.decl) ? VT_VALUES | VT_BOOL | VT_GPU
                                     : local            ? VT_LOCALS | VT_LOCAL_ENTITY
                                                        : VT_LOCALS);
            complete_structs(&c);
            complete_namespaces(&c, false);
            break;
        }
        // foreach (var item in ...): `in` after the name
        if (pk == T_IDENT && loop_variable_named(last)) {
            item(&c, "in", CK_KEYWORD, NULL, NULL, NULL);
            break;
        }
        // parallel (var at in cells by 2 offset 1): its blocks and where they start
        const int header = parallel_header(last);
        const bool keyword = pk == T_IDENT && (str_eq_c(prev->text, "in") || str_eq_c(prev->text, "by")
                                               || str_eq_c(prev->text, "offset"));
        bool after_in = false;
        for (int k = header + 1; header >= 0 && k < last && !after_in; k++) {
            after_in = DOC->toks[k].kind == T_IDENT && str_eq_c(DOC->toks[k].text, "in");
        }
        if (after_in && !keyword && ends_operand(pk)) {
            bool by = false;
            bool offset = false;
            for (int k = header + 1; k <= last; k++) {
                if (DOC->toks[k].kind != T_IDENT) continue;
                by |= str_eq_c(DOC->toks[k].text, "by");
                offset |= str_eq_c(DOC->toks[k].text, "offset");
            }
            if (!by && !offset) {
                item(&c, "by", CK_KEYWORD, "by 2", "Each step gets a block of cells this size, rather than one cell.", NULL);
            }
            if (!offset) item(&c, "offset", CK_KEYWORD, "offset 1", "Where the blocks start, like 'time.tick % 2'.", NULL);
            break;
        }
        // `Type name` declares a local: no suggestions for the name.
        const tok_kind before = last >= 1 ? DOC->toks[last - 1].kind : T_EOF;
        type ignored;
        const bool statement_start = before == T_LBRACE || before == T_RBRACE || before == T_SEMI || before == T_MUT;
        const bool type_written = pk == T_IDENT && (builtin_type_named(prev->text, &ignored) || str_eq_c(prev->text, "string"));
        if (statement_start && type_written) break;
        // Draw.Mesh(vertices, indices, ...: the vert and the frag a view draws through, by name
        if (call >= 0 && opens_mesh_draw(call) && commas >= (str_eq_c(DOC->toks[call - 1].text, "Instanced") ? 3 : 2)) {
            const scope sc = scope_at(at);
            call_args args;
            call_arguments(call, &args);
            const decl *vertex = mesh_vertex(&sc, args.first[0], args.last[0]);
            // ...and after them, the values they take
            for (int a = 2; sc.decl && sc.decl->is_view && a < args.count && a < commas; a++) {
                complete_draw_values(&c, shader_written(args.first[a], args.last[a]));
            }
            if (sc.decl && sc.decl->is_view) complete_shaders(&c, vertex);
        } else if (call >= 3 && commas >= 3 && DOC->toks[call - 1].kind == T_IDENT && str_eq_c(DOC->toks[call - 1].text, "Blit")
                   && DOC->toks[call - 2].kind == T_DOT && str_eq_c(DOC->toks[call - 3].text, "Draw")) {
            // Draw.Blit(source, destination, Blur, ...: the values its frag takes
            call_args args;
            call_arguments(call, &args);
            for (int a = 2; a < args.count && a < commas; a++) complete_draw_values(&c, shader_written(args.first[a], args.last[a]));
        }
        // Damage(10, : the names its other parameters can be passed by
        if (call >= 1 && (pk == T_LPAREN || pk == T_COMMA)) complete_argument_names(&c, call, last);
        // TryHalf(8, : an `out` parameter's argument says `out`, and where every function of that name has one
        // there, nothing else fits
        if (made.stage == OUT_START) {
            complete_out_argument(&c, &made, at);
            if (made.only) break;
        }
        const bool at_statement = pk == T_LBRACE || pk == T_RBRACE || pk == T_SEMI || pk == T_RPAREN || pk == T_ELSE;
        complete_expression(&c, at, at_statement);
        break;
    }
    }

done:
    jb_put(out, "]}");
}

// ---------------------------------------------------------------------------
// References, highlights and rename: every occurrence of one symbol

// Do `a` and `b` refer to the same thing?
static bool same_symbol(const occurrence *a, const occurrence *b)
{
    if (a->kind != b->kind) return false;
    switch (a->kind) {
    case OCC_TYPE:
        if (a->backing || b->backing) return a->backing == b->backing && str_eq(a->name, b->name);
        return a->decl ? a->decl == b->decl : !b->decl && a->type.kind == b->type.kind;
    case OCC_SYSTEM: return replaced_or_self(a->decl) == replaced_or_self(b->decl); // An override is what it replaced, by name
    case OCC_FIELD: return a->field == b->field;
    case OCC_PARAM: return a->param == b->param;
    case OCC_LOCAL: return a->local == b->local;
    case OCC_OWNER: return str_eq(a->owner, b->owner);
    case OCC_FUNCTION:
    case OCC_CONSTANT:
        if (is_routine(a->decl) || is_routine(b->decl)) return replaced_or_self(a->decl) == replaced_or_self(b->decl);
        return str_eq(a->owner, b->owner) && str_eq(a->name, b->name);
    case OCC_METHOD: // A built-in one is what it's called on's: a list's Add isn't an entity's
        if (is_routine(a->decl) || is_routine(b->decl)) return replaced_or_self(a->decl) == replaced_or_self(b->decl);
        return str_eq(a->name, b->name) && a->call == b->call && a->object_type.kind == b->object_type.kind;
    case OCC_MEMBER:
    case OCC_NAMESPACE:
    case OCC_ATTRIBUTE:
    case OCC_SETTING: return str_eq(a->name, b->name);
    case OCC_ENUM_MEMBER: return a->decl == b->decl && str_eq(a->name, b->name);
    case OCC_THIS:
    case OCC_CONST: return a->decl == b->decl;
    case OCC_DEFAULT:
    case OCC_OWNERSHIP:
    case OCC_VERIFIED:
    case OCC_OUT:
    case OCC_REPLACE: return false; // A keyword, not a symbol
    }
    return false;
}

void analysis_references(const char *uri, const int line, const int character, const bool declaration, jbuf *out)
{
    (void)uri;
    const occurrence *target = occurrence_at(from_lsp(line, character));
    jb_put(out, "[");
    int written = 0;
    for (int i = 0; target && i < A.occs.count; i++) {
        const occurrence *o = &A.occs.items[i];
        if (!same_symbol(target, o) || (o->declaration && !declaration)) continue;
        if (written++) jb_put(out, ",");
        jb_put(out, "{\"uri\":");
        jb_string(out, A.files[o->at.file].uri); // Any file of the game
        jb_put(out, ",\"range\":");
        write_range(out, o->at, o->len);
        jb_put(out, "}");
    }
    jb_put(out, "]");
}

enum { HIGHLIGHT_READ = 2, HIGHLIGHT_WRITE = 3 };

void analysis_highlights(const int line, const int character, jbuf *out)
{
    const occurrence *target = occurrence_at(from_lsp(line, character));
    jb_put(out, "[");
    int written = 0;
    for (int i = 0; target && i < A.occs.count; i++) {
        const occurrence *o = &A.occs.items[i];
        if (!same_symbol(target, o) || o->at.file != A.doc) continue; // Highlights are per document
        if (written++) jb_put(out, ",");
        jb_put(out, "{\"range\":");
        write_range(out, o->at, o->len);
        jb_printf(out, ",\"kind\":%d}", o->declaration || o->write ? HIGHLIGHT_WRITE : HIGHLIGHT_READ);
    }
    jb_put(out, "]");
}

// The occurrence at the cursor if it can be renamed, or an error for the user.
static const char *rename_target(const int line, const int character, const occurrence **out)
{
    if (A.syntax_errors > 0) return "Fix the syntax errors first: uses in code that doesn't parse would be missed.";
    const occurrence *o = occurrence_at(from_lsp(line, character));
    *out = o;
    if (!o) return "Nothing to rename here.";
    switch (o->kind) {
    case OCC_TYPE:
        if (!o->decl) return "Built-in types can't be renamed.";
        if (o->decl->builtin) return "Types built into the engine can't be renamed.";
        return NULL;
    case OCC_FIELD:
        if (!o->decl || o->decl->builtin) return "Fields of built-in types can't be renamed.";
        return NULL;
    case OCC_ENUM_MEMBER:
        if (!o->decl || o->decl->builtin) return "Members of the engine's enums can't be renamed.";
        return NULL;
    case OCC_SYSTEM:
    case OCC_PARAM:
    case OCC_LOCAL:
    case OCC_NAMESPACE:
    case OCC_CONST:
        return NULL;
    case OCC_METHOD:
    case OCC_FUNCTION:
        if (is_operator_decl(o->decl)) return "Operators are named by their symbol, so they can't be renamed.";
        if (is_routine(o->decl)) return NULL;
        return "Built-in names can't be renamed.";
    case OCC_THIS:
        return "'this' is a keyword, so it can't be renamed.";
    case OCC_OWNERSHIP:
        return "'ownership' is a keyword after 'with' and 'without', so it can't be renamed.";
    case OCC_VERIFIED:
        return "'verified' is a keyword before a parameter's type or a handler's event, so it can't be renamed.";
    case OCC_OUT:
        return "'out' is a keyword before a parameter's type or an argument, so it can't be renamed.";
    case OCC_REPLACE:
        return "'virtual' and 'override' are keywords before a declaration, so they can't be renamed.";
    case OCC_SETTING:
        return "Settings are the engine's, so they can't be renamed.";
    default:
        return "Built-in names can't be renamed.";
    }
}

const char *analysis_prepare_rename(const int line, const int character, jbuf *out)
{
    const occurrence *o;
    const char *error = rename_target(line, character, &o);
    if (error) return error;
    write_range(out, o->at, o->len);
    return NULL;
}

// Whether an `out` argument in one of a statement's own expressions declares
// a local named `name`: F(out var name).
static bool out_local_named(const stmt *s, const str name)
{
    local_list made = {0};
    out_locals(s->cond, &made);
    out_locals(s->target, &made);
    out_locals(s->value, &made);
    out_locals(s->by, &made);
    out_locals(s->offset, &made);
    for (int i = 0; i < made.count; i++) {
        if (str_eq(made.items[i]->name, name)) return true;
    }
    return false;
}

static bool local_named(const stmt *s, const str name)
{
    if (!s) return false;
    if (s->kind == S_VAR && str_eq(s->name, name)) return true;
    if (out_local_named(s, name)) return true;
    if (s->kind == S_BLOCK) {
        for (int i = 0; i < s->stmts.count; i++) {
            if (local_named(s->stmts.items[i], name)) return true;
        }
    }
    if (s->kind == S_SWITCH) {
        for (int i = 0; i < s->cases.count; i++) {
            for (int k = 0; k < s->cases.items[i].body.count; k++) {
                if (local_named(s->cases.items[i].body.items[k], name)) return true;
            }
        }
    }
    if (s->kind == S_EXPR) {
        for (int k = 0; k < 2; k++) {
            if (s->value->compare[k] && str_eq(s->value->compare[k]->name, name)) return true;
        }
        return local_named(s->value->block, name);
    }
    if (s->kind == S_FOREACH || s->kind == S_PARALLEL) return str_eq(s->name, name) || local_named(s->then_stmt, name);
    if (s->kind == S_WHILE || s->kind == S_FOR) {
        return local_named(s->init, name) || local_named(s->step, name) || local_named(s->then_stmt, name);
    }
    return s->kind == S_IF && (local_named(s->then_stmt, name) || local_named(s->else_stmt, name));
}

// The system, view or input whose body declares `local`.
static const decl *decl_of_local(const stmt *local)
{
    for (int i = 0; i < A.written.count; i++) {
        const decl *d = A.written.items[i];
        for (int k = 0; k < d->methods.count; k++) {
            const decl *m = method_written(d->methods.items[k]);
            const stmt *body = m->body;
            if (body && loc_cmp(body->at, local->at) < 0 && loc_cmp(local->at, body->end) < 0) return m;
        }
        if (d->body && loc_cmp(d->body->at, local->at) < 0 && loc_cmp(local->at, d->body->end) < 0) return d;
        if (d->sanitize && loc_cmp(d->sanitize->at, local->at) < 0 && loc_cmp(local->at, d->sanitize->end) < 0) {
            return d;
        }
    }
    return NULL;
}

// The system, view or input that declares `p`.
static const decl *decl_of_param(const param *p)
{
    for (int i = 0; i < A.written.count; i++) {
        const decl *d = A.written.items[i];
        for (int k = 0; k < d->params.count; k++) {
            if (&d->params.items[k] == p) return d;
            const param *query = &d->params.items[k];
            for (int q = 0; query->mode == PARAM_QUERY && q < query->query.count; q++) {
                if (&query->query.items[q] == p) return d;
            }
        }
        for (int m = 0; m < d->methods.count; m++) {
            const decl *method = method_written(d->methods.items[m]);
            for (int k = 0; k < method->params.count; k++) {
                if (&method->params.items[k] == p) return method;
            }
        }
    }
    return NULL;
}

static bool param_named(const decl *d, const str name)
{
    for (int i = 0; d && i < d->params.count; i++) {
        if (d->params.items[i].name.len > 0 && str_eq(d->params.items[i].name, name)) return true;
    }
    return false;
}

// Whether `name` can replace the target's name, or why not.
static const char *check_new_name(const occurrence *target, const str name)
{
    // Words the parser reads as keywords where a declaration or a part of one
    // goes, though the lexer reads them as names. The first ones only start
    // declarations, so parameters and locals can still take them.
    static const char *const contextual[] = {"input", "view", "struct", "event", "command", "enum", "scene", "local", "namespace",
                                             "using", "extern", "const", "frag", "vert", "async", "settings", "operator",
                                             "virtual", "override",
                                             "fails", "in", "by", "offset", "void"};
    enum { DECL_WORDS = 14 };
    static const char *const reserved[] = {"Math", "Draw", "Draw2D", "Draw3D", "Devices", "Time", "Owner", "Spawn", "Send", "Spawned",
                                           "Destroyed", "PlayerJoined", "PlayerLeft", "Scene", "SceneVisibility", "Prediction", "Debug",
                                           "GUI", "GUILayout", "Screen", "Cursor", "Frame", "Anchor", "Action", "Session", "SessionState", "Clipboard",
                                           "Vertex", "Vertex3", "Shape", "GUITheme", "GUIControl", "Filter", "Wrap", "Font", "Blend", "Depth", "Cull", "Format", "StencilTest", "StencilOp", "Draw", "DrawList", "Random",
                                           "DisconnectReason", "Connected", "Disconnected", "Wait", "File", "Directory", "Path", "Application", "Storage", "IOError", "List", "Map", "Set", "Grid2", "Grid3",
                                           "string", "Sample", "Sanitize"};
    static char message[160];

    if (name.len == 0 || !(isalpha((unsigned char)name.ptr[0]) || name.ptr[0] == '_')) return "Names start with a letter.";
    for (int i = 0; i < name.len; i++) {
        if (!isalnum((unsigned char)name.ptr[i]) && name.ptr[i] != '_') return "Names use letters, digits and '_' only.";
    }
    // The lexer's own keywords: it reads them as something other than a name.
    const source text = {"", name.ptr, (size_t)name.len, 0, NULL};
    const token *word = lex_all(&text);
    if (!word || word[0].kind != T_IDENT) return "That's a keyword.";
    const bool variable = target->kind == OCC_PARAM || target->kind == OCC_LOCAL;
    for (size_t i = variable ? DECL_WORDS : 0; i < sizeof contextual / sizeof contextual[0]; i++) {
        if (str_eq_c(name, contextual[i])) return "That's a keyword.";
    }
    type ignored;
    for (size_t i = 0; i < sizeof reserved / sizeof reserved[0]; i++) {
        if (str_eq_c(name, reserved[i])) return "That name is built into the language.";
    }
    if (builtin_type_named(name, &ignored)) return "That name is built into the language.";
    if (str_starts_with_c(name, "tide_")) return "Names starting with 'tide_' are reserved for generated code.";

    if (target->kind == OCC_TYPE && str_eq_c(name, "World") && target->decl && target->decl->unit
        && target->decl->unit->ns.len == 0) {
        return "'World' is built into the engine: a type of that name goes in a namespace.";
    }

    bool clash = false;
    switch (target->kind) {
    case OCC_TYPE:
    case OCC_SYSTEM:
        // Types share one namespace, and systems and views another; a
        // function, a frag or a vert shares its name with nothing.
        for (int i = 0; i < A.prog->decls.count; i++) {
            const decl *d = A.prog->decls.items[i];
            const bool same_namespace = d->kind == DECL_FUNCTION || is_shader(d) || is_shader(target->decl)
                                     || (d->kind == DECL_SYSTEM) == (target->kind == OCC_SYSTEM);
            if (d != target->decl && same_namespace && str_eq(d->name, name)) clash = true;
        }
        break;
    case OCC_METHOD:
    case OCC_FUNCTION:
    case OCC_CONST:
        if (target->decl->owner) { // A method: its type's other methods and fields
            const decl *owner = target->decl->owner;
            for (int i = 0; i < owner->methods.count; i++) {
                if (owner->methods.items[i] != target->decl && str_eq(owner->methods.items[i]->name, name)) clash = true;
            }
            for (int i = 0; i < owner->fields.count + local_field_count(owner); i++) {
                if (str_eq(field_of(owner, i)->name, name)) clash = true;
            }
        } else {
            for (int i = 0; i < A.prog->decls.count; i++) {
                const decl *d = A.prog->decls.items[i];
                if (d != target->decl && str_eq(d->name, name)) clash = true;
            }
        }
        break;
    case OCC_FIELD:
        for (int i = 0; i < target->decl->fields.count + local_field_count(target->decl); i++) {
            const field *f = field_of(target->decl, i);
            if (f != target->field && str_eq(f->name, name)) clash = true;
        }
        break;
    case OCC_ENUM_MEMBER:
        for (int i = 0; i < target->decl->members.count; i++) {
            if (str_eq(target->decl->members.items[i].name, name)) clash = true;
        }
        break;
    case OCC_PARAM: {
        const decl *d = decl_of_param(target->param);
        const param *query = target->param->query_source;
        clash = query ? str_eq_c(name, "entity") || query_binding(query, name) != NULL
                      : param_named(d, name) || (d && local_named(d->body, name));
        break;
    }
    case OCC_LOCAL: {
        const decl *d = decl_of_local(target->local);
        clash = param_named(d, name) || (d && local_named(d->body, name));
        break;
    }
    case OCC_NAMESPACE: {
        // Another namespace, or a declaration, with the new path: Game.Fight for Game.Combat
        const str old_path = target->name;
        const int parent = old_path.len - last_part(old_path).len;
        sb renamed = {0};
        sb_printf(&renamed, "%.*s" STR_FMT, parent, old_path.ptr, STR_ARG(name));
        const str path = {renamed.data, (int)renamed.len};
        for (int i = 0; i < A.prog->units.count; i++) {
            const str ns = A.prog->units.items[i]->ns;
            if (str_eq(ns, path) || (ns.len > path.len && ns.ptr[path.len] == '.' && memcmp(ns.ptr, path.ptr, (size_t)path.len) == 0)) {
                clash = true;
            }
        }
        for (int i = 0; i < A.prog->decls.count; i++) {
            if (str_eq(A.prog->decls.items[i]->qualified, path)) clash = true;
        }
        break;
    }
    default:
        break;
    }
    if (clash) {
        snprintf(message, sizeof message, "'" STR_FMT "' is already declared there.", STR_ARG(name));
        return message;
    }
    return NULL;
}

const char *analysis_rename(const char *uri, const int line, const int character, const char *new_name, jbuf *out)
{
    const occurrence *target;
    const char *error = rename_target(line, character, &target);
    if (error) return error;
    error = check_new_name(target, str_from(new_name));
    if (error) return error;

    // Edits grouped by file: every file of the game that uses the name.
    (void)uri;
    jb_put(out, "{\"changes\":{");
    int files_written = 0;
    for (int file = 0; file < A.file_count; file++) {
        int written = 0;
        for (int i = 0; i < A.occs.count; i++) {
            const occurrence *o = &A.occs.items[i];
            if (o->at.file != file || !same_symbol(target, o)) continue;
            if (written++ == 0) {
                if (files_written++) jb_put(out, ",");
                jb_string(out, A.files[file].uri);
                jb_put(out, ":[");
            } else {
                jb_put(out, ",");
            }
            jb_put(out, "{\"range\":");
            write_range(out, o->at, o->len);
            jb_put(out, ",\"newText\":");
            jb_string(out, new_name);
            jb_put(out, "}");
        }
        if (written) jb_put(out, "]");
    }
    jb_put(out, "}}");
    return NULL;
}

// ---------------------------------------------------------------------------
// Signature help: the parameters of the call being typed

typedef struct signature_list {
    jbuf *out;
    int arg;          // The argument the cursor is in
    int active;       // The first signature with enough parameters, or -1
    int active_param; // Its parameter under the cursor
    int shown;
    str named;        // The name that argument is written with, `crit` in `crit: true`, or empty: its parameter is the one named so
    bool picked;      // The next signature is the one the call means, whichever came before
} signature_list;

// The parameters in a signature's label, "Name(float2 center, float radius = 1)":
// where each starts and ends, apart from the commas and parentheses of a
// default's own, a generic type's, and text's. Returns how many (16 at most),
// or -1 for a label with no parameter list.
static int label_split(const char *label, int starts[16], int ends[16])
{
    const char *open = strchr(label, '(');
    if (!open) return -1;
    int count = 0;
    int depth = 0;
    int angle = 0;
    bool in_default = false;
    const char *start = open + 1;
    for (const char *p = open + 1; *p; p++) {
        const char ch = *p;
        if (ch == '"') { // Text, as a default
            for (p++; *p && *p != '"'; p++) {
                if (*p == '\\' && p[1]) p++;
            }
            if (!*p) return -1;
        } else if (ch == '(' || ch == '[' || ch == '{') {
            depth++;
        } else if ((ch == ')' || ch == ']' || ch == '}') && depth > 0) {
            depth--;
        } else if (ch == '<' && !in_default) {
            angle++;
        } else if (ch == '>' && !in_default && angle > 0) {
            angle--;
        } else if (ch == '=' && depth == 0) {
            in_default = true;
        } else if ((ch == ',' || ch == ')') && depth == 0 && angle == 0) {
            if (p > start && count < 16) {
                starts[count] = (int)(start - label);
                ends[count] = (int)(p - label);
                count++;
            }
            if (ch == ')') return count;
            start = p + 1;
            while (*start == ' ') start++;
            in_default = false;
        }
    }
    return -1;
}

// Adds a signature, finding its parameters in the label: "Name(float2 center, float radius)".
static void add_signature(signature_list *s, const char *label, const char *doc)
{
    int starts[16];
    int ends[16];
    const int params = label_split(label, starts, ends);
    if (params < 0) return;
    const bool variadic = strstr(label, "...") != NULL;
    // The parameter the cursor's argument goes to: by its place, or by the name it's written with
    int at = variadic && s->arg >= params ? params - 1 : s->arg;
    bool takes = variadic || params > s->arg;
    if (s->named.len > 0) {
        takes = false;
        for (int i = 0; i < params && !takes; i++) {
            const char *end = label + ends[i];
            const char *value = strstr(label + starts[i], " = ");
            if (value && value < end) end = value;
            const char *name = end;
            while (name > label + starts[i] && name[-1] != ' ') name--;
            if (end - name == s->named.len && memcmp(name, s->named.ptr, (size_t)s->named.len) == 0) {
                takes = true;
                at = i;
            }
        }
    }
    if ((s->active < 0 || s->picked) && takes) {
        s->active = s->shown;
        s->active_param = at;
    }
    s->picked = false;

    if (s->shown++) jb_put(s->out, ",");
    jb_put(s->out, "{\"label\":");
    jb_string(s->out, label);
    if (doc) {
        jb_put(s->out, ",\"documentation\":{\"kind\":\"markdown\",\"value\":");
        jb_string(s->out, doc);
        jb_put(s->out, "}");
    }
    jb_put(s->out, ",\"parameters\":[");
    for (int i = 0; i < params; i++) jb_printf(s->out, "%s{\"label\":[%d,%d]}", i ? "," : "", starts[i], ends[i]);
    jb_put(s->out, "]}");
}

static void visit_signature(void *user, const char *label, const char *doc)
{
    add_signature(user, label, doc);
}

// Constructors and other calls that aren't in the built-in function table.
static const struct {
    const char *name;
    const char *forms[4];
    const char *doc;
} call_forms[] = {
    {"byte", {"byte(int value)", "byte(long value)", "byte(float value)"}, "Keeps an integer's low 8 bits. "
                                                                       "A float first converts with int(x), then keeps its low 8 bits."},
    {"int", {"int(float value)", "int(long value)"}, "From a float: truncates toward zero and saturates at the int range; NaN gives 0. "
                                                   "From a long: its low 32 bits."},
    {"long", {"long(float value)", "long(int value)"}, "From a float: truncates toward zero and saturates at the long range; NaN gives 0. "
                                                     "An int converts by itself."},
    {"float", {"float(int value)", "float(long value)"}, "A long rounds to the nearest float."},
    {"int2", {"int2(int x, int y)", "int2(int value)", "int2(float2 value)"}, NULL},
    {"int3", {"int3(int x, int y, int z)", "int3(int2 xy, int z)", "int3(int value)"}, NULL},
    {"int4", {"int4(int x, int y, int z, int w)", "int4(int3 xyz, int w)", "int4(int value)"}, NULL},
    {"float2", {"float2(float x, float y)", "float2(float value)", "float2(int2 value)"}, NULL},
    {"float3", {"float3(float x, float y, float z)", "float3(float2 xy, float z)", "float3(float value)"}, NULL},
    {"float4", {"float4(float x, float y, float z, float w)", "float4(float3 xyz, float w)", "float4(float value)"}, NULL},
    {"quaternion", {"quaternion(float x, float y, float z, float w)", "quaternion(float4 value)", "quaternion(float3x3 rotation)"}, NULL},
    {"float2x2", {"float2x2(float2 c0, float2 c1)", "float2x2(float m00, float m01, float m10, float m11)"}, "Columns, or numbers row by row."},
    {"float3x3", {"float3x3(float3 c0, float3 c1, float3 c2)", "float3x3(quaternion rotation)"}, NULL},
    {"float4x4", {"float4x4(float4 c0, float4 c1, float4 c2, float4 c3)", "float4x4(float3x3 rotation, float3 translation)"}, NULL},
    {"Color", {"Color(float r, float g, float b)", "Color(float r, float g, float b, float a)"}, "Channels from 0 to 1."},
    {"Rect", {"Rect(float x, float y, float width, float height)"}, "From the top left of the screen, y down, in GUI units."},
    {"RenderTarget", {"RenderTarget(float width, float height)", "RenderTarget(float width, float height, float pixelsPerUnit)",
                      "RenderTarget(float width, float height, Format formats...)",
                      "RenderTarget(float width, float height, float pixelsPerUnit, Format formats...)"},
     "Creates a local image of a positive size in logical pixels, like the window's. Its image has the display's pixels in each, so it's as sharp as the window, or `pixelsPerUnit` of them: with 1, exactly `width` by `height` pixels on any display. It holds one color image beside its depth, or what its formats say, one for each color image, four at the most: `Format.Rgba8` is 4 bytes a pixel and `Format.Rgba16f` four floats, which keep any number. A frag's `out Color` parameters go to them in order, and `target.colors[1]` reads the second. `Format.Depth`, by itself, is depth alone, for a pass whose depth a frag reads afterwards (`target.depth`). Last, by name, its samples: `RenderTarget(width, height, samples: 4)` draws each pixel with 4 samples (1, 2, 4 or 8; a device with fewer draws it with what it has), which smooths the edges of the triangles drawn into it, for that many times the memory; its color is read as any target's, and its depth isn't a frag's to read. Color and depth persist until cleared; copies share it. Dispose it when finished. `RenderTarget.Window()` is one that's always the window's size."},
    {"Cubemap", {"Cubemap(float size)", "Cubemap(float size, Format format)"},
     "A Cubemap that's drawn into: six square images of `size` pixels a side, each a destination, `cube.faces[0]` "
     "to `cube.faces[5]` in the order +x, -x, +y, -y, +z, -z, with depth of its own, and all six what a frag "
     "reads by a direction, `cube.Sample(direction)`. A face is what a camera at the cube's middle sees along "
     "its axis with a field of view of 90. Its faces hold `Format.Rgba8`, or `Format.Rgba16f`, floats that keep "
     "any number. It starts black; copies share it, and `Dispose()` lets it go."},
    {"PlayerID", {"PlayerID(int index)"}, "A player by index, for local play and tests."},
    {"Random", {"Random(long seed)"}, RANDOM_SEEDED_DOC},
    {"Spawn", {"Spawn(components...)"}, "Creates an entity with these components. It's added at the end of the tick."},
    {"Send", {"Send(event)"}, "Sends an event to the whole world, handled at the end of the tick."},
    {"Grid2", {"Grid2(int width = 0, int height = 0)"}, GRID_SIZE_DOC},
    {"Grid3", {"Grid3(int width = 0, int height = 0, int depth = 0)"}, GRID_SIZE_DOC},
};

// The type of what's before the dot at token `dot`, from what's written
// there: `this`, text, or names like unit.stats. TY_ERROR for anything else.
static type receiver_type(const int dot, const loc at)
{
    if (dot < 1) return (type){TY_ERROR, NULL};
    const token *before = &DOC->toks[dot - 1];
    if (before->kind == T_STRING) return (type){TY_STRING, NULL};
    if ((before->kind == T_INTERP || before->kind == T_INTERP_PART) && before->text.ptr[before->text.len - 1] == '"') {
        return (type){TY_STRING, NULL};
    }
    const scope sc = scope_at(at);
    if (before->kind == T_THIS) return this_type(&sc);
    int first = dot - 1;
    while (first >= 2 && DOC->toks[first - 1].kind == T_DOT && DOC->toks[first - 2].kind == T_IDENT) first -= 2;
    if (DOC->toks[first].kind != T_IDENT) return (type){TY_ERROR, NULL};
    const param *ignored;
    type t = name_type(&sc, DOC->toks[first].text, &ignored);
    if (t.kind == TY_ERROR) t = loop_variable_type(&sc, first);
    if (t.kind == TY_ERROR) t = out_variable_type(first, at);
    for (int k = first + 2; k < dot && t.kind != TY_ERROR; k += 2) t = member_type(t, DOC->toks[k].text);
    return t;
}

// The token that closes the type the '<' at token `i` opens after a method's
// name, as in world.Get<Health>(e): a name, maybe qualified, then '>' and the
// call's '('. -1 where the '<' compares.
static int type_argument_close(const int i)
{
    if (i < 2 || DOC->toks[i].kind != T_LT || DOC->toks[i - 1].kind != T_IDENT || DOC->toks[i - 2].kind != T_DOT) return -1;
    int k = i + 1;
    if (k >= DOC->tok_count || DOC->toks[k].kind != T_IDENT) return -1;
    k++;
    while (k + 1 < DOC->tok_count && DOC->toks[k].kind == T_DOT && DOC->toks[k + 1].kind == T_IDENT) k += 2;
    if (k + 1 >= DOC->tok_count || DOC->toks[k].kind != T_GT || DOC->toks[k + 1].kind != T_LPAREN) return -1;
    return k;
}

// The '<' that the '>' at token `i` closes, if they hold a method's type, or -1.
static int type_argument_open(const int i)
{
    if (i < 0 || DOC->toks[i].kind != T_GT) return -1;
    int k = i - 1;
    while (k > 0 && (DOC->toks[k].kind == T_IDENT || DOC->toks[k].kind == T_DOT)) k--;
    return k > 0 && type_argument_close(k) == i ? k : -1;
}

// The component written between the '<' at token `open` and the '>' at
// `close`, as the document names it, or NULL.
static const decl *component_written(const int open, const int close)
{
    sb written = {0};
    for (int k = open + 1; k < close; k++) sb_printf(&written, STR_FMT, STR_ARG(DOC->toks[k].text));
    for (int i = 0; written.len > 0 && i < A.prog->decls.count; i++) {
        const decl *d = A.prog->decls.items[i];
        if (d->kind != DECL_COMPONENT) continue;
        if (strcmp(name_for(d), written.data) == 0 || str_eq_c(d->qualified, written.data)) return d;
    }
    return NULL;
}

// A signature's parameters, from its label as add_signature reads them: each
// a type and a name ("float2 center"), or only a name ("x" in Math.Clamp).
typedef struct label_params {
    str types[16];
    str names[16];
    int count; // -1 for a label that takes any number, like "Spawn(components...)"
} label_params;

static void parse_label(const char *label, label_params *out)
{
    out->count = 0;
    int starts[16];
    int ends[16];
    const int count = label_split(label, starts, ends);
    if (count < 0) return;
    if (strstr(label, "...")) {
        out->count = -1;
        return;
    }
    for (int i = 0; i < count; i++) {
        const char *p = label + starts[i];
        const char *end = label + ends[i];
        const char *value = strstr(p, " = "); // "bool overwrite = false": the default isn't the name
        if (value && value < end) end = value;
        const char *name = end; // The last word, and the one before it: "mut float value"
        while (name > p && name[-1] != ' ') name--;
        const char *type_end = name > p ? name - 1 : p;
        const char *type_start = type_end;
        while (type_start > p && type_start[-1] != ' ') type_start--;
        out->names[out->count] = (str){name, (int)(end - name)};
        out->types[out->count] = (str){type_start, (int)(type_end - type_start)};
        out->count++;
    }
    // A GUI container's block isn't an argument
    if (out->count > 0 && str_eq_c(out->types[out->count - 1], "Block")) out->count--;
}

// Whether a checked call's argument `k` goes to a parameter of type `type_text`:
// the type the call converted it to, if it says, or else one it can take.
static bool param_fits(const str type_text, const expr *call, const int k)
{
    type want;
    if (type_text.len == 0) return true;
    if (str_eq_c(type_text, "string")) want = (type){TY_STRING, NULL};
    else if (!builtin_type_named(type_text, &want)) return true; // Anchor, or a list's element: nothing to tell apart
    if (call->arg_want.count > k) return call->arg_want.items[k].kind == want.kind;
    return type_assignable(want, call->args.items[k]->type);
}

typedef struct label_list {
    VEC(const char *) labels;
} label_list;

static void collect_label(void *user, const char *label, const char *doc)
{
    (void)doc;
    vec_push(((label_list *)user)->labels, label);
}

// Draw.Mesh's two forms, a 2D mesh's and a 3D one's, with what may follow
// their lists in brackets. The built-in signatures (builtins.c) are those of
// a List<Vertex> and a List<Vertex3> with no vert and no frag; the checker
// takes the rest itself (check_mesh_draw).
static const char *const mesh_labels[2] = {
    "Draw.Mesh(vertices, indices[, texture[, filter[, wrap]]][, vert][, frag][, values])",
    "Draw.Mesh(vertices, indices, transform[, texture[, filter[, wrap]]][, vert][, frag][, values])",
};

static const char *const instanced_labels[2] = {
    "Draw.Instanced(vertices, indices, instances[, texture[, filter[, wrap]]][, vert][, frag][, values])",
    "Draw.Instanced(vertices, indices, instances[, texture[, filter[, wrap]]][, vert][, frag][, values])",
};

// Each of its parameters: what it's called in those, and what it takes, in a
// 2D mesh and, where that differs, in a 3D one.
static const struct {
    const char *name;
    const char *doc[2];
} mesh_params[MESH_PARAMS] = {
    [MESH_VERTICES] = {"vertices",
                       {"The mesh's corners: a `List<Vertex>`, or a list of a struct of the game's with a `float2 position`.",
                        "The mesh's corners: a `List<Vertex3>`, or a list of a struct of the game's with a `float3 position`."}},
    [MESH_INDICES] = {"indices", {"A `List<int>`: three for each triangle, places in `vertices`."}},
    [MESH_TRANSFORM] = {"transform", {"A `float4x4`: where the mesh is in the world."}},
    [MESH_TEXTURE] = {"texture",
                      {"A `Texture`, read at each corner's `uv`: an asset's, a render target's color, or a `Grid2<Color>` or "
                       "`Grid2<float>`, which is one here. A frag or vert with a Texture parameter receives it as its source."}},
    [MESH_FILTER] = {"filter", {"How the texture is read between cells, `Filter.Bilinear`, `Filter.Point` or `Filter.Trilinear`: the texture's own "
                                "where the draw leaves it out."}},
    [MESH_WRAP] = {"wrap",
                   {"What a `uv` past 0 or 1 reads, written after the filter: the cells at the texture's edge (`Wrap.Clamp`), "
                    "the texture again (`Wrap.Repeat`), or again the other way round each time (`Wrap.Mirror`). The "
                    "texture's own where the draw leaves it out."}},
    [MESH_VERT] = {"vert", {"A vert, by name: it runs on the GPU for each vertex, and changes it in place."}},
    [MESH_FRAG] = {"frag", {"A frag, by name: it runs on the GPU for each pixel, and writes its color."}},
    [MESH_INSTANCES] = {"instances",
                        {"A List<T> of instance structs, one for each mesh. A vert or frag reads each through a read-only T parameter.",
                         "A List<float4x4> of transforms, or a List<T> of instance structs. A struct's float4x4 transform places "
                         "the mesh; without one, it uses the identity."}},
    [MESH_VALUES] = {"values",
                     {"A struct's value, after the vert and the frag: they read it through a read-only parameter of its type. "
                      "It's sent once for the draw, and only the fields they read."}},
};

// The names of the parameters a checked call's arguments go to, in order:
// the game's function's, or the built-in signature the call took. Returns
// how many; 0 for calls with none to show, like Spawn(...).
static int call_param_names(const expr *call, str *names, const int max)
{
    int count = 0;
    if (call->call == CALL_METHOD || call->call == CALL_FUNCTION) {
        const decl *m = call->method;
        for (; count < m->params.count && count < max; count++) names[count] = m->params.items[count].name;
        return count;
    }
    if (call->call == CALL_DRAW && call->object
        && (is_mesh_draw(call->object->name, call->name) || (call->targeted && str_eq_c(call->name, "Mesh")))) {
        // Each argument's, by what it is: what follows the lists can be left out
        for (; count < call->args.count && count < max; count++) {
            const expr *arg = call->args.items[count];
            const type t = call->arg_want.count > count ? call->arg_want.items[count] : arg->type;
            const int param = count < 2                                  ? count
                            : arg == call->values                         ? MESH_VALUES
                            : count == 2 && call->instanced               ? MESH_INSTANCES
                            : arg->bind == BIND_SHADER && arg->type_decl ? (arg->type_decl->is_frag ? MESH_FRAG : MESH_VERT)
                            : t.kind == TY_FLOAT4X4                      ? MESH_TRANSFORM
                            : t.kind == TY_GRID || t.kind == TY_TEXTURE   ? MESH_TEXTURE
                            : t.kind == TY_ENUM && t.decl == A.prog->wrap ? MESH_WRAP
                            : t.kind == TY_ENUM                          ? MESH_FILTER
                                                                         : MESH_PARAMS;
            names[count] = param == MESH_PARAMS ? (str){"", 0} : str_from(mesh_params[param].name);
        }
        return count;
    }
    if (call->call == CALL_CONSTRUCT && call->ctor == CTOR_ENUM) { // Card(number)
        if (max > 0) names[count++] = str_from("number");
        return count;
    }
    label_list list = {0};
    switch (call->call) {
    case CALL_BUILTIN: case CALL_DRAW: case CALL_AUDIO: case CALL_GUI: case CALL_TEXT: case CALL_RANDOM: {
        if (call->resource_operation) {
            if (call->object && call->object->type.kind == TY_RENDER_TARGET) {
                const method_form *form = find_form(target_calls, sizeof target_calls / sizeof target_calls[0], call->name);
                if (form) vec_push(list.labels, form->form);
            } else if (call->object && call->object->type.kind == TY_CUBEMAP) { // cube.Resize(size)
                const method_form *form = find_form(cube_calls, sizeof cube_calls / sizeof cube_calls[0], call->name);
                if (form) vec_push(list.labels, form->form);
            } else if (!call->object && call->type.kind == TY_CUBEMAP) { // Cubemap(size[, format])
                vec_push(list.labels, call->args.count > 1 ? "Cubemap(float size, Format format)" : "Cubemap(float size)");
            } else if (call->object && !call->target_samples) { // RenderTarget.Window(factor)
                builtin_signatures(call->object->name, call->name, collect_label, &list);
            } else {
                // As the call has them: its size, then what it holds, a format for each color image, where it
                // says, then its samples, where it says those
                const int own = call->args.count - (call->target_samples ? 1 : 0);
                sb label = {0};
                sb_put(&label, call->object ? "RenderTarget.Window(" : "RenderTarget(float width, float height");
                bool first = call->object != NULL;
                for (int at = call->object ? 0 : 2; at < own; at++) {
                    const bool format = call->arg_want.count > at && call->arg_want.items[at].kind == TY_ENUM;
                    sb_printf(&label, "%s%s", first ? "" : ", ",
                              format ? "Format format" : call->object ? "float factor" : "float pixelsPerUnit");
                    first = false;
                }
                if (call->target_samples) sb_printf(&label, "%sint samples", first ? "" : ", ");
                sb_put(&label, ")");
                vec_push(list.labels, label.data);
            }
            break;
        }
        if (call->kind == E_CALL && call->type.kind == TY_STRUCT && call->type.decl == A.prog->random) { // Random(seed)
            vec_push(list.labels, "Random(long seed)");
            break;
        }
        const str owner = call->call == CALL_TEXT ? str_from("string") : call->call == CALL_RANDOM ? str_from("Random")
                          : call->targeted ? target_owner(call)
                          : call->object ? call->object->name : (str){"", 0};
        builtin_signatures(owner, call->name, collect_label, &list);
        break;
    }
    case CALL_CONSTRUCT: case CALL_NEW_GRID:
        for (size_t i = 0; i < sizeof call_forms / sizeof call_forms[0]; i++) {
            if (!str_eq_c(call->name, call_forms[i].name)) continue;
            for (int f = 0; f < 4 && call_forms[i].forms[f]; f++) vec_push(list.labels, call_forms[i].forms[f]);
        }
        break;
    case CALL_SESSION:
        for (size_t i = 0; i < sizeof session_calls / sizeof session_calls[0]; i++) {
            if (str_eq_c(call->name, session_calls[i].name)) vec_push(list.labels, session_calls[i].form);
        }
        break;
    case CALL_LOAD: case CALL_UNLOAD: case CALL_SCENE_PLAYER:
        for (size_t i = 0; i < sizeof scene_calls / sizeof scene_calls[0]; i++) {
            if (str_eq_c(call->name, scene_calls[i].name)) vec_push(list.labels, scene_calls[i].form);
        }
        break;
    case CALL_WAIT:
        for (size_t i = 0; i < sizeof wait_calls / sizeof wait_calls[0]; i++) {
            if (str_eq_c(call->name, wait_calls[i].name)) vec_push(list.labels, wait_calls[i].form);
        }
        break;
    case CALL_CLIPBOARD:
        vec_push(list.labels, CLIPBOARD_COPY_FORM);
        break;
    case CALL_LIST: {
        const method_form *form = find_form(list_calls, sizeof list_calls / sizeof list_calls[0], call->name);
        sb label = {0};
        if (form) format_method(form, call->object->type, &label);
        if (form) vec_push(list.labels, label.data);
        break;
    }
    case CALL_WORLD: {
        const method_form *form = find_form(world_calls, sizeof world_calls / sizeof world_calls[0], call->name);
        sb label = {0};
        if (form) format_world_method(form, call->type_decl, &label);
        if (form) vec_push(list.labels, label.data);
        break;
    }
    default:
        break;
    }
    // The signature with as many parameters as the call has arguments, of
    // their types; or one whose last ones it leaves out, like Session.Open().
    for (int exact = 1; exact >= 0; exact--) {
        for (int i = 0; i < list.labels.count; i++) {
            label_params params;
            parse_label(list.labels.items[i], &params);
            bool fits = exact ? params.count == call->args.count : params.count > call->args.count;
            for (int k = 0; fits && k < call->args.count; k++) fits = param_fits(params.types[k], call, k);
            if (!fits) continue;
            for (; count < params.count && count < max; count++) names[count] = params.names[count];
            return count;
        }
    }
    return 0;
}

// Where the word `name` is in a signature's label: "vert" isn't in "vertices".
static int label_word(const char *label, const char *name)
{
    const size_t n = strlen(name);
    for (const char *p = strstr(label, name); p; p = strstr(p + 1, name)) {
        if (p > label && !isalnum((unsigned char)p[-1]) && !isalnum((unsigned char)p[n])) return (int)(p - label);
    }
    return 0;
}

// The built-in signatures' docs for Draw.Mesh: a 2D mesh's, and a 3D one's.
typedef struct mesh_docs {
    const char *doc[2];
} mesh_docs;

static void collect_mesh_doc(void *user, const char *label, const char *doc)
{
    mesh_docs *docs = user;
    const int form = strstr(label, "transform") ? 1 : 0;
    if (!docs->doc[form]) docs->doc[form] = doc;
}

// Signature help in Draw.Mesh's parentheses, opened at token `open`, with
// `commas` before the cursor: its two forms, the one the mesh's vertices say
// (a 3D mesh's take a transform), and the parameter the argument at the cursor
// is. What follows the lists can be left out, so that goes by what each
// argument is.
static void mesh_signature_help(const int open, const int commas, const loc at, jbuf *out)
{
    static const int forms[3][MESH_PARAMS] = {
        {MESH_VERTICES, MESH_INDICES, MESH_TEXTURE, MESH_FILTER, MESH_WRAP, MESH_VERT, MESH_FRAG, MESH_VALUES},
        {MESH_VERTICES, MESH_INDICES, MESH_TRANSFORM, MESH_TEXTURE, MESH_FILTER, MESH_WRAP, MESH_VERT, MESH_FRAG, MESH_VALUES},
        {MESH_VERTICES, MESH_INDICES, MESH_INSTANCES, MESH_TEXTURE, MESH_FILTER, MESH_WRAP, MESH_VERT, MESH_FRAG, MESH_VALUES},
    };
    const scope sc = scope_at(at);
    call_args args;
    call_arguments(open, &args);
    const str owner = str_from("Draw"); // A target's are Draw's too
    const decl *const drawn = mesh_vertex(&sc, args.first[0], args.last[0]);
    const bool three_d = drawn ? vertex_is_3d(drawn)
                               : args.count > 2 && mesh_argument(&sc, args.first[2], args.last[2]) == MESH_TRANSFORM;
    const bool instanced = str_eq_c(DOC->toks[open - 1].text, "Instanced");
    const int *order = forms[instanced ? 2 : three_d ? 1 : 0];
    const int count = instanced || three_d ? 9 : 8;
    const int fixed = instanced || three_d ? 3 : 2; // Lists, and an ordinary 3D mesh's transform

    int active = 0;
    int next = 0; // The first parameter the next argument can be
    for (int a = 0; a <= commas && a < args.count; a++) {
        active = next < count ? next : count - 1;
        const int kind = a < fixed ? order[active] : mesh_argument(&sc, args.first[a], args.last[a]);
        for (int k = next; k < count; k++) {
            if (order[k] != kind) continue;
            active = k;
            break;
        }
        next = active + 1;
    }

    mesh_docs docs = {{NULL, NULL}};
    builtin_signatures(owner, DOC->toks[open - 1].text, collect_mesh_doc, &docs);
    jb_put(out, "{\"signatures\":[");
    const int form = three_d ? 1 : 0;
    {
        const char *label = instanced ? instanced_labels[form] : mesh_labels[form];
        sb doc = {0};
        const char *description = docs.doc[instanced ? 0 : form];
        sb_printf(&doc, "%s%s%s", description ? description : "", description ? "\n\n" : "", MESH_SHADER_DOC);
        jb_put(out, "{\"label\":");
        jb_string(out, label);
        jb_put(out, ",\"documentation\":{\"kind\":\"markdown\",\"value\":");
        jb_string(out, doc.data);
        jb_put(out, "},\"parameters\":[");
        for (int i = 0; i < count; i++) {
            const int param = order[i];
            const int start = label_word(label, mesh_params[param].name);
            const char *what = mesh_params[param].doc[form] ? mesh_params[param].doc[form] : mesh_params[param].doc[0];
            jb_printf(out, "%s{\"label\":[%d,%d],\"documentation\":{\"kind\":\"markdown\",\"value\":", i ? "," : "", start,
                      start + (int)strlen(mesh_params[param].name));
            jb_string(out, what);
            jb_put(out, "}}");
        }
        jb_put(out, "]}");
    }
    jb_printf(out, "],\"activeSignature\":0,\"activeParameter\":%d}", active);
}

void analysis_signature_help(const int line, const int character, jbuf *out)
{
    const loc at = from_lsp(line, character);
    int last = -1;
    for (int i = 0; i < DOC->tok_count && loc_cmp(DOC->toks[i].at, at) < 0; i++) last = i;

    // The innermost '(' still open at the cursor, counting the commas after it.
    int commas;
    const int open = open_call(last, &commas);
    // world.Get<Health>(: the method's name is before its type in <>
    const int type_open = type_argument_open(open - 1);
    const int callee = type_open >= 0 ? type_open - 1 : open - 1;
    if (callee < 0 || DOC->toks[callee].kind != T_IDENT) {
        jb_put(out, "null");
        return;
    }
    if (opens_mesh_draw(open)) {
        mesh_signature_help(open, commas, at, out);
        return;
    }
    const str name = DOC->toks[callee].text;
    const bool method = callee >= 1 && DOC->toks[callee - 1].kind == T_DOT;
    const str owner = method && callee >= 2 && DOC->toks[callee - 2].kind == T_IDENT ? DOC->toks[callee - 2].text : (str){"", 0};

    jbuf list = {0};
    signature_list s = {&list, commas, -1, commas, 0, {"", 0}, false};
    // The argument the cursor is in may be written with its parameter's name: `crit: `
    int first_of_arg = open + 1;
    for (int i = open + 1, depth = 0; i <= last; i++) {
        const tok_kind k = DOC->toks[i].kind;
        if (k == T_LPAREN || k == T_LBRACKET || k == T_LBRACE) depth++;
        else if (k == T_RPAREN || k == T_RBRACKET || k == T_RBRACE) depth--;
        else if (k == T_COMMA && depth == 0) first_of_arg = i + 1;
    }
    if (first_of_arg + 1 < DOC->tok_count && DOC->toks[first_of_arg].kind == T_IDENT && DOC->toks[first_of_arg + 1].kind == T_COLON) {
        s.named = DOC->toks[first_of_arg].text;
    }
    const occurrence *attribute = occurrence_at(DOC->toks[callee].at);
    if (attribute && attribute->kind == OCC_ATTRIBUTE) {
        if (str_eq_c(name, "Stage")) {
            add_signature(&s, "Stage(Stages.Phase, int offset = 0)",
                          "Phases follow enum declaration order. The signed constant offset orders work within the phase.");
        } else if (str_eq_c(name, "Before") || str_eq_c(name, "After")) {
            sb label = {0};
            sb_printf(&label, STR_FMT "(DeclarationOrPhase, ...)", STR_ARG(name));
            add_signature(&s, label.data, "An enum member names the entire phase, including all its offsets.");
        }
    } else if (owner.len > 0 && builtin_owner(owner)) { // Math.Clamp(, Draw.Circle(, quaternion.Euler(
        if (str_eq_c(owner, "Draw") && str_eq_c(name, "Blit")) add_signature(&s, BLIT_FORM, BLIT_DOC);
        else builtin_signatures(owner, name, visit_signature, &s);
    } else if (str_eq_c(owner, "Scene")) {
        for (size_t i = 0; i < sizeof scene_calls / sizeof scene_calls[0]; i++) {
            if (str_eq_c(name, scene_calls[i].name)) add_signature(&s, scene_calls[i].form, scene_calls[i].doc);
        }
    } else if (str_eq_c(owner, "Session")) {
        for (size_t i = 0; i < sizeof session_calls / sizeof session_calls[0]; i++) {
            if (str_eq_c(name, session_calls[i].name)) add_signature(&s, session_calls[i].form, session_calls[i].doc);
        }
    } else if (str_eq_c(owner, "Wait")) {
        for (size_t i = 0; i < sizeof wait_calls / sizeof wait_calls[0]; i++) {
            if (str_eq_c(name, wait_calls[i].name)) add_signature(&s, wait_calls[i].form, wait_calls[i].doc);
        }
    } else if (str_eq_c(owner, "Clipboard")) {
        if (str_eq_c(name, "Copy")) add_signature(&s, CLIPBOARD_COPY_FORM, CLIPBOARD_COPY_DOC);
    } else if (method) {
        // What it's called on decides: text's, a list's, a grid's, an entity's or a singleton's.
        // The call's own, once it parses; until then, what the names before the dot are.
        const occurrence *called = occurrence_at(DOC->toks[callee].at);
        const type t = called && called->call != CALL_NONE && called->object_type.kind != TY_ERROR
                         ? called->object_type
                         : receiver_type(callee - 1, at);
        size_t count;
        const method_form *forms = methods_of(t, &count);
        const method_form *form = find_form(forms, count, name);
        if (t.kind == TY_STRING) {
            builtin_signatures(str_from("string"), name, visit_signature, &s);
        } else if (t.kind == TY_STRUCT && t.decl == A.prog->random) {
            builtin_signatures(str_from("Random"), name, visit_signature, &s);
        } else if (t.kind == TY_RENDER_TARGET && !form) {
            builtin_signatures(str_from("Draw"), name, visit_signature, &s); // A target has the window's methods
        } else if (form && t.kind == TY_WORLD) {
            sb label = {0};
            format_world_method(form, type_open >= 0 ? component_written(type_open, open - 1) : NULL, &label);
            add_signature(&s, label.data, form->doc);
        } else if (form) {
            sb label = {0};
            format_method(form, t, &label);
            add_signature(&s, label.data, form->doc);
        }
    } else {
        for (size_t i = 0; i < sizeof call_forms / sizeof call_forms[0]; i++) {
            if (!str_eq_c(name, call_forms[i].name)) continue;
            for (int f = 0; f < 4 && call_forms[i].forms[f]; f++) add_signature(&s, call_forms[i].forms[f], call_forms[i].doc);
        }
    }
    if (s.shown == 0) {
        // An enum's name converts an int: the enum the call resolved to, or
        // while it's half written, the one the names before the parenthesis say.
        const occurrence *o = occurrence_at(DOC->toks[callee].at);
        const decl *converted = o && o->kind == OCC_TYPE && o->decl && o->decl->kind == DECL_ENUM ? o->decl : NULL;
        int first = callee;
        while (first >= 2 && DOC->toks[first - 1].kind == T_DOT && DOC->toks[first - 2].kind == T_IDENT) first -= 2;
        const str written = {DOC->toks[first].text.ptr,
                             (int)(DOC->toks[callee].text.ptr + DOC->toks[callee].text.len - DOC->toks[first].text.ptr)};
        for (int i = 0; !converted && i < A.prog->decls.count; i++) {
            const decl *d = A.prog->decls.items[i];
            if (d->kind != DECL_ENUM || d->builtin) continue;
            if (first == callee ? str_eq(d->name, name) && visible_by_name(d) : str_eq(d->qualified, written)) converted = d;
        }
        if (converted) {
            sb label = {0};
            sb doc = {0};
            sb_printf(&label, "%s(int number)", name_for(converted));
            describe_enum_conversion(converted, &doc);
            add_signature(&s, label.data, doc.data);
        }
    }
    if (s.shown == 0) {
        // The game's methods and functions: the one the call resolved to, or
        // while it's half written, every one with that name.
        // Where others share its name, as overloads do, every one of them,
        // with the one the call means as the active one.
        const occurrence *o = occurrence_at(DOC->toks[callee].at);
        const decl *meant = o && is_routine(o->decl) ? o->decl : NULL;
        for (int i = 0; i < A.prog->decls.count; i++) {
            const decl *d = A.prog->decls.items[i];
            for (int k = -1; k < d->methods.count; k++) {
                const decl *m = k < 0 ? d : d->methods.items[k];
                if (!is_routine(m) || m->is_operator || !str_eq(m->name, name)) continue;
                const bool match = meant ? m->owner == meant->owner && str_eq(m->qualified, meant->qualified)
                                         : (m->owner != NULL) == method;
                if (!match) continue;
                sb label = {0};
                format_routine(m, &label);
                s.picked = m == meant;
                add_signature(&s, label.data, NULL);
            }
        }
    }
    if (s.shown == 0) {
        jb_put(out, "null");
    } else {
        jb_put(out, "{\"signatures\":[");
        jb_putn(out, list.data, list.len);
        jb_printf(out, "],\"activeSignature\":%d,\"activeParameter\":%d}", s.active < 0 ? 0 : s.active, s.active_param);
    }
    jb_free(&list);
}

// ---------------------------------------------------------------------------
// Formatting: indentation, spacing, and C#-style braces: those of a block that
// spans lines go on lines of their own. Otherwise tokens and comments stay on
// their lines, in their order, so formatting can't change what the code means.

typedef struct fmt_item {
    const char *text; // Verbatim: a token (strings with their quotes) or a comment
    int len;
    int tok;          // Token index, or -1 for a comment
    bool unary;       // A '-' that negates
} fmt_item;

typedef VEC(fmt_item) fmt_line;

// 1-based line holding `p`.
static int line_of(const char *p)
{
    int lo = 0;
    int hi = DOC->lines.count - 1;
    while (lo < hi) {
        const int mid = (lo + hi + 1) / 2;
        if (DOC->lines.items[mid] <= p) lo = mid;
        else hi = mid - 1;
    }
    return lo + 1;
}

// A line's text, without its line break.
static str line_text(const int line)
{
    const char *start = DOC->lines.items[line - 1];
    const char *end = line < DOC->lines.count ? DOC->lines.items[line] : DOC->src.text + DOC->src.len;
    if (end > start && end[-1] == '\n') end--;
    if (end > start && end[-1] == '\r') end--;
    return (str){start, (int)(end - start)};
}

static const char *token_start(const token *t)
{
    return t->kind == T_STRING ? t->text.ptr - 1 : t->text.ptr;
}

static bool ends_operand(const tok_kind k)
{
    return k == T_IDENT || k == T_INT || k == T_FLOAT || k == T_STRING || k == T_RPAREN || k == T_RBRACKET
        || k == T_TRUE || k == T_FALSE || k == T_THIS || k == T_DEFAULT || k == T_NULL || k == T_INTERP
        || k == T_INTERP_PART;
}

// Whether the token at `i` ends an operand: `Parse(t)!`'s `!` does too.
static bool ends_operand_at(const int i)
{
    const tok_kind k = DOC->toks[i].kind;
    return ends_operand(k) || (k == T_NOT && i > 0 && ends_operand_at(i - 1));
}

// Whether the `!` at token `i` comes after a value, `Parse(t)!`, rather than
// before one.
static bool is_postfix_bang(const int i)
{
    return DOC->toks[i].kind == T_NOT && i > 0 && ends_operand_at(i - 1);
}

// Whether the `?` at token `i` makes a type a T?, as in `int? best = null;`
// or `int? Find(...)`, rather than starting `?:`.
static bool is_type_question(const int i)
{
    if (DOC->toks[i].kind != T_QUESTION || i == 0 || i + 2 >= DOC->tok_count) return false;
    const tok_kind before = DOC->toks[i - 1].kind;
    if ((before != T_IDENT && before != T_GT) || DOC->toks[i + 1].kind != T_IDENT) return false;
    const tok_kind after = DOC->toks[i + 2].kind;
    if (after == T_ASSIGN || after == T_SEMI || after == T_COMMA || after == T_RPAREN) return true;
    if (after != T_LPAREN) return false;
    // A function's declaration: its type starts the line, maybe after `mut`.
    int k = i - 1;
    while (k >= 2 && DOC->toks[k - 1].kind == T_DOT && DOC->toks[k - 2].kind == T_IDENT) k -= 2;
    return k == 0 || DOC->toks[k - 1].at.line != DOC->toks[k].at.line || DOC->toks[k - 1].kind == T_MUT;
}

// Whether the `{` at token `i` opens an enum's members: `enum Name {` or
// `enum Name : byte {`, whose members go one to a line, none continuing another.
static bool opens_enum(const int i)
{
    int k = i - 1;
    if (k >= 2 && DOC->toks[k].kind == T_IDENT && DOC->toks[k - 1].kind == T_COLON) k -= 2;
    return k >= 1 && DOC->toks[k].kind == T_IDENT && DOC->toks[k - 1].kind == T_IDENT && str_eq_c(DOC->toks[k - 1].text, "enum");
}

// Whether the `default` at token `i` is a switch's label rather than a value:
// `default:`, but not `c ? default : x`.
static bool is_default_label(const int i)
{
    return i + 1 < DOC->tok_count && DOC->toks[i + 1].kind == T_COLON && (i == 0 || DOC->toks[i - 1].kind != T_QUESTION);
}

// Whether the ':' at token `i` ends a switch's label, like `case Page.Title:`,
// rather than being part of `?:`.
static bool is_label_colon(const int i)
{
    for (int k = i - 1; k >= 0; k--) {
        const tok_kind kind = DOC->toks[k].kind;
        if (kind == T_CASE || (kind == T_DEFAULT && is_default_label(k))) return true;
        if (kind == T_QUESTION || kind == T_SEMI || kind == T_LBRACE || kind == T_RBRACE || kind == T_COLON) return false;
    }
    return false;
}

// Whether the ':' at token `i` follows an argument's name, `crit: true`: a
// name that starts an argument, which no value of `?:` does.
static bool is_argument_colon(const int i)
{
    return DOC->toks[i].kind == T_COLON && i >= 2 && DOC->toks[i - 1].kind == T_IDENT
        && (DOC->toks[i - 2].kind == T_LPAREN || DOC->toks[i - 2].kind == T_COMMA);
}

// A type written with others in <>, which the formatter keeps tight: List<int>, Map<int, float>.
static bool generic_type(const str name)
{
    return str_eq_c(name, "List") || str_eq_c(name, "Map") || str_eq_c(name, "Set") || str_eq_c(name, "Grid2")
        || str_eq_c(name, "Grid3");
}

static bool space_between(const fmt_item *a, const fmt_item *b)
{
    if (a->tok < 0 || b->tok < 0) return true; // Comments
    const tok_kind x = DOC->toks[a->tok].kind;
    const tok_kind y = DOC->toks[b->tok].kind;
    if (y == T_COLON && is_label_colon(b->tok)) return false;
    if (y == T_COLON && is_argument_colon(b->tok)) return false; // Damage(10, crit: true)
    // $"a {x:F2} b": the values in text sit against its braces
    if (y == T_INTERP_PART || y == T_INTERP_FORMAT) return false;
    if ((x == T_INTERP || x == T_INTERP_PART) && DOC->toks[a->tok].text.ptr[DOC->toks[a->tok].text.len - 1] == '{') return false;
    if (y == T_RPAREN || y == T_RBRACKET || y == T_COMMA || y == T_SEMI || y == T_DOT) return false;
    if (x == T_LPAREN || x == T_LBRACKET || x == T_DOT) return false;
    // Parse(t)! and int?: against what they follow
    if (y == T_NOT && is_postfix_bang(b->tok)) return false;
    if (y == T_QUESTION && is_type_question(b->tok)) return false;
    if (a->unary || (x == T_NOT && !is_postfix_bang(a->tok)) || x == T_TILDE) return false;
    // world.Get<Health>(e): a method's type, not a comparison
    if (y == T_LT && type_argument_close(b->tok) >= 0) return false;
    if (x == T_LT && type_argument_close(a->tok) >= 0) return false;
    if (y == T_GT && type_argument_open(b->tok) >= 0) return false;
    if (x == T_GT && y == T_LPAREN && type_argument_open(a->tok) >= 0) return false;
    // List<Item>: a type, not a comparison
    if (x == T_IDENT && y == T_LT && generic_type(DOC->toks[a->tok].text)) return false;
    if (x == T_LT && a->tok > 0 && generic_type(DOC->toks[a->tok - 1].text)) return false;
    if (y == T_GT) { // ...past a map's key and the comma after it
        int k = b->tok - 1;
        while (k > 0 && (DOC->toks[k].kind == T_IDENT || DOC->toks[k].kind == T_DOT || DOC->toks[k].kind == T_COMMA)) k--;
        if (k > 0 && DOC->toks[k].kind == T_LT && generic_type(DOC->toks[k - 1].text)) return false;
    }
    // i++ and ++i: against what they change
    if ((y == T_PLUS_PLUS || y == T_MINUS_MINUS) && ends_operand(x)) return false;
    if ((x == T_PLUS_PLUS || x == T_MINUS_MINUS) && !(a->tok > 0 && ends_operand(DOC->toks[a->tok - 1].kind))) return false;
    // `operator +(`: the symbol names the operator, like a method's name.
    const bool operator_name = a->tok > 0 && DOC->toks[a->tok - 1].kind == T_IDENT
                            && str_eq_c(DOC->toks[a->tok - 1].text, "operator");
    if (y == T_LPAREN && operator_name) return false;
    if (y == T_LPAREN && DOC->query_keywords[a->tok]) return true;
    if (y == T_LPAREN || y == T_LBRACKET) return x != T_IDENT; // Calls, but `if (` and `* (`
    return true;
}

// Whether the `{` at token `i` starts a literal, like Body { position = p } or
// Combat.Health { amount = 1 }: it follows a name where an expression goes.
// Declarations have a word before their name (component Body {, input Keys {).
static bool opens_literal(const int i)
{
    int k = i - 1;
    if (k < 0 || DOC->toks[k].kind != T_IDENT || opens_enum(i)) return false; // `enum Voxel : byte {` isn't one
    while (k >= 2 && DOC->toks[k - 1].kind == T_DOT && DOC->toks[k - 2].kind == T_IDENT) k -= 2;
    if (k == 0) return false;
    const tok_kind before = DOC->toks[k - 1].kind;
    return before != T_IDENT && before != T_COMPONENT && before != T_SINGLETON && before != T_SEMI
        && before != T_LBRACE && before != T_RBRACE && before != T_RBRACKET;
}

// Whether the ')' at token `i` ends the header of an if or a loop, whose
// body can be a statement without braces on the next line.
static bool closes_header(const int i)
{
    if (DOC->toks[i].kind != T_RPAREN) return false;
    int parens = 0;
    for (int k = i; k > 0; k--) {
        if (DOC->toks[k].kind == T_RPAREN) {
            parens++;
        } else if (DOC->toks[k].kind == T_LPAREN && --parens == 0) {
            const tok_kind keyword = DOC->toks[k - 1].kind;
            return keyword == T_IF || keyword == T_WHILE || keyword == T_FOR || keyword == T_FOREACH || keyword == T_PARALLEL;
        }
    }
    return false;
}

// How many levels deeper than its braces each token goes for the switch
// sections around it: statements under `case X:` go one level in, the labels
// stay at the switch's level.
static int *case_levels(void)
{
    int *levels = arena_alloc(sizeof(int) * ((size_t)DOC->tok_count + 1));
    // Per open brace: is it a switch's, and has a label started a section in it?
    bool *is_switch = arena_alloc(sizeof(bool) * ((size_t)DOC->tok_count + 1));
    bool *in_section = arena_alloc(sizeof(bool) * ((size_t)DOC->tok_count + 1));
    int depth = 0;
    int sections = 0; // Open braces whose section is running
    for (int i = 0; i < DOC->tok_count; i++) {
        const tok_kind k = DOC->toks[i].kind;
        const bool label = (k == T_CASE || (k == T_DEFAULT && is_default_label(i))) && depth > 0 && is_switch[depth - 1];
        const bool closing = k == T_RBRACE && depth > 0;
        levels[i] = sections - ((label || closing) && depth > 0 && in_section[depth - 1] ? 1 : 0);
        if (label && !in_section[depth - 1]) {
            in_section[depth - 1] = true;
            sections++;
        }
        if (k == T_LBRACE) {
            // switch (...) {: the ')' before it closes the '(' after `switch`.
            bool after_switch = false;
            if (i > 0 && DOC->toks[i - 1].kind == T_RPAREN) {
                int parens = 0;
                for (int j = i - 1; j >= 0; j--) {
                    if (DOC->toks[j].kind == T_RPAREN) parens++;
                    else if (DOC->toks[j].kind == T_LPAREN && --parens == 0) {
                        after_switch = j > 0 && DOC->toks[j - 1].kind == T_SWITCH;
                        break;
                    }
                }
            }
            is_switch[depth] = after_switch;
            in_section[depth] = false;
            depth++;
        } else if (closing) {
            depth--;
            if (in_section[depth]) sections--;
        }
    }
    return levels;
}

// The case_levels of what's at `text`: the first token that starts there or after.
static int case_level_at(const int *levels, const char *text)
{
    for (int i = 0; i < DOC->tok_count; i++) {
        if (token_start(&DOC->toks[i]) >= text) return levels[i];
    }
    return 0;
}

// BREAK_STACKED: the comma after a call in a list of calls before one block, whose next call starts a line
// as the first did.
enum { BREAK_BEFORE = 1, BREAK_AFTER = 2, BREAK_STACKED = 4 };

// Whether the ',' at token `i` is between calls listed before the block they
// share, `Draw.Blend(Blend.Add), Draw.Clip(panel) { ... }`: it follows a
// call's ')' with nothing open since `brace`, the brace it's in, which is a
// block's, and what it starts ends at a '{' rather than a ';'.
static bool stacks_calls(const int i, const int brace)
{
    if (i == 0 || DOC->toks[i - 1].kind != T_RPAREN || i + 1 >= DOC->tok_count || DOC->toks[i + 1].kind != T_IDENT) return false;
    if (brace < 0 || opens_literal(brace) || opens_enum(brace)) return false;
    int nested = 0;
    for (int k = i + 1; k < DOC->tok_count; k++) {
        const tok_kind kind = DOC->toks[k].kind;
        if (kind == T_LPAREN || kind == T_LBRACKET) nested++;
        else if (kind == T_RPAREN || kind == T_RBRACKET) nested--;
        else if (nested == 0 && kind == T_LBRACE) return DOC->toks[k - 1].kind == T_RPAREN;
        else if (nested == 0 && (kind == T_SEMI || kind == T_RBRACE)) return false;
        if (nested < 0) return false;
    }
    return false;
}

// Where lines break for C#-style braces, per token: around both braces of a
// block that spans lines (or isn't closed yet), and before an `else` that
// follows a block when its own block spans lines. Literals, and blocks on one
// line, stay as they are. Calls listed with commas before one block go one to
// a line.
static unsigned char *brace_breaks(void)
{
    unsigned char *breaks = arena_alloc((size_t)DOC->tok_count + 1);
    int *open = arena_alloc(sizeof(int) * ((size_t)DOC->tok_count + 1));
    int *nested_at = arena_alloc(sizeof(int) * ((size_t)DOC->tok_count + 1)); // What was open when each brace opened
    int depth = 0;
    int nested = 0; // Parentheses and brackets open
    for (int i = 0; i < DOC->tok_count; i++) {
        const tok_kind k = DOC->toks[i].kind;
        if (k == T_LPAREN || k == T_LBRACKET) nested++;
        if ((k == T_RPAREN || k == T_RBRACKET) && nested > 0) nested--;
        if (k == T_COMMA && depth > 0 && nested == nested_at[depth - 1] && stacks_calls(i, open[depth - 1])) {
            breaks[i] |= BREAK_AFTER | BREAK_STACKED;
        }
        if (k == T_LBRACE) {
            nested_at[depth] = nested;
            open[depth++] = i;
        } else if (k == T_RBRACE && depth > 0) {
            const int o = open[--depth];
            if (opens_literal(o) || DOC->toks[o].at.line == DOC->toks[i].at.line) continue;
            breaks[o] = BREAK_BEFORE | BREAK_AFTER;
            breaks[i] = BREAK_BEFORE | BREAK_AFTER;
        }
    }
    while (depth > 0) {
        const int o = open[--depth];
        if (!opens_literal(o)) breaks[o] = BREAK_BEFORE | BREAK_AFTER;
    }
    for (int i = 1; i + 1 < DOC->tok_count; i++) {
        if (DOC->toks[i].kind == T_ELSE && DOC->toks[i - 1].kind == T_RBRACE && (breaks[i + 1] & BREAK_BEFORE)) {
            breaks[i] |= BREAK_BEFORE;
        }
    }
    return breaks;
}

// The formatter's edits, each a line's (or the final line break): those that
// start on lines `first` to `last` (0-based) are written.
typedef struct line_edits {
    jbuf *out;
    int count;
    int first;
    int last;
} line_edits;

static void write_edit(line_edits *edits, const loc start, const loc end, const char *text, const size_t len)
{
    if (start.line - 1 < edits->first || start.line - 1 > edits->last) return;
    if (edits->count++) jb_put(edits->out, ",");
    jb_put(edits->out, "{\"range\":{\"start\":");
    write_position(edits->out, start);
    jb_put(edits->out, ",\"end\":");
    write_position(edits->out, end);
    jb_put(edits->out, "},\"newText\":");
    jb_string_n(edits->out, text, len);
    jb_put(edits->out, "}");
}

const char *analysis_format(int tab_size, const bool insert_spaces, const int first_line, const int last_line, jbuf *out)
{
    if (DOC->lex_errors > 0) return "The file has text Tide can't read. Fix that first.";
    if (tab_size <= 0) tab_size = 4;
    // Each line's indentation depends on the lines before it, so the whole
    // document is formatted, and only the range's lines are changed.
    line_edits edits = {out, 0, first_line, last_line};

    const bool final_newline = DOC->src.len > 0 && DOC->src.text[DOC->src.len - 1] == '\n';
    const int line_count = DOC->lines.count - (final_newline ? 1 : 0); // The empty "line" after a final \n isn't one
    fmt_line *lines = arena_alloc(sizeof(fmt_line) * (size_t)(DOC->lines.count + 1));
    bool *verbatim = arena_alloc(sizeof(bool) * (size_t)(DOC->lines.count + 2));

    // Tokens and comments, line by line, in text order. Directives and the
    // lines they leave out stay as they are.
    for (int l = 1; l <= DOC->lines.count; l++) verbatim[l] = DOC->inactive[l] != 0;
    const char *p = DOC->lines.items[0];
    for (int i = 0; i <= DOC->tok_count; i++) {
        const token *t = &DOC->toks[i];
        const char *next = i < DOC->tok_count ? token_start(t) : DOC->src.text + DOC->src.len;
        while (p < next) {
            const char *code = past_inactive(p, next);
            if (code != p) {
                p = code;
                continue;
            }
            if (p + 1 < next && p[0] == '/' && (p[1] == '/' || p[1] == '*')) {
                const char *start = p;
                const int first_line = line_of(start);
                if (p[1] == '/') {
                    while (p < next && *p != '\n') p++;
                } else {
                    p += 2;
                    while (p + 1 < next && !(p[0] == '*' && p[1] == '/')) p++;
                    p += 2;
                }
                const int last_line = line_of(p - 1);
                // A comment over several lines keeps its first line's part here;
                // its other lines stay exactly as they are.
                const str first = line_text(first_line);
                const char *end = last_line == first_line ? p : first.ptr + first.len;
                while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) end--;
                const fmt_item item = {start, (int)(end - start), -1, false};
                vec_push(lines[first_line], item);
                for (int l = first_line + 1; l <= last_line; l++) verbatim[l] = true;
            } else {
                p++;
            }
        }
        if (i == DOC->tok_count) break;
        const bool unary = t->kind == T_MINUS && (i == 0 || !ends_operand_at(i - 1));
        const fmt_item item = {token_start(t), token_len(t), i, unary};
        vec_push(lines[t->at.line], item);
        p = next + token_len(t);
    }

    int last_content = 0;
    for (int l = 1; l <= line_count; l++) {
        if (lines[l].count > 0 || verbatim[l]) last_content = l;
    }
    const unsigned char *breaks = brace_breaks();
    const int *case_level = case_levels();
    const char *first_break = memchr(DOC->src.text, '\n', DOC->src.len);
    const char *newline = first_break && first_break > DOC->src.text && first_break[-1] == '\r' ? "\r\n" : "\n";

    jb_put(out, "[");
    int depth = 0;
    int parens = 0;
    // `parens` at each brace depth: inside a literal in a call, like
    // Spawn(Body {, only parentheses opened since the brace continue a line.
    int *parens_at = arena_alloc(sizeof(int) * ((size_t)DOC->tok_count + 1));
    bool *enum_at = arena_alloc(sizeof(bool) * ((size_t)DOC->tok_count + 1)); // Each brace depth: an enum's members
    enum_at[0] = false;
    int pending = 0; // Extra indents for the statement after `if (...)` or `else` without braces
    bool blank_before = true; // Drops blank lines at the start of the file
    // The last line with code: did it end a statement or block, or open a braceless if?
    bool statement_open = false;
    bool opened_pending = false;
    bool ended_comma = false; // ...or ended with a comma, which in an enum ends a member
    for (int l = 1; l <= line_count; l++) {
        const str original = line_text(l);
        const fmt_line *items = &lines[l];
        const loc start = {l, 1, A.doc};
        const loc end = {l, original.len + 1, A.doc};

        if (items->count == 0 && !verbatim[l]) {
            // Blank: at most one in a row, none at the start or the end.
            const bool drop = blank_before || l > last_content;
            if (drop && l < DOC->lines.count) {
                write_edit(&edits, start, (loc){l + 1, 1, A.doc}, "", 0);
            } else if (original.len > 0) {
                write_edit(&edits, start, end, "", 0);
            }
            blank_before = true;
            continue;
        }
        blank_before = false;

        // The line's pieces: it breaks before a token that must start a line,
        // and after one that must end it (comments stay behind it).
        int *starts = arena_alloc(sizeof(int) * ((size_t)items->count + 1));
        int pieces = 1;
        if (!verbatim[l]) {
            int prev_tok = items->items[0].tok;
            for (int k = 1; k < items->count; k++) {
                const int tok = items->items[k].tok;
                if (tok < 0) continue;
                if ((breaks[tok] & BREAK_BEFORE) || (prev_tok >= 0 && (breaks[prev_tok] & BREAK_AFTER))) starts[pieces++] = k;
                prev_tok = tok;
            }
        }
        starts[pieces] = items->count;

        sb text = {0};
        for (int piece = 0; piece < pieces; piece++) {
            // A line with no items has no array of them to point into
            const fmt_item *from = items->items ? &items->items[starts[piece]] : NULL;
            const fmt_item *to = items->items ? &items->items[starts[piece + 1]] : NULL;

            int last_tok = -1;
            for (const fmt_item *item = from; item < to; item++) {
                if (item->tok >= 0) last_tok = item->tok;
            }

            if (!verbatim[l]) {
                if (piece > 0) sb_put(&text, newline);
                const tok_kind first = from->tok >= 0 ? DOC->toks[from->tok].kind : T_EOF;
                int level = depth;
                if (first == T_RBRACE) level--;
                if (first != T_LBRACE) level += pending;
                level += from->tok >= 0 ? case_level[from->tok] : case_level_at(case_level, from->text);
                if (level < 0) level = 0;

                // A line that continues an expression or an argument list goes one
                // level deeper, or keeps its own indentation if that's deeper
                // still: code aligned under an opening parenthesis stays aligned.
                const bool continuation = first != T_LBRACE && first != T_RBRACE && (depth > 0 || parens > 0)
                                       && ((parens > parens_at[depth] && first != T_RPAREN)
                                           || (statement_open && !opened_pending && !(enum_at[depth] && ended_comma)));
                int own_width = 0;
                if (piece == 0) {
                    for (const char *c = original.ptr; c < from->text; c++) own_width += *c == '\t' ? tab_size : 1;
                }
                if (continuation && own_width > (level + 1) * tab_size) {
                    sb_putn(&text, original.ptr, (size_t)(from->text - original.ptr));
                } else {
                    if (continuation) level++;
                    for (int k = 0; k < level; k++) {
                        if (insert_spaces) sb_printf(&text, "%*s", tab_size, "");
                        else sb_put(&text, "\t");
                    }
                }
                for (const fmt_item *item = from; item < to; item++) {
                    if (item > from && item->tok < 0) {
                        // Before a trailing comment, keep the author's spacing (often alignment).
                        const char *gap = item[-1].text + item[-1].len;
                        if (item->text > gap) sb_putn(&text, gap, (size_t)(item->text - gap));
                        else sb_put(&text, " ");
                    } else if (item > from && space_between(item - 1, item)) {
                        sb_put(&text, " ");
                    }
                    sb_putn(&text, item->text, (size_t)item->len);
                }
            }

            for (const fmt_item *item = from; item < to; item++) {
                if (item->tok < 0) continue;
                switch (DOC->toks[item->tok].kind) {
                case T_LBRACE:
                    parens_at[++depth] = parens;
                    enum_at[depth] = opens_enum(item->tok);
                    break;
                case T_RBRACE: if (depth > 0) depth--; break;
                case T_LPAREN: parens++; break;
                case T_RPAREN: if (parens > 0) parens--; break;
                default: break;
                }
            }
            if (last_tok >= 0) {
                const tok_kind k = DOC->toks[last_tok].kind;
                opened_pending = false;
                const bool label = k == T_COLON && is_label_colon(last_tok);
                if (k == T_SEMI || k == T_LBRACE || k == T_RBRACE || label) {
                    pending = 0;
                } else if ((k == T_RPAREN && parens == 0 && closes_header(last_tok)) || k == T_ELSE) {
                    pending++;
                    opened_pending = true;
                }
                // The next call in a list before a block starts a line as the first did
                const bool stacked = k == T_COMMA && (breaks[last_tok] & BREAK_STACKED);
                statement_open = k != T_SEMI && k != T_LBRACE && k != T_RBRACE && !label && !stacked;
                ended_comma = k == T_COMMA;
            }
        }
        if (!verbatim[l] && (text.len != (size_t)original.len || memcmp(text.data, original.ptr, text.len) != 0)) {
            write_edit(&edits, start, end, text.data, text.len);
        }
    }

    // End with exactly one line break.
    if (!final_newline && last_content > 0 && last_content == line_count) {
        const loc end = {line_count, line_text(line_count).len + 1, A.doc};
        write_edit(&edits, end, end, newline, strlen(newline));
    }
    jb_put(out, "]");
    return NULL;
}

// ---------------------------------------------------------------------------
// Assets: the files `asset` declarations name (docs/spec.md, Assets)
//
// A site is an `asset` declaration whose path is written as text there: where
// it's written, resolved from its file. The checker reads the files and says
// what's wrong at the path (asset_files.h); this adds what an editor does with
// that: the fixes, the completion, and the rewriting when a file is renamed.

void analysis_set_game_folders(const char *game_folder, const char *const *roots, const int root_count)
{
    free(asset_roots.game_folder);
    for (int i = 0; i < asset_roots.root_count; i++) free(asset_roots.roots[i]);
    free(asset_roots.roots);
    memset(&asset_roots, 0, sizeof asset_roots);
    if (game_folder) {
        asset_roots.game_folder = malloc(strlen(game_folder) + 1);
        if (!asset_roots.game_folder) abort();
        strcpy(asset_roots.game_folder, game_folder);
        const size_t n = strlen(asset_roots.game_folder); // Without its '/', as tide names it
        if (n > 1 && asset_roots.game_folder[n - 1] == '/') asset_roots.game_folder[n - 1] = '\0';
    }
    if (root_count > 0) {
        asset_roots.roots = calloc((size_t)root_count, sizeof(char *));
        if (!asset_roots.roots) abort();
        for (int i = 0; i < root_count; i++) {
            asset_roots.roots[i] = malloc(strlen(roots[i]) + 1);
            if (!asset_roots.roots[i]) abort();
            strcpy(asset_roots.roots[i], roots[i]);
        }
        asset_roots.root_count = root_count;
    }
}

// The hash of each file hashed so far, by its size and when it was written,
// so an analysis reads no file again that hasn't changed (malloc'd, kept
// across analyses).
typedef struct hashed_file {
    char *path;
    unsigned long long size;
    unsigned long long modified;
    char hex[65];
} hashed_file;

static struct {
    hashed_file *items;
    int count;
    int cap;
} hashed;

static char *arena_copy(const char *text)
{
    return str_to_cstr(str_from(text));
}

// The SHA-256 of the file at `path`, as hex. False when it can't be read.
static bool read_hash(const char *path, const unsigned long long size, char hex[65])
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    char *data = malloc(size > 0 ? (size_t)size : 1);
    if (!data) abort();
    const size_t n = fread(data, 1, (size_t)size, f);
    fclose(f);
    asset_hash_of(data, n, hex);
    free(data);
    return true;
}

// The SHA-256 of the file at `path`, as hex: the one hashed before, unless
// its size or time changed. False when it can't be read.
static bool file_hash(const char *path, const unsigned long long size, const unsigned long long modified, char hex[65])
{
    hashed_file *known = NULL;
    for (int i = 0; i < hashed.count && !known; i++) {
        if (asset_path_same(hashed.items[i].path, path)) known = &hashed.items[i];
    }
    if (known && known->size == size && known->modified == modified) {
        memcpy(hex, known->hex, 65);
        return true;
    }
    if (!read_hash(path, size, hex)) return false;
    if (!known) {
        if (hashed.count == hashed.cap) {
            hashed.cap = hashed.cap ? hashed.cap * 2 : 16;
            hashed.items = realloc(hashed.items, sizeof(hashed_file) * (size_t)hashed.cap);
            if (!hashed.items) abort();
        }
        known = &hashed.items[hashed.count++];
        known->path = malloc(strlen(path) + 1);
        if (!known->path) abort();
        strcpy(known->path, path);
    }
    known->size = size;
    known->modified = modified;
    memcpy(known->hex, hex, 65);
    return true;
}

static void collect_assets(void)
{
    for (int i = 0; i < A.prog->decls.count; i++) {
        const decl *d = A.prog->decls.items[i];
        if (d->kind != DECL_ASSET || !d->value || d->value->kind != E_STRING || !d->unit) continue; // A `const string` path has no site
        int f = -1;
        for (int k = 0; k < A.file_count && f < 0; k++) {
            if (&A.files[k].src == d->unit->src) f = k;
        }
        const int tok = f >= 0 ? token_at((loc){d->value->at.line, d->value->at.col, f}) : -1;
        if (tok < 0 || A.files[f].toks[tok].kind != T_STRING) continue;
        asset_site site = {0};
        site.file = f;
        site.string = tok;
        site.path = A.files[f].toks[tok].text;
        site.many = d->asset_many || d->asset_kind == ASSET_CUBEMAP; // A cube's path is a glob too: its six faces
        site.format = d->asset_kind == ASSET_GRID || d->asset_kind == ASSET_TEXTURE || d->asset_kind == ASSET_CUBEMAP ? ".png"
                    : d->asset_kind == ASSET_CLIP ? ".wav" : NULL;
        site.glob = asset_path_is_glob(site.path.ptr, (size_t)site.path.len);
        site.url = asset_path_is_url(site.path.ptr, (size_t)site.path.len);
        char *folder = asset_path_folder(A.files[f].src.path);
        site.folder = arena_copy(folder);
        if (!site.url) {
            char *abs = asset_path_join(folder, site.path.ptr, (size_t)site.path.len);
            site.abs = arena_copy(abs);
            free(abs);
        }
        free(folder);
        vec_push(A.assets, site);
    }
}

// The site whose path is token `tok` of the document, or NULL.
static const asset_site *asset_site_at(const int tok)
{
    for (int i = 0; i < A.assets.count; i++) {
        if (A.assets.items[i].file == A.doc && A.assets.items[i].string == tok) return &A.assets.items[i];
    }
    return NULL;
}

static void add_asset_file(void *user, const char *path, const unsigned long long size)
{
    (void)user;
    const asset_file file = {arena_copy(path), size};
    vec_push(A.asset_files, file);
}

static int asset_file_order(const void *a, const void *b)
{
    return path_compare(((const asset_file *)a)->path, ((const asset_file *)b)->path);
}

// Every file of the game's folder and its packages', once per analysis, for
// finding a missing one: by the roots the server gave, or else the folders of
// the analysed files.
static void list_asset_files(void)
{
    if (A.asset_files_listed) return;
    A.asset_files_listed = true;
    if (asset_roots.root_count > 0) {
        for (int i = 0; i < asset_roots.root_count; i++) folder_find_files(asset_roots.roots[i], add_asset_file, NULL);
    } else {
        VEC(const char *) folders = {0};
        for (int f = 0; f < A.file_count; f++) {
            char *folder = asset_path_folder(A.files[f].src.path);
            bool seen = false;
            for (int k = 0; k < folders.count && !seen; k++) seen = asset_path_same(folders.items[k], folder);
            if (!seen) {
                vec_push(folders, arena_copy(folder));
                folder_find_files(folder, add_asset_file, NULL);
            }
            free(folder);
        }
    }
    if (A.asset_files.count > 0) {
        qsort(A.asset_files.items, (size_t)A.asset_files.count, sizeof(asset_file), asset_file_order);
    }
}

typedef VEC(const char *) asset_paths;

// A diagnostic at a site's path.
// The checker reports what's wrong with a site's path (asset_files.c), at
// the path's token: the editor's fixes attach to that diagnostic. Where the
// checker said nothing (its own check didn't get that far), one is made.
static int asset_diagnostic(const asset_site *site, const char *message)
{
    const loc at = A.files[site->file].toks[site->string].at;
    for (int i = 0; i < A.diags.count; i++) {
        const diagnostic *d = &A.diags.items[i];
        if (d->at.line == at.line && d->at.col == at.col && d->at.file == at.file && d->severity == DIAG_ERROR) return i;
    }
    diagnostic d = {DIAG_ERROR, at, {0}, {0}, {0, 0, 0}, 0, {NULL, 0}};
    sb_put(&d.message, message);
    vec_push(A.diags, d);
    return A.diags.count - 1;
}

// Nothing at a site's path: where a file with the same bytes is now, with a
// fix that writes its path, or else the files of the same name, a fix each.
static void asset_missing(const asset_site *site, const asset_hashes *known)
{
    sb message = {0};
    sb_printf(&message, "there's no file at '" STR_FMT "' (%s)", STR_ARG(site->path), site->abs);
    const int diag = asset_diagnostic(site, message.data);
    char *from_game = asset_roots.game_folder ? asset_path_relative(asset_roots.game_folder, site->abs) : NULL;
    const asset_hash *hash = from_game ? asset_hashes_find(known, from_game) : NULL;
    free(from_game);
    list_asset_files();
    asset_paths found = {0};
    bool by_bytes = false;
    if (hash) {
        for (int i = 0; i < A.asset_files.count; i++) {
            const asset_file *file = &A.asset_files.items[i];
            unsigned long long modified = 0;
            char hex[65];
            if (file->size != hash->size || !folder_file_info(file->path, NULL, &modified)) continue;
            if (file_hash(file->path, file->size, modified, hex) && strcmp(hex, hash->hex) == 0) vec_push(found, file->path);
        }
        by_bytes = found.count > 0;
    }
    if (found.count == 0) {
        const char *name = asset_path_name(site->abs);
        for (int i = 0; i < A.asset_files.count; i++) {
            if (asset_path_same(asset_path_name(A.asset_files.items[i].path), name)) vec_push(found, A.asset_files.items[i].path);
        }
    }
    if (found.count == 0) return;
    diagnostic *d = &A.diags.items[diag];
    const bool said = strstr(d->message.data, "note:") != NULL; // The checker's note names the files already
    if (!said) sb_put(&d->message, by_bytes ? "\nnote: a file with the same bytes is at " : "\nnote: files of that name: ");
    for (int i = 0; i < found.count; i++) {
        char *relative = asset_path_relative(site->folder, found.items[i]);
        if (!said) sb_printf(&d->message, "%s'%s'", i > 0 ? ", " : "", relative);
        const asset_fix fix = {(int)(site - A.assets.items), diag, arena_copy(relative), by_bytes && i == 0};
        vec_push(A.asset_fixes, fix);
        free(relative);
        if (by_bytes) break; // One place: the first
    }
}

// What there is to offer at each site whose file is missing. The checker
// said so already, and wrote the hashes of the files that are there into the
// game's .tide/ (asset_files_save_hashes), keeping what it knew of the
// missing ones: that's what finds them by their bytes.
static void check_assets(void)
{
    asset_hashes known = {0};
    if (asset_roots.game_folder) asset_hashes_load(&known, asset_roots.game_folder);
    for (int i = 0; i < A.assets.count; i++) {
        const asset_site *site = &A.assets.items[i];
        if (site->url || site->glob) continue;
        if (!folder_file_info(site->abs, NULL, NULL)) asset_missing(site, &known);
    }
    asset_hashes_free(&known);
}

// Where a site's path is written, between the quotes.
static loc asset_path_start(const asset_site *site)
{
    const loc at = A.files[site->file].toks[site->string].at;
    return (loc){at.line, at.col + 1, at.file};
}

static void asset_fix_actions(const int start_line, const int end_line, jbuf *out, int *written)
{
    for (int i = 0; i < A.asset_fixes.count; i++) {
        const asset_fix *f = &A.asset_fixes.items[i];
        const asset_site *site = &A.assets.items[f->site];
        if (site->file != A.doc) continue;
        const loc start = asset_path_start(site);
        if (start.line - 1 < start_line || start.line - 1 > end_line) continue;
        const loc end = {start.line, start.col + site->path.len, start.file};
        sb title = {0};
        sb_printf(&title, "Change to '%s'", f->path);
        quick_fix(out, written, title.data, f->preferred, &A.diags.items[f->diag], start, end, f->path);
    }
}

int analysis_asset_count(void)
{
    return A.assets.count;
}

const char *analysis_asset_path(const int i)
{
    return A.assets.items[i].abs;
}

bool analysis_asset_is_glob(const int i)
{
    return A.assets.items[i].glob;
}

const char *analysis_asset_folder(const int i)
{
    return A.assets.items[i].folder;
}

// Where `path` is once the renames happen: malloc'd, or NULL where it stays.
static char *moved_to(const char *path, const asset_rename *renames, const int count)
{
    for (int i = 0; i < count; i++) {
        const asset_rename *r = &renames[i];
        if (asset_path_same(path, r->old_path)) {
            char *out = malloc(strlen(r->new_path) + 1);
            if (!out) abort();
            strcpy(out, r->new_path);
            return out;
        }
        if (asset_path_under(path, r->old_path)) {
            const char *rest = path + strlen(r->old_path);
            char *out = malloc(strlen(r->new_path) + strlen(rest) + 1);
            if (!out) abort();
            sprintf(out, "%s%s", r->new_path, rest);
            return out;
        }
    }
    return NULL;
}

void analysis_rename_files(const asset_rename *renames, const int count, jbuf *out)
{
    jb_put(out, "{\"changes\":{");
    int files_written = 0;
    for (int file = 0; file < A.file_count; file++) {
        int written = 0;
        for (int i = 0; i < A.assets.count; i++) {
            const asset_site *site = &A.assets.items[i];
            if (site->file != file || site->glob || site->url) continue;
            char *target = moved_to(site->abs, renames, count);
            char *declaring = moved_to(A.files[file].src.path, renames, count);
            if (!target && !declaring) continue;
            char *folder = declaring ? asset_path_folder(declaring) : NULL;
            char *relative = asset_path_relative(folder ? folder : site->folder, target ? target : site->abs);
            const char *was = str_to_cstr(site->path);
            if (!asset_path_same(relative, was)) {
                if (written++ == 0) {
                    if (files_written++) jb_put(out, ",");
                    jb_string(out, A.files[file].uri);
                    jb_put(out, ":[");
                } else {
                    jb_put(out, ",");
                }
                const loc start = asset_path_start(site);
                jb_put(out, "{\"range\":");
                write_edit_range(out, start, (loc){start.line, start.col + site->path.len, start.file});
                jb_put(out, ",\"newText\":");
                jb_string(out, relative);
                jb_put(out, "}");
            }
            free(relative);
            free(folder);
            free(declaring);
            free(target);
        }
        if (written) jb_put(out, "]");
    }
    jb_put(out, "}}");
}

// Completion inside a site's path: the folders and files where the path so
// far leads, relative to the declaring file, the files of the type's format,
// and for a list or map, globs.
typedef struct asset_entries {
    VEC(const char *) folders;
    VEC(const char *) files;
} asset_entries;

static void add_asset_entry(void *user, const char *name, const bool is_folder)
{
    asset_entries *entries = user;
    if (is_folder) vec_push(entries->folders, arena_copy(name));
    else vec_push(entries->files, arena_copy(name));
}

static int name_order(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

// A completion item that replaces `start` to `end` with `text`.
static void asset_item(completion *c, const char *label, const int kind, const loc start, const loc end, const char *text,
                       const char *detail, const char sort)
{
    if (c->count++) jb_put(c->out, ",");
    jb_put(c->out, "{\"label\":");
    jb_string(c->out, label);
    jb_printf(c->out, ",\"kind\":%d,\"filterText\":", kind);
    jb_string(c->out, text);
    jb_printf(c->out, ",\"sortText\":\"%c", sort);
    jb_put(c->out, label);
    jb_put(c->out, "\",\"textEdit\":{\"range\":");
    write_edit_range(c->out, start, end);
    jb_put(c->out, ",\"newText\":");
    jb_string(c->out, text);
    jb_put(c->out, "}");
    if (detail) {
        jb_put(c->out, ",\"detail\":");
        jb_string(c->out, detail);
    }
    jb_put(c->out, "}");
}

static bool ends_with_fold(const char *name, const char *suffix)
{
    const size_t n = strlen(name);
    const size_t s = strlen(suffix);
    if (n < s) return false;
    for (size_t i = 0; i < s; i++) {
        if (tolower((unsigned char)name[n - s + i]) != tolower((unsigned char)suffix[i])) return false;
    }
    return true;
}

static void complete_asset_path(completion *c, const int tok, const loc at)
{
    enum { KIND_FILE = 17, KIND_FOLDER = 19 };
    const asset_site *site = asset_site_at(tok);
    if (!site || site->url) return;
    const token *t = &DOC->toks[tok];
    int offset = at.col - (t->at.col + 1);
    if (offset < 0) offset = 0;
    if (offset > site->path.len) offset = site->path.len;
    int segment = 0; // Where the part being typed starts
    for (int i = 0; i < offset; i++) {
        if (site->path.ptr[i] == '/' || site->path.ptr[i] == '\\') segment = i + 1;
    }
    char *base = asset_path_join(site->folder, site->path.ptr, (size_t)segment);
    const size_t n = strlen(base);
    if (n == 0 || base[n - 1] != '/') {
        base = realloc(base, n + 2);
        if (!base) abort();
        base[n] = '/';
        base[n + 1] = '\0';
    }
    const loc start = {at.line, t->at.col + 1 + segment, A.doc};
    asset_entries entries = {{0}, {0}};
    folder_list(base, add_asset_entry, &entries);
    if (entries.folders.count > 0) qsort(entries.folders.items, (size_t)entries.folders.count, sizeof(char *), name_order);
    if (entries.files.count > 0) qsort(entries.files.items, (size_t)entries.files.count, sizeof(char *), name_order);
    // Up a folder, while there's one above
    size_t top = 0;
    while (top < strlen(base) && !(base[top] == '/' && (top == 0 || base[top - 1] == ':'))) top++;
    if (strlen(base) > top + 1) asset_item(c, "../", KIND_FOLDER, start, at, "../", "The folder above", '0');
    for (int i = 0; i < entries.folders.count; i++) {
        sb label = {0};
        sb_printf(&label, "%s/", entries.folders.items[i]);
        if (strcmp(entries.folders.items[i], "build") == 0 && asset_roots.game_folder
            && !asset_path_under(base, asset_roots.game_folder) && asset_path_under(label.data, asset_roots.game_folder)) {
            continue; // What tide makes, at the game's root
        }
        asset_item(c, label.data, KIND_FOLDER, start, at, label.data, "A folder", '1');
    }
    if (site->many) {
        sb any = {0};
        sb all = {0};
        sb_printf(&any, "*%s", site->format ? site->format : "");
        sb_printf(&all, "**/*%s", site->format ? site->format : "");
        asset_item(c, any.data, KIND_FILE, start, at, any.data, "Every matching file in this folder", '2');
        asset_item(c, all.data, KIND_FILE, start, at, all.data, "Every matching file in this folder and its subfolders", '2');
    }
    for (int i = 0; i < entries.files.count; i++) {
        const char *name = entries.files.items[i];
        if (ends_with_fold(name, ".tide")) continue;
        if (site->format && !ends_with_fold(name, site->format)) continue;
        asset_item(c, name, KIND_FILE, start, at, name, NULL, '3');
    }
    free(base);
}
