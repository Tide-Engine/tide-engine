// The renderer, through the whole drawing path (the draw list, the platform
// layer, OpenGL or WebGL), read back from an offscreen render.
//
// It checks that each shape lands where it should, that commands draw in
// the order they were recorded whatever the renderer batches together, that
// a frame can draw far more than a list used to hold, that meshes draw
// their triangles with their corners' colors, their textures and the clip,
// that 3D meshes draw through their cameras, nearest in front, and that
// meshes draw through shaders of the game's, with what those read.
// `--bench` (or `?bench` on the web) times frames of many shapes instead.

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tide/net.h" // tide_hash
#include "tide/platform.h"
#include "tide/time.h"

#ifdef __wasm__
#include "tide_web.h" // The page, to lose WebGPU's device
#endif

#define WIDTH 256
#define HEIGHT 352

static tide_draw_list list;
static int failures;

// The window's size: the one asked for on desktop and the web, and the
// screen's on a phone, where the checks stay in its top left corner.
static tide_float2 screen = {WIDTH, HEIGHT};

// The world position at window pixel (x, y), through the frame's first
// camera: the origin at the middle, a world unit a pixel, y up.
static tide_float2 at(const float x, const float y)
{
    return tide_f2(x - screen.x * 0.5f, screen.y * 0.5f - y);
}

static uint32_t rgba(const tide_color c)
{
    const float channels[4] = {c.r, c.g, c.b, c.a};
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) v = v << 8 | (uint32_t)(channels[i] * 255.0f + 0.5f);
    return v;
}

// Whether two colors' red, green and blue are within `slack` of each other.
static bool near(const uint32_t a, const uint32_t b, const int slack)
{
    for (int shift = 8; shift < 32; shift += 8) {
        const int d = (int)(a >> shift & 0xFFu) - (int)(b >> shift & 0xFFu);
        if (d > slack || d < -slack) return false;
    }
    return true;
}

static const tide_color background = {0.1f, 0.1f, 0.1f, 1.0f};
static const tide_color red = {1.0f, 0.0f, 0.0f, 1.0f};
static const tide_color blue = {0.0f, 0.0f, 1.0f, 1.0f};
static const tide_color green = {0.0f, 1.0f, 0.0f, 1.0f};
static const tide_color white = {1.0f, 1.0f, 1.0f, 1.0f};
static const tide_color yellow = {1.0f, 1.0f, 0.0f, 1.0f};
static const tide_color cyan = {0.0f, 1.0f, 1.0f, 1.0f};
static const tide_color orange = {1.0f, 0.5f, 0.0f, 1.0f};
static const tide_color magenta = {1.0f, 0.0f, 1.0f, 1.0f};
static const tide_color purple = {0.5f, 0.0f, 1.0f, 1.0f};

#define BAND 20000u // Far past the 16384 commands a list used to hold
#define BAND_TOP 272

static tide_color band_color(const uint32_t i)
{
    return i % 2u ? white : orange;
}

static void scene(void)
{
    tide_draw_reset(&list);
    tide_draw_clear(&list, background);
    tide_draw_rect(&list, at(128, 28), tide_f2(16, 16), yellow); // Cleared by the next one
    tide_draw_clear(&list, background);

    // A band of pixels, one rect each, along the bottom
    for (uint32_t i = 0; i < BAND; i++) {
        const float x = (float)(i % WIDTH) + 0.5f;
        const float y = (float)(BAND_TOP + i / WIDTH) + 0.5f;
        tide_draw_rect(&list, at(x, y), tide_f2(1, 1), band_color(i));
    }

    tide_draw_rect(&list, at(48, 48), tide_f2(20, 20), red);
    tide_draw_rect(&list, at(53, 48), tide_f2(10, 20), blue); // Over the red one's right half
    tide_draw_circle(&list, at(208, 48), 15, green);

    // Text between shapes: over the orange rect, and under the magenta one
    tide_draw_rect(&list, at(128, 100), tide_f2(60, 30), orange);
    tide_draw_text(&list, "MMMM", at(102, 88), 24, white);
    tide_draw_rect(&list, at(113, 100), tide_f2(30, 30), magenta);

    tide_draw_line(&list, at(28, 127.5f), at(228, 127.5f), cyan);
    tide_draw_wire_rect(&list, at(48, 160), tide_f2(40, 40), white);
    tide_draw_wire_circle(&list, at(208, 160), 20, yellow);

    // Another camera: 2 pixels a world unit
    tide_draw_camera(&list, tide_f2(1000, 1000), screen.y / 4.0f);
    tide_draw_rect(&list, tide_f2(1040, 970), tide_f2(4, 4), purple);

    // The GUI's pixels: from the top left, y down
    tide_draw_screen(&list);
    tide_draw_rect(&list, tide_f2(16, 250), tide_f2(10, 10), green);
}

typedef struct check {
    float x, y;
    tide_color color;
    const char *what;
} check;

static void expect(const bool ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) failures++;
}

static int run_checks(void)
{
    scene();
    const uint32_t last = BAND - 1u;
    const check checks[] = {
        {128, 28, background, "a clear covers what was drawn before it"},
        {last % WIDTH + 0.5f, BAND_TOP + last / WIDTH + 0.5f, band_color(last), "the band's last pixel is there"},
        {last % WIDTH - 0.5f, BAND_TOP + last / WIDTH + 0.5f, band_color(last - 1u), "and the one before it"},
        {last % WIDTH + 1.5f, BAND_TOP + last / WIDTH + 0.5f, background, "and nothing after it"},
        {42, 48, red, "a rect"},
        {54, 48, blue, "a rect over another"},
        {208, 48, green, "a circle's middle"},
        {208, 68, background, "and outside it"},
        {128, 127.5f, cyan, "a line"},
        {128, 125.5f, background, "and beside it"},
        {28.5f, 160, white, "a wire rect's edge"},
        {48, 160, background, "and its middle"},
        {208.5f, 140.5f, yellow, "a wire circle's edge"},
        {208, 160, background, "and its middle"},
        {screen.x * 0.5f + 80, screen.y * 0.5f + 60, purple, "a rect through another camera"},
        {16, 250, green, "a rect in the GUI's pixels"},
    };
    enum { CHECKS = sizeof checks / sizeof checks[0] };
    // The text test's region: the magenta rect, and the orange one's right half
    enum { TEXT_W = 28, TEXT_H = 28, POINTS = CHECKS + 2 * TEXT_W * TEXT_H };
    static tide_float2 points[POINTS];
    static uint32_t got[POINTS];
    for (int i = 0; i < CHECKS; i++) points[i] = tide_f2(checks[i].x, checks[i].y);
    for (int y = 0; y < TEXT_H; y++) {
        for (int x = 0; x < TEXT_W; x++) {
            points[CHECKS + y * TEXT_W + x] = tide_f2(99.5f + (float)x, 86.5f + (float)y);
            points[CHECKS + TEXT_W * TEXT_H + y * TEXT_W + x] = tide_f2(129.5f + (float)x, 86.5f + (float)y);
        }
    }
    tide_platform_read_pixels(&list, points, POINTS, got);

    for (int i = 0; i < CHECKS; i++) {
        const bool ok = near(got[i], rgba(checks[i].color), 8);
        printf("%s: %s (0x%08X, expected 0x%08X)\n", ok ? "ok" : "FAIL", checks[i].what, (unsigned)got[i],
               (unsigned)rgba(checks[i].color));
        if (!ok) failures++;
    }
    int covered = 0;
    int text = 0;
    for (int i = 0; i < TEXT_W * TEXT_H; i++) {
        if (near(got[CHECKS + i], rgba(magenta), 0)) covered++;
        if (!near(got[CHECKS + TEXT_W * TEXT_H + i], rgba(orange), 0)) text++;
    }
    expect(covered == TEXT_W * TEXT_H, "a rect drawn after text covers it");
    expect(text > 20, "text drawn after a rect shows over it");
    return failures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
// Meshes: triangles with a color at each corner, textures and the clip

// A quad from `top_left` to `bottom_right`, as the list's units have them:
// its left corners `left` and its right ones `right`, and the texture's first
// pixel at its top left.
static void quad(const tide_float2 top_left, const tide_float2 bottom_right, const tide_color left, const tide_color right,
                 const tide_texture *texture, const tide_filter filter, const bool clockwise)
{
    const tide_vertex vertices[4] = {
        {top_left, {0.0f, 0.0f}, left},
        {{bottom_right.x, top_left.y}, {1.0f, 0.0f}, right},
        {bottom_right, {1.0f, 1.0f}, right},
        {{top_left.x, bottom_right.y}, {0.0f, 1.0f}, left},
    };
    static const uint32_t one_way[6] = {0, 1, 2, 0, 2, 3}, other_way[6] = {2, 1, 0, 3, 2, 0};
    tide_draw_mesh(&list, vertices, 4, clockwise ? one_way : other_way, 6, texture, filter);
}

// Red, green, blue and white, two by two, and yellow beside a clear pixel
static uint8_t four[2 * 2 * 4] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
static const uint8_t two[2 * 1 * 4] = {255, 255, 0, 255, 0, 0, 0, 0};
static tide_texture four_texture = {four, 2, 2, 1};
static const tide_texture two_texture = {two, 2, 1, 1};

static void mesh_scene(void)
{
    const tide_color gray = {0.5f, 1.0f, 1.0f, 1.0f};
    tide_draw_reset(&list);
    tide_draw_clear(&list, background);

    // Colors blend from corner to corner
    quad(at(16, 16), at(80, 80), red, blue, NULL, TIDE_FILTER_BILINEAR, true);
    // A texture, pixel by pixel and blended, and times its corners' colors
    quad(at(96, 16), at(160, 80), white, white, &four_texture, TIDE_FILTER_POINT, true);
    quad(at(176, 16), at(240, 80), white, white, &four_texture, TIDE_FILTER_BILINEAR, false);
    quad(at(16, 96), at(80, 160), gray, gray, &four_texture, TIDE_FILTER_POINT, true);
    quad(at(96, 96), at(160, 128), white, white, &two_texture, TIDE_FILTER_POINT, true);

    // A clip, in the world's units: what's drawn after it only shows inside
    // it, a clear and shapes too
    const tide_float2 corner = at(176, 136);
    tide_draw_clip(&list, (tide_rect){corner.x, corner.y, 40, 40});
    tide_draw_clear(&list, cyan);
    quad(at(186, 106), at(206, 126), magenta, magenta, NULL, TIDE_FILTER_BILINEAR, true);
    tide_draw_rect(&list, at(226, 131), tide_f2(40, 6), orange);
    tide_draw_no_clip(&list);
    tide_draw_rect(&list, at(236, 150), tide_f2(10, 10), green);

    // In the order they were recorded, among shapes
    tide_draw_rect(&list, at(48, 240), tide_f2(64, 32), yellow);
    quad(at(16, 224), at(48, 256), blue, blue, NULL, TIDE_FILTER_BILINEAR, false);
    tide_draw_rect(&list, at(24, 240), tide_f2(16, 32), red);

    // On the screen: pixels from the top left, y down, the clip too
    tide_draw_screen(&list);
    tide_draw_clip(&list, (tide_rect){16, 176, 48, 32});
    quad(tide_f2(0, 170), tide_f2(100, 220), purple, purple, NULL, TIDE_FILTER_BILINEAR, true);
    tide_draw_no_clip(&list);
    quad(tide_f2(80, 176), tide_f2(112, 208), white, white, &four_texture, TIDE_FILTER_POINT, true);
    // ...and through a camera again
    tide_draw_camera(&list, tide_f2(0, 0), screen.y / 2.0f);
    quad(at(120, 176), at(136, 192), white, white, NULL, TIDE_FILTER_BILINEAR, true);
}

static void check_pixels(const check *checks, const int count)
{
    static tide_float2 points[64];
    static uint32_t got[64];
    for (int i = 0; i < count; i++) points[i] = tide_f2(checks[i].x, checks[i].y);
    tide_platform_read_pixels(&list, points, count, got);
    for (int i = 0; i < count; i++) {
        const bool ok = near(got[i], rgba(checks[i].color), 8);
        printf("%s: %s (0x%08X, expected 0x%08X)\n", ok ? "ok" : "FAIL", checks[i].what, (unsigned)got[i],
               (unsigned)rgba(checks[i].color));
        if (!ok) failures++;
    }
}

static void meshes(void)
{
    const tide_color dim = {0.5f, 0.0f, 0.0f, 1.0f}, pale = {0.5f, 1.0f, 1.0f, 1.0f};
    const tide_color between = {0.5f, 0.0f, 0.5f, 1.0f}, blend = {0.5f, 0.5f, 0.5f, 1.0f};
    mesh_scene();
    const check checks[] = {
        {17, 48, red, "a mesh's left corners' color"},
        {79, 48, blue, "and its right ones'"},
        {48, 48, between, "blended between them"},
        {112, 32, red, "a texture's first pixel, at uv (0, 0)"},
        {144, 32, green, "the one beside it"},
        {112, 64, blue, "the one below it"},
        {144, 64, white, "and its last"},
        {208, 48, blend, "a texture's pixels blended, between them"},
        {178, 18, red, "and not with the far edge's, at its edge"},
        {32, 112, dim, "a texture's pixel times its corners' color"},
        {64, 144, pale, "and another"},
        {112, 112, yellow, "another texture, wider than it's tall"},
        {144, 112, background, "and its clear pixel"},
        {196, 116, magenta, "a mesh inside the clip"},
        {180, 100, cyan, "a clear fills the clip"},
        {170, 116, background, "and nothing outside it"},
        {215, 100, cyan, "the clip's last column"},
        {216, 100, background, "and the one past it"},
        {180, 96, cyan, "the clip's first row"},
        {180, 95, background, "and the one before it"},
        {180, 135, cyan, "the clip's last row"},
        {180, 136, background, "and the one past it"},
        {210, 131, orange, "a rect inside the clip"},
        {230, 131, background, "and the rest of it, outside"},
        {236, 150, green, "a rect after the clip is off"},
        {24, 240, red, "a rect over a mesh"},
        {40, 240, blue, "a mesh over a rect"},
        {64, 240, yellow, "and the rect under it"},
        {40, 192, purple, "a mesh on the screen, inside the screen's clip"},
        {70, 192, background, "and nothing beside the clip"},
        {40, 212, background, "or below it"},
        {88, 184, red, "a texture on the screen: its first pixel at the top left"},
        {104, 200, white, "and its last at the bottom right"},
        {128, 184, white, "a mesh through a camera again"},
    };
    check_pixels(checks, (int)(sizeof checks / sizeof checks[0]));

    // Pixels that changed, at another version
    four[0] = 0, four[1] = 255, four[2] = 255;
    four_texture.version = 2;
    mesh_scene();
    const check changed[] = {
        {112, 32, cyan, "a texture's pixel that changed"},
        {144, 32, green, "and one that didn't"},
    };
    check_pixels(changed, 2);

    // ...and of another size
    four_texture.width = four_texture.height = 1;
    four_texture.version = 3;
    mesh_scene();
    const check resized[] = {{144, 64, cyan, "a texture that changed size"}};
    check_pixels(resized, 1);

    // A frame without it lets it go, and the next draws with it again
    tide_draw_reset(&list);
    tide_draw_clear(&list, background);
    const check none[] = {{144, 64, background, "a frame without meshes"}};
    check_pixels(none, 1);
    four[0] = 255, four[1] = 0, four[2] = 0;
    four_texture = (tide_texture){four, 2, 2, 4};
    mesh_scene();
    const check again[] = {
        {112, 32, red, "a texture drawn again after a frame without it"},
        {144, 64, white, "all of it"},
    };
    check_pixels(again, 2);

    // A host whose game was swapped for another build has the list forget its
    // textures: the new build's pixels show, though it names them as the old
    // one named its own
    four[0] = 255, four[1] = 255, four[2] = 0;
    tide_draw_forget(&list);
    mesh_scene();
    const check forgotten[] = {{112, 32, yellow, "a texture after the list forgot it"}};
    check_pixels(forgotten, 1);
}

// ---------------------------------------------------------------------------
// 3D meshes: cameras, depth, instances and textures

// The pixels a unit takes 10 away from mesh_3d_scene's camera, where most of
// its meshes are: half the screen's height over 10, as a camera seeing 90
// degrees up and down has it, or fewer on a screen too narrow for the scene
// at that (a phone's, upright), which is 14.5 units across there.
static float unit_3d(void)
{
    const float by_height = screen.y * 0.5f / 10.0f, by_width = screen.x * 0.5f / 7.25f;
    return by_height < by_width ? by_height : by_width;
}

// ...and the field of view that gives it: 90 degrees, or more on such a screen.
static float field_of_view_3d(void)
{
    const float unit = unit_3d();
    if (unit == screen.y * 0.5f / 10.0f) return 90.0f;
    return 2.0f * atanf(screen.y * 0.5f / (10.0f * unit)) * 180.0f / TIDE_PI_F;
}

// Where a 3D point lands in the window, through the camera mesh_3d_scene
// starts with: at (0, 0, -10) looking along +z, so that at a distance of d,
// a unit is 10 of unit_3d over d pixels, across as up and down.
static tide_float2 seen(const float x, const float y, const float z)
{
    const float pixels = unit_3d() * 10.0f / (z + 10.0f);
    return tide_f2(screen.x * 0.5f + x * pixels, screen.y * 0.5f - y * pixels);
}

// Red, green, blue and white, two by two
static const uint8_t quarters[2 * 2 * 4] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
static const tide_texture quarters_texture = {quarters, 2, 2, 1};

// A square `size` across around `center`, facing along -z turned by
// `rotation`, with the texture's first pixel at its top left.
static void square_3d(const tide_float3 center, const tide_quaternion rotation, const float size, const tide_color color,
                      const tide_texture *texture)
{
    const tide_vertex3 corners[4] = {
        {{-0.5f, 0.5f, 0.0f}, {0.0f, 0.0f}, color},
        {{0.5f, 0.5f, 0.0f}, {1.0f, 0.0f}, color},
        {{0.5f, -0.5f, 0.0f}, {1.0f, 1.0f}, color},
        {{-0.5f, -0.5f, 0.0f}, {0.0f, 1.0f}, color},
    };
    static const uint32_t indices[6] = {0, 1, 2, 0, 2, 3};
    tide_mesh mesh = {corners, 4, indices, 6, 0};
    mesh.version = tide_mesh_version(&mesh);
    tide_draw_mesh_3d(&list, &mesh, tide_trs_f4x4(center, rotation, tide_f3_splat(size)), texture, TIDE_FILTER_POINT);
}

static void mesh_3d_scene(void)
{
    const tide_quaternion ahead = tide_identity_q();
    tide_draw_reset(&list);
    tide_draw_clear(&list, background);
    tide_draw_camera_3d(&list, tide_f3(0, 0, -10), ahead, field_of_view_3d());
    // A clear clears depth too: nothing behind this hides after it
    square_3d(tide_f3(0, 0, -5), ahead, 20.0f, yellow, NULL);
    tide_draw_clear(&list, background);

    // The nearest in front, drawn first or last; the far one a third smaller
    square_3d(tide_f3(0, 6, 0), ahead, 2.0f, red, NULL);
    square_3d(tide_f3(0, 9, 5), ahead, 6.0f, blue, NULL);
    square_3d(tide_f3(-6, 3, 5), ahead, 6.0f, yellow, NULL);
    square_3d(tide_f3(-4, 2, 0), ahead, 2.0f, green, NULL);

    // A texture, its first pixel at the top left
    square_3d(tide_f3(4, 2, 0), ahead, 2.0f, white, &quarters_texture);

    // Instances of one mesh, one after another: over a rect drawn before
    // them, and under one drawn after, as shapes never test depth
    const tide_float2 under = seen(4, -2, 0), over = seen(0, -2, 0);
    tide_draw_rect(&list, at(under.x, under.y), tide_f2(10, 10), magenta);
    for (int i = -1; i <= 1; i++) square_3d(tide_f3(4.0f * (float)i, -2, 0), ahead, 2.0f, orange, NULL);
    tide_draw_rect(&list, at(over.x, over.y), tide_f2(6, 6), cyan);

    // A mesh too big for indices of 16 bits, drawn with its last corners
    static tide_vertex3 many[70000];
    static const uint32_t last_corners[6] = {69996, 69997, 69998, 69996, 69998, 69999};
    static const tide_float3 square[4] = {{-5, -3, 0}, {-3, -3, 0}, {-3, -5, 0}, {-5, -5, 0}};
    for (int i = 0; i < 4; i++) many[69996 + i] = (tide_vertex3){square[i], {0, 0}, yellow};
    const tide_mesh big = {many, 70000, last_corners, 6, 1};
    tide_draw_mesh_3d(&list, &big, tide_identity_f4x4(), NULL, TIDE_FILTER_POINT);

    // Turned a quarter around y, the camera looks along +x, its right along -z
    const tide_quaternion turned = tide_axisangle_q(tide_f3(0, 1, 0), TIDE_PI_F * 0.5f);
    tide_draw_camera_3d(&list, tide_f3(0, 0, -10), turned, field_of_view_3d());
    square_3d(tide_f3(10, -4, -14), turned, 2.0f, purple, NULL);

    // Any projection: an orthographic one, 10 pixels a unit
    tide_draw_camera_matrices(&list, tide_translate_f4x4(tide_f3(100, 0, 0)),
                              tide_ortho_f4x4(screen.x / 10.0f, screen.y / 10.0f, 0.1f, 100.0f));
    square_3d(tide_f3(100, -12, 5), ahead, 2.0f, green, NULL);
}

static void meshes_3d(void)
{
    const tide_float2 near_red = seen(0, 6, 0), near_green = seen(-4, 2, 0), textured = seen(4, 2, 0);
    const tide_float2 left = seen(-4, -2, 0), middle = seen(0, -2, 0), right = seen(4, -2, 0), turned = seen(4, -4, 0);
    const tide_float2 big = seen(-4, -4, 0);
    const float edge = unit_3d() + 4.0f;   // Past a near square's edge, inside a far one's
    const float quarter = unit_3d() * 0.5f; // A quarter of a near square across
    const check checks[] = {
        {near_red.x, near_red.y, red, "a near 3D mesh in front of a far one drawn after it"},
        {near_red.x + edge, near_red.y, blue, "and the far one around it, smaller for being farther"},
        {near_green.x, near_green.y, green, "a near 3D mesh drawn after a far one"},
        {near_green.x + edge, near_green.y, yellow, "and the far one around it"},
        {textured.x - quarter, textured.y - quarter, red, "a 3D mesh's texture: its first pixel at the top left"},
        {textured.x + quarter, textured.y - quarter, green, "the one beside it"},
        {textured.x - quarter, textured.y + quarter, blue, "the one below it"},
        {textured.x + quarter, textured.y + quarter, white, "and its last"},
        {left.x, left.y, orange, "the first instance of a mesh"},
        {right.x, right.y, orange, "its last, over a rect drawn before it"},
        {middle.x, middle.y, cyan, "a rect drawn after them, over the one in the middle"},
        {middle.x + 12.0f, middle.y, orange, "and the rest of it"},
        {(left.x + middle.x) * 0.5f, middle.y, background, "and nothing between them"},
        {big.x, big.y, yellow, "a mesh with more corners than 16-bit indices reach"},
        {turned.x, turned.y, purple, "a mesh to the right of a turned camera"},
        {screen.x * 0.5f, screen.y * 0.5f + 120.0f, green, "a mesh through an orthographic camera"},
        {screen.x * 0.5f + 15.0f, screen.y * 0.5f + 120.0f, background, "and its size there"},
    };
    // Twice: the second frame draws with what the first uploaded
    for (int frame = 0; frame < 2; frame++) {
        mesh_3d_scene();
        check_pixels(checks, (int)(sizeof checks / sizeof checks[0]));
    }
}

// Primitives use the same cached meshes and depth path. An orthographic
// camera makes one world unit ten pixels, so their sizes and silhouettes
// can be checked independently of the window's shape.
static tide_float3 primitive_at(const float x, const float y, const float z)
{
    const tide_float2 p = at(x, y);
    return tide_f3(p.x / 10.0f, p.y / 10.0f, z);
}

static void primitive_scene(void)
{
    tide_draw_reset(&list);
    tide_draw_clear(&list, background);
    tide_draw_camera_matrices(&list, tide_identity_f4x4(),
                              tide_ortho_f4x4(screen.x / 10.0f, screen.y / 10.0f, 0.3f, 100.0f));
    tide_draw_cube(&list, primitive_at(40, 40, 10), tide_f3(4, 4, 4), red);
    tide_draw_sphere(&list, primitive_at(110, 40, 10), 2, blue);
    tide_draw_cylinder(&list, primitive_at(180, 40, 10), 2, 4, green);

    const tide_quaternion turn_x = tide_axisangle_q(tide_f3(1, 0, 0), TIDE_PI_F * 0.5f);
    const tide_quaternion turn_z = tide_axisangle_q(tide_f3(0, 0, 1), TIDE_PI_F * 0.5f);
    tide_draw_plane_rotated(&list, primitive_at(40, 110, 10), tide_f2(4, 6), turn_x, yellow);
    tide_draw_cube_rotated(&list, primitive_at(110, 110, 10), tide_f3(2, 4, 2), turn_z, cyan);
    tide_draw_cylinder_rotated(&list, primitive_at(180, 110, 10), 2, 4, turn_x, magenta);

    // Near first, then near last: primitive order never decides depth.
    tide_draw_sphere(&list, primitive_at(40, 190, 8), 2, red);
    tide_draw_cube(&list, primitive_at(40, 190, 12), tide_f3(6, 6, 2), blue);
    tide_draw_sphere(&list, primitive_at(110, 190, 12), 3, orange);
    tide_draw_cube(&list, primitive_at(110, 190, 8), tide_f3(2, 2, 2), green);

    tide_draw_sphere(&list, primitive_at(180, 180, 10), 1, orange);
    tide_draw_sphere(&list, primitive_at(180, 230, 10), 1, orange);
}

static void primitives(void)
{
    const check checks[] = {
        {40, 40, red, "a cube's center"},
        {58, 58, red, "a cube's full width and height"},
        {62, 40, background, "outside a cube"},
        {110, 40, blue, "a sphere's center"},
        {128, 40, blue, "a sphere's radius"},
        {132, 40, background, "outside a sphere"},
        {126, 56, background, "a sphere's round silhouette"},
        {180, 40, green, "an upright cylinder"},
        {198, 58, green, "a cylinder's radius and full height"},
        {202, 40, background, "outside a cylinder"},
        {40, 110, yellow, "a rotated plane"},
        {58, 138, yellow, "a plane's width and depth rotated onto the screen"},
        {62, 110, background, "outside a plane"},
        {128, 110, cyan, "a rotated cube's long side"},
        {110, 118, cyan, "a rotated cube's short side"},
        {110, 122, background, "outside a rotated cube's short side"},
        {180, 110, magenta, "a rotated cylinder's cap"},
        {198, 110, magenta, "a cylinder's cap radius"},
        {196, 126, background, "a cylinder's round cap"},
        {40, 190, red, "a near sphere in front of a cube drawn after it"},
        {66, 190, blue, "the far cube around a sphere"},
        {110, 190, green, "a near cube in front of a sphere drawn before it"},
        {132, 190, orange, "the far sphere around a cube"},
        {180, 180, orange, "the first sphere instance"},
        {180, 230, orange, "the next sphere instance"},
    };
    for (int frame = 0; frame < 2; frame++) {
        primitive_scene();
        check_pixels(checks, (int)(sizeof checks / sizeof checks[0]));
    }
}

// ---------------------------------------------------------------------------
// Shaders of the game's: meshes of any vertices, drawn through GLSL and WGSL
// as tidec writes them from Tide's (see tide_shader), here written by hand

#ifdef __wasm__
#define WGSL(source) source
#else
#define WGSL(source) NULL // Only the web's
#endif

// What every shader's uniforms start with, before its own.
#define GLSL_UNIFORMS(own) "layout(std140) uniform uniforms { mat4 tide_camera; vec4 tide_screen; " own "};\n"
#define WGSL_UNIFORMS(own)                                                                                             \
    "struct Uniforms { tide_camera: mat4x4f, tide_screen: vec4f, " own "}\n"                                           \
    "@group(0) @binding(0) var<uniform> u: Uniforms;\n"

// A pixel in the color its corners hand it.
#define GLSL_TINT_FRAGMENT                                                                                             \
    "in vec4 tint;\n"                                                                                                  \
    "out vec4 pixel;\n"                                                                                                \
    "void main() {\n"                                                                                                  \
    "    pixel = tint;\n"                                                                                              \
    "}\n"
#define WGSL_TINT_CORNER                                                                                               \
    "struct Corner {\n"                                                                                                \
    "    @builtin(position) position: vec4f,\n"                                                                        \
    "    @location(0) tint: vec4f,\n"                                                                                  \
    "}\n"
#define WGSL_TINT_FRAGMENT                                                                                             \
    "@fragment fn fragment(in: Corner) -> @location(0) vec4f {\n"                                                      \
    "    return in.tint;\n"                                                                                            \
    "}\n"

// In 3D: a position and a normal, an instance's transform and color, with
// depth. Facing the camera, a corner is all of its instance's color.
typedef struct lit_vertex {
    float position[3];
    float normal[3];
} lit_vertex;

typedef struct lit_instance {
    tide_float4x4 transform;
    tide_color color;
} lit_instance;

static const tide_shader lit_shader = {
    .name = "lit",
    .glsl_vertex = "layout(location = 0) in vec3 position;\n"
                   "layout(location = 1) in vec3 normal;\n"
                   "layout(location = 2) in vec4 model0;\n"
                   "layout(location = 3) in vec4 model1;\n"
                   "layout(location = 4) in vec4 model2;\n"
                   "layout(location = 5) in vec4 model3;\n"
                   "layout(location = 6) in vec4 color;\n"
                   GLSL_UNIFORMS("")
                   "out vec4 tint;\n"
                   "void main() {\n"
                   "    tint = vec4(color.rgb * -normal.z, color.a);\n"
                   "    gl_Position = tide_camera * (mat4(model0, model1, model2, model3) * vec4(position, 1.0));\n"
                   "}\n",
    .glsl_fragment = GLSL_TINT_FRAGMENT,
    .wgsl = WGSL(WGSL_UNIFORMS("") WGSL_TINT_CORNER
        "@vertex fn vertex(@location(0) position: vec3f, @location(1) normal: vec3f, @location(2) model0: vec4f,\n"
        "                  @location(3) model1: vec4f, @location(4) model2: vec4f, @location(5) model3: vec4f,\n"
        "                  @location(6) color: vec4f) -> Corner {\n"
        "    let p = u.tide_camera * (mat4x4f(model0, model1, model2, model3) * vec4f(position, 1.0));\n"
        "    var out: Corner;\n"
        "    out.tint = vec4f(color.rgb * -normal.z, color.a);\n"
        "    out.position = vec4f(p.xy, (p.z + p.w) * 0.5, p.w);\n" // The camera's clip space is OpenGL's
        "    return out;\n"
        "}\n"
        WGSL_TINT_FRAGMENT),
    .id = 0x7E570001,
    .dims = 3,
    .vertex_size = sizeof(lit_vertex),
    .attribute_count = 2,
    .attributes = {{TIDE_SHADER_FLOAT3, offsetof(lit_vertex, position)},
                   {TIDE_SHADER_FLOAT3, offsetof(lit_vertex, normal)}},
    .instance_floats = 5,
};

// In 2D: a position, in its instance's color.
static const tide_shader flat_shader = {
    .name = "flat",
    .glsl_vertex = "layout(location = 0) in vec2 position;\n"
                   "layout(location = 1) in vec4 color;\n"
                   GLSL_UNIFORMS("")
                   "out vec4 tint;\n"
                   "void main() {\n"
                   "    tint = color;\n"
                   "    gl_Position = tide_camera * vec4(position, 0.0, 1.0);\n"
                   "}\n",
    .glsl_fragment = GLSL_TINT_FRAGMENT,
    .wgsl = WGSL(WGSL_UNIFORMS("") WGSL_TINT_CORNER
        "@vertex fn vertex(@location(0) position: vec2f, @location(1) color: vec4f) -> Corner {\n"
        "    let p = u.tide_camera * vec4f(position, 0.0, 1.0);\n"
        "    var out: Corner;\n"
        "    out.tint = color;\n"
        "    out.position = vec4f(p.xy, (p.z + p.w) * 0.5, p.w);\n"
        "    return out;\n"
        "}\n"
        WGSL_TINT_FRAGMENT),
    .id = 0x7E570002,
    .dims = 2,
    .vertex_size = 2 * sizeof(float),
    .attribute_count = 1,
    .attributes = {{TIDE_SHADER_FLOAT2, 0}},
    .instance_floats = 1,
};

// Uniforms of its own: a color, and ints that say what to make of it.
typedef struct picked_uniforms {
    tide_color tint;
    int32_t pick[4]; // x: 1 turns its channels around; w: -3 turns it into what's left of white
} picked_uniforms;

#define PICKED_GLSL_UNIFORMS GLSL_UNIFORMS("vec4 tint; ivec4 pick; ")

static const tide_shader picked_shader = {
    .name = "picked",
    .glsl_vertex = "layout(location = 0) in vec2 position;\n"
                   PICKED_GLSL_UNIFORMS
                   "void main() {\n"
                   "    gl_Position = tide_camera * vec4(position, 0.0, 1.0);\n"
                   "}\n",
    .glsl_fragment = PICKED_GLSL_UNIFORMS
                     "out vec4 pixel;\n"
                     "void main() {\n"
                     "    vec4 c = tint;\n"
                     "    if (pick.x == 1) c = c.gbra;\n"
                     "    if (pick.w == -3) c.rgb = 1.0 - c.rgb;\n"
                     "    pixel = c;\n"
                     "}\n",
    .wgsl = WGSL(WGSL_UNIFORMS("tint: vec4f, pick: vec4i ")
        "@vertex fn vertex(@location(0) position: vec2f) -> @builtin(position) vec4f {\n"
        "    let p = u.tide_camera * vec4f(position, 0.0, 1.0);\n"
        "    return vec4f(p.xy, (p.z + p.w) * 0.5, p.w);\n"
        "}\n"
        "@fragment fn fragment() -> @location(0) vec4f {\n"
        "    var c = u.tint;\n"
        "    if (u.pick.x == 1) { c = c.gbra; }\n"
        "    if (u.pick.w == -3) { c = vec4f(1.0 - c.rgb, c.a); }\n"
        "    return c;\n"
        "}\n"),
    .id = 0x7E570003,
    .dims = 2,
    .vertex_size = 2 * sizeof(float),
    .attribute_count = 1,
    .attributes = {{TIDE_SHADER_FLOAT2, 0}},
    .uniform_floats = 1,
    .uniform_ints = 1,
};

// As many uniforms of its own as a shader takes: colors, and after them the
// int that says which is the pixel's.
typedef struct many_uniforms {
    tide_color colors[249];
    int32_t pick[4];
} many_uniforms;

_Static_assert(sizeof(many_uniforms) == TIDE_SHADER_UNIFORM_BYTES, "all the uniforms a shader takes");

#define MANY_GLSL_UNIFORMS GLSL_UNIFORMS("vec4 colors[249]; ivec4 pick; ")

static const tide_shader many_shader = {
    .name = "many",
    .glsl_vertex = "layout(location = 0) in vec2 position;\n"
                   MANY_GLSL_UNIFORMS
                   "void main() {\n"
                   "    gl_Position = tide_camera * vec4(position, 0.0, 1.0);\n"
                   "}\n",
    .glsl_fragment = MANY_GLSL_UNIFORMS
                     "out vec4 pixel;\n"
                     "void main() {\n"
                     "    pixel = colors[pick.x];\n"
                     "}\n",
    .wgsl = WGSL(WGSL_UNIFORMS("colors: array<vec4f, 249>, pick: vec4i ")
        "@vertex fn vertex(@location(0) position: vec2f) -> @builtin(position) vec4f {\n"
        "    let p = u.tide_camera * vec4f(position, 0.0, 1.0);\n"
        "    return vec4f(p.xy, (p.z + p.w) * 0.5, p.w);\n"
        "}\n"
        "@fragment fn fragment() -> @location(0) vec4f {\n"
        "    return u.colors[u.pick.x];\n"
        "}\n"),
    .id = 0x7E570008,
    .dims = 2,
    .vertex_size = 2 * sizeof(float),
    .attribute_count = 1,
    .attributes = {{TIDE_SHADER_FLOAT2, 0}},
    .uniform_floats = 249,
    .uniform_ints = 1,
};

// Ints, in a vertex and in an instance: the vertex's is its color, 8 bits a
// channel, of which the instance says which count and which to turn around,
// and how many units to move by.
typedef struct counted_vertex {
    float position[2];
    int32_t bits;
} counted_vertex;

typedef struct counted_instance {
    float shift[4];
    int32_t mask[4]; // x: the bits that count; y, z: units to move by; w: the bits to turn around
} counted_instance;

static const tide_shader counted_shader = {
    .name = "counted",
    .glsl_vertex = "layout(location = 0) in vec2 position;\n"
                   "layout(location = 1) in int bits;\n"
                   "layout(location = 2) in vec4 shift;\n"
                   "layout(location = 3) in ivec4 mask;\n"
                   GLSL_UNIFORMS("")
                   "out vec4 tint;\n"
                   "void main() {\n"
                   "    int c = (bits & mask.x) ^ mask.w;\n"
                   "    tint = vec4(float((c >> 16) & 255), float((c >> 8) & 255), float(c & 255), 255.0) / 255.0;\n"
                   "    gl_Position = tide_camera * vec4(position + shift.xy + vec2(mask.yz), 0.0, 1.0);\n"
                   "}\n",
    .glsl_fragment = GLSL_TINT_FRAGMENT,
    .wgsl = WGSL(WGSL_UNIFORMS("") WGSL_TINT_CORNER
        "@vertex fn vertex(@location(0) position: vec2f, @location(1) bits: i32, @location(2) shift: vec4f,\n"
        "                  @location(3) mask: vec4i) -> Corner {\n"
        "    let c = (bits & mask.x) ^ mask.w;\n"
        "    let p = u.tide_camera * vec4f(position + shift.xy + vec2f(mask.yz), 0.0, 1.0);\n"
        "    var out: Corner;\n"
        "    out.tint = vec4f(f32((c >> 16u) & 255), f32((c >> 8u) & 255), f32(c & 255), 255.0) / 255.0;\n"
        "    out.position = vec4f(p.xy, (p.z + p.w) * 0.5, p.w);\n"
        "    return out;\n"
        "}\n"
        WGSL_TINT_FRAGMENT),
    .id = 0x7E570004,
    .dims = 2,
    .vertex_size = sizeof(counted_vertex),
    .attribute_count = 2,
    .attributes = {{TIDE_SHADER_FLOAT2, offsetof(counted_vertex, position)},
                   {TIDE_SHADER_INT, offsetof(counted_vertex, bits)}},
    .instance_floats = 1,
    .instance_ints = 1,
};

// Two textures: the first across its left half, the second across its right.
typedef struct uv_vertex {
    float position[2];
    float uv[2];
} uv_vertex;

static const tide_shader halves_shader = {
    .name = "halves",
    .glsl_vertex = "layout(location = 0) in vec2 position;\n"
                   "layout(location = 1) in vec2 uv;\n"
                   GLSL_UNIFORMS("")
                   "out vec2 at;\n"
                   "void main() {\n"
                   "    at = uv;\n"
                   "    gl_Position = tide_camera * vec4(position, 0.0, 1.0);\n"
                   "}\n",
    .glsl_fragment = "in vec2 at;\n"
                     "uniform sampler2D pixels0;\n"
                     "uniform sampler2D pixels1;\n"
                     "out vec4 pixel;\n"
                     "void main() {\n"
                     "    vec4 left = texture(pixels0, vec2(at.x * 2.0, at.y));\n"
                     "    vec4 right = texture(pixels1, vec2(at.x * 2.0 - 1.0, at.y));\n"
                     "    pixel = at.x < 0.5 ? left : right;\n"
                     "}\n",
    .wgsl = WGSL(WGSL_UNIFORMS("")
        "@group(0) @binding(1) var pixels0: texture_2d<f32>;\n"
        "@group(0) @binding(2) var how0: sampler;\n"
        "@group(0) @binding(3) var pixels1: texture_2d<f32>;\n"
        "@group(0) @binding(4) var how1: sampler;\n"
        "struct Corner {\n"
        "    @builtin(position) position: vec4f,\n"
        "    @location(0) at: vec2f,\n"
        "}\n"
        "@vertex fn vertex(@location(0) position: vec2f, @location(1) uv: vec2f) -> Corner {\n"
        "    let p = u.tide_camera * vec4f(position, 0.0, 1.0);\n"
        "    var out: Corner;\n"
        "    out.at = uv;\n"
        "    out.position = vec4f(p.xy, (p.z + p.w) * 0.5, p.w);\n"
        "    return out;\n"
        "}\n"
        "@fragment fn fragment(in: Corner) -> @location(0) vec4f {\n"
        "    let left = textureSample(pixels0, how0, vec2f(in.at.x * 2.0, in.at.y));\n"
        "    let right = textureSample(pixels1, how1, vec2f(in.at.x * 2.0 - 1.0, in.at.y));\n"
        "    return select(right, left, in.at.x < 0.5);\n"
        "}\n"),
    .id = 0x7E570005,
    .dims = 2,
    .vertex_size = sizeof(uv_vertex),
    .attribute_count = 2,
    .attributes = {{TIDE_SHADER_FLOAT2, offsetof(uv_vertex, position)}, {TIDE_SHADER_FLOAT2, offsetof(uv_vertex, uv)}},
    .texture_count = 2,
};

// A texture read for each corner: an instance says where it goes, then where
// in the texture its color is.
static const tide_shader sampled_shader = {
    .name = "sampled",
    .glsl_vertex = "layout(location = 0) in vec2 position;\n"
                   "layout(location = 1) in vec4 place;\n"
                   GLSL_UNIFORMS("")
                   "uniform sampler2D pixels0;\n"
                   "out vec4 tint;\n"
                   "void main() {\n"
                   "    tint = textureLod(pixels0, place.zw, 0.0);\n"
                   "    gl_Position = tide_camera * vec4(position + place.xy, 0.0, 1.0);\n"
                   "}\n",
    .glsl_fragment = GLSL_TINT_FRAGMENT,
    .wgsl = WGSL(WGSL_UNIFORMS("")
        "@group(0) @binding(1) var pixels0: texture_2d<f32>;\n"
        "@group(0) @binding(2) var how0: sampler;\n"
        WGSL_TINT_CORNER
        "@vertex fn vertex(@location(0) position: vec2f, @location(1) place: vec4f) -> Corner {\n"
        "    let p = u.tide_camera * vec4f(position + place.xy, 0.0, 1.0);\n"
        "    var out: Corner;\n"
        "    out.tint = textureSampleLevel(pixels0, how0, place.zw, 0.0);\n"
        "    out.position = vec4f(p.xy, (p.z + p.w) * 0.5, p.w);\n"
        "    return out;\n"
        "}\n"
        WGSL_TINT_FRAGMENT),
    .id = 0x7E570006,
    .dims = 2,
    .vertex_size = 2 * sizeof(float),
    .attribute_count = 1,
    .attributes = {{TIDE_SHADER_FLOAT2, 0}},
    .instance_floats = 1,
    .texture_count = 1,
};

// A pixel colored by where it is, in logical pixels from the target's top
// left, which `tide_screen` gets it from the GPU's own count: red left of 32,
// green above 256. OpenGL counts rows from the bottom.
static const tide_shader placed_shader = {
    .name = "placed",
    .glsl_vertex = "layout(location = 0) in vec2 position;\n"
                   GLSL_UNIFORMS("")
                   "void main() {\n"
                   "    gl_Position = tide_camera * vec4(position, 0.0, 1.0);\n"
                   "}\n",
    .glsl_fragment = GLSL_UNIFORMS("")
                     "out vec4 pixel;\n"
                     "void main() {\n"
                     "    vec2 p = vec2(gl_FragCoord.x, tide_screen.z - gl_FragCoord.y) / tide_screen.xy;\n"
                     "    pixel = vec4(p.x < 32.0 ? 1.0 : 0.0, p.y < 256.0 ? 1.0 : 0.0, 0.0, 1.0);\n"
                     "}\n",
    .wgsl = WGSL(WGSL_UNIFORMS("")
        "@vertex fn vertex(@location(0) position: vec2f) -> @builtin(position) vec4f {\n"
        "    let p = u.tide_camera * vec4f(position, 0.0, 1.0);\n"
        "    return vec4f(p.xy, (p.z + p.w) * 0.5, p.w);\n"
        "}\n"
        "@fragment fn fragment(@builtin(position) at: vec4f) -> @location(0) vec4f {\n"
        "    let p = at.xy / u.tide_screen.xy;\n"
        "    return vec4f(select(0.0, 1.0, p.x < 32.0), select(0.0, 1.0, p.y < 256.0), 0.0, 1.0);\n"
        "}\n"),
    .id = 0x7E570007,
    .dims = 2,
    .vertex_size = 2 * sizeof(float),
    .attribute_count = 1,
    .attributes = {{TIDE_SHADER_FLOAT2, 0}},
};

static const uint32_t quad_indices[6] = {0, 1, 2, 0, 2, 3};

// A quad of a shader's vertices, as the list keeps a mesh of Tide's: by an id
// of its own, at a version, here a hash of its bytes.
static uint32_t quad_mesh(const uint64_t id, const void *vertices, const uint32_t vertex_size)
{
    bool fill;
    const uint64_t version = tide_hash(vertices, 4u * vertex_size);
    const uint32_t mesh = tide_draw_mesh_slot(&list, TIDE_MESH_BYTES, id, 0, version, &fill);
    if (fill) tide_draw_mesh_fill_bytes(&list, mesh, vertices, vertex_size, 4, quad_indices, 6);
    return mesh;
}

// ...of positions alone, from `top_left` to `bottom_right` as the list's units have them.
static uint32_t quad_2d(const uint64_t id, const tide_float2 top_left, const tide_float2 bottom_right)
{
    const float corners[4][2] = {{top_left.x, top_left.y}, {bottom_right.x, top_left.y},
                                 {bottom_right.x, bottom_right.y}, {top_left.x, bottom_right.y}};
    return quad_mesh(id, corners, sizeof corners[0]);
}

// ...and with the texture's first pixel at its top left.
static uint32_t quad_uv(const uint64_t id, const tide_float2 top_left, const tide_float2 bottom_right)
{
    const uv_vertex corners[4] = {{{top_left.x, top_left.y}, {0, 0}}, {{bottom_right.x, top_left.y}, {1, 0}},
                                  {{bottom_right.x, bottom_right.y}, {1, 1}}, {{top_left.x, bottom_right.y}, {0, 1}}};
    return quad_mesh(id, corners, sizeof corners[0]);
}

// A texture as the list keeps it, for a shaded mesh: 1 + its place.
static uint32_t texture_slot(const tide_texture *texture)
{
    uint8_t *pixels;
    const uint32_t slot = tide_draw_texture_slot(&list, TIDE_PIXELS_MEMORY, (uint64_t)(uintptr_t)texture->pixels,
                                                 texture->version, texture->width, texture->height, &pixels);
    if (pixels) memcpy(pixels, texture->pixels, (size_t)texture->width * (size_t)texture->height * 4u);
    return slot;
}

// ...through the flat shader, in a color.
static void flat_quad(const uint64_t id, const tide_float2 top_left, const tide_float2 bottom_right,
                      const tide_color color)
{
    tide_draw_shaded(&list, &flat_shader, quad_2d(id, top_left, bottom_right), &color, NULL, NULL, 0);
}

// Yellow beside red
static const uint8_t pair[2 * 1 * 4] = {255, 255, 0, 255, 255, 0, 0, 255};
static const tide_texture pair_texture = {pair, 2, 1, 1};

// The commands that runs of instances came to, which shaded_scene counts.
static uint32_t picked_commands, counted_commands, sampled_commands;

// The window pixel (x, y) through shaded_scene's second camera: 2 pixels a unit.
static tide_float2 at_second(const float x, const float y)
{
    return tide_f2(1000.0f + (x - screen.x * 0.5f) * 0.5f, 1000.0f - (y - screen.y * 0.5f) * 0.5f);
}

static void shaded_scene(void)
{
    tide_draw_reset(&list);
    tide_draw_clear(&list, background);

    // Uniforms of the game's: each draw with its own is a command
    uint32_t before = list.count;
    static const int32_t picks[3][4] = {{0, 0, 0, 0}, {1, 0, 0, 0}, {0, 0, 0, -3}};
    for (int i = 0; i < 3; i++) {
        const float left = 16.0f + 48.0f * (float)i;
        picked_uniforms uniforms = {.tint = orange};
        memcpy(uniforms.pick, picks[i], sizeof uniforms.pick);
        const uint32_t mesh = quad_2d(1 + (uint64_t)i, at(left, 16), at(left + 32, 48));
        tide_draw_shaded(&list, &picked_shader, mesh, NULL, &uniforms, NULL, 0);
    }
    picked_commands = list.count - before;

    // As many uniforms as a shader takes, the last of them too, and each draw's its own
    static many_uniforms many = {.pick = {248}};
    many.colors[248] = cyan;
    tide_draw_shaded(&list, &many_shader, quad_2d(15, at(160, 16), at(192, 48)), NULL, &many, NULL, 0);
    many.colors[248] = magenta;
    tide_draw_shaded(&list, &many_shader, quad_2d(16, at(208, 16), at(240, 48)), NULL, &many, NULL, 0);

    // Ints in the vertices and the instances, which are one command
    const tide_float2 corner = at(16, 64), far_corner = at(48, 96);
    const counted_vertex counted[4] = {{{corner.x, corner.y}, 0xFF8000}, {{far_corner.x, corner.y}, 0xFF8000},
                                       {{far_corner.x, far_corner.y}, 0xFF8000}, {{corner.x, far_corner.y}, 0xFF8000}};
    const uint32_t counted_quad = quad_mesh(4, counted, sizeof counted[0]);
    static const counted_instance instances[3] = {
        {{0, 0, 0, 0}, {0xFFFFFF, 0, 0, 0}},     // As it is
        {{0, 0, 0, 0}, {0x00FF00, 48, 0, 0}},    // Its green alone, 48 units to the right
        {{48, 0, 0, 0}, {0xFFFFFF, 48, -8, -1}}, // Turned around, 96 to the right and 8 down
    };
    before = list.count;
    for (int i = 0; i < 3; i++) tide_draw_shaded(&list, &counted_shader, counted_quad, &instances[i], NULL, NULL, 0);
    counted_commands = list.count - before;

    // Two textures: the same one, the nearest pixel on the left and blended
    // on the right; another on the right; and none on the left, which is white
    const uint32_t quarters_slot = texture_slot(&quarters_texture), pair_slot = texture_slot(&pair_texture);
    tide_draw_shaded(&list, &halves_shader, quad_uv(5, at(16, 112), at(80, 144)), NULL, NULL,
                     (const uint32_t[TIDE_SHADER_TEXTURES]){quarters_slot, quarters_slot}, 2);
    tide_draw_shaded(&list, &halves_shader, quad_uv(6, at(96, 112), at(160, 144)), NULL, NULL,
                     (const uint32_t[TIDE_SHADER_TEXTURES]){quarters_slot, pair_slot}, 2);
    tide_draw_shaded(&list, &halves_shader, quad_uv(7, at(176, 112), at(240, 144)), NULL, NULL,
                     (const uint32_t[TIDE_SHADER_TEXTURES]){0, quarters_slot}, 0);

    // A texture read in the vertex shader: each instance its pixel
    const uint32_t sampled_quad = quad_2d(8, at(16, 160), at(32, 176));
    static const float places[3][4] = {{0, 0, 0.25f, 0.25f}, {32, 0, 0.75f, 0.25f}, {64, 0, 0.25f, 0.75f}};
    before = list.count;
    for (int i = 0; i < 3; i++) {
        tide_draw_shaded(&list, &sampled_shader, sampled_quad, places[i], NULL,
                         (const uint32_t[TIDE_SHADER_TEXTURES]){quarters_slot}, 0);
    }
    sampled_commands = list.count - before;

    // In the order they were recorded, among shapes
    tide_draw_rect(&list, at(48, 208), tide_f2(64, 32), yellow);
    flat_quad(9, at(16, 192), at(48, 224), blue);
    tide_draw_rect(&list, at(24, 208), tide_f2(16, 32), red);

    // A pixel that knows where it is
    tide_draw_shaded(&list, &placed_shader, quad_2d(10, at(16, 240), at(48, 272)), NULL, NULL, NULL, 0);

    // Where a mesh with the same corners lands, which it covers: through the
    // first camera, through another, and on the screen
    quad(at(96, 192), at(128, 224), magenta, magenta, NULL, TIDE_FILTER_BILINEAR, true);
    flat_quad(11, at(96, 192), at(128, 224), cyan);
    tide_draw_camera(&list, tide_f2(1000, 1000), screen.y / 4.0f);
    quad(at_second(144, 192), at_second(176, 224), magenta, magenta, NULL, TIDE_FILTER_BILINEAR, true);
    flat_quad(12, at_second(144, 192), at_second(176, 224), cyan);
    tide_draw_screen(&list);
    quad(tide_f2(192, 192), tide_f2(224, 224), magenta, magenta, NULL, TIDE_FILTER_BILINEAR, true);
    flat_quad(13, tide_f2(192, 192), tide_f2(224, 224), cyan);
}

static void shaded_checks(void)
{
    const tide_color dark_green = {0.0f, 0.5f, 0.0f, 1.0f}, turned = {0.0f, 0.5f, 1.0f, 1.0f};
    const tide_color black = {0.0f, 0.0f, 0.0f, 1.0f};
    // A pixel's middle is half a pixel past the middle of a texture 32 pixels
    // across: 17 parts in 32 of the pixels after it
    const tide_color blend = {0.502f, 0.531f, 0.531f, 1.0f}, between = {1.0f, 0.469f, 0.0f, 1.0f};
    shaded_scene();
    expect(picked_commands == 3, "draws through a shader with uniforms of their own are a command each");
    expect(counted_commands == 1, "instances through a shader, one after another, are one command");
    expect(sampled_commands == 1, "and so are instances with a texture");
    const check checks[] = {
        {32, 32, orange, "a shader's uniform: a color"},
        {80, 32, purple, "an int beside it, which turns its channels around"},
        {128, 32, turned, "and a negative one"},
        {176, 32, cyan, "the last of as many uniforms as a shader takes"},
        {224, 32, magenta, "and the next draw's, which are its own"},
        {32, 80, orange, "a vertex's int, read as bits"},
        {80, 80, dark_green, "an instance's ints: bits, and units to move by"},
        {128, 88, turned, "a negative one, and a float before them"},
        {128, 68, background, "and nothing where that one moved from"},
        {24, 120, red, "a shader's first texture, the nearest pixel: its first"},
        {40, 120, green, "the one beside it"},
        {24, 136, blue, "the one below it"},
        {40, 136, white, "and its last"},
        {64, 128, blend, "the same texture as its second, blended"},
        {49, 113, red, "and not with the far edge's, at its edge"},
        {104, 120, red, "a shader's first texture, with another as its second"},
        {129, 128, yellow, "the second's first pixel"},
        {144, 128, between, "blended with its last, between them"},
        {159, 128, red, "and its last"},
        {192, 128, white, "a shader's texture that's none: white"},
        {216, 120, red, "and the second beside it, the nearest pixel"},
        {232, 120, green, "and the one beside that"},
        {24, 168, red, "a texture read in the vertex shader"},
        {56, 168, green, "another pixel of it, for another instance"},
        {88, 168, blue, "and another"},
        {24, 208, red, "a rect over a shaded mesh"},
        {40, 208, blue, "a shaded mesh over a rect"},
        {64, 208, yellow, "and the rect under it"},
        {24, 248, yellow, "a pixel by where it is: left of 32 and above 256"},
        {40, 248, green, "right of 32"},
        {24, 264, red, "below 256"},
        {40, 264, black, "and both"},
        {31, 255, yellow, "the last pixel before both"},
        {32, 256, black, "and the first past them"},
        {96, 192, cyan, "a shaded mesh covers a mesh with the same corners: its first pixel"},
        {127, 223, cyan, "and its last"},
        {95, 208, background, "and nothing to its left"},
        {128, 208, background, "or its right"},
        {112, 191, background, "above it"},
        {112, 224, background, "or below it"},
        {144, 192, cyan, "through another camera: its first pixel"},
        {175, 223, cyan, "and its last"},
        {143, 208, background, "and nothing to its left"},
        {176, 208, background, "or its right"},
        {160, 191, background, "above it"},
        {160, 224, background, "or below it"},
        {192, 192, cyan, "on the screen: its first pixel"},
        {223, 223, cyan, "and its last"},
        {191, 208, background, "and nothing to its left"},
        {224, 208, background, "or its right"},
        {208, 191, background, "above it"},
        {208, 224, background, "or below it"},
    };
    check_pixels(checks, (int)(sizeof checks / sizeof checks[0]));
}

// How many commands shaded_3d_scene's instances came to.
static uint32_t lit_commands;

static void shaded_3d_scene(void)
{
    static const lit_vertex corners[4] = {{{-0.5f, 0.5f, 0.0f}, {0, 0, -1}}, {{0.5f, 0.5f, 0.0f}, {0, 0, -1}},
                                          {{0.5f, -0.5f, 0.0f}, {0, 0, -1}}, {{-0.5f, -0.5f, 0.0f}, {0, 0, -1}}};
    const tide_quaternion ahead = tide_identity_q();
    const tide_color colors[3] = {red, green, blue};
    tide_draw_reset(&list);
    tide_draw_clear(&list, background);
    tide_draw_camera_3d(&list, tide_f3(0, 0, -10), ahead, field_of_view_3d());
    // An ordinary mesh in front of where the last instance goes, drawn before it
    square_3d(tide_f3(3.2f, 1.6f, -2), ahead, 0.8f, white, NULL);
    const uint32_t mesh = quad_mesh(14, corners, sizeof corners[0]);
    const uint32_t before = list.count;
    for (int i = 0; i < 3; i++) {
        const lit_instance instance = {tide_trs_f4x4(tide_f3(4.0f * (float)(i - 1), 2, 0), ahead, tide_f3_splat(2.0f)),
                                       colors[i]};
        tide_draw_shaded(&list, &lit_shader, mesh, &instance, NULL, NULL, 0);
    }
    lit_commands = list.count - before;
    // ...and one behind the first, drawn after it
    square_3d(tide_f3(-6, 3, 5), ahead, 6.0f, yellow, NULL);
}

static void shaded_3d_checks(void)
{
    const tide_float2 first = seen(-4, 2, 0), middle = seen(0, 2, 0), last = seen(4, 2, 0);
    const float edge = unit_3d() * 0.75f; // Past the near mesh's edge, inside an instance's
    shaded_3d_scene();
    expect(lit_commands == 1, "instances of a mesh through a 3D shader, one after another, are one command");
    const check checks[] = {
        {first.x, first.y, red, "a shaded 3D mesh's first instance, in front of a far mesh drawn after it"},
        {first.x + unit_3d() * 1.5f, first.y, yellow, "and the far mesh around it"},
        {middle.x, middle.y, green, "its second, in its own color"},
        {middle.x - unit_3d() * 1.5f, middle.y, background, "and nothing beside it"},
        {last.x + edge, last.y, blue, "its last"},
        {last.x, last.y, white, "behind a near mesh drawn before it"},
    };
    check_pixels(checks, (int)(sizeof checks / sizeof checks[0]));
}

static void shaded(void)
{
    // Twice: the second frame draws with what the first uploaded
    for (int frame = 0; frame < 2; frame++) {
        shaded_checks();
        shaded_3d_checks();
    }

    // A host whose game was swapped for another build has the list forget
    // what it drew with, its shaders too, which went with the build: the
    // commands it has draw without them
    shaded_scene();
    tide_draw_forget(&list);
    const check forgotten[] = {
        {32, 32, background, "nothing where a shaded mesh was, after the list forgot it"},
        {24, 208, red, "a rect over it"},
        {40, 208, yellow, "and the one under it"},
        {112, 208, magenta, "and the mesh another covered"},
    };
    check_pixels(forgotten, (int)(sizeof forgotten / sizeof forgotten[0]));
    // ...and the next ones draw through them again: the first frame keeps
    // the pipelines of the shaders it draws through, and lets the others go
    shaded_3d_checks();
    shaded_checks();
}

// Rects side by side, a cell each as a grid's view draws them, leave no gap
// between them at any scale: not even where their shared edge falls exactly
// on a row or column of pixel centers, as it does every 8 cells at 3.125
// pixels a cell.
static void tiles_leave_no_gaps(void)
{
    static const float scales[] = {3.125f, 2.75f, 1.5f, 7.3f, 2.5f};
    static const tide_float2 centers[] = {{0.0f, 0.0f}, {0.3f, 0.7f}, {0.5f, 0.5f}, {-0.25f, 0.125f}, {0.0f, 0.2f}};
    enum { HALF = 20 }; // Cells each way from the middle
    for (size_t k = 0; k < sizeof scales / sizeof scales[0]; k++) {
        const float s = scales[k];
        tide_draw_reset(&list);
        tide_draw_clear(&list, background);
        tide_draw_camera(&list, centers[k], screen.y / (2.0f * s));
        for (int y = -HALF; y < HALF; y++) {
            for (int x = -HALF; x < HALF; x++) {
                tide_draw_rect(&list, tide_f2((float)x + 0.5f, (float)y + 0.5f), tide_f2(1, 1), orange);
            }
        }
        // Every pixel inside the tiles, but for a pixel's margin
        const float left = screen.x * 0.5f + (-HALF - centers[k].x) * s + 1.0f;
        const float right = screen.x * 0.5f + (HALF - centers[k].x) * s - 1.0f;
        const float top = screen.y * 0.5f - (HALF - centers[k].y) * s + 1.0f;
        const float bottom = screen.y * 0.5f - (-HALF - centers[k].y) * s - 1.0f;
        static tide_float2 points[WIDTH * HEIGHT];
        static uint32_t got[WIDTH * HEIGHT];
        int count = 0;
        for (int py = (int)top; py < (int)bottom && py < HEIGHT; py++) {
            for (int px = (int)left; px < (int)right && px < WIDTH; px++) {
                if (px >= 0 && py >= 0) points[count++] = tide_f2((float)px + 0.5f, (float)py + 0.5f);
            }
        }
        tide_platform_read_pixels(&list, points, count, got);
        int gaps = 0;
        for (int i = 0; i < count; i++) gaps += !near(got[i], rgba(orange), 0);
        char what[96];
        snprintf(what, sizeof what, "rects side by side leave no gaps at %.3f pixels a cell (%d missed)", (double)s, gaps);
        expect(gaps == 0, what);
    }
}

// ---------------------------------------------------------------------------
// --bench: frames of many shapes, rendered offscreen and read back, so it
// works in a hidden window or page, where tide_platform_draw draws nothing.
// Recording is timed separately. Rendering includes CPU submission, the GPU
// and transferring the target's pixels back: it is not a GPU-only timing.

#define BENCH_FRAMES 100
#define BENCH_WARMUP 20

static const char *bench_names[] = {"nothing", "65536 rects", "65536 circles", "65536 rects, text every 1024",
                                   "65536 textured quads", "65536 instances of a cube"};

// 0: nothing (what reading back costs), 1: rects, 2: circles, 3: rects with text between, 4: quads of one mesh,
// 5: instances of a 3D cube
static int bench_kind;
static int bench_frame;
static const char *bench_label;
static FILE *bench_output;

typedef struct bench_samples {
    double record[BENCH_FRAMES];
    double render_readback[BENCH_FRAMES];
    double total[BENCH_FRAMES];
} bench_samples;

typedef struct bench_timing {
    double median_ms;
    double p95_ms;
} bench_timing;

static bench_samples bench_times;

static int bench_compare(const void *a, const void *b)
{
    const double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

static bench_timing bench_summarize(double *ms)
{
    qsort(ms, BENCH_FRAMES, sizeof *ms, bench_compare);
    return (bench_timing){(ms[BENCH_FRAMES / 2 - 1] + ms[BENCH_FRAMES / 2]) / 2.0,
                         ms[(BENCH_FRAMES * 95 + 99) / 100 - 1]};
}

static void bench_json_text(FILE *f, const char *text)
{
    fputc('"', f);
    for (const unsigned char *c = (const unsigned char *)text; *c; c++) {
        if (*c == '"' || *c == '\\') fprintf(f, "\\%c", *c);
        else if (*c < 0x20) fprintf(f, "\\u%04x", *c);
        else fputc(*c, f);
    }
    fputc('"', f);
}

static void bench_write_record(const bench_timing recording, const bench_timing rendering, const bench_timing total)
{
    FILE *f = bench_output ? bench_output : stdout;
    char date[32] = "";
    const time_t now = time(NULL);
    const struct tm *utc = gmtime(&now);
    if (utc) strftime(date, sizeof date, "%Y-%m-%dT%H:%M:%SZ", utc);
#if defined(__wasm__)
    const char *os = "web", *arch = "wasm32";
#elif defined(__ANDROID__)
    const char *os = "android";
#elif defined(_WIN32)
    const char *os = "windows";
#elif defined(__APPLE__)
    const char *os = "macos";
#else
    const char *os = "linux";
#endif
#if !defined(__wasm__)
#if defined(__aarch64__) || defined(_M_ARM64)
    const char *arch = "arm64";
#elif defined(__arm__) || defined(_M_ARM)
    const char *arch = "arm";
#elif defined(__x86_64__) || defined(_M_X64)
    const char *arch = "x86_64";
#elif defined(__i386__) || defined(_M_IX86)
    const char *arch = "x86";
#else
    const char *arch = "unknown";
#endif
#endif
#if defined(__OPTIMIZE__)
    const char *optimized = "true";
#else
    const char *optimized = "false";
#endif
    // Supplied when measuring a build: configure-time Git metadata could name
    // an older revision after commits or edits that did not reconfigure it.
    const char *revision = getenv("TIDE_BENCH_REVISION");
    const char *machine = getenv("TIDE_BENCH_MACHINE");
    fprintf(f, "{\"benchmark\":\"draw\",\"label\":");
    bench_json_text(f, bench_label);
    fprintf(f, ",\"date\":\"%s\",\"os\":\"%s\",\"arch\":\"%s\",\"optimized\":%s,\"compiler\":", date, os, arch, optimized);
    bench_json_text(f, __clang_version__);
    fprintf(f, ",\"revision\":");
    bench_json_text(f, revision ? revision : "unknown");
    fprintf(f, ",\"machine\":");
    bench_json_text(f, machine ? machine : "unknown");
    fprintf(f, ",\"renderer\":");
    bench_json_text(f, tide_platform_renderer());
    fprintf(f, ",\"workload\":");
    bench_json_text(f, bench_names[bench_kind]);
    fprintf(f, ",\"count\":%u,\"width\":%d,\"height\":%d,\"warmup\":%d,\"frames\":%d,\"competitive_baseline\":false",
            bench_kind ? WIDTH * 256u : 0u, (int)screen.x, (int)screen.y, BENCH_WARMUP, BENCH_FRAMES);
    fprintf(f, ",\"record_ms\":{\"median\":%.6g,\"p95\":%.6g},\"render_readback_ms\":{\"median\":%.6g,\"p95\":%.6g},"
               "\"total_ms\":{\"median\":%.6g,\"p95\":%.6g}}\n",
            recording.median_ms, recording.p95_ms, rendering.median_ms, rendering.p95_ms, total.median_ms, total.p95_ms);
}

// A cube a unit across, its faces' corners each a color of their own.
static const tide_vertex3 cube_corners[8] = {
    {{-0.5f, -0.5f, -0.5f}, {0, 0}, {1, 0, 0, 1}}, {{0.5f, -0.5f, -0.5f}, {0, 0}, {0, 1, 0, 1}},
    {{0.5f, 0.5f, -0.5f}, {0, 0}, {0, 0, 1, 1}},   {{-0.5f, 0.5f, -0.5f}, {0, 0}, {1, 1, 0, 1}},
    {{-0.5f, -0.5f, 0.5f}, {0, 0}, {1, 0, 1, 1}},  {{0.5f, -0.5f, 0.5f}, {0, 0}, {0, 1, 1, 1}},
    {{0.5f, 0.5f, 0.5f}, {0, 0}, {1, 1, 1, 1}},    {{-0.5f, 0.5f, 0.5f}, {0, 0}, {0.5f, 0.5f, 0.5f, 1}},
};
static const uint32_t cube_indices[36] = {0, 1, 2, 0, 2, 3, 4, 6, 5, 4, 7, 6, 0, 4, 5, 0, 5, 1,
                                          3, 2, 6, 3, 6, 7, 0, 3, 7, 0, 7, 4, 1, 5, 6, 1, 6, 2};
static const tide_mesh cube = {cube_corners, 8, cube_indices, 36, 1};

static void bench_scene(void)
{
    tide_draw_reset(&list);
    if (bench_kind == 5) {
        tide_draw_camera_3d(&list, tide_f3(128, 60, -40), tide_axisangle_q(tide_f3(1, 0, 0), 0.6f), 70.0f);
        for (uint32_t i = 0; i < WIDTH * 256u; i++) {
            const tide_float3 p = tide_f3((float)(i % WIDTH), 0.0f, (float)(i / WIDTH));
            tide_draw_mesh_3d(&list, &cube, tide_translate_f4x4(p), NULL, TIDE_FILTER_BILINEAR);
        }
        return;
    }
    for (uint32_t i = 0; bench_kind > 0 && i < WIDTH * 256u; i++) {
        const tide_float2 p = at((float)(i % WIDTH) + 0.5f, (float)(i / WIDTH) + 0.5f);
        const tide_color c = {(float)(i % WIDTH) / WIDTH, (float)(i / WIDTH) / 256.0f, 0.5f, 1.0f};
        if (bench_kind == 4) {
            quad(tide_f2(p.x - 0.5f, p.y + 0.5f), tide_f2(p.x + 0.5f, p.y - 0.5f), c, c, &four_texture, TIDE_FILTER_POINT, true);
            continue;
        }
        if (bench_kind == 2) tide_draw_circle(&list, p, 0.6f, c);
        else tide_draw_rect(&list, p, tide_f2(1, 1), c);
        if (bench_kind == 3 && i % 1024u == 0) tide_draw_text(&list, "x", p, 10, white);
    }
}

static int bench(void)
{
    const uint64_t start = tide_time_now_ns();
    bench_scene();
    const uint64_t recorded = tide_time_now_ns();
    const tide_float2 point = tide_f2(1, 1);
    uint32_t pixel;
    tide_platform_read_pixels(&list, &point, 1, &pixel);
    const uint64_t rendered = tide_time_now_ns();
    if (bench_frame >= BENCH_WARMUP) {
        const int at = bench_frame - BENCH_WARMUP;
        bench_times.record[at] = (double)(recorded - start) / 1e6;
        bench_times.render_readback[at] = (double)(rendered - recorded) / 1e6;
        bench_times.total[at] = (double)(rendered - start) / 1e6;
    }
    if (++bench_frame < BENCH_WARMUP + BENCH_FRAMES) return TIDE_KEEP_RUNNING;
    const bench_timing recording = bench_summarize(bench_times.record);
    const bench_timing rendering = bench_summarize(bench_times.render_readback);
    const bench_timing total = bench_summarize(bench_times.total);
    printf("%s (ms, median / p95): record %.3f / %.3f, render + readback %.3f / %.3f, total %.3f / %.3f\n",
           bench_names[bench_kind], recording.median_ms, recording.p95_ms, rendering.median_ms, rendering.p95_ms,
           total.median_ms, total.p95_ms);
    if (bench_label) bench_write_record(recording, rendering, total);
    bench_frame = 0;
    if (++bench_kind < 6) return TIDE_KEEP_RUNNING;
    if (bench_output) {
        const bool failed = ferror(bench_output) != 0;
        const int closed = fclose(bench_output);
        bench_output = NULL;
        if (failed || closed != 0) {
            fprintf(stderr, "draw: couldn't finish saving benchmark results\n");
            return 1;
        }
        printf("saved as '%s' in " TIDE_BENCH_RESULTS "/draw.jsonl\n", bench_label);
    }
    return 0;
}

static bool benching;

static int frame(void *user, const float seconds)
{
    (void)user, (void)seconds;
    static bool said;
    if (!said) printf("renderer: %s\n", tide_platform_renderer()); // What a test of one backend looks for
    said = true;
    screen = tide_platform_screen_size();
    if (benching) return bench();
#ifdef __wasm__
    // WebGPU's device can go away (a driver that starts over), taking what the
    // renderer made on it. Once everything checked out, the page loses it as
    // that would, and when it says it found another, everything has to draw
    // again from what the list and the font keep.
    static int lost; // 1: lost, and waiting for another; 2: checking again
    if (lost == 1) {
        if (!tide_web_eval("Tide.gpuDevices() > 1")) return TIDE_KEEP_RUNNING;
        printf("the device was lost, and the page found another\n");
        lost = 2;
    }
    if (lost == 2) {
        meshes_3d();
        primitives();
        shaded();
        tiles_leave_no_gaps();
        return run_checks();
    }
#endif
    meshes();
    meshes_3d();
    primitives();
    shaded();
    tiles_leave_no_gaps();
    const int code = run_checks();
#ifdef __wasm__
    if (code == 0 && strcmp(tide_platform_renderer(), "WebGPU") == 0) {
        tide_web_eval("Tide.loseDevice()");
        lost = 1;
        return TIDE_KEEP_RUNNING;
    }
#endif
    return code;
}

int main(const int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--bench") == 0) benching = true;
        else if (strcmp(argv[i], "--save") == 0 && i + 1 < argc) bench_label = argv[++i];
        else if (strncmp(argv[i], "--save=", 7) == 0 && argv[i][7]) bench_label = argv[i] + 7;
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("usage: tide_platform_draw [--bench [--save <label>]]\n"
                   "  --bench          20 warmup frames, then 100 measured frames per workload\n"
                   "  --save <label>   append JSONL to bench/results/draw.jsonl, or print it when unavailable\n"
                   "On the web: ?bench&save=<label> prints the records.\n"
                   "Set TIDE_BENCH_REVISION and TIDE_BENCH_MACHINE to identify the build and CPU/GPU.\n");
            return 0;
        } else {
            fprintf(stderr, "draw: unknown option or missing value: '%s'\n", argv[i]);
            return 2;
        }
    }
    if (bench_label && !benching) {
        fprintf(stderr, "draw: --save needs --bench\n");
        return 2;
    }
    if (benching) {
        printf("benchmark: %d warmup and %d measured frames per workload; no competitive graphics baseline\n",
               BENCH_WARMUP, BENCH_FRAMES);
        if (bench_label) {
            bench_output = fopen(TIDE_BENCH_RESULTS "/draw.jsonl", "ab");
            if (!bench_output) printf("couldn't save to " TIDE_BENCH_RESULTS "/draw.jsonl; printing JSONL instead\n");
        }
    }
    tide_platform_open(&(tide_window_desc){.title = "draw", .width = WIDTH, .height = HEIGHT, .hidden = true});
    tide_platform_run(frame, NULL);
}
