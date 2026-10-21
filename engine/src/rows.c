#include "tide/rows.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Ends the program over a call C got wrong: which function, in which system,
// and what to write instead.
__attribute__((format(printf, 3, 4))) _Noreturn static void stop(const tide_rows *rows, const char *function, const char *format, ...)
{
    fprintf(stderr, "tide: %s, in the system '%s': ", function, rows->system ? rows->system : "?");
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
    fflush(stderr);
    abort();
}

static const tide_rows_component *component_of(const tide_rows *rows, const char *function, const uint32_t component)
{
    if (component >= rows->component_count) {
        if (rows->component_count == 0) {
            stop(rows, function, "it takes no components, and this asks for component %u: components are the system's parameters "
                                 "that aren't 'with', 'without' or singletons",
                 (unsigned)component);
        }
        stop(rows, function, "it takes %u component%s, and this asks for component %u: they're numbered from 0, in the order of "
                             "the system's parameters",
             (unsigned)rows->component_count, rows->component_count == 1 ? "" : "s", (unsigned)component);
    }
    return &rows->components[component];
}

// The lane a 4-byte field at `offset` is.
static uint32_t lane_of(const tide_rows *rows, const char *function, const tide_rows_component *c, const uint32_t component,
                        const size_t offset)
{
    if (tide_lane_width(c->size) != 4u) {
        stop(rows, function, "component %u is %u bytes, which isn't a multiple of 4, so its fields aren't arrays of 4 bytes a "
                             "row: read and write it a row at a time with tide_rows_get and tide_rows_set, or give it fields "
                             "that make its size a multiple of 4",
             (unsigned)component, (unsigned)c->size);
    }
    if (offset % 4u != 0 || offset >= c->size) {
        stop(rows, function, "offset %u isn't where a 4-byte field of component %u (%u bytes) starts: pass offsetof(its "
                             "struct, the field) of a float, an int or an int enum, and read the others with tide_rows_get",
             (unsigned)offset, (unsigned)component, (unsigned)c->size);
    }
    return (uint32_t)(offset / 4u);
}

static tide_page **page_of(const tide_rows *rows, const tide_rows_component *c, const uint32_t lane)
{
    return &rows->table->pages[rows->chunk * rows->columns->count + c->column + lane];
}

// A lane's rows, to read: wherever they are now.
static const uint8_t *lane_read(const tide_rows *rows, const tide_rows_component *c, const uint32_t lane, const uint32_t width)
{
    return (const uint8_t *)tide_page_data(*page_of(rows, c, lane)) + (size_t)rows->first * width;
}

// A lane's rows, to write. A page the table alone has is written in place. One
// a snapshot shares is left to it, and the table gets a page of its own with
// the rows in it, which the run's end keeps only if a row changed.
static uint8_t *lane_write(tide_rows *rows, const char *function, const tide_rows_component *c, const uint32_t lane,
                           const uint32_t width)
{
    tide_page **p = page_of(rows, c, lane);
    tide_page *page = *p;
    if (page->refs == 1u) {
        if (page->hashed != UINT32_MAX) page->hashed = UINT32_MAX; // What it hashed to is out of date
        return (uint8_t *)tide_page_data(page) + (size_t)rows->first * width;
    }
    // The pieces of a chunk write the same pages at once, so those are the
    // table's own before any of them starts
    if (!rows->whole) stop(rows, function, "a piece of a chunk found a page it writes shared with a snapshot");
    tide_page *own = tide_page_take(page->size);
    *own = (tide_page){1, page->size, page->first, UINT32_MAX, 0, 0};
    memcpy(tide_page_data(own), tide_page_data(page), (size_t)tide_table_rows(rows->table, rows->columns->shift, rows->chunk) * width);
    rows->held[(uint32_t)c->held + lane] = page;
    *p = own;
    return (uint8_t *)tide_page_data(own) + (size_t)rows->first * width;
}

static const tide_rows_component *writable(const tide_rows *rows, const char *function, const uint32_t component)
{
    const tide_rows_component *c = component_of(rows, function, component);
    if (c->held < 0) {
        stop(rows, function, "component %u isn't 'mut' there, so C only reads it: write 'mut' before it in the extern system's "
                             "declaration to change it",
             (unsigned)component);
    }
    return c;
}

const tide_entity *tide_rows_entities(const tide_rows *rows)
{
    if (!rows->table) return NULL;
    const tide_entity *entities = tide_table_column(rows->table, rows->columns, rows->chunk, 0);
    return entities + rows->first;
}

const float *tide_rows_floats(const tide_rows *rows, const uint32_t component, const size_t offset)
{
    const tide_rows_component *c = component_of(rows, "tide_rows_floats", component);
    return (const float *)(const void *)lane_read(rows, c, lane_of(rows, "tide_rows_floats", c, component, offset), 4u);
}

const int32_t *tide_rows_ints(const tide_rows *rows, const uint32_t component, const size_t offset)
{
    const tide_rows_component *c = component_of(rows, "tide_rows_ints", component);
    return (const int32_t *)(const void *)lane_read(rows, c, lane_of(rows, "tide_rows_ints", c, component, offset), 4u);
}

float *tide_rows_floats_mut(tide_rows *rows, const uint32_t component, const size_t offset)
{
    const tide_rows_component *c = writable(rows, "tide_rows_floats_mut", component);
    const uint32_t lane = lane_of(rows, "tide_rows_floats_mut", c, component, offset);
    return (float *)(void *)lane_write(rows, "tide_rows_floats_mut", c, lane, 4u);
}

int32_t *tide_rows_ints_mut(tide_rows *rows, const uint32_t component, const size_t offset)
{
    const tide_rows_component *c = writable(rows, "tide_rows_ints_mut", component);
    const uint32_t lane = lane_of(rows, "tide_rows_ints_mut", c, component, offset);
    return (int32_t *)(void *)lane_write(rows, "tide_rows_ints_mut", c, lane, 4u);
}

static void check_row(const tide_rows *rows, const char *function, const uint32_t row)
{
    if (row >= rows->count) {
        stop(rows, function, "row %u is past the run's %u rows: rows are numbered from 0 to tide_rows_count less one",
             (unsigned)row, (unsigned)rows->count);
    }
}

void tide_rows_get(const tide_rows *rows, const uint32_t component, const uint32_t row, void *value)
{
    const tide_rows_component *c = component_of(rows, "tide_rows_get", component);
    check_row(rows, "tide_rows_get", row);
    const uint32_t width = tide_lane_width(c->size);
    const uint32_t lanes = c->size / width;
    uint8_t *out = value;
    for (uint32_t k = 0; k < lanes; k++) memcpy(out + (size_t)k * width, lane_read(rows, c, k, width) + (size_t)row * width, width);
}

void tide_rows_set(tide_rows *rows, const uint32_t component, const uint32_t row, const void *value)
{
    const tide_rows_component *c = writable(rows, "tide_rows_set", component);
    check_row(rows, "tide_rows_set", row);
    const uint32_t width = tide_lane_width(c->size);
    const uint32_t lanes = c->size / width;
    const uint8_t *in = value;
    for (uint32_t k = 0; k < lanes; k++) {
        // A lane that's that already stays as it is: its page isn't made the table's own for it
        const uint8_t *next = in + (size_t)k * width;
        if (memcmp(lane_read(rows, c, k, width) + (size_t)row * width, next, width) == 0) continue;
        memcpy(lane_write(rows, "tide_rows_set", c, k, width) + (size_t)row * width, next, width);
    }
}

void tide_rows_begin(tide_rows *rows, tide_table *table, const tide_columns *columns, const uint32_t chunk, const uint32_t first,
                     const uint32_t count, const bool whole, const tide_rows_component *components, const uint32_t component_count,
                     tide_page **held, const char *system)
{
    *rows = (tide_rows){table, columns, components, held, system, component_count, chunk, first, table ? count : 0u, whole, NULL, NULL};
    for (uint32_t i = 0; i < component_count; i++) {
        const tide_rows_component *c = &components[i];
        if (c->held < 0) continue;
        memset(&held[c->held], 0, (size_t)(c->size / tide_lane_width(c->size)) * sizeof *held);
    }
}

void tide_rows_after_tick(tide_rows *rows, void (*finish)(void *, const tide_entities *), void *context)
{
    if (!rows->completion) return;
    *rows->completion = (tide_tick_completion){finish, context};
}

void tide_rows_finish_tick(tide_tick_completion *completions, const uint32_t count, const tide_entities *entities)
{
    for (uint32_t i = 0; i < count; i++) {
        if (completions[i].finish) completions[i].finish(completions[i].context, entities);
    }
}

void tide_rows_end(tide_rows *rows)
{
    if (!rows->table) return;
    const uint32_t in_chunk = tide_table_rows(rows->table, rows->columns->shift, rows->chunk);
    for (uint32_t i = rows->component_count; i-- > 0;) {
        const tide_rows_component *c = &rows->components[i];
        if (c->held < 0) continue;
        const uint32_t width = tide_lane_width(c->size);
        for (uint32_t k = c->size / width; k-- > 0;) {
            tide_page *was = rows->held[(uint32_t)c->held + k];
            if (!was) continue; // Not written, or written in place
            tide_page **p = page_of(rows, c, k);
            tide_page *own = *p;
            const size_t bytes = (size_t)in_chunk * width;
            if (tide_same_bytes(tide_page_data(own), tide_page_data(was), bytes)) { // No row changed: still shared, still hashed
                *p = was;
                tide_page_give(own);
                continue;
            }
            memset((uint8_t *)tide_page_data(own) + bytes, 0, own->size - bytes); // Past its rows a page is zeros
            tide_page_release(was, 1); // The snapshots keep it
        }
    }
}
