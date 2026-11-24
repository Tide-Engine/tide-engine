#if !defined(_WIN32) && !defined(__APPLE__) && !defined(__wasm__)
#define _DEFAULT_SOURCE
#endif

#include "tide/serve.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/pack.h"
#include "tide/page.h"
#include "tide/platform.h"
#include "tide/shard_coordinator.h"
#include "tide/shards.h"
#include "tide/time.h"

#if defined(__wasm__) || defined(__ANDROID__)
int tide_serve(const tide_serve_desc *desc)
{
    (void)desc;
    fputs("tide: dedicated server executables are desktop builds\n", stderr);
    return 2;
}
#else

#include <signal.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#elif defined(__APPLE__)
#include <dirent.h>
#include <fcntl.h>
#include <mach-o/dyld.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#endif

static volatile sig_atomic_t serve_stopped;

static void serve_stop(const int signal_number)
{
    (void)signal_number;
    serve_stopped = 1;
}

static bool serve_number(const char *text, const uint32_t most, uint32_t *out)
{
    uint64_t n = 0;
    if (!*text) return false;
    for (; *text; text++) {
        if (*text < '0' || *text > '9') return false;
        n = n * 10u + (unsigned)(*text - '0');
        if (n > most) return false;
    }
    *out = (uint32_t)n;
    return true;
}

static bool serve_secure(void *user, const tide_address peer)
{
    (void)user;
    return peer.kind == TIDE_ADDRESS_ROOM; // WebRTC's authenticated DTLS channel.
}

static bool serve_load(const tide_serve_desc *desc)
{
    if (!desc->assets) return true;
    char path[4096];
#ifdef _WIN32
    const DWORD length = GetModuleFileNameA(NULL, path, sizeof path);
    if (!length || length >= sizeof path) return false;
#elif defined(__APPLE__)
    uint32_t executable_room = sizeof path;
    if (_NSGetExecutablePath(path, &executable_room)) return false;
#else
    const ssize_t length = readlink("/proc/self/exe", path, sizeof path - 1u);
    if (length < 0 || (size_t)length >= sizeof path - 1u) return false;
    path[length] = 0;
#endif
    char *base = path;
    for (char *p = path; *p; p++) if (*p == '/' || *p == '\\') base = p + 1;
    const size_t room = sizeof path - (size_t)(base - path);
    const int written = snprintf(base, room, "%s.pack", desc->pack_name);
    if (written < 0 || (size_t)written >= room) return false;
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "tide: cannot open server assets at %s\n", path); return false; }
    bool ok = fseek(f, 0, SEEK_END) == 0;
    const long size = ok ? ftell(f) : -1;
    ok = size >= 0 && (uint64_t)size <= UINT32_MAX && fseek(f, 0, SEEK_SET) == 0;
    uint8_t *bytes = ok ? malloc((size_t)size ? (size_t)size : 1u) : NULL;
    ok = bytes && fread(bytes, 1, (size_t)size, f) == (size_t)size;
    fclose(f);
    if (ok) ok = desc->assets(bytes, (size_t)size);
    free(bytes);
    return ok;
}

// A sharded world's journal in a folder (--shard-journal): `checkpoint`, and
// `shard-<id>.log`, each a head (SERVE_LOG_HEAD bytes: "TSLG", a version, the
// world's name, the number of the checkpoint its records follow, the shard)
// then a u32 size and a record, over and over, from the checkpoint on. On
// starting, a folder with a checkpoint is the world again, for the build that
// made the checkpoint (tide_shard_checkpoint_id). The world's store is the
// folder's `store`, so the folder is all of the world.
#define SERVE_LOG_MAGIC 0x474c5354u // "TSLG"
#define SERVE_LOG_VERSION 1u
#define SERVE_LOG_HEAD 32u

typedef struct serve_kept {
    uint32_t shard;
    uint8_t *bytes;
    uint32_t *records; // Where each whole record's size is in `bytes`
    uint32_t count;
} serve_kept;

typedef struct serve_world {
    tide_hosting *host;
    const char *folder;
    FILE **logs;
    uint32_t log_room;
    // The shards whose logs the folder had on starting, whatever their ids: the next checkpoint removes them
    uint32_t *left;
    uint32_t left_count;
    bool written; // Records since the last flush
    bool made;    // A log made since the last flush: the folder has a new name to keep
    bool failed;
    // What a log made now says it follows: the world, and its last checkpoint kept here
    uint64_t world_id[2];
    uint32_t sequence;
    // Recovering: the records of each log the folder had, as read
    serve_kept *read;
    uint32_t read_count;
    uint32_t read_last; // The one asked for last, as a shard's records are asked for in turn
} serve_world;

static tide_server *serve_world_start(void *user, const uint32_t shard, const tide_server_desc *d, uint64_t *match)
{
    return tide_shards_hosting_start(((serve_world *)user)->host, shard, d, match);
}

static void serve_world_stop(void *user, const uint32_t shard, const uint64_t match)
{
    tide_shards_hosting_stop(((serve_world *)user)->host, shard, match);
}

static void serve_path(char *out, const size_t size, const char *folder, const char *name)
{
    snprintf(out, size, "%s/%s", folder, name);
}

static bool serve_sync(FILE *f)
{
    if (fflush(f) != 0) return false;
#ifdef _WIN32
    return _commit(_fileno(f)) == 0;
#else
    return fsync(fileno(f)) == 0;
#endif
}

// A file made, moved or removed in the folder is kept once the folder is: its names are the folder's own data
// where a file's sync doesn't cover them. (Windows keeps them with the file: MOVEFILE_WRITE_THROUGH, _commit.)
static bool serve_sync_folder(const char *folder)
{
#ifdef _WIN32
    (void)folder;
    return true;
#else
    const int fd = open(folder, O_RDONLY);
    if (fd < 0) return false;
    const bool ok = fsync(fd) == 0;
    close(fd);
    return ok;
#endif
}

static void serve_u32(uint8_t *out, const uint32_t v)
{
    for (uint32_t k = 0; k < 4u; k++) out[k] = (uint8_t)(v >> (8u * k));
}

static void serve_u64(uint8_t *out, const uint64_t v)
{
    for (uint32_t k = 0; k < 8u; k++) out[k] = (uint8_t)(v >> (8u * k));
}

static uint32_t serve_get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t serve_get_u64(const uint8_t *p)
{
    return (uint64_t)serve_get_u32(p) | (uint64_t)serve_get_u32(p + 4u) << 32;
}

static FILE *serve_log(serve_world *j, const uint32_t shard, const char *mode)
{
    if (shard >= j->log_room) {
        const uint32_t room = shard + 8u;
        FILE **grown = realloc(j->logs, room * sizeof *grown);
        if (!grown) tide_out_of_memory();
        memset(grown + j->log_room, 0, (room - j->log_room) * sizeof *grown);
        j->logs = grown;
        j->log_room = room;
    }
    if (!j->logs[shard]) {
        char name[32], path[4096];
        snprintf(name, sizeof name, "shard-%u.log", (unsigned)shard);
        serve_path(path, sizeof path, j->folder, name);
        FILE *f = j->logs[shard] = fopen(path, mode);
        if (!f) {
            fprintf(stderr, "tide: cannot write the shard journal %s\n", path);
            j->failed = true;
        } else if (fseek(f, 0, SEEK_END) != 0 || ftell(f) < 0) {
            j->failed = true;
        } else if (ftell(f) == 0) { // A new log: whose records it has, and which checkpoint they follow
            uint8_t head[SERVE_LOG_HEAD];
            serve_u32(head, SERVE_LOG_MAGIC);
            serve_u32(head + 4u, SERVE_LOG_VERSION);
            serve_u64(head + 8u, j->world_id[0]);
            serve_u64(head + 16u, j->world_id[1]);
            serve_u32(head + 24u, j->sequence);
            serve_u32(head + 28u, shard);
            if (fwrite(head, 1, sizeof head, f) != sizeof head) j->failed = true;
            j->made = true;
        }
    }
    return j->logs[shard];
}

static void serve_record(void *user, const uint32_t shard, const uint32_t tick, const uint8_t *record, const uint32_t size)
{
    (void)tick;
    serve_world *j = user;
    FILE *f = serve_log(j, shard, "ab");
    const uint8_t length[4] = {(uint8_t)size, (uint8_t)(size >> 8), (uint8_t)(size >> 16), (uint8_t)(size >> 24)};
    if (f && (fwrite(length, 1, 4, f) != 4 || fwrite(record, 1, size, f) != size)) j->failed = true;
    j->written = true;
}

// Every record so far on the disk before anything the shards sent goes on;
// a journal that can't be kept stops the server, rather than go on unkept.
// An update with no tick wrote nothing: syncing then would cost a flush of
// the disk's cache on Windows, an update at a time.
static void serve_flush(void *user)
{
    serve_world *j = user;
    for (uint32_t i = 0; j->written && i < j->log_room; i++) {
        if (j->logs[i] && !serve_sync(j->logs[i])) j->failed = true;
    }
    if (j->made && !serve_sync_folder(j->folder)) j->failed = true;
    j->written = j->made = false;
    if (j->failed) {
        fputs("tide: the shard journal couldn't be kept; stopping\n", stderr);
        exit(1);
    }
}

static void serve_remove_log(const serve_world *j, const uint32_t shard)
{
    char name[32], path[4096];
    snprintf(name, sizeof name, "shard-%u.log", (unsigned)shard);
    serve_path(path, sizeof path, j->folder, name);
    if (remove(path) != 0 && errno != ENOENT) {
        fprintf(stderr, "tide: cannot remove the shard journal %s; stopping\n", path);
        exit(1);
    }
}

// Every shard's log goes: the ones open, and the ones the folder had on starting
static void serve_remove_logs(serve_world *j)
{
    for (uint32_t i = 0; i < j->log_room; i++) {
        if (!j->logs[i]) continue;
        fclose(j->logs[i]);
        j->logs[i] = NULL;
        serve_remove_log(j, i);
    }
    for (uint32_t i = 0; i < j->left_count; i++) serve_remove_log(j, j->left[i]);
    free(j->left);
    j->left = NULL;
    j->left_count = 0;
}

// A checkpoint goes in whole beside the last, then over it, and the records
// before it go
static void serve_checkpoint(void *user, const uint8_t *data, const uint32_t size)
{
    serve_world *j = user;
    char path[4096], next[4096];
    serve_path(path, sizeof path, j->folder, "checkpoint");
    serve_path(next, sizeof next, j->folder, "checkpoint.next");
    FILE *f = fopen(next, "wb");
    const bool written = f && fwrite(data, 1, size, f) == size && serve_sync(f);
    if (f) fclose(f);
#ifdef _WIN32
    const bool moved = written && MoveFileExA(next, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
#else
    const bool moved = written && rename(next, path) == 0;
#endif
    if (!moved || !serve_sync_folder(j->folder)) {
        fprintf(stderr, "tide: cannot write the shard checkpoint %s; stopping\n", path);
        exit(1);
    }
    // The logs made from here on follow this checkpoint, and say so
    tide_shard_checkpoint_id id;
    if (tide_shard_checkpoint_identity(data, size, &id)) {
        j->world_id[0] = id.world_id[0];
        j->world_id[1] = id.world_id[1];
        j->sequence = id.sequence;
    }
    serve_remove_logs(j); // The records before it go
}

// A file beside the journal's, written whole and kept: false, with nothing of that name left half written
static bool serve_write(const char *folder, const char *name, const uint8_t *data, const uint32_t size)
{
    char path[4096];
    serve_path(path, sizeof path, folder, name);
    FILE *f = fopen(path, "wb");
    const bool written = f && fwrite(data, 1, size, f) == size && serve_sync(f);
    if (f) fclose(f);
    if (!written) remove(path);
    return written && serve_sync_folder(folder);
}

static void serve_hex(char *out, const uint8_t *bytes, const uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) snprintf(out + 2u * i, 3u, "%02x", (unsigned)bytes[i]);
}

// A journal file's bytes. A file that isn't there is told from one that is and can't be read: the first is a
// shard with no records (or a folder with no world yet), the second a world that mustn't be gone on from
// without it. `why` says what kept an unreadable one from being read.
typedef enum serve_got { SERVE_ABSENT, SERVE_READ, SERVE_UNREADABLE } serve_got;

static serve_got serve_read(const char *path, uint8_t **bytes, uint32_t *size, const char **why)
{
    *bytes = NULL;
    *size = 0;
    errno = 0;
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (errno == ENOENT) return SERVE_ABSENT;
        *why = errno ? strerror(errno) : "it couldn't be opened";
        return SERVE_UNREADABLE;
    }
    *why = NULL;
#ifndef _WIN32
    // A folder opens for reading on these, and some say it's empty
    struct stat st;
    if (fstat(fileno(f), &st) != 0) *why = strerror(errno);
    else if (!S_ISREG(st.st_mode)) *why = "it isn't a file";
#endif
    long n = -1;
    errno = 0;
    if (!*why && (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0))
        *why = errno ? strerror(errno) : "its size couldn't be read";
    if (!*why && (uint64_t)n > UINT32_MAX) *why = "it's over 4 GiB";
    uint8_t *read = NULL;
    if (!*why) {
        read = malloc((size_t)n ? (size_t)n : 1u);
        if (!read) tide_out_of_memory();
        errno = 0;
        if (fread(read, 1, (size_t)n, f) != (size_t)n) *why = errno ? strerror(errno) : "it ended before its size";
    }
    fclose(f);
    if (*why) {
        free(read);
        return SERVE_UNREADABLE;
    }
    *bytes = read;
    *size = (uint32_t)n;
    return SERVE_READ;
}

// `shard-<id>.log`'s id, written as serve_log writes it; false for any other name
static bool serve_log_id(const char *name, uint32_t *id)
{
    if (strncmp(name, "shard-", 6u) != 0) return false;
    const char *p = name + 6u;
    if (*p < '0' || *p > '9' || (*p == '0' && p[1] != '.')) return false;
    uint64_t n = 0;
    for (; *p >= '0' && *p <= '9'; p++) {
        n = n * 10u + (unsigned)(*p - '0');
        if (n > UINT32_MAX) return false;
    }
    if (strcmp(p, ".log") != 0) return false;
    *id = (uint32_t)n;
    return true;
}

static void serve_left(serve_world *j, const char *name)
{
    uint32_t id;
    if (!serve_log_id(name, &id)) return;
    uint32_t *grown = realloc(j->left, (j->left_count + 1u) * sizeof *grown);
    if (!grown) tide_out_of_memory();
    j->left = grown;
    j->left[j->left_count++] = id;
}

// Every shard log the folder has, by listing it: ids a merge ended are never given again, so the shards a
// world has can be any distance apart, and a shard started since the checkpoint has a log the checkpoint
// doesn't know of. False when the folder can't be listed; one that isn't there has none.
static bool serve_list(serve_world *j, const char **why)
{
#ifdef _WIN32
    char pattern[4096];
    serve_path(pattern, sizeof pattern, j->folder, "shard-*.log");
    WIN32_FIND_DATAA found;
    const HANDLE list = FindFirstFileA(pattern, &found);
    if (list == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        *why = "Windows wouldn't list it";
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
    }
    do serve_left(j, found.cFileName);
    while (FindNextFileA(list, &found));
    const bool ok = GetLastError() == ERROR_NO_MORE_FILES;
    FindClose(list);
    *why = "Windows stopped listing it";
    return ok;
#else
    DIR *list = opendir(j->folder);
    if (!list) {
        *why = strerror(errno);
        return errno == ENOENT;
    }
    for (;;) {
        errno = 0;
        const struct dirent *entry = readdir(list);
        if (!entry) break;
        serve_left(j, entry->d_name);
    }
    const bool ok = errno == 0;
    *why = strerror(errno);
    closedir(list);
    return ok;
#endif
}

static void serve_forget_read(serve_world *j)
{
    for (uint32_t i = 0; i < j->read_count; i++) {
        free(j->read[i].bytes);
        free(j->read[i].records);
    }
    free(j->read);
    j->read = NULL;
    j->read_count = j->read_last = 0;
}

// Every log the folder has, read whole before the world is made again. A log that's there and can't be read
// fails: its shard's ticks since the checkpoint are in it, and going on without them is another world. A
// record a crash cut short at a log's end wasn't kept (nothing its tick sent had gone on) and is passed over.
static bool serve_read_logs(serve_world *j, const tide_shard_checkpoint_id *checkpoint)
{
    j->read = calloc(j->left_count ? j->left_count : 1u, sizeof *j->read);
    if (!j->read) tide_out_of_memory();
    for (uint32_t i = 0; i < j->left_count; i++) {
        char name[32], path[4096];
        snprintf(name, sizeof name, "shard-%u.log", (unsigned)j->left[i]);
        serve_path(path, sizeof path, j->folder, name);
        serve_kept *k = &j->read[j->read_count];
        uint32_t size = 0;
        const char *why = NULL;
        const serve_got got = serve_read(path, &k->bytes, &size, &why);
        if (got == SERVE_ABSENT) continue; // Gone since the folder was listed
        if (got == SERVE_UNREADABLE) {
            fprintf(stderr, "tide: cannot read the shard journal %s (%s), which has a shard's ticks since the checkpoint. "
                            "The folder is left as it was: close whatever holds the file, or let this user read it, "
                            "and start the server again\n", path, why);
            return false;
        }
        k->shard = j->left[i];
        j->read_count++;
        uint32_t at = 0, room = 0;
        // Its head: a log is this world's, and follows this checkpoint or one before it. A crash between a
        // checkpoint's move into place and the removal of the logs before it leaves those, which are passed over.
        // Another world's, or one that follows a later checkpoint than the folder has, is a folder put together
        // from two, and nothing is gone on from.
        if (size >= SERVE_LOG_HEAD) {
            const uint8_t *head = k->bytes;
            const uint32_t sequence = serve_get_u32(head + 24u);
            if (serve_get_u32(head) != SERVE_LOG_MAGIC || serve_get_u32(head + 4u) != SERVE_LOG_VERSION) {
                fprintf(stderr, "tide: %s isn't a shard journal this server reads. The folder is left as it was\n", path);
                return false;
            }
            if (serve_get_u64(head + 8u) != checkpoint->world_id[0] || serve_get_u64(head + 16u) != checkpoint->world_id[1]
                || serve_get_u32(head + 28u) != k->shard || sequence > checkpoint->sequence) {
                fprintf(stderr, "tide: %s is another world's journal, or follows a later checkpoint than the one beside "
                                "it. The folder is left as it was: put back the checkpoint and the logs that were kept "
                                "together\n", path);
                return false;
            }
            at = SERVE_LOG_HEAD;
            if (sequence < checkpoint->sequence) {
                fprintf(stderr, "tide: %s is from before the checkpoint; it's passed over\n", path);
                continue;
            }
        }
        while (at >= SERVE_LOG_HEAD && size - at >= 4u) {
            const uint8_t *p = k->bytes + at;
            const uint32_t n = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
            if (n > size - at - 4u) break;
            if (k->count == room) {
                room = room ? room * 2u : 64u;
                uint32_t *grown = realloc(k->records, room * sizeof *grown);
                if (!grown) tide_out_of_memory();
                k->records = grown;
            }
            k->records[k->count++] = at;
            at += 4u + n;
        }
        if (at != size)
            fprintf(stderr, "tide: %s ends in %u bytes of a record cut short as it was written; they're passed over\n",
                    path, (unsigned)(size - at));
    }
    return true;
}

static bool serve_records(void *user, const uint32_t shard, const uint32_t index, const uint8_t **record, uint32_t *size)
{
    serve_world *j = user;
    if (j->read_last >= j->read_count || j->read[j->read_last].shard != shard) {
        uint32_t i = 0;
        while (i < j->read_count && j->read[i].shard != shard) i++;
        if (i == j->read_count) return false; // A shard with no log has no ticks since the checkpoint
        j->read_last = i;
    }
    const serve_kept *k = &j->read[j->read_last];
    if (index >= k->count) return false;
    const uint8_t *p = k->bytes + k->records[index];
    *record = p + 4u;
    *size = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
    return true;
}

// What a journal folder holds for this build, found by reading all of it and changing none of it
typedef enum serve_found {
    SERVE_NO_WORLD, // Nothing: a new world starts
    SERVE_GOES_ON,  // This build's checkpoint, and the records since
    SERVE_UPGRADES, // Another build's sealed checkpoint, asked for with --shard-upgrade
    SERVE_REFUSED,  // Said why
} serve_found;

static serve_found serve_open(serve_world *j, const tide_game *game, const bool upgrade, uint8_t **checkpoint,
                              uint32_t *checkpoint_size, tide_shard_checkpoint_id *id)
{
    char path[4096];
    serve_path(path, sizeof path, j->folder, "checkpoint");
    const char *why = NULL;
    const serve_got got = serve_read(path, checkpoint, checkpoint_size, &why);
    if (got == SERVE_UNREADABLE) {
        fprintf(stderr, "tide: cannot read the shard checkpoint %s (%s), so the world it keeps can't go on. "
                        "The folder is left as it was: close whatever holds the file, or let this user read it, "
                        "and start the server again\n", path, why);
        return SERVE_REFUSED;
    }
    if (!serve_list(j, &why)) {
        fprintf(stderr, "tide: cannot list the shard journal folder %s (%s) to find every shard's records. "
                        "The folder is left as it was\n", j->folder, why);
        return SERVE_REFUSED;
    }
    if (got == SERVE_ABSENT) {
        if (!j->left_count) return SERVE_NO_WORLD;
        // A world's first checkpoint is kept before its first record, so records alone are a folder that lost its
        // checkpoint: what they'd go on from
        fprintf(stderr, "tide: the shard journal in %s has records and no checkpoint, which a world has from its start. "
                        "The folder is left as it was: put its checkpoint back, or move the folder away to start anew\n",
                j->folder);
        return SERVE_REFUSED;
    }
    if (!tide_shard_checkpoint_identity(*checkpoint, *checkpoint_size, id)) {
        fprintf(stderr, "tide: %s isn't a checkpoint this server reads: another version of Tide wrote it, or it's "
                        "damaged. The folder is left as it was: start it with the server that kept it\n", path);
        return SERVE_REFUSED;
    }
    const bool fits = tide_shard_checkpoint_fits(*checkpoint, *checkpoint_size, game->simulation, game->hash)
                      == TIDE_SHARD_CHECKPOINT_FITS;
    if (!fits) {
        char theirs[17], ours[17];
        serve_hex(theirs, id->simulation, 8u);
        serve_hex(ours, game->simulation, 8u);
        if (!upgrade) {
            fprintf(stderr, "tide: the world in %s was kept by another build of the game (simulation %s; this one is %s), "
                            "and a world goes on only with the build that kept it. The folder is left as it was: start "
                            "that build's server on it, or to move the world to this build, stop that server (which "
                            "seals the world) and start this one with --shard-upgrade\n", j->folder, theirs, ours);
            return SERVE_REFUSED;
        }
        if (!id->sealed) {
            fprintf(stderr, "tide: the world in %s (simulation %s) isn't sealed: its journal has ticks only the build "
                            "that kept it can run again. The folder is left as it was: seal it with that build's server "
                            "(--shard-seal, or start it and stop it), then upgrade\n", j->folder, theirs);
            return SERVE_REFUSED;
        }
        if (id->layout != game->hash) {
            fprintf(stderr, "tide: the world in %s (simulation %s) lays its match state out differently from this "
                            "build, and --shard-upgrade takes a world whose layout is the same. The folder is left as "
                            "it was\n", j->folder, theirs);
            return SERVE_REFUSED;
        }
    }
    if (!serve_read_logs(j, id)) return SERVE_REFUSED;
    if (fits) return SERVE_GOES_ON;
    for (uint32_t i = 0; i < j->read_count; i++) {
        if (!j->read[i].count) continue;
        fprintf(stderr, "tide: the world in %s is sealed and has records after it, which only the build that kept it "
                        "can run. The folder is left as it was: seal it again with that build's server\n", j->folder);
        return SERVE_REFUSED;
    }
    return SERVE_UPGRADES;
}

// A world from what serve_open found: the checkpoint and its records run again, or another build's sealed
// checkpoint taken up as this build's. NULL, said why.
static tide_shards *serve_world_from(serve_world *j, const tide_shards_desc *shards, const serve_found found,
                                     const uint8_t *checkpoint, const uint32_t checkpoint_size, const double now)
{
    tide_shards *world = NULL;
    if (found == SERVE_GOES_ON) {
        world = tide_shards_recover(shards, checkpoint, checkpoint_size, serve_records, j, now);
        if (!world)
            fprintf(stderr, "tide: the shard journal in %s doesn't make the world it was; move it away to start anew\n", j->folder);
    } else {
        uint32_t size = 0;
        uint8_t *taken = tide_shard_checkpoint_rewrite(checkpoint, checkpoint_size, NULL, NULL, shards->game->simulation,
                                                       shards->game->hash, &size);
        if (taken) world = tide_shards_load(shards, taken, size, now);
        free(taken);
        if (!world) fprintf(stderr, "tide: this build can't take up the sealed world in %s\n", j->folder);
    }
    return world;
}

// --shard-verify: whether the folder's world goes on with this build, and where each shard would be. Nothing is
// written, no store is bound and nothing listens. With --shard-upgrade, another build's sealed world is taken
// up in memory and run a while.
static int serve_verify(const tide_serve_desc *desc, const char *folder, const bool upgrade, tide_shards_desc shards)
{
    serve_world kept = {.folder = folder};
    uint8_t *checkpoint = NULL;
    uint32_t checkpoint_size = 0;
    tide_shard_checkpoint_id id;
    const serve_found found = serve_open(&kept, desc->game, upgrade, &checkpoint, &checkpoint_size, &id);
    if (found == SERVE_NO_WORLD) fprintf(stderr, "tide: %s has no world to verify\n", folder);
    tide_shards *world = NULL;
    double now = 1.0;
    if (found == SERVE_GOES_ON || found == SERVE_UPGRADES)
        world = serve_world_from(&kept, &shards, found, checkpoint, checkpoint_size, now);
    for (uint32_t i = 0; world && found == SERVE_UPGRADES && i < 240u; i++) { // Two seconds of it as this build's
        now += 1.0 / 120.0;
        tide_shards_update(world, now);
    }
    if (world) {
        uint32_t size = 0;
        uint8_t *saved = tide_shards_save(world, &size);
        for (uint32_t i = 0; i < tide_shards_count(world); i++) {
            const void *w = tide_shards_world(world, i);
            const uint8_t *bytes = NULL;
            uint32_t n = 0, tick = 0;
            if (!w || !tide_shard_checkpoint_world(saved, size, i, &bytes, &n, &tick)) continue;
            fprintf(stderr, "tide: shard %u at tick %u, world %016llx\n", (unsigned)i, (unsigned)tick,
                    (unsigned long long)desc->game->hash_world(w));
        }
        free(saved);
        fprintf(stderr, found == SERVE_UPGRADES ? "tide: this build takes up the sealed world in %s; nothing was written\n"
                                                : "tide: the world in %s goes on with this build; nothing was written\n", folder);
        tide_shards_destroy(world);
    }
    const bool verified = world != NULL;
    serve_forget_read(&kept);
    free(kept.left);
    free(checkpoint);
    return verified ? 0 : 1;
}

static void serve_nap(void)
{
#ifdef _WIN32
    Sleep(1);
#else
    const struct timespec pause = {.tv_nsec = 1000000};
    nanosleep(&pause, NULL);
#endif
}

int tide_serve(const tide_serve_desc *desc)
{
    if (!desc || !desc->game || !desc->pack_name) return 2;
    const char *journal = NULL;
    uint32_t port = 7777, frames = 0;
    // A sharded game's world (tide/shards.h): regions along x to start with,
    // each this wide (0: 16 times its reach), and the entities a region's
    // shard owns before it splits (0: never)
    uint32_t regions = 1, shard_width = 0, shard_most = 0;
    bool udp_only = false, verify = false, seal = false, upgrade = false;
    tide_hosting_desc host = {.game = desc->game, .entropy = tide_platform_entropy, .secure_peer = serve_secure};
    for (int i = 1; i < desc->argc; i++) {
        const char *a = desc->argv[i];
        if (strcmp(a, "--anonymous") == 0) {
            host.allow_anonymous_create = host.allow_anonymous_join = true;
        } else if (strcmp(a, "--udp-only") == 0) udp_only = true;
        else if (strcmp(a, "--shard-verify") == 0) verify = true;
        else if (strcmp(a, "--shard-seal") == 0) seal = true;
        else if (strcmp(a, "--shard-upgrade") == 0) upgrade = true;
        else if (strcmp(a, "--shard-journal") == 0 && i + 1 < desc->argc && desc->argv[i + 1][0]) journal = desc->argv[++i];
        else {
            uint32_t value;
            const uint32_t most = strcmp(a, "--port") == 0 ? 65535u
                                : strcmp(a, "--max-matches") == 0 ? 4096u
                                : strcmp(a, "--max-connections") == 0 ? TIDE_PLAYERS_MOST
                                : strcmp(a, "--frames") == 0 ? 1000000000u
                                : strcmp(a, "--shards") == 0 ? 256u
                                : strcmp(a, "--shard-width") == 0 || strcmp(a, "--shard-most") == 0 ? 2000000000u : 0u;
            if (!most || i + 1 >= desc->argc || !serve_number(desc->argv[++i], most, &value) || !value) {
                fprintf(stderr, "tide: invalid server option %s; use --port, --max-matches, --max-connections, "
                                "--anonymous, --udp-only, --frames, or for a sharded game --shards, --shard-width, "
                                "--shard-most and --shard-journal <folder>, with --shard-verify, --shard-seal or "
                                "--shard-upgrade\n", a);
                return 2;
            }
            if (strcmp(a, "--port") == 0) port = value;
            else if (strcmp(a, "--max-matches") == 0) host.max_matches = value;
            else if (strcmp(a, "--max-connections") == 0) host.max_connections = value;
            else if (strcmp(a, "--shards") == 0) regions = value;
            else if (strcmp(a, "--shard-width") == 0) shard_width = value;
            else if (strcmp(a, "--shard-most") == 0) shard_most = value;
            else frames = value;
        }
    }
    if ((regions > 1u || shard_width || shard_most || journal) && !desc->game->shards) {
        fputs("tide: --shards, --shard-width, --shard-most and --shard-journal are for a sharded game: "
              "'settings { shards = true; }'\n", stderr);
        return 2;
    }
    if ((verify || seal || upgrade) && !journal) {
        fputs("tide: --shard-verify, --shard-seal and --shard-upgrade are for the world in a folder: "
              "--shard-journal <folder>\n", stderr);
        return 2;
    }
    if (seal && (verify || upgrade)) {
        fputs("tide: --shard-seal seals a world for the build that kept it; verify or upgrade it in a run of its own\n", stderr);
        return 2;
    }
    if (tide_hidden() && !frames && !verify && !seal) {
        fputs("tide: TIDE_HIDDEN is set; give the server --frames <n> to bound the run\n", stderr);
        return 2;
    }
    if (desc->entry) desc->entry(&host);
    if (desc->main_is_local && !host.start) {
        fputs("tide: Main is local; define tide_server_entry and select a match scene in its start callback\n", stderr);
        return 2;
    }
    if (!host.authorize && !host.allow_anonymous_create && !host.allow_anonymous_join) {
        fputs("tide: configure authentication in tide_server_entry, or use --anonymous for quick startup\n", stderr);
        return 2;
    }
    if (!serve_load(desc)) return 1;
    host.jobs = tide_platform_jobs();
    // A sharded world's shape, for a new one: a world gone on with has its checkpoint's
    const double shard_wide = shard_width ? (double)shard_width : desc->game->shards ? 16.0 * desc->game->shards->range : 0.0;
    const double shard_half = 0.5 * shard_wide * (double)regions;
    if (verify) {
        const tide_shards_desc d = {.game = desc->game, .world = {{-shard_half, -shard_half, -shard_half},
                                                                  {shard_half, shard_half, shard_half}},
                                    .split = {regions, 1u, 1u}, .most_entities = shard_most, .jobs = host.jobs};
        const int verified = serve_verify(desc, journal, upgrade, d);
        tide_pack_unload();
        return verified;
    }
    // What its matches save (Storage.Save in match code) is kept in the game's own folder, as a player's saves are:
    // its appId's, or for a game with none, dev.tide.<its name>, the name its player build goes by (the pack's, less
    // the "-server" a server's has). Only for a game that uses the store: the provider makes its folders at once.
    // (A build that first says Storage in a reload keeps nothing until the server starts again.)
    if (!host.io && desc->game->stored) {
        char fallback[256];
        const char *id = desc->game->app_id;
        if (!id) {
            size_t n = strlen(desc->pack_name);
            if (n > 7u && strcmp(desc->pack_name + n - 7u, "-server") == 0) n -= 7u;
            snprintf(fallback, sizeof fallback, "dev.tide.%.*s", (int)(n < 200u ? n : 200u), desc->pack_name);
            for (char *p = fallback; *p; ++p)
                if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '.')) *p = '_';
            id = fallback;
        }
        // A world with a journal keeps its store in the journal's folder: the folder is all of the world, to copy,
        // back up and put back as one
        char store[4096];
        if (journal) serve_path(store, sizeof store, journal, "store");
        host.io = tide_platform_io_init(id, journal ? store : NULL);
    }
    if (!(udp_only ? tide_platform_udp_open((uint16_t)port, &host.transport)
                   : tide_platform_host_open((uint16_t)port, &host.transport))) {
        fprintf(stderr, "tide: cannot listen on server port %u\n", (unsigned)port);
        tide_pack_unload();
        return 1;
    }
    tide_hosting *h = tide_hosting_create(&host, (double)tide_time_now_ns() * 1e-9);
    if (!h) { host.transport.close(host.transport.self); tide_pack_unload(); return 1; }
    // A sharded game's server runs one world, each of its shards a match, and
    // a player asking for match 0 joins it at its root
    tide_shards *world = NULL;
    serve_world kept = {.host = h, .folder = journal};
    if (desc->game->shards) {
        tide_shards_desc shards = {.game = desc->game, .world = {{-shard_half, -shard_half, -shard_half},
                                                                 {shard_half, shard_half, shard_half}},
                                   .split = {regions, 1u, 1u}, .most_entities = shard_most,
                                   .seed = (uint32_t)tide_platform_entropy() & 0x7FFFFFFFu, .jobs = host.jobs,
                                   .io = host.io, // Where a recovery writes again what its ticks saved
                                   .world_id = {tide_platform_entropy(), tide_platform_entropy()},
                                   .host_start = serve_world_start, .host_stop = serve_world_stop, .user = &kept};
        const double now = (double)tide_time_now_ns() * 1e-9;
        uint32_t checkpoint_size = 0;
        uint8_t *checkpoint = NULL;
        serve_found found = SERVE_NO_WORLD;
        tide_shard_checkpoint_id id = {0};
        if (journal) {
            shards.journal = serve_record;
            shards.flush = serve_flush;
            shards.checkpoint = serve_checkpoint;
            // Nothing in the folder changes until all of it has been read: a checkpoint or a log that's
            // there and can't be read ends the start, rather than a new world or one short of its ticks;
            // and so does a world another build kept
            found = serve_open(&kept, desc->game, upgrade, &checkpoint, &checkpoint_size, &id);
            if (found == SERVE_NO_WORLD && seal) {
                fprintf(stderr, "tide: %s has no world to seal\n", journal);
                found = SERVE_REFUSED;
            }
            if (found == SERVE_UPGRADES) {
                // The world as its build left it stays beside the one this build makes of it, by that build's name
                char name[64] = "checkpoint.";
                serve_hex(name + 11u, id.simulation, 8u);
                if (!serve_write(journal, name, checkpoint, checkpoint_size)) {
                    fprintf(stderr, "tide: cannot keep the sealed world as %s/%s before taking it up. The folder's "
                                    "checkpoint is as it was\n", journal, name);
                    found = SERVE_REFUSED;
                } else {
                    fprintf(stderr, "tide: the sealed world is kept as %s/%s, for the build that sealed it\n", journal, name);
                }
            }
            if (found != SERVE_REFUSED && found != SERVE_NO_WORLD)
                world = serve_world_from(&kept, &shards, found, checkpoint, checkpoint_size, now);
            serve_forget_read(&kept);
            free(checkpoint);
            if (found == SERVE_REFUSED || (found != SERVE_NO_WORLD && !world)) {
                free(kept.left);
                tide_hosting_destroy(h);
                tide_pack_unload();
                return 1;
            }
        }
        if (!world) {
            world = tide_shards_create(&shards, now);
            if (!world) {
                fputs("tide: the sharded world couldn't start: --max-matches must take every shard\n", stderr);
                tide_hosting_destroy(h);
                tide_pack_unload();
                return 1;
            }
            fprintf(stderr, "tide: sharded world of %u region%s; players join match 0\n", (unsigned)regions, regions == 1u ? "" : "s");
        }
        if (journal) {
            // A checkpoint of it at once. A new world's, so a server that ends before its next one has a world
            // to go on from, with what its start saved in the store first; one gone on with, so no record goes
            // after one a crash cut short; one taken up from another build, so the folder is this build's.
            uint32_t size = 0;
            uint8_t *saved = tide_shards_settle(world) ? tide_shards_save(world, &size) : NULL;
            if (!saved) {
                fprintf(stderr, "tide: the store in %s/store couldn't keep what the world saved; stopping\n", journal);
                exit(1);
            }
            serve_checkpoint(&kept, saved, size);
            free(saved);
            if (found == SERVE_GOES_ON) fprintf(stderr, "tide: the sharded world goes on from its journal in %s\n", journal);
            if (found == SERVE_UPGRADES) fprintf(stderr, "tide: the sharded world in %s goes on as this build's\n", journal);
        }
    }
    serve_stopped = 0;
    void (*previous_int)(int) = signal(SIGINT, serve_stop);
    void (*previous_term)(int) = signal(SIGTERM, serve_stop);
    fprintf(stderr, "tide: dedicated server listening on port %u\n", (unsigned)port);
    if (host.ready) host.ready(host.user, h);
#ifdef _WIN32
    // A per-process timer avoids Sleep(1)'s coarse system timer resolution,
    // without changing the global multimedia timer settings.
    HANDLE timer = CreateWaitableTimerExW(NULL, NULL, 2u, TIMER_ALL_ACCESS);
#endif
    uint32_t update = 0;
    char shown[TIDE_ROOM_CODE_LENGTH + 1] = {0};
    while (!seal && !serve_stopped && (!frames || update++ < frames)) {
        const double now = (double)tide_time_now_ns() * 1e-9;
        if (host.poll) host.poll(host.user, h, now);
        tide_hosting_update(h, now);
        if (world) tide_shards_update(world, now); // What the shards' ticks sent each other, and their ghosts
        if (!udp_only) {
            char room[TIDE_ROOM_CODE_LENGTH + 1] = {0};
            tide_platform_room_code(room, sizeof room);
            if (*room && strcmp(shown, room) != 0) {
                memcpy(shown, room, sizeof shown);
                fprintf(stderr, "tide: server room %s; join %s/0 to request a match\n", room, room);
            }
        }
#ifdef _WIN32
        const LARGE_INTEGER due = {.QuadPart = -10000};
        if (timer && SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE)) WaitForSingleObject(timer, INFINITE);
        else Sleep(1);
#else
        const struct timespec pause = {.tv_nsec = 1000000};
        nanosleep(&pause, NULL);
#endif
    }
#ifdef _WIN32
    if (timer) CloseHandle(timer);
#endif
    if (previous_int != SIG_ERR) signal(SIGINT, previous_int);
    if (previous_term != SIG_ERR) signal(SIGTERM, previous_term);
    // A world with a journal is sealed as its server stops: a checkpoint with nothing on its way between shards,
    // nothing waiting on a border and nothing owed to the store, and no log after it. It's what another build
    // takes up (--shard-upgrade). A world that doesn't come to rest in two seconds keeps its journal instead.
    bool sealed = false;
    if (world && journal) {
        const uint64_t until = tide_time_now_ns() + UINT64_C(2000000000);
        for (;;) {
            uint32_t size = 0;
            uint8_t *saved = tide_shards_seal(world, &size);
            if (saved) {
                serve_checkpoint(&kept, saved, size);
                free(saved);
                sealed = true;
                break;
            }
            if (tide_time_now_ns() >= until) break;
            const double now = (double)tide_time_now_ns() * 1e-9;
            tide_hosting_update(h, now);
            tide_shards_update(world, now);
            serve_nap();
        }
        fprintf(stderr, sealed ? "tide: the sharded world is sealed in %s\n"
                               : "tide: the sharded world didn't come to rest to be sealed; its journal in %s is kept, "
                                 "and this build goes on from it\n", journal);
    }
    tide_shards_destroy(world); // Its matches, before their host
    for (uint32_t i = 0; i < kept.log_room; i++) {
        if (kept.logs[i]) fclose(kept.logs[i]);
    }
    free(kept.logs);
    free(kept.left);
    tide_hosting_destroy(h);
    tide_pack_unload();
    return seal && !sealed ? 1 : 0;
}
#endif
