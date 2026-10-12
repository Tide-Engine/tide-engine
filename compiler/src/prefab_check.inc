// Authored trees are expanded at compile time. Source values are copied before
// checking: each placement and nested instance gets independent entity bindings.
enum { PREFAB_DEPTH = 128, PREFAB_ENTITIES = 1024 };

static str prefab_path(const str prefix, const str name)
{
    sb b = {0};
    if (prefix.len) sb_printf(&b, STR_FMT "%s", STR_ARG(prefix), name.len ? "." : "");
    if (name.len) sb_putn(&b, name.ptr, (size_t)name.len);
    return str_from(b.data ? b.data : "");
}

static expr *prefab_copy_expr(const expr *from, const unit *unit, const str scope, const int self)
{
    if (!from) return NULL;
    expr *e = NEW(expr);
    *e = *from;
    e->authored_unit = unit;
    e->prefab_scope = scope;
    e->prefab_entity = self;
    e->object = prefab_copy_expr(from->object, unit, scope, self);
    e->lhs = prefab_copy_expr(from->lhs, unit, scope, self);
    e->rhs = prefab_copy_expr(from->rhs, unit, scope, self);
    e->cond = prefab_copy_expr(from->cond, unit, scope, self);
    e->args.items = NULL; e->args.count = e->args.cap = 0;
    for (int i = 0; i < from->args.count; i++) vec_push(e->args, prefab_copy_expr(from->args.items[i], unit, scope, self));
    e->inits.items = NULL; e->inits.count = e->inits.cap = 0;
    for (int i = 0; i < from->inits.count; i++) {
        field_init init = from->inits.items[i];
        init.value = prefab_copy_expr(init.value, unit, scope, self);
        init.field = NULL;
        vec_push(e->inits, init);
    }
    return e;
}

static decl *prefab_named(const checker *c, const expr *arg)
{
    str name;
    decl *other;
    const expr *root = chain_root(arg);
    if (!root || root->kind != E_NAME || find_local(c, root->name) || find_param(c, root->name)) return NULL;
    if (!qualified_text(arg, &name)) return NULL;
    decl *d = lookup(c->prog, c->unit, name, NAME_PREFAB, &other);
    return d ? find_named(c, name, arg->at, NAME_PREFAB) : NULL;
}

static int prefab_entity_named(const prefab_instance *p, const str path)
{
    for (int i = 0; i < p->entities.count; i++) if (str_eq(p->entities.items[i].path, path)) return i;
    return -1;
}

static int prefab_append_entity(prefab_instance *p, const str path, const int parent, const loc at)
{
    if (p->entities.count >= PREFAB_ENTITIES) {
        diag_error(at, "a prefab instance has at most %d entities", PREFAB_ENTITIES);
        return -1;
    }
    expr *spawn = NEW(expr);
    spawn->kind = E_CALL;
    spawn->name = str_from("Spawn");
    spawn->call = CALL_SPAWN;
    spawn->at = at;
    const int index = p->entities.count;
    vec_push(p->entities, ((prefab_entity){path, parent, spawn}));
    return index;
}

static void prefab_compose(checker *c, prefab_instance *p, const int index,
                           const prefab_node *node, const str scope, decl **stack, const int depth)
{
    if (depth >= PREFAB_DEPTH) {
        diag_error(node->at, "prefab composition nests at most %d levels", PREFAB_DEPTH);
        return;
    }
    const unit *saved = c->unit;
    c->unit = node->unit;
    if (node->base.len) {
        decl *other;
        decl *base = lookup(c->prog, c->unit, node->base, NAME_PREFAB, &other);
        if (base) base = find_named(c, node->base, node->base_at, NAME_PREFAB);
        ((prefab_node *)node)->base_decl = base;
        if (!base) {
            diag_error(node->at, "unknown nested prefab '" STR_FMT "'", STR_ARG(node->base));
        } else {
            bool cycle = false;
            for (int i = 0; i < depth; i++) cycle |= stack[i] == base;
            if (cycle) diag_error(node->at, "recursive prefab inclusion of '" STR_FMT "'; a prefab cannot contain itself", STR_ARG(base->qualified));
            else {
                stack[depth] = base;
                prefab_compose(c, p, index, base->prefab_root, p->entities.items[index].path, stack, depth + 1);
                stack[depth] = NULL;
            }
        }
    }
    c->unit = node->unit;
    for (int i = 0; i < node->components.count; i++) {
        const expr *written = node->components.items[i];
        decl *component = find_type(c, written->name, written->at);
        if (!component || component->kind != DECL_COMPONENT || component->is_scene) {
            diag_error(written->at, "a prefab entry must name an ordinary component: 'Transform;' or 'Health { value = 100 };'");
            continue;
        }
        // Tooling inspects the authored block as well as its expanded copy.
        ((expr *)written)->type_decl = component;
        for (int j = 0; j < i; j++) {
            decl *other;
            if (lookup(c->prog, node->unit, node->components.items[j]->name, NAME_TYPE, &other) == component) {
                diag_error(written->at, "component '" STR_FMT "' appears twice in this entity block", STR_ARG(component->name));
            }
        }
        for (int f = 0; f < written->inits.count; f++) {
            for (int j = 0; j < f; j++) if (str_eq(written->inits.items[f].name, written->inits.items[j].name)) diag_error(written->inits.items[f].at, "field '" STR_FMT "' is set twice", STR_ARG(written->inits.items[f].name));
        }
        expr *value = prefab_copy_expr(written, node->unit, scope, index);
        value->type_decl = component;
        expr *spawn = p->entities.items[index].spawn;
        expr *existing = NULL;
        for (int j = 0; j < spawn->args.count; j++) if (spawn->args.items[j]->type_decl == component) existing = spawn->args.items[j];
        if (!existing) { vec_push(spawn->args, value); continue; }
        for (int f = 0; f < value->inits.count; f++) {
            field_init init = value->inits.items[f];
            bool replaced = false;
            for (int j = 0; j < existing->inits.count; j++) {
                if (!str_eq(existing->inits.items[j].name, init.name)) continue;
                existing->inits.items[j] = init;
                replaced = true;
                break;
            }
            if (!replaced) vec_push(existing->inits, init);
        }
    }
    for (int i = 0; i < node->children.count; i++) {
        const prefab_node *child = node->children.items[i];
        const str path = prefab_path(p->entities.items[index].path, child->name);
        if (prefab_entity_named(p, path) >= 0) {
            diag_error(child->at, "entity '" STR_FMT "' already exists in this composition; give the child a unique name", STR_ARG(path));
            continue;
        }
        const int added = prefab_append_entity(p, path, index, child->at);
        if (added >= 0) prefab_compose(c, p, added, child, scope, stack, depth + 1);
    }
    c->unit = saved;
}

static void prefab_bind_expr(const checker *c, const prefab_instance *p, expr *e,
                             const bool local, const bool neutral, type expected)
{
    if (!e) return;
    if (is_stored_optional(expected)) expected = expected.decl->fields.items[0].type;
    if (expected.kind == TY_OPTIONAL) expected = wrapped_value(expected);
    const bool reference_local = neutral ? expected.kind == TY_LOCAL_ENTITY : local;
    str name;
    if ((e->kind == E_NAME || e->kind == E_MEMBER) && qualified_text(e, &name)) {
        const int index = prefab_entity_named(p, prefab_path(e->prefab_scope, name));
        if (index >= 0) {
            e->prefab_reference = true;
            e->prefab_entity = index;
            e->type = reference_local ? (type){TY_LOCAL_ENTITY, NULL} : T_ENTITY_;
            return;
        }
    }
    if (e->kind == E_THIS) {
        e->prefab_reference = true;
        e->type = reference_local ? (type){TY_LOCAL_ENTITY, NULL} : T_ENTITY_;
        return;
    }
    prefab_bind_expr(c, p, e->object, local, neutral, T_ERR);
    prefab_bind_expr(c, p, e->lhs, local, neutral, expected);
    prefab_bind_expr(c, p, e->rhs, local, neutral, expected);
    prefab_bind_expr(c, p, e->cond, local, neutral, T_BOOL_);
    type element = expected;
    if (e->kind == E_LIST && expected.decl && (expected.kind == TY_LIST || expected.kind == TY_ARRAY)) element = expected.decl->fields.items[0].type;
    for (int i = 0; i < e->args.count; i++) prefab_bind_expr(c, p, e->args.items[i], local, neutral, element);
    decl *literal = NULL;
    if (e->kind == E_LITERAL) {
        decl *other;
        literal = lookup(c->prog, e->authored_unit ? e->authored_unit : c->unit, e->name, NAME_TYPE, &other);
    }
    for (int i = 0; i < e->inits.count; i++) {
        type field_type = T_ERR;
        for (int f = 0; literal && f < literal->fields.count; f++) if (str_eq(literal->fields.items[f].name, e->inits.items[i].name)) field_type = literal->fields.items[f].type;
        prefab_bind_expr(c, p, e->inits.items[i].value, local, neutral, field_type);
    }
}

static bool prefab_required(const field *f)
{
    for (int i = 0; i < f->attributes.count; i++) if (str_eq_c(f->attributes.items[i].name, "SceneRequired")) return true;
    return false;
}

static void prefab_check_values(checker *c, prefab_instance *p, const bool local, const bool neutral)
{
    const unit *saved = c->unit;
    const bool assets = c->assets_constant;
    c->assets_constant = true;
    for (int i = 0; i < p->entities.count; i++) {
        expr *spawn = p->entities.items[i].spawn;
        spawn->local_world = local;
        for (int j = 0; j < spawn->args.count; j++) {
            expr *value = spawn->args.items[j];
            c->unit = value->authored_unit;
            const decl *d = value->type_decl;
            for (int f = 0; f < value->inits.count; f++) {
                field_init *init = &value->inits.items[f];
                const field *target = NULL;
                for (int k = 0; k < d->fields.count; k++) if (str_eq(d->fields.items[k].name, init->name)) target = &d->fields.items[k];
                type expected = target ? target->type : T_ERR;
                if (is_stored_optional(expected)) expected = expected.decl->fields.items[0].type;
                prefab_bind_expr(c, p, init->value, local, neutral, expected);
                const unit *before = c->unit;
                if (init->value->authored_unit) c->unit = init->value->authored_unit;
                if (!is_constant(c, init->value)) diag_error(init->at, "prefab values must be constant expressions, assets or named entity references");
                c->unit = before;
            }
            check_literal(c, value);
            for (int f = 0; f < d->fields.count; f++) {
                const field *required = &d->fields.items[f];
                if (!prefab_required(required)) continue;
                const expr *effective = required->default_value;
                for (int k = 0; k < value->inits.count; k++) if (str_eq(value->inits.items[k].name, required->name)) effective = value->inits.items[k].value;
                if (!effective || !effective->prefab_reference) {
                    diag_error(value->at, "[SceneRequired] field '" STR_FMT "." STR_FMT "' needs a named entity reference in prefab '" STR_FMT "'",
                               STR_ARG(d->name), STR_ARG(required->name), STR_ARG(p->declaration->qualified));
                }
            }
        }
    }
    c->assets_constant = assets;
    c->unit = saved;
}

static prefab_instance *prefab_expand(checker *c, decl *d)
{
    prefab_instance *p = NEW(prefab_instance);
    p->declaration = d;
    prefab_append_entity(p, (str){0}, -1, d->at);
    decl *stack[PREFAB_DEPTH] = {d};
    prefab_compose(c, p, 0, d->prefab_root, (str){0}, stack, 1);
    return p;
}

static decl *prefab_scene_marker(program *prog, const bool local)
{
    if (prog->prefab_scene[local]) return prog->prefab_scene[local];
    decl *d = NEW(decl);
    d->kind = DECL_COMPONENT;
    d->builtin = d->is_scene = true;
    d->is_local = local;
    d->name = d->qualified = str_from(local ? "tide_local_prefab_scene" : "tide_prefab_scene");
    d->gen_name = str_to_cstr(d->name);
    d->index = prog->components.count;
    if (d->index >= MAX_COMPONENTS) diag_error((loc){1, 1, 0}, "too many components; the limit is %d for now", MAX_COMPONENTS);
    static const char *const names[] = {"tide_visibility", "tide_players"};
    for (int i = 0; i < 2; i++) {
        field f = {0}; f.name = str_from(names[i]); f.type = T_INT_; f.hidden = true;
        vec_push(d->fields, f);
    }
    vec_push(prog->components, d);
    prog->prefab_scene[local] = d;
    return d;
}

static type check_prefab_spawn(checker *c, expr *e, decl *d, const bool load)
{
    const bool local = load ? local_code(c) : e->placement_local;
    if (in_async_function(c)) note_task_side(c, local ? TASK_LOCAL : TASK_MATCH, e->at);
    if (!inherits(c) && !in_async_function(c) && local != local_code(c)) {
        diag_error(e->at, local ? "the match cannot spawn local prefab entities" : "local code cannot change the match; use 'local Spawn(Prefab)' for local entities");
    }
    e->call = load ? CALL_LOAD : CALL_SPAWN;
    e->local_world = local;
    e->args.items[0]->bind = BIND_TYPE;
    e->args.items[0]->type_decl = d;
    if (e->args.items[0]->kind == E_MEMBER) mark_namespaces(e->args.items[0]->object);
    prefab_instance *p = prefab_expand(c, d);
    e->prefab = p;
    p->index = c->prog->prefab_sites.count;
    p->load = load;
    vec_push(c->prog->prefab_sites, e);
    prefab_check_values(c, p, local, false);
    expr *root = p->entities.items[0].spawn;
    for (int i = 1; i < e->args.count; i++) {
        expr *arg = e->args.items[i];
        const str named = i < e->arg_names.count ? e->arg_names.items[i] : (str){0};
        if (load && str_eq_c(named, "visibility")) {
            if (p->visibility || i != e->args.count - 1 || local) {
                diag_error(arg->at, "a match prefab's visibility comes once, last: 'Scene.Load(Level, Settings { ... }, visibility: SceneVisibility.Private)'");
            }
            p->visibility = arg;
            const type t = check_expr(c, arg);
            if (t.kind != TY_ERROR && !(t.kind == TY_ENUM && t.decl == c->prog->scene_visibility)) diag_error(arg->at, "visibility must be SceneVisibility.Public or SceneVisibility.Private");
            continue;
        }
        if (named.len) diag_error(arg->at, "prefab root components are positional arguments; remove the argument name");
        decl *comp = check_component_arg(c, arg, load ? "Scene.Load prefab overrides" : "Spawn prefab overrides");
        if (!comp) continue;
        for (int j = 0; j < p->overrides.count; j++) if (p->overrides.items[j]->type_decl == comp) diag_error(arg->at, "root component '" STR_FMT "' is overridden twice", STR_ARG(comp->name));
        vec_push(p->overrides, arg);
        bool present = false;
        for (int j = 0; j < root->args.count; j++) present |= root->args.items[j]->type_decl == comp;
        if (!present) {
            expr *value = NEW(expr); value->kind = E_LITERAL; value->at = arg->at; value->name = comp->qualified; value->type_decl = comp;
            vec_push(root->args, value);
        }
    }
    e->names_taken = true;
    bool text = false;
    const unit *saved = c->unit;
    for (int i = 0; i < p->entities.count; i++) {
        expr *spawn = p->entities.items[i].spawn;
        for (int j = 0; j < spawn->args.count; j++) {
            expr *arg = spawn->args.items[j];
            decl *comp = component_placement(c->prog, arg->type_decl, local, arg->at);
            if (!comp) continue;
            arg->type_decl = comp; arg->type = decl_type(comp);
            if (!check_component_side(c, arg, comp)) continue;
            spawn->spawn_mask = mask_or(spawn->spawn_mask, bit(comp));
            text |= decl_holds_text(comp);
        }
        c->prog->spawned_mask = mask_or(c->prog->spawned_mask, spawn->spawn_mask);
        vec_push(c->spawns, spawn);
    }
    c->unit = saved;
    if (load) {
        decl *marker = prefab_scene_marker(c->prog, local);
        root->spawn_mask = mask_or(root->spawn_mask, bit(marker));
        e->type_decl = marker;
    }
    if (p->entities.count > 1) ensure_hierarchy(c->prog, local);
    e->spawn_mask = root->spawn_mask;
    vec_push(c->spawns, e);
    if (inherits(c)) note_act(c, local, e->at, local ? "spawns local prefab entities" : "spawns prefab entities", true, text);
    note_text_write(c, text);
    note_spawn(c);
    return local ? (type){TY_LOCAL_ENTITY, NULL} : T_ENTITY_;
}
