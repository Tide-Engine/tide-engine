#include "game.h"
#include "tide_test.h"

TIDE_TEST(draw_frustum_generated_metadata_settings_and_operand_effects)
{
    tide_local local = {0};
    tide_local_init(&local);
    local.Probe.target = tide_render_target_make(100, 100);
    tide_draw_list draw = {0};
    tide_gui gui = {0};
    const uint32_t expected[] = {1, 1, 2, 2};
    const int effects[] = {1, 2, 3, 3};
    for (int mode = 0; mode < 4; mode++) {
        tide_draw_reset(&draw);
        tide_draw_screen_size(&draw, tide_f2(400, 400));
        local.Probe.mode = mode;
        local.Probe.evaluated = 0;
        tide_frame(NULL, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
        uint32_t kept = 0;
        for (uint32_t i = 0; i < draw.run_count; i++) {
            const tide_draw_run *r = &draw.runs[i];
            kept += r->count;
            TIDE_CHECK((r->shader->bounds_position == 0) == (mode == 2));
        }
        TIDE_CHECK(kept == expected[mode]);
        TIDE_CHECK(local.Probe.evaluated == effects[mode]);
        TIDE_CHECK(draw.saved_count == 0);
        TIDE_CHECK(draw.place.bounds.set == 0 && draw.place.no_frustum_culling == 0);
    }
    tide_draw_free(&draw);
    tide_render_target_dispose(local.Probe.target);
    tide_local_free(&local);
    tide_render_targets_free();
}
