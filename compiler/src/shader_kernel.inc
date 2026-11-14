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
    sb_put(&o, "const tide_reversed_depth = true;\n"
               "fn tide_depth(d: f32) -> f32 { return select(d, 1.0 - d, tide_reversed_depth); }\n");
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
// The format of image `color` of a target, cube or array expression (Format's member number), or of a Texture or
// Texture3D's image: -1 unknown or ambiguous, -2 no such image, -3 no image at all (default), -6 a float grid's.
// A field's is every origin's (field.image_assignments), and an `out` or `mut` argument's place what the callee
// assigns its parameter (expr.image_place).
static int origins_target_format(const expr *first, const image_origins *origins, const int color, const int depth)
{
    int found = -3;
    for (int i = -1; i < origins->count; i++) {
        const expr *origin = i < 0 ? first : origins->items[i];
        if (!origin) continue;
        const int got = image_target_format(origin, color, depth + 1);
        if (got == -3) continue;
        if (got < 0 && got != -2) return got;
        if (found != -3 && found != got) return -1;
        found = got;
    }
    return found;
}

int image_target_format(const expr *e, const int color, const int depth)
{
    if (!e || depth > 32) return -1;
    if (e->image_place) return e->param ? origins_target_format(NULL, &e->param->image_assignments, color, depth) : -1;
    if (e->type.kind == TY_GRID && e->type.decl) { // A grid's image: R16f read as Color or float, RGBA8, or one of numbers
        const type numbers = type_number_grid_role(e->type);
        return numbers.kind == TY_FLOAT2 ? 6 : numbers.kind == TY_FLOAT4 ? 2 : numbers.kind == TY_INT ? 9
             : e->type.decl->fields.items[0].type.kind == TY_FLOAT ? -6 : 0;
    }
    if (e->kind == E_DEFAULT) return -3;
    if (e->kind == E_NAME && e->bind == BIND_LOCAL && e->local && !e->local->assigned)
        return image_target_format(e->local->value, color, depth + 1);
    if (e->kind == E_CONDITIONAL) {
        const int a = image_target_format(e->lhs, color, depth + 1), b = image_target_format(e->rhs, color, depth + 1);
        return a == -3 ? b : b == -3 ? a : a == b ? a : -1;
    }
    if (e->kind == E_MEMBER && e->object && e->object->type.kind == TY_RENDER_TARGET && str_eq_c(e->member, "color"))
        return image_target_format(e->object, 0, depth + 1);
    if (e->kind == E_INDEX && e->object && e->object->kind == E_MEMBER && e->object->target_colors) {
        // A cube's face and an array's layer hold the one image their cube or array does
        if (e->object->object && (e->object->object->type.kind == TY_CUBEMAP || e->object->object->type.kind == TY_TEXTURE_ARRAY))
            return color == 0 ? image_target_format(e->object->object, 0, depth + 1) : -2;
        if (e->lhs->kind != E_INT || e->lhs->int_value < 0 || e->lhs->int_value > 3) return -1;
        return image_target_format(e->object->object, (int)e->lhs->int_value, depth + 1);
    }
    if (e->field) return origins_target_format(e->field->default_value, &e->field->image_assignments, color, depth);
    if (e->call == CALL_BUILTIN && e->resource_operation && e->c_callee
        && (e->type.kind == TY_RENDER_TARGET || e->type.kind == TY_TEXTURE3D || e->type.kind == TY_CUBEMAP
            || e->type.kind == TY_TEXTURE_ARRAY)) {
        int slot = 0;
        for (int i = 0; i < e->args.count; i++) {
            const expr *f = e->args.items[i];
            if (f->type.kind != TY_ENUM || !f->type.decl || !str_eq_c(f->type.decl->name, "Format")) continue;
            if (slot++ != color) continue;
            for (int k = 0; f->constant && k < 32; k++) f = f->constant->value;
            return f->enum_member ? (int)f->enum_member->number : -1;
        }
        return slot == 0 && color == 0 ? 0 : -2;
    }
    return -1;
}

bool image_format_accepts(const int format, const type value)
{
    return format == 1 || format == -2 || format == -3
        || (format == 0 && value.kind == TY_COLOR)
        || (format == 2 && (value.kind == TY_COLOR || value.kind == TY_FLOAT4))
        || ((format == 3 || format == 5 || format == 7) && value.kind == TY_FLOAT)
        || ((format == 4 || format == 6) && value.kind == TY_FLOAT2)
        || (format == 8 && value.kind == TY_FLOAT4) || (format == 9 && value.kind == TY_INT);
}

static const char *image_format_types(const int format)
{
    return format == 0 ? "Color" : format == 2 ? "Color or float4" : format == 4 || format == 6 ? "float2"
         : format == 8 ? "float4" : format == 9 ? "int" : "float";
}

static bool numeric_image_origin(const expr *e, const int depth)
{
    if (!e || depth > 32) return false;
    if (e->image_place) {
        for (int i = 0; e->param && i < e->param->image_assignments.count; i++)
            if (numeric_image_origin(e->param->image_assignments.items[i], depth + 1)) return true;
        return false;
    }
    if (e->kind == E_NAME && e->bind == BIND_LOCAL && e->local)
        return numeric_image_origin(e->local->value, depth + 1);
    if (e->kind == E_CONDITIONAL)
        return numeric_image_origin(e->lhs, depth + 1) || numeric_image_origin(e->rhs, depth + 1);
    if (e->field) {
        if (numeric_image_origin(e->field->default_value, depth + 1)) return true;
        for (int i = 0; i < e->field->image_assignments.count; i++)
            if (numeric_image_origin(e->field->image_assignments.items[i], depth + 1)) return true;
    }
    if ((e->type.kind == TY_RENDER_TARGET || e->type.kind == TY_CUBEMAP || e->type.kind == TY_TEXTURE_ARRAY) && e->resource_operation) {
        for (int i = 0; i < e->args.count; i++) {
            const expr *f = e->args.items[i];
            if (f->type.kind == TY_ENUM && f->type.decl && str_eq_c(f->type.decl->name, "Format")
                && (!f->enum_member || f->enum_member->number >= 3)) return true;
        }
    }
    return numeric_image_origin(e->object, depth + 1);
}

static bool check_image_outputs(program *prog, const expr *call)
{
    const expr *target = call->blit ? call->args.items[1] : call->targeted ? call->object : NULL;
    const bool window = !target || (target->kind == E_MEMBER && str_eq_c(target->member, "window"));
    const int outputs = call->frag ? call->frag->outputs : 1;
    for (int i = 0; i < outputs; i++) {
        const int format = window ? (i == 0 ? 0 : -2) : image_target_format(target, i, 0);
        const shader fake = {.frag = call->frag};
        const type value = shader_output_type(&fake, i);
        if (format == -1 && (value.kind != TY_COLOR || numeric_image_origin(target, 0))) {
            diag_error(call->at, "this numeric frag's destination format isn't fixed by its constructor assignments: give this target one fixed Format for each image");
            return false;
        }
        if (format < 0 || image_format_accepts(format, value)) continue;
        diag_error(call->at, "frag output %d is %s, but Format." STR_FMT " takes %s: change the output type or this image's format",
                   i + 1, type_name(value), STR_ARG(prog->format->members.items[format].name), image_format_types(format));
        return false;
    }
    return true;
}

static void check_image_clear(program *prog, const expr *call)
{
    const expr *target = call->targeted ? call->object : NULL;
    const bool window = !target || (target->kind == E_MEMBER && str_eq_c(target->member, "window"));
    const bool several = strcmp(call->c_callee, "tide_draw_clear_images") == 0;
    int count = 0;
    for (int i = 0; i < 4; i++) {
        const int format = window ? (i == 0 ? 0 : -2) : image_target_format(target, i, 0);
        if (format == -1) return; // A dynamic target is checked by the runtime.
        if (format == -2 || format == -3 || format == 1) break;
        count++;
        const int arg = several ? i : 0;
        if (arg >= call->args.count) continue;
        const type value = call->args.items[arg]->type;
        if (image_format_accepts(format, value)) continue;
        diag_error(call->at, "clear value %d is %s, but Format." STR_FMT " takes %s: give each image a clear value of its type, in order",
                   i + 1, type_name(value), STR_ARG(prog->format->members.items[format].name), image_format_types(format));
        return;
    }
    if (several && call->args.count != count)
        diag_error(call->at, "this target holds %d images: give Clear exactly one value for each image, in order", count);
}

// await target.Read<T>(image): the cell type written there against the format the target's image was made with. A
// Color read of a target whose formats aren't known here (a parameter, a target made two ways) is checked as it
// runs, and fails there; a number read's format has to be known, as its grid's type is the image's own.
static void check_target_read(program *prog, const expr *call)
{
    static const char *const cells[] = {"Color", "float", "float2", "float4", "int"};
    static const char *const reads[] = {"Read", "Read<float>", "Read<float2>", "Read<float4>", "Read<int>"};
    const int kind = call->read_kind, image = call->read_image;
    if (kind > 4) return; // ReadDepth: every target has depth
    const expr *target = call->object;
    if (target->kind == E_INDEX && target->object && target->object->kind == E_MEMBER && target->object->target_colors
        && target->object->object && target->object->object->type.kind != TY_RENDER_TARGET) {
        // cube.faces[i], array.layers[i]: one image, of the format its cube or array was made with
        if (image != 0) {
            const bool cube = target->object->object->type.kind == TY_CUBEMAP;
            diag_error(call->at, "a %s holds one image: read it with 'await %s.Read()', with no number, and its depth with "
                                 "'ReadDepth()'", cube ? "cube's face" : "TextureArray's layer",
                       cube ? "cube.faces[i]" : "array.layers[i]");
            return;
        }
    }
    const int format = image_target_format(target, image, 0);
    if (format == 1 || format == -2) {
        if (format == 1 || image == 0) {
            diag_error(call->at, "this target holds depth alone, and Read reads a color image: read its depth with 'await "
                                 "target.ReadDepth()'");
        } else {
            diag_error(call->at, "Read(%d) reads the target's color image %d, and this target was made with fewer: its images "
                                 "are counted from 0, in the order of the formats it was made with", image, image);
        }
        return;
    }
    if (format < 0) {
        if (kind == 0) return;
        diag_error(call->at, "%s gives a Grid2<%s> of the image's own numbers, and this target's format for image %d isn't fixed "
                             "by its constructor assignments", reads[kind], cells[kind], image);
        diag_note("make the target one way wherever it's assigned, with a Format for each image, 'RenderTarget(width, height, "
                  "Format.R32f)', there or in a function it's handed to as 'out' or 'mut', and read the field or local it's "
                  "kept in; a RenderTarget parameter's format, and that of one C makes, isn't known here");
        return;
    }
    const bool fits = kind == 0 ? format == 0 || format == 2 : kind == 1 ? format == 3 || format == 5 || format == 7
                    : kind == 2 ? format == 4 || format == 6 : kind == 3 ? format == 2 || format == 8 : format == 9;
    if (fits) return;
    const int want = format == 0 || format == 2 ? 0 : format == 3 || format == 5 || format == 7 ? 1 : format == 4 || format == 6 ? 2
                   : format == 8 ? 3 : 4;
    char index[16] = "";
    if (image) snprintf(index, sizeof index, "%d", image);
    diag_error(call->at, "%s gives a Grid2<%s>, but this target's image %d is Format." STR_FMT ", which holds %s: write 'await "
                         "target.%s(%s)'", reads[kind], cells[kind], image, STR_ARG(prog->format->members.items[format].name),
               format == 2 ? "Color or float4" : cells[want], reads[want], index);
    if (format == 2) diag_note("'Read<float4>(%s)' gives an Rgba16f image's four numbers as stored; 'Read(%s)' gives them as a "
                               "Color, its alpha undone", index, index);
}

// A storage texture's format is part of its GPU declaration. Follow all resource assignments rather than choosing
// the first constructor reached: a different format on another path must be diagnosed before any backend runs.
static int image_origin_format(const expr *e, int color, int depth);

static int origins_storage_format(const expr *first, const image_origins *origins, const int color, const int depth)
{
    int found = -3;
    for (int i = -1; i < origins->count; i++) {
        const expr *origin = i < 0 ? first : origins->items[i];
        if (!origin) continue;
        const int got = image_origin_format(origin, color, depth + 1);
        if (got == -3) continue;
        if (got < 0) return got;
        if (found != -3 && found != got) return -1;
        found = got;
    }
    return found;
}

static int image_origin_format(const expr *e, const int color, const int depth)
{
    if (!e || depth > 32) return -1;
    if (e->image_place) return e->param ? origins_storage_format(NULL, &e->param->image_assignments, color, depth) : -1;
    if (e->type.kind == TY_GRID && e->type.decl && e->type.decl->dims == 3)
        return e->type.decl->fields.items[0].type.kind == TY_FLOAT ? -6 : -5;
    if (e->type.kind == TY_GRID) return -7; // A Grid2's Texture, of any cells: the CPU's cells, sampled
    if (e->kind == E_DEFAULT) return -3; // No image: not another format
    if (e->kind == E_NAME && e->bind == BIND_LOCAL && e->local && !e->local->assigned)
        return image_origin_format(e->local->value, color, depth + 1);
    if (e->kind == E_CONDITIONAL) {
        const int a = image_origin_format(e->lhs, color, depth + 1), b = image_origin_format(e->rhs, color, depth + 1);
        return a == -3 ? b : b == -3 ? a : a == b ? a : -1;
    }
    if (e->kind == E_MEMBER && e->object && e->object->type.kind == TY_RENDER_TARGET && str_eq_c(e->member, "color"))
        return image_origin_format(e->object, 0, depth + 1);
    if (e->kind == E_INDEX && e->object && e->object->kind == E_MEMBER && e->object->target_colors) {
        if (e->lhs->kind != E_INT || e->lhs->int_value < 0 || e->lhs->int_value > 3) return -1;
        return image_origin_format(e->object->object, (int)e->lhs->int_value, depth + 1);
    }
    if (e->field) return origins_storage_format(e->field->default_value, &e->field->image_assignments, color, depth);
    if (e->call == CALL_BUILTIN && e->resource_operation && e->c_callee
        && (e->type.kind == TY_RENDER_TARGET || e->type.kind == TY_TEXTURE3D)) {
        if (e->type.kind == TY_RENDER_TARGET && e->target_samples) {
            const expr *samples = e->args.items[e->args.count - 1];
            for (int i = 0; samples->constant && i < 16; i++) samples = samples->constant->value;
            if (samples->kind != E_INT || samples->int_value != 1) return -4;
        }
        int slot = 0;
        for (int i = 0; i < e->args.count; i++) {
            const expr *f = e->args.items[i];
            if (f->type.kind != TY_ENUM) continue;
            if (slot++ != color) continue;
            for (int k = 0; f->constant && k < 32; k++) f = f->constant->value;
            if (!f->enum_member) return -1;
            const str name = f->enum_member->name;
            return str_eq_c(name, "Rgba8") ? 0 : str_eq_c(name, "Rgba16f") ? 4 : str_eq_c(name, "R32f") ? 5
                 : str_eq_c(name, "Rgba32f") ? 6 : str_eq_c(name, "R32i") ? 7 : -2;
        }
        return slot == 0 && color == 0 ? 0 : -2;
    }
    return -1;
}

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
    if (!check_image_sources(prog, sh, call)) return NULL;
    int image_writes = 0;
    for (int i = 0; i < sh->textures.count; i++) {
        shader_texture *t = &sh->textures.items[i];
        if (!t->write) continue;
        if (field_is_linear(t->field)) {
            diag_error(call->at, "a kernel cannot write a [Linear] sampled image: sRGB formats have no storage-write form");
            diag_note("write an unmarked Format.Rgba8 target, then assign its color image to a [Linear] field for sampling");
            return NULL;
        }
        image_writes++;
        expr source = {0};
        source.field = (field *)(uintptr_t)t->field;
        const int format = t->field ? image_origin_format(&source, t->image_color, 0) : -1;
        if (format < 0) {
            diag_error(call->at, format == -4
                       ? "a kernel writes a single-sample image: create a RenderTarget without samples, or with samples: 1, then draw it into the multisampled target"
                       : format == -6
                       ? "a Grid3<float> converts to a sampled R16f volume, which core WebGPU cannot store into: create a Texture3D with Format.Rgba16f and fill it with a kernel"
                       : format == -5
                       ? "a grid-backed volume is a sampled view of CPU data: create an owned Texture3D and fill it with a kernel instead"
                       : format == -7
                       ? "a grid's Texture is a sampled view of its CPU cells, which the CPU's next change would replace: write a RenderTarget's color image with a fixed Format instead, and sample that"
                       : format == -2
                       ? (prog->format->members.count > 3
                          ? "this image's format isn't a portable storage format: write into Format.Rgba8, Rgba16f, R32f, Rgba32f or R32i instead; depth, BGRA, byte/half-float one-channel and two-channel images aren't core storage formats"
                          : "this image's format isn't a portable storage format: write into Format.Rgba8 or Format.Rgba16f instead; depth, BGRA, byte/half-float one-channel and two-channel images aren't core storage formats")
                       : "this image's writable format isn't fixed by its constructor assignments: assign one fixed Format to this field, or write into a second image with a fixed format");
            return NULL;
        }
        t->image_format = (unsigned)format;
    }
    if (image_writes > 4) {
        diag_error(call->at, "a kernel writes %d images, and every GPU supports 4 at the most: split this work into separate GPU.Run calls", image_writes);
        return NULL;
    }
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
        sh->uniform_ints += shader_size_words(&sh->sizes.items[i]);
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
