// Kernels' shaders: their WGSL, and planning one for each kernel a GPU.Run names (notes/compute.md). Included
// by shader.c, after the planning of draws' shaders.

// ---------------------------------------------------------------------------
// Kernels' WGSL

// The game's uniforms of a kernel, alone: no engine header comes before them, and a struct of nothing is no WGSL.
static void put_kernel_uniforms(const shader *sh, sb *o)
{
    sb_put(o, "struct tide_uniforms {\n");
    for (int i = 0; i < packs(sh->uniform_floats); i++) sb_printf(o, "    tide_u%d: vec4f,\n", i);
    for (int i = 0; i < packs(sh->uniform_ints); i++) sb_printf(o, "    tide_v%d: vec4i,\n", i);
    if (!sh->uniform_floats && !sh->uniform_ints) sb_put(o, "    tide_none: vec4i,\n");
    sb_put(o, "}\n@group(0) @binding(0) var<uniform> tide_block: tide_uniforms;\n");
}

// What a kernel's code reads, into its shader: as a frag's, and its GPU lists as storage.
static int kernel_group(const decl *k)
{
    return k->group[0] * k->group[1] * k->group[2];
}

// A shared array that atomics are used on, as WGSL declares it: a struct of its elements then a word of no use,
// `struct tide_atomics_<name> { tide_cells: array<atomic<i32>, n>, tide_plain: u32 }`, with at least 2 elements and
// never as many as the group's invocations (the elements past the Tide array's length are never touched). So it
// compiles on Direct3D 11: Dawn's HLSL there zeroes workgroup memory as a group starts, an atomic by an
// InterlockedExchange, and FXC refuses (X4000, "used without having been completely initialized") any groupshared
// variable that no plain store has written before its first Interlocked call. The plain word is that store: Dawn
// zeroes it in the first invocation alone, before the array, whose elements are zeroed one an invocation after it as
// long as there are 2 or more and not one an invocation of the whole group (FXC folds that `if` away and refuses it
// again), and it has to come after the array, as FXC tracks the struct. Measured with Chrome 156's Dawn on its D3D11
// adapter and fxc 10.0.22621: notes/shaders.md, Kernels on Dawn's Direct3D 11.
static int shared_atomic_length(const decl *k, const stmt *s)
{
    int n = s->type.decl->array_length < 2 ? 2 : s->type.decl->array_length;
    if (n == kernel_group(k)) n++;
    return n;
}

// The scans of a kernel's Adds' flags, a lane a list, four lists a scan at the most: scan `b`'s lanes, of `adds`.
static int scan_lanes(const int adds, const int b)
{
    return adds - 4 * b < 4 ? adds - 4 * b : 4;
}

// ...its type, an element of its array (a vector of 4 for 3 lanes, the last of no use)...
static const char *scan_type(const int lanes)
{
    return lanes == 1 ? "u32" : lanes == 2 ? "vec2u" : "vec4u";
}

// ...and the bytes a group's scans share, `group` invocations each.
static int scan_bytes(const int adds, const int group)
{
    int bytes = 0;
    for (int b = 0; 4 * b < adds; b++) bytes += group * 4 * (scan_lanes(adds, b) == 1 ? 1 : scan_lanes(adds, b) == 2 ? 2 : 4);
    return bytes;
}

// The name of scan `b`'s array: tide_scan for the first, as for a kernel of one scan.
static void put_scan(sb *o, const int b)
{
    if (b) sb_printf(o, "tide_scan%d", b);
    else sb_put(o, "tide_scan");
}

static void write_kernel(program *prog, shader *sh)
{
    emit em = emitter(prog, sh);
    write_stage(&em, sh->kernel, STAGE_VERT);
    const decl *k = sh->kernel;
    const int group = kernel_group(k);
    sb o = {0};
    put_kernel_uniforms(sh, &o);
    put_textures(&o, sh); // As put_uniforms writes a frag's
    int adds = 0;
    for (int i = 0; i < sh->storage.count; i++) adds += sh->storage.items[i].add;
    put_pools(sh, &o);
    sb_put(&o, "var<private> tide_inv_index: i32;\nvar<private> tide_inv_at: vec3i;\nvar<private> tide_inv_local: vec3i;\n"
               "var<private> tide_inv_group: vec3i;\nvar<private> tide_inv_count: vec3i;\n");
    put_dice_variable(&em, &o, k->rolls);
    for (int i = 0, a = 0; i < sh->storage.count; i++) {
        if (!sh->storage.items[i].add) continue;
        sb_printf(&o, "var<private> tide_adding%d: bool;\nvar<private> tide_added%d: %s;\n", a, a, ty(&em, slot_element(&em, i)));
        a++;
    }
    for (int b = 0; 4 * b < adds; b++) {
        sb_put(&o, "var<workgroup> ");
        put_scan(&o, b);
        sb_printf(&o, ": array<%s, %d>;\n", scan_type(scan_lanes(adds, b)), group);
    }
    if (em.guards_shared) sb_put(&o, "var<workgroup> tide_zero: u32;\n"); // Never written: its shared writes are under it
    for (int i = 0; k->body && i < k->body->stmts.count; i++) { // Its shared locals, the group's
        const stmt *s = k->body->stmts.items[i];
        if (s->kind != S_VAR || !s->is_shared) continue;
        if (s->shared_atomic) {
            sb_printf(&o, "struct tide_atomics_%s {\n    tide_cells: array<atomic<i32>, %d>,\n    tide_plain: u32,\n}\n",
                      local_name(s->name), shared_atomic_length(k, s));
            sb_printf(&o, "var<workgroup> %s: tide_atomics_%s;\n", local_name(s->name), local_name(s->name));
        }
        else sb_printf(&o, "var<workgroup> %s: %s;\n", local_name(s->name), ty(&em, s->type));
    }
    if (k->rolls) need(&em, H_HASH4); // What each invocation's generator is made of, below
    sb rest = {0};
    put_structs(&em, &rest);
    put_helpers(&em, &rest);
    sb_put(&o, rest.data ? rest.data : "");
    if (em.functions.data) sb_put(&o, em.functions.data);

    sb_printf(&o, "@compute @workgroup_size(%d, %d, %d) fn compute(@builtin(global_invocation_id) tide_gid: vec3u, "
                  "@builtin(local_invocation_id) tide_lid: vec3u, @builtin(local_invocation_index) tide_lix: u32, "
                  "@builtin(workgroup_id) tide_wid: vec3u, @builtin(num_workgroups) tide_groups: vec3u) {\n",
              k->group[0], k->group[1], k->group[2]);
    sb_put(&o, "    let tide_size = vec3u(tide_run[4], tide_run[5], tide_run[6]);\n"
               "    tide_inv_at = vec3i(tide_gid);\n    tide_inv_local = vec3i(tide_lid);\n    tide_inv_group = vec3i(tide_wid);\n"
               "    tide_inv_count = vec3i(tide_size);\n"
               "    tide_inv_index = i32(tide_gid.x + tide_size.x * (tide_gid.y + tide_size.y * tide_gid.z));\n");
    if (k->rolls) { // Each invocation's generator, of its place and the frame's roll
        em.o = &o;
        sb_put(&o, "    tide_dice = u32(tide_hash4(vec4i(tide_inv_at, ");
        put_word(&em, 'v', sh->rolled_at);
        sb_printf(&o, " ^ %u)));\n", roll_site(k));
    }
    // A barrier needs every invocation of a group: the kernel checks where it is itself then
    if (k->barrier) sb_printf(&o, "    %s();\n", function_name(k));
    else sb_printf(&o, "    if (all(tide_gid < tide_size)) {\n        %s();\n    }\n", function_name(k));
    // Ordered Adds: each group's survivors in its order, by a scan of their flags, to its region of the scratch. Four
    // lists a scan, a lane each (scan_type), all in one loop, so its barriers are once whatever the count
    if (adds) {
        for (int b = 0; 4 * b < adds; b++) {
            const int lanes = scan_lanes(adds, b);
            sb_put(&o, "    ");
            put_scan(&o, b);
            sb_put(&o, "[tide_lix] = ");
            if (lanes > 1) sb_printf(&o, "%s(", scan_type(lanes));
            for (int a = 4 * b; a < 4 * b + lanes; a++) sb_printf(&o, "%sselect(0u, 1u, tide_adding%d)", a > 4 * b ? ", " : "", a);
            if (lanes == 3) sb_put(&o, ", 0u");
            sb_printf(&o, "%s;\n", lanes > 1 ? ")" : "");
        }
        sb_printf(&o, "    workgroupBarrier();\n    for (var tide_d = 1u; tide_d < %du; tide_d = tide_d * 2u) {\n", group);
        for (int b = 0; 4 * b < adds; b++) {
            const int lanes = scan_lanes(adds, b);
            sb_printf(&o, "        var tide_v%.0d = %s%s0u%s;\n", b, lanes > 1 ? scan_type(lanes) : "", lanes > 1 ? "(" : "",
                      lanes > 1 ? ")" : "");
        }
        sb_put(&o, "        if (tide_lix >= tide_d) {\n");
        for (int b = 0; 4 * b < adds; b++) {
            sb_printf(&o, "            tide_v%.0d = ", b);
            put_scan(&o, b);
            sb_put(&o, "[tide_lix - tide_d];\n");
        }
        sb_put(&o, "        }\n        workgroupBarrier();\n");
        for (int b = 0; 4 * b < adds; b++) {
            sb_put(&o, "        ");
            put_scan(&o, b);
            sb_put(&o, "[tide_lix] = ");
            put_scan(&o, b);
            sb_printf(&o, "[tide_lix] + tide_v%.0d;\n", b);
        }
        sb_put(&o, "        workgroupBarrier();\n    }\n");
    }
    for (int i = 0, a = 0; i < sh->storage.count; i++) {
        if (!sh->storage.items[i].add) continue;
        const int stride = element_words(slot_element(&em, i), NULL);
        const int lanes = scan_lanes(adds, a / 4);
        const char *lane = lanes == 1 ? "" : (const char *[]){".x", ".y", ".z", ".w"}[a % 4];
        sb scan = {0};
        put_scan(&scan, a / 4);
        // Its scratch is in its list's pool, where the table's word after the lists' says
        const char *pool = slot_pool(&em, i);
        const bool atomic = slot_atomic(&em, i);
        sb_printf(&o, "    let tide_region%d = ", a);
        put_table_word(&em, &o, sh->storage_at + 2 * sh->storage.count + a);
        sb_printf(&o, " + (tide_wid.x + tide_groups.x * (tide_wid.y + tide_groups.y * tide_wid.z)) * %uu;\n",
                  (unsigned)(2 + group * stride));
        sb_printf(&o, "    if (tide_adding%d) {\n        let tide_o%d = tide_region%d + 2u + (%s[tide_lix]%s - 1u) * %du;\n", a, a, a,
                  scan.data, lane, stride);
        em.o = &o;
        em.indent = 1;
        char base[32];
        snprintf(base, sizeof base, "tide_o%d", a);
        char value[32];
        snprintf(value, sizeof value, "tide_added%d", a);
        int f = 0, n = 0;
        element_words(slot_element(&em, i), &n);
        sb store = {0};
        em.o = &store;
        put_store(&em, slot_element(&em, i), value, &f, &n, pool, base, atomic);
        // Indented once more, inside the if
        for (const char *c = store.data; c && *c; c++) {
            if (c == store.data || c[-1] == '\n') sb_put(&o, "    ");
            sb_putn(&o, c, 1);
        }
        if (atomic) sb_printf(&o, "    }\n    if (tide_lix == %du) {\n        atomicStore(&%s[tide_region%d], bitcast<i32>(%s[tide_lix]%s));\n"
                                  "    }\n    workgroupBarrier();\n", group - 1, pool, a, scan.data, lane);
        else sb_printf(&o, "    }\n    if (tide_lix == %du) {\n        %s[tide_region%d] = %s[tide_lix]%s;\n    }\n"
                           "    workgroupBarrier();\n", group - 1, pool, a, scan.data, lane);
        a++;
    }
    sb_put(&o, "}\n");
    sh->wgsl = o.data;
}

// A kernel's shader: one for each kernel that a GPU.Run names, as it reads what it reads whatever runs it.
static shader *plan_kernel(program *prog, const expr *call, decl *k)
{
    for (int i = 0; i < prog->shaders.count; i++) {
        if (prog->shaders.items[i]->compute && prog->shaders.items[i]->kernel == k) return prog->shaders.items[i];
    }
    shader *sh = NEW(shader);
    sh->index = prog->shaders.count;
    sh->compute = true;
    sh->kernel = k;
    sh->dims = 0;
    sh->skin_base_at = sh->storage_at = -1;
    const param *values = NULL;
    for (int i = 0; i < k->params.count; i++) {
        if (k->params.items[i].type.kind == TY_STRUCT) values = &k->params.items[i];
    }
    sh->values_type = values ? values->type.decl : NULL;
    sb name = {0};
    sb_printf(&name, "compute " STR_FMT, STR_ARG(k->name));
    sh->name = name.data;
    plan_reads(prog, sh, k);
    for (int i = 0; i < sh->values.count; i++) { // A kernel's are all the run's, once for it
        shader_value *v = &sh->values.items[i];
        v->per_instance = false;
        v->float_at = sh->uniform_floats;
        v->int_at = sh->uniform_ints;
        sh->uniform_floats += v->floats;
        sh->uniform_ints += v->ints;
    }
    for (int i = 0; i < sh->textures.count; i++) {
        shader_texture *t = &sh->textures.items[i];
        if (t->read != READ_DATA) continue;
        t->count_at = sh->uniform_ints;
        const type held = t->asset ? t->asset->return_type : t->field->type;
        sh->uniform_ints += held.kind == TY_LIST ? 1 : held.kind != TY_GRID ? 2 : held.decl->dims == 3 ? 10 : 6;
    }
    for (int i = 0; i < sh->sizes.count; i++) {
        sh->sizes.items[i].count_at = sh->uniform_ints;
        sh->uniform_ints += 2;
    }
    sh->rolled_at = sh->seed_at = -1;
    if (k->rolls) sh->rolled_at = sh->uniform_ints++;
    if (k->reads_seed) sh->seed_at = sh->uniform_ints++;
    // Which lists it writes, adds to and uses atomics on
    int adds = 0;
    for (int i = 0; i < sh->storage.count; i++) {
        shader_storage *s = &sh->storage.items[i];
        for (int j = 0; j < k->adds.count; j++) s->add |= k->adds.items[j] == s->field;
        for (int j = 0; j < k->atomics.count; j++) s->atomic |= k->atomics.items[j] == s->field;
        for (int j = 0; j < k->gpu_writes.count; j++) s->write |= k->gpu_writes.items[j] == s->field;
        s->write |= s->add || s->atomic;
        adds += s->add;
    }
    if (sh->storage.count > SHADER_STORAGE_MOST) {
        diag_error(call->at, "kernel '" STR_FMT "' names %d GPU lists, and a kernel names %d at the most", STR_ARG(k->name),
                   sh->storage.count, SHADER_STORAGE_MOST);
        diag_note("keep what several lists hold in one list of a struct, or split it in two runs");
        return NULL;
    }
    // Where each list is in its pool and its capacity, two words of its ints a list, then each Add's scratch's place,
    // a word each, which the renderer writes as it runs it (tide_shader.storage_table)
    if (sh->storage.count) {
        sh->storage_at = sh->uniform_ints;
        sh->uniform_ints += 2 * sh->storage.count + adds;
    }
    // What the group shares: its shared locals (within 16 KiB, check_kernel_flow), and its Adds' scans, a lane each
    int shared_bytes = scan_bytes(adds, kernel_group(k));
    bool shares = false;
    for (int i = 0; k->body && i < k->body->stmts.count; i++) {
        const stmt *s = k->body->stmts.items[i];
        if (s->kind != S_VAR || !s->is_shared || s->type.kind == TY_ERROR) continue;
        shared_bytes += type_layout(s->type).size;
        if (s->shared_atomic) shared_bytes += (shared_atomic_length(k, s) - s->type.decl->array_length + 1) * 4;
        shares = true;
    }
    shared_bytes += shares ? 4 : 0; // ...and the word its writes to them are under (writes_shared)
    if (shared_bytes > 16384) {
        diag_error(call->at, "kernel '" STR_FMT "' shares %d bytes within a group with its Adds' scans (%d bytes a list, "
                   "four lists a scan), and a group has 16384 (16 KiB) on every GPU", STR_ARG(k->name), shared_bytes,
                   kernel_group(k) * 4);
        diag_note("share less in it, or give it a smaller group");
        return NULL;
    }
    const int uniform_bytes = 16 * (packs(sh->uniform_floats) + packs(sh->uniform_ints));
    if (uniform_bytes > 4096) {
        diag_error(call->at, "kernel '" STR_FMT "' reads %d bytes of singletons and values, and a run's hold 4096",
                   STR_ARG(k->name), uniform_bytes);
        return NULL;
    }
    if (sh->textures.count > SHADER_TEXTURES) {
        diag_error(call->at, "kernel '" STR_FMT "' reads %d textures, and a run takes %d", STR_ARG(k->name), sh->textures.count,
                   SHADER_TEXTURES);
        return NULL;
    }
    if (plan_samplers(sh) > SHADER_TEXTURES) {
        diag_error(call->at, "kernel '" STR_FMT "' reads its textures %d ways, and a run has %d samplers", STR_ARG(k->name),
                   sh->samplers.count, SHADER_TEXTURES);
        return NULL;
    }
    write_kernel(prog, sh);
    vec_push(prog->shaders, sh);
    return sh;
}
