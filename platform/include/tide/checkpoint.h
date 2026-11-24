#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tide/layout.h"
#include "tide/migrate.h"
#include "tide/session.h"

// A dev build's checkpoints: a match's world saved to a file, which a run goes on from later, in this build or a
// later one. What's saved is the server's world, as it packs (tide_game.pack_world), with its game's layout
// (tide_layout_pack), so a build whose layout changed carries it over by name, as hot reloading does
// (tide/migrate.h). A checkpoint is a file `<name>.checkpoint` in a folder (a project's `checkpoints`), named by
// letters, digits, '-', '_' and '.'.

#define TIDE_CHECKPOINT_NAME 64u // A name's bytes, with its NUL

// What loading one did
typedef struct tide_checkpoint_loaded {
    bool same;              // The build's layout is the one it was saved with: nothing carried over
    uint32_t fields_reset;  // Fields whose type changed, which have their defaults
    uint32_t tick;          // The match's tick when it was saved
    tide_migration carried; // What carrying it over dropped, or why it couldn't (`failed`)
} tide_checkpoint_loaded;

// Whether `name` can name one; false, saying why in `why`, otherwise
bool tide_checkpoint_name_ok(const char *name, char *why, size_t why_size);

// Saves `world` (a world of `game`, whose layout is `layout`) as `folder`/`name`.checkpoint, making the folder,
// with `player` (this machine's player's index in it, or -1) and the match's `tick`. False, saying why in `why`.
bool tide_checkpoint_save(const char *folder, const char *name, const tide_layout *layout, const tide_game *game,
                          const void *world, int32_t player, uint32_t tick, char *why, size_t why_size);

// The world saved as `folder`/`name`.checkpoint, as a world of `game` (layout `layout`) in `*world` (made with
// calloc; free it with game->free_world, then free()), and the player it was saved by in `*player` (-1 for none).
// Carried over when the layout changed; `force` drops the entities of a scene that's gone rather than refusing
// (tide_migrate_world_forced). False, with why in `loaded->carried.failed`.
bool tide_checkpoint_load(const char *folder, const char *name, const tide_layout *layout, const tide_game *game,
                          bool force, void **world, int32_t *player, tide_checkpoint_loaded *loaded);

// The same as bytes, for where the files are another program's (a page's, whose tide run keeps them): a checkpoint's
// bytes (malloc'd, NULL without the memory), and the world in bytes of one, which `bytes` (8-aligned, `size` long)
// becomes the layout of as it's read. `what` names it in what's said.
uint8_t *tide_checkpoint_pack(const tide_layout *layout, const tide_game *game, const void *world, int32_t player,
                              uint32_t tick, uint32_t *size);
bool tide_checkpoint_unpack(void *bytes, uint32_t size, const char *what, const tide_layout *layout, const tide_game *game,
                            bool force, void **world, int32_t *player, tide_checkpoint_loaded *loaded);

// The checkpoints in `folder`, newest first: up to `most` names in `names`; returns how many there are of them all
uint32_t tide_checkpoint_list(const char *folder, char (*names)[TIDE_CHECKPOINT_NAME], uint32_t most);

// Other files a dev build keeps in the same folder, named as checkpoints are, by their extension (".recording": a
// match's ticks, tide/timeline.h): written whole and then in place, read (malloc'd, NULL saying why), and listed,
// newest first. A checkpoint is one of them, ".checkpoint".
bool tide_checkpoint_write(const char *folder, const char *name, const char *extension, const void *bytes, uint32_t size,
                           char *why, size_t why_size);
uint8_t *tide_checkpoint_read(const char *folder, const char *name, const char *extension, uint32_t *size, char *why,
                              size_t why_size);
uint32_t tide_checkpoint_list_of(const char *folder, const char *extension, char (*names)[TIDE_CHECKPOINT_NAME],
                                 uint32_t most);
