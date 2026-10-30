// C adapters keep ownership in the existing lists/structs, and expose only
// checked model operations to Tide. Raw decoding never fetches resources.
static void gen_skinned_adapters(gen *g)
{
    const program *p = g->prog;
    if (!p->pose) return;
    sb *o = &g->c;
    const char *animation = type_cname(p->animation), *bone = type_cname(p->bone), *pose = type_cname(p->pose);
    sb_put(o, "typedef struct tide_skin_model_lists { tide_list data, vertices, indices; } tide_skin_model_lists;\n\n");
    for (int i = 0; i < p->skinned_types.count; i++) {
        const decl *d = p->skinned_types.items[i];
        if (d->skin_kind == SKIN_ANIMATIONS) {
            const char *n = type_cname(d);
            sb_printf(o, "TIDE_HELPER tide_list tide_skin_%s_to(%s v) { return v.data; }\n"
                         "TIDE_HELPER %s tide_skin_%s_from(tide_list v) { return (%s){.data = v}; }\n", n, n, n, n, n);
            continue;
        }
        if (d->skin_kind != SKIN_MESH) continue;
        const char *n = type_cname(d);
        sb_printf(o, "TIDE_HELPER tide_skin_model_lists tide_skin_%s_to(%s v) { return (tide_skin_model_lists){v.data, v.vertices, v.indices}; }\n"
                     "TIDE_HELPER %s tide_skin_%s_from(tide_skin_model_lists v) { return (%s){v.data, v.vertices, v.indices}; }\n", n, n, n, n, n);
        if (!p->blend_shape) continue;
        // model.Shaped(weights): the same model with its vertices' position,
        // normal and tangent (the fields a file fills by those names) moved.
        const decl *vertex = d->skin_vertex.decl;
        const char *places[3] = {"-1", "-1", "-1"};
        for (int f = 0; f < vertex->fields.count; f++) {
            const field *field = &vertex->fields.items[f];
            const int fill = mesh_fills(field);
            const int slot = fill == FILL_POSITION ? 0 : fill == FILL_NORMAL ? 1 : fill == FILL_TANGENT ? 2 : -1;
            if (slot < 0) continue;
            sb place = {0};
            sb_printf(&place, "(int32_t)offsetof(%s, %s)", type_cname(vertex), field_cname(field));
            places[slot] = place.data;
        }
        sb_printf(o, "TIDE_HELPER %s tide_skin_%s_shaped(%s v, tide_list weights)\n{\n"
                     "    v.vertices = tide_skinned_shaped(v.data, weights, v.vertices, (uint32_t)sizeof(%s), %s, %s, %s);\n"
                     "    return v;\n}\n", n, n, n, type_cname(vertex), places[0], places[1], places[2]);
    }
    sb_put(o, "\nTIDE_HELPER int32_t tide_skin_member_index(tide_list data, tide_str name, bool exact, bool bones)\n{\n"
              "    if (exact) return bones ? tide_skinned_find_bone(data, name) : tide_skinned_find_animation(data, name);\n"
              "    const int32_t count = bones ? tide_skinned_bone_count(data) : tide_skinned_animation_count(data);\n"
              "    int32_t found = -1;\n    for (int32_t i = 0; i < count; i++) {\n"
              "        const tide_str source = bones ? tide_skinned_bone_name(data, i) : tide_skinned_animation_name(data, i);\n"
              "        if (source.bytes != name.bytes || source.bytes == 0) continue;\n"
              "        char first = source.ptr[0]; if (first >= 'A' && first <= 'Z') first += 'a' - 'A';\n"
              "        if (first != name.ptr[0] || memcmp(source.ptr + 1, name.ptr + 1, (size_t)name.bytes - 1u)) continue;\n"
              "        if (found >= 0) return -1;\n        found = i;\n    }\n    return found;\n}\n\n");
    sb_printf(o, "TIDE_HELPER %s tide_skin_animation_at(tide_list data, int32_t i)\n{\n"
                 "    return (%s){.data = data, .index = i >= 0 && i < tide_skinned_animation_count(data) ? i : -1};\n}\n"
                 "TIDE_HELPER %s tide_skin_bone_at(tide_list data, int32_t i)\n{\n"
                 "    return (%s){.fingerprint = (int64_t)tide_skinned_fingerprint(data), .index = i >= 0 && i < tide_skinned_bone_count(data) ? i : -1};\n}\n"
                 "TIDE_HELPER %s tide_skin_animation_member(tide_list data, tide_str name, bool exact)\n{\n"
                 "    return tide_skin_animation_at(data, tide_skin_member_index(data, name, exact, false));\n}\n"
                 "TIDE_HELPER %s tide_skin_bone_member(tide_list data, tide_str name, bool exact)\n{\n"
                 "    return tide_skin_bone_at(data, tide_skin_member_index(data, name, exact, true));\n}\n"
                 "TIDE_HELPER tide_str tide_skin_bone_name(tide_list data, %s bone)\n{\n"
                 "    if (bone.fingerprint == 0 || (uint64_t)bone.fingerprint != tide_skinned_fingerprint(data)) return TIDE_STR_EMPTY;\n"
                 "    return tide_skinned_bone_name(data, bone.index);\n}\n"
                 "TIDE_HELPER bool tide_skin_bone_equal(%s a, %s b) { return a.fingerprint == b.fingerprint && a.index == b.index; }\n"
                 "TIDE_HELPER tide_str tide_skin_animation_name(%s v) { return tide_skinned_animation_name(v.data, v.index); }\n"
                 "TIDE_HELPER float tide_skin_animation_duration(%s v) { return tide_skinned_animation_duration(v.data, v.index); }\n\n",
              animation, animation, bone, bone, animation, bone, bone, bone, bone, animation, animation);
    sb_put(o, "typedef struct tide_skin_catalog { tide_list data, sources; } tide_skin_catalog;\n"
              "TIDE_HELPER tide_list tide_skin_catalog_list(tide_list data)\n{\n"
              "    return data.at ? tide_list_from(&data, 1, sizeof data) : (tide_list){0};\n}\n"
              "TIDE_HELPER tide_list tide_skin_catalog_source(tide_skin_catalog v, int32_t i)\n{\n"
              "    if (v.data.at) return i == 0 ? v.data : (tide_list){0};\n"
              "    const tide_list *at = tide_list_at(v.sources, i, sizeof *at);\n"
              "    return at ? *at : (tide_list){0};\n}\n"
              "TIDE_HELPER int32_t tide_skin_catalog_count(tide_skin_catalog v)\n{\n"
              "    if (v.data.at) return tide_skinned_animation_count(v.data);\n"
              "    int64_t count = 0;\n    const int32_t sources = tide_list_count(v.sources);\n"
              "    for (int32_t i = 0; i < sources; i++) count += tide_skinned_animation_count(tide_skin_catalog_source(v, i));\n"
              "    return count > INT32_MAX ? INT32_MAX : (int32_t)count;\n}\n");
    sb_printf(o, "TIDE_HELPER %s tide_skin_catalog_at(tide_skin_catalog v, int32_t index)\n{\n"
                 "    if (index < 0) return tide_skin_animation_at((tide_list){0}, -1);\n"
                 "    if (v.data.at) return tide_skin_animation_at(v.data, index);\n"
                 "    const int32_t sources = tide_list_count(v.sources);\n"
                 "    for (int32_t i = 0; i < sources; i++) {\n"
                 "        const tide_list data = tide_skin_catalog_source(v, i);\n"
                 "        const int32_t count = tide_skinned_animation_count(data);\n"
                 "        if (index < count) return tide_skin_animation_at(data, index);\n"
                 "        index -= count;\n    }\n    return tide_skin_animation_at((tide_list){0}, -1);\n}\n"
                 "TIDE_HELPER %s tide_skin_catalog_find(tide_skin_catalog v, tide_str name)\n{\n"
                 "    %s found = {.index = -1};\n    if (!name.bytes) return found;\n"
                 "    if (v.data.at) return tide_skin_animation_at(v.data, tide_skinned_find_animation(v.data, name));\n"
                 "    const int32_t sources = v.data.at ? 1 : tide_list_count(v.sources);\n"
                 "    if (sources == 1) {\n"
                 "        const tide_list data = tide_skin_catalog_source(v, 0);\n"
                 "        return tide_skin_animation_at(data, tide_skinned_find_animation(data, name));\n    }\n"
                 "    for (int32_t i = 0; i < sources; i++) {\n"
                 "        const tide_list data = tide_skin_catalog_source(v, i);\n"
                 "        const int32_t count = tide_skinned_animation_count(data);\n"
                 "        for (int32_t j = 0; j < count; j++) {\n"
                 "            if (!tide_str_eq(tide_skinned_animation_name(data, j), name)) continue;\n"
                 "            if (found.index >= 0) return (%s){.index = -1};\n"
                 "            found = tide_skin_animation_at(data, j);\n        }\n    }\n    return found;\n}\n"
                 "TIDE_HELPER %s tide_skin_catalog_member(tide_skin_catalog v, tide_str name, bool exact)\n{\n"
                 "    if (v.data.at) return tide_skin_animation_member(v.data, name, exact);\n"
                 "    if (exact) return tide_skin_catalog_find(v, name);\n"
                 "    %s found = {.index = -1};\n    const int32_t sources = tide_list_count(v.sources);\n"
                 "    for (int32_t i = 0; i < sources; i++) {\n"
                 "        const %s candidate = tide_skin_animation_member(tide_skin_catalog_source(v, i), name, false);\n"
                 "        if (candidate.index < 0) continue;\n"
                 "        if (found.index >= 0) return (%s){.index = -1};\n        found = candidate;\n    }\n    return found;\n}\n\n",
              animation, animation, animation, animation, animation, animation, animation, animation);
    // Lookup writes through ordinary owned assignment, copying the source
    // before replacing a result which may already own that same source.
    sb_printf(o, "TIDE_HELPER bool tide_skin_catalog_try_at(tide_skin_catalog data, int32_t index, %s *result, uint32_t where)\n{\n"
                 "    const %s value = tide_skin_catalog_at(data, index);\n"
                 "    const bool ok = value.index >= 0;\n"
                 "    tide_assign_%s(result, ok ? tide_copy_%s(value) : (%s){0}, where);\n"
                 "    return ok;\n}\n"
                 "TIDE_HELPER bool tide_skin_catalog_try_find(tide_skin_catalog data, tide_str name, %s *result, uint32_t where)\n{\n"
                 "    const %s value = tide_skin_catalog_find(data, name);\n"
                 "    const bool ok = value.index >= 0;\n"
                 "    tide_assign_%s(result, ok ? tide_copy_%s(value) : (%s){0}, where);\n"
                 "    return ok;\n}\n"
                 "TIDE_HELPER bool tide_skin_bone_try_at(tide_list data, int32_t index, %s *result, uint32_t where)\n{\n"
                 "    (void)where;\n    const %s value = tide_skin_bone_at(data, index);\n"
                 "    const bool ok = value.index >= 0;\n    *result = ok ? value : (%s){0};\n    return ok;\n}\n"
                 "TIDE_HELPER bool tide_skin_bone_try_find(tide_list data, tide_str name, %s *result, uint32_t where)\n{\n"
                 "    return tide_skin_bone_try_at(data, tide_skinned_find_bone(data, name), result, where);\n}\n\n",
              animation, animation, animation, animation, animation, animation, animation, animation, animation, animation,
              bone, bone, bone, bone);
    if (p->blend_shape) {
        const char *shape = type_cname(p->blend_shape);
        sb_printf(o, "TIDE_HELPER %s tide_skin_blend_shape_at(tide_list data, int32_t i)\n{\n"
                     "    return (%s){.data = data, .index = i >= 0 && i < tide_skinned_shape_count(data) ? i : -1};\n}\n"
                     "TIDE_HELPER tide_str tide_skin_blend_shape_name(%s v) { return tide_skinned_shape_name(v.data, v.index); }\n"
                     "TIDE_HELPER float tide_skin_blend_shape_weight(%s v) { return tide_skinned_shape_weight(v.data, v.index); }\n"
                     "TIDE_HELPER bool tide_skin_blend_shape_try_at(tide_list data, int32_t index, %s *result, uint32_t where)\n{\n"
                     "    const %s value = tide_skin_blend_shape_at(data, index);\n"
                     "    const bool ok = value.index >= 0;\n"
                     "    tide_assign_%s(result, ok ? tide_copy_%s(value) : (%s){0}, where);\n"
                     "    return ok;\n}\n"
                     "TIDE_HELPER bool tide_skin_blend_shape_try_find(tide_list data, tide_str name, %s *result, uint32_t where)\n{\n"
                     "    return tide_skin_blend_shape_try_at(data, tide_skinned_find_shape(data, name), result, where);\n}\n\n",
                  shape, shape, shape, shape, shape, shape, shape, shape, shape, shape);
    }
    sb_put(o, "TIDE_HELPER float tide_skin_seconds(float seconds, float duration, int32_t playback)\n{\n"
              "    if (playback != 1 || !(duration > 0.0f)) return seconds;\n"
              "    if (!tide_is_finite_f(seconds)) return 0.0f;\n"
              "    float n = seconds < 0.0f ? -seconds : seconds, step = duration;\n"
              "    while (step <= n * 0.5f && tide_is_finite_f(step * 2.0f)) step *= 2.0f;\n"
              "    while (step >= duration) { if (n >= step) n -= step; step *= 0.5f; }\n"
              "    const float wrapped = seconds < 0.0f && n > 0.0f ? duration - n : n;\n"
              "    return wrapped >= duration ? 0.0f : wrapped;\n}\n\n");
    sb_printf(o, "TIDE_HELPER %s tide_skin_rest(tide_list data)\n{\n"
                 "    %s v = {.data = data};\n    (void)tide_skinned_rest(data, &v.transforms, TIDE_IN_SCRATCH);\n    return v;\n}\n"
                 "TIDE_HELPER %s tide_skin_animation_sample(%s animation, float seconds, int32_t playback)\n{\n"
                 "    %s v = {.data = animation.data};\n"
                 "    if (playback == 1) seconds = tide_skin_seconds(seconds, tide_skinned_animation_duration(animation.data, animation.index), playback);\n"
                 "    (void)tide_skinned_sample(animation.data, animation.index, seconds, &v.transforms, TIDE_IN_SCRATCH);\n    return v;\n}\n"
                 "TIDE_HELPER void tide_skin_pose_sample(%s *v, %s animation, float seconds, int32_t playback, uint32_t where)\n{\n"
                 "    if (!v) return;\n"
                 "    if (playback == 1) seconds = tide_skin_seconds(seconds, tide_skinned_animation_duration(animation.data, animation.index), playback);\n"
                 "    (void)tide_skinned_sample_into(v->data, animation.data, animation.index, seconds, &v->transforms, where);\n}\n"
                 "TIDE_HELPER void tide_skin_pose_blend(%s *v, %s other, float weight, uint32_t where)\n{\n"
                 "    if (!v || !tide_skinned_compatible(v->data, other.data)) return;\n"
                 "    (void)tide_skinned_blend(v->transforms, other.transforms, weight, &v->transforms, where);\n}\n"
                 "TIDE_HELPER bool tide_skin_pose_has(%s v, %s bone)\n{\n"
                 "    return bone.index >= 0 && bone.fingerprint != 0 && (uint64_t)bone.fingerprint == tide_skinned_fingerprint(v.data)\n"
                 "        && bone.index < tide_list_count(v.transforms) && tide_list_count(v.transforms) == tide_skinned_bone_count(v.data);\n}\n"
                 "TIDE_HELPER tide_float4x4 tide_skin_pose_model(%s v, %s bone)\n{\n"
                 "    return tide_skin_pose_has(v, bone) ? tide_skinned_model(v.data, v.transforms, bone.index) : tide_identity_f4x4();\n}\n"
                 "TIDE_HELPER const tide_bone_transform *tide_skin_pose_ref(%s v, %s bone)\n{\n"
                 "    static const tide_bone_transform zero = {.rotation = {{0, 0, 0, 1}}, .scale = {1, 1, 1}};\n"
                 "    const tide_bone_transform *at = tide_skin_pose_has(v, bone) ? tide_list_at(v.transforms, bone.index, sizeof *at) : NULL;\n"
                 "    return at ? at : &zero;\n}\n"
                 "TIDE_HELPER tide_bone_transform *tide_skin_pose_slot(%s *v, %s bone)\n{\n"
                 "    return v && tide_skin_pose_has(*v, bone) ? tide_list_at_mut(v->transforms, bone.index, sizeof(tide_bone_transform)) : NULL;\n}\n\n",
              pose, pose, pose, animation, pose, pose, animation, pose, pose, pose, bone, pose, bone, pose, bone, pose, bone);
    if (p->humanoid_rig) {
        const char *rig = type_cname(p->humanoid_rig), *mapping = type_cname(p->retarget_map);
        sb_printf(o, "TIDE_HELPER %s tide_skin_humanoid_auto(%s reference)\n{\n"
                     "    %s v = {.referencePose = tide_copy_%s(reference), .data = tide_list_copy(reference.data, 1u)};\n"
                     "    (void)tide_humanoid_auto(reference.data, reference.transforms, &v.bones, &v.status, TIDE_IN_SCRATCH);\n"
                     "    return v;\n}\n"
                     "TIDE_HELPER %s tide_skin_humanoid_bone(%s v, int32_t role)\n{\n"
                     "    const int32_t *at = role >= 0 && role < TIDE_HUMANOID_COUNT ? tide_list_at(v.bones, role, sizeof *at) : NULL;\n"
                     "    const int32_t index = at ? *at : -1;\n"
                     "    return index >= 0 && index < tide_skinned_bone_count(v.data) ? tide_skin_bone_at(v.data, index) : (%s){0};\n}\n"
                     "TIDE_HELPER %s tide_skin_humanoid_status(%s v, int32_t role)\n{\n"
                     "    const int32_t *at = role >= 0 && role < TIDE_HUMANOID_COUNT ? tide_list_at(v.status, role, sizeof *at) : NULL;\n"
                     "    return (%s)(at && *at >= TIDE_HUMANOID_MISSING && *at <= TIDE_HUMANOID_MANUAL ? *at : TIDE_HUMANOID_MISSING);\n}\n"
                     "TIDE_HELPER void tide_skin_humanoid_set(%s *v, int32_t role, %s bone, uint32_t where)\n{\n"
                     "    if (!v || role < 0 || role >= TIDE_HUMANOID_COUNT) return;\n"
                     "    const tide_list data = v->data.at ? v->data : v->referencePose.data;\n"
                     "    const uint64_t fingerprint = tide_skinned_fingerprint(data);\n"
                     "    if (!fingerprint || !tide_skinned_compatible(data, v->referencePose.data)) return;\n"
                     "    const bool unset = bone.fingerprint == 0 && bone.index == 0;\n"
                     "    if (!unset && ((uint64_t)bone.fingerprint != fingerprint || bone.index < 0 || bone.index >= tide_skinned_bone_count(data))) return;\n"
                     "    if (!v->data.at) tide_list_set(&v->data, tide_list_copy(data, 1u), 1u, where);\n"
                     "    if (tide_list_count(v->bones) != TIDE_HUMANOID_COUNT || tide_list_count(v->status) != TIDE_HUMANOID_COUNT) {\n"
                     "        tide_list_resize(&v->bones, TIDE_HUMANOID_COUNT, sizeof(int32_t), where);\n"
                     "        tide_list_resize(&v->status, TIDE_HUMANOID_COUNT, sizeof(int32_t), where);\n"
                     "        for (int32_t i = 0; i < TIDE_HUMANOID_COUNT; i++) {\n"
                     "            *(int32_t *)tide_list_at_mut(v->bones, i, sizeof(int32_t)) = -1;\n"
                     "            *(int32_t *)tide_list_at_mut(v->status, i, sizeof(int32_t)) = TIDE_HUMANOID_MISSING;\n"
                     "        }\n    }\n"
                     "    tide_list_prepare(&v->bones, sizeof(int32_t), where);\n"
                     "    tide_list_prepare(&v->status, sizeof(int32_t), where);\n"
                     "    *(int32_t *)tide_list_at_mut(v->bones, role, sizeof(int32_t)) = unset ? -1 : bone.index;\n"
                     "    *(int32_t *)tide_list_at_mut(v->status, role, sizeof(int32_t)) = TIDE_HUMANOID_MANUAL;\n}\n"
                     "TIDE_HELPER bool tide_skin_pose_retarget(%s *v, %s source, %s mapping, uint32_t where)\n{\n"
                     "    if (!v) return false;\n"
                     "    return tide_retarget_apply(mapping.plan, source.data, source.transforms, v->data, mapping.rootScale, mapping.rootOffset,\n"
                     "        mapping.headingDegrees, mapping.spineDistribution, mapping.twistDistribution, &v->transforms, where);\n}\n\n",
                  rig, pose, rig, pose, bone, rig, bone, type_cname(p->humanoid_status), rig, type_cname(p->humanoid_status),
                  rig, bone, pose, pose, mapping);
    }
    for (int i = 0; i < p->results.count; i++) {
        const decl *r = p->results.items[i];
        const type value = r->fields.items[0].type;
        if (value.kind != TY_STRUCT || !value.decl->skin_kind) continue;
        if (r->fields.count == 2 && value.decl->skin_kind == SKIN_RETARGET_MAP) {
            const char *result = c_type((type){TY_FAILABLE, (decl *)r});
            sb_printf(o, "TIDE_HELPER %s tide_skin_result%d_retarget_create(%s source, %s sourceRig, %s targetRig)\n{\n"
                         "    %s result = {0};\n"
                         "    if (sourceRig.data.at && !tide_skinned_compatible(sourceRig.data, sourceRig.referencePose.data)) {\n"
                         "        tide_debug_log(\"tide: RetargetMap.Create: source referencePose belongs to another skeleton; create a new HumanoidRig for it\");\n"
                         "        result.error = %s_IncompatibleSkeleton;\n        return result;\n    }\n"
                         "    if (targetRig.data.at && !tide_skinned_compatible(targetRig.data, targetRig.referencePose.data)) {\n"
                         "        tide_debug_log(\"tide: RetargetMap.Create: target referencePose belongs to another skeleton; create a new HumanoidRig for it\");\n"
                         "        result.error = %s_IncompatibleSkeleton;\n        return result;\n    }\n"
                         "    const int32_t error = tide_retarget_create(source.data, sourceRig.data, sourceRig.referencePose.transforms, sourceRig.bones, sourceRig.status,\n"
                         "        targetRig.data, targetRig.referencePose.transforms, targetRig.bones, targetRig.status, &result.value.plan, &result.value.rootScale, TIDE_IN_SCRATCH);\n"
                         "    result.error = (%s)error;\n    if (error) return result;\n"
                         "    result.value.spineDistribution = 1.0f;\n    result.value.twistDistribution = 1.0f;\n"
                         "    result.ok = true;\n    return result;\n}\n\n",
                      result, r->index, type_cname(p->skeleton), type_cname(p->humanoid_rig), type_cname(p->humanoid_rig),
                      result, type_cname(p->retarget_error), type_cname(p->retarget_error), type_cname(p->retarget_error));
            continue;
        }
        if (p->retarget_map && r->fields.count == 2 && r->fields.items[1].type.decl == p->io_error
            && (value.decl->skin_kind == SKIN_ANIMATION || value.decl->skin_kind == SKIN_ANIMATIONS)) {
            const bool catalog = value.decl->skin_kind == SKIN_ANIMATIONS;
            const char *result = c_type((type){TY_FAILABLE, (decl *)r});
            sb_printf(o, "TIDE_HELPER %s tide_skin_result%d_retarget_bake(%s mapping, %s source, %s target, float sampleRate)\n{\n"
                         "    %s result = {.error = IOError_InvalidData};\n"
                         "    const tide_retarget_bake_settings settings = {mapping.rootScale, mapping.rootOffset, mapping.headingDegrees,\n"
                         "        mapping.spineDistribution, mapping.twistDistribution, sampleRate};\n"
                         "    char why[256] = {0};\n",
                      result, r->index, type_cname(p->retarget_map), catalog ? "tide_skin_catalog" : animation,
                      type_cname(p->skeleton), result);
            if (catalog) {
                sb_put(o, "    const int32_t count = source.data.at ? 1 : tide_list_count(source.sources);\n"
                          "    if (count <= 0) { tide_debug_log(\"tide: RetargetMap.Bake: the source catalog contains no animations\"); return result; }\n"
                          "    const uint32_t mark = tide_scratch_mark();\n    tide_list sources = {0};\n"
                          "    tide_retarget_bake_budget budget = {0};\n"
                          "    for (int32_t i = 0; i < count; i++) {\n"
                          "        uint8_t *baked = NULL;\n        uint32_t size = 0;\n"
                          "        if (!tide_retarget_bake_with_budget(tide_skin_catalog_source(source, i), -1, target.data, mapping.plan, &settings, &budget, &baked, &size, why)) {\n"
                          "            tide_debug_log(why);\n            tide_scratch_reset(mark);\n            return result;\n        }\n"
                          "        const tide_list data = tide_list_from(baked, (int32_t)size, 1u);\n"
                          "        tide_skinned_data_free(baked);\n"
                          "        *(tide_list *)tide_list_add(&sources, sizeof(tide_list), TIDE_IN_SCRATCH) = data;\n    }\n"
                          "    result.value.data = sources;\n");
            } else {
                sb_put(o, "    uint8_t *baked = NULL;\n    uint32_t size = 0;\n"
                          "    if (source.index < 0 || !tide_retarget_bake(source.data, source.index, target.data, mapping.plan, &settings, &baked, &size, why)) {\n"
                          "        tide_debug_log(source.index < 0 ? \"tide: RetargetMap.Bake: select a valid source Animation\" : why);\n"
                          "        return result;\n    }\n"
                          "    result.value.data = tide_list_from(baked, (int32_t)size, 1u);\n"
                          "    tide_skinned_data_free(baked);\n");
            }
            sb_put(o, "    result.ok = true;\n    result.error = (IOError)0;\n    return result;\n}\n\n");
            continue;
        }
        if (r->fields.count != 2 || value.decl->skin_kind != SKIN_MESH) continue;
        const decl *mesh = value.decl, *vertex = mesh->skin_vertex.decl;
        const char *result = c_type((type){TY_FAILABLE, (decl *)r});
        sb_printf(o, "TIDE_HELPER %s tide_skin_result%d_decode(tide_list input)\n{\n"
                     "    %s result = {.error = IOError_InvalidData};\n"
                     "    bool copied = false;\n    void *raw = tide_list_flatten(input, 1u, false, &copied);\n"
                     "    uint8_t *decoded = NULL;\n    uint32_t size = 0;\n    char why[256], note[256];\n"
                     "    const bool ok = tide_skinned_data_decode(raw, (size_t)tide_list_count(input), true, NULL, NULL, &decoded, &size, why, note);\n"
                     "    tide_list_unflatten(input, raw, 1u, false, copied);\n    if (!ok) return result;\n"
                     "    tide_skinned_data_view view;\n    if (!tide_skinned_data_open(decoded, size, &view)) { tide_skinned_data_free(decoded); return result; }\n"
                     "    result.value.data = tide_list_from(decoded, (int32_t)size, 1u);\n", result, r->index, result);
        const decl *catalogs[] = {mesh->skin_animations, mesh->skin_skeleton->skin_bones};
        for (int k = 0; k < 2; k++) for (int m = 0; m < catalogs[k]->skin_catalog.count; m++) {
            const skinned_member *member = &catalogs[k]->skin_catalog.items[m];
            sb_put(o, "    if (tide_skin_member_index(result.value.data, ");
            gen_text_literal(o, member->source_name, true);
            sb_printf(o, ", %s, %s) < 0) { tide_skinned_data_free(decoded); return result; }\n",
                      member->explicit_name ? "true" : "false", k ? "true" : "false");
        }
        sb_printf(o, "    tide_list_resize(&result.value.vertices, (int32_t)view.vertices, sizeof(%s), TIDE_IN_SCRATCH);\n"
                     "    tide_list_resize(&result.value.indices, (int32_t)view.indices, sizeof(int32_t), TIDE_IN_SCRATCH);\n"
                     "    const %s defaults = ", type_cname(vertex), type_cname(vertex));
        gen_value(g, o, vertex, NULL, 0);
        sb_put(o, ";\n    for (uint32_t i = 0; i < view.vertices; i++) {\n        tide_skinned_vertex_data from;\n"
                  "        (void)tide_skinned_data_vertex(&view, i, &from);\n");
        sb_printf(o, "        %s *to = tide_list_at_mut(result.value.vertices, (int32_t)i, sizeof *to);\n        *to = defaults;\n", type_cname(vertex));
        for (int f = 0; f < vertex->fields.count; f++) {
            const field *field = &vertex->fields.items[f];
            const int fill = mesh_fills(field);
            const char *source = fill == FILL_POSITION ? "position" : fill == FILL_NORMAL ? "normal" : fill == FILL_UV ? "uv"
                               : fill == FILL_UV2 ? "uv2" : fill == FILL_COLOR ? "color" : fill == FILL_TANGENT ? "tangent" : NULL;
            if (source) sb_printf(o, "        memcpy(&to->%s, from.%s, sizeof to->%s);\n", field_cname(field), source, field_cname(field));
        }
        sb_put(o, "    }\n    for (uint32_t i = 0; i < view.indices; i++) {\n        uint32_t index;\n"
                  "        (void)tide_skinned_data_index(&view, i, &index);\n"
                  "        int32_t *to = tide_list_at_mut(result.value.indices, (int32_t)i, sizeof *to);\n        *to = (int32_t)index;\n"
                  "    }\n    tide_skinned_data_free(decoded);\n    result.ok = true;\n    result.error = (IOError)0;\n    return result;\n}\n\n");
    }
}
