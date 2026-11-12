#pragma once

#include <stdbool.h>
#include <stdio.h>

#include "tide/draw_profile.h"
#include "tide/gpu_profile.h"
#include "tide/profiler.h"

// A bounded, host-owned capture. Timing is wall-clock CPU work within the
// host callback, never simulated frame time or an estimate of display FPS.
// Collection is independent of the profiler panel and survives its clears.
#define TIDE_PROFILE_REPORT_ROWS 8192u
#define TIDE_PROFILE_REPORT_SOURCE_BYTES (1024u * 1024u)
#define TIDE_PROFILE_REPORT_SAMPLES 4096u

typedef struct tide_profile_report {
    void *state;
    tide_draw_diagnostics draw;
} tide_profile_report;

typedef struct tide_profile_report_info {
    const char *title, *renderer, *compiler;
    const char *executable_sha256; // NULL when unavailable, not a layout hash.
    const uint8_t *simulation; // 32 bytes, or NULL.
    uint64_t layout_hash;
    uint32_t total_frames, warmup, player, players, threads;
    uint32_t width, height;
    float scale_x, scale_y;
    bool optimized, sanitized;
    uint32_t gpu_pending_passes;
    int exit_code;
} tide_profile_report_info;

// Call only for measured frames. Begin snapshots the current cumulative
// counters before starting the clock; end stops the clock before collecting.
void tide_profile_report_begin(tide_profile_report *r, const tide_profile *p, uint32_t frame);
void tide_profile_report_mark(tide_profile_report *r, tide_frame_part part);
void tide_profile_report_end(tide_profile_report *r, const tide_profile *p, uint64_t draw_calls, uint64_t vertices);
// The renderer's parts of this frame's render phase (tide_platform_render_parts), given before end.
void tide_profile_report_render(tide_profile_report *r, const double ms[TIDE_RENDER_PARTS]);
// Asynchronous GPU results have no source-frame identity. They are a separate
// distribution, never paired with or added to a CPU frame.
void tide_profile_report_gpu(tide_profile_report *r, double ms);
// A tick that ran beside a frame took this long on its own thread (tide_session_alongside_ns): a distribution
// apart, as it overlaps frames and is in none of their parts.
void tide_profile_report_alongside(tide_profile_report *r, double ms);
// Occlusion culling's counts of a frame as they came back from the GPU (tide_platform_occlusion), once each: the
// instances tested, and those it hid. Asynchronous, as GPU timing is.
void tide_profile_report_occlusion(tide_profile_report *r, uint64_t tested, uint64_t hidden);
// Matches tide_gpu_pass_collect; negative/nonfinite samples count as unmeasured.
void tide_profile_report_gpu_pass(void *report, const tide_gpu_pass_sample *sample);
bool tide_profile_report_write(const tide_profile_report *r, FILE *out, const tide_profile_report_info *info);
void tide_profile_report_free(tide_profile_report *r);
