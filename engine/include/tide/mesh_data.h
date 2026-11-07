#pragma once

// A static mesh read from a file's bytes: glTF 2.0 (a .glb, or a .gltf whose
// buffers are inside it or, at build time, in files beside it) and Wavefront
// OBJ. What comes out is one mesh as Tide draws one: every mesh the file
// places, where it places it, with x turned around (glTF is right-handed),
// each triangle's corners clockwise seen from its front. docs/spec.md,
// Assets, Mesh assets, says what's read of each format.
//
// tidec's `asset Mesh<V>` imports (compiler/src/mesh_files.c, which adds FBX
// through its build-time converter) and a game's Mesh<V>.Decode read with
// this same code, so a file gives the same vertices either way, and the same
// bits on every machine: arithmetic is the basic float operations in a fixed
// order, numbers are read by Tide's own reader, and colors go through the
// engine's pow.
//
// Nothing a file says is trusted to be a place: every offset and count is
// checked against the bytes there before it's used, and a file past a limit
// (tide_mesh_limits) is refused from the counts it says, before what they ask
// for is made.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct tide_skinned_data_view; // tide/skinned_data.h

// ---------------------------------------------------------------------------
// Limits of Mesh<V>.Decode: what bytes can ask of a decode. Bytes can come
// from anywhere (a fetched URL, a player's file), and Decode runs in match
// code on every machine, so a small hostile file mustn't end the program or
// take minutes. Each limit is only its number here: change it and nothing
// else changes. They're SkinnedMesh<V>.Decode's (tide/skinned_data.h).
// tidec's imports have their own, larger ones (compiler/src/mesh_files.h).
//
// At these numbers a decode's peak is about 160 MiB for a mesh at every limit
// at once (73 bytes a vertex in the mesh and as much again while a part is
// added), plus about 100 bytes for each JSON value and twice the file's own
// size; its work is about TIDE_MESH_DECODE_MAX_READS numbers read, plus the
// file's size.

// A file's bytes: the most a decode looks at.
#define TIDE_MESH_DECODE_MAX_BYTES (512u << 20)
// JSON values in a glTF's description (numbers, strings, arrays, objects).
#define TIDE_MESH_DECODE_MAX_JSON_VALUES (1u << 22)
// The mesh's vertices and triangle corners (three a triangle), and the
// elements one accessor has. A part with no normals has three vertices a
// triangle of its own.
#define TIDE_MESH_DECODE_MAX_VERTICES (1u << 20)
#define TIDE_MESH_DECODE_MAX_INDICES (3u << 21)
// Numbers read over the whole decode: a glTF's accessors (one several parts
// name counts again for each) and an OBJ's v, vt and vn lines (a color after
// a position counts, and one not written counts as three).
#define TIDE_MESH_DECODE_MAX_READS (1u << 26)

// A set of limits, each with the name of its #define. `prefix` and each
// limit's name make the define's ("TIDE_MESH_DECODE_MAX" "_VERTICES"), which
// a refusal ends with, so a report says which; `holder` says who holds to
// them ("Mesh.Decode takes").
typedef struct tide_mesh_limits {
    uint64_t bytes, json_values, vertices, indices, reads;
    const char *prefix;
    const char *holder;
    // Refuses a mesh with a number no float holds (an infinity a node's
    // transform made, and the NaN a normal then is): Decode's output is match
    // state, where NaN's bits aren't the same on every machine.
    bool finite;
    // Said on stderr before why, for a file a limit refused ("tide:
    // Mesh.Decode refused a mesh"), so a refusal is known for what it is on
    // the machine that refused it; NULL for none (tidec says it as a diagnostic).
    const char *report;
} tide_mesh_limits;

// Mesh<V>.Decode's limits, the numbers above.
extern const tide_mesh_limits tide_mesh_decode_limits;

// ---------------------------------------------------------------------------

typedef enum tide_mesh_format {
    TIDE_MESH_ANY,  // By its first bytes: GLB's magic, then JSON's '{' (after a byte order mark and spaces), else OBJ
    TIDE_MESH_GLB,
    TIDE_MESH_GLTF,
    TIDE_MESH_OBJ,
} tide_mesh_format;

typedef struct tide_mesh_data {
    uint32_t vertices;
    uint32_t indices;  // Three a triangle
    float *position;   // 3 a vertex
    float *normal;     // 3: the file's, or its triangle's own where the file has none
    float *uv;         // 2: (0, 0) at the texture's top left; zeros where the file has none
    float *uv2;        // 2: its second set, the same way
    float *color;      // 4: the vertex's color times its material's, as the screen shows it; white where the file has neither
    float *tangent;    // 4: xyz and bitangent handedness; zeros where the file has none
    uint32_t *index;   // Each under `vertices`
    bool has_normals, has_uvs, has_uvs2, has_colors, has_tangents; // What any part of the file had, for the editor
    uint32_t parts;    // How many meshes of the file it's made of: a glTF's primitives as its nodes place them, an OBJ's one
    uint8_t hash[32];  // SHA-256 of all of the above: what's in the mesh, whatever files it came from
    uint8_t *sources;  // Private import provenance per vertex: authored tangent (1), first UV (2); not hashed
} tide_mesh_data;

// A file a .gltf names beside it (its buffers'), by the path it writes: its
// bytes, which stay the caller's, or NULL. A decode at runtime has none.
typedef const uint8_t *(*tide_mesh_side_fn)(void *user, const char *path, size_t *size);

// Reads `size` bytes as a mesh within `limits`. False for bytes that aren't a
// mesh Tide reads, with why in `why` (what follows "'tree.glb' ") and, where
// there's something to do about it, what in `note` (else empty); `*past_limit`
// (when given) says whether a limit refused it. Free what it gives with
// tide_mesh_data_free; a failed read leaves nothing to free.
bool tide_mesh_data_decode(const uint8_t *data, size_t size, tide_mesh_format format, const tide_mesh_limits *limits,
                           tide_mesh_side_fn side, void *user, tide_mesh_data *out, char why[256], char note[256],
                           bool *past_limit);

// An owned copy of a mesh this read, with missing tangents generated (by UV
// derivatives, weighted by corner angle) and a vertex used with both
// handednesses split in two. Only consumers with a float4 tangent use it.
bool tide_mesh_data_with_tangents(const tide_mesh_data *source, const tide_mesh_limits *limits, tide_mesh_data *out,
                                  char why[256], char note[256]);

void tide_mesh_data_free(tide_mesh_data *m);

// Whether every number of the mesh is one a float holds.
bool tide_mesh_data_finite(const tide_mesh_data *m);

// ---------------------------------------------------------------------------
// A Mesh with bones: a static Mesh<V> whose vertex has 'joints' or 'weights'
// is read as a skinned model is (tide/skinned_data.h), for its skin, at
// build time and by Decode alike (docs/spec.md, Models).

// A skinned model's geometry as a mesh: its vertices as the model keeps them,
// in their geometry's own space (its bind space), its hash the model bytes'.
// False, `out` untouched, for bytes that aren't a model.
bool tide_mesh_data_of_model(const uint8_t *model, uint32_t size, tide_mesh_data *out);

// `m`'s vertices (the model's own geometry) at rest: each where its bones'
// rest pose puts it, in the skeleton's space, as a SkinnedMesh of the file
// draws at rest, its normal turned the same way, written into `position` and
// `normal` (3 floats a vertex each). A vertex no bone moves stays where it
// is. False, writing nothing, for bytes that aren't a model of m's vertices.
bool tide_mesh_data_rest(const uint8_t *model, uint32_t size, const tide_mesh_data *m, float *position, float *normal);

// A vertex's four heaviest bones, the heaviest first (a tie keeps the
// model's order), as the skeleton numbers them, and their weights, which sum
// to 1. A vertex no bone moves has no weight at all, and bone 0.
void tide_mesh_data_bones(const struct tide_skinned_data_view *view, uint32_t vertex, int32_t bone[4], float weight[4]);

// A decimal number as a float, the same bits on every machine: `s` to `end`,
// all of it. False for text that isn't one. What no float holds is 0.
bool tide_mesh_number(const char *s, const char *end, float *out);
