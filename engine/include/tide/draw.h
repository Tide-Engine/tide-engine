#pragma once

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
    TIDE_DRAW_CLEAR,       // color
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
        struct {              // TIDE_DRAW_MESH
            uint32_t first;   // Its indices in tide_draw_list.indices, three a triangle...
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

// How a mesh samples its texture between pixels. Tide's `Filter` has the same
// values, which are Unity's FilterMode's.
typedef enum tide_filter {
    TIDE_FILTER_POINT,    // The nearest pixel: hard edges
    TIDE_FILTER_BILINEAR, // Blended with its neighbors
} tide_filter;

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

// How a shader's textures are sampled, a word for all of them (a
// tide_draw_run's `sampling`): bit i is set for texture i read blended with
// its neighbors, and clear for the nearest pixel, and the two bits from
// TIDE_SAMPLING_WRAP + 2 * i are its tide_wrap. So a word with only its low
// bits set clamps every texture.
#define TIDE_SAMPLING_WRAP 8u

static inline uint32_t tide_sampling_wrapped(const uint32_t texture, const int32_t wrap)
{
    return (uint32_t)tide_wrap_of(wrap) << (TIDE_SAMPLING_WRAP + 2u * texture);
}

static inline bool tide_sampling_blended(const uint32_t sampling, const uint32_t texture)
{
    return (sampling >> texture & 1u) != 0;
}

static inline tide_wrap tide_sampling_wrap(const uint32_t sampling, const uint32_t texture)
{
    return tide_wrap_of((int32_t)(sampling >> (TIDE_SAMPLING_WRAP + 2u * texture) & 3u));
}

// Pixels to draw with, where the game keeps them. Nothing holds a texture on
// the GPU: a mesh names its pixels, the list copies them when it hasn't got
// them as they are, and the renderer uploads from that copy. So C can keep
// its pixels wherever it likes, and say when they changed.
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

static inline bool tide_render_target_managed(const uint64_t id)
{
    return (id & TIDE_RENDER_MANAGED) && id != TIDE_RENDER_WINDOW && id != TIDE_RENDER_INVALID;
}

static inline tide_render_target_state *tide_render_target_state_of(const uint64_t id)
{
    return tide_render_target_managed(id)
        ? (tide_render_target_state *)(uintptr_t)(id & ~(TIDE_RENDER_MANAGED | TIDE_RENDER_BITS)) : NULL;
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

// A reference's sampling: bit 0 for the nearest pixel (none: blended, as a
// draw that says nothing is), and its tide_wrap from bit 2. The rest of
// `image.version` is TIDE_TEXTURE_NAME: a pack image's name, or a grid's
// place and cells.
#define TIDE_TEXTURE_SAMPLING_SHIFT 60u
#define TIDE_TEXTURE_NAME ((UINT64_C(1) << TIDE_TEXTURE_SAMPLING_SHIFT) - 1u)
#define TIDE_TEXTURE_GRID (-1) // A grid's reference's `image.height`

static inline uint32_t tide_sampling_bits(const int32_t filter, const int32_t wrap)
{
    return (filter == TIDE_FILTER_POINT ? 1u : 0u) | (uint32_t)tide_wrap_of(wrap) << 2;
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
    return tide_texture_sampling(texture) & 1u ? TIDE_FILTER_POINT : TIDE_FILTER_BILINEAR;
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
    return tide_texture_sampled(texture, (tide_texture_sampling(texture) & ~1u) | (filter == TIDE_FILTER_POINT ? 1u : 0u));
}

static inline tide_texture_ref tide_texture_wrapped(const tide_texture_ref texture, const int32_t wrap)
{
    return tide_texture_sampled(texture, (tide_texture_sampling(texture) & 3u) | (uint32_t)tide_wrap_of(wrap) << 2);
}

// What texture `texture` of a shader's adds to its run's `sampling` when it's
// read as its reference says: its filter's bit, or its wrap's.
static inline uint32_t tide_sampling_filter_of(const uint32_t texture, const tide_texture_ref from)
{
    return tide_texture_filter(from) != TIDE_FILTER_POINT ? 1u << texture : 0u;
}

static inline uint32_t tide_sampling_wrap_of(const uint32_t texture, const tide_texture_ref from)
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
// it ends: they're sampled like any target's. Its depth isn't a shader's to
// read (a draw that reads tide_render_target_depth of one is passed by), so a
// target of depth alone has one sample, having said so. Another number is the
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
    return (tide_texture_ref){
        .target = {.id = resolved.id && resolved.id != TIDE_RENDER_WINDOW ? resolved.id : TIDE_RENDER_INVALID,
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
        .target = {.id = resolved.id && resolved.id != TIDE_RENDER_WINDOW ? tide_render_target_whole(resolved.id)
                                                                          : TIDE_RENDER_INVALID,
                   .size = resolved.size}};
}

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
} tide_draw_texture;

// TIDE_PIXELS_TARGET is one of a render target's color images, the one its id
// says (tide_render_target_image), and TIDE_PIXELS_TARGET_DEPTH its depth,
// which only a shader that reads that texture as depth is given
// (tide_shader.texture_reads): neither has pixels here.
// TIDE_PIXELS_PACK is an image of the game's pack (tide/pack.h), whose `id` is its version: what's in it,
// the file's hash.
enum { TIDE_PIXELS_MEMORY, TIDE_PIXELS_GRID, TIDE_PIXELS_TARGET, TIDE_PIXELS_TARGET_DEPTH, TIDE_PIXELS_PACK };

// What a list's texture holds for each pixel: red, green, blue and alpha, a
// byte each, or one float of 16 bits (IEEE 754's half), which a shader reads
// as red, with green and blue 0 and alpha 1: a Grid2<float>'s cells.
enum { TIDE_TEXELS_BYTES, TIDE_TEXELS_HALF };

static inline uint32_t tide_texels_bytes(const uint32_t format)
{
    return format == TIDE_TEXELS_HALF ? 2u : 4u;
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

// The settings that say where a destination's commands land, what its 3D
// meshes do with its depth and which side of their triangles draws, and what
// its draws do with its stencil, which each destination has its own of (see
// Settings, below). A zeroed one is a frame's first: the first cameras, no
// clip, no transform, depth read and written, no triangle drawn from the
// back, and no stencil test.
typedef struct tide_draw_place {
    // What the commands to come are drawn through: 1 + its place in tide_draw_list.cameras and
    // cameras_3d, or 0 for the camera each frame starts with...
    uint32_t camera_2d, camera_3d;
    uint32_t clip;  // ...and 1 + its place in tide_draw_list.clips, or 0 for none
    uint32_t depth; // tide_depth: what the 3D meshes to come do with depth
    uint32_t cull;  // tide_cull: which side of their triangles isn't drawn
    uint32_t stencil; // A tide_stencil, packed: what the commands to come do with stencil
    uint32_t blend;   // tide_blend: how the commands to come go over what's there
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
enum { TIDE_SHADER_ATTRIBUTES = 16, TIDE_SHADER_TEXTURES = 8 };

// How a shader reads one of its textures. A color is read as it is, from
// pixels or a target's color. A target's depth is read as a number, its
// nearest pixel's: or compared with a number of the shader's, which gives how
// much of the depth stored around there isn't nearer than it, 0 to 1, as the
// GPU filters it.
typedef enum tide_shader_read {
    TIDE_SHADER_READ_COLOR,
    TIDE_SHADER_READ_DEPTH,
    TIDE_SHADER_READ_COMPARE,
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
// 2 + 2i; it's NULL off the web.
typedef struct tide_shader {
    const char *name; // For messages: "Wave and Shade on BoxVertex"
    const char *glsl_vertex, *glsl_fragment;
    const char *wgsl;
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
    uint32_t sampling; // How each texture is sampled: blended or the nearest, and its wrap (see TIDE_SAMPLING_WRAP)
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
    uint32_t *indices; // ...and their triangles, each index a place in `vertices`
    uint32_t index_count;
    uint32_t index_capacity;
    tide_draw_texture *textures; // What the meshes draw with, kept from frame to frame while they do
    uint32_t texture_count;
    uint32_t texture_capacity;
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
} tide_draw_list;

// Empties the list for a new frame. It keeps its memory for the next, and the
// textures the frame drew with.
void tide_draw_reset(tide_draw_list *d);

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

// Fills the whole screen, or what the clip leaves of it, with `color` as it
// is: no blend, tint or transform changes a clear.
void tide_draw_clear(tide_draw_list *d, tide_color color);

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

void tide_draw_circle(tide_draw_list *d, tide_float2 center, float radius, tide_color color);
void tide_draw_wire_circle(tide_draw_list *d, tide_float2 center, float radius, tide_color color);
void tide_draw_rect(tide_draw_list *d, tide_float2 center, tide_float2 size, tide_color color);
void tide_draw_wire_rect(tide_draw_list *d, tide_float2 center, tide_float2 size, tide_color color);
void tide_draw_line(tide_draw_list *d, tide_float2 from, tide_float2 to, tide_color color);

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

// A rect whose corners are round, by `radius`.
void tide_draw_round_rect(tide_draw_list *d, tide_float2 center, tide_float2 size, float radius, tide_color color);
void tide_draw_round_rect_rotated(tide_draw_list *d, tide_float2 center, tide_float2 size, float radius, float rotation,
                                  tide_color color);
void tide_draw_wire_round_rect(tide_draw_list *d, tide_float2 center, tide_float2 size, float radius, float width,
                               tide_color color);

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
void tide_draw_mesh(tide_draw_list *d, const tide_vertex *vertices, uint32_t vertex_count, const uint32_t *indices,
                    uint32_t index_count, const tide_texture *texture, tide_filter filter);
// The same, with what uvs past 0 and 1 read: `wrap`.
void tide_draw_mesh_wrapped(tide_draw_list *d, const tide_vertex *vertices, uint32_t vertex_count,
                            const uint32_t *indices, uint32_t index_count, const tide_texture *texture,
                            tide_filter filter, tide_wrap wrap);

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
void tide_draw_mesh_3d(tide_draw_list *d, const tide_mesh *mesh, tide_float4x4 transform, const tide_texture *texture,
                       tide_filter filter);
// The same, with what uvs past 0 and 1 read: `wrap`.
void tide_draw_mesh_3d_wrapped(tide_draw_list *d, const tide_mesh *mesh, tide_float4x4 transform,
                               const tide_texture *texture, tide_filter filter, tide_wrap wrap);

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
                      const void *uniforms, const uint32_t *textures, uint32_t sampling);

// The same for `count` instances, tide_shader_instance_size bytes each,
// copied in their order into one run. A shader with no instance bytes may
// take NULL with a positive count. A count of zero records nothing.
void tide_draw_shaded_instances(tide_draw_list *d, const tide_shader *shader, uint32_t mesh, const void *instances,
                                uint32_t count, const void *uniforms, const uint32_t *textures, uint32_t sampling);

// Runs a frag over the entire selected destination. The fixed Vertex mesh has
// clip-space positions, top-left uvs, and white color. The camera is identity,
// clipping is disabled for this draw alone, and none of the settings apply:
// its output goes over the destination's color as `blend` says
// (TIDE_BLEND_REPLACE: in its place), without changing its depth. Texture
// bindings and packed shader data are the same as tide_draw_shaded's.
// Sampling the destination is refused.
void tide_draw_blit(tide_draw_list *d, const tide_shader *shader, tide_blend blend, const void *instance,
                   const void *uniforms, const uint32_t *textures, uint32_t sampling);
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
