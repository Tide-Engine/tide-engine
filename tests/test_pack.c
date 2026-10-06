// The pack (tide/pack.h): built, loaded, and read back through the code that
// reads a world's text, lists, maps and grids, with nothing of its own.

#include <stdlib.h>
#include <string.h>

#include "tide/draw.h"
#include "tide/mesh.h"
#include "tide/pack.h"
#include "tide/sha256.h"
#include "tide_test.h"

// Grid2<Color>, as tidec shapes it: cells of 16 bytes, chunks of 32 by 32.
static const tide_grid_shape color_shape = {16u, 2u, {5u, 5u, 0u}};

// A map from text, as generated code describes one for tide_map_find.
static uint32_t hash_key(const void *key)
{
    const tide_str s = tide_text_view(*(const tide_text *)key);
    return tide_map_hash_bytes(s.ptr, (uint32_t)s.bytes);
}

static uint32_t hash_probe(const void *probe)
{
    const tide_str *s = probe;
    return tide_map_hash_bytes(s->ptr, (uint32_t)s->bytes);
}

static bool equal_key(const void *key, const void *probe)
{
    return tide_str_eq(tide_text_view(*(const tide_text *)key), *(const tide_str *)probe);
}

static const tide_map_kind text_grid_kind = {12u, 4u, hash_key, hash_probe, equal_key};

static void hash_of(const char *text, uint8_t out[32])
{
    tide_sha256_of(text, strlen(text), out);
}

// A picture 70 by 40 with a color at (x, y) from its place, clear black in
// its bottom right chunk, so that chunk is left out.
static tide_color pixel_at(const int x, const int y)
{
    if (x >= 64 && y >= 32) return (tide_color){0, 0, 0, 0};
    return (tide_color){(float)x / 70.0f, (float)y / 40.0f, 0.25f, 1.0f};
}

static uint8_t *build(size_t *size)
{
    tide_pack_builder b = {0};
    uint8_t h[32];
    uint32_t chars;
    hash_of("rules", h);
    const uint32_t rules = tide_pack_bytes(&b, h, "héllo wörld", 13, &chars);
    TIDE_CHECK(chars == 11);
    uint32_t again_chars;
    TIDE_CHECK(tide_pack_bytes(&b, h, "héllo wörld", 13, &again_chars) == rules); // The same content once
    tide_pack_name(&b, "Game.rules", TIDE_PACK_TEXT, rules, 13, chars);
    tide_pack_name(&b, "Game.rulesBytes", TIDE_PACK_BYTES, rules, 13, chars);

    static tide_color pixels[70 * 40];
    for (int y = 0; y < 40; y++) {
        for (int x = 0; x < 70; x++) pixels[y * 70 + x] = pixel_at(x, y);
    }
    hash_of("hero.png", h);
    const uint32_t hero = tide_pack_grid(&b, h, 70, 40, pixels, &color_shape);
    TIDE_CHECK(tide_pack_grid(&b, h, 70, 40, pixels, &color_shape) == hero);
    tide_pack_name(&b, "Game.hero", TIDE_PACK_GRID, hero, 0, 0);

    uint8_t k1[32], k2[32];
    hash_of("a.txt", k1);
    hash_of("b.txt", k2);
    const uint32_t a = tide_pack_bytes(&b, k1, "alpha", 5, &chars);
    const uint32_t bb = tide_pack_bytes(&b, k2, "beta", 4, &chars);
    const uint32_t list = tide_pack_list(&b, (const uint32_t[]){a, bb, rules}, 3);
    tide_pack_name(&b, "Game.texts", TIDE_PACK_LIST, list, 3, TIDE_PACK_TEXT);

    uint8_t n1[32], n2[32];
    hash_of("run/0.png", n1);
    hash_of("run/1.png", n2);
    const uint32_t key0 = tide_pack_bytes(&b, n1, "run/0.png", 9, &chars);
    const uint32_t key1 = tide_pack_bytes(&b, n2, "run/1.png", 9, &chars);
    uint32_t index;
    const uint32_t map = tide_pack_map(&b, (const uint32_t[]){key0, key1}, (const uint32_t[]){hero, hero}, 2, &index);
    tide_pack_name(&b, "Game.run", TIDE_PACK_MAP, map, index, 2);
    return tide_pack_end(&b, size);
}

TIDE_TEST(pack_values_read_as_a_worlds_do)
{
    size_t size;
    uint8_t *bytes = build(&size);
    TIDE_REQUIRE(tide_pack_load(bytes, size));
    TIDE_CHECK(tide_pack_region_slots == 1u);

    // Text: a tide_str into the region, with its characters counted
    const tide_pack_entry *rules = tide_pack_find("Game.rules", TIDE_PACK_TEXT);
    TIDE_REQUIRE(rules != NULL);
    const tide_str s = tide_pack_text(rules);
    TIDE_CHECK(s.bytes == 13 && s.chars == 11 && memcmp(s.ptr, "héllo wörld", 13) == 0 && s.ptr[13] == '\0');
    TIDE_CHECK(tide_pack_find("Game.rules", TIDE_PACK_GRID) == NULL); // Another kind: a stale pack
    TIDE_CHECK(tide_pack_find("Game.nothing", TIDE_PACK_TEXT) == NULL);

    // Bytes: a list in the scratch area's slots, read as any list
    const tide_list bytes_list = {tide_pack_handle(tide_pack_find("Game.rulesBytes", TIDE_PACK_BYTES))};
    TIDE_CHECK(tide_pack_holds(bytes_list.at));
    TIDE_CHECK(tide_list_count(bytes_list) == 13);
    TIDE_CHECK(*(const uint8_t *)tide_list_at(bytes_list, 1, 1) == 0xC3u);
    TIDE_CHECK(tide_list_at(bytes_list, 13, 1) == NULL);
    tide_list_cache cache = {0};
    TIDE_CHECK(*(const uint8_t *)tide_list_cached_at(&cache, bytes_list, 0, 1) == 'h');

    // A grid: its cells through peeks and caches, its size, its chunks left out
    const tide_grid hero = {tide_pack_handle(tide_pack_find("Game.hero", TIDE_PACK_GRID))};
    const tide_grid_dir *dir = tide_grid_dir_of(hero);
    TIDE_REQUIRE(dir != NULL);
    TIDE_CHECK(dir->size[0] == 70 && dir->size[1] == 40 && dir->records == 5u && dir->flat);
    tide_grid_cache gc = {0};
    bool same = true;
    for (int y = 0; y < 40; y++) {
        for (int x = 0; x < 70; x++) {
            const tide_color *c = tide_grid_cached_peek(&gc, hero, x, y, 0, &color_shape);
            const tide_color want = pixel_at(x, y);
            const tide_color got = c ? *c : (tide_color){0, 0, 0, 0};
            same = same && memcmp(&got, &want, sizeof want) == 0;
            const tide_color *p = tide_grid_peek(hero, x, y, 0, &color_shape);
            same = same && (p ? memcmp(p, &want, sizeof want) == 0 : want.a == 0.0f);
        }
    }
    TIDE_CHECK(same);
    TIDE_CHECK(tide_grid_peek(hero, 70, 0, 0, &color_shape) == NULL && tide_grid_peek(hero, -1, 0, 0, &color_shape) == NULL);
    TIDE_CHECK(tide_grid_cells_about(hero, &color_shape) == 5u * 1024u);
    tide_grid_rows rows;
    tide_grid_rows_begin(&rows, hero, &color_shape);
    int64_t x0, x1;
    int32_t ry, rz, count = 0;
    while (tide_grid_rows_next(&rows, &x0, &x1, &ry, &rz)) count += (int32_t)(x1 - x0);
    TIDE_CHECK(count == 70 * 40);

    // ...and as a texture, with a version that's the file's, so the draw list copies it once
    static tide_draw_list draw;
    tide_draw_reset(&draw);
    const uint32_t slot = tide_draw_grid_texture(&draw, hero, &color_shape);
    TIDE_CHECK(slot != 0 && draw.textures[slot - 1].width == 70 && draw.textures[slot - 1].height == 40);
    uint8_t want_hash[32];
    hash_of("hero.png", want_hash);
    uint64_t want_version;
    memcpy(&want_version, want_hash, sizeof want_version);
    TIDE_CHECK(draw.textures[slot - 1].version == want_version && tide_pack_grid_version(hero) == want_version);
    TIDE_CHECK(draw.textures[slot - 1].pixels[(10 * 70 + 35) * 4 + 2] == 64); // 0.25 as a byte
    TIDE_CHECK(tide_draw_grid_texture(&draw, hero, &color_shape) == slot);

    // A list of texts: handles into the region
    const tide_list texts = {tide_pack_handle(tide_pack_find("Game.texts", TIDE_PACK_LIST))};
    TIDE_CHECK(tide_list_count(texts) == 3);
    const tide_text *second = tide_list_at(texts, 1, sizeof(tide_text));
    TIDE_CHECK(second && tide_str_eq(tide_text_view(*second), tide_str_from_cstr("beta")));
    const tide_text *third = tide_list_at(texts, 2, sizeof(tide_text));
    TIDE_CHECK(third && tide_text_view(*third).bytes == 13);

    // A map from paths to grids: found as the game's lookups find keys
    const tide_map run = tide_pack_map_of(tide_pack_find("Game.run", TIDE_PACK_MAP));
    TIDE_CHECK(tide_map_count(run) == 2 && tide_map_entries(run) == 2);
    const tide_str probe = tide_str_from_cstr("run/1.png");
    const int32_t found = tide_map_find(run, &text_grid_kind, &probe);
    TIDE_CHECK(found == 1);
    const tide_str missing = tide_str_from_cstr("run/2.png");
    TIDE_CHECK(tide_map_find(run, &text_grid_kind, &missing) == -1);
    const uint8_t *entry = tide_map_entry(run, 1, &text_grid_kind);
    TIDE_REQUIRE(entry != NULL);
    tide_grid value;
    memcpy(&value, entry + 8, sizeof value);
    TIDE_CHECK(value.at == hero.at);

    // The scratch area still works beside it
    const uint32_t mark = tide_scratch_mark();
    const tide_str made = tide_str_add(TIDE_STR_EMPTY, s);
    TIDE_CHECK(tide_str_eq(made, s));
    tide_scratch_reset(mark);

    tide_pack_unload();
    TIDE_CHECK(tide_pack_find("Game.rules", TIDE_PACK_TEXT) == NULL && tide_pack_region_slots == 0u);
    free(bytes);
}

TIDE_TEST(pack_refuses_what_isnt_one)
{
    TIDE_CHECK(!tide_pack_load((const uint8_t *)"TIDEPACK", 8));
    TIDE_CHECK(!tide_pack_load((const uint8_t *)"not a pack at all, not a pack at all", 36));
    size_t size;
    uint8_t *bytes = build(&size);
    TIDE_CHECK(!tide_pack_load(bytes, size - 1)); // Its region cut short
    bytes[9] ^= 1; // Another version
    TIDE_CHECK(!tide_pack_load(bytes, size));
    free(bytes);
    TIDE_CHECK(tide_pack_find("Game.rules", TIDE_PACK_TEXT) == NULL);
}
