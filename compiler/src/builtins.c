#include "builtins.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "types.h"

// Every built-in math function maps to a tide/math.h function named
// tide_<lowercase name>_<type suffix>, for example Math.Dot on float3 is
// tide_dot_f3. Formulas and semantics follow Unity.Mathematics. Draw functions
// map to tide/draw.h, and GUI and GUILayout to tide/gui.h.

static const type T_F = {TY_FLOAT, NULL};
static const type T_F2 = {TY_FLOAT2, NULL};
static const type T_F3 = {TY_FLOAT3, NULL};
static const type T_COLOR = {TY_COLOR, NULL};
static const type T_STR = {TY_STRING, NULL};
static const type T_Q = {TY_QUATERNION, NULL};
static const type T_F4X4 = {TY_FLOAT4X4, NULL};
static const type T_NONE = {TY_VOID, NULL};
static const type T_BOOL = {TY_BOOL, NULL};

// The Anchor enum in signatures. The table outlives programs, so it can't
// hold the enum's declaration: calls match it against the program's own.
static const type T_ANCHOR = {TY_ENUM, NULL};
static const decl *anchor_decl;

static const decl *vertices_decl, *vertices3_decl, *indices_decl, *pixels_decl, *filter_decl, *transforms_decl;
static const decl *bytes_decl, *paths_decl;
static decl io_bytes, io_paths;
static const type T_BYTES = {TY_LIST, &io_bytes};
static const type T_PATHS = {TY_LIST, &io_paths};

void builtins_use_io(const decl *bytes, const decl *paths)
{
    bytes_decl = bytes;
    paths_decl = paths;
}

void builtins_use(const decl *anchor)
{
    anchor_decl = anchor;
    // Another program's: this one's come with its first mesh draw
    vertices_decl = vertices3_decl = indices_decl = pixels_decl = filter_decl = transforms_decl = NULL;
    bytes_decl = paths_decl = NULL;
}

// What Draw2D.Mesh and Draw3D.Mesh take, the same way: these stand for the program's own
// List<Vertex>, List<Vertex3>, List<int>, Grid2<Color> and Filter.
static decl mesh_vertices, mesh_vertices3, mesh_indices, mesh_pixels, mesh_filter, mesh_transforms, mesh_instances;
static const type T_VERTICES = {TY_LIST, &mesh_vertices};
static const type T_VERTICES3 = {TY_LIST, &mesh_vertices3};
static const type T_INDICES = {TY_LIST, &mesh_indices};
static const type T_PIXELS = {TY_GRID, &mesh_pixels};
static const type T_FILTER = {TY_ENUM, &mesh_filter};
static const type T_TRANSFORMS = {TY_LIST, &mesh_transforms};
// Editor signatures for ordinary instance structs: the checker resolves these itself.
static const type T_INSTANCES = {TY_LIST, &mesh_instances};
static const type T_RECT = {TY_RECT, NULL};
static const type T_TEXTURE = {TY_TEXTURE, NULL};

void builtins_use_mesh(const decl *vertices, const decl *vertices3, const decl *indices, const decl *pixels,
                       const decl *filter, const decl *transforms)
{
    vertices_decl = vertices;
    vertices3_decl = vertices3;
    indices_decl = indices;
    pixels_decl = pixels;
    filter_decl = filter;
    transforms_decl = transforms;
}

// Whether `t` stands for one of the program's types Draw2D.Mesh and Draw3D.Mesh take.
static bool mesh_type(const type t)
{
    return t.decl == &mesh_vertices || t.decl == &mesh_vertices3 || t.decl == &mesh_indices || t.decl == &mesh_pixels
        || t.decl == &mesh_filter || t.decl == &mesh_transforms || t.decl == &mesh_instances;
}

// A signature's type as a program sees it.
static type real_type(const type t)
{
    if (t.decl == &io_bytes && bytes_decl) return (type){TY_LIST, (decl *)bytes_decl};
    if (t.decl == &io_paths && paths_decl) return (type){TY_LIST, (decl *)paths_decl};
    if (t.kind == TY_ENUM && !t.decl) return (type){TY_ENUM, (decl *)anchor_decl};
    if (t.decl == &mesh_vertices && vertices_decl) return (type){TY_LIST, (decl *)vertices_decl};
    if (t.decl == &mesh_vertices3 && vertices3_decl) return (type){TY_LIST, (decl *)vertices3_decl};
    if (t.decl == &mesh_indices && indices_decl) return (type){TY_LIST, (decl *)indices_decl};
    if (t.decl == &mesh_pixels && pixels_decl) return (type){TY_GRID, (decl *)pixels_decl};
    if (t.decl == &mesh_filter && filter_decl) return (type){TY_ENUM, (decl *)filter_decl};
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
    if (t.decl == &mesh_transforms) return "List<float4x4>";
    if (t.decl == &mesh_instances) return "List<T>";
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
    int gui;                 // GUI_ID and GUI_CONTAINER
} signature;

#define MAX_SIGNATURES 288

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
    *s = (signature){owner, name, result, argc, {p0, p1, p2, T_NONE, T_NONE, T_NONE}, permanent(c_name), NULL, NULL, 0, 0};
    return s;
}

// A GUI function from its parameters as Tide writes them:
// "string label, mut float value, float min, float max".
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
        const bool mut = strncmp(buf, "mut ", 4) == 0;
        char *type_text = buf + (mut ? 4 : 0);
        char *space = strchr(type_text, ' ');
        *space = '\0';
        type t = {TY_ERROR, NULL};
        if (strcmp(type_text, "string") == 0) t = T_STR;
        else if (strcmp(type_text, "Anchor") == 0) t = T_ANCHOR;
        else builtin_type_named(str_from(type_text), &t);
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
                 "value's bytes, without the top bit.");
    }
    describe(add("Math", "Hash", (type){TY_INT, NULL}, "tide_hash_l", 1, (type){TY_LONG, NULL}, T_NONE, T_NONE), "value",
             "A well-mixed int from 0 to 2147483647, the same for the same value on every machine: xxHash32 of the "
             "long's 8 bytes, without the top bit.");

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
    describe(add("quaternion", "LookRotation", T_Q, "tide_lookrotation_q", 2, T_F3, T_F3, T_NONE),
             "forward, up", "A rotation that looks along `forward`, with `up` as up.");
    describe(add("float4x4", "TRS", T_F4X4, "tide_trs_f4x4", 3, T_F3, T_Q, T_F3),
             "translation, rotation, scale", "A transform: scale, then rotate, then translate.");
    describe(add("float4x4", "Translate", T_F4X4, "tide_translate_f4x4", 1, T_F3, T_NONE, T_NONE),
             "translation", "A translation matrix.");
    signature *perspective = add("float4x4", "PerspectiveFov", T_F4X4, "tide_perspectivefov_f4x4", 3, T_F, T_F, T_F);
    perspective->argc = 4;
    perspective->params[3] = T_F;
    describe(perspective, "verticalFov, aspect, near, far",
             "A perspective projection, for `Draw3D.Camera(transform, projection)`: `verticalFov` in radians, and "
             "`aspect` the screen's width over its height. It takes what a camera sees, looking down -z as "
             "OpenGL's and Unity's views do, to the screen, from `near` to `far`.");
    signature *ortho = add("float4x4", "Ortho", T_F4X4, "tide_ortho_f4x4", 3, T_F, T_F, T_F);
    ortho->argc = 4;
    ortho->params[3] = T_F;
    describe(ortho, "width, height, near, far",
             "An orthographic projection, for `Draw3D.Camera(transform, projection)`: a box `width` by `height` "
             "around the camera's axis, from `near` to `far`, with no perspective.");

    // Drawing, in views: tide/draw.h. Codegen passes the view's draw list first.
    describe(add("Draw2D", "Clear", T_NONE, "tide_draw_clear", 1, T_COLOR, T_NONE, T_NONE),
             "color", "Fills the whole screen.");
    describe(add("Draw3D", "Clear", T_NONE, "tide_draw_clear", 1, T_COLOR, T_NONE, T_NONE),
             "color", "Fills the whole screen and clears depth.");
    describe(add("Draw2D", "Camera", T_NONE, "tide_draw_camera", 2, T_F2, T_F, T_NONE),
             "center, size",
             "Sets the camera for the Draw2D calls after it. `center` is the world position at the middle of the "
             "screen and `size` is half the visible height, like Unity's orthographic size.");
    describe(add("Draw3D", "Camera", T_NONE, "tide_draw_camera_3d", 3, T_F3, T_Q, T_F),
             "position, rotation, fieldOfView",
             "Sets the 3D camera for the 3D meshes after it: where it is, which way it looks (along its rotation's "
             "+z), and how much it sees up and down, in degrees, like Unity's Camera.fieldOfView. It sees from 0.3 "
             "units in front of it on, however far.");
    describe(add("Draw3D", "Camera", T_NONE, "tide_draw_camera_matrices", 2, T_F4X4, T_F4X4, T_NONE),
             "transform, projection",
             "Sets the 3D camera with any projection: `transform` places the camera in the world, looking along "
             "its +z, and `projection` (`float4x4.PerspectiveFov`, `float4x4.Ortho`) takes what it sees to the "
             "screen, the screen's shape included.");
    describe(add("Draw2D", "Circle", T_NONE, "tide_draw_circle", 3, T_F2, T_F, T_COLOR),
             "center, radius, color", "A filled circle.");
    describe(add("Draw2D", "WireCircle", T_NONE, "tide_draw_wire_circle", 3, T_F2, T_F, T_COLOR),
             "center, radius, color", "A circle outline.");
    describe(add("Draw2D", "Rect", T_NONE, "tide_draw_rect", 3, T_F2, T_F2, T_COLOR),
             "center, size, color", "A filled rectangle.");
    describe(add("Draw2D", "WireRect", T_NONE, "tide_draw_wire_rect", 3, T_F2, T_F2, T_COLOR),
             "center, size, color", "A rectangle outline.");
    describe(add("Draw2D", "Line", T_NONE, "tide_draw_line", 3, T_F2, T_F2, T_COLOR),
             "from, to, color", "A line.");
    signature *text = add("Draw2D", "Text", T_NONE, "tide_draw_text", 3, T_STR, T_F2, T_F);
    text->argc = 4;
    text->params[3] = T_COLOR;
    describe(text, "text, position, size, color", "Text: `position` is its top left corner and `size` its height.");
    static const char mesh_doc[] =
        "Triangles: three of `indices` each, places in `vertices`. Each pixel is its corners' colors blended "
        "across the triangle, times `texture`'s at their uvs: a grid of colors with a size, whose cell (0, 0) is at "
        "uv (0, 0). `filter` is how it's read between cells: `Filter.Bilinear` (the default) or `Filter.Point`.";
    describe(add("Draw2D", "Mesh", T_NONE, "tide_draw_mesh_lists", 2, T_VERTICES, T_INDICES, T_NONE),
             "vertices, indices", mesh_doc);
    describe(add("Draw2D", "Mesh", T_NONE, "tide_draw_mesh_grid", 3, T_VERTICES, T_INDICES, T_PIXELS),
             "vertices, indices, texture", mesh_doc);
    signature *sampled = add("Draw2D", "Mesh", T_NONE, "tide_draw_mesh_grid", 3, T_VERTICES, T_INDICES, T_PIXELS);
    sampled->argc = 4;
    sampled->params[3] = T_FILTER;
    describe(sampled, "vertices, indices, texture, filter", mesh_doc);
    describe(add("Draw2D", "Mesh", T_NONE, "tide_draw_mesh_texture", 3, T_VERTICES, T_INDICES, T_TEXTURE),
             "vertices, indices, texture", mesh_doc);
    signature *texture_sampled = add("Draw2D", "Mesh", T_NONE, "tide_draw_mesh_texture", 3, T_VERTICES, T_INDICES, T_TEXTURE);
    texture_sampled->argc = 4;
    texture_sampled->params[3] = T_FILTER;
    describe(texture_sampled, "vertices, indices, texture, filter", mesh_doc);
    static const char mesh3_doc[] =
        "A 3D mesh, placed in the world by `transform`, through the 3D camera: triangles, three of `indices` each, "
        "places in `vertices`, the nearest in front. Each pixel is its corners' colors blended across the "
        "triangle, times `texture`'s at their uvs. Lists that don't change aren't copied again, and a mesh drawn "
        "again and again in a row is drawn at once.";
    describe(add("Draw3D", "Mesh", T_NONE, "tide_draw_mesh3_lists", 3, T_VERTICES3, T_INDICES, T_F4X4),
             "vertices, indices, transform", mesh3_doc);
    signature *textured3 = add("Draw3D", "Mesh", T_NONE, "tide_draw_mesh3_grid", 3, T_VERTICES3, T_INDICES, T_F4X4);
    textured3->argc = 4;
    textured3->params[3] = T_PIXELS;
    describe(textured3, "vertices, indices, transform, texture", mesh3_doc);
    signature *sampled3 = add("Draw3D", "Mesh", T_NONE, "tide_draw_mesh3_grid", 3, T_VERTICES3, T_INDICES, T_F4X4);
    sampled3->argc = 5;
    sampled3->params[3] = T_PIXELS;
    sampled3->params[4] = T_FILTER;
    describe(sampled3, "vertices, indices, transform, texture, filter", mesh3_doc);
    signature *texture3 = add("Draw3D", "Mesh", T_NONE, "tide_draw_mesh3_texture", 3, T_VERTICES3, T_INDICES, T_F4X4);
    texture3->argc = 4;
    texture3->params[3] = T_TEXTURE;
    describe(texture3, "vertices, indices, transform, texture", mesh3_doc);
    signature *texture_sampled3 = add("Draw3D", "Mesh", T_NONE, "tide_draw_mesh3_texture", 3, T_VERTICES3, T_INDICES, T_F4X4);
    texture_sampled3->argc = 5;
    texture_sampled3->params[3] = T_TEXTURE;
    texture_sampled3->params[4] = T_FILTER;
    describe(texture_sampled3, "vertices, indices, transform, texture, filter", mesh3_doc);
    static const char instanced_doc[] =
        "Draws one mesh for each element of `instances`, in order: a List<float4x4> of 3D transforms, or a List<T> "
        "of structs whose fields a vert or frag reads through a read-only T parameter. A 3D struct's float4x4 "
        "transform places it; without one it uses the identity. A vert places 2D instances.";
    describe(add("Draw3D", "Instanced", T_NONE, "tide_draw_mesh3_instances_lists", 3, T_VERTICES3, T_INDICES, T_TRANSFORMS),
             "vertices, indices, instances", instanced_doc);
    signature *instances_textured = add("Draw3D", "Instanced", T_NONE, "tide_draw_mesh3_instances_grid", 3,
                                        T_VERTICES3, T_INDICES, T_TRANSFORMS);
    instances_textured->argc = 4;
    instances_textured->params[3] = T_PIXELS;
    describe(instances_textured, "vertices, indices, instances, texture", instanced_doc);
    signature *instances_sampled = add("Draw3D", "Instanced", T_NONE, "tide_draw_mesh3_instances_grid", 3,
                                       T_VERTICES3, T_INDICES, T_TRANSFORMS);
    instances_sampled->argc = 5;
    instances_sampled->params[3] = T_PIXELS;
    instances_sampled->params[4] = T_FILTER;
    describe(instances_sampled, "vertices, indices, instances, texture, filter", instanced_doc);
    signature *instances_texture = add("Draw3D", "Instanced", T_NONE, "tide_draw_mesh3_instances_texture", 3,
                                      T_VERTICES3, T_INDICES, T_TRANSFORMS);
    instances_texture->argc = 4;
    instances_texture->params[3] = T_TEXTURE;
    describe(instances_texture, "vertices, indices, instances, texture", instanced_doc);
    signature *instances_texture_sampled = add("Draw3D", "Instanced", T_NONE, "tide_draw_mesh3_instances_texture", 3,
                                              T_VERTICES3, T_INDICES, T_TRANSFORMS);
    instances_texture_sampled->argc = 5;
    instances_texture_sampled->params[3] = T_TEXTURE;
    instances_texture_sampled->params[4] = T_FILTER;
    describe(instances_texture_sampled, "vertices, indices, instances, texture, filter", instanced_doc);
    describe(add("Draw2D", "Instanced", T_NONE, "tide_draw_mesh_shaded_instances", 3, T_VERTICES, T_INDICES, T_INSTANCES),
             "vertices, indices, instances", instanced_doc);
    describe(add("Draw3D", "Instanced", T_NONE, "tide_draw_mesh_shaded_instances", 3, T_VERTICES3, T_INDICES, T_INSTANCES),
             "vertices, indices, instances", instanced_doc);
    static const char clip_doc[] =
        "Only what's inside `rect` draws, for the Draw2D calls after it: from (x, y) to (x + width, y + height), in "
        "the units they're in. `Draw2D.Clip()` with no rect draws everywhere again. Each frame starts with none.";
    describe(add("Draw2D", "Clip", T_NONE, "tide_draw_clip", 1, T_RECT, T_NONE, T_NONE), "rect", clip_doc);
    describe(add("Draw2D", "Clip", T_NONE, "tide_draw_no_clip", 0, T_NONE, T_NONE, T_NONE), NULL, clip_doc);
    describe(add("Draw2D", "NoClip", T_NONE, "tide_draw_no_clip", 0, T_NONE, T_NONE, T_NONE), NULL, clip_doc);
    describe(add("Draw2D", "Screen", T_NONE, "tide_draw_screen", 0, T_NONE, T_NONE, T_NONE), NULL,
             "The Draw2D calls after it are in the screen's pixels: from the top left, with y down, as the GUI's are. "
             "`Draw2D.Camera` goes back to the world.");

    describe(add("Draw3D", "Cube", T_NONE, "tide_draw_cube", 3, T_F3, T_F3, T_COLOR),
             "center, size, color", "A filled cube, centered in the world, with the full size along each axis.");
    signature *cube = add("Draw3D", "Cube", T_NONE, "tide_draw_cube_rotated", 3, T_F3, T_F3, T_Q);
    cube->argc = 4;
    cube->params[3] = T_COLOR;
    describe(cube, "center, size, rotation, color", "A filled cube, rotated around its center, with the full size along each axis.");
    describe(add("Draw3D", "Sphere", T_NONE, "tide_draw_sphere", 3, T_F3, T_F, T_COLOR),
             "center, radius, color", "A filled sphere, centered in the world.");
    signature *cylinder = add("Draw3D", "Cylinder", T_NONE, "tide_draw_cylinder", 3, T_F3, T_F, T_F);
    cylinder->argc = 4;
    cylinder->params[3] = T_COLOR;
    describe(cylinder, "center, radius, height, color", "A filled cylinder, centered in the world, along the y axis.");
    signature *rotated_cylinder = add("Draw3D", "Cylinder", T_NONE, "tide_draw_cylinder_rotated", 3, T_F3, T_F, T_F);
    rotated_cylinder->argc = 5;
    rotated_cylinder->params[3] = T_Q;
    rotated_cylinder->params[4] = T_COLOR;
    describe(rotated_cylinder, "center, radius, height, rotation, color", "A filled cylinder, rotated around its center from the y axis.");
    describe(add("Draw3D", "Plane", T_NONE, "tide_draw_plane", 3, T_F3, T_F2, T_COLOR),
             "center, size, color", "A filled plane, centered in the world, with its size along x and z.");
    signature *plane = add("Draw3D", "Plane", T_NONE, "tide_draw_plane_rotated", 3, T_F3, T_F2, T_Q);
    plane->argc = 4;
    plane->params[3] = T_COLOR;
    describe(plane, "center, size, rotation, color", "A filled plane, rotated around its center from the xz plane.");

    describe(add("Draw2D", "WorldToScreen", T_F2, "tide_draw_world_to_screen", 1, T_F2, T_NONE, T_NONE),
             "position", "A world point through the current 2D camera, in screen pixels from the top left, with y down.");
    describe(add("Draw2D", "ScreenToWorld", T_F2, "tide_draw_screen_to_world", 1, T_F2, T_NONE, T_NONE),
             "position", "A screen point in pixels from the top left, with y down, through the current 2D camera to the world.");
    describe(add("Draw3D", "WorldToScreen", T_F3, "tide_draw_world_to_screen_3d", 1, T_F3, T_NONE, T_NONE),
             "position", "A world point through the current 3D camera: screen pixels from the top left, with y down, "
             "and normalized depth in z (0 at the near plane, 1 at the far plane).");
    describe(add("Draw3D", "ScreenToWorld", T_F3, "tide_draw_screen_to_world_3d", 1, T_F3, T_NONE, T_NONE),
             "position", "A screen point through the current 3D camera to the world: x and y in pixels from the top left, "
             "with y down, and normalized depth in z (0 at the near plane, 1 at the far plane).");
    describe(add("Draw3D", "ScreenRayOrigin", T_F3, "tide_draw_screen_ray_origin", 1, T_F2, T_NONE, T_NONE),
             "position", "The world origin on the near plane of a ray through this screen point in pixels from the top left.");
    describe(add("Draw3D", "ScreenRayDirection", T_F3, "tide_draw_screen_ray_direction", 1, T_F2, T_NONE, T_NONE),
             "position", "The normalized world direction of a ray through this screen point in pixels from the top left.");

    // Text's methods: tide/text.h, with the text first. Positions and lengths
    // count characters, and are clamped to the text, never out of range.
    const type t_int = {TY_INT, NULL};
    // Debug: output for whoever runs the game, which changes nothing in the match
    describe(add("Debug", "Log", T_NONE, "tide_debug_log", 1, T_STR, T_NONE, T_NONE), "text",
             "Prints a line: to the terminal under `tide run`, the browser's console on the web and the system's log on "
             "Android. Anywhere: systems, views, handlers, functions. A tick run again after a rollback logs again, so "
             "prefix match state with `Time.tick`.");
    describe(add("Debug", "Assert", T_NONE, "tide_debug_assert", 1, T_BOOL, T_NONE, T_NONE), "condition",
             "Nothing while the condition holds. When it doesn't, the program stops, naming the file, the line and the "
             "source line.");
    describe(add("Debug", "Assert", T_NONE, "tide_debug_assert", 2, T_BOOL, T_STR, T_NONE), "condition, message",
             "Nothing while the condition holds. When it doesn't, the program stops, naming the file, the line, the "
             "source line and the message.");
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
    // Widgets a view draws itself, or a library's: what they use of the devices
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
    for (int overwrite = 0; overwrite < 2; overwrite++) {
        describe(add("File", "Copy", T_NONE, "tide_file_copy", 2 + overwrite, T_STR, T_STR, T_BOOL), "source, destination, overwrite", "Copies a file; overwrite defaults to false. Fails with IOError.");
        describe(add("File", "Move", T_NONE, "tide_file_move", 2 + overwrite, T_STR, T_STR, T_BOOL), "source, destination, overwrite", "Moves a file; overwrite defaults to false. Fails with IOError.");
    }
    describe(add("File", "Delete", T_NONE, "tide_file_delete", 1, T_STR, T_NONE, T_NONE), "path", "Deletes a file. A missing file succeeds. Fails with IOError.");
    describe(add("Directory", "Exists", T_BOOL, "tide_directory_exists", 1, T_STR, T_NONE, T_NONE), "path", "Whether a directory exists on this machine.");
    describe(add("Directory", "CreateDirectory", T_NONE, "tide_directory_create_directory", 1, T_STR, T_NONE, T_NONE), "path", "Creates missing parents; an existing directory succeeds. Fails with IOError.");
    for (int recursive = 0; recursive < 2; recursive++) {
        describe(add("Directory", "GetFiles", T_PATHS, "tide_directory_get_files", 1 + recursive, T_STR, T_BOOL, T_NONE), "path, recursive", "Lists file paths sorted by name; recursive defaults to false. Fails with IOError.");
        describe(add("Directory", "GetDirectories", T_PATHS, "tide_directory_get_directories", 1 + recursive, T_STR, T_BOOL, T_NONE), "path, recursive", "Lists directory paths sorted by name; recursive defaults to false. Fails with IOError.");
        describe(add("Directory", "Delete", T_NONE, "tide_directory_delete", 1 + recursive, T_STR, T_BOOL, T_NONE), "path, recursive", "Deletes a directory; recursive defaults to false. Fails with IOError.");
    }
    describe(add("Directory", "Move", T_NONE, "tide_directory_move", 2, T_STR, T_STR, T_NONE), "source, destination", "Moves a directory, refusing an existing destination. Fails with IOError.");
    describe(add("Path", "Combine", T_STR, "tide_path_combine", 2, T_STR, T_STR, T_NONE), "first, second", "Combines two paths without accessing storage.");
    describe(add("Path", "GetFileName", T_STR, "tide_path_get_file_name", 1, T_STR, T_NONE, T_NONE), "path", "Returns the last path segment.");
    describe(add("Path", "GetDirectoryName", T_STR, "tide_path_get_directory_name", 1, T_STR, T_NONE, T_NONE), "path", "Returns the parent path, or an empty string.");
    describe(add("Path", "GetExtension", T_STR, "tide_path_get_extension", 1, T_STR, T_NONE, T_NONE), "path", "Returns the extension, including its dot, or an empty string.");
    describe(add("Storage", "Flush", T_NONE, "tide_storage_flush_begin", 0, T_NONE, T_NONE, T_NONE), NULL, "Await until preceding filesystem changes are committed to persistent storage. Fails with IOError.");
}

// ---------------------------------------------------------------------------
// Static members: constants reached through a type or Math.

static const struct {
    const char *owner;
    const char *member;
    type_kind kind;
    const char *c_constant;
} members[] = {
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
    {"Draw", "window", TY_RENDER_TARGET, "((tide_render_target){.id = TIDE_RENDER_WINDOW})"},
    {"Draw2D", "list", TY_DRAW_LIST, "tide_draw_render_target(tide_draw, (tide_render_target){.id = TIDE_RENDER_WINDOW})"},
    {"Draw3D", "list", TY_DRAW_LIST, "tide_draw_render_target(tide_draw, (tide_render_target){.id = TIDE_RENDER_WINDOW})"},
    {"Screen", "width", TY_FLOAT, "tide_ui->width"},
    {"Screen", "height", TY_FLOAT, "tide_ui->height"},
    // In views and the functions they call. The input's Sample reads it of its own devices (see check_member).
    {"Cursor", "locked", TY_BOOL, "tide_ui->devices.mouse.locked"},
};

#define MEMBER_COUNT (sizeof members / sizeof members[0])

// What editors say of members[i], for the ones a type and a name don't explain; else NULL.
static const char *member_doc(const size_t i)
{
    if (strcmp(members[i].owner, "Cursor") == 0) {
        return "Whether the window holds the mouse's cursor right now: what the system says, not what `Cursor.Lock` "
               "asked. A browser only locks it at a click or a key press, Escape gives the cursor back to the player "
               "until the next one, and a window that loses the focus lets it go too. Views read it, and the input's "
               "`Sample`.";
    }
    return NULL;
}

// ---------------------------------------------------------------------------

bool builtin_draw_owner(const str name)
{
    return str_eq_c(name, "Draw2D") || str_eq_c(name, "Draw3D");
}

bool builtin_owner(const str name)
{
    type ignored;
    return str_eq_c(name, "Math") || builtin_draw_owner(name) || str_eq_c(name, "Draw") || str_eq_c(name, "GUI") || str_eq_c(name, "GUILayout")
        || str_eq_c(name, "Screen") || str_eq_c(name, "Cursor") || str_eq_c(name, "Debug")
        || str_eq_c(name, "File") || str_eq_c(name, "Directory") || str_eq_c(name, "Path")
        || str_eq_c(name, "Application") || str_eq_c(name, "Storage")
        || builtin_type_named(name, &ignored);
}

// Widest type of a component-wise call's arguments, or false if they don't fit
// together. `default` takes whatever type the others give.
static bool unify(const expr *e, const bool ints_ok, type *out)
{
    int dim = 1;
    bool is_float = !ints_ok;
    for (int i = 0; i < e->args.count; i++) {
        if (e->args.items[i]->kind == E_DEFAULT) continue;
        const type t = e->args.items[i]->type;
        if (!type_is_numeric(t)) return false;
        const int d = type_dim(t);
        if (d > 1) {
            if (dim > 1 && d != dim) return false;
            dim = d;
        }
        if (type_is_float_based(t)) is_float = true;
    }
    *out = vector_type(is_float, dim);
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

static void arg_list(const expr *e, char *buf, const size_t size)
{
    size_t len = 0;
    buf[0] = '\0';
    for (int i = 0; i < e->args.count && len + 1 < size; i++) {
        const expr *arg = e->args.items[i];
        const int n = snprintf(buf + len, size - len, "%s%s", i ? ", " : "", arg->kind == E_DEFAULT ? "default" : type_name(arg->type));
        if (n > 0) len += (size_t)n;
    }
}

// "Draw2D.Circle(float2 center, float radius, Color color)", plus " -> float" for results.
static void format_signature(const signature *s, sb *out)
{
    sb_printf(out, "%s.%s(", s->owner, s->name);
    const char *names = s->param_names;
    for (int a = 0; a < s->argc; a++) {
        sb_printf(out, "%s%s%s", a ? ", " : "", s->mut & (1u << a) ? "mut " : "", signature_type_name(s->params[a]));
        if (names) {
            const char *end = strchr(names, ',');
            const size_t n = end ? (size_t)(end - names) : strlen(names);
            sb_put(out, " ");
            sb_putn(out, names, n);
            names = end ? end + 2 : NULL;
        }
    }
    if (s->gui & GUI_CONTAINER) sb_printf(out, "%sBlock content", s->argc ? ", " : "");
    sb_put(out, ")");
    if (s->result.kind != TY_VOID) sb_printf(out, " -> %s", signature_type_name(s->result));
    if ((strcmp(s->owner, "File") == 0 || strcmp(s->owner, "Directory") == 0) && strcmp(s->name, "Exists") != 0)
        sb_put(out, " fails IOError");
    if (strcmp(s->owner, "Storage") == 0) sb_put(out, " fails IOError");
}

// Whether signature `s` takes the call's arguments. `default`, unchecked yet,
// fits any parameter but a mut one, which changes a variable.
static bool fits(const signature *s, const expr *e)
{
    if (s->argc != e->args.count) return false;
    for (int a = 0; a < s->argc; a++) {
        const type want = real_type(s->params[a]);
        const expr *arg = e->args.items[a];
        const bool mut = s->mut & (1u << a);
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
        for (int a = 0; a < s->argc; a++) {
            const type t = real_type(s->params[a]);
            const type u = real_type(other->params[a]);
            if (e->args.items[a]->kind != E_DEFAULT || (t.kind == u.kind && t.decl == u.decl)) continue;
            diag_error(e->args.items[a]->at, "'default' could be %s or %s here: %s.%s has a version for each",
                       signature_type_name(s->params[a]), signature_type_name(other->params[a]), s->owner, s->name);
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
        if (e->args.items[i]->kind == E_DEFAULT) defaults++;
        else if (e->args.items[i]->type.kind == TY_ERROR) return (type){TY_ERROR, NULL};
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
                diag_note("it takes numbers and vectors; vectors in one call must be the same size");
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
        e->c_callee = s->c_name;
        e->arg_mut = s->mut;
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
        diag_error(e->at, STR_FMT " has no function '" STR_FMT "'", STR_ARG(owner), STR_ARG(e->name));
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
        {"Lerp", "Linear interpolation: `a + (b - a) * t`, component by component."},
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
    return "Component by component, on numbers and vectors.";
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

// "Circle(${1:center}, ${2:radius}, ${3:color})" from "center, radius, color".
static void format_snippet(const char *name, const char *param_names, sb *out)
{
    sb_printf(out, "%s(", name);
    int index = 1;
    for (const char *p = param_names; p && *p;) {
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
            format_snippet(componentwise[i].name, params, &snippet);
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
        sb snippet = {0};
        if (s->param_names) format_snippet(s->name, s->param_names, &snippet);
        else sb_printf(&snippet, "%s($1)", s->name);
        if (s->gui & GUI_CONTAINER) sb_put(&snippet, "\n{\n\t$0\n}");
        const builtin_member m = {s->name, true, detail.data, s->doc, snippet.data};
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

    // Each version's doc once, as versions share them (Draw2D.Mesh's in 2D and in 3D)
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
     "Shows the stats in the window's bottom right corner, as `tide run --stats` does: the frame rate and what a "
     "frame's work takes, the ping, the bandwidth, the tick, the entity count, the threads, the memory, what draws "
     "and in how many draw calls, and the systems that took longest in the last tick. It's part of the build, so "
     "what `tide build` makes shows them too: take it out before players get the game."},
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
