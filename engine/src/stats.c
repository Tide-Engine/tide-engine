#include "tide/stats.h"

#include <float.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/math.h"
#include "tide/page.h"

// See tide/stats.h.

static const float TEXT = 16.0f;  // Its font size
static const float PITCH = 20.0f; // From one line to the next
static const float PAD_X = 8.0f;  // Inside the panel: beside its lines
static const float PAD_Y = 6.0f;  // ...and above and under them
static const float MARGIN = 8.0f; // From the screen's edges
static const float MARK = 16.0f;  // What the arrow takes before the line that's always there
static const float GAP = 24.0f;   // Between the labels' column and the values'
static const float GROUP = 8.0f;  // Between one group of lines and the next
static const float INDENT = 12.0f; // A line that's a part of the one before it
static const float ARROW = 11.0f;  // An arrow before a number, the room after it included

static void reset_times(tide_stats *s)
{
    s->slow_count = 0;
    s->span = 0.0;
    s->profile = NULL;
    s->system_count = 0;
}

// Closed, the panel is the one line's again, and may be as narrow as that.
static void reset_width(tide_stats *s)
{
    s->held = 0.0f;
}

static bool contains(const tide_rect r, const tide_float2 p)
{
    return p.x >= r.x && p.y >= r.y && p.x < r.x + r.width && p.y < r.y + r.height;
}

// The devices count from the bottom left, y up; the drawer from the top left, y down.
static tide_float2 from_top(const tide_float2 p, const tide_float2 screen)
{
    return tide_f2(p.x, screen.y - p.y);
}

// ---------------------------------------------------------------------------
// Whose a press is

// The slot of the touch with `id`, while it has one.
static const tide_touch *touch_of(const tide_touchscreen *ts, const int32_t id)
{
    for (int i = 0; i < TIDE_TOUCHES; i++) {
        if (ts->touches.at[i].id == id) return &ts->touches.at[i];
    }
    return NULL;
}

bool tide_stats_input(tide_stats *s, const tide_devices *d, const tide_float2 screen, const bool key)
{
    bool flipped = key && !s->key_held;
    s->key_held = key;

    // The mouse. A cursor the window holds (Cursor.Lock) is hidden, and on nothing.
    const bool on = s->shown && !d->mouse.locked && contains(s->rect, from_top(d->mouse.position, screen));
    unsigned held = 0; // Its buttons that are down
    unsigned live = 0; // ...and those a sample has yet to see the last of
    unsigned bit = 1;
#define BUTTON(name)                                                                                        \
    if (d->mouse.name.held) held |= bit;                                                                    \
    if (d->mouse.name.held || d->mouse.name.pressed || d->mouse.name.down || d->mouse.name.up) live |= bit; \
    bit <<= 1;
    TIDE_MOUSE_BUTTONS(BUTTON)
#undef BUTTON
    const unsigned went_down = held & ~(unsigned)s->buttons_held;
    const unsigned went_up = ~held & s->buttons_held;
    unsigned ours = s->buttons;
    // A click: the left button, which went down on the drawer, let go on it
    if ((ours & went_up & 1u) && on) flipped = !flipped;
    // A button that goes down with the cursor on the drawer is the drawer's,
    // and the game's anywhere else. The drawer's stays so once it's let go,
    // while the devices still say it was down: the next sample would take
    // that for a press.
    ours = on ? ours | went_down : ours & ~went_down;
    s->buttons = (uint8_t)(ours & live);
    s->buttons_held = (uint8_t)held;
    s->hovered = on && !d->pointer.touch;

    // Fingers. One that touched the drawer is the drawer's while it has its
    // slot: until a sample cleared it or another finger took it, once it
    // lifted, so nothing reads that it was there.
    const tide_touchscreen *ts = &d->touchscreen;
    for (int i = 0; i < TIDE_TOUCHES; i++) {
        tide_stats_finger *f = &s->fingers[i];
        if (!f->id) continue;
        const tide_touch *t = touch_of(ts, f->id);
        if (!t) {
            *f = (tide_stats_finger){0};
            continue;
        }
        if (!f->held || t->press.held) continue;
        f->held = false; // A tap: it lifts on the drawer
        if (s->shown && contains(s->rect, from_top(t->position, screen))) flipped = !flipped;
    }
    for (int i = 0; s->shown && i < TIDE_TOUCHES; i++) {
        const tide_touch *t = &ts->touches.at[i];
        if (!t->id || !t->began || !contains(s->rect, from_top(t->startPosition, screen))) continue;
        // As many places as a touchscreen has fingers, so one is free
        for (int k = 0; k < TIDE_TOUCHES; k++) {
            if (s->fingers[k].id) continue;
            s->fingers[k] = (tide_stats_finger){.id = t->id, .held = true};
            break;
        }
    }

    if (flipped) {
        s->open = !s->open;
        reset_times(s);
        reset_width(s);
    }
    return flipped;
}

bool tide_stats_pressed(const tide_stats *s)
{
    if (s->buttons) return true;
    for (int i = 0; i < TIDE_TOUCHES; i++) {
        if (s->fingers[i].id) return true;
    }
    return false;
}

void tide_stats_hide(const tide_stats *s, tide_devices *d)
{
    unsigned bit = 1;
#define BUTTON(name)                                         \
    if (s->buttons & bit) d->mouse.name = (tide_button){0}; \
    bit <<= 1;
    TIDE_MOUSE_BUTTONS(BUTTON)
#undef BUTTON
    // The pointer's press is the left button's, or the primary touch's
    if ((s->buttons & 1u) && !d->pointer.touch) d->pointer.press = (tide_button){0};
    tide_touchscreen *ts = &d->touchscreen;
    for (int i = 0; i < TIDE_TOUCHES; i++) {
        const int32_t id = s->fingers[i].id;
        if (!id) continue;
        for (int k = 0; k < TIDE_TOUCHES; k++) {
            if (ts->touches.at[k].id == id) ts->touches.at[k] = (tide_touch){0};
        }
        if (ts->primaryTouch.id != id) continue;
        ts->primaryTouch = (tide_touch){0};
        if (d->pointer.touch) d->pointer.press = (tide_button){0};
    }
}

void tide_stats_off(tide_stats *s)
{
    s->shown = false;
    s->hovered = false;
    s->line_count = 0;
    s->buttons = 0;
    memset(s->fingers, 0, sizeof s->fingers);
    reset_times(s);
    reset_width(s);
}

void tide_stats_free(tide_stats *s)
{
    free(s->systems);
    *s = (tide_stats){0};
}

// ---------------------------------------------------------------------------
// Its systems' averages

static void count_systems(tide_stats *s, const uint32_t count)
{
    if (count > s->system_room) {
        s->systems = tide_realloc(s->systems, s->system_room * sizeof *s->systems, count * sizeof *s->systems);
        s->system_room = count;
    }
    if (count) memset(s->systems, 0, count * sizeof *s->systems);
    s->system_count = count;
}

void tide_stats_collect(tide_stats *s, const tide_profile *profile, const double seconds)
{
    if (!s->open || !profile) {
        reset_times(s);
        return;
    }
    const tide_profile_times *times = &profile->systems;
    if (s->profile != profile) {
        // Establish a baseline before this frame's ticks. A profile may have
        // kept measuring for the F7 profiler while the drawer was closed.
        reset_times(s);
        s->profile = profile;
        count_systems(s, times->count);
        for (uint32_t i = 0; i < times->count; i++) {
            tide_stats_system *to = &s->systems[i];
            const tide_profile_time *from = &times->items[i];
            memcpy(to->name, from->name, sizeof to->name);
            to->from_ms = from->measured_ms;
            to->from_count = from->measurements;
        }
        return;
    }

    bool changed = times->count != s->system_count;
    if (changed) count_systems(s, times->count); // Profile counts reset every system's counters
    for (uint32_t i = 0; i < times->count; i++) {
        const tide_stats_system *to = &s->systems[i];
        const tide_profile_time *from = &times->items[i];
        changed |= memcmp(to->name, from->name, sizeof to->name) != 0 || from->measurements < to->from_count
                   || from->measured_ms < to->from_ms;
    }
    if (changed) {
        // A replaced build or match never inherits the old one's ranking or
        // half-finished interval. Unchanged systems keep their counter baseline.
        s->slow_count = 0;
        s->span = 0.0;
        for (uint32_t i = 0; i < times->count; i++) {
            s->systems[i].total_ms = 0.0;
            s->systems[i].count = 0;
        }
    }
    for (uint32_t i = 0; i < times->count; i++) {
        tide_stats_system *to = &s->systems[i];
        const tide_profile_time *from = &times->items[i];
        if (memcmp(to->name, from->name, sizeof to->name) != 0 || from->measurements < to->from_count
            || from->measured_ms < to->from_ms) {
            *to = (tide_stats_system){0};
            memcpy(to->name, from->name, sizeof to->name);
        }
        to->total_ms += from->measured_ms - to->from_ms;
        to->count += from->measurements - to->from_count;
        to->from_ms = from->measured_ms;
        to->from_count = from->measurements;
    }
    if (seconds > 0.0 && seconds <= DBL_MAX) s->span += seconds;
    if (s->span < 0.5) return;

    uint32_t slow[3], count = 0;
    for (; count < 3; count++) {
        uint32_t best = UINT32_MAX;
        for (uint32_t i = 0; i < s->system_count; i++) {
            const tide_stats_system *t = &s->systems[i];
            bool taken = false;
            for (uint32_t k = 0; k < count; k++) taken |= slow[k] == i;
            if (taken || !t->name[0] || !t->count) continue;
            if (best == UINT32_MAX || t->total_ms / (double)t->count
                                          > s->systems[best].total_ms / (double)s->systems[best].count) best = i;
        }
        if (best == UINT32_MAX) break;
        slow[count] = best;
    }
    // With no ticks, keep the last measured averages, rather than manufacture
    // zero measurements of systems that never ran in these frames.
    if (count) {
        // A system that's still among the three keeps its row, and one that
        // joins them takes a row that's free: the first time, slowest first
        tide_stats_time rows[3] = {0};
        bool placed[3] = {false, false, false};
        for (uint32_t k = 0; k < s->slow_count; k++) {
            for (uint32_t i = 0; i < count; i++) {
                const tide_stats_system *from = &s->systems[slow[i]];
                if (placed[i] || strncmp(s->slow[k].name, from->name, sizeof from->name) != 0) continue;
                memcpy(rows[k].name, from->name, sizeof rows[k].name);
                rows[k].ms = from->total_ms / (double)from->count;
                placed[i] = true;
                break;
            }
        }
        for (uint32_t i = 0; i < count; i++) {
            if (placed[i]) continue;
            const tide_stats_system *from = &s->systems[slow[i]];
            for (uint32_t k = 0; k < 3; k++) {
                if (rows[k].name[0]) continue;
                memcpy(rows[k].name, from->name, sizeof rows[k].name);
                rows[k].ms = from->total_ms / (double)from->count;
                break;
            }
        }
        // Fewer than before: the rows that are left close up, in their order
        s->slow_count = 0;
        for (uint32_t k = 0; k < 3; k++) {
            if (rows[k].name[0]) s->slow[s->slow_count++] = rows[k];
        }
    }
    s->span = 0.0;
    for (uint32_t i = 0; i < s->system_count; i++) {
        s->systems[i].total_ms = 0.0;
        s->systems[i].count = 0;
    }
}

// ---------------------------------------------------------------------------
// Its lines

// The next line, or the last one again past as many as it keeps (never: they're counted to fit).
static tide_stats_line *add_line(tide_stats *s)
{
    if (s->line_count == TIDE_STATS_LINES) s->line_count--;
    tide_stats_line *l = &s->lines[s->line_count++];
    *l = (tide_stats_line){0};
    return l;
}

// A line of the list: its label, and its value from a format.
static tide_stats_line *line(tide_stats *s, const char *label, const char *format, ...)
{
    tide_stats_line *l = add_line(s);
    snprintf(l->label, sizeof l->label, "%s", label);
    va_list args;
    va_start(args, format);
    vsnprintf(l->value, sizeof l->value, format, args);
    va_end(args);
    return l;
}

// The lines from `from` on are a group of their own, with a little space
// before it, unless they're the list's first.
static void group(tide_stats *s, const uint32_t from)
{
    if (from > 1 && from < s->line_count) s->lines[from].gap = true;
}

// The three systems that took longest on average, each in its row. Ones
// measured below the clock's resolution have a line too, so rows don't come
// and go.
static void add_systems(tide_stats *s)
{
    const uint32_t from = s->line_count;
    for (uint32_t k = 0; k < s->slow_count; k++) line(s, s->slow[k].name, "%.2f ms", s->slow[k].ms);
    group(s, from);
}

// What the drawer shows of `v`: the line that's always there, and while it's
// open, the list above it, from the top, in groups: what a frame takes, the
// match's, the machine's, and the systems that take longest.
static void fill(tide_stats *s, const tide_stats_values *v)
{
    s->line_count = 0;
    // A match another machine runs has a ping to keep an eye on
    tide_stats_line *first = add_line(s);
    if (v->match && v->remote) {
        snprintf(first->label, sizeof first->label, "%.0f fps   ping %u ms", v->fps, (unsigned)v->ping_ms);
    } else {
        snprintf(first->label, sizeof first->label, "%.0f fps", v->fps);
    }
    if (!s->open) {
        reset_times(s);
        return;
    }
    // A frame's work on its thread is the whole, and rendering a part of it,
    // listed under it; the GPU's time is beside them, as the two overlap, with
    // the draw calls it took in its label, or on a line of their own where
    // the backend has no GPU timer
    char label[TIDE_PROFILE_NAME];
    const bool work = v->work_ms >= 0.0;
    if (work) line(s, "cpu", "%.1f ms", v->work_ms);
    if (v->render_known) line(s, "render", "%.2f ms", v->render_ms)->indent = work;
    if (v->gpu_known) {
        snprintf(label, sizeof label, "gpu (%u draw calls)", (unsigned)v->draw_calls);
        line(s, label, "%.2f ms", v->gpu_ms);
    } else {
        line(s, "draw calls", "%u", (unsigned)v->draw_calls);
    }
    if (v->match) {
        const uint32_t from = s->line_count;
        // The input's delay goes with the ping it comes of, where the ping is in the list
        if (!v->remote) {
            if (v->delay_ticks) snprintf(label, sizeof label, "ping (delay %u ticks)", (unsigned)v->delay_ticks);
            else snprintf(label, sizeof label, "ping");
            line(s, label, "%u ms", (unsigned)v->ping_ms);
        } else if (v->delay_ticks) {
            line(s, "delay", "%u ticks", (unsigned)v->delay_ticks);
        }
        // Up and down on one line, an arrow each, drawn by the drawer: the
        // font's are too thin to tell from a 1 at this size
        line(s, "network", "%.1f  %.1f KB/s", v->up / 1024.0, v->down / 1024.0)->arrows = true;
        line(s, "entities", "%u", (unsigned)v->entities);
        group(s, from);
    }
    const uint32_t from = s->line_count;
    line(s, "threads", "%u", (unsigned)(v->threads ? v->threads : 1u));
    if (v->memory) line(s, "memory", "%.0f MB", (double)v->memory / (1024.0 * 1024.0));
    if (v->renderer && v->renderer[0]) line(s, "renderer", "%s", v->renderer);
    group(s, from);
    if (v->match && v->profile == s->profile) add_systems(s);
}

static float width_of(const tide_measure_fn measure, const char *text)
{
    if (!text[0]) return 0.0f;
    return measure ? tide_ceil_f(measure(text, TEXT, NULL, TIDE_FONT_REGULAR).x) : (float)strlen(text) * TEXT * 0.5f;
}

// A value of two numbers with arrows: the first number's text (in `first`,
// NUL-terminated) and the second's, and how wide each is.
typedef struct arrowed {
    char first[32];
    const char *second;
    float first_width, second_width;
} arrowed;

static arrowed split_arrowed(const tide_stats_line *l, const tide_measure_fn measure)
{
    arrowed a = {.second = l->value};
    const char *at = strstr(l->value, "  ");
    if (at) {
        const size_t n = (size_t)(at - l->value) < sizeof a.first - 1 ? (size_t)(at - l->value) : sizeof a.first - 1;
        memcpy(a.first, l->value, n);
        a.first[n] = 0;
        a.second = at + 2;
    }
    a.first_width = width_of(measure, a.first);
    a.second_width = width_of(measure, a.second);
    return a;
}

// How wide a value draws: its text, or its two numbers with an arrow before each.
static float value_width(const tide_stats_line *l, const tide_measure_fn measure)
{
    if (!l->arrows) return width_of(measure, l->value);
    const arrowed a = split_arrowed(l, measure);
    return ARROW + a.first_width + GAP * 0.5f + ARROW + a.second_width;
}

// An arrow in a slot ARROW wide whose left is `x`, on a line whose text top
// is `y`: up or down, a shaft with a head, as the drawer's chevron is drawn.
static void draw_arrow(tide_draw_list *list, const float x, const float y, const bool up, const tide_color color)
{
    const float mid = x + 4.0f;
    const float top = y + 2.0f, bottom = y + TEXT - 2.0f;
    const float tip = up ? top : bottom, back = up ? 3.5f : -3.5f;
    tide_draw_thick_line(list, tide_f2(mid, top), tide_f2(mid, bottom), 1.5f, color);
    tide_draw_thick_line(list, tide_f2(mid - 3.5f, tip + back), tide_f2(mid, tip), 1.5f, color);
    tide_draw_thick_line(list, tide_f2(mid, tip), tide_f2(mid + 3.5f, tip + back), 1.5f, color);
}

// A label in `most` pixels: how wide it is, once what doesn't fit is cut from
// its end, where a character starts. A system's line loses the end of its
// name and keeps what the system took.
static float fit(char *label, const float most, const tide_measure_fn measure)
{
    for (;;) {
        const float width = width_of(measure, label);
        size_t at = strlen(label);
        if (width <= most || at <= 1) return width;
        do at--;
        while (at > 1 && ((unsigned char)label[at] & 0xC0u) == 0x80u); // Not inside a UTF-8 character
        label[at] = 0;
    }
}

// A rect by its top left corner, as the screen's pixels go.
static void rect(tide_draw_list *list, const float x, const float y, const float width, const float height,
                 const tide_color color)
{
    tide_draw_rect(list, tide_f2(x + width * 0.5f, y + height * 0.5f), tide_f2(width, height), color);
}

// How far down the list line `i` of it starts: the lines before it, and the
// gaps between their groups.
static float line_top(const tide_stats *s, const uint32_t i)
{
    float y = 0.0f;
    for (uint32_t k = 1; k < i; k++) y += PITCH + (s->lines[k + 1].gap ? GROUP : 0.0f);
    return y;
}

void tide_stats_draw(tide_stats *s, const tide_stats_values *values, tide_draw_list *list, const tide_float2 screen,
                     const tide_measure_fn measure)
{
    fill(s, values);
    // On a low screen, the list keeps the lines it has room for, from its top
    const float room = screen.y - 2.0f * MARGIN - 2.0f * PAD_Y;
    while (s->line_count > 1 && line_top(s, s->line_count - 1) + 2.0f * PITCH > room) s->line_count--;

    // Two columns, the labels' and the values', each as wide as its widest,
    // and no wider than the screen: a label is cut where it has to be
    float most = screen.x - 2.0f * (MARGIN + PAD_X);
    if (most < MARK + TEXT) most = MARK + TEXT;
    float values_width = 0.0f;
    for (uint32_t i = 1; i < s->line_count; i++) {
        const float w = value_width(&s->lines[i], measure);
        if (w > values_width) values_width = w;
    }
    float labels_most = most - (s->line_count > 1 ? GAP + values_width : 0.0f);
    if (labels_most < TEXT) labels_most = TEXT;
    float labels_width = 0.0f;
    for (uint32_t i = 1; i < s->line_count; i++) {
        tide_stats_line *l = &s->lines[i];
        const float inset = l->indent ? INDENT : 0.0f;
        const float w = fit(l->label, labels_most - inset, measure) + inset;
        if (w > labels_width) labels_width = w;
    }
    const float first_width = fit(s->lines[0].label, most - MARK, measure);
    float inner = first_width + MARK;
    if (s->line_count > 1 && labels_width + GAP + values_width > inner) inner = labels_width + GAP + values_width;
    // Open, the panel keeps the widest it has been, so its edge doesn't jump
    // as a longer name comes into the list and goes; the screen still bounds it
    if (s->open) {
        if (inner > s->held) s->held = inner;
        inner = s->held;
    }
    if (inner > most) inner = most;
    const float width = inner + 2.0f * PAD_X;
    const float height = (s->line_count > 1 ? line_top(s, s->line_count - 1) + PITCH : 0.0f) + PITCH + 2.0f * PAD_Y;
    const float left = screen.x - MARGIN - width;
    const float top = screen.y - MARGIN - height;
    const float right = left + width - PAD_X; // Where every value ends, and the line that's always there

    const tide_color plain = {200.0f / 255.0f, 200.0f / 255.0f, 200.0f / 255.0f, 1.0f};
    const tide_color dim = {150.0f / 255.0f, 150.0f / 255.0f, 150.0f / 255.0f, 1.0f};
    const tide_color bright = {1.0f, 1.0f, 1.0f, 1.0f};
    const tide_color first = s->hovered ? bright : plain; // Lit under the cursor: it's what a click is for
    tide_draw_defaults(list); // What a list had set isn't the drawer's
    tide_draw_set_screen(list);
    rect(list, left, top, width, height, (tide_color){0.0f, 0.0f, 0.0f, s->hovered ? 0.72f : 0.55f});
    const float inset = (PITCH - TEXT) * 0.5f; // A line's text, in the middle of its pitch
    for (uint32_t i = 1; i < s->line_count; i++) {
        const tide_stats_line *l = &s->lines[i];
        const float y = top + PAD_Y + line_top(s, i) + inset;
        // Labels in a dimmer column on the left, so the numbers stand out beside them
        tide_draw_text(list, l->label, tide_f2(left + PAD_X + (l->indent ? INDENT : 0.0f), y), TEXT, dim);
        if (l->arrows) {
            // From the right: the second number with its arrow before it, then the first with its own
            const arrowed a = split_arrowed(l, measure);
            float x = right - a.second_width;
            tide_draw_text(list, a.second, tide_f2(x, y), TEXT, plain);
            x -= ARROW;
            draw_arrow(list, x, y, false, plain);
            x -= GAP * 0.5f + a.first_width;
            tide_draw_text(list, a.first, tide_f2(x, y), TEXT, plain);
            x -= ARROW;
            draw_arrow(list, x, y, true, plain);
        } else {
            tide_draw_text(list, l->value, tide_f2(right - width_of(measure, l->value), y), TEXT, plain);
        }
    }
    // The line that's always there, under the list, and before it an arrow:
    // up while there's a list to open, down while it's open
    const float y = top + height - PAD_Y - PITCH;
    tide_draw_text(list, s->lines[0].label, tide_f2(right - first_width, y + inset), TEXT, first);
    const tide_float2 tip = tide_f2(right - first_width - MARK + 5.0f, y + PITCH * 0.5f + (s->open ? 2.0f : -2.0f));
    const float back = s->open ? -4.0f : 4.0f;
    tide_draw_thick_line(list, tide_f2(tip.x - 4.0f, tip.y + back), tip, 1.5f, first);
    tide_draw_thick_line(list, tip, tide_f2(tip.x + 4.0f, tip.y + back), 1.5f, first);

    // The pointer is on it out to the screen's corner: nothing else is there
    s->rect = (tide_rect){left, top, screen.x - left, screen.y - top};
    s->shown = true;
}
