#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/platform.h"
#ifdef __wasm__
#include "../../../platform/web/tide_web.h"
#endif

#ifdef TIDE_HAS_ASSETS
extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;
#endif

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static int logical_width, logical_height, pixel_width, pixel_height;
static float output_scale;
static PostPixelCase current_case;

#define CHECK(condition) do { if (!(condition)) { \
    printf("FAIL: post pixels line %d: %s (case %d, %dx%d at %.2fx)\n", \
        __LINE__, #condition, (int)current_case, logical_width, logical_height, output_scale); \
    return false; \
} } while (0)

typedef struct rgb { double r, g, b; } rgb;
typedef struct image { int width, height; rgb *pixels; } image;

static rgb add(rgb a, rgb b) { return (rgb){a.r + b.r, a.g + b.g, a.b + b.b}; }
static rgb scale(rgb a, double n) { return (rgb){a.r * n, a.g * n, a.b * n}; }
static rgb mix(rgb a, rgb b, double t) { return add(scale(a, 1 - t), scale(b, t)); }
static int clamp_index(int at, int count) { return at < 0 ? 0 : at >= count ? count - 1 : at; }
static int pixels(double size) { return size < 1.5 ? 1 : (int)floor(size + 0.5); }
static int quarter_width(void) { return pixels(pixel_width * 0.25); }
static int quarter_height(void) { return pixels(pixel_height * 0.25); }

static bool make_image(image *out, int width, int height)
{
    out->width = width;
    out->height = height;
    out->pixels = calloc((size_t)width * (size_t)height, sizeof *out->pixels);
    CHECK(out->pixels != NULL);
    return true;
}

static rgb at(const image *source, int x, int y)
{
    return source->pixels[(size_t)clamp_index(y, source->height) * (size_t)source->width
        + (size_t)clamp_index(x, source->width)];
}

// Independent normalized-coordinate bilinear sampler, with explicit edge
// replication. This oracle reads CPU arrays, never package math/shader results.
static rgb bilinear(const image *source, double u, double v)
{
    const double x = u * source->width - 0.5, y = v * source->height - 0.5;
    const int left = (int)floor(x), top = (int)floor(y);
    return mix(mix(at(source, left, top), at(source, left + 1, top), x - left),
               mix(at(source, left, top + 1), at(source, left + 1, top + 1), x - left), y - top);
}

static bool kernel_case(void)
{
    return current_case == PostPixelCase_Kernel || current_case == PostPixelCase_KernelEdge
        || current_case == PostPixelCase_KernelConstant;
}

static bool bloom_case(void)
{
    return current_case == PostPixelCase_ConstantBloom || current_case == PostPixelCase_Impulse
        || current_case == PostPixelCase_ImpulseEdge || current_case == PostPixelCase_BloomChain || kernel_case();
}

static bool chain_case(void)
{
    return current_case == PostPixelCase_Chain || current_case == PostPixelCase_BloomChain;
}

// The known default knee has endpoints 0 and 2. Its brightest extracted
// channel is p^2/4 inside the knee and p-1 above it. This analytic reduction
// avoids evaluating a second copy of BloomBright's general implementation.
static rgb default_bright(rgb light)
{
    const double peak = fmax(light.r, fmax(light.g, light.b));
    if (!(peak > 0)) return (rgb){0, 0, 0};
    const double extracted = peak < 2 ? peak * peak / 4 : peak - 1;
    return scale(light, extracted / peak);
}

static void extract(const image *source, image *bright)
{
    const double dx = 1.0 / source->width, dy = 1.0 / source->height;
    for (int y = 0; y < bright->height; y++) {
        for (int x = 0; x < bright->width; x++) {
            const double u = (x + 0.5) / bright->width, v = (y + 0.5) / bright->height;
            const rgb average = scale(add(add(bilinear(source, u - dx, v - dy), bilinear(source, u + dx, v - dy)),
                                          add(bilinear(source, u - dx, v + dy), bilinear(source, u + dx, v + dy))), 0.25);
            bright->pixels[(size_t)y * (size_t)bright->width + (size_t)x] = default_bright(average);
        }
    }
}

// Unpaired nine-tap reference. At unit radius the actual five-fetch package
// shader must give this result, including folded edge weights and odd extents.
static void convolve(const image *source, image *destination, bool horizontal)
{
    static const int weights[9] = {6, 20, 45, 72, 84, 72, 45, 20, 6};
    for (int y = 0; y < source->height; y++) {
        for (int x = 0; x < source->width; x++) {
            rgb sum = {0, 0, 0};
            for (int tap = -4; tap <= 4; tap++)
                sum = add(sum, scale(at(source, x + (horizontal ? tap : 0), y + (horizontal ? 0 : tap)),
                                     weights[tap + 4] / 370.0));
            destination->pixels[(size_t)y * (size_t)source->width + (size_t)x] = sum;
        }
    }
}

static double display(double linear)
{
    const double mapped = linear / (1 + linear);
    return mapped <= 0.0031308 ? 12.92 * mapped : 1.055 * pow(mapped, 1 / 2.4) - 0.055;
}

static int byte(double value)
{
    return !(value > 0) ? 0 : value >= 1 ? 255 : (int)floor(value * 255 + 0.5);
}

static bool compare_pixel(uint32_t actual, rgb expected, bool linear, int x, int y)
{
    const int want[4] = {byte(linear ? expected.r : display(expected.r)),
        byte(linear ? expected.g : display(expected.g)), byte(linear ? expected.b : display(expected.b)), 255};
    for (int channel = 0; channel < 4; channel++) {
        const int got = (int)(actual >> (24 - channel * 8) & 255u);
        // Half-float intermediates plus RGBA8 readback can move a channel by a
        // step; this tolerance remains far below every controlled fault witness.
        if (abs(got - want[channel]) > 2) {
            printf("FAIL: post pixel (%d,%d), channel %d: got %d expected %d, rgba %08x\n",
                x, y, channel, got, want[channel], (unsigned)actual);
            return false;
        }
    }
    return true;
}

// Texture.size is captured while recording. Supply the same synthetic output
// extent read_pixels_as will use, rather than the hidden host window's extent.
static tide_int2 metadata_pixels(tide_target_size size)
{
    if (size.window > 0)
        return tide_i2(pixels(pixel_width * (double)size.window), pixels(pixel_height * (double)size.window));
    const double sx = size.scale > 0 ? size.scale : (double)pixel_width / logical_width;
    const double sy = size.scale > 0 ? size.scale : (double)pixel_height / logical_height;
    return tide_i2(pixels(size.width * sx), pixels(size.height * sy));
}

static uint64_t texture_target(const tide_draw_run *run, uint32_t binding)
{
    if (binding >= run->shader->texture_count || !run->textures[binding]) return 0;
    const tide_draw_texture *texture = &draw.textures[run->textures[binding] - 1u];
    return texture->space == TIDE_PIXELS_TARGET ? tide_render_target_whole(texture->id) : 0;
}

static bool inspect_commands(bool suppressed)
{
    const Render3D_Renderer *renderer = &local.Render3D_Renderer;
    uint64_t destination = 0;
    int presentations = 0, blits = 0, arithmetic = 0, extracted = 0, blurred = 0, composed = 0;
    const bool bloom_chain = current_case == PostPixelCase_BloomChain;
    // Bloom publishes A before the first callback; three committed callbacks
    // must then route A->B->A->B, including across default/false bypasses.
    const uint64_t a = renderer->postA.id, b = renderer->postB.id;
    const uint64_t chain_source[3] = {bloom_chain ? a : renderer->hdr.id, bloom_chain ? b : a, bloom_chain ? a : b};
    const uint64_t chain_destination[3] = {bloom_chain ? b : a, bloom_chain ? a : b, bloom_chain ? b : a};
    for (uint32_t i = 0; i < draw.count; i++) {
        const tide_draw_command *command = &draw.commands[i];
        if (command->kind == TIDE_DRAW_TARGET) {
            CHECK(command->target <= draw.target_count);
            destination = command->target ? draw.targets[command->target - 1u].id : 0;
        }
        if (command->kind != TIDE_DRAW_BLIT) continue;
        CHECK(command->shaded.run < draw.run_count);
        const tide_draw_run *run = &draw.runs[command->shaded.run];
        CHECK(run->shader != NULL);
        blits++;
        if (!destination) {
            presentations++;
            if (bloom_chain) {
                CHECK(extracted == 1 && blurred == 4 && composed == 1 && arithmetic == 3);
                CHECK(texture_target(run, 0) == b);
            }
        }
        for (uint32_t binding = 0; binding < run->shader->texture_count; binding++) {
            CHECK(run->textures[binding] <= draw.texture_count);
            const uint64_t source = texture_target(run, binding);
            if (source) CHECK(source != destination);
            if (strstr(run->shader->name, "BlurBloom") || strstr(run->shader->name, "ExtractBloom")) {
                CHECK(tide_sampling_filter(run->sampling, binding) == TIDE_FILTER_BILINEAR);
                CHECK(tide_sampling_wrap(run->sampling, binding) == TIDE_WRAP_CLAMP);
            }
        }
        if (strstr(run->shader->name, "PostPixelArithmetic")) {
            if (bloom_chain) CHECK(extracted == 1 && blurred == 4 && composed == 1);
            const int entry = chain_case() ? arithmetic : 0;
            CHECK(entry < 3 && texture_target(run, 0) == chain_source[entry]);
            CHECK(destination == chain_destination[entry]);
            arithmetic++;
        }
        if (strstr(run->shader->name, "ExtractBloom")) extracted++;
        if (strstr(run->shader->name, "BlurBloom")) blurred++;
        if (strstr(run->shader->name, "CompositeBloom")) {
            if (bloom_chain) CHECK(arithmetic == 0 && extracted == 1 && blurred == 4);
            CHECK(texture_target(run, 0) == renderer->hdr.id);
            CHECK(texture_target(run, 1) == renderer->bloomA.id && destination == renderer->postA.id);
            composed++;
        }
    }
    CHECK(presentations == (suppressed ? 0 : 1));
    CHECK(extracted == (bloom_case() ? 1 : 0) && composed == extracted);
    CHECK(blurred == (kernel_case() ? 2 : current_case == PostPixelCase_ConstantBloom || bloom_chain ? 4 : bloom_case() ? 2 : 0));
    CHECK(arithmetic == (chain_case() ? 3 :
        current_case == PostPixelCase_DefaultBypass || current_case == PostPixelCase_FalseBypass ? 1 : 0));
    const int expected = current_case == PostPixelCase_Plain ? 2 :
        current_case == PostPixelCase_ConstantBloom ? 8 : bloom_chain ? 11 : current_case == PostPixelCase_Chain ? 5 :
        current_case == PostPixelCase_DefaultBypass || current_case == PostPixelCase_FalseBypass ? 3 :
        kernel_case() ? 7 : 6;
    CHECK(blits == expected - (suppressed ? 1 : 0));
    CHECK(renderer->postPendingSlot == 0);
    if (bloom_chain) CHECK(renderer->postSourceSlot == 2 && renderer->output.target.id == b);
    return true;
}

static void configure_case(PostPixelCase scenario, int width, int height, float scale_factor)
{
    current_case = scenario;
    logical_width = width;
    logical_height = height;
    output_scale = scale_factor;
    pixel_width = pixels((double)width * scale_factor);
    pixel_height = pixels((double)height * scale_factor);
    PostPixelProbe *probe = &local.PostPixelProbe;
    probe->scenario = scenario;
    probe->sourcePixels = tide_i2(pixel_width, pixel_height);
    probe->quarterPixels = tide_i2(quarter_width(), quarter_height());
    probe->sceneColor = kernel_case() ? tide_f3(0, 0, 0) : tide_f3(4, 2, 1);
    probe->seedColor = scenario == PostPixelCase_KernelConstant ? tide_f3(4, 2, 1) : tide_f3(1, 0.5f, 0.25f);
    probe->diagnosticGain = scenario == PostPixelCase_KernelEdge ? 2 : scenario == PostPixelCase_KernelConstant ? 0.125f : 16;
    probe->exposure = scenario == PostPixelCase_Chain ? 0.75f : scenario == PostPixelCase_BloomChain ? 0.5f : 1;
    const bool edge = scenario == PostPixelCase_KernelEdge || scenario == PostPixelCase_ImpulseEdge;
    const int w = kernel_case() ? quarter_width() : pixel_width;
    const int h = kernel_case() ? quarter_height() : pixel_height;
    probe->impulse = edge ? tide_i2(0, 0) : tide_i2(w / 2, h / 2);
    if (scenario == PostPixelCase_Impulse || scenario == PostPixelCase_ImpulseEdge)
        probe->sceneColor = tide_f3(64, 32, 16);
}

static bool record(void)
{
    const uint64_t hash = tide_world_hash(&world);
    const Render3D_Renderer old = local.Render3D_Renderer;
    tide_draw_reset(&draw);
    tide_draw_screen_size(&draw, tide_f2((float)logical_width, (float)logical_height));
    draw.target_pixels = metadata_pixels;
    gui.width = (float)logical_width;
    gui.height = (float)logical_height;
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    CHECK(tide_world_hash(&world) == hash);
    const Render3D_Renderer *renderer = &local.Render3D_Renderer;
    if (!local.PostPixelProbe.disabled && !local.PostPixelProbe.releaseScratch) {
        if (old.ready) CHECK(renderer->hdr.id == old.hdr.id);
        if (old.postReady) CHECK(renderer->postA.id == old.postA.id && renderer->postB.id == old.postB.id);
        if (old.bloomReady && bloom_case())
            CHECK(renderer->bloomA.id == old.bloomA.id && renderer->bloomB.id == old.bloomB.id);
    }
    return true;
}

static bool check_pixels(void)
{
    image source = {0}, bright = {0}, scratch = {0};
    const bool diagnostic = kernel_case();
    const int w = diagnostic ? quarter_width() : pixel_width;
    const int h = diagnostic ? quarter_height() : pixel_height;
    CHECK(make_image(&source, w, h));
    CHECK(make_image(&bright, quarter_width(), quarter_height()));
    CHECK(make_image(&scratch, bright.width, bright.height));
    const PostPixelProbe *probe = &local.PostPixelProbe;
    const rgb light = diagnostic ? (rgb){probe->seedColor.x, probe->seedColor.y, probe->seedColor.z}
                                : (rgb){probe->sceneColor.x, probe->sceneColor.y, probe->sceneColor.z};
    const bool impulse = current_case == PostPixelCase_Kernel || current_case == PostPixelCase_KernelEdge
        || current_case == PostPixelCase_Impulse || current_case == PostPixelCase_ImpulseEdge;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++)
            source.pixels[(size_t)y * (size_t)w + (size_t)x] = !impulse ||
                (x == probe->impulse.x && y == probe->impulse.y) ? light : (rgb){0, 0, 0};
    }
    if (diagnostic) {
        convolve(&source, &scratch, true);
        convolve(&scratch, &bright, false);
    } else if (bloom_case()) {
        extract(&source, &bright);
        const int iterations = current_case == PostPixelCase_ConstantBloom || current_case == PostPixelCase_BloomChain ? 2 : 1;
        for (int i = 0; i < iterations; i++) {
            convolve(&bright, &scratch, true);
            convolve(&scratch, &bright, false);
        }
    }
    const int count = w * h;
    tide_float2 *points = malloc((size_t)count * sizeof *points);
    uint32_t *actual = malloc((size_t)count * sizeof *actual);
    CHECK(points != NULL && actual != NULL);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const int at_pixel = y * w + x;
            // Sample the center of each quarter texel's displayed region.
            points[at_pixel] = diagnostic
                ? tide_f2((float)floor((x + 0.5) * pixel_width / w) + 0.5f,
                          (float)floor((y + 0.5) * pixel_height / h) + 0.5f)
                : tide_f2((float)x + 0.5f, (float)y + 0.5f);
        }
    }
    tide_platform_read_pixels_as(&draw, logical_width, logical_height, output_scale, points, count, actual);
    bool good = true;
    double kernel_sum = 0;
    for (int y = 0; y < h && good; y++) {
        for (int x = 0; x < w; x++) {
            rgb expected = at(&source, x, y);
            if (diagnostic) {
                expected = scale(at(&bright, x, y), probe->diagnosticGain);
                kernel_sum += (double)(actual[y * w + x] >> 24) / (255 * probe->diagnosticGain);
            } else {
                if (bloom_case()) expected = add(expected, scale(bilinear(&bright, (x + 0.5) / w, (y + 0.5) / h),
                    current_case == PostPixelCase_BloomChain ? 1 : 0.25));
                if (chain_case()) expected = scale(add(scale(expected, 2), (rgb){3, 3, 3}), 0.5);
                if (current_case == PostPixelCase_DefaultBypass || current_case == PostPixelCase_FalseBypass)
                    expected = add(expected, (rgb){3, 3, 3});
                if (current_case == PostPixelCase_BloomChain && x == 0 && y == 0) {
                    // Scene=(4,2,1), knee=(3,1.5,.75), intensity=1;
                    // then (HDR*2+3)*.5. Exposure has not entered extraction,
                    // convolution, composition or callback arithmetic.
                    CHECK(fabs(expected.r - 8.5) < 1e-12);
                    CHECK(fabs(expected.g - 5) < 1e-12);
                    CHECK(fabs(expected.b - 3.25) < 1e-12);
                }
                expected = scale(expected, probe->exposure);
            }
            if (!compare_pixel(actual[y * w + x], expected, diagnostic, x, y)) { good = false; break; }
        }
    }
    // Byte readback quantization bounds the sum's accuracy. Per-texel checks
    // above are stronger; this separately witnesses normalized impulse energy.
    if (current_case == PostPixelCase_Kernel && fabs(kernel_sum - 1) > 0.03) {
        printf("FAIL: post blur impulse energy %.9g, expected one\n", kernel_sum);
        good = false;
    }
    free(points);
    free(actual);
    free(source.pixels);
    free(bright.pixels);
    free(scratch.pixels);
    CHECK(good);
    return true;
}

static bool exercise_case(PostPixelCase scenario, int width, int height, float scale_factor)
{
    configure_case(scenario, width, height, scale_factor);
    CHECK(record());
    CHECK(inspect_commands(false));
    CHECK(check_pixels());
    return true;
}

static bool run_cases(void)
{
    CHECK(fabs(display(4) - 0.9063317533440594) < 1e-12);
    CHECK(fabs(default_bright((rgb){1, 0.5, 0.25}).r - 0.25) < 1e-12);
    // BloomChain's independent constant-color landmarks after its one final
    // exposure/transfer. Premature exposure in extraction instead gives
    // (227,213,200) at .5 and (251,247,243) at 2: both exceed the old tolerance.
    CHECK(byte(display(8.5 * 0.5)) == 232 && byte(display(5 * 0.5)) == 220 && byte(display(3.25 * 0.5)) == 206);
    CHECK(byte(display(8.5 * 2)) == 249 && byte(display(5 * 2)) == 245 && byte(display(3.25 * 2)) == 239);
    static const struct { int width, height; float scale; } sizes[] = {
        {80, 60, 1}, {83, 59, 1.5f}, {83, 59, 2}, {80, 60, 1}
    };
    const bool fresh = !local.Render3D_Renderer.ready;
    for (unsigned size = 0; size < sizeof sizes / sizeof sizes[0]; size++) {
        for (int scenario = PostPixelCase_Plain; scenario <= PostPixelCase_ImpulseEdge; scenario++) {
            CHECK(exercise_case((PostPixelCase)scenario, sizes[size].width, sizes[size].height, sizes[size].scale));
            if (fresh && size == 0 && scenario == PostPixelCase_Plain) {
                const Render3D_Renderer *renderer = &local.Render3D_Renderer;
                CHECK(!renderer->postReady && !renderer->bloomReady);
                CHECK(!renderer->postA.id && !renderer->postB.id && !renderer->bloomA.id && !renderer->bloomB.id);
            }
        }
        CHECK(exercise_case(PostPixelCase_BloomChain, sizes[size].width, sizes[size].height, sizes[size].scale));
        // Hold the same HDR/bloom/callback inputs and change exposure only.
        // Both readbacks use the unchanged tolerance of two channel values.
        local.PostPixelProbe.exposure = 2;
        CHECK(record());
        CHECK(inspect_commands(false));
        CHECK(check_pixels());
    }
    // Suppression leaves all upstream chain work and its output intact.
    configure_case(PostPixelCase_Chain, 83, 59, 1.5f);
    local.PostPixelProbe.suppressPresentation = true;
    CHECK(record());
    CHECK(inspect_commands(true));
    CHECK(local.Render3D_Renderer.ready && local.Render3D_Renderer.output.target.id == local.Render3D_Renderer.postA.id);
    local.PostPixelProbe.suppressPresentation = false;
    // Dispose/recreate scratch with an already-ready scene, as an old layout
    // gaining independently initialized post resources would do.
    const uint64_t old_a = local.Render3D_Renderer.postA.id;
    local.PostPixelProbe.releaseScratch = true;
    CHECK(exercise_case(PostPixelCase_ConstantBloom, 83, 59, 1.5f));
    CHECK(local.Render3D_Renderer.postA.id != old_a && !tide_render_target_live(old_a));
    local.PostPixelProbe.releaseScratch = false;
    const Render3D_Renderer *renderer = &local.Render3D_Renderer;
    const uint64_t old_hdr = renderer->hdr.id, a = renderer->postA.id, b = renderer->postB.id;
    const uint64_t bright = renderer->bloomA.id, spread = renderer->bloomB.id;
    local.PostPixelProbe.disabled = true;
    CHECK(record());
    CHECK(!renderer->ready && !renderer->postReady && !renderer->bloomReady);
    CHECK(!tide_render_target_live(old_hdr) && !tide_render_target_live(a) && !tide_render_target_live(b));
    CHECK(!tide_render_target_live(bright) && !tide_render_target_live(spread));
    local.PostPixelProbe.disabled = false;
    CHECK(exercise_case(PostPixelCase_Impulse, 83, 59, 1.5f));
    CHECK(renderer->hdr.id != old_hdr && renderer->postA.id != a && renderer->bloomA.id != bright);
    return true;
}

static void cleanup(void)
{
    local.PostPixelProbe.disabled = true;
    (void)record();
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_render_targets_free();
}

static int frame(void *context, float seconds)
{
    (void)context;
    (void)seconds;
    static bool first = true;
#ifdef __wasm__
    static bool lost;
    static uint64_t retained[5];
    if (lost) {
        if (!tide_web_eval("Tide.gpuDevices() > 1")) return TIDE_KEEP_RUNNING;
        const Render3D_Renderer *renderer = &local.Render3D_Renderer;
        if (renderer->hdr.id != retained[0] || renderer->postA.id != retained[1] || renderer->postB.id != retained[2]
            || renderer->bloomA.id != retained[3] || renderer->bloomB.id != retained[4]) return 1;
        puts("postprocessing: rechecking retained targets after device loss");
        // First redraw the exact retained target set before any case disables
        // bloom and releases a quarter-resolution target.
        if (!exercise_case(PostPixelCase_Impulse, 83, 59, 1.5f)) { cleanup(); return 1; }
        if (!run_cases()) { cleanup(); return 1; }
        cleanup();
        puts("ok: Render3D post pixels after device loss");
        return 0;
    }
#endif
    if (first) {
        first = false;
        printf("renderer: %s\n", tide_platform_renderer());
        tide_world_init(&world, 1.0f / 60.0f);
        tide_world_tick(&world);
        tide_local_init(&local);
        if (!run_cases()) { cleanup(); return 1; }
#ifdef __wasm__
        if (strcmp(tide_platform_renderer(), "WebGPU") == 0) {
            const Render3D_Renderer *renderer = &local.Render3D_Renderer;
            retained[0] = renderer->hdr.id; retained[1] = renderer->postA.id; retained[2] = renderer->postB.id;
            retained[3] = renderer->bloomA.id; retained[4] = renderer->bloomB.id;
            (void)tide_web_eval("Tide.loseDevice()");
            lost = true;
            return TIDE_KEEP_RUNNING;
        }
#endif
        cleanup();
    }
    puts("ok: Render3D post pixels, independent HDR/knee/kernel/edge/odd-size/chain oracles, retained resize/scale and lifecycle");
    return 0;
}

int main(void)
{
#ifdef TIDE_HAS_ASSETS
    if (!tide_assets_load(tide_game_pack, tide_game_pack_size)) return 1;
#endif
    tide_platform_open(&(tide_window_desc){.title = "Render3D postprocessing", .width = 80, .height = 60, .hidden = true});
    tide_platform_run(frame, NULL);
}
