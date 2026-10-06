#include "assets.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <direct.h>
#include <windows.h>
#else
#include <sys/stat.h>
#endif

#include "sha256.h"

bool asset_path_is_glob(const char *path, const size_t len)
{
    return memchr(path, '*', len) || memchr(path, '?', len);
}

bool asset_path_is_url(const char *path, const size_t len)
{
    return (len >= 7 && memcmp(path, "http://", 7) == 0) || (len >= 8 && memcmp(path, "https://", 8) == 0);
}

static char *copy_bytes(const char *text, const size_t len)
{
    char *out = malloc(len + 1);
    if (!out) abort();
    memcpy(out, text, len);
    out[len] = '\0';
    return out;
}

static bool is_sep(const char c)
{
    return c == '/' || c == '\\';
}

static char fold(const char c)
{
#ifdef _WIN32
    return (char)tolower((unsigned char)(is_sep(c) ? '/' : c));
#else
    return is_sep(c) ? '/' : c;
#endif
}

char *asset_path_join(const char *folder, const char *relative, const size_t len)
{
    const size_t n = strlen(folder);
    char *out = malloc(n + len + 2);
    if (!out) abort();
    memcpy(out, folder, n);
    size_t at = n;
    for (size_t i = 0; i < at; i++) {
        if (out[i] == '\\') out[i] = '/';
    }
    if (at == 0 || out[at - 1] != '/') out[at++] = '/';
    const size_t root = at; // Nothing goes above the folder's own root, "D:/" or "/"
    size_t top = 0;
    while (top < root && !(out[top] == '/' && (top == 0 || out[top - 1] == ':'))) top++;
    for (size_t i = 0; i < len;) {
        size_t end = i;
        while (end < len && !is_sep(relative[end])) end++;
        const size_t part = end - i;
        if (part == 0 || (part == 1 && relative[i] == '.')) {
            // Nothing: "a//b", "./b"
        } else if (part == 2 && relative[i] == '.' && relative[i + 1] == '.') {
            if (at > top + 1) {
                at--; // The '/' this folder ends in
                while (at > top + 1 && out[at - 1] != '/') at--;
            }
        } else {
            memcpy(out + at, relative + i, part);
            at += part;
            out[at++] = '/';
        }
        i = end + 1;
    }
    if (at > root && out[at - 1] == '/' && len > 0 && !is_sep(relative[len - 1])) at--; // A file, not a folder
    out[at] = '\0';
    return out;
}

char *asset_path_folder(const char *file)
{
    size_t n = strlen(file);
    while (n > 0 && !is_sep(file[n - 1])) n--;
    char *out = copy_bytes(file, n);
    for (size_t i = 0; i < n; i++) {
        if (out[i] == '\\') out[i] = '/';
    }
    return out;
}

bool asset_path_same(const char *a, const char *b)
{
    for (;; a++, b++) {
        if (fold(*a) != fold(*b)) return false;
        if (*a == '\0') return true;
    }
}

bool asset_path_under(const char *path, const char *folder)
{
    size_t n = strlen(folder);
    while (n > 0 && is_sep(folder[n - 1])) n--;
    if (strlen(path) <= n + 1) return false;
    for (size_t i = 0; i < n; i++) {
        if (fold(path[i]) != fold(folder[i])) return false;
    }
    return is_sep(path[n]);
}

const char *asset_path_name(const char *path)
{
    const char *name = path;
    for (const char *p = path; *p; p++) {
        if (is_sep(*p) && p[1]) name = p + 1;
    }
    return name;
}

char *asset_path_relative(const char *from, const char *to)
{
    // The segments both start with
    size_t shared = 0; // Bytes of `from` shared, through a '/'
    for (size_t i = 0;; i++) {
        const bool from_end = from[i] == '\0';
        const bool to_end = to[i] == '\0';
        if ((from_end || is_sep(from[i])) && (to_end || is_sep(to[i]))) shared = i + (from_end ? 0 : 1);
        if (from_end || to_end || fold(from[i]) != fold(to[i])) break;
    }
    if (shared == 0) return copy_bytes(to, strlen(to)); // Another drive
    // Each folder of `from` past that is a '..'
    int ups = 0;
    for (size_t i = shared; from[i]; i++) {
        if (is_sep(from[i])) ups++;
    }
    if (from[strlen(from) - 1] != '/' && from[strlen(from) - 1] != '\\') ups++;
    const char *rest = to + shared;
    while (is_sep(*rest)) rest++;
    char *out = malloc((size_t)ups * 3 + strlen(rest) + 2);
    if (!out) abort();
    size_t at = 0;
    for (int i = 0; i < ups; i++) {
        memcpy(out + at, "../", 3);
        at += 3;
    }
    strcpy(out + at, rest);
    for (char *p = out; *p; p++) {
        if (*p == '\\') *p = '/';
    }
    if (at > 0 && *rest == '\0') out[at - 1] = '\0'; // The folder itself: ".."
    return out;
}

// ---------------------------------------------------------------------------

void asset_hash_of(const void *data, const size_t size, char out[65])
{
    sha256_hex(data, size, out);
}

static char *store_path(const char *game_folder)
{
    const size_t n = strlen(game_folder);
    const bool slash = n > 0 && is_sep(game_folder[n - 1]);
    char *out = malloc(n + sizeof ASSET_HASHES_FILE + 1);
    if (!out) abort();
    snprintf(out, n + sizeof ASSET_HASHES_FILE + 1, "%s%s" ASSET_HASHES_FILE, game_folder, slash ? "" : "/");
    return out;
}

static char *read_text(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *text = malloc((size_t)(size > 0 ? size : 0) + 1);
    if (!text) abort();
    const size_t n = fread(text, 1, (size_t)(size > 0 ? size : 0), f);
    fclose(f);
    text[n] = '\0';
    if (len) *len = n;
    return text;
}

static asset_hash *add(asset_hashes *h, const char *path)
{
    for (int i = 0; i < h->count; i++) {
        if (asset_path_same(h->items[i].path, path)) return &h->items[i];
    }
    if (h->count == h->cap) {
        h->cap = h->cap ? h->cap * 2 : 8;
        h->items = realloc(h->items, sizeof(asset_hash) * (size_t)h->cap);
        if (!h->items) abort();
    }
    asset_hash *item = &h->items[h->count++];
    item->path = copy_bytes(path, strlen(path));
    for (char *p = item->path; *p; p++) {
        if (*p == '\\') *p = '/';
    }
    item->hex[0] = '\0';
    item->size = 0;
    return item;
}

void asset_hashes_load(asset_hashes *h, const char *game_folder)
{
    memset(h, 0, sizeof *h);
    char *file = store_path(game_folder);
    char *text = read_text(file, NULL);
    free(file);
    if (!text) return;
    for (char *line = text; *line;) {
        char *end = line + strcspn(line, "\n");
        const char next = *end;
        *end = '\0';
        if (end > line && end[-1] == '\r') end[-1] = '\0';
        char hex[65];
        unsigned long long size;
        int path_at = 0;
        if (line[0] != '#' && sscanf(line, "%64s %llu %n", hex, &size, &path_at) == 2 && strlen(hex) == 64 && path_at > 0
            && line[path_at]) {
            asset_hash *item = add(h, line + path_at);
            memcpy(item->hex, hex, 65);
            item->size = size;
        }
        line = next ? end + 1 : end;
    }
    free(text);
}

void asset_hashes_free(asset_hashes *h)
{
    for (int i = 0; i < h->count; i++) free(h->items[i].path);
    free(h->items);
    memset(h, 0, sizeof *h);
}

void asset_hashes_note(asset_hashes *h, const char *path, const void *data, const size_t size)
{
    asset_hash *item = add(h, path);
    sha256_hex(data, size, item->hex);
    item->size = size;
}

void asset_hashes_set(asset_hashes *h, const char *path, const char hex[65], const unsigned long long size)
{
    asset_hash *item = add(h, path);
    memcpy(item->hex, hex, 65);
    item->size = size;
}

void asset_hashes_keep(asset_hashes *h, const asset_hashes *from, const char *path)
{
    const asset_hash *known = asset_hashes_find(from, path);
    if (!known) return;
    asset_hash *item = add(h, path);
    memcpy(item->hex, known->hex, 65);
    item->size = known->size;
}

const asset_hash *asset_hashes_find(const asset_hashes *h, const char *path)
{
    for (int i = 0; i < h->count; i++) {
        if (asset_path_same(h->items[i].path, path)) return &h->items[i];
    }
    return NULL;
}

static int by_path(const void *a, const void *b)
{
    return strcmp(((const asset_hash *)a)->path, ((const asset_hash *)b)->path);
}

char *asset_hashes_text(const asset_hashes *h)
{
    static const char header[] = "# The SHA-256 of each file an asset names, so one that moves is found again. Made by tide.\n";
    asset_hash *sorted = malloc(sizeof(asset_hash) * (size_t)(h->count > 0 ? h->count : 1));
    if (!sorted) abort();
    memcpy(sorted, h->items, sizeof(asset_hash) * (size_t)h->count);
    qsort(sorted, (size_t)h->count, sizeof(asset_hash), by_path);
    size_t cap = sizeof header;
    for (int i = 0; i < h->count; i++) cap += 64 + 1 + 20 + 1 + strlen(sorted[i].path) + 1;
    char *text = malloc(cap + 1);
    if (!text) abort();
    size_t at = (size_t)snprintf(text, cap + 1, "%s", header);
    for (int i = 0; i < h->count; i++) {
        at += (size_t)snprintf(text + at, cap + 1 - at, "%s %llu %s\n", sorted[i].hex, sorted[i].size, sorted[i].path);
    }
    free(sorted);
    return text;
}

static void make_folder(const char *path)
{
#ifdef _WIN32
    _mkdir(path);
    const DWORD attributes = GetFileAttributesA(path);
    if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_HIDDEN)) {
        SetFileAttributesA(path, attributes | FILE_ATTRIBUTE_HIDDEN);
    }
#else
    mkdir(path, 0777);
#endif
}

static bool write_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    const bool ok = fputs(text, f) >= 0;
    return fclose(f) == 0 && ok;
}

bool asset_hashes_save(const asset_hashes *h, const char *game_folder)
{
    char *file = store_path(game_folder);
    char *was = read_text(file, NULL);
    if (h->count == 0 && !was) {
        free(file);
        return false;
    }
    char *text = asset_hashes_text(h);
    const bool same = was && strcmp(was, text) == 0;
    free(was);
    bool wrote = false;
    if (!same) {
        // .tide/ as tide makes it: hidden, and ignored by git
        char *folder = asset_path_folder(file);
        folder[strlen(folder) - 1] = '\0';
        make_folder(folder);
        char *ignore = malloc(strlen(folder) + sizeof "/.gitignore");
        if (!ignore) abort();
        sprintf(ignore, "%s/.gitignore", folder);
        FILE *f = fopen(ignore, "rb");
        if (f) fclose(f);
        else write_text(ignore, "# Made by tide; safe to delete.\n*\n");
        free(ignore);
        free(folder);
        wrote = write_text(file, text);
    }
    free(text);
    free(file);
    return wrote;
}
