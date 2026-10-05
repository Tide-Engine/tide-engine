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
static int draw_width, draw_height; // The selected target's logical pixels, during a draw

static tide_float2 to_screen(const camera *cam, const tide_float2 p)
{
    if (cam->gui) return tide_f2(p.x * cam->scale, p.y * cam->scale);
    const int width = draw_width ? draw_width : screen_width, height = draw_height ? draw_height : screen_height;
    return tide_f2((float)width * 0.5f + (p.x - cam->center.x) * cam->scale,
                   (float)height * 0.5f - (p.y - cam->center.y) * cam->scale);
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
// of meshes through shaders of the game's.
enum { STEP_SHAPES, STEP_CLEAR, STEP_TEXT, STEP_MESH, STEP_CLIP, STEP_NO_CLIP, STEP_MESH_3D, STEP_SHADED,
       STEP_TARGET, STEP_BLIT };

typedef struct draw_step {
    uint32_t kind;    // STEP_*
    uint32_t target;  // The destination: 0 for the window, 1 + its place in the list's targets
    uint32_t first;   // STEP_SHAPES: its shapes; STEP_TEXT and STEP_MESH: its vertices;
                      // STEP_MESH_3D: its instances' transforms in the list's matrices;
                      // STEP_SHADED: its run's place in the list's
    uint32_t count;
    uint32_t texture; // STEP_MESH and STEP_MESH_3D: 1 + its texture's place in the list's, or 0 for none
    uint32_t filter;  // ...and how it's sampled: tide_filter
    uint32_t mesh;    // STEP_MESH_3D: 1 + its mesh's place in the list's
    uint32_t camera;  // ...and 1 + its camera's matrix's place in the list's, or 0 for the first camera:
                      // a STEP_SHADED's too, for a 3D shader
    uint32_t fit;     // ...which is for a square screen (see TIDE_DRAW_CAMERA_3D)
    uint32_t smooth;  // STEP_SHAPES: for a run with smooth shapes, 1 + where its shapes' extras start; else 0
    camera through;   // STEP_SHADED: the camera its command was under, for a 2D shader
    tide_gpu_id pipeline; // ...and its shader's pipeline, once it's found
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
static uint32_t draw_calls; // What the last list drawn took, the overlay's aside (tide_platform_draw_calls)

// A target's cameras and clip belong to it for the whole frame, including
// passes recorded by different views. The clip is advanced during playback.
typedef struct frame_target {
    camera through, world;
    uint32_t camera_3d, fit;
    draw_step clipping;
    tide_gpu_id gpu;
    int width, height;
    uint64_t id;
    uint64_t version;
    bool valid;
} frame_target;

static frame_target *frame_targets;
static uint32_t frame_target_capacity;

// Whether the selected destination uses its own pixels rather than the
// window's logical pixels: a render target or the readback target does. A
// headless window's frame doesn't: it's drawn as a window's is, scale and all.
static bool offscreen;

// The window's pixels in a logical one, which its text is fitted to (see font.h).
static tide_float2 window_scale(void)
{
    return tide_f2((float)pixel_width / (float)screen_width, (float)pixel_height / (float)screen_height);
}

// The target's pixels per logical pixel.
static tide_float2 pixel_scale(void)
{
    return offscreen ? tide_f2(1.0f, 1.0f) : window_scale();
}

static draw_step *add_step(const uint32_t kind)
{
    if (step_count == step_capacity) {
        const uint32_t capacity = step_capacity ? step_capacity * 2u : 64u;
        steps = tide_realloc(steps, step_capacity * sizeof(draw_step), capacity * sizeof(draw_step));
        step_capacity = capacity;
    }
    draw_step *s = &steps[step_count++];
    *s = (draw_step){.kind = kind, .target = draw_target};
    return s;
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

// A shape, after the ones before it when nothing came between them. `extra`
// is a smooth shape's, and NULL for the plain kinds. The first smooth shape of
// a run makes it a run of them: the shapes before it get an extra each, which
// nothing reads, and the ones after it as they come.
static void add_shape(const int kind, const float ax, const float ay, const float bx, const float by, const rgba color,
                      const shape_extra *extra)
{
    if (shape_count == shape_capacity) {
        const uint32_t capacity = shape_capacity ? shape_capacity * 2u : 1024u;
        if (capacity > UINT32_MAX / sizeof(shape)) tide_out_of_memory();
        shapes = tide_realloc(shapes, shape_capacity * sizeof(shape), capacity * sizeof(shape));
        shape_capacity = capacity;
    }
    if (step_count == 0 || steps[step_count - 1].kind != STEP_SHAPES) add_step(STEP_SHAPES)->first = shape_count;
    draw_step *s = &steps[step_count - 1];
    if (extra && !s->smooth) {
        if (s->count) memset(extra_room(s->count), 0, (size_t)s->count * sizeof(shape_extra));
        s->smooth = shape_extra_count + 1u;
        shape_extra_count += s->count;
    }
    if (s->smooth) {
        *extra_room(1) = extra ? *extra : (shape_extra){0};
        shape_extra_count++;
    }
    s->count++;
    shapes[shape_count++] = (shape){{ax, ay}, {bx, by}, {color.r, color.g, color.b, color.a}, (float)kind};
}

// A TIDE_DRAW_SHAPE command's shape, through the camera: its lengths in window
// pixels, and its angles as the screen has them, where y goes down.
static void add_smooth_shape(const tide_draw_list *list, const tide_draw_command *c, const camera *cam)
{
    if (c->shape >= list->shape_count) return;
    static const int kinds[] = {[TIDE_SHAPE_BOX] = SHAPE_BOX, [TIDE_SHAPE_ELLIPSE] = SHAPE_ELLIPSE,
                                [TIDE_SHAPE_ARC] = SHAPE_ARC};
    const tide_draw_shape_data *s = &list->shapes[c->shape];
    if (s->form > TIDE_SHAPE_ARC) return;
    const tide_float2 center = to_screen(cam, c->a);
    const float scale = cam->scale;
    const rgba color = to_rgba(c->color), color2 = to_rgba(s->color2), rim = to_rgba(s->outline_color);
    add_shape(kinds[s->form], center.x, center.y, c->b.x * scale * 0.5f, c->b.y * scale * 0.5f, color,
              &(shape_extra){.turn = {s->turn.x, cam->gui ? s->turn.y : -s->turn.y},
                             .radius = s->radius * scale,
                             .outline = s->outline * scale,
                             .soft = s->soft * scale,
                             .sweep = s->sweep,
                             .color2 = {color2.r, color2.g, color2.b, color2.a},
                             .rim = {rim.r, rim.g, rim.b, rim.a}});
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
    draw_step *s = step_count ? &steps[step_count - 1] : NULL;
    if (!s || s->kind != STEP_MESH || s->texture != c->mesh.texture || s->filter != c->mesh.filter) {
        s = add_step(STEP_MESH);
        s->first = mesh_vertex_count;
        s->texture = c->mesh.texture;
        s->filter = c->mesh.filter;
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

// Text of `size` with its top left corner at `at`, in window pixels: a quad a
// glyph, after the text before it when nothing came between them. It's drawn
// on the target's own pixels (see font.h): its lines start on one, and each
// glyph is a whole number of them from the last, so none is ever blended
// between two.
static void add_text(const char *text, const tide_float2 at, const float size, const rgba color)
{
    if (!(size > 0.0f)) return;
    const tide_float2 scale = pixel_scale();
    float stretch;
    const int pixels = tide_font_pixels(size, scale.y, &stretch);
    // In the target's pixels, as the pen is
    const float left = floorf(at.x * scale.x + 0.5f);
    const float pitch = tide_font_pitch(pixels, stretch);
    float pen_x = left, pen_y = floorf((at.y + tide_font_baseline() * size) * scale.y + 0.5f); // On the baseline

    if (step_count == 0 || steps[step_count - 1].kind != STEP_TEXT) add_step(STEP_TEXT)->first = mesh_vertex_count;
    for (uint32_t c; (c = tide_utf8_next(&text)) != 0;) {
        if (c == '\n') {
            pen_x = left;
            pen_y += pitch;
            continue;
        }
        tide_font_glyph g;
        if (!tide_font_glyph_for(c, pixels, &g)) continue; // No room for it this frame
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

// What the renderer keeps on the GPU (see gpu.h): its pipelines, whose
// shaders are in shaders.h, and the buffers and textures a list's steps draw
// from, made the first time they're needed.
static struct {
    tide_gpu_id shapes, smooth_shapes, meshes, text, meshes_3d, clear; // Pipelines
    tide_gpu_id corners;                                               // The shapes' quad
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

// The shapes', meshes' and text's uniforms, as their shaders have them: from
// the target's logical pixels (from its top left, y down) to clip space, as
// OpenGL's matrices go, column by column; for text, 1 over the atlas's size;
// and for shapes, the target's own pixels in a logical one, across and down,
// which is how they know where a pixel ends.
typedef struct screen_uniforms {
    float screen[16];
    float texel[2];
    float scale[2];
} screen_uniforms;

// What the uniforms of a shader of the game's start with (see tide_shader),
// before a run's own: `tide_camera`, from the units its mesh is in to clip
// space, and `tide_screen`, the target's pixels in a logical one, across and
// down, then its height in its own pixels.
typedef struct shaded_uniforms {
    tide_float4x4 camera;
    float screen[4];
} shaded_uniforms;

_Static_assert(sizeof(shaded_uniforms) + TIDE_SHADER_UNIFORM_BYTES <= TIDE_GPU_MAX_UNIFORMS,
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
    int32_t width, height;
    uint32_t epoch; // Its list's when it copied them (see tide_draw_forget)
    tide_gpu_id texture;
    uint32_t frame; // The frame that last drew with it
} gpu_texture;

static gpu_texture *gpu_textures;
static uint32_t gpu_texture_count, gpu_texture_capacity;
static uint32_t gpu_frame;

// Render targets retain their attachments across passes and frames. Their
// owner supplies a stable identity; resizing replaces both attachments.
typedef struct gpu_target {
    uint64_t id;
    uint64_t version;
    int width, height;
    tide_gpu_id target;
} gpu_target;

static gpu_target *gpu_targets;
static uint32_t gpu_target_count, gpu_target_capacity;
static uint32_t target_epoch; // A hot reload discards the old build's target identities

static gpu_target *find_target(const uint64_t id)
{
    for (uint32_t i = 0; i < gpu_target_count; i++) {
        if (gpu_targets[i].id == id) return &gpu_targets[i];
    }
    return NULL;
}

// Called outside a pass: a newly made image is black, with its depth reset.
static gpu_target *target_for(const uint64_t id, const int width, const int height, const uint64_t version)
{
    if (!tide_render_target_live(id) || width <= 0 || height <= 0) return NULL;
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
        g->target = gpu->target(width, height);
        gpu->begin(g->target, true);
        gpu->end();
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
    bool blit;     // Replace color and leave depth alone, rather than an ordinary mesh
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
    free(gpu_state.target_pixels);
    free(gpu_state.frame_pixels);
    memset(&gpu_state, 0, sizeof gpu_state);
    if (gpu_meshes) memset(gpu_meshes, 0, gpu_mesh_count * sizeof(gpu_mesh));
    gpu_texture_count = 0;
    gpu_target_count = 0;
    shader_pipeline_count = 0;
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

// The pipelines, the quad and the white pixel, the first time a list is drawn.
static void renderer_start(void)
{
    if (gpu_state.shapes) return;
    gpu_state.shapes = gpu->pipeline(&(tide_gpu_pipeline_desc){
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
        .uniform_size = sizeof(screen_uniforms),
        .blend = TIDE_GPU_BLEND_ALPHA,
        .depth = TIDE_GPU_DEPTH_NONE,
    });
    gpu_state.smooth_shapes = gpu->pipeline(&(tide_gpu_pipeline_desc){
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
        .blend = TIDE_GPU_BLEND_ALPHA,
        .depth = TIDE_GPU_DEPTH_NONE,
    });
    tide_gpu_pipeline_desc mesh = {
        .name = "meshes",
        .glsl_vertex = mesh_glsl_vertex,
        .glsl_fragment = mesh_glsl_fragment,
        .wgsl = mesh_wgsl,
        .slots = {{sizeof(mesh_vertex), false}},
        .slot_count = 1,
        .attributes = {{0, TIDE_GPU_FLOAT2, offsetof(mesh_vertex, position)},
                       {0, TIDE_GPU_FLOAT2, offsetof(mesh_vertex, uv)},
                       {0, TIDE_GPU_BYTES4, offsetof(mesh_vertex, color)}},
        .attribute_count = 3,
        .uniform_size = sizeof(screen_uniforms),
        .texture_count = 1,
        .blend = TIDE_GPU_BLEND_ALPHA,
        .depth = TIDE_GPU_DEPTH_NONE,
    };
    gpu_state.meshes = gpu->pipeline(&mesh);
    mesh.name = "text"; // A mesh's corners, with pixels of its own
    mesh.glsl_fragment = text_glsl_fragment;
    mesh.wgsl = text_wgsl;
    gpu_state.text = gpu->pipeline(&mesh);
    gpu_state.meshes_3d = gpu->pipeline(&(tide_gpu_pipeline_desc){
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
        .uniform_size = sizeof(tide_float4x4),
        .texture_count = 1,
        .blend = TIDE_GPU_BLEND_ALPHA,
        .depth = TIDE_GPU_DEPTH_TEST,
    });
    gpu_state.clear = gpu->pipeline(&(tide_gpu_pipeline_desc){
        .name = "clears",
        .glsl_vertex = clear_glsl_vertex,
        .glsl_fragment = clear_glsl_fragment,
        .wgsl = clear_wgsl,
        .uniform_size = 4 * sizeof(float),
        .blend = TIDE_GPU_BLEND_REPLACE,
        .depth = TIDE_GPU_DEPTH_RESET,
    });
    static const float corners[12] = {0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 0};
    gpu_state.corners = gpu->buffer(TIDE_GPU_VERTICES, sizeof corners, corners);
    gpu_state.white = gpu->texture(TIDE_GPU_RGBA8, 1, 1, (const uint8_t[4]){255, 255, 255, 255});
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

// Where the pipeline of the shader `id` is in the index, or would go.
static uint32_t shader_slot(const uint64_t id, const bool blit)
{
    const uint32_t mask = shader_index_capacity - 1u;
    uint32_t i = (uint32_t)(id ^ id >> 32 ^ (blit ? 0x9e3779b9u : 0u)) & mask;
    while (shader_index[i]) {
        const shader_pipeline *p = &shader_pipelines[shader_index[i] - 1u];
        if (p->id == id && p->blit == blit) break;
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
        shader_index[shader_slot(shader_pipelines[i].id, shader_pipelines[i].blit)] = i + 1u;
    }
}

// The pipeline of a shader of the game's, made if there's none: it reads a
// mesh's vertices as they are from the first slot, and from the second an
// instance's bytes, a float4 or an int4 an attribute.
static tide_gpu_id pipeline_for(const tide_shader *shader, const bool blit)
{
    if (shader_pipeline_count >= shader_index_capacity / 2u) index_shaders();
    const uint32_t slot = shader_slot(shader->id, blit);
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
        .blend = blit ? TIDE_GPU_BLEND_REPLACE : TIDE_GPU_BLEND_ALPHA,
        .depth = !blit && shader->dims == 3 ? TIDE_GPU_DEPTH_TEST : TIDE_GPU_DEPTH_NONE,
    };
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
    shader_pipelines[shader_pipeline_count] = (shader_pipeline){shader->id, pipeline, gpu_frame, blit};
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
        const tide_draw_run *run = &list->runs[steps[i].first];
        any = true;
        mesh_for(list, run->mesh);
        steps[i].pipeline = pipeline_for(run->shader, steps[i].kind == STEP_BLIT);
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
        && g->epoch == t->epoch) {
        return g;
    }
    if (g->texture && g->width == t->width && g->height == t->height) {
        gpu->texture_write(g->texture, t->pixels);
    } else {
        if (g->texture) gpu->texture_free(g->texture);
        g->texture = gpu->texture(TIDE_GPU_RGBA8, t->width, t->height, t->pixels);
    }
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

// The GPU's copy of the list's texture `texture` (1 + its place), or the
// white pixel for none.
static tide_gpu_id texture_or_white(const tide_draw_list *list, const uint32_t texture)
{
    if (texture == 0 || texture > list->texture_count) return gpu_state.white;
    const tide_draw_texture *t = &list->textures[texture - 1u];
    if (t->space == TIDE_PIXELS_TARGET) {
        const gpu_target *g = find_target(t->id);
        return g && g->target ? gpu->target_texture(g->target) : gpu_state.white;
    }
    const gpu_texture *g = texture_for(t);
    return g->texture ? g->texture : gpu_state.white;
}

// Reading the color attachment while writing it is undefined on every GPU.
// Refuse the whole draw, including an ordinary mesh or an indirect binding.
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
                if (t->space == TIDE_PIXELS_TARGET && t->id == id) return true;
            }
        }
    } else if (s->texture && s->texture <= list->texture_count) {
        const tide_draw_texture *t = &list->textures[s->texture - 1u];
        if (t->space == TIDE_PIXELS_TARGET && t->id == id) return true;
    }
    return false;
}

static bool unavailable_texture(const tide_draw_list *list, const uint32_t texture)
{
    if (!texture || texture > list->texture_count) return false;
    const tide_draw_texture *t = &list->textures[texture - 1u];
    return t->space == TIDE_PIXELS_TARGET && (!tide_render_target_live(t->id) || t->width <= 0 || t->height <= 0);
}

static bool samples_unavailable(const tide_draw_list *list, const draw_step *s)
{
    if (s->kind == STEP_SHADED || s->kind == STEP_BLIT) {
        const tide_draw_run *run = &list->runs[s->first];
        for (uint32_t i = 0; i < run->shader->texture_count; i++) {
            if (unavailable_texture(list, run->textures[i])) return true;
        }
    } else if (unavailable_texture(list, s->texture)) {
        return true;
    }
    return false;
}

// A source recorded after Resize first sees the replacement black image,
// even when no command draws into it. An older source still aliases the
// current attachment; it never rolls a target back to an earlier revision.
static bool prepare_source_target(const tide_draw_list *list, const uint32_t texture, const bool check)
{
    if (!texture || texture > list->texture_count) return false;
    const tide_draw_texture *t = &list->textures[texture - 1u];
    if (t->space != TIDE_PIXELS_TARGET) return false;
    const gpu_target *g = find_target(t->id);
    if (g && (!tide_render_target_managed(t->id) || t->version <= g->version)) return false;
    if (!check) target_for(t->id, t->width, t->height, t->version);
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

// The texture a mesh step samples, sampled as the step says.
static void sample_step_texture(const tide_draw_list *list, const draw_step *s)
{
    gpu->sample(0, texture_or_white(list, s->texture), s->filter != TIDE_FILTER_POINT);
}

// A step's 3D camera, from the world to clip space, fitted to the screen's
// shape: x divided by its width over its height.
static tide_float4x4 camera_3d_to_clip(const tide_draw_list *list, const draw_step *s)
{
    tide_float4x4 world_to_clip = s->camera ? list->matrices[s->camera - 1u]
                                            : tide_draw_camera_3d_matrix(tide_f3(0.0f, 0.0f, 0.0f), tide_identity_q(), 60.0f);
    if (!s->camera || s->fit) {
        const float across = (float)draw_height / (float)draw_width;
        world_to_clip.c0.x *= across, world_to_clip.c1.x *= across;
        world_to_clip.c2.x *= across, world_to_clip.c3.x *= across;
    }
    return world_to_clip;
}

// A camera as a matrix, from the units its commands are in to clip space:
// where to_screen puts a point, on the target.
static tide_float4x4 camera_to_clip(const camera *cam)
{
    const float x = 2.0f * cam->scale / (float)draw_width, y = 2.0f * cam->scale / (float)draw_height;
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
    block.engine.screen[0] = scale.x, block.engine.screen[1] = scale.y;
    block.engine.screen[2] = (float)(offscreen ? draw_height : pixel_height);
    block.engine.screen[3] = 0.0f;
    if (own) memcpy(block.bytes + sizeof(shaded_uniforms), list->uniforms + run->uniforms, own);
    gpu->use(s->pipeline);
    gpu->uniforms(&block, sizeof(shaded_uniforms) + own);
    for (uint32_t i = 0; i < shader->texture_count; i++) {
        gpu->sample(i, texture_or_white(list, run->textures[i]), (run->blended >> i & 1u) != 0);
    }
    gpu->vertices(0, g->vertices, 0);
    if (tide_shader_instance_size(shader)) gpu->vertices(1, gpu_state.instances, run->first);
    gpu->indices(g->indices, g->wide);
    gpu->draw_indexed(g->index_count, run->count);
}

static void draw_mesh_3d(const tide_draw_list *list, const draw_step *s)
{
    const gpu_mesh *g = &gpu_meshes[s->mesh - 1u];
    const tide_float4x4 world_to_clip = camera_3d_to_clip(list, s);
    gpu->use(gpu_state.meshes_3d);
    gpu->uniforms(&world_to_clip, sizeof world_to_clip);
    sample_step_texture(list, s);
    gpu->vertices(0, g->vertices, 0);
    gpu->vertices(1, gpu_state.matrices, (size_t)s->first * sizeof(tide_float4x4));
    gpu->indices(g->indices, g->wide);
    gpu->draw_indexed(g->index_count, s->count);
}

static int nearest_pixel(const float v)
{
    return (int)floorf(v + 0.5f);
}

static int clamp_pixel(const int v, const int most)
{
    return v < 0 ? 0 : v > most ? most : v;
}

// Only what's inside the step's rect draws from here on, or everything again.
static void clip(const draw_step *s)
{
    const int width = offscreen ? draw_width : pixel_width, height = offscreen ? draw_height : pixel_height;
    if (s->kind == STEP_NO_CLIP) {
        gpu->scissor(0, 0, width, height);
        return;
    }
    // In the target's pixels, and inside it
    const tide_float2 scale = pixel_scale();
    const int left = clamp_pixel(nearest_pixel(s->at.x * scale.x), width);
    const int right = clamp_pixel(nearest_pixel(s->to.x * scale.x), width);
    const int top = clamp_pixel(nearest_pixel(s->at.y * scale.y), height);
    const int bottom = clamp_pixel(nearest_pixel(s->to.y * scale.y), height);
    gpu->scissor(left, top, right > left ? right - left : 0, bottom > top ? bottom - top : 0);
}

static void select_target(const uint32_t at, const tide_gpu_id window)
{
    draw_target = at;
    draw_width = frame_targets[at].width;
    draw_height = frame_targets[at].height;
    offscreen = at != 0 || window != gpu_state.frame;
}

static screen_uniforms target_uniforms(void)
{
    const tide_float2 scale = pixel_scale();
    screen_uniforms u = {.screen = {[0] = 2.0f / (float)draw_width, [5] = -2.0f / (float)draw_height,
                                    [10] = 1.0f, [12] = -1.0f, [13] = 1.0f, [15] = 1.0f},
                         .scale = {scale.x, scale.y}};
    if (gpu_state.atlas) {
        u.texel[0] = 1.0f / (float)gpu_state.atlas_width;
        u.texel[1] = 1.0f / (float)gpu_state.atlas_height;
    }
    return u;
}

// Draws the list into `target`: the window for 0, or tide_platform_read_pixels'
// own pixels. With `over`, it goes over what's there (the overlay), and leaves
// the frame's camera, textures and meshes as they are.
static void draw_list(const tide_draw_list *list, const tide_gpu_id target, const bool over)
{
    if (!gpu_ready()) { // Between a device that was lost and the next
        if (!over) draw_calls = 0;
        return;
    }
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
    frame_targets[targets] = (frame_target){.id = TIDE_RENDER_INVALID, .width = 1, .height = 1}; // Invalid destination
    for (uint32_t i = 0; i < targets; i++) {
        frame_targets[i] = (frame_target){.through = {.scale = 1.0f}, .world = {.scale = 1.0f}, .fit = 1,
                                          .clipping = {.kind = STEP_NO_CLIP}};
        if (i) {
            const tide_draw_target_data *t = &list->targets[i - 1u];
            frame_targets[i].id = t->id;
            frame_targets[i].version = t->version;
            frame_targets[i].width = t->width;
            frame_targets[i].height = t->height;
            frame_targets[i].valid = tide_render_target_live(t->id) && t->width > 0 && t->height > 0;
        } else {
            frame_targets[i].gpu = target;
            frame_targets[i].width = screen_width;
            frame_targets[i].height = screen_height;
            frame_targets[i].valid = true;
        }
    }
    select_target(0, target);
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
        switch ((tide_draw_kind)c->kind) {
        case TIDE_DRAW_TARGET:
            select_target(c->target < targets ? c->target : targets, target);
            add_step(STEP_TARGET);
            break;
        case TIDE_DRAW_CLEAR:
            add_step(STEP_CLEAR)->color = color;
            break;
        case TIDE_DRAW_CAMERA:
            cam->center = c->a;
            cam->scale = c->b.x > 0.0f ? (float)draw_height / (2.0f * c->b.x) : 1.0f;
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
            f->camera_3d = c->camera.matrix + 1u;
            f->fit = c->camera.fit;
            break;
        case TIDE_DRAW_MESH_3D: {
            if (!c->mesh3.mesh || c->mesh3.mesh > list->mesh_count) break; // Forgotten (see tide_draw_forget)
            if (list->meshes[c->mesh3.mesh - 1u].space == TIDE_MESH_BYTES) break; // A shader's: not these vertices
            draw_step *s = add_step(STEP_MESH_3D);
            s->first = c->mesh3.first;
            s->count = c->mesh3.count;
            s->texture = c->mesh3.texture;
            s->filter = c->mesh3.filter;
            s->mesh = c->mesh3.mesh;
            s->camera = f->camera_3d;
            s->fit = f->fit;
            break;
        }
        case TIDE_DRAW_SHADED:
        case TIDE_DRAW_BLIT: {
            if (!run_draws(list, c->shaded.run)) break;
            if (c->kind == TIDE_DRAW_BLIT && list->runs[c->shaded.run].shader->dims != 2) break;
            draw_step *s = add_step(c->kind == TIDE_DRAW_BLIT ? STEP_BLIT : STEP_SHADED);
            s->first = c->shaded.run;
            s->camera = f->camera_3d;
            s->fit = f->fit;
            s->through = *cam;
            break;
        }
        case TIDE_DRAW_CLIP: {
            // Its corners on the screen, whichever way up the units are
            const tide_float2 p = to_screen(cam, c->a);
            const tide_float2 q = to_screen(cam, tide_f2(c->a.x + c->b.x, c->a.y + c->b.y));
            draw_step *s = add_step(STEP_CLIP);
            s->at = tide_f2(p.x < q.x ? p.x : q.x, p.y < q.y ? p.y : q.y);
            s->to = c->b.x > 0.0f && c->b.y > 0.0f ? tide_f2(p.x < q.x ? q.x : p.x, p.y < q.y ? q.y : p.y) : s->at;
            break;
        }
        case TIDE_DRAW_NO_CLIP:
            add_step(STEP_NO_CLIP);
            break;
        case TIDE_DRAW_CIRCLE:
        case TIDE_DRAW_WIRE_CIRCLE: {
            const tide_float2 p = to_screen(cam, c->a);
            const float r = c->b.x * cam->scale;
            add_shape(c->kind == TIDE_DRAW_CIRCLE ? SHAPE_CIRCLE : SHAPE_WIRE_CIRCLE, p.x, p.y, r, r, color, NULL);
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
                      hh, color, NULL);
            break;
        }
        case TIDE_DRAW_LINE: {
            const tide_float2 from = to_screen(cam, c->a);
            const tide_float2 to = to_screen(cam, c->b);
            add_shape(SHAPE_LINE, from.x, from.y, to.x, to.y, color, NULL);
            break;
        }
        case TIDE_DRAW_TEXT:
            add_text(list->text + c->text, to_screen(cam, c->a), c->b.x * cam->scale, color);
            break;
        }
    }
    if (!over) last_camera = frame_targets[0].world;

    // What the steps draw from goes to the GPU first
    renderer_start();
    upload_shapes_and_meshes();
    upload_meshes_3d(list);
    const bool shaded = upload_shaded(list);
    upload_atlas();
    select_target(0, target);
    screen_uniforms uniforms = target_uniforms();
    gpu->begin(target, !over); // Every frame starts black; Draw2D.Clear picks another color
    bool pass_active = true;
    uint32_t calls = 0; // The steps that draw, a draw call each
    for (uint32_t i = 0; i < step_count; i++) {
        const draw_step *s = &steps[i];
        if (s->target != draw_target) {
            if (pass_active) gpu->end();
            pass_active = false;
            select_target(s->target, target);
            frame_target *f = &frame_targets[draw_target];
            if (!f->valid) {
                fprintf(stderr, "draw skipped: render target %llu is invalid or disposed\n", (unsigned long long)f->id);
                continue;
            }
            if (draw_target) {
                const gpu_target *g = target_for(f->id, f->width, f->height, f->version);
                f->gpu = g ? g->target : 0;
            }
            gpu->begin(f->gpu, false);
            pass_active = true;
            uniforms = target_uniforms();
            clip(&frame_targets[draw_target].clipping);
        }
        if (!frame_targets[draw_target].valid) continue;
        if (samples_unavailable(list, s)) {
            fprintf(stderr, "draw skipped: source render target is invalid or disposed\n");
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
            gpu->begin(frame_targets[draw_target].gpu, false);
            clip(&frame_targets[draw_target].clipping);
        }
        switch (s->kind) {
        case STEP_TARGET:
            break;
        case STEP_SHAPES:
            // A run with smooth shapes goes through the pipeline that draws every kind, with their extras
            gpu->use(s->smooth ? gpu_state.smooth_shapes : gpu_state.shapes);
            gpu->uniforms(&uniforms, sizeof uniforms);
            gpu->vertices(0, gpu_state.corners, 0);
            gpu->vertices(1, gpu_state.shape_instances, (size_t)s->first * sizeof(shape));
            if (s->smooth) gpu->vertices(2, gpu_state.shape_extras, (size_t)(s->smooth - 1u) * sizeof(shape_extra));
            gpu->draw(0, 6, s->count);
            calls++;
            break;
        case STEP_CLEAR: {
            // A triangle over the target, which the clip cuts, and which sets depth back too
            const float color[4] = {(float)s->color.r / 255.0f, (float)s->color.g / 255.0f, (float)s->color.b / 255.0f,
                                    (float)s->color.a / 255.0f};
            gpu->use(gpu_state.clear);
            gpu->uniforms(color, sizeof color);
            gpu->draw(0, 3, 1);
            calls++;
            break;
        }
        case STEP_MESH:
            if (s->count == 0) break;
            gpu->use(gpu_state.meshes);
            gpu->uniforms(&uniforms, sizeof uniforms);
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
            gpu->use(gpu_state.text);
            gpu->uniforms(&uniforms, sizeof uniforms);
            gpu->sample(0, gpu_state.atlas, true); // Stretched glyphs blend; the others land on the target's pixels
            gpu->vertices(0, gpu_state.mesh_vertices, 0);
            gpu->draw(s->first, s->count, 1);
            calls++;
            break;
        }
    }
    if (pass_active) gpu->end();
    offscreen = false;
    draw_width = draw_height = 0;
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
// the first time and again when the device is another.
static tide_gpu_id frame_destination(void)
{
    if (!headless || !gpu_ready()) return 0;
    if (!gpu_state.frame) {
        gpu_state.frame = gpu->target(pixel_width, pixel_height);
        gpu_state.frame_width = pixel_width;
        gpu_state.frame_height = pixel_height;
    }
    return gpu_state.frame;
}

void tide_platform_draw(const tide_draw_list *list)
{
    // Nobody sees it while the window is minimized or, on the web, the page
    // is hidden, and frames go on meanwhile. A headless window's are for
    // whoever reads them.
    if (headless || !tide_window_unseen()) draw_list(list, frame_destination(), false);
}

tide_float2 tide_platform_world_to_screen(const tide_float2 world)
{
    return to_screen(&last_camera, world);
}

tide_float2 tide_platform_screen_size(void)
{
    return tide_f2((float)screen_width, (float)screen_height);
}

// As add_text draws it: on the window's pixels, or a render target's own.
tide_float2 tide_platform_measure_text(const char *text, const float size, const bool target)
{
    const tide_float2 scale = target ? tide_f2(1.0f, 1.0f) : window_scale();
    tide_float2 measured;
    tide_font_size(text, size, scale.x, scale.y, &measured.x, &measured.y);
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
        widths[count] = ceilf(tide_font_measure(lines[count], size, window_scale().y));
        if (widths[count] > widest) widest = widths[count];
        line = end ? end + 1 : NULL;
    }
    // A panel in the bottom right corner, each line against its right edge
    const float right = (float)screen_width - 12.0f;
    const float top = (float)screen_height - 12.0f - (float)count * pitch;
    const tide_float2 panel = tide_f2(widest + 16.0f, (float)count * pitch + 8.0f);
    tide_draw_reset(&overlay);
    tide_draw_screen(&overlay);
    tide_draw_rect(&overlay, tide_f2(right + 8.0f - panel.x * 0.5f, top - 6.0f + panel.y * 0.5f), panel,
                   (tide_color){0.0f, 0.0f, 0.0f, 140.0f / 255.0f});
    for (int i = 0; i < count; i++) {
        tide_draw_text(&overlay, lines[i], tide_f2(right - widths[i], top + (float)i * pitch), size,
                       (tide_color){200.0f / 255.0f, 200.0f / 255.0f, 200.0f / 255.0f, 1.0f});
    }
    draw_list(&overlay, frame_destination(), true);
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
    const int width = screen_width, height = screen_height;
    if (!gpu_ready()) { // Nothing to draw with for now: no pixels
        memset(out, 0, (size_t)(count > 0 ? count : 0) * sizeof *out);
        return;
    }
    if (!gpu_state.target || gpu_state.target_width != width || gpu_state.target_height != height) {
        if (gpu_state.target) gpu->target_free(gpu_state.target);
        gpu_state.target = gpu->target(width, height);
        gpu_state.target_pixels =
            tide_realloc(gpu_state.target_pixels, (size_t)gpu_state.target_width * (size_t)gpu_state.target_height * 4u,
                         (size_t)width * (size_t)height * 4u);
        gpu_state.target_width = width;
        gpu_state.target_height = height;
    }
    draw_list(list, gpu_state.target, false);
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
