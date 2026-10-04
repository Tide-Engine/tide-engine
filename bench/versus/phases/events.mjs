// Benchmark-only transformation of generated versus_events C. Production
// code generation and event semantics stay unchanged.
//
// RECORD includes the scheduler, system execution, queue recording, and task
// settlement. REPLAY includes applying events and structural commands, queue
// clearing, and heap flush/settlement. The harness reports TICK_OTHER as its
// full tick elapsed time minus these two intervals; that remainder includes
// the instrumentation's own overhead. There are no per-event clock calls.
// APPLY/CLEAR/HEAP are subintervals of REPLAY, not additional tick phases.

function replaceOnce(source, pattern, replacement, description) {
    const matches = [...source.matchAll(pattern)];
    if (matches.length !== 1) {
        throw new Error(`Events instrumentation expected one ${description}; found ${matches.length}`);
    }
    return source.replace(pattern, replacement);
}

export function instrumentEvents(source) {
    if (typeof source !== 'string') throw new TypeError('Expected generated Events C source');
    if (source.includes('tide_phase_events_')) throw new Error('Events source is already instrumented');
    const newline = source.includes('\r\n') ? '\r\n' : '\n';

    source = replaceOnce(source, /^#include "events\.h"(?=\r?$)/gm,
        `#include "events.h"${newline}#include "phases.h"`, 'events.h include');

    source = replaceOnce(source,
        /^static void tide_apply_commands\(tide_world \*w\)\r?\n\{\r?\n([\s\S]*?)^\}/gm,
        (apply) => {
            for (const [name, pattern, phase] of [
                ['apply', /^    tide_apply_from\(w, \(tide_queue_place\)\{0, 0\}\);(?=\r?$)/gm, 'APPLY'],
                ['clear', /^    tide_queue_clear\(&w->commands\);(?=\r?$)/gm, 'CLEAR'],
                ['heap', /^    tide_heap_flush\(&w->heap\);\r?\n    tide_heap_settle\(&w->heap\);[^\r\n]*(?=\r?$)/gm, 'HEAP'],
            ]) {
                apply = replaceOnce(apply, pattern, statement => [
                    `    const uint64_t tide_phase_events_${name}_began = tide_phase_stamp();`,
                    statement,
                    `    tide_phase_add(PHASE_EVENTS_${phase}, tide_phase_stamp() - tide_phase_events_${name}_began);`,
                ].join(newline), `${name} cleanup in tide_apply_commands`);
            }
            return apply;
        }, 'tide_apply_commands definition');

    return replaceOnce(source,
        /^void tide_world_tick_on\(tide_world \*w, const tide_jobs \*jobs\)\r?\n\{\r?\n([\s\S]*?)^\}/gm,
        (tick) => {
            tick = replaceOnce(tick, /^    tide_run_systems\(w, &tide_tick, jobs\);(?=\r?$)/gm, [
                '    const uint64_t tide_phase_events_record_began = tide_phase_stamp();',
                '    tide_run_systems(w, &tide_tick, jobs);',
                '    const uint64_t tide_phase_events_record_ended = tide_phase_stamp();',
                '    tide_phase_add(PHASE_EVENTS_RECORD, tide_phase_events_record_ended - tide_phase_events_record_began);',
            ].join(newline), 'system-recording call in tide_world_tick_on');
            return replaceOnce(tick, /^    tide_apply_commands\(w\);(?=\r?$)/gm, [
                '    const uint64_t tide_phase_events_replay_began = tide_phase_stamp();',
                '    tide_apply_commands(w);',
                '    const uint64_t tide_phase_events_replay_ended = tide_phase_stamp();',
                '    tide_phase_add(PHASE_EVENTS_REPLAY, tide_phase_events_replay_ended - tide_phase_events_replay_began);',
            ].join(newline), 'event-replay call in tide_world_tick_on');
        }, 'tide_world_tick_on definition');
}
