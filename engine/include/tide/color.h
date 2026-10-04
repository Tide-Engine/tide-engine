#pragma once

// Tide's Color: red, green, blue and alpha from 0 to 1, as in Unity.
//
// Temporary implementation written by Claude; the project owner takes it over
// later. Generated code uses the type and the constants below.

typedef struct tide_color {
    float r, g, b, a;
} tide_color;

// Color.white and friends, with Unity's values.
#define TIDE_COLOR_WHITE ((tide_color){1.0f, 1.0f, 1.0f, 1.0f})
#define TIDE_COLOR_BLACK ((tide_color){0.0f, 0.0f, 0.0f, 1.0f})
#define TIDE_COLOR_RED ((tide_color){1.0f, 0.0f, 0.0f, 1.0f})
#define TIDE_COLOR_GREEN ((tide_color){0.0f, 1.0f, 0.0f, 1.0f})
#define TIDE_COLOR_BLUE ((tide_color){0.0f, 0.0f, 1.0f, 1.0f})
#define TIDE_COLOR_YELLOW ((tide_color){1.0f, 0.92156863f, 0.015686275f, 1.0f})
#define TIDE_COLOR_CYAN ((tide_color){0.0f, 1.0f, 1.0f, 1.0f})
#define TIDE_COLOR_MAGENTA ((tide_color){1.0f, 0.0f, 1.0f, 1.0f})
#define TIDE_COLOR_GRAY ((tide_color){0.5f, 0.5f, 0.5f, 1.0f})
#define TIDE_COLOR_CLEAR ((tide_color){0.0f, 0.0f, 0.0f, 0.0f})

// Tide's operators on colors: channel by channel, alpha too, and scaled by a
// number.
static inline tide_color tide_add_color(const tide_color a, const tide_color b)
{
    return (tide_color){a.r + b.r, a.g + b.g, a.b + b.b, a.a + b.a};
}

static inline tide_color tide_sub_color(const tide_color a, const tide_color b)
{
    return (tide_color){a.r - b.r, a.g - b.g, a.b - b.b, a.a - b.a};
}

static inline tide_color tide_mul_color(const tide_color a, const tide_color b)
{
    return (tide_color){a.r * b.r, a.g * b.g, a.b * b.b, a.a * b.a};
}

static inline tide_color tide_scale_color(const tide_color c, const float s)
{
    return (tide_color){c.r * s, c.g * s, c.b * s, c.a * s};
}

static inline tide_color tide_divs_color(const tide_color c, const float s)
{
    return (tide_color){c.r / s, c.g / s, c.b / s, c.a / s};
}
