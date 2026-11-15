// The server's store in match code: Storage.Save(key, bytes), Storage.Load(key)
// or Storage.Load(key, entity), Storage.Delete(key), and Storage.List(prefix),
// with the key to go on after and the entity its answer goes to if any. They
// ask the machine that runs the match to keep, find, drop or list bytes under
// keys (tide/storage.h): nothing of the match changes when they're called, and
// what a load or a listing finds comes back as a Loaded or Listed event in a
// later tick, which are Tide's own declarations (storage_prelude).
// Storage.Flush is local code's, a builtin like any other.
static decl *storage_loaded(const program *prog)
{
    return prelude_decl(prog, STORAGE_PRELUDE_PATH, "Loaded", DECL_EVENT);
}

static decl *storage_listed(const program *prog)
{
    return prelude_decl(prog, STORAGE_PRELUDE_PATH, "Listed", DECL_EVENT);
}

static bool storage_call(const checker *c, const expr *e)
{
    return visibility_owner(c, e->object, "Storage")
        && (str_eq_c(e->name, "Save") || str_eq_c(e->name, "Load") || str_eq_c(e->name, "Delete") || str_eq_c(e->name, "List"));
}

static type check_storage_static(checker *c, expr *e)
{
    program *prog = c->prog;
    const bool save = str_eq_c(e->name, "Save"), load = str_eq_c(e->name, "Load"), list = str_eq_c(e->name, "List");
    if (gpu_refuses(c, e->at, "use the server's store", "the store is kept by the machine that runs the match")) return T_ERR;
    refuse_arg_names(e, T_VOID_);
    if (e->type_arg.len) {
        diag_error(e->type_arg_at, "Storage." STR_FMT " takes no type in '<>'", STR_ARG(e->name));
        return T_ERR;
    }
    const int most = save || load ? 2 : list ? 3 : 1, least = save ? 2 : 1;
    if (e->args.count < least || e->args.count > most) {
        for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
        diag_error(e->at, save ? "Storage.Save takes a key and the bytes to keep under it: 'Storage.Save(key, bytes)'"
                        : load ? "Storage.Load takes a key, and the entity its Loaded event goes to if not the world: "
                                 "'Storage.Load(key)' or 'Storage.Load(key, entity)'"
                        : list ? "Storage.List takes the prefix of the keys it lists, the key to go on after if any, and "
                                 "the entity its Listed event goes to if not the world: 'Storage.List(prefix)', "
                                 "'Storage.List(prefix, after)', 'Storage.List(prefix, entity)' or "
                                 "'Storage.List(prefix, after, entity)'"
                               : "Storage.Delete takes the key to forget: 'Storage.Delete(key)'");
        return T_ERR;
    }
    const type key = {TY_STRING, NULL}, bytes = list_of(prog, (type){TY_BYTE, NULL}), entity = {TY_ENTITY, NULL};
    // A listing's second argument is the key it goes on after, or the entity its answer goes to: which, its type says
    bool after = list && e->args.count == 3;
    if (list && e->args.count == 2) {
        const type second = check_expr(c, e->args.items[1]);
        after = second.kind != TY_ENTITY;
    }
    e->arg_want.count = 0;
    bool typed = true;
    for (int i = 0; i < e->args.count; i++) {
        const type want = i == 0 ? key : save ? bytes : list && i == 1 && after ? key : entity;
        const type got = list && e->args.count == 2 && i == 1 ? check_expr_want_checked(c, e->args.items[i], want, true)
                                                               : check_expr_want(c, e->args.items[i], want);
        vec_push(e->arg_want, want);
        if (got.kind == TY_ERROR || same_type(got, want)) continue;
        const char *name = i == 0 ? (list ? "prefix" : "key") : save ? "bytes" : list && i == 1 && after ? "after" : "entity";
        diag_error(e->args.items[i]->at, "Storage." STR_FMT " takes its %s as %s, not %s", STR_ARG(e->name), name,
                   type_name(want), type_name(got));
        if (list && e->args.count == 2 && i == 1) {
            diag_note("a listing's second argument is the key it goes on after (a string) or the entity its Listed goes to");
        }
        typed = false;
    }
    if (!typed) return T_ERR;
    if (c->in_input || (!c->system && !c->method)) {
        diag_error(e->at, "the server's store is used by match code, not input sampling or defaults");
        return T_ERR;
    }
    if (inherits(c)) {
        note_act(c, false, e->at, "uses the server's store", false, true);
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
    // ...on one thread, in its rows' order, and after (or before) every other system that asks, as the world's one
    // list of requests is changed as a write to the match's own heap is: a system that writes that heap keeps its
    // rows in order, and two that do wait for each other
    note_text_write(c, true, e->at, NULL, true);
    if (!prog->uses_storage) prog->storage_at = e->at;
    prog->uses_storage = true;
    const bool targeted = e->args.count == (list ? (after ? 3 : 2) : 2);
    e->c_callee = save ? "tide_storage_save"
                : load ? (targeted ? "tide_storage_load_to" : "tide_storage_load")
                : list ? (after ? (targeted ? "tide_storage_list_after_to" : "tide_storage_list_after")
                                : (targeted ? "tide_storage_list_to" : "tide_storage_list"))
                       : "tide_storage_delete";
    e->visibility_operation = VISIBILITY_WORLD | VISIBILITY_MUT | VISIBILITY_STORAGE;
    e->call = CALL_VISIBILITY;
    if (load || list) {
        decl *answer = load ? storage_loaded(prog) : storage_listed(prog);
        if (!answer) {
            // Tide declares Loaded and Listed where a file's code has the words Storage, '.' and Load or List
            // (storage_prelude_wanted)
            diag_error(e->at, "write 'Storage." STR_FMT "' without a comment between its words: Tide declares the %s event "
                              "for code it finds them in", STR_ARG(e->name), load ? "Loaded" : "Listed");
            return T_ERR;
        }
        // The engine sends it: a handler of Loaded or Listed has a sender, and one that takes components needs the
        // entity every load or listing names
        e->type_decl = answer;
        vec_push(c->sends, e);
        if (!targeted && load && !prog->bare_load) {
            prog->bare_load = true;
            prog->bare_load_at = e->at;
        }
        if (!targeted && list && !prog->bare_list) {
            prog->bare_list = true;
            prog->bare_list_at = e->at;
        }
    }
    return T_VOID_;
}
