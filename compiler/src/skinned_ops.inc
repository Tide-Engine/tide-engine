// Included by check.c once expression, argument and writable checks exist.

static bool skinned_cpu(checker *c, const expr *e)
{
    if (!gpu_code(c)) return true;
    gpu_refuses(c, e->at, "read skeletons or sample animations",
                "a system or view samples a Pose on the CPU, and Draw.SkinnedMesh applies it before the user vertex stage");
    return false;
}

static type check_skinned_member(checker *c, expr *e, const type obj, bool *handled)
{
    *handled = obj.kind == TY_STRUCT && obj.decl && obj.decl->skin_kind;
    if (!*handled) return T_ERR;
    decl *d = obj.decl;
    if (d->skin_kind == SKIN_TRANSFORM) {
        *handled = false; // Visible local TRS fields use ordinary field checking.
        return T_ERR;
    }
    if (!skinned_cpu(c, e)) return T_ERR;
    if (d->skin_kind == SKIN_MESH) {
        if (str_eq_c(e->member, "vertices") || str_eq_c(e->member, "indices")) {
            *handled = false; // Ordinary visible fields, with immutable access checked at a write.
            return T_ERR;
        }
        if (str_eq_c(e->member, "animations")) {
            e->skin_operation = SKIN_OP_ANIMATIONS;
            return decl_type(d->skin_animations);
        }
        if (str_eq_c(e->member, "skeleton")) {
            e->skin_operation = SKIN_OP_SKELETON;
            return decl_type(d->skin_skeleton);
        }
        if (str_eq_c(e->member, "defaultPose")) {
            e->skin_operation = SKIN_OP_REST_POSE;
            return decl_type(c->prog->pose);
        }
    }
    if (d->skin_kind == SKIN_SKELETON && str_eq_c(e->member, "bones")) {
        e->skin_operation = SKIN_OP_BONES;
        return decl_type(d->skin_bones);
    }
    if (d->skin_kind == SKIN_ANIMATION) {
        if (str_eq_c(e->member, "name")) {
            e->skin_operation = SKIN_OP_ANIMATION_NAME;
            return (type){TY_STRING, NULL};
        }
        if (str_eq_c(e->member, "duration")) {
            e->skin_operation = SKIN_OP_ANIMATION_DURATION;
            return T_FLOAT_;
        }
    }
    const bool animations = d->skin_kind == SKIN_ANIMATIONS;
    const bool bones = d->skin_kind == SKIN_BONES;
    if (animations || bones) {
        if (str_eq_c(e->member, "count")) {
            e->skin_operation = animations ? SKIN_OP_ANIMATION_COUNT : SKIN_OP_BONE_COUNT;
            return T_INT_;
        }
        for (int i = 0; i < d->skin_catalog.count; i++) {
            const skinned_member *m = &d->skin_catalog.items[i];
            if (!str_eq(m->name, e->member)) continue;
            e->skin_operation = animations ? SKIN_OP_ANIMATION_MEMBER : SKIN_OP_BONE_MEMBER;
            return decl_type(animations ? c->prog->animation : c->prog->bone);
        }
        if (d->skin_metadata_status < 0) {
            diag_error(e->at, "%s catalog metadata is %s: cannot check member '" STR_FMT "'",
                       animations ? "animation" : "bone", d->skin_metadata_status == -2 ? "invalid" : "unavailable", STR_ARG(e->member));
        } else {
            diag_error(e->at, "%s has no checked member '" STR_FMT "'", type_name(obj), STR_ARG(e->member));
        }
        suggestion sg = suggest_start(e->member);
        for (int i = 0; i < d->skin_catalog.count; i++) suggest_consider(&sg, d->skin_catalog.items[i].name);
        suggest_note(&sg);
        diag_note("Find(name) looks up an exact original name dynamically; a SkinnedMesh contract guarantees named members through helpers");
        return T_ERR;
    }
    diag_error(e->at, "%s has no member '" STR_FMT "'", type_name(obj), STR_ARG(e->member));
    return T_ERR;
}

static bool skinned_arguments(checker *c, expr *e, const char *names, const int optional,
                               const type *types, const int count)
{
    if (!order_named_args(e, names, optional)) return false;
    if (e->args.count < count - optional || e->args.count > count) {
        if (optional) diag_error(e->at, "'" STR_FMT "' takes %d or %d arguments, not %d", STR_ARG(e->name), count - optional, count, e->args.count);
        else diag_error(e->at, "'" STR_FMT "' takes %d argument%s, not %d", STR_ARG(e->name), count, count == 1 ? "" : "s", e->args.count);
        for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
        return false;
    }
    bool valid = true;
    // A named call retains source evaluation order, including out locals in a rejected argument.
    const int written = e->args.count;
    for (int i = 0; i < written; i++) {
        const int k = e->arg_order.count ? e->arg_order.items[i] : i;
        const type got = check_expr_want(c, e->args.items[k], types[k]);
        if (got.kind == TY_ERROR) valid = false;
        else if (!type_assignable(types[k], got)) {
            diag_error(e->args.items[k]->at, "'" STR_FMT "' argument %d is %s, not %s", STR_ARG(e->name), k + 1, type_name(types[k]), type_name(got));
            valid = false;
        }
    }
    if (valid && e->args.count < count) {
        if (!e->settled) {
            e->settled = true;
            e->written.items = e->args.items;
            e->written.count = e->written.cap = e->args.count;
        }
        expr *clamp = NEW(expr);
        clamp->kind = E_INT;
        clamp->type = types[count - 1];
        clamp->at = e->at;
        clamp->int_value = 0;
        vec_push(e->args, clamp);
    }
    return valid;
}

static type check_skinned_static(checker *c, expr *e, bool *handled)
{
    const expr *receiver = e->object;
    *handled = receiver && receiver->kind == E_NAME && receiver->type_arg_ref
        && str_eq_c(receiver->type_arg_ref->name, "SkinnedMesh");
    if (!*handled) return T_ERR;
    if (!skinned_cpu(c, e)) return T_ERR;
    const type model = resolve_skinned_mesh(c, receiver->type_arg_ref, receiver->at);
    if (model.kind == TY_ERROR) return T_ERR;
    if (!str_eq_c(e->name, "Decode")) {
        for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
        diag_error(e->at, "%s has the static method Decode(bytes), not '" STR_FMT "'", type_name(model), STR_ARG(e->name));
        return T_ERR;
    }
    if (!mesh_vertex_ok(model.decl->skin_vertex.decl, e->at)) return T_ERR;
    const type bytes = list_of(c->prog, (type){TY_BYTE, NULL});
    if (!skinned_arguments(c, e, "bytes", 0, &bytes, 1)) return T_ERR;
    e->object->type = model;
    e->object->type_decl = model.decl;
    e->object->bind = BIND_TYPE;
    e->call = CALL_SKINNED;
    e->skin_operation = SKIN_OP_DECODE;
    e->type_decl = model.decl;
    return result_of(c->prog, model, (type){TY_ENUM, c->prog->io_error}, false);
}

static type check_skinned_method(checker *c, expr *e, const type obj, bool *handled)
{
    *handled = obj.kind == TY_STRUCT && obj.decl && obj.decl->skin_kind;
    if (!*handled) return T_ERR;
    if (!skinned_cpu(c, e)) return T_ERR;
    const skinned_kind kind = obj.decl->skin_kind;
    const bool animations = kind == SKIN_ANIMATIONS;
    const bool bones = kind == SKIN_BONES || kind == SKIN_SKELETON;
    type args[3] = {{TY_ERROR, NULL}, {TY_ERROR, NULL}, {TY_ERROR, NULL}};
    type result = T_ERR;
    const char *names = "";
    int count = 0, optional = 0;
    bool changes = false;
    if ((animations || bones) && str_eq_c(e->name, "Find")) {
        e->skin_operation = animations ? SKIN_OP_FIND_ANIMATION : SKIN_OP_FIND_BONE;
        args[0] = (type){TY_STRING, NULL};
        names = "name";
        count = 1;
        result = optional_type(c, decl_type(animations ? c->prog->animation : c->prog->bone), e->at);
    } else if ((animations || kind == SKIN_BONES) && str_eq_c(e->name, "At")) {
        e->skin_operation = animations ? SKIN_OP_AT_ANIMATION : SKIN_OP_AT_BONE;
        args[0] = T_INT_;
        names = "index";
        count = 1;
        result = optional_type(c, decl_type(animations ? c->prog->animation : c->prog->bone), e->at);
    } else if (kind == SKIN_SKELETON && str_eq_c(e->name, "RestPose")) {
        e->skin_operation = SKIN_OP_REST_POSE;
        result = decl_type(c->prog->pose);
    } else if ((kind == SKIN_ANIMATION || kind == SKIN_POSE) && str_eq_c(e->name, "Sample")) {
        const bool pose = kind == SKIN_POSE;
        e->skin_operation = pose ? SKIN_OP_POSE_SAMPLE : SKIN_OP_ANIMATION_SAMPLE;
        count = pose ? 3 : 2;
        if (pose) args[0] = decl_type(c->prog->animation);
        args[count - 2] = T_FLOAT_;
        args[count - 1] = decl_type(c->prog->playback);
        names = pose ? "animation, seconds, playback" : "seconds, playback";
        optional = 1;
        changes = pose;
        result = pose ? T_VOID_ : decl_type(c->prog->pose);
    } else if (kind == SKIN_POSE && str_eq_c(e->name, "Blend")) {
        e->skin_operation = SKIN_OP_POSE_BLEND;
        args[0] = decl_type(c->prog->pose);
        args[1] = T_FLOAT_;
        names = "other, weight";
        count = 2;
        changes = true;
        result = T_VOID_;
    } else if (kind == SKIN_POSE && str_eq_c(e->name, "Model")) {
        e->skin_operation = SKIN_OP_POSE_MODEL;
        args[0] = decl_type(c->prog->bone);
        names = "bone";
        count = 1;
        result = (type){TY_FLOAT4X4, NULL};
    } else {
        for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
        diag_error(e->at, "%s has no method '" STR_FMT "'", type_name(obj), STR_ARG(e->name));
        return T_ERR;
    }
    if (!skinned_arguments(c, e, names, optional, args, count)) return T_ERR;
    e->call = CALL_SKINNED;
    if (changes) {
        decl called = {0};
        called.name = e->name;
        if (through_element(e->object)) {
            diag_error(e->at, "a list's element is a copy, so '" STR_FMT "' can't change it", STR_ARG(e->name));
            diag_note("take the pose out, change it, and put it back");
            return T_ERR;
        }
        if (!check_writable(c, e->object, &called, NULL)) return T_ERR;
    }
    return result;
}

static type check_skinned_index(checker *c, expr *e, const type obj, bool *handled)
{
    *handled = obj.kind == TY_STRUCT && obj.decl && obj.decl->skin_kind == SKIN_POSE;
    if (!*handled) return T_ERR;
    if (!skinned_cpu(c, e)) return T_ERR;
    const type index = check_expr_want(c, e->lhs, decl_type(c->prog->bone));
    if (index.kind == TY_ERROR) return T_ERR;
    if (!type_assignable(decl_type(c->prog->bone), index)) {
        diag_error(e->lhs->at, "a Pose is indexed by a Bone, not %s", type_name(index));
        return T_ERR;
    }
    e->skin_operation = SKIN_OP_POSE_INDEX;
    return decl_type(c->prog->bone_transform);
}
