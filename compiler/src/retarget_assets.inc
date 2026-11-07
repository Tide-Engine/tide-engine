// Build-time attribute checking stays beside ordinary asset checking; no
// retarget operation survives into the baked Animation's runtime sampling.
static decl *retarget_profile(checker *c, expr *e)
{
    str name;
    if (!qualified_text(e, &name)) {
        diag_error(e->at, "Retarget names two HumanoidRig assets: Retarget(sourceRig, targetRig)");
        return NULL;
    }
    decl *d = find_named(c, name, e->at, NAME_ASSET);
    if (!d) { diag_error(e->at, "Retarget needs a HumanoidRig asset named '" STR_FMT "'", STR_ARG(name)); return NULL; }
    check_asset_decl(c, d);
    if (e->kind == E_MEMBER) mark_namespaces(e->object);
    e->bind = BIND_ASSET;
    e->constant = d;
    e->type = d->return_type;
    if (d->asset_kind != ASSET_HUMANOID) {
        diag_error(e->at, "Retarget needs a HumanoidRig profile asset, but '" STR_FMT "' is %s", STR_ARG(name), type_name(d->return_type));
        return NULL;
    }
    return d;
}

static bool retarget_vector(const expr *e, tide_float3 *out)
{
    if (e->bind == BIND_CONST) return retarget_vector(e->constant->value, out);
    if (e->kind != E_CALL || e->call != CALL_CONSTRUCT || e->type.kind != TY_FLOAT3) return false;
    float x, y, z;
    if (e->args.count == 1 && setting_float(e->args.items[0], &x, 0)) { *out = (tide_float3){x, x, x}; return true; }
    if (e->args.count != 3 || !setting_float(e->args.items[0], &x, 0) || !setting_float(e->args.items[1], &y, 0)
        || !setting_float(e->args.items[2], &z, 0)) return false;
    *out = (tide_float3){x, y, z};
    return true;
}

static void check_retarget_attribute(checker *c, decl *k, attribute *attr)
{
    if (k->asset_kind != ASSET_ANIMATION && k->asset_kind != ASSET_ANIMATIONS) {
        diag_error(attr->at, "Retarget goes before an Animation or AnimationCatalog asset");
        return;
    }
    expr *call = attr->call;
    if (!call || call->args.count < 2 || arg_name(call, 0).len || arg_name(call, 1).len) {
        diag_error(attr->at, "Retarget starts with two profile assets: [Retarget(sourceRig, targetRig)]");
        return;
    }
    decl *source = retarget_profile(c, call->args.items[0]);
    decl *target = retarget_profile(c, call->args.items[1]);
    tide_retarget_bake_settings settings = tide_retarget_bake_defaults(1);
    bool valid = source && target, automatic_scale = true;
    unsigned chosen = 0;
    static const char *const names[] = {"rootScale", "rootOffset", "headingDegrees", "spineDistribution", "twistDistribution", "sampleRate"};
    for (int i = 2; i < call->args.count; i++) {
        const str name = arg_name(call, i);
        int setting = 0;
        while (setting < 6 && !str_eq_c(name, names[setting])) setting++;
        expr *value = call->args.items[i];
        if (setting == 6) {
            diag_error(value->at, "Retarget options are named rootScale, rootOffset, headingDegrees, spineDistribution, twistDistribution or sampleRate");
            valid = false; continue;
        }
        if (chosen & (1u << setting)) { diag_error(value->at, "Retarget already has '%s'", names[setting]); valid = false; continue; }
        chosen |= 1u << setting;
        if (!is_constant(c, value)) { diag_error(value->at, "Retarget options are build-time constants"); valid = false; continue; }
        const type expected = {.kind = setting == 1 ? TY_FLOAT3 : TY_FLOAT};
        const type actual = check_constant_expr(c, value, true, expected);
        float number = 0;
        bool ok = type_assignable(expected, actual);
        if (setting == 1) ok = ok && retarget_vector(value, &settings.root_offset);
        else ok = ok && setting_float(value, &number, 0);
        if (!ok) {
            diag_error(value->at, "Retarget %s needs %s", names[setting], setting == 1 ? "a constant float3(number, number, number)" : "a numeric constant using literals, constants or arithmetic");
            valid = false; continue;
        }
        switch (setting) {
        case 0: settings.root_scale = number; automatic_scale = false; break;
        case 1: break;
        case 2: settings.heading_degrees = number; break;
        case 3: settings.spine_distribution = number; break;
        case 4: settings.twist_distribution = number; break;
        case 5: settings.sample_rate = number; break;
        }
    }
    if (!valid || !k->asset || !source->asset || !target->asset || c->prog->asset_metadata_only) return;
    for (int i = 0; i < k->asset->count; i++) {
        if (!humanoid_asset_bake(&k->asset->items[i], k->asset_kind == ASSET_ANIMATION ? k->asset_animation_index : -1,
                                 &source->asset->items[0], &target->asset->items[0], settings, automatic_scale,
                                 attr->at, c->prog->game_folder)) return;
    }
    if (k->asset_kind == ASSET_ANIMATION) k->asset_animation_index = 0;
}
