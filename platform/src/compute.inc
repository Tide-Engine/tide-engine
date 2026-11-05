// Compute in the renderer (notes/compute.md): GPU lists' buffers by their
// ids, kernels' runs and the engine's own kernels around them (shaders.h: a
// run over a GPU list's count, and ordered Add's offsets and scatter), list
// operations in frame order, indirect draws, and reads back. Included by
// platform.c, which turns the draw list's TIDE_DRAW_COMPUTE and
// TIDE_DRAW_GPU_LIST commands into STEP_COMPUTE and STEP_GPU_OP steps, done
// outside passes in their order.

// A GPU list's buffer, by its id. A hot reload (another epoch of the draw
// list) leaves records whose states may be gone: they're kept only for a list
// the new build's first frame names, and nothing reads their states before.
typedef struct gpu_list_buffer {
    uint64_t id;
    uint64_t version;
    uint32_t epoch; // The draw list's epoch it was last named in
    tide_gpu_id buffer;
} gpu_list_buffer;

static gpu_list_buffer *gpu_list_buffers;
static uint32_t gpu_list_count, gpu_list_capacity;
static uint32_t gpu_list_epoch;
static tide_gpu_id gpu_empty_list; // What a run or a draw reads for a list that isn't there: none, of no capacity
static bool compute_said;          // That this backend has no compute, said once

static tide_gpu_id *run_buffers; // Each run's record, a buffer of TIDE_GPU_RUN_WORDS words, the frame's k-th run's k-th
static uint32_t run_buffer_count, run_buffer_capacity, run_buffers_used;
typedef struct sized_buffer {
    tide_gpu_id buffer;
    uint64_t bytes;
} sized_buffer;
static sized_buffer *scratch_buffers; // Adds' scratch, and indirect draws' arguments, taken in turn each frame
static uint32_t scratch_count, scratch_capacity, scratch_used;

typedef struct kernel_pipeline {
    uint64_t id;
    tide_gpu_id pipeline;
    uint32_t frame;
} kernel_pipeline;
static kernel_pipeline *kernel_pipelines;
static uint32_t kernel_pipeline_count, kernel_pipeline_capacity;
static tide_gpu_id run_size_pipeline, add_offsets_pipeline, add_scatter_pipeline;

static bool can_compute(void)
{
    if (gpu->dispatch) return true;
    if (!compute_said) {
        fprintf(stderr, "tide: this game runs kernels (GPU.Run) or reads GPU lists, which %s can't: they're passed by. "
                        "Compute needs Vulkan 1.3 or WebGPU; OpenGL and WebGL go once compute is everywhere\n", gpu->name);
        compute_said = true;
    }
    return false;
}

// Everything on the device went with it: lists come back empty (tide_gpu_list_made says so to the game), and reads
// begun there fail.
static void forget_compute(void)
{
    gpu_list_count = 0;
    gpu_empty_list = 0;
    run_buffer_count = run_buffers_used = 0;
    scratch_count = scratch_used = 0;
    kernel_pipeline_count = 0;
    run_size_pipeline = add_offsets_pipeline = add_scatter_pipeline = 0;
    tide_gpu_read_request *reads = tide_gpu_read_requests();
    for (uint32_t i = 0; i < TIDE_GPU_READS; i++) {
        if (reads[i].state == TIDE_GPU_READ_BEGUN) tide_gpu_read_finished(&reads[i], false);
    }
}

static tide_gpu_id empty_list(void)
{
    if (!gpu_empty_list) {
        static const uint32_t header[TIDE_GPU_LIST_HEADER_WORDS] = {0};
        gpu_empty_list = made_buffer(TIDE_GPU_STORAGE, sizeof header, header);
    }
    return gpu_empty_list;
}

// The buffer of the GPU list `id`, made where it has none yet or its version changed (empty, its header written),
// or 0 for one that isn't there. Outside passes.
static tide_gpu_id list_buffer(const uint64_t id)
{
    if (!id) return 0;
    gpu_list_buffer *b = NULL;
    for (uint32_t i = 0; i < gpu_list_count && !b; i++) {
        if (gpu_list_buffers[i].id == id) b = &gpu_list_buffers[i];
    }
    if (b) b->epoch = gpu_list_epoch; // Named in this build's list: its state is this build's to read
    const tide_gpu_list_state *s = tide_gpu_list_state_of(id);
    if (!s || !s->alive) return 0;
    if (b && b->buffer && b->version == s->version) return b->buffer;
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
    if (b->buffer) free_buffer(b->buffer);
    const uint64_t words = TIDE_GPU_LIST_HEADER_WORDS + (uint64_t)s->capacity * s->stride;
    b->buffer = made_buffer(TIDE_GPU_STORAGE, (size_t)(words * 4u), NULL);
    const uint32_t header[TIDE_GPU_LIST_HEADER_WORDS] = {0, s->capacity, 0, 0};
    gpu->buffer_write(b->buffer, 0, header, sizeof header);
    b->version = s->version;
    tide_gpu_list_made(id);
    return b->buffer;
}

// A frame begins: after a hot reload every record waits to be named again; reads asked for long ago go free.
static void compute_frame_start(const tide_draw_list *list)
{
    run_buffers_used = scratch_used = 0;
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
        const tide_gpu_id buffer = gpu->buffer_read ? list_buffer(r->list) : 0;
        if (!buffer) {
            tide_gpu_read_finished(r, false);
            continue;
        }
        r->token = gpu->buffer_read(buffer, 0, r->bytes);
        if (r->token) r->state = TIDE_GPU_READ_BEGUN; // Or asked again next frame
    }
    uint32_t kept = 0;
    for (uint32_t i = 0; i < gpu_list_count; i++) {
        gpu_list_buffer *b = &gpu_list_buffers[i];
        // Not named since a hot reload: its state may be gone, so it isn't read
        const bool named = b->epoch == gpu_list_epoch;
        if (!named || !tide_gpu_list_live(b->id)) {
            if (b->buffer) free_buffer(b->buffer);
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

static tide_gpu_id compute_pipeline(const char *name, const char *wgsl, const uint32_t *spirv, const uint32_t words,
                                    const uint32_t storage, const uint32_t writes)
{
    tide_gpu_pipeline_desc desc = {.name = name, .wgsl = wgsl, .spirv = spirv, .spirv_words = words, .uniform_size = 16,
                                   .compute = true, .storage_count = storage};
    for (uint32_t i = 0; i < storage; i++) desc.storage_writes[i] = (writes >> i & 1u) != 0;
    return gpu->pipeline(&desc);
}

static tide_gpu_id kernel_pipeline_for(const tide_shader *shader)
{
    for (uint32_t i = 0; i < kernel_pipeline_count; i++) {
        if (kernel_pipelines[i].id != shader->id) continue;
        kernel_pipelines[i].frame = gpu_frame;
        return kernel_pipelines[i].pipeline;
    }
    const uint32_t storage = tide_shader_compute_storage(shader);
    tide_gpu_pipeline_desc desc = {.name = shader->name ? shader->name : "a kernel", .wgsl = shader->wgsl,
                                   .spirv = shader->spirv, .spirv_words = shader->spirv_words, .compute = true,
                                   .uniform_size = tide_shader_uniform_size(shader) ? tide_shader_uniform_size(shader) : 16u,
                                   .texture_count = shader->texture_count, .storage_count = storage};
    for (uint32_t i = 0; i < shader->texture_count && i < TIDE_GPU_MAX_TEXTURES; i++) desc.reads[i] = gpu_read(shader, i);
    for (uint32_t i = 0; i < storage && i < TIDE_GPU_MAX_STORAGE; i++)
        desc.storage_writes[i] = i >= shader->storage_count ? i < storage - 1u : (shader->storage_writes >> i & 1u) != 0;
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

static bool pipeline_usable(const tide_gpu_id pipeline)
{
    return pipeline && (!gpu->pipeline_ready || gpu->pipeline_ready(pipeline));
}

static bool engine_kernels(void)
{
    if (!run_size_pipeline) {
        run_size_pipeline = compute_pipeline("runs over a GPU list's count", run_size_wgsl, SPIRV_OF(run_size),
                                             SPIRV_WORDS(run_size), 2, 2u);
        add_offsets_pipeline = compute_pipeline("ordered Adds' offsets", add_offsets_wgsl, SPIRV_OF(add_offsets),
                                                SPIRV_WORDS(add_offsets), 3, 3u);
        add_scatter_pipeline = compute_pipeline("ordered Adds' elements", add_scatter_wgsl, SPIRV_OF(add_scatter),
                                                SPIRV_WORDS(add_scatter), 2, 2u);
    }
    return pipeline_usable(run_size_pipeline) && pipeline_usable(add_offsets_pipeline) && pipeline_usable(add_scatter_pipeline);
}

static uint32_t groups_of(const uint32_t size, const uint32_t group)
{
    return (uint32_t)(((uint64_t)size + group - 1u) / group);
}

// A kernel's run, outside passes: its record, its scratch, and its dispatches, with Add's after it.
static void run_compute(const tide_draw_list *list, const draw_step *s)
{
    if (!can_compute()) return;
    const tide_draw_compute *r = &list->computes[s->first];
    const tide_shader *shader = r->shader;
    if (!shader || !shader->compute || shader->add_count > TIDE_SHADER_STORAGE
        || tide_shader_compute_storage(shader) > TIDE_GPU_MAX_STORAGE || !engine_kernels()) return;
    const tide_gpu_id pipeline = kernel_pipeline_for(shader);
    if (!pipeline_usable(pipeline)) return;
    const uint32_t group = shader->group[0] * shader->group[1] * shader->group[2];
    if (!group) return;
    tide_gpu_id lists[TIDE_SHADER_STORAGE] = {0};
    for (uint32_t i = 0; i < shader->storage_count && i < TIDE_SHADER_STORAGE; i++) {
        const uint64_t id = r->storage ? list->storage[r->storage - 1u + i] : 0u;
        lists[i] = list_buffer(id);
        if (!lists[i]) lists[i] = empty_list();
    }
    const tide_gpu_id record = take_run_buffer();
    uint32_t groups[3] = {0, 1, 1};
    uint64_t most_groups; // What its Adds' scratch holds room for
    tide_gpu_id over = 0;
    if (r->over) { // Its size is the list's count, on the GPU then: its record is worked out there
        over = list_buffer(r->over);
        const tide_gpu_list_state *of = tide_gpu_list_state_of(r->over);
        if (!over || !of) return;
        most_groups = groups_of(of->capacity, shader->group[0]);
        if (most_groups > 65535u) most_groups = 65535u;
        const uint32_t size[4] = {shader->group[0], 0, 0, 0};
        gpu->compute_begin();
        gpu->use(run_size_pipeline);
        gpu->uniforms(size, sizeof size);
        gpu->storage(0, over);
        gpu->storage(1, record);
        gpu->dispatch(1, 1, 1);
        gpu->compute_end();
    } else {
        for (int a = 0; a < 3; a++) groups[a] = groups_of(r->size[a], shader->group[a]);
        if (groups[0] > 65535u || groups[1] > 65535u || groups[2] > 65535u) {
            fprintf(stderr, "tide: a run of %s over %u by %u by %u is more groups than a GPU dispatches along an axis (65535): "
                            "it's passed by\n", shader->name ? shader->name : "a kernel", r->size[0], r->size[1], r->size[2]);
            return;
        }
        if (!groups[0] || !groups[1] || !groups[2]) return;
        const uint64_t total = (uint64_t)r->size[0] * r->size[1] * r->size[2];
        const uint32_t words[TIDE_GPU_RUN_WORDS] = {groups[0], groups[1], groups[2], total > UINT32_MAX ? UINT32_MAX : (uint32_t)total,
                                                    r->size[0], r->size[1], r->size[2], 0};
        gpu->buffer_write(record, 0, words, sizeof words);
        most_groups = (uint64_t)groups[0] * groups[1] * groups[2];
    }
    tide_gpu_id scratch[TIDE_SHADER_STORAGE] = {0};
    for (uint32_t k = 0; k < shader->add_count; k++)
        scratch[k] = take_scratch(most_groups * tide_gpu_add_group_words(group, shader->adds[k].stride) * 4u);
    static uint8_t zero[16];
    gpu->compute_begin();
    gpu->use(pipeline);
    const uint32_t own = tide_shader_uniform_size(shader);
    gpu->uniforms(own ? (const void *)(list->uniforms + r->uniforms) : zero, own ? own : sizeof zero);
    for (uint32_t i = 0; i < shader->texture_count && i < TIDE_SHADER_TEXTURES; i++) {
        gpu->sample(i, texture_to_read(list, r->textures[i], gpu_read(shader, i)),
                    gpu_filter(tide_sampling_filter(r->sampling, i)), gpu_wrap(tide_sampling_wrap(r->sampling, i)));
    }
    for (uint32_t i = 0; i < shader->storage_count; i++) gpu->storage(i, lists[i]);
    for (uint32_t k = 0; k < shader->add_count; k++) gpu->storage(shader->storage_count + k, scratch[k]);
    gpu->storage(shader->storage_count + shader->add_count, record);
    if (over) gpu->dispatch_indirect(record, 0);
    else gpu->dispatch(groups[0], groups[1], groups[2]);
    // Each Add's survivors, in invocation order: where each group's go, then them
    for (uint32_t k = 0; k < shader->add_count; k++) {
        const uint32_t to = shader->adds[k].list < TIDE_SHADER_STORAGE ? shader->adds[k].list : 0u;
        const uint32_t region = (uint32_t)tide_gpu_add_group_words(group, shader->adds[k].stride);
        const uint32_t offsets[4] = {region, 0, 0, 0};
        gpu->use(add_offsets_pipeline);
        gpu->uniforms(offsets, sizeof offsets);
        gpu->storage(0, scratch[k]);
        gpu->storage(1, lists[to]);
        gpu->storage(2, record);
        gpu->dispatch(1, 1, 1);
        const uint32_t scatter[4] = {region, shader->adds[k].stride, 0, 0};
        gpu->use(add_scatter_pipeline);
        gpu->uniforms(scatter, sizeof scatter);
        gpu->storage(0, scratch[k]);
        gpu->storage(1, lists[to]);
        if (over) gpu->dispatch_indirect(record, 0);
        else gpu->dispatch(groups[0], groups[1], groups[2]);
    }
    gpu->compute_end();
}

// A GPU list's Set, Clear or SetCount, outside passes, in its place among the frame's work.
static void run_gpu_op(const tide_draw_list *list, const draw_step *s)
{
    const tide_draw_gpu_op *op = &list->gpu_ops[s->first];
    if (!gpu->storage) return; // A backend with no storage keeps no lists
    const tide_gpu_id buffer = list_buffer(op->list);
    const tide_gpu_list_state *state = tide_gpu_list_state_of(op->list);
    if (!buffer || !state) return;
    const uint32_t count = op->count < state->capacity ? op->count : state->capacity;
    if (op->kind == TIDE_GPU_LIST_SET && count) {
        const uint64_t words = (uint64_t)count * state->stride;
        if (op->words + words <= list->gpu_word_count)
            gpu->buffer_write(buffer, TIDE_GPU_LIST_HEADER_WORDS * 4u, list->gpu_words + op->words, (size_t)(words * 4u));
    }
    const uint32_t header = op->kind == TIDE_GPU_LIST_CLEAR ? 0u : count;
    gpu->buffer_write(buffer, 0, &header, sizeof header);
}

// The GPU lists the frame's draws read, made before any pass (a draw can't make one in a pass), and each indirect
// draw's arguments from `from` to the next of the frame's runs and list operations, which change counts: its index
// count, and its instances, the list's count as the GPU has it there.
static void prepare_storage_draws(const tide_draw_list *list, const uint32_t from)
{
    for (uint32_t i = from; i < step_count; i++) {
        draw_step *s = &steps[i];
        if (s->kind == STEP_COMPUTE || s->kind == STEP_GPU_OP) return;
        if (s->kind != STEP_SHADED || !run_draws(list, s->first)) continue;
        const tide_draw_run *run = &list->runs[s->first];
        if (!run->shader->storage_count) continue;
        if (!can_compute()) continue;
        for (uint32_t k = 0; k < run->shader->storage_count && run->storage; k++) list_buffer(list->storage[run->storage - 1u + k]);
        if (!run->indirect) continue;
        const tide_gpu_id instances = list_buffer(run->indirect);
        const gpu_mesh *g = &gpu_meshes[run->mesh - 1u];
        if (!instances || !g->index_count) continue;
        s->indirect = take_scratch(5u * sizeof(uint32_t));
        const uint32_t args[5] = {g->index_count, 0, 0, 0, 0};
        gpu->buffer_write(s->indirect, 0, args, sizeof args);
        gpu->buffer_copy(instances, 0, s->indirect, sizeof(uint32_t), sizeof(uint32_t));
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
    if (any && gpu->storage) empty_list(); // What a draw reads for a list that isn't there, made outside passes
    prepare_storage_draws(list, 0);
}

// A draw's GPU lists, bound for it.
static void bind_storage(const tide_draw_list *list, const tide_draw_run *run)
{
    for (uint32_t k = 0; k < run->shader->storage_count && k < TIDE_SHADER_STORAGE; k++) {
        const uint64_t id = run->storage ? list->storage[run->storage - 1u + k] : 0u;
        tide_gpu_id buffer = list_buffer(id); // Made already (prepare_storage_draws)
        gpu->storage(k, buffer ? buffer : empty_list());
    }
}
