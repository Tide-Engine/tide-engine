#if !defined(_WIN32) && !defined(__APPLE__)
#define _DEFAULT_SOURCE // getcwd
#endif
#include "tweaks.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#include "builtins.h"
#include "folders.h"
#include "lexer.h"
#include "tide/math.h"

// .tweaks files: `Name = value;` entries, beside the game's code, that give its tweaks other values than the code
// does. A value is a constant expression, as a `const` has, which can read other tweaks; it's checked as the
// tweak's own value would be (the tweak's constant has it in place of the code's, which decl.code_value keeps).

// `path` from the root: a dev build reads the file again and writes it from wherever it runs
const char *tweaks_absolute(const char *path)
{
#ifdef _WIN32
    char *full = _fullpath(NULL, path, 0);
    if (!full) return path;
    char *copy = arena_alloc(strlen(full) + 1);
    strcpy(copy, full);
    free(full);
    for (char *ch = copy; *ch; ch++) {
        if (*ch == '\\') *ch = '/';
    }
    return copy;
#else
    if (path[0] == '/') return path;
    char here[4096];
    if (!getcwd(here, sizeof here)) return path;
    sb full = {0};
    sb_printf(&full, "%s/%s", here, path);
    return full.data;
#endif
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
void tweak_type_text(sb *o, const decl *d)
{
    const str t = d->return_type_name;
    bool dotted = false;
    for (int i = 0; i < t.len; i++) dotted |= t.ptr[i] == '.';
    const unit *home = d->tweak_owner ? d->tweak_owner->unit : d->unit; // A tweak field's types are its type's namespace's
    if (!builtin_type(t) && !dotted && home && home->ns.len) sb_printf(o, STR_FMT ".", STR_ARG(home->ns));
    sb_printf(o, STR_FMT, STR_ARG(t));
}

// A tweak's qualified name, with `dot` between its parts: "Combat.Pace", or "Combat_Pace"
void tweak_name_text(sb *o, const decl *d, const char dot)
{
    if (d->unit && d->unit->ns.len) {
        for (int i = 0; i < d->unit->ns.len; i++) sb_printf(o, "%c", d->unit->ns.ptr[i] == '.' ? dot : d->unit->ns.ptr[i]);
        sb_printf(o, "%c", dot);
    }
    sb_printf(o, STR_FMT, STR_ARG(d->name));
}

static decl *tweak_named(const program *prog, const str name)
{
    for (int i = 0; i < prog->decls.count; i++) {
        decl *d = prog->decls.items[i];
        if (!d->is_tweak) continue;
        sb q = {0};
        tweak_name_text(&q, d, '.');
        if (str_eq_c(name, q.data)) return d;
    }
    return NULL;
}

typedef struct found {
    VEC(const char *) paths;
} found;

static void found_file(void *user, const char *path)
{
    found *f = user;
    char *copy = arena_alloc(strlen(path) + 1);
    strcpy(copy, path);
    vec_push(f->paths, copy);
}

static int by_path(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

typedef struct listing {
    const char *folder;
    found *files;
} listing;

// One folder's .tweaks files, not its subfolders'
static void listed_file(void *user, const char *name, const bool is_folder)
{
    const listing *l = user;
    const size_t n = strlen(name);
    if (is_folder || n < 8 || strcmp(name + n - 7, ".tweaks") != 0) return;
    sb path = {0};
    sb_printf(&path, "%s%s", l->folder, name);
    vec_push(l->files->paths, path.data);
}

bool tweaks_read(program *prog, const char *const *folders, const int count, const bool deep)
{
    bool any = false;
    for (int i = 0; i < prog->decls.count && !any; i++) any = prog->decls.items[i]->is_tweak;
    if (!any) return true; // A game with no tweaks has no .tweaks of its own
    found files = {0};
    // The language server's own (its open texts among them), or what the folders have
    for (int i = 0; i < prog->given_tweaks.count; i++) vec_push(files.paths, prog->given_tweaks.items[i]->path);
    for (int i = 0; !prog->given_tweaks.count && i < (count ? count : 1); i++) {
        const char *folder = count ? folders[i] : NULL;
        sb root = {0};
        sb_printf(&root, "%s", folder && folder[0] ? folder : ".");
        if (root.data[root.len - 1] != '/' && root.data[root.len - 1] != '\\') sb_put(&root, "/");
        if (deep) folder_find_own(root.data, ".tweaks", found_file, &files);
        else {
            listing l = {root.data, &files};
            folder_list(root.data, listed_file, &l);
        }
    }
    if (files.paths.count > 1) qsort(files.paths.items, (size_t)files.paths.count, sizeof(char *), by_path);
    bool ok = true;
    for (int f = 0; f < files.paths.count; f++) {
        const char *path = files.paths.items[f];
        source *src = NULL;
        for (int k = 0; k < prog->given_tweaks.count && !src; k++) {
            if (prog->given_tweaks.items[k]->path == path) src = prog->given_tweaks.items[k]; // Its file already
        }
        if (!src) {
            FILE *file = fopen(path, "rb");
            if (!file) continue;
            src = NEW(source);
            src->path = path;
            fseek(file, 0, SEEK_END);
            const long size = ftell(file);
            fseek(file, 0, SEEK_SET);
            char *text = arena_alloc((size_t)(size > 0 ? size : 0) + 1);
            src->len = size > 0 ? fread(text, 1, (size_t)size, file) : 0;
            text[src->len] = '\0';
            fclose(file);
            src->text = text;
            diag_add_source(src);
        }
        vec_push(prog->tweak_files, tweaks_absolute(path));
        token *toks = lex(src);
        tweak_entries entries = {0};
        if (!toks || !parse_tweaks_file(prog, toks, &entries)) {
            ok = false;
            continue;
        }
        for (int i = 0; i < entries.count; i++) {
            const tweak_entry *entry = &entries.items[i];
            decl *d = tweak_named(prog, entry->name);
            if (!d) {
                diag_warning(entry->at, "'" STR_FMT "' isn't a tweak of the game's", STR_ARG(entry->name));
                diag_note("a tweak is declared in code, like 'tweak float " STR_FMT " = 1;'; take the line out if it's gone",
                          STR_ARG(entry->name));
                continue;
            }
            if (d->tweaked_file) {
                diag_error(entry->at, "'" STR_FMT "' is set twice", STR_ARG(entry->name));
                diag_note_at(d->tweaked_at, "it's set here too: a tweak has one value, in one .tweaks file");
                ok = false;
                continue;
            }
            d->code_value = d->value;
            d->value = entry->value;
            d->tweaked_at = entry->at;
            char *raw = arena_alloc((size_t)entry->text.len + 1);
            memcpy(raw, entry->text.ptr, (size_t)entry->text.len);
            raw[entry->text.len] = '\0';
            d->tweaked_text = raw;
            d->tweaked_file = tweaks_absolute(path);
        }
    }

    return ok;
}

// The .tweaks file a tweak's value goes to: where it's set, or beside the code that declares it, of that file's name
const char *tweak_file(const decl *d)
{
    if (d->tweaked_file) return d->tweaked_file;
    const char *code = d->unit && d->unit->src ? d->unit->src->path : NULL;
    if (!code) return "";
    sb path = {0};
    const char *dot = strrchr(code, '.');
    const char *slash = strrchr(code, '/');
    const char *back = strrchr(code, '\\');
    if (back && (!slash || back > slash)) slash = back;
    if (dot && (!slash || dot > slash)) sb_putn(&path, code, (size_t)(dot - code));
    else sb_put(&path, code);
    sb_put(&path, ".tweaks");
    return tweaks_absolute(path.data);
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
        tweak_type_text(&text, d);
        sb_put(&text, " ");
        tweak_name_text(&text, d, '_');
        sb_put(&text, " = ");
        tweak_name_text(&text, d, '.');
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

// A dev build's inspector (tide/inspect.h): the command that takes a field's new value to the server, a type of the
// layout's, where in its value, and up to 32 bytes. Its handler's body is the generated C's (tide_inspect_apply),
// which writes only a field tide_inspect_writable allows, of a component the target has or a singleton.
bool inspect_prelude(program *prog)
{
    static const char text[] = "command TideSetField { int type; int offset; int size; long a; long b; long c; long d; }\n"
                               "command(TideSetField set, PlayerID sender) TideApplyField()\n{\n}\n";
    source *src = NEW(source);
    src->path = INSPECT_PRELUDE_PATH;
    src->text = text;
    src->len = sizeof text - 1;
    diag_add_source(src);
    token *toks = lex(src);
    if (!toks || !parse_file(prog, src, toks, false)) return false;
    for (int i = 0; i < prog->decls.count; i++) {
        decl *d = prog->decls.items[i];
        if (d->unit && d->unit->src == src && d->kind == DECL_EVENT) d->inspect_set = true;
    }
    return true;
}

// ---------------------------------------------------------------------------
// A tweak's value, worked out as the generated C would: the same operations on the same types in the same order,
// with tide/math.h's functions, so a dev build that has a .tweaks file's new values from tide run (which works them
// out with no C built) has what a build would.

typedef enum vkind { V_BOOL, V_INT, V_FLOAT, V_DOUBLE } vkind;

typedef struct tval {
    vkind kind;
    int n; // Components
    bool b;
    int64_t i[4];
    float f[4];
    double d[4];
} tval;

static int components(const type t)
{
    switch (t.kind) {
    case TY_INT2: case TY_FLOAT2: case TY_DOUBLE2: return 2;
    case TY_INT3: case TY_FLOAT3: case TY_DOUBLE3: return 3;
    case TY_INT4: case TY_FLOAT4: case TY_DOUBLE4: case TY_COLOR: return 4;
    default: return 1;
    }
}

static vkind kind_of(const type t)
{
    switch (t.kind) {
    case TY_BOOL: return V_BOOL;
    case TY_FLOAT: case TY_FLOAT2: case TY_FLOAT3: case TY_FLOAT4: case TY_COLOR: return V_FLOAT;
    case TY_DOUBLE: case TY_DOUBLE2: case TY_DOUBLE3: case TY_DOUBLE4: return V_DOUBLE;
    default: return V_INT;
    }
}

// An int's 32 bits, as Tide's wrap (long and byte as theirs)
static int64_t wrap(const int64_t v, const type_kind t)
{
    if (t == TY_LONG) return v;
    if (t == TY_BYTE) return (uint8_t)v;
    return (int32_t)(uint32_t)(uint64_t)v;
}

static float to_float_from_int(const int64_t v) { return (float)v; }

// A value as the type `t` takes it: ints become floats and doubles, a scalar spreads to a vector
static bool convert(tval *v, const type t)
{
    const vkind want = kind_of(t);
    const int n = components(t);
    if (v->n == 1 && n > 1) {
        for (int k = 1; k < n; k++) {
            v->i[k] = v->i[0];
            v->f[k] = v->f[0];
            v->d[k] = v->d[0];
        }
        v->n = n;
    }
    if (v->n != n) return false;
    if (v->kind == want) {
        if (want == V_INT) {
            for (int k = 0; k < n; k++) v->i[k] = wrap(v->i[k], t.kind == TY_LONG || t.kind == TY_BYTE ? t.kind : TY_INT);
        }
        return true;
    }
    if (want == V_FLOAT && v->kind == V_INT) {
        for (int k = 0; k < n; k++) v->f[k] = to_float_from_int(v->i[k]);
    } else if (want == V_DOUBLE && v->kind == V_INT) {
        for (int k = 0; k < n; k++) v->d[k] = (double)v->i[k];
    } else if (want == V_DOUBLE && v->kind == V_FLOAT) {
        for (int k = 0; k < n; k++) v->d[k] = (double)v->f[k];
    } else {
        return false; // What the checker allows no implicit conversion for
    }
    v->kind = want;
    return true;
}

static bool eval(const expr *e, tval *v, int depth);

static double literal_double(const expr *e)
{
    char digits[128];
    int n = 0;
    for (int i = 0; i < e->text.len && n + 1 < (int)sizeof digits; i++) {
        const char ch = e->text.ptr[i];
        if (ch == '_' || ch == 'f' || ch == 'F' || ch == 'd' || ch == 'D') continue;
        digits[n++] = ch;
    }
    digits[n] = '\0';
    return strtod(digits, NULL);
}

static float literal_float(const expr *e)
{
    if (e->is_degrees) return e->float_value;
    char digits[128];
    int n = 0;
    for (int i = 0; i < e->text.len && n + 1 < (int)sizeof digits; i++) {
        const char ch = e->text.ptr[i];
        if (ch == '_' || ch == 'f' || ch == 'F') continue;
        digits[n++] = ch;
    }
    digits[n] = '\0';
    return strtof(digits, NULL);
}

typedef struct math_fn {
    const char *name;
    int argc;
    float (*f1)(float);
    float (*f2)(float, float);
    float (*f3)(float, float, float);
} math_fn;

static float m_abs(float x) { return tide_abs_f(x); }
static float m_sign(float x) { return tide_sign_f(x); }
static float m_floor(float x) { return tide_floor_f(x); }
static float m_ceil(float x) { return tide_ceil_f(x); }
static float m_round(float x) { return tide_round_f(x); }
static float m_trunc(float x) { return tide_trunc_f(x); }
static float m_frac(float x) { return tide_frac_f(x); }
static float m_sqrt(float x) { return tide_sqrt_f(x); }
static float m_saturate(float x) { return tide_saturate_f(x); }
static float m_radians(float x) { return tide_radians_f(x); }
static float m_degrees(float x) { return tide_degrees_f(x); }
static float m_min(float a, float b) { return tide_min_f(a, b); }
static float m_max(float a, float b) { return tide_max_f(a, b); }
static float m_step(float a, float b) { return tide_step_f(a, b); }
static float m_clamp(float x, float a, float b) { return tide_clamp_f(x, a, b); }
static float m_lerp(float a, float b, float t) { return tide_lerp_f(a, b, t); }
static float m_unlerp(float a, float b, float t) { return tide_unlerp_f(a, b, t); }
static float m_smooth(float a, float b, float t) { return tide_smoothstep_f(a, b, t); }

static const math_fn math_fns[] = {
    {"Abs", 1, m_abs, NULL, NULL}, {"Sign", 1, m_sign, NULL, NULL}, {"Floor", 1, m_floor, NULL, NULL},
    {"Ceil", 1, m_ceil, NULL, NULL}, {"Round", 1, m_round, NULL, NULL}, {"Trunc", 1, m_trunc, NULL, NULL},
    {"Frac", 1, m_frac, NULL, NULL}, {"Sqrt", 1, m_sqrt, NULL, NULL}, {"Saturate", 1, m_saturate, NULL, NULL},
    {"Radians", 1, m_radians, NULL, NULL}, {"Degrees", 1, m_degrees, NULL, NULL},
    {"Sin", 1, tide_sin_f, NULL, NULL}, {"Cos", 1, tide_cos_f, NULL, NULL}, {"Tan", 1, tide_tan_f, NULL, NULL},
    {"Asin", 1, tide_asin_f, NULL, NULL}, {"Acos", 1, tide_acos_f, NULL, NULL}, {"Atan", 1, tide_atan_f, NULL, NULL},
    {"Exp", 1, tide_exp_f, NULL, NULL}, {"Exp2", 1, tide_exp2_f, NULL, NULL}, {"Log", 1, tide_log_f, NULL, NULL},
    {"Log2", 1, tide_log2_f, NULL, NULL}, {"Log10", 1, tide_log10_f, NULL, NULL},
    {"Min", 2, NULL, m_min, NULL}, {"Max", 2, NULL, m_max, NULL}, {"Step", 2, NULL, m_step, NULL},
    {"Atan2", 2, NULL, tide_atan2_f, NULL}, {"Pow", 2, NULL, tide_pow_f, NULL},
    {"Clamp", 3, NULL, NULL, m_clamp}, {"Lerp", 3, NULL, NULL, m_lerp}, {"Unlerp", 3, NULL, NULL, m_unlerp},
    {"SmoothStep", 3, NULL, NULL, m_smooth},
};

static bool is_math(const expr *e)
{
    return (e->kind == E_METHOD || e->kind == E_CALL) && e->object && e->object->kind == E_NAME && str_eq_c(e->object->name, "Math");
}

// Math.Name(...): componentwise on floats (ints become floats), as tide/math.h's float functions
static bool eval_math(const expr *e, tval *v, const int depth)
{
    const math_fn *fn = NULL;
    for (size_t i = 0; i < sizeof math_fns / sizeof math_fns[0] && !fn; i++) {
        if (str_eq_c(e->name, math_fns[i].name)) fn = &math_fns[i];
    }
    if (!fn || e->args.count != fn->argc || kind_of(e->type) != V_FLOAT) return false;
    tval a[3];
    for (int i = 0; i < fn->argc; i++) {
        if (!eval(e->args.items[i], &a[i], depth + 1) || !convert(&a[i], e->type)) return false;
    }
    *v = (tval){.kind = V_FLOAT, .n = components(e->type)};
    for (int k = 0; k < v->n; k++) {
        v->f[k] = fn->argc == 1 ? fn->f1(a[0].f[k]) : fn->argc == 2 ? fn->f2(a[0].f[k], a[1].f[k]) : fn->f3(a[0].f[k], a[1].f[k], a[2].f[k]);
    }
    return true;
}

static bool is_constructor(const expr *e)
{
    if (e->kind != E_CALL || e->object) return false;
    static const char *const names[] = {"float", "int", "long", "double", "byte", "float2", "float3", "float4", "int2", "int3",
                                        "int4", "double2", "double3", "double4", "Color"};
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        if (str_eq_c(e->name, names[i])) return true;
    }
    return false;
}

// float3(x, y, z), Color(r, g, b) and the like: their arguments' components, in order
static bool eval_constructor(const expr *e, tval *v, const int depth)
{
    const vkind want = kind_of(e->type);
    const int n = components(e->type);
    *v = (tval){.kind = want, .n = n};
    int at = 0;
    for (int i = 0; i < e->args.count; i++) {
        tval a;
        if (!eval(e->args.items[i], &a, depth + 1)) return false;
        type one = e->type;
        one.kind = want == V_FLOAT ? TY_FLOAT : want == V_DOUBLE ? TY_DOUBLE : want == V_BOOL ? TY_BOOL : TY_INT;
        if (n == 1) {
            // float(x), int(x): a conversion, explicit, so floats become ints too (saturating, NaN as 0)
            if (want == V_INT && a.kind == V_FLOAT) {
                const float x = a.f[0];
                a.i[0] = x != x ? 0 : x >= 2147483647.0f ? INT32_MAX : x <= -2147483648.0f ? INT32_MIN : (int64_t)x;
                a.kind = V_INT;
            } else if (want == V_INT && a.kind == V_DOUBLE) {
                const double x = a.d[0];
                a.i[0] = x != x ? 0 : x >= 2147483647.0 ? INT32_MAX : x <= -2147483648.0 ? INT32_MIN : (int64_t)x;
                a.kind = V_INT;
            } else if (want == V_FLOAT && a.kind == V_DOUBLE) {
                a.f[0] = (float)a.d[0];
                a.kind = V_FLOAT;
            }
        }
        type each = one;
        const int count = a.n;
        if (a.kind != want && !convert(&a, (type){each.kind, NULL})) {
            // A vector argument converts by its own components
            if (count == 1) return false;
        }
        for (int k = 0; k < a.n && at < 4; k++, at++) {
            v->i[at] = a.i[k];
            v->f[at] = a.f[k];
            v->d[at] = a.d[k];
        }
    }
    if (e->type.kind == TY_COLOR && at == 3) v->f[at++] = 1.0f; // Color(r, g, b): opaque
    return at == n || (n > 1 && at == 1 && convert(v, e->type));
}

static bool eval_binary(const expr *e, tval *v, const int depth)
{
    tval l, r;
    if (!eval(e->lhs, &l, depth + 1) || !eval(e->rhs, &r, depth + 1)) return false;
    const tok_kind op = e->op;
    if (op == T_AND || op == T_OR) {
        if (l.kind != V_BOOL || r.kind != V_BOOL) return false;
        *v = (tval){.kind = V_BOOL, .n = 1, .b = op == T_AND ? l.b && r.b : l.b || r.b};
        return true;
    }
    // The type both sides work in: the result's, or for a comparison the wider of the two
    type work = e->type;
    if (e->type.kind == TY_BOOL) work = l.kind == V_DOUBLE || r.kind == V_DOUBLE ? (type){TY_DOUBLE, NULL}
                                      : l.kind == V_FLOAT || r.kind == V_FLOAT ? (type){TY_FLOAT, NULL} : (type){TY_INT, NULL};
    if (e->type.kind == TY_BOOL && (l.n > 1 || r.n > 1)) return false;
    if (!convert(&l, work) || !convert(&r, work)) return false;
    if (e->type.kind == TY_BOOL) {
        int c = 0; // -1, 0, 1, or 2 for unordered
        if (l.kind == V_INT) c = l.i[0] < r.i[0] ? -1 : l.i[0] > r.i[0];
        else if (l.kind == V_FLOAT) c = l.f[0] < r.f[0] ? -1 : l.f[0] > r.f[0] ? 1 : l.f[0] == r.f[0] ? 0 : 2;
        else c = l.d[0] < r.d[0] ? -1 : l.d[0] > r.d[0] ? 1 : l.d[0] == r.d[0] ? 0 : 2;
        bool b;
        switch (op) {
        case T_LT: b = c == -1; break;
        case T_GT: b = c == 1; break;
        case T_LE: b = c == -1 || c == 0; break;
        case T_GE: b = c == 1 || c == 0; break;
        case T_EQ: b = c == 0; break;
        case T_NE: b = c != 0; break;
        default: return false;
        }
        *v = (tval){.kind = V_BOOL, .n = 1, .b = b};
        return true;
    }
    *v = l;
    for (int k = 0; k < v->n; k++) {
        if (v->kind == V_FLOAT) {
            const float a = l.f[k], b = r.f[k];
            v->f[k] = op == T_PLUS ? a + b : op == T_MINUS ? a - b : op == T_STAR ? a * b : op == T_SLASH ? a / b : NAN;
            if (op != T_PLUS && op != T_MINUS && op != T_STAR && op != T_SLASH) return false;
        } else if (v->kind == V_DOUBLE) {
            const double a = l.d[k], b = r.d[k];
            if (op != T_PLUS && op != T_MINUS && op != T_STAR && op != T_SLASH) return false;
            v->d[k] = op == T_PLUS ? a + b : op == T_MINUS ? a - b : op == T_STAR ? a * b : a / b;
        } else {
            const int64_t a = l.i[k], b = r.i[k];
            const type_kind t = work.kind == TY_LONG ? TY_LONG : TY_INT;
            const uint64_t ua = (uint64_t)a, ub = (uint64_t)b;
            switch (op) {
            case T_PLUS: v->i[k] = wrap((int64_t)(ua + ub), t); break;
            case T_MINUS: v->i[k] = wrap((int64_t)(ua - ub), t); break;
            case T_STAR: v->i[k] = wrap((int64_t)(ua * ub), t); break;
            case T_SLASH: v->i[k] = b == 0 ? 0 : (b == -1 ? wrap((int64_t)(0u - ua), t) : wrap(a / b, t)); break;
            case T_PERCENT: v->i[k] = b == 0 || b == -1 ? 0 : wrap(a % b, t); break;
            default: return false;
            }
        }
    }
    return true;
}

static bool eval(const expr *e, tval *v, const int depth)
{
    if (!e || depth > 64) return false;
    switch (e->kind) {
    case E_INT:
        *v = (tval){.kind = V_INT, .n = 1, .i = {e->int_value}};
        return true;
    case E_FLOAT:
        if (e->is_double) *v = (tval){.kind = V_DOUBLE, .n = 1, .d = {literal_double(e)}};
        else *v = (tval){.kind = V_FLOAT, .n = 1, .f = {literal_float(e)}};
        return true;
    case E_BOOL:
        *v = (tval){.kind = V_BOOL, .n = 1, .b = e->bool_value};
        return true;
    case E_NAME:
    case E_MEMBER:
        if (e->enum_member) {
            *v = (tval){.kind = V_INT, .n = 1, .i = {e->enum_member->number}};
            return true;
        }
        if ((e->bind == BIND_CONST || e->bind == BIND_TWEAK) && e->constant && !e->constant->build_fn) {
            return eval(e->constant->value, v, depth + 1) && convert(v, e->constant->return_type);
        }
        return false;
    case E_UNARY: {
        if (!eval(e->lhs ? e->lhs : e->object, v, depth + 1)) return false;
        if (e->op == T_NOT && v->kind == V_BOOL) {
            v->b = !v->b;
            return true;
        }
        if (e->op != T_MINUS) return e->op == T_PLUS;
        for (int k = 0; k < v->n; k++) {
            if (v->kind == V_FLOAT) v->f[k] = -v->f[k];
            else if (v->kind == V_DOUBLE) v->d[k] = -v->d[k];
            else if (v->kind == V_INT) v->i[k] = wrap((int64_t)(0u - (uint64_t)v->i[k]), e->type.kind == TY_LONG ? TY_LONG : TY_INT);
            else return false;
        }
        return true;
    }
    case E_BINARY:
        return eval_binary(e, v, depth);
    case E_CONDITIONAL: {
        tval c;
        if (!eval(e->cond, &c, depth + 1) || c.kind != V_BOOL) return false;
        return eval(c.b ? e->lhs : e->rhs, v, depth + 1) && convert(v, e->type);
    }
    case E_CALL:
    case E_METHOD:
        if (is_math(e)) return eval_math(e, v, depth);
        if (is_constructor(e)) return eval_constructor(e, v, depth);
        return false;
    default:
        return false;
    }
}

bool tweak_value(const decl *k, uint8_t *out, const size_t size)
{
    tval v;
    if (!eval(k->value, &v, 0) || !convert(&v, k->return_type)) return false;
    memset(out, 0, size);
    const type_kind t = k->return_type.kind;
    for (int i = 0; i < v.n; i++) {
        if (v.kind == V_BOOL) {
            if (size < 1) return false;
            out[0] = v.b ? 1u : 0u;
        } else if (v.kind == V_FLOAT) {
            if ((size_t)(i + 1) * 4u > size) return false;
            memcpy(out + 4 * i, &v.f[i], 4);
        } else if (v.kind == V_DOUBLE) {
            if ((size_t)(i + 1) * 8u > size) return false;
            memcpy(out + 8 * i, &v.d[i], 8);
        } else if (t == TY_LONG) {
            if (size < 8) return false;
            memcpy(out, &v.i[0], 8);
        } else if (t == TY_BYTE || size == 1) {
            out[0] = (uint8_t)v.i[0];
        } else if (size == 2) {
            const uint16_t x = (uint16_t)v.i[0];
            memcpy(out, &x, 2);
        } else {
            if ((size_t)(i + 1) * 4u > size) return false;
            const int32_t x = (int32_t)v.i[i];
            memcpy(out + 4 * i, &x, 4);
        }
    }
    return true;
}

bool tweak_value_plain(const decl *k)
{
    const expr *e = k->value;
    while (e && e->kind == E_UNARY && e->op == T_MINUS) e = e->lhs ? e->lhs : e->object;
    if (!e) return false;
    if (e->kind == E_INT || e->kind == E_FLOAT || e->kind == E_BOOL) return true;
    if ((e->kind == E_MEMBER || e->kind == E_NAME) && e->enum_member) return true;
    if (!is_constructor(e)) return false;
    for (int i = 0; i < e->args.count; i++) {
        const expr *a = e->args.items[i];
        while (a && a->kind == E_UNARY && a->op == T_MINUS) a = a->lhs ? a->lhs : a->object;
        if (!a || (a->kind != E_INT && a->kind != E_FLOAT)) return false;
    }
    return true;
}
