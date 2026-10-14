// Included by check.c before asset declarations are checked.

static str skinned_identifier(const char *source)
{
    if (!source || !*source) return (str){0};
    char *name = str_to_cstr(str_from(source));
    if (name[0] >= 'A' && name[0] <= 'Z') name[0] += 'a' - 'A';
    const str result = str_from(name);
    if (!lex_identifier_ok(result) || str_eq_c(result, "count") || str_eq_c(result, "TryGetByName") || str_eq_c(result, "TryGetAt")) return (str){0};
    return result;
}

// Checked imports expose writable source identifiers even when exporters use
// punctuation, spaces, leading digits or language keywords in their names.
// Contract fields retain their existing first-letter/exact-alias matching.
static str skinned_inferred_identifier(const char *source)
{
    if (!source || !*source) return (str){0};
    sb name = {0};
    bool word = false;
    for (const unsigned char *p = (const unsigned char *)source; *p; p++) {
        char ch = (char)*p;
        const bool letter = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z');
        if (!letter && *p != '_' && !(*p >= '0' && *p <= '9')) {
            word = name.len > 0;
            continue;
        }
        if (!name.len && ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
        else if (word && ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
        sb_putn(&name, &ch, 1);
        word = false;
    }
    if (!name.len) sb_put(&name, "_");
    const str result = {name.data, (int)name.len};
    if (!lex_identifier_ok(result) || str_eq_c(result, "count") || str_eq_c(result, "TryGetByName")
        || str_eq_c(result, "TryGetAt") || str_eq_c(result, "data")) {
        sb prefixed = {0};
        sb_put(&prefixed, "_");
        sb_putn(&prefixed, result.ptr, (size_t)result.len);
        return (str){prefixed.data, (int)prefixed.len};
    }
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
    decl *d = skinned_struct(prog, kind == SKIN_ANIMATIONS ? "AnimationCatalog" : "Bones", kind);
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
        if (str_eq_c(f->name, "count") || str_eq_c(f->name, "TryGetByName") || str_eq_c(f->name, "TryGetAt")) {
            diag_error(f->at, "contract member '" STR_FMT "' conflicts with a catalog operation", STR_ARG(f->name));
            continue;
        }
        bool exact;
        const str source = skinned_alias(f, &exact);
        if (exact && !source.len) {
            diag_error(f->at, "a model contract's Name alias cannot be empty");
            continue;
        }
        const skinned_member member = {f->name, source, -1, exact, {0}};
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
        if (!source || !*source) continue; // Unnamed entries have no name lookup, only TryGetAt.
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

static void skinned_infer_catalog(decl *catalog, const model_metadata *metadata, const bool bones,
                                  const char *const *files)
{
    const uint32_t count = bones ? metadata->node_count : metadata->animation_count;
    skinned_name_index *names = arena_alloc(sizeof *names * (size_t)(count ? count : 1));
    bool *duplicates = arena_alloc(sizeof *duplicates * (size_t)(count ? count : 1));
    memset(duplicates, 0, sizeof *duplicates * (size_t)count);
    str *members = arena_alloc(sizeof *members * (size_t)(count ? count : 1));
    bool *ambiguous_bones = arena_alloc(sizeof *ambiguous_bones * (size_t)(count ? count : 1));
    memset(ambiguous_bones, 0, sizeof *ambiguous_bones * (size_t)count);
    if (bones) {
        // Bone lookup uses canonical names rather than metadata node order.
        // Exact duplicate names cannot identify one canonical bone.
        for (uint32_t i = 0; i < count; i++) {
            const char *source = skinned_source_name(metadata, true, i);
            names[i] = (skinned_name_index){source ? str_from(source) : (str){0}, i};
        }
        if (count > 1) qsort(names, count, sizeof *names, skinned_name_order);
        for (uint32_t i = 1; i < count; i++)
            if (str_eq(names[i - 1].name, names[i].name))
                ambiguous_bones[names[i - 1].index] = ambiguous_bones[names[i].index] = true;
    }
    for (uint32_t i = 0; i < count; i++) {
        members[i] = skinned_inferred_identifier(skinned_source_name(metadata, bones, i));
        names[i] = (skinned_name_index){members[i], i};
    }
    if (count > 1) qsort(names, count, sizeof *names, skinned_name_order);
    for (uint32_t i = 1; i < count; i++) {
        if (str_eq(names[i - 1].name, names[i].name)) duplicates[names[i - 1].index] = duplicates[names[i].index] = true;
    }
    if (files) {
        for (uint32_t i = 0; i < count; i++) {
            if (!duplicates[i] || !members[i].len) continue;
            const char *file = files[i];
            size_t length = strlen(file);
            if (asset_path_is_url(file, length)) length = strcspn(file, "?#");
            const char *stem = asset_path_name(str_to_cstr((str){file, (int)length}));
            const char *dot = strrchr(stem, '.');
            const str prefix = skinned_inferred_identifier(str_to_cstr((str){stem, (int)(dot ? dot - stem : (ptrdiff_t)strlen(stem))}));
            sb qualified = {0};
            sb_putn(&qualified, prefix.ptr, (size_t)prefix.len);
            char first = members[i].ptr[0];
            if (first >= 'a' && first <= 'z') first -= 'a' - 'A';
            sb_putn(&qualified, &first, 1);
            sb_putn(&qualified, members[i].ptr + 1, (size_t)members[i].len - 1u);
            members[i] = (str){qualified.data, (int)qualified.len};
        }
        memset(duplicates, 0, sizeof *duplicates * (size_t)count);
        for (uint32_t i = 0; i < count; i++) names[i] = (skinned_name_index){members[i], i};
        if (count > 1) qsort(names, count, sizeof *names, skinned_name_order);
        for (uint32_t i = 1; i < count; i++) {
            if (str_eq(names[i - 1].name, names[i].name)) duplicates[names[i - 1].index] = duplicates[names[i].index] = true;
        }
    }
    for (uint32_t i = 0; i < count; i++) {
        const char *source = skinned_source_name(metadata, bones, i);
        vec_push(catalog->skin_sources, source ? str_from(str_to_cstr(str_from(source))) : (str){0});
        str name = members[i];
        if (!name.len || ambiguous_bones[i]) continue;
        if (duplicates[i]) {
            // Source ordinals make colliding aliases unique. Reserve every
            // unsuffixed name so an authored 'jump_1' keeps its own member.
            sb unique = {0};
            sb_putn(&unique, name.ptr, (size_t)name.len);
            size_t prefix = unique.len;
            skinned_name_index key = {0};
            do {
                unique.len = prefix;
                sb_put(&unique, "_");
                prefix = unique.len;
                sb_printf(&unique, "%u", i + 1u);
                key.name = (str){unique.data, (int)unique.len};
                if (!bsearch(&key, names, count, sizeof *names, skinned_name_order)) break;
            } while (true);
            name = key.name;
        }
        const skinned_member member = {name, str_from(str_to_cstr(str_from(source))), (int)i, true,
            files ? str_from(files[i]) : (str){0}};
        vec_push(catalog->skin_catalog, member);
    }
}

static void skinned_animation_asset(checker *c, decl *asset, const expr *path)
{
    str selected = {0};
    bool named = false;
    for (int i = 0; i < asset->attributes.count; i++) {
        const attribute *attr = &asset->attributes.items[i];
        if (!str_eq_c(attr->name, "Name")) continue;
        if (named || attr->args.count || attr->values.count != 1 || !text_literal(attr->values.items[0])) {
            diag_error(attr->at, "select one exact source clip with [Name(\"Root|Run\")] before an Animation asset");
            return;
        }
        named = true;
        field alias = {0};
        vec_push(alias.attributes, *attr);
        bool exact;
        selected = skinned_alias(&alias, &exact);
        if (!selected.len) {
            diag_error(attr->at, "an Animation asset's Name alias cannot be empty");
            return;
        }
    }
    if (c->prog->asset_metadata_only)
        asset->asset = asset_resolve_paths(asset->unit->src->path, path->text, ASSET_ANIMATION, false, false,
                                         path->at, c->prog->game_folder);
    if (!asset->asset) return;
    const asset_match *item = &asset->asset->items[0];
    model_metadata owned = {0};
    const model_metadata *metadata = NULL;
    const char *why = NULL;
    char error[256];
    if (c->prog->asset_metadata_only) {
        const model_metadata_status status = asset_path_is_url(item->key, strlen(item->key))
            ? model_metadata_cached_url(item->key, c->prog->game_folder, &metadata, &why)
            : model_metadata_cached(item->abs, &metadata, &why);
        if (!metadata) {
            if (status == MODEL_METADATA_INVALID) {
                if (!asset_animations_format_diagnostic(item->key, why, path->at))
                    diag_error(path->at, "model catalog metadata is invalid: %s", why ? why : "invalid model JSON");
            } else diag_info(path->at, "model catalog metadata is unavailable: %s", why ? why : "no cached model JSON");
            return;
        }
    } else {
        if (!model_metadata_read(item->data, item->size, item->key, &owned, error)) {
            diag_error(path->at, "animation catalog metadata cannot be read from '%s': %s", item->key, error);
            return;
        }
        metadata = &owned;
    }
    int found = -1;
    if (!named) {
        if (metadata->animation_count == 1) found = 0;
        else {
            diag_error(path->at, "an Animation asset requires exactly one clip, but '%s' contains %u", item->key,
                       metadata->animation_count);
            diag_note("select a clip with [Name(\"original clip name\")], or import every clip with asset AnimationCatalog");
        }
    } else {
        for (uint32_t i = 0; i < metadata->animation_count; i++) {
            if (!metadata->animations[i] || !str_eq(selected, str_from(metadata->animations[i]))) continue;
            if (found >= 0) { found = -2; break; }
            found = (int)i;
        }
        if (found < 0) diag_error(path->at, "animation '" STR_FMT "' is %s in '%s'", STR_ARG(selected),
                                 found == -2 ? "ambiguous" : "missing", item->key);
    }
    asset->asset_animation_index = found;
    model_metadata_free(&owned);
}

static void skinned_asset_catalogs(checker *c, decl *asset, const expr *path)
{
    type inner = asset->return_type;
    if (inner.kind == TY_LIST) inner = list_element(inner);
    if (inner.kind == TY_MAP) inner = map_value(inner);
    decl *base = inner.decl;
    const bool animations_only = asset->asset_kind == ASSET_ANIMATIONS;
    if (!animations_only) skinned_contract_catalogs(c->prog, base, path->at);
    // Each asset gets its own immutable catalog type; var preserves it.
    // A collection has a common contract rather than item-specific inference.
    const bool infer = !base->skin_contract && !asset->asset_many;
    decl *mesh = base;
    if (infer) {
        mesh = skinned_struct(c->prog, str_to_cstr(base->name), animations_only ? SKIN_ANIMATIONS : SKIN_MESH);
        mesh->fields = base->fields;
        mesh->skin_vertex = base->skin_vertex;
        mesh->skin_inferred = true;
        if (animations_only) mesh->skin_metadata_status = -1;
        else {
            mesh->skin_animations = skinned_catalog(c->prog, SKIN_ANIMATIONS, -1);
            mesh->skin_skeleton = skinned_struct(c->prog, "Skeleton", SKIN_SKELETON);
            skinned_data_field(c->prog, mesh->skin_skeleton);
            mesh->skin_skeleton->skin_bones = skinned_catalog(c->prog, SKIN_BONES, -1);
        }
        asset->return_type = decl_type(mesh);
    }
    decl *animations = animations_only ? mesh : mesh->skin_animations;
    decl *bones = animations_only ? NULL : mesh->skin_skeleton->skin_bones;
    if (animations_only && infer) {
        if (!asset->asset) return;
        VEC(char *) names = {0};
        VEC(const char *) files = {0};
        int status = 1;
        for (int i = 0; i < asset->asset->count; i++) {
            const asset_match *item = &asset->asset->items[i];
            model_metadata owned = {0};
            const model_metadata *metadata = NULL;
            const char *why = NULL;
            char error[256];
            if (c->prog->asset_metadata_only) {
                const model_metadata_status result = asset_path_is_url(item->key, strlen(item->key))
                    ? model_metadata_cached_url(item->key, c->prog->game_folder, &metadata, &why)
                    : model_metadata_cached(item->abs, &metadata, &why);
                if (!metadata) {
                    if (result == MODEL_METADATA_INVALID) {
                        status = -2;
                        if (!asset_animations_format_diagnostic(item->key, why, path->at))
                            diag_error(path->at, "model catalog metadata is invalid: %s", why ? why : "invalid model JSON");
                    } else {
                        if (status != -2) status = -1;
                        diag_info(path->at, "model catalog metadata is unavailable: %s", why ? why : "no cached model JSON");
                    }
                    continue;
                }
            } else {
                if (!model_metadata_read(item->data, item->size, item->key, &owned, error)) {
                    status = -2;
                    diag_error(path->at, "animation catalog metadata cannot be read from '%s': %s", item->key, error);
                    continue;
                }
                metadata = &owned;
            }
            for (uint32_t n = 0; n < metadata->animation_count; n++) {
                vec_push(names, metadata->animations[n] ? str_to_cstr(str_from(metadata->animations[n])) : NULL);
                vec_push(files, item->key);
            }
            model_metadata_free(&owned);
        }
        animations->skin_metadata_status = status;
        // Don't offer partially checked aliases: missing files can introduce
        // collisions and change the names or flattened source indices.
        if (status == 1) {
            const model_metadata metadata = {.animations = names.items, .animation_count = (uint32_t)names.count};
            skinned_infer_catalog(animations, &metadata, false, asset->asset->count > 1 ? files.items : NULL);
        }
        return;
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
            animations->skin_metadata_status = status == MODEL_METADATA_READY ? 1 : status == MODEL_METADATA_INVALID ? -2 : -1;
            if (bones) bones->skin_metadata_status = animations->skin_metadata_status;
        }
        if (!metadata) {
            if (status == MODEL_METADATA_INVALID) {
                if (!(animations_only ? asset_animations_format_diagnostic(written, why, path->at)
                                      : asset_skinned_format_diagnostic(written, why, path->at))) {
                    diag_error(path->at, "model catalog metadata is invalid: %s", why ? why : "invalid model JSON");
                }
            } else diag_info(path->at, "model catalog metadata is unavailable: %s", why ? why : "no cached model JSON");
            return;
        }
        if (base->skin_contract) skinned_validate_contract(base, metadata, path->at);
        if (infer) {
            skinned_infer_catalog(animations, metadata, false, NULL);
            if (bones) skinned_infer_catalog(bones, metadata, true, NULL);
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
            animations->skin_metadata_status = 1;
            if (bones) bones->skin_metadata_status = 1;
            skinned_infer_catalog(animations, &metadata, false, NULL);
            if (bones) skinned_infer_catalog(bones, &metadata, true, NULL);
        }
        model_metadata_free(&metadata);
    }
}
