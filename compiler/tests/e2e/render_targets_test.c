#include "game.h"
#include "tide_test.h"

#include <stdlib.h>

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

void WindowMark(tide_draw_list *commands)
{
    tide_draw_clear(commands, TIDE_COLOR_GREEN);
}

TIDE_TEST(render_targets_phase_order_and_explicit_sources)
{
    tide_world_init(&world, 1.0f);
    tide_local_init(&local);
    local.Buffers.scene = (tide_render_target){.id = 11, .size = {.width = 16, .height = 12}};
    local.Buffers.scratch = (tide_render_target){.id = 12, .size = {.width = 8, .height = 6}};
    static const uint8_t pixels[4] = {255, 127, 0, 255};
    local.Buffers.image = (tide_texture_ref){.image = {.pixels = pixels, .width = 1, .height = 1, .version = 1}};
    tide_devices devices = {0};
    tide_gui_begin(&gui, &devices, tide_f2(200, 100), NULL);
    tide_frame(&world, NULL, 1.0f, NULL, &local, &draw, &gui);
    TIDE_CHECK(local.Buffers.targetThenWindow.x == 200 && local.Buffers.targetThenWindow.y == -25);
    TIDE_CHECK(local.Buffers.windowThenTarget.x == 158 && local.Buffers.windowThenTarget.y == -69);
    TIDE_CHECK(local.Buffers.projected3.x == 8 && local.Buffers.projected3.y == 6);
    TIDE_CHECK(local.Buffers.projected3.z > 0 && local.Buffers.projected3.z < 1);

    uint32_t destination = 0, objects = 0, blits = 0, cubes = 0, marked = 0;
    bool prepared = false;
    for (uint32_t i = 0; i < draw.count; i++) {
        const tide_draw_command *c = &draw.commands[i];
        if (c->kind == TIDE_DRAW_TARGET) {
            destination = c->target ? (uint32_t)draw.targets[c->target - 1u].id : 0;
        } else if (c->kind == TIDE_DRAW_CLEAR && destination == 11) {
            prepared = true;
        } else if (c->kind == TIDE_DRAW_CLEAR && c->color.g == 1 && c->color.r == 0) {
            TIDE_CHECK(destination == 0);
            marked++;
        } else if (c->kind == TIDE_DRAW_MESH_3D) {
            TIDE_CHECK(destination == 11);
            cubes++;
        } else if (c->kind == TIDE_DRAW_RECT && destination == 11) {
            TIDE_CHECK(prepared && blits == 0);
            objects++;
        } else if (c->kind == TIDE_DRAW_BLIT) {
            TIDE_CHECK(objects == 2);
            const tide_draw_run *run = &draw.runs[c->shaded.run];
            TIDE_REQUIRE(run->shader->texture_count == 1 && run->textures[0] != 0);
            const tide_draw_texture *source = &draw.textures[run->textures[0] - 1u];
            if (blits == 0) TIDE_CHECK(destination == 12 && source->space == TIDE_PIXELS_TARGET && source->id == 11);
            if (blits == 1) TIDE_CHECK(destination == 11 && source->space == TIDE_PIXELS_MEMORY);
            if (blits == 2) TIDE_CHECK(destination == 12 && source->space == TIDE_PIXELS_GRID);
            if (blits == 3) TIDE_CHECK(destination == 0 && source->space == TIDE_PIXELS_TARGET && source->id == 12);
            blits++;
        }
    }
    TIDE_CHECK(blits == 4 && objects == 2 && destination == 0);
    TIDE_CHECK(cubes == 1 && marked == 1);
    TIDE_CHECK(draw.commands[draw.count - 1u].kind == TIDE_DRAW_CLEAR);
    tide_local_free(&local);
    tide_draw_free(&draw);
    tide_world_free(&world);
}

TIDE_TEST(render_targets_native_rebind_keeps_shared_targets_and_clears_cpu_references)
{
    tide_world_init(&world, 1.0f);
    tide_local_init(&local);
    static const uint8_t pixels[4] = {255, 127, 0, 255};
    const tide_texture_ref cpu = {.image = {.pixels = pixels, .width = 1, .height = 1, .version = 1}};
    const tide_render_target target = tide_render_target_make(16, 12);
    local.Buffers.scene = target;
    local.Buffers.scratch = tide_render_target_make_scaled(8, 6, 2); // Two pixels of its image a unit
    local.Buffers.image = cpu;
    local.Buffers.nested = (Resources){.target = target, .image = cpu, .retained = 77};
    local.Buffers.windowAlias = (tide_render_target){.id = TIDE_RENDER_WINDOW};
    local.Buffers.borrowed = (tide_render_target){.id = 99, .size = {.width = 1, .height = 1}};
    local.Buffers.beginTask = 1;
    tide_frame(&world, NULL, 1.0f, NULL, &local, &draw, &gui);
    TIDE_REQUIRE(local.arch0_Graphic.count == 1 && local.tide_tasks_function_Keep.count == 1);
    const tide_entity graphic = *(const tide_entity *)tide_table_get(&local.arch0_Graphic, &tide_columns_arch0_Graphic, 0, 0);
    tide_entity object = {0};
    for (uint32_t i = 0; i < world.entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&world.entities, i);
        if (tide_has_Object(&world, e)) { object = e; break; }
    }
    TIDE_REQUIRE(object.generation != 0);
    tide_set_Object_local(&local, object, (Object_local){.resources = local.Buffers.nested});

    // The generated queue's first spawn is Graphic: reset references without
    // dropping its pending structural change or its ordinary data.
    tide_command *queued = tide_queue_push(&local.commands, TIDE_COMMAND_SIZE(spawn0));
    queued->kind = 0;
    queued->id = 0;
    queued->data.spawn0.values.Graphic.resources = local.Buffers.nested;
    tide_local_rebind_resources(&local, true);
    TIDE_CHECK(local.Buffers.scene.id == target.id && local.Buffers.nested.target.id == target.id);
    TIDE_CHECK(local.Buffers.nested.image.image.pixels == NULL && local.Buffers.nested.retained == 77);
    TIDE_CHECK(local.Buffers.windowAlias.id == TIDE_RENDER_WINDOW && local.Buffers.borrowed.id == 0);
    TIDE_CHECK(tide_get_Graphic(&local, graphic).resources.target.id == target.id);
    TIDE_CHECK(tide_get_Graphic(&local, graphic).resources.image.image.pixels == NULL);
    TIDE_CHECK(tide_get_Object_local(&local, object).resources.target.id == target.id);
    TIDE_CHECK(tide_get_Object_local(&local, object).resources.image.image.pixels == NULL);
    TIDE_CHECK(queued->data.spawn0.values.Graphic.resources.target.id == target.id);
    TIDE_CHECK(queued->data.spawn0.values.Graphic.resources.image.image.pixels == NULL);
    TIDE_CHECK(queued->data.spawn0.values.Graphic.resources.retained == 77);
    tide_queue_clear(&local.commands);

    // The retained async frame also loses CPU pointers while keeping its target.
    local.Buffers.image = cpu;
    tide_frame(&world, NULL, 1.0f, NULL, &local, &draw, &gui);
    tide_frame(&world, NULL, 1.0f, NULL, &local, &draw, &gui);
    TIDE_CHECK(local.Buffers.resumed.target.id == target.id);
    TIDE_CHECK(local.Buffers.resumed.image.image.pixels == NULL && local.Buffers.resumed.retained == 77);

    // A page rebuild restores one new target for every saved alias, using the
    // resolved size rather than the descriptor's original construction size.
    local.Buffers.resumed.image = tide_render_target_color(target);
    tide_render_target_resize(target, 40, 30);
    // ...and what a size doesn't say: a target's pixels per unit, and that it follows the window.
    local.Buffers.borrowed = tide_render_target_screen(0.5f);
    tide_local_rebind_resources(&local, true);
    TIDE_CHECK(local.Buffers.scene.size.width == 40 && local.Buffers.resumed.image.target.size.height == 30);
    TIDE_CHECK(local.Buffers.scratch.size.scale == 2 && local.Buffers.borrowed.size.window == 0.5f);
    const uint32_t size = tide_local_pack(&local, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(bytes != NULL);
    TIDE_REQUIRE(tide_local_pack(&local, bytes, size) == size);
    tide_local restored = {0};
    TIDE_REQUIRE(tide_local_unpack(&restored, bytes, size));
    free(bytes);
    tide_local_rebind_resources(&restored, false);
    const tide_render_target recreated = restored.Buffers.scene;
    TIDE_REQUIRE(tide_render_target_managed(recreated.id) && recreated.id != target.id);
    TIDE_CHECK(recreated.size.width == 40 && recreated.size.height == 30 && recreated.size.scale == 0);
    const tide_render_target dense = tide_render_target_resolve(restored.Buffers.scratch);
    TIDE_CHECK(tide_render_target_live(dense.id) && dense.id != local.Buffers.scratch.id);
    TIDE_CHECK(dense.size.width == 8 && dense.size.height == 6 && dense.size.scale == 2);
    const tide_render_target following = tide_render_target_resolve(restored.Buffers.borrowed);
    TIDE_CHECK(tide_render_target_live(following.id) && following.size.window == 0.5f);
    TIDE_CHECK(restored.Buffers.nested.target.id == recreated.id && restored.Buffers.resumed.target.id == recreated.id);
    TIDE_CHECK(restored.Buffers.resumed.image.target.id == recreated.id);
    TIDE_CHECK(tide_get_Graphic(&restored, graphic).resources.target.id == recreated.id);
    TIDE_CHECK(tide_get_Object_local(&restored, object).resources.target.id == recreated.id);
    TIDE_CHECK(restored.Buffers.nested.retained == 77 && restored.Buffers.beginTask == 2);
    TIDE_CHECK(restored.Buffers.windowAlias.id == TIDE_RENDER_WINDOW);
    tide_local_free(&restored);
    tide_local_free(&local);
    tide_draw_free(&draw);
    tide_world_free(&world);
    tide_render_targets_free();
}
