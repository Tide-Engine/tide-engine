#include "tide/retarget.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/debug.h"
#include "tide/pack.h"
#include "tide/skinned_data.h"

// Plans contain only 32-bit scalars, not C pointers or allocation handles.
// They are copied, snapshotted and packed as ordinary immutable byte lists.
#define RETARGET_MAGIC 0x31544752u

enum { TRANSFER_NONE, TRANSFER_BONE, TRANSFER_SPINE, TRANSFER_TWIST };

typedef struct retarget_header {
    uint32_t magic, source_count, target_count;
    int32_t source_hips, target_hips;
    uint8_t source_hash[32], target_hash[32];
    tide_quaternion heading;
    tide_float3 up, source_position, target_position;
} retarget_header;

typedef struct retarget_source {
    int32_t parent;
    float scale;
} retarget_source;

typedef struct retarget_target {
    int32_t parent, source_a, source_b, mode;
    float fraction;
    tide_quaternion inverse_a, inverse_b, aligned;
    tide_float3 axis;
    tide_bone_transform local;
} retarget_target;

_Static_assert(sizeof(retarget_header) == 136u, "retarget header has no padding");
_Static_assert(sizeof(retarget_source) == 8u, "retarget source has no padding");
_Static_assert(sizeof(retarget_target) == 120u, "retarget target has no padding");
_Static_assert(TIDE_LIST_CHUNK >= TIDE_SKINNED_SHAPED_HEADER_SIZE, "identity header fits one list chunk");

typedef struct rig_node {
    tide_skinned_node_data info;
    tide_bone_transform local;
    tide_quaternion rotation;
    tide_float3 position;
    float scale;
} rig_node;

typedef struct rig_seen {
    tide_skinned_data_view view;
    tide_list data;
    void *bytes;
    bool copied;
    rig_node *nodes;
} rig_seen;

static bool finite3(const tide_float3 p)
{
    return tide_is_finite_f(p.x) && tide_is_finite_f(p.y) && tide_is_finite_f(p.z);
}

static bool finiteq(const tide_quaternion q)
{
    return tide_is_finite_f(q.value.x) && tide_is_finite_f(q.value.y)
        && tide_is_finite_f(q.value.z) && tide_is_finite_f(q.value.w);
}

static tide_quaternion normalized(const tide_quaternion q)
{
    const float largest = tide_cmax_f4(tide_abs_f4(q.value));
    return largest > 0.0f ? tide_normalize_q((tide_quaternion){tide_div_f4(q.value, tide_f4_splat(largest))})
                         : tide_identity_q();
}

static bool uniform(const tide_float3 scale)
{
    return finite3(scale) && scale.x > 0.0f
        && tide_abs_f(scale.x - scale.y) <= scale.x * 0.00001f
        && tide_abs_f(scale.x - scale.z) <= scale.x * 0.00001f;
}

static bool valid_local(const tide_bone_transform t)
{
    return finite3(t.position) && finiteq(t.rotation) && uniform(t.scale);
}

static tide_bone_transform node_local(const tide_skinned_node_data *n)
{
    return (tide_bone_transform){tide_f3(n->position[0], n->position[1], n->position[2]),
        tide_q(n->rotation[0], n->rotation[1], n->rotation[2], n->rotation[3]),
        tide_f3(n->scale[0], n->scale[1], n->scale[2])};
}

static void close_rig(rig_seen *r)
{
    free(r->nodes);
    if (r->bytes) tide_list_unflatten(r->data, r->bytes, 1u, false, r->copied);
    memset(r, 0, sizeof *r);
}

static int32_t open_rig(const tide_list data, const tide_list reference, const bool use_reference, rig_seen *r)
{
    memset(r, 0, sizeof *r);
    r->data = data;
    int32_t count;
    if (tide_pack_holds(data.at)) {
        const uint32_t offset = data.at & 0x3fffffffu;
        tide_block block;
        const void *span = tide_pack_span(offset, sizeof block);
        if (!span) return TIDE_RETARGET_INVALID_RIG;
        memcpy(&block, span, sizeof block);
        if (block.a != block.b || block.a > INT32_MAX) return TIDE_RETARGET_INVALID_RIG;
        count = (int32_t)block.a;
        r->bytes = (void *)tide_pack_span(offset + (uint32_t)sizeof block, block.a);
    } else {
        count = tide_list_count(data);
        if (count < (int32_t)TIDE_SKINNED_HEADER_SIZE) return TIDE_RETARGET_INVALID_RIG;
        r->bytes = tide_list_flatten(data, 1u, false, &r->copied);
    }
    if (!r->bytes || !tide_skinned_data_open(r->bytes, (size_t)count, &r->view)
        || !r->view.nodes || r->view.nodes > TIDE_SKINNED_MAX_NODES + 1u) return TIDE_RETARGET_INVALID_RIG;
    if (use_reference && tide_list_count(reference) != (int32_t)r->view.nodes) return TIDE_RETARGET_INVALID_REFERENCE_POSE;
    r->nodes = tide_alloc_zeroed(r->view.nodes, sizeof *r->nodes);
    tide_list_cache cache = {0};
    for (uint32_t i = 0; i < r->view.nodes; i++) {
        rig_node *n = &r->nodes[i];
        if (!tide_skinned_data_node(&r->view, i, &n->info)
            || (i ? n->info.parent < 0 || (uint32_t)n->info.parent >= i : n->info.parent != -1)) return TIDE_RETARGET_INVALID_RIG;
        if (use_reference) {
            const tide_bone_transform *local = tide_list_cached_at(&cache, reference, (int32_t)i, sizeof n->local);
            if (!local) return TIDE_RETARGET_INVALID_REFERENCE_POSE;
            n->local = *local;
        } else n->local = node_local(&n->info);
        if (!uniform(n->local.scale)) return TIDE_RETARGET_UNSUPPORTED_SCALE;
        if (!valid_local(n->local)) return TIDE_RETARGET_INVALID_REFERENCE_POSE;
        n->rotation = normalized(n->local.rotation);
        n->position = n->local.position;
        n->scale = n->local.scale.x;
        if (i) {
            const rig_node *p = &r->nodes[n->info.parent];
            n->position = tide_add_f3(p->position, tide_rotate_q(p->rotation,
                tide_mul_f3(tide_f3_splat(p->scale), n->position)));
            n->rotation = normalized(tide_mul_q(p->rotation, n->rotation));
            n->scale *= p->scale;
        }
        if (!finite3(n->position) || !finiteq(n->rotation) || !tide_is_finite_f(n->scale) || n->scale <= 0.0f)
            return TIDE_RETARGET_INVALID_REFERENCE_POSE;
    }
    return TIDE_RETARGET_OK;
}

static const char *const role_names[TIDE_HUMANOID_COUNT] = {
    "Hips", "LeftUpperLeg", "RightUpperLeg", "LeftLowerLeg", "RightLowerLeg", "LeftFoot", "RightFoot",
    "Spine", "Chest", "Neck", "Head", "LeftShoulder", "RightShoulder", "LeftUpperArm", "RightUpperArm",
    "LeftLowerArm", "RightLowerArm", "LeftHand", "RightHand", "LeftToes", "RightToes", "LeftEye", "RightEye", "Jaw",
    "LeftThumbProximal", "LeftThumbIntermediate", "LeftThumbDistal", "LeftIndexProximal", "LeftIndexIntermediate", "LeftIndexDistal",
    "LeftMiddleProximal", "LeftMiddleIntermediate", "LeftMiddleDistal", "LeftRingProximal", "LeftRingIntermediate", "LeftRingDistal",
    "LeftLittleProximal", "LeftLittleIntermediate", "LeftLittleDistal", "RightThumbProximal", "RightThumbIntermediate", "RightThumbDistal",
    "RightIndexProximal", "RightIndexIntermediate", "RightIndexDistal", "RightMiddleProximal", "RightMiddleIntermediate", "RightMiddleDistal",
    "RightRingProximal", "RightRingIntermediate", "RightRingDistal", "RightLittleProximal", "RightLittleIntermediate", "RightLittleDistal", "UpperChest"
};

static int32_t refusal(const int32_t error, const char *side, const int role, const char *why)
{
    char text[512];
    snprintf(text, sizeof text, "tide: RetargetMap.Create: %s%s%s %s", side,
        role >= 0 ? " " : "", role >= 0 ? role_names[role] : "", why);
    tide_debug_log(text);
    return error;
}

static bool descendant(const rig_seen *r, int32_t node, const int32_t ancestor)
{
    if (node < 0 || ancestor < 0) return false;
    while (node > ancestor) node = r->nodes[node].info.parent;
    return node == ancestor;
}

static bool has(const char *s, const char *part) { return strstr(s, part) != NULL; }

// Drop namespaces and common exporter prefixes. Side markers are read before
// punctuation is discarded, so thigh_l and thigh.r retain their distinction.
static int normalized_name(const tide_skinned_node_data *n, char out[128])
{
    uint32_t first = 0;
    for (uint32_t i = 0; i < n->name_length; i++) if (n->name[i] == ':' || n->name[i] == '|') first = i + 1u;
    char lower[256];
    const uint32_t length = n->name_length - first;
    if (length >= sizeof lower) { out[0] = 0; return 0; }
    for (uint32_t i = 0; i < length; i++) {
        const unsigned char c = (unsigned char)n->name[first + i];
        lower[i] = c >= 'A' && c <= 'Z' ? (char)(c + ('a' - 'A')) : (char)c;
    }
    lower[length] = 0;
    // Exported mesh objects often repeat bone aliases (mannequin_geo_pelvis).
    // Explicit mesh/geometry tokens are not anatomical candidates.
    for (uint32_t i = 0; i < length;) {
        const uint32_t begin = i;
        while (i < length && lower[i] >= 'a' && lower[i] <= 'z') i++;
        const uint32_t token = i - begin;
        if ((token == 3 && !memcmp(lower + begin, "geo", 3u))
            || (token == 4 && !memcmp(lower + begin, "mesh", 4u))
            || (token == 8 && !memcmp(lower + begin, "geometry", 8u))) { out[0] = 0; return 0; }
        if (i < length) i++;
    }
    int side = has(lower, "left") ? 1 : has(lower, "right") ? 2 : 0;
    for (uint32_t i = 0; !side && i < length; i++) {
        if ((lower[i] == 'l' || lower[i] == 'r')
            && (i == 0 || lower[i - 1u] == '_' || lower[i - 1u] == '.' || lower[i - 1u] == ' ' || lower[i - 1u] == '-')
            && (i + 1u == length || lower[i + 1u] == '_' || lower[i + 1u] == '.' || lower[i + 1u] == ' ' || lower[i + 1u] == '-'))
            side = lower[i] == 'l' ? 1 : 2;
    }
    uint32_t used = 0;
    for (uint32_t i = 0; i < length && used + 1u < 128u; i++) {
        if ((lower[i] >= 'a' && lower[i] <= 'z') || (lower[i] >= '0' && lower[i] <= '9')) out[used++] = lower[i];
        else if (used && out[used - 1u] >= '0' && out[used - 1u] <= '9'
            && i + 1u < length && lower[i + 1u] >= '0' && lower[i + 1u] <= '9') out[used++] = '_';
    }
    out[used] = 0;
    const char *prefixes[] = {"mixamorig", "bip001", "bip01", "bip", "bone", "def"};
    for (uint32_t p = 0; p < sizeof prefixes / sizeof prefixes[0]; p++) {
        const size_t k = strlen(prefixes[p]);
        if (strncmp(out, prefixes[p], k) == 0) { memmove(out, out + k, strlen(out + k) + 1u); break; }
    }
    if (!side && (out[0] == 'l' || out[0] == 'r') && (has(out + 1, "arm") || has(out + 1, "leg") || has(out + 1, "hand") || has(out + 1, "foot")))
        side = out[0] == 'l' ? 1 : 2;
    return side;
}

static bool ignored_name(const char *name)
{
    return strncmp(name, "ik", 2u) == 0 || has(name, "twist") || has(name, "roll")
        || has(name, "helper") || has(name, "attach") || has(name, "abacdyn") || has(name, "end");
}

static bool generic_finger(const char *name)
{
    return !strncmp(name, "finger", 6u) && !has(name, "thumb") && !has(name, "index")
        && !has(name, "middle") && !has(name, "ring") && !has(name, "little") && !has(name, "pinky");
}

static int finger_role(const char *name, const int side)
{
    if (!side) return -1;
    const char *names[] = {"thumb", "index", "middle", "ring", "little"};
    int finger = -1;
    for (int i = 0; i < 5; i++) if (has(name, names[i]) || (i == 4 && has(name, "pinky"))) { finger = i; break; }
    if (finger < 0 && generic_finger(name)) finger = 2; // Synty mitten's middle chain.
    if (finger < 0) return -1;
    int segment = has(name, "proximal") ? 0 : has(name, "intermediate") ? 1 : has(name, "distal") ? 2 : -1;
    if (segment < 0) for (const char *p = name; *p; p++) if (*p >= '0' && *p <= '9') {
        int number = 0;
        do { number = number < 100 ? number * 10 + (*p - '0') : 100; p++; } while (*p >= '0' && *p <= '9');
        if (number >= 1 && number <= 3) segment = number - 1;
        break;
    }
    if (segment < 0) return -1;
    return TIDE_HUMANOID_LEFT_THUMB_PROXIMAL + (side - 1) * 15 + finger * 3 + segment;
}

static int named_role(const char *name, const int side)
{
    if (!*name || ignored_name(name)) return -1;
    const int finger = finger_role(name, side);
    if (finger >= 0) return finger;
    if (side) {
        const int pair = side - 1;
        if (has(name, "clavicle") || has(name, "shoulder")) return TIDE_HUMANOID_LEFT_SHOULDER + pair;
        if (has(name, "forearm") || has(name, "lowerarm") || has(name, "elbow")) return TIDE_HUMANOID_LEFT_LOWER_ARM + pair;
        if (has(name, "upperarm") || has(name, "arm")) return TIDE_HUMANOID_LEFT_UPPER_ARM + pair;
        if (has(name, "hand") || has(name, "wrist")) return TIDE_HUMANOID_LEFT_HAND + pair;
        if (has(name, "upleg") || has(name, "upperleg") || has(name, "thigh")) return TIDE_HUMANOID_LEFT_UPPER_LEG + pair;
        if (has(name, "lowerleg") || has(name, "calf") || has(name, "shin") || has(name, "knee") || has(name, "leg")) return TIDE_HUMANOID_LEFT_LOWER_LEG + pair;
        if (has(name, "toe") || has(name, "ball")) return TIDE_HUMANOID_LEFT_TOES + pair;
        if (has(name, "foot") || has(name, "ankle")) return TIDE_HUMANOID_LEFT_FOOT + pair;
        if (has(name, "eye")) return TIDE_HUMANOID_LEFT_EYE + pair;
    }
    if (has(name, "pelvis") || has(name, "hips")) return TIDE_HUMANOID_HIPS;
    if (has(name, "upperchest")) return TIDE_HUMANOID_UPPER_CHEST;
    if (has(name, "chest")) return TIDE_HUMANOID_CHEST;
    if (has(name, "neck")) return TIDE_HUMANOID_NECK;
    if (has(name, "head")) return TIDE_HUMANOID_HEAD;
    if (has(name, "jaw")) return TIDE_HUMANOID_JAW;
    return -1;
}

static void choose(int32_t bones[TIDE_HUMANOID_COUNT], int32_t status[TIDE_HUMANOID_COUNT], const int role, const int32_t node)
{
    if (role < 0) return;
    if (status[role] == TIDE_HUMANOID_MISSING) { bones[role] = node; status[role] = TIDE_HUMANOID_AUTOMATIC; }
    else if (bones[role] != node) { bones[role] = -1; status[role] = TIDE_HUMANOID_UNCERTAIN; }
}

static void choose_named(const rig_seen *rig, int32_t *bones, int32_t *status, const int role, const int32_t node)
{
    // Ball -> Toes is one forefoot chain, whose proximal joint drives toe bend.
    if ((role == TIDE_HUMANOID_LEFT_TOES || role == TIDE_HUMANOID_RIGHT_TOES) && bones[role] >= 0) {
        if (descendant(rig, node, bones[role])) return;
        if (descendant(rig, bones[role], node)) { bones[role] = node; return; }
    }
    choose(bones, status, role, node);
}

static bool role_required(const int role)
{
    return role <= TIDE_HUMANOID_RIGHT_FOOT || role == TIDE_HUMANOID_HEAD
        || (role >= TIDE_HUMANOID_LEFT_UPPER_ARM && role <= TIDE_HUMANOID_RIGHT_HAND);
}

static int parent_role(const int role)
{
    if (role >= TIDE_HUMANOID_LEFT_THUMB_PROXIMAL && role < TIDE_HUMANOID_UPPER_CHEST) {
        const int f = role - TIDE_HUMANOID_LEFT_THUMB_PROXIMAL;
        return f % 3 ? role - 1 : TIDE_HUMANOID_LEFT_HAND + f / 15;
    }
    switch (role) {
    case TIDE_HUMANOID_LEFT_UPPER_LEG: case TIDE_HUMANOID_RIGHT_UPPER_LEG: case TIDE_HUMANOID_SPINE: return TIDE_HUMANOID_HIPS;
    case TIDE_HUMANOID_LEFT_LOWER_LEG: case TIDE_HUMANOID_RIGHT_LOWER_LEG: return role - 2;
    case TIDE_HUMANOID_LEFT_FOOT: case TIDE_HUMANOID_RIGHT_FOOT: return role - 2;
    case TIDE_HUMANOID_CHEST: return TIDE_HUMANOID_SPINE;
    case TIDE_HUMANOID_UPPER_CHEST: return TIDE_HUMANOID_CHEST;
    case TIDE_HUMANOID_NECK: case TIDE_HUMANOID_LEFT_SHOULDER: case TIDE_HUMANOID_RIGHT_SHOULDER: return TIDE_HUMANOID_UPPER_CHEST;
    case TIDE_HUMANOID_HEAD: return TIDE_HUMANOID_NECK;
    case TIDE_HUMANOID_LEFT_UPPER_ARM: case TIDE_HUMANOID_RIGHT_UPPER_ARM: return role - 2;
    case TIDE_HUMANOID_LEFT_LOWER_ARM: case TIDE_HUMANOID_RIGHT_LOWER_ARM: return role - 2;
    case TIDE_HUMANOID_LEFT_HAND: case TIDE_HUMANOID_RIGHT_HAND: return role - 2;
    case TIDE_HUMANOID_LEFT_TOES: case TIDE_HUMANOID_RIGHT_TOES: return role - 14;
    case TIDE_HUMANOID_LEFT_EYE: case TIDE_HUMANOID_RIGHT_EYE: case TIDE_HUMANOID_JAW: return TIDE_HUMANOID_HEAD;
    default: return -1;
    }
}

bool tide_humanoid_auto(const tide_list data, const tide_list reference, tide_list *bones, tide_list *status, const uint32_t where)
{
    if (!bones || !status || bones == status) return false;
    rig_seen rig;
    const int32_t error = open_rig(data, reference, true, &rig);
    if (error) { close_rig(&rig); return false; }
    int32_t found[TIDE_HUMANOID_COUNT], states[TIDE_HUMANOID_COUNT] = {0};
    for (int i = 0; i < TIDE_HUMANOID_COUNT; i++) found[i] = -1;
    int32_t *spine = tide_alloc((size_t)rig.view.nodes * sizeof *spine);
    int32_t spine_count = 0;
    for (uint32_t i = 1; i < rig.view.nodes; i++) {
        char name[128];
        const int side = normalized_name(&rig.nodes[i].info, name);
        if (!side && has(name, "spine") && !ignored_name(name)) spine[spine_count++] = (int32_t)i;
        else if (!has(name, "shoulder") && !generic_finger(name))
            choose_named(&rig, found, states, named_role(name, side), (int32_t)i);
    }
    if (spine_count) {
        bool chain = true;
        for (int32_t i = 1; i < spine_count; i++) if (!descendant(&rig, spine[i], spine[i - 1])) chain = false;
        if (chain) {
            choose(found, states, TIDE_HUMANOID_SPINE, spine[0]);
            if (spine_count > 1 && states[TIDE_HUMANOID_CHEST] == TIDE_HUMANOID_MISSING)
                choose(found, states, TIDE_HUMANOID_CHEST, spine[spine_count > 2 ? spine_count - 2 : 1]);
            if (spine_count > 2 && states[TIDE_HUMANOID_UPPER_CHEST] == TIDE_HUMANOID_MISSING)
                choose(found, states, TIDE_HUMANOID_UPPER_CHEST, spine[spine_count - 1]);
        } else states[TIDE_HUMANOID_SPINE] = TIDE_HUMANOID_UNCERTAIN;
    }
    free(spine);
    // Mixamo's Shoulder is the clavicle; Synty's Shoulder is the upper arm
    // beneath a separately named Clavicle. Resolve this after explicit names.
    for (uint32_t i = 1; i < rig.view.nodes; i++) {
        char name[128];
        const int side = normalized_name(&rig.nodes[i].info, name);
        if (!side || !has(name, "shoulder") || ignored_name(name)) continue;
        const int shoulder = TIDE_HUMANOID_LEFT_SHOULDER + side - 1;
        const int arm = TIDE_HUMANOID_LEFT_UPPER_ARM + side - 1;
        const int role = found[shoulder] >= 0 && descendant(&rig, (int32_t)i, found[shoulder]) ? arm : shoulder;
        choose(found, states, role, (int32_t)i);
    }
    // Unlabelled finger sides are fixed by their hand ancestry (including
    // Synty's right-hand _1 suffix, which is not a segment number).
    int32_t *hand_side = tide_alloc_zeroed(rig.view.nodes, sizeof *hand_side);
    int32_t mitten[2][3] = {{-1, -1, -1}, {-1, -1, -1}};
    bool mitten_ambiguous[2] = {false, false};
    for (uint32_t i = 1; i < rig.view.nodes; i++) {
        hand_side[i] = hand_side[rig.nodes[i].info.parent];
        if ((int32_t)i == found[TIDE_HUMANOID_LEFT_HAND]) hand_side[i] = 1;
        if ((int32_t)i == found[TIDE_HUMANOID_RIGHT_HAND]) hand_side[i] = 2;
        char name[128];
        int side = normalized_name(&rig.nodes[i].info, name);
        if (ignored_name(name)) continue;
        if (!side) side = hand_side[i];
        if (!side) continue;
        const int role = finger_role(name, side);
        if (generic_finger(name) && role >= 0) {
            const int segment = (role - TIDE_HUMANOID_LEFT_THUMB_PROXIMAL) % 3;
            if (mitten[side - 1][segment] >= 0) mitten_ambiguous[side - 1] = true;
            else mitten[side - 1][segment] = (int32_t)i;
        } else choose(found, states, role, (int32_t)i);
    }
    free(hand_side);
    for (int side = 0; side < 2; side++) {
        int32_t parent = found[TIDE_HUMANOID_LEFT_HAND + side];
        for (int segment = 0; segment < 3; segment++) if (mitten[side][segment] >= 0) {
            if (parent < 0 || !descendant(&rig, mitten[side][segment], parent)) mitten_ambiguous[side] = true;
            parent = mitten[side][segment];
        }
        for (int segment = 0; segment < 3; segment++) if (mitten[side][segment] >= 0) {
            const int role = TIDE_HUMANOID_LEFT_MIDDLE_PROXIMAL + side * 15 + segment;
            if (mitten_ambiguous[side]) { found[role] = -1; states[role] = TIDE_HUMANOID_UNCERTAIN; }
            else choose(found, states, role, mitten[side][segment]);
        }
    }
    // A known left/right pair establishes a body-relative side axis. Use it
    // for anatomically named but unsided limbs; never guess handedness from
    // an arbitrary model-space X direction or overwrite an explicit side.
    tide_float3 right = tide_f3_splat(0.0f);
    const int pairs[] = {TIDE_HUMANOID_LEFT_UPPER_LEG, TIDE_HUMANOID_LEFT_UPPER_ARM, TIDE_HUMANOID_LEFT_SHOULDER};
    for (uint32_t p = 0; p < sizeof pairs / sizeof pairs[0]; p++) {
        if (found[pairs[p]] < 0 || found[pairs[p] + 1] < 0) continue;
        right = tide_fromto_unit_f3(tide_sub_f3(rig.nodes[found[pairs[p] + 1]].position, rig.nodes[found[pairs[p]]].position));
        if (tide_dot_f3(right, right) > 0.0f) break;
    }
    if (found[TIDE_HUMANOID_HIPS] >= 0 && tide_dot_f3(right, right) > 0.0f) {
        const tide_float3 origin = rig.nodes[found[TIDE_HUMANOID_HIPS]].position;
        for (uint32_t i = 1; i < rig.view.nodes; i++) {
            char name[128];
            if (normalized_name(&rig.nodes[i].info, name) || ignored_name(name)) continue;
            const float side_at = tide_dot_f3(tide_sub_f3(rig.nodes[i].position, origin), right);
            if (tide_abs_f(side_at) <= 0.00001f) continue;
            const int role = named_role(name, side_at < 0.0f ? 1 : 2);
            if ((role >= TIDE_HUMANOID_LEFT_UPPER_LEG && role <= TIDE_HUMANOID_RIGHT_FOOT)
                || (role >= TIDE_HUMANOID_LEFT_SHOULDER && role <= TIDE_HUMANOID_RIGHT_EYE))
                choose_named(&rig, found, states, role, (int32_t)i);
        }
    }
    // Hierarchy can resolve an unnamed middle limb joint once its two named
    // endpoints are known. Extra twist/helper nodes prevent that inference.
    const int middles[] = {TIDE_HUMANOID_LEFT_LOWER_ARM, TIDE_HUMANOID_RIGHT_LOWER_ARM,
        TIDE_HUMANOID_LEFT_LOWER_LEG, TIDE_HUMANOID_RIGHT_LOWER_LEG};
    for (uint32_t k = 0; k < sizeof middles / sizeof middles[0]; k++) {
        const int role = middles[k], first = role - 2, last = role + 2;
        if (states[role] != TIDE_HUMANOID_MISSING || found[first] < 0 || found[last] < 0) continue;
        int32_t candidate = -1;
        for (int32_t n = rig.nodes[found[last]].info.parent; n > found[first]; n = rig.nodes[n].info.parent) {
            char name[128]; normalized_name(&rig.nodes[n].info, name);
            if (ignored_name(name)) continue;
            if (candidate >= 0) { candidate = -2; break; }
            candidate = n;
        }
        if (candidate >= 0 && descendant(&rig, candidate, found[first])) choose(found, states, role, candidate);
    }
    // Anatomically inconsistent named matches remain visible as uncertainty,
    // never a guessed correspondence that silently flips or crosses limbs.
    for (int role = 0; role < TIDE_HUMANOID_COUNT; role++) if (found[role] >= 0) {
        int parent = parent_role(role);
        while (parent >= 0 && found[parent] < 0) parent = parent_role(parent);
        if (parent >= 0 && !descendant(&rig, found[role], found[parent])) states[role] = TIDE_HUMANOID_UNCERTAIN;
    }
    tide_list_set(bones, tide_list_from(found, TIDE_HUMANOID_COUNT, sizeof *found), sizeof *found, where);
    tide_list_set(status, tide_list_from(states, TIDE_HUMANOID_COUNT, sizeof *states), sizeof *states, where);
    close_rig(&rig);
    return true;
}

static int32_t read_roles(const rig_seen *r, const tide_list bones, const tide_list status,
                           int32_t roles[TIDE_HUMANOID_COUNT], const char *side)
{
    if (tide_list_count(bones) != TIDE_HUMANOID_COUNT || tide_list_count(status) != TIDE_HUMANOID_COUNT)
        return refusal(TIDE_RETARGET_INVALID_RIG, side, -1, "has no role table; call HumanoidRig.Auto(referencePose) or assign its bones first");
    for (int role = 0; role < TIDE_HUMANOID_COUNT; role++) {
        roles[role] = *(const int32_t *)tide_list_at(bones, role, sizeof(int32_t));
        const int32_t state = *(const int32_t *)tide_list_at(status, role, sizeof(int32_t));
        if (roles[role] < -1 || roles[role] == 0 || ((uint32_t)roles[role] >= r->view.nodes && roles[role] != -1))
            return refusal(TIDE_RETARGET_INVALID_RIG, side, role, "does not name a bone in its reference skeleton; assign this role again");
        if (state < TIDE_HUMANOID_MISSING || state > TIDE_HUMANOID_MANUAL)
            return refusal(TIDE_RETARGET_INVALID_RIG, side, role, "has an invalid assignment status; assign this role again");
        if (state == TIDE_HUMANOID_UNCERTAIN) {
            if (role_required(role)) return refusal(TIDE_RETARGET_AMBIGUOUS_BONE, side, role, "is uncertain; assign rig[HumanoidBone] explicitly");
            roles[role] = -1;
        }
        if (roles[role] < 0 && role_required(role))
            return refusal(TIDE_RETARGET_MISSING_BONE, side, role, "is missing; assign rig[HumanoidBone] explicitly");
        if (roles[role] >= 0) for (int other = 0; other < role; other++) if (roles[other] == roles[role])
            return refusal(TIDE_RETARGET_INVALID_RIG, side, role, "shares a bone with another role; assign different bones or leave the optional role unmapped");
    }
    for (int role = 0; role < TIDE_HUMANOID_COUNT; role++) if (roles[role] >= 0) {
        int parent = parent_role(role);
        while (parent >= 0 && roles[parent] < 0) parent = parent_role(parent);
        if (parent >= 0 && !descendant(r, roles[role], roles[parent]))
            return refusal(TIDE_RETARGET_INVALID_RIG, side, role, "is outside its anatomical parent chain; correct its mapping");
    }
    return TIDE_RETARGET_OK;
}

typedef struct named_node { const char *name; uint32_t length; int32_t index; } named_node;

static int name_compare(const named_node a, const named_node b)
{
    const uint32_t count = a.length < b.length ? a.length : b.length;
    const int difference = count ? memcmp(a.name, b.name, count) : 0;
    return difference ? difference : a.length < b.length ? -1 : a.length > b.length ? 1 : 0;
}

// Stable bottom-up merge sort bounds hostile names to O(n log n) comparisons.
static named_node *names_of(const rig_seen *r)
{
    const uint32_t count = r->view.nodes;
    named_node *a = tide_alloc((size_t)count * sizeof *a);
    named_node *b = tide_alloc((size_t)count * sizeof *b);
    for (uint32_t i = 0; i < count; i++) a[i] = (named_node){r->nodes[i].info.name, r->nodes[i].info.name_length, (int32_t)i};
    for (uint32_t width = 1; width < count; width *= 2u) {
        for (uint32_t first = 0; first < count; first += width * 2u) {
            const uint32_t mid = first + width < count ? first + width : count;
            const uint32_t last = mid + width < count ? mid + width : count;
            uint32_t left = first, right = mid;
            for (uint32_t out = first; out < last; out++)
                b[out] = right == last || (left < mid && name_compare(a[left], a[right]) <= 0) ? a[left++] : a[right++];
        }
        named_node *swap = a; a = b; b = swap;
    }
    free(b);
    return a;
}

static int32_t named_find(const named_node *names, const uint32_t count, const tide_skinned_node_data *node)
{
    if (!node->name_length) return -1;
    const named_node key = {node->name, node->name_length, 0};
    uint32_t lo = 0, hi = count;
    while (lo < hi) {
        const uint32_t mid = lo + (hi - lo) / 2u;
        if (name_compare(names[mid], key) < 0) lo = mid + 1u; else hi = mid;
    }
    if (lo == count || name_compare(names[lo], key)) return -1;
    if (lo + 1u < count && !name_compare(names[lo + 1u], key)) return -2;
    return names[lo].index;
}

static int32_t match_export(const rig_seen *actual, const rig_seen *reference,
                             int32_t roles[TIDE_HUMANOID_COUNT], int32_t *mapping)
{
    const bool identical = actual->view.nodes == reference->view.nodes
        && !memcmp(actual->view.skeleton_hash, reference->view.skeleton_hash, 32u);
    named_node *actual_names = identical ? NULL : names_of(actual);
    named_node *reference_names = identical ? NULL : names_of(reference);
    int32_t result = TIDE_RETARGET_OK;
    int32_t *reverse = tide_alloc((size_t)actual->view.nodes * sizeof *reverse);
    for (uint32_t i = 0; i < actual->view.nodes; i++) reverse[i] = -1;
    for (uint32_t i = 0; i < reference->view.nodes; i++) {
        mapping[i] = identical ? (int32_t)i : i == 0u ? 0 : named_find(actual_names, actual->view.nodes, &reference->nodes[i].info);
        if (!identical && i && named_find(reference_names, reference->view.nodes, &reference->nodes[i].info) == -2) mapping[i] = -2;
        if (mapping[i] >= 0) reverse[mapping[i]] = (int32_t)i;
    }
    for (int role = 0; role < TIDE_HUMANOID_COUNT; role++) if (roles[role] >= 0 && mapping[roles[role]] < 0) {
        if (!role_required(role)) { roles[role] = -1; continue; }
        result = refusal(mapping[roles[role]] == -2 ? TIDE_RETARGET_AMBIGUOUS_BONE : TIDE_RETARGET_INCOMPATIBLE_SKELETON,
            "animation export", role, "has no unique bone matching the source reference; use its original rig or correct the source assignment");
        break;
    }
    if (!result) {
        int32_t *reference_parent = tide_alloc((size_t)reference->view.nodes * sizeof *reference_parent);
        int32_t *actual_parent = tide_alloc((size_t)actual->view.nodes * sizeof *actual_parent);
        for (uint32_t i = 0; i < reference->view.nodes; i++)
            reference_parent[i] = mapping[i] >= 0 ? (int32_t)i : i ? reference_parent[reference->nodes[i].info.parent] : -1;
        for (uint32_t i = 0; i < actual->view.nodes; i++)
            actual_parent[i] = reverse[i] >= 0 ? reverse[i] : i ? actual_parent[actual->nodes[i].info.parent] : -1;
        for (int role = 0; !result && role < TIDE_HUMANOID_COUNT; role++) if (roles[role] >= 0) {
            for (int32_t n = roles[role]; n > 0; n = reference->nodes[n].info.parent) if (mapping[n] >= 0) {
                const int32_t a = mapping[n];
                if (!a || reference_parent[reference->nodes[n].info.parent] != actual_parent[actual->nodes[a].info.parent]) {
                    result = refusal(TIDE_RETARGET_INCOMPATIBLE_SKELETON, "animation export", role,
                        "has a different named ancestor chain from its source reference; use a reference from that rig");
                    break;
                }
            }
        }
        free(actual_parent);
        free(reference_parent);
    }
    free(reverse);
    free(reference_names);
    free(actual_names);
    return result;
}

static float distance3(const tide_float3 a, const tide_float3 b)
{
    const tide_float3 d = tide_sub_f3(a, b);
    const float largest = tide_cmax_f3(tide_abs_f3(d));
    return largest > 0.0f && tide_is_finite_f(largest)
        ? largest * tide_length_f3(tide_div_f3(d, tide_f3_splat(largest))) : largest;
}

static bool body_frame(const rig_seen *r, const int32_t roles[TIDE_HUMANOID_COUNT], tide_quaternion *frame)
{
    const tide_float3 up = tide_fromto_unit_f3(tide_sub_f3(r->nodes[roles[TIDE_HUMANOID_HEAD]].position, r->nodes[roles[TIDE_HUMANOID_HIPS]].position));
    tide_float3 side = tide_sub_f3(r->nodes[roles[TIDE_HUMANOID_RIGHT_UPPER_LEG]].position, r->nodes[roles[TIDE_HUMANOID_LEFT_UPPER_LEG]].position);
    side = tide_fromto_unit_f3(tide_sub_f3(side, tide_mul_f3(up, tide_f3_splat(tide_dot_f3(side, up)))));
    if (tide_dot_f3(up, up) < 0.5f || tide_dot_f3(side, side) < 0.5f) return false;
    *frame = tide_lookrotation_q(tide_cross_f3(side, up), up);
    return finiteq(*frame);
}

static float leg_length(const rig_seen *r, const int32_t roles[TIDE_HUMANOID_COUNT])
{
    float total = 0.0f;
    for (int side = 0; side < 2; side++) {
        const tide_float3 a = r->nodes[roles[TIDE_HUMANOID_LEFT_UPPER_LEG + side]].position;
        const tide_float3 b = r->nodes[roles[TIDE_HUMANOID_LEFT_LOWER_LEG + side]].position;
        const tide_float3 c = r->nodes[roles[TIDE_HUMANOID_LEFT_FOOT + side]].position;
        const float upper = distance3(a, b), lower = distance3(b, c);
        if (!(upper > 0.0f) || !(lower > 0.0f) || !tide_is_finite_f(upper) || !tide_is_finite_f(lower)) return 0.0f;
        total += (upper + lower) * 0.5f;
    }
    return total;
}

static int child_role(const int role)
{
    if (role >= TIDE_HUMANOID_LEFT_THUMB_PROXIMAL && role < TIDE_HUMANOID_UPPER_CHEST)
        return (role - TIDE_HUMANOID_LEFT_THUMB_PROXIMAL) % 3 < 2 ? role + 1 : -1;
    switch (role) {
    case TIDE_HUMANOID_LEFT_SHOULDER: case TIDE_HUMANOID_RIGHT_SHOULDER:
    case TIDE_HUMANOID_LEFT_UPPER_ARM: case TIDE_HUMANOID_RIGHT_UPPER_ARM:
    case TIDE_HUMANOID_LEFT_LOWER_ARM: case TIDE_HUMANOID_RIGHT_LOWER_ARM:
    case TIDE_HUMANOID_LEFT_UPPER_LEG: case TIDE_HUMANOID_RIGHT_UPPER_LEG:
    case TIDE_HUMANOID_LEFT_LOWER_LEG: case TIDE_HUMANOID_RIGHT_LOWER_LEG: return role + 2;
    case TIDE_HUMANOID_LEFT_FOOT: case TIDE_HUMANOID_RIGHT_FOOT: return role + 14;
    case TIDE_HUMANOID_LEFT_HAND: return TIDE_HUMANOID_LEFT_MIDDLE_PROXIMAL;
    case TIDE_HUMANOID_RIGHT_HAND: return TIDE_HUMANOID_RIGHT_MIDDLE_PROXIMAL;
    case TIDE_HUMANOID_SPINE: return TIDE_HUMANOID_CHEST;
    case TIDE_HUMANOID_CHEST: return TIDE_HUMANOID_UPPER_CHEST;
    case TIDE_HUMANOID_UPPER_CHEST: return TIDE_HUMANOID_NECK;
    case TIDE_HUMANOID_NECK: return TIDE_HUMANOID_HEAD;
    default: return -1;
    }
}

static tide_quaternion align_role(const rig_seen *source, const rig_seen *target,
                                   const int32_t sr[TIDE_HUMANOID_COUNT], const int32_t tr[TIDE_HUMANOID_COUNT],
                                   const int role, const tide_quaternion heading)
{
    int child = child_role(role);
    while (child >= 0 && (sr[child] < 0 || tr[child] < 0)) child = child_role(child);
    if (child < 0) return tide_identity_q();
    const tide_float3 from = tide_sub_f3(target->nodes[tr[child]].position, target->nodes[tr[role]].position);
    const tide_float3 to = tide_rotate_q(heading, tide_sub_f3(source->nodes[sr[child]].position, source->nodes[sr[role]].position));
    tide_quaternion alignment = tide_fromto_q(from, to);
    // Two finger rays determine palm roll, which a single wrist-to-knuckle
    // ray cannot. Without them, retain reference roll for manual correction.
    if (role == TIDE_HUMANOID_LEFT_HAND || role == TIDE_HUMANOID_RIGHT_HAND) {
        const int index = role == TIDE_HUMANOID_LEFT_HAND ? TIDE_HUMANOID_LEFT_INDEX_PROXIMAL : TIDE_HUMANOID_RIGHT_INDEX_PROXIMAL;
        if (sr[index] >= 0 && tr[index] >= 0) {
            const tide_float3 target_ray = tide_rotate_q(alignment, tide_sub_f3(target->nodes[tr[index]].position, target->nodes[tr[role]].position));
            const tide_float3 source_ray = tide_rotate_q(heading, tide_sub_f3(source->nodes[sr[index]].position, source->nodes[sr[role]].position));
            const tide_float3 axis = tide_fromto_unit_f3(to);
            const tide_float3 a = tide_sub_f3(target_ray, tide_mul_f3(axis, tide_f3_splat(tide_dot_f3(target_ray, axis))));
            const tide_float3 b = tide_sub_f3(source_ray, tide_mul_f3(axis, tide_f3_splat(tide_dot_f3(source_ray, axis))));
            if (tide_dot_f3(a, a) > 1e-12f && tide_dot_f3(b, b) > 1e-12f)
                alignment = normalized(tide_mul_q(tide_fromto_q(a, b), alignment));
        }
    }
    return alignment;
}

static bool torso_role(const int role)
{
    return role == TIDE_HUMANOID_HIPS || role == TIDE_HUMANOID_SPINE || role == TIDE_HUMANOID_CHEST
        || role == TIDE_HUMANOID_UPPER_CHEST || role == TIDE_HUMANOID_NECK || role == TIDE_HUMANOID_HEAD;
}

static void transfer_anchor(retarget_target *to, const rig_seen *source, const int32_t *exported,
                            const int32_t a, const int32_t b, const float fraction, const int mode)
{
    to->source_a = exported[a]; to->source_b = exported[b];
    to->inverse_a = tide_conjugate_q(source->nodes[a].rotation);
    to->inverse_b = tide_conjugate_q(source->nodes[b].rotation);
    to->fraction = fraction; to->mode = mode;
}

static int32_t node_path(const rig_seen *r, const int32_t begin, int32_t end, int32_t *path)
{
    int32_t count = 0;
    while (end >= begin) {
        path[count++] = end;
        if (end == begin) break;
        end = r->nodes[end].info.parent;
    }
    if (!count || path[count - 1] != begin) return 0;
    for (int32_t i = 0; i < count / 2; i++) { const int32_t swap = path[i]; path[i] = path[count - i - 1]; path[count - i - 1] = swap; }
    return count;
}

static float path_distances(const rig_seen *r, const int32_t *path, const int32_t count, float *distances)
{
    float total = 0.0f;
    for (int32_t i = 0; i < count; i++) {
        if (i) total += distance3(r->nodes[path[i]].position, r->nodes[path[i - 1]].position);
        distances[i] = total;
    }
    return total;
}

static void prepare_chains(const rig_seen *source, const rig_seen *target, const int32_t *exported,
                            const int32_t sr[TIDE_HUMANOID_COUNT], const int32_t tr[TIDE_HUMANOID_COUNT],
                            const tide_quaternion heading, retarget_target *made)
{
    int32_t *sp = tide_alloc((size_t)source->view.nodes * sizeof *sp);
    int32_t *tp = tide_alloc((size_t)target->view.nodes * sizeof *tp);
    float *sd = tide_alloc((size_t)source->view.nodes * sizeof *sd);
    float *td = tide_alloc((size_t)target->view.nodes * sizeof *td);
    for (int end_role = 0; end_role < TIDE_HUMANOID_COUNT; end_role++) {
        if (sr[end_role] < 0 || tr[end_role] < 0) continue;
        int begin_role = parent_role(end_role);
        while (begin_role >= 0 && (sr[begin_role] < 0 || tr[begin_role] < 0)) begin_role = parent_role(begin_role);
        if (begin_role < 0) continue;
        const int32_t sn = node_path(source, sr[begin_role], sr[end_role], sp);
        const int32_t tn = node_path(target, tr[begin_role], tr[end_role], tp);
        if (sn < 2 || tn < 3) continue;
        const float sl = path_distances(source, sp, sn, sd), tl = path_distances(target, tp, tn, td);
        if (!(sl > 0.0f) || !(tl > 0.0f) || !tide_is_finite_f(sl) || !tide_is_finite_f(tl)) continue;
        const bool spine = torso_role(begin_role) && torso_role(end_role);
        const tide_float3 source_axis = tide_fromto_unit_f3(tide_sub_f3(source->nodes[sr[end_role]].position, source->nodes[sr[begin_role]].position));
        const tide_float3 target_axis = tide_fromto_unit_f3(tide_sub_f3(target->nodes[tr[end_role]].position, target->nodes[tr[begin_role]].position));
        int32_t a = 0, b = 1;
        while (b < sn - 1 && exported[sp[b]] < 0) b++;
        for (int32_t i = 1; i < tn - 1; i++) {
            retarget_target *to = &made[tp[i]];
            if (to->mode) continue;
            const float fraction = td[i] / tl;
            if (spine) {
                const float at = fraction * sl;
                while (b < sn - 1 && sd[b] <= at) {
                    a = b++;
                    while (b < sn - 1 && exported[sp[b]] < 0) b++;
                }
                const float span = sd[b] - sd[a];
                const float blend = span > 0.0f ? tide_saturate_f((at - sd[a]) / span) : 0.0f;
                transfer_anchor(to, source, exported, sp[a], sp[b], blend, TRANSFER_SPINE);
                const tide_float3 source_direction = tide_sub_f3(source->nodes[sp[b]].position, source->nodes[sp[a]].position);
                const tide_float3 target_direction = tide_sub_f3(target->nodes[tp[i + 1]].position, target->nodes[tp[i]].position);
                to->aligned = normalized(tide_mul_q(tide_fromto_q(target_direction, tide_rotate_q(heading, source_direction)), target->nodes[tp[i]].rotation));
            } else {
                const tide_float3 segment = tide_fromto_unit_f3(tide_sub_f3(target->nodes[tp[i + 1]].position, target->nodes[tp[i]].position));
                // Axial distribution preserves a straight limb. A helper with
                // an off-axis offset follows its proximal joint instead.
                const bool straight = tide_dot_f3(segment, target_axis) > 0.999f;
                transfer_anchor(to, source, exported, sr[begin_role], sr[end_role], straight ? fraction : 0.0f, TRANSFER_TWIST);
                to->axis = source_axis;
                to->aligned = normalized(tide_mul_q(tide_fromto_q(target_axis, tide_rotate_q(heading, source_axis)), target->nodes[tp[i]].rotation));
            }
        }
    }
    // Common deformation rigs put a twist bone beside the limb's main child,
    // at its parent's origin. Half of the distal axial motion is their useful
    // default; the map's twistDistribution scales it, including disabling it.
    for (uint32_t i = 1; i < target->view.nodes; i++) if (!made[i].mode) {
        char name[128]; normalized_name(&target->nodes[i].info, name);
        if (!has(name, "twist") && !has(name, "roll")) continue;
        for (int role = 0; role < TIDE_HUMANOID_COUNT; role++) {
            const int child = child_role(role);
            if (child < 0 || torso_role(role) || sr[role] < 0 || tr[role] < 0 || sr[child] < 0 || tr[child] < 0
                || target->nodes[i].info.parent != tr[role]) continue;
            retarget_target *to = &made[i];
            transfer_anchor(to, source, exported, sr[role], sr[child], 0.5f, TRANSFER_TWIST);
            to->axis = tide_fromto_unit_f3(tide_sub_f3(source->nodes[sr[child]].position, source->nodes[sr[role]].position));
            const tide_float3 target_axis = tide_sub_f3(target->nodes[tr[child]].position, target->nodes[tr[role]].position);
            to->aligned = normalized(tide_mul_q(tide_fromto_q(target_axis, tide_rotate_q(heading, to->axis)), target->nodes[i].rotation));
            break;
        }
    }
    free(td); free(sd); free(tp); free(sp);
}

int32_t tide_retarget_create(const tide_list source_data,
                             const tide_list source_reference_data, const tide_list source_reference,
                             const tide_list source_bones, const tide_list source_status,
                             const tide_list target_data, const tide_list target_reference,
                             const tide_list target_bones, const tide_list target_status,
                             tide_list *plan, float *root_scale, const uint32_t where)
{
    if (!plan || !root_scale) return TIDE_RETARGET_INVALID_RIG;
    rig_seen actual = {0}, source = {0}, target = {0};
    int32_t error = open_rig(source_data, (tide_list){0}, false, &actual);
    if (!error) error = open_rig(source_reference_data, source_reference, true, &source);
    if (!error) error = open_rig(target_data, target_reference, true, &target);
    int32_t sr[TIDE_HUMANOID_COUNT], calibration[TIDE_HUMANOID_COUNT], tr[TIDE_HUMANOID_COUNT];
    if (error) refusal(error, "reference", -1, "is invalid; use finite poses with positive uniform scale from valid skeletons");
    if (!error) error = read_roles(&source, source_bones, source_status, sr, "source");
    if (!error) error = read_roles(&target, target_bones, target_status, tr, "target");
    if (!error) memcpy(calibration, sr, sizeof calibration);
    int32_t *exported = !error ? tide_alloc((size_t)source.view.nodes * sizeof *exported) : NULL;
    if (!error) error = match_export(&actual, &source, sr, exported);
    tide_quaternion sf = tide_identity_q(), tf = tide_identity_q();
    float ratio = 1.0f;
    if (!error) {
        const float sl = leg_length(&source, sr), tl = leg_length(&target, tr);
        ratio = tl / sl;
        if (!body_frame(&source, sr, &sf) || !body_frame(&target, tr, &tf)
            || !(sl > 0.0f) || !(tl > 0.0f) || !tide_is_finite_f(ratio) || !(ratio > 0.0f))
            error = refusal(TIDE_RETARGET_INVALID_REFERENCE_POSE, "reference", -1,
                "has degenerate body axes or zero-length legs; provide a standing calibration pose and check its role assignments");
    }
    if (!error) {
        const size_t size = sizeof(retarget_header) + (size_t)actual.view.nodes * sizeof(retarget_source)
            + (size_t)target.view.nodes * sizeof(retarget_target);
        uint8_t *bytes = tide_alloc_zeroed(1u, size);
        retarget_header *header = (retarget_header *)bytes;
        *header = (retarget_header){.magic = RETARGET_MAGIC, .source_count = actual.view.nodes, .target_count = target.view.nodes,
            .source_hips = exported[sr[TIDE_HUMANOID_HIPS]], .target_hips = tr[TIDE_HUMANOID_HIPS],
            .heading = normalized(tide_mul_q(tf, tide_conjugate_q(sf))), .up = tide_up_q(tf),
            .source_position = source.nodes[sr[TIDE_HUMANOID_HIPS]].position,
            .target_position = target.nodes[tr[TIDE_HUMANOID_HIPS]].position};
        memcpy(header->source_hash, actual.view.skeleton_hash, 32u);
        memcpy(header->target_hash, target.view.skeleton_hash, 32u);
        retarget_source *sources = (retarget_source *)(bytes + sizeof *header);
        retarget_target *targets = (retarget_target *)(sources + actual.view.nodes);
        for (uint32_t i = 0; i < actual.view.nodes; i++)
            sources[i] = (retarget_source){actual.nodes[i].info.parent, actual.nodes[i].local.scale.x};
        for (uint32_t i = 0; i < target.view.nodes; i++) {
            targets[i].parent = target.nodes[i].info.parent;
            targets[i].source_a = targets[i].source_b = -1;
            targets[i].local = target.nodes[i].local;
            targets[i].inverse_a = targets[i].inverse_b = targets[i].aligned = tide_identity_q();
        }
        for (int role = 0; role < TIDE_HUMANOID_COUNT; role++) if (sr[role] >= 0 && tr[role] >= 0) {
            retarget_target *to = &targets[tr[role]];
            transfer_anchor(to, &source, exported, sr[role], sr[role], 0.0f, TRANSFER_BONE);
            to->aligned = normalized(tide_mul_q(align_role(&source, &target, calibration, tr, role, header->heading), target.nodes[tr[role]].rotation));
        }
        prepare_chains(&source, &target, exported, sr, tr, header->heading, targets);
        tide_list_set(plan, tide_list_from(bytes, (int32_t)size, 1u), 1u, where);
        *root_scale = ratio;
        free(bytes);
    }
    free(exported);
    close_rig(&target); close_rig(&source); close_rig(&actual);
    return error;
}

static bool identity_matches(const tide_list data, const uint32_t count, const uint8_t hash[32])
{
    int32_t size;
    const void *bytes;
    if (tide_pack_holds(data.at)) {
        const uint32_t at = data.at & 0x3fffffffu;
        tide_block block;
        const void *span = tide_pack_span(at, sizeof block);
        if (!span) return false;
        memcpy(&block, span, sizeof block);
        if (block.a != block.b || block.a > INT32_MAX) return false;
        size = (int32_t)block.a;
        bytes = tide_pack_span(at + (uint32_t)sizeof block, block.a);
    } else {
        size = tide_list_count(data);
        bytes = tide_list_at(data, 0, 1u);
    }
    if (size < (int32_t)TIDE_SKINNED_HEADER_SIZE) return false;
    // data_open reads only the header. No table getter touches the rest of
    // this immutable owned blob, which may hold a large mesh or animation.
    tide_skinned_data_view view;
    return bytes && tide_skinned_data_open(bytes, (size_t)size, &view)
        && view.nodes == count && !memcmp(view.skeleton_hash, hash, 32u);
}

typedef struct global_transform { tide_float3 position; tide_quaternion rotation; float scale; } global_transform;

static bool compose_global(const tide_bone_transform local, const int32_t parent,
                            const global_transform *globals, global_transform *out)
{
    if (!valid_local(local)) return false;
    out->position = local.position;
    out->rotation = normalized(local.rotation);
    out->scale = local.scale.x;
    if (parent >= 0) {
        const global_transform p = globals[parent];
        out->position = tide_add_f3(p.position, tide_rotate_q(p.rotation, tide_mul_f3(tide_f3_splat(p.scale), out->position)));
        out->rotation = normalized(tide_mul_q(p.rotation, out->rotation));
        out->scale *= p.scale;
    }
    return finite3(out->position) && finiteq(out->rotation) && tide_is_finite_f(out->scale) && out->scale > 0.0f;
}

static tide_quaternion twist_of(const tide_quaternion rotation, const tide_float3 axis)
{
    const tide_float3 vector = tide_f3(rotation.value.x, rotation.value.y, rotation.value.z);
    const tide_float3 along = tide_mul_f3(axis, tide_f3_splat(tide_dot_f3(vector, axis)));
    const tide_quaternion twist = tide_q(along.x, along.y, along.z, rotation.value.w);
    return tide_dot_q(twist, twist) > 1e-12f ? normalized(twist) : tide_identity_q();
}

bool tide_retarget_apply(const tide_list plan, const tide_list source_data, const tide_list source_transforms,
                         const tide_list target_data, const float root_scale, const tide_float3 root_offset,
                         const float heading_degrees, const float spine_distribution, const float twist_distribution,
                         tide_list *out, const uint32_t where)
{
    const int32_t bytes = tide_list_count(plan);
    const size_t maximum = sizeof(retarget_header) + (size_t)(TIDE_SKINNED_MAX_NODES + 1u) * (sizeof(retarget_source) + sizeof(retarget_target));
    if (!out || (out->at && (out->at == plan.at || out->at == source_data.at || out->at == target_data.at))
        || bytes < (int32_t)sizeof(retarget_header) || (size_t)bytes > maximum
        || !tide_is_finite_f(root_scale) || root_scale < 0.0f || !finite3(root_offset)
        || !tide_is_finite_f(heading_degrees) || !tide_is_finite_f(spine_distribution) || !tide_is_finite_f(twist_distribution)) return false;
    bool copied = false;
    const uint8_t *flat = tide_list_flatten(plan, 1u, false, &copied);
    if (!flat) return false;
    retarget_header header;
    memcpy(&header, flat, sizeof header);
    bool valid = header.magic == RETARGET_MAGIC && header.source_count && header.target_count
        && header.source_count <= TIDE_SKINNED_MAX_NODES + 1u && header.target_count <= TIDE_SKINNED_MAX_NODES + 1u
        && (size_t)bytes == sizeof header + (size_t)header.source_count * sizeof(retarget_source) + (size_t)header.target_count * sizeof(retarget_target)
        && header.source_hips > 0 && (uint32_t)header.source_hips < header.source_count
        && header.target_hips > 0 && (uint32_t)header.target_hips < header.target_count
        && finiteq(header.heading) && finite3(header.up) && finite3(header.source_position) && finite3(header.target_position)
        && tide_list_count(source_transforms) == (int32_t)header.source_count
        && identity_matches(source_data, header.source_count, header.source_hash)
        && identity_matches(target_data, header.target_count, header.target_hash);
    if (!valid) { tide_list_unflatten(plan, (void *)flat, 1u, false, copied); return false; }
    const retarget_source *sources = (const retarget_source *)(flat + sizeof header);
    const retarget_target *targets = (const retarget_target *)(sources + header.source_count);
    const uint32_t mark = tide_scratch_mark();
    global_transform *sg = tide_scratch_memory((size_t)header.source_count * sizeof *sg);
    global_transform *tg = tide_scratch_memory((size_t)header.target_count * sizeof *tg);
    tide_bone_transform *locals = tide_scratch_memory((size_t)header.target_count * sizeof *locals);
    tide_list_cache reads = {0};
    for (uint32_t i = 0; valid && i < header.source_count; i++) {
        const retarget_source record = sources[i];
        const tide_bone_transform *local = tide_list_cached_at(&reads, source_transforms, (int32_t)i, sizeof *local);
        valid = (i ? record.parent >= 0 && (uint32_t)record.parent < i : record.parent == -1)
            && local && tide_is_finite_f(record.scale) && record.scale > 0.0f
            && tide_abs_f(local->scale.x - record.scale) <= record.scale * 0.00001f
            && compose_global(*local, record.parent, sg, &sg[i]);
    }
    const float radians = tide_radians_f(heading_degrees);
    const tide_quaternion extra = tide_axisangle_q(header.up, radians);
    const tide_quaternion heading = normalized(header.heading), inverse = tide_conjugate_q(heading);
    const float spine = tide_saturate_f(spine_distribution), twist = tide_saturate_f(twist_distribution);
    for (uint32_t i = 0; valid && i < header.target_count; i++) {
        const retarget_target record = targets[i];
        valid = (i ? record.parent >= 0 && (uint32_t)record.parent < i : record.parent == -1)
            && record.mode >= TRANSFER_NONE && record.mode <= TRANSFER_TWIST && valid_local(record.local)
            && tide_is_finite_f(record.fraction) && record.fraction >= 0.0f && record.fraction <= 1.0f
            && finiteq(record.inverse_a) && finiteq(record.inverse_b) && finiteq(record.aligned) && finite3(record.axis);
        if (!valid) break;
        locals[i] = record.local;
        if (record.mode) {
            if (record.source_a < 0 || (uint32_t)record.source_a >= header.source_count
                || record.source_b < 0 || (uint32_t)record.source_b >= header.source_count) { valid = false; break; }
            const tide_quaternion a = normalized(tide_mul_q(sg[record.source_a].rotation, normalized(record.inverse_a)));
            const tide_quaternion b = normalized(tide_mul_q(sg[record.source_b].rotation, normalized(record.inverse_b)));
            tide_quaternion delta = a;
            if (record.mode == TRANSFER_SPINE) delta = normalized(tide_slerp_q(a, b, record.fraction * spine));
            else if (record.mode == TRANSFER_TWIST) {
                const tide_quaternion relative = normalized(tide_mul_q(tide_conjugate_q(a), b));
                const tide_quaternion axial = twist_of(relative, tide_fromto_unit_f3(record.axis));
                delta = normalized(tide_mul_q(a, tide_slerp_q(tide_identity_q(), axial, record.fraction * twist)));
            }
            tide_quaternion desired = tide_mul_q(tide_mul_q(heading, delta), inverse);
            desired = normalized(tide_mul_q(extra, tide_mul_q(desired, normalized(record.aligned))));
            locals[i].rotation = record.parent >= 0 ? normalized(tide_mul_q(tide_conjugate_q(tg[record.parent].rotation), desired)) : desired;
        }
        if ((int32_t)i == header.target_hips) {
            tide_float3 movement = tide_sub_f3(sg[header.source_hips].position, header.source_position);
            movement = tide_rotate_q(extra, tide_rotate_q(heading, tide_mul_f3(movement, tide_f3_splat(root_scale))));
            const tide_float3 desired = tide_add_f3(tide_add_f3(header.target_position, root_offset), movement);
            if (record.parent >= 0) {
                const global_transform p = tg[record.parent];
                locals[i].position = tide_div_f3(tide_rotate_q(tide_conjugate_q(p.rotation), tide_sub_f3(desired, p.position)), tide_f3_splat(p.scale));
            } else locals[i].position = desired;
        }
        valid = compose_global(locals[i], record.parent, tg, &tg[i]);
    }
    if (valid) {
        tide_list_prepare(out, sizeof *locals, where);
        tide_list_resize(out, (int32_t)header.target_count, sizeof *locals, where);
        tide_list_cache writes = {0};
        for (uint32_t i = 0; i < header.target_count; i++) {
            tide_bone_transform *to = tide_list_cached_at_mut(&writes, *out, (int32_t)i, sizeof *to);
            if (to) *to = locals[i];
        }
    }
    if (!valid || where != TIDE_IN_SCRATCH) tide_scratch_reset(mark);
    tide_list_unflatten(plan, (void *)flat, 1u, false, copied);
    return valid;
}
