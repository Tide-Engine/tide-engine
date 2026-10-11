#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

// The `nth` command that draws instances through the shader named `name`.
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

static const tide_draw_run *run_through(const char *name)
{
    const tide_draw_command *c = drawn_through(name, 0);
    return c ? &draw.runs[c->shaded.run] : NULL;
}

// Whether the run samples the texture `id` of a target, with the wrap `wrap`.
static bool samples(const tide_draw_run *run, const uint64_t id, const tide_wrap wrap)
{
    for (uint32_t i = 0; i < run->shader->texture_count; i++) {
        const uint32_t at = run->textures[i];
        if (!at || at > draw.texture_count) continue;
        const tide_draw_texture *t = &draw.textures[at - 1u];
        if (t->space == TIDE_PIXELS_TARGET && t->id == id && t->pixels == NULL && tide_sampling_wrap(run->sampling, i) == wrap) return true;
    }
    return false;
}

// What the target `id` holds is these formats, in order.
static bool holds(const uint64_t id, const uint32_t count, const tide_target_format a, const tide_target_format b,
                  const tide_target_format c, const tide_target_format d)
{
    const tide_target_formats held = tide_render_target_formats(id);
    const tide_target_format wanted[4] = {a, b, c, d};
    if (held.count != count) return false;
    for (uint32_t i = 0; i < count; i++) {
        if (held.color[i] != wanted[i]) return false;
    }
    return true;
}

// The destination the command at `at` draws into: 0 for the window, or the target's id.
static uint64_t destination_of(const tide_draw_command *command)
{
    uint32_t selected = 0;
    for (const tide_draw_command *c = draw.commands; c <= command; c++) {
        if (c->kind == TIDE_DRAW_TARGET) selected = c->target;
    }
    return selected ? draw.targets[selected - 1u].id : 0;
}

static tide_entity prop(void)
{
    for (uint32_t i = 0; i < world.entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&world.entities, i);
        if (tide_has_Prop(&world, e)) return e;
    }
    return (tide_entity){0};
}

#define C TIDE_TARGET_RGBA8
#define F TIDE_TARGET_RGBA16F

TIDE_TEST(render_colors_targets_hold_several_and_frags_write_them_in_order)
{
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    gui.width = 1280.0f, gui.height = 720.0f;
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);

    // Each way to say what a target holds, a format for each color image: after its size, after
    // its pixels per unit, for all of the screen, after a screen's factor, four of them, one, and
    // from variables, through a function
    const Stage *stage = &local.Stage;
    TIDE_CHECK(holds(stage->two.id, 2, C, F, 0, 0) && stage->two.size.width == 64.0f && stage->two.size.height == 48.0f);
    TIDE_CHECK(stage->two.size.scale == 0.0f && tide_render_target_live(stage->two.id));
    TIDE_CHECK(holds(stage->exact.id, 3, C, F, F, 0) && stage->exact.size.scale == 1.0f && stage->exact.size.width == 256.0f);
    TIDE_CHECK(holds(stage->screen.id, 2, F, C, 0, 0) && stage->screen.size.window == 1.0f);
    TIDE_CHECK(holds(stage->half.id, 3, C, C, F, 0) && stage->half.size.window == 0.5f);
    TIDE_CHECK(holds(stage->four.id, 4, C, C, C, C) && holds(stage->bright.id, 1, F, 0, 0, 0));
    TIDE_CHECK(holds(stage->picked.id, 2, F, C, 0, 0) && stage->picked.size.width == 32.0f);
    // Among several, a format that's no color's, known only as it runs, is a Format.Rgba8 image
    TIDE_CHECK(holds(stage->odd.id, 2, F, C, 0, 0));

    // Each color image is a Texture: the first is the target's `color`, and the others say
    // which they are, wherever they're kept: a field, an index worked out as it runs, a struct,
    // a function's result and a match component's local field
    TIDE_CHECK(stage->albedo.target.id == stage->two.id && stage->albedo.image.pixels == NULL);
    TIDE_CHECK(tide_render_target_whole(stage->normals.target.id) == stage->two.id && tide_render_target_image(stage->normals.target.id) == 1);
    TIDE_CHECK(tide_render_target_whole(stage->last.target.id) == stage->four.id && tide_render_target_image(stage->last.target.id) == 3);
    TIDE_CHECK(tide_render_target_whole(stage->byIndex.target.id) == stage->exact.id && tide_render_target_image(stage->byIndex.target.id) == 2);
    TIDE_CHECK(stage->normals.target.size.width == 64.0f && tide_render_target_live(stage->normals.target.id));
    // One the target hasn't got is named all the same: the renderer passes its draws by
    TIDE_CHECK(tide_render_target_whole(stage->missing.target.id) == stage->two.id && tide_render_target_image(stage->missing.target.id) == 3);
    TIDE_CHECK(stage->pass.into.id == stage->exact.id && tide_render_target_whole(stage->pass.read.target.id) == stage->exact.id
               && tide_render_target_image(stage->pass.read.target.id) == 1);
    const tide_entity entity = prop();
    TIDE_REQUIRE(entity.generation != 0);
    const uint64_t seen = tide_get_Prop_local(&local, entity).seen.target.id;
    TIDE_CHECK(tide_render_target_whole(seen) == stage->exact.id && tide_render_target_image(seen) == 1);

    // A frag's outputs are its shader's, in the order it takes them: the pixel's color, then one
    // for each image after a target's first. Both languages say where each goes
    const tide_draw_run *fill = run_through("Fill on Corner");
    TIDE_REQUIRE(fill && fill->shader->outputs == 2 && tide_shader_outputs(fill->shader) == 2);
    TIDE_CHECK(strstr(fill->shader->glsl_fragment, "layout(location = 0) out vec4 tide_pixel;\n") != NULL);
    TIDE_CHECK(strstr(fill->shader->glsl_fragment, "layout(location = 1) out vec4 tide_pixel1;\n") != NULL);
    const tide_draw_run *split = run_through("Split on Vertex");
    TIDE_REQUIRE(split && split->shader->outputs == 4 && split->shader->texture_count == 1);
    TIDE_CHECK(strstr(split->shader->glsl_fragment, "layout(location = 3) out vec4 tide_pixel3;\n") != NULL);
    TIDE_CHECK(samples(split, stage->two.id, TIDE_WRAP_CLAMP));
    // One output is as a frag always was: its shader says nothing of them
    const tide_draw_run *light = run_through("Light on Vertex");
    TIDE_REQUIRE(light && light->shader->outputs == 0 && tide_shader_outputs(light->shader) == 1);
    TIDE_CHECK(strstr(light->shader->glsl_fragment, "out vec4 tide_pixel;\n") != NULL);
    TIDE_CHECK(strstr(light->shader->glsl_fragment, "layout(location = 0) out") == NULL);
    // It reads the images another frag wrote, each a texture of the list's: the first target's
    // first and second, and the four's last, repeated
    TIDE_REQUIRE(light->shader->texture_count == 3);
    TIDE_CHECK(samples(light, stage->two.id, TIDE_WRAP_CLAMP) && samples(light, stage->normals.target.id, TIDE_WRAP_CLAMP));
    TIDE_CHECK(samples(light, stage->last.target.id, TIDE_WRAP_REPEAT) && !samples(light, stage->four.id, TIDE_WRAP_REPEAT));

    // Where each draws: a frag of two outputs into the target of two, and again into the window,
    // where its first is the pixel's; one of four into the target of four; a blit of two into one of three
    const tide_draw_command *into_two = drawn_through("Fill on Corner", 0), *into_window = drawn_through("Fill on Corner", 1);
    TIDE_REQUIRE(into_two && into_window && into_two->kind == TIDE_DRAW_SHADED);
    TIDE_CHECK(destination_of(into_two) == stage->two.id && destination_of(into_window) == 0);
    TIDE_CHECK(draw.runs[into_two->shaded.run].shader == draw.runs[into_window->shaded.run].shader);
    TIDE_CHECK(destination_of(drawn_through("Split on Vertex", 0)) == stage->four.id);
    const tide_draw_command *both = drawn_through("Both on Vertex", 0);
    TIDE_REQUIRE(both && both->kind == TIDE_DRAW_BLIT && draw.runs[both->shaded.run].shader->outputs == 2);
    TIDE_CHECK(destination_of(both) == stage->exact.id && samples(&draw.runs[both->shaded.run], stage->bright.id, TIDE_WRAP_CLAMP));
    TIDE_CHECK(destination_of(drawn_through("Light on Vertex", 0)) == 0);
    // The engine's own draw into a target of several like into any other
    uint32_t clears = 0, rects = 0, selected = 0, two = 0;
    for (uint32_t i = 0; i < draw.target_count; i++) {
        if (draw.targets[i].id == stage->two.id) two = i + 1u;
    }
    TIDE_REQUIRE(two != 0);
    for (uint32_t i = 0; i < draw.count; i++) {
        const tide_draw_command *c = &draw.commands[i];
        if (c->kind == TIDE_DRAW_TARGET) selected = c->target;
        if (selected != two) continue;
        clears += c->kind == TIDE_DRAW_CLEAR;
        rects += c->kind == TIDE_DRAW_RECT;
    }
    TIDE_CHECK(clears == 1 && rects == 1);

    // Another build of the game, in the same memory: every reference is the image it was
    const tide_render_target first = stage->two, exact = stage->exact, four = stage->four;
    const uint64_t normals = stage->normals.target.id, last = stage->last.target.id;
    tide_local_rebind_resources(&local, true);
    TIDE_CHECK(local.Stage.two.id == first.id && local.Stage.albedo.target.id == first.id);
    TIDE_CHECK(local.Stage.normals.target.id == normals && local.Stage.last.target.id == last);
    TIDE_CHECK(tide_render_target_image(local.Stage.pass.read.target.id) == 1 && local.Stage.pass.into.id == exact.id);
    TIDE_CHECK(tide_get_Prop_local(&local, entity).seen.target.id == seen);

    // ...and in other memory, as a page's next program has it: each target is made again holding
    // what it held, and a reference to one of its images names the same image of the new target
    const uint32_t size = tide_local_pack(&local, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(bytes != NULL);
    TIDE_REQUIRE(tide_local_pack(&local, bytes, size) == size);
    tide_local restored = {0};
    TIDE_REQUIRE(tide_local_unpack(&restored, bytes, size));
    free(bytes);
    tide_local_rebind_resources(&restored, false);
    const Stage *again = &restored.Stage;
    TIDE_REQUIRE(tide_render_target_managed(again->two.id) && again->two.id != first.id);
    TIDE_CHECK(holds(again->two.id, 2, C, F, 0, 0) && again->two.size.width == 64.0f);
    TIDE_CHECK(holds(again->exact.id, 3, C, F, F, 0) && again->exact.size.scale == 1.0f && again->exact.id != exact.id);
    TIDE_CHECK(holds(again->screen.id, 2, F, C, 0, 0) && again->screen.size.window == 1.0f);
    TIDE_CHECK(holds(again->half.id, 3, C, C, F, 0) && again->half.size.window == 0.5f);
    TIDE_CHECK(holds(again->four.id, 4, C, C, C, C) && again->four.id != four.id && holds(again->bright.id, 1, F, 0, 0, 0));
    TIDE_CHECK(again->albedo.target.id == again->two.id);
    TIDE_CHECK(tide_render_target_whole(again->normals.target.id) == again->two.id && tide_render_target_image(again->normals.target.id) == 1);
    TIDE_CHECK(tide_render_target_whole(again->last.target.id) == again->four.id && tide_render_target_image(again->last.target.id) == 3);
    TIDE_CHECK(tide_render_target_whole(again->byIndex.target.id) == again->exact.id && tide_render_target_image(again->byIndex.target.id) == 2);
    TIDE_CHECK(again->pass.into.id == again->exact.id && tide_render_target_whole(again->pass.read.target.id) == again->exact.id
               && tide_render_target_image(again->pass.read.target.id) == 1);
    const uint64_t seen_again = tide_get_Prop_local(&restored, entity).seen.target.id;
    TIDE_CHECK(tide_render_target_whole(seen_again) == again->exact.id && tide_render_target_image(seen_again) == 1);
    tide_render_targets_recreated();

    // A second frame draws through the same references
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    TIDE_CHECK(local.Stage.frames == 2 && run_through("Fill on Corner") != NULL && drawn_through("Both on Vertex", 0) != NULL);
    TIDE_CHECK(local.Stage.two.id == first.id && holds(local.Stage.two.id, 2, C, F, 0, 0));

    tide_local_free(&restored);
    tide_local_free(&local);
    tide_draw_free(&draw);
    tide_world_free(&world);
    tide_render_targets_free();
}
