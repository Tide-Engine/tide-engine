#pragma once

// Drawing written by hand against the GPU API, for the workloads of
// `tide_platform_draw --bench` (platform/tests/draw.c): the same target, the
// same frames, the same shapes in the same places and colors, and the same
// boundaries (record; render, then one pixel read back; total). What an
// expert writes for each: one static mesh, one instance buffer the CPU writes
// straight into, one instanced draw, as little state as the draw needs.
// versus_draw.c runs the workloads and times them; draw_gl.c and draw_vk.c
// are the two APIs.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// As tide_platform_draw's window, in pixels: the shapes fill its top 256 rows.
enum { VD_WIDTH = 256, VD_HEIGHT = 352, VD_COUNT = 65536 };

// The workloads, by what each draws. Their names (vd_names) are
// tide_platform_draw's, so rows of the two compare by name.
typedef enum vd_kind {
    VD_NOTHING,       // What reading a pixel back costs
    VD_RECTS,         // A pixel each, filling it
    VD_CIRCLES,       // 0.6 pixels around each pixel's middle, edges smooth over a pixel
    VD_QUADS,         // A pixel each, a 2 by 2 texture over it, sampled at the nearest of its pixels
    VD_CUBES,         // A unit cube at each of 256 by 256 places on the ground, with depth and back faces culled
    VD_ROUND_RECTS,   // A pixel each, corners 0.3 round, edges smooth over a pixel
    VD_CUBE_MATRICES, // The cubes again, each with its whole transform as Tide sends it (64 bytes, not 12)
    VD_KINDS
} vd_kind;

// An instance of a 2D shape, in window pixels from the top left: its middle
// and half its size (a circle's radius, twice), and its color as RGBA bytes.
// A textured quad is the same, its texture from its top left corner.
typedef struct vd_shape {
    float x, y, half_width, half_height;
    uint32_t rgba;
} vd_shape;

// A round rect: a shape and its corners' radius.
typedef struct vd_round {
    float x, y, half_width, half_height, radius;
    uint32_t rgba;
} vd_round;

// A cube's place in the world.
typedef struct vd_place {
    float x, y, z;
} vd_place;

// What each workload sends a frame: an instance's bytes.
size_t vd_instance_size(vd_kind kind);

// One API's drawing. Every frame of a workload: `instances` gives where its
// instances go (memory the GPU reads), the CPU writes them there, `written`
// says they're there, and `render` draws the frame into the target, cleared
// to black, and reads its pixel (1, 1) back, waiting for it.
typedef struct vd_api {
    const char *name; // As Tide's renderer is named: "OpenGL 3.3", "Vulkan"
    const char *api;  // What it's written against
    // `camera` is the cubes' world to clip space, column by column, as
    // tide_draw_camera_fit gives it (OpenGL's clip space, depth -1 to 1).
    // `device` gets the device and driver it draws with.
    bool (*open)(const float camera[16], const uint8_t texture[2 * 2 * 4], char *device, size_t device_size);
    void *(*instances)(vd_kind kind, size_t bytes);
    void (*written)(vd_kind kind, size_t bytes);
    // The pixel read back, red in its top byte (as tide_platform_read_pixels gives it).
    uint32_t (*render)(vd_kind kind, uint32_t count);
    // The last frame drawn, whole: RGBA, rows from the top. Outside timing.
    bool (*frame)(uint8_t *rgba);
    void (*close)(void);
} vd_api;

extern const vd_api vd_gl;     // draw_gl.c
extern const vd_api vd_vulkan; // draw_vk.c
