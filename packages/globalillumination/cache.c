#include "tide/pack.h"
#include "tide/draw.h"
#include "tide/page.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

// Preserve the complete generation bits through Tide's signed long ABI.
// No pointer, geometry byte or external state is retained by this bridge.
int64_t GIAssetGeneration(void)
{
    const uint64_t generation = tide_pack_generation();
    int64_t result;
    memcpy(&result, &generation, sizeof result);
    return result;
}

// Probe faces share the same captured shader inputs, including captureEye.
// Only their target and built-in camera differ. These private helpers reuse
// bytes already owned by this frame's draw list; no component borrow, pointer
// or packet cache survives a call/frame. The caller settles the next face's
// target/camera with Clear before replaying and records the source with CPU
// frustum rejection disabled, so all six directions have geometry to replay.
static size_t gi_bytes(const uint64_t count, const size_t size)
{
    if (size && count > SIZE_MAX / size) tide_out_of_memory();
    return (size_t)count * size;
}

static void *gi_room(void *memory, const uint32_t used, uint32_t *capacity,
                     const uint32_t extra, const size_t size)
{
    const uint64_t needed = (uint64_t)used + extra;
    if (needed > UINT32_MAX) tide_out_of_memory();
    if (needed <= *capacity) return memory;
    uint64_t next = *capacity ? *capacity : 64u;
    while (next < needed) next *= 2u;
    if (next > UINT32_MAX) next = needed;
    memory = tide_realloc(memory, gi_bytes(*capacity, size), gi_bytes(next, size));
    *capacity = (uint32_t)next;
    return memory;
}

static void gi_close_plain(tide_draw_list *d)
{
    if (d->plain_open && d->plain_open <= d->count) {
        tide_draw_command *command = &d->commands[d->plain_open - 1u];
        if (command->kind == TIDE_DRAW_PLAIN && command->plain.first <= d->plain_count)
            command->plain.count = d->plain_count - command->plain.first;
    }
    d->plain_open = 0;
}

int32_t GIRecordMark(tide_draw_list *d)
{
    if (!d || d->count > INT32_MAX || (d->count && !d->commands)) return -1;
    // End a preceding compatible shaded run. A following draw must not extend
    // a run belonging to an earlier instance's replay span.
    if (d->count && d->commands[d->count - 1u].kind == TIDE_DRAW_SHADED) {
        if (d->count == INT32_MAX) return -1;
        d->commands = gi_room(d->commands, d->count, &d->capacity, 1u, sizeof *d->commands);
        gi_close_plain(d);
        d->commands[d->count++] = (tide_draw_command){.kind = TIDE_DRAW_MESH_3D};
    }
    return (int32_t)d->count;
}

static bool gi_packet(const tide_draw_list *d, const tide_draw_command *command, const uint32_t source_runs)
{
    if (command->kind != TIDE_DRAW_SHADED || !d->runs || command->shaded.run >= source_runs) return false;
    const tide_draw_run *run = &d->runs[command->shaded.run];
    const tide_shader *shader = run->shader;
    if (!shader || shader->dims != 3 || shader->texture_count > TIDE_SHADER_TEXTURES
        || !run->count || !run->mesh || run->mesh > d->mesh_count || !d->meshes) return false;
    const tide_draw_mesh_data *mesh = &d->meshes[run->mesh - 1u];
    if (mesh->space != TIDE_MESH_BYTES || mesh->vertex_size != shader->vertex_size || !mesh->index_count) return false;
    const uint64_t instance = ((uint64_t)shader->instance_floats + shader->instance_ints) * 16u;
    const uint64_t uniform = ((uint64_t)shader->uniform_floats + shader->uniform_ints) * 16u;
    if (instance > UINT32_MAX || uniform > UINT32_MAX || run->first > d->instance_used
        || instance * run->count > d->instance_used - run->first
        || (instance && !d->instances) || run->uniforms > d->uniform_used
        || uniform > d->uniform_used - run->uniforms || (uniform && !d->uniforms)) return false;
    for (uint32_t index = 0; index < shader->texture_count; index++) {
        if (run->textures[index] > d->texture_count) return false;
    }
    return true;
}

int32_t GIReplay(tide_draw_list *d, const int32_t first, const int32_t end)
{
    if (!d || first < 0 || end <= first || (uint32_t)end > d->count || !d->commands
        || d->unsettled) return 0;
    const uint32_t source_runs = d->run_count;
    uint32_t packets = 0;
    for (uint32_t index = (uint32_t)first; index < (uint32_t)end; index++) {
        if (gi_packet(d, &d->commands[index], source_runs)) packets++;
    }
    if (!packets) return 0;
    d->commands = gi_room(d->commands, d->count, &d->capacity, packets, sizeof *d->commands);
    d->runs = gi_room(d->runs, d->run_count, &d->run_capacity, packets, sizeof *d->runs);
    gi_close_plain(d);
    for (uint32_t index = (uint32_t)first; index < (uint32_t)end; index++) {
        const tide_draw_command command = d->commands[index];
        if (!gi_packet(d, &command, source_runs)) continue;
        const tide_draw_run run = d->runs[command.shaded.run];
        d->runs[d->run_count] = run;
        d->commands[d->count] = command;
        d->commands[d->count++].shaded.run = d->run_count++;
    }
    // Runs have independent descriptors: later recording may batch into the
    // last replay without changing any earlier face's instance count.
    return (int32_t)packets;
}

// Draw.list selects the window before entering an extern. Restore the explicit
// face destination, whose camera was already settled by its clear.
int32_t GIRecordMarkTo(tide_draw_list *d, const tide_render_target target)
{
    return d ? GIRecordMark(tide_draw_render_target(d, target)) : -1;
}

int32_t GIReplayTo(tide_draw_list *d, const tide_render_target target, const int32_t first, const int32_t end)
{
    return d ? GIReplay(tide_draw_render_target(d, target), first, end) : 0;
}
