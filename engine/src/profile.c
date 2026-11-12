#include "tide/profile.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "tide/page.h"

static double ms_between(const uint64_t from_ns, const uint64_t to_ns)
{
    return to_ns > from_ns ? (double)(to_ns - from_ns) * 1e-6 : 0.0;
}

static void clear_totals(tide_profile_times *times)
{
    for (uint32_t i = 0; i < times->count; i++) times->items[i].total = 0.0;
}

static void free_source(tide_profile_time *time)
{
    free((void *)time->source.name);
    free((void *)time->source.file);
    time->source = (tide_profile_source){0};
}

static void free_times(tide_profile_times *times)
{
    for (uint32_t i = 0; i < times->count; i++) free_source(&times->items[i]);
    free(times->items);
}

static bool same_text(const char *a, const char *b)
{
    return a == b || (a && b && strcmp(a, b) == 0);
}

static const char *copy_text(const char *text)
{
    if (!text) return NULL;
    const size_t size = strlen(text) + 1u;
    char *copy = tide_realloc(NULL, 0, size);
    memcpy(copy, text, size);
    return copy;
}

static void set_source(tide_profile_time *time, const tide_profile_source source)
{
    if (same_text(time->source.name, source.name) && same_text(time->source.file, source.file)
        && time->source.line == source.line && time->source.column == source.column) return;
    // Copy first: a caller may be reusing strings from this row.
    const tide_profile_source copy = {copy_text(source.name), copy_text(source.file), source.line, source.column};
    const uint64_t revision = time->revision + 1u;
    free_source(time);
    *time = (tide_profile_time){.source = copy, .revision = revision};
    size_t n = 0;
    for (; copy.name && copy.name[n] && n + 1u < sizeof time->name; n++) time->name[n] = copy.name[n];
}

void tide_profile_clear(tide_profile *p)
{
    clear_totals(&p->systems);
    clear_totals(&p->views);
    clear_totals(&p->workers);
    p->worker_runs = 0;
    p->ticks = 0;
    p->systems_ms = 0.0;
    p->changes_ms = 0.0;
}

void tide_profile_take_ticks(tide_profile *to, tide_profile *from)
{
    if (from->systems.count) tide_profile_count(&to->systems, from->systems.count);
    for (uint32_t i = 0; i < from->systems.count; i++) {
        tide_profile_time *f = &from->systems.items[i];
        tide_profile_time *t = &to->systems.items[i];
        if (!f->measurements) continue;
        set_source(t, f->source);
        t->ms = f->ms;
        t->total += f->total;
        t->measured_ms += f->measured_ms;
        t->measurements += f->measurements;
        f->ms = f->total = f->measured_ms = 0.0;
        f->measurements = 0;
    }
    to->ticks += from->ticks;
    to->systems_ms += from->systems_ms;
    to->changes_ms += from->changes_ms;
    from->ticks = 0;
    from->systems_ms = from->changes_ms = 0.0;
}

void tide_profile_free(tide_profile *p)
{
    free_times(&p->systems);
    free_times(&p->views);
    free_times(&p->workers);
    *p = (tide_profile){0};
}

void tide_profile_count(tide_profile_times *times, const uint32_t count)
{
    if (count == times->count) return;
    times->revision++;
    for (uint32_t i = 0; i < times->count; i++) free_source(&times->items[i]);
    if (count > times->room) {
        times->items = tide_realloc(times->items, times->room * sizeof *times->items, count * sizeof *times->items);
        times->room = count;
    }
    if (count) memset(times->items, 0, count * sizeof *times->items);
    times->count = count;
}

void tide_profile_sources(tide_profile_times *times, const uint32_t count, const tide_profile_source *sources)
{
    tide_profile_count(times, count);
    for (uint32_t i = 0; i < count; i++) set_source(&times->items[i], sources[i]);
}

void tide_profile_add(tide_profile_times *times, const uint32_t index, const char *name, const uint64_t from_ns,
                      const uint64_t to_ns)
{
    if (index >= times->count) return;
    tide_profile_time *t = &times->items[index];
    if (!same_text(t->source.name, name)) set_source(t, (tide_profile_source){.name = name});
    t->ms = ms_between(from_ns, to_ns);
    t->total += t->ms;
    t->measured_ms += t->ms;
    t->measurements++;
}

void tide_profile_tick(tide_profile *p, const uint64_t began_ns, const uint64_t systems_done_ns)
{
    p->ticks++;
    p->systems_ms += ms_between(began_ns, systems_done_ns);
    p->changes_ms += ms_between(systems_done_ns, tide_time_now_ns());
}

uint64_t tide_profile_view(tide_profile *p, const uint32_t index, const uint32_t count, const char *name,
                           const uint64_t began_ns)
{
    const uint64_t now = tide_time_now_ns();
    tide_profile_count(&p->views, count);
    tide_profile_add(&p->views, index, name, began_ns, now);
    return now;
}
