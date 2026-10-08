#include "tide_test.h"
#include "tide/skinned_data.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct model_fixture {
    uint8_t bin[2048];
    uint32_t bin_size;
    char json[8192];
    size_t json_size;
} model_fixture;

static void fixture_word(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value, p[1] = (uint8_t)(value >> 8), p[2] = (uint8_t)(value >> 16), p[3] = (uint8_t)(value >> 24);
}

static void fixture_float(uint8_t *p, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    fixture_word(p, bits);
}

static uint32_t fixture_floats(model_fixture *f, const float *values, uint32_t count)
{
    const uint32_t at = f->bin_size;
    for (uint32_t i = 0; i < count; i++) fixture_float(f->bin + at + i * 4u, values[i]);
    f->bin_size += count * 4u;
    return at;
}

static void make_fixture(model_fixture *f, bool external)
{
    memset(f, 0, sizeof *f);
    uint32_t offsets[11], sizes[11];
    const float positions[] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    offsets[0] = fixture_floats(f, positions, 9u), sizes[0] = 36u;
    offsets[1] = f->bin_size, sizes[1] = 12u;
    for (uint32_t i = 0; i < 3u; i++) for (uint32_t k = 0; k < 4u; k++) f->bin[f->bin_size++] = (uint8_t)k;
    const float first_weights[] = {.2f, .2f, .2f, .2f, .2f, .2f, .2f, .2f, .2f, .2f, .2f, .2f};
    offsets[2] = fixture_floats(f, first_weights, 12u), sizes[2] = 48u;
    offsets[3] = f->bin_size, sizes[3] = 12u;
    for (uint32_t i = 0; i < 3u; i++) {
        f->bin[f->bin_size++] = 4u;
        for (uint32_t k = 0; k < 3u; k++) f->bin[f->bin_size++] = 0u;
    }
    const float second_weights[] = {.2f, 0, 0, 0, .2f, 0, 0, 0, .2f, 0, 0, 0};
    offsets[4] = fixture_floats(f, second_weights, 12u), sizes[4] = 48u;
    float inverse[80] = {0};
    for (uint32_t i = 0; i < 5u; i++) {
        inverse[i * 16u] = inverse[i * 16u + 5u] = inverse[i * 16u + 10u] = inverse[i * 16u + 15u] = 1;
        inverse[i * 16u + 12u] = -2;
        if (i == 1u) inverse[i * 16u + 13u] = -1;
    }
    offsets[5] = fixture_floats(f, inverse, 80u), sizes[5] = 320u;
    const float times[] = {0, 2};
    offsets[6] = fixture_floats(f, times, 2u), sizes[6] = 8u;
    const float translations[] = {2, 0, 0, 4, 0, 0};
    offsets[7] = fixture_floats(f, translations, 6u), sizes[7] = 24u;
    const float rotations[] = {0, 0, 0, 1, 0, 0, 1, 0};
    offsets[8] = fixture_floats(f, rotations, 8u), sizes[8] = 32u;
    const float scales[] = {1, 1, 1, 2, 2, 2};
    offsets[9] = fixture_floats(f, scales, 6u), sizes[9] = 24u;
    const float cubic[] = {0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0};
    offsets[10] = fixture_floats(f, cubic, 18u), sizes[10] = 72u;
    char views[2048];
    size_t at = 0;
    for (uint32_t i = 0; i < 11u; i++) {
        at += (size_t)snprintf(views + at, sizeof views - at, "%s{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u}",
                              i ? "," : "", offsets[i], sizes[i]);
    }
    f->json_size = (size_t)snprintf(f->json, sizeof f->json,
        "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"byteLength\":%u%s}],\"bufferViews\":[%s],"
        "\"accessors\":["
        "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
        "{\"bufferView\":1,\"componentType\":5121,\"count\":3,\"type\":\"VEC4\"},"
        "{\"bufferView\":2,\"componentType\":5126,\"count\":3,\"type\":\"VEC4\"},"
        "{\"bufferView\":3,\"componentType\":5121,\"count\":3,\"type\":\"VEC4\"},"
        "{\"bufferView\":4,\"componentType\":5126,\"count\":3,\"type\":\"VEC4\"},"
        "{\"bufferView\":5,\"componentType\":5126,\"count\":5,\"type\":\"MAT4\"},"
        "{\"bufferView\":6,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\"},"
        "{\"bufferView\":7,\"componentType\":5126,\"count\":2,\"type\":\"VEC3\"},"
        "{\"bufferView\":8,\"componentType\":5126,\"count\":2,\"type\":\"VEC4\"},"
        "{\"bufferView\":9,\"componentType\":5126,\"count\":2,\"type\":\"VEC3\"},"
        "{\"bufferView\":10,\"componentType\":5126,\"count\":6,\"type\":\"VEC3\"}],"
        "\"nodes\":[{\"name\":\"Hand.R\",\"translation\":[0,1,0],\"children\":[4]},"
        "{\"name\":\"HeroMesh\",\"mesh\":0,\"skin\":0,\"translation\":[999,999,999]},"
        "{\"name\":\"Root\",\"translation\":[2,0,0],\"children\":[0,1,5,6,7]},"
        "{\"name\":\"Helper\",\"children\":[2]},"
        "{\"name\":\"Sword\",\"mesh\":1,\"translation\":[0,2,0]},"
        "{\"name\":\"JointA\"},{\"name\":\"JointB\"},{\"name\":\"JointC\"}],"
        "\"skins\":[{\"joints\":[2,0,5,6,7],\"inverseBindMatrices\":5}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"JOINTS_0\":1,\"WEIGHTS_0\":2,\"JOINTS_1\":3,\"WEIGHTS_1\":4}}]},"
        "{\"primitives\":[{\"attributes\":{\"POSITION\":0}}]}],\"scenes\":[{\"nodes\":[3]}],\"scene\":0,"
        "\"animations\":[{\"name\":\"RUN\",\"samplers\":[{\"input\":6,\"output\":7},{\"input\":6,\"output\":8},{\"input\":6,\"output\":9,\"interpolation\":\"STEP\"}],"
        "\"channels\":[{\"sampler\":0,\"target\":{\"node\":2,\"path\":\"translation\"}},{\"sampler\":1,\"target\":{\"node\":0,\"path\":\"rotation\"}},"
        "{\"sampler\":2,\"target\":{\"node\":3,\"path\":\"scale\"}}]},"
        "{\"name\":\"Cubic\",\"samplers\":[{\"input\":6,\"output\":10,\"interpolation\":\"CUBICSPLINE\"}],"
        "\"channels\":[{\"sampler\":0,\"target\":{\"node\":0,\"path\":\"translation\"}}]}]}",
        f->bin_size, external ? ",\"uri\":\"model.bin\"" : "", views);
}

static uint8_t *fixture_glb(const model_fixture *f, size_t *size)
{
    const uint32_t json_size = ((uint32_t)f->json_size + 3u) & ~3u;
    const uint32_t bin_size = (f->bin_size + 3u) & ~3u;
    *size = 28u + json_size + bin_size;
    uint8_t *bytes = calloc(*size, 1u);
    if (!bytes) abort();
    memcpy(bytes, "glTF", 4u);
    fixture_word(bytes + 4, 2u), fixture_word(bytes + 8, (uint32_t)*size);
    fixture_word(bytes + 12, json_size), fixture_word(bytes + 16, 0x4e4f534au);
    memset(bytes + 20, ' ', json_size);
    memcpy(bytes + 20, f->json, f->json_size);
    fixture_word(bytes + 20 + json_size, bin_size), fixture_word(bytes + 24 + json_size, 0x004e4942u);
    memcpy(bytes + 28 + json_size, f->bin, f->bin_size);
    return bytes;
}

static const uint8_t *fixture_side(void *user, const char *path, size_t *size)
{
    const model_fixture *f = user;
    if (strcmp(path, "model.bin") != 0) return NULL;
    *size = f->bin_size;
    return f->bin;
}

static bool fixture_decode(const void *input, size_t size, bool runtime, const model_fixture *side,
                           uint8_t **out, uint32_t *out_size)
{
    char why[256], note[256];
    const bool ok = tide_skinned_data_decode(input, size, runtime, side ? fixture_side : NULL, (void *)side, out, out_size, why, note);
    if (!ok) printf("    decode: %s; %s\n", why, note);
    return ok;
}

TIDE_TEST(skinned_decode_preserves_hierarchy_skin_sets_and_animation_curves)
{
    model_fixture f;
    make_fixture(&f, false);
    size_t input_size;
    uint8_t *input = fixture_glb(&f, &input_size), *bytes;
    uint32_t size;
    TIDE_REQUIRE(fixture_decode(input, input_size, true, NULL, &bytes, &size));
    free(input);
    tide_skinned_data_view view;
    TIDE_REQUIRE(tide_skinned_data_open(bytes, size, &view));
    TIDE_CHECK(view.nodes == 9u && view.palettes == 6u && view.vertices == 6u && view.indices == 6u);
    TIDE_CHECK(view.clips == 2u && view.channels == 4u);
    const uint32_t expected_sources[] = {UINT32_MAX, 3, 2, 0, 1, 4, 5, 6, 7};
    for (uint32_t i = 0; i < view.nodes; i++) {
        tide_skinned_node_data node;
        TIDE_REQUIRE(tide_skinned_data_node(&view, i, &node));
        TIDE_CHECK(node.source_node == expected_sources[i]);
        if (i) TIDE_CHECK(node.parent >= 0 && (uint32_t)node.parent < i);
        if (i == 2u) TIDE_CHECK(node.position[0] == -2.0f);
        if (i == 3u) TIDE_CHECK(strcmp(node.name, "Hand.R") == 0 && node.parent == 2);
    }
    tide_skinned_palette_data palette;
    TIDE_REQUIRE(tide_skinned_data_palette(&view, 0u, &palette));
    TIDE_CHECK(palette.node == 2u && palette.inverse_bind[12] == 2.0f);
    bool has_five = false, has_rigid = false, reflected = false;
    for (uint32_t i = 0; i < view.vertices; i++) {
        tide_skinned_vertex_data vertex;
        TIDE_REQUIRE(tide_skinned_data_vertex(&view, i, &vertex));
        has_five |= vertex.influences == 5u;
        has_rigid |= vertex.influences == 1u;
        reflected |= vertex.position[0] == -1.0f;
        TIDE_CHECK(vertex.normal[2] == 1.0f);
        float total = 0;
        for (uint32_t k = 0; k < vertex.influences; k++) {
            tide_skinned_influence_data inf;
            TIDE_REQUIRE(tide_skinned_data_influence(&view, vertex.first_influence + k, &inf));
            total += inf.weight;
        }
        TIDE_CHECK(total > .9999f && total < 1.0001f);
    }
    TIDE_CHECK(has_five && has_rigid && reflected);
    tide_skinned_clip_data clip;
    TIDE_REQUIRE(tide_skinned_data_clip(&view, 0u, &clip));
    TIDE_CHECK(strcmp(clip.name, "RUN") == 0 && clip.length == 2 && clip.channels == 3u);
    tide_skinned_channel_data channel;
    float time, value[4], in[4], out[4];
    TIDE_REQUIRE(tide_skinned_data_channel(&view, 0u, &channel));
    TIDE_REQUIRE(tide_skinned_data_key(&view, &channel, 1u, &time, value, in, out));
    TIDE_CHECK(time == 2 && value[0] == -4 && channel.node == 2u && channel.interpolation == TIDE_SKINNED_LINEAR);
    TIDE_REQUIRE(tide_skinned_data_channel(&view, 1u, &channel));
    TIDE_REQUIRE(tide_skinned_data_key(&view, &channel, 1u, &time, value, in, out));
    TIDE_CHECK(value[2] == -1 && value[3] == 0);
    TIDE_REQUIRE(tide_skinned_data_channel(&view, 2u, &channel));
    TIDE_CHECK(channel.interpolation == TIDE_SKINNED_STEP);
    TIDE_REQUIRE(tide_skinned_data_channel(&view, 3u, &channel));
    TIDE_REQUIRE(tide_skinned_data_key(&view, &channel, 0u, &time, value, in, out));
    TIDE_CHECK(channel.interpolation == TIDE_SKINNED_CUBICSPLINE && value[1] == 1 && out[1] == 1);
    TIDE_CHECK(tide_skinned_data_validate(bytes, size));
    tide_skinned_data_free(bytes);
}

TIDE_TEST(skinned_decode_external_gltf_and_self_contained_glb_agree)
{
    model_fixture f;
    make_fixture(&f, true);
    uint8_t *external, *embedded;
    uint32_t external_size, embedded_size;
    TIDE_REQUIRE(fixture_decode(f.json, f.json_size, false, &f, &external, &external_size));
    make_fixture(&f, false);
    size_t input_size;
    uint8_t *input = fixture_glb(&f, &input_size);
    TIDE_REQUIRE(fixture_decode(input, input_size, true, NULL, &embedded, &embedded_size));
    TIDE_CHECK(external_size == embedded_size && memcmp(external, embedded, embedded_size) == 0);
    free(input);
    tide_skinned_data_free(external);
    tide_skinned_data_free(embedded);
}

TIDE_TEST(skinned_decode_runtime_rejects_external_resources)
{
    model_fixture f;
    make_fixture(&f, true);
    size_t input_size;
    uint8_t *input = fixture_glb(&f, &input_size), *bytes = (uint8_t *)1;
    uint32_t size = 123u;
    char why[256], note[256];
    TIDE_CHECK(!tide_skinned_data_decode(input, input_size, true, fixture_side, &f, &bytes, &size, why, note));
    TIDE_CHECK(!bytes && !size && strstr(why, "external buffer"));
    free(input);
    TIDE_CHECK(!tide_skinned_data_decode(f.json, f.json_size, true, fixture_side, &f, &bytes, &size, why, note));
    TIDE_CHECK(!bytes && !size && strstr(why, "self-contained GLB"));
}

TIDE_TEST(skinned_decode_cut_and_spoiled_glb_reads_only_its_bytes)
{
    model_fixture f;
    make_fixture(&f, false);
    size_t input_size;
    uint8_t *input = fixture_glb(&f, &input_size);
    char why[256], note[256];
    for (size_t cut = 0; cut < input_size; cut++) {
        uint8_t *copy = malloc(cut ? cut : 1u), *bytes;
        if (!copy) abort();
        memcpy(copy, input, cut);
        uint32_t size;
        const bool ok = tide_skinned_data_decode(copy, cut, true, NULL, NULL, &bytes, &size, why, note);
        TIDE_CHECK(!ok && !bytes && !size);
        free(copy);
    }
    const uint8_t spoiled[] = {0, 0x7f, 0xff};
    uint8_t *copy = malloc(input_size);
    if (!copy) abort();
    for (size_t at = 0; at < input_size; at++) {
        for (uint32_t k = 0; k < sizeof spoiled; k++) {
            memcpy(copy, input, input_size);
            copy[at] = spoiled[k];
            uint8_t *bytes;
            uint32_t size;
            if (tide_skinned_data_decode(copy, input_size, true, NULL, NULL, &bytes, &size, why, note)) {
                TIDE_CHECK(tide_skinned_data_validate(bytes, size));
                tide_skinned_data_free(bytes);
            } else TIDE_CHECK(!bytes && !size);
        }
    }
    free(copy);
    free(input);
}

TIDE_TEST(skinned_blob_cut_and_spoiled_data_is_checked)
{
    model_fixture f;
    make_fixture(&f, false);
    size_t input_size;
    uint8_t *input = fixture_glb(&f, &input_size), *bytes;
    uint32_t size;
    TIDE_REQUIRE(fixture_decode(input, input_size, true, NULL, &bytes, &size));
    free(input);
    for (uint32_t cut = 0; cut < size; cut++) {
        uint8_t *copy = malloc(cut ? cut : 1u);
        if (!copy) abort();
        memcpy(copy, bytes, cut);
        TIDE_CHECK(!tide_skinned_data_validate(copy, cut));
        free(copy);
    }
    uint8_t *copy = malloc(size);
    if (!copy) abort();
    for (uint32_t at = 0; at < size; at++) {
        memcpy(copy, bytes, size);
        copy[at] ^= 0xffu;
        (void)tide_skinned_data_validate(copy, size);
    }
    free(copy);
    tide_skinned_data_free(bytes);
}
