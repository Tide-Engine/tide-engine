// FBX belongs to the build tools. Converting through self-contained glTF keeps
// the existing canonical mesh/pose representation and the runtime boundary.
#include "fbx_files.h"

#include "tide/internal/count_text.h"
#include "ufbx.h"

#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma STDC FP_CONTRACT OFF

typedef struct fbx_builder fbx_builder;
typedef struct fbx_buffer {
    uint8_t *data;
    size_t size, room;
    fbx_builder *owner;
} fbx_buffer;

struct fbx_builder {
    char *why, *note;
    bool failed, skinned;
    // [Parts]: which mesh nodes are kept, by name (NULL: all), and how many were
    tide_skinned_keep_part_fn keep_part;
    void *keep_user;
    uint32_t parts_kept;
    // Each node's place among those written: UINT32_MAX for one left out with
    // the parts (fbx_drop_parts); with no parts chosen, its own typed_id
    uint32_t *node_index;
    ufbx_scene *scene;
    fbx_buffer bin, views, accessors, meshes, skins, animations;
    uint32_t num_views, num_accessors, num_meshes, num_skins;
    uint32_t vertices, indices;
    uint32_t *node_mesh, *node_skin;
    // Work a file can ask for without output to show for it, counted over
    // every node instancing a mesh: corners read, skin vertices checked,
    // triangulation steps (fbx_files.h) and keys baked from every take.
    uint64_t corners, skin_vertices, triangulation, baked_keys;
};

static bool fbx_fail(fbx_builder *b, const char *note, const char *fmt, ...)
{
    if (!b->failed) {
        b->failed = true;
        va_list args;
        va_start(args, fmt);
        vsnprintf(b->why, 256, fmt, args);
        va_end(args);
        snprintf(b->note, 256, "%s", note ? note : "");
    }
    return false;
}

// A limit of fbx_files.h refused the file. The message says what it has
// against the limit and ends with the limit's name, so a report says which:
// "has 20,000,000 vertices, more than the 16,777,216 one model holds
// (TIDE_FBX_MAX_VERTICES)". `have` is UINT64_MAX where the count stopped at
// the limit.
#define FBX_PAST_LIMIT(b, note, LIMIT, what, have) fbx_past_limit(b, note, #LIMIT, (uint64_t)(LIMIT), what, (uint64_t)(have))
static bool fbx_past_limit(fbx_builder *b, const char *note, const char *name, const uint64_t most, const char *what, const uint64_t have)
{
    char counted[32], limit[32];
    if (have == UINT64_MAX) return fbx_fail(b, note, "has more %s than the %s one model holds (%s)", what, tide_count_text(most, limit), name);
    return fbx_fail(b, note, "has %s %s, more than the %s one model holds (%s)", tide_count_text(have, counted), what, tide_count_text(most, limit), name);
}

static bool fbx_reserve(fbx_buffer *s, size_t more)
{
    if (s->owner->failed) return false;
    if (more > TIDE_FBX_MAX_BYTES - s->size) return FBX_PAST_LIMIT(s->owner, "split the model into smaller files", TIDE_FBX_MAX_BYTES, "bytes of FBX conversion data", (uint64_t)s->size + more);
    size_t need = s->size + more;
    if (need <= s->room) return true;
    size_t room = s->room ? s->room : 256u;
    while (room < need) room = room > TIDE_FBX_MAX_BYTES / 2u ? TIDE_FBX_MAX_BYTES : room * 2u;
    void *p = realloc(s->data, room);
    if (!p) return fbx_fail(s->owner, NULL, "can't allocate memory while importing FBX");
    s->data = p;
    s->room = room;
    return true;
}

static bool fbx_bytes(fbx_buffer *s, const void *data, size_t size)
{
    if (!fbx_reserve(s, size)) return false;
    if (size) memcpy(s->data + s->size, data, size);
    s->size += size;
    return true;
}

static bool fbx_text(fbx_buffer *s, const char *fmt, ...)
{
    char text[512];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(text, sizeof text, fmt, args);
    va_end(args);
    if (n < 0 || (size_t)n >= sizeof text) return fbx_fail(s->owner, NULL, "can't format converted FBX data");
    return fbx_bytes(s, text, (size_t)n);
}

static bool fbx_string(fbx_buffer *s, ufbx_string str)
{
    if (!fbx_text(s, "\"")) return false;
    for (size_t i = 0; i < str.length; i++) {
        unsigned char c = (unsigned char)str.data[i];
        if (c == '"' || c == '\\') { if (!fbx_text(s, "\\%c", c)) return false; }
        else if (c < 32u) { if (!fbx_text(s, "\\u%04x", c)) return false; }
        else if (!fbx_bytes(s, &c, 1u)) return false;
    }
    return fbx_text(s, "\"");
}

static bool fbx_float(fbx_builder *b, ufbx_real x, float *out)
{
    if (!(x >= -FLT_MAX && x <= FLT_MAX)) return fbx_fail(b, NULL, "has non-finite or excessively large FBX values");
    *out = (float)x + 0.0f;
    return true;
}

static bool fbx_json_float(fbx_buffer *s, ufbx_real x)
{
    float value;
    return fbx_float(s->owner, x, &value) && fbx_text(s, "%.9g", (double)value);
}

static bool fbx_word(fbx_buffer *s, uint32_t word)
{
    uint8_t bytes[4] = {(uint8_t)word, (uint8_t)(word >> 8u), (uint8_t)(word >> 16u), (uint8_t)(word >> 24u)};
    return fbx_bytes(s, bytes, 4u);
}

static bool fbx_number(fbx_buffer *s, ufbx_real x)
{
    float value;
    uint32_t word;
    if (!fbx_float(s->owner, x, &value)) return false;
    memcpy(&word, &value, sizeof word);
    return fbx_word(s, word);
}

static bool fbx_align(fbx_buffer *s)
{
    static const uint8_t zeros[4] = {0};
    return fbx_bytes(s, zeros, (4u - s->size % 4u) % 4u);
}

// A view of the bytes written since `begin`, which then end on four bytes.
static uint32_t fbx_view(fbx_builder *b, size_t begin)
{
    uint32_t view = b->num_views++;
    fbx_text(&b->views, "%s{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu}", view ? "," : "", begin, b->bin.size - begin);
    fbx_align(&b->bin);
    return view;
}

static uint32_t fbx_accessor(fbx_builder *b, size_t begin, uint32_t count, const char *type, uint32_t component)
{
    uint32_t view = fbx_view(b, begin), accessor = b->num_accessors++;
    fbx_text(&b->accessors, "%s{\"bufferView\":%u,\"componentType\":%u,\"count\":%u,\"type\":\"%s\"}",
             accessor ? "," : "", view, component, count, type);
    return accessor;
}

static bool fbx_transform_json(fbx_buffer *s, const ufbx_transform *t)
{
    const ufbx_real values[] = {t->translation.x,t->translation.y,t->translation.z,
        t->rotation.x,t->rotation.y,t->rotation.z,t->rotation.w,t->scale.x,t->scale.y,t->scale.z};
    static const char *prefix[] = {",\"translation\":[",",",",", "],\"rotation\":[",",",",",",", "],\"scale\":[",",",","};
    for (size_t i = 0; i < 10u; i++) if (!fbx_text(s, "%s", prefix[i]) || !fbx_json_float(s, values[i])) return false;
    return fbx_text(s, "]");
}

static bool fbx_matrix(fbx_buffer *s, const ufbx_matrix *m)
{
    for (uint32_t c = 0; c < 4u; c++) {
        if (!fbx_number(s, m->cols[c].x) || !fbx_number(s, m->cols[c].y) || !fbx_number(s, m->cols[c].z)
            || !fbx_number(s, c == 3u ? 1.0 : 0.0)) return false;
    }
    return true;
}

static bool fbx_material(fbx_buffer *s, const ufbx_material *m)
{
    ufbx_vec3 color = {.x=1.0, .y=1.0, .z=1.0};
    ufbx_real factor = 1.0, alpha = 1.0;
    if (m) {
        if (m->pbr.base_color.has_value) {
            color = m->pbr.base_color.value_vec3;
            factor = m->pbr.base_factor.value_real;
        } else if (m->fbx.diffuse_color.has_value) {
            color = m->fbx.diffuse_color.value_vec3;
            factor = m->fbx.diffuse_factor.value_real;
        }
        if (m->pbr.opacity.has_value) alpha = m->pbr.opacity.value_real;
        else if (m->fbx.transparency_factor.has_value) alpha = 1.0 - m->fbx.transparency_factor.value_real;
    }
    const ufbx_real values[] = {color.x * factor, color.y * factor, color.z * factor, alpha};
    if (!fbx_text(s, "{\"pbrMetallicRoughness\":{\"baseColorFactor\":[")) return false;
    for (size_t i = 0; i < 4u; i++) {
        ufbx_real v = values[i] < 0.0 ? 0.0 : values[i] > 1.0 ? 1.0 : values[i];
        if ((i && !fbx_text(s, ",")) || !fbx_json_float(s, v)) return false;
    }
    return fbx_text(s, "]}}");
}

static bool fbx_attribute(fbx_builder *b, fbx_buffer *attrs, const ufbx_mesh *mesh, const uint32_t *corners,
                           uint32_t count, uint32_t kind)
{
    static const char *names[] = {"POSITION","NORMAL","TEXCOORD_0","TEXCOORD_1","COLOR_0","TANGENT"};
    static const char *types[] = {"VEC3","VEC3","VEC2","VEC2","VEC4","VEC4"};
    const size_t begin = b->bin.size;
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t corner = corners[i];
        if (kind == 0u || kind == 1u) {
            ufbx_vec3 v = ufbx_get_vertex_vec3(kind ? &mesh->vertex_normal : &mesh->vertex_position, corner);
            if (!fbx_number(&b->bin, v.x) || !fbx_number(&b->bin, v.y) || !fbx_number(&b->bin, v.z)) return false;
        } else if (kind == 2u || kind == 3u) {
            const ufbx_vertex_vec2 *uv = kind == 2u ? &mesh->vertex_uv : &mesh->uv_sets.data[1].vertex_uv;
            ufbx_vec2 v = ufbx_get_vertex_vec2(uv, corner);
            // FBX texture coordinates start at the bottom; glTF's start at the top.
            if (!fbx_number(&b->bin, v.x) || !fbx_number(&b->bin, 1.0 - v.y)) return false;
        } else if (kind == 4u) {
            ufbx_vec4 v = ufbx_get_vertex_vec4(&mesh->vertex_color, corner);
            if (!fbx_number(&b->bin, v.x) || !fbx_number(&b->bin, v.y) || !fbx_number(&b->bin, v.z) || !fbx_number(&b->bin, v.w)) return false;
        } else {
            ufbx_vec3 t = ufbx_get_vertex_vec3(&mesh->vertex_tangent, corner);
            ufbx_real sign = 1.0;
            if (mesh->vertex_bitangent.exists && mesh->vertex_normal.exists) {
                ufbx_vec3 n = ufbx_get_vertex_vec3(&mesh->vertex_normal, corner);
                ufbx_vec3 v = ufbx_get_vertex_vec3(&mesh->vertex_bitangent, corner);
                ufbx_real dot = (n.y*t.z-n.z*t.y)*v.x + (n.z*t.x-n.x*t.z)*v.y + (n.x*t.y-n.y*t.x)*v.z;
                if (dot < 0.0) sign = -1.0;
            }
            if (!fbx_number(&b->bin, t.x) || !fbx_number(&b->bin, t.y) || !fbx_number(&b->bin, t.z) || !fbx_number(&b->bin, -sign)) return false;
        }
    }
    uint32_t accessor = fbx_accessor(b, begin, count, types[kind], 5126u);
    return fbx_text(attrs, "%s\"%s\":%u", attrs->size ? "," : "", names[kind], accessor);
}

static bool fbx_weights(fbx_builder *b, fbx_buffer *attrs, const ufbx_mesh *mesh, const ufbx_skin_deformer *skin,
                        const uint32_t *corners, uint32_t count, bool fallback)
{
    uint32_t most = 1u;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t vertex = mesh->vertex_indices.data[corners[i]];
        if (vertex >= skin->vertices.count) continue;
        const ufbx_skin_vertex *v = skin->vertices.data + vertex;
        // ufbx orders weights from largest to smallest. All-zero weights
        // therefore have the same rigid fallback as an empty weight range.
        if (v->num_weights && skin->weights.data[v->weight_begin].weight > 0.0 && v->num_weights > most) most = v->num_weights;
    }
    uint32_t sets = (most + 3u) / 4u;
    if ((uint64_t)count * sets * 24u > TIDE_FBX_MAX_BYTES) return FBX_PAST_LIMIT(b, "split the model into smaller files", TIDE_FBX_MAX_BYTES, "bytes of vertex influences", (uint64_t)count * sets * 24u);
    for (uint32_t set = 0; set < sets; set++) {
        size_t start_joints = b->bin.size;
        for (uint32_t i = 0; i < count; i++) {
            uint32_t vertex = mesh->vertex_indices.data[corners[i]];
            const ufbx_skin_vertex *v = vertex < skin->vertices.count ? skin->vertices.data + vertex : NULL;
            bool has = v && v->num_weights && skin->weights.data[v->weight_begin].weight > 0.0;
            for (uint32_t k = 0; k < 4u; k++) {
                uint32_t weight = set * 4u + k, joint = 0u;
                if (has && weight < v->num_weights) joint = skin->weights.data[(size_t)v->weight_begin + weight].cluster_index;
                else if (!has && fallback && !weight) joint = (uint32_t)skin->clusters.count;
                if (joint >= skin->clusters.count + (fallback ? 1u : 0u)) return fbx_fail(b, NULL, "has an FBX weight referring to a missing bone");
                uint8_t bytes[2] = {(uint8_t)joint, (uint8_t)(joint >> 8u)};
                if (!fbx_bytes(&b->bin, bytes, 2u)) return false;
            }
        }
        uint32_t joints = fbx_accessor(b, start_joints, count, "VEC4", 5123u);
        size_t start_weights = b->bin.size;
        for (uint32_t i = 0; i < count; i++) {
            uint32_t vertex = mesh->vertex_indices.data[corners[i]];
            const ufbx_skin_vertex *v = vertex < skin->vertices.count ? skin->vertices.data + vertex : NULL;
            bool has = v && v->num_weights && skin->weights.data[v->weight_begin].weight > 0.0;
            for (uint32_t k = 0; k < 4u; k++) {
                uint32_t weight = set * 4u + k;
                ufbx_real value = 0.0;
                if (has && weight < v->num_weights) value = skin->weights.data[(size_t)v->weight_begin + weight].weight;
                else if (!has && fallback && !weight) value = 1.0;
                if (value < 0.0) return fbx_fail(b, NULL, "has negative FBX skin weights");
                if (!fbx_number(&b->bin, value)) return false;
            }
        }
        uint32_t weights = fbx_accessor(b, start_weights, count, "VEC4", 5126u);
        if (!fbx_text(attrs, ",\"JOINTS_%u\":%u,\"WEIGHTS_%u\":%u", set, joints, set, weights)) return false;
    }
    return !b->failed;
}

// Where a node is among the nodes written (fbx_drop_parts), or UINT32_MAX for one left out.
static uint32_t fbx_node(const fbx_builder *b, const ufbx_node *node)
{
    return b->node_index ? b->node_index[node->typed_id] : node->typed_id;
}

// Whether a node holding a mesh is a part [Parts] keeps: by its name, or a geometry helper's by its parent's.
static bool fbx_part_kept(const fbx_builder *b, const ufbx_node *node)
{
    const ufbx_node *named = node->is_geometry_transform_helper && node->parent ? node->parent : node;
    return b->keep_part(b->keep_user, named->name.data, (uint32_t)named->name.length);
}

// With parts chosen, the nodes left out with the parts, as the decoder's
// drop_parts: a node holding a part left out, or a group whose children all
// are, so long as no part kept, no bone, no joint of a kept part's skin and no
// child kept is it or under it. Bones, sockets and empties holding no part
// stay. A node left out has every child left out.
static bool fbx_drop_below(fbx_builder *b, const ufbx_node *node, const uint8_t *needed, const uint8_t *parts, uint8_t *dropped,
                           const uint32_t depth)
{
    bool all = depth <= 256u;
    for (size_t c = 0; c < node->children.count; c++)
        all = fbx_drop_below(b, node->children.data[c], needed, parts, dropped, depth + 1u) && all;
    const uint32_t i = node->typed_id;
    dropped[i] = !needed[i] && all && (parts[i] || node->children.count);
    return dropped[i];
}

static bool fbx_drop_parts(fbx_builder *b)
{
    const size_t n = b->scene->nodes.count;
    uint8_t *needed = calloc(n ? n : 1u, 1u), *parts = calloc(n ? n : 1u, 1u), *dropped = calloc(n ? n : 1u, 1u);
    b->node_index = malloc((n ? n : 1u) * sizeof *b->node_index);
    bool ok = needed && parts && dropped && b->node_index;
    for (size_t i = 0; ok && i < n; i++) {
        const ufbx_node *node = b->scene->nodes.data[i];
        if (node->bone) needed[i] = 1u;
        if (!node->mesh || !node->mesh->num_triangles) continue;
        if (!fbx_part_kept(b, node)) { parts[i] = 1u; continue; }
        needed[i] = 1u;
        for (size_t s = 0; s < node->mesh->skin_deformers.count; s++) {
            const ufbx_skin_deformer *skin = node->mesh->skin_deformers.data[s];
            for (size_t c = 0; c < skin->clusters.count; c++) {
                const ufbx_node *bone = skin->clusters.data[c]->bone_node;
                if (bone && bone->typed_id < n) needed[bone->typed_id] = 1u;
            }
        }
    }
    if (ok) fbx_drop_below(b, b->scene->root_node, needed, parts, dropped, 0u);
    uint32_t written = 0;
    for (size_t i = 0; ok && i < n; i++) b->node_index[i] = dropped[i] ? UINT32_MAX : written++;
    free(needed), free(parts), free(dropped);
    return ok || fbx_fail(b, NULL, "can't allocate FBX node data");
}

static bool fbx_skin(fbx_builder *b, const ufbx_node *node, const ufbx_skin_deformer *skin, bool fallback)
{
    if (skin->clusters.count + (fallback ? 1u : 0u) > 65536u || !skin->clusters.count) return fbx_fail(b, "split the skeleton into smaller models", "has no FBX skin clusters, or more than 65536 clusters");
    const uint32_t index = b->num_skins++;
    b->node_skin[node->typed_id] = index;
    size_t begin = b->bin.size;
    for (size_t i = 0; i < skin->clusters.count; i++) {
        const ufbx_skin_cluster *cluster = skin->clusters.data[i];
        if (!cluster->bone_node) return fbx_fail(b, NULL, "has an FBX skin cluster without a bone");
        if (!fbx_matrix(&b->bin, &cluster->geometry_to_bone)) return false;
    }
    if (fallback && !fbx_matrix(&b->bin, &ufbx_identity_matrix)) return false;
    uint32_t accessor = fbx_accessor(b, begin, (uint32_t)skin->clusters.count + (fallback ? 1u : 0u), "MAT4", 5126u);
    if (!fbx_text(&b->skins, "%s{\"inverseBindMatrices\":%u,\"joints\":[", index ? "," : "", accessor)) return false;
    for (size_t i = 0; i < skin->clusters.count; i++) {
        if (!fbx_text(&b->skins, "%s%u", i ? "," : "", fbx_node(b, skin->clusters.data[i]->bone_node))) return false;
    }
    if (fallback && !fbx_text(&b->skins, ",%u", fbx_node(b, node))) return false;
    return fbx_text(&b->skins, "]}");
}

// A blend channel's one shape at full weight. A channel of in-between shapes
// (several keyframes, or one at another weight) is refused: Tide's shapes are
// linear, one delta each.
static bool fbx_channel_shape(fbx_builder *b, const ufbx_blend_channel *channel, const ufbx_blend_shape **out)
{
    *out = NULL;
    if (channel->keyframes.count != 1u || !channel->keyframes.data[0].shape || channel->keyframes.data[0].target_weight != 1.0) {
        return fbx_fail(b, "export each blend shape as one target at full weight, without in-between shapes",
                        "has FBX blend shape '%.60s' with in-between shapes, which Tide doesn't read", channel->name.data);
    }
    if (!(channel->weight >= -FLT_MAX && channel->weight <= FLT_MAX)) return fbx_fail(b, NULL, "has a non-finite FBX blend shape weight");
    *out = channel->keyframes.data[0].shape;
    return true;
}

static ufbx_string fbx_channel_name(const ufbx_blend_channel *channel)
{
    const ufbx_blend_shape *shape = channel->keyframes.data[0].shape;
    return shape->name.length ? shape->name : channel->name;
}

// Whether a mesh's channel k of deformer d repeats an earlier channel of the
// mesh exactly: its name, its offsets, and a default weight of zero, so
// leaving it out changes neither the rest geometry nor what the name moves.
// The Tales Factory's Medieval Civilians & Soldiers has one such (its
// DaggerSheath's MK_Armor02); any other repeated name is still refused.
static bool fbx_channel_repeats(const ufbx_mesh *mesh, const size_t d, const size_t k)
{
    const ufbx_blend_channel *channel = mesh->blend_deformers.data[d]->channels.data[k];
    if (channel->weight != 0.0) return false;
    const ufbx_string name = fbx_channel_name(channel);
    const ufbx_blend_shape *shape = channel->keyframes.data[0].shape;
    for (size_t e = 0; e <= d; e++) {
        const ufbx_blend_deformer *blend = mesh->blend_deformers.data[e];
        for (size_t j = 0; j < (e == d ? k : blend->channels.count); j++) {
            const ufbx_blend_channel *other = blend->channels.data[j];
            const ufbx_string other_name = fbx_channel_name(other);
            const ufbx_blend_shape *was = other->keyframes.data[0].shape;
            const size_t n = shape->num_offsets;
            if (other->weight != 0.0 || other_name.length != name.length || memcmp(other_name.data, name.data, name.length)
                || was->num_offsets != n || was->offset_vertices.count != shape->offset_vertices.count
                || was->position_offsets.count != shape->position_offsets.count || was->normal_offsets.count != shape->normal_offsets.count) continue;
            if ((!n || (!memcmp(was->offset_vertices.data, shape->offset_vertices.data, shape->offset_vertices.count * sizeof *shape->offset_vertices.data)
                        && !memcmp(was->position_offsets.data, shape->position_offsets.data, shape->position_offsets.count * sizeof *shape->position_offsets.data)
                        && (!shape->normal_offsets.count
                            || !memcmp(was->normal_offsets.data, shape->normal_offsets.data, shape->normal_offsets.count * sizeof *shape->normal_offsets.data)))))
                return true;
        }
    }
    return false;
}

// One morph target accessor of a part: a channel's position or normal offsets
// at the part's vertices, as a sparse accessor over zeros (none where the
// shape moves nothing there).
static bool fbx_target_accessor(fbx_builder *b, const ufbx_mesh *mesh, const ufbx_blend_shape *shape, const uint32_t *corners,
                                uint32_t count, bool normals, uint32_t *accessor)
{
    fbx_buffer indices = {.owner=b}, values = {.owner=b};
    uint32_t moved = 0u;
    bool ok = true;
    for (uint32_t i = 0; ok && i < count; i++) {
        const uint32_t vertex = mesh->vertex_indices.data[corners[i]];
        const uint32_t at = ufbx_get_blend_shape_offset_index(shape, vertex);
        if (at == UFBX_NO_INDEX) continue;
        const ufbx_vec3_list *offsets = normals ? &shape->normal_offsets : &shape->position_offsets;
        if (at >= offsets->count) continue;
        const ufbx_vec3 v = offsets->data[at];
        if (v.x == 0.0 && v.y == 0.0 && v.z == 0.0) continue;
        ok = fbx_word(&indices, i) && fbx_number(&values, v.x) && fbx_number(&values, v.y) && fbx_number(&values, v.z);
        moved++;
    }
    *accessor = b->num_accessors++;
    if (ok && !moved) {
        ok = fbx_text(&b->accessors, "%s{\"componentType\":5126,\"count\":%u,\"type\":\"VEC3\"}", *accessor ? "," : "", count);
    } else if (ok) {
        size_t begin = b->bin.size;
        ok = fbx_bytes(&b->bin, indices.data, indices.size);
        const uint32_t index_view = fbx_view(b, begin);
        begin = b->bin.size;
        ok = ok && fbx_bytes(&b->bin, values.data, values.size);
        const uint32_t value_view = fbx_view(b, begin);
        ok = ok && fbx_text(&b->accessors, "%s{\"componentType\":5126,\"count\":%u,\"type\":\"VEC3\",\"sparse\":{\"count\":%u,"
                            "\"indices\":{\"bufferView\":%u,\"componentType\":5125},\"values\":{\"bufferView\":%u}}}",
                            *accessor ? "," : "", count, moved, index_view, value_view);
    }
    free(indices.data);
    free(values.data);
    return ok && !b->failed;
}

static bool fbx_skin_vertex_has_weights(fbx_builder *b, const ufbx_skin_deformer *skin, size_t index, bool *has)
{
    *has = false;
    if (index >= skin->vertices.count) return true;
    const ufbx_skin_vertex *v = skin->vertices.data + index;
    if (skin->skinning_method == UFBX_SKINNING_METHOD_BLENDED_DQ_LINEAR && v->dq_weight != 0.0) return fbx_fail(b, "export using linear blend skinning", "blends FBX dual-quaternion skinning into some vertices");
    if ((uint64_t)v->weight_begin + v->num_weights > skin->weights.count) return fbx_fail(b, NULL, "has an FBX skin vertex outside its weight data");
    uint32_t positive = 0u;
    for (uint32_t i = 0; i < v->num_weights; i++) {
        ufbx_real weight = skin->weights.data[(size_t)v->weight_begin + i].weight;
        if (!(weight >= 0.0 && weight <= FLT_MAX)) return fbx_fail(b, NULL, "has negative or non-finite FBX skin weights");
        if (weight > 0.0) positive++;
    }
    if (skin->skinning_method == UFBX_SKINNING_METHOD_RIGID && positive > 1u) return fbx_fail(b, "export using linear blend skinning", "has several influences on an FBX rigid-skin vertex");
    *has = positive != 0u;
    return true;
}

static bool fbx_mesh(fbx_builder *b, const ufbx_node *node)
{
    const ufbx_mesh *mesh = node->mesh;
    if (!mesh || !mesh->num_triangles) return true;
    if (b->keep_part) { // A part is known by its node's name: a geometry helper's is its parent's
        if (!fbx_part_kept(b, node)) return true;
        b->parts_kept++;
    }
    const ufbx_skin_deformer *skin = NULL;
    bool fallback = false;
    if (b->skinned) {
        for (size_t i = 0; i < mesh->blend_deformers.count; i++) {
            const ufbx_blend_deformer *blend = mesh->blend_deformers.data[i];
            for (size_t k = 0; k < blend->channels.count; k++) {
                const ufbx_blend_shape *shape;
                if (!fbx_channel_shape(b, blend->channels.data[k], &shape)) return false;
            }
        }
        if (mesh->cache_deformers.count) return fbx_fail(b, "export bone animation instead of a vertex cache", "has FBX vertex-cache deformation, which Tide doesn't read");
        if (mesh->skin_deformers.count > 1u) return fbx_fail(b, "merge the skin deformers before exporting", "has several FBX skin deformers on one mesh");
        if (mesh->skin_deformers.count) {
            if (mesh->num_vertices > TIDE_FBX_MAX_VERTICES - b->skin_vertices)
                return FBX_PAST_LIMIT(b, "split the model into smaller files", TIDE_FBX_MAX_VERTICES, "skinned vertices (one each node that uses a mesh)",
                                      (uint64_t)b->skin_vertices + mesh->num_vertices);
            b->skin_vertices += mesh->num_vertices;
            skin = mesh->skin_deformers.data[0];
            // 3ds Max and Unreal write SkinningType "Blend" with a dual-quaternion
            // weight for each vertex; where every weight is zero that's linear
            // skinning (fbx_skin_vertex_has_weights refuses any other).
            if (skin->skinning_method != UFBX_SKINNING_METHOD_LINEAR && skin->skinning_method != UFBX_SKINNING_METHOD_RIGID
                && skin->skinning_method != UFBX_SKINNING_METHOD_BLENDED_DQ_LINEAR)
                return fbx_fail(b, "export using linear blend skinning", "uses FBX dual-quaternion skinning");
            for (size_t i = 0; i < mesh->num_vertices; i++) {
                bool has;
                if (!fbx_skin_vertex_has_weights(b, skin, i, &has)) return false;
                if (!has) fallback = true;
            }
            if (!fbx_skin(b, node, skin, fallback)) return false;
        }
    }
    if (mesh->num_indices > TIDE_FBX_MAX_VERTICES || mesh->num_indices > TIDE_FBX_MAX_VERTICES - b->corners)
        return FBX_PAST_LIMIT(b, "split the model into smaller files", TIDE_FBX_MAX_VERTICES, "polygon corners (one each node that uses a mesh)",
                              (uint64_t)b->corners + mesh->num_indices);
    if (mesh->num_triangles > TIDE_FBX_MAX_INDICES / 3u || mesh->max_face_triangles > TIDE_FBX_MAX_INDICES / 3u)
        return fbx_past_limit(b, "split the model into smaller files", "TIDE_FBX_MAX_INDICES", TIDE_FBX_MAX_INDICES / 3u, "triangles in a mesh",
                              mesh->num_triangles > mesh->max_face_triangles ? mesh->num_triangles : mesh->max_face_triangles);
    b->corners += mesh->num_indices;
    uint32_t *map = malloc(mesh->num_indices * sizeof *map);
    if (map) memset(map, 0xff, mesh->num_indices * sizeof *map);
    uint32_t *corners = malloc(mesh->num_indices * sizeof *corners);
    size_t tri_room = mesh->max_face_triangles * 3u;
    uint32_t *triangles = malloc((tri_room ? tri_room : 1u) * sizeof *triangles);
    if (!map || !corners || !triangles) { free(map); free(corners); free(triangles); return fbx_fail(b, NULL, "can't allocate FBX triangulation data"); }
    uint32_t id = b->num_meshes++;
    b->node_mesh[node->typed_id] = id;
    bool ok = fbx_text(&b->meshes, "%s{\"primitives\":[", id ? "," : "");
    uint32_t primitives = 0u;
    for (size_t p = 0; ok && p < mesh->material_parts.count; p++) {
        const ufbx_mesh_part *part = mesh->material_parts.data + p;
        if (!part->num_triangles) continue;
        fbx_buffer indices = {.owner=b}, attrs = {.owner=b};
        uint32_t count = 0u, num_indices = 0u;
        for (size_t fi = 0; ok && fi < part->face_indices.count; fi++) {
            ufbx_face face = mesh->faces.data[part->face_indices.data[fi]];
            if (face.num_indices < 3u) continue;
            const uint64_t steps = (uint64_t)face.num_indices * face.num_indices;
            if (steps > TIDE_FBX_MAX_TRIANGULATION - b->triangulation) {
                ok = FBX_PAST_LIMIT(b, "split its largest polygons, or the model into smaller files", TIDE_FBX_MAX_TRIANGULATION,
                                    "steps to cut its polygons into triangles (each polygon's corners squared)", b->triangulation + steps);
                break;
            }
            b->triangulation += steps;
            uint32_t n = ufbx_triangulate_face(triangles, tri_room, mesh, face);
            if (!n || (size_t)n * 3u > tri_room) { ok = fbx_fail(b, NULL, "has an FBX polygon that can't be triangulated"); break; }
            for (uint32_t k = 0; ok && k < n * 3u; k++) {
                uint32_t corner = triangles[k];
                if (corner >= mesh->num_indices) { ok = fbx_fail(b, NULL, "has an FBX triangle outside its geometry"); break; }
                if (map[corner] == UINT32_MAX) { map[corner] = count; corners[count++] = corner; }
                ok = fbx_word(&indices, map[corner]);
                num_indices++;
            }
        }
        if (ok && (uint64_t)b->vertices + count > TIDE_FBX_MAX_VERTICES) ok = FBX_PAST_LIMIT(b, "split the model into smaller files", TIDE_FBX_MAX_VERTICES, "vertices", (uint64_t)b->vertices + count);
        if (ok && (uint64_t)b->indices + num_indices > TIDE_FBX_MAX_INDICES) ok = FBX_PAST_LIMIT(b, "split the model into smaller files", TIDE_FBX_MAX_INDICES, "triangle corners", (uint64_t)b->indices + num_indices);
        if (ok) {
            b->vertices += count;
            b->indices += num_indices;
            ok = fbx_attribute(b, &attrs, mesh, corners, count, 0u);
            if (ok && mesh->vertex_normal.exists) ok = fbx_attribute(b, &attrs, mesh, corners, count, 1u);
            if (ok && mesh->vertex_uv.exists) ok = fbx_attribute(b, &attrs, mesh, corners, count, 2u);
            if (ok && mesh->uv_sets.count > 1u && mesh->uv_sets.data[1].vertex_uv.exists) ok = fbx_attribute(b, &attrs, mesh, corners, count, 3u);
            if (ok && mesh->vertex_color.exists) ok = fbx_attribute(b, &attrs, mesh, corners, count, 4u);
            if (ok && mesh->vertex_tangent.exists) ok = fbx_attribute(b, &attrs, mesh, corners, count, 5u);
            if (ok && skin) ok = fbx_weights(b, &attrs, mesh, skin, corners, count, fallback);
        }
        // Each blend channel is a morph target of every part, named after its
        // shape (Maya's channel is "blendShape1.smile", its shape "smile").
        fbx_buffer targets = {.owner=b};
        for (size_t d = 0; ok && b->skinned && d < mesh->blend_deformers.count; d++) {
            const ufbx_blend_deformer *blend = mesh->blend_deformers.data[d];
            for (size_t k = 0; ok && k < blend->channels.count; k++) {
                if (fbx_channel_repeats(mesh, d, k)) continue;
                const ufbx_blend_shape *shape;
                uint32_t position, normal;
                ok = fbx_channel_shape(b, blend->channels.data[k], &shape)
                  && fbx_target_accessor(b, mesh, shape, corners, count, false, &position)
                  && fbx_text(&targets, "%s{\"POSITION\":%u", targets.size ? "," : "", position);
                if (ok && mesh->vertex_normal.exists && shape->normal_offsets.count) {
                    ok = fbx_target_accessor(b, mesh, shape, corners, count, true, &normal)
                      && fbx_text(&targets, ",\"NORMAL\":%u", normal);
                }
                if (ok) ok = fbx_text(&targets, "}");
            }
        }
        if (ok) {
            size_t begin = b->bin.size;
            ok = fbx_bytes(&b->bin, indices.data, indices.size);
            uint32_t accessor = fbx_accessor(b, begin, num_indices, "SCALAR", 5125u);
            ok = ok && fbx_text(&b->meshes, "%s{\"attributes\":{", primitives++ ? "," : "")
                    && fbx_bytes(&b->meshes, attrs.data, attrs.size)
                    && fbx_text(&b->meshes, "},\"indices\":%u", accessor);
            if (ok && targets.size) ok = fbx_text(&b->meshes, ",\"targets\":[") && fbx_bytes(&b->meshes, targets.data, targets.size)
                                       && fbx_text(&b->meshes, "]");
            if (ok && part->index < node->materials.count) ok = fbx_text(&b->meshes, ",\"material\":%u", node->materials.data[part->index]->typed_id);
            if (ok) ok = fbx_text(&b->meshes, "}");
        }
        free(indices.data);
        free(attrs.data);
        free(targets.data);
        // The next part starts with no corner numbered; this part's alone are.
        for (uint32_t i = 0; i < count; i++) map[corners[i]] = UINT32_MAX;
    }
    if (ok) ok = fbx_text(&b->meshes, "]");
    // The channels' names and default weights, as glTF says them for a mesh
    size_t channels = 0u;
    for (size_t d = 0; b->skinned && d < mesh->blend_deformers.count; d++) {
        for (size_t k = 0; k < mesh->blend_deformers.data[d]->channels.count; k++) channels += !fbx_channel_repeats(mesh, d, k);
    }
    if (ok && channels) {
        fbx_buffer weights = {.owner=b}, names = {.owner=b};
        for (size_t d = 0; ok && d < mesh->blend_deformers.count; d++) {
            const ufbx_blend_deformer *blend = mesh->blend_deformers.data[d];
            for (size_t k = 0; ok && k < blend->channels.count; k++) {
                if (fbx_channel_repeats(mesh, d, k)) continue;
                const ufbx_blend_channel *channel = blend->channels.data[k];
                const bool first = !weights.size;
                ok = fbx_text(&weights, first ? "" : ",") && fbx_json_float(&weights, channel->weight)
                  && fbx_text(&names, first ? "" : ",") && fbx_string(&names, fbx_channel_name(channel));
            }
        }
        ok = ok && fbx_text(&b->meshes, ",\"weights\":[") && fbx_bytes(&b->meshes, weights.data, weights.size)
                && fbx_text(&b->meshes, "],\"extras\":{\"targetNames\":[") && fbx_bytes(&b->meshes, names.data, names.size)
                && fbx_text(&b->meshes, "]}");
        free(weights.data);
        free(names.data);
    }
    if (ok) ok = fbx_text(&b->meshes, "}");
    free(map);
    free(corners);
    free(triangles);
    return ok && !b->failed;
}

// A step's edge is placed at the nearest representable float before/after
// its key. There is no float sample between it and the key, so LINEAR keys
// retain the discontinuity without the default bake's millisecond ramp.
static bool fbx_key_time(fbx_builder *b, double time, ufbx_baked_key_flags flags, float *out)
{
    float value;
    if (!fbx_float(b, time, &value) || value < 0.0f) return fbx_fail(b, NULL, "has an FBX animation key outside the nonnegative time range");
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    if (flags & UFBX_BAKED_KEY_STEP_LEFT) {
        if (!bits) { *out = -1.0f; return true; }
        bits--;
    } else if (flags & UFBX_BAKED_KEY_STEP_RIGHT) {
        if (bits == 0x7f7fffffu) return fbx_fail(b, NULL, "has an FBX animation time too large for a step");
        bits++;
    }
    memcpy(out, &bits, sizeof bits);
    return true;
}

static bool fbx_channel(fbx_builder *b, fbx_buffer *samplers, fbx_buffer *channels, uint32_t *channel_count,
                        uint32_t node, uint32_t path, const ufbx_baked_vec3 *vec, const ufbx_baked_quat *quat, size_t count,
                        double begin_time, double duration)
{
    if (!count) return true;
    if (count > TIDE_FBX_MAX_BYTES / 20u) return fbx_past_limit(b, "shorten its takes' curves, or split the animation into several files", "TIDE_FBX_MAX_BYTES", TIDE_FBX_MAX_BYTES / 20u, "keys in one animation curve", count);
    fbx_buffer times = {.owner=b}, values = {.owner=b};
    bool ok = true;
    float previous = -1.0f;
    uint32_t keys = 0u;
    ufbx_quat previous_quat = {.w=1.0};
    // Restrict each take to its playback range, then move that range to zero.
    // Baked source keys can lie outside a stack, and FBX permits negative
    // timelines. Explicit boundary samples retain both cases and clip length.
    for (size_t i = 0; ok && i < count + 2u; i++) {
        bool boundary = i == 0u || i == count + 1u;
        if (i == count + 1u && duration == 0.0) break;
        double source_time = boundary ? begin_time + (i ? duration : 0.0) : path == 1u ? quat[i-1u].time : vec[i-1u].time;
        ufbx_baked_key_flags flags = boundary ? 0 : path == 1u ? quat[i-1u].flags : vec[i-1u].flags;
        if (!boundary && (source_time < begin_time || source_time > begin_time + duration)) continue;
        if (!boundary && source_time == begin_time && !(flags & UFBX_BAKED_KEY_STEP_RIGHT)) continue;
        if (!boundary && source_time == begin_time + duration && !(flags & UFBX_BAKED_KEY_STEP_LEFT)) continue;
        float time;
        ok = fbx_key_time(b, source_time - begin_time, flags, &time);
        if (!ok || time < 0.0f) continue;
        // Keys closer than a float can tell apart are one key, the later's
        // value (Mixamo's last key is 2e-8 s before its take's end, where
        // the boundary sample is): its time stays, its value is replaced.
        const bool merged = !(time > previous);
        if (merged) values.size -= (path == 1u ? 4u : 3u) * sizeof(float);
        else previous = time;
        if (!merged) ok = fbx_number(&times, time);
        if (path == 1u) {
            ufbx_baked_quat_list list = {.data=(ufbx_baked_quat *)quat, .count=count};
            ufbx_quat q = boundary ? ufbx_evaluate_baked_quat(list, source_time) : quat[i-1u].value;
            if (keys && q.x*previous_quat.x + q.y*previous_quat.y + q.z*previous_quat.z + q.w*previous_quat.w < 0.0) {
                q.x = -q.x; q.y = -q.y; q.z = -q.z; q.w = -q.w;
            }
            previous_quat = q;
            ok = ok && fbx_number(&values, q.x) && fbx_number(&values, q.y) && fbx_number(&values, q.z) && fbx_number(&values, q.w);
        } else {
            ufbx_baked_vec3_list list = {.data=(ufbx_baked_vec3 *)vec, .count=count};
            ufbx_vec3 v = boundary ? ufbx_evaluate_baked_vec3(list, source_time) : vec[i-1u].value;
            ok = ok && fbx_number(&values, v.x) && fbx_number(&values, v.y) && fbx_number(&values, v.z);
        }
        if (!merged) keys++;
    }
    if (ok && keys) {
        size_t begin = b->bin.size;
        ok = fbx_bytes(&b->bin, times.data, times.size);
        uint32_t input = fbx_accessor(b, begin, keys, "SCALAR", 5126u);
        begin = b->bin.size;
        ok = ok && fbx_bytes(&b->bin, values.data, values.size);
        uint32_t output = fbx_accessor(b, begin, keys, path == 1u ? "VEC4" : "VEC3", 5126u);
        uint32_t id = (*channel_count)++;
        static const char *paths[] = {"translation","rotation","scale"};
        ok = ok && fbx_text(samplers, "%s{\"input\":%u,\"output\":%u,\"interpolation\":\"LINEAR\"}", id ? "," : "", input, output)
                && fbx_text(channels, "%s{\"sampler\":%u,\"target\":{\"node\":%u,\"path\":\"%s\"}}", id ? "," : "", id, node, paths[path]);
    }
    free(times.data);
    free(values.data);
    return ok && !b->failed;
}

// FBX rotation curves are Euler angles, and exporters write them wrapped:
// one key at -182.72 degrees and the next at 178.20 is a turn of one degree,
// and (179.99, 180.30, -183.12) then (0.00, 0.10, -2.96) is the same rotation
// written as its other triple (x + 180, 180 - y, z + 180, true of every
// Tait-Bryan order). Evaluating the curves between such keys (the 60 Hz bake
// does) sweeps the long way, up to a full turn in a frame. So, as Maya's and
// Blender's Euler filters do, each key is moved by whole turns to within half
// a turn of the key before it, and where all three curves have keys at the
// same times, takes whichever of its two triples is nearer the key before.
// The rotation at every key is the same; what's between them takes the short way.
static ufbx_real fbx_near_turn(const ufbx_real value, const ufbx_real previous)
{
    const ufbx_real step = value - previous;
    if (!isfinite(step) || (step <= 180.0 && step >= -180.0)) return value;
    return value - floor((step + 180.0) / 360.0) * 360.0;
}

static void fbx_unwrap_rotations(const ufbx_scene *scene)
{
    for (size_t l = 0; l < scene->anim_layers.count; l++) {
        const ufbx_anim_layer *layer = scene->anim_layers.data[l];
        for (size_t p = 0; p < layer->anim_props.count; p++) {
            const ufbx_anim_prop *prop = layer->anim_props.data + p;
            if (strcmp(prop->prop_name.data, UFBX_Lcl_Rotation) != 0 || !prop->anim_value) continue;
            ufbx_anim_curve *const *curves = prop->anim_value->curves;
            // The three axes keyed together: a key's triple can be swapped for its twin.
            bool together = curves[0] && curves[1] && curves[2] && curves[0]->keyframes.count == curves[1]->keyframes.count
                         && curves[0]->keyframes.count == curves[2]->keyframes.count;
            for (size_t k = 0; together && k < curves[0]->keyframes.count; k++) {
                together = curves[0]->keyframes.data[k].time == curves[1]->keyframes.data[k].time
                        && curves[0]->keyframes.data[k].time == curves[2]->keyframes.data[k].time;
            }
            if (together) {
                ufbx_keyframe *x = curves[0]->keyframes.data, *y = curves[1]->keyframes.data, *z = curves[2]->keyframes.data;
                for (size_t k = 1; k < curves[0]->keyframes.count; k++) {
                    const ufbx_real same[3] = {fbx_near_turn(x[k].value, x[k - 1u].value), fbx_near_turn(y[k].value, y[k - 1u].value),
                                               fbx_near_turn(z[k].value, z[k - 1u].value)};
                    const ufbx_real twin[3] = {fbx_near_turn(x[k].value + 180.0, x[k - 1u].value),
                                               fbx_near_turn(180.0 - y[k].value, y[k - 1u].value),
                                               fbx_near_turn(z[k].value + 180.0, z[k - 1u].value)};
                    const ufbx_real away_same = fabs(same[0] - x[k - 1u].value) + fabs(same[1] - y[k - 1u].value) + fabs(same[2] - z[k - 1u].value);
                    const ufbx_real away_twin = fabs(twin[0] - x[k - 1u].value) + fabs(twin[1] - y[k - 1u].value) + fabs(twin[2] - z[k - 1u].value);
                    const bool flip = away_twin < away_same;
                    const ufbx_real *chosen = flip ? twin : same;
                    x[k].value = chosen[0], y[k].value = chosen[1], z[k].value = chosen[2];
                    if (flip) { // 180 - y runs the other way: its slopes turn round
                        y[k].left.dy = -y[k].left.dy;
                        y[k].right.dy = -y[k].right.dy;
                    }
                }
                continue;
            }
            for (int axis = 0; axis < 3; axis++) {
                const ufbx_anim_curve *curve = curves[axis];
                if (!curve) continue;
                ufbx_keyframe *keys = curve->keyframes.data;
                for (size_t k = 1; k < curve->keyframes.count; k++) keys[k].value = fbx_near_turn(keys[k].value, keys[k - 1u].value);
            }
        }
    }
}

static bool fbx_animations(fbx_builder *b)
{
    if (!b->skinned) return true;
    fbx_unwrap_rotations(b->scene);
    for (size_t a = 0; a < b->scene->anim_stacks.count; a++) {
        const ufbx_anim_stack *stack = b->scene->anim_stacks.data[a];
        ufbx_bake_opts opts = {0};
        // Keep source timestamps during baking: helper-node scale channels
        // are evaluated while other nodes bake. Shift only the finished keys,
        // so every helper is sampled in the original take's time domain.
        opts.resample_rate = 60.0;
        opts.minimum_sample_rate = 60.0;
        opts.max_keyframe_segments = TIDE_FBX_MAX_BYTES / sizeof(ufbx_baked_quat);
        opts.step_handling = UFBX_BAKE_STEP_HANDLING_IDENTICAL_TIME;
        // Baking stops at its memory, so a take can't bake much past the keys
        // left (TIDE_FBX_MAX_BAKED_KEYS): room for each as ufbx's largest
        // key, and 16 MiB for node tables. The first take has all of
        // TIDE_FBX_MAX_BYTES.
        const uint64_t left = TIDE_FBX_MAX_BAKED_KEYS - b->baked_keys;
        const uint64_t memory = left * sizeof(ufbx_baked_quat) + (16u << 20);
        opts.temp_allocator.memory_limit = opts.result_allocator.memory_limit
            = memory < TIDE_FBX_MAX_BYTES ? (size_t)memory : TIDE_FBX_MAX_BYTES;
        ufbx_error error;
        ufbx_baked_anim *anim = ufbx_bake_anim(b->scene, stack->anim, &opts, &error);
        if (!anim && error.type == UFBX_ERROR_MEMORY_LIMIT && memory < TIDE_FBX_MAX_BYTES)
            return FBX_PAST_LIMIT(b, "shorten its takes' curves, or split the animation into several files", TIDE_FBX_MAX_BAKED_KEYS, "baked animation keys across its takes", UINT64_MAX);
        if (!anim) return fbx_fail(b, NULL, "has FBX animation that can't be baked (%.160s)", error.description.data);
        uint64_t keys = 0;
        for (size_t n = 0; n < anim->nodes.count; n++) {
            const ufbx_baked_node *node = anim->nodes.data + n;
            keys += (uint64_t)node->translation_keys.count + node->rotation_keys.count + node->scale_keys.count;
        }
        if (keys > left) {
            ufbx_free_baked_anim(anim);
            return FBX_PAST_LIMIT(b, "shorten its takes' curves, or split the animation into several files", TIDE_FBX_MAX_BAKED_KEYS, "baked animation keys across its takes", b->baked_keys + keys);
        }
        b->baked_keys += keys;
        fbx_buffer samplers = {.owner=b}, channels = {.owner=b};
        bool ok = true;
        uint32_t count = 0u;
        bool root_translation = false;
        double duration = anim->playback_duration;
        double begin_time = stack->anim->time_begin;
        if (!(duration >= 0.0 && duration <= FLT_MAX) || !isfinite(begin_time)) ok = fbx_fail(b, NULL, "has an invalid FBX animation playback range");
        for (size_t n = 0; ok && n < anim->nodes.count; n++) {
            const ufbx_baked_node *node = anim->nodes.data + n;
            if (node->typed_id >= b->scene->nodes.count) { ok = fbx_fail(b, NULL, "has an FBX animation targeting a missing node"); break; }
            if (node->typed_id == b->scene->root_node->typed_id && node->translation_keys.count) root_translation = true;
            const uint32_t at = fbx_node(b, b->scene->nodes.data[node->typed_id]);
            if (at == UINT32_MAX) continue; // Left out with the parts
            ok = fbx_channel(b, &samplers, &channels, &count, at, 0u, node->translation_keys.data, NULL, node->translation_keys.count, begin_time, duration)
              && fbx_channel(b, &samplers, &channels, &count, at, 1u, NULL, node->rotation_keys.data, node->rotation_keys.count, begin_time, duration)
              && fbx_channel(b, &samplers, &channels, &count, at, 2u, node->scale_keys.data, NULL, node->scale_keys.count, begin_time, duration);
        }
        // glTF clips have no independent duration field. A constant root channel
        // preserves an empty take and the stack's playback end when needed.
        if (ok && !root_translation) {
            ufbx_baked_vec3 keys[2] = {{0}};
            keys[0].value = keys[1].value = b->scene->root_node->local_transform.translation;
            keys[1].time = duration;
            if (ok) ok = fbx_channel(b, &samplers, &channels, &count, fbx_node(b, b->scene->root_node), 0u, keys, NULL, duration > 0.0 ? 2u : 1u, 0.0, duration);
        }
        if (ok) ok = fbx_text(&b->animations, "%s{\"name\":", a ? "," : "") && fbx_string(&b->animations, stack->name)
                  && fbx_text(&b->animations, ",\"samplers\":[") && fbx_bytes(&b->animations, samplers.data, samplers.size)
                  && fbx_text(&b->animations, "],\"channels\":[") && fbx_bytes(&b->animations, channels.data, channels.size)
                  && fbx_text(&b->animations, "]}");
        free(samplers.data);
        free(channels.data);
        ufbx_free_baked_anim(anim);
        if (!ok) return false;
    }
    return true;
}

static bool fbx_json(fbx_builder *b, fbx_buffer *json)
{
    bool ok = fbx_text(json, "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[%u]}],\"nodes\":[", fbx_node(b, b->scene->root_node));
    bool first = true;
    for (size_t i = 0; ok && i < b->scene->nodes.count; i++) {
        const ufbx_node *node = b->scene->nodes.data[i];
        if (fbx_node(b, node) == UINT32_MAX) continue;
        ok = fbx_text(json, "%s{\"name\":", first ? "" : ",") && fbx_string(json, node->name) && fbx_transform_json(json, &node->local_transform);
        first = false;
        if (ok && b->node_mesh[i] != UINT32_MAX) ok = fbx_text(json, ",\"mesh\":%u", b->node_mesh[i]);
        if (ok && b->node_skin[i] != UINT32_MAX) ok = fbx_text(json, ",\"skin\":%u", b->node_skin[i]);
        if (ok && node->children.count) {
            ok = fbx_text(json, ",\"children\":[");
            bool none = true; // Children left out with the parts aren't named (they're all left out, or none)
            for (size_t c = 0; ok && c < node->children.count; c++) {
                const uint32_t child = fbx_node(b, node->children.data[c]);
                if (child == UINT32_MAX) continue;
                ok = fbx_text(json, "%s%u", none ? "" : ",", child);
                none = false;
            }
            if (ok) ok = fbx_text(json, "]");
        }
        if (ok) ok = fbx_text(json, "}");
    }
    if (ok) ok = fbx_text(json, "],\"materials\":[");
    for (size_t i = 0; ok && i < b->scene->materials.count; i++) {
        ok = (!i || fbx_text(json, ",")) && fbx_material(json, b->scene->materials.data[i]);
    }
    return ok && fbx_text(json, "],\"meshes\":[") && fbx_bytes(json, b->meshes.data, b->meshes.size)
              && fbx_text(json, "],\"skins\":[") && fbx_bytes(json, b->skins.data, b->skins.size)
              && fbx_text(json, "],\"animations\":[") && fbx_bytes(json, b->animations.data, b->animations.size)
              && fbx_text(json, "],\"buffers\":[{\"byteLength\":%zu}],\"bufferViews\":[", b->bin.size)
              && fbx_bytes(json, b->views.data, b->views.size) && fbx_text(json, "],\"accessors\":[")
              && fbx_bytes(json, b->accessors.data, b->accessors.size) && fbx_text(json, "]}");
}

bool fbx_file_is(const uint8_t *data, size_t size, const char *name)
{
    if (!data) size = 0u;
    // Binary content takes precedence over a misleading written extension.
    // The mesh and skinned import paths both accept GLB by its magic.
    if (size >= 4u && !memcmp(data, "glTF", 4u)) return false;
    static const char magic[] = "Kaydara FBX Binary  \x00\x1a\x00";
    if (size >= 20u && !memcmp(data, magic, 20u)) return true;
    if (name) {
        size_t length = strlen(name);
        if (!strncmp(name, "http://", 7u) || !strncmp(name, "https://", 8u)) {
            const char *host = strstr(name, "://") + 3u;
            const char *path = host + strcspn(host, "/?#");
            length = *path == '/' ? (size_t)(path - name) + strcspn(path, "?#") : 0u;
        }
        if (length >= 4u && name[length-4u] == '.' && (name[length-3u] == 'f' || name[length-3u] == 'F')
            && (name[length-2u] == 'b' || name[length-2u] == 'B') && (name[length-1u] == 'x' || name[length-1u] == 'X')) return true;
    }
    // Recognize an ASCII FBX declaration, rather than a name or string in
    // another format. The header must begin a top-level line, outside quotes
    // and semicolon comments, and have FBX's `name: {` declaration syntax.
    size_t scan = size < 4096u ? size : 4096u;
    size_t start = scan >= 3u && data[0] == 0xefu && data[1] == 0xbbu && data[2] == 0xbfu ? 3u : 0u;
    uint32_t depth = 0u;
    bool line_start = true, quoted = false, comment = false;
    for (size_t i = start; i < scan; i++) {
        unsigned char c = data[i];
        if (comment) {
            if (c == '\n' || c == '\r') { comment = false; line_start = true; }
            continue;
        }
        if (quoted) {
            if (c == '\\' && i + 1u < scan) i++;
            else if (c == '"') quoted = false;
            continue;
        }
        if (c == '\n' || c == '\r') { line_start = true; continue; }
        if (c == ' ' || c == '\t') continue;
        if (line_start && !depth && i + 18u <= scan && !memcmp(data + i, "FBXHeaderExtension", 18u)) {
            size_t at = i + 18u;
            while (at < scan && (data[at] == ' ' || data[at] == '\t' || data[at] == '\r' || data[at] == '\n')) at++;
            if (at < scan && data[at++] == ':') {
                while (at < scan && (data[at] == ' ' || data[at] == '\t' || data[at] == '\r' || data[at] == '\n')) at++;
                if (at < scan && data[at] == '{') return true;
            }
        }
        line_start = false;
        if (c == ';') comment = true;
        else if (c == '"') quoted = true;
        else if (c == '{') depth++;
        else if (c == '}' && depth) depth--;
    }
    return false;
}

bool fbx_file_to_glb(const uint8_t *data, size_t size, bool skinned, uint8_t **out, size_t *out_size, char why[256], char note[256])
{
    return fbx_file_to_glb_parts(data, size, skinned, NULL, NULL, out, out_size, why, note);
}

bool fbx_file_to_glb_parts(const uint8_t *data, size_t size, bool skinned, const tide_skinned_keep_part_fn keep, void *keep_user,
                           uint8_t **out, size_t *out_size, char why[256], char note[256])
{
    *out = NULL;
    *out_size = 0u;
    why[0] = note[0] = '\0';
    fbx_builder b = {.why=why, .note=note, .skinned=skinned, .keep_part=keep, .keep_user=keep_user};
    b.bin.owner = b.views.owner = b.accessors.owner = b.meshes.owner = b.skins.owner = b.animations.owner = &b;
    if (!data || !size) return fbx_fail(&b, NULL, "has no FBX bytes");
    if (size > TIDE_FBX_MAX_BYTES) return FBX_PAST_LIMIT(&b, "split the model into smaller files", TIDE_FBX_MAX_BYTES, "bytes", size);
    ufbx_load_opts opts = {0};
    opts.file_format = UFBX_FILE_FORMAT_FBX;
    opts.target_axes = ufbx_axes_right_handed_y_up;
    opts.target_unit_meters = 1.0;
    opts.space_conversion = UFBX_SPACE_CONVERSION_ADJUST_TRANSFORMS;
    opts.geometry_transform_handling = UFBX_GEOMETRY_TRANSFORM_HANDLING_HELPER_NODES;
    opts.inherit_mode_handling = UFBX_INHERIT_MODE_HANDLING_HELPER_NODES;
    opts.node_depth_limit = 128u;
    opts.ignore_embedded = true;
    opts.ignore_animation = !skinned;
    opts.skip_skin_vertices = !skinned;
    opts.use_blender_pbr_material = true;
    // An index past its array (exporters write them: Synty's POLYGON packs have some) is clamped to the
    // array's last element, as Blender and Unity read such files, and every index stays in range.
    opts.index_error_handling = UFBX_INDEX_ERROR_HANDLING_CLAMP;
    opts.force_single_thread_ascii_parsing = true;
    opts.temp_allocator.memory_limit = TIDE_FBX_MAX_BYTES;
    opts.result_allocator.memory_limit = TIDE_FBX_MAX_BYTES;
    ufbx_error error;
    b.scene = ufbx_load_memory(data, size, &opts, &error);
    if (!b.scene) return fbx_fail(&b, NULL, "isn't an FBX that decodes (%.160s)", error.description.data);
    bool ok = b.scene->nodes.count != 0;
    if (!ok) fbx_fail(&b, NULL, "has no FBX nodes");
    if (ok && b.scene->nodes.count > TIDE_FBX_MAX_NODES) ok = FBX_PAST_LIMIT(&b, "split the model into smaller files", TIDE_FBX_MAX_NODES, "nodes", b.scene->nodes.count);
    if (ok && b.scene->anim_stacks.count > TIDE_FBX_MAX_NODES) ok = FBX_PAST_LIMIT(&b, "split the animation into several files", TIDE_FBX_MAX_NODES, "animation takes", b.scene->anim_stacks.count);
    if (ok) {
        b.node_mesh = malloc(b.scene->nodes.count * sizeof *b.node_mesh);
        b.node_skin = malloc(b.scene->nodes.count * sizeof *b.node_skin);
        ok = b.node_mesh && b.node_skin;
        if (!ok) fbx_fail(&b, NULL, "can't allocate FBX node data");
        else {
            memset(b.node_mesh, 0xff, b.scene->nodes.count * sizeof *b.node_mesh);
            memset(b.node_skin, 0xff, b.scene->nodes.count * sizeof *b.node_skin);
        }
    }
    if (ok && keep) ok = fbx_drop_parts(&b);
    for (size_t i = 0; ok && i < b.scene->nodes.count; i++) ok = fbx_mesh(&b, b.scene->nodes.data[i]);
    if (ok && keep && !b.parts_kept) { // Say some of the parts it has, so the names can be written
        char parts[160] = "";
        size_t used = 0;
        for (size_t i = 0; i < b.scene->nodes.count && used < sizeof parts - 40u; i++) {
            const ufbx_node *n = b.scene->nodes.data[i];
            if (!n->mesh || n->is_geometry_transform_helper || !n->name.length) continue;
            used += (size_t)snprintf(parts + used, sizeof parts - used, "%s'%.32s'", used ? ", " : "", n->name.data);
        }
        ok = fbx_fail(&b, used ? "its parts are the nodes holding meshes, by name" : NULL, "keeps none of its parts%s%s",
                      used ? "; it has " : "", parts);
    }
    if (ok && !skinned && !b.indices) ok = fbx_fail(&b, "export a polygon mesh", "has no FBX triangles: there's nothing in it to draw");
    if (ok) ok = fbx_animations(&b);
    fbx_buffer json = {.owner=&b}, glb = {.owner=&b};
    if (ok) ok = fbx_json(&b, &json);
    while (ok && json.size % 4u) ok = fbx_text(&json, " ");
    if (ok) {
        size_t total = 28u + json.size + b.bin.size;
        if (total > TIDE_FBX_MAX_BYTES) ok = FBX_PAST_LIMIT(&b, "split the model into smaller files", TIDE_FBX_MAX_BYTES, "bytes of converted model data", total);
        else ok = fbx_word(&glb, 0x46546c67u) && fbx_word(&glb, 2u) && fbx_word(&glb, (uint32_t)total)
               && fbx_word(&glb, (uint32_t)json.size) && fbx_word(&glb, 0x4e4f534au) && fbx_bytes(&glb, json.data, json.size)
               && fbx_word(&glb, (uint32_t)b.bin.size) && fbx_word(&glb, 0x004e4942u) && fbx_bytes(&glb, b.bin.data, b.bin.size);
    }
    if (ok) { *out = glb.data; *out_size = glb.size; } else free(glb.data);
    free(json.data);
    free(b.node_mesh); free(b.node_skin); free(b.node_index);
    free(b.bin.data); free(b.views.data); free(b.accessors.data); free(b.meshes.data); free(b.skins.data); free(b.animations.data);
    ufbx_free_scene(b.scene);
    return ok;
}
