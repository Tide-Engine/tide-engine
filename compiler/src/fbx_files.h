#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Limits: what an FBX file's bytes can ask of a conversion. A file can be
// anyone's (an asset's URL), so a small hostile file mustn't take the tools'
// memory or minutes. Each limit is only its number here: change it and
// nothing else changes. A file past one is refused before what it asks for is
// made or done. The converted GLB is then held to the mesh decoders' own
// limits (mesh_files.h, tide/skinned_data.h).
//
// At these numbers a conversion's peak memory is about 3 GiB: ufbx's scene
// and its working memory (TIDE_FBX_MAX_BYTES each), one take's baked keys,
// and the GLB with the binary data it's assembled from. Its work is about the
// file's size, TIDE_FBX_MAX_TRIANGULATION and TIDE_FBX_MAX_BAKED_KEYS.

// A file's bytes, ufbx's scene and its working memory (each), each piece of
// converted data, and the converted GLB.
#define TIDE_FBX_MAX_BYTES (512u << 20)

// Nodes, and animation takes, a file has.
#define TIDE_FBX_MAX_NODES 65536u

// A mesh's polygon corners (each one a converted vertex at the most) and its
// triangle corners (three a triangle), and the converted model's.
#define TIDE_FBX_MAX_VERTICES (1u << 24)
#define TIDE_FBX_MAX_INDICES (3u << 25)

// Polygon corners squared, summed over every polygon: what cutting polygons
// into triangles costs. A polygon of n corners takes about n * n steps, so a
// file with one 100,000-corner polygon took 3.5 seconds. A million quads
// count 16 million; 4,294,967,296 is about two seconds.
#define TIDE_FBX_MAX_TRIANGULATION (UINT64_C(1) << 32)

// Animation keys baked from every take together, before each take is cut to
// its playback range: a take's curves can span far more than it plays, and a
// key is a few bytes of a file but 40 or 48 of ufbx's, and work to sample. A
// skinned model holds fewer (TIDE_SKINNED_MAX_KEY_NUMBERS, four or more a key).
#define TIDE_FBX_MAX_BAKED_KEYS (1u << 24)

// Build-time FBX import. Binary magic, an ASCII header, or the written file's
// extension identifies FBX (URLs use their path before the query/fragment).
bool fbx_file_is(const uint8_t *data, size_t size, const char *name);

// Convert to a self-contained glTF 2.0 GLB for the ordinary mesh decoders.
// Static meshes ignore deformation/animation, as the glTF importer does.
// Skinned import keeps the hierarchy, all positive influences and animation
// stacks, including animation-only files without geometry; unsupported
// deformation fails with an actionable diagnostic.
// The returned allocation belongs to the caller and is released with free().
// Failure leaves both outputs empty.
bool fbx_file_to_glb(const uint8_t *data, size_t size, bool skinned,
                     uint8_t **out, size_t *out_size, char why[256], char note[256]);
