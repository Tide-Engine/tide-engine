#include "tide/draw.h"
#include "tide/page.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

// Private Render3D pass helpers. Only the draw list's already captured bytes
// cross this boundary; no component, resource borrow or cache survives it.
typedef struct transparent_packet {
    double depth;
    uint32_t command, first;
} transparent_packet;

static size_t room(const uint64_t count, const size_t size)
{
    if (size && count > SIZE_MAX / size) tide_out_of_memory();
    return (size_t)count * size;
}

int32_t TransparentMark(tide_draw_list *d)
{
    if (!d || d->count > INT32_MAX) return -1;
    // End a preceding compatible run, so a later draw cannot merge across the
    // mark. An empty mesh is a drawing no-op and changes no destination state.
    if (d->count && d->commands[d->count - 1u].kind == TIDE_DRAW_SHADED) {
        if (d->count == INT32_MAX) return -1;
        if (d->count == d->capacity) {
            const uint32_t capacity = d->count + 1u;
            d->commands = tide_realloc(d->commands, room(d->capacity, sizeof *d->commands),
                                      room(capacity, sizeof *d->commands));
            d->capacity = capacity;
        }
        d->commands[d->count++] = (tide_draw_command){.kind = TIDE_DRAW_MESH_3D};
    }
    return (int32_t)d->count;
}

static uint32_t sortable(const tide_draw_list *d, const tide_draw_command *c)
{
    if (c->kind != TIDE_DRAW_SHADED || c->blend != TIDE_BLEND_ALPHA || c->depth != TIDE_DEPTH_READ
        || c->shaded.run >= d->run_count) return 0;
    const tide_draw_run *r = &d->runs[c->shaded.run];
    const tide_shader *s = r->shader;
    if (!s || s->dims != 3 || s->instance_floats < 4u || !r->count || !r->mesh || r->mesh > d->mesh_count) return 0;
    const uint64_t size = ((uint64_t)s->instance_floats + s->instance_ints) * 16u;
    const uint64_t uniforms = ((uint64_t)s->uniform_floats + s->uniform_ints) * 16u;
    if (size > UINT32_MAX || !d->instances || r->first > d->instance_used
        || (uint64_t)r->count * size > d->instance_used - r->first
        || r->uniforms > d->uniform_used || uniforms > d->uniform_used - r->uniforms
        || (uniforms && !d->uniforms)) return 0;
    return (uint32_t)size;
}

// Bottom-up merge sort gives a bounded O(N log N) comparison count. Choosing
// the left packet on equal keys retains the original submission sequence.
static void sort_packets(transparent_packet *packets, transparent_packet *scratch, const uint32_t count)
{
    transparent_packet *from = packets, *to = scratch;
    for (uint64_t width = 1; width < count; width *= 2u) {
        for (uint64_t begin = 0; begin < count; begin += width * 2u) {
            const uint32_t middle = (uint32_t)(begin + width < count ? begin + width : count);
            const uint32_t end = (uint32_t)(begin + width * 2u < count ? begin + width * 2u : count);
            uint32_t a = (uint32_t)begin, b = middle;
            for (uint32_t at = (uint32_t)begin; at < end; at++) {
                to[at] = b == end || (a < middle && from[a].depth >= from[b].depth) ? from[a++] : from[b++];
            }
        }
        transparent_packet *swap = from;
        from = to;
        to = swap;
    }
    if (from != packets) memcpy(packets, from, room(count, sizeof *packets));
}

static bool finite3(const tide_float3 value)
{
    return tide_is_finite_f(value.x) && tide_is_finite_f(value.y) && tide_is_finite_f(value.z);
}

static tide_float3 center_of(const tide_draw_list *d, const tide_draw_run *r)
{
    const tide_float3 origin = {0};
    const tide_draw_mesh_data *m = &d->meshes[r->mesh - 1u];
    if (m->base) {
        if (m->base > d->mesh_count) return origin;
        m = &d->meshes[m->base - 1u];
        if (m->base) return origin;
    }
    if (m->space == TIDE_MESH_NONE) return origin;
    // A geometry AABB belongs to its recorded position field. A different
    // shader's cache (or a deforming shader with no position proof) is not it.
    const uint32_t position = r->shader->bounds_position;
    if (position && m->bounds_position == position && m->bounds.set == 1
        && m->vertex_size >= sizeof(tide_float3) && position - 1u <= m->vertex_size - sizeof(tide_float3)
        && finite3(m->bounds.center) && finite3(m->bounds.extents)
        && m->bounds.extents.x >= 0.0f && m->bounds.extents.y >= 0.0f && m->bounds.extents.z >= 0.0f) {
        return m->bounds.center;
    }
    // A selected LOD uses its base mesh's sphere. This remains available when
    // automatic geometry bounds were not cached, without rescanning vertices.
    const tide_float3 center = {m->center[0], m->center[1], m->center[2]};
    if (m->levels && m->levels <= TIDE_MESH_LEVELS && finite3(center)
        && tide_is_finite_f(m->radius) && m->radius >= 0.0f) return center;
    return origin;
}

static double depth_of(const uint8_t *bytes, const tide_float3 center,
                       const tide_float3 eye, const tide_float3 forward)
{
    float world[16];
    memcpy(world, bytes, sizeof world);
    if (!tide_is_finite_f(world[12]) || !tide_is_finite_f(world[13]) || !tide_is_finite_f(world[14])) return 0.0;
    double position[3] = {world[12], world[13], world[14]};
    if (center.x != 0.0f || center.y != 0.0f || center.z != 0.0f) {
        const float local[3] = {center.x, center.y, center.z};
        for (uint32_t row = 0; row < 3; row++) {
            for (uint32_t column = 0; column < 3; column++) {
                const float scale = world[column * 4u + row];
                if (!tide_is_finite_f(scale)) return 0.0;
                position[row] += (double)scale * local[column];
            }
        }
    }
    // Binary32 matrix, center and view inputs cannot overflow double even
    // through the transformed point and final dot product.
    return (position[0] - eye.x) * forward.x + (position[1] - eye.y) * forward.y
        + (position[2] - eye.z) * forward.z;
}

static bool compatible(const tide_draw_list *d, const tide_draw_command *a, const tide_draw_run *x,
                       const tide_draw_command *b, const tide_draw_run *y, const uint32_t size)
{
    const uint32_t uniform_size = tide_shader_uniform_size(y->shader);
    return a->blend == b->blend && a->depth == b->depth && a->cull == b->cull
        && (x->shader == y->shader || (x->shader->id && x->shader->id == y->shader->id))
        && tide_shader_instance_size(x->shader) == size && x->mesh == y->mesh && x->sampling == y->sampling
        && x->first + (uint64_t)x->count * size == y->first
        && memcmp(x->textures, y->textures, sizeof x->textures) == 0 && memcmp(&x->tint, &y->tint, sizeof x->tint) == 0
        && x->moved == y->moved && (!x->moved || memcmp(&x->transform, &y->transform, sizeof x->transform) == 0)
        && (uniform_size == 0 || x->uniforms == y->uniforms
            || memcmp(d->uniforms + x->uniforms, d->uniforms + y->uniforms, uniform_size) == 0);
}

void SortTransparent(tide_draw_list *d, const int32_t first, const tide_float3 eye, const tide_float3 forward)
{
    if (!d || first < 0 || (uint32_t)first >= d->count
        || !tide_is_finite_f(eye.x) || !tide_is_finite_f(eye.y) || !tide_is_finite_f(eye.z)
        || !tide_is_finite_f(forward.x) || !tide_is_finite_f(forward.y) || !tide_is_finite_f(forward.z)) return;
    uint64_t extra = 0, most_packets = 0, most_bytes = 0;
    uint64_t packets = 0, bytes = 0;
    for (uint32_t i = (uint32_t)first; i < d->count; i++) {
        const uint32_t size = sortable(d, &d->commands[i]);
        if (size) {
            const uint32_t count = d->runs[d->commands[i].shaded.run].count;
            extra += count - 1u;
            packets += count;
            bytes += (uint64_t)count * size;
            if (packets > most_packets) most_packets = packets;
            if (bytes > most_bytes) most_bytes = bytes;
        } else packets = bytes = 0;
    }
    if (most_packets < 2u) return;
    const uint64_t capacity = d->count + extra;
    if (capacity > UINT32_MAX || most_packets > UINT32_MAX) tide_out_of_memory();
    transparent_packet *ordered = tide_alloc(room(most_packets, sizeof *ordered));
    transparent_packet *scratch = tide_alloc(room(most_packets, sizeof *scratch));
    uint8_t *reordered = tide_alloc(room(most_bytes, 1));
    tide_draw_command *commands = tide_alloc(room(capacity, sizeof *commands));
    tide_draw_run *runs = tide_alloc(room(capacity, sizeof *runs));
    uint32_t count = 0, run_count = 0, plain_open = 0;
    for (uint32_t i = 0; i < d->count;) {
        const tide_draw_command *c = &d->commands[i];
        if (i < (uint32_t)first || !sortable(d, c)) {
            if (d->plain_open == i + 1u) plain_open = count + 1u;
            commands[count] = *c;
            if ((c->kind == TIDE_DRAW_SHADED || c->kind == TIDE_DRAW_BLIT) && c->shaded.run < d->run_count) {
                runs[run_count] = d->runs[c->shaded.run];
                commands[count].shaded.run = run_count++;
            }
            count++;
            i++;
            continue;
        }
        const uint32_t start = count, byte_first = d->runs[c->shaded.run].first;
        uint64_t byte_end = byte_first;
        uint32_t n = 0;
        bool contiguous = true;
        // State commands and other drawing are barriers. The caller records
        // only this transparent pass between mark/sort, with constant cameras,
        // clip and destination. Initial lazy state commands stay in place.
        while (i < d->count) {
            const uint32_t size = sortable(d, &d->commands[i]);
            if (!size) break;
            const tide_draw_run *r = &d->runs[d->commands[i].shaded.run];
            const tide_float3 center = center_of(d, r);
            if (r->first != byte_end) contiguous = false;
            byte_end += (uint64_t)r->count * size;
            for (uint32_t k = 0; k < r->count; k++) {
                const uint32_t offset = r->first + k * size;
                ordered[n++] = (transparent_packet){.depth = depth_of(d->instances + offset, center, eye, forward),
                                                     .command = i, .first = offset};
            }
            i++;
        }
        sort_packets(ordered, scratch, n);
        uint32_t used = 0;
        for (uint32_t k = 0; k < n; k++) {
            const transparent_packet *packet = &ordered[k];
            const tide_draw_command *original = &d->commands[packet->command];
            tide_draw_run run = d->runs[original->shaded.run];
            const uint32_t size = tide_shader_instance_size(run.shader);
            run.first = contiguous ? byte_first + used : packet->first;
            run.count = 1;
            if (contiguous) {
                memcpy(reordered + used, d->instances + packet->first, size);
                used += size;
            }
            if (count > start && compatible(d, &commands[count - 1u], &runs[run_count - 1u], original, &run, size)) {
                runs[run_count - 1u].count++;
            } else {
                commands[count] = *original;
                commands[count++].shaded.run = run_count;
                runs[run_count++] = run;
            }
        }
        // Normal recording owns a consecutive byte span. Reuse that span so
        // repeated sorting neither grows capture memory nor leaves old copies.
        // Nonconsecutive public packets keep their existing checked offsets.
        if (contiguous) memcpy(d->instances + byte_first, reordered, used);
    }
    free(d->commands);
    free(d->runs);
    d->commands = commands;
    d->count = count;
    d->capacity = (uint32_t)capacity;
    d->runs = runs;
    d->run_count = run_count;
    d->run_capacity = (uint32_t)capacity;
    d->plain_open = plain_open;
    free(reordered);
    free(scratch);
    free(ordered);
}
