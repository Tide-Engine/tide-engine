#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
