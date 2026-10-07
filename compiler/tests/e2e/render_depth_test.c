#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

// The first run of instances drawn through the shader named `name`.
static const tide_draw_run *run_through(const char *name)
{
    for (uint32_t i = 0; i < draw.count; i++) {
        if (draw.commands[i].kind != TIDE_DRAW_SHADED) continue;
        const tide_draw_run *r = &draw.runs[draw.commands[i].shaded.run];
        if (r->shader && strcmp(r->shader->name, name) == 0) return r;
    }
    return NULL;
}

// The run's texture `at`, as the list has it.
static const tide_draw_texture *texture_of(const tide_draw_run *run, const uint32_t at)
{
    return run->textures[at] && run->textures[at] <= draw.texture_count ? &draw.textures[run->textures[at] - 1u] : NULL;
}

static tide_entity caster(void)
{
    for (uint32_t i = 0; i < world.entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&world.entities, i);
        if (tide_has_Caster(&world, e)) return e;
    }
    return (tide_entity){0};
}

TIDE_TEST(render_depth_targets_say_what_they_hold_and_frags_read_their_depth)
{
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    gui.width = 1280.0f, gui.height = 720.0f;
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1.0f, NULL, &local, &draw, &gui);

    // Each way to say what a target holds: after its size, after its pixels per unit, after a
    // screen's factor, alone for all of the screen, by name for color, and from a variable
    const Lamp *lamp = &local.Lamp;
    TIDE_CHECK(tide_render_target_format(lamp->map.id) == TIDE_TARGET_DEPTH_ONLY);
    TIDE_CHECK(lamp->map.size.width == 64.0f && lamp->map.size.height == 48.0f && lamp->map.size.scale == 0.0f);
    TIDE_CHECK(tide_render_target_format(lamp->exact.id) == TIDE_TARGET_DEPTH_ONLY && lamp->exact.size.scale == 1.0f);
    TIDE_CHECK(tide_render_target_format(lamp->screen.id) == TIDE_TARGET_DEPTH_ONLY && lamp->screen.size.window == 1.0f);
    TIDE_CHECK(tide_render_target_format(lamp->half.id) == TIDE_TARGET_DEPTH_ONLY && lamp->half.size.window == 0.5f);
    TIDE_CHECK(tide_render_target_format(lamp->scene.id) == TIDE_TARGET_COLOR && tide_render_target_live(lamp->scene.id));
    TIDE_CHECK(tide_render_target_format(lamp->picked.id) == TIDE_TARGET_DEPTH_ONLY && lamp->picked.size.width == 32.0f);

    // A target's depth is a reference to the target, wherever it's kept: a singleton's field, a
    // struct's, a function's result, and a match component's local field
    TIDE_CHECK(lamp->shadow.target.id == lamp->map.id && lamp->sceneDepth.target.id == lamp->scene.id);
    TIDE_CHECK(lamp->light.map.id == lamp->exact.id && lamp->light.shadow.target.id == lamp->exact.id);
    const tide_entity entity = caster();
    TIDE_REQUIRE(entity.generation != 0);
    TIDE_CHECK(tide_get_Caster_local(&local, entity).seen.target.id == lamp->exact.id);

    // A frag's depths are textures of the list's, each read the way the frag reads it: compared,
    // or as a number, which are two textures of one depth
    const tide_draw_run *lit = run_through("Lit on Vertex3");
    TIDE_REQUIRE(lit && lit->shader->texture_count == 3);
    TIDE_CHECK(lit->shader->texture_reads[0] == TIDE_SHADER_READ_COMPARE && lit->shader->texture_reads[1] == TIDE_SHADER_READ_DEPTH
               && lit->shader->texture_reads[2] == TIDE_SHADER_READ_DEPTH);
    for (uint32_t i = 0; i < 3; i++) {
        const tide_draw_texture *t = texture_of(lit, i);
        TIDE_REQUIRE(t != NULL);
        TIDE_CHECK(t->space == TIDE_PIXELS_TARGET_DEPTH && t->pixels == NULL);
        TIDE_CHECK(t->id == (i == 1 ? lamp->scene.id : lamp->map.id));
    }
    TIDE_CHECK(lit->textures[0] == lit->textures[2] && lit->sampling == 0);
    // A vert's come first
    const tide_draw_run *sunk = run_through("Sink and Lit on Vertex3");
    TIDE_REQUIRE(sunk && sunk->shader->texture_count == 3);
    TIDE_CHECK(sunk->shader->texture_reads[0] == TIDE_SHADER_READ_DEPTH && sunk->shader->texture_reads[1] == TIDE_SHADER_READ_COMPARE
               && sunk->shader->texture_reads[2] == TIDE_SHADER_READ_DEPTH);
    // ...and one in a component's local field is its entity's
    const tide_draw_run *seen = run_through("Seen on Vertex3");
    TIDE_REQUIRE(seen && seen->shader->texture_count == 1 && seen->shader->texture_reads[0] == TIDE_SHADER_READ_COMPARE);
    TIDE_REQUIRE(texture_of(seen, 0) != NULL);
    TIDE_CHECK(texture_of(seen, 0)->space == TIDE_PIXELS_TARGET_DEPTH && texture_of(seen, 0)->id == lamp->exact.id);
    // The shaders say so in both languages
    TIDE_CHECK(strstr(lit->shader->glsl_fragment, "uniform highp sampler2DShadow pixels0;") != NULL);
    TIDE_CHECK(strstr(lit->shader->glsl_fragment, "uniform highp sampler2D pixels1;") != NULL);

    // The list draws into the target of depth alone as into any other
    uint32_t into = 0;
    for (uint32_t i = 0; i < draw.target_count; i++) {
        if (draw.targets[i].id == lamp->map.id) into = i + 1u;
    }
    TIDE_REQUIRE(into != 0);
    uint32_t clears = 0, meshes = 0, shaded = 0, rects = 0, selected = 0;
    for (uint32_t i = 0; i < draw.count; i++) {
        const tide_draw_command *c = &draw.commands[i];
        if (c->kind == TIDE_DRAW_TARGET) selected = c->target;
        if (selected != into) continue;
        clears += c->kind == TIDE_DRAW_CLEAR;
        meshes += c->kind == TIDE_DRAW_MESH_3D; // With no frag: the engine's own look, never "depth only"
        shaded += c->kind == TIDE_DRAW_SHADED;
        rects += c->kind == TIDE_DRAW_RECT;
    }
    TIDE_CHECK(clears == 1 && meshes == 1 && shaded == 1 && rects == 1);

    // Another build of the game, in the same memory: every reference is the target it was
    const tide_render_target map = lamp->map, exact = lamp->exact, scene = lamp->scene;
    tide_local_rebind_resources(&local, true);
    TIDE_CHECK(local.Lamp.map.id == map.id && local.Lamp.shadow.target.id == map.id);
    TIDE_CHECK(local.Lamp.light.shadow.target.id == exact.id && local.Lamp.sceneDepth.target.id == scene.id);
    TIDE_CHECK(tide_get_Caster_local(&local, entity).seen.target.id == exact.id);

    // ...and in other memory, as a page's next program has it: each target is made again
    // holding what it held, and its depth's references are the new target's
    const uint32_t size = tide_local_pack(&local, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(bytes != NULL);
    TIDE_REQUIRE(tide_local_pack(&local, bytes, size) == size);
    tide_local restored = {0};
    TIDE_REQUIRE(tide_local_unpack(&restored, bytes, size));
    free(bytes);
    tide_local_rebind_resources(&restored, false);
    const Lamp *again = &restored.Lamp;
    TIDE_REQUIRE(tide_render_target_managed(again->map.id) && again->map.id != map.id);
    TIDE_CHECK(tide_render_target_format(again->map.id) == TIDE_TARGET_DEPTH_ONLY && again->map.size.width == 64.0f);
    TIDE_CHECK(tide_render_target_format(again->exact.id) == TIDE_TARGET_DEPTH_ONLY && again->exact.size.scale == 1.0f);
    TIDE_CHECK(tide_render_target_format(again->half.id) == TIDE_TARGET_DEPTH_ONLY && again->half.size.window == 0.5f);
    TIDE_CHECK(tide_render_target_format(again->scene.id) == TIDE_TARGET_COLOR);
    TIDE_CHECK(again->shadow.target.id == again->map.id && again->sceneDepth.target.id == again->scene.id);
    TIDE_CHECK(again->light.map.id == again->exact.id && again->light.shadow.target.id == again->exact.id);
    TIDE_CHECK(tide_get_Caster_local(&restored, entity).seen.target.id == again->exact.id);
    tide_render_targets_recreated();

    // A second frame draws through the same references
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1.0f, NULL, &local, &draw, &gui);
    TIDE_CHECK(local.Lamp.frames == 2 && run_through("Lit on Vertex3") != NULL);

    tide_local_free(&restored);
    tide_local_free(&local);
    tide_draw_free(&draw);
    tide_world_free(&world);
    tide_render_targets_free();
}
