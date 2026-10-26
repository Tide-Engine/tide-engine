#pragma once
#include "tide/texture_blocks.h"

#include <stdbool.h>
#include <stdint.h>

#include "tide/color.h"
#include "tide/math.h"

// Immediate-mode drawing: Tide's Draw calls record commands into a draw
// list, and a renderer plays them back (tide_platform_draw, on OpenGL or WebGPU). The list
// knows nothing about any renderer, so replacing one doesn't touch game code.
//
// Generated code calls the tide_draw_* functions below (and tide/mesh.h's),
// and so does a game's C that a view hands its list (a `DrawList` parameter).
//
// Positions and sizes are in world units, with y up. The camera decides where
// they land on screen. Commands draw in the order they were recorded. The
// window starts black each frame; owned targets keep their contents. After a TIDE_DRAW_SCREEN command, they're in the
// screen's pixels instead, from the top left with y down, as the GUI's are.
//
// 3D meshes (TIDE_DRAW_MESH_3D, and TIDE_DRAW_SHADED through a 3D shader) go
// through a camera of their own (TIDE_DRAW_CAMERA_3D), and are the only
// commands that have depth: unless their destination's tide_draw_set_depth
// says otherwise, they read and write it, so they hide each other by how far
// they are, whatever their order. Everything else draws over what's there, in order. A clear
// clears depth too. Each frame starts with a 3D camera at the origin, looking
// along +z with a field of view of 60 degrees.
//
// They're also the only commands with sides. The front of a triangle is the
// side its corners go clockwise on, as its mesh has them, and unless its
// destination's tide_draw_set_cull says otherwise, a triangle seen from the
// back isn't drawn. That's so whatever the mesh goes through on its way to
// the screen, a transform or a camera that mirrors too. Nothing in 2D has a
// back.

typedef enum tide_draw_kind {
    TIDE_DRAW_CLEAR,       // color; a.x: 1 for depth alone, which leaves colors and stencil
    TIDE_DRAW_CAMERA,      // a = center, b.x = size
    TIDE_DRAW_CIRCLE,      // a = center, b.x = radius, color
    TIDE_DRAW_WIRE_CIRCLE, // a = center, b.x = radius, color
    TIDE_DRAW_RECT,        // a = center, b = size, color
    TIDE_DRAW_WIRE_RECT,   // a = center, b = size, color
    TIDE_DRAW_LINE,        // a = from, b = to, color
    TIDE_DRAW_TEXT,        // a = where its anchor is, b.x = font size, color, text, words
    TIDE_DRAW_SCREEN,      // The commands after it are in the screen's pixels
    TIDE_DRAW_MESH,        // mesh: its triangles, and the texture they sample
    TIDE_DRAW_CLIP,        // clip: only what's inside it draws, for the commands after it
    TIDE_DRAW_NO_CLIP,
    TIDE_DRAW_CAMERA_3D, // camera: the 3D camera, for the 3D meshes after it
    TIDE_DRAW_MESH_3D,   // mesh3: a run of instances of a 3D mesh, color: what tints them
    TIDE_DRAW_SHADED,    // shaded: a run of instances of a mesh, through a shader of the game's
    TIDE_DRAW_TARGET,    // target: destination for subsequent commands; 0 is the window
    TIDE_DRAW_BLIT,      // shaded: a fullscreen shaded run, replacing its destination
    TIDE_DRAW_SHAPE,     // a = center, b = size, color, shape: a shape with smooth edges
    TIDE_DRAW_STENCIL,   // stencil: what the commands after it do with their destination's stencil
    TIDE_DRAW_QUADS,     // mesh: a run of quads of 2D meshes (tide_draw_list.quads), and the texture they sample
    TIDE_DRAW_PLAIN,     // plain: a run of plain shapes (tide_draw_list.plains): rects, circles, their outlines, lines
} tide_draw_kind;

// How what a command draws goes over what's there. Tide's `Blend` has the same
// values. A color's alpha is how much of it there is, in each of them.
typedef enum tide_blend {
    TIDE_BLEND_ALPHA,    // Over it, by its alpha: what's there shows through what's clear
    TIDE_BLEND_ADD,      // Added to it, as light is: glows, sparks, fire
    TIDE_BLEND_MULTIPLY, // What's there times it: shadows, stains, tinted glass. White changes nothing
    TIDE_BLEND_REPLACE,  // In its place, alpha too
} tide_blend;

// What a 3D mesh does with depth. Tide's `Depth` has the same values. Nothing
// else that draws has any: 2D commands go over what's there, in order.
typedef enum tide_depth {
    TIDE_DEPTH_READ_WRITE, // Hidden behind what's nearer, and hides what's drawn behind it later: what's solid
    TIDE_DEPTH_READ,       // Hidden behind what's nearer, and hides nothing: what's see-through, decals, glass
    TIDE_DEPTH_NONE,       // Neither: drawn over whatever is there
} tide_depth;

// Which side of a 3D mesh's triangles isn't drawn. Tide's `Cull` has the same
// values. The front is the side a triangle's corners go clockwise on.
typedef enum tide_cull {
    TIDE_CULL_BACK,  // The back isn't: what's solid, seen from outside
    TIDE_CULL_FRONT, // The front isn't: the inside of a shell
    TIDE_CULL_NONE,  // Both sides are drawn: what's flat, or see-through
} tide_cull;

// Stencil: a whole number from 0 to 255 for each of a destination's pixels,
// beside its depth, which draws compare with a number of their own and
// change. It's 0 everywhere as a frame starts in the window, and in a target
// until something draws into it, as its depth is the far limit.
//
// The test a draw passes to draw at a pixel: its own number, on the left,
// against the one stored there, as a GPU has it. TIDE_STENCIL_LESS draws where
// its number is less than the stored one. Tide's `StencilTest` has the same
// values.
typedef enum tide_stencil_test {
    TIDE_STENCIL_ALWAYS, // Everywhere: no test
    TIDE_STENCIL_NEVER,
    TIDE_STENCIL_LESS,
    TIDE_STENCIL_EQUAL,
    TIDE_STENCIL_LESS_EQUAL,
    TIDE_STENCIL_GREATER,
    TIDE_STENCIL_NOT_EQUAL,
    TIDE_STENCIL_GREATER_EQUAL,
} tide_stencil_test;

// What becomes of the stored number at each pixel a draw draws at: the ones
// that pass its stencil test and, for a 3D mesh that reads depth, aren't
// hidden. Everywhere else it stays. Tide's `StencilOp` has the same values.
typedef enum tide_stencil_op {
    TIDE_STENCIL_KEEP,      // As it is
    TIDE_STENCIL_REPLACE,   // The draw's number
    TIDE_STENCIL_INCREMENT, // One more, 255 at the most
    TIDE_STENCIL_DECREMENT, // One less, 0 at the least
    TIDE_STENCIL_ZERO,      // 0
    TIDE_STENCIL_INVERT,    // Each of its 8 bits the other way: 255 less it
} tide_stencil_op;

// What draws do with stencil: a test, their number, and what becomes of the
// stored one. A zeroed one is a frame's first: every pixel passes, and
// nothing is stored.
typedef struct tide_stencil {
    uint8_t test;      // tide_stencil_test
    uint8_t operation; // tide_stencil_op
    uint8_t value;     // The draw's number
    uint8_t unused;
} tide_stencil;

// One that tests and stores, of what Tide passes: an enum's value can be any
// number, and one that's no member's is the first; `value` is kept from 0 to
// 255. What nothing reads is 0, so that two that do the same are the same: the
// number of a test every pixel passes, unless it's what's stored, and all of
// one no pixel passes, which draws nothing and so stores nothing.
static inline tide_stencil tide_stencil_stored(const int32_t test, const int32_t value, const int32_t operation)
{
    tide_stencil s = {
        .test = (uint32_t)test <= TIDE_STENCIL_GREATER_EQUAL ? (uint8_t)test : (uint8_t)TIDE_STENCIL_ALWAYS,
        .operation = (uint32_t)operation <= TIDE_STENCIL_INVERT ? (uint8_t)operation : (uint8_t)TIDE_STENCIL_KEEP,
        .value = (uint8_t)(value < 0 ? 0 : value > 255 ? 255 : value),
    };
    if (s.test == TIDE_STENCIL_NEVER) s.operation = TIDE_STENCIL_KEEP, s.value = 0;
    if (s.test == TIDE_STENCIL_ALWAYS && s.operation != TIDE_STENCIL_REPLACE) s.value = 0;
    return s;
}

// One that only tests: what's stored stays.
static inline tide_stencil tide_stencil_tested(const int32_t test, const int32_t value)
{
    return tide_stencil_stored(test, value, TIDE_STENCIL_KEEP);
}

// A tide_stencil as one number, which places and commands keep: 0 for a frame's first.
static inline uint32_t tide_stencil_packed(const tide_stencil s)
{
    return (uint32_t)s.test | (uint32_t)s.operation << 4 | (uint32_t)s.value << 8;
}

static inline tide_stencil tide_stencil_unpacked(const uint32_t packed)
{
    return tide_stencil_stored((int32_t)(packed & 15u), (int32_t)(packed >> 8 & 255u), (int32_t)(packed >> 4 & 15u));
}

// With a command's tide_cull: its instances' transforms mirror the mesh, so
// its triangles reach the camera wound the other way, and a renderer takes
// the other winding for their front. A run of instances is all one or the
// other.
#define TIDE_DRAW_MIRRORED 4u

typedef struct tide_draw_command {
    uint8_t kind;   // tide_draw_kind
    uint8_t blend;  // tide_blend, for a command that draws
    uint8_t depth;  // tide_depth, for one that draws a 3D mesh; 0 for every other
    uint8_t cull;   // tide_cull, for one that draws a 3D mesh, with TIDE_DRAW_MIRRORED where it is; 0 for every other
    union {
        uint32_t text;  // TIDE_DRAW_TEXT: offset of the text in tide_draw_list.text
        uint32_t shape; // TIDE_DRAW_SHAPE: the rest of it, its place in tide_draw_list.shapes
    };
    union {
        uint32_t target; // TIDE_DRAW_TARGET: 1 + its place in tide_draw_list.targets, or 0
        uint32_t clip;   // TIDE_DRAW_CLIP: 1 + its place in tide_draw_list.clips
        uint32_t stencil; // TIDE_DRAW_STENCIL: a tide_stencil, packed (tide_stencil_packed)
        struct {
            tide_float2 a;
            tide_float2 b;
        };
        struct {            // TIDE_DRAW_PLAIN
            uint32_t first; // Its shapes in tide_draw_list.plains...
            uint32_t count; // ...and how many
        } plain;
        struct {              // TIDE_DRAW_MESH, TIDE_DRAW_QUADS
            uint32_t first;   // Its indices in tide_draw_list.indices, three a triangle (its quads in .quads)...
            uint32_t count;   // ...and how many
            uint32_t texture; // 1 + its texture's place in tide_draw_list.textures, or 0 for none
            uint16_t filter;  // tide_filter
            uint16_t wrap;    // tide_wrap
        } mesh;
        struct {             // TIDE_DRAW_CAMERA_3D
            uint32_t matrix; // Its place in tide_draw_list.matrices: from the world to clip space, as OpenGL's.
                             // TIDE_DRAW_FIRST_CAMERA: the one each frame starts with
            uint32_t fit;    // 1: it's for a square screen, and renderers divide x by the screen's width over its height
            uint32_t mirrored; // 1: it shows the world mirrored, so triangles reach the screen wound the other way
        } camera;
        struct {              // TIDE_DRAW_MESH_3D
            uint32_t mesh;    // 1 + its place in tide_draw_list.meshes, or 0 for none (see tide_draw_forget)
            uint32_t first;   // Its instances: transforms in tide_draw_list.matrices, from the mesh to the world...
            uint32_t count;   // ...and how many
            uint32_t texture; // As a mesh's
            uint16_t filter;
            uint16_t wrap;
        } mesh3;
        struct {          // TIDE_DRAW_SHADED
            uint32_t run; // Its place in tide_draw_list.runs
        } shaded;
        struct {             // TIDE_DRAW_TEXT, after its `a` and `b.x`
            tide_float2 at;  // `a`
            float size;      // `b.x`
            uint16_t anchor; // tide_anchor: the place of its box that's at `a`
            uint16_t bold;   // 1: in the bold weight, whose file is tide_draw_list.bold
            uint32_t turned; // 1 + its place in tide_draw_list.turns, for text a transform turned or stretched; else 0
        } words;
    };
    tide_color color;
} tide_draw_command;

#define TIDE_DRAW_FIRST_CAMERA UINT32_MAX

// A rectangle: its corner and its size. The GUI's are from the top left.
typedef struct tide_rect {
    float x, y, width, height;
} tide_rect;

// A place of a box, named as Unity's TextAnchor: which of a text's is at its
// position, and where GUILayout.Area puts an area on the screen. Tide's
// Anchor enum has the same values. Upper is towards the top of the screen.
typedef enum tide_anchor {
    TIDE_ANCHOR_UPPER_LEFT,
    TIDE_ANCHOR_UPPER_CENTER,
    TIDE_ANCHOR_UPPER_RIGHT,
    TIDE_ANCHOR_MIDDLE_LEFT,
    TIDE_ANCHOR_MIDDLE_CENTER,
    TIDE_ANCHOR_MIDDLE_RIGHT,
    TIDE_ANCHOR_LOWER_LEFT,
    TIDE_ANCHOR_LOWER_CENTER,
    TIDE_ANCHOR_LOWER_RIGHT,
} tide_anchor;

// The weights text is drawn in, each a file of the engine's font. C names a
// weight by its file: TIDE_FONT_REGULAR, which every renderer has, or
// TIDE_FONT_BOLD, whose file is in the platform layer's library, in an object
// of its own. Nothing in the engine or the platform layer names that file, so
// a program only carries it when its own code does: a game's C that draws in
// bold, or the C generated for a game that says `Font.Bold`. A program that
// names it links the platform layer, or defines the array itself.
extern const unsigned char tide_font_bold_ttf[];
#define TIDE_FONT_REGULAR ((const unsigned char *)0)
#define TIDE_FONT_BOLD tide_font_bold_ttf

// What a shape with smooth edges is: a box, whose corners can be round, the
// ellipse that fits its size, or part of a ring.
typedef enum tide_shape_form {
    TIDE_SHAPE_BOX,
    TIDE_SHAPE_ELLIPSE,
    TIDE_SHAPE_ARC, // Its size is the ring's, and its outline's width how thick the ring is
    TIDE_SHAPE_CORNERS, // A filled box: radius/outline/soft/sweep are TL/TR/BR/BL radii, not effects
    TIDE_SHAPE_FORMS = 0xFF, // What a tide_draw_shape_data's `form` has of these...
    // ...and with this, its outline is a pixel of the screen wide, whatever the camera: the outline of a
    // rect or a circle that a transform turned or stretched
    TIDE_SHAPE_HAIRLINE = 0x100,
} tide_shape_form;

// The rest of a TIDE_DRAW_SHAPE command, whose `a` is the shape's center, `b`
// its size and `color` its fill at its top. Lengths are in the units the
// command is in, as its size is.
typedef struct tide_draw_shape_data {
    tide_float2 turn;  // Its rotation's cos and sin, from +x towards +y in the units it's in; an arc's, to its middle
    float radius;      // Its corners', no more than half its smaller side
    float outline;     // How wide its outline is, inside its edge, or 0 for none
    float soft;        // How far its edge fades: outwards from it, or for less than 0, inwards
    float sweep;       // TIDE_SHAPE_ARC: half the angle it covers, in radians, under pi
    uint32_t form;     // tide_shape_form, maybe with TIDE_SHAPE_HAIRLINE
    tide_color color2; // Its fill at its bottom
    tide_color outline_color;
} tide_draw_shape_data;

// A mesh's corner: where it is, where in the texture it samples ((0, 0) is the
// first pixel's outer corner, (1, 1) the last one's), and its color, which
// multiplies the texture's. Tide's `Vertex` is the same.
typedef struct tide_vertex {
    tide_float2 position;
    tide_float2 uv;
    tide_color color;
} tide_vertex;

// A color as the bytes a GPU gets for it: each channel kept from 0 to 1, NaN as
// 0, times 255, plus a half, what's after the point dropped. The four at once
// (maxnum and minnum are fmaxf's and fminf's rule; lane by lane IEEE's
// arithmetic), the same byte for every one of the 2^32 floats as
// (uint8_t)(fminf(fmaxf(v, 0), 1) * 255 + 0.5f), checked exhaustively. The
// list records plain shapes' colors so, and renderers convert every other
// color so, so the two are the same bytes. The whole numbers, 0 to 255, are
// packed as a vector of bytes is, which takes a shuffle where four shifts took
// more.
typedef float tide_draw_color_lanes __attribute__((ext_vector_type(4)));
typedef int32_t tide_draw_color_ints __attribute__((ext_vector_type(4)));
typedef uint8_t tide_draw_color_u8 __attribute__((ext_vector_type(4)));

static inline uint32_t tide_draw_color_bytes(const tide_color c)
{
    const tide_draw_color_lanes v = {c.r, c.g, c.b, c.a};
    const tide_draw_color_lanes kept = __builtin_elementwise_minnum(__builtin_elementwise_maxnum(v, (tide_draw_color_lanes)0.0f),
                                                                    (tide_draw_color_lanes)1.0f);
    const tide_draw_color_ints whole = __builtin_convertvector(kept * 255.0f + 0.5f, tide_draw_color_ints);
    const tide_draw_color_u8 bytes = __builtin_convertvector(whole, tide_draw_color_u8);
    uint32_t packed;
    __builtin_memcpy(&packed, &bytes, sizeof packed);
    return packed;
}

// A plain shape (TIDE_DRAW_PLAIN's): a rect, a circle, their outlines or a
// line, 24 bytes, its color as the GPU gets it (red first in memory). The list
// keeps them side by side, a run of them a command, as the renderer draws them.
typedef struct tide_draw_plain {
    tide_float2 a;    // A rect's or circle's center, a line's start
    tide_float2 b;    // A rect's size, a circle's radius (and 0), a line's end
    uint32_t color;   // tide_draw_color_bytes
    uint8_t kind;     // TIDE_DRAW_RECT, _WIRE_RECT, _CIRCLE, _WIRE_CIRCLE or _LINE
    uint8_t zero[3];  // Explicit padding, kept zero
} tide_draw_plain;

// The color a plain shape is drawn with, as a color: its bytes over 255.
static inline tide_color tide_draw_plain_color(const tide_draw_plain *p)
{
    return (tide_color){(float)(p->color & 255u) / 255.0f, (float)(p->color >> 8 & 255u) / 255.0f,
                        (float)(p->color >> 16 & 255u) / 255.0f, (float)(p->color >> 24) / 255.0f};
}

// A 2D mesh of four corners and two triangles, p q r and p r s, whose corners'
// places and uvs take two numbers at most on each axis, p's and r's, and are of
// one color: a sprite, a tile, an upright image. The list keeps it as this, 52
// bytes where its corners and triangles took 152 (TIDE_DRAW_QUADS), as the
// renderer draws it: `pattern` has 4 bits for each of the six corners in order,
// x, y, u and v, 1 where it takes r's number and 0 where p's. Its corners are
// those numbers, to the bit (tide_draw_quad_corner).
typedef struct tide_draw_quad {
    tide_float2 from, to;       // p's place and r's
    tide_float2 uv_from, uv_to; // ...and their uvs
    tide_color color;
    uint32_t pattern;
} tide_draw_quad;

// Its corner `k` (0 to 5, of p q r p r s) as a vertex.
static inline tide_vertex tide_draw_quad_corner(const tide_draw_quad *q, const uint32_t k)
{
    const uint32_t pick = q->pattern >> (4u * k);
    return (tide_vertex){{pick & 1u ? q->to.x : q->from.x, pick & 2u ? q->to.y : q->from.y},
                         {pick & 4u ? q->uv_to.x : q->uv_from.x, pick & 8u ? q->uv_to.y : q->uv_from.y}, q->color};
}

// Whether four corners and six indices are such a quad (tide_draw_quad), and
// it into `q` if so. Corners compared by their bits, as the renderer would
// compare them, so what's one here is one there and the same numbers.
// A float's bits, from where it is (a register, as often as not: no wider load over narrower stores).
static inline uint32_t tide_draw_bits(const float f)
{
    uint32_t u;
    __builtin_memcpy(&u, &f, sizeof u);
    return u;
}

static inline __attribute__((always_inline)) bool tide_draw_quad_of(const tide_vertex *v, const uint32_t *t, tide_draw_quad *q)
{
    if (t[3] != t[0] || t[4] != t[2] || t[0] > 3u || t[1] > 3u || t[2] > 3u || t[5] > 3u) return false;
    const tide_vertex *const corner[4] = {&v[t[0]], &v[t[1]], &v[t[2]], &v[t[5]]}; // p, q, r and s
    uint32_t at[4][4], color[4][4]; // Each corner's x, y, u and v, and its color, as bits: field by field
    for (uint32_t k = 0; k < 4u; k++) {
        at[k][0] = tide_draw_bits(corner[k]->position.x);
        at[k][1] = tide_draw_bits(corner[k]->position.y);
        at[k][2] = tide_draw_bits(corner[k]->uv.x);
        at[k][3] = tide_draw_bits(corner[k]->uv.y);
        color[k][0] = tide_draw_bits(corner[k]->color.r);
        color[k][1] = tide_draw_bits(corner[k]->color.g);
        color[k][2] = tide_draw_bits(corner[k]->color.b);
        color[k][3] = tide_draw_bits(corner[k]->color.a);
    }
    bool fits = true;
    uint32_t q_picks = 0, r_picks = 0, s_picks = 0;
    for (uint32_t axis = 0; axis < 4u; axis++) {
        const bool qp = at[1][axis] == at[0][axis], sp = at[3][axis] == at[0][axis];
        fits &= (qp | (at[1][axis] == at[2][axis])) & (sp | (at[3][axis] == at[2][axis]));
        fits &= (color[1][axis] == color[0][axis]) & (color[2][axis] == color[0][axis]) & (color[3][axis] == color[0][axis]);
        q_picks |= (uint32_t)!qp << axis;
        r_picks |= (uint32_t)(at[2][axis] != at[0][axis]) << axis;
        s_picks |= (uint32_t)!sp << axis;
    }
    if (!fits) return false;
    // Field by field, as they were read
    *q = (tide_draw_quad){{corner[0]->position.x, corner[0]->position.y}, {corner[2]->position.x, corner[2]->position.y},
                          {corner[0]->uv.x, corner[0]->uv.y}, {corner[2]->uv.x, corner[2]->uv.y},
                          {corner[0]->color.r, corner[0]->color.g, corner[0]->color.b, corner[0]->color.a}, 0u};
    q->pattern = q_picks << 4 | r_picks << 8 | r_picks << 16 | s_picks << 20; // p q r p r s
    return true;
}

// How a mesh samples its texture between pixels. Tide's `Filter` has the same
// values, which are Unity's FilterMode's. Trilinear also reads the texture's
// smaller images (its mipmaps, each half the last's size, which the renderer
// makes of its pixels and makes again when they change): where it's drawn
// smaller than it is, a pixel is blended from the two nearest that size, so
// it's the mean of what it covers and doesn't shimmer. A texture read so in
// no draw has none.
typedef enum tide_filter {
    TIDE_FILTER_POINT,     // The nearest pixel: hard edges
    TIDE_FILTER_BILINEAR,  // Blended with its neighbors
    TIDE_FILTER_TRILINEAR, // ...and between its smaller images, where it's drawn smaller
} tide_filter;

// A filter from any number, as a Tide enum's value can be: what isn't one is TIDE_FILTER_BILINEAR.
static inline tide_filter tide_filter_of(const int32_t filter)
{
    return filter == TIDE_FILTER_POINT || filter == TIDE_FILTER_TRILINEAR ? (tide_filter)filter : TIDE_FILTER_BILINEAR;
}

// What a mesh samples of its texture where a uv is past 0 or 1, on both axes.
// Tide's `Wrap` has the same values. Blended, a pixel at the texture's edge
// is blended with what the wrap says is past it.
typedef enum tide_wrap {
    TIDE_WRAP_CLAMP,  // The pixels at its edge
    TIDE_WRAP_REPEAT, // The texture again: 1.25 reads what 0.25 does
    TIDE_WRAP_MIRROR, // The texture again, the other way round each time: 1.25 reads what 0.75 does
} tide_wrap;

// A wrap from any number, as a Tide enum's value can be: what isn't one is TIDE_WRAP_CLAMP.
static inline tide_wrap tide_wrap_of(const int32_t wrap)
{
    return wrap == TIDE_WRAP_REPEAT || wrap == TIDE_WRAP_MIRROR ? (tide_wrap)wrap : TIDE_WRAP_CLAMP;
}

enum { TIDE_SHADER_ATTRIBUTES = 16, TIDE_SHADER_TEXTURES = 12 };

// How a shader's textures are sampled, two eight-texture banks (a
// tide_draw_run's `sampling`): bit i is set for texture i read blended with
// its neighbors, and clear for the nearest pixel, and the two bits from
// TIDE_SAMPLING_WRAP + 2 * i are its tide_wrap. So a word with only its low
// bits set clamps every texture. Bit TIDE_SAMPLING_MIPS + i is set, with bit
// i, for one read between its smaller images too (TIDE_FILTER_TRILINEAR).
// Slots 0..7 keep their original low 32 bits; slots 8..11 use the same
// layout in the high 32 bits, with i starting at zero again.
#define TIDE_SAMPLING_WRAP 8u
#define TIDE_SAMPLING_MIPS 24u

static inline uint64_t tide_sampling_wrapped(const uint32_t texture, const int32_t wrap)
{
    if (texture >= TIDE_SHADER_TEXTURES) return 0;
    return (uint64_t)tide_wrap_of(wrap) << (32u * (texture / 8u) + TIDE_SAMPLING_WRAP + 2u * (texture % 8u));
}

static inline bool tide_sampling_blended(const uint64_t sampling, const uint32_t texture)
{
    return texture < TIDE_SHADER_TEXTURES && (sampling >> (32u * (texture / 8u) + texture % 8u) & 1u) != 0;
}

// Texture `texture`'s filter in the word, and the word's bits for a filter.
static inline tide_filter tide_sampling_filter(const uint64_t sampling, const uint32_t texture)
{
    if (!tide_sampling_blended(sampling, texture)) return TIDE_FILTER_POINT;
    return sampling >> (32u * (texture / 8u) + TIDE_SAMPLING_MIPS + texture % 8u) & 1u
        ? TIDE_FILTER_TRILINEAR : TIDE_FILTER_BILINEAR;
}

static inline uint64_t tide_sampling_filtered(const uint32_t texture, const int32_t filter)
{
    if (texture >= TIDE_SHADER_TEXTURES) return 0;
    const tide_filter how = tide_filter_of(filter);
    const uint32_t bank = 32u * (texture / 8u), lane = texture % 8u;
    return (how != TIDE_FILTER_POINT ? UINT64_C(1) << (bank + lane) : 0u)
         | (how == TIDE_FILTER_TRILINEAR ? UINT64_C(1) << (bank + TIDE_SAMPLING_MIPS + lane) : 0u);
}

static inline tide_wrap tide_sampling_wrap(const uint64_t sampling, const uint32_t texture)
{
    if (texture >= TIDE_SHADER_TEXTURES) return TIDE_WRAP_CLAMP;
    return tide_wrap_of((int32_t)(sampling >> (32u * (texture / 8u) + TIDE_SAMPLING_WRAP + 2u * (texture % 8u)) & 3u));
}

// Pixels to draw with, where the game keeps them. Nothing holds a texture on
// the GPU: a mesh names its pixels, the list copies them when it hasn't got
// them as they are, and the renderer uploads from that copy. So C can keep
// its pixels wherever it likes, and say when they changed. A pixel's color is
// as it is, with its alpha beside it. The list's copy has each color times
// its alpha, which is how a renderer blends them: that's the list's to do.
typedef struct tide_texture {
    const void *pixels; // width * height of them: red, green, blue, alpha, a byte each, rows from the first
    int32_t width, height;
    uint64_t version; // Any number that's another whenever these pixels are (see tide_texture_version)
} tide_texture;

// How big a rendering destination is. Its size is in logical pixels, as the
// window's is, and its image has `scale` pixels of its own in each: the
// display's for 0, so what's drawn into it is as sharp as what's drawn into
// the window, and exactly that many for any other, whatever the display. The
// image has the whole number of pixels nearest its size times its scale, 1 at
// least. One that follows the window (`window` above 0) is always the
// window's size times that, at the display's scale: its width, height and
// scale aren't read.
typedef struct tide_target_size {
    float width, height;
    float scale;
    float window;
} tide_target_size;

// The physical image size for a target under the host's current window and device.
typedef tide_int2 (*tide_target_pixels_fn)(tide_target_size size);

// Whether there's an image of that size: a window to follow, or a width and a
// height above 0 at a scale that isn't under it. Never for a NaN.
static inline bool tide_target_size_valid(const tide_target_size size)
{
    if (size.window > 0.0f) return size.window <= 3.0e38f;
    return size.width > 0.0f && size.height > 0.0f && size.width <= 3.0e38f && size.height <= 3.0e38f
        && size.scale >= 0.0f && size.scale <= 3.0e38f;
}

// Its size in logical pixels, in a window of `window_size`.
static inline tide_float2 tide_target_size_logical(const tide_target_size size, const tide_float2 window_size)
{
    if (size.window > 0.0f) return tide_f2(window_size.x * size.window, window_size.y * size.window);
    return tide_f2(size.width, size.height);
}

static inline bool tide_target_size_same(const tide_target_size a, const tide_target_size b)
{
    return a.width == b.width && a.height == b.height && a.scale == b.scale && a.window == b.window;
}

// What a target holds, said when it's made and never changed. Every target
// has depth, which its 3D meshes read and write and a shader reads (see
// tide_depth_ref), and color images beside it, up to four, or none: a target
// of depth alone, which is drawn into like any other, where a color has
// nowhere to go and only what writes depth leaves anything. A format is what
// one color image holds for each pixel (Tide's Format), and
// TIDE_TARGET_DEPTH, the one format of a target with none.
typedef enum tide_target_format {
    TIDE_TARGET_RGBA8, // 4 bytes: red, green, blue and alpha, each from 0 to 1
    TIDE_TARGET_DEPTH, // No color image at all
    // Four floats of 16 bits: any number, to 3 digits or so. Where a GPU can't
    // draw into those (an OpenGL ES 3 or a WebGL 2 without them), it's 4
    // bytes, and the renderer says so once
    TIDE_TARGET_RGBA16F,
} tide_target_format;

enum { TIDE_TARGET_COLORS = 4 }; // The most color images a target holds: what every GPU Tide draws with has
enum { TIDE_TARGET_SAMPLES = 8 }; // The most samples a pixel of a target is drawn with (tide_render_target_make_sampled)

// A target's color images, in order: how many, and what each holds
// (TIDE_TARGET_RGBA8 or TIDE_TARGET_RGBA16F). None for a target of depth alone.
typedef struct tide_target_formats {
    uint8_t count;
    uint8_t color[TIDE_TARGET_COLORS];
} tide_target_formats;

// Managed images share this state, including through a copied color reference.
// Disposal keeps a small tombstone until the build unloads, so aliases remain
// safe to inspect. The image attachments are released by the renderer.
typedef struct tide_render_target_state {
    struct tide_render_target_state *next;
    struct tide_render_target_state **owner;
    tide_target_size size;
    uint64_t version;
    uint64_t reload_id; // Temporary opaque identity while restoring another address space.
    void *memory;       // What it was allocated in, to free: the state is on a boundary inside it
    bool alive;
    bool cube; // A cube's: six images of its size, a face each (TIDE_RENDER_CUBE)
    uint16_t holds; // What it holds and its samples, as its id says them (TIDE_RENDER_FORMAT, TIDE_RENDER_SAMPLES)
} tide_render_target_state;

// A managed target's id is its state's address, with what it holds in the
// bits that leaves free: so the id alone says it, to a list, to a renderer,
// and to another build of the game that's handed the id of a target to make
// again (tide_render_target_recreate). A state is on a boundary of
// TIDE_RENDER_BITS + 1 bytes for that. The bits are 3 for each color image in
// order, 1 + its tide_target_format, and 0 past the last (so none at all is a
// target of depth alone); then, in a texture's reference to one of a target's
// color images (tide_texture_ref), 2 for which; then 2 for how many samples
// each of its pixels is drawn with: 1, 2, 4 or 8, as that power of two.
#define TIDE_RENDER_MANAGED (UINT64_C(1) << 63)
#define TIDE_RENDER_FORMAT UINT64_C(0xFFF)
#define TIDE_RENDER_IMAGE_SHIFT 12
#define TIDE_RENDER_IMAGE (UINT64_C(3) << TIDE_RENDER_IMAGE_SHIFT)
#define TIDE_RENDER_SAMPLES_SHIFT 14
#define TIDE_RENDER_SAMPLES (UINT64_C(3) << TIDE_RENDER_SAMPLES_SHIFT)
#define TIDE_RENDER_BITS (TIDE_RENDER_FORMAT | TIDE_RENDER_IMAGE | TIDE_RENDER_SAMPLES)
#define TIDE_RENDER_WINDOW UINT64_MAX
#define TIDE_RENDER_INVALID (UINT64_MAX - UINT64_C(1))
// A cube's (tide_cubemap_ref): six square color images that are read by a
// direction, each drawn into as a target of its own. Its id says so above
// where an address ends, in bits no state's has (a cube is refused one that
// has): that it's a cube's, and for one of its faces as a destination, which,
// 1 + its number. A face is a target to draw into, with the cube's state,
// its version and its size, and depth of its own; the cube's own id, with no
// face, is what's sampled.
#define TIDE_RENDER_FACE_SHIFT 48
#define TIDE_RENDER_FACE (UINT64_C(7) << TIDE_RENDER_FACE_SHIFT)
#define TIDE_RENDER_CUBE (UINT64_C(1) << 51)
enum { TIDE_CUBE_FACES = 6 }; // +x, -x, +y, -y, +z, -z

static inline bool tide_render_target_managed(const uint64_t id)
{
    return (id & TIDE_RENDER_MANAGED) && id != TIDE_RENDER_WINDOW && id != TIDE_RENDER_INVALID;
}

static inline tide_render_target_state *tide_render_target_state_of(const uint64_t id)
{
    return tide_render_target_managed(id)
        ? (tide_render_target_state *)(uintptr_t)(id & ~(TIDE_RENDER_MANAGED | TIDE_RENDER_BITS | TIDE_RENDER_CUBE | TIDE_RENDER_FACE))
        : NULL;
}

// Whether the target `id` is a cube, or one of a cube's faces.
static inline bool tide_render_target_cubic(const uint64_t id)
{
    return tide_render_target_managed(id) && (id & TIDE_RENDER_CUBE);
}

// Which of its cube's faces the destination `id` is, from 0, or -1 for what's no face: any other target, and a cube itself.
static inline int32_t tide_render_target_face(const uint64_t id)
{
    return tide_render_target_cubic(id) ? (int32_t)((id & TIDE_RENDER_FACE) >> TIDE_RENDER_FACE_SHIFT) - 1 : -1;
}

// The cube a face is of: the cube's own id, which is what a renderer's cube and a comparison of two go by.
static inline uint64_t tide_render_target_cube_of(const uint64_t id)
{
    return tide_render_target_cubic(id) ? id & ~(TIDE_RENDER_FACE | TIDE_RENDER_IMAGE) : id;
}

// What the target `id` holds. The window and a borrowed target hold one color
// image of 4 bytes a pixel.
static inline tide_target_formats tide_render_target_formats(const uint64_t id)
{
    tide_target_formats formats = {0};
    if (!tide_render_target_managed(id)) {
        formats.count = 1;
        return formats;
    }
    for (; formats.count < TIDE_TARGET_COLORS; formats.count++) {
        const uint32_t held = (uint32_t)(id >> (3u * formats.count)) & 7u;
        if (!held) break;
        formats.color[formats.count] = (uint8_t)(held - 1u);
    }
    return formats;
}

// How many color images it has: 0 for a target of depth alone.
static inline uint32_t tide_render_target_colors(const uint64_t id)
{
    return tide_render_target_formats(id).count;
}

// What its first color image holds, or TIDE_TARGET_DEPTH for a target
// with none: the format a target of one image, or of none, was made with.
static inline tide_target_format tide_render_target_format(const uint64_t id)
{
    const tide_target_formats formats = tide_render_target_formats(id);
    return formats.count ? (tide_target_format)formats.color[0] : TIDE_TARGET_DEPTH;
}

// How many samples each pixel of the target `id` is drawn with, as it was
// asked for: 1 (the window's, a borrowed target's, and any target's that
// didn't say), 2, 4 or 8. A device may have fewer, and gives what it has.
static inline uint32_t tide_render_target_samples(const uint64_t id)
{
    return tide_render_target_managed(id) ? 1u << ((id & TIDE_RENDER_SAMPLES) >> TIDE_RENDER_SAMPLES_SHIFT) : 1u;
}

// A texture's reference to one of a target's color images has which in its
// id:the image's number, from 0, and the target's own id, which is what a
// destination, a renderer's image and a comparison of two targets go by.
static inline uint32_t tide_render_target_image(const uint64_t id)
{
    return tide_render_target_managed(id) ? (uint32_t)((id & TIDE_RENDER_IMAGE) >> TIDE_RENDER_IMAGE_SHIFT) : 0;
}

static inline uint64_t tide_render_target_whole(const uint64_t id)
{
    return tide_render_target_managed(id) ? id & ~TIDE_RENDER_IMAGE : id;
}

static inline bool tide_render_target_live(const uint64_t id)
{
    const tide_render_target_state *s = tide_render_target_state_of(id);
    return s ? s->alive : id && id != TIDE_RENDER_WINDOW && id != TIDE_RENDER_INVALID;
}

static inline uint64_t tide_render_target_version(const uint64_t id)
{
    const tide_render_target_state *s = tide_render_target_state_of(id);
    return s ? s->version : 0;
}

// A local destination reference. The window uses TIDE_RENDER_WINDOW. Borrowed
// C descriptors use a nonzero id without the managed bit; their owner supplies
// the size, and they hold color. A zero-initialized reference is invalid.
typedef struct tide_render_target {
    uint64_t id;
    tide_target_size size;
} tide_render_target;

// Texture's common sampling reference: CPU pixels, a rendered image, an image
// of the game's pack (an `asset Texture`: no pixels and no target, its size
// and its version, which names its declaration: see tide_pack_texture_of), or
// a grid of the game's (no pixels, no target and a height of
// TIDE_TEXTURE_GRID: see tide_texture_of_grid in tide/mesh.h).
// Copying this value never copies an image. The existing tide_texture remains
// the CPU pixel descriptor used by C drawing callers.
//
// It carries how it's sampled where a draw doesn't say: its filter and its
// wrap (tide_texture_sampling), in the top bits of `image.version`
// (TIDE_TEXTURE_SAMPLING_SHIFT), which are free there for every kind but C's
// own pixels, whose version is all C's: for those it's in `target.size`,
// which only a rendered image reads, as a whole number in its `scale`. So a
// reference C makes of its pixels with nothing more said, and a zeroed one,
// which is no texture, are blended and clamped.
typedef struct tide_texture_ref {
    tide_texture image;
    tide_render_target target; // A rendered image; its id is 0 for CPU pixels
} tide_texture_ref;

// Base-level pixels, without capturing or uploading the image. Missing images give (0, 0).
tide_int2 tide_texture_size(tide_texture_ref texture);
// Borrow the host's resolver during local execution; returns the previous resolver for restoration.
tide_target_pixels_fn tide_texture_target_pixels(tide_target_pixels_fn resolve);

// A reference's sampling: bit 0 for the nearest pixel (none: blended, as a
// draw that says nothing is), bit 1 for its smaller images too
// (TIDE_FILTER_TRILINEAR), and its tide_wrap from bit 2. The rest of
// `image.version` is TIDE_TEXTURE_NAME: a pack image's name, or a grid's
// place and cells.
#define TIDE_TEXTURE_SAMPLING_SHIFT 60u
#define TIDE_TEXTURE_NAME ((UINT64_C(1) << TIDE_TEXTURE_SAMPLING_SHIFT) - 1u)
#define TIDE_TEXTURE_GRID (-1) // A grid's reference's `image.height`

static inline uint32_t tide_sampling_bits(const int32_t filter, const int32_t wrap)
{
    return (filter == TIDE_FILTER_POINT ? 1u : filter == TIDE_FILTER_TRILINEAR ? 2u : 0u) | (uint32_t)tide_wrap_of(wrap) << 2;
}

static inline uint32_t tide_texture_sampling(const tide_texture_ref texture)
{
    if (texture.image.pixels) { // C's pixels: beside them, where no target is
        const float kept = texture.target.id ? 0.0f : texture.target.size.scale;
        return kept >= 1.0f && kept <= 15.0f ? (uint32_t)kept : 0u;
    }
    return (uint32_t)(texture.image.version >> TIDE_TEXTURE_SAMPLING_SHIFT);
}

static inline tide_filter tide_texture_filter(const tide_texture_ref texture)
{
    const uint32_t sampling = tide_texture_sampling(texture);
    return sampling & 1u ? TIDE_FILTER_POINT : sampling & 2u ? TIDE_FILTER_TRILINEAR : TIDE_FILTER_BILINEAR;
}

static inline tide_wrap tide_texture_wrap(const tide_texture_ref texture)
{
    return tide_wrap_of((int32_t)(tide_texture_sampling(texture) >> 2));
}

// The same texture, sampled as `sampling` says (tide_sampling_bits).
static inline tide_texture_ref tide_texture_sampled(tide_texture_ref texture, const uint32_t sampling)
{
    if (texture.image.pixels) {
        if (!texture.target.id) texture.target.size.scale = (float)(sampling & 15u);
        return texture;
    }
    texture.image.version = (texture.image.version & TIDE_TEXTURE_NAME) | (uint64_t)(sampling & 15u) << TIDE_TEXTURE_SAMPLING_SHIFT;
    return texture;
}

// ...with another filter, or another wrap: what a draw that says one draws
// with. Tide's `Filter` and `Wrap`, as any number an enum's value can be.
static inline tide_texture_ref tide_texture_filtered(const tide_texture_ref texture, const int32_t filter)
{
    return tide_texture_sampled(texture, (tide_texture_sampling(texture) & ~3u) | tide_sampling_bits(filter, TIDE_WRAP_CLAMP));
}

static inline tide_texture_ref tide_texture_wrapped(const tide_texture_ref texture, const int32_t wrap)
{
    return tide_texture_sampled(texture, (tide_texture_sampling(texture) & 3u) | (uint32_t)tide_wrap_of(wrap) << 2);
}

// What texture `texture` of a shader's adds to its run's `sampling` when it's
// read as its reference says: its filter's bit, or its wrap's.
static inline uint64_t tide_sampling_filter_of(const uint32_t texture, const tide_texture_ref from)
{
    return tide_sampling_filtered(texture, (int32_t)tide_texture_filter(from));
}

static inline uint64_t tide_sampling_wrap_of(const uint32_t texture, const tide_texture_ref from)
{
    return tide_sampling_wrapped(texture, (int32_t)tide_texture_wrap(from));
}

// A target's depth, for a shader to read: a number for each of its pixels,
// 0 at the camera's near plane to 1 at its far limit, as the meshes drawn
// into the target left it, and 1 where none was since it was cleared. Tide's
// DepthTexture. Copying it never copies an image, and a zeroed one is no
// target's: it reads as 1 everywhere.
typedef struct tide_depth_ref {
    tide_render_target target;
} tide_depth_ref;

// A target of `width` by `height` logical pixels, at the display's scale: its
// image is made again, empty, when that changes.
tide_render_target tide_render_target_make(float width, float height);
// ...or at a scale of its own, above 0: `scale` pixels of its image in each
// logical one, whatever the display. At 1, the image is `width` by `height`.
tide_render_target tide_render_target_make_scaled(float width, float height, float scale);
// A target that's always the window's size times `factor` (above 0), at the
// display's scale: its image is made again, empty, when either changes.
tide_render_target tide_render_target_screen(float factor);
// ...and the window's size itself.
tide_render_target tide_render_target_window(void);
// The same four, holding what `format` says: one color image of it, or none
// for TIDE_TARGET_DEPTH. The ones above hold TIDE_TARGET_RGBA8.
tide_render_target tide_render_target_make_as(float width, float height, tide_target_format format);
tide_render_target tide_render_target_make_scaled_as(float width, float height, float scale, tide_target_format format);
tide_render_target tide_render_target_screen_as(float factor, tide_target_format format);
tide_render_target tide_render_target_window_as(tide_target_format format);
// ...and holding several color images, `count` of them, TIDE_TARGET_COLORS at
// the most: each what its format says, in order. A shader's outputs go to
// them in that order (tide_shader.outputs), and each is read as a texture of
// its own (tide_render_target_color_at). One format is as the four above;
// among several, one that's no color's (TIDE_TARGET_DEPTH) is
// TIDE_TARGET_RGBA8, having said so.
tide_render_target tide_render_target_make_of(float width, float height, const tide_target_format *formats, uint32_t count);
tide_render_target tide_render_target_make_scaled_of(float width, float height, float scale,
                                                     const tide_target_format *formats, uint32_t count);
tide_render_target tide_render_target_screen_of(float factor, const tide_target_format *formats, uint32_t count);
tide_render_target tide_render_target_window_of(const tide_target_format *formats, uint32_t count);
// ...and drawn with `samples` samples a pixel, which smooths the edges of the
// triangles drawn into it: 1, 2, 4 or 8 (TIDE_TARGET_SAMPLES at the most).
// Each pixel keeps that many colors and depths while it's drawn into, and its
// color images are what those come to, one color a pixel, each time a pass on
// it ends: they're sampled like any target's. Its depth is read like any
// target's too, one of each pixel's samples, as a renderer makes it when a
// draw reads it. A target of depth alone has one sample, having said so. Another number is the
// one of those under it, having said so; a device with fewer has what it has
// (the renderer says so once), and 1 is the functions above. NULL and 0 for
// the formats is one image of TIDE_TARGET_RGBA8.
tide_render_target tide_render_target_make_sampled(float width, float height, const tide_target_format *formats,
                                                   uint32_t count, int32_t samples);
tide_render_target tide_render_target_make_scaled_sampled(float width, float height, float scale,
                                                          const tide_target_format *formats, uint32_t count, int32_t samples);
tide_render_target tide_render_target_screen_sampled(float factor, const tide_target_format *formats, uint32_t count,
                                                     int32_t samples);
tide_render_target tide_render_target_window_sampled(const tide_target_format *formats, uint32_t count, int32_t samples);
// Another size, at the scale it has, and an empty image. Not for a target that
// follows the window, whose size is the window's to say.
void tide_render_target_resize(tide_render_target target, float width, float height);
void tide_render_target_dispose(tide_render_target target);
void tide_render_target_adopt(tide_render_target target); // Native reload: move a live reference's bookkeeping.
tide_render_target tide_render_target_recreate(tide_render_target previous); // Web reload: previous.id is opaque.
void tide_render_targets_recreated(void); // Ends one web restoration's alias mapping.
void tide_render_targets_free(void); // Build unload: references must no longer be used.

static inline tide_render_target tide_render_target_resolve(tide_render_target target)
{
    const tide_render_target_state *s = tide_render_target_state_of(target.id);
    if (s) target.size = s->alive ? s->size : (tide_target_size){0};
    return target;
}

// A target's first color image, as a texture. (Of a reference's own `target`,
// whose id says which image it names, it's that image again: how a reference
// is made again for another build of the game.)
static inline tide_texture_ref tide_render_target_color(const tide_render_target target)
{
    const tide_render_target resolved = tide_render_target_resolve(target);
    // A cube's face has no color of two dimensions to hand out: the cube is what's sampled
    return (tide_texture_ref){
        .target = {.id = resolved.id && resolved.id != TIDE_RENDER_WINDOW && !tide_render_target_cubic(resolved.id)
                             ? resolved.id : TIDE_RENDER_INVALID,
                   .size = resolved.size}};
}

// ...and its color image `image`, from 0: Tide's target.colors[image]. One a
// target hasn't got is no texture's: a draw that samples it is passed by, and
// says so.
static inline tide_texture_ref tide_render_target_color_at(const tide_render_target target, const int32_t image)
{
    tide_texture_ref color = tide_render_target_color(target);
    if (image == 0) return color;
    if (image < 0 || image >= TIDE_TARGET_COLORS || !tide_render_target_managed(color.target.id)) {
        color.target.id = TIDE_RENDER_INVALID;
    } else {
        color.target.id = tide_render_target_whole(color.target.id) | ((uint64_t)image << TIDE_RENDER_IMAGE_SHIFT);
    }
    return color;
}

// ...and its depth. The window's isn't read: no shader samples the window.
static inline tide_depth_ref tide_render_target_depth(const tide_render_target target)
{
    const tide_render_target resolved = tide_render_target_resolve(target);
    return (tide_depth_ref){
        .target = {.id = resolved.id && resolved.id != TIDE_RENDER_WINDOW && !tide_render_target_cubic(resolved.id)
                             ? tide_render_target_whole(resolved.id) : TIDE_RENDER_INVALID,
                   .size = resolved.size}};
}

// A cube of images, for a shader to read by a direction from its middle:
// Tide's Cubemap. One of the game's pack (an `asset Cubemap`: no target, and
// `named`, which names its declaration as a pack Texture's version does, so a
// copy kept through another load goes on naming what the declaration does),
// or one that's drawn into (tide_cubemap_make: `target` is the cube's
// own, whose faces are destinations). Copying it never copies an image, and
// a zeroed one is no cube: it reads as black.
typedef struct tide_cubemap_ref {
    tide_render_target target; // A cube that's drawn into; its id is 0 for the pack's
    uint64_t named;            // The pack's: what its declaration is named by
} tide_cubemap_ref;

// A cube to draw into: six images of `size` by `size` pixels (exactly: a
// cube's faces aren't the window's to scale), each of `format`, a color's
// (TIDE_TARGET_RGBA8 for anything but TIDE_TARGET_RGBA16F), black until
// they're drawn into, with depth of their own. A dead one, having said so,
// for a size there's no image of.
tide_cubemap_ref tide_cubemap_make_as(float size, tide_target_format format);
tide_cubemap_ref tide_cubemap_make(float size); // ...of TIDE_TARGET_RGBA8
// Its face `face` (0 to 5: +x, -x, +y, -y, +z, -z) as a destination: what a
// camera at the cube's middle that looks along that axis with a field of
// view of 90 degrees sees, with +y up for the four around, and for the one
// above and the one below, -z and +z. No destination (draws into it are
// passed by) for a face there's none of, and for a cube of the pack's.
static inline tide_render_target tide_cubemap_face(const tide_cubemap_ref cube, const int32_t face)
{
    const tide_render_target resolved = tide_render_target_resolve(cube.target);
    if (!tide_render_target_cubic(resolved.id) || face < 0 || face >= TIDE_CUBE_FACES) return (tide_render_target){.id = TIDE_RENDER_INVALID};
    return (tide_render_target){.id = tide_render_target_cube_of(resolved.id) | (uint64_t)(face + 1) << TIDE_RENDER_FACE_SHIFT,
                                .size = resolved.size};
}
void tide_cubemap_resize(tide_cubemap_ref cube, float size); // Another size, and empty images
void tide_cubemap_dispose(tide_cubemap_ref cube);
tide_cubemap_ref tide_cubemap_recreate(tide_cubemap_ref previous); // Web reload: as tide_render_target_recreate

// Some of a texture's pixels: `width` by `height` of them, from (x, y).
typedef struct tide_draw_area {
    int32_t x, y, width, height;
} tide_draw_area;

enum { TIDE_DRAW_TEXTURE_AREAS = 32 }; // The most areas a texture is said to have changed in: past them, it's one around them all

// What the list knows of a texture whose writer only writes the pixels that
// changed (tide_draw_texture_edit): where its pixels differ from what they
// were as the frame started, so a renderer that has those sends the GPU the
// rest and nothing more (tide_draw_texture_since).
typedef struct tide_draw_texture_changes {
    uint64_t was;   // Its version as the frame it last changed in started
    uint32_t frame; // ...which is this one
    bool known;     // `areas` are all that differs from `was`: false once anything rewrote it whole
    uint32_t count; // 0: only its version differs, and not a pixel
    tide_draw_area areas[TIDE_DRAW_TEXTURE_AREAS];
    uint64_t kind;  // Which writer wrote its pixels, and as what: 0 for one that says nothing of them
    // The writer's own, for pixels it copies part by part: a number for each part, what the part was when it
    // was last copied (a chunk's hash), and one for the whole. Zeros once another writer wrote the texture.
    uint64_t *parts;
    uint32_t part_count;
    uint64_t used;
} tide_draw_texture_changes;

// A texture the list has, for the commands that draw with it: a copy of the
// pixels as they were when a mesh last named them. One no command of a frame
// named is let go when the next frame starts.
typedef struct tide_draw_texture {
    uint64_t id;      // Whose pixels: C's address of them, or a grid's place in its world
    uint64_t version; // ...as they were then
    uint32_t space;   // What `id` is: TIDE_PIXELS_*
    uint32_t format;  // What a pixel is: TIDE_TEXELS_*
    int32_t width, height;
    uint32_t frame;    // The frame that last drew with it
    uint32_t epoch;    // The list's when it was copied (see tide_draw_forget)
    uint32_t capacity; // Bytes `pixels` has room for: 0 where they aren't the list's own
    // width * height pixels, rows from the first, 4 bytes each or 2 (tide_texels_bytes): the list's own copy,
    // or for TIDE_PIXELS_PACK the pack's, where the pack has them, noted again by each frame that draws with them
    uint8_t *pixels;
    tide_target_size target; // A target's: the rendered image's size, where the others have a width and a height
    tide_draw_texture_changes *changes; // What changed of its pixels, where its writer says: NULL for the others
    tide_texture_blocks compressed; // Optional immutable pack mip chain; RGBA remains the fallback.
} tide_draw_texture;

// TIDE_PIXELS_TARGET is one of a render target's color images, the one its id
// says (tide_render_target_image), and TIDE_PIXELS_TARGET_DEPTH its depth,
// which only a shader that reads that texture as depth is given
// (tide_shader.texture_reads): neither has pixels here.
// TIDE_PIXELS_PACK is an image of the game's pack (tide/pack.h), whose `id` is its version: what's in it,
// the file's hash.
// TIDE_PIXELS_TARGET_CUBE is a cube that's drawn into (its id is the cube's own), and TIDE_PIXELS_PACK_CUBE one
// of the pack's, whose `id` is its version, whose width is a face's side, and whose `pixels` is the pack's six
// pointers to its faces' pixels (a `const uint8_t *const *`, noted again by each frame as a pack image's are).
// Only a shader that reads that texture as a cube is given either (TIDE_SHADER_READ_CUBE).
enum {
    TIDE_PIXELS_MEMORY,
    TIDE_PIXELS_GRID,
    TIDE_PIXELS_TARGET,
    TIDE_PIXELS_TARGET_DEPTH,
    TIDE_PIXELS_PACK,
    TIDE_PIXELS_TARGET_CUBE,
    TIDE_PIXELS_PACK_CUBE,
    TIDE_PIXELS_LIST, // A list's elements as numbers (TIDE_TEXELS_INTS), which only a TIDE_SHADER_READ_DATA read is given
};

// What a list's texture holds for each pixel: red, green, blue and alpha, a
// byte each, or one float of 16 bits (IEEE 754's half), which a shader reads
// as red, with green and blue 0 and alpha 1: a Grid2<float>'s cells.
// TIDE_TEXELS_INTS is four ints of 32 bits, which a shader reads as they are,
// by a pixel's place and never between two: a list's elements, as numbers
// (tide_draw_list_data).
enum { TIDE_TEXELS_BYTES, TIDE_TEXELS_HALF, TIDE_TEXELS_INTS };

static inline uint32_t tide_texels_bytes(const uint32_t format)
{
    return format == TIDE_TEXELS_HALF ? 2u : format == TIDE_TEXELS_INTS ? 16u : 4u;
}

// A 2D transform: where it puts the point (x, y) is `offset` + x * `x` + y * `y`.
typedef struct tide_draw_transform {
    tide_float2 x;      // Where (1, 0) goes, from where (0, 0) does
    tide_float2 y;      // ...and (0, 1)
    tide_float2 offset; // Where (0, 0) goes
} tide_draw_transform;

#define TIDE_DRAW_NO_TRANSFORM ((tide_draw_transform){{1.0f, 0.0f}, {0.0f, 1.0f}, {0.0f, 0.0f}})

// Moved by `offset` after it's turned by `rotation`, in radians from +x
// towards +y, after it's scaled by `scale`: all about (0, 0).
tide_draw_transform tide_draw_transform_trs(tide_float2 offset, float rotation, tide_float2 scale);

// The same with less: as big each way, turned only, or moved only.
static inline tide_draw_transform tide_draw_transform_scaled(const tide_float2 offset, const float rotation, const float scale)
{
    return tide_draw_transform_trs(offset, rotation, tide_f2(scale, scale));
}

static inline tide_draw_transform tide_draw_transform_turned(const tide_float2 offset, const float rotation)
{
    return tide_draw_transform_trs(offset, rotation, tide_f2(1.0f, 1.0f));
}

static inline tide_draw_transform tide_draw_transform_moved(const tide_float2 offset)
{
    return (tide_draw_transform){{1.0f, 0.0f}, {0.0f, 1.0f}, offset};
}

// A matrix's: its first two columns are where (1, 0) and (0, 1) go, and its third where (0, 0) does.
static inline tide_draw_transform tide_draw_transform_matrix(const tide_float3x3 m)
{
    return (tide_draw_transform){{m.c0.x, m.c0.y}, {m.c1.x, m.c1.y}, {m.c2.x, m.c2.y}};
}

// Conservative local geometry bounds used only for drawing.
typedef struct tide_draw_bounds {
    tide_float3 center, extents;
    uint32_t set; // 0: automatic geometry bounds; 1: explicit; 2: invalid, so keep the draw
} tide_draw_bounds;

// Cached camera planes and conservative rounding coefficients; no match state.
typedef struct tide_draw_frustum {
    double plane[6][4], magnitude[6][4];
    uint32_t count;
} tide_draw_frustum;

// The settings of one destination. Zero starts a frame: first cameras, no clip
// or transform, depth read/written, back faces rejected, no stencil test,
// automatic geometry bounds and frustum culling enabled.
typedef struct tide_draw_place {
    // What the commands to come are drawn through: 1 + its place in tide_draw_list.cameras and
    // cameras_3d, or 0 for the camera each frame starts with...
    uint32_t camera_2d, camera_3d;
    uint32_t clip;  // ...and 1 + its place in tide_draw_list.clips, or 0 for none
    uint32_t depth; // tide_depth: what the 3D meshes to come do with depth
    uint32_t cull;  // tide_cull: which side of their triangles isn't drawn
    uint32_t stencil; // A tide_stencil, packed: what the commands to come do with stencil
    uint32_t blend;   // tide_blend: how the commands to come go over what's there
    uint32_t lod;        // 0: a mesh with levels of detail has one picked. Else 1 + the level every one draws at
    uint32_t lod_camera; // 0: the pick is through `camera_3d`. Else 1 + the place in cameras_3d of the camera it's through
    uint32_t no_frustum_culling; // Zero enables rejection against this destination's camera
    tide_draw_bounds bounds;    // Optional mesh-local bounds, before the instance and Draw.Transform
    uint32_t tinted;  // Whether `tint` is anything but white: it's unset otherwise
    tide_color tint;  // What the commands to come have their colors multiplied by
    // What the commands recorded so far leave a renderer with. Where it isn't the above, the commands
    // that say so are recorded before the next one that draws: a setting that's set and put back with
    // nothing drawn records nothing, and neither does one that's set to what it was.
    uint32_t played_2d, played_3d, played_clip, played_stencil;
    uint32_t moved;                // TIDE_DRAW_MOVED_*: what of the transforms below there is, for the others are unset
    float scale;                   // How many times as big `transform` makes what has no direction: a radius, a width
    tide_draw_transform transform; // What 2D commands go through before the camera
    tide_float4x4 transform_3d;    // ...and 3D meshes' instances
} tide_draw_place;

enum {
    TIDE_DRAW_MOVED = 1,         // `transform` isn't the identity
    TIDE_DRAW_MOVED_UPRIGHT = 2, // ...and only moves and scales, each axis by more than 0: a rect stays a rect
    TIDE_DRAW_MOVED_EVEN = 4,    // ...both by as much: text is fitted to pixels
    TIDE_DRAW_MOVED_3D = 8,      // `transform_3d` isn't the identity
    TIDE_DRAW_MOVED_ROUND = 16,  // `transform` scales as much every way, turned or mirrored as it may: a circle stays one
    TIDE_DRAW_MOVED_MIRROR = 32, // `transform_3d` mirrors: what goes through it comes out wound the other way
};

// A 2D camera a list was given, or the screen.
typedef struct tide_draw_camera_data {
    tide_float2 center;
    float size;      // Half the height it sees, or 0 for a pixel a unit
    uint32_t screen; // 1: the screen's pixels instead, from the top left, y down
} tide_draw_camera_data;

// A 3D camera a list was given: from the world to clip space, as OpenGL's.
typedef struct tide_draw_camera_3d_data {
    tide_float4x4 matrix;
    // Its parts, which the matrix is made of: where the camera is in the world (it looks along its +z),
    // and what takes what it sees to clip space, looking down -z, for a square screen when `fit` says so
    tide_float4x4 transform, projection;
    uint32_t fit;      // 1: it's for a square screen (see TIDE_DRAW_CAMERA_3D)
    uint32_t mirrored; // 1: it shows the world mirrored, as a looking glass does
} tide_draw_camera_3d_data;

// A clip: a rect, inside the clip it was made in.
typedef struct tide_draw_clip_data {
    tide_rect rect;  // In the units of the camera it was made under, after the transform it was made in
    uint32_t camera; // 1 + that camera's place in tide_draw_list.cameras, or 0 for the frame's first camera
    uint32_t parent; // 1 + the place here of the clip it's inside, or 0 for none
} tide_draw_clip_data;

// A rendering destination named by recorded commands. Its identity is supplied
// by the resource owner; its size is captured when the command is recorded.
typedef struct tide_draw_target_data {
    uint64_t id;
    uint64_t version; // Resize revision, captured with its size.
    tide_target_size size;
    tide_draw_place place; // Its settings, while another destination is selected
} tide_draw_target_data;

// What a block's setting found, which the block's end puts back (see tide_draw_pop).
typedef struct tide_draw_saved {
    uint32_t setting; // Which one: TIDE_DRAW_SAVED_*
    uint32_t target;  // Whose, for a destination's own: 0 for the window, or 1 + its place in tide_draw_list.targets
    union {
        uint32_t value; // What a place has for its blend, a camera, a clip, its depth, its cull or its stencil
        tide_color tint;
        struct {
            tide_draw_transform transform;
            uint32_t moved;
            float scale;
        };
        tide_float4x4 transform_3d;
        tide_draw_bounds bounds;
    };
} tide_draw_saved;

enum {
    TIDE_DRAW_SAVED_BLEND,
    TIDE_DRAW_SAVED_TINT,
    TIDE_DRAW_SAVED_NO_TINT,
    TIDE_DRAW_SAVED_TRANSFORM,
    TIDE_DRAW_SAVED_TRANSFORM_3D,
    TIDE_DRAW_SAVED_NO_TRANSFORM_3D,
    TIDE_DRAW_SAVED_CLIP,
    TIDE_DRAW_SAVED_CAMERA_2D,
    TIDE_DRAW_SAVED_CAMERA_3D,
    TIDE_DRAW_SAVED_DEPTH,
    TIDE_DRAW_SAVED_CULL,
    TIDE_DRAW_SAVED_STENCIL,
    TIDE_DRAW_SAVED_LOD,
    TIDE_DRAW_SAVED_LOD_CAMERA,
    TIDE_DRAW_SAVED_FRUSTUM_CULLING,
    TIDE_DRAW_SAVED_BOUNDS,
};

// A 3D mesh's corner, as a mesh's but in 3D. Tide's `Vertex3` is the same.
typedef struct tide_vertex3 {
    tide_float3 position;
    tide_float2 uv;
    tide_color color;
} tide_vertex3;

// Triangles in 3D, where the game keeps them: three of `indices` each, places
// in `vertices`. As with a texture, nothing holds them on the GPU: the list
// copies them when it hasn't got them as they are, and the renderer uploads
// from that copy, so a mesh that doesn't change costs neither again.
typedef struct tide_mesh {
    const tide_vertex3 *vertices;
    uint32_t vertex_count;
    const uint32_t *indices;
    uint32_t index_count;
    uint64_t version; // Any number that's another whenever these are (see tide_mesh_version)
} tide_mesh;

// A mesh's levels of detail (docs/spec.md, Levels of detail): simpler lists
// of triangles over its own vertices, each about half the one before, which
// a draw picks from by how big the mesh is on screen. `error` is how far a
// level's surface is from the full mesh's, in the mesh's units, never less
// than the level before's. `center` and `radius` are a ball all of the mesh
// is in. A mesh marked [LOD] in Tide has them made as the game is built
// (tide/pack.h); a mesh of C's is given them with tide_draw_mesh_levels.
enum { TIDE_MESH_LEVELS = 7 }; // After the full mesh, at the most
typedef struct tide_mesh_levels {
    float center[3];
    float radius;
    uint32_t count;
    float error[TIDE_MESH_LEVELS];
    uint32_t first[TIDE_MESH_LEVELS];   // Each level's triangles in `index`...
    uint32_t indices[TIDE_MESH_LEVELS]; // ...three indices each
    const uint32_t *index;
    uint32_t index_count;
} tide_mesh_levels;

// A 3D mesh the list has, for the commands that draw it: a copy of its
// vertices and triangles at a version. It never changes while it has its
// place: another version of the mesh takes a place of its own, so what a
// frame drew before it changed still shows. One no command of a frame named
// is let go when the next frame starts, and one that frames go on drawing
// keeps its place, so renderers can keep theirs by its place.
typedef struct tide_draw_mesh_data {
    uint64_t id, other; // Whose: C's addresses of its vertices and indices, or Tide's lists
    uint64_t version;
    uint32_t space;     // What the ids are: TIDE_MESH_*, or TIDE_MESH_NONE for a free place
    uint32_t frame;     // The frame that last drew it
    uint32_t epoch;     // The list's when it was copied (see tide_draw_forget)
    uint32_t next_free; // A free place's: 1 + the next free one, or 0
    union {
        tide_vertex3 *vertices; // Its corners, or for a TIDE_MESH_BYTES mesh...
        void *bytes;            // ...its vertices as its shader reads them
    };
    uint32_t vertex_size; // Bytes each
    uint32_t vertex_count;
    uint32_t vertex_capacity; // Bytes `vertices` has room for
    uint32_t *indices; // Three a triangle, each a place in `vertices`: what's past them is left out
    uint32_t index_count;
    uint32_t index_capacity;
    // Its levels of detail (tide_draw_mesh_levels), each a mesh of its own in the list while a frame draws it,
    // with these vertices: `levels` of them after the full mesh, which is this one
    uint32_t levels;
    float center[3], radius;                   // A ball the mesh is in, in its own units
    float level_error[TIDE_MESH_LEVELS];       // How far each is off, never less than the one before
    uint32_t level_first[TIDE_MESH_LEVELS];    // Its triangles in `level_indices`...
    uint32_t level_count[TIDE_MESH_LEVELS];    // ...three indices each
    uint32_t level_mesh[TIDE_MESH_LEVELS];     // 1 + the place it last had, or 0
    uint32_t *level_indices;
    uint32_t level_index_count, level_index_capacity;
    // A level's own: 1 + the place of the mesh whose vertices it draws with (it has none: `vertices` is NULL,
    // and its `vertex_count` and `vertex_size` are that mesh's), and which of its levels it is, from 1. 0 for
    // every other mesh
    uint32_t base, level;
    tide_draw_bounds bounds; // Cached geometry AABB; independent of the LOD sphere
    uint32_t bounds_position; // 1 + byte offset of the float3 used for the cached bounds
} tide_draw_mesh_data;

// TIDE_MESH_BYTES: Tide's lists, their vertices as their struct has them (see
// tide_shader), where the others' are tide_vertex3s.
enum { TIDE_MESH_NONE, TIDE_MESH_MEMORY, TIDE_MESH_LISTS, TIDE_MESH_BYTES };

// ---------------------------------------------------------------------------
// Shaders: code of the game's that runs on the GPU, for each vertex of a mesh
// and each pixel it covers. tidec writes them from Tide's `vert` and `frag`
// declarations, one for each combination a mesh draw uses, in the generated
// C: nothing else makes one.

// What a vertex's field is in its list: 1 to 4 floats, or 1 to 4 ints.
typedef enum tide_shader_format {
    TIDE_SHADER_FLOAT,
    TIDE_SHADER_FLOAT2,
    TIDE_SHADER_FLOAT3,
    TIDE_SHADER_FLOAT4,
    TIDE_SHADER_INT,
    TIDE_SHADER_INT2,
    TIDE_SHADER_INT3,
    TIDE_SHADER_INT4,
} tide_shader_format;

// The most a shader takes: vertex attributes (its vertex's fields, and a
// float4 or int4 of its instance's each), and textures.

// How a shader reads one of its textures. A color is read as it is, from
// pixels or a target's color. A target's depth is read as a number, its
// nearest pixel's: or compared with a number of the shader's, which gives how
// much of the depth stored around there isn't nearer than it, 0 to 1, as the
// GPU filters it.
typedef enum tide_shader_read {
    TIDE_SHADER_READ_COLOR,
    TIDE_SHADER_READ_DEPTH,
    TIDE_SHADER_READ_COMPARE,
    TIDE_SHADER_READ_CUBE, // A cube's color, by a direction: a `samplerCube`, and a `texture_cube<f32>` with a `sampler`
    // A list's elements, four ints a pixel, each pixel by its place: an `isampler2D` read with texelFetch, and a
    // `texture_2d<i32>` read with textureLoad
    TIDE_SHADER_READ_DATA,
} tide_shader_read;

// The most bytes of the game's a shader's uniforms hold, after the engine's
// own: with those, 255 float4s, as OpenGL ES drivers that count a block's
// members among a shader's uniform vectors take 256 at most.
#define TIDE_SHADER_UNIFORM_BYTES 3984u

// A vertex shader and a fragment shader, in each language a renderer takes,
// and what they read:
//
// - A mesh's vertices, `vertex_size` bytes each, whose fields are the
//   attributes at locations 0 on.
// - An instance for each time the mesh is drawn: `instance_floats` float4s,
//   then `instance_ints` int4s, the attributes at the locations after the
//   vertex's. A 3D shader's first four float4s are the instance's transform,
//   column by column.
// - Uniforms for each draw, one block. The engine's own come first:
//   `tide_camera`, a matrix from the mesh's space to clip space as OpenGL has
//   it (the world through the 3D camera for a 3D shader; for a 2D one, the
//   units the draw is in, through the 2D camera or the screen, and the
//   transform the draw is in), then `tide_screen`, a float4: the target's
//   pixels in a logical one across and down, the target's height in its own
//   pixels, and 1 where the draw multiplies what's there (TIDE_BLEND_MULTIPLY:
//   its color goes to white as its alpha goes to 0) or 0; then `tide_tint`,
//   a float4 the pixel's color is multiplied by (see tide_draw_set_tint).
//   After them, `uniform_floats` float4s and `uniform_ints` int4s of the
//   game's.
// - `texture_count` textures: `pixels0`, `pixels1`... each read as
//   `texture_reads` says, a color where it says nothing. One read as depth is
//   a `sampler2D` whose red is the depth in GLSL, and a `texture_depth_2d`
//   with a `sampler` in WGSL; one that's compared is a `sampler2DShadow`, and
//   a `texture_depth_2d` with a `sampler_comparison`.
//
// Its fragment stage writes `outputs` colors, at locations 0 on (one, for 0):
// the first is the pixel's in the window and in a target's first color image,
// and the others go to the color images after it of a target that holds
// several (tide_render_target_make_of). Those a target has no image for go
// nowhere, and the images past them are left as they were.
//
// GLSL is without its version line, its uniforms a std140 block named
// `uniforms`. WGSL has both stages, `vertex` and `fragment`, its uniforms at
// binding 0 of group 0, and texture i and its sampler at bindings 1 + 2i and
// 2 + 2i; it's NULL off the web. SPIR-V, for Vulkan, is the WGSL's (tidec
// translates it): its entry points `vertex` and `fragment`, its bindings
// those of descriptor set 0; it's NULL where no build draws with Vulkan
// (where TIDE_SPIRV isn't defined).
#if !defined(__wasm__)
#define TIDE_SPIRV 1
#endif
typedef struct tide_shader {
    const char *name; // For messages: "Wave and Shade on BoxVertex"
    const char *glsl_vertex, *glsl_fragment;
    const char *wgsl;
    const uint32_t *spirv; // ...and its words
    uint32_t spirv_words;
    uint64_t id;   // A hash of all of the above and below: another shader, another id
    uint32_t dims; // 2: drawn in order, through the 2D camera or the screen; 3: through the 3D camera, with depth
    uint32_t vertex_size;
    uint32_t attribute_count; // The vertex's fields
    struct {
        uint32_t format; // tide_shader_format
        uint32_t offset; // In the vertex
    } attributes[TIDE_SHADER_ATTRIBUTES];
    uint32_t instance_floats, instance_ints;
    uint32_t uniform_floats, uniform_ints;
    uint32_t texture_count;
    uint8_t texture_reads[TIDE_SHADER_TEXTURES]; // tide_shader_read
    uint8_t outputs; // The colors its fragment stage writes: 1 for 0, TIDE_TARGET_COLORS at the most
    bool blit;       // Drawn by blits (tide_draw_blit) over a whole destination, rather than as a mesh
    uint32_t bounds_position; // 1 + float3 position's byte offset, only for undeformed 3D geometry; 0: unknown
} tide_shader;

static inline uint32_t tide_shader_outputs(const tide_shader *s)
{
    return s->outputs == 0 ? 1u : s->outputs < TIDE_TARGET_COLORS ? s->outputs : (uint32_t)TIDE_TARGET_COLORS;
}

static inline uint32_t tide_shader_instance_size(const tide_shader *s)
{
    return (s->instance_floats + s->instance_ints) * 16u;
}

static inline uint32_t tide_shader_uniform_size(const tide_shader *s)
{
    return (s->uniform_floats + s->uniform_ints) * 16u;
}

// A run of instances of one mesh through one shader, which renderers draw at
// once: a TIDE_DRAW_SHADED command's.
typedef struct tide_draw_run {
    const tide_shader *shader; // NULL once forgotten (see tide_draw_forget)
    uint32_t mesh;     // 1 + its place in tide_draw_list.meshes: a TIDE_MESH_BYTES one
    uint32_t first;    // Its instances: where they start in tide_draw_list.instances, in bytes...
    uint32_t count;    // ...and how many, tide_shader_instance_size each
    uint32_t uniforms; // Its uniforms of the game's: where they start in tide_draw_list.uniforms, in bytes
    uint64_t sampling; // Two eight-texture sampling banks (see TIDE_SAMPLING_WRAP).
    uint32_t textures[TIDE_SHADER_TEXTURES]; // 1 + each one's place in tide_draw_list.textures, or 0 for none: white
    tide_color tint;   // What its pixels' colors are multiplied by
    uint32_t moved;    // Whether a 2D shader's mesh goes through `transform` before the camera
    tide_draw_transform transform;
} tide_draw_run;

// One frame's commands. Not simulation state: it lives outside the world. A
// zeroed list is ready to use, and it grows as it needs, with no limit but
// memory: running out ends the program, as it does for worlds.
typedef struct tide_draw_list {
    tide_draw_command *commands; // `count` of them, with room for `capacity`
    uint32_t count;
    uint32_t capacity;
    char *text; // The commands' text, `text_used` bytes, with room for `text_capacity`
    uint32_t text_used;
    uint32_t text_capacity;
    tide_vertex *vertices; // The meshes' corners
    uint32_t vertex_count;
    uint32_t vertex_capacity;
    tide_draw_plain *plains; // The plain shapes (TIDE_DRAW_PLAIN)
    uint32_t plain_count;
    uint32_t plain_capacity;
    // 1 + the place of the last command where it's a run of plain shapes that
    // recording goes on with as it is (the blend there is, nothing to settle
    // or work in), or 0. Its shapes are the list's from its first on, and its
    // count is written as it closes: anything that records another command or
    // changes a setting closes it (tide_draw_plain_run).
    uint32_t plain_open;
    tide_draw_quad *quads; // The meshes that are one quad each (TIDE_DRAW_QUADS)
    uint32_t quad_count;
    uint32_t quad_capacity;
    uint32_t *indices; // ...and their triangles, each index a place in `vertices`
    uint32_t index_count;
    uint32_t index_capacity;
    tide_draw_texture *textures; // What the meshes draw with, kept from frame to frame while they do
    uint32_t texture_count;
    uint32_t texture_capacity;
    uint32_t *texture_index;         // Textures by space and id: 1 + a place, or 0 for none
    uint32_t texture_index_capacity; // A power of two, or 0
    tide_draw_target_data *targets;
    uint32_t target_count, target_capacity;
    uint32_t target; // Current destination, 1 + its place above; 0 is the window
    tide_draw_place place;  // The current destination's settings...
    tide_draw_place window; // ...and the window's, while a target is selected
    uint32_t unsettled;     // Whether `place` plays other than what it has (see tide_draw_place)
    uint32_t altered;       // Whether a 2D command is recorded other than as it's given: tinted, or moved
    tide_draw_camera_data *cameras; // The 2D cameras the frame was given, which places and clips name
    uint32_t camera_count, camera_capacity;
    tide_draw_camera_3d_data *cameras_3d; // ...and the 3D ones
    uint32_t camera_3d_count, camera_3d_capacity;
    tide_draw_clip_data *clips; // ...and the clips, which TIDE_DRAW_CLIP commands name too
    uint32_t clip_count, clip_capacity;
    tide_float2x2 *turns; // The turned TIDE_DRAW_TEXT commands' transforms: where the text's right and down go
    uint32_t turn_count, turn_capacity;
    tide_draw_saved *saved; // What the blocks that are open found (see tide_draw_pop)
    uint32_t saved_count, saved_capacity;
    uint32_t *remap; // tide_draw_append's: where another list's clips are in this one
    uint32_t remap_capacity;
    tide_float4x4 *matrices; // The 3D cameras' and the instances' of 3D meshes
    uint32_t matrix_count;
    uint32_t matrix_capacity;
    tide_draw_mesh_data *meshes; // What 3D meshes draw, kept from frame to frame while they do
    uint32_t mesh_count;         // Places, free ones too
    uint32_t mesh_capacity;
    uint32_t mesh_free;           // 1 + the first free place, or 0
    uint32_t *mesh_index;         // The meshes by whose they are: 1 + a place, or 0 for none
    uint32_t mesh_index_capacity; // A power of two, or 0
    // 1 + the place tide_draw_mesh_slot found last, or 0: a view drawing a mesh for each of its entities asks for
    // it again and again. Still that mesh's while the place has the same one, as only tide_draw_mesh_slot makes
    // places, one a mesh (a free place's is TIDE_MESH_NONE, which nobody asks for)
    uint32_t mesh_last;
    tide_draw_run *runs;          // The TIDE_DRAW_SHADED commands' runs
    uint32_t run_count;
    uint32_t run_capacity;
    uint8_t *instances;     // ...their instances, one after the other
    uint32_t instance_used; // Bytes
    uint32_t instance_capacity;
    uint8_t *uniforms; // ...and their uniforms
    uint32_t uniform_used;
    uint32_t uniform_capacity;
    tide_draw_shape_data *shapes; // The TIDE_DRAW_SHAPE commands' shapes
    uint32_t shape_count;
    uint32_t shape_capacity;
    const unsigned char *bold; // The bold weight's file, once a command of the frame draws with it: its renderer's
    uint32_t frame;   // Counts tide_draw_reset
    uint32_t epoch;   // Counts tide_draw_forget
    // The image of the pack's found last (its name, TIDE_TEXTURE_NAME of a
    // reference's version) and the list's texture for it, in frame
    // `pack_frame`: within a frame a name is the same image (pack_slot)
    uint64_t pack_named;
    uint32_t pack_slot;
    uint32_t pack_frame;
    struct tide_skinned_draw_cache *skinned; // Private frame-local palette captures
    // The window's size in logical pixels, supplied before recording (tide_draw_screen_size).
    // With none, window frustum culling and automatic LOD selection are skipped.
    tide_float2 screen;
    tide_float2 screen_scale; // Frame-host physical pixels per logical pixel, independently on x/y; zero means 1
    tide_target_pixels_fn target_pixels; // Borrowed frame metadata; no GPU readback
    float lod_pixels;  // How many pixels a level may be off by to be picked; 0 is 1
    uint32_t leveled;  // Whether the frame drew a level: its runs may want gathering (tide_draw_gather_levels)
    uint8_t *level_room; // Where the instances of a draw are sorted by their levels
    uint32_t level_room_capacity;
    // What the last pick went by, kept for the draws after it through the same camera into as many pixels: a
    // view that runs for each entity asks once a draw. `frame` + 1, the camera's place, and the pixels as bits
    uint32_t lod_seen[4];
    uint32_t frustum_seen[3]; // Camera, destination and epoch; reset and sizes also invalidate the cached matrix
    tide_float2 frustum_screen;
    tide_draw_frustum frustum;
    bool frustum_ready;
    float lod_view[6]; // How many times a pixel a unit of the world is, its camera's w row, and that row's length
} tide_draw_list;

// Empties the list for a new frame. It keeps its memory for the next, and the
// textures the frame drew with.
void tide_draw_reset(tide_draw_list *d);

// Completes captured CPU drawing work before a renderer reads the list's
// pixels. A pool can prepare independent palettes together; NULL runs the
// same work on this thread. Recording and GPU calls remain ordered.
struct tide_jobs;
void tide_draw_prepare(const tide_draw_list *d, const struct tide_jobs *jobs);

// Lets the list's memory go: it's empty, and ready to use again.
void tide_draw_free(tide_draw_list *d);

// Lets the list's textures go, and has it copy again whatever the commands to
// come draw with, and renderers upload it again: for a host whose game was
// swapped for another build (hot reloading), whose C names other pixels than
// the one before, maybe at the same address with the same version.
void tide_draw_forget(tide_draw_list *d);

// Selects the destination for commands recorded next. Re-selecting it records
// nothing, so adjacent compatible draws can still batch. Each destination has
// its own camera and clip for the frame. An id of 0 selects the window.
tide_draw_list *tide_draw_to_sized(tide_draw_list *d, uint64_t id, tide_target_size size);

// ...one of `width` by `height` logical pixels, at the display's scale.
static inline tide_draw_list *tide_draw_to(tide_draw_list *d, const uint64_t id, const float width, const float height)
{
    return tide_draw_to_sized(d, id, (tide_target_size){.width = width, .height = height});
}

static inline tide_draw_list *tide_draw_render_target(tide_draw_list *d, const tide_render_target target)
{
    const tide_render_target resolved = tide_render_target_resolve(target);
    // The target itself, whichever of its color images a reference it came from names
    return tide_draw_to_sized(d, resolved.id == TIDE_RENDER_WINDOW ? 0 : resolved.id ? tide_render_target_whole(resolved.id)
                                                                                     : TIDE_RENDER_INVALID,
                              resolved.size);
}

// A rendered destination's color as a texture slot, without a CPU copy.
uint32_t tide_draw_target_texture_sized(tide_draw_list *d, uint64_t id, tide_target_size size);

static inline uint32_t tide_draw_target_texture(tide_draw_list *d, const uint64_t id, const float width, const float height)
{
    return tide_draw_target_texture_sized(d, id, (tide_target_size){.width = width, .height = height});
}

// ...and its depth, for a shader that reads that texture as depth.
uint32_t tide_draw_target_depth_sized(tide_draw_list *d, uint64_t id, tide_target_size size);

static inline uint32_t tide_draw_target_depth(tide_draw_list *d, const uint64_t id, const float width, const float height)
{
    return tide_draw_target_depth_sized(d, id, (tide_target_size){.width = width, .height = height});
}

// A CPU image or target color, copied or referenced as its source requires.
uint32_t tide_draw_texture_source(tide_draw_list *d, tide_texture_ref texture);

// A target's depth: 0, no texture, for one that's no target's.
uint32_t tide_draw_depth_source(tide_draw_list *d, tide_depth_ref depth);

// A cube, for a shader that reads that texture as one: 0, no texture, for one that's no cube, and for one of
// the pack's that the loaded pack hasn't got.
uint32_t tide_draw_cube_source(tide_draw_list *d, tide_cubemap_ref cube);

// Fills the whole screen, or what the clip leaves of it, with `color` as it
// is: no blend, tint or transform changes a clear.
void tide_draw_clear(tide_draw_list *d, tide_color color);
// Sets depth back alone, over the whole screen or what the clip leaves of it:
// colors and stencil stay as they are, and what's drawn after is in front of
// everything drawn before. It's a TIDE_DRAW_CLEAR command whose `a.x` is 1.
void tide_draw_clear_depth(tide_draw_list *d);

// ---------------------------------------------------------------------------
// Settings: what the commands recorded after them are drawn with. Each frame
// starts from the defaults (tide_draw_reset).
//
// Every setting belongs to a destination, the window or a target, each with
// its own for the frame: how its commands blend and what tints them, its
// cameras, its clip, its transforms, what its 3D meshes do with its depth,
// which side of their triangles draws, and what its draws do with its
// stencil. Each is set, and pushed, for the destination that's selected
// (tide_draw_to), and reaches no other: a target's blend doesn't reach the
// window's draws.
//
// Each has two forms. tide_draw_set_* sets it for the commands to come, until
// it's set again, and tide_draw_unset_* puts it back as each frame starts.
// tide_draw_push_* sets it around some commands: it returns a mark, and
// tide_draw_pop with that mark puts back what the push found, along with what
// every push since it found, whatever was set meanwhile. A push combines with
// what's there where that means something: a clip only narrows the one it's
// in, a tint multiplies it, and a transform is inside it. A set sets outright.
//
// A setting that's set to what it was, or set and put back with nothing drawn
// between, records nothing, so it never comes between commands a renderer
// would draw at once.

// How the selected destination's commands after it go over what's there
// (tide_blend): by their alpha, each frame's first. Clears and blits don't
// blend.
void tide_draw_set_blend(tide_draw_list *d, tide_blend blend);
void tide_draw_unset_blend(tide_draw_list *d);
uint32_t tide_draw_push_blend(tide_draw_list *d, tide_blend blend);

// What the selected destination's commands after it have their colors
// multiplied by, alpha too: a shape's and text's colors, a mesh's corners',
// and what a shader of the game's wrote. White, each frame's first, changes
// nothing.
void tide_draw_set_tint(tide_draw_list *d, tide_color tint);
void tide_draw_unset_tint(tide_draw_list *d);
uint32_t tide_draw_push_tint(tide_draw_list *d, tide_color tint);

// Moves, turns and scales the 2D commands after it, in the units they're in,
// before the camera: none, each frame's first. Shapes stay shapes, moved,
// turned, mirrored and scaled as they are; one that's stretched (scaled
// more one way than the other) or skewed is the nearest shape to it: a circle
// is an ellipse, a rect's sides follow its own axes, and what has no
// direction (a radius, an outline's width) is scaled by the transform's mean.
// Meshes go through it as they are. Text that's only moved and scaled, as
// much each way, is fitted to pixels at the size it comes out; turned or
// stretched, it's drawn as it would be upright, turned, with no pixels to fit.
// A clip made inside one is the upright rect around what its rect becomes.
void tide_draw_set_transform(tide_draw_list *d, tide_draw_transform transform);
void tide_draw_unset_transform(tide_draw_list *d);
uint32_t tide_draw_push_transform(tide_draw_list *d, tide_draw_transform transform);

// The same for 3D meshes: each instance's transform goes through it on its
// way to the world.
void tide_draw_set_transform_3d(tide_draw_list *d, tide_float4x4 transform);
void tide_draw_unset_transform_3d(tide_draw_list *d);
uint32_t tide_draw_push_transform_3d(tide_draw_list *d, tide_float4x4 transform);

// What the selected destination's 3D meshes after it do with its depth
// (tide_depth), through a shader of the game's or not: read and written, each
// frame's first, so they hide each other by how far they are, whatever their
// order. A mesh that only reads it is hidden behind what was drawn nearer
// before it, and hides nothing: what's see-through is drawn so, after what's
// solid, and goes over what's there in the order it's drawn. Nothing else
// has depth: 2D commands neither read nor write it whatever this says, a
// clear sets it back, and a blit leaves it.
void tide_draw_set_depth(tide_draw_list *d, tide_depth depth);
void tide_draw_unset_depth(tide_draw_list *d);
uint32_t tide_draw_push_depth(tide_draw_list *d, tide_depth depth);

// Which side of the triangles of the selected destination's 3D meshes after
// it isn't drawn (tide_cull), through a shader of the game's or not: the
// back, each frame's first. The front of a triangle is the side its corners
// go clockwise on, as its mesh has them, whatever it's drawn through: an
// instance's transform that mirrors it, tide_draw_set_transform_3d's or a
// camera's leave its front its front. A transform that flattens a mesh has
// none to keep: such a mesh is drawn with TIDE_CULL_NONE. A shader's vertex
// stage that turns triangles round itself is the game's to draw with
// TIDE_CULL_FRONT. Nothing in 2D has a back: 2D commands, clears and blits
// draw whatever this says.
void tide_draw_set_cull(tide_draw_list *d, tide_cull cull);
void tide_draw_unset_cull(tide_draw_list *d);
uint32_t tide_draw_push_cull(tide_draw_list *d, tide_cull cull);

// What the selected destination's commands after it do with its stencil
// (tide_stencil): the test they pass to draw at a pixel, their number against
// the one stored there, and what becomes of the stored one where they draw.
// None, each frame's first: every pixel passes and nothing is stored. It's
// for everything that draws, in 2D and 3D, through a shader of the game's or
// not, and a destination's own, as its stencil is.
//
// A draw draws, and stores, at the pixels it touches, whatever its color: a
// clear one stores and leaves the colors there as they are, under every blend
// but TIDE_BLEND_REPLACE. A shape's pixels are the ones it covers any part
// of, and text's the ones its glyphs do; a mesh's are all of its triangles',
// whatever its texture's alpha, and a shader's the ones its fragment stage
// doesn't discard.
//
// A clear sets the stored numbers back to 0, within the clip, whatever this
// says, and a blit neither tests nor stores. A push replaces what's there.
void tide_draw_set_stencil(tide_draw_list *d, tide_stencil stencil);
void tide_draw_unset_stencil(tide_draw_list *d);
uint32_t tide_draw_push_stencil(tide_draw_list *d, tide_stencil stencil);

// Levels of detail: Tide's Draw.LOD, Draw.SetLOD, Draw.LODCamera and
// Draw.SetLODCamera (docs/spec.md, Levels of detail), the selected
// destination's own, as its cull is.
//
// A mesh that has levels (tide_draw_mesh_levels) draws the simplest whose
// error covers less than a pixel of the destination (tide_draw_lod_pixels),
// each instance its own, by its transform, the ball the mesh is in and the
// destination's 3D camera. A mesh with none, and a draw into a list nobody
// told the window's size (tide_draw_screen_size), draws whole.
//
// `set_lod` has every mesh drawn after it at `level` instead: 0 is the full
// mesh, 1 its first level, a number past a mesh's last its last, and one
// under 0 is 0. `unset_lod` goes back to picking. `push_lod` and
// `push_no_lod` are the two around a block (tide_draw_pop).
void tide_draw_set_lod(tide_draw_list *d, int32_t level);
void tide_draw_unset_lod(tide_draw_list *d);
uint32_t tide_draw_push_lod(tide_draw_list *d, int32_t level);
uint32_t tide_draw_push_no_lod(tide_draw_list *d);

// The camera the pick is made through, in place of the one the destination
// draws with: the draws go where they went. It's taken as the window's, so
// the pixels it counts are the window's. `unset` is the destination's own
// camera again.
void tide_draw_set_lod_camera_3d(tide_draw_list *d, tide_float3 position, tide_quaternion rotation, float field_of_view);
void tide_draw_set_lod_camera_matrices(tide_draw_list *d, tide_float4x4 transform, tide_float4x4 projection);
void tide_draw_unset_lod_camera(tide_draw_list *d);
uint32_t tide_draw_push_lod_camera_3d(tide_draw_list *d, tide_float3 position, tide_quaternion rotation, float field_of_view);
uint32_t tide_draw_push_lod_camera_matrices(tide_draw_list *d, tide_float4x4 transform, tide_float4x4 projection);
uint32_t tide_draw_push_no_lod_camera(tide_draw_list *d);

// The window's size in logical pixels, for the pick: a host says it before
// the frame's views draw, and it stays until it's said again.
void tide_draw_screen_size(tide_draw_list *d, tide_float2 size);

// Frustum culling is visual only, per destination, and enabled each frame.
// Unknown bounds or destination size keep the draw. Custom verts and skinning
// require explicit conservative mesh-local bounds. Size is the full box size.
void tide_draw_set_frustum_culling(tide_draw_list *d, bool enabled);
void tide_draw_unset_frustum_culling(tide_draw_list *d);
uint32_t tide_draw_push_frustum_culling(tide_draw_list *d, bool enabled);
void tide_draw_set_bounds(tide_draw_list *d, tide_float3 center, tide_float3 size);
void tide_draw_unset_bounds(tide_draw_list *d);
uint32_t tide_draw_push_bounds(tide_draw_list *d, tide_float3 center, tide_float3 size);
uint32_t tide_draw_push_no_bounds(tide_draw_list *d);

// How many pixels a level may be off by and still be picked, for every
// destination: 1 until it's said, and for a number that isn't above 0.
void tide_draw_lod_pixels(tide_draw_list *d, float pixels);

// Which level of `mesh` (what tide_draw_mesh_slot gave) an instance at
// `transform` draws at through the selected destination's settings, as the
// draws themselves pick: 0 for the full mesh.
uint32_t tide_draw_mesh_level(const tide_draw_list *d, uint32_t mesh, tide_float4x4 transform);

// Both of the selected destination's transforms, and both of its cameras, as each frame starts.
void tide_draw_unset_transforms(tide_draw_list *d);
void tide_draw_unset_cameras(tide_draw_list *d);

// Sets the camera for the commands after it: `center` is the world position
// at the middle of the screen, and `size` is half the visible height, like
// Unity's orthographic size. Each frame starts with the camera at the origin
// and 1 world unit per pixel.
void tide_draw_set_camera(tide_draw_list *d, tide_float2 center, float size);
void tide_draw_unset_camera(tide_draw_list *d);
uint32_t tide_draw_push_camera(tide_draw_list *d, tide_float2 center, float size);

// The commands after it are in the screen's pixels: from the top left, with y
// down, until the next camera.
void tide_draw_set_screen(tide_draw_list *d);
uint32_t tide_draw_push_screen(tide_draw_list *d);

// Puts back what the push that gave `mark` found, and every push since.
void tide_draw_pop(tide_draw_list *d, uint32_t mark);

// Selects the window and puts every setting back where a frame starts, every
// destination's own too: for what a host draws after a game's views, like the
// GUI.
void tide_draw_defaults(tide_draw_list *d);

// The tint the selected destination's commands to come are drawn with: white for none.
static inline tide_color tide_draw_tint(const tide_draw_list *d)
{
    return d->place.tinted ? d->place.tint : TIDE_COLOR_WHITE;
}

// The plain shapes: rects, circles, their outlines and lines. Recording one
// after another is the record's stores and the list's count, so that much is
// inline, in the code that draws: a frame of shapes spends its time here, and
// a call for each cost as much as the stores (on Windows a color is passed by
// its address, too). tide_draw_plain_placed does everything else: a tint or a
// transform worked in, settings to record first, a run of its own, room for
// more.
void tide_draw_plain_placed(tide_draw_list *d, tide_draw_kind kind, tide_float2 a, tide_float2 b, tide_color color);

static inline __attribute__((always_inline)) void tide_draw_plain_kind(tide_draw_list *d, const tide_draw_kind kind,
                                                                       const tide_float2 a, const tide_float2 b,
                                                                       const tide_color color)
{
    // More of the open run, where there's room
    const uint32_t at = d->plain_count;
    if (__builtin_expect(!d->plain_open || at == d->plain_capacity, 0)) {
        tide_draw_plain_placed(d, kind, a, b, color);
        return;
    }
    d->plains[at] = (tide_draw_plain){a, b, tide_draw_color_bytes(color), (uint8_t)kind, {0, 0, 0}};
    d->plain_count = at + 1u;
}

// How many plain shapes TIDE_DRAW_PLAIN command `c` of the list has: the open
// run's are the list's from its first on.
static inline uint32_t tide_draw_plain_run(const tide_draw_list *d, const tide_draw_command *c)
{
    return d->plain_open && c == &d->commands[d->plain_open - 1u] ? d->plain_count - c->plain.first : c->plain.count;
}

// A list's commands as they'd be with a command for each plain shape: each
// TIDE_DRAW_PLAIN run's shapes a command of its kind each, with its run's blend
// and its color (tide_draw_plain_color), and every other command as it is. For
// what reads a list command by command: tests, and the embed host's packet.
// Writes as many as `room` takes into `out` and returns how many there are.
uint32_t tide_draw_expand(const tide_draw_list *d, tide_draw_command *out, uint32_t room);

static inline void tide_draw_circle(tide_draw_list *d, const tide_float2 center, const float radius, const tide_color color)
{
    tide_draw_plain_kind(d, TIDE_DRAW_CIRCLE, center, (tide_float2){radius, 0.0f}, color);
}

static inline void tide_draw_wire_circle(tide_draw_list *d, const tide_float2 center, const float radius, const tide_color color)
{
    tide_draw_plain_kind(d, TIDE_DRAW_WIRE_CIRCLE, center, (tide_float2){radius, 0.0f}, color);
}

static inline void tide_draw_rect(tide_draw_list *d, const tide_float2 center, const tide_float2 size, const tide_color color)
{
    tide_draw_plain_kind(d, TIDE_DRAW_RECT, center, size, color);
}

static inline void tide_draw_wire_rect(tide_draw_list *d, const tide_float2 center, const tide_float2 size, const tide_color color)
{
    tide_draw_plain_kind(d, TIDE_DRAW_WIRE_RECT, center, size, color);
}

static inline void tide_draw_line(tide_draw_list *d, const tide_float2 from, const tide_float2 to, const tide_color color)
{
    tide_draw_plain_kind(d, TIDE_DRAW_LINE, from, to, color);
}

// ---------------------------------------------------------------------------
// Shapes with smooth edges. The rects and the line above fill whole pixels, so
// rects side by side leave no seam and a line is a pixel wide wherever it is
// (a circle's edge is smooth). These are drawn by how far each pixel is from
// the shape's edge, which is smooth over one pixel of the target at any size,
// turned any way. Lengths (sizes, radii, widths) are in the units the commands
// are in, and angles are in radians, from +x towards +y: counterclockwise
// under a camera, where y goes up, and clockwise on the screen, where it goes
// down. A size, radius or width that isn't positive draws nothing.

// The general shape, which the functions after it are short for: a box
// `size` wide and tall around `center`, turned by `rotation` about it.
typedef struct tide_shape {
    tide_float2 center;
    tide_float2 size;
    float rotation;
    float radius;      // Its corners', up to half its smaller side: a pill, or a circle for a square
    tide_color color;  // Its fill...
    tide_color color2; // ...which goes from `color` at its top to this at its bottom, as it is before it's
                       // turned: TIDE_COLOR_NONE for one color
    float outline;     // An outline this wide inside its edge...
    tide_color outline_color; // ...in this color
    float soft;        // Its edge fades over this far. Above 0, outwards: its color goes on past its edge, as a
                       // glow or a shadow. Under 0, inwards from its edge, leaving its middle clear.
    bool ellipse;      // The ellipse that fits `size` instead, whatever its radius
} tide_shape;

// A white shape with one color and no size, to set the rest of. A zeroed one's
// fill would fade to nothing, as a zeroed color2 is a color too.
#define TIDE_SHAPE_WHITE \
    ((tide_shape){.color = TIDE_COLOR_WHITE, .color2 = TIDE_COLOR_NONE, .outline_color = TIDE_COLOR_WHITE})

void tide_draw_shape(tide_draw_list *d, tide_shape shape);

// A line `width` thick, with round ends: it reaches half its width past them.
void tide_draw_thick_line(tide_draw_list *d, tide_float2 from, tide_float2 to, float width, tide_color color);

// Outlines `width` thick, inside the shape's edge.
void tide_draw_thick_wire_rect(tide_draw_list *d, tide_float2 center, tide_float2 size, float width, tide_color color);
void tide_draw_thick_wire_circle(tide_draw_list *d, tide_float2 center, float radius, float width, tide_color color);

void tide_draw_rect_rotated(tide_draw_list *d, tide_float2 center, tide_float2 size, float rotation, tide_color color);

// A rect whose corners are round, by `radius`. Recording one is inline where
// nothing set changes it and there's room, as a plain shape's is: its shape's
// numbers and its command, stored where the list keeps them (the corners'
// radius no more than half the smaller side, and none for less than 0 or NaN);
// tide_draw_round_rect_placed does the rest, and a shape with no size.
void tide_draw_round_rect_placed(tide_draw_list *d, tide_float2 center, tide_float2 size, float radius, tide_color color);

static inline void tide_draw_round_rect(tide_draw_list *d, const tide_float2 center, const tide_float2 size, const float radius,
                                        const tide_color color)
{
    if (__builtin_expect((d->altered | d->unsettled | d->plain_open) != 0u || d->count == d->capacity
                             || d->shape_count == d->shape_capacity || !(size.x > 0.0f) || !(size.y > 0.0f), 0)) {
        tide_draw_round_rect_placed(d, center, size, radius, color);
        return;
    }
    const float most = (size.x < size.y ? size.x : size.y) * 0.5f;
    tide_draw_shape_data *s = &d->shapes[d->shape_count];
    *s = (tide_draw_shape_data){.turn = {1.0f, 0.0f}, .radius = radius > most ? most : radius > 0.0f ? radius : 0.0f,
                                .form = TIDE_SHAPE_BOX, .color2 = color, .outline_color = {0.0f, 0.0f, 0.0f, 0.0f}};
    tide_draw_command *c = &d->commands[d->count++];
    *c = (tide_draw_command){.kind = TIDE_DRAW_SHAPE, .blend = (uint8_t)d->place.blend, .shape = d->shape_count++, .a = center,
                             .b = size, .color = color};
}
void tide_draw_round_rect_rotated(tide_draw_list *d, tide_float2 center, tide_float2 size, float radius, float rotation,
                                  tide_color color);
void tide_draw_wire_round_rect(tide_draw_list *d, tide_float2 center, tide_float2 size, float radius, float width,
                               tide_color color);

// Rect-based smooth boxes. Radii run clockwise from top left in the drawing's
// coordinates. Each is clamped to half the smaller side; invalid radii are 0.
static inline void tide_draw_rect_area_radius(tide_draw_list *d, const tide_rect rect, const float radius, const tide_color color)
{
    tide_draw_round_rect(d, tide_f2(rect.x + rect.width * 0.5f, rect.y + rect.height * 0.5f),
                        tide_f2(rect.width, rect.height), radius, color);
}
static inline void tide_draw_rect_area(tide_draw_list *d, const tide_rect rect, const tide_color color)
{
    tide_draw_rect_area_radius(d, rect, 0.0f, color);
}
void tide_draw_rect_area_corners(tide_draw_list *d, tide_rect rect, tide_float4 radii, tide_color color);

// An ellipse: `radii` is half its width and half its height.
void tide_draw_ellipse(tide_draw_list *d, tide_float2 center, tide_float2 radii, tide_color color);
void tide_draw_ellipse_rotated(tide_draw_list *d, tide_float2 center, tide_float2 radii, float rotation,
                               tide_color color);
void tide_draw_wire_ellipse(tide_draw_list *d, tide_float2 center, tide_float2 radii, float width, tide_color color);

// Part of a ring: of the circle outline `width` thick inside `radius`, what's
// from the angle `start` on for `sweep` (the other way round for a sweep under
// 0), cut straight across at both ends. A width of the radius is a slice of a
// disc, and a sweep of a whole turn or more the whole ring.
void tide_draw_arc(tide_draw_list *d, tide_float2 center, float radius, float start, float sweep, float width,
                   tide_color color);

// `position` is the text's top left corner and `size` its font size: the
// height of a line of it, with its capitals in the line's middle. A newline
// starts another line under it. The text is copied into the list.
void tide_draw_text(tide_draw_list *d, const char *text, tide_float2 position, float size, tide_color color);

// The same, with `position` the place `anchor` names of the text's box,
// which is as wide as its widest line and as tall as its lines: its middle
// for TIDE_ANCHOR_MIDDLE_CENTER, its top right corner for
// TIDE_ANCHOR_UPPER_RIGHT. Upper is towards the top of the screen, under a
// camera too. Each line is against the anchor's side: lines start at a left
// anchor, end at a right one, and are centered on one between. `font` is the
// weight it's drawn in: TIDE_FONT_REGULAR or TIDE_FONT_BOLD.
void tide_draw_text_at(tide_draw_list *d, const char *text, tide_float2 position, float size, tide_color color,
                       tide_anchor anchor, const unsigned char *font);

// Triangles: three of `indices` each, places in `vertices`, both copied into
// the list. Each pixel is its corners' colors blended across the triangle,
// times `texture`'s at their uvs (NULL for none: the colors alone), sampled
// with `filter`, and for uvs past 0 and 1, the pixels at its edge. Either
// side of a triangle draws. A triangle with an index past the vertices is
// left out.
//
// A mesh of four corners and two triangles that make a quad (tide_draw_quad)
// is kept as the quad, where nothing set changes a mesh (a tint, a
// transform): a TIDE_DRAW_QUADS command, a run of them, which the renderer
// draws as the same triangles. Any other mesh is its corners and triangles.
//
// More of the run the last command draws, as sprites and tiles of one image
// one after another are (its image, as it was and noted this frame, and its
// blend, filter and wrap, with nothing set since that changes a mesh, and
// every index in the mesh), is inline: the quad, or the corners and the
// triangles, stored after the list's, and the command's count, which the code
// that just built the corners may store straight there. tide_draw_mesh_placed
// does the rest as it always did, the same as this where both do it.
void tide_draw_mesh_placed(tide_draw_list *d, const tide_vertex *vertices, uint32_t vertex_count, const uint32_t *indices,
                           uint32_t index_count, const tide_texture *texture, tide_filter filter, tide_wrap wrap);

// The same with the texture's slot already found (tide_draw_texture_slot's, or 0 for none), as Tide's lists are drawn.
void tide_draw_mesh_with(tide_draw_list *d, const tide_vertex *vertices, uint32_t vertex_count, const uint32_t *indices,
                         uint32_t index_count, uint32_t slot, tide_filter filter, tide_wrap wrap);

// Whether a mesh with `texture` is drawn with the image the last command's run draws with, as texture_slot finds it.
static inline __attribute__((always_inline)) bool tide_draw_same_image(const tide_draw_list *d, const tide_draw_command *last,
                                                                       const tide_texture *texture)
{
    if (!texture || !texture->pixels) return last->mesh.texture == 0;
    if (!last->mesh.texture || last->mesh.texture > d->texture_count) return false;
    const tide_draw_texture *t = &d->textures[last->mesh.texture - 1u];
    return t->id == (uint64_t)(uintptr_t)texture->pixels && t->space == TIDE_PIXELS_MEMORY && t->frame == d->frame
        && t->version == texture->version && t->width == texture->width && t->height == texture->height
        && t->format == TIDE_TEXELS_BYTES;
}

// The same, with what uvs past 0 and 1 read: `wrap`.
static inline __attribute__((always_inline)) void tide_draw_mesh_wrapped(tide_draw_list *d, const tide_vertex *vertices, const uint32_t vertex_count,
                                          const uint32_t *indices, const uint32_t index_count, const tide_texture *texture,
                                          const tide_filter filter, const tide_wrap wrap)
{
    const uint32_t whole = index_count / 3u * 3u; // What's past the last whole triangle is left out
    if (d->count && !(d->altered | d->unsettled) && vertex_count && whole) {
        tide_draw_command *last = &d->commands[d->count - 1u];
        const bool goes_on = last->blend == d->place.blend && last->mesh.filter == (uint16_t)filter
                          && last->mesh.wrap == (uint16_t)wrap;
        if (vertex_count == 4u && index_count == 6u) {
            tide_draw_quad q;
            if (tide_draw_quad_of(vertices, indices, &q)) { // A quad, after the last command's or of a run of its own
                if (goes_on && last->kind == TIDE_DRAW_QUADS && last->mesh.first + last->mesh.count == d->quad_count
                    && d->quad_count < d->quad_capacity && tide_draw_same_image(d, last, texture)) {
                    d->quads[d->quad_count++] = q;
                    last->mesh.count++;
                    return;
                }
                tide_draw_mesh_placed(d, vertices, vertex_count, indices, index_count, texture, filter, wrap);
                return;
            }
        }
        if (goes_on && last->kind == TIDE_DRAW_MESH && last->mesh.first + last->mesh.count == d->index_count
            && vertex_count <= d->vertex_capacity - d->vertex_count && whole <= d->index_capacity - d->index_count
            && tide_draw_same_image(d, last, texture)) {
            bool within = true;
            for (uint32_t i = 0; i < whole; i++) within &= indices[i] < vertex_count;
            if (within) {
                const uint32_t base = d->vertex_count;
                __builtin_memcpy(d->vertices + base, vertices, (size_t)vertex_count * sizeof(tide_vertex));
                uint32_t *to = d->indices + d->index_count;
                for (uint32_t i = 0; i < whole; i++) to[i] = base + indices[i];
                d->vertex_count = base + vertex_count;
                d->index_count += whole;
                last->mesh.count += whole;
                return;
            }
        }
    }
    tide_draw_mesh_placed(d, vertices, vertex_count, indices, index_count, texture, filter, wrap);
}

static inline __attribute__((always_inline)) void tide_draw_mesh(tide_draw_list *d, const tide_vertex *vertices, const uint32_t vertex_count,
                                  const uint32_t *indices, const uint32_t index_count, const tide_texture *texture,
                                  const tide_filter filter)
{
    tide_draw_mesh_wrapped(d, vertices, vertex_count, indices, index_count, texture, filter, TIDE_WRAP_CLAMP);
}

// A texture drawn upright over a box (Draw.Sprite): `center` and `size` as
// tide_draw_rect's, the texture's top left corner at the box's top left (the
// top being towards the top of the screen, under a camera too), each pixel
// times `color`, read as the reference says (tide_texture_filter,
// tide_texture_wrap). A negative width or height mirrors it. Where nothing set
// changes it, it's a quad (tide_draw_quad), more of the last command's run
// where that has the same image; otherwise four corners and two triangles,
// p q r and p r s from the top left round, as tide_draw_mesh_with draws them.
// More of a run is inline, as a mesh's is; tide_draw_sprite_placed does the
// rest. `part` is the part of the texture it shows, in uvs: (0, 0) its top left
// corner and (1, 1) its bottom right. `rotation` turns it about its center, in
// radians from +x towards +y, as tide_draw_rect_rotated is.
void tide_draw_sprite_placed(tide_draw_list *d, const tide_texture_ref *texture, tide_float2 center, tide_float2 size,
                             tide_rect part, float rotation, tide_color color);

// A quad's pattern (tide_draw_quad.pattern) for corners p q r s from its top
// left round: q takes r's x and u, and s r's y and v.
#define TIDE_DRAW_QUAD_ROUND (5u << 4 | 15u << 8 | 15u << 16 | 10u << 20)

// Whether `texture` is the image the last command's run draws with, as
// tide_draw_texture_source would find it: C's pixels as tide_draw_same_image
// tells them, and an image of the pack's by the name found last this frame.
// Not a grid or a rendered image, which the list looks at each time.
static inline __attribute__((always_inline)) bool tide_draw_same_source(const tide_draw_list *d, const tide_draw_command *last,
                                                                        const tide_texture_ref *texture)
{
    if (texture->target.id) return false;
    if (texture->image.pixels) return tide_draw_same_image(d, last, &texture->image);
    const uint64_t name = texture->image.version & TIDE_TEXTURE_NAME;
    return texture->image.height != TIDE_TEXTURE_GRID && name && name == d->pack_named && d->pack_frame == d->frame
        && last->mesh.texture && last->mesh.texture == d->pack_slot;
}

static inline __attribute__((always_inline)) void tide_draw_sprite_part(tide_draw_list *d, const tide_texture_ref texture,
                                                                        const tide_float2 center, const tide_float2 size,
                                                                        const tide_rect part, const tide_color color)
{
    if (d->count && !(d->altered | d->unsettled) && d->quad_count < d->quad_capacity) {
        tide_draw_command *last = &d->commands[d->count - 1u];
        if (last->kind == TIDE_DRAW_QUADS && last->blend == d->place.blend && last->mesh.first + last->mesh.count == d->quad_count
            && last->mesh.filter == (uint16_t)tide_texture_filter(texture) && last->mesh.wrap == (uint16_t)tide_texture_wrap(texture)
            && tide_draw_same_source(d, last, &texture)) {
            // Its top towards the top of the screen: up under a camera, down the screen's y on it
            const uint32_t camera = d->place.camera_2d;
            const bool down = camera && camera <= d->camera_count && d->cameras[camera - 1u].screen;
            const float half = size.x * 0.5f, top = down ? size.y * -0.5f : size.y * 0.5f;
            d->quads[d->quad_count++] = (tide_draw_quad){{center.x - half, center.y + top}, {center.x + half, center.y - top},
                                                         {part.x, part.y}, {part.x + part.width, part.y + part.height}, color,
                                                         TIDE_DRAW_QUAD_ROUND};
            last->mesh.count++;
            return;
        }
    }
    tide_draw_sprite_placed(d, &texture, center, size, part, 0.0f, color);
}

static inline __attribute__((always_inline)) void tide_draw_sprite(tide_draw_list *d, const tide_texture_ref texture,
                                                                   const tide_float2 center, const tide_float2 size,
                                                                   const tide_color color)
{
    tide_draw_sprite_part(d, texture, center, size, (tide_rect){0.0f, 0.0f, 1.0f, 1.0f}, color);
}

static inline void tide_draw_sprite_rotated(tide_draw_list *d, const tide_texture_ref texture, const tide_float2 center,
                                            const tide_float2 size, const float rotation, const tide_color color)
{
    tide_draw_sprite_placed(d, &texture, center, size, (tide_rect){0.0f, 0.0f, 1.0f, 1.0f}, rotation, color);
}

static inline void tide_draw_sprite_part_rotated(tide_draw_list *d, const tide_texture_ref texture, const tide_float2 center,
                                                 const tide_float2 size, const tide_rect part, const float rotation,
                                                 const tide_color color)
{
    tide_draw_sprite_placed(d, &texture, center, size, part, rotation, color);
}

// The same in two steps, for vertices several meshes share, like a GUI
// library's, whose every batch has a texture and a clip of its own:
// tide_draw_vertices copies them into the list and returns where the first
// is, and tide_draw_triangles draws triangles of the list's vertices from
// `base` on. The vertices are moved and tinted as they're copied, by the
// transform and the tint there are then.
uint32_t tide_draw_vertices(tide_draw_list *d, const tide_vertex *vertices, uint32_t count);
void tide_draw_triangles(tide_draw_list *d, uint32_t base, const uint32_t *indices, uint32_t count,
                         const tide_texture *texture, tide_filter filter);
void tide_draw_triangles_wrapped(tide_draw_list *d, uint32_t base, const uint32_t *indices, uint32_t count,
                                 const tide_texture *texture, tide_filter filter, tide_wrap wrap);

// A version for pixels that change rarely: a hash of them. C that knows when
// its pixels change can count instead.
uint64_t tide_texture_version(const void *pixels, int32_t width, int32_t height);

// Only what's inside `rect` draws, for the commands after it, clears too,
// until another clip or tide_draw_unset_clip. The rect is in the units the
// commands are in (the world's under a camera, pixels on the screen), from
// (x, y) to (x + width, y + height), and stays where it is on the screen when
// the camera changes. Each frame starts with none. A pushed one is what of
// `rect` is inside the clip there was.
void tide_draw_set_clip(tide_draw_list *d, tide_rect rect);
void tide_draw_unset_clip(tide_draw_list *d);
uint32_t tide_draw_push_clip(tide_draw_list *d, tide_rect rect);

// Adds `from`'s commands to the end of `d`, such as the GUI's over the world,
// as `from` recorded them: none of `d`'s settings changes them. It starts
// from tide_draw_defaults, and leaves the cameras and clips `from` ended with.
void tide_draw_append(tide_draw_list *d, const tide_draw_list *from);

// GUI drawing scopes restore a previously captured window place while keeping
// the renderer's current played state. The mark also closes nested settings.
void tide_draw_restore_window(tide_draw_list *d, const tide_draw_place *before, uint32_t mark);

// Reposition the window's 2D content of an anchored GUI area. Plain records
// before `plains` and clips before `clips` belong to the surrounding content.
void tide_draw_move_gui(tide_draw_list *d, uint32_t commands, uint32_t plains, uint32_t clips, tide_float2 offset);

// The texture `id` of `space` (TIDE_PIXELS_*) at `version`, for a mesh
// command: 1 + its place in the list's textures, or 0 when it has no size.
// When the list hasn't got its pixels as they are (it's new, or its version
// or size changed), *pixels is where to write them: width * height * 4 bytes.
// Otherwise it's NULL.
uint32_t tide_draw_texture_slot(tide_draw_list *d, uint32_t space, uint64_t id, uint64_t version, int32_t width,
                                int32_t height, uint8_t **pixels);

// ...whose pixels are `format` (TIDE_TEXELS_*): width * height of them to write.
uint32_t tide_draw_texture_slot_as(tide_draw_list *d, uint32_t space, uint64_t id, uint64_t version, int32_t width,
                                   int32_t height, uint32_t format, uint8_t **pixels);

// The same, for a writer that can tell what changed, so that a big texture a
// frame changes a little of costs what changed: a few pixels written here,
// and those alone sent to the GPU. `kind` is the writer's own number, never
// 0, for what it writes and how. Where the list has the texture at another
// version, with the same size and format, last written by a writer of that
// kind, *kept is true and *pixels are the ones it had: the writer changes
// those that differ and says where (tide_draw_texture_changed). Otherwise
// *kept is false and all of them are to write, as tide_draw_texture_slot_as's
// are, whatever is in them.
uint32_t tide_draw_texture_edit(tide_draw_list *d, uint32_t space, uint64_t id, uint64_t version, int32_t width,
                                int32_t height, uint32_t format, uint64_t kind, uint8_t **pixels, bool *kept);

// The pixels of the texture tide_draw_texture_edit gave from (x, y), `width`
// by `height` of them, aren't what they were. What's outside the texture is
// left out.
void tide_draw_texture_changed(tide_draw_list *d, uint32_t texture, int32_t x, int32_t y, int32_t width, int32_t height);

// `count` numbers the writer keeps with that texture, one for each part it
// copies (a chunk's hash, to tell the chunks that changed without reading
// them), as it left them: zeros for a texture whose pixels weren't kept, and
// for the numbers past those it had.
uint64_t *tide_draw_texture_parts(tide_draw_list *d, uint32_t texture, uint32_t count);

// ...and one number more, the same way (how much of the texture is in use).
uint64_t *tide_draw_texture_used(tide_draw_list *d, uint32_t texture);

// For a renderer that has the texture's pixels as they were at version `had`:
// whether the list knows all that differs since, and if so, where, in
// *areas, *count of them (none, where only the version differs). False: the
// renderer takes all of the pixels.
bool tide_draw_texture_since(const tide_draw_texture *texture, uint64_t had, const tide_draw_area **areas, uint32_t *count);

// tide_draw_triangles_wrapped, with what tide_draw_texture_slot gave as the
// texture. Triangles after others with the same texture, filter and wrap are
// one command.
void tide_draw_triangles_with(tide_draw_list *d, uint32_t base, const uint32_t *indices, uint32_t count,
                              uint32_t texture, tide_filter filter, tide_wrap wrap);

// ---------------------------------------------------------------------------
// 3D

// Sets the 3D camera for the 3D meshes after it: where it is, which way it
// looks (along its rotation's +z, its up its +y), and how much it sees up
// and down, in degrees, like Unity's Camera.fieldOfView. It sees from 0.3
// units in front of it on, however far, and as much across as the screen's
// shape gives. Other commands keep the camera they have. Each frame starts
// with one at the origin, looking along +z, that sees 60 degrees.
void tide_draw_set_camera_3d(tide_draw_list *d, tide_float3 position, tide_quaternion rotation, float field_of_view);
void tide_draw_unset_camera_3d(tide_draw_list *d);
uint32_t tide_draw_push_camera_3d(tide_draw_list *d, tide_float3 position, tide_quaternion rotation,
                                  float field_of_view);

// What tide_draw_set_camera_3d records: from the world to clip space, for a
// square screen (TIDE_DRAW_CAMERA_3D's `fit`).
tide_float4x4 tide_draw_camera_3d_matrix(tide_float3 position, tide_quaternion rotation, float field_of_view);

// The same with any projection: `transform` places the camera in the world
// (it looks along its +z), and `projection` takes what it sees, looking down
// -z as OpenGL's and Unity's views do, to clip space (see
// tide_perspectivefov_f4x4 and tide_ortho_f4x4), the screen's shape included.
void tide_draw_set_camera_matrices(tide_draw_list *d, tide_float4x4 transform, tide_float4x4 projection);
uint32_t tide_draw_push_camera_matrices(tide_draw_list *d, tide_float4x4 transform, tide_float4x4 projection);

// The camera each frame starts with, as tide_draw_set_camera_3d would record it.
tide_draw_camera_3d_data tide_draw_first_camera_3d(void);

// A matrix that's for a square screen (TIDE_DRAW_CAMERA_3D's `fit`, or such a
// camera's `projection`), for one `width` by `height`: x divided by the
// width over the height. It's what a renderer does to such a camera.
tide_float4x4 tide_draw_camera_fit(tide_float4x4 matrix, float width, float height);

// A 3D mesh, placed in the world by `transform`: each pixel is its corners'
// colors blended across the triangle, times `texture`'s at their uvs (NULL
// for none), sampled with `filter`, and for uvs past 0 and 1, the pixels at
// its edge. Either side of a triangle draws. Instances of one mesh drawn one
// after another, with one texture, filter and wrap, are one command, which
// renderers draw at once.
//
// The mesh asked for last again, with no texture and room for its transform,
// as a view drawing a mesh for each of its entities asks: inline, the
// transform stored straight where the list keeps the next instance's, from
// where the caller has it, and tide_draw_instance_stored records the rest.
// tide_draw_mesh_3d_placed does all of it otherwise, as it always did.
void tide_draw_mesh_3d_placed(tide_draw_list *d, const tide_mesh *mesh, const tide_float4x4 *transform,
                              const tide_texture *texture, tide_filter filter, tide_wrap wrap);

// An instance of mesh place `mesh` (1 + it, a valid one) with no texture, whose transform is the next of the list's
// matrices, stored there and not yet counted: culled, or counted and recorded.
void tide_draw_instance_stored(tide_draw_list *d, uint32_t mesh, tide_filter filter, tide_wrap wrap);

// The same, with what uvs past 0 and 1 read: `wrap`.
static inline void tide_draw_mesh_3d_wrapped(tide_draw_list *d, const tide_mesh *mesh, const tide_float4x4 transform,
                                             const tide_texture *texture, const tide_filter filter, const tide_wrap wrap)
{
    if (mesh && mesh->vertices && mesh->indices && (!texture || !texture->pixels) && d->mesh_last
        && d->mesh_last <= d->mesh_count && d->matrix_count < d->matrix_capacity) {
        tide_draw_mesh_data *m = &d->meshes[d->mesh_last - 1u];
        if (m->space == TIDE_MESH_MEMORY && m->id == (uint64_t)(uintptr_t)mesh->vertices
            && m->other == (uint64_t)(uintptr_t)mesh->indices && m->version == mesh->version) {
            m->frame = d->frame;
            d->matrices[d->matrix_count] = transform;
            tide_draw_instance_stored(d, d->mesh_last, filter, wrap);
            return;
        }
    }
    tide_draw_mesh_3d_placed(d, mesh, &transform, texture, filter, wrap);
}

static inline void tide_draw_mesh_3d(tide_draw_list *d, const tide_mesh *mesh, const tide_float4x4 transform,
                                     const tide_texture *texture, const tide_filter filter)
{
    tide_draw_mesh_3d_wrapped(d, mesh, transform, texture, filter, TIDE_WRAP_CLAMP);
}

// A version for vertices and indices that change rarely: a hash of them.
uint64_t tide_mesh_version(const tide_mesh *mesh);

// The same in steps, for meshes kept elsewhere, like Tide's lists: the mesh
// `id` and `other` of `space` (TIDE_MESH_*) at `version`, for a 3D mesh
// command: 1 + its place in the list's meshes. When the list hasn't got it
// at that version, *fill is true, and tide_draw_mesh_fill gives it its
// vertices and indices before anything draws it.
uint32_t tide_draw_mesh_slot(tide_draw_list *d, uint32_t space, uint64_t id, uint64_t other, uint64_t version,
                             bool *fill);
void tide_draw_mesh_fill(tide_draw_list *d, uint32_t mesh, const tide_vertex3 *vertices, uint32_t vertex_count,
                         const uint32_t *indices, uint32_t index_count);

// Gives the mesh tide_draw_mesh_slot gave, once it's filled, its levels of
// detail, which the list copies: each is triangles of the mesh's own
// vertices, and one with an index past them is left out, as in the mesh's
// own. The instances drawn after it pick among them (tide_draw_set_lod).
// NULL, or a count of 0, is none. A level is a mesh of the list's while a
// frame draws it, with the full mesh's vertices and no copy of them, so a
// renderer has each vertex once however many levels show.
void tide_draw_mesh_levels(tide_draw_list *d, uint32_t mesh, const tide_mesh_levels *levels);

// Puts the runs of one mesh's levels that were recorded one after the other
// together, a run a level, the finest first, each with its instances in the
// order they were drawn: a view that runs for each entity draws its mesh at
// whatever level each entity picks, which would otherwise be a run for every
// change of level. tide_draw_prepare does it; a renderer that never calls
// that one draws the same picture in more runs.
void tide_draw_gather_levels(const tide_draw_list *d);

// An instance of what tide_draw_mesh_slot gave, with what
// tide_draw_texture_slot gave as the texture, and how it's sampled.
void tide_draw_instance(tide_draw_list *d, uint32_t mesh, tide_float4x4 transform, uint32_t texture, tide_filter filter,
                        tide_wrap wrap);

// The same for `count` transforms, copied in their order into one run.
// With no transforms, nothing is recorded.
void tide_draw_instances(tide_draw_list *d, uint32_t mesh, const tide_float4x4 *transforms, uint32_t count,
                         uint32_t texture, tide_filter filter, tide_wrap wrap);

// tide_draw_mesh_fill for a TIDE_MESH_BYTES mesh: vertices of `vertex_size`
// bytes each, as a shader reads them.
void tide_draw_mesh_fill_bytes(tide_draw_list *d, uint32_t mesh, const void *vertices, uint32_t vertex_size,
                               uint32_t vertex_count, const uint32_t *indices, uint32_t index_count);

// An instance of the TIDE_MESH_BYTES mesh tide_draw_mesh_slot gave, through
// `shader`: its instance's bytes (tide_shader_instance_size of them), the
// draw's uniforms of the game's (tide_shader_uniform_size), and what
// tide_draw_texture_slot gave for each of its textures, with how each is
// sampled in `sampling`: bit i set for one sampled blended, and its wrap from
// bit TIDE_SAMPLING_WRAP on (tide_sampling_wrapped), none for the pixels at
// its edge. The bytes are copied; a pointer can be NULL when the shader has
// none of its kind, and with none where it has some, nothing is drawn.
// Instances of one mesh drawn one after another through one shader, with the
// same uniforms, textures and sampling, are one command, which renderers draw
// at once.
void tide_draw_shaded(tide_draw_list *d, const tide_shader *shader, uint32_t mesh, const void *instance,
                      const void *uniforms, const uint32_t *textures, uint64_t sampling);

// The same for `count` instances, tide_shader_instance_size bytes each,
// copied in their order into one run. A shader with no instance bytes may
// take NULL with a positive count. A count of zero records nothing.
void tide_draw_shaded_instances(tide_draw_list *d, const tide_shader *shader, uint32_t mesh, const void *instances,
                                uint32_t count, const void *uniforms, const uint32_t *textures, uint64_t sampling);

// Runs a frag over the entire selected destination. The fixed Vertex mesh has
// clip-space positions, top-left uvs, and white color. The camera is identity,
// clipping is disabled for this draw alone, and none of the settings apply:
// its output goes over the destination's color as `blend` says
// (TIDE_BLEND_REPLACE: in its place), without changing its depth. Texture
// bindings and packed shader data are the same as tide_draw_shaded's.
// Sampling the destination is refused.
void tide_draw_blit(tide_draw_list *d, const tide_shader *shader, tide_blend blend, const void *instance,
                   const void *uniforms, const uint32_t *textures, uint64_t sampling);
// ---------------------------------------------------------------------------
// 3D primitives: solid meshes, through the 3D camera and with depth, as
// tide_draw_mesh_3d. Every shape is centered on `center`; its geometry is
// kept by shape and color, and repeated instances draw at once. A size,
// radius or height that is not positive draws nothing.

// A cube's `size` is its full width, height and depth. The rotated form
// turns its local axes by `rotation` before placing it in the world.
void tide_draw_cube(tide_draw_list *d, tide_float3 center, tide_float3 size, tide_color color);
void tide_draw_cube_rotated(tide_draw_list *d, tide_float3 center, tide_float3 size, tide_quaternion rotation,
                             tide_color color);

void tide_draw_sphere(tide_draw_list *d, tide_float3 center, float radius, tide_color color);

// A cylinder's axis is local +y, with the full `height` around its center
// and a circular cap at each end.
void tide_draw_cylinder(tide_draw_list *d, tide_float3 center, float radius, float height, tide_color color);
void tide_draw_cylinder_rotated(tide_draw_list *d, tide_float3 center, float radius, float height,
                                 tide_quaternion rotation, tide_color color);

// A plane is in local XZ: `size.x` along x, `size.y` along z. Both sides
// draw, as for other meshes.
void tide_draw_plane(tide_draw_list *d, tide_float3 center, tide_float2 size, tide_color color);
void tide_draw_plane_rotated(tide_draw_list *d, tide_float3 center, tide_float2 size, tide_quaternion rotation,
                              tide_color color);

// ---------------------------------------------------------------------------
// Coordinates through the current camera, for the commands recorded next.
// `screen_size` is the window's width and height in logical pixels. When a
// target is selected, its dimensions are used instead. Screen
// positions are from the top left, with y down, as tide_draw_screen and the
// GUI's are. Device positions are from the bottom left:
// tide_draw_screen_flip gives them as screen positions.
//
// Each target keeps its own cameras. The 2D helpers use its 2D camera or
// screen as it's set, or the default centered camera at one world unit per
// pixel. In screen mode both are the identity. A 3D camera leaves them as
// they were, and 2D commands leave the 3D camera as it was. Neither needs a
// frame to have been rendered. They're the camera's alone: a transform
// (tide_draw_set_transform) is no part of them.
tide_float2 tide_draw_world_to_screen(const tide_draw_list *d, tide_float2 point, tide_float2 screen_size);
tide_float2 tide_draw_screen_to_world(const tide_draw_list *d, tide_float2 point, tide_float2 screen_size);

// The same point counted from the window's other edge: a device position
// (pixels from the bottom left, y up) as a screen position (from the top left,
// y down), and a screen position as a device's. It's the window's whatever
// target is selected, as the devices are.
tide_float2 tide_draw_screen_flip(tide_float2 point, tide_float2 screen_size);

// The 3D helpers use the most recent 3D camera, or the default perspective
// camera. A screen position's z is depth: 0 at the near plane, 1 at the far
// plane, as the GPU's depth buffer, rather than distance from the camera.
// Coordinates and depth are not clamped to the viewport. With the standard
// infinite perspective camera, depth 1 has no finite world position.
tide_float3 tide_draw_world_to_screen_3d(const tide_draw_list *d, tide_float3 point, tide_float2 screen_size);
tide_float3 tide_draw_screen_to_world_3d(const tide_draw_list *d, tide_float3 point, tide_float2 screen_size);

// A ray through the screen point, starting at the camera's near plane and
// pointing into the scene. The direction is normalized; this works with
// perspective cameras and orthographic or other matrix projections.
tide_float3 tide_draw_screen_ray_origin(const tide_draw_list *d, tide_float2 point, tide_float2 screen_size);
tide_float3 tide_draw_screen_ray_direction(const tide_draw_list *d, tide_float2 point, tide_float2 screen_size);

// The selected destination's 3D camera as it's set, for the commands recorded
// next, fitted to the destination's shape: the matrices a renderer draws its
// 3D meshes through, so a point through tide_draw_world_to_clip is where the
// GPU puts it (clip space as OpenGL's: x and y from -1 to 1 with y up, z from
// -1 at the near plane to 1 at the far one, before the division by w).
// tide_draw_camera_transform is where the camera is in the world, and
// tide_draw_camera_projection what takes what it sees to clip space: the two
// tide_draw_set_camera_matrices takes, which given back to it set the same
// camera. Nothing of a transform (tide_draw_set_transform_3d) is in them.
// A destination with no size has zeros for the first and the last.
tide_float4x4 tide_draw_world_to_clip(const tide_draw_list *d, tide_float2 screen_size);
tide_float4x4 tide_draw_camera_transform(const tide_draw_list *d, tide_float2 screen_size);
tide_float4x4 tide_draw_camera_projection(const tide_draw_list *d, tide_float2 screen_size);

// The selected destination's 2D camera as a matrix: what
// tide_draw_world_to_screen does to a point (x, y, 1). The identity on the
// screen, and zeros where tide_draw_world_to_screen gives zeros for every point.
tide_float3x3 tide_draw_world_to_screen_matrix(const tide_draw_list *d, tide_float2 screen_size);

// All coordinate helpers return a zero vector when an input is non-finite,
// screen size is non-positive, the camera is singular, or a homogeneous
// projection has no finite point. A point outside the view is still mapped.

// How big a renderer draws text: of `text` drawn `size` tall, the width of
// its widest line and the height of its lines, in the units `size` is in. One
// line is `size` high, and each newline adds what's from one line to the
// next. Text is fitted to the pixels it's drawn on, so where it's drawn is
// part of the answer: in the window (NULL), or in a render target of the size
// `target` says, whose image has pixels of its own. Either way the units are
// logical pixels. The platform measures with its font
// (tide_platform_measure_text), in the weight `font` is the file of:
// TIDE_FONT_REGULAR or TIDE_FONT_BOLD.
typedef tide_float2 (*tide_measure_fn)(const char *text, float size, const tide_target_size *target,
                                       const unsigned char *font);

// What stands in for a host's tide_measure_fn when it has none (NULL), as
// tests and servers do: a guess, on the wide side, the same for every weight.
tide_float2 tide_draw_guess_text(const char *text, float size, const tide_target_size *target, const unsigned char *font);

// The size of `text` as tide_draw_text_at would draw it now, at `size` in the
// weight `font` is the file of, in the units the commands recorded next are
// in: the screen's pixels on the screen, and world units through the current
// 2D camera, which draws text as many times bigger as it has pixels to a
// unit, or the units a transform takes when there's one. `x` is its widest
// line's width and `y` the height of its lines: its box, which an anchor
// names a place of. `screen_size` is as for the coordinate helpers above, and
// `measure` the renderer's, or NULL to guess. Nothing for a size that isn't
// above 0, as nothing is drawn.
tide_float2 tide_draw_measure_text(const tide_draw_list *d, const char *text, float size, const unsigned char *font,
                                   tide_float2 screen_size, tide_measure_fn measure);
