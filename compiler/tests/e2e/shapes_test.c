#include "game.h"
#include "tide_test.h"

static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

static bool close_to(const float a, const float b)
{
    const float difference = a - b;
    return difference * difference < 0.00001f;
}

static bool same_color(const tide_color a, const tide_color b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

// The window's shape commands, in order, and the target's.
static const tide_draw_command *shapes[32], *targeted[32];
static int shape_count, targeted_count;

static const tide_draw_shape_data *data(const tide_draw_command *c)
{
    return &draw.shapes[c->shape];
}

// Whether `c` is a shape of `form` around (x, y), `width` by `height`.
static bool is(const tide_draw_command *c, const tide_shape_form form, const float x, const float y, const float width,
               const float height)
{
    return data(c)->form == (uint32_t)form && close_to(c->a.x, x) && close_to(c->a.y, y) && close_to(c->b.x, width)
        && close_to(c->b.y, height);
}

static bool turned(const tide_draw_command *c, const float cos, const float sin)
{
    return close_to(data(c)->turn.x, cos) && close_to(data(c)->turn.y, sin);
}

// A shape in one color with a sharp edge, its corners round by `radius`.
static bool solid(const tide_draw_command *c, const float radius, const tide_color color)
{
    const tide_draw_shape_data *s = data(c);
    return close_to(s->radius, radius) && s->outline == 0 && s->soft == 0 && same_color(c->color, color)
        && same_color(s->color2, color);
}

// An outline alone, `width` thick.
static bool wire(const tide_draw_command *c, const float radius, const float width, const tide_color color)
{
    const tide_draw_shape_data *s = data(c);
    return close_to(s->radius, radius) && close_to(s->outline, width) && s->soft == 0 && c->color.a == 0
        && s->color2.a == 0 && same_color(s->outline_color, color);
}

TIDE_TEST(shapes_record_what_each_call_says)
{
    tide_local_init(&local);
    local.Canvas.target = (tide_render_target){.id = 11, .size = {.width = 16, .height = 12}};
    tide_devices devices = {0};
    tide_gui_begin(&gui, &devices, tide_f2(200, 100), NULL);
    tide_frame(NULL, NULL, 1.0f, NULL, &local, &draw, &gui);

    uint32_t destination = 0;
    for (uint32_t i = 0; i < draw.count; i++) {
        const tide_draw_command *c = &draw.commands[i];
        if (c->kind == TIDE_DRAW_TARGET) destination = c->target ? (uint32_t)draw.targets[c->target - 1u].id : 0;
        if (c->kind != TIDE_DRAW_SHAPE) continue;
        TIDE_CHECK(c->shape < draw.shape_count && (destination == 0 || destination == 11));
        if (destination == 0 && shape_count < 32) shapes[shape_count++] = c;
        if (destination == 11 && targeted_count < 32) targeted[targeted_count++] = c;
    }
    // What has no size, width or sweep recorded nothing
    TIDE_REQUIRE(shape_count == 17 && targeted_count == 12);
    const tide_color clear = TIDE_COLOR_CLEAR, white = TIDE_COLOR_WHITE;

    // A thick line is a pill as long as it and its ends, turned along it
    TIDE_CHECK(is(shapes[0], TIDE_SHAPE_BOX, 1.5f, 2, 7, 2) && turned(shapes[0], 0.6f, 0.8f));
    TIDE_CHECK(solid(shapes[0], 1, TIDE_COLOR_RED));
    // Outlines: a clear fill, and an outline as wide as they say
    TIDE_CHECK(is(shapes[1], TIDE_SHAPE_BOX, 1, 1, 8, 6) && wire(shapes[1], 0, 1.5f, TIDE_COLOR_GREEN));
    TIDE_CHECK(is(shapes[2], TIDE_SHAPE_BOX, 2, 2, 10, 10) && wire(shapes[2], 5, 2, TIDE_COLOR_BLUE));
    TIDE_CHECK(is(shapes[3], TIDE_SHAPE_BOX, 3, 3, 8, 4) && solid(shapes[3], 0, TIDE_COLOR_YELLOW));
    TIDE_CHECK(turned(shapes[3], tide_cos_f(0.5f), tide_sin_f(0.5f)));
    TIDE_CHECK(is(shapes[4], TIDE_SHAPE_BOX, 4, 4, 8, 4) && solid(shapes[4], 1, TIDE_COLOR_CYAN));
    TIDE_CHECK(turned(shapes[4], 1, 0));
    // Corners are no rounder than half the smaller side
    TIDE_CHECK(is(shapes[5], TIDE_SHAPE_BOX, 5, 5, 8, 4) && solid(shapes[5], 2, TIDE_COLOR_MAGENTA));
    TIDE_CHECK(turned(shapes[5], -1, 0));
    TIDE_CHECK(is(shapes[6], TIDE_SHAPE_BOX, 6, 6, 8, 4) && wire(shapes[6], 1, 0.5f, white));
    TIDE_CHECK(is(shapes[7], TIDE_SHAPE_ELLIPSE, 7, 7, 8, 4) && solid(shapes[7], 0, TIDE_COLOR_GRAY));
    // An ellipse as wide as it's tall is a circle: a box all corners
    TIDE_CHECK(is(shapes[8], TIDE_SHAPE_BOX, 8, 8, 6, 6) && solid(shapes[8], 3, TIDE_COLOR_GRAY));
    TIDE_CHECK(is(shapes[9], TIDE_SHAPE_ELLIPSE, 9, 9, 8, 4) && wire(shapes[9], 0, 0.5f, TIDE_COLOR_BLACK));
    // Part of a ring: turned to its middle, half its sweep each side
    TIDE_CHECK(is(shapes[10], TIDE_SHAPE_ARC, 10, 10, 10, 10) && turned(shapes[10], 0, 1));
    TIDE_CHECK(close_to(data(shapes[10])->sweep, TIDE_PI_F * 0.5f) && close_to(data(shapes[10])->outline, 2));
    TIDE_CHECK(same_color(shapes[10]->color, TIDE_COLOR_RED));
    // ...and a whole turn of it is the ring, no thicker than its radius
    TIDE_CHECK(is(shapes[11], TIDE_SHAPE_BOX, 11, 11, 10, 10) && wire(shapes[11], 5, 5, TIDE_COLOR_RED));

    // The general shape: what's left out is white, one color, sharp
    TIDE_CHECK(is(shapes[12], TIDE_SHAPE_BOX, 1, 2, 3, 4) && solid(shapes[12], 0, white) && turned(shapes[12], 1, 0));
    const tide_draw_shape_data *full = data(shapes[13]);
    TIDE_CHECK(is(shapes[13], TIDE_SHAPE_BOX, 5, 6, 8, 2) && turned(shapes[13], 0, 1) && close_to(full->radius, 0.5f));
    TIDE_CHECK(same_color(shapes[13]->color, TIDE_COLOR_RED) && same_color(full->color2, clear));
    TIDE_CHECK(close_to(full->outline, 0.25f) && same_color(full->outline_color, TIDE_COLOR_BLUE));
    TIDE_CHECK(full->soft == 3);
    // A shape a function made, and one a function was handed
    const tide_draw_shape_data *badge = data(shapes[14]);
    TIDE_CHECK(is(shapes[14], TIDE_SHAPE_BOX, 9, 9, 6, 6) && close_to(badge->radius, 3) && badge->outline == 1);
    TIDE_CHECK(same_color(shapes[14]->color, TIDE_COLOR_GREEN)
               && same_color(badge->color2, TIDE_COLOR_GREEN)
               && same_color(badge->outline_color, TIDE_COLOR_BLACK));
    TIDE_CHECK(is(shapes[15], TIDE_SHAPE_BOX, -3, -4, 4, 2) && solid(shapes[15], 1, white));
    // Color.none is no second color
    TIDE_CHECK(is(shapes[16], TIDE_SHAPE_BOX, 0, 0, 4, 2) && data(shapes[16])->soft == -2);
    TIDE_CHECK(same_color(data(shapes[16])->color2, white));

    // The same calls on a target record the same shapes, there
    TIDE_CHECK(is(targeted[0], TIDE_SHAPE_BOX, 0.5f, 0, 3, 2) && solid(targeted[0], 1, TIDE_COLOR_RED));
    TIDE_CHECK(wire(targeted[1], 0, 0.5f, TIDE_COLOR_RED));
    TIDE_CHECK(wire(targeted[2], 2, 0.5f, TIDE_COLOR_RED));
    TIDE_CHECK(solid(targeted[3], 0, TIDE_COLOR_RED) && turned(targeted[3], tide_cos_f(1), tide_sin_f(1)));
    TIDE_CHECK(solid(targeted[4], 0.5f, TIDE_COLOR_RED) && turned(targeted[4], 1, 0));
    TIDE_CHECK(solid(targeted[5], 0.5f, TIDE_COLOR_RED) && turned(targeted[5], tide_cos_f(1), tide_sin_f(1)));
    TIDE_CHECK(wire(targeted[6], 0.5f, 0.25f, TIDE_COLOR_RED));
    TIDE_CHECK(is(targeted[7], TIDE_SHAPE_ELLIPSE, 0, 0, 4, 2) && turned(targeted[7], 1, 0));
    TIDE_CHECK(is(targeted[8], TIDE_SHAPE_ELLIPSE, 0, 0, 4, 2) && turned(targeted[8], tide_cos_f(1), tide_sin_f(1)));
    TIDE_CHECK(is(targeted[9], TIDE_SHAPE_ELLIPSE, 0, 0, 4, 2) && wire(targeted[9], 0, 0.25f, TIDE_COLOR_RED));
    TIDE_CHECK(is(targeted[10], TIDE_SHAPE_ARC, 0, 0, 4, 4) && close_to(data(targeted[10])->sweep, 0.5f));
    TIDE_CHECK(is(targeted[11], TIDE_SHAPE_BOX, 0, 0, 4, 2) && solid(targeted[11], 1, white));

    // Appended to another list, they bring the rest of themselves along
    tide_draw_list other = {0};
    tide_draw_round_rect(&other, tide_f2(0, 0), tide_f2(1, 1), 0.25f, white);
    tide_draw_append(&other, &draw);
    TIDE_CHECK(other.shape_count == 1u + draw.shape_count);
    uint32_t at = 0;
    for (uint32_t i = 0; i < other.count && at < 2; i++) {
        const tide_draw_command *c = &other.commands[i];
        if (c->kind != TIDE_DRAW_SHAPE || at++ == 0) continue;
        const tide_draw_shape_data *s = &other.shapes[c->shape];
        TIDE_CHECK(close_to(c->a.x, 1.5f) && close_to(s->turn.x, 0.6f) && close_to(s->turn.y, 0.8f) && s->radius == 1);
    }
    TIDE_CHECK(at == 2);
    tide_draw_free(&other);
    tide_draw_free(&draw);
    tide_local_free(&local);
}
