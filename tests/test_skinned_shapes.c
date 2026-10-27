// Blend shapes (morph targets) of skinned models: decoding glTF targets into
// the canonical shape tables, their validation, and baking weights on the CPU.
#include "tide_test.h"
#include "tide/list.h"
#include "tide/math.h"
#include "tide/skinned.h"
#include "tide/skinned_data.h"
#include "tide/text.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma STDC FP_CONTRACT OFF

// A glTF written as it's made: its binary data, buffer views and accessors,
// then the rest of its JSON, packed as a GLB.
typedef struct gltf_maker {
    uint8_t bin[8192];
    uint32_t bin_size;
    char views[8192], accessors[16384];
    size_t views_size, accessors_size;
    uint32_t view_count, accessor_count;
} gltf_maker;

static void maker_add(char *text, size_t *size, size_t room, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    const int n = vsnprintf(text + *size, room - *size, fmt, args);
    va_end(args);
    if (n > 0) *size += (size_t)n;
}

static uint32_t maker_bytes(gltf_maker *m, const void *bytes, uint32_t size)
{
    const uint32_t at = m->bin_size;
    memcpy(m->bin + at, bytes, size);
    m->bin_size += (size + 3u) & ~3u;
    maker_add(m->views, &m->views_size, sizeof m->views, "%s{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u}",
              m->view_count ? "," : "", at, size);
    return m->view_count++;
}

// A float accessor of `count` elements of `width` numbers.
static uint32_t maker_floats(gltf_maker *m, const float *values, uint32_t count, uint32_t width)
{
    static const char *const types[] = {"", "SCALAR", "VEC2", "VEC3", "VEC4"};
    const uint32_t view = maker_bytes(m, values, count * width * 4u);
    maker_add(m->accessors, &m->accessors_size, sizeof m->accessors,
              "%s{\"bufferView\":%u,\"componentType\":5126,\"count\":%u,\"type\":\"%s\"}",
              m->accessor_count ? "," : "", view, count, types[width]);
    return m->accessor_count++;
}

// A VEC3 accessor of zeros with byte-sized sparse selectors replacing some.
static uint32_t maker_sparse3(gltf_maker *m, uint32_t count, const uint8_t *selectors, const float *values, uint32_t replaced)
{
    const uint32_t indices = maker_bytes(m, selectors, replaced);
    const uint32_t numbers = maker_bytes(m, values, replaced * 12u);
    maker_add(m->accessors, &m->accessors_size, sizeof m->accessors,
              "%s{\"componentType\":5126,\"count\":%u,\"type\":\"VEC3\",\"sparse\":{\"count\":%u,"
              "\"indices\":{\"bufferView\":%u,\"componentType\":5121},\"values\":{\"bufferView\":%u}}}",
              m->accessor_count ? "," : "", count, replaced, indices, numbers);
    return m->accessor_count++;
}

static uint32_t maker_indices(gltf_maker *m, const uint32_t *values, uint32_t count)
{
    const uint32_t view = maker_bytes(m, values, count * 4u);
    maker_add(m->accessors, &m->accessors_size, sizeof m->accessors,
              "%s{\"bufferView\":%u,\"componentType\":5125,\"count\":%u,\"type\":\"SCALAR\"}",
              m->accessor_count ? "," : "", view, count);
    return m->accessor_count++;
}

static void glb_word(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value, p[1] = (uint8_t)(value >> 8), p[2] = (uint8_t)(value >> 16), p[3] = (uint8_t)(value >> 24);
}

// The GLB of `rest`, the JSON after the buffers, views and accessors.
static uint8_t *maker_glb(const gltf_maker *m, const char *rest, size_t *size)
{
    const size_t room = strlen(rest) + m->views_size + m->accessors_size + 256u;
    char *json = malloc(room);
    if (!json) abort();
    size_t length = (size_t)snprintf(json, room, "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"byteLength\":%u}],"
                                     "\"bufferViews\":[%s],\"accessors\":[%s],%s}", m->bin_size, m->views, m->accessors, rest);
    while (length % 4u) json[length++] = ' ';
    *size = 28u + length + m->bin_size;
    uint8_t *glb = calloc(1, *size);
    if (!glb) abort();
    glb_word(glb, 0x46546c67u), glb_word(glb + 4, 2u), glb_word(glb + 8, (uint32_t)*size);
    glb_word(glb + 12, (uint32_t)length), glb_word(glb + 16, 0x4e4f534au);
    memcpy(glb + 20, json, length);
    glb_word(glb + 20 + length, m->bin_size), glb_word(glb + 24 + length, 0x004e4942u);
    memcpy(glb + 28 + length, m->bin, m->bin_size);
    free(json);
    return glb;
}

static bool decode_glb(const uint8_t *glb, size_t size, uint8_t **out, uint32_t *out_size, char why[256])
{
    char note[256];
    return tide_skinned_data_decode(glb, size, true, NULL, NULL, out, out_size, why, note);
}

static const float flat_normals[] = {0, 0, 1, 0, 0, 1, 0, 0, 1};
static const float flat_tangents[] = {1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1};

// Two meshes of three triangles. Mesh 0 has two parts, each with targets
// "smile" (default 0.5) and an unnamed one (sparse in the first part); mesh 1
// has an unnamed target and "smile" again, with tangent deltas it can't use.
static uint8_t *shaped_model(size_t *size, const char *mesh0_extra, const char *mesh1_names)
{
    gltf_maker m = {0};
    const uint32_t triangle[] = {0, 1, 2};
    const uint32_t indices = maker_indices(&m, triangle, 3u);
    const float part0[] = {0, 0, 0, 1, 0, 0, 0, 1, 0}, part1[] = {2, 0, 0, 3, 0, 0, 2, 1, 0}, part2[] = {0, 0, 5, 1, 0, 5, 0, 1, 5};
    const uint32_t p0 = maker_floats(&m, part0, 3u, 3u), p1 = maker_floats(&m, part1, 3u, 3u), p2 = maker_floats(&m, part2, 3u, 3u);
    const uint32_t normals = maker_floats(&m, flat_normals, 3u, 3u), tangents = maker_floats(&m, flat_tangents, 3u, 4u);
    const float smile0[] = {0.5f, 0, 0, 0, 0, 0, 0, 0.25f, 0}, smile0n[] = {0, 0.5f, 0, 0, 0, 0, 0, 0, 0};
    const float smile0t[] = {0, 0, 0, 0, 0, 1, 0, 0, 0}, smile1[] = {0, 0, 0, 0, 1, 0, 0, 0, 0};
    const float zeros[9] = {0}, smile2[] = {0, 0, -1, 0, 0, 0, 0, 0, 0}, unnamed2[] = {0, 0, 0, 0, 0, 0, 1, 1, 1};
    const uint32_t s0 = maker_floats(&m, smile0, 3u, 3u), s0n = maker_floats(&m, smile0n, 3u, 3u);
    const uint32_t s0t = maker_floats(&m, smile0t, 3u, 3u), s1 = maker_floats(&m, smile1, 3u, 3u);
    const uint8_t selector[] = {1};
    const float moved[] = {0, 0, 2};
    const uint32_t u0 = maker_sparse3(&m, 3u, selector, moved, 1u), z = maker_floats(&m, zeros, 3u, 3u);
    const uint32_t s2 = maker_floats(&m, smile2, 3u, 3u), u2 = maker_floats(&m, unnamed2, 3u, 3u), t2 = maker_floats(&m, smile0t, 3u, 3u);
    char rest[4096];
    snprintf(rest, sizeof rest,
             "\"nodes\":[{\"name\":\"root\",\"children\":[1,2]},{\"name\":\"a\",\"mesh\":0},{\"name\":\"b\",\"mesh\":1}],"
             "\"meshes\":[{\"primitives\":["
             "{\"attributes\":{\"POSITION\":%u,\"NORMAL\":%u,\"TANGENT\":%u},\"indices\":%u,"
             "\"targets\":[{\"POSITION\":%u,\"NORMAL\":%u,\"TANGENT\":%u},{\"POSITION\":%u}]},"
             "{\"attributes\":{\"POSITION\":%u,\"NORMAL\":%u,\"TANGENT\":%u},\"indices\":%u,"
             "\"targets\":[{\"POSITION\":%u},{\"POSITION\":%u}]}],"
             "\"weights\":[0.5,0],\"extras\":{\"targetNames\":[\"smile\",\"\"]}%s},"
             "{\"primitives\":[{\"attributes\":{\"POSITION\":%u,\"NORMAL\":%u},\"indices\":%u,"
             "\"targets\":[{\"POSITION\":%u},{\"POSITION\":%u,\"TANGENT\":%u}]}],"
             "\"weights\":[0,0.5],\"extras\":{\"targetNames\":%s}}]",
             p0, normals, tangents, indices, s0, s0n, s0t, u0, p1, normals, tangents, indices, s1, z, mesh0_extra,
             p2, normals, indices, u2, s2, t2, mesh1_names);
    return maker_glb(&m, rest, size);
}

static bool same_bits(float a, float b)
{
    return tide_f_bits(a) == tide_f_bits(b);
}

static bool vector_is(const float *v, float x, float y, float z)
{
    return same_bits(v[0], x) && same_bits(v[1], y) && same_bits(v[2], z);
}

TIDE_TEST(skinned_shapes_decode_targets_names_defaults_and_sparse_deltas)
{
    size_t size;
    uint8_t *glb = shaped_model(&size, "", "[\"\",\"smile\"]");
    uint8_t *data = NULL;
    uint32_t data_size = 0;
    char why[256];
    TIDE_REQUIRE(decode_glb(glb, size, &data, &data_size, why));
    TIDE_REQUIRE(tide_skinned_data_validate(data, data_size));
    tide_skinned_data_view v;
    TIDE_REQUIRE(tide_skinned_data_open(data, data_size, &v));
    TIDE_CHECK(data[4] == TIDE_SKINNED_SHAPED_VERSION && v.vertex_at >= TIDE_SKINNED_SHAPED_HEADER_SIZE);
    TIDE_CHECK(v.vertices == 9u && v.shapes == 3u && v.shape_deltas == 7u);
    // "smile" of both meshes is one shape; each mesh's unnamed target its own.
    tide_skinned_shape_data shapes[3];
    for (uint32_t i = 0; i < 3u; i++) TIDE_REQUIRE(tide_skinned_data_shape(&v, i, shapes + i));
    TIDE_CHECK(shapes[0].name_length == 5u && !memcmp(shapes[0].name, "smile", 5u) && same_bits(shapes[0].weight, 0.5f));
    TIDE_CHECK(shapes[1].name_length == 0u && shapes[1].weight == 0.0f && shapes[2].name_length == 0u);
    TIDE_CHECK(shapes[0].first_delta == 0u && shapes[0].deltas == 5u && shapes[1].first_delta == 5u && shapes[1].deltas == 1u
               && shapes[2].first_delta == 6u && shapes[2].deltas == 1u);
    tide_skinned_shape_delta_data d[7];
    for (uint32_t i = 0; i < 7u; i++) TIDE_REQUIRE(tide_skinned_data_shape_delta(&v, i, d + i));
    // Reflected in x as the vertices are, kept only where a vertex moves.
    TIDE_CHECK(d[0].vertex == 0u && vector_is(d[0].position, -0.5f, 0, 0) && vector_is(d[0].normal, 0, 0.5f, 0));
    TIDE_CHECK(d[1].vertex == 1u && vector_is(d[1].position, 0, 0, 0) && vector_is(d[1].tangent, 0, 0, 1));
    TIDE_CHECK(d[2].vertex == 2u && vector_is(d[2].position, 0, 0.25f, 0));
    TIDE_CHECK(d[3].vertex == 4u && vector_is(d[3].position, 0, 1, 0));
    TIDE_CHECK(d[4].vertex == 6u && vector_is(d[4].position, 0, 0, -1) && vector_is(d[4].tangent, 0, 0, 0));
    TIDE_CHECK(d[5].vertex == 1u && vector_is(d[5].position, 0, 0, 2));
    TIDE_CHECK(d[6].vertex == 8u && vector_is(d[6].position, -1, 1, 1));
    // The vertices have every shape at its default weight.
    tide_skinned_vertex_data vertex;
    TIDE_REQUIRE(tide_skinned_data_vertex(&v, 0u, &vertex));
    TIDE_CHECK(vector_is(vertex.position, -0.25f, 0, 0) && vector_is(vertex.normal, 0, 0.25f, 1));
    TIDE_REQUIRE(tide_skinned_data_vertex(&v, 1u, &vertex));
    TIDE_CHECK(vector_is(vertex.position, -1, 0, 0) && vector_is(vertex.tangent, -1, 0, 0.5f) && vertex.tangent[3] == -1.0f);
    TIDE_REQUIRE(tide_skinned_data_vertex(&v, 2u, &vertex));
    TIDE_CHECK(vector_is(vertex.position, 0, 1.125f, 0));
    TIDE_REQUIRE(tide_skinned_data_vertex(&v, 4u, &vertex));
    TIDE_CHECK(vector_is(vertex.position, -3, 0.5f, 0));
    TIDE_REQUIRE(tide_skinned_data_vertex(&v, 6u, &vertex));
    TIDE_CHECK(vector_is(vertex.position, 0, 0, 4.5f));
    TIDE_REQUIRE(tide_skinned_data_vertex(&v, 8u, &vertex));
    TIDE_CHECK(vector_is(vertex.position, 0, 1, 5));
    // The same model decodes to the same bytes.
    uint8_t *again = NULL;
    uint32_t again_size = 0;
    TIDE_REQUIRE(decode_glb(glb, size, &again, &again_size, why));
    TIDE_CHECK(again_size == data_size && !memcmp(again, data, data_size));
    tide_skinned_data_free(again);
    tide_skinned_data_free(data);
    free(glb);
}

TIDE_TEST(skinned_shapes_a_model_without_shapes_keeps_version_one)
{
    gltf_maker m = {0};
    const float positions[] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    const uint32_t p = maker_floats(&m, positions, 3u, 3u);
    char rest[256];
    snprintf(rest, sizeof rest, "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":%u}}],\"weights\":[1]}]", p);
    size_t size;
    uint8_t *glb = maker_glb(&m, rest, &size);
    uint8_t *data = NULL;
    uint32_t data_size;
    char why[256];
    TIDE_REQUIRE(decode_glb(glb, size, &data, &data_size, why));
    tide_skinned_data_view v;
    TIDE_REQUIRE(tide_skinned_data_open(data, data_size, &v));
    TIDE_CHECK(data[4] == TIDE_SKINNED_DATA_VERSION && v.node_at == TIDE_SKINNED_HEADER_SIZE && v.shapes == 0u);
    tide_skinned_shape_data shape;
    TIDE_CHECK(!tide_skinned_data_shape(&v, 0u, &shape));
    tide_skinned_data_free(data);
    free(glb);
}

TIDE_TEST(skinned_shapes_flat_parts_move_each_corner_and_weight_channels_are_skipped)
{
    gltf_maker m = {0};
    const float positions[] = {0, 0, 0, 1, 0, 0, 0, 1, 0}, up[] = {0, 1, 0, 0, 0, 0, 0, 0, 0}, times[] = {0, 1};
    const float weights[] = {0, 1}, moves[] = {0, 0, 0, 1, 0, 0};
    const uint32_t p = maker_floats(&m, positions, 3u, 3u), t = maker_floats(&m, up, 3u, 3u);
    const uint32_t input = maker_floats(&m, times, 2u, 1u), output = maker_floats(&m, weights, 2u, 1u);
    const uint32_t moved = maker_floats(&m, moves, 2u, 3u);
    char rest[1024];
    snprintf(rest, sizeof rest,
             "\"nodes\":[{\"name\":\"a\",\"mesh\":0}],\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":%u},"
             "\"targets\":[{\"POSITION\":%u,\"NORMAL\":%u}]}],\"extras\":{\"targetNames\":[\"lift\"]}}],"
             "\"animations\":[{\"name\":\"go\",\"samplers\":[{\"input\":%u,\"output\":%u},{\"input\":%u,\"output\":%u}],"
             "\"channels\":[{\"sampler\":0,\"target\":{\"node\":0,\"path\":\"weights\"}},"
             "{\"sampler\":1,\"target\":{\"node\":0,\"path\":\"translation\"}}]}]",
             p, t, t, input, output, input, moved);
    size_t size;
    uint8_t *glb = maker_glb(&m, rest, &size);
    uint8_t *data = NULL;
    uint32_t data_size;
    char why[256];
    TIDE_REQUIRE(decode_glb(glb, size, &data, &data_size, why));
    tide_skinned_data_view v;
    TIDE_REQUIRE(tide_skinned_data_open(data, data_size, &v));
    TIDE_CHECK(v.clips == 1u && v.channels == 1u && v.shapes == 1u && v.shape_deltas == 1u);
    tide_skinned_shape_delta_data delta;
    TIDE_REQUIRE(tide_skinned_data_shape_delta(&v, 0u, &delta));
    // The importer's own flat normals don't follow the shape.
    tide_skinned_vertex_data vertex;
    TIDE_REQUIRE(tide_skinned_data_vertex(&v, delta.vertex, &vertex));
    TIDE_CHECK(vector_is(vertex.position, 0, 0, 0) && vector_is(delta.position, 0, 1, 0) && vector_is(delta.normal, 0, 0, 0));
    tide_skinned_data_free(data);
    free(glb);
}

static void refused(const char *mesh0_extra, const char *mesh1_names, const char *with)
{
    size_t size;
    uint8_t *glb = shaped_model(&size, mesh0_extra, mesh1_names);
    uint8_t *data = NULL;
    uint32_t data_size = 0;
    char why[256];
    const bool decoded = decode_glb(glb, size, &data, &data_size, why);
    if (decoded || !strstr(why, with)) printf("expected a refusal with '%s', got '%s'\n", with, decoded ? "a model" : why);
    TIDE_CHECK(!decoded && !data && !data_size && strstr(why, with));
    free(glb);
}

TIDE_TEST(skinned_shapes_refuse_conflicting_and_malformed_targets)
{
    refused("", "[\"dup\",\"dup\"]", "two morph targets named 'dup' in one mesh");
    gltf_maker m = {0};
    const float positions[] = {0, 0, 0, 1, 0, 0, 0, 1, 0}, two[] = {0, 0, 0, 0, 0, 0};
    const uint32_t p = maker_floats(&m, positions, 3u, 3u), short_target = maker_floats(&m, two, 2u, 3u);
    const char *const cases[][2] = {
        {"\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"targets\":[{\"POSITION\":0},{\"POSITION\":0}]}],"
         "\"weights\":[0]}]", "morph target weights of another count"},
        {"\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"targets\":[{\"POSITION\":0}]},"
         "{\"attributes\":{\"POSITION\":0}," "\"targets\":[{\"POSITION\":0},{\"POSITION\":0}]}]}]", "different numbers of morph targets"},
        {"\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"targets\":[{\"POSITION\":1}]}]}]", "wrong count or width"},
        {"\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"targets\":{\"POSITION\":0}}]}]", "aren't an array"},
        {"\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"targets\":[3]}]}]", "isn't an object"},
        {"\"nodes\":[{\"mesh\":0},{\"mesh\":1}],\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"targets\":[{\"POSITION\":0}]}],"
         "\"extras\":{\"targetNames\":[\"a\"]}},{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"targets\":[{\"POSITION\":0}]}],"
         "\"weights\":[1],\"extras\":{\"targetNames\":[\"a\"]}}]", "different default weights"},
    };
    (void)p, (void)short_target;
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        size_t size;
        uint8_t *glb = maker_glb(&m, cases[i][0], &size);
        uint8_t *data = NULL;
        uint32_t data_size = 0;
        char why[256];
        const bool decoded = decode_glb(glb, size, &data, &data_size, why);
        if (decoded || !strstr(why, cases[i][1])) printf("case %zu: expected '%s', got '%s'\n", i, cases[i][1], decoded ? "a model" : why);
        TIDE_CHECK(!decoded && strstr(why, cases[i][1]));
        free(glb);
    }
    // More targets in a mesh than a model has shapes
    const size_t room = (size_t)(TIDE_SKINNED_MAX_SHAPES + 1u) * 16u + 256u;
    char *many = malloc(room);
    TIDE_REQUIRE(many);
    size_t at = (size_t)snprintf(many, room, "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"targets\":[");
    for (uint32_t i = 0; i <= TIDE_SKINNED_MAX_SHAPES; i++) at += (size_t)snprintf(many + at, room - at, "%s{\"POSITION\":0}", i ? "," : "");
    snprintf(many + at, room - at, "]}]}]");
    size_t size;
    uint8_t *glb = maker_glb(&m, many, &size);
    uint8_t *data = NULL;
    uint32_t data_size = 0;
    char why[256];
    TIDE_CHECK(!decode_glb(glb, size, &data, &data_size, why) && strstr(why, "TIDE_SKINNED_MAX_SHAPES"));
    free(glb);
    free(many);
}

static void put(uint8_t *p, uint32_t value)
{
    glb_word(p, value);
}

TIDE_TEST(skinned_shapes_cut_and_spoiled_tables_are_checked)
{
    size_t size;
    uint8_t *glb = shaped_model(&size, "", "[\"\",\"smile\"]");
    uint8_t *data = NULL;
    uint32_t data_size = 0;
    char why[256];
    TIDE_REQUIRE(decode_glb(glb, size, &data, &data_size, why));
    free(glb);
    tide_skinned_data_view v;
    TIDE_REQUIRE(tide_skinned_data_open(data, data_size, &v));
    for (uint32_t cut = 0; cut < data_size; cut++) {
        uint8_t *copy = malloc(cut ? cut : 1u);
        TIDE_REQUIRE(copy);
        memcpy(copy, data, cut);
        if (cut >= 12u) put(copy + 8, cut); // A size that agrees, so the tables are what's checked
        TIDE_CHECK(!tide_skinned_data_validate(copy, cut));
        free(copy);
    }
    uint8_t *spoiled = malloc(data_size);
    TIDE_REQUIRE(spoiled);
    const uint32_t first_delta = v.shape_delta_at, shape1 = v.shape_at + TIDE_SKINNED_SHAPE_SIZE;
    const struct { uint32_t at, value; } spoils[] = {
        {first_delta, v.vertices},                                   // A vertex the model hasn't got
        {first_delta + TIDE_SKINNED_SHAPE_DELTA_SIZE, 0u},           // Not after the delta before
        {shape1 + 8u, 4u},                                           // A range that doesn't follow the one before
        {v.shape_at + 12u, 7u},                                      // A range past the deltas
        {v.shape_at + 16u, 0x7fc00000u},                             // A weight that's no number
        {first_delta + 4u, 0x7f800000u},                             // A delta that's no number
        {120u, 0u},                                                  // Version 2 with no shapes
        {132u, data_size},                                           // Deltas past the bytes
        {124u, 122u},                                                // Shapes inside the header
    };
    for (size_t i = 0; i < sizeof spoils / sizeof spoils[0]; i++) {
        memcpy(spoiled, data, data_size);
        put(spoiled + spoils[i].at, spoils[i].value);
        TIDE_CHECK(!tide_skinned_data_validate(spoiled, data_size));
    }
    // A model of version 1 saying it has shape tables has none: the words are its nodes'.
    memcpy(spoiled, data, data_size);
    put(spoiled + 4, TIDE_SKINNED_DATA_VERSION);
    tide_skinned_data_view old;
    TIDE_CHECK(!tide_skinned_data_open(spoiled, data_size, &old) || old.shapes == 0u);
    free(spoiled);
    tide_skinned_data_free(data);
}

typedef struct shaped_vertex {
    tide_float3 position;
    tide_float2 uv;
    tide_float3 normal;
    tide_float4 tangent;
    float marker;
} shaped_vertex;

static tide_list shaped_vertices(const tide_skinned_data_view *v)
{
    tide_list out = {0};
    tide_list_resize(&out, (int32_t)v->vertices, sizeof(shaped_vertex), TIDE_IN_SCRATCH);
    for (uint32_t i = 0; i < v->vertices; i++) {
        tide_skinned_vertex_data from;
        if (!tide_skinned_data_vertex(v, i, &from)) abort();
        shaped_vertex *to = tide_list_at_mut(out, (int32_t)i, sizeof *to);
        memcpy(&to->position, from.position, 12u);
        memcpy(&to->normal, from.normal, 12u);
        memcpy(&to->tangent, from.tangent, 16u);
        to->uv = tide_f2(from.uv[0], (float)i);
        to->marker = (float)i + 0.5f;
    }
    return out;
}

static tide_list floats_of(const float *values, int32_t count)
{
    return tide_list_from(values, count, sizeof(float));
}

TIDE_TEST(skinned_shapes_bake_weights_on_the_cpu)
{
    const uint32_t mark = tide_scratch_mark();
    size_t size;
    uint8_t *glb = shaped_model(&size, "", "[\"\",\"smile\"]");
    uint8_t *bytes = NULL;
    uint32_t data_size = 0;
    char why[256];
    TIDE_REQUIRE(decode_glb(glb, size, &bytes, &data_size, why));
    free(glb);
    const tide_list data = tide_list_from(bytes, (int32_t)data_size, 1u);
    tide_skinned_data_view v;
    TIDE_REQUIRE(tide_skinned_data_open(tide_list_at(data, 0, 1u), data_size, &v));
    TIDE_CHECK(tide_skinned_shape_count(data) == 3);
    TIDE_CHECK(tide_skinned_find_shape(data, tide_str_from_cstr("smile")) == 0);
    TIDE_CHECK(tide_skinned_find_shape(data, tide_str_from_cstr("")) == -1 && tide_skinned_find_shape(data, tide_str_from_cstr("frown")) == -1);
    TIDE_CHECK(tide_str_eq(tide_skinned_shape_name(data, 0), tide_str_from_cstr("smile")) && tide_skinned_shape_name(data, 1).bytes == 0);
    TIDE_CHECK(tide_skinned_shape_weight(data, 0) == 0.5f && tide_skinned_shape_weight(data, 2) == 0.0f
               && tide_skinned_shape_weight(data, 3) == 0.0f && tide_skinned_shape_weight(data, -1) == 0.0f);
    const tide_list base = shaped_vertices(&v);
    const int32_t places[3] = {(int32_t)offsetof(shaped_vertex, position), (int32_t)offsetof(shaped_vertex, normal),
                               (int32_t)offsetof(shaped_vertex, tangent)};
    const uint32_t each = sizeof(shaped_vertex);
    // Defaults, a weight list that's short of them, and weights that are no number: nothing moves, nothing's copied.
    const float defaults[] = {0.5f, 0.0f, 0.0f}, nan = tide_f_from_bits(0x7fc00000u), none[] = {nan, tide_f_from_bits(0x7f800000u)};
    TIDE_CHECK(tide_skinned_shaped(data, floats_of(defaults, 3), base, each, places[0], places[1], places[2]).at == base.at);
    TIDE_CHECK(tide_skinned_shaped(data, (tide_list){0}, base, each, places[0], places[1], places[2]).at == base.at);
    TIDE_CHECK(tide_skinned_shaped(data, floats_of(none, 2), base, each, places[0], places[1], places[2]).at == base.at);
    // Vertices that aren't the model's, and places past a vertex, are left as they are.
    const float weights[] = {1.5f, 1.0f, -2.0f};
    tide_list fewer = tide_list_copy(base, each);
    tide_list_resize(&fewer, 3, each, TIDE_IN_SCRATCH);
    TIDE_CHECK(tide_skinned_shaped(data, floats_of(weights, 3), fewer, each, places[0], places[1], places[2]).at == fewer.at);
    TIDE_CHECK(tide_skinned_shaped(data, floats_of(weights, 3), base, each, (int32_t)each - 8, -1, -1).at == base.at);
    TIDE_CHECK(tide_skinned_shaped((tide_list){0}, floats_of(weights, 3), base, each, places[0], -1, -1).at == base.at);
    // Each shape adds (weight - default) * delta, in shape order.
    const tide_list shaped = tide_skinned_shaped(data, floats_of(weights, 3), base, each, places[0], places[1], places[2]);
    TIDE_REQUIRE(shaped.at != base.at && tide_list_count(shaped) == 9);
    shaped_vertex expect[9];
    for (int32_t i = 0; i < 9; i++) memcpy(expect + i, tide_list_at(base, i, each), each);
    for (uint32_t s = 0; s < 3u; s++) {
        tide_skinned_shape_data shape;
        TIDE_REQUIRE(tide_skinned_data_shape(&v, s, &shape));
        const float by = weights[s] - shape.weight;
        for (uint32_t k = 0; k < shape.deltas; k++) {
            tide_skinned_shape_delta_data d;
            TIDE_REQUIRE(tide_skinned_data_shape_delta(&v, shape.first_delta + k, &d));
            float *to[3] = {&expect[d.vertex].position.x, &expect[d.vertex].normal.x, &expect[d.vertex].tangent.x};
            const float *from[3] = {d.position, d.normal, d.tangent};
            for (uint32_t a = 0; a < 3u; a++) for (uint32_t c = 0; c < 3u; c++) to[a][c] = to[a][c] + by * from[a][c];
        }
    }
    for (int32_t i = 0; i < 9; i++) TIDE_CHECK(!memcmp(tide_list_at(shaped, i, each), expect + i, each));
    const shaped_vertex *v0 = tide_list_at(shaped, 0, each), *v1 = tide_list_at(shaped, 1, each), *v8 = tide_list_at(shaped, 8, each);
    TIDE_CHECK(same_bits(v0->position.x, -0.75f) && same_bits(v0->normal.y, 0.75f) && same_bits(v0->marker, 0.5f));
    TIDE_CHECK(same_bits(v1->position.z, 2.0f) && same_bits(v1->tangent.z, 1.5f) && same_bits(v1->tangent.w, -1.0f)
               && same_bits(v1->uv.y, 1.0f));
    TIDE_CHECK(same_bits(v8->position.x, 2.0f) && same_bits(v8->position.y, -1.0f) && same_bits(v8->position.z, 3.0f));
    // The base is unchanged, and the same weights give the same bits again.
    const shaped_vertex *b0 = tide_list_at(base, 0, each);
    TIDE_CHECK(same_bits(b0->position.x, -0.25f));
    const tide_list again = tide_skinned_shaped(data, floats_of(weights, 3), base, each, places[0], places[1], places[2]);
    for (int32_t i = 0; i < 9; i++) TIDE_CHECK(!memcmp(tide_list_at(again, i, each), tide_list_at(shaped, i, each), each));
    // Only the fields named are moved: no normal or tangent place moves only positions.
    const tide_list positions = tide_skinned_shaped(data, floats_of(weights, 3), base, each, places[0], -1, -1);
    const shaped_vertex *p0 = tide_list_at(positions, 0, each);
    TIDE_CHECK(same_bits(p0->position.x, -0.75f) && same_bits(p0->normal.y, 0.25f));
    // A displacement past float range leaves that number as it was.
    const float huge[] = {0.5f, 3.0e38f, 0.0f};
    const tide_list kept = tide_skinned_shaped(data, floats_of(huge, 3), base, each, places[0], places[1], places[2]);
    const shaped_vertex *k1 = tide_list_at(kept, 1, each), *b1 = tide_list_at(base, 1, each);
    TIDE_CHECK(kept.at != base.at && same_bits(k1->position.z, b1->position.z) && same_bits(k1->position.x, b1->position.x));
    tide_skinned_data_free(bytes);
    tide_scratch_reset(mark);
}

static uint8_t *two_shapes_model(size_t *size, bool targets)
{
    gltf_maker m = {0};
    const float positions[] = {0, 0, 0, 1, 0, 0, 0, 1, 0}, a[] = {0, 0, 0, 0, 1, 0, 0, 0, 0}, b[] = {0, 0, 0, 0, 0, 0, 0, 0, 2};
    const uint32_t p = maker_floats(&m, positions, 3u, 3u), n = maker_floats(&m, flat_normals, 3u, 3u);
    const uint32_t ta = maker_floats(&m, a, 3u, 3u), tb = maker_floats(&m, b, 3u, 3u);
    char rest[512];
    if (targets) {
        snprintf(rest, sizeof rest, "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":%u,\"NORMAL\":%u},"
                 "\"targets\":[{\"POSITION\":%u},{\"POSITION\":%u}]}],\"extras\":{\"targetNames\":[\"jawOpen\",\"browUp\"]}}]",
                 p, n, ta, tb);
    } else {
        snprintf(rest, sizeof rest, "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":%u,\"NORMAL\":%u}}]}]", p, n);
    }
    return maker_glb(&m, rest, size);
}

static bool keep_all(void *user, const char *name, uint32_t length) { (void)user, (void)name, (void)length; return true; }
static bool keep_none(void *user, const char *name, uint32_t length) { (void)user, (void)name, (void)length; return false; }
static bool keep_named(void *user, const char *name, uint32_t length)
{
    return strlen(user) == length && !memcmp(user, name, length);
}

TIDE_TEST(skinned_shapes_kept_by_name_rewrite_the_model_as_the_decoder_would)
{
    size_t size, plain_size;
    uint8_t *glb = two_shapes_model(&size, true), *plain_glb = two_shapes_model(&plain_size, false);
    uint8_t *data = NULL, *plain = NULL, *out = NULL;
    uint32_t data_size = 0, plain_bytes = 0, out_size = 0;
    char why[256];
    TIDE_REQUIRE(decode_glb(glb, size, &data, &data_size, why) && decode_glb(plain_glb, plain_size, &plain, &plain_bytes, why));
    // Everything kept: the same bytes.
    TIDE_REQUIRE(tide_skinned_data_keep_shapes(data, data_size, keep_all, NULL, &out, &out_size));
    TIDE_CHECK(out_size == data_size && !memcmp(out, data, data_size));
    tide_skinned_data_free(out);
    // Nothing kept: version 1, the model without targets byte for byte.
    TIDE_REQUIRE(tide_skinned_data_keep_shapes(data, data_size, keep_none, NULL, &out, &out_size));
    TIDE_CHECK(out[4] == TIDE_SKINNED_DATA_VERSION && out_size == plain_bytes && !memcmp(out, plain, plain_bytes));
    tide_skinned_data_free(out);
    // One kept: that shape's name, weight and deltas, nothing else.
    TIDE_REQUIRE(tide_skinned_data_keep_shapes(data, data_size, keep_named, "browUp", &out, &out_size));
    TIDE_REQUIRE(tide_skinned_data_validate(out, out_size));
    tide_skinned_data_view v;
    TIDE_REQUIRE(tide_skinned_data_open(out, out_size, &v));
    tide_skinned_shape_data shape;
    tide_skinned_shape_delta_data delta;
    TIDE_CHECK(v.shapes == 1u && v.shape_deltas == 1u && tide_skinned_data_shape(&v, 0, &shape)
               && shape.name_length == 6u && !memcmp(shape.name, "browUp", 6u));
    TIDE_CHECK(tide_skinned_data_shape_delta(&v, 0, &delta) && delta.vertex == 2u && vector_is(delta.position, 0, 0, 2));
    tide_skinned_data_free(out);
    // Bytes that aren't a model are refused, with nothing made.
    data[0] ^= 1u;
    TIDE_CHECK(!tide_skinned_data_keep_shapes(data, data_size, keep_all, NULL, &out, &out_size) && !out && !out_size);
    tide_skinned_data_free(data);
    tide_skinned_data_free(plain);
    free(glb);
    free(plain_glb);
}
