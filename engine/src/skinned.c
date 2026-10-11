#include "tide/skinned.h"

#include <string.h>
#include <stdlib.h>
#include <stdatomic.h>

#include "tide/jobs.h"
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
static uint32_t validated_word(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static float validated_number(const uint8_t *p)
{
    const uint32_t bits = validated_word(p);
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

// These readers require complete channel/key validation, or current immutable-
// pack provenance. The latter still needs the sampler's first-time >=0 guard.
// The opened model stays live and unchanged through sampling; no pointer escapes.
static void validated_channel(const tide_skinned_data_view *v, const uint32_t index, tide_skinned_channel_data *out)
{
    const uint8_t *p = v->data + v->channel_at + (size_t)index * TIDE_SKINNED_CHANNEL_SIZE;
    *out = (tide_skinned_channel_data){validated_word(p), validated_word(p + 4u), validated_word(p + 8u),
                                     validated_word(p + 12u), validated_word(p + 16u), validated_word(p + 20u)};
}

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

enum { SAMPLE_INTERVAL_SLOTS = 4, SAMPLE_INTERVAL_KEYS = 32 };

typedef struct sample_interval {
    const uint8_t *times;
    uint32_t keys, low;
} sample_interval;

typedef struct sample_intervals {
    sample_interval slots[SAMPLE_INTERVAL_SLOTS];
} sample_intervals;

static uint32_t sampled_interval(const tide_skinned_data_view *v, const tide_skinned_channel_data *c,
                                const float seconds, sample_intervals *intervals)
{
    // One call samples one clock against an unchanged opened model. Short
    // tracks can reuse only the search result after exact time-byte equality;
    // values, paths and interpolation never enter this cache. Bound comparisons
    // to 128 bytes, so long or colliding tracks retain bounded extra work.
    sample_interval *slot = intervals && c->keys <= SAMPLE_INTERVAL_KEYS
        ? &intervals->slots[c->keys % SAMPLE_INTERVAL_SLOTS] : NULL;
    const uint8_t *times = v->data + c->times_at;
    if (slot && slot->keys == c->keys
        && (slot->times == times || tide_same_bytes(slot->times, times, (size_t)c->keys * 4u))) return slot->low;
    uint32_t low = 0, high = c->keys;
    while (low < high) {
        const uint32_t middle = low + (high - low) / 2u;
        const float time = validated_time(v, c, middle);
        if (time <= seconds) low = middle + 1u;
        else high = middle;
    }
    if (slot) *slot = (sample_interval){times, c->keys, low};
    return low;
}

static tide_float4 sampled_value(const tide_skinned_data_view *v, const tide_skinned_channel_data *c,
                                const float seconds, sample_intervals *intervals)
{
    const uint32_t low = sampled_interval(v, c, seconds, intervals);
    float time, value[4], in[4], out[4];
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
    bool nonnegative = false;
    const bool packed = tide_pack_skinned_sample_proof(data, &nonnegative);
    tide_skinned_clip_data clip;
    bool valid = (packed || nodes_valid(&m.view)) && tide_skinned_data_clip(&m.view, (uint32_t)animation, &clip);
    for (uint32_t i = 0; valid && !(packed && nonnegative) && i < clip.channels; i++) {
        tide_skinned_channel_data channel;
        if (packed) {
            validated_channel(&m.view, clip.first_channel + i, &channel);
            // The ordinary proof permits first times in (-1, 0). Without
            // the stronger load-time fact, check this selected clip only.
            valid = validated_time(&m.view, &channel, 0u) >= 0.0f;
        } else valid = tide_skinned_data_channel(&m.view, clip.first_channel + i, &channel)
                   && channel_valid(&m.view, &channel);
    }
    if (valid) {
        fill_rest(&m.view, transforms, where);
        const float clock = tide_clamp_f(seconds, 0.0f, clip.length);
        tide_bone_transform *flat = scratch_transforms(*transforms, m.view.nodes);
        tide_list_cache cache = {0};
        sample_intervals intervals = {0};
        for (uint32_t i = 0; i < clip.channels; i++) {
            tide_skinned_channel_data channel;
            validated_channel(&m.view, clip.first_channel + i, &channel);
            const tide_float4 value = sampled_value(&m.view, &channel, clock, &intervals);
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
    const uint32_t mark = tide_scratch_mark();
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
            const tide_float4 value = sampled_value(&source.view, &channel, clock, NULL);
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
    const uint32_t mark = tide_scratch_mark();
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
    const uint32_t mark = tide_scratch_mark();
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
    uint64_t version, identity;
    uint32_t nodes, palettes;
    size_t bytes;
} cached_skin_model;

typedef struct cached_skin_pose {
    uint64_t hash, texture_id, texture_version;
    uint32_t model, texture;
    size_t bytes;
    int32_t width, height;
    uint32_t base;
    bool atlas, pending;
} cached_skin_pose;

typedef struct skin_atlas_page {
    uint64_t id, hash;
    uint32_t texture, count;
} skin_atlas_page;

typedef struct cached_skin_rest {
    uint64_t identity, version, atlas_id;
    uint32_t pose[2], page;
    int32_t info[3];
    cached_skin_pose influences;
} cached_skin_rest;

struct tide_skinned_draw_cache {
    cached_skin_model *models;
    cached_skin_pose *poses;
    uint32_t model_count, model_capacity, pose_count, pose_capacity;
    uint32_t *model_index, *pose_index;
    uint32_t model_slots, pose_slots;
    uint8_t *bytes;
    size_t used, capacity;
    skin_atlas_page *pages;
    uint32_t page_count, page_built, page_capacity;
    cached_skin_rest *rests;
    uint32_t rest_count, rest_capacity, *rest_index, rest_slots;
};

void tide_skinned_draw_cache_reset(tide_draw_list *draw)
{
    struct tide_skinned_draw_cache *c = draw->skinned;
    if (!c) return;
    c->model_count = c->pose_count = 0u;
    c->rest_count = 0u;
    c->used = 0u;
    c->page_count = 0u;
    for (uint32_t i = 0; i < c->page_built; i++) {
        c->pages[i].texture = c->pages[i].count = 0u;
        c->pages[i].hash = TIDE_HASH_START;
    }
    if (c->model_slots) memset(c->model_index, 0, (size_t)c->model_slots * sizeof(uint32_t));
    if (c->pose_slots) memset(c->pose_index, 0, (size_t)c->pose_slots * sizeof(uint32_t));
    if (c->rest_slots) memset(c->rest_index, 0, (size_t)c->rest_slots * sizeof(uint32_t));
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
    free(c->pages);
    free(c->rests);
    free(c->rest_index);
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
                                 const uint64_t version, const uint64_t identity)
{
    const size_t nodes = (size_t)v->nodes * TIDE_SKINNED_NODE_SIZE;
    const size_t palettes = (size_t)v->palettes * TIDE_SKINNED_PALETTE_SIZE;
    uint32_t at = 0u;
    if (c->model_slots) {
        at = (uint32_t)version & (c->model_slots - 1u);
        while (c->model_index[at]) {
            const uint32_t i = c->model_index[at] - 1u;
            const cached_skin_model *m = &c->models[i];
            // A current pack identity proves the captured dependencies have
            // not changed. Other identities still compare exact owned bytes,
            // including equal-content replacements and version collisions.
            if (m->version == version && m->nodes == v->nodes && m->palettes == v->palettes
                && (m->identity == identity
                    || (tide_same_bytes(c->bytes + m->bytes, v->data + v->node_at, nodes)
                        && tide_same_bytes(c->bytes + m->bytes + nodes, v->data + v->palette_at, palettes)))) return i;
            at = (at + 1u) & (c->model_slots - 1u);
        }
    }
    c->models = skin_cache_room(c->models, &c->model_capacity, c->model_count, sizeof *c->models);
    if (palettes > SIZE_MAX - nodes) tide_out_of_memory();
    const size_t bytes = skin_cache_bytes(c, nodes + palettes);
    memcpy(c->bytes + bytes, v->data + v->node_at, nodes);
    memcpy(c->bytes + bytes + nodes, v->data + v->palette_at, palettes);
    const uint32_t model = c->model_count++;
    c->models[model] = (cached_skin_model){version, identity, v->nodes, v->palettes, bytes};
    if (c->model_count >= c->model_slots / 2u) skin_cache_index(c, true);
    else c->model_index[at] = model + 1u;
    return model;
}

static cached_skin_pose *skin_cache_pose(struct tide_skinned_draw_cache *c, const uint32_t model,
                                         const uint64_t hash, const void *trs, const size_t bytes, const bool atlas)
{
    if (!c->pose_slots) return NULL;
    uint32_t at = (uint32_t)hash & (c->pose_slots - 1u);
    while (c->pose_index[at]) {
        cached_skin_pose *p = &c->poses[c->pose_index[at] - 1u];
        if (p->hash == hash && p->model == model && p->atlas == atlas && tide_same_bytes(c->bytes + p->bytes, trs, bytes)) return p;
        at = (at + 1u) & (c->pose_slots - 1u);
    }
    return NULL;
}

static void skin_cache_capture(struct tide_skinned_draw_cache *c, const uint32_t model, const uint64_t hash,
                                const void *trs, const size_t bytes, const tide_draw_list *draw, const uint32_t slot)
{
    const tide_draw_texture *t = &draw->textures[slot - 1u];
    cached_skin_pose *previous = skin_cache_pose(c, model, hash, trs, bytes, false);
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
    c->poses[pose] = (cached_skin_pose){.hash = hash, .texture_id = t->id, .texture_version = t->version,
                                      .model = model, .texture = slot, .bytes = at, .width = t->width, .height = t->height};
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

static const tide_bone_transform *skin_capture_transforms(const tide_list transforms, const uint32_t nodes)
{
    if (tide_list_count(transforms) != (int32_t)nodes) return NULL;
    // Actual scratch lists are contiguous and remain live through this
    // synchronous call. Retained cache/atlas captures still copy these bytes
    // before returning; heap/chunked inputs need a contiguous staging copy.
    if (transforms.at >> 30 == TIDE_IN_SCRATCH) return tide_list_at(transforms, 0, sizeof(tide_bone_transform));
    tide_bone_transform *out = tide_scratch_memory((size_t)nodes * sizeof *out);
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
    const uint32_t mark = tide_scratch_mark();
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

static uint32_t capture_influences(tide_draw_list *draw, const tide_skinned_data_view *v, const uint64_t version)
{
    const uint64_t pixels = (uint64_t)v->vertices + ((uint64_t)v->influences + 1u) / 2u;
    const int32_t width = pixels < TIDE_LIST_DATA_ACROSS ? (int32_t)pixels : TIDE_LIST_DATA_ACROSS;
    const int32_t height = (int32_t)((pixels + TIDE_LIST_DATA_ACROSS - 1u) / TIDE_LIST_DATA_ACROSS);
    const uint64_t kind = UINT64_C(0x534b494e494e464c);
    uint8_t *out;
    bool kept;
    const uint32_t texture = tide_draw_texture_edit(draw, TIDE_PIXELS_LIST, UINT64_C(1) << 63 | version >> 1,
                                                    version, width, height, TIDE_TEXELS_INTS, kind, &out, &kept);
    if (out && !kept) {
        memset(out, 0, (size_t)width * (size_t)height * 16u);
        int32_t *words = (int32_t *)(void *)out;
        for (uint32_t i = 0; i < v->vertices; i++) {
            tide_skinned_vertex_data vertex;
            (void)tide_skinned_data_vertex(v, i, &vertex);
            words[(size_t)i * 4u] = (int32_t)vertex.first_influence;
            words[(size_t)i * 4u + 1u] = (int32_t)vertex.influences;
        }
        for (uint32_t i = 0; i < v->influences; i++) {
            tide_skinned_influence_data influence;
            (void)tide_skinned_data_influence(v, i, &influence);
            int32_t *at = words + (size_t)v->vertices * 4u + (size_t)i * 2u;
            at[0] = (int32_t)influence.palette;
            memcpy(&at[1], &influence.weight, sizeof at[1]);
        }
    }
    return texture;
}

bool tide_draw_skinned_data(tide_draw_list *draw, const tide_list data, const tide_list transforms,
                            uint32_t textures[2], int32_t info[3])
{
    if (!textures || !info) return false;
    memset(textures, 0, 2u * sizeof *textures);
    memset(info, 0, 3u * sizeof *info);
    model_seen m;
    if (!draw || !open_model(data, &m)) return false;
    const uint32_t mark = tide_scratch_mark();
    uint64_t version, identity;
    const bool packed = tide_pack_skinned_proof(data, &identity, &version);
    bool valid = packed || tide_skinned_data_validate(m.view.data, m.view.size);
    const uint64_t pixels = (uint64_t)m.view.vertices + ((uint64_t)m.view.influences + 1u) / 2u;
    valid = valid && m.view.palettes != 0u && pixels > 0u && pixels <= TIDE_LIST_DATA_MOST;
    const tide_bone_transform *captured = NULL;
    uint32_t cached_model = 0u;
    uint64_t pose_hash = 0u;
    if (valid && packed) {
        captured = skin_capture_transforms(transforms, m.view.nodes);
        valid = captured != NULL;
        if (valid) {
            if (!draw->skinned) draw->skinned = tide_alloc_zeroed(1u, sizeof *draw->skinned);
            cached_model = skin_cache_model(draw->skinned, &m.view, version, identity);
            pose_hash = tide_hash_end(tide_hash_more(tide_hash_wide(captured, (size_t)m.view.nodes * sizeof *captured),
                                                    &cached_model, sizeof cached_model));
            const cached_skin_pose *pose = skin_cache_pose(draw->skinned, cached_model, pose_hash, captured,
                                                           (size_t)m.view.nodes * sizeof *captured, false);
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
        if (!packed) version = tide_hash(m.view.data, m.view.size);
        textures[1] = capture_influences(draw, &m.view, version);
        info[1] = (int32_t)m.view.vertices;
        info[2] = (int32_t)m.view.influences;
    }
    tide_scratch_reset(mark);
    close_model(&m);
    if (!valid) { memset(textures, 0, 2u * sizeof *textures); memset(info, 0, 3u * sizeof *info); }
    return valid;
}

// Nonempty container identities have a nonzero list handle in their low
// word; scratch content identities have their high bit set. Atlas pages use
// neither, and each page keeps its own identity across frames and list moves.
static atomic_uint atlas_identity;

static skin_atlas_page *atlas_page(tide_draw_list *draw, const uint32_t matrices)
{
    struct tide_skinned_draw_cache *c = draw->skinned;
    const uint32_t most = TIDE_LIST_DATA_MOST / 4u;
    if (!matrices || matrices > most) return NULL;
    // Ordinary pages stay at 4 MiB; a single larger valid palette gets a
    // page of its own within the existing shader-data texture bound.
    const uint32_t limit = matrices > 65536u ? matrices : 65536u;
    skin_atlas_page *p = c->page_count ? &c->pages[c->page_count - 1u] : NULL;
    if (!p || p->count > limit || matrices > limit - p->count) {
        const uint32_t at = c->page_count++;
        if (at == c->page_built) {
            c->pages = skin_cache_room(c->pages, &c->page_capacity, at, sizeof *c->pages);
            const uint32_t id = atomic_fetch_add_explicit(&atlas_identity, 1u, memory_order_relaxed) + 1u;
            if (!id || id > INT32_MAX) tide_out_of_memory();
            c->pages[at] = (skin_atlas_page){.id = (uint64_t)id << 32, .hash = TIDE_HASH_START};
            c->page_built++;
        }
        p = &c->pages[at];
    }
    const uint32_t count = p->count + matrices, pixels = count * 4u;
    const int32_t width = pixels < TIDE_LIST_DATA_ACROSS ? (int32_t)pixels : TIDE_LIST_DATA_ACROSS;
    const int32_t height = (int32_t)((pixels + TIDE_LIST_DATA_ACROSS - 1u) / TIDE_LIST_DATA_ACROSS);
    if (!p->texture) {
        uint8_t *out;
        bool kept;
        p->texture = tide_draw_texture_edit(draw, TIDE_PIXELS_LIST, p->id, 0u, width, height,
                                             TIDE_TEXELS_INTS, TIDE_SKINNED_ATLAS_KIND, &out, &kept);
    }
    tide_draw_texture *t = &draw->textures[p->texture - 1u];
    const uint32_t bytes = (uint32_t)width * (uint32_t)height * 16u;
    if (bytes > t->capacity) {
        uint32_t capacity = t->capacity ? t->capacity : 4096u;
        while (capacity < bytes) capacity *= 2u;
        t->pixels = tide_realloc(t->pixels, t->capacity, capacity);
        t->capacity = capacity;
    }
    // Growing a page changes no earlier matrix's flat offset. Reserve the
    // full logical image before workers receive any of its raw slice pointers.
    t->width = width;
    t->height = height;
    t->format = TIDE_TEXELS_INTS;
    t->frame = draw->frame;
    t->epoch = draw->epoch;
    if (t->changes) t->changes->known = false;
    return p;
}

static cached_skin_pose *atlas_capture(tide_draw_list *draw, const uint32_t model, const uint64_t version,
                                       const uint64_t hash, const void *trs, const size_t bytes)
{
    struct tide_skinned_draw_cache *c = draw->skinned;
    cached_skin_pose *previous = skin_cache_pose(c, model, hash, trs, bytes, true);
    if (previous) {
        draw->textures[previous->texture - 1u].frame = draw->frame;
        return previous;
    }
    const uint32_t matrices = c->models[model].palettes;
    skin_atlas_page *page = atlas_page(draw, matrices);
    if (!page) return NULL;
    c->poses = skin_cache_room(c->poses, &c->pose_capacity, c->pose_count, sizeof *c->poses);
    const size_t at = skin_cache_bytes(c, bytes);
    memcpy(c->bytes + at, trs, bytes);
    const uint32_t i = c->pose_count++;
    const uint32_t base = page->count;
    page->count += matrices;
    page->hash = tide_hash_more(page->hash, &version, sizeof version);
    page->hash = tide_hash_more(page->hash, &hash, sizeof hash);
    page->hash = tide_hash_more(page->hash, &matrices, sizeof matrices);
    tide_draw_texture *t = &draw->textures[page->texture - 1u];
    t->version = tide_hash_end(page->hash);
    c->poses[i] = (cached_skin_pose){.hash = hash, .model = model, .texture = page->texture,
                                    .bytes = at, .base = base, .atlas = true, .pending = true};
    if (c->pose_count >= c->pose_slots / 2u) skin_cache_index(c, false);
    else {
        uint32_t place = (uint32_t)hash & (c->pose_slots - 1u);
        while (c->pose_index[place]) place = (place + 1u) & (c->pose_slots - 1u);
        c->pose_index[place] = i + 1u;
    }
    return &c->poses[i];
}

bool tide_draw_skinned_atlas(tide_draw_list *draw, const tide_list data, const tide_list transforms,
                             uint32_t textures[2], int32_t info[4])
{
    if (!textures || !info) return false;
    memset(textures, 0, 2u * sizeof *textures);
    memset(info, 0, 4u * sizeof *info);
    uint64_t version, identity;
    if (!tide_pack_skinned_proof(data, &identity, &version)) return tide_draw_skinned_data(draw, data, transforms, textures, info);
    model_seen m;
    if (!draw || !open_model(data, &m)) return false;
    const uint32_t mark = tide_scratch_mark();
    const uint64_t pixels = (uint64_t)m.view.vertices + ((uint64_t)m.view.influences + 1u) / 2u;
    bool valid = m.view.palettes && m.view.palettes <= TIDE_LIST_DATA_MOST / 4u
        && pixels > 0u && pixels <= TIDE_LIST_DATA_MOST;
    const tide_bone_transform *trs = valid ? skin_capture_transforms(transforms, m.view.nodes) : NULL;
    valid = valid && trs != NULL;
    if (valid) {
        if (!draw->skinned) draw->skinned = tide_alloc_zeroed(1u, sizeof *draw->skinned);
        const uint32_t model = skin_cache_model(draw->skinned, &m.view, version, identity);
        const size_t bytes = (size_t)m.view.nodes * sizeof *trs;
        const uint64_t hash = tide_hash_end(tide_hash_more(tide_hash_wide(trs, bytes), &model, sizeof model));
        const cached_skin_pose *pose = atlas_capture(draw, model, version, hash, trs, bytes);
        valid = pose != NULL;
        if (valid) {
            textures[0] = pose->texture;
            textures[1] = capture_influences(draw, &m.view, version);
            info[0] = (int32_t)m.view.palettes;
            info[1] = (int32_t)m.view.vertices;
            info[2] = (int32_t)m.view.influences;
            info[3] = (int32_t)pose->base;
        }
    }
    tide_scratch_reset(mark);
    close_model(&m);
    if (!valid) { memset(textures, 0, 2u * sizeof *textures); memset(info, 0, 4u * sizeof *info); }
    return valid;
}

static cached_skin_rest *skin_rest_find(struct tide_skinned_draw_cache *c, const uint64_t identity)
{
    if (!c || !c->rest_slots) return NULL;
    uint32_t at = (uint32_t)identity & (c->rest_slots - 1u);
    while (c->rest_index[at]) {
        cached_skin_rest *rest = &c->rests[c->rest_index[at] - 1u];
        if (rest->identity == identity) return rest;
        at = (at + 1u) & (c->rest_slots - 1u);
    }
    return NULL;
}

static void skin_rest_index(struct tide_skinned_draw_cache *c)
{
    uint32_t slots = c->rest_slots ? c->rest_slots : 16u;
    while (slots / 2u <= c->rest_count) {
        if (slots > UINT32_MAX / 2u) tide_out_of_memory();
        slots *= 2u;
    }
    if ((uint64_t)slots * sizeof(uint32_t) > SIZE_MAX) tide_out_of_memory();
    c->rest_index = tide_realloc(c->rest_index, (size_t)c->rest_slots * sizeof(uint32_t), (size_t)slots * sizeof(uint32_t));
    c->rest_slots = slots;
    memset(c->rest_index, 0, (size_t)slots * sizeof(uint32_t));
    for (uint32_t i = 0; i < c->rest_count; i++) {
        uint32_t at = (uint32_t)c->rests[i].identity & (slots - 1u);
        while (c->rest_index[at]) at = (at + 1u) & (slots - 1u);
        c->rest_index[at] = i + 1u;
    }
}

static bool skin_rest_cached(tide_draw_list *draw, const cached_skin_rest *rest, const bool atlas,
                             uint32_t textures[2], int32_t *info, bool *palette_bad)
{
    *palette_bad = false;
    struct tide_skinned_draw_cache *c = draw->skinned;
    const uint32_t pose_index = rest->pose[atlas ? 1u : 0u];
    if (!pose_index || pose_index > c->pose_count) return false;
    const cached_skin_pose *pose = &c->poses[pose_index - 1u];
    if (atlas) {
        // A remembered atlas slice must not fall through to atlas_capture's
        // generic pose hit after its texture slot has failed this guard.
        *palette_bad = true;
        if (rest->page >= c->page_count || !pose->atlas || !pose->texture || pose->texture > draw->texture_count) return false;
        const skin_atlas_page *page = &c->pages[rest->page];
        tide_draw_texture *t = &draw->textures[pose->texture - 1u];
        const uint64_t pixels = (uint64_t)page->count * 4u;
        const int32_t width = pixels < TIDE_LIST_DATA_ACROSS ? (int32_t)pixels : TIDE_LIST_DATA_ACROSS;
        const int32_t height = (int32_t)((pixels + TIDE_LIST_DATA_ACROSS - 1u) / TIDE_LIST_DATA_ACROSS);
        if (page->texture != pose->texture || page->id != rest->atlas_id || t->id != page->id
            || t->space != TIDE_PIXELS_LIST || !t->changes || t->changes->kind != TIDE_SKINNED_ATLAS_KIND || t->format != TIDE_TEXELS_INTS
            || t->version != tide_hash_end(page->hash) || t->epoch != draw->epoch || t->width != width || t->height != height
            || !t->pixels || (uint64_t)(uint32_t)width * (uint32_t)height * 16u > t->capacity
            || pose->base > page->count || (uint32_t)rest->info[0] > page->count - pose->base) return false;
        t->frame = draw->frame;
        *palette_bad = false;
    } else if (!skin_cache_texture(draw, pose)) return false;
    if (!skin_cache_texture(draw, &rest->influences)) return false;
    textures[0] = pose->texture;
    textures[1] = rest->influences.texture;
    memcpy(info, rest->info, sizeof rest->info);
    if (atlas) info[3] = (int32_t)pose->base;
    return true;
}

static void skin_rest_remember(tide_draw_list *draw, const uint64_t identity, const uint64_t version,
                               const bool atlas, const uint32_t textures[2], const int32_t *info)
{
    struct tide_skinned_draw_cache *c = draw->skinned;
    uint32_t pose = c->pose_count;
    while (pose) {
        const cached_skin_pose *p = &c->poses[--pose];
        if (p->texture == textures[0] && p->atlas == atlas && (!atlas || p->base == (uint32_t)info[3])) break;
    }
    if (!c->pose_count || c->poses[pose].texture != textures[0] || c->poses[pose].atlas != atlas
        || (atlas && c->poses[pose].base != (uint32_t)info[3])) return;
    cached_skin_rest *rest = skin_rest_find(c, identity);
    if (!rest) {
        c->rests = skin_cache_room(c->rests, &c->rest_capacity, c->rest_count, sizeof *c->rests);
        const uint32_t i = c->rest_count++;
        c->rests[i] = (cached_skin_rest){.identity = identity, .version = version};
        if (c->rest_count >= c->rest_slots / 2u) skin_rest_index(c);
        else {
            uint32_t at = (uint32_t)identity & (c->rest_slots - 1u);
            while (c->rest_index[at]) at = (at + 1u) & (c->rest_slots - 1u);
            c->rest_index[at] = i + 1u;
        }
        rest = &c->rests[i];
    }
    rest->pose[atlas ? 1u : 0u] = pose + 1u;
    memcpy(rest->info, info, sizeof rest->info);
    const tide_draw_texture *t = &draw->textures[textures[1] - 1u];
    rest->influences = (cached_skin_pose){.texture = textures[1], .texture_id = t->id, .texture_version = t->version,
                                         .width = t->width, .height = t->height};
    if (atlas) {
        rest->atlas_id = draw->textures[textures[0] - 1u].id;
        for (uint32_t i = 0; i < c->page_count; i++) {
            if (c->pages[i].texture == textures[0]) { rest->page = i; break; }
        }
    }
}

static bool draw_skinned_rest(tide_draw_list *draw, const tide_list data, const bool atlas,
                              uint32_t textures[2], int32_t *info)
{
    if (!textures || !info) return false;
    memset(textures, 0, 2u * sizeof *textures);
    memset(info, 0, (atlas ? 4u : 3u) * sizeof *info);
    if (!draw) return false;
    uint64_t identity, version;
    const bool proven = tide_pack_skinned_proof(data, &identity, &version);
    cached_skin_rest *rest = proven ? skin_rest_find(draw->skinned, identity) : NULL;
    bool palette_bad = false;
    if (rest && rest->version == version && skin_rest_cached(draw, rest, atlas, textures, info, &palette_bad)) return true;
    // Only the first capture needs authored TRS. Existing draw capture owns
    // those values and model dependencies before this scratch scope ends.
    const uint32_t mark = tide_scratch_mark();
    tide_list transforms = {0};
    const bool capture_atlas = atlas && !palette_bad;
    const bool valid = tide_skinned_rest(data, &transforms, TIDE_IN_SCRATCH)
        && (capture_atlas ? tide_draw_skinned_atlas(draw, data, transforms, textures, info)
                          : tide_draw_skinned_data(draw, data, transforms, textures, info));
    if (valid && proven && draw->skinned) skin_rest_remember(draw, identity, version, capture_atlas, textures, info);
    tide_scratch_reset(mark);
    return valid;
}

bool tide_draw_skinned_rest_data(tide_draw_list *draw, const tide_list data, uint32_t textures[2], int32_t info[3])
{
    return draw_skinned_rest(draw, data, false, textures, info);
}

bool tide_draw_skinned_rest_atlas(tide_draw_list *draw, const tide_list data, uint32_t textures[2], int32_t info[4])
{
    return draw_skinned_rest(draw, data, true, textures, info);
}

typedef struct skin_prepare_work {
    const tide_draw_list *draw;
    struct tide_skinned_draw_cache *cache;
    atomic_uint next;
    uint32_t count;
} skin_prepare_work;

static void prepare_atlas_poses(void *context, const uint32_t thread)
{
    (void)thread;
    skin_prepare_work *work = context;
    struct tide_skinned_draw_cache *c = work->cache;
    tide_memory_sync();
    for (;;) {
        const uint32_t first = atomic_fetch_add_explicit(&work->next, 4u, memory_order_relaxed);
        if (first >= work->count) break;
        const uint32_t end = work->count - first < 4u ? work->count : first + 4u;
        for (uint32_t i = first; i < end; i++) {
            cached_skin_pose *pose = &c->poses[i];
            if (!pose->atlas || !pose->pending) continue;
            const cached_skin_model *model = &c->models[pose->model];
            const uint8_t *nodes = c->bytes + model->bytes;
            const uint8_t *palettes = nodes + (size_t)model->nodes * TIDE_SKINNED_NODE_SIZE;
            const tide_bone_transform *trs = (const tide_bone_transform *)(const void *)(c->bytes + pose->bytes);
            const uint32_t mark = tide_scratch_mark();
            tide_float4x4 *global = tide_scratch_memory((size_t)model->nodes * sizeof *global);
            // Captured tables were fully validated while their pack proof
            // was current. Only these owned bytes and exact current TRS are
            // used here; no pack, list handle or mutable cache escapes to a
            // worker. Each pose owns a disjoint preallocated output slice.
            for (uint32_t n = 0; n < model->nodes; n++) {
                const int32_t parent = n ? (int32_t)draw_word(nodes + (size_t)n * TIDE_SKINNED_NODE_SIZE) : -1;
                const tide_float4x4 local = tide_trs_f4x4(trs[n].position, trs[n].rotation, trs[n].scale);
                global[n] = parent < 0 ? local : tide_mul_f4x4(global[parent], local);
            }
            tide_float4x4 *out = (tide_float4x4 *)(void *)(work->draw->textures[pose->texture - 1u].pixels
                                                         + (size_t)pose->base * sizeof *out);
            for (uint32_t p = 0; p < model->palettes; p++) {
                const uint8_t *bytes = palettes + (size_t)p * TIDE_SKINNED_PALETTE_SIZE;
                const uint32_t node = draw_word(bytes);
                tide_float4x4 inverse_bind;
                for (uint32_t k = 0; k < 16u; k++) {
                    const uint32_t bits = draw_word(bytes + 4u + k * 4u);
                    memcpy((uint8_t *)&inverse_bind + k * 4u, &bits, sizeof bits);
                }
                out[p] = tide_mul_f4x4(global[node], inverse_bind);
            }
            pose->pending = false;
            tide_scratch_reset(mark);
        }
    }
}

void tide_draw_prepare(const tide_draw_list *draw, const tide_jobs *jobs)
{
    if (draw) tide_draw_gather_levels(draw);
    if (!draw || !draw->skinned) return;
    struct tide_skinned_draw_cache *c = draw->skinned;
    uint64_t nodes = 0u;
    uint32_t pending = 0u;
    for (uint32_t i = 0; i < c->pose_count; i++) {
        if (c->poses[i].atlas && c->poses[i].pending) {
            pending++;
            nodes += c->models[c->poses[i].model].nodes;
        }
    }
    if (!pending) return;
    skin_prepare_work work = {.draw = draw, .cache = c, .count = c->pose_count};
    atomic_init(&work.next, 0u);
    if (jobs && (jobs->run_some || jobs->run) && jobs->threads > 1u && nodes >= 4096u && pending >= 16u) {
        uint32_t others = pending / 32u;
        if (!others) others = 1u;
        if (others > 3u) others = 3u;
        if (others >= jobs->threads) others = jobs->threads - 1u;
        if (jobs->run_some) jobs->run_some(jobs->self, others, prepare_atlas_poses, &work);
        else jobs->run(jobs->self, prepare_atlas_poses, &work);
        tide_memory_sync();
    } else prepare_atlas_poses(&work, 0u);
    for (uint32_t i = 0; i < c->page_count; i++) {
        const skin_atlas_page *page = &c->pages[i];
        const tide_draw_texture *t = &draw->textures[page->texture - 1u];
        const size_t used = (size_t)page->count * sizeof(tide_float4x4);
        const size_t bytes = (size_t)t->width * (size_t)t->height * 16u;
        if (bytes > used) memset(t->pixels + used, 0, bytes - used);
    }
}
