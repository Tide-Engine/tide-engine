#include "model.h"
#include "tide_test.h"
#include <math.h>
#include <string.h>

TIDE_TEST(editor_model_owns_imported_geometry_after_source_is_freed)
{
    const char *obj = "v 0 0 0\nv 2 0 0\nv 0 2 0\nf 1 2 3\n";
    mesh_file imported = {0}; char why[256], note[256];
    TIDE_REQUIRE(mesh_file_read((const uint8_t *)obj, strlen(obj), "triangle.obj", NULL, NULL, &imported, why, note));
    editor_model model = {0}; TIDE_REQUIRE(editor_model_set(&model, &imported, false));
    const tide_float3 original = model.mesh.vertices[0].position;
    mesh_file_free(&imported);
    TIDE_CHECK(model.mesh.vertex_count == 3); TIDE_CHECK(model.mesh.index_count == 3);
    TIDE_CHECK(memcmp(&model.mesh.vertices[0].position, &original, sizeof original) == 0);
    TIDE_CHECK(model.center.x == -1); TIDE_CHECK(model.center.y == 1); TIDE_CHECK(fabsf(model.radius - sqrtf(2)) < .00001f);
    editor_model_free(&model); TIDE_CHECK(model.mesh.vertices == NULL);
}

TIDE_TEST(editor_model_replacement_refuses_invalid_geometry_and_clears_old_preview)
{
    float positions[] = {0,0,0, 1,0,0, 0,1,0}; uint32_t indices[] = {0,1,2};
    mesh_file source = {.vertices=3,.indices=3,.position=positions,.index=indices};
    editor_model model = {0}; TIDE_REQUIRE(editor_model_set(&model, &source, false));
    indices[2] = 3;
    TIDE_CHECK(!editor_model_set(&model, &source, false)); TIDE_CHECK(model.mesh.vertex_count == 0);
    indices[2] = 2; positions[0] = NAN;
    TIDE_CHECK(!editor_model_set(&model, &source, false)); TIDE_CHECK(model.mesh.vertices == NULL);
    positions[0] = 0;
    TIDE_CHECK(editor_model_set(&model, &source, false));
    TIDE_CHECK(!editor_model_set(&model, NULL, false)); TIDE_CHECK(model.mesh.indices == NULL);
    editor_model_free(&model);
}
