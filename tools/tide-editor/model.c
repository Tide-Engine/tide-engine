#include "model.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

void editor_model_free(editor_model *model)
{
    free((void *)model->mesh.vertices);
    free((void *)model->mesh.indices);
    *model = (editor_model){0};
}

bool editor_model_set(editor_model *out, const mesh_file *source, bool lighting)
{
    editor_model_free(out);
    if (!source || !source->vertices || !source->indices || !source->position || !source->index) return false;
    if ((uint64_t)source->vertices * sizeof(tide_vertex3) > SIZE_MAX ||
        (uint64_t)source->indices * sizeof(uint32_t) > SIZE_MAX) return false;
    tide_vertex3 *vertices = malloc((size_t)source->vertices * sizeof(*vertices));
    uint32_t *indices = malloc((size_t)source->indices * sizeof(*indices));
    if (!vertices || !indices) { free(vertices); free(indices); return false; }
    tide_float3 low = tide_f3(INFINITY, INFINITY, INFINITY), high = tide_f3(-INFINITY, -INFINITY, -INFINITY);
    const tide_float3 light = tide_normalize_f3(tide_f3(-.4f,.8f,-.6f));
    for (uint32_t i = 0; i < source->vertices; i++) {
        const float *p = source->position + (size_t)i * 3;
        if (!isfinite(p[0]) || !isfinite(p[1]) || !isfinite(p[2])) { free(vertices); free(indices); return false; }
        vertices[i].position = tide_f3(p[0], p[1], p[2]);
        vertices[i].uv = source->uv ? tide_f2(source->uv[2u * i], source->uv[2u * i + 1]) : tide_f2(0, 0);
        vertices[i].color = source->color ? (tide_color){source->color[4u*i],source->color[4u*i+1],source->color[4u*i+2],source->color[4u*i+3]} : (tide_color){1,1,1,1};
        if (lighting && source->normal) {
            const float *n = source->normal + (size_t)i * 3;
            const float shade = .3f + .7f*tide_max_f(0, tide_dot_f3(tide_normalize_f3(tide_f3(n[0],n[1],n[2])), light));
            vertices[i].color.r *= shade; vertices[i].color.g *= shade; vertices[i].color.b *= shade;
        }
        low = tide_min_f3(low, vertices[i].position); high = tide_max_f3(high, vertices[i].position);
    }
    for (uint32_t i = 0; i < source->indices; i++) {
        if (source->index[i] >= source->vertices) { free(vertices); free(indices); return false; }
        indices[i] = source->index[i];
    }
    // Half first avoids overflow for finite opposite or large bounds.
    out->center = tide_add_f3(tide_mul_f3(low, tide_f3_splat(.5f)), tide_mul_f3(high, tide_f3_splat(.5f)));
    const tide_float3 extent = tide_sub_f3(high, out->center);
    const float largest = fmaxf(fmaxf(extent.x, extent.y), extent.z);
    if (!isfinite(largest)) { free(vertices); free(indices); *out = (editor_model){0}; return false; }
    const double radius = sqrt((double)extent.x*extent.x + (double)extent.y*extent.y + (double)extent.z*extent.z);
    out->radius = (float)fmax(radius, 0.001);
    if (!isfinite(out->radius)) { free(vertices); free(indices); *out = (editor_model){0}; return false; }
    uint64_t version; memcpy(&version, source->hash, sizeof version);
    if (lighting) version ^= UINT64_C(0x753087cb159a681d);
    out->mesh = (tide_mesh){vertices, source->vertices, indices, source->indices, version ? version : 1};
    return true;
}
