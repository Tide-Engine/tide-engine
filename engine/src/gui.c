#include "tide/gui.h"

#include <float.h>
#include <stdio.h>

#include "tide/text.h"
#include "tide/time.h"
#include <stdlib.h>
#include <string.h>

// See tide/gui.h. The GUI is local: it runs on one machine and never touches
// the simulation, so it's free to use the C library (formatting and parsing
// numbers) and to depend on the window's size.

// Sizes, in GUI units (pixels). The theme's are read where `g` is the GUI.
#define FONT (g->theme.text_size)         // Text's size
#define LINE (g->theme.text_size + 12.0f) // A widget's height
#define PAD 10.0f                         // Text inside a button, from its sides
#define SPACING (g->theme.spacing)        // Between widgets
#define AREA_PADDING (g->theme.padding)   // Inside an area, around its content
#define WHEEL_LINE 28.0f                  // How far a step of the mouse's wheel scrolls
#define AREA_MARGIN 12.0f  // Between an anchored area and the screen's edges
#define LABEL_WIDTH 130.0f // A labelled widget's label, at least
#define SLIDER_WIDTH 180.0f
#define VALUE_WIDTH 56.0f  // A slider's value, on its right
#define FIELD_WIDTH 90.0f
#define PART_WIDTH 68.0f   // Each number of a vector field
#define COLOR_PART_WIDTH 52.0f
#define BOX (g->theme.text_size) // A toggle's box
#define BORDER 2.0f        // The focus's outline

// Squeezed, when there isn't room: as narrow as these go
#define LEAST_FIELD 48.0f  // A number field, or half a text field
#define LEAST_TRACK 40.0f  // A slider's track
#define LEAST_PART 36.0f   // Each number of a vector or color field

static const tide_color DIM = {0.0f, 0.0f, 0.0f, 0.5f}; // Over the screen, under a modal
static const float FADED = 0.5f; // How opaque a disabled widget is, as in Unity's IMGUI

// The look a frame starts with. Tide's GUITheme has these as its fields'
// defaults (add_builtins in the compiler's check.c): a change to one goes to
// the other.
tide_gui_theme tide_gui_theme_default(void)
{
    return (tide_gui_theme){
        .text = {0.93f, 0.93f, 0.95f, 1.0f},
        .panel = {0.07f, 0.07f, 0.1f, 0.9f},
        .control = {0.2f, 0.2f, 0.25f, 1.0f},
        .hot = {0.28f, 0.28f, 0.34f, 1.0f},
        .pressed = {0.14f, 0.14f, 0.18f, 1.0f},
        .field = {0.12f, 0.12f, 0.15f, 1.0f},
        .field_hot = {0.16f, 0.16f, 0.2f, 1.0f},
        .accent = {1.0f, 0.77f, 0.24f, 1.0f},
        .radius = 0.0f,
        .border = {1.0f, 1.0f, 1.0f, 0.12f},
        .border_width = 0.0f,
        .shading = 0.0f,
        .shadow = {0.0f, 0.0f, 0.0f, 0.35f},
        .shadow_size = 16.0f,
        .transition = 0.1f,
        .text_size = 16.0f,
        .padding = 12.0f,
        .spacing = 5.0f,
    };
}

// A theme's number as the GUI can lay out and draw with it: one that isn't a
// number is `otherwise`, and one out of its range the nearest that's in it.
static float theme_number(const float v, const float least, const float most, const float otherwise)
{
    if (!tide_is_finite_f(v)) return otherwise;
    return v < least ? least : v > most ? most : v;
}

static tide_gui_theme fitted_theme(tide_gui_theme t)
{
    const tide_gui_theme d = tide_gui_theme_default();
    t.radius = theme_number(t.radius, 0.0f, 1e6f, 0.0f);
    t.border_width = theme_number(t.border_width, 0.0f, 1e6f, 0.0f);
    t.shading = theme_number(t.shading, 0.0f, 1.0f, 0.0f);
    t.shadow_size = theme_number(t.shadow_size, 0.0f, 1e4f, 0.0f);
    t.transition = theme_number(t.transition, 0.0f, 60.0f, 0.0f);
    t.text_size = theme_number(t.text_size, 1.0f, 512.0f, d.text_size);
    t.padding = theme_number(t.padding, 0.0f, 1e4f, d.padding);
    t.spacing = theme_number(t.spacing, 0.0f, 1e4f, d.spacing);
    return t;
}

enum { VERTICAL, HORIZONTAL, AREA, SCROLL };

#define ROOT_ID 1u // The screen's own group, where widgets outside any container go

// Navigation keys, in tide_gui.keys
enum {
    KEY_UP = 1u << 0,
    KEY_DOWN = 1u << 1,
    KEY_LEFT = 1u << 2,
    KEY_RIGHT = 1u << 3,
    PAD_UP = 1u << 4, // The d-pad and the left stick
    PAD_DOWN = 1u << 5,
    PAD_LEFT = 1u << 6,
    PAD_RIGHT = 1u << 7,
    KEY_TAB = 1u << 8,
    KEY_ENTER = 1u << 9,
    KEY_SPACE = 1u << 10,
    PAD_ACCEPT = 1u << 11, // South
    KEY_CANCEL = 1u << 12, // Escape
    PAD_CANCEL = 1u << 13, // East
    KEY_BACKSPACE = 1u << 14,
    KEY_SHIFT = 1u << 15,
    KEY_DELETE = 1u << 16,
    KEY_HOME = 1u << 17,
    KEY_END = 1u << 18,
    KEY_CTRL = 1u << 19,
    KEY_A = 1u << 20,
    KEY_Z = 1u << 21,
    KEY_Y = 1u << 22,
    KEY_C = 1u << 23,
    KEY_X = 1u << 24,
};

#define STICK 0.5f // How far the left stick goes before it counts as a d-pad press

static tide_rect on_screen(const tide_gui *g, tide_rect r);

static uint32_t nav_keys(const tide_devices *d)
{
    const tide_keyboard *k = &d->keyboard;
    const tide_gamepad *p = &d->gamepad;
    uint32_t keys = 0;
    if (k->upArrow.held) keys |= KEY_UP;
    if (k->downArrow.held) keys |= KEY_DOWN;
    if (k->leftArrow.held) keys |= KEY_LEFT;
    if (k->rightArrow.held) keys |= KEY_RIGHT;
    if (k->tab.held) keys |= KEY_TAB;
    if (k->enter.held || k->numpadEnter.held) keys |= KEY_ENTER;
    if (k->space.held) keys |= KEY_SPACE;
    if (k->escape.held) keys |= KEY_CANCEL;
    if (k->backspace.held) keys |= KEY_BACKSPACE;
    if (k->leftShift.held || k->rightShift.held) keys |= KEY_SHIFT;
    if (k->delete.held) keys |= KEY_DELETE;
    if (k->home.held) keys |= KEY_HOME;
    if (k->end.held) keys |= KEY_END;
    if (k->leftCtrl.held || k->rightCtrl.held) keys |= KEY_CTRL;
    if (k->a.held) keys |= KEY_A;
    if (k->z.held) keys |= KEY_Z;
    if (k->y.held) keys |= KEY_Y;
    if (k->c.held) keys |= KEY_C;
    if (k->x.held) keys |= KEY_X;
    if (p->connected) {
        if (p->dpad.up.held || p->leftStick.y > STICK) keys |= PAD_UP;
        if (p->dpad.down.held || p->leftStick.y < -STICK) keys |= PAD_DOWN;
        if (p->dpad.left.held || p->leftStick.x < -STICK) keys |= PAD_LEFT;
        if (p->dpad.right.held || p->leftStick.x > STICK) keys |= PAD_RIGHT;
        if (p->buttonSouth.held) keys |= PAD_ACCEPT;
        if (p->buttonEast.held) keys |= PAD_CANCEL;
    }
    return keys;
}

static bool pressed(const tide_gui *g, const uint32_t keys)
{
    return (g->keys_pressed & keys) != 0;
}

static float max_f(const float a, const float b)
{
    return a > b ? a : b;
}

static float min_f(const float a, const float b)
{
    return a < b ? a : b;
}

static float clamp_f(const float v, const float lo, const float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static float text_width(const tide_gui *g, const char *text)
{
    return g->measure(text, FONT, NULL, TIDE_FONT_REGULAR).x; // The GUI draws in the window
}

// ---------------------------------------------------------------------------
// Drawing

static tide_rect grow(const tide_rect r, const float by)
{
    return (tide_rect){r.x - by, r.y - by, r.width + 2.0f * by, r.height + 2.0f * by};
}

static bool contains(const tide_rect r, const tide_float2 p)
{
    return p.x >= r.x && p.y >= r.y && p.x < r.x + r.width && p.y < r.y + r.height;
}

static tide_rect intersect(const tide_rect a, const tide_rect b)
{
    const float x = max_f(a.x, b.x), y = max_f(a.y, b.y);
    return (tide_rect){x, y, max_f(min_f(a.x + a.width, b.x + b.width) - x, 0.0f),
                            max_f(min_f(a.y + a.height, b.y + b.height) - y, 0.0f)};
}

void tide_gui_focus_next(tide_gui *g) { g->focus_next = true; }
void tide_gui_clear_focus(tide_gui *g) { g->focus = 0; g->focus_next = false; }

// Whether widgets drawn now are in a Disabled block.
static bool grayed(const tide_gui *g)
{
    return g->groups[g->depth].disabled;
}

// A widget's color: faded in a Disabled block.
static tide_color shade(const tide_gui *g, tide_color color)
{
    if (grayed(g)) color.a *= FADED;
    return color;
}

// A rect in one color, its corners round by `radius`. Square ones are the
// plain rects the GUI always drew, which fill whole pixels.
static void paint(tide_gui *g, const tide_rect r, const tide_color color, const float radius)
{
    const tide_float2 center = tide_f2(r.x + r.width * 0.5f, r.y + r.height * 0.5f);
    if (radius > 0.0f) tide_draw_round_rect(&g->list, center, tide_f2(r.width, r.height), radius, color);
    else tide_draw_rect(&g->list, center, tide_f2(r.width, r.height), color);
}

// Part of a widget, its corners the theme's.
static void fill(tide_gui *g, const tide_rect r, const tide_color color)
{
    paint(g, r, shade(g, color), g->theme.radius);
}

// A part `by` around another, or inside it for less than 0: the focus's
// outline, a toggle's check. Its corners go around the other's.
static void fill_around(tide_gui *g, const tide_rect r, const float by, const tide_color color)
{
    paint(g, grow(r, by), shade(g, color), g->theme.radius > 0.0f ? max_f(g->theme.radius + by, 0.0f) : 0.0f);
}

// `color` darkened by `by`, 0 to 1, keeping its alpha.
static tide_color darker(const tide_color color, const float by)
{
    const float keep = 1.0f - by;
    return (tide_color){color.r * keep, color.g * keep, color.b * keep, color.a};
}

// A widget's or an area's body, its corners round by `radius`, with the
// theme's border and shading where it has them. With neither, it's the rect
// paint draws.
static void body_paint(tide_gui *g, const tide_rect r, const tide_color color, const float radius)
{
    const tide_gui_theme *t = &g->theme;
    if (!(t->border_width > 0.0f) && !(t->shading > 0.0f)) {
        paint(g, r, color, radius);
        return;
    }
    tide_shape s = TIDE_SHAPE_WHITE;
    s.center = tide_f2(r.x + r.width * 0.5f, r.y + r.height * 0.5f);
    s.size = tide_f2(r.width, r.height);
    s.radius = radius;
    s.color = color;
    s.color2 = darker(color, t->shading);
    s.outline = t->border_width;
    s.outline_color = t->border;
    if (grayed(g)) s.outline_color.a *= FADED;
    tide_draw_shape(&g->list, s);
}

static float animate_lanes(tide_gui *g, uint32_t id, const float *target, float *out, int n, float seconds,
                           const float *from);

// A widget's body in `color`, which it fades to from the color it had, over
// the theme's transition. `id` is the widget's.
static void body(tide_gui *g, const uint32_t id, const tide_rect r, const tide_color color)
{
    const float target[4] = {color.r, color.g, color.b, color.a};
    float now[4];
    animate_lanes(g, tide_gui_seed(id, 0xB0D1u), target, now, 4, g->theme.transition, NULL);
    body_paint(g, r, shade(g, (tide_color){now[0], now[1], now[2], now[3]}), g->theme.radius);
}

// Text starting at `x`, centered on `middle` vertically.
static void text_at(tide_gui *g, const char *text, const float x, const float middle, const tide_color color)
{
    if (text[0]) tide_draw_text(&g->list, text, tide_f2(x, middle - FONT * 0.5f), FONT, shade(g, color));
}

static void text_centered(tide_gui *g, const char *text, const tide_rect r, const tide_color color)
{
    text_at(g, text, r.x + (r.width - text_width(g, text)) * 0.5f, r.y + r.height * 0.5f, color);
}

// ---------------------------------------------------------------------------
// IDs and remembered sizes

uint32_t tide_gui_seed(const uint32_t a, const uint32_t b)
{
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u + (a << 6) + (a >> 2));
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h ? h : 1u;
}

uint32_t tide_gui_id(tide_gui *g, const uint32_t seed, const uint32_t site)
{
    const uint32_t key = tide_gui_seed(g->key ? tide_gui_seed(seed, g->key) : seed, site);
    uint32_t slot = key & (TIDE_GUI_MAX_IDS - 1);
    for (uint32_t probe = 0; probe < TIDE_GUI_MAX_IDS; probe++) {
        tide_gui_seen *s = &g->seen[slot];
        if (s->frame != g->frame) { // Free this frame
            *s = (tide_gui_seen){key, g->frame, 1};
            return key;
        }
        if (s->key == key) return tide_gui_seed(key, s->count++);
        slot = (slot + 1) & (TIDE_GUI_MAX_IDS - 1);
    }
    return key; // Full: widgets from one site share an ID
}

// ---------------------------------------------------------------------------
// Animated values (GUI.Animate, and widgets' fades)

// The slot of the value `id`, found or made, or NULL with none to spare.
// `fresh` says it starts over: it's new, or it skipped a frame. A slot nothing
// asked for this frame or the last is free to take, the oldest first.
static tide_gui_motion *motion(tide_gui *g, const uint32_t id, bool *fresh)
{
    uint32_t slot = id & (TIDE_GUI_MAX_MOTIONS - 1), free_slot = UINT32_MAX;
    for (uint32_t probe = 0; probe < TIDE_GUI_MAX_MOTIONS; probe++) {
        tide_gui_motion *m = &g->motions[slot];
        if (m->id == id && m->frame != 0) {
            *fresh = g->frame - m->frame > 1u;
            return m;
        }
        if (m->frame == 0) { // Never used: the end of the values that probed past it
            if (free_slot == UINT32_MAX) free_slot = slot;
            break;
        }
        if (g->frame - m->frame > 1u && (free_slot == UINT32_MAX || m->frame < g->motions[free_slot].frame))
            free_slot = slot;
        slot = (slot + 1) & (TIDE_GUI_MAX_MOTIONS - 1);
    }
    if (free_slot == UINT32_MAX) return NULL;
    *fresh = true;
    g->motions[free_slot].id = id;
    return &g->motions[free_slot];
}

// Eases `n` floats toward `target` and writes where they are now to `out`:
// about 99% of the way in `seconds`, moving fastest at first and settling
// without overshooting, so a target that changes midway turns smoothly. A
// fresh value starts at `from`, or at its target with none. Returns out[0].
static float animate_lanes(tide_gui *g, const uint32_t id, const float *target, float *out, const int n,
                           const float seconds, const float *from)
{
    bool fresh = true;
    tide_gui_motion *m = motion(g, id, &fresh);
    // The share of the way left that this frame covers: 4.6 is -ln(0.01)
    const float dt = g->seconds > 0.0f && tide_is_finite_f(g->seconds) ? g->seconds : 0.0f;
    const float step = dt > 0.0f && seconds > 0.0f && tide_is_finite_f(seconds) ? 1.0f - tide_exp_f(-dt * 4.60517f / seconds) : 1.0f;
    for (int i = 0; i < n; i++) {
        const float to = target[i];
        float v = to;
        if (fresh && from) v = from[i]; // Where it starts, shown as it is this frame
        else if (!fresh) {
            v = m->value[i] + (to - m->value[i]) * step;
            if (tide_abs_f(to - v) <= 1e-4f * max_f(1.0f, tide_abs_f(to))) v = to;
        }
        if (!tide_is_finite_f(v)) v = to;
        out[i] = v;
        if (m) m->value[i] = tide_is_finite_f(v) ? v : 0.0f;
    }
    if (m) m->frame = g->frame;
    return out[0];
}

// A tween's lanes: where it is, the target it's going to, and how fast it goes there.
static float tween(tide_gui *g, const uint32_t id, const float target, const float seconds, const float *from)
{
    bool fresh = true;
    tide_gui_motion *m = motion(g, tide_gui_seed(id, 0x7EE7u), &fresh);
    const float to = tide_is_finite_f(target) ? target : 0.0f;
    const float dt = g->seconds > 0.0f && tide_is_finite_f(g->seconds) ? g->seconds : 0.0f;
    float v = to;
    if (m && !fresh) {
        v = m->value[0];
        if (m->value[1] != to) { // A new target: from rest, as far as it is in `seconds`; turned midway, as fast as it went
            const bool resting = v == m->value[1] || !(m->value[2] > 0.0f);
            if (resting) m->value[2] = seconds > 0.0f && tide_is_finite_f(seconds) ? tide_abs_f(to - v) / seconds : 0.0f;
            m->value[1] = to;
        }
        const float step = m->value[2] * dt;
        // Arrives on time: a last step short by rounding alone arrives too
        const float slack = 1e-5f * max_f(1.0f, tide_abs_f(to));
        if (!(m->value[2] > 0.0f) || !(dt > 0.0f) || tide_abs_f(to - v) <= step + slack) v = to;
        else v += to > v ? step : -step;
    } else if (from && tide_is_finite_f(*from)) {
        v = *from;
        if (m) m->value[2] = seconds > 0.0f && tide_is_finite_f(seconds) ? tide_abs_f(to - v) / seconds : 0.0f;
    }
    if (!tide_is_finite_f(v)) v = to;
    if (m) {
        m->value[0] = v;
        m->value[1] = to;
        if (fresh && !from) m->value[2] = 0.0f;
        m->frame = g->frame;
    }
    return v;
}

float tide_gui_tween(tide_gui *g, const uint32_t id, const float target, const float seconds)
{
    return tween(g, id, target, seconds, NULL);
}

float tide_gui_tween_from(tide_gui *g, const uint32_t id, const float target, const float seconds, const float from)
{
    return tween(g, id, target, seconds, &from);
}

float tide_gui_animate(tide_gui *g, const uint32_t id, const float target, const float seconds)
{
    float out;
    return animate_lanes(g, id, &target, &out, 1, seconds, NULL);
}

float tide_gui_animate_from(tide_gui *g, const uint32_t id, const float target, const float seconds, const float from)
{
    float out;
    return animate_lanes(g, id, &target, &out, 1, seconds, &from);
}

tide_float2 tide_gui_animate_float2(tide_gui *g, const uint32_t id, const tide_float2 target, const float seconds)
{
    const float to[2] = {target.x, target.y};
    float out[2];
    animate_lanes(g, id, to, out, 2, seconds, NULL);
    return tide_f2(out[0], out[1]);
}

tide_float2 tide_gui_animate_float2_from(tide_gui *g, const uint32_t id, const tide_float2 target, const float seconds,
                                         const tide_float2 from)
{
    const float to[2] = {target.x, target.y}, start[2] = {from.x, from.y};
    float out[2];
    animate_lanes(g, id, to, out, 2, seconds, start);
    return tide_f2(out[0], out[1]);
}

tide_color tide_gui_animate_color(tide_gui *g, const uint32_t id, const tide_color target, const float seconds)
{
    const float to[4] = {target.r, target.g, target.b, target.a};
    float out[4];
    animate_lanes(g, id, to, out, 4, seconds, NULL);
    return (tide_color){out[0], out[1], out[2], out[3]};
}

tide_color tide_gui_animate_color_from(tide_gui *g, const uint32_t id, const tide_color target, const float seconds,
                                       const tide_color from)
{
    const float to[4] = {target.r, target.g, target.b, target.a}, start[4] = {from.r, from.g, from.b, from.a};
    float out[4];
    animate_lanes(g, id, to, out, 4, seconds, start);
    return (tide_color){out[0], out[1], out[2], out[3]};
}

static const tide_gui_size *remembered(const tide_gui *g, const uint32_t id)
{
    uint32_t slot = id & (TIDE_GUI_MAX_SIZES - 1);
    for (uint32_t probe = 0; probe < TIDE_GUI_MAX_SIZES; probe++) {
        const tide_gui_size *s = &g->sizes[slot];
        if (s->frame == 0) return NULL;
        if (s->id == id) return s;
        slot = (slot + 1) & (TIDE_GUI_MAX_SIZES - 1);
    }
    return NULL;
}

static void remember(tide_gui *g, const uint32_t id, const tide_float2 size, const float natural, const float least)
{
    uint32_t slot = id & (TIDE_GUI_MAX_SIZES - 1), oldest = slot;
    tide_gui_size *place = NULL;
    for (uint32_t probe = 0; probe < TIDE_GUI_MAX_SIZES; probe++) {
        tide_gui_size *s = &g->sizes[slot];
        if (s->frame == 0 || s->id == id) {
            place = s;
            break;
        }
        if (g->frame - s->frame > g->frame - g->sizes[oldest].frame) oldest = slot;
        slot = (slot + 1) & (TIDE_GUI_MAX_SIZES - 1);
    }
    // Only a full cache evicts a container, preferably one not used recently.
    // Occupied slots remain occupied, so lookup can still stop at an empty one.
    if (!place) place = &g->sizes[oldest];
    *place = (tide_gui_size){id, g->frame, size, natural, least};
}

// ---------------------------------------------------------------------------
// Frames

// A button as views see it: down or up since last frame.
static tide_button frame_button(const tide_button now, const tide_button last)
{
    return (tide_button){.pressed = now.held, .down = now.held && !last.held, .up = !now.held && last.held, .held = now.held};
}

// A touch as views see it. A finger that lifted shows its last frame with its
// id, and one that came in its slot since shows as new.
static void frame_touch(tide_touch *d, const tide_touch *now, const tide_touch *last)
{
    const bool same = now->id == last->id;
    const tide_button before = same ? last->press : (tide_button){0};
    if (now->id == 0 && last->press.held) { // It lifted, and a sample cleared its slot
        *d = *last;
        d->press = frame_button((tide_button){0}, last->press);
        d->delta = tide_f2(0.0f, 0.0f);
        return;
    }
    *d = *now;
    d->press = frame_button(now->press, before);
    d->delta = now->poll_delta;
}

// The devices views read this frame, from the platform's.
static void frame_devices(tide_gui *g, const tide_devices *now)
{
    tide_devices *d = &g->devices;
    const tide_devices *last = &g->last;
    memset(d, 0, sizeof *d);
#define FRAME_KEY(name) d->keyboard.name = frame_button(now->keyboard.name, last->keyboard.name);
#define FRAME_MOUSE(name) d->mouse.name = frame_button(now->mouse.name, last->mouse.name);
#define FRAME_PAD(name) d->gamepad.name = frame_button(now->gamepad.name, last->gamepad.name);
#define FRAME_DPAD(name) d->gamepad.dpad.name = frame_button(now->gamepad.dpad.name, last->gamepad.dpad.name);
    TIDE_KEYBOARD_KEYS(FRAME_KEY)
    TIDE_MOUSE_BUTTONS(FRAME_MOUSE)
    TIDE_GAMEPAD_BUTTONS(FRAME_PAD)
    TIDE_DPAD_BUTTONS(FRAME_DPAD)
#undef FRAME_KEY
#undef FRAME_MOUSE
#undef FRAME_PAD
#undef FRAME_DPAD
    d->keyboard.text = now->keyboard.text;
    d->mouse.position = now->mouse.position;
    d->mouse.delta = now->mouse.poll_delta;
    d->mouse.scroll = now->mouse.poll_scroll;
    d->mouse.locked = now->mouse.locked;
    d->gamepad.connected = now->gamepad.connected;
    d->gamepad.leftStick = now->gamepad.leftStick;
    d->gamepad.rightStick = now->gamepad.rightStick;
    d->gamepad.leftTrigger = now->gamepad.leftTrigger;
    d->gamepad.rightTrigger = now->gamepad.rightTrigger;
    d->touchscreen.connected = now->touchscreen.connected;
    frame_touch(&d->touchscreen.primaryTouch, &now->touchscreen.primaryTouch, &last->touchscreen.primaryTouch);
    for (int i = 0; i < TIDE_TOUCHES; i++) {
        frame_touch(&d->touchscreen.touches.at[i], &now->touchscreen.touches.at[i], &last->touchscreen.touches.at[i]);
    }
    d->pointer.position = now->pointer.position;
    d->pointer.delta = now->pointer.poll_delta;
    d->pointer.press = frame_button(now->pointer.press, last->pointer.press);
    d->pointer.touch = now->pointer.touch;
    g->last = *now;
}

#define RELEASE(b) (b) = (tide_button){0};

// The keys, and what's typed.
static void hide_keyboard(tide_devices *d)
{
#define RELEASE_KEY(name) RELEASE(d->keyboard.name)
    TIDE_KEYBOARD_KEYS(RELEASE_KEY)
#undef RELEASE_KEY
    d->keyboard.text = (tide_typed){0};
}

static void hide_gamepad(tide_devices *d)
{
#define RELEASE_PAD(name) RELEASE(d->gamepad.name)
#define RELEASE_DPAD(name) RELEASE(d->gamepad.dpad.name)
    TIDE_GAMEPAD_BUTTONS(RELEASE_PAD)
    TIDE_DPAD_BUTTONS(RELEASE_DPAD)
#undef RELEASE_PAD
#undef RELEASE_DPAD
    d->gamepad.leftStick = d->gamepad.rightStick = tide_f2(0.0f, 0.0f);
    d->gamepad.leftTrigger = d->gamepad.rightTrigger = 0.0f;
}

// The mouse's buttons and scroll, and the pointer's press.
static void hide_pointer(tide_devices *d)
{
#define RELEASE_MOUSE(name) RELEASE(d->mouse.name)
    TIDE_MOUSE_BUTTONS(RELEASE_MOUSE)
#undef RELEASE_MOUSE
    d->mouse.scroll = tide_f2(0.0f, 0.0f);
    // The finger that's the pointer: the primary touch, in its slot too.
    RELEASE(d->pointer.press)
    tide_touchscreen *s = &d->touchscreen;
    if (d->pointer.touch && s->primaryTouch.id != 0) {
        for (int i = 0; i < TIDE_TOUCHES; i++) {
            if (s->touches.at[i].id == s->primaryTouch.id) RELEASE(s->touches.at[i].press)
        }
        RELEASE(s->primaryTouch.press)
    }
}

// What moves the focus and clicks, which is the GUI's while a control has the
// focus: the rest of the keyboard and the gamepad is its view's.
static void hide_navigation(tide_keyboard *k, tide_gamepad *p)
{
    RELEASE(k->upArrow)
    RELEASE(k->downArrow)
    RELEASE(k->leftArrow)
    RELEASE(k->rightArrow)
    RELEASE(k->tab)
    RELEASE(k->enter)
    RELEASE(k->numpadEnter)
    RELEASE(k->space)
    RELEASE(k->escape)
#define RELEASE_DPAD(name) RELEASE(p->dpad.name)
    TIDE_DPAD_BUTTONS(RELEASE_DPAD)
#undef RELEASE_DPAD
    RELEASE(p->buttonSouth)
    RELEASE(p->buttonEast)
    p->leftStick = tide_f2(0.0f, 0.0f);
}

// Whether the pointer in `d` is a mouse whose cursor the window holds
// (Cursor.Lock): it's hidden, so it points at nothing.
static bool cursor_hidden(const tide_devices *d)
{
    return d->mouse.locked && !d->pointer.touch;
}

// Where the pointer in `d` is, in the GUI's pixels: false for a finger that's
// off the screen and for a mouse whose cursor is locked, which are nowhere.
static bool pointer_place(const tide_gui *g, const tide_devices *d, tide_float2 *at)
{
    const tide_pointer *p = &d->pointer;
    if (cursor_hidden(d)) return false;
    if (p->touch && !p->press.held && !p->press.pressed && !p->press.up) return false;
    *at = tide_f2(p->position.x, g->height - p->position.y);
    return true;
}

// Whether the pointer in `d` is on the GUI: on a widget or an area, where the
// last frame drew them. The game's samples and the views read a press before
// the frame's GUI sees it, so where the pointer is now says whose it is, not
// where the GUI last saw it: a finger that touches a button is the GUI's from
// the frame it lands in, and so is a click the mouse made as it came.
static bool on_gui(const tide_gui *g, const tide_devices *d)
{
    tide_float2 at;
    if (!pointer_place(g, d, &at)) return false;
    for (uint32_t i = 0; i < g->rect_count; i++) {
        if (contains(g->rects[i].rect, at)) return true;
    }
    return g->rects_full && g->over; // More widgets than fit: as the frame saw it
}

#define WIDGET UINT32_MAX // The `content` of a place that's a widget's: no place is its content

// Which widget or area is on top at `at`, in the GUI's pixels, where the last
// frame drew them: the one drawn last of those there, and an area only where
// none of its content is, as its panel is under it. 0 for none; else `where`
// is its place. It's what a frame works out as it draws (hovered,
// close_area), over again for a pointer that has moved since.
static uint32_t on_top(const tide_gui *g, const tide_float2 at, tide_rect *where, uint32_t *scroll)
{
    uint32_t id = 0;
    uint32_t found = 0; // How many places there are up to the one on top so far, with it
    for (uint32_t i = 0; i < g->rect_count; i++) {
        const tide_gui_rect *r = &g->rects[i];
        if (!contains(r->rect, at)) continue;
        if (r->content != WIDGET && found > r->content) continue; // An area with some of its content there
        id = r->id;
        *where = r->rect;
        *scroll = r->scroll;
        found = i + 1u;
    }
    return id;
}

// A key or a gamepad's button that goes down while the keyboard and the
// gamepad are the GUI's (a widget has the focus, a field is typed into, a modal
// is up) is the GUI's until it's let go: by when the press began, as a press
// on the stats' drawer is by where. The widget may be gone the frame after,
// as a menu is once its Play was clicked, with the key that clicked it still
// down: it would read as the game's from then.
//
// A frame notes the presses (keep_presses), as it sees each key go down. Views
// have one hidden until a frame has seen it up, that frame too; samples until
// one has, as a key's `pressed` and `up` stay in the platform's devices until
// a sample consumes them, which may be frames later.
#define KEPT_BUTTONS(X)                                                                  \
    TIDE_KEYBOARD_KEYS(X##_KEY) TIDE_GAMEPAD_BUTTONS(X##_PAD) TIDE_DPAD_BUTTONS(X##_DPAD)

static void keep_press(tide_gui_kept *kept, const tide_button now, const tide_button last, const bool ours)
{
    // A new press is the GUI's when it has the keyboard, and the game's when it
    // hasn't: whatever was kept of the key's last press is over
    if (now.held && !last.held) kept->views = kept->samples = ours;
    // Up, with nothing left for a sample to read: a sample consumed it, or the host did
    else if (!now.held && !now.pressed && !now.down && !now.up) kept->samples = false;
}

// Before a frame's devices are worked out: `now` are the platform's, and
// g->last the ones the last frame had.
static void keep_presses(tide_gui *g, const tide_devices *now)
{
    const bool ours = g->focus || g->editing || g->modal;
#define KEEP_KEY(name) keep_press(&g->kept_keys.name, now->keyboard.name, g->last.keyboard.name, ours);
#define KEEP_PAD(name) keep_press(&g->kept_pad.name, now->gamepad.name, g->last.gamepad.name, ours);
#define KEEP_DPAD(name) keep_press(&g->kept_pad.dpad.name, now->gamepad.dpad.name, g->last.gamepad.dpad.name, ours);
    KEPT_BUTTONS(KEEP)
#undef KEEP_KEY
#undef KEEP_PAD
#undef KEEP_DPAD
}

// Hides the kept ones from the devices views read this frame (`held` as the
// platform has it), and lets go of those that are up.
static void hide_kept_from_views(tide_gui *g, tide_devices *d)
{
#define VIEWS(kept, button)        \
    if ((kept).views) {            \
        const bool held = (button).held; \
        RELEASE(button)            \
        (kept).views = held;       \
    }
#define VIEWS_KEY(name) VIEWS(g->kept_keys.name, d->keyboard.name)
#define VIEWS_PAD(name) VIEWS(g->kept_pad.name, d->gamepad.name)
#define VIEWS_DPAD(name) VIEWS(g->kept_pad.dpad.name, d->gamepad.dpad.name)
    KEPT_BUTTONS(VIEWS)
#undef VIEWS_KEY
#undef VIEWS_PAD
#undef VIEWS_DPAD
#undef VIEWS
}

// ...and from the devices a sample is taken from.
static void hide_kept_from_sample(tide_gui *g, tide_devices *d)
{
#define SAMPLE(kept, button)       \
    if ((kept).samples) {          \
        const bool held = (button).held; \
        RELEASE(button)            \
        (kept).samples = held;     \
    }
#define SAMPLE_KEY(name) SAMPLE(g->kept_keys.name, d->keyboard.name)
#define SAMPLE_PAD(name) SAMPLE(g->kept_pad.name, d->gamepad.name)
#define SAMPLE_DPAD(name) SAMPLE(g->kept_pad.dpad.name, d->gamepad.dpad.name)
    KEPT_BUTTONS(SAMPLE)
#undef SAMPLE_KEY
#undef SAMPLE_PAD
#undef SAMPLE_DPAD
#undef SAMPLE
}

// Hides what the GUI uses from devices the game or views read. The GUI works
// out the next frame's from the platform's, so nothing is lost.
static void hide(const tide_gui *g, tide_devices *d)
{
    if (g->focus || g->editing || g->modal) {
        hide_keyboard(d);
        hide_gamepad(d);
    }
    if (g->active || g->modal || on_gui(g, d)) hide_pointer(d);
    if (g->modal) {
        d->mouse.delta = tide_f2(0.0f, 0.0f);
        d->pointer.delta = tide_f2(0.0f, 0.0f);
    }
}

#undef RELEASE

// Hides what views claimed (`what`), as the GUI's own use is.
static void hide_claimed(tide_devices *d, const uint32_t what)
{
    if (what & TIDE_GUI_KEYBOARD) hide_keyboard(d);
    if (what & TIDE_GUI_POINTER) hide_pointer(d);
}

// What views claimed that `view` doesn't read, or the game for 0, with the
// pointer where `d` has it: what any view claimed last frame, and the pointer
// where it's on a widget of a view's own now, as with the GUI's (see on_gui);
// less what `view` claimed itself.
static uint32_t claimed_from(const tide_gui *g, const tide_devices *d, const uint32_t view)
{
    uint32_t taken = g->taken;
    uint32_t own = 0;
    for (uint32_t i = 0; view && i < g->claim_count; i++) {
        if (g->claims[i].view == view) own = g->claims[i].what;
    }
    tide_float2 at;
    if (g->claim_rect_count && pointer_place(g, d, &at)) {
        for (uint32_t i = 0; i < g->claim_rect_count; i++) {
            if (!contains(g->claim_rects[i].rect, at)) continue;
            taken |= TIDE_GUI_POINTER;
            if (view && g->claim_rects[i].view == view) own |= TIDE_GUI_POINTER;
        }
    }
    return taken & ~own;
}

// Tab follows declaration order. Directional input searches geometry only on
// an input edge, so an idle frame does no spatial search.
static void navigate(tide_gui *g)
{
    if (!(g->keys_pressed & (KEY_TAB | KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT |
                            PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT | KEY_CANCEL | PAD_CANCEL))) return;
    const bool keyboard = g->focus || !(g->taken & TIDE_GUI_KEYBOARD);
    const bool tab = keyboard && pressed(g, KEY_TAB);
    const bool back = tab && (g->keys & KEY_SHIFT);
    const int n = (int)g->nav_last_count;
    int at = -1;
    for (int i = 0; i < n; i++) if (g->nav_last[i] == g->focus) { at = i; break; }
    int to = at;
    if (tab && n) to = at < 0 ? (back ? n - 1 : 0) : (at + (back ? n - 1 : 1)) % n;
    else if (g->focus || g->nav_arrows) {
        uint32_t usable = keyboard ? ~0u : ~(uint32_t)(KEY_DOWN | KEY_UP | KEY_LEFT | KEY_RIGHT);
        if (g->editing || (at >= 0 && g->nav_steps_last[at]))
            usable &= ~(uint32_t)(KEY_LEFT | KEY_RIGHT | PAD_LEFT | PAD_RIGHT);
        const uint32_t edges = g->keys_pressed & usable;
        int x = 0, y = 0;
        if (edges & (KEY_RIGHT | PAD_RIGHT)) x = 1;
        else if (edges & (KEY_LEFT | PAD_LEFT)) x = -1;
        else if (edges & (KEY_DOWN | PAD_DOWN)) y = 1;
        else if (edges & (KEY_UP | PAD_UP)) y = -1;
        if ((x || y) && n) {
            if (at < 0) to = x < 0 || y < 0 ? n - 1 : 0;
            else {
                const tide_rect from = g->nav_rects[at];
                double best = DBL_MAX;
                for (int i = 0; i < n; i++) {
                    if (i == at) continue;
                    const tide_rect r = g->nav_rects[i];
                    const double dx = (double)r.x + r.width * 0.5 - from.x - from.width * 0.5;
                    const double dy = (double)r.y + r.height * 0.5 - from.y - from.height * 0.5;
                    const double along = dx * x + dy * y;
                    const double cross = x ? dy : dx;
                    const double half = x ? ((double)r.height + from.height) * 0.5 : ((double)r.width + from.width) * 0.5;
                    const double across = (cross < 0 ? -cross : cross) > half ? (cross < 0 ? -cross : cross) - half : 0;
                    if (!(along > 0.0)) continue;
                    const double score = along * along + across * across * 4.0;
                    if (score < best) { best = score; to = i; }
                }
            }
        }
    }
    if (to >= 0 && to != at) {
        g->focus = g->nav_last[to];
        g->reveal_focus = true;
        g->keys_pressed &= ~(uint32_t)(KEY_LEFT | KEY_RIGHT | PAD_LEFT | PAD_RIGHT);
    }
    if (pressed(g, KEY_CANCEL | PAD_CANCEL)) {
        if (g->editing) g->editing = 0;
        else g->focus = 0;
    }
}

void tide_gui_begin(tide_gui *g, const tide_devices *devices, const tide_float2 screen, const tide_measure_fn measure)
{
    g->frame++;
    if (g->frame == 0) g->frame = 1; // Stamps of 0 mean never
    g->width = screen.x;
    g->height = screen.y;
    g->measure = measure ? measure : tide_draw_guess_text; // Without the platform's font

    // The pointer: the mouse, or a finger. The devices count from the bottom
    // left, y up; the GUI from the top left, y down. A finger that's off the
    // screen is nowhere, once it lifted, so nothing stays hovered. So is a
    // mouse whose cursor is locked, and it presses nothing: a button still
    // held as the cursor comes back was pressed for the game, not on a widget.
    const tide_pointer *p = &devices->pointer;
    const tide_float2 nowhere = tide_f2(-1e30f, -1e30f);
    const bool hidden = cursor_hidden(devices);
    g->mouse = hidden ? nowhere : tide_f2(p->position.x, screen.y - p->position.y);
    g->mouse_held = p->press.held && !hidden;
    if (p->touch && !g->mouse_held && !g->mouse_was_held) g->mouse = nowhere;
    g->mouse_pressed = g->mouse_held && !g->press_was_held;
    g->mouse_released = !g->mouse_held && g->mouse_was_held;
    g->mouse_was_held = g->mouse_held;
    g->press_was_held = p->press.held;
    // Which widget the pointer is on goes by where it is now, as whose the
    // pointer is does (on_gui), not by where the last frame saw it: a pointer
    // that came onto a widget and pressed it since the last frame presses that
    // widget, in the one frame that has the press. Every finger does, and a
    // mouse in a slow frame. By where the last frame saw it, a tap on a button
    // of a menu the mouse rested on pressed the menu's panel, which was on top
    // there. With more widgets than a frame remembers, it's as that frame saw it.
    g->wheel_owner = 0;
    if (!g->rects_full) g->hot = on_top(g, g->mouse, &g->hot_rect, &g->wheel_owner);
    g->wheel = devices->mouse.poll_scroll;
    g->gesture_cancel = 0;
    g->drag_delta = tide_f2(0, 0);
    if (g->mouse_pressed && p->touch) {
        g->drag_scroll = g->wheel_owner;
        const tide_gui_size *last_scroll = remembered(g, g->drag_scroll);
        g->drag_axes = last_scroll ? (last_scroll->size.x > last_scroll->natural ? 1u : 0u)
                                   | (last_scroll->size.y > last_scroll->least ? 2u : 0u) : 0;
        g->drag_start = g->drag_last = g->mouse;
        g->scroll_dragging = false;
    }
    if (g->drag_scroll && p->touch && g->mouse_held) {
        const tide_float2 moved = tide_f2(g->mouse.x - g->drag_start.x, g->mouse.y - g->drag_start.y);
        if (!g->scroll_dragging && (((g->drag_axes & 1u) && tide_abs_f(moved.x) > 6) ||
                                    ((g->drag_axes & 2u) && tide_abs_f(moved.y) > 6))) {
            g->scroll_dragging = true;
            g->gesture_cancel = g->active;
            g->active = 0;
        }
        if (g->scroll_dragging) {
            g->drag_delta = tide_f2(g->drag_axes & 1u ? g->drag_last.x - g->mouse.x : 0,
                                   g->drag_axes & 2u ? g->drag_last.y - g->mouse.y : 0);
            g->drag_last = g->mouse;
        }
    } else if (!g->mouse_held) {
        g->drag_scroll = 0;
        g->scroll_dragging = false;
    }
    const uint32_t keys = nav_keys(devices);
    g->keys_pressed = keys & ~g->keys;
    g->keys = keys;
    // Repeat editing keys locally, without inventing device presses for the
    // match or other views. A stalled frame takes at most one repeat step.
    const uint32_t repeat = g->editing ? keys & (KEY_LEFT | KEY_RIGHT | KEY_BACKSPACE | KEY_DELETE) : 0;
    if (repeat) {
        const uint64_t now = tide_time_now_ns();
        if (repeat != g->edit_repeat) g->edit_repeat_at = now + UINT64_C(450000000);
        else if (now >= g->edit_repeat_at) {
            g->keys_pressed |= repeat;
            g->edit_repeat_at = now + UINT64_C(50000000);
        }
    }
    g->edit_repeat = repeat;
    g->text = devices->keyboard.text;
    g->nav_arrows = !g->game_input || g->navigate;
    g->game_input = false;
    g->back = pressed(g, KEY_CANCEL | PAD_CANCEL) && !g->editing;
    // With what the GUI used last frame, before this frame's keys move the focus.
    keep_presses(g, devices);
    frame_devices(g, devices);
    // The view whose control has the focus goes on reading the keyboard and
    // the gamepad, less what the GUI uses of them. Not under a modal, which
    // leaves the views nothing.
    g->focus_reads = g->focus != 0 && g->focus == g->focus_control && g->focus_view != 0 && !g->modal;
    if (g->focus_reads) {
        g->focus_keyboard = g->devices.keyboard;
        g->focus_gamepad = g->devices.gamepad;
        hide_navigation(&g->focus_keyboard, &g->focus_gamepad);
    }
    hide_kept_from_views(g, &g->devices); // Before the rest is hidden: it goes by which are still down
    hide(g, &g->devices);
    // ...and what views claimed then, until a view runs (tide_gui_view).
    g->unclaimed = g->devices;
    hide_claimed(&g->devices, claimed_from(g, &g->unclaimed, 0));
    g->view = 0;

    g->claimed = g->scroll_dragging;
    g->active_seen = false;
    g->focus_seen = false;
    g->editing_seen = false;

    // A click ends keyboard and gamepad navigation; a field it lands on takes the focus back.
    const uint32_t focus = g->focus;
    if (g->mouse_pressed) g->focus = 0;
    g->reveal_focus = false;
    navigate(g);
    if (g->focus != focus) g->focus_reads = false; // It moved on: the keys are the next widget's

    g->theme = tide_gui_theme_default();
    g->nav_count = 0;
    g->depth = 0;
    g->key = 0;
    g->drawing_count = 0;
    g->scrolling_count = 0;
    g->scroll_clip = g->scroll_id = 0;
    g->input_clip = (tide_rect){0, 0, screen.x, screen.y};
    g->focus_next = false;
    g->in_modal = 0;
    const tide_gui_size *root = remembered(g, ROOT_ID);
    g->groups[0] = (tide_gui_group){
        .id = ROOT_ID, .kind = VERTICAL, .room = screen.x, .stretch = root ? root->natural : 0.0f, .anchor = -1, .spacing = SPACING};
    tide_draw_reset(&g->list);
    tide_draw_screen_size(&g->list, screen);
    tide_draw_set_screen(&g->list); // In the screen's pixels, whatever list it's added to
}

void tide_gui_end(tide_gui *g, tide_draw_list *draw)
{
    tide_gui_close(g, 0);
    remember(g, ROOT_ID, g->groups[0].size, g->groups[0].natural, g->groups[0].least);

    // What wasn't drawn this frame lets go.
    if (!g->focus_seen) g->focus = 0;
    if (!g->editing_seen) g->editing = 0;
    g->edit_left = 0; // A number whose field didn't run to keep it
    if (!g->active_seen || !g->mouse_held) g->active = 0;
    g->hot = g->hot_next;
    g->hot_rect = g->hot_rect_next;
    g->hot_next = 0;
    g->over = g->over_next;
    g->over_next = false;
    // Resolve scroll clips after anchored placement and scroll relocation.
    for (uint32_t i = 0; i < g->rect_count_next; i++) {
        tide_gui_rect *r = &g->rects_next[i];
        for (uint32_t clip = r->clip; clip && clip <= g->list.clip_count; clip = g->list.clips[clip - 1u].parent)
            r->rect = intersect(r->rect, g->list.clips[clip - 1u].rect);
    }
    memcpy(g->rects, g->rects_next, g->rect_count_next * sizeof g->rects[0]);
    g->rect_count = g->rect_count_next;
    g->rect_count_next = 0;
    g->rects_full = g->rects_full_next;
    g->rects_full_next = false;
    // A modal that just came on top takes the focus next frame.
    g->grab = g->modal_next != g->modal ? g->modal_next : 0;
    g->modal = g->modal_next;
    g->modal_next = 0;
    memcpy(g->nav_last, g->nav, g->nav_count * sizeof g->nav[0]);
    memcpy(g->nav_steps_last, g->nav_steps, g->nav_count * sizeof g->nav_steps[0]);
    g->nav_last_count = g->nav_count;
    // This frame's claims stand until the next frame ends.
    g->taken = g->taken_next;
    g->taken_next = 0;
    memcpy(g->claims, g->claims_next, g->claim_count_next * sizeof g->claims[0]);
    g->claim_count = g->claim_count_next;
    g->claim_count_next = 0;
    memcpy(g->claim_rects, g->claim_rects_next, g->claim_rect_count_next * sizeof g->claim_rects[0]);
    g->claim_rect_count = g->claim_rect_count_next;
    g->claim_rect_count_next = 0;
    g->keyboard_shown = g->keyboard_shown_next;
    g->keyboard_shown_next = false;
    g->cursor_lock = g->cursor_lock_next;
    g->cursor_lock_next = false;
    g->navigate = g->navigate_next;
    g->navigate_next = false;
    g->view = 0;

    if (g->list.count > 0) {
        // Whatever the views left set, the GUI isn't drawn with: the list starts from the defaults
        tide_draw_append(draw, &g->list);
    }
}

bool tide_gui_typing(const tide_gui *g)
{
    return g->editing != 0 || g->keyboard_shown;
}

bool tide_gui_cursor_lock(const tide_gui *g)
{
    return g->cursor_lock && !g->modal; // A modal's widgets need the pointer
}

void tide_gui_hide(tide_gui *g, tide_devices *d)
{
    g->game_input = true;
    const uint32_t claimed = claimed_from(g, d, 0);
    hide_kept_from_sample(g, d);
    hide(g, d);
    hide_claimed(d, claimed);
    d->keyboard.text = (tide_typed){0}; // What's typed is a frame's: views read it
}

// ---------------------------------------------------------------------------
// Claims

void tide_gui_view(tide_gui *g, const uint32_t view)
{
    g->view = view;
    if (!g->taken && !g->claim_rect_count && !g->focus_reads) return; // Every view reads the same devices
    g->devices = g->unclaimed;
    hide_claimed(&g->devices, claimed_from(g, &g->unclaimed, view));
    if (g->focus_reads && view == g->focus_view) { // Its control has the focus
        g->devices.keyboard = g->focus_keyboard;
        g->devices.gamepad = g->focus_gamepad;
    }
}

// The view that's running claims `what`. Past TIDE_GUI_MAX_CLAIMS views, a
// claim still hides it from the game and the other views, and from the view
// itself.
static void claim(tide_gui *g, const uint32_t what)
{
    g->taken_next |= what;
    for (uint32_t i = 0; i < g->claim_count_next; i++) {
        if (g->claims_next[i].view != g->view) continue;
        g->claims_next[i].what |= what;
        return;
    }
    if (g->claim_count_next < TIDE_GUI_MAX_CLAIMS) g->claims_next[g->claim_count_next++] = (tide_gui_claim){g->view, what};
}

void tide_gui_claim_pointer(tide_gui *g)
{
    claim(g, TIDE_GUI_POINTER);
}

void tide_gui_claim_keyboard(tide_gui *g)
{
    claim(g, TIDE_GUI_KEYBOARD);
}

void tide_gui_claim_pointer_at(tide_gui *g, const tide_rect rect)
{
    if (g->claim_rect_count_next < TIDE_GUI_MAX_CLAIM_RECTS) {
        g->claim_rects_next[g->claim_rect_count_next++] = (tide_gui_claim_rect){on_screen(g, rect), g->view};
        return;
    }
    // More places than a frame remembers: the pointer on it, as this frame sees it
    if (contains(rect, g->mouse)) claim(g, TIDE_GUI_POINTER);
}

void tide_gui_show_keyboard(tide_gui *g)
{
    g->keyboard_shown_next = true;
}

void tide_gui_lock_cursor(tide_gui *g)
{
    g->cursor_lock_next = true;
}

void tide_gui_navigate(tide_gui *g)
{
    g->navigate_next = true;
}

// ---------------------------------------------------------------------------
// Interaction

// Whether widgets drawn now are under the modal on top, outside it.
static bool under_modal(const tide_gui *g)
{
    return g->modal != 0 && g->in_modal != g->modal;
}

// Whether widgets drawn now work: always, unless they're under a modal or in
// a Disabled block.
static bool live(const tide_gui *g)
{
    return !under_modal(g) && !grayed(g);
}

// A rect of the GUI's where the screen has it: inside a Target block, where
// the target is shown; the same rect elsewhere. What the next frame finds
// the pointer on, and what the focus moves by, are in the screen's pixels.
static tide_rect on_screen(const tide_gui *g, const tide_rect r)
{
    if (!g->in_target) return r;
    if (!g->space.mapped) return (tide_rect){-1e30f, -1e30f, 0.0f, 0.0f}; // Nowhere the pointer is
    return (tide_rect){g->space.origin.x + r.x * g->space.scale.x, g->space.origin.y + r.y * g->space.scale.y,
                       r.width * g->space.scale.x, r.height * g->space.scale.y};
}

// A rect of the screen's in the GUI's pixels: on_screen undone.
static tide_rect from_screen(const tide_gui *g, const tide_rect r)
{
    if (!g->in_target || !g->space.mapped) return r;
    return (tide_rect){(r.x - g->space.origin.x) / g->space.scale.x, (r.y - g->space.origin.y) / g->space.scale.y,
                       r.width / g->space.scale.x, r.height / g->space.scale.y};
}

// A move of `by` GUI pixels in the screen's.
static tide_float2 on_screen_by(const tide_gui *g, const tide_float2 by)
{
    if (!g->in_target || !g->space.mapped) return by;
    return tide_f2(by.x * g->space.scale.x, by.y * g->space.scale.y);
}

// Where the pointer is on the screen, whichever space the GUI draws in.
static tide_float2 screen_mouse(const tide_gui *g)
{
    return g->in_target ? g->space.mouse : g->mouse;
}

// Remembers where a widget or an area is: the pointer there is the GUI's, for
// the game and views that read it before the next frame's GUI (see on_gui),
// and that frame's widgets go by which of them is on top there (see on_top).
// `content` is WIDGET, or for an area, how many places there were before its
// content's.
static void remember_place(tide_gui *g, const uint32_t id, const tide_rect r, const uint32_t content)
{
    if (g->rect_count_next < TIDE_GUI_MAX_RECTS) g->rects_next[g->rect_count_next++] = (tide_gui_rect){on_screen(g, r), id, content, g->scroll_clip, g->scroll_id};
    else g->rects_full_next = true;
}

// Whether the mouse is on the widget. Where widgets overlap, the one drawn
// last is on top: the last frame drew it where the mouse is now, and it hides
// the others there. A disabled widget does too, and keeps clicks on it from
// the game, but it's never hovered itself.
static bool hovered(tide_gui *g, const uint32_t id, const tide_rect r)
{
    if (under_modal(g)) return false;
    remember_place(g, id, r, WIDGET);
    if (!contains(r, g->mouse) || (g->scroll_clip && !contains(g->input_clip, g->mouse))) return false;
    g->hot_next = id;
    g->hot_rect_next = on_screen(g, r);
    g->over_next = true;
    return !grayed(g) && (g->hot == id || g->hot == 0 || !contains(g->hot_rect, screen_mouse(g)));
}

// Whether the mouse is pressing the widget: from a press on it until it's let
// go, or until it's disabled.
static bool held_down(tide_gui *g, const uint32_t id, const bool hover)
{
    if (hover && g->mouse_pressed && !g->claimed) {
        g->active = id;
        g->claimed = true;
    }
    if (g->active != id || grayed(g)) return false;
    g->active_seen = true;
    return true;
}

// Adds the widget to the ones the focus moves between; true if it has it.
// One that `steps` its value with left and right keeps those: from the
// others, they move the focus.
static bool focusable(tide_gui *g, const uint32_t id, const bool steps, const tide_rect rect)
{
    if (!live(g)) return false;
    if (g->focus_next) {
        g->focus = id;
        g->focus_next = false;
        g->reveal_focus = true;
    }
    // The first widget of a modal that just came on top. A field under it that
    // was typed into ends its typing itself as it runs, without the focus
    if (g->grab && g->grab == g->in_modal) {
        g->grab = 0;
        g->focus = id;
    }
    if (g->nav_count < TIDE_GUI_MAX_NAV) {
        g->nav_rects[g->nav_count] = on_screen(g, rect);
        g->nav_steps[g->nav_count] = steps;
        g->nav[g->nav_count++] = id;
    }
    if (g->focus != id) return false;
    g->focus_rect = on_screen(g, rect);
    g->focus_seen = true;
    return true;
}

// A button's press: a click that starts and ends on it, or Enter, Space or
// the south button while it has the focus.
static bool clicked(tide_gui *g, const uint32_t id, const tide_rect r, bool *hover, bool *down, bool *focused)
{
    *hover = hovered(g, id, r);
    *down = held_down(g, id, *hover);
    *focused = focusable(g, id, false, r);
    return (*down && g->mouse_released && *hover) || (*focused && pressed(g, KEY_ENTER | KEY_SPACE | PAD_ACCEPT));
}

// ---------------------------------------------------------------------------
// Layout

static tide_gui_group *top(tide_gui *g)
{
    return &g->groups[g->depth];
}

// How far a row's widgets shrink, from their natural widths (0) to their
// least (1), for the row to fit its room.
static float squeeze(const float natural, const float least, const float room)
{
    if (natural <= room) return 0.0f;
    if (natural <= least) return 1.0f;
    return min_f((natural - room) / (natural - least), 1.0f);
}

// How wide a widget wants to be: `natural`, or in a vertical container, as
// wide as its widest widget was last frame if it `stretch`es.
static float wanted(const tide_gui_group *grp, const float natural, const bool stretch)
{
    return stretch && grp->kind != HORIZONTAL && grp->stretch > natural ? grp->stretch : natural;
}

// How wide it gets: what it wants if there's room, and down to `least` if
// there isn't. A row shrinks each widget by as much as it can give.
static float fit(const tide_gui_group *grp, const float want, const float least)
{
    const float floor = min_f(least, want);
    if (grp->kind == HORIZONTAL) return want - (want - floor) * grp->squeeze;
    return max_f(min_f(want, grp->room), floor);
}

// Puts something `width` wide at the next place in the current container.
// `natural` and `least` are the widths it would take with room and squeezed.
static tide_rect take(tide_gui *g, const float width, const float height, const float natural, const float least)
{
    tide_gui_group *grp = top(g);
    const tide_rect r = {grp->cursor.x, grp->cursor.y, width, height};
    grp->spacing = SPACING; // The last one's, which comes off as a row closes: a theme can change it between
    if (grp->kind == HORIZONTAL) {
        grp->cursor.x += width + SPACING;
        grp->size = tide_f2(r.x + width - grp->origin.x, max_f(grp->size.y, r.y + height - grp->origin.y));
        grp->natural += natural + SPACING; // The last one's spacing comes off when it closes
        grp->least += least + SPACING;
        return r;
    }
    grp->cursor.y += height + SPACING;
    grp->size = tide_f2(max_f(grp->size.x, r.x + width - grp->origin.x), r.y + height - grp->origin.y);
    grp->natural = max_f(grp->natural, natural);
    grp->least = max_f(grp->least, least);
    return r;
}

// Takes the next place in the current container for a widget `natural` wide,
// which can shrink to `least`. In a vertical one, widgets that `stretch` are
// as wide as its widest one was last frame.
static tide_rect reserve(tide_gui *g, const float natural, const float least, const bool stretch)
{
    const tide_gui_group *grp = top(g);
    return take(g, fit(grp, wanted(grp, natural, stretch), least), LINE, natural, min_f(least, natural));
}

static int open_group(tide_gui *g, const uint32_t id, const uint32_t kind, const tide_float2 origin)
{
    const int before = g->depth;
    if (g->depth == TIDE_GUI_MAX_DEPTH) return before; // Too deep: its content goes in the container around it
    const tide_gui_group *around = top(g);
    const tide_gui_size *last = remembered(g, id);
    // Its room: in a row, its share of the row's, from last frame's size; in a
    // vertical container, all of it.
    float room = around->room;
    if (around->kind == HORIZONTAL) {
        room = last ? fit(around, last->natural, last->least)
                    : max_f(around->room - (around->cursor.x - around->origin.x), 0.0f);
    }
    g->depth++;
    tide_gui_group *grp = top(g);
    *grp = (tide_gui_group){.id = id, .kind = kind, .origin = origin, .cursor = origin, .room = room, .anchor = -1,
                            .panel = UINT32_MAX, .in_modal_before = g->in_modal, .disabled = around->disabled, .spacing = SPACING};
    if (last && kind != HORIZONTAL) grp->stretch = last->natural;
    if (last && kind == HORIZONTAL) grp->squeeze = squeeze(last->natural, last->least, room);
    return before;
}

int tide_gui_begin_vertical(tide_gui *g, const uint32_t id)
{
    return open_group(g, id, VERTICAL, top(g)->cursor);
}

int tide_gui_begin_horizontal(tide_gui *g, const uint32_t id)
{
    return open_group(g, id, HORIZONTAL, top(g)->cursor);
}

// Where an anchored area of `size` goes on the screen. One too big for it
// starts at the top left margin, so what doesn't fit is what comes last.
static tide_float2 place(const tide_gui *g, int32_t anchor, const tide_float2 size)
{
    if (anchor < 0 || anchor > TIDE_ANCHOR_LOWER_RIGHT) anchor = TIDE_ANCHOR_UPPER_LEFT;
    const int column = anchor % 3;
    const int row = anchor / 3;
    const float x = column == 0 ? AREA_MARGIN : column == 1 ? (g->width - size.x) * 0.5f : g->width - AREA_MARGIN - size.x;
    const float y = row == 0 ? AREA_MARGIN : row == 1 ? (g->height - size.y) * 0.5f : g->height - AREA_MARGIN - size.y;
    return tide_f2(max_f(x, AREA_MARGIN), max_f(y, AREA_MARGIN));
}

// An area's background, sized when it closes. It isn't a widget: in a
// Disabled block, only its widgets fade. Where the list has it: a smooth
// shape's command (the result), or a plain shape (*plain, 1 + it). What's
// moved with it once it's placed starts at *commands, the commands after it,
// and *plains, the plain shapes from it on.
// Under it, the theme's shadow, if it has one (*shadow, a smooth shape's
// command, or UINT32_MAX), sized with it.
static uint32_t panel(tide_gui *g, const tide_rect r, uint32_t *plain, uint32_t *commands, uint32_t *plains,
                      uint32_t *shadow)
{
    const tide_gui_theme *t = &g->theme;
    // With round corners, a border or shading it's a shape, which the list leaves
    // out while it has no size: an area that isn't sized yet stands in as a pixel.
    const bool round = t->radius > 0.0f || t->border_width > 0.0f || t->shading > 0.0f;
    const tide_rect sized = {r.x, r.y, round ? max_f(r.width, 1.0f) : r.width, round ? max_f(r.height, 1.0f) : r.height};
    *shadow = UINT32_MAX;
    if (t->shadow_size > 0.0f && t->shadow.a > 0.0f) {
        const uint32_t ahead = g->list.count;
        tide_shape s = TIDE_SHAPE_WHITE;
        s.center = tide_f2(r.x + r.width * 0.5f, r.y + r.height * 0.5f + t->shadow_size * 0.25f);
        s.size = tide_f2(max_f(r.width, 1.0f), max_f(r.height, 1.0f));
        s.radius = t->radius;
        s.color = t->shadow;
        s.soft = t->shadow_size;
        tide_draw_shape(&g->list, s);
        if (g->list.count > ahead) *shadow = g->list.count - 1u;
    }
    const uint32_t before = g->list.count, plains_before = g->list.plain_count;
    body_paint(g, sized, t->panel, t->radius);
    *commands = g->list.count; // A plain one's run, and what was settled before it, stay where they are
    *plains = plains_before;
    *plain = g->list.plain_count > plains_before ? g->list.plain_count : 0u; // A plain one: more of a run, or a run's first
    if (*plain) return UINT32_MAX;
    return g->list.count > before ? g->list.count - 1u : UINT32_MAX; // The panel itself: a frame's first comes after its screen
}

int tide_gui_begin_area(tide_gui *g, const uint32_t id, const tide_rect rect)
{
    uint32_t plain, commands, plains, shadow;
    const uint32_t background = panel(g, rect, &plain, &commands, &plains, &shadow);
    const int before = open_group(g, id, AREA, tide_f2(rect.x + AREA_PADDING, rect.y + AREA_PADDING));
    if (g->depth == before) return before;
    top(g)->room = max_f(rect.width - 2.0f * AREA_PADDING, 0.0f);
    top(g)->rect = rect;
    top(g)->panel = background;
    top(g)->panel_plain = plain;
    top(g)->commands_from = commands;
    top(g)->plains_from = plains;
    top(g)->radius = g->theme.radius;
    top(g)->border = g->theme.border_width;
    top(g)->shadow = shadow;
    top(g)->home = g->list.target;
    top(g)->clips_from = g->list.clip_count;
    top(g)->hot_before = g->hot_next;
    top(g)->rects_before = g->rect_count_next;
    top(g)->nav_from = g->nav_count;
    return before;
}

// An anchored area's size as it's shown, which eases toward the size its
// content measured, over twice the theme's transition, so a panel whose
// content changes grows and shrinks rather than jumping. Its motion keeps
// what was measured too (lanes 2 and 3), for the next frame to see whether
// it's still on its way.
static tide_float2 area_shown(tide_gui *g, const uint32_t id, const tide_float2 measured)
{
    const uint32_t key = tide_gui_seed(id, 0xA4EAu);
    const float target[2] = {measured.x, measured.y};
    float shown[2];
    animate_lanes(g, key, target, shown, 2, g->theme.transition * 2.0f, NULL);
    bool fresh;
    tide_gui_motion *m = motion(g, key, &fresh); // The slot it just used
    if (m) {
        m->value[2] = measured.x;
        m->value[3] = measured.y;
    }
    return tide_f2(shown[0], shown[1]);
}

// Whether an anchored area was shown at another size than its content's last
// frame: it's on its way to it, and its content is clipped to it meanwhile.
static bool area_moving(const tide_gui *g, const uint32_t id)
{
    const uint32_t key = tide_gui_seed(id, 0xA4EAu);
    uint32_t slot = key & (TIDE_GUI_MAX_MOTIONS - 1);
    for (uint32_t probe = 0; probe < TIDE_GUI_MAX_MOTIONS; probe++) {
        const tide_gui_motion *m = &g->motions[slot];
        if (m->frame == 0) return false;
        if (m->id == key) return g->frame - m->frame <= 1u && (m->value[0] != m->value[2] || m->value[1] != m->value[3]);
        slot = (slot + 1) & (TIDE_GUI_MAX_MOTIONS - 1);
    }
    return false;
}

int tide_gui_begin_area_at(tide_gui *g, const uint32_t id, const int32_t anchor)
{
    // Placed with last frame's size, so widgets know where they are as
    // they're drawn. If the size changes, it moves at the end.
    const tide_gui_size *last = remembered(g, id);
    const tide_float2 size = last ? last->size : tide_f2(0.0f, 0.0f);
    const tide_float2 at = place(g, anchor, size);
    uint32_t plain, commands, plains, shadow;
    const uint32_t background = panel(g, (tide_rect){at.x, at.y, size.x, size.y}, &plain, &commands, &plains, &shadow);
    const int before = open_group(g, id, AREA, tide_f2(at.x + AREA_PADDING, at.y + AREA_PADDING));
    if (g->depth == before) return before;
    top(g)->room = max_f(g->width - 2.0f * (AREA_MARGIN + AREA_PADDING), 0.0f);
    top(g)->guess = at;
    top(g)->anchor = anchor;
    top(g)->panel = background;
    top(g)->panel_plain = plain;
    top(g)->commands_from = commands;
    top(g)->plains_from = plains;
    top(g)->radius = g->theme.radius;
    top(g)->border = g->theme.border_width;
    top(g)->shadow = shadow;
    top(g)->home = g->list.target;
    top(g)->clips_from = g->list.clip_count;
    top(g)->hot_before = g->hot_next;
    top(g)->rects_before = g->rect_count_next;
    top(g)->nav_from = g->nav_count;
    if (area_moving(g, id)) { // Its content inside it as it grows: placed when it closes
        top(g)->clip_mark = tide_draw_push_clip(&g->list, (tide_rect){at.x, at.y, size.x, size.y});
        top(g)->own_clip = g->list.place.clip;
    }
    return before;
}

int tide_gui_begin_modal(tide_gui *g, const uint32_t id, const int32_t anchor, bool *open)
{
    if (!*open) return -1;
    // Back closes the modal on top. Not the one that came up this frame: the
    // press that opened it isn't for it.
    if (g->modal == id && g->back) {
        g->back = false;
        *open = false;
        return -1;
    }
    paint(g, (tide_rect){0.0f, 0.0f, g->width, g->height}, DIM, 0.0f);
    g->over_next = true; // The whole screen is the modal's
    g->modal_next = id;  // The last one drawn is on top
    const int before = tide_gui_begin_area_at(g, id, anchor);
    if (g->depth > before) g->in_modal = id;
    return before;
}

// A block that lays out nothing: a copy of the container around it, which its
// widgets go on laying out, and which hands the layout back when it closes.
// False when it's too deep for one.
// Where the GUI draws now: the window, or a Target block's target.
static tide_render_target gui_destination(const tide_gui *g)
{
    return g->in_target ? g->space.dest : (tide_render_target){.id = TIDE_RENDER_WINDOW};
}

static bool open_scope(tide_gui *g)
{
    if (g->depth == TIDE_GUI_MAX_DEPTH) return false;
    g->groups[g->depth + 1] = *top(g);
    g->depth++;
    top(g)->scope = true;
    top(g)->themed = false;
    top(g)->keyed = false;
    top(g)->drawing = false;
    top(g)->targeted = false;
    return true;
}

static int begin_target(tide_gui *g, const tide_render_target target, const bool mapped, const tide_rect shown)
{
    const int before = g->depth;
    if (g->in_target || !tide_render_target_valid_id(target.id) || target.id == TIDE_RENDER_WINDOW) return -1;
    const tide_float2 size = tide_render_target_logical(target, tide_f2(g->in_target ? g->space.width : g->width,
                                                                        g->in_target ? g->space.height : g->height));
    if (!(size.x > 0.0f) || !(size.y > 0.0f)) return -1;
    if (!open_scope(g)) return -1;
    top(g)->targeted = true;
    top(g)->space_before = g->space;
    tide_gui_space space = {.dest = target, .mouse = g->mouse, .width = g->width, .height = g->height};
    const tide_float2 nowhere = tide_f2(-1e30f, -1e30f);
    space.mapped = mapped && shown.width > 0.0f && shown.height > 0.0f && tide_is_finite_f(shown.x) && tide_is_finite_f(shown.y);
    if (space.mapped) {
        space.origin = tide_f2(shown.x, shown.y);
        space.scale = tide_f2(shown.width / size.x, shown.height / size.y);
    }
    g->space = space;
    g->in_target = true;
    // The target's pixels: its size is the screen's for what lays out by one, and the pointer is in them
    g->width = size.x;
    g->height = size.y;
    g->mouse = space.mapped && contains(shown, space.mouse)
        ? tide_f2((space.mouse.x - shown.x) / space.scale.x, (space.mouse.y - shown.y) / space.scale.y) : nowhere;
    tide_draw_render_target(&g->list, target);
    tide_draw_set_screen(&g->list);
    return before;
}

int tide_gui_begin_target(tide_gui *g, const tide_render_target target, const tide_rect shown)
{
    return begin_target(g, target, true, shown);
}

int tide_gui_begin_target_unshown(tide_gui *g, const tide_render_target target)
{
    return begin_target(g, target, false, (tide_rect){0});
}

int tide_gui_begin_draw(tide_gui *g)
{
    const int before = g->depth;
    if (!open_scope(g)) return -1; // A skipped block cannot leak drawing settings at the nesting ceiling
    tide_draw_render_target(&g->list, gui_destination(g));
    top(g)->drawing = true;
    g->drawing[g->drawing_count++] = (tide_gui_draw_state){g->list.place, g->list.saved_count};
    tide_draw_set_screen(&g->list);
    return before;
}

static int begin_key(tide_gui *g, const uint32_t site, const uint32_t key)
{
    const int before = g->depth;
    if (!open_scope(g)) return before;
    top(g)->keyed = true;
    top(g)->key_before = g->key;
    g->key = tide_gui_seed(g->key, tide_gui_seed(site, key));
    return before;
}

int tide_gui_begin_key(tide_gui *g, const uint32_t site, const int32_t key)
{
    return begin_key(g, site, (uint32_t)key);
}

int tide_gui_begin_key_long(tide_gui *g, const uint32_t site, const int64_t key)
{
    const uint64_t bits = (uint64_t)key;
    return begin_key(g, site, tide_gui_seed((uint32_t)bits, (uint32_t)(bits >> 32)));
}

int tide_gui_begin_key_text(tide_gui *g, const uint32_t site, const char *key)
{
    uint32_t hash = 2166136261u;
    if (key) for (const unsigned char *p = (const unsigned char *)key; *p; p++) hash = (hash ^ *p) * 16777619u;
    return begin_key(g, site, hash);
}

int tide_gui_begin_disabled(tide_gui *g, const bool disabled)
{
    const int before = g->depth;
    open_scope(g); // Too deep: the rest of the container around it is disabled too
    top(g)->disabled = top(g)->disabled || disabled;
    return before;
}

int tide_gui_begin_theme(tide_gui *g, const tide_gui_theme theme)
{
    const int before = g->depth;
    if (!open_scope(g)) return before; // Too deep to put it back: its widgets keep the look around them
    top(g)->themed = true;
    top(g)->before = g->theme;
    g->theme = fitted_theme(theme);
    return before;
}

void tide_gui_set_theme(tide_gui *g, const tide_gui_theme theme)
{
    g->theme = fitted_theme(theme);
}

static void close_area(tide_gui *g, const tide_gui_group *grp)
{
    const tide_float2 size = tide_f2(grp->size.x + 2.0f * AREA_PADDING, grp->size.y + 2.0f * AREA_PADDING);
    tide_rect r = grp->rect;
    tide_float2 kept = size; // What the next frame places it by
    if (grp->anchor >= 0) {
        const tide_float2 shown = kept = area_shown(g, grp->id, size);
        const tide_float2 at = place(g, grp->anchor, shown);
        const float dx = at.x - grp->guess.x;
        const float dy = at.y - grp->guess.y;
        if ((dx != 0.0f || dy != 0.0f) && (grp->panel != UINT32_MAX || grp->panel_plain)) {
            tide_draw_move_gui_in(&g->list, grp->home, grp->panel != UINT32_MAX ? grp->panel : grp->commands_from,
                                  grp->plains_from, grp->clips_from, tide_f2(dx, dy));
        }
        // Its widgets are remembered where they're drawn, too, in the screen's pixels
        const tide_float2 moved = on_screen_by(g, tide_f2(dx, dy));
        for (uint32_t i = grp->rects_before; i < g->rect_count_next; i++) {
            g->rects_next[i].rect.x += moved.x;
            g->rects_next[i].rect.y += moved.y;
        }
        for (uint32_t i = grp->nav_from; i < g->nav_count; i++) {
            g->nav_rects[i].x += moved.x;
            g->nav_rects[i].y += moved.y;
            if (g->nav[i] == g->focus) g->focus_rect = g->nav_rects[i];
        }
        r = (tide_rect){at.x, at.y, shown.x, shown.y};
        if (grp->own_clip) {
            // Its own, made as it opened (a list shares a clip that's the same as the one in use)
            if (grp->own_clip > grp->clips_from) g->list.clips[grp->own_clip - 1u].rect = r;
            tide_draw_pop(&g->list, grp->clip_mark);
        }
    }
    if (grp->panel != UINT32_MAX) {
        tide_draw_command *c = &g->list.commands[grp->panel];
        c->a = tide_f2(r.x + r.width * 0.5f, r.y + r.height * 0.5f);
        c->b = tide_f2(r.width, r.height);
        // Round corners and a border were fitted to the size it had then: the list fits them again as it's added
        if (c->kind == TIDE_DRAW_SHAPE) {
            g->list.shapes[c->shape].radius = grp->radius;
            g->list.shapes[c->shape].outline = grp->border;
        }
    } else if (grp->panel_plain) {
        tide_draw_plain *p = &g->list.plains[grp->panel_plain - 1u];
        p->a = tide_f2(r.x + r.width * 0.5f, r.y + r.height * 0.5f);
        p->b = tide_f2(r.width, r.height);
    }
    if (grp->shadow != UINT32_MAX) { // Under it wherever it went, with its soft edge's offset
        tide_draw_command *c = &g->list.commands[grp->shadow];
        const float drop = g->list.shapes[c->shape].soft * 0.25f;
        c->a = tide_f2(r.x + r.width * 0.5f, r.y + r.height * 0.5f + drop);
        c->b = tide_f2(max_f(r.width, 1.0f), max_f(r.height, 1.0f));
        g->list.shapes[c->shape].radius = grp->radius;
    }
    remember(g, grp->id, kept, grp->natural, grp->least);
    remember_place(g, grp->id, r, grp->rects_before);
    if (!contains(r, g->mouse)) return;
    g->over_next = true;
    // Under the mouse, but none of its widgets is: the panel hides what's below it.
    if (g->hot_next == grp->hot_before) {
        g->hot_next = grp->id;
        g->hot_rect_next = on_screen(g, r);
    }
}

static void close_scroll(tide_gui *g, const tide_gui_group *grp)
{
    const tide_gui_scroll_state state = g->scrolling[--g->scrolling_count];
    const tide_float2 limit = tide_f2(max_f(grp->size.x - state.viewport.width, 0.0f),
                                     max_f(grp->size.y - state.viewport.height, 0.0f));
    tide_float2 offset = *state.offset;
    if (g->drag_scroll == grp->id && g->scroll_dragging && live(g)) {
        offset.x += g->drag_delta.x;
        offset.y += g->drag_delta.y;
    }
    if (g->wheel_owner == grp->id && live(g)) {
        const tide_float2 moved = tide_f2(clamp_f(offset.x - g->wheel.x * WHEEL_LINE, 0.0f, limit.x),
                                         clamp_f(offset.y - g->wheel.y * WHEEL_LINE, 0.0f, limit.y));
        g->wheel.x += (moved.x - offset.x) / WHEEL_LINE;
        g->wheel.y += (moved.y - offset.y) / WHEEL_LINE;
        offset = moved;
        g->wheel_owner = state.parent;
    }
    if (g->focus && g->focus_seen && g->reveal_focus) {
        for (uint32_t i = grp->nav_from; i < g->nav_count; i++) {
            if (g->nav[i] != g->focus) continue;
            const tide_rect r = from_screen(g, g->focus_rect); // In the GUI's pixels, as the viewport is
            if (r.x < state.viewport.x) offset.x += r.x - state.viewport.x;
            else if (r.x + r.width > state.viewport.x + state.viewport.width)
                offset.x += r.x + r.width - state.viewport.x - state.viewport.width;
            if (r.y < state.viewport.y) offset.y += r.y - state.viewport.y;
            else if (r.y + r.height > state.viewport.y + state.viewport.height)
                offset.y += r.y + r.height - state.viewport.y - state.viewport.height;
            break;
        }
    }
    offset.x = clamp_f(offset.x, 0.0f, limit.x);
    offset.y = clamp_f(offset.y, 0.0f, limit.y);
    *state.offset = offset;
    const tide_float2 shift = tide_f2(state.before.x - offset.x, state.before.y - offset.y);
    if (shift.x != 0.0f || shift.y != 0.0f) {
        tide_draw_move_gui_in(&g->list, grp->home, grp->commands_from, grp->plains_from, state.clip, shift);
        const tide_float2 moved = on_screen_by(g, shift); // Remembered in the screen's pixels
        for (uint32_t i = grp->rects_before; i < g->rect_count_next; i++) {
            g->rects_next[i].rect.x += moved.x;
            g->rects_next[i].rect.y += moved.y;
        }
        for (uint32_t i = grp->nav_from; i < g->nav_count; i++) {
            g->nav_rects[i].x += moved.x;
            g->nav_rects[i].y += moved.y;
            if (g->nav[i] == g->focus) g->focus_rect = g->nav_rects[i];
        }
    }
    tide_draw_pop(&g->list, state.mark);
    g->scroll_clip = state.clip_before_id;
    g->scroll_id = state.parent;
    g->input_clip = state.clip_before;
    // For scrolls the two width fields remember the viewport dimensions,
    // allowing touch arbitration before the next frame's content is called.
    remember(g, grp->id, grp->size, state.viewport.width, state.viewport.height);
    const uint32_t count_before = g->rect_count_next;
    remember_place(g, grp->id, state.viewport, grp->rects_before);
    // The viewport itself routes wheel input to this scroll container.
    if (g->rect_count_next > count_before) g->rects_next[g->rect_count_next - 1u].scroll = grp->id;
    if (contains(state.viewport, g->mouse) && (!g->scroll_clip || contains(g->input_clip, g->mouse))) {
        g->over_next = true;
        if (g->hot_next == grp->hot_before) {
            g->hot_next = grp->id;
            g->hot_rect_next = state.viewport;
        }
    }
}

void tide_gui_close(tide_gui *g, const int depth)
{
    while (g->depth > depth && g->depth > 0) {
        const tide_gui_group grp = *top(g);
        g->depth--;
        if (grp.scope) { // A Disabled or Theme block hands the layout back to the container around it
            if (grp.themed) g->theme = grp.before; // ...and a Theme block the look it found
            if (grp.targeted) { // ...and a Target block the screen, the pointer and the window
                g->mouse = g->space.mouse;
                g->width = g->space.width;
                g->height = g->space.height;
                g->space = grp.space_before;
                g->in_target = false;
                tide_draw_render_target(&g->list, (tide_render_target){.id = TIDE_RENDER_WINDOW});
            }
            if (grp.keyed) g->key = grp.key_before;
            if (grp.drawing) {
                const tide_gui_draw_state *state = &g->drawing[--g->drawing_count];
                tide_draw_restore_to(&g->list, gui_destination(g), &state->before, state->mark);
            }
            tide_gui_group *around = top(g);
            const tide_gui_group own = *around;
            *around = grp;
            around->disabled = own.disabled;
            around->scope = own.scope;
            around->themed = own.themed;
            around->before = own.before;
            around->keyed = own.keyed;
            around->key_before = own.key_before;
            around->drawing = own.drawing;
            around->targeted = own.targeted;
            around->space_before = own.space_before;
            continue;
        }
        g->in_modal = grp.in_modal_before;
        if (grp.kind == SCROLL) {
            close_scroll(g, &grp);
            continue;
        }
        if (grp.kind == AREA) {
            close_area(g, &grp); // Areas are on the screen, not in the container around them
            continue;
        }
        float natural = grp.natural;
        float least = grp.least;
        if (grp.kind == HORIZONTAL) { // Less the spacing after its last widget
            natural = max_f(natural - grp.spacing, 0.0f);
            least = max_f(least - grp.spacing, 0.0f);
        }
        remember(g, grp.id, grp.size, natural, least);
        take(g, grp.size.x, grp.size.y, natural, least);
    }
}

void tide_gui_layout_space(tide_gui *g, const float size)
{
    tide_gui_group *grp = top(g);
    if (grp->kind == HORIZONTAL) {
        grp->cursor.x += size;
        grp->size.x = max_f(grp->size.x, grp->cursor.x - grp->spacing - grp->origin.x);
        grp->natural += size;
        grp->least += size;
    } else {
        grp->cursor.y += size;
        grp->size.y = max_f(grp->size.y, grp->cursor.y - grp->spacing - grp->origin.y);
    }
}

static float dimension(const float value)
{
    return tide_is_finite_f(value) && value > 0.0f ? value : 0.0f;
}

int tide_gui_begin_scroll(tide_gui *g, const uint32_t id, tide_rect rect, tide_float2 *offset)
{
    if (g->depth == TIDE_GUI_MAX_DEPTH) return -1;
    rect.width = dimension(rect.width);
    rect.height = dimension(rect.height);
    const tide_gui_size *last = remembered(g, id);
    offset->x = last ? clamp_f(dimension(offset->x), 0.0f, max_f(last->size.x - rect.width, 0.0f)) : dimension(offset->x);
    offset->y = last ? clamp_f(dimension(offset->y), 0.0f, max_f(last->size.y - rect.height, 0.0f)) : dimension(offset->y);
    // An unseen container is measured at zero offset, preserving precision
    // even for a huge requested offset. Close clamps and places its content.
    const tide_float2 placed = last ? *offset : tide_f2(0, 0);
    const int before = open_group(g, id, SCROLL, tide_f2(rect.x - placed.x, rect.y - placed.y));
    tide_gui_group *grp = top(g);
    grp->room = rect.width;
    grp->stretch = rect.width;
    grp->commands_from = g->list.count;
    grp->plains_from = g->list.plain_count;
    grp->home = g->list.target;
    grp->rects_before = g->rect_count_next;
    grp->nav_from = g->nav_count;
    grp->hot_before = g->hot_next;
    const uint32_t mark = tide_draw_push_clip(&g->list, rect);
    g->scrolling[g->scrolling_count++] = (tide_gui_scroll_state){
        .offset = offset, .before = placed, .viewport = rect, .clip_before = g->input_clip,
        .clip = g->list.clip_count, .clip_before_id = g->scroll_clip, .parent = g->scroll_id, .mark = mark};
    g->input_clip = g->scroll_clip ? intersect(g->input_clip, rect) : rect;
    g->scroll_clip = g->list.place.clip;
    g->scroll_id = id;
    return before;
}

int tide_gui_begin_layout_scroll(tide_gui *g, const uint32_t id, const tide_float2 size, tide_float2 *offset)
{
    if (g->depth == TIDE_GUI_MAX_DEPTH) return -1;
    return tide_gui_begin_scroll(g, id, tide_gui_layout_reserve(g, size), offset);
}

tide_rect tide_gui_layout_reserve_flexible(tide_gui *g, const tide_float2 size, const float min_width,
                                          const bool expand_width)
{
    const float width = dimension(size.x), height = dimension(size.y);
    const float least = min_f(dimension(min_width), width);
    const tide_gui_group *grp = top(g);
    return take(g, fit(grp, wanted(grp, width, expand_width), least), height, width, least);
}

tide_rect tide_gui_layout_reserve(tide_gui *g, const tide_float2 size)
{
    return tide_gui_layout_reserve_flexible(g, size, size.x, false);
}

// A labelled widget's label column, as narrow as it goes: the label and a gap.
static float label_least(const tide_gui *g, const char *label)
{
    return label[0] ? text_width(g, label) + 2.0f * SPACING : 0.0f;
}

// The label column with room: at least LABEL_WIDTH, so a column of them lines up.
static float label_width(const tide_gui *g, const char *label)
{
    return label[0] ? max_f(LABEL_WIDTH, label_least(g, label)) : 0.0f;
}

// Takes the next place for a labelled widget whose control is `natural` wide
// and can shrink to `least`. Draws the label and returns the control's rect.
// Squeezed, the label's column gives up its room first, down to the label: by
// what the widget lost from the width it wanted, so the labelled widgets of a
// vertical container, which want the same width, keep their columns lined up.
static tide_rect labelled(tide_gui *g, const char *label, const float natural, const float least)
{
    const tide_gui_group *grp = top(g);
    const float column = label_width(g, label);
    const float tight = label_least(g, label);
    const float want = wanted(grp, column + natural, true);
    const float width = fit(grp, want, tight + least);
    const tide_rect r = take(g, width, LINE, column + natural, tight + least);
    const float w = min_f(max_f(column - (want - width), tight), r.width);
    text_at(g, label, r.x, r.y + r.height * 0.5f, g->theme.text);
    return (tide_rect){r.x + w, r.y, r.width - w, r.height};
}

// Draws the label in its column and returns the rest of the rect.
static tide_rect after_label(tide_gui *g, const tide_rect r, const char *label)
{
    if (!label[0]) return r;
    const float w = min_f(label_width(g, label), r.width);
    text_at(g, label, r.x, r.y + r.height * 0.5f, g->theme.text);
    return (tide_rect){r.x + w, r.y, r.width - w, r.height};
}

// ---------------------------------------------------------------------------
// Widgets

void tide_gui_label(tide_gui *g, const tide_rect rect, const char *text)
{
    text_at(g, text, rect.x, rect.y + rect.height * 0.5f, g->theme.text);
}

void tide_gui_layout_label(tide_gui *g, const char *text)
{
    const float width = text_width(g, text);
    tide_gui_label(g, reserve(g, width, width, false), text);
}

// A label at its own text size, as a heading is: the theme's, for it alone.
// A size that isn't a number, or is out of the theme's range, is the nearest in it.
static float label_size(const tide_gui *g, const float size)
{
    if (!tide_is_finite_f(size)) return g->theme.text_size;
    return size < 1.0f ? 1.0f : size > 512.0f ? 512.0f : size;
}

void tide_gui_label_sized(tide_gui *g, const tide_rect rect, const char *text, const float size)
{
    const float theme = g->theme.text_size;
    g->theme.text_size = label_size(g, size);
    tide_gui_label(g, rect, text);
    g->theme.text_size = theme;
}

void tide_gui_layout_label_sized(tide_gui *g, const char *text, const float size)
{
    const float theme = g->theme.text_size;
    g->theme.text_size = label_size(g, size); // Its line is as tall as its text, as a widget's is
    tide_gui_layout_label(g, text);
    g->theme.text_size = theme;
}

bool tide_gui_button(tide_gui *g, const uint32_t id, const tide_rect rect, const char *text)
{
    bool hover, down, focused;
    const bool pressed_now = clicked(g, id, rect, &hover, &down, &focused);
    if (focused) fill_around(g, rect, BORDER, g->theme.accent);
    body(g, id, rect, down && hover ? g->theme.pressed : hover ? g->theme.hot : g->theme.control);
    text_centered(g, text, rect, g->theme.text);
    return pressed_now;
}

tide_control tide_gui_control(tide_gui *g, const uint32_t id, const tide_rect rect)
{
    const bool was_active = g->active == id;
    const bool gesture_cancelled = g->gesture_cancel == id;
    if (gesture_cancelled) g->gesture_cancel = 0;
    bool hover, down, focused;
    const bool click = clicked(g, id, rect, &hover, &down, &focused);
    if (focused) { // Its view goes on reading the keys the GUI doesn't use
        g->focus_control = id;
        g->focus_view = g->view;
    }
    // Held until the pointer lets go: not in the frame it does, when a click says so
    const bool enabled = live(g);
    if (was_active && !enabled) g->active = 0;
    tide_control c = {.hovered = hover, .pressed = down && g->mouse_held && enabled,
                      .focused = focused, .clicked = click && enabled,
                      .down = down && g->mouse_pressed && enabled, .up = down && g->mouse_released && enabled,
                      .cancelled = (was_active && !enabled) || gesture_cancelled, .position = g->mouse};
    // ...and as amounts, which ease over the theme's transition
    const float states[3] = {c.hovered ? 1.0f : 0.0f, c.pressed ? 1.0f : 0.0f, c.focused ? 1.0f : 0.0f};
    float amounts[3];
    animate_lanes(g, tide_gui_seed(id, 0xB0D1u), states, amounts, 3, g->theme.transition, NULL);
    c.hover = amounts[0];
    c.press = amounts[1];
    c.focus = amounts[2];
    return c;
}

bool tide_gui_layout_button(tide_gui *g, const uint32_t id, const char *text)
{
    const float width = text_width(g, text);
    return tide_gui_button(g, id, reserve(g, width + 2.0f * PAD, width + PAD, true), text);
}

bool tide_gui_toggle(tide_gui *g, const uint32_t id, const tide_rect rect, const char *text, bool *value)
{
    bool hover, down, focused;
    const bool changed = clicked(g, id, rect, &hover, &down, &focused);
    if (changed) *value = !*value;
    const tide_rect box = {rect.x, rect.y + (rect.height - BOX) * 0.5f, BOX, BOX};
    // Its corners no rounder than a quarter of its box, so a round theme's checkbox doesn't look like a radio
    // button; the focus's outline and the check go around them as ever
    const float radius = g->theme.radius;
    g->theme.radius = min_f(radius, BOX * 0.25f);
    if (focused) fill_around(g, box, BORDER, g->theme.accent);
    body(g, id, box, hover ? g->theme.hot : g->theme.control);
    if (*value) fill_around(g, box, -4.0f, g->theme.accent);
    g->theme.radius = radius;
    text_at(g, text, rect.x + BOX + 8.0f, rect.y + rect.height * 0.5f, g->theme.text);
    return changed;
}

bool tide_gui_layout_toggle(tide_gui *g, const uint32_t id, const char *text, bool *value)
{
    const float width = BOX + 8.0f + text_width(g, text);
    return tide_gui_toggle(g, id, reserve(g, width, width, true), text, value);
}

// A slider's track and thumb. `t` is where the value is, 0 to 1; returns
// where the player moved it, or -1. An int slider also takes the direction
// of a key's step, which it applies to the integer rather than its fraction.
static double slider(tide_gui *g, const uint32_t id, const tide_rect r, const double t, const double steps, int *step)
{
    const bool hover = hovered(g, id, r);
    const bool down = held_down(g, id, hover);
    const bool focused = focusable(g, id, true, r);
    double moved = -1.0;
    if (step) *step = 0;
    if (down && g->mouse_held && r.width > 0.0f) moved = clamp_f((g->mouse.x - r.x) / r.width, 0.0f, 1.0f);
    if (focused && pressed(g, KEY_LEFT | PAD_LEFT)) {
        moved = t - 1.0 / steps;
        if (moved < 0.0) moved = 0.0;
        if (step) *step = -1;
    }
    if (focused && pressed(g, KEY_RIGHT | PAD_RIGHT)) {
        moved = t + 1.0 / steps;
        if (moved > 1.0) moved = 1.0;
        if (step) *step = 1;
    }

    const float at = (float)(moved >= 0.0 ? moved : t);
    const float middle = r.y + r.height * 0.5f;
    const tide_rect rail = {r.x, middle - 3.0f, r.width, 6.0f};
    if (focused) fill_around(g, rail, BORDER, g->theme.accent);
    body(g, id, rail, hover || down ? g->theme.hot : g->theme.control);
    fill(g, (tide_rect){rail.x, rail.y, rail.width * at, rail.height}, g->theme.accent);
    const float thumb = LINE - 4.0f;
    fill(g, (tide_rect){r.x + r.width * at - 6.0f, middle - thumb * 0.5f, 12.0f, thumb}, down ? g->theme.accent : g->theme.text);
    return moved;
}

// Where `v` is between `min` and `max`, 0 to 1; a reversed range works too.
static double fraction(const double v, const double min, const double max)
{
    if (max == min) return 0.0;
    const double t = (v - min) / (max - min);
    return t >= 0.0 ? t > 1.0 ? 1.0 : t : 0.0; // Also NaN
}

// The track's rect: the rest of the widget after its label, minus the value on the right.
static tide_rect track(tide_gui *g, const tide_rect rect, const char *label, const char *value)
{
    const tide_rect r = after_label(g, rect, label);
    const float w = max_f(r.width - VALUE_WIDTH, 0.0f);
    text_at(g, value, r.x + w + 2.0f * SPACING, r.y + r.height * 0.5f, g->theme.text);
    return (tide_rect){r.x + 6.0f, r.y, max_f(w - 12.0f, 0.0f), r.height}; // Room for the thumb at both ends
}

bool tide_gui_slider(tide_gui *g, const uint32_t id, const tide_rect rect, const char *label, float *value,
                     const float min, const float max)
{
    char text[32];
    snprintf(text, sizeof text, "%.2f", (double)*value);
    const double moved = slider(g, id, track(g, rect, label, text), fraction(*value, min, max), 20.0, NULL);
    if (!(moved >= 0.0)) return false; // Not moved, or by a rect that isn't one (NaN).
    const float v = moved <= 0.0 ? min : moved >= 1.0 ? max : (float)((double)min + ((double)max - (double)min) * moved);
    if (v == *value) return false;
    *value = v;
    return true;
}

bool tide_gui_layout_slider(tide_gui *g, const uint32_t id, const char *label, float *value, const float min,
                            const float max)
{
    const tide_rect r = labelled(g, label, SLIDER_WIDTH + VALUE_WIDTH, LEAST_TRACK + VALUE_WIDTH);
    return tide_gui_slider(g, id, r, "", value, min, max);
}

bool tide_gui_int_slider(tide_gui *g, const uint32_t id, const tide_rect rect, const char *label, int32_t *value,
                         const int32_t min, const int32_t max)
{
    char text[32];
    snprintf(text, sizeof text, "%d", (int)*value);
    const double range = (double)max - (double)min;
    const double steps = range < 0.0 ? -range : range;
    int step;
    const double moved = slider(g, id, track(g, rect, label, text), fraction(*value, min, max), steps > 0.0 ? steps : 1.0, &step);
    if (!(moved >= 0.0)) return false; // Not moved, or by a rect that isn't one (NaN)
    if (step) {
        const int32_t low = min < max ? min : max, high = min > max ? min : max;
        int64_t next = *value < low ? low : *value > high ? high : *value;
        next += min <= max ? step : -step;
        const int32_t v = next < low ? low : next > high ? high : (int32_t)next;
        if (v == *value) return false;
        *value = v;
        return true;
    }
    // A range can be all the ints, which a float doesn't tell apart at its
    // ends: worked out in doubles, and no further than an int goes
    const double exact = (double)min + ((double)max - (double)min) * (double)moved;
    const double nearest = exact < 0.0 ? exact - 0.5 : exact + 0.5;
    const int32_t v = nearest >= 2147483647.0 ? INT32_MAX : nearest <= -2147483648.0 ? INT32_MIN : (int32_t)nearest;
    if (v == *value) return false;
    *value = v;
    return true;
}

bool tide_gui_layout_int_slider(tide_gui *g, const uint32_t id, const char *label, int32_t *value, const int32_t min,
                                const int32_t max)
{
    const tide_rect r = labelled(g, label, SLIDER_WIDTH + VALUE_WIDTH, LEAST_TRACK + VALUE_WIDTH);
    return tide_gui_int_slider(g, id, r, "", value, min, max);
}

// ---------------------------------------------------------------------------
// Number fields: click or press Enter to type, Enter or leaving to keep it,
// Escape to go back. With the focus, left and right step the value.

typedef enum number_kind {
    NUMBER_INT,
    NUMBER_FLOAT,
    NUMBER_UNIT, // A float from 0 to 1, like a color's
    NUMBER_NONE, // Not one: what a text field's typing is (tide_gui.edit_kind)
} number_kind;

static void format_number(char *out, const size_t size, const double v, const number_kind kind)
{
    if (kind == NUMBER_INT) snprintf(out, size, "%d", (int)v);
    else snprintf(out, size, "%g", v);
}

static bool number_char(const uint32_t c, const number_kind kind)
{
    if ((c >= '0' && c <= '9') || c == '-' || c == '+') return true;
    return kind != NUMBER_INT && (c == '.' || c == ',' || c == 'e' || c == 'E');
}

// Whether any of this frame's typed characters fit a number.
static bool typed_number(const tide_gui *g, const number_kind kind)
{
    for (uint32_t i = 0; i < g->text.count && i < TIDE_TEXT_MAX; i++) {
        if (number_char(g->text.chars[i], kind)) return true;
    }
    return false;
}

// Edit positions are UTF-8 byte boundaries, never a pointer into game storage.
static uint32_t edit_prev(const tide_gui *g, uint32_t at)
{
    if (at) do at--; while (at && ((unsigned char)g->edit[at] & 0xC0u) == 0x80u);
    return at;
}

static uint32_t edit_next(const tide_gui *g, uint32_t at)
{
    if (at < g->edit_len) do at++; while (at < g->edit_len && ((unsigned char)g->edit[at] & 0xC0u) == 0x80u);
    return at;
}

static tide_gui_edit_state edit_state(const tide_gui *g)
{
    tide_gui_edit_state state = {.length = g->edit_len, .caret = g->edit_caret, .anchor = g->edit_anchor};
    memcpy(state.text, g->edit, g->edit_len + 1u);
    return state;
}

static void edit_start(tide_gui *g, const bool select_all)
{
    g->edit_caret = g->edit_len;
    g->edit_anchor = select_all ? 0 : g->edit_len;
    g->edit_scroll = 0.0f;
    g->edit_history_count = 1;
    g->edit_history_at = 0;
    g->edit_history[0] = edit_state(g);
}

static void edit_restore(tide_gui *g, const uint32_t at)
{
    const tide_gui_edit_state *state = &g->edit_history[at];
    memcpy(g->edit, state->text, state->length + 1u);
    g->edit_len = state->length;
    g->edit_caret = state->caret;
    g->edit_anchor = state->anchor;
    g->edit_history_at = at;
}

static void edit_record(tide_gui *g)
{
    if (g->edit_history_at == 15) {
        memmove(g->edit_history, g->edit_history + 1, 15 * sizeof g->edit_history[0]);
        g->edit_history_at--;
    }
    g->edit_history[++g->edit_history_at] = edit_state(g);
    g->edit_history_count = g->edit_history_at + 1u;
}

static void edit_replace(tide_gui *g, const char *bytes, const uint32_t count)
{
    const uint32_t lo = g->edit_caret < g->edit_anchor ? g->edit_caret : g->edit_anchor;
    const uint32_t hi = g->edit_caret > g->edit_anchor ? g->edit_caret : g->edit_anchor;
    if (count > sizeof g->edit - 1u - (g->edit_len - (hi - lo))) return;
    memmove(g->edit + lo + count, g->edit + hi, g->edit_len - hi + 1u);
    if (count) memcpy(g->edit + lo, bytes, count);
    g->edit_len = g->edit_len - (hi - lo) + count;
    g->edit_caret = g->edit_anchor = lo + count;
    g->edit_fresh = false;
}

static bool edit_space(const tide_gui *g, const uint32_t at)
{
    return at < g->edit_len && (g->edit[at] == ' ' || g->edit[at] == '\t');
}

static uint32_t edit_word(const tide_gui *g, uint32_t at, const bool right)
{
    if (right) {
        while (at < g->edit_len && !edit_space(g, at)) at = edit_next(g, at);
        while (at < g->edit_len && edit_space(g, at)) at = edit_next(g, at);
    } else {
        while (at && edit_space(g, edit_prev(g, at))) at = edit_prev(g, at);
        while (at && !edit_space(g, edit_prev(g, at))) at = edit_prev(g, at);
    }
    return at;
}

static bool edit_apply(tide_gui *g, const number_kind kind)
{
    if (!g->text.count && !pressed(g, KEY_HOME | KEY_END | KEY_LEFT | KEY_RIGHT | KEY_DELETE | KEY_BACKSPACE |
                                      KEY_A | KEY_C | KEY_X | KEY_Y | KEY_Z)) return false;
    const bool ctrl = (g->keys & KEY_CTRL) != 0, shift = (g->keys & KEY_SHIFT) != 0;
    if (ctrl && (pressed(g, KEY_Z) || pressed(g, KEY_Y))) {
        const bool redo = pressed(g, KEY_Y) || shift;
        const uint32_t at = g->edit_history_at;
        if (redo && at + 1u < g->edit_history_count) edit_restore(g, at + 1u);
        else if (!redo && at) edit_restore(g, at - 1u);
        return at != g->edit_history_at;
    }
    if (ctrl && pressed(g, KEY_A)) { g->edit_anchor = 0; g->edit_caret = g->edit_len; }
    const uint32_t lo = g->edit_caret < g->edit_anchor ? g->edit_caret : g->edit_anchor;
    const uint32_t hi = g->edit_caret > g->edit_anchor ? g->edit_caret : g->edit_anchor;
    if (pressed(g, KEY_HOME)) g->edit_caret = 0;
    else if (pressed(g, KEY_END)) g->edit_caret = g->edit_len;
    else if (pressed(g, KEY_LEFT)) g->edit_caret = !shift && lo != hi ? lo : ctrl ? edit_word(g, g->edit_caret, false) : edit_prev(g, g->edit_caret);
    else if (pressed(g, KEY_RIGHT)) g->edit_caret = !shift && lo != hi ? hi : ctrl ? edit_word(g, g->edit_caret, true) : edit_next(g, g->edit_caret);
    if (!shift && pressed(g, KEY_HOME | KEY_END | KEY_LEFT | KEY_RIGHT)) g->edit_anchor = g->edit_caret;
    if (g->edit_history_count) g->edit_history[g->edit_history_at] = edit_state(g);
    const tide_gui_edit_state before = edit_state(g);
    if (ctrl && pressed(g, KEY_C | KEY_X) && hi > lo && g->copy) {
        char text[256];
        memcpy(text, g->edit + lo, hi - lo);
        text[hi - lo] = '\0';
        g->copy(text);
    }
    if (pressed(g, KEY_BACKSPACE | KEY_DELETE) || (ctrl && pressed(g, KEY_X) && g->copy)) {
        if (lo == hi) {
            if (pressed(g, KEY_BACKSPACE)) g->edit_anchor = ctrl ? edit_word(g, lo, false) : edit_prev(g, lo);
            else if (pressed(g, KEY_DELETE)) g->edit_anchor = ctrl ? edit_word(g, hi, true) : edit_next(g, hi);
        }
        edit_replace(g, NULL, 0);
    }
    for (uint32_t i = 0; i < g->text.count && i < TIDE_TEXT_MAX; i++) {
        uint32_t c = g->text.chars[i];
        if (kind != NUMBER_NONE) {
            if (!number_char(c, kind)) continue;
            if (c == ',') c = '.';
        } else if (c < 32 || c == 127) continue;
        char bytes[4];
        const int n = tide_utf8_encode(c, bytes);
        if (n > 0) edit_replace(g, bytes, (uint32_t)n);
    }
    const bool changed = before.length != g->edit_len || memcmp(before.text, g->edit, g->edit_len) != 0;
    if (changed) edit_record(g);
    return changed;
}

static void type_into(tide_gui *g, const number_kind kind) { (void)edit_apply(g, kind); }

static float edit_width(tide_gui *g, const uint32_t end)
{
    const char saved = g->edit[end];
    g->edit[end] = '\0';
    const float width = text_width(g, g->edit);
    g->edit[end] = saved;
    return width;
}

static void edit_pointer(tide_gui *g, const uint32_t id, const tide_rect box, const bool place)
{
    if (place) { g->active = id; g->active_seen = true; }
    if (g->active != id) return;
    g->active_seen = true;
    if (!place && !g->mouse_held) return;
    uint32_t boundaries[257], count = 0;
    for (uint32_t at = 0;; at = edit_next(g, at)) {
        boundaries[count++] = at;
        if (at == g->edit_len) break;
    }
    const float x = g->mouse.x - box.x - PAD + g->edit_scroll;
    uint32_t lo = 0, hi = count - 1u;
    while (lo < hi) {
        const uint32_t mid = (lo + hi) / 2u;
        const float edge = (edit_width(g, boundaries[mid]) + edit_width(g, boundaries[mid + 1u])) * 0.5f;
        if (x < edge) hi = mid;
        else lo = mid + 1u;
    }
    g->edit_caret = boundaries[lo];
    if (place && !(g->keys & KEY_SHIFT)) g->edit_anchor = g->edit_caret;
}

static void edit_draw(tide_gui *g, const tide_rect box)
{
    const float caret = edit_width(g, g->edit_caret), room = max_f(box.width - 2 * PAD, 1.0f);
    g->edit_scroll = clamp_f(g->edit_scroll, max_f(caret - room, 0.0f), caret);
    const uint32_t mark = tide_draw_push_clip(&g->list, box);
    const float x = box.x + PAD - g->edit_scroll, y = box.y + box.height * 0.5f;
    if (g->edit_anchor != g->edit_caret) {
        const float anchor = edit_width(g, g->edit_anchor);
        const tide_color color = shade(g, (tide_color){g->theme.accent.r, g->theme.accent.g, g->theme.accent.b, 0.35f});
        tide_draw_rect(&g->list, tide_f2(x + (anchor + caret) * 0.5f, y), tide_f2(tide_abs_f(anchor - caret), FONT), color);
    }
    text_at(g, g->edit, x, y, g->theme.text);
    tide_draw_rect(&g->list, tide_f2(x + caret, y), tide_f2(1.0f, FONT), shade(g, g->theme.text));
    tide_draw_pop(&g->list, mark);
}

// What was typed, if it's a number.
static bool parse_number(tide_gui *g, const number_kind kind, double *out)
{
    g->edit[g->edit_len] = '\0';
    char *end = NULL;
    double v;
    if (kind == NUMBER_INT) {
        const long long n = strtoll(g->edit, &end, 10);
        v = n < INT32_MIN ? INT32_MIN : n > INT32_MAX ? INT32_MAX : (double)n;
    } else if (kind == NUMBER_FLOAT) {
        v = strtof(g->edit, &end); // Round to the field's float, including text for FLT_MAX.
    } else {
        v = strtod(g->edit, &end);
    }
    if (end == g->edit || *end != '\0' || v - v != 0.0) return false; // Empty, junk after it, infinite or NaN
    if (kind == NUMBER_UNIT) v = v < 0.0 ? 0.0 : v > 1.0 ? 1.0 : v;
    *out = v;
    return true;
}

// Before a field takes the one buffer there is for what's typed. Text is its
// field's as it's typed. A number isn't until its field keeps it, and that
// field has yet to run this frame (having run, it would have ended its typing
// itself, without the focus), so a valid number is left for it, to keep as it
// runs (number_box); a field that isn't called any more keeps nothing. The
// first one left in a frame stands: a field that took the buffer from it and
// is left in turn has run, and has only been typed into this frame.
static void leave_typing(tide_gui *g)
{
    double v;
    if (!g->editing || g->edit_left || g->edit_kind == NUMBER_NONE) return;
    if (!parse_number(g, (number_kind)g->edit_kind, &v)) return;
    g->edit_left = g->editing;
    g->edit_left_value = v;
}

// Starts typing into a field. Until something is typed, the whole value is
// selected: typing replaces it.
static void start_typing(tide_gui *g, const uint32_t id, const double value, const number_kind kind)
{
    leave_typing(g);
    g->editing = id;
    g->editing_seen = true;
    g->edit_kind = (uint8_t)kind;
    if (kind == NUMBER_INT) format_number(g->edit, sizeof g->edit, value, kind);
    else snprintf(g->edit, sizeof g->edit, "%.*g", FLT_DECIMAL_DIG, value); // Editing keeps every bit of a float.
    g->edit_len = (uint32_t)strlen(g->edit);
    g->edit_fresh = true;
    edit_start(g, true);
}

// One box of a number field. Returns whether the player changed `value`, and
// says in `entered`, where there's one, whether Enter or the south button
// ended its typing now.
static bool number_box(tide_gui *g, const uint32_t id, const tide_rect box, double *value, const number_kind kind,
                       bool *entered)
{
    const bool hover = hovered(g, id, box);
    if (hover && g->mouse_pressed && !g->claimed) {
        g->claimed = true;
        g->focus = id;
        if (g->editing != id) start_typing(g, id, *value, kind);
        else edit_pointer(g, id, box, true);
    }
    if (g->editing == id) edit_pointer(g, id, box, false);
    const bool focused = focusable(g, id, true, box);
    bool changed = false;
    if (entered) *entered = false;
    if (g->editing == id) {
        g->editing_seen = true;
        if (grayed(g)) {
            g->editing = 0; // Disabled while typing: keeps the old value, as Escape does
        } else if (!focused || pressed(g, KEY_ENTER | PAD_ACCEPT)) {
            if (focused) { // Entered, with what was typed before it in this frame
                type_into(g, kind);
                if (entered) *entered = true;
            }
            double v;
            if (parse_number(g, kind, &v) && v != *value) {
                *value = v;
                changed = true;
            }
            g->editing = 0;
        } else {
            type_into(g, kind);
        }
    } else if (g->edit_left == id) { // Another field took the buffer before this one ran (leave_typing)
        g->edit_left = 0;
        if (!grayed(g) && g->edit_left_value != *value) { // Disabled meanwhile: the old value, as above
            *value = g->edit_left_value;
            changed = true;
        }
    } else if (focused) {
        if (pressed(g, KEY_ENTER)) {
            start_typing(g, id, *value, kind);
        } else if (typed_number(g, kind)) {
            start_typing(g, id, *value, kind);
            type_into(g, kind);
        } else {
            const double step = kind == NUMBER_INT ? 1.0 : kind == NUMBER_UNIT ? 0.05 : 0.1;
            double v = *value;
            if (pressed(g, KEY_LEFT | PAD_LEFT)) v -= step;
            if (pressed(g, KEY_RIGHT | PAD_RIGHT)) v += step;
            if (kind == NUMBER_UNIT) v = v < 0.0 ? 0.0 : v > 1.0 ? 1.0 : v;
            if (kind == NUMBER_INT) v = v < INT32_MIN ? INT32_MIN : v > INT32_MAX ? INT32_MAX : v;
            if (v != *value) {
                *value = v;
                changed = true;
            }
        }
    }

    if (focused) fill_around(g, box, BORDER, g->theme.accent);
    body(g, id, box, hover ? g->theme.field_hot : g->theme.field);
    char text[48];
    if (g->editing == id) edit_draw(g, box);
    else {
        format_number(text, sizeof text, *value, kind);
        text_at(g, text, box.x + 10.0f, box.y + box.height * 0.5f, g->theme.text);
    }
    return changed;
}

// A field of `n` floats side by side. `entered` is its one box's, for a field
// of one: NULL for the others.
static bool float_parts(tide_gui *g, const uint32_t id, const tide_rect r, float *parts, const int n,
                        const number_kind kind, bool *entered)
{
    const float w = max_f((r.width - SPACING * (float)(n - 1)) / (float)n, 0.0f);
    bool changed = false;
    for (int i = 0; i < n; i++) {
        const tide_rect box = {r.x + (float)i * (w + SPACING), r.y, w, r.height};
        double v = parts[i];
        if (number_box(g, tide_gui_seed(id, (uint32_t)i + 1u), box, &v, kind, entered) && (float)v != parts[i]) {
            parts[i] = (float)v;
            changed = true;
        }
    }
    return changed;
}

bool tide_gui_int_field_entered(tide_gui *g, const uint32_t id, const tide_rect rect, const char *label, int32_t *value,
                                bool *entered)
{
    double v = *value;
    if (!number_box(g, id, after_label(g, rect, label), &v, NUMBER_INT, entered) || (int32_t)v == *value) return false;
    *value = (int32_t)v;
    return true;
}

bool tide_gui_int_field(tide_gui *g, const uint32_t id, const tide_rect rect, const char *label, int32_t *value)
{
    bool entered;
    return tide_gui_int_field_entered(g, id, rect, label, value, &entered);
}

bool tide_gui_float_field_entered(tide_gui *g, const uint32_t id, const tide_rect rect, const char *label, float *value,
                                  bool *entered)
{
    float parts[1] = {*value};
    if (!float_parts(g, id, after_label(g, rect, label), parts, 1, NUMBER_FLOAT, entered)) return false;
    *value = parts[0];
    return true;
}

bool tide_gui_float_field(tide_gui *g, const uint32_t id, const tide_rect rect, const char *label, float *value)
{
    bool entered;
    return tide_gui_float_field_entered(g, id, rect, label, value, &entered);
}

bool tide_gui_float2_field(tide_gui *g, const uint32_t id, const tide_rect rect, const char *label, tide_float2 *value)
{
    float parts[2] = {value->x, value->y};
    if (!float_parts(g, id, after_label(g, rect, label), parts, 2, NUMBER_FLOAT, NULL)) return false;
    *value = tide_f2(parts[0], parts[1]);
    return true;
}

bool tide_gui_float3_field(tide_gui *g, const uint32_t id, const tide_rect rect, const char *label, tide_float3 *value)
{
    float parts[3] = {value->x, value->y, value->z};
    if (!float_parts(g, id, after_label(g, rect, label), parts, 3, NUMBER_FLOAT, NULL)) return false;
    *value = tide_f3(parts[0], parts[1], parts[2]);
    return true;
}

bool tide_gui_float4_field(tide_gui *g, const uint32_t id, const tide_rect rect, const char *label, tide_float4 *value)
{
    float parts[4] = {value->x, value->y, value->z, value->w};
    if (!float_parts(g, id, after_label(g, rect, label), parts, 4, NUMBER_FLOAT, NULL)) return false;
    *value = tide_f4(parts[0], parts[1], parts[2], parts[3]);
    return true;
}

bool tide_gui_color_field(tide_gui *g, const uint32_t id, const tide_rect rect, const char *label, tide_color *value)
{
    const tide_rect r = after_label(g, rect, label);
    const tide_rect swatch = {r.x, r.y, min_f(r.height, r.width), r.height};
    body_paint(g, swatch, shade(g, g->theme.control), g->theme.radius);
    fill_around(g, swatch, -3.0f, *value);
    float parts[4] = {value->r, value->g, value->b, value->a};
    const float skip = swatch.width + SPACING;
    const tide_rect rest = {r.x + skip, r.y, max_f(r.width - skip, 0.0f), r.height};
    if (!float_parts(g, id, rest, parts, 4, NUMBER_UNIT, NULL)) return false;
    *value = (tide_color){parts[0], parts[1], parts[2], parts[3]};
    return true;
}

// ---------------------------------------------------------------------------
// Text fields: click, or press Enter or type with the focus, to type; the
// value changes as you type. Enter or Escape stop typing, and Enter is the one
// a view can ask about (`entered`).

static void start_text(tide_gui *g, const uint32_t id, const tide_str value)
{
    leave_typing(g);
    g->editing = id;
    g->editing_seen = true;
    g->edit_kind = NUMBER_NONE;
    int32_t n = value.bytes < (int32_t)sizeof g->edit - 1 ? value.bytes : (int32_t)sizeof g->edit - 1;
    while (n > 0 && n < value.bytes && ((unsigned char)value.ptr[n] & 0xC0u) == 0x80u) n--; // Whole characters
    memcpy(g->edit, value.ptr, (size_t)n);
    g->edit_len = (uint32_t)n;
    g->edit[n] = '\0';
    g->edit_fresh = false;
    edit_start(g, false);
}

// Typed characters and Backspace into the edit buffer; whether it changed.
static bool type_text(tide_gui *g) { return edit_apply(g, NUMBER_NONE); }

bool tide_gui_text_field_entered(tide_gui *g, const uint32_t id, const tide_rect rect, const char *label,
                                 const tide_textref value, bool *entered)
{
    const tide_rect box = after_label(g, rect, label);
    const bool hover = hovered(g, id, box);
    if (hover && g->mouse_pressed && !g->claimed) {
        g->claimed = true;
        g->focus = id;
        if (g->editing != id) start_text(g, id, tide_textref_get(value));
        edit_pointer(g, id, box, true);
    }
    if (g->editing == id) edit_pointer(g, id, box, false);
    const bool focused = focusable(g, id, false, box);
    bool changed = false;
    *entered = false;
    if (g->editing == id) {
        g->editing_seen = true;
        if (!focused) {
            g->editing = 0;
        } else {
            changed = type_text(g);
            if (pressed(g, KEY_ENTER)) { // Entered, with what was typed before it in this frame
                g->editing = 0;
                *entered = true;
            }
        }
    } else if (focused && (pressed(g, KEY_ENTER) || g->text.count > 0)) {
        start_text(g, id, tide_textref_get(value));
        changed = type_text(g);
    }
    if (changed) {
        // A copy of the buffer, which keeps changing as the player types.
        const tide_str typed = {g->edit, (int32_t)g->edit_len, tide_utf8_chars(g->edit, (int32_t)g->edit_len)};
        tide_textref_set(value, tide_str_add(TIDE_STR_EMPTY, typed));
    }

    if (focused) fill_around(g, box, BORDER, g->theme.accent);
    body(g, id, box, hover ? g->theme.field_hot : g->theme.field);
    const tide_str shown = tide_textref_get(value);
    char text[300];
    if (g->editing == id) edit_draw(g, box);
    else {
        snprintf(text, sizeof text, "%.*s", (int)shown.bytes, shown.ptr);
        text_at(g, text, box.x + 10.0f, box.y + box.height * 0.5f, g->theme.text);
    }
    return changed;
}

bool tide_gui_text_field(tide_gui *g, const uint32_t id, const tide_rect rect, const char *label, const tide_textref value)
{
    bool entered;
    return tide_gui_text_field_entered(g, id, rect, label, value, &entered);
}

bool tide_gui_layout_text_field_entered(tide_gui *g, const uint32_t id, const char *label, const tide_textref value,
                                        bool *entered)
{
    const tide_rect rect = labelled(g, label, 2.0f * FIELD_WIDTH, 2.0f * LEAST_FIELD);
    return tide_gui_text_field_entered(g, id, rect, "", value, entered);
}

bool tide_gui_layout_text_field(tide_gui *g, const uint32_t id, const char *label, const tide_textref value)
{
    bool entered;
    return tide_gui_layout_text_field_entered(g, id, label, value, &entered);
}

bool tide_gui_layout_int_field_entered(tide_gui *g, const uint32_t id, const char *label, int32_t *value, bool *entered)
{
    return tide_gui_int_field_entered(g, id, labelled(g, label, FIELD_WIDTH, LEAST_FIELD), "", value, entered);
}

bool tide_gui_layout_int_field(tide_gui *g, const uint32_t id, const char *label, int32_t *value)
{
    bool entered;
    return tide_gui_layout_int_field_entered(g, id, label, value, &entered);
}

bool tide_gui_layout_float_field_entered(tide_gui *g, const uint32_t id, const char *label, float *value, bool *entered)
{
    return tide_gui_float_field_entered(g, id, labelled(g, label, FIELD_WIDTH, LEAST_FIELD), "", value, entered);
}

bool tide_gui_layout_float_field(tide_gui *g, const uint32_t id, const char *label, float *value)
{
    bool entered;
    return tide_gui_layout_float_field_entered(g, id, label, value, &entered);
}

static float parts_width(const tide_gui *g, const int n, const float each)
{
    return (float)n * each + (float)(n - 1) * SPACING;
}

bool tide_gui_layout_float2_field(tide_gui *g, const uint32_t id, const char *label, tide_float2 *value)
{
    const tide_rect r = labelled(g, label, parts_width(g, 2, PART_WIDTH), parts_width(g, 2, LEAST_PART));
    return tide_gui_float2_field(g, id, r, "", value);
}

bool tide_gui_layout_float3_field(tide_gui *g, const uint32_t id, const char *label, tide_float3 *value)
{
    const tide_rect r = labelled(g, label, parts_width(g, 3, PART_WIDTH), parts_width(g, 3, LEAST_PART));
    return tide_gui_float3_field(g, id, r, "", value);
}

bool tide_gui_layout_float4_field(tide_gui *g, const uint32_t id, const char *label, tide_float4 *value)
{
    const tide_rect r = labelled(g, label, parts_width(g, 4, PART_WIDTH), parts_width(g, 4, LEAST_PART));
    return tide_gui_float4_field(g, id, r, "", value);
}

bool tide_gui_layout_color_field(tide_gui *g, const uint32_t id, const char *label, tide_color *value)
{
    const float swatch = LINE + SPACING;
    const tide_rect r =
        labelled(g, label, swatch + parts_width(g, 4, COLOR_PART_WIDTH), swatch + parts_width(g, 4, LEAST_PART));
    return tide_gui_color_field(g, id, r, "", value);
}
