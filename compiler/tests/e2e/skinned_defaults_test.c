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
            if (v[i].textLength != length || v[i].glow != (float)length) return false;
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
            TIDE_CHECK(v[j].textLength == length && v[j].glow == (float)length);
        }
    }
    TIDE_CHECK(instances == 5u);
}

// Change only an ordinary text dependency; canonical skinned bytes and keys
// still pass the generated content preflight, and geometry bytes stay untouched.
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

TIDE_TEST(skinned_defaults_refresh_final_vertex_versions_and_draw_copies)
{
    uint8_t *changed = pack_with_rules("updated"), *same = pack_with_rules("another");
    TIDE_REQUIRE(changed && same);
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    const uint64_t original = check_defaults(26);
    tide_world_init(&world, 1.0f);
    tide_local_init(&local);
    frame(26);
    const uint32_t meshes = draw.mesh_count;
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    TIDE_CHECK(check_defaults(26) == original);
    frame(26);
    TIDE_CHECK(draw.mesh_count == meshes);
    TIDE_REQUIRE(tide_assets_load(changed, tide_game_pack_size));
    const uint64_t other = check_defaults(7);
    TIDE_CHECK(other != original);
    frame(7);
    const uint32_t changed_meshes = draw.mesh_count;
    TIDE_REQUIRE(tide_assets_load(changed, tide_game_pack_size));
    TIDE_CHECK(check_defaults(7) == other);
    frame(7);
    TIDE_CHECK(draw.mesh_count == changed_meshes);
    // Other dependency bytes that produce the same defaults retain the same version.
    TIDE_REQUIRE(tide_assets_load(same, tide_game_pack_size));
    TIDE_CHECK(check_defaults(7) == other);
    frame(7);
    TIDE_CHECK(draw.mesh_count == changed_meshes);
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    TIDE_CHECK(check_defaults(26) == original);
    frame(26);
    free(changed);
    free(same);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_draw_free(&draw);
    tide_pack_unload();
}
