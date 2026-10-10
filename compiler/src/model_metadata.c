// Catalog inspection for editors. glTF reads JSON only; FBX skips geometry,
// animation curves and embedded content. No format opens external files.
#include "model_metadata.h"

#include "assets.h"
#include "folders.h"
#include "fbx_files.h"
#include "sha256.h"
#include <ufbx.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define METADATA_JSON_MOST (16u * 1024u * 1024u)
#define METADATA_FBX_MOST (64u * 1024u * 1024u)

typedef struct edge {
    uint32_t parent, child;
} edge;

typedef struct reader {
    const uint8_t *at, *end;
    model_metadata *out;
    char *why;
    bool failed;
    edge *edges;
    uint32_t edge_count, edge_room;
    uint32_t animation_room, node_room, skin_room;
} reader;

static bool fail(reader *r, const char *why)
{
    if (!r->failed) snprintf(r->why, 256, "%s", why);
    r->failed = true;
    return false;
}

static void space(reader *r)
{
    while (r->at < r->end && (*r->at == ' ' || *r->at == '\t' || *r->at == '\n' || *r->at == '\r')) r->at++;
}

static bool take(reader *r, const uint8_t wanted)
{
    space(r);
    if (r->at >= r->end || *r->at != wanted) return fail(r, "has incomplete or invalid JSON");
    r->at++;
    return true;
}

static bool grow(reader *r, void **items, uint32_t *room, const uint32_t count, const size_t size)
{
    if (count < *room) return true;
    if (count >= INT32_MAX || (uint64_t)count + 1u > SIZE_MAX / size) return fail(r, "has too many metadata items");
    const uint32_t next = *room > INT32_MAX / 2u ? INT32_MAX : *room ? *room * 2u : 8u;
    if ((uint64_t)next > SIZE_MAX / size) return fail(r, "has too many metadata items");
    void *resized = realloc(*items, (size_t)next * size);
    if (!resized) abort();
    *items = resized;
    *room = next;
    return true;
}

static int hex(const uint8_t c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

static bool code4(reader *r, const uint8_t **at, const uint8_t *end, uint32_t *code)
{
    if (end - *at < 4) return fail(r, "has an incomplete Unicode escape");
    *code = 0;
    for (int i = 0; i < 4; i++) {
        const int digit = hex(*(*at)++);
        if (digit < 0) return fail(r, "has an invalid Unicode escape");
        *code = *code * 16u + (uint32_t)digit;
    }
    return true;
}

static void emit(char *out, size_t *count, const uint32_t code)
{
    if (!out) return;
    if (code < 0x80u) out[(*count)++] = (char)code;
    else if (code < 0x800u) {
        out[(*count)++] = (char)(0xc0u | code >> 6);
        out[(*count)++] = (char)(0x80u | (code & 0x3fu));
    } else if (code < 0x10000u) {
        out[(*count)++] = (char)(0xe0u | code >> 12);
        out[(*count)++] = (char)(0x80u | ((code >> 6) & 0x3fu));
        out[(*count)++] = (char)(0x80u | (code & 0x3fu));
    } else {
        out[(*count)++] = (char)(0xf0u | code >> 18);
        out[(*count)++] = (char)(0x80u | ((code >> 12) & 0x3fu));
        out[(*count)++] = (char)(0x80u | ((code >> 6) & 0x3fu));
        out[(*count)++] = (char)(0x80u | (code & 0x3fu));
    }
}

static bool string(reader *r, char **result)
{
    if (result) *result = NULL;
    if (!take(r, '"')) return false;
    const uint8_t *from = r->at, *end = from;
    while (end < r->end && *end != '"') {
        if (*end == '\\') {
            end++;
            if (end == r->end) break;
        }
        end++;
    }
    if (end == r->end) return fail(r, "has an unterminated JSON string");
    char *out = result ? malloc((size_t)(end - from) + 1u) : NULL;
    if (result && !out) abort();
    size_t count = 0;
    const uint8_t *p = from;
    while (p < end && !r->failed) {
        uint32_t code = *p++;
        if (code < 0x20u) {
            fail(r, "has a control character in a JSON string");
            break;
        }
        if (code == '\\') {
            if (p == end) { fail(r, "has an incomplete JSON escape"); break; }
            code = *p++;
            switch (code) {
            case '"': case '\\': case '/': break;
            case 'b': code = '\b'; break;
            case 'f': code = '\f'; break;
            case 'n': code = '\n'; break;
            case 'r': code = '\r'; break;
            case 't': code = '\t'; break;
            case 'u':
                if (!code4(r, &p, end, &code)) break;
                if (code >= 0xd800u && code <= 0xdbffu) {
                    uint32_t low;
                    if (end - p < 6 || p[0] != '\\' || p[1] != 'u') {
                        fail(r, "has an unpaired Unicode surrogate"); break;
                    }
                    p += 2;
                    if (!code4(r, &p, end, &low)) break;
                    if (low < 0xdc00u || low > 0xdfffu) { fail(r, "has an unpaired Unicode surrogate"); break; }
                    code = 0x10000u + ((code - 0xd800u) << 10) + low - 0xdc00u;
                } else if (code >= 0xdc00u && code <= 0xdfffu) {
                    fail(r, "has an unpaired Unicode surrogate");
                }
                break;
            default: fail(r, "has an invalid JSON escape"); break;
            }
        } else if (code >= 0x80u) {
            const uint32_t initial = code;
            const int more = code >= 0xc2u && code <= 0xdfu ? 1 : code >= 0xe0u && code <= 0xefu ? 2
                           : code >= 0xf0u && code <= 0xf4u ? 3 : -1;
            if (more < 0 || end - p < more) { fail(r, "has invalid UTF-8 in JSON"); break; }
            code &= more == 1 ? 0x1fu : more == 2 ? 0x0fu : 7u;
            for (int i = 0; i < more; i++) {
                const uint32_t next = *p++;
                if (next < 0x80u || next > 0xbfu) { fail(r, "has invalid UTF-8 in JSON"); break; }
                code = (code << 6) | (next & 0x3fu);
            }
            const uint32_t least = more == 1 ? 0x80u : more == 2 ? 0x800u : 0x10000u;
            if (code < least || code > 0x10ffffu || (code >= 0xd800u && code <= 0xdfffu) || initial > 0xf4u) {
                fail(r, "has invalid UTF-8 in JSON");
            }
        }
        if (result && code == 0) fail(r, "has a metadata name or key containing a NUL");
        if (!r->failed) emit(out, &count, code);
    }
    if (r->failed) { free(out); return false; }
    if (out) { out[count] = '\0'; *result = out; }
    r->at = end + 1;
    return true;
}

static bool number(reader *r)
{
    const uint8_t *p = r->at;
    if (p < r->end && *p == '-') p++;
    if (p >= r->end) return fail(r, "has an invalid JSON number");
    if (*p == '0') p++;
    else {
        if (*p < '1' || *p > '9') return fail(r, "has an invalid JSON number");
        do { p++; } while (p < r->end && *p >= '0' && *p <= '9');
    }
    if (p < r->end && *p == '.') {
        p++;
        if (p >= r->end || *p < '0' || *p > '9') return fail(r, "has an invalid JSON number");
        do { p++; } while (p < r->end && *p >= '0' && *p <= '9');
    }
    if (p < r->end && (*p == 'e' || *p == 'E')) {
        p++;
        if (p < r->end && (*p == '+' || *p == '-')) p++;
        if (p >= r->end || *p < '0' || *p > '9') return fail(r, "has an invalid JSON number");
        do { p++; } while (p < r->end && *p >= '0' && *p <= '9');
    }
    r->at = p;
    return true;
}

static bool uint_value(reader *r, uint32_t *result)
{
    space(r);
    const uint8_t *from = r->at;
    if (!number(r)) return false;
    uint32_t value = 0;
    for (const uint8_t *p = from; p < r->at; p++) {
        if (*p < '0' || *p > '9' || value > (uint32_t)INT32_MAX / 10u
            || value * 10u > (uint32_t)INT32_MAX - (*p - '0')) return fail(r, "has an invalid metadata index");
        value = value * 10u + (*p - '0');
    }
    *result = value;
    return true;
}

static bool value(reader *r, int depth);

// The first item follows an opening delimiter; subsequent items follow a
// comma. A closing delimiter is allowed only before the first item.
static bool item(reader *r, const uint8_t closing, const bool first, bool *done)
{
    space(r);
    if (r->at < r->end && *r->at == closing) { r->at++; *done = true; return true; }
    *done = false;
    if (!first && !take(r, ',')) return false;
    return true;
}

static bool key(reader *r, char **name)
{
    if (!string(r, name)) return false;
    if (!take(r, ':')) { free(*name); *name = NULL; return false; }
    return true;
}

static bool value(reader *r, const int depth)
{
    space(r);
    if (r->at == r->end || depth > 64) return fail(r, "has incomplete or excessively nested JSON");
    const uint8_t c = *r->at;
    if (c == '"') return string(r, NULL);
    if (c == '{' || c == '[') {
        r->at++;
        bool first = true, done;
        for (;;) {
            if (!item(r, c == '{' ? '}' : ']', first, &done)) return false;
            if (done) return true;
            if (c == '{') { char *name; if (!key(r, &name)) return false; free(name); }
            if (!value(r, depth + 1)) return false;
            first = false;
        }
    }
    static const char *const words[] = {"true", "false", "null"};
    for (int i = 0; i < 3; i++) {
        const size_t count = strlen(words[i]);
        if ((size_t)(r->end - r->at) >= count && memcmp(r->at, words[i], count) == 0) { r->at += count; return true; }
    }
    return number(r);
}

static bool index_list(reader *r, uint32_t **indices, uint32_t *count)
{
    uint32_t room = 0;
    if (!take(r, '[')) return false;
    bool first = true, done;
    for (;;) {
        if (!item(r, ']', first, &done)) return false;
        if (done) return true;
        if (!grow(r, (void **)indices, &room, *count, sizeof **indices) || !uint_value(r, &(*indices)[*count])) return false;
        (*count)++;
        first = false;
    }
}

static bool named_object(reader *r, char **name, const int32_t node, model_metadata_skin *skin)
{
    if (!take(r, '{')) return false;
    bool first = true, done, named = false, children = false, joints = false, root = false;
    for (;;) {
        if (!item(r, '}', first, &done)) return false;
        if (done) {
            if (skin && (!joints || !skin->joint_count)) return fail(r, "has a skin without joints");
            return true;
        }
        char *field;
        if (!key(r, &field)) return false;
        bool ok;
        if (strcmp(field, "name") == 0) {
            ok = named ? fail(r, "has duplicate name properties") : string(r, name);
            named = true;
        } else if (node >= 0 && strcmp(field, "children") == 0) {
            uint32_t *indices = NULL, count = 0;
            ok = children ? fail(r, "has duplicate children properties") : index_list(r, &indices, &count);
            children = true;
            for (uint32_t i = 0; ok && i < count; i++) {
                ok = grow(r, (void **)&r->edges, &r->edge_room, r->edge_count, sizeof *r->edges);
                if (ok) r->edges[r->edge_count++] = (edge){(uint32_t)node, indices[i]};
            }
            free(indices);
        } else if (skin && strcmp(field, "joints") == 0) {
            ok = joints ? fail(r, "has duplicate joints properties") : index_list(r, &skin->joints, &skin->joint_count);
            joints = true;
        } else if (skin && strcmp(field, "skeleton") == 0) {
            uint32_t index = 0;
            ok = root ? fail(r, "has duplicate skeleton properties") : uint_value(r, &index);
            if (ok) skin->root = (int32_t)index;
            root = true;
        } else ok = value(r, 2);
        free(field);
        if (!ok) return false;
        first = false;
    }
}

static bool catalog(reader *r, const int kind)
{
    if (!take(r, '[')) return false;
    bool first = true, done;
    for (;;) {
        if (!item(r, ']', first, &done)) return false;
        if (done) return true;
        model_metadata *m = r->out;
        if (kind == 0) {
            if (!grow(r, (void **)&m->animations, &r->animation_room, m->animation_count, sizeof *m->animations)) return false;
            char **name = &m->animations[m->animation_count++];
            *name = NULL;
            if (!named_object(r, name, -1, NULL)) return false;
        } else if (kind == 1) {
            if (!grow(r, (void **)&m->nodes, &r->node_room, m->node_count, sizeof *m->nodes)) return false;
            const int32_t index = (int32_t)m->node_count;
            model_metadata_node *n = &m->nodes[m->node_count++];
            *n = (model_metadata_node){.parent = -1};
            if (!named_object(r, &n->name, index, NULL)) return false;
        } else {
            if (!grow(r, (void **)&m->skins, &r->skin_room, m->skin_count, sizeof *m->skins)) return false;
            model_metadata_skin *s = &m->skins[m->skin_count++];
            *s = (model_metadata_skin){.root = -1};
            if (!named_object(r, &s->name, -1, s)) return false;
        }
        first = false;
    }
}

static bool asset(reader *r)
{
    if (!take(r, '{')) return false;
    bool first = true, done, version = false;
    for (;;) {
        if (!item(r, '}', first, &done)) return false;
        if (done) return version || fail(r, "has no glTF asset version");
        char *field;
        if (!key(r, &field)) return false;
        bool ok;
        if (strcmp(field, "version") == 0) {
            char *text = NULL;
            ok = !version && string(r, &text);
            if (version) ok = fail(r, "has duplicate glTF version properties");
            if (ok && strcmp(text, "2.0") != 0) ok = fail(r, "is not glTF 2.0");
            free(text);
            version = true;
        } else ok = value(r, 2);
        free(field);
        if (!ok) return false;
        first = false;
    }
}

static bool hierarchy(reader *r)
{
    model_metadata *m = r->out;
    for (uint32_t i = 0; i < r->edge_count; i++) {
        const edge e = r->edges[i];
        if (e.child >= m->node_count) return fail(r, "has a child node outside its node array");
        if (m->nodes[e.child].parent >= 0) return fail(r, "has a node listed as a child more than once");
        m->nodes[e.child].parent = (int32_t)e.parent;
    }
    uint8_t *colors = calloc(m->node_count ? m->node_count : 1u, 1u);
    if (!colors) abort();
    for (uint32_t i = 0; i < m->node_count; i++) {
        int32_t at = (int32_t)i;
        while (at >= 0 && !colors[at]) { colors[at] = 1; at = m->nodes[at].parent; }
        if (at >= 0 && colors[at] == 1) { free(colors); return fail(r, "has a cycle in its node hierarchy"); }
        at = (int32_t)i;
        while (at >= 0 && colors[at] == 1) { colors[at] = 2; at = m->nodes[at].parent; }
    }
    free(colors);
    uint32_t *seen = calloc(m->node_count ? m->node_count : 1u, sizeof *seen);
    if (!seen) abort();
    for (uint32_t i = 0; i < m->skin_count; i++) {
        const model_metadata_skin *s = &m->skins[i];
        if (s->root >= 0 && (uint32_t)s->root >= m->node_count) { free(seen); return fail(r, "has a skeleton node outside its node array"); }
        for (uint32_t k = 0; k < s->joint_count; k++) {
            const uint32_t joint = s->joints[k];
            if (joint >= m->node_count) { free(seen); return fail(r, "has a joint outside its node array"); }
            if (seen[joint] == i + 1u) { free(seen); return fail(r, "has duplicate joints in a skin"); }
            seen[joint] = i + 1u;
        }
    }
    free(seen);
    return true;
}

static bool json_read(const uint8_t *data, const size_t size, model_metadata *out, char why[256])
{
    reader r = {.at = data, .end = data + size, .out = out, .why = why};
    if (!take(&r, '{')) return false;
    bool first = true, done, saw_asset = false;
    bool catalogs[3] = {false, false, false};
    for (;;) {
        if (!item(&r, '}', first, &done)) break;
        if (done) break;
        char *field;
        if (!key(&r, &field)) break;
        bool ok;
        if (strcmp(field, "asset") == 0) {
            ok = saw_asset ? fail(&r, "has duplicate asset properties") : asset(&r);
            saw_asset = true;
        } else {
            const int kind = strcmp(field, "animations") == 0 ? 0 : strcmp(field, "nodes") == 0 ? 1 : strcmp(field, "skins") == 0 ? 2 : -1;
            if (kind >= 0) {
                ok = catalogs[kind] ? fail(&r, "has duplicate catalog properties") : catalog(&r, kind);
                catalogs[kind] = true;
            } else ok = value(&r, 1);
        }
        free(field);
        if (!ok) break;
        first = false;
    }
    space(&r);
    if (!r.failed && r.at != r.end) fail(&r, "has bytes after its JSON object");
    if (!r.failed && !saw_asset) fail(&r, "has no glTF asset version");
    if (!r.failed) hierarchy(&r);
    free(r.edges);
    return !r.failed;
}

static uint32_t little32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static bool glb_header(const uint8_t *data, const size_t header, const uint64_t size, uint32_t *json, char why[256])
{
    if (header < 20 || little32(data) != 0x46546c67u || little32(data + 4) != 2u || little32(data + 8) != size
        || little32(data + 16) != 0x4e4f534au || little32(data + 12) % 4u != 0 || (uint64_t)little32(data + 12) + 20u > size) {
        snprintf(why, 256, "has an incomplete or invalid GLB 2 JSON header");
        return false;
    }
    *json = little32(data + 12);
    return true;
}

static bool fbx_name(const ufbx_string source, char **out, char why[256])
{
    if (source.length == 0) return true;
    if (memchr(source.data, 0, source.length)) { snprintf(why, 256, "has an FBX catalog name containing a NUL"); return false; }
    *out = malloc(source.length + 1u);
    if (!*out) abort();
    memcpy(*out, source.data, source.length);
    (*out)[source.length] = '\0';
    return true;
}

static bool fbx_read(const uint8_t *data, const size_t size, const bool interactive, model_metadata *out, char why[256],
                     bool *unavailable)
{
    // Keep helper generation and source ordering in step with fbx_files.c.
    // Ignoring content retains nodes, animation stacks and skin clusters.
    ufbx_load_opts opts = {0};
    opts.file_format = UFBX_FILE_FORMAT_FBX;
    opts.ignore_geometry = opts.ignore_animation = opts.ignore_embedded = true;
    opts.skip_skin_vertices = true;
    opts.target_axes = ufbx_axes_right_handed_y_up;
    opts.target_unit_meters = 1.0;
    opts.space_conversion = UFBX_SPACE_CONVERSION_ADJUST_TRANSFORMS;
    opts.geometry_transform_handling = UFBX_GEOMETRY_TRANSFORM_HANDLING_HELPER_NODES;
    opts.inherit_mode_handling = UFBX_INHERIT_MODE_HANDLING_HELPER_NODES;
    opts.node_depth_limit = 128;
    opts.index_error_handling = UFBX_INDEX_ERROR_HANDLING_ABORT_LOADING;
    opts.use_blender_pbr_material = true;
    opts.force_single_thread_ascii_parsing = true;
    opts.temp_allocator.memory_limit = opts.result_allocator.memory_limit = (interactive ? 128u : 512u) * 1024u * 1024u;
    ufbx_error error;
    ufbx_scene *scene = ufbx_load_memory(data, size, &opts, &error);
    if (!scene) {
        if (unavailable) *unavailable = error.type == UFBX_ERROR_MEMORY_LIMIT;
        snprintf(why, 256, "FBX catalog metadata: %.*s", (int)error.description.length, error.description.data);
        return false;
    }
    bool ok = false;
    if (scene->nodes.count > INT32_MAX || scene->anim_stacks.count > INT32_MAX || scene->skin_deformers.count > INT32_MAX) {
        snprintf(why, 256, "has too many FBX catalog items");
        goto done;
    }
    out->node_count = (uint32_t)scene->nodes.count;
    out->animation_count = (uint32_t)scene->anim_stacks.count;
    out->skin_count = (uint32_t)scene->skin_deformers.count;
    out->nodes = calloc(out->node_count ? out->node_count : 1u, sizeof *out->nodes);
    out->animations = calloc(out->animation_count ? out->animation_count : 1u, sizeof *out->animations);
    out->skins = calloc(out->skin_count ? out->skin_count : 1u, sizeof *out->skins);
    if (!out->nodes || !out->animations || !out->skins) abort();
    for (uint32_t i = 0; i < out->node_count; i++) {
        const ufbx_node *node = scene->nodes.data[i];
        out->nodes[i].parent = node->parent ? (int32_t)node->parent->typed_id : -1;
        if (!fbx_name(node->name, &out->nodes[i].name, why)) goto done;
    }
    for (uint32_t i = 0; i < out->animation_count; i++) {
        if (!fbx_name(scene->anim_stacks.data[i]->name, &out->animations[i], why)) goto done;
    }
    for (uint32_t i = 0; i < out->skin_count; i++) {
        const ufbx_skin_deformer *skin = scene->skin_deformers.data[i];
        model_metadata_skin *target = &out->skins[i];
        target->root = -1;
        if (skin->clusters.count > INT32_MAX) { snprintf(why, 256, "has too many FBX skin clusters"); goto done; }
        if (!fbx_name(skin->name, &target->name, why)) goto done;
        target->joint_count = (uint32_t)skin->clusters.count;
        target->joints = calloc(target->joint_count ? target->joint_count : 1u, sizeof *target->joints);
        if (!target->joints) abort();
        for (uint32_t k = 0; k < target->joint_count; k++) {
            const ufbx_node *bone = skin->clusters.data[k]->bone_node;
            if (!bone || bone->typed_id >= out->node_count) { snprintf(why, 256, "has an FBX skin cluster without a bone"); goto done; }
            target->joints[k] = bone->typed_id;
        }
    }
    ok = true;
done:
    ufbx_free_scene(scene);
    if (!ok) model_metadata_free(out);
    return ok;
}

bool model_metadata_read(const uint8_t *data, const size_t size, const char *name, model_metadata *out, char why[256])
{
    *out = (model_metadata){0};
    why[0] = '\0';
    if (!data || !size) { snprintf(why, 256, "has no model metadata"); return false; }
    if (size >= 4 && little32(data) == 0x46546c67u) {
        uint32_t count;
        if (!glb_header(data, size, size, &count, why)) return false;
        data += 20;
        if (!json_read(data, count, out, why)) { model_metadata_free(out); return false; }
    } else if (fbx_file_is(data, size, name)) return fbx_read(data, size, false, out, why, NULL);
    else if (!json_read(data, size, out, why)) { model_metadata_free(out); return false; }
    return true;
}

void model_metadata_free(model_metadata *m)
{
    for (uint32_t i = 0; i < m->animation_count; i++) free(m->animations[i]);
    for (uint32_t i = 0; i < m->node_count; i++) free(m->nodes[i].name);
    for (uint32_t i = 0; i < m->skin_count; i++) { free(m->skins[i].name); free(m->skins[i].joints); }
    free(m->animations);
    free(m->nodes);
    free(m->skins);
    *m = (model_metadata){0};
}

typedef struct cached_metadata {
    struct cached_metadata *next;
    char *path;
    unsigned long long size, modified;
    bool present, dirty;
    model_metadata_status status;
    model_metadata metadata;
    char why[256];
} cached_metadata;

static cached_metadata *cached;
static model_metadata_stats statistics;

static model_metadata_status inspect(const char *path, const char *name, const unsigned long long size, model_metadata *out, char why[256])
{
    FILE *in = fopen(path, "rb");
    if (!in) { snprintf(why, 256, "model metadata can't be read"); return MODEL_METADATA_UNAVAILABLE; }
    uint8_t header[4096];
    size_t got = fread(header, 1, 20u, in);
    statistics.file_reads++;
    statistics.bytes_read += got;
    uint32_t count = 0;
    const bool glb = got >= 4 && little32(header) == 0x46546c67u;
    bool fbx = !glb && fbx_file_is(header, got, name);
    if (!glb && !fbx) {
        // Extensionless URL caches can contain ASCII FBX with leading
        // comments. Match the full importer's bounded header sniff.
        const size_t prefix = size < sizeof header ? (size_t)size : sizeof header;
        const size_t extra = prefix > got ? fread(header + got, 1, prefix - got, in) : 0;
        got += extra;
        statistics.bytes_read += extra;
        fbx = fbx_file_is(header, got, name);
    }
    if (glb && !glb_header(header, got, size, &count, why)) { fclose(in); return MODEL_METADATA_INVALID; }
    if (!glb && size > UINT32_MAX) { fclose(in); snprintf(why, 256, "model metadata is too large for interactive inspection"); return MODEL_METADATA_UNAVAILABLE; }
    const uint64_t bytes = glb ? count : size;
    if (bytes > (fbx ? METADATA_FBX_MOST : METADATA_JSON_MOST)) {
        fclose(in);
        snprintf(why, 256, "%s exceeds the %u MiB interactive inspection budget", fbx ? "FBX source" : "model JSON", fbx ? 64u : 16u);
        return MODEL_METADATA_UNAVAILABLE;
    }
    uint8_t *json = malloc(bytes ? (size_t)bytes : 1u);
    if (!json) abort();
    size_t have = 0;
    if (!glb) {
        have = got;
        if (have > bytes) have = (size_t)bytes;
        memcpy(json, header, have);
    }
    const size_t more = fread(json + have, 1, (size_t)bytes - have, in);
    statistics.bytes_read += more;
    fclose(in);
    if (have + more != bytes) { free(json); snprintf(why, 256, "model metadata changed or couldn't be read"); return MODEL_METADATA_UNAVAILABLE; }
    statistics.parses++;
    bool unavailable = false;
    const bool ok = fbx ? fbx_read(json, (size_t)bytes, true, out, why, &unavailable) : json_read(json, (size_t)bytes, out, why);
    free(json);
    if (!ok) { model_metadata_free(out); return unavailable ? MODEL_METADATA_UNAVAILABLE : MODEL_METADATA_INVALID; }
    return MODEL_METADATA_READY;
}

static model_metadata_status cached_named(const char *path, const char *name, const model_metadata **out, const char **why)
{
    *out = NULL;
    cached_metadata *entry = cached;
    while (entry && !asset_path_same(entry->path, path)) entry = entry->next;
    if (!entry) {
        entry = calloc(1, sizeof *entry);
        if (!entry) abort();
        entry->path = malloc(strlen(path) + 1u);
        if (!entry->path) abort();
        strcpy(entry->path, path);
        entry->dirty = true;
        entry->next = cached;
        cached = entry;
    }
    unsigned long long size = 0, modified = 0;
    const bool present = folder_file_info(path, &size, &modified);
    if (!entry->dirty && entry->present == present && entry->size == size && entry->modified == modified) {
        statistics.cache_hits++;
    } else {
        model_metadata_free(&entry->metadata);
        entry->present = present;
        entry->size = size;
        entry->modified = modified;
        entry->dirty = false;
        entry->why[0] = '\0';
        if (present) entry->status = inspect(path, name, size, &entry->metadata, entry->why);
        else { entry->status = MODEL_METADATA_UNAVAILABLE; snprintf(entry->why, sizeof entry->why, "model metadata file is unavailable"); }
    }
    *why = entry->why;
    if (entry->status == MODEL_METADATA_READY) *out = &entry->metadata;
    return entry->status;
}

model_metadata_status model_metadata_cached(const char *path, const model_metadata **out, const char **why)
{
    return cached_named(path, path, out, why);
}

model_metadata_status model_metadata_cached_url(const char *url, const char *game_folder, const model_metadata **out,
                                                const char **why)
{
    char digest[65];
    sha256_hex(url, strlen(url), digest);
    const char *temporary = getenv("TMPDIR");
    if (!temporary) temporary = getenv("TEMP");
    if (!temporary) temporary = "/tmp";
    const char *base = game_folder ? game_folder : temporary;
    const char *middle = game_folder ? "/.tide/assets/urls/" : "/tide-assets/";
    const size_t length = strlen(base) + strlen(middle) + sizeof digest;
    char *path = malloc(length);
    if (!path) abort();
    snprintf(path, length, "%s%s%s", base, middle, digest);
    const model_metadata_status status = cached_named(path, url, out, why);
    free(path);
    return status;
}

void model_metadata_invalidate(const char *path)
{
    for (cached_metadata *entry = cached; entry; entry = entry->next) {
        if (!path || asset_path_same(entry->path, path)) entry->dirty = true;
    }
}

void model_metadata_forget(void)
{
    while (cached) {
        cached_metadata *next = cached->next;
        model_metadata_free(&cached->metadata);
        free(cached->path);
        free(cached);
        cached = next;
    }
    statistics = (model_metadata_stats){0};
}

model_metadata_stats model_metadata_statistics(void) { return statistics; }
