// The server's store in match code: Storage.Save(key, bytes), Storage.Load(key)
// or Storage.Load(key, entity), and Storage.Delete(key). They ask the machine
// that runs the match to keep, find or drop bytes under a key (tide/storage.h):
// nothing of the match changes when they're called, and what a load finds comes
// back as a Loaded event in a later tick, which is Tide's own declaration
// (storage_prelude). Storage.Flush is local code's, a builtin like any other.
static decl *storage_loaded(const program *prog)
{
    return prelude_decl(prog, STORAGE_PRELUDE_PATH, "Loaded", DECL_EVENT);
}

static bool storage_call(const checker *c, const expr *e)
{
    return visibility_owner(c, e->object, "Storage")
        && (str_eq_c(e->name, "Save") || str_eq_c(e->name, "Load") || str_eq_c(e->name, "Delete"));
}

static type check_storage_static(checker *c, expr *e)
{
    program *prog = c->prog;
    const bool save = str_eq_c(e->name, "Save"), load = str_eq_c(e->name, "Load");
    if (gpu_refuses(c, e->at, "use the server's store", "the store is kept by the machine that runs the match")) return T_ERR;
    refuse_arg_names(e, T_VOID_);
    if (e->type_arg.len) {
        diag_error(e->type_arg_at, "Storage." STR_FMT " takes no type in '<>'", STR_ARG(e->name));
        return T_ERR;
    }
    const int most = save || load ? 2 : 1, least = save ? 2 : 1;
    if (e->args.count < least || e->args.count > most) {
        for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
        diag_error(e->at, save ? "Storage.Save takes a key and the bytes to keep under it: 'Storage.Save(key, bytes)'"
                        : load ? "Storage.Load takes a key, and the entity its Loaded event goes to if not the world: "
                                 "'Storage.Load(key)' or 'Storage.Load(key, entity)'"
                               : "Storage.Delete takes the key to forget: 'Storage.Delete(key)'");
        return T_ERR;
    }
    const type key = {TY_STRING, NULL}, bytes = list_of(prog, (type){TY_BYTE, NULL}), entity = {TY_ENTITY, NULL};
    e->arg_want.count = 0;
    for (int i = 0; i < e->args.count; i++) {
        const type want = i == 0 ? key : save ? bytes : entity;
        check_expr_want(c, e->args.items[i], want);
        vec_push(e->arg_want, want);
    }
    if (c->in_input || (!c->system && !c->method)) {
        diag_error(e->at, "the server's store is used by match code, not input sampling or defaults");
        return T_ERR;
    }
    if (inherits(c)) {
        note_act(c, false, e->at, "uses the server's store", false, false);
    } else if (in_async_function(c)) {
        if (!note_task_side(c, TASK_MATCH, e->at)) return T_ERR;
    } else if (c->method || local_code(c)) {
        diag_error(e->at, "Storage." STR_FMT " is the match's: the machine that runs the match keeps what match code saves",
                   STR_ARG(e->name));
        diag_note("local code keeps this machine's own files with File and Directory; to save something of the match, "
                  "send a command and save in its handler");
        return T_ERR;
    }
    decl *code = reading_code(c);
    if (code) code->calls_c = true; // What it asks keeps its order, as calls of C do
    prog->uses_storage = true;
    e->c_callee = save ? "tide_storage_save" : !load ? "tide_storage_delete" : e->args.count == 2 ? "tide_storage_load_to"
                                                                                                 : "tide_storage_load";
    e->visibility_operation = VISIBILITY_WORLD | VISIBILITY_MUT | VISIBILITY_STORAGE;
    e->call = CALL_VISIBILITY;
    if (load) {
        decl *loaded = storage_loaded(prog);
        if (!loaded) {
            diag_error(e->at, "Storage.Load is written 'Storage.Load(' with nothing between its words");
            return T_ERR;
        }
        // The engine sends Loaded for it: a handler of Loaded has a sender, and one that takes components needs
        // the entity every load names
        e->type_decl = loaded;
        vec_push(c->sends, e);
        if (e->args.count == 1 && !prog->bare_load) {
            prog->bare_load = true;
            prog->bare_load_at = e->at;
        }
    }
    return T_VOID_;
}
