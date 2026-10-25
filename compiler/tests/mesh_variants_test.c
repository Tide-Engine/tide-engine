// Typed static tangent imports must never mutate the canonical mesh used by
// another layout or the editor, regardless of cold/warm declaration order.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asset_files.h"
#include "codegen.h"
#include "folders.h"
#include "mesh_lod.h"
#include "tide/pack.h"
#include "tide_test.h"

static const char mirrored_quad[] =
    "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n"
    "vt 0 0\nvt 1 0\nvt 1 1\nvt 2 1\nvn 0 0 1\n"
    "f 1/1/1 2/2/1 3/3/1\nf 1/1/1 3/3/1 4/4/1\n";

static bool write_model(const char *name, const char *text)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", ASSET_FILES_WORK, name);
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    const size_t size = strlen(text);
    const bool ok = fwrite(text, 1, size, f) == size;
    return fclose(f) == 0 && ok;
}

static program *checked_variants(const char *file, const bool mapped_first, const bool metadata)
{
    diag_reset();
    lex_symbols_reset();
    source *src = NEW(source);
    sb text = {0}, plain = {0}, mapped = {0};
    sb_printf(&plain, "[LOD] asset Mesh<Point> plain = \"%s\";", file);
    sb_printf(&mapped, "[LOD] asset Mesh<Mapped> mapped = \"%s\";", file);
    sb_printf(&text, "struct Point { float3 position; } "
        "struct Mapped { float3 position; float3 normal; float2 uv; float4 tangent; } "
        "struct Direction { float3 position; float3 tangent; } %s %s "
        "asset Mesh<Point> plainAgain = \"%s\"; asset Mesh<Mapped> mappedAgain = \"%s\"; "
        "asset Mesh<Direction> direction = \"%s\"; scene Main {}",
        mapped_first ? mapped.data : plain.data, mapped_first ? plain.data : mapped.data, file, file, file);
    *src = (source){.path = ASSET_FILES_WORK "/variants.tide", .text = text.data, .len = text.len};
    diag_add_source(src);
    program *p = program_new();
    p->asset_metadata_only = metadata;
    p->game_folder = ASSET_FILES_WORK;
    token *tokens = lex(src);
    return tokens && parse_file(p, src, tokens, false) && check(p) && !diag_error_count() ? p : NULL;
}

static const mesh_file *mesh_named(const program *p, const char *name)
{
    for (int i = 0; i < p->assets.count; i++) {
        const decl *d = p->assets.items[i];
        if (str_eq_c(d->name, name) && d->asset && d->asset->count == 1) return &d->asset->items[0].mesh;
    }
    return NULL;
}

static void same_canonical(const mesh_file *m, const mesh_file *baseline)
{
    TIDE_REQUIRE(m && m->vertices == baseline->vertices && m->indices == baseline->indices);
    TIDE_CHECK(memcmp(m->hash, baseline->hash, sizeof m->hash) == 0);
    TIDE_CHECK(memcmp(m->index, baseline->index, m->indices * sizeof(uint32_t)) == 0);
    TIDE_CHECK(memcmp(m->position, baseline->position, m->vertices * 3u * sizeof(float)) == 0);
    TIDE_CHECK(memcmp(m->normal, baseline->normal, m->vertices * 3u * sizeof(float)) == 0);
    TIDE_CHECK(memcmp(m->uv, baseline->uv, m->vertices * 2u * sizeof(float)) == 0);
    TIDE_CHECK(memcmp(m->uv2, baseline->uv2, m->vertices * 2u * sizeof(float)) == 0);
    TIDE_CHECK(memcmp(m->color, baseline->color, m->vertices * 4u * sizeof(float)) == 0);
    TIDE_CHECK(memcmp(m->tangent, baseline->tangent, m->vertices * 4u * sizeof(float)) == 0);
}

static uint8_t *generate_pack(program *p, size_t *size)
{
    const codegen_options options = {.name = "variants", .out_dir = ASSET_FILES_WORK, .pack = ASSET_FILES_WORK "/variants.pack"};
    if (!codegen(p, &options)) return NULL;
    FILE *f = fopen(options.pack, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return NULL; }
    const long bytes = ftell(f);
    if (bytes < 0 || fseek(f, 0, SEEK_SET)) { fclose(f); return NULL; }
    uint8_t *data = malloc(bytes ? (size_t)bytes : 1u);
    *size = fread(data, 1, (size_t)bytes, f);
    fclose(f);
    if (*size != (size_t)bytes) { free(data); return NULL; }
    return data;
}

static void check_packed(const char *name, const mesh_file *m, const uint32_t stride)
{
    const tide_pack_entry *entry = tide_pack_find(name, TIDE_PACK_MESH);
    TIDE_REQUIRE(entry);
    const tide_mesh_lists lists = tide_pack_mesh_of(entry, stride);
    TIDE_REQUIRE(tide_list_count(lists.vertices) == (int32_t)m->vertices && tide_list_count(lists.indices) == (int32_t)m->indices);
    for (uint32_t i = 0; i < m->vertices; i++) {
        const float *v = tide_list_at(lists.vertices, (int32_t)i, stride);
        TIDE_REQUIRE(v);
        TIDE_CHECK(memcmp(v, m->position + i * 3u, 3u * sizeof(float)) == 0);
        if (stride == 24u) TIDE_CHECK(memcmp(v + 3u, m->tangent + i * 4u, 3u * sizeof(float)) == 0);
        if (stride == 48u) {
            TIDE_CHECK(memcmp(v + 3u, m->normal + i * 3u, 3u * sizeof(float)) == 0);
            TIDE_CHECK(memcmp(v + 6u, m->uv + i * 2u, 2u * sizeof(float)) == 0);
            TIDE_CHECK(memcmp(v + 8u, m->tangent + i * 4u, 4u * sizeof(float)) == 0);
        }
    }
    for (uint32_t i = 0; i < m->indices; i++) {
        const uint32_t *index = tide_list_at(lists.indices, (int32_t)i, sizeof(uint32_t));
        TIDE_REQUIRE(index);
        TIDE_CHECK(*index == m->index[i]);
    }
}

TIDE_TEST(asset_tangent_variants_isolate_both_consumer_orders_cold_warm_and_editor)
{
    TIDE_REQUIRE(folder_make(ASSET_FILES_WORK) && write_model("variant.obj", mirrored_quad));
    mesh_file baseline;
    char why[256], note[256];
    TIDE_REQUIRE(mesh_file_read((const uint8_t *)mirrored_quad, strlen(mirrored_quad), "variant.obj", NULL, NULL, &baseline, why, note));
    static const float positions[12] = {0, 0, 0, -1, 1, 0, -1, 0, 0, 0, 1, 0};
    static const uint32_t indices[6] = {0, 1, 2, 0, 3, 1};
    TIDE_REQUIRE(baseline.vertices == 4u && baseline.indices == 6u);
    TIDE_CHECK(memcmp(baseline.position, positions, sizeof positions) == 0);
    TIDE_CHECK(memcmp(baseline.index, indices, sizeof indices) == 0);
    for (uint32_t i = 0; i < 16u; i++) TIDE_CHECK(baseline.tangent[i] == 0.0f);
    for (uint32_t cold = 0; cold < 2u; cold++) {
        asset_files_forget();
        const float *plain_cache = NULL, *mapped_cache = NULL;
        for (uint32_t pass = 0; pass < 4u; pass++) {
            const bool metadata = pass == 2u;
            program *p = checked_variants("variant.obj", (cold + pass) % 2u != 0u, metadata);
            TIDE_REQUIRE(p);
            const mesh_file *plain = mesh_named(p, "plain"), *mapped = mesh_named(p, "mapped");
            TIDE_REQUIRE(plain && mapped);
            same_canonical(plain, &baseline);
            same_canonical(mesh_named(p, "direction"), &baseline);
            TIDE_CHECK(mesh_named(p, "plainAgain")->position == plain->position);
            TIDE_CHECK(mesh_named(p, "mappedAgain")->position == mapped->position);
            if (!pass) plain_cache = plain->position, mapped_cache = mapped->position;
            TIDE_CHECK(plain->position == plain_cache);
            if (metadata) same_canonical(mapped, &baseline);
            else {
                TIDE_CHECK(mapped->vertices == 6u && mapped->position == mapped_cache && mapped_cache != plain_cache);
                for (uint32_t i = 0; i < 6u; i++) {
                    const uint32_t at = mapped->index[i];
                    TIDE_CHECK(memcmp(mapped->position + at * 3u, positions + indices[i] * 3u, 3u * sizeof(float)) == 0);
                    TIDE_CHECK(mapped->tangent[at * 4u] == (i < 3u ? -1.0f : 1.0f));
                    TIDE_CHECK(mapped->tangent[at * 4u + 3u] == (i < 3u ? 1.0f : -1.0f));
                }
                size_t size;
                uint8_t *pack = generate_pack(p, &size);
                TIDE_REQUIRE(pack && tide_pack_load(pack, size));
                check_packed("plain", plain, 12u);
                check_packed("plainAgain", plain, 12u);
                check_packed("direction", &baseline, 24u);
                check_packed("mapped", mapped, 48u);
                check_packed("mappedAgain", mapped, 48u);
                tide_pack_unload();
                free(pack);
            }
            diag_reset(); arena_reset(); lex_symbols_reset();
        }
    }
    asset_files_forget();
    mesh_file_free(&baseline);
}

TIDE_TEST(asset_tangent_variants_keep_lod_cache_layouts_distinct_and_order_independent)
{
    sb obj = {0};
    const uint32_t side = 32u, across = side + 1u;
    for (uint32_t y = 0; y <= side; y++) for (uint32_t x = 0; x <= side; x++) {
        sb_printf(&obj, "v %u %u 0\nvt %u %u\n", (unsigned)x, (unsigned)y, (unsigned)(x <= side / 2u ? x : side - x), (unsigned)y);
    }
    sb_printf(&obj, "vn 0 0 1\n");
    for (uint32_t y = 0; y < side; y++) for (uint32_t x = 0; x < side; x++) {
        const uint32_t a = y * across + x + 1u, b = a + 1u, c = a + across, d = c + 1u;
        sb_printf(&obj, "f %u/%u/1 %u/%u/1 %u/%u/1 %u/%u/1\n", a, a, b, b, d, d, c, c);
    }
    TIDE_REQUIRE(folder_make(ASSET_FILES_WORK) && write_model("variant-lod.obj", obj.data));
    asset_files_forget();
    tide_mesh_levels saved[2] = {{0}};
    uint32_t made = 0;
    for (uint32_t order = 0; order < 2u; order++) {
        program *p = checked_variants("variant-lod.obj", order != 0u, false);
        TIDE_REQUIRE(p);
        const mesh_file *plain = mesh_named(p, "plain"), *mapped = mesh_named(p, "mapped");
        TIDE_REQUIRE(plain && mapped && plain->vertices == across * across && mapped->vertices == plain->vertices + across);
        size_t size;
        uint8_t *pack = generate_pack(p, &size);
        TIDE_REQUIRE(pack && tide_pack_load(pack, size));
        for (uint32_t kind = 0; kind < 2u; kind++) {
            const tide_pack_entry *entry = tide_pack_find(kind ? "mapped" : "plain", TIDE_PACK_MESH);
            TIDE_REQUIRE(entry);
            const tide_mesh_lists lists = tide_pack_mesh_of(entry, kind ? 48u : 12u);
            tide_mesh_levels levels;
            TIDE_REQUIRE(tide_pack_list_levels(lists.indices, &levels) && levels.count >= 1u);
            if (!order) {
                saved[kind] = levels;
                uint32_t *copy = malloc(levels.index_count * sizeof(uint32_t));
                memcpy(copy, levels.index, levels.index_count * sizeof(uint32_t));
                saved[kind].index = copy;
            } else {
                TIDE_CHECK(levels.count == saved[kind].count && levels.index_count == saved[kind].index_count);
                TIDE_CHECK(memcmp(levels.indices, saved[kind].indices, sizeof levels.indices) == 0);
                TIDE_CHECK(memcmp(levels.error, saved[kind].error, sizeof levels.error) == 0);
                TIDE_CHECK(memcmp(levels.index, saved[kind].index, levels.index_count * sizeof(uint32_t)) == 0);
            }
            if (kind) for (uint32_t i = 0; i < levels.index_count; i += 3u) {
                const float first = mapped->tangent[levels.index[i] * 4u + 3u];
                TIDE_CHECK(mapped->tangent[levels.index[i + 1u] * 4u + 3u] == first);
                TIDE_CHECK(mapped->tangent[levels.index[i + 2u] * 4u + 3u] == first);
            }
        }
        if (!order) made = mesh_lod_made();
        else TIDE_CHECK(mesh_lod_made() == made);
        tide_pack_unload(); free(pack);
        diag_reset(); arena_reset(); lex_symbols_reset();
    }
    for (uint32_t i = 0; i < 2u; i++) free((void *)saved[i].index);
    asset_files_forget();
}
