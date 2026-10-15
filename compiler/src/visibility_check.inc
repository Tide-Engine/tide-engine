// Visibility policies are match values. Only group-directory changes need an
// implicit match capability; changing a Visibility value uses ordinary `mut`.
static bool visibility_owner(const checker *c, const expr *object, const char *name)
{
    return object->kind == E_NAME && str_eq_c(object->name, name)
        && !find_local(c, object->name) && !find_param(c, object->name)
        && !field_in_scope(c, object->name);
}

static bool visibility_typed_receiver(checker *c, const expr *object)
{
    if (object->kind != E_NAME) return false;
    const stmt *local = find_local(c, object->name);
    const param *parameter = local ? NULL : find_param(c, object->name);
    const type value = local ? local->type : parameter ? parameter->type : T_ERR;
    return value.kind == TY_COMPONENT && value.decl == c->prog->visibility;
}

static bool visibility_match(checker *c, const expr *e)
{
    if (c->in_input || (!c->system && !c->method)) {
        diag_error(e->at, "visibility groups are changed by match code, not input sampling or defaults");
        return false;
    }
    if (inherits(c)) {
        note_act(c, false, e->at, "changes visibility groups", false, true);
    } else if (in_async_function(c)) {
        if (!note_task_side(c, TASK_MATCH, e->at)) return false;
    } else if (c->method || local_code(c)) {
        diag_error(e->at, "visibility groups are the match's to change; local code only reads permitted state");
        return false;
    }
    note_text_write(c, true); // The shared directory changes the world's heap.
    return true;
}

static type check_visibility_static(checker *c, expr *e)
{
    program *prog = c->prog;
    const bool audience = visibility_owner(c, e->object, "Audience");
    ensure_visibility(prog);
    if (gpu_refuses(c, e->at, "work with visibility", "visibility is match networking state")) return T_ERR;
    if (e->type_arg.len) {
        diag_error(e->type_arg_at, "this visibility operation takes no type in '<>'");
        return T_ERR;
    }
    const type audiences = {TY_STRUCT, prog->audience}, groups = {TY_STRUCT, prog->visibility_group};
    type result = T_VOID_;
    int count = 0;
    type first = T_ERR, second = T_ERR;
    if (audience && str_eq_c(e->name, "Only")) {
        count = 1; first = (type){TY_PLAYER, NULL}; result = audiences;
        e->c_callee = "tide_audience_only";
    } else if (audience && str_eq_c(e->name, "From")) {
        count = 1; first = list_of(prog, (type){TY_PLAYER, NULL}); result = audiences;
        e->c_callee = "tide_audience_from";
    } else if (!audience && str_eq_c(e->name, "CreateGroup")) {
        count = 1; first = audiences; result = groups;
        e->c_callee = "tide_visibility_create_group";
    } else if (!audience && str_eq_c(e->name, "Set")) {
        count = 2; first = groups; second = audiences;
        e->c_callee = "tide_visibility_set_group";
    } else if (!audience && str_eq_c(e->name, "DestroyGroup")) {
        count = 1; first = groups;
        e->c_callee = "tide_visibility_destroy_group";
    } else {
        diag_error(e->at, audience ? "Audience has All, None, Only(player) and From(players)"
                                  : "Visibility has CreateGroup(audience), Set(group, audience) and DestroyGroup(group)");
        for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
        return T_ERR;
    }
    refuse_arg_names(e, result);
    if (e->args.count != count) {
        for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
        diag_error(e->at, "'" STR_FMT "' takes %d argument%s", STR_ARG(e->name), count, count == 1 ? "" : "s");
        return T_ERR;
    }
    e->arg_want.count = 0;
    for (int i = 0; i < count; i++) {
        const type want = i ? second : first;
        check_expr_want(c, e->args.items[i], want);
        vec_push(e->arg_want, want);
    }
    if (!audience && !visibility_match(c, e)) return T_ERR;
    e->visibility_operation = audience ? 0 : VISIBILITY_WORLD | VISIBILITY_MUT;
    e->call = CALL_VISIBILITY;
    return result;
}

static type check_visibility_method(checker *c, expr *e, const type object)
{
    program *prog = c->prog;
    ensure_visibility(prog);
    if (gpu_refuses(c, e->at, "work with visibility", "visibility is match networking state")) return T_ERR;
    refuse_arg_names(e, T_VOID_);
    const bool audience = object.decl == prog->audience;
    const bool reset = str_eq_c(e->name, "Reset");
    const bool contains = str_eq_c(e->name, "Contains");
    e->visibility_operation = VISIBILITY_RECEIVER;
    e->arg_want.count = 0;
    if (audience) {
        const bool add = str_eq_c(e->name, "Add"), remove = str_eq_c(e->name, "Remove");
        if ((!add && !remove && !contains) || e->type_arg.len || e->args.count != 1) {
            diag_error(e->at, "an Audience has Add(player), Remove(player) and Contains(player)");
            for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
            return T_ERR;
        }
        const type player = {TY_PLAYER, NULL};
        check_expr_want(c, e->args.items[0], player);
        vec_push(e->arg_want, player);
        e->c_callee = add ? "tide_audience_add" : remove ? "tide_audience_remove" : "tide_audience_contains";
    } else {
        if ((!reset && !str_eq_c(e->name, "Set")) || !e->type_arg.len || e->args.count != (reset ? 0 : 1)) {
            diag_error(e->at, "Visibility restricts a whole component: Set<Inventory>(group or audience), or Reset<Inventory>()");
            for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
            return T_ERR;
        }
        decl *component = find_type(c, e->type_arg, e->type_arg_at);
        if (!component || component->kind != DECL_COMPONENT || component->is_scene || component == prog->visibility) {
            diag_error(e->type_arg_at, "a visibility override names an ordinary component, not a scene or Visibility");
            return T_ERR;
        }
        if (component->is_local) {
            diag_error(e->type_arg_at, "visibility restricts match components; local components already belong to this machine");
            return T_ERR;
        }
        component_placement(prog, component, false, e->type_arg_at);
        e->type_decl = component;
        if (builds(prog)) component->visibility_optional = true;
        e->visibility_operation |= VISIBILITY_COMPONENT;
        if (!reset) {
            const type value = check_expr(c, e->args.items[0]);
            if (value.kind != TY_ERROR && !(value.kind == TY_STRUCT && (value.decl == prog->audience || value.decl == prog->visibility_group))) {
                diag_error(e->args.items[0]->at, "a component's visibility is an Audience or a VisibilityGroup, not %s", type_name(value));
                return T_ERR;
            }
            vec_push(e->arg_want, value);
            e->c_callee = value.decl == prog->audience ? "tide_visibility_set_audience" : "tide_visibility_set_component_group";
        } else e->c_callee = "tide_visibility_reset";
    }
    if (!contains) {
        if (!check_writable(c, e->object, NULL, NULL)) return T_ERR;
        e->visibility_operation |= VISIBILITY_MUT;
        if (!audience) note_text_write(c, true);
    }
    e->call = CALL_VISIBILITY;
    return contains ? T_BOOL_ : T_VOID_;
}
