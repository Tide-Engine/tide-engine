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
// Each has one, which the window says (tide_window_gpu): Vulkan 1.3 on
// desktop and Android (gpu_vk.c) and, on the web, WebGPU (web/gpu_web.c, in
// tide.js); inside another program, a recorder (gpu_record.c). Every one
// computes: a machine without either has no window to draw in. Clip space is
// OpenGL's (depth from -1 to 1, y up), pixels are counted from a target's top
// left, and textures' and targets' rows go from the top.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "tide/texture_blocks.h"

typedef uint32_t tide_gpu_id; // A buffer, texture, pipeline or target; 0 is none, and the window as a target

// TIDE_GPU_STORAGE is a buffer shaders read and compute shaders write (WGSL's
// var<storage>), which is also usable as a draw's vertices or indices, as
// indirect arguments (draw_indirect, dispatch_indirect), and as either side of
// buffer_copy and buffer_read. It starts zeroed where no data is given.
typedef enum tide_gpu_buffer_kind { TIDE_GPU_VERTICES, TIDE_GPU_INDICES, TIDE_GPU_STORAGE } tide_gpu_buffer_kind;

// A texture's pixels: 4 bytes each (red, green, blue, alpha), 1 (red), or one
// float of 16 bits (red: any number a half holds). One of a single channel
// reads as that for red, 0 for green and blue and 1 for alpha. A float of 16
// bits is what every backend samples blended between pixels with nothing
// asked of the device, where one of 32 needs a feature a device may lack.
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
// hold any number. Every backend draws into, blends and samples both.
typedef enum tide_gpu_color { TIDE_GPU_COLOR_NONE, TIDE_GPU_COLOR_BYTES, TIDE_GPU_COLOR_FLOAT } tide_gpu_color;

enum { TIDE_GPU_MAX_COLORS = 4 }; // The most color images a target has

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
// time, by its place: WGSL's `texture_2d<i32>` with textureLoad, whose sampler
// binding nothing names. TIDE_GPU_READ_SAMPLES reads a multisampled target's
// depth as its samples are (depth_now): `texture_depth_multisampled_2d`, each
// sample loaded; only the renderer's own kernels read one.
typedef enum tide_gpu_read {
    TIDE_GPU_READ_COLOR, TIDE_GPU_READ_DEPTH, TIDE_GPU_READ_COMPARE, TIDE_GPU_READ_CUBE, TIDE_GPU_READ_DATA,
    TIDE_GPU_READ_SAMPLES
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

enum { TIDE_GPU_MAX_SLOTS = 3, TIDE_GPU_MAX_ATTRIBUTES = 16, TIDE_GPU_MAX_TEXTURES = 12 };

// The most storage buffers a pipeline binds. Storage buffer i is at binding
// TIDE_GPU_STORAGE_BINDING + i of group 0, after the textures' bindings, so a
// pipeline's whole layout is 29 descriptors at most, within Vulkan's least
// maxPushDescriptors (32). 4 is Vulkan's least maxPerStageDescriptorStorageBuffers
// and within WebGPU's default 8 a stage: every device binds them. The renderer's
// pipelines bind at most 3 (a game's GPU lists are ranges of two pools,
// tide/draw.h's TIDE_STORAGE_WORDS and TIDE_STORAGE_INTS, and a kernel's run).
enum { TIDE_GPU_MAX_STORAGE = 4, TIDE_GPU_STORAGE_BINDING = 1 + 2 * TIDE_GPU_MAX_TEXTURES };

// The most a pipeline's uniform block takes, in bytes.
#define TIDE_GPU_MAX_UNIFORMS 4096u

// What a device's indirect draws can do (tide_gpu.indirect). TIDE_GPU_INDIRECT_COUNT: draw_indexed_indirect_count
// is one call whose count the GPU reads (Vulkan's vkCmdDrawIndexedIndirectCount, WebGPU's multi-draw-indirect),
// rather than a draw a record. TIDE_GPU_INDIRECT_FIRST_INSTANCE: its records' first instances are taken as they are
// (Vulkan's drawIndirectFirstInstance, WebGPU's indirect-first-instance).
enum { TIDE_GPU_INDIRECT_COUNT = 1, TIDE_GPU_INDIRECT_FIRST_INSTANCE = 2 };

typedef struct tide_gpu_pipeline_desc {
    const char *name; // For what the backend says when its shaders are wrong
    // Its shaders: WGSL, both stages in one, `vertex` and `fragment`, their
    // attributes at the locations below, their uniforms at binding 0 of group
    // 0, and texture i and its sampler at bindings 1 + 2i and 2 + 2i. Either
    // stage may sample. A texture read as depth is a `texture_depth_2d` with a
    // `sampler`; one that's compared, a `texture_depth_2d` with a
    // `sampler_comparison`. A cube is a `texture_cube<f32>` with a `sampler`.
    const char *wgsl;
    // Vulkan's: the WGSL as SPIR-V (tidec translates it, and the renderer's
    // own at its build), whose entry points and bindings are the WGSL's.
    const uint32_t *spirv;
    uint32_t spirv_words;
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
    // images past them are left as they were: the `@location(i)`s of what
    // `fragment` returns.
    uint32_t outputs;
    // Alpha to coverage: the alpha of its first output is the share of each
    // pixel's samples it draws into, depth and stencil too, and what it
    // leaves of them is as it was. Only for targets of colors and samples
    // (`into`); the renderer gives none for others.
    bool alpha_to_coverage;
    // A compute pipeline: its WGSL's entry point is `compute` (@compute, with
    // its own @workgroup_size), as is its SPIR-V's, and everything above
    // about vertices, blending, depth, stencil, targets and outputs isn't
    // read. It's used inside compute_begin/compute_end, never in a pass.
    bool compute;
    // The storage buffers it binds (storage), at TIDE_GPU_STORAGE_BINDING + i,
    // and whether it writes each: WGSL's var<storage, read_write>, where the
    // others are var<storage, read>. Only a compute pipeline writes one.
    uint32_t storage_count;
    bool storage_writes[TIDE_GPU_MAX_STORAGE];
    // One the renderer does without until it's ready (the GPU culling's
    // kernels and occlusion's pyramid, whose frames are the CPU's otherwise,
    // the same pixels): a backend that makes pipelines in the background
    // makes this one so whatever `background` says, so it's never used before
    // the backend knows it could make it (pipeline_failed).
    bool optional;
} tide_gpu_pipeline_desc;

typedef struct tide_gpu {
    const char *name; // What draws: "WebGPU", "Vulkan"...

    // Which device there is to draw with: 0 while there's none, and another
    // number once one was lost and another found (WebGPU's can be: a driver
    // that starts over). Everything made on the last one went with it, and
    // nothing below is called while there's none.
    uint32_t (*epoch)(void);

    // Optional, for a backend that compiles pipelines itself (Vulkan): the
    // file it keeps them in across runs, so a game's next run makes them from
    // what this one compiled. Read now, as far as it's whole and for this
    // device, and written as the backend closes. NULL for none.
    void (*pipeline_cache)(const char *file);

    // Optional: the bytes of memory the device has of its own (a discrete
    // GPU's), which the renderer keeps a share of for images not being drawn
    // (texture_residency.h). 0 where that's unknown or the memory is the
    // system's (an integrated GPU, a phone's); NULL for a backend that can't say.
    uint64_t (*device_memory)(void);

    // Optional timing of a game's drawing, outside passes. Read returns a
    // newly completed sample in milliseconds, or -1 for none (including an
    // unsupported device). It never waits for the GPU. Reset discards pending
    // samples and releases the timing resources when the drawer closes.
    void (*time_begin)(void);
    void (*time_end)(void);
    double (*time_read)(void);
    void (*time_reset)(void);

    // Optional independent pass queries. Begin, before a render pass, reserves
    // a token only on success; end is after it. Read never waits and returns
    // the original token, including when completions arrive out of order.
    bool (*pass_time_begin)(uint32_t token);
    void (*pass_time_end)(void);
    double (*pass_time_read)(uint32_t *token);
    void (*pass_time_reset)(void);

    // A buffer of `size` bytes, holding `data` if it's given. Sizes, and what's
    // written, are multiples of 4 bytes.
    tide_gpu_id (*buffer)(tide_gpu_buffer_kind kind, size_t size, const void *data);
    // Optional: the most bytes a storage buffer that a pipeline binds whole can
    // be on this device (Vulkan's maxStorageBufferRange, WebGPU's
    // maxStorageBufferBindingSize, and either's most buffer), a multiple of 16
    // and at least 128 MiB, every device's least. NULL for 128 MiB.
    uint64_t (*storage_most)(void);
    void (*buffer_write)(tide_gpu_id buffer, size_t offset, const void *data, size_t size);
    void (*buffer_free)(tide_gpu_id buffer);

    // A texture. Writing takes all of it, or the `width` by `height` pixels of it from (x, y), inside it:
    // `pixels` is all of the texture's either way, its rows one after the other, of which a part's are read
    // where they are. How it's read is each draw's to say (sample).
    tide_gpu_id (*texture)(tide_gpu_format format, int width, int height, const void *pixels);
    // Complete immutable BC1/BC3 or authored RGBA8 mip chain, or 0 when unsupported. No upload
    // or allocation on unsupported devices; the renderer then uses RGBA.
    tide_gpu_id (*texture_blocks)(int width, int height, tide_texture_blocks blocks);
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
    // Optional, for a backend that can make pipelines in the background
    // (WebGPU's): while
    // `background` is on, `pipeline` begins one and returns at once, and
    // `pipeline_ready` says once it's made. Till then nothing is drawn with
    // it: the renderer leaves its steps out (tide_platform_background_pipelines).
    // Off, as a backend starts, `pipeline` makes one before it returns. NULL
    // for a backend that only makes them so.
    void (*background)(bool on);
    bool (*pipeline_ready)(tide_gpu_id pipeline);
    // Optional: true once the backend knows a pipeline can't be made (WebGPU's
    // browser refused it: its driver's compiler, as Dawn's FXC on Direct3D 11
    // may). The renderer never uses one (pipeline_usable) and does without it
    // where it can. The backend says why, once. WebGPU knows it a moment after
    // `pipeline` for one made at once, so frames before may still use it;
    // before it's ready for one made in the background. NULL for a backend
    // whose `pipeline` makes one or stops the program.
    bool (*pipeline_failed)(tide_gpu_id pipeline);

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
    // read back. Its depth is no texture until resolve_depth is first asked
    // for it (target_depth is 0 until then): from there it's one depth a
    // pixel, a sample of each pixel's, as it was when resolve_depth was last
    // called, with 4 bytes a pixel more held. It takes that many times the
    // memory, and its color textures beside.
    tide_gpu_id (*target)(tide_gpu_target_format format, int width, int height);
    int (*target_limit)(void); // The most pixels a side one has, on this device
    // How many samples a target that holds `format`, whose samples are what's
    // asked for, has on this device: no more than asked but where the device
    // has no such count and has a higher one (WebGPU has 1 and 4: 2 is 4),
    // and 1 where it has none. What a format given to `target`, and a
    // pipeline's `into`, say.
    uint32_t (*target_samples)(tide_gpu_target_format format);
    tide_gpu_id (*target_texture)(tide_gpu_id target, uint32_t color); // 0 for an image it hasn't got
    tide_gpu_id (*target_depth)(tide_gpu_id target);
    // Outside a pass: a multisampled target's depth texture is made, the
    // first time, and is what its samples hold now. Nothing, for a target
    // that isn't multisampled, whose depth is always its texture.
    void (*resolve_depth)(tide_gpu_id target);
    // Optional. Outside a pass: a texture of what `target`'s depth (the window's for 0, as this frame's passes left
    // it) holds now, and in `samples` how many it has a pixel. With 1, it's read as depth (TIDE_GPU_READ_DEPTH):
    // a target of one sample's own depth texture, or for the window, or a multisampled target on a device that
    // can't read its samples, a texture of the backend's own filled at each call, each pixel the farthest of its
    // samples. With more, it's the target's samples themselves (TIDE_GPU_READ_SAMPLES), which a kernel loads one by
    // one; until the target's next pass, resolve_depth or depth_now. Only compute reads it, before the target is
    // drawn in again. What occlusion culling measures the depth drawn so far with (notes/drawing.md, Occlusion
    // culling from a depth pyramid). 0 where there's none to give (a window nothing drew in yet); NULL where there
    // never is.
    tide_gpu_id (*depth_now)(tide_gpu_id target, uint32_t *samples);
    void (*target_free)(tide_gpu_id target);
    // Its first color image, read back: 4 bytes a pixel, rows from the top.
    // Zeros for a target whose first isn't 4 bytes a pixel, or that has none:
    // a pass that draws a float image into bytes reads it. The browser's
    // synchronous canvas readback reports opaque alpha; the sampleable color
    // texture itself retains its original alpha.
    void (*target_read)(tide_gpu_id target, uint8_t *rgba);
    // Optional: a part of it read back as target_read reads the whole, `width`
    // by `height` pixels from (x, y), counted from its top left, inside it,
    // into `width` * `height` * 4 bytes. What reading a few pixels costs is
    // then theirs, not the whole target's. NULL where a backend reads only
    // whole targets (WebGPU's).
    void (*target_read_part)(tide_gpu_id target, int x, int y, int width, int height, uint8_t *rgba);

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

    // Compute and what it gives draws, which every backend has: compute is
    // guaranteed wherever Tide draws.
    //
    // Storage buffer `slot` (TIDE_GPU_STORAGE_BINDING + slot), all of it, for
    // the draws or dispatches after it, set as `sample` is, after `use`.
    void (*storage)(uint32_t slot, tide_gpu_id buffer);
    // A compute pass, outside passes: `use` a compute pipeline, its uniforms,
    // samples and storage, then dispatches. Everything recorded before the
    // pass (draws, dispatches, writes, copies) is done before it reads or
    // writes anything, and everything after sees what it wrote: a backend
    // puts the barriers that takes, as Vulkan needs, where WebGPU has them.
    void (*compute_begin)(void);
    void (*compute_end)(void);
    // That many workgroups along each axis, each of the pipeline's
    // @workgroup_size invocations. A count of 0 runs none.
    void (*dispatch)(uint32_t x, uint32_t y, uint32_t z);
    // ...or as many as three uint32s at `offset` of a storage buffer say, as
    // the GPU has them then (a compute pass before wrote them).
    void (*dispatch_indirect)(tide_gpu_id buffer, size_t offset);
    // In a pass: draw or draw_indexed, with their numbers read by the GPU at
    // `offset` of a storage buffer (a multiple of 4): four uint32s (vertex
    // count, instances, first vertex, first instance) or five (index count,
    // instances, first index, base vertex, first instance). The first
    // instance is always 0: WebGPU takes no other without a feature.
    void (*draw_indirect)(tide_gpu_id buffer, size_t offset);
    void (*draw_indexed_indirect)(tide_gpu_id buffer, size_t offset);
    // In a pass: draw_indexed_indirect of each of the records from `offset` of
    // `buffer`, five uint32s each, 20 bytes apart, in order: as many as the
    // uint32 at `count_offset` of the storage buffer `counts` says (a compute
    // pass before wrote it), and never more than `most`. The records from that
    // count on up to `most` must draw nothing (0 instances): a backend without
    // TIDE_GPU_INDIRECT_COUNT (indirect, below) draws every one of `most`, a
    // call each. Each record's first index and base vertex are taken as they
    // are, and its first instance where indirect has
    // TIDE_GPU_INDIRECT_FIRST_INSTANCE; elsewhere it's 0, as above. A shader's
    // instance index counts from the first instance.
    void (*draw_indexed_indirect_count)(tide_gpu_id buffer, size_t offset, uint32_t most, tide_gpu_id counts,
                                        size_t count_offset);
    // What indirect draws can do on this device, TIDE_GPU_INDIRECT_* bits.
    uint32_t (*indirect)(void);
    // Outside passes: `size` bytes copied from a storage or index buffer to a
    // storage buffer (all multiples of 4), in order with the passes and
    // compute passes around it.
    void (*buffer_copy)(tide_gpu_id from, size_t from_offset, tide_gpu_id to, size_t to_offset, size_t size);
    // Outside passes: begins reading `size` bytes at `offset` of a storage
    // buffer back (multiples of 4), as they are once everything recorded
    // before is done. A token, never 0, or 0 where none could begin.
    // buffer_read_done copies them into `data` (`size` bytes) and returns 1
    // once they're here, 0 while they aren't, and -1 for a read the device
    // lost; the token is let go once it returns anything but 0. It never
    // waits. buffer_read_wait waits until every read begun is here (for runs
    // whose frames must repeat exactly); NULL on the web, which can't wait.
    uint32_t (*buffer_read)(tide_gpu_id buffer, size_t offset, size_t size);
    int (*buffer_read_done)(uint32_t token, void *data);
    void (*buffer_read_wait)(void);
} tide_gpu;

// What makes SPIR-V of a pipeline's WGSL where it has none, for Vulkan
// (gpu_vk.c): a test's shaders written by hand in C have WGSL alone, and
// the test hands tidec's translator over (compiler/src/spirv.h). NULL in a
// game, whose shaders tidec translated as it built them.
extern uint32_t *(*tide_gpu_spirv_of)(const char *wgsl, size_t *count, char *error, size_t error_size);

// Inside another program (tide/embed.h): each call written down as
// tide_embed_render's commands, for that program to replay (gpu_record.c).
// Begin starts a frame's, which end gives, valid until the next begin. Reset
// starts over, as a device that was lost does: everything made is gone, so
// the renderer makes it again (tide_embed_render_reset). Close frees what it
// held.
extern const tide_gpu tide_gpu_record;
// A GPU that draws nothing, for a test's page that checks nothing drawn (gpu_none.c)
extern const tide_gpu tide_gpu_none;
void tide_gpu_record_begin(void);
const uint8_t *tide_gpu_record_end(uint32_t *size);
void tide_gpu_record_reset(void);
void tide_gpu_record_close(void);
// What the other program read for a buffer_read's token (tide_embed_render_read): `size` bytes, or NULL for a read
// that failed, which buffer_read_done then gives. False for a token it isn't waiting for, or another size.
bool tide_gpu_record_read(uint32_t token, const void *data, uint32_t size);
// A test's: how many pipelines of games' shaders the renderer has (platform.c),
// so a test sees whether a draw made one.
uint32_t tide_platform_shader_pipelines(void);
#ifdef __wasm__
extern const tide_gpu tide_gpu_webgpu; // web/gpu_web.c
#endif
