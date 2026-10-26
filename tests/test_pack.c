// The pack (tide/pack.h): built, loaded, and read back through the code that
// reads a world's text, lists, maps and grids, with nothing of its own.

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/draw.h"
#include "tide/mesh.h"
#include "tide/net.h"
#include "tide/pack.h"
#include "tide/sha256.h"
#include "tide/skinned.h"
#include "tide/skinned_data.h"
#include "tide_test.h"

TIDE_TEST(pack_compressed_texture_metadata_is_checked_without_changing_exact_pixels)
{
    uint32_t levels, compressed_bytes;
    TIDE_REQUIRE(tide_texture_blocks_size(128, 128, TIDE_BLOCKS_BC1, &levels, &compressed_bytes));
    uint8_t *rgba = malloc(128u * 128u * 4u), *blocks = calloc(compressed_bytes, 1u);
    TIDE_REQUIRE(rgba != NULL && blocks != NULL);
    memset(rgba, 255, 128u * 128u * 4u);
    uint8_t hash[32] = {17};
    tide_pack_builder builder = {0};
    const uint32_t at = tide_pack_texture_blocks(&builder, hash, 128, 128, rgba,
        (tide_texture_blocks){blocks, TIDE_BLOCKS_BC1, levels, compressed_bytes});
    tide_pack_name(&builder, "compressed", TIDE_PACK_TEXTURE, at, 0, 0);
    size_t size;
    uint8_t *bytes = tide_pack_end(&builder, &size), *copy = malloc(size);
    TIDE_REQUIRE(bytes != NULL && copy != NULL);
    tide_pack_header file;
    memcpy(&file, bytes, sizeof file);
    const size_t trailer = file.region_at + at + sizeof(tide_block) + 8u + 128u * 128u * 4u;
    for (uint32_t variant = 0; variant < 7; variant++) {
        memcpy(copy, bytes, size);
        const uint32_t invalid = variant == 1 ? 0u : UINT32_MAX;
        if (variant >= 1 && variant <= 4) memcpy(copy + trailer + (variant - 1u) * 4u, &invalid, sizeof invalid);
        if (variant == 5 || variant == 6) {
            const uint32_t next = variant == 5 ? UINT32_MAX : 8u + 128u * 128u * 4u;
            memcpy(copy + file.region_at + at + offsetof(tide_block, next), &next, sizeof next);
        }
        TIDE_REQUIRE(tide_pack_load(copy, size));
        const tide_texture_ref image = tide_pack_texture_of(tide_pack_find("compressed", TIDE_PACK_TEXTURE));
        const tide_texture_blocks loaded = tide_pack_texture_blocks_find(image.image.version & TIDE_TEXTURE_NAME);
        TIDE_CHECK(variant ? loaded.pixels == NULL : loaded.bytes == compressed_bytes && loaded.levels == levels);
        tide_texture exact;
        TIDE_REQUIRE(tide_pack_texture_find(image.image.version & TIDE_TEXTURE_NAME, &exact));
        TIDE_CHECK(exact.width == 128 && exact.height == 128 && !memcmp(exact.pixels, rgba, 128u * 128u * 4u));
    }
    tide_pack_unload();
    TIDE_CHECK(!tide_pack_texture_blocks_find(123).pixels);
    free(copy), free(bytes), free(rgba), free(blocks);
}

// Grid2<Color>, as tidec shapes it: cells of 16 bytes, chunks of 32 by 32.
static const tide_grid_shape color_shape = {16u, 2u, {5u, 5u, 0u}};

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

typedef struct handle_entry {
    int64_t live;
    tide_text key;
    uint32_t value;
} handle_entry;

static const tide_map_kind text_handle_kind = {sizeof(handle_entry), offsetof(handle_entry, key), hash_key, hash_probe, equal_key};

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
    const uint32_t rules = tide_pack_string(&b, h, "héllo wörld", 13, &chars);
    TIDE_CHECK(chars == 11);
    uint32_t again_chars = 0;
    TIDE_CHECK(tide_pack_string(&b, h, "héllo wörld", 13, &again_chars) == rules && again_chars == 11);
    const uint32_t rules_bytes = tide_pack_bytes(&b, h, "héllo wörld", 13, &again_chars);
    TIDE_CHECK(rules_bytes != rules && again_chars == 11);
    TIDE_CHECK(tide_pack_bytes(&b, h, "héllo wörld", 13, &again_chars) == rules_bytes);
    TIDE_CHECK(tide_pack_string(&b, h, "héllo wörld", 13, &again_chars) == rules);
    tide_pack_name(&b, "Game.rules", TIDE_PACK_TEXT, rules, 13, chars);
    tide_pack_name(&b, "Game.rulesBytes", TIDE_PACK_BYTES, rules_bytes, 13, chars);

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
    const uint32_t a = tide_pack_string(&b, k1, "alpha", 5, &chars);
    TIDE_CHECK(chars == 5 && tide_pack_bytes(&b, k1, "alpha", 5, &chars) == a);
    const uint32_t bb = tide_pack_bytes(&b, k2, "beta", 4, &chars);
    TIDE_CHECK(tide_pack_string(&b, k2, "beta", 4, &chars) == bb && chars == 4);
    const uint32_t list = tide_pack_list(&b, (const uint32_t[]){a, bb, rules}, 3);
    tide_pack_name(&b, "Game.texts", TIDE_PACK_LIST, list, 3, TIDE_PACK_TEXT);

    uint8_t n1[32], n2[32];
    hash_of("run/0.png", n1);
    hash_of("run/1.png", n2);
    const uint32_t key0 = tide_pack_string(&b, n1, "run/0.png", 9, &chars);
    const uint32_t key1 = tide_pack_string(&b, n2, "run/1.png", 9, &chars);
    uint32_t index;
    const uint32_t map = tide_pack_map(&b, (const uint32_t[]){key0, key1}, (const uint32_t[]){hero, hero}, 2, &index);
    tide_pack_name(&b, "Game.run", TIDE_PACK_MAP, map, index, 2);

    uint8_t w1[32], w2[32], w3[32];
    hash_of("mots/clé.txt", w1);
    hash_of("mots/été.txt", w2);
    hash_of("ça va", w3);
    const uint32_t word0 = tide_pack_string(&b, w1, "mots/clé.txt", 13, &chars);
    TIDE_CHECK(chars == 12);
    const uint32_t word1 = tide_pack_string(&b, w2, "mots/été.txt", 14, &chars);
    TIDE_CHECK(chars == 12);
    const uint32_t fine = tide_pack_string(&b, w3, "ça va", 6, &chars);
    TIDE_CHECK(chars == 5);
    const uint32_t words = tide_pack_map(&b, (const uint32_t[]){word0, word1}, (const uint32_t[]){rules, fine}, 2, &index);
    tide_pack_name(&b, "Game.words", TIDE_PACK_MAP, words, index, 2);
    return tide_pack_end(&b, size);
}

TIDE_TEST(pack_values_read_as_a_worlds_do)
{
    size_t size;
    uint8_t *bytes = build(&size);
    TIDE_REQUIRE(tide_pack_load(bytes, size));
    TIDE_CHECK(tide_pack_region_slots == 1u);

    const tide_pack_entry *rules = tide_pack_find("Game.rules", TIDE_PACK_TEXT);
    TIDE_REQUIRE(rules != NULL);
    const tide_str s = tide_pack_text(rules);
    TIDE_CHECK(s.bytes == 13 && s.chars == 11 && memcmp(s.ptr, "héllo wörld", 13) == 0 && s.ptr[13] == '\0');
    TIDE_CHECK(tide_pack_find("Game.rules", TIDE_PACK_GRID) == NULL);
    TIDE_CHECK(tide_pack_find("Game.nothing", TIDE_PACK_TEXT) == NULL);

    const tide_str by_handle = tide_text_view((tide_text){tide_pack_handle(rules)});
    TIDE_CHECK(by_handle.ptr == s.ptr && by_handle.bytes == 13 && by_handle.chars == 11);

    const tide_list bytes_list = {tide_pack_handle(tide_pack_find("Game.rulesBytes", TIDE_PACK_BYTES))};
    TIDE_CHECK(tide_pack_holds(bytes_list.at));
    TIDE_CHECK(tide_list_count(bytes_list) == 13);
    TIDE_CHECK((const char *)tide_list_at(bytes_list, 0, 1) != s.ptr && memcmp(tide_list_at(bytes_list, 0, 1), s.ptr, 13) == 0);
    TIDE_CHECK(*(const uint8_t *)tide_list_at(bytes_list, 1, 1) == 0xC3u);
    TIDE_CHECK(tide_list_at(bytes_list, 13, 1) == NULL);
    tide_list_cache cache = {0};
    TIDE_CHECK(*(const uint8_t *)tide_list_cached_at(&cache, bytes_list, 0, 1) == 'h');

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

    static tide_draw_list draw;
    tide_draw_reset(&draw);
    const uint32_t slot = tide_draw_grid_texture(&draw, hero, &color_shape);
    TIDE_CHECK(slot != 0 && draw.textures[slot - 1].width == 70 && draw.textures[slot - 1].height == 40);
    uint8_t want_hash[32];
    hash_of("hero.png", want_hash);
    uint64_t want_version;
    memcpy(&want_version, want_hash, sizeof want_version);
    TIDE_CHECK(draw.textures[slot - 1].version == want_version && tide_pack_grid_version(hero) == want_version);
    TIDE_CHECK(draw.textures[slot - 1].pixels[(10 * 70 + 35) * 4 + 2] == 64);
    TIDE_CHECK(tide_draw_grid_texture(&draw, hero, &color_shape) == slot);

    const tide_list texts = {tide_pack_handle(tide_pack_find("Game.texts", TIDE_PACK_LIST))};
    TIDE_CHECK(tide_list_count(texts) == 3);
    const tide_text *first = tide_list_at(texts, 0, sizeof(tide_text));
    TIDE_CHECK(first && tide_text_view(*first).bytes == 5 && tide_text_view(*first).chars == 5);
    const tide_text *second = tide_list_at(texts, 1, sizeof(tide_text));
    TIDE_CHECK(second && tide_str_eq(tide_text_view(*second), tide_str_from_cstr("beta")) && tide_text_view(*second).chars == 4);
    const tide_text *third = tide_list_at(texts, 2, sizeof(tide_text));
    TIDE_REQUIRE(third != NULL);
    TIDE_CHECK(tide_text_view(*third).bytes == 13 && tide_text_view(*third).chars == 11);
    TIDE_CHECK(tide_str_eq(tide_text_view(*third), s) && tide_text_view(*third).ptr == s.ptr);
    const tide_str world = tide_str_substring_from(tide_text_view(*third), 6);
    TIDE_CHECK(world.bytes == 6 && world.chars == 5);

    const tide_map run = tide_pack_map_of(tide_pack_find("Game.run", TIDE_PACK_MAP));
    TIDE_CHECK(tide_map_count(run) == 2 && tide_map_entries(run) == 2 && run.last_order == 2 && run.reserved == 0);
    int64_t order = 0;
    int32_t next = 0;
    TIDE_CHECK(tide_map_next(run, &text_handle_kind, &order, &next) == 0 && order == 1);
    TIDE_CHECK(tide_map_next(run, &text_handle_kind, &order, &next) == 1 && order == 2);
    TIDE_CHECK(tide_map_next(run, &text_handle_kind, &order, &next) == -1);
    const tide_str probe = tide_str_from_cstr("run/1.png");
    const int32_t found = tide_map_find(run, &text_handle_kind, &probe);
    TIDE_CHECK(found == 1);
    const tide_str missing = tide_str_from_cstr("run/2.png");
    TIDE_CHECK(tide_map_find(run, &text_handle_kind, &missing) == -1);
    const uint8_t *entry = tide_map_entry(run, 1, &text_handle_kind);
    TIDE_REQUIRE(entry != NULL);
    tide_grid value;
    memcpy(&value, entry + offsetof(handle_entry, value), sizeof value);
    TIDE_CHECK(value.at == hero.at);

    const tide_map words = tide_pack_map_of(tide_pack_find("Game.words", TIDE_PACK_MAP));
    TIDE_CHECK(tide_map_count(words) == 2);
    const char *const word_keys[2] = {"mots/clé.txt", "mots/été.txt"};
    const int32_t key_bytes[2] = {13, 14}, value_bytes[2] = {13, 6}, value_chars[2] = {11, 5};
    for (int32_t i = 0; i < 2; i++) {
        const tide_str word = tide_str_from_cstr(word_keys[i]);
        TIDE_CHECK(tide_map_find(words, &text_handle_kind, &word) == i);
        const uint8_t *pair = tide_map_entry(words, i, &text_handle_kind);
        TIDE_REQUIRE(pair != NULL);
        tide_text key, said;
        memcpy(&key, pair + offsetof(handle_entry, key), sizeof key);
        memcpy(&said, pair + offsetof(handle_entry, value), sizeof said);
        TIDE_CHECK(tide_text_view(key).bytes == key_bytes[i] && tide_text_view(key).chars == 12);
        TIDE_CHECK(tide_text_view(said).bytes == value_bytes[i] && tide_text_view(said).chars == value_chars[i]);
    }

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
    TIDE_CHECK(!tide_pack_load(bytes, size - 1));
    bytes[9] ^= 1;
    TIDE_CHECK(!tide_pack_load(bytes, size));
    free(bytes);
    TIDE_CHECK(tide_pack_find("Game.rules", TIDE_PACK_TEXT) == NULL);
}

TIDE_TEST(pack_refuses_the_previous_map_format_and_keeps_the_loaded_pack)
{
    size_t size;
    uint8_t *bytes = build(&size);
    TIDE_REQUIRE(tide_pack_load(bytes, size));
    tide_pack_header header;
    memcpy(&header, bytes, sizeof header);
    header.version = TIDE_PACK_VERSION - 1u;
    memcpy(bytes, &header, sizeof header);
    TIDE_CHECK(!tide_pack_load(bytes, size));
    const tide_map run = tide_pack_map_of(tide_pack_find("Game.run", TIDE_PACK_MAP));
    int64_t order = 0;
    int32_t next = 0;
    TIDE_CHECK(run.last_order == 2 && tide_map_next(run, &text_handle_kind, &order, &next) == 0 && order == 1);
    tide_pack_unload();
    free(bytes);
}

typedef struct sound_entry {
    int64_t live;
    tide_text key;
    tide_clip_ref value;
} sound_entry;

static const tide_map_kind text_clip_kind = {sizeof(sound_entry), offsetof(sound_entry, key), hash_key, hash_probe, equal_key};

TIDE_TEST(pack_clip_maps_keep_orders_when_expanded_and_copied)
{
    tide_pack_builder builder = {0};
    uint8_t hash[32];
    uint32_t keys[2], chars, index;
    hash_of("first", hash);
    keys[0] = tide_pack_string(&builder, hash, "first", 5, &chars);
    hash_of("second", hash);
    keys[1] = tide_pack_string(&builder, hash, "second", 6, &chars);
    hash_of("clip bytes", hash);
    const uint32_t clip = tide_pack_clip(&builder, hash, "clip bytes", 10);
    const uint32_t entries = tide_pack_map(&builder, keys, (const uint32_t[]){clip, clip}, 2, &index);
    tide_pack_name(&builder, "Game.sounds", TIDE_PACK_CLIPS, entries, index, 2);
    size_t size;
    uint8_t *bytes = tide_pack_end(&builder, &size);
    TIDE_REQUIRE(tide_pack_load(bytes, size));
    const tide_map sounds = tide_pack_map_of(tide_pack_find("Game.sounds", TIDE_PACK_CLIPS));
    TIDE_CHECK(sounds.last_order == 2);
    int64_t order = 0;
    int32_t next = 0;
    for (int32_t i = 0; i < 2; i++) {
        TIDE_CHECK(tide_map_next(sounds, &text_clip_kind, &order, &next) == i && order == i + 1);
        const sound_entry *e = tide_map_entry(sounds, i, &text_clip_kind);
        TIDE_REQUIRE(e != NULL && e->value.name != 0);
    }
    tide_map copy = tide_map_copy(sounds, &text_clip_kind);
    const tide_str third = tide_str_from_cstr("third");
    bool added;
    sound_entry *e = tide_map_insert(&copy, &text_clip_kind, &third, TIDE_IN_SCRATCH, &added);
    TIDE_REQUIRE(added && e != NULL);
    e->key = tide_text_temp(third);
    e->value = ((const sound_entry *)tide_map_entry(sounds, 0, &text_clip_kind))->value;
    TIDE_CHECK(copy.last_order == 3 && tide_map_next(copy, &text_clip_kind, &order, &next) == 2 && order == 3);
    TIDE_CHECK(tide_map_next(copy, &text_clip_kind, &order, &next) == -1);
    tide_pack_unload();
    tide_scratch_free();
    free(bytes);
}

// ---------------------------------------------------------------------------
// Images to draw with

typedef struct icon_entry {
    int64_t live;
    tide_text key;
    tide_texture_ref value;
} icon_entry;

static const tide_map_kind text_texture_kind = {sizeof(icon_entry), offsetof(icon_entry, key), hash_key, hash_probe, equal_key};

static void image_of(uint8_t *rgba, const int width, const int height, const int seed)
{
    for (int i = 0; i < width * height * 4; i++) rgba[i] = (uint8_t)(seed + i * 7);
}

static uint64_t version_of(const char *name)
{
    uint8_t h[32];
    hash_of(name, h);
    uint64_t version;
    memcpy(&version, h, sizeof version);
    return version;
}

#define MANY 100

// A pack with a texture alone, a list and a map of textures, and a list of
// many small ones. `lead` puts a block before them all, so the same images
// are elsewhere in the region, and `hero_seed` is what the hero's file holds.
static uint8_t *build_textures(size_t *size, const bool lead, const int hero_seed)
{
    tide_pack_builder b = {0};
    uint8_t h[32];
    uint32_t chars;
    if (lead) {
        hash_of("lead", h);
        tide_pack_name(&b, "Game.lead", TIDE_PACK_TEXT, tide_pack_bytes(&b, h, "something before", 16, &chars), 16, 16);
    }
    uint8_t first[6 * 4 * 4], second[3 * 5 * 4];
    image_of(first, 6, 4, hero_seed);
    image_of(second, 3, 5, 100);
    hash_of(hero_seed == 1 ? "hero.png" : "hero-edited.png", h);
    const uint32_t hero = tide_pack_texture(&b, h, 6, 4, first);
    TIDE_CHECK(tide_pack_texture(&b, h, 6, 4, first) == hero);
    tide_pack_name(&b, "Game.hero", TIDE_PACK_TEXTURE, hero, 0, 0);
    hash_of("other.png", h);
    const uint32_t other = tide_pack_texture(&b, h, 3, 5, second);
    TIDE_CHECK(other != hero);

    const uint32_t frames = tide_pack_list(&b, (const uint32_t[]){hero, other, hero}, 3);
    tide_pack_name(&b, "Game.frames", TIDE_PACK_LIST, frames, 3, TIDE_PACK_TEXTURE);

    uint8_t k1[32], k2[32];
    hash_of("icons/a.png", k1);
    hash_of("icons/b.png", k2);
    const uint32_t key_a = tide_pack_bytes(&b, k1, "icons/a.png", 11, &chars);
    const uint32_t key_b = tide_pack_bytes(&b, k2, "icons/b.png", 11, &chars);
    uint32_t index;
    const uint32_t icons = tide_pack_map(&b, (const uint32_t[]){key_a, key_b}, (const uint32_t[]){hero, other}, 2, &index);
    tide_pack_name(&b, "Game.icons", TIDE_PACK_TEXTURES, icons, index, 2);

    uint32_t small[MANY];
    for (int i = 0; i < MANY; i++) {
        char name[32];
        snprintf(name, sizeof name, "small/%d.png", i);
        hash_of(name, h);
        const uint8_t pixel[4] = {(uint8_t)i, (uint8_t)(i * 2), 3, 255};
        small[i] = tide_pack_texture(&b, h, 1, 1, pixel);
    }
    tide_pack_name(&b, "Game.small", TIDE_PACK_LIST, tide_pack_list(&b, small, MANY), MANY, TIDE_PACK_TEXTURE);
    return tide_pack_end(&b, size);
}

static const uint8_t *pixels_of(const tide_texture_ref texture, int32_t *width, int32_t *height, uint64_t *version)
{
    tide_texture image;
    if (!tide_pack_texture_find(texture.image.version, &image)) return NULL;
    if (width) *width = image.width;
    if (height) *height = image.height;
    if (version) *version = image.version;
    return image.pixels;
}

TIDE_TEST(pack_textures_are_drawn_where_the_pack_has_them)
{
    size_t size;
    uint8_t *bytes = build_textures(&size, false, 1);
    TIDE_REQUIRE(tide_pack_load(bytes, size));
    uint8_t first[6 * 4 * 4], second[3 * 5 * 4];
    image_of(first, 6, 4, 1);
    image_of(second, 3, 5, 100);

    const tide_texture_ref hero = tide_pack_texture_of(tide_pack_find("Game.hero", TIDE_PACK_TEXTURE));
    TIDE_CHECK(!hero.image.pixels && !hero.target.id && hero.image.width == 6 && hero.image.height == 4 && hero.image.version);
    const tide_texture_ref none = tide_pack_texture_of(NULL);
    TIDE_CHECK(!none.image.version && !none.image.width && !none.image.pixels);
    TIDE_CHECK(!tide_pack_texture_of(tide_pack_find("Game.frames", TIDE_PACK_LIST)).image.version);
    TIDE_CHECK(tide_pack_find("Game.hero", TIDE_PACK_GRID) == NULL);

    int32_t width, height;
    uint64_t version;
    const uint8_t *pixels = pixels_of(hero, &width, &height, &version);
    TIDE_REQUIRE(pixels != NULL);
    TIDE_CHECK(width == 6 && height == 4 && memcmp(pixels, first, sizeof first) == 0);
    TIDE_CHECK(version == version_of("hero.png"));
    tide_texture image;
    TIDE_CHECK(!tide_pack_texture_find(version, &image));
    TIDE_CHECK(!tide_pack_texture_find(0, &image));

    static tide_draw_list draw;
    tide_draw_reset(&draw);
    const uint32_t slot = tide_draw_texture_source(&draw, hero);
    TIDE_REQUIRE(slot != 0);
    const tide_draw_texture *t = &draw.textures[slot - 1];
    TIDE_CHECK(t->space == TIDE_PIXELS_PACK && t->width == 6 && t->height == 4 && t->version == version && t->id == version);
    TIDE_CHECK(t->pixels == pixels && t->capacity == 0);
    TIDE_CHECK(tide_draw_texture_source(&draw, hero) == slot && draw.texture_count == 1);
    TIDE_CHECK(tide_draw_texture_source(&draw, none) == 0);

    const tide_list frames = {tide_pack_handle(tide_pack_find("Game.frames", TIDE_PACK_LIST))};
    TIDE_CHECK(tide_pack_holds(frames.at) && tide_list_count(frames) == 3);
    const tide_texture_ref *frame0 = tide_list_at(frames, 0, sizeof(tide_texture_ref));
    const tide_texture_ref *frame1 = tide_list_at(frames, 1, sizeof(tide_texture_ref));
    const tide_texture_ref *frame2 = tide_list_at(frames, 2, sizeof(tide_texture_ref));
    TIDE_REQUIRE(frame0 && frame1 && frame2);
    TIDE_CHECK(frame0->image.version && frame0->image.version != hero.image.version && frame0->image.version != frame2->image.version);
    TIDE_CHECK(pixels_of(*frame0, NULL, NULL, NULL) == pixels && pixels_of(*frame2, NULL, NULL, NULL) == pixels);
    TIDE_CHECK(frame1->image.width == 3 && frame1->image.height == 5 && !frame1->image.pixels);
    TIDE_CHECK(tide_list_at(frames, 3, sizeof(tide_texture_ref)) == NULL);
    const tide_texture_ref other = *frame1;
    const tide_texture_ref first_frame = *frame0;
    TIDE_CHECK(tide_draw_texture_source(&draw, *frame0) == slot && tide_draw_texture_source(&draw, *frame2) == slot);
    const uint32_t other_slot = tide_draw_texture_source(&draw, other);
    TIDE_REQUIRE(other_slot != 0 && other_slot != slot);
    TIDE_CHECK(draw.textures[other_slot - 1].version == version_of("other.png"));
    TIDE_CHECK(memcmp(draw.textures[other_slot - 1].pixels, second, sizeof second) == 0);

    const tide_map icons = tide_pack_map_of(tide_pack_find("Game.icons", TIDE_PACK_TEXTURES));
    TIDE_CHECK(tide_map_count(icons) == 2 && tide_map_entries(icons) == 2 && icons.last_order == 2);
    int64_t order = 0;
    int32_t next = 0;
    TIDE_CHECK(tide_map_next(icons, &text_texture_kind, &order, &next) == 0 && order == 1);
    TIDE_CHECK(tide_map_next(icons, &text_texture_kind, &order, &next) == 1 && order == 2);
    TIDE_CHECK(tide_map_next(icons, &text_texture_kind, &order, &next) == -1);
    const tide_str key_b = tide_str_from_cstr("icons/b.png");
    const int32_t found = tide_map_find(icons, &text_texture_kind, &key_b);
    TIDE_REQUIRE(found == 1);
    const icon_entry *entry = (const icon_entry *)(const void *)tide_map_entry(icons, found, &text_texture_kind);
    TIDE_REQUIRE(entry != NULL);
    TIDE_CHECK(entry->live && entry->value.image.version != other.image.version);
    TIDE_CHECK(tide_draw_texture_source(&draw, entry->value) == other_slot);
    const tide_str key_a = tide_str_from_cstr("icons/a.png");
    const icon_entry *entry_a =
        (const icon_entry *)(const void *)tide_map_entry(icons, tide_map_find(icons, &text_texture_kind, &key_a), &text_texture_kind);
    TIDE_REQUIRE(entry_a != NULL);
    TIDE_CHECK(tide_draw_texture_source(&draw, entry_a->value) == slot);
    const tide_str key_c = tide_str_from_cstr("icons/c.png");
    TIDE_CHECK(tide_map_find(icons, &text_texture_kind, &key_c) == -1);
    TIDE_CHECK(draw.texture_count == 2);

    const tide_list small = {tide_pack_handle(tide_pack_find("Game.small", TIDE_PACK_LIST))};
    TIDE_CHECK(tide_list_count(small) == MANY);
    bool all = true;
    for (int i = 0; i < MANY; i++) {
        const tide_texture_ref *one = tide_list_at(small, i, sizeof(tide_texture_ref));
        const uint8_t *pixel = one ? pixels_of(*one, NULL, NULL, NULL) : NULL;
        all = all && pixel && pixel[0] == (uint8_t)i && pixel[1] == (uint8_t)(i * 2) && pixel[3] == 255;
    }
    TIDE_CHECK(all);

    static tide_draw_list over;
    tide_draw_reset(&over);
    const uint32_t mark = tide_scratch_mark();
    const tide_vertex corners[3] = {{{0, 0}, {0, 0}, {1, 1, 1, 1}}, {{8, 0}, {1, 0}, {1, 1, 1, 1}}, {{0, 8}, {0, 1}, {1, 1, 1, 1}}};
    tide_draw_mesh_texture(&over, tide_list_from(corners, 3, sizeof(tide_vertex)),
                           tide_list_from((const int32_t[]){0, 1, 2}, 3, sizeof(int32_t)), tide_texture_filtered(other, TIDE_FILTER_POINT));
    tide_scratch_reset(mark);
    TIDE_REQUIRE(over.count > 0 && over.commands[over.count - 1].kind == TIDE_DRAW_MESH);
    static tide_draw_list under;
    tide_draw_reset(&under);
    tide_draw_append(&under, &over);
    TIDE_REQUIRE(under.count > 0 && under.commands[under.count - 1].kind == TIDE_DRAW_MESH);
    const uint32_t appended = under.commands[under.count - 1].mesh.texture;
    TIDE_REQUIRE(appended != 0);
    TIDE_CHECK(under.textures[appended - 1].space == TIDE_PIXELS_PACK && under.textures[appended - 1].capacity == 0);
    TIDE_CHECK(under.textures[appended - 1].pixels == pixels_of(other, NULL, NULL, NULL));

    tide_draw_reset(&draw);
    tide_draw_reset(&draw);
    TIDE_CHECK(draw.texture_count == 0);
    TIDE_CHECK(memcmp(pixels_of(hero, NULL, NULL, NULL), first, sizeof first) == 0);
    tide_draw_forget(&under);
    tide_draw_free(&over);
    tide_draw_free(&under);

    tide_pack_unload();
    TIDE_CHECK(tide_draw_texture_source(&draw, hero) == 0 && draw.texture_count == 0);
    TIDE_CHECK(pixels_of(hero, NULL, NULL, NULL) == NULL);
    free(bytes);

    bytes = build_textures(&size, true, 1);
    TIDE_REQUIRE(tide_pack_load(bytes, size));
    const tide_texture_ref again = tide_pack_texture_of(tide_pack_find("Game.hero", TIDE_PACK_TEXTURE));
    // Field by field: the bytes between a reference's fields are nobody's
    TIDE_CHECK(again.image.pixels == hero.image.pixels && again.image.width == hero.image.width &&
               again.image.height == hero.image.height && again.image.version == hero.image.version &&
               again.target.id == hero.target.id);
    tide_draw_reset(&draw);
    const uint32_t found_again = tide_draw_texture_source(&draw, hero);
    TIDE_REQUIRE(found_again != 0);
    TIDE_CHECK(memcmp(draw.textures[found_again - 1].pixels, first, sizeof first) == 0);
    TIDE_CHECK(draw.textures[found_again - 1].version == version);
    free(bytes);

    bytes = build_textures(&size, false, 2);
    TIDE_REQUIRE(tide_pack_load(bytes, size));
    const tide_texture_ref edited = tide_pack_texture_of(tide_pack_find("Game.hero", TIDE_PACK_TEXTURE));
    TIDE_CHECK(edited.image.version == hero.image.version);
    tide_draw_reset(&draw);
    const uint32_t edited_slot = tide_draw_texture_source(&draw, hero);
    TIDE_REQUIRE(edited_slot != 0);
    image_of(first, 6, 4, 2);
    TIDE_CHECK(memcmp(draw.textures[edited_slot - 1].pixels, first, sizeof first) == 0);
    TIDE_CHECK(draw.textures[edited_slot - 1].version == version_of("hero-edited.png"));
    TIDE_CHECK(draw.textures[edited_slot - 1].version != version);
    TIDE_CHECK(tide_draw_texture_source(&draw, first_frame) == edited_slot);
    free(bytes);

    tide_pack_builder b = {0};
    bytes = tide_pack_end(&b, &size);
    TIDE_REQUIRE(tide_pack_load(bytes, size));
    tide_draw_reset(&draw);
    TIDE_CHECK(tide_draw_texture_source(&draw, hero) == 0);
    TIDE_CHECK(!tide_pack_texture_of(tide_pack_find("Game.hero", TIDE_PACK_TEXTURE)).image.version);

    tide_draw_free(&draw);
    tide_pack_unload();
    free(bytes);
}

TIDE_TEST(pack_textures_cut_short_are_none)
{
    tide_pack_builder b = {0};
    uint8_t h[32];
    uint8_t image[2 * 2 * 4];
    image_of(image, 2, 2, 9);
    hash_of("cut.png", h);
    const uint32_t at = tide_pack_texture(&b, h, 2, 2, image);
    tide_pack_name(&b, "Game.cut", TIDE_PACK_TEXTURE, at, 0, 0);
    tide_pack_name(&b, "Game.list", TIDE_PACK_LIST, tide_pack_list(&b, (const uint32_t[]){at, at}, 2), 2, TIDE_PACK_TEXTURE);
    size_t size;
    uint8_t *bytes = tide_pack_end(&b, &size);
    tide_pack_header header;
    memcpy(&header, bytes, sizeof header);

    const int32_t huge[2] = {30000, 30000};
    memcpy(bytes + header.region_at + at + sizeof(tide_block), huge, sizeof huge);
    TIDE_REQUIRE(tide_pack_load(bytes, size));
    TIDE_CHECK(!tide_pack_texture_of(tide_pack_find("Game.cut", TIDE_PACK_TEXTURE)).image.version);
    const tide_list list = {tide_pack_handle(tide_pack_find("Game.list", TIDE_PACK_LIST))};
    TIDE_CHECK(tide_list_count(list) == 2);
    const tide_texture_ref *one = tide_list_at(list, 1, sizeof(tide_texture_ref));
    TIDE_CHECK(one && !one->image.version && !one->image.width);

    const int32_t flat[2] = {0, 2};
    memcpy(bytes + header.region_at + at + sizeof(tide_block), flat, sizeof flat);
    TIDE_REQUIRE(tide_pack_load(bytes, size));
    TIDE_CHECK(!tide_pack_texture_of(tide_pack_find("Game.cut", TIDE_PACK_TEXTURE)).image.version);
    tide_pack_unload();
    free(bytes);
}

// A pack with a cube of six faces of `side` pixels, the last of them `last`
// by `last`, a second cube of the same files, and one of the faces as a
// texture. `lead` puts a block before them all, and `edit` is what's in the
// first face.
static uint8_t *build_cubes(size_t *size, const bool lead, const int side, const int last, const int edit)
{
    static const char *const files[TIDE_CUBE_FACES] = {"px.png", "nx.png", "py.png", "ny.png", "pz.png", "nz.png"};
    tide_pack_builder b = {0};
    uint8_t h[32];
    if (lead) {
        uint32_t chars;
        hash_of("lead", h);
        tide_pack_name(&b, "Game.lead", TIDE_PACK_TEXT, tide_pack_bytes(&b, h, "moved along", 11, &chars), 11, chars);
    }
    uint32_t faces[TIDE_CUBE_FACES];
    for (int face = 0; face < TIDE_CUBE_FACES; face++) {
        const int wide = face == TIDE_CUBE_FACES - 1 ? last : side;
        uint8_t *image = malloc((size_t)wide * (size_t)wide * 4u);
        image_of(image, wide, wide, face == 0 ? edit : 10 * face);
        char named[32];
        snprintf(named, sizeof named, "%d/%s", face == 0 ? edit : 0, files[face]);
        hash_of(named, h);
        faces[face] = tide_pack_texture(&b, h, wide, wide, image);
        free(image);
    }
    tide_pack_name(&b, "Game.sky", TIDE_PACK_CUBEMAP, tide_pack_list(&b, faces, TIDE_CUBE_FACES), TIDE_CUBE_FACES, (uint32_t)side);
    tide_pack_name(&b, "Game.skyAgain", TIDE_PACK_CUBEMAP, tide_pack_list(&b, faces, TIDE_CUBE_FACES), TIDE_CUBE_FACES, (uint32_t)side);
    tide_pack_name(&b, "Game.up", TIDE_PACK_TEXTURE, faces[2], 0, 0);
    tide_pack_name(&b, "Game.five", TIDE_PACK_CUBEMAP, tide_pack_list(&b, faces, TIDE_CUBE_FACES - 1), TIDE_CUBE_FACES - 1, (uint32_t)side);
    return tide_pack_end(&b, size);
}

TIDE_TEST(pack_cubemaps_are_six_images_named_as_one)
{
    size_t size;
    uint8_t *bytes = build_cubes(&size, false, 4, 4, 1);
    TIDE_REQUIRE(tide_pack_load(bytes, size));

    const tide_cubemap_ref sky = tide_pack_cubemap_of(tide_pack_find("Game.sky", TIDE_PACK_CUBEMAP));
    TIDE_REQUIRE(sky.named != 0 && sky.target.id == 0);
    TIDE_CHECK(tide_pack_cubemap_of(NULL).named == 0);
    TIDE_CHECK(tide_pack_cubemap_of(tide_pack_find("Game.up", TIDE_PACK_TEXTURE)).named == 0);
    TIDE_CHECK(tide_pack_find("Game.sky", TIDE_PACK_TEXTURE) == NULL && tide_pack_find("Game.sky", TIDE_PACK_LIST) == NULL);

    int32_t side = 0;
    uint64_t version = 0;
    const uint8_t *const *faces = NULL;
    TIDE_REQUIRE(tide_pack_cubemap_find(sky.named, &side, &version, &faces) && side == 4 && version != 0);
    for (int face = 0; face < TIDE_CUBE_FACES; face++) {
        uint8_t wanted[4 * 4 * 4];
        image_of(wanted, 4, 4, face == 0 ? 1 : 10 * face);
        TIDE_CHECK(memcmp(faces[face], wanted, sizeof wanted) == 0);
    }
    const tide_cubemap_ref again = tide_pack_cubemap_of(tide_pack_find("Game.skyAgain", TIDE_PACK_CUBEMAP));
    int32_t side_again = 0;
    uint64_t version_again = 0;
    const uint8_t *const *faces_again = NULL;
    TIDE_REQUIRE(again.named != 0 && again.named != sky.named);
    TIDE_REQUIRE(tide_pack_cubemap_find(again.named, &side_again, &version_again, &faces_again));
    TIDE_CHECK(version_again == version && faces_again != faces && faces_again[0] == faces[0] && faces_again[5] == faces[5]);
    const tide_texture_ref up = tide_pack_texture_of(tide_pack_find("Game.up", TIDE_PACK_TEXTURE));
    TIDE_CHECK(pixels_of(up, NULL, NULL, NULL) == faces[2]);
    TIDE_CHECK(!tide_pack_cubemap_find(0, &side_again, &version_again, &faces_again));
    TIDE_CHECK(!tide_pack_cubemap_find(version, &side_again, &version_again, &faces_again));
    const tide_cubemap_ref five = tide_pack_cubemap_of(tide_pack_find("Game.five", TIDE_PACK_CUBEMAP));
    TIDE_CHECK(five.named != 0 && !tide_pack_cubemap_find(five.named, &side_again, &version_again, &faces_again));

    static tide_draw_list draw;
    tide_draw_reset(&draw);
    const uint32_t slot = tide_draw_cube_source(&draw, sky);
    TIDE_REQUIRE(slot != 0);
    const tide_draw_texture *t = &draw.textures[slot - 1];
    TIDE_CHECK(t->space == TIDE_PIXELS_PACK_CUBE && t->width == 4 && t->height == 4 && t->version == version && t->id == version);
    TIDE_CHECK((const uint8_t *const *)(uintptr_t)t->pixels == faces && t->capacity == 0);
    TIDE_CHECK(tide_draw_cube_source(&draw, sky) == slot && draw.texture_count == 1);
    TIDE_CHECK(tide_draw_cube_source(&draw, again) == slot);
    TIDE_CHECK(tide_draw_cube_source(&draw, five) == 0 && tide_draw_cube_source(&draw, (tide_cubemap_ref){0}) == 0);
    TIDE_CHECK(tide_cubemap_face(sky, 0).id == TIDE_RENDER_INVALID);
    static tide_draw_list other;
    tide_draw_reset(&other);
    tide_draw_append(&other, &draw);
    tide_draw_reset(&draw);
    tide_draw_reset(&draw);
    tide_draw_reset(&draw);
    TIDE_CHECK(memcmp(faces[1], (const uint8_t[4]){10, 17, 24, 31}, 4) == 0);

    free(bytes);
    bytes = build_cubes(&size, true, 4, 4, 2);
    TIDE_REQUIRE(tide_pack_load(bytes, size));
    TIDE_CHECK(tide_pack_cubemap_of(tide_pack_find("Game.sky", TIDE_PACK_CUBEMAP)).named == sky.named);
    uint64_t edited = 0;
    TIDE_REQUIRE(tide_pack_cubemap_find(sky.named, &side, &edited, &faces) && side == 4 && edited != version);
    uint8_t first[4 * 4 * 4];
    image_of(first, 4, 4, 2);
    TIDE_CHECK(memcmp(faces[0], first, sizeof first) == 0);
    tide_draw_reset(&draw);
    const uint32_t next = tide_draw_cube_source(&draw, sky);
    TIDE_REQUIRE(next != 0);
    TIDE_CHECK(draw.textures[next - 1].version == edited && (const uint8_t *const *)(uintptr_t)draw.textures[next - 1].pixels == faces);

    free(bytes);
    bytes = build_cubes(&size, false, 4, 2, 1);
    TIDE_REQUIRE(tide_pack_load(bytes, size));
    TIDE_CHECK(!tide_pack_cubemap_find(sky.named, &side, &edited, &faces));
    tide_draw_reset(&draw);
    TIDE_CHECK(tide_draw_cube_source(&draw, sky) == 0);

    tide_pack_unload();
    TIDE_CHECK(!tide_pack_cubemap_find(sky.named, &side, &edited, &faces) && tide_draw_cube_source(&draw, sky) == 0);
    tide_draw_free(&draw);
    tide_draw_free(&other);
    free(bytes);
}

// ---------------------------------------------------------------------------
// Meshes

// A vertex of a game's, as generated code lays one out.
typedef struct corner {
    float position[3];
    float normal[3];
    float glow; // No file fills it: the game's default, given as the pack is loaded
} corner;

typedef struct prop_entry {
    int64_t live;
    tide_text key;
    tide_mesh_lists value;
} prop_entry;

static const tide_map_kind text_mesh_kind = {sizeof(prop_entry), offsetof(prop_entry, key), hash_key, hash_probe, equal_key};

#define CORNER_LAYOUT 77u // What a build makes of its vertex struct: any number

static const uint32_t tree_faces[6] = {0, 2, 1, 0, 3, 2};
static const uint32_t rock_faces[3] = {0, 1, 2};
static uint32_t tree_blocks[2], rock_blocks[2], plain_blocks[2];

// A pack with a mesh alone, a list and a map of two, and a mesh of the
// built-in vertex. `lead` puts a block before them all, so the same meshes are
// elsewhere in the region, and `height` is what's in the tree's file.
static uint8_t *build_meshes(size_t *size, const bool lead, const float height)
{
    tide_pack_builder b = {0};
    uint8_t h[32];
    uint32_t chars;
    if (lead) {
        hash_of("lead", h);
        tide_pack_name(&b, "Game.lead", TIDE_PACK_TEXT, tide_pack_bytes(&b, h, "moved along", 11, &chars), 11, chars);
    }
    const corner tree[4] = {{{0, 0, 0}, {0, 0, 1}, 0}, {{1, 0, 0}, {0, 0, 1}, 0}, {{1, height, 0}, {0, 0, 1}, 0}, {{0, height, 0}, {0, 0, 1}, 0}};
    const corner rock[3] = {{{5, 0, 0}, {0, 1, 0}, 0}, {{6, 0, 0}, {0, 1, 0}, 0}, {{5, 0, 1}, {0, 1, 0}, 0}};
    char named[32];
    snprintf(named, sizeof named, "tree.glb %g", (double)height);
    hash_of(named, h);
    tide_pack_mesh(&b, h, tree, sizeof(corner), 4, tree_faces, 6, tree_blocks);
    uint32_t again[2];
    tide_pack_mesh(&b, h, tree, sizeof(corner), 4, tree_faces, 6, again);
    TIDE_CHECK(again[0] == tree_blocks[0] && again[1] == tree_blocks[1]);
    hash_of("rock.glb", h);
    tide_pack_mesh(&b, h, rock, sizeof(corner), 3, rock_faces, 3, rock_blocks);
    const tide_vertex3 plain[3] = {{.position = {0, 0, 0}}, {.position = {0, 1, 0}}, {.position = {1, 0, 0}}};
    hash_of("plain.obj", h);
    tide_pack_mesh(&b, h, plain, sizeof(tide_vertex3), 3, rock_faces, 3, plain_blocks);
    tide_pack_name(&b, "Game.tree", TIDE_PACK_MESH, tree_blocks[0], tree_blocks[1], CORNER_LAYOUT);
    tide_pack_name(&b, "Game.plain", TIDE_PACK_MESH, plain_blocks[0], plain_blocks[1], 3u);
    const uint32_t both[4] = {rock_blocks[0], rock_blocks[1], tree_blocks[0], tree_blocks[1]};
    tide_pack_name(&b, "Game.props", TIDE_PACK_MESH_LIST, tide_pack_mesh_list(&b, both, 2), 2, CORNER_LAYOUT);
    uint32_t keys[2], index;
    hash_of("art/rock.glb", h);
    keys[0] = tide_pack_string(&b, h, "art/rock.glb", 12, &chars);
    hash_of("art/tree.glb", h);
    keys[1] = tide_pack_string(&b, h, "art/tree.glb", 12, &chars);
    const uint32_t entries = tide_pack_mesh_map(&b, keys, both, 2, &index);
    tide_pack_name(&b, "Game.named", TIDE_PACK_MESHES, entries, index, CORNER_LAYOUT);
    hash_of("table", h);
    tide_pack_name(&b, "Game.table", TIDE_PACK_BYTES, tide_pack_bytes(&b, h, "\1\2\3\4", 4, &chars), 4, chars);
    return tide_pack_end(&b, size);
}

TIDE_TEST(pack_meshes_are_two_lists_told_by_whats_in_them)
{
    size_t size;
    uint8_t *bytes = build_meshes(&size, false, 1.0f);
    TIDE_REQUIRE(tide_pack_load(bytes, size));
    TIDE_CHECK(tide_pack_find_mesh("Game.tree", TIDE_PACK_MESH, CORNER_LAYOUT + 1u) == NULL);
    const tide_pack_entry *e = tide_pack_find_mesh("Game.tree", TIDE_PACK_MESH, CORNER_LAYOUT);
    TIDE_REQUIRE(e != NULL);
    TIDE_CHECK(!tide_pack_mesh_of(e, 12u).vertices.at && !tide_pack_mesh_of(NULL, sizeof(corner)).vertices.at);
    const tide_mesh_lists tree = tide_pack_mesh_of(e, sizeof(corner));
    TIDE_REQUIRE(tide_list_count(tree.vertices) == 4 && tide_list_count(tree.indices) == 6);
    const corner *third = tide_list_at(tree.vertices, 2, sizeof(corner));
    TIDE_CHECK(third->position[0] == 1.0f && third->position[1] == 1.0f && third->normal[2] == 1.0f);
    for (int32_t i = 0; i < 6; i++) TIDE_CHECK(*(const uint32_t *)tide_list_at(tree.indices, i, sizeof(uint32_t)) == tree_faces[i]);
    TIDE_CHECK(tide_list_at(tree.vertices, 4, sizeof(corner)) == NULL);

    const tide_list props = {tide_pack_handle(tide_pack_find_mesh("Game.props", TIDE_PACK_MESH_LIST, CORNER_LAYOUT))};
    TIDE_REQUIRE(tide_list_count(props) == 2);
    const tide_mesh_lists *rock = tide_list_at(props, 0, sizeof(tide_mesh_lists));
    const tide_mesh_lists *second = tide_list_at(props, 1, sizeof(tide_mesh_lists));
    TIDE_CHECK(tide_list_count(rock->vertices) == 3 && tide_list_count(rock->indices) == 3);
    TIDE_CHECK(second->vertices.at == tree.vertices.at && second->indices.at == tree.indices.at);
    const tide_map named = tide_pack_mesh_map_of(tide_pack_find_mesh("Game.named", TIDE_PACK_MESHES, CORNER_LAYOUT));
    TIDE_REQUIRE(tide_map_count(named) == 2 && tide_map_entries(named) == 2 && named.last_order == 2);
    int64_t order = 0;
    int32_t next = 0;
    TIDE_CHECK(tide_map_next(named, &text_mesh_kind, &order, &next) == 0 && order == 1);
    TIDE_CHECK(tide_map_next(named, &text_mesh_kind, &order, &next) == 1 && order == 2);
    TIDE_CHECK(tide_map_next(named, &text_mesh_kind, &order, &next) == -1);
    const tide_str key = {"art/tree.glb", 12, 12}, missing = {"art/none.glb", 12, 12};
    const int32_t found = tide_map_find(named, &text_mesh_kind, &key);
    TIDE_REQUIRE(found == 1 && tide_map_find(named, &text_mesh_kind, &missing) == -1);
    const prop_entry *entry = tide_map_entry(named, found, &text_mesh_kind);
    TIDE_CHECK(entry->value.vertices.at == tree.vertices.at);
    TIDE_CHECK(!tide_pack_mesh_map_of(NULL).entries.at);

    uint64_t vertices = 0, indices = 0, other = 0;
    TIDE_CHECK(tide_pack_list_version(tree.vertices, sizeof(corner), &vertices) && tide_pack_list_version(tree.indices, 4u, &indices));
    TIDE_CHECK(vertices && indices && vertices != indices);
    TIDE_CHECK(!tide_pack_list_version(tree.vertices, 12u, &other));
    TIDE_CHECK(!tide_pack_list_version((tide_list){tide_pack_handle(tide_pack_find("Game.table", TIDE_PACK_BYTES))}, 1u, &other));
    TIDE_CHECK(!tide_pack_list_version(props, sizeof(tide_mesh_lists), &other) && !tide_pack_list_version((tide_list){0}, 4u, &other));

    uint32_t count = 0;
    corner *open = tide_pack_mesh_open(e, 0, sizeof(corner), &count);
    TIDE_REQUIRE(open && count == 4);
    for (uint32_t i = 0; i < count; i++) open[i].glow = 0.5f;
    TIDE_CHECK(((const corner *)tide_list_at(tree.vertices, 3, sizeof(corner)))->glow == 0.5f);
    TIDE_CHECK(tide_pack_mesh_open(e, 1, sizeof(corner), &count) == NULL && count == 0);
    TIDE_CHECK(tide_pack_mesh_open(e, 0, 12u, &count) != NULL && count == 0);
    const tide_pack_entry *listed = tide_pack_find("Game.props", TIDE_PACK_MESH_LIST), *mapped = tide_pack_find("Game.named", TIDE_PACK_MESHES);
    TIDE_CHECK(tide_pack_mesh_open(listed, 0, sizeof(corner), &count) && count == 3);
    TIDE_CHECK(tide_pack_mesh_open(listed, 1, sizeof(corner), &count) == (void *)open && count == 4);
    TIDE_CHECK(tide_pack_mesh_open(listed, 2, sizeof(corner), &count) == NULL);
    TIDE_CHECK(tide_pack_mesh_open(mapped, 1, sizeof(corner), &count) == (void *)open && tide_pack_mesh_open(mapped, 2, sizeof(corner), &count) == NULL);
    TIDE_CHECK(tide_pack_mesh_open(NULL, 0, sizeof(corner), &count) == NULL);
    const tide_pack_entry elsewhere = *e;
    TIDE_CHECK(tide_pack_mesh_open(&elsewhere, 0, sizeof(corner), &count) == NULL);

    static tide_draw_list draw;
    const tide_mesh_lists plain = tide_pack_mesh_of(tide_pack_find("Game.plain", TIDE_PACK_MESH), sizeof(tide_vertex3));
    tide_draw_reset(&draw);
    tide_draw_mesh3_lists(&draw, plain.vertices, plain.indices, tide_identity_f4x4());
    tide_draw_mesh3_lists(&draw, plain.vertices, plain.indices, tide_identity_f4x4());
    TIDE_REQUIRE(draw.mesh_count == 1 && draw.meshes[0].vertex_count == 3 && draw.meshes[0].index_count == 3);
    const uint64_t drawn = draw.meshes[0].version;

    free(bytes);
    bytes = build_meshes(&size, true, 1.0f);
    TIDE_REQUIRE(tide_pack_load(bytes, size));
    const tide_mesh_lists moved = tide_pack_mesh_of(tide_pack_find("Game.plain", TIDE_PACK_MESH), sizeof(tide_vertex3));
    TIDE_CHECK(moved.vertices.at != plain.vertices.at);
    tide_draw_reset(&draw);
    tide_draw_mesh3_lists(&draw, moved.vertices, moved.indices, tide_identity_f4x4());
    TIDE_CHECK(draw.mesh_count == 1 && draw.meshes[0].version == drawn);
    uint64_t before = 0, after = 0;
    const tide_mesh_lists same = tide_pack_mesh_of(tide_pack_find("Game.tree", TIDE_PACK_MESH), sizeof(corner));
    TIDE_CHECK(tide_pack_list_version(same.vertices, sizeof(corner), &before) && before == vertices);
    free(bytes);
    bytes = build_meshes(&size, true, 2.0f);
    TIDE_REQUIRE(tide_pack_load(bytes, size));
    const tide_mesh_lists edited = tide_pack_mesh_of(tide_pack_find("Game.tree", TIDE_PACK_MESH), sizeof(corner));
    TIDE_CHECK(tide_pack_list_version(edited.vertices, sizeof(corner), &after) && after != vertices);
    tide_pack_unload();
    tide_draw_free(&draw);
    free(bytes);
}

TIDE_TEST(pack_meshes_cut_short_are_empty)
{
    size_t size;
    uint8_t *bytes = build_meshes(&size, false, 1.0f);
    tide_pack_header header;
    memcpy(&header, bytes, sizeof header);
    uint8_t *region = bytes + header.region_at;
    tide_block had;
    memcpy(&had, region + tree_blocks[0], sizeof had);

    tide_block wrong = had;
    wrong.a = 1000000u;
    memcpy(region + tree_blocks[0], &wrong, sizeof wrong);
    TIDE_REQUIRE(tide_pack_load(bytes, size));
    TIDE_CHECK(!tide_pack_mesh_of(tide_pack_find("Game.tree", TIDE_PACK_MESH), sizeof(corner)).vertices.at);
    const tide_list props = {tide_pack_handle(tide_pack_find("Game.props", TIDE_PACK_MESH_LIST))};
    TIDE_REQUIRE(tide_list_count(props) == 2);
    const tide_mesh_lists *rock = tide_list_at(props, 0, sizeof(tide_mesh_lists)), *tree = tide_list_at(props, 1, sizeof(tide_mesh_lists));
    TIDE_CHECK(tide_list_count(rock->vertices) == 3 && !tree->vertices.at && !tree->indices.at);
    const tide_map named = tide_pack_mesh_map_of(tide_pack_find("Game.named", TIDE_PACK_MESHES));
    const prop_entry *entry = tide_map_entry(named, 1, &text_mesh_kind);
    TIDE_CHECK(tide_map_count(named) == 2 && !entry->value.vertices.at);
    uint32_t count = 7;
    TIDE_CHECK(tide_pack_mesh_open(tide_pack_find("Game.tree", TIDE_PACK_MESH), 0, sizeof(corner), &count) != NULL && count == 0);

    wrong = had;
    wrong.next = header.region_size;
    memcpy(region + tree_blocks[0], &wrong, sizeof wrong);
    TIDE_REQUIRE(tide_pack_load(bytes, size));
    TIDE_CHECK(!tide_pack_mesh_of(tide_pack_find("Game.tree", TIDE_PACK_MESH), sizeof(corner)).vertices.at);

    memcpy(region + tree_blocks[0], &had, sizeof had);
    tide_block faces;
    memcpy(&faces, region + tree_blocks[1], sizeof faces);
    const uint32_t two = 2u;
    memcpy(region + tree_blocks[1] + faces.next + sizeof(uint64_t), &two, sizeof two);
    TIDE_REQUIRE(tide_pack_load(bytes, size));
    TIDE_CHECK(!tide_pack_mesh_of(tide_pack_find("Game.tree", TIDE_PACK_MESH), sizeof(corner)).vertices.at);

    const uint32_t four = 4u;
    memcpy(region + tree_blocks[1] + faces.next + sizeof(uint64_t), &four, sizeof four);
    tide_pack_entry *entries = (tide_pack_entry *)(void *)(bytes + sizeof header);
    for (uint32_t i = 0; i < header.entries; i++) {
        if (entries[i].kind == TIDE_PACK_MESH && entries[i].extra == CORNER_LAYOUT) entries[i].size = header.region_size - 8u;
    }
    TIDE_REQUIRE(tide_pack_load(bytes, size));
    TIDE_CHECK(!tide_pack_mesh_of(tide_pack_find("Game.tree", TIDE_PACK_MESH), sizeof(corner)).vertices.at);
    const tide_list whole = {tide_pack_handle(tide_pack_find("Game.props", TIDE_PACK_MESH_LIST))};
    const tide_mesh_lists *still = tide_list_at(whole, 1, sizeof(tide_mesh_lists));
    TIDE_CHECK(tide_list_count(whole) == 2 && tide_list_count(still->vertices) == 4 && tide_list_count(still->indices) == 6);
    tide_pack_unload();
    free(bytes);
}

// Skinned meshes retain typed geometry while carrying portable model bytes.
typedef struct skinned_prop_entry {
    int64_t live;
    tide_text key;
    tide_skinned_lists value;
} skinned_prop_entry;

static const tide_map_kind text_skinned_kind = {sizeof(skinned_prop_entry), offsetof(skinned_prop_entry, key), hash_key,
                                               hash_probe, equal_key};

static void skin_word(uint8_t *data, const uint32_t at, const uint32_t value)
{
    data[at] = (uint8_t)value;
    data[at + 1u] = (uint8_t)(value >> 8);
    data[at + 2u] = (uint8_t)(value >> 16);
    data[at + 3u] = (uint8_t)(value >> 24);
}

static void skin_float(uint8_t *data, const uint32_t at, const float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    skin_word(data, at, bits);
}

// One identity root and palette, three influenced vertices, and one triangle.
// Written in the public portable format so the loader's validation is tested
// independently from the importer.
#define PACK_SKIN_BYTES 524u
static void pack_skin_blob(uint8_t data[PACK_SKIN_BYTES], const float height)
{
    memset(data, 0, PACK_SKIN_BYTES);
    skin_word(data, 0, TIDE_SKINNED_DATA_MAGIC);
    skin_word(data, 4, TIDE_SKINNED_DATA_VERSION);
    skin_word(data, 8, PACK_SKIN_BYTES);
    skin_word(data, 12, TIDE_SKINNED_HAS_NORMALS);
    const uint32_t tables[14] = {1u, 120u, 1u, 176u, 3u, 244u, 3u, 484u, 0u, 496u, 0u, 496u, 3u, 496u};
    for (uint32_t i = 0; i < 14u; i++) skin_word(data, 16u + i * 4u, tables[i]);
    skin_word(data, 72, 520u);
    skin_word(data, 76, 1u); // Empty names still have a NUL in the string table.
    skin_word(data, 80, PACK_SKIN_BYTES);
    skin_word(data, 120, UINT32_MAX);
    skin_word(data, 124, UINT32_MAX);
    skin_word(data, 128, 520u);
    skin_float(data, 160, 1.0f);
    for (uint32_t i = 0; i < 3; i++) skin_float(data, 164u + i * 4u, 1.0f);
    for (uint32_t i = 0; i < 4; i++) skin_float(data, 180u + i * 20u, 1.0f);
    for (uint32_t i = 0; i < 3; i++) {
        const uint32_t vertex = 244u + i * TIDE_SKINNED_VERTEX_SIZE;
        skin_float(data, vertex, i == 1u ? 1.0f : 0.0f);
        skin_float(data, vertex + 4u, i == 2u ? height : 0.0f);
        skin_float(data, vertex + 20u, 1.0f);
        skin_word(data, vertex + 72u, i);
        skin_word(data, vertex + 76u, 1u);
        skin_float(data, 500u + i * TIDE_SKINNED_INFLUENCE_SIZE, 1.0f);
    }
    skin_word(data, 484, 0u);
    skin_word(data, 488, 2u);
    skin_word(data, 492, 1u);
    TIDE_CHECK(tide_skinned_data_validate(data, PACK_SKIN_BYTES));
}

static uint32_t skinned_blocks[2][3], skinned_single, skinned_list, skinned_map, skinned_index;

// Keep all slots in range and the live count unchanged, but move the first
// key behind an empty slot so a lookup starting at its hash cannot find it.
static void misplace_skinned_index(uint8_t *bytes, const tide_pack_header *header)
{
    uint32_t *slots = (uint32_t *)(void *)(bytes + header->region_at + skinned_index + sizeof(tide_block));
    uint32_t first = UINT32_MAX, empty = UINT32_MAX;
    for (uint32_t i = 0; i < 8u; i++) {
        if (slots[i] == 1u) first = i;
        if (!slots[i]) empty = i;
    }
    TIDE_REQUIRE(first != UINT32_MAX && empty != UINT32_MAX);
    slots[first] = 0u;
    slots[empty] = 1u;
}

static uint8_t *build_skinned_pack(size_t *size)
{
    tide_pack_builder b = {0};
    for (uint32_t i = 0; i < 2; i++) {
        uint8_t data[PACK_SKIN_BYTES], h[32];
        const float height = (float)i + 1.0f;
        pack_skin_blob(data, height);
        tide_sha256_of(data, sizeof data, h);
        uint32_t chars;
        skinned_blocks[i][0] = tide_pack_bytes(&b, h, data, sizeof data, &chars);
        const corner vertices[3] = {{{0, 0, 0}, {0, 0, 1}, 0}, {{1, 0, 0}, {0, 0, 1}, 0}, {{0, height, 0}, {0, 0, 1}, 0}};
        tide_sha256_of(vertices, sizeof vertices, h);
        const uint32_t indices[3] = {0, 2, 1};
        tide_pack_mesh(&b, h, vertices, sizeof(corner), 3, indices, 3, &skinned_blocks[i][1]);
    }
    skinned_single = tide_pack_skinned_list(&b, skinned_blocks[0], 1);
    tide_pack_name(&b, "Game.actor", TIDE_PACK_SKINNED, skinned_single, 1, CORNER_LAYOUT);
    skinned_list = tide_pack_skinned_list(&b, &skinned_blocks[0][0], 2);
    tide_pack_name(&b, "Game.actors", TIDE_PACK_SKINNEDS, skinned_list, 2, CORNER_LAYOUT);
    uint32_t keys[2], chars;
    uint8_t h[32];
    hash_of("first", h);
    keys[0] = tide_pack_string(&b, h, "first", 5, &chars);
    hash_of("second", h);
    keys[1] = tide_pack_string(&b, h, "second", 6, &chars);
    skinned_map = tide_pack_skinned_map(&b, keys, &skinned_blocks[0][0], 2, &skinned_index);
    tide_pack_name(&b, "Game.namedActors", TIDE_PACK_SKINNED_MAP, skinned_map, skinned_index, CORNER_LAYOUT);
    return tide_pack_end(&b, size);
}

TIDE_TEST(pack_skinned_lists_maps_and_vertex_defaults)
{
    size_t size;
    uint8_t *bytes = build_skinned_pack(&size);
    TIDE_REQUIRE(tide_pack_load(bytes, size));
    const tide_pack_entry *e = tide_pack_find_mesh("Game.actor", TIDE_PACK_SKINNED, CORNER_LAYOUT);
    TIDE_REQUIRE(e != NULL);
    TIDE_CHECK(tide_pack_find_mesh("Game.actor", TIDE_PACK_SKINNED, CORNER_LAYOUT + 1u) == NULL);
    const tide_skinned_lists model = tide_pack_skinned_of(e, sizeof(corner));
    TIDE_CHECK(tide_list_count(model.data) == PACK_SKIN_BYTES && tide_list_count(model.vertices) == 3
               && tide_list_count(model.indices) == 3);
    TIDE_CHECK(!tide_pack_skinned_of(e, 12u).data.at && !tide_pack_skinned_of(NULL, sizeof(corner)).data.at);
    const tide_pack_entry *listed = tide_pack_find("Game.actors", TIDE_PACK_SKINNEDS);
    const tide_list models = {tide_pack_handle(listed)};
    TIDE_REQUIRE(tide_list_count(models) == 2);
    const tide_skinned_lists *first = tide_list_at(models, 0, sizeof *first), *second = tide_list_at(models, 1, sizeof *second);
    TIDE_CHECK(first->data.at == model.data.at && first->vertices.at == model.vertices.at && second->data.at != first->data.at);
    const tide_pack_entry *mapped = tide_pack_find("Game.namedActors", TIDE_PACK_SKINNED_MAP);
    const tide_map named = tide_pack_skinned_map_of(mapped);
    TIDE_CHECK(tide_map_count(named) == 2 && tide_map_entries(named) == 2 && named.last_order == 2 && named.reserved == 0);
    int64_t order = 0;
    int32_t next = 0;
    TIDE_CHECK(tide_map_next(named, &text_skinned_kind, &order, &next) == 0 && order == 1);
    TIDE_CHECK(tide_map_next(named, &text_skinned_kind, &order, &next) == 1 && order == 2);
    TIDE_CHECK(tide_map_next(named, &text_skinned_kind, &order, &next) == -1);
    const tide_str key = {"second", 6, 6};
    TIDE_REQUIRE(tide_map_find(named, &text_skinned_kind, &key) == 1);
    const skinned_prop_entry *entry = tide_map_entry(named, 1, &text_skinned_kind);
    TIDE_CHECK(entry->value.data.at == second->data.at && entry->value.indices.at == second->indices.at);
    tide_map copy = tide_map_copy(named, &text_skinned_kind);
    const tide_str third = tide_str_from_cstr("third");
    bool added;
    skinned_prop_entry *new_entry = tide_map_insert(&copy, &text_skinned_kind, &third, TIDE_IN_SCRATCH, &added);
    TIDE_REQUIRE(added && new_entry != NULL);
    new_entry->key = tide_text_temp(third);
    new_entry->value = model;
    TIDE_CHECK(copy.last_order == 3 && tide_map_next(copy, &text_skinned_kind, &order, &next) == 2 && order == 3);
    TIDE_CHECK(tide_map_next(copy, &text_skinned_kind, &order, &next) == -1);
    uint32_t count;
    corner *vertices = tide_pack_skinned_open(e, 0, sizeof(corner), &count);
    TIDE_REQUIRE(vertices && count == 3);
    for (uint32_t i = 0; i < count; i++) vertices[i].glow = 0.25f;
    TIDE_CHECK(((const corner *)tide_list_at(first->vertices, 2, sizeof(corner)))->glow == 0.25f);
    TIDE_CHECK(tide_pack_skinned_open(listed, 0, sizeof(corner), &count) == vertices && count == 3);
    TIDE_CHECK(tide_pack_skinned_open(mapped, 1, sizeof(corner), &count) && count == 3);
    TIDE_CHECK(tide_pack_skinned_open(e, 1, sizeof(corner), &count) == NULL && count == 0);
    TIDE_CHECK(tide_pack_skinned_open(e, 0, 12u, &count) != NULL && count == 0);
    TIDE_CHECK(tide_pack_skinned_open(listed, 2, sizeof(corner), &count) == NULL);
    TIDE_CHECK(tide_pack_skinned_open(mapped, 2, sizeof(corner), &count) == NULL);
    const tide_pack_entry copied = *e;
    TIDE_CHECK(tide_pack_skinned_open(&copied, 0, sizeof(corner), &count) == NULL);
    TIDE_CHECK(!tide_pack_skinned_map_of(NULL).entries.at);
    tide_pack_unload();
    tide_scratch_free();
    free(bytes);
}

TIDE_TEST(pack_skinned_verified_versions_follow_loaded_bytes_and_owned_copies)
{
    size_t size;
    uint8_t *bytes = build_skinned_pack(&size);
    TIDE_REQUIRE(tide_pack_load(bytes, size));
    const tide_skinned_lists model = tide_pack_skinned_of(tide_pack_find("Game.actor", TIDE_PACK_SKINNED), sizeof(corner));
    uint64_t original, version;
    TIDE_REQUIRE(tide_pack_skinned_version(model.data, &original));
    TIDE_CHECK(original == tide_hash(tide_list_at(model.data, 0, 1u), PACK_SKIN_BYTES));
    TIDE_CHECK(!tide_pack_skinned_version(model.vertices, &version));
    TIDE_CHECK(!tide_pack_skinned_version(model.indices, &version));
    TIDE_CHECK(!tide_pack_skinned_version((tide_list){model.data.at + 16u}, &version));
    TIDE_CHECK(!tide_pack_skinned_version((tide_list){0}, &version));
    TIDE_CHECK(!tide_pack_skinned_version(model.data, NULL));

    tide_list copy = tide_list_copy(model.data, 1u);
    tide_list_prepare(&copy, 1u, TIDE_IN_SCRATCH);
    TIDE_CHECK(!tide_pack_skinned_version(copy, &version));
    tide_list pose = {0};
    tide_draw_list draw = {0};
    uint32_t textures[2], copied[2], edited[2];
    int32_t info[3];
    TIDE_REQUIRE(tide_skinned_rest(model.data, &pose, TIDE_IN_SCRATCH));
    TIDE_REQUIRE(tide_draw_skinned_data(&draw, model.data, pose, textures, info));
    TIDE_REQUIRE(tide_draw_skinned_data(&draw, copy, pose, copied, info));
    TIDE_CHECK(textures[0] == copied[0] && textures[1] == copied[1]);
    uint8_t *owned = tide_list_at_mut(copy, 0, 1u);
    skin_float(owned, 500u, 2.0f);
    TIDE_REQUIRE(tide_draw_skinned_data(&draw, copy, pose, edited, info));
    TIDE_CHECK(edited[0] == textures[0] && edited[1] != textures[1]);
    const tide_draw_texture *captured = &draw.textures[textures[1] - 1u];
    const int32_t *words = (const int32_t *)(const void *)captured->pixels;
    TIDE_CHECK(tide_f_from_bits((uint32_t)words[13]) == 1.0f);
    skin_word(owned, 496u, UINT32_MAX);
    TIDE_CHECK(!tide_draw_skinned_data(&draw, copy, pose, edited, info));
    TIDE_CHECK(!edited[0] && !edited[1] && !info[0] && !info[1] && !info[2]);
    TIDE_CHECK(!tide_draw_skinned_data(&draw, model.data, (tide_list){0}, edited, info));

    tide_pack_header header;
    memcpy(&header, bytes, sizeof header);
    // The replacement reuses the same byte-list handle with different valid
    // content. Its registry must report the new content rather than the old.
    skin_float(bytes + header.region_at + skinned_blocks[0][0] + sizeof(tide_block), 500u, 3.0f);
    TIDE_REQUIRE(tide_pack_load(bytes, size));
    TIDE_REQUIRE(tide_pack_skinned_version(model.data, &version));
    TIDE_CHECK(version != original);
    TIDE_CHECK(version == tide_hash(tide_list_at(model.data, 0, 1u), PACK_SKIN_BYTES));
    TIDE_REQUIRE(tide_draw_skinned_data(&draw, model.data, pose, edited, info));
    TIDE_CHECK(edited[1] != textures[1]);
    // A structurally valid pack containing a malformed model clears all of
    // that model's records and must also clear the old registration.
    skin_word(bytes + header.region_at + skinned_blocks[0][0] + sizeof(tide_block), 120u, 0u);
    TIDE_REQUIRE(tide_pack_load(bytes, size));
    TIDE_CHECK(!tide_pack_skinned_version(model.data, &version));
    TIDE_CHECK(!tide_draw_skinned_data(&draw, model.data, pose, edited, info));
    tide_pack_unload();
    TIDE_CHECK(!tide_pack_skinned_version(model.data, &version));
    tide_draw_free(&draw);
    tide_scratch_free();
    free(bytes);
}

TIDE_TEST(pack_skinned_verified_versions_grow_and_exclude_ordinary_bytes)
{
    enum { MODELS = 24 };
    tide_pack_builder b = {0};
    uint32_t offsets[MODELS][3], raw = 0;
    uint64_t versions[MODELS];
    for (uint32_t i = 0; i <= MODELS; i++) {
        uint8_t data[PACK_SKIN_BYTES], h[32];
        pack_skin_blob(data, (float)i + 1.0f);
        tide_sha256_of(data, sizeof data, h);
        uint32_t chars;
        const uint32_t at = tide_pack_bytes(&b, h, data, sizeof data, &chars);
        if (i == MODELS) { raw = at; continue; }
        offsets[i][0] = at;
        versions[i] = tide_hash(data, sizeof data);
        const corner vertices[3] = {{{0, 0, 0}, {0, 0, 1}, 0}, {{1, 0, 0}, {0, 0, 1}, 0}, {{0, (float)i + 1.0f, 0}, {0, 0, 1}, 0}};
        tide_sha256_of(vertices, sizeof vertices, h);
        const uint32_t indices[3] = {0, 2, 1};
        tide_pack_mesh(&b, h, vertices, sizeof(corner), 3u, indices, 3u, &offsets[i][1]);
    }
    const uint32_t list = tide_pack_skinned_list(&b, &offsets[0][0], MODELS);
    tide_pack_name(&b, "Game.actors", TIDE_PACK_SKINNEDS, list, MODELS, CORNER_LAYOUT);
    tide_pack_name(&b, "Game.raw", TIDE_PACK_BYTES, raw, PACK_SKIN_BYTES, 0u);
    size_t size;
    uint8_t *bytes = tide_pack_end(&b, &size);
    TIDE_REQUIRE(tide_pack_load(bytes, size));
    const tide_list models = {tide_pack_handle(tide_pack_find("Game.actors", TIDE_PACK_SKINNEDS))};
    for (uint32_t i = 0; i < MODELS; i++) {
        const tide_skinned_lists *model = tide_list_at(models, (int32_t)i, sizeof *model);
        uint64_t version;
        TIDE_REQUIRE(model && tide_pack_skinned_version(model->data, &version));
        TIDE_CHECK(version == versions[i]);
    }
    const tide_list ordinary = {tide_pack_handle(tide_pack_find("Game.raw", TIDE_PACK_BYTES))};
    uint64_t version;
    TIDE_CHECK(tide_skinned_valid(ordinary));
    TIDE_CHECK(!tide_pack_skinned_version(ordinary, &version));
    tide_pack_unload();
    tide_scratch_free();
    free(bytes);
}

TIDE_TEST(pack_skinned_proof_identities_follow_loaded_blocks)
{
    size_t size;
    uint8_t *bytes = build_skinned_pack(&size);
    TIDE_REQUIRE(tide_pack_load(bytes, size));
    const tide_skinned_lists model = tide_pack_skinned_of(tide_pack_find("Game.actor", TIDE_PACK_SKINNED), sizeof(corner));
    const tide_list listed = {tide_pack_handle(tide_pack_find("Game.actors", TIDE_PACK_SKINNEDS))};
    const tide_skinned_lists *first = tide_list_at(listed, 0, sizeof *first);
    const tide_skinned_lists *second = tide_list_at(listed, 1, sizeof *second);
    TIDE_REQUIRE(first && second);
    uint64_t identity, version, same, other;
    TIDE_REQUIRE(tide_pack_skinned_proof(model.data, &identity, &version));
    TIDE_CHECK(identity != 0u);
    bool nonnegative = false;
    TIDE_REQUIRE(tide_pack_skinned_sample_proof(model.data, &nonnegative));
    TIDE_CHECK(nonnegative && tide_pack_skinned_sample_proof(model.data, NULL));
    TIDE_REQUIRE(tide_pack_skinned_proof(first->data, &same, NULL));
    TIDE_CHECK(same == identity);
    TIDE_REQUIRE(tide_pack_skinned_proof(second->data, &other, NULL));
    TIDE_CHECK(other != 0u && other != identity);
    TIDE_REQUIRE(tide_pack_skinned_proof(model.data, NULL, &same));
    TIDE_CHECK(same == version && tide_pack_skinned_proof(model.data, NULL, NULL));
    tide_list copy = tide_list_copy(model.data, 1u);
    tide_list_prepare(&copy, 1u, TIDE_IN_SCRATCH);
    same = identity;
    other = version;
    TIDE_CHECK(!tide_pack_skinned_proof(copy, &same, &other));
    TIDE_CHECK(same == identity && other == version);
    TIDE_CHECK(!tide_pack_skinned_sample_proof(copy, &nonnegative) && nonnegative);
    TIDE_CHECK(!tide_pack_skinned_proof((tide_list){0}, &same, &other));

    // A rejected load retains the current proof; equal successful loads must
    // replace it even when the numeric handle and canonical hash stay equal.
    TIDE_CHECK(!tide_pack_load(bytes, sizeof(tide_pack_header) - 1u));
    TIDE_REQUIRE(tide_pack_skinned_proof(model.data, &same, NULL));
    TIDE_CHECK(same == identity);
    nonnegative = false;
    TIDE_REQUIRE(tide_pack_skinned_sample_proof(model.data, &nonnegative));
    TIDE_CHECK(nonnegative);
    TIDE_REQUIRE(tide_pack_load(bytes, size));
    TIDE_REQUIRE(tide_pack_skinned_proof(model.data, &same, &other));
    TIDE_CHECK(same != 0u && same != identity && other == version);
    const uint64_t replacement = same;
    tide_pack_unload();
    TIDE_CHECK(!tide_pack_skinned_proof(model.data, &same, &other));
    TIDE_CHECK(same == replacement && other == version);
    TIDE_CHECK(!tide_pack_skinned_sample_proof(model.data, &nonnegative) && nonnegative);
    TIDE_REQUIRE(tide_pack_load(bytes, size));
    TIDE_REQUIRE(tide_pack_skinned_proof(model.data, &same, &other));
    TIDE_CHECK(same != 0u && same != replacement && same != identity && other == version);
    tide_pack_unload();
    tide_scratch_free();
    free(bytes);
}

TIDE_TEST(pack_skinned_verified_versions_invalidate_geometry_aliases)
{
    tide_pack_builder b = {0};
    uint8_t data[PACK_SKIN_BYTES], h[32], carrier[sizeof(tide_block) + PACK_SKIN_BYTES];
    pack_skin_blob(data, 1.0f);
    const tide_block inner = {.a = PACK_SKIN_BYTES, .b = PACK_SKIN_BYTES};
    memcpy(carrier, &inner, sizeof inner);
    memcpy(carrier + sizeof inner, data, sizeof data);
    tide_sha256_of(carrier, sizeof carrier, h);
    const uint32_t repeated[3] = {0u, 0u, 0u};
    uint32_t writable[2];
    tide_pack_mesh(&b, h, carrier, sizeof carrier, 1u, repeated, 3u, writable);
    tide_pack_name(&b, "Game.writable", TIDE_PACK_MESH, writable[0], writable[1], 1u);
    uint32_t offsets[2][3];
    for (uint32_t i = 0; i < 2u; i++) {
        pack_skin_blob(data, (float)i + 1.0f);
        tide_sha256_of(data, sizeof data, h);
        uint32_t chars;
        offsets[i][0] = i ? tide_pack_bytes(&b, h, data, sizeof data, &chars) : writable[0] + (uint32_t)sizeof(tide_block);
        const corner vertices[3] = {{{0, 0, 0}, {0, 0, 1}, 0}, {{1, 0, 0}, {0, 0, 1}, 0},
                                   {{0, (float)i + 1.0f, 0}, {0, 0, 1}, 0}};
        tide_sha256_of(vertices, sizeof vertices, h);
        const uint32_t indices[3] = {0u, 2u, 1u};
        tide_pack_mesh(&b, h, vertices, sizeof(corner), 3u, indices, 3u, &offsets[i][1]);
    }
    const uint32_t list = tide_pack_skinned_list(&b, &offsets[0][0], 2u);
    tide_pack_name(&b, "Game.actors", TIDE_PACK_SKINNEDS, list, 2u, CORNER_LAYOUT);
    size_t size;
    uint8_t *bytes = tide_pack_end(&b, &size);
    TIDE_REQUIRE(tide_pack_load(bytes, size));
    const tide_list models = {tide_pack_handle(tide_pack_find("Game.actors", TIDE_PACK_SKINNEDS))};
    const tide_skinned_lists *aliased = tide_list_at(models, 0, sizeof *aliased);
    const tide_skinned_lists *separate = tide_list_at(models, 1, sizeof *separate);
    uint64_t version, aliased_identity, separate_identity;
    TIDE_REQUIRE(aliased && separate && tide_pack_skinned_version(aliased->data, &version));
    TIDE_REQUIRE(tide_pack_skinned_version(separate->data, &version));
    TIDE_REQUIRE(tide_pack_skinned_proof(aliased->data, &aliased_identity, NULL));
    TIDE_REQUIRE(tide_pack_skinned_proof(separate->data, &separate_identity, NULL));
    tide_list pose = {0};
    tide_draw_list draw = {0};
    uint32_t textures[2], count;
    int32_t info[3];
    TIDE_REQUIRE(tide_skinned_rest(aliased->data, &pose, TIDE_IN_SCRATCH));
    TIDE_REQUIRE(tide_draw_skinned_data(&draw, aliased->data, pose, textures, info));
    uint8_t *to = tide_pack_mesh_open(tide_pack_find("Game.writable", TIDE_PACK_MESH), 0u, sizeof carrier, &count);
    TIDE_REQUIRE(to && count == 1u);
    skin_word(to + sizeof(tide_block), 496u, UINT32_MAX);
    tide_pack_mesh_updated(to, sizeof carrier);
    TIDE_CHECK(!tide_pack_skinned_version(aliased->data, &version));
    TIDE_CHECK(tide_pack_skinned_version(separate->data, &version));
    uint64_t current_identity = aliased_identity;
    TIDE_CHECK(!tide_pack_skinned_proof(aliased->data, &current_identity, NULL));
    TIDE_CHECK(current_identity == aliased_identity);
    TIDE_REQUIRE(tide_pack_skinned_proof(separate->data, &current_identity, NULL));
    TIDE_CHECK(current_identity == separate_identity);
    TIDE_CHECK(!tide_draw_skinned_data(&draw, aliased->data, pose, textures, info));
    TIDE_CHECK(!textures[0] && !textures[1] && !info[0] && !info[1] && !info[2]);
    // The aliased list header can change too. Check its actual packed span
    // before using its claimed size or any canonical table offsets.
    skin_word(to, offsetof(tide_block, a), INT32_MAX);
    skin_word(to, offsetof(tide_block, b), INT32_MAX);
    skin_word(to + sizeof(tide_block), 8u, INT32_MAX);
    skin_word(to + sizeof(tide_block), 20u, INT32_MAX - 119u);
    tide_pack_mesh_updated(to, sizeof carrier);
    TIDE_CHECK(!tide_skinned_valid(aliased->data));
    TIDE_CHECK(!tide_draw_skinned_data(&draw, aliased->data, pose, textures, info));
    tide_pack_header header;
    memcpy(&header, bytes, sizeof header);
    const tide_list end = {TIDE_IN_SCRATCH << 30 | (TIDE_PACK_BASE + header.region_size - 4u)};
    TIDE_CHECK(!tide_skinned_valid(end));
    tide_draw_free(&draw);
    tide_pack_unload();
    tide_scratch_free();
    free(bytes);
}

TIDE_TEST(pack_skinned_verified_versions_follow_load_sanitization)
{
    enum { BLOB_BYTES = 576 };
    for (uint32_t variant = 0; variant < 2u; variant++) {
        tide_pack_builder b = {0};
        uint8_t data[BLOB_BYTES] = {0}, h[32];
        pack_skin_blob(data, 1.0f);
        skin_word(data, 8u, sizeof data);
        const uint32_t nested = variant ? 304u : 528u;
        const tide_block inner = {.a = 1u, .b = 1u};
        memcpy(data + nested, &inner, sizeof inner);
        if (!variant) for (uint32_t i = 0; i < 3u; i++) skin_word(data, nested + (uint32_t)sizeof inner + i * 4u, UINT32_MAX);
        TIDE_REQUIRE(tide_skinned_data_validate(data, sizeof data));
        const uint64_t before = tide_hash(data, sizeof data);
        tide_sha256_of(data, sizeof data, h);
        uint32_t chars, offsets[3];
        offsets[0] = tide_pack_bytes(&b, h, data, sizeof data, &chars);
        const corner vertices[3] = {{{0, 0, 0}, {0, 0, 1}, 0}, {{1, 0, 0}, {0, 0, 1}, 0}, {{0, 1, 0}, {0, 0, 1}, 0}};
        tide_sha256_of(vertices, sizeof vertices, h);
        const uint32_t indices[3] = {0u, 2u, 1u};
        tide_pack_mesh(&b, h, vertices, sizeof(corner), 3u, indices, 3u, &offsets[1]);
        const uint32_t model = tide_pack_skinned_list(&b, offsets, 1u);
        tide_pack_name(&b, "Game.actor", TIDE_PACK_SKINNED, model, 1u, CORNER_LAYOUT);
        // The later invalid record aliases either unused bytes or a vertex's
        // influence count. Clearing it changes valid bytes or spoils the model
        // after the first record's complete validation.
        tide_pack_name(&b, "Game.bad", TIDE_PACK_SKINNED, offsets[0] + (uint32_t)sizeof(tide_block) + nested, 1u, CORNER_LAYOUT);
        size_t size;
        uint8_t *bytes = tide_pack_end(&b, &size);
        TIDE_REQUIRE(tide_pack_load(bytes, size));
        const tide_skinned_lists loaded = tide_pack_skinned_of(tide_pack_find("Game.actor", TIDE_PACK_SKINNED), sizeof(corner));
        uint64_t version;
        if (variant) {
            TIDE_CHECK(!tide_pack_skinned_version(loaded.data, &version));
            TIDE_CHECK(!tide_skinned_valid(loaded.data));
        } else {
            TIDE_REQUIRE(tide_pack_skinned_version(loaded.data, &version));
            TIDE_CHECK(version != before);
            TIDE_CHECK(version == tide_hash(tide_list_at(loaded.data, 0, 1u), sizeof data));
            TIDE_CHECK(tide_skinned_valid(loaded.data));
        }
        TIDE_CHECK(!tide_pack_skinned_of(tide_pack_find("Game.bad", TIDE_PACK_SKINNED), sizeof(corner)).data.at);
        tide_pack_unload();
        tide_scratch_free();
        free(bytes);
    }
}

TIDE_TEST(pack_skinned_spoiled_models_and_counts_clear_all_handles)
{
    size_t size;
    uint8_t *bytes = build_skinned_pack(&size);
    tide_pack_header header;
    memcpy(&header, bytes, sizeof header);
    uint8_t *region = bytes + header.region_at;
    const uint32_t parent = 0u;
    memcpy(region + skinned_blocks[0][0] + sizeof(tide_block) + 120u, &parent, sizeof parent);
    TIDE_REQUIRE(tide_pack_load(bytes, size));
    TIDE_CHECK(!tide_pack_skinned_of(tide_pack_find("Game.actor", TIDE_PACK_SKINNED), sizeof(corner)).data.at);
    const tide_list models = {tide_pack_handle(tide_pack_find("Game.actors", TIDE_PACK_SKINNEDS))};
    const tide_skinned_lists *first = tide_list_at(models, 0, sizeof *first), *second = tide_list_at(models, 1, sizeof *second);
    TIDE_REQUIRE(first && second);
    TIDE_CHECK(!first->data.at && !first->vertices.at && !first->indices.at && tide_list_count(second->vertices) == 3);
    const tide_map named = tide_pack_skinned_map_of(tide_pack_find("Game.namedActors", TIDE_PACK_SKINNED_MAP));
    const skinned_prop_entry *entry = tide_map_entry(named, 0, &text_skinned_kind);
    TIDE_CHECK(entry && !entry->value.data.at && !entry->value.vertices.at && !entry->value.indices.at);
    tide_pack_unload();
    free(bytes);

    bytes = build_skinned_pack(&size);
    memcpy(&header, bytes, sizeof header);
    region = bytes + header.region_at;
    tide_block vertices;
    memcpy(&vertices, region + skinned_blocks[0][1], sizeof vertices);
    vertices.a = vertices.b = 2u; // Whole geometry, wrong count for the influences.
    memcpy(region + skinned_blocks[0][1], &vertices, sizeof vertices);
    TIDE_REQUIRE(tide_pack_load(bytes, size));
    TIDE_CHECK(!tide_pack_skinned_of(tide_pack_find("Game.actor", TIDE_PACK_SKINNED), sizeof(corner)).vertices.at);
    tide_pack_unload();
    free(bytes);
}

TIDE_TEST(pack_skinned_spans_handles_indices_and_maps_are_checked)
{
    size_t size;
    uint8_t *bytes = build_skinned_pack(&size), *copy = malloc(size);
    TIDE_REQUIRE(copy != NULL);
    tide_pack_header header;
    memcpy(&header, bytes, sizeof header);
    uint32_t first_slot;
    memcpy(&first_slot, bytes + header.region_at + skinned_index + sizeof(tide_block), sizeof first_slot);
    const uint32_t changes[][2] = {
        {skinned_blocks[0][0] + offsetof(tide_block, a), UINT32_MAX}, // Byte count past the region.
        {skinned_blocks[0][1] + offsetof(tide_block, next), header.region_size}, // Vertex metadata outside the region.
        {skinned_blocks[0][2] + sizeof(tide_block), 2u}, // Valid vertex ID, different canonical index.
        {skinned_single + sizeof(tide_block), TIDE_IN_SCRATCH << 30 | (TIDE_PACK_BASE + header.region_size - 8u)},
        {skinned_single + sizeof(tide_block), TIDE_IN_MATCH << 30 | skinned_blocks[0][0]}, // A non-pack handle.
        {skinned_single + offsetof(tide_block, a), UINT32_MAX}, // Record block count exceeds its bytes.
        {skinned_index + sizeof(tide_block), 3u}, // Index points past the map's rows.
        {skinned_map + sizeof(tide_block) + offsetof(skinned_prop_entry, key), UINT32_MAX}, // Key points nowhere.
        {skinned_map + sizeof(tide_block), 0u}, // An insertion order must be positive and match source order.
        {skinned_map + sizeof(tide_block) + sizeof(uint32_t), UINT32_MAX}, // Invalid high bits of the signed order.
        {skinned_index + sizeof(tide_block), first_slot == 1u ? 2u : 1u}, // An in-range slot cannot duplicate or omit a row.
    };
    for (uint32_t i = 0; i < sizeof changes / sizeof changes[0]; i++) {
        memcpy(copy, bytes, size);
        memcpy(copy + header.region_at + changes[i][0], &changes[i][1], sizeof(uint32_t));
        TIDE_REQUIRE(tide_pack_load(copy, size));
        if (i < 6u) TIDE_CHECK(!tide_pack_skinned_of(tide_pack_find("Game.actor", TIDE_PACK_SKINNED), sizeof(corner)).data.at);
        else TIDE_CHECK(!tide_pack_skinned_map_of(tide_pack_find("Game.namedActors", TIDE_PACK_SKINNED_MAP)).entries.at);
        tide_pack_unload();
    }
    memcpy(copy, bytes, size);
    misplace_skinned_index(copy, &header);
    TIDE_REQUIRE(tide_pack_load(copy, size));
    TIDE_CHECK(!tide_pack_skinned_map_of(tide_pack_find("Game.namedActors", TIDE_PACK_SKINNED_MAP)).entries.at);
    tide_pack_unload();
    free(copy);
    free(bytes);
}

TIDE_TEST(pack_skinned_content_preflight_keeps_the_loaded_pack)
{
    size_t size;
    uint8_t *bytes = build_skinned_pack(&size);
    tide_pack_header header;
    memcpy(&header, bytes, sizeof header);
    uint8_t hashes[64], map_hashes[64];
    for (uint32_t i = 0; i < 2; i++) {
        const uint8_t *data = bytes + header.region_at + skinned_blocks[i][0] + sizeof(tide_block);
        tide_sha256_of(data, PACK_SKIN_BYTES, hashes + i * 32u);
        tide_sha256 content;
        tide_sha256_init(&content);
        tide_sha256_add(&content, i ? "second" : "first", i ? 6u : 5u);
        tide_sha256_add(&content, data, PACK_SKIN_BYTES);
        tide_sha256_end(&content, map_hashes + i * 32u);
    }
    TIDE_CHECK(tide_pack_skinned_content(bytes, size, "Game.actor", TIDE_PACK_SKINNED, CORNER_LAYOUT, hashes, 1));
    TIDE_CHECK(tide_pack_skinned_content(bytes, size, "Game.actors", TIDE_PACK_SKINNEDS, CORNER_LAYOUT, hashes, 2));
    TIDE_CHECK(tide_pack_skinned_content(bytes, size, "Game.namedActors", TIDE_PACK_SKINNED_MAP, CORNER_LAYOUT, map_hashes, 2));
    TIDE_CHECK(!tide_pack_skinned_content(bytes, size, "Game.actor", TIDE_PACK_SKINNED, CORNER_LAYOUT + 1u, hashes, 1));
    TIDE_CHECK(!tide_pack_skinned_content(bytes, size, "Game.actor", TIDE_PACK_SKINNEDS, CORNER_LAYOUT, hashes, 1));
    TIDE_CHECK(!tide_pack_skinned_content(bytes, size, "Game.actors", TIDE_PACK_SKINNEDS, CORNER_LAYOUT, hashes, 1));
    TIDE_CHECK(!tide_pack_skinned_content(bytes, size, "Game.unknown", TIDE_PACK_SKINNED, CORNER_LAYOUT, hashes, 1));
    TIDE_CHECK(!tide_pack_skinned_content(bytes, size, "Game.actor", TIDE_PACK_SKINNED, CORNER_LAYOUT, hashes + 32u, 1));
    TIDE_REQUIRE(tide_pack_load(bytes, size));
    const tide_skinned_lists original = tide_pack_skinned_of(tide_pack_find("Game.actor", TIDE_PACK_SKINNED), sizeof(corner));
    TIDE_REQUIRE(original.data.at != 0u);
    uint8_t *copy = malloc(size);
    TIDE_REQUIRE(copy != NULL);
    memcpy(copy, bytes, size);
    skin_float(copy + header.region_at + skinned_blocks[0][0] + sizeof(tide_block), 244u, 2.0f);
    TIDE_CHECK(!tide_pack_skinned_content(copy, size, "Game.actor", TIDE_PACK_SKINNED, CORNER_LAYOUT, hashes, 1));
    const tide_skinned_lists kept = tide_pack_skinned_of(tide_pack_find("Game.actor", TIDE_PACK_SKINNED), sizeof(corner));
    TIDE_CHECK(kept.data.at == original.data.at && kept.vertices.at == original.vertices.at && tide_list_count(kept.vertices) == 3);
    memcpy(copy, bytes, size);
    uint32_t key;
    memcpy(&key, copy + header.region_at + skinned_map + sizeof(tide_block) + offsetof(skinned_prop_entry, key), sizeof key);
    const uint32_t key_at = (key & 0x3fffffffu) - TIDE_PACK_BASE;
    memcpy(copy + header.region_at + key_at + sizeof(tide_block), "third", 5u);
    TIDE_CHECK(!tide_pack_skinned_content(copy, size, "Game.namedActors", TIDE_PACK_SKINNED_MAP, CORNER_LAYOUT, map_hashes, 2));
    memcpy(copy, bytes, size);
    misplace_skinned_index(copy, &header);
    TIDE_CHECK(!tide_pack_skinned_content(copy, size, "Game.namedActors", TIDE_PACK_SKINNED_MAP, CORNER_LAYOUT, map_hashes, 2));
    for (size_t n = 0; n < size; n++) {
        uint8_t *cut = malloc(n ? n : 1u);
        TIDE_REQUIRE(cut != NULL);
        if (n) memcpy(cut, bytes, n);
        TIDE_CHECK(!tide_pack_skinned_content(cut, n, "Game.actors", TIDE_PACK_SKINNEDS, CORNER_LAYOUT, hashes, 2));
        free(cut);
    }
    // An entry, record or byte handle cannot redirect the hash read outside
    // the supplied region, even when the rest of the pack is whole.
    uint32_t first_slot;
    memcpy(&first_slot, bytes + header.region_at + skinned_index + sizeof(tide_block), sizeof first_slot);
    const uint32_t changes[][2] = {
        {header.region_at + skinned_list + offsetof(tide_block, a), UINT32_MAX},
        {header.region_at + skinned_list + sizeof(tide_block), UINT32_MAX},
        {header.region_at + skinned_blocks[0][0] + offsetof(tide_block, a), UINT32_MAX},
        {header.region_at + skinned_index + sizeof(tide_block), 3u},
        {header.region_at + skinned_map + sizeof(tide_block), 0u},
        {header.region_at + skinned_map + sizeof(tide_block) + sizeof(uint32_t), UINT32_MAX},
        {header.region_at + skinned_index + sizeof(tide_block), first_slot == 1u ? 2u : 1u},
    };
    for (uint32_t i = 0; i < sizeof changes / sizeof changes[0]; i++) {
        memcpy(copy, bytes, size);
        memcpy(copy + changes[i][0], &changes[i][1], sizeof(uint32_t));
        const bool map = i >= 3u;
        TIDE_CHECK(!tide_pack_skinned_content(copy, size, map ? "Game.namedActors" : "Game.actors",
                                             map ? TIDE_PACK_SKINNED_MAP : TIDE_PACK_SKINNEDS, CORNER_LAYOUT, map ? map_hashes : hashes, 2));
    }
    tide_pack_unload();
    free(copy);
    free(bytes);
}

static uint32_t leveled_blocks[2], unleveled_blocks[2];

static uint8_t *build_levels(size_t *size, const bool lead)
{
    tide_pack_builder b = {0};
    uint8_t h[32];
    uint32_t chars;
    if (lead) {
        hash_of("lead", h);
        tide_pack_name(&b, "Game.lead", TIDE_PACK_TEXT, tide_pack_bytes(&b, h, "moved along", 11, &chars), 11, chars);
    }
    const corner tree[4] = {{{0, 0, 0}, {0, 0, 1}, 0}, {{1, 0, 0}, {0, 0, 1}, 0}, {{1, 1, 0}, {0, 0, 1}, 0}, {{0, 1, 0}, {0, 0, 1}, 0}};
    static const uint32_t whole[12] = {0, 2, 1, 0, 3, 2, 0, 3, 1, 1, 3, 2};
    static const uint32_t simpler[9] = {0, 2, 1, 0, 3, 2, 0, 2, 1};
    const tide_mesh_levels levels = {.center = {0.5f, 0.5f, 0.0f}, .radius = 0.75f, .count = 2, .error = {0.01f, 0.25f},
                                     .first = {0, 6}, .indices = {6, 3}, .index = simpler, .index_count = 9};
    hash_of("tree.glb with levels", h);
    tide_pack_mesh_levels(&b, h, tree, sizeof(corner), 4, whole, 12, &levels, leveled_blocks);
    hash_of("tree.glb", h);
    tide_pack_mesh_levels(&b, h, tree, sizeof(corner), 4, whole, 12, NULL, unleveled_blocks);
    tide_pack_name(&b, "Game.tree", TIDE_PACK_MESH, leveled_blocks[0], leveled_blocks[1], CORNER_LAYOUT);
    tide_pack_name(&b, "Game.plain", TIDE_PACK_MESH, unleveled_blocks[0], unleveled_blocks[1], CORNER_LAYOUT);
    const tide_vertex3 flat[4] = {{.position = {0, 0, 0}}, {.position = {1, 0, 0}}, {.position = {1, 1, 0}}, {.position = {0, 1, 0}}};
    uint32_t flat_blocks[2];
    hash_of("flat.glb with levels", h);
    tide_pack_mesh_levels(&b, h, flat, sizeof(tide_vertex3), 4, whole, 12, &levels, flat_blocks);
    tide_pack_name(&b, "Game.flat", TIDE_PACK_MESH, flat_blocks[0], flat_blocks[1], 3u);
    return tide_pack_end(&b, size);
}

TIDE_TEST(pack_meshes_have_levels_of_detail_beside_their_lists)
{
    for (int lead = 0; lead < 2; lead++) {
        size_t size;
        uint8_t *bytes = build_levels(&size, lead != 0);
        TIDE_REQUIRE(tide_pack_load(bytes, size));
        const tide_mesh_lists tree = tide_pack_mesh_of(tide_pack_find("Game.tree", TIDE_PACK_MESH), sizeof(corner));
        const tide_mesh_lists plain = tide_pack_mesh_of(tide_pack_find("Game.plain", TIDE_PACK_MESH), sizeof(corner));
        TIDE_REQUIRE(tide_list_count(tree.vertices) == 4 && tide_list_count(tree.indices) == 12 && tide_list_count(plain.indices) == 12);
        TIDE_CHECK(*(const uint32_t *)tide_list_at(tree.indices, 11, sizeof(uint32_t)) == 2u);
        TIDE_CHECK(tree.indices.at != plain.indices.at);
        tide_mesh_levels found = {0};
        TIDE_REQUIRE(tide_pack_list_levels(tree.indices, &found));
        TIDE_CHECK(found.count == 2 && found.index_count == 9 && found.radius == 0.75f && found.center[1] == 0.5f);
        TIDE_CHECK(found.error[0] == 0.01f && found.error[1] == 0.25f);
        TIDE_CHECK(found.first[0] == 0 && found.indices[0] == 6 && found.first[1] == 6 && found.indices[1] == 3);
        TIDE_REQUIRE(found.index != NULL);
        TIDE_CHECK(found.index[0] == 0 && found.index[1] == 2 && found.index[8] == 1);
        tide_mesh_levels none = {.count = 9};
        TIDE_CHECK(!tide_pack_list_levels(plain.indices, &none) && none.count == 9);
        TIDE_CHECK(!tide_pack_list_levels(tree.vertices, &none) && !tide_pack_list_levels((tide_list){0}, &none));
        tide_draw_list d = {0};
        tide_draw_reset(&d);
        tide_draw_set_lod(&d, 2);
        const tide_mesh_lists flat = tide_pack_mesh_of(tide_pack_find("Game.flat", TIDE_PACK_MESH), sizeof(tide_vertex3));
        tide_draw_mesh3_lists(&d, flat.vertices, flat.indices, tide_identity_f4x4());
        TIDE_REQUIRE(d.count >= 1 && d.commands[d.count - 1u].kind == TIDE_DRAW_MESH_3D);
        const tide_draw_mesh_data *drawn = &d.meshes[d.commands[d.count - 1u].mesh3.mesh - 1u];
        TIDE_CHECK(drawn->level == 2 && drawn->index_count == 3 && drawn->base != 0);
        tide_draw_free(&d);
        tide_pack_unload();
        TIDE_CHECK(!tide_pack_list_levels(tree.indices, &none));
        free(bytes);
    }
}

TIDE_TEST(pack_levels_of_detail_spoiled_are_none)
{
    size_t size;
    uint8_t *bytes = build_levels(&size, true);
    tide_pack_header header;
    memcpy(&header, bytes, sizeof header);
    uint8_t *region = bytes + header.region_at;
    // Where the levels are: the word after the indices' element size, less 1
    tide_block faces;
    memcpy(&faces, region + leveled_blocks[1], sizeof faces);
    uint8_t *names = region + leveled_blocks[1] + faces.next + sizeof(uint64_t) + sizeof(uint32_t);
    uint32_t named;
    memcpy(&named, names, sizeof named);
    TIDE_REQUIRE(named != 0 && (named - 1u) % 4u == 0);
    uint32_t *words = (uint32_t *)(void *)(region + (named - 1u) + sizeof(tide_block));
    uint32_t kept[28];
    memcpy(kept, words, sizeof kept);
    const struct {
        uint32_t word, value;
    } spoils[] = {
        {4, 0}, {4, 8}, {4, UINT32_MAX},        // How many levels
        {5, 1000000u}, {5, UINT32_MAX},         // How many indices
        {7, 10}, {8, 12}, {8, 5}, {11, 7},      // A level past the indices, or not whole triangles
        {6, 0x7FC00000u}, {9, 0}, {9, 0xFF800000u}, // An error that's no number, or under the level before's
        {3, 0x7FC00000u}, {3, 0xBF800000u}, {1, 0x7F800000u}, // A ball that's none
    };
    tide_mesh_levels found;
    for (size_t i = 0; i < sizeof spoils / sizeof spoils[0]; i++) {
        words[spoils[i].word] = spoils[i].value;
        TIDE_REQUIRE(tide_pack_load(bytes, size));
        const tide_mesh_lists tree = tide_pack_mesh_of(tide_pack_find("Game.tree", TIDE_PACK_MESH), sizeof(corner));
        TIDE_CHECK(tide_list_count(tree.indices) == 12);
        TIDE_CHECK(!tide_pack_list_levels(tree.indices, &found));
        memcpy(words, kept, sizeof kept);
    }
    const uint32_t places[] = {header.region_size + 1u, header.region_size - 3u, named + 2u, UINT32_MAX};
    for (size_t i = 0; i < sizeof places / sizeof places[0]; i++) {
        memcpy(names, &places[i], sizeof named);
        TIDE_REQUIRE(tide_pack_load(bytes, size));
        const tide_mesh_lists tree = tide_pack_mesh_of(tide_pack_find("Game.tree", TIDE_PACK_MESH), sizeof(corner));
        TIDE_CHECK(!tide_pack_list_levels(tree.indices, &found));
    }
    memcpy(names, &named, sizeof named);
    TIDE_REQUIRE(tide_pack_load(bytes, size));
    const tide_mesh_lists tree = tide_pack_mesh_of(tide_pack_find("Game.tree", TIDE_PACK_MESH), sizeof(corner));
    TIDE_CHECK(tide_pack_list_levels(tree.indices, &found) && found.count == 2);
    tide_pack_unload();
    free(bytes);
}
