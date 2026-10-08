// The mesh files' reader (compiler/src/mesh_files.h), on its own: the numbers
// it reads, what it refuses, and that a file cut anywhere or wrong anywhere is
// never read past its bytes. What it makes of whole files is the end-to-end
// test's (e2e/asset_meshes).

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
        {"1e-45", 0x1p-149f},                                           // ...and one of them
        {"1e400", 0.0f},  // What no float holds is none
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
    // The same file gives the same mesh, to the bit
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
    TIDE_CHECK(refused("{\"asset\":{\"version\":\"2.0\"},\"nodes\":[{\"mesh\":0}],\"meshes\":[{\"primitives\":[{\"attributes\":"
                       "{\"POSITION\":0},\"extensions\":{\"KHR_draco_mesh_compression\":{\"bufferView\":0}}}]}],"
                       TRIANGLE_ACCESSORS TRIANGLE_VIEWS "}", "compressed with Draco"));
    TIDE_CHECK(refused("{\"asset\":{\"version\":\"2.0\"},\"extensionsRequired\":[\"EXT_meshopt_compression\"]}", "compressed with meshopt"));
    // Lines and points alone, and text that isn't JSON
    TIDE_CHECK(refused("{\"asset\":{\"version\":\"2.0\"},\"nodes\":[{\"mesh\":0}],\"meshes\":[{\"primitives\":[{\"attributes\":"
                       "{\"POSITION\":0},\"mode\":1}]}]," TRIANGLE_ACCESSORS TRIANGLE_VIEWS "}", "has no triangles"));
    TIDE_CHECK(refused("{\"asset\":{\"version\":\"2.0\"},", "isn't JSON"));
    TIDE_CHECK(refused("[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[", "isn't JSON"));
}
