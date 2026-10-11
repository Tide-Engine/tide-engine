#include "asset_files.h"
#include "asset_usage.h"
#include "codegen.h"
#include "folders.h"
#include "tide/pack.h"
#include "tide_test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void capture(void *user, const diag_severity severity, const loc at, const char *message)
{
    (void)at;
    if (severity == DIAG_ERROR) {
        *(bool *)user = true;
        fprintf(stderr, "%s\n", message);
    }
}

static program *checked(const char *text)
{
    diag_reset();
    lex_symbols_reset();
    bool errors = false;
    diag_set_sink(capture, &errors);
    source *src = NEW(source);
    *src = (source){.path = ASSET_USAGE_ROOT "/compiler/tests/e2e/asset_usage.tide", .text = text, .len = strlen(text)};
    diag_add_source(src);
    program *p = program_new();
    token *tokens = lex(src);
    const bool ok = tokens && parse_file(p, src, tokens, false) && check(p) && !errors;
    diag_set_sink(NULL, NULL);
    return ok ? p : NULL;
}

static void done(void)
{
    asset_files_forget();
    diag_reset();
    arena_reset();
    lex_symbols_reset();
}

static bool has_asset(const program *p, const char *name)
{
    for (int i = 0; i < p->assets.count; i++) if (str_eq_c(p->assets.items[i]->name, name)) return true;
    return false;
}

static void *read_bytes(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return NULL; }
    const long length = ftell(f);
    if (length < 0 || fseek(f, 0, SEEK_SET)) { fclose(f); return NULL; }
    char *bytes = calloc((size_t)length + 1, 1);
    if (!bytes) { fclose(f); return NULL; }
    *size = fread(bytes, 1, (size_t)length, f);
    fclose(f);
    if (*size != (size_t)length) { free(bytes); return NULL; }
    return bytes;
}

static void free_info(game_info *info)
{
    for (int i = 0; i < info->asset_count; i++) free(info->asset_paths[i]);
    free(info->asset_paths);
}

TIDE_TEST(asset_usage_retains_checked_defaults_helpers_operators_shaders_and_collections)
{
    static const char text[] =
        "asset string unused = \"assets/rules.txt\";\n"
        "asset string direct = \"assets/rules.txt\";\n"
        "asset string helper = \"assets/rules.txt\";\n"
        "asset string nested = \"assets/rules.txt\";\n"
        "asset string operated = \"assets/rules.txt\";\n"
        "asset string vertexDefault = \"assets/rules.txt\";\n"
        "asset Texture sampled = \"assets/hero.png\";\n"
        "asset List<byte> gpuBytes = \"assets/table.bin\";\n"
        "asset List<string> list = \"assets/notes/*.txt\";\n"
        "asset Map<string, string> map = \"assets/notes/*.txt\";\n"
        "struct Nested { string text = nested; }\n"
        "const Nested NESTED = default;\n"
        "int Uncalled(int value = default) { return helper.length + value; }\n"
        "struct Count { int value; int operator +(Count a, int b) { return operated.length + a.value + b; } }\n"
        "struct Corner { float3 position; int extra = vertexDefault.length; }\n"
        "asset Mesh<Corner> mesh = \"assets/meshes/quad.glb\";\n"
        "asset SkinnedMesh<Corner> skin = \"skinned/models/a.gltf\";\n"
        "asset AnimationCatalog unusedClips = \"animations/clips/a.gltf\";\n"
        "singleton State { int count = direct.length; string text = NESTED.text; }\n"
        "system Tick(mut State state) { state.count = list.count + map.count + mesh.vertices.count + skin.vertices.count + (Count { } + 1); }\n"
        "Color SampleAsset(float2 uv) { return sampled.Sample(uv); }\n"
        "frag Shade(Vertex vertex, out Color color) { color = SampleAsset(vertex.uv) * float(gpuBytes.count); }\n"
        "view Show() { Draw.Mesh([Vertex { }, Vertex { }, Vertex { }], [0, 1, 2], Shade); }\n"
        "scene Main { }\n";
    program *p = checked(text);
    TIDE_REQUIRE(p);
    TIDE_CHECK(p->assets.count == 13);
    program_strip_unused_assets(p);
    TIDE_CHECK(p->assets.count == 11);
    TIDE_CHECK(!has_asset(p, "unused") && !has_asset(p, "unusedClips"));
    static const char *const names[] = {"direct", "helper", "nested", "operated", "vertexDefault", "sampled",
        "gpuBytes", "list", "map", "mesh", "skin"};
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) TIDE_CHECK(has_asset(p, names[i]));
    // Compaction must not change collection/declaration ordering or mutate
    // name-binding declarations used by checked expressions.
    TIDE_CHECK(str_eq_c(p->assets.items[0]->name, "direct"));
    TIDE_CHECK(str_eq_c(p->assets.items[p->assets.count - 1]->name, "skin"));
    TIDE_CHECK(p->decls.count > p->assets.count);
    program_strip_unused_assets(p);
    TIDE_CHECK(p->assets.count == 11);
    done();
}

TIDE_TEST(asset_usage_codegen_opt_in_prunes_pack_globals_watch_paths_and_model_identity)
{
    static const char text[] =
        "asset string kept = \"assets/rules.txt\";\n"
        "asset List<byte> unused = \"assets/table.bin\";\n"
        "asset AnimationCatalog unusedClips = \"animations/clips/a.gltf\";\n"
        "singleton State { int count = kept.length; }\nscene Main { }\n";
    TIDE_REQUIRE(folder_make(ASSET_USAGE_WORK));
    for (int strip = 0; strip < 2; strip++) {
        program *p = checked(text);
        TIDE_REQUIRE(p);
        game_info info = {0};
        const codegen_options options = {.name = "usage", .out_dir = ASSET_USAGE_WORK,
            .pack = ASSET_USAGE_WORK "/usage.pack", .strip_unused_assets = strip != 0, .info = &info};
        TIDE_REQUIRE(codegen(p, &options));
        size_t size;
        tide_pack_header *pack = read_bytes(options.pack, &size);
        TIDE_REQUIRE(pack && size >= sizeof *pack);
        TIDE_CHECK(pack->entries == (strip ? 1u : 3u));
        free(pack);
        char *c = read_bytes(ASSET_USAGE_WORK "/usage.c", &size);
        TIDE_REQUIRE(c);
        TIDE_CHECK(strstr(c, "tide_asset_kept") != NULL);
        TIDE_CHECK((strstr(c, "tide_asset_unused;") != NULL) == !strip);
        TIDE_CHECK((strstr(c, "tide_asset_unusedClips") != NULL) == !strip);
        free(c);
        char *h = read_bytes(ASSET_USAGE_WORK "/usage.h", &size);
        TIDE_REQUIRE(h);
        TIDE_CHECK((strstr(h, "TIDE_HAS_ASSETS") != NULL));
        TIDE_CHECK((strstr(h, "unusedClips") != NULL) == !strip);
        free(h);
        TIDE_CHECK(info.assets_affect_match == !strip);
        TIDE_CHECK(info.asset_count == (strip ? 1 : 3));
        for (int i = 0; strip && i < info.asset_count; i++) TIDE_CHECK(strstr(info.asset_paths[i], "rules.txt") != NULL);
        free_info(&info);
        done();
    }
}

TIDE_TEST(asset_usage_pruning_does_not_skip_unused_asset_validation)
{
    program *p = checked("asset Texture unused = \"assets/bad.png\";\nscene Main { }\n");
    TIDE_CHECK(!p);
    TIDE_CHECK(diag_error_count() > 0);
    done();
}

TIDE_TEST(asset_usage_runtime_decode_and_implicit_default_helpers_retain_assets)
{
    static const char text[] =
        "asset string dependency = \"assets/rules.txt\";\n"
        "asset List<byte> modelBytes = \"skinned/models/a.gltf\";\n"
        "asset string unused = \"assets/rules.txt\";\n"
        "struct Value { int count = dependency.length; }\n"
        "int Read(Value value = default) { return value.count; }\n"
        "struct Measure { int extra; int operator +(Measure value, int amount) { return Read() + value.extra + amount; } }\n"
        "struct Corner { float3 position; int extra = Measure { } + 1; }\n"
        "singleton Saved { SkinnedMesh<Corner> model; }\n"
        "system Tick(mut Saved saved) { saved.model = SkinnedMesh<Corner>.Decode(modelBytes)!; }\n"
        "int Missing() { Map<int, Value> values = default; mut Value result = Value { count = 0 }; values.TryGet(0, out result); return result.count; }\n"
        "scene Main { }\n";
    program *p = checked(text);
    TIDE_REQUIRE(p);
    const codegen_options options = {.name = "decode", .out_dir = ASSET_USAGE_WORK,
        .pack = ASSET_USAGE_WORK "/decode.pack", .strip_unused_assets = true};
    TIDE_REQUIRE(folder_make(ASSET_USAGE_WORK));
    TIDE_REQUIRE(codegen(p, &options));
    TIDE_CHECK(p->assets.count == 2);
    TIDE_CHECK(has_asset(p, "dependency") && has_asset(p, "modelBytes") && !has_asset(p, "unused"));
    size_t size;
    char *c = read_bytes(ASSET_USAGE_WORK "/decode.c", &size);
    TIDE_REQUIRE(c);
    TIDE_CHECK(strstr(c, "tide_asset_dependency") != NULL);
    TIDE_CHECK(strstr(c, "tide_asset_modelBytes") != NULL);
    free(c);
    done();
}

TIDE_TEST(asset_usage_all_unused_removes_an_old_pack_and_asset_api)
{
    static const char text[] = "asset List<byte> unused = \"assets/table.bin\";\nscene Main { }\n";
    TIDE_REQUIRE(folder_make(ASSET_USAGE_WORK));
    for (int strip = 0; strip < 2; strip++) {
        program *p = checked(text);
        TIDE_REQUIRE(p);
        game_info info = {0};
        const codegen_options options = {.name = "empty", .out_dir = ASSET_USAGE_WORK,
            .pack = ASSET_USAGE_WORK "/empty.pack", .strip_unused_assets = strip != 0, .info = &info};
        TIDE_REQUIRE(codegen(p, &options));
        unsigned long long size, modified;
        TIDE_CHECK(folder_file_info(options.pack, &size, &modified) == !strip);
        size_t length;
        char *h = read_bytes(ASSET_USAGE_WORK "/empty.h", &length);
        TIDE_REQUIRE(h);
        TIDE_CHECK((strstr(h, "TIDE_HAS_ASSETS") != NULL) == !strip);
        TIDE_CHECK((strstr(h, "tide_assets_load") != NULL) == !strip);
        free(h);
        TIDE_CHECK(info.asset_count == (strip ? 0 : 1));
        TIDE_CHECK(!info.assets_affect_match);
        free_info(&info);
        done();
    }
}

TIDE_TEST(asset_usage_unused_animation_content_does_not_change_the_stripped_header)
{
    static const char *const sources[] = {
        "asset AnimationCatalog unused = \"animations/clips/a.gltf\";\nscene Main { }\n",
        "asset AnimationCatalog unused = \"animations/clips/b.gltf\";\nscene Main { }\n"
    };
    TIDE_REQUIRE(folder_make(ASSET_USAGE_WORK));
    char *first = NULL;
    for (int i = 0; i < 2; i++) {
        program *p = checked(sources[i]);
        TIDE_REQUIRE(p);
        const codegen_options options = {.name = "identity", .out_dir = ASSET_USAGE_WORK, .strip_unused_assets = true};
        TIDE_REQUIRE(codegen(p, &options));
        size_t size;
        char *h = read_bytes(ASSET_USAGE_WORK "/identity.h", &size);
        TIDE_REQUIRE(h);
        if (i) { TIDE_CHECK(strcmp(first, h) == 0); free(h); }
        else first = h;
        done();
    }
    free(first);
}

TIDE_TEST(asset_usage_iterative_walk_handles_shared_and_deep_expression_graphs)
{
    program *p = program_new();
    decl *used = NEW(decl), *unused = NEW(decl), *helper = NEW(decl);
    used->kind = unused->kind = DECL_ASSET;
    helper->kind = DECL_FUNCTION;
    vec_push(p->assets, unused);
    vec_push(p->assets, used);
    vec_push(p->decls, unused);
    vec_push(p->decls, used);
    vec_push(p->decls, helper);
    expr *e = NEW(expr);
    e->bind = BIND_ASSET;
    e->constant = used;
    for (int i = 0; i < 100000; i++) {
        expr *parent = NEW(expr);
        parent->lhs = e;
        parent->rhs = e;
        e = parent;
    }
    // Resolved recursive helpers and repeated operands must be visited once.
    e->method = helper;
    helper->value = e;
    program_strip_unused_assets(p);
    TIDE_CHECK(p->assets.count == 1 && p->assets.items[0] == used);
    done();
}
