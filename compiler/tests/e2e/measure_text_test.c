#include "game.h"
#include "tide_test.h"

#include <string.h>

static bool close_to(const float a, const float b)
{
    const float difference = a - b;
    return difference * difference < 0.0001f;
}

// What the renderer was asked, in order.
static struct {
    float size;
    bool target;
} asked[16];
static int asks;

// A font of the test's: a character is half as wide as text is tall, and
// lines are one and a half sizes apart.
static tide_float2 measure(const char *text, const float size, const bool target, const unsigned char *font)
{
    (void)font;
    if (asks < 16) asked[asks].size = size, asked[asks].target = target;
    asks++;
    int widest = 0, across = 0, lines = 1;
    for (; *text; text++) {
        if (*text == '\n') lines++, across = 0;
        else if (++across > widest) widest = across;
    }
    return tide_f2((float)widest * size * 0.5f, size + (float)(lines - 1) * size * 1.5f);
}

static void frame(tide_local *local, tide_draw_list *draw, const tide_measure_fn font)
{
    static tide_gui gui;
    tide_devices devices = {0};
    tide_local_init(local);
    local->Probe.panel = (tide_render_target){.id = 11, .width = 16, .height = 12};
    tide_draw_reset(draw);
    tide_gui_begin(&gui, &devices, tide_f2(800, 600), font);
    tide_frame(NULL, NULL, 1, NULL, local, draw, &gui);
}

TIDE_TEST(measure_text_is_in_the_units_the_draw_calls_are_in)
{
    tide_local local = {0};
    tide_draw_list draw = {0};
    asks = 0;
    frame(&local, &draw, measure);
    const Probe *probe = &local.Probe;

    // On the screen, a unit is a pixel
    TIDE_CHECK(probe->screen.x == 20 && probe->screen.y == 10);
    TIDE_CHECK(probe->lines.x == 20 && probe->lines.y == 25);
    TIDE_CHECK(probe->nothing.x == 0 && probe->nothing.y == 0);
    TIDE_CHECK(probe->centered.x == 380 && probe->centered.y == 290);
    bool drawn = false;
    for (uint32_t i = 0; i < draw.count; i++) {
        const tide_draw_command *c = &draw.commands[i];
        if (c->kind != TIDE_DRAW_TEXT) continue;
        drawn = strcmp(draw.text + c->text, "abcd") == 0 && c->a.x == 380 && c->a.y == 290 && c->b.x == 20;
    }
    TIDE_CHECK(drawn);

    // Under a camera that shows 10 units of the window's 600 pixels, text is
    // drawn 60 times its size in pixels, and measured there
    TIDE_CHECK(close_to(probe->world.x, 1) && close_to(probe->world.y, 0.5f));
    TIDE_CHECK(close_to(probe->helper.x, 0.5f) && close_to(probe->helper.y, 0.25f));
    // A target has its own pixels and its own camera: 8 units of its 12 pixels
    TIDE_CHECK(close_to(probe->target.x, 4) && close_to(probe->target.y, 2));
    TIDE_CHECK(close_to(probe->after.x, 1) && close_to(probe->after.y, 0.5f));

    // A size nothing is drawn at asks the renderer nothing
    TIDE_REQUIRE(asks == 7);
    TIDE_CHECK(asked[0].size == 10 && asked[1].size == 10 && asked[2].size == 20);
    TIDE_CHECK(close_to(asked[3].size, 30) && close_to(asked[4].size, 15) && close_to(asked[6].size, 30));
    TIDE_CHECK(close_to(asked[5].size, 3) && asked[5].target);
    for (int i = 0; i < 7; i++) TIDE_CHECK(asked[i].target == (i == 5));

    tide_draw_free(&draw);
    tide_local_free(&local);
}

TIDE_TEST(measure_text_guesses_without_a_renderer)
{
    tide_local local = {0};
    tide_draw_list draw = {0};
    frame(&local, &draw, NULL);
    const Probe *probe = &local.Probe;

    // As the GUI guesses: 0.6 of the size a byte, and lines 1.2 sizes apart
    TIDE_CHECK(close_to(probe->screen.x, 24) && probe->screen.y == 10);
    TIDE_CHECK(close_to(probe->lines.x, 42) && close_to(probe->lines.y, 22));
    TIDE_CHECK(probe->nothing.x == 0 && probe->nothing.y == 0);
    TIDE_CHECK(close_to(probe->world.x, 1.2f) && close_to(probe->world.y, 0.5f));
    TIDE_CHECK(close_to(probe->target.x, 4.8f) && close_to(probe->target.y, 2));

    tide_draw_free(&draw);
    tide_local_free(&local);
}
