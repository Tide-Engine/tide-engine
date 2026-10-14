#pragma once

#include "mesh_files.h"
#include "tide/draw.h"

// Editor-owned geometry. It survives compiler refresh and never executes game
// vertex defaults, shaders or views.
typedef struct editor_model {
    tide_mesh mesh;
    tide_float3 center;
    float radius;
} editor_model;

bool editor_model_set(editor_model *out, const mesh_file *source, bool lighting);
void editor_model_free(editor_model *model);
