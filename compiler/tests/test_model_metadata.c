#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "folders.h"
#include "model_metadata.h"
#include "sha256.h"
#include "tide/time.h"
#include "tide_test.h"

#ifndef MODEL_METADATA_WORK
#define MODEL_METADATA_WORK "."
#endif

static const char model[] = "{\"asset\":{\"version\":\"2.0\"},"
    "\"animations\":[{\"name\":\"Idle\",\"channels\":[]},{},{\"name\":\"Idle\"}],"
    "\"nodes\":[{\"name\":\"hand\"},{\"name\":\"rig\",\"children\":[2,0]},{\"name\":\"arm\"},{}],"
    "\"skins\":[{\"name\":\"body\",\"skeleton\":1,\"joints\":[0,2]}],"
    "\"buffers\":[{\"uri\":\"never-open-this.bin\"}],\"accessors\":[{\"count\":123456789}],"
    "\"extras\":{\"unused\":[true,false,null,-1.5e+20,{\"x\":\"\\u0000\"}]}}";

static bool read_text(const char *text, model_metadata *out, char why[256])
{
    return model_metadata_read((const uint8_t *)text, strlen(text), "hero.gltf", out, why);
}

static void put32(uint8_t *out, const uint32_t value)
{
    for (int i = 0; i < 4; i++) out[i] = (uint8_t)(value >> (8 * i));
}

static uint8_t *glb(const char *json, const uint32_t bin_size, size_t *size)
{
    const uint32_t json_size = ((uint32_t)strlen(json) + 3u) & ~3u;
    *size = 28u + json_size + bin_size;
    uint8_t *bytes = calloc(*size, 1u);
    if (!bytes) abort();
    put32(bytes, 0x46546c67u);
    put32(bytes + 4, 2u);
    put32(bytes + 8, (uint32_t)*size);
    put32(bytes + 12, json_size);
    put32(bytes + 16, 0x4e4f534au);
    memset(bytes + 20, ' ', json_size);
    memcpy(bytes + 20, json, strlen(json));
    put32(bytes + 20u + json_size, bin_size);
    put32(bytes + 24u + json_size, 0x004e4942u);
    return bytes;
}

TIDE_TEST(model_metadata_reads_source_catalog_and_hierarchy_without_buffers)
{
    model_metadata m;
    char why[256];
    TIDE_REQUIRE(read_text(model, &m, why));
    TIDE_CHECK(m.animation_count == 3);
    TIDE_CHECK(strcmp(m.animations[0], "Idle") == 0);
    TIDE_CHECK(m.animations[1] == NULL);
    TIDE_CHECK(strcmp(m.animations[2], "Idle") == 0); // Duplicate names stay available to the language's alias checks.
    TIDE_CHECK(m.node_count == 4);
    TIDE_CHECK(strcmp(m.nodes[0].name, "hand") == 0 && m.nodes[0].parent == 1);
    TIDE_CHECK(m.nodes[1].parent == -1 && m.nodes[2].parent == 1);
    TIDE_CHECK(m.nodes[3].name == NULL && m.nodes[3].parent == -1);
    TIDE_CHECK(m.skin_count == 1 && m.skins[0].root == 1);
    TIDE_CHECK(m.skins[0].joint_count == 2 && m.skins[0].joints[0] == 0 && m.skins[0].joints[1] == 2);
    model_metadata_free(&m);
    TIDE_CHECK(m.animations == NULL && m.nodes == NULL && m.skins == NULL);
}

TIDE_TEST(model_metadata_reads_escaped_keys_and_utf8_names)
{
    static const char json[] = "{\"asset\":{\"version\":\"2.0\"},\"ani\\u006dations\":[{\"name\":\"caf\\u00e9\\uD83D\\uDE00\\\"\"}],"
        "\"nodes\":[{\"name\":\"\xc3\xa9\"}]}";
    model_metadata m;
    char why[256];
    TIDE_REQUIRE(read_text(json, &m, why));
    TIDE_CHECK(m.animation_count == 1 && strcmp(m.animations[0], "caf\xc3\xa9\xf0\x9f\x98\x80\"") == 0);
    TIDE_CHECK(m.node_count == 1 && strcmp(m.nodes[0].name, "\xc3\xa9") == 0);
    model_metadata_free(&m);
}

TIDE_TEST(model_metadata_refuses_invalid_json_and_hierarchy_without_partial_catalogs)
{
    static const char *const wrong[] = {
        "", "[]", "{}", "{\"asset\":{\"version\":\"1.0\"}}", "{\"asset\":{}}",
        "{\"asset\":{\"version\":\"2.0\"},}", "{\"asset\":{\"version\":\"2.0\"}} junk",
        "{\"asset\":{\"version\":\"2.0\"},\"animations\":[{\"name\":1}]}",
        "{\"asset\":{\"version\":\"2.0\"},\"animations\":[{\"name\":\"a\",\"name\":\"b\"}]}",
        "{\"asset\":{\"version\":\"2.0\"},\"animations\":[{\"name\":\"\\uD800\"}]}",
        "{\"asset\":{\"version\":\"2.0\"},\"animations\":[{\"name\":\"\\uDC00\"}]}",
        "{\"asset\":{\"version\":\"2.0\"},\"animations\":[{\"name\":\"a\\u0000b\"}]}",
        "{\"asset\":{\"version\":\"2.0\"},\"animations\":[{\"name\":\"\xc0\xaf\"}]}",
        "{\"asset\":{\"version\":\"2.0\"},\"nodes\":[{\"children\":[1]}]}",
        "{\"asset\":{\"version\":\"2.0\"},\"nodes\":[{\"children\":[0]}]}",
        "{\"asset\":{\"version\":\"2.0\"},\"nodes\":[{\"children\":[1]},{\"children\":[0]}]}",
        "{\"asset\":{\"version\":\"2.0\"},\"nodes\":[{\"children\":[1,1]},{}]}",
        "{\"asset\":{\"version\":\"2.0\"},\"nodes\":[{\"children\":[2]},{\"children\":[2]},{}]}",
        "{\"asset\":{\"version\":\"2.0\"},\"nodes\":[{\"children\":[-1]}]}",
        "{\"asset\":{\"version\":\"2.0\"},\"nodes\":[{\"children\":[1.0]}]}",
        "{\"asset\":{\"version\":\"2.0\"},\"nodes\":[{\"children\":[2147483648]}]}",
        "{\"asset\":{\"version\":\"2.0\"},\"skins\":[{}]}",
        "{\"asset\":{\"version\":\"2.0\"},\"skins\":[{\"joints\":[]}]}",
        "{\"asset\":{\"version\":\"2.0\"},\"nodes\":[{}],\"skins\":[{\"joints\":[1]}]}",
        "{\"asset\":{\"version\":\"2.0\"},\"nodes\":[{}],\"skins\":[{\"joints\":[0,0]}]}",
        "{\"asset\":{\"version\":\"2.0\"},\"nodes\":[{}],\"skins\":[{\"joints\":[0],\"skeleton\":1}]}",
        "{\"asset\":{\"version\":\"2.0\"},\"animations\":[],\"animations\":[]}",
        "{\"asset\":{\"version\":\"2.0\"},\"x\":01}",
        "{\"asset\":{\"version\":\"2.0\"},\"x\":.1}",
        "{\"asset\":{\"version\":\"2.0\"},\"x\":1.}",
    };
    for (size_t i = 0; i < sizeof wrong / sizeof wrong[0]; i++) {
        model_metadata m;
        char why[256];
        const bool ok = read_text(wrong[i], &m, why);
        if (ok) printf("    unexpectedly accepted %s\n", wrong[i]);
        TIDE_CHECK(!ok && why[0]);
        TIDE_CHECK(m.animation_count == 0 && m.node_count == 0 && m.skin_count == 0);
        model_metadata_free(&m);
    }
}

static void spoil(const uint8_t *data, const size_t size)
{
    for (size_t cut = 0; cut < size; cut++) {
        uint8_t *bytes = malloc(cut ? cut : 1u);
        if (!bytes) abort();
        memcpy(bytes, data, cut);
        model_metadata m;
        char why[256];
        model_metadata_read(bytes, cut, "model", &m, why);
        model_metadata_free(&m);
        free(bytes);
    }
    for (size_t at = 0; at < size; at++) {
        for (int pattern = 0; pattern < 3; pattern++) {
            uint8_t *bytes = malloc(size);
            if (!bytes) abort();
            memcpy(bytes, data, size);
            bytes[at] = pattern == 0 ? 0 : pattern == 1 ? 0xff : bytes[at] ^ 0x80u;
            model_metadata m;
            char why[256];
            model_metadata_read(bytes, size, "model", &m, why);
            model_metadata_free(&m);
            free(bytes);
        }
    }
}

TIDE_TEST(model_metadata_reads_glb_and_checks_every_cut_and_spoiled_byte)
{
    size_t size;
    uint8_t *bytes = glb(model, 64, &size);
    memset(bytes + size - 64u, 0xff, 64u); // Binary geometry can be arbitrary; metadata never inspects it.
    model_metadata m;
    char why[256];
    TIDE_REQUIRE(model_metadata_read(bytes, size, "hero.glb", &m, why));
    TIDE_CHECK(m.animation_count == 3 && m.node_count == 4);
    model_metadata_free(&m);
    spoil((const uint8_t *)model, strlen(model));
    spoil(bytes, size);
    free(bytes);
}

static bool write_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    const bool ok = fwrite(text, 1, strlen(text), f) == strlen(text);
    return fclose(f) == 0 && ok;
}

TIDE_TEST(model_metadata_cache_distinguishes_absence_invalidity_and_refresh)
{
    model_metadata_forget();
    TIDE_REQUIRE(folder_make(MODEL_METADATA_WORK));
    const char *path = MODEL_METADATA_WORK "/catalog.gltf";
    remove(path);
    const model_metadata *m;
    const char *why;
    TIDE_CHECK(model_metadata_cached(path, &m, &why) == MODEL_METADATA_UNAVAILABLE && m == NULL && why[0]);
    TIDE_REQUIRE(write_text(path, "{"));
    TIDE_CHECK(model_metadata_cached(path, &m, &why) == MODEL_METADATA_INVALID && m == NULL && why[0]);
    TIDE_REQUIRE(write_text(path, model));
    TIDE_REQUIRE(model_metadata_cached(path, &m, &why) == MODEL_METADATA_READY && m);
    TIDE_CHECK(strcmp(m->animations[0], "Idle") == 0);
    const model_metadata_stats before = model_metadata_statistics();
    TIDE_CHECK(model_metadata_cached(path, &m, &why) == MODEL_METADATA_READY);
    const model_metadata_stats warm = model_metadata_statistics();
    TIDE_CHECK(warm.file_reads == before.file_reads && warm.bytes_read == before.bytes_read && warm.parses == before.parses);
    TIDE_CHECK(warm.cache_hits == before.cache_hits + 1);
    model_metadata_invalidate(path); // A watcher must force inspection even when size and time stayed identical.
    TIDE_CHECK(model_metadata_cached(path, &m, &why) == MODEL_METADATA_READY);
    TIDE_CHECK(model_metadata_statistics().parses == warm.parses + 1);
    TIDE_REQUIRE(remove(path) == 0);
    TIDE_CHECK(model_metadata_cached(path, &m, &why) == MODEL_METADATA_UNAVAILABLE && m == NULL);
    model_metadata_forget();
}

TIDE_TEST(model_metadata_cache_skips_a_large_glb_binary_chunk)
{
    model_metadata_forget();
    TIDE_REQUIRE(folder_make(MODEL_METADATA_WORK));
    const char *path = MODEL_METADATA_WORK "/large.glb";
    const uint32_t binary = 128u * 1024u * 1024u;
    const uint32_t json_size = ((uint32_t)strlen(model) + 3u) & ~3u;
    uint8_t header[20], chunk[8];
    put32(header, 0x46546c67u);
    put32(header + 4, 2u);
    put32(header + 8, 28u + json_size + binary);
    put32(header + 12, json_size);
    put32(header + 16, 0x4e4f534au);
    put32(chunk, binary);
    put32(chunk + 4, 0x004e4942u);
    FILE *f = fopen(path, "wb");
    TIDE_REQUIRE(f);
    TIDE_CHECK(fwrite(header, 1, sizeof header, f) == sizeof header);
    TIDE_CHECK(fwrite(model, 1, strlen(model), f) == strlen(model));
    for (size_t i = strlen(model); i < json_size; i++) TIDE_CHECK(fputc(' ', f) != EOF);
    TIDE_CHECK(fwrite(chunk, 1, sizeof chunk, f) == sizeof chunk);
    TIDE_CHECK(fseek(f, binary - 1, SEEK_CUR) == 0 && fputc(0xff, f) != EOF);
    TIDE_REQUIRE(fclose(f) == 0);
    const model_metadata *m;
    const char *why;
    const uint64_t start = tide_time_now_ns();
    TIDE_REQUIRE(model_metadata_cached(path, &m, &why) == MODEL_METADATA_READY && m);
    const uint64_t cold = tide_time_now_ns() - start;
    TIDE_CHECK(model_metadata_statistics().bytes_read == 20u + json_size);
    TIDE_CHECK(model_metadata_statistics().parses == 1);
    const uint64_t warm_start = tide_time_now_ns();
    for (int i = 0; i < 1000; i++) TIDE_CHECK(model_metadata_cached(path, &m, &why) == MODEL_METADATA_READY);
    const uint64_t warm = tide_time_now_ns() - warm_start;
    TIDE_CHECK(model_metadata_statistics().bytes_read == 20u + json_size && model_metadata_statistics().parses == 1);
    printf("    128 MiB GLB: cold %.3f ms, warm %.3f us per lookup, %llu JSON bytes read\n", (double)cold / 1e6,
           (double)warm / 1e6, (unsigned long long)model_metadata_statistics().bytes_read);
    TIDE_CHECK(remove(path) == 0);
    model_metadata_forget();
}

TIDE_TEST(model_metadata_url_inspection_uses_only_an_existing_local_cache)
{
    model_metadata_forget();
    const char *folder = MODEL_METADATA_WORK "/url-game";
    const char *url = "https://must-never-be-contacted.invalid/hero.glb?revision=1";
    const model_metadata *m;
    const char *why;
    TIDE_CHECK(model_metadata_cached_url(url, folder, &m, &why) == MODEL_METADATA_UNAVAILABLE && m == NULL);
    TIDE_CHECK(model_metadata_statistics().file_reads == 0);
    const char *dir = MODEL_METADATA_WORK "/url-game/.tide/assets/urls";
    TIDE_REQUIRE(folder_make(dir));
    char digest[65], path[1024];
    sha256_hex(url, strlen(url), digest);
    snprintf(path, sizeof path, "%s/%s", dir, digest);
    TIDE_REQUIRE(write_text(path, model));
    TIDE_REQUIRE(model_metadata_cached_url(url, folder, &m, &why) == MODEL_METADATA_READY && m);
    TIDE_CHECK(m->animation_count == 3);
    TIDE_CHECK(model_metadata_statistics().file_reads == 1);
    TIDE_CHECK(remove(path) == 0);
    model_metadata_forget();
}
