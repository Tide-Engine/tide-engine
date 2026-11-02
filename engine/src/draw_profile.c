#include "tide/draw_profile.h"

#include <stdlib.h>
#include <string.h>

#include "tide/draw.h"
#include "tide/page.h"

static const char *source_name(const tide_profile_source *source)
{
    return source && source->name ? source->name : "(unattributed)";
}

static const char *source_file(const tide_profile_source *source)
{
    return source && source->file ? source->file : "";
}

static bool same_source(const tide_draw_diagnostic *row, const tide_profile_source *source)
{
    return row->source.line == (source ? source->line : 0u)
        && row->source.column == (source ? source->column : 0u)
        && strcmp(row->source.name, source_name(source)) == 0
        && strcmp(row->source.file, source_file(source)) == 0;
}

static bool this_frame(const tide_draw_diagnostic *row, const tide_draw_list *d)
{
    return row->list_identity == (uintptr_t)d && row->frame == d->frame;
}

static tide_draw_diagnostic *diagnostic_row(tide_draw_list *d)
{
    tide_draw_diagnostics *p = d->diagnostics;
    const uint64_t target = d->target && d->target <= d->target_count ? d->targets[d->target - 1u].id : 0u;
    const tide_profile_source *source = d->profile_source;
    if (p->last && p->last <= p->count) {
        tide_draw_diagnostic *row = &p->rows[p->last - 1u];
        if (row->target == target && this_frame(row, d) && row->camera_index == d->place.camera_3d
            && same_source(row, source)) return row;
    }
    uint32_t slot = 1;
    for (uint32_t i = 0; i < p->count; i++) {
        tide_draw_diagnostic *row = &p->rows[i];
        if (row->target != target || !same_source(row, source) || !this_frame(row, d)) continue;
        if (row->camera_index == d->place.camera_3d) {
            p->last = i + 1u;
            return row;
        }
        if (row->camera_slot >= slot) slot = row->camera_slot + 1u;
    }
    for (uint32_t i = 0; i < p->count; i++) {
        tide_draw_diagnostic *row = &p->rows[i];
        if (row->target != target || row->camera_slot != slot || !same_source(row, source)) continue;
        row->list_identity = (uintptr_t)d;
        row->frame = d->frame;
        row->camera_index = d->place.camera_3d;
        p->last = i + 1u;
        return row;
    }
    if (p->count == TIDE_DRAW_PROFILE_ROWS) return NULL;
    const char *name = source_name(source), *file = source_file(source);
    const size_t name_bytes = strlen(name) + 1u, file_bytes = strlen(file) + 1u;
    const size_t remaining = TIDE_DRAW_PROFILE_SOURCE_BYTES - p->source_bytes;
    if (name_bytes > remaining || file_bytes > remaining - name_bytes) return NULL;
    if (!p->rows) {
        p->rows = tide_alloc(TIDE_DRAW_PROFILE_ROWS * sizeof *p->rows);
        memset(p->rows, 0, TIDE_DRAW_PROFILE_ROWS * sizeof *p->rows);
    }
    char *copy = tide_alloc(name_bytes + file_bytes);
    memcpy(copy, name, name_bytes);
    memcpy(copy + name_bytes, file, file_bytes);
    tide_draw_diagnostic *row = &p->rows[p->count++];
    *row = (tide_draw_diagnostic){
        .source = {.name = copy, .file = copy + name_bytes,
                   .line = source ? source->line : 0u, .column = source ? source->column : 0u},
        .target = target, .camera_slot = slot,
        .list_identity = (uintptr_t)d, .frame = d->frame, .camera_index = d->place.camera_3d,
    };
    p->source_bytes += (uint32_t)(name_bytes + file_bytes);
    p->last = p->count;
    return row;
}

void tide_draw_diagnostics_clear(tide_draw_diagnostics *p)
{
    if (!p) return;
    for (uint32_t i = 0; i < p->count; i++) free((void *)p->rows[i].source.name);
    p->count = p->last = p->source_bytes = 0;
    p->dropped_calls = p->dropped_candidates = 0;
}

void tide_draw_diagnostics_free(tide_draw_diagnostics *p)
{
    if (!p) return;
    tide_draw_diagnostics_clear(p);
    free(p->rows);
    *p = (tide_draw_diagnostics){0};
}

tide_draw_diagnostic_scope tide_draw_diagnostics_begin(tide_draw_list *d, const uint32_t candidates)
{
    tide_draw_diagnostic_scope scope = {.previous = d->profile_active};
    if (!d->diagnostics || !candidates) return scope;
    scope.row = diagnostic_row(d);
    d->profile_active = scope.row;
    if (scope.row) {
        scope.row->calls++;
        scope.row->candidates += candidates;
        scope.began = tide_time_now_ns();
    } else {
        d->diagnostics->dropped_calls++;
        d->diagnostics->dropped_candidates += candidates;
    }
    return scope;
}

void tide_draw_diagnostics_end(tide_draw_list *d, const tide_draw_diagnostic_scope scope)
{
    if (scope.row) {
        const uint64_t now = tide_time_now_ns();
        if (now > scope.began) scope.row->mesh_recording_ns += now - scope.began;
    }
    d->profile_active = scope.previous;
}
