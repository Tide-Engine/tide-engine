// The pack (tide/pack.h): built, loaded, and read back through the code that
// reads a world's text, lists, maps and grids, with nothing of its own.

#include <stddef.h>
#include <stdio.h>
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

// ---------------------------------------------------------------------------
// Images to draw with

// A map from text to textures, as generated code describes one.
typedef struct icon_entry {
    int32_t live;
    tide_text key;
    tide_texture_ref value;
} icon_entry;

static const tide_map_kind text_texture_kind = {sizeof(icon_entry), offsetof(icon_entry, key), hash_key, hash_probe, equal_key};

// An image's bytes, each from its place and the image's own number.
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
    TIDE_CHECK(tide_pack_texture(&b, h, 6, 4, first) == hero); // The same file once
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

// The image a Texture names, as the loaded pack has it: its pixels, or NULL.
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

    // A Texture holds no pixels: it names its declaration's image, which the pack has
    const tide_texture_ref hero = tide_pack_texture_of(tide_pack_find("Game.hero", TIDE_PACK_TEXTURE));
    TIDE_CHECK(!hero.image.pixels && !hero.target.id && hero.image.width == 6 && hero.image.height == 4 && hero.image.version);
    const tide_texture_ref none = tide_pack_texture_of(NULL);
    TIDE_CHECK(!none.image.version && !none.image.width && !none.image.pixels);
    TIDE_CHECK(!tide_pack_texture_of(tide_pack_find("Game.frames", TIDE_PACK_LIST)).image.version); // Another kind's entry
    TIDE_CHECK(tide_pack_find("Game.hero", TIDE_PACK_GRID) == NULL);

    int32_t width, height;
    uint64_t version;
    const uint8_t *pixels = pixels_of(hero, &width, &height, &version);
    TIDE_REQUIRE(pixels != NULL);
    TIDE_CHECK(width == 6 && height == 4 && memcmp(pixels, first, sizeof first) == 0);
    TIDE_CHECK(version == version_of("hero.png")); // What's in it: the file's hash
    tide_texture image;
    TIDE_CHECK(!tide_pack_texture_find(version, &image)); // ...which names no declaration
    TIDE_CHECK(!tide_pack_texture_find(0, &image));

    // The draw list draws with them where they are: no copy of its own
    static tide_draw_list draw;
    tide_draw_reset(&draw);
    const uint32_t slot = tide_draw_texture_source(&draw, hero);
    TIDE_REQUIRE(slot != 0);
    const tide_draw_texture *t = &draw.textures[slot - 1];
    TIDE_CHECK(t->space == TIDE_PIXELS_PACK && t->width == 6 && t->height == 4 && t->version == version && t->id == version);
    TIDE_CHECK(t->pixels == pixels && t->capacity == 0);
    TIDE_CHECK(tide_draw_texture_source(&draw, hero) == slot && draw.texture_count == 1);
    TIDE_CHECK(tide_draw_texture_source(&draw, none) == 0);

    // A list of them: Textures side by side, which loading lays out, each named by its place
    const tide_list frames = {tide_pack_handle(tide_pack_find("Game.frames", TIDE_PACK_LIST))};
    TIDE_CHECK(tide_pack_holds(frames.at) && tide_list_count(frames) == 3);
    const tide_texture_ref *frame0 = tide_list_at(frames, 0, sizeof(tide_texture_ref));
    const tide_texture_ref *frame1 = tide_list_at(frames, 1, sizeof(tide_texture_ref));
    const tide_texture_ref *frame2 = tide_list_at(frames, 2, sizeof(tide_texture_ref));
    TIDE_REQUIRE(frame0 && frame1 && frame2);
    TIDE_CHECK(frame0->image.version && frame0->image.version != hero.image.version && frame0->image.version != frame2->image.version);
    TIDE_CHECK(pixels_of(*frame0, NULL, NULL, NULL) == pixels && pixels_of(*frame2, NULL, NULL, NULL) == pixels); // One image
    TIDE_CHECK(frame1->image.width == 3 && frame1->image.height == 5 && !frame1->image.pixels);
    TIDE_CHECK(tide_list_at(frames, 3, sizeof(tide_texture_ref)) == NULL);
    const tide_texture_ref other = *frame1;
    const tide_texture_ref first_frame = *frame0; // Kept: the list itself goes with its pack
    // Two names of one file are one texture of the list's, and another file is another
    TIDE_CHECK(tide_draw_texture_source(&draw, *frame0) == slot && tide_draw_texture_source(&draw, *frame2) == slot);
    const uint32_t other_slot = tide_draw_texture_source(&draw, other);
    TIDE_REQUIRE(other_slot != 0 && other_slot != slot);
    TIDE_CHECK(draw.textures[other_slot - 1].version == version_of("other.png"));
    TIDE_CHECK(memcmp(draw.textures[other_slot - 1].pixels, second, sizeof second) == 0);

    // A map of them, found as the game's lookups find keys, each named by its key
    const tide_map icons = tide_pack_map_of(tide_pack_find("Game.icons", TIDE_PACK_TEXTURES));
    TIDE_CHECK(tide_map_count(icons) == 2 && tide_map_entries(icons) == 2);
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

    // Many of them: each found by its name
    const tide_list small = {tide_pack_handle(tide_pack_find("Game.small", TIDE_PACK_LIST))};
    TIDE_CHECK(tide_list_count(small) == MANY);
    bool all = true;
    for (int i = 0; i < MANY; i++) {
        const tide_texture_ref *one = tide_list_at(small, i, sizeof(tide_texture_ref));
        const uint8_t *pixel = one ? pixels_of(*one, NULL, NULL, NULL) : NULL;
        all = all && pixel && pixel[0] == (uint8_t)i && pixel[1] == (uint8_t)(i * 2) && pixel[3] == 255;
    }
    TIDE_CHECK(all);

    // A list added after another keeps drawing with the pack's own pixels
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

    // Frames that draw without them let them go, and free nothing of the pack's
    tide_draw_reset(&draw);
    tide_draw_reset(&draw);
    TIDE_CHECK(draw.texture_count == 0);
    TIDE_CHECK(memcmp(pixels_of(hero, NULL, NULL, NULL), first, sizeof first) == 0);
    tide_draw_forget(&under);
    tide_draw_free(&over);
    tide_draw_free(&under);

    // With the pack gone, a Texture kept from it is no texture, as the default one is
    tide_pack_unload();
    TIDE_CHECK(tide_draw_texture_source(&draw, hero) == 0 && draw.texture_count == 0);
    TIDE_CHECK(pixels_of(hero, NULL, NULL, NULL) == NULL);
    free(bytes);

    // The pack again, with the image elsewhere in it: the Texture kept from before names it still
    bytes = build_textures(&size, true, 1);
    TIDE_REQUIRE(tide_pack_load(bytes, size));
    const tide_texture_ref again = tide_pack_texture_of(tide_pack_find("Game.hero", TIDE_PACK_TEXTURE));
    TIDE_CHECK(memcmp(&again, &hero, sizeof hero) == 0);
    tide_draw_reset(&draw);
    const uint32_t found_again = tide_draw_texture_source(&draw, hero);
    TIDE_REQUIRE(found_again != 0);
    TIDE_CHECK(memcmp(draw.textures[found_again - 1].pixels, first, sizeof first) == 0);
    TIDE_CHECK(draw.textures[found_again - 1].version == version);
    free(bytes);

    // ...and with the file edited, as tide run packs again when one is saved: the Texture kept from before is
    // the declaration's image as it is now, and to the list it's another texture
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
    // One of a list is its place's image as it is now
    TIDE_CHECK(tide_draw_texture_source(&draw, first_frame) == edited_slot);
    free(bytes);

    // A pack with no such declaration: no texture
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

// A pack whose texture blocks aren't whole reads as the default Texture, and
// its lists of them as far as they're whole: a file is only bytes.
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

    // Its size says more pixels than the region has
    const int32_t huge[2] = {30000, 30000};
    memcpy(bytes + header.region_at + at + sizeof(tide_block), huge, sizeof huge);
    TIDE_REQUIRE(tide_pack_load(bytes, size));
    TIDE_CHECK(!tide_pack_texture_of(tide_pack_find("Game.cut", TIDE_PACK_TEXTURE)).image.version);
    const tide_list list = {tide_pack_handle(tide_pack_find("Game.list", TIDE_PACK_LIST))};
    TIDE_CHECK(tide_list_count(list) == 2);
    const tide_texture_ref *one = tide_list_at(list, 1, sizeof(tide_texture_ref));
    TIDE_CHECK(one && !one->image.version && !one->image.width);

    // ...and a size under 1
    const int32_t flat[2] = {0, 2};
    memcpy(bytes + header.region_at + at + sizeof(tide_block), flat, sizeof flat);
    TIDE_REQUIRE(tide_pack_load(bytes, size));
    TIDE_CHECK(!tide_pack_texture_of(tide_pack_find("Game.cut", TIDE_PACK_TEXTURE)).image.version);
    tide_pack_unload();
    free(bytes);
}
