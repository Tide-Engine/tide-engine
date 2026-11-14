#include <stdio.h>
#include <string.h>
#include "game.h"
#include "tide/platform.h"

static int failures;
static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_devices devices;

static int frame(void *context, float seconds)
{
    (void)context;
    (void)seconds;
    const float distances[] = {10, 100, 1000, 10000};
    const float separations[] = {0.01f, 0.1f, 1};
    const float fars[] = {1000, 10000, 100000};
    const tide_float2 center = {32, 32};
    printf("renderer: %s\n", tide_platform_renderer());
    for (int mode = 0; mode < 3; mode++) {
        local.CameraProbe.mode = mode;
        for (int row = 0; row < 7; row++) {
            local.CameraProbe.standard = row == 6;
            local.CameraProbe.near = row < 3 ? 0.1f : row < 6 ? 0.01f : 0.3f;
            local.CameraProbe.far = row == 6 ? 0 : fars[row % 3];
            for (int di = 0; di < 4; di++) for (int si = 0; si < 3; si++) {
                if (row != 6 && distances[di] + separations[si] + distances[di] * 15e-6f >= fars[row % 3]) continue;
                local.CameraProbe.distance = distances[di];
                local.CameraProbe.separation = separations[si];
                int wrong = 0, blank = 0;
                for (int step = 0; step < 16; step++) {
                    local.CameraProbe.step = step;
                    tide_draw_reset(&draw);
                    tide_gui_begin(&gui, &devices, tide_f2(64, 64), tide_platform_measure_text);
                    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
                    tide_gui_end(&gui, &draw);
                    uint32_t pixel;
                    tide_platform_read_pixels_as(&draw, 64, 64, 1, &center, 1, &pixel);
                    wrong += pixel == 0xff0000ffu;
                    blank += pixel != 0xff0000ffu && pixel != 0x00ff00ffu;
                }
                failures += wrong + blank;
                printf("CAMERA,%d,%d,%d,%d,%d,%d\n", mode, row, di, si, wrong, blank);
            }
        }
    }
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
    puts("camera depth regression complete");
    return failures;
}

int main(int argc, char **argv)
{
    bool bounded = false;
    for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "--frames=1") || !strcmp(argv[i], "--frames")) bounded = true;
    if (!bounded) { fputs("use --frames=1 for the bounded hidden regression\n", stderr); return 2; }
    tide_world_init(&world, 1.0f / 60.0f);
    tide_local_init(&local);
    tide_platform_open(&(tide_window_desc){.title = "depth regression", .width = 64, .height = 64, .hidden = true});
    tide_platform_run(frame, NULL);
    return failures;
}
