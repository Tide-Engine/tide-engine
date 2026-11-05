#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "hlsl.h"
#include "spirv.h"
#include "types.h"

// The most textures a draw or a run samples (tide/draw.h's TIDE_SHADER_TEXTURES, gpu.h's TIDE_GPU_MAX_TEXTURES:
// what every backend takes in a stage, and as many samplers), and where its storage buffers start, after their
// bindings (TIDE_GPU_STORAGE_BINDING).
#define SHADER_TEXTURES 16
#define SHADER_STORAGE_BINDING (1 + 2 * SHADER_TEXTURES)
// The most GPU lists one shader names (tide/draw.h's TIDE_SHADER_STORAGE): a table's, in its uniforms, as a shader
// binds the pools its lists are ranges of rather than each list (shader_compute.inc)
#define SHADER_STORAGE_MOST 64

// Tide code the GPU runs: a `frag` for each pixel of a mesh, a `vert` for
// each of its vertices, and the functions they call. For every combination a
// mesh draw uses (a vertex struct, with a vert, a frag, both or neither) this
// writes a shader in WGSL, for WebGPU, which spirv.c translates into SPIR-V
// for Vulkan and hlsl.c into HLSL for Unity. Codegen puts them in the
// generated C as tide_shaders (tide/draw.h). (GLSL, for the OpenGL backends,
// went with them, 2026-11-05: compute is on every backend there is.)
//
// The GPU runs less than the CPU does: bools, ints, floats, vectors,
// quaternions, matrices, colors and structs of those, locals, `if`, loops
// that count, and sampling a grid of colors. `Math` is all there, and what
// builds its types: what a shader language has no function for, or has one
// that does otherwise than tide/math.h's, is written out (helper_text).
// Nothing of it is left out but for a reason, which its error says: a long's
// hash, as these GPUs have no 64-bit integers. Random's rolls on the type are
// there too, from a generator of 32 bits that each pixel and each vertex has
// for itself (Rolls, below); a Random value, whose state is 64 bits, isn't.
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
    H_Q_FROM_M3, H_FROMTO_UNIT3, H_FROMTO_Q, H_LOOKROTATION_Q, H_M3_FROM_Q, H_M4_FROM_M3, H_TRS, H_SCALE, H_PERSPECTIVE, H_ORTHO,
    H_RECIPROCAL, H_INVERSE2, H_INVERSE3, H_INVERSE4,
    H_NORMALS, // tide_normals(m): what a mesh's normals go through to the world, for instance.Normal
    H_STRAIGHT, // tide_straight(c): a texture's pixel, which has its alpha worked into its color, as the color it is
    H_EASE, // tide_ease(t, kind): Math.EaseIn and the rest, as tide/math.h's
    // Random's rolls on the type: tide_roll(), the next 32 bits of the stage's own generator, and what's made of them
    H_ROLL, H_MUL_HIGH, H_ROLL_INT, H_ROLL_INT_BETWEEN, H_ROLL_INT_BELOW, H_ROLL_FLOAT, H_ROLL_FLOAT_BETWEEN, H_ROLL_FLOAT_BELOW,
    H_ROLL_BOOL, H_ROLL_FLOAT2, H_ROLL_FLOAT2_BETWEEN, H_ROLL_FLOAT3, H_ROLL_FLOAT3_BETWEEN, H_ROLL_FLOAT2_DIRECTION,
    H_ROLL_FLOAT3_DIRECTION, H_ROLL_QUATERNION_ROTATION,
    H_COUNT
};

// A list, a grid, a map or a set in code the GPU runs: which of the shader's
// textures of numbers it is (data_of).
typedef struct data_ref {
    int texture; // -1 while checking, when no shader says
    type element; // A list's element, a grid's cell, a map's entry (its key and its value) or a set's key
    int grid;     // A grid's axes, 2 or 3: 0 for anything else
    const decl *map; // The map's or the set's type, for one of those
    // Where the code notes which parts of an element it reads (shader_leaves), while checking: the field's own, or
    // inside a function, its parameter's, which each call hands on. NULL for an asset's, which is sent whole
    bool *leaves;
} data_ref;

typedef struct data_alias {
    const stmt *local;
    data_ref data;
    const expr *names; // What the local was declared as: a grid that's sampled is found through it (texture_of)
} data_alias;

// What a function is written for, by its parameter: the list or grid it's
// called with, and the texture it samples. Neither is handed to it, as
// neither language passes a texture to a function on every backend.
typedef struct given {
    data_ref data;       // A list, a grid, a map or a set: which of the shader's textures of numbers it is
    const expr *sampled; // A texture that's sampled: where the frag or vert names the one it was given (texture_of)
    int slot;            // A GPU list: which of the shader's storage buffers it is (gpu_list_slot), -1 while checking
} given;

// A local that holds an element of a list the code was given, or a part of
// one: a foreach's variable, or `var light = lights[i];`. What the code reads
// of the local is what it reads of the element.
typedef struct element_alias {
    const stmt *local;
    bool *leaves;     // The element's (data_ref.leaves)
    int first, count; // The parts of it the local holds
} element_alias;

typedef struct shader_function {
    const decl *fn;
    const char *suffix;
} shader_function;

typedef struct emit {
    program *prog;
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
    // The structs the code names, each after the ones it holds
    VEC(const decl *) structs;
    VEC(shader_function) done; // The functions written, callees first, each for the lists and grids it's called with
    sb functions;
    bool helpers[H_COUNT];
    // A draw's textures, up to SHADER_TEXTURES
    bool lists[SHADER_TEXTURES]; // The shader's textures that are lists or grids whose reader is written (need_data)
    bool finds[SHADER_TEXTURES]; // ...that are maps or sets whose lookup is written (need_find)
    bool tries[SHADER_TEXTURES]; // ...that are maps whose TryGet is written (need_try)
    VEC(data_alias) aliases;   // The locals of the code being written that name a list or a grid (data_of)
    const given *spec;         // By the parameter of the function being written: the list, grid or texture it's called with
    VEC(const decl *) arrays;  // The array types whose element reader is written (need_array)
    VEC(type) picks;           // The types whose '?:' is a function (need_pick)
    int raw; // Writing what an assignment stores into: an array's element is the element itself, in range already
    VEC(const field *) hoisted; // Arrays the engine supplies that the function being written made a variable of as it starts
    // Which parts of a list's elements the code reads (read_element): only those are sent
    VEC(element_alias) elements; // The locals of the code being written that hold an element
    const expr *part_root;       // The element a chain of fields being written reads a part of (note_part)...
    int part_first, part_count;  // ...and which part
    const expr *held_root;       // The element, or the part of one, that the local being declared holds (st_var)...
    const stmt *held_by;         // ...and the local
    bool storage_done[SHADER_STORAGE_MOST]; // The GPU lists whose functions are written (need_storage)
    VEC(const char *) atomic_fns; // ...and the atomics' and shared memory's (need_atomic)
    bool guards_shared;           // A kernel's write to shared memory is under tide_zero (writes_shared)
} emit;

static void ex(emit *em, const expr *e);
static void ex_as(emit *em, const expr *e, type want);
static bool is_whole_variable(const expr *e);
static bool is_pointer(const param *p);
static void st(emit *em, const stmt *s);
static void need_function(emit *em, decl *fn, loc at, const given *spec, const char *suffix);
static bool is_const(const expr *e);
static bool is_sampled(type t);
static const field *field_called(const decl *d, const char *name);
static int packs(int words);
static bool ex_gpu(emit *em, const expr *e);
static bool st_gpu(emit *em, const stmt *s);
static void ex_invocation(emit *em, const expr *e);
static void put_assigned(emit *em, const stmt *s);
static bool writes_shared(const stmt *s);
static int gpu_list_slot(emit *em, const expr *list);

// Textures and samplers. Texture i is at binding 1 + 2i and sampler j at 2 + 2j, each counted apart, 16 of each: a
// texture is one image however many ways its code reads it (shader_way), each way through a sampler. Textures every
// draw reads the same way share one; a way whose filter or wrap a Texture carries (one the draw or a field gives,
// which says how it's sampled itself) has one of that texture's alone, set as each draw goes (gen_shader_textures).
// A list is loaded, through none. tide_shader's sampler_count, sampler_reads and texture_samplers carry this to
// the backends, where it's anything but a sampler of its own for each texture that's sampled, in order.

// Whether two of a shader's reads are of one image: the same read, or a depth read as a number and compared.
static bool one_image(const shader_read a, const shader_read b)
{
    return a == b || ((a == READ_DEPTH || a == READ_COMPARE) && (b == READ_DEPTH || b == READ_COMPARE))
        || ((a == READ_DEPTH_ARRAY || a == READ_COMPARE_ARRAY) && (b == READ_DEPTH_ARRAY || b == READ_COMPARE_ARRAY));
}

// How a sampler reads for a way a texture is read: an array's as a texture's, as it's sampled the same way.
static shader_read base_read(const shader_read read)
{
    return read == READ_ARRAY ? READ_COLOR : read == READ_DEPTH_ARRAY ? READ_DEPTH : read == READ_COMPARE_ARRAY ? READ_COMPARE : read;
}

int shader_size_words(const shader_texture *t)
{
    const type held = t->asset ? t->asset->return_type : t->field ? t->field->type : (type){TY_TEXTURE, NULL};
    return held.kind == TY_TEXTURE_ARRAY || held.kind == TY_DEPTH_TEXTURE_ARRAY ? 3 : 2;
}

// Whether a texture is read by a uv and a layer.
static bool arrayed_read(const shader_read read)
{
    return read == READ_ARRAY || read == READ_DEPTH_ARRAY || read == READ_COMPARE_ARRAY;
}

// Whether a way of reading a texture is the Texture's to say as each draw goes, in its filter or its wrap: the
// draw's own texture, or a Texture the draw or a field gives, whose Sample leaves either to it.
static bool way_carried(const shader *sh, const shader_way *w)
{
    if (w->read == READ_COMPARE || w->read == READ_DEPTH) return false; // Its sampler is how it's read alone
    const shader_texture *t = &sh->textures.items[w->texture];
    const bool own = sh->textured && !t->source && !t->asset && !t->direct; // The draw's own
    const bool given = own || (w->read == READ_COLOR && !t->asset
                               && (t->direct || (t->field && t->field->type.kind == TY_TEXTURE)));
    return own || (given && (!w->filter_said || !w->wrap_said));
}

// Each way's sampler: one that's the same for every draw shared by every way read alike, and a carried one by the
// ways of its own texture read alike. Gives how many there are.
static int plan_samplers(shader *sh)
{
    sh->samplers.count = 0;
    for (int i = 0; i < sh->ways.count; i++) {
        shader_way *w = &sh->ways.items[i];
        const bool carried = way_carried(sh, w);
        const shader_read read = base_read(w->read); // An array's read through a sampler as a texture's is
        int k = 0;
        for (; k < sh->samplers.count; k++) {
            const shader_sampler *s = &sh->samplers.items[k];
            if (s->carried != carried || s->read != read) continue;
            if (carried ? s->texture == w->texture && s->filter == w->filter && s->wrap == w->wrap
                          && s->filter_said == w->filter_said && s->wrap_said == w->wrap_said
                        : read == READ_COMPARE || read == READ_DEPTH // How it's read alone
                          || (s->filter == w->filter && (read == READ_CUBE || s->wrap == w->wrap))) { // A cube wraps nothing
                break;
            }
        }
        if (k == sh->samplers.count) {
            const shader_sampler s = {w->texture, read, w->filter, w->wrap, w->filter_said, w->wrap_said, carried};
            vec_push(sh->samplers, s);
        }
        w->sampler = k;
    }
    return sh->samplers.count;
}

// The samplers texture `i` is read through: bit j for sampler j.
static unsigned texture_samplers(const shader *sh, const int i)
{
    unsigned through = 0;
    for (int w = 0; w < sh->ways.count; w++) {
        if (sh->ways.items[w].texture == i) through |= 1u << sh->ways.items[w].sampler;
    }
    return through;
}

// Whether a shader's samplers are what a tide_shader that says none has: each texture that's sampled has one of
// its own, sampler i for texture i, read as the texture is.
static bool samplers_own(const shader *sh)
{
    for (int i = 0; i < sh->textures.count; i++) {
        const shader_read read = sh->textures.items[i].read;
        if (texture_samplers(sh, i) != (read == READ_DATA ? 0u : 1u << i)) return false;
        if (read != READ_DATA && sh->samplers.items[i].read != base_read(read)) return false;
    }
    return true;
}

// The texture and the sampler a way of the shader being written reads (sample_slot).
static int way_texture(const emit *em, const int way)
{
    if (!em->sh || way < 0 || way >= em->sh->ways.count) return way;
    return em->sh->ways.items[way].texture;
}

static int way_sampler(const emit *em, const int way)
{
    if (!em->sh || way < 0 || way >= em->sh->ways.count) return way;
    return em->sh->ways.items[way].sampler;
}

// The textures' declarations and the samplers', in the order of their bindings.
static void put_textures(sb *o, const shader *sh)
{
    const int most = sh->textures.count > sh->samplers.count ? sh->textures.count : sh->samplers.count;
    for (int i = 0; i < most; i++) {
        const shader_texture *t = i < sh->textures.count ? &sh->textures.items[i] : NULL;
        if (t && t->read == READ_DATA) { // A list's numbers, with all of their bits, read through no sampler
            sb_printf(o, "@group(0) @binding(%d) var pixels%d: texture_2d<i32>;\n", 1 + 2 * i, i);
        } else if (t) {
            sb_printf(o, "@group(0) @binding(%d) var pixels%d: %s;\n", 1 + 2 * i, i,
                      t->read == READ_COLOR ? "texture_2d<f32>" : t->read == READ_CUBE ? "texture_cube<f32>"
                      : t->read == READ_ARRAY ? "texture_2d_array<f32>" : t->read == READ_DEPTH_ARRAY ? "texture_depth_2d_array"
                      : "texture_depth_2d");
        }
        if (i < sh->samplers.count) {
            sb_printf(o, "@group(0) @binding(%d) var sampler%d: %s;\n", 2 + 2 * i, i,
                      sh->samplers.items[i].read == READ_COMPARE ? "sampler_comparison" : "sampler");
        }
    }
}

static const char *read_name(const shader_read read)
{
    return read == READ_COMPARE ? "COMPARE" : read == READ_DEPTH ? "DEPTH" : read == READ_CUBE ? "CUBE"
         : read == READ_DATA ? "DATA" : read == READ_ARRAY ? "ARRAY" : read == READ_DEPTH_ARRAY ? "DEPTH_ARRAY" : "COLOR";
}

// What a shader's tide_shader says of its textures where any isn't a color, and of its samplers where they aren't
// each sampled texture's own (samplers_own): how each texture is read, and how each sampler and which read each.
void shader_put_texture_reads(sb *o, const shader *sh)
{
    bool reads = false;
    for (int t = 0; t < sh->textures.count; t++) reads |= sh->textures.items[t].read != READ_COLOR;
    if (reads) {
        sb_put(o, "    .texture_reads = {");
        for (int t = 0; t < sh->textures.count; t++) sb_printf(o, "%sTIDE_SHADER_READ_%s", t ? ", " : "", read_name(sh->textures.items[t].read));
        sb_put(o, "},\n");
    }
    if (!samplers_own(sh)) {
        sb_printf(o, "    .sampler_count = %d,\n    .sampler_reads = {", sh->samplers.count);
        for (int j = 0; j < sh->samplers.count; j++) sb_printf(o, "%sTIDE_SHADER_READ_%s", j ? ", " : "", read_name(sh->samplers.items[j].read));
        sb_put(o, "},\n    .texture_samplers = {");
        for (int t = 0; t < sh->textures.count; t++) sb_printf(o, "%s0x%xu", t ? ", " : "", texture_samplers(sh, t));
        sb_put(o, "},\n");
    }
}

static const char *shader_word(const decl *d)
{
    return d->is_compute ? "kernel" : d->is_frag ? "frag" : "vert";
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
// underscores ever meet (GLSL kept those for itself) and no two names
// collide.
static const char *name_of(const char *prefix, const str name)
{
    sb b = {0};
    sb_put(&b, prefix);
    for (int i = 0; i < name.len; i++) {
        if (name.ptr[i] == '_') sb_put(&b, "_0");
        else if (name.ptr[i] == '.') sb_put(&b, "_1");
        else if (name.ptr[i] == '<') sb_put(&b, "_5"); // A map's entry, 'Entry<int, Stat>'
        else if (name.ptr[i] == '>') sb_put(&b, "_6");
        else if (name.ptr[i] == ',') sb_put(&b, "_7");
        else if (name.ptr[i] == ' ') continue;
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
    return t.kind == TY_STRUCT || t.kind == TY_COMPONENT || t.kind == TY_SINGLETON || t.kind == TY_EVENT;
}

// How many floats and ints a value of type `t` is, as the GPU is sent it.
// False for a type it can't have.
static bool type_words(const type t, int *floats, int *ints)
{
    if (type_is_double_based(t)) return false;
    if (t.kind == TY_BYTE) { // An int there, from 0 to 255
        *ints += 1;
        return true;
    }
    if (t.kind == TY_ARRAY) { // Its elements, one after the other, an array's own among them
        const type element = t.decl->fields.items[0].type;
        int f = 0, i = 0;
        if (!type_words(element, &f, &i)) return false;
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
    case TY_CHAR:
        *ints += 1;
        return true;
    case TY_STRUCT:
    case TY_COMPONENT:
    case TY_SINGLETON:
    case TY_EVENT:
        if (t.decl->skin_kind && t.decl->skin_kind != SKIN_TRANSFORM) return false;
        for (int i = 0; i < t.decl->fields.count; i++) {
            const field *f = &t.decl->fields.items[i];
            if (!f->hidden && !type_words(f->type, floats, ints)) return false;
        }
        return true;
    default:
        return false;
    }
}

int shader_leaves(const type t)
{
    if (!is_struct_like(t)) return 1;
    int n = 0;
    for (int i = 0; i < t.decl->fields.count; i++) {
        if (!t.decl->fields.items[i].hidden) n += shader_leaves(t.decl->fields.items[i].type);
    }
    return n;
}

void shader_part_words(const type t, const bool *leaves, int *leaf, int *floats, int *ints)
{
    if (is_struct_like(t)) {
        for (int i = 0; i < t.decl->fields.count; i++) {
            const field *f = &t.decl->fields.items[i];
            if (!f->hidden) shader_part_words(f->type, leaves, leaf, floats, ints);
        }
        return;
    }
    if (!leaves || leaves[*leaf]) type_words(t, floats, ints);
    *leaf += 1;
}

unsigned shader_layout(const type t, const bool *leaves)
{
    const int n = shader_leaves(t);
    bool all = true;
    unsigned h = 0x811C9DC5u;
    for (int i = 0; i < n; i++) {
        const bool read = !leaves || leaves[i];
        all &= read;
        h = (h ^ (read ? 1u : 2u)) * 0x01000193u;
    }
    return all ? 0u : (h & 0x7FFFFFFFu) | 1u;
}

const bool *shader_texture_leaves(const shader_texture *t)
{
    return t->field && !t->asset ? t->field->gpu_leaves : NULL;
}

// Where the parts of field `f` start among those of a value of the struct `d`.
static int leaf_offset(const decl *d, const field *f)
{
    int n = 0;
    for (int i = 0; i < d->fields.count && &d->fields.items[i] != f; i++) {
        if (!d->fields.items[i].hidden) n += shader_leaves(d->fields.items[i].type);
    }
    return n;
}

// A field declared `T?` in a struct `d`, or in a struct it holds, and the struct it's in: NULL if there's none.
static const field *optional_field_in(const decl *d, const decl **in)
{
    for (int i = 0; i < d->fields.count; i++) {
        const field *f = &d->fields.items[i];
        type held = f->type;
        while (held.kind == TY_ARRAY) held = held.decl->fields.items[0].type;
        if (held.kind != TY_STRUCT) continue;
        if (held.decl->optional_of) {
            *in = d;
            return f;
        }
        const field *inner = optional_field_in(held.decl, in);
        if (inner) return inner;
    }
    return NULL;
}

static bool holds_data(type t);

// Whether the GPU has values of type `t`; says why not, at `at`.
static bool type_fits(emit *em, const type t, const loc at)
{
    // A struct with a T? field: a value or nothing is the CPU's to handle. A component or a singleton with one
    // is read there a field at a time, and the T? field itself is what's refused (TY_OPTIONAL, below).
    const decl *holder = NULL;
    const field *optional = t.kind == TY_STRUCT && !t.decl->optional_of ? optional_field_in(t.decl, &holder) : NULL;
    if (optional) {
        refuse(em, at, "'%s' can't go to the GPU: its field '" STR_FMT "' is a T?, and the GPU has no values that may be missing",
               type_name(t), STR_ARG(optional->name));
        diag_note("keep what the GPU reads in a struct of its own, with a value and a bool beside it where '" STR_FMT "." STR_FMT
                  "' is a T?", STR_ARG(holder->name), STR_ARG(optional->name));
        return false;
    }
    if (t.kind == TY_STRUCT && t.decl->skin_kind && t.decl->skin_kind != SKIN_TRANSFORM) {
        refuse(em, at, "%s belongs to the CPU: sample and edit a Pose before Draw.SkinnedMesh", type_name(t));
        return false;
    }
    int floats = 0, ints = 0;
    if (t.kind == TY_ERROR || type_words(t, &floats, &ints)) {
        if (is_struct_like(t) && floats + ints == 0) {
            refuse(em, at, "%s has no fields, so there's nothing of it for the GPU to hold", type_name(t));
            return false;
        }
        return true;
    }
    const char *why = "the GPU runs on bools, ints, floats, vectors, matrices, colors and structs of those";
    if (holds_data(t)) {
        refuse(em, at, "%s holds a list, so code the GPU runs reads its fields, or passes it to a function or calls its method, "
                       "by the name the frag or vert was given it: it can't be made, kept in a local, returned or picked there",
               type_name(t));
        diag_note("read its fields where it was given, or pass that name on to a function that takes one");
        return false;
    }
    if (t.kind == TY_STRUCT && t.decl == em->prog->random) { // Its state is a long, and the code that steps it the CPU's
        refuse(em, at, "a Random can't go to the GPU: its state is 64 bits, and the GPU's ints are 32");
        diag_note("roll on the type, 'Random.NextInt(6)': a frag has a generator of its pixel's own there, and a vert of its "
                  "vertex's");
        return false;
    }
    switch (t.kind) {
    case TY_DOUBLE: case TY_DOUBLE2: case TY_DOUBLE3: case TY_DOUBLE4:
    case TY_DOUBLE2X2: case TY_DOUBLE3X3: case TY_DOUBLE4X4:
        why = "the GPU's floats are 32 bits: convert to a float scalar, vector or matrix in local code before drawing"; break;
    case TY_LONG: why = "the GPU's ints are 32 bits: convert it with int(x) before it gets there"; break;
    case TY_STRING: why = "text is the CPU's"; break;
    case TY_MESH: why = "a Mesh is drawn, 'Draw.Mesh(mesh, transform, ...)', which sends the GPU its vertices and its triangles"; break;
    case TY_LIST:
        refuse(em, at, "a list in code the GPU runs is one the frag or vert was given, by a name: it can't be made, changed, "
                       "returned, picked between or kept in a struct there");
        diag_note("name a field of a component, a singleton or the draw's values, or an asset: 'var lights = scene.lights;', "
                  "or pass it to a function that takes a 'List<Light>'");
        return false;
    case TY_MAP:
    case TY_SET:
        refuse(em, at, "a %s in code the GPU runs is one the frag or vert was given, by a name: it can't be made, changed, "
                       "returned, picked between or kept in a struct there", t.kind == TY_MAP ? "map" : "set");
        diag_note(t.kind == TY_MAP ? "name a field of a component, a singleton or the draw's values: 'tiles.kinds[key]', "
                                     "'tiles.kinds.Has(key)', 'tiles.kinds.TryGet(key, out kind)', 'tiles.kinds.count', or "
                                     "'foreach (var entry in tiles.kinds)'"
                                   : "name a field of a component, a singleton or the draw's values: 'tiles.lit.Has(key)', "
                                     "'tiles.lit.count', or 'foreach (var key in tiles.lit)'");
        return false;
    case TY_GRID:
        refuse(em, at, "a grid in code the GPU runs is one the frag or vert was given, by a name: it can't be made, changed, "
                       "returned, picked between or kept in a struct there");
        diag_note("name a field of a component or a singleton, or an asset: 'var cells = board.cells;', or pass it to a function "
                  "that takes a 'Grid2<int>'");
        return false;
    case TY_ARRAY:
        why = "an array the GPU has is of bools, ints, floats, vectors, matrices, colors, structs of those, or arrays of them";
        break;
    case TY_TEXTURE: why = "a texture is sampled with 'texture.Sample(uv)', not kept as a GPU value"; break;
    case TY_DEPTH_TEXTURE:
        why = "a target's depth is read with 'depth.Sample(uv)' or 'depth.Compare(uv, value)', not kept as a GPU value";
        break;
    case TY_CUBEMAP: why = "a cube is read with 'cube.Sample(direction)', not kept as a GPU value"; break;
    case TY_TEXTURE_ARRAY: why = "an array is read with 'array.Sample(uv, layer)', not kept as a GPU value"; break;
    case TY_DEPTH_TEXTURE_ARRAY:
        why = "an array's depth is read with 'depths.Sample(uv, layer)' or 'depths.Compare(uv, layer, value)', not kept as a GPU value";
        break;
    case TY_RENDER_TARGET: why = "a render target belongs to the CPU: sample its color as a Texture"; break;
    case TY_GPU_LIST:
        refuse(em, at, "a GPU list in code the GPU runs is named where it's used: it can't be kept in a local, returned or "
                       "picked there");
        diag_note("read it by its field, 'meadow.blades[i]', or pass it by that name to a function that takes a GPUList, or "
                  "the struct, component or singleton that holds it");
        return false;
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
        if (t.decl == em->prog->invocation) {
            refuse(em, at, "a kernel's invocation is read through 'this.index', 'this.at', 'this.local', 'this.group' and "
                          "'this.count': 'this' is not a value to keep or pass on");
            return false;
        }
        if (t.decl == em->prog->placed) {
            refuse(em, at, "where the engine would put a vert's vertex is read through 'this.clip': 'this' is not a value to "
                          "keep or pass on");
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

static void need_struct(emit *em, const decl *d);

// The structs a value of type `t` is made of, before whatever holds it.
static void need_held(emit *em, const type t)
{
    if (is_struct_like(t)) need_struct(em, t.decl);
    else if (t.kind == TY_ARRAY) need_held(em, t.decl->fields.items[0].type);
}

// A struct, after the structs it holds.
static void need_struct(emit *em, const decl *d)
{
    for (int i = 0; i < em->structs.count; i++) {
        if (em->structs.items[i] == d) return;
    }
    for (int i = 0; i < d->fields.count; i++) {
        const field *f = &d->fields.items[i];
        if (!f->hidden) need_held(em, f->type);
    }
    vec_push(em->structs, d);
}

static const char *ty(emit *em, const type t)
{
    static const char *const floats[] = {"", "f32", "vec2f", "vec3f", "vec4f"};
    static const char *const ints[] = {"", "i32", "vec2i", "vec3i", "vec4i"};
    static const char *const matrices[] = {"", "", "mat2x2f", "mat3x3f", "mat4x4f"};
    if (t.kind == TY_BYTE) return "i32";
    const int dim = type_dim(t);
    if (dim > 0) return type_is_float_based(t) ? floats[dim] : ints[dim];
    if (matrix_dim(t)) return matrices[matrix_dim(t)];
    if (t.kind == TY_COLOR || t.kind == TY_QUATERNION) return "vec4f";
    if (t.kind == TY_BOOL) return "bool";
    if (t.kind == TY_ENUM || t.kind == TY_CHAR) return "i32"; // A char is its code point there
    if (t.kind == TY_ARRAY) {
        sb b = {0};
        sb_printf(&b, "array<%s, %d>", ty(em, t.decl->fields.items[0].type), t.decl->array_length);
        return b.data;
    }
    if (is_struct_like(t)) {
        need_struct(em, t.decl);
        return struct_name(t.decl);
    }
    return "f32"; // Refused already
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
    if (e->text.len == 0 || e->is_char) return e->int_value; // The 1 of i++, and a char's code point
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
    sb_printf(em->o, "%s()", ty(em, t));
}

// ---------------------------------------------------------------------------
// The engine's own functions
//
// What Math does that a shader language doesn't, or doesn't do as the CPU
// does: tide/math.h's formulas, in its order, so the same code gives the same
// answer on the CPU and on the GPU up to the GPU's rounding, and Math.Hash,
// which is integers, the same number. Each is written in a notation of its
// own, C-like (GLSL's, when there was a GLSL to write), which put_helper
// spells as WGSL: a function's head on a line of its own, `float tide_f(float
// x) {`, a statement a line, a local declared with its type and a value, no
// '?:', no parameter assigned to, GLSL's names for types, and atan2,
// inversesqrt, floatBitsToInt and intBitsToFloat for WGSL's functions of them.

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
    "    float numerator = c - a;\n"
    "    float denominator = b - a;\n"
    "    float divisor = denominator;\n"
    "    if (divisor == 0.0) { divisor = 1.0; }\n"
    "    float t = tide_saturate1(numerator / divisor);\n"
    "    if (denominator == 0.0) {\n"
    "        t = 0.0;\n"
    "        if (floatBitsToInt(denominator) < 0) {\n"
    "            if (numerator < 0.0) { t = 1.0; }\n"
    "        } else if (numerator > 0.0) { t = 1.0; }\n"
    "    }\n"
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
    // tide/math.h's tide_ease_*_f, in the same order: kind 0 in, 1 out, 2 in-out, 3 in-back, 4 out-back. One
    // return, as ANGLE's D3D11 warns of what an early one might leave unset
    [H_EASE] = "float tide_ease(float x, int kind) {\n"
               "    float t = tide_saturate1(x);\n"
               "    float u = 1.0 - t;\n"
               "    float r = t * t * t;\n"
               "    if (kind == 1) { r = 1.0 - u * u * u; }\n"
               "    if (kind == 2) {\n"
               "        float w = 2.0 - 2.0 * t;\n"
               "        r = 1.0 - w * w * w * 0.5;\n"
               "        if (t < 0.5) { r = 4.0 * t * t * t; }\n"
               "    }\n"
               "    if (kind == 3) { r = 2.70158 * t * t * t - 1.70158 * t * t; }\n"
               "    if (kind == 4) {\n"
               "        float b = t - 1.0;\n"
               "        r = 1.0 + 2.70158 * b * b * b + 1.70158 * b * b;\n"
               "    }\n"
               "    return r;\n"
               "}\n",
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
    // The four corners use only two coordinates per axis and one seed. Reuse
    // the rotated xxHash lanes, retaining exactly hash4's unsigned sum and end.
    // This changes no lattice/gradient bits or floating-point expression.
    // Independent rank tests retain the CPU's x, then y, then z tie priority.
    [H_SIMPLEX3] = "uint tide_simplex_lane(uint h, int w, uint r) {\n"
                   "    uint v = tide_xxh_lane(h, w);\n"
                   "    return (v << r) | (v >> (32u - r));\n"
                   "}\n"
                   "uint tide_simplex_pick(uint a, uint b, int high) {\n"
                   "    uint v = a;\n"
                   "    if (high != 0) { v = b; }\n"
                   "    return v;\n"
                   "}\n"
                   "float tide_corner3(int h, float x, float y, float z) {\n"
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
                   "    if (x0 >= y0 && x0 >= z0) { i1 = 1; }\n"
                   "    if (y0 > x0 && y0 >= z0) { j1 = 1; }\n"
                   "    if (z0 > x0 && z0 > y0) { k1 = 1; }\n"
                   "    if (x0 >= y0 || x0 >= z0) { i2 = 1; }\n"
                   "    if (y0 > x0 || y0 >= z0) { j2 = 1; }\n"
                   "    if (z0 > x0 || z0 > y0) { k2 = 1; }\n"
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
                   "    uint ax = tide_simplex_lane(0x24234428u, i, 1u);\n"
                   "    uint bx = tide_simplex_lane(0x24234428u, i + 1, 1u);\n"
                   "    uint ay = tide_simplex_lane(0x85EBCA77u, j, 7u);\n"
                   "    uint by = tide_simplex_lane(0x85EBCA77u, j + 1, 7u);\n"
                   "    uint az = tide_simplex_lane(0u, k, 12u);\n"
                   "    uint bz = tide_simplex_lane(0u, k + 1, 12u);\n"
                   "    uint ds = tide_simplex_lane(0x61C8864Fu, seed, 18u);\n"
                   "    int h0 = tide_xxh_end(ax + ay + az + ds + 16u);\n"
                   "    int h1 = tide_xxh_end(tide_simplex_pick(ax, bx, i1) + tide_simplex_pick(ay, by, j1) + "
                   "tide_simplex_pick(az, bz, k1) + ds + 16u);\n"
                   "    int h2 = tide_xxh_end(tide_simplex_pick(ax, bx, i2) + tide_simplex_pick(ay, by, j2) + "
                   "tide_simplex_pick(az, bz, k2) + ds + 16u);\n"
                   "    int h3 = tide_xxh_end(bx + by + bz + ds + 16u);\n"
                   "    float n0 = tide_corner3(h0, x0, y0, z0);\n"
                   "    float n1 = tide_corner3(h1, x1, y1, z1);\n"
                   "    float n2 = tide_corner3(h2, x2, y2, z2);\n"
                   "    float n3 = tide_corner3(h3, x3, y3, z3);\n"
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
    // Shepperd's method: by the largest term. The selected finite branch has
    // a nonzero scale; guard the other candidates too because ANGLE's HLSL
    // compiler diagnoses their zero divisors while folding a constant matrix.
    [H_Q_FROM_M3] = "vec4 tide_q_from_m3(mat3 m) {\n"
                    "    float x = m[0].x;\n"
                    "    float y = m[1].y;\n"
                    "    float z = m[2].z;\n"
                    "    float t = x + y + z;\n"
                    "    if (t > 0.0) {\n"
                    "        float s = sqrt(t + 1.0) * 2.0;\n"
                    "        if (s == 0.0) { s = 1.0; }\n"
                    "        return vec4((m[1].z - m[2].y) / s, (m[2].x - m[0].z) / s, (m[0].y - m[1].x) / s, 0.25 * s);\n"
                    "    }\n"
                    "    if (x > y && x > z) {\n"
                    "        float s = sqrt(1.0 + x - y - z) * 2.0;\n"
                    "        if (s == 0.0) { s = 1.0; }\n"
                    "        return vec4(0.25 * s, (m[1].x + m[0].y) / s, (m[2].x + m[0].z) / s, (m[1].z - m[2].y) / s);\n"
                    "    }\n"
                    "    if (y > z) {\n"
                    "        float s = sqrt(1.0 + y - x - z) * 2.0;\n"
                    "        if (s == 0.0) { s = 1.0; }\n"
                    "        return vec4((m[1].x + m[0].y) / s, 0.25 * s, (m[2].y + m[1].z) / s, (m[2].x - m[0].z) / s);\n"
                    "    }\n"
                    "    float s = sqrt(1.0 + z - x - y) * 2.0;\n"
                    "    if (s == 0.0) { s = 1.0; }\n"
                    "    return vec4((m[2].x + m[0].z) / s, (m[2].y + m[1].z) / s, 0.25 * s, (m[0].y - m[1].x) / s);\n"
                    "}\n",
    [H_FROMTO_UNIT3] = "vec3 tide_fromto_unit3(vec3 v) {\n"
                       "    if ((floatBitsToInt(v.x) & 0x7f800000) == 0x7f800000 || (floatBitsToInt(v.y) & 0x7f800000) == 0x7f800000 || (floatBitsToInt(v.z) & 0x7f800000) == 0x7f800000) { return vec3(0.0); }\n"
                       "    float largest = max(max(abs(v.x), abs(v.y)), abs(v.z));\n"
                       "    if (largest == 0.0) { return vec3(0.0); }\n"
                       "    return tide_normalize3(v / largest);\n"
                       "}\n",
    [H_FROMTO_Q] = "vec4 tide_fromto_q(vec3 f, vec3 t) {\n"
                   "    vec3 a = tide_fromto_unit3(f);\n"
                   "    vec3 b = tide_fromto_unit3(t);\n"
                   "    if (dot(a, a) == 0.0 || dot(b, b) == 0.0) { return vec4(0.0, 0.0, 0.0, 1.0); }\n"
                   "    float d = clamp(dot(a, b), -1.0, 1.0);\n"
                   "    vec3 axis = cross(a, b);\n"
                   "    if (d >= 0.0) { return tide_normalize_q(vec4(axis, 1.0 + d)); }\n"
                   "    float largest = max(max(abs(axis.x), abs(axis.y)), abs(axis.z));\n"
                   "    if (largest == 0.0 || (a.x == -b.x && a.y == -b.y && a.z == -b.z)) {\n"
                   "        vec3 absolute = abs(a);\n"
                   "        vec3 side = vec3(0.0, 0.0, 1.0);\n"
                   "        if (absolute.x <= absolute.y && absolute.x <= absolute.z) { side = vec3(1.0, 0.0, 0.0); }\n"
                   "        else if (absolute.y <= absolute.z) { side = vec3(0.0, 1.0, 0.0); }\n"
                   "        return vec4(tide_fromto_unit3(cross(a, side)), 0.0);\n"
                   "    }\n"
                   "    vec3 scaled = axis / largest;\n"
                   "    float size = length(scaled);\n"
                   "    float sine = sqrt(0.5 * (1.0 - d));\n"
                   "    return tide_normalize_q(vec4(scaled * (sine / size), (largest * size) / (2.0 * sine)));\n"
                   "}\n",
    // After FromTo, its fallback when up gives no side. One return: ANGLE's
    // D3D11 compiler warns (X4000) of an early one returning FromTo's value.
    [H_LOOKROTATION_Q] = "vec4 tide_lookrotation_q(vec3 f, vec3 u) {\n"
                         "    vec3 z = tide_fromto_unit3(f);\n"
                         "    vec3 side = cross(tide_fromto_unit3(u), z);\n"
                         "    vec4 q = vec4(0.0, 0.0, 0.0, 1.0);\n"
                         "    if (dot(side, side) < 1e-10) {\n"
                         "        q = tide_fromto_q(vec3(0.0, 0.0, 1.0), f);\n"
                         "    } else {\n"
                         "        vec3 y = tide_normalize3(cross(z, tide_normalize3(side)));\n"
                         "        q = tide_normalize_q(tide_q_from_m3(mat3(cross(y, z), y, z)));\n"
                         "    }\n"
                         "    return q;\n"
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
    [H_SCALE] = "mat4 tide_scaling(vec3 s) {\n"
                "    return mat4(vec4(s.x, 0.0, 0.0, 0.0), vec4(0.0, s.y, 0.0, 0.0), vec4(0.0, 0.0, s.z, 0.0), vec4(0.0, 0.0, 0.0, 1.0));\n"
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
    // Keep IEEE signed infinities at zero without a literal zero divisor
    // after ANGLE/HLSL inlines a constant singular matrix's inverse.
    [H_RECIPROCAL] = "float tide_reciprocal(float value) {\n"
                     "    float divisor = value;\n"
                     "    if (divisor == 0.0) { divisor = 1.0; }\n"
                     "    float result = 1.0 / divisor;\n"
                     "    if (value == 0.0) { result = intBitsToFloat(floatBitsToInt(value) | 0x7f800000); }\n"
                     "    return result;\n"
                     "}\n",
    [H_INVERSE2] = "mat2 tide_inverse2(mat2 m) {\n"
                   "    float s = tide_reciprocal(m[0].x * m[1].y - m[1].x * m[0].y);\n"
                   "    return mat2(m[1].y * s, -m[0].y * s, -m[1].x * s, m[0].x * s);\n"
                   "}\n",
    // Its rows are the cross products of pairs of columns, over the determinant
    [H_INVERSE3] = "mat3 tide_inverse3(mat3 m) {\n"
                   "    vec3 a = cross(m[1], m[2]);\n"
                   "    vec3 b = cross(m[2], m[0]);\n"
                   "    vec3 c = cross(m[0], m[1]);\n"
                   "    return transpose(mat3(a, b, c)) * tide_reciprocal(dot(m[0], a));\n"
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
                   "    float d = tide_reciprocal(s0 * k5 - s1 * k4 + s2 * k3 + s3 * k2 - s4 * k1 + s5 * k0);\n"
                   "    return mat4((a11 * k5 - a12 * k4 + a13 * k3) * d, (-a10 * k5 + a12 * k2 - a13 * k1) * d, "
                   "(a10 * k4 - a11 * k2 + a13 * k0) * d, (-a10 * k3 + a11 * k1 - a12 * k0) * d, "
                   "(-a01 * k5 + a02 * k4 - a03 * k3) * d, (a00 * k5 - a02 * k2 + a03 * k1) * d, "
                   "(-a00 * k4 + a01 * k2 - a03 * k0) * d, (a00 * k3 - a01 * k1 + a02 * k0) * d, "
                   "(a31 * s5 - a32 * s4 + a33 * s3) * d, (-a30 * s5 + a32 * s2 - a33 * s1) * d, "
                   "(a30 * s4 - a31 * s2 + a33 * s0) * d, (-a30 * s3 + a31 * s1 - a32 * s0) * d, "
                   "(-a21 * s5 + a22 * s4 - a23 * s3) * d, (a20 * s5 - a22 * s2 + a23 * s1) * d, "
                   "(-a20 * s4 + a21 * s2 - a23 * s0) * d, (a20 * s3 - a21 * s1 + a22 * s0) * d);\n"
                   "}\n",
    // What a mesh's normals go through on their way to the world (instance.Normal): the inverse of the transform's
    // 3 by 3, transposed, but for its length, which the normalize after it takes away. That's the cross products of
    // pairs of the columns, tide_inverse3's rows before they're divided by the determinant, and turned round where
    // the determinant is under 0, which is a transform that mirrors its mesh. So nothing is divided: a transform
    // that flattens a mesh has no inverse, and its normals are still along the axis it flattened
    [H_NORMALS] = "mat3 tide_normals(mat4 m) {\n"
                  "    vec3 a = cross(m[1].xyz, m[2].xyz);\n"
                  "    vec3 b = cross(m[2].xyz, m[0].xyz);\n"
                  "    vec3 c = cross(m[0].xyz, m[1].xyz);\n"
                  "    if (dot(m[0].xyz, a) < 0.0) {\n"
                  "        return mat3(-a, -b, -c);\n"
                  "    }\n"
                  "    return mat3(a, b, c);\n"
                  "}\n",
    // What a frag or a vert rolls with (Random.NextInt and the rest, on the type): `tide_dice`, a generator of the
    // pixel's or the vertex's own, which its stage makes as it starts (put_dice). Its state is 32 bits, as these GPUs
    // have no 64-bit integers, so it isn't the CPU's (tide/random.h): PCG's RXS-M-XS (O'Neill), a linear
    // congruential step, and the state out through a shift its own top bits pick. Whole numbers, so the same on
    // every GPU. What's made of the 32 bits is the CPU's formulas, line for line
    [H_ROLL] = "uint tide_roll() {\n"
               "    tide_dice = tide_dice * 747796405u + 2891336453u;\n"
               "    uint w = ((tide_dice >> ((tide_dice >> 28) + 4u)) ^ tide_dice) * 277803737u;\n"
               "    return (w >> 22) ^ w;\n"
               "}\n",
    // The high 32 bits of a times b, a half at a time: there's no 64-bit product to shift down
    [H_MUL_HIGH] = "uint tide_mul_high(uint a, uint b) {\n"
                   "    uint p = (a & 0xFFFFu) * (b & 0xFFFFu);\n"
                   "    uint q = (a >> 16) * (b & 0xFFFFu) + (p >> 16);\n"
                   "    uint s = (a & 0xFFFFu) * (b >> 16) + (q & 0xFFFFu);\n"
                   "    return (a >> 16) * (b >> 16) + (q >> 16) + (s >> 16);\n"
                   "}\n",
    // From 0 to 2147483647, so % never goes negative
    [H_ROLL_INT] = "int tide_roll_int() {\n"
                   "    return int(tide_roll() >> 1);\n"
                   "}\n",
    // From a up to b, b excluded, and a where there's nothing between them: the 32 bits scaled down to the range
    [H_ROLL_INT_BETWEEN] = "int tide_roll_int_between(int a, int b) {\n"
                           "    uint k = tide_roll();\n"
                           "    if (b <= a) { return a; }\n"
                           "    return int(uint(a) + tide_mul_high(k, uint(b) - uint(a)));\n"
                           "}\n",
    [H_ROLL_INT_BELOW] = "int tide_roll_int_below(int b) {\n"
                         "    return tide_roll_int_between(0, b);\n"
                         "}\n",
    // From 0 up to 1, 1 excluded: 24 of the bits over 2^24, which a float holds exactly
    [H_ROLL_FLOAT] = "float tide_roll_float() {\n"
                     "    return float(tide_roll() >> 8) * (1.0 / 16777216.0);\n"
                     "}\n",
    // From a up to b, b excluded: a where there's nothing between them, or where rounding would give b
    [H_ROLL_FLOAT_BETWEEN] = "float tide_roll_float_between(float a, float b) {\n"
                             "    float v = a + (b - a) * tide_roll_float();\n"
                             "    if (v < b) { return v; }\n"
                             "    return a;\n"
                             "}\n",
    [H_ROLL_FLOAT_BELOW] = "float tide_roll_float_below(float b) {\n"
                           "    return tide_roll_float_between(0.0, b);\n"
                           "}\n",
    [H_ROLL_BOOL] = "bool tide_roll_bool() {\n"
                    "    return (tide_roll() >> 31) != 0u;\n"
                    "}\n",
    // A vector's components are rolled a statement each, x first, as the CPU rolls them
    [H_ROLL_FLOAT2] = "vec2 tide_roll_float2() {\n"
                      "    float x = tide_roll_float();\n"
                      "    float y = tide_roll_float();\n"
                      "    return vec2(x, y);\n"
                      "}\n",
    [H_ROLL_FLOAT2_BETWEEN] = "vec2 tide_roll_float2_between(vec2 a, vec2 b) {\n"
                              "    float x = tide_roll_float_between(a.x, b.x);\n"
                              "    float y = tide_roll_float_between(a.y, b.y);\n"
                              "    return vec2(x, y);\n"
                              "}\n",
    [H_ROLL_FLOAT3] = "vec3 tide_roll_float3() {\n"
                      "    float x = tide_roll_float();\n"
                      "    float y = tide_roll_float();\n"
                      "    float z = tide_roll_float();\n"
                      "    return vec3(x, y, z);\n"
                      "}\n",
    [H_ROLL_FLOAT3_BETWEEN] = "vec3 tide_roll_float3_between(vec3 a, vec3 b) {\n"
                              "    float x = tide_roll_float_between(a.x, b.x);\n"
                              "    float y = tide_roll_float_between(a.y, b.y);\n"
                              "    float z = tide_roll_float_between(a.z, b.z);\n"
                              "    return vec3(x, y, z);\n"
                              "}\n",
    // A point on the unit circle, every angle as likely
    [H_ROLL_FLOAT2_DIRECTION] = "vec2 tide_roll_float2_direction() {\n"
                                "    float w = tide_roll_float() * 6.28318531;\n"
                                "    return vec2(cos(w), sin(w));\n"
                                "}\n",
    // A point on the unit sphere, every direction as likely
    [H_ROLL_FLOAT3_DIRECTION] = "vec3 tide_roll_float3_direction() {\n"
                                "    float z = tide_roll_float() * 2.0 - 1.0;\n"
                                "    float w = tide_roll_float() * 6.28318531;\n"
                                "    float s = sqrt(max(1.0 - z * z, 0.0));\n"
                                "    return vec3(cos(w) * s, sin(w) * s, z);\n"
                                "}\n",
    // A rotation, every one as likely, with w never negative
    [H_ROLL_QUATERNION_ROTATION] = "vec4 tide_roll_quaternion_rotation() {\n"
                                   "    float t = tide_roll_float() * 6.28318531;\n"
                                   "    float p = tide_roll_float() * 6.28318531;\n"
                                   "    float u = tide_roll_float();\n"
                                   "    float i = sqrt(1.0 - u);\n"
                                   "    float j = sqrt(u);\n"
                                   "    vec4 q = vec4(i * sin(t), i * cos(t), j * sin(p), j * cos(p));\n"
                                   "    if (q.w < 0.0) { return -q; }\n"
                                   "    return q;\n"
                                   "}\n",
};

// Random's rolls, by the function of tide/random.h's a call of one names (expr.c_callee): the function of the
// engine's own that's it here, and the helper that is.
static const struct {
    const char *cpu;
    int helper;
    const char *call;
} roll_calls[] = {
    {"tide_random_next_int", H_ROLL_INT, "tide_roll_int"},
    {"tide_random_next_int_below", H_ROLL_INT_BELOW, "tide_roll_int_below"},
    {"tide_random_next_int_between", H_ROLL_INT_BETWEEN, "tide_roll_int_between"},
    {"tide_random_next_float", H_ROLL_FLOAT, "tide_roll_float"},
    {"tide_random_next_float_below", H_ROLL_FLOAT_BELOW, "tide_roll_float_below"},
    {"tide_random_next_float_between", H_ROLL_FLOAT_BETWEEN, "tide_roll_float_between"},
    {"tide_random_next_bool", H_ROLL_BOOL, "tide_roll_bool"},
    {"tide_random_next_float2", H_ROLL_FLOAT2, "tide_roll_float2"},
    {"tide_random_next_float2_between", H_ROLL_FLOAT2_BETWEEN, "tide_roll_float2_between"},
    {"tide_random_next_float3", H_ROLL_FLOAT3, "tide_roll_float3"},
    {"tide_random_next_float3_between", H_ROLL_FLOAT3_BETWEEN, "tide_roll_float3_between"},
    {"tide_random_next_float2_direction", H_ROLL_FLOAT2_DIRECTION, "tide_roll_float2_direction"},
    {"tide_random_next_float3_direction", H_ROLL_FLOAT3_DIRECTION, "tide_roll_float3_direction"},
    {"tide_random_next_quaternion_rotation", H_ROLL_QUATERNION_ROTATION, "tide_roll_quaternion_rotation"},
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
    case H_EASE:
        need(em, family_helper(FAMILY_SATURATE, 1));
        break;
    case H_INVERSE2:
    case H_INVERSE3:
    case H_INVERSE4:
        need(em, H_RECIPROCAL);
        break;
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
        need(em, H_XXH);
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
        need(em, H_FROMTO_Q);
        break;
    case H_FROMTO_UNIT3:
        need(em, H_NORMALIZE3);
        break;
    case H_FROMTO_Q:
        need(em, H_FROMTO_UNIT3);
        need(em, H_NORMALIZE_Q);
        break;
    case H_TRS:
        need(em, H_M3_FROM_Q);
        break;
    case H_ROLL_INT:
    case H_ROLL_FLOAT:
    case H_ROLL_BOOL:
        need(em, H_ROLL);
        break;
    case H_ROLL_INT_BETWEEN:
        need(em, H_ROLL);
        need(em, H_MUL_HIGH);
        break;
    case H_ROLL_INT_BELOW:
        need(em, H_ROLL_INT_BETWEEN);
        break;
    case H_ROLL_FLOAT_BETWEEN:
    case H_ROLL_FLOAT2:
    case H_ROLL_FLOAT3:
    case H_ROLL_FLOAT2_DIRECTION:
    case H_ROLL_FLOAT3_DIRECTION:
    case H_ROLL_QUATERNION_ROTATION:
        need(em, H_ROLL_FLOAT);
        break;
    case H_ROLL_FLOAT_BELOW:
    case H_ROLL_FLOAT2_BETWEEN:
    case H_ROLL_FLOAT3_BETWEEN:
        need(em, H_ROLL_FLOAT_BETWEEN);
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

// `len` chars of a helper, word by word, as WGSL spells them.
static void put_spelled(sb *o, const char *text, const size_t len)
{
    for (size_t i = 0; i < len;) {
        size_t n = 1;
        while (word_char(text[i]) && i + n < len && word_char(text[i + n])) n++;
        const char *as = NULL;
        if (word_is(text + i, n, "inversesqrt")) {
            as = "inverseSqrt";
        } else if (word_is(text + i, n, "floatBitsToInt")) {
            as = "bitcast<i32>";
        } else if (word_is(text + i, n, "intBitsToFloat")) {
            as = "bitcast<f32>";
        } else {
            as = wgsl_type(text + i, n);
        }
        if (as) sb_put(o, as);
        else sb_putn(o, text + i, n);
        i += n;
    }
}

// A helper, from the notation helper_text writes it in, as WGSL has it. WGSL
// declares the other way around: a function's head is `fn name(a: T) -> T`,
// and a local `var name: T = value`.
static void put_helper(sb *o, const char *text)
{
    for (const char *line = text; *line;) {
        const char *end = strchr(line, '\n');
        size_t indent = 0, type_len = 0, name_len = 0;
        while (line[indent] == ' ') indent++;
        while (word_char(line[indent + type_len])) type_len++;
        // What the line declares, if it starts with a type and a name
        const char *type = line[indent + type_len] == ' ' ? wgsl_type(line + indent, type_len) : NULL;
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
            put_spelled(o, value, (size_t)(end - value));
            sb_put(o, "\n");
        } else {
            put_spelled(o, line, (size_t)(end - line));
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
        put_helper(o, text.data);
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
    sb_printf(em->o, "%stide_%c%d", uniform ? "tide_block." : "", stream, word / 4);
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
    } else if (t.kind == TY_ENUM || t.kind == TY_CHAR) {
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

static void put_zeros(emit *em, type t);

// The same for an element of a list, of which only the parts `leaves` says
// are read are in the streams (shader_leaves, from *leaf on, which moves past
// it): the others are zeros, which nothing reads.
static void put_part(emit *em, const type t, const char floats, const char ints, int *f, int *i, const bool *leaves, int *leaf)
{
    if (!leaves) {
        put_value(em, t, floats, ints, f, i);
        return;
    }
    if (!is_struct_like(t)) {
        if (leaves[(*leaf)++]) put_value(em, t, floats, ints, f, i);
        else put_zeros(em, t);
        return;
    }
    sb_printf(em->o, "%s(", ty(em, t));
    bool first = true;
    for (int k = 0; k < t.decl->fields.count; k++) {
        const field *fl = &t.decl->fields.items[k];
        if (fl->hidden) continue;
        if (!first) sb_put(em->o, ", ");
        first = false;
        put_part(em, fl->type, floats, ints, f, i, leaves, leaf);
    }
    sb_put(em->o, ")");
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
            sb_put(em->o, "mat4x4f(1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0)");
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
    if (t.kind == TY_MAP) return (type){TY_STRUCT, t.decl->entry}; // A map's entries: each a key and a value
    return t.decl->fields.items[0].type; // A list's elements, a grid's cells, or a set's keys
}

static bool is_data(const type t)
{
    return (t.kind == TY_LIST || t.kind == TY_GRID || t.kind == TY_MAP || t.kind == TY_SET) && t.decl;
}

static const char *data_word(const type t)
{
    return t.kind == TY_LIST ? "list" : t.kind == TY_GRID ? "grid" : t.kind == TY_MAP ? "map" : "set";
}

// ---------------------------------------------------------------------------
// Structs that hold lists
//
// A struct, a singleton or a component with a list, a grid, a map or a set
// among its fields is no GPU value, as a list isn't. But one the frag or vert
// was given (the draw's values, a singleton, a component) can go to a
// function, `float4x4 Skin(Poses poses, int bone)`, and have methods called
// on it, `poses.Skin(bone)`, as a list can: the function is handed nothing
// for it, and reads its fields where the frag or vert would, as everything
// the engine supplies is the stage's own (put_bound: the uniforms, and each
// instance's attributes, are the shader's globals in both languages), and its
// lists as the frag's textures of numbers (data_of), by its type and field.
// What a function reads of one is noted on its parameter, or on the method
// for its receiver, as its body is checked once (given_reads, self_reads),
// and each call reads that of what it passes (pass_given_reads).

static bool holds_data(const type t)
{
    if (!is_struct_like(t) || !t.decl) return false;
    for (int i = 0; i < t.decl->fields.count; i++) {
        if (is_data(t.decl->fields.items[i].type) || t.decl->fields.items[i].type.kind == TY_GPU_LIST) return true;
    }
    const decl *part = t.kind == TY_STRUCT ? NULL : t.decl->local_part;
    for (int i = 0; part && i < part->fields.count; i++) {
        if (is_data(part->fields.items[i].type) || part->fields.items[i].type.kind == TY_GPU_LIST) return true;
    }
    return false;
}

// A method of a type that holds a list: its receiver is given, never passed.
static bool given_self(const decl *fn)
{
    return fn && fn->owner && !fn->is_operator && !fn->is_interpolate && fn->kind != DECL_SHADER
        && holds_data((type){fn->owner->kind == DECL_COMPONENT ? TY_COMPONENT
                             : fn->owner->kind == DECL_SINGLETON ? TY_SINGLETON : TY_STRUCT, fn->owner});
}

// Whether `e` names a struct that holds a list, one the code was given: the
// frag's or vert's own, or a function's parameter, which stands for its caller's.
static bool names_given(const emit *em, const expr *e)
{
    return e && e->kind == E_NAME && e->bind == BIND_PARAM && holds_data(e->param->type)
        && (is_bound(em, e->param) || e->param->function_param);
}

static bool *reads_room(bool **reads, const decl *d)
{
    if (!*reads && d) *reads = arena_alloc((size_t)(d->fields.count + 1) * sizeof(bool));
    return *reads;
}

// While checking: that the code reads field `f` (a local field where `local`)
// of what `object` names, a field of what the frag or vert was given, or of a
// struct a function was given, or of a method's receiver where `object` is
// NULL. A grid's is a texture of numbers of the frag's or vert's (data_of).
static void note_given_field(emit *em, const expr *object, const field *f, const bool local)
{
    if (!em->checking || !f) return;
    if (!object) { // The method's receiver
        decl *fn = em->code;
        const decl *holds = local ? fn->owner->local_part : fn->owner;
        bool *reads = reads_room(local ? &fn->self_local_reads : &fn->self_reads, holds);
        if (reads) reads[field_index(holds, f)] = true;
        return;
    }
    param *p = (param *)(uintptr_t)object->param;
    const decl *holds = local ? p->type.decl->local_part : p->type.decl;
    if (!holds) return;
    if (!is_bound(em, p)) { // A function's: its callers read it of what they pass
        bool *reads = reads_room(local ? &p->given_local_reads : &p->given_reads, holds);
        reads[field_index(holds, f)] = true;
        return;
    }
    param *own = &em->root->params.items[param_index(em->root, p)];
    if (f->type.kind != TY_GRID) {
        if (local && own->local_reads) own->local_reads[field_index(holds, f)] = true;
        else if (!local && own->reads) own->reads[field_index(holds, f)] = true;
        return;
    }
    const shader_sample sample = {param_index(em->root, p), field_index(holds, f), local, 0, NULL, 0, READ_DATA, true, true};
    for (int i = 0; i < em->root->samples.count; i++) {
        const shader_sample *s = &em->root->samples.items[i];
        if (s->param == sample.param && s->field == sample.field && s->local == sample.local && !s->asset
            && s->read == READ_DATA) return;
    }
    vec_push(em->root->samples, sample);
}

// While checking: what a call's function reads of a struct it's given, the
// code that passes it reads of what it passes (`object`, NULL for its own receiver).
static void pass_given_reads(emit *em, const expr *object, const decl *type_decl, const bool *reads, const bool *local_reads)
{
    for (int k = 0; reads && k < type_decl->fields.count; k++) {
        if (reads[k]) note_given_field(em, object, &type_decl->fields.items[k], false);
    }
    const decl *part = type_decl->local_part;
    for (int k = 0; local_reads && part && k < part->fields.count; k++) {
        if (local_reads[k]) note_given_field(em, object, &part->fields.items[k], true);
    }
}

// A data_ref for a value of type `t`, which no texture is found for yet.
static data_ref data_unknown(const type t)
{
    return (data_ref){.texture = -1, .element = data_element(t), .grid = t.kind == TY_GRID ? t.decl->dims : 0,
                      .map = t.kind == TY_MAP || t.kind == TY_SET ? t.decl : NULL};
}

// Whether the GPU reads a map or a set of type `t`: its keys are ints there,
// which it hashes and compares, and its values what it has values of.
static bool map_fits(emit *em, const type t, const loc at)
{
    const type key = t.decl->fields.items[0].type;
    const char *why = NULL;
    switch (key.kind) {
    case TY_INT: case TY_BYTE: case TY_ENUM: case TY_CHAR: break;
    case TY_STRING: why = "text is the CPU's"; break;
    case TY_LONG: why = "the GPU's ints are 32 bits"; break;
    default: why = "it's a handle into the world, which the GPU doesn't have"; break;
    }
    if (why) {
        refuse(em, at, "a %s keyed by %s can't be read on the GPU: %s", data_word(t), type_name(key), why);
        diag_note("the GPU reads a %s whose keys are ints, bytes or enums: keep one keyed that way for what it reads", data_word(t));
        return false;
    }
    return t.kind == TY_SET || type_fits(em, t.decl->fields.items[1].type, at);
}

// Where the code notes the parts it reads of an element of a list of type
// `t`: made the first time the list is read. A map's key is always read: its
// entries are found by it.
static bool *leaves_room(bool **held, const type t)
{
    if (!*held) {
        *held = arena_alloc((size_t)(shader_leaves(data_element(t)) + 1) * sizeof(bool));
        if (t.kind == TY_MAP || t.kind == TY_SET) (*held)[0] = true;
    }
    return *held;
}

// The expression a chain of struct fields starts from, `a` in `a.b.c`, and in
// *first, where the parts of the chain's last field start among a's.
static const expr *chain_root(const expr *e, int *first)
{
    *first = 0;
    while (e->kind == E_MEMBER && e->field && !e->local_field && !e->swizzle_len && !e->constant && !e->enum_member
           && !e->c_constant && is_struct_like(e->object->type) && e->object->type.decl
           && e->field >= e->object->type.decl->fields.items
           && e->field < e->object->type.decl->fields.items + e->object->type.decl->fields.count) {
        *first += leaf_offset(e->object->type.decl, e->field);
        e = e->object;
    }
    return e;
}

static bool may_be_element(const expr *e)
{
    return e->kind == E_INDEX || (e->kind == E_NAME && e->bind == BIND_LOCAL);
}

// `e` reads a field of a struct. Where that's a field of an element of a list
// the code was given (`lights[i].color`, or `light.color` of a local that
// holds one), only that part of the element is read, which the element is
// told as it's written (read_element): the whole chain's, the first time one
// of its links comes here.
static void note_part(emit *em, const expr *e)
{
    int first;
    const expr *root = chain_root(e, &first);
    if (root == e || !may_be_element(root) || em->part_root == root) return;
    em->part_root = root;
    em->part_first = first;
    em->part_count = shader_leaves(e->type);
}

// The code reads an element of a list it was given where `e` is written, an
// index or a local that holds one: its parts from `base` on, `count` of them.
// All of those, unless `e` starts a chain of fields, which reads a part of
// them, or is what a local being declared holds, whose own reads then say.
static void read_element(emit *em, const expr *e, bool *leaves, int base, int count)
{
    if (em->part_root == e) {
        em->part_root = NULL;
        base += em->part_first;
        count = em->part_count;
    }
    if (em->held_root == e) {
        em->held_root = NULL;
        const element_alias alias = {em->held_by, leaves, base, count};
        vec_push(em->elements, alias);
        return;
    }
    for (int i = 0; em->checking && leaves && i < count; i++) leaves[base + i] = true;
}

// `e` isn't an element after all: nothing waits for it to be written.
static void no_element(emit *em, const expr *e)
{
    if (em->part_root == e) em->part_root = NULL;
    if (em->held_root == e) em->held_root = NULL;
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
    if (!e || !is_data(e->type)) return false;
    *d = data_unknown(e->type);
    if (e->kind == E_NAME && e->bind == BIND_LOCAL) { // A local that names one
        for (int i = em->aliases.count - 1; i >= 0; i--) {
            if (em->aliases.items[i].local != e->local) continue;
            *d = em->aliases.items[i].data;
            return true;
        }
        return false;
    }
    if (e->kind == E_NAME && e->bind == BIND_PARAM && e->param->function_param && em->code != em->root) { // Its function's
        if (em->spec) *d = em->spec[param_index(em->code, e->param)].data;
        // What the function reads of an element is its parameter's to say, for each of its calls (ex_user_call)
        if (em->checking) d->leaves = leaves_room(&em->code->params.items[param_index(em->code, e->param)].gpu_leaves, e->type);
        return true;
    }
    const decl *asset = NULL;
    const decl *source = NULL;     // Whose field it is: the struct, singleton or component's
    const expr *object = NULL;     // ...as the code names it, NULL for a method's receiver
    if (e->kind == E_NAME && e->bind == BIND_ASSET && e->constant) {
        asset = e->constant;
    } else if (e->kind == E_MEMBER && e->field && e->object->kind == E_NAME && e->object->bind == BIND_PARAM
               && is_struct_like(e->object->param->type)
               && (is_bound(em, e->object->param) || names_given(em, e->object))) {
        object = e->object;
        source = object->param->type.decl;
    } else if (e->kind == E_NAME && e->bind == BIND_FIELD && e->field && given_self(em->code)) { // A field of its receiver
        source = em->code->owner;
    } else if (e->kind == E_MEMBER && e->object->type.kind == TY_MESH) { // tree.vertices, tree.indices
        refuse(em, e->at, "a Mesh's vertices and indices aren't read as lists in code the GPU runs: the draw sends them, as the "
                          "mesh they are");
        diag_note("what a frag or a vert needs of them, copy into a list of a singleton it takes, which it reads by that name");
        return true;
    } else {
        return false;
    }
    if (d->map ? !map_fits(em, e->type, e->at) : !type_fits(em, d->element, e->at)) return true;
    if (source) { // A field's elements: what's read of them is noted on the field, for every frag and vert that reads it
        field *held = (field *)(uintptr_t)e->field;
        d->leaves = em->checking ? leaves_room(&held->gpu_leaves, e->type) : held->gpu_leaves;
    }
    if (em->checking) {
        if (asset) note_asset(em, asset);
        else note_given_field(em, object, e->field, e->local_field);
        return true;
    }
    for (int i = 0; i < em->sh->textures.count; i++) {
        const shader_texture *t = &em->sh->textures.items[i];
        if (t->read == READ_DATA && t->asset == asset && t->source == source && t->field == (source ? e->field : NULL)) {
            d->texture = i;
        }
    }
    return true;
}

// All of a value as zeros, whatever its fields' defaults are.
static void put_zeros(emit *em, const type t)
{
    sb_printf(em->o, "%s()", ty(em, t));
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
// cell of its grid of `dims` axes (0 for a list), written before the code
// that calls it: zeros outside it, and otherwise the value made of its ints,
// floats by their bits. A grid's texture is the box of cells its chunks take
// up (tide_draw_grid_data): the uniforms' ints say, after the grid's size,
// where the box starts and how many cells it is along each axis, and for a
// Grid3 how many of its layers are side by side. A place is outside the box
// when it's before its start or as far past it as the box is long, which is
// asked with unsigned numbers: a place and the box's start can be further
// apart than an int says. Those same unsigned numbers are where the cell is
// in the box, and nothing subtracts the two again as ints (with a place
// that's written as numbers, 'cells[3, 1]', OpenGL 3.3 on an NVIDIA driver
// read another pixel than the one asked for when it did).
static void need_data(emit *em, const int texture, const type element, const int dims)
{
    const bool grid = dims > 0;
    if (texture < 0 || em->lists[texture]) return;
    em->lists[texture] = true;
    // Only the parts of an element that the game's code reads are in the texture (shader_leaves)
    const bool *leaves = shader_texture_leaves(&em->sh->textures.items[texture]);
    int floats = 0, ints = 0, leaf = 0;
    shader_part_words(element, leaves, &leaf, &floats, &ints);
    const int words = floats + ints;
    const int stride = data_stride(words), pixels = words == 0 ? 0 : stride < 4 ? 1 : stride / 4;
    const int count_at = em->sh->textures.items[texture].count_at;
    sb text = {0};
    sb *const out = em->o;
    em->o = &text;
    const char *t = ty(em, element);
    if (grid) {
        const char *vec = dims == 3 ? "vec3i" : "vec2i";
        sb_printf(&text, "fn tide_list%d(tide_i: %s) -> %s {\n    let tide_o = ", texture, vec, t);
        put_words(em, 'v', count_at + dims, dims, vec);
        sb_put(&text, ";\n    let tide_e = ");
        put_words(em, 'v', count_at + 2 * dims, dims, vec);
        sb_put(&text, ";\n");
        for (int a = 0; a < dims; a++) {
            const char axis = "xyz"[a];
            sb_printf(&text, "    let tide_%c = u32(tide_i.%c) - u32(tide_o.%c);\n", axis, axis, axis);
        }
        sb_put(&text, "    if (");
        for (int a = 0; a < dims; a++) {
            const char axis = "xyz"[a];
            sb_printf(&text, "%stide_i.%c < tide_o.%c || tide_%c >= u32(tide_e.%c)", a ? " || " : "", axis, axis, axis, axis);
        }
    } else {
        sb_printf(&text, "fn tide_list%d(tide_i: i32) -> %s {\n    if (tide_i < 0 || tide_i >= ", texture, t);
        put_word(em, 'v', count_at);
    }
    sb_put(&text, ") {\n        return ");
    put_zeros(em, element);
    sb_put(&text, ";\n    }\n");
    if (pixels) { // Nothing of it is read, where the code only counts its elements, or asks a grid's size
        sb_printf(&text, "    let tide_word = %s%s * %d;\n    let tide_first = tide_word >> 2u;\n", grid ? "i32" : "",
                  grid ? "(tide_x)" : "tide_i", stride);
        if (dims == 3) { // Its layers side by side, then under each other
            sb_put(&text, "    let tide_layer = i32(tide_z);\n    let tide_across = ");
            put_word(em, 'v', count_at + 9);
            sb_printf(&text, ";\n    let tide_left = (tide_layer %% tide_across) * ((tide_e.x * %d + 3) >> 2u);\n"
                             "    let tide_row = (tide_layer / tide_across) * tide_e.y + i32(tide_y);\n",
                      stride);
        } else if (grid) {
            sb_put(&text, "    let tide_row = i32(tide_y);\n");
        }
    }
    for (int k = 0; k < pixels; k++) {
        char name[16], at[96];
        if (stride < 4) snprintf(name, sizeof name, "tide_r");
        else snprintf(name, sizeof name, "tide_q%d", k);
        // A list's rows are TIDE_LIST_DATA_ACROSS pixels; a grid's are its own
        if (dims == 3) snprintf(at, sizeof at, "tide_left + tide_first + %d, tide_row", k);
        else if (grid) snprintf(at, sizeof at, "tide_first + %d, tide_row", k);
        else snprintf(at, sizeof at, "(tide_first + %d) & 1023, (tide_first + %d) >> 10u", k, k);
        sb_printf(&text, "    let %s = textureLoad(pixels%d, vec2i(%s), 0);\n", name, texture, at);
        if (stride < 4) { // Several to a pixel: this one's ints, from where it starts in it
            sb_printf(&text, "    let tide_q0 = vec4i(tide_r[tide_word & 3], %s, 0, 0);\n",
                      stride == 2 ? "tide_r[(tide_word & 3) + 1]" : "0");
        }
        if (k * 4 < floats) sb_printf(&text, "    let tide_p%d = bitcast<vec4f>(tide_q%d);\n", k, k);
    }
    sb_put(&text, "    return ");
    int f = 0, n = floats;
    leaf = 0;
    put_part(em, element, 'p', 'q', &f, &n, leaves, &leaf);
    sb_put(&text, ";\n}\n\n");
    em->o = out;
    sb_put(&em->functions, text.data);
}

// ---------------------------------------------------------------------------
// Maps and sets
//
// A map or a set the code names is read by its keys: `map[key]`, Has, TryGet,
// `count` and foreach. It reaches the GPU as a list does, in one texture
// (tide_draw_map_data): its live entries in the order their keys were added,
// each as a list's element is (a map's as a struct of its key and its value,
// a set's as its key), and after them, from the next pixel on, an index of
// the GPU's own: a power of two of slots, an int each, 0 or an entry's number
// plus one, by Math.Hash of the key with linear probing. The CPU's own index
// is by a hash of 64 bits, which these GPUs can't work out. The draw's ints
// say how many entries, and how many slots.

// The function that finds a key's entry in the shader's map: its number, or -1.
static void need_find(emit *em, const data_ref *d)
{
    const int texture = d->texture;
    if (texture < 0) return;
    need_data(em, texture, d->element, 0);
    if (em->finds[texture]) return;
    em->finds[texture] = true;
    need(em, H_HASH1);
    // Where an entry's key is among its words: its ints come after its floats, which are all its value's, and the
    // key is the first of them. Of the value, only the parts that are read are there (need_data)
    int floats = 0, ints = 0, leaf = 0;
    shader_part_words(d->element, shader_texture_leaves(&em->sh->textures.items[texture]), &leaf, &floats, &ints);
    const int stride = data_stride(floats + ints), key_word = floats;
    const int count_at = em->sh->textures.items[texture].count_at;
    sb text = {0};
    sb *const out = em->o;
    em->o = &text;
    sb_printf(&text, "fn tide_find%d(tide_key: i32) -> i32 {\n", texture);
    sb_put(&text, "    let tide_slots = ");
    put_word(em, 'v', count_at + 1);
    sb_put(&text, ";\n    if (tide_slots <= 0) {\n        return -1;\n    }\n    let tide_base = ((");
    put_word(em, 'v', count_at);
    sb_printf(&text, " * %d + 3) >> 2u) << 2u;\n", stride);
    sb_put(&text, "    var tide_s = tide_hash1(tide_key) & (tide_slots - 1);\n");
    sb_put(&text, "    for (var tide_n: i32 = 0; tide_n < tide_slots; tide_n++) {\n");
    const char *fetch = "        let %s = textureLoad(pixels%d, vec2i((%s >> 2u) & 1023, %s >> 12u), 0);\n";
    sb_put(&text, "        let tide_w = tide_base + tide_s;\n");
    sb_printf(&text, fetch, "tide_slot", texture, "tide_w", "tide_w");
    sb_put(&text, "        let tide_e = tide_slot[tide_w & 3];\n");
    sb_put(&text, "        if (tide_e == 0) {\n            return -1;\n        }\n");
    sb_printf(&text, "        let tide_k = (tide_e - 1) * %d + %d;\n", stride, key_word);
    sb_printf(&text, fetch, "tide_held", texture, "tide_k", "tide_k");
    sb_put(&text, "        if (tide_held[tide_k & 3] == tide_key) {\n");
    sb_put(&text, "            return tide_e - 1;\n        }\n"
                  "        tide_s = (tide_s + 1) & (tide_slots - 1);\n    }\n    return -1;\n}\n\n");
    em->o = out;
    sb_put(&em->functions, text.data);
}

// The function that's the shader's map's TryGet: whether the key is there,
// and into the caller's variable its value, or the value type's default.
static void need_try(emit *em, const data_ref *d)
{
    const int texture = d->texture;
    if (texture < 0) return;
    need_find(em, d);
    if (em->tries[texture]) return;
    em->tries[texture] = true;
    const type value = d->map->fields.items[1].type;
    sb text = {0};
    sb *const out = em->o;
    em->o = &text;
    const char *t = ty(em, value);
    sb_printf(&text, "fn tide_try%d(tide_key: i32, tide_value: ptr<function, %s>) -> bool {\n    let", texture, t);
    sb_printf(&text, " tide_e = tide_find%d(tide_key);\n    if (tide_e < 0) {\n        *tide_value = ", texture);
    put_default(em, value);
    sb_printf(&text, ";\n        return false;\n    }\n    *tide_value = tide_list%d(tide_e).%s;\n    return true;\n}\n\n",
              texture, field_name(&d->map->entry->fields.items[1]));
    em->o = out;
    sb_put(&em->functions, text.data);
}

// Has and TryGet of a map or a set the code names. What changes one is the CPU's.
static void ex_map_call(emit *em, const expr *e)
{
    const bool set = e->object->type.kind == TY_SET;
    if (str_eq_c(e->name, "Add") || str_eq_c(e->name, "Remove") || str_eq_c(e->name, "Clear")) {
        refuse(em, e->at, "the GPU can't change a %s: it reads the one a system or a view filled", set ? "set" : "map");
        diag_note(set ? "code the GPU runs has 'Has(key)' and 'count', and goes through a set with foreach"
                      : "code the GPU runs has 'map[key]', 'Has(key)', 'TryGet(key, out value)' and 'count', and goes through a "
                        "map with foreach");
        return;
    }
    if (str_eq_c(e->name, "Get")) {
        refuse(em, e->at, "Get gives a value that may be missing, and the GPU has none of those");
        diag_note("ask with 'TryGet(key, out value)', or read 'map[key]', which is zeros for a key that isn't there");
        return;
    }
    data_ref data;
    if (!data_of(em, e->object, &data)) {
        type_fits(em, e->object->type, e->object->at);
        return;
    }
    const int t = data.texture < 0 ? 0 : data.texture;
    const type key = data.map->fields.items[0].type;
    if (key.kind != TY_INT && key.kind != TY_BYTE && key.kind != TY_ENUM && key.kind != TY_CHAR) return; // Said, where the map was named
    if (str_eq_c(e->name, "Has")) {
        need_find(em, &data);
        sb_printf(em->o, "(tide_find%d(", t);
        ex_as(em, e->args.items[0], key);
        sb_put(em->o, ") >= 0)");
        return;
    }
    // TryGet(key, out value): the variable itself, as a function's mut argument is handed over
    const expr *to = e->args.items[1];
    if (!is_whole_variable(to)) {
        refuse(em, to->at, "TryGet's value goes into a local in code the GPU runs: 'TryGet(key, out var value)'");
        return;
    }
    // All of the value goes into the variable: every part of it is read
    for (int k = 0; em->checking && data.leaves && k < shader_leaves(data.map->fields.items[1].type); k++) data.leaves[1 + k] = true;
    need_try(em, &data);
    sb_printf(em->o, "tide_try%d(", t);
    ex_as(em, e->args.items[0], key);
    const bool address = !(to->bind == BIND_PARAM && is_pointer(to->param));
    sb_printf(em->o, ", %s%s)", address ? "&" : "", local_name(to->name));
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
    sb_printf(&text, "fn tide_el%d(tide_a: %s, tide_i: i32) -> %s {\n    if (tide_i < 0 || tide_i >= %d) {\n        return %s();\n"
                     "    }\n    var tide_all = tide_a;\n    return tide_all[tide_i];\n}\n\n", n, a, t, array->array_length, t);
    em->o = out;
    sb_put(&em->functions, text.data);
    return n;
}

// The function that picks between two values of type `t`, a struct, a matrix
// or an array, for '?:': written before the code that calls it.
static int need_pick(emit *em, const type t)
{
    for (int i = 0; i < em->picks.count; i++) {
        if (em->picks.items[i].kind == t.kind && em->picks.items[i].decl == t.decl) return i;
    }
    vec_push(em->picks, t);
    const int n = em->picks.count - 1;
    const char *name = ty(em, t);
    sb text = {0};
    sb_printf(&text, "fn tide_pick%d(tide_c: bool, tide_a: %s, tide_b: %s) -> %s {\n", n, name, name, name);
    sb_put(&text, "    if (tide_c) {\n        return tide_a;\n    }\n    return tide_b;\n}\n\n");
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

// Random.seed: the seed of the match the frame draws, which its draw sends.
static bool is_seed(const expr *e)
{
    return e->kind == E_MEMBER && e->c_constant && e->object && e->object->kind == E_NAME && str_eq_c(e->object->name, "Random");
}

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
        if (is_seed(e)) return false; // A draw's, not the shader's text's
        return e->c_constant || e->enum_member || e->constant || (e->swizzle_len && is_const(e->object));
    case E_UNARY:
        return !e->method && is_const(e->lhs);
    case E_BINARY:
        return !e->method && is_const(e->lhs) && is_const(e->rhs);
    case E_CONDITIONAL:
        return is_const(e->cond) && is_const(e->lhs) && is_const(e->rhs);
    case E_SWITCH:
        if (!is_const(e->object)) return false;
        for (int i = 0; i < e->args.count; i++) if (!is_const(e->args.items[i])) return false;
        return true;
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

static bool is_pointer(const param *p)
{
    return p->function_param && p->mode == PARAM_MUT;
}

static void put_self(emit *em)
{
    sb_put(em->o, em->code->is_mut_method ? "(*tide_self)" : "tide_self");
}

static void ex_name(emit *em, const expr *e)
{
    if (is_data(e->type)) { // A name for one, where a value goes
        type_fits(em, e->type, e->at);
        return;
    }
    switch (e->bind) {
    case BIND_LOCAL:
        if (em->lifting) { // Before it's declared: its value, which nothing changed (liftable)
            no_element(em, e); // What that value reads of a list's element, it says itself
            sb_put(em->o, "(");
            ex_as(em, e->local->value, e->local->type);
            sb_put(em->o, ")");
            return;
        }
        for (int i = em->elements.count - 1; i >= 0; i--) { // One that holds an element of a list: what's read of it
            const element_alias *held = &em->elements.items[i];
            if (held->local != e->local) continue;
            read_element(em, e, held->leaves, held->first, held->count);
            break;
        }
        no_element(em, e);
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
        if (holds_data(p->type)) { // A function's struct that holds a list: it's its caller's, read by its fields
            refuse(em, e->at, "'" STR_FMT "' holds a list, so code the GPU runs reads it by its fields or passes it on by its "
                              "name: it's not a value to keep or pick", STR_ARG(e->name));
            diag_note("read its fields, '" STR_FMT "." STR_FMT "', or pass '" STR_FMT "' to a function that takes a '%s'",
                      STR_ARG(e->name), STR_ARG(p->type.decl->fields.items[0].name), STR_ARG(e->name), type_name(p->type));
            return;
        }
        if (is_sampled(p->type)) { // A function's texture is sampled, or passed on: it's no value, and nothing is written for it
            type_fits(em, p->type, e->at);
            return;
        }
        sb_printf(em->o, is_pointer(p) ? "(*%s)" : "%s", local_name(p->name));
        return;
    }
    case BIND_FIELD:
        if (given_self(em->code)) { // A field of a receiver that holds a list: its caller's (Structs that hold lists)
            note_given_field(em, NULL, e->field, e->local_field);
            if (type_fits(em, e->type, e->at)) put_bound(em, em->code->owner, e->field, e->type);
            return;
        }
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
    if (is_seed(e)) { // A word of the draw's ints (codegen fills it); 0 while checking, when there's no shader
        if (em->sh && em->sh->seed_at >= 0) put_word(em, 'v', em->sh->seed_at);
        else sb_put(em->o, "0");
        return;
    }
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
    if (str_eq_c(e->member, "position")) { // The screen's pixels from the top left, as the GUI's
        sb_put(em->o, "(tide_at.xy / tide_block.tide_screen.xy)");
    } else if (str_eq_c(e->member, "depth")) {
        sb_put(em->o, "tide_at.z");
    } else {
        sb_put(em->o, "tide_world");
    }
}

// this.clip in a vert: where the engine would put its vertex as the vert has
// it at that line, in clip space. It's what the vertex stage's own code works
// out after a vert that doesn't place its vertex itself (put_vertex_main),
// written the same way, so a vert that writes it back unchanged draws where
// it would have.
static void ex_placed(emit *em)
{
    const param *vertex = em->root->params.count ? &em->root->params.items[0] : NULL;
    const field *position = vertex && vertex->type.kind == TY_STRUCT ? field_called(vertex->type.decl, "position") : NULL;
    if (!position) {
        put_zero(em, (type){TY_FLOAT4, NULL});
        return;
    }
    const bool flat = position->type.kind != TY_FLOAT3; // A 2D mesh: its camera has its transform in it
    if (flat) sb_put(em->o, "(tide_block.tide_camera * ");
    else sb_put(em->o, "tide_project(");
    if (!flat) {
        put_bound(em, NULL, NULL, (type){TY_FLOAT4X4, NULL});
        sb_put(em->o, ", ");
    }
    sb_printf(em->o, is_pointer(vertex) ? "%s((*%s).%s, " : "%s(%s.%s, ", float_ty(em, 4), local_name(vertex->name),
              field_name(position));
    sb_put(em->o, flat ? "0.0, 1.0))" : "1.0))");
}

static void ex_texture_size(emit *em, const expr *e);

static void ex_member(emit *em, const expr *e)
{
    if (((e->object->type.kind == TY_TEXTURE || e->object->type.kind == TY_DEPTH_TEXTURE) && str_eq_c(e->member, "size"))
        || ((e->object->type.kind == TY_TEXTURE_ARRAY || e->object->type.kind == TY_DEPTH_TEXTURE_ARRAY)
            && (str_eq_c(e->member, "size") || str_eq_c(e->member, "count")))) {
        ex_texture_size(em, e);
        return;
    }
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
        const int dims = obj->type.decl->dims;
        const char *vector = ty(em, (type){dims == 3 ? TY_INT3 : TY_INT2, NULL});
        if (data.texture < 0) sb_printf(em->o, "%s(0)", vector);
        else put_words(em, 'v', em->sh->textures.items[data.texture].count_at, dims, vector);
        return;
    }
    if (str_eq_c(e->member, "count") && (obj->type.kind == TY_LIST || obj->type.kind == TY_MAP || obj->type.kind == TY_SET)
        && data_of(em, obj, &data)) { // How many a list or a map has
        put_list_count(em, data.texture);
        return;
    }
    if (obj->kind == E_THIS && obj->type.kind == TY_RECORD && obj->type.decl == em->prog->pixel) {
        if (em->checking && str_eq_c(e->member, "world")) em->root->pixel_world = true;
        ex_pixel(em, e);
        return;
    }
    if (obj->kind == E_THIS && obj->type.kind == TY_RECORD && obj->type.decl == em->prog->invocation) { // this.index...
        ex_invocation(em, e);
        return;
    }
    if (obj->kind == E_THIS && obj->type.kind == TY_RECORD && obj->type.decl == em->prog->placed) { // this.clip
        ex_placed(em);
        return;
    }
    // A field of what the engine supplies
    if (obj->kind == E_NAME && obj->bind == BIND_PARAM && (is_bound(em, obj->param) || names_given(em, obj)) && e->field) {
        const param *p = obj->param;
        if (!is_bound(em, p)) { // A struct that holds a list, the function's: its caller's (Structs that hold lists)
            note_given_field(em, obj, e->field, e->local_field);
        } else if (em->checking && e->local_field) {
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
                    sb_printf(&made, "    var tide_array%d: %s = ", at, ty(em, e->type));
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
        sb_printf(em->o, is_pointer(obj->param) ? "(*%s).%s" : "%s.%s", local_name(obj->param->name), field_name(e->field));
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
        note_part(em, e); // Of a list's element, only this field is read
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
    if (have.kind == TY_CHAR && want.kind == TY_FLOAT) { // A char where a float goes: its code point, an int here
        if (e->kind == E_INT) {
            put_float(em, (double)int_literal(e));
            return;
        }
        sb_printf(em->o, "%s(", ty(em, want));
        ex(em, e);
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
    } else if (matrix_dim(lt) && op == T_SLASH) { // WGSL divides no matrix
        ex(em, l);
        sb_put(em->o, " * (1.0 / ");
        ex_as(em, r, (type){TY_FLOAT, NULL});
        sb_put(em->o, ")");
    } else if (op == T_SHL || op == T_SHR) {
        ex(em, l);
        sb_printf(em->o, " %s ", op_text(op));
        if (is_const(r) && !all_const) em->defuse++;
        sb_put(em->o, "u32(");
        ex(em, r);
        sb_put(em->o, ")");
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

// Tide's Math functions that are WGSL's own.
static const struct {
    const char *name;
    const char *wgsl;
} same_math[] = {
    {"Abs", "abs"}, {"Sign", "sign"}, {"Min", "min"}, {"Max", "max"},
    {"Floor", "floor"}, {"Ceil", "ceil"}, {"Round", "round"}, {"Trunc", "trunc"},
    {"Frac", "fract"}, {"Sqrt", "sqrt"}, {"Rsqrt", "inverseSqrt"},
    {"Radians", "radians"}, {"Degrees", "degrees"}, {"Sin", "sin"}, {"Cos", "cos"},
    {"Tan", "tan"}, {"Asin", "asin"}, {"Acos", "acos"}, {"Atan", "atan"},
    {"Exp", "exp"}, {"Exp2", "exp2"}, {"Log", "log"}, {"Log2", "log2"},
    {"Step", "step"}, {"Lerp", "mix"},
    {"Dot", "dot"}, {"Length", "length"}, {"Distance", "distance"},
    {"Reflect", "reflect"}, {"Cross", "cross"},
    {"Transpose", "transpose"}, {"Determinant", "determinant"},
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
    {"quaternion", "FromToRotation", H_FROMTO_Q, "tide_fromto_q"}, {"float4x4", "Scale", H_SCALE, "tide_scaling"},
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
    static const char *const eases[] = {"EaseIn", "EaseOut", "EaseInOut", "EaseInBack", "EaseOutBack"};
    for (int k = 0; k < 5; k++) {
        if (!str_eq_c(e->name, eases[k])) continue;
        need(em, H_EASE);
        sb_put(em->o, "tide_ease(");
        put_arg(em, e, 0);
        sb_printf(em->o, ", %d)", k);
        em->defuse--;
        return;
    }
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
    } else if (str_eq_c(e->name, "AsInt") || str_eq_c(e->name, "AsFloat")) {
        sb_printf(em->o, "bitcast<%s>(", ty(em, e->type));
        put_arg(em, e, 0);
        sb_put(em->o, ")");
    } else if (str_eq_c(e->name, "Hash")) { // xxHash of the same integer or float bits as the CPU
        need(em, H_HASH1 + type_dim(first) - 1);
        sb_printf(em->o, "tide_hash%d(", type_dim(first));
        if (type_is_float_based(first)) sb_printf(em->o, "bitcast<%s>(", ty(em, vector_type(false, type_dim(first))));
        put_arg(em, e, 0);
        if (type_is_float_based(first)) sb_put(em->o, ")");
        sb_put(em->o, ")");
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
    } else if (str_eq_c(e->name, "Inverse") && !quaternion) { // WGSL has none
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
            if (str_eq_c(e->name, same_math[i].name)) fn = same_math[i].wgsl;
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
    if (str_eq_c(owner, "quaternion") && str_eq_c(e->name, "Euler") && e->args.count == 3) {
        need(em, H_EULER_Q);
        sb_printf(em->o, "tide_euler_q(%s(", float_ty(em, 3));
        for (int i = 0; i < 3; i++) {
            if (i) sb_put(em->o, ", ");
            put_arg(em, e, i);
        }
        sb_put(em->o, "))");
        return true;
    }
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
    case CTOR_CHAR: { // char(n): n, or 0 for a number no character has, as tide_char_of gives on the CPU
        const expr *n = e->args.items[0];
        if (n->type.kind == TY_CHAR) {
            ex(em, n);
            return;
        }
        sb_put(em->o, "select(");
        ex(em, n);
        sb_put(em->o, ", 0, (");
        ex(em, n);
        sb_put(em->o, " < 0 || ");
        ex(em, n);
        sb_put(em->o, " > 1114111 || (");
        ex(em, n);
        sb_put(em->o, " >= 55296 && ");
        ex(em, n);
        sb_put(em->o, " < 57344)))");
        return;
    }
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
    return texture->field
           && (texture->type.kind == TY_GRID || texture->type.kind == TY_TEXTURE || texture->type.kind == TY_TEXTURE_ARRAY
               || texture->type.kind == TY_CUBEMAP)
           && field_is_raw(texture->field);
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
    if (of->type.kind == TY_DEPTH_TEXTURE || of->type.kind == TY_DEPTH_TEXTURE_ARRAY) return 0;
    if (of->type.kind == TY_TEXTURE_ARRAY) { // After its uv and its layer
        const int at = str_eq_c(e->name, "SampleGrad") ? 4 : 2;
        if (e->args.count > at && e->args.items[at]->enum_member) return (int)e->args.items[at]->enum_member->number;
        int filter, wrap;
        declared_sampling(of, &filter, &wrap);
        return filter == 0 || filter == 2 ? filter : 1;
    }
    if (e->args.count >= 2) return e->args.items[1]->enum_member ? (int)e->args.items[1]->enum_member->number : 1;
    if (of->type.kind == TY_CUBEMAP) return 1; // A cube that says none is read blended
    if (of->bind != BIND_ASSET && of->type.kind == TY_TEXTURE) return -1;
    int filter, wrap;
    declared_sampling(of, &filter, &wrap);
    return filter == 0 || filter == 2 ? filter : 1;
}

// Whether working `e` out rolls with Random: a roll, or a call of what rolls. With `made`, a local made of one
// counts too: what the expression is made of was rolled, though it rolls nothing itself.
static bool rolls_in(const expr *e, const bool made, const int depth)
{
    if (!e || depth > 64) return false;
    if (e->call == CALL_ROLL || (e->method && e->method->rolls)) return true;
    if (made && e->kind == E_NAME && e->bind == BIND_LOCAL && e->local && e->local->kind == S_VAR
        && rolls_in(e->local->value, made, depth + 1)) {
        return true;
    }
    if (rolls_in(e->object, made, depth + 1) || rolls_in(e->lhs, made, depth + 1) || rolls_in(e->rhs, made, depth + 1)
        || rolls_in(e->cond, made, depth + 1)) {
        return true;
    }
    for (int i = 0; i < e->args.count; i++) {
        if (rolls_in(e->args.items[i], made, depth + 1)) return true;
    }
    for (int i = 0; i < e->inits.count; i++) {
        if (rolls_in(e->inits.items[i].value, made, depth + 1)) return true;
    }
    return false;
}

static bool has_roll(const expr *e, const int depth)
{
    return rolls_in(e, true, depth);
}

// Whether a frag can work an expression out as it starts, before any branch:
// it's made of the frag's parameters, constants, locals that are never
// assigned again and whose own values are so, and what changes nothing
// (arithmetic, Math, Instance.Normal, constructors, functions that take everything by value,
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
            if (e->method->rolls) return false; // Written twice, it would roll twice
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
            if (e->object->type.kind == TY_TEXTURE_ARRAY || e->object->type.kind == TY_DEPTH_TEXTURE_ARRAY) {
                if (known_filter(e) == 2 && !str_eq_c(e->name, "SampleGrad")) return false; // It'd ask how fast its uv changes
                for (int i = 0; i < e->args.count; i++) {
                    if (!e->args.items[i]->enum_member && !liftable(em, e->args.items[i], depth + 1)) return false;
                }
                return true;
            }
            if (str_eq_c(e->name, "SampleGrad")) {
                for (int i = 0; i < 3; i++) {
                    if (!liftable(em, e->args.items[i], depth + 1)) return false;
                }
                return true;
            }
            const int filter = known_filter(e);
            return (str_eq_c(e->name, "Sample") || str_eq_c(e->name, "Compare")) && (filter >= -1 && filter <= 2)
                && liftable(em, e->args.items[0], depth + 1) && (e->args.count < 2 || e->object->type.kind != TY_DEPTH_TEXTURE
                                                                 || liftable(em, e->args.items[1], depth + 1));
        }
        if (e->call == CALL_DERIVATIVE) return liftable(em, e->args.items[0], depth + 1); // Asked as it starts too
        if (e->call == CALL_FUNCTION || e->call == CALL_METHOD) {
            const decl *fn = e->method;
            if (!fn || fn->is_extern || fn->is_mut_method || fn->rolls) return false; // One that rolls would roll twice
            for (int i = 0; i < fn->params.count; i++) { // One with the pixel could leave it undrawn
                const param *p = &fn->params.items[i];
                if (p->mode == PARAM_MUT || p->is_out || p->type.decl == em->prog->pixel) return false;
            }
        } else if (e->call != CALL_CONSTRUCT && e->call != CALL_BUILTIN && e->call != CALL_NORMAL) {
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

// ---------------------------------------------------------------------------
// Textures that are sampled
//
// A texture the code samples is one the frag or vert was given, by a name: a
// field of a component or a singleton it takes, its own Texture, or an asset.
// A function samples its own parameter's, which is whichever of those its
// call names: like a list, it's no argument, and the function is written once
// for each set of textures it's called with (need_function). Its body is
// checked once, though, whichever frag or vert reaches it first, so what it
// samples is kept on it (decl.gpu_samples), by its parameter and by how the
// Sample is written, and each call notes that of the texture it names
// (pass_samples).

// Whether a value of type `t` is a texture to sample: a Texture, a target's
// depth, a cube, or a grid of colors or of floats.
static bool is_sampled(const type t)
{
    return t.kind == TY_TEXTURE || t.kind == TY_DEPTH_TEXTURE || t.kind == TY_CUBEMAP || t.kind == TY_TEXTURE_ARRAY
        || t.kind == TY_DEPTH_TEXTURE_ARRAY || type_is_texture_grid(t);
}

// How a Sample reads its texture: which kind of read it is, and its filter
// and its wrap where they're known as the game is compiled (shader_sample).
typedef struct sampling {
    shader_read read;
    int filter, wrap;
    bool filter_said, wrap_said;
} sampling;

// How a Sample that says `filter` and `wrap`, -1 for one it leaves out, reads
// the texture `of` names.
static sampling sampling_of(const expr *of, const shader_read read, const int filter, const int wrap)
{
    if (arrayed_read(read)) { // As a texture or a depth is, its way its own
        sampling s = sampling_of(of, base_read(read), filter, wrap);
        s.read = read;
        return s;
    }
    if (read == READ_CUBE) return (sampling){read, filter == 0 ? 0 : filter == 2 ? 2 : 1, 0, true, true}; // One that says none is read blended
    // A depth's nearest pixel is read, the pixels at its edge past them, and what its comparisons give, the
    // GPU blends: none of it is the draw's to say
    if (read != READ_COLOR) return (sampling){read, 0, 0, true, true};
    // A Sample that doesn't say its filter or its wrap has its texture's own. A grid field's and an asset's
    // by name are their declaration's ([Filter], [Wrap]), and without one, blended and the cells at the edge:
    // known here. A Texture's that's no asset's (the frag's own Texture parameter, a Texture field) is
    // whichever texture it's given, which says how it's sampled: that's each draw's run's to carry
    int declared_filter, declared_wrap;
    declared_sampling(of, &declared_filter, &declared_wrap);
    const bool carried = !(of->bind == BIND_ASSET && of->constant) && of->type.kind == TY_TEXTURE;
    const int said = filter >= 0 ? filter : declared_filter;
    return (sampling){read, said == 0 ? 0 : said == 2 ? 2 : 1, wrap >= 0 ? wrap : declared_wrap > 0 ? declared_wrap : 0,
                      !carried || filter >= 0, !carried || wrap >= 0};
}

// The texture `of` names in the frag or vert, read as `how` says: noted among
// what the frag or vert samples, while checking, and otherwise which of the
// shader's ways of reading its textures it is (way_texture, way_sampler), or
// for a size, which of its sizes. A texture is one binding however it's read;
// one sampled with another filter or another wrap is read through another
// sampler, and so is a DepthTexture read as a number (Sample) and compared
// (Compare): the GPU reads each through a sampler of another kind.
static int sample_slot(emit *em, const expr *of, const sampling how)
{
    const decl *asset = of->bind == BIND_ASSET ? of->constant : NULL; // A Grid2<Color>, Texture or Cubemap asset, by name
    const bool direct = of->kind == E_NAME && !asset;
    const param *p = asset ? NULL : direct ? of->param : of->object->param;
    if (em->checking) {
        const decl *holds = direct || asset ? NULL : of->local_field ? p->type.decl->local_part : p->type.decl;
        const shader_sample sample = {asset ? -1 : param_index(em->root, p), direct || asset ? -1 : field_index(holds, of->field),
                                      of->local_field && !asset, how.filter, asset, how.wrap, how.read, how.filter_said,
                                      how.wrap_said};
        bool known = false;
        for (int i = 0; i < em->root->samples.count; i++) {
            const shader_sample *s = &em->root->samples.items[i];
            known |= s->param == sample.param && s->field == sample.field && s->local == sample.local && s->filter == sample.filter
                  && s->asset == sample.asset && s->wrap == sample.wrap && s->read == sample.read
                  && s->filter_said == sample.filter_said && s->wrap_said == sample.wrap_said;
        }
        if (!known) vec_push(em->root->samples, sample);
        return 0;
    }
    const shader_texture *textures = how.read == READ_SIZE ? em->sh->sizes.items : em->sh->textures.items;
    const int count = how.read == READ_SIZE ? em->sh->sizes.count : em->sh->textures.count;
    int slot = 0;
    while (slot < count) {
        const shader_texture *t = &textures[slot];
        if (t->asset == asset && t->direct == direct && t->source == (p ? p->type.decl : NULL) && t->field == (asset ? NULL : of->field)
            && one_image(t->read, how.read)) {
            break;
        }
        slot++;
    }
    if (how.read == READ_SIZE) return slot;
    // The way it's read, of that texture
    int way = 0;
    while (way < em->sh->ways.count) {
        const shader_way *w = &em->sh->ways.items[way];
        if (w->texture == slot && w->read == how.read && w->filter == how.filter && w->wrap == how.wrap
            && w->filter_said == how.filter_said && w->wrap_said == how.wrap_said) {
            break;
        }
        way++;
    }
    return way;
}

// A number for the texture `of` names that no other the shader samples has:
// the first of the shader's textures that's a way it's read. -1 for one
// that's read no way, which nothing then tells from another.
static int texture_number(const emit *em, const expr *of)
{
    if (!of || !em->sh) return -1;
    const decl *asset = of->bind == BIND_ASSET ? of->constant : NULL;
    const bool direct = of->kind == E_NAME && !asset;
    const param *p = asset ? NULL : direct ? of->param : of->object->param;
    for (int slot = 0; slot < em->sh->textures.count; slot++) {
        const shader_texture *t = &em->sh->textures.items[slot];
        if (t->read != READ_DATA && t->asset == asset && t->direct == direct && t->source == (p ? p->type.decl : NULL)
            && t->field == (asset ? NULL : of->field)) {
            return slot;
        }
    }
    for (int slot = 0; slot < em->sh->sizes.count; slot++) {
        const shader_texture *t = &em->sh->sizes.items[slot];
        if (t->asset == asset && t->direct == direct && t->source == (p ? p->type.decl : NULL)
            && t->field == (asset ? NULL : of->field)) return em->sh->textures.count + slot;
    }
    return -1;
}

// `e` as a texture the code samples: the expression that names it in the frag
// or vert, which says which of the shader's textures each Sample of it is. An
// asset by its name, anywhere; in the frag or vert, its own Texture or a field
// of a component or singleton it takes; in a function, its own parameter,
// which is what its call named: *own is then which parameter, and the result
// NULL while checking, when no one call says. A grid's local is what it
// names. NULL for anything else: a texture that's made, picked or returned.
static const expr *texture_of(emit *em, const expr *e, int *own)
{
    *own = -1;
    if (!e) return NULL;
    if (e->kind == E_NAME && e->bind == BIND_ASSET && e->constant) return e;
    if (e->kind == E_NAME && e->bind == BIND_LOCAL) {
        for (int i = em->aliases.count - 1; i >= 0; i--) {
            if (em->aliases.items[i].local == e->local) return texture_of(em, em->aliases.items[i].names, own);
        }
        return NULL;
    }
    if (em->code != em->root) {
        if (e->kind != E_NAME || e->bind != BIND_PARAM || !e->param->function_param) return NULL;
        *own = param_index(em->code, e->param);
        return em->checking || !em->spec ? NULL : em->spec[*own].sampled;
    }
    if (e->kind == E_NAME && e->bind == BIND_PARAM && e->param->type.kind == TY_TEXTURE && is_bound(em, e->param)) return e;
    if (e->kind == E_MEMBER && e->field && e->object->kind == E_NAME && e->object->bind == BIND_PARAM && is_bound(em, e->object->param)
        && (e->object->param->type.kind == TY_COMPONENT || e->object->param->type.kind == TY_SINGLETON)) {
        return e;
    }
    return NULL;
}

// A texture a function is passed, or samples, that texture_of finds no name for.
static void refuse_texture(emit *em, const expr *e)
{
    refuse(em, e->at, "a texture a function samples is one the frag or vert was given, by a name: it can't be made, returned, "
                      "picked between or kept in a struct in code the GPU runs");
    if (em->checking) {
        diag_note("pass a field of a component or a singleton the frag or vert takes, its own Texture, or an asset: "
                  "'Lit(lamp.shadow, at)'; a function passes on a parameter of its own");
    }
}

// Notes on a function being checked that it samples its parameter `param`,
// or with -1 the asset `asset` names, as the Sample at `at` says.
static void note_function_sample(decl *fn, const int param, const expr *asset, const shader_read read, const int filter,
                                 const int wrap, const bool gradients, const loc at)
{
    for (int i = 0; i < fn->gpu_samples.count; i++) {
        const function_sample *s = &fn->gpu_samples.items[i];
        if (s->param == param && (param >= 0 || s->asset->constant == asset->constant) && s->read == read && s->filter == filter
            && s->wrap == wrap && s->gradients == gradients) {
            return;
        }
    }
    const function_sample sample = {.param = param, .asset = param >= 0 ? NULL : asset, .read = read, .filter = filter,
                                    .wrap = wrap, .gradients = gradients, .at = at};
    vec_push(fn->gpu_samples, sample);
}

static void ex_texture_size(emit *em, const expr *e)
{
    int own;
    const expr *of = texture_of(em, e->object, &own);
    if (em->code != em->root && (own >= 0 || of)) {
        if (em->checking) note_function_sample(em->code, own, of, READ_SIZE, 0, 0, false, e->at);
        if (em->checking) { sb_printf(em->o, "%s(0)", ty(em, e->type)); return; }
    }
    if (!of) {
        refuse_texture(em, e->object);
        sb_printf(em->o, "%s(0)", ty(em, e->type));
        return;
    }
    const int slot = sample_slot(em, of, sampling_of(of, READ_SIZE, 0, 0));
    if (em->checking) sb_printf(em->o, "%s(0)", ty(em, e->type));
    else if (str_eq_c(e->member, "count")) put_words(em, 'v', em->sh->sizes.items[slot].count_at + 2, 1, ty(em, e->type));
    else put_words(em, 'v', em->sh->sizes.items[slot].count_at, 2, ty(em, e->type));
}

// What a texture is called where it's named, for a message.
static const char *texture_called(const expr *of)
{
    sb name = {0};
    if (of->kind == E_MEMBER && of->object && of->object->kind == E_NAME) {
        sb_printf(&name, "'" STR_FMT "." STR_FMT "'", STR_ARG(of->object->name), STR_ARG(of->member));
    } else if (of->kind == E_NAME) {
        sb_printf(&name, "'" STR_FMT "'", STR_ARG(of->name));
    } else {
        sb_put(&name, "this texture");
    }
    return name.data;
}

// The code calls `fn`, which was checked: it samples what `fn` does. The
// textures the call names for fn's parameters, and the assets fn names
// itself, go into the frag's or vert's own samples; from a function, into
// that function's, by its own parameters, for its callers in turn.
//
// A function's ordinary Sample never reads between a texture's smaller images
// (Filter.Trilinear): a GPU picks between them by how fast the uv changes
// from one pixel to the next, which a frag asks as it starts, of a uv it can
// work out there (ex_sample), and a function's uv is its call's. So one that
// says Trilinear, or leaves its filter to a texture that says it, is an
// error in what a frag reaches, and one that leaves it to a Texture that may
// is a warning, as in a frag whose uv can't be worked out as it starts. A
// vert has no pixels to compare, and reads the texture's own. SampleGrad
// supplies the footprint explicitly and is fragment-only through helpers too.
static void pass_samples(emit *em, decl *fn, const expr *call, const given *spec, const int *own)
{
    for (int i = 0; i < fn->gpu_samples.count; i++) {
        const function_sample s = fn->gpu_samples.items[i];
        const expr *arg = s.param >= 0 && s.param < call->args.count ? call->args.items[s.param] : NULL;
        const expr *of = s.param < 0 ? s.asset : arg ? spec[s.param].sampled : NULL;
        const int mine = arg ? own[s.param] : -1;
        if (em->code != em->root && (mine >= 0 || of)) {
            note_function_sample(em->code, mine, of, s.read, s.filter, s.wrap, s.gradients, s.at);
            continue;
        }
        if (!of) { // A grid with no name a Sample could go by: one that isn't a grid was refused where it's passed
            if (arg && is_data(fn->params.items[s.param].type)) refuse_texture(em, arg);
            continue;
        }
        const sampling how = sampling_of(of, s.read, s.filter, s.wrap);
        sample_slot(em, of, how);
        if (s.gradients && !em->root->is_frag) {
            refuse(em, s.at, "SampleGrad reads a Texture with UV derivatives per destination pixel: use it in a frag or a function a frag calls");
            diag_note_at(call->at, "vert '" STR_FMT "' samples it through '" STR_FMT "', here",
                         STR_ARG(em->root->name), STR_ARG(fn->name));
            continue;
        }
        if (s.gradients || !em->root->is_frag || (how.read != READ_COLOR && how.read != READ_CUBE && how.read != READ_ARRAY)) continue;
        const char *where = how.read == READ_CUBE ? "direction" : "uv";
        const char *what = how.read == READ_CUBE ? "cube" : "texture";
        if (how.filter == 2) {
            em->failed = true;
            if (s.refused) continue;
            fn->gpu_samples.items[i].refused = true;
            if (s.filter == 2) {
                refuse(em, s.at, "a Sample in a function can't be Filter.Trilinear: only a frag's own Sample reads between a "
                                 "%s's smaller images", what);
            } else {
                refuse(em, s.at, "this Sample reads its %s as it says, and %s says Filter.Trilinear, which only a frag's own "
                                 "Sample reads", what, texture_called(of));
            }
            diag_note("the GPU picks between a %s's smaller images by how fast the %s changes from one pixel to the next, which "
                      "a frag asks as it starts, and a function's %s is its call's: sample in the frag itself, or with "
                      "Filter.Bilinear here", what, where, where);
        } else if (how.read == READ_COLOR && !how.filter_said && em->prog->names_trilinear && !s.warned) {
            // The texture may say Trilinear, and this read can't: said, not left to be found in a shimmer
            fn->gpu_samples.items[i].warned = true;
            diag_warning(s.at, "%s may say Filter.Trilinear, which a function's Sample can't read: it reads the texture's own "
                               "pixels", texture_called(of));
            diag_note("write the filter here, 'Sample(uv, Filter.Bilinear)', which is what's read anyway; or sample in the frag "
                      "itself, of a uv made of its parameters, constants and variables declared with 'var' and never assigned "
                      "again");
        } else {
            continue;
        }
        diag_note_at(call->at, "%s '" STR_FMT "' samples it through '" STR_FMT "', here", shader_word(em->root),
                     STR_ARG(em->root->name), STR_ARG(fn->name));
    }
}

// A Cubemap's Sample: an asset's by name, or a field's, read by a direction.
// One sampled with another filter is another binding, as a texture's is, and
// one read between its smaller images has its direction worked out as the
// frag starts, with how fast it changes, as a uv is (ex_sample).
static void ex_cube_sample(emit *em, const expr *e, const expr *of, const sampling how)
{
    const int filter = how.filter;
    const bool lifts = em->root->is_frag && em->code == em->root && !em->lifting && filter == 2;
    const bool grad = lifts && liftable(em, e->args.items[0], 0);
    if (lifts && !grad && em->checking) {
        refuse(em, e->args.items[0]->at, has_roll(e->args.items[0], 0)
               ? "a Sample that's Filter.Trilinear reads where the frag can work out as it starts: this direction is made of a roll"
               : "a Sample that's Filter.Trilinear reads where the frag can work out as it starts: this "
                 "direction is made of a variable that changes, or of one a loop counts");
        if (has_roll(e->args.items[0], 0)) {
            diag_note("the GPU picks between a cube's smaller images by how fast the direction changes from one pixel to the "
                      "next, and a roll is another number at every pixel: sample with Filter.Bilinear here");
        } else {
            diag_note("the GPU picks between a cube's smaller images by how fast the direction changes from one pixel to the "
                      "next, which it's asked before any branch or loop: make it of the frag's parameters, constants and "
                      "variables declared with 'var' and never assigned again, or sample with Filter.Bilinear here");
        }
    }
    // In a function that's being checked, the cube is noted on the function, for each frag and vert that reaches it
    const int slot = em->checking && em->code != em->root ? 0 : sample_slot(em, of, how);
    const type way = {TY_FLOAT3, NULL};
    // The color itself, where the cube has it times its alpha. One that's [Raw] is numbers, as they are
    const bool straight = !declared_raw(of);
    if (straight) {
        need(em, H_STRAIGHT);
        sb_put(em->o, "tide_straight(");
    }
    if (grad) {
        const int at = em->grad_count++;
        sb *const out = em->o;
        const int defuse = em->defuse;
        const expr *statement = em->statement;
        em->o = &em->grads;
        em->defuse = 0;
        em->statement = NULL;
        em->lifting++;
        sb_printf(em->o, "    let tide_uv%d: vec3f = ", at);
        ex_as(em, e->args.items[0], way);
        sb_printf(em->o, ";\n    let tide_dx%d: vec3f = dpdx(tide_uv%d);\n    let tide_dy%d: vec3f = dpdy(tide_uv%d);\n", at, at, at, at);
        em->lifting--;
        em->o = out;
        em->defuse = defuse;
        em->statement = statement;
        sb_printf(em->o, "textureSampleGrad(pixels%d, sampler%d, ", way_texture(em, slot), way_sampler(em, slot));
        ex_as(em, e->args.items[0], way);
        sb_printf(em->o, ", tide_dx%d, tide_dy%d)%s", at, at, straight ? ")" : "");
        return;
    }
    sb_printf(em->o, "textureSampleLevel(pixels%d, sampler%d, ", way_texture(em, slot), way_sampler(em, slot));
    ex_as(em, e->args.items[0], way);
    sb_put(em->o, straight ? ", 0.0))" : ", 0.0)");
}

// An array's layer, after a Sample's uv: its argument `at` (none where it's -1), held to the layers the array has,
// as WGSL leaves one past them to the GPU.
static void put_layer(emit *em, const expr *e, const int at, const int slot)
{
    if (at < 0) return;
    sb_put(em->o, ", clamp(");
    ex_as(em, e->args.items[at], (type){TY_INT, NULL});
    sb_printf(em->o, ", 0, i32(textureNumLayers(pixels%d)) - 1)", way_texture(em, slot));
}

// A Sample, SampleGrad, or a depth's Compare: of a texture parameter, a grid or texture
// field or an asset, where the frag or vert names it, and in a function, of
// its own parameter, which is the texture its call named, or of an asset.
static void ex_sample(emit *em, const expr *e)
{
    const bool gradients = str_eq_c(e->name, "SampleGrad");
    if (gradients && !em->root->is_frag) {
        refuse(em, e->at, "SampleGrad reads a Texture with UV derivatives per destination pixel: use it in a frag or a function a frag calls");
        put_zero(em, e->type);
        return;
    }
    const type held = e->object->type;
    const bool cube = held.kind == TY_CUBEMAP;
    const bool compares = str_eq_c(e->name, "Compare");
    const shader_read read = cube ? READ_CUBE : held.kind == TY_TEXTURE_ARRAY ? READ_ARRAY
                           : held.kind == TY_DEPTH_TEXTURE_ARRAY ? (compares ? READ_COMPARE_ARRAY : READ_DEPTH_ARRAY)
                           : held.kind != TY_DEPTH_TEXTURE ? READ_COLOR : compares ? READ_COMPARE : READ_DEPTH;
    // An array's are read at a layer, the argument after its uv (and SampleGrad's derivatives)
    const bool arrayed = arrayed_read(read);
    const shader_read base = base_read(read);
    const int layer_at = arrayed ? (gradients ? 3 : 1) : -1;
    // What the Sample says of how it reads: -1 for what it leaves to its texture
    const int filter_at = (gradients ? 3 : 1) + (arrayed ? 1 : 0);
    const int said_filter = (cube || base == READ_COLOR) && e->args.count > filter_at ? (int)e->args.items[filter_at]->enum_member->number : -1;
    const int said_wrap = base == READ_COLOR && e->args.count > filter_at + 1 ? (int)e->args.items[filter_at + 1]->enum_member->number : -1;
    const expr *of = e->object;
    if (em->code != em->root) { // A function's: its own parameter's, which each call names, or an asset's
        int own;
        of = texture_of(em, e->object, &own);
        if (em->checking && (own >= 0 || of)) note_function_sample(em->code, own, of, read, said_filter, said_wrap, gradients, e->at);
        if (!of) {
            if (own < 0) refuse_texture(em, e->object);
            if (!em->checking) { // Nothing named it: refused as it was checked
                put_zero(em, e->type);
                return;
            }
            // Which texture it is, each call says: where it's read is checked all the same
            ex_as(em, e->args.items[0], (type){cube ? TY_FLOAT3 : TY_FLOAT2, NULL});
            if (gradients) {
                ex_as(em, e->args.items[1], (type){TY_FLOAT2, NULL});
                ex_as(em, e->args.items[2], (type){TY_FLOAT2, NULL});
            }
            if (arrayed) ex_as(em, e->args.items[layer_at], (type){TY_INT, NULL});
            if (base == READ_COMPARE) ex_as(em, e->args.items[arrayed ? 2 : 1], (type){TY_FLOAT, NULL});
            return;
        }
    }
    sampling how = sampling_of(of, base, said_filter, said_wrap);
    how.read = read; // An array's way is its own, its sampler a texture's (base_read)
    if (cube) {
        ex_cube_sample(em, e, of, how);
        return;
    }
    const int filter = how.filter;
    const bool filter_said = how.filter_said;
    // Between a texture's smaller images (Filter.Trilinear), a GPU reads by how fast the uv changes from one
    // pixel to the next, which it only answers for outside branches and loops, where code here samples. So a
    // frag works the uv out a first time as it starts, asks there, and reads with the answer wherever the
    // Sample is: for one that says Trilinear, and for one that leaves its filter to the Texture it's given in a
    // game that names Trilinear at all. A uv that can't be worked out there (it reads a variable that changes)
    // is an error where the Sample says Trilinear, and reads the texture's own pixels where a Texture might.
    // A vert has no pixels to compare: it reads the texture's own. So does a function, whose uv is its
    // call's (pass_samples).
    const bool lifts = !gradients && base == READ_COLOR && em->root->is_frag && em->code == em->root && !em->lifting
                    && (filter == 2 || (!filter_said && em->prog->names_trilinear));
    const bool grad = lifts && liftable(em, e->args.items[0], 0);
    if (lifts && !grad && filter == 2 && em->checking) {
        refuse(em, e->args.items[0]->at, has_roll(e->args.items[0], 0)
               ? "a Sample that's Filter.Trilinear reads where the frag can work out as it starts: this uv is made of a roll"
               : "a Sample that's Filter.Trilinear reads where the frag can work out as it starts: this "
                 "uv is made of a variable that changes, or of one a loop counts");
        if (has_roll(e->args.items[0], 0)) {
            diag_note("the GPU picks between a texture's smaller images by how fast the uv changes from one pixel to the "
                      "next, and a roll is another number at every pixel: sample with Filter.Bilinear here");
        } else {
            diag_note("the GPU picks between a texture's smaller images by how fast the uv changes from one pixel to the next, "
                      "which it's asked before any branch or loop: make the uv of the frag's parameters, constants and "
                      "variables declared with 'var' and never assigned again, or sample with Filter.Bilinear here");
        }
    } else if (lifts && !grad && em->checking) {
        // The texture may say Trilinear, and this read can't: said, not left to be found in a shimmer
        diag_warning(e->args.items[0]->at, has_roll(e->args.items[0], 0)
                     ? "%s may say Filter.Trilinear, which this Sample can't read: its uv is made of a roll, so it reads the "
                       "texture's own pixels"
                     : "%s may say Filter.Trilinear, which this Sample can't read: its uv is made of a "
                       "variable that changes, or of one a loop counts, so it reads the texture's own pixels",
                     texture_called(of));
        diag_note("make the uv of the frag's parameters, constants and variables declared with 'var' and never assigned "
                  "again, so the GPU can be asked how fast it changes as the frag starts; or write the filter here, "
                  "'Sample(uv, Filter.Bilinear)', which is what's read anyway");
    }
    // In a function that's being checked, an asset is noted on the function, for each frag and vert that reaches it
    const int slot = em->checking && em->code != em->root ? 0 : sample_slot(em, of, how);
    // No pixel's neighbors are asked about, so it can be sampled anywhere, in a branch or a vertex
    if (base == READ_COMPARE) {
        // What it's compared with is between the near plane and the far limit, as what's stored is:
        // past them, GPUs differ in what they make of it
        sb_printf(em->o, "textureSampleCompareLevel(pixels%d, sampler%d, ", way_texture(em, slot), way_sampler(em, slot));
        ex_as(em, e->args.items[0], (type){TY_FLOAT2, NULL});
        put_layer(em, e, layer_at, slot);
        sb_put(em->o, ", clamp(");
        ex_as(em, e->args.items[arrayed ? 2 : 1], (type){TY_FLOAT, NULL});
        sb_put(em->o, ", 0.0, 1.0))");
        return;
    }
    // The color itself, where the texture has it times its alpha. One that's [Raw] is numbers, as they are
    const bool straight = base == READ_COLOR && !declared_raw(of);
    if (straight) {
        need(em, H_STRAIGHT);
        sb_put(em->o, "tide_straight(");
    }
    if (gradients) {
        sb_printf(em->o, "textureSampleGrad(pixels%d, sampler%d, ", way_texture(em, slot), way_sampler(em, slot));
        for (int i = 0; i < 3; i++) {
            if (i) sb_put(em->o, ", ");
            ex_as(em, e->args.items[i], (type){TY_FLOAT2, NULL});
            if (i == 0) put_layer(em, e, layer_at, slot);
        }
        sb_put(em->o, straight ? "))" : ")");
        return;
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
        sb_printf(em->o, "    let tide_uv%d: vec2f = ", at);
        ex_as(em, e->args.items[0], (type){TY_FLOAT2, NULL});
        sb_printf(em->o, ";\n    let tide_dx%d: vec2f = dpdx(tide_uv%d);\n    let tide_dy%d: vec2f = dpdy(tide_uv%d);\n", at, at, at, at);
        em->lifting--;
        em->o = out;
        em->defuse = defuse;
        em->statement = statement;
        sb_printf(em->o, "textureSampleGrad(pixels%d, sampler%d, ", way_texture(em, slot), way_sampler(em, slot));
        ex_as(em, e->args.items[0], (type){TY_FLOAT2, NULL});
        put_layer(em, e, layer_at, slot);
        sb_printf(em->o, ", tide_dx%d, tide_dy%d)%s", at, at, straight ? ")" : "");
        return;
    }
    if (em->lifting && em->root->is_frag && base == READ_COLOR && (filter == 2 || (!filter_said && em->prog->names_trilinear))) {
        // Worked out as the frag starts (ex_derivative), outside any branch: the GPU picks its smaller images itself
        sb_printf(em->o, "textureSample(pixels%d, sampler%d, ", way_texture(em, slot), way_sampler(em, slot));
        ex_as(em, e->args.items[0], (type){TY_FLOAT2, NULL});
        put_layer(em, e, layer_at, slot);
        sb_put(em->o, straight ? "))" : ")");
        return;
    }
    sb_printf(em->o, "textureSampleLevel(pixels%d, sampler%d, ", way_texture(em, slot), way_sampler(em, slot));
    ex_as(em, e->args.items[0], (type){TY_FLOAT2, NULL});
    put_layer(em, e, layer_at, slot);
    // A depth is one number, which WGSL reads at a level that's an int
    sb_put(em->o, straight ? ", 0.0))" : base == READ_COLOR ? ", 0.0)" : ", 0)");
}

// this.Ddx(v), this.Ddy(v) and this.Fwidth(v), in a frag: a GPU says how fast
// a value changes from one pixel to the next only outside branches and loops
// (WGSL refuses the shader otherwise), and a frag's
// code can be anywhere. So, as for a Sample between a texture's smaller
// images, the frag works v out a first time as it starts and asks there
// (`tide_d<n>`, collected in emit.grads), and the call is that answer. A v it
// can't work out there is an error. Written while lifting, it's asked in place.
static void ex_derivative(emit *em, const expr *e)
{
    // A pixel down is the next row as `this.position` counts them: WGSL's
    const char *fn = str_eq_c(e->name, "Ddx") ? "dpdx" : str_eq_c(e->name, "Ddy") ? "dpdy" : "fwidth";
    const char *close = ")";
    const expr *v = e->args.items[0];
    if (em->lifting) {
        sb_printf(em->o, "%s(", fn);
        ex(em, v);
        sb_put(em->o, close);
        return;
    }
    if (!liftable(em, v, 0)) {
        refuse(em, v->at, "'this.%.*s' asks how fast a value changes as the frag starts, and this one can't be worked "
                          "out there: it's made of a variable that changes, or of one a loop counts",
               e->name.len, e->name.ptr);
        diag_note("a GPU says how fast a value changes from pixel to pixel only before any branch or loop: make it of the "
                  "frag's parameters, constants and variables declared with 'var' and never assigned again");
        return;
    }
    const int at = em->grad_count++;
    sb *const out = em->o;
    const int defuse = em->defuse;
    const expr *statement = em->statement;
    em->o = &em->grads;
    em->defuse = 0;
    em->statement = NULL;
    em->lifting++;
    const char *t = ty(em, e->type);
    sb_printf(em->o, "    let tide_d%d: %s = %s(", at, t, fn);
    ex_as(em, v, e->type);
    sb_printf(em->o, "%s;\n", close);
    em->lifting--;
    em->o = out;
    em->defuse = defuse;
    em->statement = statement;
    sb_printf(em->o, "tide_d%d", at);
}

// Whether a function's parameter is a list or a grid, which it's written for and not handed.
static bool takes_data(const param *p)
{
    return is_data(p->type);
}

// ...or that, or a texture it samples, or a GPU list: none is an argument.
static bool is_given(const param *p)
{
    return takes_data(p) || is_sampled(p->type) || holds_data(p->type) || p->type.kind == TY_GPU_LIST;
}

// A struct that holds a list, where a function is given one: it has to be
// one the frag or vert was given, by its name (Structs that hold lists).
static void refuse_given(emit *em, const expr *arg, const type t)
{
    refuse(em, arg->at, "a %s that holds a list goes to code the GPU runs by its name, as the frag or vert was given it",
           type_name(t));
    diag_note("pass the frag's or vert's own parameter, its singleton, its component or the draw's values, or a function's "
              "parameter that stands for one");
}

// A call of a function or method of the game's. A mut argument is the
// caller's variable: WGSL takes a pointer to a whole variable, so what isn't one goes through a copy, which only a call
// that's a statement of its own can do.
static void ex_user_call(emit *em, const expr *e)
{
    decl *fn = e->method;
    // The lists, grids and textures it's called with, which are no arguments: the function is written for them
    given *spec = arena_alloc((size_t)(fn->params.count + 1) * sizeof(given));
    int *own = arena_alloc((size_t)(fn->params.count + 1) * sizeof(int)); // In a function: which of its own parameters each is
    sb suffix = {0};
    for (int i = 0; i < fn->params.count; i++) {
        const param *p = &fn->params.items[i];
        own[i] = -1;
        if (!is_given(p)) continue;
        const expr *arg = i < e->args.count ? e->args.items[i] : NULL;
        int number = -1;
        bool named = arg != NULL;
        if (holds_data(p->type) && arg && !names_given(em, arg)) {
            refuse_given(em, arg, p->type);
            named = false;
        }
        if (p->type.kind == TY_GPU_LIST) { // Written for the storage buffer the call names (gpu_list_slot)
            spec[i].slot = arg ? gpu_list_slot(em, arg) : -1;
            number = spec[i].slot;
        }
        if (takes_data(p)) {
            spec[i].data = data_unknown(p->type);
            if (arg && !data_of(em, arg, &spec[i].data)) {
                type_fits(em, arg->type, arg->at);
                named = false;
            }
            number = spec[i].data.texture;
        }
        if (named && is_sampled(p->type)) {
            spec[i].sampled = texture_of(em, arg, &own[i]);
            // A grid that's only read by its cells needs no name a Sample could go by: one that's sampled says (pass_samples)
            if (!spec[i].sampled && own[i] < 0 && !takes_data(p)) refuse_texture(em, arg);
            if (number < 0) number = texture_number(em, spec[i].sampled);
        }
        sb_printf(&suffix, "%s%d", suffix.data ? "x" : "_4", number < 0 ? 0 : number);
    }
    need_function(em, fn, e->at, spec, suffix.data ? suffix.data : "");
    // What the function reads of the elements of a list it takes, the code that hands it the list reads
    for (int i = 0; em->checking && i < fn->params.count; i++) {
        const bool *read = fn->params.items[i].gpu_leaves;
        if (!takes_data(&fn->params.items[i]) || !read || !spec[i].data.leaves) continue;
        const int parts = shader_leaves(spec[i].data.element);
        for (int k = 0; k < parts; k++) spec[i].data.leaves[k] |= read[k];
    }
    if (em->checking) pass_samples(em, fn, e, spec, own); // ...and the textures it samples, the code samples
    // ...and what it reads of a struct that holds a list, of what the code passes (Structs that hold lists)
    for (int i = 0; em->checking && i < fn->params.count && i < e->args.count; i++) {
        const param *p = &fn->params.items[i];
        if (holds_data(p->type) && names_given(em, e->args.items[i]))
            pass_given_reads(em, e->args.items[i], p->type.decl, p->given_reads, p->given_local_reads);
    }
    const bool method = fn->owner && !fn->is_operator && !fn->is_interpolate;
    const bool given = method && given_self(fn); // Its receiver holds a list: it's handed nothing for it
    if (given) {
        const expr *receiver = e->kind == E_METHOD ? e->object : NULL;
        if (receiver && !names_given(em, receiver)) {
            refuse_given(em, receiver, receiver->type);
        } else if (!receiver && !given_self(em->code)) {
            refuse(em, e->at, "'" STR_FMT "' is a method of '" STR_FMT "', which holds a list: in code the GPU runs it's called on "
                              "one the frag or vert was given, by its name", STR_ARG(fn->name), STR_ARG(fn->owner->name));
        } else if (em->checking) {
            pass_given_reads(em, receiver, fn->owner, fn->self_reads, fn->self_local_reads);
        }
    }
    const bool statement = em->statement == e;
    sb_printf(em->o, "%s%s(", function_name(fn), suffix.data ? suffix.data : "");
    bool first = true;
    if (method && !given) {
        const expr *receiver = e->kind == E_METHOD ? e->object : NULL;
        first = false;
        if (!receiver) { // Another of its own type's methods: on the same value
            if (fn->is_mut_method) sb_put(em->o, "tide_self");
            else sb_put(em->o, em->code->is_mut_method ? "(*tide_self)" : "tide_self");
        } else if (!fn->is_mut_method) {
            ex(em, receiver);
        } else if (is_whole_variable(receiver)) {
            const bool pointer = receiver->bind == BIND_PARAM && is_pointer(receiver->param);
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
        if (p && is_given(p)) continue;
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
        if (is_whole_variable(arg)) {
            const bool pointer = arg->bind == BIND_PARAM && is_pointer(arg->param);
            sb_printf(em->o, "%s%s", pointer ? "" : "&", local_name(arg->name));
        } else {
            sb_printf(em->o, "&%s", copy_name(i));
        }
    }
    sb_put(em->o, ")");
}

// instance.Normal(normal): a normal of the mesh as it is in the world, of
// length 1. A normal isn't turned as a direction is (Math.Rotate): it stays
// square to its surface only through the inverse of the transform,
// transposed, which differs wherever the transform scales the mesh more one
// way than another (tide_normals). The vertex stage works that matrix out
// from the instance's transform: where a vert calls this, and once a vertex
// for a frag that does (shader.frag_normals), which gets its three columns as
// values that don't blend, so a pixel multiplies and normalizes and no more.
// A 2D mesh has no transform, so its normal is only normalized.
static void ex_normal(emit *em, const expr *e)
{
    if (em->checking) em->root->world_normal = true;
    need(em, H_NORMALIZE3);
    sb_put(em->o, "tide_normalize3(");
    if (em->sh && em->sh->dims == 3 && em->stage == STAGE_FRAG) {
        sb_printf(em->o, "%s(tide_normals0, tide_normals1, tide_normals2) * ", ty(em, (type){TY_FLOAT3X3, NULL}));
    } else if (em->sh && em->sh->dims == 3) {
        need(em, H_NORMALS);
        sb_put(em->o, "tide_normals(");
        put_bound(em, NULL, NULL, (type){TY_FLOAT4X4, NULL});
        sb_put(em->o, ") * ");
    }
    sb_put(em->o, "(");
    ex(em, e->args.items[0]);
    sb_put(em->o, "))");
}

static void ex_call(emit *em, const expr *e)
{
    const char *what = NULL;
    switch (e->call) {
    case CALL_ENUM_COUNT:
        sb_printf(em->o, "%lld", (long long)e->int_value);
        return;
    case CALL_SKINNED:
        refuse(em, e->at, "skeleton lookup and animation sampling run on the CPU, before Draw.SkinnedMesh");
        return;
    case CALL_NORMAL:
        ex_normal(em, e);
        return;
    case CALL_DERIVATIVE:
        ex_derivative(em, e);
        return;
    case CALL_CONSTRUCT:
        ex_construct(em, e);
        return;
    case CALL_FUNCTION:
    case CALL_METHOD:
        ex_user_call(em, e);
        return;
    case CALL_ROLL: // Random.NextInt(6), on the type: the stage's own generator steps, and the CPU's formula makes the number
        for (size_t i = 0; i < sizeof roll_calls / sizeof roll_calls[0]; i++) {
            if (!e->c_callee || strcmp(e->c_callee, roll_calls[i].cpu) != 0) continue;
            put_helper_call(em, e, roll_calls[i].helper, roll_calls[i].call);
            return;
        }
        refuse(em, e->at, "Random." STR_FMT " isn't on the GPU yet", STR_ARG(e->name)); // A roll Random gains goes in roll_calls
        diag_note("roll it in the view, and keep the number in a field the GPU reads");
        return;
    case CALL_RANDOM:
        refuse(em, e->at, "the GPU can't step a Random of the game's: its state is 64 bits, and the GPU's ints are 32");
        diag_note("roll on the type, 'Random." STR_FMT "(...)': a frag has a generator of its pixel's own there, and a vert of "
                  "its vertex's", STR_ARG(e->name));
        return;
    case CALL_BUILTIN:
        if (e->kind == E_METHOD && e->object->kind == E_NAME && str_eq_c(e->object->name, "Math")) {
            ex_math(em, e);
            return;
        }
        if (e->kind == E_CALL && e->type.kind == TY_STRUCT && e->type.decl == em->prog->random) { // Random(seed)
            refuse(em, e->at, "the GPU can't make a Random: its state is 64 bits, and the GPU's ints are 32");
            diag_note("roll on the type, 'Random.NextInt(6)': a frag has a generator of its pixel's own there, and a vert of "
                      "its vertex's");
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
        if (str_eq_c(e->name, "Sample") || str_eq_c(e->name, "SampleGrad")
            || (str_eq_c(e->name, "Compare") && (e->object->type.kind == TY_DEPTH_TEXTURE || e->object->type.kind == TY_DEPTH_TEXTURE_ARRAY))) {
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
    case CALL_SNAP: what = "change the world: systems do, on the CPU"; break;
    case CALL_SEND: what = "send events: systems and views do, on the CPU"; break;
    case CALL_DRAW:
    case CALL_GUI: what = "draw: a view does, and names the frag at mesh draw"; break;
    case CALL_MAP:
        ex_map_call(em, e);
        return;
    case CALL_LIST:
    case CALL_WORLD: what = "change lists or use the World: it reads fields of numbers, lists and maps, and samples grids"; break;
    case CALL_TEXT:
        if (e->object->type.kind == TY_COLOR && str_eq_c(e->name, "WithAlpha")) { // The color with that alpha
            sb_put(em->o, "vec4f((");
            ex(em, e->object);
            sb_put(em->o, ").rgb, ");
            ex_as(em, e->args.items[0], (type){TY_FLOAT, NULL}); // An int's a float there, as on the CPU
            sb_put(em->o, ")");
            return;
        }
        what = e->object->type.kind == TY_RECT ? "move a Rect: pass its x, y, width and height as a float4"
                                               : "work with text";
        break;
    default: what = "do this"; break;
    }
    refuse(em, e->at, "the GPU can't %s", what);
}

static void ex(emit *em, const expr *e)
{
    // Operand captures live in the body; derivative reconstruction precedes it.
    if (e->hoisted && !(em->lifting && em->code == em->root)) {
        sb_put(em->o, e->hoisted);
        return;
    }
    if ((e->gpu || e->kind == E_INDEX || e->kind == E_NAME) && ex_gpu(em, e)) return;
    switch (e->kind) {
    case E_SWITCH:
        if (switch_choice(e) >= 0) ex_as(em, e->args.items[switch_choice(e)], e->type);
        else refuse(em, e->at, "this switch expression needs to run inside a statement");
        return;
    case E_INT:
        if (e->type.kind == TY_LONG) type_fits(em, e->type, e->at);
        else put_int(em, int_literal(e));
        return;
    case E_FLOAT: {
        if (e->is_double) { type_fits(em, e->type, e->at); return; }
        if (e->is_degrees) {
            put_float(em, e->float_value);
            return;
        }
        char text[64];
        snprintf(text, sizeof text, STR_FMT, STR_ARG(e->text));
        put_float(em, strtod(text, NULL));
        return;
    }
    case E_HAS: // The checker made it a bool
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
        if (e->op == T_MINUS && matrix_dim(e->type)) { // WGSL negates no matrix
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
        // WGSL's select picks between numbers, vectors and bools: a struct, a matrix or an array is picked by a
        // function of its type. Both sides are worked out either way
        // ...and so is one with a roll in it, whatever its type: select works both sides out in an order of its
        // own, and a function's arguments are worked out left to right, so the rolls a pixel makes there are the
        // same numbers on every backend
        if (is_struct_like(e->type) || matrix_dim(e->type) || e->type.kind == TY_ARRAY || rolls_in(e, false, 0)) {
            sb_printf(em->o, "tide_pick%d(", need_pick(em, e->type));
            ex(em, e->cond);
            sb_put(em->o, ", ");
            ex_as(em, e->lhs, e->type);
            sb_put(em->o, ", ");
            ex_as(em, e->rhs, e->type);
            sb_put(em->o, ")");
            return;
        }
        sb_put(em->o, "select(");
        ex_as(em, e->rhs, e->type);
        sb_put(em->o, ", ");
        ex_as(em, e->lhs, e->type);
        sb_put(em->o, ", ");
        ex(em, e->cond);
        sb_put(em->o, ")");
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
                if (e->inits.items[k].field == f || (t.decl->callable && str_eq(e->inits.items[k].name, f->name)))
                    value = e->inits.items[k].value;
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
        const bool named = data_of(em, e->object, &data);
        if (named && data.map) { // map[key]: its value, or zeros, as an entry past the end reads
            const type key = data.map->fields.items[0].type;
            if (data.map->is_set || (key.kind != TY_INT && key.kind != TY_BYTE && key.kind != TY_ENUM && key.kind != TY_CHAR)) { // Said already
                no_element(em, e);
                return;
            }
            read_element(em, e, data.leaves, 1, shader_leaves(data.map->fields.items[1].type)); // An entry's value, after its key
            need_find(em, &data);
            const int t = data.texture < 0 ? 0 : data.texture;
            sb_printf(em->o, "tide_list%d(tide_find%d(", t, t);
            ex_as(em, e->lhs, key);
            sb_printf(em->o, ")).%s", field_name(&data.map->entry->fields.items[1]));
            return;
        }
        no_element(em, named ? NULL : e);
        if (named) { // items[i], cells[x, y]: an element by its place
            read_element(em, e, data.leaves, 0, shader_leaves(data.element));
            need_data(em, data.texture, data.element, data.grid);
            sb_printf(em->o, "tide_list%d(", data.texture < 0 ? 0 : data.texture);
            ex_as(em, e->lhs, (type){data.grid == 3 ? TY_INT3 : data.grid ? TY_INT2 : TY_INT, NULL});
            sb_put(em->o, ")");
            return;
        }
        if (e->object->type.kind == TY_ARRAY) {
            ex_array_index(em, e);
            return;
        }
        if (is_data(e->object->type)) {
            type_fits(em, e->object->type, e->object->at);
            return;
        }
        refuse(em, e->at, "the GPU indexes arrays, and the lists, maps and grids the frag or vert was given");
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
        if (e->type.kind == TY_RECORD && e->type.decl == em->prog->placed) {
            refuse(em, e->at, "where the engine would put a vert's vertex is read through 'this.clip': 'this' is not a value "
                              "to keep or pass on");
            return;
        }
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
    if (is_data(s->type)) { // A name for one the code was given: nothing is written
        const char *what = data_word(s->type);
        const loc at = s->name_at.line ? s->name_at : s->at;
        data_alias alias = {.local = s, .names = s->is_mut ? NULL : s->value};
        alias.data = data_unknown(s->type);
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
    sb_printf(em->o, "var %s: %s = ", local_name(s->name), ty(em, s->type));
    if (!s->value) {
        put_default(em, s->type);
        return;
    }
    // A local that holds an element of a list, or a part of one, and never anything else: what the code reads of
    // the local is what it reads of the element (read_element)
    int first;
    const expr *root = chain_root(s->value, &first);
    if (!s->is_mut && may_be_element(root)) {
        em->held_root = root;
        em->held_by = s;
    }
    ex_as(em, s->value, s->type);
    em->held_root = NULL;
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
    if (target->kind == E_MEMBER && target->swizzle_len > 1) {
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

// A call that's a statement: its mut arguments that aren't whole variables
// go in through copies and come back out after it, as WGSL hands over only
// whole variables.
static void st_call(emit *em, const stmt *s)
{
    const expr *e = s->value;
    const bool user = (e->call == CALL_FUNCTION || e->call == CALL_METHOD) && e->method;
    const decl *fn = user ? e->method : NULL;
    const bool method = fn && fn->owner && !fn->is_operator && !fn->is_interpolate;
#define COPIED(arg) (!is_whole_variable(arg))
    const expr *receiver = method && fn->is_mut_method && e->kind == E_METHOD && COPIED(e->object) ? e->object : NULL;
    bool copies = receiver != NULL;
    for (int i = 0; fn && i < e->args.count && i < fn->params.count; i++) {
        copies |= fn->params.items[i].mode == PARAM_MUT && COPIED(e->args.items[i]);
    }
    em->statement = e;
    if (!copies) {
        if (!user) sb_put(em->o, "_ = "); // A value WGSL won't let go unused
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
        if (fn->params.items[i].mode != PARAM_MUT || !COPIED(e->args.items[i])) continue;
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
    sb_printf(em->o, "var %s: %s = ", local_name(local->name), ty(em, t));
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
    if (e->kind == E_SWITCH) return; // The selected arm declares its own out locals.
    st_outs(em, e->cond);
    st_outs(em, e->lhs);
    st_outs(em, e->rhs);
    for (int i = 0; i < e->args.count; i++) st_outs(em, e->args.items[i]);
}

// The variable a step changes: the local or parameter its target is, or is a part of.
static const expr *step_variable(const expr *e)
{
    if (e->property && e->kind == E_METHOD) e = e->object;
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

// Switch values need statements, and SampleGrad's three numeric operands
// are worked out in source order, before the statement.
static bool needs_operand_order(const expr *e)
{
    if (!e) return false;
    if (e->kind == E_SWITCH || (e->call == CALL_GRID && str_eq_c(e->name, "SampleGrad"))
        || needs_operand_order(e->object) || needs_operand_order(e->cond)
        || needs_operand_order(e->lhs) || needs_operand_order(e->rhs)) return true;
    for (int i = 0; i < e->args.count; i++) if (needs_operand_order(e->args.items[i])) return true;
    for (int i = 0; i < e->inits.count; i++) if (needs_operand_order(e->inits.items[i].value)) return true;
    return false;
}

static const char *switch_temp(emit *em, const type t)
{
    sb name = {0};
    sb_printf(&name, "tide_choice%d", em->temps++);
    line(em);
    sb_printf(em->o, "var %s: %s", name.data, ty(em, t));
    return name.data;
}

static void keep_switch_operand(emit *em, expr *e, step_list *made)
{
    if (!e || e->hoisted || e->type.kind == TY_VOID || e->type.kind == TY_ERROR || is_data(e->type)
        || e->bind == BIND_TYPE || e->bind == BIND_NAMESPACE || e->type.kind == TY_RECORD || e->type.kind == TY_TEXTURE
        || e->type.kind == TY_DEPTH_TEXTURE || e->type.kind == TY_CUBEMAP || e->type.kind == TY_TEXTURE_ARRAY
        || e->type.kind == TY_DEPTH_TEXTURE_ARRAY) return;
    const char *name = switch_temp(em, e->type);
    sb_put(em->o, " = ");
    ex(em, e);
    sb_put(em->o, ";\n");
    e->hoisted = name;
    vec_push(*made, e);
}

// `++i` and `i++` in a statement's expression `e`, each run before the
// statement, in the order written, into a variable the statement then reads:
// WGSL has neither as a value. So that the value is the CPU's, the statement names the variable
// nowhere else, and the step isn't in a part that only runs sometimes.
// `sometimes`: `e` is one.
static void st_steps(emit *em, const stmt *s, expr *e, const bool sometimes, step_list *made)
{
    if (!e) return;
    if (e->kind == E_SWITCH) {
        if (!type_fits(em, e->type, e->at) || !type_fits(em, e->object->type, e->object->at)) return;
        st_steps(em, s, e->object, sometimes, made);
        keep_switch_operand(em, e->object, made);
        const char *name = switch_temp(em, e->type);
        sb_put(em->o, ";\n");
        line(em);
        sb_put(em->o, "switch (");
        ex(em, e->object);
        sb_put(em->o, ") {\n");
        em->indent++;
        for (int i = 0; i < e->args.count; i++) {
            const expr *label = e->inits.items[i].value;
            line(em);
            if (label) sb_printf(em->o, "case %lld {\n", (long long)label->int_value);
            else sb_put(em->o, "default {\n");
            em->indent++;
            st_outs(em, e->args.items[i]);
            st_steps(em, s, e->args.items[i], false, made);
            line(em);
            sb_printf(em->o, "%s = ", name);
            ex_as(em, e->args.items[i], e->type);
            sb_put(em->o, ";\n");
            em->indent--;
            line(em);
            sb_put(em->o, "}\n");
        }
        em->indent--;
        line(em);
        sb_put(em->o, "}\n");
        e->hoisted = name;
        vec_push(*made, e);
        return;
    }
    const bool shorted = e->kind == E_BINARY && !e->method && (e->op == T_AND || e->op == T_OR);
    const bool ordering = needs_operand_order(s->target) || needs_operand_order(s->cond) || needs_operand_order(s->value);
    if ((e->kind == E_CONDITIONAL || shorted) && ordering) {
        expr *cond = shorted ? e->lhs : e->cond;
        st_steps(em, s, cond, sometimes, made);
        keep_switch_operand(em, cond, made);
        const char *name = switch_temp(em, e->type);
        sb_put(em->o, ";\n");
        line(em);
        sb_put(em->o, "if (");
        ex(em, cond);
        sb_put(em->o, ") {\n");
        for (int side = 0; side < 2; side++) {
            expr *arm = shorted ? ((side == 0) == (e->op == T_AND) ? e->rhs : cond) : side == 0 ? e->lhs : e->rhs;
            em->indent++;
            st_steps(em, s, arm, false, made);
            line(em);
            sb_printf(em->o, "%s = ", name);
            ex_as(em, arm, e->type);
            sb_put(em->o, ";\n");
            em->indent--;
            line(em);
            sb_put(em->o, side == 0 ? "} else {\n" : "}\n");
        }
        e->hoisted = name;
        vec_push(*made, e);
        return;
    }
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
        if (e->step->property_set) {
            sb old = {0}, next = {0};
            sb_printf(&old, "tide_property_was%d", em->temps);
            sb_printf(&next, "tide_property_next%d", em->temps++);
            line(em);
            sb_printf(em->o, "let %s: %s = ", old.data, ty(em, e->type));
            ex(em, e->lhs);
            sb_put(em->o, ";\n");
            expr before = {.kind = E_NAME, .at = e->at, .type = e->type, .hoisted = old.data};
            expr value = {.kind = E_BINARY, .at = e->at, .type = e->type, .op = compound_op(e->op), .lhs = &before, .rhs = e->step->value};
            line(em);
            sb_printf(em->o, "let %s: %s = ", next.data, ty(em, e->type));
            ex(em, &value);
            sb_put(em->o, ";\n");
            expr stepped = {.kind = E_NAME, .at = e->at, .type = e->type, .hoisted = next.data};
            expr *argument = &stepped;
            expr setter = *e->step->property_set;
            setter.args.items = &argument;
            stmt call = {.kind = S_EXPR, .at = e->at, .value = &setter};
            line(em);
            st_call(em, &call);
            e->hoisted = e->postfix ? old.data : next.data;
            vec_push(*made, e);
            return;
        }
        sb name = {0};
        sb_printf(&name, "tide_step%d", em->temps++);
        for (int pass = 0; pass < 2; pass++) {
            line(em);
            if ((pass == 0) == e->postfix) {
                sb_printf(em->o, "var %s: %s = ", name.data, ty(em, e->type));
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
    if (ordering && (e->kind == E_CALL || e->kind == E_METHOD) && !e->arg_mut
        && !(e->method && e->method->is_mut_method)) keep_switch_operand(em, e->object, made);
    st_steps(em, s, e->cond, sometimes, made);
    st_steps(em, s, e->lhs, sometimes || e->kind == E_CONDITIONAL, made);
    if (ordering && (e->kind == E_BINARY || e->kind == E_UNARY || e->kind == E_INDEX)) keep_switch_operand(em, e->lhs, made);
    st_steps(em, s, e->rhs, sometimes || unit, made);
    if (ordering && e->kind == E_BINARY) keep_switch_operand(em, e->rhs, made);
    for (int n = 0; n < e->args.count; n++) {
        const int i = e->arg_order.count ? e->arg_order.items[n] : n;
        expr *arg = e->args.items[i];
        st_steps(em, s, arg, sometimes, made);
        const bool writable = arg->is_out || (e->method && i < e->method->params.count && e->method->params.items[i].mode == PARAM_MUT)
                           || (e->arg_mut & (1u << (i < 32 ? i : 31)));
        const bool sampling_metadata = e->call == CALL_GRID && str_eq_c(e->name, "SampleGrad") && i >= 3;
        if (ordering && !writable && !sampling_metadata) keep_switch_operand(em, arg, made);
    }
    for (int i = 0; i < e->inits.count; i++) {
        st_steps(em, s, e->inits.items[i].value, sometimes, made);
        if (ordering) keep_switch_operand(em, e->inits.items[i].value, made);
    }
}

static void st_one(emit *em, const stmt *s, bool outs_ready);

// A for that isn't one that counts (st_for): whatever it starts with, goes on
// while and steps by. WGSL's for takes less on its line: a loop with its step
// where a `continue` still reaches it.
static void st_loop(emit *em, const stmt *s)
{
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
}

// A foreach over a list the engine supplies: a loop that counts its elements,
// each read as the loop comes to it.
static void st_foreach(emit *em, const stmt *s, const data_ref *list)
{
    const int k = em->temps++;
    if (!list) { // An array: each of its elements, by its place
        const type array = s->value->type, element = array.decl->fields.items[0].type;
        if (!type_fits(em, array, s->value->at)) return;
        const int reader = need_array(em, array.decl);
        sb_printf(em->o, "for (var tide_k%d: i32 = 0; tide_k%d < %d; tide_k%d++) {\n", k, k, array.decl->array_length, k);
        em->indent++;
        line(em);
        sb_printf(em->o, "var %s: %s = tide_el%d(", local_name(s->name), ty(em, element), reader);
        ex(em, s->value);
        sb_printf(em->o, ", tide_k%d);\n", k);
    } else {
        const int texture = list->texture;
        const type element = list->element;
        // What the loop's code reads of its variable is what it reads of each element
        const element_alias each = {s, list->leaves, 0, shader_leaves(element)};
        vec_push(em->elements, each);
        need_data(em, texture, element, 0);
        sb_printf(em->o, "for (var tide_k%d: i32 = 0; tide_k%d < ", k, k);
        put_list_count(em, texture);
        sb_printf(em->o, "; tide_k%d++) {\n", k);
        em->indent++;
        line(em);
        sb_printf(em->o, "var %s: %s = tide_list%d(tide_k%d);\n", local_name(s->name), ty(em, element), texture < 0 ? 0 : texture, k);
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

// A switch on an int, a char or an enum, as WGSL has its own: a section's
// labels are constants, and no section goes on into the next.
static void st_switch(emit *em, const stmt *s)
{
    if (s->cond->type.kind != TY_INT && s->cond->type.kind != TY_ENUM && s->cond->type.kind != TY_CHAR) {
        refuse(em, s->cond->at, "a switch the GPU runs is on an int, a char or an enum, and this is %s", type_name(s->cond->type));
        diag_note("write it as 'if' and 'else if'");
        return;
    }
    sb_put(em->o, "switch ");
    ex(em, s->cond);
    sb_put(em->o, " {\n");
    em->indent++;
    bool has_default = false;
    for (int i = 0; i < s->cases.count; i++) {
        const switch_case *section = &s->cases.items[i];
        line(em);
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
        em->indent++;
        bool ended = false;
        for (int k = 0; k < section->body.count && !ended; k++) {
            st(em, section->body.items[k]);
            ended = ends(section->body.items[k]);
        }
        em->indent--;
        line(em);
        sb_put(em->o, "}\n");
    }
    if (!has_default) { // WGSL wants one
        line(em);
        sb_put(em->o, "default {\n");
        line(em);
        sb_put(em->o, "}\n");
    }
    em->indent--;
    line(em);
    sb_put(em->o, "}\n");
}

static void st(emit *em, const stmt *s)
{
    if (st_gpu(em, s)) return;
    step_list made = {0};
    bool outs_ready = false;
    if (s->kind == S_FOR || s->kind == S_WHILE) { // Its line runs again each round, where a statement before it runs once
        const stmt *v = s->init;
        if (has_steps(s->cond) || (v && (has_steps(v->value) || has_steps(v->target)))
            || (s->step && (has_steps(s->step->value) || has_steps(s->step->target)))) {
            refuse(em, s->at, "a loop's own line has no '++' or '--' inside an expression in code the GPU runs");
            diag_note("a for's step can be 'i++'; change any other variable in the loop's body");
            return;
        }
        if (needs_operand_order(s->cond) || (v && needs_operand_order(v->value))
            || (s->step && (needs_operand_order(s->step->value) || needs_operand_order(s->step->target)))) {
            if (s->kind == S_FOR && (has_outs(s->cond) || (v && has_outs(v->value))
                || (s->step && (has_outs(s->step->value) || has_outs(s->step->target))))) {
                refuse(em, s->at, "a for the GPU runs declares nothing with 'out' on its own line");
                diag_note("make the call before the loop, or in its body");
                return;
            }
            // Put the step at the next iteration's start so continue still runs it.
            line(em);
            sb_put(em->o, "{\n");
            em->indent++;
            if (s->init) st(em, s->init);
            const char *first = NULL;
            if (s->step) {
                first = switch_temp(em, (type){TY_BOOL, NULL});
                sb_put(em->o, " = true;\n");
            }
            line(em);
            sb_put(em->o, "loop {\n");
            em->indent++;
            if (s->step) {
                line(em);
                sb_printf(em->o, "if (!%s) {\n", first);
                em->indent++;
                st(em, s->step);
                em->indent--;
                line(em);
                sb_put(em->o, "}\n");
                line(em);
                sb_printf(em->o, "%s = false;\n", first);
            }
            if (s->cond) {
                st_outs(em, s->cond);
                st_steps(em, s, s->cond, false, &made);
                line(em);
                sb_put(em->o, "if (!(");
                ex(em, s->cond);
                sb_put(em->o, ")) { break; }\n");
                for (int i = 0; i < made.count; i++) made.items[i]->hoisted = NULL;
            }
            st(em, s->then_stmt);
            em->indent--;
            line(em);
            sb_put(em->o, "}\n");
            em->indent--;
            line(em);
            sb_put(em->o, "}\n");
            return;
        }
    } else if (s->kind != S_BLOCK && (has_steps(s->target) || has_steps(s->value) || has_steps(s->cond)
               || needs_operand_order(s->target) || needs_operand_order(s->value) || needs_operand_order(s->cond))) {
        st_outs(em, s->target);
        st_outs(em, s->cond);
        st_outs(em, s->value);
        outs_ready = true;
        st_steps(em, s, s->target, false, &made);
        st_steps(em, s, s->cond, false, &made);
        st_steps(em, s, s->value, false, &made);
    }
    st_one(em, s, outs_ready);
    for (int i = 0; i < made.count; i++) made.items[i]->hoisted = NULL; // The CPU's code makes its own
}

static void st_one(emit *em, const stmt *s, bool outs_ready)
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
    } else if (!outs_ready) {
        st_outs(em, s->cond);
        st_outs(em, s->value);
        st_outs(em, s->target);
    }
    if (s->kind == S_VAR && is_data(s->type)) { // A name: no line of its own
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
        if (writes_shared(s)) { // Under a condition always true that fxc can't see through (writes_shared)
            sb_put(em->o, "if (tide_zero == 0u) {\n");
            em->indent++;
            line(em);
            st_assign(em, s, true);
            em->indent--;
            line(em);
            sb_put(em->o, "}\n");
            em->guards_shared = true;
            break;
        }
        st_assign(em, s, true);
        break;
    case S_EXPR:
        if (s->value->kind == E_METHOD && s->value->call == CALL_COVER) { // How much of the pixel it covers, finished as it ends
            sb_put(em->o, "tide_cover = ");
            ex_as(em, s->value->args.items[0], (type){TY_FLOAT, NULL});
            sb_put(em->o, ";\n");
        } else if (s->value->kind == E_METHOD && s->value->call == CALL_DISCARD) {
            if (em->code != em->root) refuse(em, s->at, "a pixel is left undrawn in the frag itself, not in a function it calls");
            sb_put(em->o, "discard;\n");
            line(em); // WGSL goes on running a discarded pixel's code
            sb_put(em->o, "return;\n");
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
        if (s->value && (s->value->type.kind == TY_LIST || s->value->type.kind == TY_MAP || s->value->type.kind == TY_SET)) {
            if (data_of(em, s->value, &data)) st_foreach(em, s, &data);
            else type_fits(em, s->value->type, s->value->at);
            break;
        }
        if (s->value && s->value->type.kind == TY_ARRAY) {
            st_foreach(em, s, NULL);
            break;
        }
        refuse(em, s->at, "a foreach the GPU runs goes through an array, a list, a map or a set, like 'foreach (var light in "
                          "lights.items)': it has no queries or grids to go through");
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

#include "shader_compute.inc"

// ---------------------------------------------------------------------------
// Functions

// A function or method the code calls: written before its caller, once.
// While checking, its body is checked once, whichever frag or vert reaches it
// first, and a function that reaches itself is refused.
static void need_function(emit *em, decl *fn, const loc at, const given *spec, const char *suffix)
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
    const int indent = em->indent, defuse = em->defuse, aliases = em->aliases.count, elements = em->elements.count;
    const expr *statement = em->statement;
    const bool failed = em->failed;
    const given *const outer_spec = em->spec;
    const expr *const part_root = em->part_root, *const held_root = em->held_root; // What its caller is in the middle of
    em->part_root = em->held_root = NULL;
    em->spec = spec;
    em->o = &body;
    em->code = fn;
    em->indent = 1;
    em->defuse = 0;
    em->statement = NULL;
    em->failed = false;
    fn->gpu_state = em->checking ? 1 : fn->gpu_state;

    const bool method = fn->owner && !fn->is_operator && !fn->is_interpolate;
    const bool shader = fn->kind == DECL_SHADER;
    sb head = {0};
    em->o = &head;
    const bool value = fn->return_type.kind != TY_VOID && fn->return_type.kind != TY_ERROR;
    if (value && !type_fits(em, fn->return_type, fn->at)) em->failed = true;
    sb_printf(&head, "fn %s%s(", function_name(fn), suffix);
    bool first = true;
    if (method && given_self(fn)) { // Its receiver holds a list: it's handed nothing for it (Structs that hold lists)
        if (fn->is_mut_method) {
            refuse(em, fn->at, "'" STR_FMT "' changes the '" STR_FMT "' it's called on, and code the GPU runs only reads one that "
                               "holds a list", STR_ARG(fn->name), STR_ARG(fn->owner->name));
        }
    } else if (method) {
        const type self = {fn->owner->kind == DECL_COMPONENT ? TY_COMPONENT : TY_STRUCT, fn->owner};
        if (type_fits(em, self, fn->at)) {
            sb_printf(&head, fn->is_mut_method ? "tide_self: ptr<function, %s>" : "tide_self: %s", ty(em, self));
        }
        first = false;
    }
    for (int i = 0; i < fn->params.count; i++) {
        const param *p = &fn->params.items[i];
        if (shader && !p->function_param && !handed_to(em->sh, p)) continue; // The engine supplies it
        if (spec && is_given(p)) { // Which of the shader's lists, grids or textures it is, this is written for
            if (p->mode == PARAM_MUT && p->type.kind == TY_GPU_LIST) {
                refuse(em, p->at, "a GPU list is a handle, written without 'mut': '" STR_FMT "' can't be '%s'", STR_ARG(p->name),
                       p->is_out ? "out" : "mut");
            } else if (p->mode == PARAM_MUT) {
                refuse(em, p->at, "a %s a function takes in code the GPU runs is read there, never changed: '" STR_FMT "' can't be "
                                  "'%s'", takes_data(p) ? data_word(p->type) : type_name(p->type), STR_ARG(p->name),
                       p->is_out ? "out" : "mut");
            }
            continue;
        }
        if (!type_fits(em, p->type, p->type_at.line ? p->type_at : p->at)) continue;
        if (!first) sb_put(&head, ", ");
        first = false;
        const bool mut = p->mode == PARAM_MUT;
        sb_printf(&head, mut ? "%s: ptr<function, %s>" : "%s: %s", local_name(p->name), ty(em, p->type));
    }
    if (value) sb_printf(&head, ") -> %s {\n", ty(em, fn->return_type));
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
    em->elements.count = elements;
    em->part_root = part_root;
    em->held_root = held_root;
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
    for (int i = 0; i < em->structs.count; i++) {
        const decl *d = em->structs.items[i];
        sb_printf(o, "struct %s {\n", struct_name(d));
        for (int k = 0; k < d->fields.count; k++) {
            const field *f = &d->fields.items[k];
            if (f->hidden) continue;
            sb_printf(o, "    %s: %s,\n", field_name(f), ty(em, f->type));
        }
        sb_put(o, "}\n");
    }
}

// The uniform block's members, the same in both stages: the engine's own,
// then the game's float4s and int4s.
static void put_uniforms(const shader *sh, sb *o)
{
    sb_put(o, "struct tide_uniforms {\n    tide_camera: mat4x4f,\n    tide_screen: vec4f,\n    tide_tint: vec4f,\n");
    if (sh->dims == 3) sb_put(o, "    tide_origin: vec4f,\n");
    for (int i = 0; i < packs(sh->uniform_floats); i++) sb_printf(o, "    tide_u%d: vec4f,\n", i);
    for (int i = 0; i < packs(sh->uniform_ints); i++) sb_printf(o, "    tide_v%d: vec4i,\n", i);
    sb_put(o, "}\n@group(0) @binding(0) var<uniform> tide_block: tide_uniforms;\n");
    if (sh->dims == 3) {
        // Rebase the model before applying local geometry, rather than rounding a large
        // world position and subtracting afterward. Its homogeneous row stays unchanged.
        sb_put(o, "fn tide_project(model: mat4x4f, vertex: vec4f) -> vec4f {\n");
        sb_put(o, "    let relative = mat4x4f(\n");
        for (int i = 0; i < 4; i++)
            sb_printf(o, "        model[%d] - tide_block.tide_origin * model[%d].w%s\n", i, i, i == 3 ? ");" : ",");
        sb_put(o, "    return tide_block.tide_camera * (relative * vertex);\n}\n");
    }
    put_textures(o, sh);
}

static const field *field_called(const decl *d, const char *name)
{
    for (int i = 0; i < d->fields.count; i++) {
        if (str_eq_c(d->fields.items[i].name, name)) return &d->fields.items[i];
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// Rolls
//
// A frag or a vert that rolls with Random (the checker's decl.rolls: itself,
// or through a function it calls) has a generator of its own each time it
// runs, `tide_dice`, a variable of its stage's that tide_roll steps. The stage
// makes it as it starts, of what tells one run from another: for a frag, its
// pixel's place in the destination's own pixels and which instance of its
// draw the pixel is of (which the vertex stage passes on, `tide_nth`); for a
// vert, which vertex of the mesh and which instance; and for both, a number
// the frame rolled (a word of the draw's ints, which codegen fills: the same
// for every draw of a frame, so draws still join into runs) and which frag or
// vert it is, so two that draw the same pixel roll apart. Math.Hash of those
// is the state: whole numbers, the same on every backend.

static bool stage_rolls(const shader *sh, const int stage)
{
    const decl *root = stage == STAGE_VERT ? sh->vert : sh->frag;
    return root && root->rolls;
}

// A number for the frag or vert, as roll_site's is for a system (codegen.c): of its name, and an int's worth.
static unsigned roll_site(const decl *root)
{
    unsigned hash = 2166136261u;
    for (const char *s = function_name(root); *s; s++) hash = (hash ^ (unsigned char)*s) * 16777619u;
    return hash & 0x7FFFFFFFu;
}

// The stage's generator, made as its main starts. `nth` is how the fragment stage names its instance.
static void put_dice(emit *em, sb *o, const int stage, const char *nth)
{
    const shader *sh = em->sh;
    if (!stage_rolls(sh, stage)) return;
    sb *const out = em->o;
    em->o = o;
    need(em, stage == STAGE_VERT ? H_HASH3 : H_HASH4);
    if (stage == STAGE_VERT) sb_put(o, "    tide_dice = u32(tide_hash3(vec3i(i32(tide_vertex_index), i32(tide_instance_index), ");
    else sb_printf(o, "    tide_dice = u32(tide_hash4(vec4i(vec2i(tide_at.xy), %s, ", nth);
    put_word(em, 'v', sh->rolled_at);
    sb_printf(o, " ^ %u)));\n", roll_site(stage == STAGE_VERT ? sh->vert : sh->frag));
    em->o = out;
}

// What the vertex stage passes on to the pixels, as members of the stage's
// struct: the vertex's fields the pixels read, the fields they read of what
// the vert wrote for them, where they are in the world, and the instance's
// float4s and int4s, which don't blend.
static void put_varyings(emit *em, sb *o)
{
    const shader *sh = em->sh;
    int location = 0;
    for (int i = 0; i < sh->vertex->fields.count; i++) {
        if (!sh->frag_fields[i]) continue;
        const char *t = ty(em, as_float(sh->vertex->fields.items[i].type));
        sb_printf(o, "    @location(%d) tide_s%d: %s,\n", location++, i, t);
    }
    for (int h = 0; h < sh->handed.count; h++) {
        const shader_handed *handed = &sh->handed.items[h];
        for (int i = 0; i < handed->type->fields.count; i++) {
            if (!handed->fields[i]) continue;
            const char *t = ty(em, as_float(handed->type->fields.items[i].type));
            sb_printf(o, "    @location(%d) tide_h%d_%d: %s,\n", location++, h, i, t);
        }
    }
    if (sh->frag_world) sb_printf(o, "    @location(%d) tide_world: vec3f,\n", location++);
    for (int i = 0; sh->frag_normals && i < 3; i++) // The columns of what normals go through (ex_normal), an instance's own
        sb_printf(o, "    @location(%d) @interpolate(flat) tide_normals%d: vec3f,\n", location++, i);
    for (int i = 0; i < packs(sh->frag_floats); i++) sb_printf(o, "    @location(%d) @interpolate(flat) tide_f%d: vec4f,\n", location++, i);
    for (int i = 0; i < packs(sh->frag_ints); i++) sb_printf(o, "    @location(%d) @interpolate(flat) tide_g%d: vec4i,\n", location++, i);
    if (stage_rolls(sh, STAGE_FRAG)) // Which instance of its draw a pixel is of, for its generator (put_dice)
        sb_printf(o, "    @location(%d) @interpolate(flat) tide_nth: i32,\n", location++);
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

// Skin data is two ordinary READ_DATA textures: matrices, then one vertex
// range per pixel followed by pairs of palette indices and float weight bits.
// The user vertex schema has no joints or weights added to it.
static void put_skin_helpers(emit *em)
{
    const shader *sh = em->sh;
    if (!sh->skinned) return;
    int palette = -1, influences = -1;
    for (int i = 0; i < sh->textures.count; i++) {
        if (sh->textures.items[i].skin == 1) palette = i;
        if (sh->textures.items[i].skin == 2) influences = i;
    }
    if (palette < 0 || influences < 0) return;
    sb *const old = em->o;
    em->o = &em->functions;
    sb *o = em->o;
    static const char identity[] = "mat4x4f(vec4f(1,0,0,0),vec4f(0,1,0,0),vec4f(0,0,1,0),vec4f(0,0,0,1))";
    for (int which = 0; which < 2; which++) {
        const int slot = which ? influences : palette;
        sb_printf(o, "fn tide_skin_words%d(p: i32) -> vec4i {\n    return textureLoad(pixels%d, vec2i(p & 1023, p >> 10), 0);\n}\n",
                  which, slot);
    }
    sb_put(o, "fn tide_skin_matrix(i: i32) -> mat4x4f {\n");
    sb_put(o, "    if (i < 0 || i >= ");
    put_word(em, 'v', sh->textures.items[palette].count_at);
    sb_printf(o, ") { return %s; }\n", identity);
    sb_put(o, "    let p = ");
    if (sh->skin_base_at >= 0) {
        sb_put(o, "(");
        put_word(em, 'j', sh->skin_base_at);
        sb_put(o, " + i)");
    } else sb_put(o, "i");
    sb_put(o, " * 4;\n    return mat4x4f(");
    for (int column = 0; column < 4; column++) sb_printf(o, "%sbitcast<vec4f>(tide_skin_words0(p + %d))", column ? ", " : "", column);
    sb_put(o, ");\n}\n");
    sb_put(o, "fn tide_skinning(vertex: i32) -> mat4x4f {\n");
    sb_put(o, "    if (vertex < 0 || vertex >= ");
    put_word(em, 'v', sh->textures.items[influences].count_at);
    sb_printf(o, ") { return %s; }\n", identity);
    sb_put(o, "    let range = tide_skin_words1(vertex).xy;\n    let entries = ");
    put_word(em, 'v', sh->textures.items[influences].count_at + 1);
    sb_put(o, ";\n    if (range.x < 0 || range.y < 0 || range.x > entries || range.y > entries - range.x) ");
    sb_printf(o, "{ return %s; }\n", identity);
    for (int c = 0; c < 4; c++) sb_printf(o, "    var c%d = vec4f(0.0);\n", c);
    sb_put(o, "    var total = 0.0;\n    for (var n = 0; n < range.y; n++) {\n        let i = range.x + n;\n        let pair = tide_skin_words1(");
    put_word(em, 'v', sh->textures.items[influences].count_at);
    sb_put(o, " + (i >> 1));\n");
    sb_put(o, "        let joint = select(pair.x, pair.z, (i & 1) != 0);\n"
              "        let weight = bitcast<f32>(select(pair.y, pair.w, (i & 1) != 0));\n"
              "        let matrix = tide_skin_matrix(joint);\n");
    for (int c = 0; c < 4; c++) sb_printf(o, "        c%d = c%d + matrix[%d] * weight;\n", c, c, c);
    sb_put(o, "        total = total + weight;\n    }\n");
    sb_printf(o, "    if (!(total > 0.0)) { return %s; }\n    return mat4x4f(c0 / total, c1 / total, c2 / total, c3 / total);\n}\n",
              identity);
    em->o = old;
}

static void put_skin_vertex(emit *em, sb *o)
{
    if (!em->sh->skinned) return;
    put_skin_helpers(em);
    const decl *vertex = em->sh->vertex;
    sb_put(o, "    let tide_skin = tide_skinning(i32(tide_vertex_index));\n");
    sb_printf(o, "    tide_vertex.%s = (tide_skin * vec4f(tide_vertex.%s, 1.0)).xyz;\n",
              field_name(field_called(vertex, "position")), field_name(field_called(vertex, "position")));
    const field *normal = field_called(vertex, "normal"), *tangent = field_called(vertex, "tangent");
    if (normal && normal->type.kind == TY_FLOAT3) {
        need(em, H_NORMALS);
        need(em, H_NORMALIZE3);
        sb_printf(o, "    tide_vertex.%s = tide_normalize3(tide_normals(tide_skin) * tide_vertex.%s);\n", field_name(normal), field_name(normal));
    }
    if (tangent && (tangent->type.kind == TY_FLOAT3 || tangent->type.kind == TY_FLOAT4)) {
        need(em, H_NORMALIZE3);
        const char *name = field_name(tangent), *xyz = tangent->type.kind == TY_FLOAT4 ? ".xyz" : "";
        sb_printf(o, "    var tide_tangent = (tide_skin * vec4f(tide_vertex.%s%s, 0.0)).xyz;\n", name, xyz);
        if (normal && normal->type.kind == TY_FLOAT3) {
            sb_printf(o, "    tide_tangent = tide_tangent - tide_vertex.%s * dot(tide_vertex.%s, tide_tangent);\n", field_name(normal), field_name(normal));
        }
        if (tangent->type.kind == TY_FLOAT4) {
            sb_put(o, "    let tide_side = select(1.0, -1.0, dot(tide_skin[0].xyz, cross(tide_skin[1].xyz, tide_skin[2].xyz)) < 0.0);\n");
            sb_printf(o, "    tide_vertex.%s = vec4f(tide_normalize3(tide_tangent), tide_vertex.%s.w * tide_side);\n", name, name);
        } else sb_printf(o, "    tide_vertex.%s = tide_normalize3(tide_tangent);\n", name);
    }
}

// The vertex stage's own code: the vertex from its attributes, through the
// vert, placed by its instance's transform and the camera, and what goes on
// to the pixels, into the stage's struct, `out`.
static void put_vertex_main(emit *em, sb *o)
{
    const shader *sh = em->sh;
    const decl *v = sh->vertex;
    const type vertex = {TY_STRUCT, (decl *)v};
    const char *f4 = float_ty(em, 4);
    const char *out = "out.";
    em->stage = STAGE_VERT;
    em->o = o;
    // A GPU list's indices: this corner's index is its place among them (zeros past their count), as an index buffer's
    if (sh->gpu_indices) {
        need_storage(em, sh->index_storage);
        sb_printf(o, "    let tide_vertex_index = bitcast<u32>(tide_get%d(i32(tide_corner_at)));\n", sh->index_storage);
    }
    put_dice(em, o, STAGE_VERT, NULL);
    if (sh->gpu_vertices) { // A GPU list's vertex, by its index: zeros past their count
        need_storage(em, sh->vertex_storage);
        sb_printf(o, "    var tide_vertex: %s = tide_get%d(i32(tide_vertex_index));\n", ty(em, vertex), sh->vertex_storage);
    } else {
        sb_printf(o, "    var tide_vertex: %s = %s(", ty(em, vertex), ty(em, vertex));
        for (int i = 0; i < v->fields.count; i++) sb_printf(o, "%stide_a%d", i ? ", " : "", i);
        sb_put(o, ");\n");
    }
    put_skin_vertex(em, o);
    // What the vert writes for the frag: a variable each, in the order it takes them
    for (int h = 0; h < sh->handed.count; h++) {
        const type handed = {TY_STRUCT, (decl *)sh->handed.items[h].type};
        sb_printf(o, "    var tide_handed%d: %s = ", h, ty(em, handed));
        put_zero(em, handed);
        sb_put(o, ";\n");
    }
    // Where a vert that places its vertex itself puts it, its `out float4`: the engine's own placing is then
    // only for what the pixels read of where they are in the world
    const bool placed = sh->vert && sh->vert->place;
    if (placed) {
        sb_printf(o, "    var tide_moved: %s = ", f4);
        put_zero(em, (type){TY_FLOAT4, NULL});
        sb_put(o, ";\n");
    }
    if (sh->vert) { // Called with what it writes after its vertex, in the order it takes them
        sb_printf(o, "    %s(&tide_vertex", function_name(sh->vert));
        int h = 0;
        for (int i = 1; i < sh->vert->params.count; i++) {
            const param *p = &sh->vert->params.items[i];
            if (p->place) sb_put(o, ", &tide_moved");
            else if (p->is_out && p->type.kind == TY_STRUCT) sb_printf(o, ", &tide_handed%d", h++);
        }
        sb_put(o, ");\n");
    }
    const char *position = field_name(field_called(v, "position"));
    if ((placed || sh->dims == 3) && !sh->frag_world) {
        // Nothing reads where the engine would have put it
    } else if (sh->dims == 3) {
        sb_put(o, "    let tide_place = ");
        put_bound(em, NULL, NULL, (type){TY_FLOAT4X4, NULL});
        sb_printf(o, " * %s(tide_vertex.%s, 1.0);\n", f4, position);
    } else {
        sb_printf(o, "    let tide_place = %s(tide_vertex.%s, 0.0, 1.0);\n", f4, position);
    }
    if (!placed && sh->dims == 3) {
        sb_put(o, "    let tide_projected = tide_project(");
        put_bound(em, NULL, NULL, (type){TY_FLOAT4X4, NULL});
        sb_printf(o, ", %s(tide_vertex.%s, 1.0));\n", f4, position);
    }
    // The camera's clip space is OpenGL's: depth from -1
    sb_printf(o, "    let tide_clip = %s;\n    var out: tide_corner;\n"
                 "    out.tide_at = vec4f(tide_clip.xy, (tide_clip.z + tide_clip.w) * 0.5, tide_clip.w);\n",
              placed ? "tide_moved" : sh->dims == 3 ? "tide_projected" : "tide_block.tide_camera * tide_place");
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
    if (sh->frag_normals) { // What the frag's instance.Normal multiplies by, worked out here, once a vertex
        need(em, H_NORMALS);
        sb_put(o, "    let tide_turned = tide_normals(");
        put_bound(em, NULL, NULL, (type){TY_FLOAT4X4, NULL});
        sb_put(o, ");\n");
        for (int i = 0; i < 3; i++) sb_printf(o, "    %stide_normals%d = tide_turned[%d];\n", out, i, i);
    }
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
    if (stage_rolls(sh, STAGE_FRAG)) sb_printf(o, "    %stide_nth = i32(tide_instance_index);\n", out);
}

// How many colors a shader's fragment stage writes: its frag's `out Color`
// parameters, or the one a mesh drawn with no frag has.
static int shader_outputs(const shader *sh)
{
    return sh->frag && sh->frag->outputs > 1 ? sh->frag->outputs : 1;
}

// The variable the fragment stage has its output `k` in: the pixel's color
// first, which has the name there always was.
static const char *output_name(const int k)
{
    static char names[4][16];
    if (k == 0) return "tide_color";
    snprintf(names[k & 3], sizeof names[0], "tide_color%d", k);
    return names[k & 3];
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
static void put_fragment_main(emit *em, sb *o)
{
    const shader *sh = em->sh;
    const char *in = "in.";
    const decl *v = sh->vertex;
    const char *f4 = float_ty(em, 4);
    const int outputs = shader_outputs(sh);
    em->stage = STAGE_FRAG;
    em->o = o;
    sb nth = {0};
    sb_printf(&nth, "%stide_nth", in);
    put_dice(em, o, STAGE_FRAG, nth.data);
    for (int k = 0; k < outputs; k++) {
        sb_printf(o, "    var %s: vec4f = %s(1.0, 1.0, 1.0, 1.0);\n", output_name(k), f4);
    }
    if (sh->frag && sh->frag->cutout) sb_put(o, "    tide_cover = 1.0;\n"); // Whole, where it doesn't say
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
                sb_printf(o, ", &%s", output_name(p->output));
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
        int sampler = 0; // ...and the sampler it's read through
        while (sampler < sh->samplers.count && sh->samplers.items[sampler].texture != texture) sampler++;
        if (sh->textured) need(em, H_STRAIGHT); // The color itself, where the texture has it times its alpha
        // The draw's own texture is read as it says. In a game that names Filter.Trilinear that can be between
        // its smaller images, which the GPU picks by itself here, before any branch
        if (sh->textured && em->prog->names_trilinear) {
            sb_printf(o, "    tide_color = tide_color * tide_straight(textureSample(pixels%d, sampler%d, %stide_s%d));\n",
                      texture, sampler, in, field_index(v, uv));
        } else if (sh->textured) {
            sb_printf(o, "    tide_color = tide_color * tide_straight(textureSampleLevel(pixels%d, sampler%d, %stide_s%d, 0.0));\n",
                      texture, sampler, in, field_index(v, uv));
        }
    }
    for (int k = 0; k < outputs; k++) {
        const char *color = output_name(k);
        if (k == 0 && sh->frag && sh->frag->cutout) {
            // A cut-out says how much of the pixel it covers (this.Cutout, this.Coverage), into `tide_cover`, times
            // its tint's alpha. Cutout's is an alpha, sharpened here to a pixel by how fast it changes. The GPU
            // makes it the share of the pixel's samples drawn into (alpha to coverage) where the target has them;
            // a target of one sample (`tide_screen.w` below 0) has it cut at 0.5. What it covers it covers wholly,
            // so its color's own alpha isn't read, its color isn't times one, and it doesn't go to white for a
            // draw that multiplies
            const bool sharpen = !sh->frag->raw_coverage;
            sb_printf(o, "    %s = %s * tide_block.tide_tint;\n"
                         "    var tide_share: f32 = tide_cover * tide_block.tide_tint.a;\n", color, color);
            if (sharpen) sb_put(o, "    tide_share = (tide_share - 0.5) / max(fwidth(tide_share), 0.0001) + 0.5;\n");
            sb_printf(o, "    if (tide_block.tide_screen.w < 0.0) {\n"
                         "        if (tide_share < 0.5) {\n"
                         "            discard;\n"
                         "        }\n"
                         "        tide_share = 1.0;\n"
                         "    }\n"
                         "    %s = vec4f(%s.rgb, clamp(tide_share, 0.0, 1.0));\n", color, color);
            continue;
        }
        sb_printf(o, "    %s = %s * tide_block.tide_tint;\n"
                     "    %s = vec4f(%s.rgb * %s.a, %s.a);\n"
                     "    if (tide_block.tide_screen.w > 0.0) {\n"
                     "        %s = vec4f(%s.rgb + (1.0 - %s.a), %s.a);\n"
                     "    }\n", color, color, color, color, color, color, color, color, color, color);
    }
    if (outputs == 1) {
        sb_put(o, "    return tide_color;\n");
    } else { // Its struct of them, in order
        sb_put(o, "    return tide_colors(");
        for (int k = 0; k < outputs; k++) sb_printf(o, "%s%s", k ? ", " : "", output_name(k));
        sb_put(o, ");\n");
    }
}

// The private variables WGSL's functions read their stage's inputs through,
// which the stage sets as it starts.
static void put_wgsl_inputs(const shader *sh, sb *o)
{
    for (int i = 0; i < packs(sh->instance_floats); i++) sb_printf(o, "var<private> tide_n%d: vec4f;\n", i);
    for (int i = 0; i < packs(sh->instance_ints); i++) sb_printf(o, "var<private> tide_j%d: vec4i;\n", i);
    for (int i = 0; i < packs(sh->frag_floats); i++) sb_printf(o, "var<private> tide_f%d: vec4f;\n", i);
    for (int i = 0; i < packs(sh->frag_ints); i++) sb_printf(o, "var<private> tide_g%d: vec4i;\n", i);
    sb_put(o, "var<private> tide_at: vec4f;\nvar<private> tide_world: vec3f;\n");
    for (int i = 0; sh->frag_normals && i < 3; i++) sb_printf(o, "var<private> tide_normals%d: vec3f;\n", i);
}

// The generator a stage's code rolls with (put_dice), where it has one: a
// variable of the stage's own, each run of it with its own.
static void put_dice_variable(const emit *em, sb *o, const bool rolls)
{
    if (!rolls && !em->helpers[H_ROLL]) return;
    sb_put(o, "var<private> tide_dice: u32;\n");
}

static emit emitter(program *prog, const shader *sh)
{
    return (emit){.prog = prog, .sh = sh};
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

// Whether a shader's draws can be listed (shader.listed): an instanced 3D draw, not skinned, whose every value
// for each instance is its list item's, or the transform (the item's, or the identity where it has none), with a
// texture left for the list's numbers.
static bool listable(const shader *sh)
{
    if (!sh->instanced || sh->skinned || sh->blit || sh->dims != 3 || sh->textures.count >= SHADER_TEXTURES
        || packs(sh->instance_floats) < 4 || sh->storage.count) return false;
    for (int i = 0; i < sh->values.count; i++) {
        const shader_value *v = &sh->values.items[i];
        if (v->per_instance && v->source && v->source != sh->instance_type) return false;
    }
    return true;
}

// The statements that start a listed vertex stage (shader.listed): each of the instance's packs, a pixel of its
// list's numbers from where its place says, as the attributes of an ordinary one would have them. A list's rows
// are TIDE_LIST_DATA_ACROSS (1024) pixels.
static void put_listed_loads(const shader *sh, sb *o)
{
    const int floats = packs(sh->instance_floats), all = floats + packs(sh->instance_ints);
    const int texture = sh->textures.count;
    sb_printf(o, "    let tide_listed_at = tide_listed * %d;\n", all);
    for (int k = 0; k < all; k++) {
        char at[96];
        snprintf(at, sizeof at, "(tide_listed_at + %d) & 1023, (tide_listed_at + %d) >> 10u", k, k);
        const bool f = k < floats;
        const int n = f ? k : k - floats;
        sb_printf(o, "    tide_%c%d = %stextureLoad(pixels%d, vec2i(%s), 0)%s;\n", f ? 'n' : 'j', n,
                  f ? "bitcast<vec4f>(" : "", texture, at, f ? ")" : "");
    }
}

// The words of field `f` of struct `d`'s value, where they start: its floats among the value's floats, its ints
// among the value's words (after all of its floats).
static void field_words_at(const decl *d, const field *f, int *floats_at, int *ints_at)
{
    int floats = 0, ints = 0, all_floats = 0, all_ints = 0;
    for (int i = 0; i < d->fields.count; i++) {
        const field *other = &d->fields.items[i];
        if (other->hidden) continue;
        if (other == f) floats = all_floats, ints = all_ints;
        type_words(other->type, &all_floats, &all_ints);
    }
    *floats_at = floats;
    *ints_at = all_floats + ints;
}

// An indirect draw's instance, from its list's storage (shader.gpu_instances): the instance's float4s and int4s
// as its attributes would have them, read from the element `instance_index` of the list.
static void put_instance_loads(emit *em, sb *o)
{
    const shader *sh = em->sh;
    const int slot = sh->instance_storage;
    need_storage(em, slot);
    const type element = slot_element(em, slot);
    // Its element, held inside the list's range: the GPU draws as many as its count, never more than its capacity
    sb_printf(o, "    let tide_e = 4u + min(tide_instance_index, u32(max(");
    sb *const out = em->o;
    em->o = o;
    put_word(em, 'v', sh->storage_at + 2 * slot + 1);
    em->o = out;
    sb_printf(o, ", 1) - 1)) * %du;\n", element_words(element, NULL));
    for (int v = 0; v < sh->values.count; v++) {
        const shader_value *value = &sh->values.items[v];
        if (!value->per_instance) continue;
        int floats_at = 0, ints_at = 0;
        if (!sh->instance_type) { // A list of transforms: each element is one
            floats_at = 0;
        } else if (value->source) {
            field_words_at(sh->instance_type, value->field, &floats_at, &ints_at);
        } else if (sh->instance_transform) {
            field_words_at(sh->instance_type, sh->instance_transform, &floats_at, &ints_at);
        } else { // No transform: the identity
            for (int k = 0; k < 16; k++) {
                const int w = value->float_at + k;
                sb_printf(o, "    tide_n%d.%c = %s;\n", w / 4, "xyzw"[w % 4], k % 5 == 0 ? "1.0" : "0.0");
            }
            continue;
        }
        for (int k = 0; k < value->floats; k++) {
            const int w = value->float_at + k;
            sb_printf(o, "    tide_n%d.%c = bitcast<f32>(tide_word%d(tide_e + %du));\n", w / 4, "xyzw"[w % 4], slot, floats_at + k);
        }
        for (int k = 0; k < value->ints; k++) {
            const int w = value->int_at + k;
            sb_printf(o, "    tide_j%d.%c = bitcast<i32>(tide_word%d(tide_e + %du));\n", w / 4, "xyzw"[w % 4], slot, ints_at + k);
        }
    }
    // Moved by Draw.Transform as the draw was recorded: its matrix times each one's transform, which starts the packs
    for (int v = 0; sh->through_at >= 0 && v < sh->values.count; v++) {
        const shader_value *value = &sh->values.items[v];
        if (!value->per_instance || value->source || value->float_at % 4) continue;
        const int n = value->float_at / 4, u = sh->through_at / 4;
        if (sh->sides_at >= 0) { // A run of those whose own transform mirrors (2) or doesn't (1): the others' corners
            // are all one point, so their triangles cover nothing. As draw.c's mirrors(), term by term
            const int k = sh->sides_at / 4;
            sb_printf(o, "    let tide_keep = tide_block.tide_u%d.x;\n", k);
            sb_printf(o, "    let tide_own = tide_n%d.x * (tide_n%d.y * tide_n%d.z - tide_n%d.z * tide_n%d.y) + tide_n%d.y * "
                         "(tide_n%d.z * tide_n%d.x - tide_n%d.x * tide_n%d.z) + tide_n%d.z * (tide_n%d.x * tide_n%d.y - tide_n%d.y * "
                         "tide_n%d.x);\n", n, n + 1, n + 2, n + 1, n + 2, n, n + 1, n + 2, n + 1, n + 2, n, n + 1, n + 2, n + 1, n + 2);
            sb_put(o, "    if (tide_keep != 0.0 && (tide_own < 0.0) != (tide_keep > 1.5)) {\n        return tide_corner();\n    }\n");
        }
        sb_printf(o, "    let tide_moved = mat4x4f(tide_block.tide_u%d, tide_block.tide_u%d, tide_block.tide_u%d, tide_block.tide_u%d)"
                     " * mat4x4f(tide_n%d, tide_n%d, tide_n%d, tide_n%d);\n", u, u + 1, u + 2, u + 3, n, n + 1, n + 2, n + 3);
        for (int c = 0; c < 4; c++) sb_printf(o, "    tide_n%d = tide_moved[%d];\n", n + c, c);
    }
}

static void write_wgsl(program *prog, shader *sh)
{
    emit em = emitter(prog, sh);
    sb vertex = {0}, fragment = {0};
    write_stage(&em, sh->vert, STAGE_VERT);
    put_vertex_main(&em, &vertex);
    write_stage(&em, sh->frag, STAGE_FRAG);
    put_fragment_main(&em, &fragment);

    const bool rolls = stage_rolls(sh, STAGE_VERT) || stage_rolls(sh, STAGE_FRAG);
    sb o = {0}, rest = {0};
    put_uniforms(sh, &o);
    put_pools(sh, &o); // The pools of the GPU lists its code reads, and an indirect draw's instances
    sb loads = {0};
    if (sh->gpu_instances) put_instance_loads(&em, &loads);
    put_wgsl_inputs(sh, &rest);
    put_dice_variable(&em, &rest, rolls);
    if (sh->frag && sh->frag->cutout) sb_put(&rest, "var<private> tide_cover: f32;\n"); // What it covers
    sb_put(&rest, "struct tide_corner {\n    @builtin(position) tide_at: vec4f,\n");
    put_varyings(&em, &rest);
    sb_put(&rest, "}\n");
    put_structs(&em, &rest);
    put_helpers(&em, &rest);
    if (em.functions.data) sb_put(&rest, em.functions.data);

    // The vertex stage takes its mesh's attributes, then its instance's: a listed one's are its place in its list,
    // whose numbers are its packs (put_listed_loads)
    sb corners = {0}, after = {0}, listed = {0};
    int location = 0;
    for (int i = 0; !sh->gpu_vertices && i < sh->vertex->fields.count; i++) {
        sb_printf(&corners, "%s@location(%d) tide_a%d: %s", location ? ", " : "", location, i, attribute_type(&em, sh->vertex->fields.items[i].type));
        location++;
    }
    const int listed_at = location;
    for (int i = 0; !sh->gpu_instances && i < packs(sh->instance_floats); i++, location++)
        sb_printf(&after, "%s@location(%d) tide_in%d: vec4f", location ? ", " : "", location, i);
    for (int i = 0; !sh->gpu_instances && i < packs(sh->instance_ints); i++, location++)
        sb_printf(&after, "%s@location(%d) tide_ij%d: vec4i", location ? ", " : "", location, i);
    // Which vertex of the mesh, and which instance of the draw: what tells a vert's runs, and an instance's pixels, apart.
    // A vertex a GPU list's is read by its index, which a GPU list of indices gives for each corner (put_vertex_main)
    sb builtins = {0};
    if (rolls || sh->skinned || sh->gpu_instances || sh->gpu_vertices) {
        sb_printf(&builtins, "%s@builtin(vertex_index) %s: u32, @builtin(instance_index) tide_instance_index: u32",
                  location ? ", " : "", sh->gpu_indices ? "tide_corner_at" : "tide_vertex_index");
    }
    sb_put(&builtins, ") -> tide_corner {\n");
    if (sh->listed) {
        sb_printf(&listed, "%s@group(0) @binding(%d) var pixels%d: texture_2d<i32>;\n%s@vertex fn vertex(%s, @location(%d) tide_listed: i32%s",
                  o.data, 1 + 2 * sh->textures.count, sh->textures.count, rest.data, corners.data, listed_at, builtins.data);
        put_listed_loads(sh, &listed);
        sb_printf(&listed, "%s    return out;\n}\n", vertex.data ? vertex.data : "");
    }
    sb_printf(&o, "%s@vertex fn vertex(%s%s%s", rest.data, corners.data ? corners.data : "", after.data ? after.data : "", builtins.data);
    if (sh->gpu_instances) sb_put(&o, loads.data ? loads.data : "");
    for (int i = 0; !sh->gpu_instances && i < packs(sh->instance_floats); i++) sb_printf(&o, "    tide_n%d = tide_in%d;\n", i, i);
    for (int i = 0; !sh->gpu_instances && i < packs(sh->instance_ints); i++) sb_printf(&o, "    tide_j%d = tide_ij%d;\n", i, i);
    sb_printf(&o, "%s    return out;\n}\n", vertex.data);

    // The fragment stage, the same for the listed vertex stage and the other
    sb entry = {0};
    const int outputs = shader_outputs(sh);
    if (outputs == 1) {
        sb_put(&entry, "@fragment fn fragment(in: tide_corner) -> @location(0) vec4f {\n    tide_at = in.tide_at;\n");
    } else { // Several colors, each to the color image of a target that's its place
        sb_put(&entry, "struct tide_colors {\n");
        for (int k = 0; k < outputs; k++) sb_printf(&entry, "    @location(%d) %s: vec4f,\n", k, output_name(k));
        sb_put(&entry, "}\n@fragment fn fragment(in: tide_corner) -> tide_colors {\n    tide_at = in.tide_at;\n");
    }
    if (sh->frag_world) sb_put(&entry, "    tide_world = in.tide_world;\n");
    for (int i = 0; sh->frag_normals && i < 3; i++) sb_printf(&entry, "    tide_normals%d = in.tide_normals%d;\n", i, i);
    for (int i = 0; i < packs(sh->frag_floats); i++) sb_printf(&entry, "    tide_f%d = in.tide_f%d;\n", i, i);
    for (int i = 0; i < packs(sh->frag_ints); i++) sb_printf(&entry, "    tide_g%d = in.tide_g%d;\n", i, i);
    sb_printf(&entry, "%s}\n", fragment.data);
    sb_put(&o, entry.data);
    sh->wgsl = o.data;
    if (sh->listed) {
        sb_put(&listed, entry.data);
        sh->wgsl_listed = listed.data;
    }
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
    emit em = emitter(prog, NULL);
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

// A GPU list the code names, as one of the shader's slots: a range of the pool of ints for a GPUList<int>.
static void add_storage(shader *sh, const decl *source, const field *f)
{
    for (int i = 0; i < sh->storage.count; i++) {
        if (sh->storage.items[i].source == source && sh->storage.items[i].field == f) return;
    }
    const shader_storage s = {.source = source, .field = f, .ints = f->type.decl->fields.items[0].type.kind == TY_INT};
    vec_push(sh->storage, s);
}

// A list the code reads, as one of the shader's textures.
static void add_list(shader *sh, const decl *source, const field *f, const bool bound)
{
    for (int i = 0; i < sh->textures.count; i++) {
        if (sh->textures.items[i].read == READ_DATA && sh->textures.items[i].source == source && sh->textures.items[i].field == f) return;
    }
    const shader_texture t = {.source = source, .field = f, .read = READ_DATA};
    vec_push(sh->textures, t);
    if (bound) add_bound(sh, (decl *)source);
}

// What a frag or vert reads of the components and singletons it takes, and
// the grids it samples, into the shader.
static void plan_reads(const program *prog, shader *sh, const decl *code)
{
    if (!code) return;
    if (code->is_frag && code->pixel_world) sh->frag_world = true;
    if (code->is_frag && code->world_normal && sh->dims == 3) sh->frag_normals = true;
    for (int i = code->is_compute ? 0 : 1; i < code->params.count; i++) { // A kernel has no vertex first
        const param *p = &code->params.items[i];
        if (p->type.decl == prog->instance && p->reads && p->reads[0] && sh->dims == 3) {
            shader_value *v = add_value(sh, NULL, NULL, (type){TY_FLOAT4X4, NULL}, true);
            if (code->is_frag) v->frag_reads = true;
        }
        if (p->type.decl == prog->pixel && p->reads && p->reads[2]) sh->frag_world = true;
        const bool explicit_instance = p->type.kind == TY_STRUCT && p->type.decl == sh->instance_type;
        // The struct the draw gives after its vert and frag: once for the draw, as a singleton's fields
        const bool given = p->type.kind == TY_STRUCT && !p->is_out && sh->values_type && p->type.decl == sh->values_type;
        const bool binding = p->type.kind == TY_STRUCT && p->type.decl->binding_of;
        if (p->type.kind != TY_COMPONENT && p->type.kind != TY_SINGLETON && !explicit_instance && !given && !binding) continue;
        for (int k = 0; k < p->type.decl->fields.count; k++) {
            const field *f = &p->type.decl->fields.items[k];
            if (f->hidden || !(p->whole || (p->reads && p->reads[k]))) continue;
            if (f->type.kind == TY_GPU_LIST) { // A storage buffer of the shader's
                add_storage(sh, p->type.decl, f);
                if (!explicit_instance && !given) add_bound(sh, p->type.decl);
                continue;
            }
            if (f->type.kind == TY_LIST || f->type.kind == TY_MAP || f->type.kind == TY_SET) { // Its elements, as a texture of numbers
                if (explicit_instance) sh->instance_list = f;
                else add_list(sh, p->type.decl, f, !given);
                continue;
            }
            if (f->type.kind == TY_GRID || f->type.kind == TY_TEXTURE || f->type.kind == TY_DEPTH_TEXTURE
                || f->type.kind == TY_CUBEMAP || f->type.kind == TY_TEXTURE_ARRAY || f->type.kind == TY_DEPTH_TEXTURE_ARRAY)
                continue; // Sampled, below
            shader_value *v = add_value(sh, p->type.decl, f, f->type, p->type.kind == TY_COMPONENT || explicit_instance);
            if (code->is_frag) v->frag_reads = true;
            else v->vert_reads = true;
            if (!explicit_instance && !given) add_bound(sh, p->type.decl);
        }
        // Its local fields, which this machine keeps beside the match's
        const decl *part = p->type.decl->local_part;
        for (int k = 0; part && p->local_reads && k < part->fields.count; k++) {
            const field *f = &part->fields.items[k];
            if (p->local_reads[k] && f->type.kind == TY_GPU_LIST) {
                add_storage(sh, p->type.decl, f);
                add_bound(sh, p->type.decl);
                continue;
            }
            if (p->local_reads[k] && (f->type.kind == TY_LIST || f->type.kind == TY_MAP || f->type.kind == TY_SET)) {
                add_list(sh, p->type.decl, f, true);
                continue;
            }
            if (!p->local_reads[k] || f->type.kind == TY_GRID || f->type.kind == TY_TEXTURE || f->type.kind == TY_DEPTH_TEXTURE
                || f->type.kind == TY_CUBEMAP || f->type.kind == TY_TEXTURE_ARRAY || f->type.kind == TY_DEPTH_TEXTURE_ARRAY) continue;
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
                                  .direct = s->field < 0 && !s->asset, .asset = s->asset,
                                  .read = s->read == READ_COMPARE_ARRAY ? READ_DEPTH_ARRAY : s->read}; // An array's depth's
        // The texture, once however it's read (a size is apart: metadata, never a binding)...
        const shader_texture *textures = s->read == READ_SIZE ? sh->sizes.items : sh->textures.items;
        const int count = s->read == READ_SIZE ? sh->sizes.count : sh->textures.count;
        int k = 0;
        while (k < count && !(textures[k].direct == t.direct && textures[k].source == t.source && textures[k].field == t.field
                              && textures[k].asset == t.asset && one_image(textures[k].read, t.read))) {
            k++;
        }
        if (k == count) {
            if (s->read == READ_SIZE) vec_push(sh->sizes, t);
            else vec_push(sh->textures, t);
        }
        if (source) add_bound(sh, (decl *)source);
        if (s->read == READ_SIZE) continue;
        // ...and each way it's read
        const shader_way way = {k, s->read, s->filter, s->wrap, s->filter_said, s->wrap_said, 0};
        bool known = false;
        for (int w = 0; w < sh->ways.count; w++) {
            const shader_way *u = &sh->ways.items[w];
            known |= u->texture == k && u->read == way.read && u->filter == way.filter && u->wrap == way.wrap
                  && u->filter_said == way.filter_said && u->wrap_said == way.wrap_said;
        }
        if (!known) vec_push(sh->ways, way);
    }
}

bool shader_reads_local(const shader *sh, const decl *d)
{
    for (int i = 0; i < sh->sizes.count; i++) {
        if (sh->sizes.items[i].source == d && sh->sizes.items[i].field && sh->sizes.items[i].field->is_local) return true;
    }
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
            && s->skinned == call->skinned
            && s->values_type == call->values_type && s->gpu_instances == call->gpu_instances
            && s->gpu_vertices == call->gpu_vertices && s->gpu_indices == call->gpu_indices
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
    sh->skinned = call->skinned;
    sh->skin_base_at = -1;
    sh->instance_type = call->instance_type;
    sh->values_type = call->values_type;
    sh->gpu_instances = call->gpu_instances;
    sh->gpu_vertices = call->gpu_vertices;
    sh->gpu_indices = call->gpu_indices;
    sh->instance_storage = sh->vertex_storage = sh->index_storage = sh->through_at = sh->sides_at = sh->storage_at = -1;
    // The lists the draw gives are its first storage buffers: its instances', its vertices', then its indices'
    if (sh->gpu_instances) {
        const shader_storage instances = {.role = STORAGE_INSTANCES};
        sh->instance_storage = sh->storage.count;
        vec_push(sh->storage, instances);
    }
    if (sh->gpu_vertices) {
        const shader_storage vertices = {.role = STORAGE_VERTICES};
        sh->vertex_storage = sh->storage.count;
        vec_push(sh->storage, vertices);
    }
    if (sh->gpu_indices) {
        const shader_storage indices = {.role = STORAGE_INDICES, .ints = true};
        sh->index_storage = sh->storage.count;
        vec_push(sh->storage, indices);
    }
    const field *position = field_called(vertex, "position");
    sh->dims = position->type.kind == TY_FLOAT3 ? 3 : 2;
    if (sh->instance_type && sh->dims == 3) sh->instance_transform = field_called(sh->instance_type, "transform");
    if (sh->instance_type && sh->skinned) sh->instance_pose = field_called(sh->instance_type, "pose");
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
    if (sh->storage.count > SHADER_STORAGE_MOST) {
        diag_error(call->at, "this draw reads %d GPU lists, and a draw's shaders name %d at the most", sh->storage.count,
                   SHADER_STORAGE_MOST);
        diag_note("keep what several lists hold in one list of a struct, or draw in two draws");
        return NULL;
    }
    if (textured) {
        const shader_texture t = {.read = READ_COLOR};
        vec_push(sh->textures, t);
        const shader_way way = {.texture = sh->textures.count - 1, .read = READ_COLOR, .filter = 1}; // As the texture says
        vec_push(sh->ways, way);
    }
    if (sh->skinned) {
        const shader_texture palette = {.read = READ_DATA, .skin = 1};
        const shader_texture influences = {.read = READ_DATA, .skin = 2};
        vec_push(sh->textures, palette);
        vec_push(sh->textures, influences);
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
    // Draw.Transform's matrix, for instances the GPU reads (Draw.Instanced over a GPU list in 3D): the draw's,
    // whole float4s of its floats, which the engine writes as the draw is recorded (tide_shader.instance_through)
    if (sh->gpu_instances && sh->dims == 3) {
        sh->uniform_floats = packs(sh->uniform_floats) * 4;
        sh->through_at = sh->uniform_floats;
        sh->uniform_floats += 16;
        // Instances that have transforms of their own: which of them a run keeps, those that mirror the mesh or those
        // that don't, as the CPU splits a list's into runs wound each way (tide_draw_shaded_indirect)
        if (!sh->instance_type || sh->instance_transform) {
            sh->sides_at = sh->uniform_floats;
            sh->uniform_floats += 4;
        }
    }
    // How many elements each list it reads has: a word of the draw's ints
    for (int i = 0; i < sh->textures.count; i++) {
        shader_texture *t = &sh->textures.items[i];
        if (t->read != READ_DATA) continue;
        // A list's count, a map's entries and its index's slots, or a grid's size and where its cells are in
        // its texture (TIDE_GRID2_DATA_INTS, TIDE_GRID3_DATA_INTS)
        t->count_at = sh->uniform_ints;
        if (t->skin) { sh->uniform_ints += t->skin == 1 ? 1 : 2; continue; }
        const type held = t->asset ? t->asset->return_type : t->field->type;
        sh->uniform_ints += held.kind == TY_LIST ? 1 : held.kind != TY_GRID ? 2 : held.decl->dims == 3 ? 10 : 6;
    }
    for (int i = 0; i < sh->sizes.count; i++) {
        sh->sizes.items[i].count_at = sh->uniform_ints;
        sh->uniform_ints += shader_size_words(&sh->sizes.items[i]);
    }
    // The number the frame rolled, which the generators of code that rolls are made of, and the match's seed
    // for code that reads it: a word each of the draw's ints, the same for every draw of a frame
    sh->rolled_at = sh->seed_at = -1;
    if (stage_rolls(sh, STAGE_VERT) || stage_rolls(sh, STAGE_FRAG)) sh->rolled_at = sh->uniform_ints++;
    if ((sh->vert && sh->vert->reads_seed) || (sh->frag && sh->frag->reads_seed)) sh->seed_at = sh->uniform_ints++;
    // Where each GPU list it reads is in its pool and its capacity, two words of the draw's ints a list, which the
    // renderer writes (tide_shader.storage_table)
    if (sh->storage.count) {
        sh->storage_at = sh->uniform_ints;
        sh->uniform_ints += 2 * sh->storage.count;
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
    int varyings = (sh->frag_world ? 1 : 0) + (sh->frag_normals ? 3 : 0) + packs(sh->frag_floats) + packs(sh->frag_ints)
                 + (stage_rolls(sh, STAGE_FRAG) ? 1 : 0);
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

    // An atlas base stays with each instance, after its ordinary int words.
    // Reuse an int4's spare lane before adding an attribute. A valid layout
    // already using all 16 attributes can keep its individual palette.
    if (sh->skinned && vertex->fields.count + packs(sh->instance_floats) + packs(sh->instance_ints + 1) <= 16)
        sh->skin_base_at = sh->instance_ints++;

    // What a GPU takes, on every backend: vertices a GPU list's are read from it, not attributes
    const int vertex_attributes = sh->gpu_vertices ? 0 : vertex->fields.count;
    const int attributes = vertex_attributes + (sh->gpu_instances ? 0 : packs(sh->instance_floats) + packs(sh->instance_ints));
    const int uniform_bytes = 16 * (packs(sh->uniform_floats) + packs(sh->uniform_ints));
    if (attributes > 16) {
        sb from = {0};
        list_bound(sh, &from, true);
        diag_error(call->at, "this draw reads %d values for each vertex, and a GPU takes 16: %d fields of " STR_FMT "%s, and %d "
                   "float4s and int4s of %s", attributes, vertex_attributes, STR_ARG(vertex->name),
                   sh->dims == 3 ? ", 4 for the transform" : "", attributes - vertex_attributes - (sh->dims == 3 ? 4 : 0),
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
        if (stage_rolls(sh, STAGE_FRAG)) {
            diag_note("one of them is for its rolls: which instance of its draw a pixel is of, which its generator is made of");
        }
        if (sh->frag_normals) diag_note("three of them are what its 'Normal' turns the mesh's normals into the world by");
        return NULL;
    }
    const int uniform_limit = sh->dims == 3 ? 3968 : 3984; // TIDE_SHADER_UNIFORM_[3D_]BYTES
    if (uniform_bytes > uniform_limit) {
        sb from = {0};
        list_bound(sh, &from, false);
        diag_error(call->at, "this draw reads %d bytes of singletons%s (%s), and a draw's hold %d", uniform_bytes,
                   sh->values_type ? " and of the values it gives" : "", from.data ? from.data : "", uniform_limit);
        diag_note("read fewer of their fields in the frag and the vert: only what they read is sent");
        return NULL;
    }
    if (sh->textures.count > SHADER_TEXTURES) {
        diag_error(call->at, "this draw reads %d textures, and a draw takes %d", sh->textures.count, SHADER_TEXTURES);
        diag_note("each texture the code samples counts once, however it's read, and so does each list it reads; put several "
                  "images side by side in one texture");
        return NULL;
    }
    if (plan_samplers(sh) > SHADER_TEXTURES) {
        diag_error(call->at, "this draw reads its textures %d ways, and a draw has %d samplers", sh->samplers.count,
                   SHADER_TEXTURES);
        diag_note("textures read the same way share a sampler: a filter and a wrap, a depth read or compared, a cube's filter. "
                  "A Texture that says its own filter or wrap has samplers of its own: say them at the Sample to share");
        return NULL;
    }
    sh->listed = listable(sh);
    write_wgsl(prog, sh);
    vec_push(prog->shaders, sh);
    return sh;
}

static void bind_view(decl *view, const shader *sh, const expr *call);

#include "shader_kernel.inc"

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
        bind_view(draw->in, sh, call);
    }
    for (int i = 0; i < prog->compute_runs.count; i++) {
        const mesh_draw *run = &prog->compute_runs.items[i];
        shader *sh = plan_kernel(prog, run->call, run->call->kernel);
        if (!sh) continue;
        run->call->shader = sh;
        bind_view(run->in, sh, run->call);
    }
}

// What a view's draws and runs read that it doesn't take itself, it's handed (codegen's bind_views): views can infer
// missing inputs, and functions must already take them explicitly.
static void bind_view(decl *view, const shader *sh, const expr *call)
{
        for (int b = 0; b < sh->bound.count; b++) {
            decl *d = sh->bound.items[b];
            if (d->binding_of) continue;
            bool has = false;
            for (int k = 0; k < view->params.count; k++) {
                param *p = &view->params.items[k];
                bool takes = p->type.decl == d && (p->type.kind == TY_COMPONENT || p->type.kind == TY_SINGLETON)
                          && (p->mode == PARAM_READ || p->mode == PARAM_MUT) && !p->is_out;
                // What's drawn is the entity as the view draws it: a component it
                // takes from the confirmed world beside that isn't the mesh's
                if (view->verified_join && p->verified && p->type.kind == TY_COMPONENT) takes = false;
                if (takes && shader_reads_local(sh, d)) {
                    if (!p->local_use) p->local_use_at = call->at;
                    p->local_use = true; // Its local fields come with it
                }
                has |= takes;
            }
            for (int k = 0; k < view->bound.count; k++) has |= view->bound.items[k] == d;
            if (!has && view->kind == DECL_SYSTEM) vec_push(view->bound, d);
        }
}

void print_shaders(const program *prog, sb *out)
{
    for (int i = 0; i < prog->shaders.count; i++) {
        const shader *sh = prog->shaders.items[i];
        sb_printf(out, "// %s: WGSL\n%s\n", sh->name, sh->wgsl);
    }
    if (prog->shaders.count == 0) sb_put(out, "// No mesh draw draws through a shader of the game's.\n");
}

bool write_spirv(const program *prog, const char *dir, sb *out)
{
    for (int i = 0; i < prog->shaders.count; i++) {
        const shader *sh = prog->shaders.items[i];
        char error[256];
        size_t count = 0;
        uint32_t *words = tide_wgsl_to_spirv(sh->wgsl, &count, error, sizeof error);
        if (!words) {
            diag_error((loc){0}, "the WGSL tidec wrote for %s didn't become SPIR-V (%s): this is a bug in tidec", sh->name, error);
            return false;
        }
        sb path = {0};
        sb_printf(&path, "%s/%d.spv", dir, i);
        FILE *file = fopen(path.data, "wb");
        const bool written = file && fwrite(words, sizeof(uint32_t), count, file) == count;
        if (file && fclose(file) != 0) {
            free(words);
            diag_error((loc){0}, "can't write %s", path.data);
            return false;
        }
        free(words);
        if (!written) {
            diag_error((loc){0}, "can't write %s", path.data);
            return false;
        }
        sb_printf(out, "%d.spv: %s\n", i, sh->name);
    }
    return true;
}

// A name for C made of a Tide name: its letters, digits and underscores as they are.
static void put_c_name(sb *o, const str name)
{
    for (int i = 0; i < name.len; i++) {
        const char ch = name.ptr[i];
        const bool word = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_';
        sb_printf(o, "%c", word ? ch : '_');
    }
}

bool write_kernels(const program *prog, const char *path)
{
    sb o = {0};
    sb_put(&o, "// The renderer's own kernels, written by tidec --kernels from their Tide (notes/compute.md): don't edit.\n"
               "// Each one's WGSL (the web's), SPIR-V (Vulkan's) and tide_shader, and an enum of where its values' fields\n"
               "// go: _STORAGE_<field> its storage slot, _TEXTURE_<field> its texture and _COUNT_<field> the uniform int\n"
               "// saying how many elements a list has, _INT_<field> and _FLOAT_<field> where a number's words start.\n\n"
               "#pragma once\n\n#include \"tide/draw.h\"\n\n");
    const str *done[64];
    int done_count = 0;
    for (int i = 0; i < prog->shaders.count; i++) {
        const shader *sh = prog->shaders.items[i];
        if (!sh->compute || !sh->kernel) continue;
        const str name = sh->kernel->name;
        bool again = false;
        for (int k = 0; k < done_count; k++) again |= str_eq(*done[k], name);
        if (again) continue; // Run more than once: one shader, as each run plans it alike
        if (done_count == 64) {
            diag_error(sh->kernel->at, "a file of the renderer's own has more than 64 kernels");
            return false;
        }
        done[done_count++] = &sh->kernel->name;
        if (sh->rolled_at >= 0 || sh->seed_at >= 0) {
            diag_error(sh->kernel->at, "the renderer's own kernels can't roll with Random: nothing gives them a frame's");
            return false;
        }
        for (int s = 0; s < sh->storage.count; s++) {
            if (sh->storage.items[s].add) {
                diag_error(sh->kernel->at, "the renderer's own kernels can't Add: nothing gives them its scratch");
                return false;
            }
        }
        sb kernel = {0};
        sb_put(&kernel, "tide_kernel_");
        put_c_name(&kernel, name);
        sb_printf(&o, "#ifdef __wasm__\nstatic const char %s_wgsl[] = {", kernel.data);
        for (size_t c = 0; sh->wgsl[c]; c++) sb_printf(&o, "%s%d,", c % 40 == 0 ? "\n    " : "", sh->wgsl[c]);
        sb_put(&o, "0};\n#endif\n#ifdef TIDE_SPIRV\n");
        char error[256];
        size_t words = 0;
        uint32_t *spirv = tide_wgsl_to_spirv(sh->wgsl, &words, error, sizeof error);
        if (!spirv) {
            diag_error(sh->kernel->at, "the WGSL tidec wrote for %s didn't become SPIR-V (%s): this is a bug in tidec",
                       sh->name, error);
            return false;
        }
        sb_printf(&o, "static const uint32_t %s_spirv[%d] = {", kernel.data, (int)words);
        for (size_t w = 0; w < words; w++) sb_printf(&o, "%s0x%08x,", w % 10 == 0 ? "\n    " : "", spirv[w]);
        free(spirv);
        sb_put(&o, "\n};\n#endif\n");
        // Its id: what the WGSL is and how it's run, as a game's kernel's is
        uint64_t id = 0xCBF29CE484222325ull;
        for (size_t c = 0; sh->wgsl[c]; c++) id = (id ^ (uint8_t)sh->wgsl[c]) * 0x100000001B3ull;
        const int numbers[] = {sh->kernel->group[0], sh->kernel->group[1], sh->kernel->group[2], sh->uniform_floats,
                               sh->uniform_ints, sh->textures.count, sh->storage.count};
        for (size_t n = 0; n < sizeof numbers / sizeof *numbers; n++) id = (id ^ (uint32_t)numbers[n]) * 0x100000001B3ull;
        unsigned long long writes = 0, ints = 0;
        for (int s = 0; s < sh->storage.count && s < 64; s++) {
            writes |= sh->storage.items[s].write ? 1ull << s : 0u;
            ints |= sh->storage.items[s].ints ? 1ull << s : 0u;
        }
        sb_printf(&o, "static const tide_shader %s = {\n    .name = \"the renderer's " STR_FMT "\",\n"
                      "#ifdef __wasm__\n    .wgsl = %s_wgsl,\n#endif\n"
                      "#ifdef TIDE_SPIRV\n    .spirv = %s_spirv,\n    .spirv_words = %d,\n#endif\n"
                      "    .id = 0x%llxull,\n    .uniform_floats = %d,\n    .uniform_ints = %d,\n    .texture_count = %d,\n",
                  kernel.data, STR_ARG(name), kernel.data, kernel.data, (int)words, (unsigned long long)id,
                  (sh->uniform_floats + 3) / 4, (sh->uniform_ints + 3) / 4, sh->textures.count);
        shader_put_texture_reads(&o, sh);
        // Its table, after its values in its ints (the renderer's own kernels have no floats: cull.inc writes them as ints)
        sb_printf(&o, "    .storage_count = %d,\n    .storage_writes = 0x%llxull,\n    .storage_ints = 0x%llxull,\n"
                      "    .storage_table = %d,\n    .compute = true,\n    .group = {%d, %d, %d},\n};\n",
                  sh->storage.count, writes, ints, 16 * ((sh->uniform_floats + 3) / 4) + 4 * (sh->storage_at < 0 ? 0 : sh->storage_at),
                  sh->kernel->group[0], sh->kernel->group[1], sh->kernel->group[2]);
        sb_put(&o, "enum {\n");
        for (int s = 0; s < sh->storage.count; s++) {
            sb_printf(&o, "    %s_STORAGE_", kernel.data);
            put_c_name(&o, sh->storage.items[s].field->name);
            sb_printf(&o, " = %d,\n", s);
        }
        for (int t = 0; t < sh->textures.count; t++) {
            const shader_texture *texture = &sh->textures.items[t];
            if (!texture->field) continue;
            sb_printf(&o, "    %s_TEXTURE_", kernel.data);
            put_c_name(&o, texture->field->name);
            sb_printf(&o, " = %d,\n", t);
            if (texture->read != READ_DATA) continue;
            sb_printf(&o, "    %s_COUNT_", kernel.data);
            put_c_name(&o, texture->field->name);
            sb_printf(&o, " = %d,\n", texture->count_at);
        }
        for (int v = 0; v < sh->values.count; v++) {
            const shader_value *value = &sh->values.items[v];
            if (value->floats) {
                sb_printf(&o, "    %s_FLOAT_", kernel.data);
                put_c_name(&o, value->field->name);
                sb_printf(&o, " = %d,\n", value->float_at);
            }
            if (value->ints) {
                sb_printf(&o, "    %s_INT_", kernel.data);
                put_c_name(&o, value->field->name);
                sb_printf(&o, " = %d,\n", value->int_at);
            }
        }
        sb_printf(&o, "    %s_GROUP = %d,\n};\n\n", kernel.data, sh->kernel->group[0] * sh->kernel->group[1] * sh->kernel->group[2]);
    }
    FILE *file = fopen(path, "wb");
    const bool written = file && fwrite(o.data, 1, o.len, file) == o.len;
    if ((file && fclose(file) != 0) || !written) {
        diag_error((loc){0}, "can't write %s", path);
        remove(path);
        return false;
    }
    return true;
}

bool write_hlsl(const program *prog, const char *dir, sb *out)
{
    for (int i = 0; i < prog->shaders.count; i++) {
        const shader *sh = prog->shaders.items[i];
        char error[256];
        char *hlsl = tide_wgsl_to_hlsl(sh->wgsl, error, sizeof error);
        if (!hlsl) {
            diag_error((loc){0}, "the WGSL tidec wrote for %s didn't become HLSL (%s): this is a bug in tidec", sh->name, error);
            return false;
        }
        sb path = {0};
        sb_printf(&path, "%s/%d.hlsl", dir, i);
        FILE *file = fopen(path.data, "wb");
        const size_t length = strlen(hlsl);
        const bool written = file && fwrite(hlsl, 1, length, file) == length;
        free(hlsl);
        if ((file && fclose(file) != 0) || !written) {
            diag_error((loc){0}, "can't write %s", path.data);
            return false;
        }
        sb_printf(out, "%d.hlsl: %s\n", i, sh->name);
    }
    return true;
}
