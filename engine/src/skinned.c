#include "tide/skinned.h"

#include <string.h>

#include "tide/mesh.h"
#include "tide/net.h"
#include "tide/skinned_data.h"

_Static_assert(sizeof(tide_bone_transform) == 40u, "bone transforms have ten float words");

typedef struct model_seen {
    tide_skinned_data_view view;
    tide_list list;
    void *bytes;
    bool copied;
} model_seen;

static bool open_model(const tide_list data, model_seen *m)
{
    memset(m, 0, sizeof *m);
    const int32_t count = tide_list_count(data);
    if (count < (int32_t)TIDE_SKINNED_HEADER_SIZE) return false;
    m->list = data;
    m->bytes = tide_list_flatten(data, 1u, false, &m->copied);
    if (m->bytes && tide_skinned_data_open(m->bytes, (size_t)count, &m->view)) return true;
    tide_list_unflatten(data, m->bytes, 1u, false, m->copied);
    memset(m, 0, sizeof *m);
    return false;
}

static void close_model(model_seen *m)
{
    tide_list_unflatten(m->list, m->bytes, 1u, false, m->copied);
}

static bool nodes_valid(const tide_skinned_data_view *v)
{
    if (v->nodes == 0u) return false;
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

static void fill_rest(const tide_skinned_data_view *v, tide_list *transforms, const uint32_t where)
{
    tide_list_resize(transforms, (int32_t)v->nodes, sizeof(tide_bone_transform), where);
    tide_list_cache cache = {0};
    for (uint32_t i = 0; i < v->nodes; i++) {
        tide_skinned_node_data n;
        (void)tide_skinned_data_node(v, i, &n);
        tide_bone_transform *to = tide_list_cached_at_mut(&cache, *transforms, (int32_t)i, sizeof *to);
        if (to) *to = local_of(&n);
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
    model_seen first, second;
    if (!open_model(a, &first)) return false;
    if (!open_model(b, &second)) {
        close_model(&first);
        return false;
    }
    const bool same = first.view.nodes == second.view.nodes
        && memcmp(first.view.skeleton_hash, second.view.skeleton_hash, sizeof first.view.skeleton_hash) == 0;
    close_model(&second);
    close_model(&first);
    return same;
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
    const bool valid = nodes_valid(&m.view);
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

static tide_float4 sampled_value(const tide_skinned_data_view *v, const tide_skinned_channel_data *c, const float seconds)
{
    uint32_t low = 0, high = c->keys;
    float time, value[4], in[4], out[4];
    while (low < high) {
        const uint32_t middle = low + (high - low) / 2u;
        (void)tide_skinned_data_key(v, c, middle, &time, value, in, out);
        if (time <= seconds) low = middle + 1u;
        else high = middle;
    }
    const uint32_t before = low ? low - 1u : 0u;
    float first_time, first[4], unused[4], first_out[4];
    (void)tide_skinned_data_key(v, c, before, &first_time, first, unused, first_out);
    tide_float4 a = tide_f4(first[0], first[1], first[2], first[3]);
    if (low == 0u || low == c->keys || c->interpolation == TIDE_SKINNED_STEP) return a;
    (void)tide_skinned_data_key(v, c, low, &time, value, in, out);
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
    tide_skinned_clip_data clip;
    bool valid = nodes_valid(&m.view) && tide_skinned_data_clip(&m.view, (uint32_t)animation, &clip);
    for (uint32_t i = 0; valid && i < clip.channels; i++) {
        tide_skinned_channel_data channel;
        valid = tide_skinned_data_channel(&m.view, clip.first_channel + i, &channel) && channel_valid(&m.view, &channel);
    }
    if (valid) {
        fill_rest(&m.view, transforms, where);
        const float clock = tide_clamp_f(seconds, 0.0f, clip.length);
        tide_list_cache cache = {0};
        for (uint32_t i = 0; i < clip.channels; i++) {
            tide_skinned_channel_data channel;
            (void)tide_skinned_data_channel(&m.view, clip.first_channel + i, &channel);
            const tide_float4 value = sampled_value(&m.view, &channel, clock);
            tide_bone_transform *to = tide_list_cached_at_mut(&cache, *transforms, (int32_t)channel.node, sizeof *to);
            if (!to) continue;
            if (channel.path == TIDE_SKINNED_TRANSLATION) to->position = tide_f3(value.x, value.y, value.z);
            else if (channel.path == TIDE_SKINNED_SCALE) to->scale = tide_f3(value.x, value.y, value.z);
            else to->rotation = tide_normalize_q((tide_quaternion){value});
        }
    }
    close_model(&m);
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

static tide_float4x4 *global_matrices(const tide_skinned_data_view *v, const tide_list transforms)
{
    if (!nodes_valid(v) || tide_list_count(transforms) != (int32_t)v->nodes) return NULL;
    tide_float4x4 *global = tide_scratch_memory((size_t)v->nodes * sizeof *global);
    tide_list_cache cache = {0};
    for (uint32_t i = 0; i < v->nodes; i++) {
        tide_skinned_node_data n;
        (void)tide_skinned_data_node(v, i, &n);
        const tide_bone_transform *local = tide_list_cached_at(&cache, transforms, (int32_t)i, sizeof *local);
        if (!local) return NULL;
        const tide_float4x4 matrix = tide_trs_f4x4(local->position, local->rotation, local->scale);
        global[i] = n.parent < 0 ? matrix : tide_mul_f4x4(global[n.parent], matrix);
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
        const tide_float4x4 *global = global_matrices(&m.view, transforms);
        if (global) out = global[bone];
    }
    tide_scratch_reset(mark);
    close_model(&m);
    return out;
}

static tide_list palette_of(const tide_skinned_data_view *v, const tide_list transforms)
{
    const tide_float4x4 *global = global_matrices(v, transforms);
    if (!global) return (tide_list){0};
    tide_list out = {0};
    tide_list_resize(&out, (int32_t)v->palettes, sizeof(tide_float4x4), TIDE_IN_SCRATCH);
    for (uint32_t i = 0; i < v->palettes; i++) {
        tide_skinned_palette_data p;
        if (!tide_skinned_data_palette(v, i, &p)) return (tide_list){0};
        tide_float4x4 inverse_bind;
        memcpy(&inverse_bind, p.inverse_bind, sizeof inverse_bind);
        tide_float4x4 *to = tide_list_at_mut(out, (int32_t)i, sizeof *to);
        if (to) *to = tide_mul_f4x4(global[p.node], inverse_bind);
    }
    return out;
}

tide_list tide_skinned_palette(const tide_list data, const tide_list transforms)
{
    model_seen m;
    if (!open_model(data, &m)) return (tide_list){0};
    const tide_list out = palette_of(&m.view, transforms);
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
    const uint32_t mark = tide_scratch_mark();
    bool valid = tide_skinned_data_validate(m.view.data, m.view.size);
    tide_list palette = valid ? palette_of(&m.view, transforms) : (tide_list){0};
    valid = valid && tide_list_count(palette) == (int32_t)m.view.palettes && m.view.palettes != 0u;
    const uint64_t pixels = (uint64_t)m.view.vertices + ((uint64_t)m.view.influences + 1u) / 2u;
    valid = valid && pixels > 0u && pixels <= TIDE_LIST_DATA_MOST;
    if (valid) {
        textures[0] = tide_draw_list_data(draw, palette, sizeof(tide_float4x4), 16u, 0u, matrix_words, &info[0]);
        valid = info[0] == (int32_t)m.view.palettes;
    }
    if (valid) {
        const int32_t width = pixels < TIDE_LIST_DATA_ACROSS ? (int32_t)pixels : TIDE_LIST_DATA_ACROSS;
        const int32_t height = (int32_t)((pixels + TIDE_LIST_DATA_ACROSS - 1u) / TIDE_LIST_DATA_ACROSS);
        const uint64_t version = tide_hash(m.view.data, m.view.size), kind = UINT64_C(0x534b494e494e464c);
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
