// Compute in the renderer (notes/compute.md): GPU lists' ranges in two pools, kernels' runs and the engine's own
// kernels around them (shaders.h: a run over a GPU list's count, ordered Add's offsets and scatter, and a range
// zeroed), list operations in frame order, indirect draws, and reads back. Included by platform.c, which turns the
// draw list's TIDE_DRAW_COMPUTE and TIDE_DRAW_GPU_LIST commands into STEP_COMPUTE and STEP_GPU_OP steps, done
// outside passes in their order.

// ---------------------------------------------------------------------------
// Pools (notes/compute.md, GPU lists in pools): every GPU list is a range of one of two storage buffers, that of
// lists of ints (GPUList<int>, which atomics take: a kernel that uses atomics declares that pool atomic, so the other
// lists it reads stay plain) and that of the rest; the culling's lists (cull.inc) and Adds' scratch are ranges of
// them too. A shader binds both pools and finds each of its lists by a table in its uniforms (tide_shader's
// storage_table): its range's first word and its capacity. Each pool starts with an empty list's header at word 0,
// which a table of zeros names, and a list that isn't there, or that a pool can't hold, is read as that one.
//
// A pool grows by being made again twice as big, its words copied on the GPU in order with the frame's work, so a
// range's place never changes while it's had. Only outside passes; where it can't grow (the device's most, gpu.h's
// storage_most), what asked is left out and said once. Ranges let go of are taken again first, zeroed for a list.

enum { POOL_FIRST_WORDS = 65536u, POOL_STEP = 4u }; // A pool's first 256 KiB; ranges in steps of 16 bytes

typedef struct gpu_range {
    uint32_t pool;  // TIDE_STORAGE_WORDS or TIDE_STORAGE_INTS
    uint32_t at;    // Its first word there
    uint32_t words; // 0 for none
} gpu_range;

typedef struct pool_span {
    uint32_t at, words;
} pool_span;

typedef struct gpu_pool {
    tide_gpu_id buffer;
    uint32_t words;  // Its size
    uint32_t used;   // Words from here on were never a range's since it was made: zeros
    pool_span *free; // Ranges let go of, below `used`, in order and joined where they touch
    uint32_t free_count, free_capacity;
    bool said;       // That it can't hold what was asked, once
} gpu_pool;

static gpu_pool gpu_pools[2];

// The most words a pool grows to: what a storage buffer bound whole is on this device, or TIDE_GPU_POOL_MOST's
// bytes where less (for tests), under 4 GiB.
static uint32_t pool_most_words(void)
{
    static uint64_t asked = UINT64_MAX;
    if (asked == UINT64_MAX) {
        const char *said = getenv("TIDE_GPU_POOL_MOST");
        asked = said && *said ? strtoull(said, NULL, 10) : 0u;
    }
    uint64_t bytes = gpu->storage_most ? gpu->storage_most() : TIDE_GPU_STORAGE_MOST_BYTES;
    if (asked && asked < bytes) bytes = asked;
    if (bytes > UINT32_MAX - 15u) bytes = UINT32_MAX - 15u;
    return (uint32_t)(bytes / 4u) & ~(POOL_STEP - 1u);
}

static void pool_say(gpu_pool *p, const uint32_t pool, const uint64_t words)
{
    if (p->said) return;
    p->said = true;
    fprintf(stderr, "tide: GPU lists %s take more than this GPU binds as one buffer (%llu bytes): what asked for %llu "
                    "bytes more is left out, a list of it empty\n", pool == TIDE_STORAGE_INTS ? "of ints" : "of other elements",
            (unsigned long long)pool_most_words() * 4u, (unsigned long long)words * 4u);
}

// The pool made, or made again bigger with what it holds, for at least `need` words. Outside passes.
static bool pool_grow(gpu_pool *p, const uint32_t pool, const uint64_t need)
{
    if (passing) return false;
    const uint32_t most = pool_most_words();
    uint64_t size = p->words ? (uint64_t)p->words * 2u : POOL_FIRST_WORDS;
    while (size < need) size *= 2u;
    if (size > most) size = most;
    if (size < need || size < POOL_STEP) {
        pool_say(p, pool, need - p->used);
        return false;
    }
    const tide_gpu_id buffer = made_buffer(TIDE_GPU_STORAGE, (size_t)size * 4u, NULL);
    if (!buffer) return false;
    if (p->buffer) {
        if (p->used) gpu->buffer_copy(p->buffer, 0, buffer, 0, (size_t)p->used * 4u);
        free_buffer(p->buffer);
    } else {
        p->used = POOL_STEP; // Word 0's header, every pool's empty list: a table of zeros names it
    }
    p->buffer = buffer;
    p->words = (uint32_t)size;
    return true;
}

static tide_gpu_id pool_buffer(const uint32_t pool)
{
    return gpu_pools[pool].buffer;
}

static bool pools_ready(void)
{
    for (uint32_t k = 0; k < 2; k++) {
        if (!gpu_pools[k].buffer && !pool_grow(&gpu_pools[k], k, POOL_STEP)) return false;
    }
    return true;
}

// A range of `words` words in pool `pool`, its first word, or UINT32_MAX where the pool can't hold it; `dirty` says
// whether a range let go of before had it (so it holds what that one left).
static uint32_t pool_take(const uint32_t pool, uint64_t words, bool *dirty)
{
    gpu_pool *p = &gpu_pools[pool];
    words = (words + POOL_STEP - 1u) / POOL_STEP * POOL_STEP;
    if (!words) words = POOL_STEP;
    *dirty = false;
    if (!p->buffer && !pool_grow(p, pool, POOL_STEP)) return UINT32_MAX;
    for (uint32_t i = 0; i < p->free_count; i++) {
        pool_span *f = &p->free[i];
        if (f->words < words) continue;
        const uint32_t at = f->at;
        f->at += (uint32_t)words;
        f->words -= (uint32_t)words;
        if (!f->words) {
            memmove(f, f + 1, (size_t)(p->free_count - i - 1u) * sizeof *f);
            p->free_count--;
        }
        *dirty = true;
        return at;
    }
    if ((uint64_t)p->used + words > p->words && !pool_grow(p, pool, (uint64_t)p->used + words)) {
        pool_say(p, pool, words);
        return UINT32_MAX;
    }
    const uint32_t at = p->used;
    p->used += (uint32_t)words;
    return at;
}

static void pool_give(const gpu_range r)
{
    if (!r.words || r.pool > 1u) return;
    gpu_pool *p = &gpu_pools[r.pool];
    if (!p->buffer) return;
    uint32_t i = 0;
    while (i < p->free_count && p->free[i].at < r.at) i++;
    const bool joins_before = i > 0 && p->free[i - 1u].at + p->free[i - 1u].words == r.at;
    const bool joins_after = i < p->free_count && r.at + r.words == p->free[i].at;
    if (joins_before && joins_after) {
        p->free[i - 1u].words += r.words + p->free[i].words;
        memmove(&p->free[i], &p->free[i + 1u], (size_t)(p->free_count - i - 1u) * sizeof *p->free);
        p->free_count--;
    } else if (joins_before) {
        p->free[i - 1u].words += r.words;
    } else if (joins_after) {
        p->free[i].at = r.at;
        p->free[i].words += r.words;
    } else {
        if (p->free_count == p->free_capacity) {
            const uint32_t capacity = p->free_capacity ? p->free_capacity * 2u : 16u;
            p->free = tide_realloc(p->free, p->free_capacity * sizeof *p->free, capacity * sizeof *p->free);
            p->free_capacity = capacity;
        }
        memmove(&p->free[i + 1u], &p->free[i], (size_t)(p->free_count - i) * sizeof *p->free);
        p->free[i] = (pool_span){r.at, r.words};
        p->free_count++;
    }
}

// A range of `words` in pool `pool`: none (words 0) where the pool can't hold it.
static gpu_range pool_range(const uint32_t pool, const uint64_t words, bool *dirty)
{
    const uint32_t at = pool_take(pool, words, dirty);
    if (at == UINT32_MAX) return (gpu_range){0};
    return (gpu_range){pool, at, (uint32_t)((words + POOL_STEP - 1u) / POOL_STEP * POOL_STEP)};
}

// ---------------------------------------------------------------------------
// Lists' ranges, by their ids. A hot reload (another epoch of the draw list) leaves records whose states may be
// gone: they're kept only for a list the new build's first frame names, and nothing reads their states before.

typedef struct gpu_list_buffer {
    uint64_t id;
    uint64_t version;
    uint64_t words;    // What it was made for: the header and capacity * stride
    uint32_t epoch;    // The draw list's epoch it was last named in
    uint32_t capacity; // Its elements, as its range was made for
    gpu_range range;
} gpu_list_buffer;

static gpu_list_buffer *gpu_list_buffers;
static uint32_t gpu_list_count, gpu_list_capacity;
static uint32_t gpu_list_epoch;

static tide_gpu_id *run_buffers; // Each run's record, a buffer of TIDE_GPU_RUN_WORDS words, the frame's k-th run's k-th
static uint32_t run_buffer_count, run_buffer_capacity, run_buffers_used;
typedef struct sized_buffer {
    tide_gpu_id buffer;
    uint64_t bytes;
} sized_buffer;
static sized_buffer *scratch_buffers; // Indirect draws' arguments, taken in turn each frame
static uint32_t scratch_count, scratch_capacity, scratch_used;
static gpu_range *add_scratch[2]; // Adds' scratch in each pool, taken in turn each frame
static uint32_t add_scratch_count[2], add_scratch_capacity[2], add_scratch_used[2];

typedef struct kernel_pipeline {
    uint64_t id;
    tide_gpu_id pipeline;
    uint32_t frame;
} kernel_pipeline;
static kernel_pipeline *kernel_pipelines;
static uint32_t kernel_pipeline_count, kernel_pipeline_capacity;
static tide_gpu_id run_size_pipeline, add_offsets_pipeline, add_scatter_pipeline, zero_words_pipeline;

// Everything on the device went with it: lists come back empty (tide_gpu_list_made says so to the game), and reads
// begun there fail.
static void forget_compute(void)
{
    gpu_list_count = 0;
    for (uint32_t k = 0; k < 2; k++) {
        gpu_pools[k].buffer = 0;
        gpu_pools[k].words = gpu_pools[k].used = gpu_pools[k].free_count = 0;
        add_scratch_count[k] = add_scratch_used[k] = 0;
    }
    run_buffer_count = run_buffers_used = 0;
    scratch_count = scratch_used = 0;
    kernel_pipeline_count = 0;
    run_size_pipeline = add_offsets_pipeline = add_scatter_pipeline = zero_words_pipeline = 0;
    forget_culling();
    tide_gpu_read_request *reads = tide_gpu_read_requests();
    for (uint32_t i = 0; i < TIDE_GPU_READS; i++) {
        if (reads[i].state == TIDE_GPU_READ_BEGUN) tide_gpu_read_finished(&reads[i], false);
    }
}

static tide_gpu_id compute_pipeline(const char *name, const char *wgsl, const uint32_t *spirv, const uint32_t words,
                                    const uint32_t storage, const uint32_t writes)
{
    tide_gpu_pipeline_desc desc = {.name = name, .wgsl = wgsl, .spirv = spirv, .spirv_words = words, .uniform_size = 16,
                                   .compute = true, .storage_count = storage};
    for (uint32_t i = 0; i < storage; i++) desc.storage_writes[i] = (writes >> i & 1u) != 0;
    return gpu->pipeline(&desc);
}

static bool pipeline_usable(const tide_gpu_id pipeline)
{
    return pipeline && (!gpu->pipeline_ready || gpu->pipeline_ready(pipeline))
        && !(gpu->pipeline_failed && gpu->pipeline_failed(pipeline));
}

static bool engine_kernels(void)
{
    if (!run_size_pipeline) {
        run_size_pipeline = compute_pipeline("runs over a GPU list's count", run_size_wgsl, SPIRV_OF(run_size),
                                             SPIRV_WORDS(run_size), 2, 2u);
        add_offsets_pipeline = compute_pipeline("ordered Adds' offsets", add_offsets_wgsl, SPIRV_OF(add_offsets),
                                                SPIRV_WORDS(add_offsets), 2, 1u);
        add_scatter_pipeline = compute_pipeline("ordered Adds' elements", add_scatter_wgsl, SPIRV_OF(add_scatter),
                                                SPIRV_WORDS(add_scatter), 1, 1u);
        zero_words_pipeline = compute_pipeline("GPU lists' words zeroed", zero_words_wgsl, SPIRV_OF(zero_words),
                                               SPIRV_WORDS(zero_words), 1, 1u);
    }
    return pipeline_usable(run_size_pipeline) && pipeline_usable(add_offsets_pipeline) && pipeline_usable(add_scatter_pipeline);
}

static uint32_t groups_of(const uint32_t size, const uint32_t group)
{
    return (uint32_t)(((uint64_t)size + group - 1u) / group);
}

// `words` words of pool `pool` from `at` zeroed, in order with the frame's work: by the engine's kernel where it's
// made, else written from the CPU. Outside passes.
static void zero_words(const uint32_t pool, const uint32_t at, const uint32_t words)
{
    if (!words) return;
    engine_kernels();
    if (pipeline_usable(zero_words_pipeline)) {
        enum { EACH = 256u * 64u }; // Words a group, 64 an invocation
        uint32_t groups = (uint32_t)(((uint64_t)words + EACH - 1u) / EACH);
        if (groups > 65535u) groups = 65535u; // Each invocation goes on through what's left, by the dispatch's width
        const uint32_t uniforms[4] = {at, words, 0, 0};
        gpu->compute_begin();
        gpu->use(zero_words_pipeline);
        gpu->uniforms(uniforms, sizeof uniforms);
        gpu->storage(0, pool_buffer(pool));
        gpu->dispatch(groups, 1, 1);
        gpu->compute_end();
        return;
    }
    static const uint32_t zeros[4096];
    for (uint32_t done = 0; done < words;) {
        const uint32_t n = words - done < 4096u ? words - done : 4096u;
        gpu->buffer_write(pool_buffer(pool), ((size_t)at + done) * 4u, zeros, (size_t)n * 4u);
        done += n;
    }
}

// The record of the GPU list `id`, its range made where it has none yet or its version changed (empty, its header
// written; `make`, outside passes), or NULL for a list that isn't there or that its pool can't hold.
static const gpu_list_buffer *list_range(const uint64_t id, const bool make)
{
    if (!id) return NULL;
    gpu_list_buffer *b = NULL;
    for (uint32_t i = 0; i < gpu_list_count && !b; i++) {
        if (gpu_list_buffers[i].id == id) b = &gpu_list_buffers[i];
    }
    if (b) b->epoch = gpu_list_epoch; // Named in this build's list: its state is this build's to read
    const tide_gpu_list_state *s = tide_gpu_list_state_of(id);
    if (!s || !s->alive) return NULL;
    const uint32_t pool = s->ints ? TIDE_STORAGE_INTS : TIDE_STORAGE_WORDS;
    const uint64_t words = TIDE_GPU_LIST_HEADER_WORDS + (uint64_t)s->capacity * s->stride;
    // A state no renderer made a range for is a new list, even at the address of one the last build freed (a full
    // reload, or a swap that didn't adopt it): its record, kept for an adopted list, isn't its
    if (b && b->range.words && b->version == s->version && b->range.pool == pool && b->words == words && s->made) return b;
    if (!make || passing) return NULL;
    if (!b) {
        if (gpu_list_count == gpu_list_capacity) {
            const uint32_t capacity = gpu_list_capacity ? gpu_list_capacity * 2u : 16u;
            gpu_list_buffers = tide_realloc(gpu_list_buffers, gpu_list_capacity * sizeof *gpu_list_buffers,
                                            capacity * sizeof *gpu_list_buffers);
            gpu_list_capacity = capacity;
        }
        b = &gpu_list_buffers[gpu_list_count++];
        *b = (gpu_list_buffer){.id = id, .epoch = gpu_list_epoch};
    }
    // Its own range, empty: one taken where another list's was is zeroed first (the pool's tail is zeros)
    pool_give(b->range);
    b->range = (gpu_range){0};
    bool dirty;
    const gpu_range range = pool_range(pool, words, &dirty);
    if (!range.words) return NULL;
    if (dirty) zero_words(pool, range.at + TIDE_GPU_LIST_HEADER_WORDS, range.words - TIDE_GPU_LIST_HEADER_WORDS);
    const uint32_t header[TIDE_GPU_LIST_HEADER_WORDS] = {0, s->capacity, 0, 0};
    gpu->buffer_write(pool_buffer(pool), (size_t)range.at * 4u, header, sizeof header);
    b->range = range;
    b->words = words;
    b->capacity = s->capacity;
    b->version = s->version;
    tide_gpu_list_made(id);
    return b;
}

// A shader's table (tide_shader.storage_table) into its uniforms of the game's, `game`, `size` bytes: each slot's
// list's first word and capacity, the empty list's (word 0, none) for one that isn't there or is of the other pool's
// kind, then a kernel's Adds' scratch's first words (`scratch`). The lists' ranges are made where `make` says.
static void put_table(uint8_t *game, const uint32_t size, const tide_shader *shader, const uint64_t *ids, const bool make,
                      const uint32_t *scratch)
{
    const uint32_t words = tide_shader_table_words(shader);
    if (!words || (uint64_t)shader->storage_table + 4u * words > size) return;
    uint8_t *to = game + shader->storage_table;
    for (uint32_t k = 0; k < shader->storage_count && k < TIDE_SHADER_STORAGE; k++) {
        const uint32_t pool = tide_shader_storage_ints(shader, k) ? TIDE_STORAGE_INTS : TIDE_STORAGE_WORDS;
        const gpu_list_buffer *b = list_range(ids ? ids[k] : 0u, make);
        const uint32_t pair[2] = {b && b->range.pool == pool ? b->range.at : 0u, b && b->range.pool == pool ? b->capacity : 0u};
        memcpy(to + 8u * k, pair, sizeof pair);
    }
    for (uint32_t a = 0; shader->compute && scratch && a < shader->add_count && a < TIDE_SHADER_STORAGE; a++)
        memcpy(to + 8u * shader->storage_count + 4u * a, &scratch[a], sizeof scratch[a]);
}

// Both pools, at their storage slots (TIDE_STORAGE_WORDS, TIDE_STORAGE_INTS).
static void bind_pools(void)
{
    gpu->storage(TIDE_STORAGE_WORDS, pool_buffer(TIDE_STORAGE_WORDS));
    gpu->storage(TIDE_STORAGE_INTS, pool_buffer(TIDE_STORAGE_INTS));
}

// A frame begins: after a hot reload every record waits to be named again; reads asked for long ago go free.
static void compute_frame_start(const tide_draw_list *list)
{
    run_buffers_used = scratch_used = 0;
    add_scratch_used[0] = add_scratch_used[1] = 0;
    if (list->epoch != gpu_list_epoch) gpu_list_epoch = list->epoch; // Records of the last epoch are named again, or go
    tide_gpu_reads_frame();
    // Reads begun last frame are done once its work is, which headless runs wait for: they repeat exactly
    tide_gpu_read_request *reads = tide_gpu_read_requests();
    if (headless && gpu->buffer_read_wait) {
        bool begun = false;
        for (uint32_t i = 0; i < TIDE_GPU_READS; i++) begun |= reads[i].state == TIDE_GPU_READ_BEGUN;
        if (begun) gpu->buffer_read_wait();
    }
    for (uint32_t i = 0; gpu->buffer_read_done && i < TIDE_GPU_READS; i++) {
        tide_gpu_read_request *r = &reads[i];
        if (r->state != TIDE_GPU_READ_BEGUN) continue;
        const int done = gpu->buffer_read_done(r->token, r->data);
        if (done) tide_gpu_read_finished(r, done > 0);
    }
}

// The frame's work is recorded: reads asked for begin, of the lists as it leaves them, and lists that are gone go.
static void compute_frame_end(void)
{
    tide_gpu_read_request *reads = tide_gpu_read_requests();
    for (uint32_t i = 0; i < TIDE_GPU_READS; i++) {
        tide_gpu_read_request *r = &reads[i];
        if (r->state != TIDE_GPU_READ_ASKED) continue;
        if (r->forgotten) {
            tide_gpu_read_finished(r, false);
            continue;
        }
        const gpu_list_buffer *b = gpu->buffer_read ? list_range(r->list, true) : NULL;
        if (!b) {
            tide_gpu_read_finished(r, false);
            continue;
        }
        // Its range, as far as what was asked for goes (the rest of what it's read into stays zeros)
        const uint64_t bytes = (uint64_t)b->range.words * 4u < r->bytes ? (uint64_t)b->range.words * 4u : r->bytes;
        r->token = gpu->buffer_read(pool_buffer(b->range.pool), (size_t)b->range.at * 4u, (size_t)bytes);
        if (r->token) r->state = TIDE_GPU_READ_BEGUN; // Or asked again next frame
    }
    uint32_t kept = 0;
    for (uint32_t i = 0; i < gpu_list_count; i++) {
        gpu_list_buffer *b = &gpu_list_buffers[i];
        // Not named since a hot reload: its state may be gone, so it isn't read
        const bool named = b->epoch == gpu_list_epoch;
        if (!named || !tide_gpu_list_live(b->id)) {
            pool_give(b->range);
            continue;
        }
        gpu_list_buffers[kept++] = *b;
    }
    gpu_list_count = kept;
}

static tide_gpu_id take_run_buffer(void)
{
    if (run_buffers_used == run_buffer_count) {
        if (run_buffer_count == run_buffer_capacity) {
            const uint32_t capacity = run_buffer_capacity ? run_buffer_capacity * 2u : 8u;
            run_buffers = tide_realloc(run_buffers, run_buffer_capacity * sizeof *run_buffers, capacity * sizeof *run_buffers);
            run_buffer_capacity = capacity;
        }
        run_buffers[run_buffer_count++] = made_buffer(TIDE_GPU_STORAGE, TIDE_GPU_RUN_WORDS * 4u, NULL);
    }
    return run_buffers[run_buffers_used++];
}

static tide_gpu_id take_scratch(uint64_t bytes)
{
    if (bytes < 16u) bytes = 16u;
    if (scratch_used == scratch_count) {
        if (scratch_count == scratch_capacity) {
            const uint32_t capacity = scratch_capacity ? scratch_capacity * 2u : 8u;
            scratch_buffers = tide_realloc(scratch_buffers, scratch_capacity * sizeof *scratch_buffers,
                                           capacity * sizeof *scratch_buffers);
            scratch_capacity = capacity;
        }
        scratch_buffers[scratch_count++] = (sized_buffer){0};
    }
    sized_buffer *b = &scratch_buffers[scratch_used++];
    if (b->bytes < bytes) {
        if (b->buffer) free_buffer(b->buffer);
        b->bytes = bytes;
        b->buffer = made_buffer(TIDE_GPU_STORAGE, (size_t)bytes, NULL);
    }
    return b->buffer;
}

// An Add's scratch of `words` in pool `pool`, taken in turn each frame: its first word, or UINT32_MAX where the pool
// can't hold it. Outside passes.
static uint32_t take_add_scratch(const uint32_t pool, const uint64_t words)
{
    if (add_scratch_used[pool] == add_scratch_count[pool]) {
        if (add_scratch_count[pool] == add_scratch_capacity[pool]) {
            const uint32_t capacity = add_scratch_capacity[pool] ? add_scratch_capacity[pool] * 2u : 8u;
            add_scratch[pool] = tide_realloc(add_scratch[pool], add_scratch_capacity[pool] * sizeof *add_scratch[pool],
                                             capacity * sizeof *add_scratch[pool]);
            add_scratch_capacity[pool] = capacity;
        }
        add_scratch[pool][add_scratch_count[pool]++] = (gpu_range){0};
    }
    gpu_range *r = &add_scratch[pool][add_scratch_used[pool]++];
    if (r->words < words) {
        pool_give(*r);
        bool dirty;
        *r = pool_range(pool, words, &dirty); // What it held isn't read: each group writes its own before it's read
    }
    return r->words ? r->at : UINT32_MAX;
}

static tide_gpu_id kernel_pipeline_for(const tide_shader *shader)
{
    for (uint32_t i = 0; i < kernel_pipeline_count; i++) {
        if (kernel_pipelines[i].id != shader->id) continue;
        kernel_pipelines[i].frame = gpu_frame;
        return kernel_pipelines[i].pipeline;
    }
    tide_gpu_pipeline_desc desc = {.name = shader->name ? shader->name : "a kernel", .wgsl = shader->wgsl,
                                   .spirv = shader->spirv, .spirv_words = shader->spirv_words, .compute = true,
                                   .uniform_size = tide_shader_uniform_size(shader) ? tide_shader_uniform_size(shader) : 16u,
                                   .texture_count = shader->texture_count, .storage_count = tide_shader_compute_storage(shader)};
    shader_pipeline_textures(&desc, shader);
    desc.storage_writes[TIDE_STORAGE_WORDS] = tide_shader_pool_written(shader, TIDE_STORAGE_WORDS);
    desc.storage_writes[TIDE_STORAGE_INTS] = tide_shader_pool_written(shader, TIDE_STORAGE_INTS);
    if (kernel_pipeline_count == kernel_pipeline_capacity) {
        const uint32_t capacity = kernel_pipeline_capacity ? kernel_pipeline_capacity * 2u : 8u;
        kernel_pipelines = tide_realloc(kernel_pipelines, kernel_pipeline_capacity * sizeof *kernel_pipelines,
                                        capacity * sizeof *kernel_pipelines);
        kernel_pipeline_capacity = capacity;
    }
    const tide_gpu_id pipeline = gpu->pipeline(&desc);
    kernel_pipelines[kernel_pipeline_count++] = (kernel_pipeline){shader->id, pipeline, gpu_frame};
    return pipeline;
}

// A build that took over keeps the kernels it runs (as forget_pipelines does for draws).
static void forget_kernel_pipelines(void)
{
    uint32_t kept = 0;
    for (uint32_t i = 0; i < kernel_pipeline_count; i++) {
        if (kernel_pipelines[i].frame != gpu_frame) {
            gpu->pipeline_free(kernel_pipelines[i].pipeline);
            continue;
        }
        kernel_pipelines[kept++] = kernel_pipelines[i];
    }
    kernel_pipeline_count = kept;
}

// The ids of the GPU lists a run or draw names, by slot: `storage`, 1 + where they start in the list's, or 0.
static const uint64_t *slot_ids(const tide_draw_list *list, const uint32_t storage)
{
    return storage ? list->storage + storage - 1u : NULL;
}

// A kernel's run, outside passes: its lists' ranges and its Adds' scratch first (where a pool may grow), its record,
// then in one compute pass its dispatch, with Add's after it.
static void run_compute(const tide_draw_list *list, const draw_step *s)
{
    const tide_draw_compute *r = &list->computes[s->first];
    const tide_shader *shader = r->shader;
    if (!shader || !shader->compute || shader->storage_count > TIDE_SHADER_STORAGE || shader->add_count > TIDE_SHADER_STORAGE
        || !engine_kernels() || !pools_ready()) return;
    const tide_gpu_id pipeline = kernel_pipeline_for(shader);
    if (!pipeline_usable(pipeline)) return;
    const uint32_t group = shader->group[0] * shader->group[1] * shader->group[2];
    if (!group) return;
    const uint64_t *ids = slot_ids(list, r->storage);
    for (uint32_t i = 0; ids && i < shader->storage_count; i++) list_range(ids[i], true);
    uint32_t groups[3] = {0, 1, 1};
    uint64_t most_groups; // What its Adds' scratch holds room for
    const gpu_list_buffer *over = NULL;
    if (r->over) { // Its size is the list's count, on the GPU then: its record is worked out there
        over = list_range(r->over, true);
        if (!over) return;
        most_groups = groups_of(over->capacity, shader->group[0]);
        if (most_groups > 65535u) most_groups = 65535u;
    } else {
        for (int a = 0; a < 3; a++) groups[a] = groups_of(r->size[a], shader->group[a]);
        if (groups[0] > 65535u || groups[1] > 65535u || groups[2] > 65535u) {
            fprintf(stderr, "tide: a run of %s over %u by %u by %u is more groups than a GPU dispatches along an axis (65535): "
                            "it's passed by\n", shader->name ? shader->name : "a kernel", r->size[0], r->size[1], r->size[2]);
            return;
        }
        if (!groups[0] || !groups[1] || !groups[2]) return;
        most_groups = (uint64_t)groups[0] * groups[1] * groups[2];
    }
    uint32_t scratch[TIDE_SHADER_STORAGE] = {0};
    for (uint32_t k = 0; k < shader->add_count; k++) {
        const uint64_t words = most_groups * tide_gpu_add_group_words(group, shader->adds[k].stride);
        if (words * 4u > TIDE_GPU_STORAGE_MOST_BYTES) { // Each group's room for what it adds, more than every GPU binds
            fprintf(stderr, "tide: a run of %s adds from more invocations than a GPU holds room for (%llu bytes of %u): "
                            "it's passed by\n", shader->name ? shader->name : "a kernel", (unsigned long long)words * 4u,
                    (unsigned)TIDE_GPU_STORAGE_MOST_BYTES);
            return;
        }
        const uint32_t pool = tide_shader_storage_ints(shader, shader->adds[k].list) ? TIDE_STORAGE_INTS : TIDE_STORAGE_WORDS;
        scratch[k] = take_add_scratch(pool, words);
        if (scratch[k] == UINT32_MAX) return; // Its pool said why
    }
    // Every range is made now: the pools are what they'll be for the run
    const tide_gpu_id record = take_run_buffer();
    if (over) {
        const uint32_t size[4] = {shader->group[0], over->range.at, 0, 0};
        gpu->compute_begin();
        gpu->use(run_size_pipeline);
        gpu->uniforms(size, sizeof size);
        gpu->storage(0, pool_buffer(over->range.pool));
        gpu->storage(1, record);
        gpu->dispatch(1, 1, 1);
        gpu->compute_end();
    } else {
        const uint64_t total = (uint64_t)r->size[0] * r->size[1] * r->size[2];
        const uint32_t words[TIDE_GPU_RUN_WORDS] = {groups[0], groups[1], groups[2], total > UINT32_MAX ? UINT32_MAX : (uint32_t)total,
                                                    r->size[0], r->size[1], r->size[2], 0};
        gpu->buffer_write(record, 0, words, sizeof words);
    }
    static uint8_t block[TIDE_GPU_MAX_UNIFORMS];
    const uint32_t own = tide_shader_uniform_size(shader);
    if (own > sizeof block) return;
    memset(block, 0, 16);
    if (own) memcpy(block, list->uniforms + r->uniforms, own);
    put_table(block, own, shader, ids, false, scratch);
    gpu->compute_begin();
    gpu->use(pipeline);
    gpu->uniforms(block, own ? own : 16u);
    bind_shader_textures(list, shader, r->textures, r->sampling);
    bind_pools();
    gpu->storage(TIDE_STORAGE_RUN, record);
    if (over) gpu->dispatch_indirect(record, 0);
    else gpu->dispatch(groups[0], groups[1], groups[2]);
    // Each Add's survivors, in invocation order: where each group's go, then them
    for (uint32_t k = 0; k < shader->add_count; k++) {
        const uint32_t to = shader->adds[k].list < shader->storage_count ? shader->adds[k].list : 0u;
        const uint32_t pool = tide_shader_storage_ints(shader, to) ? TIDE_STORAGE_INTS : TIDE_STORAGE_WORDS;
        const gpu_list_buffer *b = list_range(ids ? ids[to] : 0u, false);
        const uint32_t at = b && b->range.pool == pool ? b->range.at : 0u; // Or the empty list, whose count stays 0
        const uint32_t region = (uint32_t)tide_gpu_add_group_words(group, shader->adds[k].stride);
        const uint32_t offsets[4] = {region, scratch[k], at, 0};
        gpu->use(add_offsets_pipeline);
        gpu->uniforms(offsets, sizeof offsets);
        gpu->storage(0, pool_buffer(pool));
        gpu->storage(1, record);
        gpu->dispatch(1, 1, 1);
        const uint32_t scatter[4] = {region, shader->adds[k].stride, scratch[k], at};
        gpu->use(add_scatter_pipeline);
        gpu->uniforms(scatter, sizeof scatter);
        gpu->storage(0, pool_buffer(pool));
        if (over) gpu->dispatch_indirect(record, 0);
        else gpu->dispatch(groups[0], groups[1], groups[2]);
    }
    gpu->compute_end();
}

// A GPU list's Set, Clear or SetCount, outside passes, in its place among the frame's work.
static void run_gpu_op(const tide_draw_list *list, const draw_step *s)
{
    const tide_draw_gpu_op *op = &list->gpu_ops[s->first];
    const gpu_list_buffer *b = list_range(op->list, true);
    const tide_gpu_list_state *state = tide_gpu_list_state_of(op->list);
    if (!b || !state) return;
    const tide_gpu_id buffer = pool_buffer(b->range.pool);
    const size_t at = (size_t)b->range.at * 4u;
    const uint32_t count = op->count < b->capacity ? op->count : b->capacity;
    if (op->kind == TIDE_GPU_LIST_SET && count) {
        const uint64_t words = (uint64_t)count * state->stride;
        if (op->words + words <= list->gpu_word_count && TIDE_GPU_LIST_HEADER_WORDS + words <= b->range.words)
            gpu->buffer_write(buffer, at + TIDE_GPU_LIST_HEADER_WORDS * 4u, list->gpu_words + op->words, (size_t)(words * 4u));
    }
    const uint32_t header = op->kind == TIDE_GPU_LIST_CLEAR ? 0u : count;
    gpu->buffer_write(buffer, at, &header, sizeof header);
}

// Whether a step reads the GPU list `id`: a draw's lists, its instances', vertices' and indices' among them.
static bool step_reads_list(const tide_draw_list *list, const draw_step *s, const uint64_t id)
{
    if (s->kind != STEP_SHADED && s->kind != STEP_BLIT) return false;
    const tide_draw_run *run = &list->runs[s->first];
    if (run->indirect == id) return true;
    if (!run->shader || !run->storage) return false;
    for (uint32_t k = 0; k < run->shader->storage_count && k < TIDE_SHADER_STORAGE; k++) {
        if (list->storage[run->storage - 1u + k] == id) return true;
    }
    return false;
}

// Whether a run samples a render target's image, which a draw into a target may be changing.
static bool run_samples_target(const tide_draw_list *list, const tide_draw_compute *r)
{
    for (uint32_t i = 0; i < r->shader->texture_count && i < TIDE_SHADER_TEXTURES; i++) {
        const uint32_t t = r->textures[i];
        if (!t || t > list->texture_count) continue;
        const uint32_t space = list->textures[t - 1u].space;
        if (space == TIDE_PIXELS_TARGET || space == TIDE_PIXELS_TARGET_DEPTH || space == TIDE_PIXELS_TARGET_CUBE) return true;
    }
    return false;
}

// Whether work `w` (a run or a list's Set, Clear or SetCount) can be done before step `s` with the same results:
// `s` is no work (work keeps its order), reads no list `w` writes, and draws into no target where `w` samples one.
static bool moves_before(const tide_draw_list *list, const draw_step *w, const draw_step *s)
{
    if (s->kind == STEP_COMPUTE || s->kind == STEP_GPU_OP) return false;
    if (w->kind == STEP_GPU_OP) return !step_reads_list(list, s, list->gpu_ops[w->first].list);
    const tide_draw_compute *r = &list->computes[w->first];
    const tide_shader *shader = r->shader;
    if (!shader || !shader->compute) return true; // It runs nothing
    if (s->target && run_samples_target(list, r)) return false;
    for (uint32_t k = 0; k < shader->storage_count && k < TIDE_SHADER_STORAGE; k++) {
        bool writes = (shader->storage_writes >> k & 1u) != 0;
        for (uint32_t a = 0; a < shader->add_count && a < TIDE_SHADER_STORAGE; a++) writes |= shader->adds[a].list == k;
        const uint64_t id = r->storage ? list->storage[r->storage - 1u + k] : 0u;
        if (writes && id && step_reads_list(list, s, id)) return false;
    }
    return true;
}

// Runs and list operations moved before the draws recorded ahead of them that don't depend on them, in their order:
// out of the render pass they fell in, which they'd otherwise end, so it isn't split. What's drawn is as if done in
// recording order: a draw before a run still reads none of what the run writes.
static void move_work_out_of_passes(const tide_draw_list *list)
{
    for (uint32_t i = 1; i < step_count; i++) {
        if (steps[i].kind != STEP_COMPUTE && steps[i].kind != STEP_GPU_OP) continue;
        uint32_t to = i;
        while (to > 0 && moves_before(list, &steps[i], &steps[to - 1u])) to--;
        if (to == i) continue;
        const draw_step work = steps[i];
        memmove(&steps[to + 1u], &steps[to], (size_t)(i - to) * sizeof *steps);
        steps[to] = work;
    }
}

// Whether a step draws indirect, with counts the GPU has: instances a GPU list's, or indices.
static bool step_indirect(const tide_draw_list *list, const draw_step *s)
{
    if (s->kind != STEP_SHADED) return false;
    const tide_draw_run *run = &list->runs[s->first];
    return run->indirect || (run->shader && run->shader->index_storage);
}

// The GPU lists the frame's draws read, made before any pass (a draw can't make one in a pass), and each indirect
// draw's arguments from `from` to the next of the frame's runs and list operations, which change counts: its index
// count, and its instances, the list's count as the GPU has it there. A draw whose indices are a GPU list's draws
// as many corners as that list's count, each reading its vertex's index there, `draw_indirect`'s numbers.
static void prepare_storage_draws(const tide_draw_list *list, const uint32_t from)
{
    for (uint32_t i = from; i < step_count; i++) {
        draw_step *s = &steps[i];
        if (s->kind == STEP_COMPUTE || s->kind == STEP_GPU_OP) return;
        if (s->kind != STEP_SHADED || !run_draws(list, s->first)) continue;
        const tide_draw_run *run = &list->runs[s->first];
        const tide_shader *shader = run->shader;
        if (!shader->storage_count) continue;
        for (uint32_t k = 0; k < shader->storage_count && run->storage; k++) list_range(list->storage[run->storage - 1u + k], true);
        if (!run->indirect && !shader->index_storage) continue;
        const gpu_list_buffer *instances = run->indirect ? list_range(run->indirect, true) : NULL;
        if (run->indirect && !instances) continue;
        if (shader->index_storage) {
            const uint32_t slot = shader->index_storage - 1u;
            const gpu_list_buffer *indices = slot < shader->storage_count && run->storage
                                           ? list_range(list->storage[run->storage - 1u + slot], true) : NULL;
            if (!indices) continue;
            s->indirect = take_scratch(4u * sizeof(uint32_t));
            const uint32_t args[4] = {0, run->indirect ? 0u : run->count, 0, 0};
            gpu->buffer_write(s->indirect, 0, args, sizeof args);
            gpu->buffer_copy(pool_buffer(indices->range.pool), (size_t)indices->range.at * 4u, s->indirect, 0, sizeof(uint32_t));
        } else {
            const gpu_mesh *g = &gpu_meshes[run->mesh - 1u];
            if (!g->index_count) continue;
            s->indirect = take_scratch(5u * sizeof(uint32_t));
            const uint32_t args[5] = {g->index_count, 0, 0, 0, 0};
            gpu->buffer_write(s->indirect, 0, args, sizeof args);
        }
        if (instances) {
            gpu->buffer_copy(pool_buffer(instances->range.pool), (size_t)instances->range.at * 4u, s->indirect,
                             sizeof(uint32_t), sizeof(uint32_t));
        }
    }
}

static void prepare_storage(const tide_draw_list *list)
{
    bool any = false;
    for (uint32_t i = 0; i < step_count; i++) {
        steps[i].indirect = 0;
        any |= steps[i].kind == STEP_SHADED && list->runs[steps[i].first].shader
            && list->runs[steps[i].first].shader->storage_count;
    }
    if (any) pools_ready(); // What draws bind, made outside passes
    prepare_storage_draws(list, 0);
}

