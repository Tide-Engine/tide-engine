// WGSL to HLSL (see hlsl.h): the text is read into spirv.c's tree
// (wgsl_read.inc), then written out as HLSL, each expression's type worked
// out on the way, as spirv.c does for SPIR-V. It takes the WGSL spirv.c
// takes, and keeps what WebGPU makes safe safe: integer division and
// remainder by zero, shifts past the width, floats to ints past their range
// and indices past an array's end.

#include "hlsl.h"

#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wgsl_tree.inc"

// ---------------------------------------------------------------------------
// Text

typedef struct hs {
    char *s;
    size_t n, cap;
} hs;

static void hputn(hs *o, const char *s, const size_t n)
{
    if (o->n + n + 1 > o->cap) {
        size_t cap = o->cap ? o->cap * 2 : 1024;
        while (cap < o->n + n + 1) cap *= 2;
        o->s = realloc(o->s, cap);
        if (!o->s) abort();
        o->cap = cap;
    }
    memcpy(o->s + o->n, s, n);
    o->n += n;
    o->s[o->n] = '\0';
}

static void hput(hs *o, const char *s)
{
    hputn(o, s, strlen(s));
}

static void hprintf(hs *o, const char *format, ...)
{
    char small[512];
    va_list args;
    va_start(args, format);
    const int n = vsnprintf(small, sizeof small, format, args);
    va_end(args);
    if (n < 0) return;
    if ((size_t)n < sizeof small) {
        hputn(o, small, (size_t)n);
        return;
    }
    char *big = malloc((size_t)n + 1);
    if (!big) abort();
    va_start(args, format);
    vsnprintf(big, (size_t)n + 1, format, args);
    va_end(args);
    hputn(o, big, (size_t)n);
    free(big);
}

// What's written so far, taken: `o` is empty again.
static char *htake(hs *o)
{
    char *s = o->s ? o->s : calloc(1, 1);
    if (!s) abort();
    *o = (hs){0};
    return s;
}

// ---------------------------------------------------------------------------
// The translation

// A name in a function's scope: a parameter, a variable or a let.
typedef struct local {
    const char *name;
    int len;
    int t;           // Its type: a pointer parameter's is the pointer's
    const char *alias; // A let of a pointer: the place it points at, as HLSL
} local;

// A loop or a switch that `break` and `continue` reach.
typedef struct control {
    bool loop;
    int wrapped; // A loop with a continuing part: its body is in a do/while(false), and this is its number, from 1
} control;

struct translation {
    arena a;
    const char *text;
    token *toks;
    int ntok, pos;
    jmp_buf failed;
    char *error;
    size_t error_size;

    sdecl **structs;
    int nstructs;
    gdecl **globals;
    int nglobals;
    fdecl **fns;
    int nfns;

    type *types;
    int ntypes, cap_types;

    hs decls;   // Structs and arrays, each after what it holds
    hs helpers; // Constructors, made as they're used
    char **made; // The helpers made, by name
    int nmade, cap_made;
    bool *declared; // By type: its declaration is in `decls`
    int cap_declared;

    local *scope;
    int nscope, cap_scope;
    fdecl *fn;
    control controls[64];
    int ncontrols;
    int loops; // Loops with continuing parts so far, which number their flags
    int temps; // Names of this file's own in functions so far
    // A function with a return inside a loop or a switch keeps what it returns, and whether it has, and returns it
    // at its end (see function): fxc and DXC take a return there for a value that may not be there
    bool flagged;
    // The functions known to take derivatives (an implicit level's sample, dpdx...), or not: a loop of one is left to
    // fxc to unroll, as derivatives need; one of none is a [loop], which fxc doesn't unroll however few its iterations
    const fdecl *derives[256];
    bool derives_answer[256];
    int nderives;
    // Indices into arrays an assignment writes by a number worked out as it runs, which fxc can't: each is
    // written as the name in `fixed_names`, a loop's counter, while it's set
    const expr *fixed[4];
    const char *fixed_names[4];
    int nfixed;
    const gdecl *uniform;
};

#include "wgsl_read.inc"

// ---------------------------------------------------------------------------
// Names

static const char *const reserved[] = {
    "AppendStructuredBuffer", "asm", "asm_fragment", "BlendState", "bool", "break", "Buffer", "ByteAddressBuffer",
    "case", "cbuffer", "centroid", "class", "column_major", "compile", "compile_fragment", "CompileShader", "const",
    "continue", "ComputeShader", "ConsumeStructuredBuffer", "default", "DepthStencilState", "DepthStencilView",
    "discard", "do", "double", "DomainShader", "dword", "else", "export", "extern", "false", "float", "for",
    "fxgroup", "GeometryShader", "groupshared", "half", "HullShader", "if", "in", "inline", "inout", "InputPatch",
    "int", "interface", "line", "lineadj", "linear", "LineStream", "matrix", "min16float", "min10float", "min16int",
    "min12int", "min16uint", "namespace", "nointerpolation", "noperspective", "NULL", "out", "OutputPatch",
    "packoffset", "pass", "pixelfragment", "PixelShader", "point", "PointStream", "precise", "RasterizerState",
    "RenderTargetView", "return", "register", "row_major", "RWBuffer", "RWByteAddressBuffer", "RWStructuredBuffer",
    "RWTexture1D", "RWTexture1DArray", "RWTexture2D", "RWTexture2DArray", "RWTexture3D", "sample", "sampler",
    "SamplerState", "SamplerComparisonState", "shared", "snorm", "stateblock", "stateblock_state", "static",
    "string", "struct", "switch", "StructuredBuffer", "tbuffer", "technique", "technique10", "technique11",
    "texture", "Texture1D", "Texture1DArray", "Texture2D", "Texture2DArray", "Texture2DMS", "Texture2DMSArray",
    "Texture3D", "TextureCube", "TextureCubeArray", "true", "typedef", "triangle", "triangleadj", "TriangleStream",
    "uint", "uniform", "unorm", "unsigned", "vector", "vertexfragment", "VertexShader", "void", "volatile", "while",
    "auto", "catch", "char", "const_cast", "delete", "dynamic_cast", "enum", "explicit", "friend", "goto", "long",
    "mutable", "new", "operator", "private", "protected", "public", "reinterpret_cast", "short", "signed",
    "sizeof", "static_cast", "template", "this", "throw", "try", "typename", "union", "using", "virtual",
    "abs", "acos", "all", "any", "asdouble", "asfloat", "asin", "asint", "asuint", "atan", "atan2", "ceil", "clamp",
    "clip", "cos", "cosh", "countbits", "cross", "ddx", "ddx_coarse", "ddx_fine", "ddy", "ddy_coarse", "ddy_fine",
    "degrees", "determinant", "distance", "dot", "dst", "exp", "exp2", "f16tof32", "f32tof16", "faceforward",
    "firstbithigh", "firstbitlow", "floor", "fma", "fmod", "frac", "frexp", "fwidth", "isfinite", "isinf", "isnan",
    "ldexp", "length", "lerp", "lit", "log", "log10", "log2", "mad", "max", "min", "modf", "mul", "noise",
    "normalize", "pow", "radians", "rcp", "reflect", "refract", "reversebits", "round", "rsqrt", "saturate", "sign",
    "sin", "sincos", "sinh", "smoothstep", "sqrt", "step", "tan", "tanh", "transpose", "trunc",
};

// A type's name HLSL has: bool3, float4x4, min16int2...
static bool vector_type_name(const char *s, const int len)
{
    static const char *const scalars[] = {"bool", "int", "uint", "float", "half", "double", "dword", "min16float",
                                          "min10float", "min16int", "min12int", "min16uint"};
    for (size_t i = 0; i < sizeof scalars / sizeof scalars[0]; i++) {
        const int n = (int)strlen(scalars[i]);
        if (len <= n || memcmp(s, scalars[i], (size_t)n) != 0) continue;
        const char *rest = s + n;
        const int left = len - n;
        if (left == 1 && rest[0] >= '1' && rest[0] <= '4') return true;
        if (left == 3 && rest[0] >= '1' && rest[0] <= '4' && rest[1] == 'x' && rest[2] >= '1' && rest[2] <= '4') return true;
    }
    return false;
}

// A WGSL name as HLSL has it: as it is, but where HLSL has it already, or it
// starts as this file's own do, which get `_w` after them.
static void put_name(hs *o, const char *name, const int len)
{
    bool taken = vector_type_name(name, len) || (len >= 7 && memcmp(name, "tidehl_", 7) == 0)
              || (len >= 5 && memcmp(name, "_Tide", 5) == 0) || (len >= 2 && name[len - 1] == 'w' && name[len - 2] == '_');
    for (size_t i = 0; !taken && i < sizeof reserved / sizeof reserved[0]; i++) taken = same(name, len, reserved[i]);
    hputn(o, name, (size_t)len);
    if (taken) hput(o, "_w");
}

// ---------------------------------------------------------------------------
// Types as HLSL has them

static void declare(translation *x, int t);

static int concrete_type(translation *x, const int t)
{
    const kind k = T(x, t)->kind;
    if (k == K_AINT) return t_simple(x, K_I32);
    if (k == K_AFLOAT) return t_simple(x, K_F32);
    return t;
}

static const char *scalar_spelling(translation *x, const int t)
{
    switch (T(x, concrete_type(x, t))->kind) {
    case K_BOOL: return "bool";
    case K_I32: return "int";
    case K_U32: return "uint";
    default: return "float";
    }
}

static void put_type(translation *x, hs *o, int t)
{
    t = concrete_type(x, t);
    const type ty = *T(x, t);
    switch (ty.kind) {
    case K_BOOL:
    case K_I32:
    case K_U32:
    case K_F32: hput(o, scalar_spelling(x, t)); return;
    case K_VEC: hprintf(o, "%s%d", scalar_spelling(x, ty.elem), ty.n); return;
    case K_MAT: hprintf(o, "float%dx%d", ty.n, T(x, ty.elem)->n); return;
    case K_ARRAY:
        declare(x, t);
        hprintf(o, "tidehl_a%d", t);
        return;
    case K_STRUCT:
        declare(x, t);
        put_name(o, ty.s->name, ty.s->len);
        return;
    case K_PTR: put_type(x, o, ty.elem); return;
    case K_SAMPLER:
    case K_SAMPLER_CMP: hput(o, "uint"); return; // How it samples: see `_TideSampling`
    case K_TEXTURE: {
        const char *texel = ty.depth ? "float" : T(x, ty.elem)->kind == K_I32 ? "int4" : T(x, ty.elem)->kind == K_U32 ? "uint4" : "float4";
        hprintf(o, "%s<%s>", ty.dim == DIM_CUBE ? "TextureCube" : ty.multisampled ? "Texture2DMS" : "Texture2D", texel);
        return;
    }
    default: fail(x, 0, 0, "a type HLSL has nothing for");
    }
}

// A struct's or an array's declaration, once, after those of what it holds.
static void declare(translation *x, const int t)
{
    if (x->cap_declared <= t) {
        const int cap = t * 2 + 64;
        x->declared = realloc(x->declared, sizeof(bool) * (size_t)cap);
        if (!x->declared) abort();
        memset(x->declared + x->cap_declared, 0, sizeof(bool) * (size_t)(cap - x->cap_declared));
        x->cap_declared = cap;
    }
    if (x->declared[t]) return;
    x->declared[t] = true;
    const type ty = *T(x, t);
    hs d = {0};
    if (ty.kind == K_ARRAY) {
        hs elem = {0};
        put_type(x, &elem, ty.elem);
        hprintf(&d, "struct tidehl_a%d { %s v[%d]; };\n", t, elem.s, ty.n);
        free(elem.s);
    } else if (ty.kind == K_STRUCT) {
        const sdecl *s = ty.s;
        hs body = {0};
        for (int i = 0; i < s->count; i++) {
            hput(&body, "    ");
            put_type(x, &body, s->member_types[i]);
            hput(&body, " ");
            put_name(&body, s->members[i].name, s->members[i].len);
            hput(&body, ";\n");
        }
        hput(&d, "struct ");
        put_name(&d, s->name, s->len);
        hprintf(&d, " {\n%s};\n", body.s ? body.s : "    int tidehl_none;\n");
        free(body.s);
    }
    if (d.s) hput(&x->decls, d.s);
    free(d.s);
}

// Whether the helper `name` is made yet: true the first time, which makes it.
static bool first_time(translation *x, const char *name)
{
    for (int i = 0; i < x->nmade; i++) {
        if (strcmp(x->made[i], name) == 0) return false;
    }
    if (x->nmade == x->cap_made) {
        x->cap_made = x->cap_made ? x->cap_made * 2 : 16;
        x->made = realloc(x->made, sizeof(char *) * (size_t)x->cap_made);
        if (!x->made) abort();
    }
    const size_t n = strlen(name) + 1;
    x->made[x->nmade] = grab(&x->a, n);
    memcpy(x->made[x->nmade++], name, n);
    return true;
}

// ---------------------------------------------------------------------------
// Scope

static void bind(translation *x, const char *name, const int len, const int t, const char *alias)
{
    if (x->nscope == x->cap_scope) {
        x->cap_scope = x->cap_scope ? x->cap_scope * 2 : 64;
        x->scope = realloc(x->scope, sizeof(local) * (size_t)x->cap_scope);
        if (!x->scope) abort();
    }
    x->scope[x->nscope++] = (local){.name = name, .len = len, .t = t, .alias = alias};
}

static const local *find_local(translation *x, const char *name, const int len)
{
    for (int i = x->nscope - 1; i >= 0; i--) {
        if (x->scope[i].len == len && memcmp(x->scope[i].name, name, (size_t)len) == 0) return &x->scope[i];
    }
    return NULL;
}

static const gdecl *find_global(translation *x, const char *name, const int len)
{
    for (int i = 0; i < x->nglobals; i++) {
        if (x->globals[i]->len == len && memcmp(x->globals[i]->name, name, (size_t)len) == 0) return x->globals[i];
    }
    return NULL;
}

static fdecl *find_fn(translation *x, const char *name, const int len)
{
    for (int i = 0; i < x->nfns; i++) {
        if (x->fns[i]->len == len && memcmp(x->fns[i]->name, name, (size_t)len) == 0) return x->fns[i];
    }
    return NULL;
}

static void signature(translation *x, fdecl *f)
{
    if (f->typed) return;
    f->result_type = f->result ? resolve(x, f->result) : t_simple(x, K_VOID);
    f->param_types = grab(&x->a, sizeof(int) * (size_t)(f->count ? f->count : 1));
    for (int i = 0; i < f->count; i++) f->param_types[i] = resolve(x, f->params[i].type);
    f->typed = true;
}

// A global's type: a constant's, from its value where it says none.
static int global_type(translation *x, const gdecl *g)
{
    if (g->type) return resolve(x, g->type);
    const expr *e = g->init;
    if (e->kind == X_UNARY) e = e->a;
    if (e->kind == X_BOOL) return t_simple(x, K_BOOL);
    if (e->kind == X_INT) return t_simple(x, e->suffix == 'u' ? K_U32 : K_I32);
    if (e->kind == X_FLOAT) return t_simple(x, K_F32);
    fail(x, g->line, g->col, "a module's constant here is a number");
    return 0;
}

// The slot of a texture or a sampler, by its binding: texture i at 1 + 2i, its sampler at 2 + 2i.
static int handle_slot(translation *x, const gdecl *g, const bool sampler)
{
    const int b = g->at.binding;
    if (g->at.group != 0 || b < 1 || b > 2 * 12 || (b % 2 == 0) != sampler) {
        fail(x, g->line, g->col, "'%.*s' isn't at a binding a texture or its sampler has here", g->len, g->name);
    }
    return sampler ? (b - 2) / 2 : (b - 1) / 2;
}

// ---------------------------------------------------------------------------
// Expressions: each writes its HLSL into `o` and gives its type

static int ex(translation *x, const expr *e, hs *o);

// An expression into a string of its own, and its type.
static int ex_text(translation *x, const expr *e, char **text)
{
    hs o = {0};
    const int t = ex(x, e, &o);
    *text = htake(&o);
    return t;
}

static void put_float(hs *o, const double v)
{
    const float f = (float)v;
    char text[64];
    snprintf(text, sizeof text, "%.9g", (double)f);
    if (!strchr(text, '.') && !strchr(text, 'e') && !strchr(text, 'n') && !strchr(text, 'i')) strcat(text, ".0");
    if (f < 0) hprintf(o, "(%s)", text);
    else hput(o, text);
}

static void put_int(hs *o, const int64_t v, const bool unsigned_)
{
    if (unsigned_ || v > 2147483647ll) hprintf(o, "%lluu", (unsigned long long)(uint64_t)v & 0xFFFFFFFFull);
    else if (v == -2147483648ll) hput(o, "(-2147483647 - 1)");
    else if (v < 0) hprintf(o, "(%lld)", (long long)v);
    else hprintf(o, "%lld", (long long)v);
}

static int deref_type(translation *x, const int t)
{
    return T(x, t)->kind == K_PTR ? T(x, t)->elem : t;
}

static int swizzle_component(const char c)
{
    switch (c) {
    case 'x': case 'r': return 0;
    case 'y': case 'g': return 1;
    case 'z': case 'b': return 2;
    case 'w': case 'a': return 3;
    default: return -1;
    }
}

static int name_expr(translation *x, const expr *e, hs *o)
{
    const local *l = find_local(x, e->name, e->len);
    if (l) {
        if (l->alias) hput(o, l->alias);
        else put_name(o, e->name, e->len);
        return l->t;
    }
    const gdecl *g = find_global(x, e->name, e->len);
    if (!g) fail(x, e->line, e->col, "'%.*s' isn't declared", e->len, e->name);
    if (g->is_const) {
        put_name(o, g->name, g->len);
        return global_type(x, g);
    }
    const int t = resolve(x, g->type);
    switch (T(x, t)->kind) {
    case K_TEXTURE: hprintf(o, "_TideTexture%d", handle_slot(x, g, false)); return t;
    case K_SAMPLER:
    case K_SAMPLER_CMP: {
        const int slot = handle_slot(x, g, true);
        hprintf(o, "((%s >> %du) & 15u)", slot < 8 ? "_TideSampling" : "_TideSamplingHigh", 4 * (slot % 8));
        return t;
    }
    default: break;
    }
    if (g->space == SPACE_UNIFORM) fail(x, e->line, e->col, "the uniform block is read by its members here");
    put_name(o, g->name, g->len);
    return t;
}

static int member_expr(translation *x, const expr *e, hs *o)
{
    // The uniform block's members are the cbuffer's
    if (e->a->kind == X_NAME && !find_local(x, e->a->name, e->a->len)) {
        const gdecl *g = find_global(x, e->a->name, e->a->len);
        if (g && !g->is_const && g->space == SPACE_UNIFORM) {
            const sdecl *s = T(x, resolve(x, g->type))->s;
            for (int i = 0; i < s->count; i++) {
                if (s->members[i].len == e->len
                    && memcmp(s->members[i].name, e->name, (size_t)e->len) == 0) {
                    hput(o, "tidehl_u_");
                    hputn(o, s->members[i].name, (size_t)s->members[i].len);
                    return s->member_types[i];
                }
            }
            fail(x, e->line, e->col, "'%.*s' has no member '%.*s'", s->len, s->name, e->len, e->name);
        }
    }
    char *base;
    const int bt = deref_type(x, ex_text(x, e->a, &base));
    const type ty = *T(x, bt);
    if (ty.kind == K_STRUCT) {
        for (int i = 0; i < ty.s->count; i++) {
            if (ty.s->members[i].len != e->len || memcmp(ty.s->members[i].name, e->name, (size_t)e->len) != 0) continue;
            hprintf(o, "%s.", base);
            put_name(o, e->name, e->len);
            free(base);
            return ty.s->member_types[i];
        }
        free(base);
        fail(x, e->line, e->col, "'%.*s' has no member '%.*s'", ty.s->len, ty.s->name, e->len, e->name);
    }
    if (ty.kind == K_VEC) {
        if (e->len < 1 || e->len > 4) fail(x, e->line, e->col, "'.%.*s' isn't a swizzle", e->len, e->name);
        for (int i = 0; i < e->len; i++) {
            const int c = swizzle_component(e->name[i]);
            if (c < 0 || c >= ty.n) fail(x, e->line, e->col, "'.%.*s' isn't a swizzle of a vec%d", e->len, e->name, ty.n);
        }
        hprintf(o, "%s.%.*s", base, e->len, e->name);
        free(base);
        return e->len == 1 ? ty.elem : t_vec(x, ty.elem, e->len);
    }
    free(base);
    fail(x, e->line, e->col, "that has no members");
    return 0;
}

// An index past the end, as WebGPU has it: the last element instead.
static int index_expr(translation *x, const expr *e, hs *o)
{
    char *base, *at;
    const int bt = deref_type(x, ex_text(x, e->a, &base));
    const int it = ex_text(x, e->b, &at);
    const type ty = *T(x, bt);
    int elem;
    if (ty.kind == K_VEC || ty.kind == K_ARRAY) elem = ty.elem;
    else if (ty.kind == K_MAT) elem = ty.elem;
    else fail(x, e->line, e->col, "that isn't indexed"), elem = 0;
    if (!is_int(x, it) || !is_scalar(x, it)) fail(x, e->line, e->col, "an index is an i32 or a u32");
    hprintf(o, "%s%s[", base, ty.kind == K_ARRAY ? ".v" : "");
    for (int k = 0; k < x->nfixed; k++) {
        if (x->fixed[k] != e) continue;
        hprintf(o, "%s]", x->fixed_names[k]);
        free(base);
        free(at);
        return elem;
    }
    if (e->b->kind == X_INT) {
        const int64_t i = e->b->i;
        hprintf(o, "%lld]", (long long)(i < 0 ? 0 : i >= ty.n ? ty.n - 1 : i));
    } else {
        hprintf(o, "min((uint)(%s), %du)]", at, ty.n - 1);
    }
    free(base);
    free(at);
    return elem;
}

// A value of `want`, from `text` of `have`: a scalar spread, or as it is.
static void put_as(translation *x, hs *o, const char *text, const int have, const int want)
{
    if (T(x, want)->kind == K_VEC && width_of(x, have) == 1 && is_scalar(x, have)) {
        hput(o, "((");
        put_type(x, o, want);
        hprintf(o, ")(%s))", text);
    } else {
        hput(o, text);
    }
}

static int binop(translation *x, const punct op, const int ta, const char *a, const int tb, const char *b, hs *o,
                 const int line, const int col)
{
    const type A = *T(x, ta), B = *T(x, tb);
    // Matrices, which are WGSL's transposed
    if (A.kind == K_MAT || B.kind == K_MAT) {
        if (op == P_STAR) {
            if (A.kind == K_MAT && B.kind == K_VEC) {
                hprintf(o, "mul(%s, %s)", b, a);
                return A.elem;
            }
            if (A.kind == K_VEC && B.kind == K_MAT) {
                hprintf(o, "mul(%s, %s)", b, a);
                return t_vec(x, t_simple(x, K_F32), B.n);
            }
            if (A.kind == K_MAT && B.kind == K_MAT) {
                hprintf(o, "mul(%s, %s)", b, a);
                return t_mat(x, B.n, T(x, A.elem)->n);
            }
            hprintf(o, "(%s * %s)", a, b);
            return A.kind == K_MAT ? ta : tb;
        }
        if ((op == P_PLUS || op == P_MINUS) && ta == tb) {
            hprintf(o, "(%s %c %s)", a, op == P_PLUS ? '+' : '-', b);
            return ta;
        }
        fail(x, line, col, "matrices don't go together that way");
    }
    const bool abstract_a = A.kind == K_AINT || A.kind == K_AFLOAT, abstract_b = B.kind == K_AINT || B.kind == K_AFLOAT;
    const int wa = width_of(x, ta), wb = width_of(x, tb);
    const int width = wa > wb ? wa : wb;
    // The scalar both come to
    int scalar;
    if (abstract_a && abstract_b) {
        scalar = t_simple(x, A.kind == K_AFLOAT || B.kind == K_AFLOAT ? K_AFLOAT : K_AINT);
    } else if (op == P_SHL || op == P_SHR) {
        scalar = abstract_a ? t_simple(x, K_I32) : scalar_of(x, ta);
    } else {
        scalar = abstract_a ? scalar_of(x, tb) : scalar_of(x, ta);
    }
    const int t = t_vec(x, scalar, width);
    const kind sk = T(x, scalar)->kind;
    const char *sym;
    switch (op) {
    case P_EQ: sym = "=="; break;
    case P_NE: sym = "!="; break;
    case P_LT: sym = "<"; break;
    case P_LE: sym = "<="; break;
    case P_GT: sym = ">"; break;
    case P_GE: sym = ">="; break;
    default: sym = NULL; break;
    }
    if (sym) {
        hprintf(o, "(%s %s %s)", a, sym, b);
        return t_vec(x, t_simple(x, K_BOOL), width);
    }
    if (op == P_SHL || op == P_SHR) { // By the count's low 5 bits, as WebGPU shifts
        hprintf(o, "(%s %s (%s & 31u))", a, op == P_SHL ? "<<" : ">>", b);
        return abstract_a ? t_vec(x, abstract_b ? t_simple(x, K_AINT) : t_simple(x, K_I32), wa) : ta;
    }
    if (sk == K_BOOL) {
        const char *bsym = op == P_AMP ? "&&" : op == P_PIPE ? "||" : op == P_CARET ? "!=" : NULL;
        if (!bsym) fail(x, line, col, "bools don't do arithmetic");
        hprintf(o, "(%s %s %s)", a, bsym, b);
        return t;
    }
    if (op == P_AMP || op == P_PIPE || op == P_CARET) {
        hprintf(o, "(%s %c %s)", a, op == P_AMP ? '&' : op == P_PIPE ? '|' : '^', b);
        return t;
    }
    if (op == P_PERCENT && (sk == K_F32 || sk == K_AFLOAT)) {
        hs l = {0}, r = {0};
        put_as(x, &l, a, ta, t);
        put_as(x, &r, b, tb, t);
        hprintf(o, "fmod(%s, %s)", l.s, r.s);
        free(l.s);
        free(r.s);
        return t;
    }
    if ((op == P_SLASH || op == P_PERCENT) && (sk == K_I32 || sk == K_U32)) {
        // As WebGPU divides: by zero, or the least i32 by -1, as by 1
        hs l = {0}, r = {0};
        put_as(x, &l, a, abstract_a ? t_vec(x, scalar, wa) : ta, t);
        put_as(x, &r, b, abstract_b ? t_vec(x, scalar, wb) : tb, t);
        hprintf(o, "tidehl_%s(", op == P_SLASH ? "div" : "rem");
        if (abstract_a || T(x, ta)->kind != T(x, t)->kind || width_of(x, ta) != width) {
            hput(o, "(");
            put_type(x, o, t);
            hprintf(o, ")(%s)", l.s);
        } else {
            hput(o, l.s);
        }
        hput(o, ", ");
        if (abstract_b || width_of(x, tb) != width) {
            hput(o, "(");
            put_type(x, o, t);
            hprintf(o, ")(%s)", r.s);
        } else {
            hput(o, r.s);
        }
        hput(o, ")");
        free(l.s);
        free(r.s);
        return t;
    }
    const char *asym = op == P_PLUS ? "+" : op == P_MINUS ? "-" : op == P_STAR ? "*" : op == P_SLASH ? "/" : "%";
    hprintf(o, "(%s %s %s)", a, asym, b);
    return t;
}

static int unary(translation *x, const expr *e, hs *o)
{
    char *v;
    const int t = ex_text(x, e->a, &v);
    if (e->op == P_MINUS) hprintf(o, "(-%s)", v);
    else if (e->op == P_BANG) hprintf(o, "(!%s)", v);
    else hprintf(o, "(~%s)", v);
    free(v);
    return t;
}

// The arguments of a call, each into a string, and their types.
typedef struct args {
    char *text[64];
    int t[64];
    int n;
} args;

static void eval_args(translation *x, const expr *e, args *a)
{
    if (e->nargs > 64) fail(x, e->line, e->col, "too many arguments");
    a->n = e->nargs;
    for (int i = 0; i < e->nargs; i++) a->t[i] = ex_text(x, e->args[i], &a->text[i]);
}

static void free_args(args *a)
{
    for (int i = 0; i < a->n; i++) free(a->text[i]);
}

// The scalar a call's numbers come to: the first concrete one's, or by
// default an f32 if any is a float (or `floats` says the function takes
// floats alone), else an i32.
static int common_scalar(translation *x, const args *a, const int from, const int n, const bool floats)
{
    bool any_float = floats;
    for (int i = from; i < from + n; i++) {
        const kind k = T(x, a->t[i])->kind;
        if (k != K_AINT && k != K_AFLOAT) return scalar_of(x, a->t[i]);
        if (k == K_AFLOAT) any_float = true;
    }
    return t_simple(x, any_float ? K_F32 : K_I32);
}

static int widest(translation *x, const args *a, const int from, const int n)
{
    int w = 1;
    for (int i = from; i < from + n; i++) {
        if (width_of(x, a->t[i]) > w) w = width_of(x, a->t[i]);
    }
    return w;
}

// A number's (or a vector's numbers') conversion to `want`, as WGSL's f32(),
// i32(), u32() and bool() do: a float past an int's range is the nearest it
// holds, as WebGPU has it.
static void convert(translation *x, hs *o, const char *v, const int have, const int want)
{
    const kind from = T(x, concrete_type(x, scalar_of(x, have)))->kind, to = T(x, scalar_of(x, want))->kind;
    const bool spread = T(x, want)->kind == K_VEC && width_of(x, have) == 1;
    if (to == K_BOOL) {
        hprintf(o, "(%s != ", v);
        hput(o, "(");
        put_type(x, o, spread ? want : with_scalar(x, have, scalar_of(x, have)));
        hput(o, ")0)");
        return;
    }
    if (from == K_F32 && (to == K_I32 || to == K_U32) && T(x, scalar_of(x, have))->kind != K_AFLOAT) {
        if (spread) {
            hput(o, "((");
            put_type(x, o, want);
            hprintf(o, ")(tidehl_f2%c(%s)))", to == K_I32 ? 'i' : 'u', v);
        } else {
            hprintf(o, "tidehl_f2%c(%s)", to == K_I32 ? 'i' : 'u', v);
        }
        return;
    }
    hput(o, "((");
    put_type(x, o, want);
    hprintf(o, ")(%s))", v);
}

static int construct_helper(translation *x, const int t, const args *a, hs *o)
{
    const type ty = *T(x, t);
    const int count = ty.kind == K_ARRAY ? ty.n : ty.s->count;
    hs name = {0};
    hput(&name, "tidehl_make_");
    if (ty.kind == K_ARRAY) hprintf(&name, "a%d", t);
    else put_name(&name, ty.s->name, ty.s->len);
    if (first_time(x, name.s)) {
        hs h = {0};
        put_type(x, &h, t);
        hprintf(&h, " %s(", name.s);
        for (int i = 0; i < count; i++) {
            if (i) hput(&h, ", ");
            put_type(x, &h, ty.kind == K_ARRAY ? ty.elem : ty.s->member_types[i]);
            hprintf(&h, " v%d", i);
        }
        hput(&h, ") {\n    ");
        put_type(x, &h, t);
        hput(&h, " r;\n");
        for (int i = 0; i < count; i++) {
            if (ty.kind == K_ARRAY) {
                hprintf(&h, "    r.v[%d] = v%d;\n", i, i);
            } else {
                hput(&h, "    r.");
                put_name(&h, ty.s->members[i].name, ty.s->members[i].len);
                hprintf(&h, " = v%d;\n", i);
            }
        }
        hput(&h, "    return r;\n}\n");
        hput(&x->helpers, h.s);
        free(h.s);
    }
    hprintf(o, "%s(", name.s);
    for (int i = 0; i < a->n; i++) hprintf(o, "%s%s", i ? ", " : "", a->text[i]);
    hput(o, ")");
    free(name.s);
    return t;
}

static int zero_of(translation *x, const int t, hs *o)
{
    hput(o, "((");
    put_type(x, o, t);
    hput(o, ")0)");
    return t;
}

static int construct_vector(translation *x, const expr *e, int scalar, const int n, hs *o)
{
    args a;
    eval_args(x, e, &a);
    if (!scalar) scalar = a.n ? common_scalar(x, &a, 0, a.n, false) : t_simple(x, K_F32);
    const int t = t_vec(x, scalar, n);
    if (a.n == 0) {
        zero_of(x, t, o);
    } else if (a.n == 1 && (is_scalar(x, a.t[0]) || width_of(x, a.t[0]) == n)) {
        convert(x, o, a.text[0], a.t[0], t);
    } else {
        put_type(x, o, t);
        hput(o, "(");
        for (int i = 0; i < a.n; i++) {
            if (i) hput(o, ", ");
            const kind from = T(x, scalar_of(x, a.t[i]))->kind, to = T(x, scalar)->kind;
            if (from != to && from != K_AINT && from != K_AFLOAT) convert(x, o, a.text[i], a.t[i], with_scalar(x, a.t[i], scalar));
            else hput(o, a.text[i]);
        }
        hput(o, ")");
    }
    free_args(&a);
    return t;
}

static int construct(translation *x, const expr *e, hs *o, bool *made)
{
    *made = true;
    const tnode *n = e->type;
    tnode plain = {.name = e->name, .len = e->len, .line = e->line, .col = e->col};
    if (!n) n = &plain;
    const char *s = n->name;
    const int len = n->len;
    if (!n->nargs) { // Those whose template may be left out: vec3(...), array(...), mat3x3(...)
        if (len == 4 && memcmp(s, "vec", 3) == 0 && s[3] >= '2' && s[3] <= '4') return construct_vector(x, e, 0, s[3] - '0', o);
        if (same(s, len, "array")) {
            args a;
            eval_args(x, e, &a);
            if (!a.n) fail(x, e->line, e->col, "an empty array says its type and count");
            const int t = t_array(x, concrete_type(x, a.t[0]), a.n);
            construct_helper(x, t, &a, o);
            free_args(&a);
            return t;
        }
        if (len == 6 && memcmp(s, "mat", 3) == 0 && s[4] == 'x') {
            const int t = t_mat(x, s[3] - '0', s[5] - '0');
            args a;
            eval_args(x, e, &a);
            if (!a.n) zero_of(x, t, o);
            else {
                put_type(x, o, t);
                hput(o, "(");
                for (int i = 0; i < a.n; i++) hprintf(o, "%s%s", i ? ", " : "", a.text[i]);
                hput(o, ")");
            }
            free_args(&a);
            return t;
        }
    }
    const int t = resolve_maybe(x, n);
    if (!t) {
        *made = false;
        return 0;
    }
    const type ty = *T(x, t);
    switch (ty.kind) {
    case K_BOOL:
    case K_I32:
    case K_U32:
    case K_F32: {
        args a;
        eval_args(x, e, &a);
        if (!a.n) zero_of(x, t, o);
        else convert(x, o, a.text[0], a.t[0], t);
        free_args(&a);
        return t;
    }
    case K_VEC: return construct_vector(x, e, ty.elem, ty.n, o);
    case K_MAT: {
        args a;
        eval_args(x, e, &a);
        if (!a.n) zero_of(x, t, o);
        else if (a.n == 1) hput(o, a.text[0]);
        else {
            put_type(x, o, t);
            hput(o, "(");
            for (int i = 0; i < a.n; i++) hprintf(o, "%s%s", i ? ", " : "", a.text[i]);
            hput(o, ")");
        }
        free_args(&a);
        return t;
    }
    case K_ARRAY:
    case K_STRUCT: {
        args a;
        eval_args(x, e, &a);
        if (!a.n) zero_of(x, t, o);
        else construct_helper(x, t, &a, o);
        free_args(&a);
        return t;
    }
    default: fail(x, e->line, e->col, "a value of that type isn't made by a call");
    }
    return 0;
}

static int bitcast(translation *x, const expr *e, hs *o)
{
    if (!e->type || e->type->nargs != 1) fail(x, e->line, e->col, "bitcast says the type it gives");
    const int to = resolve(x, e->type->args[0]);
    args a;
    eval_args(x, e, &a);
    if (a.n != 1) fail(x, e->line, e->col, "bitcast takes one value");
    const kind k = T(x, scalar_of(x, to))->kind;
    const char *fn = k == K_F32 ? "asfloat" : k == K_I32 ? "asint" : "asuint";
    if (T(x, a.t[0])->kind == K_AINT) {
        hprintf(o, "%s(", fn);
        put_int(o, e->args[0]->kind == X_INT ? e->args[0]->i : 0, e->args[0]->kind == X_INT && e->args[0]->i > 2147483647ll);
        if (e->args[0]->kind != X_INT) hprintf(o, " + (%s)", a.text[0]);
        hput(o, ")");
    } else {
        hprintf(o, "%s(%s)", fn, a.text[0]);
    }
    free_args(&a);
    return to;
}

static int texture_call(translation *x, const expr *e, hs *o)
{
    const char *n = e->name;
    const int len = e->len, line = e->line, col = e->col;
    args a;
    eval_args(x, e, &a);
    if (a.n < 1 || T(x, a.t[0])->kind != K_TEXTURE) fail(x, line, col, "'%.*s' takes a texture", len, n);
    const type tex = *T(x, a.t[0]);
    const int f32 = t_simple(x, K_F32), u32 = t_simple(x, K_U32);
    const int result = tex.depth ? f32 : t_vec(x, tex.elem, 4);
    int t = result;
    // The texture's own sampler, which OpenGL samples with (TIDEHL_OWN_SAMPLERS): sampler_<texture> for a module's
    // texture, as Unity names a texture's sampler; one of the inline ones for a texture a function was given
    char own[64];
    if (strncmp(a.text[0], "_TideTexture", 12) == 0) snprintf(own, sizeof own, "sampler%s", a.text[0]);
    else snprintf(own, sizeof own, "tidehl_linear_clamp_sampler");
    if (same(n, len, "textureSample") || same(n, len, "textureSampleLevel") || same(n, len, "textureSampleBias")
        || same(n, len, "textureSampleGrad")) {
        if (a.n < 3) fail(x, line, col, "'%.*s' takes a texture, a sampler and where", len, n);
        if (tex.depth) {
            hprintf(o, "tidehl_depth(%s, %s, %s)", a.text[0], own, a.text[2]);
        } else if (same(n, len, "textureSample")) {
            hprintf(o, "tidehl_sample(%s, %s, %s, %s)", a.text[0], own, a.text[1], a.text[2]);
        } else if (same(n, len, "textureSampleGrad")) {
            if (a.n != 5) fail(x, line, col, "textureSampleGrad takes 5 arguments");
            hprintf(o, "tidehl_sample_grad(%s, %s, %s, %s, %s, %s)", a.text[0], own, a.text[1], a.text[2], a.text[3], a.text[4]);
        } else {
            if (a.n != 4) fail(x, line, col, "'%.*s' takes 4 arguments", len, n);
            hprintf(o, "tidehl_sample_%s(%s, %s, %s, %s, float(%s))", n[13] == 'L' ? "level" : "bias", a.text[0], own, a.text[1],
                    a.text[2], a.text[3]);
        }
    } else if (same(n, len, "textureSampleCompare") || same(n, len, "textureSampleCompareLevel")) {
        if (a.n != 4 || !tex.depth) fail(x, line, col, "'%.*s' takes a depth texture, a sampler, where and a depth", len, n);
        hprintf(o, "tidehl_compare(%s, %s, %s, %s)", a.text[0], own, a.text[2], a.text[3]);
    } else if (same(n, len, "textureLoad")) {
        if (a.n != 3) fail(x, line, col, "textureLoad takes a texture, where and a level or sample");
        hprintf(o, "tidehl_load(%s, int2(%s), int(%s))", a.text[0], a.text[1], a.text[2]);
    } else if (same(n, len, "textureDimensions")) {
        t = t_vec(x, u32, 2);
        if (tex.multisampled) hprintf(o, "tidehl_size(%s)", a.text[0]);
        else hprintf(o, "tidehl_size(%s, uint(%s))", a.text[0], a.n > 1 ? a.text[1] : "0");
    } else if (same(n, len, "textureNumLevels")) {
        t = u32;
        hprintf(o, "tidehl_levels(%s)", a.text[0]);
    } else if (same(n, len, "textureNumSamples")) {
        t = u32;
        hprintf(o, "tidehl_samples(%s)", a.text[0]);
    } else {
        fail(x, line, col, "'%.*s' isn't a function", len, n);
    }
    free_args(&a);
    return t;
}

// Math whose HLSL takes the same arguments: WGSL's name, HLSL's, how many
// arguments, whether its abstract numbers are floats, and whether it gives a
// scalar of its arguments'.
static const struct {
    const char *wgsl, *hlsl;
    int args;
    bool floats, scalar;
} maths[] = {
    {"abs", "abs", 1, false, false}, {"min", "min", 2, false, false}, {"max", "max", 2, false, false},
    {"clamp", "clamp", 3, false, false}, {"floor", "floor", 1, true, false}, {"ceil", "ceil", 1, true, false},
    {"round", "round", 1, true, false}, {"trunc", "trunc", 1, true, false}, {"fract", "frac", 1, true, false},
    {"sqrt", "sqrt", 1, true, false}, {"inverseSqrt", "rsqrt", 1, true, false}, {"radians", "radians", 1, true, false},
    {"degrees", "degrees", 1, true, false}, {"sin", "sin", 1, true, false}, {"cos", "cos", 1, true, false},
    {"tan", "tan", 1, true, false}, {"asin", "asin", 1, true, false}, {"acos", "acos", 1, true, false},
    {"atan", "atan", 1, true, false}, {"sinh", "sinh", 1, true, false}, {"cosh", "cosh", 1, true, false},
    {"tanh", "tanh", 1, true, false}, {"asinh", "tidehl_asinh", 1, true, false},
    {"acosh", "tidehl_acosh", 1, true, false}, {"atanh", "tidehl_atanh", 1, true, false},
    {"atan2", "atan2", 2, true, false}, {"pow", "tidehl_pow", 2, true, false}, {"exp", "exp", 1, true, false},
    {"exp2", "exp2", 1, true, false}, {"log", "log", 1, true, false}, {"log2", "log2", 1, true, false},
    {"step", "step", 2, true, false}, {"smoothstep", "smoothstep", 3, true, false}, {"fma", "mad", 3, true, false},
    {"normalize", "normalize", 1, true, false}, {"length", "length", 1, true, true},
    {"distance", "distance", 2, true, true}, {"cross", "cross", 2, true, false}, {"reflect", "reflect", 2, true, false},
    {"faceForward", "faceforward", 3, true, false}, {"saturate", "saturate", 1, true, false},
    {"mix", "lerp", 3, true, false}, {"refract", "refract", 3, true, false}, {"ldexp", "ldexp", 2, true, false},
    {"dpdx", "ddx", 1, true, false}, {"dpdy", "ddy", 1, true, false}, {"fwidth", "fwidth", 1, true, false},
    {"dpdxFine", "ddx_fine", 1, true, false}, {"dpdyFine", "ddy_fine", 1, true, false},
    {"dpdxCoarse", "ddx_coarse", 1, true, false}, {"dpdyCoarse", "ddy_coarse", 1, true, false},
    {"fwidthFine", "tidehl_fwidth_fine", 1, true, false}, {"fwidthCoarse", "tidehl_fwidth_coarse", 1, true, false},
};

static int builtin(translation *x, const expr *e, hs *o)
{
    const char *n = e->name;
    const int len = e->len, line = e->line, col = e->col;
    if (len > 7 && memcmp(n, "texture", 7) == 0) return texture_call(x, e, o);
    args a;
    eval_args(x, e, &a);
    int t = 0;
    for (size_t i = 0; i < sizeof maths / sizeof maths[0]; i++) {
        if (!same(n, len, maths[i].wgsl)) continue;
        if (a.n != maths[i].args) fail(x, line, col, "%s takes %d arguments", maths[i].wgsl, maths[i].args);
        const bool ldexp = same(n, len, "ldexp"), refract = same(n, len, "refract");
        const int values = ldexp ? 1 : refract ? 2 : a.n;
        const int scalar = common_scalar(x, &a, 0, values, maths[i].floats);
        t = t_vec(x, scalar, widest(x, &a, 0, values));
        if (maths[i].floats && !is_float(x, scalar)) {
            char found[96];
            type_name(x, t, found, sizeof found);
            fail(x, line, col, "%s takes floats, not %s", maths[i].wgsl, found);
        }
        if (same(n, len, "abs") && T(x, scalar)->kind == K_U32) {
            hput(o, a.text[0]);
        } else {
            hprintf(o, "%s(", maths[i].hlsl);
            for (int k = 0; k < a.n; k++) {
                if (k) hput(o, ", ");
                if (k < values && width_of(x, a.t[k]) != width_of(x, t)) put_as(x, o, a.text[k], a.t[k], t);
                else if (k < values && T(x, a.t[k])->kind == K_AINT && T(x, scalar)->kind != K_I32) hprintf(o, "%s(%s)", scalar_spelling(x, scalar), a.text[k]);
                else hput(o, a.text[k]);
            }
            hput(o, ")");
        }
        if (maths[i].scalar) t = scalar;
        free_args(&a);
        return t;
    }
    if (same(n, len, "sign")) { // HLSL's gives ints
        t = concrete_type(x, a.t[0]);
        hput(o, "((");
        put_type(x, o, t);
        hprintf(o, ")sign(%s))", a.text[0]);
    } else if (same(n, len, "dot")) {
        const int scalar = common_scalar(x, &a, 0, 2, false);
        t = scalar;
        hprintf(o, "dot(%s, %s)", a.text[0], a.text[1]);
    } else if (same(n, len, "select")) {
        if (a.n != 3) fail(x, line, col, "select takes three values");
        if (!is_bool(x, a.t[2])) fail(x, line, col, "select's last value is a bool");
        const int scalar = common_scalar(x, &a, 0, 2, false);
        t = t_vec(x, scalar, widest(x, &a, 0, 2));
        hs f = {0}, v = {0};
        put_as(x, &f, a.text[0], a.t[0], t);
        put_as(x, &v, a.text[1], a.t[1], t);
        // Unity compiles with HLSL 2018's rules, whose ?: picks each element by its own condition
        hprintf(o, "(%s ? %s : %s)", a.text[2], v.s, f.s);
        free(f.s);
        free(v.s);
    } else if (same(n, len, "all") || same(n, len, "any")) {
        t = t_simple(x, K_BOOL);
        hprintf(o, "%.*s(%s)", len, n, a.text[0]);
    } else if (same(n, len, "transpose")) {
        const type m = *T(x, a.t[0]);
        t = t_mat(x, T(x, m.elem)->n, m.n);
        hprintf(o, "transpose(%s)", a.text[0]);
    } else if (same(n, len, "determinant")) {
        t = t_simple(x, K_F32);
        hprintf(o, "determinant(%s)", a.text[0]);
    } else if (same(n, len, "countOneBits") || same(n, len, "reverseBits") || same(n, len, "firstLeadingBit")
               || same(n, len, "firstTrailingBit")) {
        t = concrete_type(x, a.t[0]);
        const bool signed_ = is_signed(x, t);
        const char *fn = n[0] == 'c' ? "countbits" : n[0] == 'r' ? "reversebits" : n[5] == 'L' ? "firstbithigh" : "firstbitlow";
        hput(o, "((");
        put_type(x, o, t);
        if (n[0] == 'r' && signed_) hprintf(o, ")asint(reversebits(asuint(%s))))", a.text[0]);
        else hprintf(o, ")%s(%s))", fn, a.text[0]);
    } else if (same(n, len, "extractBits") || same(n, len, "insertBits")) {
        const bool insert = n[0] == 'i';
        if (a.n != (insert ? 4 : 3)) fail(x, line, col, "%.*s takes %d values", len, n, insert ? 4 : 3);
        t = concrete_type(x, a.t[0]);
        const int w = width_of(x, t);
        const bool signed_ = is_signed(x, t);
        if (w > 1) {
            put_type(x, o, t);
            hput(o, "(");
        }
        for (int c = 0; c < w; c++) {
            const char *part = w > 1 ? (c == 0 ? ".x" : c == 1 ? ".y" : c == 2 ? ".z" : ".w") : "";
            if (c) hput(o, ", ");
            const int vi = insert ? 2 : 1;
            if (insert) {
                hprintf(o, "%s(tidehl_insert(asuint(%s%s), asuint(%s%s), uint(%s), uint(%s)))", signed_ ? "asint" : "", a.text[0],
                        part, a.text[1], part, a.text[vi], a.text[vi + 1]);
            } else if (signed_) {
                hprintf(o, "tidehl_extract_signed(%s%s, uint(%s), uint(%s))", a.text[0], part, a.text[vi], a.text[vi + 1]);
            } else {
                hprintf(o, "tidehl_extract(%s%s, uint(%s), uint(%s))", a.text[0], part, a.text[vi], a.text[vi + 1]);
            }
        }
        if (w > 1) hput(o, ")");
    } else if (same(n, len, "quantizeToF16")) {
        t = concrete_type(x, a.t[0]);
        hprintf(o, "f16tof32(f32tof16(%s))", a.text[0]);
    } else {
        static const struct {
            const char *name;
            int to; // 0: a u32; 2 or 4: a vec2f or vec4f
        } packs[] = {
            {"pack4x8snorm", 0}, {"pack4x8unorm", 0}, {"pack2x16snorm", 0}, {"pack2x16unorm", 0}, {"pack2x16float", 0},
            {"unpack4x8snorm", 4}, {"unpack4x8unorm", 4}, {"unpack2x16snorm", 2}, {"unpack2x16unorm", 2}, {"unpack2x16float", 2},
        };
        bool found = false;
        for (size_t i = 0; i < sizeof packs / sizeof packs[0] && !found; i++) {
            if (!same(n, len, packs[i].name)) continue;
            found = true;
            t = packs[i].to ? t_vec(x, t_simple(x, K_F32), packs[i].to) : t_simple(x, K_U32);
            hprintf(o, "tidehl_%s(%s)", packs[i].name, a.text[0]);
        }
        if (!found) fail(x, line, col, "'%.*s' isn't a function", len, n);
    }
    free_args(&a);
    return t;
}

static int call_user(translation *x, fdecl *f, const expr *e, hs *o)
{
    signature(x, f);
    if (f->at.stage) fail(x, e->line, e->col, "'%.*s' is an entry point, which nothing calls", f->len, f->name);
    if (e->nargs != f->count) fail(x, e->line, e->col, "'%.*s' takes %d arguments, not %d", f->len, f->name, f->count, e->nargs);
    args a;
    eval_args(x, e, &a);
    put_name(o, f->name, f->len);
    hput(o, "(");
    for (int i = 0; i < a.n; i++) {
        if (i) hput(o, ", ");
        hput(o, a.text[i]);
    }
    hput(o, ")");
    free_args(&a);
    return f->result_type;
}

static int call(translation *x, const expr *e, hs *o)
{
    if (e->type && same(e->type->name, e->type->len, "bitcast")) return bitcast(x, e, o);
    if (!e->type) {
        fdecl *f = find_fn(x, e->name, e->len);
        if (f) return call_user(x, f, e, o);
    }
    bool made;
    const int t = construct(x, e, o, &made);
    if (made) return t;
    return builtin(x, e, o);
}

static int ex(translation *x, const expr *e, hs *o)
{
    switch (e->kind) {
    case X_INT:
        put_int(o, e->i, e->suffix == 'u');
        if (e->suffix == 'i') return t_simple(x, K_I32);
        if (e->suffix == 'u') return t_simple(x, K_U32);
        return t_simple(x, K_AINT);
    case X_FLOAT:
        if (e->suffix == 'h') fail(x, e->line, e->col, "f16 isn't taken here");
        put_float(o, e->f);
        return t_simple(x, e->suffix == 'f' ? K_F32 : K_AFLOAT);
    case X_BOOL:
        hput(o, e->i ? "true" : "false");
        return t_simple(x, K_BOOL);
    case X_NAME: return name_expr(x, e, o);
    case X_MEMBER: return member_expr(x, e, o);
    case X_INDEX: return index_expr(x, e, o);
    case X_UNARY: return unary(x, e, o);
    case X_DEREF: {
        const int t = ex(x, e->a, o);
        if (T(x, t)->kind != K_PTR) fail(x, e->line, e->col, "'*' takes a pointer");
        return T(x, t)->elem;
    }
    case X_ADDR: {
        const int t = ex(x, e->a, o);
        return t_ptr(x, SC_FUNCTION, deref_type(x, t));
    }
    case X_BINARY: {
        char *a, *b;
        const int ta = deref_type(x, ex_text(x, e->a, &a));
        const int tb = deref_type(x, ex_text(x, e->b, &b));
        int t;
        if (e->op == P_ANDAND || e->op == P_OROR) {
            hprintf(o, "(%s %s %s)", a, e->op == P_ANDAND ? "&&" : "||", b);
            t = t_simple(x, K_BOOL);
        } else {
            t = binop(x, e->op, ta, a, tb, b, o, e->line, e->col);
        }
        free(a);
        free(b);
        return t;
    }
    case X_CALL: return call(x, e, o);
    }
    fail(x, e->line, e->col, "an expression this doesn't take");
    return 0;
}

// ---------------------------------------------------------------------------
// Statements

static void indent(hs *o, const int depth)
{
    for (int i = 0; i < depth; i++) hput(o, "    ");
}

static void statement(translation *x, const stmt *s, hs *o, int depth);
static void emit_block(translation *x, const stmt *b, hs *o, int depth);

static bool stmt_derives(translation *x, const stmt *s);

static bool expr_derives(translation *x, const expr *e)
{
    if (!e) return false;
    if (e->kind == X_CALL && !e->type) {
        static const char *const implicit[] = {
            "textureSample", "textureSampleBias", "dpdx", "dpdy", "fwidth", "dpdxFine", "dpdyFine", "fwidthFine",
            "dpdxCoarse", "dpdyCoarse", "fwidthCoarse",
        };
        for (size_t i = 0; i < sizeof implicit / sizeof implicit[0]; i++) {
            if (same(e->name, e->len, implicit[i])) return true;
        }
        const fdecl *f = find_fn(x, e->name, e->len);
        if (f) {
            for (int i = 0; i < x->nderives; i++) {
                if (x->derives[i] == f) return x->derives_answer[i];
            }
            if (x->nderives < 256) {
                const int at = x->nderives++;
                x->derives[at] = f;
                x->derives_answer[at] = true; // While it's looked at: a call back into it (WGSL has none) takes them
                x->derives_answer[at] = stmt_derives(x, f->body);
                if (x->derives_answer[at]) return true;
            } else {
                return true;
            }
        }
    }
    if (expr_derives(x, e->a) || expr_derives(x, e->b)) return true;
    for (int i = 0; i < e->nargs; i++) {
        if (expr_derives(x, e->args[i])) return true;
    }
    return false;
}

static bool stmt_derives(translation *x, const stmt *s)
{
    if (!s) return false;
    if (expr_derives(x, s->e) || expr_derives(x, s->e2)) return true;
    if (stmt_derives(x, s->body) || stmt_derives(x, s->other) || stmt_derives(x, s->init) || stmt_derives(x, s->update)
        || stmt_derives(x, s->continuing)) {
        return true;
    }
    for (int i = 0; i < s->count; i++) {
        if (stmt_derives(x, s->list[i])) return true;
    }
    for (int i = 0; i < s->ncases; i++) {
        if (stmt_derives(x, s->cases[i].body)) return true;
    }
    return false;
}

// Whether a statement has a return in it anywhere; with `controlled`, inside a loop or a switch.
static bool has_return(const stmt *s, const bool controlled, const bool inside)
{
    if (!s) return false;
    if (s->kind == S_RETURN) return !controlled || inside;
    const bool in = inside || s->kind == S_FOR || s->kind == S_WHILE || s->kind == S_LOOP || s->kind == S_SWITCH;
    if (has_return(s->body, controlled, in) || has_return(s->other, controlled, inside) || has_return(s->continuing, controlled, in)) {
        return true;
    }
    for (int i = 0; i < s->count; i++) {
        if (has_return(s->list[i], controlled, inside)) return true;
    }
    for (int i = 0; i < s->ncases; i++) {
        if (has_return(s->cases[i].body, controlled, true)) return true;
    }
    return false;
}

static const control *innermost(translation *x, bool loop);

// What a return of a flagged function does after keeping its value: leaves the innermost loop or switch, and a
// loop with a continuing part by its flag, where the code after it is skipped as the flag is seen.
static void put_return_exit(translation *x, hs *o)
{
    const control *c = innermost(x, false);
    if (!c) return;
    if (c->loop && c->wrapped) hprintf(o, " tidehl_broken%d = true;", c->wrapped);
    hput(o, " break;");
}

// Whether every way through a statement returns.
static bool returns(const stmt *s)
{
    if (!s) return false;
    if (s->kind == S_RETURN) return true;
    if (s->kind == S_BLOCK) return s->count > 0 && returns(s->list[s->count - 1]);
    if (s->kind == S_IF) return s->other && returns(s->body) && returns(s->other);
    return false;
}

// A block's statements from `from` on. An `if` that returns, with no else, has the statements after it as its else:
// fxc and DXC take a return inside a branch, the code after it going on, for a value that may not be there
// ("use of potentially uninitialized variable"), which Unity shows as a warning.
static void statements_from(translation *x, stmt *const *list, const int count, const int from, hs *o, const int depth)
{
    for (int i = from; i < count; i++) {
        const stmt *s = list[i];
        if (x->flagged && s->kind != S_RETURN && has_return(s, false, false) && i + 1 < count) {
            // A flagged function's statement that may have returned: what's after it, only where it didn't
            statement(x, s, o, depth);
            indent(o, depth);
            if (innermost(x, false)) {
                hput(o, "if (tidehl_returned) {");
                put_return_exit(x, o);
                hput(o, " }\n");
                continue;
            }
            hput(o, "if (!tidehl_returned) {\n");
            const int scope = x->nscope;
            statements_from(x, list, count, i + 1, o, depth + 1);
            x->nscope = scope;
            indent(o, depth);
            hput(o, "}\n");
            return;
        }
        if (s->kind == S_IF && !s->other && returns(s->body) && i + 1 < count) {
            char *c;
            ex_text(x, s->e, &c);
            indent(o, depth);
            hprintf(o, "if (%s) ", c);
            free(c);
            emit_block(x, s->body, o, depth);
            hput(o, " else {\n");
            const int scope = x->nscope;
            statements_from(x, list, count, i + 1, o, depth + 1);
            x->nscope = scope;
            indent(o, depth);
            hput(o, "}\n");
            return;
        }
        statement(x, s, o, depth);
    }
}

static void emit_block(translation *x, const stmt *b, hs *o, const int depth)
{
    const int scope = x->nscope;
    hput(o, "{\n");
    if (b->kind == S_BLOCK) {
        statements_from(x, b->list, b->count, 0, o, depth + 1);
    } else {
        statement(x, b, o, depth + 1);
    }
    indent(o, depth);
    hput(o, "}");
    x->nscope = scope;
}

static punct compound_op(const punct p)
{
    switch (p) {
    case P_PLUSEQ: return P_PLUS;
    case P_MINUSEQ: return P_MINUS;
    case P_STAREQ: return P_STAR;
    case P_SLASHEQ: return P_SLASH;
    case P_PERCENTEQ: return P_PERCENT;
    case P_AMPEQ: return P_AMP;
    case P_PIPEEQ: return P_PIPE;
    case P_CARETEQ: return P_CARET;
    case P_SHLEQ: return P_SHL;
    case P_SHREQ: return P_SHR;
    default: return P_NONE;
    }
}

// The places in `e` an array is indexed by a number worked out as it runs, outermost last.
static int dynamic_indices(translation *x, const expr *e, const expr **found, int *counts, const int most)
{
    int n = 0;
    while (e && (e->kind == X_MEMBER || e->kind == X_INDEX || e->kind == X_DEREF)) {
        if (e->kind == X_INDEX && e->b->kind != X_INT) {
            char *text;
            const int bt = deref_type(x, ex_text(x, e->a, &text));
            free(text);
            if (T(x, bt)->kind == K_ARRAY) {
                if (n == most) fail(x, e->line, e->col, "an assignment through more than %d arrays indexed as it runs", most);
                found[n] = e;
                counts[n++] = T(x, bt)->n;
            }
        }
        e = e->a;
    }
    return n;
}

static void simple(translation *x, const stmt *s, hs *o);

// An assignment into an array by an index worked out as it runs, which fxc
// can't write: a loop over each such index's elements that writes the one it is.
static bool unrolled(translation *x, const stmt *s, hs *o)
{
    const expr *found[4];
    int counts[4];
    const int n = dynamic_indices(x, s->e, found, counts, 4);
    if (!n) return false;
    const int id = ++x->temps;
    hput(o, "{ ");
    for (int k = 0; k < n; k++) {
        char *at;
        ex_text(x, found[k]->b, &at);
        hprintf(o, "uint tidehl_k%d_%d = min((uint)(%s), %du); ", id, k, at, counts[k] - 1);
        free(at);
    }
    stmt copy = *s;
    expr value = {0};
    if (s->kind == S_ASSIGN) {
        char *v;
        const int vt = concrete_type(x, deref_type(x, ex_text(x, s->e2, &v)));
        put_type(x, o, vt);
        hprintf(o, " tidehl_v%d = %s; ", id, v);
        free(v);
        char *name = grab(&x->a, 32);
        snprintf(name, 32, "tidehl_v%d", id);
        value = (expr){.kind = X_NAME, .name = name, .len = (int)strlen(name)};
        bind(x, name, value.len, vt, name); // Its own name, as it is
        copy.e2 = &value;
    }
    char names[4][32];
    for (int k = 0; k < n; k++) {
        snprintf(names[k], sizeof names[k], "tidehl_j%d_%d", id, k);
        hprintf(o, "[unroll] for (uint %s = 0u; %s < %du; %s++) if (%s == tidehl_k%d_%d) ", names[k], names[k], counts[k],
                names[k], names[k], id, k);
        x->fixed[k] = found[k];
        x->fixed_names[k] = names[k];
    }
    x->nfixed = n;
    hs inner = {0};
    simple(x, &copy, &inner);
    x->nfixed = 0;
    hprintf(o, "%s; }", inner.s ? inner.s : "");
    free(inner.s);
    return true;
}

// A statement as a `for` has its first and third parts: no `;`.
static void simple(translation *x, const stmt *s, hs *o)
{
    if ((s->kind == S_ASSIGN || s->kind == S_INC || s->kind == S_DEC) && !x->nfixed && unrolled(x, s, o)) return;
    switch (s->kind) {
    case S_VAR: {
        char *v = NULL;
        int t = s->type ? resolve(x, s->type) : 0;
        if (s->e) {
            const int vt = ex_text(x, s->e, &v);
            if (!t) t = concrete_type(x, deref_type(x, vt));
        }
        put_type(x, o, t);
        hput(o, " ");
        put_name(o, s->name, s->len);
        if (v) {
            hprintf(o, " = %s", v);
        } else { // WGSL's start as zeros
            hput(o, " = ");
            zero_of(x, t, o);
        }
        free(v);
        bind(x, s->name, s->len, t, NULL);
        return;
    }
    case S_LET:
    case S_CONST: {
        char *v;
        const int vt = ex_text(x, s->e, &v);
        if (T(x, vt)->kind == K_PTR) { // A name for a place: wherever it's used, the place
            bind(x, s->name, s->len, vt, v);
            hput(o, "/* let */");
            return;
        }
        const int t = s->type ? resolve(x, s->type) : concrete_type(x, vt);
        put_type(x, o, t);
        hput(o, " ");
        put_name(o, s->name, s->len);
        hprintf(o, " = %s", v);
        free(v);
        bind(x, s->name, s->len, t, NULL);
        return;
    }
    case S_ASSIGN: {
        char *place, *v;
        const int pt = deref_type(x, ex_text(x, s->e, &place));
        const int vt = deref_type(x, ex_text(x, s->e2, &v));
        if (s->op == P_ASSIGN) {
            hprintf(o, "%s = %s", place, v);
        } else {
            hprintf(o, "%s = ", place);
            binop(x, compound_op(s->op), pt, place, vt, v, o, s->line, s->col);
        }
        free(place);
        free(v);
        return;
    }
    case S_INC:
    case S_DEC: {
        char *place;
        ex_text(x, s->e, &place);
        hprintf(o, "%s%s", place, s->kind == S_INC ? "++" : "--");
        free(place);
        return;
    }
    case S_CALL:
    case S_PHONY: {
        char *v;
        ex_text(x, s->e, &v);
        if (s->e->kind == X_CALL) hput(o, v);
        free(v);
        return;
    }
    default: fail(x, s->line, s->col, "that isn't a statement here");
    }
}

static const control *innermost(translation *x, const bool loop)
{
    for (int i = x->ncontrols - 1; i >= 0; i--) {
        if (!loop || x->controls[i].loop) return &x->controls[i];
    }
    return NULL;
}

static void push_control(translation *x, const bool loop, const int wrapped)
{
    if (x->ncontrols == 64) fail(x, 0, 0, "loops and switches nested too deep");
    x->controls[x->ncontrols++] = (control){.loop = loop, .wrapped = wrapped};
}

static void statement(translation *x, const stmt *s, hs *o, const int depth)
{
    switch (s->kind) {
    case S_VAR:
    case S_LET:
    case S_CONST:
    case S_ASSIGN:
    case S_INC:
    case S_DEC:
    case S_CALL:
    case S_PHONY: {
        hs line = {0};
        simple(x, s, &line);
        if (line.s && line.s[0]) {
            indent(o, depth);
            hprintf(o, "%s;\n", line.s);
        }
        free(line.s);
        return;
    }
    case S_IF: {
        indent(o, depth);
        for (const stmt *at = s;;) {
            char *c;
            ex_text(x, at->e, &c);
            hprintf(o, "if (%s) ", c);
            free(c);
            emit_block(x, at->body, o, depth);
            if (!at->other) break;
            hput(o, " else ");
            if (at->other->kind == S_IF) {
                at = at->other;
                continue;
            }
            emit_block(x, at->other, o, depth);
            break;
        }
        hput(o, "\n");
        return;
    }
    case S_FOR: {
        const int scope = x->nscope;
        indent(o, depth);
        hput(o, "{\n");
        if (s->init) {
            hs init = {0};
            simple(x, s->init, &init);
            indent(o, depth + 1);
            hprintf(o, "%s;\n", init.s ? init.s : "");
            free(init.s);
        }
        char *c = NULL;
        if (s->e) ex_text(x, s->e, &c);
        hs update = {0};
        if (s->update) simple(x, s->update, &update);
        indent(o, depth + 1);
        hprintf(o, "%sfor (; %s; %s) ", stmt_derives(x, s) ? "" : "[loop] ", c ? c : "", update.s ? update.s : "");
        free(c);
        free(update.s);
        push_control(x, true, 0);
        emit_block(x, s->body, o, depth + 1);
        x->ncontrols--;
        hput(o, "\n");
        indent(o, depth);
        hput(o, "}\n");
        x->nscope = scope;
        return;
    }
    case S_WHILE: {
        char *c;
        ex_text(x, s->e, &c);
        indent(o, depth);
        hprintf(o, "%swhile (%s) ", stmt_derives(x, s) ? "" : "[loop] ", c);
        free(c);
        push_control(x, true, 0);
        emit_block(x, s->body, o, depth);
        x->ncontrols--;
        hput(o, "\n");
        return;
    }
    case S_LOOP: {
        const int scope = x->nscope;
        if (!s->continuing) {
            indent(o, depth);
            hput(o, stmt_derives(x, s) ? "for (;;) " : "[loop] for (;;) ");
            push_control(x, true, 0);
            emit_block(x, s->body, o, depth);
            x->ncontrols--;
            hput(o, "\n");
            return;
        }
        // Its body in a do/while(false), whose break is the body's continue, and then its continuing part
        const int id = ++x->loops;
        indent(o, depth);
        hprintf(o, "{\n");
        indent(o, depth + 1);
        hprintf(o, "bool tidehl_broken%d = false;\n", id);
        indent(o, depth + 1);
        hput(o, stmt_derives(x, s) ? "for (;;) {\n" : "[loop] for (;;) {\n");
        indent(o, depth + 2);
        hput(o, "do ");
        push_control(x, true, id);
        // The body's names are the continuing part's too
        hput(o, "{\n");
        for (int i = 0; i < s->body->count; i++) statement(x, s->body->list[i], o, depth + 3);
        indent(o, depth + 2);
        hput(o, "} while (false);\n");
        x->ncontrols--;
        indent(o, depth + 2);
        hprintf(o, "if (tidehl_broken%d) break;\n", id);
        push_control(x, true, 0);
        for (int i = 0; i < s->continuing->count; i++) {
            const stmt *c = s->continuing->list[i];
            if (c->kind == S_BREAK_IF) {
                char *cond;
                ex_text(x, c->e, &cond);
                indent(o, depth + 2);
                hprintf(o, "if (%s) break;\n", cond);
                free(cond);
            } else {
                statement(x, c, o, depth + 2);
            }
        }
        x->ncontrols--;
        indent(o, depth + 1);
        hput(o, "}\n");
        indent(o, depth);
        hput(o, "}\n");
        x->nscope = scope;
        return;
    }
    case S_SWITCH: {
        char *v;
        ex_text(x, s->e, &v);
        indent(o, depth);
        hprintf(o, "switch (%s) {\n", v);
        free(v);
        push_control(x, false, 0);
        for (int i = 0; i < s->ncases; i++) {
            const scase *c = &s->cases[i];
            indent(o, depth);
            for (int k = 0; k < c->count; k++) {
                char *sel;
                ex_text(x, c->selectors[k], &sel);
                hprintf(o, "case %s: ", sel);
                free(sel);
            }
            if (c->fallback) hput(o, "default: ");
            emit_block(x, c->body, o, depth);
            hput(o, "\n");
            indent(o, depth + 1);
            hput(o, "break;\n");
        }
        x->ncontrols--;
        indent(o, depth);
        hput(o, "}\n");
        return;
    }
    case S_BREAK: {
        const control *c = innermost(x, false);
        if (!c) fail(x, s->line, s->col, "break outside a loop or a switch");
        indent(o, depth);
        if (c->loop && c->wrapped) hprintf(o, "{ tidehl_broken%d = true; break; }\n", c->wrapped);
        else hput(o, "break;\n");
        return;
    }
    case S_CONTINUE: {
        const control *c = innermost(x, true);
        if (!c) fail(x, s->line, s->col, "continue outside a loop");
        if (c->wrapped && innermost(x, false) != c) {
            fail(x, s->line, s->col, "a continue inside a switch, in a loop with a continuing part, isn't taken here");
        }
        indent(o, depth);
        hput(o, c->wrapped ? "break;\n" : "continue;\n");
        return;
    }
    case S_BREAK_IF: fail(x, s->line, s->col, "'break if' ends a loop's continuing part"); return;
    case S_RETURN: {
        indent(o, depth);
        if (x->flagged) {
            hput(o, "{");
            if (s->e) {
                char *v;
                ex_text(x, s->e, &v);
                hprintf(o, " tidehl_value = %s;", v);
                free(v);
            }
            hput(o, " tidehl_returned = true;");
            put_return_exit(x, o);
            hput(o, " }\n");
        } else if (s->e) {
            char *v;
            ex_text(x, s->e, &v);
            hprintf(o, "return %s;\n", v);
            free(v);
        } else {
            hput(o, "return;\n");
        }
        return;
    }
    case S_DISCARD:
        indent(o, depth);
        hput(o, "discard;\n");
        return;
    case S_BLOCK:
        indent(o, depth);
        emit_block(x, s, o, depth);
        hput(o, "\n");
        return;
    }
}

// ---------------------------------------------------------------------------
// Functions, globals and the entry points

static void put_signature(translation *x, fdecl *f, hs *o)
{
    signature(x, f);
    if (T(x, f->result_type)->kind == K_VOID) hput(o, "void");
    else put_type(x, o, f->result_type);
    hput(o, " ");
    put_name(o, f->name, f->len);
    hput(o, "(");
    for (int i = 0; i < f->count; i++) {
        if (i) hput(o, ", ");
        if (T(x, f->param_types[i])->kind == K_PTR) hput(o, "inout ");
        put_type(x, o, f->param_types[i]);
        hput(o, " ");
        put_name(o, f->params[i].name, f->params[i].len);
    }
    hput(o, ")");
}

static void function(translation *x, fdecl *f, hs *o)
{
    x->fn = f;
    x->nscope = 0;
    put_signature(x, f, o);
    for (int i = 0; i < f->count; i++) bind(x, f->params[i].name, f->params[i].len, f->param_types[i], NULL);
    hput(o, "\n");
    x->flagged = has_return(f->body, true, false);
    if (x->flagged) {
        // Its value kept, and returned at its end alone
        const bool gives = T(x, f->result_type)->kind != K_VOID;
        hput(o, "{\n");
        if (gives) {
            hput(o, "    ");
            put_type(x, o, f->result_type);
            hput(o, " tidehl_value = ");
            zero_of(x, f->result_type, o);
            hput(o, ";\n");
        }
        hput(o, "    bool tidehl_returned = false;\n    ");
        emit_block(x, f->body, o, 1);
        hprintf(o, "\n%s}\n\n", gives ? "    return tidehl_value;\n" : "");
    } else {
        emit_block(x, f->body, o, 0);
        hput(o, "\n\n");
    }
    x->flagged = false;
    x->fn = NULL;
}

// The cbuffer of the uniform block: its members, each where WGSL lays it out,
// with padding where HLSL would put it sooner.
static void uniform_block(translation *x, const gdecl *g, hs *o)
{
    if (g->at.group != 0 || g->at.binding != 0) fail(x, g->line, g->col, "the uniform block is at group 0, binding 0");
    const int t = resolve(x, g->type);
    if (T(x, t)->kind != K_STRUCT) fail(x, g->line, g->col, "a uniform block is a struct");
    const sdecl *s = T(x, t)->s;
    hput(o, "cbuffer TideUniforms {\n");
    uint32_t at = 0; // Where HLSL would put the next member
    int pads = 0;
    for (int i = 0; i < s->count; i++) {
        const int mt = s->member_types[i];
        const type m = *T(x, mt);
        const uint32_t want = s->offsets[i];
        uint32_t align, size;
        layout_of(x, mt, &align, &size);
        const bool packed = m.kind != K_MAT && m.kind != K_ARRAY && m.kind != K_STRUCT; // A scalar or a vector
        if (m.kind == K_STRUCT) fail(x, g->line, g->col, "a struct in the uniform block isn't taken here");
        if (m.kind == K_MAT && T(x, m.elem)->n == 2) fail(x, g->line, g->col, "a matrix of vec2 columns in the uniform block isn't taken here");
        if (want < at) fail(x, g->line, g->col, "the uniform block's member '%.*s' overlaps HLSL's", s->members[i].len, s->members[i].name);
        // Floats where HLSL would put it sooner than WGSL does: each goes where it's next
        uint32_t place;
        for (;;) {
            place = packed ? (at % 16u + size > 16u ? (at + 15u) & ~15u : at) : (at + 15u) & ~15u;
            if (place >= want) break;
            hprintf(o, "    float tidehl_pad%d;\n", pads++);
            at += 4;
        }
        if (place != want) fail(x, g->line, g->col, "the uniform block's member '%.*s' isn't where HLSL puts it", s->members[i].len, s->members[i].name);
        hput(o, "    ");
        if (m.kind == K_MAT) hput(o, "row_major ");
        put_type(x, o, mt);
        hput(o, " tidehl_u_");
        hputn(o, s->members[i].name, (size_t)s->members[i].len);
        hput(o, ";\n");
        at = place + size;
        if (!packed && i + 1 < s->count && size % 16u) {
            fail(x, g->line, g->col, "the uniform block's member after '%.*s' starts inside its last row", s->members[i].len, s->members[i].name);
        }
    }
    hput(o, "};\n");
}

// An entry point's input or output: a value with its @location or @builtin.
typedef struct io {
    const char *path; // Where it is in what the WGSL function takes or gives: "" for all of it, ".name" for a member
    int t;
    attrs at;
} io;

// What a WGSL entry point takes or gives, a struct's members each one of them.
static int ios(translation *x, const int t, const attrs at, const char *name, const int len, io *out, const int most)
{
    if (T(x, t)->kind == K_STRUCT && at.location < 0 && at.builtin < 0) {
        const sdecl *s = T(x, t)->s;
        if (s->count > most) fail(x, 0, 0, "too many inputs or outputs");
        for (int m = 0; m < s->count; m++) {
            hs path = {0};
            hputn(&path, name, (size_t)len);
            hput(&path, ".");
            put_name(&path, s->members[m].name, s->members[m].len);
            char *p = grab(&x->a, path.n + 1);
            memcpy(p, path.s, path.n + 1);
            free(path.s);
            out[m] = (io){.path = p, .t = s->member_types[m], .at = s->members[m].at};
            if (out[m].at.location < 0 && out[m].at.builtin < 0) fail(x, 0, 0, "an entry point's input or output says where it is: @location or @builtin");
        }
        return s->count;
    }
    if (most < 1) fail(x, 0, 0, "too many inputs or outputs");
    hs path = {0};
    hputn(&path, name, (size_t)len);
    char *p = grab(&x->a, path.n + 1);
    memcpy(p, path.s ? path.s : "", path.n + 1);
    free(path.s);
    out[0] = (io){.path = p, .t = t, .at = at};
    if (at.location < 0 && at.builtin < 0) fail(x, 0, 0, "an entry point's input or output says where it is: @location or @builtin");
    return 1;
}

enum { BUILTIN_POSITION = 0, BUILTIN_FRONT_FACING = 17, BUILTIN_FRAG_DEPTH = 22, BUILTIN_VERTEX_INDEX = 42, BUILTIN_INSTANCE_INDEX = 43 };

// What the vertex stage gives the fragment stage: its position, and each @location.
static void varyings(translation *x, const io *outs, const int count, hs *o)
{
    hput(o, "struct tidehl_varyings {\n    float4 position : SV_Position;\n");
    for (int i = 0; i < count; i++) {
        if (outs[i].at.location < 0) continue;
        hput(o, "    ");
        if (outs[i].at.flat || is_int(x, outs[i].t)) hput(o, "nointerpolation ");
        put_type(x, o, outs[i].t);
        hprintf(o, " l%d : TEXCOORD%d;\n", outs[i].at.location, outs[i].at.location);
    }
    hput(o, "};\n\n");
}

// A vertex attribute read from its buffer, as the declared type.
static void attribute(translation *x, const io *in, hs *o)
{
    const int t = in->t;
    const int w = width_of(x, t);
    const kind k = T(x, scalar_of(x, t))->kind;
    const char *swizzle = w == 1 ? ".x" : w == 2 ? ".xy" : w == 3 ? ".xyz" : "";
    if (k == K_F32) hprintf(o, "tidehl_float_attribute(%du, tidehl_vertex, tidehl_instance)%s", in->at.location, swizzle);
    else if (k == K_I32) hprintf(o, "tidehl_int_attribute(%du, tidehl_vertex, tidehl_instance)%s", in->at.location, swizzle);
    else if (k == K_U32) hprintf(o, "asuint(tidehl_int_attribute(%du, tidehl_vertex, tidehl_instance)%s)", in->at.location, swizzle);
    else fail(x, 0, 0, "a vertex attribute is a number or a vector of them");
}

static void entry_points(translation *x, fdecl *vertex, fdecl *fragment, hs *o)
{
    signature(x, vertex);
    signature(x, fragment);
    io outs[32];
    int nouts = 0;
    if (T(x, vertex->result_type)->kind != K_VOID) {
        nouts = ios(x, vertex->result_type, vertex->result_at, "tidehl_r", 8, outs, 32);
    }
    bool positioned = false;
    for (int i = 0; i < nouts; i++) positioned = positioned || outs[i].at.builtin == BUILTIN_POSITION;
    if (!positioned) fail(x, vertex->line, vertex->col, "the vertex stage gives a @builtin(position)");
    varyings(x, outs, nouts, o);

    // The vertex stage
    hput(o, "tidehl_varyings tidehl_vertex(uint tidehl_v : SV_VertexID, uint tidehl_instance : SV_InstanceID) {\n");
    hput(o, "    uint tidehl_vertex = tidehl_vertex_of(tidehl_v);\n");
    for (int p = 0; p < vertex->count; p++) {
        io ins[32];
        const int n = ios(x, vertex->param_types[p], vertex->params[p].at, "tidehl_p", 8, ins, 32);
        hprintf(o, "    ");
        put_type(x, o, vertex->param_types[p]);
        hprintf(o, " tidehl_p%d;\n", p);
        for (int i = 0; i < n; i++) {
            hprintf(o, "    tidehl_p%d%s = ", p, ins[i].path + 8);
            if (ins[i].at.builtin == BUILTIN_VERTEX_INDEX) hput(o, "tidehl_vertex");
            else if (ins[i].at.builtin == BUILTIN_INSTANCE_INDEX) hput(o, "tidehl_instance");
            else if (ins[i].at.location >= 0) attribute(x, &ins[i], o);
            else fail(x, vertex->line, vertex->col, "that @builtin isn't the vertex stage's");
            hput(o, ";\n");
        }
    }
    hput(o, "    ");
    put_type(x, o, vertex->result_type);
    hput(o, " tidehl_r = ");
    put_name(o, vertex->name, vertex->len);
    hput(o, "(");
    for (int p = 0; p < vertex->count; p++) hprintf(o, "%stidehl_p%d", p ? ", " : "", p);
    hput(o, ");\n    tidehl_varyings tidehl_o;\n");
    for (int i = 0; i < nouts; i++) {
        if (outs[i].at.builtin == BUILTIN_POSITION) hprintf(o, "    tidehl_o.position = tidehl_clip(%s);\n", outs[i].path);
        else if (outs[i].at.location >= 0) hprintf(o, "    tidehl_o.l%d = %s;\n", outs[i].at.location, outs[i].path);
    }
    hput(o, "    return tidehl_o;\n}\n\n");

    // The fragment stage
    io results[32];
    int nresults = 0;
    const bool gives = T(x, fragment->result_type)->kind != K_VOID;
    if (gives) nresults = ios(x, fragment->result_type, fragment->result_at, "tidehl_r", 8, results, 32);
    if (gives) {
        hput(o, "struct tidehl_targets {\n");
        for (int i = 0; i < nresults; i++) {
            hput(o, "    ");
            put_type(x, o, results[i].t);
            if (results[i].at.builtin == BUILTIN_FRAG_DEPTH) hprintf(o, " depth : SV_Depth;\n");
            else if (results[i].at.location >= 0) hprintf(o, " c%d : SV_Target%d;\n", results[i].at.location, results[i].at.location);
            else fail(x, fragment->line, fragment->col, "that @builtin isn't what the fragment stage gives");
        }
        hput(o, "};\n\n");
    }
    hprintf(o, "%s tidehl_fragment(tidehl_varyings tidehl_i, bool tidehl_front : SV_IsFrontFace)%s {\n",
            gives ? "tidehl_targets" : "void", "");
    for (int p = 0; p < fragment->count; p++) {
        io ins[32];
        const int n = ios(x, fragment->param_types[p], fragment->params[p].at, "tidehl_p", 8, ins, 32);
        hprintf(o, "    ");
        put_type(x, o, fragment->param_types[p]);
        hprintf(o, " tidehl_p%d = ", p);
        zero_of(x, fragment->param_types[p], o);
        hput(o, ";\n");
        for (int i = 0; i < n; i++) {
            hprintf(o, "    tidehl_p%d%s = ", p, ins[i].path + 8);
            if (ins[i].at.builtin == BUILTIN_POSITION) {
                hput(o, "tidehl_position(tidehl_i.position)");
            } else if (ins[i].at.builtin == BUILTIN_FRONT_FACING) {
                hput(o, "tidehl_facing(tidehl_front)");
            } else if (ins[i].at.location >= 0) {
                bool given = false;
                for (int k = 0; k < nouts; k++) given = given || outs[k].at.location == ins[i].at.location;
                if (!given) fail(x, fragment->line, fragment->col, "the fragment stage reads @location(%d), which the vertex stage doesn't give", ins[i].at.location);
                hprintf(o, "tidehl_i.l%d", ins[i].at.location);
            } else {
                fail(x, fragment->line, fragment->col, "that @builtin isn't the fragment stage's");
            }
            hput(o, ";\n");
        }
    }
    hput(o, "    ");
    if (gives) {
        put_type(x, o, fragment->result_type);
        hput(o, " tidehl_r = ");
    }
    put_name(o, fragment->name, fragment->len);
    hput(o, "(");
    for (int p = 0; p < fragment->count; p++) hprintf(o, "%stidehl_p%d", p ? ", " : "", p);
    hput(o, ");\n");
    if (gives) {
        hput(o, "    tidehl_targets tidehl_o;\n");
        for (int i = 0; i < nresults; i++) {
            if (results[i].at.builtin == BUILTIN_FRAG_DEPTH) hprintf(o, "    tidehl_o.depth = tidehl_depth_out(%s);\n", results[i].path);
            else if (results[i].at.location == 0 && results[i].t == t_vec(x, t_simple(x, K_F32), 4)) hprintf(o, "    tidehl_o.c0 = tidehl_output(%s);\n", results[i].path);
            else hprintf(o, "    tidehl_o.c%d = %s;\n", results[i].at.location, results[i].path);
        }
        hput(o, "    return tidehl_o;\n");
    }
    hput(o, "}\n");
}

// What every program has: Unity's platforms' depth, the names TideRender.cs
// sets, the samplers, and the functions the rest calls.
static const char *const prelude[] = {
    "// Made by tidec from WGSL (compiler/src/hlsl.c). Do not edit.\n"

    "#if defined(SHADER_API_D3D11) || defined(SHADER_API_PSSL) || defined(SHADER_API_METAL) || defined(SHADER_API_VULKAN) \\\n"
    "    || defined(SHADER_API_SWITCH) || defined(SHADER_API_XBOXONE) || defined(SHADER_API_GAMECORE) || defined(SHADER_API_WEBGPU)\n"
    "#define TIDEHL_REVERSED_Z 1 // Unity's depth is 1 near and 0 far\n"
    "#else\n"
    "#define TIDEHL_REVERSED_Z 0 // OpenGL's: clip depth from -w near to w far\n"
    "#endif\n"
    "#if defined(SHADER_API_GLCORE) || defined(SHADER_API_GLES3) || defined(SHADER_API_GLES) || defined(SHADER_API_WEBGL)\n"
    "#define TIDEHL_OWN_SAMPLERS 1 // Unity's OpenGL has no samplers apart from textures: each texture's own\n"
    "#else\n"
    "#define TIDEHL_OWN_SAMPLERS 0\n"
    "#endif\n"
    "#if defined(SHADER_API_GLES3) || defined(SHADER_API_GLES) || defined(SHADER_API_WEBGL)\n"
    "#define TIDEHL_TEXTURE_BUFFERS 1 // OpenGL ES 3.0's and WebGL 2's: buffers as textures, a word a pixel's bytes, 4,096 a row\n"
    "#else\n"
    "#define TIDEHL_TEXTURE_BUFFERS 0\n"
    "#endif\n"
    "float4 _ProjectionParams;\n"
    "float _TideFlip, _TideHeight, _TideLinear;\n"
    "float4 _TideAttributes[16];\n"
    "float4 _TideSlots[3];\n"
    "uint _TideOffset0, _TideOffset1, _TideOffset2, _TideFirst, _TideSampling, _TideSamplingHigh;\n"
    "#if TIDEHL_TEXTURE_BUFFERS\n"
    "Texture2D<float4> _TideVertices0, _TideVertices1, _TideVertices2;\n"
    "#else\n"
    "ByteAddressBuffer _TideVertices0, _TideVertices1, _TideVertices2;\n"
    "#endif\n"
    "SamplerState tidehl_point_clamp_sampler, tidehl_point_repeat_sampler, tidehl_point_mirror_sampler;\n"
    "SamplerState tidehl_linear_clamp_sampler, tidehl_linear_repeat_sampler, tidehl_linear_mirror_sampler;\n"
    "SamplerState tidehl_trilinear_clamp_sampler, tidehl_trilinear_repeat_sampler, tidehl_trilinear_mirror_sampler;\n"
    "\n",
    "// Which way up the target is: -1 where Unity keeps its rows from the bottom\n"
    "float tidehl_flip() { return _TideFlip != 0.0 ? sign(_TideFlip) : _ProjectionParams.x; }\n"
    "float4 tidehl_clip(float4 p) {\n"
    "    p.y *= tidehl_flip();\n"
    "#if TIDEHL_REVERSED_Z\n"
    "    p.z = p.w - p.z;\n"
    "#else\n"
    "    p.z = 2.0 * p.z - p.w;\n"
    "#endif\n"
    "    return p;\n"
    "}\n"
    "float tidehl_depth_in(float d) { return TIDEHL_REVERSED_Z ? 1.0 - d : d; }\n"
    "float tidehl_depth_out(float d) { return TIDEHL_REVERSED_Z ? 1.0 - d : d; }\n"
    "float4 tidehl_position(float4 p) {\n"
    "    if (tidehl_flip() < 0.0) p.y = _TideHeight - p.y;\n"
    "    p.z = tidehl_depth_in(p.z);\n"
    "    p.w = 1.0 / p.w;\n"
    "    return p;\n"
    "}\n"
    "bool tidehl_facing(bool front) { return tidehl_flip() < 0.0 ? !front : front; }\n"
    "// A color into Unity's own target, which takes linear colors where its project is in linear color space\n"
    "float4 tidehl_output(float4 c) {\n"
    "    if (_TideLinear > 0.5 && c.a > 0.0) {\n"
    "        float3 s = saturate(c.rgb / c.a);\n"
    "        c.rgb = (s <= 0.04045 ? s / 12.92 : pow((s + 0.055) / 1.055, 2.4)) * c.a;\n"
    "    }\n"
    "    return c;\n"
    "}\n"
    "\n",
    "// Vertices, from their buffers\n"
    "uint tidehl_vertex_of(uint v) { return _TideFirst + v; } // An indexed draw's first is 0: SV_VertexID is the index\n"
    "#if TIDEHL_TEXTURE_BUFFERS\n"
    "uint tidehl_word(Texture2D<float4> b, uint at) {\n"
    "    uint w = at >> 2;\n"
    "    uint4 bytes = uint4(round(b.Load(int3(int(w & 4095u), int(w >> 12), 0)) * 255.0));\n"
    "    return bytes.x | bytes.y << 8 | bytes.z << 16 | bytes.w << 24;\n"
    "}\n"
    "uint4 tidehl_words(Texture2D<float4> b, uint at, uint n) {\n"
    "    uint4 r = uint4(tidehl_word(b, at), 0u, 0u, 0u);\n"
    "    if (n > 1u) r.y = tidehl_word(b, at + 4u);\n"
    "    if (n > 2u) r.z = tidehl_word(b, at + 8u);\n"
    "    if (n > 3u) r.w = tidehl_word(b, at + 12u);\n"
    "    return r;\n"
    "}\n"
    "#else\n"
    "uint4 tidehl_words(ByteAddressBuffer b, uint at, uint n) {\n"
    "    uint4 r;\n"
    "    if (n == 1u) r = uint4(b.Load(at), 0u, 0u, 0u);\n"
    "    else if (n == 2u) r = uint4(b.Load2(at), 0u, 0u);\n"
    "    else if (n == 3u) r = uint4(b.Load3(at), 0u);\n"
    "    else r = b.Load4(at);\n"
    "    return r;\n"
    "}\n"
    "#endif\n"
    "uint4 tidehl_raw(uint location, uint vertex, uint instance, out uint format) {\n"
    "    float4 a = _TideAttributes[location];\n"
    "    uint slot = min((uint)a.x, 2u);\n"
    "    format = (uint)a.z;\n"
    "    float4 s = _TideSlots[slot];\n"
    "    uint at = (s.y > 0.5 ? instance : vertex) * (uint)s.x + (uint)a.y;\n"
    "    uint n = format == 4u ? 1u : format < 4u ? format + 1u : format - 4u;\n"
    "    uint4 r;\n"
    "    if (slot == 0u) r = tidehl_words(_TideVertices0, at + _TideOffset0, n);\n"
    "    else if (slot == 1u) r = tidehl_words(_TideVertices1, at + _TideOffset1, n);\n"
    "    else r = tidehl_words(_TideVertices2, at + _TideOffset2, n);\n"
    "    return r;\n"
    "}\n"
    "float4 tidehl_float_attribute(uint location, uint vertex, uint instance) {\n"
    "    uint format;\n"
    "    uint4 r = tidehl_raw(location, vertex, instance, format);\n"
    "    float4 f = asfloat(r);\n"
    "    if (format == 4u) f = float4(r.x & 255u, (r.x >> 8) & 255u, (r.x >> 16) & 255u, r.x >> 24) / 255.0;\n"
    "    else if (format == 0u) f = float4(f.x, 0.0, 0.0, 1.0);\n"
    "    else if (format == 1u) f = float4(f.xy, 0.0, 1.0);\n"
    "    else if (format == 2u) f = float4(f.xyz, 1.0);\n"
    "    return f;\n"
    "}\n"
    "int4 tidehl_int_attribute(uint location, uint vertex, uint instance) {\n"
    "    uint format;\n"
    "    int4 r = asint(tidehl_raw(location, vertex, instance, format));\n"
    "    if (format == 5u) r = int4(r.x, 0, 0, 1);\n"
    "    else if (format == 6u) r = int4(r.xy, 0, 1);\n"
    "    else if (format == 7u) r = int4(r.xyz, 1);\n"
    "    return r;\n"
    "}\n"
    "\n",
    "// Sampling, as `mode` says (_TideSampling): nearest and linear read a texture's own pixels, mipmaps its smaller ones too\n"
    "#if TIDEHL_OWN_SAMPLERS\n"
    "#define TIDEHL_SAMPLE(T, C)                                                                            \\\n"
    "float4 tidehl_sample_grad(T t, SamplerState own, uint mode, C at, C dx, C dy) {                        \\\n"
    "    float4 r;                                                                                          \\\n"
    "    if ((mode & 3u) == 2u) r = t.SampleGrad(own, at, dx, dy);                                          \\\n"
    "    else r = t.SampleLevel(own, at, 0.0);                                                              \\\n"
    "    return r;                                                                                          \\\n"
    "}                                                                                                      \\\n"
    "float4 tidehl_sample_level(T t, SamplerState own, uint mode, C at, float level) {                      \\\n"
    "    return t.SampleLevel(own, at, (mode & 3u) == 2u ? level : 0.0);                                    \\\n"
    "}\n"
    "#else\n"
    "#define TIDEHL_SAMPLE(T, C)                                                                            \\\n"
    "float4 tidehl_sample_grad(T t, SamplerState own, uint mode, C at, C dx, C dy) {                        \\\n"
    "    uint filter = mode & 3u, wrap = (mode >> 2) & 3u;                                                  \\\n"
    "    float4 r;                                                                                          \\\n"
    "    if (filter == 2u) {                                                                                \\\n"
    "        if (wrap == 1u) r = t.SampleGrad(tidehl_trilinear_repeat_sampler, at, dx, dy);                \\\n"
    "        else if (wrap == 2u) r = t.SampleGrad(tidehl_trilinear_mirror_sampler, at, dx, dy);           \\\n"
    "        else r = t.SampleGrad(tidehl_trilinear_clamp_sampler, at, dx, dy);                            \\\n"
    "    } else if (filter == 1u) {                                                                         \\\n"
    "        if (wrap == 1u) r = t.SampleLevel(tidehl_linear_repeat_sampler, at, 0.0);                     \\\n"
    "        else if (wrap == 2u) r = t.SampleLevel(tidehl_linear_mirror_sampler, at, 0.0);                \\\n"
    "        else r = t.SampleLevel(tidehl_linear_clamp_sampler, at, 0.0);                                 \\\n"
    "    } else {                                                                                           \\\n"
    "        if (wrap == 1u) r = t.SampleLevel(tidehl_point_repeat_sampler, at, 0.0);                      \\\n"
    "        else if (wrap == 2u) r = t.SampleLevel(tidehl_point_mirror_sampler, at, 0.0);                 \\\n"
    "        else r = t.SampleLevel(tidehl_point_clamp_sampler, at, 0.0);                                  \\\n"
    "    }                                                                                                  \\\n"
    "    return r;                                                                                          \\\n"
    "}                                                                                                      \\\n"
    "float4 tidehl_sample_level(T t, SamplerState own, uint mode, C at, float level) {                      \\\n"
    "    uint filter = mode & 3u, wrap = (mode >> 2) & 3u;                                                  \\\n"
    "    float4 r;                                                                                          \\\n"
    "    if (filter != 2u) r = tidehl_sample_grad(t, own, mode, at, (C)0, (C)0);                            \\\n"
    "    else if (wrap == 1u) r = t.SampleLevel(tidehl_trilinear_repeat_sampler, at, level);               \\\n"
    "    else if (wrap == 2u) r = t.SampleLevel(tidehl_trilinear_mirror_sampler, at, level);               \\\n"
    "    else r = t.SampleLevel(tidehl_trilinear_clamp_sampler, at, level);                                \\\n"
    "    return r;                                                                                          \\\n"
    "}\n"
    "#endif\n"
    ,
    "#define TIDEHL_SAMPLE_DERIVED(T, C)                                                                    \\\n"
    "float4 tidehl_sample(T t, SamplerState own, uint mode, C at) { return tidehl_sample_grad(t, own, mode, at, ddx(at), ddy(at)); } \\\n"
    "float4 tidehl_sample_bias(T t, SamplerState own, uint mode, C at, float bias) {                        \\\n"
    "    float scale = exp2(bias);                                                                          \\\n"
    "    return tidehl_sample_grad(t, own, mode, at, ddx(at) * scale, ddy(at) * scale);                     \\\n"
    "}\n"
    "TIDEHL_SAMPLE(Texture2D<float4>, float2)\n"
    "TIDEHL_SAMPLE(TextureCube<float4>, float3)\n"
    "TIDEHL_SAMPLE_DERIVED(Texture2D<float4>, float2)\n"
    "TIDEHL_SAMPLE_DERIVED(TextureCube<float4>, float3)\n"
    "\n",
    "// Depth, 0 near and 1 far whichever way Unity keeps it: the nearest pixel's, or 1 where it isn't nearer than `d`\n"
    "#if TIDEHL_OWN_SAMPLERS\n"
    "#define TIDEHL_NEAREST(own) own // A depth texture's own sampler is the nearest pixel's (TideRender.cs)\n"
    "#else\n"
    "#define TIDEHL_NEAREST(own) tidehl_point_clamp_sampler\n"
    "#endif\n"
    "float tidehl_depth(Texture2D<float> t, SamplerState own, float2 at) { return tidehl_depth_in(t.SampleLevel(TIDEHL_NEAREST(own), at, 0.0)); }\n"
    "float tidehl_depth(TextureCube<float> t, SamplerState own, float3 at) { return tidehl_depth_in(t.SampleLevel(TIDEHL_NEAREST(own), at, 0.0)); }\n"
    "float tidehl_compare(Texture2D<float> t, SamplerState own, float2 at, float d) {\n"
    "    uint w, h;\n"
    "    t.GetDimensions(w, h);\n"
    "    float2 size = float2(w, h);\n"
    "    float2 p = at * size - 0.5;\n"
    "    float2 f = frac(p);\n"
    "#if TIDEHL_TEXTURE_BUFFERS\n"
    "    // OpenGL ES 3.0 gathers nothing: the four pixels, each loaded\n"
    "    int2 i = int2(floor(p)), most = int2(w, h) - 1;\n"
    "    float4 g = float4(t.Load(int3(clamp(i + int2(0, 1), 0, most), 0)), t.Load(int3(clamp(i + int2(1, 1), 0, most), 0)),\n"
    "                      t.Load(int3(clamp(i + int2(1, 0), 0, most), 0)), t.Load(int3(clamp(i, 0, most), 0)));\n"
    "#else\n"
    "    float4 g = t.GatherRed(TIDEHL_NEAREST(own), (floor(p) + 1.0) / size);\n"
    "#endif\n"
    "#if TIDEHL_REVERSED_Z\n"
    "    g = 1.0 - g;\n"
    "#endif\n"
    "    float4 lit = step(d.xxxx, g); // Gathered as (0, 1), (1, 1), (1, 0), (0, 0)\n"
    "    return lerp(lerp(lit.w, lit.z, f.x), lerp(lit.x, lit.y, f.x), f.y);\n"
    "}\n"
    "float tidehl_compare(TextureCube<float> t, SamplerState own, float3 at, float d) { return d <= tidehl_depth(t, own, at) ? 1.0 : 0.0; }\n"
    "\n",
    "// Loads and sizes\n"
    "float4 tidehl_load(Texture2D<float4> t, int2 at, int level) { return t.Load(int3(at, level)); }\n"
    "int4 tidehl_load(Texture2D<int4> t, int2 at, int level) { return t.Load(int3(at, level)); }\n"
    "uint4 tidehl_load(Texture2D<uint4> t, int2 at, int level) { return t.Load(int3(at, level)); }\n"
    "float tidehl_load(Texture2D<float> t, int2 at, int level) { return tidehl_depth_in(t.Load(int3(at, level))); }\n"
    "float4 tidehl_load(Texture2DMS<float4> t, int2 at, int s) { return t.Load(at, s); }\n"
    "float tidehl_load(Texture2DMS<float> t, int2 at, int s) { return tidehl_depth_in(t.Load(at, s)); }\n"
    "#define TIDEHL_SIZE(T)                                                                                 \\\n"
    "uint2 tidehl_size(T t, uint level) { uint w, h, n; t.GetDimensions(level, w, h, n); return uint2(w, h); } \\\n"
    "uint tidehl_levels(T t) { uint w, h, n; t.GetDimensions(0, w, h, n); return n; }\n"
    "TIDEHL_SIZE(Texture2D<float4>)\n"
    "TIDEHL_SIZE(Texture2D<int4>)\n"
    "TIDEHL_SIZE(Texture2D<uint4>)\n"
    "TIDEHL_SIZE(Texture2D<float>)\n"
    "TIDEHL_SIZE(TextureCube<float4>)\n"
    "TIDEHL_SIZE(TextureCube<float>)\n"
    "#define TIDEHL_SIZE_MS(T)                                                                              \\\n"
    "uint2 tidehl_size(T t) { uint w, h, n; t.GetDimensions(w, h, n); return uint2(w, h); }                \\\n"
    "uint tidehl_samples(T t) { uint w, h, n; t.GetDimensions(w, h, n); return n; }\n"
    "TIDEHL_SIZE_MS(Texture2DMS<float4>)\n"
    "TIDEHL_SIZE_MS(Texture2DMS<float>)\n"
    "\n",
    "// Numbers as WebGPU has them: division and remainder by zero, or the least int by -1, as by 1; floats to ints clamped\n"
    "#define TIDEHL_INTS(T, U, F)                                                                           \\\n"
    "T tidehl_divisor(T a, T b) { return (b == (T)0) || ((a == (T)(-2147483647 - 1)) && (b == (T)(-1))) ? (T)1 : b; } \\\n"
    "T tidehl_div(T a, T b) {                                                                               \\\n"
    "    T d = tidehl_divisor(a, b);                                                                        \\\n"
    "    U q = asuint(a < (T)0 ? -a : a) / asuint(d < (T)0 ? -d : d);                                       \\\n"
    "    return ((a < (T)0) != (d < (T)0)) ? -asint(q) : asint(q);                                          \\\n"
    "}                                                                                                      \\\n"
    "T tidehl_rem(T a, T b) {                                                                               \\\n"
    "    T d = tidehl_divisor(a, b);                                                                        \\\n"
    "    U r = asuint(a < (T)0 ? -a : a) % asuint(d < (T)0 ? -d : d);                                       \\\n"
    "    return a < (T)0 ? -asint(r) : asint(r);                                                            \\\n"
    "}                                                                                                      \\\n"
    "U tidehl_div(U a, U b) { return a / (b == (U)0 ? (U)1 : b); }                                         \\\n"
    "U tidehl_rem(U a, U b) { return a % (b == (U)0 ? (U)1 : b); }                                         \\\n"
    "T tidehl_f2i(F f) { return (T)clamp(f, -2147483648.0, 2147483520.0); }                               \\\n"
    "U tidehl_f2u(F f) { return (U)clamp(f, 0.0, 4294967040.0); }\n"
    "TIDEHL_INTS(int, uint, float)\n"
    "TIDEHL_INTS(int2, uint2, float2)\n"
    "TIDEHL_INTS(int3, uint3, float3)\n"
    "TIDEHL_INTS(int4, uint4, float4)\n",
    "uint tidehl_extract(uint e, uint offset, uint count) {\n"
    "    uint o = min(offset, 32u), n = min(count, 32u - o);\n"
    "    return n == 0u ? 0u : (e >> o) & (n == 32u ? 0xFFFFFFFFu : (1u << n) - 1u);\n"
    "}\n"
    "int tidehl_extract_signed(int e, uint offset, uint count) {\n"
    "    uint o = min(offset, 32u), n = min(count, 32u - o);\n"
    "    return n == 0u ? 0 : asint(asuint(e) << (32u - o - n)) >> (32u - n);\n"
    "}\n"
    "uint tidehl_insert(uint e, uint v, uint offset, uint count) {\n"
    "    uint o = min(offset, 32u), n = min(count, 32u - o);\n"
    "    if (n == 0u) return e;\n"
    "    uint mask = (n == 32u ? 0xFFFFFFFFu : (1u << n) - 1u) << o;\n"
    "    return (e & ~mask) | ((v << o) & mask);\n"
    "}\n"
    "#define TIDEHL_HYPERBOLIC(T)                                                                           \\\n"
    "T tidehl_pow(T x, T y) { return exp2(log2(x) * y); }                                                   \\\n"
    "T tidehl_asinh(T v) { return log(v + sqrt(v * v + 1.0)); }                                           \\\n"
    "T tidehl_acosh(T v) { return log(v + sqrt(v * v - 1.0)); }                                           \\\n"
    "T tidehl_atanh(T v) { return 0.5 * log((1.0 + v) / (1.0 - v)); }                                     \\\n"
    "T tidehl_fwidth_fine(T v) { return abs(ddx_fine(v)) + abs(ddy_fine(v)); }                             \\\n"
    "T tidehl_fwidth_coarse(T v) { return abs(ddx_coarse(v)) + abs(ddy_coarse(v)); }\n"
    "TIDEHL_HYPERBOLIC(float)\n"
    "TIDEHL_HYPERBOLIC(float2)\n"
    "TIDEHL_HYPERBOLIC(float3)\n"
    "TIDEHL_HYPERBOLIC(float4)\n"
    "uint tidehl_pack4x8unorm(float4 v) { uint4 b = (uint4)round(saturate(v) * 255.0); return b.x | b.y << 8 | b.z << 16 | b.w << 24; }\n"
    "uint tidehl_pack4x8snorm(float4 v) { uint4 b = asuint((int4)round(clamp(v, -1.0, 1.0) * 127.0)) & 255u; return b.x | b.y << 8 | b.z << 16 | b.w << 24; }\n"
    "uint tidehl_pack2x16unorm(float2 v) { uint2 b = (uint2)round(saturate(v) * 65535.0); return b.x | b.y << 16; }\n"
    "uint tidehl_pack2x16snorm(float2 v) { uint2 b = asuint((int2)round(clamp(v, -1.0, 1.0) * 32767.0)) & 0xFFFFu; return b.x | b.y << 16; }\n"
    "uint tidehl_pack2x16float(float2 v) { return f32tof16(v.x) | f32tof16(v.y) << 16; }\n"
    "float4 tidehl_unpack4x8unorm(uint u) { return float4(u & 255u, (u >> 8) & 255u, (u >> 16) & 255u, u >> 24) / 255.0; }\n"
    "float4 tidehl_unpack4x8snorm(uint u) { int4 b = asint(uint4(u << 24, u << 16, u << 8, u)) >> 24; return max(float4(b) / 127.0, -1.0); }\n"
    "float2 tidehl_unpack2x16unorm(uint u) { return float2(u & 0xFFFFu, u >> 16) / 65535.0; }\n"
    "float2 tidehl_unpack2x16snorm(uint u) { int2 b = asint(uint2(u << 16, u)) >> 16; return max(float2(b) / 32767.0, -1.0); }\n"
    "float2 tidehl_unpack2x16float(uint u) { return float2(f16tof32(u & 0xFFFFu), f16tof32(u >> 16)); }\n"
    "\n",
    NULL,
};

static void translate(translation *x, hs *out)
{
    add_type(x, (type){.kind = K_NONE}); // Type 0 is none
    tokenize(x);
    parse_module(x);
    fdecl *vertex = NULL, *fragment = NULL;
    for (int i = 0; i < x->nfns; i++) {
        fdecl *f = x->fns[i];
        if (f->at.stage == 1) {
            if (vertex) fail(x, f->line, f->col, "a second vertex stage");
            vertex = f;
        } else if (f->at.stage == 2) {
            if (fragment) fail(x, f->line, f->col, "a second fragment stage");
            fragment = f;
        }
    }
    if (!vertex || !fragment) fail(x, 1, 1, "a module here has a @vertex and a @fragment function");
    // The module's structs, each after what it holds
    for (int i = 0; i < x->nstructs; i++) declare(x, resolve_struct(x, x->structs[i], 0, 0));
    // Its globals: the uniform block, textures (named by their slots), constants and private variables
    hs globals = {0};
    for (int i = 0; i < x->nglobals; i++) {
        const gdecl *g = x->globals[i];
        if (g->is_const) {
            if (!g->init) fail(x, g->line, g->col, "'%.*s' needs a value", g->len, g->name);
            const int t = global_type(x, g);
            char *v;
            ex_text(x, g->init, &v);
            hput(&globals, "static const ");
            put_type(x, &globals, t);
            hput(&globals, " ");
            put_name(&globals, g->name, g->len);
            hprintf(&globals, " = %s;\n", v);
            free(v);
            continue;
        }
        const int t = g->type ? resolve(x, g->type) : 0;
        if (!t) fail(x, g->line, g->col, "'%.*s' needs a type", g->len, g->name);
        if (g->space == SPACE_UNIFORM) {
            if (x->uniform) fail(x, g->line, g->col, "a second uniform block");
            x->uniform = g;
            uniform_block(x, g, &globals);
        } else if (g->space == SPACE_PRIVATE) {
            hput(&globals, "static ");
            put_type(x, &globals, t);
            hput(&globals, " ");
            put_name(&globals, g->name, g->len);
            hput(&globals, " = ");
            if (g->init) {
                char *v;
                ex_text(x, g->init, &v);
                hput(&globals, v);
                free(v);
            } else {
                zero_of(x, t, &globals);
            }
            hput(&globals, ";\n");
        } else if (T(x, t)->kind == K_TEXTURE) {
            put_type(x, &globals, t);
            hprintf(&globals, " _TideTexture%d;\nSamplerState sampler_TideTexture%d;\n", handle_slot(x, g, false), handle_slot(x, g, false));
        } else if (T(x, t)->kind == K_SAMPLER || T(x, t)->kind == K_SAMPLER_CMP) {
            handle_slot(x, g, true); // Unity's inline samplers, picked by _TideSampling
        } else {
            fail(x, g->line, g->col, "a module's variable here is a texture, a sampler, var<uniform> or var<private>");
        }
    }
    // Its functions, each declared first, so that any calls any
    hs prototypes = {0}, functions = {0};
    for (int i = 0; i < x->nfns; i++) {
        put_signature(x, x->fns[i], &prototypes);
        hput(&prototypes, ";\n");
    }
    for (int i = 0; i < x->nfns; i++) function(x, x->fns[i], &functions);
    hs entries = {0};
    entry_points(x, vertex, fragment, &entries);
    for (int i = 0; prelude[i]; i++) hput(out, prelude[i]);
    if (x->decls.s) hput(out, x->decls.s);
    hput(out, "\n");
    if (globals.s) hput(out, globals.s);
    hput(out, "\n");
    if (x->helpers.s) hput(out, x->helpers.s);
    if (prototypes.s) hput(out, prototypes.s);
    hput(out, "\n");
    if (functions.s) hput(out, functions.s);
    if (entries.s) hput(out, entries.s);
    free(globals.s);
    free(prototypes.s);
    free(functions.s);
    free(entries.s);
}

char *tide_wgsl_to_hlsl(const char *wgsl, char *error, const size_t error_size)
{
    translation *x = calloc(1, sizeof(translation));
    if (!x) abort();
    x->text = wgsl;
    x->error = error;
    x->error_size = error_size;
    if (error && error_size) error[0] = '\0';
    hs out = {0};
    char *result = NULL;
    if (!setjmp(x->failed)) {
        translate(x, &out);
        result = htake(&out);
    }
    free(out.s);
    free(x->toks);
    free(x->structs);
    free(x->globals);
    free(x->fns);
    free(x->types);
    free(x->decls.s);
    free(x->helpers.s);
    free(x->made);
    free(x->declared);
    free(x->scope);
    release(&x->a);
    free(x);
    return result;
}

char *tide_hlsl_unity_shader(const char *hlsl, const char *name)
{
    hs o = {0};
    hprintf(&o,
            "// Made by tide unity from the game's WGSL. Do not edit: it's made again as the game is built.\n"
            "Shader \"%s\"\n"
            "{\n"
            "    Properties\n"
            "    {\n"
            "        _TideSrcColor (\"\", Float) = 1\n"
            "        _TideDstColor (\"\", Float) = 10\n"
            "        _TideSrcAlpha (\"\", Float) = 1\n"
            "        _TideDstAlpha (\"\", Float) = 10\n"
            "        _TideZTest (\"\", Float) = 8\n"
            "        _TideZWrite (\"\", Float) = 0\n"
            "        _TideCull (\"\", Float) = 0\n"
            "        _TideStencilRef (\"\", Float) = 0\n"
            "        _TideStencilComp (\"\", Float) = 8\n"
            "        _TideStencilPass (\"\", Float) = 0\n"
            "        _TideAlphaToMask (\"\", Float) = 0\n"
            "    }\n",
            name);
    // Twice: for GPUs whose shaders read buffers, and for OpenGL ES 3.0 and WebGL 2, which read them as textures
    // (hlsl.h's TIDEHL_TEXTURE_BUFFERS)
    for (int i = 0; i < 2; i++) {
        hput(&o, "    SubShader\n"
                 "    {\n"
                 "        Pass\n"
                 "        {\n"
                 "            Blend [_TideSrcColor] [_TideDstColor], [_TideSrcAlpha] [_TideDstAlpha]\n"
                 "            ZTest [_TideZTest]\n"
                 "            ZWrite [_TideZWrite]\n"
                 "            Cull [_TideCull]\n"
                 "            AlphaToMask [_TideAlphaToMask]\n"
                 "            Stencil\n"
                 "            {\n"
                 "                Ref [_TideStencilRef]\n"
                 "                Comp [_TideStencilComp]\n"
                 "                Pass [_TideStencilPass]\n"
                 "            }\n"
                 "            HLSLPROGRAM\n"
                 "            #pragma vertex tidehl_vertex\n"
                 "            #pragma fragment tidehl_fragment\n");
        hput(&o, i == 0 ? "            #pragma target 4.5\n            #pragma exclude_renderers gles3\n"
                        : "            #pragma target 3.5\n            #pragma only_renderers gles3\n");
        hput(&o, "            #include \"Packages/com.tide.unity/Runtime/Shaders/TideWarnings.hlsl\"\n");
        hput(&o, hlsl);
        hput(&o, "            ENDHLSL\n        }\n    }\n");
    }
    hput(&o, "}\n");
    return htake(&o);
}
