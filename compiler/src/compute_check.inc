// Compute on the GPU, in the checker (docs/spec.md, Compute on the GPU;
// notes/compute.md): kernels (`compute`), GPU lists (GPUList<T>), GPU.Run and
// GPU.Barrier, `shared` locals, ordered Add and atomics. Included by check.c.

// The kernel whose own body is being checked, or NULL: a function it calls is not its body.
static decl *kernel_own(const checker *c)
{
    return c->method && c->method->kind == DECL_SHADER && c->method->is_compute ? c->method : NULL;
}

// The field a GPU list is named by in code the GPU runs: `meadow.blades`, a field of a component, singleton or
// values struct the kernel, frag or vert takes, or of a struct a function is given. NULL for anything else.
static const field *gpu_list_field(const expr *e)
{
    return e && e->kind == E_MEMBER && e->field && e->object && e->object->kind == E_NAME && e->object->bind == BIND_PARAM
        ? e->field : NULL;
}

// The CPU reads a GPU list's elements or its count where `e` is: in code the GPU runs that's what it does; in a
// function that's what makes it the GPU's (check_gpu_only_calls); elsewhere it's an error, said here.
static bool gpu_list_read_ok(checker *c, const loc at, const char *what)
{
    if (gpu_code(c)) return true;
    decl *fn = gpu_function(c);
    if (fn) {
        if (!fn->gpu_only) {
            fn->gpu_only = true;
            fn->gpu_only_at = at;
            fn->gpu_only_reads = TY_GPU_LIST;
        }
        return true;
    }
    diag_error(at, "a GPU list's %s %s on the GPU: the CPU reads it back with 'await list.%s'", what,
               strcmp(what, "count") == 0 ? "is" : "are", strcmp(what, "count") == 0 ? "ReadCount()" : "Read()");
    diag_note("code the GPU runs (a kernel, a frag, a vert) reads it as it is; local async code awaits a copy, a frame or more "
              "later, and publishes it into local state");
    return false;
}

static type gpu_list_element(const type t)
{
    return t.decl->fields.items[0].type;
}

static type check_gpu_list_member(checker *c, expr *e, const type obj)
{
    if (str_eq_c(e->member, "count")) {
        if (!gpu_list_read_ok(c, e->at, "count")) return T_ERR;
        e->gpu = GPU_COUNT;
        return T_INT_;
    }
    if (str_eq_c(e->member, "capacity")) {
        e->gpu = GPU_CAPACITY;
        return T_INT_;
    }
    if (str_eq_c(e->member, "contentRevision")) {
        if (gpu_refuses(c, e->at, "read a GPU list's contentRevision", "read it in a view, and pass what the kernel needs in a field"))
            return T_ERR;
        e->gpu = GPU_REVISION;
        return T_LONG_;
    }
    diag_error(e->at, "a GPU list has 'count', 'capacity' and 'contentRevision', and methods like Set(items) and Add(value), "
                      "not '" STR_FMT "'", STR_ARG(e->member));
    suggestion s = suggest_start(e->member);
    suggest_consider_c(&s, "count");
    suggest_consider_c(&s, "capacity");
    suggest_consider_c(&s, "contentRevision");
    suggest_note(&s);
    (void)obj;
    return T_ERR;
}

static type check_gpu_list_index(checker *c, expr *e, const type obj)
{
    const type index = check_expr_want(c, e->lhs, T_INT_);
    if (!gpu_list_read_ok(c, e->at, "elements")) return T_ERR;
    if (index.kind != TY_ERROR && !type_assignable(T_INT_, index)) {
        diag_error(e->lhs->at, "a GPU list's index is an int, not %s", type_name(index));
        return T_ERR;
    }
    e->gpu = GPU_INDEX;
    return gpu_list_element(obj);
}

// Where a kernel writes a GPU list (an element, Add, an atomic): its own body, and a list a field names. Says
// what's wrong otherwise.
static bool gpu_list_write_ok(checker *c, const expr *list, const loc at, const char *does)
{
    const decl *s = gpu_code(c);
    if (s && !s->is_compute) {
        diag_error(at, "a %s reads GPU lists, and kernels write them: a %s can't %s", shader_word(s), shader_word(s), does);
        diag_note("a 'compute' declaration writes them, run with GPU.Run before the draw: what's drawn after a run sees what it "
                  "wrote");
        return false;
    }
    if (!s) {
        if (gpu_function(c) && !c->method->is_async) {
            diag_error(at, "a kernel's own body writes GPU lists, so a function can't %s", does);
            diag_note("return what it works out, and %s in the kernel", does);
        } else {
            diag_error(at, "the CPU can't %s: kernels write GPU lists on the GPU", does);
            diag_note("local code sends a list's elements with 'list.Set(items)', in frame order with draws and runs");
        }
        return false;
    }
    if (!gpu_list_field(list)) {
        diag_error(list->at, "a kernel writes a GPU list a field names, of a component, singleton or values struct it takes");
        diag_note("like 'meadow.shown.Add(blade)', for a kernel that takes 'Meadow meadow'");
        return false;
    }
    return true;
}

static void note_field_once(VEC(const field *) *fields, const field *f)
{
    for (int i = 0; i < fields->count; i++) {
        if (fields->items[i] == f) return;
    }
    vec_push(*fields, f);
}

static const char *const atomic_names[] = {"AtomicAdd", "AtomicSub", "AtomicMin", "AtomicMax", "AtomicAnd", "AtomicOr",
                                           "AtomicXor", "AtomicExchange", "AtomicCompareExchange"};

static bool is_atomic_name(const str name)
{
    for (size_t i = 0; i < sizeof atomic_names / sizeof atomic_names[0]; i++) {
        if (str_eq_c(name, atomic_names[i])) return true;
    }
    return false;
}

// list.AtomicAdd(i, v) and the rest, on an int element of a GPU list or of a kernel's `shared int[N]`: the old value.
static type check_atomic(checker *c, expr *e, const type element, const bool shared)
{
    const bool exchange = str_eq_c(e->name, "AtomicCompareExchange");
    const int wanted = exchange ? 3 : 2;
    for (int i = 0; i < e->args.count; i++) check_expr_want(c, e->args.items[i], T_INT_);
    if (element.kind != TY_INT) {
        diag_error(e->at, "atomics work on ints, and this %s holds %s", shared ? "array" : "list", type_name(element));
        diag_note("no backend has portable float atomics: count in ints, or Add elements in order with 'list.Add(v)'");
        return T_ERR;
    }
    if (e->args.count != wanted) {
        diag_error(e->at, exchange ? "AtomicCompareExchange takes the element's index, the value it compares with, and the "
                                     "value it puts there when they're equal: 'counts.AtomicCompareExchange(i, 0, 1)'"
                                   : STR_FMT " takes the element's index and a value: 'counts." STR_FMT "(i, 1)'",
                   STR_ARG(e->name), STR_ARG(e->name));
        return T_ERR;
    }
    for (int i = 0; i < e->args.count; i++) {
        const type t = e->args.items[i]->type;
        if (t.kind != TY_ERROR && !type_assignable(T_INT_, t)) {
            diag_error(e->args.items[i]->at, "an atomic's %s is an int, not %s", i ? "value" : "index", type_name(t));
            return T_ERR;
        }
        vec_push(e->arg_want, T_INT_);
    }
    e->gpu = shared ? GPU_SHARED_ATOMIC : GPU_ATOMIC;
    return T_INT_;
}

// What a GPU list's methods are, for its errors.
static void note_gpu_list_methods(const bool gpu)
{
    if (gpu) diag_note("a kernel has 'list.Add(value)', 'list[i] = value' and, on a GPUList<int>, 'AtomicAdd(i, value)' and the "
                       "other atomics; frags and verts read 'list[i]', 'count' and foreach");
    else diag_note("local code has Set(items), Clear(), SetCount(n), Resize(capacity), Dispose(), and 'await Read()' and "
                   "'await ReadCount()' in local async code");
}

static type check_gpu_list_method(checker *c, expr *e, const type obj)
{
    program *prog = c->prog;
    const type element = gpu_list_element(obj);
    if (str_eq_c(e->name, "Add")) {
        if (e->args.count == 1) check_expr_want(c, e->args.items[0], element);
        else for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
        if (!gpu_list_write_ok(c, e->object, e->at, "add to a GPU list")) return T_ERR;
        if (e->args.count != 1) {
            diag_error(e->at, "Add takes the element it adds: 'list.Add(value)'");
            return T_ERR;
        }
        const type t = e->args.items[0]->type;
        if (t.kind != TY_ERROR && !type_assignable(element, t)) {
            diag_error(e->args.items[0]->at, "%s holds %s, not %s", type_name(obj), type_name(element), type_name(t));
            return T_ERR;
        }
        if (c->statement_call != e) {
            diag_error(e->at, "Add is a statement of its own: it gives nothing");
            return T_ERR;
        }
        if (c->loop_depth > 0) {
            diag_error(e->at, "a kernel's invocation adds to a GPU list once at the most, so Add can't be in a loop");
            diag_note("each invocation's element goes in invocation order; to add several, run over more invocations, or "
                      "count places with atomics, 'int at = counts.AtomicAdd(0, n);', and write 'list[at + k] = v'");
            return T_ERR;
        }
        vec_push(e->arg_want, element);
        decl *kernel = kernel_own(c);
        note_field_once((void *)&kernel->adds, gpu_list_field(e->object));
        note_field_once((void *)&kernel->gpu_writes, gpu_list_field(e->object));
        e->gpu = GPU_ADD;
        return T_VOID_;
    }
    if (is_atomic_name(e->name)) {
        if (!gpu_list_write_ok(c, e->object, e->at, "use atomics")) {
            for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
            return T_ERR;
        }
        const type result = check_atomic(c, e, element, false);
        if (result.kind != TY_ERROR) {
            note_field_once((void *)&kernel_own(c)->atomics, gpu_list_field(e->object));
            note_field_once((void *)&kernel_own(c)->gpu_writes, gpu_list_field(e->object));
        }
        return result;
    }
    const bool read = str_eq_c(e->name, "Read") || str_eq_c(e->name, "ReadCount");
    const bool frame = str_eq_c(e->name, "Set") || str_eq_c(e->name, "Clear") || str_eq_c(e->name, "SetCount");
    const bool state = str_eq_c(e->name, "Resize") || str_eq_c(e->name, "Dispose");
    if (!read && !frame && !state) {
        for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
        diag_error(e->at, "a GPU list has no method '" STR_FMT "'", STR_ARG(e->name));
        note_gpu_list_methods(gpu_code(c) != NULL);
        return T_ERR;
    }
    if (gpu_code(c)) {
        for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
        gpu_refuses(c, e->at, read ? "read a GPU list back" : "change what a GPU list is", NULL);
        note_gpu_list_methods(true);
        return T_ERR;
    }
    if (str_eq_c(e->name, "Set")) {
        if (e->args.count == 1) check_expr_want(c, e->args.items[0], list_of(prog, element));
        else for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
    } else {
        for (int i = 0; i < e->args.count; i++) check_expr_want(c, e->args.items[i], T_INT_);
    }
    const int wanted = str_eq_c(e->name, "Set") || str_eq_c(e->name, "SetCount") || str_eq_c(e->name, "Resize") ? 1 : 0;
    if (e->args.count != wanted) {
        diag_error(e->at, wanted ? STR_FMT " takes one argument: '%s'" : STR_FMT " takes nothing: 'list." STR_FMT "()'",
                   STR_ARG(e->name), str_eq_c(e->name, "Set") ? "list.Set(items)" : str_eq_c(e->name, "SetCount") ? "list.SetCount(n)"
                   : str_eq_c(e->name, "Resize") ? "list.Resize(capacity)" : "", STR_ARG(e->name));
        return T_ERR;
    }
    if (wanted) {
        const type want = str_eq_c(e->name, "Set") ? list_of(prog, element) : T_INT_;
        const type t = e->args.items[0]->type;
        if (t.kind != TY_ERROR && !type_assignable(want, t)) {
            diag_error(e->args.items[0]->at, str_eq_c(e->name, "Set") ? "Set sends a %s, the list's elements, not %s"
                                                                       : "%s, not %s", str_eq_c(e->name, "Set") ? type_name(want)
                       : str_eq_c(e->name, "SetCount") ? "a count is an int" : "a capacity is an int", type_name(t));
            return T_ERR;
        }
        vec_push(e->arg_want, want);
    }
    if (read) {
        if (c->awaiting != e) {
            diag_error(e->at, STR_FMT " is a read the GPU finishes later: await it, 'var items = await list." STR_FMT "();'",
                       STR_ARG(e->name), STR_ARG(e->name));
            return T_ERR;
        }
        if (in_async_function(c)) {
            if (!note_side(c->method, TASK_LOCAL, e->at)) return T_ERR;
        } else if (!local_code(c)) {
            diag_error(e->at, "a GPU list is this machine's, so only local async code reads it back");
            return T_ERR;
        }
        decl *code = reading_code(c);
        if (code) code->uses_gpu_read = true; // Its task's frame keeps the read's ticket
        e->gpu = str_eq_c(e->name, "Read") ? GPU_READ : GPU_READ_COUNT;
        const type value = e->gpu == GPU_READ ? list_of(prog, element) : T_INT_;
        return result_of(prog, value, (type){TY_ENUM, prog->io_error}, false);
    }
    if (frame && !check_frame_use(c, e->at, "A GPU list's Set, Clear and SetCount")) return T_ERR;
    e->gpu = str_eq_c(e->name, "Set") ? GPU_SET : str_eq_c(e->name, "Clear") ? GPU_CLEAR : str_eq_c(e->name, "SetCount")
           ? GPU_SET_COUNT : str_eq_c(e->name, "Resize") ? GPU_RESIZE : GPU_DISPOSE;
    e->resource_operation = true;
    decl *code = reading_code(c);
    if (code) code->calls_c = true;
    return T_VOID_;
}

// sums.AtomicAdd(i, v) on a kernel's `shared int[N]`.
static bool shared_local(const expr *e)
{
    return e && e->kind == E_NAME && e->bind == BIND_LOCAL && e->local && e->local->is_shared;
}

static type check_shared_atomic(checker *c, expr *e, const type obj)
{
    if (!kernel_own(c)) {
        for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
        diag_error(e->at, "atomics on shared memory are a kernel's own body's");
        return T_ERR;
    }
    if (obj.kind != TY_ARRAY) {
        for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
        diag_error(e->at, "atomics on shared memory are on an element of a 'shared int[N]': 'sums.AtomicAdd(i, 1)'");
        return T_ERR;
    }
    const type result = check_atomic(c, e, array_element(obj), true);
    if (result.kind != TY_ERROR) ((stmt *)e->object->local)->shared_atomic = true;
    return result;
}

// list[i] = v in a kernel: the element, dropped past the list's capacity. The count stays: Add, Set, Clear and
// SetCount change it. True when `s` writes a GPU list, said here; false for any other assignment.
static bool check_gpu_list_write(checker *c, const stmt *s)
{
    const expr *index = NULL;
    for (const expr *part = s->target; part && (part->kind == E_MEMBER || part->kind == E_INDEX); part = part->object) {
        if (part->kind == E_INDEX && part->object->type.kind == TY_GPU_LIST) index = part;
    }
    if (!index) return false;
    if (s->target->type.kind == TY_ERROR || !gpu_list_write_ok(c, index->object, s->at, "write a GPU list's element"))
        return true;
    if (index != s->target) {
        diag_error(s->target->at, "a kernel writes a GPU list's whole element: read it, change it, and write it back");
        diag_note("like 'var b = list[i]; b.height = 2; list[i] = b;'");
        return true;
    }
    note_field_once((void *)&kernel_own(c)->gpu_writes, gpu_list_field(index->object));
    if (c->statement_call && c->step_assignment) return true;
    const type value = s->value->type;
    if (s->op == T_ASSIGN && value.kind != TY_ERROR && !type_assignable(s->target->type, value)) {
        diag_error(s->value->at, "can't assign %s to %s", type_name(value), type_name(s->target->type));
        note_conversion(s->target->type, value);
    }
    return true;
}

// GPUList<T>(capacity): an empty list of this machine's, made in local code.
static type check_gpu_list_make(checker *c, expr *e)
{
    for (int i = 0; i < e->args.count; i++) check_expr_want(c, e->args.items[i], T_INT_);
    if (e->type_args.count != 1) {
        diag_error(e->at, "a GPU list says what it holds in <>: 'GPUList<float4>(1024)'");
        return T_ERR;
    }
    const generic_form *form = generic_form_named(str_from("GPUList"));
    const type element = resolve_arg(c, e->type_args.items[0], form, 0, e->at, NULL, false, false);
    if (element.kind == TY_ERROR) return T_ERR;
    const char *why = gpu_element_refusal(element, 0);
    if (why) {
        diag_error(e->at, "a GPUList can't hold %s: %s", type_name(element), why);
        return T_ERR;
    }
    if (gpu_refuses(c, e->at, "make a GPU list", "local code makes it, 'GPUList<float4>(1024)', and keeps it in a local field")) return T_ERR;
    if (e->args.count != 1) {
        diag_error(e->at, "a GPU list is made with its capacity, the most elements it holds: 'GPUList<%s>(1024)'", type_name(element));
        return T_ERR;
    }
    const type t = e->args.items[0]->type;
    if (t.kind != TY_ERROR && !type_assignable(T_INT_, t)) {
        diag_error(e->args.items[0]->at, "a GPU list's capacity is an int, not %s", type_name(t));
        return T_ERR;
    }
    vec_push(e->arg_want, T_INT_);
    e->gpu = GPU_MAKE;
    e->resource_operation = true;
    decl *code = reading_code(c);
    if (code) code->calls_c = true;
    return gpu_list_of(c->prog, element);
}

// The struct a kernel takes as its run's values, or NULL.
static const param *kernel_values(const decl *kernel)
{
    for (int i = 0; i < kernel->params.count; i++) {
        if (kernel->params.items[i].type.kind == TY_STRUCT) return &kernel->params.items[i];
    }
    return NULL;
}

// GPU.Run(kernel, over[, values]) and GPU.Barrier().
static type check_gpu_static(checker *c, expr *e)
{
    e->object->bind = BIND_TYPE;
    if (str_eq_c(e->name, "Barrier")) {
        for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
        decl *kernel = kernel_own(c);
        if (!kernel) {
            diag_error(e->at, "GPU.Barrier() waits for a kernel's whole group, so it's a kernel's own body's");
            diag_note(gpu_code(c) ? "frags and verts have no groups" : "it's code the GPU runs: a 'compute' declaration");
            return T_ERR;
        }
        if (e->args.count) {
            diag_error(e->at, "GPU.Barrier takes nothing: 'GPU.Barrier();'");
            return T_ERR;
        }
        if (c->statement_call != e) {
            diag_error(e->at, "GPU.Barrier() is a statement of its own");
            return T_ERR;
        }
        kernel->barrier = true;
        e->gpu = GPU_BARRIER;
        return T_VOID_;
    }
    if (!str_eq_c(e->name, "Run")) {
        for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
        diag_error(e->at, "GPU has Run(kernel, over[, values]) and Barrier(), not '" STR_FMT "'", STR_ARG(e->name));
        return T_ERR;
    }
    decl *kernel = e->args.count ? named_shader(c, e->args.items[0]) : NULL;
    for (int i = 1; i < e->args.count; i++) check_expr(c, e->args.items[i]);
    if (gpu_refuses(c, e->at, "run a kernel", "a view runs it, with 'GPU.Run(kernel, over)', before what reads its results"))
        return T_ERR;
    if (!check_frame_use(c, e->at, "GPU.Run")) return T_ERR;
    if (e->args.count < 2 || e->args.count > 3) {
        diag_error(e->at, "GPU.Run takes the kernel, what it runs over, and maybe its values: 'GPU.Run(Cull, meadow.blades)'");
        diag_note("what it runs over is a count (an int), a size (an int2 or int3), a List<T> (its count) or a GPUList<T> (its "
                  "count on the GPU)");
        return T_ERR;
    }
    if (!kernel) {
        diag_error(e->args.items[0]->at, "GPU.Run runs a kernel, a 'compute' declaration, by its name");
        return T_ERR;
    }
    if (!kernel->is_compute) {
        diag_error(e->args.items[0]->at, "GPU.Run runs a kernel, a 'compute' declaration, and '" STR_FMT "' is a %s",
                   STR_ARG(kernel->name), shader_word(kernel));
        diag_note("a %s is drawn: name it in a Draw.Mesh", shader_word(kernel));
        return T_ERR;
    }
    if (c->statement_call != e) {
        diag_error(e->at, "GPU.Run is a statement of its own: what the kernel works out, it writes to GPU lists");
        return T_ERR;
    }
    const type over = e->args.items[1]->type;
    if (over.kind != TY_ERROR && over.kind != TY_INT && over.kind != TY_INT2 && over.kind != TY_INT3 && over.kind != TY_LIST
        && over.kind != TY_GPU_LIST && over.kind != TY_BYTE) {
        diag_error(e->args.items[1]->at, "a kernel runs over a count (an int), a size (an int2 or int3), a List<T> or a "
                   "GPUList<T>, and this is %s", type_name(over));
        return T_ERR;
    }
    const param *values = kernel_values(kernel);
    if (e->args.count == 3) {
        const type given = e->args.items[2]->type;
        if (given.kind == TY_ERROR) return T_ERR;
        if (!values) {
            diag_error(e->args.items[2]->at, "kernel '" STR_FMT "' takes no values: it takes a struct parameter for them, "
                       "'%s " STR_FMT "(%s settings, ...)'", STR_ARG(kernel->name), "compute", STR_ARG(kernel->name),
                       given.kind == TY_STRUCT ? type_name(given) : "Settings");
            return T_ERR;
        }
        if (given.kind != TY_STRUCT || given.decl != values->type.decl) {
            diag_error(e->args.items[2]->at, "kernel '" STR_FMT "' takes a %s as its values, and this is %s", STR_ARG(kernel->name),
                       type_name(values->type), type_name(given));
            return T_ERR;
        }
        e->values = e->args.items[2];
        e->values_type = given.decl;
    } else if (values) {
        diag_error(e->at, "kernel '" STR_FMT "' takes a %s: give it after what it runs over, 'GPU.Run(" STR_FMT ", over, %s { ... })'",
                   STR_ARG(kernel->name), type_name(values->type), STR_ARG(kernel->name), type_name(values->type));
        return T_ERR;
    }
    e->gpu = GPU_RUN;
    e->kernel = kernel;
    const mesh_draw run = {e, reading_code(c)};
    vec_push(c->prog->compute_runs, run);
    return T_VOID_;
}

// [Group(x[, y[, z]])] on a kernel: its group's size, 64 when it says none, within what every target has.
static void check_group(checker *c, decl *d)
{
    d->group[0] = 64;
    d->group[1] = d->group[2] = 1;
    for (int a = 0; a < d->attributes.count; a++) {
        attribute *attr = &d->attributes.items[a];
        if (!str_eq_c(attr->name, "Group")) continue;
        if (attr->values.count < 1 || attr->values.count > 3 || attr->args.count) {
            diag_error(attr->at, "Group takes a kernel's group size along one, two or three axes: '[Group(64)]', "
                                 "'[Group(8, 8)]', '[Group(4, 4, 4)]'");
            return;
        }
        int size[3] = {1, 1, 1};
        for (int i = 0; i < attr->values.count; i++) {
            expr *v = attr->values.items[i];
            const type t = check_constant_expr(c, v, true, T_INT_);
            int64_t n;
            if (t.kind == TY_ERROR) return;
            if (!fold_int(v, &n) || n < 1 || n > 256) {
                diag_error(v->at, "a group's size along an axis is a whole number from 1 to 256");
                return;
            }
            size[i] = (int)n;
        }
        if (size[2] > 64 || size[0] * size[1] * size[2] > 256) {
            diag_error(attr->at, "a group is 256 invocations at the most, and 64 along z: what every GPU runs (WebGPU's least), "
                                 "and this is %d", size[0] * size[1] * size[2]);
            diag_note("like '[Group(64)]', '[Group(16, 16)]' or '[Group(4, 4, 4)]'");
            return;
        }
        memcpy(d->group, size, sizeof size);
    }
}

// A kernel's signature: the components of the entity the calling view runs for and singletons, as a frag's, and
// at most one struct, its run's values. It returns nothing, and changes nothing it takes.
static void check_kernel_decl(const checker *c, decl *d)
{
    const program *prog = c->prog;
    d->return_type = d->result = d->fails = T_VOID_;
    d->outputs = 0;
    d->place = NULL;
    const param *values = NULL;
    for (int i = 0; i < d->params.count; i++) {
        param *p = &d->params.items[i];
        bind_alias_ref(c, p->type_ref);
        const loc at = p->type_at.line ? p->type_at : p->at;
        p->type = T_ERR;
        if (p->verified) {
            diag_error(p->verified_at, "a kernel reads what the view that runs it is given, so 'verified' isn't its to say");
            p->verified = false;
        }
        if (p->mode == PARAM_WITH || p->mode == PARAM_WITHOUT) {
            diag_error(p->at, "a kernel has no entities to filter: it takes what it reads");
            continue;
        }
        if (p->name.len > 0) {
            check_reserved(p->name, p->at);
            for (int j = 0; j < i; j++) {
                if (str_eq(d->params.items[j].name, p->name)) diag_error(p->name_at, "parameter '" STR_FMT "' is declared twice", STR_ARG(p->name));
            }
        }
        if (p->mode == PARAM_MUT) {
            diag_error(p->at, "a kernel reads what it takes, and writes GPU lists: drop '%s'", p->is_out ? "out" : "mut");
            diag_note("what it works out goes to a GPU list a field of what it takes names, like 'meadow.shown.Add(blade)'");
            p->mode = PARAM_READ;
            p->is_out = false;
        }
        if (names_type_as(c, p->type_name, "World") && !find_type(c, p->type_name, p->type_at)) {
            diag_error(at, "a kernel can't reach other entities: the GPU is sent what it takes");
            continue;
        }
        decl *t = find_type(c, p->type_name, p->type_at);
        if (t && t->kind == DECL_COMPONENT) t = component_placement(c->prog, t, p->placement_local, p->type_at);
        else if (p->placement_local) diag_error(p->placement_at, "local access qualifies a component");
        type value = T_ERR;
        const bool plain = !t && resolve_builtin_alias(c, p->type_name, &value);
        if (t && t->kind == DECL_STRUCT) {
            if (values) {
                diag_error(p->at, "a kernel takes one struct, its run's values, and this is a second: put both in one");
                continue;
            }
            values = p;
            p->type = decl_type(t);
            continue;
        }
        if (plain || (t && t->kind == DECL_ENUM) || (!t && (names_type_as(c, p->type_name, "Instance")
                                                          || names_type_as(c, p->type_name, "Pixel")))) {
            diag_error(at, "a kernel takes components, singletons and one struct of values, and '" STR_FMT "' is %s",
                       STR_ARG(p->type_name), plain ? type_name(value) : t ? decl_what(t) : "a draw's");
            diag_note("its place in the run is 'this': 'this.index', 'this.at'; what the run is given goes in a struct, "
                      "'GPU.Run(kernel, over, Settings { ... })'");
            continue;
        }
        if (!t) {
            diag_error(at, "unknown component, singleton or struct '" STR_FMT "'", STR_ARG(p->type_name));
            suggestion s = suggest_start(p->type_name);
            suggest_decls(&s, prog, true, true, false);
            suggest_note(&s);
            continue;
        }
        if (t->kind != DECL_COMPONENT && t->kind != DECL_SINGLETON) {
            diag_error(at, "'" STR_FMT "' is %s; a kernel takes components, singletons and one struct", STR_ARG(t->name), decl_what(t));
            continue;
        }
        for (int j = 0; j < i; j++) {
            if (d->params.items[j].type.decl == t) diag_error(p->at, "'" STR_FMT "' appears twice", STR_ARG(t->name));
        }
        p->type = decl_type(t);
    }
}

// ---------------------------------------------------------------------------
// A kernel's body, once checked: each invocation adds to a list once at the
// most, GPU.Barrier() where every invocation of a group reaches it, and
// `shared` locals at its top level.

static bool adds_to(const stmt *s, const field *list)
{
    return s && s->kind == S_EXPR && s->value && s->value->gpu == GPU_ADD && gpu_list_field(s->value->object) == list;
}

// The most Adds to `list` on a path through `s` that goes on after it, from `before` (-1: none goes on), with
// *most the most on any path that ended.
static int adds_through(const stmt *s, const field *list, const int before, int *most, const stmt **second)
{
    if (!s || before < 0) return before;
    switch (s->kind) {
    case S_BLOCK: {
        int n = before;
        for (int i = 0; i < s->stmts.count; i++) n = adds_through(s->stmts.items[i], list, n, most, second);
        return n;
    }
    case S_EXPR:
        if (!adds_to(s, list)) return before;
        if (before >= 1 && !*second) *second = s;
        return before + 1;
    case S_IF: {
        const int a = adds_through(s->then_stmt, list, before, most, second);
        const int b = s->else_stmt ? adds_through(s->else_stmt, list, before, most, second) : before;
        return a > b ? a : b;
    }
    case S_SWITCH: {
        int n = before; // With no default, a path goes past every section
        for (int i = 0; i < s->cases.count; i++) {
            int k = before;
            for (int j = 0; j < s->cases.items[i].body.count; j++) k = adds_through(s->cases.items[i].body.items[j], list, k, most, second);
            if (k > n) n = k;
        }
        return n;
    }
    case S_RETURN:
    case S_FAIL:
        if (before > *most) *most = before;
        return -1;
    default: // Loops can't Add (check_gpu_list_method); their returns end paths with what was added before them
        return before;
    }
}

static bool has_barrier(const stmt *s);

static bool expr_is_barrier(const expr *e)
{
    return e && e->gpu == GPU_BARRIER;
}

static bool has_barrier(const stmt *s)
{
    if (!s) return false;
    if (s->kind == S_EXPR) return expr_is_barrier(s->value);
    if (has_barrier(s->then_stmt) || has_barrier(s->else_stmt) || has_barrier(s->init)) return true;
    for (int i = 0; i < s->stmts.count; i++) {
        if (has_barrier(s->stmts.items[i])) return true;
    }
    for (int i = 0; i < s->cases.count; i++) {
        for (int j = 0; j < s->cases.items[i].body.count; j++) {
            if (has_barrier(s->cases.items[i].body.items[j])) return true;
        }
    }
    return false;
}

static bool leaves_loop(const stmt *s)
{
    if (!s) return false;
    if (s->kind == S_BREAK || s->kind == S_CONTINUE || s->kind == S_RETURN || s->kind == S_FAIL) return true;
    if (leaves_loop(s->then_stmt) || leaves_loop(s->else_stmt)) return true;
    for (int i = 0; i < s->stmts.count; i++) {
        if (leaves_loop(s->stmts.items[i])) return true;
    }
    for (int i = 0; i < s->cases.count; i++) {
        for (int j = 0; j < s->cases.items[i].body.count; j++) {
            if (leaves_loop(s->cases.items[i].body.items[j])) return true;
        }
    }
    return false;
}

// A for whose count is the same for every invocation: `for (var i = A; i < B; i++)`, A and B constants.
static bool counts_alike(const stmt *s)
{
    int64_t n;
    if (s->kind != S_FOR || !s->init || s->init->kind != S_VAR || !s->init->value || !fold_int(s->init->value, &n)) return false;
    const expr *cond = s->cond;
    if (!cond || cond->kind != E_BINARY) return false;
    const bool left = cond->lhs->kind == E_NAME && cond->lhs->bind == BIND_LOCAL && cond->lhs->local == s->init;
    const bool right = cond->rhs->kind == E_NAME && cond->rhs->bind == BIND_LOCAL && cond->rhs->local == s->init;
    return (left && fold_int(cond->rhs, &n)) || (right && fold_int(cond->lhs, &n));
}

// GPU.Barrier() in `s` where every invocation of a group reaches it (WGSL's uniformity, conservatively): in no
// branch, in loops of a constant count that nothing leaves early, with no return before it. *returned: a return
// came before, on some path.
static void barriers_uniform(const stmt *s, const bool loop, bool *returned)
{
    if (!s) return;
    switch (s->kind) {
    case S_BLOCK:
        for (int i = 0; i < s->stmts.count; i++) barriers_uniform(s->stmts.items[i], loop, returned);
        return;
    case S_EXPR:
        if (!expr_is_barrier(s->value)) return;
        if (*returned) {
            diag_error(s->at, "every invocation of a group reaches GPU.Barrier(), and a 'return' before it ends some of them");
            diag_note("run the rest under a condition instead: 'if (this.index < count) { ... }', with the barrier outside it");
        }
        return;
    case S_RETURN:
    case S_FAIL:
        *returned = true;
        return;
    case S_FOR:
        if (has_barrier(s->then_stmt)) {
            if (!counts_alike(s)) {
                diag_error(s->at, "every invocation of a group reaches GPU.Barrier(), so a loop with one counts the same for each: "
                                  "from a constant to a constant");
                diag_note("like 'for (var step = 1; step < 64; step *= 2)', with 64 a constant");
            } else if (leaves_loop(s->then_stmt)) {
                diag_error(s->at, "a loop with GPU.Barrier() in it runs to its end for every invocation: 'break', 'continue' "
                                  "and 'return' in it would leave some behind");
            }
            barriers_uniform(s->then_stmt, true, returned);
        }
        return;
    case S_IF:
    case S_WHILE:
    case S_FOREACH:
    case S_SWITCH:
    case S_PARALLEL: {
        const stmt *parts[2] = {s->then_stmt, s->else_stmt};
        bool barrier = has_barrier(parts[0]) || has_barrier(parts[1]);
        for (int i = 0; i < s->cases.count && !barrier; i++) {
            for (int j = 0; j < s->cases.items[i].body.count; j++) barrier |= has_barrier(s->cases.items[i].body.items[j]);
        }
        if (barrier) {
            diag_error(s->at, "every invocation of a group reaches GPU.Barrier(), and this %s can send them different ways",
                       s->kind == S_IF ? "'if'" : s->kind == S_SWITCH ? "'switch'" : "loop");
            diag_note("put the barrier outside it: each invocation does its part under the condition, then all of them wait");
        }
        if (s->kind != S_SWITCH && !loop) { // A return inside counts as one before what follows
            bool inner = *returned;
            barriers_uniform(s->then_stmt, loop, &inner);
            barriers_uniform(s->else_stmt, loop, &inner);
            *returned |= inner;
        } else if (leaves_loop(s)) {
            *returned = true;
        }
        return;
    }
    default:
        return;
    }
}

static void shared_placement(const stmt *s, const bool top)
{
    if (!s) return;
    if (s->kind == S_VAR && s->is_shared && !top) {
        diag_error(s->shared_at, "a shared local is the whole group's, so it's declared at the top of the kernel's body, outside "
                                 "any block");
    }
    shared_placement(s->then_stmt, false);
    shared_placement(s->else_stmt, false);
    for (int i = 0; i < s->stmts.count; i++) shared_placement(s->stmts.items[i], false);
    for (int i = 0; i < s->cases.count; i++) {
        for (int j = 0; j < s->cases.items[i].body.count; j++) shared_placement(s->cases.items[i].body.items[j], false);
    }
}

static void check_kernel_flow(const checker *c, decl *d)
{
    (void)c;
    if (!d->body) return;
    for (int i = 0; i < d->body->stmts.count; i++) shared_placement(d->body->stmts.items[i], true);
    int shared_bytes = 0;
    for (int i = 0; i < d->body->stmts.count; i++) {
        const stmt *s = d->body->stmts.items[i];
        if (s->kind != S_VAR || !s->is_shared || s->type.kind == TY_ERROR) continue;
        const char *why = gpu_element_refusal(s->type, 0);
        if (why) {
            diag_error(s->type_at.line ? s->type_at : s->at, "a shared local is a GPU value or an array of them, and this is %s: %s",
                       type_name(s->type), why);
            continue;
        }
        if (s->value && s->value->kind != E_DEFAULT) {
            diag_error(s->value->at, "a shared local starts zeroed for the whole group: it takes no value of its own");
            diag_note("one invocation writes it after, with a GPU.Barrier() before the others read it");
        }
        shared_bytes += type_layout(s->type).size;
    }
    if (shared_bytes > 16384) {
        diag_error(d->at, "kernel '" STR_FMT "' shares %d bytes within a group, and a group has 16384 (16 KiB) on every GPU",
                   STR_ARG(d->name), shared_bytes);
    }
    if (d->adds.count && (d->group[1] > 1 || d->group[2] > 1)) {
        diag_error(d->at, "kernel '" STR_FMT "' adds to a GPU list, so its group is along one axis, like '[Group(%d)]': Add keeps "
                   "this.index's order, which a group of %d by %d%s doesn't run in", STR_ARG(d->name),
                   d->group[0] * d->group[1] * d->group[2], d->group[0], d->group[1], d->group[2] > 1 ? " by more" : "");
        diag_note("a run over a size of two or three axes is still in this.index's order with a group of one axis: each row "
                  "of its size is groups after each other");
    }
    for (int i = 0; i < d->adds.count; i++) {
        int most = 0;
        const stmt *second = NULL;
        const int end = adds_through(d->body, d->adds.items[i], 0, &most, &second);
        if (second || end > 1 || most > 1) {
            diag_error(second ? second->at : d->at, "'" STR_FMT "' can add to '" STR_FMT "' twice: an invocation adds to a GPU list "
                       "once at the most, on any path", STR_ARG(d->name), STR_ARG(d->adds.items[i]->name));
            diag_note("pick the element first, and add it once: 'var kept = ...; if (keep) " STR_FMT ".Add(kept);'",
                      STR_ARG(d->adds.items[i]->name));
        }
    }
    if (d->barrier) {
        bool returned = false;
        barriers_uniform(d->body, false, &returned);
    }
}
