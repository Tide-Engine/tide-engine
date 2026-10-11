#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

enum { C = TIDE_TARGET_RGBA8, F = TIDE_TARGET_RGBA16F };

// What the target `id` holds is `count` color images, the first of `first`, drawn with `samples` a pixel.
static bool holds(const uint64_t id, const uint32_t count, const tide_target_format first, const uint32_t samples)
{
    const tide_target_formats held = tide_render_target_formats(id);
    return held.count == count && (count == 0 || held.color[0] == first) && tide_render_target_samples(id) == samples;
}

// The command that draws through the shader named `name`.
static const tide_draw_command *drawn_through(const char *name, uint32_t nth)
{
    for (uint32_t i = 0; i < draw.count; i++) {
        const tide_draw_command *c = &draw.commands[i];
        if (c->kind != TIDE_DRAW_SHADED && c->kind != TIDE_DRAW_BLIT) continue;
        const tide_draw_run *r = &draw.runs[c->shaded.run];
        if (r->shader && strcmp(r->shader->name, name) == 0 && nth-- == 0) return c;
    }
    return NULL;
}

// The texture of the list's that the run reads first.
static const tide_draw_texture *read_by(const tide_draw_command *command)
{
    const tide_draw_run *run = &draw.runs[command->shaded.run];
    const uint32_t at = run->shader->texture_count ? run->textures[0] : 0;
    return at && at <= draw.texture_count ? &draw.textures[at - 1u] : NULL;
}

TIDE_TEST(render_samples_are_said_where_a_target_is_made_and_kept_in_its_id)
{
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    gui.width = 1280.0f, gui.height = 720.0f;
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);

    // Each way to say them: after a size, after its pixels per unit, after one format and after
    // several, for the window, after its factor and its formats, from a constant and a variable
    const Stage *stage = &local.Stage;
    TIDE_CHECK(holds(stage->plain.id, 1, C, 1) && holds(stage->one.id, 1, C, 1));
    TIDE_CHECK(holds(stage->four.id, 1, C, 4) && stage->four.size.width == 64.0f && stage->four.size.height == 48.0f);
    TIDE_CHECK(stage->four.size.scale == 0.0f && tide_render_target_live(stage->four.id));
    TIDE_CHECK(holds(stage->exact.id, 1, C, 2) && stage->exact.size.scale == 1.0f && stage->exact.size.width == 256.0f);
    TIDE_CHECK(holds(stage->wide.id, 1, F, 8) && stage->wide.size.width == 32.0f);
    TIDE_CHECK(holds(stage->several.id, 2, C, 4) && stage->several.size.scale == 2.0f);
    TIDE_CHECK(tide_render_target_formats(stage->several.id).color[1] == F);
    TIDE_CHECK(holds(stage->window.id, 1, C, 4) && stage->window.size.window == 1.0f);
    TIDE_CHECK(holds(stage->half.id, 1, C, 2) && stage->half.size.window == 0.5f);
    TIDE_CHECK(holds(stage->bright.id, 1, F, 4) && stage->bright.size.window == 1.0f);
    TIDE_CHECK(holds(stage->both.id, 2, C, 8) && stage->both.size.window == 0.25f);
    TIDE_CHECK(holds(stage->counted.id, 1, C, 4) && holds(stage->made.id, 1, F, 4) && stage->made.size.width == 24.0f);
    // A number that's none of 1, 2, 4 and 8, known only as it runs, is the one of them under it
    TIDE_CHECK(holds(stage->odd.id, 1, C, 4) && holds(stage->many.id, 1, C, 8));
    // ...and a target of depth alone has one, as nothing could be read of it
    TIDE_CHECK(holds(stage->shadow.id, 0, C, 1) && tide_render_target_live(stage->shadow.id));

    // Its images are textures like any target's, which say whose they are, samples and all: the
    // target's own id is what a renderer finds its image by
    TIDE_CHECK(stage->smooth.target.id == stage->four.id && stage->smooth.image.pixels == NULL);
    TIDE_CHECK(tide_render_target_whole(stage->second.target.id) == stage->several.id
               && tide_render_target_image(stage->second.target.id) == 1
               && tide_render_target_samples(stage->second.target.id) == 4);
    TIDE_CHECK(stage->deep.target.id == stage->four.id);
    TIDE_CHECK(stage->pass.into.id == stage->wide.id && stage->pass.read.target.id == stage->wide.id);

    // It's a destination like any other, by the same id, and a source
    uint32_t four = 0, selected = 0, clears = 0, meshes = 0, rects = 0;
    for (uint32_t i = 0; i < draw.target_count; i++) {
        if (draw.targets[i].id == stage->four.id) four = i + 1u;
    }
    TIDE_REQUIRE(four != 0);
    for (uint32_t i = 0; i < draw.count; i++) {
        const tide_draw_command *c = &draw.commands[i];
        if (c->kind == TIDE_DRAW_TARGET) selected = c->target;
        if (selected != four) continue;
        clears += c->kind == TIDE_DRAW_CLEAR;
        meshes += c->kind == TIDE_DRAW_MESH_3D;
        rects += c->kind == TIDE_DRAW_RECT;
    }
    TIDE_CHECK(clears == 1 && meshes == 1 && rects == 1);
    const tide_draw_command *show = drawn_through("Show on Vertex", 0), *second = drawn_through("Show on Vertex", 1);
    TIDE_REQUIRE(show && second && read_by(show) && read_by(second));
    TIDE_CHECK(read_by(show)->space == TIDE_PIXELS_TARGET && read_by(show)->id == stage->four.id);
    TIDE_CHECK(read_by(second)->id == stage->second.target.id);
    // Its depth is recorded as any target's: it's the renderer that passes the draw by
    const tide_draw_command *deep = drawn_through("Deep on Vertex", 0);
    TIDE_REQUIRE(deep && read_by(deep));
    TIDE_CHECK(read_by(deep)->space == TIDE_PIXELS_TARGET_DEPTH && tide_render_target_samples(read_by(deep)->id) == 4);

    // Another build of the game, in the same memory: every reference is the target it was
    const tide_render_target first = stage->four, wide = stage->wide, several = stage->several;
    tide_local_rebind_resources(&local, true);
    TIDE_CHECK(local.Stage.four.id == first.id && local.Stage.smooth.target.id == first.id);
    TIDE_CHECK(local.Stage.deep.target.id == first.id && local.Stage.pass.into.id == wide.id);

    // ...and in other memory, as a page's next program has it: each target is made again with the
    // samples it had, which its id said
    const uint32_t size = tide_local_pack(&local, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(bytes != NULL);
    TIDE_REQUIRE(tide_local_pack(&local, bytes, size) == size);
    tide_local restored = {0};
    TIDE_REQUIRE(tide_local_unpack(&restored, bytes, size));
    free(bytes);
    tide_local_rebind_resources(&restored, false);
    const Stage *again = &restored.Stage;
    TIDE_REQUIRE(tide_render_target_managed(again->four.id) && again->four.id != first.id);
    TIDE_CHECK(holds(again->plain.id, 1, C, 1) && holds(again->one.id, 1, C, 1));
    TIDE_CHECK(holds(again->four.id, 1, C, 4) && again->four.size.width == 64.0f);
    TIDE_CHECK(holds(again->exact.id, 1, C, 2) && again->exact.size.scale == 1.0f);
    TIDE_CHECK(holds(again->wide.id, 1, F, 8) && again->wide.id != wide.id);
    TIDE_CHECK(holds(again->several.id, 2, C, 4) && again->several.id != several.id);
    TIDE_CHECK(holds(again->window.id, 1, C, 4) && again->window.size.window == 1.0f);
    TIDE_CHECK(holds(again->half.id, 1, C, 2) && holds(again->bright.id, 1, F, 4) && holds(again->both.id, 2, C, 8));
    TIDE_CHECK(holds(again->shadow.id, 0, C, 1) && holds(again->made.id, 1, F, 4));
    TIDE_CHECK(again->smooth.target.id == again->four.id && again->deep.target.id == again->four.id);
    TIDE_CHECK(tide_render_target_whole(again->second.target.id) == again->several.id
               && tide_render_target_image(again->second.target.id) == 1);
    TIDE_CHECK(again->pass.into.id == again->wide.id && again->pass.read.target.id == again->wide.id);
    tide_render_targets_recreated();

    // A second frame draws through the same references
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    TIDE_CHECK(local.Stage.frames == 2 && drawn_through("Show on Vertex", 0) != NULL);
    TIDE_CHECK(local.Stage.four.id == first.id && holds(local.Stage.four.id, 1, C, 4));

    tide_local_free(&restored);
    tide_local_free(&local);
    tide_draw_free(&draw);
    tide_world_free(&world);
    tide_render_targets_free();
}
