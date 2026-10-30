#include "tide/skinned.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdatomic.h>

#include "tide/debug.h"
#include "tide/jobs.h"
#include "tide/mesh.h"
#include "tide/net.h"
#include "tide/pack.h"
#include "tide/skinned_data.h"
#include "asset_store.h"
#include "skinned_cache.h"

_Static_assert(sizeof(tide_bone_transform) == 40u, "bone transforms have ten float words");
_Static_assert(TIDE_LIST_CHUNK >= TIDE_SKINNED_SHAPED_HEADER_SIZE, "a byte list's first chunk holds the model header");

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
        const uint32_t offset = tide_handle_offset(data.at);
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

void tide_skinned_own(tide_list *data, const uint32_t where)
{
    if (!data) return;
    tide_heap *heap = tide_heap_of(where);
    if (!heap || !data->at) { tide_list_own(data, 1u, where); return; }
    if (tide_pack_holds(data->at)) {
        // Unlike list ownership's trusted pack path, this boundary also sees
        // failed/invalid model values. Check the complete byte-list span first.
        const uint32_t offset = tide_handle_offset(data->at);
        const uint8_t *header = tide_pack_span(offset, sizeof(tide_block));
        tide_block block = {0};
        if (header) memcpy(&block, header, sizeof block);
        if (!header || block.a != block.b || block.a > INT32_MAX
            || !tide_pack_span(offset + (uint32_t)sizeof block, block.a)) {
            data->at = 0;
            return;
        }
        tide_list_own(data, 1u, where);
        return;
    }
    if (tide_asset_list(*data)) { tide_list_own(data, 1u, where); return; }
    model_seen model;
    tide_asset_data *asset = NULL;
    if (open_model(*data, &model)) {
        asset = tide_asset_model(model.bytes, (uint32_t)tide_list_count(*data));
        close_model(&model);
    }
    if (!asset) { tide_list_own(data, 1u, where); return; }
    // The payload has been copied before heap allocation can move source pages.
    const uint32_t at = tide_heap_alloc(heap, 32u);
    tide_asset_use(heap, asset, at);
    tide_block *block = tide_heap_write(heap, at);
    block->a = asset->count;
    block->b = TIDE_LIST_ASSET;
    memcpy(block + 1, asset->id, sizeof asset->id);
    data->at = tide_handle_of(where, at);
    tide_asset_release(asset);
}

// A model's bytes that can't change while its key stays the same: a block
// within the pack file's bytes (with the pack's generation), or an owned
// asset copy, named by the SHA-256 of what's in it.
typedef struct model_key {
    uint32_t kind, at;
    uint64_t generation;
    uint8_t id[32];
} model_key;

enum { KEY_NONE, KEY_PACK, KEY_ASSET };

// The key a model's bytes would have, without opening them: a pack block's
// still needs checking to lie within the file's bytes (key_of).
static bool quick_key(const tide_list data, model_key *out)
{
    memset(out, 0, sizeof *out);
    if (tide_pack_holds(data.at)) {
        // Generated code never writes the pack, and the runtime's writes
        // (loads, geometry defaults) change its generation.
        out->kind = KEY_PACK;
        out->at = data.at;
        out->generation = tide_pack_generation();
        return true;
    }
    const tide_asset_data *asset = tide_asset_list(data);
    if (!asset) return false;
    out->kind = KEY_ASSET;
    memcpy(out->id, asset->id, sizeof out->id);
    return true;
}

static bool key_of(const tide_list data, const model_seen *m, model_key *out)
{
    // Blocks laid out past the file's bytes are left out.
    return quick_key(data, out)
        && (out->kind != KEY_PACK || tide_pack_file_span(tide_handle_offset(data.at), (uint64_t)sizeof(tide_block) + m->view.size));
}

// What a model's header says, kept on each thread for bytes with a key:
// indexing a Pose and its Model check the model on every use.
typedef struct model_facts {
    model_key key;
    uint32_t nodes, size, node_at;
    int32_t valid; // nodes_valid: 0 until asked, then 1 or -1
    uint64_t fingerprint;
    uint8_t skeleton_hash[32];
} model_facts;

enum { MODEL_FACTS = 16 };
static TIDE_THREAD_LOCAL model_facts facts_seen[MODEL_FACTS];

static uint64_t fingerprint_of(const tide_skinned_data_view *v)
{
    const uint64_t hash = tide_hash(v->skeleton_hash, sizeof v->skeleton_hash);
    return hash ? hash : 1u;
}

// NULL when the bytes have no key or don't open: callers then look at them
// as they would without this.
static TIDE_THREAD_LOCAL model_facts *facts_last; // The last one found, checked first

static model_facts *known_model(const tide_list data)
{
    model_facts *last = facts_last;
    if (last && last->key.kind == KEY_PACK && last->key.at == data.at && tide_pack_holds(data.at)
        && last->key.generation == tide_pack_generation()) return last;
    model_key key;
    if (!quick_key(data, &key)) return NULL;
    uint32_t word;
    memcpy(&word, key.kind == KEY_PACK ? (const void *)&key.at : (const void *)key.id, sizeof word);
    model_facts *f = &facts_seen[(word * 2654435761u) >> 28];
    if (f->key.kind && memcmp(&f->key, &key, sizeof key) == 0) return facts_last = f;
    model_seen m;
    if (!open_model(data, &m)) return NULL;
    const bool kept = key_of(data, &m, &key);
    if (kept) {
        *f = (model_facts){.key = key, .nodes = m.view.nodes, .size = m.view.size, .node_at = m.view.node_at,
                           .fingerprint = fingerprint_of(&m.view)};
        memcpy(f->skeleton_hash, m.view.skeleton_hash, sizeof f->skeleton_hash);
    }
    close_model(&m);
    if (kept) facts_last = f;
    return kept ? f : NULL;
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
    if (tide_handle_where(transforms.at) != TIDE_IN_SCRATCH || tide_list_count(transforms) != (int32_t)count) return NULL;
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

// Binding callers validate every node before entering their private readers.
// Keep these decoded pointers inside that unchanged opened model's lifetime.
static void validated_node(const tide_skinned_data_view *v, const uint32_t index, tide_skinned_node_data *out)
{
    const uint8_t *p = v->data + v->node_at + (size_t)index * TIDE_SKINNED_NODE_SIZE;
    out->parent = index ? (int32_t)validated_word(p) : -1;
    out->source_node = validated_word(p + 4u);
    out->name = (const char *)v->data + validated_word(p + 8u);
    out->name_length = validated_word(p + 12u);
    validated_numbers(p + 16u, 3u, out->position);
    validated_numbers(p + 28u, 4u, out->rotation);
    validated_numbers(p + 44u, 3u, out->scale);
}

// A validated node's rest: only its ten stored little-endian TRS words.
static tide_bone_transform rest_of(const tide_skinned_data_view *v, const uint32_t index)
{
    float rest[10];
    validated_numbers(v->data + v->node_at + (size_t)index * TIDE_SKINNED_NODE_SIZE + 16u, 10u, rest);
    return (tide_bone_transform){tide_f3(rest[0], rest[1], rest[2]), {tide_f4(rest[3], rest[4], rest[5], rest[6])},
                                 tide_f3(rest[7], rest[8], rest[9])};
}

static void fill_rest(const tide_skinned_data_view *v, tide_list *transforms, const uint32_t where)
{
    // All callers establish node validity before resizing/writing the output.
    tide_list_resize(transforms, (int32_t)v->nodes, sizeof(tide_bone_transform), where);
    tide_bone_transform *flat = scratch_transforms(*transforms, v->nodes);
    tide_list_cache cache = {0};
    for (uint32_t i = 0; i < v->nodes; i++) {
        tide_bone_transform *to = flat ? flat + i : tide_list_cached_at_mut(&cache, *transforms, (int32_t)i, sizeof *to);
        if (to) *to = rest_of(v, i);
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
    const model_facts *f = known_model(data);
    if (f) return f->fingerprint;
    model_seen m;
    if (!open_model(data, &m)) return 0;
    const uint64_t hash = fingerprint_of(&m.view);
    close_model(&m);
    return hash;
}

bool tide_skinned_compatible(const tide_list a, const tide_list b)
{
    skeleton_identity first, second;
    const model_facts *fa = known_model(a);
    if (fa) {
        // Looking up b can replace the same bounded cache slot. Preserve a's
        // identity before the second lookup, rather than comparing b to itself.
        first.nodes = fa->nodes;
        memcpy(first.hash, fa->skeleton_hash, sizeof first.hash);
        const model_facts *fb = known_model(b);
        if (fb) return first.nodes == fb->nodes && memcmp(first.hash, fb->skeleton_hash, sizeof first.hash) == 0;
    }
    return read_identity(a, &first) && read_identity(b, &second) && first.nodes == second.nodes
        && memcmp(first.hash, second.hash, sizeof first.hash) == 0;
}

int32_t tide_skinned_bone_count(const tide_list data)
{
    const model_facts *f = known_model(data);
    if (f) return (int32_t)f->nodes;
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

enum { NAMED_BONE, NAMED_ANIMATION, NAMED_SHAPE };

// Names found, kept on each thread for models with a key: a rig's members
// (`bones.head`) are looked up by name each time they're read. A name is
// matched by its bytes, so a slot another name took only misses.
typedef struct name_seen {
    model_key key;
    int32_t kind, found;
    uint32_t length;
    char name[40];
} name_seen;

enum { NAMES_SEEN = 64 };
static TIDE_THREAD_LOCAL name_seen names_seen[NAMES_SEEN];

static int32_t search_name(const tide_list data, const tide_str name, const int kind);

static int32_t find_name(const tide_list data, const tide_str name, const int kind)
{
    if (!name.ptr || name.bytes <= 0) return -1;
    const model_facts *f = (size_t)name.bytes <= sizeof names_seen[0].name ? known_model(data) : NULL;
    if (!f) return search_name(data, name, kind);
    name_seen *seen = &names_seen[(tide_hash(name.ptr, (size_t)name.bytes) ^ (uint64_t)kind) & (NAMES_SEEN - 1u)];
    if (seen->kind == kind && seen->length == (uint32_t)name.bytes && memcmp(&seen->key, &f->key, sizeof f->key) == 0
        && memcmp(seen->name, name.ptr, (size_t)name.bytes) == 0) return seen->found;
    const int32_t found = search_name(data, name, kind);
    *seen = (name_seen){.key = f->key, .kind = kind, .found = found, .length = (uint32_t)name.bytes};
    memcpy(seen->name, name.ptr, (size_t)name.bytes);
    return found;
}

static int32_t search_name(const tide_list data, const tide_str name, const int kind)
{
    model_seen m;
    if (!open_model(data, &m)) return -1;
    int32_t found = -1;
    const uint32_t count = kind == NAMED_ANIMATION ? m.view.clips : kind == NAMED_SHAPE ? m.view.shapes : m.view.nodes;
    for (uint32_t i = 0; i < count; i++) {
        const char *text;
        uint32_t length;
        if (kind == NAMED_ANIMATION) {
            tide_skinned_clip_data c;
            if (!tide_skinned_data_clip(&m.view, i, &c)) continue;
            text = c.name;
            length = c.name_length;
        } else if (kind == NAMED_SHAPE) {
            tide_skinned_shape_data s;
            if (!tide_skinned_data_shape(&m.view, i, &s)) continue;
            text = s.name;
            length = s.name_length;
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

int32_t tide_skinned_find_bone(const tide_list data, const tide_str name) { return find_name(data, name, NAMED_BONE); }
int32_t tide_skinned_find_animation(const tide_list data, const tide_str name) { return find_name(data, name, NAMED_ANIMATION); }
int32_t tide_skinned_find_shape(const tide_list data, const tide_str name) { return find_name(data, name, NAMED_SHAPE); }

static tide_str read_name(const tide_list data, const int32_t index, const int kind)
{
    if (index < 0) return TIDE_STR_EMPTY;
    model_seen m;
    if (!open_model(data, &m)) return TIDE_STR_EMPTY;
    const char *text = NULL;
    uint32_t length = 0;
    if (kind == NAMED_ANIMATION) {
        tide_skinned_clip_data c;
        if (tide_skinned_data_clip(&m.view, (uint32_t)index, &c)) { text = c.name; length = c.name_length; }
    } else if (kind == NAMED_SHAPE) {
        tide_skinned_shape_data s;
        if (tide_skinned_data_shape(&m.view, (uint32_t)index, &s)) { text = s.name; length = s.name_length; }
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

tide_str tide_skinned_bone_name(const tide_list data, const int32_t bone) { return read_name(data, bone, NAMED_BONE); }
tide_str tide_skinned_animation_name(const tide_list data, const int32_t animation) { return read_name(data, animation, NAMED_ANIMATION); }
tide_str tide_skinned_shape_name(const tide_list data, const int32_t shape) { return read_name(data, shape, NAMED_SHAPE); }

int32_t tide_skinned_shape_count(const tide_list data)
{
    model_seen m;
    if (!open_model(data, &m)) return 0;
    const int32_t count = m.view.shapes > INT32_MAX ? INT32_MAX : (int32_t)m.view.shapes;
    close_model(&m);
    return count;
}

float tide_skinned_shape_weight(const tide_list data, const int32_t shape)
{
    if (shape < 0) return 0.0f;
    model_seen m;
    if (!open_model(data, &m)) return 0.0f;
    tide_skinned_shape_data s;
    const float weight = tide_skinned_data_shape(&m.view, (uint32_t)shape, &s) ? s.weight : 0.0f;
    close_model(&m);
    return weight;
}

tide_list tide_skinned_shaped(const tide_list data, const tide_list weights, const tide_list vertices, const uint32_t vertex_size,
                              const int32_t position_at, const int32_t normal_at, const int32_t tangent_at)
{
    model_seen m;
    if (!open_model(data, &m)) return vertices;
    const int32_t places[3] = {position_at, normal_at, tangent_at};
    bool fits = vertex_size != 0u && tide_list_count(vertices) == (int32_t)m.view.vertices && m.view.vertices <= INT32_MAX;
    for (uint32_t a = 0; a < 3u; a++) fits = fits && (places[a] < 0 || (uint64_t)places[a] + 12u <= vertex_size);
    const int32_t given = tide_list_count(weights);
    tide_list out = vertices;
    bool copied = false;
    for (uint32_t i = 0; fits && i < m.view.shapes; i++) {
        tide_skinned_shape_data shape;
        if (!tide_skinned_data_shape(&m.view, i, &shape)) break;
        const float *weight = (int64_t)i < given ? tide_list_at(weights, (int32_t)i, sizeof(float)) : NULL;
        const float by = weight && tide_is_finite_f(*weight) ? *weight - shape.weight : 0.0f;
        if (by == 0.0f || !tide_is_finite_f(by)) continue;
        for (uint32_t k = 0; k < shape.deltas; k++) {
            tide_skinned_shape_delta_data delta;
            if (!tide_skinned_data_shape_delta(&m.view, shape.first_delta + k, &delta)) continue;
            if (!copied) {
                // A copy of an asset's or the pack's vertices shares them until it's prepared
                out = tide_list_copy(vertices, vertex_size);
                tide_list_prepare(&out, vertex_size, TIDE_IN_SCRATCH);
                copied = true;
            }
            uint8_t *vertex = tide_list_at_mut(out, (int32_t)delta.vertex, vertex_size);
            if (!vertex) continue;
            const float *moves[3] = {delta.position, delta.normal, delta.tangent};
            for (uint32_t a = 0; a < 3u; a++) {
                if (places[a] < 0) continue;
                float value[3];
                memcpy(value, vertex + places[a], sizeof value);
                for (uint32_t c = 0; c < 3u; c++) {
                    const float moved = value[c] + by * moves[a][c];
                    if (tide_is_finite_f(moved)) value[c] = moved;
                }
                memcpy(vertex + places[a], value, sizeof value);
            }
        }
    }
    close_model(&m);
    return out;
}

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
    // Both callers obtain c through data_channel, which checks the complete
    // descriptor and key spans. Scan their contents once rather than repeating
    // those checks and constructing three temporary vectors for every key.
    if (!c->keys) return false;
    const uint8_t *times = v->data + c->times_at;
    const float first = validated_number(times);
    if (!tide_is_finite_f(first) || !(first >= 0.0f)) return false;
    // Independent adjacent comparisons and a reduction let native/web
    // compilers vectorize the complete scan. Keep float comparisons so -0
    // remains a valid first key, while either ordering of +/-0 is rejected.
    uint32_t invalid = 0u;
    for (uint32_t i = 1u; i < c->keys; i++) {
        const uint32_t bits = validated_word(times + (size_t)i * 4u);
        const float time = validated_number(times + (size_t)i * 4u);
        const float previous = validated_number(times + (size_t)(i - 1u) * 4u);
        invalid |= (uint32_t)((bits & 0x7f800000u) == 0x7f800000u);
        invalid |= (uint32_t)!(time > previous);
    }
    const uint32_t width = c->path == TIDE_SKINNED_ROTATION ? 4u : 3u;
    const uint32_t copies = c->interpolation == TIDE_SKINNED_CUBICSPLINE ? 3u : 1u;
    const uint64_t words = (uint64_t)c->keys * width * copies;
    const uint8_t *values = v->data + c->values_at;
    for (uint64_t i = 0; i < words; i++) {
        invalid |= (uint32_t)((validated_word(values + (size_t)i * 4u) & 0x7f800000u) == 0x7f800000u);
    }
    return invalid == 0u;
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

// Where a clock falls among a channel's keys, from sampled_interval's low:
// the same for every channel whose key times are the same bytes.
typedef struct key_clock {
    uint32_t before, low;
    bool held; // Before the first key or past the last: the value of the key before
    float span, t;
} key_clock;

static key_clock clock_of(const tide_skinned_data_view *v, const tide_skinned_channel_data *c, const float seconds,
                          const uint32_t low)
{
    key_clock k = {low ? low - 1u : 0u, low, low == 0u || low == c->keys, 0.0f, 0.0f};
    if (k.held) return k;
    const float first_time = validated_time(v, c, k.before);
    k.span = validated_time(v, c, low) - first_time;
    k.t = tide_saturate_f((seconds - first_time) / k.span);
    return k;
}

// A key's value or tangent: three lanes and a zero, or a rotation's four.
// Fixed widths keep the decoding to loads, without library calls.
static tide_float4 key_value(const uint8_t *p, const bool rotation)
{
    return tide_f4(validated_number(p), validated_number(p + 4u), validated_number(p + 8u),
                   rotation ? validated_number(p + 12u) : 0.0f);
}

// tide_slerp_q, except that its nlerp branch leaves the normalizing to the
// caller (one more, normalize says).
static tide_float4 linear_rotation(const tide_float4 a, const tide_float4 b, const float t, uint32_t *normalize)
{
    float dt = tide_dot_q((tide_quaternion){a}, (tide_quaternion){b});
    tide_float4 bv = b;
    if (dt < 0.0f) {
        dt = -dt;
        bv = tide_neg_f4(bv);
    }
    if (dt < 0.9995f) return tide_slerp_q((tide_quaternion){a}, (tide_quaternion){b}, t).value;
    // tide_nlerp_q(a, bv, t)
    const tide_float4 nv = tide_dot_q((tide_quaternion){a}, (tide_quaternion){bv}) < 0.0f ? tide_neg_f4(bv) : bv;
    ++*normalize;
    return tide_lerp_f4(a, nv, tide_f4_splat(t));
}

// Only the values interpolation reads are decoded. A rotation is normalized
// `normalize` more times (tide_normalize_q) to finish it.
static inline __attribute__((always_inline)) tide_float4 sampled_value(const tide_skinned_data_view *v,
                                 const tide_skinned_channel_data *c, const key_clock *k, uint32_t *normalize)
{
    const bool rotation = c->path == TIDE_SKINNED_ROTATION, cubic = c->interpolation == TIDE_SKINNED_CUBICSPLINE;
    const uint32_t width = rotation ? 16u : 12u;
    const size_t key = cubic ? width * 3u : width;
    const uint8_t *values = v->data + c->values_at + (cubic ? width : 0u);
    *normalize = 0;
    const tide_float4 a = key_value(values + k->before * key, rotation);
    if (k->held || c->interpolation == TIDE_SKINNED_STEP) return a;
    const tide_float4 b = key_value(values + k->low * key, rotation);
    const float span = k->span, t = k->t;
    if (!cubic) return rotation ? linear_rotation(a, b, t, normalize) : tide_lerp_f4(a, b, tide_f4_splat(t));
    const tide_float4 out = key_value(values + k->before * key + width, rotation);
    const tide_float4 in = key_value(values + k->low * key - width, rotation);
    const float first[4] = {a.x, a.y, a.z, a.w}, first_out[4] = {out.x, out.y, out.z, out.w};
    const float value[4] = {b.x, b.y, b.z, b.w}, before[4] = {in.x, in.y, in.z, in.w};
    const float t2 = t * t, t3 = t2 * t;
    const float h0 = (2.0f * t3 - 3.0f * t2) + 1.0f, h1 = (t3 - 2.0f * t2) + t;
    const float h2 = -2.0f * t3 + 3.0f * t2, h3 = t3 - t2;
    float parts[4];
    for (uint32_t i = 0; i < 4u; i++) parts[i] = ((h0 * first[i] + h1 * span * first_out[i]) + h2 * value[i]) + h3 * span * before[i];
    *normalize = rotation;
    return tide_f4(parts[0], parts[1], parts[2], parts[3]);
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
        validated_node(v, i, &n);
        if (!n.name_length) continue;
        const uint64_t hash = tide_hash(n.name, n.name_length);
        uint32_t at = (uint32_t)hash & names.mask;
        while (names.slots[at].index >= 0) {
            tide_skinned_node_data previous;
            validated_node(v, (uint32_t)names.slots[at].index, &previous);
            if (names.slots[at].hash == hash && same_name(&n, &previous)) break;
            at = (at + 1u) & names.mask;
        }
        if (names.slots[at].index >= 0) names.slots[at].ambiguous = true;
        else names.slots[at] = (named_node){hash, (int32_t)i, false};
    }
    return names;
}

// The node named like n, flagged ambiguous when several are; NULL for none.
static const named_node *named_slot(const tide_skinned_data_view *v, const node_names names, const tide_skinned_node_data *n)
{
    if (!n->name_length) return NULL;
    const uint64_t hash = tide_hash(n->name, n->name_length);
    uint32_t at = (uint32_t)hash & names.mask;
    while (names.slots[at].index >= 0) {
        tide_skinned_node_data found;
        validated_node(v, (uint32_t)names.slots[at].index, &found);
        if (names.slots[at].hash == hash && same_name(n, &found)) return &names.slots[at];
        at = (at + 1u) & names.mask;
    }
    return NULL;
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
    if (index == 0u) return false;
    tide_skinned_node_data n;
    validated_node(v, index, &n);
    const tide_bone_transform identity = {tide_f3(0, 0, 0), {tide_f4(0, 0, 0, 1)}, tide_f3(1, 1, 1)};
    return n.parent == 0 && n.name_length == 0u
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

// Exporters such as Blender write the armature object as a named node above
// the skeleton's top bone, carrying the object's transform (a centimetre rig's
// 0.01 scale, say). A named top-level node with no node of its name on the
// other side is folded: its transform moves into its bound children's
// parent-local transforms, so their model-space results stay as authored.
// A source node the model has no node of its name for is skipped: a clip may
// animate nodes a model lacks (IK helpers, another part's bones), and their
// channels are left out, as Unity does.
enum { BIND_WRAPPER = -2, BIND_FOLDED = -3, BIND_SKIPPED = -4 };

typedef struct node_binding {
    int32_t *map;   // Source node to target node, BIND_WRAPPER, BIND_FOLDED or BIND_SKIPPED.
    int32_t *outer; // A bound source node's folded source parent, or -1.
    int32_t *inner; // A bound source node's folded target parent, or -1.
    bool folds;
} node_binding;

static bool top_level(const tide_skinned_data_view *v, const tide_skinned_node_data *n)
{
    return n->parent == 0 || (n->parent > 0 && root_wrapper(v, (uint32_t)n->parent));
}

// Folding a uniform scale keeps every composed transform an exact TRS.
static bool uniform_scale(const tide_float3 s)
{
    const float size = s.x < 0.0f ? -s.x : s.x, y = s.y - s.x, z = s.z - s.x;
    const float tolerance = 1e-5f * size;
    return (y < 0.0f ? -y : y) <= tolerance && (z < 0.0f ? -z : z) <= tolerance;
}

static bool target_fold(const tide_skinned_data_view *target, const uint32_t index,
                        const tide_skinned_data_view *source, const node_names source_names)
{
    tide_skinned_node_data n;
    validated_node(target, index, &n);
    return !root_wrapper(target, index) && top_level(target, &n) && n.name_length && !named_slot(source, source_names, &n)
        && n.scale[0] != 0.0f && uniform_scale(local_of(&n).scale);
}

// outer * inner, as one parent-local transform.
static tide_bone_transform fold_into(const tide_bone_transform outer, const tide_bone_transform inner)
{
    return (tide_bone_transform){
        tide_add_f3(outer.position, tide_rotate_q(outer.rotation, tide_mul_f3(outer.scale, inner.position))),
        tide_normalize_q(tide_mul_q(outer.rotation, inner.rotation)),
        tide_mul_f3(outer.scale, inner.scale)};
}

// inverse(outer) * inner, for a target node below a folded target parent.
static tide_bone_transform fold_out(const tide_bone_transform outer, const tide_bone_transform inner)
{
    const tide_quaternion back = tide_conjugate_q(tide_normalize_q(outer.rotation));
    return (tide_bone_transform){
        tide_div_f3(tide_rotate_q(back, tide_sub_f3(inner.position, outer.position)), outer.scale),
        tide_normalize_q(tide_mul_q(back, inner.rotation)),
        tide_div_f3(inner.scale, outer.scale)};
}

static tide_bone_transform bound_local(const node_binding *b, const tide_skinned_data_view *target, const uint32_t node,
                                       const tide_bone_transform *source_locals, tide_bone_transform local)
{
    if (b->outer[node] >= 0) local = fold_into(source_locals[b->outer[node]], local);
    if (b->inner[node] >= 0) {
        tide_skinned_node_data n;
        validated_node(target, (uint32_t)b->inner[node], &n);
        local = fold_out(local_of(&n), local);
    }
    return local;
}

#define NODE_NAME(n) (int)((n).name_length > 80u ? 80u : (n).name_length), (n).name

static void parent_name(const tide_skinned_data_view *v, const int32_t parent, char *out, const size_t size)
{
    tide_skinned_node_data p;
    if (parent > 0) validated_node(v, (uint32_t)parent, &p);
    if (parent > 0 && p.name_length) snprintf(out, size, "\"%.*s\"", NODE_NAME(p));
    else snprintf(out, size, "%s", parent > 0 ? "an unnamed node" : "nothing");
}

// Map required source nodes to target nodes. On failure, why says which
// node differs, unless data was invalid.
static bool bind_nodes(const tide_skinned_data_view *target, const tide_skinned_data_view *source, uint8_t *required,
                       node_binding *out, char *why, const size_t why_size)
{
    const node_names target_names = index_names(target), source_names = index_names(source);
    int32_t *map = tide_scratch_memory((size_t)source->nodes * 3u * sizeof *map);
    for (size_t i = 0; i < (size_t)source->nodes * 3u; i++) map[i] = -1;
    *out = (node_binding){map, map + source->nodes, map + (size_t)source->nodes * 2u, false};
    map[0] = 0;
    for (uint32_t i = source->nodes; i-- > 1u;) {
        if (!required[i]) continue;
        tide_skinned_node_data n;
        validated_node(source, i, &n);
        required[n.parent] = 1u;
    }
    for (uint32_t i = 0; i < source->nodes; i++) {
        if (!required[i]) continue;
        tide_skinned_node_data n, to;
        validated_node(source, i, &n);
        if (root_wrapper(source, i)) {
            map[i] = BIND_WRAPPER; // Transparent, never copied onto the canonical root.
            continue;
        }
        if (i != 0u) {
            const named_node *own = named_slot(source, source_names, &n), *found = named_slot(target, target_names, &n);
            if (!own) {
                snprintf(why, why_size, "it moves an unnamed node, or one below it, which can't be matched by name");
                return false;
            }
            if (own->ambiguous) {
                snprintf(why, why_size, "it has more than one node named \"%.*s\"", NODE_NAME(n));
                return false;
            }
            if (!found && top_level(source, &n)) {
                map[i] = BIND_FOLDED;
                out->folds = true;
                continue;
            }
            if (found && found->ambiguous) {
                snprintf(why, why_size, "the model has more than one node named \"%.*s\"", NODE_NAME(n));
                return false;
            }
            if (!found) {
                map[i] = BIND_SKIPPED;
                continue;
            }
            map[i] = found->index;
        }
        validated_node(target, (uint32_t)map[i], &to);
        int32_t source_parent = n.parent > 0 && root_wrapper(source, (uint32_t)n.parent) ? 0 : n.parent;
        int32_t target_parent = to.parent > 0 && root_wrapper(target, (uint32_t)to.parent) ? 0 : to.parent;
        if (source_parent > 0 && map[source_parent] == BIND_FOLDED) {
            out->outer[i] = source_parent;
            out->folds = true;
            source_parent = 0;
        }
        if (source_parent == 0 && target_parent > 0 && target_fold(target, (uint32_t)target_parent, source, source_names)) {
            out->inner[i] = target_parent;
            out->folds = true;
            target_parent = 0;
        }
        if (source_parent > 0 && map[source_parent] == BIND_SKIPPED) {
            // A node both have keeps its parent chain: one below a node the
            // model lacks would be moved relative to another parent.
            char in_source[100];
            parent_name(source, n.parent, in_source, sizeof in_source);
            snprintf(why, why_size, "\"%.*s\" is below %s in it, which the model hasn't got", NODE_NAME(n), in_source);
            return false;
        }
        if (target_parent != (source_parent < 0 ? -1 : map[source_parent])) {
            char in_source[100], in_target[100];
            parent_name(source, n.parent, in_source, sizeof in_source);
            parent_name(target, to.parent, in_target, sizeof in_target);
            snprintf(why, why_size, "\"%.*s\" is below %s in it but below %s in the model", NODE_NAME(n), in_source, in_target);
            return false;
        }
    }
    // A folded node is above bound bones; one moving nothing bound is a node
    // the model lacks, skipped.
    for (uint32_t i = 0; i < source->nodes; i++) {
        if (out->outer[i] >= 0) required[out->outer[i]] = 2u;
    }
    uint32_t bound = 0, skipped = 0, first_skipped = 0;
    for (uint32_t i = 1; i < source->nodes; i++) {
        if (!required[i]) continue;
        if (map[i] == BIND_FOLDED && required[i] != 2u) map[i] = BIND_SKIPPED;
        if (map[i] == BIND_SKIPPED && !skipped++) first_skipped = i;
        if (map[i] >= 0) bound++;
    }
    // Nothing it moves is in the model: another rig altogether, not a part of one.
    if (skipped && !bound) {
        tide_skinned_node_data n;
        validated_node(source, first_skipped, &n);
        snprintf(why, why_size, "the model has none of the nodes it moves (no node named \"%.*s\", for one)", NODE_NAME(n));
        return false;
    }
    return true;
}

// A folded node's default and every key of its scale channels stay uniform.
static bool clip_folds_uniform(const tide_skinned_data_view *v, const uint32_t index, const node_binding *b,
                               const uint8_t *required, char *why, const size_t why_size)
{
    tide_skinned_clip_data clip;
    if (!tide_skinned_data_clip(v, index, &clip)) return false;
    for (uint32_t i = 0; i < v->nodes; i++) {
        tide_skinned_node_data n;
        if (!required[i] || b->map[i] != BIND_FOLDED) continue;
        validated_node(v, i, &n);
        bool uniform = uniform_scale(local_of(&n).scale);
        for (uint32_t c = 0; uniform && c < clip.channels; c++) {
            tide_skinned_channel_data channel;
            if (!tide_skinned_data_channel(v, clip.first_channel + c, &channel)) return false;
            if (channel.node != i || channel.path != TIDE_SKINNED_SCALE) continue;
            for (uint32_t k = 0; uniform && k < channel.keys; k++) {
                float time, value[4], in[4], out[4];
                if (!tide_skinned_data_key(v, &channel, k, &time, value, in, out)) return false;
                uniform = uniform_scale(tide_f3(value[0], value[1], value[2]))
                    && (channel.interpolation != TIDE_SKINNED_CUBICSPLINE
                        || (in[0] == in[1] && in[0] == in[2] && out[0] == out[1] && out[0] == out[2]));
            }
        }
        if (!uniform) {
            snprintf(why, why_size, "\"%.*s\" above its bones scales unevenly, so it can't be folded into them", NODE_NAME(n));
            return false;
        }
    }
    return true;
}

// Say once, for each pair of models and clip, why a binding failed: the
// result is otherwise a pose left as it was, or a draw that doesn't happen.
static void report_binding(const tide_skinned_data_view *target, const tide_skinned_data_view *source,
                           const int32_t clip, const char *why)
{
    static _Atomic uint64_t reported[64];
    if (!why[0]) return;
    uint8_t identity[68];
    memcpy(identity, target->skeleton_hash, 32u);
    memcpy(identity + 32, source->skeleton_hash, 32u);
    memcpy(identity + 64, &clip, 4u);
    const uint64_t key = tide_hash(identity, sizeof identity) | 1u;
    for (uint32_t k = 0;; k++) {
        if (k == 64u) return;
        uint64_t seen = 0u;
        if (atomic_compare_exchange_strong(&reported[(key + k) & 63u], &seen, key)) break;
        if (seen == key) return;
    }
    char text[640];
    tide_skinned_clip_data c;
    if (clip >= 0 && tide_skinned_data_clip(source, (uint32_t)clip, &c)) {
        snprintf(text, sizeof text, "tide: animation \"%.*s\" doesn't bind to this SkinnedMesh, so sampling it leaves the pose"
                 " unchanged: %s. Export the clip with the model's bone names and hierarchy.",
                 (int)(c.name_length > 80u ? 80u : c.name_length), c.name, why);
    } else {
        snprintf(text, sizeof text, "tide: a pose of another model doesn't bind to this SkinnedMesh, so it isn't drawn: %s."
                 " Export the animation with the model's bone names and hierarchy.", why);
    }
    tide_debug_log(text);
}

// What sampling a clip onto a pose does, decided from the models' bytes
// alone: whether it binds, which output node each channel writes and the
// pose before any channel. Applying one only searches and interpolates, with
// the expressions and channel order of the samplers before plans, so a kept
// plan gives the same bits as one made for a single call.
#define PLAN_SEARCH UINT32_MAX // A channel searching its own key times

typedef struct plan_channel {
    tide_skinned_channel_data channel;
    int32_t to;     // An output node, or -1 - a folded node's slot
    uint32_t group; // Channels whose key times are the same bytes share one search
} plan_channel;

typedef struct plan_fold {
    int32_t to, outer; // The output node; the folded slot composed above it, or -1
    bool inner;        // Divide rest, a folded target parent's, out of it
    tide_bone_transform rest;
} plan_fold;

enum { PLAN_OWN = 1, PLAN_INTO };

typedef struct plan_key {
    model_key target, source;
    int32_t animation;
    uint32_t mode;
} plan_key;

typedef struct sample_plan {
    plan_key key;
    _Atomic uint32_t refs;
    uint64_t used;
    bool valid;
    uint32_t target_size, source_size; // The opened views' sizes, checked again on use
    float length;
    uint32_t nodes, channels, folded, folds, groups;
    tide_bone_transform *initial, *folded_initial;
    plan_channel *channel;
    plan_fold *fold;
} sample_plan;

// tide_skinned_sample's checks, then its channels onto the model's own nodes.
static bool plan_own(const tide_list data, const tide_skinned_data_view *v, const int32_t animation, sample_plan *p)
{
    bool nonnegative = false;
    const bool packed = tide_pack_skinned_sample_proof(data, &nonnegative);
    tide_skinned_clip_data clip;
    bool valid = (packed || nodes_valid(v)) && tide_skinned_data_clip(v, (uint32_t)animation, &clip);
    for (uint32_t i = 0; valid && !(packed && nonnegative) && i < clip.channels; i++) {
        tide_skinned_channel_data channel;
        if (packed) {
            validated_channel(v, clip.first_channel + i, &channel);
            // The ordinary proof permits first times in (-1, 0). Without
            // the stronger load-time fact, check this selected clip only.
            valid = validated_time(v, &channel, 0u) >= 0.0f;
        } else valid = tide_skinned_data_channel(v, clip.first_channel + i, &channel) && channel_valid(v, &channel);
    }
    if (!valid) return false;
    p->nodes = v->nodes;
    p->initial = tide_scratch_memory((size_t)v->nodes * sizeof *p->initial);
    for (uint32_t i = 0; i < v->nodes; i++) p->initial[i] = rest_of(v, i);
    p->length = clip.length;
    p->channel = tide_scratch_memory((size_t)clip.channels * sizeof *p->channel + 1u);
    for (uint32_t i = 0; i < clip.channels; i++) {
        plan_channel *c = &p->channel[p->channels++];
        validated_channel(v, clip.first_channel + i, &c->channel);
        c->to = (int32_t)c->channel.node;
        c->group = PLAN_SEARCH;
    }
    return true;
}

// Separate-file binding (bind_nodes): required source nodes onto the target's.
static bool plan_bound(const tide_skinned_data_view *target, const tide_skinned_data_view *source, const int32_t animation,
                       sample_plan *p)
{
    char why[400] = "";
    uint8_t *required = tide_scratch_memory(source->nodes);
    memset(required, 0, source->nodes);
    node_binding binding = {0};
    const bool valid = require_clip(source, (uint32_t)animation, required, true)
        && bind_nodes(target, source, required, &binding, why, sizeof why)
        && (!binding.folds || clip_folds_uniform(source, (uint32_t)animation, &binding, required, why, sizeof why));
    if (!valid) {
        report_binding(target, source, animation, why);
        return false;
    }
    p->nodes = target->nodes;
    p->initial = tide_scratch_memory((size_t)target->nodes * sizeof *p->initial);
    for (uint32_t i = 0; i < target->nodes; i++) p->initial[i] = rest_of(target, i);
    // Separate exports can store a take's starting pose as their defaults.
    // Required ancestors must follow those source locals too, just as when
    // binding an already sampled pose. Unrelated target nodes stay at rest.
    int32_t *slot = binding.folds ? tide_scratch_memory((size_t)source->nodes * sizeof *slot) : NULL;
    p->folded_initial = binding.folds ? tide_scratch_memory((size_t)source->nodes * sizeof *p->folded_initial) : NULL;
    for (uint32_t i = 0; i < source->nodes; i++) {
        if (!required[i] || (binding.map[i] < 0 && binding.map[i] != BIND_FOLDED)) continue;
        tide_skinned_node_data node;
        validated_node(source, i, &node);
        if (binding.map[i] == BIND_FOLDED) {
            slot[i] = (int32_t)p->folded;
            p->folded_initial[p->folded++] = local_of(&node);
        } else p->initial[binding.map[i]] = local_of(&node);
    }
    tide_skinned_clip_data clip;
    (void)tide_skinned_data_clip(source, (uint32_t)animation, &clip);
    p->length = clip.length;
    p->channel = tide_scratch_memory((size_t)clip.channels * sizeof *p->channel + 1u);
    for (uint32_t i = 0; i < clip.channels; i++) {
        tide_skinned_channel_data channel;
        (void)tide_skinned_data_channel(source, clip.first_channel + i, &channel);
        const int32_t to = binding.map[channel.node];
        if (to < 0 && to != BIND_FOLDED) continue;
        p->channel[p->channels++] = (plan_channel){channel, to == BIND_FOLDED ? -1 - slot[channel.node] : to, PLAN_SEARCH};
    }
    p->fold = binding.folds ? tide_scratch_memory((size_t)source->nodes * sizeof *p->fold) : NULL;
    for (uint32_t i = 0; binding.folds && i < source->nodes; i++) {
        if (!required[i] || binding.map[i] < 0 || (binding.outer[i] < 0 && binding.inner[i] < 0)) continue;
        plan_fold *f = &p->fold[p->folds++];
        memset(f, 0, sizeof *f);
        f->to = binding.map[i];
        f->outer = binding.outer[i] >= 0 ? slot[binding.outer[i]] : -1;
        f->inner = binding.inner[i] >= 0;
        if (f->inner) {
            tide_skinned_node_data n;
            validated_node(target, (uint32_t)binding.inner[i], &n);
            f->rest = local_of(&n);
        }
    }
    return true;
}

// A single-key channel always writes the same value, including its final
// rotation normalization. Bake unique writers into the initial pose once;
// duplicate node/path channels keep their original per-call write order.
static __attribute__((noinline)) void plan_constants(sample_plan *p, const tide_skinned_data_view *source)
{
    bool any = false;
    for (uint32_t i = 0; i < p->channels; i++) any |= p->channel[i].channel.keys == 1u;
    if (!any) return;
    const size_t slots = ((size_t)p->nodes + p->folded) * 3u;
    uint32_t *writers = tide_scratch_memory(slots * sizeof *writers);
    memset(writers, 0, slots * sizeof *writers);
    for (uint32_t i = 0; i < p->channels; i++) {
        const plan_channel *c = &p->channel[i];
        const uint32_t node = c->to < 0 ? p->nodes + (uint32_t)(-1 - c->to) : (uint32_t)c->to;
        writers[(size_t)node * 3u + c->channel.path]++;
    }
    uint32_t count = 0;
    for (uint32_t i = 0; i < p->channels; i++) {
        const plan_channel c = p->channel[i];
        const uint32_t node = c.to < 0 ? p->nodes + (uint32_t)(-1 - c.to) : (uint32_t)c.to;
        if (c.channel.keys != 1u || writers[(size_t)node * 3u + c.channel.path] != 1u) {
            p->channel[count++] = c;
            continue;
        }
        const key_clock k = {.held = true};
        uint32_t more;
        tide_float4 value = sampled_value(source, &c.channel, &k, &more);
        const uint32_t normalize = c.channel.path == TIDE_SKINNED_ROTATION ? more + 1u : 0u;
        for (uint32_t n = 0; n < normalize; n++) value = tide_normalize_q((tide_quaternion){value}).value;
        tide_bone_transform *to = c.to < 0 ? &p->folded_initial[-1 - c.to] : &p->initial[c.to];
        if (c.channel.path == TIDE_SKINNED_TRANSLATION) to->position = tide_f3(value.x, value.y, value.z);
        else if (c.channel.path == TIDE_SKINNED_SCALE) to->scale = tide_f3(value.x, value.y, value.z);
        else to->rotation = (tide_quaternion){value};
    }
    p->channels = count;
}

// A scratch plan in one allocation of its own.
static sample_plan *plan_keep(const sample_plan *p)
{
    const size_t initial = (size_t)p->nodes * sizeof *p->initial, folded = (size_t)p->folded * sizeof *p->folded_initial;
    const size_t channels = (size_t)p->channels * sizeof *p->channel, folds = (size_t)p->folds * sizeof *p->fold;
    _Static_assert(sizeof(sample_plan) % 8u == 0u && _Alignof(tide_bone_transform) <= 8u && sizeof(tide_bone_transform) % 4u == 0u
                   && _Alignof(plan_channel) <= 4u && _Alignof(plan_fold) <= 4u, "a kept plan's tables follow each other");
    uint8_t *at = tide_alloc(sizeof *p + initial + folded + channels + folds);
    sample_plan *kept = (sample_plan *)(void *)at;
    memcpy(kept, p, sizeof *p);
    at += sizeof *p;
    kept->initial = (tide_bone_transform *)(void *)at;
    if (initial) memcpy(kept->initial, p->initial, initial);
    kept->folded_initial = (tide_bone_transform *)(void *)(at += initial);
    if (folded) memcpy(kept->folded_initial, p->folded_initial, folded);
    kept->channel = (plan_channel *)(void *)(at += folded);
    if (channels) memcpy(kept->channel, p->channel, channels);
    kept->fold = (plan_fold *)(void *)(at + channels);
    if (folds) memcpy(kept->fold, p->fold, folds);
    return kept;
}

// Channels of a kept plan whose key times are the same bytes search once:
// a 60 Hz resampled clip's channels mostly share one time track.
static void plan_groups(sample_plan *p, const tide_skinned_data_view *source)
{
    uint32_t *first = tide_scratch_memory((size_t)p->channels * sizeof *first + 4u);
    uint64_t *hashes = tide_scratch_memory((size_t)p->channels * sizeof *hashes + 8u);
    for (uint32_t i = 0; i < p->channels; i++) {
        plan_channel *c = &p->channel[i];
        const uint8_t *times = source->data + c->channel.times_at;
        const size_t bytes = (size_t)c->channel.keys * 4u;
        const uint64_t hash = tide_hash(times, bytes);
        uint32_t g = 0;
        for (; g < p->groups; g++) {
            const tide_skinned_channel_data *other = &p->channel[first[g]].channel;
            if (hashes[g] == hash && other->keys == c->channel.keys && memcmp(source->data + other->times_at, times, bytes) == 0) break;
        }
        if (g == p->groups) {
            first[g] = i;
            hashes[g] = hash;
            p->groups++;
        }
        c->group = g;
    }
}

static void plan_apply(const sample_plan *p, const tide_skinned_data_view *source, const float seconds,
                       tide_list *transforms, const uint32_t where)
{
    tide_list_resize(transforms, (int32_t)p->nodes, sizeof(tide_bone_transform), where);
    const uint32_t mark = tide_scratch_mark();
    tide_bone_transform *flat = scratch_transforms(*transforms, p->nodes);
    tide_list_cache cache = {0};
    if (flat) memcpy(flat, p->initial, (size_t)p->nodes * sizeof *flat);
    for (uint32_t i = 0; !flat && i < p->nodes; i++) {
        tide_bone_transform *to = tide_list_cached_at_mut(&cache, *transforms, (int32_t)i, sizeof *to);
        if (to) *to = p->initial[i];
    }
    tide_bone_transform *folded = p->folded ? tide_scratch_memory((size_t)p->folded * sizeof *folded) : NULL;
    if (folded) memcpy(folded, p->folded_initial, (size_t)p->folded * sizeof *folded);
    key_clock few[32];
    key_clock *clocks = p->groups <= 32u ? few : tide_scratch_memory((size_t)p->groups * sizeof *clocks);
    for (uint32_t g = 0; g < p->groups; g++) clocks[g].low = PLAN_SEARCH;
    const float clock = tide_clamp_f(seconds, 0.0f, p->length);
    sample_intervals intervals = {0};
    // Every channel's value, then the rotations' normalizing in a loop of its
    // own, where different channels' square roots and divisions overlap,
    // then the writes in channel order.
    tide_float4 few_values[192];
    uint8_t few_counts[192];
    tide_float4 *values = p->channels <= 192u ? few_values : tide_scratch_memory((size_t)p->channels * sizeof *values);
    uint8_t *normalize = p->channels <= 192u ? few_counts : tide_scratch_memory(p->channels);
    for (uint32_t i = 0; i < p->channels; i++) {
        const plan_channel *c = &p->channel[i];
        key_clock alone;
        const key_clock *k = &alone;
        if (c->group == PLAN_SEARCH) alone = clock_of(source, &c->channel, clock, sampled_interval(source, &c->channel, clock, &intervals));
        else {
            k = &clocks[c->group];
            if (k->low == PLAN_SEARCH) clocks[c->group] = clock_of(source, &c->channel, clock, sampled_interval(source, &c->channel, clock, NULL));
        }
        uint32_t more;
        values[i] = sampled_value(source, &c->channel, k, &more);
        // A pose's rotation is normalized once more as it's written.
        normalize[i] = (uint8_t)(c->channel.path == TIDE_SKINNED_ROTATION ? more + 1u : 0u);
    }
    for (uint32_t i = 0; i < p->channels; i++) {
        for (uint32_t n = 0; n < normalize[i]; n++) values[i] = tide_normalize_q((tide_quaternion){values[i]}).value;
    }
    for (uint32_t i = 0; i < p->channels; i++) {
        const plan_channel *c = &p->channel[i];
        tide_bone_transform *to = c->to < 0 ? &folded[-1 - c->to]
            : flat ? flat + c->to : tide_list_cached_at_mut(&cache, *transforms, c->to, sizeof *to);
        if (!to) continue;
        const tide_float4 value = values[i];
        if (c->channel.path == TIDE_SKINNED_TRANSLATION) to->position = tide_f3(value.x, value.y, value.z);
        else if (c->channel.path == TIDE_SKINNED_SCALE) to->scale = tide_f3(value.x, value.y, value.z);
        else to->rotation = (tide_quaternion){value};
    }
    for (uint32_t i = 0; i < p->folds; i++) {
        const plan_fold *f = &p->fold[i];
        tide_bone_transform *to = flat ? flat + f->to : tide_list_cached_at_mut(&cache, *transforms, f->to, sizeof *to);
        if (!to) continue;
        tide_bone_transform local = *to;
        if (f->outer >= 0) local = fold_into(folded[f->outer], local);
        if (f->inner) local = fold_out(f->rest, local);
        *to = local;
    }
    // Flat scratch storage was sized before the mark, so only temporary
    // sampling arrays follow it. Other scratch writes may copy shared chunks.
    if (flat || where != TIDE_IN_SCRATCH) tide_scratch_reset(mark);
}

// Kept plans, shared by every thread: a few ways for each set, the least
// recently used replaced. A plan stays alive while a caller holds it.
enum { PLAN_SETS = 64, PLAN_WAYS = 4 };
static sample_plan *plans[PLAN_SETS][PLAN_WAYS];
static uint64_t plans_used;
static atomic_flag plans_lock = ATOMIC_FLAG_INIT;

static void plan_release(sample_plan *p)
{
    if (p && atomic_fetch_sub_explicit(&p->refs, 1u, memory_order_acq_rel) == 1u) free(p);
}

static sample_plan **plan_set(const plan_key *key)
{
    return plans[tide_hash(key, sizeof *key) & (PLAN_SETS - 1u)];
}

static sample_plan *plan_find(const plan_key *key)
{
    sample_plan **set = plan_set(key);
    sample_plan *found = NULL;
    while (atomic_flag_test_and_set_explicit(&plans_lock, memory_order_acquire)) {}
    for (uint32_t w = 0; w < PLAN_WAYS && !found; w++) {
        if (!set[w] || memcmp(&set[w]->key, key, sizeof *key) != 0) continue;
        found = set[w];
        found->used = ++plans_used;
        atomic_fetch_add_explicit(&found->refs, 1u, memory_order_relaxed);
    }
    atomic_flag_clear_explicit(&plans_lock, memory_order_release);
    return found;
}

// Keeps made (held once by the caller) unless another thread kept its equal
// first, and returns the one kept, held once more for the caller.
static sample_plan *plan_share(sample_plan *made)
{
    sample_plan **set = plan_set(&made->key);
    sample_plan *evicted = NULL, *found = NULL;
    while (atomic_flag_test_and_set_explicit(&plans_lock, memory_order_acquire)) {}
    uint32_t oldest = 0;
    for (uint32_t w = 0; w < PLAN_WAYS && !found; w++) {
        if (set[w] && memcmp(&set[w]->key, &made->key, sizeof made->key) == 0) found = set[w];
        else if (set[oldest] && (!set[w] || set[w]->used < set[oldest]->used)) oldest = w;
    }
    if (found) {
        found->used = ++plans_used;
        atomic_fetch_add_explicit(&found->refs, 1u, memory_order_relaxed);
    } else {
        evicted = set[oldest];
        made->used = ++plans_used;
        atomic_fetch_add_explicit(&made->refs, 1u, memory_order_relaxed);
        set[oldest] = made;
    }
    atomic_flag_clear_explicit(&plans_lock, memory_order_release);
    plan_release(evicted);
    if (!found) return made;
    plan_release(made);
    return found;
}

// The plan for sampling source's clip onto target's pose (onto source's own
// for PLAN_OWN), held once for the caller. It's kept for later calls when
// both models' bytes have keys, and made for this call alone otherwise.
static sample_plan *plan_for(const uint32_t mode, const tide_list target_data, const model_seen *target,
                             const tide_list source_data, const model_seen *source, const int32_t animation)
{
    plan_key key;
    memset(&key, 0, sizeof key);
    key.animation = animation;
    key.mode = mode;
    const bool keyed = (mode == PLAN_OWN || key_of(target_data, target, &key.target)) && key_of(source_data, source, &key.source);
    sample_plan *p = keyed ? plan_find(&key) : NULL;
    if (p && p->target_size == target->view.size && p->source_size == source->view.size) return p;
    plan_release(p);
    const uint32_t mark = tide_scratch_mark();
    sample_plan made;
    memset(&made, 0, sizeof made);
    made.key = key;
    made.target_size = target->view.size;
    made.source_size = source->view.size;
    if (mode == PLAN_OWN) made.valid = plan_own(source_data, &source->view, animation, &made);
    else if (nodes_valid(&target->view) && nodes_valid(&source->view)) {
        // The same skeleton samples as tide_skinned_sample does, onto the clip's own nodes.
        const bool exact = target->view.nodes == source->view.nodes
            && memcmp(target->view.skeleton_hash, source->view.skeleton_hash, sizeof target->view.skeleton_hash) == 0;
        made.valid = exact ? plan_own(source_data, &source->view, animation, &made)
                           : plan_bound(&target->view, &source->view, animation, &made);
    }
    if (!made.valid) made.nodes = made.channels = made.folded = made.folds = 0;
    if (keyed && made.valid) plan_constants(&made, &source->view);
    p = plan_keep(&made);
    atomic_init(&p->refs, 1u);
    if (keyed && p->valid) plan_groups(p, &source->view);
    tide_scratch_reset(mark);
    return keyed ? plan_share(p) : p;
}

static bool plan_sample(const uint32_t mode, const tide_list target_data, const tide_list source_data, const int32_t animation,
                        const float seconds, tide_list *transforms, const uint32_t where)
{
    if (!transforms || animation < 0) return false;
    const bool own = mode == PLAN_OWN;
    model_seen target, source;
    if (!own && !open_model(target_data, &target)) return false;
    if (!open_model(source_data, &source)) {
        if (!own) close_model(&target);
        return false;
    }
    sample_plan *p = plan_for(mode, target_data, own ? &source : &target, source_data, &source, animation);
    const bool valid = p->valid;
    if (valid) plan_apply(p, &source.view, seconds, transforms, where);
    plan_release(p);
    close_model(&source);
    if (!own) close_model(&target);
    return valid;
}

bool tide_skinned_sample(const tide_list data, const int32_t animation, const float seconds,
                         tide_list *transforms, const uint32_t where)
{
    return plan_sample(PLAN_OWN, data, data, animation, seconds, transforms, where);
}

bool tide_skinned_sample_into(const tide_list target_data, const tide_list animation_data, const int32_t animation,
                              const float seconds, tide_list *transforms, const uint32_t where)
{
    return plan_sample(PLAN_INTO, target_data, animation_data, animation, seconds, transforms, where);
}

bool tide_skinned_pose_into(const tide_list target_data, const tide_list source_data, const tide_list source_transforms,
                            tide_list *transforms, const uint32_t where)
{
    if (!transforms) return false;
    model_seen target, source;
    if (!open_model(target_data, &target)) return false;
    if (!open_model(source_data, &source)) { close_model(&target); return false; }
    const uint32_t mark = tide_scratch_mark();
    char why[400] = "";
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
            validated_node(&source.view, i, &n);
            if (!same_transform(*local, local_of(&n))) {
                if (root_wrapper(&source.view, i)) { valid = false; break; }
                required[i] = 1u;
            }
        }
    }
    node_binding binding = {0};
    valid = valid && (exact || bind_nodes(&target.view, &source.view, required, &binding, why, sizeof why));
    tide_bone_transform *locals = valid && binding.folds ? tide_scratch_memory((size_t)source.view.nodes * sizeof *locals) : NULL;
    for (uint32_t i = 0; locals && valid && i < source.view.nodes; i++) {
        locals[i] = *(const tide_bone_transform *)tide_list_cached_at(&cache, source_transforms, (int32_t)i, sizeof *locals);
        if (required[i] && binding.map[i] == BIND_FOLDED && !uniform_scale(locals[i].scale)) {
            tide_skinned_node_data n;
            validated_node(&source.view, i, &n);
            snprintf(why, sizeof why, "\"%.*s\" above its bones scales unevenly, so it can't be folded into them", NODE_NAME(n));
            valid = false;
        }
    }
    if (valid) {
        // Stage the complete target before resizing: the output may alias source.
        tide_bone_transform *made = tide_scratch_memory((size_t)target.view.nodes * sizeof *made);
        for (uint32_t i = 0; i < target.view.nodes; i++) {
            tide_skinned_node_data n;
            validated_node(&target.view, i, &n);
            made[i] = local_of(&n);
        }
        for (uint32_t i = 0; i < source.view.nodes; i++) {
            if (!exact && (!required[i] || binding.map[i] < 0)) continue;
            const tide_bone_transform *local = tide_list_cached_at(&cache, source_transforms, (int32_t)i, sizeof *local);
            made[exact ? i : (uint32_t)binding.map[i]] = binding.folds ? bound_local(&binding, &target.view, i, locals, *local) : *local;
        }
        tide_list_resize(transforms, (int32_t)target.view.nodes, sizeof *made, where);
        tide_list_cache output = {0};
        for (uint32_t i = 0; i < target.view.nodes; i++) {
            tide_bone_transform *to = tide_list_cached_at_mut(&output, *transforms, (int32_t)i, sizeof *to);
            if (to) *to = made[i];
        }
    }
    else report_binding(&target.view, &source.view, -1, why);
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
    if (tide_handle_where(transforms.at) == TIDE_IN_SCRATCH) return tide_list_at(transforms, 0, sizeof(tide_bone_transform));
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

// A bone's model matrix: its ancestors' locals multiplied from the root down,
// as global_matrices does for every node (each depends only on its
// ancestors). The model's nodes are valid; false for a pose of another count.
static bool chain_model(const tide_skinned_data_view *v, const tide_list transforms, const uint32_t bone, tide_float4x4 *out)
{
    if (tide_list_count(transforms) != (int32_t)v->nodes) return false;
    uint32_t few[64], depth = 0;
    uint32_t *chain = few;
    for (uint32_t i = bone;; i = draw_word(v->data + v->node_at + (size_t)i * TIDE_SKINNED_NODE_SIZE)) {
        if (depth == 64u && chain == few) {
            chain = tide_scratch_memory((size_t)v->nodes * sizeof *chain);
            memcpy(chain, few, sizeof few);
        }
        chain[depth++] = i;
        if (i == 0u) break;
    }
    tide_list_cache cache = {0};
    tide_float4x4 global = tide_identity_f4x4();
    for (uint32_t k = depth; k-- > 0;) {
        const tide_bone_transform *local = tide_list_cached_at(&cache, transforms, (int32_t)chain[k], sizeof *local);
        if (!local) return false;
        const tide_float4x4 matrix = tide_trs_f4x4(local->position, local->rotation, local->scale);
        global = k == depth - 1u ? matrix : tide_mul_f4x4(global, matrix);
    }
    *out = global;
    return true;
}

tide_float4x4 tide_skinned_model(const tide_list data, const tide_list transforms, const int32_t bone)
{
    tide_float4x4 out = tide_identity_f4x4();
    if (bone < 0) return out;
    model_facts *f = known_model(data);
    const uint32_t mark = tide_scratch_mark();
    if (f && f->key.kind == KEY_PACK) {
        // Known bytes in the pack: no need to open them again.
        if (!f->valid) {
            model_seen m;
            if (!open_model(data, &m)) return out;
            f->valid = nodes_valid(&m.view) ? 1 : -1;
            close_model(&m);
        }
        const tide_skinned_data_view v = {.data = tide_pack_span(tide_handle_offset(data.at) + (uint32_t)sizeof(tide_block), f->size),
                                          .size = f->size, .nodes = f->nodes, .node_at = f->node_at};
        tide_float4x4 global;
        if (v.data && (uint32_t)bone < v.nodes && f->valid > 0 && chain_model(&v, transforms, (uint32_t)bone, &global)) out = global;
        tide_scratch_reset(mark);
        return out;
    }
    model_seen m;
    if (!open_model(data, &m)) return out;
    if ((uint32_t)bone < m.view.nodes) {
        if (f && !f->valid) f->valid = nodes_valid(&m.view) ? 1 : -1;
        tide_float4x4 global;
        if ((f ? f->valid > 0 : nodes_valid(&m.view)) && chain_model(&m.view, transforms, (uint32_t)bone, &global)) out = global;
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
