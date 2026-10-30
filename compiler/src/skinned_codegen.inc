// Included after the ordinary expression helpers. A model operand is always
// evaluated once; catalogs are small values carrying the immutable bytes.
static void gen_skin_data(gen *g, sb *o, expr *object)
{
    sb_put(o, "(");
    gen_expr(g, o, object);
    sb_put(o, ").data");
}

static void gen_skin_catalog(gen *g, sb *o, expr *object)
{
    // Direct model catalog operations borrow its blob, retaining the fixed
    // path's allocation-free behavior. Escaped catalogs own source lists.
    if (object->skin_operation == SKIN_OP_ANIMATIONS && !object->hoisted) {
        sb_put(o, "((tide_skin_catalog){.data = ");
        gen_skin_data(g, o, object->object);
    } else {
        sb_put(o, "((tide_skin_catalog){.sources = ");
        gen_skin_data(g, o, object);
    }
    sb_put(o, "})");
}

static void gen_skin_expr(gen *g, sb *o, const expr *e)
{
    const skinned_operation op = (skinned_operation)e->skin_operation;
    if (skinned_bakes(op)) {
        sb_printf(o, "tide_skin_result%d_retarget_bake(", e->type.decl->index);
        gen_expr(g, o, e->object);
        sb_put(o, ", ");
        if (op == SKIN_OP_RETARGET_BAKE_CATALOG) gen_skin_catalog(g, o, e->args.items[0]);
        else gen_expr(g, o, e->args.items[0]);
        sb_put(o, ", ");
        gen_as(g, o, e->args.items[1], (type){TY_STRUCT, g->prog->skeleton});
        sb_put(o, ", ");
        gen_as(g, o, e->args.items[2], (type){TY_FLOAT, NULL});
        sb_put(o, ")");
        return;
    }
    if (op == SKIN_OP_HUMANOID_AUTO) {
        sb_put(o, "tide_skin_humanoid_auto(");
        gen_expr(g, o, e->args.items[0]);
        sb_put(o, ")");
        return;
    }
    if (op == SKIN_OP_HUMANOID_INDEX || op == SKIN_OP_HUMANOID_STATUS) {
        sb_printf(o, "tide_skin_humanoid_%s(", op == SKIN_OP_HUMANOID_INDEX ? "bone" : "status");
        gen_expr(g, o, e->object);
        sb_put(o, ", ");
        gen_expr(g, o, op == SKIN_OP_HUMANOID_INDEX ? e->lhs : e->args.items[0]);
        sb_put(o, ")");
        return;
    }
    if (op == SKIN_OP_RETARGET_CREATE) {
        sb_printf(o, "tide_skin_result%d_retarget_create(", e->type.decl->index);
        gen_as(g, o, e->args.items[0], (type){TY_STRUCT, g->prog->skeleton});
        sb_put(o, ", ");
        gen_expr(g, o, e->args.items[1]);
        sb_put(o, ", ");
        gen_expr(g, o, e->args.items[2]);
        sb_put(o, ")");
        return;
    }
    if (op == SKIN_OP_POSE_RETARGET) {
        sb_put(o, "tide_skin_pose_retarget(&(");
        gen_expr(g, o, e->object);
        sb_put(o, "), ");
        gen_expr(g, o, e->args.items[0]);
        sb_put(o, ", ");
        gen_expr(g, o, e->args.items[1]);
        sb_printf(o, ", %s)", place_where(g, e->object));
        return;
    }
    if (op == SKIN_OP_BLEND_SHAPES) {
        sb_printf(o, "((%s){.data = ", c_type(e->type));
        gen_skin_data(g, o, e->object);
        sb_put(o, "})");
        return;
    }
    if (op == SKIN_OP_BLEND_SHAPE_COUNT) {
        sb_put(o, "tide_skinned_shape_count(");
        gen_skin_data(g, o, e->object);
        sb_put(o, ")");
        return;
    }
    if (op == SKIN_OP_BLEND_SHAPE_NAME || op == SKIN_OP_BLEND_SHAPE_WEIGHT) {
        sb_printf(o, "tide_skin_blend_shape_%s(", op == SKIN_OP_BLEND_SHAPE_NAME ? "name" : "weight");
        gen_expr(g, o, e->object);
        sb_put(o, ")");
        return;
    }
    if (op == SKIN_OP_TRY_GET_BLEND_SHAPE_BY_NAME || op == SKIN_OP_TRY_GET_BLEND_SHAPE_AT) {
        sb_printf(o, "tide_skin_blend_shape_try_%s(", op == SKIN_OP_TRY_GET_BLEND_SHAPE_BY_NAME ? "find" : "at");
        gen_skin_data(g, o, e->object);
        sb_put(o, ", ");
        if (op == SKIN_OP_TRY_GET_BLEND_SHAPE_BY_NAME) gen_expr(g, o, e->args.items[0]);
        else gen_as(g, o, e->args.items[0], (type){TY_INT, NULL});
        sb_put(o, ", &(");
        gen_expr(g, o, e->args.items[1]);
        sb_printf(o, "), %s)", place_where(g, e->args.items[1]));
        return;
    }
    if (op == SKIN_OP_SHAPED) {
        sb_printf(o, "tide_skin_%s_shaped(", type_cname(e->object->type.decl));
        gen_expr(g, o, e->object);
        sb_put(o, ", ");
        gen_expr(g, o, e->args.items[0]);
        sb_put(o, ")");
        return;
    }
    if (op == SKIN_OP_ANIMATIONS || op == SKIN_OP_SKELETON || op == SKIN_OP_BONES) {
        sb_printf(o, "((%s){.data = ", c_type(e->type));
        if (op == SKIN_OP_ANIMATIONS) sb_put(o, "tide_skin_catalog_list(");
        gen_skin_data(g, o, e->object);
        if (op == SKIN_OP_ANIMATIONS) sb_put(o, ")");
        sb_put(o, "})");
        return;
    }
    if (op == SKIN_OP_ANIMATION_MEMBER || op == SKIN_OP_BONE_MEMBER) {
        const decl *catalog = e->object->type.decl;
        const skinned_member *member = NULL;
        for (int i = 0; i < catalog->skin_catalog.count; i++) {
            if (str_eq(catalog->skin_catalog.items[i].name, e->member)) member = &catalog->skin_catalog.items[i];
        }
        if (member->index >= 0 && op == SKIN_OP_ANIMATION_MEMBER) {
            // Imported catalogs pin source order, including duplicate original
            // names. The asset identity/preflight already authenticates it.
            sb_put(o, "tide_skin_catalog_at(");
            gen_skin_catalog(g, o, e->object);
            sb_printf(o, ", %d)", member->index);
            return;
        }
        sb_printf(o, "tide_skin_%s_member(", op == SKIN_OP_ANIMATION_MEMBER ? "catalog" : "bone");
        if (op == SKIN_OP_ANIMATION_MEMBER) gen_skin_catalog(g, o, e->object);
        else gen_skin_data(g, o, e->object);
        sb_put(o, ", ");
        gen_text_literal(o, member->source_name, true);
        sb_printf(o, ", %s)", member->explicit_name ? "true" : "false");
        // Inferred catalogs store exact file names; contracts store normalized
        // field names unless [Name] supplied an exact alias.
        return;
    }
    if (op == SKIN_OP_ANIMATION_COUNT || op == SKIN_OP_BONE_COUNT) {
        sb_put(o, op == SKIN_OP_ANIMATION_COUNT ? "tide_skin_catalog_count(" : "tide_skinned_bone_count(");
        if (op == SKIN_OP_ANIMATION_COUNT) gen_skin_catalog(g, o, e->object);
        else gen_skin_data(g, o, e->object);
        sb_put(o, ")");
        return;
    }
    if (op == SKIN_OP_ANIMATION_NAME || op == SKIN_OP_ANIMATION_DURATION) {
        sb_printf(o, "tide_skin_animation_%s(", op == SKIN_OP_ANIMATION_NAME ? "name" : "duration");
    } else if (skinned_lookup(op)) {
        sb_printf(o, "tide_skin_%s_try_%s(",
                  op == SKIN_OP_TRY_GET_ANIMATION_BY_NAME || op == SKIN_OP_TRY_GET_ANIMATION_AT ? "catalog" : "bone",
                  op == SKIN_OP_TRY_GET_ANIMATION_BY_NAME || op == SKIN_OP_TRY_GET_BONE_BY_NAME ? "find" : "at");
        if (op == SKIN_OP_TRY_GET_ANIMATION_BY_NAME || op == SKIN_OP_TRY_GET_ANIMATION_AT) gen_skin_catalog(g, o, e->object);
        else gen_skin_data(g, o, e->object);
        sb_put(o, ", ");
        if (op == SKIN_OP_TRY_GET_ANIMATION_BY_NAME || op == SKIN_OP_TRY_GET_BONE_BY_NAME) gen_expr(g, o, e->args.items[0]);
        else gen_as(g, o, e->args.items[0], (type){TY_INT, NULL});
        sb_put(o, ", &(");
        gen_expr(g, o, e->args.items[1]);
        sb_printf(o, "), %s)", place_where(g, e->args.items[1]));
        return;
    } else if (op == SKIN_OP_REST_POSE) {
        sb_put(o, "tide_skin_rest(");
        gen_skin_data(g, o, e->object);
        sb_put(o, ")");
        return;
    } else if (op == SKIN_OP_ANIMATION_SAMPLE) sb_put(o, "tide_skin_animation_sample(");
    else if (op == SKIN_OP_POSE_SAMPLE || op == SKIN_OP_POSE_BLEND) {
        sb_printf(o, "tide_skin_pose_%s(&(", op == SKIN_OP_POSE_SAMPLE ? "sample" : "blend");
        gen_expr(g, o, e->object);
        sb_put(o, "), ");
        for (int i = 0; i < e->args.count; i++) {
            if (i) sb_put(o, ", ");
            const bool seconds = i == 1;
            gen_as(g, o, e->args.items[i], seconds ? (type){TY_FLOAT, NULL} : e->args.items[i]->type);
        }
        sb_printf(o, ", %s)", place_where(g, e->object));
        return;
    } else if (op == SKIN_OP_POSE_MODEL) sb_put(o, "tide_skin_pose_model(");
    else if (op == SKIN_OP_POSE_INDEX) {
        sb_put(o, "(*tide_skin_pose_ref(");
        gen_expr(g, o, e->object);
        sb_put(o, ", ");
        gen_expr(g, o, e->lhs);
        sb_put(o, "))");
        return;
    } else if (op == SKIN_OP_DECODE) {
        sb_printf(o, "tide_skin_result%d_decode(", e->type.decl->index);
        gen_expr(g, o, e->args.items[0]);
        sb_put(o, ")");
        return;
    } else return;
    gen_expr(g, o, e->object);
    for (int i = 0; i < e->args.count; i++) {
        sb_put(o, ", ");
        gen_as(g, o, e->args.items[i], op == SKIN_OP_ANIMATION_SAMPLE && i == 0 ? (type){TY_FLOAT, NULL} : e->args.items[i]->type);
    }
    sb_put(o, ")");
}
