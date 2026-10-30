#pragma once

#include <stdbool.h>

#include "tide/math.h"

// Tide's Color: red, green, blue and alpha from 0 to 1, as in Unity. Generated
// code uses the type and the constants below.

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

// Color.none: no color, where one can be left out, like a shape's second
// color. It's no color to draw with: its alpha is under 0, and nothing shows.
#define TIDE_COLOR_NONE ((tide_color){0.0f, 0.0f, 0.0f, -1.0f})

static inline bool tide_color_is_none(const tide_color c)
{
    return c.r == 0.0f && c.g == 0.0f && c.b == 0.0f && c.a == -1.0f;
}

// Tide's operators on colors: channel by channel, alpha too, and scaled by a
// number.
// color.WithAlpha(a): the color with that alpha, its red, green and blue as they are.
static inline tide_color tide_color_with_alpha(const tide_color c, const float alpha)
{
    return (tide_color){c.r, c.g, c.b, alpha};
}

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

// Math's component-wise functions on colors: tide/math.h's on each channel,
// alpha too. A number beside a color is that number in every channel.
static inline tide_color tide_c_splat(const float s) { return (tide_color){s, s, s, s}; }

#define TIDE_COLOR_MAP1(name)                                           \
    static inline tide_color tide_##name##_c(const tide_color a)        \
    {                                                                   \
        return (tide_color){tide_##name##_f(a.r), tide_##name##_f(a.g), \
                            tide_##name##_f(a.b), tide_##name##_f(a.a)}; \
    }

#define TIDE_COLOR_MAP2(name)                                                         \
    static inline tide_color tide_##name##_c(const tide_color a, const tide_color b)  \
    {                                                                                 \
        return (tide_color){tide_##name##_f(a.r, b.r), tide_##name##_f(a.g, b.g),     \
                            tide_##name##_f(a.b, b.b), tide_##name##_f(a.a, b.a)};    \
    }

#define TIDE_COLOR_MAP3(name)                                                                            \
    static inline tide_color tide_##name##_c(const tide_color a, const tide_color b, const tide_color c) \
    {                                                                                                    \
        return (tide_color){tide_##name##_f(a.r, b.r, c.r), tide_##name##_f(a.g, b.g, c.g),              \
                            tide_##name##_f(a.b, b.b, c.b), tide_##name##_f(a.a, b.a, c.a)};             \
    }

TIDE_COLOR_MAP1(abs)
TIDE_COLOR_MAP1(sign)
TIDE_COLOR_MAP2(min)
TIDE_COLOR_MAP2(max)
TIDE_COLOR_MAP3(clamp)
TIDE_COLOR_MAP1(floor)
TIDE_COLOR_MAP1(ceil)
TIDE_COLOR_MAP1(round)
TIDE_COLOR_MAP1(trunc)
TIDE_COLOR_MAP1(frac)
TIDE_COLOR_MAP1(sqrt)
TIDE_COLOR_MAP1(rsqrt)
TIDE_COLOR_MAP1(saturate)
TIDE_COLOR_MAP1(radians)
TIDE_COLOR_MAP1(degrees)
TIDE_COLOR_MAP1(sin)
TIDE_COLOR_MAP1(cos)
TIDE_COLOR_MAP1(tan)
TIDE_COLOR_MAP1(asin)
TIDE_COLOR_MAP1(acos)
TIDE_COLOR_MAP1(atan)
TIDE_COLOR_MAP2(atan2)
TIDE_COLOR_MAP1(exp)
TIDE_COLOR_MAP1(exp2)
TIDE_COLOR_MAP1(log)
TIDE_COLOR_MAP1(log2)
TIDE_COLOR_MAP1(log10)
TIDE_COLOR_MAP2(pow)
TIDE_COLOR_MAP2(step)
TIDE_COLOR_MAP3(lerp)
TIDE_COLOR_MAP3(unlerp)
TIDE_COLOR_MAP3(smoothstep)

#undef TIDE_COLOR_MAP1
#undef TIDE_COLOR_MAP2
#undef TIDE_COLOR_MAP3
