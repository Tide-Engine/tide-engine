// The mesh files' reader (compiler/src/mesh_files.h), on its own: the numbers
// it reads, what it refuses, and that a file cut anywhere or wrong anywhere is
// never read past its bytes. What it makes of whole files is the end-to-end
// test's (e2e/asset_meshes).

#include <math.h>
#include <stdarg.h>
#include <float.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mesh_files.h"
#include "tide_test.h"

static bool number(const char *text, float *out)
{
    return mesh_number(text, text + strlen(text), out);
}

// A decimal number is the float nearest it, by the basic operations alone:
// the same bits on every machine that builds a game.
TIDE_TEST(mesh_numbers_are_the_nearest_floats)
{
    static const struct {
        const char *text;
        float is;
    } cases[] = {
        {"0", 0.0f},
        {"1", 1.0f},
        {"-1", -1.0f},
        {"+2.5", 2.5f},
        {"0.1", 0x1.99999ap-4f},
        {"3.14159265", 0x1.921fb6p+1f},
        {".5", 0.5f},
        {"5.", 5.0f},
        {"1e3", 1000.0f},
        {"1E-3", 0x1.0624dep-10f},
        {"-2.5e+2", -250.0f},
        {"16777217", 16777216.0f},
        {"0.000001", 0x1.0c6f7ap-20f},
        {"123456789012345678901234567890", 0x1.8ee910p+96f}, // More digits than a number keeps
        {"0.00000000000000000000000000000000000000000001", 0x1.cp-147f}, // Seven of the smallest float there is
        {"1e-45", 0x1p-149f},
        {"1e400", 0.0f}, // What no float holds is none
        {"-1e400", 0.0f},
        {"1e-400", 0.0f},
        {"340282346638528859811704183484516925440", 0x1.fffffep+127f}, // The biggest
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        float read = -7.0f;
        TIDE_CHECK(number(cases[i].text, &read));
        if (read != cases[i].is) printf("    %s read as %a, not %a\n", cases[i].text, (double)read, (double)cases[i].is);
        TIDE_CHECK(read == cases[i].is);
    }
    static const char *const wrong[] = {"", "-", "+", ".", "e5", "1e", "1e+", "1.2.3", "0x10", "nan", "inf", "1 ", " 1", "1f", "--1"};
    for (size_t i = 0; i < sizeof wrong / sizeof wrong[0]; i++) {
        float read;
        TIDE_CHECK(!number(wrong[i], &read));
    }
}

// A .glb: its header, its description, and its bytes.
static uint8_t *make_glb(const char *json, const void *bin, const uint32_t bin_size, size_t *size)
{
    const uint32_t json_size = ((uint32_t)strlen(json) + 3u) & ~3u, padded = (bin_size + 3u) & ~3u;
    *size = 12u + 8u + json_size + 8u + padded;
    uint8_t *out = malloc(*size);
    memset(out, 0x20, *size);
    const uint32_t header[5] = {0x46546C67u, 2u, (uint32_t)*size, json_size, 0x4E4F534Au};
    memcpy(out, header, sizeof header);
    memcpy(out + 20u, json, strlen(json));
    const uint32_t chunk[2] = {padded, 0x004E4942u};
    memcpy(out + 20u + json_size, chunk, sizeof chunk);
    memset(out + 28u + json_size, 0, padded);
    memcpy(out + 28u + json_size, bin, bin_size);
    return out;
}

// A triangle's positions and normals, and its three indices after them, as a buffer's bytes.
typedef struct triangle_bytes {
    float positions[9];
    float normals[9];
    uint16_t indices[4];
} triangle_bytes;

static const triangle_bytes triangle = {{0, 0, 0, 1, 0, 0, 0, 1, 0}, {0, 0, 1, 0, 0, 1, 0, 0, 1}, {0, 1, 2, 0}};

#define TRIANGLE_VIEWS "\"bufferViews\":[{\"buffer\":0,\"byteLength\":36},{\"buffer\":0,\"byteOffset\":36,\"byteLength\":36},"         \
                       "{\"buffer\":0,\"byteOffset\":72,\"byteLength\":6}],\"buffers\":[{\"byteLength\":80}]"
#define TRIANGLE_ACCESSORS "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"                 \
                           "{\"bufferView\":1,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"                               \
                           "{\"bufferView\":2,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}],"
#define TRIANGLE_MESH "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1},\"indices\":2}]}],"

static const char *const triangle_json = "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
                                         TRIANGLE_MESH TRIANGLE_ACCESSORS TRIANGLE_VIEWS "}";

// The same triangle, whose index accessor starts with zeros or dense data,
// and has its last two values replaced by a sparse accessor. Sparse selector
// types and the accessor's value type are independent: each can have 1, 2 or
// 4 bytes. Both sparse views also have an offset within them.
static uint8_t *make_sparse_triangle(const uint32_t type, const uint32_t selector_type, const bool dense,
                                     const char *sparse_as_given, const uint32_t first_selector, size_t *size)
{
    uint8_t bytes[112] = {0};
    memcpy(bytes, triangle.positions, sizeof triangle.positions);
    memcpy(bytes + 36u, triangle.normals, sizeof triangle.normals);
    const uint32_t each = type == 5121u ? 1u : type == 5123u ? 2u : 4u;
    const uint32_t selector_bytes = selector_type == 5121u ? 1u : selector_type == 5123u ? 2u : 4u;
    static const uint32_t initial[3] = {0u, 2u, 1u}, values[2] = {1u, 2u};
    const uint32_t selectors[2] = {first_selector, 2u};
    for (uint32_t i = 0; i < 3u; i++) memcpy(bytes + 72u + i * each, &initial[i], each);
    for (uint32_t i = 0; i < 2u; i++) {
        memcpy(bytes + 88u + i * selector_bytes, &selectors[i], selector_bytes);
        memcpy(bytes + 100u + i * each, &values[i], each);
    }
    char sparse[256];
    snprintf(sparse, sizeof sparse, "{\"count\":2,\"indices\":{\"bufferView\":3,\"byteOffset\":4,\"componentType\":%u},"
                                   "\"values\":{\"bufferView\":4,\"byteOffset\":4}}", (unsigned)selector_type);
    char json[2048];
    snprintf(json, sizeof json, "{\"asset\":{\"version\":\"2.0\"}," TRIANGLE_MESH
                                "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
                                "{\"bufferView\":1,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
                                "{%s\"componentType\":%u,\"count\":3,\"type\":\"SCALAR\",\"sparse\":%s}],"
                                "\"bufferViews\":[{\"buffer\":0,\"byteLength\":36},{\"buffer\":0,\"byteOffset\":36,\"byteLength\":36},"
                                "{\"buffer\":0,\"byteOffset\":72,\"byteLength\":12},{\"buffer\":0,\"byteOffset\":84,\"byteLength\":12},"
                                "{\"buffer\":0,\"byteOffset\":96,\"byteLength\":16}],\"buffers\":[{\"byteLength\":112}]}",
             dense ? "\"bufferView\":2," : "", (unsigned)type, sparse_as_given ? sparse_as_given : sparse);
    return make_glb(json, bytes, sizeof bytes, size);
}

// Reads `size` bytes kept in memory of exactly that size, so a read past them is one past an allocation.
static bool read_exactly(const uint8_t *data, const size_t size, const char *name, mesh_file *mesh, char why[256])
{
    uint8_t *exact = malloc(size ? size : 1u);
    memcpy(exact, data, size);
    char note[256];
    const bool ok = mesh_file_read(exact, size, name, NULL, NULL, mesh, why, note);
    free(exact);
    return ok;
}

typedef struct tangent_bytes {
    float position[8][3], normal[8][3], uv[8][2], uv2[8][2], color[8][4], tangent[8][4];
    uint16_t index[12];
} tangent_bytes;

static tangent_bytes tangent_plane(void)
{
    tangent_bytes bytes = {.position = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}},
                           .uv = {{0, 0}, {1, 0}, {1, 1}, {0, 1}}, .index = {0, 1, 2, 0, 2, 3}};
    for (uint32_t i = 0; i < 8u; i++) {
        bytes.normal[i][2] = 1.0f;
        bytes.tangent[i][0] = bytes.tangent[i][3] = 1.0f;
        bytes.uv2[i][0] = (float)i / 8.0f;
        bytes.uv2[i][1] = 0.25f;
        bytes.color[i][0] = (float)i / 8.0f;
        bytes.color[i][1] = bytes.color[i][2] = 0.5f;
        bytes.color[i][3] = 0.75f;
    }
    return bytes;
}

static bool read_tangent_mesh(const tangent_bytes *bytes, const uint32_t vertices, const uint32_t indices,
                              const bool normals, const bool uvs, const int authored, const char *node,
                              mesh_file *mesh, char why[256])
{
    char json[3072];
    snprintf(json, sizeof json, "{\"asset\":{\"version\":\"2.0\"},\"nodes\":[{\"mesh\":0%s}],"
                                "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"TEXCOORD_1\":3,\"COLOR_0\":4%s%s%s},\"indices\":6}%s]}],"
                                "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":%u,\"type\":\"VEC3\"},"
                                "{\"bufferView\":1,\"componentType\":5126,\"count\":%u,\"type\":\"VEC3\"},"
                                "{\"bufferView\":2,\"componentType\":5126,\"count\":%u,\"type\":\"VEC2\"},"
                                "{\"bufferView\":3,\"componentType\":5126,\"count\":%u,\"type\":\"VEC2\"},"
                                "{\"bufferView\":4,\"componentType\":5126,\"count\":%u,\"type\":\"VEC4\"},"
                                "{\"bufferView\":5,\"componentType\":5126,\"count\":%u,\"type\":\"VEC4\"},"
                                "{\"bufferView\":6,\"componentType\":5123,\"count\":%u,\"type\":\"SCALAR\"}],"
                                "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":96},"
                                "{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":96},"
                                "{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":64},"
                                "{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":64},"
                                "{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":128},"
                                "{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":128},"
                                "{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":24}],\"buffers\":[{\"byteLength\":%u}]}",
             node ? node : "", normals ? ",\"NORMAL\":1" : "", uvs ? ",\"TEXCOORD_0\":2" : "", authored ? ",\"TANGENT\":5" : "",
             authored == 2 ? ",{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":2,\"TEXCOORD_1\":3,\"COLOR_0\":4},\"indices\":6}" : "",
             (unsigned)vertices, (unsigned)vertices, (unsigned)vertices, (unsigned)vertices, (unsigned)vertices,
             (unsigned)vertices, (unsigned)indices, (unsigned)offsetof(tangent_bytes, position), (unsigned)offsetof(tangent_bytes, normal),
             (unsigned)offsetof(tangent_bytes, uv), (unsigned)offsetof(tangent_bytes, uv2), (unsigned)offsetof(tangent_bytes, color),
             (unsigned)offsetof(tangent_bytes, tangent), (unsigned)offsetof(tangent_bytes, index), (unsigned)sizeof *bytes);
    size_t size;
    uint8_t *glb = make_glb(json, bytes, sizeof *bytes, &size);
    mesh_file canonical;
    bool ok = read_exactly(glb, size, "tangents.glb", &canonical, why);
    if (ok) {
        char note[256];
        ok = mesh_file_with_tangents(&canonical, mesh, why, note);
        mesh_file_free(&canonical);
    }
    free(glb);
    return ok;
}

static bool tangent_is(const float *t, const float x, const float y, const float z, const float side)
{
    return fabsf(t[0] - x) < 0.00001f && fabsf(t[1] - y) < 0.00001f && fabsf(t[2] - z) < 0.00001f && t[3] == side;
}

static void check_tangent_basis(const mesh_file *mesh)
{
    for (uint32_t i = 0; i < mesh->vertices; i++) {
        const float *t = mesh->tangent + i * 4u, *n = mesh->normal + i * 3u;
        for (uint32_t k = 0; k < 4u; k++) TIDE_CHECK(isfinite(t[k]));
        TIDE_CHECK(fabsf(t[0] * t[0] + t[1] * t[1] + t[2] * t[2] - 1.0f) < 0.00001f);
        if (isfinite(n[0]) && isfinite(n[1]) && isfinite(n[2])) {
            TIDE_CHECK(fabsf(t[0] * n[0] + t[1] * n[1] + t[2] * n[2]) < 0.00001f);
        }
        TIDE_CHECK(t[3] == -1.0f || t[3] == 1.0f);
    }
}

static void check_tangent_uv_directions(const mesh_file *mesh)
{
    for (uint32_t i = 0; i < mesh->indices; i += 3u) {
        const float *p = mesh->position + mesh->index[i] * 3u, *q = mesh->position + mesh->index[i + 1u] * 3u,
                    *r = mesh->position + mesh->index[i + 2u] * 3u;
        const float *a = mesh->uv + mesh->index[i] * 2u, *b = mesh->uv + mesh->index[i + 1u] * 2u,
                    *c = mesh->uv + mesh->index[i + 2u] * 2u;
        const double du1 = (double)b[0] - a[0], dv1 = (double)b[1] - a[1], du2 = (double)c[0] - a[0], dv2 = (double)c[1] - a[1];
        const double side = du1 * dv2 - du2 * dv1 < 0.0 ? -1.0 : 1.0;
        double u[3], v[3];
        for (uint32_t k = 0; k < 3u; k++) {
            u[k] = (((double)q[k] - p[k]) * dv2 - ((double)r[k] - p[k]) * dv1) * side;
            v[k] = (((double)r[k] - p[k]) * du1 - ((double)q[k] - p[k]) * du2) * side;
        }
        for (uint32_t j = 0; j < 3u; j++) {
            const float *n = mesh->normal + mesh->index[i + j] * 3u, *t = mesh->tangent + mesh->index[i + j] * 4u;
            const double bitangent[3] = {(double)t[3] * (n[1] * t[2] - n[2] * t[1]),
                                         (double)t[3] * (n[2] * t[0] - n[0] * t[2]),
                                         (double)t[3] * (n[0] * t[1] - n[1] * t[0])};
            TIDE_CHECK(t[0] * u[0] + t[1] * u[1] + t[2] * u[2] > 0.0);
            TIDE_CHECK(bitangent[0] * v[0] + bitangent[1] * v[1] + bitangent[2] * v[2] > 0.0);
        }
    }
}

// All indexed non-tangent values survive, including vertices appended for a
// tangent seam. This also pins the exact triangle winding/order and bounds.
static void check_indexed_geometry(const mesh_file *mesh, const mesh_file *control)
{
    TIDE_REQUIRE(mesh->indices == control->indices);
    TIDE_CHECK(mesh->parts == control->parts && mesh->has_normals == control->has_normals && mesh->has_uvs == control->has_uvs);
    for (uint32_t i = 0; i < mesh->indices; i++) {
        const uint32_t a = mesh->index[i], b = control->index[i];
        TIDE_REQUIRE(a < mesh->vertices && b < control->vertices);
        TIDE_CHECK(memcmp(mesh->position + a * 3u, control->position + b * 3u, 3u * sizeof(float)) == 0);
        TIDE_CHECK(memcmp(mesh->normal + a * 3u, control->normal + b * 3u, 3u * sizeof(float)) == 0);
        TIDE_CHECK(memcmp(mesh->uv + a * 2u, control->uv + b * 2u, 2u * sizeof(float)) == 0);
        TIDE_CHECK(memcmp(mesh->uv2 + a * 2u, control->uv2 + b * 2u, 2u * sizeof(float)) == 0);
        TIDE_CHECK(memcmp(mesh->color + a * 4u, control->color + b * 4u, 4u * sizeof(float)) == 0);
    }
}

TIDE_TEST(mesh_missing_tangents_form_the_uv_basis_without_changing_geometry)
{
    const tangent_bytes bytes = tangent_plane();
    mesh_file mesh, control;
    char why[256];
    TIDE_REQUIRE(read_tangent_mesh(&bytes, 4u, 6u, true, true, false, NULL, &mesh, why));
    TIDE_REQUIRE(read_tangent_mesh(&bytes, 4u, 6u, true, true, true, NULL, &control, why));
    TIDE_CHECK(mesh.vertices == 4u && !mesh.has_tangents && control.has_tangents);
    TIDE_CHECK(memcmp(mesh.index, control.index, 6u * sizeof(uint32_t)) == 0);
    check_indexed_geometry(&mesh, &control);
    check_tangent_basis(&mesh);
    check_tangent_uv_directions(&mesh);
    for (uint32_t i = 0; i < mesh.vertices; i++) TIDE_CHECK(tangent_is(mesh.tangent + i * 4u, -1, 0, 0, -1));
    mesh_file_free(&control);
    mesh_file_free(&mesh);
}

TIDE_TEST(mesh_missing_tangents_split_only_shared_mirrored_handedness)
{
    tangent_bytes bytes = tangent_plane();
    bytes.uv[3][0] = 2.0f;
    mesh_file mesh, control, again;
    char why[256];
    TIDE_REQUIRE(read_tangent_mesh(&bytes, 4u, 6u, true, true, false, NULL, &mesh, why));
    TIDE_REQUIRE(read_tangent_mesh(&bytes, 4u, 6u, true, true, true, NULL, &control, why));
    TIDE_CHECK(mesh.vertices == 6u && control.vertices == 4u);
    check_indexed_geometry(&mesh, &control);
    check_tangent_basis(&mesh);
    check_tangent_uv_directions(&mesh);
    for (uint32_t i = 0; i < 6u; i++) {
        const float *t = mesh.tangent + mesh.index[i] * 4u;
        TIDE_CHECK(i < 3u ? tangent_is(t, -1, 0, 0, -1) : tangent_is(t, 1, 0, 0, 1));
    }
    TIDE_CHECK(mesh.index[0] != mesh.index[3] && mesh.index[1] != mesh.index[5]);
    TIDE_REQUIRE(read_tangent_mesh(&bytes, 4u, 6u, true, true, false, NULL, &again, why));
    TIDE_CHECK(memcmp(mesh.hash, again.hash, sizeof mesh.hash) == 0);
    TIDE_CHECK(memcmp(mesh.index, again.index, 6u * sizeof(uint32_t)) == 0);
    TIDE_CHECK(memcmp(mesh.tangent, again.tangent, mesh.vertices * 4u * sizeof(float)) == 0);
    mesh_file_free(&again);
    mesh_file_free(&control);
    mesh_file_free(&mesh);
}

TIDE_TEST(mesh_missing_tangents_keep_authored_uv_seams_separate)
{
    tangent_bytes bytes = tangent_plane();
    memcpy(bytes.position[3], bytes.position[0], sizeof bytes.position[0]);
    memcpy(bytes.position[4], bytes.position[2], sizeof bytes.position[0]);
    bytes.position[5][1] = 1.0f;
    bytes.uv[3][0] = bytes.uv[3][1] = 0.0f;
    bytes.uv[4][0] = bytes.uv[4][1] = bytes.uv[5][0] = 1.0f;
    bytes.index[3] = 3u, bytes.index[4] = 4u, bytes.index[5] = 5u;
    mesh_file mesh, control;
    char why[256];
    TIDE_REQUIRE(read_tangent_mesh(&bytes, 6u, 6u, true, true, false, NULL, &mesh, why));
    TIDE_REQUIRE(read_tangent_mesh(&bytes, 6u, 6u, true, true, true, NULL, &control, why));
    TIDE_CHECK(mesh.vertices == 6u);
    check_indexed_geometry(&mesh, &control);
    check_tangent_basis(&mesh);
    for (uint32_t i = 0; i < 6u; i++) TIDE_CHECK(i < 3u ? tangent_is(mesh.tangent + i * 4u, -1, 0, 0, -1)
                                                                     : tangent_is(mesh.tangent + i * 4u, 0, 1, 0, 1));
    mesh_file_free(&control);
    mesh_file_free(&mesh);
}

TIDE_TEST(mesh_missing_tangents_weight_directions_by_corner_angle)
{
    tangent_bytes bytes = tangent_plane();
    const float position[5][3] = {{0, 0, 0}, {2, 0, 0}, {0, 2, 0}, {0, 1, 0}, {-1, 0, 0}};
    const float uv[5][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 0}, {0, 1}};
    memcpy(bytes.position, position, sizeof position);
    memcpy(bytes.uv, uv, sizeof uv);
    bytes.index[4] = 3u, bytes.index[5] = 4u;
    mesh_file mesh;
    char why[256];
    TIDE_REQUIRE(read_tangent_mesh(&bytes, 5u, 6u, true, true, false, NULL, &mesh, why));
    TIDE_CHECK(mesh.vertices == 5u);
    check_tangent_basis(&mesh);
    const float diagonal = 0.7071067811865475f;
    TIDE_CHECK(tangent_is(mesh.tangent, -diagonal, diagonal, 0, -1));
    mesh_file_free(&mesh);
}

TIDE_TEST(mesh_missing_tangents_keep_the_uv_direction_at_narrow_valid_corners)
{
    static const float widths[] = {0.0001f, 1e-10f, 1e-30f, 1e-30f};
    for (uint32_t scenario = 0; scenario < 4u; scenario++) {
        tangent_bytes bytes = tangent_plane();
        memset(bytes.position, 0, sizeof bytes.position);
        memset(bytes.uv, 0, sizeof bytes.uv);
        bytes.position[1][1] = bytes.position[2][1] = scenario == 3u ? 3e38f : 1.0f;
        bytes.position[2][0] = widths[scenario];
        bytes.uv[1][0] = bytes.uv[2][0] = bytes.uv[2][1] = 1.0f;
        for (uint32_t i = 0; i < 3u; i++) bytes.normal[i][2] = -1.0f;
        mesh_file mesh;
        char why[256];
        TIDE_REQUIRE(read_tangent_mesh(&bytes, 3u, 3u, true, true, false, NULL, &mesh, why));
        TIDE_CHECK(mesh.vertices == 3u);
        check_tangent_basis(&mesh);
        check_tangent_uv_directions(&mesh);
        for (uint32_t i = 0; i < mesh.vertices; i++) TIDE_CHECK(tangent_is(mesh.tangent + i * 4u, 0, 1, 0, -1));
        mesh_file_free(&mesh);
    }
}

TIDE_TEST(mesh_missing_tangents_are_finite_for_degenerate_and_extreme_inputs)
{
    for (uint32_t scenario = 0; scenario < 9u; scenario++) {
        tangent_bytes bytes = tangent_plane();
        if (scenario == 1u) memset(bytes.uv, 0, sizeof bytes.uv);
        if (scenario == 2u || scenario == 5u) memset(bytes.position, 0, sizeof bytes.position);
        if (scenario == 3u) for (uint32_t i = 0; i < 4u; i++) bytes.position[i][1] = 0.0f;
        if (scenario == 4u) memset(bytes.normal, 0, sizeof bytes.normal);
        if (scenario == 6u) for (uint32_t i = 0; i < 4u; i++) for (uint32_t k = 0; k < 2u; k++) bytes.uv[i][k] *= 1e-30f;
        if (scenario >= 7u) for (uint32_t i = 0; i < 4u; i++) for (uint32_t k = 0; k < 3u; k++) bytes.position[i][k] *= scenario == 7u ? 1e-30f : FLT_MAX;
        mesh_file mesh;
        char why[256];
        TIDE_REQUIRE(read_tangent_mesh(&bytes, 4u, 6u, scenario != 5u, scenario != 0u, false, NULL, &mesh, why));
        TIDE_CHECK(mesh.vertices == (scenario == 5u ? 6u : 4u));
        check_tangent_basis(&mesh);
        for (uint32_t i = 0; i < mesh.vertices; i++) TIDE_CHECK(scenario < 4u || scenario == 5u
            ? tangent_is(mesh.tangent + i * 4u, 1, 0, 0, 1) : tangent_is(mesh.tangent + i * 4u, -1, 0, 0, -1));
        mesh_file_free(&mesh);
    }
}

TIDE_TEST(mesh_missing_tangents_handle_flat_and_nearly_parallel_normals)
{
    tangent_bytes bytes = tangent_plane();
    mesh_file flat;
    char why[256];
    TIDE_REQUIRE(read_tangent_mesh(&bytes, 4u, 6u, false, true, false, NULL, &flat, why));
    TIDE_CHECK(flat.vertices == 6u && !flat.has_normals);
    check_tangent_basis(&flat);
    check_tangent_uv_directions(&flat);
    mesh_file_free(&flat);
    for (uint32_t scenario = 0; scenario < 3u; scenario++) {
        for (uint32_t i = 0; i < 4u; i++) {
            bytes.position[i][1] = bytes.position[i][0] + (i >= 2u ? 1.0f : 0.0f);
            bytes.position[i][2] = bytes.position[i][0];
            bytes.normal[i][0] = bytes.normal[i][1] = 1.0f;
            bytes.normal[i][2] = scenario == 0u ? 1.0f : scenario == 1u ? 0x1.000002p0f : 1e-30f;
        }
        mesh_file mesh;
        TIDE_REQUIRE(read_tangent_mesh(&bytes, 4u, 6u, true, true, false, NULL, &mesh, why));
        check_tangent_basis(&mesh);
        mesh_file_free(&mesh);
    }
}

TIDE_TEST(mesh_missing_tangents_follow_nonuniform_and_mirrored_node_transforms)
{
    tangent_bytes bytes = tangent_plane();
    for (uint32_t i = 0; i < 4u; i++) {
        bytes.position[i][2] = bytes.position[i][0] + bytes.position[i][1];
        bytes.normal[i][0] = bytes.normal[i][1] = -1.0f;
    }
    static const char *const nodes[] = {",\"scale\":[2,3,4]", ",\"scale\":[-2,3,4]", ",\"scale\":[3e38,3e38,3e38]"};
    for (uint32_t i = 0; i < 3u; i++) {
        mesh_file mesh, control;
        char why[256];
        TIDE_REQUIRE(read_tangent_mesh(&bytes, 4u, 6u, true, true, false, nodes[i], &mesh, why));
        TIDE_REQUIRE(read_tangent_mesh(&bytes, 4u, 6u, true, true, true, nodes[i], &control, why));
        TIDE_CHECK(mesh.vertices == 4u);
        check_indexed_geometry(&mesh, &control);
        check_tangent_basis(&mesh);
        if (i < 2u) check_tangent_uv_directions(&mesh);
        for (uint32_t k = 0; k < 4u; k++) TIDE_CHECK(i == 2u ? tangent_is(mesh.tangent + k * 4u, 1, 0, 0, 1)
            : tangent_is(mesh.tangent + k * 4u, i ? 0.4472135955f : -0.4472135955f, 0, 0.894427191f, i ? 1 : -1));
        mesh_file_free(&control);
        mesh_file_free(&mesh);
    }
}

TIDE_TEST(mesh_authored_tangents_keep_the_existing_values_even_when_not_orthogonal)
{
    tangent_bytes bytes = tangent_plane();
    bytes.tangent[0][3] = 0.25f;
    bytes.tangent[1][0] = 0.0f, bytes.tangent[1][2] = 2.0f, bytes.tangent[1][3] = -0.5f;
    memset(bytes.tangent[2], 0, sizeof bytes.tangent[2]);
    bytes.uv[3][0] = 2.0f; // Would need a handedness split without authored tangents.
    mesh_file mesh;
    char why[256];
    TIDE_REQUIRE(read_tangent_mesh(&bytes, 4u, 6u, true, true, true, NULL, &mesh, why));
    TIDE_CHECK(mesh.vertices == 4u && mesh.has_tangents);
    const float expected[4][4] = {{-1, 0, 0, -0.25f}, {0, 0, 1, 0.5f}, {0, 0, 0, 0}, {-1, 0, 0, -1}};
    TIDE_CHECK(memcmp(mesh.tangent, expected, sizeof expected) == 0);
    mesh_file_free(&mesh);
}

TIDE_TEST(mesh_missing_tangents_do_not_replace_other_primitives_authored_tangents)
{
    tangent_bytes bytes = tangent_plane();
    for (uint32_t i = 0; i < 4u; i++) {
        bytes.tangent[i][0] = 0.0f, bytes.tangent[i][2] = 2.0f, bytes.tangent[i][3] = -0.25f;
    }
    mesh_file mesh;
    char why[256];
    TIDE_REQUIRE(read_tangent_mesh(&bytes, 4u, 6u, true, true, 2, NULL, &mesh, why));
    TIDE_CHECK(mesh.vertices == 8u && mesh.parts == 2u && mesh.has_tangents);
    for (uint32_t i = 0; i < 4u; i++) {
        TIDE_CHECK(tangent_is(mesh.tangent + i * 4u, 0, 0, 1, 0.25f));
        TIDE_CHECK(tangent_is(mesh.tangent + (i + 4u) * 4u, -1, 0, 0, -1));
    }
    mesh_file_free(&mesh);
}

TIDE_TEST(mesh_tangent_inputs_reject_nonfinite_authored_attributes)
{
    static const uint32_t nonfinite[] = {0x7f800000u, 0xff800000u, 0x7fc00000u};
    static const char *const names[] = {"positions", "normals", "uvs", "tangents"};
    for (uint32_t attribute = 0; attribute < 4u; attribute++) {
        for (uint32_t i = 0; i < 3u; i++) {
            tangent_bytes bytes = tangent_plane();
            float *value = attribute == 0u ? bytes.position[0] : attribute == 1u ? bytes.normal[0] : attribute == 2u ? bytes.uv[0] : bytes.tangent[0];
            memcpy(value, nonfinite + i, sizeof(float));
            mesh_file mesh;
            char why[256];
            TIDE_CHECK(!read_tangent_mesh(&bytes, 4u, 6u, true, true, true, NULL, &mesh, why));
            TIDE_CHECK(strstr(why, "non-finite") && strstr(why, names[attribute]));
        }
    }
}

TIDE_TEST(mesh_obj_missing_tangents_use_flipped_uvs_and_missing_corner_fallback)
{
    static const char *const objects[] = {
        "v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 0 1\nvn 0 0 1\nf 1/1/1 2/2/1 3/3/1\n",
        "v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvn 0 0 1\nf 1/1/1 2/2/1 3//1\n"
    };
    for (uint32_t i = 0; i < 2u; i++) {
        mesh_file mesh;
        char why[256];
        mesh_file canonical;
        char note[256];
        TIDE_REQUIRE(read_exactly((const uint8_t *)objects[i], strlen(objects[i]), "tangents.obj", &canonical, why));
        TIDE_REQUIRE(mesh_file_with_tangents(&canonical, &mesh, why, note));
        mesh_file_free(&canonical);
        TIDE_CHECK(mesh.vertices == 3u && mesh.indices == 3u && !mesh.has_tangents);
        check_tangent_basis(&mesh);
        for (uint32_t k = 0; k < 3u; k++) TIDE_CHECK(i ? tangent_is(mesh.tangent + k * 4u, 1, 0, 0, 1)
                                                                    : tangent_is(mesh.tangent + k * 4u, -1, 0, 0, 1));
        mesh_file_free(&mesh);
    }
}

// Every length a file can be cut to, and every byte of it wrong, in turn: read, or refused, and nothing more.
static void cut_and_spoil(const uint8_t *data, const size_t size, const char *name)
{
    mesh_file mesh;
    char why[256];
    for (size_t length = 0; length < size; length++) {
        if (read_exactly(data, length, name, &mesh, why)) mesh_file_free(&mesh);
        else TIDE_CHECK(why[0] != '\0'); // What's refused says why
    }
    uint8_t *spoiled = malloc(size);
    static const uint8_t ways[3] = {0xFFu, 0x01u, 0x80u};
    for (size_t at = 0; at < size; at++) {
        for (int way = 0; way < 3; way++) {
            memcpy(spoiled, data, size);
            spoiled[at] ^= ways[way];
            if (read_exactly(spoiled, size, name, &mesh, why)) mesh_file_free(&mesh);
        }
    }
    free(spoiled);
}

TIDE_TEST(mesh_glb_reads_and_is_never_read_past_its_bytes)
{
    size_t size;
    uint8_t *glb = make_glb(triangle_json, &triangle, sizeof triangle, &size);
    mesh_file mesh;
    char why[256];
    TIDE_REQUIRE(read_exactly(glb, size, "triangle.glb", &mesh, why));
    TIDE_CHECK(mesh.vertices == 3 && mesh.indices == 3 && mesh.parts == 1 && mesh.has_normals && !mesh.has_uvs && !mesh.has_colors);
    // x turned around, and the triangle wound the other way
    TIDE_CHECK(mesh.position[3] == -1.0f && mesh.position[7] == 1.0f && mesh.normal[2] == 1.0f);
    TIDE_CHECK(mesh.index[0] == 0 && mesh.index[1] == 2 && mesh.index[2] == 1);
    TIDE_CHECK(mesh.color[0] == 1.0f && mesh.color[3] == 1.0f && mesh.uv[0] == 0.0f && mesh.tangent[3] == 0.0f);
    mesh_file again;
    TIDE_REQUIRE(read_exactly(glb, size, "triangle.glb", &again, why));
    TIDE_CHECK(memcmp(mesh.hash, again.hash, sizeof mesh.hash) == 0);
    mesh_file_free(&again);
    mesh_file_free(&mesh);
    cut_and_spoil(glb, size, "triangle.glb");
    free(glb);
}

TIDE_TEST(mesh_gltf_reads_sparse_index_accessors)
{
    mesh_file expected;
    char why[256];
    size_t size;
    uint8_t *glb = make_glb(triangle_json, &triangle, sizeof triangle, &size);
    TIDE_REQUIRE(read_exactly(glb, size, "triangle.glb", &expected, why));
    free(glb);
    static const uint32_t types[3] = {5121u, 5123u, 5125u};
    for (uint32_t value = 0; value < 3u; value++) {
        for (uint32_t selector = 0; selector < 3u; selector++) {
            for (int dense = 0; dense < 2; dense++) {
                glb = make_sparse_triangle(types[value], types[selector], dense != 0, NULL, 1u, &size);
                mesh_file mesh;
                TIDE_REQUIRE(read_exactly(glb, size, "sparse.glb", &mesh, why));
                TIDE_CHECK(mesh.vertices == 3u && mesh.indices == 3u);
                TIDE_CHECK(mesh.index[0] == 0u && mesh.index[1] == 2u && mesh.index[2] == 1u);
                // Only how the data was stored changed: the mesh is identical.
                TIDE_CHECK(memcmp(mesh.hash, expected.hash, sizeof mesh.hash) == 0);
                mesh_file_free(&mesh);
                free(glb);
            }
        }
    }
    mesh_file_free(&expected);
    glb = make_sparse_triangle(5125u, 5121u, false, NULL, 1u, &size);
    cut_and_spoil(glb, size, "sparse.glb");
    free(glb);
}

TIDE_TEST(mesh_gltf_refuses_invalid_sparse_index_accessors)
{
    static const struct {
        const char *sparse;
        uint32_t first;
        const char *why;
    } cases[] = {
        {NULL, 3u, "for an element it hasn't got"},
        {"{\"count\":4,\"indices\":{\"bufferView\":3,\"componentType\":5123},\"values\":{\"bufferView\":4}}", 1u,
         "run past their buffer view's end"},
        {"{\"count\":2,\"indices\":{\"bufferView\":3,\"byteOffset\":11,\"componentType\":5123},\"values\":{\"bufferView\":4}}", 1u,
         "run past their buffer view's end"},
        {"{\"count\":2,\"indices\":{\"bufferView\":3,\"componentType\":5123},\"values\":{\"bufferView\":4,\"byteOffset\":15}}", 1u,
         "run past their buffer view's end"},
        {"{\"count\":2,\"indices\":{\"bufferView\":3,\"componentType\":5122},\"values\":{\"bufferView\":4}}", 1u,
         "don't say where their numbers are"},
        {"{\"count\":2,\"indices\":{\"bufferView\":3,\"componentType\":5123},\"values\":{}}", 1u,
         "don't say where their numbers are"},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        size_t size;
        uint8_t *glb = make_sparse_triangle(5123u, 5123u, false, cases[i].sparse, cases[i].first, &size);
        mesh_file mesh;
        char why[256];
        const bool ok = read_exactly(glb, size, "sparse.glb", &mesh, why);
        free(glb);
        TIDE_CHECK(!ok);
        if (ok) mesh_file_free(&mesh);
        else TIDE_CHECK(strstr(why, cases[i].why) != NULL);
    }
}

// Two material primitives share an accessor but use only vertices 2, 4 and 6,
// repeatedly and out of order. Every attribute is strided. Sparse positions
// replace a used vertex and an unused one. The compact control describes the
// same triangles with only those three vertices, and the override baked in.
static uint8_t *make_shared_vertices(const bool compact, const bool normals, size_t *size)
{
    uint8_t bytes[640] = {0};
    const uint32_t count = compact ? 3u : 8u, vertex_bytes = count * 72u;
    static const uint32_t kept[3] = {2u, 4u, 6u};
    static const float replacement[6] = {90, 90, 90, 13, 7, 5};
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t from = compact ? kept[i] : i;
        float attributes[18] = {(float)from, (float)(from % 3u), (float)(from / 3u),
                                1, 2, 3, 2, -1, 0, -1, // normal, tangent
                                (float)from / 8.0f, 0.25f, 0.75f, (float)from / 8.0f, // both uvs
                                0.25f, 0.5f, 0.75f, 0.5f};
        if (compact && from == 4u) memcpy(attributes, replacement + 3u, 3u * sizeof(float));
        memcpy(bytes + i * 72u, attributes, sizeof attributes);
    }
    static const uint16_t original[12] = {6, 2, 4, 6, 4, 2, 4, 6, 2, 2, 6, 4};
    static const uint16_t remapped[12] = {2, 0, 1, 2, 1, 0, 1, 2, 0, 0, 2, 1};
    memcpy(bytes + vertex_bytes, compact ? remapped : original, sizeof original);
    memcpy(bytes + vertex_bytes + 24u, replacement, sizeof replacement);
    bytes[vertex_bytes + 48u] = 0u;
    bytes[vertex_bytes + 49u] = 4u;
    const uint32_t byte_count = vertex_bytes + 52u;
    char json[4096];
    snprintf(json, sizeof json, "{\"asset\":{\"version\":\"2.0\"},\"nodes\":[{\"mesh\":0,\"scale\":[-2,3,4],"
                                "\"translation\":[10,20,30]}],\"meshes\":[{\"primitives\":["
                                "{\"attributes\":{\"POSITION\":0,%s\"TANGENT\":2,\"TEXCOORD_0\":3,\"TEXCOORD_1\":4,\"COLOR_0\":5},"
                                "\"indices\":6,\"material\":0},"
                                "{\"attributes\":{\"POSITION\":0,%s\"TANGENT\":2,\"TEXCOORD_0\":3,\"TEXCOORD_1\":4,\"COLOR_0\":5},"
                                "\"indices\":7,\"material\":1}]}],"
                                "\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorFactor\":[0.25,0.5,1,0.5]}},"
                                "{\"pbrMetallicRoughness\":{\"baseColorFactor\":[1,0.25,0.5,1]}}],"
                                "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":%u,\"type\":\"VEC3\"%s},"
                                "{\"bufferView\":0,\"byteOffset\":12,\"componentType\":5126,\"count\":%u,\"type\":\"VEC3\"},"
                                "{\"bufferView\":0,\"byteOffset\":24,\"componentType\":5126,\"count\":%u,\"type\":\"VEC4\"},"
                                "{\"bufferView\":0,\"byteOffset\":40,\"componentType\":5126,\"count\":%u,\"type\":\"VEC2\"},"
                                "{\"bufferView\":0,\"byteOffset\":48,\"componentType\":5126,\"count\":%u,\"type\":\"VEC2\"},"
                                "{\"bufferView\":0,\"byteOffset\":56,\"componentType\":5126,\"count\":%u,\"type\":\"VEC4\"},"
                                "{\"bufferView\":1,\"componentType\":5123,\"count\":6,\"type\":\"SCALAR\"},"
                                "{\"bufferView\":1,\"byteOffset\":12,\"componentType\":5123,\"count\":6,\"type\":\"SCALAR\"}],"
                                "\"bufferViews\":[{\"buffer\":0,\"byteStride\":72,\"byteLength\":%u},"
                                "{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":24},"
                                "{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":24},"
                                "{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":2}],\"buffers\":[{\"byteLength\":%u}]}",
             normals ? "\"NORMAL\":1," : "", normals ? "\"NORMAL\":1," : "", (unsigned)count,
             compact ? "" : ",\"sparse\":{\"count\":2,\"indices\":{\"bufferView\":3,\"componentType\":5121},\"values\":{\"bufferView\":2}}",
             (unsigned)count, (unsigned)count, (unsigned)count, (unsigned)count, (unsigned)count,
             (unsigned)vertex_bytes, (unsigned)vertex_bytes, (unsigned)(vertex_bytes + 24u), (unsigned)(vertex_bytes + 48u),
             (unsigned)byte_count);
    return make_glb(json, bytes, byte_count, size);
}

TIDE_TEST(mesh_gltf_keeps_only_referenced_vertices_of_each_primitive)
{
    for (int normals = 0; normals < 2; normals++) {
        size_t size;
        uint8_t *glb = make_shared_vertices(false, normals != 0, &size);
        mesh_file mesh, control;
        char why[256];
        TIDE_REQUIRE(read_exactly(glb, size, "shared.glb", &mesh, why));
        free(glb);
        glb = make_shared_vertices(true, normals != 0, &size);
        TIDE_REQUIRE(read_exactly(glb, size, "compact.glb", &control, why));
        free(glb);
        TIDE_CHECK(mesh.vertices == (normals ? 6u : 12u) && mesh.indices == 12u && mesh.parts == 2u);
        TIDE_CHECK(mesh.has_normals == (normals != 0) && mesh.has_uvs && mesh.has_uvs2 && mesh.has_colors && mesh.has_tangents);
        // All streams, indices, transforms, normals and material colors agree,
        // including repeated corners' sharing within each normal-bearing part.
        TIDE_CHECK(mesh.vertices == control.vertices);
        if (mesh.vertices == control.vertices) {
            TIDE_CHECK(memcmp(mesh.position, control.position, (size_t)mesh.vertices * 3u * sizeof(float)) == 0);
            TIDE_CHECK(memcmp(mesh.normal, control.normal, (size_t)mesh.vertices * 3u * sizeof(float)) == 0);
            TIDE_CHECK(memcmp(mesh.uv, control.uv, (size_t)mesh.vertices * 2u * sizeof(float)) == 0);
            TIDE_CHECK(memcmp(mesh.uv2, control.uv2, (size_t)mesh.vertices * 2u * sizeof(float)) == 0);
            TIDE_CHECK(memcmp(mesh.color, control.color, (size_t)mesh.vertices * 4u * sizeof(float)) == 0);
            TIDE_CHECK(memcmp(mesh.tangent, control.tangent, (size_t)mesh.vertices * 4u * sizeof(float)) == 0);
            TIDE_CHECK(memcmp(mesh.index, control.index, (size_t)mesh.indices * sizeof(uint32_t)) == 0);
            TIDE_CHECK(memcmp(mesh.hash, control.hash, sizeof mesh.hash) == 0);
        }
        if (normals) {
            static const uint32_t indices[12] = {2, 0, 1, 2, 1, 0, 4, 5, 3, 3, 5, 4};
            TIDE_CHECK(memcmp(mesh.index, indices, sizeof indices) == 0);
            TIDE_CHECK(mesh.color[3] == 0.25f && mesh.color[15] == 0.5f); // The materials stay separate
            TIDE_CHECK(mesh.position[3] == 16.0f && mesh.position[4] == 41.0f && mesh.position[5] == 50.0f);
        }
        mesh_file_free(&control);
        mesh_file_free(&mesh);
    }
}

TIDE_TEST(mesh_gltf_drops_vertices_outside_complete_triangles)
{
    static const struct {
        float position[12], normal[12];
        uint16_t index[4];
    } bytes = {{0, 0, 0, 1, 0, 0, 0, 1, 0, 10, 10, 10}, {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1}, {2, 0, 1, 3}};
    for (int indexed = 0; indexed < 2; indexed++) {
        char json[1024];
        snprintf(json, sizeof json, "{\"asset\":{\"version\":\"2.0\"},\"meshes\":[{\"primitives\":[{"
                                    "\"attributes\":{\"POSITION\":0,\"NORMAL\":1}%s}]}],"
                                    "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"},"
                                    "{\"bufferView\":1,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"},"
                                    "{\"bufferView\":2,\"componentType\":5123,\"count\":4,\"type\":\"SCALAR\"}],"
                                    "\"bufferViews\":[{\"buffer\":0,\"byteLength\":48},{\"buffer\":0,\"byteOffset\":48,\"byteLength\":48},"
                                    "{\"buffer\":0,\"byteOffset\":96,\"byteLength\":8}],\"buffers\":[{\"byteLength\":104}]}",
                 indexed ? ",\"indices\":2" : "");
        size_t size;
        uint8_t *glb = make_glb(json, &bytes, sizeof bytes, &size);
        mesh_file mesh;
        char why[256];
        TIDE_REQUIRE(read_exactly(glb, size, "incomplete.glb", &mesh, why));
        free(glb);
        TIDE_CHECK(mesh.vertices == 3u && mesh.indices == 3u);
        TIDE_CHECK(mesh.position[3] == -1.0f && mesh.position[7] == 1.0f);
        TIDE_CHECK(mesh.index[0] == (indexed ? 2u : 0u) && mesh.index[1] == (indexed ? 1u : 2u) && mesh.index[2] == (indexed ? 0u : 1u));
        mesh_file_free(&mesh);
    }
}

TIDE_TEST(mesh_gltf_text_is_never_read_past_its_bytes)
{
    // The same triangle, its buffer inside the text: 80 bytes as base64
    static const char base64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint8_t bytes[81] = {0};
    memcpy(bytes, &triangle, sizeof triangle);
    char encoded[120];
    size_t n = 0;
    for (size_t i = 0; i < 81; i += 3) {
        const uint32_t three = (uint32_t)bytes[i] << 16 | (uint32_t)bytes[i + 1] << 8 | bytes[i + 2];
        encoded[n++] = base64[three >> 18], encoded[n++] = base64[(three >> 12) & 63u];
        encoded[n++] = base64[(three >> 6) & 63u], encoded[n++] = base64[three & 63u];
    }
    encoded[n] = '\0';
    char json[2048];
    snprintf(json, sizeof json, "{\"asset\":{\"version\":\"2.0\"},\"nodes\":[{\"mesh\":0,\"scale\":[2,2,2],\"name\":\"a \\u00e9\\n\"}],"
                                TRIANGLE_MESH TRIANGLE_ACCESSORS "\"bufferViews\":[{\"buffer\":0,\"byteLength\":36},"
                                "{\"buffer\":0,\"byteOffset\":36,\"byteLength\":36},{\"buffer\":0,\"byteOffset\":72,\"byteLength\":6}],"
                                "\"buffers\":[{\"uri\":\"data:application/octet-stream;base64,%s\",\"byteLength\":80}]}", encoded);
    mesh_file mesh;
    char why[256];
    TIDE_REQUIRE(read_exactly((const uint8_t *)json, strlen(json), "triangle.gltf", &mesh, why));
    TIDE_CHECK(mesh.vertices == 3 && mesh.position[3] == -2.0f && mesh.position[7] == 2.0f); // Through its node
    mesh_file_free(&mesh);
    cut_and_spoil((const uint8_t *)json, strlen(json), "triangle.gltf");
}

TIDE_TEST(mesh_obj_text_is_never_read_past_its_bytes)
{
    static const char obj[] = "# a square\r\nv 0 0 0\r\nv 1 0 0 0.5\nv 1 1 0 1 0 0\nv 0 1 0\nvt 0 0\nvt 1 1\nvn 0 0 1\n"
                              "f 1/1/1 2/2/1 3//1 -1/1/1\nf 1 2 3\nl 1 2\n";
    mesh_file mesh;
    char why[256];
    TIDE_REQUIRE(read_exactly((const uint8_t *)obj, sizeof obj - 1u, "square.obj", &mesh, why));
    // Four corners that share vertices, and a face with no normal, whose three are its own
    TIDE_CHECK(mesh.vertices == 7 && mesh.indices == 9 && mesh.has_normals && mesh.has_uvs && mesh.has_colors);
    mesh_file_free(&mesh);
    cut_and_spoil((const uint8_t *)obj, sizeof obj - 1u, "square.obj");
}

// URL queries and fragments don't name a format. In a local name those
// characters are literal, and in a URL's host they name no file at all.
TIDE_TEST(mesh_file_names_use_url_paths_and_keep_local_suffixes)
{
    static const char obj[] = "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    static const struct {
        const char *name;
        bool reads;
    } cases[] = {
        {"tree.obj", true},
        {"tree#old.obj", true},
        {"tree?old.obj", true},
        {"tree.obj?revision=1", false},
        {"tree.obj#model", false},
        {"http://example.com/tree.obj?revision=1", true},
        {"https://example.com/tree.OBJ#model", true},
        {"https://example.com/tree.obj?revision=1#model", true},
        {"https://tree.obj?revision=1", false},
        {"https://example.com?model=/tree.obj", false},
        {"https://example.com/tree?format=.obj", false},
        {"https://example.com/tree#.obj", false},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        mesh_file mesh;
        char why[256];
        const bool read = read_exactly((const uint8_t *)obj, sizeof obj - 1u, cases[i].name, &mesh, why);
        TIDE_CHECK(read == cases[i].reads);
        if (read) mesh_file_free(&mesh);
        else TIDE_CHECK(strstr(why, "isn't a mesh file Tide reads") != NULL);
    }
}

static bool refused(const char *json, const char *with)
{
    mesh_file mesh;
    char why[256];
    size_t size;
    uint8_t *glb = make_glb(json, &triangle, sizeof triangle, &size);
    const bool ok = read_exactly(glb, size, "wrong.glb", &mesh, why);
    free(glb);
    if (ok) {
        mesh_file_free(&mesh);
        return false;
    }
    if (!strstr(why, with)) printf("    refused with '%s', not '%s'\n", why, with);
    return strstr(why, with) != NULL;
}

// What a description says that isn't so, each refused with what's wrong.
TIDE_TEST(mesh_gltf_refuses_what_its_bytes_dont_bear_out)
{
    // A node in the scene twice, and one inside itself
    TIDE_CHECK(refused("{\"asset\":{\"version\":\"2.0\"},\"scenes\":[{\"nodes\":[0,0]}],\"nodes\":[{\"mesh\":0}]," TRIANGLE_MESH
                       TRIANGLE_ACCESSORS TRIANGLE_VIEWS "}", "in its scene twice"));
    TIDE_CHECK(refused("{\"asset\":{\"version\":\"2.0\"},\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"children\":[1]},{\"children\":[0]}],"
                       TRIANGLE_MESH TRIANGLE_ACCESSORS TRIANGLE_VIEWS "}", "in its scene twice"));
    // More normals than positions
    TIDE_CHECK(refused("{\"asset\":{\"version\":\"2.0\"},\"nodes\":[{\"mesh\":0}]," TRIANGLE_MESH
                       "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
                       "{\"bufferView\":1,\"componentType\":5126,\"count\":2,\"type\":\"VEC3\"},"
                       "{\"bufferView\":2,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}]," TRIANGLE_VIEWS "}",
                       "2 normals for 3 positions"));
    // More positions than the view has bytes for
    TIDE_CHECK(refused("{\"asset\":{\"version\":\"2.0\"},\"nodes\":[{\"mesh\":0}]," TRIANGLE_MESH
                       "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"},"
                       "{\"bufferView\":1,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"},"
                       "{\"bufferView\":2,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}]," TRIANGLE_VIEWS "}",
                       "run past their buffer view's end"));
    // An index with no vertex: the buffer's fourth index is 0, so the count of positions is what's short
    TIDE_CHECK(refused("{\"asset\":{\"version\":\"2.0\"},\"nodes\":[{\"mesh\":0}],"
                       "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":2}]}],"
                       "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":2,\"type\":\"VEC3\"},"
                       "{\"bufferView\":1,\"componentType\":5126,\"count\":2,\"type\":\"VEC3\"},"
                       "{\"bufferView\":2,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}]," TRIANGLE_VIEWS "}",
                       "a corner it has no vertex for"));
    // A view past its buffer, a stride no mesh has, a mesh and a node it hasn't got
    TIDE_CHECK(refused("{\"asset\":{\"version\":\"2.0\"},\"nodes\":[{\"mesh\":0}]," TRIANGLE_MESH TRIANGLE_ACCESSORS
                       "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":60,\"byteLength\":36},{\"buffer\":0,\"byteLength\":36},"
                       "{\"buffer\":0,\"byteLength\":6}],\"buffers\":[{\"byteLength\":80}]}", "runs past its buffer's end"));
    TIDE_CHECK(refused("{\"asset\":{\"version\":\"2.0\"},\"nodes\":[{\"mesh\":0}]," TRIANGLE_MESH TRIANGLE_ACCESSORS
                       "\"bufferViews\":[{\"buffer\":0,\"byteLength\":36,\"byteStride\":4503599627370496},"
                       "{\"buffer\":0,\"byteLength\":36},{\"buffer\":0,\"byteLength\":6}],\"buffers\":[{\"byteLength\":80}]}",
                       "a stride no mesh has"));
    TIDE_CHECK(refused("{\"asset\":{\"version\":\"2.0\"},\"nodes\":[{\"mesh\":3}]," TRIANGLE_MESH TRIANGLE_ACCESSORS TRIANGLE_VIEWS "}",
                       "a mesh it hasn't got"));
    TIDE_CHECK(refused("{\"asset\":{\"version\":\"2.0\"},\"scenes\":[{\"nodes\":[9]}],\"nodes\":[{\"mesh\":0}]," TRIANGLE_MESH
                       TRIANGLE_ACCESSORS TRIANGLE_VIEWS "}", "a node it hasn't got"));
    // A buffer said longer than the file's bytes, and geometry that's compressed
    TIDE_CHECK(refused("{\"asset\":{\"version\":\"2.0\"},\"nodes\":[{\"mesh\":0}]," TRIANGLE_MESH TRIANGLE_ACCESSORS
                       "\"bufferViews\":[{\"buffer\":0,\"byteLength\":36},{\"buffer\":0,\"byteLength\":36},{\"buffer\":0,\"byteLength\":6}],"
                       "\"buffers\":[{\"byteLength\":4096}]}", "longer than its bytes are"));
    TIDE_CHECK(refused("{\"asset\":{\"version\":\"2.0\"},\"extensionsRequired\":[\"KHR_draco_mesh_compression\"],\"nodes\":[{\"mesh\":0}],\"meshes\":[{\"primitives\":[{\"attributes\":"
                       "{\"POSITION\":0},\"extensions\":{\"KHR_draco_mesh_compression\":{\"bufferView\":0}}}]}],"
                       TRIANGLE_ACCESSORS TRIANGLE_VIEWS "}", "compressed with Draco"));
    TIDE_CHECK(refused("{\"asset\":{\"version\":\"2.0\"},\"extensionsRequired\":[\"EXT_meshopt_compression\"]}", "compressed with meshopt"));
    // Lines and points alone, and text that isn't JSON
    TIDE_CHECK(refused("{\"asset\":{\"version\":\"2.0\"},\"nodes\":[{\"mesh\":0}],\"meshes\":[{\"primitives\":[{\"attributes\":"
                       "{\"POSITION\":0},\"mode\":1}]}]," TRIANGLE_ACCESSORS TRIANGLE_VIEWS "}", "has no triangles"));
    TIDE_CHECK(refused("{\"asset\":{\"version\":\"2.0\"},", "isn't JSON"));
    TIDE_CHECK(refused("[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[", "isn't JSON"));
}

static bool same_triangle(const char *json)
{
    mesh_file expected, actual;
    char why[256];
    size_t size;
    uint8_t *bytes = make_glb(triangle_json, &triangle, sizeof triangle, &size);
    const bool baseline = read_exactly(bytes, size, "triangle.glb", &expected, why);
    free(bytes);
    if (!baseline) return false;
    bytes = make_glb(json, &triangle, sizeof triangle, &size);
    const bool ok = read_exactly(bytes, size, "fallback.glb", &actual, why);
    if (!ok) printf("    fallback: %s\n", why);
    free(bytes);
    const bool same = ok && memcmp(expected.hash, actual.hash, sizeof expected.hash) == 0;
    if (ok) mesh_file_free(&actual);
    mesh_file_free(&expected);
    return same;
}

TIDE_TEST(mesh_gltf_required_extensions_and_optional_compression_fallbacks)
{
    TIDE_CHECK(refused("{\"asset\":{\"version\":\"2.0\"},\"extensionsRequired\":[\"EXT_mesh_gpu_instancing\"]}",
                       "requires an unsupported glTF extension ('EXT_mesh_gpu_instancing')"));
    TIDE_CHECK(refused("{\"asset\":{\"version\":\"2.0\"},\"extensionsRequired\":{}}", "isn't an array"));
    TIDE_CHECK(refused("{\"asset\":{\"version\":\"2.0\"},\"extensionsRequired\":[17]}", "isn't text"));
    TIDE_CHECK(same_triangle("{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
        "\"extensionsRequired\":[\"KHR_mesh_quantization\"]," TRIANGLE_MESH TRIANGLE_ACCESSORS TRIANGLE_VIEWS "}"));
    TIDE_CHECK(same_triangle("{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
        "\"extensionsUsed\":[\"KHR_draco_mesh_compression\"],\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1},"
        "\"indices\":2,\"extensions\":{\"KHR_draco_mesh_compression\":{\"bufferView\":0,\"attributes\":{\"POSITION\":0,\"NORMAL\":1}}}}]}],"
        TRIANGLE_ACCESSORS TRIANGLE_VIEWS "}"));
    static const char *const meshopt[] = {"EXT_meshopt_compression", "KHR_meshopt_compression"};
    for (uint32_t i = 0; i < 2u; i++) {
        char json[2048];
        snprintf(json, sizeof json, "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
            "\"extensionsUsed\":[\"%s\"]," TRIANGLE_MESH TRIANGLE_ACCESSORS
            "\"bufferViews\":[{\"buffer\":1,\"byteLength\":36,\"byteStride\":12,\"extensions\":{\"%s\":{\"buffer\":0,\"byteOffset\":0,"
            "\"byteLength\":36,\"byteStride\":12,\"count\":3,\"mode\":\"ATTRIBUTES\"}}},{\"buffer\":0,\"byteOffset\":36,\"byteLength\":36},"
            "{\"buffer\":0,\"byteOffset\":72,\"byteLength\":6}],\"buffers\":[{\"byteLength\":80},{\"byteLength\":36,"
            "\"uri\":\"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA\",\"extensions\":{\"%s\":{\"fallback\":true}}}]}",
            meshopt[i], meshopt[i], meshopt[i]);
        TIDE_CHECK(same_triangle(json));
        snprintf(json, sizeof json, "{\"asset\":{\"version\":\"2.0\"},\"extensionsRequired\":[\"%s\"]}", meshopt[i]);
        TIDE_CHECK(refused(json, "compressed with meshopt"));
    }
    // Optional compression without core bytes must still fail to read.
    TIDE_CHECK(refused("{\"asset\":{\"version\":\"2.0\"}," TRIANGLE_MESH TRIANGLE_ACCESSORS
        "\"bufferViews\":[{\"buffer\":1,\"byteLength\":36},{\"buffer\":0,\"byteOffset\":36,\"byteLength\":36},"
        "{\"buffer\":0,\"byteOffset\":72,\"byteLength\":6}],\"buffers\":[{\"byteLength\":80},{\"byteLength\":36,"
        "\"extensions\":{\"EXT_meshopt_compression\":{\"fallback\":true}}}]}", "buffer with no bytes"));
}

TIDE_TEST(mesh_glb_reports_malformed_headers_and_chunks)
{
    mesh_file mesh;
    char why[256];
    uint8_t bad[20] = {0};
    TIDE_CHECK(!read_exactly(bad, sizeof bad, "https://models.test/HERO.GLB?key=1#x", &mesh, why));
    TIDE_CHECK(strstr(why, "invalid GLB header") != NULL);
    TIDE_CHECK(!read_exactly(bad, 3u, "hero.glb", &mesh, why));
    TIDE_CHECK(strstr(why, "header") != NULL);
    size_t size;
    uint8_t *bytes = make_glb(triangle_json, &triangle, sizeof triangle, &size);
    const uint32_t length = (uint32_t)size - 4u;
    memcpy(bytes + 8u, &length, 4u);
    TIDE_CHECK(!read_exactly(bytes, size, "hero.glb", &mesh, why));
    TIDE_CHECK(strstr(why, "declared length") != NULL);
    const uint32_t actual = (uint32_t)size, binary = 0x004e4942u;
    memcpy(bytes + 8u, &actual, 4u);
    memcpy(bytes + 16u, &binary, 4u);
    TIDE_CHECK(!read_exactly(bytes, size, "hero.glb", &mesh, why));
    TIDE_CHECK(strstr(why, "first chunk isn't JSON") != NULL);
    free(bytes);
    bytes = make_glb(triangle_json, &triangle, sizeof triangle, &size);
    uint32_t json_size;
    memcpy(&json_size, bytes + 12u, 4u);
    json_size--;
    memcpy(bytes + 12u, &json_size, 4u);
    TIDE_CHECK(!read_exactly(bytes, size, "hero.glb", &mesh, why));
    TIDE_CHECK(strstr(why, "alignment") != NULL);
    free(bytes);
}

TIDE_TEST(mesh_gltf_reports_invalid_attributes_instead_of_changing_geometry)
{
    TIDE_CHECK(refused("{\"asset\":{\"version\":\"2.0\"},\"nodes\":[{\"mesh\":0,\"children\":[-1]}],"
        TRIANGLE_MESH TRIANGLE_ACCESSORS TRIANGLE_VIEWS "}", "a node it hasn't got"));
    TIDE_CHECK(refused("{\"asset\":{\"version\":\"2.0\"},\"meshes\":[{\"primitives\":["
        "{\"attributes\":{\"POSITION\":0,\"NORMAL\":1},\"indices\":2},{\"attributes\":{},\"mode\":99}]}],"
        TRIANGLE_ACCESSORS TRIANGLE_VIEWS "}", "invalid primitive mode"));
    TIDE_CHECK(refused("{\"asset\":{\"version\":\"2.0\"},\"meshes\":[{\"primitives\":["
        "{\"attributes\":{\"POSITION\":0,\"NORMAL\":1},\"indices\":2},{\"attributes\":{}}]}],"
        TRIANGLE_ACCESSORS TRIANGLE_VIEWS "}", "no POSITION"));
    TIDE_CHECK(refused("{\"asset\":{\"version\":\"2.0\"}," TRIANGLE_MESH
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
        "{\"bufferView\":1,\"componentType\":5126,\"count\":3,\"type\":\"VEC2\"},"
        "{\"bufferView\":2,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}]," TRIANGLE_VIEWS "}", "normals with the wrong number"));
    static const uint32_t nonfinite[] = {0x7f800000u, 0xff800000u, 0x7fc00000u};
    for (uint32_t i = 0; i < 3u; i++) {
        triangle_bytes altered = triangle;
        memcpy(&altered.positions[3], &nonfinite[i], 4u);
        size_t size;
        uint8_t *bytes = make_glb(triangle_json, &altered, sizeof altered, &size);
        mesh_file mesh;
        char why[256];
        TIDE_CHECK(!read_exactly(bytes, size, "hero.glb", &mesh, why));
        TIDE_CHECK(strstr(why, "non-finite positions") != NULL);
        free(bytes);
    }
}

// A .gltf's text built in pieces, for files at the limits (mesh_files.h).
typedef struct limit_text {
    char *at;
    size_t length, room;
} limit_text;

static void limit_add(limit_text *t, const char *fmt, ...)
{
    for (;;) {
        va_list args;
        va_start(args, fmt);
        const int n = vsnprintf(t->at ? t->at + t->length : NULL, t->at ? t->room - t->length : 0u, fmt, args);
        va_end(args);
        if (n < 0) abort();
        if (t->at && t->length + (size_t)n < t->room) {
            t->length += (size_t)n;
            return;
        }
        t->room = (t->room ? t->room * 2u : 4096u) + (size_t)n;
        t->at = realloc(t->at, t->room);
        if (!t->at) abort();
    }
}

// Whether reading the text as a .gltf refuses it, saying `with`.
static bool limit_refused(limit_text *t, const char *with)
{
    mesh_file mesh;
    char why[256];
    const bool ok = read_exactly((const uint8_t *)t->at, t->length, "limit.gltf", &mesh, why);
    free(t->at);
    *t = (limit_text){0};
    if (ok) {
        mesh_file_free(&mesh);
        printf("    read, where '%s' was expected\n", with);
        return false;
    }
    if (!strstr(why, with)) printf("    refused with '%s', not '%s'\n", why, with);
    return strstr(why, with) != NULL;
}

// One past each limit is refused from the counts the file says, before what
// they ask for is made: accessors without a buffer view are zeros that take
// no bytes, so most of these files are small.
TIDE_TEST(mesh_gltf_refuses_files_past_its_limits)
{
    limit_text t = {0};
    // A 256-byte file once made 6 million zero vertices and 100 million indices.
    limit_add(&t, "{\"asset\":{\"version\":\"2.0\"},\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":0},"
                  "\"indices\":1}]}],\"accessors\":[{\"componentType\":5126,\"count\":6000000,\"type\":\"VEC3\"},"
                  "{\"componentType\":5125,\"count\":100663296,\"type\":\"SCALAR\"}]}");
    TIDE_CHECK(limit_refused(&t, "elements in an accessor, more than the 25,165,824 one Mesh holds (TIDE_MESH_MAX_INDICES)"));
    limit_add(&t, "{\"asset\":{\"version\":\"2.0\"},\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0}}]}],"
                  "\"accessors\":[{\"componentType\":5126,\"count\":%u,\"type\":\"VEC3\"}]}", TIDE_MESH_MAX_VERTICES + 1u);
    TIDE_CHECK(limit_refused(&t, "more than the 16,777,216 one Mesh holds (TIDE_MESH_MAX_VERTICES)"));
    limit_add(&t, "{\"asset\":{\"version\":\"2.0\"},\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}],"
                  "\"accessors\":[{\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
                  "{\"componentType\":5125,\"count\":%u,\"type\":\"SCALAR\"}]}", TIDE_MESH_MAX_INDICES + 1u);
    TIDE_CHECK(limit_refused(&t, "elements in an accessor, more than the 25,165,824 one Mesh holds (TIDE_MESH_MAX_INDICES)"));
    // Without normals, each triangle's corners are vertices of their own: indices within their
    // limit can still ask for more vertices than a mesh holds.
    limit_add(&t, "{\"asset\":{\"version\":\"2.0\"},\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}],"
                  "\"accessors\":[{\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
                  "{\"componentType\":5125,\"count\":%u,\"type\":\"SCALAR\"}]}", (TIDE_MESH_MAX_VERTICES / 3u + 1u) * 3u);
    TIDE_CHECK(limit_refused(&t, "more than the 16,777,216 one Mesh holds (TIDE_MESH_MAX_VERTICES)"));
    // A part with no triangles over the most vertices, placed by many nodes, is walked again for each.
    limit_add(&t, "{\"asset\":{\"version\":\"2.0\"},\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}],"
                  "\"accessors\":[{\"componentType\":5126,\"count\":%u,\"type\":\"VEC3\"},{\"componentType\":5125,\"count\":0,"
                  "\"type\":\"SCALAR\"}],\"nodes\":[", TIDE_MESH_MAX_VERTICES);
    const uint32_t placements = TIDE_MESH_MAX_READS / TIDE_MESH_MAX_VERTICES + 1u;
    for (uint32_t i = 0; i < placements; i++) limit_add(&t, "%s{\"mesh\":0}", i ? "," : "");
    limit_add(&t, "]}");
    TIDE_CHECK(limit_refused(&t, "more than the 536,870,912 one Mesh holds (TIDE_MESH_MAX_READS)"));
    // A description of one more value than the most
    limit_add(&t, "{\"asset\":{\"version\":\"2.0\"},\"extras\":[0");
    for (uint32_t i = 4u; i < TIDE_MESH_MAX_JSON_VALUES; i++) limit_add(&t, ",0");
    limit_add(&t, "]}");
    TIDE_CHECK(limit_refused(&t, "JSON values"));
}

// Objects with many keys are looked up by halves rather than by a walk, and a
// key written twice still means its first: a triangle whose objects carry
// many other keys, repeated ones among them, reads as the same mesh.
TIDE_TEST(mesh_gltf_objects_with_many_keys_read_as_before)
{
    mesh_file plain, keyed;
    char why[256];
    size_t size;
    uint8_t *glb = make_glb(triangle_json, &triangle, sizeof triangle, &size);
    TIDE_REQUIRE(read_exactly(glb, size, "plain.glb", &plain, why));
    free(glb);
    char extra[512], json[4096];
    size_t at = 0;
    for (uint32_t i = 0; i < 24u; i++) at += (size_t)snprintf(extra + at, sizeof extra - at, "\"zz%02u\":%u,", i, i);
    snprintf(json, sizeof json, "{%s\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
             TRIANGLE_MESH "\"accessors\":[{%s\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\",\"count\":2},"
             "{\"bufferView\":1,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
             "{%s\"bufferView\":2,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\",\"bufferView\":0}],"
             TRIANGLE_VIEWS ",\"scene\":4}", extra, extra, extra);
    glb = make_glb(json, &triangle, sizeof triangle, &size);
    TIDE_REQUIRE(read_exactly(glb, size, "keyed.glb", &keyed, why));
    free(glb);
    TIDE_CHECK(plain.vertices == keyed.vertices && plain.indices == keyed.indices);
    TIDE_CHECK(memcmp(plain.hash, keyed.hash, sizeof plain.hash) == 0);
    mesh_file_free(&plain);
    mesh_file_free(&keyed);
}
