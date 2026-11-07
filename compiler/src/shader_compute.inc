// Compute on the GPU, in the shader writer (notes/compute.md): GPU lists as
// storage buffers, kernels' WGSL, ordered Add's compaction, atomics, shared
// memory and barriers. Included by shader.c.
//
// A GPU list a shader's code names is its slot: a range of one of the
// renderer's two pools, `tide_ints` (at binding 34: lists of ints, which atomics
// take; array<atomic<i32>> in a kernel that uses atomics) and `tide_words` (at
// 33: the others), arrays of u32 words. A range is a header of 4 words (its
// count, its capacity) and then its elements, `stride` words each, a value's
// floats' bits then its ints, as put_value reads words everywhere. Where each
// slot's range starts and its capacity are two words of the uniforms' ints from
// `storage_at` on (the table, which the renderer writes: tide_shader's
// storage_table), and every read and write is held inside it. Each slot has
// functions of its own, written once a shader: tide_base<s>, tide_word<s>,
// tide_count<s>, tide_capacity<s>, tide_get<s> and, in a kernel that writes it,
// tide_put<s>.

// The words of an element of type `t`: its floats, then its ints.
static int element_words(const type t, int *floats)
{
    int f = 0, i = 0;
    type_words(t, &f, &i);
    if (floats) *floats = f;
    return f + i;
}

// The storage slot of the GPU list `list` names, a field of what the engine supplies or of a struct a function is
// given, or a function's own GPUList parameter, which its call names (the function is written for that slot, as for
// a list's texture): -1 while checking, which notes the field as read. Refuses anything else.
static int gpu_list_slot(emit *em, const expr *list)
{
    if (list && list->kind == E_NAME && list->bind == BIND_PARAM && list->param->function_param
        && list->param->type.kind == TY_GPU_LIST && em->code != em->root) { // Its caller's, noted where the call names it
        return em->checking || !em->sh || !em->spec ? -1 : em->spec[param_index(em->code, list->param)].slot;
    }
    const expr *obj = list && list->kind == E_MEMBER ? list->object : NULL;
    if (!obj || !list->field || obj->kind != E_NAME || obj->bind != BIND_PARAM
        || !(is_bound(em, obj->param) || names_given(em, obj))) {
        refuse(em, list ? list->at : (loc){0}, "a GPU list in code the GPU runs is named where it's used, a field of what it "
                                               "takes or a function's parameter: it can't be kept in a local or returned there");
        diag_note("pass it on by that name, 'Keep(meadow.shown, blade)', to a function that takes a GPUList");
        return -1;
    }
    const param *p = obj->param;
    if (!is_bound(em, p)) {
        note_given_field(em, obj, list->field, list->local_field);
    } else if (em->checking && list->local_field) {
        em->root->params.items[param_index(em->root, p)].local_reads[field_index(p->type.decl->local_part, list->field)] = true;
    } else if (em->checking) {
        em->root->params.items[param_index(em->root, p)].reads[field_index(p->type.decl, list->field)] = true;
    }
    if (em->checking || !em->sh) return -1;
    for (int i = 0; i < em->sh->storage.count; i++) {
        if (em->sh->storage.items[i].field == list->field && em->sh->storage.items[i].source == p->type.decl) return i;
    }
    return -1;
}

// Whether a kernel's pool of ints is atomic: it uses atomics on a list of it. Then every list of ints it names is
// read and written through atomics, as WGSL takes an atomic array no other way.
static bool ints_atomic(const shader *sh)
{
    for (int i = 0; sh && sh->compute && i < sh->storage.count; i++) {
        if (sh->storage.items[i].ints && sh->storage.items[i].atomic) return true;
    }
    return false;
}

static bool slot_atomic(const emit *em, const int slot)
{
    return em->sh && slot >= 0 && em->sh->storage.items[slot].ints && ints_atomic(em->sh);
}

// The pool slot `slot`'s list is a range of, as the shader declares it.
static const char *slot_pool(const emit *em, const int slot)
{
    return slot >= 0 && em->sh->storage.items[slot].ints ? "tide_ints" : "tide_words";
}

// The pools a shader's GPU lists are ranges of, each one it names: `tide_words` at binding 33, `tide_ints` at 34
// (tide/draw.h's TIDE_STORAGE_WORDS, TIDE_STORAGE_INTS), read in a frag or a vert; in a kernel read_write where it
// writes a list of it (an Add's scratch is in its list's pool), and the pool of ints array<atomic<i32>> where it uses
// atomics on one. Then a kernel's run record, at 35 (TIDE_STORAGE_RUN). Bindings from SHADER_STORAGE_BINDING on.
static void put_pools(const shader *sh, sb *o)
{
    bool words = false, ints = false, words_written = false, ints_written = false;
    for (int i = 0; i < sh->storage.count; i++) {
        const shader_storage *s = &sh->storage.items[i];
        if (s->ints) ints = true, ints_written |= s->write;
        else words = true, words_written |= s->write;
    }
    if (words) {
        sb_printf(o, "@group(0) @binding(%d) var<storage, %s> tide_words: array<u32>;\n", SHADER_STORAGE_BINDING,
                  sh->compute && words_written ? "read_write" : "read");
    }
    if (ints) {
        sb_printf(o, "@group(0) @binding(%d) var<storage, %s> tide_ints: array<%s>;\n", SHADER_STORAGE_BINDING + 1,
                  sh->compute && ints_written ? "read_write" : "read", ints_atomic(sh) ? "atomic<i32>" : "u32");
    }
    if (sh->compute) sb_printf(o, "@group(0) @binding(%d) var<storage, read> tide_run: array<u32>;\n", SHADER_STORAGE_BINDING + 2);
}

// A word of the uniforms' table, as a u32: `word` of their ints.
static void put_table_word(emit *em, sb *text, const int word)
{
    sb *const out = em->o;
    em->o = text;
    sb_put(text, "u32(");
    put_word(em, 'v', word);
    sb_put(text, ")");
    em->o = out;
}

// The element type of the GPU list in slot `slot`: for a list the draw gives, its instances', its vertices' or its
// indices'.
static type slot_element(const emit *em, const int slot)
{
    const shader_storage *s = &em->sh->storage.items[slot];
    if (s->role == STORAGE_VERTICES) return (type){TY_STRUCT, em->sh->vertex};
    if (s->role == STORAGE_INDICES) return (type){TY_INT, NULL};
    if (!s->field) return em->sh->instance_type ? (type){TY_STRUCT, em->sh->instance_type} : (type){TY_FLOAT4X4, NULL};
    return s->field->type.decl->fields.items[0].type;
}

// Statements that store a value of type `t` (the WGSL expression `access`) as its words into `array` from word
// `base` on: floats at *f, ints at *i, as put_value reads them back.
static void put_store(emit *em, const type t, const char *access, int *f, int *i, const char *array, const char *base,
                      const bool atomic)
{
    // Its paths are as long as its fields' names and its nesting make them: built, never cut short
#define STORE(at, ...) do { \
        sb value = {0}; \
        sb_printf(&value, __VA_ARGS__); \
        if (atomic) sb_printf(em->o, "    atomicStore(&%s[%s + %du], bitcast<i32>(%s));\n", array, base, at, value.data); \
        else sb_printf(em->o, "    %s[%s + %du] = %s;\n", array, base, at, value.data); \
    } while (0)
    const int dim = type_dim(t);
    const int n = matrix_dim(t);
    if (t.kind == TY_BOOL) {
        STORE(*i, "select(0u, 1u, %s)", access);
        *i += 1;
    } else if (t.kind == TY_BYTE || t.kind == TY_ENUM || t.kind == TY_CHAR || (dim == 1 && !type_is_float_based(t))) {
        STORE(*i, "bitcast<u32>(%s)", access);
        *i += 1;
    } else if (dim == 1) {
        STORE(*f, "bitcast<u32>(%s)", access);
        *f += 1;
    } else if (dim > 1 || t.kind == TY_COLOR || t.kind == TY_QUATERNION) {
        const int count = dim > 1 ? dim : 4;
        const bool floats = dim == 0 || type_is_float_based(t);
        for (int k = 0; k < count; k++) STORE(floats ? *f + k : *i + k, "bitcast<u32>((%s).%c)", access, "xyzw"[k]);
        *(floats ? f : i) += count;
    } else if (n > 0) {
        for (int c = 0; c < n; c++) {
            for (int r = 0; r < n; r++) STORE(*f + c * n + r, "bitcast<u32>((%s)[%d][%d])", access, c, r);
        }
        *f += n * n;
    } else if (t.kind == TY_ARRAY) {
        for (int k = 0; k < t.decl->array_length; k++) {
            sb inner = {0};
            sb_printf(&inner, "(%s)[%d]", access, k);
            put_store(em, t.decl->fields.items[0].type, inner.data, f, i, array, base, atomic);
        }
    } else if (is_struct_like(t)) {
        for (int k = 0; k < t.decl->fields.count; k++) {
            const field *fl = &t.decl->fields.items[k];
            if (fl->hidden) continue;
            sb inner = {0};
            sb_printf(&inner, "(%s).%s", access, field_name(fl));
            put_store(em, fl->type, inner.data, f, i, array, base, atomic);
        }
    }
#undef STORE
}

// The value of type `element` whose words are at `base` of what `word` reads ("tide_word3(%s)"): declares its packs
// as the locals tide_q<k> (ints) and tide_p<k> (floats), then returns the expression that makes the value.
static void put_load(emit *em, sb *text, const type element, const char *word, const char *base)
{
    int floats = 0;
    const int words = element_words(element, &floats);
    for (int k = 0; k < packs(words); k++) {
        sb_printf(text, "    let tide_q%d = vec4i(", k);
        for (int c = 0; c < 4; c++) {
            const int w = 4 * k + c;
            if (c) sb_put(text, ", ");
            if (w >= words) {
                sb_put(text, "0");
                continue;
            }
            char at[96];
            snprintf(at, sizeof at, "%s + %du", base, w);
            sb_put(text, "bitcast<i32>(");
            sb_printf(text, word, at);
            sb_put(text, ")");
        }
        sb_put(text, ");\n");
        if (4 * k < floats) sb_printf(text, "    let tide_p%d = bitcast<vec4f>(tide_q%d);\n", k, k);
    }
    sb *const out = em->o;
    em->o = text;
    sb_put(text, "    return ");
    int f = 0, n = floats;
    put_value(em, element, 'p', 'q', &f, &n);
    sb_put(text, ";\n");
    em->o = out;
}

// The functions that read and write slot `slot`, written once before the code that calls them.
static void need_storage(emit *em, const int slot)
{
    if (slot < 0 || slot >= SHADER_STORAGE_MOST || em->storage_done[slot]) return;
    em->storage_done[slot] = true;
    const shader_storage *s = &em->sh->storage.items[slot];
    const type element = slot_element(em, slot);
    const char *t = ty(em, element);
    sb text = {0};
    const bool atomic = slot_atomic(em, slot);
    const char *pool = slot_pool(em, slot);
    const int words = element_words(element, NULL);
    // Its range's first word and its capacity, as the table has them: the count is no more than that capacity, and
    // the capacity that's held to is the header's too, a word of storage (so Direct3D 11's FXC, which can't see what
    // storage holds, takes a write every invocation makes to one place under it: notes/compute.md)
    const int at = em->sh->storage_at + 2 * slot;
    sb_printf(&text, "fn tide_base%d() -> u32 {\n    return ", slot);
    put_table_word(em, &text, at);
    sb_put(&text, ";\n}\n\n");
    if (atomic) sb_printf(&text, "fn tide_word%d(tide_k: u32) -> u32 {\n    let tide_at = tide_base%d() + tide_k;\n"
                                 "    return bitcast<u32>(atomicLoad(&%s[tide_at]));\n}\n\n", slot, slot, pool);
    else sb_printf(&text, "fn tide_word%d(tide_k: u32) -> u32 {\n    return %s[tide_base%d() + tide_k];\n}\n\n", slot, pool, slot);
    sb_printf(&text, "fn tide_count%d() -> i32 {\n    return i32(min(tide_word%d(0u), ", slot, slot);
    put_table_word(em, &text, at + 1);
    sb_put(&text, "));\n}\n\n");
    sb_printf(&text, "fn tide_capacity%d() -> i32 {\n    return i32(min(tide_word%d(1u), ", slot, slot);
    put_table_word(em, &text, at + 1);
    sb_put(&text, "));\n}\n\n");
    sb_printf(&text, "fn tide_get%d(tide_i: i32) -> %s {\n    if (tide_i < 0 || tide_i >= tide_count%d()) {\n        return ", slot, t, slot);
    sb *const out = em->o;
    em->o = &text;
    put_zeros(em, element);
    em->o = out;
    sb_printf(&text, ";\n    }\n    let tide_b = 4u + u32(tide_i) * %du;\n", words);
    char word[32];
    snprintf(word, sizeof word, "tide_word%d(%%s)", slot);
    put_load(em, &text, element, word, "tide_b");
    sb_put(&text, "}\n\n");
    if (s->write) {
        sb_printf(&text, "fn tide_put%d(tide_i: i32, tide_v: %s) {\n    if (tide_i < 0 || tide_i >= tide_capacity%d()) {\n"
                         "        return;\n    }\n    let tide_b = tide_base%d() + 4u + u32(tide_i) * %du;\n", slot, t, slot,
                  slot, words);
        em->o = &text;
        int f = 0, i = 0;
        element_words(element, &i);
        put_store(em, element, "tide_v", &f, &i, pool, "tide_b", atomic);
        em->o = out;
        sb_put(&text, "}\n\n");
    }
    sb_put(&em->functions, text.data);
}

// An atomic on an element of slot `slot`'s list (a GPUList<int>, in the pool of ints) or, for a slot of -1, of the
// shared array `array`: the function that does it, which leaves an element past the capacity alone and gives 0
// there, written once.
static const char *need_atomic(emit *em, const int slot, const str name, const char *array, const int length)
{
    sb fn = {0};
    if (slot >= 0) sb_printf(&fn, "tide_atomic%d_" STR_FMT, slot, STR_ARG(name));
    else sb_printf(&fn, "tide_atomic_%s_" STR_FMT, array, STR_ARG(name));
    for (int i = 0; i < em->atomic_fns.count; i++) {
        if (!strcmp(em->atomic_fns.items[i], fn.data)) return fn.data;
    }
    vec_push(em->atomic_fns, fn.data);
    const bool exchange = str_eq_c(name, "AtomicCompareExchange");
    static const char *const ops[][2] = {{"AtomicAdd", "atomicAdd"}, {"AtomicSub", "atomicSub"}, {"AtomicMin", "atomicMin"},
                                         {"AtomicMax", "atomicMax"}, {"AtomicAnd", "atomicAnd"}, {"AtomicOr", "atomicOr"},
                                         {"AtomicXor", "atomicXor"}, {"AtomicExchange", "atomicExchange"}};
    const char *op = "atomicAdd";
    for (size_t i = 0; i < sizeof ops / sizeof ops[0]; i++) {
        if (str_eq_c(name, ops[i][0])) op = ops[i][1];
    }
    sb text = {0};
    sb_printf(&text, "fn %s(tide_i: i32, %stide_v: i32) -> i32 {\n", fn.data, exchange ? "tide_c: i32, " : "");
    char place[96]; // Each atomic names its element itself, as the translators take them
    if (slot >= 0) {
        sb_printf(&text, "    if (tide_i < 0 || tide_i >= tide_capacity%d()) {\n        return 0;\n    }\n"
                         "    let tide_at = tide_base%d() + 4u + u32(tide_i);\n", slot, slot);
        snprintf(place, sizeof place, "&tide_ints[tide_at]");
    } else {
        sb_printf(&text, "    if (tide_i < 0 || tide_i >= %d) {\n        return 0;\n    }\n", length);
        snprintf(place, sizeof place, "&%s.tide_cells[tide_i]", array); // A shared array, its struct (shared_atomic_length)
    }
    if (exchange) sb_printf(&text, "    return atomicCompareExchangeWeak(%s, tide_c, tide_v).old_value;\n}\n\n", place);
    else sb_printf(&text, "    return %s(%s, tide_v);\n}\n\n", op, place);
    sb_put(&em->functions, text.data);
    return fn.data;
}

// A write to a kernel's shared memory, not through atomics: under `if (tide_zero == 0u)`, a word of the group's
// that nothing writes, so always 0 (workgroup memory starts zeroed) and the write happens for every invocation as
// written. Unity's Direct3D 11 compiler (fxc) refuses a write every invocation makes to one place with values that
// differ (X3695, no pragma turns it off; a condition on the run's values or the group doesn't help), and a condition
// on what shared memory holds is one it can't see through. Measured with fxc 10.0.22621, cs_5_0 /WX: notes/compute.md.
static bool shared_atomic(const expr *e);

static bool writes_shared(const stmt *s)
{
    if (s->kind != S_ASSIGN) return false;
    const expr *root = s->target;
    while (root && (root->kind == E_INDEX || root->kind == E_MEMBER)) root = root->object;
    return root && root->kind == E_NAME && root->bind == BIND_LOCAL && root->local && root->local->is_shared
        && !shared_atomic(root);
}

// A kernel's `shared int[N]` that atomics are used on: its reads and writes are atomic too.
static bool shared_atomic(const expr *e)
{
    return e && e->kind == E_NAME && e->bind == BIND_LOCAL && e->local && e->local->is_shared && e->local->shared_atomic;
}

static const char *need_shared_access(emit *em, const stmt *local, const bool store)
{
    sb fn = {0};
    sb_printf(&fn, "tide_%s_%s", store ? "store" : "load", local_name(local->name));
    for (int i = 0; i < em->atomic_fns.count; i++) {
        if (!strcmp(em->atomic_fns.items[i], fn.data)) return fn.data;
    }
    vec_push(em->atomic_fns, fn.data);
    const int length = local->type.decl->array_length;
    sb text = {0};
    if (store) sb_printf(&text, "fn %s(tide_i: i32, tide_v: i32) {\n    if (tide_i >= 0 && tide_i < %d) {\n"
                                "        atomicStore(&%s.tide_cells[tide_i], tide_v);\n    }\n}\n\n", fn.data, length,
                         local_name(local->name));
    else sb_printf(&text, "fn %s(tide_i: i32) -> i32 {\n    if (tide_i < 0 || tide_i >= %d) {\n        return 0;\n    }\n"
                          "    return atomicLoad(&%s.tide_cells[tide_i]);\n}\n\n", fn.data, length, local_name(local->name));
    sb_put(&em->functions, text.data);
    return fn.data;
}

// The Add of a kernel to slot `slot`: which of its Adds it is.
static int add_of(const emit *em, const int slot)
{
    int k = 0;
    for (int i = 0; i < slot; i++) k += em->sh->storage.items[i].add;
    return k;
}

// An expression of a GPU list or a kernel's: written, and true; false for any other.
static bool ex_gpu(emit *em, const expr *e)
{
    if (e->kind == E_INDEX && shared_atomic(e->object)) {
        sb_printf(em->o, "%s(", need_shared_access(em, e->object->local, false));
        ex_as(em, e->lhs, (type){TY_INT, NULL});
        sb_put(em->o, ")");
        return true;
    }
    if (e->kind == E_NAME && shared_atomic(e)) {
        refuse(em, e->at, "a shared array that atomics are used on is read an element at a time: '" STR_FMT "[i]'", STR_ARG(e->name));
        return true;
    }
    switch (e->gpu) {
    case GPU_COUNT:
    case GPU_CAPACITY: {
        const int slot = gpu_list_slot(em, e->object);
        if (slot < 0) {
            sb_put(em->o, "0");
            return true;
        }
        need_storage(em, slot);
        sb_printf(em->o, e->gpu == GPU_COUNT ? "tide_count%d()" : "tide_capacity%d()", slot);
        return true;
    }
    case GPU_INDEX: {
        const int slot = gpu_list_slot(em, e->object);
        need_storage(em, slot);
        sb_printf(em->o, "tide_get%d(", slot < 0 ? 0 : slot);
        ex_as(em, e->lhs, (type){TY_INT, NULL});
        sb_put(em->o, ")");
        return true;
    }
    case GPU_ATOMIC:
    case GPU_SHARED_ATOMIC: {
        int slot = -1;
        char array[96] = "tide_ints";
        int length = 0;
        if (e->gpu == GPU_ATOMIC) {
            slot = gpu_list_slot(em, e->object);
            need_storage(em, slot);
            if (slot < 0) slot = 0;
        } else {
            snprintf(array, sizeof array, "%s", local_name(e->object->name));
            length = e->object->type.decl->array_length;
        }
        if (em->checking) { // Checked for what it reads
            for (int i = 0; i < e->args.count; i++) ex_as(em, e->args.items[i], (type){TY_INT, NULL});
            return true;
        }
        sb_printf(em->o, "%s(", need_atomic(em, slot, e->name, array, length));
        for (int i = 0; i < e->args.count; i++) {
            if (i) sb_put(em->o, ", ");
            ex_as(em, e->args.items[i], (type){TY_INT, NULL});
        }
        sb_put(em->o, ")");
        return true;
    }
    case GPU_ADD:
    case GPU_BARRIER:
        refuse(em, e->at, "this is a statement of its own"); // The checker said so already
        return true;
    case GPU_NONE:
        return false;
    default:
        refuse(em, e->at, "the GPU can't run '" STR_FMT "': it's the CPU's", STR_ARG(e->name));
        return true;
    }
}

// A statement of a GPU list or a kernel's: written, and true; false for any other.
static bool st_gpu(emit *em, const stmt *s)
{
    if (s->kind == S_VAR && s->is_shared) { // A module's variable, the group's (put_shared): nothing here
        if (em->code != em->root) refuse(em, s->at, "a shared local is a kernel's own");
        type_fits(em, s->type, s->type_at.line ? s->type_at : s->at);
        return true;
    }
    if (s->kind == S_ASSIGN && s->target->kind == E_INDEX && shared_atomic(s->target->object)) {
        line(em);
        sb_printf(em->o, "%s(", need_shared_access(em, s->target->object->local, true));
        ex_as(em, s->target->lhs, (type){TY_INT, NULL});
        sb_put(em->o, ", ");
        put_assigned(em, s);
        sb_put(em->o, ");\n");
        return true;
    }
    if (s->kind == S_ASSIGN && s->target->kind == E_INDEX && s->target->object->type.kind == TY_GPU_LIST) {
        const int slot = gpu_list_slot(em, s->target->object);
        need_storage(em, slot);
        line(em);
        sb_printf(em->o, "tide_put%d(", slot < 0 ? 0 : slot);
        ex_as(em, s->target->lhs, (type){TY_INT, NULL});
        sb_put(em->o, ", ");
        put_assigned(em, s);
        sb_put(em->o, ");\n");
        return true;
    }
    if (s->kind == S_EXPR && s->value->gpu == GPU_BARRIER) {
        line(em);
        sb_put(em->o, "workgroupBarrier();\n");
        line(em);
        sb_put(em->o, "storageBarrier();\n");
        return true;
    }
    if (s->kind == S_EXPR && s->value->gpu == GPU_ADD) {
        const int slot = gpu_list_slot(em, s->value->object);
        if (slot < 0) {
            if (em->checking) ex_as(em, s->value->args.items[0], s->value->arg_want.items[0]);
            return true;
        }
        const int k = add_of(em, slot);
        line(em);
        sb_printf(em->o, "tide_adding%d = true;\n", k);
        line(em);
        sb_printf(em->o, "tide_added%d = ", k);
        ex_as(em, s->value->args.items[0], s->value->arg_want.items[0]);
        sb_put(em->o, ";\n");
        return true;
    }
    if (s->kind == S_FOREACH && s->value && s->value->type.kind == TY_GPU_LIST) {
        const int slot = gpu_list_slot(em, s->value);
        need_storage(em, slot);
        const type element = s->value->type.decl->fields.items[0].type;
        if (!type_fits(em, element, s->value->at)) return true;
        const int k = em->temps++;
        const int at = slot < 0 ? 0 : slot;
        line(em);
        sb_printf(em->o, "for (var tide_k%d: i32 = 0; tide_k%d < tide_count%d(); tide_k%d++) {\n", k, k, at, k);
        em->indent++;
        line(em);
        sb_printf(em->o, "var %s: %s = tide_get%d(tide_k%d);\n", local_name(s->name), ty(em, element), at, k);
        const stmt *body = s->then_stmt;
        for (int i = 0; body && body->kind == S_BLOCK && i < body->stmts.count; i++) {
            st(em, body->stmts.items[i]);
            if (ends(body->stmts.items[i])) break;
        }
        if (body && body->kind != S_BLOCK) st(em, body);
        em->indent--;
        line(em);
        sb_put(em->o, "}\n");
        return true;
    }
    return false;
}

// this.index, this.at...: which invocation of its run a kernel's is, which its entry point sets.
static void ex_invocation(emit *em, const expr *e)
{
    sb_printf(em->o, "tide_inv_" STR_FMT, STR_ARG(e->member));
}

