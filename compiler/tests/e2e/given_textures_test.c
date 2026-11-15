#include <stdint.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

static const uint8_t red[4] = {255, 0, 0, 255}, green[4] = {0, 255, 0, 255}, blue[4] = {0, 0, 255, 255};
static const uint8_t sky[4] = {40, 80, 120, 255};

static tide_texture_ref image(const uint8_t *pixels)
{
    return (tide_texture_ref){.image = {.pixels = pixels, .width = 1, .height = 1, .version = 1}};
}

// The `at`th run through the shader of this name, in the order they were drawn.
static const tide_draw_run *through(const char *name, uint32_t at)
{
    for (uint32_t i = 0; i < draw.run_count; i++) {
        const tide_draw_run *r = &draw.runs[i];
        if (r->shader && strcmp(r->shader->name, name) == 0 && at-- == 0) return r;
    }
    return NULL;
}

// Whether a run's texture `t` holds these pixels: a texture of C's memory is known by their address.
static bool holds(const tide_draw_run *r, const uint32_t t, const uint8_t *pixels)
{
    if (!r || t >= r->shader->texture_count || r->textures[t] == 0) return false;
    const tide_draw_texture *texture = &draw.textures[r->textures[t] - 1u];
    return texture->space == TIDE_PIXELS_MEMORY && texture->id == (uint64_t)(uintptr_t)pixels;
}

// Whether a run reads sampler `s` blended: its filter's bit (TIDE_SAMPLING_*, the first eight samplers').
static bool blended(const tide_draw_run *r, const uint32_t s)
{
    return (r->sampling >> s & 1u) != 0;
}

TIDE_TEST(several_textures_reach_a_draw_in_order)
{
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    local.Art.a = image(red);
    local.Art.b = image(green);
    local.Art.c = image(blue);
    local.Stage.sky = image(sky);
    local.Stage.strength = 0.5f;
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);

    // The frag's three, each its texture of the shader's in the order it samples them: two draws, each its own run
    const tide_draw_run *given = through("Three on Corner", 0);
    const tide_draw_run *turned = through("Three on Corner", 1);
    TIDE_REQUIRE(given && turned && given->shader == turned->shader && given->shader->texture_count == 3);
    TIDE_CHECK(holds(given, 0, red) && holds(given, 1, green) && holds(given, 2, blue));
    TIDE_CHECK(holds(turned, 0, blue) && holds(turned, 1, green) && holds(turned, 2, red));
    // The filter the draw says after a texture is that texture's alone: the second's sampler reads its nearest
    // pixel in the first draw and blends in the second, and the first's blends in both
    TIDE_CHECK(!blended(given, 1) && blended(turned, 1));
    TIDE_CHECK(blended(given, 0) && blended(turned, 0));
    TIDE_CHECK(!blended(given, 2) && !blended(turned, 2)); // Said at its Sample

    // A blit's source and the texture after it, before its destination
    const tide_draw_run *blit = through("Two on Vertex", 0);
    TIDE_REQUIRE(blit && blit->shader->texture_count == 2 && blit->shader->blit);
    TIDE_CHECK(holds(blit, 0, red) && holds(blit, 1, green));

    // An instanced draw's, the same for all its instances
    const tide_draw_run *patched = through("Patched on Corner", 0);
    TIDE_REQUIRE(patched && patched->count == 2 && patched->shader->texture_count == 2);
    TIDE_CHECK(holds(patched, 0, green) && holds(patched, 1, blue));

    // A singleton's texture, sampled by a function it's handed and by its own method: the shader's one texture
    const tide_draw_run *sunny = through("Sunny on Corner", 0);
    TIDE_REQUIRE(sunny && sunny->shader->texture_count == 1);
    TIDE_CHECK(holds(sunny, 0, sky));

    tide_local_free(&local);
    tide_draw_free(&draw);
    tide_world_free(&world);
}
