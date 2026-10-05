// The C side of out_params.tide: an `out` parameter is a pointer C writes
// through, like a `mut` one's, and an `out` list its writer (tide/list.h).

#include <stdbool.h>
#include <stdint.h>

#include "tide/list.h"
#include "tide/math.h"

// The game's Pair.
typedef struct pair {
    int32_t a;
    float b;
} pair;

bool c_try_half(const int value, int *half)
{
    if (value % 2 != 0) {
        *half = 0;
        return false;
    }
    *half = value / 2;
    return true;
}

void c_out_pair(const int seed, pair *out)
{
    *out = (pair){.a = seed * 2, .b = (float)seed + 0.5f};
}

void c_out_two(const float seed, float *a, tide_float3 *b)
{
    *a = seed * 2.0f;
    *b = (tide_float3){seed, seed + 1.0f, seed + 2.0f};
}

int c_out_squares(const int count, tide_list_out *squares)
{
    int32_t *room = tide_list_out_room(squares, (uint32_t)count);
    for (int i = 0; i < count; i++) room[i] = i * i;
    tide_list_out_count(squares, (uint32_t)count);
    return count;
}
