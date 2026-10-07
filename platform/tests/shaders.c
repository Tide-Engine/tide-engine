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
    {0, 2, -6, 0, 1, 0, 0, "pixel.position counts the screen's pixels from the left"},
    {0, 2, 6, 0, 0, 0, 1, "on both sides of the line"},
    {1, 2, 0, 0, back, back, back, "pixel.Discard leaves the pixel undrawn"},
    {1, 2, 9, 0, 1, 0, 1, "and the others drawn"},
    {2, 2, -6, 0, 1, 0, 0, "pixel.world is where the pixel is in the world"},
    {2, 2, 6, 0, 0, 1, 0, "on both sides of the line"},
    {3, 2, 0, 0, 0.4905f, 0, 0, "pixel.depth is how far it is between the camera's near and far sides"},
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
    {4, 4, 0, -6, YELLOW, "a 2D mesh on the screen's pixels: pixel.position from the top"},
    {4, 4, 0, 6, 0, 1, 1, "on both sides of the line"},
    {5, 4, 0, 0, back, back, back, "a vert moved the 2D mesh away"},
    {7, 4, 0, 0, 1, 1, 1, "to where the singleton says"},
    // The language
    {0, 5, 0, 0, 0, 1, 0, "a frag's numbers, vectors, matrices, structs, functions and loops come out as on the CPU"},
    {1, 5, 0, 0, 0, 1, 0, "a vert reads its vertex's ints, samples a grid, and changes the color its pixels get"},
    {2, 5, 0, 0, 0, 1, 0, "and so do its matrices, int vectors, assignments, counting loops, structs in structs and the rest of Math"},
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
    tide_frame(&world, NULL, 1.0f, NULL, &local, &list, &gui);
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
        tide_frame(&world, NULL, 1.0f, NULL, &local, &list, &gui);
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
            {64, 64, {0}, {0}, "a frag's pixel.position is in logical pixels in a screen target"},
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
        tide_frame(&world, NULL, 1.0f, NULL, &local, &list, &gui);
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
    tide_frame(&world, NULL, 1.0f, NULL, &local, &list, &gui);
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
    tide_frame(&world, NULL, 1.0f, NULL, &local, &list, &gui);
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
        tide_frame(&world, NULL, 1.0f, NULL, &local, &list, &gui);
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
        tide_frame(&world, NULL, 1.0f, NULL, &local, &list, &gui);
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
    tide_frame(&world, NULL, 1.0f, NULL, &local, &list, &gui);
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

// Grids of floats as textures (pass 12, `Heights` in shaders.tide): one
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
    local.Art.targetPass = 12;
    tide_draw_reset(&list);
    tide_gui_begin(&gui, &devices, tide_f2(SIZE, SIZE), tide_platform_measure_text);
    tide_frame(&world, NULL, 1.0f, NULL, &local, &list, &gui);
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
        run_height_checks(", on another device");
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
    run_height_checks("");
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
