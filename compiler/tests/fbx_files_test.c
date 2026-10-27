// The FBX compiler boundary: exporter files and small independently authored
// ASCII/binary pairs must become the same canonical geometry and animation.
#if !defined(_WIN32) && !defined(__APPLE__)
#define _DEFAULT_SOURCE
#endif
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <sys/utime.h>
#else
#include <utime.h>
#endif

#include "asset_files.h"
#include "fbx_files.h"
#include "folders.h"
#include "mesh_files.h"
#include "model_metadata.h"
#include "tide/math.h"
#include "tide/skinned_data.h"
#include "tide_test.h"

static uint8_t *fixture(const char *name, size_t *size)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", FBX_FIXTURES, name);
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return NULL; }
    const long length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET) != 0) { fclose(file); return NULL; }
    uint8_t *bytes = malloc(length ? (size_t)length : 1u);
    const bool ok = bytes && fread(bytes, 1, (size_t)length, file) == (size_t)length;
    fclose(file);
    if (!ok) { free(bytes); return NULL; }
    *size = (size_t)length;
    return bytes;
}

static bool near(const float a, const float b) { return fabsf(a - b) < 0.00001f; }
static bool vector3(const float *v, const float x, const float y, const float z)
{
    return near(v[0], x) && near(v[1], y) && near(v[2], z);
}
static float display(const float value)
{
    return value <= 0.0031308f ? value * 12.92f : 1.055f * tide_pow_f(value, 1.0f / 2.4f) - 0.055f;
}

static bool read_mesh(const uint8_t *data, const size_t size, const char *name, mesh_file *out)
{
    char why[256], note[256];
    const bool ok = mesh_file_read(data, size, name, NULL, NULL, out, why, note);
    if (!ok) printf("    %s: %s (%s)\n", name, why, note);
    return ok;
}

static bool decode(const uint8_t *data, const size_t size, uint8_t **out, uint32_t *out_size)
{
    uint8_t *glb = NULL;
    size_t glb_size = 0;
    char why[256], note[256];
    if (!fbx_file_to_glb(data, size, true, &glb, &glb_size, why, note)) {
        printf("    FBX: %s (%s)\n", why, note);
        return false;
    }
    const bool ok = tide_skinned_data_decode(glb, glb_size, false, NULL, NULL, out, out_size, why, note);
    free(glb);
    if (!ok) printf("    converted GLB: %s (%s)\n", why, note);
    return ok;
}

TIDE_TEST(fbx_ascii_and_binary_static_geometry_material_uv_and_mirror)
{
    size_t a_size, b_size;
    uint8_t *a = fixture("static/triangle_ascii.fbx", &a_size), *b = fixture("static/triangle_binary.fbx", &b_size);
    TIDE_REQUIRE(a && b);
    TIDE_CHECK(fbx_file_is(a, a_size, "triangle.fbx"));
    TIDE_CHECK(fbx_file_is(b, b_size, "cached-download"));
    mesh_file ascii, binary;
    TIDE_REQUIRE(read_mesh(a, a_size, "triangle.fbx", &ascii));
    TIDE_REQUIRE(read_mesh(b, b_size, "cached-download", &binary));
    TIDE_CHECK(memcmp(ascii.hash, binary.hash, sizeof ascii.hash) == 0);
    TIDE_REQUIRE(ascii.vertices == 3 && ascii.indices == 3);
    TIDE_CHECK(ascii.has_normals && ascii.has_uvs);
    bool seen[3] = {false, false, false};
    static const float positions[3][3] = {{-3, 4, 0}, {-1, 4, 0}, {-3, 8, 0}};
    static const float uvs[3][2] = {{0, 1}, {1, 1}, {0, 0}};
    for (uint32_t i = 0; i < ascii.vertices; i++) {
        int matched = -1;
        for (int j = 0; j < 3; j++) {
            if (vector3(ascii.position + i * 3u, positions[j][0], positions[j][1], positions[j][2])) matched = j;
        }
        TIDE_REQUIRE(matched >= 0);
        TIDE_CHECK(!seen[matched]);
        seen[matched] = true;
        TIDE_CHECK(vector3(ascii.normal + i * 3u, 0, 0, 1));
        TIDE_CHECK(near(ascii.uv[i * 2u], uvs[matched][0]) && near(ascii.uv[i * 2u + 1u], uvs[matched][1]));
        TIDE_CHECK(near(ascii.color[i * 4u], display(0.25f)) && near(ascii.color[i * 4u + 1u], display(0.5f))
                   && ascii.color[i * 4u + 2u] == 1.0f && ascii.color[i * 4u + 3u] == 1.0f);
    }
    const float *p = ascii.position + ascii.index[0] * 3u, *q = ascii.position + ascii.index[1] * 3u,
                *r = ascii.position + ascii.index[2] * 3u;
    TIDE_CHECK((q[0] - p[0]) * (r[1] - p[1]) - (q[1] - p[1]) * (r[0] - p[0]) > 0.0f);
    mesh_file_free(&ascii);
    mesh_file_free(&binary);
    free(a);
    free(b);
}

static uint32_t named_node(const tide_skinned_data_view *view, const char *name)
{
    for (uint32_t i = 0; i < view->nodes; i++) {
        tide_skinned_node_data node;
        if (tide_skinned_data_node(view, i, &node) && node.name_length == strlen(name)
            && memcmp(node.name, name, node.name_length) == 0) return i;
    }
    return UINT32_MAX;
}

static bool rewrite(char *text, const char *from, const char *to)
{
    char *at = strstr(text, from);
    if (!at) return false;
    const size_t old_length = strlen(from), new_length = strlen(to);
    memmove(at + new_length, at + old_length, strlen(at + old_length) + 1u);
    memcpy(at, to, new_length);
    return true;
}

TIDE_TEST(fbx_index_past_its_array_is_clamped_to_the_last_element)
{
    size_t size;
    uint8_t *bytes = fixture("static/triangle_ascii.fbx", &size);
    TIDE_REQUIRE(bytes);
    char *text = malloc(size + 64u);
    TIDE_REQUIRE(text);
    memcpy(text, bytes, size);
    text[size] = '\0';
    // The first corner's uv index is past the three uvs: it reads the last, (0, 1), which is (0, 0) as Tide has uvs
    TIDE_REQUIRE(rewrite(text, "UVIndex: *3 {\n        a: 0,1,2", "UVIndex: *3 {\n        a: 7,1,2"));
    mesh_file mesh;
    TIDE_REQUIRE(read_mesh((const uint8_t *)text, strlen(text), "triangle.fbx", &mesh));
    TIDE_REQUIRE(mesh.vertices == 3 && mesh.has_uvs);
    bool found = false;
    for (uint32_t i = 0; i < mesh.vertices; i++) {
        if (!vector3(mesh.position + i * 3u, -3, 4, 0)) continue;
        found = true;
        TIDE_CHECK(near(mesh.uv[i * 2u], 0) && near(mesh.uv[i * 2u + 1u], 0));
    }
    TIDE_CHECK(found);
    mesh_file_free(&mesh);
    free(text);
    free(bytes);
}

TIDE_TEST(fbx_centimeter_units_and_z_up_axes_convert_to_meters_y_up)
{
    size_t size;
    uint8_t *bytes = fixture("static/triangle_ascii.fbx", &size);
    TIDE_REQUIRE(bytes);
    char *changed = calloc(size + 128u, 1);
    TIDE_REQUIRE(changed);
    memcpy(changed, bytes, size);
    mesh_file original, centimeters, z_up;
    TIDE_REQUIRE(read_mesh(bytes, size, "triangle.fbx", &original));
    TIDE_REQUIRE(rewrite(changed, "\"UnitScaleFactor\", \"double\", \"\", \"A\", 100", "\"UnitScaleFactor\", \"double\", \"\", \"A\", 1"));
    TIDE_REQUIRE(read_mesh((uint8_t *)changed, strlen(changed), "centimeters.fbx", &centimeters));
    TIDE_REQUIRE(centimeters.vertices == original.vertices && centimeters.indices == original.indices);
    for (uint32_t i = 0; i < original.vertices * 3u; i++) TIDE_CHECK(near(centimeters.position[i], original.position[i] * 0.01f));
    memcpy(changed, bytes, size);
    changed[size] = 0;
    TIDE_REQUIRE(rewrite(changed, "\"UpAxis\", \"int\", \"\", \"A\", 1", "\"UpAxis\", \"int\", \"\", \"A\", 2"));
    TIDE_REQUIRE(rewrite(changed, "\"FrontAxis\", \"int\", \"\", \"A\", 2", "\"FrontAxis\", \"int\", \"\", \"A\", 1"));
    TIDE_REQUIRE(rewrite(changed, "\"FrontAxisSign\", \"int\", \"\", \"A\", 1", "\"FrontAxisSign\", \"int\", \"\", \"A\", -1"));
    TIDE_REQUIRE(read_mesh((uint8_t *)changed, strlen(changed), "z_up.fbx", &z_up));
    TIDE_REQUIRE(z_up.vertices == original.vertices && z_up.indices == original.indices);
    for (uint32_t i = 0; i < original.vertices; i++) {
        TIDE_CHECK(vector3(z_up.position + i * 3u, original.position[i * 3u], 0, -original.position[i * 3u + 1u]));
        TIDE_CHECK(vector3(z_up.normal + i * 3u, 0, 1, 0));
    }
    mesh_file_free(&original);
    mesh_file_free(&centimeters);
    mesh_file_free(&z_up);
    free(changed);
    free(bytes);
}

TIDE_TEST(fbx_skin_preserves_five_influences_inverse_binds_rest_and_clip)
{
    size_t a_size, b_size;
    uint8_t *a = fixture("models/rig_ascii.fbx", &a_size), *b = fixture("models/rig_binary.fbx", &b_size);
    TIDE_REQUIRE(a && b);
    uint8_t *ascii = NULL, *binary = NULL;
    uint32_t ascii_size = 0, binary_size = 0;
    TIDE_REQUIRE(decode(a, a_size, &ascii, &ascii_size));
    TIDE_REQUIRE(decode(b, b_size, &binary, &binary_size));
    TIDE_CHECK(ascii_size == binary_size && memcmp(ascii, binary, ascii_size) == 0);
    TIDE_CHECK(tide_skinned_data_validate(ascii, ascii_size));
    tide_skinned_data_view view;
    TIDE_REQUIRE(tide_skinned_data_open(ascii, ascii_size, &view));
    TIDE_REQUIRE(view.vertices == 3 && view.indices == 3 && view.clips == 1);
    const uint32_t root = named_node(&view, "Root"), bone = named_node(&view, "Bone0");
    TIDE_REQUIRE(root != UINT32_MAX && bone != UINT32_MAX);
    tide_skinned_node_data node;
    TIDE_REQUIRE(tide_skinned_data_node(&view, root, &node));
    TIDE_CHECK(vector3(node.position, -2, 0, 0));
    TIDE_REQUIRE(tide_skinned_data_node(&view, bone, &node));
    TIDE_CHECK(node.parent == (int32_t)root && vector3(node.position, -1, 0, 0));
    for (uint32_t i = 0; i < view.vertices; i++) {
        tide_skinned_vertex_data vertex;
        TIDE_REQUIRE(tide_skinned_data_vertex(&view, i, &vertex));
        TIDE_CHECK(vertex.influences == 5);
        bool found[5] = {false, false, false, false, false};
        float total = 0;
        for (uint32_t j = 0; j < vertex.influences; j++) {
            tide_skinned_influence_data influence;
            tide_skinned_palette_data palette;
            TIDE_REQUIRE(tide_skinned_data_influence(&view, vertex.first_influence + j, &influence));
            TIDE_REQUIRE(tide_skinned_data_palette(&view, influence.palette, &palette));
            TIDE_REQUIRE(tide_skinned_data_node(&view, palette.node, &node));
            TIDE_REQUIRE(node.name_length == 5 && memcmp(node.name, "Bone", 4) == 0);
            const int which = node.name[4] - '0';
            TIDE_REQUIRE(which >= 0 && which < 5);
            TIDE_CHECK(!found[which]);
            found[which] = true;
            TIDE_CHECK(near(influence.weight, (float)(which + 1) / 15.0f));
            TIDE_CHECK(palette.inverse_bind[12] == (float)(which + 3));
            total += influence.weight;
        }
        TIDE_CHECK(near(total, 1.0f));
    }
    tide_skinned_clip_data clip;
    TIDE_REQUIRE(tide_skinned_data_clip(&view, 0, &clip));
    TIDE_CHECK(clip.name_length == 4 && memcmp(clip.name, "Move", 4) == 0 && clip.length == 1.0f);
    bool translated = false;
    for (uint32_t i = 0; i < clip.channels; i++) {
        tide_skinned_channel_data channel;
        TIDE_REQUIRE(tide_skinned_data_channel(&view, clip.first_channel + i, &channel));
        if (channel.node != bone || channel.path != TIDE_SKINNED_TRANSLATION) continue;
        translated = true;
        float time, value[4], in[4], out[4];
        TIDE_REQUIRE(tide_skinned_data_key(&view, &channel, 0, &time, value, in, out));
        TIDE_CHECK(time == 0 && vector3(value, -1, 0, 0));
        TIDE_REQUIRE(tide_skinned_data_key(&view, &channel, channel.keys - 1u, &time, value, in, out));
        TIDE_CHECK(time == 1 && vector3(value, -5, 0, 0));
    }
    TIDE_CHECK(translated);
    tide_skinned_data_free(ascii);
    tide_skinned_data_free(binary);
    free(a);
    free(b);
}

// Blend channels become the model's shapes, named after their target shape,
// with the channel's DeformPercent as the default the vertices already have.
TIDE_TEST(fbx_blend_shapes_become_shapes_and_in_betweens_are_refused)
{
    size_t a_size, b_size;
    uint8_t *a = fixture("shapes/shapes_ascii.fbx", &a_size), *b = fixture("shapes/shapes_binary.fbx", &b_size);
    TIDE_REQUIRE(a && b);
    uint8_t *ascii = NULL, *binary = NULL;
    uint32_t ascii_size = 0, binary_size = 0;
    TIDE_REQUIRE(decode(a, a_size, &ascii, &ascii_size));
    TIDE_REQUIRE(decode(b, b_size, &binary, &binary_size));
    TIDE_CHECK(ascii_size == binary_size && memcmp(ascii, binary, ascii_size) == 0);
    TIDE_CHECK(tide_skinned_data_validate(ascii, ascii_size));
    tide_skinned_data_view view;
    TIDE_REQUIRE(tide_skinned_data_open(ascii, ascii_size, &view));
    TIDE_REQUIRE(view.vertices == 3 && view.shapes == 2 && view.shape_deltas == 3);
    tide_skinned_shape_data smile, jaw;
    TIDE_REQUIRE(tide_skinned_data_shape(&view, 0, &smile) && tide_skinned_data_shape(&view, 1, &jaw));
    TIDE_CHECK(smile.name_length == 5 && memcmp(smile.name, "smile", 5) == 0 && smile.weight == 0.25f && smile.deltas == 2);
    TIDE_CHECK(jaw.name_length == 7 && memcmp(jaw.name, "jawOpen", 7) == 0 && jaw.weight == 0.0f && jaw.deltas == 1);
    tide_skinned_shape_delta_data delta[3];
    for (uint32_t i = 0; i < 3; i++) TIDE_REQUIRE(tide_skinned_data_shape_delta(&view, i, delta + i));
    // The triangle's corners are its vertices; x is reflected as everywhere.
    TIDE_CHECK(delta[0].vertex == 0 && vector3(delta[0].position, 0, 0.5f, 0) && vector3(delta[0].normal, 0, 0, 0));
    TIDE_CHECK(delta[1].vertex == 2 && vector3(delta[1].position, 0, 0, 0.25f) && vector3(delta[1].normal, -1, 0, 0));
    TIDE_CHECK(delta[2].vertex == 1 && vector3(delta[2].position, 0, -1, 0));
    tide_skinned_vertex_data vertex;
    TIDE_REQUIRE(tide_skinned_data_vertex(&view, 0, &vertex));
    TIDE_CHECK(vector3(vertex.position, 0, 0.125f, 0) && vertex.influences == 5);
    TIDE_REQUIRE(tide_skinned_data_vertex(&view, 2, &vertex));
    TIDE_CHECK(vector3(vertex.position, 0, 2, 0.0625f) && vector3(vertex.normal, -0.25f, 0, 1));
    TIDE_REQUIRE(tide_skinned_data_vertex(&view, 1, &vertex));
    TIDE_CHECK(vector3(vertex.position, -2, 0, 0));
    // Static meshes keep the rest geometry, shapes ignored.
    mesh_file mesh = {0};
    TIDE_REQUIRE(read_mesh(a, a_size, "shapes_ascii.fbx", &mesh));
    TIDE_CHECK(mesh.vertices == 3 && vector3(mesh.position, 0, 0, 0));
    mesh_file_free(&mesh);
    tide_skinned_data_free(ascii);
    tide_skinned_data_free(binary);
    free(a);
    free(b);
    static const char *const between[] = {"shapes/inbetween_ascii.fbx", "shapes/inbetween_binary.fbx"};
    for (uint32_t i = 0; i < 2; i++) {
        size_t size;
        uint8_t *bytes = fixture(between[i], &size), *glb = NULL;
        TIDE_REQUIRE(bytes);
        size_t glb_size = 0;
        char why[256], note[256];
        TIDE_CHECK(!fbx_file_to_glb(bytes, size, true, &glb, &glb_size, why, note) && !glb);
        TIDE_CHECK(strstr(why, "blend shape 'Blends.smile' with in-between shapes") && strstr(note, "without in-between shapes"));
        // A static mesh reads its rest geometry whatever its shapes are.
        TIDE_CHECK(read_mesh(bytes, size, between[i], &mesh));
        mesh_file_free(&mesh);
        free(bytes);
    }
}

// Euler rotation keys written wrapped (179 then -179 degrees about z) are a
// two-degree turn; the 60 Hz bake between them must not sweep back through 0.
TIDE_TEST(fbx_wrapped_euler_rotation_keys_bake_the_short_way)
{
    static const char *const names[] = {"animation/wrapped_ascii.fbx", "animation/wrapped_binary.fbx"};
    uint8_t *canonical[2] = {NULL, NULL};
    uint32_t sizes[2] = {0, 0};
    for (uint32_t f = 0; f < 2; f++) {
        size_t size;
        uint8_t *bytes = fixture(names[f], &size);
        TIDE_REQUIRE(bytes);
        TIDE_REQUIRE(decode(bytes, size, &canonical[f], &sizes[f]));
        free(bytes);
    }
    TIDE_CHECK(sizes[0] == sizes[1] && !memcmp(canonical[0], canonical[1], sizes[0]));
    tide_skinned_data_view view;
    TIDE_REQUIRE(tide_skinned_data_open(canonical[0], sizes[0], &view));
    const uint32_t bone = named_node(&view, "Bone0");
    bool found = false;
    for (uint32_t c = 0; c < view.channels; c++) {
        tide_skinned_channel_data channel;
        TIDE_REQUIRE(tide_skinned_data_channel(&view, c, &channel));
        if (channel.node != bone || channel.path != TIDE_SKINNED_ROTATION) continue;
        found = true;
        TIDE_CHECK(channel.keys >= 3u);
        float previous[4] = {0};
        for (uint32_t k = 0; k < channel.keys; k++) {
            float time, q[4], in[4], out[4];
            TIDE_REQUIRE(tide_skinned_data_key(&view, &channel, k, &time, q, in, out));
            // Every key is within a degree of a half turn about z: none near the identity.
            TIDE_CHECK(fabsf(q[2]) > 0.9999f && fabsf(q[3]) < 0.0175f);
            if (k) TIDE_CHECK(q[0] * previous[0] + q[1] * previous[1] + q[2] * previous[2] + q[3] * previous[3] > 0.9998f);
            memcpy(previous, q, sizeof previous);
        }
    }
    TIDE_CHECK(found);
    tide_skinned_data_free(canonical[0]);
    tide_skinned_data_free(canonical[1]);
}

TIDE_TEST(fbx_animation_only_ascii_and_binary_keep_clips_without_geometry)
{
    static const char *const names[] = {"clips/move_ascii.fbx", "clips/move_binary.fbx"};
    uint8_t *canonical[2] = {NULL, NULL};
    uint32_t canonical_size[2] = {0, 0};
    for (uint32_t i = 0; i < 2; i++) {
        size_t size;
        uint8_t *bytes = fixture(names[i], &size);
        TIDE_REQUIRE(bytes);
        TIDE_REQUIRE(decode(bytes, size, &canonical[i], &canonical_size[i]));
        tide_skinned_data_view view;
        TIDE_REQUIRE(tide_skinned_data_validate(canonical[i], canonical_size[i])
                     && tide_skinned_data_open(canonical[i], canonical_size[i], &view));
        TIDE_CHECK(view.vertices == 0 && view.indices == 0 && view.palettes == 0 && view.clips == 1);
        TIDE_CHECK(named_node(&view, "Root") != UINT32_MAX && named_node(&view, "Bone0") != UINT32_MAX);
        tide_skinned_clip_data clip;
        TIDE_REQUIRE(tide_skinned_data_clip(&view, 0, &clip));
        TIDE_CHECK(clip.length == 1 && clip.name_length == 4 && memcmp(clip.name, "Move", 4) == 0);
        mesh_file mesh = {0};
        char why[256], note[256];
        TIDE_CHECK(!mesh_file_read(bytes, size, names[i], NULL, NULL, &mesh, why, note));
        TIDE_CHECK(strcmp(why, "has no FBX triangles: there's nothing in it to draw") == 0
                   && strcmp(note, "export a polygon mesh") == 0);
        mesh_file_free(&mesh);
        free(bytes);
    }
    TIDE_CHECK(canonical_size[0] == canonical_size[1] && memcmp(canonical[0], canonical[1], canonical_size[0]) == 0);
    tide_skinned_data_free(canonical[0]);
    tide_skinned_data_free(canonical[1]);
}

TIDE_TEST(fbx_real_blender_and_maya_binary_exports)
{
    static const char *const names[] = {"upstream/blender_272_cube_7400_binary.fbx", "upstream/maya_game_sausage_7500_binary.fbx",
                                        "upstream/maya_anim_interpolation_7700_binary.fbx"};
    for (uint32_t i = 0; i < 3; i++) {
        size_t size;
        uint8_t *bytes = fixture(names[i], &size);
        TIDE_REQUIRE(bytes);
        mesh_file mesh;
        TIDE_REQUIRE(read_mesh(bytes, size, names[i], &mesh));
        TIDE_CHECK(mesh.vertices > 0 && mesh.indices > 0 && mesh.indices % 3u == 0);
        for (uint32_t j = 0; j < mesh.vertices * 3u; j++) TIDE_CHECK(isfinite(mesh.position[j]));
        if (i == 0) {
            float minimum[3] = {1000000, 1000000, 1000000}, maximum[3] = {-1000000, -1000000, -1000000};
            for (uint32_t j = 0; j < mesh.vertices; j++) {
                for (uint32_t k = 0; k < 3; k++) {
                    if (mesh.position[j * 3u + k] < minimum[k]) minimum[k] = mesh.position[j * 3u + k];
                    if (mesh.position[j * 3u + k] > maximum[k]) maximum[k] = mesh.position[j * 3u + k];
                }
            }
            TIDE_CHECK(vector3(minimum, -1, -1, -1) && vector3(maximum, 1, 1, 1));
        }
        mesh_file_free(&mesh);
        uint8_t *data = NULL;
        uint32_t data_size = 0;
        TIDE_REQUIRE(decode(bytes, size, &data, &data_size));
        tide_skinned_data_view view;
        TIDE_REQUIRE(tide_skinned_data_validate(data, data_size) && tide_skinned_data_open(data, data_size, &view));
        if (i == 0) TIDE_CHECK(view.vertices == 24 && view.indices == 36 && named_node(&view, "Cube") != UINT32_MAX);
        if (i == 1) {
            TIDE_CHECK(view.vertices == 88 && view.indices == 132 && view.palettes == 3 && view.influences == 88);
            TIDE_CHECK(named_node(&view, "joint1") != UINT32_MAX && named_node(&view, "joint4") != UINT32_MAX);
        }
        if (i == 2) {
            tide_skinned_clip_data clip;
            TIDE_REQUIRE(tide_skinned_data_clip(&view, 0, &clip));
            TIDE_CHECK(view.clips == 1 && clip.name_length == 8 && memcmp(clip.name, "Take 001", 8) == 0 && clip.length == 2);
        }
        tide_skinned_data_free(data);
        free(bytes);
    }
}

static const char *name_or_empty(const char *name) { return name ? name : ""; }

TIDE_TEST(fbx_metadata_catalogs_match_the_converted_model)
{
    static const char *const names[] = {"static/triangle_ascii.fbx", "static/triangle_binary.fbx", "models/rig_ascii.fbx",
        "models/rig_binary.fbx", "clips/move_ascii.fbx", "clips/move_binary.fbx", "upstream/blender_272_cube_7400_binary.fbx", "upstream/maya_game_sausage_7500_binary.fbx",
        "upstream/maya_anim_interpolation_7700_binary.fbx"};
    for (uint32_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        size_t size, glb_size = 0;
        uint8_t *bytes = fixture(names[i], &size), *glb = NULL;
        TIDE_REQUIRE(bytes);
        char why[256], note[256];
        model_metadata source, converted;
        TIDE_REQUIRE(model_metadata_read(bytes, size, names[i], &source, why));
        TIDE_REQUIRE(fbx_file_to_glb(bytes, size, true, &glb, &glb_size, why, note));
        TIDE_REQUIRE(model_metadata_read(glb, glb_size, "model.glb", &converted, why));
        TIDE_REQUIRE(source.animation_count == converted.animation_count && source.node_count == converted.node_count);
        for (uint32_t j = 0; j < source.animation_count; j++)
            TIDE_CHECK(strcmp(name_or_empty(source.animations[j]), name_or_empty(converted.animations[j])) == 0);
        for (uint32_t j = 0; j < source.node_count; j++) {
            TIDE_CHECK(source.nodes[j].parent == converted.nodes[j].parent);
            TIDE_CHECK(strcmp(name_or_empty(source.nodes[j].name), name_or_empty(converted.nodes[j].name)) == 0);
        }
        model_metadata_free(&source);
        model_metadata_free(&converted);
        free(glb);
        free(bytes);
    }
}

// These allocations stop exactly at each supplied length. Sanitizers can
// therefore catch a speculative read beyond the FBX buffer's actual end.
static void bounded(const uint8_t *bytes, const size_t size, const bool skinned)
{
    uint8_t *exact = malloc(size ? size : 1u);
    memcpy(exact, bytes, size);
    uint8_t *out = NULL;
    size_t out_size = 0;
    char why[256], note[256];
    const bool ok = fbx_file_to_glb(exact, size, skinned, &out, &out_size, why, note);
    if (ok) TIDE_CHECK(out && out_size > 0);
    else TIDE_CHECK(!out && !out_size && why[0]);
    free(out);
    free(exact);
}

TIDE_TEST(fbx_exact_length_prefixes_and_corrupt_bytes_are_bounded)
{
    static const char *const names[] = {"static/triangle_ascii.fbx", "static/triangle_binary.fbx"};
    for (uint32_t f = 0; f < 2; f++) {
        size_t size;
        uint8_t *bytes = fixture(names[f], &size);
        TIDE_REQUIRE(bytes);
        for (size_t cut = 0; cut < size; cut++) bounded(bytes, cut, cut % 2u != 0);
        for (size_t at = 0; at < size; at++) {
            bytes[at] ^= 0xffu;
            bounded(bytes, size, at % 2u != 0);
            bytes[at] ^= 0xffu;
        }
        free(bytes);
    }
}

TIDE_TEST(fbx_malformed_file_and_runtime_source_decode_are_refused)
{
    size_t size;
    uint8_t *bytes = fixture("bad/malformed.fbx", &size);
    TIDE_REQUIRE(bytes);
    uint8_t *out = NULL;
    size_t out_size = 0;
    char why[256], note[256];
    TIDE_CHECK(!fbx_file_to_glb(bytes, size, true, &out, &out_size, why, note));
    TIDE_CHECK(!out && !out_size && why[0]);
    free(bytes);
    bytes = fixture("models/rig_binary.fbx", &size);
    TIDE_REQUIRE(bytes);
    uint32_t model_size = 0;
    TIDE_CHECK(!tide_skinned_data_decode(bytes, size, true, NULL, NULL, &out, &model_size, why, note));
    TIDE_CHECK(!out && !model_size);
    free(bytes);
}

int ufbx_isnan(double x); // ufbx_math.c, the portable math ufbx is built with

TIDE_TEST(fbx_portable_isnan_reads_a_low_word_of_0x80000000_without_overflow)
{
    // Scene finalization asks this of constant curve values. Negating the
    // low word as a signed int overflowed for 0x80000000; sanitized builds
    // stop there (UBSan found it fuzzing fbx_file_to_glb).
    static const uint64_t numbers[] = {UINT64_C(0x3ff0000080000000), UINT64_C(0xbff0000080000000), UINT64_C(0x0000000080000000)};
    static const uint64_t nans[] = {UINT64_C(0x7ff0000080000000), UINT64_C(0xfff0000080000000)};
    for (uint32_t i = 0; i < 3; i++) {
        double x;
        memcpy(&x, &numbers[i], sizeof x);
        TIDE_CHECK(!ufbx_isnan(x));
    }
    for (uint32_t i = 0; i < 2; i++) {
        double x;
        memcpy(&x, &nans[i], sizeof x);
        TIDE_CHECK(ufbx_isnan(x));
    }
}

// One star-shaped polygon of `corners` corners, alternately at radius 1 and 1/2.
static char *ngon_fbx(const uint32_t corners)
{
    const size_t room = 4096u + (size_t)corners * 48u;
    char *text = malloc(room);
    if (!text) return NULL;
    size_t at = (size_t)snprintf(text, room, "; FBX 7.4.0 project file\nFBXHeaderExtension:  {\n  FBXHeaderVersion: 1003\n"
        "  FBXVersion: 7400\n}\nObjects:  {\n  Geometry: 1000, \"Geometry::Star\", \"Mesh\" {\n    Vertices: *%u {\n      a: ",
        corners * 3u);
    for (uint32_t i = 0; i < corners; i++) {
        const double radius = i % 2u ? 0.5 : 1.0, angle = 6.283185307179586 * i / corners;
        at += (size_t)snprintf(text + at, room - at, "%s%.6f,%.6f,0", i ? "," : "", radius * cos(angle), radius * sin(angle));
    }
    at += (size_t)snprintf(text + at, room - at, "\n    }\n    PolygonVertexIndex: *%u {\n      a: ", corners);
    for (uint32_t i = 0; i < corners; i++)
        at += (size_t)snprintf(text + at, room - at, "%s%d", i ? "," : "", i + 1u < corners ? (int)i : -(int)i - 1);
    snprintf(text + at, room - at, "\n    }\n  }\n  Model: 2000, \"Model::Star\", \"Mesh\" {\n    Version: 232\n  }\n}\n"
        "Connections:  {\n  C: \"OO\", 1000, 2000\n  C: \"OO\", 2000, 0\n}\n");
    return text;
}

TIDE_TEST(fbx_polygon_triangulation_work_is_bounded_before_cutting)
{
    // Cutting a polygon of n corners takes about n * n steps. A 4,000-corner
    // polygon is well within TIDE_FBX_MAX_TRIANGULATION; one of 70,000 corners
    // (4.9 billion) is refused before ufbx cuts it, which would take seconds.
    static const uint32_t sizes[] = {4000u, 70000u};
    for (uint32_t i = 0; i < 2; i++) {
        char *text = ngon_fbx(sizes[i]);
        TIDE_REQUIRE(text);
        for (uint32_t skinned = 0; skinned < 2; skinned++) {
            uint8_t *glb = NULL;
            size_t glb_size = 0;
            char why[256], note[256];
            const bool ok = fbx_file_to_glb((const uint8_t *)text, strlen(text), skinned != 0, &glb, &glb_size, why, note);
            if (i == 0) {
                if (!ok) printf("    %u corners: %s\n", sizes[i], why);
                TIDE_CHECK(ok && glb && glb_size);
            } else {
                TIDE_CHECK(!ok && !glb && !glb_size && strstr(why, "steps to cut its polygons into triangles (each polygon's corners squared), more than the 4,294,967,296 one model holds (TIDE_FBX_MAX_TRIANGULATION)") && strstr(note, "split"));
            }
            free(glb);
        }
        if (i == 0) {
            mesh_file mesh;
            TIDE_REQUIRE(read_mesh((const uint8_t *)text, strlen(text), "star.fbx", &mesh));
            TIDE_CHECK(mesh.indices == (sizes[i] - 2u) * 3u);
            mesh_file_free(&mesh);
        }
        free(text);
    }
}

static bool write_fixture(const char *path, const uint8_t *bytes, const size_t size)
{
    FILE *file = fopen(path, "wb");
    if (!file) return false;
    const bool ok = fwrite(bytes, 1, size, file) == size;
    if (fclose(file) != 0 || !ok) return false;
#ifdef _WIN32
    struct _utimbuf time = {1700000000, 1700000000};
    return _utime(path, &time) == 0;
#else
    const struct utimbuf time = {1700000000, 1700000000};
    return utime(path, &time) == 0;
#endif
}

TIDE_TEST(fbx_detection_keeps_gltf_strings_and_glb_magic_on_the_gltf_path)
{
    static const char json[] = "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],"
        "\"nodes\":[{\"name\":\"FBXHeaderExtension\",\"mesh\":0,\"extras\":{\"source\":\"FBXHeaderExtension: {\"}}],"
        "\"materials\":[{\"name\":\"FBXHeaderExtension\"}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"material\":0}]}],"
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteLength\":36}],"
        "\"buffers\":[{\"byteLength\":36,\"uri\":\"data:application/octet-stream;base64,"
        "AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA\"}]}";
    static const char *const names[] = {"triangle.gltf", "https://models.invalid/triangle.gltf?download=1#FBXHeaderExtension",
        "https://models.invalid/triangle.GLTF?source=FBXHeaderExtension#mesh"};
    for (uint32_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        TIDE_CHECK(!fbx_file_is((const uint8_t *)json, sizeof json - 1u, names[i]));
        mesh_file mesh;
        TIDE_REQUIRE(read_mesh((const uint8_t *)json, sizeof json - 1u, names[i], &mesh));
        TIDE_CHECK(mesh.vertices == 3 && mesh.indices == 3);
        mesh_file_free(&mesh);
    }
    uint8_t *canonical = NULL;
    uint32_t canonical_size = 0u;
    char why[256], note[256];
    TIDE_REQUIRE(tide_skinned_data_decode(json, sizeof json - 1u, false, NULL, NULL,
        &canonical, &canonical_size, why, note));
    tide_skinned_data_view view;
    TIDE_REQUIRE(tide_skinned_data_open(canonical, canonical_size, &view));
    TIDE_CHECK(view.vertices == 3 && view.indices == 3 && named_node(&view, "FBXHeaderExtension") != UINT32_MAX);
    tide_skinned_data_free(canonical);

    size_t source_size = 0u, glb_size = 0u;
    uint8_t *source = fixture("models/rig_binary.fbx", &source_size), *glb = NULL;
    TIDE_REQUIRE(source);
    TIDE_REQUIRE(fbx_file_to_glb(source, source_size, true, &glb, &glb_size, why, note));
    TIDE_CHECK(!fbx_file_is(glb, glb_size, "anything.fbx"));
    TIDE_CHECK(!fbx_file_is(glb, glb_size, "https://models.invalid/anything.FBX?download=1#mesh"));
    mesh_file mesh;
    TIDE_REQUIRE(read_mesh(glb, glb_size, "anything.fbx", &mesh));
    TIDE_CHECK(mesh.vertices == 3 && mesh.indices == 3);
    mesh_file_free(&mesh);

    char declaring[1024], json_path[1024], glb_path[1024];
    snprintf(declaring, sizeof declaring, "%s/format-main.tide", FBX_WORK);
    snprintf(json_path, sizeof json_path, "%s/actually-gltf.gltf", FBX_WORK);
    snprintf(glb_path, sizeof glb_path, "%s/actually-glb.fbx", FBX_WORK);
    TIDE_REQUIRE(folder_make(FBX_WORK));
    TIDE_REQUIRE(write_fixture(json_path, (const uint8_t *)json, sizeof json - 1u));
    TIDE_REQUIRE(write_fixture(glb_path, glb, glb_size));
    asset_files_forget();
    diag_reset();
    static const asset_kind kinds[] = {ASSET_MESH, ASSET_SKINNED};
    static const char *const paths[] = {"actually-gltf.gltf", "actually-glb.fbx"};
    for (uint32_t i = 0; i < 2u; i++) {
        for (uint32_t j = 0; j < 2u; j++) {
            asset_value *value = asset_resolve(declaring, str_from(paths[j]), kinds[i], false, false, (loc){0}, NULL);
            TIDE_REQUIRE(value && value->count == 1);
            TIDE_CHECK(value->items[0].mesh.vertices == 3 && value->items[0].mesh.indices == 3);
            if (kinds[i] == ASSET_SKINNED) {
                TIDE_REQUIRE(tide_skinned_data_open(value->items[0].skinned, value->items[0].skinned_size, &view));
                TIDE_CHECK(named_node(&view, j == 0u ? "FBXHeaderExtension" : "Bone0") != UINT32_MAX);
            }
        }
    }
    asset_files_forget();
    arena_reset();
    diag_reset();
    remove(json_path);
    remove(glb_path);
    free(glb);
    free(source);
}

TIDE_TEST(fbx_ascii_detection_requires_a_declaration_outside_comments_strings_and_nested_nodes)
{
    static const char *const other[] = {"; FBXHeaderExtension: { is only a comment\nObjects: { }\n",
        "Object: {\nFBXHeaderExtension: { }\n}\n", "\"FBXHeaderExtension: {\"\n", "FBXHeaderExtensionElse: { }\n",
        "FBXHeaderExtension\n"};
    for (uint32_t i = 0; i < sizeof other / sizeof other[0]; i++)
        TIDE_CHECK(!fbx_file_is((const uint8_t *)other[i], strlen(other[i]), "cached-download"));
    size_t size;
    uint8_t *bytes = fixture("static/triangle_ascii.fbx", &size);
    TIDE_REQUIRE(bytes);
    TIDE_CHECK(fbx_file_is(bytes, size, "cached-download"));
    static const char prefix[] = "\xef\xbb\xbf; an ignored quote \" and FBXHeaderExtension: {\r\n";
    uint8_t *prefixed = malloc(sizeof prefix - 1u + size);
    TIDE_REQUIRE(prefixed);
    memcpy(prefixed, prefix, sizeof prefix - 1u);
    memcpy(prefixed + sizeof prefix - 1u, bytes, size);
    TIDE_CHECK(fbx_file_is(prefixed, sizeof prefix - 1u + size, "cached-download"));
    free(prefixed);
    free(bytes);
}

TIDE_TEST(fbx_static_and_skinned_cache_notifications_override_unchanged_metadata)
{
    size_t size;
    uint8_t *bytes = fixture("models/rig_ascii.fbx", &size);
    TIDE_REQUIRE(bytes);
    bytes = realloc(bytes, size + 1u);
    TIDE_REQUIRE(bytes);
    bytes[size] = 0;
    char *position = strstr((char *)bytes, "0,0,0,2,0,0,0,2,0");
    TIDE_REQUIRE(position);
    position += 6;
    char source[1024], model[1024];
    snprintf(source, sizeof source, "%s/main.tide", FBX_WORK);
    snprintf(model, sizeof model, "%s/shape.fbx", FBX_WORK);
    TIDE_REQUIRE(folder_make(FBX_WORK));
    static const asset_kind kinds[] = {ASSET_MESH, ASSET_SKINNED};
    for (uint32_t i = 0; i < 2; i++) {
        asset_files_forget();
        diag_reset();
        *position = '2';
        TIDE_REQUIRE(write_fixture(model, bytes, size));
        unsigned long long length, stamp, changed_length, changed_stamp;
        TIDE_REQUIRE(folder_file_info(model, &length, &stamp));
        asset_value *value = asset_resolve(source, str_from("shape.fbx"), kinds[i], false, false, (loc){0}, NULL);
        TIDE_REQUIRE(value && value->count == 1 && value->items[0].mesh.vertices == 3);
        uint8_t original[32];
        memcpy(original, value->items[0].mesh.hash, sizeof original);
        *position = '3';
        TIDE_REQUIRE(write_fixture(model, bytes, size));
        TIDE_REQUIRE(folder_file_info(model, &changed_length, &changed_stamp));
        TIDE_CHECK(length == changed_length && stamp == changed_stamp);
        value = asset_resolve(source, str_from("shape.fbx"), kinds[i], false, false, (loc){0}, NULL);
        TIDE_REQUIRE(value && value->count == 1);
        TIDE_CHECK(memcmp(original, value->items[0].mesh.hash, sizeof original) == 0);
        asset_files_changed(model);
        value = asset_resolve(source, str_from("shape.fbx"), kinds[i], false, false, (loc){0}, NULL);
        TIDE_REQUIRE(value && value->count == 1);
        TIDE_CHECK(memcmp(original, value->items[0].mesh.hash, sizeof original) != 0);
        *position = '2';
        TIDE_REQUIRE(write_fixture(model, bytes, size));
        asset_files_changed(model);
        value = asset_resolve(source, str_from("shape.fbx"), kinds[i], false, false, (loc){0}, NULL);
        TIDE_REQUIRE(value && value->count == 1);
        TIDE_CHECK(memcmp(original, value->items[0].mesh.hash, sizeof original) == 0);
    }
    asset_files_forget();
    arena_reset();
    diag_reset();
    remove(model);
    free(bytes);
}

