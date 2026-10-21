#pragma once

// For tests that read a draw list a command at a time. The list keeps runs of
// plain shapes (rects, circles, their outlines, lines) as records of their own
// under one TIDE_DRAW_PLAIN command; tide_test_flat gives the list with each of
// them a command again (tide_draw_expand), as the shapes were drawn.

#include <stdlib.h>

#include "tide/draw.h"

// The list with each plain shape a command of its own: a copy whose commands
// are kept here, the four latest of them at once (so a test can hold a few).
// Its plain shapes' colors are the bytes they're kept as (tide_test_color_is).
static inline tide_draw_list tide_test_flat(const tide_draw_list *d)
{
    static tide_draw_command *kept[4];
    static uint32_t room[4], next;
    const uint32_t slot = next++ % 4u;
    const uint32_t count = tide_draw_expand(d, NULL, 0);
    if (count > room[slot]) {
        kept[slot] = realloc(kept[slot], (size_t)count * sizeof(tide_draw_command));
        room[slot] = count;
    }
    tide_draw_expand(d, kept[slot], count);
    tide_draw_list flat = *d;
    flat.commands = kept[slot];
    flat.count = count;
    flat.capacity = count;
    flat.plains = NULL;
    flat.plain_count = flat.plain_open = 0;
    flat.plain_capacity = 0;
    return flat;
}

// The same, in the list itself, for a list a test only reads: its commands
// become the ones above, and it has no plain shapes.
static inline void tide_test_flatten(tide_draw_list *d)
{
    const uint32_t count = tide_draw_expand(d, NULL, 0);
    tide_draw_command *commands = malloc((size_t)(count ? count : 1u) * sizeof(tide_draw_command));
    tide_draw_expand(d, commands, count);
    free(d->commands);
    d->commands = commands;
    d->count = count;
    d->capacity = count ? count : 1u;
    d->plain_count = d->plain_open = 0;
}

// A color as a plain shape keeps it, a byte a channel: within half of one of
// a byte's steps of what was asked for.
static inline int tide_test_color_is(const tide_color c, const float r, const float g, const float b, const float a)
{
    const float within = 0.5f / 255.0f + 1e-6f;
    return c.r - r <= within && r - c.r <= within && c.g - g <= within && g - c.g <= within
        && c.b - b <= within && b - c.b <= within && c.a - a <= within && a - c.a <= within;
}
