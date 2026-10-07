#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tide/color.h"
#include "tide/draw.h"
#include "tide/grid.h"
#include "tide/list.h"
#include "tide/map.h"
#include "tide/text.h"

// The pack: a game's assets as the values `asset` declarations resolve to,
// built once (by tidec, or by tide under tide run) and loaded as the game
// starts. Text, bytes, grids of colors, images to draw with, and lists and
// maps of those, laid out as the heap lays them out, in one read-only region
// every world can name (see docs/spec.md, Assets).
//
// Where the region is: the top of the scratch area's offsets, from
// TIDE_PACK_BASE on (tide/text.h), so a handle into it is a scratch handle
// (TIDE_IN_SCRATCH) that reads through tide_scratch_at like any other, with
// no code of its own on the paths that read text, lists and maps; grids read
// their chunks the same way where their handle says the scratch area (see
// tide_grid_chunk_at). No world holds the region, so nothing copies, hashes
// or sends it, and the checker refuses every write, so the runtime never sees
// one. Each thread maps the region into its slots the first time it runs
// code after a load (tide_pack_use, which generated code calls where it sets
// the thread's heaps).
//
// The file: a header, a table of the declarations by name (a SHA-256 of the
// qualified name, so a build and a pack of different times still meet, and
// a stale one is found by its kind), and the region.

#define TIDE_PACK_MAGIC "TIDEPACK"
#define TIDE_PACK_VERSION 1u

// What a name's entry is: the kind the declaration's type resolves to.
enum {
    TIDE_PACK_TEXT = 1,  // string: a block of bytes, with a NUL after them; `extra` is its characters
    TIDE_PACK_BYTES = 2, // List<byte>: the same block, read as a list
    TIDE_PACK_GRID = 3,  // Grid2<Color>: a directory and its chunks
    TIDE_PACK_LIST = 4,  // List<T> of a glob: a block of handles; `extra` is the elements' kind
    TIDE_PACK_MAP = 5,   // Map<string, T> of a glob: its entries and its index (tide_map); `extra` is its live count
    // Texture: an image to draw with, a byte a channel (tide_pack_texture). No code reads its pixels: only the
    // draw list does, so it's a quarter of a grid's memory. A list's elements' kind too (`extra`)
    TIDE_PACK_TEXTURE = 6,
    TIDE_PACK_TEXTURES = 7, // Map<string, Texture> of a glob: as TIDE_PACK_MAP, its values' blocks textures
    // Cubemap: a block of six handles, as a list's, each a texture's block (tide_pack_texture): its faces' images
    // in the order +x, -x, +y, -y, +z, -z, all squares of one size. A face's file is in the pack once, as any is
    TIDE_PACK_CUBEMAP = 8,
};

typedef struct tide_pack_entry {
    uint8_t name[32];  // SHA-256 of the declaration's qualified name
    uint32_t kind;     // TIDE_PACK_*
    uint32_t offset;   // Its block in the region (a map's: its entries' block)
    uint32_t size;     // The block's bytes after its header (a map's: its index block's offset)
    uint32_t extra;    // By kind, above (a map's: its live count)
} tide_pack_entry;

// The file's header, before the entries and the region.
typedef struct tide_pack_header {
    char magic[8];
    uint32_t version;
    uint32_t entries;
    uint32_t region_at; // Where the region starts in the file
    uint32_t region_size;
    uint32_t reserved[2];
} tide_pack_header;

// ---------------------------------------------------------------------------
// Building one

typedef struct tide_pack_shared {
    uint8_t content[32];
    uint32_t kind, offset, size, extra;
} tide_pack_shared;

typedef struct tide_pack_builder {
    uint8_t *region;
    uint32_t size, room;
    tide_pack_shared *shared; // Blocks by their content's hash: a file in the pack once however many names it
    uint32_t shared_count, shared_room;
    tide_pack_entry *entries;
    uint32_t entry_count, entry_room;
} tide_pack_builder;

// A block of `size` bytes with a NUL after them, shared by `content` (its
// SHA-256): its offset. *chars gets how many UTF-8 characters they are.
uint32_t tide_pack_bytes(tide_pack_builder *b, const uint8_t content[32], const void *data, uint32_t size, uint32_t *chars);

// A grid of `width` by `height` colors, as a Grid2<Color> of `shape` is laid
// out (its directory, then the chunks that have a color in them), shared by
// `content`: its directory's offset. `version` is what the draw list tells
// the grid by (tide_draw_grid_texture): the content's first bytes do.
uint32_t tide_pack_grid(tide_pack_builder *b, const uint8_t content[32], int32_t width, int32_t height, const tide_color *pixels,
                        const tide_grid_shape *shape);

// An image of `width` by `height` pixels to draw with: red, green, blue and
// alpha, a byte each, rows from the top, shared by `content`. Its block's
// offset: the version the draw list tells it by in its header (the content's
// first bytes, never 0), then its width and its height, then its pixels.
uint32_t tide_pack_texture(tide_pack_builder *b, const uint8_t content[32], int32_t width, int32_t height, const uint8_t *rgba);

// A list of `count` handles (offsets of blocks in this pack, which it tags):
// its block's offset. Not shared: a declaration's own.
uint32_t tide_pack_list(tide_pack_builder *b, const uint32_t *offsets, uint32_t count);

// A map from text to values: `keys` are offsets of byte blocks, `values` of
// the values' blocks. Its entries' block, with its index's block in *index.
// The keys are hashed as tide_map_hash_bytes does, so the game's own lookups
// find them.
uint32_t tide_pack_map(tide_pack_builder *b, const uint32_t *keys, const uint32_t *values, uint32_t count, uint32_t *index);

// Names a block: the declaration `qualified` (like "Game.hero") of `kind` is
// the block at `offset`.
void tide_pack_name(tide_pack_builder *b, const char *qualified, uint32_t kind, uint32_t offset, uint32_t size, uint32_t extra);

// The pack as a file's bytes (to free()), and its size. The builder is freed.
uint8_t *tide_pack_end(tide_pack_builder *b, size_t *size);

// ---------------------------------------------------------------------------
// Loading one

// Loads a pack: its region into memory of its own, which handles then reach,
// in place of the one loaded before. False for bytes that aren't a pack, with
// the one before still loaded. The bytes themselves aren't kept.
bool tide_pack_load(const uint8_t *bytes, size_t size);

// Lets go of the loaded pack: before a game's library is unloaded.
void tide_pack_unload(void);

// The entry a declaration's name has in the loaded pack, of `kind`, or NULL:
// for a pack from another build of the game, or none loaded.
const tide_pack_entry *tide_pack_find(const char *qualified, uint32_t kind);

// What an entry resolves to, as the value the game reads: a handle (a tagged
// offset, for a grid's or a list's `at`, or a text's), a tide_str, or a map.
uint32_t tide_pack_handle(const tide_pack_entry *e);
tide_str tide_pack_text(const tide_pack_entry *e);
tide_map tide_pack_map_of(const tide_pack_entry *e);

// A texture's entry as the value the game reads: a Texture of the pack's,
// which has no pixels of its own (`image.pixels` is NULL) and names its image
// by its declaration (`image.version`: a number made of the declaration's
// name, and for one of a glob's, of its place in the list or its key in the
// map), with the size it has in this pack. So a copy of one kept through
// another load (tide run packs again when a file changes) is never a place
// in a pack that's gone, and goes on naming what the declaration does: the
// file as it is now. tide_draw_texture_source looks the image up in the pack
// that's loaded then. The default Texture for no entry.
//
// A list or a map of textures is a block of handles in the file, like one of
// grids, as a file is the same on every platform. Tide reads their elements
// as these values, whose layout is the platform's, so loading lays them out
// after the file's region, and the entries name those.
tide_texture_ref tide_pack_texture_of(const tide_pack_entry *e);

// The loaded pack's image that a Texture names (`named`: its
// `image.version`), as pixels: width * height * 4 bytes, rows from the top,
// and a version that's what's in them (the file's hash). False when the
// pack has no image under that name. The pixels stay where they are until
// another pack is loaded or this one is let go, and a draw list draws with
// them there (TIDE_PIXELS_PACK), with no copy of its own: a list that drew
// with one isn't rendered after its pack went.
bool tide_pack_texture_find(uint64_t named, tide_texture *image);
// A cubemap's entry as the value the game reads: a Cubemap of the pack's,
// which names its declaration as a Texture of the pack's does. One for no
// entry is no cube.
tide_cubemap_ref tide_pack_cubemap_of(const tide_pack_entry *e);
// The loaded pack's cube that a Cubemap names: its faces' side in pixels, a
// version that's what's in the six (their files' hashes), and six pointers
// to their pixels, 4 bytes each, rows from the top, in the order +x, -x, +y,
// -y, +z, -z. False when the pack has no cube under that name, or one whose
// faces aren't six squares of one size. The pointers and the pixels stay
// where they are until another pack is loaded, as an image's do.
bool tide_pack_cubemap_find(uint64_t named, int32_t *size, uint64_t *version, const uint8_t *const **faces);

// Maps the loaded region into this thread's scratch slots, if it hasn't since
// the last load: generated code calls it wherever it sets the thread's heaps
// (tide_text_use), so every thread that runs Tide code reads the pack.
void tide_pack_use(void);

// The version of a grid of the pack, for the draw list: from its directory's
// block header, where the builder put it.
uint64_t tide_pack_grid_version(tide_grid g);

// `bytes` bytes of the loaded region at a scratch offset (a handle's low 30
// bits), where they're all in it: NULL otherwise, as for a handle kept from a
// pack that's gone.
const uint8_t *tide_pack_span(uint32_t offset, uint64_t bytes);

// Has every texture of a list's or a map's entry sampled as `sampling` says
// (tide_sampling_bits): what the asset's declaration says of them, which
// generated code gives after each load.
void tide_pack_sample_textures(const tide_pack_entry *e, uint32_t sampling);

// How many scratch slots the loaded region takes, from TIDE_PACK_BASE: 0 for
// none. The scratch area stops short of them (text.c).
extern uint32_t tide_pack_region_slots;
