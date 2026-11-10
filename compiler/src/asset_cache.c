#if !defined(_WIN32) && !defined(__APPLE__)
#define _DEFAULT_SOURCE
#endif
#include "asset_cache.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "folders.h"
#include "tide/sha256.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

// The code the entries come from (compiler/CMakeLists.txt): a folder of its own, so that what another importer made
// is let go of as a whole
static const char importers[] = TIDE_ASSET_CACHE_IMPORTERS;
#define FOLDER_NAME_CHARS 16

void asset_cache_key(uint8_t out[32], const char *kind, const uint8_t source[32], const void *variant, const size_t size)
{
    tide_sha256 h;
    tide_sha256_init(&h);
    const uint32_t format[] = {2, 0x01020304, sizeof(float)};
    tide_sha256_add(&h, format, sizeof format);
    tide_sha256_add(&h, importers, sizeof importers);
    tide_sha256_add(&h, kind, strlen(kind) + 1);
    tide_sha256_add(&h, source, 32);
    if (size) tide_sha256_add(&h, variant, size);
    tide_sha256_end(&h, out);
}

static bool cache_folder(char path[4096], const char *folder)
{
    return folder && *folder
        && snprintf(path, 4096, "%s/.tide/assets/imports/%.*s", folder, FOLDER_NAME_CHARS, importers) < 4096;
}

bool asset_cache_path(char path[4096], const char *folder, const uint8_t key[32])
{
    char hex[65], directory[4096];
    if (!cache_folder(directory, folder)) return false;
    for (int i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", key[i]);
    return snprintf(path, 4096, "%s/%s.bin", directory, hex) < 4096;
}

// ---------------------------------------------------------------------------
// When an entry was last used: its file's last-write time, as folder_file_info gives it, which a read moves to now
// once a day at most, so that a warm build writes nothing

#ifdef _WIN32
enum { TICKS_PER_SECOND = 10000000 }; // A FILETIME's
#else
enum { TICKS_PER_SECOND = 1000000000 };
#endif
#define TICKS_PER_DAY (86400ull * TICKS_PER_SECOND)

static unsigned long long now_ticks(void)
{
#ifdef _WIN32
    FILETIME t;
    GetSystemTimeAsFileTime(&t);
    return ((unsigned long long)t.dwHighDateTime << 32) | t.dwLowDateTime;
#else
    struct timespec t;
    if (timespec_get(&t, TIME_UTC) != TIME_UTC) return 0;
    return (unsigned long long)t.tv_sec * 1000000000u + (unsigned long long)t.tv_nsec;
#endif
}

// Whether `then` is at least `days` before `now`: never for a time after now, which a clock set back gives
static bool days_before(const unsigned long long then, const unsigned long long now, const unsigned days)
{
    return then <= now && now - then >= days * TICKS_PER_DAY;
}

static void touch(const char *path)
{
#ifdef _WIN32
    HANDLE file = CreateFileA(path, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    SetFileTime(file, NULL, NULL, &now);
    CloseHandle(file);
#else
    utimensat(AT_FDCWD, path, NULL, 0);
#endif
}

bool asset_cache_has(const char *folder, const uint8_t key[32])
{
    char path[4096];
    unsigned long long bytes;
    return asset_cache_path(path, folder, key) && folder_file_info(path, &bytes, NULL) && bytes > 64
        && bytes - 64 <= ASSET_CACHE_MAX_BYTES;
}

uint8_t *asset_cache_read(const char *folder, const uint8_t key[32], size_t *size)
{
    char path[4096];
    unsigned long long bytes, written;
    if (!asset_cache_path(path, folder, key) || !folder_file_info(path, &bytes, &written)
        || bytes <= 64 || bytes - 64 > ASSET_CACHE_MAX_BYTES) return NULL;
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    const size_t count = (size_t)(bytes - 64);
    uint8_t *data = malloc(count), header[64], digest[32];
    bool ok = data && fread(header, 1, sizeof header, file) == sizeof header
        && !memcmp(header, key, 32) && fread(data, 1, count, file) == count && fgetc(file) == EOF;
    fclose(file);
    if (ok) {
        tide_sha256_of(data, count, digest);
        ok = !memcmp(digest, header + 32, 32);
    }
    if (!ok) { free(data); return NULL; }
    if (days_before(written, now_ticks(), 1)) touch(path);
    *size = count;
    return data;
}

void asset_cache_write(const char *folder, const uint8_t key[32], const void *data, const size_t size)
{
    char path[4096], directory[4096], fresh[4160];
    if (!size || size > ASSET_CACHE_MAX_BYTES || !asset_cache_path(path, folder, key) || !cache_folder(directory, folder)) return;
    if (!folder_make(directory)) return;
    static atomic_uint serial; // Images are written from several threads (codegen.c, want_textures)
#ifdef _WIN32
    const unsigned long pid = GetCurrentProcessId();
#else
    const unsigned long pid = (unsigned long)getpid();
#endif
    snprintf(fresh, sizeof fresh, "%s.%lu-%u.new", path, pid, atomic_fetch_add(&serial, 1u) + 1u);
    FILE *file = fopen(fresh, "wb");
    if (!file) return;
    uint8_t digest[32];
    tide_sha256_of(data, size, digest);
    bool ok = fwrite(key, 1, 32, file) == 32 && fwrite(digest, 1, 32, file) == 32
        && fwrite(data, 1, size, file) == size;
    ok = fclose(file) == 0 && ok;
    if (ok) {
#ifdef _WIN32
        ok = MoveFileExA(fresh, path, MOVEFILE_REPLACE_EXISTING) != 0;
#else
        ok = rename(fresh, path) == 0;
#endif
    }
    if (!ok) remove(fresh);
}

// ---------------------------------------------------------------------------
// Letting go of what isn't used

// The `used` file of an importer's folder: the time, in seconds since 1970, its importer last tidied the cache
static bool read_used(const char *directory, unsigned long long *seconds)
{
    char path[4160], text[32] = {0};
    snprintf(path, sizeof path, "%s/used", directory);
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    const size_t n = fread(text, 1, sizeof text - 1, file);
    fclose(file);
    char *end;
    *seconds = strtoull(text, &end, 10);
    return n && end != text;
}

static void write_used(const char *directory, const unsigned long long seconds)
{
    char path[4160], fresh[4200];
    snprintf(path, sizeof path, "%s/used", directory);
#ifdef _WIN32
    snprintf(fresh, sizeof fresh, "%s.%lu.new", path, (unsigned long)GetCurrentProcessId());
#else
    snprintf(fresh, sizeof fresh, "%s.%lu.new", path, (unsigned long)getpid());
#endif
    FILE *file = fopen(fresh, "wb");
    if (!file) return;
    const bool ok = fprintf(file, "%llu\n", seconds) > 0;
    if (fclose(file) != 0 || !ok) { remove(fresh); return; }
#ifdef _WIN32
    if (!MoveFileExA(fresh, path, MOVEFILE_REPLACE_EXISTING)) remove(fresh);
#else
    if (rename(fresh, path) != 0) remove(fresh);
#endif
}

typedef struct entry_list {
    char **names;
    bool *folders;
    int count;
} entry_list;

static void listed(void *user, const char *name, const bool is_folder)
{
    entry_list *l = user;
    char **names = realloc(l->names, sizeof *names * (size_t)(l->count + 1));
    bool *folders = realloc(l->folders, sizeof *folders * (size_t)(l->count + 1));
    if (names) l->names = names;
    if (folders) l->folders = folders;
    if (!names || !folders) return;
    const size_t n = strlen(name) + 1;
    l->names[l->count] = malloc(n);
    if (!l->names[l->count]) return;
    memcpy(l->names[l->count], name, n);
    l->folders[l->count++] = is_folder;
}

static entry_list list_of(const char *directory)
{
    char slash[4160];
    snprintf(slash, sizeof slash, "%s/", directory);
    entry_list l = {0};
    folder_list(slash, listed, &l);
    return l;
}

static void free_list(entry_list *l)
{
    for (int i = 0; i < l->count; i++) free(l->names[i]);
    free(l->names);
    free(l->folders);
}

static void remove_folder(const char *directory)
{
    entry_list l = list_of(directory);
    char path[8320];
    for (int i = 0; i < l.count; i++) {
        snprintf(path, sizeof path, "%s/%s", directory, l.names[i]);
        if (!l.folders[i]) remove(path);
    }
    free_list(&l);
#ifdef _WIN32
    RemoveDirectoryA(directory);
#else
    rmdir(directory);
#endif
}

void asset_cache_tidy(const char *folder)
{
    char imports[4096], own[4096];
    if (!folder || !*folder || snprintf(imports, sizeof imports, "%s/.tide/assets/imports", folder) >= (int)sizeof imports
        || !cache_folder(own, folder)) return;
    const unsigned long long seconds = (unsigned long long)time(NULL), ticks = now_ticks();
    unsigned long long last;
    // Once a day: what's read is touched a day apart at the most (asset_cache_read)
    if (read_used(own, &last) && last <= seconds && seconds - last < 86400u) return;
    if (!folder_make(own)) return;
    write_used(own, seconds);
    entry_list l = list_of(imports);
    char path[8320];
    for (int i = 0; i < l.count; i++) {
        snprintf(path, sizeof path, "%s/%s", imports, l.names[i]);
        if (!l.folders[i]) remove(path); // What earlier compilers kept beside the folders
        else if (strcmp(path, own) != 0) {
            // Another importer's, which a different tide version still building this game keeps using
            unsigned long long used;
            if (!read_used(path, &used) || used > seconds || seconds - used >= ASSET_CACHE_OTHER_DAYS * 86400ull)
                remove_folder(path);
        }
    }
    free_list(&l);
    l = list_of(own);
    for (int i = 0; i < l.count; i++) {
        if (l.folders[i] || strcmp(l.names[i], "used") == 0) continue;
        snprintf(path, sizeof path, "%s/%s", own, l.names[i]);
        unsigned long long written;
        const size_t n = strlen(l.names[i]);
        // A temporary file a stopped compiler left, or an entry nothing has read for a while: its source changed,
        // or its declaration went
        const unsigned days = n > 4 && strcmp(l.names[i] + n - 4, ".new") == 0 ? 1u : ASSET_CACHE_UNUSED_DAYS;
        if (folder_file_info(path, NULL, &written) && days_before(written, ticks, days)) remove(path);
    }
    free_list(&l);
}
