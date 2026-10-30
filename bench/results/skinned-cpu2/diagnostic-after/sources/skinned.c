#include "tide/skinned.h"

#include <string.h>
#include <stdlib.h>

#include "tide/mesh.h"
#include "tide/net.h"
#include "tide/pack.h"
#include "tide/skinned_data.h"
#include "skinned_cache.h"

_Static_assert(sizeof(tide_bone_transform) == 40u, "bone transforms have ten float words");
_Static_assert(TIDE_LIST_CHUNK >= TIDE_SKINNED_HEADER_SIZE, "a byte list's first chunk holds the model header");

typedef struct model_seen {
    tide_skinned_data_view view;
    tide_list list;
    void *bytes;
    bool copied;
} model_seen;

static bool open_model(const tide_list data, model_seen *m)
{
    memset(m, 0, sizeof *m);
    int32_t count;
    if (tide_pack_holds(data.at)) {
        const uint32_t offset = data.at & 0x3FFFFFFFu;
        const uint8_t *header = tide_pack_span(offset, sizeof(tide_block));
        if (!header) return false;
        tide_block block;
        memcpy(&block, header, sizeof block);
        if (block.a != block.b || block.a > INT32_MAX) return false;
        count = (int32_t)block.a;
        m->bytes = (void *)tide_pack_span(offset + (uint32_t)sizeof(tide_block), block.a);
        if (!m->bytes) return false;
    } else {
        count = tide_list_count(data);
        if (count < (int32_t)TIDE_SKINNED_HEADER_SIZE) return false;
        m->bytes = tide_list_flatten(data, 1u, false, &m->copied);
    }
    if (count < (int32_t)TIDE_SKINNED_HEADER_SIZE) return false;
    m->list = data;
    if (m->bytes && tide_skinned_data_open(m->bytes, (size_t)count, &m->view)) return true;
    tide_list_unflatten(data, m->bytes, 1u, false, m->copied);
    memset(m, 0, sizeof *m);
    return false;
}

static void close_model(model_seen *m)
{
    tide_list_unflatten(m->list, m->bytes, 1u, false, m->copied);
}

typedef struct skeleton_identity {
    uint32_t nodes;
    uint8_t hash[32];
} skeleton_identity;

static bool read_identity(const tide_list data, skeleton_identity *out)
{
    tide_skinned_data_view view;
    if (tide_pack_holds(data.at)) {
        model_seen packed;
        if (!open_model(data, &packed)) return false;
        out->nodes = packed.view.nodes;
        memcpy(out->hash, packed.view.skeleton_hash, sizeof out->hash);
        close_model(&packed);
        return true;
    }
    const int32_t count = tide_list_count(data);
    if (count < (int32_t)TIDE_SKINNED_HEADER_SIZE) return false;
    // Byte lists hold their complete header in the first contiguous run,
    // including heap lists whose remaining bytes occupy other chunks.
    // data_open reads only that header, checking table spans against the
    // logical total size. Keep this partial view local: no table getter may
    // use it, and no pointer into the list escapes with the identity.
    const void *header = tide_list_at(data, 0, 1u);
    if (!tide_skinned_data_open(header, (size_t)count, &view)) return false;
    out->nodes = view.nodes;
    memcpy(out->hash, view.skeleton_hash, sizeof out->hash);
    return true;
}

static bool nodes_valid(const tide_skinned_data_view *v)
{
    if (v->nodes == 0u || v->nodes > INT32_MAX) return false;
    for (uint32_t i = 0; i < v->nodes; i++) {
        tide_skinned_node_data n;
        if (!tide_skinned_data_node(v, i, &n) || (i == 0u ? n.parent != -1 : n.parent < 0 || (uint32_t)n.parent >= i)) return false;
    }
    return true;
}

static tide_bone_transform local_of(const tide_skinned_node_data *n)
{
    return (tide_bone_transform){tide_f3(n->position[0], n->position[1], n->position[2]),
                                 {tide_f4(n->rotation[0], n->rotation[1], n->rotation[2], n->rotation[3])},
                                 tide_f3(n->scale[0], n->scale[1], n->scale[2])};
}

static tide_bone_transform *scratch_transforms(const tide_list transforms, const uint32_t count)
{
    // Scratch lists are contiguous. Keep this pointer only across loops that
    // cannot resize the list; existing heap/chunked outputs keep their COW path.
    if (transforms.at >> 30 != TIDE_IN_SCRATCH || tide_list_count(transforms) != (int32_t)count) return NULL;
    return tide_list_at_mut(transforms, 0, sizeof(tide_bone_transform));
}

// Private readers used only after the relevant nodes or channels have been
// checked, or current immutable-pack provenance proves them. The opened model
// remains live through each loop. Explicit LE decoding preserves stored bits.
static float validated_number(const uint8_t *p)
{
    const uint32_t bits = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
    float value;
    memcpy(&value, &bits, sizeof value);
    return value;
}

static void validated_numbers(const uint8_t *p, const uint32_t count, float *out)
{
    for (uint32_t i = 0; i < count; i++) out[i] = validated_number(p + (size_t)i * 4u);
}

static void fill_rest(const tide_skinned_data_view *v, tide_list *transforms, const uint32_t where)
{
    // All callers establish node validity before resizing/writing the output.
    tide_list_resize(transforms, (int32_t)v->nodes, sizeof(tide_bone_transform), where);
    tide_bone_transform *flat = scratch_transforms(*transforms, v->nodes);
    tide_list_cache cache = {0};
    for (uint32_t i = 0; i < v->nodes; i++) {
        float rest[10];
        validated_numbers(v->data + v->node_at + (size_t)i * TIDE_SKINNED_NODE_SIZE + 16u, 10u, rest);
        tide_bone_transform *to = flat ? flat + i : tide_list_cached_at_mut(&cache, *transforms, (int32_t)i, sizeof *to);
        if (to) *to = (tide_bone_transform){tide_f3(rest[0], rest[1], rest[2]),
                                           {tide_f4(rest[3], rest[4], rest[5], rest[6])},
                                           tide_f3(rest[7], rest[8], rest[9])};
    }
}

bool tide_skinned_valid(const tide_list data)
{
    model_seen m;
    if (!open_model(data, &m)) return false;
    const bool valid = tide_skinned_data_validate(m.view.data, m.view.size);
    close_model(&m);
    return valid;
}

uint64_t tide_skinned_fingerprint(const tide_list data)
{
    model_seen m;
    if (!open_model(data, &m)) return 0;
    const uint64_t hash = tide_hash(m.view.skeleton_hash, sizeof m.view.skeleton_hash);
    close_model(&m);
    return hash ? hash : 1u;
}

bool tide_skinned_compatible(const tide_list a, const tide_list b)
{
    skeleton_identity first, second;
    return read_identity(a, &first) && read_identity(b, &second) && first.nodes == second.nodes
        && memcmp(first.hash, second.hash, sizeof first.hash) == 0;
}

int32_t tide_skinned_bone_count(const tide_list data)
{
    model_seen m;
    if (!open_model(data, &m)) return 0;
    const int32_t count = (int32_t)m.view.nodes;
    close_model(&m);
    return count;
}

int32_t tide_skinned_animation_count(const tide_list data)
{
    model_seen m;
    if (!open_model(data, &m)) return 0;
    const int32_t count = (int32_t)m.view.clips;
    close_model(&m);
    return count;
}

static int32_t find_name(const tide_list data, const tide_str name, const bool animation)
{
    if (!name.ptr || name.bytes <= 0) return -1;
    model_seen m;
    if (!open_model(data, &m)) return -1;
    int32_t found = -1;
    const uint32_t count = animation ? m.view.clips : m.view.nodes;
    for (uint32_t i = 0; i < count; i++) {
        const char *text;
        uint32_t length;
        if (animation) {
            tide_skinned_clip_data c;
            if (!tide_skinned_data_clip(&m.view, i, &c)) continue;
            text = c.name;
            length = c.name_length;
        } else {
            tide_skinned_node_data n;
            if (!tide_skinned_data_node(&m.view, i, &n)) continue;
            text = n.name;
            length = n.name_length;
        }
        if (length != (uint32_t)name.bytes || memcmp(text, name.ptr, length) != 0) continue;
        if (found >= 0) { found = -1; break; } // Ambiguous names have no typed reference.
        found = (int32_t)i;
    }
    close_model(&m);
    return found;
}

int32_t tide_skinned_find_bone(const tide_list data, const tide_str name) { return find_name(data, name, false); }
int32_t tide_skinned_find_animation(const tide_list data, const tide_str name) { return find_name(data, name, true); }

static tide_str read_name(const tide_list data, const int32_t index, const bool animation)
{
    if (index < 0) return TIDE_STR_EMPTY;
    model_seen m;
    if (!open_model(data, &m)) return TIDE_STR_EMPTY;
    const char *text = NULL;
    uint32_t length = 0;
    if (animation) {
        tide_skinned_clip_data c;
        if (tide_skinned_data_clip(&m.view, (uint32_t)index, &c)) { text = c.name; length = c.name_length; }
    } else {
        tide_skinned_node_data n;
        if (tide_skinned_data_node(&m.view, (uint32_t)index, &n)) { text = n.name; length = n.name_length; }
    }
    tide_str out = TIDE_STR_EMPTY;
    if (text && length) {
        char *copy = tide_scratch_memory((size_t)length + 1u);
        memcpy(copy, text, length);
        copy[length] = '\0';
        out = tide_str_from_cstr(copy);
    }
    close_model(&m);
    return out;
}

tide_str tide_skinned_bone_name(const tide_list data, const int32_t bone) { return read_name(data, bone, false); }
tide_str tide_skinned_animation_name(const tide_list data, const int32_t animation) { return read_name(data, animation, true); }

float tide_skinned_animation_duration(const tide_list data, const int32_t animation)
{
    if (animation < 0) return 0.0f;
    model_seen m;
    if (!open_model(data, &m)) return 0.0f;
    tide_skinned_clip_data c;
    const float length = tide_skinned_data_clip(&m.view, (uint32_t)animation, &c) ? c.length : 0.0f;
    close_model(&m);
    return length;
}

bool tide_skinned_rest(const tide_list data, tide_list *transforms, const uint32_t where)
{
    if (!transforms) return false;
    model_seen m;
    if (!open_model(data, &m)) return false;
    uint64_t version;
    const bool valid = tide_pack_skinned_version(data, &version) || nodes_valid(&m.view);
    if (valid) fill_rest(&m.view, transforms, where);
    close_model(&m);
    return valid;
}

static bool channel_valid(const tide_skinned_data_view *v, const tide_skinned_channel_data *c)
{
    float previous = -1.0f;
    for (uint32_t i = 0; i < c->keys; i++) {
        float time, value[4], in[4], out[4];
        if (!tide_skinned_data_key(v, c, i, &time, value, in, out) || !(time >= 0.0f) || (i && !(time > previous))) return false;
        previous = time;
    }
    return c->keys != 0u;
}

// Only sampled_value uses these unchecked time/key readers. Its two callers first
// validate every selected channel/key, or establish current immutable-pack
// provenance and check the channel plus its first nonnegative key. The opened
// model stays live and unchanged through sampling; no pointer escapes. Decode
// little-endian words explicitly, retaining the exact stored float bits.
static float validated_time(const tide_skinned_data_view *v, const tide_skinned_channel_data *c, const uint32_t index)
{
    return validated_number(v->data + c->times_at + (size_t)index * 4u);
}

static void validated_key(const tide_skinned_data_view *v, const tide_skinned_channel_data *c, const uint32_t index,
                          float *time, float value[4], float in[4], float out[4])
{
    const uint32_t width = c->path == TIDE_SKINNED_ROTATION ? 4u : 3u;
    const uint32_t copies = c->interpolation == TIDE_SKINNED_CUBICSPLINE ? 3u : 1u;
    const uint8_t *p = v->data + c->values_at + (size_t)index * width * copies * 4u;
    memset(value, 0, 4u * sizeof *value);
    memset(in, 0, 4u * sizeof *in);
    memset(out, 0, 4u * sizeof *out);
    *time = validated_time(v, c, index);
    if (copies == 1u) validated_numbers(p, width, value);
    else {
        validated_numbers(p, width, in);
        validated_numbers(p + width * 4u, width, value);
        validated_numbers(p + width * 8u, width, out);
    }
}

static tide_float4 sampled_value(const tide_skinned_data_view *v, const tide_skinned_channel_data *c, const float seconds)
{
    uint32_t low = 0, high = c->keys;
    float time, value[4], in[4], out[4];
    while (low < high) {
        const uint32_t middle = low + (high - low) / 2u;
        time = validated_time(v, c, middle);
        if (time <= seconds) low = middle + 1u;
        else high = middle;
    }
    const uint32_t before = low ? low - 1u : 0u;
    float first_time, first[4], unused[4], first_out[4];
    validated_key(v, c, before, &first_time, first, unused, first_out);
    tide_float4 a = tide_f4(first[0], first[1], first[2], first[3]);
    if (low == 0u || low == c->keys || c->interpolation == TIDE_SKINNED_STEP) return a;
    validated_key(v, c, low, &time, value, in, out);
    const tide_float4 b = tide_f4(value[0], value[1], value[2], value[3]);
    const float span = time - first_time, t = tide_saturate_f((seconds - first_time) / span);
    if (c->interpolation == TIDE_SKINNED_LINEAR) {
        return c->path == TIDE_SKINNED_ROTATION ? tide_slerp_q((tide_quaternion){a}, (tide_quaternion){b}, t).value
                                               : tide_lerp_f4(a, b, tide_f4_splat(t));
    }
    const float t2 = t * t, t3 = t2 * t;
    const float h0 = (2.0f * t3 - 3.0f * t2) + 1.0f, h1 = (t3 - 2.0f * t2) + t;
    const float h2 = -2.0f * t3 + 3.0f * t2, h3 = t3 - t2;
    float parts[4];
    for (uint32_t i = 0; i < 4u; i++) parts[i] = ((h0 * first[i] + h1 * span * first_out[i]) + h2 * value[i]) + h3 * span * in[i];
    const tide_float4 made = tide_f4(parts[0], parts[1], parts[2], parts[3]);
    return c->path == TIDE_SKINNED_ROTATION ? tide_normalize_q((tide_quaternion){made}).value : made;
}

bool tide_skinned_sample(const tide_list data, const int32_t animation, const float seconds,
                         tide_list *transforms, const uint32_t where)
{
    if (!transforms || animation < 0) return false;
    model_seen m;
    if (!open_model(data, &m)) return false;
    uint64_t version;
    const bool packed = tide_pack_skinned_version(data, &version);
    tide_skinned_clip_data clip;
    bool valid = (packed || nodes_valid(&m.view)) && tide_skinned_data_clip(&m.view, (uint32_t)animation, &clip);
    for (uint32_t i = 0; valid && i < clip.channels; i++) {
        tide_skinned_channel_data channel;
        valid = tide_skinned_data_channel(&m.view, clip.first_channel + i, &channel);
        if (valid && packed) {
            // Full immutable-model validation proves finite increasing keys,
            // but permits the first time in (-1, 0). Sampling requires >=0.
            float time, value[4], in[4], out[4];
            valid = tide_skinned_data_key(&m.view, &channel, 0u, &time, value, in, out) && time >= 0.0f;
        } else if (valid) valid = channel_valid(&m.view, &channel);
    }
    if (valid) {
        fill_rest(&m.view, transforms, where);
        const float clock = tide_clamp_f(seconds, 0.0f, clip.length);
        tide_bone_transform *flat = scratch_transforms(*transforms, m.view.nodes);
        tide_list_cache cache = {0};
        for (uint32_t i = 0; i < clip.channels; i++) {
            tide_skinned_channel_data channel;
            (void)tide_skinned_data_channel(&m.view, clip.first_channel + i, &channel);
            const tide_float4 value = sampled_value(&m.view, &channel, clock);
            tide_bone_transform *to = flat ? flat + channel.node : tide_list_cached_at_mut(&cache, *transforms, (int32_t)channel.node, sizeof *to);
            if (!to) continue;
            if (channel.path == TIDE_SKINNED_TRANSLATION) to->position = tide_f3(value.x, value.y, value.z);
            else if (channel.path == TIDE_SKINNED_SCALE) to->scale = tide_f3(value.x, value.y, value.z);
            else to->rotation = tide_normalize_q((tide_quaternion){value});
        }
    }
    close_model(&m);
    return valid;
}

typedef struct named_node {
    uint64_t hash;
    int32_t index;
    bool ambiguous;
} named_node;

typedef struct node_names {
    named_node *slots;
    uint32_t mask;
} node_names;

static bool same_name(const tide_skinned_node_data *a, const tide_skinned_node_data *b)
{
    return a->name_length == b->name_length && memcmp(a->name, b->name, a->name_length) == 0;
}

static node_names index_names(const tide_skinned_data_view *v)
{
    uint32_t room = 1u;
    while ((uint64_t)room < (uint64_t)v->nodes * 2u) room *= 2u;
    node_names names = {tide_scratch_memory((size_t)room * sizeof(named_node)), room - 1u};
    for (uint32_t i = 0; i < room; i++) names.slots[i] = (named_node){.index = -1};
    for (uint32_t i = 1; i < v->nodes; i++) {
        tide_skinned_node_data n;
        (void)tide_skinned_data_node(v, i, &n);
        if (!n.name_length) continue;
        const uint64_t hash = tide_hash(n.name, n.name_length);
        uint32_t at = (uint32_t)hash & names.mask;
        while (names.slots[at].index >= 0) {
            tide_skinned_node_data previous;
            (void)tide_skinned_data_node(v, (uint32_t)names.slots[at].index, &previous);
            if (names.slots[at].hash == hash && same_name(&n, &previous)) break;
            at = (at + 1u) & names.mask;
        }
        if (names.slots[at].index >= 0) names.slots[at].ambiguous = true;
        else names.slots[at] = (named_node){hash, (int32_t)i, false};
    }
    return names;
}

static int32_t named_index(const tide_skinned_data_view *v, const node_names names, const tide_skinned_node_data *n)
{
    if (!n->name_length) return -1;
    const uint64_t hash = tide_hash(n->name, n->name_length);
    uint32_t at = (uint32_t)hash & names.mask;
    while (names.slots[at].index >= 0) {
        tide_skinned_node_data found;
        (void)tide_skinned_data_node(v, (uint32_t)names.slots[at].index, &found);
        if (names.slots[at].hash == hash && same_name(n, &found)) {
            return names.slots[at].ambiguous ? -1 : names.slots[at].index;
        }
        at = (at + 1u) & names.mask;
    }
    return -1;
}

static bool same_transform(const tide_bone_transform a, const tide_bone_transform b)
{
    const bool rotation = (a.rotation.value.x == b.rotation.value.x && a.rotation.value.y == b.rotation.value.y
                           && a.rotation.value.z == b.rotation.value.z && a.rotation.value.w == b.rotation.value.w)
        || (a.rotation.value.x == -b.rotation.value.x && a.rotation.value.y == -b.rotation.value.y
            && a.rotation.value.z == -b.rotation.value.z && a.rotation.value.w == -b.rotation.value.w);
    return a.position.x == b.position.x && a.position.y == b.position.y && a.position.z == b.position.z
        && a.scale.x == b.scale.x && a.scale.y == b.scale.y && a.scale.z == b.scale.z && rotation;
}

// FBX conversion retains its scene root under the canonical identity root.
// A static identity wrapper has no effect on named bones' parent-local TRS.
static bool root_wrapper(const tide_skinned_data_view *v, const uint32_t index)
{
    tide_skinned_node_data n;
    const tide_bone_transform identity = {tide_f3(0, 0, 0), {tide_f4(0, 0, 0, 1)}, tide_f3(1, 1, 1)};
    return index != 0u && tide_skinned_data_node(v, index, &n) && n.parent == 0 && n.name_length == 0u
        && same_transform(local_of(&n), identity);
}

static bool wrapper_channel_at_rest(const tide_skinned_data_view *v, const tide_skinned_channel_data *channel)
{
    const uint32_t width = channel->path == TIDE_SKINNED_ROTATION ? 4u : 3u;
    float rotation_sign = 0.0f;
    for (uint32_t i = 0; i < channel->keys; i++) {
        float time, value[4], in[4], out[4];
        if (!tide_skinned_data_key(v, channel, i, &time, value, in, out)) return false;
        if (channel->path == TIDE_SKINNED_ROTATION && channel->interpolation == TIDE_SKINNED_CUBICSPLINE) {
            if (i != 0u && value[3] != rotation_sign) return false;
            rotation_sign = value[3];
        }
        for (uint32_t k = 0; k < width; k++) {
            const float rest = channel->path == TIDE_SKINNED_SCALE || (channel->path == TIDE_SKINNED_ROTATION && k == 3u) ? 1.0f : 0.0f;
            if (value[k] != rest && !(channel->path == TIDE_SKINNED_ROTATION && k == 3u && value[k] == -1.0f)) return false;
            if (channel->interpolation == TIDE_SKINNED_CUBICSPLINE && (in[k] != 0.0f || out[k] != 0.0f)) return false;
        }
    }
    return true;
}

static bool require_clip(const tide_skinned_data_view *v, const uint32_t index, uint8_t *required, const bool keys)
{
    tide_skinned_clip_data clip;
    if (!tide_skinned_data_clip(v, index, &clip)) return false;
    for (uint32_t i = 0; i < clip.channels; i++) {
        tide_skinned_channel_data channel;
        if (!tide_skinned_data_channel(v, clip.first_channel + i, &channel) || (keys && !channel_valid(v, &channel))) return false;
        if (root_wrapper(v, channel.node) && !wrapper_channel_at_rest(v, &channel)) return false;
        required[channel.node] = 1u;
    }
    return true;
}

static int32_t *bind_nodes(const tide_skinned_data_view *target, const tide_skinned_data_view *source, uint8_t *required)
{
    const node_names target_names = index_names(target), source_names = index_names(source);
    int32_t *map = tide_scratch_memory((size_t)source->nodes * sizeof *map);
    for (uint32_t i = 0; i < source->nodes; i++) map[i] = -1;
    map[0] = 0;
    for (uint32_t i = source->nodes; i-- > 1u;) {
        if (!required[i]) continue;
        tide_skinned_node_data n;
        (void)tide_skinned_data_node(source, i, &n);
        required[n.parent] = 1u;
    }
    for (uint32_t i = 0; i < source->nodes; i++) {
        if (!required[i]) continue;
        tide_skinned_node_data n, to;
        (void)tide_skinned_data_node(source, i, &n);
        if (root_wrapper(source, i)) {
            map[i] = -2; // Transparent, never copied onto the canonical root.
            continue;
        }
        if (i != 0u) {
            if (named_index(source, source_names, &n) != (int32_t)i) return NULL;
            map[i] = named_index(target, target_names, &n);
        }
        if (map[i] < 0 || !tide_skinned_data_node(target, (uint32_t)map[i], &to)) return NULL;
        const int32_t source_parent = n.parent > 0 && root_wrapper(source, (uint32_t)n.parent) ? 0 : n.parent;
        const int32_t target_parent = to.parent > 0 && root_wrapper(target, (uint32_t)to.parent) ? 0 : to.parent;
        if (target_parent != (source_parent < 0 ? -1 : map[source_parent]) || !same_transform(local_of(&n), local_of(&to))) return NULL;
    }
    return map;
}

bool tide_skinned_sample_into(const tide_list target_data, const tide_list animation_data, const int32_t animation,
                              const float seconds, tide_list *transforms, const uint32_t where)
{
    if (!transforms || animation < 0) return false;
    model_seen target, source;
    if (!open_model(target_data, &target)) return false;
    if (!open_model(animation_data, &source)) { close_model(&target); return false; }
    bool valid = nodes_valid(&target.view) && nodes_valid(&source.view);
    if (valid && target.view.nodes == source.view.nodes
        && memcmp(target.view.skeleton_hash, source.view.skeleton_hash, sizeof target.view.skeleton_hash) == 0) {
        close_model(&source);
        close_model(&target);
        return tide_skinned_sample(animation_data, animation, seconds, transforms, where);
    }
    const uint64_t mark = tide_scratch_mark();
    uint8_t *required = valid ? tide_scratch_memory(source.view.nodes) : NULL;
    if (valid) memset(required, 0, source.view.nodes);
    valid = valid && require_clip(&source.view, (uint32_t)animation, required, true);
    const int32_t *map = valid ? bind_nodes(&target.view, &source.view, required) : NULL;
    valid = valid && map != NULL;
    if (valid) {
        fill_rest(&target.view, transforms, where);
        tide_skinned_clip_data clip;
        (void)tide_skinned_data_clip(&source.view, (uint32_t)animation, &clip);
        const float clock = tide_clamp_f(seconds, 0.0f, clip.length);
        tide_list_cache cache = {0};
        for (uint32_t i = 0; i < clip.channels; i++) {
            tide_skinned_channel_data channel;
            (void)tide_skinned_data_channel(&source.view, clip.first_channel + i, &channel);
            if (map[channel.node] < 0) continue;
            const tide_float4 value = sampled_value(&source.view, &channel, clock);
            tide_bone_transform *to = tide_list_cached_at_mut(&cache, *transforms, map[channel.node], sizeof *to);
            if (!to) continue;
            if (channel.path == TIDE_SKINNED_TRANSLATION) to->position = tide_f3(value.x, value.y, value.z);
            else if (channel.path == TIDE_SKINNED_SCALE) to->scale = tide_f3(value.x, value.y, value.z);
            else to->rotation = tide_normalize_q((tide_quaternion){value});
        }
    }
    if (!valid || where != TIDE_IN_SCRATCH) tide_scratch_reset(mark);
    close_model(&source);
    close_model(&target);
    return valid;
}

bool tide_skinned_pose_into(const tide_list target_data, const tide_list source_data, const tide_list source_transforms,
                            tide_list *transforms, const uint32_t where)
{
    if (!transforms) return false;
    model_seen target, source;
    if (!open_model(target_data, &target)) return false;
    if (!open_model(source_data, &source)) { close_model(&target); return false; }
    const uint64_t mark = tide_scratch_mark();
    bool valid = nodes_valid(&target.view) && nodes_valid(&source.view) && tide_list_count(source_transforms) == (int32_t)source.view.nodes;
    const bool exact = valid && target.view.nodes == source.view.nodes
        && memcmp(target.view.skeleton_hash, source.view.skeleton_hash, sizeof target.view.skeleton_hash) == 0;
    uint8_t *required = valid && !exact ? tide_scratch_memory(source.view.nodes) : NULL;
    if (required) memset(required, 0, source.view.nodes);
    for (uint32_t i = 0; valid && !exact && i < source.view.clips; i++) valid = require_clip(&source.view, i, required, false);
    tide_list_cache cache = {0};
    for (uint32_t i = 0; valid && i < source.view.nodes; i++) {
        const tide_bone_transform *local = tide_list_cached_at(&cache, source_transforms, (int32_t)i, sizeof *local);
        if (!local) { valid = false; break; }
        if (!exact) {
            tide_skinned_node_data n;
            (void)tide_skinned_data_node(&source.view, i, &n);
            if (!same_transform(*local, local_of(&n))) {
                if (root_wrapper(&source.view, i)) { valid = false; break; }
                required[i] = 1u;
            }
        }
    }
    const int32_t *map = valid && !exact ? bind_nodes(&target.view, &source.view, required) : NULL;
    valid = valid && (exact || map != NULL);
    if (valid) {
        // Stage the complete target before resizing: the output may alias source.
        tide_bone_transform *made = tide_scratch_memory((size_t)target.view.nodes * sizeof *made);
        for (uint32_t i = 0; i < target.view.nodes; i++) {
            tide_skinned_node_data n;
            (void)tide_skinned_data_node(&target.view, i, &n);
            made[i] = local_of(&n);
        }
        for (uint32_t i = 0; i < source.view.nodes; i++) {
            if (!exact && (!required[i] || map[i] < 0)) continue;
            const tide_bone_transform *local = tide_list_cached_at(&cache, source_transforms, (int32_t)i, sizeof *local);
            made[exact ? i : (uint32_t)map[i]] = *local;
        }
        tide_list_resize(transforms, (int32_t)target.view.nodes, sizeof *made, where);
        tide_list_cache output = {0};
        for (uint32_t i = 0; i < target.view.nodes; i++) {
            tide_bone_transform *to = tide_list_cached_at_mut(&output, *transforms, (int32_t)i, sizeof *to);
            if (to) *to = made[i];
        }
    }
    if (!valid || where != TIDE_IN_SCRATCH) tide_scratch_reset(mark);
    close_model(&source);
    close_model(&target);
    return valid;
}

bool tide_skinned_blend(const tide_list a, const tide_list b, const float weight, tide_list *out, const uint32_t where)
{
    if (!out) return false;
    const int32_t count = tide_list_count(a);
    if (count < 0 || count != tide_list_count(b)) return false;
    const uint64_t mark = tide_scratch_mark();
    tide_bone_transform *made = tide_scratch_memory((size_t)count * sizeof *made);
    const float t = tide_saturate_f(weight);
    tide_list_cache ac = {0}, bc = {0};
    for (int32_t i = 0; i < count; i++) {
        const tide_bone_transform *left = tide_list_cached_at(&ac, a, i, sizeof *left);
        const tide_bone_transform *right = tide_list_cached_at(&bc, b, i, sizeof *right);
        if (!left || !right) { tide_scratch_reset(mark); return false; }
        made[i] = (tide_bone_transform){tide_lerp_f3(left->position, right->position, tide_f3_splat(t)),
                                       tide_slerp_q(left->rotation, right->rotation, t),
                                       tide_lerp_f3(left->scale, right->scale, tide_f3_splat(t))};
    }
    tide_list_resize(out, count, sizeof *made, where);
    tide_list_cache oc = {0};
    for (int32_t i = 0; i < count; i++) {
        tide_bone_transform *to = tide_list_cached_at_mut(&oc, *out, i, sizeof *to);
        if (to) *to = made[i];
    }
    // A scratch output's storage may have been allocated after mark.
    if (where != TIDE_IN_SCRATCH) tide_scratch_reset(mark);
    return true;
}

typedef struct cached_skin_model {
    uint64_t version;
    uint32_t nodes, palettes;
    size_t bytes;
} cached_skin_model;

typedef struct cached_skin_pose {
    uint64_t hash, texture_id, texture_version;
    uint32_t model, texture;
    size_t bytes;
    int32_t width, height;
} cached_skin_pose;

struct tide_skinned_draw_cache {
    cached_skin_model *models;
    cached_skin_pose *poses;
    uint32_t model_count, model_capacity, pose_count, pose_capacity;
    uint32_t *model_index, *pose_index;
    uint32_t model_slots, pose_slots;
    uint8_t *bytes;
    size_t used, capacity;
};

void tide_skinned_draw_cache_reset(tide_draw_list *draw)
{
    struct tide_skinned_draw_cache *c = draw->skinned;
    if (!c) return;
    c->model_count = c->pose_count = 0u;
    c->used = 0u;
    if (c->model_slots) memset(c->model_index, 0, (size_t)c->model_slots * sizeof(uint32_t));
    if (c->pose_slots) memset(c->pose_index, 0, (size_t)c->pose_slots * sizeof(uint32_t));
}

void tide_skinned_draw_cache_free(tide_draw_list *draw)
{
    struct tide_skinned_draw_cache *c = draw->skinned;
    if (!c) return;
    free(c->models);
    free(c->poses);
    free(c->model_index);
    free(c->pose_index);
    free(c->bytes);
    free(c);
    draw->skinned = NULL;
}

static void *skin_cache_room(void *items, uint32_t *capacity, const uint32_t count, const size_t size)
{
    if (count < *capacity) return items;
    const uint64_t room = *capacity ? (uint64_t)*capacity * 2u : 8u;
    if (room > UINT32_MAX || room > SIZE_MAX / size) tide_out_of_memory();
    items = tide_realloc(items, (size_t)*capacity * size, (size_t)room * size);
    *capacity = (uint32_t)room;
    return items;
}

static size_t skin_cache_bytes(struct tide_skinned_draw_cache *c, const size_t bytes)
{
    if (bytes > SIZE_MAX - c->used) tide_out_of_memory();
    const size_t end = c->used + bytes, start = c->used;
    if (end > c->capacity) {
        size_t room = c->capacity ? c->capacity : 4096u;
        while (room < end) {
            if (room > SIZE_MAX / 2u) { room = end; break; }
            room *= 2u;
        }
        c->bytes = tide_realloc(c->bytes, c->capacity, room);
        c->capacity = room;
    }
    c->used = end;
    return start;
}

static void skin_cache_index(struct tide_skinned_draw_cache *c, const bool models)
{
    uint32_t *slots = models ? &c->model_slots : &c->pose_slots;
    uint32_t **index = models ? &c->model_index : &c->pose_index;
    const uint32_t count = models ? c->model_count : c->pose_count;
    uint32_t room = *slots ? *slots : 16u;
    while (room / 2u <= count) {
        if (room > UINT32_MAX / 2u) tide_out_of_memory();
        room *= 2u;
    }
    if ((uint64_t)room * sizeof(uint32_t) > SIZE_MAX) tide_out_of_memory();
    *index = tide_realloc(*index, (size_t)*slots * sizeof(uint32_t), (size_t)room * sizeof(uint32_t));
    *slots = room;
    memset(*index, 0, (size_t)room * sizeof(uint32_t));
    for (uint32_t i = 0; i < count; i++) {
        const uint64_t hash = models ? c->models[i].version : c->poses[i].hash;
        uint32_t at = (uint32_t)hash & (room - 1u);
        while ((*index)[at]) at = (at + 1u) & (room - 1u);
        (*index)[at] = i + 1u;
    }
}

static uint32_t skin_cache_model(struct tide_skinned_draw_cache *c, const tide_skinned_data_view *v,
                                 const uint64_t version)
{
    const size_t nodes = (size_t)v->nodes * TIDE_SKINNED_NODE_SIZE;
    const size_t palettes = (size_t)v->palettes * TIDE_SKINNED_PALETTE_SIZE;
    uint32_t at = 0u;
    if (c->model_slots) {
        at = (uint32_t)version & (c->model_slots - 1u);
        while (c->model_index[at]) {
            const uint32_t i = c->model_index[at] - 1u;
            const cached_skin_model *m = &c->models[i];
            // The full content version is an index, never proof of equality.
            // Exact captured dependencies also guard a pack replaced mid-frame.
            if (m->version == version && m->nodes == v->nodes && m->palettes == v->palettes
                && tide_same_bytes(c->bytes + m->bytes, v->data + v->node_at, nodes)
                && tide_same_bytes(c->bytes + m->bytes + nodes, v->data + v->palette_at, palettes)) return i;
            at = (at + 1u) & (c->model_slots - 1u);
        }
    }
    c->models = skin_cache_room(c->models, &c->model_capacity, c->model_count, sizeof *c->models);
    if (palettes > SIZE_MAX - nodes) tide_out_of_memory();
    const size_t bytes = skin_cache_bytes(c, nodes + palettes);
    memcpy(c->bytes + bytes, v->data + v->node_at, nodes);
    memcpy(c->bytes + bytes + nodes, v->data + v->palette_at, palettes);
    const uint32_t model = c->model_count++;
    c->models[model] = (cached_skin_model){version, v->nodes, v->palettes, bytes};
    if (c->model_count >= c->model_slots / 2u) skin_cache_index(c, true);
    else c->model_index[at] = model + 1u;
    return model;
}

static cached_skin_pose *skin_cache_pose(struct tide_skinned_draw_cache *c, const uint32_t model,
                                         const uint64_t hash, const void *trs, const size_t bytes)
{
    if (!c->pose_slots) return NULL;
    uint32_t at = (uint32_t)hash & (c->pose_slots - 1u);
    while (c->pose_index[at]) {
        cached_skin_pose *p = &c->poses[c->pose_index[at] - 1u];
        if (p->hash == hash && p->model == model && tide_same_bytes(c->bytes + p->bytes, trs, bytes)) return p;
        at = (at + 1u) & (c->pose_slots - 1u);
    }
    return NULL;
}

static void skin_cache_capture(struct tide_skinned_draw_cache *c, const uint32_t model, const uint64_t hash,
                                const void *trs, const size_t bytes, const tide_draw_list *draw, const uint32_t slot)
{
    const tide_draw_texture *t = &draw->textures[slot - 1u];
    cached_skin_pose *previous = skin_cache_pose(c, model, hash, trs, bytes);
    if (previous) {
        previous->texture_id = t->id;
        previous->texture_version = t->version;
        previous->texture = slot;
        previous->width = t->width;
        previous->height = t->height;
        return;
    }
    c->poses = skin_cache_room(c->poses, &c->pose_capacity, c->pose_count, sizeof *c->poses);
    const size_t at = skin_cache_bytes(c, bytes);
    memcpy(c->bytes + at, trs, bytes);
    const uint32_t pose = c->pose_count++;
    c->poses[pose] = (cached_skin_pose){hash, t->id, t->version, model, slot, at, t->width, t->height};
    if (c->pose_count >= c->pose_slots / 2u) skin_cache_index(c, false);
    else {
        uint32_t place = (uint32_t)hash & (c->pose_slots - 1u);
        while (c->pose_index[place]) place = (place + 1u) & (c->pose_slots - 1u);
        c->pose_index[place] = pose + 1u;
    }
}

static bool skin_cache_texture(tide_draw_list *draw, const cached_skin_pose *pose)
{
    if (pose->texture == 0u || pose->texture > draw->texture_count) return false;
    tide_draw_texture *t = &draw->textures[pose->texture - 1u];
    if (t->space != TIDE_PIXELS_LIST || t->id != pose->texture_id || t->version != pose->texture_version
        || t->format != TIDE_TEXELS_INTS || t->width != pose->width || t->height != pose->height
        || t->epoch != draw->epoch || !t->pixels) return false;
    t->frame = draw->frame;
    return true;
}

static tide_bone_transform *skin_capture_transforms(const tide_list transforms, const uint32_t nodes)
{
    if (tide_list_count(transforms) != (int32_t)nodes) return NULL;
    tide_bone_transform *out = tide_scratch_memory((size_t)nodes * sizeof *out);
    if (transforms.at >> 30 == TIDE_IN_SCRATCH) {
        const void *flat = tide_list_at(transforms, 0, sizeof *out);
        if (!flat) return NULL;
        memcpy(out, flat, (size_t)nodes * sizeof *out);
        return out;
    }
    tide_list_cache cache = {0};
    for (uint32_t i = 0; i < nodes; i++) {
        const tide_bone_transform *local = tide_list_cached_at(&cache, transforms, (int32_t)i, sizeof *out);
        if (!local) return NULL;
        out[i] = *local;
    }
    return out;
}

static uint32_t draw_word(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static tide_float4x4 *global_matrices(const tide_skinned_data_view *v, const tide_list transforms,
                                      const bool verified, const tide_bone_transform *captured)
{
    if ((!verified && !nodes_valid(v)) || tide_list_count(transforms) != (int32_t)v->nodes) return NULL;
    tide_float4x4 *global = tide_scratch_memory((size_t)v->nodes * sizeof *global);
    tide_list_cache cache = {0};
    for (uint32_t i = 0; i < v->nodes; i++) {
        int32_t parent;
        if (verified) parent = i ? (int32_t)draw_word(v->data + v->node_at + (size_t)i * TIDE_SKINNED_NODE_SIZE) : -1;
        else {
            tide_skinned_node_data n;
            (void)tide_skinned_data_node(v, i, &n);
            parent = n.parent;
        }
        const tide_bone_transform *local = captured ? &captured[i] : tide_list_cached_at(&cache, transforms, (int32_t)i, sizeof *local);
        if (!local) return NULL;
        const tide_float4x4 matrix = tide_trs_f4x4(local->position, local->rotation, local->scale);
        global[i] = parent < 0 ? matrix : tide_mul_f4x4(global[parent], matrix);
    }
    return global;
}

tide_float4x4 tide_skinned_model(const tide_list data, const tide_list transforms, const int32_t bone)
{
    tide_float4x4 out = tide_identity_f4x4();
    model_seen m;
    if (bone < 0 || !open_model(data, &m)) return out;
    const uint64_t mark = tide_scratch_mark();
    if ((uint32_t)bone < m.view.nodes) {
        const tide_float4x4 *global = global_matrices(&m.view, transforms, false, NULL);
        if (global) out = global[bone];
    }
    tide_scratch_reset(mark);
    close_model(&m);
    return out;
}

static tide_list palette_of(const tide_skinned_data_view *v, const tide_list transforms, const bool verified,
                            const tide_bone_transform *captured)
{
    const tide_float4x4 *global = global_matrices(v, transforms, verified, captured);
    if (!global) return (tide_list){0};
    tide_list out = {0};
    tide_list_resize(&out, (int32_t)v->palettes, sizeof(tide_float4x4), TIDE_IN_SCRATCH);
    if (tide_list_count(out) != (int32_t)v->palettes) return (tide_list){0};
    tide_float4x4 *matrices = tide_list_at_mut(out, 0, sizeof *matrices);
    for (uint32_t i = 0; i < v->palettes; i++) {
        uint32_t node;
        tide_float4x4 inverse_bind;
        if (verified) {
            const uint8_t *p = v->data + v->palette_at + (size_t)i * TIDE_SKINNED_PALETTE_SIZE;
            node = draw_word(p);
            for (uint32_t k = 0; k < 16u; k++) {
                const uint32_t bits = draw_word(p + 4u + k * 4u);
                memcpy((uint8_t *)&inverse_bind + k * 4u, &bits, sizeof bits);
            }
        } else {
            tide_skinned_palette_data p;
            if (!tide_skinned_data_palette(v, i, &p)) return (tide_list){0};
            node = p.node;
            memcpy(&inverse_bind, p.inverse_bind, sizeof inverse_bind);
        }
        tide_float4x4 *to = matrices ? &matrices[i] : NULL;
        if (to) *to = tide_mul_f4x4(global[node], inverse_bind);
    }
    return out;
}

tide_list tide_skinned_palette(const tide_list data, const tide_list transforms)
{
    model_seen m;
    if (!open_model(data, &m)) return (tide_list){0};
    const tide_list out = palette_of(&m.view, transforms, false, NULL);
    close_model(&m);
    return out;
}

static void matrix_words(const void *element, int32_t *words)
{
    memcpy(words, element, sizeof(tide_float4x4));
}

bool tide_draw_skinned_data(tide_draw_list *draw, const tide_list data, const tide_list transforms,
                            uint32_t textures[2], int32_t info[3])
{
    if (!textures || !info) return false;
    memset(textures, 0, 2u * sizeof *textures);
    memset(info, 0, 3u * sizeof *info);
    model_seen m;
    if (!draw || !open_model(data, &m)) return false;
    const uint64_t mark = tide_scratch_mark();
    uint64_t version;
    const bool packed = tide_pack_skinned_version(data, &version);
    bool valid = packed || tide_skinned_data_validate(m.view.data, m.view.size);
    const uint64_t pixels = (uint64_t)m.view.vertices + ((uint64_t)m.view.influences + 1u) / 2u;
    valid = valid && m.view.palettes != 0u && pixels > 0u && pixels <= TIDE_LIST_DATA_MOST;
    tide_bone_transform *captured = NULL;
    uint32_t cached_model = 0u;
    uint64_t pose_hash = 0u;
    if (valid && packed) {
        captured = skin_capture_transforms(transforms, m.view.nodes);
        valid = captured != NULL;
        if (valid) {
            if (!draw->skinned) draw->skinned = tide_alloc_zeroed(1u, sizeof *draw->skinned);
            cached_model = skin_cache_model(draw->skinned, &m.view, version);
            pose_hash = tide_hash_end(tide_hash_more(tide_hash(captured, (size_t)m.view.nodes * sizeof *captured),
                                                    &cached_model, sizeof cached_model));
            const cached_skin_pose *pose = skin_cache_pose(draw->skinned, cached_model, pose_hash, captured,
                                                           (size_t)m.view.nodes * sizeof *captured);
            if (pose && skin_cache_texture(draw, pose)) {
                textures[0] = pose->texture;
                info[0] = (int32_t)m.view.palettes;
            }
        }
    }
    if (valid && !textures[0]) {
        const tide_list palette = palette_of(&m.view, transforms, true, captured);
        valid = tide_list_count(palette) == (int32_t)m.view.palettes;
        if (valid) {
            textures[0] = tide_draw_list_data(draw, palette, sizeof(tide_float4x4), 16u, 0u, matrix_words, &info[0]);
            valid = textures[0] != 0u && info[0] == (int32_t)m.view.palettes;
            if (valid && packed) skin_cache_capture(draw->skinned, cached_model, pose_hash, captured,
                                                    (size_t)m.view.nodes * sizeof *captured, draw, textures[0]);
        }
    }
    if (valid) {
        const int32_t width = pixels < TIDE_LIST_DATA_ACROSS ? (int32_t)pixels : TIDE_LIST_DATA_ACROSS;
        const int32_t height = (int32_t)((pixels + TIDE_LIST_DATA_ACROSS - 1u) / TIDE_LIST_DATA_ACROSS);
        if (!packed) version = tide_hash(m.view.data, m.view.size);
        const uint64_t kind = UINT64_C(0x534b494e494e464c);
        uint8_t *out;
        bool kept;
        textures[1] = tide_draw_texture_edit(draw, TIDE_PIXELS_LIST, UINT64_C(1) << 63 | version >> 1,
                                             version, width, height, TIDE_TEXELS_INTS, kind, &out, &kept);
        if (out && !kept) {
            memset(out, 0, (size_t)width * (size_t)height * 16u);
            int32_t *words = (int32_t *)(void *)out;
            for (uint32_t i = 0; i < m.view.vertices; i++) {
                tide_skinned_vertex_data vertex;
                (void)tide_skinned_data_vertex(&m.view, i, &vertex);
                words[(size_t)i * 4u] = (int32_t)vertex.first_influence;
                words[(size_t)i * 4u + 1u] = (int32_t)vertex.influences;
            }
            for (uint32_t i = 0; i < m.view.influences; i++) {
                tide_skinned_influence_data influence;
                (void)tide_skinned_data_influence(&m.view, i, &influence);
                int32_t *at = words + (size_t)m.view.vertices * 4u + (size_t)i * 2u;
                at[0] = (int32_t)influence.palette;
                memcpy(&at[1], &influence.weight, sizeof at[1]);
            }
        }
        info[1] = (int32_t)m.view.vertices;
        info[2] = (int32_t)m.view.influences;
    }
    tide_scratch_reset(mark);
    close_model(&m);
    if (!valid) { memset(textures, 0, 2u * sizeof *textures); memset(info, 0, 3u * sizeof *info); }
    return valid;
}
