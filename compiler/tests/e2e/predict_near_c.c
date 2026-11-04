// The C side of predict_near.tide: a system written in C, handed the runs of
// rows its predicted ticks run (the far ones left out between them).

#include <stddef.h>
#include <stdint.h>

#include "tide/math.h"
#include "tide/rows.h"

// Body, field for field
typedef struct body {
    tide_float2 position;
    int32_t ran;
    int32_t aged;
} body;

void Age(tide_rows *rows)
{
    int32_t *aged = tide_rows_ints_mut(rows, 0, offsetof(body, aged));
    for (uint32_t i = 0; i < tide_rows_count(rows); i++) aged[i] += 1;
}
