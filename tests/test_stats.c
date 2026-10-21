// The stats' drawer (tide/stats.h): what it shows closed and open, what opens
// and closes it, and whose a press on it is. Its numbers are made up, and so
// are the devices: each frame here goes in a host's order, the poll, the
// drawer's input, the game's sample, the GUI and the views, then the drawer.

#include <string.h>

#include "tide/gui.h"
#include "tide/stats.h"
#include "tide_test.h"
#include "draw_test.h"

#define MS UINT64_C(1000000)

static tide_devices devices;
static tide_stats stats;
static tide_gui gui;
static tide_draw_list list;
static tide_profile profile;
static tide_stats_values values;
static tide_float2 screen;
static tide_devices sampled;

// Every character 8 pixels wide (a UTF-8 character, not a byte: the arrows are three).
static tide_float2 font(const char *text, const float size, const tide_target_size *target, const unsigned char *weight)
{
    (void)target;
    (void)weight;
    float width = 0.0f;
    for (; *text; text++) {
        if (((unsigned char)*text & 0xC0u) != 0x80u) width += 8.0f;
    }
    return tide_f2(width, size);
}

static void start(const bool open)
{
    memset(&devices, 0, sizeof devices);
    memset(&gui, 0, sizeof gui);
    tide_stats_free(&stats);
    stats = (tide_stats){.open = open};
    screen = tide_f2(960.0f, 540.0f);
    tide_profile_free(&profile);
    tide_profile_count(&profile.systems, 4);
    tide_profile_add(&profile.systems, 0, "Fall", 0, 310000);
    tide_profile_add(&profile.systems, 1, "Combat.Hit", 0, 1500000);
    tide_profile_add(&profile.systems, 2, "Land", 0, 0);
    tide_profile_add(&profile.systems, 3, "Move", 0, 200000);
    values = (tide_stats_values){
        .fps = 59.6,
        .work_ms = 2.14,
        .threads = 12,
        .memory = 48u * 1024u * 1024u,
        .gpu_memory = 12u * 1024u * 1024u,
        .renderer = "OpenGL 3.3",
        .draw_calls = 7,
        .vertices = UINT64_C(123456),
        .match = true,
        .ping_ms = 34,
        .up = 2048.0,
        .down = 512.0,
        .entities = 56,
        .profile = &profile,
    };
}

static void draw_only(void)
{
    tide_draw_reset(&list);
    tide_stats_draw(&stats, &values, &list, screen, font);
    tide_test_flatten(&list);
}

// One made-up tick of the same durations, half a second into the frame. Prime
// before it on opening, as the host does, so a closed drawer's samples stay out.
static void draw(void)
{
    const tide_profile *p = values.match ? values.profile : NULL;
    if (stats.open && p && stats.profile != p) tide_stats_collect(&stats, p, 0.0);
    if (stats.open && p) {
        for (uint32_t i = 0; i < profile.systems.count; i++) {
            const tide_profile_time *t = &profile.systems.items[i];
            tide_profile_add(&profile.systems, i, t->name, 0, (uint64_t)(t->ms * (double)MS));
        }
    }
    tide_stats_collect(&stats, p, 0.5);
    draw_only();
}

static bool line_is(const uint32_t i, const char *label, const char *value)
{
    return i < stats.line_count && strcmp(stats.lines[i].label, label) == 0 && strcmp(stats.lines[i].value, value) == 0;
}

static const tide_draw_command *text_command(const char *text)
{
    for (uint32_t i = 0; i < list.count; i++) {
        const tide_draw_command *c = &list.commands[i];
        if (c->kind == TIDE_DRAW_TEXT && strcmp(list.text + c->text, text) == 0) return c;
    }
    return NULL;
}

static tide_rect panel(void)
{
    for (uint32_t i = 0; i < list.count; i++) {
        const tide_draw_command *c = &list.commands[i];
        if (c->kind == TIDE_DRAW_RECT) return (tide_rect){c->a.x - c->b.x * 0.5f, c->a.y - c->b.y * 0.5f, c->b.x, c->b.y};
    }
    return (tide_rect){0};
}

// ---------------------------------------------------------------------------
// What it shows

TIDE_TEST(stats_closed_are_one_line_in_the_corner)
{
    start(false);
    tide_draw_reset(&list);
    tide_draw_set_clip(&list, (tide_rect){0, 0, 10, 10});
    tide_draw_set_tint(&list, TIDE_COLOR_RED);
    tide_stats_draw(&stats, &values, &list, screen, font);
    tide_test_flatten(&list);
    TIDE_REQUIRE(stats.line_count == 1 && stats.shown);
    TIDE_CHECK(line_is(0, "60 fps", ""));
    TIDE_CHECK(!list.place.tinted && text_command("60 fps") && !text_command("cpu"));

    const tide_rect box = panel();
    TIDE_CHECK(box.x + box.width == 952.0f && box.y + box.height == 532.0f && box.height == 32.0f);
    TIDE_CHECK(box.width == 48.0f + 16.0f + 16.0f);
    const tide_draw_command *line = text_command("60 fps");
    TIDE_CHECK(line->a.x + 48.0f == 944.0f && line->a.y == 508.0f && line->b.x == 16.0f);
    TIDE_CHECK(stats.rect.x == box.x && stats.rect.y == box.y);
    TIDE_CHECK(stats.rect.x + stats.rect.width == 960.0f && stats.rect.y + stats.rect.height == 540.0f);

    values.remote = true;
    draw();
    TIDE_CHECK(stats.line_count == 1 && line_is(0, "60 fps   ping 34 ms", ""));
    values.match = false;
    draw();
    TIDE_CHECK(stats.line_count == 1 && line_is(0, "60 fps", ""));
    tide_draw_free(&list);
}

TIDE_TEST(stats_open_list_the_rest_above_their_line)
{
    start(false);
    draw();
    const tide_float2 closed = text_command("60 fps")->a;
    stats.open = true;
    draw();
    const char *labels[] = {"60 fps", "cpu", "draw calls", "vertices", "ping", "network", "entities",
                            "threads", "memory", "vram", "renderer", "Combat.Hit", "Fall", "Move"};
    const char *values_[] = {"", "2.1 ms", "7", "123456", "34 ms", "2.0  0.5 KB/s", "56",
                             "12", "48 MB", "12 MB", "OpenGL 3.3", "1.50 ms", "0.31 ms", "0.20 ms"};
    TIDE_REQUIRE(stats.line_count == 14);
    for (uint32_t i = 0; i < 14; i++) TIDE_CHECK(line_is(i, labels[i], values_[i]) && text_command(labels[i]));
    for (uint32_t i = 1; i < 14; i++) TIDE_CHECK(i == 5 || text_command(values_[i]));
    for (uint32_t i = 1; i < 14; i++) TIDE_CHECK(stats.lines[i].gap == (i == 4 || i == 7 || i == 11) && !stats.lines[i].indent);
    TIDE_CHECK(stats.lines[5].arrows && text_command("2.0") && text_command("0.5 KB/s"));
    const tide_draw_command *up = text_command("2.0"), *down = text_command("0.5 KB/s");
    TIDE_CHECK(down->a.x + 8.0f * 8.0f == 944.0f && up->a.x == down->a.x - 11.0f - 12.0f - 3.0f * 8.0f);
    int shafts = 0;
    for (uint32_t i = 0; i < list.count; i++) {
        const tide_draw_command *c = &list.commands[i];
        if (c->kind != TIDE_DRAW_SHAPE || c->a.y != up->a.y + 8.0f) continue; // A shaft's middle, on the line
        shafts += c->a.x == up->a.x - 11.0f + 4.0f || c->a.x == down->a.x - 11.0f + 4.0f;
    }
    TIDE_CHECK(shafts == 2);

    const tide_draw_command *line = text_command("60 fps");
    TIDE_CHECK(line->a.x == closed.x && line->a.y == closed.y);
    const tide_draw_command *first = text_command("cpu");
    const tide_draw_command *first_value = text_command("2.1 ms");
    const tide_draw_command *ping = text_command("ping");
    const tide_draw_command *last = text_command("Move");
    const tide_draw_command *last_value = text_command("0.20 ms");
    TIDE_CHECK(first->a.y == closed.y - 13.0f * 20.0f - 3.0f * 8.0f && last->a.y == closed.y - 20.0f);
    TIDE_CHECK(ping->a.y == first->a.y + 3.0f * 20.0f + 8.0f);
    TIDE_CHECK(first_value->a.y == first->a.y && last_value->a.y == last->a.y);
    TIDE_CHECK(first->a.x == last->a.x && first->a.x == 718.0f);
    TIDE_CHECK(first_value->a.x + 6.0f * 8.0f == closed.x + 48.0f && last_value->a.x + 7.0f * 8.0f == closed.x + 48.0f);
    const tide_rect box = panel();
    TIDE_CHECK(box.x + box.width == 952.0f && box.y + box.height == 532.0f && box.height == 14.0f * 20.0f + 3.0f * 8.0f + 12.0f);
    TIDE_CHECK(box.width == 10.0f * 8.0f + 24.0f + 122.0f + 16.0f && stats.rect.y == box.y);

    values.work_ms = -1.0;
    values.memory = 0;
    values.gpu_memory = 0;
    values.renderer = NULL;
    values.delay_ticks = 3;
    draw();
    TIDE_CHECK(stats.line_count == 10 && line_is(1, "draw calls", "7") && line_is(3, "ping (delay 3 ticks)", "34 ms"));
    TIDE_CHECK(line_is(6, "threads", "12") && !stats.lines[1].gap && stats.lines[3].gap && stats.lines[6].gap);
    values.remote = true;
    draw();
    TIDE_CHECK(line_is(0, "60 fps   ping 34 ms", "") && line_is(3, "delay", "3 ticks") && stats.line_count == 10);
    values = (tide_stats_values){.fps = 144.2, .work_ms = 0.5, .threads = 1, .draw_calls = 2, .profile = &profile};
    draw();
    TIDE_CHECK(stats.line_count == 5 && line_is(0, "144 fps", "") && line_is(1, "cpu", "0.5 ms") && line_is(2, "draw calls", "2")
               && line_is(3, "vertices", "0") && line_is(4, "threads", "1") && stats.lines[4].gap);
    values.vertices = UINT64_C(4294967296);
    draw();
    TIDE_CHECK(line_is(3, "vertices", "4294967296"));
    tide_draw_free(&list);
}

// Inside another program's frame (tide/embed.h) its draws are that program's: no draw call, vertex, render or GPU
// lines, the frame's work and the match's lines still there
TIDE_TEST(stats_drawn_elsewhere_leave_out_the_other_programs_lines)
{
    start(true);
    values.elsewhere = true;
    values.render_known = values.gpu_known = true;
    values.render_ms = 0.25;
    values.gpu_ms = 1.5;
    draw();
    TIDE_CHECK(line_is(1, "cpu", "2.1 ms") && line_is(2, "ping", "34 ms"));
    for (uint32_t i = 0; i < stats.line_count; i++) {
        TIDE_CHECK(strcmp(stats.lines[i].label, "draw calls") != 0 && strcmp(stats.lines[i].label, "vertices") != 0
                   && strcmp(stats.lines[i].label, "render") != 0 && strncmp(stats.lines[i].label, "gpu", 3) != 0);
    }
    TIDE_CHECK(!text_command("vertices") && !text_command("draw calls"));
    tide_draw_free(&list);
}

TIDE_TEST(stats_panel_keeps_its_width_while_open)
{
    start(true);
    draw();
    const float width = panel().width;
    TIDE_CHECK(width == 242.0f);
    tide_profile_add(&profile.systems, 1, "Combat.Projectiles.Resolve", 0, 9 * MS);
    draw();
    TIDE_CHECK(line_is(11, "Combat.Projectiles.Resolve", "9.00 ms") && panel().width == 26.0f * 8.0f + 24.0f + 122.0f + 16.0f);
    const float wider = panel().width;
    tide_profile_add(&profile.systems, 1, "Combat.Hit", 0, 9 * MS);
    draw();
    TIDE_CHECK(line_is(11, "Combat.Hit", "9.00 ms") && panel().width == wider);
    TIDE_CHECK(text_command("cpu")->a.x == 952.0f - wider + 8.0f);
    tide_touches_poll(&devices.touchscreen);
    tide_pointer_poll(&devices, false);
    tide_stats_input(&stats, &devices, screen, true);
    TIDE_REQUIRE(!stats.open);
    draw();
    TIDE_CHECK(panel().width == 48.0f + 16.0f + 16.0f);
    tide_stats_input(&stats, &devices, screen, false);
    tide_stats_input(&stats, &devices, screen, true);
    TIDE_REQUIRE(stats.open);
    draw();
    TIDE_CHECK(panel().width == width);
    tide_draw_free(&list);
}

TIDE_TEST(stats_fit_a_small_screen)
{
    start(true);
    screen = tide_f2(360.0f, 202.0f);
    draw();
    TIDE_REQUIRE(stats.line_count == 7);
    TIDE_CHECK(line_is(0, "60 fps", "") && line_is(1, "cpu", "2.1 ms") && line_is(6, "entities", "56"));
    tide_rect box = panel();
    TIDE_CHECK(box.y >= 8.0f && box.y + box.height == 194.0f && box.x >= 8.0f);
    TIDE_CHECK(box.height == 7.0f * 20.0f + 8.0f + 12.0f && !text_command("memory"));

    start(true);
    tide_profile_add(&profile.systems, 1, "Combat.Projectiles.ResolveEveryCollisionThereIs", 0, 9 * MS);
    screen = tide_f2(232.0f, 540.0f);
    draw();
    TIDE_REQUIRE(stats.line_count == 14);
    box = panel();
    TIDE_CHECK(box.x == 14.0f && box.width == 210.0f);
    const char *cut = stats.lines[11].label;
    TIDE_CHECK(strcmp(cut, "Combat") == 0 && line_is(11, cut, "9.00 ms"));
    TIDE_CHECK(text_command(cut) && text_command(cut)->a.x == 22.0f && text_command("9.00 ms")->a.x == 216.0f - 56.0f);

    start(true);
    screen = tide_f2(960.0f, 50.0f);
    draw();
    TIDE_CHECK(stats.line_count == 1 && line_is(0, "60 fps", ""));
    tide_draw_free(&list);
}

TIDE_TEST(stats_render_and_gpu_times_keep_the_three_system_rows)
{
    start(true);
    values.render_ms = 0.32;
    values.render_known = true;
    values.gpu_ms = 1.25;
    values.gpu_known = true;
    values.delay_ticks = 2;
    draw();
    TIDE_REQUIRE(stats.line_count == 15);
    TIDE_CHECK(line_is(1, "cpu", "2.1 ms") && line_is(2, "render", "0.32 ms") && line_is(3, "gpu (7 draw calls)", "1.25 ms"));
    TIDE_CHECK(line_is(4, "vertices", "123456"));
    TIDE_CHECK(stats.lines[2].indent && !stats.lines[1].indent && !stats.lines[3].indent && stats.lines[5].gap);
    TIDE_CHECK(text_command("render")->a.x == text_command("cpu")->a.x + 12.0f
               && text_command("gpu (7 draw calls)")->a.x == text_command("cpu")->a.x);
    TIDE_CHECK(line_is(5, "ping (delay 2 ticks)", "34 ms") && !text_command("draw calls"));
    TIDE_CHECK(line_is(12, "Combat.Hit", "1.50 ms") && line_is(13, "Fall", "0.31 ms") && line_is(14, "Move", "0.20 ms"));
    values.remote = true;
    draw();
    TIDE_CHECK(stats.line_count == 15 && line_is(5, "delay", "2 ticks") && line_is(14, "Move", "0.20 ms"));

    values.work_ms = -1.0;
    draw();
    TIDE_CHECK(line_is(1, "render", "0.32 ms") && !stats.lines[1].indent);
    values.work_ms = 2.14;

    values.render_known = values.gpu_known = false;
    draw();
    TIDE_CHECK(!text_command("render") && !text_command("gpu (7 draw calls)") && line_is(2, "draw calls", "7"));
    tide_draw_free(&list);
}

TIDE_TEST(stats_system_times_average_measurements_and_keep_their_rows)
{
    start(true);
    draw();
    TIDE_CHECK(line_is(11, "Combat.Hit", "1.50 ms") && line_is(12, "Fall", "0.31 ms") && line_is(13, "Move", "0.20 ms"));

    tide_profile_add(&profile.systems, 0, "Fall", 0, 8 * MS);
    tide_profile_add(&profile.systems, 0, "Fall", 0, 8 * MS);
    tide_profile_add(&profile.systems, 1, "Combat.Hit", 0, 2 * MS);
    tide_profile_add(&profile.systems, 2, "Land", 0, 0);
    tide_profile_add(&profile.systems, 3, "Move", 0, MS);
    tide_stats_collect(&stats, &profile, 0.1);
    draw_only();
    TIDE_CHECK(line_is(11, "Combat.Hit", "1.50 ms") && line_is(12, "Fall", "0.31 ms"));

    // The F7 profiler clears its totals on its own cadence. Both readers still
    // see their whole interval, including the samples measured before a clear.
    tide_profile_clear(&profile);
    tide_profile_add(&profile.systems, 0, "Fall", 0, MS);
    tide_profile_add(&profile.systems, 1, "Combat.Hit", 0, 4 * MS);
    tide_profile_add(&profile.systems, 2, "Land", 0, 0);
    tide_profile_add(&profile.systems, 3, "Move", 0, MS);
    tide_stats_collect(&stats, &profile, 0.1);
    draw_only();
    TIDE_CHECK(line_is(11, "Combat.Hit", "1.50 ms"));
    tide_stats_collect(&stats, &profile, 0.3);
    draw_only();
    TIDE_CHECK(line_is(11, "Combat.Hit", "3.00 ms") && line_is(12, "Fall", "5.67 ms") && line_is(13, "Move", "1.00 ms"));
    TIDE_CHECK(profile.systems.items[0].total == 1.0);

    tide_stats_collect(&stats, &profile, 0.5);
    draw_only();
    TIDE_CHECK(line_is(11, "Combat.Hit", "3.00 ms") && line_is(12, "Fall", "5.67 ms"));

    tide_profile_add(&profile.systems, 0, "Fall", 0, 0);
    tide_profile_add(&profile.systems, 1, "Combat.Hit", 0, 3 * MS);
    tide_profile_add(&profile.systems, 2, "Land", 0, 2 * MS);
    tide_profile_add(&profile.systems, 3, "Move", 0, MS);
    tide_stats_collect(&stats, &profile, 0.5);
    draw_only();
    TIDE_CHECK(line_is(11, "Combat.Hit", "3.00 ms") && line_is(12, "Land", "2.00 ms") && line_is(13, "Move", "1.00 ms"));

    tide_profile_add(&profile.systems, 2, "Land", 0, 0);
    tide_stats_collect(&stats, &profile, 0.5);
    draw_only();
    TIDE_CHECK(stats.slow_count == 1 && line_is(11, "Land", "0.00 ms"));
    tide_draw_free(&list);
}

TIDE_TEST(stats_system_times_restart_for_closed_drawers_matches_and_builds)
{
    start(true);
    draw();
    stats.open = false;
    tide_stats_collect(&stats, &profile, 0.5);
    tide_profile_add(&profile.systems, 0, "Fall", 0, 100 * MS);
    stats.open = true;
    tide_stats_collect(&stats, &profile, 0.0);
    draw_only();
    TIDE_CHECK(stats.slow_count == 0 && stats.line_count == 11);
    tide_profile_add(&profile.systems, 0, "Fall", 0, 2 * MS);
    tide_stats_collect(&stats, &profile, 0.5);
    draw_only();
    TIDE_CHECK(stats.slow_count == 1 && line_is(11, "Fall", "2.00 ms"));

    tide_profile_add(&profile.systems, 0, "Wind", 0, 4 * MS);
    tide_stats_collect(&stats, &profile, 0.1);
    draw_only();
    TIDE_CHECK(stats.slow_count == 0 && stats.line_count == 11);
    tide_profile_add(&profile.systems, 0, "Wind", 0, 2 * MS);
    tide_stats_collect(&stats, &profile, 0.4);
    draw_only();
    TIDE_CHECK(line_is(11, "Wind", "3.00 ms"));

    tide_profile_count(&profile.systems, 1);
    tide_profile_add(&profile.systems, 0, "Rain", 0, 5 * MS);
    tide_stats_collect(&stats, &profile, 0.5);
    draw_only();
    TIDE_CHECK(stats.slow_count == 1 && line_is(11, "Rain", "5.00 ms"));
    tide_stats_collect(&stats, NULL, 0.5);
    draw_only();
    TIDE_CHECK(stats.slow_count == 0 && stats.line_count == 11);
    tide_stats_collect(&stats, &profile, 0.0);
    tide_profile_add(&profile.systems, 0, "Rain", 0, MS);
    tide_stats_collect(&stats, &profile, 0.5);
    tide_stats_off(&stats);
    TIDE_CHECK(stats.open && stats.slow_count == 0 && !stats.profile);
    tide_stats_free(&stats);
    TIDE_CHECK(!stats.open && !stats.systems && stats.system_room == 0);
    tide_draw_free(&list);
}

// ---------------------------------------------------------------------------
// What opens it, and whose a press is

static bool has_press(const tide_devices *d)
{
    bool any = false;
#define ANY(b) any |= (b).pressed || (b).down || (b).up || (b).held;
#define MOUSE(name) ANY(d->mouse.name)
    TIDE_MOUSE_BUTTONS(MOUSE)
#undef MOUSE
    ANY(d->pointer.press)
    ANY(d->touchscreen.primaryTouch.press)
    for (int i = 0; i < TIDE_TOUCHES; i++) ANY(d->touchscreen.touches.at[i].press)
#undef ANY
    return any;
}

#define FLIPPED 1 // The drawer opened or closed
#define SAMPLED 2 // The game's input read a press
#define CLICKED 4 // The GUI's button under the drawer was clicked
#define VIEWED 8  // The views read a press

static int frame(const bool key, const bool tick)
{
    int what = tide_stats_input(&stats, &devices, screen, key) ? FLIPPED : 0;
    sampled = devices;
    tide_gui_hide(&gui, &sampled);
    tide_stats_hide(&stats, &sampled);
    if (tick) {
        if (has_press(&sampled)) what |= SAMPLED;
        tide_devices_consume(&devices);
    }
    tide_devices seen = devices;
    tide_stats_hide(&stats, &seen);
    tide_gui_begin(&gui, &seen, screen, font);
    if (has_press(&gui.devices)) what |= VIEWED;
    if (tide_gui_button(&gui, 7, (tide_rect){700, 480, 260, 60}, "Menu")) what |= CLICKED;
    tide_draw_reset(&list);
    tide_gui_end(&gui, &list);
    draw();
    return what;
}

static int mouse_frame(const float x, const float y, tide_button *button, const bool held)
{
    tide_touches_poll(&devices.touchscreen);
    devices.mouse.position = tide_f2(x, screen.y - y);
    tide_button_set(button, held);
    tide_pointer_poll(&devices, true);
    return frame(false, true);
}

#define NONE ((tide_touch_phase)TIDE_TOUCHES)

static int finger_frame(const uint32_t source, const tide_touch_phase first, const tide_touch_phase second, const float x,
                        const float y)
{
    tide_touches_poll(&devices.touchscreen);
    const tide_touch_phase phases[] = {first, second};
    for (int i = 0; i < 2; i++) {
        if (phases[i] != NONE) tide_touch_event(&devices.touchscreen, phases[i], source, tide_f2(x, screen.y - y));
    }
    tide_pointer_poll(&devices, false);
    return frame(false, true);
}

TIDE_TEST(stats_key_opens_and_closes_them)
{
    start(false);
    tide_touches_poll(&devices.touchscreen);
    tide_pointer_poll(&devices, false);
    TIDE_CHECK(frame(false, true) == 0 && !stats.open);
    TIDE_CHECK(frame(true, true) == FLIPPED && stats.open && stats.line_count == 14);
    TIDE_CHECK(frame(true, true) == 0 && stats.open);
    TIDE_CHECK(frame(false, true) == 0 && stats.open);
    TIDE_CHECK(frame(true, true) == FLIPPED && !stats.open && stats.line_count == 1);
    tide_draw_free(&list);
}

TIDE_TEST(stats_click_opens_them_and_is_never_the_games)
{
    start(false);
    tide_button *left = &devices.mouse.left;
    TIDE_CHECK(mouse_frame(100, 100, left, false) == 0);
    TIDE_CHECK(mouse_frame(920, 516, left, true) == 0);
    TIDE_CHECK(tide_stats_pressed(&stats) && stats.hovered && gui.active == 0);
    TIDE_CHECK(mouse_frame(920, 516, left, true) == 0);
    TIDE_CHECK(mouse_frame(920, 516, left, false) == FLIPPED);
    TIDE_CHECK(stats.open && stats.line_count == 14);
    TIDE_CHECK(mouse_frame(920, 516, left, false) == 0 && !tide_stats_pressed(&stats));
    TIDE_CHECK(mouse_frame(920, 516, left, true) == 0);
    TIDE_CHECK(mouse_frame(920, 516, left, false) == FLIPPED && !stats.open);

    stats.open = true;
    mouse_frame(100, 100, left, false);
    TIDE_CHECK(mouse_frame(900, 300, left, true) == 0);
    TIDE_CHECK(mouse_frame(900, 300, left, false) == FLIPPED && !stats.open);
    TIDE_CHECK(mouse_frame(900, 300, left, false) == 0);
    TIDE_CHECK(mouse_frame(900, 300, left, true) == (SAMPLED | VIEWED));
    TIDE_CHECK(mouse_frame(900, 300, left, false) == (SAMPLED | VIEWED) && !stats.open);

    mouse_frame(720, 500, left, false);
    TIDE_CHECK(mouse_frame(720, 500, left, true) == 0);
    TIDE_CHECK(mouse_frame(720, 500, left, false) == CLICKED && !stats.open);
    stats.open = true;
    mouse_frame(100, 100, left, false);
    TIDE_CHECK(mouse_frame(100, 100, left, true) == (SAMPLED | VIEWED));
    TIDE_CHECK(mouse_frame(900, 300, left, true) == (SAMPLED | VIEWED) && !tide_stats_pressed(&stats));
    TIDE_CHECK(mouse_frame(900, 300, left, false) == (SAMPLED | VIEWED) && stats.open);
    mouse_frame(900, 300, left, false);
    TIDE_CHECK(mouse_frame(900, 300, left, true) == 0);
    TIDE_CHECK(mouse_frame(100, 100, left, true) == 0 && tide_stats_pressed(&stats));
    TIDE_CHECK(mouse_frame(100, 100, left, false) == 0 && stats.open);
    TIDE_CHECK(mouse_frame(100, 100, left, false) == 0 && !tide_stats_pressed(&stats));

    tide_button *right = &devices.mouse.right;
    mouse_frame(900, 300, right, false);
    TIDE_CHECK(mouse_frame(900, 300, right, true) == 0 && tide_stats_pressed(&stats));
    TIDE_CHECK(mouse_frame(900, 300, right, false) == 0 && stats.open);
    TIDE_CHECK(mouse_frame(100, 100, right, false) == 0);
    TIDE_CHECK(mouse_frame(100, 100, right, true) == (SAMPLED | VIEWED));
    mouse_frame(100, 100, right, false);
    mouse_frame(100, 100, right, false);

    devices.mouse.locked = true;
    TIDE_CHECK(mouse_frame(900, 300, left, true) == (SAMPLED | VIEWED) && !stats.hovered);
    TIDE_CHECK(mouse_frame(900, 300, left, false) == (SAMPLED | VIEWED) && stats.open);
    tide_draw_free(&list);
}

TIDE_TEST(stats_click_is_the_drawers_until_a_sample_is_done_with_it)
{
    start(false);
    tide_button *left = &devices.mouse.left;
    mouse_frame(100, 100, left, false);
    for (int held = 1; held >= 0; held--) {
        tide_touches_poll(&devices.touchscreen);
        devices.mouse.position = tide_f2(920, screen.y - 516);
        tide_button_set(left, held == 1);
        tide_pointer_poll(&devices, true);
        TIDE_CHECK(frame(false, false) == (held ? 0 : FLIPPED));
    }
    TIDE_CHECK(stats.open && left->pressed && left->up && tide_stats_pressed(&stats));
    TIDE_CHECK(mouse_frame(920, 516, left, false) == 0);
    TIDE_CHECK(mouse_frame(920, 516, left, false) == 0 && !tide_stats_pressed(&stats));

    for (int held = 1; held >= 0; held--) {
        tide_touches_poll(&devices.touchscreen);
        tide_button_set(left, held == 1);
        tide_pointer_poll(&devices, true);
        frame(false, false);
    }
    TIDE_CHECK(!stats.open && tide_stats_pressed(&stats));
    tide_touches_poll(&devices.touchscreen);
    devices.mouse.position = tide_f2(100, screen.y - 100);
    tide_button_set(left, true);
    tide_pointer_poll(&devices, true);
    TIDE_CHECK(frame(false, false) == VIEWED && !tide_stats_pressed(&stats));
    tide_draw_free(&list);
}

TIDE_TEST(stats_tap_opens_them_and_is_never_the_games)
{
    start(false);
    TIDE_CHECK(finger_frame(1, NONE, NONE, 0, 0) == 0);
    TIDE_CHECK(finger_frame(1, TIDE_TOUCH_BEGAN, NONE, 920, 516) == 0);
    TIDE_CHECK(tide_stats_pressed(&stats) && !stats.hovered && sampled.touchscreen.touches.at[0].id == 0);
    TIDE_CHECK(finger_frame(1, NONE, NONE, 0, 0) == 0);
    TIDE_CHECK(finger_frame(1, TIDE_TOUCH_ENDED, NONE, 920, 516) == FLIPPED && stats.open);
    TIDE_CHECK(finger_frame(1, NONE, NONE, 0, 0) == 0 && !tide_stats_pressed(&stats));

    TIDE_CHECK(finger_frame(2, TIDE_TOUCH_BEGAN, TIDE_TOUCH_ENDED, 920, 516) == 0 && stats.open);
    TIDE_CHECK(finger_frame(2, NONE, NONE, 0, 0) == FLIPPED && !stats.open);
    TIDE_CHECK(finger_frame(2, NONE, NONE, 0, 0) == 0 && !tide_stats_pressed(&stats));

    TIDE_CHECK(finger_frame(3, TIDE_TOUCH_BEGAN, NONE, 920, 516) == 0);
    TIDE_CHECK(finger_frame(3, TIDE_TOUCH_MOVED, NONE, 400, 300) == 0);
    TIDE_CHECK(finger_frame(3, TIDE_TOUCH_ENDED, NONE, 400, 300) == 0 && !stats.open);
    finger_frame(3, NONE, NONE, 0, 0);

    TIDE_CHECK(finger_frame(4, TIDE_TOUCH_BEGAN, NONE, 100, 300) == (SAMPLED | VIEWED));
    TIDE_CHECK(finger_frame(5, TIDE_TOUCH_BEGAN, NONE, 920, 516) == (SAMPLED | VIEWED));
    TIDE_CHECK(sampled.touchscreen.touches.at[0].press.held && sampled.touchscreen.touches.at[1].id == 0);
    TIDE_CHECK(sampled.touchscreen.primaryTouch.press.held && sampled.pointer.press.held);
    TIDE_CHECK(finger_frame(5, TIDE_TOUCH_ENDED, NONE, 920, 516) == (FLIPPED | SAMPLED | VIEWED) && stats.open);
    TIDE_CHECK(sampled.touchscreen.touches.at[1].id == 0);
    TIDE_CHECK(finger_frame(4, TIDE_TOUCH_ENDED, NONE, 100, 300) == (SAMPLED | VIEWED) && stats.open);
    tide_draw_free(&list);
}

TIDE_TEST(stats_that_are_off_take_no_press)
{
    start(true);
    tide_button *left = &devices.mouse.left;
    mouse_frame(100, 100, left, false);
    TIDE_CHECK(mouse_frame(920, 516, left, true) == 0 && tide_stats_pressed(&stats));
    tide_stats_off(&stats);
    TIDE_CHECK(!stats.shown && !tide_stats_pressed(&stats) && stats.open);
    tide_touches_poll(&devices.touchscreen);
    tide_button_set(left, false);
    tide_pointer_poll(&devices, true);
    TIDE_CHECK(!tide_stats_input(&stats, &devices, screen, false) && stats.open);
    tide_devices_consume(&devices);
    tide_touches_poll(&devices.touchscreen);
    tide_button_set(left, true);
    tide_pointer_poll(&devices, true);
    TIDE_CHECK(!tide_stats_input(&stats, &devices, screen, false) && !tide_stats_pressed(&stats));
    tide_devices copy = devices;
    tide_stats_hide(&stats, &copy);
    TIDE_CHECK(copy.mouse.left.held && copy.pointer.press.held);
    tide_draw_free(&list);
    tide_draw_free(&gui.list);
    tide_profile_free(&profile);
    tide_stats_free(&stats);
}
