#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "types.h"

// Tide code the GPU runs: a `frag` for each pixel of a mesh, a `vert` for
// each of its vertices, and the functions they call. For every combination a
// mesh draw uses (a vertex struct, with a vert, a frag, both or neither) this
// writes a shader in each language a renderer takes: GLSL, for OpenGL 3.3,
// OpenGL ES 3 and WebGL 2, and WGSL, for WebGPU. Codegen puts them in the
// generated C as tide_shaders (tide/draw.h).
//
// The GPU runs less than the CPU does: bools, ints, floats, vectors,
// matrices, colors and structs of those, locals, `if`, loops that count,
// `Math`, and sampling a grid of colors. The same code that writes a shader
// checks that: a first pass over each frag and vert, and the functions they
// reach, writes nothing and reports what doesn't fit (`checking`). What's
// written afterwards always compiles, so nobody sees a shader compiler's
// error: every name of the game's gets a prefix no keyword has, conversions
// are written out, WGSL's rules for pointers, swizzles and constant
// expressions are worked around here.
//
// What the code reads is what's sent: a component's fields for each
// instance, as float4 and int4 vertex attributes, and a singleton's for each
// draw, as float4s and int4s of one uniform block. A value is words of those,
// floats and ints apart (value_text), so both languages lay them out alike.

typedef enum lang { GLSL, WGSL } lang;

enum { STAGE_VERT, STAGE_FRAG };

// Functions a shader may need defined before its own.
enum {
    HELP_F = 1,         // tide_f(x): x, a float the compiler can't fold
    HELP_I = 2,         // tide_i(x): the same for an int
    HELP_NORMALIZE = 4, // tide_normalize<n>(v), for n = 2, 3, 4: three bits from here
};

typedef struct emit {
    program *prog;
    lang l;
    bool checking;     // Reports what the GPU can't run; what it writes is thrown away
    bool failed;
    const shader *sh;  // The shader being written; NULL while checking
    int stage;
    decl *root;        // The frag or vert
    decl *code;        // Whose body is being written: the root, or a function or method it reaches
    sb *o;
    int indent;
    int defuse;        // Inside an expression whose literals go through tide_f and tide_i
    int temps;
    const expr *statement; // The call a statement makes: its mut arguments can go through copies
    VEC(const decl *) structs; // The structs the code names, each after the ones it holds
    VEC(const decl *) done;    // The functions written, callees first
    sb functions;
    unsigned helpers;
} emit;

static void ex(emit *em, const expr *e);
static void ex_as(emit *em, const expr *e, type want);
static void st(emit *em, const stmt *s);
static void need_function(emit *em, decl *fn, loc at);

static const char *shader_word(const decl *d)
{
    return d->is_frag ? "frag" : "vert";
}

// Something the GPU can't run, where it's written. Code a frag or vert only
// reaches through a function says which one reaches it.
static void refuse(emit *em, const loc at, const char *fmt, ...)
{
    em->failed = true;
    if (!em->checking) return;
    char text[640];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof text, fmt, args);
    va_end(args);
    diag_error(at, "%s", text);
    if (em->code != em->root) {
        diag_note_at(em->root->at, "%s '" STR_FMT "' runs on the GPU, and '" STR_FMT "' with it, as it calls it",
                     shader_word(em->root), STR_ARG(em->root->name), STR_ARG(em->code->name));
    }
}

// ---------------------------------------------------------------------------
// Names and types

// A name of the game's in a shader: a prefix no keyword of either language
// starts with, then the name, its '_' as "_0" and its '.' as "_1", so no two
// underscores ever meet (GLSL keeps those for itself) and no two names
// collide.
static const char *name_of(const char *prefix, const str name)
{
    sb b = {0};
    sb_put(&b, prefix);
    for (int i = 0; i < name.len; i++) {
        if (name.ptr[i] == '_') sb_put(&b, "_0");
        else if (name.ptr[i] == '.') sb_put(&b, "_1");
        else sb_putn(&b, &name.ptr[i], 1);
    }
    return b.data;
}

static const char *local_name(const str name)
{
    return name_of("t_", name);
}

static const char *field_name(const field *f)
{
    return name_of("m_", f->name);
}

static const char *struct_name(const decl *d)
{
    return name_of("tt_", d->qualified.len ? d->qualified : d->name);
}

// A function's name, or a method's: its type's, then which of its methods it is.
static const char *function_name(const decl *fn)
{
    if (!fn->owner) return name_of(fn->kind == DECL_SHADER ? "ts_" : "tf_", fn->qualified.len ? fn->qualified : fn->name);
    int index = 0;
    while (index < fn->owner->methods.count && fn->owner->methods.items[index] != fn) index++;
    sb b = {0};
    sb_printf(&b, "%s_2m%d", name_of("tf_", fn->owner->qualified.len ? fn->owner->qualified : fn->owner->name), index);
    return b.data;
}

static bool is_struct_like(const type t)
{
    return t.kind == TY_STRUCT || t.kind == TY_COMPONENT || t.kind == TY_SINGLETON;
}

// How many floats and ints a value of type `t` is, as the GPU is sent it.
// False for a type it can't have.
static bool type_words(const type t, int *floats, int *ints)
{
    if (t.kind == TY_BYTE) return false;
    const int dim = type_dim(t);
    if (dim > 0) {
        *(type_is_float_based(t) ? floats : ints) += dim;
        return true;
    }
    const int n = matrix_dim(t);
    if (n > 0) {
        *floats += n * n;
        return true;
    }
    switch (t.kind) {
    case TY_COLOR:
    case TY_QUATERNION:
        *floats += 4;
        return true;
    case TY_BOOL:
    case TY_ENUM:
        *ints += 1;
        return true;
    case TY_STRUCT:
    case TY_COMPONENT:
    case TY_SINGLETON:
        for (int i = 0; i < t.decl->fields.count; i++) {
            const field *f = &t.decl->fields.items[i];
            if (!f->hidden && !type_words(f->type, floats, ints)) return false;
        }
        return true;
    default:
        return false;
    }
}

// Whether the GPU has values of type `t`; says why not, at `at`.
static bool type_fits(emit *em, const type t, const loc at)
{
    int floats = 0, ints = 0;
    if (t.kind == TY_ERROR || type_words(t, &floats, &ints)) {
        if (is_struct_like(t) && floats + ints == 0) {
            refuse(em, at, "%s has no fields, so there's nothing of it for the GPU to hold", type_name(t));
            return false;
        }
        return true;
    }
    const char *why = "the GPU runs on bools, ints, floats, vectors, matrices, colors and structs of those";
    switch (t.kind) {
    case TY_BYTE: why = "the GPU's ints are 32 bits: convert it with int(x) before it gets there"; break;
    case TY_LONG: why = "the GPU's ints are 32 bits: convert it with int(x) before it gets there"; break;
    case TY_STRING: why = "text is the CPU's"; break;
    case TY_LIST:
    case TY_MAP:
    case TY_SET: why = "the GPU has no lists, maps or sets: it reads fields of numbers, and samples grids of colors"; break;
    case TY_GRID: why = "a grid on the GPU is a texture, read with 'grid.Sample(uv)' where it's a field of a parameter"; break;
    case TY_TEXTURE: why = "a texture is sampled with 'texture.Sample(uv)', not kept as a GPU value"; break;
    case TY_RENDER_TARGET: why = "a render target belongs to the CPU: sample its color as a Texture"; break;
    case TY_ENTITY:
    case TY_LOCAL_ENTITY:
    case TY_PLAYER: why = "it's a handle into the world, which the GPU doesn't have"; break;
    case TY_RECT: why = "pass its x, y, width and height as a float4"; break;
    case TY_OPTIONAL:
    case TY_FAILABLE: why = "the GPU has no values that may be missing: handle it on the CPU"; break;
    case TY_RECORD:
        refuse(em, at, "a %s is read by its fields, where the frag or vert takes it: it's not a value to keep or pass on",
               type_name(t));
        return false;
    default: break;
    }
    refuse(em, at, "%s can't go to the GPU: %s", type_name(t), why);
    return false;
}

static void need_struct(emit *em, const decl *d)
{
    for (int i = 0; i < em->structs.count; i++) {
        if (em->structs.items[i] == d) return;
    }
    for (int i = 0; i < d->fields.count; i++) {
        const field *f = &d->fields.items[i];
        if (!f->hidden && is_struct_like(f->type)) need_struct(em, f->type.decl);
    }
    vec_push(em->structs, d);
}

static const char *ty(emit *em, const type t)
{
    static const char *const glsl_f[] = {"", "float", "vec2", "vec3", "vec4"};
    static const char *const wgsl_f[] = {"", "f32", "vec2f", "vec3f", "vec4f"};
    static const char *const glsl_i[] = {"", "int", "ivec2", "ivec3", "ivec4"};
    static const char *const wgsl_i[] = {"", "i32", "vec2i", "vec3i", "vec4i"};
    static const char *const glsl_m[] = {"", "", "mat2", "mat3", "mat4"};
    static const char *const wgsl_m[] = {"", "", "mat2x2f", "mat3x3f", "mat4x4f"};
    const bool wgsl = em->l == WGSL;
    const int dim = type_dim(t);
    if (dim > 0) return type_is_float_based(t) ? (wgsl ? wgsl_f : glsl_f)[dim] : (wgsl ? wgsl_i : glsl_i)[dim];
    if (matrix_dim(t)) return (wgsl ? wgsl_m : glsl_m)[matrix_dim(t)];
    if (t.kind == TY_COLOR || t.kind == TY_QUATERNION) return wgsl ? "vec4f" : "vec4";
    if (t.kind == TY_BOOL) return "bool";
    if (t.kind == TY_ENUM) return wgsl ? "i32" : "int";
    if (is_struct_like(t)) {
        need_struct(em, t.decl);
        return struct_name(t.decl);
    }
    return wgsl ? "f32" : "float"; // Refused already
}

static const char *float_ty(emit *em, const int dim)
{
    return ty(em, vector_type(true, dim));
}

// A float as both languages read it.
static void put_float(emit *em, const double value)
{
    double v = value;
    if (v != v) v = 0.0;
    if (v > 3.4028234e38) v = 3.4028234e38;
    if (v < -3.4028234e38) v = -3.4028234e38;
    char text[64];
    snprintf(text, sizeof text, "%.9g", v);
    if (!strpbrk(text, ".e")) strcat(text, ".0");
    if (em->defuse > 0) {
        em->helpers |= HELP_F;
        sb_printf(em->o, "tide_f(%s)", text);
    } else {
        sb_put(em->o, text);
    }
}

static void put_int(emit *em, const long long value)
{
    if (em->defuse > 0) {
        em->helpers |= HELP_I;
        sb_printf(em->o, "tide_i(%lld)", value);
    } else {
        sb_printf(em->o, "%lld", value);
    }
}

static long long int_literal(const expr *e)
{
    if (e->text.len == 0) return e->int_value; // The 1 of i++
    char text[64];
    int n = 0;
    for (int i = 0; i < e->text.len && n < 63; i++) {
        if (e->text.ptr[i] != '_') text[n++] = e->text.ptr[i];
    }
    text[n] = '\0';
    return strtoll(text, NULL, 0);
}

static void put_zero(emit *em, type t);

// The value a field, or a local with none given, starts with.
static void put_default(emit *em, const type t)
{
    if (!is_struct_like(t)) {
        put_zero(em, t);
        return;
    }
    sb_printf(em->o, "%s(", ty(em, t));
    bool first = true;
    for (int i = 0; i < t.decl->fields.count; i++) {
        const field *f = &t.decl->fields.items[i];
        if (f->hidden) continue;
        if (!first) sb_put(em->o, ", ");
        first = false;
        if (f->default_value && f->default_value->type.kind != TY_ERROR && f->default_value->type.kind != TY_VOID) {
            ex_as(em, f->default_value, f->type);
        } else {
            put_default(em, f->type);
        }
    }
    sb_put(em->o, ")");
}

static void put_zero(emit *em, const type t)
{
    if (is_struct_like(t)) {
        put_default(em, t);
        return;
    }
    if (em->l == WGSL) {
        sb_printf(em->o, "%s()", ty(em, t));
        return;
    }
    if (t.kind == TY_BOOL) sb_put(em->o, "false");
    else if (t.kind == TY_INT || t.kind == TY_ENUM) sb_put(em->o, "0");
    else if (t.kind == TY_FLOAT) sb_put(em->o, "0.0");
    else if (type_is_int_based(t)) sb_printf(em->o, "%s(0)", ty(em, t));
    else sb_printf(em->o, "%s(0.0)", ty(em, t));
}

// ---------------------------------------------------------------------------
// What the draw supplies

// The float4 or int4 that holds word `word` of a stream, as the stage names
// it: an instance's ('n' and 'j'), what the vertex stage passes on of it to
// the pixels ('f' and 'g'), or the uniforms' ('u' and 'v').
static void put_pack(emit *em, const char stream, const int word)
{
    const bool uniform = stream == 'u' || stream == 'v';
    sb_printf(em->o, "%stide_%c%d", uniform && em->l == WGSL ? "tide_block." : "", stream, word / 4);
}

static void put_word(emit *em, const char stream, const int word)
{
    put_pack(em, stream, word);
    sb_printf(em->o, ".%c", "xyzw"[word % 4]);
}

// `count` words of a stream from `at`, as a vector (or a scalar) of them.
static void put_words(emit *em, const char stream, const int at, const int count, const char *vector)
{
    if (count == 1) {
        put_word(em, stream, at);
    } else if (at / 4 == (at + count - 1) / 4) { // In one float4: a swizzle of it
        put_pack(em, stream, at);
        if (count < 4) sb_printf(em->o, ".%.*s", count, &"xyzw"[at % 4]);
    } else {
        sb_printf(em->o, "%s(", vector);
        for (int i = 0; i < count; i++) {
            if (i) sb_put(em->o, ", ");
            put_word(em, stream, at + i);
        }
        sb_put(em->o, ")");
    }
}

// A value of type `t` from a float stream and an int stream, at *f and *i,
// which move past it.
static void put_value(emit *em, const type t, const char floats, const char ints, int *f, int *i)
{
    const int dim = type_dim(t);
    const int n = matrix_dim(t);
    if (dim > 0 && type_is_float_based(t)) {
        put_words(em, floats, *f, dim, ty(em, t));
        *f += dim;
    } else if (dim > 0) {
        put_words(em, ints, *i, dim, ty(em, t));
        *i += dim;
    } else if (t.kind == TY_COLOR || t.kind == TY_QUATERNION) {
        put_words(em, floats, *f, 4, ty(em, t));
        *f += 4;
    } else if (n > 0) {
        sb_printf(em->o, "%s(", ty(em, t));
        for (int c = 0; c < n; c++) {
            if (c) sb_put(em->o, ", ");
            put_words(em, floats, *f + c * n, n, float_ty(em, n));
        }
        sb_put(em->o, ")");
        *f += n * n;
    } else if (t.kind == TY_BOOL) {
        sb_put(em->o, "(");
        put_word(em, ints, *i);
        sb_put(em->o, " != 0)");
        *i += 1;
    } else if (t.kind == TY_ENUM) {
        put_word(em, ints, *i);
        *i += 1;
    } else if (is_struct_like(t)) {
        sb_printf(em->o, "%s(", ty(em, t));
        bool first = true;
        for (int k = 0; k < t.decl->fields.count; k++) {
            const field *fl = &t.decl->fields.items[k];
            if (fl->hidden) continue;
            if (!first) sb_put(em->o, ", ");
            first = false;
            put_value(em, fl->type, floats, ints, f, i);
        }
        sb_put(em->o, ")");
    }
}

static const shader_value *find_value(const shader *sh, const decl *source, const field *f)
{
    for (int i = 0; i < sh->values.count; i++) {
        if (sh->values.items[i].source == source && sh->values.items[i].field == f) return &sh->values.items[i];
    }
    return NULL;
}

// Field `f` of the component or singleton `source` (the draw's transform for
// NULL), as the stage being written has it.
static void put_bound(emit *em, const decl *source, const field *f, const type t)
{
    const shader_value *v = em->sh ? find_value(em->sh, source, f) : NULL;
    if (!v) { // Checking, or a 2D mesh's transform: there's none
        if (!source && !em->checking) {
            sb_put(em->o, em->l == WGSL ? "mat4x4f(1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0)"
                                        : "mat4(1.0)");
        } else {
            put_zero(em, t);
        }
        return;
    }
    int f_at = v->float_at, i_at = v->int_at;
    char floats = 'u', ints = 'v';
    if (v->per_instance && em->stage == STAGE_FRAG) {
        floats = 'f', ints = 'g';
        f_at = v->frag_float_at, i_at = v->frag_int_at;
    } else if (v->per_instance) {
        floats = 'n', ints = 'j';
    }
    put_value(em, t, floats, ints, &f_at, &i_at);
}

static int field_index(const decl *d, const field *f)
{
    return (int)(f - d->fields.items);
}

// Whether `p` is a parameter of the frag or vert that the engine supplies: a
// component, a singleton, Pixel or Instance.
static bool is_bound(const emit *em, const param *p)
{
    return em->code == em->root && !p->function_param;
}

static int param_index(const decl *d, const param *p)
{
    return (int)(p - d->params.items);
}

// ---------------------------------------------------------------------------
// Expressions

static bool is_const(const expr *e)
{
    if (!e) return true;
    switch (e->kind) {
    case E_INT:
    case E_FLOAT:
    case E_BOOL:
        return true;
    case E_NAME:
        return e->bind == BIND_CONST;
    case E_MEMBER:
        return e->c_constant || e->enum_member || e->constant || (e->swizzle_len && is_const(e->object));
    case E_UNARY:
        return !e->method && is_const(e->lhs);
    case E_BINARY:
        return !e->method && is_const(e->lhs) && is_const(e->rhs);
    case E_CONDITIONAL:
        return is_const(e->cond) && is_const(e->lhs) && is_const(e->rhs);
    case E_CALL:
    case E_METHOD:
        if (e->call != CALL_CONSTRUCT && e->call != CALL_BUILTIN) return false;
        for (int i = 0; i < e->args.count; i++) {
            if (!is_const(e->args.items[i])) return false;
        }
        return true;
    default:
        return false;
    }
}

// Whether a mut argument can be handed over as it is in WGSL, which takes a
// pointer to a whole variable: a local, or a parameter that's one already.
static bool is_whole_variable(const expr *e)
{
    return e->kind == E_NAME && (e->bind == BIND_LOCAL || (e->bind == BIND_PARAM && e->param->function_param));
}

// The copy a call's mut argument `arg` goes through in WGSL (its receiver's
// for -1), named as no local of the game's can be: 'tide_' is the engine's.
static const char *copy_name(const int arg)
{
    char name[32];
    if (arg < 0) snprintf(name, sizeof name, "tide_self_copy");
    else snprintf(name, sizeof name, "tide_copy%d", arg);
    return local_name(str_from(name));
}

static bool is_pointer(const emit *em, const param *p)
{
    return em->l == WGSL && p->function_param && p->mode == PARAM_MUT;
}

static void put_self(emit *em)
{
    const bool pointer = em->l == WGSL && em->code->is_mut_method;
    sb_put(em->o, pointer ? "(*tide_self)" : "tide_self");
}

static void ex_name(emit *em, const expr *e)
{
    switch (e->bind) {
    case BIND_LOCAL:
        sb_put(em->o, local_name(e->local->name));
        return;
    case BIND_PARAM: {
        const param *p = e->param;
        if (is_bound(em, p)) { // All of a component or singleton, as a value
            if (p->type.kind == TY_RECORD) {
                refuse(em, e->at, "'" STR_FMT "' is read by its fields, like '" STR_FMT "." STR_FMT "': it's not a value to pass on",
                       STR_ARG(e->name), STR_ARG(e->name), STR_ARG(p->type.decl->fields.items[0].name));
                return;
            }
            if (em->checking) em->root->params.items[param_index(em->root, p)].whole = true;
            if (!type_fits(em, p->type, e->at)) return;
            sb_printf(em->o, "%s(", ty(em, p->type));
            bool first = true;
            for (int i = 0; i < p->type.decl->fields.count; i++) {
                const field *f = &p->type.decl->fields.items[i];
                if (f->hidden) continue;
                if (!first) sb_put(em->o, ", ");
                first = false;
                put_bound(em, p->type.decl, f, f->type);
            }
            sb_put(em->o, ")");
            return;
        }
        if (em->checking && em->code == em->root) em->root->params.items[param_index(em->root, p)].whole = true;
        sb_printf(em->o, is_pointer(em, p) ? "(*%s)" : "%s", local_name(p->name));
        return;
    }
    case BIND_FIELD:
        put_self(em);
        sb_printf(em->o, ".%s", field_name(e->field));
        return;
    case BIND_CONST:
        ex_as(em, e->constant->value, e->constant->return_type);
        return;
    default:
        refuse(em, e->at, "'" STR_FMT "' isn't something the GPU has", STR_ARG(e->name));
    }
}

// Math.PI, Color.red, float4x4.identity: a built-in constant.
static void ex_static(emit *em, const expr *e)
{
    static const struct {
        const char *name;
        float r, g, b, a;
    } colors[] = {{"white", 1, 1, 1, 1}, {"black", 0, 0, 0, 1}, {"red", 1, 0, 0, 1}, {"green", 0, 1, 0, 1},
                  {"blue", 0, 0, 1, 1}, {"yellow", 1, 0.92156863f, 0.015686275f, 1}, {"cyan", 0, 1, 1, 1},
                  {"magenta", 1, 0, 1, 1}, {"gray", 0.5f, 0.5f, 0.5f, 1}, {"clear", 0, 0, 0, 0}};
    const str owner = e->object->name;
    if (str_eq_c(owner, "Math")) {
        put_float(em, str_eq_c(e->member, "PI") ? 3.14159265358979 : str_eq_c(e->member, "TAU") ? 6.28318530717959
                                                                                             : 2.71828182845905);
        return;
    }
    if (str_eq_c(owner, "Color")) {
        for (size_t i = 0; i < sizeof colors / sizeof colors[0]; i++) {
            if (!str_eq_c(e->member, colors[i].name)) continue;
            sb_printf(em->o, "%s(", float_ty(em, 4));
            put_float(em, colors[i].r);
            sb_put(em->o, ", ");
            put_float(em, colors[i].g);
            sb_put(em->o, ", ");
            put_float(em, colors[i].b);
            sb_put(em->o, ", ");
            put_float(em, colors[i].a);
            sb_put(em->o, ")");
            return;
        }
    }
    const int n = matrix_dim(e->type);
    if (n > 0 && str_eq_c(e->member, "identity")) {
        sb_printf(em->o, "%s(", ty(em, e->type));
        for (int i = 0; i < n * n; i++) sb_printf(em->o, "%s%s", i ? ", " : "", i / n == i % n ? "1.0" : "0.0");
        sb_put(em->o, ")");
        return;
    }
    if (e->type.kind == TY_QUATERNION) {
        sb_printf(em->o, "%s(0.0, 0.0, 0.0, 1.0)", float_ty(em, 4));
        return;
    }
    refuse(em, e->at, "'" STR_FMT "." STR_FMT "' is the CPU's: a frag or vert reads what it takes as parameters", STR_ARG(owner),
           STR_ARG(e->member));
}

// pixel.position and the like: what the GPU says of the pixel being drawn.
static void ex_pixel(emit *em, const expr *e)
{
    const bool wgsl = em->l == WGSL;
    if (str_eq_c(e->member, "position")) {
        // The screen's pixels from the top left, as the GUI's: OpenGL counts from the bottom
        if (wgsl) sb_put(em->o, "(tide_at.xy / tide_block.tide_screen.xy)");
        else sb_put(em->o, "(vec2(gl_FragCoord.x, tide_screen.z - gl_FragCoord.y) / tide_screen.xy)");
    } else if (str_eq_c(e->member, "depth")) {
        sb_put(em->o, wgsl ? "tide_at.z" : "gl_FragCoord.z");
    } else {
        sb_put(em->o, "tide_world");
    }
}

static void ex_member(emit *em, const expr *e)
{
    if (e->constant) {
        ex_as(em, e->constant->value, e->constant->return_type);
        return;
    }
    if (e->enum_member) {
        put_int(em, e->enum_member->number);
        return;
    }
    if (e->c_constant) {
        ex_static(em, e);
        return;
    }
    const expr *obj = e->object;
    // A field of what the engine supplies
    if (obj->kind == E_NAME && obj->bind == BIND_PARAM && is_bound(em, obj->param) && e->field) {
        const param *p = obj->param;
        if (em->checking && e->local_field) {
            em->root->params.items[param_index(em->root, p)].local_reads[field_index(p->type.decl->local_part, e->field)] = true;
        } else if (em->checking) {
            em->root->params.items[param_index(em->root, p)].reads[field_index(p->type.decl, e->field)] = true;
        }
        if (p->type.decl == em->prog->pixel) {
            ex_pixel(em, e);
        } else if (p->type.decl == em->prog->instance) {
            put_bound(em, NULL, NULL, e->type);
        } else if (type_fits(em, e->type, e->at)) {
            put_bound(em, p->type.decl, e->field, e->type);
        }
        return;
    }
    // A field of the frag's or vert's own vertex: which ones it reads decides what the mesh's corners pass on
    if (em->checking && em->code == em->root && obj->kind == E_NAME && obj->bind == BIND_PARAM && e->field && obj->param->reads) {
        em->root->params.items[param_index(em->root, obj->param)].reads[field_index(obj->param->type.decl, e->field)] = true;
        if (em->root->is_frag && type_is_int_based(e->type)) {
            refuse(em, e->at, "'" STR_FMT "' is made of ints, which don't blend across a triangle, so a frag can't read it",
                   STR_ARG(e->member));
            diag_note("a vert can read it; for the pixels, keep it as a float in the vertex");
            return;
        }
        sb_printf(em->o, is_pointer(em, obj->param) ? "(*%s).%s" : "%s.%s", local_name(obj->param->name), field_name(e->field));
        return;
    }
    const type ot = obj->type;
    if (e->swizzle_len > 0) {
        ex(em, obj);
        sb_put(em->o, ".");
        for (int i = 0; i < e->swizzle_len; i++) sb_putn(em->o, &"xyzw"[e->swizzle[i]], 1);
    } else if (ot.kind == TY_QUATERNION) {
        ex(em, obj); // Its value, a float4, is what it is here
    } else if (ot.kind == TY_COLOR) {
        ex(em, obj);
        sb_printf(em->o, "." STR_FMT, STR_ARG(e->member));
    } else if (matrix_dim(ot)) {
        ex(em, obj);
        sb_printf(em->o, "[%c]", e->member.ptr[1]);
    } else if (e->field && is_struct_like(ot)) {
        ex(em, obj);
        sb_printf(em->o, ".%s", field_name(e->field));
    } else if (type_fits(em, ot, obj->at)) {
        refuse(em, e->at, "'." STR_FMT "' of %s isn't something the GPU has", STR_ARG(e->member), type_name(ot));
    }
}

// An int or int vector as its float kind, of the same size.
static type as_float(const type t)
{
    const int dim = type_dim(t);
    return dim > 0 ? vector_type(true, dim) : t;
}

static void ex_as(emit *em, const expr *e, const type want)
{
    const type have = e->type;
    if (e->kind == E_DEFAULT) {
        if (type_fits(em, want, e->at)) put_default(em, want);
        return;
    }
    const int have_dim = type_dim(have), want_dim = type_dim(want);
    if (have_dim == 0 || want_dim == 0 || (have.kind == want.kind)) {
        ex(em, e);
        return;
    }
    // A number literal is written as the kind it's wanted as
    if (e->kind == E_INT && want.kind == TY_FLOAT) {
        put_float(em, (double)int_literal(e));
        return;
    }
    if (have_dim == 1 && want_dim > 1) { // A scalar, as a vector of it
        sb_printf(em->o, "%s(", ty(em, want));
        ex_as(em, e, vector_type(type_is_float_based(want), 1));
        sb_put(em->o, ")");
        return;
    }
    sb_printf(em->o, "%s(", ty(em, want));
    ex(em, e);
    sb_put(em->o, ")");
}

static const char *op_text(const tok_kind op)
{
    switch (op) {
    case T_PLUS: return "+";
    case T_MINUS: return "-";
    case T_STAR: return "*";
    case T_SLASH: return "/";
    case T_PERCENT: return "%";
    case T_SHL: return "<<";
    case T_SHR: return ">>";
    case T_AMP: return "&";
    case T_PIPE: return "|";
    case T_CARET: return "^";
    case T_EQ: return "==";
    case T_NE: return "!=";
    case T_LT: return "<";
    case T_LE: return "<=";
    case T_GT: return ">";
    case T_GE: return ">=";
    case T_AND: return "&&";
    case T_OR: return "||";
    default: return "+";
    }
}

// l op r, where r is `value` (or 1 for NULL), as the type `result`.
static void put_binary(emit *em, const tok_kind op, const expr *l, const expr *r, const type result, const loc at)
{
    const type lt = l->type, rt = r->type;
    const bool wgsl = em->l == WGSL;
    const bool risky = op == T_SLASH || op == T_PERCENT || op == T_SHL || op == T_SHR;
    const bool all_const = is_const(l) && is_const(r);
    if (all_const) em->defuse++;
    sb_put(em->o, "(");
    if (op == T_AND || op == T_OR) {
        ex(em, l);
        sb_printf(em->o, " %s ", op_text(op));
        ex(em, r);
    } else if (op == T_EQ || op == T_NE || op == T_LT || op == T_LE || op == T_GT || op == T_GE) {
        // Numbers compare as floats when either is one
        const bool floats = lt.kind == TY_FLOAT || rt.kind == TY_FLOAT;
        ex_as(em, l, floats ? as_float(lt) : lt);
        sb_printf(em->o, " %s ", op_text(op));
        ex_as(em, r, floats ? as_float(rt) : rt);
    } else if (matrix_dim(lt) && op == T_SLASH && wgsl) { // WGSL divides no matrix
        ex(em, l);
        sb_put(em->o, " * (1.0 / ");
        ex_as(em, r, (type){TY_FLOAT, NULL});
        sb_put(em->o, ")");
    } else if (op == T_SHL || op == T_SHR) {
        ex(em, l);
        sb_printf(em->o, " %s ", op_text(op));
        if (is_const(r) && !all_const) em->defuse++;
        if (wgsl) sb_put(em->o, "u32(");
        ex(em, r);
        if (wgsl) sb_put(em->o, ")");
        if (is_const(r) && !all_const) em->defuse--;
    } else {
        // Numbers, vectors, matrices and colors: ints become floats beside a float
        const bool floats = type_is_float_based(result) || matrix_dim(result) || result.kind == TY_COLOR;
        ex_as(em, l, floats ? as_float(lt) : lt);
        sb_printf(em->o, " %s ", op_text(op));
        if (risky && is_const(r) && !all_const) em->defuse++;
        ex_as(em, r, floats ? as_float(rt) : rt);
        if (risky && is_const(r) && !all_const) em->defuse--;
    }
    sb_put(em->o, ")");
    if (all_const) em->defuse--;
    (void)at;
}

// Tide's Math functions that are the shader languages' own.
static const struct {
    const char *name;
    const char *glsl;
    const char *wgsl;
} same_math[] = {
    {"Abs", "abs", "abs"}, {"Sign", "sign", "sign"}, {"Min", "min", "min"}, {"Max", "max", "max"}, {"Clamp", "clamp", "clamp"},
    {"Floor", "floor", "floor"}, {"Ceil", "ceil", "ceil"}, {"Round", "roundEven", "round"}, {"Trunc", "trunc", "trunc"},
    {"Frac", "fract", "fract"}, {"Sqrt", "sqrt", "sqrt"}, {"Rsqrt", "inversesqrt", "inverseSqrt"},
    {"Radians", "radians", "radians"}, {"Degrees", "degrees", "degrees"}, {"Sin", "sin", "sin"}, {"Cos", "cos", "cos"},
    {"Tan", "tan", "tan"}, {"Asin", "asin", "asin"}, {"Acos", "acos", "acos"}, {"Atan", "atan", "atan"},
    {"Atan2", "atan", "atan2"}, {"Exp", "exp", "exp"}, {"Exp2", "exp2", "exp2"}, {"Log", "log", "log"}, {"Log2", "log2", "log2"},
    {"Pow", "pow", "pow"}, {"Step", "step", "step"}, {"Lerp", "mix", "mix"}, {"SmoothStep", "smoothstep", "smoothstep"},
    {"Dot", "dot", "dot"}, {"Length", "length", "length"}, {"Distance", "distance", "distance"},
    {"Reflect", "reflect", "reflect"}, {"Cross", "cross", "cross"},
    {"Transpose", "transpose", "transpose"}, {"Determinant", "determinant", "determinant"},
};

static void put_arg(emit *em, const expr *e, const int i)
{
    ex_as(em, e->args.items[i], i < e->arg_want.count ? e->arg_want.items[i] : e->args.items[i]->type);
}

static void ex_math(emit *em, const expr *e)
{
    const bool wgsl = em->l == WGSL;
    const type first = e->arg_want.count ? e->arg_want.items[0] : (type){TY_ERROR, NULL};
    for (int i = 0; i < e->arg_want.count; i++) {
        if (e->arg_want.items[i].kind == TY_QUATERNION && !str_eq_c(e->name, "Dot") && !str_eq_c(e->name, "Normalize")) {
            refuse(em, e->at, "Math." STR_FMT " on a quaternion isn't on the GPU yet", STR_ARG(e->name));
            diag_note("turn the rotation into a float3x3 on the CPU, 'float3x3(rotation)', keep it in a component or a "
                      "singleton, and multiply with Math.Mul there");
            return;
        }
    }
    // The compiler folds nothing here: a constant out of a function's range is an error to WGSL, not a value
    em->defuse++;
    const int dim = type_dim(first) ? type_dim(first) : 4;
    if (str_eq_c(e->name, "Saturate")) {
        if (wgsl) sb_put(em->o, "saturate(");
        else sb_put(em->o, "clamp(");
        put_arg(em, e, 0);
        sb_put(em->o, wgsl ? ")" : ", 0.0, 1.0)");
    } else if (str_eq_c(e->name, "Log10")) {
        sb_put(em->o, "(log(");
        put_arg(em, e, 0);
        sb_put(em->o, ") * 0.434294482)");
    } else if (str_eq_c(e->name, "Unlerp")) {
        sb_put(em->o, "((");
        put_arg(em, e, 2);
        sb_put(em->o, " - ");
        put_arg(em, e, 0);
        sb_put(em->o, ") / (");
        put_arg(em, e, 1);
        sb_put(em->o, " - ");
        put_arg(em, e, 0);
        sb_put(em->o, "))");
    } else if (str_eq_c(e->name, "LengthSq")) {
        sb_put(em->o, "dot(");
        put_arg(em, e, 0);
        sb_put(em->o, ", ");
        put_arg(em, e, 0);
        sb_put(em->o, ")");
    } else if (str_eq_c(e->name, "DistanceSq")) {
        sb_put(em->o, "dot(");
        for (int k = 0; k < 2; k++) {
            sb_put(em->o, k ? ", " : "");
            put_arg(em, e, 0);
            sb_put(em->o, " - ");
            put_arg(em, e, 1);
        }
        sb_put(em->o, ")");
    } else if (str_eq_c(e->name, "Normalize")) { // Zero for a zero vector, as on the CPU: the GPU's own gives nothing sure
        em->helpers |= (unsigned)HELP_NORMALIZE << (dim - 2);
        sb_printf(em->o, "tide_normalize%d(", dim);
        put_arg(em, e, 0);
        sb_put(em->o, ")");
    } else if (str_eq_c(e->name, "Csum")) {
        sb_put(em->o, "dot(");
        put_arg(em, e, 0);
        sb_printf(em->o, ", %s(1.0))", float_ty(em, dim));
    } else if (str_eq_c(e->name, "Cmin") || str_eq_c(e->name, "Cmax")) {
        const char *fn = str_eq_c(e->name, "Cmin") ? "min" : "max";
        for (int k = 0; k + 1 < dim; k++) sb_printf(em->o, "%s(", fn);
        for (int k = 0; k < dim; k++) {
            put_arg(em, e, 0);
            sb_printf(em->o, ".%c%s", "xyzw"[k], k == 0 ? ", " : k + 1 < dim ? "), " : ")");
        }
    } else if (str_eq_c(e->name, "Mul")) {
        sb_put(em->o, "(");
        put_arg(em, e, 0);
        sb_put(em->o, " * ");
        put_arg(em, e, 1);
        sb_put(em->o, ")");
    } else if ((str_eq_c(e->name, "Transform") || str_eq_c(e->name, "Rotate")) && first.kind == TY_FLOAT4X4) {
        sb_put(em->o, "(");
        put_arg(em, e, 0);
        sb_printf(em->o, " * %s(", float_ty(em, 4));
        put_arg(em, e, 1);
        sb_printf(em->o, ", %s)).xyz", str_eq_c(e->name, "Transform") ? "1.0" : "0.0");
    } else {
        const char *fn = NULL;
        for (size_t i = 0; i < sizeof same_math / sizeof same_math[0]; i++) {
            if (str_eq_c(e->name, same_math[i].name)) fn = wgsl ? same_math[i].wgsl : same_math[i].glsl;
        }
        if (!fn || (str_eq_c(e->name, "Inverse"))) {
            em->defuse--;
            refuse(em, e->at, "Math." STR_FMT " isn't on the GPU yet", STR_ARG(e->name));
            if (str_eq_c(e->name, "Inverse")) {
                diag_note("invert it on the CPU, in the view, and keep the result in a component or a singleton it reads");
            } else if (str_eq_c(e->name, "Hash")) {
                diag_note("work it out on the CPU and keep it in a component, or sample a grid of noise");
            }
            return;
        }
        sb_printf(em->o, "%s(", fn);
        for (int i = 0; i < e->args.count; i++) {
            if (i) sb_put(em->o, ", ");
            put_arg(em, e, i);
        }
        sb_put(em->o, ")");
    }
    em->defuse--;
}

// float3(1, 2, 3), int(x), Color(r, g, b)
static void ex_construct(emit *em, const expr *e)
{
    const type t = e->type;
    const bool floats = type_is_float_based(t) || matrix_dim(t) || t.kind == TY_COLOR;
    const int n = matrix_dim(t);
    switch (e->ctor) {
    case CTOR_SCALAR:
    case CTOR_CONVERT:
        if (e->args.items[0]->kind == E_INT && t.kind == TY_FLOAT) {
            put_float(em, (double)int_literal(e->args.items[0]));
            return;
        }
        // A constant out of the type's range is an error to WGSL, not a value
        if (is_const(e->args.items[0])) em->defuse++;
        sb_printf(em->o, "%s(", ty(em, t));
        ex(em, e->args.items[0]);
        sb_put(em->o, ")");
        if (is_const(e->args.items[0])) em->defuse--;
        return;
    case CTOR_SPLAT:
        sb_printf(em->o, "%s(", ty(em, t));
        ex_as(em, e->args.items[0], vector_type(floats, 1));
        sb_put(em->o, ")");
        return;
    case CTOR_ENUM: { // An enum is an int here whatever it's stored as: one stored as less keeps its low bits, as on the CPU
        const int width = t.decl->width;
        if (width != 1 && width != 2) {
            ex(em, e->args.items[0]);
            return;
        }
        sb_put(em->o, "(");
        ex(em, e->args.items[0]);
        sb_printf(em->o, " & %d)", width == 1 ? 255 : 65535);
        return;
    }
    case CTOR_COMPONENTS:
    case CTOR_MAT_COLUMNS:
    case CTOR_COLOR:
        sb_printf(em->o, "%s(", ty(em, t));
        for (int i = 0; i < e->args.count; i++) {
            const expr *arg = e->args.items[i];
            if (i) sb_put(em->o, ", ");
            ex_as(em, arg, floats ? as_float(arg->type) : arg->type);
        }
        if (e->ctor == CTOR_COLOR && e->args.count == 3) sb_put(em->o, ", 1.0");
        sb_put(em->o, ")");
        return;
    case CTOR_MAT_SCALARS: // Written row by row; both languages take columns
        sb_printf(em->o, "%s(", ty(em, t));
        for (int c = 0; c < n; c++) {
            for (int r = 0; r < n; r++) {
                if (c || r) sb_put(em->o, ", ");
                ex_as(em, e->args.items[r * n + c], (type){TY_FLOAT, NULL});
            }
        }
        sb_put(em->o, ")");
        return;
    default:
        refuse(em, e->at, "%s isn't built this way on the GPU yet", type_name(t));
        diag_note("build it on the CPU, in the view, and keep it in a component or a singleton the code reads");
    }
}

// A texture parameter, or a grid or texture field: which binding reads it.
static void ex_sample(emit *em, const expr *e)
{
    const expr *of = e->object;
    const bool direct = of->kind == E_NAME;
    const param *p = direct ? of->param : of->object->param;
    const bool blended = e->args.count < 2 || e->args.items[1]->enum_member->number != 0;
    int slot = 0;
    if (em->checking) {
        if (em->code != em->root) {
            refuse(em, e->at, "a texture is sampled in the frag or vert that takes it, not in a function it calls");
            return;
        }
        const decl *holds = direct ? NULL : of->local_field ? p->type.decl->local_part : p->type.decl;
        const shader_sample sample = {param_index(em->root, p), direct ? -1 : field_index(holds, of->field),
                                      of->local_field, blended};
        bool known = false;
        for (int i = 0; i < em->root->samples.count; i++) {
            const shader_sample *s = &em->root->samples.items[i];
            known |= s->param == sample.param && s->field == sample.field && s->local == sample.local && s->blended == sample.blended;
        }
        if (!known) vec_push(em->root->samples, sample);
    } else {
        while (slot < em->sh->textures.count) {
            const shader_texture *t = &em->sh->textures.items[slot];
            if (t->direct == direct && t->source == p->type.decl && t->field == of->field && t->blended == blended) break;
            slot++;
        }
    }
    // No pixel's neighbors are asked about, so it can be sampled anywhere, in a branch or a vertex
    if (em->l == WGSL) sb_printf(em->o, "textureSampleLevel(pixels%d, sampler%d, ", slot, slot);
    else sb_printf(em->o, "textureLod(pixels%d, ", slot);
    ex_as(em, e->args.items[0], (type){TY_FLOAT2, NULL});
    sb_put(em->o, ", 0.0)");
}

// A call of a function or method of the game's. A mut argument is the
// caller's variable: GLSL takes it as it is (inout), and WGSL a pointer to a
// whole variable, so what isn't one goes through a copy, which only a call
// that's a statement of its own can do.
static void ex_user_call(emit *em, const expr *e)
{
    decl *fn = e->method;
    need_function(em, fn, e->at);
    const bool method = fn->owner && !fn->is_operator && !fn->is_interpolate;
    const bool statement = em->statement == e;
    sb_printf(em->o, "%s(", function_name(fn));
    bool first = true;
    if (method) {
        const expr *receiver = e->kind == E_METHOD ? e->object : NULL;
        first = false;
        if (!receiver) { // Another of its own type's methods: on the same value
            const bool pointer = em->l == WGSL && em->code->is_mut_method;
            if (fn->is_mut_method) sb_put(em->o, "tide_self");
            else sb_put(em->o, pointer ? "(*tide_self)" : "tide_self");
        } else if (!fn->is_mut_method) {
            ex(em, receiver);
        } else if (em->l == GLSL) {
            ex(em, receiver);
        } else if (is_whole_variable(receiver)) {
            const bool pointer = receiver->bind == BIND_PARAM && is_pointer(em, receiver->param);
            sb_printf(em->o, "%s%s", pointer ? "" : "&", local_name(receiver->name));
        } else if (statement) {
            sb_printf(em->o, "&%s", copy_name(-1));
        } else {
            refuse(em, e->at, "a call that changes what it's called on is a statement of its own in code the GPU runs");
        }
        if (em->checking && fn->is_mut_method && receiver && !is_whole_variable(receiver) && !statement) {
            refuse(em, e->at, "a call that changes what it's called on is a statement of its own in code the GPU runs");
        }
    }
    for (int i = 0; i < e->args.count; i++) {
        const param *p = i < fn->params.count ? &fn->params.items[i] : NULL;
        const expr *arg = e->args.items[i];
        if (!first) sb_put(em->o, ", ");
        first = false;
        if (!p || p->mode != PARAM_MUT) {
            ex_as(em, arg, p ? p->type : arg->type);
            continue;
        }
        if (em->checking && !is_whole_variable(arg) && !statement) {
            refuse(em, arg->at, "a call that changes '" STR_FMT "' is a statement of its own in code the GPU runs",
                   STR_ARG(p->name));
            diag_note("write the call on a line by itself, or have the function return the new value");
        }
        if (em->l == GLSL) {
            ex(em, arg);
        } else if (is_whole_variable(arg)) {
            const bool pointer = arg->bind == BIND_PARAM && is_pointer(em, arg->param);
            sb_printf(em->o, "%s%s", pointer ? "" : "&", local_name(arg->name));
        } else {
            sb_printf(em->o, "&%s", copy_name(i));
        }
    }
    sb_put(em->o, ")");
}

static void ex_call(emit *em, const expr *e)
{
    const char *what = NULL;
    switch (e->call) {
    case CALL_CONSTRUCT:
        ex_construct(em, e);
        return;
    case CALL_FUNCTION:
    case CALL_METHOD:
        ex_user_call(em, e);
        return;
    case CALL_BUILTIN:
        if (e->kind == E_METHOD && e->object->kind == E_NAME && str_eq_c(e->object->name, "Math")) {
            ex_math(em, e);
            return;
        }
        if (e->kind == E_METHOD && e->object->kind == E_NAME && str_eq_c(e->object->name, "Debug")) {
            refuse(em, e->at, "Debug." STR_FMT " speaks from the CPU: code the GPU runs has nowhere to print", STR_ARG(e->name));
            diag_note("to look at a value, write it to the pixel's color");
            return;
        }
        refuse(em, e->at, "'" STR_FMT "' isn't on the GPU yet", STR_ARG(e->name));
        diag_note("work it out on the CPU, in the view, and keep the result in a component or a singleton the code reads");
        return;
    case CALL_GRID:
        if (str_eq_c(e->name, "Sample")) {
            ex_sample(em, e);
            return;
        }
        what = "change a grid: it reads one as a texture, with 'grid.Sample(uv)'";
        break;
    case CALL_SPAWN:
    case CALL_ADD:
    case CALL_REMOVE:
    case CALL_DESTROY:
    case CALL_LOAD:
    case CALL_UNLOAD:
    case CALL_SCENE_PLAYER:
    case CALL_SNAP: what = "change the world: systems do, on the CPU"; break;
    case CALL_SEND: what = "send events: systems and views do, on the CPU"; break;
    case CALL_DRAW:
    case CALL_GUI: what = "draw: a view does, and names the frag at mesh draw"; break;
    case CALL_LIST:
    case CALL_MAP:
    case CALL_WORLD: what = "use lists, maps or the World: it reads fields of numbers, and samples grids of colors"; break;
    case CALL_TEXT: what = "work with text"; break;
    default: what = "do this"; break;
    }
    refuse(em, e->at, "the GPU can't %s", what);
}

static void ex(emit *em, const expr *e)
{
    switch (e->kind) {
    case E_INT:
        if (e->type.kind == TY_LONG) type_fits(em, e->type, e->at);
        else put_int(em, int_literal(e));
        return;
    case E_FLOAT: {
        char text[64];
        snprintf(text, sizeof text, STR_FMT, STR_ARG(e->text));
        put_float(em, strtod(text, NULL));
        return;
    }
    case E_BOOL:
        sb_put(em->o, e->bool_value ? "true" : "false");
        return;
    case E_NAME:
        ex_name(em, e);
        return;
    case E_MEMBER:
        ex_member(em, e);
        return;
    case E_CALL:
    case E_METHOD:
        if (e->io_operation || e->path_operation || e->storage_flush) {
            refuse(em, e->at, "file and directory access belongs to local CPU code: read it before drawing");
            return;
        }
        if (e->type.kind != TY_VOID && !type_fits(em, e->type, e->at)) return;
        ex_call(em, e);
        return;
    case E_BINARY:
        if (e->method) {
            ex_user_call(em, &(expr){.kind = E_CALL, .at = e->at, .method = e->method, .call = CALL_FUNCTION,
                                     .args = {(expr *[]){e->lhs, e->rhs}, 2, 2}});
            return;
        }
        put_binary(em, e->op, e->lhs, e->rhs, e->type, e->at);
        return;
    case E_UNARY:
        if (e->method) {
            ex_user_call(em, &(expr){.kind = E_CALL, .at = e->at, .method = e->method, .call = CALL_FUNCTION,
                                     .args = {(expr *[]){e->lhs}, 1, 1}});
            return;
        }
        if (is_const(e->lhs)) em->defuse++;
        if (e->op == T_MINUS && matrix_dim(e->type) && em->l == WGSL) { // WGSL negates no matrix
            sb_put(em->o, "(");
            ex(em, e->lhs);
            sb_put(em->o, " * -1.0)");
        } else {
            sb_put(em->o, e->op == T_NOT ? "(!" : e->op == T_TILDE ? "(~" : "(-");
            ex(em, e->lhs);
            sb_put(em->o, ")");
        }
        if (is_const(e->lhs)) em->defuse--;
        return;
    case E_CONDITIONAL:
        if (!type_fits(em, e->type, e->at)) return;
        if (em->l == WGSL || em->checking) {
            // WGSL's select picks between numbers, vectors and bools, and works both sides out
            if (is_struct_like(e->type) || matrix_dim(e->type)) {
                refuse(em, e->at, "'?:' picks between numbers, vectors and colors in code the GPU runs, and this is %s",
                       type_name(e->type));
                diag_note("write it as 'if' and 'else', assigning a variable");
                return;
            }
        }
        if (em->l == WGSL) {
            sb_put(em->o, "select(");
            ex_as(em, e->rhs, e->type);
            sb_put(em->o, ", ");
            ex_as(em, e->lhs, e->type);
            sb_put(em->o, ", ");
            ex(em, e->cond);
            sb_put(em->o, ")");
        } else {
            sb_put(em->o, "(");
            ex(em, e->cond);
            sb_put(em->o, " ? ");
            ex_as(em, e->lhs, e->type);
            sb_put(em->o, " : ");
            ex_as(em, e->rhs, e->type);
            sb_put(em->o, ")");
        }
        return;
    case E_LITERAL: {
        const type t = e->type;
        if (!type_fits(em, t, e->at)) return;
        sb_printf(em->o, "%s(", ty(em, t));
        bool first = true;
        for (int i = 0; i < t.decl->fields.count; i++) {
            const field *f = &t.decl->fields.items[i];
            if (f->hidden) continue;
            if (!first) sb_put(em->o, ", ");
            first = false;
            const expr *value = NULL;
            for (int k = 0; k < e->inits.count; k++) {
                if (e->inits.items[k].field == f) value = e->inits.items[k].value;
            }
            if (value) ex_as(em, value, f->type);
            else if (f->default_value && f->default_value->type.kind != TY_ERROR) ex_as(em, f->default_value, f->type);
            else put_default(em, f->type);
        }
        sb_put(em->o, ")");
        return;
    }
    case E_DEFAULT:
        if (type_fits(em, e->type, e->at)) put_default(em, e->type);
        return;
    case E_STRING:
    case E_INTERP:
        refuse(em, e->at, "text is the CPU's: code the GPU runs has none");
        return;
    case E_INDEX:
    case E_LIST:
        refuse(em, e->at, "the GPU has no lists, maps or grids to index or make: it reads fields of numbers, and samples a "
                          "grid of colors with 'grid.Sample(uv)'");
        return;
    case E_THIS:
        refuse(em, e->at, "the GPU has no entities: code it runs takes values, and has no 'this'");
        return;
    case E_NULL:
    case E_COALESCE:
    case E_IS:
    case E_TRY:
    case E_DEFAULTED:
        refuse(em, e->at, "the GPU has no values that may be missing, and nothing fails there: handle it on the CPU");
        return;
    case E_AWAIT:
        refuse(em, e->at, "the GPU doesn't wait: tasks are the CPU's");
        return;
    }
}

// ---------------------------------------------------------------------------
// Statements

static void line(emit *em)
{
    for (int i = 0; i < em->indent; i++) sb_put(em->o, "    ");
}

// Whether nothing after `s` in its block runs: it's dropped, as WGSL warns
// about code that can't be reached.
static bool ends(const stmt *s)
{
    switch (s->kind) {
    case S_RETURN:
    case S_BREAK:
    case S_CONTINUE:
        return true;
    case S_BLOCK:
        for (int i = 0; i < s->stmts.count; i++) {
            if (ends(s->stmts.items[i])) return true;
        }
        return false;
    case S_IF:
        return s->else_stmt && ends(s->then_stmt) && ends(s->else_stmt);
    case S_EXPR: // pixel.Discard()
        return s->value->kind == E_METHOD && s->value->call == CALL_DISCARD;
    default:
        return false;
    }
}

static bool returns(const stmt *s)
{
    switch (s->kind) {
    case S_RETURN:
        return true;
    case S_BLOCK:
        for (int i = 0; i < s->stmts.count; i++) {
            if (returns(s->stmts.items[i])) return true;
        }
        return false;
    case S_IF:
        return s->else_stmt && returns(s->then_stmt) && returns(s->else_stmt);
    default:
        return false;
    }
}

static void st_body(emit *em, const stmt *s)
{
    sb_put(em->o, "{\n");
    em->indent++;
    if (s->kind == S_BLOCK) {
        for (int i = 0; i < s->stmts.count; i++) {
            st(em, s->stmts.items[i]);
            if (ends(s->stmts.items[i])) break;
        }
    } else {
        st(em, s);
    }
    em->indent--;
    line(em);
    sb_put(em->o, "}");
}

static void st_var(emit *em, const stmt *s)
{
    if (!type_fits(em, s->type, s->name_at.line ? s->name_at : s->at)) return;
    if (em->l == WGSL) sb_printf(em->o, "var %s: %s = ", local_name(s->name), ty(em, s->type));
    else sb_printf(em->o, "%s %s = ", ty(em, s->type), local_name(s->name));
    if (s->value) ex_as(em, s->value, s->type);
    else put_default(em, s->type);
}

static tok_kind plain_op(const tok_kind op)
{
    switch (op) {
    case T_PLUS_ASSIGN:
    case T_PLUS_PLUS: return T_PLUS;
    case T_MINUS_ASSIGN:
    case T_MINUS_MINUS: return T_MINUS;
    case T_STAR_ASSIGN: return T_STAR;
    case T_SLASH_ASSIGN: return T_SLASH;
    case T_PERCENT_ASSIGN: return T_PERCENT;
    case T_SHL_ASSIGN: return T_SHL;
    case T_SHR_ASSIGN: return T_SHR;
    case T_AMP_ASSIGN: return T_AMP;
    case T_PIPE_ASSIGN: return T_PIPE;
    default: return T_CARET;
    }
}

// The value an assignment stores: its own for '=', or the target's combined with it.
static void put_assigned(emit *em, const stmt *s)
{
    const type t = s->target->type;
    if (s->op == T_ASSIGN) {
        ex_as(em, s->value, t);
    } else if (s->operator_decl) {
        ex_user_call(em, &(expr){.kind = E_CALL, .at = s->at, .method = s->operator_decl, .call = CALL_FUNCTION,
                                 .args = {(expr *[]){s->target, s->value}, 2, 2}});
    } else {
        put_binary(em, plain_op(s->op), s->target, s->value, t, s->at);
    }
}

// An assignment: a statement, or a for's step, which takes no ';' of its own.
static void st_assign(emit *em, const stmt *s, const bool statement)
{
    const expr *target = s->target;
    if (!type_fits(em, target->type, target->at)) return;
    // WGSL assigns one component at a time, not a swizzle of several
    if (em->l == WGSL && target->kind == E_MEMBER && target->swizzle_len > 1) {
        const int temp = em->temps++;
        sb_printf(em->o, "{ var tide_set%d: %s = ", temp, ty(em, target->type));
        put_assigned(em, s);
        sb_put(em->o, ";");
        for (int i = 0; i < target->swizzle_len; i++) {
            sb_put(em->o, " ");
            ex(em, target->object);
            sb_printf(em->o, ".%c = tide_set%d.%c;", "xyzw"[target->swizzle[i]], temp, "xyzw"[i]);
        }
        sb_put(em->o, statement ? " }\n" : " }");
        return;
    }
    ex(em, target);
    sb_put(em->o, " = ");
    put_assigned(em, s);
    if (statement) sb_put(em->o, ";\n");
}

// A call that's a statement: in WGSL, its mut arguments that aren't whole
// variables go in through copies and come back out after it.
static void st_call(emit *em, const stmt *s)
{
    const expr *e = s->value;
    const bool user = (e->call == CALL_FUNCTION || e->call == CALL_METHOD) && e->method;
    const decl *fn = user ? e->method : NULL;
    const bool method = fn && fn->owner && !fn->is_operator && !fn->is_interpolate;
    const expr *receiver = method && fn->is_mut_method && e->kind == E_METHOD && !is_whole_variable(e->object) ? e->object : NULL;
    bool copies = receiver != NULL;
    for (int i = 0; fn && i < e->args.count && i < fn->params.count; i++) {
        copies |= fn->params.items[i].mode == PARAM_MUT && !is_whole_variable(e->args.items[i]);
    }
    em->statement = e;
    if (em->l != WGSL || !copies) {
        if (em->l == WGSL && !user) sb_put(em->o, "_ = "); // A value WGSL won't let go unused
        ex(em, e);
        sb_put(em->o, ";\n");
        em->statement = NULL;
        return;
    }
    sb_put(em->o, "{\n");
    em->indent++;
    if (receiver) {
        line(em);
        sb_printf(em->o, "var %s: %s = ", copy_name(-1), ty(em, receiver->type));
        ex(em, receiver);
        sb_put(em->o, ";\n");
    }
    for (int i = 0; i < e->args.count && i < fn->params.count; i++) {
        if (fn->params.items[i].mode != PARAM_MUT || is_whole_variable(e->args.items[i])) continue;
        line(em);
        sb_printf(em->o, "var %s: %s = ", copy_name(i), ty(em, e->args.items[i]->type));
        ex(em, e->args.items[i]);
        sb_put(em->o, ";\n");
    }
    line(em);
    ex(em, e);
    sb_put(em->o, ";\n");
    em->statement = NULL;
    // Back out, through an assignment each, so a swizzle is set as WGSL can
    for (int i = -1; i < e->args.count && i < fn->params.count; i++) {
        const expr *to = i < 0 ? receiver : e->args.items[i];
        if (!to || (i >= 0 && (fn->params.items[i].mode != PARAM_MUT || is_whole_variable(to)))) continue;
        expr copy = {.kind = E_NAME, .at = to->at, .type = to->type, .bind = BIND_LOCAL};
        stmt local = {.kind = S_VAR, .type = to->type};
        char name[32];
        if (i < 0) snprintf(name, sizeof name, "tide_self_copy");
        else snprintf(name, sizeof name, "tide_copy%d", i);
        local.name = str_from(name);
        copy.local = &local;
        const stmt back = {.kind = S_ASSIGN, .at = to->at, .target = (expr *)to, .op = T_ASSIGN, .value = &copy};
        line(em);
        st_assign(em, &back, true);
    }
    em->indent--;
    line(em);
    sb_put(em->o, "}\n");
}

// Whether a for counts: a variable of its own, compared with a bound, that
// only its step changes.
static bool counts(const stmt *s)
{
    const stmt *v = s->init;
    if (!v || v->kind != S_VAR || v->type.kind != TY_INT || v->is_mut || !v->value) return false;
    const expr *c = s->cond;
    if (!c || c->kind != E_BINARY || (c->op != T_LT && c->op != T_LE && c->op != T_GT && c->op != T_GE)) return false;
    const bool left = c->lhs->kind == E_NAME && c->lhs->bind == BIND_LOCAL && c->lhs->local == v;
    const bool right = c->rhs->kind == E_NAME && c->rhs->bind == BIND_LOCAL && c->rhs->local == v;
    if (!left && !right) return false;
    const stmt *step = s->step;
    return step && step->kind == S_ASSIGN && step->target->kind == E_NAME && step->target->bind == BIND_LOCAL
        && step->target->local == v && step->op != T_ASSIGN;
}

static void st(emit *em, const stmt *s)
{
    if (s->kind == S_BLOCK) {
        line(em);
        st_body(em, s);
        sb_put(em->o, "\n");
        return;
    }
    line(em);
    switch (s->kind) {
    case S_VAR:
        st_var(em, s);
        sb_put(em->o, ";\n");
        break;
    case S_ASSIGN:
        st_assign(em, s, true);
        break;
    case S_EXPR:
        if (s->value->kind == E_METHOD && s->value->call == CALL_DISCARD) {
            if (em->code != em->root) refuse(em, s->at, "a pixel is left undrawn in the frag itself, not in a function it calls");
            sb_put(em->o, em->l == WGSL ? "discard;\n" : "discard;\n");
            if (em->l == WGSL) { // WGSL goes on running a discarded pixel's code
                line(em);
                sb_put(em->o, "return;\n");
            }
        } else {
            st_call(em, s);
        }
        break;
    case S_IF:
        sb_put(em->o, "if (");
        ex(em, s->cond);
        sb_put(em->o, ") ");
        st_body(em, s->then_stmt);
        if (s->else_stmt) {
            sb_put(em->o, " else ");
            st_body(em, s->else_stmt);
        }
        sb_put(em->o, "\n");
        break;
    case S_RETURN:
        if (s->value && em->code->return_type.kind != TY_VOID) {
            sb_put(em->o, "return ");
            ex_as(em, s->value, em->code->return_type);
            sb_put(em->o, ";\n");
        } else {
            sb_put(em->o, "return;\n");
        }
        break;
    case S_FOR:
        if (!counts(s)) {
            refuse(em, s->at, "a loop the GPU runs counts, so it's known how far it goes: a variable of its own, compared "
                              "with its bound, that only its step changes");
            diag_note("like 'for (int i = 0; i < 8; i++) { ... }'");
            break;
        }
        sb_put(em->o, "for (");
        st_var(em, s->init);
        sb_put(em->o, "; ");
        ex(em, s->cond);
        sb_put(em->o, "; ");
        st_assign(em, s->step, false);
        sb_put(em->o, ") ");
        st_body(em, s->then_stmt);
        sb_put(em->o, "\n");
        break;
    case S_BREAK:
        sb_put(em->o, "break;\n");
        break;
    case S_CONTINUE:
        sb_put(em->o, "continue;\n");
        break;
    case S_WHILE:
        refuse(em, s->at, "a loop the GPU runs counts, so it's known how far it goes: 'while' could go on forever");
        diag_note("write it as a for with a bound, and 'break' out of it: 'for (int i = 0; i < 8; i++) { ... }'");
        break;
    case S_FOREACH:
    case S_PARALLEL:
        refuse(em, s->at, "the GPU has no lists or grids to go through: it reads fields of numbers, and samples a grid of "
                          "colors with 'grid.Sample(uv)'");
        break;
    case S_SWITCH:
        refuse(em, s->at, "'switch' isn't in code the GPU runs yet: write it as 'if' and 'else if'");
        break;
    case S_FAIL:
        refuse(em, s->at, "nothing fails on the GPU: handle errors on the CPU");
        break;
    case S_BLOCK:
        break;
    }
}

// ---------------------------------------------------------------------------
// Functions

// A function or method the code calls: written before its caller, once.
// While checking, its body is checked once, whichever frag or vert reaches it
// first, and a function that reaches itself is refused.
static void need_function(emit *em, decl *fn, const loc at)
{
    if (em->checking) {
        if (fn->gpu_state == 1) {
            refuse(em, at, "'" STR_FMT "' calls itself, and the GPU has no recursion", STR_ARG(fn->name));
            diag_note("write it as a loop that counts: 'for (int i = 0; i < 8; i++) { ... }'");
            return;
        }
        if (fn->gpu_state == 3) em->failed = true;
        if (fn->gpu_state != 0) return;
    }
    for (int i = 0; i < em->done.count; i++) {
        if (em->done.items[i] == fn) return;
    }
    vec_push(em->done, fn);
    const char *why = fn->is_extern        ? "it's C, which runs on the CPU"
                      : fn->is_async       ? "it's a task, which the CPU runs between ticks"
                      : fn->takes_action   ? "it takes an Action, code of its caller's"
                      : fn->fails.kind != TY_VOID && fn->fails.kind != TY_ERROR ? "it can fail, and nothing fails on the GPU"
                      : fn->uses_this      ? "it uses 'this', and the GPU has no entities"
                                           : NULL;
    if (why) {
        refuse(em, at, "the GPU can't run '" STR_FMT "': %s", STR_ARG(fn->name), why);
        return;
    }
    // Its body, with what it calls before it
    sb body = {0};
    sb *const out = em->o;
    decl *const code = em->code;
    const int indent = em->indent, defuse = em->defuse;
    const expr *statement = em->statement;
    const bool failed = em->failed;
    em->o = &body;
    em->code = fn;
    em->indent = 1;
    em->defuse = 0;
    em->statement = NULL;
    em->failed = false;
    fn->gpu_state = em->checking ? 1 : fn->gpu_state;

    const bool wgsl = em->l == WGSL;
    const bool method = fn->owner && !fn->is_operator && !fn->is_interpolate;
    const bool shader = fn->kind == DECL_SHADER;
    sb head = {0};
    em->o = &head;
    const bool value = fn->return_type.kind != TY_VOID && fn->return_type.kind != TY_ERROR;
    if (value && !type_fits(em, fn->return_type, fn->at)) em->failed = true;
    if (wgsl) sb_printf(&head, "fn %s(", function_name(fn));
    else sb_printf(&head, "%s %s(", value ? ty(em, fn->return_type) : "void", function_name(fn));
    bool first = true;
    if (method) {
        const type self = {fn->owner->kind == DECL_COMPONENT ? TY_COMPONENT : TY_STRUCT, fn->owner};
        if (type_fits(em, self, fn->at)) {
            if (wgsl) sb_printf(&head, fn->is_mut_method ? "tide_self: ptr<function, %s>" : "tide_self: %s", ty(em, self));
            else sb_printf(&head, "%s%s tide_self", fn->is_mut_method ? "inout " : "", ty(em, self));
        }
        first = false;
    }
    for (int i = 0; i < fn->params.count; i++) {
        const param *p = &fn->params.items[i];
        if (shader && !p->function_param) continue; // The engine supplies it
        if (!type_fits(em, p->type, p->type_at.line ? p->type_at : p->at)) continue;
        if (!first) sb_put(&head, ", ");
        first = false;
        const bool mut = p->mode == PARAM_MUT;
        if (wgsl) sb_printf(&head, mut ? "%s: ptr<function, %s>" : "%s: %s", local_name(p->name), ty(em, p->type));
        else sb_printf(&head, "%s%s %s", mut ? "inout " : "", ty(em, p->type), local_name(p->name));
    }
    if (wgsl && value) sb_printf(&head, ") -> %s {\n", ty(em, fn->return_type));
    else sb_put(&head, ") {\n");

    em->o = &body;
    for (int i = 0; fn->body && i < fn->body->stmts.count; i++) {
        st(em, fn->body->stmts.items[i]);
        if (ends(fn->body->stmts.items[i])) break;
    }
    if (value && fn->body && !returns(fn->body)) { // The checker knows it never gets here; the shader's compiler doesn't
        line(em);
        sb_put(em->o, "return ");
        put_zero(em, fn->return_type);
        sb_put(em->o, ";\n");
    }
    if (em->checking) fn->gpu_state = em->failed ? 3 : 2;
    const bool bad = em->failed;
    em->o = out;
    em->code = code;
    em->indent = indent;
    em->defuse = defuse;
    em->statement = statement;
    em->failed = failed || bad;
    sb_put(&em->functions, head.data);
    if (body.data) sb_put(&em->functions, body.data);
    sb_put(&em->functions, "}\n\n");
}

// ---------------------------------------------------------------------------
// A shader's sources

static int packs(const int words)
{
    return (words + 3) / 4;
}

static void put_helpers(const emit *em, sb *o)
{
    const bool wgsl = em->l == WGSL;
    if (em->helpers & HELP_F) sb_put(o, wgsl ? "fn tide_f(x: f32) -> f32 { return x; }\n" : "float tide_f(float x) { return x; }\n");
    if (em->helpers & HELP_I) sb_put(o, wgsl ? "fn tide_i(x: i32) -> i32 { return x; }\n" : "int tide_i(int x) { return x; }\n");
    for (int n = 2; n <= 4; n++) {
        if (!(em->helpers & ((unsigned)HELP_NORMALIZE << (n - 2)))) continue;
        if (wgsl) {
            sb_printf(o, "fn tide_normalize%d(v: vec%df) -> vec%df {\n    let l = dot(v, v);\n    if (l > 1.17549435e-38) { return v * "
                         "inverseSqrt(l); }\n    return vec%df();\n}\n", n, n, n, n);
        } else {
            sb_printf(o, "vec%d tide_normalize%d(vec%d v) {\n    float l = dot(v, v);\n    if (l > 1.17549435e-38) { return v * "
                         "inversesqrt(l); }\n    return vec%d(0.0);\n}\n", n, n, n, n);
        }
    }
}

static void put_structs(emit *em, sb *o)
{
    const bool wgsl = em->l == WGSL;
    for (int i = 0; i < em->structs.count; i++) {
        const decl *d = em->structs.items[i];
        sb_printf(o, "struct %s {\n", struct_name(d));
        for (int k = 0; k < d->fields.count; k++) {
            const field *f = &d->fields.items[k];
            if (f->hidden) continue;
            if (wgsl) sb_printf(o, "    %s: %s,\n", field_name(f), ty(em, f->type));
            else sb_printf(o, "    %s %s;\n", ty(em, f->type), field_name(f));
        }
        sb_put(o, wgsl ? "}\n" : "};\n");
    }
}

// The uniform block's members, the same in both stages and both languages:
// the engine's own, then the game's float4s and int4s.
static void put_uniforms(const shader *sh, const lang l, sb *o)
{
    const bool wgsl = l == WGSL;
    sb_put(o, wgsl ? "struct tide_uniforms {\n    tide_camera: mat4x4f,\n    tide_screen: vec4f,\n"
                   : "layout(std140) uniform uniforms {\n    mat4 tide_camera;\n    vec4 tide_screen;\n");
    for (int i = 0; i < packs(sh->uniform_floats); i++) sb_printf(o, wgsl ? "    tide_u%d: vec4f,\n" : "    vec4 tide_u%d;\n", i);
    for (int i = 0; i < packs(sh->uniform_ints); i++) sb_printf(o, wgsl ? "    tide_v%d: vec4i,\n" : "    ivec4 tide_v%d;\n", i);
    sb_put(o, wgsl ? "}\n@group(0) @binding(0) var<uniform> tide_block: tide_uniforms;\n" : "};\n");
    for (int i = 0; i < sh->textures.count; i++) {
        if (wgsl) {
            sb_printf(o, "@group(0) @binding(%d) var pixels%d: texture_2d<f32>;\n@group(0) @binding(%d) var sampler%d: sampler;\n",
                      1 + 2 * i, i, 2 + 2 * i, i);
        } else {
            sb_printf(o, "uniform sampler2D pixels%d;\n", i);
        }
    }
}

static const field *field_called(const decl *d, const char *name)
{
    for (int i = 0; i < d->fields.count; i++) {
        if (str_eq_c(d->fields.items[i].name, name)) return &d->fields.items[i];
    }
    return NULL;
}

// What the vertex stage passes on to the pixels, as each language declares
// it: the vertex's fields the pixels read, where they are in the world, and
// the instance's float4s and int4s, which don't blend.
static void put_varyings(emit *em, sb *o, const char *direction)
{
    const shader *sh = em->sh;
    const bool wgsl = em->l == WGSL;
    int location = 0;
    for (int i = 0; i < sh->vertex->fields.count; i++) {
        if (!sh->frag_fields[i]) continue;
        const char *t = ty(em, as_float(sh->vertex->fields.items[i].type));
        if (wgsl) sb_printf(o, "    @location(%d) tide_s%d: %s,\n", location++, i, t);
        else sb_printf(o, "%s %s tide_s%d;\n", direction, t, i);
    }
    if (sh->frag_world) {
        if (wgsl) sb_printf(o, "    @location(%d) tide_world: vec3f,\n", location++);
        else sb_printf(o, "%s vec3 tide_world;\n", direction);
    }
    for (int i = 0; i < packs(sh->frag_floats); i++) {
        if (wgsl) sb_printf(o, "    @location(%d) @interpolate(flat) tide_f%d: vec4f,\n", location++, i);
        else sb_printf(o, "flat %s vec4 tide_f%d;\n", direction, i);
    }
    for (int i = 0; i < packs(sh->frag_ints); i++) {
        if (wgsl) sb_printf(o, "    @location(%d) @interpolate(flat) tide_g%d: vec4i,\n", location++, i);
        else sb_printf(o, "flat %s ivec4 tide_g%d;\n", direction, i);
    }
}

// Where word `word` of what the pixels get of the instance comes from in the
// instance itself.
static int frag_word_source(const shader *sh, const int word, const bool ints)
{
    for (int i = 0; i < sh->values.count; i++) {
        const shader_value *v = &sh->values.items[i];
        if (!v->per_instance || !v->frag_reads) continue;
        const int at = ints ? v->frag_int_at : v->frag_float_at, count = ints ? v->ints : v->floats;
        if (word >= at && word < at + count) return (ints ? v->int_at : v->float_at) + word - at;
    }
    return -1;
}

// The vertex stage's own code: the vertex from its attributes, through the
// vert, placed by its instance's transform and the camera, and what goes on
// to the pixels. `out` is how the language names what it writes: "" for
// GLSL's globals, "out." for WGSL's struct.
static void put_vertex_main(emit *em, sb *o, const char *in, const char *out)
{
    const shader *sh = em->sh;
    const bool wgsl = em->l == WGSL;
    const decl *v = sh->vertex;
    const type vertex = {TY_STRUCT, (decl *)v};
    const char *f4 = float_ty(em, 4);
    em->stage = STAGE_VERT;
    em->o = o;
    sb_printf(o, wgsl ? "    var tide_vertex: %s = %s(" : "    %s tide_vertex = %s(", ty(em, vertex), ty(em, vertex));
    for (int i = 0; i < v->fields.count; i++) sb_printf(o, "%s%stide_a%d", i ? ", " : "", in, i);
    sb_put(o, ");\n");
    if (sh->vert) sb_printf(o, "    %s(%stide_vertex);\n", function_name(sh->vert), wgsl ? "&" : "");
    const char *position = field_name(field_called(v, "position"));
    if (sh->dims == 3) {
        sb_printf(o, wgsl ? "    let tide_place = " : "    vec4 tide_place = ");
        put_bound(em, NULL, NULL, (type){TY_FLOAT4X4, NULL});
        sb_printf(o, " * %s(tide_vertex.%s, 1.0);\n", f4, position);
    } else {
        sb_printf(o, wgsl ? "    let tide_place = %s(tide_vertex.%s, 0.0, 1.0);\n" : "    vec4 tide_place = %s(tide_vertex.%s, 0.0, 1.0);\n",
                  f4, position);
    }
    if (wgsl) { // The camera's clip space is OpenGL's: depth from -1
        sb_put(o, "    let tide_clip = tide_block.tide_camera * tide_place;\n    var out: tide_corner;\n"
                  "    out.tide_at = vec4f(tide_clip.xy, (tide_clip.z + tide_clip.w) * 0.5, tide_clip.w);\n");
    } else {
        sb_put(o, "    gl_Position = tide_camera * tide_place;\n");
    }
    for (int i = 0; i < v->fields.count; i++) {
        if (sh->frag_fields[i]) sb_printf(o, "    %stide_s%d = tide_vertex.%s;\n", out, i, field_name(&v->fields.items[i]));
    }
    if (sh->frag_world) sb_printf(o, "    %stide_world = tide_place.xyz;\n", out);
    for (int ints = 0; ints < 2; ints++) {
        const int words = ints ? sh->frag_ints : sh->frag_floats;
        for (int p = 0; p < packs(words); p++) {
            sb_printf(o, "    %stide_%c%d = %s(", out, ints ? 'g' : 'f', p, ty(em, vector_type(!ints, 4)));
            for (int w = p * 4; w < p * 4 + 4; w++) {
                const int from = w < words ? frag_word_source(sh, w, ints) : -1;
                if (w > p * 4) sb_put(o, ", ");
                if (from < 0) sb_put(o, ints ? "0" : "0.0");
                else put_word(em, ints ? 'j' : 'n', from);
            }
            sb_put(o, ");\n");
        }
    }
}

// The fragment stage's own: the vertex as the pixel sees it, blended, then
// the frag, or with none, the vertex's color times the texture.
static void put_fragment_main(emit *em, sb *o, const char *in, const char *result)
{
    const shader *sh = em->sh;
    const bool wgsl = em->l == WGSL;
    const decl *v = sh->vertex;
    const char *f4 = float_ty(em, 4);
    em->stage = STAGE_FRAG;
    em->o = o;
    sb_printf(o, wgsl ? "    var tide_color: vec4f = %s(1.0, 1.0, 1.0, 1.0);\n" : "    vec4 tide_color = %s(1.0, 1.0, 1.0, 1.0);\n", f4);
    if (sh->frag) {
        const type vertex = {TY_STRUCT, (decl *)v};
        sb_printf(o, "    %s(%s(", function_name(sh->frag), ty(em, vertex));
        for (int i = 0; i < v->fields.count; i++) {
            if (i) sb_put(o, ", ");
            if (sh->frag_fields[i]) sb_printf(o, "%stide_s%d", in, i);
            else put_zero(em, v->fields.items[i].type);
        }
        sb_printf(o, "), %stide_color);\n", wgsl ? "&" : "");
    } else {
        const field *color = field_called(v, "color");
        const field *uv = field_called(v, "uv");
        if (color) sb_printf(o, "    tide_color = %stide_s%d;\n", in, field_index(v, color));
        int texture = 0;
        while (texture < sh->textures.count && (sh->textures.items[texture].source || sh->textures.items[texture].direct)) texture++;
        if (sh->textured && wgsl) {
            sb_printf(o, "    tide_color = tide_color * textureSampleLevel(pixels%d, sampler%d, %stide_s%d, 0.0);\n",
                      texture, texture, in, field_index(v, uv));
        } else if (sh->textured) {
            sb_printf(o, "    tide_color = tide_color * textureLod(pixels%d, %stide_s%d, 0.0);\n", texture, in, field_index(v, uv));
        }
    }
    sb_printf(o, "    %s tide_color;\n", result);
}

// The private variables WGSL's functions read their stage's inputs through,
// which the stage sets as it starts: GLSL's are globals already.
static void put_wgsl_inputs(const shader *sh, sb *o)
{
    for (int i = 0; i < packs(sh->instance_floats); i++) sb_printf(o, "var<private> tide_n%d: vec4f;\n", i);
    for (int i = 0; i < packs(sh->instance_ints); i++) sb_printf(o, "var<private> tide_j%d: vec4i;\n", i);
    for (int i = 0; i < packs(sh->frag_floats); i++) sb_printf(o, "var<private> tide_f%d: vec4f;\n", i);
    for (int i = 0; i < packs(sh->frag_ints); i++) sb_printf(o, "var<private> tide_g%d: vec4i;\n", i);
    sb_put(o, "var<private> tide_at: vec4f;\nvar<private> tide_world: vec3f;\n");
}

static emit emitter(program *prog, const shader *sh, const lang l)
{
    return (emit){.prog = prog, .sh = sh, .l = l};
}

// The functions a stage's code needs, into em->functions.
static void write_stage(emit *em, decl *root, const int stage)
{
    if (!root) return;
    em->root = em->code = root;
    em->stage = stage;
    need_function(em, root, root->at);
}

static const char *attribute_type(emit *em, const type t)
{
    return ty(em, t.kind == TY_COLOR ? (type){TY_FLOAT4, NULL} : t);
}

static void write_glsl(program *prog, shader *sh)
{
    for (int stage = STAGE_VERT; stage <= STAGE_FRAG; stage++) {
        emit em = emitter(prog, sh, GLSL);
        sb main = {0};
        if (stage == STAGE_VERT) {
            write_stage(&em, sh->vert, STAGE_VERT);
            put_vertex_main(&em, &main, "", "");
        } else {
            write_stage(&em, sh->frag, STAGE_FRAG);
            put_fragment_main(&em, &main, "", "tide_pixel =");
        }
        sb o = {0};
        put_uniforms(sh, GLSL, &o);
        if (stage == STAGE_VERT) {
            int location = 0;
            for (int i = 0; i < sh->vertex->fields.count; i++) {
                sb_printf(&o, "layout(location = %d) in %s tide_a%d;\n", location++, attribute_type(&em, sh->vertex->fields.items[i].type), i);
            }
            for (int i = 0; i < packs(sh->instance_floats); i++) sb_printf(&o, "layout(location = %d) in vec4 tide_n%d;\n", location++, i);
            for (int i = 0; i < packs(sh->instance_ints); i++) sb_printf(&o, "layout(location = %d) in ivec4 tide_j%d;\n", location++, i);
            put_varyings(&em, &o, "out");
        } else {
            put_varyings(&em, &o, "in");
            sb_put(&o, "out vec4 tide_pixel;\n");
        }
        put_structs(&em, &o);
        put_helpers(&em, &o);
        if (em.functions.data) sb_put(&o, em.functions.data);
        sb_printf(&o, "void main() {\n%s}\n", main.data);
        if (stage == STAGE_VERT) sh->glsl_vertex = o.data;
        else sh->glsl_fragment = o.data;
    }
}

static void write_wgsl(program *prog, shader *sh)
{
    emit em = emitter(prog, sh, WGSL);
    sb vertex = {0}, fragment = {0};
    write_stage(&em, sh->vert, STAGE_VERT);
    put_vertex_main(&em, &vertex, "", "out.");
    write_stage(&em, sh->frag, STAGE_FRAG);
    put_fragment_main(&em, &fragment, "in.", "return");

    sb o = {0};
    put_uniforms(sh, WGSL, &o);
    put_wgsl_inputs(sh, &o);
    sb_put(&o, "struct tide_corner {\n    @builtin(position) tide_at: vec4f,\n");
    put_varyings(&em, &o, "");
    sb_put(&o, "}\n");
    put_structs(&em, &o);
    put_helpers(&em, &o);
    if (em.functions.data) sb_put(&o, em.functions.data);

    sb_put(&o, "@vertex fn vertex(");
    int location = 0;
    for (int i = 0; i < sh->vertex->fields.count; i++) {
        sb_printf(&o, "%s@location(%d) tide_a%d: %s", location ? ", " : "", location, i, attribute_type(&em, sh->vertex->fields.items[i].type));
        location++;
    }
    for (int i = 0; i < packs(sh->instance_floats); i++, location++) sb_printf(&o, ", @location(%d) tide_in%d: vec4f", location, i);
    for (int i = 0; i < packs(sh->instance_ints); i++, location++) sb_printf(&o, ", @location(%d) tide_ij%d: vec4i", location, i);
    sb_put(&o, ") -> tide_corner {\n");
    for (int i = 0; i < packs(sh->instance_floats); i++) sb_printf(&o, "    tide_n%d = tide_in%d;\n", i, i);
    for (int i = 0; i < packs(sh->instance_ints); i++) sb_printf(&o, "    tide_j%d = tide_ij%d;\n", i, i);
    sb_printf(&o, "%s    return out;\n}\n", vertex.data);

    sb_put(&o, "@fragment fn fragment(in: tide_corner) -> @location(0) vec4f {\n    tide_at = in.tide_at;\n");
    if (sh->frag_world) sb_put(&o, "    tide_world = in.tide_world;\n");
    for (int i = 0; i < packs(sh->frag_floats); i++) sb_printf(&o, "    tide_f%d = in.tide_f%d;\n", i, i);
    for (int i = 0; i < packs(sh->frag_ints); i++) sb_printf(&o, "    tide_g%d = in.tide_g%d;\n", i, i);
    sb_printf(&o, "%s}\n", fragment.data);
    sh->wgsl = o.data;
}

// ---------------------------------------------------------------------------
// Planning: what each mesh draw draws through

// Checks a frag's or vert's code for the GPU, and notes what it reads.
static bool check_shader(program *prog, decl *d)
{
    for (int i = 0; i < d->params.count; i++) {
        param *p = &d->params.items[i];
        const bool fields = p->type.kind == TY_STRUCT || p->type.kind == TY_COMPONENT || p->type.kind == TY_SINGLETON
                         || p->type.kind == TY_RECORD;
        if (fields && !p->reads) p->reads = arena_alloc((size_t)(p->type.decl->fields.count + 1) * sizeof(bool));
        const decl *part = fields && p->type.kind != TY_RECORD ? p->type.decl->local_part : NULL;
        if (part && !p->local_reads) p->local_reads = arena_alloc((size_t)(part->fields.count + 1) * sizeof(bool));
    }
    emit em = emitter(prog, NULL, GLSL);
    em.checking = true;
    em.root = em.code = d;
    sb scratch = {0};
    em.o = &scratch;
    need_function(&em, d, d->at);
    return d->gpu_state == 2;
}

static shader_value *add_value(shader *sh, const decl *source, const field *f, const type t, const bool per_instance)
{
    for (int i = 0; i < sh->values.count; i++) {
        if (sh->values.items[i].source == source && sh->values.items[i].field == f) return &sh->values.items[i];
    }
    shader_value v = {.source = source, .field = f, .per_instance = per_instance};
    type_words(t, &v.floats, &v.ints);
    vec_push(sh->values, v);
    return &sh->values.items[sh->values.count - 1];
}

static void add_bound(shader *sh, decl *d)
{
    for (int i = 0; i < sh->bound.count; i++) {
        if (sh->bound.items[i] == d) return;
    }
    vec_push(sh->bound, d);
}

// What a frag or vert reads of the components and singletons it takes, and
// the grids it samples, into the shader.
static void plan_reads(const program *prog, shader *sh, const decl *code)
{
    if (!code) return;
    for (int i = 1; i < code->params.count; i++) {
        const param *p = &code->params.items[i];
        if (p->type.decl == prog->instance && p->reads && p->reads[0] && sh->dims == 3) {
            shader_value *v = add_value(sh, NULL, NULL, (type){TY_FLOAT4X4, NULL}, true);
            if (code->is_frag) v->frag_reads = true;
        }
        if (p->type.decl == prog->pixel && p->reads && p->reads[2]) sh->frag_world = true;
        const bool explicit_instance = p->type.kind == TY_STRUCT && p->type.decl == sh->instance_type;
        if (p->type.kind != TY_COMPONENT && p->type.kind != TY_SINGLETON && !explicit_instance) continue;
        for (int k = 0; k < p->type.decl->fields.count; k++) {
            const field *f = &p->type.decl->fields.items[k];
            if (f->hidden || !(p->whole || (p->reads && p->reads[k]))) continue;
            if (f->type.kind == TY_GRID || f->type.kind == TY_TEXTURE) continue; // Sampled, below
            shader_value *v = add_value(sh, p->type.decl, f, f->type, p->type.kind == TY_COMPONENT || explicit_instance);
            if (code->is_frag) v->frag_reads = true;
            else v->vert_reads = true;
            if (!explicit_instance) add_bound(sh, p->type.decl);
        }
        // Its local fields, which this machine keeps beside the match's
        const decl *part = p->type.decl->local_part;
        for (int k = 0; part && p->local_reads && k < part->fields.count; k++) {
            const field *f = &part->fields.items[k];
            if (!p->local_reads[k] || f->type.kind == TY_GRID || f->type.kind == TY_TEXTURE) continue;
            shader_value *v = add_value(sh, p->type.decl, f, f->type, p->type.kind == TY_COMPONENT);
            if (code->is_frag) v->frag_reads = true;
            else v->vert_reads = true;
            add_bound(sh, p->type.decl);
        }
    }
    for (int i = 0; i < code->samples.count; i++) {
        const shader_sample *s = &code->samples.items[i];
        const decl *source = code->params.items[s->param].type.decl;
        const shader_texture t = {.source = source,
                                  .field = s->field < 0 ? NULL : &(s->local ? source->local_part : source)->fields.items[s->field],
                                  .blended = s->blended, .direct = s->field < 0};
        bool known = false;
        for (int k = 0; k < sh->textures.count; k++) {
            known |= sh->textures.items[k].direct == t.direct && sh->textures.items[k].source == t.source && sh->textures.items[k].field == t.field
                  && sh->textures.items[k].blended == t.blended;
        }
        if (!known) vec_push(sh->textures, t);
        if (source) add_bound(sh, (decl *)source);
    }
}

bool shader_reads_local(const shader *sh, const decl *d)
{
    for (int i = 0; i < sh->values.count; i++) {
        if (sh->values.items[i].source == d && sh->values.items[i].field && sh->values.items[i].field->is_local) return true;
    }
    for (int i = 0; i < sh->textures.count; i++) {
        if (sh->textures.items[i].source == d && sh->textures.items[i].field->is_local) return true;
    }
    return false;
}

static void list_bound(const shader *sh, sb *out, const bool per_instance)
{
    int n = 0;
    if (per_instance && sh->instance_type) {
        sb_printf(out, STR_FMT, STR_ARG(sh->instance_type->name));
        n++;
    }
    for (int i = 0; i < sh->bound.count; i++) {
        const decl *d = sh->bound.items[i];
        if ((d->kind == DECL_COMPONENT) != per_instance) continue;
        sb_printf(out, "%s" STR_FMT, n++ ? ", " : "", STR_ARG(d->name));
    }
}

// The shader a mesh draw draws through: the one planned already for the same
// vertex, vert and frag, or a new one, laid out and written. NULL after an
// error, reported at the call.
static shader *plan(program *prog, const expr *call, const decl *vertex, const bool textured, const bool direct_texture)
{
    const bool source_grid = call->blit && call->args.items[0]->type.kind == TY_GRID;
    for (int i = 0; i < prog->shaders.count; i++) {
        shader *s = prog->shaders.items[i];
        if (s->vertex == vertex && s->vert == call->vert && s->frag == call->frag && s->textured == textured
            && s->instanced == call->instanced && s->instance_type == call->instance_type
            && s->blit == call->blit && s->direct_texture == direct_texture && s->source_grid == source_grid) return s;
    }
    shader *sh = NEW(shader);
    sh->index = prog->shaders.count;
    sh->vertex = (decl *)vertex;
    sh->vert = call->vert;
    sh->frag = call->frag;
    sh->textured = textured;
    sh->blit = call->blit;
    sh->direct_texture = direct_texture;
    sh->source_grid = source_grid;
    sh->instanced = call->instanced;
    sh->instance_type = call->instance_type;
    const field *position = field_called(vertex, "position");
    sh->dims = position->type.kind == TY_FLOAT3 ? 3 : 2;
    if (sh->instance_type && sh->dims == 3) sh->instance_transform = field_called(sh->instance_type, "transform");
    sb name = {0};
    if (sh->vert) sb_printf(&name, STR_FMT "%s", STR_ARG(sh->vert->name), sh->frag ? " and " : " ");
    if (sh->frag) sb_printf(&name, STR_FMT " ", STR_ARG(sh->frag->name));
    sb_printf(&name, "%s" STR_FMT, sh->vert || sh->frag ? "on " : "meshes of ", STR_ARG(vertex->name));
    sh->name = name.data;

    // What goes with each instance and each draw: the transform first
    if (sh->dims == 3) add_value(sh, NULL, NULL, (type){TY_FLOAT4X4, NULL}, true)->vert_reads = true;
    plan_reads(prog, sh, sh->vert);
    plan_reads(prog, sh, sh->frag);
    if (textured) {
        const shader_texture t = {.blended = true}; // How it's read is the draw's to say
        vec_push(sh->textures, t);
    }
    for (int i = 0; i < sh->values.count; i++) {
        shader_value *v = &sh->values.items[i];
        int *floats = v->per_instance ? &sh->instance_floats : &sh->uniform_floats;
        int *ints = v->per_instance ? &sh->instance_ints : &sh->uniform_ints;
        v->float_at = *floats;
        v->int_at = *ints;
        *floats += v->floats;
        *ints += v->ints;
        if (v->per_instance && v->frag_reads) {
            v->frag_float_at = sh->frag_floats;
            v->frag_int_at = sh->frag_ints;
            sh->frag_floats += v->floats;
            sh->frag_ints += v->ints;
        }
    }
    // What the pixels read of the vertex: the frag's fields, or with none, its color and its uv
    sh->frag_fields = arena_alloc((size_t)(vertex->fields.count + 1) * sizeof(bool));
    int varyings = (sh->frag_world ? 1 : 0) + packs(sh->frag_floats) + packs(sh->frag_ints);
    for (int i = 0; i < vertex->fields.count; i++) {
        const field *f = &vertex->fields.items[i];
        if (sh->frag) {
            const param *p = &sh->frag->params.items[0];
            sh->frag_fields[i] = (p->whole || (p->reads && p->reads[i])) && !type_is_int_based(f->type);
        } else {
            sh->frag_fields[i] = str_eq_c(f->name, "color") || (textured && str_eq_c(f->name, "uv"));
        }
        varyings += sh->frag_fields[i];
    }

    // What a GPU takes, on every backend
    const int attributes = vertex->fields.count + packs(sh->instance_floats) + packs(sh->instance_ints);
    const int uniform_bytes = 16 * (packs(sh->uniform_floats) + packs(sh->uniform_ints));
    if (attributes > 16) {
        sb from = {0};
        list_bound(sh, &from, true);
        diag_error(call->at, "this draw reads %d values for each vertex, and a GPU takes 16: %d fields of " STR_FMT "%s, and %d "
                   "float4s and int4s of %s", attributes, vertex->fields.count, STR_ARG(vertex->name),
                   sh->dims == 3 ? ", 4 for the transform" : "", attributes - vertex->fields.count - (sh->dims == 3 ? 4 : 0),
                   from.data ? from.data : "its entity");
        diag_note("a component's fields are sent for each entity drawn, 4 floats or 4 ints to a value: read fewer of them, or "
                  "keep what every entity shares in a singleton, which is sent once for the draw");
        return NULL;
    }
    if (varyings > 15) {
        diag_error(call->at, "this draw's pixels read %d values from the mesh's corners, and a GPU carries 15 between them",
                   varyings);
        diag_note("each field of the vertex the frag reads is one, its place in the world another, and every 4 floats or 4 ints "
                  "it reads of the entity's components another: read fewer, or keep what every entity shares in a singleton");
        return NULL;
    }
    if (uniform_bytes > 4000) {
        sb from = {0};
        list_bound(sh, &from, false);
        diag_error(call->at, "this draw reads %d bytes of singletons (%s), and a draw's hold 4000", uniform_bytes,
                   from.data ? from.data : "");
        diag_note("read fewer of their fields in the frag and the vert: only what they read is sent");
        return NULL;
    }
    if (sh->textures.count > 8) {
        diag_error(call->at, "this draw samples %d grids, and a draw takes 8", sh->textures.count);
        diag_note("a grid sampled with both filters counts twice; put several images side by side in one grid");
        return NULL;
    }
    write_glsl(prog, sh);
    write_wgsl(prog, sh);
    vec_push(prog->shaders, sh);
    return sh;
}

void plan_shaders(program *prog)
{
    // Every frag and vert, whether a draw names it or not
    bool ok = true;
    for (int i = 0; i < prog->decls.count; i++) {
        decl *d = prog->decls.items[i];
        if (d->kind == DECL_SHADER) ok &= check_shader(prog, d);
    }
    if (!ok || diag_error_count() > 0) return;
    for (int i = 0; i < prog->mesh_draws.count; i++) {
        const mesh_draw *draw = &prog->mesh_draws.items[i];
        expr *call = draw->call;
        const decl *vertex = call->blit ? prog->vertex : call->args.items[0]->type.decl->fields.items[0].type.decl;
        // A texture is the value argument after the indices, or after a 3D mesh's transform
        bool textured = false, direct_texture = call->blit;
        for (int a = 2; !call->blit && a < call->args.count; a++) {
            const type_kind kind = call->args.items[a]->type.kind;
            textured |= !call->frag && (kind == TY_GRID || kind == TY_TEXTURE);
            direct_texture |= kind == TY_TEXTURE;
        }
        shader *sh = plan(prog, call, vertex, textured, direct_texture);
        if (!sh) continue;
        call->shader = sh;
        // What the view has to hand over that it doesn't take itself
        decl *view = draw->in;
        for (int b = 0; b < sh->bound.count; b++) {
            decl *d = sh->bound.items[b];
            bool has = false;
            for (int k = 0; k < view->params.count; k++) {
                param *p = &view->params.items[k];
                const bool takes = p->type.decl == d && (p->type.kind == TY_COMPONENT || p->type.kind == TY_SINGLETON)
                                && (p->mode == PARAM_READ || p->mode == PARAM_MUT);
                if (takes && shader_reads_local(sh, d)) p->local_use = true; // Its local fields come with it
                has |= takes;
            }
            for (int k = 0; k < view->bound.count; k++) has |= view->bound.items[k] == d;
            if (!has) vec_push(view->bound, d);
        }
    }
}

void print_shaders(const program *prog, sb *out)
{
    for (int i = 0; i < prog->shaders.count; i++) {
        const shader *sh = prog->shaders.items[i];
        sb_printf(out, "// %s: GLSL, vertex\n%s\n// %s: GLSL, fragment\n%s\n// %s: WGSL\n%s\n", sh->name, sh->glsl_vertex,
                  sh->name, sh->glsl_fragment, sh->name, sh->wgsl);
    }
    if (prog->shaders.count == 0) sb_put(out, "// No mesh draw draws through a shader of the game's.\n");
}
