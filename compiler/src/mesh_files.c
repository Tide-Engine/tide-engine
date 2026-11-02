// Mesh files as `asset Mesh<V>` reads them (see mesh_files.h). glTF 2.0 and
// Wavefront OBJ are the engine's reader (tide/mesh_data.h, engine/src/
// mesh_decode.c), which a game's Mesh<V>.Decode uses too, held here to the
// build's own limits; this adds FBX, through the build-time ufbx converter,
// and the file's name, which says the format where its first bytes don't.

#include "mesh_files.h"

#include "fbx_files.h"

#include "assets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const tide_mesh_limits build_limits = {
    .bytes = UINT64_MAX,
    .json_values = TIDE_MESH_MAX_JSON_VALUES,
    .vertices = TIDE_MESH_MAX_VERTICES,
    .indices = TIDE_MESH_MAX_INDICES,
    .reads = TIDE_MESH_MAX_READS,
    .prefix = "TIDE_MESH_MAX",
    .holder = "one Mesh holds",
};

bool mesh_number(const char *s, const char *end, float *out)
{
    return tide_mesh_number(s, end, out);
}

void mesh_file_free(mesh_file *m)
{
    tide_mesh_data_free(m);
}

static bool ends_with(const char *name, const char *extension)
{
    size_t n = strlen(name);
    if (asset_path_is_url(name, n)) {
        // Only the URL's path names its format. Its query and fragment still
        // belong to the complete URL used for fetching, caching and map keys.
        const char *authority = strstr(name, "://") + 3;
        const char *path = authority + strcspn(authority, "/?#");
        if (*path != '/') return false;
        n = (size_t)(path - name) + strcspn(path, "?#");
    }
    const size_t e = strlen(extension);
    if (n < e) return false;
    for (size_t i = 0; i < e; i++) {
        const char c = name[n - e + i];
        if ((c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c) != extension[i]) return false;
    }
    return true;
}

bool mesh_file_read(const uint8_t *data, const size_t size, const char *name, const mesh_side_fn side, void *user, mesh_file *out,
                    char why[256], char note[256])
{
    why[0] = note[0] = '\0';
    if (size >= 4u && memcmp(data, "glTF", 4) == 0) {
        return tide_mesh_data_decode(data, size, TIDE_MESH_GLB, &build_limits, side, user, out, why, note, NULL);
    }
    if (fbx_file_is(data, size, name)) {
        uint8_t *glb = NULL;
        size_t glb_size = 0;
        bool ok = fbx_file_to_glb(data, size, false, &glb, &glb_size, why, note);
        if (ok) ok = tide_mesh_data_decode(glb, glb_size, TIDE_MESH_GLB, &build_limits, side, user, out, why, note, NULL);
        free(glb);
        return ok;
    }
    const tide_mesh_format format = ends_with(name, ".glb")    ? TIDE_MESH_GLB
                                  : ends_with(name, ".gltf") ? TIDE_MESH_GLTF
                                  : ends_with(name, ".obj")  ? TIDE_MESH_OBJ
                                                             : TIDE_MESH_ANY;
    if (format == TIDE_MESH_ANY) {
        snprintf(why, 256, "isn't a mesh file Tide reads");
        snprintf(note, 256, "a Mesh asset is glTF (.glb or .gltf), Wavefront .obj, or FBX (.fbx): export it as one of those");
        return false;
    }
    return tide_mesh_data_decode(data, size, format, &build_limits, side, user, out, why, note, NULL);
}

bool mesh_file_with_tangents(const mesh_file *source, mesh_file *out, char why[256], char note[256])
{
    return tide_mesh_data_with_tangents(source, &build_limits, out, why, note);
}
