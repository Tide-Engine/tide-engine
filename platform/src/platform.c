#include "tide/platform.h"

#include <errno.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/page.h"
#include "tide/time.h"
#include "font.h"
#include "gpu.h"
#include "png.h"
#include "script.h"
#include "shaders.h"
#include "window.h"

uint8_t *tide_platform_pack(size_t *size)
{
    return tide_window_pack(size);
}

#ifdef __wasm__
#include "tide_web.h" // The page's frame loop (platform/web/tide.js)
#endif

#define COUNT_OF(array) (sizeof(array) / sizeof((array)[0]))

// Buttons are found by their offset in the device struct, in the order
// window.h has them, which is devices.h's.
#define BUTTON_AT(device, offset) ((tide_button *)((char *)(device) + (offset)))

#define KEY_OFFSET(name) offsetof(tide_keyboard, name),
#define MOUSE_OFFSET(name) offsetof(tide_mouse, name),
#define PAD_OFFSET(name) offsetof(tide_gamepad, name),
#define DPAD_OFFSET(name) offsetof(tide_gamepad, dpad.name),

static const size_t key_offsets[] = {TIDE_KEYBOARD_KEYS(KEY_OFFSET)};
static const size_t mouse_offsets[] = {TIDE_MOUSE_BUTTONS(MOUSE_OFFSET)};
static const size_t pad_offsets[] = {TIDE_GAMEPAD_BUTTONS(PAD_OFFSET) TIDE_DPAD_BUTTONS(DPAD_OFFSET)};

_Static_assert(COUNT_OF(key_offsets) == TIDE_KEY_COUNT, "a tide_key for every key");
_Static_assert(COUNT_OF(pad_offsets) == TIDE_PAD_COUNT, "a tide_pad_button for every gamepad button");

#ifdef __wasm__
static bool web_timer_frames; // Frames on timers instead of animation frames
#endif

static tide_frame_fn run_frame;
static void *run_user;
static uint64_t opened;    // When the window opened (tide_time_now_ns)
static double frame_start; // When the frame running, or the last one, started (now())
static double frame_due;   // When the next one comes while nobody sees the window
static bool due_asked;     // ...as the frame function asked (tide_platform_next_frame)

// The window's size as of the frame's start, in logical pixels, and in the
// pixels it renders.
static int screen_width = 1, screen_height = 1;
static int pixel_width = 1, pixel_height = 1;

// What draws in the window (see gpu.h), which it says once it's open.
static const tide_gpu *gpu;
static bool gpu_timing;

// The last frames' lengths, for tide_platform_fps.
static float frame_seconds[32];
static unsigned frames_counted;

// A headless window (tide_platform_open_headless): frames draw into pixels of
// the platform's own, at the size and scale it was asked for, in made-up time,
// with made-up devices (script.h). The window under it is never shown, and
// only there for its graphics context.
static bool headless;
static tide_headless_desc headless_run;
static uint32_t headless_frame; // The frame running, or the last one, counted from 1

// Seconds since the window opened.
static double now(void)
{
    return (double)(tide_time_now_ns() - opened) * 1e-9;
}

static void follow_window(void)
{
    if (headless) { // As it was asked for, whatever the display and the window under it are
        screen_width = headless_run.width;
        screen_height = headless_run.height;
        pixel_width = (int)((float)screen_width * headless_run.scale + 0.5f);
        pixel_height = (int)((float)screen_height * headless_run.scale + 0.5f);
        if (pixel_width < 1) pixel_width = 1;
        if (pixel_height < 1) pixel_height = 1;
        return;
    }
    tide_window_size(&screen_width, &screen_height);
    tide_window_pixel_size(&pixel_width, &pixel_height);
}

void tide_platform_open(const tide_window_desc *desc)
{
#if !defined(__wasm__) && !defined(__ANDROID__)
    // Nobody watches this run, so its window can't show, and one that isn't
    // hidden has nothing but its closing to end the run
    if (!desc->hidden && tide_hidden()) {
        fprintf(stderr, "tide: TIDE_HIDDEN is set, so no window shows, and this one's closing is all that would end its run: a "
                        "program that ends by itself opens its window hidden (tide_window_desc.hidden) or headless "
                        "(tide_platform_open_headless)\n");
        exit(2);
    }
#endif
    if (!tide_window_open(desc)) exit(1); // It said why
    gpu = tide_window_gpu();
    opened = tide_time_now_ns();
    follow_window();
#ifdef __wasm__
    web_timer_frames = desc->hidden;
#endif
}

void tide_platform_open_headless(const tide_headless_desc *desc)
{
#if defined(__wasm__) || defined(__ANDROID__)
    (void)desc;
    fprintf(stderr, "tide: a game runs without a window on desktop only\n");
    exit(2);
#else
    headless_run = *desc;
    if (headless_run.width <= 0) headless_run.width = 960;
    if (headless_run.height <= 0) headless_run.height = 540;
    if (!(headless_run.scale > 0.0f)) headless_run.scale = 1.0f;
    const float wide = (float)headless_run.width * headless_run.scale, high = (float)headless_run.height * headless_run.scale;
    if (wide > (float)TIDE_PNG_MAX_SIDE || high > (float)TIDE_PNG_MAX_SIDE) {
        fprintf(stderr, "tide: %d by %d at scale %g is more than a capture's %d pixels a side\n", headless_run.width,
                headless_run.height, (double)headless_run.scale, TIDE_PNG_MAX_SIDE);
        exit(2);
    }
    // The script first: a line that's wrong needs no window to say so
    if (headless_run.script && !tide_script_load(headless_run.script)) exit(2);
    if (!headless_run.frames && !tide_script_quits()) {
        fprintf(stderr, "tide: nothing ends this run, and it has no window to close: give it --frames <n>, or its script a "
                        "`quit` line\n");
        exit(2);
    }
    if (!tide_window_open(&(tide_window_desc){.title = "", .width = 64, .height = 64, .hidden = true})) exit(1); // It said why
    headless = true;
    gpu = tide_window_gpu();
    opened = tide_time_now_ns();
    follow_window();
#endif
}

// A headless run's frame, once it drew: the pictures its script and its desc
// ask of it. TIDE_KEEP_RUNNING, or the run's exit code.
static int headless_drawn(void)
{
    const int code = tide_script_drawn(headless_frame);
    if (code != 0 && code != TIDE_KEEP_RUNNING) return code;
    const bool last = code == 0 || (headless_run.frames && headless_frame >= headless_run.frames);
    if (last && headless_run.capture && !tide_platform_capture(headless_run.capture)) return 1;
    return last ? 0 : TIDE_KEEP_RUNNING;
}

_Noreturn static void finish(const int code)
{
    tide_platform_gpu_timing(false);
#ifdef __wasm__
    tide_web_stop();
#endif
    tide_window_close();
    exit(code);
}

void tide_platform_next_frame(const double seconds)
{
    const double due = frame_start + (seconds > 0.0 ? seconds : 0.0);
    if (!due_asked || due < frame_due) frame_due = due;
    due_asked = true;
}

static int step(void)
{
    const double start = now();
    // A headless window's time is made up: the same on every machine, every run
    const float seconds = headless ? 1.0f / 60.0f : (float)(start - frame_start);
    frame_start = start;
    frame_due = start + 1.0 / 60.0;
    due_asked = false;
    frame_seconds[frames_counted++ % COUNT_OF(frame_seconds)] = seconds;
    follow_window();
    if (headless) tide_script_input(++headless_frame); // In the devices at this frame's poll
    int code = run_frame(run_user, seconds);
    if (headless && code == TIDE_KEEP_RUNNING) code = headless_drawn();
    tide_window_present(); // Also takes the system's input
#ifndef __wasm__
    // A window nobody sees (minimized, or an app in the background) has no
    // display's pace to wait for: the next frame comes when the frame function
    // asked, and draws nothing (see tide_platform_draw). It waits on the
    // system, so a window restored or closed meanwhile goes on at once.
    while (tide_window_unseen() && !tide_window_should_close()) {
        const double left = frame_due - now();
        if (left <= 0.0) break;
        tide_window_wait(left);
    }
#endif
    return code;
}

#ifdef __wasm__
// The page calls it for every frame, once tide_platform_run starts the loop:
// seconds until the next one, which the page waits while it's hidden.
__attribute__((export_name("tide_web_frame"))) double tide_web_frame(void)
{
    const int code = step();
    if (code != TIDE_KEEP_RUNNING) finish(code);
    return frame_due - now();
}
#endif

void tide_platform_run(const tide_frame_fn frame, void *user)
{
    run_frame = frame;
    run_user = user;
    frame_start = now();
#ifdef __wasm__
    // Frames on requestAnimationFrame, or as fast as timers allow when hidden.
    // Doesn't return: it unwinds main's stack back to the browser.
    tide_web_run(web_timer_frames);
#else
    for (;;) {
        if (tide_window_should_close()) finish(0);
        const int code = step();
        if (code != TIDE_KEEP_RUNNING) finish(code);
    }
#endif
}

int tide_platform_fps(void)
{
    const unsigned count = frames_counted < COUNT_OF(frame_seconds) ? frames_counted : (unsigned)COUNT_OF(frame_seconds);
    float total = 0.0f;
    for (unsigned i = 0; i < count; i++) total += frame_seconds[i];
    return total > 0.0f ? (int)((float)count / total + 0.5f) : 0;
}

// ---------------------------------------------------------------------------
// Devices

// What the devices say comes from the window, or for a headless one from
// what was made up for it (script.h), which says it the same way.

static void poll_keyboard(tide_keyboard *k)
{
    for (size_t i = 0; i < COUNT_OF(key_offsets); i++) {
        const bool held = headless ? tide_script_key_held((tide_key)i) : tide_window_key_held((tide_key)i);
        tide_button_set(BUTTON_AT(k, key_offsets[i]), held);
    }
}

// The mouse; true if it was used: it moved or scrolled, or a button went down
// or up.
static bool poll_mouse(tide_mouse *m)
{
    static float last_x, last_y;
    static bool polled;
    tide_window_mouse mouse;
    if (headless) tide_script_mouse_state(&mouse);
    else tide_window_mouse_state(&mouse);
    // How it moved: where the cursor went or, while the window holds the
    // cursor (Cursor.Lock), what the mouse says it moved, as the cursor stays.
    const float dx = mouse.locked ? mouse.moved_x : polled ? mouse.x - last_x : 0.0f;
    const float dy = mouse.locked ? mouse.moved_y : polled ? mouse.y - last_y : 0.0f;
    last_x = mouse.x, last_y = mouse.y;
    polled = true;
    m->locked = mouse.locked;
    // The window counts from the top left, y down; Devices follows Unity: from
    // the bottom left, y up.
    m->position = tide_f2(mouse.x, (float)screen_height - mouse.y);
    // Delta and scroll add up until the input is sampled.
    m->delta = tide_f2(m->delta.x + dx, m->delta.y - dy);
    m->scroll = tide_f2(m->scroll.x + mouse.wheel_x, m->scroll.y + mouse.wheel_y);
    m->poll_delta = tide_f2(dx, -dy);
    m->poll_scroll = tide_f2(mouse.wheel_x, mouse.wheel_y);
    bool used = dx != 0.0f || dy != 0.0f || mouse.wheel_x != 0.0f || mouse.wheel_y != 0.0f;
    for (size_t i = 0; i < COUNT_OF(mouse_offsets); i++) {
        tide_button *b = BUTTON_AT(m, mouse_offsets[i]);
        const bool held = (mouse.buttons >> i & 1u) != 0;
        used |= held != b->held;
        tide_button_set(b, held);
    }
    return used;
}

// Fingers on a touchscreen: the page's on the web, the window's on Windows,
// the activity's on Android. Other desktops have none yet.
static void poll_touches(tide_touchscreen *s)
{
    tide_touches_poll(s);
    s->connected = headless ? tide_script_touchscreen() : tide_window_touchscreen();
    tide_touch_report r;
    while (headless ? tide_script_take_touch(&r) : tide_window_take_touch(&r)) {
        tide_touch_event(s, r.phase, r.source, tide_f2(r.x, (float)screen_height - r.y));
    }
}

static void poll_gamepad(tide_gamepad *g)
{
    tide_window_gamepad pad;
    if (headless) tide_script_gamepad_state(&pad);
    else tide_window_gamepad_state(&pad);
    g->connected = pad.connected;
    if (pad.connected) {
        g->leftStick = tide_f2(pad.left_x, -pad.left_y);
        g->rightStick = tide_f2(pad.right_x, -pad.right_y);
        g->leftTrigger = pad.left_trigger;
        g->rightTrigger = pad.right_trigger;
    } else {
        g->leftStick = g->rightStick = tide_f2(0.0f, 0.0f);
        g->leftTrigger = g->rightTrigger = 0.0f;
    }
    for (size_t i = 0; i < COUNT_OF(pad_offsets); i++) {
        tide_button_set(BUTTON_AT(g, pad_offsets[i]), pad.connected && (pad.buttons >> i & 1u) != 0);
    }
}

// What Ctrl+V (Cmd+V on macOS) pasted, as characters typed: the next to type
// is pasted[pasted_at]. On the web, the page does it (tide.js).
static uint32_t pasted[1024];
static uint32_t pasted_count, pasted_at;

static void paste(void)
{
    const char *text = headless ? NULL : tide_window_take_paste(); // Never the system's clipboard into a headless run
    if (!text) return;
    for (uint32_t c; pasted_count < COUNT_OF(pasted) && (c = tide_utf8_next(&text)) != 0;) {
        if (c >= 32 && c != 127) pasted[pasted_count++] = c; // Not newlines or tabs
    }
}

// Characters typed since the last poll, which follow the keyboard layout. A
// poll takes as many as it holds, and leaves the rest for the next.
static void poll_text(tide_typed *text)
{
    text->count = 0;
    paste();
    while (pasted_at < pasted_count && text->count < TIDE_TEXT_MAX) text->chars[text->count++] = pasted[pasted_at++];
    if (pasted_at == pasted_count) pasted_at = pasted_count = 0;
    while (text->count < TIDE_TEXT_MAX) {
        const uint32_t c = headless ? tide_script_take_char() : tide_window_take_char();
        if (c == 0) break;
        text->chars[text->count++] = c;
    }
}

void tide_platform_typing(const bool typing)
{
    static bool was;
    if (typing == was || headless) return;
    was = typing;
    tide_window_typing(typing);
}

void tide_platform_cursor_lock(const bool lock)
{
    if (headless) {
        tide_script_cursor_lock(lock);
        return;
    }
    tide_window_cursor_lock(lock); // Every frame: what holds the cursor changes under it (the focus, the browser)
}

void tide_platform_copy(const char *text)
{
    if (headless) return; // A run nobody watches leaves the clipboard of whoever is at the machine alone
    tide_window_copy(text);
}

bool tide_platform_input(const char *event)
{
    char error[512] = "";
    if (!headless) snprintf(error, sizeof error, "only a headless window's input is made up (tide_platform_open_headless)");
    else if (tide_script_event(event, error, sizeof error)) return true;
    fprintf(stderr, "tide: input '%s': %s\n", event, error);
    return false;
}

void tide_platform_poll(tide_devices *devices)
{
    poll_keyboard(&devices->keyboard);
    const bool mouse_used = poll_mouse(&devices->mouse);
    poll_gamepad(&devices->gamepad);
    poll_touches(&devices->touchscreen);
    tide_pointer_poll(devices, mouse_used);
    poll_text(&devices->keyboard.text);
}

// ---------------------------------------------------------------------------
// The renderer: a draw list's commands, through gpu.h, whichever API is under it.

// The camera maps world units (y up) to window pixels (y down). Each frame's
// list starts at the origin with 1 unit per pixel. After TIDE_DRAW_SCREEN, it
// maps the screen's pixels instead: from the top left, y down.
typedef struct camera {
    tide_float2 center;
    float scale; // Pixels per world unit
    bool gui;
} camera;

static camera last_camera = {{0.0f, 0.0f}, 1.0f, false};
static float draw_width, draw_height;           // The selected target's logical pixels, during a draw
static int draw_pixel_width, draw_pixel_height; // ...and its image's own

static tide_float2 to_screen(const camera *cam, const tide_float2 p)
{
    if (cam->gui) return tide_f2(p.x * cam->scale, p.y * cam->scale);
    const float width = draw_width > 0.0f ? draw_width : (float)screen_width;
    const float height = draw_height > 0.0f ? draw_height : (float)screen_height;
    return tide_f2(width * 0.5f + (p.x - cam->center.x) * cam->scale, height * 0.5f - (p.y - cam->center.y) * cam->scale);
}

// A rect's top left corner on screen: y goes up in the world, down in the GUI.
static tide_float2 rect_corner(const camera *cam, const tide_draw_command *c)
{
    const float half_height = cam->gui ? -c->b.y * 0.5f : c->b.y * 0.5f;
    return to_screen(cam, tide_f2(c->a.x - c->b.x * 0.5f, c->a.y + half_height));
}

// A color as the GPU takes it: a byte each.
typedef struct rgba {
    uint8_t r, g, b, a;
} rgba;

static uint8_t color_channel(const float v)
{
    if (!(v > 0.0f)) return 0; // Also NaN
    if (v >= 1.0f) return 255;
    return (uint8_t)(v * 255.0f + 0.5f);
}

static rgba to_rgba(const tide_color c)
{
    return (rgba){color_channel(c.r), color_channel(c.g), color_channel(c.b), color_channel(c.a)};
}

// Shapes go to the GPU as instances of one quad, each its shape in window
// pixels, so a run of them between other commands is one draw call, however
// many there are. The first kinds fill whole pixels (rects, their outlines and
// lines) or are plain circles; the ones from SHAPE_BOX on are the shapes with
// smooth edges (TIDE_DRAW_SHAPE), which a pixel is in by how far it is from
// their edge.

enum { SHAPE_RECT, SHAPE_WIRE_RECT, SHAPE_CIRCLE, SHAPE_WIRE_CIRCLE, SHAPE_LINE,
       SHAPE_BOX, SHAPE_ELLIPSE, SHAPE_ARC };

typedef struct shape {
    float a[2];       // Its middle, or a line's start
    float b[2];       // Half its size (a circle's radius, twice), or a line's end
    uint8_t color[4]; // RGBA: a smooth shape's fill at its top
    float kind;       // SHAPE_*
} shape;

// What a smooth shape is drawn with besides. Few shapes are smooth ones, and
// these are more than a shape again, so only a run that has some has them:
// one for each of its shapes, in a buffer of their own, which a pipeline of
// its own reads (see STEP_SHAPES). A run of the plain kinds alone, however
// long, goes to the GPU as it always did.
typedef struct shape_extra {
    float turn[2];     // Its rotation's cos and sin, on the screen; an arc's, to its middle
    float radius;      // Its corners'
    float outline;     // How wide its outline is, inside its edge
    float soft;        // How far its edge fades: outwards, or under 0, inwards
    float sweep;       // SHAPE_ARC: half the angle it covers
    uint8_t color2[4]; // Its fill at its bottom
    uint8_t rim[4];    // Its outline's color
} shape_extra;

// A mesh's corner, in window pixels. Meshes go to the GPU as triangles, three
// of these each: a run of them with one texture is one draw call. Text goes
// the same way, two triangles a glyph, its uv in the font atlas's pixels.
typedef struct mesh_vertex {
    float position[2];
    float uv[2];
    uint8_t color[4]; // RGBA
} mesh_vertex;

// What a list comes to, in order: runs of shapes, clears, runs of text, runs
// of triangles, clips, runs of instances of 3D meshes, and runs of instances
// of meshes through shaders of the game's. A STEP_STENCIL only ends the run
// before it: the steps after it have another stencil than the ones before.
enum { STEP_SHAPES, STEP_CLEAR, STEP_TEXT, STEP_MESH, STEP_CLIP, STEP_NO_CLIP, STEP_MESH_3D, STEP_SHADED,
       STEP_TARGET, STEP_BLIT, STEP_STENCIL };

typedef struct draw_step {
    uint32_t kind;    // STEP_*
    uint32_t blend;   // How it goes over what's there: tide_blend
    uint32_t depth;   // STEP_MESH_3D, and a STEP_SHADED through a 3D shader: what it does with depth, tide_depth
    uint32_t cull;    // ...and which of its triangles don't draw, as they reach the GPU: tide_gpu_cull
    uint32_t stencil; // What it does with its destination's stencil: a tide_stencil, packed. None, for a clear and a blit
    uint32_t target;  // The destination: 0 for the window, 1 + its place in the list's targets
    uint32_t first;   // STEP_SHAPES: its shapes; STEP_TEXT and STEP_MESH: its vertices;
                      // STEP_MESH_3D: its instances' transforms in the list's matrices;
                      // STEP_SHADED: its run's place in the list's
    uint32_t count;
    uint32_t texture; // STEP_MESH and STEP_MESH_3D: 1 + its texture's place in the list's, or 0 for none
    uint32_t filter;  // ...and how it's sampled: tide_filter...
    uint32_t wrap;    // ...and tide_wrap
    uint32_t mesh;    // STEP_MESH_3D: 1 + its mesh's place in the list's
    uint32_t camera;  // ...and 1 + its camera's matrix's place in the list's, or 0 for the first camera:
                      // a STEP_SHADED's too, for a 3D shader
    uint32_t fit;     // ...which is for a square screen (see TIDE_DRAW_CAMERA_3D)
    uint32_t smooth;  // STEP_SHAPES: for a run with smooth shapes, 1 + where its shapes' extras start; else 0
    camera through;   // STEP_SHADED: the camera its command was under, for a 2D shader
    tide_gpu_id pipeline; // What draws it, once it's found: its shader's, for a STEP_SHADED
    tide_color tint;  // STEP_MESH_3D: what its pixels' colors are multiplied by
    tide_float2 at;   // STEP_CLIP: its top left corner in window pixels
    tide_float2 to;   // ...and its bottom right one
    rgba color;       // STEP_CLEAR
} draw_step;

static shape *shapes;
static uint32_t shape_count, shape_capacity;
static shape_extra *shape_extras;
static uint32_t shape_extra_count, shape_extra_capacity;
static mesh_vertex *mesh_vertices;
static uint32_t mesh_vertex_count, mesh_vertex_capacity;
static draw_step *steps;
static uint32_t step_count, step_capacity;
static uint32_t draw_target;
static uint32_t draw_blend; // How the command being turned into steps blends: tide_blend
static uint32_t draw_stencil; // What the selected destination's commands do with its stencil, by now: a tide_stencil, packed
static uint32_t draw_calls; // What the last list drawn took, the overlay's aside (tide_platform_draw_calls)

// A target's cameras and clip belong to it for the whole frame, including
// passes recorded by different views. The clip is advanced during playback.
typedef struct frame_target {
    camera through, world;
    uint32_t camera_3d, fit;
    bool mirrored; // Its 3D camera mirrors what it sees, so triangles reach the GPU wound the other way
    uint32_t stencil; // What its commands do with its stencil, as the last one turned into steps found it
    draw_step clipping;
    tide_gpu_id gpu;
    float width, height;           // Its logical pixels, as the window's
    int pixel_width, pixel_height; // ...and its image's own
    uint64_t id;
    uint64_t version;
    tide_target_size size; // As the list recorded it
    bool valid;
    bool bare; // It holds depth alone (TIDE_TARGET_DEPTH): a color has nowhere to go there
    tide_gpu_target_format into; // What it holds, which the pipelines that draw into it are made for
} frame_target;

static frame_target *frame_targets;
static uint32_t frame_target_capacity;

// What a list is drawn into, where its commands for the window go: the
// window, a headless one's frame, or pixels to read back. It has a size in
// logical pixels and one in its own, and how many of the second are in the
// first is the display's scale for this draw: the list's render targets that
// are at the display's scale take it from here, as its text does.
typedef struct draw_root {
    tide_gpu_id gpu;
    float width, height;
    int pixel_width, pixel_height;
} draw_root;

static draw_root root; // The draw's, while a list is drawn

// The most pixels a side a render target's image has on the device there is
// (tide_gpu.target_limit), or 0 before one drew.
static int target_most;

// Whether the selected destination's pixels aren't logical ones: a window's
// on a scaled display, or a render target's at another scale than 1. Circles
// are drawn as smooth shapes there (see add_circle).
static bool scaled;

// The window's pixels in a logical one, which its text is fitted to (see font.h).
static tide_float2 window_scale(void)
{
    return tide_f2((float)pixel_width / (float)screen_width, (float)pixel_height / (float)screen_height);
}

// The selected destination's pixels per logical pixel.
static tide_float2 pixel_scale(void)
{
    if (!(draw_width > 0.0f)) return window_scale(); // Not in a draw
    return tide_f2((float)draw_pixel_width / draw_width, (float)draw_pixel_height / draw_height);
}

// A whole number of pixels for `pixels` of them: the nearest, 1 at least.
static int whole_pixels(const double pixels)
{
    // A size is any number a game says, and before the first draw nothing
    // has said what the GPU takes: more than an int holds is the most of one
    if (!(pixels >= 1.5)) return 1;
    return pixels < 2147483647.0 ? (int)floor(pixels + 0.5) : INT32_MAX;
}

// A render target's size as it's drawn under `to`: its logical pixels, like
// the window's, and its image's own. One at the display's scale has as many
// of those in a logical one as `to` has, and one that follows the window has
// `to`'s size times its factor, in both. An image the GPU has no room for
// keeps its shape with fewer pixels (`limited`): what's drawn into it still
// lands where it would, less sharp. False for a size there's no image of.
static bool target_extent(const tide_target_size size, const draw_root *to, float *width, float *height,
                          int *wide_pixels, int *high_pixels, bool *limited)
{
    *width = *height = 0.0f;
    *wide_pixels = *high_pixels = 0;
    *limited = false;
    if (!tide_target_size_valid(size) || !(to->width > 0.0f) || !(to->height > 0.0f)) return false;
    const tide_float2 logical = tide_target_size_logical(size, tide_f2(to->width, to->height));
    if (!(logical.x > 0.0f) || !(logical.y > 0.0f)) return false;
    double wide, high;
    if (size.window > 0.0f) { // The window's own pixels, times the factor: all of them for 1
        wide = (double)to->pixel_width * (double)size.window;
        high = (double)to->pixel_height * (double)size.window;
    } else if (size.scale > 0.0f) {
        wide = (double)size.width * (double)size.scale;
        high = (double)size.height * (double)size.scale;
    } else {
        wide = (double)size.width * (double)to->pixel_width / (double)to->width;
        high = (double)size.height * (double)to->pixel_height / (double)to->height;
    }
    const double longest = wide > high ? wide : high;
    if (target_most > 0 && floor(longest + 0.5) > (double)target_most) {
        wide *= (double)target_most / longest;
        high *= (double)target_most / longest;
        *limited = true;
    }
    *width = logical.x;
    *height = logical.y;
    *wide_pixels = whole_pixels(wide);
    *high_pixels = whole_pixels(high);
    return true;
}

// Room for `count` more extras after the ones there.
static shape_extra *extra_room(const uint32_t count)
{
    if (count > shape_extra_capacity - shape_extra_count) {
        uint64_t capacity = shape_extra_capacity ? shape_extra_capacity : 256u;
        while (capacity < (uint64_t)shape_extra_count + count) capacity *= 2u;
        if (capacity > UINT32_MAX / sizeof(shape_extra)) tide_out_of_memory();
        shape_extras = tide_realloc(shape_extras, shape_extra_capacity * sizeof(shape_extra),
                                    (size_t)capacity * sizeof(shape_extra));
        shape_extra_capacity = (uint32_t)capacity;
    }
    return shape_extras + shape_extra_count;
}

// Every shape of a run that has smooth ones has an extra, in the run's order.
// Plain shapes don't say so as they're added, which keeps adding one as cheap
// as it was: the ones added since the run's last smooth shape get theirs
// here, zeroed, as nothing reads them. Called before the run's next smooth
// shape, and once the run is over.
static void settle_extras(void)
{
    if (step_count == 0) return;
    const draw_step *run = &steps[step_count - 1];
    if (run->kind != STEP_SHAPES || !run->smooth) return;
    const uint32_t missing = run->count - (shape_extra_count - (run->smooth - 1u));
    if (missing == 0) return;
    memset(extra_room(missing), 0, (size_t)missing * sizeof(shape_extra));
    shape_extra_count += missing;
}

static draw_step *add_step(const uint32_t kind)
{
    settle_extras(); // The run before it, if it was one of shapes, is over
    if (step_count == step_capacity) {
        const uint32_t capacity = step_capacity ? step_capacity * 2u : 64u;
        steps = tide_realloc(steps, step_capacity * sizeof(draw_step), capacity * sizeof(draw_step));
        step_capacity = capacity;
    }
    draw_step *s = &steps[step_count++];
    // A clear sets stencil back whatever is set, and a blit is its destination's whatever is
    *s = (draw_step){.kind = kind, .blend = draw_blend, .target = draw_target,
                     .stencil = kind == STEP_CLEAR || kind == STEP_BLIT ? 0u : draw_stencil};
    return s;
}

// Whether what's drawn next goes on with the last step: it's of that kind, and blends as it does.
static bool run_goes_on(const uint32_t kind)
{
    return step_count != 0 && steps[step_count - 1].kind == kind && steps[step_count - 1].blend == draw_blend;
}

// A run of shapes starts. What multiplies goes through the pipeline that
// draws every kind, the one whose colors go to white where they're clear, so
// each of its shapes has an extra (see settle_extras).
static void start_shapes(void)
{
    draw_step *s = add_step(STEP_SHAPES);
    s->first = shape_count;
    if (draw_blend == TIDE_BLEND_MULTIPLY) s->smooth = shape_extra_count + 1u;
}

// A shape, after the ones before it when nothing came between them. A frame
// of plain shapes spends its time here.
static void add_shape(const int kind, const float ax, const float ay, const float bx, const float by, const rgba color)
{
    if (shape_count == shape_capacity) {
        const uint32_t capacity = shape_capacity ? shape_capacity * 2u : 1024u;
        if (capacity > UINT32_MAX / sizeof(shape)) tide_out_of_memory();
        shapes = tide_realloc(shapes, shape_capacity * sizeof(shape), capacity * sizeof(shape));
        shape_capacity = capacity;
    }
    if (!run_goes_on(STEP_SHAPES)) start_shapes();
    steps[step_count - 1].count++;
    shapes[shape_count++] = (shape){{ax, ay}, {bx, by}, {color.r, color.g, color.b, color.a}, (float)kind};
}

// A smooth shape in window pixels, with its extra. The first one of a run
// makes it a run of smooth shapes, whose every shape has an extra.
static void add_smooth(const int kind, const float x, const float y, const float half_width, const float half_height,
                       const rgba color, const shape_extra extra)
{
    if (!run_goes_on(STEP_SHAPES)) start_shapes();
    if (!steps[step_count - 1].smooth) steps[step_count - 1].smooth = shape_extra_count + 1u;
    settle_extras(); // The plain shapes before it in the run
    add_shape(kind, x, y, half_width, half_height, color);
    *extra_room(1) = extra;
    shape_extra_count++;
}

// A TIDE_DRAW_SHAPE command's shape, through the camera: its lengths in window
// pixels, and its angles as the screen has them, where y goes down.
static void add_smooth_shape(const tide_draw_list *list, const tide_draw_command *c, const camera *cam)
{
    if (c->shape >= list->shape_count) return;
    static const int kinds[] = {[TIDE_SHAPE_BOX] = SHAPE_BOX, [TIDE_SHAPE_ELLIPSE] = SHAPE_ELLIPSE,
                                [TIDE_SHAPE_ARC] = SHAPE_ARC};
    const tide_draw_shape_data *s = &list->shapes[c->shape];
    const uint32_t form = s->form & (uint32_t)TIDE_SHAPE_FORMS;
    if (form > TIDE_SHAPE_ARC) return;
    const tide_float2 center = to_screen(cam, c->a);
    const float scale = cam->scale;
    const rgba color2 = to_rgba(s->color2), rim = to_rgba(s->outline_color);
    add_smooth(kinds[form], center.x, center.y, c->b.x * scale * 0.5f, c->b.y * scale * 0.5f, to_rgba(c->color),
               (shape_extra){.turn = {s->turn.x, cam->gui ? s->turn.y : -s->turn.y},
                             .radius = s->radius * scale,
                             // A hairline is a logical pixel wide, whatever the camera
                             .outline = s->form & (uint32_t)TIDE_SHAPE_HAIRLINE ? 1.0f : s->outline * scale,
                             .soft = s->soft * scale,
                             .sweep = s->sweep,
                             .color2 = {color2.r, color2.g, color2.b, color2.a},
                             .rim = {rim.r, rim.g, rim.b, rim.a}});
}

// A circle or its outline, `radius` window pixels around (x, y). The plain
// kind's edge is smooth over a logical pixel, which is one of the target's
// where the display isn't scaled. Where it is, that would be soft: there a
// circle goes as the smooth shape it also is, a box that's all corners, whose
// edge is smooth over one of the target's own pixels. Its outline is a
// logical pixel wide either way.
static void add_circle(const bool wire, const float x, const float y, const float radius, const rgba color)
{
    if (!scaled) {
        add_shape(wire ? SHAPE_WIRE_CIRCLE : SHAPE_CIRCLE, x, y, radius, radius, color);
        return;
    }
    const float r = fabsf(radius);
    const rgba fill = wire ? (rgba){0} : color;
    add_smooth(SHAPE_BOX, x, y, r, r, fill,
               (shape_extra){.turn = {1.0f, 0.0f},
                             .radius = r,
                             .outline = wire ? fminf(r, 1.0f) : 0.0f,
                             .color2 = {fill.r, fill.g, fill.b, fill.a},
                             .rim = {color.r, color.g, color.b, color.a}});
}

// Room for `count` more vertices after the ones there.
static mesh_vertex *mesh_room(const uint32_t count)
{
    if (count > mesh_vertex_capacity - mesh_vertex_count) {
        uint64_t capacity = mesh_vertex_capacity ? mesh_vertex_capacity : 4096u;
        while (capacity < (uint64_t)mesh_vertex_count + count) capacity *= 2u;
        if (capacity > UINT32_MAX / sizeof(mesh_vertex)) tide_out_of_memory();
        mesh_vertices = tide_realloc(mesh_vertices, mesh_vertex_capacity * sizeof(mesh_vertex),
                                     (size_t)capacity * sizeof(mesh_vertex));
        mesh_vertex_capacity = (uint32_t)capacity;
    }
    return mesh_vertices + mesh_vertex_count;
}

// A mesh command's triangles, their corners through the camera, after the
// ones before it when nothing came between them.
static void add_mesh(const tide_draw_list *list, const tide_draw_command *c, const camera *cam)
{
    const uint32_t count = c->mesh.count;
    mesh_vertex *out = mesh_room(count);
    draw_step *s = run_goes_on(STEP_MESH) ? &steps[step_count - 1] : NULL;
    if (!s || s->texture != c->mesh.texture || s->filter != c->mesh.filter || s->wrap != c->mesh.wrap) {
        s = add_step(STEP_MESH);
        s->first = mesh_vertex_count;
        s->texture = c->mesh.texture;
        s->filter = c->mesh.filter;
        s->wrap = c->mesh.wrap;
    }
    s->count += count;
    const uint32_t *indices = list->indices + c->mesh.first;
    for (uint32_t i = 0; i < count; i++) {
        const tide_vertex *v = &list->vertices[indices[i]];
        const tide_float2 p = to_screen(cam, v->position);
        const rgba color = to_rgba(v->color);
        out[i] = (mesh_vertex){{p.x, p.y}, {v->uv.x, v->uv.y}, {color.r, color.g, color.b, color.a}};
    }
    mesh_vertex_count += count;
}

// Where the line of text at `line` starts, in the target's pixels, a whole
// one: at `x`, or for a `side` of 1 or 2, with its middle or its end there.
static float line_start(const unsigned char *font, const char *line, const int pixels, const float stretch,
                        const float x, const uint32_t side)
{
    int64_t across = 0; // A line of any length: more pixels than an int counts
    for (uint32_t c; side && (c = tide_utf8_next(&line)) != 0 && c != '\n';) across += tide_font_advance(font, c, pixels);
    return floorf(x - (float)across * stretch * 0.5f * (float)side + 0.5f);
}

// Text of `size` in window pixels, in the weight `font` is the file of, with
// the place `anchor` names (a tide_anchor) of its box at `at`: a quad a glyph,
// after the text before it when nothing came between them. It's drawn on the
// target's own pixels (see font.h): its lines start on one, and each glyph is
// a whole number of them from the last, so none is ever blended between two.
static void add_text(const char *text, const tide_float2 at, const float size, const rgba color, const uint32_t anchor,
                     const unsigned char *font)
{
    if (!(size > 0.0f)) return;
    const tide_float2 scale = pixel_scale();
    float stretch;
    const int pixels = tide_font_pixels(size, scale.y, &stretch);
    const float pitch = tide_font_pitch(font, pixels, stretch);
    // The anchor's column and row: each line is against its side, and the
    // lines are a box as tall as tide_font_size says, whose top, middle or
    // bottom is at `at`
    const uint32_t side = anchor % 3u, row = anchor / 3u;
    float above = 0.0f;
    if (row) {
        uint32_t lines = 1;
        for (const char *c = text; *c; c++) lines += *c == '\n';
        above = (size * scale.y + (float)(lines - 1u) * pitch) * 0.5f * (float)row;
    }
    // In the target's pixels, as the pen is
    const float x = at.x * scale.x;
    float pen_x = line_start(font, text, pixels, stretch, x, side);
    float pen_y = floorf((at.y + tide_font_baseline(font) * size) * scale.y - above + 0.5f); // On the baseline

    if (!run_goes_on(STEP_TEXT)) add_step(STEP_TEXT)->first = mesh_vertex_count;
    for (uint32_t c; (c = tide_utf8_next(&text)) != 0;) {
        if (c == '\n') {
            pen_x = line_start(font, text, pixels, stretch, x, side);
            pen_y += pitch;
            continue;
        }
        tide_font_glyph g;
        if (!tide_font_glyph_for(font, c, pixels, &g)) continue; // No room for it this frame
        if (g.width) {
            const float x0 = (pen_x + (float)g.left * stretch) / scale.x;
            const float y0 = (pen_y + (float)g.top * stretch) / scale.y;
            const float x1 = x0 + (float)g.width * stretch / scale.x, y1 = y0 + (float)g.height * stretch / scale.y;
            const float u0 = (float)g.x, v0 = (float)g.y, u1 = u0 + (float)g.width, v1 = v0 + (float)g.height;
            const mesh_vertex corners[4] = {
                {{x0, y0}, {u0, v0}, {color.r, color.g, color.b, color.a}},
                {{x1, y0}, {u1, v0}, {color.r, color.g, color.b, color.a}},
                {{x1, y1}, {u1, v1}, {color.r, color.g, color.b, color.a}},
                {{x0, y1}, {u0, v1}, {color.r, color.g, color.b, color.a}},
            };
            mesh_vertex *out = mesh_room(6); // After add_step: the steps before it keep their vertices' places
            out[0] = corners[0], out[1] = corners[1], out[2] = corners[2];
            out[3] = corners[0], out[4] = corners[2], out[5] = corners[3];
            mesh_vertex_count += 6;
            steps[step_count - 1].count += 6;
        }
        pen_x += (float)g.advance * stretch;
    }
}

// Text a transform turned or stretched (see tide_draw_set_transform): laid out
// as it would be upright, at the size the transform's mean scale makes it,
// then each glyph through the transform. `at` is where the place `anchor`
// names of its box is on the screen, and `size` its font size in the units
// its command is in, as the transform found it. There are no pixels to fit it
// to: its glyphs are the ones fitted for upright text of that size, and their
// pixels blend where they land.
static void add_text_turned(const char *text, const tide_float2 at, const float size, const camera *cam,
                            const tide_float2x2 turn, const rgba color, const uint32_t anchor, const unsigned char *font)
{
    const float mean = sqrtf(fabsf(turn.c0.x * turn.c1.y - turn.c0.y * turn.c1.x));
    const float drawn = size * cam->scale * mean; // Its size on the screen, in logical pixels
    if (!(drawn > 0.0f) || !(mean > 0.0f)) return;
    const tide_float2 scale = pixel_scale();
    float stretch;
    const int pixels = tide_font_pixels(drawn, scale.y, &stretch);
    const float pitch = tide_font_pitch(font, pixels, stretch);
    // The anchor's column and row, as upright text has them (see add_text)
    const uint32_t side = anchor % 3u, row = anchor / 3u;
    float above = 0.0f;
    if (row) {
        uint32_t lines = 1;
        for (const char *c = text; *c; c++) lines += *c == '\n';
        above = (drawn * scale.y + (float)(lines - 1u) * pitch) * 0.5f * (float)row;
    }
    // Where the layout's right and down go on the screen: through the transform, in the units it's in,
    // whose y goes up under a camera, and less its scale, which the layout's size has already
    const float up = cam->gui ? 1.0f : -1.0f;
    const tide_float2 right = tide_f2(turn.c0.x / mean, turn.c0.y * up / mean);
    const tide_float2 down = tide_f2(turn.c1.x * up / mean, turn.c1.y / mean);
    // From its anchor, in the target's pixels
    float pen_x = line_start(font, text, pixels, stretch, 0.0f, side);
    float pen_y = floorf(tide_font_baseline(font) * drawn * scale.y - above + 0.5f);

    if (!run_goes_on(STEP_TEXT)) add_step(STEP_TEXT)->first = mesh_vertex_count;
    for (uint32_t c; (c = tide_utf8_next(&text)) != 0;) {
        if (c == '\n') {
            pen_x = line_start(font, text, pixels, stretch, 0.0f, side);
            pen_y += pitch;
            continue;
        }
        tide_font_glyph g;
        if (!tide_font_glyph_for(font, c, pixels, &g)) continue; // No room for it this frame
        if (g.width) {
            // Half an atlas pixel more each way: the clear one around the glyph, which its edge blends with
            const float x0 = (pen_x + ((float)g.left - 0.5f) * stretch) / scale.x;
            const float y0 = (pen_y + ((float)g.top - 0.5f) * stretch) / scale.y;
            const float x1 = x0 + ((float)g.width + 1.0f) * stretch / scale.x;
            const float y1 = y0 + ((float)g.height + 1.0f) * stretch / scale.y;
            const float u0 = (float)g.x - 0.5f, v0 = (float)g.y - 0.5f;
            const float u1 = u0 + (float)g.width + 1.0f, v1 = v0 + (float)g.height + 1.0f;
            const float xs[4] = {x0, x1, x1, x0}, ys[4] = {y0, y0, y1, y1};
            const float us[4] = {u0, u1, u1, u0}, vs[4] = {v0, v0, v1, v1};
            mesh_vertex corners[4];
            for (int i = 0; i < 4; i++) {
                corners[i] = (mesh_vertex){{at.x + xs[i] * right.x + ys[i] * down.x, at.y + xs[i] * right.y + ys[i] * down.y},
                                           {us[i], vs[i]},
                                           {color.r, color.g, color.b, color.a}};
            }
            mesh_vertex *out = mesh_room(6);
            out[0] = corners[0], out[1] = corners[1], out[2] = corners[2];
            out[3] = corners[0], out[4] = corners[2], out[5] = corners[3];
            mesh_vertex_count += 6;
            steps[step_count - 1].count += 6;
        }
        pen_x += (float)g.advance * stretch;
    }
}

// The engine's own pipelines, each in a version for each way of blending
// (tide_blend) and, for the 3D meshes', each thing a mesh does with depth
// (tide_depth) and each side of a triangle it leaves out (tide_gpu_cull),
// made the first time a list draws with it.
enum { PIPELINE_SHAPES, PIPELINE_SMOOTH_SHAPES, PIPELINE_MESHES, PIPELINE_TEXT, PIPELINE_MESHES_3D, PIPELINE_KINDS };
enum { BLENDS = TIDE_BLEND_REPLACE + 1, DEPTHS = TIDE_DEPTH_NONE + 1, CULLS = TIDE_GPU_CULL_FRONT + 1 };

// Stencil's numbers are the draw list's all the way down.
_Static_assert((int)TIDE_GPU_STENCIL_ALWAYS == (int)TIDE_STENCIL_ALWAYS && (int)TIDE_GPU_STENCIL_NEVER == (int)TIDE_STENCIL_NEVER
               && (int)TIDE_GPU_STENCIL_LESS == (int)TIDE_STENCIL_LESS && (int)TIDE_GPU_STENCIL_EQUAL == (int)TIDE_STENCIL_EQUAL
               && (int)TIDE_GPU_STENCIL_LESS_EQUAL == (int)TIDE_STENCIL_LESS_EQUAL
               && (int)TIDE_GPU_STENCIL_GREATER == (int)TIDE_STENCIL_GREATER
               && (int)TIDE_GPU_STENCIL_NOT_EQUAL == (int)TIDE_STENCIL_NOT_EQUAL
               && (int)TIDE_GPU_STENCIL_GREATER_EQUAL == (int)TIDE_STENCIL_GREATER_EQUAL,
               "a pipeline's stencil tests are tide_stencil_test's");
_Static_assert((int)TIDE_GPU_STENCIL_KEEP == (int)TIDE_STENCIL_KEEP && (int)TIDE_GPU_STENCIL_REPLACE == (int)TIDE_STENCIL_REPLACE
               && (int)TIDE_GPU_STENCIL_INCREMENT == (int)TIDE_STENCIL_INCREMENT
               && (int)TIDE_GPU_STENCIL_DECREMENT == (int)TIDE_STENCIL_DECREMENT
               && (int)TIDE_GPU_STENCIL_ZERO == (int)TIDE_STENCIL_ZERO && (int)TIDE_GPU_STENCIL_INVERT == (int)TIDE_STENCIL_INVERT,
               "a pipeline's stencil operations are tide_stencil_op's");

// What of a step's stencil is its pipeline's: its test and what it stores (a
// tide_stencil_test, and a tide_stencil_op from the fourth bit). Its number
// is set at the draw (tide_gpu.stencil), so no pipeline is made for one.
static uint32_t stencil_state(const uint32_t stencil)
{
    return stencil & 255u;
}

// ...and its number.
static uint32_t stencil_value(const uint32_t stencil)
{
    return stencil >> 8 & 255u;
}

// Whether a step that draws changes the numbers stored where it draws.
static bool stencil_stores(const uint32_t stencil)
{
    return (stencil >> 4 & 15u) != TIDE_STENCIL_KEEP;
}

// The engine's pipelines that test or store stencil, each made the first time
// a list draws with it, like the others: by kind, blend, depth and cull, by
// the stencil's test and what it stores, and by what they draw into. Few
// games have more than a handful, so they're a list, looked through.
typedef struct stencil_pipeline {
    uint8_t kind, blend, depth, cull, state;
    tide_gpu_target_format into; // What the targets it draws into hold
    tide_gpu_id pipeline;
} stencil_pipeline;

static stencil_pipeline *stencil_pipelines;
static uint32_t stencil_pipeline_count, stencil_pipeline_capacity;

// The number the pass's stencil tests compare with, as the backend has it: 0 as a pass begins.
static uint32_t pass_stencil;

// A pass a list's steps draw in.
static void begin_pass(const tide_gpu_id target, const bool clear)
{
    gpu->begin(target, clear);
    pass_stencil = 0;
}

// A step's pipeline, for the draws after it, with the step's stencil number where the pass has another.
static void use_pipeline(const draw_step *s)
{
    gpu->use(s->pipeline);
    const uint32_t value = stencil_value(s->stencil);
    if (value != pass_stencil) gpu->stencil(pass_stencil = value);
}

// What a pipeline does with depth for each tide_depth.
static const tide_gpu_depth gpu_depths[DEPTHS] = {
    [TIDE_DEPTH_READ_WRITE] = TIDE_GPU_DEPTH_TEST, [TIDE_DEPTH_READ] = TIDE_GPU_DEPTH_READ,
    [TIDE_DEPTH_NONE] = TIDE_GPU_DEPTH_NONE};

// What the renderer keeps on the GPU (see gpu.h): its pipelines, whose
// shaders are in shaders.h, and the buffers and textures a list's steps draw
// from, made the first time they're needed.
static struct {
    tide_gpu_id far; // A target of depth alone that's 1 everywhere: what a shader reads for a depth it wasn't given
    tide_gpu_id corners; // The shapes' quad
    tide_gpu_id shape_instances;
    uint32_t shape_capacity; // Shapes the instance buffer holds
    tide_gpu_id shape_extras;
    uint32_t shape_extra_capacity; // Extras theirs holds
    tide_gpu_id mesh_vertices;
    uint32_t mesh_capacity; // Vertices the buffer holds
    tide_gpu_id matrices;
    uint32_t matrix_capacity; // Transforms the 3D meshes' instance buffer holds
    tide_gpu_id instances;
    uint32_t instance_capacity; // Bytes the shaded runs' instance buffer holds
    tide_gpu_id white;        // A white pixel, which a mesh with no texture samples
    tide_gpu_id atlas;        // The font atlas, as of `atlas_version`
    int atlas_width, atlas_height;
    uint32_t atlas_version;
    tide_gpu_id target; // What tide_platform_read_pixels draws into, kept while the window keeps its size
    int target_width, target_height;
    uint8_t *target_pixels; // ...read back: RGBA, rows from the top
    tide_gpu_id frame; // What a headless window's frames draw into, in its window's place
    int frame_width, frame_height;
    uint8_t *frame_pixels; // ...read back (tide_platform_frame_pixels)
} gpu_state;

// The engine's own pipelines into one kind of target, which is what it holds
// (a pipeline is made for one): the window's kind, one color image of 4 bytes
// a pixel, first, and another for each kind a list draws into. They draw one
// color, into a target's first color image, and leave the others it has
// alone, but for the clears', which fill every one. Into depth alone, only
// the 3D meshes' and the clears' leave anything, and what stores stencil
// (writes_depth). The ones that test or store stencil are apart, a list of
// their own (stencil_pipelines), by what their targets hold too.
typedef struct engine_pipelines {
    tide_gpu_target_format into;
    tide_gpu_id made[PIPELINE_KINDS][BLENDS][DEPTHS][CULLS]; // Only the 3D meshes' have more than the first depth and cull
    tide_gpu_id clear; // The clears'
} engine_pipelines;

static engine_pipelines *engine_made;
static uint32_t engine_made_count;

static engine_pipelines *pipelines_into(const tide_gpu_target_format into)
{
    for (uint32_t i = 0; i < engine_made_count; i++) {
        if (engine_made[i].into == into) return &engine_made[i];
    }
    engine_made = tide_realloc(engine_made, engine_made_count * sizeof(engine_pipelines),
                               (engine_made_count + 1u) * sizeof(engine_pipelines));
    engine_made[engine_made_count] = (engine_pipelines){.into = into};
    return &engine_made[engine_made_count++];
}

// What a render target holds, as the GPU has it: its color images' formats, in order.
static tide_gpu_target_format gpu_target_format(const uint64_t id)
{
    _Static_assert(TIDE_TARGET_COLORS == (int)TIDE_GPU_MAX_COLORS, "gpu.h's targets hold as many colors as draw.h's");
    const tide_target_formats held = tide_render_target_formats(id);
    tide_gpu_target_format format = TIDE_GPU_TARGET_DEPTH;
    for (uint32_t i = 0; i < held.count; i++) {
        format = tide_gpu_target_with(format, held.color[i] == TIDE_TARGET_RGBA16F ? TIDE_GPU_COLOR_FLOAT : TIDE_GPU_COLOR_BYTES);
    }
    // ...and its samples, as this device has them for what it asked for
    const uint32_t asked = tide_render_target_samples(id);
    if (asked > 1u && held.count) {
        const uint32_t samples = gpu->target_samples(tide_gpu_target_sampled(format, asked));
        if (samples < asked) {
            static bool said;
            if (!said) {
                fprintf(stderr, "tide: a render target asked for %u samples a pixel, and this device's %s has %u for "
                                "what it holds: it's drawn with %u\n", (unsigned)asked, gpu->name, (unsigned)samples,
                        (unsigned)samples);
            }
            said = true;
        }
        format = tide_gpu_target_sampled(format, samples);
    }
    return format;
}

// The shapes', meshes' and text's uniforms, as their shaders have them: from
// the target's logical pixels (from its top left, y down) to clip space, as
// OpenGL's matrices go, column by column; for text, 1 over the atlas's size;
// for shapes, the target's own pixels in a logical one, across and down,
// which is how they know where a pixel ends; and `fix`, whose first is 1 for
// what multiplies what's there (TIDE_BLEND_MULTIPLY), 0 otherwise: there a
// color goes to white as its alpha goes to 0, so what's clear changes nothing.
// Its second is 1 for a step that stores stencil: there a pixel is drawn, and
// stores, wherever the draw touches it, a clear shape's too, and none of the
// text's quads' that no glyph covers is.
// A run of the plain shapes alone has `screen` only.
typedef struct screen_uniforms {
    float screen[16];
    float texel[2];
    float scale[2];
    float fix[4];
} screen_uniforms;

// The 3D meshes': the camera, from the world to clip space, what tints them,
// and `fix`, as above.
typedef struct mesh_3d_uniforms {
    tide_float4x4 camera;
    float tint[4];
    float fix[4];
} mesh_3d_uniforms;

// What the uniforms of a shader of the game's start with (see tide_shader),
// before a run's own: `tide_camera`, from the units its mesh is in to clip
// space, `tide_screen`, the target's pixels in a logical one, across and
// down, its height in its own pixels, and 1 where the draw multiplies what's
// there, and `tide_tint`, what its pixels' colors are multiplied by.
typedef struct shaded_uniforms {
    tide_float4x4 camera;
    float screen[4];
    float tint[4];
} shaded_uniforms;

// A float4 short of the block, too: Android's emulator links a shader whose
// block is 255 float4s and refuses one of 256, which it counts as its
// vertex shader's uniform vectors
_Static_assert(sizeof(shaded_uniforms) + TIDE_SHADER_UNIFORM_BYTES <= TIDE_GPU_MAX_UNIFORMS - 16u,
               "a shader's uniforms fit a pipeline's block");
_Static_assert(TIDE_SHADER_ATTRIBUTES <= TIDE_GPU_MAX_ATTRIBUTES && TIDE_SHADER_TEXTURES <= TIDE_GPU_MAX_TEXTURES,
               "a shader's attributes and textures fit a pipeline's");

// A 3D mesh's corner on the GPU. A mesh goes there as its corners, each
// once, and its triangles as indices into them, uploaded when a list first
// draws it, and let go once a frame draws without it. A mesh for a shader of
// the game's (TIDE_MESH_BYTES) goes as the list has it: the shader reads it.
typedef struct mesh_3d_vertex {
    float position[3];
    float uv[2];
    uint8_t color[4]; // RGBA
} mesh_3d_vertex;

// The GPU's copy of a list's 3D mesh, kept in the place the list keeps it.
typedef struct gpu_mesh {
    uint64_t id, other, version; // Which it is (see tide_draw_mesh_data)
    uint32_t space;
    uint32_t epoch;
    tide_gpu_id vertices, indices; // Its buffers
    uint32_t vertex_capacity;      // Bytes `vertices` has room for
    uint32_t index_capacity;       // Bytes `indices` has room for
    uint32_t index_count;          // Three a triangle...
    bool wide;                     // ...of 16 bits where they fit, or 32
    uint32_t frame;                // The frame that last drew it
} gpu_mesh;

static gpu_mesh *gpu_meshes;
static uint32_t gpu_mesh_count; // Places, as the list's
static void *mesh_3d_upload;    // A mesh's corners or indices, on their way to the GPU
static size_t mesh_3d_upload_capacity;

// Room for `bytes` in mesh_3d_upload.
static void *upload_room(const size_t bytes)
{
    if (bytes > mesh_3d_upload_capacity) {
        mesh_3d_upload = tide_realloc(mesh_3d_upload, mesh_3d_upload_capacity, bytes);
        mesh_3d_upload_capacity = bytes;
    }
    return mesh_3d_upload;
}

// A texture on the GPU: a copy of a draw list's, uploaded when a list first
// draws with it and again when its pixels changed, and let go once a frame
// draws without it. Nothing else keeps it: the pixels are the game's.
typedef struct gpu_texture {
    uint64_t id; // Whose pixels (see tide_draw_texture)
    uint64_t version;
    uint32_t space;
    uint32_t format; // TIDE_TEXELS_*
    int32_t width, height;
    uint32_t epoch; // Its list's when it copied them (see tide_draw_forget)
    tide_gpu_id texture;
    uint32_t frame; // The frame that last drew with it
} gpu_texture;

static gpu_texture *gpu_textures;
static uint32_t gpu_texture_count, gpu_texture_capacity;
static uint32_t gpu_frame;

// Render targets retain their attachments across passes and frames. Their
// owner supplies a stable identity; resizing replaces both attachments, and
// so does an image that comes to another number of pixels: the display's
// scale changed under a target that's at it, or the window's size under one
// that follows it.
typedef struct gpu_target {
    uint64_t id;
    uint64_t version;
    int width, height; // Its image's pixels
    tide_gpu_id target;
} gpu_target;

static gpu_target *gpu_targets;
static uint32_t gpu_target_count, gpu_target_capacity;

// How a pipeline reads a shader's texture `i`: a color, unless the shader says it's a depth.
static tide_gpu_read gpu_read(const tide_shader *shader, const uint32_t i)
{
    const uint8_t read = i < TIDE_SHADER_TEXTURES ? shader->texture_reads[i] : TIDE_SHADER_READ_COLOR;
    return read == TIDE_SHADER_READ_DEPTH ? TIDE_GPU_READ_DEPTH :
           read == TIDE_SHADER_READ_COMPARE ? TIDE_GPU_READ_COMPARE : TIDE_GPU_READ_COLOR;
}
static uint32_t target_epoch; // A hot reload discards the old build's target identities

static gpu_target *find_target(const uint64_t id)
{
    for (uint32_t i = 0; i < gpu_target_count; i++) {
        if (gpu_targets[i].id == id) return &gpu_targets[i];
    }
    return NULL;
}

// The GPU's image of a render target of `size`, under the draw's root. Called
// outside a pass: a newly made image is black, with its depth reset.
static gpu_target *target_for(const uint64_t id, const tide_target_size size, const uint64_t version)
{
    float logical_width, logical_height;
    int width, height;
    bool limited;
    if (!tide_render_target_live(id)
        || !target_extent(size, &root, &logical_width, &logical_height, &width, &height, &limited)) return NULL;
    gpu_target *g = find_target(id);
    if (!g) {
        if (gpu_target_count == gpu_target_capacity) {
            const uint32_t capacity = gpu_target_capacity ? gpu_target_capacity * 2u : 8u;
            gpu_targets = tide_realloc(gpu_targets, gpu_target_capacity * sizeof(gpu_target),
                                       capacity * sizeof(gpu_target));
            gpu_target_capacity = capacity;
        }
        g = &gpu_targets[gpu_target_count++];
        *g = (gpu_target){.id = id};
    }
    if (!g->target || g->width != width || g->height != height || g->version != version) {
        if (g->target) gpu->target_free(g->target);
        g->width = width, g->height = height;
        g->version = version;
        // What it holds is its id's to say: the same for as long as it's this target
        g->target = gpu->target(gpu_target_format(id), width, height);
        gpu->begin(g->target, true);
        gpu->end();
        if (limited) {
            fprintf(stderr, "tide: a render target of %g by %g is more pixels than this GPU has a side (%d): its image "
                            "is %d by %d, less sharp\n", (double)logical_width, (double)logical_height, target_most,
                    width, height);
        }
    }
    return g;
}

// Dispose invalidates every alias, even one already recorded in a list.
// A hot reload clears this cache first, before old state pointers are read.
static void forget_disposed_targets(void)
{
    uint32_t kept = 0;
    for (uint32_t i = 0; i < gpu_target_count; i++) {
        if (!tide_render_target_live(gpu_targets[i].id)) {
            gpu->target_free(gpu_targets[i].target);
            continue;
        }
        gpu_targets[kept++] = gpu_targets[i];
    }
    gpu_target_count = kept;
}

// The pipeline of a shader of the game's (tide_shader), made the first time a
// list draws through it and found again by the shader's id. That's a hash of
// all the shader is, so the same shader in another build of the game (hot
// reloading) is the same pipeline, and nothing here keeps the shader itself.
typedef struct shader_pipeline {
    uint64_t id;
    tide_gpu_id pipeline;
    uint32_t frame; // The frame that last drew through it
    uint8_t blend;  // How it goes over what's there: a pipeline for each way a shader is drawn (tide_blend)
    uint8_t depth;  // ...and for each thing a 3D one does with depth (tide_depth): 0 for a 2D one, and a blit
    uint8_t cull;   // ...and each side of a triangle it leaves out (tide_gpu_cull): none, for those too
    uint8_t state;  // ...and each thing it does with stencil (stencil_state): nothing, for a blit
    bool blit;      // The whole destination, leaving its depth alone, rather than an ordinary mesh
    // What the targets it draws into hold, which a pipeline is made for. Into depth alone, its blend is the
    // first, as none is read
    tide_gpu_target_format into;
} shader_pipeline;

static shader_pipeline *shader_pipelines;
static uint32_t shader_pipeline_count, shader_pipeline_capacity;
static uint32_t *shader_index;         // The pipelines by id: 1 + a place, or 0 for none
static uint32_t shader_index_capacity; // A power of two, or 0: at most half of it is taken
static uint32_t shader_epoch;          // The epoch of the last list that drew through them (see tide_draw_forget)

static uint32_t gpu_epoch; // The device all of the above was made on (tide_gpu.epoch)

// Whether there's a device to draw with. When it's another than last time,
// what the renderer had on the last one went with it: it forgets it, and
// makes it again as lists draw. It can: the lists keep their textures' and
// meshes' copies and their runs' shaders, and the font its atlas.
static bool gpu_ready(void)
{
    const uint32_t epoch = gpu->epoch();
    if (epoch == 0 || epoch == gpu_epoch) return epoch != 0;
    if (gpu->time_reset) gpu->time_reset();
    free(gpu_state.target_pixels);
    free(gpu_state.frame_pixels);
    memset(&gpu_state, 0, sizeof gpu_state);
    target_most = gpu->target_limit();
    if (gpu_meshes) memset(gpu_meshes, 0, gpu_mesh_count * sizeof(gpu_mesh));
    gpu_texture_count = 0;
    gpu_target_count = 0;
    engine_made_count = 0;
    shader_pipeline_count = 0;
    stencil_pipeline_count = 0;
    if (shader_index) memset(shader_index, 0, shader_index_capacity * sizeof(uint32_t));
    gpu_epoch = epoch;
    return true;
}

// A shader that shaders.h has in two pieces, as one: made once, and kept. NULL
// where it has none (WGSL is only the web's).
static const char *joined(const char *first, const char *second)
{
    static char *whole;
    if (!first || !second || whole) return whole;
    const size_t length = strlen(first);
    whole = tide_alloc(length + strlen(second) + 1u);
    memcpy(whole, first, length);
    strcpy(whole + length, second);
    return whole;
}

// Which of a 3D mesh's triangles a pipeline leaves out, for its command's cull
// (a tide_cull, with TIDE_DRAW_MIRRORED or not) under a camera that mirrors or
// doesn't. A triangle's front goes clockwise as its mesh has it, and reaches
// the GPU that way unless what it went through mirrored it an odd number of
// times, its instance's transform and the camera.
static tide_gpu_cull gpu_cull(const uint32_t cull, const bool mirrored)
{
    const uint32_t side = cull & 3u;
    if (side == TIDE_CULL_NONE) return TIDE_GPU_CULL_NONE;
    const bool back = side != TIDE_CULL_FRONT;
    return back != (((cull & TIDE_DRAW_MIRRORED) != 0) != mirrored) ? TIDE_GPU_CULL_BACK : TIDE_GPU_CULL_FRONT;
}

// One of the engine's pipelines, for one way of blending, one thing done with
// stencil (`state`: stencil_state) and, for the 3D meshes', one thing done
// with depth and one side left out, into targets that hold what `into` says:
// made if there's none. Into targets of depth alone, where a color has nowhere
// to go, it's the 3D meshes' that write depth, and whatever stores stencil.
static tide_gpu_id engine_pipeline(const int kind, uint32_t blend, uint32_t depth, uint32_t cull,
                                   const tide_gpu_target_format into, const uint32_t state)
{
    static const tide_gpu_blend blends[BLENDS] = {
        [TIDE_BLEND_ALPHA] = TIDE_GPU_BLEND_ALPHA, [TIDE_BLEND_ADD] = TIDE_GPU_BLEND_ADD,
        [TIDE_BLEND_MULTIPLY] = TIDE_GPU_BLEND_MULTIPLY, [TIDE_BLEND_REPLACE] = TIDE_GPU_BLEND_REPLACE};
    const bool bare = into == TIDE_GPU_TARGET_DEPTH;
    if (blend >= BLENDS || bare) blend = TIDE_BLEND_ALPHA; // None is read where there's no color
    if (kind != PIPELINE_MESHES_3D || depth >= DEPTHS) depth = TIDE_DEPTH_READ_WRITE;
    if (kind != PIPELINE_MESHES_3D || cull >= CULLS) cull = TIDE_GPU_CULL_NONE; // Nothing in 2D has a back
    tide_gpu_id *made;
    if (state) { // One that tests or stores stencil: among the few of those
        made = NULL;
        for (uint32_t i = 0; i < stencil_pipeline_count && !made; i++) {
            stencil_pipeline *p = &stencil_pipelines[i];
            if (p->kind == kind && p->blend == blend && p->depth == depth && p->cull == cull && p->state == state
                && p->into == into) {
                made = &p->pipeline;
            }
        }
        if (!made) {
            if (stencil_pipeline_count == stencil_pipeline_capacity) {
                const uint32_t capacity = stencil_pipeline_capacity ? stencil_pipeline_capacity * 2u : 16u;
                stencil_pipelines = tide_realloc(stencil_pipelines, stencil_pipeline_capacity * sizeof(stencil_pipeline),
                                                 capacity * sizeof(stencil_pipeline));
                stencil_pipeline_capacity = capacity;
            }
            stencil_pipelines[stencil_pipeline_count] = (stencil_pipeline){
                (uint8_t)kind, (uint8_t)blend, (uint8_t)depth, (uint8_t)cull, (uint8_t)state, into, 0};
            made = &stencil_pipelines[stencil_pipeline_count++].pipeline;
        }
    } else {
        made = &pipelines_into(into)->made[kind][blend][depth][cull];
    }
    if (*made) return *made;
    tide_gpu_pipeline_desc desc;
    switch (kind) {
    case PIPELINE_SHAPES:
        desc = (tide_gpu_pipeline_desc){
            .name = "shapes",
            .glsl_vertex = shape_glsl_vertex,
            .glsl_fragment = shape_glsl_fragment,
            .wgsl = shape_wgsl,
            .slots = {{2 * sizeof(float), false}, {sizeof(shape), true}}, // The quad's corners, and an instance a shape
            .slot_count = 2,
            .attributes = {{0, TIDE_GPU_FLOAT2, 0},
                           {1, TIDE_GPU_FLOAT4, offsetof(shape, a)},
                           {1, TIDE_GPU_BYTES4, offsetof(shape, color)},
                           {1, TIDE_GPU_FLOAT, offsetof(shape, kind)}},
            .attribute_count = 4,
            .uniform_size = 16 * sizeof(float),
        };
        break;
    case PIPELINE_SMOOTH_SHAPES:
        desc = (tide_gpu_pipeline_desc){
            .name = "smooth shapes",
            .glsl_vertex = smooth_shape_glsl_vertex,
            .glsl_fragment = smooth_shape_glsl_fragment,
            .wgsl = joined(smooth_shape_wgsl_vertex, smooth_shape_wgsl_fragment),
            // The quad's corners, an instance a shape, and an extra for each
            .slots = {{2 * sizeof(float), false}, {sizeof(shape), true}, {sizeof(shape_extra), true}},
            .slot_count = 3,
            .attributes = {{0, TIDE_GPU_FLOAT2, 0},
                           {1, TIDE_GPU_FLOAT4, offsetof(shape, a)},
                           {1, TIDE_GPU_BYTES4, offsetof(shape, color)},
                           {1, TIDE_GPU_FLOAT, offsetof(shape, kind)},
                           {2, TIDE_GPU_FLOAT4, offsetof(shape_extra, turn)},
                           {2, TIDE_GPU_FLOAT2, offsetof(shape_extra, soft)},
                           {2, TIDE_GPU_BYTES4, offsetof(shape_extra, color2)},
                           {2, TIDE_GPU_BYTES4, offsetof(shape_extra, rim)}},
            .attribute_count = 8,
            .uniform_size = sizeof(screen_uniforms),
        };
        break;
    case PIPELINE_MESHES:
    case PIPELINE_TEXT: // A mesh's corners, with pixels of its own
        desc = (tide_gpu_pipeline_desc){
            .name = kind == PIPELINE_TEXT ? "text" : "meshes",
            .glsl_vertex = mesh_glsl_vertex,
            .glsl_fragment = kind == PIPELINE_TEXT ? text_glsl_fragment : mesh_glsl_fragment,
            .wgsl = kind == PIPELINE_TEXT ? text_wgsl : mesh_wgsl,
            .slots = {{sizeof(mesh_vertex), false}},
            .slot_count = 1,
            .attributes = {{0, TIDE_GPU_FLOAT2, offsetof(mesh_vertex, position)},
                           {0, TIDE_GPU_FLOAT2, offsetof(mesh_vertex, uv)},
                           {0, TIDE_GPU_BYTES4, offsetof(mesh_vertex, color)}},
            .attribute_count = 3,
            .uniform_size = sizeof(screen_uniforms),
            .texture_count = 1,
        };
        break;
    default:
        desc = (tide_gpu_pipeline_desc){
            .name = "3D meshes",
            .glsl_vertex = mesh_3d_glsl_vertex,
            .glsl_fragment = mesh_glsl_fragment,
            .wgsl = mesh_3d_wgsl,
            .slots = {{sizeof(mesh_3d_vertex), false}, {sizeof(tide_float4x4), true}}, // Its corners, and an instance a transform
            .slot_count = 2,
            .attributes = {{0, TIDE_GPU_FLOAT3, offsetof(mesh_3d_vertex, position)},
                           {0, TIDE_GPU_FLOAT2, offsetof(mesh_3d_vertex, uv)},
                           {0, TIDE_GPU_BYTES4, offsetof(mesh_3d_vertex, color)},
                           {1, TIDE_GPU_FLOAT4, 0 * sizeof(tide_float4)},
                           {1, TIDE_GPU_FLOAT4, 1 * sizeof(tide_float4)},
                           {1, TIDE_GPU_FLOAT4, 2 * sizeof(tide_float4)},
                           {1, TIDE_GPU_FLOAT4, 3 * sizeof(tide_float4)}},
            .attribute_count = 7,
            .uniform_size = sizeof(mesh_3d_uniforms),
            .texture_count = 1,
            .depth = gpu_depths[depth],
            .cull = (tide_gpu_cull)cull,
        };
        break;
    }
    desc.blend = blends[blend];
    desc.stencil_test = (tide_gpu_stencil_test)(state & 15u);
    desc.stencil_op = (tide_gpu_stencil_op)(state >> 4);
    desc.into = into;
    desc.outputs = 1; // The one color they have: a target's first image, whatever else it holds
    if (bare && kind == PIPELINE_MESHES_3D) desc.name = "3D meshes into depth alone";
    return *made = gpu->pipeline(&desc);
}

// A clear, in a version for each kind of target: a triangle over it, which
// sets depth back and stencil to 0, and fills each color image it has with
// the clear's color.
static tide_gpu_id clear_pipeline(const tide_gpu_target_format into)
{
    tide_gpu_id *made = &pipelines_into(into)->clear;
    if (*made) return *made;
    const bool several = tide_gpu_target_colors(into) > 1;
    return *made = gpu->pipeline(&(tide_gpu_pipeline_desc){
        .name = into == TIDE_GPU_TARGET_DEPTH ? "clears of depth alone" : several ? "clears of several colors" : "clears",
        .glsl_vertex = clear_glsl_vertex,
        .glsl_fragment = several ? clear_all_glsl_fragment : clear_glsl_fragment,
        .wgsl = several ? clear_all_wgsl : clear_wgsl,
        .uniform_size = 4 * sizeof(float),
        .blend = TIDE_GPU_BLEND_REPLACE,
        .depth = TIDE_GPU_DEPTH_RESET,
        .stencil_op = TIDE_GPU_STENCIL_ZERO,
        .into = into,
        .outputs = several ? TIDE_GPU_MAX_COLORS : 1u,
    });
}

// The window's clears' pipeline, the quad and the white pixel, the first time a list is drawn.
static void renderer_start(void)
{
    if (gpu_state.corners) return;
    clear_pipeline(TIDE_GPU_TARGET_COLOR);
    static const float corners[12] = {0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 0};
    gpu_state.corners = gpu->buffer(TIDE_GPU_VERTICES, sizeof corners, corners);
    gpu_state.white = gpu->texture(TIDE_GPU_RGBA8, 1, 1, (const uint8_t[4]){255, 255, 255, 255});
}

// Whether a step leaves anything in a target of depth alone, where a color
// has nowhere to go: a clear, which sets depth and stencil back, a 3D mesh
// that writes depth, through a shader of the game's or not, and whatever
// draws and stores stencil. Everything else only draws colors, which such a
// target has none of, and is passed by there.
static bool writes_depth(const tide_draw_list *list, const draw_step *s)
{
    switch (s->kind) {
    case STEP_BLIT: return false;
    case STEP_SHAPES:
    case STEP_TEXT:
    case STEP_MESH: return stencil_stores(s->stencil);
    case STEP_MESH_3D: return s->depth == TIDE_DEPTH_READ_WRITE || stencil_stores(s->stencil);
    case STEP_SHADED:
        return (list->runs[s->first].shader->dims == 3 && s->depth == TIDE_DEPTH_READ_WRITE) || stencil_stores(s->stencil);
    default: return true; // A clear, and what only says where the steps after it go
    }
}

// Whether a step goes into a target of depth alone, and leaves nothing there.
static bool leaves_nothing(const tide_draw_list *list, const draw_step *s)
{
    return frame_targets[s->target].bare && !writes_depth(list, s);
}

// The pipeline each step of the engine's own draws through, before anything draws.
static void find_pipelines(const tide_draw_list *list)
{
    for (uint32_t i = 0; i < step_count; i++) {
        draw_step *s = &steps[i];
        if (!frame_targets[s->target].valid || leaves_nothing(list, s)) continue;
        const tide_gpu_target_format into = frame_targets[s->target].into;
        const uint32_t state = stencil_state(s->stencil);
        switch (s->kind) {
        case STEP_SHAPES:
            s->pipeline = engine_pipeline(s->smooth ? PIPELINE_SMOOTH_SHAPES : PIPELINE_SHAPES, s->blend, 0, 0, into, state);
            break;
        case STEP_MESH: s->pipeline = engine_pipeline(PIPELINE_MESHES, s->blend, 0, 0, into, state); break;
        case STEP_TEXT: s->pipeline = engine_pipeline(PIPELINE_TEXT, s->blend, 0, 0, into, state); break;
        case STEP_MESH_3D:
            s->pipeline = engine_pipeline(PIPELINE_MESHES_3D, s->blend, s->depth, s->cull, into, state);
            break;
        case STEP_CLEAR: s->pipeline = clear_pipeline(into); break;
        default: break;
        }
    }
}

// The GPU's copy of the list's 3D mesh `mesh`, uploaded if it hasn't got it.
static gpu_mesh *mesh_for(const tide_draw_list *list, const uint32_t mesh)
{
    if (list->mesh_count > gpu_mesh_count) {
        gpu_meshes = tide_realloc(gpu_meshes, gpu_mesh_count * sizeof(gpu_mesh), list->mesh_count * sizeof(gpu_mesh));
        memset(gpu_meshes + gpu_mesh_count, 0, (list->mesh_count - gpu_mesh_count) * sizeof(gpu_mesh));
        gpu_mesh_count = list->mesh_count;
    }
    const tide_draw_mesh_data *m = &list->meshes[mesh - 1u];
    gpu_mesh *g = &gpu_meshes[mesh - 1u];
    g->frame = gpu_frame;
    if (g->vertices && g->id == m->id && g->other == m->other && g->version == m->version && g->space == m->space
        && g->epoch == m->epoch) {
        return g;
    }
    // Its corners, each once: as they are for a shader of the game's, which reads their bytes
    const void *corners = m->bytes;
    size_t vertex_bytes = (size_t)m->vertex_count * m->vertex_size;
    if (m->space != TIDE_MESH_BYTES) {
        vertex_bytes = (size_t)m->vertex_count * sizeof(mesh_3d_vertex);
        mesh_3d_vertex *packed = upload_room(vertex_bytes);
        for (uint32_t i = 0; i < m->vertex_count; i++) {
            const tide_vertex3 *v = &m->vertices[i];
            const rgba color = to_rgba(v->color);
            packed[i] = (mesh_3d_vertex){{v->position.x, v->position.y, v->position.z}, {v->uv.x, v->uv.y},
                                         {color.r, color.g, color.b, color.a}};
        }
        corners = packed;
    }
    if (g->vertices && vertex_bytes <= g->vertex_capacity) {
        gpu->buffer_write(g->vertices, 0, corners, vertex_bytes);
    } else {
        if (g->vertices) gpu->buffer_free(g->vertices);
        g->vertices = gpu->buffer(TIDE_GPU_VERTICES, vertex_bytes, corners);
        g->vertex_capacity = (uint32_t)vertex_bytes;
    }
    // ...and its triangles, in 16 bits where they fit, to a multiple of 4 bytes, as buffers take them
    const bool wide = m->vertex_count > 65536u;
    const size_t index_bytes = ((size_t)m->index_count * (wide ? sizeof(uint32_t) : sizeof(uint16_t)) + 3u) & ~(size_t)3u;
    const void *indices = m->indices;
    if (!wide) {
        uint16_t *narrow = upload_room(index_bytes);
        memset(narrow, 0, index_bytes);
        for (uint32_t i = 0; i < m->index_count; i++) narrow[i] = (uint16_t)m->indices[i];
        indices = narrow;
    }
    if (g->indices && index_bytes <= g->index_capacity) {
        gpu->buffer_write(g->indices, 0, indices, index_bytes);
    } else {
        if (g->indices) gpu->buffer_free(g->indices);
        g->indices = gpu->buffer(TIDE_GPU_INDICES, index_bytes, indices);
        g->index_capacity = (uint32_t)index_bytes;
    }
    g->id = m->id;
    g->other = m->other;
    g->version = m->version;
    g->space = m->space;
    g->epoch = m->epoch;
    g->index_count = m->index_count;
    g->wide = wide;
    return g;
}

// A buffer for `count` elements of `size` bytes, in place of `*buffer` when
// it has no room for them: its capacity doubles until it does.
static void buffer_room(tide_gpu_id *buffer, uint32_t *capacity, const uint32_t count, const size_t size,
                        const uint32_t at_least)
{
    if (count <= *capacity) return;
    uint32_t grown = *capacity ? *capacity : at_least;
    while (grown < count) grown *= 2u;
    if (*buffer) gpu->buffer_free(*buffer);
    *buffer = gpu->buffer(TIDE_GPU_VERTICES, (size_t)grown * size, NULL);
    *capacity = grown;
}

// The list's matrices into the instance buffer, which grows as they need,
// and its 3D meshes onto the GPU, before anything draws.
static void upload_meshes_3d(const tide_draw_list *list)
{
    bool any = false;
    for (uint32_t i = 0; i < step_count; i++) {
        if (steps[i].kind != STEP_MESH_3D) continue;
        any = true;
        mesh_for(list, steps[i].mesh);
    }
    if (!any) return;
    buffer_room(&gpu_state.matrices, &gpu_state.matrix_capacity, list->matrix_count, sizeof(tide_float4x4), 1024u);
    gpu->buffer_write(gpu_state.matrices, 0, list->matrices, (size_t)list->matrix_count * sizeof(tide_float4x4));
}

// Lets go of the 3D meshes the frame didn't draw.
static void forget_meshes_3d(void)
{
    for (uint32_t i = 0; i < gpu_mesh_count; i++) {
        if (gpu_meshes[i].vertices && gpu_meshes[i].frame != gpu_frame) {
            gpu->buffer_free(gpu_meshes[i].vertices);
            gpu->buffer_free(gpu_meshes[i].indices);
            gpu_meshes[i] = (gpu_mesh){0};
        }
    }
}

// Where the pipeline of the shader `id`, blending as `blend`, doing `depth` with depth and `state`
// with stencil and leaving `cull` out, into targets that hold what `into` says, is in the index, or
// would go.
static uint32_t shader_slot(const uint64_t id, const bool blit, const uint8_t blend, const uint8_t depth,
                            const uint8_t cull, const uint8_t state, const tide_gpu_target_format into)
{
    const uint32_t mask = shader_index_capacity - 1u;
    uint32_t i = (uint32_t)(id ^ id >> 32 ^ (blit ? 0x9e3779b9u : 0u) ^ blend * 0x85ebca6bu ^ depth * 0xc2b2ae35u
                            ^ cull * 0x27d4eb2fu ^ state * 0x2545f491u ^ into * 0x165667b1u) & mask;
    while (shader_index[i]) {
        const shader_pipeline *p = &shader_pipelines[shader_index[i] - 1u];
        if (p->id == id && p->blit == blit && p->blend == blend && p->depth == depth && p->cull == cull
            && p->state == state && p->into == into) {
            break;
        }
        i = (i + 1u) & mask;
    }
    return i;
}

// The index again, from the pipelines there are, with room for another.
static void index_shaders(void)
{
    uint32_t capacity = shader_index_capacity ? shader_index_capacity : 64u;
    while (capacity / 2u <= shader_pipeline_count) capacity *= 2u;
    if (capacity != shader_index_capacity) {
        free(shader_index);
        shader_index = tide_alloc_zeroed(capacity, sizeof(uint32_t));
        shader_index_capacity = capacity;
    } else {
        memset(shader_index, 0, capacity * sizeof(uint32_t));
    }
    for (uint32_t i = 0; i < shader_pipeline_count; i++) {
        const shader_pipeline *p = &shader_pipelines[i];
        shader_index[shader_slot(p->id, p->blit, p->blend, p->depth, p->cull, p->state, p->into)] = i + 1u;
    }
}

// The pipeline of a shader of the game's for one way of blending, one thing
// done with stencil (`state`: stencil_state) and, for a 3D one, one thing done
// with depth and one side left out, made if there's none: it reads a mesh's
// vertices as they are from the first slot, and from the second an instance's
// bytes, a float4 or an int4 an attribute. It's for targets that hold what
// `into` says, whose color images its frag's outputs go to in order: one with
// no image in its place goes nowhere (into depth alone, that's all of them;
// the frag still runs, as it may discard the pixel), and the images past them
// are left as they were.
static tide_gpu_id pipeline_for(const tide_shader *shader, const bool blit, uint32_t blend, uint32_t depth,
                                uint32_t cull, const tide_gpu_target_format into, uint32_t state)
{
    static const tide_gpu_blend blends[BLENDS] = {
        [TIDE_BLEND_ALPHA] = TIDE_GPU_BLEND_ALPHA, [TIDE_BLEND_ADD] = TIDE_GPU_BLEND_ADD,
        [TIDE_BLEND_MULTIPLY] = TIDE_GPU_BLEND_MULTIPLY, [TIDE_BLEND_REPLACE] = TIDE_GPU_BLEND_REPLACE};
    if (blend >= BLENDS) blend = blit ? TIDE_BLEND_REPLACE : TIDE_BLEND_ALPHA;
    if (into == TIDE_GPU_TARGET_DEPTH) blend = TIDE_BLEND_ALPHA; // None is read
    // Only a 3D mesh has depth: a 2D one draws over what's there, and a blit leaves its destination's alone
    const bool deep = !blit && shader->dims == 3;
    if (!deep || depth >= DEPTHS) depth = TIDE_DEPTH_READ_WRITE;
    if (!deep || cull >= CULLS) cull = TIDE_GPU_CULL_NONE; // ...and only a 3D mesh's triangles have a back
    if (blit) state = 0; // A blit neither tests stencil nor stores any
    if (shader_pipeline_count >= shader_index_capacity / 2u) index_shaders();
    const uint32_t slot = shader_slot(shader->id, blit, (uint8_t)blend, (uint8_t)depth, (uint8_t)cull, (uint8_t)state, into);
    if (shader_index[slot]) {
        shader_pipeline *p = &shader_pipelines[shader_index[slot] - 1u];
        p->frame = gpu_frame;
        return p->pipeline;
    }
    static const tide_gpu_attribute_format formats[] = {
        [TIDE_SHADER_FLOAT] = TIDE_GPU_FLOAT,   [TIDE_SHADER_FLOAT2] = TIDE_GPU_FLOAT2,
        [TIDE_SHADER_FLOAT3] = TIDE_GPU_FLOAT3, [TIDE_SHADER_FLOAT4] = TIDE_GPU_FLOAT4,
        [TIDE_SHADER_INT] = TIDE_GPU_INT,       [TIDE_SHADER_INT2] = TIDE_GPU_INT2,
        [TIDE_SHADER_INT3] = TIDE_GPU_INT3,     [TIDE_SHADER_INT4] = TIDE_GPU_INT4,
    };
    const uint32_t instance_size = tide_shader_instance_size(shader);
    tide_gpu_pipeline_desc desc = {
        .name = shader->name ? shader->name : "a shader",
        .glsl_vertex = shader->glsl_vertex,
        .glsl_fragment = shader->glsl_fragment,
        .wgsl = shader->wgsl,
        .slots = {{shader->vertex_size, false}, {instance_size, true}},
        .slot_count = instance_size ? 2u : 1u,
        .uniform_size = (uint32_t)sizeof(shaded_uniforms) + tide_shader_uniform_size(shader),
        .texture_count = shader->texture_count,
        .blend = blends[blend],
        .depth = deep ? gpu_depths[depth] : TIDE_GPU_DEPTH_NONE,
        .cull = (tide_gpu_cull)cull,
        .stencil_test = (tide_gpu_stencil_test)(state & 15u),
        .stencil_op = (tide_gpu_stencil_op)(state >> 4),
        .into = into,
        .outputs = tide_shader_outputs(shader),
    };
    for (uint32_t i = 0; i < shader->texture_count && i < TIDE_GPU_MAX_TEXTURES; i++) desc.reads[i] = gpu_read(shader, i);
    for (uint32_t i = 0; i < shader->attribute_count; i++) {
        desc.attributes[i].slot = 0;
        desc.attributes[i].format = formats[shader->attributes[i].format];
        desc.attributes[i].offset = shader->attributes[i].offset;
    }
    desc.attribute_count = shader->attribute_count;
    for (uint32_t i = 0; i < shader->instance_floats + shader->instance_ints; i++) {
        const uint32_t at = desc.attribute_count++;
        desc.attributes[at].slot = 1;
        desc.attributes[at].format = i < shader->instance_floats ? TIDE_GPU_FLOAT4 : TIDE_GPU_INT4;
        desc.attributes[at].offset = i * 16u;
    }
    if (shader_pipeline_count == shader_pipeline_capacity) {
        const uint32_t capacity = shader_pipeline_capacity ? shader_pipeline_capacity * 2u : 16u;
        shader_pipelines = tide_realloc(shader_pipelines, shader_pipeline_capacity * sizeof(shader_pipeline),
                                        capacity * sizeof(shader_pipeline));
        shader_pipeline_capacity = capacity;
    }
    const tide_gpu_id pipeline = gpu->pipeline(&desc);
    shader_pipelines[shader_pipeline_count] = (shader_pipeline){shader->id, pipeline, gpu_frame, (uint8_t)blend,
                                                                (uint8_t)depth, (uint8_t)cull, (uint8_t)state, blit, into};
    shader_index[slot] = ++shader_pipeline_count;
    return pipeline;
}

// Lets go of the pipelines the frame didn't draw through: for the first frame
// of another build of the game (see tide_draw_forget), whose shaders that
// didn't change keep theirs.
static void forget_pipelines(void)
{
    uint32_t kept = 0;
    for (uint32_t i = 0; i < shader_pipeline_count; i++) {
        if (shader_pipelines[i].frame != gpu_frame) {
            gpu->pipeline_free(shader_pipelines[i].pipeline);
            continue;
        }
        shader_pipelines[kept++] = shader_pipelines[i];
    }
    if (kept == shader_pipeline_count) return;
    shader_pipeline_count = kept;
    index_shaders();
}

// Whether the list's run `place` can draw: it wasn't forgotten (see
// tide_draw_forget), its shader takes no more than a pipeline does, its mesh
// is of the shader's vertices, and its instances and uniforms are all there.
static bool run_draws(const tide_draw_list *list, const uint32_t place)
{
    if (place >= list->run_count) return false;
    const tide_draw_run *run = &list->runs[place];
    const tide_shader *shader = run->shader;
    if (!shader || run->mesh == 0 || run->mesh > list->mesh_count || run->count == 0) return false;
    const tide_draw_mesh_data *m = &list->meshes[run->mesh - 1u];
    if (m->space != TIDE_MESH_BYTES || m->index_count == 0 || m->vertex_size != shader->vertex_size) return false;
    const uint64_t instance_fields = (uint64_t)shader->instance_floats + shader->instance_ints;
    const uint64_t uniform_fields = (uint64_t)shader->uniform_floats + shader->uniform_ints;
    if (shader->attribute_count + instance_fields > TIDE_SHADER_ATTRIBUTES
        || shader->texture_count > TIDE_SHADER_TEXTURES || uniform_fields * 16u > TIDE_SHADER_UNIFORM_BYTES) {
        return false;
    }
    for (uint32_t i = 0; i < shader->attribute_count; i++) {
        if (shader->attributes[i].format > TIDE_SHADER_INT4) return false;
    }
    return run->first + instance_fields * 16u * run->count <= list->instance_used
           && run->uniforms + uniform_fields * 16u <= list->uniform_used;
}

// What the list's shaded runs draw from goes to the GPU, before anything
// draws: their meshes, their instances into the instance buffer, which grows
// as they need, and a pipeline for each shader that has none.
static bool upload_shaded(const tide_draw_list *list)
{
    bool any = false;
    for (uint32_t i = 0; i < step_count; i++) {
        if (steps[i].kind != STEP_SHADED && steps[i].kind != STEP_BLIT) continue;
        if (!frame_targets[steps[i].target].valid || leaves_nothing(list, &steps[i])) continue;
        const tide_draw_run *run = &list->runs[steps[i].first];
        any = true;
        mesh_for(list, run->mesh);
        steps[i].pipeline = pipeline_for(run->shader, steps[i].kind == STEP_BLIT, steps[i].blend, steps[i].depth,
                                         steps[i].cull, frame_targets[steps[i].target].into,
                                         stencil_state(steps[i].stencil));
        // A shader that reads a depth it may not be given reads the far one: made here, outside any pass
        for (uint32_t t = 0; t < run->shader->texture_count && !gpu_state.far; t++) {
            if (gpu_read(run->shader, t) == TIDE_GPU_READ_COLOR) continue;
            gpu_state.far = gpu->target(TIDE_GPU_TARGET_DEPTH, 1, 1);
            gpu->begin(gpu_state.far, true);
            gpu->end();
        }
    }
    if (any && list->instance_used) {
        buffer_room(&gpu_state.instances, &gpu_state.instance_capacity, list->instance_used, 1, 4096u);
        gpu->buffer_write(gpu_state.instances, 0, list->instances, list->instance_used);
    }
    return any;
}

// This list's shapes into the instance buffer, and its triangles, text's too,
// into theirs: both grow as they need.
static void upload_shapes_and_meshes(void)
{
    if (shape_count) {
        buffer_room(&gpu_state.shape_instances, &gpu_state.shape_capacity, shape_count, sizeof(shape), 1024u);
        gpu->buffer_write(gpu_state.shape_instances, 0, shapes, (size_t)shape_count * sizeof(shape));
    }
    if (shape_extra_count) {
        buffer_room(&gpu_state.shape_extras, &gpu_state.shape_extra_capacity, shape_extra_count, sizeof(shape_extra),
                    256u);
        gpu->buffer_write(gpu_state.shape_extras, 0, shape_extras, (size_t)shape_extra_count * sizeof(shape_extra));
    }
    if (mesh_vertex_count) {
        buffer_room(&gpu_state.mesh_vertices, &gpu_state.mesh_capacity, mesh_vertex_count, sizeof(mesh_vertex), 4096u);
        gpu->buffer_write(gpu_state.mesh_vertices, 0, mesh_vertices, (size_t)mesh_vertex_count * sizeof(mesh_vertex));
    }
}

// The font atlas onto the GPU, as the list's text left it.
static void upload_atlas(void)
{
    int width = 0, height = 0;
    uint32_t version = 0;
    const uint8_t *pixels = tide_font_atlas(&width, &height, &version);
    if (!pixels || (gpu_state.atlas && gpu_state.atlas_version == version)) return;
    if (gpu_state.atlas && width == gpu_state.atlas_width && height == gpu_state.atlas_height) {
        gpu->texture_write(gpu_state.atlas, pixels);
    } else {
        if (gpu_state.atlas) gpu->texture_free(gpu_state.atlas);
        gpu_state.atlas = gpu->texture(TIDE_GPU_R8, width, height, pixels);
    }
    gpu_state.atlas_width = width;
    gpu_state.atlas_height = height;
    gpu_state.atlas_version = version;
}

// The GPU's copy of a list's texture, uploaded if it hasn't got these pixels.
static gpu_texture *texture_for(const tide_draw_texture *t)
{
    gpu_texture *g = NULL;
    for (uint32_t i = 0; i < gpu_texture_count && !g; i++) {
        if (gpu_textures[i].id == t->id && gpu_textures[i].space == t->space) g = &gpu_textures[i];
    }
    if (!g) {
        if (gpu_texture_count == gpu_texture_capacity) {
            const uint32_t capacity = gpu_texture_capacity ? gpu_texture_capacity * 2u : 8u;
            gpu_textures = tide_realloc(gpu_textures, gpu_texture_capacity * sizeof(gpu_texture),
                                        capacity * sizeof(gpu_texture));
            gpu_texture_capacity = capacity;
        }
        g = &gpu_textures[gpu_texture_count++];
        *g = (gpu_texture){.id = t->id, .space = t->space};
    }
    g->frame = gpu_frame;
    if (g->texture && g->version == t->version && g->width == t->width && g->height == t->height
        && g->format == t->format && g->epoch == t->epoch) {
        return g;
    }
    if (g->texture && g->width == t->width && g->height == t->height && g->format == t->format) {
        gpu->texture_write(g->texture, t->pixels);
    } else {
        if (g->texture) gpu->texture_free(g->texture);
        // A grid of floats' cells are one float of 16 bits a pixel
        g->texture = gpu->texture(t->format == TIDE_TEXELS_HALF ? TIDE_GPU_R16F : TIDE_GPU_RGBA8, t->width, t->height, t->pixels);
    }
    g->format = t->format;
    g->version = t->version;
    g->width = t->width;
    g->height = t->height;
    g->epoch = t->epoch;
    return g;
}

// Lets go of the textures the frame didn't draw with.
static void forget_textures(void)
{
    uint32_t kept = 0;
    for (uint32_t i = 0; i < gpu_texture_count; i++) {
        if (gpu_textures[i].frame != gpu_frame) {
            if (gpu_textures[i].texture) gpu->texture_free(gpu_textures[i].texture);
            continue;
        }
        gpu_textures[kept++] = gpu_textures[i];
    }
    gpu_texture_count = kept;
    gpu_frame++;
}

// Whether a list's texture is a render target's image: its color, or its depth.
static bool of_target(const tide_draw_texture *t)
{
    return t->space == TIDE_PIXELS_TARGET || t->space == TIDE_PIXELS_TARGET_DEPTH;
}

// The GPU's copy of the list's texture `texture` (1 + its place), for a
// shader that reads it as `read` says (tide_gpu_read). For none, and for one
// that isn't what the shader reads there (a color where it reads a depth, or
// the other way round, as a list filled by hand may say), it's the white
// pixel where a color is read, and where a depth is, the far one.
static tide_gpu_id texture_to_read(const tide_draw_list *list, const uint32_t texture, const tide_gpu_read read)
{
    const bool depth = read != TIDE_GPU_READ_COLOR;
    const tide_gpu_id none = depth ? gpu->target_depth(gpu_state.far) : gpu_state.white;
    if (texture == 0 || texture > list->texture_count) return none;
    const tide_draw_texture *t = &list->textures[texture - 1u];
    if (depth != (t->space == TIDE_PIXELS_TARGET_DEPTH)) return none;
    if (of_target(t)) { // Its depth, or the color image its id names
        const gpu_target *g = find_target(tide_render_target_whole(t->id));
        const tide_gpu_id image = !g || !g->target ? 0 : depth ? gpu->target_depth(g->target)
                                : gpu->target_texture(g->target, tide_render_target_image(t->id));
        return image ? image : none;
    }
    const gpu_texture *g = texture_for(t);
    return g->texture ? g->texture : none;
}

// Reading an attachment while writing it is undefined on every GPU, a
// target's depth as much as any of its color images, whichever of them the
// draw writes. Refuse the whole draw, including an ordinary mesh or an
// indirect binding.
static bool samples_destination(const tide_draw_list *list, const draw_step *s)
{
    if (!s->target) return false;
    const uint64_t id = frame_targets[s->target].id;
    if (s->kind == STEP_SHADED || s->kind == STEP_BLIT) {
        const tide_draw_run *run = &list->runs[s->first];
        for (uint32_t i = 0; i < run->shader->texture_count; i++) {
            const uint32_t at = run->textures[i];
            if (at && at <= list->texture_count) {
                const tide_draw_texture *t = &list->textures[at - 1u];
                if (of_target(t) && tide_render_target_whole(t->id) == id) return true;
            }
        }
    } else if (s->texture && s->texture <= list->texture_count) {
        const tide_draw_texture *t = &list->textures[s->texture - 1u];
        if (of_target(t) && tide_render_target_whole(t->id) == id) return true;
    }
    return false;
}

// Why a list's texture can't be sampled, or NULL: a target that was disposed
// or has no image, the color of one that holds depth alone, and a color image
// past the ones a target holds.
static const char *unavailable_texture(const tide_draw_list *list, const uint32_t texture)
{
    if (!texture || texture > list->texture_count) return NULL;
    const tide_draw_texture *t = &list->textures[texture - 1u];
    if (!of_target(t)) return NULL;
    if (!tide_render_target_live(t->id) || !tide_target_size_valid(t->target)) return "source render target is invalid or disposed";
    if (t->space == TIDE_PIXELS_TARGET_DEPTH) {
        // By what it asked for, whatever this device gave it: the same draws are passed by everywhere
        return tide_render_target_samples(t->id) > 1u
            ? "source render target is multisampled: its depth has several samples a pixel, which no shader reads" : NULL;
    }
    const uint32_t colors = tide_render_target_colors(t->id);
    if (colors == 0) return "source render target holds depth alone: it has no color to sample";
    if (tide_render_target_image(t->id) >= colors) {
        return "source render target has fewer color images than the one sampled: its 'colors' are counted from 0";
    }
    return NULL;
}

static const char *samples_unavailable(const tide_draw_list *list, const draw_step *s)
{
    if (s->kind == STEP_SHADED || s->kind == STEP_BLIT) {
        const tide_draw_run *run = &list->runs[s->first];
        for (uint32_t i = 0; i < run->shader->texture_count; i++) {
            const char *why = unavailable_texture(list, run->textures[i]);
            if (why) return why;
        }
        return NULL;
    }
    return unavailable_texture(list, s->texture);
}

// A source recorded after Resize first sees the replacement black image,
// even when no command draws into it. An older source still aliases the
// current attachment; it never rolls a target back to an earlier revision.
// A source at the revision there is, whose image now comes to another number
// of pixels (the display's scale changed, or the window's size under a target
// that follows it), sees the image made again, black, as a destination would.
static bool prepare_source_target(const tide_draw_list *list, const uint32_t texture, const bool check)
{
    if (!texture || texture > list->texture_count) return false;
    const tide_draw_texture *t = &list->textures[texture - 1u];
    if (!of_target(t)) return false;
    const uint64_t id = tide_render_target_whole(t->id); // The target, whichever of its images is sampled
    const gpu_target *g = find_target(id);
    if (g && (!tide_render_target_managed(id) || t->version < g->version)) return false;
    float width, height;
    int wide, high;
    bool limited;
    if (!target_extent(t->target, &root, &width, &height, &wide, &high, &limited)) return false;
    if (g && g->target && t->version == g->version && g->width == wide && g->height == high) return false;
    if (!check) target_for(id, t->target, t->version);
    return true;
}

static bool prepare_source_targets(const tide_draw_list *list, const draw_step *s, const bool check)
{
    bool needed = false;
    if (s->kind == STEP_SHADED || s->kind == STEP_BLIT) {
        const tide_draw_run *run = &list->runs[s->first];
        for (uint32_t i = 0; i < run->shader->texture_count; i++) {
            needed |= prepare_source_target(list, run->textures[i], check);
        }
    } else {
        needed = prepare_source_target(list, s->texture, check);
    }
    return needed;
}

// A list's wrap as the GPU's, which has tide_wrap's numbers: what isn't one clamps.
static tide_gpu_wrap gpu_wrap(const uint32_t wrap)
{
    _Static_assert(TIDE_GPU_WRAP_REPEAT == (int)TIDE_WRAP_REPEAT && TIDE_GPU_WRAP_MIRROR == (int)TIDE_WRAP_MIRROR,
                   "gpu.h's wraps are draw.h's");
    return wrap == TIDE_WRAP_REPEAT || wrap == TIDE_WRAP_MIRROR ? (tide_gpu_wrap)wrap : TIDE_GPU_WRAP_CLAMP;
}

// The texture a mesh step samples, sampled as the step says.
static void sample_step_texture(const tide_draw_list *list, const draw_step *s)
{
    gpu->sample(0, texture_to_read(list, s->texture, TIDE_GPU_READ_COLOR), s->filter != TIDE_FILTER_POINT, gpu_wrap(s->wrap));
}

// A step's 3D camera, from the world to clip space, fitted to the screen's
// shape: x divided by its width over its height.
static tide_float4x4 camera_3d_to_clip(const tide_draw_list *list, const draw_step *s)
{
    tide_float4x4 world_to_clip = s->camera ? list->matrices[s->camera - 1u]
                                            : tide_draw_camera_3d_matrix(tide_f3(0.0f, 0.0f, 0.0f), tide_identity_q(), 60.0f);
    if (!s->camera || s->fit) {
        const float across = draw_height / draw_width;
        world_to_clip.c0.x *= across, world_to_clip.c1.x *= across;
        world_to_clip.c2.x *= across, world_to_clip.c3.x *= across;
    }
    return world_to_clip;
}

// A camera as a matrix, from the units its commands are in to clip space:
// where to_screen puts a point, on the target.
static tide_float4x4 camera_to_clip(const camera *cam)
{
    const float x = 2.0f * cam->scale / draw_width, y = 2.0f * cam->scale / draw_height;
    tide_float4x4 m = tide_identity_f4x4();
    m.c0.x = x;
    if (cam->gui) { // From the top left, y down
        m.c1.y = -y;
        m.c3.x = -1.0f, m.c3.y = 1.0f;
    } else {
        m.c1.y = y;
        m.c3.x = -cam->center.x * x, m.c3.y = -cam->center.y * y;
    }
    return m;
}

// A run of instances of a mesh through a shader of the game's, at once.
static void draw_shaded(const tide_draw_list *list, const draw_step *s)
{
    static union {
        shaded_uniforms engine; // Then the run's own
        uint8_t bytes[TIDE_GPU_MAX_UNIFORMS];
    } block;
    const tide_draw_run *run = &list->runs[s->first];
    const tide_shader *shader = run->shader;
    const gpu_mesh *g = &gpu_meshes[run->mesh - 1u];
    const tide_float2 scale = pixel_scale();
    const uint32_t own = tide_shader_uniform_size(shader);
    block.engine.camera = s->kind == STEP_BLIT ? tide_identity_f4x4() :
                          shader->dims == 3 ? camera_3d_to_clip(list, s) : camera_to_clip(&s->through);
    if (run->moved && shader->dims != 3 && s->kind != STEP_BLIT) { // Its mesh goes through its transform first
        tide_float4x4 moved = tide_identity_f4x4();
        moved.c0.x = run->transform.x.x, moved.c0.y = run->transform.x.y;
        moved.c1.x = run->transform.y.x, moved.c1.y = run->transform.y.y;
        moved.c3.x = run->transform.offset.x, moved.c3.y = run->transform.offset.y;
        block.engine.camera = tide_mul_f4x4(block.engine.camera, moved);
    }
    block.engine.screen[0] = scale.x, block.engine.screen[1] = scale.y;
    block.engine.screen[2] = (float)draw_pixel_height;
    block.engine.screen[3] = s->blend == TIDE_BLEND_MULTIPLY ? 1.0f : 0.0f;
    block.engine.tint[0] = run->tint.r, block.engine.tint[1] = run->tint.g;
    block.engine.tint[2] = run->tint.b, block.engine.tint[3] = run->tint.a;
    if (own) memcpy(block.bytes + sizeof(shaded_uniforms), list->uniforms + run->uniforms, own);
    use_pipeline(s);
    gpu->uniforms(&block, sizeof(shaded_uniforms) + own);
    for (uint32_t i = 0; i < shader->texture_count; i++) {
        gpu->sample(i, texture_to_read(list, run->textures[i], gpu_read(shader, i)), tide_sampling_blended(run->sampling, i),
                    gpu_wrap(tide_sampling_wrap(run->sampling, i)));
    }
    gpu->vertices(0, g->vertices, 0);
    if (tide_shader_instance_size(shader)) gpu->vertices(1, gpu_state.instances, run->first);
    gpu->indices(g->indices, g->wide);
    gpu->draw_indexed(g->index_count, run->count);
}

static void draw_mesh_3d(const tide_draw_list *list, const draw_step *s)
{
    const gpu_mesh *g = &gpu_meshes[s->mesh - 1u];
    const mesh_3d_uniforms uniforms = {.camera = camera_3d_to_clip(list, s),
                                       .tint = {s->tint.r, s->tint.g, s->tint.b, s->tint.a},
                                       .fix = {s->blend == TIDE_BLEND_MULTIPLY ? 1.0f : 0.0f}};
    use_pipeline(s);
    gpu->uniforms(&uniforms, sizeof uniforms);
    sample_step_texture(list, s);
    gpu->vertices(0, g->vertices, 0);
    gpu->vertices(1, gpu_state.matrices, (size_t)s->first * sizeof(tide_float4x4));
    gpu->indices(g->indices, g->wide);
    gpu->draw_indexed(g->index_count, s->count);
}

// The pixel nearest `v`, from 0 to `most`: a rect's corner is any float a
// game says, so it's kept to the target before it's made an int (NaN is 0).
static int clamp_pixel(const float v, const int most)
{
    const float nearest = floorf(v + 0.5f);
    return !(nearest > 0.0f) ? 0 : nearest < (float)most ? (int)nearest : most;
}

// Only what's inside the step's rect draws from here on, or everything again.
static void clip(const draw_step *s)
{
    const int width = draw_pixel_width, height = draw_pixel_height;
    if (s->kind == STEP_NO_CLIP) {
        gpu->scissor(0, 0, width, height);
        return;
    }
    // In the target's pixels, and inside it
    const tide_float2 scale = pixel_scale();
    const int left = clamp_pixel(s->at.x * scale.x, width);
    const int right = clamp_pixel(s->to.x * scale.x, width);
    const int top = clamp_pixel(s->at.y * scale.y, height);
    const int bottom = clamp_pixel(s->to.y * scale.y, height);
    gpu->scissor(left, top, right > left ? right - left : 0, bottom > top ? bottom - top : 0);
}

// One of the list's 2D cameras (1 + its place, or 0 for a frame's first), on the selected target.
static camera camera_named(const tide_draw_list *list, const uint32_t at)
{
    camera cam = {{0.0f, 0.0f}, 1.0f, false};
    if (!at || at > list->camera_count) return cam;
    const tide_draw_camera_data *c = &list->cameras[at - 1u];
    if (c->screen) {
        cam.gui = true;
    } else {
        cam.center = c->center;
        cam.scale = c->size > 0.0f ? (float)draw_height / (2.0f * c->size) : 1.0f;
    }
    return cam;
}

// One of the list's clips on the selected target, from `at` to `to` in its
// logical pixels: its rect through the camera it was made under, and of that,
// what's inside the clip it was made in.
static void clip_corners(const tide_draw_list *list, uint32_t clip, tide_float2 *at, tide_float2 *to)
{
    *at = *to = tide_f2(0.0f, 0.0f);
    bool first = true;
    for (uint32_t depth = 0; clip && clip <= list->clip_count && depth < list->clip_count; depth++) {
        const tide_draw_clip_data *c = &list->clips[clip - 1u];
        const camera cam = camera_named(list, c->camera);
        // Its corners on the screen, whichever way up the units are
        const tide_float2 p = to_screen(&cam, tide_f2(c->rect.x, c->rect.y));
        const tide_float2 q = to_screen(&cam, tide_f2(c->rect.x + c->rect.width, c->rect.y + c->rect.height));
        const tide_float2 low = tide_f2(p.x < q.x ? p.x : q.x, p.y < q.y ? p.y : q.y);
        const tide_float2 high = c->rect.width > 0.0f && c->rect.height > 0.0f
                                     ? tide_f2(p.x < q.x ? q.x : p.x, p.y < q.y ? q.y : p.y) : low;
        if (first) {
            *at = low, *to = high;
        } else {
            *at = tide_f2(at->x > low.x ? at->x : low.x, at->y > low.y ? at->y : low.y);
            *to = tide_f2(to->x < high.x ? to->x : high.x, to->y < high.y ? to->y : high.y);
            if (to->x < at->x) to->x = at->x;
            if (to->y < at->y) to->y = at->y;
        }
        first = false;
        clip = c->parent;
    }
}

static void select_target(const uint32_t at)
{
    const frame_target *f = &frame_targets[at];
    draw_target = at;
    draw_stencil = f->stencil;
    draw_width = f->width;
    draw_height = f->height;
    draw_pixel_width = f->pixel_width;
    draw_pixel_height = f->pixel_height;
    scaled = (float)draw_pixel_width != draw_width || (float)draw_pixel_height != draw_height;
}

static screen_uniforms target_uniforms(void)
{
    const tide_float2 scale = pixel_scale();
    screen_uniforms u = {.screen = {[0] = 2.0f / draw_width, [5] = -2.0f / draw_height,
                                    [10] = 1.0f, [12] = -1.0f, [13] = 1.0f, [15] = 1.0f},
                         .scale = {scale.x, scale.y}};
    if (gpu_state.atlas) {
        u.texel[0] = 1.0f / (float)gpu_state.atlas_width;
        u.texel[1] = 1.0f / (float)gpu_state.atlas_height;
    }
    return u;
}

// The uniforms of a step that draws shapes, a mesh or text: the target's, how the step blends, and whether it
// stores stencil.
static const screen_uniforms *step_uniforms(screen_uniforms *of_target, const draw_step *s)
{
    of_target->fix[0] = s->blend == TIDE_BLEND_MULTIPLY ? 1.0f : 0.0f;
    of_target->fix[1] = stencil_stores(s->stencil) ? 1.0f : 0.0f;
    return of_target;
}

// Draws the list into `to`: the window, a headless one's frame, or
// tide_platform_read_pixels' own pixels. With `over`, it goes over what's
// there (the overlay), and leaves the frame's camera, textures and meshes as
// they are.
static void draw_list(const tide_draw_list *list, const draw_root to, const bool over, const bool timed)
{
    if (!gpu_ready()) { // Between a device that was lost and the next
        if (!over) draw_calls = 0;
        return;
    }
    const tide_gpu_id target = to.gpu;
    root = to;
    if (!over && target_epoch != list->epoch) {
        for (uint32_t i = 0; i < gpu_target_count; i++) gpu->target_free(gpu_targets[i].target);
        gpu_target_count = 0;
        target_epoch = list->epoch;
    }
    if (!over) forget_disposed_targets();
    const uint32_t targets = list->target_count + 1u;
    if (targets + 1u > frame_target_capacity) {
        frame_targets = tide_realloc(frame_targets, frame_target_capacity * sizeof(frame_target),
                                     (targets + 1u) * sizeof(frame_target));
        frame_target_capacity = targets + 1u;
    }
    // Invalid destination
    frame_targets[targets] = (frame_target){.id = TIDE_RENDER_INVALID, .width = 1.0f, .height = 1.0f,
                                            .pixel_width = 1, .pixel_height = 1};
    for (uint32_t i = 0; i < targets; i++) {
        frame_target *f = &frame_targets[i];
        *f = (frame_target){.through = {.scale = 1.0f}, .world = {.scale = 1.0f}, .fit = 1,
                            .clipping = {.kind = STEP_NO_CLIP}};
        if (i) {
            const tide_draw_target_data *t = &list->targets[i - 1u];
            bool limited;
            f->id = t->id;
            f->version = t->version;
            f->size = t->size;
            // In logical pixels, like the window: what's drawn into it lands as it would there
            f->valid = target_extent(t->size, &root, &f->width, &f->height, &f->pixel_width, &f->pixel_height, &limited)
                    && tide_render_target_live(t->id);
            f->into = gpu_target_format(t->id);
            f->bare = f->into == TIDE_GPU_TARGET_DEPTH;
            if (!f->valid) f->width = f->height = 1.0f, f->pixel_width = f->pixel_height = 1;
        } else {
            f->gpu = target;
            f->into = TIDE_GPU_TARGET_COLOR; // The window, or pixels of the renderer's own in its place
            f->width = root.width;
            f->height = root.height;
            f->pixel_width = root.pixel_width;
            f->pixel_height = root.pixel_height;
            f->valid = true;
        }
    }
    select_target(0);
    tide_font_frame();
    shape_count = 0;
    shape_extra_count = 0;
    mesh_vertex_count = 0;
    step_count = 0;
    for (uint32_t i = 0; i < list->count; i++) {
        const tide_draw_command *c = &list->commands[i];
        frame_target *f = &frame_targets[draw_target];
        camera *cam = &f->through;
        if (c->kind != TIDE_DRAW_TARGET && !f->valid) continue;
        const rgba color = to_rgba(c->color);
        draw_blend = c->blend;
        switch ((tide_draw_kind)c->kind) {
        case TIDE_DRAW_TARGET:
            select_target(c->target < targets ? c->target : targets);
            add_step(STEP_TARGET);
            break;
        case TIDE_DRAW_CLEAR:
            add_step(STEP_CLEAR)->color = color;
            break;
        case TIDE_DRAW_CAMERA:
            cam->center = c->a;
            cam->scale = c->b.x > 0.0f ? draw_height / (2.0f * c->b.x) : 1.0f;
            cam->gui = false;
            f->world = *cam;
            break;
        case TIDE_DRAW_SCREEN:
            cam->scale = 1.0f;
            cam->gui = true;
            break;
        case TIDE_DRAW_MESH:
            add_mesh(list, c, cam);
            break;
        case TIDE_DRAW_CAMERA_3D:
            if (c->camera.matrix >= list->matrix_count) { // TIDE_DRAW_FIRST_CAMERA
                f->camera_3d = 0;
                f->fit = 1;
                f->mirrored = false;
                break;
            }
            f->camera_3d = c->camera.matrix + 1u;
            f->fit = c->camera.fit;
            f->mirrored = c->camera.mirrored != 0;
            break;
        case TIDE_DRAW_MESH_3D: {
            if (!c->mesh3.mesh || c->mesh3.mesh > list->mesh_count) break; // Forgotten (see tide_draw_forget)
            if (list->meshes[c->mesh3.mesh - 1u].space == TIDE_MESH_BYTES) break; // A shader's: not these vertices
            draw_step *s = add_step(STEP_MESH_3D);
            s->depth = c->depth;
            s->cull = gpu_cull(c->cull, f->mirrored);
            s->first = c->mesh3.first;
            s->count = c->mesh3.count;
            s->texture = c->mesh3.texture;
            s->filter = c->mesh3.filter;
            s->wrap = c->mesh3.wrap;
            s->mesh = c->mesh3.mesh;
            s->camera = f->camera_3d;
            s->fit = f->fit;
            s->tint = c->color;
            break;
        }
        case TIDE_DRAW_SHADED:
        case TIDE_DRAW_BLIT: {
            if (!run_draws(list, c->shaded.run)) break;
            if (c->kind == TIDE_DRAW_BLIT && list->runs[c->shaded.run].shader->dims != 2) break;
            draw_step *s = add_step(c->kind == TIDE_DRAW_BLIT ? STEP_BLIT : STEP_SHADED);
            s->depth = c->depth;
            s->cull = gpu_cull(c->cull, f->mirrored);
            s->first = c->shaded.run;
            s->camera = f->camera_3d;
            s->fit = f->fit;
            s->through = *cam;
            break;
        }
        case TIDE_DRAW_CLIP: {
            draw_step *s = add_step(STEP_CLIP);
            clip_corners(list, c->clip, &s->at, &s->to);
            break;
        }
        case TIDE_DRAW_NO_CLIP:
            add_step(STEP_NO_CLIP);
            break;
        case TIDE_DRAW_STENCIL:
            // A destination's own, for the steps after it: what's drawn next starts a run of its own
            f->stencil = draw_stencil = tide_stencil_packed(tide_stencil_unpacked(c->stencil));
            add_step(STEP_STENCIL);
            break;
        case TIDE_DRAW_CIRCLE:
        case TIDE_DRAW_WIRE_CIRCLE: {
            const tide_float2 p = to_screen(cam, c->a);
            const float r = c->b.x * cam->scale;
            add_circle(c->kind == TIDE_DRAW_WIRE_CIRCLE, p.x, p.y, r, color);
            break;
        }
        case TIDE_DRAW_SHAPE:
            add_smooth_shape(list, c, cam);
            break;
        case TIDE_DRAW_RECT:
        case TIDE_DRAW_WIRE_RECT: {
            const tide_float2 top_left = rect_corner(cam, c);
            const float hw = c->b.x * cam->scale * 0.5f;
            const float hh = c->b.y * cam->scale * 0.5f;
            add_shape(c->kind == TIDE_DRAW_RECT ? SHAPE_RECT : SHAPE_WIRE_RECT, top_left.x + hw, top_left.y + hh, hw,
                      hh, color);
            break;
        }
        case TIDE_DRAW_LINE: {
            const tide_float2 from = to_screen(cam, c->a);
            const tide_float2 to = to_screen(cam, c->b);
            add_shape(SHAPE_LINE, from.x, from.y, to.x, to.y, color);
            break;
        }
        case TIDE_DRAW_TEXT: {
            // A command in bold without the bold's file (a list filled by hand) is in the regular weight
            const unsigned char *font = c->words.bold ? list->bold : TIDE_FONT_REGULAR;
            if (c->words.turned && c->words.turned <= list->turn_count) {
                add_text_turned(list->text + c->text, to_screen(cam, c->a), c->b.x, cam, list->turns[c->words.turned - 1u],
                                color, c->words.anchor, font);
            } else {
                add_text(list->text + c->text, to_screen(cam, c->a), c->b.x * cam->scale, color, c->words.anchor, font);
            }
            break;
        }
        }
    }
    settle_extras();
    if (!over) last_camera = frame_targets[0].world;

    // What the steps draw from goes to the GPU first
    renderer_start();
    find_pipelines(list);
    upload_shapes_and_meshes();
    upload_meshes_3d(list);
    const bool shaded = upload_shaded(list);
    upload_atlas();
    select_target(0);
    screen_uniforms uniforms = target_uniforms();
    // Start after the CPU expanded the list and prepared/uploaded resources.
    // Otherwise a desktop GPU waiting for that work would time the CPU too.
    if (timed && gpu->time_begin) gpu->time_begin();
    begin_pass(target, !over); // Every frame starts black; Draw.Clear picks another color
    bool pass_active = true;
    uint32_t calls = 0; // The steps that draw, a draw call each
    for (uint32_t i = 0; i < step_count; i++) {
        const draw_step *s = &steps[i];
        if (s->target != draw_target) {
            if (pass_active) gpu->end();
            pass_active = false;
            select_target(s->target);
            frame_target *f = &frame_targets[draw_target];
            if (!f->valid) {
                fprintf(stderr, "draw skipped: render target %llu is invalid or disposed\n", (unsigned long long)f->id);
                continue;
            }
            if (draw_target) {
                const gpu_target *g = target_for(f->id, f->size, f->version);
                f->gpu = g ? g->target : 0;
            }
            begin_pass(f->gpu, false);
            pass_active = true;
            uniforms = target_uniforms();
            clip(&frame_targets[draw_target].clipping);
        }
        if (!frame_targets[draw_target].valid) continue;
        if (leaves_nothing(list, s)) continue; // A color, into a target of depth alone
        const char *unavailable = samples_unavailable(list, s);
        if (unavailable) {
            fprintf(stderr, "draw skipped: %s\n", unavailable);
            continue;
        }
        if (samples_destination(list, s)) {
            fprintf(stderr, "draw skipped: render target %llu cannot sample itself\n",
                    (unsigned long long)frame_targets[s->target].id);
            continue;
        }
        if (prepare_source_targets(list, s, true)) {
            gpu->end();
            prepare_source_targets(list, s, false);
            begin_pass(frame_targets[draw_target].gpu, false);
            clip(&frame_targets[draw_target].clipping);
        }
        switch (s->kind) {
        case STEP_TARGET:
        case STEP_STENCIL:
            break;
        case STEP_SHAPES:
            // A run with smooth shapes goes through the pipeline that draws every kind, with their extras
            use_pipeline(s);
            if (s->smooth) {
                gpu->uniforms(step_uniforms(&uniforms, s), sizeof uniforms);
                gpu->vertices(2, gpu_state.shape_extras, (size_t)(s->smooth - 1u) * sizeof(shape_extra));
            } else {
                gpu->uniforms(uniforms.screen, sizeof uniforms.screen);
            }
            gpu->vertices(0, gpu_state.corners, 0);
            gpu->vertices(1, gpu_state.shape_instances, (size_t)s->first * sizeof(shape));
            gpu->draw(0, 6, s->count);
            calls++;
            break;
        case STEP_CLEAR: {
            // A triangle over the target, which the clip cuts, and which sets depth and stencil back too
            const float color[4] = {(float)s->color.r / 255.0f, (float)s->color.g / 255.0f, (float)s->color.b / 255.0f,
                                    (float)s->color.a / 255.0f};
            use_pipeline(s);
            gpu->uniforms(color, sizeof color);
            gpu->draw(0, 3, 1);
            calls++;
            break;
        }
        case STEP_MESH:
            if (s->count == 0) break;
            use_pipeline(s);
            gpu->uniforms(step_uniforms(&uniforms, s), sizeof uniforms);
            sample_step_texture(list, s);
            gpu->vertices(0, gpu_state.mesh_vertices, 0);
            gpu->draw(s->first, s->count, 1);
            calls++;
            break;
        case STEP_MESH_3D:
            draw_mesh_3d(list, s);
            calls++;
            break;
        case STEP_SHADED:
            draw_shaded(list, s);
            calls++;
            break;
        case STEP_BLIT:
            clip(&(draw_step){.kind = STEP_NO_CLIP});
            draw_shaded(list, s);
            calls++;
            clip(&frame_targets[draw_target].clipping);
            break;
        case STEP_CLIP:
        case STEP_NO_CLIP:
            frame_targets[draw_target].clipping = *s;
            clip(s);
            break;
        default: // Text
            if (s->count == 0 || !gpu_state.atlas) break;
            use_pipeline(s);
            gpu->uniforms(step_uniforms(&uniforms, s), sizeof uniforms);
            // Stretched glyphs blend; the others land on the target's pixels
            gpu->sample(0, gpu_state.atlas, true, TIDE_GPU_WRAP_CLAMP);
            gpu->vertices(0, gpu_state.mesh_vertices, 0);
            gpu->draw(s->first, s->count, 1);
            calls++;
            break;
        }
    }
    if (pass_active) gpu->end();
    if (timed && gpu->time_end) gpu->time_end();
    draw_width = draw_height = 0.0f;
    draw_pixel_width = draw_pixel_height = 0;
    scaled = false;
    draw_target = 0;
    if (!over) {
        draw_calls = calls;
        // The first frame through shaders after the list forgot (a hot reload): what's left of the last build's goes
        if (shaded && list->epoch != shader_epoch) {
            forget_pipelines();
            shader_epoch = list->epoch;
        }
        forget_meshes_3d();
        forget_textures();
    }
}

// What frames draw into: the window (0), or a headless one's own pixels, made
// the first time and again when the device is another. Either way it's the
// window's size, at the display's scale.
static draw_root frame_destination(void)
{
    draw_root to = {.width = (float)screen_width, .height = (float)screen_height,
                    .pixel_width = pixel_width, .pixel_height = pixel_height};
    if (!headless || !gpu_ready()) return to;
    if (!gpu_state.frame) {
        gpu_state.frame = gpu->target(TIDE_GPU_TARGET_COLOR, pixel_width, pixel_height);
        gpu_state.frame_width = pixel_width;
        gpu_state.frame_height = pixel_height;
    }
    to.gpu = gpu_state.frame;
    return to;
}

void tide_platform_draw(const tide_draw_list *list)
{
    // Nobody sees it while the window is minimized or, on the web, the page
    // is hidden, and frames go on meanwhile. A headless window's are for
    // whoever reads them.
    if (headless || !tide_window_unseen()) {
        if (!gpu_ready()) return;
        draw_list(list, frame_destination(), false, gpu_timing);
    }
}

void tide_platform_gpu_timing(const bool enabled)
{
    if (gpu_timing == enabled) return;
    gpu_timing = enabled;
    if (!enabled && gpu && gpu->epoch() && gpu->time_reset) gpu->time_reset();
}

double tide_platform_gpu_ms(void)
{
    if (!gpu_timing || !gpu || !gpu_ready() || (!headless && tide_window_unseen()) || !gpu->time_read) return -1.0;
    return gpu->time_read();
}

tide_float2 tide_platform_world_to_screen(const tide_float2 world)
{
    return to_screen(&last_camera, world);
}

tide_float2 tide_platform_screen_size(void)
{
    return tide_f2((float)screen_width, (float)screen_height);
}

// As add_text draws it: on the window's pixels, or on those of a render
// target's image, as many to a logical one as a frame drawn into the window
// gives it.
tide_float2 tide_platform_measure_text(const char *text, const float size, const tide_target_size *target,
                                       const unsigned char *font)
{
    tide_float2 scale = window_scale();
    if (target) {
        const draw_root window = {.width = (float)screen_width, .height = (float)screen_height,
                                  .pixel_width = pixel_width, .pixel_height = pixel_height};
        float width, height;
        int wide, high;
        bool limited;
        if (!target_extent(*target, &window, &width, &height, &wide, &high, &limited)) return tide_f2(0.0f, 0.0f);
        scale = tide_f2((float)wide / width, (float)high / height);
    }
    tide_float2 measured;
    tide_font_size(font, text, size, scale.x, scale.y, &measured.x, &measured.y);
    return measured;
}

const char *tide_platform_renderer(void)
{
    return gpu ? gpu->name : "";
}

uint32_t tide_platform_draw_calls(void)
{
    return draw_calls;
}

void tide_platform_draw_overlay(const char *text)
{
    if (!headless && tide_window_unseen()) return;
    static tide_draw_list overlay;
    const float size = 16.0f;
    const float pitch = 20.0f; // From one line to the next
    char lines[16][128];
    float widths[16];
    int count = 0;
    float widest = 0.0f;
    for (const char *line = text; line && count < 16; count++) {
        const char *end = strchr(line, '\n');
        size_t n = end ? (size_t)(end - line) : strlen(line);
        if (n >= sizeof lines[0]) n = sizeof lines[0] - 1;
        memcpy(lines[count], line, n);
        lines[count][n] = '\0';
        widths[count] = ceilf(tide_font_measure(TIDE_FONT_REGULAR, lines[count], size, window_scale().y));
        if (widths[count] > widest) widest = widths[count];
        line = end ? end + 1 : NULL;
    }
    // A panel in the bottom right corner, each line against its right edge
    const float right = (float)screen_width - 12.0f;
    const float top = (float)screen_height - 12.0f - (float)count * pitch;
    const tide_float2 panel = tide_f2(widest + 16.0f, (float)count * pitch + 8.0f);
    tide_draw_reset(&overlay);
    tide_draw_set_screen(&overlay);
    tide_draw_rect(&overlay, tide_f2(right + 8.0f - panel.x * 0.5f, top - 6.0f + panel.y * 0.5f), panel,
                   (tide_color){0.0f, 0.0f, 0.0f, 140.0f / 255.0f});
    for (int i = 0; i < count; i++) {
        tide_draw_text(&overlay, lines[i], tide_f2(right - widths[i], top + (float)i * pitch), size,
                       (tide_color){200.0f / 255.0f, 200.0f / 255.0f, 200.0f / 255.0f, 1.0f});
    }
    draw_list(&overlay, frame_destination(), true, false);
}

void tide_platform_draw_over(const tide_draw_list *list)
{
    if (headless || !tide_window_unseen()) draw_list(list, frame_destination(), true, false);
}

const uint8_t *tide_platform_frame_pixels(int *width, int *height)
{
    *width = *height = 0;
    if (!headless || !gpu_ready() || !gpu_state.frame) return NULL;
    if (!gpu_state.frame_pixels) {
        gpu_state.frame_pixels = tide_alloc((size_t)gpu_state.frame_width * (size_t)gpu_state.frame_height * 4u);
    }
    gpu->target_read(gpu_state.frame, gpu_state.frame_pixels);
    *width = gpu_state.frame_width;
    *height = gpu_state.frame_height;
    return gpu_state.frame_pixels;
}

bool tide_platform_capture(const char *path)
{
    int width, height;
    const uint8_t *pixels = tide_platform_frame_pixels(&width, &height);
    if (!pixels) {
        fprintf(stderr, "tide: no frame to capture: a headless window's are, once one drew (tide_platform_open_headless)\n");
        return false;
    }
    size_t size = 0;
    uint8_t *png = tide_png_encode(pixels, width, height, &size);
    errno = 0;
    FILE *file = png ? fopen(path, "wb") : NULL;
    bool written = file && fwrite(png, 1, size, file) == size;
    const int error = errno; // Before fclose says something else
    if (file && fclose(file) != 0) written = false;
    free(png);
    if (!written) {
        fprintf(stderr, "tide: can't write %s: %s\n", path, error ? strerror(error) : "the file isn't whole");
        return false;
    }
    printf("tide: captured %s, %d by %d pixels\n", path, width, height);
    fflush(stdout);
    return true;
}

void tide_platform_read_pixels(const tide_draw_list *list, const tide_float2 *points, const int count, uint32_t *out)
{
    tide_platform_read_pixels_as(list, screen_width, screen_height, 1.0f, points, count, out);
}

void tide_platform_read_pixels_as(const tide_draw_list *list, const int logical_width, const int logical_height,
                                  const float scale, const tide_float2 *points, const int count, uint32_t *out)
{
    // As a window of that size has them on a display scaled that much (see follow_window)
    const int width = whole_pixels((double)logical_width * (double)scale);
    const int height = whole_pixels((double)logical_height * (double)scale);
    if (!gpu_ready() || logical_width < 1 || logical_height < 1 || !(scale > 0.0f)) { // Nothing to draw with: no pixels
        memset(out, 0, (size_t)(count > 0 ? count : 0) * sizeof *out);
        return;
    }
    if (!gpu_state.target || gpu_state.target_width != width || gpu_state.target_height != height) {
        if (gpu_state.target) gpu->target_free(gpu_state.target);
        gpu_state.target = gpu->target(TIDE_GPU_TARGET_COLOR, width, height);
        gpu_state.target_pixels =
            tide_realloc(gpu_state.target_pixels, (size_t)gpu_state.target_width * (size_t)gpu_state.target_height * 4u,
                         (size_t)width * (size_t)height * 4u);
        gpu_state.target_width = width;
        gpu_state.target_height = height;
    }
    draw_list(list, (draw_root){.gpu = gpu_state.target, .width = (float)logical_width, .height = (float)logical_height,
                              .pixel_width = width, .pixel_height = height}, false, false);
    gpu->target_read(gpu_state.target, gpu_state.target_pixels);
    for (int i = 0; i < count; i++) {
        const int x = (int)points[i].x, y = (int)points[i].y;
        uint32_t pixel = 0;
        if (x >= 0 && x < width && y >= 0 && y < height) {
            const uint8_t *p = gpu_state.target_pixels + ((size_t)y * (size_t)width + (size_t)x) * 4u;
            pixel = (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | (uint32_t)p[3];
        }
        out[i] = pixel;
    }
}
