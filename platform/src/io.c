// Files belong to the local host. Native providers use the OS's files and
// the browser provider uses the page's virtual files (tide_web.h).
#if !defined(_WIN32) && !defined(__wasm__)
#define _POSIX_C_SOURCE 200809L
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#elif defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif
#endif

#include "tide/platform.h"

#include <errno.h>
#ifndef __wasm__
#include "fetch.h" // Web.Fetch, on a thread of its own
#endif
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct io_state {
    tide_io provider;
    char *app_id;
    char *persistent;
    char *temporary;
    tide_io_error persistent_error;
    tide_io_error temporary_error;
#ifndef __wasm__
    struct io_dirty *dirty;
    uint32_t dirty_count;
#endif
} io_state;

static io_state state;

static char *duplicate(const char *text)
{
    const size_t size = strlen(text) + 1u;
    char *copy = tide_alloc(size);
    memcpy(copy, text, size);
    return copy;
}

#ifndef __wasm__
static char *join(const char *a, const char *b)
{
    const size_t first = strlen(a), second = strlen(b);
    const bool slash = first && a[first - 1] != '/' && a[first - 1] != '\\';
    char *path = tide_alloc(first + second + (slash ? 2u : 1u));
    memcpy(path, a, first);
    if (slash) path[first] = '/';
    memcpy(path + first + (slash ? 1u : 0u), b, second + 1u);
    for (size_t i = 0; path[i]; i++) if (path[i] == '\\') path[i] = '/';
    return path;
}
#endif

#ifdef __wasm__

#include "tide_web.h"

static bool backend_exists(void *user, const char *path, const bool directory)
{
    (void)user;
    return tide_web_io_exists(path, directory);
}

static tide_io_error backend_create(void *user, const char *path, const bool directory)
{
    (void)user;
    return (tide_io_error)tide_web_io_create(path, directory);
}

static tide_io_error backend_read(void *user, const char *path, uint8_t **data, uint32_t *size)
{
    (void)user;
    tide_io_error error = (tide_io_error)tide_web_io_read_size(path, size);
    if (error) return error;
    if (*size > INT32_MAX) return TIDE_IO_INVALID_DATA;
    *data = *size ? tide_alloc(*size) : NULL;
    error = (tide_io_error)tide_web_io_read(path, *data, *size);
    if (error) { free(*data); *data = NULL; *size = 0; }
    return error;
}

static tide_io_error backend_write(void *user, const char *path, const uint8_t *data, const uint32_t size, const bool append)
{
    (void)user;
    return (tide_io_error)tide_web_io_write(path, data, size, append);
}

static tide_io_error backend_copy(void *user, const char *source, const char *destination, const bool overwrite)
{
    (void)user;
    return (tide_io_error)tide_web_io_copy(source, destination, overwrite);
}

static tide_io_error backend_move(void *user, const char *source, const char *destination, const bool overwrite,
                                 const bool directory)
{
    (void)user;
    return (tide_io_error)tide_web_io_move(source, destination, overwrite, directory);
}

static tide_io_error backend_remove(void *user, const char *path, const bool directory, const bool recursive)
{
    (void)user;
    return (tide_io_error)tide_web_io_delete(path, directory, recursive);
}

static tide_io_error backend_list(void *user, const char *path, const bool directories, const bool recursive,
                                 const tide_io_entry_fn entry, void *entry_user)
{
    (void)user;
    uint32_t size = 0;
    tide_io_error error = (tide_io_error)tide_web_io_list_size(path, directories, recursive, &size);
    if (error || !size) return error;
    char *paths = tide_alloc(size);
    error = (tide_io_error)tide_web_io_list(path, directories, recursive, paths, size);
    if (!error) {
        for (uint32_t at = 0; at < size;) {
            const char *end = memchr(paths + at, 0, size - at);
            if (!end) { error = TIDE_IO_INVALID_DATA; break; }
            entry(entry_user, paths + at);
            at = (uint32_t)(end - paths) + 1u;
        }
    }
    free(paths);
    return error;
}

static uint32_t backend_flush_begin(void *user) { (void)user; return tide_web_io_flush_begin(); }
static bool backend_flush_poll(void *user, const uint32_t ticket, tide_io_error *error)
{
    (void)user;
    return tide_web_io_flush_poll(ticket, error);
}
static void backend_flush_forget(void *user, const uint32_t ticket) { (void)user; tide_web_io_flush_forget(ticket); }

// Web.Fetch: the page's fetch (tide.js), its body read out once it's done
static uint32_t backend_fetch_begin(void *user, const char *url) { (void)user; return tide_web_io_fetch_begin(url); }
static bool backend_fetch_poll(void *user, const uint32_t ticket, tide_io_error *error, uint8_t **data, uint32_t *size)
{
    (void)user;
    uint32_t n = 0;
    const int32_t state = tide_web_io_fetch_poll(ticket, &n);
    if (state < 0) return false;
    *error = (tide_io_error)state;
    *data = NULL;
    *size = 0;
    if (!state && n) {
        *data = tide_alloc(n);
        if (tide_web_io_fetch_read(ticket, *data, n) != 0) {
            free(*data);
            *data = NULL;
            *error = TIDE_IO_FAILED;
        } else *size = n;
    }
    tide_web_io_fetch_forget(ticket);
    return true;
}
static void backend_fetch_forget(void *user, const uint32_t ticket) { (void)user; tide_web_io_fetch_forget(ticket); }

static char *web_path(const int kind)
{
    const int32_t size = tide_web_io_path(kind, NULL, 0);
    if (size < 0) return duplicate("");
    char *path = tide_alloc((size_t)size + 1u);
    if (tide_web_io_path(kind, path, (uint32_t)size + 1u) < 0) path[0] = 0;
    return path;
}

#else

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#ifdef __linux__
#include <linux/fs.h>
#include <sys/syscall.h>
#endif
#include <unistd.h>
#endif

typedef struct io_dirty {
    char *path;
    bool directory;
} io_dirty;

typedef struct io_flush {
    struct io_flush *next;
    io_dirty *dirty;
    uint32_t dirty_count;
    uint32_t ticket;
    uint32_t generation;
    tide_io_error error;
    bool running;
    bool done;
    bool forgotten;
    bool moved;
} io_flush;

static struct {
    io_flush *tasks;
    uint32_t next_ticket;
    uint32_t generation;
    bool worker_started;
} flushes;

#ifdef _WIN32
static SRWLOCK io_lock = SRWLOCK_INIT;
static CONDITION_VARIABLE io_wake = CONDITION_VARIABLE_INIT;
static void lock_io(void) { AcquireSRWLockExclusive(&io_lock); }
static void unlock_io(void) { ReleaseSRWLockExclusive(&io_lock); }
static void wake_io(void) { WakeConditionVariable(&io_wake); }
static void wait_io(void) { SleepConditionVariableSRW(&io_wake, &io_lock, INFINITE, 0); }
#else
static pthread_mutex_t io_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t io_wake = PTHREAD_COND_INITIALIZER;
static void lock_io(void) { pthread_mutex_lock(&io_lock); }
static void unlock_io(void) { pthread_mutex_unlock(&io_lock); }
static void wake_io(void) { pthread_cond_signal(&io_wake); }
static void wait_io(void) { pthread_cond_wait(&io_wake, &io_lock); }
#endif

static tide_io_error errno_error(void)
{
    switch (errno) {
    case ENOENT: return TIDE_IO_NOT_FOUND;
    case EEXIST: return TIDE_IO_ALREADY_EXISTS;
    case EACCES: case EPERM: case EROFS: return TIDE_IO_ACCESS_DENIED;
    case ENOTDIR: return TIDE_IO_NOT_DIRECTORY;
    case ENOTEMPTY: return TIDE_IO_DIRECTORY_NOT_EMPTY;
    case ENOSPC: return TIDE_IO_OUT_OF_SPACE;
    case EINVAL: case ENAMETOOLONG: return TIDE_IO_INVALID_PATH;
    default: return TIDE_IO_FAILED;
    }
}

#ifdef _WIN32
static tide_io_error windows_error(void)
{
    switch (GetLastError()) {
    case ERROR_FILE_NOT_FOUND: case ERROR_PATH_NOT_FOUND: return TIDE_IO_NOT_FOUND;
    case ERROR_ALREADY_EXISTS: case ERROR_FILE_EXISTS: return TIDE_IO_ALREADY_EXISTS;
    case ERROR_ACCESS_DENIED: case ERROR_SHARING_VIOLATION: case ERROR_WRITE_PROTECT: return TIDE_IO_ACCESS_DENIED;
    case ERROR_DIRECTORY: return TIDE_IO_NOT_DIRECTORY;
    case ERROR_DIR_NOT_EMPTY: return TIDE_IO_DIRECTORY_NOT_EMPTY;
    case ERROR_DISK_FULL: case ERROR_HANDLE_DISK_FULL: return TIDE_IO_OUT_OF_SPACE;
    case ERROR_INVALID_NAME: case ERROR_BAD_PATHNAME: case ERROR_FILENAME_EXCED_RANGE: return TIDE_IO_INVALID_PATH;
    default: return TIDE_IO_FAILED;
    }
}

static wchar_t *wide_path(const char *path)
{
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    if (!count) return NULL;
    wchar_t *wide = tide_alloc((size_t)count * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, count);
    for (int i = 0; i < count; i++) if (wide[i] == L'/') wide[i] = L'\\';
    return wide;
}

static char *from_wide(const wchar_t *wide)
{
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1, NULL, 0, NULL, NULL);
    if (!size) return NULL;
    char *text = tide_alloc((size_t)size);
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1, text, size, NULL, NULL);
    for (int i = 0; i < size; i++) if (text[i] == '\\') text[i] = '/';
    return text;
}

static char *environment(const wchar_t *name)
{
    const DWORD size = GetEnvironmentVariableW(name, NULL, 0);
    if (!size) return NULL;
    wchar_t *wide = tide_alloc((size_t)size * sizeof(wchar_t));
    if (!GetEnvironmentVariableW(name, wide, size)) { free(wide); return NULL; }
    char *text = from_wide(wide);
    free(wide);
    return text;
}
#endif

static bool absolute(const char *path)
{
    return path[0] == '/' || path[0] == '\\' || (path[0] && path[1] == ':');
}

static char *resolve(const io_state *io, const char *path)
{
    char *full = absolute(path) ? duplicate(path) : join(io->persistent, path);
    for (size_t i = 0; full[i]; i++) if (full[i] == '\\') full[i] = '/';
    // Remove redundant separators and '.' aliases, preserving '..': the OS
    // resolves a parent through a symlink differently from lexical removal.
    size_t read = 0, written = 0;
    if (full[0] == '/') { full[written++] = '/'; read++; if (full[1] == '/') { full[written++] = '/'; read++; } }
    else if (full[0] && full[1] == ':' && full[2] == '/') { read = written = 3; }
    while (full[read]) {
        while (full[read] == '/') read++;
        const size_t start = read;
        while (full[read] && full[read] != '/') read++;
        const size_t size = read - start;
        if (!size || (size == 1 && full[start] == '.')) continue;
        if (written && full[written - 1] != '/') full[written++] = '/';
        memmove(full + written, full + start, size);
        written += size;
    }
    full[written] = 0;
    return full;
}

static bool inside(const char *path, const char *root)
{
    const size_t size = strlen(root);
    return size && strncmp(path, root, size) == 0 && (!path[size] || path[size] == '/' || path[size] == '\\');
}

static tide_io_error root_error(const io_state *io, const char *path)
{
    if (inside(path, io->temporary)) return io->temporary_error;
    if (!absolute(path) || inside(path, io->persistent)) return io->persistent_error;
    return TIDE_IO_NONE;
}

static void dirty_path_locked(io_state *io, const char *path, const bool directory)
{
    for (uint32_t i = 0; i < io->dirty_count; i++) {
        if (io->dirty[i].directory == directory && strcmp(io->dirty[i].path, path) == 0) return;
    }
    io->dirty = tide_realloc(io->dirty, (size_t)io->dirty_count * sizeof(io_dirty),
                             ((size_t)io->dirty_count + 1u) * sizeof(io_dirty));
    io->dirty[io->dirty_count++] = (io_dirty){duplicate(path), directory};
}

static void dirty_path(io_state *io, const char *path, const bool directory)
{
    lock_io();
    dirty_path_locked(io, path, directory);
    unlock_io();
}

static void dirty_parent(io_state *io, const char *path)
{
    char *parent = duplicate(path);
    char *end = strrchr(parent, '/');
    char *back = strrchr(parent, '\\');
    if (!end || (back && back > end)) end = back;
    if (end) {
        if (end == parent) end[1] = 0;
        else if (end == parent + 2 && parent[1] == ':') end[1] = 0;
        else *end = 0;
        dirty_path(io, parent, true);
    }
    free(parent);
}

static void changed_file(io_state *io, const char *path)
{
    dirty_path(io, path, false);
    dirty_parent(io, path);
}

static bool path_prefix(const char *path, const char *from, const size_t prefix, size_t *offset)
{
#ifdef _WIN32
    char *part = tide_alloc(prefix + 1u);
    memcpy(part, from, prefix);
    part[prefix] = 0;
    wchar_t *wide_from = wide_path(part), *wide_path_value = wide_path(path);
    free(part);
    bool same = false;
    if (wide_from && wide_path_value) {
        const size_t units = wcslen(wide_from);
        if (wcslen(wide_path_value) >= units
            && CompareStringOrdinal(wide_path_value, (int)units, wide_from, (int)units, TRUE) == CSTR_EQUAL) {
            const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_path_value, (int)units,
                                                   NULL, 0, NULL, NULL);
            if (bytes > 0) { *offset = (size_t)bytes; same = true; }
        }
    }
    free(wide_from);
    free(wide_path_value);
    if (!same) return false;
#else
    if (strlen(path) < prefix || strncmp(path, from, prefix) != 0) return false;
    *offset = prefix;
#endif
    return !path[*offset] || path[*offset] == '/';
}

static void relocated_path_locked(io_state *io, const io_dirty *dirty, const char *from, const size_t prefix,
                                  const char *to)
{
    size_t offset = 0;
    if (!path_prefix(dirty->path, from, prefix, &offset)) return;
    const size_t target = strlen(to), suffix = strlen(dirty->path) - offset;
    char *moved = tide_alloc(target + suffix + 1u);
    memcpy(moved, to, target);
    memcpy(moved + target, dirty->path + offset, suffix + 1u);
    dirty_path_locked(io, moved, dirty->directory);
    free(moved);
}

static void relocated_directory(io_state *io, const char *from, const char *to)
{
    size_t prefix = strlen(from);
    while (prefix > 1 && from[prefix - 1] == '/') prefix--;
    lock_io();
    const uint32_t count = io->dirty_count;
    for (uint32_t i = 0; i < count; i++) {
        // Adding a relocated path can move the array, so the entry is a
        // value and its owned path remains alive through that allocation.
        const io_dirty dirty = io->dirty[i];
        relocated_path_locked(io, &dirty, from, prefix, to);
    }
    for (const io_flush *prior = flushes.tasks; prior; prior = prior->next) {
        if (prior->done || prior->generation != flushes.generation) continue;
        for (uint32_t i = 0; i < prior->dirty_count; i++) {
            relocated_path_locked(io, &prior->dirty[i], from, prefix, to);
        }
    }
    unlock_io();
}

static void moved_flushes_locked(const char *from, const bool directory)
{
    size_t prefix = strlen(from);
    while (prefix > 1 && from[prefix - 1] == '/') prefix--;
    for (io_flush *task = flushes.tasks; task; task = task->next) {
        if (task->done || task->generation != flushes.generation) continue;
        for (uint32_t i = 0; i < task->dirty_count; i++) {
            size_t offset = 0;
            if (path_prefix(task->dirty[i].path, from, prefix, &offset)
                && (directory || !task->dirty[i].path[offset])) {
                // The worker may not yet have opened this old path. Its
                // missing-file case must not acknowledge data under a new
                // name. Fail this waiter conservatively; the moved paths are
                // retained in the dirty set for a subsequent barrier.
                task->moved = true;
                break;
            }
        }
    }
}

// type: 1 file, 2 directory; links are reported separately so recursion never
// enters a directory link. The existence query itself follows file links.
static tide_io_error path_type(const char *path, int *type, bool *link)
{
#ifdef _WIN32
    wchar_t *wide = wide_path(path);
    if (!wide) return TIDE_IO_INVALID_PATH;
    const DWORD attrs = GetFileAttributesW(wide);
    free(wide);
    if (attrs == INVALID_FILE_ATTRIBUTES) return windows_error();
    *type = attrs & FILE_ATTRIBUTE_DIRECTORY ? 2 : 1;
    *link = (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    struct stat own, target;
    if (lstat(path, &own) != 0) return errno_error();
    *link = S_ISLNK(own.st_mode);
    if (*link) {
        if (stat(path, &target) != 0) { *type = 1; return TIDE_IO_NONE; }
    } else target = own;
    *type = S_ISDIR(target.st_mode) ? 2 : 1;
#endif
    return TIDE_IO_NONE;
}

static bool backend_exists(void *user, const char *path, const bool directory)
{
    io_state *io = user;
    if (root_error(io, path)) return false;
    char *full = resolve(io, path);
    int type = 0;
    bool link = false;
    const tide_io_error error = path_type(full, &type, &link);
    free(full);
    return !error && type == (directory ? 2 : 1);
}

// made: whether this call made the directory, rather than found it there.
static tide_io_error mkdir_one(const char *path, bool *made)
{
    *made = false;
#ifdef _WIN32
    wchar_t *wide = wide_path(path);
    if (!wide) return TIDE_IO_INVALID_PATH;
    const bool created = CreateDirectoryW(wide, NULL) != 0;
    free(wide);
    const tide_io_error error = created ? TIDE_IO_NONE : windows_error();
#else
    const tide_io_error error = mkdir(path, 0700) == 0 ? TIDE_IO_NONE : errno_error();
#endif
    *made = !error;
    if (error != TIDE_IO_ALREADY_EXISTS) return error;
    int type = 0;
    bool link = false;
    const tide_io_error found = path_type(path, &type, &link);
    return found ? found : type == 2 ? TIDE_IO_NONE : TIDE_IO_NOT_DIRECTORY;
}

static tide_io_error mkdir_parents(io_state *io, const char *path)
{
    char *copy = duplicate(path);
    tide_io_error error = TIDE_IO_NONE;
    // Skip drive/root and UNC server/share prefixes.
    size_t start = copy[0] == '/' || copy[0] == '\\' ? 1u : copy[0] && copy[1] == ':' ? 3u : 0u;
#ifdef _WIN32
    if ((copy[0] == '/' || copy[0] == '\\') && (copy[1] == '/' || copy[1] == '\\')) {
        start = 2;
        for (int part = 0; part < 2 && copy[start]; part++) {
            while (copy[start] && copy[start] != '/' && copy[start] != '\\') start++;
            if (copy[start]) start++;
        }
    }
#endif
    // Only a directory made here changes its parent. One that was there
    // already is left out of the flush: nothing in it changed, and those
    // above an app's own folders may not be this program's to open.
    bool made = false;
    for (size_t i = start; copy[i]; i++) {
        if (copy[i] != '/' && copy[i] != '\\') continue;
        const char saved = copy[i];
        copy[i] = 0;
        made = false;
        if (i && strcmp(copy + start, ".") != 0) error = mkdir_one(copy, &made);
        if (made) { dirty_path(io, copy, true); dirty_parent(io, copy); }
        copy[i] = saved;
        if (error) break;
    }
    made = false;
    if (!error) error = mkdir_one(copy, &made);
    if (made) { dirty_path(io, copy, true); dirty_parent(io, copy); }
    free(copy);
    return error;
}

static FILE *open_file(const char *path, const bool append)
{
#ifdef _WIN32
    wchar_t *wide = wide_path(path);
    if (!wide) { errno = EINVAL; return NULL; }
    FILE *file = _wfopen(wide, append ? L"ab" : L"wb");
    free(wide);
    return file;
#else
    return fopen(path, append ? "ab" : "wb");
#endif
}

static tide_io_error backend_write(void *user, const char *path, const uint8_t *data, const uint32_t size, const bool append)
{
    io_state *io = user;
    tide_io_error error = root_error(io, path);
    if (error) return error;
    char *full = resolve(io, path);
    FILE *file = open_file(full, append);
    if (!file) error = errno_error();
    else {
        if (size && fwrite(data, 1, size, file) != size) error = errno_error();
        if (fclose(file) != 0 && !error) error = errno_error();
        // A failed write may still have created or changed the file.
        changed_file(io, full);
    }
    free(full);
    return error;
}

static tide_io_error backend_create(void *user, const char *path, const bool directory)
{
    if (!directory) return backend_write(user, path, NULL, 0, false);
    io_state *io = user;
    tide_io_error error = root_error(io, path);
    if (error) return error;
    char *full = resolve(io, path);
    error = mkdir_parents(io, full);
    free(full);
    return error;
}

static tide_io_error backend_read(void *user, const char *path, uint8_t **data, uint32_t *size)
{
    io_state *io = user;
    tide_io_error error = root_error(io, path);
    if (error) return error;
    char *full = resolve(io, path);
    int type = 0;
    bool link = false;
    error = path_type(full, &type, &link);
    if (!error && type == 2) error = TIDE_IO_FAILED;
    FILE *file = NULL;
    if (!error) {
#ifdef _WIN32
        wchar_t *wide = wide_path(full);
        if (wide) { file = _wfopen(wide, L"rb"); free(wide); }
        else error = TIDE_IO_INVALID_PATH;
#else
        file = fopen(full, "rb");
#endif
        if (!file && !error) error = errno_error();
    }
    free(full);
    if (error) return error;
    uint32_t count = 0, capacity = 4096;
    uint8_t *bytes = tide_alloc(capacity);
    for (;;) {
        const size_t got = fread(bytes + count, 1, capacity - count, file);
        count += (uint32_t)got;
        if (ferror(file)) { error = errno_error(); break; }
        if (feof(file)) break;
        if (count == capacity) {
            if (capacity == INT32_MAX) { error = TIDE_IO_INVALID_DATA; break; }
            const uint32_t next = capacity > INT32_MAX / 2u ? INT32_MAX : capacity * 2u;
            bytes = tide_realloc(bytes, capacity, next);
            capacity = next;
        }
    }
    if (fclose(file) != 0 && !error) error = errno_error();
    if (error) { free(bytes); return error; }
    *data = bytes;
    *size = count;
    return TIDE_IO_NONE;
}

static tide_io_error backend_copy(void *user, const char *source, const char *destination, const bool overwrite)
{
    io_state *io = user;
    tide_io_error error = root_error(io, source);
    if (!error) error = root_error(io, destination);
    if (error) return error;
    char *from = resolve(io, source), *to = resolve(io, destination);
#ifdef _WIN32
    wchar_t *wide_from = wide_path(from), *wide_to = wide_path(to);
    if (!wide_from || !wide_to) error = TIDE_IO_INVALID_PATH;
    else if (!CopyFileW(wide_from, wide_to, !overwrite)) error = windows_error();
    free(wide_from);
    free(wide_to);
#else
    int input = open(from, O_RDONLY);
    if (input < 0) error = errno_error();
    struct stat first = {0}, second = {0};
    if (!error && fstat(input, &first) != 0) error = errno_error();
    if (!error && S_ISDIR(first.st_mode)) error = TIDE_IO_FAILED;
    int output = -1;
    if (!error) {
        output = open(to, O_WRONLY | O_CREAT | (overwrite ? 0 : O_EXCL), 0600);
        if (output < 0) error = errno_error();
    }
    if (!error && fstat(output, &second) != 0) error = errno_error();
    if (!error && first.st_dev == second.st_dev && first.st_ino == second.st_ino) error = TIDE_IO_INVALID_PATH;
    if (!error && ftruncate(output, 0) != 0) error = errno_error();
    uint8_t bytes[16384];
    while (!error) {
        const ssize_t count = read(input, bytes, sizeof bytes);
        if (count < 0) { if (errno == EINTR) continue; error = errno_error(); break; }
        if (!count) break;
        for (ssize_t at = 0; at < count;) {
            const ssize_t written = write(output, bytes + at, (size_t)(count - at));
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) { error = errno_error(); break; }
            at += written;
        }
    }
    if (input >= 0 && close(input) != 0 && !error) error = errno_error();
    if (output >= 0 && close(output) != 0 && !error) error = errno_error();
#endif
#ifdef _WIN32
    if (!error) changed_file(io, to);
#else
    // Opening may create the destination; later reads, writes, or close can
    // fail after changing it. Flush must cover those partial changes too.
    if (output >= 0) changed_file(io, to);
#endif
    free(from);
    free(to);
    return error;
}

#ifndef _WIN32
static int rename_without_replace(const char *from, const char *to)
{
#if defined(__linux__) && defined(SYS_renameat2)
    // Use the syscall directly: Android's libc wrapper requires API 30.
    return (int)syscall(SYS_renameat2, AT_FDCWD, from, AT_FDCWD, to, RENAME_NOREPLACE);
#elif defined(__APPLE__)
    return renamex_np(from, to, RENAME_EXCL);
#else
    (void)from;
    (void)to;
    errno = ENOSYS;
    return -1;
#endif
}
#endif

static tide_io_error backend_move(void *user, const char *source, const char *destination, const bool overwrite,
                                 const bool directory)
{
    io_state *io = user;
    tide_io_error error = root_error(io, source);
    if (!error) error = root_error(io, destination);
    if (error) return error;
    char *from = resolve(io, source), *to = resolve(io, destination);
    int type = 0;
    bool link = false;
    error = path_type(from, &type, &link);
    if (!error && type != (directory ? 2 : 1)) error = directory ? TIDE_IO_NOT_DIRECTORY : TIDE_IO_FAILED;
    if (!error) {
        lock_io();
#ifdef _WIN32
        wchar_t *wide_from = wide_path(from), *wide_to = wide_path(to);
        if (!wide_from || !wide_to) error = TIDE_IO_INVALID_PATH;
        else if (!MoveFileExW(wide_from, wide_to, MOVEFILE_WRITE_THROUGH | (overwrite ? MOVEFILE_REPLACE_EXISTING : 0))) {
            error = windows_error();
        }
        free(wide_from);
        free(wide_to);
#else
        // Checking first and then calling rename would let another process's
        // destination be overwritten. Unsupported no-replace operations fail.
        if ((overwrite ? rename(from, to) : rename_without_replace(from, to)) != 0) error = errno_error();
#endif
        if (!error) moved_flushes_locked(from, directory);
        unlock_io();
    }
    if (!error) {
        dirty_parent(io, from);
        if (directory) {
            relocated_directory(io, from, to);
            dirty_path(io, to, true);
            dirty_parent(io, to);
        }
        else changed_file(io, to);
    }
    free(from);
    free(to);
    return error;
}

static tide_io_error walk(const char *path, const bool directories, const bool recursive, const tide_io_entry_fn entry,
                          void *entry_user);

typedef struct remove_walk {
    io_state *io;
    tide_io_error error;
} remove_walk;

static tide_io_error remove_full(io_state *io, const char *path, bool directory, bool recursive);

static void remove_entry(void *user, const char *path)
{
    remove_walk *removal = user;
    if (removal->error) return;
    int type = 0;
    bool link = false;
    removal->error = path_type(path, &type, &link);
    if (!removal->error) removal->error = remove_full(removal->io, path, type == 2, !link);
}

static tide_io_error remove_full(io_state *io, const char *path, const bool directory, const bool recursive)
{
    int type = 0;
    bool link = false;
    tide_io_error error = path_type(path, &type, &link);
    if (error) return !directory && error == TIDE_IO_NOT_FOUND ? TIDE_IO_NONE : error;
    if (type != (directory ? 2 : 1)) return directory ? TIDE_IO_NOT_DIRECTORY : TIDE_IO_FAILED;
    if (directory && recursive && !link) {
        remove_walk removal = {io, TIDE_IO_NONE};
        error = walk(path, false, false, remove_entry, &removal);
        if (!error) error = removal.error;
        if (!error) error = walk(path, true, false, remove_entry, &removal);
        if (!error) error = removal.error;
        if (error) return error;
    }
#ifdef _WIN32
    wchar_t *wide = wide_path(path);
    if (!wide) return TIDE_IO_INVALID_PATH;
    if (!(directory ? RemoveDirectoryW(wide) : DeleteFileW(wide))) error = windows_error();
    free(wide);
#else
    if ((directory && !link ? rmdir(path) : unlink(path)) != 0) error = errno_error();
#endif
    if (!error) dirty_parent(io, path);
    return error;
}

static tide_io_error backend_remove(void *user, const char *path, const bool directory, const bool recursive)
{
    io_state *io = user;
    tide_io_error error = root_error(io, path);
    if (error) return error;
    char *full = resolve(io, path);
    error = remove_full(io, full, directory, recursive);
    free(full);
    return error;
}

static tide_io_error walk_entry(const char *path, const bool directories, const bool recursive,
                               const tide_io_entry_fn entry, void *entry_user)
{
    int type = 0;
    bool link = false;
    tide_io_error error = path_type(path, &type, &link);
    if (error) return error;
    if ((type == 2) == directories) entry(entry_user, path);
    if (recursive && type == 2 && !link) error = walk(path, directories, true, entry, entry_user);
    return error;
}

static tide_io_error walk(const char *path, const bool directories, const bool recursive, const tide_io_entry_fn entry,
                          void *entry_user)
{
    tide_io_error error = TIDE_IO_NONE;
#ifdef _WIN32
    char *pattern = join(path, "*");
    wchar_t *wide = wide_path(pattern);
    free(pattern);
    if (!wide) return TIDE_IO_INVALID_PATH;
    WIN32_FIND_DATAW found;
    HANDLE scan = FindFirstFileW(wide, &found);
    free(wide);
    if (scan == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND ? TIDE_IO_NONE : windows_error();
    do {
        if (wcscmp(found.cFileName, L".") == 0 || wcscmp(found.cFileName, L"..") == 0) continue;
        char *name = from_wide(found.cFileName);
        if (!name) { error = TIDE_IO_INVALID_DATA; break; }
        char *full = join(path, name);
        free(name);
        error = walk_entry(full, directories, recursive, entry, entry_user);
        free(full);
        if (error) break;
    } while (FindNextFileW(scan, &found));
    if (!error && GetLastError() != ERROR_NO_MORE_FILES) error = windows_error();
    FindClose(scan);
#else
    DIR *scan = opendir(path);
    if (!scan) return errno_error();
    struct dirent *found;
    for (;;) {
        errno = 0;
        found = readdir(scan);
        if (!found) { if (errno) error = errno_error(); break; }
        if (strcmp(found->d_name, ".") == 0 || strcmp(found->d_name, "..") == 0) continue;
        char *full = join(path, found->d_name);
        error = walk_entry(full, directories, recursive, entry, entry_user);
        free(full);
        if (error) break;
    }
    if (closedir(scan) != 0 && !error) error = errno_error();
#endif
    return error;
}

static tide_io_error backend_list(void *user, const char *path, const bool directories, const bool recursive,
                                 const tide_io_entry_fn entry, void *entry_user)
{
    io_state *io = user;
    tide_io_error error = root_error(io, path);
    if (error) return error;
    char *full = resolve(io, path);
    int type = 0;
    bool link = false;
    error = path_type(full, &type, &link);
    if (!error && type != 2) error = TIDE_IO_NOT_DIRECTORY;
    if (!error) error = walk(full, directories, recursive, entry, entry_user);
    free(full);
    return error;
}

static tide_io_error flush_dirty(const io_dirty *paths, const uint32_t count)
{
    tide_io_error error = TIDE_IO_NONE;
    for (uint32_t i = 0; i < count; i++) {
        const io_dirty *dirty = &paths[i];
#ifdef _WIN32
        // Windows flushes file data through a write handle. Directory flush
        // handles aren't supported; rename already uses WRITE_THROUGH.
        if (dirty->directory) continue;
        wchar_t *wide = wide_path(dirty->path);
        if (!wide) { error = TIDE_IO_INVALID_PATH; break; }
        HANDLE file = CreateFileW(wide, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        free(wide);
        if (file == INVALID_HANDLE_VALUE) {
            error = windows_error();
            if (error == TIDE_IO_NOT_FOUND) { error = TIDE_IO_NONE; continue; }
            break;
        }
        if (!FlushFileBuffers(file)) error = windows_error();
        CloseHandle(file);
#else
        const int file = open(dirty->path, O_RDONLY);
        if (file < 0) {
            error = errno_error();
            if (error == TIDE_IO_NOT_FOUND) { error = TIDE_IO_NONE; continue; }
            break;
        }
        if (fsync(file) != 0) error = errno_error();
        if (close(file) != 0 && !error) error = errno_error();
#endif
        if (error) break;
    }
    return error;
}

static void free_flush(io_flush *task)
{
    for (uint32_t i = 0; i < task->dirty_count; i++) free(task->dirty[i].path);
    free(task->dirty);
    free(task);
}

static void erase_flush_locked(io_flush *task)
{
    io_flush **at = &flushes.tasks;
    while (*at && *at != task) at = &(*at)->next;
    if (*at) *at = task->next;
    free_flush(task);
}

#ifdef _WIN32
static DWORD WINAPI flush_worker(void *unused)
#else
static void *flush_worker(void *unused)
#endif
{
    (void)unused;
    for (;;) {
        lock_io();
        io_flush *task = NULL;
        for (;;) {
            for (task = flushes.tasks; task; task = task->next) if (!task->running && !task->done) break;
            if (task) break;
            wait_io();
        }
        task->running = true;
        unlock_io();
        tide_memory_sync();
        tide_io_error error = flush_dirty(task->dirty, task->dirty_count);
        lock_io();
        if (!error && task->moved) error = TIDE_IO_FAILED;
        task->error = error;
        task->done = true;
        // An unsuccessful flush retains its paths for the next attempt.
        // An app switch retires the previous app's queue and dirty paths.
        if (error && task->generation == flushes.generation) {
            for (uint32_t i = 0; i < task->dirty_count; i++) {
                dirty_path_locked(&state, task->dirty[i].path, task->dirty[i].directory);
            }
        }
        if (task->forgotten) erase_flush_locked(task);
        unlock_io();
    }
}

static bool start_flush_worker_locked(void)
{
    if (flushes.worker_started) return true;
#ifdef _WIN32
    HANDLE thread = CreateThread(NULL, 0, flush_worker, NULL, 0, NULL);
    if (!thread) return false;
    CloseHandle(thread); // The worker lives with the platform provider.
#else
    pthread_t thread;
    if (pthread_create(&thread, NULL, flush_worker, NULL) != 0) return false;
    pthread_detach(thread);
#endif
    flushes.worker_started = true;
    return true;
}

static uint32_t backend_flush_begin(void *user)
{
    io_state *io = user;
    lock_io();
    if (!start_flush_worker_locked()) { unlock_io(); return 0; }
    // A later barrier also covers writes an earlier one is still flushing.
    // This matters when an await restarts after local-state restoration: its
    // previous waiter may be forgotten while the old commit is still running.
    for (const io_flush *prior = flushes.tasks; prior; prior = prior->next) {
        if (prior->done || prior->generation != flushes.generation) continue;
        for (uint32_t i = 0; i < prior->dirty_count; i++) {
            dirty_path_locked(io, prior->dirty[i].path, prior->dirty[i].directory);
        }
    }
    io_flush *task = tide_alloc_zeroed(1, sizeof(io_flush));
    task->dirty = io->dirty;
    task->dirty_count = io->dirty_count;
    task->generation = flushes.generation;
    do { task->ticket = ++flushes.next_ticket; } while (!task->ticket);
    io->dirty = NULL;
    io->dirty_count = 0;
    io_flush **at = &flushes.tasks;
    while (*at) at = &(*at)->next;
    *at = task;
    const uint32_t ticket = task->ticket;
    wake_io();
    unlock_io();
    return ticket;
}

static bool backend_flush_poll(void *user, const uint32_t ticket, tide_io_error *error)
{
    (void)user;
    lock_io();
    io_flush *task = flushes.tasks;
    while (task && task->ticket != ticket) task = task->next;
    if (!task) { *error = TIDE_IO_FAILED; unlock_io(); return true; }
    if (!task->done) { unlock_io(); return false; }
    *error = task->error;
    erase_flush_locked(task);
    unlock_io();
    return true;
}

static void backend_flush_forget(void *user, const uint32_t ticket)
{
    (void)user;
    lock_io();
    for (io_flush *task = flushes.tasks; task; task = task->next) {
        if (task->ticket != ticket) continue;
        if (task->done) erase_flush_locked(task);
        else task->forgotten = true;
        break;
    }
    unlock_io();
}

static void switch_app(void)
{
    lock_io();
    flushes.generation++;
    for (io_flush *task = flushes.tasks; task;) {
        io_flush *next = task->next;
        if (task->done) erase_flush_locked(task);
        else task->forgotten = true;
        task = next;
    }
    for (uint32_t i = 0; i < state.dirty_count; i++) free(state.dirty[i].path);
    free(state.dirty);
    state.dirty = NULL;
    state.dirty_count = 0;
    unlock_io();
}

#ifdef __ANDROID__
// NativeActivity owns these paths. NULL when a command-line test has no
// activity; then it uses the test's working directory.
const char *tide_android_internal_data_path(void);
const char *tide_android_cache_path(void);
#endif

static void native_paths(io_state *io)
{
#ifdef _WIN32
    char *profile = environment(L"USERPROFILE");
    char *base = profile ? join(profile, "AppData/LocalLow") : NULL;
    io->persistent = base ? join(base, io->app_id) : duplicate("");
    free(base);
    free(profile);
    const DWORD size = GetTempPathW(0, NULL);
    wchar_t *wide = size ? tide_alloc(((size_t)size + 1u) * sizeof(wchar_t)) : NULL;
    if (wide && GetTempPathW(size + 1u, wide)) base = from_wide(wide);
    else base = NULL;
    io->temporary = base ? join(base, io->app_id) : duplicate("");
    free(wide);
    free(base);
#elif defined(__ANDROID__)
    const char *data = tide_android_internal_data_path(), *cache = tide_android_cache_path();
    if (data) io->persistent = duplicate(data);
    else {
        char *cwd = getcwd(NULL, 0);
        io->persistent = cwd ? join(cwd, io->app_id) : duplicate("");
        free(cwd);
    }
    io->temporary = cache ? duplicate(cache) : join(io->persistent, "cache");
#else
    const char *home = getenv("HOME");
#ifdef __APPLE__
    char *base = home ? join(home, "Library/Application Support") : NULL;
#else
    const char *xdg = getenv("XDG_DATA_HOME");
    char *base = xdg && absolute(xdg) ? duplicate(xdg) : home ? join(home, ".local/share") : NULL;
#endif
    io->persistent = base ? join(base, io->app_id) : duplicate("");
    free(base);
    const char *temp = getenv("TMPDIR");
    io->temporary = join(temp && absolute(temp) ? temp : "/tmp", io->app_id);
#endif
    io->persistent_error = io->persistent[0] ? mkdir_parents(io, io->persistent) : TIDE_IO_STORAGE_UNAVAILABLE;
    io->temporary_error = io->temporary[0] ? mkdir_parents(io, io->temporary) : TIDE_IO_STORAGE_UNAVAILABLE;
}

#endif

#ifndef __wasm__
static uint32_t backend_fetch_begin(void *user, const char *url) { (void)user; return tide_fetch_begin(url); }
static bool backend_fetch_poll(void *user, const uint32_t ticket, tide_io_error *error, uint8_t **data, uint32_t *size)
{
    (void)user;
    return tide_fetch_poll(ticket, error, data, size);
}
static void backend_fetch_forget(void *user, const uint32_t ticket) { (void)user; tide_fetch_forget(ticket); }
#endif

const tide_io *tide_platform_io_init(const char *app_id)
{
    if (!app_id || !*app_id) app_id = "dev.tide.game";
    if (state.app_id && strcmp(state.app_id, app_id) == 0) return &state.provider;
    // A host has one app at a time. Same-app hot reload never enters here.
    free(state.app_id);
    free(state.persistent);
    free(state.temporary);
#ifndef __wasm__
    switch_app();
#endif
    memset(&state, 0, sizeof state);
    state.app_id = duplicate(app_id);
#ifdef __wasm__
    tide_web_io_init(app_id);
    state.persistent = web_path(1);
    state.temporary = web_path(2);
#else
    native_paths(&state);
#endif
    state.provider = (tide_io){
        .user = &state,
        .persistent_data_path = state.persistent,
        .temporary_cache_path = state.temporary,
        .exists = backend_exists,
        .create = backend_create,
        .read = backend_read,
        .write = backend_write,
        .copy = backend_copy,
        .move = backend_move,
        .remove = backend_remove,
        .list = backend_list,
        .flush_begin = backend_flush_begin,
        .flush_poll = backend_flush_poll,
        .flush_forget = backend_flush_forget,
        .fetch_begin = backend_fetch_begin,
        .fetch_poll = backend_fetch_poll,
        .fetch_forget = backend_fetch_forget,
    };
    return &state.provider;
}
