#include <stdio.h>
#include "game.h"
#include "tide/platform.h"
#include "window.h"
static tide_world world;
static tide_local local;
static tide_draw_list list;
static tide_gui gui;
static tide_devices devices;
static int failures;
static int frame(void *user, float seconds)
{
    (void)user; (void)seconds;
    tide_draw_reset(&list);
    tide_gui_begin(&gui, &devices, tide_f2(32, 32), tide_platform_measure_text);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &list, &gui);
    tide_gui_end(&gui, &list);
    uint32_t pixel;
    tide_platform_read_pixels_as(&list, 32, 32, 1, &(tide_float2){0,0}, 1, &pixel);
    if (local.CopyResult.done) {
        int32_t size[3]; tide_grid_size(local.CopyResult.pixels, size);
        const tide_grid_shape shape = {sizeof(tide_color), 2, {5,5,0}};
        bool good = !local.CopyResult.failed && size[0] == 4 && size[1] == 3;
        for (int32_t y = 0; y < 3; y++) for (int32_t x = 0; x < 4; x++) {
            const tide_color *c = tide_grid_peek(local.CopyResult.pixels, x, y, 0, &shape);
            const bool green = x < 2 && y < 2;
            good = good && c && c->r == (green ? 0 : 1) && c->g == (green ? 1 : 0) && c->b == 0 && c->a == 1;
        }
        const tide_color *hdr = tide_grid_peek(local.CopyResult.hdr, 0, 0, 0, &shape);
        good = good && hdr && hdr->r == 2 && hdr->g == 0.5f && hdr->b == 0.25f && hdr->a == 0.5f;
        good = good && local.CopyState.frame == 5;
        printf("%s: await target.Read owns the exact drawn 4x3 colors at frame %d\n", good ? "ok" : "FAIL", local.CopyState.frame);
        printf("renderer: %s\n", tide_platform_renderer());
        failures = good ? 0 : 1; return failures;
    }
    if (local.CopyState.frame > 120) { puts("FAIL: target.Read did not complete"); failures = 1; return 1; }
    return TIDE_KEEP_RUNNING;
}
int main(void)
{
    tide_world_init(&world, 1.0f / 60.0f); tide_local_init(&local);
    tide_platform_open(&(tide_window_desc){.title="Readback",.width=32,.height=32,.hidden=true});
    tide_platform_run(frame, NULL);
    return failures;
}
