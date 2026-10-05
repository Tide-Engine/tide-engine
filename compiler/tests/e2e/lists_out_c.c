// The C side of lists_out.tide: functions that make lists, through
// tide/list.h's tide_list_out.

#include <stdbool.h>
#include <stdint.h>

#include "tide/list.h"

// The game's Cell, with the padding it writes out.
typedef struct cell {
    int32_t id;
    float weight;
    bool alive;
    uint8_t unused[3];
} cell;

static uint8_t byte_at(const int i, const int seed)
{
    return (uint8_t)(i * 31 + (i >> 8) * 7 + seed * 13 + 1);
}

int c_out_bytes(tide_list_out *bytes, const int count, const int seed)
{
    uint8_t *room = tide_list_out_room(bytes, (uint32_t)count);
    for (int i = 0; i < count; i++) room[i] = byte_at(i, seed);
    tide_list_out_count(bytes, (uint32_t)count);
    return count;
}

int c_out_cells(tide_list_out *cells, const int count, const int seed)
{
    cell *room = tide_list_out_room(cells, (uint32_t)count);
    for (int i = 0; i < count; i++) room[i] = (cell){.id = i + seed, .weight = (float)i * 0.5f, .alive = i % 3 == 0};
    tide_list_out_count(cells, (uint32_t)count);
    return count;
}

int c_out_poked(tide_list_out *bytes, const int count, const int seed, const int at)
{
    uint8_t *room = tide_list_out_room(bytes, (uint32_t)count);
    for (int i = 0; i < count; i++) room[i] = byte_at(i, seed);
    if (at >= 0 && at < count) room[at] ^= 0xFF;
    tide_list_out_count(bytes, (uint32_t)count);
    return count;
}

void c_out_twice(tide_list_out *bytes, const int first, const int count, const int seed)
{
    uint8_t *room = tide_list_out_room(bytes, (uint32_t)first);
    for (int i = 0; i < first; i++) room[i] = 0xEE;
    tide_list_out_count(bytes, (uint32_t)first); // Of the first room: asking again starts over
    room = tide_list_out_room(bytes, (uint32_t)count);
    for (int i = 0; i < count; i++) room[i] = byte_at(i, seed);
    tide_list_out_count(bytes, (uint32_t)count);
}

void c_out_said(tide_list_out *bytes, const int room_for, const int said, const int seed)
{
    uint8_t *room = tide_list_out_room(bytes, (uint32_t)room_for);
    for (int i = 0; i < room_for; i++) room[i] = byte_at(i, seed);
    tide_list_out_count(bytes, (uint32_t)said);
}

void c_out_silent(tide_list_out *bytes, const int count, const int seed)
{
    uint8_t *room = tide_list_out_room(bytes, (uint32_t)count);
    for (int i = 0; i < count; i++) room[i] = byte_at(i, seed);
}

void c_out_nothing(tide_list_out *bytes)
{
    (void)bytes;
}

int c_out_doubled(const float *from, const int count, tide_list_out *twice)
{
    float *room = tide_list_out_room(twice, (uint32_t)count);
    for (int i = 0; i < count; i++) room[i] = from[i] * 2;
    tide_list_out_count(twice, (uint32_t)count);
    return count;
}

int c_right_bytes(const uint8_t *bytes, const int count, const int seed)
{
    int right = 0;
    for (int i = 0; i < count; i++) right += bytes[i] == byte_at(i, seed);
    return right;
}
