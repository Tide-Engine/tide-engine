#include "direct.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct render3d_direct {
    tide_draw_list frame;
    tide_draw_camera_3d_data *cameras; // Original parts, one per recorded command.
    uint8_t **blocks;                // Immutable compressed pack bytes we own.
    uint64_t digest;
};

static char direct_error[256];

static bool fail(const char *message)
{
    (void)snprintf(direct_error, sizeof direct_error, "%s", message);
    return false;
}

static bool fail_at(const char *message, const uint32_t at)
{
    (void)snprintf(direct_error, sizeof direct_error, "%s (command %u)", message, (unsigned)at);
    return false;
}

const char *render3d_direct_error(void)
{
    return direct_error;
}

static bool bytes_equal(const void *a, const void *b, const size_t count)
{
    return !count || (a && b && memcmp(a, b, count) == 0);
}

// Explicit word encoding avoids struct padding and host-sized registry fields.
static uint64_t hash_bytes(uint64_t h, const void *data, const size_t count)
{
    const uint8_t *p = data;
    for (size_t i = 0; i < count; i++) h = (h ^ p[i]) * UINT64_C(1099511628211);
    return h;
}

static uint64_t hash_u32(uint64_t h, const uint32_t value)
{
    for (uint32_t shift = 0; shift < 32u; shift += 8u) {
        const uint8_t b = (uint8_t)(value >> shift);
        h = hash_bytes(h, &b, 1);
    }
    return h;
}

static uint64_t hash_u64(uint64_t h, const uint64_t value)
{
    h = hash_u32(h, (uint32_t)value);
    return hash_u32(h, (uint32_t)(value >> 32));
}

static uint64_t hash_float(const uint64_t h, const float value)
{
    return hash_u32(h, tide_draw_bits(value));
}

static uint64_t hash_size(uint64_t h, const tide_target_size size)
{
    h = hash_float(h, size.width);
    h = hash_float(h, size.height);
    h = hash_float(h, size.scale);
    return hash_float(h, size.window);
}

static bool size_equal(const tide_target_size a, const tide_target_size b)
{
    return tide_draw_bits(a.width) == tide_draw_bits(b.width)
        && tide_draw_bits(a.height) == tide_draw_bits(b.height)
        && tide_draw_bits(a.scale) == tide_draw_bits(b.scale)
        && tide_draw_bits(a.window) == tide_draw_bits(b.window);
}

static float pixel_scale(const float scale)
{
    return scale == 0.0f ? 1.0f : scale;
}

static uint64_t hash_screen(uint64_t h, const tide_draw_list *d)
{
    h = hash_float(h, d->screen.x);
    h = hash_float(h, d->screen.y);
    h = hash_float(h, pixel_scale(d->screen_scale.x));
    return hash_float(h, pixel_scale(d->screen_scale.y));
}

static bool screen_equal(const tide_draw_list *a, const tide_draw_list *b)
{
    return bytes_equal(&a->screen, &b->screen, sizeof a->screen)
        && tide_draw_bits(pixel_scale(a->screen_scale.x)) == tide_draw_bits(pixel_scale(b->screen_scale.x))
        && tide_draw_bits(pixel_scale(a->screen_scale.y)) == tide_draw_bits(pixel_scale(b->screen_scale.y));
}

static bool camera2_equal(const tide_draw_camera_data a, const tide_draw_camera_data b)
{
    if ((a.screen != 0) != (b.screen != 0)) return false;
    return a.screen || (bytes_equal(&a.center, &b.center, sizeof a.center)
                        && tide_draw_bits(a.size) == tide_draw_bits(b.size));
}

static uint64_t hash_camera2(uint64_t h, const tide_draw_camera_data camera)
{
    h = hash_u32(h, camera.screen != 0);
    if (camera.screen) return h;
    h = hash_float(h, camera.center.x);
    h = hash_float(h, camera.center.y);
    return hash_float(h, camera.size);
}

static bool texture_valid(const tide_draw_texture *t)
{
    if (t->space == TIDE_PIXELS_TARGET || t->space == TIDE_PIXELS_TARGET_DEPTH) {
        return t->id && tide_target_size_valid(t->target) ? true : fail("Invalid captured target texture");
    }
    if (t->space != TIDE_PIXELS_MEMORY && t->space != TIDE_PIXELS_GRID && t->space != TIDE_PIXELS_PACK
        && t->space != TIDE_PIXELS_LIST) return fail("Unsupported texture space (cube textures are not Render3D inputs)");
    if (t->format > TIDE_TEXELS_INTS || t->width <= 0 || t->height <= 0 || !t->pixels) {
        return fail("Invalid captured CPU texture");
    }
    const uint64_t bytes = (uint64_t)t->width * (uint64_t)t->height * tide_texels_bytes(t->format);
    if (bytes > UINT32_MAX) return fail("Captured texture is too large");
    if (t->space == TIDE_PIXELS_PACK && t->format != TIDE_TEXELS_BYTES) return fail("Unsupported pack texture format");
    if (t->compressed.bytes) {
        uint32_t levels, count;
        if (t->space != TIDE_PIXELS_PACK || !t->compressed.pixels
            || !tide_texture_blocks_size(t->width, t->height, t->compressed.format, &levels, &count)
            || levels != t->compressed.levels || count != t->compressed.bytes) {
            return fail("Invalid captured compressed pack texture");
        }
    } else if (t->compressed.pixels || t->compressed.levels || t->compressed.format) {
        return fail("Incomplete compressed texture descriptor");
    }
    return true;
}

static uint64_t hash_texture(uint64_t h, const tide_draw_texture *t)
{
    h = hash_u32(h, t != NULL);
    if (!t) return h;
    h = hash_u32(h, t->space);
    h = hash_u64(h, t->id);
    h = hash_u64(h, t->version);
    h = hash_u32(h, (uint32_t)t->width);
    h = hash_u32(h, (uint32_t)t->height);
    h = hash_u32(h, t->format);
    h = hash_size(h, t->target);
    if (t->pixels) {
        h = hash_bytes(h, t->pixels, (size_t)t->width * (size_t)t->height * tide_texels_bytes(t->format));
    }
    h = hash_u32(h, t->compressed.format);
    h = hash_u32(h, t->compressed.levels);
    h = hash_u32(h, t->compressed.bytes);
    return hash_bytes(h, t->compressed.pixels, t->compressed.bytes);
}

static bool texture_equal(const tide_draw_texture *a, const tide_draw_texture *b)
{
    if (!a || !b) return a == b;
    if (a->space != b->space || a->id != b->id || a->version != b->version || a->width != b->width
        || a->height != b->height || a->format != b->format || !size_equal(a->target, b->target)
        || a->compressed.format != b->compressed.format || a->compressed.levels != b->compressed.levels
        || a->compressed.bytes != b->compressed.bytes) return false;
    if (a->space != TIDE_PIXELS_TARGET && a->space != TIDE_PIXELS_TARGET_DEPTH
        && !bytes_equal(a->pixels, b->pixels, (size_t)a->width * (size_t)a->height * tide_texels_bytes(a->format))) {
        return false;
    }
    return bytes_equal(a->compressed.pixels, b->compressed.pixels, a->compressed.bytes);
}

typedef struct direct_state {
    tide_draw_camera_data camera2;
    tide_draw_camera_3d_data camera3;
    uint32_t clip, stencil;
} direct_state;

typedef struct direct_iterator {
    const tide_draw_list *draw;
    direct_state *states;
    uint32_t target, at, item;
    bool failed;
} direct_iterator;

typedef struct direct_event {
    const tide_draw_list *draw;
    const tide_draw_command *command;
    const tide_draw_run *run;
    const tide_draw_mesh_data *mesh;
    const tide_draw_target_data *target;
    const uint8_t *instance;
    direct_state state;
} direct_event;

static bool iterator_start(direct_iterator *it, const tide_draw_list *d)
{
    *it = (direct_iterator){.draw = d};
    if (!d || (d->count && !d->commands) || d->target_count == UINT32_MAX) return fail("Invalid draw list");
    it->states = calloc((size_t)d->target_count + 1u, sizeof *it->states);
    if (!it->states) return fail("Out of memory for draw comparison");
    const tide_draw_camera_3d_data first = tide_draw_first_camera_3d();
    for (uint32_t i = 0; i <= d->target_count; i++) it->states[i].camera3 = first;
    return true;
}

static bool clip_valid(const tide_draw_list *d, const uint32_t slot)
{
    if (!slot || slot > d->clip_count) return fail("Invalid captured clip slot");
    const tide_draw_clip_data *clip = &d->clips[slot - 1u];
    if (clip->parent) return fail("Nested clips are outside this Render3D baseline");
    return clip->camera <= d->camera_count ? true : fail("Invalid clip camera");
}

static bool replay_cull_matches(const tide_draw_list *d, const tide_draw_command *c, const tide_draw_run *r)
{
    if (c->kind != TIDE_DRAW_SHADED || r->shader->dims != 3) return true;
    const bool expected = (c->cull & TIDE_DRAW_MIRRORED) != 0;
    if ((c->cull & 3u) == TIDE_CULL_NONE || r->shader->instance_floats < 4u) {
        return !expected || fail("Recorded mirror state cannot be reproduced by public shaded recording");
    }
    const uint32_t size = tide_shader_instance_size(r->shader);
    for (uint32_t i = 0; i < r->count; i++) {
        tide_float4x4 m;
        memcpy(&m, d->instances + r->first + (size_t)i * size, sizeof m);
        // Match draw.c's exact determinant expression. Original recording may
        // classify an enclosing transform separately; singular/rounded composite
        // transforms can lose that sign. Reject those snapshots outside timing.
        const bool mirrored = m.c0.x * (m.c1.y * m.c2.z - m.c1.z * m.c2.y)
            + m.c0.y * (m.c1.z * m.c2.x - m.c1.x * m.c2.z)
            + m.c0.z * (m.c1.x * m.c2.y - m.c1.y * m.c2.x) < 0.0f;
        if (mirrored != expected) return fail("Recorded mirror state differs from the captured composite transform");
    }
    return true;
}

static bool run_valid(const tide_draw_list *d, const tide_draw_command *c)
{
    if (c->shaded.run >= d->run_count) return fail("Invalid shaded run slot");
    const tide_draw_run *r = &d->runs[c->shaded.run];
    const tide_shader *s = r->shader;
    if (!s || !r->mesh || r->mesh > d->mesh_count || !r->count) return fail("Invalid shaded run");
    const tide_draw_mesh_data *m = &d->meshes[r->mesh - 1u];
    if (m->space != TIDE_MESH_BYTES || m->base || m->level || m->levels) return fail("Only static byte meshes without LOD are supported");
    if (s->dims != 2 && s->dims != 3) return fail("Unsupported shader dimensions");
    if (s->texture_count > TIDE_SHADER_TEXTURES || s->vertex_size != m->vertex_size
        || !m->vertex_size || !m->vertex_count || !m->index_count || !m->bytes || !m->indices
        || (uint64_t)m->vertex_size * m->vertex_count > UINT32_MAX) return fail("Invalid shaded mesh");
    if (((uint64_t)s->instance_floats + s->instance_ints) * 16u > UINT32_MAX
        || ((uint64_t)s->uniform_floats + s->uniform_ints) * 16u > UINT32_MAX) return fail("Invalid shader byte counts");
    const uint64_t instances = (uint64_t)tide_shader_instance_size(s) * r->count;
    const uint32_t uniforms = tide_shader_uniform_size(s);
    if ((uint64_t)r->first + instances > d->instance_used || (uint64_t)r->uniforms + uniforms > d->uniform_used
        || (instances && !d->instances) || (uniforms && !d->uniforms)) return fail("Invalid shader data range");
    if (r->moved > 1u || (r->moved && s->dims != 2)) return fail("Unsupported recorded shader transform");
    if (c->blend > TIDE_BLEND_REPLACE || c->depth > TIDE_DEPTH_NONE || (c->cull & ~7u)
        || (c->cull & 3u) > TIDE_CULL_NONE) return fail("Unsupported draw state");
    const tide_color white = TIDE_COLOR_WHITE;
    if (c->kind == TIDE_DRAW_BLIT && (s->dims != 2 || r->count != 1u || r->moved
        || !bytes_equal(&r->tint, &white, sizeof r->tint))) return fail("Unsupported blit run");
    for (uint32_t i = 0; i < s->texture_count; i++) {
        if (r->textures[i] > d->texture_count) return fail("Invalid shader texture slot");
        if (r->textures[i] && !texture_valid(&d->textures[r->textures[i] - 1u])) return false;
    }
    return replay_cull_matches(d, c, r);
}

// Expand each shaded batch to one event per instance. State-only commands
// change the appropriate destination, but never contribute noise to equality.
static bool iterator_next(direct_iterator *it, direct_event *out)
{
    const tide_draw_list *d = it->draw;
    while (it->at < d->count) {
        const tide_draw_command *c = &d->commands[it->at];
        direct_state *state = &it->states[it->target];
        switch (c->kind) {
        case TIDE_DRAW_TARGET:
            if (c->target > d->target_count) { fail("Invalid target selector"); goto invalid; }
            it->target = c->target;
            if (it->target) {
                const tide_draw_target_data *t = &d->targets[it->target - 1u];
                if (!t->id || !tide_target_size_valid(t->size)) { fail("Invalid captured destination"); goto invalid; }
            }
            break;
        case TIDE_DRAW_CAMERA:
            state->camera2 = (tide_draw_camera_data){.center = c->a, .size = c->b.x};
            break;
        case TIDE_DRAW_SCREEN:
            state->camera2 = (tide_draw_camera_data){.screen = 1};
            break;
        case TIDE_DRAW_CAMERA_3D:
            if (c->camera.matrix == TIDE_DRAW_FIRST_CAMERA) state->camera3 = tide_draw_first_camera_3d();
            else {
                if (c->camera.matrix >= d->matrix_count || c->camera.fit > 1u || c->camera.mirrored > 1u) {
                    fail("Invalid 3D camera command"); goto invalid;
                }
                state->camera3 = (tide_draw_camera_3d_data){.matrix = d->matrices[c->camera.matrix],
                    .fit = c->camera.fit, .mirrored = c->camera.mirrored};
            }
            break;
        case TIDE_DRAW_CLIP:
            if (!clip_valid(d, c->clip)) goto invalid;
            state->clip = c->clip;
            break;
        case TIDE_DRAW_NO_CLIP:
            state->clip = 0;
            break;
        case TIDE_DRAW_STENCIL:
            state->stencil = tide_stencil_packed(tide_stencil_unpacked(c->stencil));
            break;
        case TIDE_DRAW_CLEAR:
            if (c->a.x != 0.0f && c->a.x != 1.0f) { fail("Unsupported clear mode"); goto invalid; }
            *out = (direct_event){.draw = d, .command = c, .state = *state,
                .target = it->target ? &d->targets[it->target - 1u] : NULL};
            it->at++;
            return true;
        case TIDE_DRAW_SHADED:
        case TIDE_DRAW_BLIT: {
            if (!it->item && !run_valid(d, c)) goto invalid;
            const tide_draw_run *r = &d->runs[c->shaded.run];
            const uint32_t size = tide_shader_instance_size(r->shader);
            *out = (direct_event){.draw = d, .command = c, .run = r, .mesh = &d->meshes[r->mesh - 1u],
                .target = it->target ? &d->targets[it->target - 1u] : NULL, .state = *state,
                .instance = size ? d->instances + r->first + (size_t)it->item * size : NULL};
            if (++it->item == r->count) it->item = 0, it->at++;
            return true;
        }
        default:
            (void)snprintf(direct_error, sizeof direct_error, "Unsupported Render3D command kind %u (command %u)",
                           (unsigned)c->kind, (unsigned)it->at);
            goto invalid;
        }
        it->at++;
    }
    return false;
invalid:
    it->failed = true;
    return false;
}

static tide_draw_camera_data clip_camera(const tide_draw_list *d, const tide_draw_clip_data *clip)
{
    return clip->camera ? d->cameras[clip->camera - 1u] : (tide_draw_camera_data){0};
}

static uint64_t hash_clip(uint64_t h, const direct_event *e)
{
    h = hash_u32(h, e->state.clip != 0);
    if (!e->state.clip) return h;
    const tide_draw_clip_data *c = &e->draw->clips[e->state.clip - 1u];
    h = hash_float(h, c->rect.x);
    h = hash_float(h, c->rect.y);
    h = hash_float(h, c->rect.width);
    h = hash_float(h, c->rect.height);
    return hash_camera2(h, clip_camera(e->draw, c));
}

static bool clip_equal(const direct_event *a, const direct_event *b)
{
    if (!a->state.clip || !b->state.clip) return !a->state.clip && !b->state.clip;
    const tide_draw_clip_data *ca = &a->draw->clips[a->state.clip - 1u];
    const tide_draw_clip_data *cb = &b->draw->clips[b->state.clip - 1u];
    return bytes_equal(&ca->rect, &cb->rect, sizeof ca->rect)
        && camera2_equal(clip_camera(a->draw, ca), clip_camera(b->draw, cb));
}

static const tide_draw_texture *event_texture(const direct_event *e, const uint32_t slot)
{
    return e->run->textures[slot] ? &e->draw->textures[e->run->textures[slot] - 1u] : NULL;
}

static uint64_t hash_common(const direct_event *e)
{
    uint64_t h = hash_u32(UINT64_C(14695981039346656037), e->command->kind);
    h = hash_u32(h, e->target != NULL);
    if (e->target) {
        h = hash_u64(h, e->target->id);
        h = hash_u64(h, e->target->version);
        h = hash_size(h, e->target->size);
    }
    if (e->command->kind != TIDE_DRAW_BLIT) h = hash_clip(h, e);
    if (!e->run) {
        h = hash_u32(h, e->command->a.x != 0.0f);
        return e->command->a.x != 0.0f ? h : hash_bytes(h, &e->command->color, sizeof e->command->color);
    }
    const tide_draw_run *r = e->run;
    const tide_shader *s = r->shader;
    h = hash_u32(h, e->command->blend);
    h = hash_u64(h, s->id);
    h = hash_u32(h, s->dims);
    h = hash_u32(h, tide_shader_instance_size(s));
    h = hash_u32(h, tide_shader_uniform_size(s));
    h = hash_u32(h, s->texture_count);
    if (e->command->kind != TIDE_DRAW_BLIT) {
        h = hash_u32(h, e->state.stencil);
        if (s->dims == 3) {
            h = hash_bytes(h, &e->state.camera3.matrix, sizeof e->state.camera3.matrix);
            h = hash_u32(h, e->state.camera3.fit);
            h = hash_u32(h, e->state.camera3.mirrored);
            h = hash_u32(h, e->command->depth);
            h = hash_u32(h, e->command->cull);
        } else {
            h = hash_camera2(h, e->state.camera2);
            h = hash_u32(h, r->moved != 0);
            if (r->moved) h = hash_bytes(h, &r->transform, sizeof r->transform);
        }
        h = hash_bytes(h, &r->tint, sizeof r->tint);
    }
    h = hash_u32(h, r->sampling);
    h = hash_u32(h, e->mesh->vertex_size);
    h = hash_u32(h, e->mesh->vertex_count);
    h = hash_u32(h, e->mesh->index_count);
    h = hash_bytes(h, e->mesh->bytes, (size_t)e->mesh->vertex_size * e->mesh->vertex_count);
    h = hash_bytes(h, e->mesh->indices, (size_t)e->mesh->index_count * sizeof(uint32_t));
    const uint32_t uniforms = tide_shader_uniform_size(s);
    if (uniforms) h = hash_bytes(h, e->draw->uniforms + r->uniforms, uniforms);
    for (uint32_t i = 0; i < s->texture_count; i++) h = hash_texture(h, event_texture(e, i));
    return h;
}

static bool common_equal(const direct_event *a, const direct_event *b)
{
    if (a->command->kind != b->command->kind || (a->target != NULL) != (b->target != NULL)) return false;
    if (a->target && (a->target->id != b->target->id || a->target->version != b->target->version
        || !size_equal(a->target->size, b->target->size))) return false;
    if (a->command->kind != TIDE_DRAW_BLIT && !clip_equal(a, b)) return false;
    if (!a->run) {
        if ((a->command->a.x != 0.0f) != (b->command->a.x != 0.0f)) return false;
        return a->command->a.x != 0.0f || bytes_equal(&a->command->color, &b->command->color, sizeof a->command->color);
    }
    const tide_shader *sa = a->run->shader, *sb = b->run->shader;
    if (a->command->blend != b->command->blend || sa->id != sb->id || sa->dims != sb->dims
        || tide_shader_instance_size(sa) != tide_shader_instance_size(sb)
        || tide_shader_uniform_size(sa) != tide_shader_uniform_size(sb) || sa->texture_count != sb->texture_count) return false;
    if (a->command->kind != TIDE_DRAW_BLIT) {
        if (a->state.stencil != b->state.stencil || !bytes_equal(&a->run->tint, &b->run->tint, sizeof a->run->tint)) return false;
        if (sa->dims == 3) {
            if (!bytes_equal(&a->state.camera3.matrix, &b->state.camera3.matrix, sizeof a->state.camera3.matrix)
                || a->state.camera3.fit != b->state.camera3.fit || a->state.camera3.mirrored != b->state.camera3.mirrored
                || a->command->depth != b->command->depth || a->command->cull != b->command->cull) return false;
        } else if (!camera2_equal(a->state.camera2, b->state.camera2) || a->run->moved != b->run->moved
            || (a->run->moved && !bytes_equal(&a->run->transform, &b->run->transform, sizeof a->run->transform))) return false;
    }
    const tide_draw_mesh_data *ma = a->mesh, *mb = b->mesh;
    if (a->run->sampling != b->run->sampling || ma->vertex_size != mb->vertex_size || ma->vertex_count != mb->vertex_count
        || ma->index_count != mb->index_count || !bytes_equal(ma->bytes, mb->bytes, (size_t)ma->vertex_size * ma->vertex_count)
        || !bytes_equal(ma->indices, mb->indices, (size_t)ma->index_count * sizeof(uint32_t))) return false;
    const uint32_t uniforms = tide_shader_uniform_size(sa);
    if (uniforms && !bytes_equal(a->draw->uniforms + a->run->uniforms, b->draw->uniforms + b->run->uniforms, uniforms)) return false;
    for (uint32_t i = 0; i < sa->texture_count; i++) {
        if (!texture_equal(event_texture(a, i), event_texture(b, i))) return false;
    }
    return true;
}

static bool frame_digest(const tide_draw_list *d, uint64_t *digest)
{
    direct_iterator it;
    if (!iterator_start(&it, d)) return false;
    uint64_t h = hash_screen(UINT64_C(14695981039346656037), d), common = 0, count = 0;
    const tide_draw_command *last = NULL;
    direct_event event;
    while (iterator_next(&it, &event)) {
        if (event.command != last) common = hash_common(&event), last = event.command;
        h = hash_u64(h, common);
        if (event.run) h = hash_bytes(h, event.instance, tide_shader_instance_size(event.run->shader));
        count++;
    }
    free(it.states);
    if (it.failed) return false;
    *digest = hash_u64(h, count);
    return true;
}

bool render3d_direct_equal(const render3d_direct *cache, const tide_draw_list *draw)
{
    direct_error[0] = '\0';
    if (!cache || !draw) return fail("Missing direct cache or draw list");
    if (!screen_equal(&cache->frame, draw)) return fail("Draw screen metadata differs");
    direct_iterator a, b;
    if (!iterator_start(&a, &cache->frame)) return false;
    if (!iterator_start(&b, draw)) { free(a.states); return false; }
    const tide_draw_command *last_a = NULL, *last_b = NULL;
    direct_event ea, eb;
    uint64_t item = 0;
    bool equal = true;
    for (;;) {
        const bool has_a = iterator_next(&a, &ea), has_b = iterator_next(&b, &eb);
        if (a.failed || b.failed) { equal = false; break; }
        if (!has_a || !has_b) {
            if (has_a != has_b) equal = fail("Expanded draw count differs");
            break;
        }
        if ((ea.command != last_a || eb.command != last_b) && !common_equal(&ea, &eb)) {
            (void)snprintf(direct_error, sizeof direct_error, "Expanded draw state/resource differs at item %llu (commands %u/%u)",
                           (unsigned long long)item, (unsigned)(ea.command - cache->frame.commands),
                           (unsigned)(eb.command - draw->commands));
            equal = false;
            break;
        }
        if (ea.run && !bytes_equal(ea.instance, eb.instance, tide_shader_instance_size(ea.run->shader))) {
            (void)snprintf(direct_error, sizeof direct_error, "Instance bytes differ at expanded item %llu", (unsigned long long)item);
            equal = false;
            break;
        }
        last_a = ea.command, last_b = eb.command;
        item++;
    }
    free(a.states);
    free(b.states);
    return equal;
}

render3d_direct *render3d_direct_capture(const tide_draw_list *source)
{
    direct_error[0] = '\0';
    if (!source) { fail("Missing capture source"); return NULL; }
    // Complete deferred texture data before inspecting/copying it; outside timing.
    tide_draw_prepare(source, NULL);
    uint64_t expected;
    if (!frame_digest(source, &expected)) return NULL;
    render3d_direct *cache = calloc(1, sizeof *cache);
    if (!cache) { fail("Out of memory for direct cache"); return NULL; }
    cache->frame.screen = source->screen;
    cache->frame.screen_scale = source->screen_scale;
    cache->frame.target_pixels = source->target_pixels;
    tide_draw_append(&cache->frame, source);
    const tide_draw_list *d = &cache->frame;
    cache->cameras = calloc(d->count ? d->count : 1u, sizeof *cache->cameras);
    cache->blocks = calloc(d->texture_count ? d->texture_count : 1u, sizeof *cache->blocks);
    if (!cache->cameras || !cache->blocks) { fail("Out of memory for direct capture metadata"); goto invalid; }
    for (uint32_t i = 0; i < d->texture_count; i++) {
        tide_draw_texture *t = &cache->frame.textures[i];
        if (!texture_valid(t)) goto invalid;
        // append deliberately borrows pack resources. This immutable snapshot owns
        // them instead, including compression, so source reset/reload cannot edit it.
        if (t->space == TIDE_PIXELS_PACK) {
            const size_t bytes = (size_t)t->width * (size_t)t->height * tide_texels_bytes(t->format);
            uint8_t *pixels = malloc(bytes);
            if (!pixels) { fail("Out of memory for pack pixels"); goto invalid; }
            memcpy(pixels, t->pixels, bytes);
            t->pixels = pixels;
            t->capacity = (uint32_t)bytes;
        }
        if (t->compressed.bytes) {
            cache->blocks[i] = malloc(t->compressed.bytes);
            if (!cache->blocks[i]) { fail("Out of memory for compressed pixels"); goto invalid; }
            memcpy(cache->blocks[i], t->compressed.pixels, t->compressed.bytes);
            t->compressed.pixels = cache->blocks[i];
        }
    }
    for (uint32_t i = 0; i < d->count; i++) {
        const tide_draw_command *c = &d->commands[i];
        if (c->kind != TIDE_DRAW_CAMERA_3D || c->camera.matrix == TIDE_DRAW_FIRST_CAMERA) continue;
        if (c->camera.fit) { fail_at("Only explicit unfitted Render3D matrix cameras are supported", i); goto invalid; }
        const tide_draw_camera_3d_data *found = NULL;
        for (uint32_t j = 0; j < source->camera_3d_count; j++) {
            const tide_draw_camera_3d_data *candidate = &source->cameras_3d[j];
            if (candidate->fit == c->camera.fit && candidate->mirrored == c->camera.mirrored
                && bytes_equal(&candidate->matrix, &d->matrices[c->camera.matrix], sizeof candidate->matrix)) {
                found = candidate;
                break;
            }
        }
        if (!found) { fail_at("Camera matrix has no original transform/projection record", i); goto invalid; }
        cache->cameras[i] = *found;
    }
    if (!frame_digest(d, &cache->digest)) goto invalid;
    if (cache->digest != expected) { fail("Capture changed the expanded frame digest"); goto invalid; }
    if (!render3d_direct_equal(cache, source)) goto invalid;
    return cache;
invalid:
    render3d_direct_free(cache);
    return NULL;
}

static uint32_t record_texture(tide_draw_list *d, const tide_draw_texture *t)
{
    if (t->space == TIDE_PIXELS_TARGET || t->space == TIDE_PIXELS_TARGET_DEPTH) {
        if (tide_render_target_version(t->id) != t->version) { fail("Captured texture target was resized or disposed"); return 0; }
        return t->space == TIDE_PIXELS_TARGET ? tide_draw_target_texture_sized(d, t->id, t->target)
                                             : tide_draw_target_depth_sized(d, t->id, t->target);
    }
    uint8_t *pixels = NULL;
    const uint32_t slot = tide_draw_texture_slot_as(d, t->space, t->id, t->version, t->width, t->height, t->format, &pixels);
    if (!slot) { fail("CPU texture registration failed"); return 0; }
    tide_draw_texture *out = &d->textures[slot - 1u];
    if (t->space == TIDE_PIXELS_PACK) {
        // Public registration has no immutable-pack pointer setter. Keep the
        // pack convention (capacity 0), including when package/direct alternate:
        // generated pack recording may replace pixels without clearing capacity.
        if (out->capacity) free(out->pixels);
        out->pixels = t->pixels;
        out->capacity = 0;
    } else if (pixels) {
        memcpy(pixels, t->pixels, (size_t)t->width * (size_t)t->height * tide_texels_bytes(t->format));
    }
    // Compression is immutable metadata with no public setter. The cache owns it.
    out->compressed = t->compressed;
    return slot;
}

static void record_clip(tide_draw_list *d, const tide_draw_list *source, const uint32_t slot)
{
    const tide_draw_clip_data *clip = &source->clips[slot - 1u];
    const tide_draw_camera_data camera = clip_camera(source, clip);
    tide_draw_unset_transforms(d);
    const uint32_t mark = camera.screen ? tide_draw_push_screen(d) : tide_draw_push_camera(d, camera.center, camera.size);
    tide_draw_set_clip(d, clip->rect);
    tide_draw_pop(d, mark);
}

bool render3d_direct_record(const render3d_direct *cache, tide_draw_list *draw)
{
    direct_error[0] = '\0';
    if (!cache || !draw) return fail("Missing direct cache or destination");
    if (draw == &cache->frame || draw->count || draw->saved_count) return fail("Reset the destination before direct recording");
    const tide_draw_list *d = &cache->frame;
    // Host metadata has no command/setter and is not part of replayed commands.
    tide_draw_screen_size(draw, d->screen);
    draw->screen_scale = d->screen_scale;
    draw->target_pixels = d->target_pixels;
    tide_draw_defaults(draw);
    for (uint32_t i = 0; i < d->count; i++) {
        const tide_draw_command *c = &d->commands[i];
        switch (c->kind) {
        case TIDE_DRAW_TARGET:
            if (!c->target) tide_draw_to_sized(draw, 0, (tide_target_size){0});
            else {
                const tide_draw_target_data *target = &d->targets[c->target - 1u];
                if (!tide_render_target_live(target->id) || tide_render_target_version(target->id) != target->version) {
                    return fail_at("Captured render target was resized or disposed", i);
                }
                tide_draw_to_sized(draw, target->id, target->size);
            }
            break;
        case TIDE_DRAW_CLEAR:
            if (c->a.x != 0.0f) tide_draw_clear_depth(draw);
            else tide_draw_clear(draw, c->color);
            break;
        case TIDE_DRAW_CAMERA:
            tide_draw_set_camera(draw, c->a, c->b.x);
            break;
        case TIDE_DRAW_SCREEN:
            tide_draw_set_screen(draw);
            break;
        case TIDE_DRAW_CAMERA_3D:
            if (c->camera.matrix == TIDE_DRAW_FIRST_CAMERA) tide_draw_unset_camera_3d(draw);
            else tide_draw_set_camera_matrices(draw, cache->cameras[i].transform, cache->cameras[i].projection);
            break;
        case TIDE_DRAW_CLIP:
            record_clip(draw, d, c->clip);
            break;
        case TIDE_DRAW_NO_CLIP:
            tide_draw_unset_clip(draw);
            break;
        case TIDE_DRAW_STENCIL:
            tide_draw_set_stencil(draw, tide_stencil_unpacked(c->stencil));
            break;
        case TIDE_DRAW_SHADED:
        case TIDE_DRAW_BLIT: {
            const tide_draw_run *r = &d->runs[c->shaded.run];
            uint32_t textures[TIDE_SHADER_TEXTURES] = {0};
            for (uint32_t j = 0; j < r->shader->texture_count; j++) {
                if (!r->textures[j]) continue;
                textures[j] = record_texture(draw, &d->textures[r->textures[j] - 1u]);
                if (!textures[j]) return false;
            }
            const void *instances = tide_shader_instance_size(r->shader) ? d->instances + r->first : NULL;
            const void *uniforms = tide_shader_uniform_size(r->shader) ? d->uniforms + r->uniforms : NULL;
            if (c->kind == TIDE_DRAW_BLIT) {
                tide_draw_blit(draw, r->shader, (tide_blend)c->blend, instances, uniforms, textures, r->sampling);
                break;
            }
            const tide_draw_mesh_data *mesh = &d->meshes[r->mesh - 1u];
            bool fill;
            const uint32_t slot = tide_draw_mesh_slot(draw, mesh->space, mesh->id, mesh->other, mesh->version, &fill);
            if (!slot) return fail_at("Mesh registration failed", i);
            if (fill) tide_draw_mesh_fill_bytes(draw, slot, mesh->bytes, mesh->vertex_size, mesh->vertex_count,
                                                mesh->indices, mesh->index_count);
            tide_draw_set_blend(draw, (tide_blend)c->blend);
            tide_draw_set_depth(draw, (tide_depth)c->depth);
            tide_draw_set_cull(draw, (tide_cull)(c->cull & 3u));
            tide_draw_set_tint(draw, r->tint);
            tide_draw_unset_transforms(draw);
            if (r->moved) tide_draw_set_transform(draw, r->transform);
            // The snapshot already contains precisely the accepted instances;
            // do not run visibility/LOD selection a second time in cached_direct.
            tide_draw_set_frustum_culling(draw, false);
            tide_draw_shaded_instances(draw, r->shader, slot, instances, r->count, uniforms, textures, r->sampling);
            break;
        }
        default:
            return fail_at("Unsupported command in immutable direct cache", i);
        }
    }
    return true;
}

uint64_t render3d_direct_digest(const render3d_direct *cache)
{
    return cache ? cache->digest : 0;
}

void render3d_direct_free(render3d_direct *cache)
{
    if (!cache) return;
    if (cache->blocks) {
        for (uint32_t i = 0; i < cache->frame.texture_count; i++) free(cache->blocks[i]);
    }
    free(cache->blocks);
    free(cache->cameras);
    tide_draw_free(&cache->frame);
    free(cache);
}
