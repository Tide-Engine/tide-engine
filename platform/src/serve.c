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
#include <mach-o/dyld.h>
#include <time.h>
#include <unistd.h>
#else
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
// `shard-<id>.log`, each a u32 size and a record, over and over, from the
// checkpoint on. On starting, a folder with a checkpoint is the world again.
typedef struct serve_world {
    tide_hosting *host;
    const char *folder;
    FILE **logs;
    uint32_t log_room;
    uint32_t known; // Shards whose logs a journal read on starting may have left
    bool failed;
    // Recovering: each shard's records as read, back to back
    uint8_t **read;
    uint32_t *read_size;
    uint32_t read_count;
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
        j->logs[shard] = fopen(path, mode);
        if (!j->logs[shard]) {
            fprintf(stderr, "tide: cannot write the shard journal %s\n", path);
            j->failed = true;
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
}

// Every record so far on the disk before anything the shards sent goes on;
// a journal that can't be kept stops the server, rather than go on unkept
static void serve_flush(void *user)
{
    serve_world *j = user;
    for (uint32_t i = 0; i < j->log_room; i++) {
        if (j->logs[i] && !serve_sync(j->logs[i])) j->failed = true;
    }
    if (j->failed) {
        fputs("tide: the shard journal couldn't be kept; stopping\n", stderr);
        exit(1);
    }
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
    if (!moved) {
        fprintf(stderr, "tide: cannot write the shard checkpoint %s; stopping\n", path);
        exit(1);
    }
    // The records before it go: every shard's log, open or left from before
    const uint32_t ids = j->log_room > j->known ? j->log_room : j->known;
    for (uint32_t i = 0; i < ids; i++) {
        if (i < j->log_room && j->logs[i]) {
            fclose(j->logs[i]);
            j->logs[i] = NULL;
        }
        char name[32];
        snprintf(name, sizeof name, "shard-%u.log", (unsigned)i);
        serve_path(path, sizeof path, j->folder, name);
        if (remove(path) != 0 && errno != ENOENT) {
            fprintf(stderr, "tide: cannot remove %s after a checkpoint; stopping\n", path);
            exit(1);
        }
    }
}

static uint8_t *serve_read(const char *path, uint32_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    bool ok = fseek(f, 0, SEEK_END) == 0;
    const long n = ok ? ftell(f) : -1;
    ok = n >= 0 && (uint64_t)n <= UINT32_MAX && fseek(f, 0, SEEK_SET) == 0;
    uint8_t *bytes = ok ? malloc((size_t)n ? (size_t)n : 1u) : NULL;
    ok = bytes && fread(bytes, 1, (size_t)n, f) == (size_t)n;
    fclose(f);
    if (!ok) {
        free(bytes);
        return NULL;
    }
    *size = (uint32_t)n;
    return bytes;
}

static bool serve_records(void *user, const uint32_t shard, const uint32_t index, const uint8_t **record, uint32_t *size)
{
    serve_world *j = user;
    if (shard >= j->read_count || !j->read[shard]) return false;
    uint32_t at = 0;
    for (uint32_t i = 0;; i++) {
        if (at + 4u > j->read_size[shard]) return false;
        const uint8_t *p = j->read[shard] + at;
        const uint32_t n = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
        if (n > j->read_size[shard] - at - 4u) return false; // A record the crash cut short wasn't kept
        if (i == index) {
            *record = p + 4u;
            *size = n;
            return true;
        }
        at += 4u + n;
    }
}

int tide_serve(const tide_serve_desc *desc)
{
    if (!desc || !desc->game || !desc->pack_name) return 2;
    const char *journal = NULL;
    uint32_t port = 7777, frames = 0;
    // A sharded game's world (tide/shards.h): regions along x to start with,
    // each this wide (0: 16 times its shardRange), and the entities a region's
    // shard owns before it splits (0: never)
    uint32_t regions = 1, shard_width = 0, shard_most = 0;
    bool udp_only = false;
    tide_hosting_desc host = {.game = desc->game, .entropy = tide_platform_entropy, .secure_peer = serve_secure};
    for (int i = 1; i < desc->argc; i++) {
        const char *a = desc->argv[i];
        if (strcmp(a, "--anonymous") == 0) {
            host.allow_anonymous_create = host.allow_anonymous_join = true;
        } else if (strcmp(a, "--udp-only") == 0) udp_only = true;
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
                                "--shard-most and --shard-journal <folder>\n", a);
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
    if (tide_hidden() && !frames) {
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
        const double width = shard_width ? (double)shard_width : 16.0 * desc->game->shards->range;
        const double half = 0.5 * width * (double)regions;
        tide_shards_desc shards = {.game = desc->game, .world = {{-half, -half, -half}, {half, half, half}},
                                   .split = {regions, 1u, 1u}, .most_entities = shard_most,
                                   .seed = (uint32_t)tide_platform_entropy() & 0x7FFFFFFFu, .jobs = host.jobs,
                                   .host_start = serve_world_start, .host_stop = serve_world_stop, .user = &kept};
        const double now = (double)tide_time_now_ns() * 1e-9;
        uint32_t checkpoint_size = 0;
        uint8_t *checkpoint = NULL;
        if (journal) {
            shards.journal = serve_record;
            shards.flush = serve_flush;
            shards.checkpoint = serve_checkpoint;
            char path[4096];
            serve_path(path, sizeof path, journal, "checkpoint");
            checkpoint = serve_read(path, &checkpoint_size);
        }
        if (checkpoint) { // The world as it was, from its checkpoint and the records kept since
            for (uint32_t missing = 0; missing < 64u; kept.read_count++) { // Ids a merge ended have none
                char name[32], path[4096];
                snprintf(name, sizeof name, "shard-%u.log", (unsigned)kept.read_count);
                serve_path(path, sizeof path, journal, name);
                uint32_t n = 0;
                uint8_t *bytes = serve_read(path, &n);
                missing = bytes ? 0u : missing + 1u;
                kept.read = realloc(kept.read, (kept.read_count + 1u) * sizeof *kept.read);
                kept.read_size = realloc(kept.read_size, (kept.read_count + 1u) * sizeof *kept.read_size);
                if (!kept.read || !kept.read_size) tide_out_of_memory();
                kept.read[kept.read_count] = bytes;
                kept.read_size[kept.read_count] = n;
            }
            kept.known = kept.read_count;
            world = tide_shards_recover(&shards, checkpoint, checkpoint_size, serve_records, &kept, now);
            for (uint32_t i = 0; i < kept.read_count; i++) free(kept.read[i]);
            free(kept.read);
            free(kept.read_size);
            kept.read = NULL;
            kept.read_count = 0;
            free(checkpoint);
            if (!world) {
                fprintf(stderr, "tide: the shard journal in %s doesn't make the world it was; move it away to start anew\n", journal);
                tide_hosting_destroy(h);
                tide_pack_unload();
                return 1;
            }
            // A checkpoint of it at once, so no record goes after one a crash cut short
            uint32_t size = 0;
            uint8_t *saved = tide_shards_save(world, &size);
            serve_checkpoint(&kept, saved, size);
            free(saved);
            fprintf(stderr, "tide: the sharded world goes on from its journal in %s\n", journal);
        } else {
            world = tide_shards_create(&shards, now);
            if (!world) {
                fputs("tide: the sharded world couldn't start: --max-matches must take every shard\n", stderr);
                tide_hosting_destroy(h);
                tide_pack_unload();
                return 1;
            }
            fprintf(stderr, "tide: sharded world of %u region%s; players join match 0\n", (unsigned)regions, regions == 1u ? "" : "s");
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
    while (!serve_stopped && (!frames || update++ < frames)) {
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
    tide_shards_destroy(world); // Its matches, before their host
    for (uint32_t i = 0; i < kept.log_room; i++) {
        if (kept.logs[i]) fclose(kept.logs[i]);
    }
    free(kept.logs);
    tide_hosting_destroy(h);
    tide_pack_unload();
    return 0;
}
#endif
