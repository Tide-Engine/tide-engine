#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tide/gui.h"
#include "tide/time.h"

static tide_gui gui;
static tide_draw_list draw;
static tide_devices devices;

static void frame(const int custom)
{
    tide_draw_reset(&draw);
    tide_gui_begin(&gui, &devices, tide_f2(1920, 1080), NULL);
    const int panel = tide_gui_begin_area(&gui, 1, (tide_rect){10, 10, 1800, 1000});
    for (uint32_t row = 0; row < 32; row++) {
        const int horizontal = tide_gui_begin_horizontal(&gui, row + 10);
        for (uint32_t col = 0; col < 8; col++) {
#if CURRENT
            if (custom) {
                const int keyed = custom == 1 ? tide_gui_begin_key(&gui, 700, (int32_t)(row * 8 + col)) : -1;
                const tide_rect r = tide_gui_layout_reserve(&gui, tide_f2(120, 28));
                const uint32_t id = custom == 1 ? tide_gui_id(&gui, 1, 800) : row * 8 + col + 1000;
                const tide_control c = tide_gui_control(&gui, id, r);
                const int painting = custom == 1 ? tide_gui_begin_draw(&gui) : -1;
                tide_draw_rect(&gui.list, tide_f2(r.x + 60, r.y + 14), tide_f2(120, 28),
                               c.hovered ? TIDE_COLOR_BLUE : TIDE_COLOR_RED);
                tide_draw_text(&gui.list, "Play", tide_f2(r.x + 10, r.y + 6), 16, TIDE_COLOR_WHITE);
                if (painting >= 0) tide_gui_close(&gui, painting);
                if (keyed >= 0) tide_gui_close(&gui, keyed);
            } else
#else
            (void)custom;
#endif
                tide_gui_layout_button(&gui, tide_gui_id(&gui, row + 100, 800), "Play");
        }
        tide_gui_close(&gui, horizontal);
    }
    tide_gui_close(&gui, panel);
    tide_gui_end(&gui, &draw);
}

static int compare(const void *a, const void *b)
{
    const double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

int main(int argc, char **argv)
{
    const int custom = argc > 1 ? (strcmp(argv[1], "custom") == 0 ? 1 : strcmp(argv[1], "direct") == 0 ? 2 : 0) : 0;
    devices.pointer.position = tide_f2(-100, -100);
    for (int i = 0; i < 64; i++) frame(custom);
    double samples[9];
    for (int sample = 0; sample < 9; sample++) {
        const uint64_t start = tide_time_now_ns();
        for (int i = 0; i < 2000; i++) frame(custom);
        samples[sample] = (double)(tide_time_now_ns() - start) / 2000.0 / 1000.0;
    }
    qsort(samples, 9, sizeof samples[0], compare);
    const uint32_t count = tide_draw_expand(&draw, NULL, 0);
    tide_draw_command *flat = calloc(count ? count : 1u, sizeof *flat);
    if (!flat) return 1;
    tide_draw_expand(&draw, flat, count);
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < (size_t)count * sizeof *flat; i++) hash = (hash ^ ((const unsigned char *)flat)[i]) * UINT64_C(1099511628211);
    for (uint32_t i = 0; i < draw.text_used; i++) hash = (hash ^ (unsigned char)draw.text[i]) * UINT64_C(1099511628211);
    printf("%s median %.3f us/frame, best %.3f, commands %u, group %zu bytes, digest %016llx\n",
           custom == 1 ? "custom" : custom == 2 ? "direct" : "stock", samples[4], samples[0], draw.count,
           sizeof(tide_gui_group), (unsigned long long)hash);
    printf("sorted_samples_us:");
    for (int i = 0; i < 9; i++) printf(" %.3f", samples[i]);
    putchar('\n');
    free(flat);
    tide_draw_free(&gui.list);
    tide_draw_free(&draw);
    return 0;
}
