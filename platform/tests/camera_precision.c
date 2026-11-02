#include <stdio.h>
#include <stdlib.h>

#include "game.h"
#include "tide/platform.h"

enum { SIZE = 256, SIDE = 9, COUNT = SIDE * SIDE };
static tide_world world;
static tide_local local;
static tide_draw_list draw, copied;
static tide_gui gui;
static tide_devices devices;
static tide_float2 points[COUNT];

static void render(const int mode, const float yaw, const bool distant,
                   const bool append, uint32_t pixels[COUNT])
{
    local.Probe.origin = distant ? tide_f3(3000, 200, 3000) : tide_f3(0, 0, 0);
    local.Probe.yaw = yaw;
    local.Probe.mode = mode;
    tide_draw_reset(&draw);
    tide_gui_begin(&gui, &devices, tide_f2(SIZE, SIZE), tide_platform_measure_text);
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    tide_gui_end(&gui, &draw);
    if (append) {
        tide_draw_reset(&copied);
        tide_draw_append(&copied, &draw);
    }
    tide_platform_read_pixels_as(append ? &copied : &draw, SIZE, SIZE, 1, points, COUNT, pixels);
}

static int compare(const uint32_t reference[COUNT], const uint32_t actual[COUNT],
                   const char *what, const int mode, const float yaw)
{
    int bad = 0, largest = 0;
    for (int i = 0; i < COUNT; i++) {
        bool same = true;
        for (int c = 0; c < 3; c++) {
            const int a = (int)(actual[i] >> (24 - 8 * c) & 255u);
            const int b = (int)(reference[i] >> (24 - 8 * c) & 255u);
            const int difference = abs(a - b);
            if (difference > largest) largest = difference;
            if (difference > 3) same = false;
        }
        if (!same) bad++;
    }
    printf("%s: %s mode %d yaw %.3f: %d/%d pixels differ, max channel error %d\n",
           bad ? "FAIL" : "ok", what, mode, (double)yaw, bad, COUNT, largest);
    return bad != 0;
}

static int frame(void *context, const float seconds)
{
    (void)context;
    (void)seconds;
    printf("renderer: %s\n", tide_platform_renderer());
    // Interior sample grid, away from the sheet silhouette and the exact
    // intersection at x=0. Each depth byte represents about 0.12 m at 140 m;
    // three bytes allow backend rounding but reject metre-scale cancellation.
    for (int y = 0; y < SIDE; y++)
        for (int x = 0; x < SIDE; x++)
            points[y * SIDE + x] = tide_f2(99.5f + (float)x * 7, 99.5f + (float)y * 7);
    int failures = 0;
    const float yaws[] = {-0.13f, -0.07f, 0.0f, 0.07f, 0.13f};
    for (unsigned angle = 0; angle < sizeof yaws / sizeof yaws[0]; angle++) {
        uint32_t reference[COUNT], actual[COUNT], authored[COUNT];
        render(2, yaws[angle], false, false, authored);
        for (int mode = 0; mode < 5; mode++) {
            render(mode, yaws[angle], false, false, reference);
            render(mode, yaws[angle], true, false, actual);
            failures += compare(reference, actual, "translated vs origin", mode, yaws[angle]);
            if (mode < 3) {
                failures += compare(authored, actual, "authored relative clip vs distant", mode, yaws[angle]);
                for (int i = 0; i < COUNT; i++) {
                    const unsigned green = actual[i] >> 16 & 255u;
                    if (green < 126 || green > 129) {
                        printf("FAIL: world-space material/instance at mode %d yaw %.3f pixel %d: %08X\n",
                               mode, (double)yaws[angle], i, (unsigned)actual[i]);
                        failures++;
                        break;
                    }
                }
            }
            if (mode >= 3) {
                for (int i = 0; i < COUNT; i++) {
                    if (actual[i] != 0xff0000ffu && actual[i] != 0x0000ffffu) {
                        printf("FAIL: builtin mesh missing at mode %d pixel %d: %08X\n",
                               mode, i, (unsigned)actual[i]);
                        failures++;
                        break;
                    }
                }
            }
            render(mode, yaws[angle], true, true, reference);
            failures += compare(actual, reference, "appended camera", mode, yaws[angle]);
        }
    }
    tide_draw_free(&copied);
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
    return failures;
}

int main(void)
{
    tide_world_init(&world, 1.0f / 60.0f);
    tide_local_init(&local);
    tide_platform_open(&(tide_window_desc){.title = "camera precision", .width = SIZE, .height = SIZE, .hidden = true});
    tide_platform_run(frame, NULL);
}
