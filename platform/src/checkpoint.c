// A dev build's checkpoints (tide/checkpoint.h): a match's world in a file, with its layout. A file of its own, as it
// lists and makes folders with the operating system's calls.

#if !defined(_WIN32) && !defined(__APPLE__) && !defined(__wasm__)
#define _DEFAULT_SOURCE // Folders and their files under strict C
#endif

#include "tide/checkpoint.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

#define MAGIC 0x4B444954u // "TIDK"
#define VERSION 1u

// The file: this, then the layout (tide_layout_pack) and the world's bytes (tide_game.pack_world), each from an
// 8-byte boundary
typedef struct header {
    uint32_t magic, version;
    uint64_t hash; // The game's (tide_game.hash): the same one has the same layout
    int32_t player;
    uint32_t tick;
    uint32_t layout, layout_size;
    uint32_t world, world_size;
} header;

static uint32_t align8(const uint32_t n)
{
    return (n + 7u) & ~7u;
}

bool tide_checkpoint_name_ok(const char *name, char *why, const size_t why_size)
{
    const size_t n = name ? strlen(name) : 0;
    if (!n || n >= TIDE_CHECKPOINT_NAME) {
        snprintf(why, why_size, "a checkpoint's name is 1 to %u letters, digits, '-', '_' and '.', like boss-fight",
                 (unsigned)(TIDE_CHECKPOINT_NAME - 1u));
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        const char c = name[i];
        const bool fine = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_'
                       || (c == '.' && i > 0);
        if (!fine) {
            snprintf(why, why_size, "'%s' can't name a checkpoint: use letters, digits, '-', '_' and '.' (not first), like boss-fight",
                     name);
            return false;
        }
    }
    return true;
}

// `folder`/`name`.checkpoint, and with ".part" for the file it's written to first
static bool path_of(const char *folder, const char *name, const char *suffix, char *out, const size_t size)
{
    const int n = snprintf(out, size, "%s%s%s.checkpoint%s", folder && folder[0] ? folder : ".", "/", name, suffix);
    return n > 0 && (size_t)n < size;
}

#ifdef _WIN32
// A UTF-8 path as Windows' own calls take it
static wchar_t *wide(const char *path)
{
    const int n = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    wchar_t *w = n > 0 ? malloc((size_t)n * sizeof *w) : NULL;
    if (w && MultiByteToWideChar(CP_UTF8, 0, path, -1, w, n) != n) {
        free(w);
        w = NULL;
    }
    return w;
}
#endif

static FILE *open_file(const char *path, const char *mode)
{
#ifdef _WIN32
    wchar_t *w = wide(path), *m = wide(mode);
    FILE *f = w && m ? _wfopen(w, m) : NULL;
    free(w);
    free(m);
    return f;
#else
    return fopen(path, mode);
#endif
}

static bool make_folder(const char *folder)
{
#ifdef _WIN32
    wchar_t *w = wide(folder);
    const bool ok = w && (CreateDirectoryW(w, NULL) || GetLastError() == ERROR_ALREADY_EXISTS);
    free(w);
    return ok;
#else
    struct stat s;
    return mkdir(folder, 0777) == 0 || (stat(folder, &s) == 0 && S_ISDIR(s.st_mode));
#endif
}

// `from` in place of `to`, whether or not `to` was there
static bool replace(const char *from, const char *to)
{
#ifdef _WIN32
    wchar_t *f = wide(from), *t = wide(to);
    const bool ok = f && t && MoveFileExW(f, t, MOVEFILE_REPLACE_EXISTING);
    free(f);
    free(t);
    return ok;
#else
    return rename(from, to) == 0;
#endif
}

uint8_t *tide_checkpoint_pack(const tide_layout *layout, const tide_game *game, const void *world, const int32_t player,
                              const uint32_t tick, uint32_t *size)
{
    *size = 0;
    uint32_t layout_size = 0;
    void *packed = tide_layout_pack(layout, &layout_size);
    const uint32_t world_size = game->pack_world(world, NULL, 0);
    header h = {MAGIC, VERSION, game->hash, player, tick, 0, layout_size, 0, world_size};
    h.layout = align8(sizeof h);
    h.world = align8(h.layout + layout_size);
    const uint64_t total = (uint64_t)h.world + world_size;
    uint8_t *bytes = packed && total <= UINT32_MAX ? calloc(1, (size_t)total) : NULL;
    if (bytes) {
        memcpy(bytes, &h, sizeof h);
        memcpy(bytes + h.layout, packed, layout_size);
        game->pack_world(world, bytes + h.world, world_size);
        *size = (uint32_t)total;
    }
    free(packed);
    return bytes;
}

bool tide_checkpoint_save(const char *folder, const char *name, const tide_layout *layout, const tide_game *game,
                          const void *world, const int32_t player, const uint32_t tick, char *why, const size_t why_size)
{
    if (!tide_checkpoint_name_ok(name, why, why_size)) return false;
    if (!layout || !game || !world || !game->pack_world) {
        snprintf(why, why_size, "there's no match on this machine's server to save");
        return false;
    }
    char path[4096], part[4096];
    if (!path_of(folder, name, "", path, sizeof path) || !path_of(folder, name, ".part", part, sizeof part)) {
        snprintf(why, why_size, "the checkpoints' folder's path is too long");
        return false;
    }
    if (!make_folder(folder && folder[0] ? folder : ".")) {
        snprintf(why, why_size, "can't make the folder %s", folder);
        return false;
    }
    uint32_t size = 0;
    uint8_t *bytes = tide_checkpoint_pack(layout, game, world, player, tick, &size);
    if (!bytes) {
        snprintf(why, why_size, "there wasn't enough memory");
        return false;
    }
    // Written whole beside it, then in its place: a checkpoint is never half a file
    FILE *f = open_file(part, "wb");
    bool ok = f && fwrite(bytes, 1, (size_t)size, f) == (size_t)size;
    if (f && fclose(f) != 0) ok = false;
    free(bytes);
    if (ok) ok = replace(part, path);
    if (!ok) {
        remove(part);
        snprintf(why, why_size, "can't write %s", path);
    }
    return ok;
}

bool tide_checkpoint_load(const char *folder, const char *name, const tide_layout *layout, const tide_game *game,
                          const bool force, void **world_out, int32_t *player, tide_checkpoint_loaded *loaded)
{
    *loaded = (tide_checkpoint_loaded){0};
    *world_out = NULL;
    *player = -1;
    char *failed = loaded->carried.failed;
    const size_t failed_size = sizeof loaded->carried.failed;
    if (!tide_checkpoint_name_ok(name, failed, failed_size)) return false;
    char path[4096];
    if (!path_of(folder, name, "", path, sizeof path)) {
        snprintf(failed, failed_size, "the checkpoints' folder's path is too long");
        return false;
    }
    FILE *f = open_file(path, "rb");
    if (!f) {
        snprintf(failed, failed_size, "there's no checkpoint %s (%s)", name, path);
        return false;
    }
    uint8_t *bytes = NULL;
    size_t size = 0, capacity = 0;
    for (;;) {
        if (size == capacity) {
            capacity = capacity ? capacity * 2u : 65536u;
            uint8_t *grown = capacity <= UINT32_MAX ? realloc(bytes, capacity) : NULL;
            if (!grown) break;
            bytes = grown;
        }
        const size_t got = fread(bytes + size, 1, capacity - size, f);
        size += got;
        if (got == 0) break;
    }
    const bool read_whole = feof(f) && !ferror(f) && size <= UINT32_MAX;
    fclose(f);
    if (!read_whole) {
        free(bytes);
        snprintf(failed, failed_size, "%s didn't read whole", path);
        return false;
    }
    const bool ok = tide_checkpoint_unpack(bytes, (uint32_t)size, path, layout, game, force, world_out, player, loaded);
    free(bytes);
    return ok;
}

bool tide_checkpoint_unpack(void *data, const uint32_t size, const char *path, const tide_layout *layout,
                            const tide_game *game, const bool force, void **world_out, int32_t *player,
                            tide_checkpoint_loaded *loaded)
{
    *loaded = (tide_checkpoint_loaded){0};
    *world_out = NULL;
    *player = -1;
    char *failed = loaded->carried.failed;
    const size_t failed_size = sizeof loaded->carried.failed;
    uint8_t *bytes = data;
    header h = {0};
    if (size >= sizeof h) memcpy(&h, bytes, sizeof h);
    if (size >= 8u && h.magic == MAGIC && h.version != VERSION) {
        snprintf(failed, failed_size, "%s was saved by another version of tide", path);
        return false;
    }
    const bool whole = size >= sizeof h && h.magic == MAGIC && h.layout <= size && h.layout_size <= size - h.layout
                    && h.world <= size && h.world_size <= size - h.world && (h.layout & 7u) == 0 && (h.world & 7u) == 0;
    if (!whole) {
        snprintf(failed, failed_size, "%s isn't a checkpoint, or isn't whole", path);
        return false;
    }
    loaded->tick = h.tick;
    void *world = calloc(1, game->world_size);
    bool ok = world != NULL;
    if (!ok) snprintf(failed, failed_size, "there wasn't enough memory");
    if (ok && h.hash == game->hash) {
        // The same layout: its bytes are a world of this build as they are
        loaded->same = true;
        ok = game->unpack_world(world, bytes + h.world, h.world_size);
        if (!ok) snprintf(failed, failed_size, "%s isn't a world of this build", path);
    } else if (ok) {
        const tide_layout *old = tide_layout_unpack(bytes + h.layout, h.layout_size);
        void *carried = NULL;
        uint32_t carried_size = 0;
        if (!old) {
            snprintf(failed, failed_size, "%s's layout didn't read", path);
            ok = false;
        } else {
            loaded->fields_reset = tide_layout_fields_reset(old, layout);
            ok = force ? tide_migrate_world_forced(old, &old->match, bytes + h.world, h.world_size, layout, &layout->match,
                                                   &carried, &carried_size, &loaded->carried)
                       : tide_migrate_world(old, &old->match, bytes + h.world, h.world_size, layout, &layout->match, &carried,
                                            &carried_size, &loaded->carried);
            if (ok && !game->unpack_world(world, carried, carried_size)) {
                snprintf(failed, failed_size, "this build couldn't take what was carried over");
                ok = false;
            }
        }
        free(carried);
    }
    if (!ok) {
        if (world) {
            game->free_world(world);
            free(world);
        }
        return false;
    }
    *world_out = world;
    *player = h.player;
    return true;
}

typedef struct found {
    char name[TIDE_CHECKPOINT_NAME];
    int64_t when;
} found;

static int newest_first(const void *a, const void *b)
{
    const found *x = a, *y = b;
    if (x->when != y->when) return x->when > y->when ? -1 : 1;
    return strcmp(x->name, y->name);
}

// A file's name as a checkpoint's: what's before ".checkpoint", if that's all it ends with
static bool checkpoint_name(const char *file, char *name)
{
    const size_t n = strlen(file), suffix = strlen(".checkpoint");
    if (n <= suffix || n - suffix >= TIDE_CHECKPOINT_NAME || strcmp(file + n - suffix, ".checkpoint") != 0) return false;
    memcpy(name, file, n - suffix);
    name[n - suffix] = '\0';
    char why[8];
    return tide_checkpoint_name_ok(name, why, sizeof why);
}

uint32_t tide_checkpoint_list(const char *folder, char (*names)[TIDE_CHECKPOINT_NAME], const uint32_t most)
{
    found *all = NULL;
    uint32_t count = 0, capacity = 0;
    const char *at = folder && folder[0] ? folder : ".";
#ifdef _WIN32
    char pattern[4096];
    if (snprintf(pattern, sizeof pattern, "%s/*.checkpoint", at) >= (int)sizeof pattern) return 0;
    wchar_t *w = wide(pattern);
    WIN32_FIND_DATAW data;
    HANDLE scan = w ? FindFirstFileW(w, &data) : INVALID_HANDLE_VALUE;
    free(w);
    if (scan == INVALID_HANDLE_VALUE) return 0;
    do {
        char file[1024];
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (!WideCharToMultiByte(CP_UTF8, 0, data.cFileName, -1, file, sizeof file, NULL, NULL)) continue;
        found f;
        if (!checkpoint_name(file, f.name)) continue;
        f.when = (int64_t)(((uint64_t)data.ftLastWriteTime.dwHighDateTime << 32) | data.ftLastWriteTime.dwLowDateTime);
#else
    DIR *scan = opendir(at);
    if (!scan) return 0;
    for (const struct dirent *e; (e = readdir(scan));) {
        found f;
        if (!checkpoint_name(e->d_name, f.name)) continue;
        // Its time if it has one (a WASI runtime may not say), for the newest first
        char path[4096];
        struct stat s;
        const bool stated = snprintf(path, sizeof path, "%s/%s", at, e->d_name) < (int)sizeof path && stat(path, &s) == 0;
        if (stated && S_ISDIR(s.st_mode)) continue;
        f.when = stated ? (int64_t)s.st_mtime : 0;
#endif
        if (count == capacity) {
            capacity = capacity ? capacity * 2u : 16u;
            found *grown = realloc(all, capacity * sizeof *all);
            if (!grown) break;
            all = grown;
        }
        all[count++] = f;
#ifdef _WIN32
    } while (FindNextFileW(scan, &data));
    FindClose(scan);
#else
    }
    closedir(scan);
#endif
    if (count) qsort(all, count, sizeof *all, newest_first);
    for (uint32_t i = 0; i < count && i < most; i++) memcpy(names[i], all[i].name, TIDE_CHECKPOINT_NAME);
    free(all);
    return count;
}
