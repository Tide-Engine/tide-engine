#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tide/entity.h"
#include "tide/table.h"

// What a system written in C is handed (`extern system Move(mut Position
// position, Velocity velocity, Time time);`): a run of rows, the entities it
// runs for that are side by side in the match's memory, and their components
// as arrays.
//
//     void Move(tide_rows *rows, const Time *time)
//     {
//         const uint32_t count = tide_rows_count(rows);
//         float *x = tide_rows_floats_mut(rows, 0, offsetof(Position, x));
//         const float *dx = tide_rows_floats(rows, 1, offsetof(Velocity, x));
//         for (uint32_t i = 0; i < count; i++) x[i] += dx[i] * time->dt;
//     }
//
// The function is called once for each run: a chunk of an archetype the
// system's filters admit, or a piece of one when the tick shares a chunk out
// over threads, in the order a system written in Tide would go through them
// on one thread. A system that runs once a tick (no component, no filter) is
// called once, with no rows. The singletons the system takes follow `rows`,
// each a pointer, in the declaration's order.
//
// A component is kept in lanes: a column of 4 bytes of it each, so a field of
// 4 bytes (a float, an int, an int enum) is an array of its own, a value a
// row. Components are numbered from 0, in the order of the system's
// parameters (filters don't count), and a field is named by where it starts
// in the C struct that has the component's fields in order
// (offsetof(Position, x)): Tide's types have no padding but what they write
// out, so the two layouts match.
//
// Nothing is copied to read. An array to write is asked for by itself, one
// field at a time, and only then does that lane's memory become the match's
// to change: the match shares what it doesn't change with its snapshots (see
// tide/page.h), and a client keeps one of every tick. A lane that comes out
// as it was is shared again once the function returns, so asking for an array
// and writing nothing new into it leaves the match as it was.
//
// An array to write has what's there in it, and is the field from then on: C
// reads it through that pointer too. An array of the same field that was asked
// for to read before it may no longer be the one the run writes. Every array
// is the run's: good until the function returns, and no longer.
//
// What C does with the rows has to come out the same on every machine and on
// any number of threads, as everything C does (see tide/jobs.h): a run only
// reaches its own rows, and a system that keeps state between runs takes a
// `mut` singleton, which keeps it on one thread. C can't spawn, destroy, add
// or remove components or send events from here.
//
// A call that can't be answered stops the program, saying what to write
// instead: a component the system doesn't take, an offset that isn't a 4-byte
// field's, an array to write of a component that isn't `mut`.

// One of the system's component parameters in an archetype: generated code's.
typedef struct tide_rows_component {
    uint32_t column; // Its first lane's column in the archetype's table
    uint32_t size;   // Its bytes: its lanes are tide_lane_width(size) bytes each
    int32_t held;    // `mut`: where its lanes start in tide_rows.held; -1 for one the system only reads
} tide_rows_component;

// A run of rows. C reads it through the functions below.
typedef struct tide_rows {
    tide_table *table;
    const tide_columns *columns;
    const tide_rows_component *components;
    // For each lane the system can change: the page a snapshot shares, once
    // the run took one of the table's own to write (tide_rows_end puts it
    // back if nothing changed); else NULL
    tide_page **held;
    const char *system; // Its name in Tide, for messages
    uint32_t component_count;
    uint32_t chunk;
    uint32_t first; // Its first row in the chunk
    uint32_t count;
    bool whole;     // It has the chunk to itself: no other thread has a piece of it
} tide_rows;

// How many rows the run has: every array has that many values. 0 for a system
// that runs once a tick.
static inline uint32_t tide_rows_count(const tide_rows *rows)
{
    return rows->count;
}

// Each row's entity: NULL for a run of no rows.
const tide_entity *tide_rows_entities(const tide_rows *rows);

// The array of a 4-byte field of component `component`, a value a row:
// `offset` is where the field starts in the component (offsetof). To read.
const float *tide_rows_floats(const tide_rows *rows, uint32_t component, size_t offset);
const int32_t *tide_rows_ints(const tide_rows *rows, uint32_t component, size_t offset);

// The same to write, for a component the system takes as `mut`, with what's
// there in it.
float *tide_rows_floats_mut(tide_rows *rows, uint32_t component, size_t offset);
int32_t *tide_rows_ints_mut(tide_rows *rows, uint32_t component, size_t offset);

// One row's whole component, into `value` and from it: for fields that aren't
// 4 bytes (a long or an entity is two lanes, a bool a byte of one), for a
// component whose size isn't a multiple of 4, and for C that works a struct at
// a time. tide_rows_set writes the lanes that differ from what's there, so
// putting back what was read costs nothing.
void tide_rows_get(const tide_rows *rows, uint32_t component, uint32_t row, void *value);
void tide_rows_set(tide_rows *rows, uint32_t component, uint32_t row, const void *value);

// For generated code, around the call into C: rows `first` to `first + count`
// of `chunk`, with room in `held` for a page a lane of each `mut` component.
// `whole` says the run has the chunk to itself; the pieces of a chunk that's
// shared out find the pages they write the table's own already (a system's
// `own`, tide/jobs.h). A NULL table is a run of no rows.
void tide_rows_begin(tide_rows *rows, tide_table *table, const tide_columns *columns, uint32_t chunk, uint32_t first, uint32_t count,
                     bool whole, const tide_rows_component *components, uint32_t component_count, tide_page **held,
                     const char *system);
void tide_rows_end(tide_rows *rows);
