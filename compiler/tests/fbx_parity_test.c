// A real export must produce the same deformed surface through Tide's runtime
// sampling and ufbx's independent scene evaluator. The positive take start and
// animated scale compensation catch baking helpers in a shifted time domain.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "fbx_files.h"
#include "ufbx.h"
#include "tide/skinned.h"
#include "tide/skinned_data.h"
#include "tide_test.h"

static uint8_t *parity_source(size_t *size)
{
    FILE *file = fopen(FBX_FIXTURES "/upstream/maya_game_sausage_7500_binary_deform.fbx", "rb");
    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END)) { fclose(file); return NULL; }
    const long length = ftell(file);
    if (length < 0 || length > (1 << 20) || fseek(file, 0, SEEK_SET)) { fclose(file); return NULL; }
    uint8_t *bytes = malloc(length ? (size_t)length : 1u);
    bool ok = bytes && fread(bytes, 1u, (size_t)length, file) == (size_t)length;
    fclose(file);
    if (!ok) { free(bytes); return NULL; }
    *size = (size_t)length;
    return bytes;
}

static bool parity_sample(const tide_list model, const tide_skinned_data_view *view,
                           const ufbx_scene *source, const float seconds, double *worst)
{
    bool ok = false;
    const uint32_t mark = tide_scratch_mark();
    tide_list pose = {0};
    ufbx_evaluate_opts opts = {0};
    opts.evaluate_skinning = true;
    ufbx_error error;
    const ufbx_anim_stack *stack = source->anim_stacks.data[0];
    ufbx_scene *evaluated = ufbx_evaluate_scene(source, stack->anim, stack->time_begin + (double)seconds, &opts, &error);
    if (!evaluated || !tide_skinned_sample(model, 0, seconds, &pose, TIDE_IN_SCRATCH)) goto done;
    const tide_list palette = tide_skinned_palette(model, pose);
    if ((uint32_t)tide_list_count(palette) != view->palettes) goto done;
    for (uint32_t i = 0; i < view->vertices; i++) {
        tide_skinned_vertex_data vertex;
        if (!tide_skinned_data_vertex(view, i, &vertex)) goto done;
        tide_float3 point = tide_f3(0.0f, 0.0f, 0.0f);
        for (uint32_t k = 0; k < vertex.influences; k++) {
            tide_skinned_influence_data influence;
            if (!tide_skinned_data_influence(view, vertex.first_influence + k, &influence)) goto done;
            const tide_float4x4 *matrix = tide_list_at(palette, (int32_t)influence.palette, sizeof *matrix);
            if (!matrix) goto done;
            const tide_float3 position = tide_transform_f4x4(*matrix, tide_f3(vertex.position[0], vertex.position[1], vertex.position[2]));
            point = tide_add_f3(point, tide_mul_f3(position, tide_f3_splat(influence.weight)));
        }
        double closest = 1e100;
        for (size_t n = 0; n < evaluated->nodes.count; n++) {
            const ufbx_node *node = evaluated->nodes.data[n];
            if (!node->mesh) continue;
            const ufbx_mesh *mesh = node->mesh;
            if (mesh->num_indices > 1024u || !mesh->skinned_position.exists) goto done;
            for (size_t k = 0; k < mesh->num_indices; k++) {
                ufbx_vec3 expected = ufbx_get_vertex_vec3(&mesh->skinned_position, k);
                if (mesh->skinned_is_local) expected = ufbx_transform_position(&node->geometry_to_world, expected);
                // The reference evaluator stays in right-handed coordinates.
                const double dx = expected.x + (double)point.x;
                const double dy = expected.y - (double)point.y;
                const double dz = expected.z - (double)point.z;
                const double distance = dx * dx + dy * dy + dz * dz;
                if (distance < closest) closest = distance;
            }
        }
        if (closest > *worst) *worst = closest;
    }
    ok = true;
done:
    ufbx_free_scene(evaluated);
    tide_scratch_reset(mark);
    return ok;
}

TIDE_TEST(fbx_runtime_skin_matches_maya_export_with_animated_scale_and_offset_take)
{
    size_t source_size = 0u;
    uint8_t *source = parity_source(&source_size);
    TIDE_REQUIRE(source);
    uint8_t *glb = NULL, *canonical = NULL;
    size_t glb_size = 0u;
    uint32_t canonical_size = 0u;
    char why[256], note[256];
    bool converted = fbx_file_to_glb(source, source_size, true, &glb, &glb_size, why, note)
                  && tide_skinned_data_decode(glb, glb_size, false, NULL, NULL, &canonical, &canonical_size, why, note);
    TIDE_CHECK(converted);
    free(glb);
    if (!converted) { free(source); return; }
    tide_skinned_data_view view;
    const bool opened = tide_skinned_data_open(canonical, canonical_size, &view);
    TIDE_CHECK(opened);
    if (!opened) { free(source); tide_skinned_data_free(canonical); return; }
    TIDE_CHECK(view.vertices == 88u && view.indices == 132u && view.palettes == 3u && view.clips == 1u);
    ufbx_load_opts opts = {0};
    opts.target_axes = ufbx_axes_right_handed_y_up;
    opts.target_unit_meters = 1.0;
    opts.space_conversion = UFBX_SPACE_CONVERSION_ADJUST_TRANSFORMS;
    opts.ignore_embedded = true;
    opts.temp_allocator.memory_limit = opts.result_allocator.memory_limit = 16u << 20;
    // Keep FBX's own geometry transforms/inheritance in the reference scene;
    // it need not reproduce the importer's generated glTF helper hierarchy.
    ufbx_error error;
    ufbx_scene *scene = ufbx_load_memory(source, source_size, &opts, &error);
    TIDE_CHECK(scene && scene->anim_stacks.count == 1u);
    if (scene && scene->anim_stacks.count == 1u) {
        TIDE_CHECK(scene->anim_stacks.data[0]->time_begin > 0.0);
        const uint32_t mark = tide_scratch_mark();
        const tide_list model = tide_list_from(canonical, (int32_t)canonical_size, 1u);
        const float duration = tide_skinned_animation_duration(model, 0);
        double worst = 0.0;
        for (uint32_t i = 0; i < 5u; i++) TIDE_CHECK(parity_sample(model, &view, scene, duration * (float)i / 4.0f, &worst));
        if (!(worst <= 1e-10)) printf("    maximum skin error: %.9g meters\n", sqrt(worst));
        TIDE_CHECK(worst <= 1e-10); // Ten micrometers.
        tide_scratch_reset(mark);
    }
    ufbx_free_scene(scene);
    tide_skinned_data_free(canonical);
    free(source);
}
