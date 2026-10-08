#pragma once

// The GPU as the renderer (platform.c) uses it, whichever API is under it:
// buffers, textures, pipelines and passes. It's shaped by WebGPU, the
// strictest of them, so the others fit under it:
//
// - A pipeline is made once, with its shaders, the vertices it reads, how it
//   blends and tests depth and stencil, and which side of a triangle it
//   draws. Nothing about it changes afterwards. The number its stencil test
//   compares with isn't its own: it's the pass's, set before a draw.
// - A pipeline's uniforms are one block of bytes, set before a draw.
// - Drawing happens in a pass on a target, which starts cleared or as it was
//   left. There's no clear inside a pass: the renderer draws one.
//
// Each has one, which the window says (tide_window_gpu): OpenGL (gpu_gl.c, on
// gl.h) and, on the web, WebGPU (web/gpu_web.c, in tide.js). Vulkan is to come.
// Clip space is OpenGL's (depth from -1 to 1, y up), pixels are counted from a
// target's top left, and textures' and targets' rows go from the top.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint32_t tide_gpu_id; // A buffer, texture, pipeline or target; 0 is none, and the window as a target

typedef enum tide_gpu_buffer_kind { TIDE_GPU_VERTICES, TIDE_GPU_INDICES } tide_gpu_buffer_kind;

// A texture's pixels: 4 bytes each (red, green, blue, alpha), 1 (red), or one
// float of 16 bits (red: any number a half holds). One of a single channel
// reads as that for red, 0 for green and blue and 1 for alpha. A float of 16
// bits is what every backend samples blended between pixels with nothing
// asked of the device (OpenGL 3.3, OpenGL ES 3, WebGL 2 and WebGPU alike),
// where one of 32 needs an extension or a feature a device may lack.
// TIDE_GPU_RGBA32I is four ints of 32 bits, which a pipeline reads as they
// are, each pixel by its place (TIDE_GPU_READ_DATA) and never blended: numbers
// of any kind, a float's as its bits. Every backend has it with nothing asked.
typedef enum tide_gpu_format { TIDE_GPU_RGBA8, TIDE_GPU_R8, TIDE_GPU_R16F, TIDE_GPU_RGBA32I } tide_gpu_format;

// The bytes a pixel of a texture takes, as texture and texture_write are given them.
static inline int tide_gpu_format_bytes(const tide_gpu_format format)
{
    return format == TIDE_GPU_RGBA8 ? 4 : format == TIDE_GPU_R16F ? 2 : format == TIDE_GPU_RGBA32I ? 16 : 1;
}

// What one of a target's color images holds for each pixel: 4 bytes (red,
// green, blue, alpha, each from 0 to 1), or four floats of 16 bits, which
// hold any number. A device that can't draw into floats (float_targets) has 4
// bytes for those too.
typedef enum tide_gpu_color { TIDE_GPU_COLOR_NONE, TIDE_GPU_COLOR_BYTES, TIDE_GPU_COLOR_FLOAT } tide_gpu_color;

enum { TIDE_GPU_MAX_COLORS = 4 }; // The most color images a target has: what OpenGL ES 3 and WebGL 2 promise

// What a target holds beside its depth, which every target has: its color
// images in order, 4 bits each (tide_gpu_color), and 0 past the last. So 0 is
// a target of depth alone, and 1 a target of one image of 4 bytes a pixel,
// which is also what the window is. Past its colors' 16 bits, how many
// samples each of its pixels is drawn with (tide_gpu_target_samples): 0, which
// is 1, for what isn't multisampled, as the window isn't. It's one number so
// that whatever is made for a kind of target (a pipeline's `into`) is made
// for its samples too, which a pipeline is on WebGPU.
typedef uint32_t tide_gpu_target_format;
#define TIDE_GPU_TARGET_DEPTH 0u
#define TIDE_GPU_TARGET_COLOR 1u
#define TIDE_GPU_TARGET_SAMPLES_SHIFT 16u
enum { TIDE_GPU_MAX_SAMPLES = 8 }; // The most any device is asked for

// How many samples a pixel of such a target is drawn with: 1, 2, 4 or 8.
static inline uint32_t tide_gpu_target_samples(const tide_gpu_target_format format)
{
    const uint32_t samples = format >> TIDE_GPU_TARGET_SAMPLES_SHIFT & 15u;
    return samples > 1u ? samples : 1u;
}

// ...and the format with `samples` of them (1 is none said, so two formats
// of as many samples are the same number).
static inline tide_gpu_target_format tide_gpu_target_sampled(const tide_gpu_target_format format, const uint32_t samples)
{
    return (format & 0xFFFFu) | (samples > 1u ? (samples & 15u) << TIDE_GPU_TARGET_SAMPLES_SHIFT : 0u);
}

// A target's color image `color`, from 0: TIDE_GPU_COLOR_NONE for one it hasn't got.
static inline tide_gpu_color tide_gpu_target_color(const tide_gpu_target_format format, const uint32_t color)
{
    const uint32_t held = color < TIDE_GPU_MAX_COLORS ? format >> (4u * color) & 15u : 0u;
    return held == TIDE_GPU_COLOR_FLOAT ? TIDE_GPU_COLOR_FLOAT : held ? TIDE_GPU_COLOR_BYTES : TIDE_GPU_COLOR_NONE;
}

// How many color images it has.
static inline uint32_t tide_gpu_target_colors(const tide_gpu_target_format format)
{
    uint32_t count = 0;
    while (count < TIDE_GPU_MAX_COLORS && tide_gpu_target_color(format, count) != TIDE_GPU_COLOR_NONE) count++;
    return count;
}

// ...and the format with one more, `color`, after them.
static inline tide_gpu_target_format tide_gpu_target_with(const tide_gpu_target_format format, const tide_gpu_color color)
{
    const uint32_t count = tide_gpu_target_colors(format);
    return count < TIDE_GPU_MAX_COLORS ? format | (uint32_t)color << (4u * count) : format;
}

// How a pipeline reads one of its textures. A color is read as it is. A
// target's depth is read as a number, 0 at the near plane to 1 at the far
// limit, the nearest pixel's; or compared with a number of the shader's,
// which gives how much of the depth stored around there isn't nearer than it,
// from 0 to 1, as the GPU filters it.
// A cube (tide_gpu.cube) is read by a direction, as a color.
// TIDE_GPU_READ_DATA reads a texture of ints (TIDE_GPU_RGBA32I) a pixel at a
// time, by its place: GLSL's `isampler2D` with texelFetch, and WGSL's
// `texture_2d<i32>` with textureLoad, whose sampler binding nothing names.
typedef enum tide_gpu_read {
    TIDE_GPU_READ_COLOR, TIDE_GPU_READ_DEPTH, TIDE_GPU_READ_COMPARE, TIDE_GPU_READ_CUBE, TIDE_GPU_READ_DATA
} tide_gpu_read;

// A cube's six faces, in the order it has them: the one each axis points at,
// +x, -x, +y, -y, +z, -z.
enum { TIDE_GPU_CUBE_FACES = 6 };

// What a vertex attribute is in its buffer: floats, 4 bytes read as 0 to 1, or
// ints of 32 bits, which a shader reads as ints.
typedef enum tide_gpu_attribute_format {
    TIDE_GPU_FLOAT,
    TIDE_GPU_FLOAT2,
    TIDE_GPU_FLOAT3,
    TIDE_GPU_FLOAT4,
    TIDE_GPU_BYTES4,
    TIDE_GPU_INT,
    TIDE_GPU_INT2,
    TIDE_GPU_INT3,
    TIDE_GPU_INT4,
} tide_gpu_attribute_format;

// How a pipeline's pixels go over what's there. A pixel's color is as its
// fragment shader wrote it, already times its alpha (premultiplied), and its
// alpha how much of what's there it covers. A texture's pixels are the same
// way, so that what's blended between two of them is right whatever color a
// clear one has, and a target holds what was blended into it that way too.
// TIDE_GPU_BLEND_MULTIPLY's shader writes a color that's gone to white where
// its alpha is 0 (the renderer's shaders do, by a uniform, as one shader is
// drawn through pipelines that blend each way).
typedef enum tide_gpu_blend {
    TIDE_GPU_BLEND_ALPHA,    // Over what's there: its color, and what's there less its alpha
    TIDE_GPU_BLEND_ADD,      // Added to what's there; the alpha there grows by its own, of what was left to 1
    TIDE_GPU_BLEND_MULTIPLY, // What's there times its color; the alpha there stays
    TIDE_GPU_BLEND_REPLACE,  // In place of what's there, alpha too
} tide_gpu_blend;

typedef enum tide_gpu_depth {
    TIDE_GPU_DEPTH_NONE,  // Neither tested nor written
    TIDE_GPU_DEPTH_TEST,  // The nearest shows, and is written
    TIDE_GPU_DEPTH_READ,  // The nearest shows, and nothing is written: what's see-through
    TIDE_GPU_DEPTH_RESET, // Written whatever is there: for clears
} tide_gpu_depth;

// Which side of a triangle a pipeline doesn't draw. The front is the side its
// corners go clockwise on in clip space, with y up: as they're seen, in the
// window and in a target alike, whichever way up a backend keeps its rows.
typedef enum tide_gpu_cull {
    TIDE_GPU_CULL_NONE,  // Both sides draw
    TIDE_GPU_CULL_BACK,  // The triangles that go counterclockwise don't
    TIDE_GPU_CULL_FRONT, // The ones that go clockwise don't
} tide_gpu_cull;

// Stencil: a number from 0 to 255 for each of a target's pixels, beside its
// depth, the window's too. The test a pipeline's draws pass to draw at a
// pixel is the pass's number (tide_gpu.stencil), on the left, against the one
// stored there: tide_stencil_test's numbers. Neither side of a triangle has
// its own, and no bits are masked.
typedef enum tide_gpu_stencil_test {
    TIDE_GPU_STENCIL_ALWAYS, // No test
    TIDE_GPU_STENCIL_NEVER,
    TIDE_GPU_STENCIL_LESS,
    TIDE_GPU_STENCIL_EQUAL,
    TIDE_GPU_STENCIL_LESS_EQUAL,
    TIDE_GPU_STENCIL_GREATER,
    TIDE_GPU_STENCIL_NOT_EQUAL,
    TIDE_GPU_STENCIL_GREATER_EQUAL,
} tide_gpu_stencil_test;

// What becomes of the stored number where a pipeline draws, at the pixels that
// pass its stencil test and its depth test: tide_stencil_op's numbers. It
// stays at the others.
typedef enum tide_gpu_stencil_op {
    TIDE_GPU_STENCIL_KEEP,
    TIDE_GPU_STENCIL_REPLACE,   // The pass's number
    TIDE_GPU_STENCIL_INCREMENT, // One more, 255 at the most
    TIDE_GPU_STENCIL_DECREMENT, // One less, 0 at the least
    TIDE_GPU_STENCIL_ZERO,
    TIDE_GPU_STENCIL_INVERT,    // Each of its bits the other way
} tide_gpu_stencil_op;

// What a texture's uvs past 0 and 1 read, on both axes: tide_wrap's numbers.
typedef enum tide_gpu_wrap {
    TIDE_GPU_WRAP_CLAMP,  // The pixels at its edge
    TIDE_GPU_WRAP_REPEAT, // The texture again
    TIDE_GPU_WRAP_MIRROR, // The texture again, the other way round each time
} tide_gpu_wrap;

// How a texture is read between its pixels: tide_filter's numbers. With
// TIDE_GPU_FILTER_MIPMAPS it's also read between its smaller images
// (tide_gpu.mipmaps), by how much smaller than itself it's drawn; one that
// has none yet is read as TIDE_GPU_FILTER_LINEAR.
typedef enum tide_gpu_filter {
    TIDE_GPU_FILTER_NEAREST, // The nearest pixel
    TIDE_GPU_FILTER_LINEAR,  // Blended with its neighbors
    TIDE_GPU_FILTER_MIPMAPS, // ...and between the two of its images nearest the size it's drawn at
} tide_gpu_filter;

enum { TIDE_GPU_MAX_SLOTS = 3, TIDE_GPU_MAX_ATTRIBUTES = 16, TIDE_GPU_MAX_TEXTURES = 8 };

// The most a pipeline's uniform block takes, in bytes.
#define TIDE_GPU_MAX_UNIFORMS 4096u

typedef struct tide_gpu_pipeline_desc {
    const char *name; // For what the backend says when its shaders are wrong
    // Its shaders, of which a backend takes its own. OpenGL's: a vertex and a
    // fragment shader without their version line, their attributes at the
    // locations below, their uniforms in a std140 block named `uniforms`, and
    // their textures `pixels0`, `pixels1`... WebGPU's: both in one, `vertex`
    // and `fragment`, their uniforms at binding 0 of group 0, and texture i
    // and its sampler at bindings 1 + 2i and 2 + 2i. Either stage may sample.
    // A texture read as depth is a `sampler2D` whose red is the depth, and a
    // `texture_depth_2d` with a `sampler`; one that's compared is a
    // `sampler2DShadow`, and a `texture_depth_2d` with a `sampler_comparison`.
    // A cube is a `samplerCube`, and a `texture_cube<f32>` with a `sampler`.
    const char *glsl_vertex, *glsl_fragment;
    const char *wgsl;
    // The vertex buffers it reads, by slot: how far apart their elements are,
    // and whether they're one for each vertex or for each instance.
    struct {
        uint32_t stride;
        bool per_instance;
    } slots[TIDE_GPU_MAX_SLOTS];
    uint32_t slot_count;
    // Its attributes, each at the location that's its place here.
    struct {
        uint32_t slot;
        tide_gpu_attribute_format format;
        uint32_t offset;
    } attributes[TIDE_GPU_MAX_ATTRIBUTES];
    uint32_t attribute_count;
    uint32_t uniform_size;  // Its uniform block's, in bytes: every pipeline has one
    uint32_t texture_count; // The textures it samples
    uint8_t reads[TIDE_GPU_MAX_TEXTURES]; // ...and how it reads each: tide_gpu_read
    tide_gpu_blend blend;
    tide_gpu_depth depth;
    tide_gpu_cull cull;
    tide_gpu_stencil_test stencil_test; // Zeroed: every pixel passes, and nothing is stored
    tide_gpu_stencil_op stencil_op;
    // What the targets it draws into hold: a pipeline is for one kind, and
    // the window is of the kind TIDE_GPU_TARGET_COLOR. One for targets of
    // depth alone writes no color, and its blend isn't read. The kind has
    // the targets' samples in it, as the device gives them (target_samples).
    tide_gpu_target_format into;
    // How many colors its fragment shader writes, at locations 0 on: one, for
    // 0. They go to the target's color images in that order, each blended the
    // way `blend` says. Those the target has no image for go nowhere, and the
    // images past them are left as they were. GLSL's are `out vec4`s, each
    // with its `layout(location = i)` where there are several; WGSL's, the
    // `@location(i)`s of what `fragment` returns.
    uint32_t outputs;
} tide_gpu_pipeline_desc;

typedef struct tide_gpu {
    const char *name; // What draws: "WebGPU", "OpenGL 3.3"...

    // Which device there is to draw with: 0 while there's none, and another
    // number once one was lost and another found (WebGPU's can be: a driver
    // that starts over). Everything made on the last one went with it, and
    // nothing below is called while there's none.
    uint32_t (*epoch)(void);

    // Optional timing of a game's drawing, outside passes. Read returns a
    // newly completed sample in milliseconds, or -1 for none (including an
    // unsupported device). It never waits for the GPU. Reset discards pending
    // samples and releases the timing resources when the drawer closes.
    void (*time_begin)(void);
    void (*time_end)(void);
    double (*time_read)(void);
    void (*time_reset)(void);

    // A buffer of `size` bytes, holding `data` if it's given. Sizes, and what's
    // written, are multiples of 4 bytes.
    tide_gpu_id (*buffer)(tide_gpu_buffer_kind kind, size_t size, const void *data);
    void (*buffer_write)(tide_gpu_id buffer, size_t offset, const void *data, size_t size);
    void (*buffer_free)(tide_gpu_id buffer);

    // A texture. Writing takes all of it, or the `width` by `height` pixels of it from (x, y), inside it:
    // `pixels` is all of the texture's either way, its rows one after the other, of which a part's are read
    // where they are. How it's read is each draw's to say (sample).
    tide_gpu_id (*texture)(tide_gpu_format format, int width, int height, const void *pixels);
    void (*texture_write)(tide_gpu_id texture, const void *pixels);
    void (*texture_write_part)(tide_gpu_id texture, const void *pixels, int x, int y, int width, int height);
    // Its smaller images, each half the last one's size down to one pixel
    // and each pixel the mean of the four it's made of, from its pixels as
    // they are: what TIDE_GPU_FILTER_MIPMAPS reads. Outside a pass. For a
    // texture and for a target's color image alike. They're as they were
    // made until this is called again: writing a texture, or drawing into a
    // target, changes none of them. Where the device can't make them (a
    // float texture where it draws into no floats), the texture has none,
    // and is read as TIDE_GPU_FILTER_LINEAR.
    void (*mipmaps)(tide_gpu_id texture);
    void (*texture_free)(tide_gpu_id texture);

    // A cube: six square images of `size` pixels a side, each as `color` says
    // (4 bytes a pixel, or four floats of 16 bits, with a device's bytes
    // where it draws into no floats), which a pipeline reads by a direction
    // from its middle (TIDE_GPU_READ_CUBE) and never as a texture of two
    // dimensions. `faces` is their pixels, 4 bytes each, in the order +x, -x,
    // +y, -y, +z, -z, each as it's seen from the cube's middle with its rows
    // from the top: +y is up for the four around, away from +z for the one
    // above and towards it for the one below, and +x is to the right of +z
    // (a left-handed world's, as the cameras here are). NULL for a cube
    // that's drawn into, which starts black. Between two faces it's read
    // across the edge. It's a texture to `sample` (whose wrap isn't read),
    // `mipmaps` and `texture_free`, and to nothing else.
    tide_gpu_id (*cube)(tide_gpu_color color, int size, const void *const *faces);
    // A target whose one color image is the face `face` (0 to 5: +x, -x, +y,
    // -y, +z, -z) of a cube that was made with no pixels, with depth and
    // stencil of its own. What a camera
    // at the cube's middle that looks along the face's axis sees is drawn
    // into it as into any target, rows from the top. Its kind is a target's
    // of one image of the cube's `color`. Its color isn't a texture by itself
    // (target_texture is 0: the cube is what's sampled, never in a pass on
    // one of its faces), its depth is, and target_read reads the face.
    // target_free frees it, and leaves the cube, which is freed after its
    // faces' targets.
    tide_gpu_id (*cube_face)(tide_gpu_id cube, uint32_t face);

    tide_gpu_id (*pipeline)(const tide_gpu_pipeline_desc *desc);
    void (*pipeline_free)(tide_gpu_id pipeline);

    // Pixels of its own to draw into, with depth and stencil: the color
    // images `format` says, in order, rows from the top. Each color image is
    // also a texture (target_texture, by its number from 0), with the same UVs
    // as an uploaded image: (0, 0) is its top left. So is its depth, which a
    // pipeline reads as depth or compares (tide_gpu_read), never as a color.
    // The target owns those textures; only target_free frees them. None can
    // be sampled in a pass that draws into it, or written with texture_write.
    // A target of depth alone (TIDE_GPU_TARGET_DEPTH) has no color: its
    // textures are 0, nothing is read back from it, and the pipelines that
    // draw into it are made for that (tide_gpu_pipeline_desc.into), as those
    // into every other kind of target are.
    //
    // A format with samples (tide_gpu_target_sampled, which has to be what
    // target_samples gives for it) is a multisampled target: each pixel has
    // that many colors, depths and stencils while it's drawn into, kept from
    // pass to pass, and a triangle's edge covers some of them. Its color
    // textures have one color a pixel, what its samples come to, as each
    // pass on it ends (`end` resolves them): those are what's sampled and
    // read back. Its depth is no texture (target_depth is 0): no backend
    // here resolves one. It takes that many times the memory, and its color
    // textures beside.
    tide_gpu_id (*target)(tide_gpu_target_format format, int width, int height);
    int (*target_limit)(void); // The most pixels a side one has, on this device
    // How many samples a target that holds `format`, whose samples are what's
    // asked for, has on this device: no more than asked but where the device
    // has no such count and has a higher one (WebGPU has 1 and 4: 2 is 4),
    // and 1 where it has none. What a format given to `target`, and a
    // pipeline's `into`, say.
    uint32_t (*target_samples)(tide_gpu_target_format format);
    // Whether a color image of floats (TIDE_GPU_COLOR_FLOAT) is one on this
    // device. Where it isn't (an OpenGL ES 3 or a WebGL 2 with no way to draw
    // into floats), such an image is 4 bytes a pixel, having said so once:
    // what's drawn into it is kept from 0 to 1.
    bool (*float_targets)(void);
    tide_gpu_id (*target_texture)(tide_gpu_id target, uint32_t color); // 0 for an image it hasn't got
    tide_gpu_id (*target_depth)(tide_gpu_id target);
    void (*target_free)(tide_gpu_id target);
    // Its first color image, read back: 4 bytes a pixel, rows from the top.
    // Zeros for a target whose first isn't 4 bytes a pixel, or that has none:
    // a pass that draws a float image into bytes reads it. The browser's
    // synchronous canvas readback reports opaque alpha; the sampleable color
    // texture itself retains its original alpha.
    void (*target_read)(tide_gpu_id target, uint8_t *rgba);

    // A pass on `target`, or the window for 0: cleared to black (every color
    // image it has, a float one too), depth too and stencil to 0, or over
    // what's there. One at a time; everything below is inside one.
    void (*begin)(tide_gpu_id target, bool clear);
    void (*end)(void); // A multisampled target's color textures are what it holds from here

    // Only what's inside the rect draws from here on: the whole target, to
    // draw everywhere. It's inside the target.
    void (*scissor)(int x, int y, int width, int height);
    // The pipeline the draws after it use, then what it draws with.
    void (*use)(tide_gpu_id pipeline);
    // The number the stencil tests of the draws after it compare with, and
    // what TIDE_GPU_STENCIL_REPLACE stores, from 0 to 255: the pass's, whatever
    // pipeline is used, and 0 as a pass begins.
    void (*stencil)(uint32_t value);
    void (*uniforms)(const void *data, size_t size);
    // Its texture `slot`, and how it's sampled: between pixels, as `filter`
    // says, and past its edges, as `wrap` says. A
    // texture of any size wraps, and one texture can be in several slots,
    // sampled another way in each. Each of a pipeline's is set before it
    // draws, after the pipeline is. One the pipeline reads as depth is a
    // target's depth, and neither `filter` nor `wrap` is read: its nearest
    // pixel is, the pixels at its edge past them, and where it's compared,
    // the GPU blends what the comparisons give.
    void (*sample)(uint32_t slot, tide_gpu_id texture, tide_gpu_filter filter, tide_gpu_wrap wrap);
    void (*vertices)(uint32_t slot, tide_gpu_id buffer, size_t offset);
    void (*indices)(tide_gpu_id buffer, bool wide); // 32 bits each, or 16
    // Triangles, three vertices (or indices) each, `instances` times over.
    void (*draw)(uint32_t first, uint32_t count, uint32_t instances);
    void (*draw_indexed)(uint32_t count, uint32_t instances);
} tide_gpu;

extern const tide_gpu tide_gpu_gl; // gpu_gl.c
// A test's: has OpenGL make its targets as a device that can't draw into
// floats does, while it's set. The targets there already stay as they are.
extern bool tide_gpu_gl_no_floats;
#ifdef __wasm__
extern const tide_gpu tide_gpu_webgpu; // web/gpu_web.c
#endif
