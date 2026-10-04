#include "tide/profiler.h"

#include <stdio.h>
#include <string.h>

#include "tide/math.h"
#include "tide/time.h"

// See tide/profiler.h.

// How long it measures before the panel's numbers change, in seconds.
#define WINDOW 0.5

// The panel's columns.
enum { FRAME, TICK };

// The slowest views and systems each have a row, and the others share one.
#define MOST_VIEWS 6u
#define MOST_SYSTEMS 10u

static tide_profiler_row *add_row(tide_profiler *p, const int column, const uint32_t depth, const char *label)
{
    uint32_t *count = &p->row_count[column];
    if (*count == TIDE_PROFILER_ROWS) (*count)--; // Never: each column's rows are counted to fit
    tide_profiler_row *r = &p->rows[column][(*count)++];
    *r = (tide_profiler_row){.depth = depth};
    snprintf(r->label, sizeof r->label, "%s", label);
    return r;
}

// A row for `ms` of a column whose whole is `whole`.
static tide_profiler_row *add_time(tide_profiler *p, const int column, const uint32_t depth, const char *label,
                                   const double ms, const double whole)
{
    tide_profiler_row *r = add_row(p, column, depth, label);
    snprintf(r->value, sizeof r->value, depth ? "%.2f" : "%.2f ms", ms);
    const double share = whole > 0.0 ? ms / whole : 0.0;
    r->share = share < 0.0 ? 0.0f : share > 1.0 ? 1.0f : (float)share;
    return r;
}

// The slowest of `times` since the profile was cleared, slowest first, into
// `out`, up to `most` of them: how many there are. Ones that never ran are
// left out.
static uint32_t slowest(const tide_profile_times *times, uint32_t *out, const uint32_t most)
{
    uint32_t n = 0;
    for (; n < most; n++) {
        uint32_t best = UINT32_MAX;
        for (uint32_t i = 0; i < times->count; i++) {
            bool taken = false;
            for (uint32_t k = 0; k < n; k++) taken |= out[k] == i;
            if (taken || times->items[i].total <= 0.0) continue;
            if (best == UINT32_MAX || times->items[i].total > times->items[best].total) best = i;
        }
        if (best == UINT32_MAX) break;
        out[n] = best;
    }
    return n;
}

static double at_least_zero(const double ms)
{
    return ms > 0.0 ? ms : 0.0;
}

// Rows for the slowest of `times`, and one for the others that ran, together.
// `per` is what the totals are divided by (frames or ticks). Returns what they
// all add up to.
static double add_slowest(tide_profiler *p, const int column, const tide_profile_times *times, const uint32_t most,
                          const double per, const double whole)
{
    uint32_t order[MOST_SYSTEMS];
    const uint32_t n = slowest(times, order, most);
    double all = 0.0;
    uint32_t ran = 0; // The ones that took any time
    for (uint32_t i = 0; i < times->count; i++) {
        all += times->items[i].total;
        ran += times->items[i].total > 0.0;
    }
    double listed = 0.0;
    for (uint32_t k = 0; k < n; k++) {
        const tide_profile_time *t = &times->items[order[k]];
        add_time(p, column, 2, t->name, t->total / per, whole);
        listed += t->total;
    }
    if (ran > n) {
        char label[32];
        snprintf(label, sizeof label, "%u more", (unsigned)(ran - n));
        add_time(p, column, 2, label, at_least_zero(all - listed) / per, whole);
    }
    return all / per;
}

// The panel's rows, from what was measured since it last took stock.
static void take_stock(tide_profiler *p, const uint32_t again_ticks)
{
    const tide_profile *f = &p->profile;
    const double frames = (double)p->frames;
    const double frame_ms = p->span * 1000.0 / frames;
    double part[TIDE_FRAME_PARTS];
    double work = 0.0;
    for (int i = 0; i < TIDE_FRAME_PARTS; i++) {
        part[i] = p->part_ms[i] / frames;
        work += part[i];
    }
    p->row_count[FRAME] = 0;
    p->row_count[TICK] = 0;

    // A frame
    tide_profiler_row *r = add_time(p, FRAME, 0, "Frame", frame_ms, frame_ms);
    snprintf(r->note, sizeof r->note, "%.0f fps, longest %.1f ms", frame_ms > 0.0 ? 1000.0 / frame_ms : 0.0,
             p->longest * 1000.0);
    add_time(p, FRAME, 1, "Input", part[TIDE_FRAME_INPUT], frame_ms);
    add_time(p, FRAME, 1, "Match", part[TIDE_FRAME_MATCH], frame_ms);
    const double ticks_ms = (f->systems_ms + f->changes_ms) / frames;
    if (f->ticks) {
        // Ticks run again count from one stock to the next: a count that went
        // down is another client's, which starts from none
        const uint32_t again = p->again_known && again_ticks >= p->again_from ? again_ticks - p->again_from : 0u;
        r = add_time(p, FRAME, 2, "Ticks", ticks_ms, frame_ms);
        if (again) {
            snprintf(r->note, sizeof r->note, "%.1f a frame, %.1f again", (double)f->ticks / frames, (double)again / frames);
        } else {
            snprintf(r->note, sizeof r->note, "%.1f a frame", (double)f->ticks / frames);
        }
        add_time(p, FRAME, 2, "Netcode", at_least_zero(part[TIDE_FRAME_MATCH] - ticks_ms), frame_ms);
    }
    add_time(p, FRAME, 1, "Views", part[TIDE_FRAME_VIEWS], frame_ms);
    const double views_ms = add_slowest(p, FRAME, &f->views, MOST_VIEWS, frames, frame_ms);
    add_time(p, FRAME, 2, "GUI, tasks", at_least_zero(part[TIDE_FRAME_VIEWS] - views_ms), frame_ms);
    add_time(p, FRAME, 1, "Render", part[TIDE_FRAME_RENDER], frame_ms);
    add_time(p, FRAME, 1, "Other", part[TIDE_FRAME_OTHER], frame_ms);
    add_time(p, FRAME, 1, "Swap, idle", at_least_zero(frame_ms - work), frame_ms);

    // A tick
    if (f->ticks) {
        const double ticks = (double)f->ticks;
        const double tick_ms = (f->systems_ms + f->changes_ms) / ticks;
        r = add_time(p, TICK, 0, "Tick", tick_ms, tick_ms);
        snprintf(r->note, sizeof r->note, "%.0f a second", p->span > 0.0 ? ticks / p->span : 0.0);
        add_time(p, TICK, 1, "Systems", f->systems_ms / ticks, tick_ms);
        add_slowest(p, TICK, &f->systems, MOST_SYSTEMS, ticks, tick_ms);
        add_time(p, TICK, 1, "Changes, events", f->changes_ms / ticks, tick_ms);
    } else {
        r = add_row(p, TICK, 0, "Tick");
        snprintf(r->note, sizeof r->note, "none ran");
    }

    p->span = 0.0;
    p->frames = 0;
    p->longest = 0.0;
    memset(p->part_ms, 0, sizeof p->part_ms);
    p->again_from = again_ticks;
    p->again_known = true;
    tide_profile_clear(&p->profile);
}

void tide_profiler_reset(tide_profiler *p)
{
    if (!p) return;
    p->mark_ns = 0;
    p->span = 0.0;
    p->frames = 0;
    p->longest = 0.0;
    memset(p->part_ms, 0, sizeof p->part_ms);
    p->again_known = false;
    tide_profile_clear(&p->profile);
    p->row_count[FRAME] = 0;
    p->row_count[TICK] = 0;
    tide_profiler_row *r = add_row(p, FRAME, 0, "Profiler");
    snprintf(r->note, sizeof r->note, "measuring");
}

void tide_profiler_begin(tide_profiler *p, const float seconds)
{
    if (!p) return;
    const double span = seconds == seconds && seconds > 0.0f ? (double)seconds : 0.0;
    p->span += span;
    p->frames++;
    if (span > p->longest) p->longest = span;
    p->mark_ns = tide_time_now_ns();
}

void tide_profiler_mark(tide_profiler *p, const tide_frame_part part)
{
    if (!p || !p->mark_ns) return; // No frame began
    const uint64_t now = tide_time_now_ns();
    if (now > p->mark_ns) p->part_ms[part] += (double)(now - p->mark_ns) * 1e-6;
    p->mark_ns = now;
}

void tide_profiler_end(tide_profiler *p, const uint32_t again_ticks)
{
    if (!p || !p->mark_ns) return;
    tide_profiler_mark(p, TIDE_FRAME_OTHER);
    p->mark_ns = 0;
    if (!p->again_known) { // The first frame since it started over: counted from here
        p->again_from = again_ticks;
        p->again_known = true;
    }
    if (p->span >= WINDOW && p->frames) take_stock(p, again_ticks);
}

void tide_profiler_free(tide_profiler *p)
{
    if (!p) return;
    tide_profile_free(&p->profile);
    *p = (tide_profiler){0};
}

// ---------------------------------------------------------------------------
// The panel

static const float TEXT = 13.0f;   // Its font size
static const float PITCH = 18.0f;  // From one row to the next
static const float BAR = 16.0f;    // A row's height
static const float INDENT = 12.0f; // For each step of depth
static const float PAD = 8.0f;     // Around the columns, and inside a row
static const float MARGIN = 12.0f; // From the screen's corner
static const float COLUMN = 340.0f;

static float width_of(const tide_measure_fn measure, const char *text)
{
    return measure ? measure(text, TEXT) : (float)strlen(text) * TEXT * 0.5f;
}

// `text` in `width` pixels: cut where a character starts, if it's longer.
static void fit(char *text, const float width, const tide_measure_fn measure)
{
    size_t n = strlen(text);
    while (n > 1 && width_of(measure, text) > width) {
        n--;
        while (n > 1 && ((unsigned char)text[n] & 0xC0u) == 0x80u) n--; // Not inside a UTF-8 character
        text[n] = '\0';
    }
}

// A rect by its top left corner, as the screen's pixels go.
static void rect(tide_draw_list *list, const float x, const float y, const float width, const float height,
                 const tide_color color)
{
    tide_draw_rect(list, tide_f2(x + width * 0.5f, y + height * 0.5f), tide_f2(width, height), color);
}

static void draw_column(const tide_profiler *p, const int column, tide_draw_list *list, const float x, const float y,
                        const float width, const tide_measure_fn measure)
{
    const tide_color bright = {0.94f, 0.95f, 0.97f, 1.0f};
    const tide_color plain = {0.80f, 0.82f, 0.86f, 1.0f};
    const tide_color dim = {0.55f, 0.58f, 0.64f, 1.0f};
    const tide_color bars[2] = {{0.25f, 0.50f, 0.95f, 0.40f}, {0.20f, 0.70f, 0.50f, 0.40f}};
    for (uint32_t i = 0; i < p->row_count[column]; i++) {
        const tide_profiler_row *r = &p->rows[column][i];
        const float top = y + (float)i * PITCH;
        if (r->share > 0.0f) rect(list, x, top, tide_floor_f(width * r->share), BAR, bars[column]);
        const float value_width = tide_ceil_f(width_of(measure, r->value));
        const float right = x + width - PAD;
        const float left = x + PAD + (float)r->depth * INDENT;
        if (r->value[0]) {
            tide_draw_text(list, r->value, tide_f2(right - value_width, top + 1.0f), TEXT, r->depth ? plain : bright);
        }
        // The label, and its note after it, in what the value leaves
        float room = right - value_width - PAD - left;
        char label[sizeof r->label];
        memcpy(label, r->label, sizeof label);
        fit(label, room, measure);
        tide_draw_text(list, label, tide_f2(left, top + 1.0f), TEXT, r->depth ? plain : bright);
        if (!r->note[0]) continue;
        const float label_width = tide_ceil_f(width_of(measure, label)) + PAD;
        room -= label_width;
        char note[sizeof r->note];
        memcpy(note, r->note, sizeof note);
        if (room < TEXT * 2.0f) continue;
        fit(note, room, measure);
        tide_draw_text(list, note, tide_f2(left + label_width, top + 1.0f), TEXT, dim);
    }
}

void tide_profiler_draw(const tide_profiler *p, tide_draw_list *list, const tide_float2 screen,
                        const tide_measure_fn measure)
{
    if (!p || !p->row_count[FRAME]) return;
    // Side by side where both columns fit, or else one under the other, as
    // wide as the screen lets them be
    const uint32_t frame_rows = p->row_count[FRAME], tick_rows = p->row_count[TICK];
    const bool beside = tick_rows && screen.x >= 2.0f * (MARGIN + PAD + COLUMN) + PAD;
    float width = COLUMN;
    if (!beside && screen.x < COLUMN + 2.0f * (MARGIN + PAD)) width = screen.x - 2.0f * (MARGIN + PAD);
    if (width < 120.0f) width = 120.0f;
    const uint32_t rows = beside ? (frame_rows > tick_rows ? frame_rows : tick_rows) : frame_rows + tick_rows;
    const float gap = tick_rows ? PAD : 0.0f; // Between the columns
    const float panel_width = beside ? 2.0f * width + gap + 2.0f * PAD : width + 2.0f * PAD;
    const float panel_height = (float)rows * PITCH - (PITCH - BAR) + 2.0f * PAD + (beside ? 0.0f : gap);

    if (list->clipped) tide_draw_no_clip(list); // A view's clip isn't the panel's
    tide_draw_screen(list);
    rect(list, MARGIN, MARGIN, panel_width, panel_height, (tide_color){0.05f, 0.06f, 0.08f, 0.88f});
    const float x = MARGIN + PAD, y = MARGIN + PAD;
    draw_column(p, FRAME, list, x, y, width, measure);
    if (beside) draw_column(p, TICK, list, x + width + gap, y, width, measure);
    else draw_column(p, TICK, list, x, y + (float)frame_rows * PITCH + gap, width, measure);
}
