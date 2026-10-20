// versus_draw: tide_platform_draw --bench's workloads, drawn by hand against
// OpenGL 3.3 core (draw_gl.c) or Vulkan 1.3 (draw_vk.c), and timed at the same
// boundaries, so Tide's renderer has a baseline: what an expert's C costs for
// the same pixels on the same machine.
//
// Each workload has 20 warmup frames and 100 measured ones, in a 256 by 352
// target. A frame is timed in two parts, as Tide's are:
// - record: the CPU writes the frame's instances where the GPU reads them
//   (OpenGL: a buffer it maps; Vulkan: host-visible memory kept mapped). Tide's
//   record is its draw list; its upload is in its render part instead, so the
//   two parts split the work differently, and the totals compare.
// - render + readback: the frame is drawn, cleared to black first, and its pixel
//   (1, 1) read back, waiting for the GPU (Tide reads the whole target back).
// TIDE_BACKEND says which API (vulkan or opengl); without it, Vulkan where the
// machine has 1.3, as Tide's renderer picks.

#include "versus_draw.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "tide/draw.h"
#include "tide/math.h"
#include "tide/page.h"
#include "tide/time.h"

#define WARMUP 20
#define FRAMES 100
#define QUICK_WARMUP 2
#define QUICK_FRAMES 3

// As tide_platform_draw names them, but the last, which it doesn't draw.
static const char *const names[VD_KINDS] = {
    "nothing", "65536 rects", "65536 circles", "65536 textured quads", "65536 instances of a cube", "65536 round rects",
    "65536 instances of a cube, a matrix each"};
// Where each is in tide_platform_draw's list (its --dump-frames files): the last draws its fifth's pixels.
static const char *const tide_numbers[VD_KINDS] = {"0", "1", "2", "4", "5", "6", "5"};

size_t vd_instance_size(const vd_kind kind)
{
    switch (kind) {
    case VD_RECTS:
    case VD_CIRCLES:
    case VD_QUADS:
        return sizeof(vd_shape);
    case VD_ROUND_RECTS:
        return sizeof(vd_round);
    case VD_CUBES:
        return sizeof(vd_place);
    case VD_CUBE_MATRICES:
        return 16u * sizeof(float);
    default:
        return 0;
    }
}

// A color channel of i / 256 as Tide's renderer makes it a byte (to_rgba in
// platform/src/platform.c), from the colors tide_platform_draw gives its
// shapes: the column's red, the row's green.
static uint8_t channel[256];

static void make_channels(void)
{
    for (int i = 0; i < 256; i++) {
        const float v = (float)i / 256.0f;
        channel[i] = !(v > 0.0f) ? 0 : v >= 1.0f ? 255 : (uint8_t)(v * 255.0f + 0.5f);
    }
}

// The color of the shape at (column, row): blue is 0.5, 128 as a byte.
static inline uint32_t color_at(const uint32_t column, const uint32_t row)
{
    return (uint32_t)channel[column] | (uint32_t)channel[row] << 8 | 128u << 16 | 255u << 24;
}

// A frame's instances, written in order straight into what the GPU reads
// (memory a CPU should only write, front to back). The 2D shapes are a pixel
// each, a row of 256 for each of the target's top 256 rows, in window pixels
// from the top left; the cubes stand on the ground in the same order.
static void record(const vd_kind kind, void *to)
{
    switch (kind) {
    case VD_RECTS:
    case VD_QUADS:
    case VD_CIRCLES: {
        const float half = kind == VD_CIRCLES ? 0.6f : 0.5f; // A circle's radius, or half a pixel
        vd_shape *out = to;
        for (uint32_t row = 0; row < 256u; row++) {
            const float y = (float)row + 0.5f;
            for (uint32_t column = 0; column < 256u; column++, out++) {
                *out = (vd_shape){(float)column + 0.5f, y, half, half, color_at(column, row)};
            }
        }
        break;
    }
    case VD_ROUND_RECTS: {
        vd_round *out = to;
        for (uint32_t row = 0; row < 256u; row++) {
            const float y = (float)row + 0.5f;
            for (uint32_t column = 0; column < 256u; column++, out++) {
                *out = (vd_round){(float)column + 0.5f, y, 0.5f, 0.5f, 0.3f, color_at(column, row)};
            }
        }
        break;
    }
    case VD_CUBES: {
        vd_place *out = to;
        for (uint32_t row = 0; row < 256u; row++) {
            for (uint32_t column = 0; column < 256u; column++, out++) *out = (vd_place){(float)column, 0.0f, (float)row};
        }
        break;
    }
    case VD_CUBE_MATRICES: {
        float *out = to;
        for (uint32_t row = 0; row < 256u; row++) {
            for (uint32_t column = 0; column < 256u; column++, out += 16) {
                const float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, (float)column, 0, (float)row, 1};
                memcpy(out, m, sizeof m);
            }
        }
        break;
    }
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// What the pixel read back should be, for --quick: the top left's second
// pixel, under the second row's second shape.

static uint32_t packed(const double r, const double g, const double b, const double a)
{
    const double c[4] = {r, g, b, a};
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) v = v << 8 | (uint32_t)(c[i] < 0.0 ? 0.0 : c[i] > 1.0 ? 255.0 : c[i] * 255.0 + 0.5);
    return v;
}

static bool near(const uint32_t a, const uint32_t b, const int slack)
{
    for (int shift = 0; shift < 32; shift += 8) {
        const int d = (int)(a >> shift & 0xFFu) - (int)(b >> shift & 0xFFu);
        if (d > slack || d < -slack) return false;
    }
    return true;
}

static const uint8_t texture[2 * 2 * 4] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};

static bool expected(const vd_kind kind, const uint32_t got)
{
    const double r = channel[1] / 255.0, g = channel[1] / 255.0, b = 128.0 / 255.0; // Shape 257's
    switch (kind) {
    case VD_NOTHING:
        return got == 0x000000FFu;
    case VD_RECTS:
    case VD_ROUND_RECTS: // Its neighbours' corners don't reach it
        return got == packed(r, g, b, 1.0);
    case VD_CIRCLES: {
        // Its own covers it, then the next one's (to its right) and the one below's each cover a tenth of it
        double c[3] = {r, g, b};
        const double right[3] = {channel[2] / 255.0, channel[1] / 255.0, b}, below[3] = {channel[1] / 255.0, channel[2] / 255.0, b};
        for (int i = 0; i < 3; i++) c[i] = right[i] * 0.1 + c[i] * 0.9;
        for (int i = 0; i < 3; i++) c[i] = below[i] * 0.1 + c[i] * 0.9;
        return near(got, packed(c[0], c[1], c[2], 1.0), 2);
    }
    case VD_QUADS: // Its color times one of the texture's pixels: the GPU picks which, at the quad's middle
        for (int i = 0; i < 4; i++) {
            const uint8_t *t = texture + i * 4;
            if (near(got, packed(r * t[0] / 255.0, g * t[1] / 255.0, b * t[2] / 255.0, 1.0), 1)) return true;
        }
        return false;
    default: // The cubes' are checked against each other
        return true;
    }
}

// ---------------------------------------------------------------------------
// Timing and saving, as tide_platform_draw does both.

typedef struct timing {
    double median_ms, p95_ms;
} timing;

static int compare(const void *a, const void *b)
{
    const double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

static timing summarize(double *ms, const int count)
{
    qsort(ms, (size_t)count, sizeof *ms, compare);
    return (timing){(ms[count / 2 - 1] + ms[count / 2]) / 2.0, ms[(count * 95 + 99) / 100 - 1]};
}

static void json_text(FILE *f, const char *text)
{
    fputc('"', f);
    for (const unsigned char *c = (const unsigned char *)text; *c; c++) {
        if (*c == '"' || *c == '\\') fprintf(f, "\\%c", *c);
        else if (*c < 0x20) fprintf(f, "\\u%04x", *c);
        else fputc(*c, f);
    }
    fputc('"', f);
}

static void save_row(FILE *f, const char *label, const vd_api *api, const char *device, const vd_kind kind,
                     const timing recording, const timing rendering, const timing total)
{
    char date[32] = "";
    const time_t now = time(NULL);
    const struct tm *utc = gmtime(&now);
    if (utc) strftime(date, sizeof date, "%Y-%m-%dT%H:%M:%SZ", utc);
#if defined(_WIN32)
    const char *os = "windows";
#elif defined(__APPLE__)
    const char *os = "macos";
#else
    const char *os = "linux";
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
    const char *arch = "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
    const char *arch = "x86_64";
#else
    const char *arch = "unknown";
#endif
#if defined(__OPTIMIZE__)
    const char *optimized = "true";
#else
    const char *optimized = "false";
#endif
    const char *revision = getenv("TIDE_BENCH_REVISION");
    const char *machine = getenv("TIDE_BENCH_MACHINE");
    fprintf(f, "{\"benchmark\":\"draw\",\"label\":");
    json_text(f, label);
    fprintf(f, ",\"date\":\"%s\",\"os\":\"%s\",\"arch\":\"%s\",\"optimized\":%s,\"compiler\":", date, os, arch, optimized);
    json_text(f, __clang_version__);
    fprintf(f, ",\"revision\":");
    json_text(f, revision ? revision : "unknown");
    fprintf(f, ",\"machine\":");
    json_text(f, machine ? machine : "unknown");
    fprintf(f, ",\"renderer\":");
    json_text(f, api->name);
    fprintf(f, ",\"way\":\"C\",\"api\":");
    json_text(f, api->api);
    fprintf(f, ",\"device\":");
    json_text(f, device);
    fprintf(f, ",\"workload\":");
    json_text(f, names[kind]);
    fprintf(f, ",\"count\":%u,\"width\":%d,\"height\":%d,\"warmup\":%d,\"frames\":%d,\"competitive_baseline\":true",
            kind ? VD_COUNT : 0u, VD_WIDTH, VD_HEIGHT, WARMUP, FRAMES);
    fprintf(f, ",\"record_ms\":{\"median\":%.6g,\"p95\":%.6g},\"render_readback_ms\":{\"median\":%.6g,\"p95\":%.6g},"
               "\"total_ms\":{\"median\":%.6g,\"p95\":%.6g}}\n",
            recording.median_ms, recording.p95_ms, rendering.median_ms, rendering.p95_ms, total.median_ms, total.p95_ms);
}

// ---------------------------------------------------------------------------
// Frames as files, to compare with tide_platform_draw's (--dump-frames there).

static bool write_ppm(const char *path, const uint8_t *rgba)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    fprintf(f, "P6\n%d %d\n255\n", VD_WIDTH, VD_HEIGHT);
    for (size_t i = 0; i < (size_t)VD_WIDTH * VD_HEIGHT; i++) fwrite(rgba + i * 4u, 1, 3, f);
    return fclose(f) == 0;
}

static bool read_ppm(const char *path, uint8_t *rgb)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    int width = 0, height = 0, most = 0;
    const bool ok = fscanf(f, "P6 %d %d %d", &width, &height, &most) == 3 && fgetc(f) != EOF && width == VD_WIDTH
                 && height == VD_HEIGHT && most == 255 && fread(rgb, 3, (size_t)width * (size_t)height, f) == (size_t)width * (size_t)height;
    fclose(f);
    return ok;
}

// How the frame differs from Tide's: pixels that differ at all, and by more than 2 of 255, and the most.
static void compare_frame(const char *tide_prefix, const vd_kind kind, const uint8_t *rgba)
{
    static uint8_t rgb[VD_WIDTH * VD_HEIGHT * 3];
    char path[1024];
    snprintf(path, sizeof path, "%s-%s.ppm", tide_prefix, tide_numbers[kind]);
    if (!read_ppm(path, rgb)) {
        printf("  (no frame of Tide's at %s to compare with)\n", path);
        return;
    }
    uint32_t differ = 0, beyond = 0;
    int most = 0;
    for (size_t i = 0; i < (size_t)VD_WIDTH * VD_HEIGHT; i++) {
        int worst = 0;
        for (int c = 0; c < 3; c++) {
            const int d = abs((int)rgba[i * 4u + (size_t)c] - (int)rgb[i * 3u + (size_t)c]);
            if (d > worst) worst = d;
        }
        differ += worst > 0;
        beyond += worst > 2;
        if (worst > most) most = worst;
    }
    printf("  against Tide's frame: %u of %d pixels differ, %u by more than 2 of 255, by %d at most\n", differ,
           VD_WIDTH * VD_HEIGHT, beyond, most);
}

// ---------------------------------------------------------------------------

static void on_glfw_error(const int code, const char *text)
{
    fprintf(stderr, "versus_draw: GLFW (%d): %s\n", code, text);
}

static int usage(void)
{
    printf("usage: versus_draw [--save <label>] [--quick] [--dump <prefix>] [--compare <prefix>]\n"
           "  Draws tide_platform_draw --bench's workloads by hand, with the API TIDE_BACKEND says\n"
           "  (vulkan or opengl; Vulkan where there is one, without it), and times them the same way.\n"
           "  --save <label>     append JSONL to bench/results/draw.jsonl, or print it when unavailable\n"
           "  --quick            a few frames of each, checking the pixels read back (a test)\n"
           "  --dump <prefix>    write each workload's last frame to <prefix>-<n>.ppm, n as tide_platform_draw numbers it\n"
           "  --compare <prefix> compare each workload's last frame with tide_platform_draw --dump-frames <prefix>'s\n"
           "Set TIDE_BENCH_REVISION and TIDE_BENCH_MACHINE to identify the build and CPU/GPU.\n");
    return 0;
}

int main(const int argc, char **argv)
{
    const char *label = NULL, *dump = NULL, *against = NULL;
    bool quick = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--save") == 0 && i + 1 < argc) label = argv[++i];
        else if (strcmp(argv[i], "--dump") == 0 && i + 1 < argc) dump = argv[++i];
        else if (strcmp(argv[i], "--compare") == 0 && i + 1 < argc) against = argv[++i];
        else if (strcmp(argv[i], "--quick") == 0) quick = true;
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) return usage();
        else {
            fprintf(stderr, "versus_draw: unknown option or missing value: '%s'\n", argv[i]);
            return 2;
        }
    }
    if (label && quick) {
        fprintf(stderr, "versus_draw: --quick runs too few frames to save\n");
        return 2;
    }
    (void)tide_hidden(); // Its program start turns stop dialogs off, in a run nobody watches
    const char *backend = getenv("TIDE_BACKEND");
    const bool asks_vulkan = backend && strcmp(backend, "vulkan") == 0, asks_gl = backend && strcmp(backend, "opengl") == 0;
    if (backend && *backend && !asks_vulkan && !asks_gl) {
        fprintf(stderr, "versus_draw: TIDE_BACKEND is \"%s\", where it's vulkan or opengl\n", backend);
        return 2;
    }
    glfwSetErrorCallback(on_glfw_error);
    if (!glfwInit()) {
        fprintf(stderr, "versus_draw: no desktop to draw on\n");
        return 1;
    }
    make_channels();
    // tide_platform_draw's cubes' camera, fitted to the target, as its renderer fits it
    const tide_float4x4 camera = tide_draw_camera_fit(
        tide_draw_camera_3d_matrix(tide_f3(128, 60, -40), tide_axisangle_q(tide_f3(1, 0, 0), 0.6f), 70.0f), (float)VD_WIDTH,
        (float)VD_HEIGHT);
    float matrix[16];
    memcpy(matrix, &camera, sizeof matrix);
    char device[256] = "";
    const vd_api *api = asks_gl ? &vd_gl : &vd_vulkan;
    if (!api->open(matrix, texture, device, sizeof device)) {
        if (asks_vulkan || asks_gl || !vd_gl.open(matrix, texture, device, sizeof device)) {
            fprintf(stderr, "versus_draw: no %s to draw with\n", asks_gl ? "OpenGL 3.3" : asks_vulkan ? "Vulkan 1.3" : "GPU API");
            glfwTerminate();
            return 1;
        }
        api = &vd_gl;
    }
    const int warmup = quick ? QUICK_WARMUP : WARMUP, frames = quick ? QUICK_FRAMES : FRAMES;
    printf("versus_draw: %d warmup and %d measured frames per workload, by hand in %s on %s\n", warmup, frames, api->api, device);
    FILE *out = NULL;
    if (label) {
        out = fopen(TIDE_BENCH_RESULTS "/draw.jsonl", "ab");
        if (!out) printf("couldn't save to " TIDE_BENCH_RESULTS "/draw.jsonl; printing JSONL instead\n");
    }
    static double record_ms[FRAMES], render_ms[FRAMES], total_ms[FRAMES];
    static uint8_t frame_pixels[VD_WIDTH * VD_HEIGHT * 4];
    uint32_t cube_pixel = 0;
    int failures = 0;
    for (int k = 0; k < VD_KINDS; k++) {
        const vd_kind kind = (vd_kind)k;
        const uint32_t count = kind ? VD_COUNT : 0u;
        const size_t bytes = (size_t)count * vd_instance_size(kind);
        uint32_t pixel = 0;
        for (int frame = 0; frame < warmup + frames; frame++) {
            const uint64_t start = tide_time_now_ns();
            if (bytes) {
                record(kind, api->instances(kind, bytes));
                api->written(kind, bytes);
            }
            const uint64_t recorded = tide_time_now_ns();
            pixel = api->render(kind, count);
            const uint64_t rendered = tide_time_now_ns();
            if (frame >= warmup) {
                record_ms[frame - warmup] = (double)(recorded - start) / 1e6;
                render_ms[frame - warmup] = (double)(rendered - recorded) / 1e6;
                total_ms[frame - warmup] = (double)(rendered - start) / 1e6;
            }
            if (quick && !expected(kind, pixel)) {
                fprintf(stderr, "versus_draw: %s: pixel (1, 1) is %08x, which isn't what it draws\n", names[kind], (unsigned)pixel);
                failures++;
                break;
            }
        }
        if (kind == VD_CUBES) cube_pixel = pixel;
        if (kind == VD_CUBE_MATRICES && pixel != cube_pixel) {
            fprintf(stderr, "versus_draw: the cubes drawn by matrices read %08x, by places %08x\n", (unsigned)pixel,
                    (unsigned)cube_pixel);
            failures++;
        }
        const timing recording = summarize(record_ms, frames), rendering = summarize(render_ms, frames),
                     total = summarize(total_ms, frames);
        printf("%s (ms, median / p95): record %.3f / %.3f, render + readback %.3f / %.3f, total %.3f / %.3f\n", names[kind],
               recording.median_ms, recording.p95_ms, rendering.median_ms, rendering.p95_ms, total.median_ms, total.p95_ms);
        if (label) save_row(out ? out : stdout, label, api, device, kind, recording, rendering, total);
        if ((dump || against) && api->frame(frame_pixels)) {
            if (dump) {
                char path[1024];
                snprintf(path, sizeof path, "%s-%s%s.ppm", dump, tide_numbers[kind], kind == VD_CUBE_MATRICES ? "m" : "");
                if (!write_ppm(path, frame_pixels)) {
                    fprintf(stderr, "versus_draw: couldn't write %s\n", path);
                    failures++;
                }
            }
            if (against) compare_frame(against, kind, frame_pixels);
        }
    }
    api->close();
    glfwTerminate();
    if (out) {
        const bool failed = ferror(out) != 0;
        if (fclose(out) != 0 || failed) {
            fprintf(stderr, "versus_draw: couldn't finish saving benchmark results\n");
            return 1;
        }
        printf("saved as '%s' in " TIDE_BENCH_RESULTS "/draw.jsonl\n", label);
    }
    return failures ? 1 : 0;
}
