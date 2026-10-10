// Included by check.c before asset declarations are checked.

static str skinned_identifier(const char *source)
{
    if (!source || !*source) return (str){0};
    char *name = str_to_cstr(str_from(source));
    if (name[0] >= 'A' && name[0] <= 'Z') name[0] += 'a' - 'A';
    const str result = str_from(name);
    if (!lex_identifier_ok(result) || str_eq_c(result, "count") || str_eq_c(result, "Find") || str_eq_c(result, "At")) return (str){0};
    return result;
}

static str skinned_alias(const field *f, bool *exact)
{
    *exact = false;
    for (int i = 0; i < f->attributes.count; i++) {
        const attribute *a = &f->attributes.items[i];
        if (!str_eq_c(a->name, "Name") || a->values.count != 1 || a->args.count) continue;
        const expr *literal = text_literal(a->values.items[0]);
        if (!literal) continue;
        *exact = true;
        // The language's literal escapes, without treating a backslash as a path.
        sb decoded = {0};
        for (int k = 0; k < literal->text.len; k++) {
            char ch = literal->text.ptr[k];
            if (ch == '\\' && k + 1 < literal->text.len) {
                ch = literal->text.ptr[++k];
                ch = ch == 'n' ? '\n' : ch == 'r' ? '\r' : ch == 't' ? '\t' : ch;
            }
            sb_putn(&decoded, &ch, 1);
        }
        return (str){decoded.data, (int)decoded.len};
    }
    return f->name;
}

static decl *skinned_catalog(program *prog, const skinned_kind kind, const int status)
{
    decl *d = skinned_struct(prog, kind == SKIN_ANIMATIONS ? "Animations" : "Bones", kind);
    skinned_data_field(prog, d);
    d->skin_metadata_status = status;
    return d;
}

static void skinned_contract_catalogs(program *prog, decl *mesh, const loc at)
{
    if (!mesh->skin_contract || mesh->skin_animations != prog->animations) return;
    decl *animations = mesh->skin_animations = skinned_catalog(prog, SKIN_ANIMATIONS, 1);
    decl *bones = skinned_catalog(prog, SKIN_BONES, 1);
    decl *skeleton = mesh->skin_skeleton = skinned_struct(prog, "Skeleton", SKIN_SKELETON);
    skinned_data_field(prog, skeleton);
    skeleton->skin_bones = bones;
    for (int i = 0; i < mesh->skin_contract->fields.count; i++) {
        const field *f = &mesh->skin_contract->fields.items[i];
        const skinned_kind kind = f->type.kind == TY_STRUCT ? f->type.decl->skin_kind : SKIN_NONE;
        if ((kind != SKIN_ANIMATION && kind != SKIN_BONE) || f->is_local || f->default_value) {
            diag_error(f->at.line ? f->at : at, "a model contract's fields are required Animation or Bone values, without defaults");
            continue;
        }
        if (str_eq_c(f->name, "count") || str_eq_c(f->name, "Find") || str_eq_c(f->name, "At")) {
            diag_error(f->at, "contract member '" STR_FMT "' conflicts with a catalog operation", STR_ARG(f->name));
            continue;
        }
        bool exact;
        const str source = skinned_alias(f, &exact);
        if (exact && !source.len) {
            diag_error(f->at, "a model contract's Name alias cannot be empty");
            continue;
        }
        const skinned_member member = {f->name, source, -1, exact};
        decl *catalog = kind == SKIN_ANIMATION ? animations : bones;
        vec_push(catalog->skin_catalog, member);
    }
}

static const char *skinned_source_name(const model_metadata *metadata, const bool bones, const uint32_t index)
{
    return bones ? metadata->nodes[index].name : metadata->animations[index];
}

static int skinned_source_find(const model_metadata *metadata, const bool bones, const skinned_member *member)
{
    const uint32_t count = bones ? metadata->node_count : metadata->animation_count;
    int found = -1;
    for (uint32_t i = 0; i < count; i++) {
        const char *source = skinned_source_name(metadata, bones, i);
        if (!source || !*source) continue; // Unnamed entries have no name lookup, only At.
        const str name = member->explicit_name ? str_from(source) : skinned_identifier(source);
        if (!str_eq(name, member->source_name)) continue;
        if (found != -1) return -2;
        found = (int)i;
    }
    return found;
}

static void skinned_validate_contract(const decl *mesh, const model_metadata *metadata, const loc at)
{
    const decl *catalogs[] = {mesh->skin_animations, mesh->skin_skeleton->skin_bones};
    for (int kind = 0; kind < 2; kind++) {
        const decl *catalog = catalogs[kind];
        for (int i = 0; i < catalog->skin_catalog.count; i++) {
            const skinned_member *member = &catalog->skin_catalog.items[i];
            const int found = skinned_source_find(metadata, kind == 1, member);
            if (found < 0) {
                diag_error(at, "model contract requires %s '" STR_FMT "', which is %s", kind ? "bone" : "animation",
                           STR_ARG(member->source_name), found == -2 ? "ambiguous in the model" : "missing from the model");
            }
        }
    }
}

typedef struct skinned_name_index {
    str name;
    uint32_t index;
} skinned_name_index;

static int skinned_name_order(const void *a, const void *b)
{
    const skinned_name_index *left = a, *right = b;
    const int common = left->name.len < right->name.len ? left->name.len : right->name.len;
    const int order = common ? memcmp(left->name.ptr, right->name.ptr, (size_t)common) : 0;
    return order ? order : left->name.len - right->name.len;
}

static void skinned_infer_catalog(decl *catalog, const model_metadata *metadata, const bool bones)
{
    const uint32_t count = bones ? metadata->node_count : metadata->animation_count;
    skinned_name_index *names = arena_alloc(sizeof *names * (size_t)(count ? count : 1));
    bool *duplicates = arena_alloc(sizeof *duplicates * (size_t)(count ? count : 1));
    memset(duplicates, 0, sizeof *duplicates * (size_t)count);
    for (uint32_t i = 0; i < count; i++) names[i] = (skinned_name_index){skinned_identifier(skinned_source_name(metadata, bones, i)), i};
    if (count > 1) qsort(names, count, sizeof *names, skinned_name_order);
    for (uint32_t i = 1; i < count; i++) {
        if (str_eq(names[i - 1].name, names[i].name)) duplicates[names[i - 1].index] = duplicates[names[i].index] = true;
    }
    for (uint32_t i = 0; i < count; i++) {
        const char *source = skinned_source_name(metadata, bones, i);
        if (source) vec_push(catalog->skin_sources, str_from(str_to_cstr(str_from(source))));
        const str name = skinned_identifier(source);
        if (!name.len || duplicates[i]) continue;
        const skinned_member member = {name, str_from(str_to_cstr(str_from(source))), (int)i, true};
        vec_push(catalog->skin_catalog, member);
    }
}

static void skinned_asset_catalogs(checker *c, decl *asset, const expr *path)
{
    type inner = asset->return_type;
    if (inner.kind == TY_LIST) inner = list_element(inner);
    if (inner.kind == TY_MAP) inner = map_value(inner);
    decl *base = inner.decl;
    skinned_contract_catalogs(c->prog, base, path->at);
    // Each asset gets its own immutable catalog type; var preserves it.
    // A collection has a common contract rather than item-specific inference.
    const bool infer = !base->skin_contract && !asset->asset_many;
    decl *mesh = base;
    if (infer) {
        mesh = skinned_struct(c->prog, str_to_cstr(base->name), SKIN_MESH);
        mesh->fields = base->fields;
        mesh->skin_vertex = base->skin_vertex;
        mesh->skin_inferred = true;
        mesh->skin_animations = skinned_catalog(c->prog, SKIN_ANIMATIONS, -1);
        mesh->skin_skeleton = skinned_struct(c->prog, "Skeleton", SKIN_SKELETON);
        skinned_data_field(c->prog, mesh->skin_skeleton);
        mesh->skin_skeleton->skin_bones = skinned_catalog(c->prog, SKIN_BONES, -1);
        asset->return_type = decl_type(mesh);
    }
    if (c->prog->asset_metadata_only) {
        if (asset->asset_many) return;
        const char *written = asset_written_path(path->text);
        const model_metadata *metadata = NULL;
        const char *why = NULL;
        model_metadata_status status;
        if (asset_path_is_url(written, strlen(written))) {
            status = model_metadata_cached_url(written, c->prog->game_folder, &metadata, &why);
        } else {
            char *folder = asset_path_folder(asset->unit->src->path);
            char *absolute = asset_path_join(folder, written, strlen(written));
            free(folder);
            status = model_metadata_cached(absolute, &metadata, &why);
            free(absolute);
        }
        if (infer) {
            mesh->skin_animations->skin_metadata_status = status == MODEL_METADATA_READY ? 1 : status == MODEL_METADATA_INVALID ? -2 : -1;
            mesh->skin_skeleton->skin_bones->skin_metadata_status = mesh->skin_animations->skin_metadata_status;
        }
        if (!metadata) {
            if (status == MODEL_METADATA_INVALID) {
                if (!asset_skinned_format_diagnostic(written, why, path->at)) {
                    diag_error(path->at, "model catalog metadata is invalid: %s", why ? why : "invalid model JSON");
                }
            } else diag_info(path->at, "model catalog metadata is unavailable: %s", why ? why : "no cached model JSON");
            return;
        }
        if (base->skin_contract) skinned_validate_contract(base, metadata, path->at);
        if (infer) {
            skinned_infer_catalog(mesh->skin_animations, metadata, false);
            skinned_infer_catalog(mesh->skin_skeleton->skin_bones, metadata, true);
        }
        return;
    }
    if (!asset->asset) return;
    for (int i = 0; i < asset->asset->count; i++) {
        const asset_match *item = &asset->asset->items[i];
        model_metadata metadata = {0};
        char why[256];
        if (!model_metadata_read(item->data, item->size, item->key, &metadata, why)) continue; // Full import already diagnosed bad data.
        if (base->skin_contract) skinned_validate_contract(base, &metadata, path->at);
        if (infer) {
            mesh->skin_animations->skin_metadata_status = mesh->skin_skeleton->skin_bones->skin_metadata_status = 1;
            skinned_infer_catalog(mesh->skin_animations, &metadata, false);
            skinned_infer_catalog(mesh->skin_skeleton->skin_bones, &metadata, true);
        }
        model_metadata_free(&metadata);
    }
}
