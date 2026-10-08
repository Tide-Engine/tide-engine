#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/pack.h"
#include "tide/sha256.h"
#include "tide_test.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

static bool default_is(const SkinCorner *v, const int32_t length)
{
    return v->textLength == length && v->glow == (float)length && v->fromConstant == length && v->fromNested == length
        && v->fromOperator == length + 1 && v->fromMesh == length && v->byteCount == 300 && v->noteCount == 2;
}

static bool defaults_are(const char *name, const uint32_t kind, const uint32_t models, const int32_t length)
{
    const tide_pack_entry *e = tide_pack_find(name, kind);
    if (!e) return false;
    uint32_t total = 0, k = 0;
    for (;; k++) {
        uint32_t count;
        const SkinCorner *v = tide_pack_skinned_open(e, k, sizeof(SkinCorner), &count);
        if (!v) break;
        total += count;
        for (uint32_t i = 0; i < count; i++) {
            if (!default_is(&v[i], length)) return false;
        }
    }
    return k == models && total == models * 4u;
}

static uint64_t check_defaults(const int32_t length)
{
    TIDE_CHECK(defaults_are("skinOne", TIDE_PACK_SKINNED, 1u, length));
    TIDE_CHECK(defaults_are("skinMany", TIDE_PACK_SKINNEDS, 2u, length));
    TIDE_CHECK(defaults_are("skinNamed", TIDE_PACK_SKINNED_MAP, 2u, length));
    const tide_skinned_lists model = tide_pack_skinned_of(tide_pack_find("skinOne", TIDE_PACK_SKINNED), sizeof(SkinCorner));
    uint64_t version = 0;
    TIDE_CHECK(tide_pack_list_version(model.vertices, sizeof(SkinCorner), &version) && version != 0);
    return version;
}

static void frame(const int32_t length)
{
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1.0f, NULL, &local, &draw, &gui);
    uint32_t instances = 0;
    for (uint32_t i = 0; i < draw.count; i++) {
        const tide_draw_command *c = &draw.commands[i];
        if (c->kind != TIDE_DRAW_SHADED) continue;
        const tide_draw_run *r = &draw.runs[c->shaded.run];
        const tide_draw_mesh_data *m = &draw.meshes[r->mesh - 1u];
        TIDE_CHECK(m->vertex_size == sizeof(SkinCorner) && m->vertex_count == 4u);
        instances += r->count;
        const SkinCorner *v = m->bytes;
        for (uint32_t j = 0; j < m->vertex_count; j++) {
            TIDE_CHECK(default_is(&v[j], length));
        }
    }
    TIDE_CHECK(instances == 5u);
}

// Change only an ordinary dependency; canonical model bytes and geometry stay
// untouched. The compiled defaults still require the original dependency.
static uint8_t *pack_with_rules(const char *text)
{
    uint8_t *bytes = malloc(tide_game_pack_size);
    if (!bytes) return NULL;
    memcpy(bytes, tide_game_pack, tide_game_pack_size);
    const tide_pack_header *h = (const tide_pack_header *)(const void *)bytes;
    tide_pack_entry *entries = (tide_pack_entry *)(void *)(bytes + sizeof *h);
    uint8_t named[32];
    tide_sha256_of("laterRules", strlen("laterRules"), named);
    const size_t size = strlen(text);
    for (uint32_t i = 0; i < h->entries; i++) {
        tide_pack_entry *e = &entries[i];
        if (e->kind != TIDE_PACK_TEXT || memcmp(e->name, named, sizeof named)) continue;
        if (size > e->size) break;
        tide_block *b = (tide_block *)(void *)(bytes + h->region_at + e->offset);
        b->a = b->b = (uint32_t)size;
        memcpy(b + 1, text, size + 1u);
        e->size = e->extra = (uint32_t)size;
        return bytes;
    }
    free(bytes);
    return NULL;
}

static uint8_t *pack_with_logo(void)
{
    uint8_t *bytes = malloc(tide_game_pack_size);
    if (!bytes) return NULL;
    memcpy(bytes, tide_game_pack, tide_game_pack_size);
    const tide_pack_header *h = (const tide_pack_header *)(const void *)bytes;
    const tide_pack_entry *entries = (const tide_pack_entry *)(const void *)(bytes + sizeof *h);
    uint8_t named[32];
    tide_sha256_of("logo", strlen("logo"), named);
    for (uint32_t i = 0; i < h->entries; i++) {
        const tide_pack_entry *e = &entries[i];
        if (e->kind != TIDE_PACK_TEXTURE || memcmp(e->name, named, sizeof named)) continue;
        tide_block *b = (tide_block *)(void *)(bytes + h->region_at + e->offset);
        uint8_t *pixels = (uint8_t *)(b + 1) + 2u * sizeof(int32_t);
        pixels[0] ^= 1u;
        b->a ^= 1u;
        return bytes;
    }
    free(bytes);
    return NULL;
}

// Keep the canonical model, indices, layout and dependency bytes intact, but
// change a source-filled typed attribute that match code can own and read.
static uint8_t *pack_with_source_position(void)
{
    uint8_t *bytes = malloc(tide_game_pack_size);
    if (!bytes) return NULL;
    memcpy(bytes, tide_game_pack, tide_game_pack_size);
    const tide_pack_header *h = (const tide_pack_header *)(const void *)bytes;
    const tide_pack_entry *entries = (const tide_pack_entry *)(const void *)(bytes + sizeof *h);
    uint8_t named[32];
    tide_sha256_of("skinOne", strlen("skinOne"), named);
    for (uint32_t i = 0; i < h->entries; i++) {
        const tide_pack_entry *e = &entries[i];
        if (e->kind != TIDE_PACK_SKINNED || memcmp(e->name, named, sizeof named)) continue;
        tide_skinned_lists model;
        memcpy(&model, bytes + h->region_at + e->offset + sizeof(tide_block), sizeof model);
        const uint32_t at = (model.vertices.at & 0x3fffffffu) - TIDE_PACK_BASE;
        SkinCorner *vertices = (SkinCorner *)(void *)(bytes + h->region_at + at + sizeof(tide_block));
        vertices[0].position.x += 17.0f;
        return bytes;
    }
    free(bytes);
    return NULL;
}

static void drawn_vertices_hash(uint8_t hash[32])
{
    tide_sha256 content;
    tide_sha256_init(&content);
    for (uint32_t i = 0; i < draw.mesh_count; i++) {
        const tide_draw_mesh_data *m = &draw.meshes[i];
        tide_sha256_add(&content, m->bytes, (size_t)m->vertex_count * m->vertex_size);
    }
    tide_sha256_end(&content, hash);
}

static void check_owned(void)
{
    tide_text_use(&world.heap, &local.heap);
    uint32_t count;
    const SkinCorner *loaded = tide_pack_skinned_open(tide_pack_find("skinOne", TIDE_PACK_SKINNED), 0u, sizeof *loaded, &count);
    TIDE_REQUIRE(loaded && count == 4u);
    const tide_list vertices = world.Saved.model.vertices;
    TIDE_CHECK(!tide_pack_holds(vertices.at) && tide_list_count(vertices) == 4);
    for (int32_t i = 0; i < tide_list_count(vertices); i++) {
        const SkinCorner *v = tide_list_at(vertices, i, sizeof *v);
        TIDE_CHECK(v && default_is(v, 26));
        TIDE_CHECK(v && !memcmp(&v->position, &loaded[i].position, sizeof v->position));
    }
}

TIDE_TEST(skinned_defaults_reject_changed_dependencies_and_keep_the_loaded_pack)
{
    uint8_t *changed = pack_with_rules("updated"), *same = pack_with_rules("zyxwvutsrqponmlkjihgfedcba");
    uint8_t *geometry = pack_with_source_position();
    TIDE_REQUIRE(changed && same && geometry);
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    const uint64_t original = check_defaults(26);
    tide_world_init(&world, 1.0f);
    tide_local_init(&local);
    check_owned();
    const uint64_t hash = tide_world_hash(&world);
    frame(26);
    const uint32_t meshes = draw.mesh_count;
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    TIDE_CHECK(check_defaults(26) == original);
    frame(26);
    TIDE_CHECK(draw.mesh_count == meshes);
    const tide_pack_entry *before = tide_pack_find("skinOne", TIDE_PACK_SKINNED);
    TIDE_REQUIRE(before != NULL);
    uint8_t drawn_before[32];
    drawn_vertices_hash(drawn_before);
    const uint8_t *rejected[] = {changed, changed, same, geometry};
    for (size_t i = 0; i < sizeof rejected / sizeof rejected[0]; i++) {
        TIDE_CHECK(!tide_assets_load(rejected[i], tide_game_pack_size));
        TIDE_CHECK(tide_pack_find("skinOne", TIDE_PACK_SKINNED) == before);
        TIDE_CHECK(check_defaults(26) == original && tide_world_hash(&world) == hash);
        check_owned();
        frame(26);
        TIDE_CHECK(draw.mesh_count == meshes);
        uint8_t drawn_after[32];
        drawn_vertices_hash(drawn_after);
        TIDE_CHECK(!memcmp(drawn_before, drawn_after, sizeof drawn_before));
    }
    // Ordinary visual assets outside the defaults' dependency tree still reload.
    uint8_t *visual = pack_with_logo();
    TIDE_REQUIRE(visual != NULL);
    const tide_texture_ref logo = tide_pack_texture_of(tide_pack_find("logo", TIDE_PACK_TEXTURE));
    tide_texture image;
    TIDE_REQUIRE(tide_pack_texture_find(logo.image.version, &image));
    const uint8_t red = ((const uint8_t *)image.pixels)[0];
    TIDE_REQUIRE(tide_assets_load(visual, tide_game_pack_size));
    TIDE_REQUIRE(tide_pack_texture_find(logo.image.version, &image));
    TIDE_CHECK(((const uint8_t *)image.pixels)[0] == (uint8_t)(red ^ 1u));
    TIDE_CHECK(check_defaults(26) == original && tide_world_hash(&world) == hash);
    check_owned();
    frame(26);
    TIDE_CHECK(draw.mesh_count == meshes);
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    TIDE_CHECK(check_defaults(26) == original);
    frame(26);
    free(changed);
    free(same);
    free(geometry);
    free(visual);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_draw_free(&draw);
    tide_pack_unload();
}
