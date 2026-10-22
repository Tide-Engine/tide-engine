// FBX belongs to the build tools. Converting through self-contained glTF keeps
// the existing canonical mesh/pose representation and the runtime boundary.
#include "fbx_files.h"

#include "ufbx.h"

#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma STDC FP_CONTRACT OFF

#define FBX_BYTES_MOST (512u << 20)
#define FBX_NODES_MOST 65536u
#define FBX_VERTICES_MOST (1u << 24)
#define FBX_INDICES_MOST (3u << 25)

typedef struct fbx_builder fbx_builder;
typedef struct fbx_buffer {
    uint8_t *data;
    size_t size, room;
    fbx_builder *owner;
} fbx_buffer;

struct fbx_builder {
    char *why, *note;
    bool failed, skinned;
    ufbx_scene *scene;
    fbx_buffer bin, views, accessors, meshes, skins, animations;
    uint32_t num_views, num_accessors, num_meshes, num_skins;
    uint32_t vertices, indices;
    uint32_t *node_mesh, *node_skin;
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

static bool fbx_reserve(fbx_buffer *s, size_t more)
{
    if (s->owner->failed) return false;
    if (more > FBX_BYTES_MOST - s->size) return fbx_fail(s->owner, "split the model into smaller files", "needs more than 512 MiB of FBX conversion data");
    size_t need = s->size + more;
    if (need <= s->room) return true;
    size_t room = s->room ? s->room : 256u;
    while (room < need) room = room > FBX_BYTES_MOST / 2u ? FBX_BYTES_MOST : room * 2u;
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

static uint32_t fbx_accessor(fbx_builder *b, size_t begin, uint32_t count, const char *type, uint32_t component)
{
    uint32_t view = b->num_views++, accessor = b->num_accessors++;
    fbx_text(&b->views, "%s{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu}", view ? "," : "", begin, b->bin.size - begin);
    fbx_text(&b->accessors, "%s{\"bufferView\":%u,\"componentType\":%u,\"count\":%u,\"type\":\"%s\"}",
             accessor ? "," : "", view, component, count, type);
    fbx_align(&b->bin);
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
    if ((uint64_t)count * sets * 24u > FBX_BYTES_MOST) return fbx_fail(b, NULL, "needs more than 512 MiB of FBX vertex influences");
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
        if (!fbx_text(&b->skins, "%s%u", i ? "," : "", skin->clusters.data[i]->bone_node->typed_id)) return false;
    }
    if (fallback && !fbx_text(&b->skins, ",%u", node->typed_id)) return false;
    return fbx_text(&b->skins, "]}");
}

static bool fbx_skin_vertex_has_weights(fbx_builder *b, const ufbx_skin_deformer *skin, size_t index, bool *has)
{
    *has = false;
    if (index >= skin->vertices.count) return true;
    const ufbx_skin_vertex *v = skin->vertices.data + index;
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
    const ufbx_skin_deformer *skin = NULL;
    bool fallback = false;
    if (b->skinned) {
        if (mesh->blend_deformers.count) return fbx_fail(b, "export without blend shapes or morph targets", "has FBX blend shapes, which skinned models don't read yet");
        if (mesh->cache_deformers.count) return fbx_fail(b, "export bone animation instead of a vertex cache", "has FBX vertex-cache deformation, which Tide doesn't read");
        if (mesh->skin_deformers.count > 1u) return fbx_fail(b, "merge the skin deformers before exporting", "has several FBX skin deformers on one mesh");
        if (mesh->skin_deformers.count) {
            skin = mesh->skin_deformers.data[0];
            if (skin->skinning_method != UFBX_SKINNING_METHOD_LINEAR && skin->skinning_method != UFBX_SKINNING_METHOD_RIGID) return fbx_fail(b, "export using linear blend skinning", "uses FBX dual-quaternion or other unsupported skinning");
            for (size_t i = 0; i < mesh->num_vertices; i++) {
                bool has;
                if (!fbx_skin_vertex_has_weights(b, skin, i, &has)) return false;
                if (!has) fallback = true;
            }
            if (!fbx_skin(b, node, skin, fallback)) return false;
        }
    }
    if (mesh->num_indices > FBX_VERTICES_MOST || mesh->num_triangles > FBX_INDICES_MOST / 3u
        || mesh->max_face_triangles > FBX_INDICES_MOST / 3u) return fbx_fail(b, "split the model into smaller files", "has too many FBX corners or triangles");
    uint32_t *map = malloc(mesh->num_indices * sizeof *map);
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
        memset(map, 0xff, mesh->num_indices * sizeof *map);
        fbx_buffer indices = {.owner=b}, attrs = {.owner=b};
        uint32_t count = 0u, num_indices = 0u;
        for (size_t fi = 0; ok && fi < part->face_indices.count; fi++) {
            ufbx_face face = mesh->faces.data[part->face_indices.data[fi]];
            if (face.num_indices < 3u) continue;
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
        if (ok && ((uint64_t)b->vertices + count > FBX_VERTICES_MOST || (uint64_t)b->indices + num_indices > FBX_INDICES_MOST)) ok = fbx_fail(b, "split the model into smaller files", "has more vertices or indices than a mesh holds");
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
        if (ok) {
            size_t begin = b->bin.size;
            ok = fbx_bytes(&b->bin, indices.data, indices.size);
            uint32_t accessor = fbx_accessor(b, begin, num_indices, "SCALAR", 5125u);
            ok = ok && fbx_text(&b->meshes, "%s{\"attributes\":{", primitives++ ? "," : "")
                    && fbx_bytes(&b->meshes, attrs.data, attrs.size)
                    && fbx_text(&b->meshes, "},\"indices\":%u", accessor);
            if (ok && part->index < node->materials.count) ok = fbx_text(&b->meshes, ",\"material\":%u", node->materials.data[part->index]->typed_id);
            if (ok) ok = fbx_text(&b->meshes, "}");
        }
        free(indices.data);
        free(attrs.data);
    }
    if (ok) ok = fbx_text(&b->meshes, "]}");
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
    if (count > FBX_BYTES_MOST / 20u) return fbx_fail(b, NULL, "has too many FBX animation keys");
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
        if (!(time > previous)) { ok = fbx_fail(b, "move the animation keys farther apart, or shorten its time range", "has FBX animation keys too close together for float timestamps"); break; }
        previous = time;
        ok = fbx_number(&times, time);
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
        keys++;
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

static bool fbx_animations(fbx_builder *b)
{
    if (!b->skinned) return true;
    for (size_t a = 0; a < b->scene->anim_stacks.count; a++) {
        const ufbx_anim_stack *stack = b->scene->anim_stacks.data[a];
        ufbx_bake_opts opts = {0};
        // Keep source timestamps during baking: helper-node scale channels
        // are evaluated while other nodes bake. Shift only the finished keys,
        // so every helper is sampled in the original take's time domain.
        opts.resample_rate = 60.0;
        opts.minimum_sample_rate = 60.0;
        opts.max_keyframe_segments = FBX_BYTES_MOST / sizeof(ufbx_baked_quat);
        opts.step_handling = UFBX_BAKE_STEP_HANDLING_IDENTICAL_TIME;
        opts.temp_allocator.memory_limit = FBX_BYTES_MOST;
        opts.result_allocator.memory_limit = FBX_BYTES_MOST;
        ufbx_error error;
        ufbx_baked_anim *anim = ufbx_bake_anim(b->scene, stack->anim, &opts, &error);
        if (!anim) return fbx_fail(b, NULL, "has FBX animation that can't be baked (%.160s)", error.description.data);
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
            ok = fbx_channel(b, &samplers, &channels, &count, node->typed_id, 0u, node->translation_keys.data, NULL, node->translation_keys.count, begin_time, duration)
              && fbx_channel(b, &samplers, &channels, &count, node->typed_id, 1u, NULL, node->rotation_keys.data, node->rotation_keys.count, begin_time, duration)
              && fbx_channel(b, &samplers, &channels, &count, node->typed_id, 2u, node->scale_keys.data, NULL, node->scale_keys.count, begin_time, duration);
        }
        // glTF clips have no independent duration field. A constant root channel
        // preserves an empty take and the stack's playback end when needed.
        if (ok && !root_translation) {
            ufbx_baked_vec3 keys[2] = {{0}};
            keys[0].value = keys[1].value = b->scene->root_node->local_transform.translation;
            keys[1].time = duration;
            if (ok) ok = fbx_channel(b, &samplers, &channels, &count, b->scene->root_node->typed_id, 0u, keys, NULL, duration > 0.0 ? 2u : 1u, 0.0, duration);
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
    bool ok = fbx_text(json, "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[%u]}],\"nodes\":[", b->scene->root_node->typed_id);
    for (size_t i = 0; ok && i < b->scene->nodes.count; i++) {
        const ufbx_node *node = b->scene->nodes.data[i];
        ok = fbx_text(json, "%s{\"name\":", i ? "," : "") && fbx_string(json, node->name) && fbx_transform_json(json, &node->local_transform);
        if (ok && b->node_mesh[i] != UINT32_MAX) ok = fbx_text(json, ",\"mesh\":%u", b->node_mesh[i]);
        if (ok && b->node_skin[i] != UINT32_MAX) ok = fbx_text(json, ",\"skin\":%u", b->node_skin[i]);
        if (ok && node->children.count) {
            ok = fbx_text(json, ",\"children\":[");
            for (size_t c = 0; ok && c < node->children.count; c++) ok = fbx_text(json, "%s%u", c ? "," : "", node->children.data[c]->typed_id);
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
    *out = NULL;
    *out_size = 0u;
    why[0] = note[0] = '\0';
    fbx_builder b = {.why=why, .note=note, .skinned=skinned};
    b.bin.owner = b.views.owner = b.accessors.owner = b.meshes.owner = b.skins.owner = b.animations.owner = &b;
    if (!data || !size || size > FBX_BYTES_MOST) return fbx_fail(&b, NULL, "has no FBX bytes, or more than 512 MiB");
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
    opts.temp_allocator.memory_limit = FBX_BYTES_MOST;
    opts.result_allocator.memory_limit = FBX_BYTES_MOST;
    ufbx_error error;
    b.scene = ufbx_load_memory(data, size, &opts, &error);
    if (!b.scene) return fbx_fail(&b, NULL, "isn't an FBX that decodes (%.160s)", error.description.data);
    bool ok = b.scene->nodes.count && b.scene->nodes.count <= FBX_NODES_MOST && b.scene->anim_stacks.count <= FBX_NODES_MOST;
    if (!ok) fbx_fail(&b, NULL, "has no FBX nodes, or more than 65536 nodes or animations");
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
    for (size_t i = 0; ok && i < b.scene->nodes.count; i++) ok = fbx_mesh(&b, b.scene->nodes.data[i]);
    if (ok && !skinned && !b.indices) ok = fbx_fail(&b, "export a polygon mesh", "has no FBX triangles: there's nothing in it to draw");
    if (ok) ok = fbx_animations(&b);
    fbx_buffer json = {.owner=&b}, glb = {.owner=&b};
    if (ok) ok = fbx_json(&b, &json);
    while (ok && json.size % 4u) ok = fbx_text(&json, " ");
    if (ok) {
        size_t total = 28u + json.size + b.bin.size;
        if (total > FBX_BYTES_MOST) ok = fbx_fail(&b, "split the model into smaller files", "needs more than 512 MiB of converted FBX data");
        else ok = fbx_word(&glb, 0x46546c67u) && fbx_word(&glb, 2u) && fbx_word(&glb, (uint32_t)total)
               && fbx_word(&glb, (uint32_t)json.size) && fbx_word(&glb, 0x4e4f534au) && fbx_bytes(&glb, json.data, json.size)
               && fbx_word(&glb, (uint32_t)b.bin.size) && fbx_word(&glb, 0x004e4942u) && fbx_bytes(&glb, b.bin.data, b.bin.size);
    }
    if (ok) { *out = glb.data; *out_size = glb.size; } else free(glb.data);
    free(json.data);
    free(b.node_mesh); free(b.node_skin);
    free(b.bin.data); free(b.views.data); free(b.accessors.data); free(b.meshes.data); free(b.skins.data); free(b.animations.data);
    ufbx_free_scene(b.scene);
    return ok;
}
