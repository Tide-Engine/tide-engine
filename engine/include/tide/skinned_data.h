#pragma once

// Portable, immutable skinned-model data. Every stored scalar is little
// endian; offsets are from the first byte, never native pointers. A game
// owns these bytes through its ordinary list ownership.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TIDE_SKINNED_DATA_MAGIC 0x4e4b5354u // "TSKN"
#define TIDE_SKINNED_DATA_VERSION 1u
#define TIDE_SKINNED_HEADER_SIZE 120u
#define TIDE_SKINNED_NODE_SIZE 56u
#define TIDE_SKINNED_PALETTE_SIZE 68u
#define TIDE_SKINNED_VERTEX_SIZE 80u
#define TIDE_SKINNED_INFLUENCE_SIZE 8u
#define TIDE_SKINNED_CLIP_SIZE 20u
#define TIDE_SKINNED_CHANNEL_SIZE 24u

// ---------------------------------------------------------------------------
// Limits: what a file's bytes can ask of a decode (tide_skinned_data_decode,
// below). Bytes can come from anywhere (a fetched URL, a player's file), and
// SkinnedMesh<V>.Decode runs in match code on every machine, so a small
// hostile file mustn't end the program or take minutes. Each limit is only
// its number here: change it and nothing else changes. A file past one is
// refused (InvalidData) from the counts it says, before what it asks for is
// made or done; an accessor with no buffer view (zeros, which take no bytes)
// counts as one with bytes. tidec's SkinnedMesh and Animations imports are
// held to the same limits, so what builds also decodes.
//
// At these numbers a decode's peak memory is about 500 MiB for a model at
// every limit at once, plus about 100 bytes for each JSON value and twice the
// file's own size; its work is about TIDE_SKINNED_MAX_READS numbers read, plus the file's size.

// A file's bytes, and the decoded model's: the most either is.
#define TIDE_SKINNED_MAX_BYTES (512u << 20)

// JSON values in the file's description (numbers, strings, arrays, objects,
// each one), each about 100 bytes once read. A description is small beside
// its binary data: 65,536 nodes with all their fields are under a million.
#define TIDE_SKINNED_MAX_JSON_VALUES (1u << 22)

// Nodes (the skeleton has one more, its root), skins, animations and
// buffers a file has.
#define TIDE_SKINNED_MAX_NODES 65536u
#define TIDE_SKINNED_MAX_SKINS 4096u
#define TIDE_SKINNED_MAX_CLIPS 65536u
#define TIDE_SKINNED_MAX_BUFFERS 65536u

// The decoded model's vertices (80 bytes each: 80 MiB) and triangle corners
// (4 bytes each: 24 MiB), and the elements one accessor has. A part with no
// normals has three vertices a triangle of its own.
#define TIDE_SKINNED_MAX_VERTICES (1u << 20)
#define TIDE_SKINNED_MAX_INDICES (3u << 21)

// Inverse binds (a skin's joints, and one for each part no skin moves: 68
// bytes each, 17 MiB) and vertex influences (8 bytes each, 64 MiB: eight a
// vertex at the most vertices; a part's joint and weight sets are refused
// before they're read when they'd be more).
#define TIDE_SKINNED_MAX_PALETTES (1u << 18)
#define TIDE_SKINNED_MAX_INFLUENCES (1u << 23)

// Animation channels (24 bytes each) and key numbers, times and values
// together (4 bytes each: 64 MiB, four million rotation keys).
#define TIDE_SKINNED_MAX_CHANNELS (1u << 18)
#define TIDE_SKINNED_MAX_KEY_NUMBERS (1u << 24)

// Numbers read from accessors over the whole decode: an accessor several
// parts name is read again for each, so this bounds the work, not the counts
// a file says once. A model at the vertex, index, influence and key limits
// reads about 56 million.
#define TIDE_SKINNED_MAX_READS (1u << 26)

// Header words: magic, version, byte size, attribute flags, then count/offset
// pairs for nodes, palettes, vertices, indices, clips, channels and influences.
// Words 18/19 give the strings' offset/byte size, 20/21 the key data's offset/byte
// size. Bytes 88..119 are the skeleton's SHA-256 (names, parents and rest
// transforms only). All table and key offsets are aligned to four bytes.
enum {
    TIDE_SKINNED_HAS_NORMALS = 1u,
    TIDE_SKINNED_HAS_UVS = 2u,
    TIDE_SKINNED_HAS_UVS2 = 4u,
    TIDE_SKINNED_HAS_COLORS = 8u,
    TIDE_SKINNED_HAS_TANGENTS = 16u
};

enum {
    TIDE_SKINNED_TRANSLATION,
    TIDE_SKINNED_ROTATION,
    TIDE_SKINNED_SCALE
};

enum {
    TIDE_SKINNED_STEP,
    TIDE_SKINNED_LINEAR,
    TIDE_SKINNED_CUBICSPLINE
};

typedef struct tide_skinned_data_view {
    const uint8_t *data;
    uint32_t size, flags;
    uint32_t nodes, node_at, palettes, palette_at;
    uint32_t vertices, vertex_at, indices, index_at;
    uint32_t clips, clip_at, channels, channel_at;
    uint32_t influences, influence_at;
    uint32_t string_at, string_size, key_at, key_size;
    uint8_t skeleton_hash[32];
} tide_skinned_data_view;

// Node 0 is an identity root. Every other parent precedes its child.
// source_node is its original glTF index (UINT32_MAX for the root). Names
// retain the file's spelling; they are not necessarily present or unique.
typedef struct tide_skinned_node_data {
    int32_t parent;
    uint32_t source_node;
    const char *name;
    uint32_t name_length;
    float position[3], rotation[4], scale[3];
} tide_skinned_node_data;

// Vertex influences address palettes, not nodes. The same skeleton node
// can have different inverse binds in different skins. Matrices are columns.
typedef struct tide_skinned_palette_data {
    uint32_t node;
    float inverse_bind[16];
} tide_skinned_palette_data;

typedef struct tide_skinned_vertex_data {
    float position[3], normal[3], uv[2], uv2[2], color[4], tangent[4];
    uint32_t first_influence, influences;
} tide_skinned_vertex_data;

typedef struct tide_skinned_influence_data {
    uint32_t palette;
    float weight;
} tide_skinned_influence_data;

typedef struct tide_skinned_clip_data {
    const char *name;
    uint32_t name_length, first_channel, channels;
    float length;
} tide_skinned_clip_data;

typedef struct tide_skinned_channel_data {
    uint32_t node, path, interpolation, keys;
    uint32_t times_at, values_at;
} tide_skinned_channel_data;

// Checks the header and all table spans without traversing model data.
// The getters check their records and referenced spans before reading them.
bool tide_skinned_data_open(const void *bytes, size_t size, tide_skinned_data_view *out);
bool tide_skinned_data_node(const tide_skinned_data_view *view, uint32_t index, tide_skinned_node_data *out);
bool tide_skinned_data_palette(const tide_skinned_data_view *view, uint32_t index, tide_skinned_palette_data *out);
bool tide_skinned_data_vertex(const tide_skinned_data_view *view, uint32_t index, tide_skinned_vertex_data *out);
bool tide_skinned_data_influence(const tide_skinned_data_view *view, uint32_t index, tide_skinned_influence_data *out);
bool tide_skinned_data_index(const tide_skinned_data_view *view, uint32_t index, uint32_t *out);
bool tide_skinned_data_clip(const tide_skinned_data_view *view, uint32_t index, tide_skinned_clip_data *out);
bool tide_skinned_data_channel(const tide_skinned_data_view *view, uint32_t index, tide_skinned_channel_data *out);
bool tide_skinned_data_key(const tide_skinned_data_view *view, const tide_skinned_channel_data *channel,
                           uint32_t index, float *time, float value[4], float in_tangent[4], float out_tangent[4]);
// Full structural/content validation, including parent and influence bounds,
// names, channel/key counts, finite numbers and increasing key times.
bool tide_skinned_data_validate(const void *bytes, size_t size);

// A compile-time .gltf can resolve its external buffers with this callback.
// Returned bytes remain the caller's. A runtime decode passes no callback.
typedef const uint8_t *(*tide_skinned_side_fn)(void *user, const char *path, size_t *size);

// Decode a glTF 2.0 JSON file or GLB. With runtime=true only a self-contained
// GLB is accepted, and no external resources are requested. On success the
// returned allocation belongs to the caller; free it with the function below.
// On failure both outputs are empty and why/note describe the first error.
bool tide_skinned_data_decode(const void *bytes, size_t size, bool runtime, tide_skinned_side_fn side, void *user,
                              uint8_t **out, uint32_t *out_size, char why[256], char note[256]);
void tide_skinned_data_free(uint8_t *bytes);
