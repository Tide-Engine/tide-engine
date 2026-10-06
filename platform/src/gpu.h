#pragma once

// The GPU as the renderer (platform.c) uses it, whichever API is under it:
// buffers, textures, pipelines and passes. It's shaped by WebGPU, the
// strictest of them, so the others fit under it:
//
// - A pipeline is made once, with its shaders, the vertices it reads, and how
//   it blends and tests depth. Nothing about it changes afterwards.
// - A pipeline's uniforms are one block of bytes, set before a draw.
// - Drawing happens in a pass on a target, which starts cleared or as it was
//   left. There's no clear inside a pass: the renderer draws one.
//
// Each has one, which the window says (tide_window_gpu): OpenGL (gpu_gl.c, on
// gl.h) and, on the web, WebGPU (web/gpu_web.c, in tide.js). Vulkan is to come.
// Clip space is OpenGL's (depth from -1 to 1), pixels are counted from a
// target's top left, and textures' and targets' rows go from the top.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint32_t tide_gpu_id; // A buffer, texture, pipeline or target; 0 is none, and the window as a target

typedef enum tide_gpu_buffer_kind { TIDE_GPU_VERTICES, TIDE_GPU_INDICES } tide_gpu_buffer_kind;

// A texture's pixels: 4 bytes each (red, green, blue, alpha), or 1 (red).
typedef enum tide_gpu_format { TIDE_GPU_RGBA8, TIDE_GPU_R8 } tide_gpu_format;

// What a target holds beside its depth, which every target has: a color
// image, 4 bytes a pixel, or none.
typedef enum tide_gpu_target_format { TIDE_GPU_TARGET_COLOR, TIDE_GPU_TARGET_DEPTH } tide_gpu_target_format;

// How a pipeline reads one of its textures. A color is read as it is. A
// target's depth is read as a number, 0 at the near plane to 1 at the far
// limit, the nearest pixel's; or compared with a number of the shader's,
// which gives how much of the depth stored around there isn't nearer than it,
// from 0 to 1, as the GPU filters it.
typedef enum tide_gpu_read { TIDE_GPU_READ_COLOR, TIDE_GPU_READ_DEPTH, TIDE_GPU_READ_COMPARE } tide_gpu_read;

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
// fragment shader wrote it, its alpha how much of it there is, but for
// TIDE_GPU_BLEND_MULTIPLY, whose shader writes a color that's gone to white
// where its alpha is 0 (the renderer's shaders do, by a uniform, as one
// shader is drawn through pipelines that blend each way).
typedef enum tide_gpu_blend {
    TIDE_GPU_BLEND_ALPHA,    // Over what's there, by its alpha
    TIDE_GPU_BLEND_ADD,      // Added to what's there, by its alpha; the alpha there grows by it
    TIDE_GPU_BLEND_MULTIPLY, // What's there times its color; the alpha there stays
    TIDE_GPU_BLEND_REPLACE,  // In place of what's there, alpha too
} tide_gpu_blend;

typedef enum tide_gpu_depth {
    TIDE_GPU_DEPTH_NONE,  // Neither tested nor written
    TIDE_GPU_DEPTH_TEST,  // The nearest shows, and is written
    TIDE_GPU_DEPTH_READ,  // The nearest shows, and nothing is written: what's see-through
    TIDE_GPU_DEPTH_RESET, // Written whatever is there: for clears
} tide_gpu_depth;

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
    // What the targets it draws into hold: a pipeline is for one kind. One for
    // targets of depth alone writes no color, and its blend isn't read.
    tide_gpu_target_format into;
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

    // A texture, which doesn't repeat past its edges. Writing takes all of it.
    tide_gpu_id (*texture)(tide_gpu_format format, int width, int height, const void *pixels);
    void (*texture_write)(tide_gpu_id texture, const void *pixels);
    void (*texture_free)(tide_gpu_id texture);

    tide_gpu_id (*pipeline)(const tide_gpu_pipeline_desc *desc);
    void (*pipeline_free)(tide_gpu_id pipeline);

    // Pixels of its own to draw into, with depth, and to read back: 4 bytes
    // each, rows from the top. Its color is also a texture, with the same UVs
    // as an uploaded image: (0, 0) is its top left. So is its depth, which a
    // pipeline reads as depth or compares (tide_gpu_read), never as a color.
    // The target owns both textures; only target_free frees them. Neither can
    // be sampled in a pass that draws into it, or written with texture_write.
    // A target of depth alone (TIDE_GPU_TARGET_DEPTH) has no color: its
    // texture is 0, nothing is read back from it, and the pipelines that draw
    // into it are made for that (tide_gpu_pipeline_desc.into).
    tide_gpu_id (*target)(tide_gpu_target_format format, int width, int height);
    int (*target_limit)(void); // The most pixels a side one has, on this device
    tide_gpu_id (*target_texture)(tide_gpu_id target);
    tide_gpu_id (*target_depth)(tide_gpu_id target);
    void (*target_free)(tide_gpu_id target);
    // The browser's synchronous canvas readback reports opaque alpha; the
    // sampleable color texture itself retains its original alpha.
    void (*target_read)(tide_gpu_id target, uint8_t *rgba);

    // A pass on `target`, or the window for 0: cleared to black, depth too, or
    // over what's there. One at a time; everything below is inside one.
    void (*begin)(tide_gpu_id target, bool clear);
    void (*end)(void);
    // Only what's inside the rect draws from here on: the whole target, to
    // draw everywhere. It's inside the target.
    void (*scissor)(int x, int y, int width, int height);
    // The pipeline the draws after it use, then what it draws with.
    void (*use)(tide_gpu_id pipeline);
    void (*uniforms)(const void *data, size_t size);
    // Its texture `slot`, and how it's sampled between pixels: blended with
    // its neighbors, or the nearest. Each of a pipeline's is set before it
    // draws, after the pipeline is. One the pipeline reads as depth is a
    // target's depth, and `blended` isn't read: its nearest pixel is, and
    // where it's compared, the GPU blends what the comparisons give.
    void (*sample)(uint32_t slot, tide_gpu_id texture, bool blended);
    void (*vertices)(uint32_t slot, tide_gpu_id buffer, size_t offset);
    void (*indices)(tide_gpu_id buffer, bool wide); // 32 bits each, or 16
    // Triangles, three vertices (or indices) each, `instances` times over.
    void (*draw)(uint32_t first, uint32_t count, uint32_t instances);
    void (*draw_indexed)(uint32_t count, uint32_t instances);
} tide_gpu;

extern const tide_gpu tide_gpu_gl; // gpu_gl.c
#ifdef __wasm__
extern const tide_gpu tide_gpu_webgpu; // web/gpu_web.c
#endif
