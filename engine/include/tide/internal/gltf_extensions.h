#pragma once

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

// Optional extensions use their core glTF fallback. Required extensions must
// be understood before either geometry or an editor catalog is accepted.
static inline bool gltf_required_extension_error(const char *name, char why[256], char note[256])
{
    if (strcmp(name, "KHR_mesh_quantization") == 0) return false;
    const char *compression = strcmp(name, "KHR_draco_mesh_compression") == 0 ? "Draco"
                            : strcmp(name, "EXT_meshopt_compression") == 0 || strcmp(name, "KHR_meshopt_compression") == 0 ? "meshopt" : NULL;
    if (compression) {
        snprintf(why, 256, "has its geometry compressed with %s, which Tide doesn't read", compression);
        snprintf(note, 256, "export it again with compression off: Tide reads a mesh's numbers as they are");
    } else {
        snprintf(why, 256, "requires an unsupported glTF extension ('%.80s')", name);
        snprintf(note, 256, "export without required extensions other than KHR_mesh_quantization");
    }
    return true;
}
