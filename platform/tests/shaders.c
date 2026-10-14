// frag and vert, written in Tide (shaders.tide), through the whole path:
// tidec's GLSL and WGSL, the draw list, the renderer and the GPU, read back
// from an offscreen render. Every backend compiles every shader the game
// uses, and has to give the same pixels: OpenGL 3.3 natively, and in a
// browser WebGL 2 and WebGPU, whose device is then lost and found again.
//
// The game draws a 24 pixel square in the middle of 32 pixel cells, each
// through another frag or vert: `cell` is a pixel in one. Its frames are a
// window of SIZE by SIZE logical pixels on a display of a scale of 1,
// whatever the window that's open is (tide_platform_read_pixels_as): on a
// phone, that's a screen of another shape.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/platform.h"
#include "window.h" // The backend, for whether it draws into floats

#ifdef __wasm__
#include "tide_web.h" // The page, to lose WebGPU's device
#endif

#define SIZE 256

// The game's pack, as C (tide_add_game's for HOST programs)
extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

static tide_world world;
static tide_local local;
static tide_draw_list list;
static tide_gui gui;
static tide_devices devices;
static int failures;

typedef struct check {
    int column, row;   // The cell
    float dx, dy;      // From its middle, in pixels, y down
    float r, g, b;
    const char *what;
} check;

static const float back = 0.1f;

#define YELLOW 1.0f, 0.92156863f, 0.015686275f // Color.yellow, which is Unity's

static const check checks[] = {
    // What a frag reads
    {0, 0, 0, 0, 1, 0, 0, "a frag writes a color from its entity's component, times a singleton's"},
    {1, 0, 0, 0, 0, 1, 0, "the next instance of the draw has its own component"},
    {2, 0, 0, 0, 0, 0, 0.5f, "and the third, whose blue the singleton halves"},
    {4, 0, 0, 0, 1, 1, 1, "a frag that reads nothing writes white"},
    {5, 0, 0, 0, 1, 1, 1, "a vertex with only a position draws white, with no frag"},
    {6, 0, 0, 0, YELLOW, "with no frag, a pixel is its vertex's color"},
    {0, 1, 0, 0, back, back, back, "a vert moved the mesh away from where it was drawn"},
    {2, 1, 0, 0, YELLOW, "to where the singleton says"},
    {4, 1, -6, -6, 1, 0, 0, "a frag samples a grid: its cell (0, 0) at uv (0, 0)"},
    {4, 1, 6, -6, 0, 1, 0, "cell (1, 0) to the right"},
    {4, 1, -6, 6, 0, 0, 1, "cell (0, 1) below"},
    {4, 1, 6, 6, 1, 1, 1, "and cell (1, 1), the nearest cell each"},
    {5, 1, 0, 0, 0.5f, 0.54f, 0.54f, "sampled between its cells, they blend, by how near each is"},
    {6, 1, -6, -6, 1, 0, 0, "with no frag, the draw's texture times the vertex's color"},
    {6, 1, 6, 6, YELLOW, "at each of its cells"},
    // Where the pixel is
    {0, 2, -6, 0, 1, 0, 0, "this.position counts the screen's pixels from the left"},
    {0, 2, 6, 0, 0, 0, 1, "on both sides of the line"},
    {1, 2, 0, 0, back, back, back, "this.Discard leaves the pixel undrawn"},
    {1, 2, 9, 0, 1, 0, 1, "and the others drawn"},
    {2, 2, -6, 0, 1, 0, 0, "this.world is where the pixel is in the world"},
    {2, 2, 6, 0, 0, 1, 0, "on both sides of the line"},
    {3, 2, 0, 0, 0.4905f, 0, 0, "this.depth is how far it is between the camera's near and far sides"},
    {4, 2, 0, 0, 0.5f, 1, 0.5f, "a vert turns a normal by instance.transform, and the frag gets it"},
    {5, 2, 0, 0, 0, 1, 1, "a frag reads instance.transform"},
    // Ints, bools, enums and structs of a component
    {0, 3, 0, 0, 0, 1, 0, "a frag branches on an int of its instance"},
    {1, 3, 0, 0, 0.5f, 0, 0.5f, "on an enum and a bool, and reads a struct's fields"},
    {2, 3, 0, 0, 0, 0, 1, "and neither"},
    // The draw's settings
    {7, 0, 0, 0, 0.5f, 0.5f, 0, "a tint multiplies what a frag wrote, in a mesh a transform moved"},
    {7, 1, 0, 0, 1, 0.5f, 0.5f, "a mesh through a frag, added to what's there"},
    {7, 2, 0, 0, 0.5f, 0.75f, 0.5f, "and multiplying it, by as much as its alpha says"},
    {7, 6, 7, 0, 1, 0.5f, 0, "a transform turns and scales a mesh through a frag: past where its side was"},
    {7, 6, 5, 5, back, back, back, "and short of where its corner was"},
    {7, 3, 0, 0, 0, 1, 0, "a 3D mesh through a frag, tinted, through the 3D transform"},
    // Which side of a triangle draws, four to a cell
    {5, 6, -7, -7, 1, 1, 1, "a 3D mesh through a frag, mirrored by its own transform, is still seen from the front"},
    {5, 6, 7, -7, back, back, back, "one seen from the back isn't drawn"},
    {5, 6, 7, 7, back, back, back, "mirrored by its own transform or not"},
    {5, 6, -7, 7, 1, 1, 1, "unless the draw says the back is"},
    {6, 6, -7, -7, back, back, back, "a vert that mirrors its mesh turns its triangles round: they aren't drawn"},
    {6, 6, 7, 7, 1, 1, 1, "and the ones that were seen from the back are"},
    {6, 6, 7, -7, 1, 1, 1, "as are the first, with the draw saying the back is"},
    {6, 6, -7, 7, 1, 1, 1, "or both sides"},
    // 2D
    {0, 4, 0, 0, 0, 1, 0, "a rect drawn after a 2D mesh is over it"},
    {0, 4, 8, 0, 1, 0.5f, 0, "the mesh, through its frag, over the rect before it"},
    {0, 4, 14, 14, 0, 0, 1, "which shows around it"},
    {2, 4, 0, 0, 1, 1, 1, "a 2D vertex with only a position draws white"},
    {3, 4, -6, -6, 1, 0, 0, "a 2D mesh with no frag samples the draw's texture"},
    {3, 4, 6, 6, 1, 1, 1, "at each of its cells"},
    {4, 4, 0, -6, YELLOW, "a 2D mesh on the screen's pixels: this.position from the top"},
    {4, 4, 0, 6, 0, 1, 1, "on both sides of the line"},
    {5, 4, 0, 0, back, back, back, "a vert moved the 2D mesh away"},
    {7, 4, 0, 0, 1, 1, 1, "to where the singleton says"},
    // The language
    {0, 5, 0, 0, 0, 1, 0, "a frag's numbers, vectors, matrices, structs, functions and loops come out as on the CPU"},
    {1, 5, 0, 0, 0, 1, 0, "a vert reads its vertex's ints, samples a grid, and changes the color its pixels get"},
    {2, 5, 0, 0, 0, 1, 0, "and so do its matrices, int vectors, assignments, counting loops, structs in structs and the rest of Math"},
    {6, 4, -6, -6, 0.27f, 0.27f, 0, "Color(uv.xy, 0) writes uv.x and uv.y as red and green, with blue 0 and alpha 1"},
    {6, 4, 6, 6, 0.77f, 0.77f, 0, "and follows the uv across the triangle"},
    // Math, against what the CPU worked out
    {3, 3, 0, 0, 0, 1, 0, "Math.Hash in a frag is the CPU's number, for an int and each int vector, of constants too"},
    {4, 3, 0, 0, 0, 1, 0, "quaternions in a frag: products, rotations, slerps, and what builds one"},
    {5, 3, 0, 0, 0, 1, 0, "matrices in a frag: inverses, determinants, products, and what builds one"},
    {6, 3, 0, 0, 0, 1, 0, "the rest of Math in a frag is forgiving as the CPU's: NaN, vectors with no direction, powers of numbers under 0"},
    {3, 5, 0, 0, 0, 1, 0, "Math.Hash in a vert is the CPU's number"},
    {4, 5, 0, 0, 0, 1, 0, "quaternions in a vert"},
    {5, 5, 0, 0, 0, 1, 0, "matrices in a vert"},
    {6, 5, 0, 0, 0, 1, 0, "and the rest of Math in a vert"},
    {7, 3, 0, 0, 0, 1, 0, "a frag of loops alone, which start under 0 or at what's worked out"},
    {7, 5, 0, 0, 0.5f, 0.5f, 0, "a loop that starts under 0 samples a grid at each step"},
    // Instances of a 2D mesh
    {0, 6, 0, 0, 1, 0, 0, "three entities in one 2D draw: the vert puts each where its component says"},
    {1, 6, 0, 0, 0, 1, 0, "the second"},
    {2, 6, 0, 0, 0, 0, 1, "and the third"},
    {3, 6, 0, 0, back, back, back, "and nothing where there's none"},
    // Manual instances, without entities
    {0, 7, 0, 0, 1, 0, 0, "a list item's transform and tint reach its vert and frag"},
    {1, 7, 0, 0, 0, 1, 0, "the next ordinary struct supplies its own float and int fields"},
    {2, 7, 0, 0, 0, 0, 1, "and the third, through a mut helper in the vert"},
    {3, 7, -6, -6, 1, 0, 0, "a transform list instances the built-in 3D mesh with a point texture"},
    {3, 7, 6, 6, 1, 1, 1, "at each of its texture's cells"},
    {4, 7, 0, 0, YELLOW, "a 2D instance struct's transform field is ordinary shader data, with an identity Instance transform"},
    {5, 7, 0, 0, 0, 1, 1, "the second 2D item has its own placement and color"},
    {6, 7, -6, -6, 1, 0, 0, "manual 2D instancing takes a texture with a vert and no frag"},
    {6, 7, 6, 6, 1, 1, 1, "with the filter the draw specified"},
    {7, 7, 0, 0, back, back, back, "an empty instance list adds nothing"},
    // What a vert writes for its frag
    {3, 0, -6, 0, 0.27f, 1, 0, "a frag reads what its vert wrote for it, blended across the triangle: a quarter of the way"},
    {3, 0, 6, 0, 0.77f, 1, 0, "and three quarters"},
    {1, 1, 0, -6, 1, 0, 0.27f, "a frag reads another struct of the same vert's, whole, through a function"},
    {1, 1, 0, 6, 1, 0, 0.77f, "blended down the triangle"},
    {6, 2, 0, 0, YELLOW, "with no frag, what a vert writes for one goes nowhere, and the mesh draws as it does"},
    {4, 6, -6, 0, back, back, back, "a frag's path that discards its pixel writes no color"},
    {4, 6, 6, 0, 0, 1, 0, "and the others what the vert handed it"},
    {3, 1, 0, 0, 0, 0.5f, 1, "a frag reads an instance's struct beside what its vert wrote from it"},
    {1, 4, 0, -6, 0, 0.27f, 1, "a 2D vert hands its frag a struct the same way"},
    {1, 4, 0, 6, 0, 0.77f, 1, "blended down the triangle"},
};

#define CHECKS ((int)(sizeof checks / sizeof checks[0]))

static void expect(const bool ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) failures++;
}

static uint8_t byte(const float v)
{
    return (uint8_t)(v * 255.0f + 0.5f);
}

// The run of instances drawn through the shader named `name`: how many
// commands draw through it, and how many instances the first has.
static uint32_t runs_through(const char *name, uint32_t *instances)
{
    uint32_t found = 0;
    for (uint32_t i = 0; i < list.count; i++) {
        if (list.commands[i].kind != TIDE_DRAW_SHADED) continue;
        const tide_draw_run *r = &list.runs[list.commands[i].shaded.run];
        if (!r->shader || strcmp(r->shader->name, name) != 0) continue;
        if (found++ == 0) *instances = r->count;
    }
    return found;
}

// A frame of the game, and its pixels against what they should be.
static void run_checks(const char *when)
{
    local.Art.targetPass = 0;
    tide_draw_reset(&list);
    tide_gui_begin(&gui, &devices, tide_f2(SIZE, SIZE), tide_platform_measure_text);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &list, &gui);
    tide_gui_end(&gui, &list);

    uint32_t instances = 0;
    expect(runs_through("Tinted on Corner3", &instances) == 1 && instances == 3, "three entities through one frag are one draw");
    expect(runs_through("Place and Glow on Corner2", &instances) == 1 && instances == 3, "and so are three through a 2D vert");
    expect(runs_through("BatchPlace3 and BatchPaint3 on Corner3", &instances) == 1 && instances == 3,
           "three ordinary list items through a vert and frag are one draw");
    expect(runs_through("BatchPlace2 and BatchPaint2 on Corner2", &instances) == 1 && instances == 2,
           "two ordinary 2D items are one draw");
    expect(runs_through("Hand and Take on Corner3", &instances) == 1 && instances == 1
           && runs_through("Hand and TakeWhole on Corner3", &instances) == 1,
           "a vert drawn with two frags is two shaders, each carrying what its frag reads");

    static tide_float2 points[CHECKS];
    static uint32_t got[CHECKS];
    for (int i = 0; i < CHECKS; i++) {
        points[i] = tide_f2(16.0f + 32.0f * (float)checks[i].column + checks[i].dx, 16.0f + 32.0f * (float)checks[i].row + checks[i].dy);
    }
    tide_platform_read_pixels_as(&list, SIZE, SIZE, 1.0f, points, CHECKS, got);
    for (int i = 0; i < CHECKS; i++) {
        const uint8_t want[3] = {byte(checks[i].r), byte(checks[i].g), byte(checks[i].b)};
        bool ok = true;
        for (int c = 0; c < 3; c++) {
            const int d = (int)(got[i] >> (24 - 8 * c) & 0xFFu) - (int)want[c];
            ok = ok && d >= -8 && d <= 8;
        }
        printf("%s: %s%s (0x%08X, expected 0x%02X%02X%02XFF)\n", ok ? "ok" : "FAIL", checks[i].what, when, (unsigned)got[i],
               want[0], want[1], want[2]);
        if (!ok) failures++;
    }
}

// The generated shaders also run independently over entire destinations,
// including their corners where a stale camera or clip would leave pixels.
static void run_target_checks(const char *when)
{
    static const tide_float2 points[] = {{4, 4}, {SIZE - 5, 4}, {4, SIZE - 5}, {SIZE - 5, SIZE - 5},
                                         {64, 64}, {192, 64}, {64, 192}, {192, 192}};
    static const uint32_t pattern[] = {0xff0000ffu, 0x00ff00ffu, 0x0000ffffu, 0xffffffffu};
    static const uint32_t alpha[] = {0x40ff00ffu, 0x4000ffffu, 0x400000ffu, 0x40ffffffu};
    static const char *what[] = {"rendered Texture source, field filters and full alpha replacement",
                                "grid Blit source keeps top-left UVs across targets",
                                "CPU Texture source, field filters and full alpha replacement",
                                "a vert samples an explicit Texture with both filters",
                                "an ordinary mesh samples an explicit Texture"};
    for (int pass = 1; pass <= 5; pass++) {
        local.Art.targetPass = pass;
        tide_draw_reset(&list);
        tide_gui_begin(&gui, &devices, tide_f2(SIZE, SIZE), tide_platform_measure_text);
        tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &list, &gui);
        tide_gui_end(&gui, &list);
        uint32_t got[8];
        tide_platform_read_pixels_as(&list, SIZE, SIZE, 1.0f, points, 8, got);
        bool ok = true;
        const uint32_t *wanted = pass == 1 || pass == 3 ? alpha : pattern;
        for (int p = 0; p < 8; p++) {
            for (int channel = 0; channel < 4; channel++) {
                const int shift = 24 - 8 * channel;
                const int difference = (int)(got[p] >> shift & 0xffu) - (int)(wanted[p % 4] >> shift & 0xffu);
                ok &= difference >= -2 && difference <= 2;
            }
            if (!ok) printf("target pass %d point %d%s: 0x%08X, expected 0x%08X\n", pass, p, when,
                            (unsigned)got[p], (unsigned)wanted[p % 4]);
        }
        printf("%s: %s%s\n", ok ? "ok" : "FAIL", what[pass - 1], when);
        if (!ok) failures++;
    }
    local.Art.targetPass = 0;
}

// Targets that take their size from the window, and one with pixels of its
// own, made once by the game (pass 6): drawn as windows of several sizes would
// draw them on displays of several scales, they're the same picture in
// logical pixels, with nothing resized in between.
static void run_screen_target_checks(const char *when)
{
    static const struct {
        int width, height;
        float scale;
    } windows[] = {{SIZE, SIZE, 1.0f}, {SIZE, SIZE, 2.0f}, {SIZE, SIZE, 1.5f}, {320, 200, 1.5f}, {SIZE, SIZE, 1.0f}};
    for (size_t k = 0; k < sizeof windows / sizeof windows[0]; k++) {
        const float w = (float)windows[k].width, h = (float)windows[k].height, scale = windows[k].scale;
        const struct {
            float x, y; // In logical pixels
            int low[3], high[3];
            const char *what;
        } wanted[] = {
            {64, 64, {0}, {0}, "a frag's this.position is in logical pixels in a screen target"},
            {w * 0.75f, h * 0.25f, {0}, {0}, "all the way across it"},
            {w - 16, 16, {0, 0, 250}, {5, 5, 255}, "a screen target's own units are logical pixels, from the window's corner"},
            {w * 0.7f, h * 0.75f, {0, 250, 0}, {5, 255, 5}, "a target of half the window is half as many logical pixels across"},
            {w * 0.9f, h * 0.75f, {0, 0, 0}, {5, 5, 5}, "and no more"},
            {8, h - 56, {225, 225, 225}, {255, 255, 255}, "a target with 1 pixel a unit has its 4 by 4 pixels: the first"},
            {12, h - 56, {165, 165, 165}, {210, 210, 210}, "blended with the next, a quarter of the target across"},
            {40, h - 24, {0, 0, 0}, {5, 5, 5}, "and the others"},
        };
        enum { COUNT = sizeof wanted / sizeof wanted[0] };
        local.Art.targetPass = 6;
        tide_draw_reset(&list);
        tide_gui_begin(&gui, &devices, tide_f2(w, h), tide_platform_measure_text);
        tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &list, &gui);
        tide_gui_end(&gui, &list);
        tide_float2 points[COUNT];
        uint32_t got[COUNT];
        for (int i = 0; i < COUNT; i++) points[i] = tide_f2(wanted[i].x * scale, wanted[i].y * scale); // In its display's pixels
        tide_platform_read_pixels_as(&list, windows[k].width, windows[k].height, scale, points, COUNT, got);
        for (int i = 0; i < COUNT; i++) {
            int low[3], high[3];
            for (int c = 0; c < 3; c++) low[c] = wanted[i].low[c], high[c] = wanted[i].high[c];
            if (i < 2) { // Where the pixel is, over the window's size
                const int across = (int)(wanted[i].x / w * 255.0f + 0.5f), down = (int)(wanted[i].y / h * 255.0f + 0.5f);
                low[0] = across - 3, high[0] = across + 3, low[1] = down - 3, high[1] = down + 3, low[2] = 0, high[2] = 3;
            }
            bool ok = true;
            for (int c = 0; c < 3; c++) {
                const int channel = (int)(got[i] >> (24 - 8 * c) & 0xFFu);
                ok = ok && channel >= low[c] && channel <= high[c];
            }
            printf("%s: %s, in a window of %d by %d at scale %g%s (0x%08X)\n", ok ? "ok" : "FAIL", wanted[i].what,
                   windows[k].width, windows[k].height, (double)scale, when, (unsigned)got[i]);
            if (!ok) failures++;
        }
    }
    local.Art.targetPass = 0;
}

// What a uv past a texture's edges reads (pass 7): each wrap in a mesh draw,
// in a frag and in a vert, for a grid and a target's color, in 2D and 3D. A
// square's cells are a pixel of its texture each: `column` and `row` are one's,
// from the square's top left, its texture's first cell at (0, 0).
static void run_wrap_checks(const char *when)
{
    static const struct {
        float left, top, cell; // The square's corner, and how big a cell is, in pixels
        int column, row;
        float r, g, b;
        const char *what;
    } wanted[] = {
        {8, 8, 12, 0, 0, 1, 0, 0, "a mesh draw with no wrap: its texture's first cell"},
        {8, 8, 12, 2, 0, 0, 1, 0, "past its right edge, the cell at that edge"},
        {8, 8, 12, 0, 3, 0, 0, 1, "past its bottom edge, the one at that"},
        {8, 8, 12, 3, 3, 1, 1, 1, "and past both, its last"},
        {64, 8, 12, 2, 0, 1, 0, 0, "a mesh draw's Wrap.Repeat: its first cell again"},
        {64, 8, 12, 3, 0, 0, 1, 0, "then the one beside it"},
        {64, 8, 12, 0, 2, 1, 0, 0, "down as across"},
        {64, 8, 12, 2, 3, 0, 0, 1, "and both ways at once"},
        {120, 8, 12, 2, 0, 0, 1, 0, "a mesh draw's Wrap.Mirror: its last cell again"},
        {120, 8, 12, 3, 0, 1, 0, 0, "then back to its first"},
        {120, 8, 12, 0, 3, 1, 0, 0, "down as across"},
        {120, 8, 12, 2, 3, 0, 1, 0, "and both ways at once"},
        {176, 8, 12, 1, 1, 1, 1, 1, "a render target's color in a mesh draw: its own pixels"},
        {176, 8, 12, 2, 0, 1, 0, 0, "which repeat"},
        {176, 8, 12, 2, 3, 0, 0, 1, "both ways"},
        {8, 64, 12, 2, 0, 0, 1, 0, "a frag's Wrap.Clamp, said outright: the cell at the edge"},
        {8, 64, 12, 3, 3, 1, 1, 1, "its last, past both"},
        {64, 64, 12, 2, 0, 1, 0, 0, "a frag's Wrap.Repeat"},
        {64, 64, 12, 2, 3, 0, 0, 1, "both ways"},
        {120, 64, 12, 3, 0, 1, 0, 0, "a frag's Wrap.Mirror"},
        {120, 64, 12, 2, 3, 0, 1, 0, "both ways"},
        {176, 64, 12, 3, 0, 1, 0, 0, "a frag mirrors the Texture its draw gives it, a target's color"},
        {176, 64, 12, 0, 3, 1, 0, 0, "down as across"},
        {176, 64, 12, 2, 3, 0, 1, 0, "and both ways"},
        {8, 120, 12, 0, 2, 1, 0, 0, "one grid read two ways in one frag: where it repeats"},
        {8, 120, 12, 1, 3, 1, 1, 1, "row after row"},
        {8, 120, 12, 3, 2, 0, 0, 1, "and where it mirrors"},
        {8, 120, 12, 3, 3, 1, 0, 0, "row before row"},
        {64, 120, 12, 1, 1, 1, 1, 1, "a vert samples past the edges, each wrap its own"},
        {120, 120, 12, 3, 0, 1, 0, 0, "a vertex of the game's with no frag: the draw's Wrap.Mirror"},
        {120, 120, 12, 2, 3, 0, 1, 0, "both ways"},
        {176, 120, 12, 2, 0, 0, 1, 0, "and through the same shader, a draw that says no wrap: the cell at the edge"},
        {176, 120, 12, 0, 3, 0, 0, 1, "down as across"},
        // Blended, a cell 24 pixels across and one row, twice over: the rest are where two cells meet
        {8, 176, 24, 0, 0, 1, 0, 0, "a frag's blended Wrap.Repeat: its first cell at its middle"},
        {8 - 12, 176, 24, 2, 0, 0.5f, 0.5f, 0, "blended with its first where it starts again"},
        {8 - 12 + 0.5f, 176, 24, 0, 0, 0.5f, 0.5f, 0, "and with its last at its own edge"},
        {120 - 12, 176, 24, 2, 0, 0, 1, 0, "a frag's blended Wrap.Mirror: its last cell with itself, where it turns round"},
        {120 - 12 + 0.5f, 176, 24, 0, 0, 1, 0, 0, "and its first with itself at its own edge"},
        {120 - 12 - 0.5f, 176, 24, 4, 0, 1, 0, 0, "and where it turns again"},
        // 3D: Cell(column, 7)'s square, 24 across, a cell 6
        {4, 228, 6, 2, 0, 1, 0, 0, "a 3D mesh draw's Wrap.Repeat"},
        {4, 228, 6, 3, 3, 1, 1, 1, "both ways"},
        {36, 228, 6, 3, 0, 1, 0, 0, "a 3D mesh draw's Wrap.Mirror"},
        {36, 228, 6, 2, 3, 0, 1, 0, "both ways"},
        {68, 228, 6, 2, 1, 0, 0, 1, "a list's instances with a target's color, which repeats"},
        {100, 228, 6, 2, 0, 1, 0, 0, "in each of them"},
        {132, 228, 6, 3, 0, 1, 0, 0, "a 3D mesh mirrors a Texture of the CPU's pixels"},
        {132, 228, 6, 2, 3, 0, 1, 0, "both ways"},
    };
    enum { COUNT = sizeof wanted / sizeof wanted[0] };
    local.Art.targetPass = 7;
    tide_draw_reset(&list);
    tide_gui_begin(&gui, &devices, tide_f2(SIZE, SIZE), tide_platform_measure_text);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &list, &gui);
    tide_gui_end(&gui, &list);
    local.Art.targetPass = 0;
    tide_float2 points[COUNT];
    uint32_t got[COUNT];
    for (int i = 0; i < COUNT; i++) {
        points[i] = tide_f2(wanted[i].left + wanted[i].cell * ((float)wanted[i].column + 0.5f),
                            wanted[i].top + wanted[i].cell * ((float)wanted[i].row + 0.5f));
    }
    tide_platform_read_pixels_as(&list, SIZE, SIZE, 1.0f, points, COUNT, got);
    for (int i = 0; i < COUNT; i++) {
        const uint8_t want[3] = {byte(wanted[i].r), byte(wanted[i].g), byte(wanted[i].b)};
        bool ok = true;
        for (int c = 0; c < 3; c++) {
            const int d = (int)(got[i] >> (24 - 8 * c) & 0xFFu) - (int)want[c];
            ok = ok && d >= -8 && d <= 8;
        }
        printf("%s: %s%s (0x%08X, expected 0x%02X%02X%02XFF)\n", ok ? "ok" : "FAIL", wanted[i].what, when, (unsigned)got[i],
               want[0], want[1], want[2]);
        if (!ok) failures++;
    }
}

// Images of the game's own (pass 10): a Texture asset is drawn with the pack's
// pixels, and a texture is sampled as its declaration says ([Filter], [Wrap])
// wherever a draw or a Sample that names it doesn't say. Squares as the wraps'
// (run_wrap_checks), each cell a pixel of a texture that's red, green, blue
// and white, two by two.
static void run_image_checks(const char *when)
{
    static const struct {
        float left, top, cell;
        int column, row;
        float r, g, b;
        const char *what;
    } wanted[] = {
        {8, 8, 12, 0, 0, 1, 0, 0, "an image of the pack, drawn by its name: its first pixel"},
        {8, 8, 12, 1, 1, 1, 1, 1, "and its last"},
        {8, 8, 12, 2, 0, 1, 0, 0, "which repeats, as its declaration says"},
        {8, 8, 12, 3, 0, 0, 1, 0, "pixel by pixel"},
        {8, 8, 12, 2, 3, 0, 0, 1, "both ways"},
        {64, 8, 12, 2, 0, 0, 1, 0, "one whose declaration says no wrap: the pixel at its edge"},
        {64, 8, 12, 3, 3, 1, 1, 1, "its last, past both"},
        {120, 8, 12, 2, 0, 0, 1, 0, "a draw that says its wrap, over the declaration's: mirrored"},
        {120, 8, 12, 3, 0, 1, 0, 0, "back to its first"},
        {176, 8, 12, 0, 1, 0, 0, 1, "one of a list of images"},
        {176, 8, 12, 2, 0, 0, 1, 0, "which says nothing: the pixel at its edge"},
        {8, 64, 12, 1, 0, 0, 1, 0, "the image a field was given"},
        {8, 64, 12, 2, 0, 1, 0, 0, "a value, which is sampled as the image it was given says: repeated"},
        {8, 64, 12, 0, 3, 0, 0, 1, "down as across"},
        {64, 64, 12, 2, 0, 1, 0, 0, "a frag samples an image by its name, as its declaration says: repeated"},
        {64, 64, 12, 3, 3, 1, 1, 1, "both ways"},
        {120, 64, 12, 2, 0, 1, 0, 0, "the Texture a draw gives a frag, read as its source's declaration says: repeated"},
        {120, 64, 12, 3, 2, 0, 1, 0, "both ways"},
        {176, 64, 12, 2, 0, 0, 1, 0, "the same frag with another source: the pixel at its edge"},
        {176, 64, 12, 0, 2, 0, 0, 1, "down as across"},
        {8, 120, 12, 2, 0, 0, 1, 0, "a grid's asset in a mesh draw, as its declaration says: mirrored"},
        {8, 120, 12, 3, 0, 1, 0, 0, "back to its first"},
        {64, 120, 12, 2, 3, 0, 1, 0, "and in a frag, by its name"},
        {64, 120, 12, 3, 0, 1, 0, 0, "the same"},
        {120, 120, 12, 2, 0, 1, 0, 0, "a grid's field in a mesh draw, as its declaration says: repeated"},
        {120, 120, 12, 2, 3, 0, 0, 1, "both ways"},
        {176, 120, 12, 3, 0, 0, 1, 0, "and in a frag"},
        {176, 120, 12, 2, 2, 1, 0, 0, "the same"},
        // 3D: Cell(column, 7)'s square, 24 across, a cell 6
        {4, 228, 6, 2, 0, 1, 0, 0, "a 3D mesh with an image of the pack, as its declaration says: repeated"},
        {4, 228, 6, 3, 3, 1, 1, 1, "both ways"},
        {36, 228, 6, 2, 0, 0, 1, 0, "and one that says no wrap: the pixel at its edge"},
    };
    enum { COUNT = sizeof wanted / sizeof wanted[0] };
    local.Art.targetPass = 10;
    tide_draw_reset(&list);
    tide_gui_begin(&gui, &devices, tide_f2(SIZE, SIZE), tide_platform_measure_text);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &list, &gui);
    tide_gui_end(&gui, &list);
    local.Art.targetPass = 0;
    tide_float2 points[COUNT];
    uint32_t got[COUNT];
    for (int i = 0; i < COUNT; i++) {
        points[i] = tide_f2(wanted[i].left + wanted[i].cell * ((float)wanted[i].column + 0.5f),
                            wanted[i].top + wanted[i].cell * ((float)wanted[i].row + 0.5f));
    }
    tide_platform_read_pixels_as(&list, SIZE, SIZE, 1.0f, points, COUNT, got);
    for (int i = 0; i < COUNT; i++) {
        const uint8_t want[3] = {byte(wanted[i].r), byte(wanted[i].g), byte(wanted[i].b)};
        bool ok = true;
        for (int c = 0; c < 3; c++) {
            const int d = (int)(got[i] >> (24 - 8 * c) & 0xFFu) - (int)want[c];
            ok = ok && d >= -8 && d <= 8;
        }
        printf("%s: %s%s (0x%08X, expected 0x%02X%02X%02XFF)\n", ok ? "ok" : "FAIL", wanted[i].what, when, (unsigned)got[i],
               want[0], want[1], want[2]);
        if (!ok) failures++;
    }
    // The list drew the pack's own pixels: no copy of them
    bool own = false, copied = false;
    for (uint32_t i = 0; i < list.texture_count; i++) {
        if (list.textures[i].space != TIDE_PIXELS_PACK) continue;
        own = true;
        copied = copied || list.textures[i].capacity != 0;
    }
    printf("%s: the pack's images are drawn where the pack has them%s\n", own && !copied ? "ok" : "FAIL", when);
    if (!own || copied) failures++;
}

// A shadow map written in Tide (pass 8): casters drawn into a target of depth
// alone through a lamp's camera, a frag that compares each pixel's distance
// from the lamp with what's stored, and the stored depth read as a number.
// The window looks straight down on a floor 8 by 8, 32 pixels a unit, x to
// the right; the lamp slants along +x, so a shadow falls as far to the right
// as what casts it is high. The target has pixels of its own, so the picture
// is the same in logical pixels on any display.
static void run_shadow_checks(const char *when)
{
    static const struct {
        float x, y;      // In logical pixels
        int r, g, b;
        const char *what;
    } wanted[] = {
        {144, 128, 255, 255, 255, "a floor the lamp sees is lit"},
        {112, 128, 64, 64, 64, "under a slab one above it, a floor is in shadow, one to the right"},
        {112, 80, 255, 255, 255, "and lit past the slab's side"},
        {64, 128, 255, 0, 0, "the slab itself is lit: it's the nearest thing to the lamp there"},
        {240, 192, 64, 64, 64, "a caster through a frag casts where its pixels were drawn"},
        {208, 192, 255, 255, 255, "and nothing where its frag discarded them"},
        {29, 32, 67, 67, 67, "a depth read as a number: the slab's, from the lamp"},
        {42, 40, 83, 83, 83, "and the grate's, a little farther"},
        {38, 40, 255, 255, 255, "the far limit where the grate's frag discarded"},
        {10, 10, 255, 255, 255, "and where nothing was drawn since the clear, whatever colors were"},
    };
    enum { COUNT = sizeof wanted / sizeof wanted[0] };
    static const float scales[] = {1.0f, 2.0f, 1.5f};
    for (size_t k = 0; k < sizeof scales / sizeof scales[0]; k++) {
        const float scale = scales[k];
        local.Art.targetPass = 8;
        tide_draw_reset(&list);
        tide_gui_begin(&gui, &devices, tide_f2(SIZE, SIZE), tide_platform_measure_text);
        tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &list, &gui);
        tide_gui_end(&gui, &list);
        tide_float2 points[COUNT];
        uint32_t got[COUNT];
        for (int i = 0; i < COUNT; i++) points[i] = tide_f2((wanted[i].x + 0.5f) * scale, (wanted[i].y + 0.5f) * scale);
        tide_platform_read_pixels_as(&list, SIZE, SIZE, scale, points, COUNT, got);
        for (int i = 0; i < COUNT; i++) {
            const int channels[3] = {wanted[i].r, wanted[i].g, wanted[i].b};
            bool ok = true;
            for (int c = 0; c < 3; c++) {
                const int d = (int)(got[i] >> (24 - 8 * c) & 0xFFu) - channels[c];
                ok = ok && d >= -5 && d <= 5;
            }
            printf("%s: %s, at scale %g%s (0x%08X, expected 0x%02X%02X%02XFF)\n", ok ? "ok" : "FAIL", wanted[i].what,
                   (double)scale, when, (unsigned)got[i], (unsigned)wanted[i].r, (unsigned)wanted[i].g, (unsigned)wanted[i].b);
            if (!ok) failures++;
        }
    }
    local.Art.targetPass = 0;
}

// Stencil, from Tide (pass 9): shapes, meshes through frags and a vert, and 3D
// meshes that store a number for each pixel and are tested against it, in the
// window and in a target of the game's, which has its own. The window is blue
// where nothing was drawn.
static void run_stencil_checks(const char *when)
{
    static const struct {
        float x, y; // In logical pixels
        int r, g, b;
        const char *what;
    } wanted[] = {
        {48, 48, 255, 255, 255, "a rect drawn where a clear circle stored: at its middle"},
        {70, 48, 255, 255, 255, "and towards its edge"},
        {48, 10, 0, 0, 255, "and not above the circle, where the rect is too"},
        {20, 20, 0, 0, 255, "nor past its round side"},
        {140, 48, 0, 255, 0, "a mesh through a frag, drawn where another's frag stored"},
        {170, 48, 0, 0, 255, "and not where that one discarded its pixels"},
        {180, 70, 255, 255, 255, "a mesh through a vert, drawn where the number is another: past the sheet's left half"},
        {210, 70, 0, 0, 255, "and only as far as its vert leaves it"},
        {64, 160, 255, 255, 0, "two rects that each store one more: 2 where both are"},
        {30, 160, 255, 0, 255, "and 1 where only the first is"},
        {100, 160, 255, 0, 255, "or only the second"},
        {120, 160, 0, 0, 255, "and 0 past them"},
        {176, 144, 255, 0, 0, "a rect drawn where a 3D mesh stored"},
        {158, 144, 255, 255, 255, "a 3D mesh around it, drawn where the first didn't store: beside it"},
        {176, 126, 255, 255, 255, "and above it"},
        {210, 144, 0, 0, 255, "and nothing past both"},
        {200, 224, 0, 255, 0, "a target's own stencil: a rect drawn where its own draws stored, whatever the window's says"},
        {240, 224, 0, 0, 0, "and not where they didn't"},
    };
    enum { COUNT = sizeof wanted / sizeof wanted[0] };
    // One frame, as on a display scaled to twice its pixels, where a circle is a smooth shape: this
    // test's frames are slow in a browser with no GPU, and targets.c has stencil at every scale
    static const float scales[] = {2.0f};
    for (size_t k = 0; k < sizeof scales / sizeof scales[0]; k++) {
        const float scale = scales[k];
        local.Art.targetPass = 9;
        tide_draw_reset(&list);
        tide_gui_begin(&gui, &devices, tide_f2(SIZE, SIZE), tide_platform_measure_text);
        tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &list, &gui);
        tide_gui_end(&gui, &list);
        tide_float2 points[COUNT];
        uint32_t got[COUNT];
        for (int i = 0; i < COUNT; i++) points[i] = tide_f2((wanted[i].x + 0.5f) * scale, (wanted[i].y + 0.5f) * scale);
        tide_platform_read_pixels_as(&list, SIZE, SIZE, scale, points, COUNT, got);
        for (int i = 0; i < COUNT; i++) {
            const int channels[3] = {wanted[i].r, wanted[i].g, wanted[i].b};
            bool ok = true;
            for (int c = 0; c < 3; c++) {
                const int d = (int)(got[i] >> (24 - 8 * c) & 0xFFu) - channels[c];
                ok = ok && d >= -5 && d <= 5;
            }
            printf("%s: %s, at scale %g%s (0x%08X, expected 0x%02X%02X%02XFF)\n", ok ? "ok" : "FAIL", wanted[i].what,
                   (double)scale, when, (unsigned)got[i], (unsigned)wanted[i].r, (unsigned)wanted[i].g, (unsigned)wanted[i].b);
            if (!ok) failures++;
        }
    }
    local.Art.targetPass = 0;
}

// Several colors from one frag (pass 11): a target of three color images, the
// second and third of floats, drawn into by frags of three outputs and two,
// by one that discards, and by a shape of the engine's; each image read back
// through a frag of its own, into a panel of 64 pixels in the window; and a
// target of one float image that adds up past 1. A point is a pixel of the
// scene, (x, y), as its panel shows it. Where the device can't draw into
// floats, those images are bytes, and what was drawn is kept from 0 to 1.
static void run_output_checks(const char *when)
{
    const bool floats = tide_window_gpu()->float_targets();
    static const struct {
        int panel_x, panel_y; // The panel's corner in the window
        float x, y;           // In the panel
        int with[3];          // What floats give
        int without[3];       // ...and bytes
        const char *what;
    } wanted[] = {
        // The first image: bytes
        {0, 0, 48, 36, {193, 194, 0}, {193, 194, 0}, "a frag's first output is its target's first color image"},
        {0, 0, 32, 56, {128, 64, 255}, {128, 64, 255}, "which a clear fills, where nothing was drawn since"},
        {0, 0, 8, 8, {0, 0, 255}, {0, 0, 255}, "a frag of two outputs into a target of three writes the first"},
        {0, 0, 56, 8, {0, 255, 0}, {0, 255, 0}, "a shape of the engine's own is in the first image"},
        {0, 0, 28, 32, {114, 173, 0}, {114, 173, 0}, "where a frag of several outputs discards, the first image is as it was"},
        {0, 0, 36, 32, {255, 255, 255}, {255, 255, 255}, "and written where it doesn't"},
        // The second: floats, shown an eighth of what they are
        {64, 0, 48, 36, {97, 64, 128}, {32, 32, 128}, "a frag's second output is the second image, which keeps numbers past 1"},
        {64, 0, 32, 56, {16, 8, 255}, {16, 8, 255}, "a clear fills the second image too"},
        {64, 0, 8, 8, {255, 128, 64}, {32, 32, 64}, "a frag of two outputs writes the second"},
        {64, 0, 56, 8, {113, 64, 128}, {32, 32, 128}, "a shape of the engine's own leaves the second image as it was"},
        {64, 0, 28, 32, {57, 64, 128}, {32, 32, 128}, "where a frag discards, the second image is as it was"},
        {64, 0, 36, 32, {191, 191, 255}, {32, 32, 255}, "and written where it doesn't"},
        // The third: floats, shown with what's under 0 turned over
        {128, 0, 48, 36, {255, 64, 194}, {0, 64, 255}, "a frag's third output is the third image, which keeps numbers under 0"},
        {128, 0, 32, 56, {0, 64, 255}, {0, 64, 255}, "a clear fills the third image too"},
        {128, 0, 8, 8, {255, 64, 45}, {0, 64, 255}, "a frag of two outputs leaves the third image as it was"},
        {128, 0, 36, 32, {255, 64, 173}, {0, 64, 255}, "and so does one of two outputs that draws over it"},
        // A frag of three outputs, drawn into the window
        {192, 0, 48, 16, {193, 66, 0}, {193, 66, 0}, "a frag of several outputs drawn into the window: its first is the pixel's"},
        // One float image that draws added up
        {0, 64, 48, 32, {191, 191, 191}, {64, 64, 64}, "white added three times over is 3 in a float image"},
        {0, 64, 16, 32, {0, 0, 0}, {0, 0, 0}, "and nothing where nothing was drawn"},
        {0, 64, 30, 32, {24, 24, 24}, {8, 8, 8}, "a float image is blended between its pixels: nearer the dark one"},
        {0, 64, 33, 32, {167, 167, 167}, {56, 56, 56}, "and nearer the bright one"},
        // An image the target hasn't got
        {64, 64, 32, 32, {255, 0, 255}, {255, 0, 255}, "a draw that samples a color image its target hasn't got is passed by"},
    };
    enum { COUNT = sizeof wanted / sizeof wanted[0] };
    local.Art.targetPass = 11;
    tide_draw_reset(&list);
    tide_gui_begin(&gui, &devices, tide_f2(SIZE, SIZE), tide_platform_measure_text);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &list, &gui);
    tide_gui_end(&gui, &list);
    local.Art.targetPass = 0;
    tide_float2 points[COUNT];
    uint32_t got[COUNT];
    for (int i = 0; i < COUNT; i++) {
        points[i] = tide_f2((float)wanted[i].panel_x + wanted[i].x + 0.5f, (float)wanted[i].panel_y + wanted[i].y + 0.5f);
    }
    tide_platform_read_pixels_as(&list, SIZE, SIZE, 1.0f, points, COUNT, got);
    for (int i = 0; i < COUNT; i++) {
        const int *want = floats ? wanted[i].with : wanted[i].without;
        bool ok = true;
        for (int c = 0; c < 3; c++) {
            const int d = (int)(got[i] >> (24 - 8 * c) & 0xFFu) - want[c];
            ok = ok && d >= -6 && d <= 6;
        }
        printf("%s: %s%s%s (0x%08X, expected 0x%02X%02X%02XFF)\n", ok ? "ok" : "FAIL", wanted[i].what,
               floats ? "" : ", on a device with no float images", when, (unsigned)got[i], (unsigned)want[0],
               (unsigned)want[1], (unsigned)want[2]);
        if (!ok) failures++;
    }
}

// A draw's values (pass 12): blits of one frag with values of their own,
// meshes whose vert and frag read theirs, the same values twice as one draw,
// and instances beside them. A point is a pixel of the window.
static void run_values_checks(const char *when)
{
    static const struct {
        float x, y;
        int want[3];
        const char *what;
    } wanted[] = {
        {16, 16, {255, 128, 64}, "a blit's frag reads the values the blit gives"},
        {48, 16, {191, 128, 191}, "another blit of the same frag has its own, an int among them, and a third adds over it with its own"},
        {8, 72, {255, 0, 0}, "a mesh's vert and frag read the values its draw gives"},
        {40, 72, {0, 128, 0}, "the next draw of the same mesh and frag has other values"},
        {24, 72, {0, 0, 0}, "and each is where its own values put it"},
        {72, 72, {128, 128, 128}, "the same values twice are two instances of one draw"},
        {8, 136, {128, 0, 0}, "an instance's struct is its own, and the draw's values every instance's"},
        {40, 136, {0, 0, 128}, "the second instance has its own color and the same values"},
        {8, 168, {255, 0, 0}, "another draw of the same instances has other values"},
        {40, 168, {0, 0, 255}, "for each of its instances"},
        {24, 152, {0, 0, 0}, "and nothing is drawn between them"},
    };
    enum { COUNT = sizeof wanted / sizeof wanted[0] };
    local.Art.targetPass = 12;
    tide_draw_reset(&list);
    tide_gui_begin(&gui, &devices, tide_f2(SIZE, SIZE), tide_platform_measure_text);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &list, &gui);
    tide_gui_end(&gui, &list);
    local.Art.targetPass = 0;
    tide_float2 points[COUNT];
    uint32_t got[COUNT];
    for (int i = 0; i < COUNT; i++) points[i] = tide_f2(wanted[i].x + 0.5f, wanted[i].y + 0.5f);
    tide_platform_read_pixels_as(&list, SIZE, SIZE, 1.0f, points, COUNT, got);
    for (int i = 0; i < COUNT; i++) {
        bool ok = true;
        for (int c = 0; c < 3; c++) {
            const int d = (int)(got[i] >> (24 - 8 * c) & 0xFFu) - wanted[i].want[c];
            ok = ok && d >= -2 && d <= 2;
        }
        printf("%s: %s%s (0x%08X, expected 0x%02X%02X%02XFF)\n", ok ? "ok" : "FAIL", wanted[i].what, when, (unsigned)got[i],
               (unsigned)wanted[i].want[0], (unsigned)wanted[i].want[1], (unsigned)wanted[i].want[2]);
        if (!ok) failures++;
    }
    // One draw call for the two that share their values, and one for each of the others: three blits; two
    // meshes and the pair; two draws of instances
    int blits = 0, meshes = 0, instanced = 0;
    for (uint32_t i = 0; i < list.run_count; i++) {
        const tide_shader *shader = list.runs[i].shader;
        if (!shader) continue;
        blits += strcmp(shader->name, "Rinsed on Vertex") == 0;
        meshes += strcmp(shader->name, "PutAt and PutTint on Vertex") == 0;
        instanced += strcmp(shader->name, "SpeckAt and SpeckTint on Vertex") == 0;
    }
    const bool runs = blits == 3 && meshes == 3 && instanced == 2;
    printf("%s: draws with the same values in a row are one run, and others their own%s (%d, %d and %d runs)\n",
           runs ? "ok" : "FAIL", when, blits, meshes, instanced);
    if (!runs) failures++;
}

// Grids of floats as textures (pass 13, `Heights` in shaders.tide): one
// channel, read as red with green and blue 0 and alpha 1, whose numbers are
// the grid's own (2 is 2, and -1 is -1, where a byte would hold 1 and 0), and
// a Texture a field holds of one, which carries how it's sampled. Squares as
// the images' (run_image_checks): the grid is 0.5 and 1 over 2 and -1.
static void run_height_checks(const char *when)
{
    static const struct {
        float left, top, cell;
        int column, row;
        float r, g, b;
        const char *what;
    } wanted[] = {
        {8, 8, 12, 0, 0, 0.25f, 0.25f, 0, "a frag samples a grid of floats: its first cell as red, green and blue 0, alpha 1"},
        {8, 8, 12, 1, 0, 0.5f, 0.25f, 0, "the next"},
        {8, 8, 12, 0, 1, 1, 0.25f, 0, "a cell that's 2 is 2: more than a byte holds"},
        {8, 8, 12, 1, 1, 0, 0.25f, 0, "and one under 0"},
        {8, 8, 12, 3, 0, 0.5f, 0.25f, 0, "past its edge, the cell at its edge"},
        {64, 8, 12, 0, 0, 0.5f, 0, 0, "a mesh with no frag draws a grid of floats as red"},
        {64, 8, 12, 1, 0, 1, 0, 0, "cell by cell"},
        {64, 8, 12, 1, 1, 0, 0, 0, "black where it's under 0"},
        {120, 8, 24, 0, 0, 0.54f, 0, 0, "between two cells, a grid of floats blends, by how near each is"},
        {120, 8, 4, 0, 1, 0, 0, 0, "from the one"},
        {120, 8, 4, 5, 1, 1, 0, 0, "to the other"},
        {176, 8, 12, 2, 0, 0.25f, 0, 1, "the Texture a field holds of a grid, with the draw's wrap: repeated"},
        {176, 8, 12, 3, 1, 0, 0, 1, "both ways"},
        {176, 8, 12, 2, 1, 1, 0, 1, "cell by cell"},
        {8, 64, 12, 2, 0, 0.5f, 0, 1, "and as it says itself: the cell at its edge"},
        {8, 64, 12, 0, 3, 1, 0, 1, "down as across"},
    };
    enum { COUNT = sizeof wanted / sizeof wanted[0] };
    local.Art.targetPass = 13;
    tide_draw_reset(&list);
    tide_gui_begin(&gui, &devices, tide_f2(SIZE, SIZE), tide_platform_measure_text);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &list, &gui);
    tide_gui_end(&gui, &list);
    local.Art.targetPass = 0;
    tide_float2 points[COUNT];
    uint32_t got[COUNT];
    for (int i = 0; i < COUNT; i++) {
        points[i] = tide_f2(wanted[i].left + wanted[i].cell * ((float)wanted[i].column + 0.5f),
                            wanted[i].top + wanted[i].cell * ((float)wanted[i].row + 0.5f));
    }
    tide_platform_read_pixels_as(&list, SIZE, SIZE, 1.0f, points, COUNT, got);
    for (int i = 0; i < COUNT; i++) {
        const uint8_t want[3] = {byte(wanted[i].r), byte(wanted[i].g), byte(wanted[i].b)};
        bool ok = true;
        for (int c = 0; c < 3; c++) {
            const int d = (int)(got[i] >> (24 - 8 * c) & 0xFFu) - (int)want[c];
            ok = ok && d >= -8 && d <= 8;
        }
        printf("%s: %s%s (0x%08X, expected 0x%02X%02X%02XFF)\n", ok ? "ok" : "FAIL", wanted[i].what, when, (unsigned)got[i],
               want[0], want[1], want[2]);
        if (!ok) failures++;
    }
    // The list has the grid as floats of 16 bits, one a cell
    bool halves = false;
    for (uint32_t i = 0; i < list.texture_count; i++) {
        const tide_draw_texture *t = &list.textures[i];
        halves = halves || (t->space == TIDE_PIXELS_GRID && t->format == TIDE_TEXELS_HALF && t->width == 2 && t->height == 2);
    }
    printf("%s: a grid of floats goes to the GPU as a float of 16 bits a cell%s\n", halves ? "ok" : "FAIL", when);
    if (!halves) failures++;
}

// Filter.Trilinear (pass 14, `Mipmaps` in shaders.tide): each square is a
// texture of stripes drawn an eighth of its size, whose pixels are the mean
// of the 8 by 8 they cover where it's read between its smaller images, and
// the ones under a pixel's middle, which have no stripe, where it isn't.
// `lit` is how wide the stripes drawn into the target are that frame.
static void run_mipmap_checks(const float lit, const char *when)
{
    // A float texture's smaller images are drawn, so made where the device draws into floats
    const bool floats = tide_window_gpu()->float_targets();
    const int drawn = (int)(lit * 255.0f / 8.0f + 0.5f);
    const struct {
        float left;
        int want[3];
        const char *what;
    } wanted[] = {
        {8, {32, 64, 255}, "a grid whose declaration says Trilinear, sampled in a frag's branch: the mean of what a pixel covers"},
        {24, {32, 64, 255}, "a Texture that says Trilinear, through a frag that leaves the filter to it, after a discard"},
        {40, {0, 0, 255}, "the same frag with a draw that says Bilinear: the texture's own pixels"},
        {56, {32, 64, 255}, "a Sample that says Trilinear, whatever the draw says"},
        {72, {0, 0, 255}, "a uv a loop changes can't be asked about as the frag starts: the texture's own pixels"},
        {88, {32, 64, 255}, "a mesh with no frag, of a grid that says Trilinear"},
        {104, {0, 0, 255}, "and with a draw that says Point"},
        {120, {32, 64, 255}, "a mesh through a vert alone, of a Texture that says Trilinear"},
        {136, {floats ? 32 : 0, 0, 0}, "a grid of floats, sampled Trilinear"},
        {152, {drawn, 0, 0}, "a target's color, read between its smaller images: made of what was just drawn into it"},
        {168, {drawn, 0, 0}, "through a frag too"},
    };
    enum { COUNT = sizeof wanted / sizeof wanted[0] };
    local.Art.targetPass = 14;
    local.Mips.lit = lit;
    tide_draw_reset(&list);
    tide_gui_begin(&gui, &devices, tide_f2(SIZE, SIZE), tide_platform_measure_text);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &list, &gui);
    tide_gui_end(&gui, &list);
    local.Art.targetPass = 0;
    tide_float2 points[2 * COUNT];
    uint32_t got[2 * COUNT];
    for (int i = 0; i < COUNT; i++) { // A pixel near each square's corner, and one inside
        points[2 * i] = tide_f2(wanted[i].left + 0.5f, 8.5f);
        points[2 * i + 1] = tide_f2(wanted[i].left + 5.5f, 12.5f);
    }
    tide_platform_read_pixels_as(&list, SIZE, SIZE, 1.0f, points, 2 * COUNT, got);
    for (int i = 0; i < 2 * COUNT; i++) {
        const int *want = wanted[i / 2].want;
        bool ok = true;
        for (int c = 0; c < 3; c++) {
            const int d = (int)(got[i] >> (24 - 8 * c) & 0xFFu) - want[c];
            ok = ok && d >= -6 && d <= 6;
        }
        printf("%s: %s%s%s (0x%08X, expected 0x%02X%02X%02XFF)\n", ok ? "ok" : "FAIL", wanted[i / 2].what,
               i % 2 ? ", at another pixel" : "", when, (unsigned)got[i], (unsigned)want[0], (unsigned)want[1], (unsigned)want[2]);
        if (!ok) failures++;
    }
    // One frag given a texture read two ways is one shader in two runs: how it's read is no part of its text
    int runs = 0;
    for (uint32_t i = 0; i < list.run_count; i++) {
        runs += list.runs[i].shader && strcmp(list.runs[i].shader->name, "MipSource on Vertex") == 0;
    }
    printf("%s: a frag whose texture is read another way is the same shader in another run%s (%d runs)\n",
           runs == 3 ? "ok" : "FAIL", when, runs);
    if (runs != 3) failures++;
}

// Alpha (pass 15, `Alphas` in shaders.tide): textures hold colors times their
// alpha and blends take them so, which nothing written in Tide sees. Squares
// of 48 pixels over white.
static void run_alpha_checks(const char *when)
{
    static const struct {
        float x, y;
        float r, g, b;
        const char *what;
    } wanted[] = {
        {20, 32, 1, 1, 1, "a grid's white cell blended a quarter of the way to a clear one of another color, over white, is white"},
        {32, 32, 1, 1, 1, "and halfway"},
        {52, 32, 1, 1, 1, "and what's under shows where it's clear"},
        {88, 32, 1, 0.5f, 0, "a Sample between the two is the white cell's color, and half its alpha"},
        {144, 32, 0.5f, 0.75f, 1, "a half-clear rect over white"},
        {200, 32, 0.5f, 0.75f, 1, "the same drawn into a clear target, and the target drawn over white"},
        {32, 88, 0.5f, 0.75f, 1, "and through a frag that samples the target"},
    };
    enum { COUNT = sizeof wanted / sizeof wanted[0] };
    local.Art.targetPass = 15;
    tide_draw_reset(&list);
    tide_gui_begin(&gui, &devices, tide_f2(SIZE, SIZE), tide_platform_measure_text);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &list, &gui);
    tide_gui_end(&gui, &list);
    local.Art.targetPass = 0;
    tide_float2 points[COUNT];
    uint32_t got[COUNT];
    for (int i = 0; i < COUNT; i++) points[i] = tide_f2(wanted[i].x, wanted[i].y);
    tide_platform_read_pixels_as(&list, SIZE, SIZE, 1.0f, points, COUNT, got);
    for (int i = 0; i < COUNT; i++) {
        const uint8_t want[3] = {byte(wanted[i].r), byte(wanted[i].g), byte(wanted[i].b)};
        bool ok = true;
        for (int k = 0; k < 3; k++) {
            const int d = (int)(got[i] >> (24 - 8 * k) & 0xFFu) - (int)want[k];
            ok = ok && d >= -3 && d <= 3;
        }
        printf("%s: %s%s (0x%08X, expected 0x%02X%02X%02XFF)\n", ok ? "ok" : "FAIL", wanted[i].what, when, (unsigned)got[i],
               want[0], want[1], want[2]);
        if (!ok) failures++;
    }
}

// Textures that are numbers (pass 16, `Raws` in shaders.tide): [Raw] ones are
// read as their file or their grid has them, where a texture of colors has no
// color in a pixel with no alpha. Squares of 32 pixels, 40 apart.
static void run_raw_checks(const char *when)
{
    static const struct {
        float r, g, b;
        const char *what;
    } wanted[] = {
        {210 / 255.0f, 200 / 255.0f, 100 / 255.0f, "a [Raw] Texture asset's clear pixel keeps the numbers its file has"},
        {0, 0, 0, "the same file as colors has none where it's clear"},
        {210 / 255.0f, 200 / 255.0f, 100 / 255.0f, "a [Raw] Grid2<Color> asset keeps them too"},
        {1, 0.5f, 0.25f, "a [Raw] grid's cell with no alpha is its numbers"},
        {0, 0, 0, "and the same cell of a grid of colors is none"},
        {0.5f, 0.5f, 0.5f, "between two cells, a [Raw] grid's numbers blend as numbers, whatever their alpha"},
    };
    enum { COUNT = sizeof wanted / sizeof wanted[0] };
    local.Art.targetPass = 16;
    tide_draw_reset(&list);
    tide_gui_begin(&gui, &devices, tide_f2(SIZE, SIZE), tide_platform_measure_text);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &list, &gui);
    tide_gui_end(&gui, &list);
    local.Art.targetPass = 0;
    tide_float2 points[COUNT];
    uint32_t got[COUNT];
    for (int i = 0; i < COUNT; i++) points[i] = tide_f2(24.0f + 40.0f * (float)i, 24.0f);
    tide_platform_read_pixels_as(&list, SIZE, SIZE, 1.0f, points, COUNT, got);
    for (int i = 0; i < COUNT; i++) {
        const uint8_t want[3] = {byte(wanted[i].r), byte(wanted[i].g), byte(wanted[i].b)};
        bool ok = true;
        for (int k = 0; k < 3; k++) {
            const int d = (int)(got[i] >> (24 - 8 * k) & 0xFFu) - (int)want[k];
            ok = ok && d >= -3 && d <= 3;
        }
        printf("%s: %s%s (0x%08X, expected 0x%02X%02X%02XFF)\n", ok ? "ok" : "FAIL", wanted[i].what, when, (unsigned)got[i],
               want[0], want[1], want[2]);
        if (!ok) failures++;
    }
}

// Cubemaps (pass 17, `CubeFaces` and `CubeReads` in shaders.tide): each
// square of 8 pixels reads a cube towards one direction, or across a face.
// `lit` is how wide the stripes drawn into the painted cube's first face are
// that frame, 8 apart: 2 or 6, so the pixel under a square's pixel's middle
// is between two stripes, or on one.
static void run_cube_checks(const float lit, const char *when)
{
    static const int colors[6][3] = {{255, 0, 0}, {0, 255, 255}, {0, 255, 0}, {255, 0, 255}, {0, 0, 255}, {255, 255, 0}};
    static const char *const sides[6] = {"+x", "-x", "+y", "-y", "+z", "-z"};
    const bool floats = tide_window_gpu()->float_targets();
    struct {
        float x, y;
        int want[3];
        const char *what, *side;
    } wanted[64];
    int count = 0;
#define WANT(at_x, at_y, r, g, b, text, of)                                                                             \
    wanted[count++] = (__typeof__(wanted[0])){(float)(at_x), (float)(at_y), {(r), (g), (b)}, (text), (of)}
    for (int face = 0; face < 6; face++) {
        const int *c = colors[face];
        WANT(8 + face * 32, 8, 255, 255, 255, "an asset's cube, towards a face's top left quarter, which is white: face ", sides[face]);
        WANT(24 + face * 32, 8, c[0], c[1], c[2], "and towards its bottom right, by a direction of any length: face ", sides[face]);
        if (face) {
            WANT(8 + face * 32, 24, 255, 255, 255, "a cube a view painted, towards a face's top left quarter: face ", sides[face]);
            WANT(24 + face * 32, 24, c[0], c[1], c[2], "and towards its bottom right: face ", sides[face]);
        }
        WANT(8 + face * 32, 72, c[0], c[1], c[2], "what a camera at the cube's middle saw up and to the right of the face's axis: face ",
             sides[face]);
        WANT(24 + face * 32, 72, 0, 0, 0, "and up and to the left, where nothing was: face ", sides[face]);
    }
    const int mean = (int)(lit * 255.0f / 8.0f + 0.5f);
    WANT(8, 40, 255, 255, 64, "an asset's cube kept in a field, blended at a face's middle, in a branch", "");
    WANT(24, 40, mean, 0, 0, "a face read between its smaller images: the mean of what a pixel covers, made of what was just drawn", "");
    WANT(40, 40, lit > 4 ? 255 : 0, 0, 0, "the same face at its own pixels: what's under a pixel's middle", "");
    if (floats) WANT(56, 40, 128, 64, 32, "a cube of floats, added into twice, holds what's past 1", "");
    else WANT(56, 40, 64, 64, 32, "a cube of floats that's bytes here, added into twice, keeps to 1", "");
    WANT(72, 40, 0, 128, 0, "a cube that was never made reads as black", "");
    WANT(88, 40, 128, 128, 128, "a vert that reads a cube moved its square away", "");
    WANT(88, 56, 255, 255, 255, "to where the cube's color says", "");
#undef WANT
    local.Art.targetPass = 17;
    local.Cubes.lit = lit;
    tide_draw_reset(&list);
    tide_gui_begin(&gui, &devices, tide_f2(SIZE, SIZE), tide_platform_measure_text);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &list, &gui);
    tide_gui_end(&gui, &list);
    local.Art.targetPass = 0;
    tide_float2 points[2 * 64];
    uint32_t got[2 * 64];
    for (int i = 0; i < count; i++) { // A pixel at each square's corner, and one inside
        points[2 * i] = tide_f2(wanted[i].x + 0.5f, wanted[i].y + 0.5f);
        points[2 * i + 1] = tide_f2(wanted[i].x + 5.5f, wanted[i].y + 4.5f);
    }
    tide_platform_read_pixels_as(&list, SIZE, SIZE, 1.0f, points, (uint32_t)(2 * count), got);
    for (int i = 0; i < 2 * count; i++) {
        const int *want = wanted[i / 2].want;
        bool ok = true;
        for (int c = 0; c < 3; c++) {
            const int d = (int)(got[i] >> (24 - 8 * c) & 0xFFu) - want[c];
            ok = ok && d >= -6 && d <= 6;
        }
        printf("%s: %s%s%s%s (0x%08X, expected 0x%02X%02X%02XFF)\n", ok ? "ok" : "FAIL", wanted[i / 2].what, wanted[i / 2].side,
               i % 2 ? ", at another pixel" : "", when, (unsigned)got[i], (unsigned)want[0], (unsigned)want[1], (unsigned)want[2]);
        if (!ok) failures++;
    }
    // Draws of one frag towards other directions are runs of one shader: the direction is its values
    int runs = 0;
    for (uint32_t i = 0; i < list.run_count; i++) {
        runs += list.runs[i].shader && strcmp(list.runs[i].shader->name, "SkyLook on Vertex") == 0;
    }
    printf("%s: a frag that reads an asset's cube towards twelve directions is one shader in twelve runs%s (%d runs)\n",
           runs == 12 ? "ok" : "FAIL", when, runs);
    if (runs != 12) failures++;
}

// Lists (pass 18, `ListReads` and `ListOwners` in shaders.tide): frags and a
// vert read lists by an element, a count and foreach. Squares of 8 pixels, 16
// apart. `turn` is what the first color's green is that frame.
static void run_list_checks(const float turn, const char *when)
{
    const struct {
        float x, y;
        int want[3];
        const char *what;
    } wanted[] = {
        {8, 8, {128, 128, 128}, "an element before a list's start is nothing"},
        {24, 8, {255, (int)(turn * 255.0f), 0}, "a list's first element, as the view left it this frame"},
        {40, 8, {0, 255, 0}, "its second"},
        {56, 8, {0, 0, 255}, "its third"},
        {72, 8, {255, 255, 0}, "its last"},
        {88, 8, {128, 128, 128}, "an element past a list's end is nothing"},
        {8, 24, {128, 64, 128}, "how many each list has, an empty one's none"},
        {24, 24, {128, 128, 64}, "a foreach over structs of three pixels each, with a switch on an enum"},
        {40, 24, {52, 53, 0}, "a list of floats, four to a pixel, longer than two rows of its texture"},
        {56, 24, {85, 64, 255}, "an element that's across two rows of its list's texture"},
        {72, 24, {187, 64, 255}, "a list's last element, in its third row"},
        {88, 24, {128, 128, 128}, "a vert that reads a list moved its square away"},
        {88, 40, {255, 255, 255}, "to where the list's element says"},
        {8, 40, {255, 0, 0}, "an entity's own list"},
        {24, 40, {0, 0, 255}, "another entity's, in the same frame"},
        {8, 56, {0, 0, 255}, "a list the draw gives, made along the way"},
        {24, 56, {0, 255, 0}, "another draw's, of the same frag"},
        {40, 56, {128, 128, 128}, "a draw that gives no list reads nothing"},
        {8, 72, {128, 191, 128}, "an array's elements, by a place the draw gives, and how many another has"},
        {24, 72, {255, 0, 128}, "an array's last element, and nothing past it"},
        {40, 72, {207, 191, 64}, "a copy of an array written at places inside it and past its ends, and arrays through foreach and a function"},
        {56, 72, {255, 128, 64}, "an array that's in each element of a list"},
        {8, 88, {200, 44, 128}, "a list of bytes, and a byte that keeps to 255 as it's added to"},
        {24, 88, {64, 128, 191}, "a grid's cell by its place, past its first chunk, with nothing past its edges"},
        {40, 88, {128, 128, 255}, "a grid's size, and a cell of a grid of ints"},
        {56, 88, {0, 255, 0}, "an asset's grid, a cell by its place"},
        {72, 88, {128, 191, 64}, "a while, a for that doubles, and a loop only a break ends"},
        {8, 104, {0, 128, 128}, "a list under a local's name, and a local that names that one"},
        {24, 104, {191, 159, 64}, "a function that takes two lists, which passes each on to another and goes through one"},
        {40, 104, {159, 191, 32}, "the same functions called with the lists the other way round"},
        {56, 104, {191, 191, 128}, "grids under locals' names and as functions' parameters, passed on"},
        {72, 104, {126, 128, 64}, "an asset's list of bytes and an asset's grid, read in functions, with nothing past the list's end"},
        {88, 104, {128, 128, 128}, "a vert that reads a list in a function moved its square away"},
        {88, 88, {255, 255, 255}, "to where the list's element says, above"},
        {8, 120, {255, (int)(turn * 255.0f), 0}, "a map's value by its key, as the view left it this frame"},
        {24, 120, {64, 128, 191}, "another key's"},
        {40, 120, {128, 128, 128}, "a key that was removed is nothing"},
        {56, 120, {128, 128, 128}, "a key that never was there is nothing"},
        {72, 120, {0, 0, 255}, "a key under zero"},
        {88, 120, {255, 128, 191}, "TryGet and Has of a key that's there"},
        {104, 120, {0, 0, 64}, "TryGet and Has of a key that was removed"},
        {8, 136, {128, 128, 255}, "a map keyed by an enum, a struct for its value"},
        {24, 136, {191, 191, 64}, "a key that isn't there: TryGet's default, and an index's zeros"},
        {40, 136, {183, 0, 128}, "a map of 3000 keys: its last, the key after it, and how many"},
        {56, 136, {255, 128, 64}, "a foreach over a map, in the order its keys were added, without the one removed"},
        {72, 136, {255, 128, 128}, "a set's key, how many it has, and its keys in order"},
        {88, 136, {0, 128, 128}, "a key removed from a set"},
        {104, 136, {0, 191, 128}, "an empty map, and a map keyed by bytes"},
        {120, 136, {0, 255, 255}, "a map the draw gives, made along the way"},
        {136, 136, {255, 255, 0}, "a map under locals' names, and a map and a set as a function's parameters"},
        {8, 152, {255, 64, 191}, "an array of arrays, read inside it and past either's end, and how many each has"},
        {24, 152, {191, 64, 128}, "a copy of one written an element and a row at a time, inside it and past its ends"},
        {40, 152, {128, 191, 64}, "a for whose step writes an array's element, through a continue, and one that starts by writing one"},
        {56, 152, {128, 64, 255}, "'?:' between structs, arrays, rows and matrices, its first side"},
        {72, 152, {64, 255, 64}, "and its second"},
        {8, 168, {64, 128, 191}, "a Grid3's cell by its place, past its first chunk and layer, with nothing past its edges"},
        {24, 168, {128, 128, 255}, "a Grid3's size, and a cell of a Grid3 of ints in its last layer"},
        {40, 168, {191, 191, 191}, "a grid with no size: cells before 0 and far apart, and nothing elsewhere"},
        {56, 168, {128, 128, 64}, "a Grid3 that's open along an axis, and a grid with nothing in it"},
        {8, 184, {191, 64, 64}, "fields of a list's elements, of which only what's read is sent: by a chain of fields"},
        {24, 184, {159, 128, 0}, "what a foreach reads of its variable, an array among it"},
        {40, 184, {191, 128, 191}, "what locals that hold an element, and a part of one, are read of"},
        {56, 184, {128, 255, 64}, "a list only a function reads, a float of each, four elements to a pixel"},
        {72, 184, {128, 223, 64}, "parts of a map's values, by a key and through a foreach, with its keys"},
        {88, 184, {64, 191, 128}, "a part of a grid's cells, and its size"},
        {104, 184, {191, 96, 0}, "a list that's only counted, of which nothing is sent"},
    };
    enum { COUNT = sizeof wanted / sizeof wanted[0] };
    local.Art.targetPass = 18;
    local.Tables.turn = turn;
    tide_draw_reset(&list);
    tide_gui_begin(&gui, &devices, tide_f2(SIZE, SIZE), tide_platform_measure_text);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &list, &gui);
    tide_gui_end(&gui, &list);
    local.Art.targetPass = 0;
    tide_float2 points[COUNT];
    uint32_t got[COUNT];
    for (int i = 0; i < COUNT; i++) points[i] = tide_f2(wanted[i].x + 4.5f, wanted[i].y + 4.5f);
    tide_platform_read_pixels_as(&list, SIZE, SIZE, 1.0f, points, COUNT, got);
    for (int i = 0; i < COUNT; i++) {
        bool ok = true;
        for (int k = 0; k < 3; k++) {
            const int d = (int)(got[i] >> (24 - 8 * k) & 0xFFu) - wanted[i].want[k];
            ok = ok && d >= -3 && d <= 3;
        }
        printf("%s: %s%s (0x%08X, expected 0x%02X%02X%02XFF)\n", ok ? "ok" : "FAIL", wanted[i].what, when, (unsigned)got[i],
               (unsigned)wanted[i].want[0], (unsigned)wanted[i].want[1], (unsigned)wanted[i].want[2]);
        if (!ok) failures++;
    }
}

// Rolls (pass 19, `Rolls` in shaders.tide): Random's rolls on the type in
// frags and verts, and Random.seed. The GPU's generator is whole numbers, so
// every backend rolls the same, and so does this: the state a pixel's or a
// vertex's starts from is Math.Hash of where it is, which instance of its draw
// it's of, and the number the frame rolled with which frag or vert it is; and
// each roll is PCG's RXS-M-XS (put_dice and tide_roll in the compiler's
// shader.c). `state` is where this machine's generator is as the frame starts,
// which the frame's number is the next roll of. Squares of 8 pixels, 16 apart.
static uint32_t gpu_roll(uint32_t *dice)
{
    *dice = *dice * 747796405u + 2891336453u;
    const uint32_t w = ((*dice >> ((*dice >> 28) + 4u)) ^ *dice) * 277803737u;
    return (w >> 22) ^ w;
}

// Random.NextInt(max), as the GPU has it
static int gpu_roll_below(uint32_t *dice, const uint32_t max)
{
    return (int)(((uint64_t)gpu_roll(dice) * max) >> 32);
}

// Which frag or vert a stage's text says it is: the number its generators are made with
static int32_t roll_site(const char *glsl)
{
    const char *made = strstr(glsl, "tide_dice = uint(");
    const char *with = made ? strstr(made, " ^ ") : NULL;
    return with ? (int32_t)strtoul(with + 3, NULL, 10) : 0;
}

static const tide_shader *shader_named(const char *name)
{
    for (uint32_t i = 0; i < list.run_count; i++) {
        if (list.runs[i].shader && strcmp(list.runs[i].shader->name, name) == 0) return list.runs[i].shader;
    }
    return NULL;
}

// What RollBytes writes at pixel (x, y) of instance `nth` of its draw
static void roll_bytes(const int x, const int y, const int nth, const int32_t rolled, const int32_t site, float out[3])
{
    uint32_t dice = (uint32_t)tide_hash_i4((tide_int4){x, y, nth, rolled ^ site});
    out[0] = (float)gpu_roll_below(&dice, 256);
    out[1] = (float)(100 + gpu_roll_below(&dice, 100));
    out[2] = (float)((gpu_roll(&dice) >> 1) % 256u);
}

// What the colors RollTint rolls for the square's four corners blend to at (dx, dy) in it: its
// triangles are corners (0, 1, 2) and (0, 2, 3)
static void roll_tint(const float dx, const float dy, const int32_t rolled, const int32_t site, float out[3])
{
    const float u = dx / 8.0f, v = dy / 8.0f;
    float weights[4] = {0};
    if (u >= v) weights[0] = 1.0f - u, weights[1] = u - v, weights[2] = v;
    else weights[0] = 1.0f - v, weights[2] = u, weights[3] = v - u;
    out[0] = out[1] = out[2] = 0.0f;
    for (int corner = 0; corner < 4; corner++) {
        uint32_t dice = (uint32_t)tide_hash_i3((tide_int3){corner, 0, rolled ^ site});
        for (int c = 0; c < 3; c++) out[c] += weights[corner] * (float)gpu_roll_below(&dice, 256);
    }
}

static void run_roll_checks(const int64_t state, const char *when)
{
    enum { BYTES, FIRST, SECOND, RANGES, TINT, SEED, OVER, FILL, PICK };
    static const struct {
        int kind, x, y; // The pixel
        const char *what;
    } wanted[] = {
        {BYTES, 9, 9, "a pixel's rolls in a frag"},
        {BYTES, 10, 9, "the pixel beside it rolls its own"},
        {BYTES, 13, 14, "and so does one across the square"},
        {BYTES, 15, 15, "and its last"},
        {FIRST, 27, 11, "a pixel of a draw's first instance"},
        {FIRST, 30, 14, "another of them"},
        {SECOND, 43, 11, "the same place in its second instance, which rolls apart"},
        {SECOND, 46, 14, "another of those"},
        {RANGES, 57, 9, "every roll, 24 times over, is within what it says it gives"},
        {RANGES, 60, 12, "at another pixel"},
        {RANGES, 62, 10, "and another"},
        {TINT, 78, 9, "a vert's rolls are each corner's own, blended across the triangle"},
        {TINT, 73, 14, "and across the square's other triangle"},
        {TINT, 76, 10, "and between"},
        {SEED, 92, 12, "the match's seed, a byte a channel"},
        {OVER, 14, 25, "a frag that rolls behind a vert that rolls: each with a generator of its own"},
        {OVER, 10, 29, "in the square's other triangle"},
        {FILL, 200, 60, "a blit's frag rolls for each pixel of its destination, over the image it reads"},
        {FILL, 201, 60, "the next pixel"},
        {FILL, 60, 200, "and one where the image is another color"},
        {PICK, 27, 27, "a '?:' that rolls on both sides makes both rolls, the first side's first"},
        {PICK, 30, 29, "at another pixel"},
    };
    enum { COUNT = sizeof wanted / sizeof wanted[0], SEEDED = 0x00ABCDEF };
    local.Art.targetPass = 19;
    local.tide_dice.state = state;
    world.Time.seed = SEEDED;
    tide_random dice = {state};
    const int32_t rolled = (int32_t)tide_random_next(&dice); // What tide_frame rolls for its draws
    tide_draw_reset(&list);
    tide_gui_begin(&gui, &devices, tide_f2(SIZE, SIZE), tide_platform_measure_text);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &list, &gui);
    tide_gui_end(&gui, &list);
    local.Art.targetPass = 0;
    world.Time.seed = 0;

    const tide_shader *bytes = shader_named("RollBytes on Vertex");
    const tide_shader *placed = shader_named("SpotPlace and RollBytes on Vertex");
    const tide_shader *tint = shader_named("RollTint on Vertex");
    const tide_shader *over = shader_named("RollTint and RollOver on Vertex");
    const tide_shader *fill = shader_named("RollFill on Vertex");
    const tide_shader *pick = shader_named("RollPick on Vertex");
    uint32_t instances = 0;
    const bool drawn = bytes && placed && tint && over && fill && pick
                    && runs_through("SpotPlace and RollBytes on Vertex", &instances) == 1 && instances == 2;
    printf("%s: frags and verts that roll draw, two instances in one draw%s\n", drawn ? "ok" : "FAIL", when);
    if (!drawn) {
        failures++;
        return;
    }

    tide_float2 points[COUNT];
    uint32_t got[COUNT];
    for (int i = 0; i < COUNT; i++) points[i] = tide_f2((float)wanted[i].x + 0.5f, (float)wanted[i].y + 0.5f);
    tide_platform_read_pixels_as(&list, SIZE, SIZE, 1.0f, points, COUNT, got);
    for (int i = 0; i < COUNT; i++) {
        const int x = wanted[i].x, y = wanted[i].y;
        float want[3] = {0, 255, 0}; // RANGES: green
        int within = 1; // Whole numbers come out as they are; what's blended between corners is the GPU's rounding
        switch (wanted[i].kind) {
        case BYTES: roll_bytes(x, y, 0, rolled, roll_site(bytes->glsl_fragment), want); break;
        case FIRST: roll_bytes(x, y, 0, rolled, roll_site(placed->glsl_fragment), want); break;
        case SECOND: roll_bytes(x, y, 1, rolled, roll_site(placed->glsl_fragment), want); break;
        case TINT:
            roll_tint((float)(x - 72) + 0.5f, (float)(y - 8) + 0.5f, rolled, roll_site(tint->glsl_vertex), want);
            within = 3;
            break;
        case SEED: want[0] = SEEDED & 255, want[1] = (SEEDED >> 8) & 255, want[2] = (SEEDED >> 16) & 255; break;
        case OVER: {
            float blended[3];
            roll_tint((float)(x - 8) + 0.5f, (float)(y - 24) + 0.5f, rolled, roll_site(over->glsl_vertex), blended);
            uint32_t pixel = (uint32_t)tide_hash_i4((tide_int4){x, y, 0, rolled ^ roll_site(over->glsl_fragment)});
            want[0] = (float)gpu_roll_below(&pixel, 256), want[1] = blended[1], want[2] = 0;
            within = 3;
            break;
        }
        case FILL: // The image's cells are red and green above blue and white: its blue is 0 above and 1 below
            roll_bytes(x, y, 0, rolled, roll_site(fill->glsl_fragment), want);
            want[2] = y < SIZE / 2 ? 0 : 255;
            break;
        case PICK: { // The first roll is picked, the second made and left, and the third is what's rolled after
            uint32_t pixel = (uint32_t)tide_hash_i4((tide_int4){x, y, 0, rolled ^ roll_site(pick->glsl_fragment)});
            want[0] = (float)gpu_roll_below(&pixel, 256);
            (void)gpu_roll_below(&pixel, 256);
            want[1] = (float)gpu_roll_below(&pixel, 256), want[2] = 0;
            break;
        }
        default: break;
        }
        bool ok = true;
        for (int k = 0; k < 3; k++) {
            const int d = (int)(got[i] >> (24 - 8 * k) & 0xFFu) - (int)(want[k] + 0.5f);
            ok = ok && d >= -within && d <= within;
        }
        printf("%s: %s%s (0x%08X, expected 0x%02X%02X%02XFF)\n", ok ? "ok" : "FAIL", wanted[i].what, when, (unsigned)got[i],
               (unsigned)(want[0] + 0.5f), (unsigned)(want[1] + 0.5f), (unsigned)(want[2] + 0.5f));
        if (!ok) failures++;
    }
    // No two of the pixels that roll are alike, and a frame later they're all other colors
    static uint32_t before[8];
    static bool rolled_before;
    bool apart = true, changed = true;
    for (int i = 0; i < 8; i++) {
        for (int k = i + 1; k < 8; k++) apart = apart && got[i] != got[k];
        changed = changed && got[i] != before[i];
    }
    printf("%s: no two pixels that roll are alike%s\n", apart ? "ok" : "FAIL", when);
    if (!apart) failures++;
    if (rolled_before) {
        printf("%s: and a frame later each is another color%s\n", changed ? "ok" : "FAIL", when);
        if (!changed) failures++;
    }
    memcpy(before, got, sizeof before);
    rolled_before = true;
}

static tide_float3 linear_column(const tide_float4 c)
{
    return tide_f3(c.x, c.y, c.z);
}

// `normal` through `m` as a normal goes: the inverse of its 3 by 3,
// transposed. The shader works it out another way (cross products of the
// columns, and nothing divided), so each checks the other.
static tide_float3 normal_through(const tide_float4x4 m, const tide_float3 normal)
{
    const tide_float3x3 linear = {linear_column(m.c0), linear_column(m.c1), linear_column(m.c2)};
    return tide_normalize_f3(tide_mul_f3x3_f3(tide_transpose_f3x3(tide_inverse_f3x3(linear)), normal));
}

// Normals (pass 20): instance.Normal in a frag and in a vert, under the
// transforms the game keeps in its Facets. A square's color is the normal it
// was given, a half for 0, read at its middle, which is where its transform
// puts the mesh's.
static void run_normal_checks(const char *when)
{
    local.Art.targetPass = 20;
    tide_draw_reset(&list);
    tide_gui_begin(&gui, &devices, tide_f2(SIZE, SIZE), tide_platform_measure_text);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &list, &gui);
    tide_gui_end(&gui, &list);
    local.Art.targetPass = 0;

    const tide_float3 normal = tide_f3(1.0f, 2.0f, 2.0f); // The squares' corners', as the game made them
    const tide_float4x4 flat = local.Facets.flat;
    const tide_float4x4 through = tide_mul_f4x4(local.Facets.outer, local.Facets.inner);
    const tide_float4x4 rotated = local.Facets.rotated;
    // With no depth left there's no inverse, and the normals are along the axis that went, the way they leaned
    const tide_float3 flattened = tide_normalize_f3(tide_cross_f3(linear_column(flat.c0), linear_column(flat.c1)));
    const tide_float3 leaned = tide_normalize_f3(tide_rotate_f4x4(rotated, normal));
    const tide_float3 right = normal_through(rotated, normal);
    const struct {
        float x, y; // The pixel
        tide_float3 normal;
        const char *what;
    } wanted[] = {
        {local.Facets.even.c3.x, local.Facets.even.c3.y, normal_through(local.Facets.even, normal),
         "a mesh scaled as much every way"},
        {local.Facets.squashed.c3.x, local.Facets.squashed.c3.y, normal_through(local.Facets.squashed, normal),
         "a mesh scaled more one way than another"},
        {local.Facets.mirrored.c3.x, local.Facets.mirrored.c3.y, normal_through(local.Facets.mirrored, normal),
         "a mesh turned inside out as well: its normal still points out of it"},
        {flat.c3.x, flat.c3.y, flattened, "a mesh with no depth left, whose transform has no inverse"},
        {through.c3.x, through.c3.y, normal_through(through, normal), "a mesh under Draw.Transform, through both"},
        {local.Facets.turned.c3.x, local.Facets.turned.c3.y, normal_through(local.Facets.turned, normal),
         "a vert's, which its frag gets from the corners"},
        {rotated.c3.x, rotated.c3.y, leaned, "what Math.Rotate makes of the same normal"},
        {local.Facets.first.c3.x, local.Facets.first.c3.y, normal_through(local.Facets.first, normal),
         "an item of an Instanced draw's list, by its own transform"},
        {local.Facets.second.c3.x, local.Facets.second.c3.y, normal_through(local.Facets.second, normal),
         "the next item, by its own, which mirrors it"},
        {208.0f - 128.0f, 128.0f - 208.0f, tide_f3(0.6f, 0.0f, 0.8f), "a 2D mesh's, which has no transform: of length 1"},
    };
    enum { COUNT = sizeof wanted / sizeof wanted[0] };
    tide_float2 points[COUNT];
    uint32_t got[COUNT];
    // The camera has the world's (0, 0) at the window's middle, a pixel a unit, y up
    for (int i = 0; i < COUNT; i++) points[i] = tide_f2(SIZE / 2 + wanted[i].x + 0.5f, SIZE / 2 - wanted[i].y + 0.5f);
    tide_platform_read_pixels_as(&list, SIZE, SIZE, 1.0f, points, COUNT, got);
    for (int i = 0; i < COUNT; i++) {
        const float channels[3] = {wanted[i].normal.x, wanted[i].normal.y, wanted[i].normal.z};
        int want[3];
        bool ok = true;
        for (int c = 0; c < 3; c++) {
            want[c] = byte(channels[c] * 0.5f + 0.5f);
            const int d = (int)(got[i] >> (24 - 8 * c) & 0xFFu) - want[c];
            ok = ok && d >= -3 && d <= 3;
        }
        printf("%s: the normal of %s%s (0x%08X, expected 0x%02X%02X%02XFF)\n", ok ? "ok" : "FAIL", wanted[i].what, when,
               (unsigned)got[i], (unsigned)want[0], (unsigned)want[1], (unsigned)want[2]);
        if (!ok) failures++;
    }
    // ...which is far from where the normal goes, or these checks would pass a Normal that only rotated
    const bool apart = tide_dot_f3(leaned, right) < 0.7f;
    printf("%s: and that's another direction than the normal's%s\n", apart ? "ok" : "FAIL", when);
    if (!apart) failures++;
}

// Meshes from files (pass 21): squares 16 pixels across, each with its middle
// at a pixel, whose corners are red (bottom left, seen from its front), green
// (bottom right), blue (top right) and white (top left), in two triangles
// that meet along the line from red to blue. Seen from the front, the file's
// picture is as the file has it; a square seen from behind isn't drawn.
static void run_model_checks(const char *when)
{
    enum { NEAR_RED, NEAR_GREEN, NEAR_BLUE, MIDDLE, TOP_LEFT, TOP_RIGHT, BOTTOM_LEFT, BOTTOM_RIGHT };
    // A spot's pixel from the square's middle, and what the corners' colors blend to there
    static const float spots[][2] = {{-4.5f, 5.5f}, {4.5f, 5.5f}, {4.5f, -2.5f}, {0.5f, 0.5f},
                                     {-4.5f, -4.5f}, {4.5f, -4.5f}, {-4.5f, 4.5f}, {4.5f, 4.5f}};
    static const float red[3] = {0.78125f, 0.0625f, 0.15625f}, green[3] = {0.21875f, 0.625f, 0.15625f};
    static const float blue[3] = {0.21875f, 0.125f, 0.65625f}, none[3] = {0, 0, 0};
    static const float lit_first[3] = {0.5f, 1.0f, 0.15625f}, lit_third[3] = {0.5f, 1.0f, 0.65625f};
    static const float pure_red[3] = {1, 0, 0}, pure_green[3] = {0, 1, 0}, pure_blue[3] = {0, 0, 1}, white[3] = {1, 1, 1};
    static const struct {
        float x, y; // The square's middle
        int spot;
        const float *is;
        const char *what;
    } wanted[] = {
        {24, 24, NEAR_RED, red, "a mesh from a .glb, seen from its front: the color of its first corner"},
        {24, 24, NEAR_GREEN, green, "its second, to the right, as the file has it"},
        {24, 24, NEAR_BLUE, blue, "its third, above that"},
        {56, 24, MIDDLE, none, "the same mesh from behind isn't drawn"},
        {88, 24, NEAR_RED, green, "but with both sides drawn: mirrored"},
        {88, 24, NEAR_GREEN, red, "its first corner to the right"},
        {120, 24, NEAR_RED, lit_first, "a vertex of the game's through a frag: its default, its normal, its color"},
        {120, 24, NEAR_BLUE, lit_third, "across the square"},
        {152, 24, TOP_LEFT, pure_red, "its uvs: the texture's first pixel at its top left"},
        {152, 24, TOP_RIGHT, pure_green, "its second to the right"},
        {152, 24, BOTTOM_LEFT, pure_blue, "its third below"},
        {152, 24, BOTTOM_RIGHT, white, "and its last"},
        {184, 24, NEAR_RED, red, "a mesh from an .obj: the same square"},
        {184, 24, NEAR_GREEN, green, "the same way round"},
        {184, 24, NEAR_BLUE, blue, "and up"},
        {24, 56, NEAR_RED, red, "an instance of a mesh from a file"},
        {56, 56, NEAR_GREEN, green, "and a second"},
        {88, 56, NEAR_BLUE, blue, "one of a glob's list"},
        {120, 56, NEAR_RED, red, "one of a glob's map, by its path"},
        {152, 56, NEAR_GREEN, green, "a mesh by its two lists"},
        {184, 56, MIDDLE, none, "a key the map hasn't got draws nothing"},
    };
    enum { COUNT = sizeof wanted / sizeof wanted[0] };
    local.Art.targetPass = 21;
    tide_draw_reset(&list);
    tide_gui_begin(&gui, &devices, tide_f2(SIZE, SIZE), tide_platform_measure_text);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &list, &gui);
    tide_gui_end(&gui, &list);
    local.Art.targetPass = 0;
    // The quad by its name, in an instanced draw, from the list, from the map and by its lists: one mesh in the list
    uint32_t first = 0, drawn[8] = {0}, plain = 0;
    for (uint32_t i = 0; i < list.count; i++) {
        if (list.commands[i].kind == TIDE_DRAW_CLEAR) first = i; // The pass's own draws come after its clear
    }
    for (uint32_t i = first; i < list.count; i++) {
        const tide_draw_command *c = &list.commands[i];
        if (c->kind != TIDE_DRAW_MESH_3D) continue;
        bool known = false;
        for (uint32_t k = 0; k < plain; k++) known = known || drawn[k] == c->mesh3.mesh;
        if (!known && plain < 8) drawn[plain++] = c->mesh3.mesh;
    }
    expect(plain == 2, "a file drawn by its name, from a list, from a map and by its lists is one mesh, and the .obj another");
    tide_float2 points[COUNT];
    uint32_t got[COUNT];
    for (int i = 0; i < COUNT; i++) points[i] = tide_f2(wanted[i].x + spots[wanted[i].spot][0], wanted[i].y + spots[wanted[i].spot][1]);
    tide_platform_read_pixels_as(&list, SIZE, SIZE, 1.0f, points, COUNT, got);
    for (int i = 0; i < COUNT; i++) {
        const uint8_t want[3] = {byte(wanted[i].is[0]), byte(wanted[i].is[1]), byte(wanted[i].is[2])};
        bool ok = true;
        for (int c = 0; c < 3; c++) {
            const int d = (int)(got[i] >> (24 - 8 * c) & 0xFFu) - (int)want[c];
            ok = ok && d >= -8 && d <= 8;
        }
        printf("%s: %s%s (0x%08X, expected 0x%02X%02X%02XFF)\n", ok ? "ok" : "FAIL", wanted[i].what, when, (unsigned)got[i],
               want[0], want[1], want[2]);
        if (!ok) failures++;
    }
}

// Places (pass 22): verts that put their vertices themselves, `out float4`,
// from where the engine would have, `this.clip`. Each square is read inside
// where it has to be and outside it, which is often where its transform
// alone would have drawn it.
static void run_place_checks(const char *when)
{
    static const float black[3] = {0, 0, 0}, red[3] = {1, 0, 0}, green[3] = {0, 1, 0}, blue[3] = {0, 0, 1}, white[3] = {1, 1, 1};
    // Where the square moved to the right is in the world, half a pixel from its middle: its transform's place, at pixel (56, 176)
    static const float where[3] = {(56.5f - 128.0f) / 256.0f + 0.5f, (128.0f - 176.5f) / 256.0f + 0.5f, 0};
    static const struct {
        float x, y; // The pixel
        const float *is;
        const char *what;
    } wanted[] = {
        // With perspective, a unit 10 away is 22.17 pixels: a square with no vert from 17.1 to 61.5, each way...
        {39.5f, 39.5f, red, "a square with no vert, under a camera with perspective"},
        {20.5f, 39.5f, red, "its left edge"},
        {13.5f, 39.5f, black, "and nothing left of it"},
        {57.5f, 39.5f, red, "its right edge"},
        {65.5f, 39.5f, black, "and nothing right of it"},
        // ...and its mirror image, through a vert that writes this.clip back, from 194.5 to 238.9
        {216.5f, 39.5f, green, "a vert that writes this.clip back draws where the engine would have, under perspective"},
        {235.5f, 39.5f, green, "its right edge"},
        {242.5f, 39.5f, black, "and nothing right of it"},
        {198.5f, 39.5f, green, "its left edge"},
        {190.5f, 39.5f, black, "and nothing left of it"},
        {216.5f, 20.5f, green, "its top"},
        {216.5f, 13.5f, black, "and nothing above it"},
        {216.5f, 58.5f, green, "its bottom"},
        {216.5f, 65.5f, black, "and nothing below it"},
        // One from 110.3 to 154.6 across and 72.6 to 116.9 down, moved onto a grid of 32 pixels: 96 to 160, and 64 to 128
        {99.5f, 100.5f, blue, "a corner moved onto its frame's pixel: left of where the square was"},
        {92.5f, 100.5f, black, "and no further"},
        {157.5f, 100.5f, blue, "right of where it was"},
        {163.5f, 100.5f, black, "and no further"},
        {128.5f, 67.5f, blue, "above where it was"},
        {128.5f, 60.5f, black, "and no further"},
        {128.5f, 124.5f, blue, "below where it was"},
        {128.5f, 131.5f, black, "and no further"},
        // A unit to a pixel
        {24.5f, 176.5f, red, "a vert that writes this.clip back, a unit to a pixel"},
        {40.5f, 176.5f, black, "and nothing beside it"},
        {88.5f, 176.5f, where, "a square its vert moved 32 pixels right: its frag's this.world is still its transform's place"},
        {56.5f, 176.5f, black, "and nothing is drawn where its transform alone would have put it"},
        {120.5f, 152.5f, blue, "this.clip read after the vert changed its vertex: 24 pixels up"},
        {120.5f, 176.5f, black, "and nothing where it was"},
        {168.5f, 176.5f, green, "a square behind another, whose vert says its depth is nearer"},
        {200.5f, 176.5f, red, "and one whose vert leaves its depth: hidden"},
        {56.5f, 216.5f, green, "an Instanced draw's item, by its own transform and its own push to the right"},
        {24.5f, 216.5f, black, "and nothing where its transform is"},
        {88.5f, 216.5f, green, "the next item, pushed to the left"},
        {120.5f, 216.5f, black, "and nothing where its transform is"},
        {200.5f, 216.5f, white, "a 2D mesh's place, through its camera and Draw.Transform: 32 pixels right"},
        {168.5f, 216.5f, black, "and nothing where its transform alone would have put it"},
    };
    enum { COUNT = sizeof wanted / sizeof wanted[0] };
    local.Art.targetPass = 22;
    tide_draw_reset(&list);
    tide_gui_begin(&gui, &devices, tide_f2(SIZE, SIZE), tide_platform_measure_text);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &list, &gui);
    tide_gui_end(&gui, &list);
    local.Art.targetPass = 0;
    tide_float2 points[COUNT];
    uint32_t got[COUNT];
    for (int i = 0; i < COUNT; i++) points[i] = tide_f2(wanted[i].x, wanted[i].y);
    tide_platform_read_pixels_as(&list, SIZE, SIZE, 1.0f, points, COUNT, got);
    for (int i = 0; i < COUNT; i++) {
        const uint8_t want[3] = {byte(wanted[i].is[0]), byte(wanted[i].is[1]), byte(wanted[i].is[2])};
        bool ok = true;
        for (int c = 0; c < 3; c++) {
            const int d = (int)(got[i] >> (24 - 8 * c) & 0xFFu) - (int)want[c];
            ok = ok && d >= -3 && d <= 3;
        }
        printf("%s: %s%s (0x%08X, expected 0x%02X%02X%02XFF)\n", ok ? "ok" : "FAIL", wanted[i].what, when, (unsigned)got[i],
               want[0], want[1], want[2]);
        if (!ok) failures++;
    }
}

static int frame(void *user, const float seconds)
{
    (void)user, (void)seconds;
    static bool said;
    if (!said) printf("renderer: %s\n", tide_platform_renderer()); // What a test of one backend looks for
    said = true;
#ifdef __wasm__
    // WebGPU's device can go away, taking the pipelines made from the game's
    // shaders with it: once everything checked out, the page loses it, and
    // on the next one they're made again and draw the same.
    static int lost; // 1: lost, and waiting for another; 2: checking again
    if (lost == 1) {
        if (!tide_web_eval("Tide.gpuDevices() > 1")) return TIDE_KEEP_RUNNING;
        printf("the device was lost, and the page found another\n");
        lost = 2;
    }
    if (lost == 2) {
        run_checks(", on another device");
        run_target_checks(", on another device");
        run_screen_target_checks(", on another device");
        run_wrap_checks(", on another device");
        run_image_checks(", on another device");
        run_shadow_checks(", on another device");
        run_stencil_checks(", on another device");
        run_output_checks(", on another device");
        run_values_checks(", on another device");
        run_height_checks(", on another device");
        run_mipmap_checks(2, ", on another device");
        run_alpha_checks(", on another device");
        run_raw_checks(", on another device");
        run_cube_checks(2, ", on another device");
        run_list_checks(0, ", on another device");
        run_roll_checks(555, ", on another device");
        run_normal_checks(", on another device");
        run_model_checks(", on another device");
        run_place_checks(", on another device");
        return failures;
    }
#endif
    run_checks("");
    run_target_checks("");
    // A second frame draws the same from what the list and the renderer kept
    run_checks(", again");
    run_target_checks(", again");
    run_screen_target_checks("");
    run_wrap_checks("");
    run_wrap_checks(", again");
    run_image_checks("");
    run_image_checks(", again");
    run_shadow_checks("");
    run_shadow_checks(", again");
    run_stencil_checks("");
    run_output_checks("");
    run_values_checks("");
    run_values_checks(", again");
    run_height_checks("");
    run_mipmap_checks(2, "");
    run_mipmap_checks(2, ", again");
    run_mipmap_checks(4, ", with other pixels drawn into the target");
    run_alpha_checks("");
    run_raw_checks("");
    run_cube_checks(2, "");
    run_cube_checks(2, ", again");
    run_cube_checks(6, ", with other pixels drawn into the cube");
    run_list_checks(0, "");
    run_list_checks(0, ", again");
    run_list_checks(1, ", with another element written");
    run_list_checks(0, ", and written back");
    run_roll_checks(1234, "");
    run_roll_checks(987654321, ", the next frame");
    run_normal_checks("");
    run_model_checks("");
    run_model_checks(", again");
    run_place_checks("");
    run_place_checks(", again");
#ifdef __wasm__
    if (failures == 0 && strcmp(tide_platform_renderer(), "WebGPU") == 0) {
        tide_web_eval("Tide.loseDevice()");
        lost = 1;
        return TIDE_KEEP_RUNNING;
    }
#endif
    return failures;
}

int main(void)
{
    if (!tide_assets_load(tide_game_pack, tide_game_pack_size)) {
        printf("FAIL: the game's pack doesn't load\n");
        return 1;
    }
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world); // The entities are there
    tide_local_init(&local);
    local.Art.sourceTarget = (tide_render_target){.id = 1001, .size = {.width = 32, .height = 32}};
    local.Art.outputTarget = (tide_render_target){.id = 1002, .size = {.width = 48, .height = 24}};
    static const uint8_t pixels[] = {255, 0, 0, 255, 0, 255, 0, 255,
                                     0, 0, 255, 255, 255, 255, 255, 255};
    local.Art.uploaded = (tide_texture_ref){.image = {.pixels = pixels, .width = 2, .height = 2, .version = 1}};
    tide_platform_open(&(tide_window_desc){.title = "shaders", .width = SIZE, .height = SIZE, .hidden = true});
    tide_platform_run(frame, NULL);
}
