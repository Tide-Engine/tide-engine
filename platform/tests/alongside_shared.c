// C with state of its own that the match's code and a view both use (alongside.c): a system is inside it for a
// while each tick, and a view asks whether one is, having let the tick that runs beside its frame run first
// (tide_ticks_settle), which is what makes the answer no.
#include <stdint.h>

#include "tide/jobs.h"

static volatile int32_t inside;

int32_t SharedByTick(const int32_t from)
{
    inside = 1;
    volatile uint32_t spun = 0;
    for (uint32_t i = 0; i < 200000u; i++) spun += i; // Long enough for a view to come by
    inside = 0;
    return from + 1 + (int32_t)(spun & 0u);
}

int32_t SharedByView(void)
{
    tide_ticks_settle();
    return inside;
}
