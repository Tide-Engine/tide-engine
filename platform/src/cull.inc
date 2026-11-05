// Listed runs culled on the GPU (notes/drawing.md, Listed runs culled on the GPU): the draw list records a draw of a
// list it keeps as its candidates (tide_draw_run.culled), a run a level, and the renderer's kernels (cull.tide, made
// C by tidec as the platform builds: cull_kernels.h) find each run's instances: those its level picks and the
// frustum keeps, in their order. Each run is a bucket; its candidates are its parts', a draw's each, a whole
// number of groups of 64. The frame's are culled at once, before its first pass: CullListed, CullOffsets,
// CullScatter and CullDraws, one compute pass. Buckets one after another that draw alike, through one pipeline with
// the same values and textures, of levels of one mesh, are a join (cull_joins), drawn by one
// draw_indexed_indirect_count through their listed pipeline: CullDraws writes a record for each of its buckets that
// kept any, in order, and how many. An ordinary listed run drawn through its listed pipeline that draws alike beside
// culled ones is a bucket of the join too, its places the CPU's, written where its bucket's go (cull_fixed). Where
// that can't be (the recorder, a kernel or the listed pipeline not made yet, a fifth page), the CPU decides a run's
// instances (tide_draw_culled_places), which are the CPU path's. Then what GPU-decided draws drew is read back for
// the stats (counted_*). Included by platform.c, after compute.inc.
//
// Occlusion (notes/drawing.md, Occlusion culling from a depth pyramid): a run of GPU-culled steps into one
// destination whose draws all test it (a span: draws with Draw.Occlusion on, with any steps between them but other
// GPU-culled ones, till the destination is cleared) is culled again before its first step, once a pyramid has been
// made of the depth drawn into the destination so far (the hiz kernel, shaders.h, one dispatch): CullListed's phase 1
// over the span's groups, then CullOffsets and CullScatter over its buckets, into the places and arguments the
// frame's culling wrote. Depth only gets nearer till a clear, so what that depth hides would fail the depth test
// anyway, and what it held at any point since the destination's clear, as where its pass had ended for something
// else (hiz_plan), is no nearer.

#include "cull_kernels.h"

static bool step_draws(const draw_step *s);

// The kernels' lists, each a GPU list's layout (a header of four words: count, capacity, 0, 0; then elements)
enum { CULL_CAMERAS, CULL_JOBS, CULL_PARTS, CULL_BUCKETS, CULL_GROUPS, CULL_SCRATCH, CULL_PLACES, CULL_COUNTS,
       CULL_JOINS, CULL_ARGS, CULL_DRAWS, CULL_LISTS };
// Words an element of each, as cull.tide's structs are (floats, then ints)
enum { CULL_CAMERA_WORDS = 108, CULL_JOB_WORDS = 28, CULL_PART_WORDS = 4, CULL_BUCKET_WORDS = 8, CULL_GROUP_WORDS = 2,
       CULL_JOIN_WORDS = 4, CULL_ARG_WORDS = 5, CULL_GROUP = 64 };
enum { CULL_LISTED, CULL_OFFSETS, CULL_SCATTER, CULL_DRAWS_KERNEL, CULL_KERNELS };
_Static_assert(tide_kernel_CullListed_GROUP == CULL_GROUP && tide_kernel_CullOffsets_GROUP == CULL_GROUP
               && tide_kernel_CullScatter_GROUP == CULL_GROUP && tide_kernel_CullDraws_GROUP == CULL_GROUP,
               "cull.tide's kernels run in groups of 64");

// A frame's join: buckets `first` to `first + count`, drawn through `indices` (`wide` or not) by one call. With
// `first_instance` its records' first instances say where each bucket's places start, all bound from the first;
// else it's one bucket, whose places are bound from where its own start.
typedef struct cull_join {
    uint32_t first, count;
    tide_gpu_id indices; // ...or for a join of a family's, its family's buffer as it is when it draws
    uint32_t family;     // 1 + its family's place (cull_families), or 0
    bool wide, first_instance;
    bool drawn; // Its call was made this frame: what's read back of its records counts (counted_*)
} cull_join;

// A bucket as the CPU has it: where its places start in CULL_PLACES, its step, and where its indices start in its
// join's index buffer
typedef struct cull_bucket {
    uint32_t region, step, first_index;
} cull_bucket;
static cull_bucket *cull_buckets;
static uint32_t cull_bucket_count, cull_bucket_capacity;
static cull_join *cull_joins;
static uint32_t cull_join_count, cull_join_capacity;

static sized_buffer cull_buffers[CULL_LISTS];
static tide_gpu_id cull_pipelines[CULL_KERNELS];
static bool cull_on;                   // This frame's culled runs may be culled on the GPU (cull_plan)
static uint32_t cull_pages[4], cull_page_count; // The pages they read, the textures K1 samples
static uint32_t *cull_words[CULL_LISTS];
static uint32_t cull_word_count[CULL_LISTS], cull_word_capacity[CULL_LISTS];
static uint32_t cull_cpu_capacity;

static int32_t cull_page_counts[4]; // The pages' sizes, as their lists' counts, CullListed's uniforms

static const tide_shader *const cull_shaders[CULL_KERNELS] = {&tide_kernel_CullListed, &tide_kernel_CullOffsets,
                                                              &tide_kernel_CullScatter, &tide_kernel_CullDraws};

// A mesh's family: its indices and its levels' (the meshes whose vertices are its), one after another in one buffer,
// so that a join of runs of its levels draws through one index buffer. Made where a join first asks for it, and
// again where it asks for a level it hasn't (a level's place is the copy's: what it was, it keeps); let go after
// CULL_FAMILY_IDLE_FRAMES frames unused, or for another mesh's where all are used.
enum { CULL_FAMILIES_MOST = 256, CULL_FAMILY_IDLE_FRAMES = 300 };
typedef struct cull_family_level {
    tide_gpu_id from; // The level's index buffer it was copied from, 0 for none
    uint64_t id, other, version;
    uint32_t epoch, count, first; // ...its indices, and where they start in the family's buffer, in indices
} cull_family_level;
typedef struct cull_family {
    uint32_t base; // 1 + its mesh's place among the GPU's (gpu_meshes), 0 for a free place
    uint64_t id, other, version;
    uint32_t epoch;
    tide_gpu_id buffer;
    uint32_t bytes; // ...of which its levels' take this many
    uint32_t frame; // The last that drew through it
    bool wide;
    cull_family_level level[TIDE_MESH_LEVELS + 1];
} cull_family;
static cull_family cull_families[CULL_FAMILIES_MOST];

// What GPU-decided draws drew (tide_platform_draw_vertices, tide_platform_draws): each frame's records (its culled
// joins' and its indirect draws' over GPU lists) copied together as it ends into a buffer of its own, read back, and
// summed once here, a frame or more later; with geometry diagnostics, CullListed's groups too, which each culled
// draw's rows count (tide_draw_geometry_add). The newest frame read is the one counted.
enum { COUNTED_FRAMES = 4 };
typedef struct counted_part {
    uint32_t row, first, groups; // A culled draw at a level: its row, and its groups in CULL_GROUPS
} counted_part;
typedef struct counted_frame {
    uint32_t token;   // Its read's, 0 for a free place
    uint64_t serial;  // Which frame it was
    tide_gpu_id buffer;
    uint32_t capacity;
    uint32_t records; // Indirect records, 5 words each, from the start...
    uint32_t *drawn;  // ...a bit each, 1 where its call was made: a culled join's records are copied whole
    uint32_t drawn_capacity;
    uint32_t groups;  // ...then CullListed's groups, 2 words each, for parts
    counted_part *parts;
    uint32_t part_count, part_capacity;
    tide_draw_diagnostics *diagnostics;
    uint64_t revision;
} counted_frame;
static counted_frame counted[COUNTED_FRAMES];
static uint64_t counted_serial, counted_newest; // Frames ended, and the newest one counted
static uint64_t counted_vertices; // What the newest one's GPU-decided draws drew: vertices...
static uint32_t counted_draws;    // ...and draws, a record with instances each
static uint32_t *counted_words;
static uint32_t counted_word_capacity;
static uint32_t *counted_steps; // The frame's indirect draws over GPU lists that were drawn: their steps
static uint32_t counted_step_count, counted_step_capacity;

// Occlusion's pyramids: in one GPU list of ints (cull.tide's Culling.depths), elements 0 to 3 the frame's counts
// (tested, hidden; 2 is the pyramid kernel's count of its groups done, 0 between dispatches), then a room for each
// destination a span of the frame measures, its pyramid's levels there, the farthest depths of what they cover as bits
// (occlusion.tide). A destination's is made where a span measures it, and made again where another does after
// something was drawn into it (a pass on it began since).
enum { HIZ_COUNTS = 4, HIZ_DONE = 2, HIZ_MOST_WORDS = 1u << 24 };
static sized_buffer hiz_buffer;
static uint32_t hiz_read; // The frame's counts on their way back: a buffer_read token, or 0
static tide_platform_occlusion_report hiz_report;
static tide_gpu_id hiz_pipelines[2]; // The pyramid's kernel (shaders.h), reading a depth of one sample and of more
// Kernels this device can't make (its driver refused them: gpu.h, pipeline_failed), said once a device: the culling's
// (culled runs decided on the CPU as they're drawn, the same pixels) and the pyramid's (no occlusion, the same pixels)
static bool cull_failed, hiz_failed;
typedef struct hiz_room {
    uint32_t target; // 1 + the destination whose pyramid it holds, 0 for none
    uint32_t at;     // Where in `depths` its level 0 starts, in elements
    uint32_t passes; // ...made when that many passes on it had begun this frame (frame_target.passes)
    bool made;
} hiz_room;
static hiz_room *hiz_rooms;
static uint32_t hiz_room_count, hiz_room_capacity;

// A span: its first step and its destination, its buckets, their groups and their joins, one after another (a join
// is never partly a span's: cull_run); the step before which it's measured (`at`, no later than its first: where
// the destination's pass has ended anyway, if one has since something was drawn into it, else its first), and its
// destination's room. Each is measured once, at `at` or, where that couldn't be, at its first.
typedef struct cull_span {
    uint32_t first, target, at, room;
    uint32_t first_bucket, buckets, first_group, groups, first_join, joins;
    bool done;
} cull_span;
static cull_span *cull_spans;
static uint32_t cull_span_count, cull_span_capacity, cull_span_next;

// Everything on the device went with it.
static void forget_culling(void)
{
    memset(cull_buffers, 0, sizeof cull_buffers);
    memset(cull_pipelines, 0, sizeof cull_pipelines);
    memset(hiz_pipelines, 0, sizeof hiz_pipelines);
    cull_failed = hiz_failed = false;
    memset(cull_families, 0, sizeof cull_families);
    cull_on = false;
    cull_join_count = 0;
    for (uint32_t i = 0; i < COUNTED_FRAMES; i++) counted[i].token = 0, counted[i].buffer = 0, counted[i].capacity = 0;
    counted_step_count = 0;
    hiz_buffer = (sized_buffer){0};
    hiz_read = 0;
}

static uint32_t cull_uniform_size(const tide_shader *shader)
{
    const uint32_t packs = shader->uniform_floats + shader->uniform_ints;
    return packs ? packs * 16u : 16u;
}

// The kernels' pipelines, made once a device: whether they're ready.
static bool cull_kernels(void)
{
    bool ready = true;
    for (uint32_t k = 0; k < CULL_KERNELS; k++) {
        const tide_shader *shader = cull_shaders[k];
        if (!cull_pipelines[k]) {
            const uint32_t storage = tide_shader_compute_storage(shader);
            tide_gpu_pipeline_desc desc = {.name = shader->name, .wgsl = shader->wgsl, .spirv = shader->spirv,
                                           .spirv_words = shader->spirv_words, .compute = true,
                                           .uniform_size = cull_uniform_size(shader),
                                           .texture_count = shader->texture_count, .storage_count = storage,
                                           .optional = true}; // The CPU decides runs without them
            for (uint32_t i = 0; i < shader->texture_count && i < TIDE_GPU_MAX_TEXTURES; i++) desc.reads[i] = gpu_read(shader, i);
            for (uint32_t i = 0; i < storage && i < TIDE_GPU_MAX_STORAGE; i++)
                desc.storage_writes[i] = i < shader->storage_count && (shader->storage_writes >> i & 1u) != 0;
            cull_pipelines[k] = gpu->pipeline(&desc);
        }
        ready &= pipeline_usable(cull_pipelines[k]);
    }
    if (!hiz_pipelines[0]) { // The pyramid's, which only spans need: a depth of one sample, and samples loaded
        static char *wgsl[2];
        const char *const first[2] = {hiz_one_wgsl, hiz_samples_wgsl};
        const uint32_t *const spirv[2] = {SPIRV_OF(hiz_one), SPIRV_OF(hiz_samples)};
        const uint32_t words[2] = {SPIRV_WORDS(hiz_one), SPIRV_WORDS(hiz_samples)};
        for (uint32_t k = 0; k < 2; k++) {
            tide_gpu_pipeline_desc desc = {.name = k ? "occlusion's pyramid of samples" : "occlusion's pyramid",
                                           .wgsl = joined(&wgsl[k], first[k], hiz_wgsl), .spirv = spirv[k],
                                           .spirv_words = words[k], .compute = true, .uniform_size = 32,
                                           .texture_count = 1, .storage_count = 1, .optional = true}; // Or no occlusion
            desc.reads[0] = k ? TIDE_GPU_READ_SAMPLES : TIDE_GPU_READ_DEPTH;
            desc.storage_writes[0] = true;
            hiz_pipelines[k] = gpu->pipeline(&desc);
        }
    }
    // Where the device can't make one, the frame does without it, and says so once (the backend has said why)
    for (uint32_t k = 0; gpu->pipeline_failed && k < CULL_KERNELS; k++) {
        if (cull_failed || !gpu->pipeline_failed(cull_pipelines[k])) continue;
        fprintf(stderr, "tide: this device can't make %s, a kernel of the GPU culling: runs recorded to be culled are "
                        "decided on the CPU as they're drawn\n", cull_shaders[k]->name);
        cull_failed = true;
    }
    for (uint32_t k = 0; gpu->pipeline_failed && k < 2; k++) {
        if (hiz_failed || !gpu->pipeline_failed(hiz_pipelines[k])) continue;
        fprintf(stderr, "tide: this device can't make occlusion culling's depth pyramid: draws testing occlusion are culled "
                        "without it\n");
        hiz_failed = true;
    }
    hiz_report.failed = hiz_failed;
    return ready && !cull_failed;
}

static void cull_warm(void)
{
    if (gpu == &tide_gpu_record) return;
    cull_kernels();
    for (uint32_t k = 0; k < 2; k++) warm_ask(hiz_pipelines[k]);
    for (uint32_t k = 0; k < CULL_KERNELS; k++) warm_ask(cull_pipelines[k]);
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

// The counts a frame's culling gave back, where they're here (tide_platform_occlusion): never waiting.
static void hiz_poll(void)
{
    if (!hiz_read || !gpu || !gpu->buffer_read_done) return;
    uint32_t counts[HIZ_COUNTS];
    const int done = gpu->buffer_read_done(hiz_read, counts);
    if (!done) return;
    hiz_read = 0;
    if (done < 0) return;
    hiz_report.tested = counts[0];
    hiz_report.hidden = counts[1];
    hiz_report.serial++;
    hiz_report.counted = true;
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
    cull_join_count = 0;
    cull_span_count = cull_span_next = 0;
    hiz_report.spans = hiz_report.measured = hiz_report.pyramids = hiz_report.breaks = 0;
    hiz_poll();
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

// A run the CPU decided, counted in its draws' geometry rows (tide_draw_cull.rows): each draw's own decisions.
static void cull_decided_rows(const tide_draw_list *list, const tide_draw_run *run)
{
    if (!list->diagnostics || !run->culled || !run->mesh || run->mesh > list->mesh_count) return;
    const uint32_t level = list->meshes[run->mesh - 1u].level;
    for (uint32_t j = run->culled - 1u; j < run->culled - 1u + run->culled_count && j < list->cull_count; j++) {
        const tide_draw_cull *job = &list->culls[j];
        if (!tide_draw_cull_in(job, level) || level > TIDE_MESH_LEVELS || !job->rows[level]) continue;
        tide_draw_run one = *run;
        one.culled = j + 1u;
        one.culled_count = 1;
        tide_draw_geometry_add(list->diagnostics, job->rows_revision, job->rows[level], cull_decide(list, &one));
    }
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
    return ((size_t)TIDE_GPU_LIST_HEADER_WORDS + cull_buckets[bucket].region) * sizeof(uint32_t);
}

static tide_gpu_id cull_args(void)
{
    return cull_buffers[CULL_ARGS].buffer;
}

static size_t cull_args_at(const uint32_t bucket)
{
    return ((size_t)TIDE_GPU_LIST_HEADER_WORDS + (size_t)bucket * CULL_ARG_WORDS) * sizeof(uint32_t);
}

// The record of a run of `x` by `y` groups, `width` by `height` invocations (TIDE_GPU_RUN_WORDS).
static tide_gpu_id cull_record_of(const uint32_t x, const uint32_t y, const uint32_t width, const uint32_t height)
{
    const tide_gpu_id record = take_run_buffer();
    const uint32_t words[TIDE_GPU_RUN_WORDS] = {x, y, 1, width * height, width, height, 1, 0};
    gpu->buffer_write(record, 0, words, sizeof words);
    return record;
}

// Whether a shader's listed vertex stage reads its instance's index (a vert or frag that rolls): the first instances
// of a join's records would change it, so its runs are a call each.
static bool cull_numbered(const tide_shader *shader)
{
    static struct {
        const tide_shader *shader;
        uint64_t id;
        bool numbered;
    } seen[64];
    static uint32_t next;
    for (uint32_t i = 0; i < 64; i++) {
        if (seen[i].shader == shader && seen[i].id == shader->id) return seen[i].numbered;
    }
    bool numbered = shader->wgsl_listed ? strstr(shader->wgsl_listed, "instance_index") != NULL : !shader->spirv_listed;
    // ...or its SPIR-V (a native build's), where a decoration says BuiltIn InstanceIndex (43): OpDecorate (71) or
    // OpMemberDecorate (72)
    for (uint32_t at = 5; !shader->wgsl_listed && shader->spirv_listed && at < shader->spirv_listed_words;) {
        const uint32_t word = shader->spirv_listed[at], count = word >> 16, op = word & 0xFFFFu;
        if (!count || count > shader->spirv_listed_words - at) break;
        if ((op == 71u && count >= 4u && shader->spirv_listed[at + 2] == 11u && shader->spirv_listed[at + 3] == 43u)
            || (op == 72u && count >= 5u && shader->spirv_listed[at + 3] == 11u && shader->spirv_listed[at + 4] == 43u)) {
            numbered = true;
        }
        at += count;
    }
    seen[next % 64u].shader = shader;
    seen[next % 64u].id = shader->id;
    seen[next % 64u].numbered = numbered;
    next++;
    return numbered;
}

// The list's mesh whose vertices its mesh `mesh` draws with: its own place, or a level's mesh's.
static uint32_t cull_family_of(const tide_draw_list *list, const uint32_t mesh)
{
    const tide_draw_mesh_data *m = &list->meshes[mesh - 1u];
    return m->base && m->base <= list->mesh_count ? m->base : mesh;
}

// Whether a step culled on the GPU draws in the same call as the one just before it (`a`): the same pipeline,
// destination, camera, values, textures and tint, levels of the same mesh at the same width, both drawn.
static bool cull_joins_with(const tide_draw_list *list, const draw_step *a, const draw_step *s)
{
    if (a->pipeline != s->pipeline || a->target != s->target || a->stencil != s->stencil || a->camera != s->camera
        || a->camera_data != s->camera_data || a->fit != s->fit || a->blend != s->blend || a->depth != s->depth
        || a->cull != s->cull) return false;
    const tide_draw_run *x = &list->runs[a->first], *y = &list->runs[s->first];
    const tide_shader *shader = x->shader;
    if (y->shader != shader || x->sampling != y->sampling || memcmp(&x->tint, &y->tint, sizeof x->tint) != 0) return false;
    for (uint32_t i = 0; i <= shader->texture_count; i++) {
        if (x->textures[i] != y->textures[i]) return false;
    }
    const uint32_t own = tide_shader_uniform_size(shader);
    if (own && x->uniforms != y->uniforms && memcmp(list->uniforms + x->uniforms, list->uniforms + y->uniforms, own) != 0)
        return false;
    for (uint32_t k = 0; k < shader->storage_count && k < TIDE_SHADER_STORAGE; k++) { // The GPU lists it reads
        const uint64_t a = x->storage ? list->storage[x->storage - 1u + k] : 0u, b = y->storage ? list->storage[y->storage - 1u + k] : 0u;
        if (a != b) return false;
    }
    if (cull_family_of(list, x->mesh) != cull_family_of(list, y->mesh)) return false;
    if (list->meshes[y->mesh - 1u].level > TIDE_MESH_LEVELS) return false;
    return gpu_meshes[x->mesh - 1u].wide == gpu_meshes[y->mesh - 1u].wide && mesh_drawable(list, y->mesh)
        && pipeline_made(s);
}

// Whether a step is an ordinary listed run that can be drawn through the frame's culled buckets: drawn through its
// listed pipeline, its places the list's (none the CPU decided), of a 3D shader's mesh of vertices of its own.
static bool cull_listed_takes(const tide_draw_list *list, const draw_step *s)
{
    if (s->kind != STEP_SHADED || s->culled || s->listed != 1 || s->decided || !frame_targets[s->target].valid) return false;
    const tide_draw_run *run = &list->runs[s->first];
    const tide_shader *shader = run->shader;
    return run->listed && !run->culled && !run->indirect && run->count && shader->dims == 3 && !shader->vertex_storage
        && !shader->index_storage && (uint64_t)run->first + (uint64_t)run->count * sizeof(uint32_t) <= list->instance_used;
}

static bool cull_family_has(const cull_family_level *level, const gpu_mesh *g)
{
    return level->from && level->from == g->indices && level->id == g->id && level->other == g->other
        && level->version == g->version && level->epoch == g->epoch && level->count == g->index_count;
}

static uint32_t cull_index_bytes(const uint32_t count, const bool wide)
{
    return (count * (wide ? 4u : 2u) + 3u) & ~3u;
}

// The family of the join's meshes, holding each of their indices, which gives each of its buckets where its start:
// NULL where it can't be had. Levels it hasn't are copied on the GPU from the meshes' index buffers, before the
// frame's first pass, into a buffer made again: laid out anew, or where a join before this one in the frame drew
// through it, after what it held, which stays where it was (each join's call takes the family's buffer as it is
// when it draws: cull_draw).
static cull_family *cull_family_for(const tide_draw_list *list, const cull_join *join)
{
    const uint32_t lead = list->runs[steps[cull_buckets[join->first].step].first].mesh;
    const uint32_t base = cull_family_of(list, lead);
    gpu_mesh *b = &gpu_meshes[base - 1u];
    const bool wide = gpu_meshes[lead - 1u].wide;
    const uint32_t width = wide ? 4u : 2u;
    cull_family *f = b->family && b->family <= CULL_FAMILIES_MOST && cull_families[b->family - 1u].base == base
                   ? &cull_families[b->family - 1u] : NULL;
    if (!f) { // A free place, or the one longest unused
        for (uint32_t i = 0; i < CULL_FAMILIES_MOST && !f; i++) f = cull_families[i].base ? NULL : &cull_families[i];
        for (uint32_t i = 0; i < CULL_FAMILIES_MOST && !f; i++) {
            if (cull_families[i].frame == gpu_frame) continue; // Drawn through this frame
            f = &cull_families[i];
        }
        for (uint32_t i = 0; f && f->base && i < CULL_FAMILIES_MOST; i++) {
            const cull_family *each = &cull_families[i];
            if (each->frame != gpu_frame && gpu_frame - each->frame > gpu_frame - f->frame) f = &cull_families[i];
        }
        if (!f) return NULL;
        if (f->buffer) free_buffer(f->buffer);
        memset(f, 0, sizeof *f);
        f->base = base;
        b->family = (uint32_t)(f - cull_families) + 1u;
    }
    const bool drawn = f->frame == gpu_frame && f->buffer; // A join before this one draws through it
    if (f->id != b->id || f->other != b->other || f->version != b->version || f->epoch != b->epoch || f->wide != wide) {
        if (drawn) return NULL;
        // Another mesh in its place, or this one changed: its levels are others
        if (f->buffer) free_buffer(f->buffer);
        memset(f->level, 0, sizeof f->level);
        f->buffer = 0;
        f->bytes = 0;
        f->id = b->id, f->other = b->other, f->version = b->version, f->epoch = b->epoch, f->wide = wide;
    }
    f->frame = gpu_frame;
    cull_family_level next[TIDE_MESH_LEVELS + 1];
    memcpy(next, f->level, sizeof next);
    bool fresh[TIDE_MESH_LEVELS + 1] = {false};
    bool again = !f->buffer;
    for (uint32_t k = join->first; k < join->first + join->count; k++) {
        const uint32_t mesh = list->runs[steps[cull_buckets[k].step].first].mesh;
        const uint32_t level = list->meshes[mesh - 1u].level;
        const gpu_mesh *g = &gpu_meshes[mesh - 1u];
        if (cull_family_has(&next[level], g)) continue;
        next[level] = (cull_family_level){.from = g->indices, .id = g->id, .other = g->other, .version = g->version,
                                          .epoch = g->epoch, .count = g->index_count};
        fresh[level] = true;
        again = true;
    }
    if (again) {
        // Drawn through already: what it holds stays where it is, the new after it; else every level anew
        uint64_t bytes = drawn ? f->bytes : 0u;
        for (uint32_t level = 0; level <= TIDE_MESH_LEVELS; level++) {
            if (!next[level].from || (drawn && !fresh[level])) continue;
            next[level].first = (uint32_t)(bytes / width);
            bytes += cull_index_bytes(next[level].count, wide);
        }
        if (!bytes || bytes > TIDE_GPU_STORAGE_MOST_BYTES || bytes / width > UINT32_MAX) return NULL;
        const tide_gpu_id buffer = made_buffer(TIDE_GPU_STORAGE, (size_t)bytes, NULL);
        if (!buffer) return NULL;
        if (drawn) gpu->buffer_copy(f->buffer, 0, buffer, 0, f->bytes);
        for (uint32_t level = 0; level <= TIDE_MESH_LEVELS; level++) {
            if (!next[level].from || (drawn && !fresh[level])) continue;
            const size_t size = cull_index_bytes(next[level].count, wide), to = (size_t)next[level].first * width;
            if (fresh[level] || !f->buffer) gpu->buffer_copy(next[level].from, 0, buffer, to, size);
            else gpu->buffer_copy(f->buffer, (size_t)f->level[level].first * width, buffer, to, size);
        }
        if (f->buffer) free_buffer(f->buffer);
        f->buffer = buffer;
        f->bytes = (uint32_t)bytes;
        memcpy(f->level, next, sizeof next);
    }
    for (uint32_t k = join->first; k < join->first + join->count; k++) {
        const uint32_t mesh = list->runs[steps[cull_buckets[k].step].first].mesh;
        const uint32_t level = list->meshes[mesh - 1u].level;
        // Two meshes of one level in one join: one of them isn't what the family holds there
        if (!cull_family_has(&f->level[level], &gpu_meshes[mesh - 1u])) return NULL;
        cull_buckets[k].first_index = f->level[level].first;
    }
    return f;
}

static cull_join *cull_join_add(const uint32_t first, const uint32_t count)
{
    if (cull_join_count == cull_join_capacity) {
        const uint32_t capacity = cull_join_capacity ? cull_join_capacity * 2u : 64u;
        cull_joins = tide_realloc(cull_joins, cull_join_capacity * sizeof *cull_joins, capacity * sizeof *cull_joins);
        cull_join_capacity = capacity;
    }
    cull_join *j = &cull_joins[cull_join_count++];
    *j = (cull_join){.first = first, .count = count};
    return j;
}

// The record of a run of `groups` groups of 64, `size` invocations of them run.
static tide_gpu_id cull_record(const uint32_t groups, const uint32_t size)
{
    return cull_record_of(groups, 1, size, 1);
}

// CullListed bound to the frame's lists and pages, with `uniforms` (the pages' counts written in).
static void cull_listed(const tide_draw_list *list, int32_t uniforms[16], const tide_gpu_id depths, const uint32_t groups)
{
    uniforms[tide_kernel_CullListed_COUNT_page0] = cull_page_counts[0];
    uniforms[tide_kernel_CullListed_COUNT_page1] = cull_page_counts[1];
    uniforms[tide_kernel_CullListed_COUNT_page2] = cull_page_counts[2];
    uniforms[tide_kernel_CullListed_COUNT_page3] = cull_page_counts[3];
    const uint32_t page_textures[4] = {tide_kernel_CullListed_TEXTURE_page0, tide_kernel_CullListed_TEXTURE_page1,
                                       tide_kernel_CullListed_TEXTURE_page2, tide_kernel_CullListed_TEXTURE_page3};
    static const int32_t none[4] = {0, 0, 0, 0};
    if (!gpu_state.nothing) gpu_state.nothing = made_texture(TIDE_GPU_RGBA32I, 1, 1, none); // For pages it hasn't
    gpu->use(cull_pipelines[CULL_LISTED]);
    gpu->uniforms(uniforms, cull_uniform_size(&tide_kernel_CullListed));
    for (uint32_t p = 0; p < 4; p++) {
        const tide_gpu_id texture = p < cull_page_count ? texture_to_read(list, cull_pages[p], TIDE_GPU_READ_DATA)
                                                        : texture_to_read(list, 0, TIDE_GPU_READ_DATA);
        gpu->sample(page_textures[p], texture, gpu_filter(TIDE_FILTER_POINT), gpu_wrap(TIDE_WRAP_CLAMP));
    }
    gpu->storage(tide_kernel_CullListed_STORAGE_cameras, cull_buffers[CULL_CAMERAS].buffer);
    gpu->storage(tide_kernel_CullListed_STORAGE_jobs, cull_buffers[CULL_JOBS].buffer);
    gpu->storage(tide_kernel_CullListed_STORAGE_parts, cull_buffers[CULL_PARTS].buffer);
    gpu->storage(tide_kernel_CullListed_STORAGE_groups, cull_buffers[CULL_GROUPS].buffer);
    gpu->storage(tide_kernel_CullListed_STORAGE_scratch, cull_buffers[CULL_SCRATCH].buffer);
    gpu->storage(tide_kernel_CullListed_STORAGE_depths, depths);
    gpu->storage(tide_kernel_CullListed.storage_count, cull_record(groups, groups * CULL_GROUP));
    gpu->dispatch(groups, 1, 1);
}

// CullOffsets over `buckets` buckets from `first`, CullScatter over their `groups` groups from `first_group`, then
// CullDraws over their `joins` joins from `first_join`: their places, counts and draw records written again.
static void cull_places_of(const uint32_t first, const uint32_t buckets, const uint32_t first_group, const uint32_t groups,
                           const uint32_t first_join, const uint32_t joins)
{
    int32_t uniforms[16] = {0};
    uniforms[tide_kernel_CullOffsets_INT_first] = (int32_t)first;
    gpu->use(cull_pipelines[CULL_OFFSETS]);
    gpu->uniforms(uniforms, cull_uniform_size(&tide_kernel_CullOffsets));
    gpu->storage(tide_kernel_CullOffsets_STORAGE_buckets, cull_buffers[CULL_BUCKETS].buffer);
    gpu->storage(tide_kernel_CullOffsets_STORAGE_groups, cull_buffers[CULL_GROUPS].buffer);
    gpu->storage(tide_kernel_CullOffsets_STORAGE_counts, cull_buffers[CULL_COUNTS].buffer);
    gpu->storage(tide_kernel_CullOffsets.storage_count, cull_record(buckets, buckets * CULL_GROUP));
    gpu->dispatch(buckets, 1, 1);
    if (groups) { // ...where there's any candidate: every bucket's count is CullOffsets' either way
        memset(uniforms, 0, sizeof uniforms);
        uniforms[tide_kernel_CullScatter_INT_first] = (int32_t)first_group;
        gpu->use(cull_pipelines[CULL_SCATTER]);
        gpu->uniforms(uniforms, cull_uniform_size(&tide_kernel_CullScatter));
        gpu->storage(tide_kernel_CullScatter_STORAGE_groups, cull_buffers[CULL_GROUPS].buffer);
        gpu->storage(tide_kernel_CullScatter_STORAGE_scratch, cull_buffers[CULL_SCRATCH].buffer);
        gpu->storage(tide_kernel_CullScatter_STORAGE_places, cull_buffers[CULL_PLACES].buffer);
        gpu->storage(tide_kernel_CullScatter.storage_count, cull_record(groups, groups * CULL_GROUP));
        gpu->dispatch(groups, 1, 1);
    }
    if (!joins) return;
    const uint32_t join_groups = (joins + CULL_GROUP - 1u) / CULL_GROUP;
    memset(uniforms, 0, sizeof uniforms);
    uniforms[tide_kernel_CullDraws_INT_first] = (int32_t)first_join;
    gpu->use(cull_pipelines[CULL_DRAWS_KERNEL]);
    gpu->uniforms(uniforms, cull_uniform_size(&tide_kernel_CullDraws));
    gpu->storage(tide_kernel_CullDraws_STORAGE_joins, cull_buffers[CULL_JOINS].buffer);
    gpu->storage(tide_kernel_CullDraws_STORAGE_counts, cull_buffers[CULL_COUNTS].buffer);
    gpu->storage(tide_kernel_CullDraws_STORAGE_buckets, cull_buffers[CULL_BUCKETS].buffer);
    gpu->storage(tide_kernel_CullDraws_STORAGE_args, cull_buffers[CULL_ARGS].buffer);
    gpu->storage(tide_kernel_CullDraws_STORAGE_draws, cull_buffers[CULL_DRAWS].buffer);
    gpu->storage(tide_kernel_CullDraws.storage_count, cull_record(join_groups, joins));
    gpu->dispatch(join_groups, 1, 1);
}

// ---------------------------------------------------------------------------
// Occlusion's pyramids and spans

static uint32_t hiz_size(const int size, const uint32_t level)
{
    return ((uint32_t)(size - 1) >> (level + 1u)) + 1u;
}

// As occlusion.tide's HiZLevels: down to one texel, 16 at the most
static uint32_t hiz_levels(const int width, const int height)
{
    uint32_t levels = 1;
    for (uint32_t k = 0; k < 15; k++) {
        if (hiz_size(width, k) > 1u || hiz_size(height, k) > 1u) levels = k + 2u;
    }
    return levels;
}

static uint64_t hiz_level_words(const int width, const int height)
{
    uint64_t words = 0;
    for (uint32_t k = 0; k < hiz_levels(width, height); k++) words += (uint64_t)hiz_size(width, k) * hiz_size(height, k);
    return words;
}

// The pyramid's buffer, with room for the biggest of the frame's: its header's count is all of it, and the frame's
// counts are 0.
static tide_gpu_id hiz_buffer_for(const uint64_t words)
{
    const uint64_t bytes = (uint64_t)(TIDE_GPU_LIST_HEADER_WORDS + HIZ_COUNTS + words) * 4u;
    if (hiz_buffer.bytes < bytes || !hiz_buffer.buffer) {
        if (hiz_buffer.buffer) free_buffer(hiz_buffer.buffer);
        uint64_t room = hiz_buffer.bytes ? hiz_buffer.bytes : 65536u;
        while (room < bytes) room *= 2u;
        hiz_buffer = (sized_buffer){.buffer = made_buffer(TIDE_GPU_STORAGE, (size_t)room, NULL), .bytes = room};
    }
    const uint32_t elements = (uint32_t)(hiz_buffer.bytes / 4u) - TIDE_GPU_LIST_HEADER_WORDS;
    const uint32_t header[TIDE_GPU_LIST_HEADER_WORDS + HIZ_COUNTS] = {elements, elements, 0, 0, 0, 0, 0, 0};
    gpu->buffer_write(hiz_buffer.buffer, 0, header, sizeof header);
    return hiz_buffer.buffer;
}

// Whether a GPU-culled step tests occlusion: every draw of its run with it on, and a depth test to fail.
static bool hiz_member(const tide_draw_list *list, const draw_step *s)
{
    if (s->kind != STEP_SHADED || !s->culled || s->depth == TIDE_DEPTH_NONE) return false;
    const tide_draw_run *run = &list->runs[s->first];
    if (!run->culled || run->culled - 1u + (uint64_t)run->culled_count > list->cull_count) return false;
    for (uint32_t j = run->culled - 1u; j < run->culled - 1u + run->culled_count; j++) {
        if (!list->culls[j].occlusion) return false;
    }
    return true;
}

// Whether a step writes depth where it draws: what a pyramid made after it can measure.
static bool hiz_writes_depth(const tide_draw_list *list, const draw_step *s)
{
    if (s->kind == STEP_MESH_3D) return s->depth == TIDE_DEPTH_READ_WRITE;
    return s->kind == STEP_SHADED && list->runs[s->first].shader->dims == 3 && s->depth == TIDE_DEPTH_READ_WRITE;
}

// Whether a step that goes through the frame's culled buckets is an ordinary listed run's, its places the CPU's
// (cull_run): one of a join, never tested.
static bool cull_fixed(const tide_draw_list *list, const draw_step *s)
{
    return s->kind == STEP_SHADED && s->culled && !list->runs[s->first].culled;
}

// The room in `depths` for destination `target`'s pyramid, found or made: UINT32_MAX where the frame's are too many
// words.
static uint32_t hiz_room_for(const uint32_t target, uint64_t *words)
{
    for (uint32_t r = 0; r < hiz_room_count; r++) {
        if (hiz_rooms[r].target == target + 1u) return r;
    }
    const frame_target *f = &frame_targets[target];
    const uint64_t more = hiz_level_words(f->pixel_width, f->pixel_height);
    if (HIZ_COUNTS + *words + more > HIZ_MOST_WORDS) return UINT32_MAX; // Too big to measure: tested against nothing
    if (hiz_room_count == hiz_room_capacity) {
        const uint32_t capacity = hiz_room_capacity ? hiz_room_capacity * 2u : 4u;
        hiz_rooms = tide_realloc(hiz_rooms, hiz_room_capacity * sizeof *hiz_rooms, capacity * sizeof *hiz_rooms);
        hiz_room_capacity = capacity;
    }
    hiz_rooms[hiz_room_count] = (hiz_room){.target = target + 1u, .at = HIZ_COUNTS + (uint32_t)*words};
    *words += more;
    return hiz_room_count++;
}

// The frame's spans, once its buckets are known: each from a GPU-culled step that tests occlusion, over those after
// it into the same destination with nothing GPU-culled that doesn't between them (ordinary listed runs of the frame's
// joins aside), and no clear of that destination. Their draws' jobs are marked (cull.tide's CullJob.more.w). None
// where nothing was drawn into its destination with depth since its last clear: what's measured would hide nothing.
// Each is measured at the last step before its first where the destination's pass has ended anyway (a step into
// another destination, or a kernel's run, came before it; nothing since began a pass) after something was drawn into
// it, so measuring costs the pass no break; else at its first. The words its pyramids take.
static uint64_t hiz_plan(const tide_draw_list *list)
{
    uint64_t words = 0;
    hiz_room_count = 0;
    cull_span *open = NULL;
    bool skipping = false; // The span going on is one measured nowhere: nothing to hide
    const uint32_t *buckets = cull_words[CULL_BUCKETS];
    const uint32_t targets = list->target_count + 2u;
    // Per destination: the step that first wrote depth into it since its last clear, or UINT32_MAX for none
    static uint32_t *written;
    static uint32_t written_capacity;
    if (targets > written_capacity) {
        written = tide_realloc(written, written_capacity * sizeof *written, targets * sizeof *written);
        written_capacity = targets;
    }
    for (uint32_t t = 0; t < targets; t++) written[t] = UINT32_MAX;
    // Where passes are surely ended: the last step whose pass, at the point it's measured (cull_spans_at), hasn't begun
    uint32_t idle = UINT32_MAX, before = UINT32_MAX; // ...and the destination of the step before
    bool live = false, ran = true;
    for (uint32_t i = 0; i < step_count; i++) {
        const draw_step *s = &steps[i];
        if (s->kind == STEP_COMPUTE || s->kind == STEP_GPU_OP) { // Outside passes: the one going on ends
            ran = true;
            continue;
        }
        if (ran || s->target != before) live = false;
        ran = false;
        before = s->target;
        if (!live) idle = i;
        if (s->kind == STEP_CLEAR && s->target < targets) {
            written[s->target] = UINT32_MAX;
            if (open && s->target == open->target) open = NULL;
        }
        const bool member = s->kind == STEP_SHADED && s->culled && !cull_fixed(list, s);
        if (member) {
            if (!hiz_member(list, s) || !frame_targets[s->target].valid) {
                open = NULL;
            } else {
                const uint32_t b = s->culled - 1u;
                const uint32_t *bucket = buckets + (size_t)b * CULL_BUCKET_WORDS;
                if (!open || open->target != s->target) {
                    // Nothing drawn with depth since the destination's clear (or this frame): nothing to hide
                    const uint32_t since = s->target < targets ? written[s->target] : UINT32_MAX;
                    const uint32_t room = since == UINT32_MAX ? UINT32_MAX : hiz_room_for(s->target, &words);
                    if (cull_span_count == cull_span_capacity) {
                        const uint32_t capacity = cull_span_capacity ? cull_span_capacity * 2u : 8u;
                        cull_spans = tide_realloc(cull_spans, cull_span_capacity * sizeof *cull_spans,
                                                  capacity * sizeof *cull_spans);
                        cull_span_capacity = capacity;
                    }
                    open = &cull_spans[cull_span_count];
                    *open = (cull_span){.first = i, .target = s->target, .first_bucket = b, .first_group = bucket[0],
                                        .room = room, .at = idle != UINT32_MAX && idle > since ? idle : i};
                    skipping = room == UINT32_MAX;
                    if (!skipping) cull_span_count++;
                }
                if (!skipping) {
                    open->buckets = b + 1u - open->first_bucket;
                    open->groups = bucket[0] + bucket[1] - open->first_group;
                    if (s->join) { // A join's first step: the span's first step is one (a join never crosses its edge)
                        if (!open->joins) open->first_join = s->join - 1u;
                        open->joins = s->join - open->first_join;
                    }
                    const tide_draw_run *run = &list->runs[s->first];
                    for (uint32_t j = run->culled - 1u; j < run->culled - 1u + run->culled_count; j++) {
                        cull_words[CULL_JOBS][(size_t)j * CULL_JOB_WORDS + 27u] = 1u; // CullJob.more.w
                    }
                }
            }
        }
        if (step_draws(s)) live = true;
        if (s->target < targets && written[s->target] == UINT32_MAX && hiz_writes_depth(list, s)) written[s->target] = i;
    }
    hiz_report.spans = cull_span_count;
    return words;
}

static void cull_run(const tide_draw_list *list)
{
    for (uint32_t i = 0; i < CULL_FAMILIES_MOST; i++) { // Families no frame drew through for long go
        cull_family *f = &cull_families[i];
        if (f->base && gpu_frame - f->frame > CULL_FAMILY_IDLE_FRAMES) {
            if (f->buffer) free_buffer(f->buffer);
            memset(f, 0, sizeof *f);
        }
    }
    cull_bucket_count = cull_join_count = 0;
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
        for (uint32_t k = 0; k < 16; k++) cull_put_float(to + 88 + k, camera->world_to_clip[k]); // toClip0..3
        to[104] = culls ? 1u : 0u;
        to[105] = camera->lod[0] > 0.0f ? 1u : 0u;
        to[106] = camera->lod_level;
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
    // The buckets, their parts and their regions, in the steps' order, and the runs of them one after another that
    // draw alike (in `cull_joins` for now: they're made joins below). Ordinary listed runs drawn through their listed
    // pipeline go in those runs too, as buckets whose places the CPU writes (cull_fixed): first, which runs of steps
    // that draw alike have a culled one, as only those gain a call from it
    const bool joining = gpu->indirect && (gpu->indirect() & TIDE_GPU_INDIRECT_FIRST_INSTANCE) != 0;
    static uint32_t *alike_of; // Per step: 1 + its run of steps that draw alike, 0 for none...
    static bool *alike_culled; // ...and per run, whether one of its steps is culled
    static uint32_t alike_capacity;
    if (step_count > alike_capacity) {
        alike_of = tide_realloc(alike_of, alike_capacity * sizeof *alike_of, step_count * sizeof *alike_of);
        alike_culled = tide_realloc(alike_culled, alike_capacity * sizeof *alike_culled, step_count * sizeof *alike_culled);
        alike_capacity = step_count;
    }
    uint32_t runs_alike = 0;
    bool open = false;  // The last run of them takes the next step, where that draws alike...
    bool tests = false; // ...and whether its first tests occlusion, which every culled step in it then does
    for (uint32_t i = 0; i < step_count; i++) {
        draw_step *s = &steps[i];
        alike_of[i] = 0;
        const bool culled = s->kind == STEP_SHADED && s->culled;
        if (!culled && !cull_listed_takes(list, s)) {
            open = false;
            continue;
        }
        const tide_draw_run *run = &list->runs[s->first];
        const bool joins = joining && pipeline_made(s) && mesh_drawable(list, run->mesh) && !cull_numbered(run->shader);
        const bool hiz = culled && hiz_member(list, s);
        // ...and every culled one tests occlusion or none does: a span (hiz_plan) is culled again before its first
        // step, so a join drawn before it, or drawn from within it, can't take in steps on the other side of its edge
        if (open && joins && alike_of[i - 1u] && cull_joins_with(list, &steps[i - 1u], s) && (!culled || hiz == tests)) {
            alike_of[i] = runs_alike;
        } else {
            alike_of[i] = ++runs_alike;
            alike_culled[runs_alike - 1u] = false;
            tests = hiz;
        }
        alike_culled[alike_of[i] - 1u] |= culled;
        open = joins;
    }
    uint32_t groups = 0, total = 0, last = 0;
    for (uint32_t i = 0; i < step_count; i++) {
        draw_step *s = &steps[i];
        if (!alike_of[i] || !alike_culled[alike_of[i] - 1u]) continue; // Listed runs with no culled one: as they are
        s->join = 0;
        const tide_draw_run *run = &list->runs[s->first];
        const uint32_t level = list->meshes[run->mesh - 1u].level;
        if (cull_bucket_count == cull_bucket_capacity) {
            const uint32_t capacity = cull_bucket_capacity ? cull_bucket_capacity * 2u : 64u;
            cull_buckets = tide_realloc(cull_buckets, cull_bucket_capacity * sizeof *cull_buckets, capacity * sizeof *cull_buckets);
            cull_bucket_capacity = capacity;
        }
        const uint32_t b = cull_bucket_count;
        cull_buckets[b] = (cull_bucket){.region = total, .step = i};
        const uint32_t first = groups;
        uint32_t fixed = 0;
        if (s->culled) {
            for (uint32_t j = run->culled - 1u; j < run->culled - 1u + run->culled_count; j++) {
                const tide_draw_cull *job = &list->culls[j];
                if (!tide_draw_cull_in(job, level) || !job->count) continue;
                uint32_t *part = cull_room(CULL_PARTS, CULL_PART_WORDS);
                part[0] = j, part[1] = level, part[2] = groups, part[3] = b;
                groups += (job->count + CULL_GROUP - 1u) / CULL_GROUP;
                total += job->count;
            }
        } else { // An ordinary listed run's: its places, all of them, as the CPU has them (written below)
            fixed = run->count;
            total += fixed;
        }
        uint32_t *bucket = cull_room(CULL_BUCKETS, CULL_BUCKET_WORDS);
        bucket[0] = first, bucket[1] = groups - first, bucket[2] = cull_buckets[b].region, bucket[3] = fixed;
        bucket[4] = gpu_meshes[run->mesh - 1u].index_count;
        s->culled = ++cull_bucket_count;
        if (b && alike_of[i] == last && cull_buckets[b - 1u].step == i - 1u) cull_joins[cull_join_count - 1u].count++;
        else cull_join_add(b, 1);
        last = alike_of[i];
    }
    if (!cull_bucket_count) return;
    // Each run of more than one whose family's index buffer has every one of its indices is a join; the others are a
    // join of each of their buckets, through its own mesh's
    const uint32_t alike = cull_join_count;
    cull_join *same = tide_alloc((size_t)alike * sizeof *same);
    memcpy(same, cull_joins, (size_t)alike * sizeof *same);
    cull_join_count = 0;
    for (uint32_t k = 0; k < alike; k++) {
        const cull_family *f = same[k].count > 1u ? cull_family_for(list, &same[k]) : NULL;
        if (f) {
            cull_join *j = cull_join_add(same[k].first, same[k].count);
            j->family = (uint32_t)(f - cull_families) + 1u, j->wide = f->wide, j->first_instance = true;
            continue;
        }
        for (uint32_t b = same[k].first; b < same[k].first + same[k].count; b++) {
            const gpu_mesh *g = &gpu_meshes[list->runs[steps[cull_buckets[b].step].first].mesh - 1u];
            cull_join *j = cull_join_add(b, 1);
            j->indices = g->indices, j->wide = g->wide;
            cull_buckets[b].first_index = 0;
        }
    }
    free(same);
    for (uint32_t k = 0; k < cull_join_count; k++) {
        const cull_join *j = &cull_joins[k];
        uint32_t *words = cull_room(CULL_JOINS, CULL_JOIN_WORDS);
        words[0] = j->first, words[1] = j->count, words[2] = j->first_instance ? 1u : 0u;
        steps[cull_buckets[j->first].step].join = k + 1u; // Its first step makes its call
        for (uint32_t b = j->first; b < j->first + j->count; b++) {
            cull_words[CULL_BUCKETS][b * CULL_BUCKET_WORDS + 5u] = cull_buckets[b].first_index;
        }
    }
    const tide_gpu_id depths = hiz_buffer_for(hiz_plan(list));
    // What the kernels write: room alone
    const uint32_t buckets = cull_bucket_count;
    cull_word_count[CULL_GROUPS] = groups * CULL_GROUP_WORDS;
    cull_word_count[CULL_SCRATCH] = groups * CULL_GROUP;
    cull_word_count[CULL_PLACES] = total;
    cull_word_count[CULL_COUNTS] = buckets;
    cull_word_count[CULL_ARGS] = buckets * CULL_ARG_WORDS;
    cull_word_count[CULL_DRAWS] = cull_join_count;
    cull_buffer(CULL_CAMERAS, list->cull_camera_count, true);
    cull_buffer(CULL_JOBS, list->cull_count, true);
    cull_buffer(CULL_PARTS, cull_word_count[CULL_PARTS] / CULL_PART_WORDS, true);
    cull_buffer(CULL_BUCKETS, buckets, true);
    cull_buffer(CULL_GROUPS, groups, false);
    cull_buffer(CULL_SCRATCH, groups * CULL_GROUP, false);
    cull_buffer(CULL_PLACES, total, false);
    for (uint32_t b = 0; b < buckets; b++) { // Ordinary listed runs' places, as they are
        const draw_step *s = &steps[cull_buckets[b].step];
        if (!cull_fixed(list, s)) continue;
        const tide_draw_run *run = &list->runs[s->first];
        gpu->buffer_write(cull_places(), cull_places_at(b), list->instances + run->first, (size_t)run->count * sizeof(uint32_t));
    }
    cull_buffer(CULL_COUNTS, buckets, false);
    cull_buffer(CULL_JOINS, cull_join_count, true);
    cull_buffer(CULL_ARGS, buckets * CULL_ARG_WORDS, false);
    cull_buffer(CULL_DRAWS, cull_join_count, false);
    // The pages' sizes, as their lists' counts, where CullListed's uniforms have them
    for (uint32_t p = 0; p < 4; p++) {
        const tide_draw_texture *t = p < cull_page_count ? &list->textures[cull_pages[p] - 1u] : NULL;
        cull_page_counts[p] = t && t->width > 0 && t->height > 0 ? t->width * t->height : 0;
    }
    int32_t uniforms[16] = {0};
    gpu->compute_begin();
    if (groups) cull_listed(list, uniforms, depths, groups);
    cull_places_of(0, buckets, 0, groups, 0, cull_join_count);
    gpu->compute_end();
}

// A step culled on the GPU, drawn: its join's records, as many as CullDraws wrote, by the join's first step; the
// others are the join's, and draw nothing of their own. False where it made no call.
static bool cull_draw(const draw_step *s)
{
    if (!s->join || s->join > cull_join_count) return false;
    cull_join *j = &cull_joins[s->join - 1u];
    // With first instances each record's places start where its own do, all bound from the first; else its one's
    gpu->vertices(1, cull_places(), j->first_instance ? (size_t)TIDE_GPU_LIST_HEADER_WORDS * sizeof(uint32_t)
                                                      : cull_places_at(j->first));
    gpu->indices(j->family ? cull_families[j->family - 1u].buffer : j->indices, j->wide);
    gpu->draw_indexed_indirect_count(cull_args(), cull_args_at(j->first), j->count, cull_buffers[CULL_DRAWS].buffer,
                                     ((size_t)TIDE_GPU_LIST_HEADER_WORDS + s->join - 1u) * sizeof(uint32_t));
    j->drawn = true;
    return true;
}

// Whether span `span` is measured just before step `i` (outside passes): 1 at its first, or from its `at` on where no
// pass is going on (`active`); 0 not yet; -1 never, the loop having passed its first by (its destination not drawn).
static int cull_span_due(const cull_span *span, const uint32_t i, const bool active)
{
    if (span->done || i < span->at) return 0;
    if (i > span->first) return -1;
    return i == span->first || !active ? 1 : 0;
}

// Whether a span is measured just before step `i`, the pass going on or not: one at its first is, ending the pass,
// and with it any whose `at` is past.
static bool cull_spans_at(const uint32_t i, const bool active)
{
    bool due = false;
    for (uint32_t k = cull_span_next; k < cull_span_count; k++) {
        const int d = cull_span_due(&cull_spans[k], i, active);
        if (d < 0) cull_spans[k].done = true; // Its draws keep the frame's culling
        due |= d > 0;
    }
    while (cull_span_next < cull_span_count && cull_spans[cull_span_next].done) cull_span_next++;
    return due;
}

// Just before step `i`, outside passes: each span due there measured, its draws culled again against the pyramid of
// the depth drawn into its destination so far (phase 1), their joins' records and counts written again from their
// buckets' new counts (CullDraws), in one compute pass. The pyramid is made there (hiz kernel, one dispatch) unless
// its room holds one made since the last pass on that destination began. None where the destination hasn't begun a
// pass this frame (what it holds may not be what its first pass starts with, as the window's is cleared then), or
// gives no depth to measure: the frame's culling stands.
static void cull_spans_start(const tide_draw_list *list, const uint32_t i)
{
    enum { MOST = 16 };
    uint32_t due[MOST], count = 0;
    tide_gpu_id depth[MOST];
    uint32_t samples[MOST];
    for (uint32_t k = cull_span_next; k < cull_span_count && count < MOST; k++) {
        cull_span *span = &cull_spans[k];
        if (cull_span_due(span, i, false) <= 0) continue;
        span->done = true;
        const frame_target *f = &frame_targets[span->target];
        hiz_room *room = &hiz_rooms[span->room];
        if (!f->begun || !f->valid || !span->groups || !gpu->depth_now) continue;
        depth[count] = 0;
        samples[count] = 1;
        if (!room->made || room->passes != f->passes) { // Drawn into since: made again
            room->made = false;
            for (uint32_t j = 0; j < count && !depth[count]; j++) { // Another span here measures the same
                if (cull_spans[due[j]].room == span->room) depth[count] = depth[j], samples[count] = samples[j];
            }
            if (!depth[count]) depth[count] = gpu->depth_now(f->gpu, &samples[count]);
            if (!depth[count] || !pipeline_usable(hiz_pipelines[samples[count] > 1u])) continue;
        }
        due[count++] = k;
    }
    while (cull_span_next < cull_span_count && cull_spans[cull_span_next].done) cull_span_next++;
    if (!count) return;
    gpu->compute_begin();
    for (uint32_t d = 0; d < count; d++) {
        const cull_span *span = &cull_spans[due[d]];
        hiz_room *room = &hiz_rooms[span->room];
        if (room->made) continue;
        const frame_target *f = &frame_targets[span->target];
        const uint32_t width = (uint32_t)f->pixel_width, height = (uint32_t)f->pixel_height;
        const uint32_t across = hiz_size((int)width, 4), down = hiz_size((int)height, 4); // A group's level 4 texel
        const uint32_t uniforms[8] = {width, height, hiz_levels((int)width, (int)height), 0,
                                      TIDE_GPU_LIST_HEADER_WORDS + room->at, across * down,
                                      TIDE_GPU_LIST_HEADER_WORDS + HIZ_DONE, 0};
        gpu->use(hiz_pipelines[samples[d] > 1u]);
        gpu->uniforms(uniforms, sizeof uniforms);
        gpu->sample(0, depth[d], gpu_filter(TIDE_FILTER_POINT), gpu_wrap(TIDE_WRAP_CLAMP));
        gpu->storage(0, hiz_buffer.buffer);
        gpu->dispatch(across, down, 1);
        room->made = true;
        room->passes = f->passes;
        hiz_report.pyramids++;
    }
    for (uint32_t d = 0; d < count; d++) { // Each span's draws culled again, as the frame's were, and against it
        const cull_span *span = &cull_spans[due[d]];
        const frame_target *f = &frame_targets[span->target];
        int32_t uniforms[16] = {0};
        uniforms[tide_kernel_CullListed_INT_phase] = 1;
        uniforms[tide_kernel_CullListed_INT_first] = (int32_t)span->first_group;
        uniforms[tide_kernel_CullListed_INT_width] = f->pixel_width;
        uniforms[tide_kernel_CullListed_INT_height] = f->pixel_height;
        uniforms[tide_kernel_CullListed_INT_levels] = (int32_t)hiz_levels(f->pixel_width, f->pixel_height);
        uniforms[tide_kernel_CullListed_INT_pyramid] = (int32_t)hiz_rooms[span->room].at;
        cull_listed(list, uniforms, hiz_buffer.buffer, span->groups);
        cull_places_of(span->first_bucket, span->buckets, span->first_group, span->groups, span->first_join, span->joins);
        hiz_report.measured++;
    }
    gpu->compute_end();
}

// After the frame's steps: the counts its culling found begin their way back, where the last frame's are back.
static void cull_frame_end(void)
{
    if (!cull_on || !hiz_report.pyramids || hiz_read || !hiz_buffer.buffer || !gpu->buffer_read) return;
    hiz_read = gpu->buffer_read(hiz_buffer.buffer, (size_t)TIDE_GPU_LIST_HEADER_WORDS * 4u, HIZ_COUNTS * 4u);
}

bool tide_platform_occlusion(tide_platform_occlusion_report *report, const bool wait)
{
    if (wait && hiz_read && gpu && gpu->buffer_read_wait) gpu->buffer_read_wait();
    hiz_poll();
    *report = hiz_report;
    return hiz_report.counted;
}


bool tide_platform_cull_compare(const tide_draw_list *list, tide_platform_cull_report *report)
{
    *report = (tide_platform_cull_report){.failed = cull_failed};
    const uint32_t buckets = cull_bucket_count, joins = cull_join_count;
    for (uint32_t b = 0; b < buckets; b++) {
        if (list && cull_fixed(list, &steps[cull_buckets[b].step])) report->listed++;
        else report->runs++;
    }
    for (uint32_t k = 0; k < joins; k++) report->calls += cull_joins[k].drawn;
    if (!list || !gpu || !gpu->buffer_read || !gpu->buffer_read_wait || !gpu->buffer_read_done) return false;
    if (!buckets || !cull_args() || !cull_places() || !cull_buffers[CULL_COUNTS].buffer || !cull_buffers[CULL_DRAWS].buffer)
        return false;
    const size_t args_bytes = cull_args_at(buckets), places_bytes = (size_t)TIDE_GPU_LIST_HEADER_WORDS * 4u
                                                                    + (size_t)cull_word_count[CULL_PLACES] * 4u;
    const size_t counts_bytes = ((size_t)TIDE_GPU_LIST_HEADER_WORDS + buckets) * 4u;
    const size_t draws_bytes = ((size_t)TIDE_GPU_LIST_HEADER_WORDS + joins) * 4u;
    uint32_t *args = tide_alloc(args_bytes), *places = tide_alloc(places_bytes), *counts = tide_alloc(counts_bytes);
    uint32_t *draws = tide_alloc(draws_bytes);
    const uint32_t reads[4] = {gpu->buffer_read(cull_args(), 0, args_bytes), gpu->buffer_read(cull_places(), 0, places_bytes),
                               gpu->buffer_read(cull_buffers[CULL_COUNTS].buffer, 0, counts_bytes),
                               gpu->buffer_read(cull_buffers[CULL_DRAWS].buffer, 0, draws_bytes)};
    gpu->buffer_read_wait();
    bool read = true;
    uint32_t *const into[4] = {args, places, counts, draws};
    for (uint32_t k = 0; k < 4; k++) read &= reads[k] && gpu->buffer_read_done(reads[k], into[k]) > 0;
    const uint32_t *bucket_words = cull_words[CULL_BUCKETS];
    for (uint32_t b = 0; read && b < buckets; b++) {
        const tide_draw_run *run = &list->runs[steps[cull_buckets[b].step].first];
        const uint32_t found = counts[TIDE_GPU_LIST_HEADER_WORDS + b];
        const uint32_t *theirs = places + TIDE_GPU_LIST_HEADER_WORDS + cull_buckets[b].region;
        uint32_t n;
        if (cull_fixed(list, &steps[cull_buckets[b].step])) { // An ordinary listed run's: all its places, as they are
            n = run->count;
            if (n > cull_cpu_capacity) {
                cull_cpu = tide_realloc(cull_cpu, (size_t)cull_cpu_capacity * sizeof(uint32_t), (size_t)n * sizeof(uint32_t));
                cull_cpu_capacity = n;
            }
            memcpy(cull_cpu, list->instances + run->first, (size_t)n * sizeof(uint32_t));
        } else {
            n = cull_decide(list, run);
        }
        // The CPU's, in order, among the GPU's
        uint32_t matched = 0;
        for (uint32_t k = 0; k < found && matched < n; k++) matched += theirs[k] == cull_cpu[matched];
        report->gpu += found;
        report->cpu += n;
        report->missing += n - matched;
        report->extra += found - matched;
    }
    // Each join's records: those of its buckets that kept any, in order, then none, and how many
    for (uint32_t k = 0; read && k < joins; k++) {
        const cull_join *j = &cull_joins[k];
        uint32_t drawn = 0;
        for (uint32_t b = j->first; b < j->first + j->count; b++) {
            const uint32_t found = counts[TIDE_GPU_LIST_HEADER_WORDS + b];
            if (!found) continue;
            const uint32_t *r = args + TIDE_GPU_LIST_HEADER_WORDS + (size_t)(j->first + drawn) * CULL_ARG_WORDS;
            const uint32_t want[CULL_ARG_WORDS] = {bucket_words[b * CULL_BUCKET_WORDS + 4u], found,
                                                   cull_buckets[b].first_index, 0, j->first_instance ? cull_buckets[b].region : 0u};
            report->misdrawn += memcmp(r, want, sizeof want) != 0;
            drawn++;
        }
        for (uint32_t r = j->first + drawn; r < j->first + j->count; r++) {
            const uint32_t *w = args + TIDE_GPU_LIST_HEADER_WORDS + (size_t)r * CULL_ARG_WORDS;
            report->misdrawn += (w[0] | w[1] | w[2] | w[3] | w[4]) != 0;
        }
        report->misdrawn += draws[TIDE_GPU_LIST_HEADER_WORDS + k] != drawn;
        report->draws += drawn;
        report->joined += j->count > 1u ? j->count : 0u;
    }
    free(args);
    free(places);
    free(counts);
    free(draws);
    return read;
}

// ---------------------------------------------------------------------------
// What GPU-decided draws drew (counted_*, above)

// An indirect draw over GPU lists was made: its record is read back with the frame's.
static void counted_step(const uint32_t step)
{
    if (counted_step_count == counted_step_capacity) {
        const uint32_t capacity = counted_step_capacity ? counted_step_capacity * 2u : 16u;
        counted_steps = tide_realloc(counted_steps, counted_step_capacity * sizeof *counted_steps, capacity * sizeof *counted_steps);
        counted_step_capacity = capacity;
    }
    counted_steps[counted_step_count++] = step;
}

static void counted_drawn_room(counted_frame *c, const uint32_t records)
{
    const uint32_t words = (records + 31u) / 32u;
    if (words > c->drawn_capacity) {
        c->drawn = tide_realloc(c->drawn, c->drawn_capacity * sizeof *c->drawn, words * sizeof *c->drawn);
        c->drawn_capacity = words;
    }
    memset(c->drawn, 0, words * sizeof *c->drawn);
}

static void counted_part_add(counted_frame *c, const counted_part part)
{
    if (c->part_count == c->part_capacity) {
        const uint32_t capacity = c->part_capacity ? c->part_capacity * 2u : 64u;
        c->parts = tide_realloc(c->parts, c->part_capacity * sizeof *c->parts, capacity * sizeof *c->parts);
        c->part_capacity = capacity;
    }
    c->parts[c->part_count++] = part;
}

// A frame's words are here: what its draws drew, counted where it's the newest frame here, and its rows.
static void counted_take(const counted_frame *c, const uint32_t *words)
{
    uint64_t vertices = 0;
    uint32_t draws = 0;
    for (uint32_t r = 0; r < c->records; r++) {
        if (!(c->drawn[r / 32u] >> (r % 32u) & 1u)) continue;
        const uint32_t *w = words + (size_t)r * CULL_ARG_WORDS;
        if (!w[1] || !w[0]) continue;
        draws++;
        vertices += (uint64_t)w[0] * w[1];
    }
    const uint32_t *groups = words + (size_t)c->records * CULL_ARG_WORDS;
    for (uint32_t p = 0; p < c->part_count; p++) {
        const counted_part *part = &c->parts[p];
        uint64_t kept = 0;
        for (uint32_t g = part->first; g < part->first + part->groups && g < c->groups; g++) kept += groups[(size_t)g * 2u];
        tide_draw_geometry_add(c->diagnostics, c->revision, part->row, kept);
    }
    if (c->serial > counted_newest) {
        counted_newest = c->serial;
        counted_vertices = vertices;
        counted_draws = draws;
    }
}

// As a frame starts: the reads that are here.
static void counted_poll(void)
{
    for (uint32_t i = 0; gpu->buffer_read_done && i < COUNTED_FRAMES; i++) {
        counted_frame *c = &counted[i];
        if (!c->token) continue;
        const uint32_t words = c->records * CULL_ARG_WORDS + c->groups * CULL_GROUP_WORDS;
        if (words > counted_word_capacity) {
            counted_words = tide_realloc(counted_words, counted_word_capacity * sizeof *counted_words, words * sizeof *counted_words);
            counted_word_capacity = words;
        }
        const int done = gpu->buffer_read_done(c->token, counted_words);
        if (!done) continue;
        c->token = 0;
        if (done > 0) counted_take(c, counted_words);
    }
}

// As a frame's drawing ends: its GPU-decided draws' records copied together and read back.
static void counted_frame_end(const tide_draw_list *list)
{
    const uint64_t serial = ++counted_serial;
    bool culled = false;
    for (uint32_t k = 0; k < cull_join_count; k++) culled |= cull_joins[k].drawn;
    const uint32_t from = culled ? cull_bucket_count : 0u, records = from + counted_step_count;
    counted_frame *c = NULL;
    for (uint32_t i = 0; i < COUNTED_FRAMES && !c; i++) c = counted[i].token ? NULL : &counted[i];
    const uint32_t group_count = culled && list->diagnostics ? cull_word_count[CULL_GROUPS] / CULL_GROUP_WORDS : 0u;
    const uint64_t bytes = ((uint64_t)records * CULL_ARG_WORDS + (uint64_t)group_count * CULL_GROUP_WORDS) * sizeof(uint32_t);
    if (!records || !gpu->buffer_read || !gpu->buffer_copy) { // None: the GPU drew nothing the CPU doesn't know of
        counted_newest = serial;
        counted_vertices = 0;
        counted_draws = 0;
    } else if (c && bytes <= TIDE_GPU_STORAGE_MOST_BYTES) {
        if (!c->buffer || c->capacity < bytes) {
            if (c->buffer) free_buffer(c->buffer);
            uint64_t room = c->capacity ? c->capacity : 1024u;
            while (room < bytes) room *= 2u;
            c->buffer = made_buffer(TIDE_GPU_STORAGE, (size_t)room, NULL);
            c->capacity = c->buffer ? (uint32_t)room : 0u;
        }
        if (c->buffer) {
            c->records = records;
            c->groups = group_count;
            c->part_count = 0;
            c->diagnostics = list->diagnostics;
            c->revision = list->diagnostics ? list->diagnostics->revision : 0u;
            counted_drawn_room(c, records);
            if (culled) {
                gpu->buffer_copy(cull_args(), cull_args_at(0), c->buffer, 0, (size_t)from * CULL_ARG_WORDS * sizeof(uint32_t));
                for (uint32_t k = 0; k < cull_join_count; k++) {
                    const cull_join *j = &cull_joins[k];
                    for (uint32_t r = j->first; j->drawn && r < j->first + j->count; r++) c->drawn[r / 32u] |= 1u << (r % 32u);
                }
            }
            for (uint32_t k = 0; k < counted_step_count; k++) {
                const draw_step *s = &steps[counted_steps[k]];
                const uint32_t r = from + k;
                // A draw of corners has four words (count, instances, first, first instance), an indexed one five
                const uint32_t words = list->runs[s->first].shader->index_storage ? 4u : 5u;
                gpu->buffer_copy(s->indirect, 0, c->buffer, (size_t)r * CULL_ARG_WORDS * sizeof(uint32_t), words * sizeof(uint32_t));
                c->drawn[r / 32u] |= 1u << (r % 32u);
            }
            if (group_count) { // Each culled draw's, at each level, for its geometry rows
                gpu->buffer_copy(cull_buffers[CULL_GROUPS].buffer, (size_t)TIDE_GPU_LIST_HEADER_WORDS * sizeof(uint32_t),
                                 c->buffer, (size_t)records * CULL_ARG_WORDS * sizeof(uint32_t),
                                 (size_t)group_count * CULL_GROUP_WORDS * sizeof(uint32_t));
                const uint32_t parts = cull_word_count[CULL_PARTS] / CULL_PART_WORDS;
                for (uint32_t p = 0; p < parts; p++) {
                    const uint32_t *part = cull_words[CULL_PARTS] + (size_t)p * CULL_PART_WORDS;
                    const tide_draw_cull *job = part[0] < list->cull_count ? &list->culls[part[0]] : NULL;
                    const uint32_t level = part[1], bucket = part[3];
                    // Its bucket's records are drawn bits already (above)
                    const bool drawn = bucket < from && (c->drawn[bucket / 32u] >> (bucket % 32u) & 1u);
                    if (!job || level > TIDE_MESH_LEVELS || !job->rows[level] || !drawn || job->rows_revision != c->revision)
                        continue;
                    counted_part_add(c, (counted_part){.row = job->rows[level], .first = part[2],
                                                       .groups = (job->count + CULL_GROUP - 1u) / CULL_GROUP});
                }
            }
            c->serial = serial;
            c->token = gpu->buffer_read(c->buffer, 0, (size_t)bytes);
        }
    }
    // ...else every place waits for a read: this frame isn't counted, and the newest counted stays
    counted_step_count = 0;
}

