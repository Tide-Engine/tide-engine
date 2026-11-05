// Listed runs culled on the GPU (notes/drawing.md, Listed runs culled on the GPU): the draw list records a draw of a
// list it keeps as its candidates (tide_draw_run.culled), a run a level, and the renderer's kernels (cull.tide, made
// C by tidec as the platform builds: cull_kernels.h) find each run's instances: those its level picks and the
// frustum keeps, in their order. Each run is a bucket; its candidates are its parts', a draw's each, a whole
// number of groups of 64. The frame's are culled at once, before its first pass: CullListed, CullOffsets and
// CullScatter, one compute pass, then each bucket is an indirect draw of its places through its listed pipeline.
// Where that can't be (the recorder, a kernel or the listed pipeline not made yet, a fifth page), the
// CPU decides a run's instances (tide_draw_culled_places), which are the CPU path's. Included by platform.c, after
// compute.inc.

#include "cull_kernels.h"

// The kernels' lists, each a GPU list's layout (a header of four words: count, capacity, 0, 0; then elements)
enum { CULL_CAMERAS, CULL_JOBS, CULL_PARTS, CULL_BUCKETS, CULL_GROUPS, CULL_SCRATCH, CULL_PLACES, CULL_ARGS, CULL_LISTS };
// Words an element of each, as cull.tide's structs are (floats, then ints)
enum { CULL_CAMERA_WORDS = 92, CULL_JOB_WORDS = 28, CULL_PART_WORDS = 4, CULL_BUCKET_WORDS = 4, CULL_GROUP_WORDS = 2,
       CULL_ARG_WORDS = 5, CULL_GROUP = 64 };
_Static_assert(tide_kernel_CullListed_GROUP == CULL_GROUP && tide_kernel_CullOffsets_GROUP == CULL_GROUP
               && tide_kernel_CullScatter_GROUP == CULL_GROUP, "cull.tide's kernels run in groups of 64");

static sized_buffer cull_buffers[CULL_LISTS];
static tide_gpu_id cull_pipelines[3]; // CullListed, CullOffsets, CullScatter
static bool cull_on;                   // This frame's culled runs may be culled on the GPU (cull_plan)
static uint32_t cull_pages[4], cull_page_count; // The pages they read, the textures K1 samples
static uint32_t *cull_words[CULL_LISTS];
static uint32_t cull_word_count[CULL_LISTS], cull_word_capacity[CULL_LISTS];
static uint32_t cull_cpu_capacity;
static uint32_t cull_regions_capacity;
static uint32_t *cull_regions; // Each bucket's first place in CULL_PLACES

static const tide_shader *const cull_shaders[3] = {&tide_kernel_CullListed, &tide_kernel_CullOffsets,
                                                    &tide_kernel_CullScatter};

// Everything on the device went with it.
static void forget_culling(void)
{
    memset(cull_buffers, 0, sizeof cull_buffers);
    memset(cull_pipelines, 0, sizeof cull_pipelines);
    cull_on = false;
}

// The three kernels' pipelines, made once a device: whether they're ready.
static bool cull_kernels(void)
{
    bool ready = true;
    for (uint32_t k = 0; k < 3; k++) {
        const tide_shader *shader = cull_shaders[k];
        if (!cull_pipelines[k]) {
            const uint32_t storage = tide_shader_compute_storage(shader);
            tide_gpu_pipeline_desc desc = {.name = shader->name, .wgsl = shader->wgsl, .spirv = shader->spirv,
                                           .spirv_words = shader->spirv_words, .compute = true, .uniform_size = 16u,
                                           .texture_count = shader->texture_count, .storage_count = storage};
            for (uint32_t i = 0; i < shader->texture_count && i < TIDE_GPU_MAX_TEXTURES; i++) desc.reads[i] = gpu_read(shader, i);
            for (uint32_t i = 0; i < storage && i < TIDE_GPU_MAX_STORAGE; i++)
                desc.storage_writes[i] = i < shader->storage_count && (shader->storage_writes >> i & 1u) != 0;
            cull_pipelines[k] = gpu->pipeline(&desc);
        }
        ready &= pipeline_usable(cull_pipelines[k]);
    }
    return ready;
}

static void cull_warm(void)
{
    if (gpu == &tide_gpu_record) return;
    cull_kernels();
    for (uint32_t k = 0; k < 3; k++) warm_ask(cull_pipelines[k]);
}

static uint32_t *cull_room(const uint32_t list, const uint32_t words)
{
    const uint64_t need = (uint64_t)cull_word_count[list] + words;
    if (need > UINT32_MAX) tide_out_of_memory();
    if (need > cull_word_capacity[list]) {
        uint32_t capacity = cull_word_capacity[list] ? cull_word_capacity[list] : 256u;
        while (capacity < need) capacity = capacity > UINT32_MAX / 2u ? (uint32_t)need : capacity * 2u;
        cull_words[list] = tide_realloc(cull_words[list], (size_t)cull_word_capacity[list] * sizeof(uint32_t),
                                        (size_t)capacity * sizeof(uint32_t));
        cull_word_capacity[list] = capacity;
    }
    uint32_t *to = cull_words[list] + cull_word_count[list];
    memset(to, 0, (size_t)words * sizeof(uint32_t));
    cull_word_count[list] += words;
    return to;
}

static void cull_plan(const tide_draw_list *list)
{
    static int decide = -1; // TIDE_GPU_CULLING=decide: the CPU decides culled runs as they're drawn, for tests
    if (decide < 0) {
        const char *said = getenv("TIDE_GPU_CULLING");
        decide = said && strcmp(said, "decide") == 0;
    }
    cull_on = false;
    cull_page_count = 0;
    if (!list->cull_count || gpu == &tide_gpu_record || view_only_3d || decide) return;
    cull_on = cull_kernels();
}

// The page index of the GPU's for page texture `slot`: one of four.
static uint32_t cull_page_of(const uint32_t slot)
{
    for (uint32_t i = 0; i < cull_page_count; i++) {
        if (cull_pages[i] == slot) return i;
    }
    if (cull_page_count == 4) return UINT32_MAX;
    cull_pages[cull_page_count] = slot;
    return cull_page_count++;
}

static bool cull_takes(const tide_draw_list *list, const tide_draw_run *run)
{
    if (!cull_on) return false;
    const uint32_t slot = run->textures[run->shader->texture_count];
    if (!slot || slot > list->texture_count) return false;
    for (uint32_t j = run->culled - 1u; j < run->culled - 1u + run->culled_count; j++) {
        if (list->culls[j].page != slot) return false; // A set's draws are of one page
    }
    return cull_page_of(slot) != UINT32_MAX;
}

static uint32_t cull_decide(const tide_draw_list *list, const tide_draw_run *run)
{
    if (run->count > cull_cpu_capacity) {
        uint32_t capacity = cull_cpu_capacity ? cull_cpu_capacity : 1024u;
        while (capacity < run->count) capacity = capacity > UINT32_MAX / 2u ? run->count : capacity * 2u;
        cull_cpu = tide_realloc(cull_cpu, (size_t)cull_cpu_capacity * sizeof(uint32_t), (size_t)capacity * sizeof(uint32_t));
        cull_cpu_capacity = capacity;
    }
    return tide_draw_culled_places(list, run, cull_cpu);
}

// Whether a box's numbers are ones the GPU's test takes: finite, under 2^40 each way, its extents not under 0.
static bool cull_box_small(const tide_draw_bounds *b)
{
    const float v[6] = {b->center.x, b->center.y, b->center.z, b->extents.x, b->extents.y, b->extents.z};
    for (uint32_t i = 0; i < 6; i++) {
        if (!(v[i] < 0x1p40f && v[i] > -0x1p40f)) return false;
    }
    return v[3] >= 0.0f && v[4] >= 0.0f && v[5] >= 0.0f;
}

static void cull_put_float(uint32_t *to, const float v)
{
    memcpy(to, &v, sizeof v);
}

// A list's buffer, with room for its words after its header, written: its count and capacity, `count` elements.
static tide_gpu_id cull_buffer(const uint32_t list, const uint32_t count, const bool write)
{
    sized_buffer *b = &cull_buffers[list];
    const uint64_t bytes = (uint64_t)(TIDE_GPU_LIST_HEADER_WORDS + cull_word_count[list]) * 4u;
    if (b->bytes < bytes || !b->buffer) {
        if (b->buffer) free_buffer(b->buffer);
        uint64_t room = b->bytes ? b->bytes : 4096u;
        while (room < bytes) room *= 2u;
        if (room > TIDE_GPU_STORAGE_MOST_BYTES) room = bytes;
        b->buffer = made_buffer(TIDE_GPU_STORAGE, (size_t)room, NULL);
        b->bytes = room;
    }
    const uint32_t header[TIDE_GPU_LIST_HEADER_WORDS] = {count, count, 0, 0};
    gpu->buffer_write(b->buffer, 0, header, sizeof header);
    if (write && cull_word_count[list]) {
        gpu->buffer_write(b->buffer, sizeof header, cull_words[list], (size_t)cull_word_count[list] * 4u);
    }
    return b->buffer;
}

static tide_gpu_id cull_places(void)
{
    return cull_buffers[CULL_PLACES].buffer;
}

static size_t cull_places_at(const uint32_t bucket)
{
    return ((size_t)TIDE_GPU_LIST_HEADER_WORDS + cull_regions[bucket]) * sizeof(uint32_t);
}

static tide_gpu_id cull_args(void)
{
    return cull_buffers[CULL_ARGS].buffer;
}

static size_t cull_args_at(const uint32_t bucket)
{
    return ((size_t)TIDE_GPU_LIST_HEADER_WORDS + (size_t)bucket * CULL_ARG_WORDS) * sizeof(uint32_t);
}

// The record of a run of `groups` groups of 64 (TIDE_GPU_RUN_WORDS).
static tide_gpu_id cull_record(const uint32_t groups)
{
    const tide_gpu_id record = take_run_buffer();
    const uint32_t size = groups * CULL_GROUP;
    const uint32_t words[TIDE_GPU_RUN_WORDS] = {groups, 1, 1, size, size, 1, 1, 0};
    gpu->buffer_write(record, 0, words, sizeof words);
    return record;
}

static void cull_run(const tide_draw_list *list)
{
    if (!cull_on) return;
    for (uint32_t i = 0; i < CULL_LISTS; i++) cull_word_count[i] = 0;
    // Every camera and every draw of the list's, as their numbers are
    for (uint32_t c = 0; c < list->cull_camera_count; c++) {
        const tide_draw_cull_camera *camera = &list->cull_cameras[c];
        uint32_t *to = cull_room(CULL_CAMERAS, CULL_CAMERA_WORDS);
        float planes[72];
        const bool culls = tide_draw_cull_planes(camera, planes);
        for (uint32_t k = 0; k < 72; k++) cull_put_float(to + k, planes[k]);
        for (uint32_t k = 0; k < 4; k++) {
            cull_put_float(to + 72 + k, camera->lod[1 + k]);  // lodW
            cull_put_float(to + 76 + k, camera->lod[6 + k]);  // lodX
            cull_put_float(to + 80 + k, camera->lod[10 + k]); // lodY
        }
        cull_put_float(to + 84, camera->lod[0]);
        cull_put_float(to + 85, camera->lod[5]);
        cull_put_float(to + 86, camera->lod[14]);
        cull_put_float(to + 87, camera->lod[15]);
        to[88] = culls ? 1u : 0u;
        to[89] = camera->lod[0] > 0.0f ? 1u : 0u;
        to[90] = camera->lod_level;
    }
    for (uint32_t j = 0; j < list->cull_count; j++) {
        const tide_draw_cull *job = &list->culls[j];
        uint32_t *to = cull_room(CULL_JOBS, CULL_JOB_WORDS);
        const tide_draw_mesh_data *m = job->mesh && job->mesh <= list->mesh_count ? &list->meshes[job->mesh - 1u] : NULL;
        if (!m) continue; // Never a bucket's (cull_takes)
        cull_put_float(to + 0, job->bounds.center.x), cull_put_float(to + 1, job->bounds.center.y);
        cull_put_float(to + 2, job->bounds.center.z);
        cull_put_float(to + 4, job->bounds.extents.x), cull_put_float(to + 5, job->bounds.extents.y);
        cull_put_float(to + 6, job->bounds.extents.z);
        for (uint32_t k = 0; k < 3; k++) cull_put_float(to + 8 + k, m->center[k]);
        cull_put_float(to + 11, m->radius);
        for (uint32_t k = 0; k < TIDE_MESH_LEVELS; k++) cull_put_float(to + 12 + k, k < m->levels ? m->level_error[k] : 0.0f);
        cull_put_float(to + 19, m->lod_multiplier);
        uint32_t page = 0;
        for (uint32_t p = 0; p < cull_page_count; p++) page = cull_pages[p] == job->page ? p : page;
        to[20] = job->camera, to[21] = page, to[22] = job->first, to[23] = job->count;
        to[24] = job->packs, to[25] = m->levels;
        to[26] = job->bounds.set == 1 && cull_box_small(&job->bounds) ? 1u : 0u;
    }
    // The buckets, their parts and their regions, in the steps' order
    uint32_t buckets = 0, groups = 0, total = 0;
    for (uint32_t i = 0; i < step_count; i++) {
        draw_step *s = &steps[i];
        if (s->kind != STEP_SHADED || !s->culled) continue;
        const tide_draw_run *run = &list->runs[s->first];
        const uint32_t level = list->meshes[run->mesh - 1u].level;
        if (buckets == cull_regions_capacity) {
            const uint32_t capacity = cull_regions_capacity ? cull_regions_capacity * 2u : 64u;
            cull_regions = tide_realloc(cull_regions, cull_regions_capacity * sizeof *cull_regions, capacity * sizeof *cull_regions);
            cull_regions_capacity = capacity;
        }
        cull_regions[buckets] = total;
        const uint32_t first = groups;
        for (uint32_t j = run->culled - 1u; j < run->culled - 1u + run->culled_count; j++) {
            const tide_draw_cull *job = &list->culls[j];
            if (!tide_draw_cull_in(job, level) || !job->count) continue;
            uint32_t *part = cull_room(CULL_PARTS, CULL_PART_WORDS);
            part[0] = j, part[1] = level, part[2] = groups, part[3] = buckets;
            groups += (job->count + CULL_GROUP - 1u) / CULL_GROUP;
            total += job->count;
        }
        uint32_t *bucket = cull_room(CULL_BUCKETS, CULL_BUCKET_WORDS);
        bucket[0] = first, bucket[1] = groups - first, bucket[2] = cull_regions[buckets];
        uint32_t *args = cull_room(CULL_ARGS, CULL_ARG_WORDS);
        args[0] = gpu_meshes[run->mesh - 1u].index_count;
        s->culled = ++buckets;
    }
    if (!buckets) return;
    if (!groups) { // Nothing to find: each bucket's draw is of none
        cull_buffer(CULL_PLACES, 1, false);
        cull_buffer(CULL_ARGS, buckets * CULL_ARG_WORDS, true);
        return;
    }
    // What the kernels write: room alone
    cull_word_count[CULL_GROUPS] = groups * CULL_GROUP_WORDS;
    cull_word_count[CULL_SCRATCH] = groups * CULL_GROUP;
    cull_word_count[CULL_PLACES] = total;
    const tide_gpu_id lists[CULL_LISTS] = {
        cull_buffer(CULL_CAMERAS, list->cull_camera_count, true), cull_buffer(CULL_JOBS, list->cull_count, true),
        cull_buffer(CULL_PARTS, cull_word_count[CULL_PARTS] / CULL_PART_WORDS, true),
        cull_buffer(CULL_BUCKETS, buckets, true), cull_buffer(CULL_GROUPS, groups, false),
        cull_buffer(CULL_SCRATCH, groups * CULL_GROUP, false), cull_buffer(CULL_PLACES, total, false),
        cull_buffer(CULL_ARGS, buckets * CULL_ARG_WORDS, true)};
    const tide_gpu_id records[3] = {cull_record(groups), cull_record(buckets), cull_record(groups)};
    // The pages' sizes, as their lists' counts, where CullListed's uniforms have them
    int32_t counts[4] = {0, 0, 0, 0};
    for (uint32_t p = 0; p < cull_page_count; p++) {
        const tide_draw_texture *t = &list->textures[cull_pages[p] - 1u];
        counts[p] = t->width > 0 && t->height > 0 ? t->width * t->height : 0;
    }
    int32_t uniforms[4] = {0, 0, 0, 0};
    uniforms[tide_kernel_CullListed_COUNT_page0] = counts[0];
    uniforms[tide_kernel_CullListed_COUNT_page1] = counts[1];
    uniforms[tide_kernel_CullListed_COUNT_page2] = counts[2];
    uniforms[tide_kernel_CullListed_COUNT_page3] = counts[3];
    const uint32_t page_textures[4] = {tide_kernel_CullListed_TEXTURE_page0, tide_kernel_CullListed_TEXTURE_page1,
                                       tide_kernel_CullListed_TEXTURE_page2, tide_kernel_CullListed_TEXTURE_page3};
    static const int32_t none[4] = {0, 0, 0, 0};
    if (!gpu_state.nothing) gpu_state.nothing = made_texture(TIDE_GPU_RGBA32I, 1, 1, none); // For pages it hasn't
    gpu->compute_begin();
    gpu->use(cull_pipelines[0]);
    gpu->uniforms(uniforms, sizeof uniforms);
    for (uint32_t p = 0; p < 4; p++) {
        const tide_gpu_id texture = p < cull_page_count ? texture_to_read(list, cull_pages[p], TIDE_GPU_READ_DATA)
                                                        : texture_to_read(list, 0, TIDE_GPU_READ_DATA);
        gpu->sample(page_textures[p], texture, gpu_filter(TIDE_FILTER_POINT), gpu_wrap(TIDE_WRAP_CLAMP));
    }
    gpu->storage(tide_kernel_CullListed_STORAGE_cameras, lists[CULL_CAMERAS]);
    gpu->storage(tide_kernel_CullListed_STORAGE_jobs, lists[CULL_JOBS]);
    gpu->storage(tide_kernel_CullListed_STORAGE_parts, lists[CULL_PARTS]);
    gpu->storage(tide_kernel_CullListed_STORAGE_groups, lists[CULL_GROUPS]);
    gpu->storage(tide_kernel_CullListed_STORAGE_scratch, lists[CULL_SCRATCH]);
    gpu->storage(tide_kernel_CullListed.storage_count, records[0]);
    gpu->dispatch(groups, 1, 1);
    gpu->use(cull_pipelines[1]);
    gpu->uniforms(none, sizeof none);
    gpu->storage(tide_kernel_CullOffsets_STORAGE_buckets, lists[CULL_BUCKETS]);
    gpu->storage(tide_kernel_CullOffsets_STORAGE_groups, lists[CULL_GROUPS]);
    gpu->storage(tide_kernel_CullOffsets_STORAGE_args, lists[CULL_ARGS]);
    gpu->storage(tide_kernel_CullOffsets.storage_count, records[1]);
    gpu->dispatch(buckets, 1, 1);
    gpu->use(cull_pipelines[2]);
    gpu->uniforms(none, sizeof none);
    gpu->storage(tide_kernel_CullScatter_STORAGE_groups, lists[CULL_GROUPS]);
    gpu->storage(tide_kernel_CullScatter_STORAGE_scratch, lists[CULL_SCRATCH]);
    gpu->storage(tide_kernel_CullScatter_STORAGE_places, lists[CULL_PLACES]);
    gpu->storage(tide_kernel_CullScatter.storage_count, records[2]);
    gpu->dispatch(groups, 1, 1);
    gpu->compute_end();
}

bool tide_platform_cull_compare(const tide_draw_list *list, tide_platform_cull_report *report)
{
    *report = (tide_platform_cull_report){0};
    uint32_t buckets = 0;
    for (uint32_t i = 0; i < step_count; i++) buckets += steps[i].kind == STEP_SHADED && steps[i].culled;
    report->runs = buckets;
    if (!list || !gpu || !gpu->buffer_read || !gpu->buffer_read_wait || !gpu->buffer_read_done) return false;
    if (!buckets || !cull_buffers[CULL_ARGS].buffer || !cull_buffers[CULL_PLACES].buffer) return false;
    const size_t args_bytes = cull_args_at(buckets), places_bytes = (size_t)TIDE_GPU_LIST_HEADER_WORDS * 4u
                                                                    + (size_t)cull_word_count[CULL_PLACES] * 4u;
    uint32_t *args = tide_alloc(args_bytes), *places = tide_alloc(places_bytes);
    const uint32_t reads[2] = {gpu->buffer_read(cull_args(), 0, args_bytes), gpu->buffer_read(cull_places(), 0, places_bytes)};
    gpu->buffer_read_wait();
    const bool read = reads[0] && reads[1] && gpu->buffer_read_done(reads[0], args) > 0 && gpu->buffer_read_done(reads[1], places) > 0;
    for (uint32_t i = 0; read && i < step_count; i++) {
        const draw_step *s = &steps[i];
        if (s->kind != STEP_SHADED || !s->culled) continue;
        const uint32_t b = s->culled - 1u;
        const tide_draw_run *run = &list->runs[s->first];
        const uint32_t found = args[TIDE_GPU_LIST_HEADER_WORDS + b * CULL_ARG_WORDS + 1u];
        const uint32_t *theirs = places + TIDE_GPU_LIST_HEADER_WORDS + cull_regions[b];
        const uint32_t n = cull_decide(list, run);
        // The CPU's, in order, among the GPU's
        uint32_t matched = 0;
        for (uint32_t k = 0; k < found && matched < n; k++) matched += theirs[k] == cull_cpu[matched];
        report->gpu += found;
        report->cpu += n;
        report->missing += n - matched;
        report->extra += found - matched;
    }
    free(args);
    free(places);
    return read;
}
