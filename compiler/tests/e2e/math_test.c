#include <math.h>

#include "game.h"
#include "tide_test.h"

static tide_world world;

static const tide_entity THING = {1, 1};

static bool near(const float a, const float b)
{
    return fabsf(a - b) <= 1e-5f * fmaxf(1.0f, fabsf(b));
}

static bool eq3(const tide_float3 v, const float x, const float y, const float z)
{
    return near(v.x, x) && near(v.y, y) && near(v.z, z);
}

static bool color_is(const tide_color v, const float r, const float g, const float b, const float a)
{
    return v.r == r && v.g == g && v.b == b && v.a == a;
}

static Results results(void)
{
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    return tide_get_Results(&world, THING);
}

TIDE_TEST(math_defaults)
{
    tide_world_init(&world, 1.0f);
    const Body b = tide_get_Body(&world, THING);
    TIDE_REQUIRE(tide_has_Body(&world, THING));
    TIDE_CHECK(eq3(b.position, 1, 2, 3));
    TIDE_CHECK(b.color.y == 0.5f && b.color.w == 1.0f);
    TIDE_CHECK(b.cell.x == 3 && b.cell.y == -4);
    TIDE_CHECK(b.rotation.value.w == 1.0f && b.rotation.value.x == 0.0f);
    TIDE_CHECK(b.basis.c0.x == 1.0f && b.basis.c1.y == 1.0f && b.basis.c0.y == 0.0f);
    TIDE_CHECK(near(b.angle, TIDE_PI_F / 2.0f));
    TIDE_CHECK(b.pi == TIDE_PI_F);
    TIDE_CHECK(color_is(b.rgbTint, 0.25f, 0.5f, 0.75f, 1));
    TIDE_CHECK(color_is(b.rgbaTint, 0.25f, 0.5f, 0.75f, 0.5f));
    TIDE_CHECK(color_is(b.mixedTint, 0.25f, 0.5f, 0.75f, 1));
    TIDE_CHECK(color_is(b.integerTint, 1, 0, 1, 0.5f));
}

TIDE_TEST(math_constructors_and_swizzles)
{
    const Results r = results();
    TIDE_REQUIRE(tide_has_Results(&world, THING));
    TIDE_CHECK(eq3(r.swizzled, 3, 2, 1));
    TIDE_CHECK(r.built.x == 1 && r.built.y == 2 && r.built.z == 7 && r.built.w == 8);
    TIDE_CHECK(eq3(r.splat, 2, 2, 2));
    const Body b = tide_get_Body(&world, THING);
    TIDE_CHECK(b.flat.x == 1 && b.flat.y == 3);
}

TIDE_TEST(math_operators)
{
    const Results r = results();
    TIDE_REQUIRE(tide_has_Results(&world, THING));
    TIDE_CHECK(eq3(r.scaled, 3, 5, 7));
    TIDE_CHECK(r.halved.x == 3 && r.halved.y == -3 && r.halved.z == 4);
    TIDE_CHECK(eq3(r.widened, 3.5f, -3.5f, 3.5f));
    TIDE_CHECK(eq3(r.negated, -1, -2, -3));
}

TIDE_TEST(math_color_constructors)
{
    const Results r = results();
    TIDE_REQUIRE(tide_has_Results(&world, THING));
    TIDE_CHECK(color_is(r.uvColor, 0.25f, 0.5f, 0, 1));
    TIDE_CHECK(color_is(r.rgbColor, 0.25f, 0.5f, 0.75f, 1));
    TIDE_CHECK(color_is(r.rgbaColor, 0.25f, 0.5f, 0.75f, 0.5f));
    TIDE_CHECK(color_is(r.alphaColor, 0.25f, 0.5f, 0.75f, 0.5f));
    TIDE_CHECK(color_is(r.scalarFirstColor, 0.25f, 0.5f, 0.75f, 1));
    TIDE_CHECK(color_is(r.vectorMiddleColor, 0.25f, 0.5f, 0.75f, 0.5f));
    TIDE_CHECK(color_is(r.pairedColor, 0.25f, 0.5f, 0.75f, 0.5f));
    TIDE_CHECK(color_is(r.integerColor, 1, 0, 1, 1));
    TIDE_CHECK(color_is(r.integerRgbaColor, 1, 0, 1, 0));
    TIDE_CHECK(color_is(r.scalarColor, 0.25f, 0.5f, 0.75f, 0.5f));
    TIDE_CHECK(color_is(r.lerpedColor, 0.25f, 0.5f, 0.75f, 0.25f));
    TIDE_CHECK(color_is(r.perChannelColor, 0, 0.5f, 0.5f, 0.5f));
    TIDE_CHECK(color_is(r.clampedColor, 0, 0.25f, 1, 0.75f));
    TIDE_CHECK(color_is(r.saturatedColor, 0, 0.5f, 1, 1));
    TIDE_CHECK(color_is(r.brighterColor, 0.25f, 0.5f, 1, 0.25f));
}

TIDE_TEST(math_conversions_saturate)
{
    const Results r = results();
    TIDE_REQUIRE(tide_has_Results(&world, THING));
    TIDE_CHECK(r.truncated.x == 1 && r.truncated.y == -1 && r.truncated.z == INT32_MAX);
    TIDE_CHECK(r.converted == -2);
}

TIDE_TEST(math_functions)
{
    const Results r = results();
    TIDE_REQUIRE(tide_has_Results(&world, THING));
    TIDE_CHECK(r.dotted == 6.0f);
    TIDE_CHECK(r.length == 13.0f);
    TIDE_CHECK(eq3(r.normalized, 0, 1, 0));
    // Normalize gives zero for what has no direction, and the identity for a quaternion
    TIDE_CHECK(eq3(r.stayed, 1, 2, 3));
    TIDE_CHECK(r.noDirection.x == 0 && r.noDirection.y == 0);
    TIDE_CHECK(r.tooShort.x == 0 && r.tooShort.y == 0 && r.tooShort.z == 0 && r.tooShort.w == 0);
    TIDE_CHECK(r.noRotation.value.x == 0 && r.noRotation.value.y == 0 && r.noRotation.value.z == 0
               && r.noRotation.value.w == 1);
    TIDE_CHECK(eq3(r.crossed, 0, 0, 1));
    TIDE_CHECK(r.clamped == 1);
    TIDE_CHECK(eq3(r.lerped, 5, 10, 15));
    TIDE_CHECK(r.sine == 1.0f);
    TIDE_CHECK(near(r.angle45, 45.0f));
    TIDE_CHECK(r.power == 1024.0f);
}

TIDE_TEST(math_quaternions_and_matrices)
{
    const Results r = results();
    TIDE_REQUIRE(tide_has_Results(&world, THING));
    TIDE_CHECK(eq3(r.rotated, 0, 0, -1));
    TIDE_CHECK(eq3(r.transformed, 3, 4, 5));
    TIDE_CHECK(near(r.determinant, 8.0f));
    TIDE_CHECK(r.matrixFromRows.c0.x == 1 && r.matrixFromRows.c1.x == 2);
    TIDE_CHECK(r.matrixFromRows.c0.y == 3 && r.matrixFromRows.c1.y == 4);
    TIDE_CHECK(eq3(r.columnScaled, 0, 3, 0));
    const Body b = tide_get_Body(&world, THING);
    TIDE_CHECK(near(b.rotation.value.y, sinf(TIDE_PI_F / 4.0f)));
}

TIDE_TEST(math_swizzle_writes)
{
    const Results r = results();
    TIDE_REQUIRE(tide_has_Results(&world, THING));
    // w = (0,0,0); w.xz = (4,5) -> (4,0,5); w.y += 1 -> (4,1,5); w.zx += (1,1) -> (5,1,6)
    TIDE_CHECK(eq3(r.written, 5, 1, 6));
}

TIDE_TEST(math_hash)
{
    const Results r = results();
    TIDE_REQUIRE(tide_has_Results(&world, THING));
    TIDE_CHECK(r.hashed == tide_hash_i(-7));
    TIDE_CHECK(r.hashed2 == tide_hash_i2(tide_i2(3, -4)));
    TIDE_CHECK(r.hashed3 == tide_hash_i3(tide_i3(3, -4, 9)));
    TIDE_CHECK(r.hashed4 == tide_hash_i4(tide_i4(1, 2, 3, 4)));
    TIDE_CHECK(r.hashed2 != r.hashed3);
}

// Noise is noise.c's, bit for bit, with 0 for a seed the call leaves out
TIDE_TEST(math_noise)
{
    const Results r = results();
    TIDE_REQUIRE(tide_has_Results(&world, THING));
    const tide_float2 at = tide_f2(1.0f * 0.37f + 0.11f, 2.0f * 0.37f + 0.11f);
    const tide_float3 deep = tide_f3(1.0f * 0.83f, 2.0f * 0.83f, 3.0f * 0.83f);
    TIDE_CHECK(r.perlins.x == tide_perlin_f2(at, 0) && r.perlins.y == tide_perlin_f2(at, 7));
    TIDE_CHECK(r.perlins.z == tide_perlin_f3(deep, 0) && r.perlins.w == tide_perlin_f3(deep, -3));
    TIDE_CHECK(r.simplexes.x == tide_simplex_f2(at, 0) && r.simplexes.y == tide_simplex_f2(at, 7));
    TIDE_CHECK(r.simplexes.z == tide_simplex_f3(deep, 0) && r.simplexes.w == tide_simplex_f3(deep, -3));
    const tide_float2 c2 = tide_cellular_f2(at, 0);
    const tide_float2 c3 = tide_cellular_f3(deep, 7);
    TIDE_CHECK(r.cells.x == c2.x && r.cells.y == c2.y && r.cells.z == c3.x && r.cells.w == c3.y);
    TIDE_CHECK(r.perlins.x != r.perlins.y && r.perlins.x != 0.0f && r.simplexes.z != r.simplexes.w);
}
