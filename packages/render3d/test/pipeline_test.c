#include "game.h"
#include "tide_test.h"
extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

TIDE_TEST(render3d_pass_order_and_retained_resource_lifetime)
{
    static tide_world world;
    static tide_local local;
    static tide_draw_list draw;
    static tide_gui gui;
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    gui.width = 128, gui.height = 64;
    const uint64_t hash = tide_world_hash(&world);
    uint64_t hdr = 0, shadow = 0;
    for (int frame = 0; frame < 4; frame++) {
        tide_draw_reset(&draw);
        tide_draw_screen_size(&draw, (tide_float2){128, 64});
        local.Probe.disableRenderer = frame == 2;
        tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
        TIDE_CHECK(tide_world_hash(&world) == hash);
        if (frame == 2) {
            TIDE_CHECK(!local.Render3D_Renderer.ready && draw.run_count == 0);
            TIDE_CHECK(!tide_render_target_live(hdr) && !tide_render_target_live(shadow));
            continue;
        }
        TIDE_REQUIRE(local.Render3D_Renderer.ready);
        if (frame == 1) {
            TIDE_CHECK(local.Render3D_Renderer.hdr.id == hdr);
            TIDE_CHECK(local.Render3D_Renderer.shadowMap.id == shadow);
        }
        if (frame == 3) TIDE_CHECK(local.Render3D_Renderer.hdr.id != hdr);
        hdr = local.Render3D_Renderer.hdr.id;
        shadow = local.Render3D_Renderer.shadowMap.id;
        TIDE_CHECK(tide_render_target_live(hdr) && tide_render_target_live(shadow));
        uint64_t destination = 0;
        int phase = 0, casters = 0, surfaces = 0, presentations = 0;
        for (uint32_t i = 0; i < draw.count; i++) {
            const tide_draw_command *command = &draw.commands[i];
            if (command->kind == TIDE_DRAW_TARGET)
                destination = command->target ? draw.targets[command->target - 1].id : 0;
            if (command->kind == TIDE_DRAW_MESH_3D || command->kind == TIDE_DRAW_SHADED) {
                if (destination == shadow) {
                    TIDE_CHECK(phase == 0);
                    casters++;
                } else {
                    TIDE_CHECK(phase <= 1 && destination == hdr);
                    phase = 1;
                    surfaces++;
                }
            }
            if (command->kind == TIDE_DRAW_BLIT) {
                TIDE_CHECK(phase == 1 && destination == 0);
                phase = 2;
                presentations++;
            }
        }
        TIDE_CHECK(casters > 0 && surfaces > 0 && presentations == 1 && phase == 2);
    }
    tide_draw_reset(&draw);
    local.Probe.disableRenderer = true;
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    TIDE_CHECK(!tide_render_target_live(hdr) && !tide_render_target_live(shadow));
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_render_targets_free();
}
