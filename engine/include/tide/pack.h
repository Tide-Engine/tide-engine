#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tide/color.h"
#include "tide/audio.h"
#include "tide/draw.h"
#include "tide/grid.h"
#include "tide/list.h"
#include "tide/map.h"
#include "tide/mesh.h"
#include "tide/text.h"
#include "tide/texture_blocks.h"

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
#define TIDE_PACK_VERSION 2u

// What a name's entry is: the kind the declaration's type resolves to.
enum {
    // string: a text's block (tide_pack_string), its bytes with a NUL after them; `extra` is its characters
    TIDE_PACK_TEXT = 1,
    // List<byte>: a file's bytes as a list's block (tide_pack_bytes), which text of the same file reads too
    // where each of its bytes is a character
    TIDE_PACK_BYTES = 2,
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
    // Clip: a sound's file as it is (tide_pack_clip), which the engine decodes as it's first played
    // (tide_clip_of). A list's elements' kind too (`extra`)
    TIDE_PACK_CLIP = 9,
    TIDE_PACK_CLIPS = 10, // Map<string, Clip> of a glob: as TIDE_PACK_MAP, its values' blocks clips
    // Mesh<V>: its vertices' block, a list's, of the game's vertex struct as the build that packed it lays one
    // out (tide_pack_mesh); `size` is its indices' block's offset, a list of ints. `extra`, for this kind and
    // the two after it, is its vertices' layout: a number the build made of the vertex struct, its fields' names,
    // types and places and their defaults, which the build that reads the pack asks for (tide_pack_find_mesh)
    TIDE_PACK_MESH = 11,
    // List<Mesh<V>> of a glob: a block of its meshes, each its vertices' handle then its indices'
    // (tide_mesh_lists, which is the same bytes on every platform); `size` is how many
    TIDE_PACK_MESH_LIST = 12,
    // Map<string, Mesh<V>> of a glob: its entries' block, each a key and a mesh's two handles, all live; `size`
    // is its index block's offset, as TIDE_PACK_MAP's
    TIDE_PACK_MESHES = 13,
    // SkinnedMesh<V>: a one-element block of tide_skinned_lists. Its immutable
    // model bytes and typed geometry are separate list blocks; `size` is 1.
    // All three kinds use `extra` for the same vertex layout as Mesh<V>.
    TIDE_PACK_SKINNED = 14,
    TIDE_PACK_SKINNEDS = 15, // List<SkinnedMesh<V>>: three handles per element; `size` is its count
    TIDE_PACK_SKINNED_MAP = 16, // Map<string, SkinnedMesh<V>>: live, key, three handles; `size` is its index offset
};

// The hidden portable model bytes followed by ordinary typed geometry lists.
// These handles have the same layout in generated structs on every platform.
typedef struct tide_skinned_lists {
    tide_list data, vertices, indices;
} tide_skinned_lists;

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
    // Blocks by their content's hash and their kind: a file in the pack once for each kind of block it's read
    // as, however many name it
    tide_pack_shared *shared;
    uint32_t shared_count, shared_room;
    tide_pack_entry *entries;
    uint32_t entry_count, entry_room;
} tide_pack_builder;

// A block of `size` bytes with a NUL after them, shared by `content` (its
// SHA-256): its offset. *chars gets how many UTF-8 characters they are. It's
// a list's block, of as many bytes: what a List<byte> reads, and never text
// whose characters aren't a byte each, as a text's block says its characters
// where a list's says its room (tide_pack_string is for text).
uint32_t tide_pack_bytes(tide_pack_builder *b, const uint8_t content[32], const void *data, uint32_t size, uint32_t *chars);

// Text of `size` bytes of UTF-8, with a NUL after them, shared by `content`:
// its block's offset, which a tide_text reads as any text's (its header says
// its bytes and its characters), alone or as an element, a key or a value of
// a list or a map. *chars gets its characters. Where each byte is a character
// it's the block tide_pack_bytes gives the same content, which says the same
// of it; otherwise a block of its own, so a file read as text and as bytes is
// then in the pack as each.
uint32_t tide_pack_string(tide_pack_builder *b, const uint8_t content[32], const void *data, uint32_t size, uint32_t *chars);

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
// Build-tool support: exact RGBA plus validated-format, complete BC mip chain.
// The blocks are copied; generated game code needs no new runtime call.
uint32_t tide_pack_texture_blocks(tide_pack_builder *b, const uint8_t content[32], int32_t width, int32_t height,
                                 const uint8_t *rgba, tide_texture_blocks blocks);

// A sound's file of `size` bytes as it is (a WAV), shared by `content`. Its
// block's offset: the version it's told by in its header (the content's
// first bytes, never 0), then its size, then the bytes.
uint32_t tide_pack_clip(tide_pack_builder *b, const uint8_t content[32], const void *data, uint32_t size);

// A mesh: `count` vertices of `vertex` bytes each, as the game's vertex
// struct lays one out, and `index_count` ints, three a triangle. Two blocks,
// each a list's, which code reads as it reads any list: offsets[0] is the
// vertices' and offsets[1] the indices'. Shared by `content`, the SHA-256 of
// what's in both (so of the file and of how the struct lays it out).
//
// Each block says what's in it after its elements, where a list's header has
// no word to spare: its header's `next`, which only a heap's free blocks use,
// is how far from the block's start that is, and there are 8 bytes of version
// (`content`'s, never 0), then an element's bytes. A draw tells a mesh of the
// pack's by those (tide_pack_list_version), with none of its elements read.
void tide_pack_mesh(tide_pack_builder *b, const uint8_t content[32], const void *vertices, uint32_t vertex, uint32_t count,
                    const uint32_t *indices, uint32_t index_count, uint32_t offsets[2]);

// A list of `count` meshes, whose `offsets` are two each (tide_pack_mesh's):
// its block's offset. Not shared: a declaration's own.
uint32_t tide_pack_mesh_list(tide_pack_builder *b, const uint32_t *offsets, uint32_t count);

// A map from text to meshes: `keys` are offsets of text blocks, and
// `offsets` two a mesh. Its entries' block, with its index's in *index, as
// tide_pack_map's.
uint32_t tide_pack_mesh_map(tide_pack_builder *b, const uint32_t *keys, const uint32_t *offsets, uint32_t count, uint32_t *index);

// Three offsets per model: bytes (tide_pack_bytes), vertices and indices
// (tide_pack_mesh). A singleton uses this builder with count 1 too.
uint32_t tide_pack_skinned_list(tide_pack_builder *b, const uint32_t *offsets, uint32_t count);
uint32_t tide_pack_skinned_map(tide_pack_builder *b, const uint32_t *keys, const uint32_t *offsets, uint32_t count, uint32_t *index);

// A list of `count` handles (offsets of blocks in this pack, which it tags):
// its block's offset. Not shared: a declaration's own.
uint32_t tide_pack_list(tide_pack_builder *b, const uint32_t *offsets, uint32_t count);

// A map from text to values: `keys` are offsets of text blocks
// (tide_pack_string), `values` of the values' blocks. Its entries' block,
// with its index's block in *index. The keys are hashed as
// tide_map_hash_bytes does, so the game's own lookups find them.
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
// Optional borrowed blocks for the current pack, with the same lifetime as
// texture_find's pixels. Malformed/missing metadata gives no blocks.
tide_texture_blocks tide_pack_texture_blocks_find(uint64_t named);
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

// A clip's entry as the value the game reads: a Clip that names its sound by
// its declaration, as a Texture names its image (above). Lists and maps of
// clips are laid out after the region as textures' are. The default Clip for
// no entry.
tide_clip_ref tide_pack_clip_of(const tide_pack_entry *e);

// The loaded pack's sound that a Clip names: the file's bytes as they are,
// and a version that's what's in them (the file's hash). False when the pack
// has no sound under that name.
bool tide_pack_clip_find(uint64_t named, const uint8_t **bytes, uint32_t *size, uint64_t *version);
// Calls `fn` for each clip the loaded pack has, with its file's bytes and
// version (two declarations of one file are two calls with one version).
void tide_pack_clips_each(void (*fn)(const uint8_t *bytes, uint32_t size, uint64_t version, void *context), void *context);

// The entry a Mesh declaration's name has in the loaded pack, of `kind`
// (TIDE_PACK_MESH, _MESH_LIST or _MESHES), whose vertices are laid out as
// `layout` says: NULL for none, and for one of another layout, which a pack
// another build made has where the vertex struct changed since. So code never
// reads vertices as a struct they weren't packed as.
const tide_pack_entry *tide_pack_find_mesh(const char *qualified, uint32_t kind, uint32_t layout);

// A mesh's entry as the value the game reads: its two lists, where both
// blocks are whole in the pack and its vertices are `vertex` bytes each, which
// is what the code that reads them takes them to be. An empty mesh otherwise,
// and for no entry.
tide_mesh_lists tide_pack_mesh_of(const tide_pack_entry *e, uint32_t vertex);

// A map of meshes' entry as the value the game reads (TIDE_PACK_MESHES): an empty map for no entry.
tide_map tide_pack_mesh_map_of(const tide_pack_entry *e);

// The vertices of an entry's mesh number `k` (a Mesh alone has one, number 0;
// a list's are in its order, a map's in its entries'), to write: generated
// code gives the fields no file fills their defaults with it, after each
// load, before anything reads the pack. *count gets how many there are. NULL
// past the entry's last mesh; a mesh whose vertices aren't `vertex` bytes each
// has none (*count is 0, and what's returned isn't NULL). `e` is an entry of
// the loaded pack's.
void *tide_pack_mesh_open(const tide_pack_entry *e, uint32_t k, uint32_t vertex, uint32_t *count);

// After filling defaults through tide_pack_mesh_open or tide_pack_skinned_open,
// refresh that block's stored version from its final vertex bytes before a draw;
// unchanged values keep the same version, including in shared mesh blocks.
void tide_pack_mesh_updated(void *vertices, uint32_t vertex);

// Skinned equivalents: invalid model bytes or geometry give three empty
// lists. Loading checks the complete blob and matching geometry counts.
// Maps and default-field writes follow the Mesh contracts above.
tide_skinned_lists tide_pack_skinned_of(const tide_pack_entry *e, uint32_t vertex);
tide_map tide_pack_skinned_map_of(const tide_pack_entry *e);
void *tide_pack_skinned_open(const tide_pack_entry *e, uint32_t k, uint32_t vertex, uint32_t *count);

// Check a raw pack before loading it: this declaration's models must have
// the build's layout, count and content SHA-256s, in source order. Singleton
// and list hashes cover canonical bytes; map hashes cover key bytes followed
// by canonical bytes, so changing a gameplay lookup key changes its identity.
// Nothing changes the loaded pack, so rejection preserves its old assets.
bool tide_pack_skinned_content(const uint8_t *bytes, size_t size, const char *qualified, uint32_t kind, uint32_t layout,
                               const uint8_t *hashes, uint32_t count);

// What's in a list of the pack's whose block says (a mesh's vertices or its
// indices, tide_pack_mesh), for the draw list: false for any other list, and
// for one whose elements aren't `element` bytes each.
bool tide_pack_list_version(tide_list list, uint32_t element, uint64_t *version);

// Runtime draw support: true only for immutable canonical model bytes that
// passed complete skinned-record validation in the currently loaded pack.
// The version is the hash of those exact bytes, computed once at load. Other
// byte lists, including owned copies and decoded values, are not registered.
// Replacement/unload discard this provenance, as do geometry-default writes
// whose storage overlaps a model's header or bytes in a forged pack.
bool tide_pack_skinned_version(tide_list data, uint64_t *version);

// The same current proof, with a nonzero process-local identity unique to this
// validated block and load. Unlike the content hash, identity proves that the
// bytes have not changed while this proof remains current. Reload/replacement
// creates new identities even for equal bytes; overlapping writes invalidate
// them. Optional outputs are left unchanged on failure. No identity is stored
// in a pack, game state or snapshot.
bool tide_pack_skinned_proof(tide_list data, uint64_t *identity, uint64_t *version);

// The same current proof, plus whether every channel's first key time is
// nonnegative (including negative zero). False in the optional output means
// sampling still checks the selected clip's channels, not that the model is
// invalid. The output is unchanged when no current proof exists.
bool tide_pack_skinned_sample_proof(tide_list data, bool *nonnegative_first_times);

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
