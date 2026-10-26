#include "tide/profile.h"

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

void tide_profile_free(tide_profile *p)
{
    free(p->systems.items);
    free(p->views.items);
    free(p->workers.items);
    *p = (tide_profile){0};
}

void tide_profile_count(tide_profile_times *times, const uint32_t count)
{
    if (count == times->count) return;
    if (count > times->room) {
        times->items = tide_realloc(times->items, times->room * sizeof *times->items, count * sizeof *times->items);
        times->room = count;
    }
    if (count) memset(times->items, 0, count * sizeof *times->items);
    times->count = count;
}

void tide_profile_add(tide_profile_times *times, const uint32_t index, const char *name, const uint64_t from_ns,
                      const uint64_t to_ns)
{
    if (index >= times->count) return;
    tide_profile_time *t = &times->items[index];
    char copy[TIDE_PROFILE_NAME];
    size_t n = 0;
    for (; name && name[n] && n + 1u < sizeof copy; n++) copy[n] = name[n];
    memset(copy + n, 0, sizeof copy - n);
    if (memcmp(copy, t->name, sizeof copy) != 0) {
        memcpy(t->name, copy, sizeof copy);
        t->total = 0.0;
        t->measured_ms = 0.0;
        t->measurements = 0;
    }
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
