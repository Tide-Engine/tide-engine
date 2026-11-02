// The static mesh reader as a game's Mesh<V>.Decode uses it (tide/mesh_data.h):
// bytes with no file name, Decode's limits, and anything a file could say.
// tidec's imports through the same reader are compiler/tests/mesh_files_test.c.

#include "tide_test.h"
#include "tide/mesh_data.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// One triangle, (0, 0, 0), (1, 0, 0), (0, 1, 0), with no normals: 36 bytes of
// positions and three ushort indices, padded to 44.
static void triangle_buffer(uint8_t out[44])
{
    const float positions[9] = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    const uint16_t indices[4] = {0, 1, 2, 0};
    memcpy(out, positions, sizeof positions);
    memcpy(out + 36, indices, 8u);
}

#define TRIANGLE_PARTS                                                                                                   \
    "\"asset\":{\"version\":\"2.0\"},\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}],"   \
    "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"                           \
    "{\"bufferView\":1,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}],"                                       \
    "\"bufferViews\":[{\"buffer\":0,\"byteLength\":36},{\"buffer\":0,\"byteOffset\":36,\"byteLength\":6}],"

static uint8_t *triangle_glb(size_t *size)
{
    char json[1024];
    int length = snprintf(json, sizeof json, "{" TRIANGLE_PARTS "\"buffers\":[{\"byteLength\":44}]}");
    while (length % 4) json[length++] = ' ';
    *size = 12u + 8u + (size_t)length + 8u + 44u;
    uint8_t *out = malloc(*size);
    if (!out) abort();
    const uint32_t header[5] = {0x46546c67u, 2u, (uint32_t)*size, (uint32_t)length, 0x4E4F534Au};
    memcpy(out, header, sizeof header);
    memcpy(out + 20, json, (size_t)length);
    const uint32_t bin[2] = {44u, 0x004E4942u};
    memcpy(out + 20 + length, bin, sizeof bin);
    triangle_buffer(out + 28 + length);
    return out;
}

static void base64(const uint8_t *in, const size_t size, char *out)
{
    static const char digits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t at = 0;
    for (size_t i = 0; i < size; i += 3u) {
        const uint32_t v = (uint32_t)in[i] << 16 | (i + 1u < size ? (uint32_t)in[i + 1u] << 8 : 0u) | (i + 2u < size ? in[i + 2u] : 0u);
        out[at++] = digits[v >> 18 & 63u];
        out[at++] = digits[v >> 12 & 63u];
        out[at++] = i + 1u < size ? digits[v >> 6 & 63u] : '=';
        out[at++] = i + 2u < size ? digits[v & 63u] : '=';
    }
    out[at] = '\0';
}

// The same triangle as a .gltf whose buffer is inside it, after `before` (a byte order mark, spaces).
static char *triangle_gltf(const char *before)
{
    uint8_t buffer[44];
    char encoded[80];
    triangle_buffer(buffer);
    base64(buffer, sizeof buffer, encoded);
    char *out = malloc(2048);
    if (!out) abort();
    snprintf(out, 2048, "%s{" TRIANGLE_PARTS "\"buffers\":[{\"byteLength\":44,\"uri\":\"data:application/octet-stream;base64,%s\"}]}",
             before, encoded);
    return out;
}

static const char triangle_obj[] = "# a triangle\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";

static bool decode(const void *bytes, const size_t size, const tide_mesh_limits *limits, tide_mesh_data *out, char why[256],
                   bool *limited)
{
    char note[256];
    return tide_mesh_data_decode(bytes, size, TIDE_MESH_ANY, limits, NULL, NULL, out, why, note, limited);
}

TIDE_TEST(mesh_decode_reads_glb_gltf_and_obj_by_their_bytes)
{
    char why[256];
    size_t glb_size;
    uint8_t *glb = triangle_glb(&glb_size);
    char *gltf = triangle_gltf(""), *marked = triangle_gltf("\xEF\xBB\xBF \r\n\t");
    tide_mesh_data from_glb, from_gltf, from_marked, from_obj, named;
    TIDE_REQUIRE(decode(glb, glb_size, &tide_mesh_decode_limits, &from_glb, why, NULL));
    TIDE_REQUIRE(decode(gltf, strlen(gltf), &tide_mesh_decode_limits, &from_gltf, why, NULL));
    TIDE_REQUIRE(decode(marked, strlen(marked), &tide_mesh_decode_limits, &from_marked, why, NULL));
    TIDE_REQUIRE(decode(triangle_obj, strlen(triangle_obj), &tide_mesh_decode_limits, &from_obj, why, NULL));
    char note[256];
    TIDE_REQUIRE(tide_mesh_data_decode(glb, glb_size, TIDE_MESH_GLB, &tide_mesh_decode_limits, NULL, NULL, &named, why, note, NULL));
    // Each triangle gets its own vertices (no normals), x turned around and wound clockwise from its front,
    // which faces +z as it did in the file.
    TIDE_CHECK(from_glb.vertices == 3u && from_glb.indices == 3u);
    const float expected[9] = {0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -1.0f, 0.0f, 0.0f};
    TIDE_CHECK(memcmp(from_glb.position, expected, sizeof expected) == 0);
    TIDE_CHECK(from_glb.index[0] == 0u && from_glb.index[1] == 1u && from_glb.index[2] == 2u);
    TIDE_CHECK(from_glb.normal[2] == 1.0f && from_glb.color[0] == 1.0f && from_glb.color[3] == 1.0f);
    // The same mesh whichever way it came, and as a format named
    TIDE_CHECK(memcmp(from_glb.hash, from_gltf.hash, 32) == 0);
    TIDE_CHECK(memcmp(from_glb.hash, from_marked.hash, 32) == 0);
    TIDE_CHECK(memcmp(from_glb.hash, from_obj.hash, 32) == 0);
    TIDE_CHECK(memcmp(from_glb.hash, named.hash, 32) == 0);
    tide_mesh_data_free(&from_glb);
    tide_mesh_data_free(&from_gltf);
    tide_mesh_data_free(&from_marked);
    tide_mesh_data_free(&from_obj);
    tide_mesh_data_free(&named);
    free(glb);
    free(gltf);
    free(marked);
}

TIDE_TEST(mesh_decode_refuses_what_it_cannot_read_at_runtime)
{
    char why[256];
    tide_mesh_data out = {0};
    bool limited = true;
    // A .gltf whose buffer is a file beside it: a decode has no files
    const char beside[] = "{" TRIANGLE_PARTS "\"buffers\":[{\"byteLength\":44,\"uri\":\"triangle.bin\"}]}";
    TIDE_CHECK(!decode(beside, strlen(beside), &tide_mesh_decode_limits, &out, why, &limited));
    TIDE_CHECK(!limited && out.position == NULL);
    // FBX, binary and ASCII, is the build's to read
    const char binary[] = "Kaydara FBX Binary  \0\x1a\0";
    TIDE_CHECK(!decode(binary, sizeof binary, &tide_mesh_decode_limits, &out, why, &limited));
    TIDE_CHECK(strstr(why, "FBX") != NULL && !limited);
    const char ascii[] = "; FBX 7.4.0 project file\n";
    TIDE_CHECK(!decode(ascii, strlen(ascii), &tide_mesh_decode_limits, &out, why, NULL));
    TIDE_CHECK(strstr(why, "FBX") != NULL);
    // Text that's no OBJ of anything, and no bytes at all
    const char words[] = "hello, world";
    TIDE_CHECK(!decode(words, strlen(words), &tide_mesh_decode_limits, &out, why, NULL));
    TIDE_CHECK(strstr(why, "no triangles") != NULL);
    TIDE_CHECK(!decode(words, 0u, &tide_mesh_decode_limits, &out, why, NULL));
    // JSON that isn't a glTF
    const char json[] = "{\"hello\":1}";
    TIDE_CHECK(!decode(json, strlen(json), &tide_mesh_decode_limits, &out, why, NULL));
    // A node that scales a vertex past a float: no infinity, and no NaN normal, in match state
    uint8_t buffer[44];
    char encoded[80], huge[2048];
    triangle_buffer(buffer);
    base64(buffer, sizeof buffer, encoded);
    snprintf(huge, sizeof huge,
             "{" TRIANGLE_PARTS "\"buffers\":[{\"byteLength\":44,\"uri\":\"data:application/octet-stream;base64,%s\"}],"
             "\"nodes\":[{\"mesh\":0,\"scale\":[3e38,3e38,3e38]},{\"children\":[0],\"scale\":[3e38,3e38,3e38]}],"
             "\"scenes\":[{\"nodes\":[1]}]}",
             encoded);
    TIDE_CHECK(!decode(huge, strlen(huge), &tide_mesh_decode_limits, &out, why, &limited));
    TIDE_CHECK(strstr(why, "past what a float holds") != NULL && !limited);
    tide_mesh_limits build = tide_mesh_decode_limits;
    build.finite = false; // A build's import keeps its old rule
    TIDE_CHECK(decode(huge, strlen(huge), &build, &out, why, NULL));
    tide_mesh_data_free(&out);
}

static char *text_of(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    const int size = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    char *out = malloc((size_t)size + 1u);
    if (!out) abort();
    va_start(args, fmt);
    vsnprintf(out, (size_t)size + 1u, fmt, args);
    va_end(args);
    return out;
}

static bool refused(const char *text, const tide_mesh_limits *limits, const char *expected)
{
    char why[256];
    tide_mesh_data out;
    bool limited = false;
    const bool ok = decode(text, strlen(text), limits, &out, why, &limited);
    if (ok) tide_mesh_data_free(&out);
    if (ok || !limited || !strstr(why, expected)) {
        printf("    got '%s'\n", ok ? "(read)" : why);
        return false;
    }
    return true;
}

TIDE_TEST(mesh_decode_refuses_files_past_its_limits)
{
    const tide_mesh_limits small = {.bytes = 4096u, .json_values = 200u, .vertices = 8u, .indices = 12u, .reads = 60u,
                                    .prefix = "TEST_MAX", .holder = "the test takes", .finite = true};
    // Each in its own way, from what the file says, before it's made
    char *big = calloc(5000u, 1u);
    if (!big) abort();
    memset(big, ' ', 4999u);
    TIDE_CHECK(refused(big, &small, "has 4,999 bytes, more than the 4,096 the test takes (TEST_MAX_BYTES)"));
    free(big);
    char *lines = text_of("%s", "");
    for (int i = 0; i < 11; i++) {
        char *more = text_of("%sv 0 0 %d\n", lines, i);
        free(lines);
        lines = more;
    }
    TIDE_CHECK(refused(lines, &small, "numbers in its vertex, normal and uv lines than the 60 the test takes (TEST_MAX_READS)"));
    free(lines);
    TIDE_CHECK(refused("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3 1 2 3 1\n", &small,
                       "more triangles in a face than the 4 the test takes (TEST_MAX_INDICES)"));
    TIDE_CHECK(refused("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\nf 1 2 3\nf 1 2 3\n", &small,
                       "has 9 vertices, more than the 8 the test takes (TEST_MAX_VERTICES)"));
    TIDE_CHECK(refused("{\"asset\":{\"version\":\"2.0\"},\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0}}]}],"
                       "\"accessors\":[{\"componentType\":5126,\"count\":9,\"type\":\"VEC3\"}]}",
                       &small, "has 9 vertices in a part, more than the 8 the test takes (TEST_MAX_VERTICES)"));
    char *values = text_of("%s", "{\"asset\":{\"version\":\"2.0\"},\"extras\":[0");
    for (int i = 0; i < 300; i++) {
        char *more = text_of("%s,0", values);
        free(values);
        values = more;
    }
    char *closed = text_of("%s]}", values);
    TIDE_CHECK(refused(closed, &small, "JSON values in its description than the 200 the test takes (TEST_MAX_JSON_VALUES)"));
    free(values);
    free(closed);
    // Decode's own numbers, by their names: a few bytes saying a million zeros
    char *zeros = text_of("{\"asset\":{\"version\":\"2.0\"},\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0}}]}],"
                          "\"accessors\":[{\"componentType\":5126,\"count\":%u,\"type\":\"VEC3\"}]}",
                          TIDE_MESH_DECODE_MAX_VERTICES + 1u);
    TIDE_CHECK(refused(zeros, &tide_mesh_decode_limits, "more than the 1,048,576 Mesh.Decode takes (TIDE_MESH_DECODE_MAX_VERTICES)"));
    free(zeros);
    char *corners = text_of("{\"asset\":{\"version\":\"2.0\"},\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}],"
                            "\"accessors\":[{\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
                            "{\"componentType\":5125,\"count\":%u,\"type\":\"SCALAR\"}]}",
                            TIDE_MESH_DECODE_MAX_INDICES + 1u);
    TIDE_CHECK(refused(corners, &tide_mesh_decode_limits, "more than the 6,291,456 Mesh.Decode takes (TIDE_MESH_DECODE_MAX_INDICES)"));
    free(corners);
}

// What a decode gives is always a mesh whose every index is a vertex and whose every number is a float's.
static void check_whole(const tide_mesh_data *m)
{
    for (uint32_t i = 0; i < m->indices; i++) TIDE_CHECK(m->index[i] < m->vertices);
    for (uint64_t i = 0; i < (uint64_t)m->vertices * 3u; i++) TIDE_CHECK(isfinite(m->position[i]) && isfinite(m->normal[i]));
    for (uint64_t i = 0; i < (uint64_t)m->vertices * 4u; i++) TIDE_CHECK(isfinite(m->color[i]) && isfinite(m->tangent[i]));
}

static void cut_and_spoil(const uint8_t *input, const size_t size)
{
    char why[256];
    for (size_t cut = 0; cut <= size; cut++) {
        uint8_t *copy = malloc(cut ? cut : 1u);
        if (!copy) abort();
        memcpy(copy, input, cut);
        tide_mesh_data out;
        if (decode(copy, cut, &tide_mesh_decode_limits, &out, why, NULL)) {
            check_whole(&out);
            tide_mesh_data_free(&out);
        }
        free(copy);
    }
    const uint8_t spoiled[] = {0, '9', 0x7f, 0xff};
    uint8_t *copy = malloc(size);
    if (!copy) abort();
    for (size_t at = 0; at < size; at++) {
        for (uint32_t k = 0; k < sizeof spoiled; k++) {
            memcpy(copy, input, size);
            copy[at] = spoiled[k];
            tide_mesh_data out;
            if (decode(copy, size, &tide_mesh_decode_limits, &out, why, NULL)) {
                check_whole(&out);
                tide_mesh_data_free(&out);
            }
        }
    }
    free(copy);
}

TIDE_TEST(mesh_decode_cut_and_spoiled_bytes_read_only_their_bytes)
{
    size_t glb_size;
    uint8_t *glb = triangle_glb(&glb_size);
    char *gltf = triangle_gltf("");
    cut_and_spoil(glb, glb_size);
    cut_and_spoil((const uint8_t *)gltf, strlen(gltf));
    cut_and_spoil((const uint8_t *)triangle_obj, strlen(triangle_obj));
    free(glb);
    free(gltf);
}
