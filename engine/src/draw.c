#include "tide/draw.h"

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "tide/mesh.h"
#include "tide/net.h"
#include "tide/pack.h"
#include "tide/page.h"
#include "skinned_cache.h"

static void reindex_meshes(tide_draw_list *d);
static void reindex_textures(tide_draw_list *d);

static tide_render_target_state *render_targets;

_Static_assert(TIDE_TARGET_COLORS * 3 == TIDE_RENDER_IMAGE_SHIFT && TIDE_TARGET_RGBA16F + 1 <= 7
               && TIDE_TARGET_COLORS - 1 <= (int)(TIDE_RENDER_IMAGE >> TIDE_RENDER_IMAGE_SHIFT),
               "a target's id has 3 bits for each color image's format, then which image a texture names");
_Static_assert(TIDE_RENDER_SAMPLES_SHIFT == TIDE_RENDER_IMAGE_SHIFT + 2 && (1 << 3) == TIDE_TARGET_SAMPLES
               && TIDE_RENDER_BITS == UINT64_C(0xFFFF),
               "...then 2 bits for its samples, as a power of two: all of it in what a uint16_t holds");

// A managed target's id: its state's address, and what it holds.
static uint64_t managed_id(const tide_render_target_state *s)
{
    return TIDE_RENDER_MANAGED | (uint64_t)(uintptr_t)s | s->holds | (s->cube ? TIDE_RENDER_CUBE : 0u);
}

// What a target of these color images holds, as its id's bits say it: for
// each in order, 1 + its format, which is a color's (anything but
// TIDE_TARGET_RGBA16F is TIDE_TARGET_RGBA8).
static uint16_t holds_of(const tide_target_formats formats)
{
    uint16_t holds = 0;
    for (uint32_t i = 0; i < formats.count && i < TIDE_TARGET_COLORS; i++) {
        const uint32_t format = formats.color[i] == TIDE_TARGET_RGBA16F ? TIDE_TARGET_RGBA16F : TIDE_TARGET_RGBA8;
        holds |= (uint16_t)((format + 1u) << (3u * i));
    }
    return holds;
}

// What a target made with one format holds: one color image of it, or none for
// TIDE_TARGET_DEPTH. A format there's none of is TIDE_TARGET_RGBA8.
static tide_target_formats one_format(const tide_target_format format)
{
    if (format == TIDE_TARGET_DEPTH) return (tide_target_formats){0};
    return (tide_target_formats){.count = 1, .color = {(uint8_t)(format == TIDE_TARGET_RGBA16F ? TIDE_TARGET_RGBA16F : TIDE_TARGET_RGBA8)}};
}

// ...and one made with `count` of them, each a color image's.
static tide_target_formats several_formats(const tide_target_format *formats, const uint32_t count)
{
    if (!formats || count == 0) return one_format(TIDE_TARGET_RGBA8);
    if (count == 1) return one_format(formats[0]);
    tide_target_formats held = {.count = (uint8_t)(count < TIDE_TARGET_COLORS ? count : (uint32_t)TIDE_TARGET_COLORS)};
    if (count > TIDE_TARGET_COLORS) {
        fprintf(stderr, "RenderTarget: a target holds %d color images at the most, and this asked for %u: it has the "
                        "first %d\n", TIDE_TARGET_COLORS, (unsigned)count, TIDE_TARGET_COLORS);
    }
    for (uint32_t i = 0; i < held.count; i++) {
        if (formats[i] == TIDE_TARGET_DEPTH) {
            fprintf(stderr, "RenderTarget: Format.Depth is the format of a target with no color image, by itself: "
                            "among several it's a Format.Rgba8 image\n");
        }
        held.color[i] = (uint8_t)(formats[i] == TIDE_TARGET_RGBA16F ? TIDE_TARGET_RGBA16F : TIDE_TARGET_RGBA8);
    }
    return held;
}

// The bits of a target's id for `samples` a pixel: 1, 2, 4 or 8, any other
// number being the one of those under it (1 for what's under 1), having said
// so. A target with no color image has 1: nothing reads a multisampled depth.
static uint16_t samples_of(const int32_t samples, const tide_target_formats formats)
{
    uint32_t power = 0;
    while (power < 3u && (2 << power) <= samples) power++;
    if (samples != 1 << power) {
        fprintf(stderr, "RenderTarget: a target's samples are 1, 2, 4 or 8, and this asked for %d: it has %d\n",
                (int)samples, 1 << power);
    }
    if (power && formats.count == 0) {
        fprintf(stderr, "RenderTarget: a target of depth alone (Format.Depth) has one sample a pixel, and this asked "
                        "for %d: a multisampled target's depth can't be read, so nothing could be read of it\n",
                1 << power);
        power = 0;
    }
    return (uint16_t)(power << TIDE_RENDER_SAMPLES_SHIFT);
}

// A managed target of `size` that holds `formats`, or a dead one, having said
// `why`, for a size there's no image of. Its state is on a boundary that
// leaves its id the bits what it holds goes in. `sampled` is its samples, as
// those bits (samples_of).
static tide_render_target make_target_sampled(const tide_target_size size, const tide_target_formats formats,
                                              const uint16_t sampled, const char *why)
{
    void *memory = tide_alloc_zeroed(1, sizeof(tide_render_target_state) + (size_t)TIDE_RENDER_BITS);
    tide_render_target_state *s = (tide_render_target_state *)(((uintptr_t)memory + (uintptr_t)TIDE_RENDER_BITS)
                                                               & ~(uintptr_t)TIDE_RENDER_BITS);
    *s = (tide_render_target_state){.next = render_targets, .owner = &render_targets, .size = size, .version = 1,
                                    .memory = memory, .alive = tide_target_size_valid(size),
                                    .holds = (uint16_t)(holds_of(formats) | sampled)};
    render_targets = s;
    if (!s->alive) {
        s->size = (tide_target_size){0};
        fprintf(stderr, "%s\n", why);
    }
    return (tide_render_target){.id = managed_id(s), .size = s->size};
}

static tide_render_target make_target(const tide_target_size size, const tide_target_formats formats, const char *why)
{
    return make_target_sampled(size, formats, 0, why);
}

static tide_render_target make_sized(const float width, const float height, const tide_target_formats formats)
{
    return make_target((tide_target_size){.width = width, .height = height}, formats, "RenderTarget requires positive dimensions");
}

static tide_render_target make_scaled(const float width, const float height, const float scale,
                                      const tide_target_formats formats)
{
    // A scale of 0 is the display's in a size, which this never asks for
    const tide_target_size size = {.width = scale > 0.0f ? width : 0.0f, .height = height, .scale = scale};
    return make_target(size, formats, "RenderTarget requires positive dimensions and a positive number of pixels per unit");
}

static tide_render_target make_screen(const float factor, const tide_target_formats formats)
{
    const tide_target_size size = {.window = factor > 0.0f ? factor : 0.0f};
    return make_target(size, formats, "RenderTarget.Window requires a positive factor of the window's size");
}

tide_render_target tide_render_target_make_as(const float width, const float height, const tide_target_format format)
{
    return make_sized(width, height, one_format(format));
}

tide_render_target tide_render_target_make_scaled_as(const float width, const float height, const float scale,
                                                     const tide_target_format format)
{
    return make_scaled(width, height, scale, one_format(format));
}

tide_render_target tide_render_target_screen_as(const float factor, const tide_target_format format)
{
    return make_screen(factor, one_format(format));
}

tide_render_target tide_render_target_window_as(const tide_target_format format)
{
    return tide_render_target_screen_as(1.0f, format);
}

tide_render_target tide_render_target_make_of(const float width, const float height, const tide_target_format *formats,
                                              const uint32_t count)
{
    return make_sized(width, height, several_formats(formats, count));
}

tide_render_target tide_render_target_make_scaled_of(const float width, const float height, const float scale,
                                                     const tide_target_format *formats, const uint32_t count)
{
    return make_scaled(width, height, scale, several_formats(formats, count));
}

tide_render_target tide_render_target_screen_of(const float factor, const tide_target_format *formats, const uint32_t count)
{
    return make_screen(factor, several_formats(formats, count));
}

tide_render_target tide_render_target_window_of(const tide_target_format *formats, const uint32_t count)
{
    return tide_render_target_screen_of(1.0f, formats, count);
}

tide_render_target tide_render_target_make_sampled(const float width, const float height, const tide_target_format *formats,
                                                   const uint32_t count, const int32_t samples)
{
    const tide_target_formats held = several_formats(formats, count);
    return make_target_sampled((tide_target_size){.width = width, .height = height}, held, samples_of(samples, held),
                               "RenderTarget requires positive dimensions");
}

tide_render_target tide_render_target_make_scaled_sampled(const float width, const float height, const float scale,
                                                          const tide_target_format *formats, const uint32_t count,
                                                          const int32_t samples)
{
    const tide_target_formats held = several_formats(formats, count);
    const tide_target_size size = {.width = scale > 0.0f ? width : 0.0f, .height = height, .scale = scale};
    return make_target_sampled(size, held, samples_of(samples, held),
                               "RenderTarget requires positive dimensions and a positive number of pixels per unit");
}

tide_render_target tide_render_target_screen_sampled(const float factor, const tide_target_format *formats,
                                                     const uint32_t count, const int32_t samples)
{
    const tide_target_formats held = several_formats(formats, count);
    return make_target_sampled((tide_target_size){.window = factor > 0.0f ? factor : 0.0f}, held, samples_of(samples, held),
                               "RenderTarget.Window requires a positive factor of the window's size");
}

tide_render_target tide_render_target_window_sampled(const tide_target_format *formats, const uint32_t count,
                                                     const int32_t samples)
{
    return tide_render_target_screen_sampled(1.0f, formats, count, samples);
}

tide_render_target tide_render_target_make(const float width, const float height)
{
    return tide_render_target_make_as(width, height, TIDE_TARGET_RGBA8);
}

tide_render_target tide_render_target_make_scaled(const float width, const float height, const float scale)
{
    return tide_render_target_make_scaled_as(width, height, scale, TIDE_TARGET_RGBA8);
}

tide_render_target tide_render_target_screen(const float factor)
{
    return tide_render_target_screen_as(factor, TIDE_TARGET_RGBA8);
}

tide_render_target tide_render_target_window(void)
{
    return tide_render_target_window_as(TIDE_TARGET_RGBA8);
}

void tide_render_target_resize(const tide_render_target target, const float width, const float height)
{
    tide_render_target_state *s = tide_render_target_state_of(target.id);
    if (s && s->cube) {
        fprintf(stderr, "RenderTarget.Resize: a cube's face has its cube's size: 'cube.Resize(size)' resizes all six\n");
        return;
    }
    if (s && s->alive && s->size.window > 0.0f) {
        fprintf(stderr, "RenderTarget.Resize: a target made with RenderTarget.Window follows the window's size, and "
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
    if (s && s->cube) fprintf(stderr, "RenderTarget.Dispose: a cube's face goes with its cube: 'cube.Dispose()' lets all six go\n");
    else if (s) s->alive = false;
    else if (target.id) fprintf(stderr, "RenderTarget.Dispose requires an owned target\n");
}

tide_cubemap_ref tide_cubemap_make(const float size)
{
    return tide_cubemap_make_as(size, TIDE_TARGET_RGBA8);
}

tide_cubemap_ref tide_cubemap_make_as(const float size, const tide_target_format format)
{
    const tide_render_target made = make_target((tide_target_size){.width = size, .height = size, .scale = 1.0f},
                                                one_format(format == TIDE_TARGET_RGBA16F ? TIDE_TARGET_RGBA16F : TIDE_TARGET_RGBA8),
                                                "Cubemap(size) requires a positive size, its faces' side in pixels");
    tide_render_target_state *s = tide_render_target_state_of(made.id);
    if ((uint64_t)(uintptr_t)s & (TIDE_RENDER_CUBE | TIDE_RENDER_FACE)) { // Its id says which face there
        fprintf(stderr, "tide: a cube's state is at an address past what its id has room for\n");
        abort();
    }
    s->cube = true;
    return (tide_cubemap_ref){.target = {.id = managed_id(s), .size = s->size}};
}

void tide_cubemap_resize(const tide_cubemap_ref cube, const float size)
{
    tide_render_target_state *s = tide_render_target_state_of(cube.target.id);
    const tide_target_size sized = {.width = size, .height = size, .scale = 1.0f};
    if (!s || !s->cube || !s->alive || !tide_target_size_valid(sized)) {
        fprintf(stderr, "Cubemap.Resize requires a live cube made with Cubemap(size) and a positive size\n");
        return;
    }
    s->size = sized;
    s->version++;
}

void tide_cubemap_dispose(const tide_cubemap_ref cube)
{
    tide_render_target_state *s = tide_render_target_state_of(cube.target.id);
    if (s && s->cube) s->alive = false;
    else fprintf(stderr, "Cubemap.Dispose requires a cube made with Cubemap(size): an asset's is the pack's\n");
}

tide_cubemap_ref tide_cubemap_recreate(const tide_cubemap_ref previous)
{
    if (!previous.target.id) return previous; // The pack's: its name is the same in every build
    return (tide_cubemap_ref){.target = tide_render_target_recreate(previous.target)};
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
    // A texture's reference names one of its target's color images, and its new one names the same; a
    // cube's face is that face of the new cube
    const uint64_t image = previous.id & (TIDE_RENDER_IMAGE | TIDE_RENDER_FACE), whole = previous.id & ~image;
    for (const tide_render_target_state *s = render_targets; s; s = s->next) {
        if (s->reload_id == whole) return (tide_render_target){.id = managed_id(s) | image, .size = s->size};
    }
    // As it was: its size, its scale, whether it follows the window, and what it holds, its samples and
    // whether it's a cube, which its id says
    tide_render_target next = make_target_sampled(previous.size, tide_render_target_formats(whole),
                                                  (uint16_t)(whole & TIDE_RENDER_SAMPLES), "");
    tide_render_target_state *s = tide_render_target_state_of(next.id);
    s->reload_id = whole;
    s->cube = (whole & TIDE_RENDER_CUBE) != 0;
    next.id = managed_id(s) | image;
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
        free(s->memory);
    }
}

// Lets a texture's pixels go, where they're the list's own copy: an image of
// the pack's is drawn where the pack has it (pack_texture), and stays there.
static void free_pixels(const tide_draw_texture *t)
{
    if (t->capacity) free(t->pixels);
    if (t->changes) {
        free(t->changes->parts);
        free(t->changes);
    }
}

void tide_draw_reset(tide_draw_list *d)
{
    tide_skinned_draw_cache_reset(d);
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
    d->unsettled = d->altered = 0;
    d->camera_count = d->camera_3d_count = d->clip_count = d->turn_count = d->saved_count = 0;
    d->bold = NULL;
    // The textures the frame drew with stay, for the next to find its pixels
    // here already
    uint32_t kept = 0;
    for (uint32_t i = 0; i < d->texture_count; i++) {
        tide_draw_texture *t = &d->textures[i];
        if (t->frame != d->frame) {
            free_pixels(t);
            continue;
        }
        d->textures[kept++] = *t;
    }
    if (kept != d->texture_count) {
        d->texture_count = kept;
        reindex_textures(d);
    }
    // ...and so do its 3D meshes, each in its place
    bool freed = false;
    for (uint32_t i = 0; i < d->mesh_count; i++) {
        tide_draw_mesh_data *m = &d->meshes[i];
        if (m->space == TIDE_MESH_NONE || m->frame == d->frame) continue;
        free(m->vertices);
        free(m->indices);
        free(m->level_indices);
        *m = (tide_draw_mesh_data){.next_free = d->mesh_free};
        d->mesh_free = i + 1u;
        freed = true;
    }
    if (freed) reindex_meshes(d);
    d->leveled = 0;
    d->frame++;
}

static void free_meshes(tide_draw_list *d)
{
    for (uint32_t i = 0; i < d->mesh_count; i++) {
        free(d->meshes[i].vertices);
        free(d->meshes[i].indices);
        free(d->meshes[i].level_indices);
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
    tide_skinned_draw_cache_free(d);
    for (uint32_t i = 0; i < d->texture_count; i++) free_pixels(&d->textures[i]);
    free_meshes(d);
    free(d->commands);
    free(d->text);
    free(d->vertices);
    free(d->indices);
    free(d->textures);
    free(d->texture_index);
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
    free(d->level_room);
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

// A texture's place stays in the array; the index only finds it. Targets can
// have several places for one id, at different sizes or versions.
static uint32_t texture_hash(const uint32_t space, const uint64_t id)
{
    uint64_t h = id * 0x9E3779B97F4A7C15ull ^ space;
    h ^= h >> 32;
    h *= 0xD6E8FEB86659FD93ull;
    h ^= h >> 32;
    return (uint32_t)h;
}

static void index_texture(tide_draw_list *d, const uint32_t place)
{
    const tide_draw_texture *t = &d->textures[place];
    const uint32_t mask = d->texture_index_capacity - 1u;
    uint32_t i = texture_hash(t->space, t->id) & mask;
    while (d->texture_index[i]) i = (i + 1u) & mask;
    d->texture_index[i] = place + 1u;
}

static void reindex_textures(tide_draw_list *d)
{
    uint32_t capacity = d->texture_index_capacity ? d->texture_index_capacity : 64u;
    while (capacity / 2u <= d->texture_count) {
        if (capacity > UINT32_MAX / 2u / sizeof(uint32_t)) tide_out_of_memory();
        capacity *= 2u;
    }
    if (capacity != d->texture_index_capacity) {
        free(d->texture_index);
        d->texture_index = tide_alloc_zeroed(capacity, sizeof(uint32_t));
        d->texture_index_capacity = capacity;
    } else {
        memset(d->texture_index, 0, (size_t)capacity * sizeof(uint32_t));
    }
    // In array order, so looking for only space/id still finds its first place.
    for (uint32_t i = 0; i < d->texture_count; i++) index_texture(d, i);
}

static tide_draw_texture *find_texture(const tide_draw_list *d, const uint32_t space, const uint64_t id)
{
    if (!d->texture_index_capacity) return NULL;
    const uint32_t mask = d->texture_index_capacity - 1u;
    for (uint32_t i = texture_hash(space, id) & mask; d->texture_index[i]; i = (i + 1u) & mask) {
        tide_draw_texture *t = &d->textures[d->texture_index[i] - 1u];
        if (t->space == space && t->id == id) return t;
    }
    return NULL;
}

static tide_draw_texture *add_texture(tide_draw_list *d, const tide_draw_texture texture)
{
    d->textures = grow(d->textures, d->texture_count, &d->texture_capacity, 1, sizeof(tide_draw_texture), 8u);
    const uint32_t place = d->texture_count++;
    d->textures[place] = texture;
    if (d->texture_count >= d->texture_index_capacity / 2u) reindex_textures(d);
    else index_texture(d, place);
    return &d->textures[place];
}

static void reserve(tide_draw_list *d, const uint32_t more)
{
    d->commands = grow(d->commands, d->count, &d->capacity, more, sizeof(tide_draw_command), 256u);
}

// ---------------------------------------------------------------------------
// Settings

// What of a place's `moved` is its 3D transform's.
#define MOVED_3D ((uint32_t)(TIDE_DRAW_MOVED_3D | TIDE_DRAW_MOVED_MIRROR))

// Whether `m` mirrors what goes through it, so that a triangle comes out wound
// the other way: its determinant is under 0. One that flattens (0) doesn't:
// what it makes has no side that's the front.
static inline bool mirrors(const tide_float4x4 *m)
{
    return m->c0.x * (m->c1.y * m->c2.z - m->c1.z * m->c2.y) + m->c0.y * (m->c1.z * m->c2.x - m->c1.x * m->c2.z)
               + m->c0.z * (m->c1.x * m->c2.y - m->c1.y * m->c2.x) < 0.0f;
}

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
    d->unsettled = p->camera_2d != p->played_2d || p->camera_3d != p->played_3d || p->clip != p->played_clip
                || p->stencil != p->played_stencil;
    d->altered = d->place.tinted || (p->moved & TIDE_DRAW_MOVED) != 0;
}

// A command that sets something for the commands after it, and draws nothing.
static tide_draw_command *push_state(tide_draw_list *d, const tide_draw_kind kind)
{
    reserve(d, 1);
    tide_draw_command *c = &d->commands[d->count++];
    *c = (tide_draw_command){.kind = (uint8_t)kind};
    return c;
}

// The commands that leave a renderer with the selected destination's cameras,
// clip and stencil as its place has them.
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
        uint32_t matrix = TIDE_DRAW_FIRST_CAMERA, fit = 1, mirrored = 0;
        if (p->camera_3d) {
            d->matrices = grow(d->matrices, d->matrix_count, &d->matrix_capacity, 1, sizeof(tide_float4x4), 256u);
            d->matrices[d->matrix_count] = d->cameras_3d[p->camera_3d - 1u].matrix;
            matrix = d->matrix_count++;
            fit = d->cameras_3d[p->camera_3d - 1u].fit;
            mirrored = d->cameras_3d[p->camera_3d - 1u].mirrored;
        }
        tide_draw_command *c = push_state(d, TIDE_DRAW_CAMERA_3D);
        c->camera.matrix = matrix;
        c->camera.fit = fit;
        c->camera.mirrored = mirrored;
        p->played_3d = p->camera_3d;
    }
    if (p->clip != p->played_clip) {
        push_state(d, p->clip ? TIDE_DRAW_CLIP : TIDE_DRAW_NO_CLIP)->clip = p->clip;
        p->played_clip = p->clip;
    }
    if (p->stencil != p->played_stencil) {
        push_state(d, TIDE_DRAW_STENCIL)->stencil = p->stencil;
        p->played_stencil = p->stencil;
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
    *c = (tide_draw_command){.kind = (uint8_t)kind, .blend = (uint8_t)d->place.blend, .a = a, .b = b, .color = color};
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
        if (s->target <= d->target_count) { // Its destination's, wherever commands go by now
            tide_draw_place *p = place_of(d, s->target);
            switch (s->setting) {
            case TIDE_DRAW_SAVED_BLEND: p->blend = s->value; break;
            case TIDE_DRAW_SAVED_TINT:
                p->tint = s->tint;
                p->tinted = 1;
                break;
            case TIDE_DRAW_SAVED_NO_TINT: p->tinted = 0; break;
            case TIDE_DRAW_SAVED_TRANSFORM:
                p->transform = s->transform;
                p->moved = (p->moved & MOVED_3D) | s->moved;
                p->scale = s->scale;
                break;
            case TIDE_DRAW_SAVED_TRANSFORM_3D:
                p->transform_3d = s->transform_3d;
                p->moved = (p->moved & ~MOVED_3D) | TIDE_DRAW_MOVED_3D
                    | (mirrors(&s->transform_3d) ? (uint32_t)TIDE_DRAW_MOVED_MIRROR : 0u);
                break;
            case TIDE_DRAW_SAVED_NO_TRANSFORM_3D: p->moved &= ~MOVED_3D; break;
            case TIDE_DRAW_SAVED_CLIP: p->clip = s->value; break;
            case TIDE_DRAW_SAVED_DEPTH: p->depth = s->value; break;
            case TIDE_DRAW_SAVED_CULL: p->cull = s->value; break;
            case TIDE_DRAW_SAVED_STENCIL: p->stencil = s->value; break;
            case TIDE_DRAW_SAVED_LOD: p->lod = s->value; break;
            case TIDE_DRAW_SAVED_LOD_CAMERA: p->lod_camera = s->value; break;
            case TIDE_DRAW_SAVED_CAMERA_2D: p->camera_2d = s->value; break;
            default: p->camera_3d = s->value; break;
            }
        }
    }
    refresh(d);
}

// How the selected destination's commands go over what's there: its own, as its cameras are.
void tide_draw_set_blend(tide_draw_list *d, const tide_blend blend)
{
    d->place.blend = (uint32_t)blend <= TIDE_BLEND_REPLACE ? (uint32_t)blend : TIDE_BLEND_ALPHA;
}

void tide_draw_unset_blend(tide_draw_list *d)
{
    d->place.blend = TIDE_BLEND_ALPHA;
}

uint32_t tide_draw_push_blend(tide_draw_list *d, const tide_blend blend)
{
    const uint32_t mark = d->saved_count;
    save(d, TIDE_DRAW_SAVED_BLEND)->value = d->place.blend;
    tide_draw_set_blend(d, blend);
    return mark;
}

// What the selected destination's 3D meshes do with its depth: its own, as its cameras are.
void tide_draw_set_depth(tide_draw_list *d, const tide_depth depth)
{
    d->place.depth = (uint32_t)depth <= TIDE_DEPTH_NONE ? (uint32_t)depth : TIDE_DEPTH_READ_WRITE;
}

void tide_draw_unset_depth(tide_draw_list *d)
{
    d->place.depth = TIDE_DEPTH_READ_WRITE;
}

uint32_t tide_draw_push_depth(tide_draw_list *d, const tide_depth depth)
{
    const uint32_t mark = d->saved_count;
    save(d, TIDE_DRAW_SAVED_DEPTH)->value = d->place.depth;
    tide_draw_set_depth(d, depth);
    return mark;
}

// Which side of the triangles of the selected destination's 3D meshes isn't drawn: its own, as its depth is.
void tide_draw_set_cull(tide_draw_list *d, const tide_cull cull)
{
    d->place.cull = (uint32_t)cull <= TIDE_CULL_NONE ? (uint32_t)cull : TIDE_CULL_BACK;
}

void tide_draw_unset_cull(tide_draw_list *d)
{
    d->place.cull = TIDE_CULL_BACK;
}

uint32_t tide_draw_push_cull(tide_draw_list *d, const tide_cull cull)
{
    const uint32_t mark = d->saved_count;
    save(d, TIDE_DRAW_SAVED_CULL)->value = d->place.cull;
    tide_draw_set_cull(d, cull);
    return mark;
}

// Which level of detail the selected destination's meshes draw at, where they have any: its own,
// as its cull is. No command says it: a level is a mesh, which a run names as it names any.
void tide_draw_set_lod(tide_draw_list *d, const int32_t level)
{
    d->place.lod = 1u + (uint32_t)(level < 0 ? 0 : level > TIDE_MESH_LEVELS ? TIDE_MESH_LEVELS : level);
}

void tide_draw_unset_lod(tide_draw_list *d)
{
    d->place.lod = 0;
}

uint32_t tide_draw_push_lod(tide_draw_list *d, const int32_t level)
{
    const uint32_t mark = d->saved_count;
    save(d, TIDE_DRAW_SAVED_LOD)->value = d->place.lod;
    tide_draw_set_lod(d, level);
    return mark;
}

uint32_t tide_draw_push_no_lod(tide_draw_list *d)
{
    const uint32_t mark = d->saved_count;
    save(d, TIDE_DRAW_SAVED_LOD)->value = d->place.lod;
    d->place.lod = 0;
    return mark;
}

void tide_draw_unset_lod_camera(tide_draw_list *d)
{
    d->place.lod_camera = 0;
}

uint32_t tide_draw_push_no_lod_camera(tide_draw_list *d)
{
    const uint32_t mark = d->saved_count;
    save(d, TIDE_DRAW_SAVED_LOD_CAMERA)->value = d->place.lod_camera;
    d->place.lod_camera = 0;
    return mark;
}

void tide_draw_screen_size(tide_draw_list *d, const tide_float2 size)
{
    d->screen = size.x > 0.0f && size.y > 0.0f && size.x <= 3.0e38f && size.y <= 3.0e38f ? size : tide_f2(0.0f, 0.0f);
}

void tide_draw_lod_pixels(tide_draw_list *d, const float pixels)
{
    d->lod_pixels = pixels > 0.0f && pixels <= 3.0e38f ? pixels : 0.0f;
}

// What the selected destination's commands do with its stencil: its own, as the stencil is. It
// reaches a renderer as cameras and clips do, in a command before the next one that draws.
void tide_draw_set_stencil(tide_draw_list *d, const tide_stencil stencil)
{
    d->place.stencil = tide_stencil_packed(tide_stencil_stored(stencil.test, stencil.value, stencil.operation));
    refresh(d);
}

void tide_draw_unset_stencil(tide_draw_list *d)
{
    d->place.stencil = 0;
    refresh(d);
}

uint32_t tide_draw_push_stencil(tide_draw_list *d, const tide_stencil stencil)
{
    const uint32_t mark = d->saved_count;
    save(d, TIDE_DRAW_SAVED_STENCIL)->value = d->place.stencil;
    tide_draw_set_stencil(d, stencil);
    return mark;
}

// What the selected destination's commands have their colors multiplied by: its own too.
void tide_draw_set_tint(tide_draw_list *d, const tide_color tint)
{
    d->place.tint = tint;
    d->place.tinted = !(tint.r == 1.0f && tint.g == 1.0f && tint.b == 1.0f && tint.a == 1.0f);
    refresh(d);
}

void tide_draw_unset_tint(tide_draw_list *d)
{
    d->place.tinted = 0;
    refresh(d);
}

uint32_t tide_draw_push_tint(tide_draw_list *d, const tide_color tint)
{
    const uint32_t mark = d->saved_count;
    if (d->place.tinted) save(d, TIDE_DRAW_SAVED_TINT)->tint = d->place.tint;
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
    p->moved &= MOVED_3D;
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
    d->place.moved &= MOVED_3D;
    refresh(d);
}

uint32_t tide_draw_push_transform(tide_draw_list *d, const tide_draw_transform transform)
{
    const uint32_t mark = d->saved_count;
    tide_draw_place *p = &d->place;
    tide_draw_saved *s = save(d, TIDE_DRAW_SAVED_TRANSFORM);
    s->transform = p->transform;
    s->moved = p->moved & ~MOVED_3D;
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
    tide_draw_place *p = &d->place;
    p->transform_3d = transform;
    p->moved &= ~MOVED_3D;
    if (same_matrix(&transform, &none)) return;
    // Whether it mirrors is known from here, so a mesh that goes through it asks of its own transform alone
    p->moved |= TIDE_DRAW_MOVED_3D | (mirrors(&transform) ? (uint32_t)TIDE_DRAW_MOVED_MIRROR : 0u);
}

void tide_draw_unset_transform_3d(tide_draw_list *d)
{
    d->place.moved &= ~MOVED_3D;
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
    for (uint32_t i = 0; i <= d->target_count; i++) {
        tide_draw_place *p = i ? &d->targets[i - 1u].place : &d->place;
        p->camera_2d = p->camera_3d = p->clip = p->moved = p->stencil = p->tinted = 0;
        p->lod = p->lod_camera = 0;
        p->blend = TIDE_BLEND_ALPHA;
        p->depth = TIDE_DEPTH_READ_WRITE;
        p->cull = TIDE_CULL_BACK;
    }
    refresh(d);
}

void tide_draw_forget(tide_draw_list *d)
{
    tide_skinned_draw_cache_reset(d);
    const bool had_targets = d->target_count != 0;
    for (uint32_t i = 0; i < d->texture_count; i++) free_pixels(&d->textures[i]);
    d->texture_count = 0;
    if (d->texture_index_capacity) memset(d->texture_index, 0, (size_t)d->texture_index_capacity * sizeof(uint32_t));
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

// A target's image as a texture of the list's: its color (TIDE_PIXELS_TARGET) or its depth.
static uint32_t target_texture_version(tide_draw_list *d, const uint32_t space, const uint64_t id, const tide_target_size size,
                                       const uint64_t version)
{
    if (!id) return 0;
    if (d->texture_index_capacity) {
        const uint32_t mask = d->texture_index_capacity - 1u;
        for (uint32_t i = texture_hash(space, id) & mask; d->texture_index[i]; i = (i + 1u) & mask) {
            tide_draw_texture *t = &d->textures[d->texture_index[i] - 1u];
            if (t->space == space && t->id == id && tide_target_size_same(t->target, size) && t->version == version) {
                t->frame = d->frame;
                return d->texture_index[i];
            }
        }
    }
    add_texture(d, (tide_draw_texture){
        .id = id, .version = version, .space = space, .target = size, .frame = d->frame, .epoch = d->epoch});
    return d->texture_count;
}

uint32_t tide_draw_target_texture_sized(tide_draw_list *d, const uint64_t id, const tide_target_size size)
{
    return target_texture_version(d, TIDE_PIXELS_TARGET, id, size, tide_render_target_version(id));
}

uint32_t tide_draw_target_depth_sized(tide_draw_list *d, const uint64_t id, const tide_target_size size)
{
    return target_texture_version(d, TIDE_PIXELS_TARGET_DEPTH, id, size, tide_render_target_version(id));
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
    if (d->place.tinted) color = tide_mul_color(color, d->place.tint);
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
    if (d->place.tinted) {
        *color = tide_mul_color(*color, d->place.tint);
        shape->color2 = tide_mul_color(shape->color2, d->place.tint);
        shape->outline_color = tide_mul_color(shape->outline_color, d->place.tint);
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
    if (d->place.tinted) color = tide_mul_color(color, d->place.tint);
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
    return tide_draw_texture_slot_as(d, space, id, version, width, height, TIDE_TEXELS_BYTES, pixels);
}

// The texture `id` of `space` at `version`, with its pixels to write unless
// the list has them so. `kept`, where a writer of `kind` asks, says they're
// the ones it wrote at another version, whose changes it then says: anything
// else leaves nothing known of what changed.
static uint32_t texture_place(tide_draw_list *d, const uint32_t space, const uint64_t id, const uint64_t version,
                              const int32_t width, const int32_t height, const uint32_t format, const uint64_t kind,
                              uint8_t **pixels, bool *kept)
{
    *pixels = NULL;
    if (kept) *kept = false;
    if (width <= 0 || height <= 0) return 0;
    const uint64_t bytes = (uint64_t)width * (uint64_t)height * tide_texels_bytes(format);
    if (bytes > UINT32_MAX) tide_out_of_memory();
    tide_draw_texture *t = find_texture(d, space, id);
    bool same = false; // Its pixels are there, in this shape
    if (!t) {
        t = add_texture(d, (tide_draw_texture){.id = id, .space = space, .epoch = d->epoch});
    } else if (t->width == width && t->height == height && t->format == format) {
        if (t->version == version) { // The same pixels, whoever wrote them
            t->frame = d->frame;
            return (uint32_t)(t - d->textures) + 1u;
        }
        same = t->capacity != 0;
    }
    if (bytes > t->capacity) {
        t->pixels = tide_realloc(t->pixels, t->capacity, (size_t)bytes);
        t->capacity = (uint32_t)bytes;
    }
    tide_draw_texture_changes *c = t->changes;
    if (kind && !c) c = t->changes = tide_alloc_zeroed(1, sizeof *c);
    if (kind && same && c->kind == kind) {
        // What a frame changed is counted from the version the frame found: a renderer draws between two frames
        if (!c->known || c->frame != d->frame) {
            c->was = t->version;
            c->frame = d->frame;
            c->count = 0;
            c->known = true;
        }
        if (kept) *kept = true;
    } else if (c) { // All of it is written again, or by another writer
        c->known = false;
        c->kind = kind;
        c->used = 0;
        if (c->part_count) memset(c->parts, 0, (size_t)c->part_count * sizeof *c->parts);
    }
    t->version = version;
    t->format = format;
    t->width = width;
    t->height = height;
    t->frame = d->frame;
    *pixels = t->pixels;
    return (uint32_t)(t - d->textures) + 1u;
}

uint32_t tide_draw_texture_slot_as(tide_draw_list *d, const uint32_t space, const uint64_t id, const uint64_t version,
                                   const int32_t width, const int32_t height, const uint32_t format, uint8_t **pixels)
{
    return texture_place(d, space, id, version, width, height, format, 0, pixels, NULL);
}

uint32_t tide_draw_texture_edit(tide_draw_list *d, const uint32_t space, const uint64_t id, const uint64_t version,
                                const int32_t width, const int32_t height, const uint32_t format, const uint64_t kind,
                                uint8_t **pixels, bool *kept)
{
    return texture_place(d, space, id, version, width, height, format, kind ? kind : 1u, pixels, kept);
}

static tide_draw_texture_changes *changes_of(tide_draw_list *d, const uint32_t texture)
{
    if (texture == 0 || texture > d->texture_count) return NULL;
    tide_draw_texture *t = &d->textures[texture - 1u];
    if (!t->capacity) return NULL; // Not the list's own pixels
    if (!t->changes) t->changes = tide_alloc_zeroed(1, sizeof *t->changes);
    return t->changes;
}

void tide_draw_texture_changed(tide_draw_list *d, const uint32_t texture, int32_t x, int32_t y, int32_t width, int32_t height)
{
    if (texture == 0 || texture > d->texture_count) return;
    const tide_draw_texture *t = &d->textures[texture - 1u];
    tide_draw_texture_changes *c = t->changes;
    if (!c || !c->known) return; // Nothing is known of it: a renderer takes it whole
    if (x < 0) width += x, x = 0;
    if (y < 0) height += y, y = 0;
    if (width > t->width - x) width = t->width - x;
    if (height > t->height - y) height = t->height - y;
    if (width <= 0 || height <= 0) return;
    if (c->count) { // More of the last one, where it's against it: rows under rows, or pixels beside pixels
        tide_draw_area *last = &c->areas[c->count - 1u];
        if (last->x == x && last->width == width && y <= last->y + last->height && y + height >= last->y) {
            const int32_t top = y < last->y ? y : last->y;
            const int32_t bottom = y + height > last->y + last->height ? y + height : last->y + last->height;
            last->y = top;
            last->height = bottom - top;
            return;
        }
        if (last->y == y && last->height == height && x <= last->x + last->width && x + width >= last->x) {
            const int32_t left = x < last->x ? x : last->x;
            const int32_t right = x + width > last->x + last->width ? x + width : last->x + last->width;
            last->x = left;
            last->width = right - left;
            return;
        }
    }
    if (c->count < TIDE_DRAW_TEXTURE_AREAS) {
        c->areas[c->count++] = (tide_draw_area){x, y, width, height};
        return;
    }
    // More areas than it keeps: one around them all
    int32_t left = x, top = y, right = x + width, bottom = y + height;
    for (uint32_t i = 0; i < c->count; i++) {
        const tide_draw_area *a = &c->areas[i];
        if (a->x < left) left = a->x;
        if (a->y < top) top = a->y;
        if (a->x + a->width > right) right = a->x + a->width;
        if (a->y + a->height > bottom) bottom = a->y + a->height;
    }
    c->areas[0] = (tide_draw_area){left, top, right - left, bottom - top};
    c->count = 1;
}

uint64_t *tide_draw_texture_parts(tide_draw_list *d, const uint32_t texture, const uint32_t count)
{
    tide_draw_texture_changes *c = changes_of(d, texture);
    if (!c) return NULL;
    if (c->part_count != count) { // The numbers it had stay, and the ones past them are zeros
        uint64_t *parts = count ? tide_alloc_zeroed(count, sizeof *parts) : NULL;
        const uint32_t had = c->part_count < count ? c->part_count : count;
        if (had) memcpy(parts, c->parts, (size_t)had * sizeof *parts);
        free(c->parts);
        c->parts = parts;
        c->part_count = count;
    }
    return c->parts;
}

uint64_t *tide_draw_texture_used(tide_draw_list *d, const uint32_t texture)
{
    tide_draw_texture_changes *c = changes_of(d, texture);
    return c ? &c->used : NULL;
}

bool tide_draw_texture_since(const tide_draw_texture *texture, const uint64_t had, const tide_draw_area **areas,
                             uint32_t *count)
{
    const tide_draw_texture_changes *c = texture->changes;
    if (!c || !c->known || c->was != had) return false;
    *areas = c->areas;
    *count = c->count;
    return true;
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
    if (pixels) { // The list's copy has each color times its alpha, as every texture a GPU blends (see tide_gpu_blend)
        const size_t count = (size_t)texture->width * (size_t)texture->height;
        for (size_t i = 0; i < count; i++) {
            const uint8_t *in = (const uint8_t *)texture->pixels + i * 4u;
            const uint32_t alpha = in[3];
            pixels[i * 4u] = (uint8_t)((in[0] * alpha + 127u) / 255u);
            pixels[i * 4u + 1u] = (uint8_t)((in[1] * alpha + 127u) / 255u);
            pixels[i * 4u + 2u] = (uint8_t)((in[2] * alpha + 127u) / 255u);
            pixels[i * 4u + 3u] = in[3];
        }
    }
    return slot;
}

// The list's texture for an image of the game's pack, whose pixels are where
// the pack has them: nothing is copied. They're noted again each time a
// command names the image, so a frame's textures point into the pack that's
// loaded while the frame is recorded.
static uint32_t pack_texture(tide_draw_list *d, const uint64_t version, const int32_t width, const int32_t height,
                             const uint8_t *pixels)
{
    tide_draw_texture *t = find_texture(d, TIDE_PIXELS_PACK, version);
    if (!t) {
        t = add_texture(d, (tide_draw_texture){.id = version, .space = TIDE_PIXELS_PACK, .epoch = d->epoch});
    }
    t->version = version;
    t->format = TIDE_TEXELS_BYTES;
    t->width = width;
    t->height = height;
    t->frame = d->frame;
    t->pixels = (uint8_t *)(uintptr_t)pixels; // The pack's own: `capacity` stays 0, and nothing here frees them
    return (uint32_t)(t - d->textures) + 1u;
}

// An image of the game's pack, which a Texture names by its declaration
// (tide_pack_texture_of): whatever the pack that's loaded has under that
// name, as it is now. The list's texture is told by what's in the image, so
// two names of one file are one texture, and a file that changed is another.
// A name the pack has no image under (a Texture kept from another build's) is
// no texture, as the default Texture is.
static uint32_t pack_slot(tide_draw_list *d, const tide_texture *texture)
{
    tide_texture image;
    if (!tide_pack_texture_find(texture->version & TIDE_TEXTURE_NAME, &image)) return 0;
    const uint32_t slot = pack_texture(d, image.version, image.width, image.height, image.pixels);
    d->textures[slot - 1u].compressed = tide_pack_texture_blocks_find(texture->version & TIDE_TEXTURE_NAME);
    return slot;
}

uint32_t tide_draw_texture_source(tide_draw_list *d, const tide_texture_ref texture)
{
    if (!texture.target.id) {
        if (texture.image.pixels) return texture_slot(d, &texture.image);
        tide_grid grid;
        tide_grid_shape shape;
        if (tide_texture_grid(texture, &grid, &shape)) return tide_draw_grid_texture(d, grid, &shape);
        return pack_slot(d, &texture.image);
    }
    const tide_render_target target = tide_render_target_resolve(texture.target);
    return tide_draw_target_texture_sized(d, target.id, target.size);
}

uint32_t tide_draw_depth_source(tide_draw_list *d, const tide_depth_ref depth)
{
    if (!depth.target.id) return 0;
    const tide_render_target target = tide_render_target_resolve(depth.target);
    return tide_draw_target_depth_sized(d, target.id, target.size);
}

// The list's texture for a cube of the pack's, whose faces' pixels are where the pack has them, as an image's are.
static uint32_t pack_cube(tide_draw_list *d, const uint64_t version, const int32_t size, const uint8_t *const *faces)
{
    tide_draw_texture *t = find_texture(d, TIDE_PIXELS_PACK_CUBE, version);
    if (!t) {
        t = add_texture(d, (tide_draw_texture){.id = version, .space = TIDE_PIXELS_PACK_CUBE, .epoch = d->epoch});
    }
    t->version = version;
    t->format = TIDE_TEXELS_BYTES;
    t->width = t->height = size;
    t->frame = d->frame;
    t->pixels = (uint8_t *)(uintptr_t)faces; // The pack's six pointers: `capacity` stays 0, and nothing here frees them
    return (uint32_t)(t - d->textures) + 1u;
}

uint32_t tide_draw_cube_source(tide_draw_list *d, const tide_cubemap_ref cube)
{
    if (!cube.target.id) {
        int32_t size;
        uint64_t version;
        const uint8_t *const *faces;
        if (!tide_pack_cubemap_find(cube.named, &size, &version, &faces)) return 0;
        return pack_cube(d, version, size, faces);
    }
    const tide_render_target target = tide_render_target_resolve(cube.target);
    if (!tide_render_target_cubic(target.id)) return 0;
    const uint64_t id = tide_render_target_cube_of(target.id);
    return target_texture_version(d, TIDE_PIXELS_TARGET_CUBE, id, target.size, tide_render_target_version(id));
}

// A mesh's corners as the tint and the transform leave them: apart from
// tide_draw_vertices, which a frame of meshes with neither spends its time in.
static __attribute__((noinline)) void alter_vertices(tide_draw_list *d, const uint32_t base, const uint32_t count)
{
    const tide_draw_place *p = &d->place;
    const bool moved = (p->moved & TIDE_DRAW_MOVED) != 0;
    for (tide_vertex *v = d->vertices + base, *end = v + count; v < end; v++) {
        if (d->place.tinted) v->color = tide_mul_color(v->color, d->place.tint);
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
                              const uint32_t slot, const tide_filter filter, const tide_wrap wrap)
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
    if (last && last->kind == TIDE_DRAW_MESH && last->blend == d->place.blend && last->mesh.texture == slot
        && last->mesh.filter == (uint16_t)filter && last->mesh.wrap == (uint16_t)wrap
        && last->mesh.first + last->mesh.count == first) {
        last->mesh.count += d->index_count - first;
        return;
    }
    tide_draw_command *c = push(d, TIDE_DRAW_MESH, tide_f2(0.0f, 0.0f), tide_f2(0.0f, 0.0f), TIDE_COLOR_CLEAR);
    c->mesh.first = first;
    c->mesh.count = d->index_count - first;
    c->mesh.texture = slot;
    c->mesh.filter = (uint16_t)filter;
    c->mesh.wrap = (uint16_t)wrap;
}

void tide_draw_triangles_wrapped(tide_draw_list *d, const uint32_t base, const uint32_t *indices, const uint32_t count,
                                 const tide_texture *texture, const tide_filter filter, const tide_wrap wrap)
{
    tide_draw_triangles_with(d, base, indices, count, texture_slot(d, texture), filter, wrap);
}

void tide_draw_triangles(tide_draw_list *d, const uint32_t base, const uint32_t *indices, const uint32_t count,
                         const tide_texture *texture, const tide_filter filter)
{
    tide_draw_triangles_wrapped(d, base, indices, count, texture, filter, TIDE_WRAP_CLAMP);
}

void tide_draw_mesh_wrapped(tide_draw_list *d, const tide_vertex *vertices, const uint32_t vertex_count,
                            const uint32_t *indices, const uint32_t index_count, const tide_texture *texture,
                            const tide_filter filter, const tide_wrap wrap)
{
    if (vertex_count == 0 || index_count < 3u) return;
    tide_draw_triangles_wrapped(d, tide_draw_vertices(d, vertices, vertex_count), indices, index_count, texture, filter,
                                wrap);
}

void tide_draw_mesh(tide_draw_list *d, const tide_vertex *vertices, const uint32_t vertex_count, const uint32_t *indices,
                    const uint32_t index_count, const tide_texture *texture, const tide_filter filter)
{
    tide_draw_mesh_wrapped(d, vertices, vertex_count, indices, index_count, texture, filter, TIDE_WRAP_CLAMP);
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
// change, or a new one. `mirrored` says it shows the world as a looking glass
// would, which whoever made the matrix knows of its parts: asked of the
// matrix itself, a camera far from the origin would answer by its rounding.
static void camera_3d(tide_draw_list *d, const tide_draw_camera_3d_data camera)
{
    const tide_float4x4 world_to_clip = camera.matrix;
    const bool fit = camera.fit != 0, mirrored = camera.mirrored != 0;
    tide_draw_place *p = &d->place;
    const uint32_t known[2] = {p->played_3d, p->camera_3d};
    for (int i = 0; i < 2; i++) {
        if (!known[i]) continue;
        const tide_draw_camera_3d_data *c = &d->cameras_3d[known[i] - 1u];
        if (c->fit == (uint32_t)fit && c->mirrored == (uint32_t)mirrored && same_matrix(&c->matrix, &world_to_clip)) {
            p->camera_3d = known[i];
            refresh(d);
            return;
        }
    }
    d->cameras_3d = grow(d->cameras_3d, d->camera_3d_count, &d->camera_3d_capacity, 1, sizeof(tide_draw_camera_3d_data), 8u);
    d->cameras_3d[d->camera_3d_count] = camera;
    p->camera_3d = ++d->camera_3d_count;
    refresh(d);
}

// A camera of which only the matrix is known, as a command has it: one at the
// origin whose projection is all of it.
static tide_draw_camera_3d_data camera_of_matrix(const tide_float4x4 world_to_clip, const bool fit, const bool mirrored)
{
    tide_float4x4 projection = world_to_clip;
    projection.c2 = tide_neg_f4(projection.c2);
    return (tide_draw_camera_3d_data){.matrix = world_to_clip, .transform = tide_identity_f4x4(),
                                      .projection = projection, .fit = fit, .mirrored = mirrored};
}

static tide_draw_camera_3d_data perspective_camera(const tide_float3 position, const tide_quaternion rotation,
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
    return (tide_draw_camera_3d_data){
        .matrix = tide_mul_f4x4(projection, turn_z(view)),
        .transform = tide_trs_f4x4(position, tide_normalize_q(rotation), tide_f3(1.0f, 1.0f, 1.0f)),
        .projection = projection,
        .fit = 1,
    };
}

tide_float4x4 tide_draw_camera_3d_matrix(const tide_float3 position, const tide_quaternion rotation,
                                         const float field_of_view)
{
    return perspective_camera(position, rotation, field_of_view).matrix;
}

tide_draw_camera_3d_data tide_draw_first_camera_3d(void)
{
    return perspective_camera(tide_f3(0.0f, 0.0f, 0.0f), tide_identity_q(), 60.0f);
}

tide_float4x4 tide_draw_camera_fit(tide_float4x4 matrix, const float width, const float height)
{
    const float across = height / width;
    matrix.c0.x *= across, matrix.c1.x *= across;
    matrix.c2.x *= across, matrix.c3.x *= across;
    return matrix;
}

void tide_draw_set_camera_3d(tide_draw_list *d, const tide_float3 position, const tide_quaternion rotation,
                             const float field_of_view)
{
    camera_3d(d, perspective_camera(position, rotation, field_of_view));
}

void tide_draw_unset_camera_3d(tide_draw_list *d)
{
    d->place.camera_3d = 0;
    refresh(d);
}

void tide_draw_set_camera_matrices(tide_draw_list *d, const tide_float4x4 transform, const tide_float4x4 projection)
{
    // A projection as OpenGL's turns its view's z around, so its determinant is under 0, and a
    // camera's transform turns and moves it. One of the two the other way shows the world mirrored
    // (a camera placed by a matrix that mirrors, a projection upside down, or the identity's, which
    // looks back along z), and a triangle's front is still its front there
    const bool mirrored = mirrors(&transform) != (tide_determinant_f4x4(projection) > 0.0f);
    camera_3d(d, (tide_draw_camera_3d_data){.matrix = tide_mul_f4x4(projection, turn_z(tide_inverse_f4x4(transform))),
                                            .transform = transform, .projection = projection, .mirrored = mirrored});
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

// The camera the selected destination's levels of detail are picked through: one of the list's
// cameras that no command names, so nothing is drawn through it.
static void lod_camera(tide_draw_list *d, const tide_draw_camera_3d_data camera)
{
    d->cameras_3d = grow(d->cameras_3d, d->camera_3d_count, &d->camera_3d_capacity, 1, sizeof(tide_draw_camera_3d_data), 8u);
    d->cameras_3d[d->camera_3d_count] = camera;
    d->place.lod_camera = ++d->camera_3d_count;
}

void tide_draw_set_lod_camera_3d(tide_draw_list *d, const tide_float3 position, const tide_quaternion rotation,
                                 const float field_of_view)
{
    lod_camera(d, perspective_camera(position, rotation, field_of_view));
}

void tide_draw_set_lod_camera_matrices(tide_draw_list *d, const tide_float4x4 transform, const tide_float4x4 projection)
{
    lod_camera(d, (tide_draw_camera_3d_data){.matrix = tide_mul_f4x4(projection, turn_z(tide_inverse_f4x4(transform))),
                                             .transform = transform, .projection = projection});
}

uint32_t tide_draw_push_lod_camera_3d(tide_draw_list *d, const tide_float3 position, const tide_quaternion rotation,
                                      const float field_of_view)
{
    const uint32_t mark = d->saved_count;
    save(d, TIDE_DRAW_SAVED_LOD_CAMERA)->value = d->place.lod_camera;
    tide_draw_set_lod_camera_3d(d, position, rotation, field_of_view);
    return mark;
}

uint32_t tide_draw_push_lod_camera_matrices(tide_draw_list *d, const tide_float4x4 transform, const tide_float4x4 projection)
{
    const uint32_t mark = d->saved_count;
    save(d, TIDE_DRAW_SAVED_LOD_CAMERA)->value = d->place.lod_camera;
    tide_draw_set_lod_camera_matrices(d, transform, projection);
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

void tide_draw_mesh_levels(tide_draw_list *d, const uint32_t mesh, const tide_mesh_levels *levels)
{
    if (mesh == 0 || mesh > d->mesh_count || d->meshes[mesh - 1u].space == TIDE_MESH_NONE) return;
    tide_draw_mesh_data *m = &d->meshes[mesh - 1u];
    m->levels = m->level_index_count = 0;
    if (m->base || m->index_count == 0 || !levels || levels->count == 0 || !levels->index) return;
    m->level_indices = grow(m->level_indices, 0, &m->level_index_capacity, levels->index_count, sizeof(uint32_t), 192u);
    memcpy(m->center, levels->center, sizeof m->center);
    m->radius = levels->radius >= 0.0f ? levels->radius : 0.0f;
    float before = 0.0f;
    for (uint32_t level = 0; level < levels->count && level < TIDE_MESH_LEVELS; level++) {
        const uint32_t first = levels->first[level], count = levels->indices[level];
        if (first > levels->index_count || count > levels->index_count - first) break;
        const uint32_t at = m->level_index_count;
        // A triangle with an index past the vertices is left out, as in the mesh's own
        for (uint32_t i = 0; i + 3u <= count; i += 3u) {
            const uint32_t *corner = &levels->index[first + i];
            if (corner[0] >= m->vertex_count || corner[1] >= m->vertex_count || corner[2] >= m->vertex_count) continue;
            memcpy(&m->level_indices[m->level_index_count], corner, 3u * sizeof(uint32_t));
            m->level_index_count += 3u;
        }
        // One with nothing left, or off by a number that's none, ends them: a draw that picked it would draw nothing
        const float error = levels->error[level];
        if (m->level_index_count == at || !(error >= before) || error > 3.0e38f) {
            m->level_index_count = at;
            break;
        }
        m->level_error[level] = before = error;
        m->level_first[level] = at;
        m->level_count[level] = m->level_index_count - at;
        m->level_mesh[level] = 0;
        m->levels = level + 1u;
    }
}

// The mesh that level `level` (from 1) of `mesh` is, for a run's command: its triangles alone, with `mesh`'s
// vertices. A frame that draws it keeps `mesh` too.
static uint32_t level_mesh(tide_draw_list *d, const uint32_t mesh, const uint32_t level)
{
    tide_draw_mesh_data *m = &d->meshes[mesh - 1u];
    m->frame = d->frame;
    const uint64_t numbers[2] = {m->version, level};
    const uint64_t version = tide_hash(numbers, sizeof numbers);
    const uint32_t had = m->level_mesh[level - 1u];
    if (had && had <= d->mesh_count) {
        tide_draw_mesh_data *l = &d->meshes[had - 1u];
        if (l->base == mesh && l->level == level && l->space == m->space && l->id == m->id && l->other == m->other
            && l->version == version && l->epoch == m->epoch) {
            l->frame = d->frame;
            return had;
        }
    }
    bool fill;
    const uint32_t slot = tide_draw_mesh_slot(d, m->space, m->id, m->other, version, &fill);
    m = &d->meshes[mesh - 1u]; // The meshes may have moved
    tide_draw_mesh_data *l = &d->meshes[slot - 1u];
    if (fill) {
        const uint32_t count = m->level_count[level - 1u];
        l->indices = grow(l->indices, 0, &l->index_capacity, count, sizeof(uint32_t), 192u);
        memcpy(l->indices, m->level_indices + m->level_first[level - 1u], (size_t)count * sizeof(uint32_t));
        l->index_count = count;
        l->vertex_size = m->vertex_size;
        l->vertex_count = m->vertex_count;
        l->level = level;
    }
    l->base = mesh;
    m->level_mesh[level - 1u] = slot;
    return slot;
}

// What a pick goes by, of the selected destination: worked out once a draw.
typedef struct lod_view {
    uint32_t level;  // 1 + the level every mesh draws at, or 0 where each is picked
    bool picks;      // Whether there's a camera and a size to pick by: the full mesh otherwise
    float scale;     // How many times the pixels a level may be off by a unit of the world is, a unit from the camera
    float w[4];      // What gives how far along the camera's view a place in the world is, which sizes shrink by...
    float w_length;  // ...and how much of it a unit of the world is, at the most
} lod_view;

static lod_view lod_view_of(const tide_draw_list *d);

// The same for a draw of one instance, which a view that runs for each entity records a frame's worth of: the
// camera's part is kept while the camera, the frame and the pixels are the ones it was worked out for.
static lod_view lod_view_kept(tide_draw_list *d)
{
    const tide_draw_place *p = &d->place;
    lod_view view = {.level = p->lod};
    if (view.level) return view;
    float height = d->screen.y;
    if (d->target && !p->lod_camera) {
        const tide_target_size size = d->targets[d->target - 1u].size;
        if (!(size.window > 0.0f)) height = size.height * (size.scale > 0.0f ? size.scale : 1.0f);
        else height *= size.window;
    }
    uint32_t seen[4] = {d->frame + 1u, p->lod_camera ? p->lod_camera : p->camera_3d};
    memcpy(&seen[2], &height, sizeof height);
    memcpy(&seen[3], &d->lod_pixels, sizeof d->lod_pixels);
    if (memcmp(seen, d->lod_seen, sizeof seen) != 0) {
        const lod_view fresh = lod_view_of(d);
        memcpy(d->lod_seen, seen, sizeof seen);
        d->lod_view[0] = fresh.picks ? fresh.scale : 0.0f;
        memcpy(&d->lod_view[1], fresh.w, sizeof fresh.w);
        d->lod_view[5] = fresh.w_length;
    }
    view.scale = d->lod_view[0];
    memcpy(view.w, &d->lod_view[1], sizeof view.w);
    view.w_length = d->lod_view[5];
    view.picks = view.scale > 0.0f;
    return view;
}

static lod_view lod_view_of(const tide_draw_list *d)
{
    const tide_draw_place *p = &d->place;
    lod_view view = {.level = p->lod};
    if (view.level) return view;
    // The pixels counted are the destination's own, or the window's for a camera that isn't the destination's
    float height = d->screen.y;
    if (d->target && !p->lod_camera) {
        const tide_target_size size = d->targets[d->target - 1u].size;
        if (!(size.window > 0.0f)) height = size.height * (size.scale > 0.0f ? size.scale : 1.0f);
        else height *= size.window;
    }
    if (!(height > 0.0f)) return view;
    const uint32_t through = p->lod_camera ? p->lod_camera : p->camera_3d;
    const tide_draw_camera_3d_data camera = through && through <= d->camera_3d_count ? d->cameras_3d[through - 1u]
                                                                                    : tide_draw_first_camera_3d();
    const tide_float4x4 *c = &camera.matrix;
    // Up the screen, clip space is 2 for the destination's height, whatever its shape is
    const float up = tide_sqrt_f(c->c0.y * c->c0.y + c->c1.y * c->c1.y + c->c2.y * c->c2.y);
    const float pixels = d->lod_pixels > 0.0f ? d->lod_pixels : 1.0f;
    view.scale = up * height * 0.5f / pixels;
    view.w[0] = c->c0.w, view.w[1] = c->c1.w, view.w[2] = c->c2.w, view.w[3] = c->c3.w;
    view.w_length = tide_sqrt_f(c->c0.w * c->c0.w + c->c1.w * c->c1.w + c->c2.w * c->c2.w);
    view.picks = view.scale > 0.0f && view.scale <= 3.0e38f;
    return view;
}

// The level an instance of `m` at `world` draws at: 0 for the full mesh. The simplest whose error, as big as
// it is at the nearest the mesh's ball comes to the camera, is within what the view allows.
static inline uint32_t pick_level(const tide_draw_mesh_data *m, const lod_view *view, const tide_float4x4 *world)
{
    if (view->level) return view->level - 1u < m->levels ? view->level - 1u : m->levels;
    if (!view->picks) return 0;
    const float x = m->center[0], y = m->center[1], z = m->center[2];
    const float cx = world->c0.x * x + world->c1.x * y + world->c2.x * z + world->c3.x;
    const float cy = world->c0.y * x + world->c1.y * y + world->c2.y * z + world->c3.y;
    const float cz = world->c0.z * x + world->c1.z * y + world->c2.z * z + world->c3.z;
    // How many times as big the transform makes the mesh, the way it makes it biggest
    const float sx = world->c0.x * world->c0.x + world->c0.y * world->c0.y + world->c0.z * world->c0.z;
    const float sy = world->c1.x * world->c1.x + world->c1.y * world->c1.y + world->c1.z * world->c1.z;
    const float sz = world->c2.x * world->c2.x + world->c2.y * world->c2.y + world->c2.z * world->c2.z;
    const float big = tide_sqrt_f(sx > sy ? (sx > sz ? sx : sz) : (sy > sz ? sy : sz));
    const float away = view->w[0] * cx + view->w[1] * cy + view->w[2] * cz + view->w[3] - m->radius * big * view->w_length;
    if (!(away > 0.0f)) return 0; // Around the camera, or behind it, or nowhere a number says
    const float allowed = away / (big * view->scale);
    uint32_t level = 0;
    while (level < m->levels && m->level_error[level] <= allowed) level++;
    return level;
}

uint32_t tide_draw_mesh_level(const tide_draw_list *d, const uint32_t mesh, const tide_float4x4 transform)
{
    if (mesh == 0 || mesh > d->mesh_count || d->meshes[mesh - 1u].levels == 0) return 0;
    const lod_view view = lod_view_of(d);
    const tide_draw_place *p = &d->place;
    const tide_float4x4 world = p->moved & TIDE_DRAW_MOVED_3D ? tide_mul_f4x4(p->transform_3d, transform) : transform;
    return pick_level(&d->meshes[mesh - 1u], &view, &world);
}

// Room for `bytes` to sort a draw's instances in, and a level for each of `count` after them.
static uint8_t *level_room(tide_draw_list *d, const uint64_t bytes, const uint32_t count)
{
    const uint64_t all = bytes + count;
    if (all > UINT32_MAX) tide_out_of_memory();
    d->level_room = grow(d->level_room, 0, &d->level_room_capacity, all, 1, 4096u);
    return d->level_room;
}

static bool same_color(const tide_color a, const tide_color b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

// With a cull: it's a command's own, which says whether its instances are
// mirrored, so their transforms aren't asked again.
#define SIDES_KNOWN 8u

// A command's cull for `cull`, a tide_cull with TIDE_DRAW_MIRRORED or not: one
// that's none of the three is TIDE_CULL_BACK, and with both sides drawn,
// mirrored is nothing to say.
static inline uint8_t sides(const uint32_t cull)
{
    const uint32_t side = cull & 3u;
    if (side == TIDE_CULL_NONE) return TIDE_CULL_NONE;
    return (uint8_t)((side <= TIDE_CULL_NONE ? side : (uint32_t)TIDE_CULL_BACK) | (cull & TIDE_DRAW_MIRRORED));
}

void tide_draw_shaded(tide_draw_list *d, const tide_shader *shader, const uint32_t mesh, const void *instance,
                      const void *uniforms, const uint32_t *textures, const uint32_t sampling)
{
    tide_draw_shaded_instances(d, shader, mesh, instance, 1, uniforms, textures, sampling);
}

// A run of instances through a shader. `with` says what tints it and, for a
// 2D one, what it goes through before the camera: the rest of it is made here.
// `through` is what a 3D one's instances go through on their way to the
// world, or NULL, `depth` what a 3D one does with depth, and `cull` its
// command's (a tide_cull, with TIDE_DRAW_MIRRORED where the instances are):
// a 2D one, and a blit, have neither.
static void shaded_instances(tide_draw_list *d, const tide_shader *shader, const uint32_t mesh, const void *instances,
                             const uint32_t count, const void *uniforms, const uint32_t *textures,
                             const uint32_t sampling, const tide_draw_kind kind, const tide_blend blend,
                             const tide_depth depth, const uint32_t cull, const tide_draw_run *with,
                             const tide_float4x4 *through)
{
    if (count == 0 || !shader || mesh == 0 || mesh > d->mesh_count) return;
    const tide_draw_mesh_data *m = &d->meshes[mesh - 1u];
    if (m->space != TIDE_MESH_BYTES || m->index_count == 0 || m->vertex_size != shader->vertex_size) return;
    const uint32_t instance_size = tide_shader_instance_size(shader);
    const uint32_t uniform_size = tide_shader_uniform_size(shader);
    if ((instance_size && !instances) || (uniform_size && !uniforms)) return; // Nothing to draw it with
    const uint32_t texture_count = shader->texture_count < TIDE_SHADER_TEXTURES ? shader->texture_count : TIDE_SHADER_TEXTURES;
    tide_draw_run run = {.shader = shader, .mesh = mesh, .first = d->instance_used, .count = count, .sampling = sampling,
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
    // ...and what has no sides, so neither does that one
    const uint8_t sided = kind == TIDE_DRAW_SHADED && shader->dims == 3 ? sides(cull) : 0;
    if (kind == TIDE_DRAW_SHADED && last && last->kind == TIDE_DRAW_SHADED && last->blend == (uint8_t)blend
        && last->depth == deep && last->cull == sided) {
        tide_draw_run *r = &d->runs[last->shaded.run];
        if ((r->shader == shader || (shader->id && r->shader && r->shader->id == shader->id))
            && r->mesh == mesh && r->sampling == sampling
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
    c->cull = sided;
    c->shaded.run = d->run_count++;
}

// Whether the transform that starts a 3D shader's instance mirrors its mesh.
static bool instance_mirrors(const uint8_t *instance)
{
    tide_float4x4 own;
    memcpy(&own, instance, sizeof own);
    return mirrors(&own);
}

// Instances through a shader as the selected destination's settings have them, of the mesh as it's named: a
// level of detail is named by whoever picked it.
static void shaded_placed(tide_draw_list *d, const tide_shader *shader, const uint32_t mesh, const void *instances,
                          const uint32_t count, const void *uniforms, const uint32_t *textures, const uint32_t sampling)
{
    const tide_draw_place *p = &d->place;
    tide_draw_run with = {.tint = tide_draw_tint(d)};
    if (shader && shader->dims == 2 && (p->moved & TIDE_DRAW_MOVED)) {
        with.moved = 1;
        with.transform = p->transform;
    }
    const tide_float4x4 *through = p->moved & TIDE_DRAW_MOVED_3D ? &p->transform_3d : NULL;
    const uint32_t size = shader ? tide_shader_instance_size(shader) : 0;
    // As a 3D mesh's instances (mesh_instances): those whose transforms mirror the mesh are runs of
    // their own. An instance with no transform has none that could
    if (!shader || shader->dims != 3 || p->cull == TIDE_CULL_NONE || shader->instance_floats < 4u || !instances
        || count == 0) {
        shaded_instances(d, shader, mesh, instances, count, uniforms, textures, sampling, TIDE_DRAW_SHADED,
                         (tide_blend)d->place.blend, (tide_depth)p->depth, p->cull, &with, through);
        return;
    }
    const bool turned = through && (p->moved & TIDE_DRAW_MOVED_MIRROR);
    const uint8_t *bytes = instances;
    uint32_t first = 0;
    bool mirrored = instance_mirrors(bytes) != turned;
    for (uint32_t i = 1; i <= count; i++) {
        const bool next = i < count ? instance_mirrors(bytes + (size_t)i * size) != turned : !mirrored;
        if (next == mirrored) continue;
        shaded_instances(d, shader, mesh, bytes + (size_t)first * size, i - first, uniforms, textures, sampling,
                         TIDE_DRAW_SHADED, (tide_blend)d->place.blend, (tide_depth)p->depth,
                         p->cull | (mirrored ? TIDE_DRAW_MIRRORED : 0u), &with, through);
        first = i;
        mirrored = next;
    }
}

void tide_draw_shaded_instances(tide_draw_list *d, const tide_shader *shader, const uint32_t mesh, const void *instances,
                                const uint32_t count, const void *uniforms, const uint32_t *textures,
                                const uint32_t sampling)
{
    if (!shader || shader->dims != 3 || count == 0 || mesh == 0 || mesh > d->mesh_count || d->meshes[mesh - 1u].levels == 0) {
        shaded_placed(d, shader, mesh, instances, count, uniforms, textures, sampling);
        return;
    }
    // A mesh with levels of detail: each instance's own, by the transform that starts it (none is the identity).
    // The instances of a level are a run, the finest first, in the order they were given
    const lod_view view = count == 1u ? lod_view_kept(d) : lod_view_of(d);
    const tide_draw_place *p = &d->place;
    const tide_float4x4 *through = p->moved & TIDE_DRAW_MOVED_3D ? &p->transform_3d : NULL;
    const uint32_t size = tide_shader_instance_size(shader);
    const bool placed = shader->instance_floats >= 4u && instances && size >= sizeof(tide_float4x4);
    const uint64_t bytes = (uint64_t)size * count;
    uint8_t *sorted = level_room(d, bytes, count);
    uint8_t *levels = sorted + bytes;
    uint32_t counts[TIDE_MESH_LEVELS + 1] = {0};
    for (uint32_t i = 0; i < count; i++) {
        tide_float4x4 world = tide_identity_f4x4();
        if (placed) memcpy(&world, (const uint8_t *)instances + (size_t)i * size, sizeof world);
        if (through) world = tide_mul_f4x4(*through, world);
        levels[i] = (uint8_t)pick_level(&d->meshes[mesh - 1u], &view, &world);
        counts[levels[i]]++;
    }
    d->leveled = 1;
    uint32_t starts[TIDE_MESH_LEVELS + 1];
    uint32_t at = 0;
    for (uint32_t level = 0; level <= TIDE_MESH_LEVELS; level++) {
        starts[level] = at;
        at += counts[level];
    }
    const bool one = counts[levels[0]] == count; // All at one level: as they are
    if (!one && size && instances) {
        uint32_t next[TIDE_MESH_LEVELS + 1];
        memcpy(next, starts, sizeof next);
        for (uint32_t i = 0; i < count; i++) {
            memcpy(sorted + (size_t)next[levels[i]]++ * size, (const uint8_t *)instances + (size_t)i * size, size);
        }
    }
    for (uint32_t level = 0; level <= TIDE_MESH_LEVELS; level++) {
        if (counts[level] == 0) continue;
        const uint32_t drawn = level ? level_mesh(d, mesh, level) : mesh;
        const void *theirs = !instances ? NULL : one ? instances : (const void *)(sorted + (size_t)starts[level] * size);
        shaded_placed(d, shader, drawn, theirs, counts[level], uniforms, textures, sampling);
    }
}

void tide_draw_blit(tide_draw_list *d, const tide_shader *shader, const tide_blend blend, const void *instance,
                   const void *uniforms, const uint32_t *textures, const uint32_t sampling)
{
    if (!shader || shader->dims != 2 || shader->vertex_size != sizeof(tide_vertex)) return;
    for (uint32_t i = 0; textures && i < shader->texture_count && i < TIDE_SHADER_TEXTURES; i++) {
        const uint32_t slot = textures[i];
        if (!slot || slot > d->texture_count || !d->target) continue;
        const tide_draw_texture *t = &d->textures[slot - 1u];
        if (((t->space == TIDE_PIXELS_TARGET || t->space == TIDE_PIXELS_TARGET_DEPTH)
             && tide_render_target_whole(t->id) == d->targets[d->target - 1u].id) // Any of its color images, or its depth
            || (t->space == TIDE_PIXELS_TARGET_CUBE && t->id == tide_render_target_cube_of(d->targets[d->target - 1u].id))) {
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
    shaded_instances(d, shader, mesh, instance, 1, uniforms, textures, sampling, TIDE_DRAW_BLIT,
                     (uint32_t)blend <= TIDE_BLEND_REPLACE ? blend : TIDE_BLEND_REPLACE, TIDE_DEPTH_READ_WRITE, 0, &with,
                     NULL);
}

// Instances of a 3D mesh whose transforms are the list's from `at`, all
// mirrored or none: more of the run before them, when nothing came between
// and it draws the same way, or a command of their own.
static inline __attribute__((always_inline)) void mesh_run(tide_draw_list *d, const uint32_t mesh, const uint32_t at,
                                                           const uint32_t count, const uint32_t texture,
                                                           const tide_filter filter, const tide_wrap wrap,
                                                           const tide_blend blend, const tide_depth depth,
                                                           const uint8_t cull, const tide_color tint)
{
    tide_draw_command *last = d->count ? &d->commands[d->count - 1u] : NULL;
    if (last && last->kind == TIDE_DRAW_MESH_3D && last->blend == (uint8_t)blend && last->depth == (uint8_t)depth
        && last->cull == cull && last->mesh3.mesh == mesh && last->mesh3.texture == texture
        && last->mesh3.filter == (uint16_t)filter && last->mesh3.wrap == (uint16_t)wrap
        && last->mesh3.first + last->mesh3.count == at && same_color(last->color, tint)) {
        last->mesh3.count += count;
        return;
    }
    tide_draw_command *c = push(d, TIDE_DRAW_MESH_3D, tide_f2(0.0f, 0.0f), tide_f2(0.0f, 0.0f), tint);
    c->blend = (uint8_t)blend;
    c->depth = (uint8_t)depth;
    c->cull = cull;
    c->mesh3.mesh = mesh;
    c->mesh3.first = at;
    c->mesh3.count = count;
    c->mesh3.texture = texture;
    c->mesh3.filter = (uint16_t)filter;
    c->mesh3.wrap = (uint16_t)wrap;
}

// A run of instances of a 3D mesh, tinted by `tint`: `through` is what they
// go through on their way to the world, or NULL, which mirrors them when
// `turned`. `cull` is the setting, or with SIDES_KNOWN a command's own, which
// says all there is to say. Part of what calls it, as a frame of instances
// spends its time here.
static inline __attribute__((always_inline)) void mesh_instances(tide_draw_list *d, const uint32_t mesh, const tide_float4x4 *transforms, const uint32_t count,
                           const uint32_t texture, const tide_filter filter, const tide_wrap wrap,
                           const tide_blend blend, const tide_depth depth, const uint32_t cull, const tide_color tint,
                           const tide_float4x4 *through, const bool turned)
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
    // With both sides drawn there's nothing to tell of a transform, and they're one run
    if ((cull & SIDES_KNOWN) || cull == TIDE_CULL_NONE) {
        mesh_run(d, mesh, at, count, texture, filter, wrap, blend, depth, sides(cull), tint);
        return;
    }
    // A triangle's front is its front through a transform that mirrors it, which winds it the other
    // way for the GPU: the instances that are mirrored are runs of their own, which say so
    uint32_t first = 0;
    bool mirrored = mirrors(&transforms[0]) != turned;
    for (uint32_t i = 1; i <= count; i++) {
        const bool next = i < count ? mirrors(&transforms[i]) != turned : !mirrored;
        if (next == mirrored) continue;
        mesh_run(d, mesh, at + first, i - first, texture, filter, wrap, blend, depth,
                 sides(cull | (mirrored ? TIDE_DRAW_MIRRORED : 0u)), tint);
        first = i;
        mirrored = next;
    }
}

// The mesh one instance of `mesh`, which has levels of detail, draws: its own, or the level picked for it.
static uint32_t one_level(tide_draw_list *d, const uint32_t mesh, const tide_float4x4 *transform, const tide_float4x4 *through)
{
    const lod_view view = lod_view_kept(d);
    const tide_float4x4 world = through ? tide_mul_f4x4(*through, *transform) : *transform;
    const uint32_t level = pick_level(&d->meshes[mesh - 1u], &view, &world);
    d->leveled = 1;
    return level ? level_mesh(d, mesh, level) : mesh;
}

// One instance: what a view that runs for each entity records, so it's the
// code above for a count of one, not a call to the one for any number.
void tide_draw_instance(tide_draw_list *d, const uint32_t mesh, const tide_float4x4 transform, const uint32_t texture,
                        const tide_filter filter, const tide_wrap wrap)
{
    const tide_draw_place *p = &d->place;
    const tide_float4x4 *through = p->moved & TIDE_DRAW_MOVED_3D ? &p->transform_3d : NULL;
    uint32_t drawn = mesh;
    if (mesh && mesh <= d->mesh_count && d->meshes[mesh - 1u].levels) drawn = one_level(d, mesh, &transform, through);
    mesh_instances(d, drawn, &transform, 1, texture, filter, wrap, (tide_blend)d->place.blend, (tide_depth)p->depth, p->cull,
                   tide_draw_tint(d), through, through && (p->moved & TIDE_DRAW_MOVED_MIRROR));
}

// Instances as the selected destination's settings have them, of the mesh as it's named.
static void instances_placed(tide_draw_list *d, const uint32_t mesh, const tide_float4x4 *transforms, const uint32_t count,
                             const uint32_t texture, const tide_filter filter, const tide_wrap wrap)
{
    const tide_draw_place *p = &d->place;
    const tide_float4x4 *through = p->moved & TIDE_DRAW_MOVED_3D ? &p->transform_3d : NULL;
    mesh_instances(d, mesh, transforms, count, texture, filter, wrap, (tide_blend)d->place.blend, (tide_depth)p->depth,
                   p->cull, tide_draw_tint(d), through, through && (p->moved & TIDE_DRAW_MOVED_MIRROR));
}

void tide_draw_instances(tide_draw_list *d, const uint32_t mesh, const tide_float4x4 *transforms, const uint32_t count,
                         const uint32_t texture, const tide_filter filter, const tide_wrap wrap)
{
    if (count == 0 || !transforms || mesh == 0 || mesh > d->mesh_count || d->meshes[mesh - 1u].levels == 0) {
        instances_placed(d, mesh, transforms, count, texture, filter, wrap);
        return;
    }
    // A mesh with levels of detail: each instance's own, and the instances of a level a run, the finest first,
    // in the order they were given
    const lod_view view = lod_view_of(d);
    const tide_draw_place *p = &d->place;
    const tide_float4x4 *through = p->moved & TIDE_DRAW_MOVED_3D ? &p->transform_3d : NULL;
    const uint64_t bytes = (uint64_t)count * sizeof(tide_float4x4);
    uint8_t *sorted = level_room(d, bytes, count);
    uint8_t *levels = sorted + bytes;
    uint32_t counts[TIDE_MESH_LEVELS + 1] = {0};
    for (uint32_t i = 0; i < count; i++) {
        const tide_float4x4 world = through ? tide_mul_f4x4(*through, transforms[i]) : transforms[i];
        levels[i] = (uint8_t)pick_level(&d->meshes[mesh - 1u], &view, &world);
        counts[levels[i]]++;
    }
    d->leveled = 1;
    if (counts[levels[0]] == count) { // All at one level: as they are
        instances_placed(d, levels[0] ? level_mesh(d, mesh, levels[0]) : mesh, transforms, count, texture, filter, wrap);
        return;
    }
    uint32_t starts[TIDE_MESH_LEVELS + 1], next[TIDE_MESH_LEVELS + 1];
    uint32_t at = 0;
    for (uint32_t level = 0; level <= TIDE_MESH_LEVELS; level++) {
        starts[level] = next[level] = at;
        at += counts[level];
    }
    for (uint32_t i = 0; i < count; i++) {
        memcpy(sorted + (size_t)next[levels[i]]++ * sizeof(tide_float4x4), &transforms[i], sizeof(tide_float4x4));
    }
    for (uint32_t level = 0; level <= TIDE_MESH_LEVELS; level++) {
        if (counts[level] == 0) continue;
        const uint32_t drawn = level ? level_mesh(d, mesh, level) : mesh;
        instances_placed(d, drawn, (const tide_float4x4 *)(const void *)(sorted + (size_t)starts[level] * sizeof(tide_float4x4)),
                         counts[level], texture, filter, wrap);
    }
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
    tide_draw_mesh_3d_wrapped(d, mesh, transform, texture, filter, TIDE_WRAP_CLAMP);
}

void tide_draw_mesh_3d_wrapped(tide_draw_list *d, const tide_mesh *mesh, const tide_float4x4 transform,
                               const tide_texture *texture, const tide_filter filter, const tide_wrap wrap)
{
    if (!mesh || !mesh->vertices || !mesh->indices) return;
    bool fill;
    const uint32_t slot = tide_draw_mesh_slot(d, TIDE_MESH_MEMORY, (uint64_t)(uintptr_t)mesh->vertices,
                                              (uint64_t)(uintptr_t)mesh->indices, mesh->version, &fill);
    if (fill) tide_draw_mesh_fill(d, slot, mesh->vertices, mesh->vertex_count, mesh->indices, mesh->index_count);
    tide_draw_instance(d, slot, transform, texture_slot(d, texture), filter, wrap);
}

// ---------------------------------------------------------------------------
// One list after another

// The texture of `from`'s mesh command, as `d` has it.
static uint32_t append_texture(tide_draw_list *d, const tide_draw_list *from, const uint32_t texture)
{
    if (!texture) return 0;
    const tide_draw_texture *t = &from->textures[texture - 1u];
    if (t->space == TIDE_PIXELS_TARGET || t->space == TIDE_PIXELS_TARGET_DEPTH || t->space == TIDE_PIXELS_TARGET_CUBE) {
        return target_texture_version(d, t->space, t->id, t->target, t->version);
    }
    if (t->space == TIDE_PIXELS_PACK) {
        const uint32_t slot = pack_texture(d, t->id, t->width, t->height, t->pixels);
        d->textures[slot - 1u].compressed = t->compressed;
        return slot;
    }
    if (t->space == TIDE_PIXELS_PACK_CUBE) return pack_cube(d, t->id, t->width, (const uint8_t *const *)(uintptr_t)t->pixels);
    // An atlas page keeps its identity across source frames. Its appended
    // image is a snapshot: resetting and appending that source again cannot
    // replace pixels still used by earlier commands in this destination.
    uint64_t id = t->id;
    if (t->changes && (t->changes->kind == TIDE_SKINNED_ATLAS_KIND || t->changes->kind == ~TIDE_SKINNED_ATLAS_KIND)) {
        id = tide_hash_end(tide_hash_more(tide_hash(&id, sizeof id), &t->version, sizeof t->version)) | (UINT64_C(1) << 63);
    }
    // What `from` knows changed since the pixels `d` has is all that's copied, and `d` knows it of its own then.
    // The kind is a copy's, of that writer's: the writer itself, on `d`, would find none of its own numbers here
    const tide_draw_texture *mine = find_texture(d, t->space, id);
    const uint64_t had = mine ? mine->version : 0;
    const uint64_t kind = t->changes && t->changes->kind ? ~t->changes->kind : 0;
    uint8_t *pixels;
    bool kept;
    const uint32_t slot = kind ? tide_draw_texture_edit(d, t->space, id, t->version, t->width, t->height, t->format, kind,
                                                        &pixels, &kept)
                               : texture_place(d, t->space, id, t->version, t->width, t->height, t->format, 0, &pixels, &kept);
    if (!pixels) return slot;
    const size_t pixel = tide_texels_bytes(t->format), row = (size_t)t->width * pixel;
    const tide_draw_area *areas;
    uint32_t count;
    if (!kept || !tide_draw_texture_since(t, had, &areas, &count)) {
        memcpy(pixels, t->pixels, row * (size_t)t->height);
        tide_draw_texture_changed(d, slot, 0, 0, t->width, t->height);
        return slot;
    }
    for (uint32_t i = 0; i < count; i++) {
        const tide_draw_area *a = &areas[i];
        for (int32_t y = a->y; y < a->y + a->height; y++) {
            const size_t at = (size_t)y * row + (size_t)a->x * pixel;
            memcpy(pixels + at, t->pixels + at, (size_t)a->width * pixel);
        }
        tide_draw_texture_changed(d, slot, a->x, a->y, a->width, a->height);
    }
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

// What `from` had its destination's cameras, clip and stencil as, for `d`'s selected one.
static void append_place(tide_draw_list *d, const tide_draw_list *from, const tide_draw_place *place)
{
    tide_draw_place *p = &d->place;
    p->camera_2d = append_camera(d, from, place->camera_2d);
    if (place->camera_3d && place->camera_3d <= from->camera_3d_count) {
        const tide_draw_camera_3d_data *c = &from->cameras_3d[place->camera_3d - 1u];
        camera_3d(d, *c);
    } else {
        p->camera_3d = 0;
    }
    p->clip = append_clip(d, from, place->clip);
    p->stencil = place->stencil;
    refresh(d);
}

// The mesh of `from`'s that one of its commands names, as one of `d`'s: a level of detail with the
// mesh whose vertices it draws with, which `d` then has too. 0 for one that's no mesh.
static uint32_t append_mesh(tide_draw_list *d, const tide_draw_list *from, const uint32_t mesh)
{
    if (mesh == 0 || mesh > from->mesh_count) return 0;
    const tide_draw_mesh_data *m = &from->meshes[mesh - 1u];
    if (m->space == TIDE_MESH_NONE) return 0;
    uint32_t base = 0;
    if (m->base) {
        if (m->base > from->mesh_count || from->meshes[m->base - 1u].base) return 0;
        base = append_mesh(d, from, m->base);
        if (!base) return 0;
    }
    bool fill;
    const uint32_t slot = tide_draw_mesh_slot(d, m->space, m->id, m->other, m->version, &fill);
    if (!base) {
        if (fill) tide_draw_mesh_fill_bytes(d, slot, m->bytes, m->vertex_size, m->vertex_count, m->indices, m->index_count);
        return slot;
    }
    tide_draw_mesh_data *level = &d->meshes[slot - 1u];
    if (fill) {
        level->indices = grow(level->indices, 0, &level->index_capacity, m->index_count, sizeof(uint32_t), 192u);
        if (m->index_count) memcpy(level->indices, m->indices, (size_t)m->index_count * sizeof(uint32_t));
        level->index_count = m->index_count;
        level->vertex_size = m->vertex_size;
        level->vertex_count = m->vertex_count;
        level->level = m->level;
    }
    level->base = base;
    d->meshes[base - 1u].frame = d->frame;
    return slot;
}

// Whether `next` is more of the same draw as `c`, of another level of the mesh `family`: a run that follows
// it with nothing between, drawn with the same of everything else.
static bool level_follows(const tide_draw_list *d, const tide_draw_command *c, const tide_draw_command *next, const uint32_t family)
{
    if (next->kind != c->kind || next->blend != c->blend || next->depth != c->depth || next->cull != c->cull) return false;
    if (c->kind == TIDE_DRAW_MESH_3D) {
        const uint32_t mesh = next->mesh3.mesh;
        if (mesh == 0 || mesh > d->mesh_count || next->mesh3.count == 0) return false;
        const tide_draw_mesh_data *m = &d->meshes[mesh - 1u];
        return (m->base ? m->base : mesh) == family && next->mesh3.texture == c->mesh3.texture
            && next->mesh3.filter == c->mesh3.filter && next->mesh3.wrap == c->mesh3.wrap
            && c->mesh3.first + c->mesh3.count == next->mesh3.first && same_color(c->color, next->color);
    }
    if (c->shaded.run >= d->run_count || next->shaded.run >= d->run_count) return false;
    const tide_draw_run *a = &d->runs[c->shaded.run], *b = &d->runs[next->shaded.run];
    if (!b->shader || b->mesh == 0 || b->mesh > d->mesh_count || b->count == 0) return false;
    const tide_draw_mesh_data *m = &d->meshes[b->mesh - 1u];
    const uint32_t uniform_size = tide_shader_uniform_size(a->shader);
    return (m->base ? m->base : b->mesh) == family
        && (a->shader == b->shader || (a->shader->id && a->shader->id == b->shader->id)) && a->sampling == b->sampling
        && a->first + (uint64_t)a->count * tide_shader_instance_size(a->shader) == b->first
        && memcmp(a->textures, b->textures, sizeof a->textures) == 0 && same_color(a->tint, b->tint) && a->moved == b->moved
        && (!a->moved || memcmp(&a->transform, &b->transform, sizeof a->transform) == 0)
        && (uniform_size == 0 || a->uniforms == b->uniforms
            || memcmp(d->uniforms + a->uniforms, d->uniforms + b->uniforms, uniform_size) == 0);
}

void tide_draw_gather_levels(const tide_draw_list *d)
{
    if (!d || !d->leveled) return;
    for (uint32_t i = 0; i < d->count;) {
        tide_draw_command *c = &d->commands[i];
        uint32_t end = i + 1u;
        const bool shaded = c->kind == TIDE_DRAW_SHADED;
        if (!shaded && c->kind != TIDE_DRAW_MESH_3D) {
            i = end;
            continue;
        }
        const tide_draw_run *run = shaded && c->shaded.run < d->run_count ? &d->runs[c->shaded.run] : NULL;
        const uint32_t mesh = shaded ? (run && run->shader && run->count ? run->mesh : 0u) : (c->mesh3.count ? c->mesh3.mesh : 0u);
        if (mesh == 0 || mesh > d->mesh_count) {
            i = end;
            continue;
        }
        const uint32_t family = d->meshes[mesh - 1u].base ? d->meshes[mesh - 1u].base : mesh;
        if (family > d->mesh_count || d->meshes[family - 1u].levels == 0) {
            i = end;
            continue;
        }
        while (end < d->count && level_follows(d, &d->commands[end - 1u], &d->commands[end], family)) end++;
        // Its runs' levels and how many instances each has in all: nothing to do where each level is one run
        // already, the finest first
        uint32_t counts[TIDE_MESH_LEVELS + 1] = {0}, meshes[TIDE_MESH_LEVELS + 1] = {0};
        uint32_t total = 0, before = 0;
        bool sorted = true;
        for (uint32_t k = i; k < end; k++) {
            const tide_draw_command *each = &d->commands[k];
            const uint32_t named = shaded ? d->runs[each->shaded.run].mesh : each->mesh3.mesh;
            const uint32_t level = d->meshes[named - 1u].level <= TIDE_MESH_LEVELS ? d->meshes[named - 1u].level : 0u;
            const uint32_t count = shaded ? d->runs[each->shaded.run].count : each->mesh3.count;
            if (k > i && level <= before) sorted = false;
            before = level;
            counts[level] += count;
            meshes[level] = named;
            total += count;
        }
        if (sorted) {
            i = end;
            continue;
        }
        const size_t size = shaded ? tide_shader_instance_size(run->shader) : sizeof(tide_float4x4);
        const uint32_t first = shaded ? run->first : c->mesh3.first;
        uint8_t *instances = shaded ? d->instances + first : (uint8_t *)(void *)(d->matrices + first);
        uint32_t starts[TIDE_MESH_LEVELS + 1], next[TIDE_MESH_LEVELS + 1];
        uint32_t at = 0;
        for (uint32_t level = 0; level <= TIDE_MESH_LEVELS; level++) {
            starts[level] = next[level] = at;
            at += counts[level];
        }
        if (size) {
            uint8_t *sorting = tide_alloc((size_t)total * size);
            uint32_t from = 0;
            for (uint32_t k = i; k < end; k++) {
                const tide_draw_command *each = &d->commands[k];
                const uint32_t named = shaded ? d->runs[each->shaded.run].mesh : each->mesh3.mesh;
                const uint32_t level = d->meshes[named - 1u].level <= TIDE_MESH_LEVELS ? d->meshes[named - 1u].level : 0u;
                const uint32_t count = shaded ? d->runs[each->shaded.run].count : each->mesh3.count;
                memcpy(sorting + (size_t)next[level] * size, instances + (size_t)from * size, (size_t)count * size);
                next[level] += count;
                from += count;
            }
            memcpy(instances, sorting, (size_t)total * size);
            free(sorting);
        }
        // A run a level in the first commands, and the rest draw nothing
        uint32_t k = i;
        for (uint32_t level = 0; level <= TIDE_MESH_LEVELS; level++) {
            if (counts[level] == 0) continue;
            tide_draw_command *each = &d->commands[k++];
            if (shaded) {
                tide_draw_run *r = &d->runs[each->shaded.run];
                r->mesh = meshes[level];
                r->first = first + (uint32_t)(starts[level] * size);
                r->count = counts[level];
            } else {
                each->mesh3.mesh = meshes[level];
                each->mesh3.first = first + starts[level];
                each->mesh3.count = counts[level];
            }
        }
        for (; k < end; k++) {
            tide_draw_command *each = &d->commands[k];
            if (shaded) d->runs[each->shaded.run] = (tide_draw_run){0};
            else each->mesh3.mesh = each->mesh3.count = 0;
        }
        i = end;
    }
}

void tide_draw_append(tide_draw_list *d, const tide_draw_list *from)
{
    tide_draw_prepare(from, NULL);
    tide_draw_defaults(d);
    reserve(d, from->count);
    d->remap = grow(d->remap, 0, &d->remap_capacity, from->clip_count, sizeof(uint32_t), 16u);
    if (from->clip_count) memset(d->remap, 0, (size_t)from->clip_count * sizeof(uint32_t));
    const uint32_t base = tide_draw_vertices(d, from->vertices, from->vertex_count);
    for (uint32_t i = 0; i < from->count; i++) {
        const tide_draw_command *c = &from->commands[i];
        d->place.blend = c->blend;
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
            else if (c->camera.matrix < from->matrix_count) {
                camera_3d(d, camera_of_matrix(from->matrices[c->camera.matrix], c->camera.fit != 0, c->camera.mirrored != 0));
            }
        } else if (c->kind == TIDE_DRAW_CLIP || c->kind == TIDE_DRAW_NO_CLIP) {
            d->place.clip = c->kind == TIDE_DRAW_CLIP ? append_clip(d, from, c->clip) : 0;
            refresh(d);
        } else if (c->kind == TIDE_DRAW_STENCIL) {
            d->place.stencil = tide_stencil_packed(tide_stencil_unpacked(c->stencil));
            refresh(d);
        } else if (c->kind == TIDE_DRAW_TEXT) {
            const uint32_t turned = c->words.turned;
            push_text(d, from->text + c->text, c->a, c->b.x, c->color, (tide_anchor)c->words.anchor,
                      c->words.bold ? from->bold : TIDE_FONT_REGULAR,
                      turned && turned <= from->turn_count ? &from->turns[turned - 1u] : NULL);
        } else if (c->kind == TIDE_DRAW_MESH) {
            // Its indices are places in `from`'s vertices, which are `d`'s from `base`
            tide_draw_triangles_with(d, base, from->indices + c->mesh.first, c->mesh.count,
                                     append_texture(d, from, c->mesh.texture), (tide_filter)c->mesh.filter,
                                     (tide_wrap)c->mesh.wrap);
        } else if (c->kind == TIDE_DRAW_MESH_3D) {
            if (!c->mesh3.mesh || c->mesh3.count == 0) continue;
            const uint32_t slot = append_mesh(d, from, c->mesh3.mesh);
            if (!slot) continue;
            const uint32_t texture = append_texture(d, from, c->mesh3.texture);
            mesh_instances(d, slot, from->matrices + c->mesh3.first, c->mesh3.count, texture,
                           (tide_filter)c->mesh3.filter, (tide_wrap)c->mesh3.wrap, (tide_blend)c->blend,
                           c->depth <= TIDE_DEPTH_NONE ? (tide_depth)c->depth : TIDE_DEPTH_READ_WRITE,
                           c->cull | SIDES_KNOWN, c->color, NULL, false);
        } else if (c->kind == TIDE_DRAW_SHADED || c->kind == TIDE_DRAW_BLIT) {
            const tide_draw_run *r = &from->runs[c->shaded.run];
            if (!r->shader || !r->mesh || r->count == 0) continue;
            const uint32_t slot = append_mesh(d, from, r->mesh);
            if (!slot) continue;
            uint32_t textures[TIDE_SHADER_TEXTURES];
            for (uint32_t t = 0; t < TIDE_SHADER_TEXTURES; t++) textures[t] = append_texture(d, from, r->textures[t]);
            const uint32_t size = tide_shader_instance_size(r->shader);
            shaded_instances(d, r->shader, slot, size ? from->instances + r->first : NULL, r->count,
                             tide_shader_uniform_size(r->shader) ? from->uniforms + r->uniforms : NULL, textures,
                             r->sampling, (tide_draw_kind)c->kind, (tide_blend)c->blend, (tide_depth)c->depth, c->cull,
                             r, NULL);
        } else if (c->kind == TIDE_DRAW_SHAPE) {
            if (c->shape < from->shape_count) push_shape(d, c->a, c->b, c->color, from->shapes[c->shape]);
        } else {
            push(d, (tide_draw_kind)c->kind, c->a, c->b, c->color);
        }
    }
    for (uint32_t i = 0; i <= d->target_count; i++) place_of(d, i)->blend = TIDE_BLEND_ALPHA;
    // What `from` set after the last command that drew isn't in its commands: its destinations' places have it
    const uint32_t ended = d->target;
    for (uint32_t i = 0; i <= from->target_count; i++) {
        const tide_draw_place *place = i == from->target ? &from->place : i ? &from->targets[i - 1u].place : &from->window;
        if (place->camera_2d == place->played_2d && place->camera_3d == place->played_3d && place->clip == place->played_clip
            && place->stencil == place->played_stencil) {
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
