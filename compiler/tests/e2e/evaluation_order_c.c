// The C side of evaluation_order.tide: functions that change what they're
// passed, as a library would write them, with its own type for the game's.
#include <stdint.h>

typedef struct tally {
    int32_t n;
    int32_t trace;
} tally;

typedef struct corner {
    float x, y;
} corner;

int32_t c_next(tally *t)
{
    t->n += 1;
    return t->n;
}

corner c_corner(tally *t)
{
    t->n += 1;
    return (corner){(float)t->n, (float)(t->n * 2)};
}
