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
// quaternions, matrices, colors and structs of those, locals, `if`, loops
// that count, and sampling a grid of colors. `Math` is all there, and what
// builds its types: what a shader language has no function for, or has one
// that does otherwise than tide/math.h's, is written out (helper_text).
// Nothing of it is left out but for a reason, which its error says: a long's
// hash, as these GPUs have no 64-bit integers.
//
// The same code that writes a shader checks it: a first pass over each frag
// and vert, and the functions they reach, writes nothing and reports what
// doesn't fit (`checking`). What's
// written afterwards always compiles, so nobody sees a shader compiler's
// error: every name of the game's gets a prefix no keyword has, conversions
// are written out, WGSL's rules for pointers, swizzles and constant
// expressions are worked around here.
//
// What the code reads is what's sent: a component's fields for each
// instance, as float4 and int4 vertex attributes, and a singleton's for each
// draw, as float4s and int4s of one uniform block. A value is words of those,
// floats and ints apart (value_text), so both languages lay them out alike.
//
// What each writes is an `out` parameter: a frag's color, and the structs a
// vert writes for the frag it's drawn with (shader_handed), whose fields the
// frag reads go from the mesh's corners to its pixels as its vertex's do,
// blended. The checker saw each written on every path (assigned.c).

typedef enum lang { GLSL, WGSL } lang;

enum { STAGE_VERT, STAGE_FRAG };

// Functions of the engine's own a shader may need defined before the game's:
// what Math does that the shader languages don't, or don't do as the CPU
// does (helper_text). Each comes after the ones it calls.
enum {
    FAMILY_MIN, FAMILY_MAX, FAMILY_CLAMP, FAMILY_SATURATE, FAMILY_SMOOTHSTEP, FAMILY_POW, FAMILY_ATAN2, FAMILY_COUNT
};

enum {
    H_F,   // tide_f(x): x, a float the compiler can't fold
    H_I,   // tide_i(x): the same for an int
    H_NAN, // tide_nan(x): whether x is no number
    H_FAMILIES, // tide_min<n> and the other families, for n = 1 to 4, a family after another: family_helper
    H_NORMALIZE2 = H_FAMILIES + 4 * FAMILY_COUNT, H_NORMALIZE3, H_NORMALIZE4,
    H_XXH, H_HASH1, H_HASH2, H_HASH3, H_HASH4,
    H_FADE, H_GRAD2, H_GRAD3, H_PERLIN2, H_PERLIN3, H_SIMPLEX2, H_SIMPLEX3, H_NEAREST, H_CELLULAR2, H_CELLULAR3, // noise.c's
    H_NORMALIZE_Q, H_MUL_Q, H_ROTATE_Q, H_INVERSE_Q, H_CONJUGATE_Q, H_NLERP_Q, H_SLERP_Q, H_ANGLE_Q, H_AXISANGLE_Q, H_EULER_Q,
    H_Q_FROM_M3, H_LOOKROTATION_Q, H_M3_FROM_Q, H_M4_FROM_M3, H_TRS, H_PERSPECTIVE, H_ORTHO,
    H_INVERSE2, H_INVERSE3, H_INVERSE4,
    H_STRAIGHT, // tide_straight(c): a texture's pixel, which has its alpha worked into its color, as the color it is
    H_COUNT
};

// A list or a grid in code the GPU runs: which of the shader's textures of
// numbers it is (data_of).
typedef struct data_ref {
    int texture; // -1 while checking, when no shader says
    type element;
    bool grid;
} data_ref;

typedef struct data_alias {
    const stmt *local;
    data_ref data;
} data_alias;

typedef struct shader_function {
    const decl *fn;
    const char *suffix;
} shader_function;

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
    // What a frag works out as it starts, before any branch: for each Sample that may read between a texture's
    // smaller images, where it reads and how fast that changes from one pixel to the next (ex_sample)
    sb grads;
    int grad_count;
    int lifting; // Writing one of those: a local is written as what it was declared as
    VEC(const decl *) structs; // The structs the code names, each after the ones it holds
    VEC(shader_function) done; // The functions written, callees first, each for the lists and grids it's called with
    sb functions;
    bool helpers[H_COUNT];
    bool lists[8]; // The shader's textures that are lists or grids whose reader is written (need_data)
    VEC(data_alias) aliases;   // The locals of the code being written that name a list or a grid (data_of)
    const data_ref *spec;      // By the parameter of the function being written: the list or grid it's called with
    VEC(const decl *) arrays;  // The array types whose element reader is written (need_array)
    int raw; // Writing what an assignment stores into: an array's element is the element itself, in range already
    VEC(const field *) hoisted; // Arrays the engine supplies that the function being written made a variable of as it starts
} emit;

static void ex(emit *em, const expr *e);
static void ex_as(emit *em, const expr *e, type want);
static void st(emit *em, const stmt *s);
static void need_function(emit *em, decl *fn, loc at, const data_ref *spec, const char *suffix);
static bool is_const(const expr *e);
static int packs(int words);

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
    if (!fn->owner && fn->c_suffix) { // One of several that share the name: its parameters' types go on, as in generated C
        sb b = {0};
        sb_printf(&b, "%s%s", name_of(fn->kind == DECL_SHADER ? "ts_" : "tf_", fn->qualified.len ? fn->qualified : fn->name),
                  name_of("_3", str_from(fn->c_suffix)));
        return b.data;
    }
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
    if (t.kind == TY_BYTE) { // An int there, from 0 to 255
        *ints += 1;
        return true;
    }
    if (t.kind == TY_ARRAY) { // Its elements, one after the other. Neither GLSL has arrays of arrays
        const type element = t.decl->fields.items[0].type;
        int f = 0, i = 0;
        if (element.kind == TY_ARRAY || !type_words(element, &f, &i)) return false;
        *floats += f * t.decl->array_length;
        *ints += i * t.decl->array_length;
        return true;
    }
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
    if (t.kind == TY_STRUCT && t.decl == em->prog->random) { // Its state is a long, and the code that steps it the CPU's
        refuse(em, at, "a Random can't go to the GPU: it keeps its state where the CPU steps it");
        diag_note("roll in the view or a system, and keep the number in a field the GPU reads");
        return false;
    }
    switch (t.kind) {
    case TY_LONG: why = "the GPU's ints are 32 bits: convert it with int(x) before it gets there"; break;
    case TY_STRING: why = "text is the CPU's"; break;
    case TY_LIST:
        refuse(em, at, "a list in code the GPU runs is one the frag or vert was given, by a name: it can't be made, changed, "
                       "returned, picked between or kept in a struct there");
        diag_note("name a field of a component, a singleton or the draw's values, or an asset: 'var lights = scene.lights;', "
                  "or pass it to a function that takes a 'List<Light>'");
        return false;
    case TY_MAP:
    case TY_SET: why = "the GPU has no maps or sets: keep what it reads in a list, which it reads by an element's place"; break;
    case TY_GRID:
        refuse(em, at, "a grid in code the GPU runs is one the frag or vert was given, by a name: it can't be made, changed, "
                       "returned, picked between or kept in a struct there");
        diag_note("name a field of a component or a singleton, or an asset: 'var cells = board.cells;', or pass it to a function "
                  "that takes a 'Grid2<int>'");
        return false;
    case TY_ARRAY:
        why = t.decl->fields.items[0].type.kind == TY_ARRAY
            ? "the GPU has no arrays of arrays: keep the rows side by side in one array, or each row in a struct"
            : "an array the GPU has is of bools, ints, floats, vectors, matrices, colors or structs of those";
        break;
    case TY_TEXTURE: why = "a texture is sampled with 'texture.Sample(uv)', not kept as a GPU value"; break;
    case TY_DEPTH_TEXTURE:
        why = "a target's depth is read with 'depth.Sample(uv)' or 'depth.Compare(uv, value)', not kept as a GPU value";
        break;
    case TY_CUBEMAP: why = "a cube is read with 'cube.Sample(direction)', not kept as a GPU value"; break;
    case TY_RENDER_TARGET: why = "a render target belongs to the CPU: sample its color as a Texture"; break;
    case TY_RESIDENT: why = "resident state belongs to the CPU: the C of the package that keeps it reads it"; break;
    case TY_ENTITY:
    case TY_LOCAL_ENTITY:
    case TY_PLAYER: why = "it's a handle into the world, which the GPU doesn't have"; break;
    case TY_RECT: why = "pass its x, y, width and height as a float4"; break;
    case TY_OPTIONAL:
    case TY_FAILABLE: why = "the GPU has no values that may be missing: handle it on the CPU"; break;
    case TY_RECORD:
        if (t.decl == em->prog->pixel) {
            refuse(em, at, "a frag's pixel is read through 'this.position', 'this.depth' and 'this.world': it's not a value "
                          "to keep or pass on");
            return false;
        }
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
    if (t.kind == TY_BYTE) return wgsl ? "i32" : "int";
    const int dim = type_dim(t);
    if (dim > 0) return type_is_float_based(t) ? (wgsl ? wgsl_f : glsl_f)[dim] : (wgsl ? wgsl_i : glsl_i)[dim];
    if (matrix_dim(t)) return (wgsl ? wgsl_m : glsl_m)[matrix_dim(t)];
    if (t.kind == TY_COLOR || t.kind == TY_QUATERNION) return wgsl ? "vec4f" : "vec4";
    if (t.kind == TY_BOOL) return "bool";
    if (t.kind == TY_ENUM) return wgsl ? "i32" : "int";
    if (t.kind == TY_ARRAY) {
        sb b = {0};
        sb_printf(&b, wgsl ? "array<%s, %d>" : "%s[%d]", ty(em, t.decl->fields.items[0].type), t.decl->array_length);
        return b.data;
    }
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
        em->helpers[H_F] = true;
        sb_printf(em->o, "tide_f(%s)", text);
    } else {
        sb_put(em->o, text);
    }
}

static void put_int(emit *em, const long long value)
{
    if (em->defuse > 0) {
        em->helpers[H_I] = true;
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
static void put_zeros(emit *em, type t);

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
    if (t.kind == TY_ARRAY) { // Each element, as zeros
        sb_printf(em->o, "%s(", ty(em, t));
        for (int i = 0; i < t.decl->array_length; i++) {
            if (i) sb_put(em->o, ", ");
            put_zeros(em, t.decl->fields.items[0].type);
        }
        sb_put(em->o, ")");
        return;
    }
    if (t.kind == TY_BOOL) sb_put(em->o, "false");
    else if (t.kind == TY_INT || t.kind == TY_ENUM || t.kind == TY_BYTE) sb_put(em->o, "0");
    else if (t.kind == TY_FLOAT) sb_put(em->o, "0.0");
    else if (type_is_int_based(t)) sb_printf(em->o, "%s(0)", ty(em, t));
    else sb_printf(em->o, "%s(0.0)", ty(em, t));
}

// ---------------------------------------------------------------------------
// The engine's own functions
//
// What Math does that a shader language doesn't, or doesn't do as the CPU
// does: tide/math.h's formulas, in its order, so the same code gives the same
// answer on the CPU and on the GPU up to the GPU's rounding, and Math.Hash,
// which is integers, the same number. Each is written once, in the GLSL both
// languages can say, which put_helper turns into WGSL: a function's head on a
// line of its own, a statement a line, a local declared with its type and a
// value, no '?:', no parameter assigned to, and atan2 for the atan of two.

static const char *const family_names[FAMILY_COUNT] = {"min", "max", "clamp", "saturate", "smoothstep", "pow", "atan2_"};
static const int family_args[FAMILY_COUNT] = {2, 2, 3, 1, 3, 2, 2};

// A family's function of one number: its vectors go component by component.
static const char *const family_scalars[FAMILY_COUNT] = {
    // With one NaN, the other. Each is asked, as a compiler that reckons without NaN may make the comparison its own
    // min or max, which says nothing of which it gives then
    "float tide_min1(float a, float b) {\n"
    "    if (tide_nan(b)) { return a; }\n"
    "    if (tide_nan(a)) { return b; }\n"
    "    if (a < b) { return a; }\n"
    "    return b;\n"
    "}\n",
    "float tide_max1(float a, float b) {\n"
    "    if (tide_nan(b)) { return a; }\n"
    "    if (tide_nan(a)) { return b; }\n"
    "    if (a > b) { return a; }\n"
    "    return b;\n"
    "}\n",
    // Always within its bounds: the lower one for NaN, and for bounds the wrong way around
    "float tide_clamp1(float a, float b, float c) {\n"
    "    if (tide_nan(a)) { return b; }\n"
    "    return tide_max1(b, tide_min1(c, a));\n"
    "}\n",
    // 0 for NaN. Past that the languages' own clamp says what it does, between bounds the right way around
    "float tide_saturate1(float a) {\n"
    "    if (tide_nan(a)) { return 0.0; }\n"
    "    return clamp(a, 0.0, 1.0);\n"
    "}\n",
    // The languages' own is undefined from an edge down to a lower one
    "float tide_smoothstep1(float a, float b, float c) {\n"
    "    float t = tide_saturate1((c - a) / (b - a));\n"
    "    return t * t * (3.0 - 2.0 * t);\n"
    "}\n",
    // The languages' own is undefined under 0: a whole power of a number under 0 is that of the number over 0, with its sign
    "float tide_pow1(float a, float b) {\n"
    "    if (b == 0.0) { return 1.0; }\n"
    "    bool w = a < 0.0 && floor(b) == b;\n"
    "    float x = a;\n"
    "    if (w) { x = -a; }\n"
    "    float p = pow(x, b);\n"
    "    if (w && floor(b * 0.5) * 2.0 != b) { return -p; }\n"
    "    return p;\n"
    "}\n",
    // The languages' own is undefined at (0, 0)
    "float tide_atan2_1(float a, float b) {\n"
    "    if (a == 0.0 && b == 0.0) { return 0.0; }\n"
    "    return atan2(a, b);\n"
    "}\n",
};

static const char *const helper_texts[H_COUNT] = {
    // A texture holds colors times their alpha, so that what's blended between two pixels is right (a clear
    // pixel's color counts for nothing). Code reads the color itself. A pixel with no alpha has none
    [H_STRAIGHT] = "vec4 tide_straight(vec4 c) {\n"
                   "    if (c.a == 0.0) { return vec4(0.0); }\n"
                   "    return vec4(c.rgb / c.a, c.a);\n"
                   "}\n",
    [H_F] = "float tide_f(float x) {\n"
            "    return x;\n"
            "}\n",
    [H_I] = "int tide_i(int x) {\n"
            "    return x;\n"
            "}\n",
    // By its bits: a compiler may take x != x for false, as the languages let it reckon without NaN
    [H_NAN] = "bool tide_nan(float x) {\n"
              "    return (floatBitsToInt(x) & 0x7fffffff) > 0x7f800000;\n"
              "}\n",
    // xxHash32 (tide/math.h): a word of an input under 16 bytes, a lane of a 16 byte stripe, and the end
    [H_XXH] = "uint tide_xxh_word(uint h, int w) {\n"
              "    uint k = h + uint(w) * 0xC2B2AE3Du;\n"
              "    return ((k << 17) | (k >> 15)) * 0x27D4EB2Fu;\n"
              "}\n"
              "uint tide_xxh_lane(uint h, int w) {\n"
              "    uint k = h + uint(w) * 0x85EBCA77u;\n"
              "    return ((k << 13) | (k >> 19)) * 0x9E3779B1u;\n"
              "}\n"
              "int tide_xxh_end(uint h) {\n"
              "    uint k = h ^ (h >> 15);\n"
              "    k = k * 0x85EBCA77u;\n"
              "    k = k ^ (k >> 13);\n"
              "    k = k * 0xC2B2AE3Du;\n"
              "    k = k ^ (k >> 16);\n"
              "    return int(k & 0x7FFFFFFFu);\n"
              "}\n",
    [H_HASH1] = "int tide_hash1(int v) {\n"
                "    return tide_xxh_end(tide_xxh_word(0x165667B5u, v));\n"
                "}\n",
    [H_HASH2] = "int tide_hash2(ivec2 v) {\n"
                "    return tide_xxh_end(tide_xxh_word(tide_xxh_word(0x165667B9u, v.x), v.y));\n"
                "}\n",
    [H_HASH3] = "int tide_hash3(ivec3 v) {\n"
                "    return tide_xxh_end(tide_xxh_word(tide_xxh_word(tide_xxh_word(0x165667BDu, v.x), v.y), v.z));\n"
                "}\n",
    [H_HASH4] = "int tide_hash4(ivec4 v) {\n"
                "    uint a = tide_xxh_lane(0x24234428u, v.x);\n"
                "    uint b = tide_xxh_lane(0x85EBCA77u, v.y);\n"
                "    uint c = tide_xxh_lane(0u, v.z);\n"
                "    uint d = tide_xxh_lane(0x61C8864Fu, v.w);\n"
                "    return tide_xxh_end(((a << 1) | (a >> 31)) + ((b << 7) | (b >> 25)) + ((c << 12) | (c >> 20)) + "
                "((d << 18) | (d >> 14)) + 16u);\n"
                "}\n",
    // Noise (engine/src/noise.c), the same lines as the CPU's, so a frag draws what the match computed, up to the
    // GPU's rounding. A cell's gradient, or its feature point, is the hash of the cell and the seed, as there
    [H_FADE] = "float tide_fade(float t) {\n"
               "    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);\n"
               "}\n",
    // A unit gradient in 8 directions, the axes and the diagonals, dotted with (x, y)
    [H_GRAD2] = "float tide_grad2(int h, float x, float y) {\n"
                "    float gx = 1.0;\n"
                "    float gy = 0.0;\n"
                "    if ((h & 4) != 0) {\n"
                "        gx = 0.70710678;\n"
                "        gy = 0.70710678;\n"
                "        if ((h & 1) != 0) { gx = -0.70710678; }\n"
                "        if ((h & 2) != 0) { gy = -0.70710678; }\n"
                "    } else if ((h & 2) != 0) {\n"
                "        gx = 0.0;\n"
                "        gy = 1.0;\n"
                "        if ((h & 1) != 0) { gy = -1.0; }\n"
                "    } else if ((h & 1) != 0) {\n"
                "        gx = -1.0;\n"
                "    }\n"
                "    return gx * x + gy * y;\n"
                "}\n",
    // A gradient along one of a cube's 12 edges (Perlin's 16, four of them twice), dotted with (x, y, z)
    [H_GRAD3] = "float tide_grad3(int h, float x, float y, float z) {\n"
                "    int k = h & 15;\n"
                "    float u = x;\n"
                "    float v = y;\n"
                "    if (k >= 8) { u = y; }\n"
                "    if (k >= 4) { v = z; }\n"
                "    if (k == 12 || k == 14) { v = x; }\n"
                "    if ((k & 1) != 0) { u = -u; }\n"
                "    if ((k & 2) != 0) { v = -v; }\n"
                "    return u + v;\n"
                "}\n",
    [H_PERLIN2] = "float tide_perlin2(vec2 p, int seed) {\n"
                  "    vec2 f = floor(p);\n"
                  "    ivec2 i = ivec2(f);\n"
                  "    ivec2 j = i + 1;\n"
                  "    float x = p.x - f.x;\n"
                  "    float y = p.y - f.y;\n"
                  "    float u = tide_fade(x);\n"
                  "    float v = tide_fade(y);\n"
                  "    float n00 = tide_grad2(tide_hash3(ivec3(i.x, i.y, seed)), x, y);\n"
                  "    float n10 = tide_grad2(tide_hash3(ivec3(j.x, i.y, seed)), x - 1.0, y);\n"
                  "    float n01 = tide_grad2(tide_hash3(ivec3(i.x, j.y, seed)), x, y - 1.0);\n"
                  "    float n11 = tide_grad2(tide_hash3(ivec3(j.x, j.y, seed)), x - 1.0, y - 1.0);\n"
                  "    float a = n00 + u * (n10 - n00);\n"
                  "    float b = n01 + u * (n11 - n01);\n"
                  "    return (a + v * (b - a)) * 1.41421356;\n"
                  "}\n",
    [H_PERLIN3] = "float tide_perlin3(vec3 p, int seed) {\n"
                  "    vec3 f = floor(p);\n"
                  "    ivec3 i = ivec3(f);\n"
                  "    ivec3 j = i + 1;\n"
                  "    float x = p.x - f.x;\n"
                  "    float y = p.y - f.y;\n"
                  "    float z = p.z - f.z;\n"
                  "    float u = tide_fade(x);\n"
                  "    float v = tide_fade(y);\n"
                  "    float w = tide_fade(z);\n"
                  "    float n000 = tide_grad3(tide_hash4(ivec4(i.x, i.y, i.z, seed)), x, y, z);\n"
                  "    float n100 = tide_grad3(tide_hash4(ivec4(j.x, i.y, i.z, seed)), x - 1.0, y, z);\n"
                  "    float n010 = tide_grad3(tide_hash4(ivec4(i.x, j.y, i.z, seed)), x, y - 1.0, z);\n"
                  "    float n110 = tide_grad3(tide_hash4(ivec4(j.x, j.y, i.z, seed)), x - 1.0, y - 1.0, z);\n"
                  "    float n001 = tide_grad3(tide_hash4(ivec4(i.x, i.y, j.z, seed)), x, y, z - 1.0);\n"
                  "    float n101 = tide_grad3(tide_hash4(ivec4(j.x, i.y, j.z, seed)), x - 1.0, y, z - 1.0);\n"
                  "    float n011 = tide_grad3(tide_hash4(ivec4(i.x, j.y, j.z, seed)), x, y - 1.0, z - 1.0);\n"
                  "    float n111 = tide_grad3(tide_hash4(ivec4(j.x, j.y, j.z, seed)), x - 1.0, y - 1.0, z - 1.0);\n"
                  "    float a0 = n000 + u * (n100 - n000);\n"
                  "    float a1 = n010 + u * (n110 - n010);\n"
                  "    float b0 = n001 + u * (n101 - n001);\n"
                  "    float b1 = n011 + u * (n111 - n011);\n"
                  "    float a = a0 + v * (a1 - a0);\n"
                  "    float b = b0 + v * (b1 - b0);\n"
                  "    return a + w * (b - a);\n"
                  "}\n",
    // A simplex's corner: its bump, times its gradient dotted with the offset from it
    [H_SIMPLEX2] = "float tide_corner2(int h, float x, float y) {\n"
                   "    float t = 0.5 - x * x - y * y;\n"
                   "    if (t < 0.0) { return 0.0; }\n"
                   "    float tt = t * t;\n"
                   "    return tt * tt * tide_grad2(h, x, y);\n"
                   "}\n"
                   "float tide_simplex2(vec2 p, int seed) {\n"
                   "    float s = (p.x + p.y) * 0.36602540;\n"
                   "    float fi = floor(p.x + s);\n"
                   "    float fj = floor(p.y + s);\n"
                   "    float t = (fi + fj) * 0.21132487;\n"
                   "    float x0 = p.x - (fi - t);\n"
                   "    float y0 = p.y - (fj - t);\n"
                   "    int i1 = 0;\n"
                   "    int j1 = 1;\n"
                   "    if (x0 > y0) {\n"
                   "        i1 = 1;\n"
                   "        j1 = 0;\n"
                   "    }\n"
                   "    float x1 = x0 - float(i1) + 0.21132487;\n"
                   "    float y1 = y0 - float(j1) + 0.21132487;\n"
                   "    float x2 = x0 - 1.0 + 0.42264973;\n"
                   "    float y2 = y0 - 1.0 + 0.42264973;\n"
                   "    int i = int(fi);\n"
                   "    int j = int(fj);\n"
                   "    float n0 = tide_corner2(tide_hash3(ivec3(i, j, seed)), x0, y0);\n"
                   "    float n1 = tide_corner2(tide_hash3(ivec3(i + i1, j + j1, seed)), x1, y1);\n"
                   "    float n2 = tide_corner2(tide_hash3(ivec3(i + 1, j + 1, seed)), x2, y2);\n"
                   "    return (n0 + n1 + n2) * 99.2;\n"
                   "}\n",
    [H_SIMPLEX3] = "float tide_corner3(int h, float x, float y, float z) {\n"
                   "    float t = 0.6 - x * x - y * y - z * z;\n"
                   "    if (t < 0.0) { return 0.0; }\n"
                   "    float tt = t * t;\n"
                   "    return tt * tt * tide_grad3(h, x, y, z);\n"
                   "}\n"
                   "float tide_simplex3(vec3 p, int seed) {\n"
                   "    float s = (p.x + p.y + p.z) * 0.33333334;\n"
                   "    float fi = floor(p.x + s);\n"
                   "    float fj = floor(p.y + s);\n"
                   "    float fk = floor(p.z + s);\n"
                   "    float t = (fi + fj + fk) * 0.16666667;\n"
                   "    float x0 = p.x - (fi - t);\n"
                   "    float y0 = p.y - (fj - t);\n"
                   "    float z0 = p.z - (fk - t);\n"
                   "    int i1 = 0;\n"
                   "    int j1 = 0;\n"
                   "    int k1 = 0;\n"
                   "    int i2 = 0;\n"
                   "    int j2 = 0;\n"
                   "    int k2 = 0;\n"
                   "    if (x0 >= y0) {\n"
                   "        if (y0 >= z0) {\n"
                   "            i1 = 1;\n"
                   "            i2 = 1;\n"
                   "            j2 = 1;\n"
                   "        } else if (x0 >= z0) {\n"
                   "            i1 = 1;\n"
                   "            i2 = 1;\n"
                   "            k2 = 1;\n"
                   "        } else {\n"
                   "            k1 = 1;\n"
                   "            i2 = 1;\n"
                   "            k2 = 1;\n"
                   "        }\n"
                   "    } else {\n"
                   "        if (y0 < z0) {\n"
                   "            k1 = 1;\n"
                   "            j2 = 1;\n"
                   "            k2 = 1;\n"
                   "        } else if (x0 < z0) {\n"
                   "            j1 = 1;\n"
                   "            j2 = 1;\n"
                   "            k2 = 1;\n"
                   "        } else {\n"
                   "            j1 = 1;\n"
                   "            i2 = 1;\n"
                   "            j2 = 1;\n"
                   "        }\n"
                   "    }\n"
                   "    float x1 = x0 - float(i1) + 0.16666667;\n"
                   "    float y1 = y0 - float(j1) + 0.16666667;\n"
                   "    float z1 = z0 - float(k1) + 0.16666667;\n"
                   "    float x2 = x0 - float(i2) + 0.33333334;\n"
                   "    float y2 = y0 - float(j2) + 0.33333334;\n"
                   "    float z2 = z0 - float(k2) + 0.33333334;\n"
                   "    float x3 = x0 - 0.5;\n"
                   "    float y3 = y0 - 0.5;\n"
                   "    float z3 = z0 - 0.5;\n"
                   "    int i = int(fi);\n"
                   "    int j = int(fj);\n"
                   "    int k = int(fk);\n"
                   "    float n0 = tide_corner3(tide_hash4(ivec4(i, j, k, seed)), x0, y0, z0);\n"
                   "    float n1 = tide_corner3(tide_hash4(ivec4(i + i1, j + j1, k + k1, seed)), x1, y1, z1);\n"
                   "    float n2 = tide_corner3(tide_hash4(ivec4(i + i2, j + j2, k + k2, seed)), x2, y2, z2);\n"
                   "    float n3 = tide_corner3(tide_hash4(ivec4(i + 1, j + 1, k + 1, seed)), x3, y3, z3);\n"
                   "    return (n0 + n1 + n2 + n3) * 32.5;\n"
                   "}\n",
    // The two shortest squared distances so far, with d among them
    [H_NEAREST] = "vec2 tide_nearest(vec2 best, float d) {\n"
                  "    if (d < best.x) { return vec2(d, best.x); }\n"
                  "    if (d < best.y) { return vec2(best.x, d); }\n"
                  "    return best;\n"
                  "}\n",
    // The squared distance to the feature point of the cell (ox, oy) over from the point's, which is at (fx, fy) in its own
    [H_CELLULAR2] = "float tide_feature2(ivec2 i, int ox, int oy, int seed, float fx, float fy) {\n"
                    "    int h = tide_hash3(ivec3(i.x + ox, i.y + oy, seed));\n"
                    "    float px = float(ox) + float(h & 32767) * 0.000030518509;\n"
                    "    float py = float(oy) + float((h >> 15) & 32767) * 0.000030518509;\n"
                    "    float dx = px - fx;\n"
                    "    float dy = py - fy;\n"
                    "    return dx * dx + dy * dy;\n"
                    "}\n"
                    "vec2 tide_cellular2(vec2 p, int seed) {\n"
                    "    vec2 f = floor(p);\n"
                    "    ivec2 i = ivec2(f);\n"
                    "    float x = p.x - f.x;\n"
                    "    float y = p.y - f.y;\n"
                    "    vec2 best = vec2(8.0, 8.0);\n"
                    "    best = tide_nearest(best, tide_feature2(i, -1, -1, seed, x, y));\n"
                    "    best = tide_nearest(best, tide_feature2(i, 0, -1, seed, x, y));\n"
                    "    best = tide_nearest(best, tide_feature2(i, 1, -1, seed, x, y));\n"
                    "    best = tide_nearest(best, tide_feature2(i, -1, 0, seed, x, y));\n"
                    "    best = tide_nearest(best, tide_feature2(i, 0, 0, seed, x, y));\n"
                    "    best = tide_nearest(best, tide_feature2(i, 1, 0, seed, x, y));\n"
                    "    best = tide_nearest(best, tide_feature2(i, -1, 1, seed, x, y));\n"
                    "    best = tide_nearest(best, tide_feature2(i, 0, 1, seed, x, y));\n"
                    "    best = tide_nearest(best, tide_feature2(i, 1, 1, seed, x, y));\n"
                    "    return vec2(sqrt(best.x), sqrt(best.y));\n"
                    "}\n",
    [H_CELLULAR3] = "float tide_feature3(ivec3 i, int ox, int oy, int oz, int seed, float fx, float fy, float fz) {\n"
                    "    int h = tide_hash4(ivec4(i.x + ox, i.y + oy, i.z + oz, seed));\n"
                    "    float px = float(ox) + float(h & 1023) * 0.00097751711;\n"
                    "    float py = float(oy) + float((h >> 10) & 1023) * 0.00097751711;\n"
                    "    float pz = float(oz) + float((h >> 20) & 1023) * 0.00097751711;\n"
                    "    float dx = px - fx;\n"
                    "    float dy = py - fy;\n"
                    "    float dz = pz - fz;\n"
                    "    return dx * dx + dy * dy + dz * dz;\n"
                    "}\n"
                    "vec2 tide_cellular3(vec3 p, int seed) {\n"
                    "    vec3 f = floor(p);\n"
                    "    ivec3 i = ivec3(f);\n"
                    "    float x = p.x - f.x;\n"
                    "    float y = p.y - f.y;\n"
                    "    float z = p.z - f.z;\n"
                    "    vec2 best = vec2(8.0, 8.0);\n"
                    "    best = tide_nearest(best, tide_feature3(i, -1, -1, -1, seed, x, y, z));\n"
                    "    best = tide_nearest(best, tide_feature3(i, 0, -1, -1, seed, x, y, z));\n"
                    "    best = tide_nearest(best, tide_feature3(i, 1, -1, -1, seed, x, y, z));\n"
                    "    best = tide_nearest(best, tide_feature3(i, -1, 0, -1, seed, x, y, z));\n"
                    "    best = tide_nearest(best, tide_feature3(i, 0, 0, -1, seed, x, y, z));\n"
                    "    best = tide_nearest(best, tide_feature3(i, 1, 0, -1, seed, x, y, z));\n"
                    "    best = tide_nearest(best, tide_feature3(i, -1, 1, -1, seed, x, y, z));\n"
                    "    best = tide_nearest(best, tide_feature3(i, 0, 1, -1, seed, x, y, z));\n"
                    "    best = tide_nearest(best, tide_feature3(i, 1, 1, -1, seed, x, y, z));\n"
                    "    best = tide_nearest(best, tide_feature3(i, -1, -1, 0, seed, x, y, z));\n"
                    "    best = tide_nearest(best, tide_feature3(i, 0, -1, 0, seed, x, y, z));\n"
                    "    best = tide_nearest(best, tide_feature3(i, 1, -1, 0, seed, x, y, z));\n"
                    "    best = tide_nearest(best, tide_feature3(i, -1, 0, 0, seed, x, y, z));\n"
                    "    best = tide_nearest(best, tide_feature3(i, 0, 0, 0, seed, x, y, z));\n"
                    "    best = tide_nearest(best, tide_feature3(i, 1, 0, 0, seed, x, y, z));\n"
                    "    best = tide_nearest(best, tide_feature3(i, -1, 1, 0, seed, x, y, z));\n"
                    "    best = tide_nearest(best, tide_feature3(i, 0, 1, 0, seed, x, y, z));\n"
                    "    best = tide_nearest(best, tide_feature3(i, 1, 1, 0, seed, x, y, z));\n"
                    "    best = tide_nearest(best, tide_feature3(i, -1, -1, 1, seed, x, y, z));\n"
                    "    best = tide_nearest(best, tide_feature3(i, 0, -1, 1, seed, x, y, z));\n"
                    "    best = tide_nearest(best, tide_feature3(i, 1, -1, 1, seed, x, y, z));\n"
                    "    best = tide_nearest(best, tide_feature3(i, -1, 0, 1, seed, x, y, z));\n"
                    "    best = tide_nearest(best, tide_feature3(i, 0, 0, 1, seed, x, y, z));\n"
                    "    best = tide_nearest(best, tide_feature3(i, 1, 0, 1, seed, x, y, z));\n"
                    "    best = tide_nearest(best, tide_feature3(i, -1, 1, 1, seed, x, y, z));\n"
                    "    best = tide_nearest(best, tide_feature3(i, 0, 1, 1, seed, x, y, z));\n"
                    "    best = tide_nearest(best, tide_feature3(i, 1, 1, 1, seed, x, y, z));\n"
                    "    return vec2(sqrt(best.x), sqrt(best.y));\n"
                    "}\n",
    // The identity for one too short, as a zeroed one is no rotation
    [H_NORMALIZE_Q] = "vec4 tide_normalize_q(vec4 q) {\n"
                      "    float l = dot(q, q);\n"
                      "    if (l > 1.17549435e-38) { return q * inversesqrt(l); }\n"
                      "    return vec4(0.0, 0.0, 0.0, 1.0);\n"
                      "}\n",
    [H_MUL_Q] = "vec4 tide_mul_q(vec4 a, vec4 b) {\n"
                "    return vec4(a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y + a.y * b.w + a.z * b.x - a.x * b.z, "
                "a.w * b.z + a.z * b.w + a.x * b.y - a.y * b.x, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z);\n"
                "}\n",
    [H_ROTATE_Q] = "vec3 tide_rotate_q(vec4 q, vec3 v) {\n"
                   "    vec3 t = 2.0 * cross(q.xyz, v);\n"
                   "    return v + q.w * t + cross(q.xyz, t);\n"
                   "}\n",
    [H_INVERSE_Q] = "vec4 tide_inverse_q(vec4 q) {\n"
                    "    return vec4(-q.xyz, q.w) * (1.0 / dot(q, q));\n"
                    "}\n",
    [H_CONJUGATE_Q] = "vec4 tide_conjugate_q(vec4 q) {\n"
                      "    return vec4(-q.xyz, q.w);\n"
                      "}\n",
    [H_NLERP_Q] = "vec4 tide_nlerp_q(vec4 a, vec4 b, float t) {\n"
                  "    vec4 c = b;\n"
                  "    if (dot(a, b) < 0.0) { c = -b; }\n"
                  "    return tide_normalize_q(a + t * (c - a));\n"
                  "}\n",
    [H_SLERP_Q] = "vec4 tide_slerp_q(vec4 a, vec4 b, float t) {\n"
                  "    float d = dot(a, b);\n"
                  "    vec4 c = b;\n"
                  "    if (d < 0.0) {\n"
                  "        d = -d;\n"
                  "        c = -b;\n"
                  "    }\n"
                  "    if (d < 0.9995) {\n"
                  "        float w = acos(d);\n"
                  "        float s = inversesqrt(1.0 - d * d);\n"
                  "        return a * (sin(w * (1.0 - t)) * s) + c * (sin(w * t) * s);\n"
                  "    }\n"
                  "    return tide_nlerp_q(a, c, t);\n"
                  "}\n",
    // The length is 1 at most but for the GPU's rounding, which mustn't take asin out of its range
    [H_ANGLE_Q] = "float tide_angle_q(vec4 a, vec4 b) {\n"
                  "    vec4 d = tide_normalize_q(tide_mul_q(vec4(-a.xyz, a.w), b));\n"
                  "    float w = asin(min(length(d.xyz), 1.0));\n"
                  "    return w + w;\n"
                  "}\n",
    [H_AXISANGLE_Q] = "vec4 tide_axisangle_q(vec3 v, float w) {\n"
                      "    return vec4(v * sin(0.5 * w), cos(0.5 * w));\n"
                      "}\n",
    // Z first, then X, then Y
    [H_EULER_Q] = "vec4 tide_euler_q(vec3 v) {\n"
                  "    vec3 s = sin(0.5 * v);\n"
                  "    vec3 c = cos(0.5 * v);\n"
                  "    return tide_mul_q(tide_mul_q(vec4(0.0, s.y, 0.0, c.y), vec4(s.x, 0.0, 0.0, c.x)), vec4(0.0, 0.0, s.z, c.z));\n"
                  "}\n",
    // Shepperd's method: by the largest term
    [H_Q_FROM_M3] = "vec4 tide_q_from_m3(mat3 m) {\n"
                    "    float x = m[0].x;\n"
                    "    float y = m[1].y;\n"
                    "    float z = m[2].z;\n"
                    "    float t = x + y + z;\n"
                    "    if (t > 0.0) {\n"
                    "        float s = sqrt(t + 1.0) * 2.0;\n"
                    "        return vec4((m[1].z - m[2].y) / s, (m[2].x - m[0].z) / s, (m[0].y - m[1].x) / s, 0.25 * s);\n"
                    "    }\n"
                    "    if (x > y && x > z) {\n"
                    "        float s = sqrt(1.0 + x - y - z) * 2.0;\n"
                    "        return vec4(0.25 * s, (m[1].x + m[0].y) / s, (m[2].x + m[0].z) / s, (m[1].z - m[2].y) / s);\n"
                    "    }\n"
                    "    if (y > z) {\n"
                    "        float s = sqrt(1.0 + y - x - z) * 2.0;\n"
                    "        return vec4((m[1].x + m[0].y) / s, 0.25 * s, (m[2].y + m[1].z) / s, (m[2].x - m[0].z) / s);\n"
                    "    }\n"
                    "    float s = sqrt(1.0 + z - x - y) * 2.0;\n"
                    "    return vec4((m[2].x + m[0].z) / s, (m[2].y + m[1].z) / s, 0.25 * s, (m[0].y - m[1].x) / s);\n"
                    "}\n",
    [H_LOOKROTATION_Q] = "vec4 tide_lookrotation_q(vec3 f, vec3 u) {\n"
                         "    vec3 t = tide_normalize3(cross(u, f));\n"
                         "    return tide_q_from_m3(mat3(t, cross(f, t), f));\n"
                         "}\n",
    [H_M3_FROM_Q] = "mat3 tide_m3_from_q(vec4 q) {\n"
                    "    float x = q.x + q.x;\n"
                    "    float y = q.y + q.y;\n"
                    "    float z = q.z + q.z;\n"
                    "    float xx = q.x * x;\n"
                    "    float yy = q.y * y;\n"
                    "    float zz = q.z * z;\n"
                    "    float xy = q.x * y;\n"
                    "    float xz = q.x * z;\n"
                    "    float yz = q.y * z;\n"
                    "    float wx = q.w * x;\n"
                    "    float wy = q.w * y;\n"
                    "    float wz = q.w * z;\n"
                    "    return mat3(1.0 - (yy + zz), xy + wz, xz - wy, xy - wz, 1.0 - (xx + zz), yz + wx, xz + wy, yz - wx, "
                    "1.0 - (xx + yy));\n"
                    "}\n",
    [H_M4_FROM_M3] = "mat4 tide_m4_from_m3(mat3 m, vec3 t) {\n"
                     "    return mat4(vec4(m[0], 0.0), vec4(m[1], 0.0), vec4(m[2], 0.0), vec4(t, 1.0));\n"
                     "}\n",
    [H_TRS] = "mat4 tide_trs(vec3 t, vec4 q, vec3 s) {\n"
              "    mat3 m = tide_m3_from_q(q);\n"
              "    return mat4(vec4(m[0] * s.x, 0.0), vec4(m[1] * s.y, 0.0), vec4(m[2] * s.z, 0.0), vec4(t, 1.0));\n"
              "}\n",
    // From a view down -z to OpenGL's clip space, as Unity.Mathematics': the field of view, the aspect, near and far
    [H_PERSPECTIVE] = "mat4 tide_perspective(float v, float a, float n, float f) {\n"
                      "    float c = 1.0 / tan(v * 0.5);\n"
                      "    float r = 1.0 / (n - f);\n"
                      "    return mat4(vec4(c / a, 0.0, 0.0, 0.0), vec4(0.0, c, 0.0, 0.0), vec4(0.0, 0.0, (f + n) * r, -1.0), "
                      "vec4(0.0, 0.0, 2.0 * n * f * r, 0.0));\n"
                      "}\n",
    [H_ORTHO] = "mat4 tide_ortho(float w, float h, float n, float f) {\n"
                "    float r = 1.0 / (f - n);\n"
                "    return mat4(vec4(2.0 / w, 0.0, 0.0, 0.0), vec4(0.0, 2.0 / h, 0.0, 0.0), vec4(0.0, 0.0, -2.0 * r, 0.0), "
                "vec4(0.0, 0.0, -(f + n) * r, 1.0));\n"
                "}\n",
    [H_INVERSE2] = "mat2 tide_inverse2(mat2 m) {\n"
                   "    float s = 1.0 / (m[0].x * m[1].y - m[1].x * m[0].y);\n"
                   "    return mat2(m[1].y * s, -m[0].y * s, -m[1].x * s, m[0].x * s);\n"
                   "}\n",
    // Its rows are the cross products of pairs of columns, over the determinant
    [H_INVERSE3] = "mat3 tide_inverse3(mat3 m) {\n"
                   "    vec3 a = cross(m[1], m[2]);\n"
                   "    vec3 b = cross(m[2], m[0]);\n"
                   "    vec3 c = cross(m[0], m[1]);\n"
                   "    return transpose(mat3(a, b, c)) * (1.0 / dot(m[0], a));\n"
                   "}\n",
    // By Laplace expansion over 2 by 2 determinants: a<row><column> of the matrix, and the inverse column by column
    [H_INVERSE4] = "mat4 tide_inverse4(mat4 m) {\n"
                   "    float a00 = m[0].x;\n"
                   "    float a01 = m[1].x;\n"
                   "    float a02 = m[2].x;\n"
                   "    float a03 = m[3].x;\n"
                   "    float a10 = m[0].y;\n"
                   "    float a11 = m[1].y;\n"
                   "    float a12 = m[2].y;\n"
                   "    float a13 = m[3].y;\n"
                   "    float a20 = m[0].z;\n"
                   "    float a21 = m[1].z;\n"
                   "    float a22 = m[2].z;\n"
                   "    float a23 = m[3].z;\n"
                   "    float a30 = m[0].w;\n"
                   "    float a31 = m[1].w;\n"
                   "    float a32 = m[2].w;\n"
                   "    float a33 = m[3].w;\n"
                   "    float s0 = a00 * a11 - a10 * a01;\n"
                   "    float s1 = a00 * a12 - a10 * a02;\n"
                   "    float s2 = a00 * a13 - a10 * a03;\n"
                   "    float s3 = a01 * a12 - a11 * a02;\n"
                   "    float s4 = a01 * a13 - a11 * a03;\n"
                   "    float s5 = a02 * a13 - a12 * a03;\n"
                   "    float k5 = a22 * a33 - a32 * a23;\n"
                   "    float k4 = a21 * a33 - a31 * a23;\n"
                   "    float k3 = a21 * a32 - a31 * a22;\n"
                   "    float k2 = a20 * a33 - a30 * a23;\n"
                   "    float k1 = a20 * a32 - a30 * a22;\n"
                   "    float k0 = a20 * a31 - a30 * a21;\n"
                   "    float d = 1.0 / (s0 * k5 - s1 * k4 + s2 * k3 + s3 * k2 - s4 * k1 + s5 * k0);\n"
                   "    return mat4((a11 * k5 - a12 * k4 + a13 * k3) * d, (-a10 * k5 + a12 * k2 - a13 * k1) * d, "
                   "(a10 * k4 - a11 * k2 + a13 * k0) * d, (-a10 * k3 + a11 * k1 - a12 * k0) * d, "
                   "(-a01 * k5 + a02 * k4 - a03 * k3) * d, (a00 * k5 - a02 * k2 + a03 * k1) * d, "
                   "(-a00 * k4 + a01 * k2 - a03 * k0) * d, (a00 * k3 - a01 * k1 + a02 * k0) * d, "
                   "(a31 * s5 - a32 * s4 + a33 * s3) * d, (-a30 * s5 + a32 * s2 - a33 * s1) * d, "
                   "(a30 * s4 - a31 * s2 + a33 * s0) * d, (-a30 * s3 + a31 * s1 - a32 * s0) * d, "
                   "(-a21 * s5 + a22 * s4 - a23 * s3) * d, (a20 * s5 - a22 * s2 + a23 * s1) * d, "
                   "(-a20 * s4 + a21 * s2 - a23 * s0) * d, (a20 * s3 - a21 * s1 + a22 * s0) * d);\n"
                   "}\n",
};

static int family_helper(const int family, const int dim)
{
    return H_FAMILIES + 4 * family + dim - 1;
}

// Helper `h`, as it's written once for both languages.
static void helper_text(const int h, sb *o)
{
    if (h >= H_FAMILIES && h < H_NORMALIZE2) {
        const int family = (h - H_FAMILIES) / 4, dim = (h - H_FAMILIES) % 4 + 1, args = family_args[family];
        const char *name = family_names[family];
        if (dim == 1) {
            sb_put(o, family_scalars[family]);
            return;
        }
        sb_printf(o, "vec%d tide_%s%d(", dim, name, dim);
        for (int a = 0; a < args; a++) sb_printf(o, "%svec%d %c", a ? ", " : "", dim, 'a' + a);
        sb_printf(o, ") {\n    return vec%d(", dim);
        for (int c = 0; c < dim; c++) {
            sb_printf(o, "%stide_%s1(", c ? ", " : "", name);
            for (int a = 0; a < args; a++) sb_printf(o, "%s%c.%c", a ? ", " : "", 'a' + a, "xyzw"[c]);
            sb_put(o, ")");
        }
        sb_put(o, ");\n}\n");
    } else if (h >= H_NORMALIZE2 && h <= H_NORMALIZE4) { // Zero for a vector too short, where the languages' own gives nothing sure
        const int dim = h - H_NORMALIZE2 + 2;
        sb_printf(o, "vec%d tide_normalize%d(vec%d v) {\n    float l = dot(v, v);\n    if (l > 1.17549435e-38) { return v * "
                     "inversesqrt(l); }\n    return vec%d(0.0);\n}\n", dim, dim, dim, dim);
    } else {
        sb_put(o, helper_texts[h]);
    }
}

// Notes that the shader calls helper `h`, and the helpers it calls.
static void need(emit *em, const int h)
{
    if (em->helpers[h]) return;
    em->helpers[h] = true;
    if (h >= H_FAMILIES && h < H_NORMALIZE2) {
        const int family = (h - H_FAMILIES) / 4;
        if ((h - H_FAMILIES) % 4 > 0) {
            need(em, family_helper(family, 1));
        } else if (family == FAMILY_MIN || family == FAMILY_MAX) {
            need(em, H_NAN);
        } else if (family == FAMILY_CLAMP) {
            need(em, family_helper(FAMILY_MIN, 1));
            need(em, family_helper(FAMILY_MAX, 1));
        } else if (family == FAMILY_SATURATE) {
            need(em, H_NAN);
        } else if (family == FAMILY_SMOOTHSTEP) {
            need(em, family_helper(FAMILY_SATURATE, 1));
        }
        return;
    }
    switch (h) {
    case H_HASH1:
    case H_HASH2:
    case H_HASH3:
    case H_HASH4:
        need(em, H_XXH);
        break;
    case H_PERLIN2:
        need(em, H_HASH3);
        need(em, H_FADE);
        need(em, H_GRAD2);
        break;
    case H_PERLIN3:
        need(em, H_HASH4);
        need(em, H_FADE);
        need(em, H_GRAD3);
        break;
    case H_SIMPLEX2:
        need(em, H_HASH3);
        need(em, H_GRAD2);
        break;
    case H_SIMPLEX3:
        need(em, H_HASH4);
        need(em, H_GRAD3);
        break;
    case H_CELLULAR2:
        need(em, H_HASH3);
        need(em, H_NEAREST);
        break;
    case H_CELLULAR3:
        need(em, H_HASH4);
        need(em, H_NEAREST);
        break;
    case H_NLERP_Q:
        need(em, H_NORMALIZE_Q);
        break;
    case H_SLERP_Q:
        need(em, H_NLERP_Q);
        break;
    case H_ANGLE_Q:
        need(em, H_NORMALIZE_Q);
        need(em, H_MUL_Q);
        break;
    case H_EULER_Q:
        need(em, H_MUL_Q);
        break;
    case H_LOOKROTATION_Q:
        need(em, H_NORMALIZE3);
        need(em, H_Q_FROM_M3);
        break;
    case H_TRS:
        need(em, H_M3_FROM_Q);
        break;
    default:
        break;
    }
}

static bool word_char(const char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

static bool word_is(const char *word, const size_t len, const char *other)
{
    return strlen(other) == len && memcmp(word, other, len) == 0;
}

// The type the word of `len` chars at `word` names, as WGSL spells it; NULL when it names none.
static const char *wgsl_type(const char *word, const size_t len)
{
    static const char *const types[][2] = {
        {"float", "f32"}, {"int", "i32"}, {"uint", "u32"}, {"bool", "bool"}, {"vec2", "vec2f"}, {"vec3", "vec3f"},
        {"vec4", "vec4f"}, {"ivec2", "vec2i"}, {"ivec3", "vec3i"}, {"ivec4", "vec4i"}, {"mat2", "mat2x2f"},
        {"mat3", "mat3x3f"}, {"mat4", "mat4x4f"},
    };
    for (size_t i = 0; i < sizeof types / sizeof types[0]; i++) {
        if (word_is(word, len, types[i][0])) return types[i][1];
    }
    return NULL;
}

// `len` chars of a helper, word by word, as `l` spells them.
static void put_spelled(sb *o, const lang l, const char *text, const size_t len)
{
    for (size_t i = 0; i < len;) {
        size_t n = 1;
        while (word_char(text[i]) && i + n < len && word_char(text[i + n])) n++;
        const char *as = NULL;
        if (l == GLSL) {
            if (word_is(text + i, n, "atan2")) as = "atan";
        } else if (word_is(text + i, n, "inversesqrt")) {
            as = "inverseSqrt";
        } else if (word_is(text + i, n, "floatBitsToInt")) {
            as = "bitcast<i32>";
        } else {
            as = wgsl_type(text + i, n);
        }
        if (as) sb_put(o, as);
        else sb_putn(o, text + i, n);
        i += n;
    }
}

// A helper as `l` writes it. WGSL declares the other way around: a
// function's head is `fn name(a: T) -> T`, and a local `var name: T = value`.
static void put_helper(sb *o, const lang l, const char *text)
{
    for (const char *line = text; *line;) {
        const char *end = strchr(line, '\n');
        size_t indent = 0, type_len = 0, name_len = 0;
        while (line[indent] == ' ') indent++;
        while (word_char(line[indent + type_len])) type_len++;
        // What the line declares, if it starts with a type and a name
        const char *type = l == WGSL && line[indent + type_len] == ' ' ? wgsl_type(line + indent, type_len) : NULL;
        const char *name = line + indent + type_len + 1;
        while (type && word_char(name[name_len])) name_len++;
        if (type && name_len && name[name_len] == '(') {
            sb_printf(o, "fn %.*s(", (int)name_len, name);
            for (const char *p = name + name_len + 1; *p != ')';) {
                size_t of = 0, called = 0;
                while (word_char(p[of])) of++;
                const char *param = p + of + 1;
                while (word_char(param[called])) called++;
                sb_printf(o, "%.*s: %s", (int)called, param, wgsl_type(p, of));
                p = param + called;
                if (*p == ',') {
                    sb_put(o, ", ");
                    p += 2;
                }
            }
            sb_printf(o, ") -> %s {\n", type);
        } else if (type && name_len && strncmp(name + name_len, " = ", 3) == 0) {
            const char *value = name + name_len + 3;
            sb_printf(o, "%.*svar %.*s: %s = ", (int)indent, line, (int)name_len, name, type);
            put_spelled(o, l, value, (size_t)(end - value));
            sb_put(o, "\n");
        } else {
            put_spelled(o, l, line, (size_t)(end - line));
            sb_put(o, "\n");
        }
        line = end + 1;
    }
}

static void put_helpers(const emit *em, sb *o)
{
    for (int h = 0; h < H_COUNT; h++) {
        if (!em->helpers[h]) continue;
        sb text = {0};
        helper_text(h, &text);
        put_helper(o, em->l, text.data);
    }
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
    if (t.kind == TY_BYTE) {
        put_word(em, ints, *i);
        *i += 1;
    } else if (dim > 0 && type_is_float_based(t)) {
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
    } else if (t.kind == TY_ARRAY) {
        sb_printf(em->o, "%s(", ty(em, t));
        for (int k = 0; k < t.decl->array_length; k++) {
            if (k) sb_put(em->o, ", ");
            put_value(em, t.decl->fields.items[0].type, floats, ints, f, i);
        }
        sb_put(em->o, ")");
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

// A frag's struct parameter `p` among what the shader's vert writes for its
// frag, or NULL: for one the draw's instances supply, and while checking,
// when no draw has said which it is.
static const shader_handed *handed_to(const shader *sh, const param *p)
{
    for (int i = 0; sh && i < sh->handed.count; i++) {
        if (sh->handed.items[i].to == p) return &sh->handed.items[i];
    }
    return NULL;
}

// Whether `p` is a parameter of the frag or vert that the engine supplies: a
// component, a singleton, Instance, an instance's struct or the one
// the draw gives after its vert and frag. What the
// vert wrote for the frag is a value the frag is called with, like its vertex.
static bool is_bound(const emit *em, const param *p)
{
    return em->code == em->root && !p->function_param && !handed_to(em->sh, p);
}

static int param_index(const decl *d, const param *p)
{
    return (int)(p - d->params.items);
}

// ---------------------------------------------------------------------------
// Lists
//
// A list that's a field of what the engine supplies (a component, a
// singleton, the draw's values) is read by its elements: `items[i]`,
// `items.count` and foreach. It reaches the GPU as a texture of four ints a
// pixel (READ_DATA, tide_draw_list_data), an element taking the pixels its
// words need: its floats first, by their bits, then its ints, as a value's
// words are anywhere (put_value). How many it has is a word of the uniforms'
// ints. An element past the list's end is zeros, as on the CPU.

// A list or a grid the code names is one of the shader's textures of numbers,
// known as the shader is written: a field of what the engine supplies, an
// asset by its name, a local that names one of those, or a parameter of a
// function, which is written once for each set of textures it's called with
// (need_function), as neither language passes a texture to a function on
// every backend.
static type data_element(const type t)
{
    return t.decl->fields.items[0].type;
}

// Notes that the code reads the numbers of an asset's list or grid, while
// checking: the frag or vert itself, or a function, whose callers then do.
static void note_asset(emit *em, const decl *asset)
{
    if (!em->checking) return;
    if (em->code != em->root) {
        for (int i = 0; i < em->code->gpu_data.count; i++) {
            if (em->code->gpu_data.items[i] == asset) return;
        }
        vec_push(em->code->gpu_data, asset);
        return;
    }
    const shader_sample sample = {-1, -1, false, 0, asset, 0, READ_DATA, true, true};
    for (int i = 0; i < em->root->samples.count; i++) {
        const shader_sample *s = &em->root->samples.items[i];
        if (s->param < 0 && s->asset == asset && s->read == READ_DATA) return;
    }
    vec_push(em->root->samples, sample);
}

// `e` as such a list or grid: which of the shader's textures it is, -1 while
// checking, when there's no shader, and for one that can't be read. False for
// anything else: a list that was made, picked or returned.
static bool data_of(emit *em, const expr *e, data_ref *d)
{
    if (!e || (e->type.kind != TY_LIST && e->type.kind != TY_GRID) || !e->type.decl) return false;
    *d = (data_ref){.texture = -1, .element = data_element(e->type), .grid = e->type.kind == TY_GRID};
    if (e->kind == E_NAME && e->bind == BIND_LOCAL) { // A local that names one
        for (int i = em->aliases.count - 1; i >= 0; i--) {
            if (em->aliases.items[i].local != e->local) continue;
            *d = em->aliases.items[i].data;
            return true;
        }
        return false;
    }
    if (e->kind == E_NAME && e->bind == BIND_PARAM && e->param->function_param && em->code != em->root) { // Its function's
        if (em->spec) *d = em->spec[param_index(em->code, e->param)];
        return true;
    }
    const decl *asset = NULL;
    const param *p = NULL;
    if (e->kind == E_NAME && e->bind == BIND_ASSET && e->constant) {
        asset = e->constant;
    } else if (e->kind == E_MEMBER && e->field && e->object->kind == E_NAME && e->object->bind == BIND_PARAM
               && is_bound(em, e->object->param) && is_struct_like(e->object->param->type)) {
        p = e->object->param;
    } else {
        return false;
    }
    if (d->grid && e->type.decl->dims != 2) {
        refuse(em, e->at, "a Grid3's cells aren't read on the GPU yet: keep the layer it reads in a Grid2");
        return true;
    }
    if (!type_fits(em, d->element, e->at)) return true;
    if (em->checking) {
        if (asset) {
            note_asset(em, asset);
        } else if (d->grid) {
            const decl *holds = e->local_field ? p->type.decl->local_part : p->type.decl;
            const shader_sample sample = {param_index(em->root, p), field_index(holds, e->field), e->local_field, 0, NULL, 0,
                                          READ_DATA, true, true};
            bool known = false;
            for (int i = 0; i < em->root->samples.count; i++) {
                const shader_sample *s = &em->root->samples.items[i];
                known |= s->param == sample.param && s->field == sample.field && s->local == sample.local && !s->asset
                      && s->read == READ_DATA;
            }
            if (!known) vec_push(em->root->samples, sample);
        } else if (e->local_field) {
            em->root->params.items[param_index(em->root, p)].local_reads[field_index(p->type.decl->local_part, e->field)] = true;
        } else {
            em->root->params.items[param_index(em->root, p)].reads[field_index(p->type.decl, e->field)] = true;
        }
        return true;
    }
    for (int i = 0; i < em->sh->textures.count; i++) {
        const shader_texture *t = &em->sh->textures.items[i];
        if (t->read == READ_DATA && t->asset == asset && t->source == (p ? p->type.decl : NULL) && t->field == (p ? e->field : NULL)) {
            d->texture = i;
        }
    }
    return true;
}

// All of a value as zeros, whatever its fields' defaults are.
static void put_zeros(emit *em, const type t)
{
    if (!is_struct_like(t) || em->l == WGSL) {
        if (em->l == WGSL) sb_printf(em->o, "%s()", ty(em, t));
        else put_zero(em, t);
        return;
    }
    sb_printf(em->o, "%s(", ty(em, t));
    bool first = true;
    for (int i = 0; i < t.decl->fields.count; i++) {
        if (t.decl->fields.items[i].hidden) continue;
        if (!first) sb_put(em->o, ", ");
        first = false;
        put_zeros(em, t.decl->fields.items[i].type);
    }
    sb_put(em->o, ")");
}

// How many elements the list has: the uniforms' word that says.
static void put_list_count(emit *em, const int texture)
{
    if (texture < 0) sb_put(em->o, "0");
    else put_word(em, 'v', em->sh->textures.items[texture].count_at);
}

// Ints from an element to the next in a list's or a grid's texture (tide_data_stride).
static int data_stride(const int words)
{
    return words <= 2 ? words : packs(words) * 4;
}

// The function that reads an element of the shader's list `texture`, or a
// cell of its grid, written before the code that calls it: zeros outside it,
// and otherwise the value made of its ints, floats by their bits.
static void need_data(emit *em, const int texture, const type element, const bool grid)
{
    if (texture < 0 || em->lists[texture]) return;
    em->lists[texture] = true;
    const bool wgsl = em->l == WGSL;
    int floats = 0, ints = 0;
    type_words(element, &floats, &ints);
    const int stride = data_stride(floats + ints), pixels = stride < 4 ? 1 : stride / 4;
    const int count_at = em->sh->textures.items[texture].count_at;
    sb text = {0};
    sb *const out = em->o;
    em->o = &text;
    const char *t = ty(em, element);
    if (grid) {
        if (wgsl) sb_printf(&text, "fn tide_list%d(tide_i: vec2i) -> %s {\n", texture, t);
        else sb_printf(&text, "%s tide_list%d(ivec2 tide_i) {\n", t, texture);
        sb_put(&text, "    if (tide_i.x < 0 || tide_i.y < 0 || tide_i.x >= ");
        put_word(em, 'v', count_at);
        sb_put(&text, " || tide_i.y >= ");
        put_word(em, 'v', count_at + 1);
    } else {
        if (wgsl) sb_printf(&text, "fn tide_list%d(tide_i: i32) -> %s {\n    if (tide_i < 0 || tide_i >= ", texture, t);
        else sb_printf(&text, "%s tide_list%d(int tide_i) {\n    if (tide_i < 0 || tide_i >= ", t, texture);
        put_word(em, 'v', count_at);
    }
    sb_put(&text, ") {\n        return ");
    put_zeros(em, element);
    sb_printf(&text, ";\n    }\n    %s tide_word = tide_i%s * %d;\n    %s tide_first = tide_word >> %s;\n", wgsl ? "let" : "int",
              grid ? ".x" : "", stride, wgsl ? "let" : "int", wgsl ? "2u" : "2");
    for (int k = 0; k < pixels; k++) {
        char name[16], at[96];
        if (stride < 4) snprintf(name, sizeof name, "tide_r");
        else snprintf(name, sizeof name, "tide_q%d", k);
        // A list's rows are TIDE_LIST_DATA_ACROSS pixels; a grid's are its own
        if (grid) snprintf(at, sizeof at, "tide_first + %d, tide_i.y", k);
        else snprintf(at, sizeof at, "(tide_first + %d) & 1023, (tide_first + %d) >> %s", k, k, wgsl ? "10u" : "10");
        if (wgsl) sb_printf(&text, "    let %s = textureLoad(pixels%d, vec2i(%s), 0);\n", name, texture, at);
        else sb_printf(&text, "    ivec4 %s = texelFetch(pixels%d, ivec2(%s), 0);\n", name, texture, at);
        if (stride < 4) { // Several to a pixel: this one's ints, from where it starts in it
            sb_printf(&text, wgsl ? "    let tide_q0 = vec4i(tide_r[tide_word & 3], %s, 0, 0);\n"
                                  : "    ivec4 tide_q0 = ivec4(tide_r[tide_word & 3], %s, 0, 0);\n",
                      stride == 2 ? "tide_r[(tide_word & 3) + 1]" : "0");
        }
        if (k * 4 < floats) sb_printf(&text, wgsl ? "    let tide_p%d = bitcast<vec4f>(tide_q%d);\n" : "    vec4 tide_p%d = intBitsToFloat(tide_q%d);\n", k, k);
    }
    sb_put(&text, "    return ");
    int f = 0, n = floats;
    put_value(em, element, 'p', 'q', &f, &n);
    sb_put(&text, ";\n}\n\n");
    em->o = out;
    sb_put(&em->functions, text.data);
}

// ---------------------------------------------------------------------------
// Arrays
//
// An array of a fixed length is a value like a struct: its elements' words,
// one after the other, wherever a value goes. An element past its ends reads
// as zeros, and writing one does nothing, as on the CPU.

// The function that reads an element of arrays of type `array`, written before the code that calls it.
static int need_array(emit *em, const decl *array)
{
    for (int i = 0; i < em->arrays.count; i++) {
        if (em->arrays.items[i] == array) return i;
    }
    vec_push(em->arrays, array);
    const int n = em->arrays.count - 1;
    const type whole = {TY_ARRAY, (decl *)array}, element = array->fields.items[0].type;
    sb text = {0};
    sb *const out = em->o;
    em->o = &text;
    const char *a = ty(em, whole), *t = ty(em, element);
    if (em->l == WGSL) {
        sb_printf(&text, "fn tide_el%d(tide_a: %s, tide_i: i32) -> %s {\n    if (tide_i < 0 || tide_i >= %d) {\n        return %s();\n"
                         "    }\n    var tide_all = tide_a;\n    return tide_all[tide_i];\n}\n\n", n, a, t, array->array_length, t);
    } else {
        sb_printf(&text, "%s tide_el%d(%s tide_a, int tide_i) {\n    if (tide_i < 0 || tide_i >= %d) {\n        return ", t, n, a,
                  array->array_length);
        put_zeros(em, element);
        sb_put(&text, ";\n    }\n    return tide_a[tide_i];\n}\n\n");
    }
    em->o = out;
    sb_put(&em->functions, text.data);
    return n;
}

// array[i]: its element, or while an assignment's target is written, the place itself.
static void ex_array_index(emit *em, const expr *e)
{
    const type array = e->object->type;
    if (!type_fits(em, array, e->object->at)) return;
    if (em->raw > 0) {
        ex(em, e->object);
        const int raw = em->raw;
        em->raw = 0; // What says which element is read like anything else
        const bool folds = e->lhs->kind != E_INT && is_const(e->lhs);
        if (folds) em->defuse++;
        sb_put(em->o, "[");
        ex_as(em, e->lhs, (type){TY_INT, NULL});
        sb_put(em->o, "]");
        if (folds) em->defuse--;
        em->raw = raw;
        return;
    }
    sb_printf(em->o, "tide_el%d(", need_array(em, array.decl));
    ex(em, e->object);
    sb_put(em->o, ", ");
    ex_as(em, e->lhs, (type){TY_INT, NULL});
    sb_put(em->o, ")");
}

// Whether `e` is a place inside an array: an element, or part of one.
static bool in_array(const expr *e)
{
    for (; e; e = e->object) {
        if (e->kind == E_INDEX && e->object && e->object->type.kind == TY_ARRAY) return true;
        if (e->kind != E_INDEX && e->kind != E_MEMBER) return false;
    }
    return false;
}

// How many of the elements the place `e` names are known only as the code
// runs: one named by a number is known here. -1 when one of those is past its
// array's ends, so the place is nowhere.
static int unknown_elements(const expr *e)
{
    int unknown = 0;
    for (; e && (e->kind == E_INDEX || e->kind == E_MEMBER); e = e->object) {
        if (e->kind != E_INDEX || !e->object || e->object->type.kind != TY_ARRAY) continue;
        if (e->lhs->kind == E_INT) {
            const long long at = int_literal(e->lhs);
            if (at < 0 || at >= e->object->type.decl->array_length) return -1;
        } else {
            unknown++;
        }
    }
    return unknown;
}

// "(i >= 0 && i < N) && ...": that each of those elements is one its array has.
static void put_in_range(emit *em, const expr *e)
{
    bool first = true;
    for (; e && (e->kind == E_INDEX || e->kind == E_MEMBER); e = e->object) {
        if (e->kind != E_INDEX || !e->object || e->object->type.kind != TY_ARRAY || e->lhs->kind == E_INT) continue;
        if (!first) sb_put(em->o, " && ");
        first = false;
        const bool folds = is_const(e->lhs); // A constant past the ends is no error here: this is what catches it
        if (folds) em->defuse++;
        sb_put(em->o, "(");
        ex_as(em, e->lhs, (type){TY_INT, NULL});
        sb_put(em->o, " >= 0 && ");
        ex_as(em, e->lhs, (type){TY_INT, NULL});
        sb_printf(em->o, " < %d)", e->object->type.decl->array_length);
        if (folds) em->defuse--;
    }
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

// -1, -0.5: a literal with a minus before it, which is in an int's range.
static bool negated_literal(const expr *e)
{
    if (e->kind != E_UNARY || e->op != T_MINUS || e->method || !e->lhs) return false;
    if (e->lhs->kind == E_FLOAT) return true;
    return e->lhs->kind == E_INT && e->lhs->type.kind != TY_LONG && int_literal(e->lhs) <= 2147483647ll;
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
    if (e->type.kind == TY_LIST || e->type.kind == TY_GRID) { // A name for one, where a value goes
        type_fits(em, e->type, e->at);
        return;
    }
    switch (e->bind) {
    case BIND_LOCAL:
        if (em->lifting) { // Before it's declared: its value, which nothing changed (liftable)
            sb_put(em->o, "(");
            ex_as(em, e->local->value, e->local->type);
            sb_put(em->o, ")");
            return;
        }
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
                  {"magenta", 1, 0, 1, 1}, {"gray", 0.5f, 0.5f, 0.5f, 1}, {"clear", 0, 0, 0, 0},
                  {"none", 0, 0, 0, -1}};
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

// this.position and the like: what the GPU says of the pixel being drawn.
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
    if (e->object->type.kind == TY_ARRAY && str_eq_c(e->member, "count")) { // How many an array has: its type says
        put_int(em, e->object->type.decl->array_length);
        return;
    }
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
    data_ref data;
    if (str_eq_c(e->member, "size") && obj->type.kind == TY_GRID && data_of(em, obj, &data)) { // A grid's size
        if (data.texture < 0) sb_printf(em->o, "%s(0)", ty(em, (type){TY_INT2, NULL}));
        else put_words(em, 'v', em->sh->textures.items[data.texture].count_at, 2, ty(em, (type){TY_INT2, NULL}));
        return;
    }
    if (str_eq_c(e->member, "count") && obj->type.kind == TY_LIST && data_of(em, obj, &data)) { // How many a list has
        put_list_count(em, data.texture);
        return;
    }
    if (obj->kind == E_THIS && obj->type.kind == TY_RECORD && obj->type.decl == em->prog->pixel) {
        if (em->checking && str_eq_c(e->member, "world")) em->root->pixel_world = true;
        ex_pixel(em, e);
        return;
    }
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
            if (e->type.kind == TY_ARRAY && !em->checking && !em->lifting) { // Made once, as the function starts
                int at = 0;
                while (at < em->hoisted.count && em->hoisted.items[at] != e->field) at++;
                if (at == em->hoisted.count) {
                    vec_push(em->hoisted, e->field);
                    sb *const out = em->o;
                    sb made = {0};
                    em->o = &made;
                    if (em->l == WGSL) sb_printf(&made, "    var tide_array%d: %s = ", at, ty(em, e->type));
                    else sb_printf(&made, "    %s tide_array%d = ", ty(em, e->type), at);
                    put_bound(em, p->type.decl, e->field, e->type);
                    sb_put(&made, ";\n");
                    em->o = out;
                    sb_put(&em->grads, made.data);
                }
                sb_printf(em->o, "tide_array%d", at);
            } else {
                put_bound(em, p->type.decl, e->field, e->type);
            }
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
    if (want.kind == TY_COLOR && have_dim == 1) { // A number beside colors in Math: that in each channel
        sb_printf(em->o, "%s(", ty(em, want));
        ex_as(em, e, (type){TY_FLOAT, NULL});
        sb_put(em->o, ")");
        return;
    }
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
    {"Abs", "abs", "abs"}, {"Sign", "sign", "sign"}, {"Min", "min", "min"}, {"Max", "max", "max"},
    {"Floor", "floor", "floor"}, {"Ceil", "ceil", "ceil"}, {"Round", "roundEven", "round"}, {"Trunc", "trunc", "trunc"},
    {"Frac", "fract", "fract"}, {"Sqrt", "sqrt", "sqrt"}, {"Rsqrt", "inversesqrt", "inverseSqrt"},
    {"Radians", "radians", "radians"}, {"Degrees", "degrees", "degrees"}, {"Sin", "sin", "sin"}, {"Cos", "cos", "cos"},
    {"Tan", "tan", "tan"}, {"Asin", "asin", "asin"}, {"Acos", "acos", "acos"}, {"Atan", "atan", "atan"},
    {"Exp", "exp", "exp"}, {"Exp2", "exp2", "exp2"}, {"Log", "log", "log"}, {"Log2", "log2", "log2"},
    {"Step", "step", "step"}, {"Lerp", "mix", "mix"},
    {"Dot", "dot", "dot"}, {"Length", "length", "length"}, {"Distance", "distance", "distance"},
    {"Reflect", "reflect", "reflect"}, {"Cross", "cross", "cross"},
    {"Transpose", "transpose", "transpose"}, {"Determinant", "determinant", "determinant"},
};

// Math's functions on a quaternion that are a function of the engine's own, of the same arguments.
static const struct {
    const char *name;
    int helper;
    const char *call;
} quaternion_math[] = {
    {"Normalize", H_NORMALIZE_Q, "tide_normalize_q"}, {"Rotate", H_ROTATE_Q, "tide_rotate_q"},
    {"Inverse", H_INVERSE_Q, "tide_inverse_q"}, {"Conjugate", H_CONJUGATE_Q, "tide_conjugate_q"},
    {"Slerp", H_SLERP_Q, "tide_slerp_q"}, {"Nlerp", H_NLERP_Q, "tide_nlerp_q"}, {"Angle", H_ANGLE_Q, "tide_angle_q"},
};

// What builds a rotation or a matrix from numbers, the same way.
static const struct {
    const char *owner;
    const char *name;
    int helper;
    const char *call;
} builders[] = {
    {"quaternion", "AxisAngle", H_AXISANGLE_Q, "tide_axisangle_q"}, {"quaternion", "Euler", H_EULER_Q, "tide_euler_q"},
    {"quaternion", "LookRotation", H_LOOKROTATION_Q, "tide_lookrotation_q"}, {"float4x4", "TRS", H_TRS, "tide_trs"},
    {"float4x4", "PerspectiveFov", H_PERSPECTIVE, "tide_perspective"}, {"float4x4", "Ortho", H_ORTHO, "tide_ortho"},
};

static void put_arg(emit *em, const expr *e, const int i)
{
    ex_as(em, e->args.items[i], i < e->arg_want.count ? e->arg_want.items[i] : e->args.items[i]->type);
}

// Component `k` of the call's first argument.
static void put_component(emit *em, const expr *e, const int k)
{
    put_arg(em, e, 0);
    sb_printf(em->o, ".%c", "xyzw"[k]);
}

// name(the call's arguments)
static void put_call(emit *em, const expr *e, const char *name)
{
    sb_printf(em->o, "%s(", name);
    for (int i = 0; i < e->args.count; i++) {
        if (i) sb_put(em->o, ", ");
        put_arg(em, e, i);
    }
    sb_put(em->o, ")");
}

// The same through a function of the engine's own.
static void put_helper_call(emit *em, const expr *e, const int helper, const char *name)
{
    need(em, helper);
    put_call(em, e, name);
}

static void put_family_call(emit *em, const expr *e, const int family, const int dim)
{
    char name[32];
    snprintf(name, sizeof name, "tide_%s%d", family_names[family], dim);
    put_helper_call(em, e, family_helper(family, dim), name);
}

// Everything in Math is here, as on the CPU up to the GPU's rounding, but
// what the GPU has no way to do: a long's hash.
static void ex_math(emit *em, const expr *e)
{
    const bool wgsl = em->l == WGSL;
    const type first = e->arg_want.count ? e->arg_want.items[0] : (type){TY_ERROR, NULL};
    const bool floats = type_is_float_based(first) || first.kind == TY_COLOR; // A color is four floats here
    const bool quaternion = first.kind == TY_QUATERNION;
    const int dim = type_dim(first) ? type_dim(first) : 4;
    if (str_eq_c(e->name, "Hash") && first.kind == TY_LONG) {
        refuse(em, e->at, "Math.Hash of a long isn't on the GPU, whose ints are 32 bits");
        diag_note("split it on the CPU, into fields the GPU reads: 'Math.Hash(int2(low, high))' is the same number for "
                  "'low = int(x)' and 'high = int(x >> 32)'");
        return;
    }
    // The compiler folds nothing here: a constant out of a function's range is an error to WGSL, not a value
    em->defuse++;
    if ((str_eq_c(e->name, "Min") || str_eq_c(e->name, "Max")) && floats) { // With one NaN, the other, as on the CPU
        put_family_call(em, e, str_eq_c(e->name, "Min") ? FAMILY_MIN : FAMILY_MAX, dim);
    } else if (str_eq_c(e->name, "Clamp") && floats) {
        put_family_call(em, e, FAMILY_CLAMP, dim);
    } else if (str_eq_c(e->name, "Clamp")) { // Ints: the lower bound when they're the wrong way around, as on the CPU
        sb_put(em->o, "max(");
        put_arg(em, e, 1);
        sb_put(em->o, ", min(");
        put_arg(em, e, 2);
        sb_put(em->o, ", ");
        put_arg(em, e, 0);
        sb_put(em->o, "))");
    } else if (str_eq_c(e->name, "Saturate")) {
        put_family_call(em, e, FAMILY_SATURATE, dim);
    } else if (str_eq_c(e->name, "SmoothStep")) {
        put_family_call(em, e, FAMILY_SMOOTHSTEP, dim);
    } else if (str_eq_c(e->name, "Pow")) {
        put_family_call(em, e, FAMILY_POW, dim);
    } else if (str_eq_c(e->name, "Atan2")) {
        put_family_call(em, e, FAMILY_ATAN2, dim);
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
    } else if (str_eq_c(e->name, "Normalize") && !quaternion) { // Zero for a zero vector, as on the CPU
        char name[32];
        snprintf(name, sizeof name, "tide_normalize%d", dim);
        put_helper_call(em, e, H_NORMALIZE2 + dim - 2, name);
    } else if (str_eq_c(e->name, "Csum")) {
        sb_put(em->o, "dot(");
        put_arg(em, e, 0);
        sb_printf(em->o, ", %s(1.0))", float_ty(em, dim));
    } else if (str_eq_c(e->name, "Cmin") || str_eq_c(e->name, "Cmax")) { // In the CPU's order, a pair at a time
        const char *fn = str_eq_c(e->name, "Cmin") ? "tide_min1" : "tide_max1";
        need(em, family_helper(str_eq_c(e->name, "Cmin") ? FAMILY_MIN : FAMILY_MAX, 1));
        sb_printf(em->o, dim > 2 ? "%s(%s(" : "%s(", fn, fn);
        put_component(em, e, 0);
        sb_put(em->o, ", ");
        put_component(em, e, 1);
        sb_put(em->o, ")");
        if (dim == 3) {
            sb_put(em->o, ", ");
            put_component(em, e, 2);
            sb_put(em->o, ")");
        } else if (dim == 4) {
            sb_printf(em->o, ", %s(", fn);
            put_component(em, e, 2);
            sb_put(em->o, ", ");
            put_component(em, e, 3);
            sb_put(em->o, "))");
        }
    } else if (str_eq_c(e->name, "Perlin") || str_eq_c(e->name, "Simplex") || str_eq_c(e->name, "Cellular")) {
        // noise.c's lines, so a frag draws the cells the match computed; the seed is 0 where the call leaves it out
        const bool perlin = str_eq_c(e->name, "Perlin"), simplex = str_eq_c(e->name, "Simplex");
        need(em, (perlin ? H_PERLIN2 : simplex ? H_SIMPLEX2 : H_CELLULAR2) + dim - 2);
        sb_printf(em->o, "tide_%s%d(", perlin ? "perlin" : simplex ? "simplex" : "cellular", dim);
        put_arg(em, e, 0);
        sb_put(em->o, ", ");
        if (e->args.count > 1) put_arg(em, e, 1);
        else sb_put(em->o, "0");
        sb_put(em->o, ")");
    } else if (str_eq_c(e->name, "Hash")) { // Integers: the CPU's number
        char name[32];
        snprintf(name, sizeof name, "tide_hash%d", type_dim(first));
        put_helper_call(em, e, H_HASH1 + type_dim(first) - 1, name);
    } else if (quaternion && str_eq_c(e->name, "Mul")) { // Two rotations, or one and the vector it turns
        const bool turns = e->arg_want.items[1].kind != TY_QUATERNION;
        put_helper_call(em, e, turns ? H_ROTATE_Q : H_MUL_Q, turns ? "tide_rotate_q" : "tide_mul_q");
    } else if (str_eq_c(e->name, "Forward") || str_eq_c(e->name, "Up") || str_eq_c(e->name, "Right")) {
        need(em, H_ROTATE_Q);
        sb_put(em->o, "tide_rotate_q(");
        put_arg(em, e, 0);
        sb_printf(em->o, ", %s(%s))", float_ty(em, 3), str_eq_c(e->name, "Forward") ? "0.0, 0.0, 1.0"
                                                      : str_eq_c(e->name, "Up")    ? "0.0, 1.0, 0.0"
                                                                                   : "1.0, 0.0, 0.0");
    } else if (str_eq_c(e->name, "Mul")) {
        sb_put(em->o, "(");
        put_arg(em, e, 0);
        sb_put(em->o, " * ");
        put_arg(em, e, 1);
        sb_put(em->o, ")");
    } else if (str_eq_c(e->name, "Inverse") && !quaternion) { // WGSL has none, and GLSL's says nothing of its order
        char name[32];
        snprintf(name, sizeof name, "tide_inverse%d", matrix_dim(first));
        put_helper_call(em, e, H_INVERSE2 + matrix_dim(first) - 2, name);
    } else if ((str_eq_c(e->name, "Transform") || str_eq_c(e->name, "Rotate")) && first.kind == TY_FLOAT4X4) {
        sb_put(em->o, "(");
        put_arg(em, e, 0);
        sb_printf(em->o, " * %s(", float_ty(em, 4));
        put_arg(em, e, 1);
        sb_printf(em->o, ", %s)).xyz", str_eq_c(e->name, "Transform") ? "1.0" : "0.0");
    } else {
        const char *fn = NULL;
        int helper = -1;
        for (size_t i = 0; quaternion && i < sizeof quaternion_math / sizeof quaternion_math[0]; i++) {
            if (!str_eq_c(e->name, quaternion_math[i].name)) continue;
            fn = quaternion_math[i].call;
            helper = quaternion_math[i].helper;
        }
        for (size_t i = 0; !fn && i < sizeof same_math / sizeof same_math[0]; i++) {
            if (str_eq_c(e->name, same_math[i].name)) fn = wgsl ? same_math[i].wgsl : same_math[i].glsl;
        }
        if (!fn) { // A function Math gains goes here too, or says why it can't
            em->defuse--;
            refuse(em, e->at, "Math." STR_FMT " isn't on the GPU yet", STR_ARG(e->name));
            diag_note("work it out on the CPU, in the view, and keep the result in a component or a singleton the code reads");
            return;
        }
        if (helper >= 0) need(em, helper);
        put_call(em, e, fn);
    }
    em->defuse--;
}

// quaternion.AxisAngle, float4x4.TRS and the others: tide/math.h's, written out.
static bool ex_builder(emit *em, const expr *e)
{
    const str owner = e->object->name;
    if (str_eq_c(owner, "float4x4") && str_eq_c(e->name, "Translate")) {
        const char *f4 = float_ty(em, 4);
        sb_printf(em->o, "%s(%s(1.0, 0.0, 0.0, 0.0), %s(0.0, 1.0, 0.0, 0.0), %s(0.0, 0.0, 1.0, 0.0), %s(",
                  ty(em, e->type), f4, f4, f4, f4);
        put_arg(em, e, 0);
        sb_put(em->o, ", 1.0))");
        return true;
    }
    for (size_t i = 0; i < sizeof builders / sizeof builders[0]; i++) {
        if (!str_eq_c(owner, builders[i].owner) || !str_eq_c(e->name, builders[i].name)) continue;
        put_helper_call(em, e, builders[i].helper, builders[i].call);
        return true;
    }
    return false;
}

// float3(1, 2, 3), int(x), Color(r, g, b)
static void ex_construct(emit *em, const expr *e)
{
    const type t = e->type;
    const bool floats = type_is_float_based(t) || matrix_dim(t) || t.kind == TY_COLOR || t.kind == TY_QUATERNION;
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
        sb_printf(em->o, t.kind == TY_BYTE ? "(%s(" : "%s(", ty(em, t));
        ex(em, e->args.items[0]);
        sb_put(em->o, t.kind == TY_BYTE ? ") & 255)" : ")"); // A byte keeps its low 8 bits, as on the CPU
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
    case CTOR_COLOR: {
        sb_printf(em->o, "%s(", ty(em, t));
        int components = 0;
        for (int i = 0; i < e->args.count; i++) {
            const expr *arg = e->args.items[i];
            if (i) sb_put(em->o, ", ");
            ex_as(em, arg, floats ? as_float(arg->type) : arg->type);
            components += type_dim(arg->type);
        }
        if (e->ctor == CTOR_COLOR && components == 3) sb_put(em->o, ", 1.0");
        sb_put(em->o, ")");
        return;
    }
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
    case CTOR_QUAT_FROM_F4: // A quaternion is its float4 here
        ex(em, e->args.items[0]);
        return;
    case CTOR_QUAT_FROM_MAT:
        put_helper_call(em, e, H_Q_FROM_M3, "tide_q_from_m3");
        return;
    case CTOR_MAT_FROM_QUAT:
        put_helper_call(em, e, H_M3_FROM_Q, "tide_m3_from_q");
        return;
    case CTOR_MAT_FROM_ROT_T:
        need(em, H_M4_FROM_M3);
        sb_put(em->o, "tide_m4_from_m3(");
        ex(em, e->args.items[0]);
        sb_put(em->o, ", ");
        ex_as(em, e->args.items[1], (type){TY_FLOAT3, NULL});
        sb_put(em->o, ")");
        return;
    default: // A PlayerID or a Rect, which the GPU has none of: refused as a type before it gets here
        refuse(em, e->at, "%s isn't something the GPU has", type_name(t));
    }
}

// The first value of the field attribute `name`, as its enum member's number: -1 without one.
static int field_sampling(const field *f, const char *name)
{
    for (int i = 0; f && i < f->attributes.count; i++) {
        const attribute *a = &f->attributes.items[i];
        if (str_eq_c(a->name, name) && a->values.count == 1 && a->values.items[0]->enum_member) {
            return (int)a->values.items[0]->enum_member->number;
        }
    }
    return -1;
}

bool field_is_raw(const field *f)
{
    for (int i = 0; f && i < f->attributes.count; i++) {
        if (str_eq_c(f->attributes.items[i].name, "Raw")) return true;
    }
    return false;
}

bool declared_raw(const expr *texture)
{
    if (!texture) return false;
    if (texture->bind == BIND_ASSET && texture->constant) return texture->constant->sample_raw;
    return texture->kind == E_MEMBER && texture->field && texture->type.kind == TY_GRID && field_is_raw(texture->field);
}

void declared_sampling(const expr *texture, int *filter, int *wrap)
{
    *filter = *wrap = -1;
    if (!texture) return;
    // frames[2], icons["coin"]: one of an asset's glob, whose declaration is the glob's
    const expr *named = texture->kind == E_INDEX && texture->object && texture->object->bind == BIND_ASSET ? texture->object : texture;
    // ...and `frame` of 'foreach (var frame in frames)', which is read-only: one of them too
    if (named->bind == BIND_LOCAL && named->local && named->local->kind == S_FOREACH && named->local->value
        && named->local->value->bind == BIND_ASSET) {
        named = named->local->value;
    }
    // target.color, target.colors[1]: a RenderTarget field's color images, as the field says
    if (named->kind == E_INDEX && named->object && named->object->kind == E_MEMBER && named->object->target_colors) {
        named = named->object->object;
    } else if (named->kind == E_MEMBER && named->object && named->object->type.kind == TY_RENDER_TARGET
               && named->type.kind == TY_TEXTURE) {
        named = named->object;
    }
    if (named->bind == BIND_ASSET && named->constant) {
        *filter = named->constant->sample_filter - 1;
        *wrap = named->constant->sample_wrap - 1;
    } else if (named->kind == E_MEMBER && named->field && (named->type.kind == TY_GRID || named->type.kind == TY_RENDER_TARGET)) {
        *filter = field_sampling(named->field, "Filter");
        *wrap = field_sampling(named->field, "Wrap");
    }
}

// How a Sample's texture is read between its pixels where that's known as
// the game is compiled: its Filter's number, or -1 for a Texture's that's no
// asset's by name, which is whichever texture a draw gives.
static int known_filter(const expr *e)
{
    const expr *of = e->object;
    if (of->type.kind == TY_DEPTH_TEXTURE) return 0;
    if (e->args.count >= 2) return e->args.items[1]->enum_member ? (int)e->args.items[1]->enum_member->number : 1;
    if (of->type.kind == TY_CUBEMAP) return 1; // A cube that says none is read blended
    if (of->bind != BIND_ASSET && of->type.kind == TY_TEXTURE) return -1;
    int filter, wrap;
    declared_sampling(of, &filter, &wrap);
    return filter == 0 || filter == 2 ? filter : 1;
}

// Whether a frag can work an expression out as it starts, before any branch:
// it's made of the frag's parameters, constants, locals that are never
// assigned again and whose own values are so, and what changes nothing
// (arithmetic, Math, constructors, functions that take everything by value,
// and a texture read at its own pixels). A frag asks how fast a Sample's uv
// changes from pixel to pixel there, which a GPU only answers outside
// branches and loops.
static bool liftable(const emit *em, const expr *e, const int depth)
{
    if (!e || depth > 64) return false;
    switch (e->kind) {
    case E_INT:
    case E_FLOAT:
    case E_BOOL:
    case E_DEFAULT:
        return true;
    case E_THIS:
        return em->code == em->root && em->root->is_frag;
    case E_NAME:
        if (e->bind == BIND_CONST) return true;
        if (e->bind == BIND_PARAM) return em->code == em->root && e->param->mode != PARAM_MUT && !e->param->is_out;
        if (e->bind == BIND_LOCAL) {
            const stmt *s = e->local;
            return s && s->kind == S_VAR && !s->is_mut && !s->loop_var && !s->out_decl && !s->assigned && s->value
                && liftable(em, s->value, depth + 1);
        }
        return false;
    case E_MEMBER:
        if (e->constant || e->enum_member || e->c_constant) return true;
        return liftable(em, e->object, depth + 1);
    case E_BINARY:
    case E_UNARY:
        if (e->method) {
            for (int i = 0; i < e->method->params.count; i++) {
                if (e->method->params.items[i].mode == PARAM_MUT) return false;
            }
        }
        return liftable(em, e->lhs, depth + 1) && (e->kind == E_UNARY || liftable(em, e->rhs, depth + 1));
    case E_CONDITIONAL:
        return liftable(em, e->cond, depth + 1) && liftable(em, e->lhs, depth + 1) && liftable(em, e->rhs, depth + 1);
    case E_LITERAL:
        for (int i = 0; i < e->inits.count; i++) {
            if (!liftable(em, e->inits.items[i].value, depth + 1)) return false;
        }
        return true;
    case E_CALL:
    case E_METHOD: {
        const bool on_value = e->kind == E_METHOD && e->object && !(e->object->kind == E_NAME && e->object->bind != BIND_LOCAL
                                                                    && e->object->bind != BIND_PARAM && e->object->bind != BIND_CONST);
        if (e->call == CALL_GRID) { // A texture read at its own pixels asks nothing of the pixels around
            const int filter = known_filter(e);
            return (str_eq_c(e->name, "Sample") || str_eq_c(e->name, "Compare")) && (filter == 0 || filter == 1)
                && liftable(em, e->args.items[0], depth + 1) && (e->args.count < 2 || e->object->type.kind != TY_DEPTH_TEXTURE
                                                                 || liftable(em, e->args.items[1], depth + 1));
        }
        if (e->call == CALL_FUNCTION || e->call == CALL_METHOD) {
            const decl *fn = e->method;
            if (!fn || fn->is_extern || fn->is_mut_method) return false;
            for (int i = 0; i < fn->params.count; i++) { // One with the pixel could leave it undrawn
                const param *p = &fn->params.items[i];
                if (p->mode == PARAM_MUT || p->is_out || p->type.decl == em->prog->pixel) return false;
            }
        } else if (e->call != CALL_CONSTRUCT && e->call != CALL_BUILTIN) {
            return false;
        }
        if (on_value && !liftable(em, e->object, depth + 1)) return false;
        for (int i = 0; i < e->args.count; i++) {
            if (!liftable(em, e->args.items[i], depth + 1)) return false;
        }
        return true;
    }
    default:
        return false;
    }
}

// A Cubemap's Sample: an asset's by name, or a field's, read by a direction.
// One sampled with another filter is another binding, as a texture's is, and
// one read between its smaller images has its direction worked out as the
// frag starts, with how fast it changes, as a uv is (ex_sample).
static void ex_cube_sample(emit *em, const expr *e, const decl *asset, const param *p)
{
    const expr *of = e->object;
    const int said = e->args.count >= 2 ? (int)e->args.items[1]->enum_member->number : 1;
    const int filter = said == 0 ? 0 : said == 2 ? 2 : 1;
    const bool lifts = em->root->is_frag && em->code == em->root && !em->lifting && filter == 2;
    const bool grad = lifts && liftable(em, e->args.items[0], 0);
    if (lifts && !grad && em->checking) {
        refuse(em, e->args.items[0]->at, "a Sample that's Filter.Trilinear reads where the frag can work out as it starts: this "
                                         "direction is made of a variable that changes, or of one a loop counts");
        diag_note("the GPU picks between a cube's smaller images by how fast the direction changes from one pixel to the "
                  "next, which it's asked before any branch or loop: make it of the frag's parameters, constants and "
                  "variables declared with 'var' and never assigned again, or sample with Filter.Bilinear here");
    }
    int slot = 0;
    if (em->checking) {
        if (em->code != em->root) {
            refuse(em, e->at, "a texture is sampled in the frag or vert that takes it, not in a function it calls");
            return;
        }
        const decl *holds = asset ? NULL : of->local_field ? p->type.decl->local_part : p->type.decl;
        const shader_sample sample = {asset ? -1 : param_index(em->root, p), asset ? -1 : field_index(holds, of->field),
                                      of->local_field && !asset, filter, asset, 0, READ_CUBE, true, true};
        bool known = false;
        for (int i = 0; i < em->root->samples.count; i++) {
            const shader_sample *s = &em->root->samples.items[i];
            known |= s->param == sample.param && s->field == sample.field && s->local == sample.local && s->filter == sample.filter
                  && s->asset == sample.asset && s->read == READ_CUBE;
        }
        if (!known) vec_push(em->root->samples, sample);
    } else {
        while (slot < em->sh->textures.count) {
            const shader_texture *t = &em->sh->textures.items[slot];
            if (t->asset == asset && !t->direct && t->source == (p ? p->type.decl : NULL) && t->field == (asset ? NULL : of->field)
                && t->filter == filter && t->read == READ_CUBE) {
                break;
            }
            slot++;
        }
    }
    const type way = {TY_FLOAT3, NULL};
    need(em, H_STRAIGHT); // The color itself, where the cube has it times its alpha
    sb_put(em->o, "tide_straight(");
    if (grad) {
        const int at = em->grad_count++;
        sb *const out = em->o;
        const int defuse = em->defuse;
        const expr *statement = em->statement;
        em->o = &em->grads;
        em->defuse = 0;
        em->statement = NULL;
        em->lifting++;
        sb_printf(em->o, em->l == WGSL ? "    let tide_uv%d: vec3f = " : "    vec3 tide_uv%d = ", at);
        ex_as(em, e->args.items[0], way);
        if (em->l == WGSL) {
            sb_printf(em->o, ";\n    let tide_dx%d: vec3f = dpdx(tide_uv%d);\n    let tide_dy%d: vec3f = dpdy(tide_uv%d);\n", at, at, at, at);
        } else {
            sb_printf(em->o, ";\n    vec3 tide_dx%d = dFdx(tide_uv%d);\n    vec3 tide_dy%d = dFdy(tide_uv%d);\n", at, at, at, at);
        }
        em->lifting--;
        em->o = out;
        em->defuse = defuse;
        em->statement = statement;
        if (em->l == WGSL) sb_printf(em->o, "textureSampleGrad(pixels%d, sampler%d, ", slot, slot);
        else sb_printf(em->o, "textureGrad(pixels%d, ", slot);
        ex_as(em, e->args.items[0], way);
        sb_printf(em->o, ", tide_dx%d, tide_dy%d))", at, at);
        return;
    }
    if (em->l == WGSL) sb_printf(em->o, "textureSampleLevel(pixels%d, sampler%d, ", slot, slot);
    else sb_printf(em->o, "textureLod(pixels%d, ", slot);
    ex_as(em, e->args.items[0], way);
    sb_put(em->o, ", 0.0))");
}

// A texture parameter, or a grid or texture field: which binding reads it.
// One sampled with another filter or another wrap is another binding, as a
// backend's sampler is how a binding is read. A DepthTexture field is a
// binding for each way it's read, as a number (Sample) and compared
// (Compare): the GPU reads each through a sampler of another kind.
static void ex_sample(emit *em, const expr *e)
{
    const expr *of = e->object;
    const decl *asset = of->bind == BIND_ASSET ? of->constant : NULL; // A Grid2<Color> or Texture asset, by name
    const bool direct = of->kind == E_NAME && !asset;
    const param *p = asset ? NULL : direct ? of->param : of->object->param;
    if (of->type.kind == TY_CUBEMAP) {
        ex_cube_sample(em, e, asset, p);
        return;
    }
    const shader_read read = of->type.kind != TY_DEPTH_TEXTURE ? READ_COLOR : str_eq_c(e->name, "Compare") ? READ_COMPARE : READ_DEPTH;
    // A depth's nearest pixel is read, the pixels at its edge past them, and what its comparisons give, the
    // GPU blends: none of it is the draw's to say
    // A Sample that doesn't say its filter or its wrap has its texture's own. A grid field's and an asset's
    // by name are their declaration's ([Filter], [Wrap]), and without one, blended and the cells at the edge:
    // known here. A Texture's that's no asset's (the frag's own Texture parameter, a Texture field) is
    // whichever texture it's given, which says how it's sampled: that's each draw's run's to carry
    int declared_filter, declared_wrap;
    declared_sampling(of, &declared_filter, &declared_wrap);
    const bool says_filter = e->args.count >= 2, says_wrap = e->args.count >= 3;
    const bool carried = read == READ_COLOR && !asset && of->type.kind == TY_TEXTURE;
    const bool filter_said = !carried || says_filter, wrap_said = !carried || says_wrap;
    const int said = read != READ_COLOR ? 0 : says_filter ? (int)e->args.items[1]->enum_member->number : declared_filter;
    const int filter = read != READ_COLOR || said == 0 ? 0 : said == 2 ? 2 : 1;
    // Between a texture's smaller images (Filter.Trilinear), a GPU reads by how fast the uv changes from one
    // pixel to the next, which it only answers for outside branches and loops, where code here samples. So a
    // frag works the uv out a first time as it starts, asks there, and reads with the answer wherever the
    // Sample is: for one that says Trilinear, and for one that leaves its filter to the Texture it's given in a
    // game that names Trilinear at all. A uv that can't be worked out there (it reads a variable that changes)
    // is an error where the Sample says Trilinear, and reads the texture's own pixels where a Texture might.
    // A vert has no pixels to compare: it reads the texture's own.
    const bool lifts = read == READ_COLOR && em->root->is_frag && em->code == em->root && !em->lifting
                    && (filter == 2 || (!filter_said && em->prog->names_trilinear));
    const bool grad = lifts && liftable(em, e->args.items[0], 0);
    if (lifts && !grad && filter == 2 && em->checking) {
        refuse(em, e->args.items[0]->at, "a Sample that's Filter.Trilinear reads where the frag can work out as it starts: this "
                                         "uv is made of a variable that changes, or of one a loop counts");
        diag_note("the GPU picks between a texture's smaller images by how fast the uv changes from one pixel to the next, "
                  "which it's asked before any branch or loop: make the uv of the frag's parameters, constants and "
                  "variables declared with 'var' and never assigned again, or sample with Filter.Bilinear here");
    } else if (lifts && !grad && em->checking) {
        // The texture may say Trilinear, and this read can't: said, not left to be found in a shimmer
        const expr *named = direct ? of : of->object && of->object->kind == E_NAME ? of->object : NULL;
        char which[160];
        if (direct) snprintf(which, sizeof which, "'" STR_FMT "'", STR_ARG(of->name));
        else if (named) snprintf(which, sizeof which, "'" STR_FMT "." STR_FMT "'", STR_ARG(named->name), STR_ARG(of->member));
        else snprintf(which, sizeof which, "this texture");
        diag_warning(e->args.items[0]->at, "%s may say Filter.Trilinear, which this Sample can't read: its uv is made of a "
                                           "variable that changes, or of one a loop counts, so it reads the texture's own pixels",
                     which);
        diag_note("make the uv of the frag's parameters, constants and variables declared with 'var' and never assigned "
                  "again, so the GPU can be asked how fast it changes as the frag starts; or write the filter here, "
                  "'Sample(uv, Filter.Bilinear)', which is what's read anyway");
        if (em->code != em->root) {
            diag_note_at(em->root->at, "%s '" STR_FMT "' runs on the GPU, and '" STR_FMT "' with it, as it calls it",
                         shader_word(em->root), STR_ARG(em->root->name), STR_ARG(em->code->name));
        }
    }
    const int wrap = read != READ_COLOR ? 0 : says_wrap ? (int)e->args.items[2]->enum_member->number : declared_wrap > 0 ? declared_wrap : 0;
    int slot = 0;
    if (em->checking) {
        if (em->code != em->root) {
            refuse(em, e->at, "a texture is sampled in the frag or vert that takes it, not in a function it calls");
            return;
        }
        const decl *holds = direct || asset ? NULL : of->local_field ? p->type.decl->local_part : p->type.decl;
        const shader_sample sample = {asset ? -1 : param_index(em->root, p), direct || asset ? -1 : field_index(holds, of->field),
                                      of->local_field && !asset, filter, asset, wrap, read, filter_said, wrap_said};
        bool known = false;
        for (int i = 0; i < em->root->samples.count; i++) {
            const shader_sample *s = &em->root->samples.items[i];
            known |= s->param == sample.param && s->field == sample.field && s->local == sample.local && s->filter == sample.filter
                  && s->asset == sample.asset && s->wrap == sample.wrap && s->read == sample.read
                  && s->filter_said == sample.filter_said && s->wrap_said == sample.wrap_said;
        }
        if (!known) vec_push(em->root->samples, sample);
    } else {
        while (slot < em->sh->textures.count) {
            const shader_texture *t = &em->sh->textures.items[slot];
            if (t->asset == asset && t->direct == direct && t->source == (p ? p->type.decl : NULL) && t->field == of->field
                && t->filter == filter && t->wrap == wrap && t->read == read && t->filter_said == filter_said
                && t->wrap_said == wrap_said) {
                break;
            }
            slot++;
        }
    }
    // No pixel's neighbors are asked about, so it can be sampled anywhere, in a branch or a vertex
    if (read == READ_COMPARE) {
        // What it's compared with is between the near plane and the far limit, as what's stored is:
        // past them, GPUs differ in what they make of it
        if (em->l == WGSL) sb_printf(em->o, "textureSampleCompareLevel(pixels%d, sampler%d, ", slot, slot);
        else sb_printf(em->o, "textureLod(pixels%d, vec3(", slot);
        ex_as(em, e->args.items[0], (type){TY_FLOAT2, NULL});
        sb_put(em->o, ", clamp(");
        ex_as(em, e->args.items[1], (type){TY_FLOAT, NULL});
        sb_put(em->o, em->l == WGSL ? ", 0.0, 1.0))" : ", 0.0, 1.0)), 0.0)");
        return;
    }
    // The color itself, where the texture has it times its alpha. One that's [Raw] is numbers, as they are
    const bool straight = read == READ_COLOR && !declared_raw(of);
    if (straight) {
        need(em, H_STRAIGHT);
        sb_put(em->o, "tide_straight(");
    }
    if (grad) {
        // As the frag starts: the uv, and what it changes by a pixel across and a pixel down
        const int at = em->grad_count++;
        sb *const out = em->o;
        const int defuse = em->defuse;
        const expr *statement = em->statement;
        em->o = &em->grads;
        em->defuse = 0;
        em->statement = NULL;
        em->lifting++;
        sb_printf(em->o, em->l == WGSL ? "    let tide_uv%d: vec2f = " : "    vec2 tide_uv%d = ", at);
        ex_as(em, e->args.items[0], (type){TY_FLOAT2, NULL});
        if (em->l == WGSL) {
            sb_printf(em->o, ";\n    let tide_dx%d: vec2f = dpdx(tide_uv%d);\n    let tide_dy%d: vec2f = dpdy(tide_uv%d);\n", at, at, at, at);
        } else {
            sb_printf(em->o, ";\n    vec2 tide_dx%d = dFdx(tide_uv%d);\n    vec2 tide_dy%d = dFdy(tide_uv%d);\n", at, at, at, at);
        }
        em->lifting--;
        em->o = out;
        em->defuse = defuse;
        em->statement = statement;
        if (em->l == WGSL) sb_printf(em->o, "textureSampleGrad(pixels%d, sampler%d, ", slot, slot);
        else sb_printf(em->o, "textureGrad(pixels%d, ", slot);
        ex_as(em, e->args.items[0], (type){TY_FLOAT2, NULL});
        sb_printf(em->o, ", tide_dx%d, tide_dy%d)%s", at, at, straight ? ")" : "");
        return;
    }
    if (em->l == WGSL) sb_printf(em->o, "textureSampleLevel(pixels%d, sampler%d, ", slot, slot);
    else sb_printf(em->o, "textureLod(pixels%d, ", slot);
    ex_as(em, e->args.items[0], (type){TY_FLOAT2, NULL});
    // A depth is one number: WGSL gives it as that, at a level that's an int, and GLSL as a color's red
    sb_put(em->o, straight ? ", 0.0))" : read == READ_COLOR ? ", 0.0)" : em->l == WGSL ? ", 0)" : ", 0.0).r");
}

// Whether a function's parameter is a list or a grid, which it's written for and not handed.
static bool takes_data(const param *p)
{
    return (p->type.kind == TY_LIST || p->type.kind == TY_GRID) && p->type.decl;
}

// A call of a function or method of the game's. A mut argument is the
// caller's variable: GLSL takes it as it is (inout), and WGSL a pointer to a
// whole variable, so what isn't one goes through a copy, which only a call
// that's a statement of its own can do.
static void ex_user_call(emit *em, const expr *e)
{
    decl *fn = e->method;
    // The lists and grids it's called with, which are no arguments: the function is written for them
    data_ref *spec = arena_alloc((size_t)(fn->params.count + 1) * sizeof(data_ref));
    sb suffix = {0};
    for (int i = 0; i < fn->params.count; i++) {
        if (!takes_data(&fn->params.items[i])) continue;
        spec[i] = (data_ref){.texture = -1, .element = data_element(fn->params.items[i].type),
                             .grid = fn->params.items[i].type.kind == TY_GRID};
        const expr *arg = i < e->args.count ? e->args.items[i] : NULL;
        if (arg && !data_of(em, arg, &spec[i])) type_fits(em, arg->type, arg->at);
        sb_printf(&suffix, "%s%d", suffix.data ? "x" : "_4", spec[i].texture < 0 ? 0 : spec[i].texture);
    }
    need_function(em, fn, e->at, spec, suffix.data ? suffix.data : "");
    const bool method = fn->owner && !fn->is_operator && !fn->is_interpolate;
    const bool statement = em->statement == e;
    sb_printf(em->o, "%s%s(", function_name(fn), suffix.data ? suffix.data : "");
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
        } else if (em->l == GLSL && !in_array(receiver)) {
            ex(em, receiver);
        } else if (em->l == GLSL && statement) {
            sb_put(em->o, copy_name(-1));
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
        if (p && takes_data(p)) continue;
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
        if (em->l == GLSL && !in_array(arg)) {
            ex(em, arg);
        } else if (em->l == GLSL) {
            sb_put(em->o, copy_name(i));
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
    case CALL_RANDOM:
    case CALL_ROLL:
        refuse(em, e->at, "the GPU can't roll random numbers: a Random keeps its state where the CPU steps it");
        diag_note("roll in the view or a system, and keep the number in a field the GPU reads");
        return;
    case CALL_BUILTIN:
        if (e->kind == E_METHOD && e->object->kind == E_NAME && str_eq_c(e->object->name, "Math")) {
            ex_math(em, e);
            return;
        }
        if (e->kind == E_CALL && e->type.kind == TY_STRUCT && e->type.decl == em->prog->random) { // Random(seed)
            refuse(em, e->at, "the GPU can't make a Random: it keeps its state where the CPU steps it");
            diag_note("roll in the view or a system, and keep the number in a field the GPU reads");
            return;
        }
        if (e->kind == E_METHOD && e->object->kind == E_NAME && ex_builder(em, e)) return;
        if (e->kind == E_METHOD && e->object->kind == E_NAME && str_eq_c(e->object->name, "Debug")) {
            refuse(em, e->at, "Debug." STR_FMT " speaks from the CPU: code the GPU runs has nowhere to print", STR_ARG(e->name));
            diag_note("to look at a value, write it to the pixel's color");
            return;
        }
        refuse(em, e->at, "'" STR_FMT "' isn't on the GPU yet", STR_ARG(e->name));
        diag_note("work it out on the CPU, in the view, and keep the result in a component or a singleton the code reads");
        return;
    case CALL_GRID:
        if (str_eq_c(e->name, "Sample") || (str_eq_c(e->name, "Compare") && e->object->type.kind == TY_DEPTH_TEXTURE)) {
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
        if (e->io_operation || e->path_operation || e->storage_flush || e->web_fetch) {
            refuse(em, e->at, e->web_fetch ? "the network belongs to local CPU code: fetch before drawing"
                                           : "file and directory access belongs to local CPU code: read it before drawing");
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
        // A number with a minus before it is that number, which no compiler folds into an error
        const bool folds = is_const(e->lhs) && !negated_literal(e);
        if (folds) em->defuse++;
        if (e->op == T_MINUS && matrix_dim(e->type) && em->l == WGSL) { // WGSL negates no matrix
            sb_put(em->o, "(");
            ex(em, e->lhs);
            sb_put(em->o, " * -1.0)");
        } else {
            sb_put(em->o, e->op == T_NOT ? "(!" : e->op == T_TILDE ? "(~" : "(-");
            ex(em, e->lhs);
            sb_put(em->o, ")");
        }
        if (folds) em->defuse--;
        return;
    case E_CONDITIONAL:
        if (!type_fits(em, e->type, e->at)) return;
        if (em->l == WGSL || em->checking) {
            // WGSL's select picks between numbers, vectors and bools, and works both sides out
            if (is_struct_like(e->type) || matrix_dim(e->type) || e->type.kind == TY_ARRAY) {
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
    case E_INDEX: {
        data_ref data;
        if (data_of(em, e->object, &data)) { // items[i], cells[x, y]: an element by its place
            need_data(em, data.texture, data.element, data.grid);
            sb_printf(em->o, "tide_list%d(", data.texture < 0 ? 0 : data.texture);
            ex_as(em, e->lhs, (type){data.grid ? TY_INT2 : TY_INT, NULL});
            sb_put(em->o, ")");
            return;
        }
        if (e->object->type.kind == TY_ARRAY) {
            ex_array_index(em, e);
            return;
        }
        if (e->object->type.kind == TY_GRID || e->object->type.kind == TY_LIST) {
            type_fits(em, e->object->type, e->object->at);
            return;
        }
        refuse(em, e->at, "the GPU indexes arrays, and lists and grids that are fields of what the frag or vert takes: it "
                          "has no maps");
        return;
    }
    case E_LIST:
        if (e->type.kind == TY_ARRAY) { // An array's elements, written out
            if (!type_fits(em, e->type, e->at)) return;
            sb_printf(em->o, "%s(", ty(em, e->type));
            for (int i = 0; i < e->args.count; i++) {
                if (i) sb_put(em->o, ", ");
                ex_as(em, e->args.items[i], e->type.decl->fields.items[0].type);
            }
            sb_put(em->o, ")");
            return;
        }
        refuse(em, e->at, "the GPU makes no lists: it reads the ones that are fields of what the frag or vert takes");
        return;
    case E_THIS:
        refuse(em, e->at, "a frag's pixel is read through 'this.position', 'this.depth' and 'this.world': it's not a value "
                          "to keep or pass on");
        return;
    case E_NULL:
    case E_COALESCE:
    case E_IS:
    case E_TRY:
    case E_DEFAULTED:
        refuse(em, e->at, "the GPU has no values that may be missing, and nothing fails there: handle it on the CPU");
        return;
    case E_STEP: // Ran before its statement, into a variable (st_steps)
        if (e->hoisted) sb_put(em->o, e->hoisted);
        else refuse(em, e->at, "'%s' can't be part of this expression in code the GPU runs: write it as a statement of its own before it",
                    e->op == T_PLUS_PLUS ? "++" : "--");
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
    case S_EXPR: // this.Discard()
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
    if (s->type.kind == TY_LIST || s->type.kind == TY_GRID) { // A name for one the code was given: nothing is written
        const char *what = s->type.kind == TY_LIST ? "list" : "grid";
        const loc at = s->name_at.line ? s->name_at : s->at;
        data_alias alias = {.local = s};
        alias.data = (data_ref){.texture = -1, .element = data_element(s->type), .grid = s->type.kind == TY_GRID};
        if (s->is_mut) {
            refuse(em, at, "a %s's local in code the GPU runs names one %s, for good: '" STR_FMT "' can't be 'mut'", what, what,
                   STR_ARG(s->name));
            diag_note("which %s it reads is settled as the shader is written, so nothing picks another as it runs: read each "
                      "under its own name", what);
        } else if (!s->value || !data_of(em, s->value, &alias.data)) {
            type_fits(em, s->type, s->value ? s->value->at : at);
        }
        vec_push(em->aliases, alias); // Said once: what reads it says nothing more
        return;
    }
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
    } else if (t.kind == TY_BYTE) { // What a byte holds of the sum: its low 8 bits
        sb_put(em->o, "(");
        put_binary(em, plain_op(s->op), s->target, s->value, (type){TY_INT, NULL}, s->at);
        sb_put(em->o, " & 255)");
    } else {
        put_binary(em, plain_op(s->op), s->target, s->value, t, s->at);
    }
}

// An assignment: a statement, or a for's step, which takes no ';' of its own.
static void st_assign(emit *em, const stmt *s, const bool statement)
{
    const expr *target = s->target;
    if (!type_fits(em, target->type, target->at)) return;
    if (statement && in_array(target) && em->raw == 0) { // An element its array hasn't got is written nowhere
        const int unknown = unknown_elements(target);
        if (unknown < 0) {
            sb_put(em->o, "{\n");
            line(em);
            sb_put(em->o, "}\n");
            return;
        }
        if (unknown > 0) {
            sb_put(em->o, "if (");
            put_in_range(em, target);
            sb_put(em->o, ") {\n");
            em->indent++;
            line(em);
        }
        em->raw = 1;
        st_assign(em, s, true);
        em->raw = 0;
        if (unknown > 0) {
            em->indent--;
            line(em);
            sb_put(em->o, "}\n");
        }
        return;
    }
    const int raw = em->raw; // The target is the place; what's stored is read like anything else
    em->raw = 0;
    // WGSL assigns one component at a time, not a swizzle of several
    if (em->l == WGSL && target->kind == E_MEMBER && target->swizzle_len > 1) {
        const int temp = em->temps++;
        sb_printf(em->o, "{ var tide_set%d: %s = ", temp, ty(em, target->type));
        put_assigned(em, s);
        sb_put(em->o, ";");
        for (int i = 0; i < target->swizzle_len; i++) {
            sb_put(em->o, " ");
            em->raw = raw;
            ex(em, target->object);
            em->raw = 0;
            sb_printf(em->o, ".%c = tide_set%d.%c;", "xyzw"[target->swizzle[i]], temp, "xyzw"[i]);
        }
        sb_put(em->o, statement ? " }\n" : " }");
        return;
    }
    em->raw = raw;
    ex(em, target);
    em->raw = 0;
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
    // WGSL hands over whole variables; GLSL anything that's a place, which an array's element isn't here
    const bool wgsl = em->l == WGSL;
#define COPIED(arg) (wgsl ? !is_whole_variable(arg) : in_array(arg))
    const expr *receiver = method && fn->is_mut_method && e->kind == E_METHOD && COPIED(e->object) ? e->object : NULL;
    bool copies = receiver != NULL;
    for (int i = 0; fn && i < e->args.count && i < fn->params.count; i++) {
        copies |= fn->params.items[i].mode == PARAM_MUT && COPIED(e->args.items[i]);
    }
    em->statement = e;
    if (!copies) {
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
        if (wgsl) sb_printf(em->o, "var %s: %s = ", copy_name(-1), ty(em, receiver->type));
        else sb_printf(em->o, "%s %s = ", ty(em, receiver->type), copy_name(-1));
        ex(em, receiver);
        sb_put(em->o, ";\n");
    }
    for (int i = 0; i < e->args.count && i < fn->params.count; i++) {
        if (fn->params.items[i].mode != PARAM_MUT || !COPIED(e->args.items[i])) continue;
        line(em);
        if (wgsl) sb_printf(em->o, "var %s: %s = ", copy_name(i), ty(em, e->args.items[i]->type));
        else sb_printf(em->o, "%s %s = ", ty(em, e->args.items[i]->type), copy_name(i));
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
        if (!to || (i >= 0 && (fn->params.items[i].mode != PARAM_MUT || !COPIED(to)))) continue;
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
#undef COPIED
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

// What a loop's own line says as it is: a number, or a variable.
static bool plain_operand(const expr *e)
{
    if (e->kind == E_INT || e->kind == E_FLOAT || negated_literal(e)) return true;
    return e->kind == E_NAME && (e->bind == BIND_LOCAL || (e->bind == BIND_PARAM && type_is_numeric(e->type)));
}

// A variable of the loop's own for `value`, worked out before the loop: its
// declaration, and `name` to stand for the value on the loop's line. A Tide
// name starts with no digit, so it's no local of the game's.
static void put_before_loop(emit *em, const expr *value, const type t, const char *what, char *text, stmt *local, expr *name)
{
    const int n = snprintf(text, 32, "0%s%d", what, em->temps++);
    *local = (stmt){.kind = S_VAR, .at = value->at, .name = {text, n}, .type = t};
    *name = (expr){.kind = E_NAME, .at = value->at, .name = local->name, .bind = BIND_LOCAL, .local = local, .type = t};
    line(em);
    if (em->l == WGSL) sb_printf(em->o, "var %s: %s = ", local_name(local->name), ty(em, t));
    else sb_printf(em->o, "%s %s = ", ty(em, t), local_name(local->name));
    ex_as(em, value, t);
    sb_put(em->o, ";\n");
}

// A loop that counts. Its own line has only numbers and variables: what it
// starts at, and what it goes to and steps by when those are constants, are
// worked out before it when they're anything more (a sum, a call, a constant
// of the game's), each once, as the line would. Shader compilers read how far
// a loop goes off that line, and one of them (the Android emulator's, for a
// loop that samples a texture) loses a function that only that line calls.
static void st_for(emit *em, const stmt *s)
{
    const stmt *v = s->init;
    const expr *c = s->cond;
    const bool left = c->lhs->kind == E_NAME && c->lhs->bind == BIND_LOCAL && c->lhs->local == v;
    const expr *bound = left ? c->rhs : c->lhs;
    const expr *by = s->step->value;
    const bool from_before = !plain_operand(v->value);
    const bool to_before = !plain_operand(bound) && is_const(bound); // What isn't a constant is worked out at each turn
    const bool by_before = by && !plain_operand(by) && is_const(by);
    stmt init = *v, step = *s->step, locals[3];
    expr cond = *c, names[3];
    char texts[3][32];
    if (from_before || to_before || by_before) {
        sb_put(em->o, "{\n");
        em->indent++;
        if (from_before) {
            put_before_loop(em, v->value, v->type, "from", texts[0], &locals[0], &names[0]);
            init.value = &names[0];
        }
        if (to_before) {
            put_before_loop(em, bound, bound->type, "to", texts[1], &locals[1], &names[1]);
            if (left) cond.rhs = &names[1];
            else cond.lhs = &names[1];
        }
        if (by_before) {
            put_before_loop(em, by, by->type, "by", texts[2], &locals[2], &names[2]);
            step.value = &names[2];
        }
        line(em);
    }
    sb_put(em->o, "for (");
    st_var(em, &init);
    sb_put(em->o, "; ");
    ex(em, &cond);
    sb_put(em->o, "; ");
    st_assign(em, &step, false);
    sb_put(em->o, ") ");
    st_body(em, s->then_stmt);
    sb_put(em->o, "\n");
    if (from_before || to_before || by_before) {
        em->indent--;
        line(em);
        sb_put(em->o, "}\n");
    }
}

// Whether an `out` argument in `e` declares a local: F(out var x).
static bool has_outs(const expr *e)
{
    if (!e) return false;
    if (e->out_local) return true;
    if (has_outs(e->object) || has_outs(e->lhs) || has_outs(e->rhs) || has_outs(e->cond)) return true;
    for (int i = 0; i < e->args.count; i++) {
        if (has_outs(e->args.items[i])) return true;
    }
    return false;
}

// The locals `out` arguments declare in an expression, declared before its
// statement, as the CPU's code has them: the call that makes each takes it as
// it takes a `mut` argument, a whole variable.
static void st_outs(emit *em, const expr *e)
{
    if (!e) return;
    if (e->out_local) {
        line(em);
        st_var(em, e->out_local);
        sb_put(em->o, ";\n");
        return;
    }
    st_outs(em, e->object);
    st_outs(em, e->cond);
    st_outs(em, e->lhs);
    st_outs(em, e->rhs);
    for (int i = 0; i < e->args.count; i++) st_outs(em, e->args.items[i]);
}

// The variable a step changes: the local or parameter its target is, or is a part of.
static const expr *step_variable(const expr *e)
{
    while (e->kind == E_MEMBER) e = e->object;
    return e->kind == E_NAME && (e->bind == BIND_LOCAL || e->bind == BIND_PARAM) ? e : NULL;
}

// Whether `e` names the variable `var` is, anywhere but in `own`.
static bool names_variable(const expr *e, const expr *var, const expr *own)
{
    if (!e || e == own) return false;
    if (e->kind == E_NAME && e->bind == var->bind && (var->bind == BIND_LOCAL ? e->local == var->local : e->param == var->param)) {
        return true;
    }
    if (names_variable(e->object, var, own) || names_variable(e->lhs, var, own) || names_variable(e->rhs, var, own)
        || names_variable(e->cond, var, own)) return true;
    for (int i = 0; i < e->args.count; i++) {
        if (names_variable(e->args.items[i], var, own)) return true;
    }
    for (int i = 0; i < e->inits.count; i++) {
        if (names_variable(e->inits.items[i].value, var, own)) return true;
    }
    return false;
}

static bool has_steps(const expr *e)
{
    if (!e) return false;
    if (e->kind == E_STEP) return true;
    if (has_steps(e->object) || has_steps(e->lhs) || has_steps(e->rhs) || has_steps(e->cond)) return true;
    for (int i = 0; i < e->args.count; i++) {
        if (has_steps(e->args.items[i])) return true;
    }
    for (int i = 0; i < e->inits.count; i++) {
        if (has_steps(e->inits.items[i].value)) return true;
    }
    return false;
}

typedef VEC(expr *) step_list;

// `++i` and `i++` in a statement's expression `e`, each run before the
// statement, in the order written, into a variable the statement then reads:
// WGSL has neither as a value, and GLSL leaves open which operand is worked
// out first. So that the value is the CPU's, the statement names the variable
// nowhere else, and the step isn't in a part that only runs sometimes.
// `sometimes`: `e` is one.
static void st_steps(emit *em, const stmt *s, expr *e, const bool sometimes, step_list *made)
{
    if (!e) return;
    if (e->kind == E_STEP) {
        const char *op = e->op == T_PLUS_PLUS ? "++" : "--";
        const expr *var = step_variable(e->lhs);
        if (sometimes) {
            refuse(em, e->at, "'%s' is in a part of its expression that only runs sometimes, which code the GPU runs can't "
                              "keep it to", op);
            diag_note("write it as an 'if' with the '%s' inside", op);
            e->hoisted = "0"; // Said already
            vec_push(*made, e);
            return;
        }
        if (!var || has_steps(e->lhs)) {
            refuse(em, e->at, "'%s' changes a local or a parameter in code the GPU runs", op);
            e->hoisted = "0"; // Said already
            vec_push(*made, e);
            return;
        }
        if (names_variable(s->cond, var, e->lhs) || names_variable(s->value, var, e->lhs) || names_variable(s->target, var, e->lhs)) {
            refuse(em, e->at, "in code the GPU runs, a statement with '%s' on '" STR_FMT "' reads it nowhere else", op,
                   STR_ARG(var->name));
            diag_note("write the '%s' as a statement of its own, before or after this one", op);
            e->hoisted = "0"; // Said already
            vec_push(*made, e);
            return;
        }
        sb name = {0};
        sb_printf(&name, "tide_step%d", em->temps++);
        for (int pass = 0; pass < 2; pass++) {
            line(em);
            if ((pass == 0) == e->postfix) {
                if (em->l == WGSL) sb_printf(em->o, "var %s: %s = ", name.data, ty(em, e->type));
                else sb_printf(em->o, "%s %s = ", ty(em, e->type), name.data);
                ex(em, e->lhs);
                sb_put(em->o, ";\n");
            } else {
                st_assign(em, e->step, true);
            }
        }
        e->hoisted = name.data;
        vec_push(*made, e);
        return;
    }
    const bool unit = e->kind == E_CONDITIONAL || (e->kind == E_BINARY && !e->method && (e->op == T_AND || e->op == T_OR));
    st_steps(em, s, e->object, sometimes, made);
    st_steps(em, s, e->cond, sometimes, made);
    st_steps(em, s, e->lhs, sometimes || e->kind == E_CONDITIONAL, made);
    st_steps(em, s, e->rhs, sometimes || unit, made);
    for (int i = 0; i < e->args.count; i++) st_steps(em, s, e->args.items[i], sometimes, made);
    for (int i = 0; i < e->inits.count; i++) st_steps(em, s, e->inits.items[i].value, sometimes, made);
}

static void st_one(emit *em, const stmt *s);

// A for that isn't one that counts (st_for): whatever it starts with, goes on
// while and steps by. GLSL's own for; in WGSL, whose for takes less on its
// line, a loop with its step where a `continue` still reaches it.
static void st_loop(emit *em, const stmt *s)
{
    if (em->l == WGSL) {
        sb_put(em->o, "{\n");
        em->indent++;
        if (s->init) st(em, s->init);
        line(em);
        sb_put(em->o, "loop {\n");
        em->indent++;
        if (s->cond) {
            line(em);
            sb_put(em->o, "if (!(");
            ex(em, s->cond);
            sb_put(em->o, ")) {\n");
            em->indent++;
            line(em);
            sb_put(em->o, "break;\n");
            em->indent--;
            line(em);
            sb_put(em->o, "}\n");
        }
        line(em);
        st_body(em, s->then_stmt);
        sb_put(em->o, "\n");
        if (s->step) {
            line(em);
            sb_put(em->o, "continuing {\n");
            em->indent++;
            st(em, s->step);
            em->indent--;
            line(em);
            sb_put(em->o, "}\n");
        }
        em->indent--;
        line(em);
        sb_put(em->o, "}\n");
        em->indent--;
        line(em);
        sb_put(em->o, "}\n");
        return;
    }
    if (s->step && s->step->kind == S_ASSIGN && in_array(s->step->target)) {
        refuse(em, s->step->at, "a for's step that writes an array's element is written at the end of the loop's body in code "
                                "the GPU runs");
        return;
    }
    sb_put(em->o, "for (");
    if (s->init && s->init->kind == S_VAR) st_var(em, s->init);
    else if (s->init && s->init->kind == S_ASSIGN) st_assign(em, s->init, false);
    else if (s->init && s->init->kind == S_EXPR) ex(em, s->init->value);
    sb_put(em->o, "; ");
    if (s->cond) ex(em, s->cond);
    sb_put(em->o, "; ");
    if (s->step && s->step->kind == S_ASSIGN) st_assign(em, s->step, false);
    else if (s->step && s->step->kind == S_EXPR) ex(em, s->step->value);
    sb_put(em->o, ") ");
    st_body(em, s->then_stmt);
    sb_put(em->o, "\n");
}

// A foreach over a list the engine supplies: a loop that counts its elements,
// each read as the loop comes to it.
static void st_foreach(emit *em, const stmt *s, const data_ref *list)
{
    const bool wgsl = em->l == WGSL;
    const int k = em->temps++;
    if (!list) { // An array: each of its elements, by its place
        const type array = s->value->type, element = array.decl->fields.items[0].type;
        if (!type_fits(em, array, s->value->at)) return;
        const int reader = need_array(em, array.decl);
        sb_printf(em->o, wgsl ? "for (var tide_k%d: i32 = 0; tide_k%d < %d; tide_k%d++) {\n" : "for (int tide_k%d = 0; tide_k%d < %d; tide_k%d++) {\n",
                  k, k, array.decl->array_length, k);
        em->indent++;
        line(em);
        if (wgsl) sb_printf(em->o, "var %s: %s = tide_el%d(", local_name(s->name), ty(em, element), reader);
        else sb_printf(em->o, "%s %s = tide_el%d(", ty(em, element), local_name(s->name), reader);
        ex(em, s->value);
        sb_printf(em->o, ", tide_k%d);\n", k);
    } else {
        const int texture = list->texture;
        const type element = list->element;
        need_data(em, texture, element, false);
        sb_printf(em->o, wgsl ? "for (var tide_k%d: i32 = 0; tide_k%d < " : "for (int tide_k%d = 0; tide_k%d < ", k, k);
        put_list_count(em, texture);
        sb_printf(em->o, "; tide_k%d++) {\n", k);
        em->indent++;
        line(em);
        if (wgsl) sb_printf(em->o, "var %s: %s = tide_list%d(tide_k%d);\n", local_name(s->name), ty(em, element), texture < 0 ? 0 : texture, k);
        else sb_printf(em->o, "%s %s = tide_list%d(tide_k%d);\n", ty(em, element), local_name(s->name), texture < 0 ? 0 : texture, k);
    }
    const stmt *body = s->then_stmt;
    for (int i = 0; body && body->kind == S_BLOCK && i < body->stmts.count; i++) {
        st(em, body->stmts.items[i]);
        if (ends(body->stmts.items[i])) break;
    }
    if (body && body->kind != S_BLOCK) st(em, body);
    em->indent--;
    line(em);
    sb_put(em->o, "}\n");
}

// A switch on an int or an enum, as each language has its own: a section's
// labels are constants, and no section goes on into the next.
static void st_switch(emit *em, const stmt *s)
{
    const bool wgsl = em->l == WGSL;
    if (s->cond->type.kind != TY_INT && s->cond->type.kind != TY_ENUM) {
        refuse(em, s->cond->at, "a switch the GPU runs is on an int or an enum, and this is %s", type_name(s->cond->type));
        diag_note("write it as 'if' and 'else if'");
        return;
    }
    sb_put(em->o, wgsl ? "switch " : "switch (");
    ex(em, s->cond);
    sb_put(em->o, wgsl ? " {\n" : ") {\n");
    em->indent++;
    bool has_default = false;
    for (int i = 0; i < s->cases.count; i++) {
        const switch_case *section = &s->cases.items[i];
        line(em);
        if (wgsl) {
            bool first = true, cases = false;
            for (int k = 0; k < section->labels.count; k++) cases |= section->labels.items[k] != NULL;
            if (cases) sb_put(em->o, "case ");
            for (int k = 0; k < section->labels.count; k++) {
                const expr *label = section->labels.items[k];
                if (!first) sb_put(em->o, ", ");
                first = false;
                if (label) sb_printf(em->o, "%lld", (long long)(label->kind == E_INT ? int_literal(label) : label->int_value));
                else sb_put(em->o, "default");
                has_default |= !label;
            }
            sb_put(em->o, " {\n");
        } else {
            for (int k = 0; k < section->labels.count; k++) {
                const expr *label = section->labels.items[k];
                if (label) sb_printf(em->o, "case %lld: ", (long long)(label->kind == E_INT ? int_literal(label) : label->int_value));
                else sb_put(em->o, "default: ");
                has_default |= !label;
            }
            sb_put(em->o, "{\n");
        }
        em->indent++;
        bool ended = false;
        for (int k = 0; k < section->body.count && !ended; k++) {
            st(em, section->body.items[k]);
            ended = ends(section->body.items[k]);
        }
        em->indent--;
        line(em);
        sb_put(em->o, wgsl || ended ? "}\n" : "} break;\n");
    }
    if (!has_default) { // WGSL wants one, and GLSL a statement after its last label
        line(em);
        sb_put(em->o, wgsl ? "default {\n" : "default: {\n");
        line(em);
        sb_put(em->o, wgsl ? "}\n" : "} break;\n");
    }
    em->indent--;
    line(em);
    sb_put(em->o, "}\n");
}

static void st(emit *em, const stmt *s)
{
    step_list made = {0};
    if (s->kind == S_FOR || s->kind == S_WHILE) { // Its line runs again each round, where a statement before it runs once
        const stmt *v = s->init;
        if (has_steps(s->cond) || (v && (has_steps(v->value) || has_steps(v->target)))
            || (s->step && (has_steps(s->step->value) || has_steps(s->step->target)))) {
            refuse(em, s->at, "a loop's own line has no '++' or '--' inside an expression in code the GPU runs");
            diag_note("a for's step can be 'i++'; change any other variable in the loop's body");
            return;
        }
    } else if (s->kind != S_BLOCK && (has_steps(s->target) || has_steps(s->value) || has_steps(s->cond))) {
        st_steps(em, s, s->target, false, &made);
        st_steps(em, s, s->cond, false, &made);
        st_steps(em, s, s->value, false, &made);
    }
    st_one(em, s);
    for (int i = 0; i < made.count; i++) made.items[i]->hoisted = NULL; // The CPU's code makes its own
}

static void st_one(emit *em, const stmt *s)
{
    if (s->kind == S_BLOCK) {
        line(em);
        st_body(em, s);
        sb_put(em->o, "\n");
        return;
    }
    if (s->kind == S_FOR) {
        const stmt *v = s->init;
        const stmt *step = s->step;
        if (has_outs(s->cond) || (v && has_outs(v->value)) || (step && (has_outs(step->value) || has_outs(step->target)))) {
            refuse(em, s->at, "a for the GPU runs declares nothing with 'out' on its own line");
            diag_note("make the call before the loop, or in its body");
            return;
        }
    } else {
        st_outs(em, s->cond);
        st_outs(em, s->value);
        st_outs(em, s->target);
    }
    if (s->kind == S_VAR && (s->type.kind == TY_LIST || s->type.kind == TY_GRID)) { // A name: no line of its own
        st_var(em, s);
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
        if (counts(s)) st_for(em, s);
        else st_loop(em, s);
        break;
    case S_BREAK:
        sb_put(em->o, "break;\n");
        break;
    case S_CONTINUE:
        sb_put(em->o, "continue;\n");
        break;
    case S_WHILE:
        sb_put(em->o, "while (");
        ex(em, s->cond);
        sb_put(em->o, ") ");
        st_body(em, s->then_stmt);
        sb_put(em->o, "\n");
        break;
    case S_FOREACH: {
        data_ref data;
        if (s->value && s->value->type.kind == TY_LIST) {
            if (data_of(em, s->value, &data)) st_foreach(em, s, &data);
            else type_fits(em, s->value->type, s->value->at);
            break;
        }
        if (s->value && s->value->type.kind == TY_ARRAY) {
            st_foreach(em, s, NULL);
            break;
        }
        refuse(em, s->at, "a foreach the GPU runs goes through an array or a list, like 'foreach (var light in lights.items)': "
                          "it has no maps, sets, queries or grids to go through");
        break;
    }
    case S_PARALLEL:
        refuse(em, s->at, "the GPU has no grids to go through: it samples a grid of colors with 'grid.Sample(uv)'");
        break;
    case S_SWITCH:
        st_switch(em, s);
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
static void need_function(emit *em, decl *fn, const loc at, const data_ref *spec, const char *suffix)
{
    if (em->checking && fn->gpu_state >= 2) { // What it reads of assets by their names, its caller reads
        for (int i = 0; i < fn->gpu_data.count; i++) note_asset(em, fn->gpu_data.items[i]);
    }
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
        if (em->done.items[i].fn == fn && !strcmp(em->done.items[i].suffix, suffix)) return;
    }
    const shader_function written = {fn, suffix};
    vec_push(em->done, written);
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
    const sb grads = em->grads;
    const int grad_count = em->grad_count, hoisted = em->hoisted.count;
    em->grads = (sb){0};
    em->grad_count = 0;
    em->hoisted.count = 0; // A function's own: only a frag or vert has any, and it gets its own back
    const int indent = em->indent, defuse = em->defuse, aliases = em->aliases.count;
    const expr *statement = em->statement;
    const bool failed = em->failed;
    const data_ref *const outer_spec = em->spec;
    em->spec = spec;
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
    if (wgsl) sb_printf(&head, "fn %s%s(", function_name(fn), suffix);
    else sb_printf(&head, "%s %s%s(", value ? ty(em, fn->return_type) : "void", function_name(fn), suffix);
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
        if (shader && !p->function_param && !handed_to(em->sh, p)) continue; // The engine supplies it
        if (spec && takes_data(p)) { // Which of the shader's lists or grids it is, this is written for
            if (p->mode == PARAM_MUT) {
                refuse(em, p->at, "a %s a function takes in code the GPU runs is read there, never changed: '" STR_FMT "' can't be "
                                  "'%s'", p->type.kind == TY_LIST ? "list" : "grid", STR_ARG(p->name), p->is_out ? "out" : "mut");
            }
            continue;
        }
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
    em->spec = outer_spec;
    em->aliases.count = aliases;
    if (em->checking) { // What it reads of assets by their names, its caller reads
        for (int i = 0; i < fn->gpu_data.count; i++) note_asset(em, fn->gpu_data.items[i]);
    }
    em->indent = indent;
    em->defuse = defuse;
    em->statement = statement;
    em->failed = failed || bad;
    sb_put(&em->functions, head.data);
    if (em->grads.data) sb_put(&em->functions, em->grads.data); // What its Samples ask as it starts
    em->grads = grads;
    em->grad_count = grad_count;
    em->hoisted.count = hoisted;
    if (body.data) sb_put(&em->functions, body.data);
    sb_put(&em->functions, "}\n\n");
}

// ---------------------------------------------------------------------------
// A shader's sources

static int packs(const int words)
{
    return (words + 3) / 4;
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
    sb_put(o, wgsl ? "struct tide_uniforms {\n    tide_camera: mat4x4f,\n    tide_screen: vec4f,\n    tide_tint: vec4f,\n"
                   : "layout(std140) uniform uniforms {\n    mat4 tide_camera;\n    vec4 tide_screen;\n    vec4 tide_tint;\n");
    for (int i = 0; i < packs(sh->uniform_floats); i++) sb_printf(o, wgsl ? "    tide_u%d: vec4f,\n" : "    vec4 tide_u%d;\n", i);
    for (int i = 0; i < packs(sh->uniform_ints); i++) sb_printf(o, wgsl ? "    tide_v%d: vec4i,\n" : "    ivec4 tide_v%d;\n", i);
    sb_put(o, wgsl ? "}\n@group(0) @binding(0) var<uniform> tide_block: tide_uniforms;\n" : "};\n");
    for (int i = 0; i < sh->textures.count; i++) {
        const shader_read read = sh->textures.items[i].read;
        if (read == READ_DATA) { // A list's numbers, with all of their bits, read through no sampler
            sb_printf(o, wgsl ? "@group(0) @binding(%d) var pixels%d: texture_2d<i32>;\n" : "uniform highp isampler2D pixels%d;\n",
                      wgsl ? 1 + 2 * i : i, i);
        } else if (wgsl) {
            sb_printf(o, "@group(0) @binding(%d) var pixels%d: %s;\n@group(0) @binding(%d) var sampler%d: %s;\n",
                      1 + 2 * i, i, read == READ_COLOR ? "texture_2d<f32>" : read == READ_CUBE ? "texture_cube<f32>" : "texture_depth_2d",
                      2 + 2 * i, i, read == READ_COMPARE ? "sampler_comparison" : "sampler");
        } else if (read == READ_CUBE) {
            sb_printf(o, "uniform highp samplerCube pixels%d;\n", i); // With all of its bits, for a cube of floats
        } else if (read != READ_COLOR) {
            // With all of its bits: a sampler's own precision is the lowest there is, where there's a choice
            sb_printf(o, "uniform highp %s pixels%d;\n", read == READ_COMPARE ? "sampler2DShadow" : "sampler2D", i);
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
// it: the vertex's fields the pixels read, the fields they read of what the
// vert wrote for them, where they are in the world, and the instance's
// float4s and int4s, which don't blend.
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
    for (int h = 0; h < sh->handed.count; h++) {
        const shader_handed *handed = &sh->handed.items[h];
        for (int i = 0; i < handed->type->fields.count; i++) {
            if (!handed->fields[i]) continue;
            const char *t = ty(em, as_float(handed->type->fields.items[i].type));
            if (wgsl) sb_printf(o, "    @location(%d) tide_h%d_%d: %s,\n", location++, h, i, t);
            else sb_printf(o, "%s %s tide_h%d_%d;\n", direction, t, h, i);
        }
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
    // What the vert writes for the frag: a variable each, in the order it takes them
    for (int h = 0; h < sh->handed.count; h++) {
        const type handed = {TY_STRUCT, (decl *)sh->handed.items[h].type};
        if (wgsl) sb_printf(o, "    var tide_handed%d: %s = ", h, ty(em, handed));
        else sb_printf(o, "    %s tide_handed%d = ", ty(em, handed), h);
        put_zero(em, handed);
        sb_put(o, ";\n");
    }
    if (sh->vert) {
        sb_printf(o, "    %s(%stide_vertex", function_name(sh->vert), wgsl ? "&" : "");
        for (int h = 0; h < sh->handed.count; h++) sb_printf(o, ", %stide_handed%d", wgsl ? "&" : "", h);
        sb_put(o, ");\n");
    }
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
    for (int h = 0; h < sh->handed.count; h++) {
        const shader_handed *handed = &sh->handed.items[h];
        for (int i = 0; i < handed->type->fields.count; i++) {
            if (!handed->fields[i]) continue;
            sb_printf(o, "    %stide_h%d_%d = tide_handed%d.%s;\n", out, h, i, h, field_name(&handed->type->fields.items[i]));
        }
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

// How many colors a shader's fragment stage writes: its frag's `out Color`
// parameters, or the one a mesh drawn with no frag has.
static int shader_outputs(const shader *sh)
{
    return sh->frag && sh->frag->outputs > 1 ? sh->frag->outputs : 1;
}

// The variable the fragment stage has its output `k` in (`pixel` false), and
// what GLSL writes it to (true): the pixel's color first, which has the name
// there always was.
static const char *output_name(const bool pixel, const int k)
{
    static char names[2][4][16];
    const char *stem = pixel ? "tide_pixel" : "tide_color";
    if (k == 0) return stem;
    snprintf(names[pixel][k & 3], sizeof names[0][0], "%s%d", stem, k);
    return names[pixel][k & 3];
}

// The fragment stage's own: the vertex as the pixel sees it, blended, then
// the frag, or with none, the vertex's color times the texture. The frag is
// called with what it takes as values, in its parameters' order: its vertex,
// what the vert wrote for it, blended too, and the colors it writes, each of
// which the checker saw it write on every path that ends (the white they
// start as here is for the shader's compiler, which didn't). What it leaves
// is the draw's to finish: each times its tint (Draw.Tint), then its color
// times its alpha, which is how every blend takes it (see tide_gpu_blend),
// and for a draw that multiplies what's there (Blend.Multiply, which
// `tide_screen.w` says), towards white as it clears, so what's clear changes
// nothing. A frag's
// outputs go out at the locations of their order, the pixel's color at 0.
static void put_fragment_main(emit *em, sb *o, const char *in, const char *result)
{
    const shader *sh = em->sh;
    const bool wgsl = em->l == WGSL;
    const decl *v = sh->vertex;
    const char *f4 = float_ty(em, 4);
    const int outputs = shader_outputs(sh);
    em->stage = STAGE_FRAG;
    em->o = o;
    for (int k = 0; k < outputs; k++) {
        sb_printf(o, wgsl ? "    var %s: vec4f = %s(1.0, 1.0, 1.0, 1.0);\n" : "    vec4 %s = %s(1.0, 1.0, 1.0, 1.0);\n",
                  output_name(false, k), f4);
    }
    if (sh->frag) {
        const type vertex = {TY_STRUCT, (decl *)v};
        sb_printf(o, "    %s(%s(", function_name(sh->frag), ty(em, vertex));
        for (int i = 0; i < v->fields.count; i++) {
            if (i) sb_put(o, ", ");
            if (sh->frag_fields[i]) sb_printf(o, "%stide_s%d", in, i);
            else put_zero(em, v->fields.items[i].type);
        }
        sb_put(o, ")");
        for (int i = 1; i < sh->frag->params.count; i++) {
            const param *p = &sh->frag->params.items[i];
            const shader_handed *handed = handed_to(sh, p);
            if (handed) {
                sb_printf(o, ", %s(", ty(em, p->type));
                bool first = true;
                for (int k = 0; k < handed->type->fields.count; k++) {
                    const field *f = &handed->type->fields.items[k];
                    if (f->hidden) continue;
                    if (!first) sb_put(o, ", ");
                    first = false;
                    if (handed->fields[k]) sb_printf(o, "%stide_h%d_%d", in, (int)(handed - sh->handed.items), k);
                    else put_zero(em, f->type);
                }
                sb_put(o, ")");
            } else if (p->function_param) { // One of the colors it writes
                sb_printf(o, ", %s%s", wgsl ? "&" : "", output_name(false, p->output));
            }
        }
        sb_put(o, ");\n");
    } else {
        const field *color = field_called(v, "color");
        const field *uv = field_called(v, "uv");
        if (color) sb_printf(o, "    tide_color = %stide_s%d;\n", in, field_index(v, color));
        int texture = 0;
        while (texture < sh->textures.count
               && (sh->textures.items[texture].source || sh->textures.items[texture].direct || sh->textures.items[texture].asset)) {
            texture++;
        }
        if (sh->textured) need(em, H_STRAIGHT); // The color itself, where the texture has it times its alpha
        // The draw's own texture is read as it says. In a game that names Filter.Trilinear that can be between
        // its smaller images, which the GPU picks by itself here, before any branch
        if (sh->textured && em->prog->names_trilinear && wgsl) {
            sb_printf(o, "    tide_color = tide_color * tide_straight(textureSample(pixels%d, sampler%d, %stide_s%d));\n",
                      texture, texture, in, field_index(v, uv));
        } else if (sh->textured && em->prog->names_trilinear) {
            sb_printf(o, "    tide_color = tide_color * tide_straight(texture(pixels%d, %stide_s%d));\n", texture, in, field_index(v, uv));
        } else if (sh->textured && wgsl) {
            sb_printf(o, "    tide_color = tide_color * tide_straight(textureSampleLevel(pixels%d, sampler%d, %stide_s%d, 0.0));\n",
                      texture, texture, in, field_index(v, uv));
        } else if (sh->textured) {
            sb_printf(o, "    tide_color = tide_color * tide_straight(textureLod(pixels%d, %stide_s%d, 0.0));\n", texture, in, field_index(v, uv));
        }
    }
    for (int k = 0; k < outputs; k++) {
        const char *color = output_name(false, k);
        if (wgsl) {
            sb_printf(o, "    %s = %s * tide_block.tide_tint;\n"
                         "    %s = vec4f(%s.rgb * %s.a, %s.a);\n"
                         "    if (tide_block.tide_screen.w > 0.0) {\n"
                         "        %s = vec4f(%s.rgb + (1.0 - %s.a), %s.a);\n"
                         "    }\n", color, color, color, color, color, color, color, color, color, color);
        } else {
            sb_printf(o, "    %s = %s * tide_tint;\n"
                         "    %s.rgb = %s.rgb * %s.a;\n"
                         "    if (tide_screen.w > 0.0) %s.rgb = %s.rgb + (1.0 - %s.a);\n",
                      color, color, color, color, color, color, color, color);
        }
    }
    if (outputs == 1) {
        sb_printf(o, "    %s tide_color;\n", result);
    } else if (wgsl) { // Its struct of them, in order
        sb_put(o, "    return tide_colors(");
        for (int k = 0; k < outputs; k++) sb_printf(o, "%s%s", k ? ", " : "", output_name(false, k));
        sb_put(o, ");\n");
    } else {
        for (int k = 0; k < outputs; k++) {
            const char *color = output_name(false, k);
            sb_printf(o, "    %s = ", output_name(true, k));
            sb_printf(o, "%s;\n", color);
        }
    }
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
    need_function(em, root, root->at, NULL, "");
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
            const int outputs = shader_outputs(sh);
            if (outputs == 1) sb_put(&o, "out vec4 tide_pixel;\n");
            // Several say where each goes: a target's color images, in order
            for (int k = 0; outputs > 1 && k < outputs; k++) {
                sb_printf(&o, "layout(location = %d) out vec4 %s;\n", k, output_name(true, k));
            }
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

    const int outputs = shader_outputs(sh);
    if (outputs == 1) {
        sb_put(&o, "@fragment fn fragment(in: tide_corner) -> @location(0) vec4f {\n    tide_at = in.tide_at;\n");
    } else { // Several colors, each to the color image of a target that's its place
        sb_put(&o, "struct tide_colors {\n");
        for (int k = 0; k < outputs; k++) sb_printf(&o, "    @location(%d) %s: vec4f,\n", k, output_name(false, k));
        sb_put(&o, "}\n@fragment fn fragment(in: tide_corner) -> tide_colors {\n    tide_at = in.tide_at;\n");
    }
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
    need_function(&em, d, d->at, NULL, "");
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

// A list the code reads, as one of the shader's textures.
static void add_list(shader *sh, const decl *source, const field *f, const bool bound)
{
    for (int i = 0; i < sh->textures.count; i++) {
        if (sh->textures.items[i].read == READ_DATA && sh->textures.items[i].source == source && sh->textures.items[i].field == f) return;
    }
    const shader_texture t = {.source = source, .field = f, .read = READ_DATA, .filter_said = true, .wrap_said = true};
    vec_push(sh->textures, t);
    if (bound) add_bound(sh, (decl *)source);
}

// What a frag or vert reads of the components and singletons it takes, and
// the grids it samples, into the shader.
static void plan_reads(const program *prog, shader *sh, const decl *code)
{
    if (!code) return;
    if (code->is_frag && code->pixel_world) sh->frag_world = true;
    for (int i = 1; i < code->params.count; i++) {
        const param *p = &code->params.items[i];
        if (p->type.decl == prog->instance && p->reads && p->reads[0] && sh->dims == 3) {
            shader_value *v = add_value(sh, NULL, NULL, (type){TY_FLOAT4X4, NULL}, true);
            if (code->is_frag) v->frag_reads = true;
        }
        if (p->type.decl == prog->pixel && p->reads && p->reads[2]) sh->frag_world = true;
        const bool explicit_instance = p->type.kind == TY_STRUCT && p->type.decl == sh->instance_type;
        // The struct the draw gives after its vert and frag: once for the draw, as a singleton's fields
        const bool given = p->type.kind == TY_STRUCT && !p->is_out && sh->values_type && p->type.decl == sh->values_type;
        if (p->type.kind != TY_COMPONENT && p->type.kind != TY_SINGLETON && !explicit_instance && !given) continue;
        for (int k = 0; k < p->type.decl->fields.count; k++) {
            const field *f = &p->type.decl->fields.items[k];
            if (f->hidden || !(p->whole || (p->reads && p->reads[k]))) continue;
            if (f->type.kind == TY_LIST) { // Its elements, as a texture of numbers
                if (explicit_instance) sh->instance_list = f;
                else add_list(sh, p->type.decl, f, !given);
                continue;
            }
            if (f->type.kind == TY_GRID || f->type.kind == TY_TEXTURE || f->type.kind == TY_DEPTH_TEXTURE
                || f->type.kind == TY_CUBEMAP) continue; // Sampled, below
            shader_value *v = add_value(sh, p->type.decl, f, f->type, p->type.kind == TY_COMPONENT || explicit_instance);
            if (code->is_frag) v->frag_reads = true;
            else v->vert_reads = true;
            if (!explicit_instance && !given) add_bound(sh, p->type.decl);
        }
        // Its local fields, which this machine keeps beside the match's
        const decl *part = p->type.decl->local_part;
        for (int k = 0; part && p->local_reads && k < part->fields.count; k++) {
            const field *f = &part->fields.items[k];
            if (p->local_reads[k] && f->type.kind == TY_LIST) {
                add_list(sh, p->type.decl, f, true);
                continue;
            }
            if (!p->local_reads[k] || f->type.kind == TY_GRID || f->type.kind == TY_TEXTURE || f->type.kind == TY_DEPTH_TEXTURE
                || f->type.kind == TY_CUBEMAP) continue;
            shader_value *v = add_value(sh, p->type.decl, f, f->type, p->type.kind == TY_COMPONENT);
            if (code->is_frag) v->frag_reads = true;
            else v->vert_reads = true;
            add_bound(sh, p->type.decl);
        }
    }
    for (int i = 0; i < code->samples.count; i++) {
        const shader_sample *s = &code->samples.items[i];
        const decl *source = s->asset ? NULL : code->params.items[s->param].type.decl;
        const shader_texture t = {.source = source,
                                  .field = s->field < 0 ? NULL : &(s->local ? source->local_part : source)->fields.items[s->field],
                                  .filter = s->filter, .direct = s->field < 0 && !s->asset, .asset = s->asset,
                                  .wrap = s->wrap, .read = s->read, .filter_said = s->filter_said, .wrap_said = s->wrap_said};
        bool known = false;
        for (int k = 0; k < sh->textures.count; k++) {
            known |= sh->textures.items[k].direct == t.direct && sh->textures.items[k].source == t.source && sh->textures.items[k].field == t.field
                  && sh->textures.items[k].filter == t.filter && sh->textures.items[k].asset == t.asset
                  && sh->textures.items[k].wrap == t.wrap && sh->textures.items[k].read == t.read
                  && sh->textures.items[k].filter_said == t.filter_said && sh->textures.items[k].wrap_said == t.wrap_said;
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
        if (sh->textures.items[i].source == d && sh->textures.items[i].field && sh->textures.items[i].field->is_local) return true;
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
    if (!per_instance && sh->values_type) {
        sb_printf(out, STR_FMT, STR_ARG(sh->values_type->name));
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
    for (int i = 0; i < prog->shaders.count; i++) {
        shader *s = prog->shaders.items[i];
        if (s->vertex == vertex && s->vert == call->vert && s->frag == call->frag && s->textured == textured
            && s->instanced == call->instanced && s->instance_type == call->instance_type
            && s->values_type == call->values_type
            && s->blit == call->blit && s->direct_texture == direct_texture) return s;
    }
    shader *sh = NEW(shader);
    sh->index = prog->shaders.count;
    sh->vertex = (decl *)vertex;
    sh->vert = call->vert;
    sh->frag = call->frag;
    sh->textured = textured;
    sh->blit = call->blit;
    sh->direct_texture = direct_texture;
    sh->instanced = call->instanced;
    sh->instance_type = call->instance_type;
    sh->values_type = call->values_type;
    const field *position = field_called(vertex, "position");
    sh->dims = position->type.kind == TY_FLOAT3 ? 3 : 2;
    if (sh->instance_type && sh->dims == 3) sh->instance_transform = field_called(sh->instance_type, "transform");
    sb name = {0}; // A vert or frag that shares its name goes with its parameters' types
    if (sh->vert) {
        if (sh->vert->signature) sb_printf(&name, "%s%s", sh->vert->signature, sh->frag ? " and " : " ");
        else sb_printf(&name, STR_FMT "%s", STR_ARG(sh->vert->name), sh->frag ? " and " : " ");
    }
    if (sh->frag && sh->frag->signature) sb_printf(&name, "%s ", sh->frag->signature);
    else if (sh->frag) sb_printf(&name, STR_FMT " ", STR_ARG(sh->frag->name));
    sb_printf(&name, "%s" STR_FMT, sh->vert || sh->frag ? "on " : "meshes of ", STR_ARG(vertex->name));
    sh->name = name.data;

    // What goes with each instance and each draw: the transform first
    if (sh->dims == 3) add_value(sh, NULL, NULL, (type){TY_FLOAT4X4, NULL}, true)->vert_reads = true;
    plan_reads(prog, sh, sh->vert);
    plan_reads(prog, sh, sh->frag);
    if (textured) {
        const shader_texture t = {.filter = 1}; // How it's read is the texture's to say, which the draw gives
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
    // How many elements each list it reads has: a word of the draw's ints
    for (int i = 0; i < sh->textures.count; i++) {
        shader_texture *t = &sh->textures.items[i];
        if (t->read != READ_DATA) continue;
        t->count_at = sh->uniform_ints; // A list's count, or a grid's size across and down
        sh->uniform_ints += (t->asset ? t->asset->return_type.kind : t->field->type.kind) == TY_GRID ? 2 : 1;
    }
    if (sh->instance_list) {
        diag_error(call->at, "this draw's instances each have a list, '" STR_FMT "', that its code reads, and a draw sends one "
                   "list for all of them", STR_ARG(sh->instance_list->name));
        diag_note("keep the list in a singleton or in the values the draw gives after its frag, and in each instance where "
                  "its part of the list starts and how many elements that is");
        return NULL;
    }
    // What the vert writes for the frag, each struct it takes as `out`, and what
    // of it the frag's parameter of that type reads: only that goes on
    int varyings = (sh->frag_world ? 1 : 0) + packs(sh->frag_floats) + packs(sh->frag_ints);
    for (int i = 1; sh->vert && i < sh->vert->params.count; i++) {
        const param *from = &sh->vert->params.items[i];
        if (!from->is_out || from->type.kind != TY_STRUCT) continue;
        shader_handed handed = {.type = from->type.decl, .from = from};
        handed.fields = arena_alloc((size_t)(handed.type->fields.count + 1) * sizeof(bool));
        for (int k = 1; sh->frag && k < sh->frag->params.count; k++) {
            const param *to = &sh->frag->params.items[k];
            if (to->type.kind != TY_STRUCT || to->type.decl != handed.type || to->mode == PARAM_MUT) continue;
            handed.to = to;
            for (int f = 0; f < handed.type->fields.count; f++) {
                handed.fields[f] = !handed.type->fields.items[f].hidden && (to->whole || (to->reads && to->reads[f]));
                varyings += handed.fields[f];
            }
        }
        vec_push(sh->handed, handed);
    }
    // What the pixels read of the vertex: the frag's fields, or with none, its color and its uv
    sh->frag_fields = arena_alloc((size_t)(vertex->fields.count + 1) * sizeof(bool));
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
        diag_note("each field of the vertex the frag reads is one, each it reads of what the vert wrote for it another, its "
                  "place in the world another, and every 4 floats or 4 ints it reads of the entity's components another: read "
                  "fewer, or keep what every entity shares in a singleton");
        return NULL;
    }
    if (uniform_bytes > 3984) { // TIDE_SHADER_UNIFORM_BYTES
        sb from = {0};
        list_bound(sh, &from, false);
        diag_error(call->at, "this draw reads %d bytes of singletons%s (%s), and a draw's hold 3984", uniform_bytes,
                   sh->values_type ? " and of the values it gives" : "", from.data ? from.data : "");
        diag_note("read fewer of their fields in the frag and the vert: only what they read is sent");
        return NULL;
    }
    if (sh->textures.count > 8) {
        diag_error(call->at, "this draw samples %d textures, and a draw takes 8", sh->textures.count);
        diag_note("a texture sampled with another filter or another wrap counts again, and so does a depth that's both read "
                  "and compared, and each list the code reads is one; put several images side by side in one texture");
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
        // A texture is the value argument after the indices, or after a 3D mesh's transform: a Texture, or a
        // grid, which is one there
        bool textured = false, direct_texture = call->blit;
        for (int a = 2; !call->blit && a < call->args.count; a++) {
            const type_kind kind = call->args.items[a]->type.kind;
            textured |= !call->frag && (kind == TY_GRID || kind == TY_TEXTURE);
            direct_texture |= kind == TY_GRID || kind == TY_TEXTURE;
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
                bool takes = p->type.decl == d && (p->type.kind == TY_COMPONENT || p->type.kind == TY_SINGLETON)
                          && (p->mode == PARAM_READ || p->mode == PARAM_MUT);
                // What's drawn is the entity as the view draws it: a component it
                // takes from the confirmed world beside that isn't the mesh's
                if (view->verified_join && p->verified && p->type.kind == TY_COMPONENT) takes = false;
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
