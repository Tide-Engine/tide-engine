#pragma once

#include <stdint.h>

// Benchmark-only clocks around coarse operations in a copy of generated C.
// Workers accumulate into their own slot; the harness reads after joining.
enum tide_phase {
    PHASE_LIFE_BEGIN,
    PHASE_LIFE_WINDOW,
    PHASE_LIFE_COPY,
    PHASE_LIFE_KERNEL,
    PHASE_LIFE_PUT,
    PHASE_LIFE_END,
    PHASE_LIFE_TASK_OTHER,
    PHASE_EVENTS_RECORD,
    PHASE_EVENTS_REPLAY,
    PHASE_EVENTS_TICK_OTHER,
    PHASE_EVENTS_APPLY,
    PHASE_EVENTS_CLEAR,
    PHASE_EVENTS_HEAP,
    PHASE_LIFE_PARALLEL,
    PHASE_LIFE_SYSTEMS,
    PHASE_LIFE_CLEANUP,
    PHASE_COUNT
};

uint64_t tide_phase_stamp(void);
void tide_phase_add(enum tide_phase phase, uint64_t elapsed_ns);
