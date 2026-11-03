// Build code (docs/spec.md, Build code): functions that run while the game builds, and the assets and constants
// they make. An asset's or a constant's code is a function of its own, made here and then checked and generated
// as any other: `void TideBuild_<name>(mut T <name>) { ... }`, T the value's editable form (build_editable). What
// it leaves in its parameter is what the game is built with (build_code.c runs it).

// A type as if it were written: `name<arg>`.
static type_ref *build_type_ref(const char *name, type_ref *arg, const loc at)
{
    type_ref *r = NEW(type_ref);
    sb text = {0};
    if (arg) sb_printf(&text, "%s<" STR_FMT ">", name, STR_ARG(arg->text));
    else sb_printf(&text, "%s", name);
    r->name = str_from(name);
    r->text = str_from(text.data);
    r->at = r->name_at = at;
    if (arg) vec_push(r->args, arg);
    return r;
}

// The function that makes `owner`'s value, with `body` as its code. It's in the owner's namespace, and named so
// no code can name it.
static decl *build_function(checker *c, decl *owner, type_ref *editable, stmt *body)
{
    decl *fn = NEW(decl);
    fn->kind = DECL_FUNCTION;
    fn->unit = owner->unit;
    fn->at = owner->at;
    fn->end = owner->end;
    sb name = {0};
    sb_printf(&name, "TideBuild_" STR_FMT, STR_ARG(owner->name));
    fn->name = str_from(name.data);
    const str ns = decl_ns(owner);
    if (ns.len) {
        sb qualified = {0};
        sb_printf(&qualified, STR_FMT "." STR_FMT, STR_ARG(ns), STR_ARG(fn->name));
        fn->qualified = str_from(qualified.data);
    } else fn->qualified = fn->name;
    sb cname = {0};
    c_name_of(fn, &cname);
    fn->c_name = cname.data;
    fn->return_type_ref = build_type_ref("void", NULL, owner->at);
    fn->return_type_name = fn->return_type_ref->text;
    fn->return_type_at = fn->return_type_qual_at = owner->at;
    fn->is_build = true;
    fn->build_at = owner->at;
    fn->build_owner = owner;
    param p = {0};
    p.mode = PARAM_MUT;
    p.function_param = true;
    p.name = owner->name;
    p.type_ref = editable;
    p.type_name = editable->text;
    p.at = p.name_at = p.type_at = p.type_qual_at = owner->at;
    vec_push(fn->params, p);
    if (owner->kind == DECL_ASSET && owner->asset_kind == ASSET_MESH) { // ...and its levels of detail, which it can make
        param levels = p;
        levels.name = str_from("levels");
        levels.type_ref = build_type_ref("List", build_type_ref("MeshLevel", NULL, owner->at), owner->at);
        levels.type_name = levels.type_ref->text;
        vec_push(fn->params, levels);
    }
    fn->body = body;
    fn->body_at = owner->body_at.line ? owner->body_at : owner->at;
    owner->build_fn = fn;
    vec_push(c->prog->decls, fn);
    vec_push(c->prog->build_roots, owner);
    return fn;
}

// A constant worked out by code: one with a body, or whose value isn't a constant expression, like a call to a
// function (`const List<float> CURVE = Bake(64);`). Its value is the build's, so only what reads values as the
// game runs can read it, as for an asset.
// Whether `e` calls something: what makes a constant's value build code, rather than a mistake.
static bool build_calls(const expr *e)
{
    if (!e) return false;
    if (e->kind == E_CALL || e->kind == E_METHOD) return true;
    if (build_calls(e->object) || build_calls(e->lhs) || build_calls(e->rhs) || build_calls(e->cond)) return true;
    for (int i = 0; i < e->args.count; i++) {
        if (build_calls(e->args.items[i])) return true;
    }
    for (int i = 0; i < e->inits.count; i++) {
        if (build_calls(e->inits.items[i].value)) return true;
    }
    return false;
}

static bool build_constant(const checker *c, const decl *k)
{
    static int depth;
    if (k->build_fn || k->body) return true;
    if (!k->value || depth > 64 || !build_calls(k->value)) return false;
    checker scoped = *c;
    scoped.unit = k->unit;
    scoped.assets_constant = false;
    depth++;
    const bool built = !is_constant(&scoped, k->value);
    depth--;
    return built;
}

// const T NAME = <not a constant>;: its value, assigned to its parameter by the function that makes it.
static void build_constant_function(checker *c, decl *k)
{
    stmt *body = k->body;
    if (!body) {
        stmt *assign = NEW(stmt);
        assign->kind = S_ASSIGN;
        assign->at = k->value->at;
        assign->op = T_ASSIGN;
        expr *target = NEW(expr);
        target->kind = E_NAME;
        target->at = k->at;
        target->name = k->name;
        assign->target = target;
        assign->value = k->value;
        body = NEW(stmt);
        body->kind = S_BLOCK;
        body->at = k->value->at;
        body->end = k->end;
        vec_push(body->stmts, assign);
    }
    type_ref *r = (type_ref *)type_ref_or_text(k->return_type_ref, k->return_type_name);
    build_function(c, k, r, body);
}

// What a built constant can be: what any constant can, and lists of plain values (`List<float>`), which the pack
// holds. Not maps, sets, grids or arrays, nor a list of lists or text.
static bool build_constant_type_ok(const type t, const loc at)
{
    if (t.kind == TY_LIST) {
        const type element = t.decl->fields.items[0].type;
        if (holds_heap(element) || holds_render_resource(element) || holds_callable(element)
            || element.kind == TY_ENTITY || element.kind == TY_LOCAL_ENTITY || element.kind == TY_MESH) {
            diag_error(at, "a constant list holds plain values: numbers, vectors, colors, enums and structs of those, not %s",
                       type_name(element));
            return false;
        }
        return true;
    }
    return true;
}

// The form an asset's code edits: what it is, but for a Texture, whose code edits its pixels (Grid2<Color>), and a
// Clip, whose code edits its samples (ClipData). NULL for one that code can't make yet, which `why` says.
static type_ref *build_editable(const decl *k, const char **why)
{
    *why = NULL;
    if (k->asset_many) {
        *why = "a list or a map of files can't have code yet: build code makes one file's value, so give the files it "
               "edits a declaration each";
        return NULL;
    }
    switch (k->asset_kind) {
    case ASSET_TEXT: case ASSET_BYTES: case ASSET_GRID: case ASSET_MESH:
        return (type_ref *)type_ref_or_text(k->return_type_ref, k->return_type_name);
    case ASSET_TEXTURE: return build_type_ref("Grid2", build_type_ref("Color", NULL, k->at), k->at);
    case ASSET_CLIP: return build_type_ref("ClipData", NULL, k->at);
    case ASSET_CUBEMAP: *why = "a Cubemap can't have code yet: build code makes text, bytes, images, textures, meshes and clips"; return NULL;
    case ASSET_SKINNED: *why = "a SkinnedMesh can't have code yet: build code makes text, bytes, images, textures, meshes and clips"; return NULL;
    default: *why = "an animation can't have code yet: build code makes text, bytes, images, textures, meshes and clips"; return NULL;
    }
}

// A built-in struct only an asset's code uses, ClipData or MeshLevel: a declaration of the program's, with its fields'
// types, once a game's code needs it. Only then, so no other game's declarations, lists or names are numbered
// differently.
static void build_struct_ready(checker *c, decl *d)
{
    if (d->named) return;
    d->qualified = d->name;
    sb cname = {0};
    c_name_of(d, &cname);
    d->c_name = cname.data;
    vec_push(c->prog->decls, d);
    checker scoped = *c;
    scoped.unit = d->unit;
    resolve_field_types(&scoped, d);
    d->named = true;
}

// An asset with code: the function that makes it, from the file (if it names one) as its code starts.
static void build_asset_function(checker *c, decl *k)
{
    const char *why;
    type_ref *editable = build_editable(k, &why);
    if (!editable) {
        diag_error(k->body_at, "%s", why);
        diag_note("what else it can make is open: docs/spec.md, Build code");
        return;
    }
    if (k->asset_kind == ASSET_CLIP) build_struct_ready(c, c->prog->clip_data);
    if (k->asset_kind == ASSET_MESH) build_struct_ready(c, c->prog->mesh_level);
    build_function(c, k, editable, k->body);
}

// Why `d` can't run while the game builds, or NULL: what it does that needs a running game.
static const char *build_effect(const decl *d)
{
    if (d->draws || d->plays || d->frame_time || d->frame_devices || d->frame_cursor)
        return "draw, play audio or read the frame: those are a running game's";
    if (d->uses_io) return "access files or the network: build code reads files through assets, which the build names";
    if (d->world_local || d->world_match || d->uses_this || d->world_uses.count)
        return "read or change a world: it runs before there's a game";
    if (d->acts[0] || d->acts[1]) return "send events, spawn or change entities: it runs before there's a game";
    if (d->starts_tasks || d->is_async || d->uses_workers) return "start or wait for tasks";
    if (d->rolls || d->reads_seed) return "roll the match's random numbers: use Math.Hash, or a Random it makes with a seed";
    for (int i = 0; i < DEVICE_WORDS; i++) {
        if (d->device_uses[i]) return "read devices";
    }
    if (d->takes_action || d->borrows_queries) return "borrow queries or run actions";
    if (d->gpu_sampling || (d->gpu_only && !d->is_build) || d->is_audio) return "run GPU or audio code";
    if (d->is_extern) return "call C yet: the program that runs build code is built from Tide code alone";
    return NULL;
}

static bool build_known(const decl *const *seen, const int count, const decl *d)
{
    for (int i = 0; i < count; i++) {
        if (seen[i] == d) return true;
    }
    return false;
}

// Every build function, and all it calls, can run while the game builds; and only build code calls a `build`
// function, which runs nowhere else.
static void check_build_code(const checker *c)
{
    for (int k = 0; k < c->calls.count; k++) {
        const call_site *site = &c->calls.items[k];
        if (!site->to->is_build || site->to->build_owner || (site->from && site->from->is_build)) continue;
        const loc at = site->call ? site->call->at : site->to->at;
        diag_error(at, "'" STR_FMT "' is a build function: it runs only while the game builds, so only build code calls it",
                   STR_ARG(site->to->name));
        diag_note("call it from an asset's or a constant's code, or from another build function, like 'const " STR_FMT
                  " VALUE = " STR_FMT "(...);'", STR_ARG(site->to->return_type_name), STR_ARG(site->to->name));
    }
    for (int i = 0; i < c->prog->decls.count; i++) {
        const decl *root = c->prog->decls.items[i];
        if (!root->is_build || root->kind != DECL_FUNCTION) continue;
        VEC(const decl *) seen = {0};
        vec_push(seen, root);
        for (int at = 0; at < seen.count; at++) {
            const decl *d = seen.items[at];
            const char *effect = build_effect(d);
            if (effect) {
                const decl *owner = root->build_owner;
                if (owner) diag_error(owner->at, "%s '" STR_FMT "' is made by build code, which can't %s",
                                      owner->kind == DECL_ASSET ? "asset" : "constant", STR_ARG(owner->name), effect);
                else diag_error(root->build_at, "build function '" STR_FMT "' can't %s", STR_ARG(root->name), effect);
                if (d != root) diag_note_at(d->at, "it calls '" STR_FMT "', which does", STR_ARG(d->name));
                break;
            }
            for (int k = 0; k < c->calls.count; k++) {
                const call_site *site = &c->calls.items[k];
                if (site->from == d && !build_known(seen.items, seen.count, site->to)) vec_push(seen, site->to);
            }
        }
    }
}
