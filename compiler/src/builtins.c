#include "builtins.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "types.h"

// Every built-in math function maps to a tide/math.h function named
// tide_<lowercase name>_<type suffix>, for example Math.Dot on float3 is
// tide_dot_f3. Formulas and semantics follow Unity.Mathematics. Draw functions
// map to tide/draw.h, and GUI and GUILayout to tide/gui.h.

static const type T_F = {TY_FLOAT, NULL};
static const type T_WHOLE = {TY_INT, NULL};
static const type T_F2 = {TY_FLOAT2, NULL};
static const type T_F3 = {TY_FLOAT3, NULL};
static const type T_COLOR = {TY_COLOR, NULL};
static const type T_STR = {TY_STRING, NULL};
static const type T_CHR = {TY_CHAR, NULL};
static const type T_Q = {TY_QUATERNION, NULL};
static const type T_F4X4 = {TY_FLOAT4X4, NULL};
static const type T_F3X3 = {TY_FLOAT3X3, NULL};
static const type T_NONE = {TY_VOID, NULL};
static const type T_BOOL = {TY_BOOL, NULL};

// The Anchor enum in signatures. The table outlives programs, so it can't
// hold the enum's declaration: calls match it against the program's own.
static const type T_ANCHOR = {TY_ENUM, NULL};
static const decl *anchor_decl;

// The Shape struct, which Draw.Shape takes, the same way.
static decl shape_struct;
static const type T_SHAPE = {TY_STRUCT, &shape_struct};
static const decl *shape_decl;

// ...and the Blend enum, which Draw.Blend does.
static decl blend_enum;
static const type T_BLEND = {TY_ENUM, &blend_enum};
static const decl *blend_decl;
// ...and the Depth enum, which Draw.Depth does.
static decl depth_enum;
static const type T_DEPTH = {TY_ENUM, &depth_enum};
static const decl *depth_decl;
// ...and the Cull enum, which Draw.Cull does.
static decl cull_enum;
static const type T_CULL = {TY_ENUM, &cull_enum};
static const decl *cull_decl;
// And the Font enum, which Draw.Text and Draw.MeasureText take.
static decl text_font;
static const type T_FONT = {TY_ENUM, &text_font};
static const decl *font_decl;
// ...and the Format enum, what a render target is made to hold.
static decl format_enum;
static const type T_FORMAT = {TY_ENUM, &format_enum};
static const decl *format_decl;

void builtins_use_format(const decl *format)
{
    format_decl = format;
}

// ...and the StencilTest and StencilOp enums, which Draw.Stencil takes.
static decl stencil_test_enum, stencil_op_enum;
static const type T_STENCIL_TEST = {TY_ENUM, &stencil_test_enum};
static const type T_STENCIL_OP = {TY_ENUM, &stencil_op_enum};
static const decl *stencil_test_decl, *stencil_op_decl;

void builtins_use_stencil(const decl *test, const decl *op)
{
    stencil_test_decl = test;
    stencil_op_decl = op;
}

// The GUITheme and GUIControl structs, which the GUI takes and gives, the same way.
static decl theme_struct, control_struct;
static const type T_THEME = {TY_STRUCT, &theme_struct};
static const type T_CONTROL = {TY_STRUCT, &control_struct};
static const decl *theme_decl, *control_decl;

void builtins_use_gui(const decl *theme, const decl *control)
{
    theme_decl = theme;
    control_decl = control;
}

static const decl *vertices_decl, *vertices3_decl, *indices_decl, *pixels_decl, *filter_decl, *wrap_decl, *transforms_decl;
static const decl *bytes_decl, *paths_decl;
static decl io_bytes, io_paths;
static const type T_BYTES = {TY_LIST, &io_bytes};
static const type T_PATHS = {TY_LIST, &io_paths};

void builtins_use_io(const decl *bytes, const decl *paths)
{
    bytes_decl = bytes;
    paths_decl = paths;
}

void builtins_use(const decl *anchor, const decl *shape, const decl *blend, const decl *depth, const decl *cull,
                  const decl *font)
{
    anchor_decl = anchor;
    shape_decl = shape;
    blend_decl = blend;
    depth_decl = depth;
    cull_decl = cull;
    font_decl = font;
    // Another program's: this one's come with its first mesh draw
    vertices_decl = vertices3_decl = indices_decl = pixels_decl = filter_decl = wrap_decl = transforms_decl = NULL;
    bytes_decl = paths_decl = NULL;
}

// What Draw.Mesh takes, the same way: these stand for the program's own
// List<Vertex>, List<Vertex3>, List<int>, Grid2<Color>, Filter and Wrap.
static decl mesh_vertices, mesh_vertices3, mesh_indices, mesh_pixels, mesh_filter, mesh_wrap, mesh_transforms, mesh_instances;
static const type T_VERTICES = {TY_LIST, &mesh_vertices};
static const type T_VERTICES3 = {TY_LIST, &mesh_vertices3};
static const type T_INDICES = {TY_LIST, &mesh_indices};
static const type T_FILTER = {TY_ENUM, &mesh_filter};
static const type T_WRAP = {TY_ENUM, &mesh_wrap};
static const type T_TRANSFORMS = {TY_LIST, &mesh_transforms};
// Editor signatures for ordinary instance structs: the checker resolves these itself.
static const type T_INSTANCES = {TY_LIST, &mesh_instances};
static const type T_RECT = {TY_RECT, NULL};
static const type T_TEXTURE = {TY_TEXTURE, NULL};
static const type T_CLIP = {TY_CLIP, NULL};
static const type T_TARGET = {TY_RENDER_TARGET, NULL};

void builtins_use_mesh(const decl *vertices, const decl *vertices3, const decl *indices, const decl *pixels,
                       const decl *filter, const decl *wrap, const decl *transforms)
{
    vertices_decl = vertices;
    vertices3_decl = vertices3;
    indices_decl = indices;
    pixels_decl = pixels;
    filter_decl = filter;
    wrap_decl = wrap;
    transforms_decl = transforms;
}

// Whether `t` stands for one of the program's types Draw.Mesh takes.
static bool mesh_type(const type t)
{
    return t.decl == &mesh_vertices || t.decl == &mesh_vertices3 || t.decl == &mesh_indices || t.decl == &mesh_pixels
        || t.decl == &mesh_filter || t.decl == &mesh_wrap || t.decl == &mesh_transforms || t.decl == &mesh_instances;
}

// A signature's type as a program sees it.
static type real_type(const type t)
{
    if (t.decl == &io_bytes && bytes_decl) return (type){TY_LIST, (decl *)bytes_decl};
    if (t.decl == &io_paths && paths_decl) return (type){TY_LIST, (decl *)paths_decl};
    if (t.kind == TY_ENUM && !t.decl) return (type){TY_ENUM, (decl *)anchor_decl};
    if (t.decl == &shape_struct && shape_decl) return (type){TY_STRUCT, (decl *)shape_decl};
    if (t.decl == &blend_enum && blend_decl) return (type){TY_ENUM, (decl *)blend_decl};
    if (t.decl == &depth_enum && depth_decl) return (type){TY_ENUM, (decl *)depth_decl};
    if (t.decl == &cull_enum && cull_decl) return (type){TY_ENUM, (decl *)cull_decl};
    if (t.decl == &format_enum && format_decl) return (type){TY_ENUM, (decl *)format_decl};
    if (t.decl == &stencil_test_enum && stencil_test_decl) return (type){TY_ENUM, (decl *)stencil_test_decl};
    if (t.decl == &stencil_op_enum && stencil_op_decl) return (type){TY_ENUM, (decl *)stencil_op_decl};
    if (t.decl == &text_font && font_decl) return (type){TY_ENUM, (decl *)font_decl};
    if (t.decl == &theme_struct && theme_decl) return (type){TY_STRUCT, (decl *)theme_decl};
    if (t.decl == &control_struct && control_decl) return (type){TY_STRUCT, (decl *)control_decl};
    if (t.decl == &mesh_vertices && vertices_decl) return (type){TY_LIST, (decl *)vertices_decl};
    if (t.decl == &mesh_vertices3 && vertices3_decl) return (type){TY_LIST, (decl *)vertices3_decl};
    if (t.decl == &mesh_indices && indices_decl) return (type){TY_LIST, (decl *)indices_decl};
    if (t.decl == &mesh_pixels && pixels_decl) return (type){TY_GRID, (decl *)pixels_decl};
    if (t.decl == &mesh_filter && filter_decl) return (type){TY_ENUM, (decl *)filter_decl};
    if (t.decl == &mesh_wrap && wrap_decl) return (type){TY_ENUM, (decl *)wrap_decl};
    if (t.decl == &mesh_transforms && transforms_decl) return (type){TY_LIST, (decl *)transforms_decl};
    return t;
}

static const char *signature_type_name(const type t)
{
    if (t.decl == &io_bytes) return "List<byte>";
    if (t.decl == &io_paths) return "List<string>";
    if (t.decl == &mesh_vertices) return "List<Vertex>";
    if (t.decl == &mesh_vertices3) return "List<Vertex3>";
    if (t.decl == &mesh_indices) return "List<int>";
    if (t.decl == &mesh_pixels) return "Grid2<Color>";
    if (t.decl == &mesh_filter) return "Filter";
    if (t.decl == &mesh_wrap) return "Wrap";
    if (t.decl == &mesh_transforms) return "List<float4x4>";
    if (t.decl == &mesh_instances) return "List<T>";
    if (t.decl == &shape_struct) return "Shape";
    if (t.decl == &blend_enum) return "Blend";
    if (t.decl == &depth_enum) return "Depth";
    if (t.decl == &cull_enum) return "Cull";
    if (t.decl == &format_enum) return "Format";
    if (t.decl == &stencil_test_enum) return "StencilTest";
    if (t.decl == &stencil_op_enum) return "StencilOp";
    if (t.decl == &text_font) return "Font";
    if (t.decl == &theme_struct) return "GUITheme";
    if (t.decl == &control_struct) return "GUIControl";
    return t.kind == TY_ENUM && !t.decl ? "Anchor" : type_name(t);
}

// ---------------------------------------------------------------------------
// Component-wise functions: take float or int scalars and vectors, mixed
// freely as long as the vectors have one size; scalars widen to that size.

static const struct {
    const char *name;
    int argc;
    bool ints; // Works on ints too; otherwise ints convert to float.
} componentwise[] = {
    {"Abs", 1, true}, {"Sign", 1, true}, {"Min", 2, true}, {"Max", 2, true}, {"Clamp", 3, true},
    {"Floor", 1, false}, {"Ceil", 1, false}, {"Round", 1, false}, {"Trunc", 1, false}, {"Frac", 1, false},
    {"Sqrt", 1, false}, {"Rsqrt", 1, false}, {"Saturate", 1, false}, {"Radians", 1, false}, {"Degrees", 1, false},
    {"Sin", 1, false}, {"Cos", 1, false}, {"Tan", 1, false}, {"Asin", 1, false}, {"Acos", 1, false},
    {"Atan", 1, false}, {"Atan2", 2, false}, {"Exp", 1, false}, {"Exp2", 1, false}, {"Log", 1, false},
    {"Log2", 1, false}, {"Log10", 1, false}, {"Pow", 2, false}, {"Step", 2, false},
    {"Lerp", 3, false}, {"Unlerp", 3, false}, {"SmoothStep", 3, false},
};

#define COMPONENTWISE_COUNT (sizeof componentwise / sizeof componentwise[0])

// ---------------------------------------------------------------------------
// Fixed signatures, tried in order; the first whose parameters accept the
// arguments (with implicit int -> float widening) wins.
//
// The table is built once and kept for the whole process, so its strings are
// malloc'd rather than taken from the arena, which the language server resets.

#define MAX_PARAMS 6

typedef struct signature {
    const char *owner;
    const char *name;
    type result;
    int argc;
    type params[MAX_PARAMS];
    const char *c_name;
    const char *param_names; // "center, radius, color", or NULL
    const char *doc;         // One line for editors, or NULL
    unsigned mut;            // A bit per parameter the call changes: the argument's variable itself
    int gui;                 // GUI_ID and GUI_CONTAINER, or which form of a draw setting it is: DRAW_BLOCK, DRAW_SET
    const char *pack;        // The function that makes the one value c_name takes of the arguments, or NULL
    // The parameters a call can leave out, its last `optional`: each one's
    // default as Tide writes it ("false"), which its C function is passed then
    int optional;
    const char *defaults[MAX_PARAMS];
    unsigned out;            // A bit per `out` parameter, which the call makes: its bit in `mut` is set too
} signature;

#define MAX_SIGNATURES 384

static signature signatures[MAX_SIGNATURES];
static int signature_count;

static const char *permanent(const char *s)
{
    const size_t n = strlen(s) + 1;
    char *out = malloc(n);
    if (!out) {
        fprintf(stderr, "tidec: out of memory\n");
        exit(1);
    }
    memcpy(out, s, n);
    return out;
}

// tide_<lowercase name>_<suffix>, from the arena.
static const char *c_function(const char *name, const type t)
{
    char buf[128];
    int n = snprintf(buf, sizeof buf, "tide_");
    for (const char *p = name; *p && n < (int)sizeof buf - 1; p++) buf[n++] = (char)tolower((unsigned char)*p);
    snprintf(buf + n, sizeof buf - (size_t)n, "_%s", type_suffix(t));
    char *out = arena_alloc(strlen(buf) + 1);
    memcpy(out, buf, strlen(buf));
    return out;
}

static signature *add(const char *owner, const char *name, const type result, const char *c_name,
                      const int argc, const type p0, const type p1, const type p2)
{
    if (signature_count == MAX_SIGNATURES) {
        fprintf(stderr, "tidec: too many built-in signatures (raise MAX_SIGNATURES)\n");
        exit(1);
    }
    signature *s = &signatures[signature_count++];
    *s = (signature){owner, name, result, argc, {p0, p1, p2, T_NONE, T_NONE, T_NONE}, permanent(c_name), NULL, NULL, 0, 0, NULL, 0, {NULL}, 0};
    return s;
}

// The last parameter of `s` is one a call can leave out, for the value
// `text`, as Tide writes it: `bool overwrite = false`.
static signature *defaulted(signature *s, const char *text)
{
    s->optional = 1;
    s->defaults[s->argc - 1] = text;
    return s;
}

// A mesh draw's last two parameters are ones a call can leave out, the wrap
// alone or both: how its texture is read between cells, and past its edges.
// `argc` is how many parameters it has before them.
static signature *sampled(signature *s, const int argc)
{
    s->argc = argc + 2;
    s->params[argc] = T_FILTER;
    s->params[argc + 1] = T_WRAP;
    s->optional = 2;
    s->defaults[argc] = "the texture's";
    s->defaults[argc + 1] = "the texture's";
    return s;
}

// A GUI function from its parameters as Tide writes them:
// "string label, mut float value, float min, float max", or with one the call
// makes, "string label, mut string value, out bool entered".
static void add_gui(const char *owner, const char *name, const type result, const char *c_name, const char *params,
                    const int gui, const char *doc)
{
    signature *s = add(owner, name, result, c_name, 0, T_NONE, T_NONE, T_NONE);
    s->gui = gui;
    s->doc = doc;
    sb names = {0};
    for (const char *p = params; *p;) {
        const char *end = strchr(p, ',');
        const size_t n = end ? (size_t)(end - p) : strlen(p);
        char buf[64];
        snprintf(buf, sizeof buf, "%.*s", (int)n, p);
        const bool out = strncmp(buf, "out ", 4) == 0;
        const bool mut = out || strncmp(buf, "mut ", 4) == 0; // An out one goes by address too
        char *type_text = buf + (mut ? 4 : 0);
        char *space = strchr(type_text, ' ');
        *space = '\0';
        type t = {TY_ERROR, NULL};
        if (strcmp(type_text, "string") == 0) t = T_STR;
        else if (strcmp(type_text, "Anchor") == 0) t = T_ANCHOR;
        else if (strcmp(type_text, "GUITheme") == 0) t = T_THEME;
        else builtin_type_named(str_from(type_text), &t);
        if (out) s->out |= 1u << s->argc;
        if (mut) s->mut |= 1u << s->argc;
        s->params[s->argc++] = t;
        sb_printf(&names, "%s%s", names.len ? ", " : "", space + 1);
        p = end ? end + 2 : p + n;
    }
    s->param_names = permanent(names.data ? names.data : "");
}

// A widget in both GUI, at a rect, and GUILayout, laid out: tide_gui_<c> and
// tide_gui_layout_<c>.
static void add_widget(const char *name, const type result, const char *c, const char *params, const int gui,
                       const char *doc)
{
    char c_name[64];
    char with_rect[256];
    snprintf(with_rect, sizeof with_rect, params[0] ? "Rect rect, %s" : "Rect rect", params);
    snprintf(c_name, sizeof c_name, "tide_gui_%s", c);
    add_gui("GUI", name, result, c_name, with_rect, gui, doc);
    snprintf(c_name, sizeof c_name, "tide_gui_layout_%s", c);
    add_gui("GUILayout", name, result, c_name, params, gui, doc);
}

static void describe(signature *s, const char *param_names, const char *doc)
{
    s->param_names = param_names;
    s->doc = doc;
}

// A draw setting in its two forms: `name`, around a block, which puts back what
// it found when the block ends (c_push), and Set`name`, for the draws that
// follow (c_set). `what` says what it sets, and `pack` names the function that
// makes the one value both take of their arguments, or is NULL. `argc`
// parameters, each a `const type *`.
static void add_setting(const char *owner, const char *name, const char *c_push, const char *c_set, const char *pack,
                        const char *param_names, const char *what, const int argc, ...)
{
    type params[MAX_PARAMS];
    va_list types;
    va_start(types, argc);
    for (int i = 0; i < argc; i++) params[i] = *va_arg(types, const type *);
    va_end(types);
    char set_name[64];
    snprintf(set_name, sizeof set_name, "Set%s", name);
    sb around = {0}, after = {0};
    sb_printf(&around, "%s For the draws in its block: what it found is put back when the block ends, however the block "
                       "is left. To set it for the draws that follow instead, `%s.%s`.", what, owner, set_name);
    sb_printf(&after, "%s For the draws after it, in this view and the views that follow, until it's set again: every "
                      "frame starts over. Around some draws only, `%s.%s(...) { ... }`.", what, owner, name);
    for (int form = 0; form < 2; form++) {
        signature *g = add(owner, form ? permanent(set_name) : name, T_NONE, form ? c_set : c_push, 0, T_NONE, T_NONE, T_NONE);
        for (int i = 0; i < argc; i++) g->params[i] = params[i];
        g->argc = argc;
        g->gui = form ? DRAW_SET : DRAW_BLOCK;
        g->pack = pack;
        g->param_names = param_names;
        g->doc = permanent(form ? after.data : around.data);
    }
}

// A setting's Set form with no arguments, which puts it back as each frame starts.
static void add_unset(const char *owner, const char *name, const char *c_name, const char *doc)
{
    signature *g = add(owner, name, T_NONE, c_name, 0, T_NONE, T_NONE, T_NONE);
    g->gui = DRAW_SET;
    g->doc = doc;
}

// A Draw function of up to MAX_PARAMS parameters: `argc` of them, each a `const type *`.
static signature *add_draw(const char *name, const char *c_name, const char *param_names, const char *doc, const int argc, ...)
{
    signature *s = add("Draw", name, T_NONE, c_name, 0, T_NONE, T_NONE, T_NONE);
    va_list types;
    va_start(types, argc);
    for (int i = 0; i < argc; i++) s->params[i] = *va_arg(types, const type *);
    va_end(types);
    s->argc = argc;
    describe(s, param_names, doc);
    return s;
}

static void build_signatures(void)
{
    if (signature_count > 0) return;

    for (int n = 2; n <= 4; n++) {
        const type v = vector_type(true, n);
        add("Math", "Dot", T_F, c_function("dot", v), 2, v, v, T_NONE);
        add("Math", "Length", T_F, c_function("length", v), 1, v, T_NONE, T_NONE);
        add("Math", "LengthSq", T_F, c_function("lengthsq", v), 1, v, T_NONE, T_NONE);
        add("Math", "Distance", T_F, c_function("distance", v), 2, v, v, T_NONE);
        add("Math", "DistanceSq", T_F, c_function("distancesq", v), 2, v, v, T_NONE);
        describe(add("Math", "Normalize", v, c_function("normalize", v), 1, v, T_NONE, T_NONE), "vector",
                 "The vector at length 1. One too short to have a direction, like a move with no key held, gives "
                 "zero, never NaN.");
        add("Math", "Reflect", v, c_function("reflect", v), 2, v, v, T_NONE);
        add("Math", "Csum", T_F, c_function("csum", v), 1, v, T_NONE, T_NONE);
        add("Math", "Cmin", T_F, c_function("cmin", v), 1, v, T_NONE, T_NONE);
        add("Math", "Cmax", T_F, c_function("cmax", v), 1, v, T_NONE, T_NONE);
    }
    add("Math", "Cross", T_F3, "tide_cross_f3", 2, T_F3, T_F3, T_NONE);
    for (int n = 1; n <= 4; n++) {
        const type v = vector_type(false, n);
        describe(add("Math", "Hash", (type){TY_INT, NULL}, c_function("hash", v), 1, v, T_NONE, T_NONE), "value",
                 "A well-mixed int from 0 to 2147483647, the same for the same value on every machine: random numbers "
                 "from cells, indices and seeds, like `Math.Hash(int3(x, y, seed)) % 6`. It's xxHash32 of the "
                 "value's bytes, without the top bit. A frag or vert gets the same number on every GPU.");
    }
    describe(add("Math", "Hash", (type){TY_INT, NULL}, "tide_hash_l", 1, (type){TY_LONG, NULL}, T_NONE, T_NONE), "value",
             "A well-mixed int from 0 to 2147483647, the same for the same value on every machine: xxHash32 of the "
             "long's 8 bytes, without the top bit. A GPU has no long: a frag or vert hashes its halves, "
             "`Math.Hash(int2(int(x), int(x >> 32)))`, which is the same number.");

    // Noise (engine/src/noise.c): a point, and a seed a call can leave out.
    // On the GPU it's the same formulas (shader.c), so a frag draws what the match computed.
    for (int n = 2; n <= 3; n++) {
        const type v = vector_type(true, n);
        describe(defaulted(add("Math", "Perlin", T_F, c_function("perlin", v), 2, v, T_WHOLE, T_NONE), "0"), "point, seed",
                 "Gradient (Perlin) noise at a point: smooth, from -1 to 1, and 0 at every whole number, so its features "
                 "are about a unit across. Scale the point for finer or coarser noise, and add octaves for detail. "
                 "The same point gives the same number on every machine, and on every GPU up to its rounding; another "
                 "seed is another field of noise.");
        describe(defaulted(add("Math", "Simplex", T_F, c_function("simplex", v), 2, v, T_WHOLE, T_NONE), "0"), "point, seed",
                 "Simplex noise at a point: smooth, about -1 to 1, with no grid showing through, and cheaper than "
                 "Perlin in 3D. Its features are about a unit across. The same point gives the same number on every "
                 "machine; another seed is another field of noise.");
        describe(defaulted(add("Math", "Cellular", T_F2, c_function("cellular", v), 2, v, T_WHOLE, T_NONE), "0"), "point, seed",
                 "Cellular (Worley) noise at a point: a feature point in every unit cell, and the distances to the nearest "
                 "one (x) and the next nearest (y), from 0 to about 1.4. `y - x` is 0 on the borders between cells. "
                 "The same point gives the same distances on every machine; another seed moves every feature point.");
    }

    // A Random's methods: tide/random.h, with the generator first, by address,
    // as each steps it. Formulas follow Unity.Mathematics' Random.
    const type r_int = {TY_INT, NULL};
    describe(add("Random", "NextInt", r_int, "tide_random_next_int", 0, T_NONE, T_NONE, T_NONE), NULL,
             "A random int from 0 to 2147483647, like `Math.Hash`'s, so `%` never goes negative.");
    describe(add("Random", "NextInt", r_int, "tide_random_next_int_below", 1, r_int, T_NONE, T_NONE), "max",
             "A random int from 0 up to `max`, `max` excluded: `NextInt(6)` is 0 to 5. 0 when `max` isn't above 0.");
    describe(add("Random", "NextInt", r_int, "tide_random_next_int_between", 2, r_int, r_int, T_NONE), "min, max",
             "A random int from `min` up to `max`, `max` excluded: `NextInt(1, 7)` is a die. `min` when `max` isn't above it.");
    describe(add("Random", "NextFloat", T_F, "tide_random_next_float", 0, T_NONE, T_NONE, T_NONE), NULL,
             "A random float from 0 up to 1, 1 excluded.");
    describe(add("Random", "NextFloat", T_F, "tide_random_next_float_below", 1, T_F, T_NONE, T_NONE), "max",
             "A random float from 0 up to `max`, `max` excluded. 0 when `max` isn't above 0.");
    describe(add("Random", "NextFloat", T_F, "tide_random_next_float_between", 2, T_F, T_F, T_NONE), "min, max",
             "A random float from `min` up to `max`, `max` excluded. `min` when `max` isn't above it.");
    describe(add("Random", "NextBool", T_BOOL, "tide_random_next_bool", 0, T_NONE, T_NONE, T_NONE), NULL,
             "True or false, each as likely.");
    describe(add("Random", "NextFloat2", T_F2, "tide_random_next_float2", 0, T_NONE, T_NONE, T_NONE), NULL,
             "A float2 with each component from 0 up to 1: a point in the unit square.");
    describe(add("Random", "NextFloat2", T_F2, "tide_random_next_float2_between", 2, T_F2, T_F2, T_NONE), "min, max",
             "A float2 with each component from `min`'s up to `max`'s: a point in a rectangle.");
    describe(add("Random", "NextFloat3", T_F3, "tide_random_next_float3", 0, T_NONE, T_NONE, T_NONE), NULL,
             "A float3 with each component from 0 up to 1: a point in the unit cube.");
    describe(add("Random", "NextFloat3", T_F3, "tide_random_next_float3_between", 2, T_F3, T_F3, T_NONE), "min, max",
             "A float3 with each component from `min`'s up to `max`'s: a point in a box.");
    describe(add("Random", "NextFloat2Direction", T_F2, "tide_random_next_float2_direction", 0, T_NONE, T_NONE, T_NONE), NULL,
             "A float2 of length 1, every direction as likely.");
    describe(add("Random", "NextFloat3Direction", T_F3, "tide_random_next_float3_direction", 0, T_NONE, T_NONE, T_NONE), NULL,
             "A float3 of length 1, every direction as likely.");
    describe(add("Random", "NextQuaternionRotation", T_Q, "tide_random_next_quaternion_rotation", 0, T_NONE, T_NONE, T_NONE), NULL,
             "A rotation, every one as likely.");

    // Quaternions
    add("Math", "Dot", T_F, "tide_dot_q", 2, T_Q, T_Q, T_NONE);
    describe(add("Math", "Normalize", T_Q, "tide_normalize_q", 1, T_Q, T_NONE, T_NONE), "rotation",
             "The quaternion at length 1, which a rotation has to be. One too short, like a field nothing set, "
             "gives the identity, never NaN.");
    add("Math", "Mul", T_Q, "tide_mul_q", 2, T_Q, T_Q, T_NONE);
    add("Math", "Mul", T_F3, "tide_rotate_q", 2, T_Q, T_F3, T_NONE);
    add("Math", "Rotate", T_F3, "tide_rotate_q", 2, T_Q, T_F3, T_NONE);
    add("Math", "Inverse", T_Q, "tide_inverse_q", 1, T_Q, T_NONE, T_NONE);
    add("Math", "Conjugate", T_Q, "tide_conjugate_q", 1, T_Q, T_NONE, T_NONE);
    add("Math", "Slerp", T_Q, "tide_slerp_q", 3, T_Q, T_Q, T_F);
    add("Math", "Nlerp", T_Q, "tide_nlerp_q", 3, T_Q, T_Q, T_F);
    add("Math", "Forward", T_F3, "tide_forward_q", 1, T_Q, T_NONE, T_NONE);
    add("Math", "Up", T_F3, "tide_up_q", 1, T_Q, T_NONE, T_NONE);
    add("Math", "Right", T_F3, "tide_right_q", 1, T_Q, T_NONE, T_NONE);
    add("Math", "Angle", T_F, "tide_angle_q", 2, T_Q, T_Q, T_NONE);

    // Matrices
    for (int n = 2; n <= 4; n++) {
        const type m = matrix_type(n);
        const type v = vector_type(true, n);
        char mul_vec[64];
        snprintf(mul_vec, sizeof mul_vec, "tide_mul_%s_%s", type_suffix(m), type_suffix(v));
        add("Math", "Mul", m, c_function("mul", m), 2, m, m, T_NONE);
        add("Math", "Mul", v, mul_vec, 2, m, v, T_NONE);
        add("Math", "Transpose", m, c_function("transpose", m), 1, m, T_NONE, T_NONE);
        add("Math", "Inverse", m, c_function("inverse", m), 1, m, T_NONE, T_NONE);
        add("Math", "Determinant", T_F, c_function("determinant", m), 1, m, T_NONE, T_NONE);
    }
    add("Math", "Transform", T_F3, "tide_transform_f4x4", 2, T_F4X4, T_F3, T_NONE);
    add("Math", "Rotate", T_F3, "tide_rotate_f4x4", 2, T_F4X4, T_F3, T_NONE);

    // Ways to build quaternions and matrices
    describe(add("quaternion", "AxisAngle", T_Q, "tide_axisangle_q", 2, T_F3, T_F, T_NONE),
             "axis, angle", "A rotation of `angle` radians around `axis`.");
    describe(add("quaternion", "Euler", T_Q, "tide_euler_q", 1, T_F3, T_NONE, T_NONE),
             "radians", "A rotation from Euler angles in radians: Z first, then X, then Y.");
    signature *euler = add("quaternion", "Euler", T_Q, "tide_euler_q", 3, T_F, T_F, T_F);
    euler->pack = "tide_f3";
    describe(euler, "x, y, z", "A rotation from Euler angles in radians: Z first, then X, then Y.");
    describe(add("quaternion", "LookRotation", T_Q, "tide_lookrotation_q", 2, T_F3, T_F3, T_NONE),
             "forward, up", "A rotation that looks along `forward`, with `up` as up.");
    describe(add("quaternion", "FromToRotation", T_Q, "tide_fromto_q", 2, T_F3, T_F3, T_NONE),
             "from, to", "The shortest rotation between directions of any length. Zero or nonfinite directions give identity; "
             "opposite directions use a deterministic perpendicular axis.");
    describe(add("float4x4", "TRS", T_F4X4, "tide_trs_f4x4", 3, T_F3, T_Q, T_F3),
             "translation, rotation, scale", "A transform: scale, then rotate, then translate.");
    describe(add("float4x4", "Translate", T_F4X4, "tide_translate_f4x4", 1, T_F3, T_NONE, T_NONE),
             "translation", "A translation matrix.");
    describe(add("float4x4", "Scale", T_F4X4, "tide_scaling_f4x4", 1, T_F3, T_NONE, T_NONE),
             "scale", "A scale matrix, without rotation or translation.");
    signature *perspective = add("float4x4", "PerspectiveFov", T_F4X4, "tide_perspectivefov_f4x4", 3, T_F, T_F, T_F);
    perspective->argc = 4;
    perspective->params[3] = T_F;
    describe(perspective, "verticalFov, aspect, near, far",
             "A perspective projection, for `Draw.Camera(transform, projection)`: `verticalFov` in radians, and "
             "`aspect` the screen's width over its height. It takes what a camera sees, looking down -z as "
             "OpenGL's and Unity's views do, to the screen, from `near` to `far`.");
    signature *ortho = add("float4x4", "Ortho", T_F4X4, "tide_ortho_f4x4", 3, T_F, T_F, T_F);
    ortho->argc = 4;
    ortho->params[3] = T_F;
    describe(ortho, "width, height, near, far",
             "An orthographic projection, for `Draw.Camera(transform, projection)`: a box `width` by `height` "
             "around the camera's axis, from `near` to `far`, with no perspective.");

    // Sound, in local code: tide/audio.h. Codegen passes the frame's audio list first, and a Clip through
    // tide_clip_of, which finds its samples in the pack.
    {
        signature *play = add("Audio", "Play", T_NONE, "tide_audio_play", 3, T_CLIP, T_F, T_F);
        play->optional = 2;
        play->defaults[1] = "1";
        play->defaults[2] = "1";
        describe(play, "clip, volume, pitch",
                 "Plays a clip once, from its start: a one-shot the mixer owns from then on, with nothing to stop it. `volume` "
                 "1 is the clip as it is, and `pitch` 1 its own speed (2 an octave up). Plain stereo, from nowhere in particular.");
        signature *flat = add("Audio", "Play", T_NONE, "tide_audio_play_at2", 4, T_CLIP, T_F2, T_F);
        flat->params[3] = T_F;
        flat->optional = 2;
        flat->defaults[2] = "1";
        flat->defaults[3] = "1";
        describe(flat, "clip, position, volume, pitch",
                 "Plays a clip once from `position` in the world (z 0): heard from the side it's on against the listener, "
                 "and quieter with its distance (at its volume within 1 unit, silent past 50, unless the view sets a falloff).");
        signature *placed = add("Audio", "Play", T_NONE, "tide_audio_play_at", 4, T_CLIP, T_F3, T_F);
        placed->params[3] = T_F;
        placed->optional = 2;
        placed->defaults[2] = "1";
        placed->defaults[3] = "1";
        describe(placed, "clip, position, volume, pitch",
                 "Plays a clip once from `position` in the world: heard from the side it's on against the listener, and quieter "
                 "with its distance (at its volume within 1 unit, silent past 50, unless the view sets a falloff).");
    }

    describe(add("Audio", "SetListener", T_NONE, "tide_audio_set_listener", 2, T_F3, T_Q, T_NONE), "position, rotation",
             "Where this frame's spatial sounds are heard from, and which way it faces (its right is +x turned by `rotation`): "
             "for the frames after too, until it's set again or the frame ends. Without it, the origin, facing +z.");
    describe(add("Audio", "SetListener", T_NONE, "tide_audio_set_listener2", 1, T_F2, T_NONE, T_NONE), "center",
             "The listener at a 2D place (z 0), facing +z: a sound to its right is heard on the right.");
    describe(add("Audio", "SetFalloff", T_NONE, "tide_audio_set_falloff", 2, T_F, T_F, T_NONE), "near, far",
             "How a spatial sound's volume falls off with its distance from the listener: at its volume within `near`, "
             "silent past `far`, and as `near / distance` between, scaled to reach 0 at `far`. 1 and 50 without it.");

    // Drawing, in views: tide/draw.h. Codegen passes the view's draw list first.
    describe(add("Draw", "Clear", T_NONE, "tide_draw_clear", 1, T_COLOR, T_NONE, T_NONE),
             "color", "Fills the whole screen, or what the clip leaves of it, and clears depth and stencil. No blend, tint "
             "or transform changes a clear.");
    describe(add("Draw", "ClearDepth", T_NONE, "tide_draw_clear_depth", 0, T_NONE, T_NONE, T_NONE),
             "", "Sets depth back alone, over the whole screen or what the clip leaves of it: colors and stencil stay, "
             "and the 3D meshes drawn after are in front of everything drawn before.");
    // The settings: what the draws after them, or in their block, are drawn with (tide/draw.h's Settings)
    add_setting("Draw", "Blend", "tide_draw_push_blend", "tide_draw_set_blend", NULL, "mode",
                "How what's drawn goes over what's there: `Blend.Alpha` (by its alpha, each frame's first), `Blend.Add` "
                "(added to it, as light is), `Blend.Multiply` (what's there times it) or `Blend.Replace` (in its place, "
                "alpha too). It's the window's: a render target has its own, `target.Blend`.",
                1, &T_BLEND);
    add_unset("Draw", "SetBlend", "tide_draw_unset_blend", "Back to `Blend.Alpha`, as each frame starts.");
    add_setting("Draw", "Tint", "tide_draw_push_tint", "tide_draw_set_tint", NULL, "color",
                "What everything drawn has its color multiplied by, alpha too: `Color(1, 1, 1, 0.5)` fades it by half. "
                "It's the window's: a render target has its own, `target.Tint`. In a block, it multiplies the tint "
                "there is.",
                1, &T_COLOR);
    add_unset("Draw", "SetTint", "tide_draw_unset_tint", "No tint, as each frame starts.");
    // Stencil: both forms, with or without what's stored
    static const char stencil_doc[] =
        "What the draws do with stencil, a number from 0 to 255 for each pixel beside its depth: they draw only where "
        "`test` passes, `value` against the number stored there (`StencilTest.Equal`: where it's `value`; "
        "`StencilTest.Less`: where `value` is less than it; `StencilTest.Always`, each frame's first: everywhere), and "
        "where they draw, `operation` says what becomes of the stored number.";
    add_setting("Draw", "Stencil", "tide_draw_push_stencil", "tide_draw_set_stencil", "tide_stencil_tested", "test, value",
                stencil_doc, 2, &T_STENCIL_TEST, &T_WHOLE);
    add_setting("Draw", "Stencil", "tide_draw_push_stencil", "tide_draw_set_stencil", "tide_stencil_stored",
                "test, value, operation", stencil_doc, 3, &T_STENCIL_TEST, &T_WHOLE, &T_STENCIL_OP);
    add_unset("Draw", "SetStencil", "tide_draw_unset_stencil",
              "No stencil test, and nothing stored, as each frame starts.");
    static const char transform_doc[] =
        "Moves, turns and scales the 2D draws (shapes, text and 2D meshes), in the units they're in, about (0, 0): scaled, then turned by "
        "`rotation` (radians, from +x towards +y), then moved by `offset`. Shapes stay shapes; text that's only moved "
        "and scaled as much each way stays fitted to pixels, and turned or stretched text is drawn without. In a "
        "block, it's inside the transform there is.";
    add_setting("Draw", "Transform", "tide_draw_push_transform", "tide_draw_set_transform", "tide_draw_transform_moved",
                "offset", transform_doc, 1, &T_F2);
    add_setting("Draw", "Transform", "tide_draw_push_transform", "tide_draw_set_transform", "tide_draw_transform_turned",
                "offset, rotation", transform_doc, 2, &T_F2, &T_F);
    add_setting("Draw", "Transform", "tide_draw_push_transform", "tide_draw_set_transform", "tide_draw_transform_scaled",
                "offset, rotation, scale", transform_doc, 3, &T_F2, &T_F, &T_F);
    add_setting("Draw", "Transform", "tide_draw_push_transform", "tide_draw_set_transform", "tide_draw_transform_trs",
                "offset, rotation, scale", transform_doc, 3, &T_F2, &T_F, &T_F2);
    add_setting("Draw", "Transform", "tide_draw_push_transform", "tide_draw_set_transform", "tide_draw_transform_matrix",
                "matrix",
                "Puts the 2D draws through a matrix, in the units they're in: its first two columns are where (1, 0) "
                "and (0, 1) go, and its third where (0, 0) does. In a block, it's inside the transform there is.",
                1, &T_F3X3);
    add_setting("Draw", "Transform", "tide_draw_push_transform_3d", "tide_draw_set_transform_3d", NULL, "transform",
                "Puts the 3D meshes through `transform` on their way to the world, each after its own. In a block, it's "
                "inside the transform there is.",
                1, &T_F4X4);
    add_unset("Draw", "SetTransform", "tide_draw_unset_transforms", "No transform, 2D or 3D, as each frame starts.");
    add_setting("Draw", "Depth", "tide_draw_push_depth", "tide_draw_set_depth", NULL, "mode",
                "What the 3D meshes do with depth: `Depth.ReadWrite` (each frame's first: a mesh is hidden behind what's "
                "nearer and hides what's drawn behind it later, whatever the order), `Depth.Read` (hidden behind what's "
                "nearer, and hides nothing: what's see-through, drawn after what's solid, like decals, glass and smoke) or "
                "`Depth.None` (drawn over whatever is there). Only 3D meshes have depth: the 2D draws (shapes, text and "
                "2D meshes) draw over what's there, in order, whatever this says.",
                1, &T_DEPTH);
    add_unset("Draw", "SetDepth", "tide_draw_unset_depth", "Back to `Depth.ReadWrite`, as each frame starts.");
    add_setting("Draw", "Cull", "tide_draw_push_cull", "tide_draw_set_cull", NULL, "side",
                "Which side of the 3D meshes' triangles isn't drawn: `Cull.Back` (each frame's first: a triangle seen "
                "from behind isn't drawn, so what's solid costs half its triangles), `Cull.Front` (only the backs are: "
                "the inside of a shell) or `Cull.None` (both sides: what's flat or see-through, like a leaf or a "
                "pane). The front of a triangle is the side its corners go clockwise on, whatever transform or camera "
                "it's drawn through, one that mirrors too. A face that's missing is usually a triangle wound the other "
                "way. Only 3D meshes' triangles have a back: the 2D draws draw whichever way they're wound.",
                1, &T_CULL);
    add_unset("Draw", "SetCull", "tide_draw_unset_cull", "Back to `Cull.Back`, as each frame starts.");
    add_setting("Draw", "FrustumCulling", "tide_draw_push_frustum_culling", "tide_draw_set_frustum_culling", NULL, "enabled",
                "Skip 3D instances whose bounds are wholly outside this destination's camera. Enabled each frame. "
                "Custom verts and skinned meshes need explicit Bounds; unknown bounds keep the draw. "
                "This changes drawing only, never view execution or match simulation.", 1, &T_BOOL);
    add_unset("Draw", "SetFrustumCulling", "tide_draw_unset_frustum_culling", "Enable frustum culling, as each frame starts.");
    add_setting("Draw", "Bounds", "tide_draw_push_bounds", "tide_draw_set_bounds", NULL, "center, size",
                "A conservative mesh-local box for frustum culling: center and full size, before the instance transform "
                "and Draw.Transform. Include all skinning or custom vertex displacement. Invalid bounds keep the draw.",
                2, &T_F3, &T_F3);
    add_setting("Draw", "Bounds", "tide_draw_push_no_bounds", "tide_draw_unset_bounds", NULL, "",
                "Use geometry bounds where known; custom verts and skinned meshes remain uncullable without bounds.", 0);
    // Levels of detail (docs/spec.md): a mesh asset marked [LOD] has simpler versions, which a draw picks among
    add_setting("Draw", "LOD", "tide_draw_push_lod", "tide_draw_set_lod", NULL, "level",
                "Which level of detail the meshes marked `[LOD]` draw at, in place of the one each would pick by how "
                "big it is on screen: `0` is the full mesh, `1` the first simpler one, and a number past a mesh's last "
                "level is its last. Every instance of an instanced draw has it, in the order they're given. A mesh "
                "with no levels draws as it always does.",
                1, &T_WHOLE);
    add_setting("Draw", "LOD", "tide_draw_push_no_lod", "tide_draw_unset_lod", NULL, "",
                "Back to each mesh marked `[LOD]` picking its own level of detail, by how big it is on screen, as each "
                "frame starts.", 0);
    add_setting("Draw", "LODCamera", "tide_draw_push_lod_camera_3d", "tide_draw_set_lod_camera_3d", NULL,
                "position, rotation, fieldOfView",
                "The camera the meshes marked `[LOD]` pick their level of detail through, in place of the one they're "
                "drawn through, which stays as it is: a shadow map told the player's camera casts, for each mesh, the "
                "level the player sees. It's taken as the window's camera, so the pixels it counts are the window's.",
                3, &T_F3, &T_Q, &T_F);
    add_setting("Draw", "LODCamera", "tide_draw_push_lod_camera_matrices", "tide_draw_set_lod_camera_matrices", NULL,
                "transform, projection",
                "The camera the meshes marked `[LOD]` pick their level of detail through, as matrices: what "
                "`Draw.CameraTransform()` and `Draw.CameraProjection()` give of the camera to pick by. The draws still "
                "go through their destination's own camera.",
                2, &T_F4X4, &T_F4X4);
    add_setting("Draw", "LODCamera", "tide_draw_push_no_lod_camera", "tide_draw_unset_lod_camera", NULL, "",
                "Back to picking levels of detail through the camera the meshes are drawn through, as each frame "
                "starts.", 0);
    add_setting("Draw", "Camera", "tide_draw_push_camera", "tide_draw_set_camera", NULL, "center, size",
                "The 2D camera, which the shapes, text and 2D meshes go through: `center` is the world position at the "
                "middle of the screen and `size` is half the visible height, like Unity's orthographic size. The 3D "
                "camera is the version with a position, a rotation and a field of view, or with matrices.",
                2, &T_F2, &T_F);
    add_setting("Draw", "Screen", "tide_draw_push_screen", "tide_draw_set_screen", NULL, "",
                "The 2D draws are in the screen's pixels: from the top left, with y down, as the GUI's are.", 0);
    add_setting("Draw", "Camera", "tide_draw_push_camera_3d", "tide_draw_set_camera_3d", NULL,
                "position, rotation, fieldOfView",
                "The 3D camera the 3D meshes go through: where it is, which way it looks (along its rotation's +z), and "
                "how much it sees up and down, in degrees, like Unity's Camera.fieldOfView. It sees from 0.3 units in "
                "front of it on, however far.",
                3, &T_F3, &T_Q, &T_F);
    add_setting("Draw", "Camera", "tide_draw_push_camera_matrices", "tide_draw_set_camera_matrices", NULL,
                "transform, projection",
                "The 3D camera with any projection: `transform` places the camera in the world, looking along its +z, "
                "and `projection` (`float4x4.PerspectiveFov`, `float4x4.Ortho`) takes what it sees to the screen, the "
                "screen's shape included.",
                2, &T_F4X4, &T_F4X4);
    add_unset("Draw", "SetCamera", "tide_draw_unset_cameras",
              "The cameras each frame starts with, both: the 2D one with the origin at the middle of the screen, a world "
              "unit a pixel, and the 3D one at the origin, looking along +z, seeing 60 degrees up and down.");
    describe(add("Draw", "Circle", T_NONE, "tide_draw_circle", 3, T_F2, T_F, T_COLOR),
             "center, radius, color", "A filled circle.");
    describe(add("Draw", "WireCircle", T_NONE, "tide_draw_wire_circle", 3, T_F2, T_F, T_COLOR),
             "center, radius, color", "A circle outline.");
    describe(add("Draw", "Rect", T_NONE, "tide_draw_rect", 3, T_F2, T_F2, T_COLOR),
             "center, size, color", "A filled rectangle.");
    describe(add("Draw", "WireRect", T_NONE, "tide_draw_wire_rect", 3, T_F2, T_F2, T_COLOR),
             "center, size, color", "A rectangle outline.");
    describe(add("Draw", "Line", T_NONE, "tide_draw_line", 3, T_F2, T_F2, T_COLOR),
             "from, to, color", "A line a pixel wide.");
    // The shapes with smooth edges: tide/draw.h's, under the functions above
    add_draw("Line", "tide_draw_thick_line", "from, to, width, color",
               "A line `width` thick, with round ends and smooth edges.", 4, &T_F2, &T_F2, &T_F, &T_COLOR);
    add_draw("WireCircle", "tide_draw_thick_wire_circle", "center, radius, width, color",
               "A circle outline `width` thick, inside its radius, with smooth edges.", 4, &T_F2, &T_F, &T_F, &T_COLOR);
    add_draw("WireRect", "tide_draw_thick_wire_rect", "center, size, width, color",
               "A rectangle outline `width` thick, inside its size, with smooth edges.", 4, &T_F2, &T_F2, &T_F, &T_COLOR);
    add_draw("Rect", "tide_draw_rect_rotated", "center, size, rotation, color",
               "A filled rectangle turned about its center, with smooth edges. `rotation` is in radians, from +x "
               "towards +y: counterclockwise under a camera, clockwise on the screen.",
               4, &T_F2, &T_F2, &T_F, &T_COLOR);
    add_draw("RoundRect", "tide_draw_round_rect", "center, size, radius, color",
               "A filled rectangle whose corners are round, by `radius`: up to half its smaller side, which makes a pill.",
               4, &T_F2, &T_F2, &T_F, &T_COLOR);
    add_draw("RoundRect", "tide_draw_round_rect_rotated", "center, size, radius, rotation, color",
               "A filled rectangle with round corners, turned about its center. `rotation` is in radians, from +x "
               "towards +y: counterclockwise under a camera, clockwise on the screen.",
               5, &T_F2, &T_F2, &T_F, &T_F, &T_COLOR);
    add_draw("WireRoundRect", "tide_draw_wire_round_rect", "center, size, radius, width, color",
               "The outline of a rectangle with round corners, `width` thick, inside its size.",
               5, &T_F2, &T_F2, &T_F, &T_F, &T_COLOR);
    add_draw("Ellipse", "tide_draw_ellipse", "center, radii, color",
               "A filled ellipse: `radii` is half its width and half its height.", 3, &T_F2, &T_F2, &T_COLOR);
    add_draw("Ellipse", "tide_draw_ellipse_rotated", "center, radii, rotation, color",
               "A filled ellipse turned about its center. `rotation` is in radians, from +x towards +y: "
               "counterclockwise under a camera, clockwise on the screen.",
               4, &T_F2, &T_F2, &T_F, &T_COLOR);
    add_draw("WireEllipse", "tide_draw_wire_ellipse", "center, radii, width, color",
               "An ellipse outline `width` thick, inside its radii, as wide all the way round.",
               4, &T_F2, &T_F2, &T_F, &T_COLOR);
    add_draw("Arc", "tide_draw_arc", "center, radius, start, sweep, width, color",
               "Part of a ring: of the circle outline `width` thick inside `radius`, what's from the angle `start` on "
               "for `sweep`, in radians from +x towards +y (counterclockwise under a camera, clockwise on the screen; "
               "the other way round for a sweep under 0), cut straight across at both ends. A width of the radius "
               "is a slice of a disc, and a sweep of a whole turn or more the whole ring.",
               6, &T_F2, &T_F, &T_F, &T_F, &T_F, &T_COLOR);
    add_draw("Shape", "tide_draw_shape", "shape",
               "The general shape, which RoundRect, Ellipse, the thick outlines and the thick Line are short for: a "
               "`Shape`'s box or ellipse, turned, with a fill that can go from `color` at its top to `color2` at its "
               "bottom, an outline inside its edge, and an edge that can fade outwards (`soft` above 0: a glow, a "
               "shadow) or inwards (under 0).",
               1, &T_SHAPE);
    static const char text_doc[] =
        "Text, `size` high: a line's height, with its capitals in the line's middle. `position` is its top left "
        "corner, or with an `anchor`, that place of its box: `Anchor.MiddleCenter` centers it there, "
        "`Anchor.UpperRight` ends its first line there. Upper is towards the top of the screen, under a camera too, "
        "and each line of several is against the anchor's side. `font` is its weight: `Font.Regular` (the "
        "default) or `Font.Bold`.";
    add_draw("Text", "tide_draw_text", "text, position, size, color", text_doc, 4, &T_STR, &T_F2, &T_F, &T_COLOR);
    // With an anchor, the font is one a call can leave out; a font alone goes after the color
    defaulted(add_draw("Text", "tide_draw_text_at", "text, position, size, color, anchor, font", text_doc,
                         6, &T_STR, &T_F2, &T_F, &T_COLOR, &T_ANCHOR, &T_FONT), "Font.Regular");
    add_draw("Text", "tide_draw_text_at", "text, position, size, color, font", text_doc,
               5, &T_STR, &T_F2, &T_F, &T_COLOR, &T_FONT);
    static const char measure_doc[] =
        "How big `Draw.Text` would draw `text` at `size` now, in the weight `font` (`Font.Regular` without "
        "one): the width of its widest line in x and the height of its lines in y, in the units the 2D draws are "
        "in. One line is `size` high. It's the box an anchor names a place of: "
        "`Draw.Text(text, center, size, color, Anchor.MiddleCenter)` centers it.";
    describe(defaulted(add("Draw", "MeasureText", T_F2, "tide_draw_measure_text", 3, T_STR, T_F, T_FONT), "Font.Regular"),
             "text, size, font", measure_doc);
    static const char mesh_doc[] =
        "A 2D mesh, drawn where its vertices are, through the 2D camera: triangles, three of `indices` each, places "
        "in `vertices`. Each pixel is its corners' colors blended "
        "across the triangle, times `texture`'s at their uvs: a Texture, which an asset, a render target's color "
        "and a grid of colors or of floats with a size are (a grid's cell (0, 0) is at uv (0, 0)). `filter` is how "
        "it's read between pixels, `Filter.Bilinear`, `Filter.Point` or `Filter.Trilinear` (its smaller images too, "
        "where it's drawn smaller than it is), and `wrap`, after the filter, what a uv past "
        "0 or 1 reads: the pixels at the texture's edge (`Wrap.Clamp`), the texture again (`Wrap.Repeat`), or again "
        "the other way round each time (`Wrap.Mirror`). A call that leaves one out has the texture's own, which is "
        "what its declaration says (`[Filter]`, `[Wrap]`), and blended and clamped without one.";
    describe(add("Draw", "Mesh", T_NONE, "tide_draw_mesh_lists", 2, T_VERTICES, T_INDICES, T_NONE),
             "vertices, indices", mesh_doc);
    // The texture is a Texture, which a Grid2<Color> and a Grid2<float> are where one is wanted; its filter
    // and its wrap, where the call says them, are the texture's for this draw (gen_expr)
    describe(sampled(add("Draw", "Mesh", T_NONE, "tide_draw_mesh_texture", 3, T_VERTICES, T_INDICES, T_TEXTURE), 3),
             "vertices, indices, texture, filter, wrap", mesh_doc);
    static const char mesh3_doc[] =
        "A 3D mesh, placed in the world by `transform`, through the 3D camera: triangles, three of `indices` each, "
        "places in `vertices`, the nearest in front. Each pixel is its corners' colors blended across the "
        "triangle, times `texture`'s at their uvs, read with `filter` between its pixels and, for a uv past 0 or 1, "
        "as `wrap` says: the texture's own where the call leaves them out. Lists that don't change aren't copied "
        "again, and a mesh drawn again and again in a row is drawn at once.";
    describe(add("Draw", "Mesh", T_NONE, "tide_draw_mesh3_lists", 3, T_VERTICES3, T_INDICES, T_F4X4),
             "vertices, indices, transform", mesh3_doc);
    signature *texture_sampled3 = add("Draw", "Mesh", T_NONE, "tide_draw_mesh3_texture", 3, T_VERTICES3, T_INDICES, T_F4X4);
    texture_sampled3->params[3] = T_TEXTURE;
    describe(sampled(texture_sampled3, 4), "vertices, indices, transform, texture, filter, wrap", mesh3_doc);
    static const char instanced_doc[] =
        "Draws one mesh for each element of `instances`, in order: a List<float4x4> of 3D transforms, or a List<T> "
        "of structs whose fields a vert or frag reads through a read-only T parameter. A 3D struct's float4x4 "
        "transform places it; without one it uses the identity. A vert places 2D instances.";
    describe(add("Draw", "Instanced", T_NONE, "tide_draw_mesh3_instances_lists", 3, T_VERTICES3, T_INDICES, T_TRANSFORMS),
             "vertices, indices, instances", instanced_doc);
    signature *instances_texture_sampled = add("Draw", "Instanced", T_NONE, "tide_draw_mesh3_instances_texture", 3,
                                              T_VERTICES3, T_INDICES, T_TRANSFORMS);
    instances_texture_sampled->params[3] = T_TEXTURE;
    describe(sampled(instances_texture_sampled, 4), "vertices, indices, instances, texture, filter, wrap", instanced_doc);
    describe(add("Draw", "Instanced", T_NONE, "tide_draw_mesh_shaded_instances", 3, T_VERTICES, T_INDICES, T_INSTANCES),
             "vertices, indices, instances", instanced_doc);
    describe(add("Draw", "Instanced", T_NONE, "tide_draw_mesh_shaded_instances", 3, T_VERTICES3, T_INDICES, T_INSTANCES),
             "vertices, indices, instances", instanced_doc);
    // The checker resolves model and Pose types itself; this entry gives editors the public call's order.
    describe(add("Draw", "SkinnedMesh", T_NONE, "tide_draw_mesh_shaded", 3, T_NONE, T_F4X4, T_NONE),
             "model, transform, pose", "Draws a SkinnedMesh<V> at transform, deformed by pose before its vert runs. "
             "Optional texture, vert, frag and draw values follow the pose. Reuse the same pose for color and shadow passes.");
    add_setting("Draw", "Clip", "tide_draw_push_clip", "tide_draw_set_clip", NULL, "rect",
                "Only what's inside `rect` draws: from (x, y) to (x + width, y + height), in the units the 2D draws "
                "are in. In a block, it's what of `rect` is inside the clip there is.",
                1, &T_RECT);
    add_unset("Draw", "SetClip", "tide_draw_unset_clip", "No clip, as each frame starts: everything draws again.");
    // The checker takes these itself (check_window_target): they're here for editors
    static const char window_target_doc[] =
        "A render target that's always the window's size, or that part of it (`RenderTarget.Window(0.5)` is half as "
        "wide and half as high), at the display's full resolution. It's made again, empty, whenever the window's "
        "size or the display's scale changes, so it's for what a view draws again every frame, like a pass over "
        "the whole screen, and it's never resized by hand. `RenderTarget.Window(samples: 4)` draws each of its pixels "
        "with 4 samples (1, 2, 4 or 8), which smooths the edges of the triangles drawn into it: a frame drawn into "
        "one and brought to the window with `Draw.Blit` is an antialiased frame.";
    describe(add("RenderTarget", "Window", T_TARGET, "tide_render_target_window", 0, T_NONE, T_NONE, T_NONE), NULL,
             window_target_doc);
    describe(add("RenderTarget", "Window", T_TARGET, "tide_render_target_screen", 1, T_F, T_NONE, T_NONE), "factor",
             window_target_doc);
    static const char window_format_doc[] =
        "A render target that's always the window's size, or that part of it, which holds what its formats say, one "
        "for each of its color images, four at the most: `RenderTarget.Window(Format.Rgba8, Format.Rgba16f)` holds "
        "two, which a frag's `out Color` parameters go to in order. `Format.Depth`, by itself, is depth alone, "
        "with no color image, for a pass whose depth a frag reads afterwards (`target.depth`). It's made again, "
        "empty, whenever the window's size or the display's scale changes. Its samples come last, by name: "
        "`RenderTarget.Window(Format.Rgba16f, samples: 4)`.";
    describe(add("RenderTarget", "Window", T_TARGET, "tide_render_target_window_as", 1, T_FORMAT, T_NONE, T_NONE), "format",
             window_format_doc);
    describe(add("RenderTarget", "Window", T_TARGET, "tide_render_target_screen_as", 2, T_F, T_FORMAT, T_NONE), "factor, format",
             window_format_doc);

    describe(add("Draw", "Cube", T_NONE, "tide_draw_cube", 3, T_F3, T_F3, T_COLOR),
             "center, size, color", "A filled cube, centered in the world, with the full size along each axis.");
    signature *cube = add("Draw", "Cube", T_NONE, "tide_draw_cube_rotated", 3, T_F3, T_F3, T_Q);
    cube->argc = 4;
    cube->params[3] = T_COLOR;
    describe(cube, "center, size, rotation, color", "A filled cube, rotated around its center, with the full size along each axis.");
    describe(add("Draw", "Sphere", T_NONE, "tide_draw_sphere", 3, T_F3, T_F, T_COLOR),
             "center, radius, color", "A filled sphere, centered in the world.");
    signature *cylinder = add("Draw", "Cylinder", T_NONE, "tide_draw_cylinder", 3, T_F3, T_F, T_F);
    cylinder->argc = 4;
    cylinder->params[3] = T_COLOR;
    describe(cylinder, "center, radius, height, color", "A filled cylinder, centered in the world, along the y axis.");
    signature *rotated_cylinder = add("Draw", "Cylinder", T_NONE, "tide_draw_cylinder_rotated", 3, T_F3, T_F, T_F);
    rotated_cylinder->argc = 5;
    rotated_cylinder->params[3] = T_Q;
    rotated_cylinder->params[4] = T_COLOR;
    describe(rotated_cylinder, "center, radius, height, rotation, color", "A filled cylinder, rotated around its center from the y axis.");
    describe(add("Draw", "Plane", T_NONE, "tide_draw_plane", 3, T_F3, T_F2, T_COLOR),
             "center, size, color", "A filled plane, centered in the world, with its size along x and z.");
    signature *plane = add("Draw", "Plane", T_NONE, "tide_draw_plane_rotated", 3, T_F3, T_F2, T_Q);
    plane->argc = 4;
    plane->params[3] = T_COLOR;
    describe(plane, "center, size, rotation, color", "A filled plane, rotated around its center from the xz plane.");

    describe(add("Draw", "WorldToScreen", T_F2, "tide_draw_world_to_screen", 1, T_F2, T_NONE, T_NONE),
             "position", "A world point through the current 2D camera, in screen pixels from the top left, with y down. "
             "A float3 goes through the 3D camera instead.");
    describe(add("Draw", "ScreenToWorld", T_F2, "tide_draw_screen_to_world", 1, T_F2, T_NONE, T_NONE),
             "position", "A screen point in pixels from the top left, with y down, through the current 2D camera to the "
             "world. A float3, with a depth in z, goes through the 3D camera instead.");
    describe(add("Draw", "WorldToScreen", T_F3, "tide_draw_world_to_screen_3d", 1, T_F3, T_NONE, T_NONE),
             "position", "A world point through the current 3D camera: screen pixels from the top left, with y down, "
             "and normalized depth in z (0 at the near plane, 1 at the far plane).");
    describe(add("Draw", "ScreenToWorld", T_F3, "tide_draw_screen_to_world_3d", 1, T_F3, T_NONE, T_NONE),
             "position", "A screen point through the current 3D camera to the world: x and y in pixels from the top left, "
             "with y down, and normalized depth in z (0 at the near plane, 1 at the far plane).");
    describe(add("Draw", "ScreenRayOrigin", T_F3, "tide_draw_screen_ray_origin", 1, T_F2, T_NONE, T_NONE),
             "position", "The world origin on the near plane of a ray through this screen point in pixels from the top left.");
    describe(add("Draw", "ScreenRayDirection", T_F3, "tide_draw_screen_ray_direction", 1, T_F2, T_NONE, T_NONE),
             "position", "The normalized world direction of a ray through this screen point in pixels from the top left.");
    // The cameras as matrices: what the conversions above go through
    describe(add("Draw", "WorldToClip", T_F4X4, "tide_draw_world_to_clip", 0, T_NONE, T_NONE, T_NONE),
             "", "The current 3D camera as one matrix, from the world to clip space, fitted to the shape of what it "
             "draws into: what the GPU multiplies a 3D mesh's positions by, the turn of z included. Keep a target's "
             "for a frag that looks a pixel up in what the target saw, as a shadow map's does.");
    describe(add("Draw", "CameraTransform", T_F4X4, "tide_draw_camera_transform", 0, T_NONE, T_NONE, T_NONE),
             "", "Where the current 3D camera is in the world, as a matrix: it looks along its +z, with +y up. "
             "`Draw.Camera(Draw.CameraTransform(), Draw.CameraProjection())` is the same camera.");
    describe(add("Draw", "CameraProjection", T_F4X4, "tide_draw_camera_projection", 0, T_NONE, T_NONE, T_NONE),
             "", "The current 3D camera's projection, fitted to the shape of what it draws into: from what the camera "
             "sees, looking down -z, to clip space, as `float4x4.PerspectiveFov` and `float4x4.Ortho` make them.");
    describe(add("Draw", "WorldToScreenMatrix", T_F3X3, "tide_draw_world_to_screen_matrix", 0, T_NONE, T_NONE, T_NONE),
             "", "The current 2D camera as a matrix: what `WorldToScreen` does to a float2 as (x, y, 1), into screen "
             "pixels from the top left, with y down. The identity inside `Screen`.");
    // The window's height comes with the call (see gen_expr)
    describe(add("Screen", "Flip", T_F2, "tide_draw_screen_flip", 1, T_F2, T_NONE, T_NONE),
             "position", "The same point counted from the screen's other edge. Devices count pixels from the bottom "
             "left, with y up, and the screen, the GUI and `ScreenToWorld` from the top left, with y down: "
             "`Screen.Flip(devices.pointer.position)` is where the pointer is on the screen. It's its own inverse, so "
             "it takes a screen position to the devices' too.");

    // Text's methods: tide/text.h, with the text first. Positions and lengths
    // count characters, and are clamped to the text, never out of range.
    const type t_int = {TY_INT, NULL};
    // Debug: output for whoever runs the game, which changes nothing in the match
    describe(add("Debug", "Log", T_NONE, "tide_debug_log", 1, T_STR, T_NONE, T_NONE), "text",
             "Prints a line: to the terminal under `tide run`, the browser's console on the web and the system's log on "
             "Android. Anywhere: systems, views, handlers, functions. A tick run again after a rollback logs again, so "
             "prefix match state with `Time.tick`.");
    describe(defaulted(add("Debug", "Assert", T_NONE, "tide_debug_assert", 2, T_BOOL, T_STR, T_NONE), "\"\""), "condition, message",
             "Nothing while the condition holds. When it doesn't, the program stops, naming the file, the line, the "
             "source line and the message, if it has one.");
    describe(add("string", "Contains", T_BOOL, "tide_str_contains", 1, T_STR, T_NONE, T_NONE),
             "value", "Whether `value` is in the text.");
    describe(add("string", "StartsWith", T_BOOL, "tide_str_starts_with", 1, T_STR, T_NONE, T_NONE),
             "value", "Whether the text starts with `value`.");
    describe(add("string", "EndsWith", T_BOOL, "tide_str_ends_with", 1, T_STR, T_NONE, T_NONE),
             "value", "Whether the text ends with `value`.");
    describe(add("string", "IndexOf", t_int, "tide_str_index_of", 1, T_STR, T_NONE, T_NONE),
             "value", "Where `value` first is in the text, counting characters from 0, or -1.");
    describe(add("string", "Substring", T_STR, "tide_str_substring_from", 1, t_int, T_NONE, T_NONE),
             "start", "The text from character `start` on.");
    describe(add("string", "Substring", T_STR, "tide_str_substring", 2, t_int, t_int, T_NONE),
             "start, length", "`length` characters of the text, from character `start`.");
    describe(add("string", "ToUpper", T_STR, "tide_str_to_upper", 0, T_NONE, T_NONE, T_NONE),
             NULL, "The text in upper case: ASCII letters only, for now.");
    describe(add("string", "ToLower", T_STR, "tide_str_to_lower", 0, T_NONE, T_NONE, T_NONE),
             NULL, "The text in lower case: ASCII letters only, for now.");
    describe(add("string", "Trim", T_STR, "tide_str_trim", 0, T_NONE, T_NONE, T_NONE),
             NULL, "The text without spaces, tabs and new lines at its start and end.");
    describe(add("string", "Replace", T_STR, "tide_str_replace", 2, T_STR, T_STR, T_NONE),
             "from, to", "The text with every `from` in it replaced by `to`.");
    // The same of one character, a char: after the text ones, which a call with text picks
    describe(add("string", "Contains", T_BOOL, "tide_str_contains_char", 1, T_CHR, T_NONE, T_NONE),
             "character", "Whether the character is in the text.");
    describe(add("string", "StartsWith", T_BOOL, "tide_str_starts_with_char", 1, T_CHR, T_NONE, T_NONE),
             "character", "Whether the text starts with the character.");
    describe(add("string", "EndsWith", T_BOOL, "tide_str_ends_with_char", 1, T_CHR, T_NONE, T_NONE),
             "character", "Whether the text ends with the character.");
    describe(add("string", "IndexOf", t_int, "tide_str_index_of_char", 1, T_CHR, T_NONE, T_NONE),
             "character", "Where the character first is in the text, counting characters from 0, or -1.");
    describe(add("string", "Replace", T_STR, "tide_str_replace_char", 2, T_CHR, T_CHR, T_NONE),
             "from, to", "The text with every character `from` in it replaced by `to`; `'\\0'`, which is no character, takes them out.");

    // Text as bytes and in pieces (tide/list.h), and numbers out of text (tide/text.h): the same on every
    // platform, so the match can use them
    describe(add("string", "ToBytes", T_BYTES, "tide_str_to_bytes", 0, T_NONE, T_NONE, T_NONE),
             NULL, "The text's bytes, as UTF-8: what `string.FromBytes` reads back.");
    describe(add("string", "Split", T_PATHS, "tide_str_split", 1, T_STR, T_NONE, T_NONE),
             "separator", "The pieces of the text between each `separator` and the next, empty ones too: one piece, the "
             "whole text, when the separator isn't in it or is empty.");
    describe(add("string", "Split", T_PATHS, "tide_str_split_char", 1, T_CHR, T_NONE, T_NONE),
             "separator", "The pieces of the text between each of that character and the next, empty ones too.");
    add_gui("int", "TryParse", T_BOOL, "tide_str_parse_int", "string text, out int value", 0,
            "Whether the whole text is an int, which goes to `value`: digits, with a `-` before them for a negative "
            "one. Anything else (spaces, a `+`, a point, a number past an int's range) gives false, and 0.");
    add_gui("long", "TryParse", T_BOOL, "tide_str_parse_long", "string text, out long value", 0,
            "Whether the whole text is a long, which goes to `value`: digits, with a `-` before them for a negative "
            "one. Anything else gives false, and 0.");
    add_gui("float", "TryParse", T_BOOL, "tide_str_parse_float", "string text, out float value", 0,
            "Whether the whole text is a number, which goes to `value` as the nearest float: `-12.5`, `.5`, `1e-3`. "
            "The same bits on every platform. Anything else (spaces, a `+`, `NaN`, a number past a float's range) "
            "gives false, and 0.");

    // The GUI, in views: tide/gui.h. Codegen passes the view's GUI first, then
    // the widget's ID, then a mut argument's address.
    add_widget("Label", T_NONE, "label", "string text", 0, "Text.");
    add_widget("Button", T_BOOL, "button", "string text", GUI_ID, "A button. Returns whether it was pressed.");
    add_widget("Toggle", T_BOOL, "toggle", "string text, mut bool value", GUI_ID,
               "A checkbox for `value`. Returns whether it changed it.");
    add_widget("Slider", T_BOOL, "slider", "string label, mut float value, float min, float max", GUI_ID,
               "A slider for `value`, from `min` to `max`. Returns whether it changed it.");
    add_widget("IntSlider", T_BOOL, "int_slider", "string label, mut int value, int min, int max", GUI_ID,
               "A slider for a whole number, from `min` to `max`. Returns whether it changed it.");
    add_widget("IntField", T_BOOL, "int_field", "string label, mut int value", GUI_ID,
               "A field to type a whole number into. Returns whether it changed `value`.");
    add_widget("FloatField", T_BOOL, "float_field", "string label, mut float value", GUI_ID,
               "A field to type a number into. Returns whether it changed `value`.");
    add_widget("Float2Field", T_BOOL, "float2_field", "string label, mut float2 value", GUI_ID,
               "Fields for a float2's x and y. Returns whether they changed `value`.");
    add_widget("Float3Field", T_BOOL, "float3_field", "string label, mut float3 value", GUI_ID,
               "Fields for a float3's x, y and z. Returns whether they changed `value`.");
    add_widget("Float4Field", T_BOOL, "float4_field", "string label, mut float4 value", GUI_ID,
               "Fields for a float4's x, y, z and w. Returns whether they changed `value`.");
    add_widget("ColorField", T_BOOL, "color_field", "string label, mut Color value", GUI_ID,
               "A color's swatch, and fields for its r, g, b and a. Returns whether they changed `value`.");
    add_widget("TextField", T_BOOL, "text_field", "string label, mut string value", GUI_ID,
               "A field to type text into. It changes `value` as the player types, and returns whether it did.");
    // ...and the ones that say whether the player entered what they typed: tide_gui_<field>_entered
    add_widget("TextField", T_BOOL, "text_field_entered", "string label, mut string value, out bool entered", GUI_ID,
               "With `out entered`, it also says whether the player entered the text: true in the frame Enter ends "
               "the typing in this field, like `GUILayout.TextField(\"Code\", lobby.code, out var entered);`.");
    add_widget("IntField", T_BOOL, "int_field_entered", "string label, mut int value, out bool entered", GUI_ID,
               "With `out entered`, it also says whether the player entered the number: true in the frame Enter, or "
               "a gamepad's south button, ends the typing in this field.");
    add_widget("FloatField", T_BOOL, "float_field_entered", "string label, mut float value, out bool entered", GUI_ID,
               "With `out entered`, it also says whether the player entered the number: true in the frame Enter, or "
               "a gamepad's south button, ends the typing in this field.");
    add_gui("GUILayout", "Space", T_NONE, "tide_gui_layout_space", "float size", 0,
            "Empty space: down in a vertical container, across in a horizontal one.");
    add_gui("GUILayout", "Vertical", T_NONE, "tide_gui_begin_vertical", "", GUI_ID | GUI_CONTAINER,
            "Stacks the widgets in its block top to bottom.");
    add_gui("GUILayout", "Horizontal", T_NONE, "tide_gui_begin_horizontal", "", GUI_ID | GUI_CONTAINER,
            "Puts the widgets in its block side by side.");
    add_gui("GUILayout", "Area", T_NONE, "tide_gui_begin_area_at", "Anchor anchor", GUI_ID | GUI_CONTAINER,
            "A panel on the screen, sized to its block's widgets, at one of nine anchors like `Anchor.MiddleCenter`.");
    add_gui("GUILayout", "Area", T_NONE, "tide_gui_begin_area", "Rect rect", GUI_ID | GUI_CONTAINER,
            "A panel on the screen at `rect`, with its block's widgets laid out inside.");
    add_gui("GUILayout", "Modal", T_NONE, "tide_gui_begin_modal", "Anchor anchor, mut bool open",
            GUI_ID | GUI_CONTAINER | GUI_SKIPS,
            "A panel over the whole screen while `open` is true, like a pause menu. While it's up, the widgets "
            "outside it don't work, the game and views get no input, and back (Escape or the east button) closes "
            "it.");
    add_gui("GUI", "Disabled", T_NONE, "tide_gui_begin_disabled", "bool disabled", GUI_CONTAINER,
            "Grays out the widgets in its block while `disabled` is true: they're drawn faded and can't be "
            "clicked, focused or typed into. They stay where they are, laid out as usual.");
    // A widget a view draws itself: the GUI handles its input, and draws nothing.
    // Generated C has it as Tide's GUIControl (tide_gui_control_value), and a
    // theme as tide/gui.h's (tide_gui_set_theme_value, tide_gui_begin_theme_value).
    add_gui("GUI", "Control", T_CONTROL, "tide_gui_control_value", "Rect rect", GUI_ID,
            "A widget this view draws itself, at `rect`: the GUI handles its input as a button's, and draws "
            "nothing. It says whether it's `hovered`, `pressed` (the pointer holds it), `focused` (the keyboard and "
            "gamepad are on it) and `clicked`. A press on it never reaches the input's `Sample` or the other views; "
            "Tab, the arrows, the d-pad and the left stick move the focus to it with the other widgets, and Enter, "
            "Space and the south button click it. `GUI.Disabled` and modals stop it like any widget.");
    add_gui("GUI", "Navigate", T_NONE, "tide_gui_navigate", "", 0,
            "A menu is up: the arrows, the d-pad and the left stick can start moving the focus between widgets, "
            "though the match reads the devices. Without it they only move a focus that's there, which Tab starts, "
            "so they never take a HUD's button away from the game. Call it every frame the menu is up.");
    add_gui("GUI", "SetTheme", T_NONE, "tide_gui_set_theme_value", "GUITheme theme", 0,
            "How the GUI's own widgets look from here on: in this view and the ones after it, until it's set again "
            "or the frame ends. Every frame starts with the default look. For some widgets alone, wrap them: "
            "`GUI.Theme(theme) { ... }`.");
    add_gui("GUI", "Theme", T_NONE, "tide_gui_begin_theme_value", "GUITheme theme", GUI_CONTAINER,
            "How the GUI's own widgets look in its block: after it, they look as they did before. For the views "
            "that follow too, `GUI.SetTheme(theme)`.");
    // Widgets that read the devices themselves, like a library's: what they use of them
    add_gui("GUI", "ClaimPointer", T_NONE, "tide_gui_claim_pointer_at", "Rect rect", 0,
            "This view has a widget of its own at `rect`: while the pointer is on it, the mouse's buttons and "
            "scroll, the pointer's press and the primary touch are hidden from the input's `Sample` and from the "
            "other views, as on a button, and this view goes on reading them. Call it every frame the widget is "
            "there: a press on it is the view's from the frame it lands in, a finger's too.");
    add_gui("GUI", "ClaimPointer", T_NONE, "tide_gui_claim_pointer", "", 0,
            "This view is using the pointer, wherever it is, like a widget of its own that's being dragged: the "
            "mouse's buttons and scroll, the pointer's press and the primary touch are hidden from the input's "
            "`Sample` and from the other views, and this view goes on reading them. Call it every frame it "
            "does: a claim hides from the next frame on. For a widget at a rect, give the rect.");
    add_gui("GUI", "ClaimKeyboard", T_NONE, "tide_gui_claim_keyboard", "", 0,
            "This view is using the keyboard, for a widget of its own: the keys and what's typed are hidden from "
            "the input's `Sample` and from the other views, as typing in a field is. This view goes on reading "
            "them. Call it every frame the widget has the keyboard: a claim hides from the next frame on.");
    add_gui("GUI", "ShowKeyboard", T_NONE, "tide_gui_show_keyboard", "", 0,
            "The player is typing into a widget of this view's own: phones show their keyboard, as they do for a "
            "text field. Call it every frame they are.");
    // The mouse's cursor: a frame's ask, as the keyboard's is, which the GUI collects
    add_gui("Cursor", "Lock", T_NONE, "tide_gui_lock_cursor", "", 0,
            "Locks the mouse's cursor for a camera that looks around: it's hidden and stays where it is, and "
            "`devices.mouse.delta` keeps coming however far the mouse goes. Call it every frame the game wants the "
            "mouse: the cursor comes back once no view asks. The system may refuse, or take the cursor back (when "
            "the player presses Escape, until their next click or key press), so read `Cursor.locked` for whether it "
            "holds.");

    describe(add("File", "Exists", T_BOOL, "tide_file_exists", 1, T_STR, T_NONE, T_NONE), "path",
             "Whether a file exists on this machine. Returns false for missing or inaccessible paths.");
    describe(add("File", "Create", T_NONE, "tide_file_create", 1, T_STR, T_NONE, T_NONE), "path", "Creates or replaces an empty file. Fails with IOError.");
    describe(add("File", "ReadAllText", T_STR, "tide_file_read_all_text", 1, T_STR, T_NONE, T_NONE), "path", "Reads the whole file as UTF-8 text. Fails with IOError.");
    describe(add("File", "WriteAllText", T_NONE, "tide_file_write_all_text", 2, T_STR, T_STR, T_NONE), "path, contents", "Creates or replaces a UTF-8 text file. Fails with IOError.");
    describe(add("File", "AppendAllText", T_NONE, "tide_file_append_all_text", 2, T_STR, T_STR, T_NONE), "path, contents", "Appends UTF-8 text, creating the file if missing. Fails with IOError.");
    describe(add("File", "ReadAllBytes", T_BYTES, "tide_file_read_all_bytes", 1, T_STR, T_NONE, T_NONE), "path", "Reads the whole file into List<byte>. Fails with IOError.");
    describe(add("File", "WriteAllBytes", T_NONE, "tide_file_write_all_bytes", 2, T_STR, T_BYTES, T_NONE), "path, contents", "Creates or replaces a binary file. Fails with IOError.");
    describe(defaulted(add("File", "Copy", T_NONE, "tide_file_copy", 3, T_STR, T_STR, T_BOOL), "false"), "source, destination, overwrite",
             "Copies a file, over one that's there only with `overwrite`. Fails with IOError.");
    describe(defaulted(add("File", "Move", T_NONE, "tide_file_move", 3, T_STR, T_STR, T_BOOL), "false"), "source, destination, overwrite",
             "Moves a file, over one that's there only with `overwrite`. Fails with IOError.");
    describe(add("File", "Delete", T_NONE, "tide_file_delete", 1, T_STR, T_NONE, T_NONE), "path", "Deletes a file. A missing file succeeds. Fails with IOError.");
    describe(add("Directory", "Exists", T_BOOL, "tide_directory_exists", 1, T_STR, T_NONE, T_NONE), "path", "Whether a directory exists on this machine.");
    describe(add("Directory", "CreateDirectory", T_NONE, "tide_directory_create_directory", 1, T_STR, T_NONE, T_NONE), "path", "Creates missing parents; an existing directory succeeds. Fails with IOError.");
    describe(defaulted(add("Directory", "GetFiles", T_PATHS, "tide_directory_get_files", 2, T_STR, T_BOOL, T_NONE), "false"), "path, recursive",
             "Lists file paths sorted by name, with `recursive` those of the folders inside too. Fails with IOError.");
    describe(defaulted(add("Directory", "GetDirectories", T_PATHS, "tide_directory_get_directories", 2, T_STR, T_BOOL, T_NONE), "false"),
             "path, recursive", "Lists directory paths sorted by name, with `recursive` those inside them too. Fails with IOError.");
    describe(defaulted(add("Directory", "Delete", T_NONE, "tide_directory_delete", 2, T_STR, T_BOOL, T_NONE), "false"), "path, recursive",
             "Deletes a directory: an empty one, or with `recursive` everything in it. Fails with IOError.");
    describe(add("Directory", "Move", T_NONE, "tide_directory_move", 2, T_STR, T_STR, T_NONE), "source, destination", "Moves a directory, refusing an existing destination. Fails with IOError.");
    describe(add("Path", "Combine", T_STR, "tide_path_combine", 2, T_STR, T_STR, T_NONE), "first, second", "Combines two paths without accessing storage.");
    describe(add("Path", "GetFileName", T_STR, "tide_path_get_file_name", 1, T_STR, T_NONE, T_NONE), "path", "Returns the last path segment.");
    describe(add("Path", "GetDirectoryName", T_STR, "tide_path_get_directory_name", 1, T_STR, T_NONE, T_NONE), "path", "Returns the parent path, or an empty string.");
    describe(add("Path", "GetExtension", T_STR, "tide_path_get_extension", 1, T_STR, T_NONE, T_NONE), "path", "Returns the extension, including its dot, or an empty string.");
    describe(add("Storage", "Flush", T_NONE, "tide_storage_flush_begin", 0, T_NONE, T_NONE, T_NONE), NULL, "Await until preceding filesystem changes are committed to persistent storage. Fails with IOError.");
    // Web.Request(url) is in no table: it's a call of Tide's own code (see web_prelude in builtins.h)
    describe(add("Web", "Fetch", T_BYTES, "tide_web_fetch_begin", 1, T_STR, T_NONE, T_NONE), "url",
             "Await a URL's bytes, fetched over HTTP or HTTPS by this machine: `var bytes = await Web.Fetch(url)`. "
             "Local code only, like File. Fails with IOError: NotFound for a page that isn't there (404), InvalidPath "
             "for a URL that isn't http:// or https://, and Failed for anything else (no connection, another status, "
             "a body over 64 MiB, or on the web a server whose CORS refuses the page).");
}

// ---------------------------------------------------------------------------
// Static members: constants reached through a type or Math.

static const struct {
    const char *owner;
    const char *member;
    type_kind kind;
    const char *c_constant;
} members[] = {
    {"Application", "title", TY_STRING, "tide_str_from_cstr(tide_game_title)"},
    {"Application", "persistentDataPath", TY_STRING, "tide_application_persistent_data_path(tide_l->tide_io)"},
    {"Application", "temporaryCachePath", TY_STRING, "tide_application_temporary_cache_path(tide_l->tide_io)"},
    {"Math", "PI", TY_FLOAT, "TIDE_PI_F"},
    {"Math", "TAU", TY_FLOAT, "TIDE_TAU_F"},
    {"Math", "E", TY_FLOAT, "TIDE_E_F"},
    {"quaternion", "identity", TY_QUATERNION, "tide_identity_q()"},
    {"float2x2", "identity", TY_FLOAT2X2, "tide_identity_f2x2()"},
    {"float3x3", "identity", TY_FLOAT3X3, "tide_identity_f3x3()"},
    {"float4x4", "identity", TY_FLOAT4X4, "tide_identity_f4x4()"},
    {"Color", "white", TY_COLOR, "TIDE_COLOR_WHITE"},
    {"Color", "black", TY_COLOR, "TIDE_COLOR_BLACK"},
    {"Color", "red", TY_COLOR, "TIDE_COLOR_RED"},
    {"Color", "green", TY_COLOR, "TIDE_COLOR_GREEN"},
    {"Color", "blue", TY_COLOR, "TIDE_COLOR_BLUE"},
    {"Color", "yellow", TY_COLOR, "TIDE_COLOR_YELLOW"},
    {"Color", "cyan", TY_COLOR, "TIDE_COLOR_CYAN"},
    {"Color", "magenta", TY_COLOR, "TIDE_COLOR_MAGENTA"},
    {"Color", "gray", TY_COLOR, "TIDE_COLOR_GRAY"},
    {"Color", "clear", TY_COLOR, "TIDE_COLOR_CLEAR"},
    {"Color", "none", TY_COLOR, "TIDE_COLOR_NONE"},
    {"Clip", "none", TY_CLIP, "((tide_clip_ref){0})"},
    {"Clip", "Microphone", TY_CLIP, "TIDE_CLIP_MICROPHONE"},
    {"Draw", "window", TY_RENDER_TARGET, "((tide_render_target){.id = TIDE_RENDER_WINDOW})"},
    {"Draw", "list", TY_DRAW_LIST, "tide_draw_render_target(tide_draw, (tide_render_target){.id = TIDE_RENDER_WINDOW})"},
    {"Screen", "width", TY_FLOAT, "tide_ui->width"},
    {"Screen", "height", TY_FLOAT, "tide_ui->height"},
    // In views and the functions they call. The input's Sample reads it of its own devices (see check_member).
    {"Cursor", "locked", TY_BOOL, "tide_ui->devices.mouse.locked"},
    // In local code and the functions it calls: what the host said of this frame (tide_local_frame_time)
    {"Frame", "dt", TY_FLOAT, "tide_l->tide_frame_seconds"},
};

#define MEMBER_COUNT (sizeof members / sizeof members[0])

// What editors say of members[i], for the ones a type and a name don't explain; else NULL.
static const char *member_doc(const size_t i)
{
    if (strcmp(members[i].owner, "Color") == 0 && strcmp(members[i].member, "none") == 0) {
        return "No color, where one can be left out: a `Shape`'s `color2` by default, for a fill of one color. It's "
               "not a color to draw with: its alpha is under 0, so nothing shows.";
    }
    if (strcmp(members[i].owner, "Clip") == 0 && strcmp(members[i].member, "none") == 0) {
        return "No clip, which is a `Clip`'s default: a `Sound` with it plays nothing, or, with an `effect`, is a source "
               "that effect makes sound for from silence.";
    }
    if (strcmp(members[i].owner, "Clip") == 0 && strcmp(members[i].member, "Microphone") == 0) {
        return "The microphone, as a clip a `Sound` plays: the voice is what the capture device hears, mono, which its "
               "`effect` can filter and its bus plays or mutes (`volume` 0 to only process). The device is open while a "
               "frame names such a `Sound` playing, and the system asks the player first on phones and the web: until "
               "they allow it, silence. A one-shot (`Audio.Play`) of it plays nothing.";
    }
    if (strcmp(members[i].owner, "Cursor") == 0) {
        return "Whether the window holds the mouse's cursor right now: what the system says, not what `Cursor.Lock` "
               "asked. A browser only locks it at a click or a key press, Escape gives the cursor back to the player "
               "until the next one, and a window that loses the focus lets it go too. Views read it, and the input's "
               "`Sample`.";
    }
    if (strcmp(members[i].owner, "Frame") == 0) {
        return "The seconds since this machine's previous frame started, as its host gave them: what moves things "
               "that aren't the match's, like particles and menus, at the same speed whatever the display's rate. "
               "It's there with no match too. Local code reads it: views, local handlers and tasks, and the "
               "functions they call. The match's is `Time.dt`, a tick's length. It's all `Frame` has: for a clock, add "
               "it up in a field of local state and wrap it where it repeats.";
    }
    return NULL;
}

// ---------------------------------------------------------------------------

bool builtin_draw_owner(const str name)
{
    return str_eq_c(name, "Draw");
}

bool builtin_old_draw_owner(const str name)
{
    return str_eq_c(name, "Draw2D") || str_eq_c(name, "Draw3D");
}

bool builtin_frame_clock(const str name)
{
    // In lower case: a clock is asked for as a field ('time') and as a function ('Time()')
    static const char *const clocks[] = {
        "t", "time", "now", "clock", "seconds", "elapsed", "elapsedtime", "elapsedseconds", "totaltime", "totalseconds",
        "sincestart", "timesincestart", "timesincestartup", "realtimesincestartup", "realtime", "unscaledtime",
        "timeasdouble", "timestamp", "uptime", "runtime",
    };
    for (size_t i = 0; i < sizeof clocks / sizeof clocks[0]; i++) {
        if (strlen(clocks[i]) != (size_t)name.len) continue;
        bool same = true;
        for (int k = 0; k < name.len && same; k++) same = tolower((unsigned char)name.ptr[k]) == clocks[i][k];
        if (same) return true;
    }
    return false;
}

bool builtin_owner(const str name)
{
    type ignored;
    return str_eq_c(name, "Math") || builtin_draw_owner(name) || builtin_old_draw_owner(name) || str_eq_c(name, "GUI") || str_eq_c(name, "GUILayout")
        || str_eq_c(name, "Screen") || str_eq_c(name, "Cursor") || str_eq_c(name, "Frame") || str_eq_c(name, "Debug")
        || str_eq_c(name, "File") || str_eq_c(name, "Directory") || str_eq_c(name, "Path")
        || str_eq_c(name, "Application") || str_eq_c(name, "Storage") || str_eq_c(name, "Web") || str_eq_c(name, "Audio")
        || str_eq_c(name, "Json") || str_eq_c(name, "Audience")
        || builtin_type_named(name, &ignored);
}

// Widest type of a component-wise call's arguments, or false if they don't fit
// together. `default` takes whatever type the others give.
static bool unify(const expr *e, const bool ints_ok, type *out)
{
    int dim = 1;
    bool is_float = !ints_ok;
    bool color = false;
    for (int i = 0; i < e->args.count; i++) {
        if (e->args.items[i]->kind == E_DEFAULT) continue;
        const type t = e->args.items[i]->type;
        if (t.kind == TY_COLOR) { // Channel by channel, alpha too: with colors and numbers, never a vector
            color = true;
            continue;
        }
        if (t.kind == TY_CHAR) continue; // Its code point, an int, as a char is wherever an int goes
        if (!type_is_numeric(t)) return false;
        const int d = type_dim(t);
        if (d > 1) {
            if (dim > 1 && d != dim) return false;
            dim = d;
        }
        if (type_is_float_based(t)) is_float = true;
    }
    if (color && dim > 1) return false;
    *out = color ? T_COLOR : vector_type(is_float, dim);
    return true;
}

// Whether the arguments are ints and longs, with a long among them: then the
// call is the long version, and the ints widen.
static bool unify_long(const expr *e, type *out)
{
    bool any_long = false;
    for (int i = 0; i < e->args.count; i++) {
        if (e->args.items[i]->kind == E_DEFAULT) continue;
        const type t = e->args.items[i]->type;
        if (!type_is_integer(t)) return false;
        any_long |= t.kind == TY_LONG;
    }
    *out = (type){TY_LONG, NULL};
    return any_long;
}

// Whether an argument written `out var entered` or `out _` waits for its
// parameter to say its type.
static bool out_untyped(const expr *arg)
{
    return arg->is_out && arg->out_local && arg->out_local->type_name.len == 0;
}

static void arg_list(const expr *e, char *buf, const size_t size)
{
    size_t len = 0;
    buf[0] = '\0';
    for (int i = 0; i < e->args.count && len + 1 < size; i++) {
        const expr *arg = e->args.items[i];
        const char *what = arg->kind == E_DEFAULT ? "default" : !out_untyped(arg) ? type_name(arg->type)
                         : arg->out_local->discard ? "_" : "var";
        const int n = snprintf(buf + len, size - len, "%s%s%s", i ? ", " : "", arg->is_out ? "out " : "", what);
        if (n > 0) len += (size_t)n;
    }
}

// "Draw.Circle(float2 center, float radius, Color color)", plus " -> float" for results.
static void format_signature(const signature *s, sb *out)
{
    sb_printf(out, "%s.%s(", s->owner, s->name);
    const char *names = s->param_names;
    for (int a = 0; a < s->argc; a++) {
        sb_printf(out, "%s%s%s", a ? ", " : "", s->out & (1u << a) ? "out " : s->mut & (1u << a) ? "mut " : "",
                  signature_type_name(s->params[a]));
        if (names) {
            const char *end = strchr(names, ',');
            const size_t n = end ? (size_t)(end - names) : strlen(names);
            sb_put(out, " ");
            sb_putn(out, names, n);
            names = end ? end + 2 : NULL;
        }
        if (s->defaults[a]) sb_printf(out, " = %s", s->defaults[a]); // One a call can leave out
    }
    if (s->gui & (GUI_CONTAINER | DRAW_BLOCK)) sb_printf(out, "%sBlock content", s->argc ? ", " : "");
    sb_put(out, ")");
    if (s->result.kind != TY_VOID) sb_printf(out, " -> %s", signature_type_name(s->result));
    if ((strcmp(s->owner, "File") == 0 || strcmp(s->owner, "Directory") == 0) && strcmp(s->name, "Exists") != 0)
        sb_put(out, " fails IOError");
    if (strcmp(s->owner, "Storage") == 0 || strcmp(s->owner, "Web") == 0) sb_put(out, " fails IOError");
}

// The parameter of `s` named `name`, or -1.
static int param_named(const signature *s, const str name)
{
    int index = 0;
    for (const char *p = s->param_names; p && *p; index++) {
        const char *end = strchr(p, ',');
        const size_t n = end ? (size_t)(end - p) : strlen(p);
        if ((size_t)name.len == n && memcmp(name.ptr, p, n) == 0) return index < s->argc ? index : -1;
        p = end ? end + 2 : NULL;
    }
    return -1;
}

// Whether a call writes names on its arguments that the engine's functions
// take. Mesh and Instanced go by what each argument is, so theirs stay in order.
static bool names_args(const expr *e)
{
    return e->arg_names.count > 0 && !str_eq_c(e->name, "Mesh") && !str_eq_c(e->name, "Instanced")
        && !str_eq_c(e->name, "SkinnedMesh");
}

// Which parameter of `s` each argument of a call is for, as in C#: in order,
// then by the names written. The parameters a call leaves out are the last
// ones. False when the arguments can't be `s`'s.
static bool bind(const signature *s, const expr *e, int *param_of)
{
    const int written = e->args.count;
    if (written > s->argc || written < s->argc - s->optional) return false;
    bool given[MAX_PARAMS] = {false};
    bool named = false;
    for (int i = 0; i < written; i++) {
        const str name = names_args(e) ? e->arg_names.items[i] : (str){"", 0};
        int at = i;
        if (name.len > 0) {
            named = true;
            at = param_named(s, name);
            if (at < 0) return false;
        } else if (named) {
            return false; // After a named one, each is named
        }
        if (given[at]) return false;
        given[at] = true;
        param_of[i] = at;
    }
    for (int k = 0; k < written; k++) {
        if (!given[k]) return false;
    }
    return true;
}

// Whether signature `s` takes the call's arguments. `default`, unchecked yet,
// fits any parameter but a mut one, which changes a variable. An argument
// written `out` goes where it's written, whatever the parameter there is and
// whatever type it declares: what's wrong with it is said once the version is
// picked, as for a function of the game's (check_out_param).
static bool fits(const signature *s, const expr *e)
{
    int param_of[MAX_PARAMS];
    if (!bind(s, e, param_of)) return false;
    for (int i = 0; i < e->args.count; i++) {
        const int a = param_of[i];
        const type want = real_type(s->params[a]);
        const expr *arg = e->args.items[i];
        const bool mut = s->mut & (1u << a);
        if (arg->enum_shorthand) {
            if (mut || want.kind != TY_ENUM) return false;
            continue;
        }
        if (arg->is_out && (out_untyped(arg) || (s->out & (1u << a)))) continue;
        // A type the program doesn't have, like Grid2<Color> where no grid is one: nothing is it
        if (mesh_type(want)) return false;
        if (arg->kind == E_DEFAULT) {
            if (mut) return false;
            continue;
        }
        // A mut argument is the variable itself, so its type matches exactly.
        if (mut ? want.kind != arg->type.kind || want.decl != arg->type.decl : !type_assignable(want, arg->type)) {
            return false;
        }
    }
    return true;
}

// Whether a later version of signatures[first]'s function fits the call too,
// with another type where a `default` is: then nothing says which one it
// means. Reports it.
static bool default_ambiguous(const int first, const expr *e)
{
    const signature *s = &signatures[first];
    for (int j = first + 1; j < signature_count; j++) {
        const signature *other = &signatures[j];
        if (strcmp(other->owner, s->owner) != 0 || strcmp(other->name, s->name) != 0 || !fits(other, e)) continue;
        int mine[MAX_PARAMS];
        int theirs[MAX_PARAMS];
        bind(s, e, mine);
        bind(other, e, theirs);
        for (int i = 0; i < e->args.count; i++) {
            const type t = real_type(s->params[mine[i]]);
            const type u = real_type(other->params[theirs[i]]);
            if ((e->args.items[i]->kind != E_DEFAULT && !e->args.items[i]->enum_shorthand)
                || (t.kind == u.kind && t.decl == u.decl)) continue;
            diag_error(e->args.items[i]->at, "'%s' could be %s or %s here: %s.%s has a version for each",
                       e->args.items[i]->enum_shorthand ? "enum shorthand" : "default",
                       signature_type_name(s->params[mine[i]]), signature_type_name(other->params[theirs[i]]), s->owner, s->name);
            diag_note("write the value itself instead, so the call says which version it means");
            return true;
        }
    }
    return false;
}

bool builtin_list_param(const str owner, const str name, const int index, const expr *list, type *out)
{
    build_signatures();
    // The type its first element names, if it names one
    const expr *first = list && list->args.count > 0 ? list->args.items[0] : NULL;
    const bool named = first && first->kind == E_LITERAL;
    bool found = false, several = false, picked = false;
    for (int i = 0; i < signature_count; i++) {
        const signature *s = &signatures[i];
        if (!str_eq_c(owner, s->owner) || !str_eq_c(name, s->name) || s->argc <= index) continue;
        const type t = real_type(s->params[index]);
        if (t.kind != TY_LIST || mesh_type(t)) return false;
        const type element = t.decl->fields.items[0].type;
        if (named && element.kind == TY_STRUCT && str_eq(element.decl->name, first->name)) {
            *out = t;
            picked = true;
        }
        if (picked) continue;
        if (found && t.decl != out->decl) several = true;
        *out = t;
        found = true;
    }
    return picked || (found && !several);
}

type resolve_builtin_call(const str owner, expr *e)
{
    int defaults = 0; // `default` arguments take the type of the version the others pick
    for (int i = 0; i < e->args.count; i++) {
        const expr *arg = e->args.items[i];
        if (arg->kind == E_DEFAULT || arg->enum_shorthand) defaults++;
        // `out var entered` takes its parameter's type, where a version has an `out` parameter
        else if (out_untyped(arg) && builtin_takes_out(owner, e->name)) continue;
        else if (arg->type.kind == TY_ERROR) return (type){TY_ERROR, NULL};
    }
    e->call = CALL_BUILTIN;
    e->arg_want.count = 0;

    if (str_eq_c(owner, "Math")) {
        for (size_t i = 0; i < COMPONENTWISE_COUNT; i++) {
            if (!str_eq_c(e->name, componentwise[i].name)) continue;
            if (e->args.count != componentwise[i].argc) {
                diag_error(e->at, "Math.%s takes %d argument%s, not %d", componentwise[i].name, componentwise[i].argc,
                           componentwise[i].argc == 1 ? "" : "s", e->args.count);
                return (type){TY_ERROR, NULL};
            }
            if (defaults == e->args.count) {
                diag_error(e->args.items[0]->at, "Math.%s takes numbers and vectors of any size, so 'default' could be any of them",
                           componentwise[i].name);
                diag_note("write the value itself instead, like '0' or 'float3(0)'");
                return (type){TY_ERROR, NULL};
            }
            type target;
            if (componentwise[i].ints && unify_long(e, &target)) { // Longs, with ints: the long version
                for (int a = 0; a < e->args.count; a++) vec_push(e->arg_want, target);
                e->c_callee = c_function(componentwise[i].name, target);
                return target;
            }
            if (!unify(e, componentwise[i].ints, &target)) {
                char args[256];
                arg_list(e, args, sizeof args);
                diag_error(e->at, "Math.%s can't take (%s)", componentwise[i].name, args);
                diag_note("it takes numbers, vectors and colors; vectors in one call must be the same size, and a color "
                          "goes with colors and numbers");
                return (type){TY_ERROR, NULL};
            }
            for (int a = 0; a < e->args.count; a++) vec_push(e->arg_want, target);
            e->c_callee = c_function(componentwise[i].name, target);
            return target;
        }
    }

    build_signatures();
    bool known = false;
    for (int i = 0; i < signature_count; i++) {
        const signature *s = &signatures[i];
        if (!str_eq_c(owner, s->owner) || !str_eq_c(e->name, s->name)) continue;
        known = true;
        if (!fits(s, e)) continue;
        if (defaults > 0 && default_ambiguous(i, e)) return (type){TY_ERROR, NULL};
        for (int a = 0; a < s->argc; a++) vec_push(e->arg_want, real_type(s->params[a]));
        // Arguments written with names go in the parameters' order: what was
        // written stays in `written`, and is worked out in that order
        if (names_args(e)) {
            int param_of[MAX_PARAMS];
            bind(s, e, param_of);
            bool in_order = true;
            for (int k = 1; k < e->args.count; k++) in_order &= param_of[k - 1] < param_of[k];
            if (!in_order) {
                expr **ordered = arena_alloc(sizeof(expr *) * (size_t)e->args.count);
                for (int k = 0; k < e->args.count; k++) {
                    ordered[param_of[k]] = e->args.items[k];
                    vec_push(e->arg_order, param_of[k]);
                }
                e->settled = true;
                e->written.items = e->args.items;
                e->written.count = e->written.cap = e->args.count;
                e->args.items = ordered;
                e->args.cap = e->args.count;
            }
            e->names_taken = true;
        }
        e->c_callee = s->c_name;
        e->c_pack = s->pack;
        e->arg_mut = s->mut;
        e->arg_out = s->out;
        e->gui = s->gui;
        return real_type(s->result);
    }

    if (!known) {
        // There was a NormalizeSafe while Normalize gave NaN for a zero vector
        if (str_eq_c(owner, "Math") && str_eq_c(e->name, "NormalizeSafe")) {
            diag_error(e->at, "Math.NormalizeSafe is gone: write Math.Normalize");
            diag_note("Math.Normalize no longer gives NaN for a vector too short to normalize: it gives zero, and "
                      "the identity for a quaternion");
            suggest_replacement(e->name, "Normalize");
            return (type){TY_ERROR, NULL};
        }
        diag_error(e->at, STR_FMT " has no %s '" STR_FMT "'", STR_ARG(owner), str_eq_c(owner, "Random") ? "method" : "function",
                   STR_ARG(e->name));
        if (str_eq_c(owner, "Draw")) {
            diag_note("Draw has the window's shapes, text, meshes and primitives, its settings (Blend, Tint, Transform, "
                      "Clip, Camera, Screen, Depth, Cull, Stencil), the conversions, the cameras' matrices, MeasureText, Blit, "
                      "window and list; "
                      "a render target has the same, as its methods");
        }
        suggestion s = suggest_start(e->name);
        for (size_t i = 0; i < COMPONENTWISE_COUNT && str_eq_c(owner, "Math"); i++) {
            suggest_consider_c(&s, componentwise[i].name);
        }
        for (int i = 0; i < signature_count; i++) {
            if (str_eq_c(owner, signatures[i].owner)) suggest_consider_c(&s, signatures[i].name);
        }
        suggest_note(&s);
        return (type){TY_ERROR, NULL};
    }
    char args[256];
    arg_list(e, args, sizeof args);
    diag_error(e->at, "no version of " STR_FMT "." STR_FMT " takes (%s)", STR_ARG(owner), STR_ARG(e->name), args);
    bool changes = false;
    for (int i = 0; i < signature_count; i++) {
        const signature *s = &signatures[i];
        if (!str_eq_c(owner, s->owner) || !str_eq_c(e->name, s->name)) continue;
        sb line = {0};
        format_signature(s, &line);
        diag_note("%s", line.data);
        changes |= s->mut != 0;
    }
    if (changes) diag_note("a mut argument is the variable it changes, so its type is exactly the parameter's");
    // A text's anchor and its weight: which comes first, and what measuring takes of them
    int anchor_at = -1, font_at = -1;
    for (int i = 0; i < e->args.count; i++) {
        const type t = e->args.items[i]->type;
        if (t.kind == TY_ENUM && t.decl == anchor_decl && anchor_at < 0) anchor_at = i;
        if (t.kind == TY_ENUM && t.decl == font_decl && font_at < 0) font_at = i;
    }
    if (str_eq_c(owner, "Draw") && str_eq_c(e->name, "Text") && font_at >= 0 && anchor_at > font_at) {
        diag_note("the anchor comes before the font: 'Draw.Text(text, position, size, color, Anchor.MiddleCenter, "
                  "Font.Bold);'");
    } else if (str_eq_c(owner, "Draw") && str_eq_c(e->name, "Text") && (anchor_at >= 0 || font_at >= 0)
               && (anchor_at >= 0 ? anchor_at : font_at) < 4) {
        diag_note("an anchor and a font come after the color: 'Draw.Text(text, position, size, Color.white, "
                  "Anchor.MiddleCenter);'");
    }
    if (str_eq_c(owner, "Draw") && str_eq_c(e->name, "MeasureText") && anchor_at >= 0) {
        diag_note("a text is as big at every anchor: MeasureText gives its box, which an anchor names a place of, so "
                  "leave the anchor out");
    }
    if (str_eq_c(owner, "Draw") && str_eq_c(e->name, "Shape")) {
        diag_note("a Shape is written with the fields it sets, like 'Draw.Shape(Shape { center = position, size = "
                  "float2(4, 2), radius = 1, color = Color.red });'");
    }
    if (str_eq_c(owner, "GUI") && (str_eq_c(e->name, "SetTheme") || str_eq_c(e->name, "Theme"))) {
        diag_note("a GUITheme is written with the fields it changes, like 'GUI." STR_FMT "(GUITheme { control = Color.red, "
                  "radius = 8 })%s'", STR_ARG(e->name), str_eq_c(e->name, "Theme") ? " { ... }" : ";");
    }
    return (type){TY_ERROR, NULL};
}

type resolve_builtin_member(const str owner, expr *e)
{
    for (size_t i = 0; i < MEMBER_COUNT; i++) {
        if (str_eq_c(owner, members[i].owner) && str_eq_c(e->member, members[i].member)) {
            e->c_constant = members[i].c_constant;
            return (type){members[i].kind, NULL};
        }
    }
    diag_error(e->at, STR_FMT " has no member '" STR_FMT "'", STR_ARG(owner), STR_ARG(e->member));
    suggestion s = suggest_start(e->member);
    for (size_t i = 0; i < MEMBER_COUNT; i++) {
        if (str_eq_c(owner, members[i].owner)) suggest_consider_c(&s, members[i].member);
    }
    suggest_note(&s);
    return (type){TY_ERROR, NULL};
}

bool builtin_takes_out(const str owner, const str name)
{
    build_signatures();
    for (int i = 0; i < signature_count; i++) {
        const signature *s = &signatures[i];
        if (s->out && str_eq_c(owner, s->owner) && str_eq_c(name, s->name)) return true;
    }
    return false;
}

bool builtin_out_versions(const str owner, const str name, sb *names, sb *example)
{
    build_signatures();
    const signature *with[8]; // Each function once, in the order they were added
    int count = 0;
    bool known = false;
    for (int i = 0; i < signature_count; i++) {
        const signature *s = &signatures[i];
        if (!str_eq_c(owner, s->owner)) continue;
        if (str_eq_c(name, s->name)) known = true;
        if (!s->out) continue;
        bool listed = false;
        for (int k = 0; k < count; k++) listed = listed || strcmp(with[k]->name, s->name) == 0;
        if (!listed && count < 8) with[count++] = s;
    }
    if (!known || count == 0) return false;
    for (int k = 0; k < count; k++) sb_printf(names, "%s%s", k == 0 ? "" : k == count - 1 ? " and " : ", ", with[k]->name);
    sb_printf(example, "%s.%s(", with[0]->owner, with[0]->name);
    const char *p = with[0]->param_names;
    for (int a = 0; a < with[0]->argc && p; a++) {
        const char *end = strchr(p, ',');
        sb_printf(example, "%s%s", a ? ", " : "", with[0]->out & (1u << a) ? "out var " : "");
        sb_putn(example, p, end ? (size_t)(end - p) : strlen(p));
        p = end ? end + 2 : NULL;
    }
    sb_put(example, ")");
    return true;
}

str builtin_param_name(const expr *e, const int index)
{
    for (int i = 0; i < signature_count; i++) {
        const signature *s = &signatures[i];
        if (s->c_name != e->c_callee) continue; // Each version has its own
        const char *p = s->param_names;
        for (int k = 0; p && *p; k++) {
            const char *end = strchr(p, ',');
            if (k == index) return (str){p, (int)(end ? (size_t)(end - p) : strlen(p))};
            p = end ? end + 2 : NULL;
        }
    }
    return str_from("value");
}

const char *builtin_function_owner(const str name)
{
    build_signatures();
    for (size_t i = 0; i < COMPONENTWISE_COUNT; i++) {
        if (str_eq_c(name, componentwise[i].name)) return "Math";
    }
    for (int i = 0; i < signature_count; i++) {
        if (str_eq_c(name, signatures[i].name)) return signatures[i].owner;
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// For editors

static const char *componentwise_doc(const char *name)
{
    static const struct {
        const char *name;
        const char *doc;
    } docs[] = {
        {"Clamp", "Clamps `x` between `a` and `b`, component by component. NaN gives `a`."},
        {"Min", "The smaller of `a` and `b`, component by component. If one is NaN, the other."},
        {"Max", "The larger of `a` and `b`, component by component. If one is NaN, the other."},
        {"Lerp", "Linear interpolation: `a + (b - a) * t`, component by component. Between two colors, channel by channel."},
        {"Unlerp", "The `t` for which Lerp(a, b, t) gives `x`."},
        {"SmoothStep", "Smooth Hermite interpolation between 0 and 1 as `x` goes from `a` to `b`."},
        {"Step", "1 where `x >= edge`, 0 elsewhere."},
        {"Saturate", "Clamps between 0 and 1. NaN gives 0."},
        {"Round", "Rounds to the nearest integer; ties go to even."},
        {"Frac", "The fractional part: `x - Floor(x)`."},
        {"Rsqrt", "1 / Sqrt(x)."},
        {"Radians", "Degrees to radians."},
        {"Degrees", "Radians to degrees."},
        {"Atan2", "The angle of the point (x, y) in radians. Takes (y, x), as in Unity."},
    };
    for (size_t i = 0; i < sizeof docs / sizeof docs[0]; i++) {
        if (strcmp(docs[i].name, name) == 0) return docs[i].doc;
    }
    return "Component by component, on numbers, vectors and colors.";
}

static const char *componentwise_params(const int argc, const char *name)
{
    if (strcmp(name, "Clamp") == 0) return "x, a, b";
    if (strcmp(name, "Lerp") == 0) return "a, b, t";
    if (strcmp(name, "Unlerp") == 0 || strcmp(name, "SmoothStep") == 0) return "a, b, x";
    if (strcmp(name, "Step") == 0) return "edge, x";
    if (strcmp(name, "Atan2") == 0) return "y, x";
    if (strcmp(name, "Pow") == 0) return "x, y";
    return argc == 1 ? "x" : argc == 2 ? "a, b" : "a, b, c";
}

// "Circle(${1:center}, ${2:radius}, ${3:color})" from "center, radius, color":
// the first `count` parameters, the ones every call writes.
static void format_snippet(const char *name, const char *param_names, const int count, sb *out)
{
    sb_printf(out, "%s(", name);
    int index = 1;
    for (const char *p = param_names; p && *p && index <= count;) {
        const char *end = strchr(p, ',');
        const size_t n = end ? (size_t)(end - p) : strlen(p);
        sb_printf(out, "%s${%d:", index > 1 ? ", " : "", index);
        sb_putn(out, p, n);
        sb_put(out, "}");
        index++;
        p = end ? end + 2 : NULL;
    }
    sb_put(out, ")");
}

void builtin_list_members(const str owner, void (*visit)(void *user, const builtin_member *m), void *user)
{
    build_signatures();

    if (str_eq_c(owner, "Math")) {
        for (size_t i = 0; i < COMPONENTWISE_COUNT; i++) {
            const char *params = componentwise_params(componentwise[i].argc, componentwise[i].name);
            sb detail = {0};
            sb_printf(&detail, "Math.%s(%s)", componentwise[i].name, params);
            sb snippet = {0};
            format_snippet(componentwise[i].name, params, componentwise[i].argc, &snippet);
            const builtin_member m = {componentwise[i].name, true, detail.data, componentwise_doc(componentwise[i].name),
                                      snippet.data};
            visit(user, &m);
        }
    }

    for (int i = 0; i < signature_count; i++) {
        const signature *s = &signatures[i];
        if (!str_eq_c(owner, s->owner)) continue;
        bool seen = false; // Overloads appear once, under their first signature.
        for (int j = 0; j < i; j++) {
            if (strcmp(signatures[j].owner, s->owner) == 0 && strcmp(signatures[j].name, s->name) == 0) seen = true;
        }
        if (seen) continue;
        sb detail = {0};
        format_signature(s, &detail);
        // Its other versions: how many, after the first one's signature, and each under the first one's doc,
        // with what it says of its own
        sb doc = {0};
        const char *said[MAX_SIGNATURES];
        int said_count = 0;
        int others = 0;
        for (int j = i + 1; j < signature_count; j++) {
            const signature *o = &signatures[j];
            if (strcmp(o->owner, s->owner) != 0 || strcmp(o->name, s->name) != 0) continue;
            if (others++ == 0 && s->doc) {
                sb_put(&doc, s->doc);
                said[said_count++] = s->doc;
            }
            sb_put(&doc, "\n\n```tide\n");
            format_signature(o, &doc);
            sb_put(&doc, "\n```");
            bool repeats = !o->doc;
            for (int k = 0; k < said_count && !repeats; k++) repeats = strcmp(said[k], o->doc) == 0;
            if (repeats) continue;
            sb_printf(&doc, "\n%s", o->doc);
            said[said_count++] = o->doc;
        }
        if (others) sb_printf(&detail, " (+%d version%s)", others, others == 1 ? "" : "s");
        sb snippet = {0};
        if (s->param_names) format_snippet(s->name, s->param_names, s->argc - s->optional, &snippet);
        else sb_printf(&snippet, "%s($1)", s->name);
        if (s->gui & (GUI_CONTAINER | DRAW_BLOCK)) sb_put(&snippet, "\n{\n\t$0\n}");
        const builtin_member m = {s->name, true, detail.data, others ? doc.data : s->doc, snippet.data};
        visit(user, &m);
    }

    for (size_t i = 0; i < MEMBER_COUNT; i++) {
        if (!str_eq_c(owner, members[i].owner)) continue;
        sb detail = {0};
        sb_printf(&detail, "%s.%s: %s", members[i].owner, members[i].member, type_name((type){members[i].kind, NULL}));
        const builtin_member m = {members[i].member, false, detail.data, member_doc(i), NULL};
        visit(user, &m);
    }
}

void builtin_signatures(const str owner, const str name, void (*visit)(void *user, const char *label, const char *doc),
                        void *user)
{
    build_signatures();
    for (size_t i = 0; i < COMPONENTWISE_COUNT && str_eq_c(owner, "Math"); i++) {
        if (!str_eq_c(name, componentwise[i].name)) continue;
        sb label = {0};
        sb_printf(&label, "Math.%s(%s)", componentwise[i].name,
                  componentwise_params(componentwise[i].argc, componentwise[i].name));
        visit(user, label.data, componentwise_doc(componentwise[i].name));
        return;
    }
    for (int i = 0; i < signature_count; i++) {
        const signature *s = &signatures[i];
        if (!str_eq_c(owner, s->owner) || !str_eq_c(name, s->name)) continue;
        sb label = {0};
        format_signature(s, &label);
        visit(user, label.data, s->doc);
    }
}

bool builtin_describe(const str owner, const str name, sb *out)
{
    build_signatures();
    bool found = false;

    for (size_t i = 0; i < COMPONENTWISE_COUNT && str_eq_c(owner, "Math"); i++) {
        if (!str_eq_c(name, componentwise[i].name)) continue;
        sb_printf(out, "Math.%s(%s)\n", componentwise[i].name,
                  componentwise_params(componentwise[i].argc, componentwise[i].name));
        sb_printf(out, "\n%s", componentwise_doc(componentwise[i].name));
        return true;
    }

    // Each version's doc once, as versions share them (Draw.Mesh's in 2D and in 3D)
    const char *docs[MAX_SIGNATURES];
    int doc_count = 0;
    for (int i = 0; i < signature_count; i++) {
        const signature *s = &signatures[i];
        if (!str_eq_c(owner, s->owner) || !str_eq_c(name, s->name)) continue;
        format_signature(s, out);
        sb_put(out, "\n");
        bool seen = !s->doc;
        for (int j = 0; j < doc_count && !seen; j++) seen = docs[j] == s->doc;
        if (!seen) docs[doc_count++] = s->doc;
        found = true;
    }
    if (found) {
        for (int j = 0; j < doc_count; j++) sb_printf(out, "\n%s\n", docs[j]);
        return true;
    }

    for (size_t i = 0; i < MEMBER_COUNT; i++) {
        if (str_eq_c(owner, members[i].owner) && str_eq_c(name, members[i].member)) {
            sb_printf(out, "%s.%s: %s", members[i].owner, members[i].member, type_name((type){members[i].kind, NULL}));
            if (member_doc(i)) sb_printf(out, "\n\n%s", member_doc(i));
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Settings

static const setting settings[] = {
    {"maxRewind", TY_FLOAT, "0", "Maximum historical physics query age in seconds. Zero disables collider history. "
     "Input viewTime is engine-stamped and server-validated; games still validate firing and aim."},
    {"title", TY_STRING, "the game's name",
     "The window's title. Without it, it's the game's name: its folder's under tide, its target's under CMake. "
     "`--title` and CMake's `TITLE` take precedence."},
    {"tickRate", TY_INT, "60",
     "How many times a second the match ticks, from 1 to 1000: `Time.dt` is 1 / tickRate, and code reads the rate "
     "as `Time.tickRate`. The machine that runs "
     "the match decides, and every player ticks at its rate. A new rate takes effect when a match starts."},
    {"maxPlayers", TY_INT, "16",
     "How many players a match takes, from 1 to 256. The world keeps an input for each, and every player's "
     "input goes to every other each tick, so a match's traffic grows with the square of its players. Builds "
     "with different maxPlayers can't play together."},
    {"appId", TY_STRING, "dev.tide.<the game's name>",
     "The app's ID on phones, like `com.studio.game`: what Android and iOS, and their app stores, know it by, the "
     "same for every version. Parts of letters and digits, each starting with a letter, between dots. Without it, "
     "builds are `dev.tide.<the game's name>`, which is fine for testing; `tide build --android --release` needs one."},
    {"version", TY_STRING, "1.0",
     "The version people see, like `1.2.0`: one to three numbers between dots, which Android and iOS both take. "
     "Each build's version code, which app stores order updates by, goes up by itself."},
    {"hostMigration", TY_BOOL, "false",
     "When the machine running a room's match leaves or stops answering, another player's machine takes the match "
     "over from the last tick it verified, and the others join it again as the same players. Private scenes the new "
     "host wasn't in are lost, so a game with host migration keeps no secrets. `Session.End()` ends a match instead."},
    {"prediction", TY_ENUM, "Prediction.Rollback",
     "How clients keep up with the match. `Prediction.Rollback`: a client runs ahead of the server with no input "
     "delay, guessing other players' inputs, and runs ticks again when a guess was wrong; when that costs more than a "
     "frame's budget, it delays its input a tick at a time until it fits. `Prediction.Delay`: clients never predict: "
     "inputs are delayed so every player's reach the server before its tick, and ticks run once, as the server "
     "confirms them, at the cost of input delay."},
    {"stats", TY_BOOL, "false",
     "Shows the stats in the window's bottom right corner: one line, the frame rate, and the ping in a match another "
     "machine runs. A click or a tap on it, or F8, opens it to the rest: what a frame's work takes, the bandwidth, "
     "the tick, the entity count, the threads, the memory, what draws and in how many draw calls, and the systems "
     "that took longest in the last tick. Nothing is measured for them until then. It's part of the build, so what "
     "`tide build` makes shows them too: take it out before players get the game, unless the frame rate is theirs "
     "to see. `tide run --stats` shows them open."},
    {"debugViews", TY_BOOL, "false",
     "The engine's debug views are on in the game: F7 shows the profiler, which says where a frame's time goes (the "
     "match's ticks, each view, drawing) and a tick's (each system, then its changes and events). Nothing is measured "
     "until it's shown. Starting the game with `--debug-views` turns them on too, whatever this says."},
};

#define SETTING_COUNT (sizeof settings / sizeof settings[0])

const setting *setting_named(const str name)
{
    for (size_t i = 0; i < SETTING_COUNT; i++) {
        if (str_eq_c(name, settings[i].name)) return &settings[i];
    }
    return NULL;
}

const setting *settings_list(int *count)
{
    *count = (int)SETTING_COUNT;
    return settings;
}

static const char web_prelude_text[] =
    "// What Web.Request(url) gives: a request to send, built up a call at a time.\n"
    "// Each call gives the request with that one thing changed, so they chain:\n"
    "// 'await Web.Request(url).SetMethod(\"POST\").SetBody(bytes).Send()'.\n"
    "struct WebRequest\n"
    "{\n"
    "    string url;\n"
    "    string method = \"GET\";\n"
    "    string headers; // A line each: \"Name: value\"\n"
    "    List<byte> body;\n"
    "    float timeout;  // Seconds its answer may take; 0 or less is no limit\n"
    "\n"
    "    // The request with this method, like \"POST\" or \"DELETE\".\n"
    "    WebRequest SetMethod(string value)\n"
    "    {\n"
    "        return WebRequest { url = url, method = value, headers = headers, body = body, timeout = timeout };\n"
    "    }\n"
    "\n"
    "    // The request with this header too, like SetHeader(\"Content-Type\", \"text/plain\").\n"
    "    WebRequest SetHeader(string name, string value)\n"
    "    {\n"
    "        var line = $\"{name.Replace(\"\\n\", \" \")}: {value.Replace(\"\\n\", \" \")}\\n\";\n"
    "        return WebRequest { url = url, method = method, headers = $\"{headers}{line}\", body = body, timeout = timeout };\n"
    "    }\n"
    "\n"
    "    // The request with these bytes as its body.\n"
    "    WebRequest SetBody(List<byte> bytes)\n"
    "    {\n"
    "        return WebRequest { url = url, method = method, headers = headers, body = bytes, timeout = timeout };\n"
    "    }\n"
    "\n"
    "    // The request with a time limit: Send fails with IOError.TimedOut when its\n"
    "    // whole answer hasn't come in this many seconds. 0 or less is no limit.\n"
    "    WebRequest SetTimeout(float seconds)\n"
    "    {\n"
    "        return WebRequest { url = url, method = method, headers = headers, body = body, timeout = seconds };\n"
    "    }\n"
    "}\n"
    "\n"
    "// What a request's Send gives: the answer's status, like 200 or 404, and its body.\n"
    "struct WebResponse\n"
    "{\n"
    "    int status;\n"
    "    List<byte> body;\n"
    "}\n"
    "\n"
    "WebRequest " WEB_REQUEST_MAKER "(string url)\n"
    "{\n"
    "    return WebRequest { url = url };\n"
    "}\n";

const char *web_prelude(size_t *len)
{
    *len = sizeof web_prelude_text - 1;
    return web_prelude_text;
}

static bool has_text(const char *text, const size_t len, const char *word)
{
    const size_t n = strlen(word);
    for (size_t i = 0; i + n <= len; i++) {
        if (text[i] == word[0] && memcmp(text + i, word, n) == 0) return true;
    }
    return false;
}

bool web_prelude_wanted(const char *text, const size_t len)
{
    // IOError.TimedOut is a request's own error, which only a program with this code has (add_builtins in check.c)
    return has_text(text, len, "Web.Request") || has_text(text, len, "WebRequest") || has_text(text, len, "WebResponse")
        || has_text(text, len, "IOError.TimedOut");
}
