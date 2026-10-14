#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// A model's catalog and hierarchy, without geometry, animation samples or
// side buffers. Names are the source's UTF-8, NULL for an unnamed item.
// Node indices and array order are the import source's, including FBX's
// unnamed root and transform helpers, not a runtime skeleton's.
typedef struct model_metadata_node {
    char *name;
    int32_t parent; // -1 for a root
} model_metadata_node;

typedef struct model_metadata_skin {
    char *name;
    int32_t root; // The optional skeleton node, or -1
    uint32_t *joints;
    uint32_t joint_count;
} model_metadata_skin;

typedef struct model_metadata {
    char **animations;
    uint32_t animation_count;
    model_metadata_node *nodes;
    uint32_t node_count;
    model_metadata_skin *skins;
    uint32_t skin_count;
} model_metadata;

// Reads glTF JSON, the JSON chunk of GLB 2, or ASCII/binary FBX. All resulting
// arrays and names are owned; free them with model_metadata_free. On failure
// out is empty and why says why (256 bytes). This checks catalog structure,
// hierarchy and skin indices, not the data the full importer validates.
bool model_metadata_read(const uint8_t *data, size_t size, const char *name, model_metadata *out, char why[256]);
void model_metadata_free(model_metadata *metadata);

typedef enum model_metadata_status {
    MODEL_METADATA_READY,
    MODEL_METADATA_UNAVAILABLE, // Missing, unreadable, or beyond interactive inspection's budget
    MODEL_METADATA_INVALID,
} model_metadata_status;

// Cached inspection of a local file. GLB reads its header and JSON only,
// regardless of its BIN size; JSON over 16 MiB is unavailable interactively.
// FBX reads at most 64 MiB of source and skips geometry/curves/embedded data,
// with a 128 MiB limit for each of the parser's temporary and result arenas.
// A READY result borrows metadata until this path is reloaded or the cache
// is forgotten. Other results set out to NULL. why borrows the same cache.
model_metadata_status model_metadata_cached(const char *path, const model_metadata **out, const char **why);

// Uses only the existing .tide/assets/urls/<URL SHA-256> file. Never fetches
// or creates files, even when there is no cache. game_folder may be NULL for
// the compiler's temporary URL cache. The same borrowing rule applies.
model_metadata_status model_metadata_cached_url(const char *url, const char *game_folder, const model_metadata **out,
                                                const char **why);

// Marks a path dirty even when its size and timestamp did not change. NULL
// invalidates every entry. Existing borrowed results live until the next read.
void model_metadata_invalidate(const char *path);
void model_metadata_forget(void);

typedef struct model_metadata_stats {
    uint64_t file_reads;
    uint64_t bytes_read;
    uint64_t parses;
    uint64_t cache_hits;
} model_metadata_stats;

// Inspection counters for responsiveness measurements. forget resets them.
model_metadata_stats model_metadata_statistics(void);
