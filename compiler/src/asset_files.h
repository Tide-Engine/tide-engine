#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common.h"
#include "mesh_files.h"
#include "tide/png.h"
#include "tide/skinned_data.h"
#include "tide/texture_blocks.h"

// The files an `asset` declaration resolves to (docs/spec.md, Assets): found
// from the declaring file, read, hashed and, for a Grid2<Color>, decoded, so
// the checker can say what's wrong at the declaration and codegen can pack
// them. Reads are kept by path, size and time, so a language server that
// checks a game at every keystroke reads and decodes each file once. A project's
// persistent import cache also reuses model/audio processing across processes;
// prepared draw-only textures can defer pixels in code-writing compilations.

typedef enum asset_kind {
    ASSET_TEXT,  // string: UTF-8 text
    ASSET_BYTES, // List<byte>: any file
    ASSET_GRID,  // Grid2<Color>: a supported image, decoded
    ASSET_TEXTURE, // Texture: a supported image, decoded, kept a byte a channel to draw with
    // Cubemap: six images a glob matches, each named for its face by how its name ends before the extension (px,
    // nx, py, ny, pz, nz), decoded: its items are the six in that order
    ASSET_CUBEMAP,
    ASSET_CLIP,    // Clip: WAV/Vorbis kept encoded; MP3/FLAC converted to float PCM WAV at build time
    // Mesh<V>: glTF (.glb, .gltf), OBJ or FBX, read here as one mesh (mesh_files.h), which codegen lays out as
    // the declaration's vertex struct
    ASSET_MESH,
    ASSET_SKINNED,
    ASSET_ANIMATIONS, // AnimationCatalog: glTF skeleton/clip data, without typed render geometry
    ASSET_ANIMATION, // Animation: one selected clip from an animation source.
    ASSET_HUMANOID, // HumanoidRig: a checked calibration profile, packed as immutable typed blocks.
    ASSET_VALUES,   // A constant List<T> build code made: plain elements side by side (build_code.h)
    ASSET_CLIP_DATA, // ClipData: an audio file's samples, decoded, as code reads and edits them (build code's source)
    ASSET_FONT,     // Font: a TrueType font file, read into font data (tide/font.h), which `data` then is
    // TextureArray: images of one size, a glob's matches or a list of paths, as Textures are read: its items are its
    // layers in order
    ASSET_TEXTURE_ARRAY,
} asset_kind;

// An image's texture as the build's cache has it prepared (texture_cache.h), known by its entry's first bytes: its
// size and how it's kept, the content that tells it in a pack, and the entry's key, which finds its pixels.
typedef struct asset_prepared {
    uint32_t width, height;
    uint32_t format, levels, bytes; // Its blocks' (tide/texture_blocks.h), or 0, 0 and its RGBA's bytes
    bool linear, raw;
    uint8_t content[32], key[32];
} asset_prepared;

typedef struct asset_match {
    const char *key;      // The path as written from the declaring file, forward slashes: a map's key. A URL as it is.
    const char *abs;      // Where the bytes were read from
    // File bytes, or an imported Clip's PCM WAV (kept until the file changes). NULL for a Texture or a Mesh whose
    // import the cache had, where the file is as a build recorded it (asset_files_trust): asset_match_bytes reads
    // them when something wants them after all. `size` is the file's either way.
    const uint8_t *data;
    size_t size;
    uint8_t hash[32];     // SHA-256 of the original file bytes
    tide_png_image image; // ASSET_GRID and ASSET_TEXTURE: its pixels
    tide_texture_blocks image_mips; // Optional authored full RGBA8 chain, straight samples; cache-owned.
    bool has_prepared;       // A draw-only texture the cache has: its pixels stay in the cache's file, undecoded
    asset_prepared prepared; // ...and what its entry says of it
    mesh_file mesh;       // ASSET_MESH: its vertices and triangles, which stay until the file changes, as its bytes do
    const uint8_t *skinned;
    uint32_t skinned_size;
    struct humanoid_asset *humanoid;
    const char **sides;   // ...and the files beside a .gltf that it was read with, its buffers': absolute paths
    int side_count;
    // What build code made (build_code.h), in place of a file's contents: `data` holds a mesh's vertices as the
    // declaration's struct lays them out (`mesh` has its indices, and what the levels of detail weigh), or a
    // constant list's elements; `element` bytes each, `count` of them
    bool built;
    uint32_t element, count;
    const struct tide_mesh_levels *levels; // A built mesh's levels of detail, as its code made them, or NULL
} asset_match;

typedef struct asset_value {
    asset_kind kind;
    bool many;   // A List<T> or Map<string, T>: a glob's matches, or the one file a path or a URL names
    bool is_map; // ...a Map<string, T>
    bool paths_only; // Editor discovery: paths without file reads or full imports.
    asset_match *items;
    int count;
    const char *cache_folder; // Build-tool import cache; never part of the asset's identity.
} asset_value;

// Resolves the path written in a declaration: `path` as the source has it
// (escapes and all), in the file `declaring` (an absolute path). Errors go to
// the diagnostics at `at`, saying what to write, and give NULL. `game_folder`
// is where the game's .tide/ is, or NULL where nothing says (tidec on its
// own): it's where the hashes of named files are kept, so a moved file is
// found by its bytes, and where fetched URLs are cached.
asset_value *asset_resolve(const char *declaring, str path, asset_kind kind, bool many, bool is_map, loc at, const char *game_folder);
asset_value *asset_resolve_paths(const char *declaring, str path, asset_kind kind, bool many, bool is_map, loc at,
                                 const char *game_folder);
// [Parts]: the skinned models asset_resolve reads until this is called again
// keep only the parts `keep` accepts (tide_skinned_data_decode_parts). Each is
// read for its declaration, outside the file's own cache. Patterns identify
// the selection for the persistent cache and must describe keep. NULL keeps all.
void asset_choose_parts(tide_skinned_keep_part_fn keep, void *user, const str *patterns, int count);
// A name against a [BlendShapes] or [Parts] pattern: * is any run of
// characters (none too), everything else itself.
bool asset_name_matches(str pattern, const char *name, uint32_t length);
// Select a lazily cached generated-tangent copy for a static float4-tangent
// consumer. Other declarations and editor metadata retain the canonical mesh.
bool asset_mesh_tangents(asset_value *value, loc at);
// Decode the language literal's escapes and normalize path separators (arena).
const char *asset_written_path(str text);

// After a skinned import fails, diagnose an unsupported source format with
// export guidance instead of a glTF JSON error. URL queries and fragments do
// not name a format. False leaves the original import diagnostic to callers.
bool asset_skinned_format_diagnostic(const char *path, const char *why, loc at);
bool asset_animations_format_diagnostic(const char *path, const char *why, loc at);

// The folders and files a resolved value depends on, for whoever watches
// them: each file's path, and a glob's folder (ending in '/'). Calls `found`
// with each, once. What a file was read with beside it (a .gltf's buffers) is
// its value's to say: asset_match.sides.
typedef void (*asset_path_fn)(void *user, const char *path);
void asset_watched(const char *declaring, str path, asset_path_fn found, void *user);
// The images `count` paths name (each written in the file `declaring[i]`; files or globs), read and decoded ahead,
// several at once, where asset_resolve would decode them as it reads them: not one whose texture the cache has
// prepared, which it takes instead. What each gives is what asset_resolve would get, as it's the same file read the
// same way; what's wrong with one is said when an asset reads it.
void asset_files_decode_ahead(const char *const *declaring, const str *paths, int count, const char *game_folder);

// Whether `path` (forward slashes) matches the glob `pattern`: `*` within a
// name, `**` across folders, `?` a character; Windows paths ignore case.
bool asset_glob_match(const char *pattern, const char *path);

// An asset as checked: its declaring file, its path as written, and what it
// resolved to (NULL where it couldn't).
typedef struct asset_named {
    const char *declaring;
    str path;
    const asset_value *value;
} asset_named;

// Keeps the hashes of the named files in the game's .tide/ (assets.h), once a
// game is checked: each file resolved, by its path from the game's folder, and
// what was known of a file that's missing now, so it's found by its bytes
// later. Written only where it changed; nothing without a `game_folder`.
void asset_files_save_hashes(const char *game_folder, const asset_named *named, int count);

// Lets go of every file read so far: tests, or a process that goes on without them.
void asset_files_forget(void);

// A file or folder explicitly changed: read its cached files and dependent
// static or skinned meshes again even if their size/time is unchanged.
// Existing values stay valid until the next resolution, as when metadata changes.
void asset_files_changed(const char *path);

// Code-writing compilations may read/write persistent imports and retain a
// prepared Texture instead of float pixels. Off for direct importer/editor
// callers and inspection-only checks.
void asset_files_use_build_cache(bool enabled);

// A build with the cache on takes a file's hash from the game's record of it (<game>/.tide/assets.times) where the
// file's size and last-write time are as recorded, without reading it, as git takes a file to be unchanged by
// them (owner, 2026-11-17). asset_files_trust reads the record for the compilation that follows, or lets go of it
// for NULL; asset_files_save_times writes it from the files read and taken so far. A file is recorded only where
// it was read at least two seconds after it was last written: one written again inside the time its file system
// can tell apart is read again next time, as git reads a file as new as its index.
void asset_files_trust(const char *game_folder);
void asset_files_save_times(const char *game_folder);

// The SHA-256 of the file at `path`, as reading an asset's file gives it: from the game's record where the file is
// as recorded, else read. False where there's no file, or it can't be read.
bool asset_file_hash(const char *path, uint8_t hash[32]);

// The SHA-256 of an imported model's bytes (asset_match.skinned), worked out once for each run of bytes, by where
// they are: megabytes each, which the simulation's identity, the model's meshes, their levels and its block in the
// pack each ask for. asset_bytes_hash_forget starts afresh, for a compilation whose bytes are other ones.
void asset_bytes_hash(const uint8_t *bytes, size_t size, uint8_t hash[32]);
void asset_bytes_hash_forget(void);

// An item's file bytes, read now where they weren't (asset_match.data): NULL where the file can't be read. Called
// from several threads only for items whose bytes are there already.
const uint8_t *asset_match_bytes(const asset_match *item, size_t *size);
