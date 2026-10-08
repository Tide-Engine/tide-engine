// Included after the ordinary expression helpers. A model operand is always
// evaluated once; catalogs are small values carrying the immutable bytes.
static void gen_skin_data(gen *g, sb *o, expr *object)
{
    sb_put(o, "(");
    gen_expr(g, o, object);
    sb_put(o, ").data");
}

static void gen_skin_expr(gen *g, sb *o, const expr *e)
{
    const skinned_operation op = (skinned_operation)e->skin_operation;
    if (op == SKIN_OP_ANIMATIONS || op == SKIN_OP_SKELETON || op == SKIN_OP_BONES) {
        sb_printf(o, "((%s){.data = ", c_type(e->type));
        gen_skin_data(g, o, e->object);
        sb_put(o, "})");
        return;
    }
    if (op == SKIN_OP_ANIMATION_MEMBER || op == SKIN_OP_BONE_MEMBER) {
        const decl *catalog = e->object->type.decl;
        const skinned_member *member = NULL;
        for (int i = 0; i < catalog->skin_catalog.count; i++) {
            if (str_eq(catalog->skin_catalog.items[i].name, e->member)) member = &catalog->skin_catalog.items[i];
        }
        sb_printf(o, "tide_skin_%s_member(", op == SKIN_OP_ANIMATION_MEMBER ? "animation" : "bone");
        gen_skin_data(g, o, e->object);
        sb_put(o, ", ");
        gen_text_literal(o, member->source_name, true);
        sb_printf(o, ", %s)", member->explicit_name ? "true" : "false");
        // Inferred catalogs store exact file names; contracts store normalized
        // field names unless [Name] supplied an exact alias.
        return;
    }
    if (op == SKIN_OP_ANIMATION_COUNT || op == SKIN_OP_BONE_COUNT) {
        sb_printf(o, "tide_skinned_%s_count(", op == SKIN_OP_ANIMATION_COUNT ? "animation" : "bone");
        gen_skin_data(g, o, e->object);
        sb_put(o, ")");
        return;
    }
    if (op == SKIN_OP_ANIMATION_NAME || op == SKIN_OP_ANIMATION_DURATION) {
        sb_printf(o, "tide_skin_animation_%s(", op == SKIN_OP_ANIMATION_NAME ? "name" : "duration");
    } else if (op == SKIN_OP_FIND_ANIMATION || op == SKIN_OP_FIND_BONE || op == SKIN_OP_AT_ANIMATION || op == SKIN_OP_AT_BONE) {
        sb_printf(o, "tide_skin_result%d_%s(", e->type.decl->index,
                  op == SKIN_OP_FIND_ANIMATION || op == SKIN_OP_FIND_BONE ? "find" : "at");
        gen_skin_data(g, o, e->object);
        sb_put(o, ", ");
        if (op == SKIN_OP_FIND_ANIMATION || op == SKIN_OP_FIND_BONE) gen_expr(g, o, e->args.items[0]);
        else gen_as(g, o, e->args.items[0], (type){TY_INT, NULL});
        sb_put(o, ")");
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
