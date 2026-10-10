// Included by check.c after the ordinary list/result type builders.
// Model values deliberately use their existing ownership and snapshot paths.

static field skinned_field(const char *name, const type t, const bool hidden)
{
    field f = {0};
    f.name = str_from(name);
    f.type = t;
    f.hidden = hidden;
    return f;
}

static decl *skinned_struct(program *prog, const char *name, const skinned_kind kind)
{
    decl *d = NEW(decl);
    d->kind = DECL_STRUCT;
    d->builtin = true;
    d->skin_kind = kind;
    d->name = str_from(name);
    sb cname = {0};
    sb_printf(&cname, "tide_skinned_type%d", prog->skinned_types.count);
    d->c_name = cname.data;
    d->index = 2;
    vec_push(prog->skinned_types, d);
    vec_push(prog->structs, d);
    return d;
}

static void skinned_data_field(program *prog, decl *d)
{
    vec_push(d->fields, skinned_field("data", list_of(prog, (type){TY_BYTE, NULL}), true));
}

static void skinned_builtins(program *prog)
{
    if (prog->animation) return;
    decl *t = prog->bone_transform = skinned_struct(prog, "BoneTransform", SKIN_TRANSFORM);
    t->c_name = "tide_bone_transform";
    vec_push(t->fields, skinned_field("position", (type){TY_FLOAT3, NULL}, false));
    field rotation = skinned_field("rotation", (type){TY_QUATERNION, NULL}, false);
    rotation.default_value = NEW(expr);
    rotation.default_value->kind = E_MEMBER;
    rotation.default_value->type = rotation.type;
    rotation.default_value->member = str_from("identity");
    rotation.default_value->object = NEW(expr);
    rotation.default_value->object->kind = E_NAME;
    rotation.default_value->object->name = str_from("quaternion");
    rotation.default_value->object->type = rotation.type;
    rotation.default_value->c_constant = "((tide_quaternion){{0, 0, 0, 1}})";
    vec_push(t->fields, rotation);
    field scale = skinned_field("scale", (type){TY_FLOAT3, NULL}, false);
    scale.default_value = NEW(expr);
    scale.default_value->kind = E_CALL;
    scale.default_value->type = scale.type;
    scale.default_value->name = str_from("float3");
    scale.default_value->call = CALL_CONSTRUCT;
    scale.default_value->ctor = CTOR_SPLAT;
    expr *one = NEW(expr);
    one->kind = E_FLOAT;
    one->type = T_FLOAT_;
    one->text = str_from("1");
    vec_push(scale.default_value->args, one);
    vec_push(t->fields, scale);
    prog->animation = skinned_struct(prog, "Animation", SKIN_ANIMATION);
    skinned_data_field(prog, prog->animation);
    vec_push(prog->animation->fields, skinned_field("index", T_INT_, true));
    prog->bone = skinned_struct(prog, "Bone", SKIN_BONE);
    vec_push(prog->bone->fields, skinned_field("fingerprint", T_LONG_, true));
    vec_push(prog->bone->fields, skinned_field("index", T_INT_, true));
    prog->pose = skinned_struct(prog, "Pose", SKIN_POSE);
    skinned_data_field(prog, prog->pose);
    vec_push(prog->pose->fields, skinned_field("transforms", list_of(prog, decl_type(t)), true));
    prog->animations = skinned_struct(prog, "Animations", SKIN_ANIMATIONS);
    skinned_data_field(prog, prog->animations);
    prog->bones = skinned_struct(prog, "Bones", SKIN_BONES);
    skinned_data_field(prog, prog->bones);
    prog->skeleton = skinned_struct(prog, "Skeleton", SKIN_SKELETON);
    skinned_data_field(prog, prog->skeleton);
    prog->skeleton->skin_bones = prog->bones;
    // This enum is lazy too: other programs retain their existing headers/hash.
    prog->playback = NEW(decl);
    prog->playback->kind = DECL_ENUM;
    prog->playback->builtin = true;
    prog->playback->name = str_from("Playback");
    prog->playback->width = 4;
    const enum_member clamp = {str_from("Clamp"), {0}, NULL, 0};
    const enum_member loop = {str_from("Loop"), {0}, NULL, 1};
    vec_push(prog->playback->members, clamp);
    vec_push(prog->playback->members, loop);
    vec_push(prog->decls, prog->playback);
}

static bool skinned_type_named(program *prog, const str name, type *out)
{
    static const char *const names[] = {"Animation", "Bone", "Pose", "Skeleton", "BoneTransform", "Playback", "Animations"};
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        if (!str_eq_c(name, names[i])) continue;
        skinned_builtins(prog);
        decl *const types[] = {prog->animation, prog->bone, prog->pose, prog->skeleton, prog->bone_transform, prog->playback, prog->animations};
        *out = decl_type(types[i]);
        return true;
    }
    return false;
}

static type skinned_mesh_of(program *prog, const type vertex, decl *contract)
{
    skinned_builtins(prog);
    for (int i = 0; i < prog->skinned_types.count; i++) {
        decl *d = prog->skinned_types.items[i];
        if (d->skin_kind == SKIN_MESH && d->skin_vertex.decl == vertex.decl
            && d->skin_contract == contract && !d->skin_inferred) return decl_type(d);
    }
    sb name = {0};
    sb_printf(&name, "SkinnedMesh<%s", type_name(vertex));
    if (contract) sb_printf(&name, ", " STR_FMT, STR_ARG(contract->name));
    sb_put(&name, ">");
    decl *d = skinned_struct(prog, name.data, SKIN_MESH);
    d->skin_vertex = vertex;
    d->skin_contract = contract;
    d->skin_animations = prog->animations;
    d->skin_skeleton = prog->skeleton;
    skinned_data_field(prog, d);
    vec_push(d->fields, skinned_field("vertices", list_of(prog, vertex), false));
    vec_push(d->fields, skinned_field("indices", list_of(prog, T_INT_), false));
    return decl_type(d);
}
