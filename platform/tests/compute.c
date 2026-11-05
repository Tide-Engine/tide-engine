// Compute on each backend, at gpu.h: storage buffers written by compute
// passes and read back, a workgroup's shared memory and its barrier, atomics,
// a dispatch whose size a dispatch before wrote, copies between buffers,
// and draws whose numbers, indices and vertices compute wrote, whose shaders
// read storage too. Shaders are WGSL alone: Vulkan's SPIR-V is made of it.
#include <stdio.h>
#include <string.h>

#include "gpu.h"
#include "tide/platform.h"
#include "window.h"
#ifdef TIDE_SPIRV
#include "spirv.h" // compiler/src's translator, which the test's build adds
#endif

#ifdef __wasm__
#include "tide_web.h"
#else
#define COMPUTE_VULKAN // Desktop computes with Vulkan where it can, whose device the test loses
#include "gpu_vk.h"
#endif

enum { COUNT = 1000, GROUP = 64, SIDE = 4 };

static int failures;

static void fail(const char *what, const unsigned at, const unsigned got, const unsigned wanted)
{
    if (failures++ < 40) fprintf(stderr, "%s [%u]: %u, wanted %u\n", what, at, got, wanted);
}

// out[i] = in[i] * in[i] + add, for i < count: every invocation past the end does nothing.
static const char square_wgsl[] =
    "struct Uniforms { add: u32, count: u32, pad0: u32, pad1: u32 }\n"
    "@group(0) @binding(0) var<uniform> u: Uniforms;\n"
    "@group(0) @binding(25) var<storage, read> given: array<u32>;\n"
    "@group(0) @binding(26) var<storage, read_write> made: array<u32>;\n"
    "@compute @workgroup_size(64) fn compute(@builtin(global_invocation_id) id: vec3<u32>) {\n"
    "    if (id.x >= u.count) { return; }\n"
    "    made[id.x] = given[id.x] * given[id.x] + u.add;\n"
    "}\n";

// Each group of 64 reversed through its shared memory; counts of odd numbers,
// and the greatest, by atomics on storage and on shared memory.
static const char shared_wgsl[] =
    "struct Uniforms { count: u32, pad0: u32, pad1: u32, pad2: u32 }\n"
    "@group(0) @binding(0) var<uniform> u: Uniforms;\n"
    "@group(0) @binding(25) var<storage, read> given: array<u32>;\n"
    "@group(0) @binding(26) var<storage, read_write> reversed: array<u32>;\n"
    "@group(0) @binding(27) var<storage, read_write> tally: array<atomic<u32>>;\n"
    "@group(0) @binding(28) var<storage, read_write> signed_tally: array<atomic<i32>>;\n"
    "var<workgroup> held: array<u32, 64>;\n"
    "var<workgroup> odd_here: atomic<u32>;\n"
    "@compute @workgroup_size(64) fn compute(@builtin(global_invocation_id) id: vec3<u32>,\n"
    "                                        @builtin(local_invocation_id) local: vec3<u32>,\n"
    "                                        @builtin(workgroup_id) group: vec3<u32>) {\n"
    "    var value = 0u;\n"
    "    if (id.x < u.count) { value = given[id.x]; }\n"
    "    held[local.x] = value;\n"
    "    if (id.x < u.count && (value & 1u) == 1u) { atomicAdd(&odd_here, 1u); }\n"
    "    workgroupBarrier();\n"
    "    let mirrored = group.x * 64u + 63u - local.x;\n"
    "    if (mirrored < u.count) { reversed[mirrored] = held[local.x]; }\n"
    "    if (local.x == 0u) { atomicAdd(&tally[0], atomicLoad(&odd_here)); }\n"
    "    if (id.x < u.count) {\n"
    "        atomicMax(&tally[1], value);\n"
    "        atomicMin(&signed_tally[0], i32(value) - 500);\n"
    "        let old = atomicExchange(&tally[2], 7u);\n"
    "        let swapped = atomicCompareExchangeWeak(&tally[3], 0u, value + 1u);\n"
    "        if (swapped.exchanged) { atomicOr(&tally[4], 1u); }\n"
    "        atomicXor(&tally[5], select(0u, 0u, old == 99u));\n"
    "        atomicAnd(&tally[6], 0xFFu);\n"
    "    }\n"
    "}\n";

// Writes the next dispatch's size (3 groups), a draw's numbers, its indices
// and its three corners.
static const char setup_wgsl[] =
    "@group(0) @binding(0) var<uniform> u: vec4<u32>;\n"
    "@group(0) @binding(25) var<storage, read_write> sizes: array<u32>;\n"
    "@group(0) @binding(26) var<storage, read_write> corners: array<vec2<f32>>;\n"
    "@compute @workgroup_size(1) fn compute() {\n"
    "    sizes[0] = 3u; sizes[1] = 1u; sizes[2] = 1u;\n"            // dispatch_indirect
    "    sizes[4] = 3u; sizes[5] = 1u; sizes[6] = 0u; sizes[7] = 0u;\n" // draw_indirect
    "    sizes[8] = 3u; sizes[9] = 1u; sizes[10] = 16u; sizes[11] = 0u; sizes[12] = 0u;\n" // draw_indexed_indirect
    "    sizes[16] = 2u; sizes[17] = 1u; sizes[18] = 0u;\n"         // indices, 32 bits each
    "    corners[0] = vec2<f32>(-1.0, -1.0); corners[1] = vec2<f32>(3.0, -1.0); corners[2] = vec2<f32>(-1.0, 3.0);\n"
    "}\n";

// Marks the groups it ran in, 3D: each invocation adds its own place's number.
static const char marks_wgsl[] =
    "@group(0) @binding(0) var<uniform> u: vec4<u32>;\n"
    "@group(0) @binding(25) var<storage, read_write> marks: array<atomic<u32>>;\n"
    "@compute @workgroup_size(2, 2, 2) fn compute(@builtin(workgroup_id) group: vec3<u32>,\n"
    "                                             @builtin(num_workgroups) groups: vec3<u32>,\n"
    "                                             @builtin(local_invocation_index) local: u32) {\n"
    "    let at = group.x + groups.x * (group.y + groups.y * group.z);\n"
    "    atomicAdd(&marks[at], local + 1u);\n"
    "    if (local == 0u) { atomicAdd(&marks[64], 1u); }\n"
    "}\n";

// A triangle over the whole target whose corners are read from storage by
// the vertex stage, and whose color is read from storage by the fragment's.
static const char storage_draw_wgsl[] =
    "@group(0) @binding(0) var<uniform> u: vec4<f32>;\n"
    "@group(0) @binding(25) var<storage, read> corners: array<vec2<f32>>;\n"
    "@group(0) @binding(26) var<storage, read> colors: array<u32>;\n"
    "@vertex fn vertex(@builtin(vertex_index) i: u32) -> @builtin(position) vec4<f32> {\n"
    "    return vec4<f32>(corners[i], 0.5, 1.0);\n"
    "}\n"
    "@fragment fn fragment() -> @location(0) vec4<f32> {\n"
    "    let c = colors[u32(u.x)];\n"
    "    return vec4<f32>(f32(c & 255u), f32((c >> 8u) & 255u), f32((c >> 16u) & 255u), 255.0) / 255.0;\n"
    "}\n";

// The same triangle, its corners from a storage buffer bound as vertices.
static const char vertex_draw_wgsl[] =
    "@group(0) @binding(0) var<uniform> u: vec4<f32>;\n"
    "@vertex fn vertex(@location(0) at: vec2<f32>) -> @builtin(position) vec4<f32> {\n"
    "    return vec4<f32>(at, 0.5, 1.0);\n"
    "}\n"
    "@fragment fn fragment() -> @location(0) vec4<f32> { return u; }\n";

static tide_gpu_id compute_pipeline(const tide_gpu *gpu, const char *name, const char *wgsl, const uint32_t uniforms,
                                    const uint32_t storage, const bool *writes)
{
    tide_gpu_pipeline_desc desc = {.name = name, .wgsl = wgsl, .uniform_size = uniforms, .compute = true,
                                   .storage_count = storage};
    for (uint32_t i = 0; i < storage; i++) desc.storage_writes[i] = writes[i];
    return gpu->pipeline(&desc);
}

// Waits, a frame at a time where the backend can't, for a read to land.
typedef struct pending_read {
    uint32_t token;
    void *into;
    const char *what;
} pending_read;

static pending_read reads[16];
static uint32_t read_count;

static void read_back(const tide_gpu *gpu, const tide_gpu_id buffer, const size_t size, void *into, const char *what)
{
    const uint32_t token = gpu->buffer_read(buffer, 0, size);
    if (!token) {
        fprintf(stderr, "%s: no read began\n", what);
        failures++;
        return;
    }
    reads[read_count++] = (pending_read){token, into, what};
}

static bool reads_landed(const tide_gpu *gpu)
{
    if (gpu->buffer_read_wait) gpu->buffer_read_wait();
    bool all = true;
    for (uint32_t i = 0; i < read_count; i++) {
        if (!reads[i].token) continue;
        const int done = gpu->buffer_read_done(reads[i].token, reads[i].into);
        if (done == 0) {
            all = false;
            continue;
        }
        if (done < 0) {
            fprintf(stderr, "%s: the device lost the read\n", reads[i].what);
            failures++;
        }
        reads[i].token = 0;
    }
    if (gpu->buffer_read_wait && !all) {
        fprintf(stderr, "a read wasn't there after buffer_read_wait\n");
        failures++;
        return true;
    }
    return all;
}

static uint32_t given[COUNT], squared[COUNT], reversed[COUNT], tally[8], copied[COUNT];
static int32_t signed_tally[2];
static uint32_t marks[65];
static uint8_t pixels[3][SIDE * SIDE * 4];

static tide_gpu_id given_buffer, squared_buffer, reversed_buffer, tally_buffer, signed_buffer, copy_buffer;
static tide_gpu_id sizes_buffer, corners_buffer, colors_buffer, marks_buffer;
static tide_gpu_id targets[3];
static tide_gpu_id square, shared, setup, marking, storage_draw, vertex_draw;

// The buffers and pipelines, which a backend that makes pipelines in the background may not have made yet.
static void make(const tide_gpu *gpu)
{
    for (uint32_t i = 0; i < COUNT; i++) given[i] = i * 7u + 3u;
    given_buffer = gpu->buffer(TIDE_GPU_STORAGE, sizeof given, given);
    squared_buffer = gpu->buffer(TIDE_GPU_STORAGE, sizeof squared, NULL);
    reversed_buffer = gpu->buffer(TIDE_GPU_STORAGE, sizeof reversed, NULL);
    const uint32_t tally_start[8] = {0, 0, 99, 0, 0, 0, 0xFFFFu, 0};
    tally_buffer = gpu->buffer(TIDE_GPU_STORAGE, sizeof tally_start, tally_start);
    const int32_t signed_start[2] = {1000000, 0};
    signed_buffer = gpu->buffer(TIDE_GPU_STORAGE, sizeof signed_start, signed_start);
    copy_buffer = gpu->buffer(TIDE_GPU_STORAGE, sizeof copied, NULL);
    sizes_buffer = gpu->buffer(TIDE_GPU_STORAGE, 32 * 4, NULL);
    corners_buffer = gpu->buffer(TIDE_GPU_STORAGE, 8 * 4, NULL);
    const uint32_t colors[4] = {0x0000FFu, 0x00FF00u, 0xFF0000u, 0x804020u};
    colors_buffer = gpu->buffer(TIDE_GPU_STORAGE, sizeof colors, colors);
    marks_buffer = gpu->buffer(TIDE_GPU_STORAGE, sizeof marks, NULL);

    square = compute_pipeline(gpu, "square", square_wgsl, 16, 2, (const bool[]){false, true});
    shared = compute_pipeline(gpu, "shared", shared_wgsl, 16, 4, (const bool[]){false, true, true, true});
    setup = compute_pipeline(gpu, "setup", setup_wgsl, 16, 2, (const bool[]){true, true});
    marking = compute_pipeline(gpu, "marks", marks_wgsl, 16, 1, (const bool[]){true});
    storage_draw = gpu->pipeline(&(tide_gpu_pipeline_desc){
        .name = "storage draw", .wgsl = storage_draw_wgsl, .uniform_size = 16, .blend = TIDE_GPU_BLEND_REPLACE,
        .into = TIDE_GPU_TARGET_COLOR, .storage_count = 2,
    });
    vertex_draw = gpu->pipeline(&(tide_gpu_pipeline_desc){
        .name = "vertex draw", .wgsl = vertex_draw_wgsl, .uniform_size = 16, .blend = TIDE_GPU_BLEND_REPLACE,
        .into = TIDE_GPU_TARGET_COLOR, .slots = {{8, false}}, .slot_count = 1,
        .attributes = {{0, TIDE_GPU_FLOAT2, 0}}, .attribute_count = 1,
    });
    if (!square || !shared || !setup || !marking || !storage_draw || !vertex_draw) {
        fprintf(stderr, "a pipeline wasn't made\n");
        failures++;
    }
}

static bool made(const tide_gpu *gpu)
{
    if (!gpu->pipeline_ready) return true;
    const tide_gpu_id all[] = {square, shared, setup, marking, storage_draw, vertex_draw};
    for (size_t i = 0; i < sizeof all / sizeof all[0]; i++) {
        if (!gpu->pipeline_ready(all[i])) return false;
    }
    return true;
}

static void record(const tide_gpu *gpu)
{

    gpu->compute_begin();
    gpu->use(square);
    gpu->uniforms((const uint32_t[4]){5, COUNT, 0, 0}, 16);
    gpu->storage(0, given_buffer);
    gpu->storage(1, squared_buffer);
    gpu->dispatch((COUNT + GROUP - 1) / GROUP, 1, 1);
    gpu->use(shared);
    gpu->uniforms((const uint32_t[4]){COUNT, 0, 0, 0}, 16);
    gpu->storage(0, squared_buffer); // What the dispatch before wrote, in the same compute pass
    gpu->storage(1, reversed_buffer);
    gpu->storage(2, tally_buffer);
    gpu->storage(3, signed_buffer);
    gpu->dispatch((COUNT + GROUP - 1) / GROUP, 1, 1);
    gpu->use(setup);
    gpu->uniforms((const uint32_t[4]){0, 0, 0, 0}, 16);
    gpu->storage(0, sizes_buffer);
    gpu->storage(1, corners_buffer);
    gpu->dispatch(1, 1, 1);
    gpu->compute_end();

    gpu->compute_begin(); // A pass of its own: the size it reads was written by the last
    gpu->use(marking);
    gpu->uniforms((const uint32_t[4]){0, 0, 0, 0}, 16);
    gpu->storage(0, marks_buffer);
    gpu->dispatch_indirect(sizes_buffer, 0);
    gpu->dispatch(2, 3, 4); // ...and one of every axis, after it
    gpu->dispatch(0, 1, 1); // ...and one of nothing
    gpu->compute_end();

    gpu->buffer_copy(reversed_buffer, 4 * 10, copy_buffer, 4 * 20, 4 * 100);

    for (int i = 0; i < 3; i++) targets[i] = gpu->target(TIDE_GPU_TARGET_COLOR, SIDE, SIDE);

    gpu->begin(targets[0], true);
    gpu->use(storage_draw);
    gpu->uniforms((const float[4]){1, 0, 0, 0}, 16);
    gpu->storage(0, corners_buffer);
    gpu->storage(1, colors_buffer);
    gpu->draw_indirect(sizes_buffer, 4 * 4);
    gpu->end();

    gpu->begin(targets[1], true);
    gpu->use(storage_draw);
    gpu->uniforms((const float[4]){3, 0, 0, 0}, 16);
    gpu->storage(0, corners_buffer);
    gpu->storage(1, colors_buffer);
    gpu->indices(sizes_buffer, true); // Indices compute wrote, from word 16
    gpu->draw_indexed_indirect(sizes_buffer, 4 * 8);
    gpu->end();

    gpu->begin(targets[2], true);
    gpu->use(vertex_draw);
    gpu->uniforms((const float[4]){0.2f, 0.6f, 0.8f, 1}, 16);
    gpu->vertices(0, corners_buffer, 0);
    gpu->draw(0, 3, 1);
    gpu->end();

    read_back(gpu, squared_buffer, sizeof squared, squared, "squared");
    read_back(gpu, reversed_buffer, sizeof reversed, reversed, "reversed");
    read_back(gpu, tally_buffer, sizeof tally, tally, "tally");
    read_back(gpu, signed_buffer, sizeof signed_tally, signed_tally, "signed tally");
    read_back(gpu, copy_buffer, sizeof copied, copied, "copied");
    read_back(gpu, marks_buffer, sizeof marks, marks, "marks");
    for (int i = 0; i < 3; i++) gpu->target_read(targets[i], pixels[i]);
    gpu->pipeline_free(square);
    gpu->pipeline_free(shared);
    gpu->pipeline_free(setup);
    gpu->pipeline_free(marking);
    gpu->pipeline_free(storage_draw);
    gpu->pipeline_free(vertex_draw);
}

static void expect_pixels(const int which, const uint8_t r, const uint8_t g, const uint8_t b, const char *what)
{
    for (int i = 0; i < SIDE * SIDE; i++) {
        const uint8_t *p = pixels[which] + i * 4;
        if (p[0] != r || p[1] != g || p[2] != b) {
            fail(what, (unsigned)i, (unsigned)(p[0] | p[1] << 8 | p[2] << 16), (unsigned)(r | g << 8 | b << 16));
            return;
        }
    }
}

static void check(void)
{
    uint32_t odd = 0, greatest = 0;
    int32_t least = 1000000;
    for (uint32_t i = 0; i < COUNT; i++) {
        const uint32_t wanted = given[i] * given[i] + 5u;
        if (squared[i] != wanted) fail("squared", i, squared[i], wanted);
        const uint32_t mirrored = i / GROUP * GROUP + GROUP - 1 - i % GROUP;
        if (mirrored < COUNT && reversed[mirrored] != wanted) fail("reversed", mirrored, reversed[mirrored], wanted);
        odd += wanted & 1u;
        greatest = wanted > greatest ? wanted : greatest;
        least = (int32_t)wanted - 500 < least ? (int32_t)wanted - 500 : least;
    }
    if (tally[0] != odd) fail("odd count", 0, tally[0], odd);
    if (tally[1] != greatest) fail("greatest", 1, tally[1], greatest);
    if (signed_tally[0] != least) fail("least, signed", 0, (unsigned)signed_tally[0], (unsigned)least);
    if (tally[2] != 7u) fail("exchanged", 2, tally[2], 7);
    if (tally[3] == 0u) fail("compare-exchanged", 3, tally[3], 1);
    if (tally[4] != 1u) fail("one compare-exchange took", 4, tally[4], 1);
    if (tally[6] != 0xFFu) fail("and", 6, tally[6], 0xFF);
    for (uint32_t i = 0; i < COUNT; i++) {
        const uint32_t wanted = i >= 20 && i < 120 ? reversed[i - 10] : 0;
        if (copied[i] != wanted) fail("copied", i, copied[i], wanted);
    }
    // The indirect dispatch's 3 groups (0, 1, 2), then 2 x 3 x 4 groups, each of 8 invocations adding 1 to 8.
    for (uint32_t at = 0; at < 64; at++) {
        const uint32_t wanted = (at < 3 ? 36u : 0u) + (at < 24 ? 36u : 0u);
        if (marks[at] != wanted) fail("marks", at, marks[at], wanted);
    }
    if (marks[64] != 27u) fail("groups run", 64, marks[64], 27);
    expect_pixels(0, 0, 255, 0, "draw_indirect, colored from storage");
    expect_pixels(1, 0x20, 0x40, 0x80, "draw_indexed_indirect, indices from storage");
    expect_pixels(2, 51, 153, 204, "storage as vertices");
}

static int stage;

#ifdef __wasm__
// On WebGPU, once it passed: a read begun as the device goes is lost (-1), and everything is made again on the
// next device, its pipelines in the background (createComputePipelineAsync), and passes again.
static int device_round;
static uint32_t lost_read, lost_epoch;
static bool lost_said;

static void start_over(void)
{
    memset(squared, 0, sizeof squared);
    memset(reversed, 0, sizeof reversed);
    memset(tally, 0, sizeof tally);
    memset(signed_tally, 0, sizeof signed_tally);
    memset(copied, 0, sizeof copied);
    memset(marks, 0, sizeof marks);
    memset(pixels, 0, sizeof pixels);
    read_count = 0;
}
#endif

#ifdef COMPUTE_VULKAN
static int vulkan_round;
static uint32_t lost_vk_read, lost_vk_epoch; // A read begun as Vulkan's device was lost on purpose, and the device's epoch then
static uint32_t lost_into[4];
#endif

static int frame(void *user, const float seconds)
{
    (void)user;
    (void)seconds;
    const tide_gpu *gpu = tide_window_gpu();
    if (!gpu->compute_begin) {
#ifdef __wasm__
        // The page's WebGL test (tide_add_web_test makes one beside the WebGPU test): WebGL 2 can't compute,
        // and goes once compute lands
        printf("SKIPPED: %s can't compute\n", gpu->name);
        return 0;
#else
        fprintf(stderr, "%s can't compute: compute is guaranteed on every render backend\n", gpu->name);
        return 1;
#endif
    }
#ifdef __wasm__
    if (stage == 3) { // The device lost: its read has to say so, and another device come
        if (!lost_said) {
            const int done = gpu->buffer_read_done(lost_read, squared);
            if (done > 0) {
                fprintf(stderr, "a read begun as the device went landed\n");
                return 1;
            }
            lost_said = done < 0;
        }
        const uint32_t epoch = gpu->epoch();
        if (!lost_said || epoch == 0 || epoch == lost_epoch) return TIDE_KEEP_RUNNING;
        start_over();
        tide_platform_background_pipelines(true);
        if (gpu->background) gpu->background(true);
        device_round = 1;
        stage = 0;
    }
#endif
#ifdef COMPUTE_VULKAN
    if (stage == 10) { // The device was lost as the last frame was shown: the read with it, and everything on the next device
        const int done = gpu->buffer_read_done(lost_vk_read, lost_into);
        if (done != -1) fail("a read the device lost", 0, (unsigned)done, (unsigned)-1);
        if (gpu->epoch() == lost_vk_epoch || !gpu->epoch()) {
            fprintf(stderr, "Vulkan's lost device wasn't made again\n");
            return 1;
        }
        memset(squared, 0, sizeof squared), memset(reversed, 0, sizeof reversed), memset(tally, 0, sizeof tally);
        memset(copied, 0, sizeof copied), memset(signed_tally, 0, sizeof signed_tally), memset(marks, 0, sizeof marks);
        memset(pixels, 0, sizeof pixels);
        read_count = 0;
        vulkan_round = 1;
        stage = 0;
    }
#endif
    if (stage == 0) {
        if (gpu->epoch() == 0) return TIDE_KEEP_RUNNING;
        printf("renderer: %s\n", tide_platform_renderer());
        make(gpu);
        stage = 1;
    }
    if (failures) return 1;
    if (stage == 1) {
        if (!made(gpu)) return TIDE_KEEP_RUNNING;
        record(gpu);
        stage = 2;
    }
    if (failures) return 1;
    if (!reads_landed(gpu)) return TIDE_KEEP_RUNNING;
    check();
    if (failures) return 1;
#ifdef __wasm__
    if (device_round == 0 && strcmp(tide_platform_renderer(), "WebGPU") == 0) {
        printf("compute passed on the first device\n");
        lost_epoch = gpu->epoch();
        lost_read = gpu->buffer_read(given_buffer, 0, sizeof given);
        if (!lost_read) {
            fprintf(stderr, "no read began before the device went\n");
            return 1;
        }
        tide_web_eval("Tide.loseDevice()");
        stage = 3;
        return TIDE_KEEP_RUNNING;
    }
#endif
#ifdef COMPUTE_VULKAN
    if (vulkan_round == 0 && strcmp(tide_platform_renderer(), "Vulkan") == 0) {
        // A read begun and not yet sent isn't there, and asking doesn't wait for it
        lost_vk_read = gpu->buffer_read(tally_buffer, 0, sizeof lost_into);
        if (!lost_vk_read) fail("a read began", 0, 0, 1);
        else if (gpu->buffer_read_done(lost_vk_read, lost_into) != 0) fail("a read not yet sent is there", 0, 1, 0);
        lost_vk_epoch = gpu->epoch();
        tide_gpu_vk_lose();
        stage = 10;
        return failures ? 1 : TIDE_KEEP_RUNNING;
    }
    if (vulkan_round == 1) printf("the device was lost with a read on its way, and another computed it all again\n");
#endif
    printf("compute passed\n");
    return 0;
}

int main(void)
{
#ifdef TIDE_SPIRV
    tide_gpu_spirv_of = tide_wgsl_to_spirv; // The shaders here have WGSL alone: Vulkan's SPIR-V is made of it
#endif
    tide_platform_open(&(tide_window_desc){.title = "compute", .width = SIDE, .height = SIDE, .hidden = true});
    tide_platform_run(frame, NULL);
}
