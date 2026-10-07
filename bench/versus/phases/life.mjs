// Benchmark-only instrumentation of tidec's generated Life C. Keep clocks
// outside the cell loops so the kernel's vectorization and work stay intact.

function replaceOnce(source, pattern, replacement, label) {
    const matches = [...source.matchAll(pattern)];
    if (matches.length !== 1) {
        throw new Error(`Life phases: expected one ${label}, found ${matches.length}`);
    }
    return source.replace(pattern, replacement);
}

// Find a generated function's body without mistaking a quoted brace or a
// comment for C syntax. Prototypes have no opening brace on the next line.
function functionBody(source, name) {
    const escaped = name.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
    const pattern = new RegExp(`^(?:TIDE_HELPER |static )?void ${escaped}\\([^\\n]*\\)\\r?\\n\\{`, "gm");
    const matches = [...source.matchAll(pattern)];
    if (matches.length !== 1) {
        throw new Error(`Life phases: expected one definition of ${name}, found ${matches.length}`);
    }
    const open = matches[0].index + matches[0][0].length - 1;
    let depth = 1, quote = null, comment = null;
    for (let i = open + 1; i < source.length; i++) {
        const c = source[i], next = source[i + 1];
        if (comment === "line") {
            if (c === "\n") comment = null;
        } else if (comment === "block") {
            if (c === "*" && next === "/") { comment = null; i++; }
        } else if (quote) {
            if (c === "\\") i++;
            else if (c === quote) quote = null;
        } else if (c === "/" && next === "/") {
            comment = "line"; i++;
        } else if (c === "/" && next === "*") {
            comment = "block"; i++;
        } else if (c === '"' || c === "'") {
            quote = c;
        } else if (c === "{") {
            depth++;
        } else if (c === "}" && --depth === 0) {
            return {begin: open + 1, end: i};
        }
    }
    throw new Error(`Life phases: unterminated definition of ${name}`);
}

function patchFunction(source, name, transform) {
    const {begin, end} = functionBody(source, name);
    return source.slice(0, begin) + transform(source.slice(begin, end)) + source.slice(end);
}

export function instrumentLife(source) {
    if (typeof source !== "string") throw new TypeError("Life phases: generated C must be a string");
    if (source.includes('"phases.h"') || source.includes("PHASE_LIFE_")) {
        throw new Error("Life phases: source is already instrumented");
    }
    const newline = source.includes("\r\n") ? "\r\n" : "\n";
    const lines = (...parts) => parts.join(newline);
    const range = functionBody(source, "tide_system_Step");
    const system = source.slice(range.begin, range.end);
    const callbacks = [...system.matchAll(/tide_parallel_for\(tide_pl\.tasks, (tide_par\d+),/g)];
    if (callbacks.length !== 1) {
        throw new Error(`Life phases: expected one Step parallel callback, found ${callbacks.length}`);
    }
    const taskName = callbacks[0][1];

    source = replaceOnce(source, /^#include "life\.h"\r?$/gm,
        `#include "life.h"${newline}#include "phases.h"`, "Life header include");
    source = patchFunction(source, "tide_system_Step", body => {
        for (const [operation, phase] of [["begin", "BEGIN"], ["end", "END"]]) {
            const pattern = new RegExp(`^([ \\t]*)(tide_par_${operation}\\([^\\n]*\\);)\\r?$`, "gm");
            body = replaceOnce(body, pattern, (_, indent, statement) => lines(
                `${indent}const uint64_t tide_phase_${operation}_start = tide_phase_stamp();`,
                `${indent}${statement}`,
                `${indent}tide_phase_add(PHASE_LIFE_${phase}, tide_phase_stamp() - tide_phase_${operation}_start);`
            ), `Step ${operation}`);
        }
        body = replaceOnce(body, /^([ \t]*)(tide_parallel_for\([^\n]*\);)\r?$/gm,
            (_, indent, statement) => lines(
                `${indent}const uint64_t tide_phase_parallel_start = tide_phase_stamp();`,
                `${indent}${statement}`,
                `${indent}tide_phase_add(PHASE_LIFE_PARALLEL, tide_phase_stamp() - tide_phase_parallel_start);`
            ), 'Step parallel region');
        return body;
    });

    source = patchFunction(source, "tide_world_tick_on", body => {
        for (const [label, pattern, phase] of [
            ['systems', /^    tide_run_systems\(w, &tide_tick, jobs\);\r?$/gm, 'SYSTEMS'],
            ['cleanup', /^    tide_apply_commands\(w\);\r?$/gm, 'CLEANUP'],
        ]) {
            body = replaceOnce(body, pattern, statement => lines(
                `    const uint64_t tide_phase_${label}_start = tide_phase_stamp();`,
                statement,
                `    tide_phase_add(PHASE_LIFE_${phase}, tide_phase_stamp() - tide_phase_${label}_start);`
            ), `tick ${label}`);
        }
        return body;
    });

    source = patchFunction(source, taskName, body => {
        body = lines("", "    const uint64_t tide_phase_task_start = tide_phase_stamp();",
            "    uint64_t tide_phase_task_measured = 0;") + body;

        for (const [name, pattern, phase] of [
            ["window", /^([ \t]*)(tide_par_window\([^\n]*\);)\r?$/gm, "WINDOW"],
            ["copy", /^([ \t]*)(memcpy\(tide_wm \+ tide_wn, tide_wm, [^\n]*\);)\r?$/gm, "COPY"],
            ["put", /^([ \t]*)(tide_par_window_put\([^\n]*\);)\r?$/gm, "PUT"]
        ]) {
            body = replaceOnce(body, pattern, (_, indent, statement) => lines(
                `${indent}const uint64_t tide_phase_${name}_start = tide_phase_stamp();`,
                `${indent}${statement}`,
                `${indent}const uint64_t tide_phase_${name}_ns = tide_phase_stamp() - tide_phase_${name}_start;`,
                `${indent}tide_phase_add(PHASE_LIFE_${phase}, tide_phase_${name}_ns);`,
                `${indent}tide_phase_task_measured += tide_phase_${name}_ns;`
            ), `${taskName} ${name}`);
        }

        body = replaceOnce(body, /^    if \(!tide_sparse\) \{\r?$/gm,
            lines("    const uint64_t tide_phase_kernel_start = tide_phase_stamp();", "    if (!tide_sparse) {"),
            `${taskName} kernel start`);
        body = replaceOnce(body, /^    const uint64_t tide_phase_put_start = tide_phase_stamp\(\);\r?$/gm,
            lines(
                "    const uint64_t tide_phase_kernel_ns = tide_phase_stamp() - tide_phase_kernel_start;",
                "    tide_phase_add(PHASE_LIFE_KERNEL, tide_phase_kernel_ns);",
                "    tide_phase_task_measured += tide_phase_kernel_ns;",
                "    const uint64_t tide_phase_put_start = tide_phase_stamp();"
            ), `${taskName} kernel end`);

        // Empty tiles return before any of the four measured task spans.
        body = replaceOnce(body, /^([ \t]*)return;\r?$/gm, (_, indent) => lines(
            `${indent}tide_phase_add(PHASE_LIFE_TASK_OTHER, tide_phase_stamp() - tide_phase_task_start);`,
            `${indent}return;`
        ), `${taskName} empty tile return`);
        body = replaceOnce(body, /^    tide_scratch_reset\(tide_mark\);\r?$/gm, lines(
            "    tide_scratch_reset(tide_mark);",
            "    tide_phase_add(PHASE_LIFE_TASK_OTHER, tide_phase_stamp() - tide_phase_task_start - tide_phase_task_measured);"
        ), `${taskName} final scratch reset`);
        return body;
    });
    return source;
}
