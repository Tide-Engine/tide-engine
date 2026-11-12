#include "tide/profile_report.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "tide/page.h"

enum { CPU = TIDE_FRAME_PARTS, GPU, RENDER, RENDER_OTHER = RENDER + TIDE_RENDER_PARTS, ALONGSIDE, METRICS, GROUPS = 3, INDEX_SIZE = TIDE_PROFILE_REPORT_ROWS * 2u };

typedef struct distribution {
    uint64_t count;
    double total, min, max;
    double samples[TIDE_PROFILE_REPORT_SAMPLES];
} distribution;

typedef struct report_row {
    tide_profile_source source;
    uint32_t group;
    uint64_t hash, serial, before_count, calls, frames;
    uint64_t table_revision, row_revision;
    double before_ms, total_ms, max_frame_ms;
} report_row;

typedef struct gpu_row {
    tide_gpu_pass_sample identity;
    distribution timing;
    uint64_t draw_calls, vertices;
} gpu_row;

typedef struct report_state {
    gpu_row *gpu_rows[TIDE_GPU_PROFILE_ROWS];
    uint32_t gpu_count, gpu_name_bytes;
    uint64_t gpu_unmeasured, gpu_dropped;
    distribution metrics[METRICS];
    report_row *rows;
    uint32_t *index;
    uint32_t count, source_bytes, first, last;
    uint64_t frames, dropped, began, mark, serial, draw_calls, vertices;
    uint64_t occlusion_tested, occlusion_hidden, occlusion_frames;
    uint64_t ticks;
    uint32_t before_ticks;
    double before_systems, before_changes, systems_ms, changes_ms;
    double frame_parts[TIDE_FRAME_PARTS];
    double render_parts[TIDE_RENDER_PARTS];
    bool rendered; // tide_profile_report_render was given this frame's
} report_state;

static report_state *state(tide_profile_report *r)
{
    if (!r->state) {
        report_state *s = tide_alloc(sizeof *s);
        memset(s, 0, sizeof *s);
        s->rows = tide_alloc(TIDE_PROFILE_REPORT_ROWS * sizeof *s->rows);
        s->index = tide_alloc(INDEX_SIZE * sizeof *s->index);
        memset(s->index, 0, INDEX_SIZE * sizeof *s->index);
        r->state = s;
    }
    return r->state;
}

static uint64_t mix(uint64_t n)
{
    n = (n ^ (n >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    n = (n ^ (n >> 27)) * UINT64_C(0x94d049bb133111eb);
    return n ^ (n >> 31);
}

static void sample(distribution *d, const double ms)
{
    if (!(ms >= 0.0) || !isfinite(ms)) return;
    if (!d->count || ms < d->min) d->min = ms;
    if (!d->count || ms > d->max) d->max = ms;
    d->total += ms;
    const uint64_t n = ++d->count;
    // Deterministic reservoir, independent of the game's RNG. Exact for short
    // captures; bounded uniformly sampled estimates for longer ones.
    const uint64_t at = n <= TIDE_PROFILE_REPORT_SAMPLES ? n - 1u : mix(n) % n;
    if (at < TIDE_PROFILE_REPORT_SAMPLES) d->samples[at] = ms;
}

static uint64_t hash_text(uint64_t hash, const char *text)
{
    for (const unsigned char *p = (const unsigned char *)(text ? text : ""); *p; p++) hash = (hash ^ *p) * UINT64_C(1099511628211);
    return (hash ^ 255u) * UINT64_C(1099511628211);
}

static bool equal_text(const char *a, const char *b)
{
    return strcmp(a ? a : "", b ? b : "") == 0;
}

static report_row *row_for(report_state *s, const uint32_t group, const tide_profile_time *t)
{
    const char *name = t->source.name ? t->source.name : t->name;
    const char *file = t->source.file;
    uint64_t hash = hash_text(hash_text(UINT64_C(14695981039346656037), name), file);
    hash = mix(hash ^ ((uint64_t)t->source.line << 32) ^ t->source.column ^ ((uint64_t)group << 60));
    uint32_t slot = (uint32_t)hash & (INDEX_SIZE - 1u);
    while (s->index[slot]) {
        report_row *r = &s->rows[s->index[slot] - 1u];
        if (r->hash == hash && r->group == group && r->source.line == t->source.line &&
            r->source.column == t->source.column && equal_text(r->source.name, name) && equal_text(r->source.file, file)) return r;
        slot = (slot + 1u) & (INDEX_SIZE - 1u);
    }
    const size_t name_bytes = strlen(name) + 1u, file_bytes = file ? strlen(file) + 1u : 0u;
    if (s->count == TIDE_PROFILE_REPORT_ROWS || name_bytes > TIDE_PROFILE_REPORT_SOURCE_BYTES - s->source_bytes ||
        file_bytes > TIDE_PROFILE_REPORT_SOURCE_BYTES - s->source_bytes - name_bytes) {
        s->dropped++;
        return NULL;
    }
    report_row *r = &s->rows[s->count];
    *r = (report_row){.group = group, .hash = hash};
    char *copy = tide_alloc(name_bytes + file_bytes);
    memcpy(copy, name, name_bytes);
    if (file_bytes) memcpy(copy + name_bytes, file, file_bytes);
    r->source = (tide_profile_source){copy, file_bytes ? copy + name_bytes : NULL, t->source.line, t->source.column};
    s->source_bytes += (uint32_t)(name_bytes + file_bytes);
    s->index[slot] = ++s->count;
    return r;
}

static void collect(report_state *s, const tide_profile *p, const bool before)
{
    if (!p) return;
    const tide_profile_times *groups[GROUPS] = {&p->systems, &p->views, &p->workers};
    for (uint32_t g = 0; g < GROUPS; g++) {
        for (uint32_t i = 0; i < groups[g]->count; i++) {
            const tide_profile_time *t = &groups[g]->items[i];
            if (!t->measurements && !t->source.name) continue;
            report_row *r = row_for(s, g, t);
            if (!r) continue;
            if (before) {
                r->serial = s->serial;
                r->before_count = t->measurements;
                r->before_ms = t->measured_ms;
                r->table_revision = groups[g]->revision;
                r->row_revision = t->revision;
            } else {
                const bool same = r->serial == s->serial && r->table_revision == groups[g]->revision && r->row_revision == t->revision
                    && t->measurements >= r->before_count && t->measured_ms >= r->before_ms;
                const uint64_t calls = t->measurements - (same ? r->before_count : 0u);
                const double ms = t->measured_ms - (same ? r->before_ms : 0.0);
                if (calls) {
                    r->calls += calls;
                    r->frames++;
                    r->total_ms += ms;
                    if (ms > r->max_frame_ms) r->max_frame_ms = ms;
                }
            }
        }
    }
}

void tide_profile_report_begin(tide_profile_report *r, const tide_profile *p, const uint32_t frame)
{
    report_state *s = state(r);
    s->serial++;
    collect(s, p, true);
    s->before_ticks = p ? p->ticks : 0u;
    s->before_systems = p ? p->systems_ms : 0.0;
    s->before_changes = p ? p->changes_ms : 0.0;
    memset(s->frame_parts, 0, sizeof s->frame_parts);
    memset(s->render_parts, 0, sizeof s->render_parts);
    s->rendered = false;
    if (!s->frames) s->first = frame;
    s->last = frame;
    s->began = s->mark = tide_time_now_ns();
}

void tide_profile_report_mark(tide_profile_report *r, const tide_frame_part part)
{
    report_state *s = r->state;
    if (!s || !s->began || (uint32_t)part >= TIDE_FRAME_PARTS) return;
    const uint64_t now = tide_time_now_ns();
    s->frame_parts[part] += now > s->mark ? (double)(now - s->mark) * 1e-6 : 0.0;
    s->mark = now;
}

void tide_profile_report_end(tide_profile_report *r, const tide_profile *p, const uint64_t draw_calls, const uint64_t vertices)
{
    report_state *s = r->state;
    if (!s || !s->began) return;
    tide_profile_report_mark(r, TIDE_FRAME_OTHER);
    sample(&s->metrics[CPU], s->mark > s->began ? (double)(s->mark - s->began) * 1e-6 : 0.0);
    for (uint32_t i = 0; i < TIDE_FRAME_PARTS; i++) sample(&s->metrics[i], s->frame_parts[i]);
    if (s->rendered) { // The render phase's parts, and what's left of it: the host's own work there
        double parts = 0.0;
        for (uint32_t i = 0; i < TIDE_RENDER_PARTS; i++) {
            sample(&s->metrics[RENDER + i], s->render_parts[i]);
            parts += s->render_parts[i];
        }
        const double left = s->frame_parts[TIDE_FRAME_RENDER] - parts;
        sample(&s->metrics[RENDER_OTHER], left > 0.0 ? left : 0.0);
    }
    collect(s, p, false);
    if (p) {
        s->ticks += p->ticks >= s->before_ticks ? p->ticks - s->before_ticks : p->ticks;
        s->systems_ms += p->systems_ms >= s->before_systems ? p->systems_ms - s->before_systems : p->systems_ms;
        s->changes_ms += p->changes_ms >= s->before_changes ? p->changes_ms - s->before_changes : p->changes_ms;
    }
    s->draw_calls += draw_calls;
    s->vertices += vertices;
    s->frames++;
    s->began = 0;
}

void tide_profile_report_render(tide_profile_report *r, const double ms[TIDE_RENDER_PARTS])
{
    report_state *s = r->state;
    if (!s || !s->began) return;
    for (uint32_t i = 0; i < TIDE_RENDER_PARTS; i++) s->render_parts[i] += ms[i];
    s->rendered = true;
}

void tide_profile_report_gpu(tide_profile_report *r, const double ms)
{
    sample(&state(r)->metrics[GPU], ms);
}

void tide_profile_report_alongside(tide_profile_report *r, const double ms)
{
    sample(&state(r)->metrics[ALONGSIDE], ms);
}

void tide_profile_report_occlusion(tide_profile_report *r, const uint64_t tested, const uint64_t hidden)
{
    report_state *s = state(r);
    s->occlusion_tested += tested;
    s->occlusion_hidden += hidden;
    s->occlusion_frames++;
}

void tide_profile_report_gpu_pass(void *report, const tide_gpu_pass_sample *pass)
{
    report_state *s = state(report);
    if (!(pass->ms >= 0.0) || !isfinite(pass->ms)) { s->gpu_unmeasured++; return; }
    const char *names = pass->shaders ? pass->shaders : "";
    gpu_row *row = NULL;
    for (uint32_t i = 0; i < s->gpu_count; i++) {
        gpu_row *r = s->gpu_rows[i];
        if (r->identity.target == pass->target && r->identity.pass == pass->pass &&
            r->identity.width == pass->width && r->identity.height == pass->height &&
            equal_text(r->identity.shaders, names)) { row = r; break; }
    }
    if (!row) {
        const size_t bytes = strlen(names) + 1;
        if (s->gpu_count == TIDE_GPU_PROFILE_ROWS || bytes > TIDE_PROFILE_REPORT_SOURCE_BYTES - s->gpu_name_bytes) {
            s->gpu_dropped++;
            return;
        }
        row = tide_alloc(sizeof *row);
        memset(row, 0, sizeof *row);
        row->identity = *pass;
        char *copy = tide_alloc(bytes);
        memcpy(copy, names, bytes);
        row->identity.shaders = copy;
        s->gpu_rows[s->gpu_count++] = row;
        s->gpu_name_bytes += (uint32_t)bytes;
    }
    sample(&row->timing, pass->ms);
    row->draw_calls += pass->draw_calls;
    row->vertices += pass->vertices;
}

static void string(FILE *f, const char *s)
{
    if (!s) { fputs("null", f); return; }
    fputc('"', f);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p == '"' || *p == '\\') { fputc('\\', f); fputc(*p, f); }
        else if (*p < 32u) fprintf(f, "\\u%04x", (unsigned)*p);
        else fputc(*p, f);
    }
    fputc('"', f);
}

static void source(FILE *f, const tide_profile_source *s)
{
    fputs("{\"name\":", f); string(f, s->name);
    fputs(",\"file\":", f); string(f, s->file && s->file[0] ? s->file : NULL);
    fprintf(f, ",\"line\":%u,\"column\":%u}", (unsigned)s->line, (unsigned)s->column);
}

static int compare(const void *a, const void *b)
{
    const double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static void distribution_json(FILE *f, const distribution *d)
{
    const uint32_t n = (uint32_t)(d->count < TIDE_PROFILE_REPORT_SAMPLES ? d->count : TIDE_PROFILE_REPORT_SAMPLES);
    fprintf(f, "{\"samples\":%llu,\"total_ms\":%.9g,\"mean_ms\":", (unsigned long long)d->count, d->total);
    if (n) {
        double *sorted = tide_alloc(n * sizeof *sorted);
        memcpy(sorted, d->samples, n * sizeof *sorted);
        qsort(sorted, n, sizeof *sorted, compare);
        fprintf(f, "%.9g,\"min_ms\":%.9g,\"max_ms\":%.9g,\"p50_ms\":%.9g,\"p95_ms\":%.9g",
                d->total / (double)d->count, d->min, d->max, sorted[(n - 1u) / 2u], sorted[(n * 95u + 99u) / 100u - 1u]);
        free(sorted);
    } else fputs("null,\"min_ms\":null,\"max_ms\":null,\"p50_ms\":null,\"p95_ms\":null", f);
    fprintf(f, ",\"quantile_samples\":%u,\"quantiles_estimated\":%s}", (unsigned)n,
            d->count > TIDE_PROFILE_REPORT_SAMPLES ? "true" : "false");
}

bool tide_profile_report_write(const tide_profile_report *r, FILE *out, const tide_profile_report_info *info)
{
    const report_state *s = r->state;
    // No measured frame is a valid, explicitly empty report (warmup may use
    // the entire run). Allocate no persistent capture just to write it.
    if (!s) {
        tide_profile_report empty = {0};
        state(&empty);
        empty.draw = r->draw;
        const bool ok = tide_profile_report_write(&empty, out, info);
        empty.draw = (tide_draw_diagnostics){0}; // Borrowed only for serialization.
        tide_profile_report_free(&empty);
        return ok;
    }
    fputs("{\n\"schema\":\"tide.profile\",\"version\":1,\n\"metadata\":{\"title\":", out); string(out, info->title);
    fputs(",\"renderer\":", out); string(out, info->renderer);
    fputs(",\"compiler\":", out); string(out, info->compiler);
    fputs(",\"executable_sha256\":", out); string(out, info->executable_sha256);
    fprintf(out, ",\"layout_hash\":\"%016llx\",\"simulation_id\":", (unsigned long long)info->layout_hash);
    if (info->simulation) {
        fputc('"', out);
        for (uint32_t i = 0; i < 32u; i++) fprintf(out, "%02x", (unsigned)info->simulation[i]);
        fputc('"', out);
    } else fputs("null", out);
    fprintf(out, ",\"optimized\":%s,\"sanitized\":%s,\"threads\":%u,\"player\":%u,\"players\":%u,"
                 "\"width\":%u,\"height\":%u,\"scale\":[%.9g,%.9g],\"exit_code\":%d},\n",
            info->optimized ? "true" : "false", info->sanitized ? "true" : "false", (unsigned)info->threads,
            (unsigned)info->player, (unsigned)info->players, (unsigned)info->width, (unsigned)info->height,
            (double)info->scale_x, (double)info->scale_y, info->exit_code);
    fprintf(out, "\"frames\":{\"total\":%u,\"warmup_requested\":%u,\"measured\":%llu,\"first\":%u,\"last\":%u},\n",
            (unsigned)info->total_frames, (unsigned)info->warmup, (unsigned long long)s->frames, (unsigned)s->first, (unsigned)s->last);
    fputs("\"cpu_work\":", out); distribution_json(out, &s->metrics[CPU]);
    fputs(",\n\"cpu_phases\":{", out);
    const char *names[TIDE_FRAME_PARTS] = {"input", "match", "views", "render", "other"};
    for (uint32_t i = 0; i < TIDE_FRAME_PARTS; i++) {
        if (i) fputc(',', out);
        string(out, names[i]); fputc(':', out); distribution_json(out, &s->metrics[i]);
    }
    fputs("},\n\"render_phases\":{", out);
    const char *render_names[TIDE_RENDER_PARTS + 1] = {"prepare", "steps", "uploads", "culling", "record", "finish", "other"};
    for (uint32_t i = 0; i <= TIDE_RENDER_PARTS; i++) {
        if (i) fputc(',', out);
        string(out, render_names[i]); fputc(':', out); distribution_json(out, &s->metrics[RENDER + i]);
    }
    fputs("},\n\"gpu\":{\"available\":", out);
    fputs(s->metrics[GPU].count ? "true" : "false", out);
    fputs(",\"frame_aligned\":false,\"timing\":", out); distribution_json(out, &s->metrics[GPU]); fputs("},\n", out);
    fprintf(out, "\"gpu_passes\":{\"pending\":%u,\"unmeasured\":%llu,\"dropped_observations\":%llu,\"rows\":[",
            (unsigned)info->gpu_pending_passes, (unsigned long long)s->gpu_unmeasured, (unsigned long long)s->gpu_dropped);
    for (uint32_t i = 0; i < s->gpu_count; i++) {
        const gpu_row *row = s->gpu_rows[i];
        if (i) fputc(',', out);
        fprintf(out, "{\"target\":\"%016llx\",\"pass\":%u,\"width\":%u,\"height\":%u,\"shaders\":",
                (unsigned long long)row->identity.target, (unsigned)row->identity.pass,
                (unsigned)row->identity.width, (unsigned)row->identity.height);
        string(out, row->identity.shaders);
        fprintf(out, ",\"draw_calls\":%llu,\"vertices\":%llu,\"timing\":",
                (unsigned long long)row->draw_calls, (unsigned long long)row->vertices);
        distribution_json(out, &row->timing);
        fputc('}', out);
    }
    fputs("]},\n", out);
    fprintf(out, "\"ticks\":{\"count\":%llu,\"systems_ms\":%.9g,\"changes_events_tasks_ms\":%.9g},\n",
            (unsigned long long)s->ticks, s->systems_ms, s->changes_ms);
    fputs("\"ticks_alongside\":", out); distribution_json(out, &s->metrics[ALONGSIDE]); fputs(",\n", out);
    for (uint32_t g = 0; g < GROUPS; g++) {
        const char *groups[GROUPS] = {"systems", "views", "workers"};
        string(out, groups[g]); fputs(":[", out);
        bool comma = false;
        for (uint32_t i = 0; i < s->count; i++) {
            const report_row *row = &s->rows[i];
            if (row->group != g || !row->calls) continue;
            if (comma) fputc(',', out);
            comma = true;
            fputs("{\"source\":", out); source(out, &row->source);
            fprintf(out, ",\"calls\":%llu,\"frames\":%llu,\"total_ms\":%.9g,\"mean_call_ms\":%.9g,\"mean_frame_ms\":%.9g,\"max_frame_ms\":%.9g}",
                    (unsigned long long)row->calls, (unsigned long long)row->frames, row->total_ms,
                    row->total_ms / (double)row->calls, s->frames ? row->total_ms / (double)s->frames : 0.0, row->max_frame_ms);
        }
        fputs("],\n", out);
    }
    fputs("\"geometry\":{\"dropped_instances\":", out);
    fprintf(out, "%llu,\"rows\":[", (unsigned long long)r->draw.dropped_geometry_instances);
    bool geometry_comma = false;
    for (uint32_t i = 0; i < r->draw.geometry_count; i++) {
        const tide_draw_geometry *d = &r->draw.geometry[i];
        if (!d->instances) continue;
        if (geometry_comma) fputc(',', out);
        geometry_comma = true;
        fputs("{\"source\":", out); source(out, &d->source);
        fputs(",\"mesh\":", out); string(out, d->mesh);
        fprintf(out, ",\"identity\":\"%016llx:%016llx:%016llx\",\"target\":\"%016llx\",\"lod\":%u,\"mesh_vertices\":%u,\"mesh_indices\":%u,"
                     "\"instances\":%llu,\"vertices\":%llu,\"triangles\":%llu}",
                (unsigned long long)d->identity, (unsigned long long)d->geometry_id, (unsigned long long)d->geometry_other,
                (unsigned long long)d->target, (unsigned)d->lod,
                (unsigned)d->mesh_vertices, (unsigned)d->mesh_indices, (unsigned long long)d->instances,
                (unsigned long long)d->vertices, (unsigned long long)d->triangles);
    }
    fputs("]},\n", out);
    fprintf(out, "\"render_work\":{\"draw_calls\":%llu,\"vertices\":%llu},\n", (unsigned long long)s->draw_calls,
            (unsigned long long)s->vertices);
    fprintf(out, "\"occlusion\":{\"frames\":%llu,\"tested\":%llu,\"hidden\":%llu},\n\"draw_diagnostics\":[",
            (unsigned long long)s->occlusion_frames, (unsigned long long)s->occlusion_tested,
            (unsigned long long)s->occlusion_hidden);
    for (uint32_t i = 0; i < r->draw.count; i++) {
        const tide_draw_diagnostic *d = &r->draw.rows[i];
        if (i) fputc(',', out);
        fputs("{\"source\":", out); source(out, &d->source);
        fprintf(out, ",\"target\":\"%016llx\",\"camera_slot\":%u,\"calls\":%llu,\"mesh_recording_ms\":%.9g,"
                     "\"candidates\":%llu,\"tested\":%llu,\"rejected\":%llu,\"untestable\":%llu,\"bypassed\":%llu,\"submitted\":%llu,"
                     "\"gpu_culled\":%llu,\"bounds_cache_hits\":%llu,\"bounds_builds\":%llu,\"frustum_builds\":%llu}",
                (unsigned long long)d->target, (unsigned)d->camera_slot, (unsigned long long)d->calls, (double)d->mesh_recording_ns * 1e-6,
                (unsigned long long)d->candidates, (unsigned long long)d->tested, (unsigned long long)d->rejected,
                (unsigned long long)d->untestable, (unsigned long long)d->bypassed, (unsigned long long)d->submitted,
                (unsigned long long)d->gpu_culled, (unsigned long long)d->bounds_cache_hits, (unsigned long long)d->bounds_builds, (unsigned long long)d->frustum_builds);
    }
    fprintf(out, "],\n\"limits\":{\"dropped_timing_observations\":%llu,\"dropped_draw_calls\":%llu,\"dropped_draw_candidates\":%llu},\n",
            (unsigned long long)s->dropped, (unsigned long long)r->draw.dropped_calls, (unsigned long long)r->draw.dropped_candidates);
    fputs("\"semantics\":{\"cpu_work\":\"Host callback wall time; excludes interactive waits, present, headless audio mixing, screenshot readback and report collection. Includes instrumentation and any enabled overlays. Not display FPS.\","
          "\"render_phases\":\"Parts of cpu_phases.render: the renderer's CPU work on the frame's list, then other (the host's own work there). Excludes presenting, where command buffers are submitted and frames in flight waited for.\","
          "\"ticks_alongside\":\"Each tick that ran beside a frame (the ticksAlongsideViews setting, --ticks-alongside): its systems, changes, handlers and hash, on the threads' leading thread, from start to end. It overlaps the frames it ran beside and is in no cpu_phases part: cpu_phases.match is then what beginning and ending ticks took the frame's thread, and any wait for one that hadn't ended.\","
          "\"systems\":\"Elapsed first-job-start to last-job-end; overlapping systems must not be summed as CPU work.\","
          "\"workers\":\"Completed-job queue, compute and copy intervals; may overlap frames.\","
          "\"mesh_recording\":\"Inclusive bounds, frustum testing, LOD and instance recording after mesh acquisition; contained in the caller view, not isolated culling or GPU time.\","
          "\"camera_slot\":\"Per-source, per-target camera encounter order each frame; conditional reordering changes its meaning. Target zero is the window, not a GPU pass ID.\","
          "\"geometry\":\"Actual submitted cached 3D and shader mesh instances, grouped by mesh/version, originating view, destination and selected LOD. Counts repeated passes; vertices are submitted indices, not unique vertices or GPU time.\","
          "\"gpu_passes\":\"Whole render-pass intervals grouped by target, encounter order, pixel size and shader membership. Mixed shaders have no individual cost attribution. Passes may overlap and omit work outside passes; do not sum as frame time. Conditional order changes identity.\","
          "\"gpu\":\"Asynchronous results from measured work; unavailable or pending samples are absent, never zero. CPU and GPU overlap.\","
          "\"quantiles\":\"Exact nearest-rank quantiles through 4096 samples, then deterministic reservoir estimates; totals, means and maxima cover all observations.\"}\n}\n", out);
    return !ferror(out);
}

void tide_profile_report_free(tide_profile_report *r)
{
    report_state *s = r->state;
    if (s) {
        for (uint32_t i = 0; i < s->gpu_count; i++) {
            free((void *)s->gpu_rows[i]->identity.shaders);
            free(s->gpu_rows[i]);
        }
        for (uint32_t i = 0; i < s->count; i++) free((void *)s->rows[i].source.name);
        free(s->rows);
        free(s->index);
        free(s);
    }
    tide_draw_diagnostics_free(&r->draw);
    *r = (tide_profile_report){0};
}
