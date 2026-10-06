#include "tide/draw.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "tide/net.h"
#include "tide/page.h"

static void reindex_meshes(tide_draw_list *d);

static tide_render_target_state *render_targets;

// A managed target of `size`, or a dead one, having said `why`, for a size
// there's no image of.
static tide_render_target make_target(const tide_target_size size, const char *why)
{
    tide_render_target_state *s = tide_alloc_zeroed(1, sizeof *s);
    *s = (tide_render_target_state){.next = render_targets, .owner = &render_targets, .size = size, .version = 1,
                                    .alive = tide_target_size_valid(size)};
    render_targets = s;
    if (!s->alive) {
        s->size = (tide_target_size){0};
        fprintf(stderr, "%s\n", why);
    }
    return (tide_render_target){.id = TIDE_RENDER_MANAGED | (uint64_t)(uintptr_t)s, .size = s->size};
}

tide_render_target tide_render_target_make(const float width, const float height)
{
    return make_target((tide_target_size){.width = width, .height = height}, "RenderTarget requires positive dimensions");
}

tide_render_target tide_render_target_make_scaled(const float width, const float height, const float scale)
{
    // A scale of 0 is the display's in a size, which this never asks for
    const tide_target_size size = {.width = scale > 0.0f ? width : 0.0f, .height = height, .scale = scale};
    return make_target(size, "RenderTarget requires positive dimensions and a positive number of pixels per unit");
}

tide_render_target tide_render_target_screen(const float factor)
{
    const tide_target_size size = {.window = factor > 0.0f ? factor : 0.0f};
    return make_target(size, "RenderTarget.Screen requires a positive factor of the window's size");
}

tide_render_target tide_render_target_window(void)
{
    return tide_render_target_screen(1.0f);
}

void tide_render_target_resize(const tide_render_target target, const float width, const float height)
{
    tide_render_target_state *s = tide_render_target_state_of(target.id);
    if (s && s->alive && s->size.window > 0.0f) {
        fprintf(stderr, "RenderTarget.Resize: a target made with RenderTarget.Screen follows the window's size, and "
                        "keeps it\n");
        return;
    }
    const tide_target_size size = {.width = width, .height = height, .scale = s ? s->size.scale : 0.0f};
    if (!s || !s->alive || !tide_target_size_valid(size)) {
        fprintf(stderr, "RenderTarget.Resize requires a live owned target and positive dimensions\n");
        return;
    }
    s->size = size;
    s->version++;
}

void tide_render_target_dispose(const tide_render_target target)
{
    tide_render_target_state *s = tide_render_target_state_of(target.id);
    if (s) s->alive = false;
    else if (target.id) fprintf(stderr, "RenderTarget.Dispose requires an owned target\n");
}

void tide_render_target_adopt(const tide_render_target target)
{
    tide_render_target_state *s = tide_render_target_state_of(target.id);
    if (!s || s->owner == &render_targets) return;
    tide_render_target_state **at = s->owner;
    while (*at && *at != s) at = &(*at)->next;
    if (*at) *at = s->next;
    s->owner = &render_targets;
    s->next = render_targets;
    render_targets = s;
}

tide_render_target tide_render_target_recreate(const tide_render_target previous)
{
    if (!tide_render_target_managed(previous.id) || !tide_target_size_valid(previous.size))
        return (tide_render_target){0};
    for (const tide_render_target_state *s = render_targets; s; s = s->next) {
        if (s->reload_id == previous.id)
            return (tide_render_target){.id = TIDE_RENDER_MANAGED | (uint64_t)(uintptr_t)s, .size = s->size};
    }
    // As it was: its size, its scale, and whether it follows the window
    const tide_render_target next = make_target(previous.size, "");
    tide_render_target_state_of(next.id)->reload_id = previous.id;
    return next;
}

void tide_render_targets_recreated(void)
{
    for (tide_render_target_state *s = render_targets; s; s = s->next) s->reload_id = 0;
}

void tide_render_targets_free(void)
{
    while (render_targets) {
        tide_render_target_state *s = render_targets;
        render_targets = s->next;
        free(s);
    }
}

void tide_draw_reset(tide_draw_list *d)
{
    d->count = 0;
    d->text_used = 0;
    d->vertex_count = 0;
    d->index_count = 0;
    d->matrix_count = 0;
    d->run_count = 0;
    d->instance_used = 0;
    d->uniform_used = 0;
    d->shape_count = 0;
    d->target_count = d->target = 0;
    // Every setting is back where a frame starts
    d->place = d->window = (tide_draw_place){0};
    d->unsettled = d->altered = d->tinted = 0;
    d->blend = TIDE_BLEND_ALPHA;
    d->depth = TIDE_DEPTH_READ_WRITE;
    d->camera_count = d->camera_3d_count = d->clip_count = d->turn_count = d->saved_count = 0;
    d->bold = NULL;
    // The textures the frame drew with stay, for the next to find its pixels
    // here already
    uint32_t kept = 0;
    for (uint32_t i = 0; i < d->texture_count; i++) {
        tide_draw_texture *t = &d->textures[i];
        if (t->frame != d->frame) {
            free(t->pixels);
            continue;
        }
        d->textures[kept++] = *t;
    }
    d->texture_count = kept;
    // ...and so do its 3D meshes, each in its place
    bool freed = false;
    for (uint32_t i = 0; i < d->mesh_count; i++) {
        tide_draw_mesh_data *m = &d->meshes[i];
        if (m->space == TIDE_MESH_NONE || m->frame == d->frame) continue;
        free(m->vertices);
        free(m->indices);
        *m = (tide_draw_mesh_data){.next_free = d->mesh_free};
        d->mesh_free = i + 1u;
        freed = true;
    }
    if (freed) reindex_meshes(d);
    d->frame++;
}

static void free_meshes(tide_draw_list *d)
{
    for (uint32_t i = 0; i < d->mesh_count; i++) {
        free(d->meshes[i].vertices);
        free(d->meshes[i].indices);
    }
    free(d->meshes);
    free(d->mesh_index);
    d->meshes = NULL;
    d->mesh_count = d->mesh_capacity = d->mesh_free = 0;
    d->mesh_index = NULL;
    d->mesh_index_capacity = 0;
}

void tide_draw_free(tide_draw_list *d)
{
    for (uint32_t i = 0; i < d->texture_count; i++) free(d->textures[i].pixels);
    free_meshes(d);
    free(d->commands);
    free(d->text);
    free(d->vertices);
    free(d->indices);
    free(d->textures);
    free(d->targets);
    free(d->cameras);
    free(d->cameras_3d);
    free(d->clips);
    free(d->turns);
    free(d->saved);
    free(d->remap);
    free(d->matrices);
    free(d->runs);
    free(d->instances);
    free(d->uniforms);
    free(d->shapes);
    *d = (tide_draw_list){0};
}

// Room for `more` items of `size` bytes after the `count` there are, the
// array doubling as it needs.
static void *grow(void *items, const uint32_t count, uint32_t *capacity, const uint64_t more, const size_t size,
                  const uint32_t first)
{
    if (more <= *capacity - count) return items;
    uint64_t room = *capacity ? *capacity : first;
    while (room < count + more) room *= 2u;
    if (room > UINT32_MAX / size) tide_out_of_memory();
    items = tide_realloc(items, *capacity * size, (size_t)room * size);
    *capacity = (uint32_t)room;
    return items;
}

static void reserve(tide_draw_list *d, const uint32_t more)
{
    d->commands = grow(d->commands, d->count, &d->capacity, more, sizeof(tide_draw_command), 256u);
}

// ---------------------------------------------------------------------------
// Settings

// A destination's settings: the list's own while it's selected.
static tide_draw_place *place_of(tide_draw_list *d, const uint32_t target)
{
    return target == d->target ? &d->place : target ? &d->targets[target - 1u].place : &d->window;
}

// What the list keeps beside the settings, so that recording a command asks
// one thing of each: whether the selected destination plays other than it
// has, and whether a 2D command is recorded other than as it's given. After
// anything that changes a setting, or the destination.
static void refresh(tide_draw_list *d)
{
    const tide_draw_place *p = &d->place;
    d->unsettled = p->camera_2d != p->played_2d || p->camera_3d != p->played_3d || p->clip != p->played_clip;
    d->altered = d->tinted || (p->moved & TIDE_DRAW_MOVED) != 0;
}

// A command that sets something for the commands after it, and draws nothing.
static tide_draw_command *push_state(tide_draw_list *d, const tide_draw_kind kind)
{
    reserve(d, 1);
    tide_draw_command *c = &d->commands[d->count++];
    *c = (tide_draw_command){.kind = (uint16_t)kind};
    return c;
}

// The commands that leave a renderer with the selected destination's cameras
// and clip as its place has them.
static __attribute__((noinline)) void settle(tide_draw_list *d)
{
    tide_draw_place *p = &d->place;
    if (p->camera_2d != p->played_2d) {
        const tide_draw_camera_data camera = p->camera_2d ? d->cameras[p->camera_2d - 1u] : (tide_draw_camera_data){0};
        tide_draw_command *c = push_state(d, camera.screen ? TIDE_DRAW_SCREEN : TIDE_DRAW_CAMERA);
        c->a = camera.center;
        c->b = tide_f2(camera.size, 0.0f);
        p->played_2d = p->camera_2d;
    }
    if (p->camera_3d != p->played_3d) {
        uint32_t matrix = TIDE_DRAW_FIRST_CAMERA, fit = 1;
        if (p->camera_3d) {
            d->matrices = grow(d->matrices, d->matrix_count, &d->matrix_capacity, 1, sizeof(tide_float4x4), 256u);
            d->matrices[d->matrix_count] = d->cameras_3d[p->camera_3d - 1u].matrix;
            matrix = d->matrix_count++;
            fit = d->cameras_3d[p->camera_3d - 1u].fit;
        }
        tide_draw_command *c = push_state(d, TIDE_DRAW_CAMERA_3D);
        c->camera.matrix = matrix;
        c->camera.fit = fit;
        p->played_3d = p->camera_3d;
    }
    if (p->clip != p->played_clip) {
        push_state(d, p->clip ? TIDE_DRAW_CLIP : TIDE_DRAW_NO_CLIP)->clip = p->clip;
        p->played_clip = p->clip;
    }
    d->unsettled = 0;
}

// Room for another command, when there's none: apart from push, which it's rare in.
static __attribute__((noinline)) void more_commands(tide_draw_list *d)
{
    reserve(d, 1);
}

// A command that draws, with the blend there is, after what its destination's
// settings still had to say. A frame of shapes spends its time here, so it's
// part of each function that records one, and what's rare in it isn't: a
// shape costs a few stores.
static inline __attribute__((always_inline)) tide_draw_command *push(tide_draw_list *d, const tide_draw_kind kind,
                                                                     const tide_float2 a, const tide_float2 b,
                                                                     const tide_color color)
{
    if (__builtin_expect(d->unsettled != 0, 0)) settle(d);
    if (__builtin_expect(d->count == d->capacity, 0)) more_commands(d);
    tide_draw_command *c = &d->commands[d->count++];
    *c = (tide_draw_command){.kind = (uint16_t)kind, .blend = (uint8_t)d->blend, .a = a, .b = b, .color = color};
    return c;
}

static tide_draw_saved *save(tide_draw_list *d, const uint32_t setting)
{
    d->saved = grow(d->saved, d->saved_count, &d->saved_capacity, 1, sizeof(tide_draw_saved), 16u);
    tide_draw_saved *s = &d->saved[d->saved_count++];
    *s = (tide_draw_saved){.setting = setting, .target = d->target};
    return s;
}

void tide_draw_pop(tide_draw_list *d, const uint32_t mark)
{
    while (d->saved_count > mark) {
        const tide_draw_saved *s = &d->saved[--d->saved_count];
        if (s->setting == TIDE_DRAW_SAVED_BLEND) {
            d->blend = s->value;
        } else if (s->setting == TIDE_DRAW_SAVED_DEPTH) {
            d->depth = s->value;
        } else if (s->setting == TIDE_DRAW_SAVED_TINT) {
            d->tint = s->tint;
            d->tinted = 1;
        } else if (s->setting == TIDE_DRAW_SAVED_NO_TINT) {
            d->tinted = 0;
        } else if (s->target <= d->target_count) { // Its destination's, wherever commands go by now
            tide_draw_place *p = place_of(d, s->target);
            switch (s->setting) {
            case TIDE_DRAW_SAVED_TRANSFORM:
                p->transform = s->transform;
                p->moved = (p->moved & TIDE_DRAW_MOVED_3D) | s->moved;
                p->scale = s->scale;
                break;
            case TIDE_DRAW_SAVED_TRANSFORM_3D:
                p->transform_3d = s->transform_3d;
                p->moved |= TIDE_DRAW_MOVED_3D;
                break;
            case TIDE_DRAW_SAVED_NO_TRANSFORM_3D: p->moved &= ~(uint32_t)TIDE_DRAW_MOVED_3D; break;
            case TIDE_DRAW_SAVED_CLIP: p->clip = s->value; break;
            case TIDE_DRAW_SAVED_CAMERA_2D: p->camera_2d = s->value; break;
            default: p->camera_3d = s->value; break;
            }
        }
    }
    refresh(d);
}

void tide_draw_set_blend(tide_draw_list *d, const tide_blend blend)
{
    d->blend = (uint32_t)blend <= TIDE_BLEND_REPLACE ? (uint32_t)blend : TIDE_BLEND_ALPHA;
}

void tide_draw_unset_blend(tide_draw_list *d)
{
    d->blend = TIDE_BLEND_ALPHA;
}

uint32_t tide_draw_push_blend(tide_draw_list *d, const tide_blend blend)
{
    const uint32_t mark = d->saved_count;
    save(d, TIDE_DRAW_SAVED_BLEND)->value = d->blend;
    tide_draw_set_blend(d, blend);
    return mark;
}

void tide_draw_set_depth(tide_draw_list *d, const tide_depth depth)
{
    d->depth = (uint32_t)depth <= TIDE_DEPTH_NONE ? (uint32_t)depth : TIDE_DEPTH_READ_WRITE;
}

void tide_draw_unset_depth(tide_draw_list *d)
{
    d->depth = TIDE_DEPTH_READ_WRITE;
}

uint32_t tide_draw_push_depth(tide_draw_list *d, const tide_depth depth)
{
    const uint32_t mark = d->saved_count;
    save(d, TIDE_DRAW_SAVED_DEPTH)->value = d->depth;
    tide_draw_set_depth(d, depth);
    return mark;
}

void tide_draw_set_tint(tide_draw_list *d, const tide_color tint)
{
    d->tint = tint;
    d->tinted = !(tint.r == 1.0f && tint.g == 1.0f && tint.b == 1.0f && tint.a == 1.0f);
    refresh(d);
}

void tide_draw_unset_tint(tide_draw_list *d)
{
    d->tinted = 0;
    refresh(d);
}

uint32_t tide_draw_push_tint(tide_draw_list *d, const tide_color tint)
{
    const uint32_t mark = d->saved_count;
    if (d->tinted) save(d, TIDE_DRAW_SAVED_TINT)->tint = d->tint;
    else save(d, TIDE_DRAW_SAVED_NO_TINT);
    tide_draw_set_tint(d, tide_mul_color(tide_draw_tint(d), tint)); // Inside the one there is
    return mark;
}

// A rotation's cos and sin. Most shapes aren't turned, and those are exact.
static tide_float2 turn_by(const float rotation)
{
    return rotation == 0.0f ? tide_f2(1.0f, 0.0f) : tide_f2(tide_cos_f(rotation), tide_sin_f(rotation));
}

tide_draw_transform tide_draw_transform_trs(const tide_float2 offset, const float rotation, const tide_float2 scale)
{
    const tide_float2 turn = turn_by(rotation);
    return (tide_draw_transform){{turn.x * scale.x, turn.y * scale.x}, {-turn.y * scale.y, turn.x * scale.y}, offset};
}

// Where a transform puts a point, and which way it turns and stretches a
// direction, which it doesn't move.
static tide_float2 moved_point(const tide_draw_transform *t, const tide_float2 p)
{
    return tide_f2(t->offset.x + p.x * t->x.x + p.y * t->y.x, t->offset.y + p.x * t->x.y + p.y * t->y.y);
}

static tide_float2 moved_way(const tide_draw_transform *t, const tide_float2 v)
{
    return tide_f2(v.x * t->x.x + v.y * t->y.x, v.x * t->x.y + v.y * t->y.y);
}

// The place's 2D transform, and what's known of it.
static void place_transform(tide_draw_place *p, const tide_draw_transform t)
{
    p->transform = t;
    p->moved &= (uint32_t)TIDE_DRAW_MOVED_3D;
    p->scale = 1.0f;
    if (t.x.x == 1.0f && t.x.y == 0.0f && t.y.x == 0.0f && t.y.y == 1.0f && t.offset.x == 0.0f && t.offset.y == 0.0f) return;
    p->moved |= TIDE_DRAW_MOVED;
    const float area = t.x.x * t.y.y - t.x.y * t.y.x;
    p->scale = tide_sqrt_f(area < 0.0f ? -area : area);
    // As long each way, and still square to each other: turned and mirrored as it may be
    if (t.x.x * t.x.x + t.x.y * t.x.y == t.y.x * t.y.x + t.y.y * t.y.y && t.x.x * t.y.x + t.x.y * t.y.y == 0.0f) {
        p->moved |= TIDE_DRAW_MOVED_ROUND;
    }
    if (t.x.y == 0.0f && t.y.x == 0.0f && t.x.x > 0.0f && t.y.y > 0.0f) {
        p->moved |= TIDE_DRAW_MOVED_UPRIGHT;
        if (t.x.x == t.y.y) {
            p->moved |= TIDE_DRAW_MOVED_EVEN;
            p->scale = t.x.x; // Exactly
        }
    }
}

void tide_draw_set_transform(tide_draw_list *d, const tide_draw_transform transform)
{
    place_transform(&d->place, transform);
    refresh(d);
}

void tide_draw_unset_transform(tide_draw_list *d)
{
    d->place.moved &= (uint32_t)TIDE_DRAW_MOVED_3D;
    refresh(d);
}

uint32_t tide_draw_push_transform(tide_draw_list *d, const tide_draw_transform transform)
{
    const uint32_t mark = d->saved_count;
    tide_draw_place *p = &d->place;
    tide_draw_saved *s = save(d, TIDE_DRAW_SAVED_TRANSFORM);
    s->transform = p->transform;
    s->moved = p->moved & ~(uint32_t)TIDE_DRAW_MOVED_3D;
    s->scale = p->scale;
    if (p->moved & TIDE_DRAW_MOVED) { // Inside the one there is
        const tide_draw_transform *outer = &p->transform;
        tide_draw_set_transform(d, (tide_draw_transform){moved_way(outer, transform.x), moved_way(outer, transform.y),
                                                         moved_point(outer, transform.offset)});
    } else {
        tide_draw_set_transform(d, transform);
    }
    return mark;
}

static bool same_matrix(const tide_float4x4 *a, const tide_float4x4 *b)
{
    return memcmp(a, b, sizeof *a) == 0;
}

void tide_draw_set_transform_3d(tide_draw_list *d, const tide_float4x4 transform)
{
    const tide_float4x4 none = tide_identity_f4x4();
    d->place.transform_3d = transform;
    if (same_matrix(&transform, &none)) d->place.moved &= ~(uint32_t)TIDE_DRAW_MOVED_3D;
    else d->place.moved |= TIDE_DRAW_MOVED_3D;
}

void tide_draw_unset_transform_3d(tide_draw_list *d)
{
    d->place.moved &= ~(uint32_t)TIDE_DRAW_MOVED_3D;
}

void tide_draw_unset_transforms(tide_draw_list *d)
{
    d->place.moved = 0;
    refresh(d);
}

uint32_t tide_draw_push_transform_3d(tide_draw_list *d, const tide_float4x4 transform)
{
    const uint32_t mark = d->saved_count;
    const tide_draw_place *p = &d->place;
    if (p->moved & TIDE_DRAW_MOVED_3D) {
        save(d, TIDE_DRAW_SAVED_TRANSFORM_3D)->transform_3d = p->transform_3d;
        tide_draw_set_transform_3d(d, tide_mul_f4x4(p->transform_3d, transform)); // Inside the one there is
    } else {
        save(d, TIDE_DRAW_SAVED_NO_TRANSFORM_3D);
        tide_draw_set_transform_3d(d, transform);
    }
    return mark;
}

// The place a 2D camera has in the list: the one the destination plays or has
// when that's it, so that setting it again is no change, or a new one.
static uint32_t camera_slot(tide_draw_list *d, const tide_draw_camera_data camera)
{
    const tide_draw_place *p = &d->place;
    const uint32_t known[2] = {p->played_2d, p->camera_2d};
    for (int i = 0; i < 2; i++) {
        if (!known[i]) continue;
        const tide_draw_camera_data *c = &d->cameras[known[i] - 1u];
        if (c->screen == camera.screen && c->center.x == camera.center.x && c->center.y == camera.center.y
            && c->size == camera.size) {
            return known[i];
        }
    }
    d->cameras = grow(d->cameras, d->camera_count, &d->camera_capacity, 1, sizeof(tide_draw_camera_data), 16u);
    d->cameras[d->camera_count] = camera;
    return ++d->camera_count;
}

void tide_draw_set_camera(tide_draw_list *d, const tide_float2 center, const float size)
{
    d->place.camera_2d = camera_slot(d, (tide_draw_camera_data){.center = center, .size = size});
    refresh(d);
}

void tide_draw_unset_camera(tide_draw_list *d)
{
    d->place.camera_2d = 0;
    refresh(d);
}

void tide_draw_unset_cameras(tide_draw_list *d)
{
    d->place.camera_2d = d->place.camera_3d = 0;
    refresh(d);
}

void tide_draw_set_screen(tide_draw_list *d)
{
    d->place.camera_2d = camera_slot(d, (tide_draw_camera_data){.screen = 1});
    refresh(d);
}

uint32_t tide_draw_push_camera(tide_draw_list *d, const tide_float2 center, const float size)
{
    const uint32_t mark = d->saved_count;
    save(d, TIDE_DRAW_SAVED_CAMERA_2D)->value = d->place.camera_2d;
    tide_draw_set_camera(d, center, size);
    return mark;
}

uint32_t tide_draw_push_screen(tide_draw_list *d)
{
    const uint32_t mark = d->saved_count;
    save(d, TIDE_DRAW_SAVED_CAMERA_2D)->value = d->place.camera_2d;
    tide_draw_set_screen(d);
    return mark;
}

// The place a clip has in the list, as a camera's.
static uint32_t clip_slot(tide_draw_list *d, const tide_draw_clip_data clip)
{
    const tide_draw_place *p = &d->place;
    const uint32_t known[2] = {p->played_clip, p->clip};
    for (int i = 0; i < 2; i++) {
        if (known[i] && memcmp(&d->clips[known[i] - 1u], &clip, sizeof clip) == 0) return known[i];
    }
    d->clips = grow(d->clips, d->clip_count, &d->clip_capacity, 1, sizeof(tide_draw_clip_data), 16u);
    d->clips[d->clip_count] = clip;
    return ++d->clip_count;
}

// `rect` as a clip of the selected destination's, inside the clip `parent`.
static void clip_to(tide_draw_list *d, tide_rect rect, const uint32_t parent)
{
    tide_draw_place *p = &d->place;
    if (p->moved & TIDE_DRAW_MOVED) { // The upright rect around what the transform makes of it
        const tide_draw_transform *t = &p->transform;
        const tide_float2 corners[4] = {
            moved_point(t, tide_f2(rect.x, rect.y)), moved_point(t, tide_f2(rect.x + rect.width, rect.y)),
            moved_point(t, tide_f2(rect.x, rect.y + rect.height)),
            moved_point(t, tide_f2(rect.x + rect.width, rect.y + rect.height))};
        tide_float2 low = corners[0], high = corners[0];
        for (int i = 1; i < 4; i++) {
            low = tide_f2(tide_min_f(low.x, corners[i].x), tide_min_f(low.y, corners[i].y));
            high = tide_f2(tide_max_f(high.x, corners[i].x), tide_max_f(high.y, corners[i].y));
        }
        // One with no size clips everything, turned or not
        const bool sized = rect.width > 0.0f && rect.height > 0.0f;
        rect = (tide_rect){low.x, low.y, sized ? high.x - low.x : 0.0f, sized ? high.y - low.y : 0.0f};
    }
    p->clip = clip_slot(d, (tide_draw_clip_data){.rect = rect, .camera = p->camera_2d, .parent = parent});
    refresh(d);
}

void tide_draw_set_clip(tide_draw_list *d, const tide_rect rect)
{
    clip_to(d, rect, 0);
}

void tide_draw_unset_clip(tide_draw_list *d)
{
    d->place.clip = 0;
    refresh(d);
}

uint32_t tide_draw_push_clip(tide_draw_list *d, const tide_rect rect)
{
    const uint32_t mark = d->saved_count;
    save(d, TIDE_DRAW_SAVED_CLIP)->value = d->place.clip;
    clip_to(d, rect, d->place.clip); // Inside the one there is
    return mark;
}

void tide_draw_defaults(tide_draw_list *d)
{
    tide_draw_to(d, 0, 0, 0);
    d->blend = TIDE_BLEND_ALPHA;
    d->depth = TIDE_DEPTH_READ_WRITE;
    d->tinted = 0;
    for (uint32_t i = 0; i <= d->target_count; i++) {
        tide_draw_place *p = i ? &d->targets[i - 1u].place : &d->place;
        p->camera_2d = p->camera_3d = p->clip = p->moved = 0;
    }
    refresh(d);
}

void tide_draw_forget(tide_draw_list *d)
{
    const bool had_targets = d->target_count != 0;
    for (uint32_t i = 0; i < d->texture_count; i++) free(d->textures[i].pixels);
    d->texture_count = 0;
    // The window's cameras stay. Its clip goes, and what blocks saved, as
    // none is open between two frames.
    if (d->target) d->place = d->window;
    d->target_count = d->target = 0;
    d->place.clip = 0;
    d->saved_count = 0;
    free_meshes(d);
    d->epoch++;
    // Commands recorded already would draw with textures and meshes that are
    // gone, and with shaders of a build that is
    for (uint32_t i = 0; i < d->count; i++) {
        tide_draw_command *c = &d->commands[i];
        if (c->kind == TIDE_DRAW_MESH) c->mesh.texture = 0;
        if (c->kind == TIDE_DRAW_MESH_3D) c->mesh3.mesh = c->mesh3.texture = 0;
        if (c->kind == TIDE_DRAW_TARGET) c->target = UINT32_MAX;
    }
    for (uint32_t i = 0; i < d->run_count; i++) d->runs[i] = (tide_draw_run){0};
    // The forgotten selectors now sink their old commands. End on a valid
    // window selector so direct C draws recorded afterward have a destination.
    if (had_targets) push_state(d, TIDE_DRAW_TARGET)->target = 0;
    refresh(d);
}

static uint32_t target_slot(tide_draw_list *d, const uint64_t id, const tide_target_size size, const uint64_t version)
{
    if (!id) return 0;
    for (uint32_t i = 0; i < d->target_count; i++) {
        const tide_draw_target_data *t = &d->targets[i];
        if (t->id == id && tide_target_size_same(t->size, size) && t->version == version) return i + 1u;
    }
    d->targets = grow(d->targets, d->target_count, &d->target_capacity, 1, sizeof(tide_draw_target_data), 8u);
    d->targets[d->target_count] = (tide_draw_target_data){.id = id, .version = version, .size = size};
    return ++d->target_count;
}

static tide_draw_list *draw_to_version(tide_draw_list *d, const uint64_t id, const tide_target_size size,
                                       const uint64_t version)
{
    const uint32_t slot = target_slot(d, id, size, version);
    if (slot == d->target) return d;
    // Each destination's settings wait while another is selected
    if (d->target) d->targets[d->target - 1u].place = d->place;
    else d->window = d->place;
    d->target = slot;
    d->place = slot ? d->targets[slot - 1u].place : d->window;
    push_state(d, TIDE_DRAW_TARGET)->target = slot;
    refresh(d);
    return d;
}

tide_draw_list *tide_draw_to_sized(tide_draw_list *d, const uint64_t id, const tide_target_size size)
{
    return draw_to_version(d, id, size, tide_render_target_version(id));
}

static uint32_t target_texture_version(tide_draw_list *d, const uint64_t id, const tide_target_size size,
                                       const uint64_t version)
{
    if (!id) return 0;
    for (uint32_t i = 0; i < d->texture_count; i++) {
        tide_draw_texture *t = &d->textures[i];
        if (t->space == TIDE_PIXELS_TARGET && t->id == id && tide_target_size_same(t->target, size)
            && t->version == version) {
            t->frame = d->frame;
            return i + 1u;
        }
    }
    d->textures = grow(d->textures, d->texture_count, &d->texture_capacity, 1, sizeof(tide_draw_texture), 8u);
    d->textures[d->texture_count] = (tide_draw_texture){
        .id = id, .version = version, .space = TIDE_PIXELS_TARGET, .target = size, .frame = d->frame, .epoch = d->epoch};
    return ++d->texture_count;
}

uint32_t tide_draw_target_texture_sized(tide_draw_list *d, const uint64_t id, const tide_target_size size)
{
    return target_texture_version(d, id, size, tide_render_target_version(id));
}

void tide_draw_clear(tide_draw_list *d, const tide_color color)
{
    push(d, TIDE_DRAW_CLEAR, tide_f2(0.0f, 0.0f), tide_f2(0.0f, 0.0f), color);
}

// ---------------------------------------------------------------------------
// Shapes

static void push_shape(tide_draw_list *d, tide_float2 center, tide_float2 size, tide_color color,
                       tide_draw_shape_data shape);

// A shape that fills whole pixels (or a plain circle), as the settings leave
// it. A frame of them spends its time in the functions that call this, which
// only do when there's a tint or a transform.
static __attribute__((noinline)) void plain_altered(tide_draw_list *d, const tide_draw_kind kind, tide_float2 a,
                                                   tide_float2 b, tide_color color)
{
    const tide_draw_place *p = &d->place;
    const tide_draw_transform *t = &p->transform;
    const bool moved = (p->moved & TIDE_DRAW_MOVED) != 0;
    const bool circle = kind == TIDE_DRAW_CIRCLE || kind == TIDE_DRAW_WIRE_CIRCLE;
    if (moved && kind != TIDE_DRAW_LINE && !(p->moved & (circle ? TIDE_DRAW_MOVED_ROUND : TIDE_DRAW_MOVED_UPRIGHT))) {
        // Turned or stretched, it's the smooth shape it also is: a box, all corners for a circle. An
        // outline's stays a pixel wide.
        const bool wire = kind == TIDE_DRAW_WIRE_RECT || kind == TIDE_DRAW_WIRE_CIRCLE;
        const float radius = tide_abs_f(b.x);
        const tide_float2 size = circle ? tide_f2(radius * 2.0f, radius * 2.0f) : tide_f2(tide_abs_f(b.x), tide_abs_f(b.y));
        const tide_color fill = wire ? TIDE_COLOR_CLEAR : color;
        push_shape(d, a, size, fill,
                   (tide_draw_shape_data){.turn = {1.0f, 0.0f},
                                          .radius = circle ? radius : 0.0f,
                                          .form = (uint32_t)TIDE_SHAPE_BOX | (wire ? (uint32_t)TIDE_SHAPE_HAIRLINE : 0u),
                                          .color2 = fill,
                                          .outline_color = color});
        return;
    }
    if (d->tinted) color = tide_mul_color(color, d->tint);
    if (moved) {
        a = moved_point(t, a);
        b = kind == TIDE_DRAW_LINE ? moved_point(t, b) : circle ? tide_f2(b.x * p->scale, 0.0f) : tide_f2(b.x * t->x.x, b.y * t->y.y);
    }
    push(d, kind, a, b, color);
}

void tide_draw_circle(tide_draw_list *d, const tide_float2 center, const float radius, const tide_color color)
{
    if (d->altered) plain_altered(d, TIDE_DRAW_CIRCLE, center, tide_f2(radius, 0.0f), color);
    else push(d, TIDE_DRAW_CIRCLE, center, tide_f2(radius, 0.0f), color);
}

void tide_draw_wire_circle(tide_draw_list *d, const tide_float2 center, const float radius, const tide_color color)
{
    if (d->altered) plain_altered(d, TIDE_DRAW_WIRE_CIRCLE, center, tide_f2(radius, 0.0f), color);
    else push(d, TIDE_DRAW_WIRE_CIRCLE, center, tide_f2(radius, 0.0f), color);
}

void tide_draw_rect(tide_draw_list *d, const tide_float2 center, const tide_float2 size, const tide_color color)
{
    if (d->altered) plain_altered(d, TIDE_DRAW_RECT, center, size, color);
    else push(d, TIDE_DRAW_RECT, center, size, color);
}

void tide_draw_wire_rect(tide_draw_list *d, const tide_float2 center, const tide_float2 size, const tide_color color)
{
    if (d->altered) plain_altered(d, TIDE_DRAW_WIRE_RECT, center, size, color);
    else push(d, TIDE_DRAW_WIRE_RECT, center, size, color);
}

void tide_draw_line(tide_draw_list *d, const tide_float2 from, const tide_float2 to, const tide_color color)
{
    if (d->altered) plain_altered(d, TIDE_DRAW_LINE, from, to, color);
    else push(d, TIDE_DRAW_LINE, from, to, color);
}

// ---------------------------------------------------------------------------
// Shapes with smooth edges

// Whatever a shape is drawn with fits it: its corners no rounder, and its
// outline no wider, than half its smaller side.
static void fit_shape(const tide_float2 size, tide_draw_shape_data *shape)
{
    const float most = (size.x < size.y ? size.x : size.y) * 0.5f;
    shape->radius = shape->radius > most ? most : shape->radius > 0.0f ? shape->radius : 0.0f;
    shape->outline = shape->outline > most ? most : shape->outline > 0.0f ? shape->outline : 0.0f;
}

// An ellipse as wide as it's tall is a circle, which is a box whose corners
// take all of it: the same pixels, with less to work out for each.
static tide_shape_form ellipse_form(const tide_float2 size, float *radius)
{
    if (size.x != size.y) return TIDE_SHAPE_ELLIPSE;
    *radius = size.x * 0.5f;
    return TIDE_SHAPE_BOX;
}

// A smooth shape as the tint and the transform leave it: apart from
// push_shape, which a frame of shapes with neither spends its time in.
static __attribute__((noinline)) void alter_shape(const tide_draw_list *d, tide_float2 *center, tide_float2 *size, tide_color *color,
                        tide_draw_shape_data *shape)
{
    if (d->tinted) {
        *color = tide_mul_color(*color, d->tint);
        shape->color2 = tide_mul_color(shape->color2, d->tint);
        shape->outline_color = tide_mul_color(shape->outline_color, d->tint);
    }
    const tide_draw_place *p = &d->place;
    if (!(p->moved & TIDE_DRAW_MOVED)) return;
    const tide_draw_transform *t = &p->transform;
    const uint32_t hairline = shape->form & (uint32_t)TIDE_SHAPE_HAIRLINE;
    uint32_t form = shape->form & (uint32_t)TIDE_SHAPE_FORMS;
    *center = moved_point(t, *center);
    // Where its own axes go: along it and across it
    const tide_float2 along = moved_way(t, shape->turn), across = moved_way(t, tide_f2(-shape->turn.y, shape->turn.x));
    const float long_by = tide_length_f2(along), wide_by = tide_length_f2(across);
    const float lean = along.x * across.x + along.y * across.y; // 0 while they're still square to each other
    const bool square = tide_abs_f(lean) <= 0.00001f * long_by * wide_by;
    const bool round = form == TIDE_SHAPE_ELLIPSE
                    || (form == TIDE_SHAPE_BOX && size->x == size->y && shape->radius * 2.0f >= size->x);
    if (form == TIDE_SHAPE_ARC) {
        // Part of a ring has one radius: it's scaled as much each way
        *size = tide_f2(size->x * p->scale, size->y * p->scale);
        shape->turn = long_by > 0.0f ? tide_f2(along.x / long_by, along.y / long_by) : tide_f2(1.0f, 0.0f);
    } else if (round && !square) {
        // Still an ellipse, whose axes are the ways the transform stretches it most and least
        const tide_float2 a = tide_f2(along.x * size->x * 0.5f, along.y * size->x * 0.5f);
        const tide_float2 b = tide_f2(across.x * size->y * 0.5f, across.y * size->y * 0.5f);
        const float e = (a.x + b.y) * 0.5f, f = (a.x - b.y) * 0.5f, g = (a.y + b.x) * 0.5f, h = (a.y - b.x) * 0.5f;
        const float q = tide_sqrt_f(e * e + h * h), r = tide_sqrt_f(f * f + g * g);
        const float turned = (tide_atan2_f(h, e) + tide_atan2_f(g, f)) * 0.5f;
        *size = tide_f2((q + r) * 2.0f, tide_abs_f(q - r) * 2.0f);
        shape->turn = tide_f2(tide_cos_f(turned), tide_sin_f(turned));
        form = TIDE_SHAPE_ELLIPSE;
    } else {
        *size = tide_f2(size->x * long_by, size->y * wide_by);
        shape->turn = long_by > 0.0f ? tide_f2(along.x / long_by, along.y / long_by) : tide_f2(1.0f, 0.0f);
        if (round && long_by != wide_by) form = TIDE_SHAPE_ELLIPSE;
    }
    // Mirrored, its top is where its bottom was
    if (along.x * across.y - along.y * across.x < 0.0f) {
        const tide_color top = *color;
        *color = shape->color2;
        shape->color2 = top;
    }
    shape->radius *= p->scale;
    shape->outline *= p->scale;
    shape->soft *= p->scale;
    if (form == TIDE_SHAPE_ELLIPSE) form = (uint32_t)ellipse_form(*size, &shape->radius);
    shape->form = form | hairline;
}

// A shape `size` across around `center`, whose fill at its top is `color`.
static void push_shape(tide_draw_list *d, tide_float2 center, tide_float2 size, tide_color color,
                       tide_draw_shape_data shape)
{
    if (!(size.x > 0.0f) || !(size.y > 0.0f)) return; // Also NaN
    fit_shape(size, &shape);
    if (d->altered) {
        alter_shape(d, &center, &size, &color, &shape);
        if (!(size.x > 0.0f) || !(size.y > 0.0f)) return;
        fit_shape(size, &shape);
    }
    d->shapes = grow(d->shapes, d->shape_count, &d->shape_capacity, 1, sizeof(tide_draw_shape_data), 256u);
    d->shapes[d->shape_count] = shape;
    push(d, TIDE_DRAW_SHAPE, center, size, color)->shape = d->shape_count++;
}

// A shape in one color, with a sharp edge.
static void solid(tide_draw_list *d, const tide_float2 center, const tide_float2 size, const float radius,
                  const float rotation, const tide_shape_form form, const tide_color color)
{
    push_shape(d, center, size, color,
               (tide_draw_shape_data){.turn = turn_by(rotation), .radius = radius, .form = (uint32_t)form, .color2 = color});
}

// Its outline alone, `width` thick.
static void wire(tide_draw_list *d, const tide_float2 center, const tide_float2 size, const float radius,
                 const tide_shape_form form, const float width, const tide_color color)
{
    if (!(width > 0.0f)) return;
    push_shape(d, center, size, TIDE_COLOR_CLEAR,
               (tide_draw_shape_data){.turn = {1.0f, 0.0f}, .radius = radius, .outline = width, .form = (uint32_t)form,
                                      .color2 = TIDE_COLOR_CLEAR, .outline_color = color});
}

void tide_draw_shape(tide_draw_list *d, const tide_shape shape)
{
    float radius = shape.radius;
    const tide_shape_form form = shape.ellipse ? ellipse_form(shape.size, &radius) : TIDE_SHAPE_BOX;
    push_shape(d, shape.center, shape.size, shape.color,
               (tide_draw_shape_data){.turn = turn_by(shape.rotation), .radius = radius, .outline = shape.outline,
                                      .soft = shape.soft == shape.soft ? shape.soft : 0.0f, .form = (uint32_t)form,
                                      .color2 = tide_color_is_none(shape.color2) ? shape.color : shape.color2,
                                      .outline_color = shape.outline_color});
}

void tide_draw_thick_line(tide_draw_list *d, const tide_float2 from, const tide_float2 to, const float width,
                          const tide_color color)
{
    if (!(width > 0.0f)) return;
    // A pill as long as the line and its ends, turned along it
    const tide_float2 along = tide_f2(to.x - from.x, to.y - from.y);
    const float length = tide_length_f2(along);
    const tide_float2 turn = length > 0.0f ? tide_f2(along.x / length, along.y / length) : tide_f2(1.0f, 0.0f);
    push_shape(d, tide_f2((from.x + to.x) * 0.5f, (from.y + to.y) * 0.5f), tide_f2(length + width, width), color,
               (tide_draw_shape_data){.turn = turn, .radius = width * 0.5f, .form = TIDE_SHAPE_BOX, .color2 = color});
}

void tide_draw_thick_wire_rect(tide_draw_list *d, const tide_float2 center, const tide_float2 size, const float width,
                               const tide_color color)
{
    wire(d, center, size, 0.0f, TIDE_SHAPE_BOX, width, color);
}

void tide_draw_thick_wire_circle(tide_draw_list *d, const tide_float2 center, const float radius, const float width,
                                 const tide_color color)
{
    wire(d, center, tide_f2(radius * 2.0f, radius * 2.0f), radius, TIDE_SHAPE_BOX, width, color);
}

void tide_draw_rect_rotated(tide_draw_list *d, const tide_float2 center, const tide_float2 size, const float rotation,
                            const tide_color color)
{
    solid(d, center, size, 0.0f, rotation, TIDE_SHAPE_BOX, color);
}

void tide_draw_round_rect(tide_draw_list *d, const tide_float2 center, const tide_float2 size, const float radius,
                          const tide_color color)
{
    solid(d, center, size, radius, 0.0f, TIDE_SHAPE_BOX, color);
}

void tide_draw_round_rect_rotated(tide_draw_list *d, const tide_float2 center, const tide_float2 size,
                                  const float radius, const float rotation, const tide_color color)
{
    solid(d, center, size, radius, rotation, TIDE_SHAPE_BOX, color);
}

void tide_draw_wire_round_rect(tide_draw_list *d, const tide_float2 center, const tide_float2 size, const float radius,
                               const float width, const tide_color color)
{
    wire(d, center, size, radius, TIDE_SHAPE_BOX, width, color);
}

void tide_draw_ellipse(tide_draw_list *d, const tide_float2 center, const tide_float2 radii, const tide_color color)
{
    tide_draw_ellipse_rotated(d, center, radii, 0.0f, color);
}

void tide_draw_ellipse_rotated(tide_draw_list *d, const tide_float2 center, const tide_float2 radii,
                               const float rotation, const tide_color color)
{
    const tide_float2 size = tide_f2(radii.x * 2.0f, radii.y * 2.0f);
    float radius = 0.0f;
    const tide_shape_form form = ellipse_form(size, &radius);
    solid(d, center, size, radius, rotation, form, color);
}

void tide_draw_wire_ellipse(tide_draw_list *d, const tide_float2 center, const tide_float2 radii, const float width,
                            const tide_color color)
{
    const tide_float2 size = tide_f2(radii.x * 2.0f, radii.y * 2.0f);
    float radius = 0.0f;
    const tide_shape_form form = ellipse_form(size, &radius);
    wire(d, center, size, radius, form, width, color);
}

void tide_draw_arc(tide_draw_list *d, const tide_float2 center, const float radius, const float start,
                   const float sweep, const float width, const tide_color color)
{
    if (!(width > 0.0f) || !(sweep != 0.0f) || sweep != sweep || start != start) return;
    if (sweep >= TIDE_TAU_F || sweep <= -TIDE_TAU_F) { // All the way round: the ring
        tide_draw_thick_wire_circle(d, center, radius, width, color);
        return;
    }
    // Turned to its middle, it's the same on both sides of it
    const float half = (sweep < 0.0f ? -sweep : sweep) * 0.5f;
    push_shape(d, center, tide_f2(radius * 2.0f, radius * 2.0f), color,
               (tide_draw_shape_data){.turn = turn_by(start + sweep * 0.5f), .radius = radius, .outline = width,
                                      .sweep = half, .form = TIDE_SHAPE_ARC, .color2 = color});
}

// Text as it's given, through `turn` when a transform turned or stretched it.
static void push_text(tide_draw_list *d, const char *text, const tide_float2 position, const float size,
                      const tide_color color, const tide_anchor anchor, const unsigned char *font,
                      const tide_float2x2 *turn)
{
    uint32_t turned = 0;
    if (turn) {
        d->turns = grow(d->turns, d->turn_count, &d->turn_capacity, 1, sizeof(tide_float2x2), 16u);
        d->turns[d->turn_count] = *turn;
        turned = ++d->turn_count;
    }
    const size_t bytes = strlen(text) + 1;
    d->text = grow(d->text, d->text_used, &d->text_capacity, bytes, 1, 4096u);
    tide_draw_command *c = push(d, TIDE_DRAW_TEXT, position, tide_f2(size, 0.0f), color);
    c->text = d->text_used;
    // An anchor that's none of the nine is the first, as the GUI's areas take one
    c->words.anchor = (uint16_t)((uint32_t)anchor <= TIDE_ANCHOR_LOWER_RIGHT ? anchor : TIDE_ANCHOR_UPPER_LEFT);
    c->words.bold = font != NULL;
    c->words.turned = turned;
    if (font) d->bold = font;
    memcpy(d->text + d->text_used, text, bytes);
    d->text_used += (uint32_t)bytes;
}

void tide_draw_text_at(tide_draw_list *d, const char *text, tide_float2 position, const float size, tide_color color,
                       const tide_anchor anchor, const unsigned char *font)
{
    const tide_draw_place *p = &d->place;
    if (!d->altered) {
        push_text(d, text, position, size, color, anchor, font, NULL);
        return;
    }
    if (d->tinted) color = tide_mul_color(color, d->tint);
    if (!(p->moved & TIDE_DRAW_MOVED)) {
        push_text(d, text, position, size, color, anchor, font, NULL);
        return;
    }
    position = moved_point(&p->transform, position); // Where its anchor is
    if (p->moved & TIDE_DRAW_MOVED_EVEN) { // Bigger or smaller text, fitted to pixels as any
        push_text(d, text, position, size * p->scale, color, anchor, font, NULL);
        return;
    }
    push_text(d, text, position, size, color, anchor, font, &(tide_float2x2){p->transform.x, p->transform.y});
}

void tide_draw_text(tide_draw_list *d, const char *text, const tide_float2 position, const float size,
                    const tide_color color)
{
    tide_draw_text_at(d, text, position, size, color, TIDE_ANCHOR_UPPER_LEFT, TIDE_FONT_REGULAR);
}

// ---------------------------------------------------------------------------
// Meshes

uint32_t tide_draw_texture_slot(tide_draw_list *d, const uint32_t space, const uint64_t id, const uint64_t version,
                                const int32_t width, const int32_t height, uint8_t **pixels)
{
    *pixels = NULL;
    if (width <= 0 || height <= 0) return 0;
    const uint64_t bytes = (uint64_t)width * (uint64_t)height * 4u;
    if (bytes > UINT32_MAX) tide_out_of_memory();
    tide_draw_texture *t = NULL;
    for (uint32_t i = 0; i < d->texture_count && !t; i++) {
        if (d->textures[i].id == id && d->textures[i].space == space) t = &d->textures[i];
    }
    if (!t) {
        d->textures = grow(d->textures, d->texture_count, &d->texture_capacity, 1, sizeof(tide_draw_texture), 8u);
        t = &d->textures[d->texture_count++];
        *t = (tide_draw_texture){.id = id, .space = space, .epoch = d->epoch};
    } else if (t->version == version && t->width == width && t->height == height) {
        t->frame = d->frame;
        return (uint32_t)(t - d->textures) + 1u;
    }
    if (bytes > t->capacity) {
        t->pixels = tide_realloc(t->pixels, t->capacity, (size_t)bytes);
        t->capacity = (uint32_t)bytes;
    }
    t->version = version;
    t->width = width;
    t->height = height;
    t->frame = d->frame;
    *pixels = t->pixels;
    return (uint32_t)(t - d->textures) + 1u;
}

uint64_t tide_texture_version(const void *pixels, const int32_t width, const int32_t height)
{
    if (!pixels || width <= 0 || height <= 0) return 0;
    return tide_hash(pixels, (size_t)width * (size_t)height * 4u);
}

static uint32_t texture_slot(tide_draw_list *d, const tide_texture *texture)
{
    if (!texture || !texture->pixels) return 0;
    uint8_t *pixels;
    const uint32_t slot = tide_draw_texture_slot(d, TIDE_PIXELS_MEMORY, (uint64_t)(uintptr_t)texture->pixels,
                                                 texture->version, texture->width, texture->height, &pixels);
    if (pixels) memcpy(pixels, texture->pixels, (size_t)texture->width * (size_t)texture->height * 4u);
    return slot;
}

uint32_t tide_draw_texture_source(tide_draw_list *d, const tide_texture_ref texture)
{
    if (!texture.target.id) return texture_slot(d, &texture.image);
    const tide_render_target target = tide_render_target_resolve(texture.target);
    return tide_draw_target_texture_sized(d, target.id, target.size);
}

// A mesh's corners as the tint and the transform leave them: apart from
// tide_draw_vertices, which a frame of meshes with neither spends its time in.
static __attribute__((noinline)) void alter_vertices(tide_draw_list *d, const uint32_t base, const uint32_t count)
{
    const tide_draw_place *p = &d->place;
    const bool moved = (p->moved & TIDE_DRAW_MOVED) != 0;
    for (tide_vertex *v = d->vertices + base, *end = v + count; v < end; v++) {
        if (d->tinted) v->color = tide_mul_color(v->color, d->tint);
        if (moved) v->position = moved_point(&p->transform, v->position);
    }
}

uint32_t tide_draw_vertices(tide_draw_list *d, const tide_vertex *vertices, const uint32_t count)
{
    const uint32_t base = d->vertex_count;
    if (count == 0) return base;
    d->vertices = grow(d->vertices, d->vertex_count, &d->vertex_capacity, count, sizeof(tide_vertex), 1024u);
    memcpy(d->vertices + base, vertices, (size_t)count * sizeof(tide_vertex));
    d->vertex_count += count;
    if (__builtin_expect(d->altered != 0, 0)) alter_vertices(d, base, count); // Tinted and moved as they're copied
    return base;
}

void tide_draw_triangles_with(tide_draw_list *d, const uint32_t base, const uint32_t *indices, const uint32_t count,
                              const uint32_t slot, const tide_filter filter)
{
    if (count < 3u || base > d->vertex_count || slot > d->texture_count) return;
    const uint32_t within = d->vertex_count - base;
    d->indices = grow(d->indices, d->index_count, &d->index_capacity, count, sizeof(uint32_t), 2048u);
    const uint32_t first = d->index_count;
    for (uint32_t i = 0; i + 3u <= count; i += 3u) {
        const uint32_t a = indices[i], b = indices[i + 1u], c = indices[i + 2u];
        if (a >= within || b >= within || c >= within) continue;
        uint32_t *out = &d->indices[d->index_count];
        out[0] = base + a;
        out[1] = base + b;
        out[2] = base + c;
        d->index_count += 3u;
    }
    if (d->index_count == first) return;
    // More of the mesh before it, when nothing came between them
    tide_draw_command *last = d->count && !d->unsettled ? &d->commands[d->count - 1u] : NULL;
    if (last && last->kind == TIDE_DRAW_MESH && last->blend == d->blend && last->mesh.texture == slot
        && last->mesh.filter == (uint32_t)filter && last->mesh.first + last->mesh.count == first) {
        last->mesh.count += d->index_count - first;
        return;
    }
    tide_draw_command *c = push(d, TIDE_DRAW_MESH, tide_f2(0.0f, 0.0f), tide_f2(0.0f, 0.0f), TIDE_COLOR_CLEAR);
    c->mesh.first = first;
    c->mesh.count = d->index_count - first;
    c->mesh.texture = slot;
    c->mesh.filter = (uint32_t)filter;
}

void tide_draw_triangles(tide_draw_list *d, const uint32_t base, const uint32_t *indices, const uint32_t count,
                         const tide_texture *texture, const tide_filter filter)
{
    tide_draw_triangles_with(d, base, indices, count, texture_slot(d, texture), filter);
}

void tide_draw_mesh(tide_draw_list *d, const tide_vertex *vertices, const uint32_t vertex_count, const uint32_t *indices,
                    const uint32_t index_count, const tide_texture *texture, const tide_filter filter)
{
    if (vertex_count == 0 || index_count < 3u) return;
    tide_draw_triangles(d, tide_draw_vertices(d, vertices, vertex_count), indices, index_count, texture, filter);
}

// ---------------------------------------------------------------------------
// 3D

// OpenGL's view looks down -z, and the world's cameras along +z: z turned
// around, after `m`.
static tide_float4x4 turn_z(tide_float4x4 m)
{
    m.c0.z = -m.c0.z;
    m.c1.z = -m.c1.z;
    m.c2.z = -m.c2.z;
    m.c3.z = -m.c3.z;
    return m;
}

// The 3D camera `world_to_clip`, for the commands to come: the one the
// destination plays or has when that's it, so that setting it again is no
// change, or a new one.
static void camera_3d(tide_draw_list *d, const tide_float4x4 world_to_clip, const bool fit)
{
    tide_draw_place *p = &d->place;
    const uint32_t known[2] = {p->played_3d, p->camera_3d};
    for (int i = 0; i < 2; i++) {
        if (!known[i]) continue;
        const tide_draw_camera_3d_data *c = &d->cameras_3d[known[i] - 1u];
        if (c->fit == (uint32_t)fit && same_matrix(&c->matrix, &world_to_clip)) {
            p->camera_3d = known[i];
            refresh(d);
            return;
        }
    }
    d->cameras_3d = grow(d->cameras_3d, d->camera_3d_count, &d->camera_3d_capacity, 1, sizeof(tide_draw_camera_3d_data), 8u);
    d->cameras_3d[d->camera_3d_count] = (tide_draw_camera_3d_data){.matrix = world_to_clip, .fit = fit};
    p->camera_3d = ++d->camera_3d_count;
    refresh(d);
}

tide_float4x4 tide_draw_camera_3d_matrix(const tide_float3 position, const tide_quaternion rotation,
                                         const float field_of_view)
{
    // The world as the camera sees it: moved and turned the other way
    const tide_quaternion back = tide_conjugate_q(tide_normalize_q(rotation));
    const tide_float4x4 view = tide_f4x4_from_f3x3_f3(tide_f3x3_from_q(back), tide_neg_f3(tide_rotate_q(back, position)));
    // A perspective for a square screen, from 0.3 units on, as Unity's
    // cameras, with no far side: precision hardly depends on it
    const float near_z = 0.3f;
    const float f = 1.0f / tide_tan_f(tide_radians_f(tide_clamp_f(field_of_view, 0.00001f, 179.0f)) * 0.5f);
    const tide_float4x4 projection = {{f, 0.0f, 0.0f, 0.0f},
                                      {0.0f, f, 0.0f, 0.0f},
                                      {0.0f, 0.0f, -1.0f, -1.0f},
                                      {0.0f, 0.0f, -2.0f * near_z, 0.0f}};
    return tide_mul_f4x4(projection, turn_z(view));
}

void tide_draw_set_camera_3d(tide_draw_list *d, const tide_float3 position, const tide_quaternion rotation,
                             const float field_of_view)
{
    camera_3d(d, tide_draw_camera_3d_matrix(position, rotation, field_of_view), true);
}

void tide_draw_unset_camera_3d(tide_draw_list *d)
{
    d->place.camera_3d = 0;
    refresh(d);
}

void tide_draw_set_camera_matrices(tide_draw_list *d, const tide_float4x4 transform, const tide_float4x4 projection)
{
    camera_3d(d, tide_mul_f4x4(projection, turn_z(tide_inverse_f4x4(transform))), false);
}

uint32_t tide_draw_push_camera_3d(tide_draw_list *d, const tide_float3 position, const tide_quaternion rotation,
                                  const float field_of_view)
{
    const uint32_t mark = d->saved_count;
    save(d, TIDE_DRAW_SAVED_CAMERA_3D)->value = d->place.camera_3d;
    tide_draw_set_camera_3d(d, position, rotation, field_of_view);
    return mark;
}

uint32_t tide_draw_push_camera_matrices(tide_draw_list *d, const tide_float4x4 transform, const tide_float4x4 projection)
{
    const uint32_t mark = d->saved_count;
    save(d, TIDE_DRAW_SAVED_CAMERA_3D)->value = d->place.camera_3d;
    tide_draw_set_camera_matrices(d, transform, projection);
    return mark;
}

// The meshes are found by whose they are and their version, in a table of
// places that's at most half full.
static uint32_t mesh_hash(const uint32_t space, const uint64_t id, const uint64_t other, const uint64_t version)
{
    uint64_t h = id * 0x9E3779B97F4A7C15ull ^ other * 0xC2B2AE3D27D4EB4Full ^ version * 0x165667B19E3779F9ull ^ space;
    h ^= h >> 32;
    h *= 0xD6E8FEB86659FD93ull;
    h ^= h >> 32;
    return (uint32_t)h;
}

static void index_mesh(tide_draw_list *d, const uint32_t place)
{
    const tide_draw_mesh_data *m = &d->meshes[place];
    const uint32_t mask = d->mesh_index_capacity - 1u;
    uint32_t i = mesh_hash(m->space, m->id, m->other, m->version) & mask;
    while (d->mesh_index[i]) i = (i + 1u) & mask;
    d->mesh_index[i] = place + 1u;
}

static void reindex_meshes(tide_draw_list *d)
{
    uint32_t capacity = d->mesh_index_capacity ? d->mesh_index_capacity : 64u;
    while (capacity / 2u <= d->mesh_count) {
        if (capacity > UINT32_MAX / 2u / sizeof(uint32_t)) tide_out_of_memory();
        capacity *= 2u;
    }
    if (capacity != d->mesh_index_capacity) {
        free(d->mesh_index);
        d->mesh_index = tide_alloc_zeroed(capacity, sizeof(uint32_t));
        d->mesh_index_capacity = capacity;
    } else {
        memset(d->mesh_index, 0, (size_t)capacity * sizeof(uint32_t));
    }
    for (uint32_t i = 0; i < d->mesh_count; i++) {
        if (d->meshes[i].space != TIDE_MESH_NONE) index_mesh(d, i);
    }
}

uint32_t tide_draw_mesh_slot(tide_draw_list *d, const uint32_t space, const uint64_t id, const uint64_t other,
                             const uint64_t version, bool *fill)
{
    *fill = false;
    if (space == TIDE_MESH_NONE) return 0;
    if (d->mesh_index_capacity) {
        const uint32_t mask = d->mesh_index_capacity - 1u;
        for (uint32_t i = mesh_hash(space, id, other, version) & mask; d->mesh_index[i]; i = (i + 1u) & mask) {
            tide_draw_mesh_data *m = &d->meshes[d->mesh_index[i] - 1u];
            if (m->space == space && m->id == id && m->other == other && m->version == version) {
                m->frame = d->frame;
                return d->mesh_index[i];
            }
        }
    }
    uint32_t place;
    if (d->mesh_free) {
        place = d->mesh_free - 1u;
        d->mesh_free = d->meshes[place].next_free;
    } else {
        d->meshes = grow(d->meshes, d->mesh_count, &d->mesh_capacity, 1, sizeof(tide_draw_mesh_data), 16u);
        place = d->mesh_count++;
    }
    d->meshes[place] = (tide_draw_mesh_data){
        .id = id, .other = other, .version = version, .space = space, .frame = d->frame, .epoch = d->epoch};
    if (d->mesh_count >= d->mesh_index_capacity / 2u) reindex_meshes(d);
    else index_mesh(d, place);
    *fill = true;
    return place + 1u;
}

void tide_draw_mesh_fill_bytes(tide_draw_list *d, const uint32_t mesh, const void *vertices, const uint32_t vertex_size,
                               const uint32_t vertex_count, const uint32_t *indices, const uint32_t index_count)
{
    if (mesh == 0 || mesh > d->mesh_count || d->meshes[mesh - 1u].space == TIDE_MESH_NONE) return;
    tide_draw_mesh_data *m = &d->meshes[mesh - 1u];
    m->vertex_count = m->index_count = 0;
    m->vertex_size = vertex_size;
    if (vertex_count == 0 || vertex_size == 0 || index_count < 3u) return;
    m->bytes = grow(m->bytes, 0, &m->vertex_capacity, (uint64_t)vertex_count * vertex_size, 1, 64u * vertex_size);
    memcpy(m->bytes, vertices, (size_t)vertex_count * vertex_size);
    m->vertex_count = vertex_count;
    // A triangle with an index past the vertices is left out
    m->indices = grow(m->indices, 0, &m->index_capacity, index_count, sizeof(uint32_t), 192u);
    for (uint32_t i = 0; i + 3u <= index_count; i += 3u) {
        const uint32_t a = indices[i], b = indices[i + 1u], c = indices[i + 2u];
        if (a >= vertex_count || b >= vertex_count || c >= vertex_count) continue;
        uint32_t *out = &m->indices[m->index_count];
        out[0] = a;
        out[1] = b;
        out[2] = c;
        m->index_count += 3u;
    }
}

void tide_draw_mesh_fill(tide_draw_list *d, const uint32_t mesh, const tide_vertex3 *vertices,
                         const uint32_t vertex_count, const uint32_t *indices, const uint32_t index_count)
{
    tide_draw_mesh_fill_bytes(d, mesh, vertices, sizeof(tide_vertex3), vertex_count, indices, index_count);
}

static bool same_color(const tide_color a, const tide_color b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

void tide_draw_shaded(tide_draw_list *d, const tide_shader *shader, const uint32_t mesh, const void *instance,
                      const void *uniforms, const uint32_t *textures, const uint32_t blended)
{
    tide_draw_shaded_instances(d, shader, mesh, instance, 1, uniforms, textures, blended);
}

// A run of instances through a shader. `with` says what tints it and, for a
// 2D one, what it goes through before the camera: the rest of it is made here.
// `through` is what a 3D one's instances go through on their way to the
// world, or NULL, and `depth` what a 3D one does with depth: a 2D one, and a
// blit, have none.
static void shaded_instances(tide_draw_list *d, const tide_shader *shader, const uint32_t mesh, const void *instances,
                             const uint32_t count, const void *uniforms, const uint32_t *textures,
                             const uint32_t blended, const tide_draw_kind kind, const tide_blend blend,
                             const tide_depth depth, const tide_draw_run *with, const tide_float4x4 *through)
{
    if (count == 0 || !shader || mesh == 0 || mesh > d->mesh_count) return;
    const tide_draw_mesh_data *m = &d->meshes[mesh - 1u];
    if (m->space != TIDE_MESH_BYTES || m->index_count == 0 || m->vertex_size != shader->vertex_size) return;
    const uint32_t instance_size = tide_shader_instance_size(shader);
    const uint32_t uniform_size = tide_shader_uniform_size(shader);
    if ((instance_size && !instances) || (uniform_size && !uniforms)) return; // Nothing to draw it with
    const uint32_t texture_count = shader->texture_count < TIDE_SHADER_TEXTURES ? shader->texture_count : TIDE_SHADER_TEXTURES;
    tide_draw_run run = {.shader = shader, .mesh = mesh, .first = d->instance_used, .count = count, .blended = blended,
                         .tint = with->tint, .moved = with->moved, .transform = with->transform};
    for (uint32_t i = 0; i < texture_count; i++) {
        if (textures && textures[i] <= d->texture_count) run.textures[i] = textures[i];
    }
    const uint64_t bytes = (uint64_t)instance_size * count;
    d->instances = grow(d->instances, d->instance_used, &d->instance_capacity, bytes, 1, 4096u);
    if (bytes) memcpy(d->instances + d->instance_used, instances, (size_t)bytes);
    if (through && shader->dims == 3 && shader->instance_floats >= 4u) { // Each one's transform starts it
        for (uint32_t i = 0; i < count; i++) {
            tide_float4x4 own;
            uint8_t *at = d->instances + d->instance_used + (size_t)i * instance_size;
            memcpy(&own, at, sizeof own);
            own = tide_mul_f4x4(*through, own);
            memcpy(at, &own, sizeof own);
        }
    }
    d->instance_used += (uint32_t)bytes;
    // More instances of the run before it, when nothing came between them and they draw with the same
    tide_draw_command *last = d->count && !d->unsettled ? &d->commands[d->count - 1u] : NULL;
    // What has no depth says 0 whatever is set, so the setting never comes between two 2D runs
    const uint8_t deep = kind == TIDE_DRAW_SHADED && shader->dims == 3 && (uint32_t)depth <= TIDE_DEPTH_NONE ? (uint8_t)depth : 0;
    if (kind == TIDE_DRAW_SHADED && last && last->kind == TIDE_DRAW_SHADED && last->blend == (uint8_t)blend
        && last->depth == deep) {
        tide_draw_run *r = &d->runs[last->shaded.run];
        if ((r->shader == shader || (shader->id && r->shader && r->shader->id == shader->id))
            && r->mesh == mesh && r->blended == blended
            && r->first + (uint64_t)r->count * instance_size == run.first
            && memcmp(r->textures, run.textures, sizeof run.textures) == 0
            && same_color(r->tint, run.tint) && r->moved == run.moved
            && (!run.moved || memcmp(&r->transform, &run.transform, sizeof run.transform) == 0)
            && (uniform_size == 0 || memcmp(d->uniforms + r->uniforms, uniforms, uniform_size) == 0)) {
            if (r->count > UINT32_MAX - count) tide_out_of_memory();
            r->count += count;
            return;
        }
    }
    run.uniforms = d->uniform_used;
    d->uniforms = grow(d->uniforms, d->uniform_used, &d->uniform_capacity, uniform_size, 1, 1024u);
    if (uniform_size) memcpy(d->uniforms + d->uniform_used, uniforms, uniform_size);
    d->uniform_used += uniform_size;
    d->runs = grow(d->runs, d->run_count, &d->run_capacity, 1, sizeof(tide_draw_run), 64u);
    d->runs[d->run_count] = run;
    tide_draw_command *c = push(d, kind, tide_f2(0.0f, 0.0f), tide_f2(0.0f, 0.0f), TIDE_COLOR_CLEAR);
    c->blend = (uint8_t)blend;
    c->depth = deep;
    c->shaded.run = d->run_count++;
}

void tide_draw_shaded_instances(tide_draw_list *d, const tide_shader *shader, const uint32_t mesh, const void *instances,
                                const uint32_t count, const void *uniforms, const uint32_t *textures,
                                const uint32_t blended)
{
    const tide_draw_place *p = &d->place;
    tide_draw_run with = {.tint = tide_draw_tint(d)};
    if (shader && shader->dims == 2 && (p->moved & TIDE_DRAW_MOVED)) {
        with.moved = 1;
        with.transform = p->transform;
    }
    shaded_instances(d, shader, mesh, instances, count, uniforms, textures, blended, TIDE_DRAW_SHADED,
                     (tide_blend)d->blend, (tide_depth)d->depth, &with,
                     p->moved & TIDE_DRAW_MOVED_3D ? &p->transform_3d : NULL);
}

void tide_draw_blit(tide_draw_list *d, const tide_shader *shader, const tide_blend blend, const void *instance,
                   const void *uniforms, const uint32_t *textures, const uint32_t blended)
{
    if (!shader || shader->dims != 2 || shader->vertex_size != sizeof(tide_vertex)) return;
    for (uint32_t i = 0; textures && i < shader->texture_count && i < TIDE_SHADER_TEXTURES; i++) {
        const uint32_t slot = textures[i];
        if (!slot || slot > d->texture_count || !d->target) continue;
        const tide_draw_texture *t = &d->textures[slot - 1u];
        if (t->space == TIDE_PIXELS_TARGET && t->id == d->targets[d->target - 1u].id) {
            fprintf(stderr, "Draw.Blit cannot sample its destination\n");
            return;
        }
    }
    static const tide_vertex vertices[] = {
        {{-1, 1}, {0, 0}, {1, 1, 1, 1}}, {{-1, -1}, {0, 1}, {1, 1, 1, 1}},
        {{1, -1}, {1, 1}, {1, 1, 1, 1}}, {{1, 1}, {1, 0}, {1, 1, 1, 1}},
    };
    static const uint32_t indices[] = {0, 1, 2, 0, 2, 3};
    bool fill;
    const uint32_t mesh = tide_draw_mesh_slot(d, TIDE_MESH_BYTES, (uint64_t)(uintptr_t)vertices,
                                              (uint64_t)(uintptr_t)indices, 1, &fill);
    if (fill) tide_draw_mesh_fill_bytes(d, mesh, vertices, sizeof(tide_vertex), 4, indices, 6);
    // None of the settings is a blit's: it's the whole destination, as its frag writes it
    const tide_draw_run with = {.tint = TIDE_COLOR_WHITE};
    shaded_instances(d, shader, mesh, instance, 1, uniforms, textures, blended, TIDE_DRAW_BLIT,
                     (uint32_t)blend <= TIDE_BLEND_REPLACE ? blend : TIDE_BLEND_REPLACE, TIDE_DEPTH_READ_WRITE, &with,
                     NULL);
}

// A run of instances of a 3D mesh, tinted by `tint`: `through` is what they
// go through on their way to the world, or NULL. Part of what calls it, as a
// frame of instances spends its time here.
static inline __attribute__((always_inline)) void mesh_instances(tide_draw_list *d, const uint32_t mesh, const tide_float4x4 *transforms, const uint32_t count,
                           const uint32_t texture, const tide_filter filter, const tide_blend blend,
                           const tide_depth depth, const tide_color tint, const tide_float4x4 *through)
{
    if (count == 0 || !transforms) return;
    if (mesh == 0 || mesh > d->mesh_count || d->meshes[mesh - 1u].index_count == 0 || texture > d->texture_count) return;
    if (d->meshes[mesh - 1u].space == TIDE_MESH_BYTES) return; // A shader's vertices, not tide_vertex3s
    // What its destination's settings still had to say first: a camera's matrix would come between its transforms
    if (d->unsettled) settle(d);
    const uint32_t at = d->matrix_count;
    d->matrices = grow(d->matrices, d->matrix_count, &d->matrix_capacity, count, sizeof(tide_float4x4), 256u);
    memcpy(d->matrices + at, transforms, (size_t)count * sizeof(tide_float4x4));
    for (uint32_t i = 0; through && i < count; i++) d->matrices[at + i] = tide_mul_f4x4(*through, d->matrices[at + i]);
    d->matrix_count += count;
    // More instances of the mesh before it, when nothing came between them
    tide_draw_command *last = d->count ? &d->commands[d->count - 1u] : NULL;
    if (last && last->kind == TIDE_DRAW_MESH_3D && last->blend == (uint8_t)blend && last->depth == (uint8_t)depth
        && last->mesh3.mesh == mesh && last->mesh3.texture == texture && last->mesh3.filter == (uint32_t)filter
        && last->mesh3.first + last->mesh3.count == at && same_color(last->color, tint)) {
        last->mesh3.count += count;
        return;
    }
    tide_draw_command *c = push(d, TIDE_DRAW_MESH_3D, tide_f2(0.0f, 0.0f), tide_f2(0.0f, 0.0f), tint);
    c->blend = (uint8_t)blend;
    c->depth = (uint8_t)depth;
    c->mesh3.mesh = mesh;
    c->mesh3.first = at;
    c->mesh3.count = count;
    c->mesh3.texture = texture;
    c->mesh3.filter = (uint32_t)filter;
}

void tide_draw_instance(tide_draw_list *d, const uint32_t mesh, const tide_float4x4 transform, const uint32_t texture,
                        const tide_filter filter)
{
    tide_draw_instances(d, mesh, &transform, 1, texture, filter);
}

void tide_draw_instances(tide_draw_list *d, const uint32_t mesh, const tide_float4x4 *transforms, const uint32_t count,
                         const uint32_t texture, const tide_filter filter)
{
    const tide_draw_place *p = &d->place;
    mesh_instances(d, mesh, transforms, count, texture, filter, (tide_blend)d->blend, (tide_depth)d->depth,
                   tide_draw_tint(d), p->moved & TIDE_DRAW_MOVED_3D ? &p->transform_3d : NULL);
}

uint64_t tide_mesh_version(const tide_mesh *mesh)
{
    if (!mesh || !mesh->vertices || !mesh->indices) return 0;
    uint64_t h = tide_hash_more(TIDE_HASH_START, &mesh->vertex_count, sizeof mesh->vertex_count);
    h = tide_hash_more(h, &mesh->index_count, sizeof mesh->index_count);
    h = tide_hash_more(h, mesh->vertices, (size_t)mesh->vertex_count * sizeof(tide_vertex3));
    return tide_hash_end(tide_hash_more(h, mesh->indices, (size_t)mesh->index_count * sizeof(uint32_t)));
}

void tide_draw_mesh_3d(tide_draw_list *d, const tide_mesh *mesh, const tide_float4x4 transform,
                       const tide_texture *texture, const tide_filter filter)
{
    if (!mesh || !mesh->vertices || !mesh->indices) return;
    bool fill;
    const uint32_t slot = tide_draw_mesh_slot(d, TIDE_MESH_MEMORY, (uint64_t)(uintptr_t)mesh->vertices,
                                              (uint64_t)(uintptr_t)mesh->indices, mesh->version, &fill);
    if (fill) tide_draw_mesh_fill(d, slot, mesh->vertices, mesh->vertex_count, mesh->indices, mesh->index_count);
    tide_draw_instance(d, slot, transform, texture_slot(d, texture), filter);
}

// ---------------------------------------------------------------------------
// One list after another

// The texture of `from`'s mesh command, as `d` has it.
static uint32_t append_texture(tide_draw_list *d, const tide_draw_list *from, const uint32_t texture)
{
    if (!texture) return 0;
    const tide_draw_texture *t = &from->textures[texture - 1u];
    if (t->space == TIDE_PIXELS_TARGET) return target_texture_version(d, t->id, t->target, t->version);
    uint8_t *pixels;
    const uint32_t slot = tide_draw_texture_slot(d, t->space, t->id, t->version, t->width, t->height, &pixels);
    if (pixels) memcpy(pixels, t->pixels, (size_t)t->width * (size_t)t->height * 4u);
    return slot;
}

// `from`'s 2D camera `camera` (1 + its place, or 0), as `d` has it.
static uint32_t append_camera(tide_draw_list *d, const tide_draw_list *from, const uint32_t camera)
{
    return camera && camera <= from->camera_count ? camera_slot(d, from->cameras[camera - 1u]) : 0;
}

// `from`'s clip `clip` (1 + its place, or 0), as `d` has it: `d->remap` has the ones it has already.
static uint32_t append_clip(tide_draw_list *d, const tide_draw_list *from, const uint32_t clip)
{
    if (!clip || clip > from->clip_count) return 0;
    if (d->remap[clip - 1u]) return d->remap[clip - 1u];
    const tide_draw_clip_data *c = &from->clips[clip - 1u];
    // The clip it's inside is before it in `from`, so this ends
    const uint32_t parent = c->parent < clip ? append_clip(d, from, c->parent) : 0;
    const uint32_t camera = append_camera(d, from, c->camera);
    d->clips = grow(d->clips, d->clip_count, &d->clip_capacity, 1, sizeof(tide_draw_clip_data), 16u);
    d->clips[d->clip_count] = (tide_draw_clip_data){.rect = c->rect, .camera = camera, .parent = parent};
    return d->remap[clip - 1u] = ++d->clip_count;
}

// What `from` had its destination's cameras and clip as, for `d`'s selected one.
static void append_place(tide_draw_list *d, const tide_draw_list *from, const tide_draw_place *place)
{
    tide_draw_place *p = &d->place;
    p->camera_2d = append_camera(d, from, place->camera_2d);
    if (place->camera_3d && place->camera_3d <= from->camera_3d_count) {
        const tide_draw_camera_3d_data *c = &from->cameras_3d[place->camera_3d - 1u];
        camera_3d(d, c->matrix, c->fit != 0);
    } else {
        p->camera_3d = 0;
    }
    p->clip = append_clip(d, from, place->clip);
    refresh(d);
}

void tide_draw_append(tide_draw_list *d, const tide_draw_list *from)
{
    tide_draw_defaults(d);
    reserve(d, from->count);
    d->remap = grow(d->remap, 0, &d->remap_capacity, from->clip_count, sizeof(uint32_t), 16u);
    if (from->clip_count) memset(d->remap, 0, (size_t)from->clip_count * sizeof(uint32_t));
    const uint32_t base = tide_draw_vertices(d, from->vertices, from->vertex_count);
    for (uint32_t i = 0; i < from->count; i++) {
        const tide_draw_command *c = &from->commands[i];
        d->blend = c->blend;
        if (c->kind == TIDE_DRAW_TARGET) {
            if (c->target > from->target_count) {
                draw_to_version(d, TIDE_RENDER_INVALID, (tide_target_size){0}, 0);
                continue;
            }
            const tide_draw_target_data *t = c->target ? &from->targets[c->target - 1u] : NULL;
            draw_to_version(d, t ? t->id : 0, t ? t->size : (tide_target_size){0}, t ? t->version : 0);
        } else if (c->kind == TIDE_DRAW_CAMERA || c->kind == TIDE_DRAW_SCREEN) {
            // What `from` plays is what `d` has from here on: the next command that draws records it
            d->place.camera_2d = camera_slot(d, (tide_draw_camera_data){.center = c->a, .size = c->b.x,
                                                                        .screen = c->kind == TIDE_DRAW_SCREEN});
            refresh(d);
        } else if (c->kind == TIDE_DRAW_CAMERA_3D) {
            if (c->camera.matrix == TIDE_DRAW_FIRST_CAMERA) tide_draw_unset_camera_3d(d);
            else if (c->camera.matrix < from->matrix_count) camera_3d(d, from->matrices[c->camera.matrix], c->camera.fit != 0);
        } else if (c->kind == TIDE_DRAW_CLIP || c->kind == TIDE_DRAW_NO_CLIP) {
            d->place.clip = c->kind == TIDE_DRAW_CLIP ? append_clip(d, from, c->clip) : 0;
            refresh(d);
        } else if (c->kind == TIDE_DRAW_TEXT) {
            const uint32_t turned = c->words.turned;
            push_text(d, from->text + c->text, c->a, c->b.x, c->color, (tide_anchor)c->words.anchor,
                      c->words.bold ? from->bold : TIDE_FONT_REGULAR,
                      turned && turned <= from->turn_count ? &from->turns[turned - 1u] : NULL);
        } else if (c->kind == TIDE_DRAW_MESH) {
            // Its indices are places in `from`'s vertices, which are `d`'s from `base`
            tide_draw_triangles_with(d, base, from->indices + c->mesh.first, c->mesh.count,
                                     append_texture(d, from, c->mesh.texture), (tide_filter)c->mesh.filter);
        } else if (c->kind == TIDE_DRAW_MESH_3D) {
            if (!c->mesh3.mesh || c->mesh3.count == 0) continue;
            const tide_draw_mesh_data *m = &from->meshes[c->mesh3.mesh - 1u];
            bool fill;
            const uint32_t slot = tide_draw_mesh_slot(d, m->space, m->id, m->other, m->version, &fill);
            if (fill) tide_draw_mesh_fill_bytes(d, slot, m->bytes, m->vertex_size, m->vertex_count, m->indices, m->index_count);
            const uint32_t texture = append_texture(d, from, c->mesh3.texture);
            mesh_instances(d, slot, from->matrices + c->mesh3.first, c->mesh3.count, texture,
                           (tide_filter)c->mesh3.filter, (tide_blend)c->blend,
                           c->depth <= TIDE_DEPTH_NONE ? (tide_depth)c->depth : TIDE_DEPTH_READ_WRITE, c->color, NULL);
        } else if (c->kind == TIDE_DRAW_SHADED || c->kind == TIDE_DRAW_BLIT) {
            const tide_draw_run *r = &from->runs[c->shaded.run];
            if (!r->shader || !r->mesh || r->count == 0) continue;
            const tide_draw_mesh_data *m = &from->meshes[r->mesh - 1u];
            bool fill;
            const uint32_t slot = tide_draw_mesh_slot(d, m->space, m->id, m->other, m->version, &fill);
            if (fill) tide_draw_mesh_fill_bytes(d, slot, m->bytes, m->vertex_size, m->vertex_count, m->indices, m->index_count);
            uint32_t textures[TIDE_SHADER_TEXTURES];
            for (uint32_t t = 0; t < TIDE_SHADER_TEXTURES; t++) textures[t] = append_texture(d, from, r->textures[t]);
            const uint32_t size = tide_shader_instance_size(r->shader);
            shaded_instances(d, r->shader, slot, size ? from->instances + r->first : NULL, r->count,
                             tide_shader_uniform_size(r->shader) ? from->uniforms + r->uniforms : NULL, textures,
                             r->blended, (tide_draw_kind)c->kind, (tide_blend)c->blend, (tide_depth)c->depth, r, NULL);
        } else if (c->kind == TIDE_DRAW_SHAPE) {
            if (c->shape < from->shape_count) push_shape(d, c->a, c->b, c->color, from->shapes[c->shape]);
        } else {
            push(d, (tide_draw_kind)c->kind, c->a, c->b, c->color);
        }
    }
    d->blend = TIDE_BLEND_ALPHA;
    // What `from` set after the last command that drew isn't in its commands: its destinations' places have it
    const uint32_t ended = d->target;
    for (uint32_t i = 0; i <= from->target_count; i++) {
        const tide_draw_place *place = i == from->target ? &from->place : i ? &from->targets[i - 1u].place : &from->window;
        if (place->camera_2d == place->played_2d && place->camera_3d == place->played_3d && place->clip == place->played_clip) {
            continue;
        }
        const tide_draw_target_data *t = i ? &from->targets[i - 1u] : NULL;
        draw_to_version(d, t ? t->id : 0, t ? t->size : (tide_target_size){0}, t ? t->version : 0);
        append_place(d, from, place);
    }
    if (d->target != ended) {
        const tide_draw_target_data *t = ended ? &d->targets[ended - 1u] : NULL;
        draw_to_version(d, t ? t->id : 0, t ? t->size : (tide_target_size){0}, t ? t->version : 0);
    }
}
