#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// A mesh file as an `asset Mesh<V>` reads it (docs/spec.md, Assets, Meshes):
// glTF 2.0 (a .glb, or a .gltf with its buffers inside it or in files beside
// it), Wavefront OBJ, and FBX (through the compiler's ufbx import). Whatever the
// file is, what comes out is one mesh as Tide draws one: every mesh the file
// places, where it places it, with x turned around (glTF is right-handed;
// FBX first converts to right-handed Y-up meters, and OBJ uses its written
// units). Each triangle's corners go clockwise seen from its front.
//
// Arithmetic has a fixed order; FBX uses ufbx's portable math implementation.
// A file gives the same bits whichever machine builds the game: match code
// reads a mesh's vertices too.

// ---------------------------------------------------------------------------
// Limits: what a file's bytes can ask of a read. A file can be anyone's (an
// asset's URL), and tidels reads a game's mesh files at every keystroke, so a
// small hostile file mustn't take the tools' memory or minutes. Each limit is
// only its number here: change it and nothing else changes. A file past one
// is refused from the counts it says, before what it asks for is made or
// done; an accessor with no buffer view (zeros, which take no bytes) counts
// as one with bytes. SkinnedMesh imports are held to the engine's own limits
// instead (tide/skinned_data.h), since a game decodes those too.
//
// At these numbers a read's peak memory is about 3 GiB for a mesh at every
// limit at once (73 bytes a vertex in the mesh and as much again while a part
// is added), plus about 100 bytes for each JSON value and twice the file's own
// size; its work is about TIDE_MESH_MAX_READS numbers read, plus the file's size.

// JSON values in a glTF's description (numbers, strings, arrays, objects,
// each one), each about 100 bytes once read. A description is small beside
// its binary data: 65,536 nodes with all their fields are under a million.
#define TIDE_MESH_MAX_JSON_VALUES (1u << 22)

// A mesh's vertices and triangle corners (three a triangle: 8,388,608
// triangles), and the elements one accessor has. A part with no normals has
// three vertices a triangle of its own.
#define TIDE_MESH_MAX_VERTICES (1u << 24)
#define TIDE_MESH_MAX_INDICES (3u << 23)

// Numbers read from a glTF's accessors over the whole read: an accessor
// several parts name is read again for each, so this bounds the work, not
// the counts a file says once. A mesh at the vertex and index limits reads
// about 330 million.
#define TIDE_MESH_MAX_READS (1u << 29)

typedef struct mesh_file {
    uint32_t vertices;
    uint32_t indices;  // Three a triangle
    float *position;   // 3 a vertex
    float *normal;     // 3: the file's, or its triangle's own where the file has none
    float *uv;         // 2: (0, 0) at the texture's top left; zeros where the file has none
    float *uv2;        // 2: its second set, the same way
    float *color;      // 4: the vertex's color times its material's, as the screen shows it; white where the file has neither
    float *tangent;    // 4: xyz and bitangent handedness; zeros where the file has none
    uint32_t *index;
    bool has_normals, has_uvs, has_uvs2, has_colors, has_tangents; // What any part of the file had, for the editor
    uint32_t parts;    // How many meshes of the file it's made of: a glTF's primitives as its nodes place them, an OBJ's one
    uint8_t hash[32];  // SHA-256 of all of the above: what's in the mesh, whatever files it came from
    uint8_t *sources;  // Private import provenance per vertex: authored tangent (1), first UV (2); not hashed/packed
} mesh_file;

// A file a .gltf names beside it (its buffers'), by the path it writes, from
// the .gltf's own folder: its bytes, which stay the caller's, or NULL.
typedef const uint8_t *(*mesh_side_fn)(void *user, const char *path, size_t *size);

// Reads `size` bytes as a mesh. `name` is the file's name, whose extension
// says what it is where its first bytes don't. False for bytes that aren't a
// mesh Tide reads, with why in `why` (what follows "'tree.glb' ") and, where
// there's something to do about it, what in `note` (else empty).
bool mesh_file_read(const uint8_t *data, size_t size, const char *name, mesh_side_fn side, void *user, mesh_file *out,
                    char why[256], char note[256]);

// An owned copy of a canonical static import, with missing tangents generated
// and opposite handedness split. Only consumers with float4 tangent select it;
// mesh_file_read and its metadata/other-layout consumers retain original data.
bool mesh_file_with_tangents(const mesh_file *source, mesh_file *out, char why[256], char note[256]);

void mesh_file_free(mesh_file *m);

// A decimal number as a float, the same bits on every machine: `s` to `end`,
// all of it. False for text that isn't one. What no float holds is 0.
bool mesh_number(const char *s, const char *end, float *out);
