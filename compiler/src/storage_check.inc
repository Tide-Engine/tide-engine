// The server's store in match code: Storage.Save(key, bytes), Storage.Load(key)
// or Storage.Load(key, entity), Storage.Delete(key), and Storage.List(prefix),
// with the key to go on after and the entity its answer goes to if any. They
// ask the machine that runs the match to keep, find, drop or list bytes under
// keys (tide/storage.h): nothing of the match changes when they're called, and
// what a load or a listing finds comes back as a Loaded or Listed event in a
// later tick, which are Tide's own declarations (storage_prelude). A match
// task can await a load or a listing instead: `await Storage.Load(key)` gives
// its bytes, and `await Storage.List(prefix[, after])` a StorageKeys, each
// failing with IOError, in the tick the answer is given to on every machine.
// Storage.Flush is local code's, a builtin like any other.
static decl *storage_loaded(const program *prog)
{
    return prelude_decl(prog, STORAGE_PRELUDE_PATH, "Loaded", DECL_EVENT);
}

static decl *storage_listed(const program *prog)
{
    return prelude_decl(prog, STORAGE_PRELUDE_PATH, "Listed", DECL_EVENT);
}

static decl *storage_keys(const program *prog)
{
    return prelude_decl(prog, STORAGE_PRELUDE_PATH, "StorageKeys", DECL_STRUCT);
}

// An awaited load or listing that names the entity its answer goes to, with the argument at `at`: the answer is the
// task's.
static void storage_names_entity(const loc at, const bool load)
{
    if (load) {
        diag_error(at, "an awaited load's bytes are the task's, so it names no entity: 'var bytes = await Storage.Load(key);'");
        diag_note("without 'await', 'Storage.Load(key, entity);' sends the entity a Loaded event instead");
    } else {
        diag_error(at, "an awaited listing's keys are the task's, so it names no entity: "
                       "'var page = await Storage.List(prefix);' or 'await Storage.List(prefix, after)'");
        diag_note("without 'await', 'Storage.List(prefix, entity);' sends the entity a Listed event instead");
    }
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
    // `await Storage.Load(key)`, `await Storage.List(prefix[, after])`: the answer is the task's value
    const bool awaited = c->awaiting == e;
    if (gpu_refuses(c, e->at, "use the server's store", "the store is kept by the machine that runs the match")) return T_ERR;
    refuse_arg_names(e, T_VOID_);
    if (e->type_arg.len) {
        diag_error(e->type_arg_at, "Storage." STR_FMT " takes no type in '<>'", STR_ARG(e->name));
        return T_ERR;
    }
    if (awaited && !load && !list) {
        for (int i = 0; i < e->args.count; i++) {
            check_expr_want(c, e->args.items[i], i == 0 ? (type){TY_STRING, NULL} : list_of(prog, (type){TY_BYTE, NULL}));
        }
        diag_error(e->at, "Storage." STR_FMT " gives nothing to wait for: call it as a statement, '%s'", STR_ARG(e->name),
                   save ? "Storage.Save(key, bytes);" : "Storage.Delete(key);");
        diag_note("it's carried out once the tick has ended, before what later ticks ask: a load after it finds what it left");
        return T_ERR;
    }
    if (awaited && load && e->args.count == 2) {
        for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
        storage_names_entity(e->args.items[1]->at, true);
        return T_ERR;
    }
    if (awaited && list && e->args.count == 3) {
        for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
        storage_names_entity(e->args.items[2]->at, false);
        return T_ERR;
    }
    const int most = save || load ? 2 : list ? 3 : 1, least = save ? 2 : 1;
    if (awaited && (e->args.count < least || e->args.count > most)) {
        for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
        diag_error(e->at, load ? "an awaited Storage.Load takes the key it loads: 'var bytes = await Storage.Load(key);'"
                               : "an awaited Storage.List takes the prefix of the keys it lists, and the key to go on after "
                                 "if any: 'var page = await Storage.List(prefix);' or 'await Storage.List(prefix, after)'");
        return T_ERR;
    }
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
        if (awaited && !after) {
            (void)check_expr(c, e->args.items[0]);
            storage_names_entity(e->args.items[1]->at, false);
            return T_ERR;
        }
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
        if (list && e->args.count == 2 && i == 1 && !awaited) {
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
        if (c->method->task_side == TASK_LOCAL) { // What it did before made it this machine's
            diag_error(e->at, "Storage." STR_FMT " is the match's, and '" STR_FMT "' is this machine's task: it uses local "
                              "state on line %d", STR_ARG(e->name), STR_ARG(c->method->name), c->method->task_side_at.line);
            diag_note("local code keeps this machine's own files with File and Directory; a match task, which uses no "
                      "local state, can await the store");
            return T_ERR;
        }
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
    if (awaited) {
        // The task waits for its answer, which a later tick is given as input on every machine: the request and
        // the answer are its frame's (codegen.c, gen_stored_await), and no event is sent
        type value = bytes;
        if (list) {
            decl *keys = storage_keys(prog);
            if (!keys) {
                diag_error(e->at, "write 'Storage.List' without a comment between its words: Tide declares StorageKeys for "
                                  "code it finds them in");
                return T_ERR;
            }
            value = decl_type(keys);
        }
        const type result = result_of(prog, value, (type){TY_ENUM, prog->io_error}, false);
        if (load) prog->stored_load = result;
        else prog->stored_list = result;
        prog->uses_text = true;
        if (code) code->awaits_store |= load ? 1u : 2u;
        e->storage_await = true;
        e->c_callee = load ? "tide_storage_load" : "tide_storage_list";
        e->visibility_operation = VISIBILITY_WORLD | VISIBILITY_MUT | VISIBILITY_STORAGE;
        e->call = CALL_VISIBILITY;
        return result;
    }
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
        vec_push(c->send_code, NULL); // (The engine's own event, whichever code asks: check_sends has nothing to say of it)
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
