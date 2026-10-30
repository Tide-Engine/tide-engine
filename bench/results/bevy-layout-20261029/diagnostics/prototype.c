// Experimental canonical layouts for this pure numeric workload only.
// Reuse its exact seed, arithmetic and independent C reference.
#include "model.c"
#include <stdio.h>
#include "tide/time.h"

enum { RECORDS, BLOCKS4, PACKED, WRITEBACK, REBUILD, MODES };
typedef struct layout {
    uint32_t count, moving, mode, share;
    bool tagged, ready, keeps;
    size_t stride, floats;
    float *data, *snapshot, *mirror;
    uint32_t *selected, *entity_at, *row_of;
    uint32_t kept_count;
    tide_queue commands;
    tide_entities entities;
} layout;

static size_t address(const layout *w, uint32_t row, uint32_t lane)
{
    if (w->mode == RECORDS) return (size_t)row * LANES + lane;
    if (w->mode == BLOCKS4) return (size_t)(row / 4u) * LANES * 4u + lane * 4u + row % 4u;
    return (size_t)lane * w->stride + row;
}

static void *layout_make(const versus_flag *f)
{
    layout *w = tide_alloc_zeroed(1, sizeof *w);
    w->count = f[0].value;
    w->keeps = f[1].value != 0;
    w->tagged = f[2].value != 0;
    w->share = f[3].value;
    w->mode = f[4].value;
    if (w->mode >= MODES) abort();
    return w;
}

static void layout_setup(layout *w)
{
    wide initial = {0};
    initial.count = w->count;
    initial.tagged = w->tagged;
    initial.frozen_share = w->share;
    setup(&initial);
    w->moving = initial.moving;
    const size_t rounded = ((size_t)w->count + 15u) / 16u * 16u;
    w->stride = rounded + 192u;
    w->floats = w->mode >= PACKED ? w->stride * LANES : (rounded ? rounded : 4u) * LANES;
    w->data = tide_alloc_zeroed(w->floats, sizeof(float));
    if (w->keeps) w->snapshot = tide_alloc_zeroed(w->floats, sizeof(float));
    const uint32_t n = w->count ? w->count : 1u;
    w->selected = tide_alloc((size_t)n * sizeof(uint32_t));
    w->entity_at = tide_alloc((size_t)n * sizeof(uint32_t));
    w->row_of = tide_alloc((size_t)n * sizeof(uint32_t));
    if (w->mode >= WRITEBACK) w->mirror = tide_alloc_zeroed((size_t)n * LANES, sizeof(float));
    uint32_t active = 0, frozen = w->moving;
    for (uint32_t i = 0; i < w->count; i++) {
        const bool moving = initial.frozen[i] == 0;
        const uint32_t row = w->mode >= PACKED ? (moving ? active : frozen++) : i;
        if (moving) w->selected[active++] = i;
        w->entity_at[row] = i;
        w->row_of[i] = row;
        for (uint32_t l = 0; l < LANES; l++) {
            w->data[address(w, row, l)] = initial.lane[l][i];
            if (w->mirror) w->mirror[(size_t)l * w->count + i] = initial.lane[l][i];
        }
    }
    free(initial.block);
    free(initial.frozen);
    w->ready = true;
}

static inline void scalar_step(float value[LANES])
{
    const float keep = 1.0f - value[DRAG] * VERSUS_DT;
    value[VX] = (value[VX] + value[AX] * VERSUS_DT) * keep;
    value[VY] = (value[VY] + value[AY] * VERSUS_DT) * keep;
    value[VZ] = (value[VZ] + value[AZ] * VERSUS_DT) * keep;
    value[PX] += value[VX] * VERSUS_DT;
    value[PY] += value[VY] * VERSUS_DT;
    value[PZ] += value[VZ] * VERSUS_DT;
    if (value[PY] < value[RADIUS]) {
        value[PY] = value[RADIUS] * 2.0f - value[PY];
        value[VY] = -value[VY] * value[BOUNCE];
    }
}

static uint32_t layout_tasks(const void *state, uint64_t *work)
{
    const layout *w = state;
    *work = (uint64_t)w->moving * 37u;
    // Retained query indices are contiguous even for unpartitioned storage.
    return w->moving ? (w->moving + 2047u) / 2048u : 1u;
}

static void layout_task(void *state, uint32_t task, uint32_t pieces)
{
    layout *w = state;
    const uint32_t chunk = task / pieces, piece = task % pieces;
    const uint32_t base = chunk * 2048u;
    const uint32_t n = w->moving - base < 2048u ? w->moving - base : 2048u;
    const uint32_t from = base + (uint32_t)((uint64_t)n * piece / pieces);
    const uint32_t to = base + (uint32_t)((uint64_t)n * (piece + 1u) / pieces);
    if (w->mode >= PACKED) {
        float *l[LANES];
        for (uint32_t k = 0; k < LANES; k++) l[k] = w->data + (size_t)k * w->stride;
        if (w->mode == REBUILD) {
            for (uint32_t k = 0; k < LANES; k++)
                for (uint32_t i = from; i < to; i++) l[k][i] = w->mirror[(size_t)k * w->count + w->entity_at[i]];
        }
        move_rows(from, to, VERSUS_DT, l[PX], l[PY], l[PZ], l[VX], l[VY], l[VZ],
                  l[AX], l[AY], l[AZ], l[DRAG], l[BOUNCE], l[RADIUS]);
        if (w->mode >= WRITEBACK) {
            for (uint32_t k = 0; k < AX; k++)
                for (uint32_t i = from; i < to; i++) w->mirror[(size_t)k * w->count + w->entity_at[i]] = l[k][i];
        }
    } else {
        for (uint32_t i = from; i < to; i++) {
            const uint32_t row = w->selected[i];
            if (w->mode == RECORDS) scalar_step(w->data + (size_t)row * LANES);
            else {
                float value[LANES];
                for (uint32_t k = 0; k < LANES; k++) value[k] = w->data[address(w, row, k)];
                scalar_step(value);
                for (uint32_t k = 0; k < AX; k++) w->data[address(w, row, k)] = value[k];
            }
        }
    }
}

static uint32_t idle_count(const void *state, uint64_t *work) { (void)state; *work = 0; return 1; }
static void idle_task(void *state, uint32_t task, uint32_t pieces) { (void)state; (void)task; (void)pieces; }
static const tide_system_tasks layout_systems[] = {
    {.count=idle_count, .run=idle_task, .name="Setup"},
    {.count=idle_count, .run=idle_task, .name="Move"},
    {.count=layout_tasks, .run=layout_task, .splits=true, .name="Shift"},
};

static void layout_tick(void *state, const tide_jobs *jobs)
{
    layout *w = state;
    if (!w->ready) { layout_setup(w); return; }
    if (w->keeps) {
        memcpy(w->snapshot, w->data, w->floats * sizeof(float));
        w->kept_count = w->count;
    }
    const tide_tick_systems schedule = {layout_systems, 3, NULL, &w->commands, &w->entities, NULL, NULL};
    tide_run_systems(w, &schedule, jobs);
}

static uint64_t layout_values_digest(const layout *w, const float *data, uint32_t count)
{
    uint64_t sum = 0;
    for (uint32_t i = 0; i < count; i++) {
        float value[LANES];
        for (uint32_t k = 0; k < LANES; k++) value[k] = data[address(w, i, k)];
        sum += versus_item(value, sizeof value);
    }
    return sum;
}

static uint64_t layout_digest(const void *state)
{
    const layout *w = state;
    uint64_t sum = layout_values_digest(w, w->data, w->ready ? w->count : 0u);
    if (w->keeps) {
        const uint64_t before = layout_values_digest(w, w->snapshot, w->kept_count);
        sum += versus_item(&before, sizeof before);
    }
    if (w->mirror) {
        float *lanes[LANES];
        for (uint32_t k = 0; k < LANES; k++) lanes[k] = w->mirror + (size_t)k * w->count;
        if (arrays_digest(w->ready ? w->count : 0u, lanes) != layout_values_digest(w, w->data, w->ready ? w->count : 0u)) abort();
    }
    return sum;
}

static void layout_free(void *state)
{
    layout *w = state;
    free(w->data); free(w->snapshot); free(w->mirror);
    free(w->selected); free(w->entity_at); free(w->row_of);
    tide_queue_free(&w->commands);
    tide_entities_free(&w->entities);
    free(w);
}

const versus_way layout_way = {"Layout", layout_make, layout_tick, layout_digest, layout_free};

// Packed storage can move rows while preserving entity identity. This is a
// diagnostic for membership maintenance, not a generic ECS structural path.
static void layout_toggle(layout *w, uint32_t entity)
{
    const uint32_t row = w->row_of[entity];
    const bool was_active = row < w->moving;
    const uint32_t boundary = was_active ? w->moving - 1u : w->moving;
    const uint32_t other = w->entity_at[boundary];
    for (uint32_t k = 0; k < LANES; k++) {
        const size_t a = address(w, row, k), b = address(w, boundary, k);
        const float saved = w->data[a]; w->data[a] = w->data[b]; w->data[b] = saved;
    }
    w->entity_at[row] = other; w->row_of[other] = row;
    w->entity_at[boundary] = entity; w->row_of[entity] = boundary;
    w->moving = was_active ? w->moving - 1u : w->moving + 1u;
}

int layout_validate(void)
{
    const uint32_t sizes[] = {0,1,3,4,17,127,2047,2048,2049,4099};
    uint64_t comparisons = 0;
    for (unsigned c = 0; c < sizeof sizes / sizeof *sizes; c++) {
        versus_flag f[5] = {0}; f[0].value=sizes[c]; f[2].value=1; f[3].value=95; f[4].value=PACKED;
        layout *w = layout_make(f); layout_setup(w);
        wide reference = {0}; reference.count=sizes[c]; reference.tagged=true; reference.frozen_share=95; setup(&reference);
        for (uint32_t step = 0; step < 30; step++) {
            for (uint32_t j = 0; j < 13 && w->count; j++) {
                const uint32_t id = (step * 137u + j * 31u) % w->count;
                layout_toggle(w, id);
                reference.frozen[id] ^= UINT32_MAX;
            }
            layout_tick(w, NULL);
            for (uint32_t id = 0; id < w->count; id++) {
                float v[LANES];
                for (uint32_t k = 0; k < LANES; k++) v[k]=reference.lane[k][id];
                if (!reference.frozen[id]) scalar_step(v);
                for (uint32_t k = 0; k < LANES; k++) {
                    reference.lane[k][id]=v[k];
                    if (memcmp(&v[k], &w->data[address(w,w->row_of[id],k)],sizeof(float))) return 1;
                    comparisons++;
                }
                if (w->entity_at[w->row_of[id]] != id) return 2;
            }
        }
        layout_free(w); free(reference.block); free(reference.frozen);
    }
    printf("membership validation: %llu exact field comparisons passed\n", (unsigned long long)comparisons);
    return 0;
}

int layout_maintenance(void)
{
    versus_flag f[5] = {0}; f[0].value=1000000; f[2].value=1; f[3].value=95; f[4].value=PACKED;
    layout *w=layout_make(f);
    uint64_t start=tide_time_now_ns(); layout_setup(w);
    printf("{\"setup_ms\":%.6f,\"active\":%u,\"data_bytes\":%zu,\"index_bytes\":%zu,\"churn\":[", (double)(tide_time_now_ns()-start)/1e6,w->moving,w->floats*sizeof(float),(size_t)w->count*3u*sizeof(uint32_t));
    const uint32_t rates[]={0,1,100,1000,10000};
    for (unsigned r=0;r<sizeof rates/sizeof *rates;r++) {
        double elapsed=0;
        for (uint32_t repeat=0;repeat<100;repeat++) {
            start=tide_time_now_ns();
            for (uint32_t j=0;j<rates[r];j++) layout_toggle(w,(j*7919u)%w->count);
            layout_tick(w,NULL);
            elapsed+=(double)(tide_time_now_ns()-start)/1e6;
            // Restore exactly the same membership outside the measured interval.
            for (uint32_t j=0;j<rates[r];j++) layout_toggle(w,(j*7919u)%w->count);
        }
        printf("%s{\"toggles\":%u,\"mean_tick_and_changes_ms\":%.6f}",r?",":"",rates[r],elapsed/100);
    }
    printf("]}\n"); layout_free(w); return 0;
}
