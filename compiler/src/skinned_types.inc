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
    type data = list_of(prog, (type){TY_BYTE, NULL});
    if (d->skin_kind == SKIN_ANIMATIONS) data = list_of(prog, data);
    vec_push(d->fields, skinned_field("data", data, true));
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
    prog->animations = skinned_struct(prog, "AnimationCatalog", SKIN_ANIMATIONS);
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

// BlendShape and the catalog of them, made the first time a program uses
// blend shapes: a skinned program that doesn't keeps its header and its C.
static void skinned_blend_shapes(program *prog)
{
    skinned_builtins(prog);
    if (prog->blend_shape) return;
    prog->blend_shape = skinned_struct(prog, "BlendShape", SKIN_BLEND_SHAPE);
    skinned_data_field(prog, prog->blend_shape);
    vec_push(prog->blend_shape->fields, skinned_field("index", T_INT_, true));
    prog->blend_shapes = skinned_struct(prog, "BlendShapes", SKIN_BLEND_SHAPES);
    skinned_data_field(prog, prog->blend_shapes);
}

static decl *skinned_enum(program *prog, const char *name, const char *const *members, const int count, const int first)
{
    decl *d = NEW(decl);
    d->kind = DECL_ENUM;
    d->builtin = true;
    d->name = str_from(name);
    d->qualified = d->name;
    d->width = 4;
    for (int i = 0; i < count; i++) {
        expr *value = NEW(expr);
        value->kind = E_INT;
        value->type = T_INT_;
        value->int_value = i + first;
        const enum_member m = {str_from(members[i]), {0}, value, i + first};
        vec_push(d->members, m);
    }
    vec_push(prog->decls, d);
    return d;
}

// Retarget profiles and prepared plans are ordinary owned values. Making them
// lazily leaves programs which only sample/bind poses byte-for-byte unchanged.
static void skinned_retarget_types(program *prog)
{
    skinned_builtins(prog);
    if (prog->humanoid_rig) return;
    static const char *const roles[] = {
        "Hips", "LeftUpperLeg", "RightUpperLeg", "LeftLowerLeg", "RightLowerLeg", "LeftFoot", "RightFoot",
        "Spine", "Chest", "Neck", "Head", "LeftShoulder", "RightShoulder", "LeftUpperArm", "RightUpperArm",
        "LeftLowerArm", "RightLowerArm", "LeftHand", "RightHand", "LeftToes", "RightToes", "LeftEye", "RightEye", "Jaw",
        "LeftThumbProximal", "LeftThumbIntermediate", "LeftThumbDistal",
        "LeftIndexProximal", "LeftIndexIntermediate", "LeftIndexDistal",
        "LeftMiddleProximal", "LeftMiddleIntermediate", "LeftMiddleDistal",
        "LeftRingProximal", "LeftRingIntermediate", "LeftRingDistal",
        "LeftLittleProximal", "LeftLittleIntermediate", "LeftLittleDistal",
        "RightThumbProximal", "RightThumbIntermediate", "RightThumbDistal",
        "RightIndexProximal", "RightIndexIntermediate", "RightIndexDistal",
        "RightMiddleProximal", "RightMiddleIntermediate", "RightMiddleDistal",
        "RightRingProximal", "RightRingIntermediate", "RightRingDistal",
        "RightLittleProximal", "RightLittleIntermediate", "RightLittleDistal", "UpperChest",
    };
    static const char *const statuses[] = {"Missing", "Automatic", "Uncertain", "Manual"};
    static const char *const errors[] = {
        "InvalidRig", "MissingBone", "AmbiguousBone", "InvalidReferencePose", "IncompatibleSkeleton", "UnsupportedScale",
    };
    prog->humanoid_bone = skinned_enum(prog, "HumanoidBone", roles, (int)(sizeof roles / sizeof roles[0]), 0);
    prog->humanoid_status = skinned_enum(prog, "HumanoidBoneStatus", statuses, (int)(sizeof statuses / sizeof statuses[0]), 0);
    prog->retarget_error = skinned_enum(prog, "RetargetError", errors, (int)(sizeof errors / sizeof errors[0]), 1);
    prog->humanoid_rig = skinned_struct(prog, "HumanoidRig", SKIN_HUMANOID_RIG);
    prog->humanoid_rig->qualified = prog->humanoid_rig->name;
    skinned_data_field(prog, prog->humanoid_rig);
    vec_push(prog->humanoid_rig->fields, skinned_field("referencePose", decl_type(prog->pose), false));
    vec_push(prog->humanoid_rig->fields, skinned_field("bones", list_of(prog, T_INT_), true));
    vec_push(prog->humanoid_rig->fields, skinned_field("status", list_of(prog, T_INT_), true));
    prog->retarget_map = skinned_struct(prog, "RetargetMap", SKIN_RETARGET_MAP);
    prog->retarget_map->qualified = prog->retarget_map->name;
    vec_push(prog->retarget_map->fields, skinned_field("plan", list_of(prog, (type){TY_BYTE, NULL}), true));
    field scale = skinned_field("rootScale", T_FLOAT_, false);
    scale.default_value = NEW(expr);
    scale.default_value->kind = E_FLOAT;
    scale.default_value->type = T_FLOAT_;
    scale.default_value->text = str_from("1");
    vec_push(prog->retarget_map->fields, scale);
    vec_push(prog->retarget_map->fields, skinned_field("rootOffset", (type){TY_FLOAT3, NULL}, false));
    vec_push(prog->retarget_map->fields, skinned_field("headingDegrees", T_FLOAT_, false));
    field spine = skinned_field("spineDistribution", T_FLOAT_, false);
    spine.default_value = scale.default_value;
    vec_push(prog->retarget_map->fields, spine);
    field twist = skinned_field("twistDistribution", T_FLOAT_, false);
    twist.default_value = scale.default_value;
    vec_push(prog->retarget_map->fields, twist);
}

static bool skinned_type_named(program *prog, const str name, type *out)
{
    static const char *const retarget[] = {"HumanoidRig", "HumanoidBone", "HumanoidBoneStatus", "RetargetMap", "RetargetError"};
    for (size_t i = 0; i < sizeof retarget / sizeof retarget[0]; i++) {
        if (!str_eq_c(name, retarget[i])) continue;
        skinned_retarget_types(prog);
        decl *const types[] = {prog->humanoid_rig, prog->humanoid_bone, prog->humanoid_status, prog->retarget_map, prog->retarget_error};
        *out = decl_type(types[i]);
        return true;
    }
    if (str_eq_c(name, "BlendShape")) {
        skinned_blend_shapes(prog);
        *out = decl_type(prog->blend_shape);
        return true;
    }
    static const char *const names[] = {"Animation", "Bone", "Pose", "Skeleton", "BoneTransform", "Playback", "AnimationCatalog"};
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
