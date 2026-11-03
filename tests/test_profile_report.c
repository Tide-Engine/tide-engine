// Machine reports use real clocks only for frame sample counts. Named costs
// and GPU samples below are fabricated, so no assertion depends on speed.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/json.h"
#include "tide/profile_report.h"
#include "tide_test.h"

#define REPORT_MS UINT64_C(1000000)

static tide_json report_json(const char *text)
{
    tide_json j;
    tide_json_begin(&j, tide_str_from_cstr(text));
    return j;
}

static tide_json field(tide_json j, const char *name)
{
    TIDE_CHECK(tide_json_object(&j));
    int32_t found;
    while ((found = tide_json_field(&j, &name, 1)) != TIDE_JSON_DONE) {
        if (found == 0) return j;
        if (!tide_json_skip(&j)) break;
    }
    TIDE_CHECK(found == 0);
    j.failed = true;
    return j;
}

static tide_json item(tide_json j, const uint32_t index)
{
    TIDE_CHECK(tide_json_array(&j));
    for (uint32_t i = 0; i <= index; i++) {
        if (!tide_json_item(&j, 0)) {
            TIDE_CHECK(false);
            j.failed = true;
            return j;
        }
        if (i == index) return j;
        TIDE_CHECK(tide_json_skip(&j));
    }
    return j;
}

static uint32_t array_count(tide_json j)
{
    uint32_t n = 0;
    TIDE_CHECK(tide_json_array(&j));
    while (tide_json_item(&j, 0)) {
        n++;
        if (!tide_json_skip(&j)) break;
    }
    TIDE_CHECK(!j.failed);
    return n;
}

static double number(tide_json j)
{
    double n = NAN;
    TIDE_CHECK(tide_json_double(&j, &n));
    return n;
}

static bool boolean(tide_json j)
{
    bool value = false;
    TIDE_CHECK(tide_json_bool(&j, &value));
    return value;
}

static bool null_value(tide_json j)
{
    return tide_json_null(&j);
}

static bool text_is(tide_json j, const char *expected)
{
    const uint64_t mark = tide_scratch_mark();
    tide_text text;
    const bool ok = tide_json_text(&j, &text);
    const tide_str view = ok ? tide_text_view(text) : TIDE_STR_EMPTY;
    const bool same = ok && (size_t)view.bytes == strlen(expected) && memcmp(view.ptr, expected, (size_t)view.bytes) == 0;
    tide_scratch_reset(mark);
    return same;
}

static char *written(const tide_profile_report *report, const tide_profile_report_info *info)
{
    // WASI exposes the working directory, not the system temporary directory.
    // Exclusive creation avoids interfering with another concurrently run test.
    char path[96];
    FILE *file = NULL;
    for (uint32_t attempt = 0; attempt < 32u && !file; attempt++) {
        snprintf(path, sizeof path, "profile-report-test-%016llx-%u.tmp", (unsigned long long)tide_time_now_ns(),
                 (unsigned)attempt);
        file = fopen(path, "w+bx");
    }
    TIDE_CHECK(file != NULL);
    if (!file) return NULL;
    bool ok = tide_profile_report_write(report, file, info) && fflush(file) == 0 && fseek(file, 0, SEEK_END) == 0;
    const long bytes = ok ? ftell(file) : -1;
    TIDE_CHECK(bytes >= 0 && bytes < 16 * 1024 * 1024);
    if (bytes < 0 || bytes >= 16 * 1024 * 1024) ok = false;
    char *text = ok ? malloc((size_t)bytes + 1u) : NULL;
    if (text) {
        ok = fseek(file, 0, SEEK_SET) == 0 && fread(text, 1, (size_t)bytes, file) == (size_t)bytes;
        text[bytes] = '\0';
    } else ok = false;
    TIDE_CHECK(fclose(file) == 0);
    TIDE_CHECK(remove(path) == 0);
    TIDE_CHECK(ok);
    if (!ok) { free(text); return NULL; }
    tide_json j = report_json(text);
    TIDE_CHECK(tide_json_skip(&j) && tide_json_end(&j));
    return text;
}

TIDE_TEST(profile_report_empty_has_unknown_timings_and_run_metadata)
{
    tide_profile_report report = {0};
    uint8_t simulation[32] = {0};
    simulation[0] = 0xab;
    simulation[31] = 0xcd;
    const tide_profile_report_info info = {
        .title = "Grass \"canopy\"\nnight", .renderer = "test\\renderer", .compiler = "fixture",
        .simulation = simulation, .layout_hash = UINT64_C(0x123456789abcdef0),
        .total_frames = 12, .warmup = 20, .threads = 4, .width = 640, .height = 360,
        .scale_x = 1.5f, .scale_y = 1.5f, .optimized = true,
    };
    char *text = written(&report, &info);
    TIDE_REQUIRE(text);
    const tide_json j = report_json(text);
    TIDE_CHECK(text_is(field(j, "schema"), "tide.profile") && number(field(j, "version")) == 1);
    const tide_json metadata = field(j, "metadata"), frames = field(j, "frames");
    TIDE_CHECK(text_is(field(metadata, "title"), info.title));
    TIDE_CHECK(text_is(field(metadata, "renderer"), info.renderer));
    TIDE_CHECK(text_is(field(metadata, "layout_hash"), "123456789abcdef0"));
    TIDE_CHECK(text_is(field(metadata, "simulation_id"), "ab000000000000000000000000000000000000000000000000000000000000cd"));
    TIDE_CHECK(null_value(field(metadata, "executable_sha256")));
    TIDE_CHECK(boolean(field(metadata, "optimized")) && !boolean(field(metadata, "sanitized")));
    TIDE_CHECK(number(field(frames, "total")) == 12 && number(field(frames, "warmup_requested")) == 20);
    TIDE_CHECK(number(field(frames, "measured")) == 0 && number(field(frames, "first")) == 0 && number(field(frames, "last")) == 0);
    const tide_json cpu = field(j, "cpu_work"), gpu = field(j, "gpu");
    TIDE_CHECK(number(field(cpu, "samples")) == 0 && null_value(field(cpu, "mean_ms")) && null_value(field(cpu, "p95_ms")));
    TIDE_CHECK(!boolean(field(gpu, "available")) && !boolean(field(gpu, "frame_aligned")));
    TIDE_CHECK(null_value(field(field(gpu, "timing"), "max_ms")));
    TIDE_CHECK(array_count(field(j, "systems")) == 0 && array_count(field(j, "views")) == 0);
    TIDE_CHECK(array_count(field(j, "draw_diagnostics")) == 0);
    TIDE_CHECK(!report.state && !report.draw.rows); // Serializing an empty report leaves it empty.
    free(text);
    tide_profile_report_free(&report);
}

TIDE_TEST(profile_report_excludes_baseline_and_survives_overlay_clears)
{
    tide_profile_report report = {0};
    tide_profile p = {0};
    const tide_profile_source system = {"Movement", "game/movement.tide", 10, 1};
    const tide_profile_source view = {"Grass", "game/grass.tide", 24, 6};
    tide_profile_sources(&p.systems, 1, &system);
    tide_profile_sources(&p.views, 1, &view);
    tide_profile_count(&p.workers, 1);
    tide_profile_add(&p.systems, 0, system.name, 0, 20 * REPORT_MS);
    tide_profile_add(&p.views, 0, view.name, 0, 30 * REPORT_MS);
    tide_profile_add(&p.workers, 0, "Mesher.compute", 0, 50 * REPORT_MS);
    p.ticks = 60;
    p.systems_ms = 600;
    p.changes_ms = 60;

    tide_profile_report_begin(&report, &p, 61);
    tide_profile_add(&p.systems, 0, system.name, 0, 2 * REPORT_MS);
    tide_profile_add(&p.systems, 0, system.name, 0, 4 * REPORT_MS);
    tide_profile_add(&p.views, 0, view.name, 0, 10 * REPORT_MS);
    tide_profile_add(&p.workers, 0, "Mesher.compute", 0, REPORT_MS);
    p.ticks += 2;
    p.systems_ms += 4;
    p.changes_ms += 0.5;
    tide_profile_report_mark(&report, TIDE_FRAME_MATCH);
    tide_profile_report_end(&report, &p, 7, 90);
    tide_profile_clear(&p); // The panel resets ordinary totals, not cumulative observations.

    tide_profile_report_begin(&report, &p, 62);
    tide_profile_add(&p.systems, 0, system.name, 0, REPORT_MS);
    tide_profile_add(&p.views, 0, view.name, 0, 3 * REPORT_MS);
    p.ticks++;
    p.systems_ms += 1;
    p.changes_ms += 0.25;
    tide_profile_report_end(&report, &p, 3, 100);
    tide_profile_free(&p); // Report sources and totals survive the originating profile.

    char *text = written(&report, &(tide_profile_report_info){.total_frames = 62, .warmup = 60});
    TIDE_REQUIRE(text);
    const tide_json j = report_json(text), frames = field(j, "frames");
    TIDE_CHECK(number(field(frames, "measured")) == 2 && number(field(frames, "first")) == 61 && number(field(frames, "last")) == 62);
    const tide_json sys = item(field(j, "systems"), 0), grass = item(field(j, "views"), 0);
    TIDE_CHECK(number(field(sys, "calls")) == 3 && number(field(sys, "total_ms")) == 7);
    TIDE_CHECK(number(field(sys, "frames")) == 2 && number(field(sys, "max_frame_ms")) == 6);
    TIDE_CHECK(number(field(sys, "mean_frame_ms")) == 3.5);
    TIDE_CHECK(number(field(grass, "calls")) == 2 && number(field(grass, "total_ms")) == 13);
    TIDE_CHECK(text_is(field(field(grass, "source"), "file"), "game/grass.tide"));
    const tide_json worker = item(field(j, "workers"), 0);
    TIDE_CHECK(number(field(worker, "calls")) == 1 && number(field(worker, "total_ms")) == 1);
    TIDE_CHECK(number(field(worker, "mean_frame_ms")) == 0.5);
    const tide_json ticks = field(j, "ticks"), render = field(j, "render_work");
    TIDE_CHECK(number(field(ticks, "count")) == 3 && number(field(ticks, "systems_ms")) == 5);
    TIDE_CHECK(number(field(ticks, "changes_events_tasks_ms")) == 0.75);
    TIDE_CHECK(number(field(render, "draw_calls")) == 10 && number(field(render, "vertices")) == 190);
    TIDE_CHECK(number(field(field(j, "cpu_work"), "samples")) == 2);
    TIDE_CHECK(number(field(field(j, "cpu_work"), "total_ms")) >= 0.0);
    free(text);
    tide_profile_report_free(&report);
}

TIDE_TEST(profile_report_owns_distinct_long_names_and_escaped_source_paths)
{
    tide_profile_report report = {0};
    tide_profile p = {0};
    char first[] = "Grass.Very.Long.Namespace.SharedPrefixBeyondThePanelLabel.First";
    char second[] = "Grass.Very.Long.Namespace.SharedPrefixBeyondThePanelLabel.Second";
    char file[] = "C:\\game\\\"grass\"\t\n.tide";
    const tide_profile_source sources[] = {{first, file, 12, 4}, {second, file, 32, 4}};
    tide_profile_sources(&p.views, 2, sources);
    tide_profile_report_begin(&report, &p, 1);
    tide_profile_add(&p.views, 0, first, 0, REPORT_MS);
    tide_profile_add(&p.views, 1, second, 0, 2 * REPORT_MS);
    tide_profile_report_end(&report, &p, 0, 0);
    tide_profile_report_begin(&report, &p, 2);
    tide_profile_add(&p.views, 0, first, 0, 3 * REPORT_MS);
    tide_profile_report_end(&report, &p, 0, 0);
    tide_profile_free(&p);
    memset(first, 'x', sizeof first - 1u);
    memset(second, 'y', sizeof second - 1u);
    memset(file, 'z', sizeof file - 1u);
    char *text = written(&report, &(tide_profile_report_info){0});
    TIDE_REQUIRE(text);
    const tide_json views = field(report_json(text), "views");
    TIDE_CHECK(array_count(views) == 2);
    const tide_json a = item(views, 0), b = item(views, 1), source = field(a, "source");
    TIDE_CHECK(text_is(field(source, "name"), "Grass.Very.Long.Namespace.SharedPrefixBeyondThePanelLabel.First"));
    TIDE_CHECK(text_is(field(field(b, "source"), "name"), "Grass.Very.Long.Namespace.SharedPrefixBeyondThePanelLabel.Second"));
    TIDE_CHECK(text_is(field(source, "file"), "C:\\game\\\"grass\"\t\n.tide"));
    TIDE_CHECK(number(field(source, "line")) == 12 && number(field(source, "column")) == 4);
    TIDE_CHECK(number(field(a, "calls")) == 2 && number(field(a, "total_ms")) == 4);
    TIDE_CHECK(number(field(b, "calls")) == 1 && number(field(b, "total_ms")) == 2);
    free(text);
    tide_profile_report_free(&report);
}

TIDE_TEST(profile_report_detects_counter_generations_even_when_new_totals_are_larger)
{
    tide_profile_report report = {0};
    tide_profile p = {0};
    const tide_profile_source sources[] = {{"Grass", "grass.tide", 1, 1}, {"Sky", "sky.tide", 1, 1}};
    tide_profile_sources(&p.views, 1, sources);
    tide_profile_add(&p.views, 0, "Grass", 0, REPORT_MS);
    tide_profile_report_begin(&report, &p, 2);
    // Growing the declaration set resets the profile's old counters. The same
    // source now has larger values than its baseline; subtraction is invalid.
    tide_profile_sources(&p.views, 2, sources);
    tide_profile_add(&p.views, 0, "Grass", 0, 2 * REPORT_MS);
    tide_profile_add(&p.views, 0, "Grass", 0, 2 * REPORT_MS);
    tide_profile_add(&p.views, 1, "Sky", 0, 5 * REPORT_MS);
    tide_profile_report_end(&report, &p, 0, 0);
    char *text = written(&report, &(tide_profile_report_info){0});
    TIDE_REQUIRE(text);
    const tide_json views = field(report_json(text), "views"), grass = item(views, 0);
    TIDE_CHECK(array_count(views) == 2);
    TIDE_CHECK(number(field(grass, "calls")) == 2 && number(field(grass, "total_ms")) == 4);
    TIDE_CHECK(number(field(item(views, 1), "total_ms")) == 5);
    free(text);
    tide_profile_free(&p);
    tide_profile_report_free(&report);
}

TIDE_TEST(profile_report_keeps_replaced_source_locations_separate)
{
    tide_profile_report report = {0};
    tide_profile p = {0};
    tide_profile_source source = {"Grass", "grass.tide", 1, 1};
    tide_profile_sources(&p.views, 1, &source);
    tide_profile_report_begin(&report, &p, 1);
    tide_profile_add(&p.views, 0, source.name, 0, REPORT_MS);
    tide_profile_report_end(&report, &p, 0, 0);
    tide_profile_report_begin(&report, &p, 2);
    source.line = 20;
    tide_profile_sources(&p.views, 1, &source);
    tide_profile_add(&p.views, 0, source.name, 0, 3 * REPORT_MS);
    tide_profile_report_end(&report, &p, 0, 0);
    char *text = written(&report, &(tide_profile_report_info){0});
    TIDE_REQUIRE(text);
    const tide_json views = field(report_json(text), "views"), a = item(views, 0), b = item(views, 1);
    TIDE_CHECK(array_count(views) == 2);
    TIDE_CHECK(number(field(field(a, "source"), "line")) == 1 && number(field(a, "total_ms")) == 1);
    TIDE_CHECK(number(field(field(b, "source"), "line")) == 20 && number(field(b, "total_ms")) == 3);
    free(text);
    tide_profile_free(&p);
    tide_profile_report_free(&report);
}

TIDE_TEST(profile_report_gpu_samples_are_independent_and_quantiles_use_nearest_rank)
{
    tide_profile_report report = {0};
    const double values[] = {5, 0, 10, 2, 6, 1, 4, 3, 7, 8, 9, 11, 12, 13, 14, 15, 16, 17, 18, 19};
    for (uint32_t i = 0; i < sizeof values / sizeof values[0]; i++) tide_profile_report_gpu(&report, values[i]);
    tide_profile_report_gpu(&report, -1);
    tide_profile_report_gpu(&report, NAN);
    tide_profile_report_gpu(&report, INFINITY);
    char *text = written(&report, &(tide_profile_report_info){0});
    TIDE_REQUIRE(text);
    const tide_json j = report_json(text), gpu = field(j, "gpu"), timing = field(gpu, "timing");
    TIDE_CHECK(boolean(field(gpu, "available")) && !boolean(field(gpu, "frame_aligned")));
    TIDE_CHECK(number(field(timing, "samples")) == 20 && number(field(timing, "quantile_samples")) == 20);
    TIDE_CHECK(number(field(timing, "total_ms")) == 190 && number(field(timing, "mean_ms")) == 9.5);
    TIDE_CHECK(number(field(timing, "min_ms")) == 0 && number(field(timing, "max_ms")) == 19);
    TIDE_CHECK(number(field(timing, "p50_ms")) == 9 && number(field(timing, "p95_ms")) == 18);
    TIDE_CHECK(!boolean(field(timing, "quantiles_estimated")));
    TIDE_CHECK(number(field(field(j, "frames"), "measured")) == 0);
    TIDE_CHECK(null_value(field(field(j, "cpu_work"), "mean_ms")));
    free(text);
    tide_profile_report_free(&report);
}

TIDE_TEST(profile_report_reservoir_is_bounded_and_preserves_full_totals)
{
    tide_profile_report report = {0}, again = {0};
    for (uint32_t i = 1; i <= TIDE_PROFILE_REPORT_SAMPLES; i++) tide_profile_report_gpu(&report, (double)i);
    char *text = written(&report, &(tide_profile_report_info){0});
    TIDE_REQUIRE(text);
    tide_json timing = field(field(report_json(text), "gpu"), "timing");
    TIDE_CHECK(!boolean(field(timing, "quantiles_estimated")));
    TIDE_CHECK(number(field(timing, "p50_ms")) == (double)TIDE_PROFILE_REPORT_SAMPLES / 2.0);
    free(text);

    tide_profile_report_gpu(&report, (double)TIDE_PROFILE_REPORT_SAMPLES + 1.0);
    const uint32_t total = TIDE_PROFILE_REPORT_SAMPLES + 1u;
    for (uint32_t i = 1; i <= total; i++) tide_profile_report_gpu(&again, (double)i);
    text = written(&report, &(tide_profile_report_info){0});
    char *repeated = written(&again, &(tide_profile_report_info){0});
    TIDE_REQUIRE(text && repeated);
    timing = field(field(report_json(text), "gpu"), "timing");
    TIDE_CHECK(boolean(field(timing, "quantiles_estimated")));
    TIDE_CHECK(number(field(timing, "quantile_samples")) == TIDE_PROFILE_REPORT_SAMPLES);
    TIDE_CHECK(number(field(timing, "samples")) == total);
    TIDE_CHECK(number(field(timing, "total_ms")) == (double)total * (double)(total + 1u) / 2.0);
    TIDE_CHECK(number(field(timing, "mean_ms")) == (double)(total + 1u) / 2.0);
    TIDE_CHECK(number(field(timing, "max_ms")) == total && number(field(timing, "min_ms")) == 1);
    TIDE_CHECK(number(field(timing, "p50_ms")) <= number(field(timing, "p95_ms")));
    TIDE_CHECK(strcmp(text, repeated) == 0);
    free(text);
    free(repeated);
    tide_profile_report_free(&report);
    tide_profile_report_free(&again);
}

TIDE_TEST(profile_report_limits_drop_new_rows_without_aliasing_or_hiding_loss)
{
    tide_profile_report report = {0};
    tide_profile p = {0};
    tide_profile_report_begin(&report, &p, 1);
    tide_profile_count(&p.views, TIDE_PROFILE_REPORT_ROWS + 1u);
    for (uint32_t i = 0; i <= TIDE_PROFILE_REPORT_ROWS; i++) {
        char name[32];
        snprintf(name, sizeof name, "View%u", (unsigned)i);
        tide_profile_add(&p.views, i, name, 0, REPORT_MS);
    }
    tide_profile_report_end(&report, &p, 0, 0);
    char *text = written(&report, &(tide_profile_report_info){0});
    TIDE_REQUIRE(text);
    tide_json j = report_json(text);
    TIDE_CHECK(array_count(field(j, "views")) == TIDE_PROFILE_REPORT_ROWS);
    TIDE_CHECK(number(field(field(j, "limits"), "dropped_timing_observations")) == 1);
    free(text);
    tide_profile_free(&p);
    tide_profile_report_free(&report);

    char *oversized = malloc(TIDE_PROFILE_REPORT_SOURCE_BYTES + 1u);
    TIDE_REQUIRE(oversized);
    memset(oversized, 'x', TIDE_PROFILE_REPORT_SOURCE_BYTES);
    oversized[TIDE_PROFILE_REPORT_SOURCE_BYTES] = '\0';
    tide_profile_report_begin(&report, &p, 1);
    tide_profile_count(&p.views, 1);
    tide_profile_add(&p.views, 0, oversized, 0, REPORT_MS);
    free(oversized);
    tide_profile_report_end(&report, &p, 0, 0);
    text = written(&report, &(tide_profile_report_info){0});
    TIDE_REQUIRE(text);
    j = report_json(text);
    TIDE_CHECK(array_count(field(j, "views")) == 0);
    TIDE_CHECK(number(field(field(j, "limits"), "dropped_timing_observations")) == 1);
    free(text);
    tide_profile_free(&p);
    tide_profile_report_free(&report);
}

TIDE_TEST(profile_report_serializes_draw_only_diagnostics_and_drop_counts)
{
    tide_profile_report report = {0};
    const tide_profile_source source = {.name = "Grass"};
    tide_draw_list draw = {.diagnostics = &report.draw, .profile_source = &source};
    tide_draw_diagnostic_scope scope = tide_draw_diagnostics_begin(&draw, 3);
    TIDE_REQUIRE(scope.row);
    scope.row->tested = 3;
    scope.row->rejected = 1;
    scope.row->submitted = 2;
    tide_draw_diagnostics_end(&draw, scope);
    report.draw.rows[0].mesh_recording_ns = 2 * REPORT_MS;
    report.draw.dropped_calls = 7;
    report.draw.dropped_candidates = 100;
    TIDE_CHECK(report.state == NULL);
    char *text = written(&report, &(tide_profile_report_info){0});
    TIDE_REQUIRE(text);
    const tide_json j = report_json(text), rows = field(j, "draw_diagnostics"), row = item(rows, 0);
    TIDE_CHECK(array_count(rows) == 1);
    TIDE_CHECK(text_is(field(field(row, "source"), "name"), "Grass"));
    TIDE_CHECK(null_value(field(field(row, "source"), "file")));
    TIDE_CHECK(number(field(row, "candidates")) == 3 && number(field(row, "tested")) == 3);
    TIDE_CHECK(number(field(row, "rejected")) == 1 && number(field(row, "submitted")) == 2);
    TIDE_CHECK(number(field(row, "mesh_recording_ms")) == 2);
    TIDE_CHECK(number(field(field(j, "limits"), "dropped_draw_calls")) == 7);
    TIDE_CHECK(number(field(field(j, "limits"), "dropped_draw_candidates")) == 100);
    TIDE_CHECK(report.state == NULL && report.draw.count == 1);
    free(text);
    draw.diagnostics = NULL;
    tide_draw_free(&draw);
    tide_profile_report_free(&report);
    TIDE_CHECK(!report.state && !report.draw.rows);
}

TIDE_TEST(profile_report_gpu_passes_own_identity_and_separate_work)
{
    tide_profile_report report = {0};
    char names[] = "Terrain and Shade\nSky";
    tide_gpu_pass_sample p = {.target = 7, .pass = 2, .width = 1024, .height = 512,
        .draw_calls = 3, .vertices = 90, .shaders = names, .ms = 2};
    tide_profile_report_gpu_pass(&report, &p);
    p.ms = 4;
    tide_profile_report_gpu_pass(&report, &p);
    p.target = 8;
    tide_profile_report_gpu_pass(&report, &p);
    p.target = 7; p.pass = 3;
    tide_profile_report_gpu_pass(&report, &p);
    p.ms = -1;
    tide_profile_report_gpu_pass(&report, &p);
    p.ms = NAN;
    tide_profile_report_gpu_pass(&report, &p);
    memset(names, 'x', sizeof names - 1);
    char *text = written(&report, &(tide_profile_report_info){.gpu_pending_passes = 5});
    TIDE_REQUIRE(text);
    const tide_json j = report_json(text), passes = field(j, "gpu_passes"), rows = field(passes, "rows");
    TIDE_CHECK(array_count(rows) == 3);
    TIDE_CHECK(number(field(passes, "pending")) == 5 && number(field(passes, "unmeasured")) == 2);
    const tide_json row = item(rows, 0), time = field(row, "timing");
    TIDE_CHECK(text_is(field(row, "shaders"), "Terrain and Shade\nSky"));
    TIDE_CHECK(text_is(field(row, "target"), "0000000000000007"));
    TIDE_CHECK(number(field(row, "pass")) == 2 && number(field(row, "width")) == 1024);
    TIDE_CHECK(number(field(row, "draw_calls")) == 6 && number(field(row, "vertices")) == 180);
    TIDE_CHECK(number(field(time, "samples")) == 2 && number(field(time, "mean_ms")) == 3);
    TIDE_CHECK(number(field(time, "p95_ms")) == 4 && number(field(time, "max_ms")) == 4);
    TIDE_CHECK(number(field(field(field(j, "gpu"), "timing"), "samples")) == 0);
    free(text);
    tide_profile_report_free(&report);
}

TIDE_TEST(profile_report_gpu_pass_limit_keeps_existing_rows)
{
    tide_profile_report report = {0};
    tide_gpu_pass_sample p = {.shaders = "Shader", .ms = 1};
    for (uint32_t i = 0; i <= TIDE_GPU_PROFILE_ROWS; i++) {
        p.pass = i;
        tide_profile_report_gpu_pass(&report, &p);
    }
    p.pass = 0;
    tide_profile_report_gpu_pass(&report, &p);
    char *text = written(&report, &(tide_profile_report_info){0});
    TIDE_REQUIRE(text);
    const tide_json passes = field(report_json(text), "gpu_passes"), rows = field(passes, "rows");
    TIDE_CHECK(array_count(rows) == TIDE_GPU_PROFILE_ROWS);
    TIDE_CHECK(number(field(passes, "dropped_observations")) == 1);
    TIDE_CHECK(number(field(field(item(rows, 0), "timing"), "samples")) == 2);
    free(text);
    tide_profile_report_free(&report);
}

TIDE_TEST(profile_report_geometry_serializes_exact_work_without_gpu_timing)
{
    tide_profile_report report = {0};
    tide_draw_list d = {.diagnostics = &report.draw};
    tide_profile_source source = {.name = "Boulders", .file = "rocks.tide", .line = 5};
    d.profile_source = &source;
    const tide_vertex3 vertices[3] = {{.position = {0, 0, 0}}, {.position = {1, 0, 0}}, {.position = {0, 1, 0}}};
    const uint32_t indices[3] = {0, 1, 2};
    const tide_mesh mesh = {vertices, 3, indices, 3, 1};
    tide_draw_set_frustum_culling(&d, false);
    tide_draw_mesh_3d(&d, &mesh, tide_identity_f4x4(), NULL, TIDE_FILTER_POINT);
    for (uint32_t i = 0; i < d.count; i++) if (d.commands[i].kind == TIDE_DRAW_MESH_3D)
        tide_draw_geometry_submit(&d, d.commands[i].profile_geometry);
    char *text = written(&report, &(tide_profile_report_info){0});
    TIDE_REQUIRE(text);
    const tide_json j = report_json(text), geometry = field(j, "geometry"), rows = field(geometry, "rows"), row = item(rows, 0);
    TIDE_CHECK(array_count(rows) == 1 && number(field(geometry, "dropped_instances")) == 0);
    TIDE_CHECK(text_is(field(field(row, "source"), "name"), "Boulders"));
    TIDE_CHECK(number(field(row, "mesh_vertices")) == 3 && number(field(row, "mesh_indices")) == 3);
    TIDE_CHECK(number(field(row, "instances")) == 1 && number(field(row, "vertices")) == 3 && number(field(row, "triangles")) == 1);
    TIDE_CHECK(!boolean(field(field(j, "gpu"), "available")));
    free(text);
    tide_draw_free(&d);
    tide_profile_report_free(&report);
}
