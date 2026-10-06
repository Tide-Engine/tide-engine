#include "tide/stats.h"

#include <float.h>
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

static void reset_times(tide_stats *s)
{
    s->slow_count = 0;
    s->span = 0.0;
    s->profile = NULL;
    s->system_count = 0;
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
        s->slow_count = count;
        for (uint32_t i = 0; i < count; i++) {
            const tide_stats_system *from = &s->systems[slow[i]];
            memcpy(s->slow[i].name, from->name, sizeof s->slow[i].name);
            s->slow[i].ms = from->total_ms / (double)from->count;
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

#define LINE(s, ...) snprintf(add_line(s)->text, sizeof(((tide_stats_line *)0)->text), __VA_ARGS__)

// The three systems that took longest on average, slowest first. Ones measured
// below the clock's resolution have a line too, so rows don't come and go.
static void add_systems(tide_stats *s)
{
    for (uint32_t k = 0; k < s->slow_count; k++) {
        tide_stats_line *l = add_line(s);
        snprintf(l->text, sizeof l->text, "%s %.2f ms", s->slow[k].name, s->slow[k].ms);
        l->name = (uint32_t)strlen(s->slow[k].name);
    }
}

// What the drawer shows of `v`: the line that's always there, and while it's
// open, the list above it, from the top.
static void fill(tide_stats *s, const tide_stats_values *v)
{
    s->line_count = 0;
    // A match another machine runs has a ping to keep an eye on
    if (v->match && v->remote) LINE(s, "%.0f fps   ping %u ms", v->fps, (unsigned)v->ping_ms);
    else LINE(s, "%.0f fps", v->fps);
    if (!s->open) {
        reset_times(s);
        return;
    }
    if (v->work_ms >= 0.0) LINE(s, "cpu %.1f ms", v->work_ms);
    if (v->render_known) LINE(s, "render cpu %.2f ms", v->render_ms);
    if (v->gpu_known) LINE(s, "gpu %.2f ms", v->gpu_ms);
    if (v->match) {
        if (!v->remote) LINE(s, "ping %u ms", (unsigned)v->ping_ms);
        if (v->delay_ticks) LINE(s, "delay %u ticks", (unsigned)v->delay_ticks);
        LINE(s, "up %.1f KB/s", v->up / 1024.0);
        LINE(s, "down %.1f KB/s", v->down / 1024.0);
        LINE(s, "tick %d", (int)v->tick);
        LINE(s, "%u entities", (unsigned)v->entities);
    }
    LINE(s, "threads %u", (unsigned)(v->threads ? v->threads : 1u));
    if (v->memory) LINE(s, "memory %.0f MB", (double)v->memory / (1024.0 * 1024.0));
    if (v->renderer && v->renderer[0]) LINE(s, "%s", v->renderer);
    LINE(s, "%u draw calls", (unsigned)v->draw_calls);
    if (v->match && v->profile == s->profile) add_systems(s);
}

static float width_of(const tide_measure_fn measure, const char *text)
{
    return measure ? tide_ceil_f(measure(text, TEXT, NULL, TIDE_FONT_REGULAR).x) : (float)strlen(text) * TEXT * 0.5f;
}

// A line in `most` pixels: how wide it is, once what doesn't fit is cut, where
// a character starts. A system's line loses the end of its name, and keeps
// what the system took; any other, its end.
static float fit(tide_stats_line *l, const float most, const tide_measure_fn measure)
{
    for (;;) {
        const float width = width_of(measure, l->text);
        size_t at = l->name > 1 ? l->name : strlen(l->text);
        if (width <= most || at <= 1) return width;
        do at--;
        while (at > 1 && ((unsigned char)l->text[at] & 0xC0u) == 0x80u); // Not inside a UTF-8 character
        if (l->name > 1) {
            memmove(l->text + at, l->text + l->name, strlen(l->text + l->name) + 1);
            l->name = (uint32_t)at;
        } else {
            l->text[at] = '\0';
        }
    }
}

// A rect by its top left corner, as the screen's pixels go.
static void rect(tide_draw_list *list, const float x, const float y, const float width, const float height,
                 const tide_color color)
{
    tide_draw_rect(list, tide_f2(x + width * 0.5f, y + height * 0.5f), tide_f2(width, height), color);
}

void tide_stats_draw(tide_stats *s, const tide_stats_values *values, tide_draw_list *list, const tide_float2 screen,
                     const tide_measure_fn measure)
{
    fill(s, values);
    // On a low screen, the list keeps the lines it has room for, from its top
    const float room = (screen.y - 2.0f * MARGIN - 2.0f * PAD_Y) / PITCH;
    const uint32_t rows = room >= 2.0f ? (uint32_t)room : 1u;
    if (s->line_count > rows) s->line_count = rows;

    // As wide as its widest line, and no wider than the screen
    float most = screen.x - 2.0f * (MARGIN + PAD_X);
    if (most < MARK + TEXT) most = MARK + TEXT;
    float widths[TIDE_STATS_LINES];
    widths[0] = fit(&s->lines[0], most - MARK, measure);
    float widest = widths[0] + MARK;
    for (uint32_t i = 1; i < s->line_count; i++) {
        widths[i] = fit(&s->lines[i], most, measure);
        if (widths[i] > widest) widest = widths[i];
    }
    const float width = widest + 2.0f * PAD_X;
    const float height = (float)s->line_count * PITCH + 2.0f * PAD_Y;
    const float left = screen.x - MARGIN - width;
    const float top = screen.y - MARGIN - height;
    const float right = left + width - PAD_X; // Where every line ends

    const tide_color plain = {200.0f / 255.0f, 200.0f / 255.0f, 200.0f / 255.0f, 1.0f};
    const tide_color bright = {1.0f, 1.0f, 1.0f, 1.0f};
    const tide_color first = s->hovered ? bright : plain; // Lit under the cursor: it's what a click is for
    tide_draw_defaults(list); // What a list had set isn't the drawer's
    tide_draw_set_screen(list);
    rect(list, left, top, width, height, (tide_color){0.0f, 0.0f, 0.0f, s->hovered ? 0.72f : 0.55f});
    const float inset = (PITCH - TEXT) * 0.5f; // A line's text, in the middle of its pitch
    for (uint32_t i = 1; i < s->line_count; i++) {
        const float y = top + PAD_Y + (float)(i - 1u) * PITCH;
        tide_draw_text(list, s->lines[i].text, tide_f2(right - widths[i], y + inset), TEXT, plain);
    }
    // The line that's always there, under the list, and before it an arrow:
    // up while there's a list to open, down while it's open
    const float y = top + PAD_Y + (float)(s->line_count - 1u) * PITCH;
    tide_draw_text(list, s->lines[0].text, tide_f2(right - widths[0], y + inset), TEXT, first);
    const tide_float2 tip = tide_f2(right - widths[0] - MARK + 5.0f, y + PITCH * 0.5f + (s->open ? 2.0f : -2.0f));
    const float back = s->open ? -4.0f : 4.0f;
    tide_draw_thick_line(list, tide_f2(tip.x - 4.0f, tip.y + back), tip, 1.5f, first);
    tide_draw_thick_line(list, tip, tide_f2(tip.x + 4.0f, tip.y + back), 1.5f, first);

    // The pointer is on it out to the screen's corner: nothing else is there
    s->rect = (tide_rect){left, top, screen.x - left, screen.y - top};
    s->shown = true;
}
