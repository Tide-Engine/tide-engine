#include <stdio.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

// Draw.Text's anchor and weight, and Draw.MeasureText's weight, as the
// draw list and the renderer's measure get them.

static bool close_to(const float a, const float b)
{
    const float d = a - b;
    return d < 0.001f && d > -0.001f;
}

// A font of the test's: a character is half as wide as text is tall, and
// twice that in bold.
static struct {
    float size;
    bool target;
    const unsigned char *font;
} asked[16];
static int asks;

static tide_float2 measure(const char *text, const float size, const tide_target_size *target, const unsigned char *font)
{
    if (asks < 16) asked[asks].size = size, asked[asks].target = target != NULL, asked[asks].font = font;
    asks++;
    return tide_f2((float)strlen(text) * size * (font ? 1.0f : 0.5f), size);
}

static void frame(tide_local *local, tide_draw_list *draw)
{
    static tide_gui gui;
    tide_devices devices = {0};
    local->Probe.panel = (tide_render_target){.id = 11, .size = {.width = 16, .height = 12}};
    tide_draw_reset(draw);
    tide_gui_begin(&gui, &devices, tide_f2(800, 600), measure);
    tide_frame(NULL, NULL, 1, NULL, local, draw, &gui);
}

static const tide_draw_command *text_command(const tide_draw_list *draw, const char *text)
{
    for (uint32_t i = 0; i < draw->count; i++) {
        const tide_draw_command *c = &draw->commands[i];
        if (c->kind == TIDE_DRAW_TEXT && strcmp(draw->text + c->text, text) == 0) return c;
    }
    return NULL;
}

// Whether the list has `text` at (x, y), `size` high, at `anchor`, in bold or not.
static bool drawn(const tide_draw_list *draw, const char *text, const float x, const float y, const float size,
                  const tide_anchor anchor, const bool bold)
{
    const tide_draw_command *c = text_command(draw, text);
    return c && close_to(c->a.x, x) && close_to(c->a.y, y) && close_to(c->b.x, size) && c->words.anchor == (uint32_t)anchor
        && c->words.bold == (bold ? 1u : 0u);
}

TIDE_TEST(text_takes_an_anchor_and_a_weight)
{
    tide_local local = {0};
    tide_draw_list draw = {0};
    tide_local_init(&local);
    asks = 0;
    frame(&local, &draw);

    // Without either, a text is at its top left corner in the regular weight, as it always was
    TIDE_CHECK(drawn(&draw, "plain", 1, 2, 10, TIDE_ANCHOR_UPPER_LEFT, false));
    TIDE_CHECK(drawn(&draw, "anchored", 3, 4, 11, TIDE_ANCHOR_MIDDLE_CENTER, false));
    TIDE_CHECK(drawn(&draw, "bold", 5, 6, 12, TIDE_ANCHOR_UPPER_LEFT, true));
    TIDE_CHECK(drawn(&draw, "both", 7, 8, 13, TIDE_ANCHOR_LOWER_RIGHT, true));
    TIDE_CHECK(drawn(&draw, "regular", 9, 10, 14, TIDE_ANCHOR_UPPER_CENTER, false));
    TIDE_CHECK(drawn(&draw, "passed on", 11, 12, 15, TIDE_ANCHOR_MIDDLE_LEFT, true));
    // A position is the anchor's, in the units the call is in: under a camera, the world's
    TIDE_CHECK(drawn(&draw, "world", -1, 1.5f, 0.5f, TIDE_ANCHOR_UPPER_RIGHT, true));
    TIDE_CHECK(drawn(&draw, "panel", 1, 1, 2, TIDE_ANCHOR_MIDDLE_RIGHT, true));

    // Tide's Anchor has tide_anchor's values, and a number that's none of them is the first
    for (int i = 0; i < 9; i++) {
        char text[16];
        snprintf(text, sizeof text, "anchor %d", i);
        TIDE_CHECK(drawn(&draw, text, (float)i, (float)i, 18, (tide_anchor)i, false));
    }
    TIDE_CHECK(drawn(&draw, "no anchor", 17, 18, 19, TIDE_ANCHOR_UPPER_LEFT, false));

    // A weight worked out as the game runs: the regular one here, each call once
    TIDE_CHECK(drawn(&draw, "chosen", 13, 14, 16, TIDE_ANCHOR_UPPER_LEFT, false));
    TIDE_CHECK(drawn(&draw, "chosen again", 15, 16, 17, TIDE_ANCHOR_LOWER_CENTER, false));
    TIDE_CHECK(local.Probe.asked == 2);

    // The list has the bold weight's file for its renderer, as bold text was drawn
    TIDE_CHECK(draw.bold == tide_font_bold_ttf);

    tide_draw_free(&draw);
    tide_local_free(&local);
}

TIDE_TEST(text_measures_in_its_weight)
{
    tide_local local = {0};
    tide_draw_list draw = {0};
    tide_local_init(&local);
    asks = 0;
    frame(&local, &draw);
    const Probe *probe = &local.Probe;

    // The renderer is asked in the weight's file: none for the regular one
    TIDE_CHECK(probe->regular.x == 20 && probe->regular.y == 10);
    TIDE_CHECK(probe->named.x == 20 && probe->bold.x == 40 && probe->chosen.x == 20);
    // A target's own pixels and camera: 8 units of its 12 pixels
    TIDE_CHECK(close_to(probe->target.x, 8) && close_to(probe->target.y, 2));
    TIDE_REQUIRE(asks == 5);
    TIDE_CHECK(asked[0].font == NULL && asked[1].font == NULL && asked[3].font == NULL);
    TIDE_CHECK(asked[2].font == tide_font_bold_ttf && asked[4].font == tide_font_bold_ttf);
    TIDE_CHECK(asked[4].target && close_to(asked[4].size, 3));
    for (int i = 0; i < 4; i++) TIDE_CHECK(!asked[i].target && asked[i].size == 10);

    // The weight a function gives, once it gives the bold one
    local.Probe.heavy = true;
    local.Probe.asked = 0;
    asks = 0;
    frame(&local, &draw);
    TIDE_CHECK(drawn(&draw, "chosen", 13, 14, 16, TIDE_ANCHOR_UPPER_LEFT, true));
    TIDE_CHECK(drawn(&draw, "chosen again", 15, 16, 17, TIDE_ANCHOR_LOWER_CENTER, true));
    TIDE_CHECK(drawn(&draw, "plain", 1, 2, 10, TIDE_ANCHOR_UPPER_LEFT, false));
    TIDE_CHECK(local.Probe.asked == 2);
    TIDE_CHECK(local.Probe.chosen.x == 40);
    TIDE_REQUIRE(asks == 5);
    TIDE_CHECK(asked[3].font == tide_font_bold_ttf);

    tide_draw_free(&draw);
    tide_local_free(&local);
}

// The GUI's list goes after the world's (tide_gui_end): text keeps its
// anchor and its weight on the way, and the list it joins gets the file.
TIDE_TEST(text_keeps_its_anchor_and_weight_when_lists_join)
{
    tide_draw_list from = {0}, to = {0};
    tide_draw_text_at(&from, "joined", tide_f2(4, 5), 20, TIDE_COLOR_WHITE, TIDE_ANCHOR_LOWER_CENTER, TIDE_FONT_BOLD);
    tide_draw_text(&from, "plain", tide_f2(1, 2), 10, TIDE_COLOR_WHITE);
    TIDE_CHECK(to.bold == NULL);
    tide_draw_append(&to, &from);
    TIDE_CHECK(drawn(&to, "joined", 4, 5, 20, TIDE_ANCHOR_LOWER_CENTER, true));
    TIDE_CHECK(drawn(&to, "plain", 1, 2, 10, TIDE_ANCHOR_UPPER_LEFT, false));
    TIDE_CHECK(to.bold == tide_font_bold_ttf);
    // A frame that draws nothing in bold has no file for it
    tide_draw_reset(&to);
    TIDE_CHECK(to.bold == NULL);
    tide_draw_free(&from);
    tide_draw_free(&to);
}
